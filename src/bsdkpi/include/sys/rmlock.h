/* SPDX-License-Identifier: MIT */
/* sys/rmlock.h - read-mostly locks as a plain mutex; readers serialise. */
#ifndef _BSDKPI_SYS_RMLOCK_H_
#define _BSDKPI_SYS_RMLOCK_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>

struct rmlock {
	struct lock_object	lock_object;
	struct mtx		rm_mtx;
};

struct rm_priotracker {
	struct rmlock	*rmp_rmlock;
};

#define RM_NOWITNESS	0x00000001
#define RM_RECURSE	0x00000002
#define RM_SLEEPABLE	0x00000004
#define RM_NEW		0x00000008
#define RM_DUPOK	0x00000010

#define RA_LOCKED	LA_LOCKED
#define RA_RLOCKED	LA_SLOCKED
#define RA_WLOCKED	LA_XLOCKED
#define RA_UNLOCKED	LA_UNLOCKED
#define RA_RECURSED	LA_RECURSED
#define RA_NOTRECURSED	LA_NOTRECURSED

static __inline void
rm_init_flags(struct rmlock *rm, const char *name, int opts)
{
	rm->lock_object.lo_name = name;
	rm->lock_object.lo_flags = LO_INITIALIZED;
	mtx_init(&rm->rm_mtx, name, NULL,
	    MTX_DEF | ((opts & RM_RECURSE) ? MTX_RECURSE : 0));
}

static __inline void rm_init(struct rmlock *rm, const char *name) { rm_init_flags(rm, name, 0); }
static __inline void rm_destroy(struct rmlock *rm) { mtx_destroy(&rm->rm_mtx); }
static __inline void rm_wlock(struct rmlock *rm) { mtx_lock(&rm->rm_mtx); }
static __inline void rm_wunlock(struct rmlock *rm) { mtx_unlock(&rm->rm_mtx); }
static __inline void rm_rlock(struct rmlock *rm, struct rm_priotracker *t) { t->rmp_rmlock = rm; mtx_lock(&rm->rm_mtx); }
static __inline void rm_runlock(struct rmlock *rm, struct rm_priotracker *t) { (void)t; mtx_unlock(&rm->rm_mtx); }
static __inline int rm_try_rlock(struct rmlock *rm, struct rm_priotracker *t) { t->rmp_rmlock = rm; return (mtx_trylock(&rm->rm_mtx)); }
static __inline int rm_wowned(const struct rmlock *rm) { return (mtx_owned(&rm->rm_mtx)); }
#define rm_assert(rm, what)	do { } while (0)
#define rm_sleep(chan, rm, pri, wmesg, timo) \
	bsdkpi_msleep((chan), &(rm)->rm_mtx, (pri), (wmesg), (timo))
#define rm_name(rm)		((rm)->lock_object.lo_name)
struct rm_args {
	struct rmlock	*ra_rm;
	const char	*ra_desc;
	int		 ra_flags;
};
static __inline void
rm_sysinit(void *arg)
{
	struct rm_args *args = (struct rm_args *)arg;

	rm_init_flags(args->ra_rm, args->ra_desc, args->ra_flags);
}
#define RM_SYSINIT_FLAGS(name, rm, desc, flags)				\
	static struct rm_args name##_args = { (rm), (desc), (flags) };	\
	SYSINIT(name##_rm_sysinit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    rm_sysinit, &name##_args)
#define RM_SYSINIT(name, rm, desc)	RM_SYSINIT_FLAGS(name, rm, desc, 0)
#endif /* _BSDKPI_SYS_RMLOCK_H_ */
