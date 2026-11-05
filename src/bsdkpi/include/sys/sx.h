/* SPDX-License-Identifier: MIT */
/*
 * sys/sx.h - shared/exclusive locks as a plain mutex: readers serialise.
 * Correct, not concurrent (the same approximation LinuxKPI uses for rwsem).
 */
#ifndef _BSDKPI_SYS_SX_H_
#define _BSDKPI_SYS_SX_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>

struct sx {
	struct lock_object	lock_object;
	struct mtx		sx_mtx;
	int			sx_shared;	/* readers inside (0 or 1) */
};

#define SX_NOWITNESS	0x01
#define SX_DUPOK	0x02
#define SX_NOPROFILE	0x04
#define SX_RECURSE	0x08
#define SX_QUIET	0x10
#define SX_NEW		0x20

#define SA_LOCKED	LA_LOCKED
#define SA_SLOCKED	LA_SLOCKED
#define SA_XLOCKED	LA_XLOCKED
#define SA_UNLOCKED	LA_UNLOCKED
#define SA_RECURSED	LA_RECURSED
#define SA_NOTRECURSED	LA_NOTRECURSED
#define SX_LOCKED	SA_LOCKED
#define SX_SLOCKED	SA_SLOCKED
#define SX_XLOCKED	SA_XLOCKED
#define SX_UNLOCKED	SA_UNLOCKED

static __inline void
sx_init_flags(struct sx *sx, const char *description, int opts)
{
	sx->lock_object.lo_name = description;
	sx->lock_object.lo_flags = LO_INITIALIZED;
	sx->sx_shared = 0;
	mtx_init(&sx->sx_mtx, description, NULL,
	    MTX_DEF | ((opts & SX_RECURSE) ? MTX_RECURSE : 0));
}

static __inline void sx_init(struct sx *sx, const char *description) { sx_init_flags(sx, description, 0); }
static __inline void sx_destroy(struct sx *sx) { mtx_destroy(&sx->sx_mtx); sx->lock_object.lo_flags = 0; }
static __inline void sx_xlock(struct sx *sx) { mtx_lock(&sx->sx_mtx); }
static __inline void sx_xunlock(struct sx *sx) { mtx_unlock(&sx->sx_mtx); }
static __inline void sx_slock(struct sx *sx) { mtx_lock(&sx->sx_mtx); sx->sx_shared = 1; }
static __inline void sx_sunlock(struct sx *sx) { sx->sx_shared = 0; mtx_unlock(&sx->sx_mtx); }
static __inline int sx_try_xlock(struct sx *sx) { return (mtx_trylock(&sx->sx_mtx)); }
static __inline int sx_try_slock(struct sx *sx) { int r = mtx_trylock(&sx->sx_mtx); if (r) sx->sx_shared = 1; return (r); }
static __inline int sx_try_upgrade(struct sx *sx) { sx->sx_shared = 0; return (1); }
static __inline void sx_downgrade(struct sx *sx) { sx->sx_shared = 1; }
static __inline void sx_unlock(struct sx *sx) { sx->sx_shared = 0; mtx_unlock(&sx->sx_mtx); }
static __inline int sx_xlocked(const struct sx *sx) { return (mtx_owned(&sx->sx_mtx) && !sx->sx_shared); }
static __inline int sx_xholder_is_me(const struct sx *sx) { return (sx_xlocked(sx)); }
#define sx_xlock_sig(sx)	(sx_xlock(sx), 0)
#define sx_slock_sig(sx)	(sx_slock(sx), 0)
#define sx_sleep(chan, sx, pri, wmesg, timo) \
	bsdkpi_msleep((chan), &(sx)->sx_mtx, (pri), (wmesg), (timo))
#define sx_assert(sx, what)	do { } while (0)
#define sx_name(sx)		((sx)->lock_object.lo_name)
#define SX_SYSINIT(name, sxa, desc)					\
	static struct sx_args name##_args = { (sxa), (desc), 0 };	\
	SYSINIT(name##_sx_sysinit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    sx_sysinit, &name##_args)
#define SX_SYSINIT_FLAGS(name, sxa, desc, flags) SX_SYSINIT(name, sxa, desc)
struct sx_args {
	struct sx	*sa_sx;
	const char	*sa_desc;
	int		 sa_flags;
};
static __inline void
sx_sysinit(void *arg)
{
	struct sx_args *sargs = (struct sx_args *)arg;

	sx_init_flags(sargs->sa_sx, sargs->sa_desc, sargs->sa_flags);
}
#endif /* _BSDKPI_SYS_SX_H_ */
