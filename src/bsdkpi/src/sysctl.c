// SPDX-License-Identifier: MIT
/*
 * sysctl(9): a tree of OIDs with handlers, no sysctl(2).  Static
 * declarations register through constructors (sys/sysctl.h), dynamic ones
 * through sysctl_add_oid(); bsdkpi_sysctl_find()/bsdkpi_sysctl_invoke() let
 * a debug user client read and write them by "net.wlan.0.debug"-style path.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/sbuf.h>
#include <sys/counter.h>
#include <sys/proc.h>

struct sysctl_oid_list sysctl__children = SLIST_HEAD_INITIALIZER(sysctl__children);

#define BSDKPI_SYSCTL_ROOT(name, descr)					\
	struct sysctl_oid sysctl__##name = {				\
		.oid_children = SLIST_HEAD_INITIALIZER(sysctl__##name.oid_children), \
		.oid_parent = &sysctl__children,			\
		.oid_number = OID_AUTO,					\
		.oid_kind = CTLTYPE_NODE | CTLFLAG_RW,			\
		.oid_name = &(#name)[1],				\
		.oid_fmt = "N",						\
		.oid_descr = descr,					\
	};								\
	__attribute__((constructor)) static void			\
	sysctl__##name##_register(void)					\
	{								\
		sysctl_register_oid(&sysctl__##name);			\
	}

BSDKPI_SYSCTL_ROOT(_kern, "High kernel, proc, limits &c")
BSDKPI_SYSCTL_ROOT(_net, "Network, (see socket.h)")
BSDKPI_SYSCTL_ROOT(_debug, "Debugging")
BSDKPI_SYSCTL_ROOT(_hw, "hardware")
BSDKPI_SYSCTL_ROOT(_dev, "device driver information")
BSDKPI_SYSCTL_ROOT(_compat, "Compatibility code")

static int sysctl_next_auto = CTL_AUTO_START;

/*
 * Registration happens from constructors (no lock available, single
 * threaded) and from drivers at attach time; a spinlock covers the latter.
 */
static DEFINE_SPINLOCK(sysctl_lock);

void
sysctl_register_oid(struct sysctl_oid *oidp)
{
	struct sysctl_oid_list *parent = oidp->oid_parent;
	struct sysctl_oid *p;
	unsigned long flags;

	spin_lock_irqsave(&sysctl_lock, flags);
	if (oidp->oid_number == OID_AUTO)
		oidp->oid_number = sysctl_next_auto++;
	/* Replace an existing entry of the same name (static then dynamic). */
	SLIST_FOREACH(p, parent, oid_link) {
		if (p == oidp) {
			spin_unlock_irqrestore(&sysctl_lock, flags);
			return;
		}
	}
	SLIST_INSERT_HEAD(parent, oidp, oid_link);
	spin_unlock_irqrestore(&sysctl_lock, flags);
}

void
sysctl_unregister_oid(struct sysctl_oid *oidp)
{
	unsigned long flags;

	spin_lock_irqsave(&sysctl_lock, flags);
	if (oidp->oid_parent != NULL)
		SLIST_REMOVE(oidp->oid_parent, oidp, sysctl_oid, oid_link);
	spin_unlock_irqrestore(&sysctl_lock, flags);
}

int
sysctl_remove_oid(struct sysctl_oid *oidp, int del, int recurse)
{
	struct sysctl_oid *p, *tmp;

	if (oidp == NULL)
		return (EINVAL);
	if ((oidp->oid_kind & CTLFLAG_DYN) == 0)
		return (EINVAL);
	if ((oidp->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
		if (!SLIST_EMPTY(&oidp->oid_children) && !recurse)
			return (ENOTEMPTY);
		SLIST_FOREACH_SAFE(p, &oidp->oid_children, oid_link, tmp)
			(void)sysctl_remove_oid(p, del, recurse);
	}
	sysctl_unregister_oid(oidp);
	if (del) {
		free(__DECONST(char *, oidp->oid_name), M_TEMP);
		free(oidp, M_TEMP);
	}
	return (0);
}

void
sysctl_ctx_init(struct sysctl_ctx_list *clist)
{
	TAILQ_INIT(clist);
}

int
sysctl_ctx_free(struct sysctl_ctx_list *clist)
{
	struct sysctl_ctx_entry *e, *tmp;

	/* Entries were added head-first, so the children come before the
	 * parents; remove in that order. */
	TAILQ_FOREACH_SAFE(e, clist, link, tmp) {
		TAILQ_REMOVE(clist, e, link);
		(void)sysctl_remove_oid(e->entry, 1, 0);
		free(e, M_TEMP);
	}
	return (0);
}

struct sysctl_oid *
sysctl_add_oid(struct sysctl_ctx_list *clist, struct sysctl_oid_list *parent,
    int nbr, const char *name, int kind, void *arg1, intmax_t arg2,
    int (*handler)(SYSCTL_HANDLER_ARGS), const char *fmt, const char *descr,
    const char *label)
{
	struct sysctl_oid *oidp;
	struct sysctl_ctx_entry *e;
	char *dname;

	oidp = malloc(sizeof(*oidp), M_TEMP, M_WAITOK | M_ZERO);
	if (oidp == NULL)
		return (NULL);
	dname = malloc(strlen(name) + 1, M_TEMP, M_WAITOK);
	if (dname == NULL) {
		free(oidp, M_TEMP);
		return (NULL);
	}
	strcpy(dname, name);
	SLIST_INIT(&oidp->oid_children);
	oidp->oid_parent = parent;
	oidp->oid_number = nbr;
	oidp->oid_kind = (u_int)kind | CTLFLAG_DYN;
	oidp->oid_arg1 = arg1;
	oidp->oid_arg2 = arg2;
	oidp->oid_name = dname;
	oidp->oid_handler = handler;
	oidp->oid_fmt = fmt;
	oidp->oid_descr = descr;
	oidp->oid_label = label;
	if (clist != NULL) {
		e = malloc(sizeof(*e), M_TEMP, M_WAITOK | M_ZERO);
		if (e == NULL) {
			free(dname, M_TEMP);
			free(oidp, M_TEMP);
			return (NULL);
		}
		e->entry = oidp;
		TAILQ_INSERT_HEAD(clist, e, link);
	}
	sysctl_register_oid(oidp);
	return (oidp);
}

/* ---- request I/O ---------------------------------------------------- */

static int
sysctl_old_kernel(struct sysctl_req *req, const void *p, size_t l)
{
	size_t i = 0;

	if (req->oldptr != NULL) {
		i = l;
		if (req->oldlen <= req->oldidx)
			i = 0;
		else if (i > req->oldlen - req->oldidx)
			i = req->oldlen - req->oldidx;
		if (i > 0)
			memcpy((char *)req->oldptr + req->oldidx, p, i);
	}
	req->oldidx += l;
	if (req->oldptr != NULL && i != l)
		return (ENOMEM);
	return (0);
}

static int
sysctl_new_kernel(struct sysctl_req *req, void *p, size_t l)
{
	if (req->newptr == NULL)
		return (0);
	if (req->newlen - req->newidx < l)
		return (EINVAL);
	memcpy(p, (const char *)req->newptr + req->newidx, l);
	req->newidx += l;
	return (0);
}

int
sysctl_wire_old_buffer(struct sysctl_req *req, size_t len)
{
	(void)req;
	(void)len;
	return (0);
}

/* ---- the standard handlers ------------------------------------------ */

int
sysctl_handle_bool(SYSCTL_HANDLER_ARGS)
{
	uint8_t temp;
	int error;

	(void)oidp;
	if (arg1 != NULL)
		temp = *(bool *)arg1 ? 1 : 0;
	else
		temp = arg2 ? 1 : 0;
	error = SYSCTL_OUT(req, &temp, sizeof(temp));
	if (error || !req->newptr)
		return (error);
	error = SYSCTL_IN(req, &temp, sizeof(temp));
	if (!error) {
		if (arg1 != NULL)
			*(bool *)arg1 = temp ? 1 : 0;
		else
			error = EPERM;
	}
	return (error);
}

int
sysctl_handle_8(SYSCTL_HANDLER_ARGS)
{
	int8_t tmpout;
	int error = 0;

	(void)oidp;
	if (arg1 != NULL)
		tmpout = *(int8_t *)arg1;
	else
		tmpout = (int8_t)arg2;
	error = SYSCTL_OUT(req, &tmpout, sizeof(tmpout));
	if (error || !req->newptr)
		return (error);
	if (arg1 == NULL)
		error = EPERM;
	else
		error = SYSCTL_IN(req, arg1, sizeof(tmpout));
	return (error);
}

int
sysctl_handle_16(SYSCTL_HANDLER_ARGS)
{
	int16_t tmpout;
	int error = 0;

	(void)oidp;
	if (arg1 != NULL)
		tmpout = *(int16_t *)arg1;
	else
		tmpout = (int16_t)arg2;
	error = SYSCTL_OUT(req, &tmpout, sizeof(tmpout));
	if (error || !req->newptr)
		return (error);
	if (arg1 == NULL)
		error = EPERM;
	else
		error = SYSCTL_IN(req, arg1, sizeof(tmpout));
	return (error);
}

int
sysctl_handle_32(SYSCTL_HANDLER_ARGS)
{
	int32_t tmpout;
	int error = 0;

	(void)oidp;
	if (arg1 != NULL)
		tmpout = *(int32_t *)arg1;
	else
		tmpout = (int32_t)arg2;
	error = SYSCTL_OUT(req, &tmpout, sizeof(tmpout));
	if (error || !req->newptr)
		return (error);
	if (arg1 == NULL)
		error = EPERM;
	else
		error = SYSCTL_IN(req, arg1, sizeof(tmpout));
	return (error);
}

int
sysctl_handle_int(SYSCTL_HANDLER_ARGS)
{
	int tmpout, error = 0;

	(void)oidp;
	if (arg1 != NULL)
		tmpout = *(int *)arg1;
	else
		tmpout = (int)arg2;
	error = SYSCTL_OUT(req, &tmpout, sizeof(int));
	if (error || !req->newptr)
		return (error);
	if (arg1 == NULL)
		error = EPERM;
	else
		error = SYSCTL_IN(req, arg1, sizeof(int));
	return (error);
}

int
sysctl_handle_long(SYSCTL_HANDLER_ARGS)
{
	long tmplong;
	int error = 0;

	(void)oidp;
	if (arg1 != NULL)
		tmplong = *(long *)arg1;
	else
		tmplong = (long)arg2;
	error = SYSCTL_OUT(req, &tmplong, sizeof(long));
	if (error || !req->newptr)
		return (error);
	if (arg1 == NULL)
		error = EPERM;
	else
		error = SYSCTL_IN(req, arg1, sizeof(long));
	return (error);
}

int
sysctl_handle_64(SYSCTL_HANDLER_ARGS)
{
	uint64_t tmpout;
	int error = 0;

	(void)oidp;
	if (arg1 != NULL)
		tmpout = *(uint64_t *)arg1;
	else
		tmpout = (uint64_t)arg2;
	error = SYSCTL_OUT(req, &tmpout, sizeof(uint64_t));
	if (error || !req->newptr)
		return (error);
	if (arg1 == NULL)
		error = EPERM;
	else
		error = SYSCTL_IN(req, arg1, sizeof(uint64_t));
	return (error);
}

int
sysctl_handle_string(SYSCTL_HANDLER_ARGS)
{
	char *tmparg;
	size_t outlen;
	int error = 0;

	(void)oidp;
	if (arg2 == 0 || arg2 > (intmax_t)SIZE_MAX / 2) {
		tmparg = arg1;
		outlen = strlen(tmparg) + 1;
	} else {
		outlen = strnlen((const char *)arg1, (size_t)arg2) + 1;
		tmparg = arg1;
	}
	error = SYSCTL_OUT(req, tmparg, outlen);
	if (error || !req->newptr)
		return (error);
	if (req->newlen - req->newidx >= (size_t)arg2 ||
	    req->newlen - req->newidx < 0) {
		error = EINVAL;
	} else if (req->newlen - req->newidx == 0) {
		((char *)arg1)[0] = '\0';
	} else {
		error = SYSCTL_IN(req, arg1, req->newlen - req->newidx);
		if (error == 0)
			((char *)arg1)[req->newidx] = '\0';
	}
	return (error);
}

int
sysctl_handle_opaque(SYSCTL_HANDLER_ARGS)
{
	int error;

	(void)oidp;
	error = SYSCTL_OUT(req, arg1, (size_t)arg2);
	if (error || !req->newptr)
		return (error);
	error = SYSCTL_IN(req, arg1, (size_t)arg2);
	return (error);
}

int
sysctl_handle_counter_u64(SYSCTL_HANDLER_ARGS)
{
	uint64_t out;
	int error;

	(void)oidp;
	(void)arg2;
	out = counter_u64_fetch(*(counter_u64_t *)arg1);
	error = SYSCTL_OUT(req, &out, sizeof(uint64_t));
	if (error || !req->newptr)
		return (error);
	/* Any write zeroes the counter. */
	counter_u64_zero(*(counter_u64_t *)arg1);
	return (0);
}

int
sysctl_handle_counter_u64_array(SYSCTL_HANDLER_ARGS)
{
	uint64_t *out;
	int error;
	size_t n = (size_t)arg2;

	(void)oidp;
	out = malloc(n * sizeof(uint64_t), M_TEMP, M_WAITOK);
	if (out == NULL)
		return (ENOMEM);
	for (size_t i = 0; i < n; i++)
		out[i] = counter_u64_fetch(((counter_u64_t *)arg1)[i]);
	error = SYSCTL_OUT(req, out, n * sizeof(uint64_t));
	free(out, M_TEMP);
	if (error || !req->newptr)
		return (error);
	for (size_t i = 0; i < n; i++)
		counter_u64_zero(((counter_u64_t *)arg1)[i]);
	return (0);
}

/* ---- lookup and invocation ------------------------------------------ */

struct sysctl_oid *
bsdkpi_sysctl_find(const char *path)
{
	struct sysctl_oid_list *list = &sysctl__children;
	struct sysctl_oid *oid = NULL, *p;
	const char *s = path;
	size_t n;

	while (*s != '\0') {
		n = 0;
		while (s[n] != '\0' && s[n] != '.')
			n++;
		oid = NULL;
		SLIST_FOREACH(p, list, oid_link) {
			if (strlen(p->oid_name) == n &&
			    strncmp(p->oid_name, s, n) == 0) {
				oid = p;
				break;
			}
		}
		if (oid == NULL)
			return (NULL);
		s += n;
		if (*s == '.')
			s++;
		list = &oid->oid_children;
	}
	return (oid);
}

int
bsdkpi_sysctl_invoke(struct sysctl_oid *oid, void *oldp, size_t *oldlenp,
    const void *newp, size_t newlen)
{
	struct sysctl_req req;
	int error;

	if (oid == NULL)
		return (ENOENT);
	if (oid->oid_handler == NULL)
		return (EISDIR);
	if (newp != NULL && (oid->oid_kind & CTLFLAG_WR) == 0)
		return (EPERM);
	memset(&req, 0, sizeof(req));
	req.td = curthread;
	req.oldptr = oldp;
	req.oldlen = oldlenp != NULL ? *oldlenp : 0;
	req.oldfunc = sysctl_old_kernel;
	req.newptr = newp;
	req.newlen = newlen;
	req.newfunc = sysctl_new_kernel;
	error = oid->oid_handler(oid, oid->oid_arg1, oid->oid_arg2, &req);
	if (oldlenp != NULL)
		*oldlenp = req.oldidx;
	return (error);
}
