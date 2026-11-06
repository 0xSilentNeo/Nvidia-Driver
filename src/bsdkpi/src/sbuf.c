// SPDX-License-Identifier: MIT
/*
 * sbuf(9): safe string buffers with optional auto-extension and a drain
 * callback (used by sysctl handlers).
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>

#define SBUF_ISDYNAMIC(s)	((s)->s_flags & SBUF_DYNAMIC)
#define SBUF_ISDYNSTRUCT(s)	((s)->s_flags & SBUF_DYNSTRUCT)
#define SBUF_ISFINISHED(s)	((s)->s_flags & SBUF_FINISHED)
#define SBUF_HASROOM(s)		((s)->s_len < (s)->s_size - 1)
#define SBUF_FREESPACE(s)	((s)->s_size - ((s)->s_len + 1))
#define SBUF_CANEXTEND(s)	((s)->s_flags & SBUF_AUTOEXTEND)
#define SBUF_ISSECTION(s)	((s)->s_flags & SBUF_INSECTION)
#define SBUF_NULINCLUDED(s)	((s)->s_flags & SBUF_INCLUDENUL)
#define SBUF_ISDRAINTOEOR(s)	((s)->s_flags & SBUF_DRAINTOEOR)
#define SBUF_DODRAINTOEOR(s)	(SBUF_ISSECTION(s) && SBUF_ISDRAINTOEOR(s))
#define SBUF_MALLOCFLAG(s)	(((s)->s_flags & SBUF_NOWAIT) ? M_NOWAIT : M_WAITOK)

#define SBUF_MINSIZE		 2
#define SBUF_MINEXTENDSIZE	16
#define SBUF_MAXEXTENDSIZE	PAGE_SIZE
#define SBUF_MAXEXTENDINCR	PAGE_SIZE

static int
sbuf_extendsize(int size)
{
	int newsize;

	if (size < (int)SBUF_MAXEXTENDSIZE) {
		newsize = SBUF_MINEXTENDSIZE;
		while (newsize < size)
			newsize *= 2;
	} else {
		newsize = roundup2(size, SBUF_MAXEXTENDINCR);
	}
	return (newsize);
}

static int
sbuf_extend(struct sbuf *s, int addlen)
{
	char *newbuf;
	int newsize;

	if (!SBUF_CANEXTEND(s))
		return (-1);
	newsize = sbuf_extendsize((int)s->s_size + addlen);
	newbuf = malloc(newsize, M_TEMP, SBUF_MALLOCFLAG(s));
	if (newbuf == NULL)
		return (-1);
	memcpy(newbuf, s->s_buf, (size_t)s->s_size);
	if (SBUF_ISDYNAMIC(s))
		free(s->s_buf, M_TEMP);
	else
		s->s_flags |= SBUF_DYNAMIC;
	s->s_buf = newbuf;
	s->s_size = newsize;
	return (0);
}

static struct sbuf *
sbuf_newbuf(struct sbuf *s, char *buf, int length, int flags)
{
	memset(s, 0, sizeof(*s));
	s->s_flags |= (flags & SBUF_USRFLAGMSK);
	s->s_size = length;
	s->s_buf = buf;
	s->s_rec_off = -1;

	if ((s->s_flags & SBUF_AUTOEXTEND) == 0) {
		KASSERT(s->s_size >= SBUF_MINSIZE,
		    ("attempt to create an sbuf smaller than %d bytes",
		    SBUF_MINSIZE));
	}
	if (s->s_buf != NULL)
		return (s);
	if ((flags & SBUF_AUTOEXTEND) != 0)
		s->s_size = sbuf_extendsize((int)s->s_size);
	s->s_buf = malloc((size_t)s->s_size, M_TEMP, SBUF_MALLOCFLAG(s));
	if (s->s_buf == NULL)
		return (NULL);
	s->s_flags |= SBUF_DYNAMIC;
	return (s);
}

struct sbuf *
sbuf_new(struct sbuf *s, char *buf, int length, int flags)
{
	KASSERT(length >= 0, ("attempt to create an sbuf of negative length (%d)", length));
	KASSERT((flags & ~SBUF_USRFLAGMSK) == 0, ("%s called with invalid flags", __func__));
	flags &= SBUF_USRFLAGMSK;
	if (s != NULL)
		return (sbuf_newbuf(s, buf, length, flags));
	s = malloc(sizeof(*s), M_TEMP, (flags & SBUF_NOWAIT) ? M_NOWAIT : M_WAITOK);
	if (s == NULL)
		return (NULL);
	if (sbuf_newbuf(s, buf, length, flags) == NULL) {
		free(s, M_TEMP);
		return (NULL);
	}
	s->s_flags |= SBUF_DYNSTRUCT;
	return (s);
}

void
sbuf_clear(struct sbuf *s)
{
	s->s_flags &= ~SBUF_FINISHED;
	s->s_error = 0;
	s->s_len = 0;
	s->s_rec_off = -1;
	s->s_sect_len = 0;
}

int
sbuf_setpos(struct sbuf *s, ssize_t pos)
{
	KASSERT(pos >= 0, ("attempt to seek to a negative position (%jd)", (intmax_t)pos));
	KASSERT(pos < s->s_size, ("attempt to seek past end of sbuf (%jd >= %jd)",
	    (intmax_t)pos, (intmax_t)s->s_size));
	KASSERT(!SBUF_ISSECTION(s), ("attempt to seek when in a section"));
	if (pos < 0 || pos > s->s_len)
		return (-1);
	s->s_len = pos;
	return (0);
}

void
sbuf_set_drain(struct sbuf *s, sbuf_drain_func *func, void *ctx)
{
	KASSERT((s->s_drain_func == NULL && s->s_drain_arg == NULL) ||
	    (func == s->s_drain_func && ctx == s->s_drain_arg),
	    ("Cannot change drain to %p(%p) from %p(%p)",
	    func, ctx, s->s_drain_func, s->s_drain_arg));
	s->s_drain_func = func;
	s->s_drain_arg = ctx;
}

static int
sbuf_drain(struct sbuf *s)
{
	int len;

	if (s->s_len == 0 || (SBUF_DODRAINTOEOR(s) && s->s_rec_off == 0))
		return (0);
	if (SBUF_DODRAINTOEOR(s))
		len = s->s_drain_func(s->s_drain_arg, s->s_buf, (int)s->s_rec_off);
	else
		len = s->s_drain_func(s->s_drain_arg, s->s_buf, (int)s->s_len);
	if (len <= 0) {
		s->s_error = len ? -len : EDEADLK;
		return (s->s_error);
	}
	/* Shift the data around if the drain didn't take everything. */
	s->s_len -= len;
	if (s->s_rec_off != -1)
		s->s_rec_off -= len;
	if (s->s_len > 0)
		memmove(s->s_buf, s->s_buf + len, (size_t)s->s_len);
	return (0);
}

static void
sbuf_put_byte(struct sbuf *s, char c)
{
	if (__predict_false(s->s_error != 0))
		return;
	if (__predict_false(!SBUF_HASROOM(s))) {
		if (s->s_drain_func != NULL) {
			if (sbuf_drain(s) != 0)
				return;
		} else if (sbuf_extend(s, 1) < 0) {
			s->s_error = ENOMEM;
			return;
		}
	}
	s->s_buf[s->s_len++] = c;
	if (SBUF_ISSECTION(s))
		s->s_sect_len++;
}

int
sbuf_bcat(struct sbuf *s, const void *buf, size_t len)
{
	const char *str = buf;
	const char *end = str + len;

	if (s->s_error != 0)
		return (-1);
	for (; str < end; str++) {
		sbuf_put_byte(s, *str);
		if (s->s_error != 0)
			return (-1);
	}
	return (0);
}

int
sbuf_bcopyin(struct sbuf *s, const void *uaddr, size_t len)
{
	return (sbuf_bcat(s, uaddr, len));
}

int
sbuf_bcpy(struct sbuf *s, const void *buf, size_t len)
{
	sbuf_clear(s);
	return (sbuf_bcat(s, buf, len));
}

int
sbuf_cat(struct sbuf *s, const char *str)
{
	if (s->s_error != 0)
		return (-1);
	while (*str != '\0') {
		sbuf_put_byte(s, *str++);
		if (s->s_error != 0)
			return (-1);
	}
	return (0);
}

int
sbuf_copyin(struct sbuf *s, const void *uaddr, size_t len)
{
	const char *str = uaddr;
	size_t n;

	if (len == 0)
		len = strlen(str);
	n = strnlen(str, len);
	return (sbuf_bcat(s, str, n));
}

int
sbuf_cpy(struct sbuf *s, const char *str)
{
	sbuf_clear(s);
	return (sbuf_cat(s, str));
}

static void
sbuf_putc_func(int c, void *arg)
{
	if (c != '\0')
		sbuf_put_byte((struct sbuf *)arg, (char)c);
}

int
sbuf_vprintf(struct sbuf *s, const char *fmt, va_list ap)
{
	KASSERT(fmt != NULL, ("%s called with a NULL format string", __func__));
	if (s->s_error != 0)
		return (-1);
	(void)kvprintf(fmt, sbuf_putc_func, s, 10, ap);
	if (s->s_error != 0)
		return (-1);
	return (0);
}

int
sbuf_printf(struct sbuf *s, const char *fmt, ...)
{
	va_list ap;
	int result;

	va_start(ap, fmt);
	result = sbuf_vprintf(s, fmt, ap);
	va_end(ap);
	return (result);
}

int
sbuf_putc(struct sbuf *s, int c)
{
	sbuf_put_byte(s, (char)c);
	if (s->s_error != 0)
		return (-1);
	return (0);
}

int
sbuf_nl_terminate(struct sbuf *s)
{
	if (s->s_len == 0 || s->s_buf[s->s_len - 1] != '\n')
		sbuf_put_byte(s, '\n');
	if (s->s_error != 0)
		return (-1);
	return (0);
}

int
sbuf_trim(struct sbuf *s)
{
	KASSERT(s->s_drain_func == NULL, ("%s makes no sense on sbuf %p with drain", __func__, s));
	if (s->s_error != 0)
		return (-1);
	while (s->s_len > 0 && (s->s_buf[s->s_len - 1] == ' ' ||
	    s->s_buf[s->s_len - 1] == '\t' || s->s_buf[s->s_len - 1] == '\n')) {
		--s->s_len;
		if (SBUF_ISSECTION(s))
			s->s_sect_len--;
	}
	return (0);
}

int
sbuf_error(const struct sbuf *s)
{
	return (s->s_error);
}

int
sbuf_finish(struct sbuf *s)
{
	if (s->s_drain_func != NULL) {
		while (s->s_len > 0 && s->s_error == 0)
			s->s_error = sbuf_drain(s);
	} else if (SBUF_NULINCLUDED(s)) {
		if (s->s_error == 0)
			s->s_buf[s->s_len++] = '\0';
	} else {
		s->s_buf[s->s_len] = '\0';
	}
	s->s_flags |= SBUF_FINISHED;
	return (s->s_error);
}

char *
sbuf_data(struct sbuf *s)
{
	KASSERT(SBUF_ISFINISHED(s), ("%s called on an unfinished sbuf", __func__));
	KASSERT(s->s_drain_func == NULL, ("%s makes no sense on sbuf %p with drain", __func__, s));
	return (s->s_buf);
}

ssize_t
sbuf_len(struct sbuf *s)
{
	KASSERT(s->s_drain_func == NULL, ("%s makes no sense on sbuf %p with drain", __func__, s));
	if (s->s_error != 0)
		return (-1);
	/* If finished, nulterm is already in len, else add one. */
	if (SBUF_NULINCLUDED(s) && !SBUF_ISFINISHED(s))
		return (s->s_len + 1);
	return (s->s_len);
}

int
sbuf_done(const struct sbuf *s)
{
	return (SBUF_ISFINISHED(s));
}

void
sbuf_delete(struct sbuf *s)
{
	int isdyn;

	if (SBUF_ISDYNAMIC(s))
		free(s->s_buf, M_TEMP);
	isdyn = SBUF_ISDYNSTRUCT(s);
	memset(s, 0, sizeof(*s));
	if (isdyn)
		free(s, M_TEMP);
}

void
sbuf_start_section(struct sbuf *s, ssize_t *old_lenp)
{
	if (!SBUF_ISSECTION(s)) {
		KASSERT(s->s_sect_len == 0, ("s_sect_len != 0 when starting a section"));
		if (old_lenp != NULL)
			*old_lenp = -1;
		s->s_rec_off = s->s_len;
		s->s_flags |= SBUF_INSECTION;
	} else {
		KASSERT(old_lenp != NULL, ("s_sect_len should be saved when starting a subsection"));
		*old_lenp = s->s_sect_len;
		s->s_sect_len = 0;
	}
}

ssize_t
sbuf_end_section(struct sbuf *s, ssize_t old_len, size_t pad, int c)
{
	ssize_t len;

	KASSERT(SBUF_ISSECTION(s), ("attempt to end a section when not in a section"));
	if (pad > 1) {
		len = roundup((ssize_t)s->s_sect_len, (ssize_t)pad) - s->s_sect_len;
		for (; s->s_error == 0 && len > 0; len--)
			sbuf_put_byte(s, (char)c);
	}
	len = s->s_sect_len;
	if (old_len == -1) {
		s->s_rec_off = -1;
		s->s_sect_len = 0;
		s->s_flags &= ~SBUF_INSECTION;
	} else {
		s->s_sect_len += (int)old_len;
	}
	if (s->s_error != 0)
		return (-1);
	return (len);
}

void
sbuf_hexdump(struct sbuf *sb, const void *ptr, int length, const char *hdr,
    int flags)
{
	int i, j, k;
	int cols;
	const unsigned char *cp;
	char delim;

	if ((flags & HD_DELIM_MASK) != 0)
		delim = (flags & HD_DELIM_MASK) >> 8;
	else
		delim = ' ';

	if ((flags & HD_COLUMN_MASK) != 0)
		cols = flags & HD_COLUMN_MASK;
	else
		cols = 16;

	cp = ptr;
	for (i = 0; i < length; i += cols) {
		if (hdr != NULL)
			sbuf_printf(sb, "%s", hdr);

		if ((flags & HD_OMIT_COUNT) == 0)
			sbuf_printf(sb, "%04x  ", i);

		if ((flags & HD_OMIT_HEX) == 0) {
			for (j = 0; j < cols; j++) {
				k = i + j;
				if (k < length)
					sbuf_printf(sb, "%c%02x", delim, cp[k]);
				else
					sbuf_printf(sb, "   ");
			}
		}

		if ((flags & HD_OMIT_CHARS) == 0) {
			sbuf_printf(sb, "  |");
			for (j = 0; j < cols; j++) {
				k = i + j;
				if (k >= length)
					sbuf_printf(sb, " ");
				else if (cp[k] >= ' ' && cp[k] <= '~')
					sbuf_printf(sb, "%c", cp[k]);
				else
					sbuf_printf(sb, ".");
			}
			sbuf_printf(sb, "|");
		}
		sbuf_printf(sb, "\n");
	}
}

int
sbuf_count_drain(void *arg, const char *data, int len)
{
	size_t *sizep = arg;

	(void)data;
	*sizep += (size_t)len;
	return (len);
}

int
sbuf_printf_drain(void *arg, const char *data, int len)
{
	size_t *retvalptr = arg;
	int r;

	r = printf("%.*s", len, data);
	if (r < 0)
		return (-EIO);
	if (retvalptr != NULL)
		*retvalptr += (size_t)r;
	return (r);
}

void
sbuf_putbuf(struct sbuf *s)
{
	printf("%s", sbuf_data(s));
}

void
sbuf_set_flags(struct sbuf *s, int flags)
{
	s->s_flags |= (flags & SBUF_USRFLAGMSK);
}

void
sbuf_clear_flags(struct sbuf *s, int flags)
{
	s->s_flags &= ~(flags & SBUF_USRFLAGMSK);
}

int
sbuf_get_flags(struct sbuf *s)
{
	return (s->s_flags & SBUF_USRFLAGMSK);
}

/* ---- sysctl glue ---------------------------------------------------- */

static int
sbuf_sysctl_drain(void *arg, const char *data, int len)
{
	struct sysctl_req *req = arg;
	int error;

	error = SYSCTL_OUT(req, data, (size_t)len);
	KASSERT(error >= 0, ("Got unexpected negative value %d", error));
	return (error == 0 ? len : -error);
}

struct sbuf *
sbuf_new_for_sysctl(struct sbuf *s, char *buf, int length,
    struct sysctl_req *req)
{
	/* Supply a minimum buffer size if none was given. */
	if (buf == NULL && length == 0)
		length = 64;
	s = sbuf_new(s, buf, length, SBUF_FIXEDLEN | SBUF_INCLUDENUL);
	if (s != NULL)
		sbuf_set_drain(s, sbuf_sysctl_drain, req);
	return (s);
}
