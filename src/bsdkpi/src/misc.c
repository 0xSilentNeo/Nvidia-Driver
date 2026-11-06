// SPDX-License-Identifier: MIT
/*
 * Odds and ends of the FreeBSD kernel API: malloc(9) on kmalloc, the clock
 * variables, counter(9), priv/proc placeholders, bpf(9) and route(9) stubs,
 * epoch callbacks, libkern leftovers, and bsdkpi_init()/bsdkpi_fini().
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/counter.h>
#include <sys/epoch.h>
#include <sys/proc.h>
#include <sys/pcpu.h>
#include <sys/time.h>
#include <sys/mbuf.h>
#include <sys/callout.h>
#include <sys/taskqueue.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/bpf.h>
#include <net/route.h>
#include <net/vnet.h>
#include <bsdkpi/bsdkpi.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>

/* ---- clock ---------------------------------------------------------- */

int hz = HZ;
int tick = 1000000 / HZ;
struct timeval boottime;
int mp_ncpus = 1;
int mp_maxid = 0;
int ngroups_max = 1023;
int maxproc = 1, maxusers = 1, maxfiles = 1;
unsigned long physmem;

/* ---- proc placeholders ---------------------------------------------- */

static struct ucred bsdkpi_cred0;
struct proc bsdkpi_proc0 = { .p_pid = 0, .p_ucred = &bsdkpi_cred0, .p_comm = "kernel" };
struct thread bsdkpi_thread0 = {
	.td_proc = &bsdkpi_proc0, .td_ucred = &bsdkpi_cred0, .td_name = "kernel",
};
struct vnet bsdkpi_vnet0;
struct vnet *vnet0 = &bsdkpi_vnet0;

/* ---- malloc(9) ------------------------------------------------------ */

MALLOC_DEFINE(M_CACHE, "cache", "Various Dynamically allocated caches");
MALLOC_DEFINE(M_DEVBUF, "devbuf", "device driver memory");
MALLOC_DEFINE(M_TEMP, "temp", "misc temporary data buffers");
MALLOC_DEFINE(M_IFADDR, "ifaddr", "interface address");
MALLOC_DEFINE(M_IFMADDR, "ether_multi", "link-level multicast address");

gfp_t
bsdkpi_malloc_gfp(int flags)
{
	gfp_t gfp;

	gfp = (flags & M_WAITOK) ? GFP_KERNEL : GFP_ATOMIC;
	if (flags & M_ZERO)
		gfp |= __GFP_ZERO;
	return (gfp);
}

static void
malloc_type_account(struct malloc_type *type, long bytes, long count)
{
	if (type == NULL)
		return;
	__atomic_fetch_add(&type->ks_memuse, bytes, __ATOMIC_RELAXED);
	__atomic_fetch_add(&type->ks_inuse, count, __ATOMIC_RELAXED);
}

void *
bsdkpi_malloc(size_t size, struct malloc_type *type, int flags)
{
	void *p;

	KASSERT((flags & (M_WAITOK | M_NOWAIT)) != (M_WAITOK | M_NOWAIT),
	    ("malloc: both M_WAITOK and M_NOWAIT"));
	if (size == 0)
		size = 1;
	p = kmalloc(size, bsdkpi_malloc_gfp(flags));
	if (p != NULL)
		malloc_type_account(type, (long)lkpi_os_malloc_size(p), 1);
	return (p);
}

void *
bsdkpi_mallocarray(size_t nmemb, size_t size, struct malloc_type *type,
    int flags)
{
	size_t bytes;

	if (nmemb != 0 && size > SIZE_MAX / nmemb) {
		if (flags & M_WAITOK)
			panic("mallocarray: %zu * %zu overflowed", nmemb, size);
		return (NULL);
	}
	bytes = nmemb * size;
	return (bsdkpi_malloc(bytes, type, flags));
}

void
bsdkpi_free(void *addr, struct malloc_type *type)
{
	if (addr == NULL)
		return;
	malloc_type_account(type, -(long)lkpi_os_malloc_size(addr), -1);
	kfree(addr);
}

void
bsdkpi_zfree(void *addr, struct malloc_type *type)
{
	if (addr == NULL)
		return;
	memset(addr, 0, lkpi_os_malloc_size(addr));
	bsdkpi_free(addr, type);
}

void *
bsdkpi_realloc(void *addr, size_t size, struct malloc_type *type, int flags)
{
	void *n;
	size_t old;

	if (addr == NULL)
		return (bsdkpi_malloc(size, type, flags));
	n = bsdkpi_malloc(size, type, flags);
	if (n == NULL)
		return (NULL);
	old = lkpi_os_malloc_size(addr);
	memcpy(n, addr, old < size ? old : size);
	bsdkpi_free(addr, type);
	return (n);
}

void *
bsdkpi_reallocf(void *addr, size_t size, struct malloc_type *type, int flags)
{
	void *n;

	n = bsdkpi_realloc(addr, size, type, flags);
	if (n == NULL)
		bsdkpi_free(addr, type);
	return (n);
}

size_t
bsdkpi_malloc_usable_size(const void *addr)
{
	return (addr != NULL ? lkpi_os_malloc_size(addr) : 0);
}

/* ---- counter(9) ----------------------------------------------------- */

counter_u64_t
counter_u64_alloc(int flags)
{
	return (bsdkpi_malloc(sizeof(uint64_t), M_TEMP, flags | M_ZERO));
}

void
counter_u64_free(counter_u64_t c)
{
	bsdkpi_free(c, M_TEMP);
}

/* ---- epoch(9) ------------------------------------------------------- */

struct epoch bsdkpi_global_epoch;
epoch_t global_epoch = &bsdkpi_global_epoch;
epoch_t global_epoch_preempt = &bsdkpi_global_epoch;
epoch_t net_epoch_preempt = &bsdkpi_global_epoch;

struct epoch {
	int	e_dummy;
};

struct bsdkpi_epoch_call {
	struct rcu_head		 rcu;
	epoch_callback_t	*cb;
	epoch_context_t		 ctx;
};

static void
bsdkpi_epoch_call_rcu(struct rcu_head *head)
{
	struct bsdkpi_epoch_call *c =
	    container_of(head, struct bsdkpi_epoch_call, rcu);

	c->cb(c->ctx);
	kfree(c);
}

void
epoch_call(epoch_t epoch, epoch_callback_t *cb, epoch_context_t ctx)
{
	struct bsdkpi_epoch_call *c;

	(void)epoch;
	c = kmalloc(sizeof(*c), GFP_ATOMIC);
	if (c == NULL) {
		/* no memory: wait for the grace period synchronously */
		synchronize_rcu();
		cb(ctx);
		return;
	}
	c->cb = cb;
	c->ctx = ctx;
	call_rcu(&c->rcu, bsdkpi_epoch_call_rcu);
}

/* ---- time helpers --------------------------------------------------- */

int
ppsratecheck(struct timeval *lasttime, int *curpps, int maxpps)
{
	struct timeval tv, delta;
	int rv;

	getmicrouptime(&tv);
	delta = tv;
	timevalsub(&delta, lasttime);

	/*
	 * Check for 0,0 so that the message will be seen at least once.
	 * If more than one second has passed since the last update of
	 * lasttime, reset the counter.
	 */
	if ((lasttime->tv_sec == 0 && lasttime->tv_usec == 0) ||
	    delta.tv_sec >= 1) {
		*lasttime = tv;
		*curpps = 0;
		rv = 1;
	} else if (maxpps < 0)
		rv = 1;
	else if (*curpps < maxpps)
		rv = 1;
	else
		rv = 0;
	if (*curpps + 1 > 0)
		*curpps = *curpps + 1;
	return (rv);
}

int
ratecheck(struct timeval *lasttime, const struct timeval *mininterval)
{
	struct timeval tv, delta;
	int rv = 0;

	getmicrouptime(&tv);
	delta = tv;
	timevalsub(&delta, lasttime);

	if ((lasttime->tv_sec == 0 && lasttime->tv_usec == 0) ||
	    timevalcmp(&delta, mininterval, >=)) {
		*lasttime = tv;
		rv = 1;
	}
	return (rv);
}

int
tvtohz(struct timeval *tv)
{
	int64_t t;

	t = (int64_t)tv->tv_sec * hz + ((int64_t)tv->tv_usec * hz + 999999) / 1000000;
	if (t <= 0)
		return (1);
	if (t > INT_MAX)
		return (INT_MAX);
	return ((int)t + 1);
}

/* ---- libkern leftovers ---------------------------------------------- */

char *
bsdkpi_strsep(char **stringp, const char *delim)
{
	char *s, *tok;
	const char *spanp;
	int c, sc;

	if ((s = *stringp) == NULL)
		return (NULL);
	for (tok = s;;) {
		c = *s++;
		spanp = delim;
		do {
			if ((sc = *spanp++) == c) {
				if (c == 0)
					s = NULL;
				else
					s[-1] = 0;
				*stringp = s;
				return (tok);
			}
		} while (sc != 0);
	}
}

void *
bsdkpi_memcchr(const void *begin, int c, size_t n)
{
	const unsigned char *p = begin;

	while (n-- > 0) {
		if (*p != (unsigned char)c)
			return (__DECONST(void *, p));
		p++;
	}
	return (NULL);
}

int
nullop(void)
{
	return (0);
}

int
eopnotsupp(void)
{
	return (EOPNOTSUPP);
}

/* ---- bpf(9): no listeners ------------------------------------------- */

struct bpf_if *
bpf_attach(const char *name, u_int dlt, u_int hdrlen,
    const struct bif_methods *methods, void *arg)
{
	(void)name; (void)dlt; (void)hdrlen; (void)methods; (void)arg;
	return (NULL);
}

void
bpf_detach(struct bpf_if *bp)
{
	(void)bp;
}

void
bpfattach(struct ifnet *ifp, u_int dlt, u_int hdrlen)
{
	(void)ifp; (void)dlt; (void)hdrlen;
}

void
bpfattach2(struct ifnet *ifp, u_int dlt, u_int hdrlen, struct bpf_if **bpp)
{
	(void)ifp; (void)dlt; (void)hdrlen;
	*bpp = NULL;
}

void
bpfdetach(struct ifnet *ifp)
{
	(void)ifp;
}

void
bpf_mtap(struct bpf_if *bp, struct mbuf *m)
{
	(void)bp; (void)m;
}

void
bpf_mtap_if(struct ifnet *ifp, struct mbuf *m)
{
	(void)ifp; (void)m;
}

void
bpf_mtap2(struct bpf_if *bp, void *data, u_int dlen, struct mbuf *m)
{
	(void)bp; (void)data; (void)dlen; (void)m;
}

void
bpf_mtap2_if(struct ifnet *ifp, void *data, u_int dlen, struct mbuf *m)
{
	(void)ifp; (void)data; (void)dlen; (void)m;
}

void
bpf_tap(struct bpf_if *bp, u_char *pkt, u_int pktlen)
{
	(void)bp; (void)pkt; (void)pktlen;
}

void
bpf_tap_if(struct ifnet *ifp, u_char *pkt, u_int pktlen)
{
	(void)ifp; (void)pkt; (void)pktlen;
}

bool
bpf_peers_present_if(struct ifnet *ifp)
{
	(void)ifp;
	return (false);
}

int
bpf_validate(const void *f, int len)
{
	(void)f; (void)len;
	return (0);
}

u_int
bpf_filter(const void *pc, u_char *p, u_int wirelen, u_int buflen)
{
	(void)pc; (void)p; (void)buflen;
	return (wirelen);
}

uint32_t
bpf_ifnet_wrsize(void *arg)
{
	struct ifnet *ifp = arg;

	return (ifp->if_mtu + ifp->if_hdrlen);
}

int
bpf_ifnet_promisc(void *arg, bool on)
{
	(void)arg; (void)on;
	return (0);
}

/* ---- start-up ------------------------------------------------------- */

int	bsdkpi_callout_init(void);	/* callout.c */
void	bsdkpi_callout_fini(void);
int	bsdkpi_taskqueue_init(void);	/* taskqueue.c */
void	bsdkpi_taskqueue_fini(void);

static int bsdkpi_initialized;

int
bsdkpi_init(void)
{
	int error;

	if (bsdkpi_initialized)
		return (0);
	getmicrotime(&boottime);
	mp_ncpus = lkpi_os_cpu_count();
	if (mp_ncpus < 1)
		mp_ncpus = 1;
	mp_maxid = mp_ncpus - 1;
	error = bsdkpi_callout_init();
	if (error != 0)
		return (error);
	error = bsdkpi_taskqueue_init();
	if (error != 0) {
		bsdkpi_callout_fini();
		return (error);
	}
	bsdkpi_initialized = 1;
	return (net80211_register_builtin_modules());
}

void
bsdkpi_fini(void)
{
	if (!bsdkpi_initialized)
		return;
	net80211_unregister_builtin_modules();
	bsdkpi_taskqueue_fini();
	bsdkpi_callout_fini();
	bsdkpi_initialized = 0;
}
