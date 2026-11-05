/* SPDX-License-Identifier: MIT */
/* sys/rwlock.h - rwlock(9) as a plain mutex; readers serialise. */
#ifndef _BSDKPI_SYS_RWLOCK_H_
#define _BSDKPI_SYS_RWLOCK_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>

struct rwlock {
	struct lock_object	lock_object;
	struct mtx		rw_mtx;
	int			rw_shared;
};

#define RW_DUPOK	0x01
#define RW_NOPROFILE	0x02
#define RW_NOWITNESS	0x04
#define RW_QUIET	0x08
#define RW_RECURSE	0x10
#define RW_NEW		0x20

#define RA_LOCKED	LA_LOCKED
#define RA_RLOCKED	LA_SLOCKED
#define RA_WLOCKED	LA_XLOCKED
#define RA_UNLOCKED	LA_UNLOCKED
#define RA_RECURSED	LA_RECURSED
#define RA_NOTRECURSED	LA_NOTRECURSED

static __inline void
rw_init_flags(struct rwlock *rw, const char *name, int opts)
{
	rw->lock_object.lo_name = name;
	rw->lock_object.lo_flags = LO_INITIALIZED;
	rw->rw_shared = 0;
	mtx_init(&rw->rw_mtx, name, NULL,
	    MTX_DEF | ((opts & RW_RECURSE) ? MTX_RECURSE : 0));
}

static __inline void rw_init(struct rwlock *rw, const char *name) { rw_init_flags(rw, name, 0); }
static __inline void rw_destroy(struct rwlock *rw) { mtx_destroy(&rw->rw_mtx); rw->lock_object.lo_flags = 0; }
static __inline void rw_wlock(struct rwlock *rw) { mtx_lock(&rw->rw_mtx); }
static __inline void rw_wunlock(struct rwlock *rw) { mtx_unlock(&rw->rw_mtx); }
static __inline void rw_rlock(struct rwlock *rw) { mtx_lock(&rw->rw_mtx); rw->rw_shared = 1; }
static __inline void rw_runlock(struct rwlock *rw) { rw->rw_shared = 0; mtx_unlock(&rw->rw_mtx); }
static __inline int rw_try_wlock(struct rwlock *rw) { return (mtx_trylock(&rw->rw_mtx)); }
static __inline int rw_try_rlock(struct rwlock *rw) { int r = mtx_trylock(&rw->rw_mtx); if (r) rw->rw_shared = 1; return (r); }
static __inline int rw_try_upgrade(struct rwlock *rw) { rw->rw_shared = 0; return (1); }
static __inline void rw_downgrade(struct rwlock *rw) { rw->rw_shared = 1; }
static __inline void rw_unlock(struct rwlock *rw) { rw->rw_shared = 0; mtx_unlock(&rw->rw_mtx); }
static __inline int rw_wowned(const struct rwlock *rw) { return (mtx_owned(&rw->rw_mtx) && !rw->rw_shared); }
static __inline int rw_initialized(const struct rwlock *rw) { return (lock_initialized(&rw->lock_object)); }
#define rw_assert(rw, what)	do { } while (0)
#define rw_name(rw)		((rw)->lock_object.lo_name)
#define rw_sleep(chan, rw, pri, wmesg, timo) \
	bsdkpi_msleep((chan), &(rw)->rw_mtx, (pri), (wmesg), (timo))
struct rw_args {
	void		*ra_rw;
	const char	*ra_desc;
	int		 ra_flags;
};
static __inline void
rw_sysinit(void *arg)
{
	struct rw_args *args = (struct rw_args *)arg;

	rw_init_flags((struct rwlock *)args->ra_rw, args->ra_desc, args->ra_flags);
}
#define RW_SYSINIT(name, rw, desc)					\
	static struct rw_args name##_args = { (rw), (desc), 0 };	\
	SYSINIT(name##_rw_sysinit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    rw_sysinit, &name##_args)
#define RW_SYSINIT_FLAGS(name, rw, desc, flags) RW_SYSINIT(name, rw, desc)
#endif /* _BSDKPI_SYS_RWLOCK_H_ */
