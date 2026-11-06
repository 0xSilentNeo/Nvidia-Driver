/* SPDX-License-Identifier: MIT */
/*
 * sys/mutex.h - mutex(9) on LinuxKPI.
 *
 * MTX_DEF is a sleepable struct mutex (an IOLock in the kext) with owner
 * tracking so that MTX_RECURSE and mtx_owned()/mtx_assert() work; MTX_SPIN
 * is a spinlock_t taken with interrupts disabled.  Implementation in
 * src/mutex.c.  net80211's IEEE80211_LOCK family is MTX_DEF | MTX_RECURSE.
 */
#ifndef _BSDKPI_SYS_MUTEX_H_
#define _BSDKPI_SYS_MUTEX_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <lkpi/os.h>

struct mtx {
	struct lock_object	lock_object;
	struct mutex		mtx_mutex;	/* MTX_DEF */
	spinlock_t		mtx_spin;	/* MTX_SPIN */
	lkpi_os_thread_t	mtx_owner;	/* MTX_DEF: holder */
	unsigned long		mtx_irqstate;	/* MTX_SPIN: saved state */
	int			mtx_recurse;
	int			mtx_flags;	/* MTX_* at init time */
};

#define MTX_DEF		0x00000000
#define MTX_SPIN	0x00000001
#define MTX_RECURSE	0x00000004
#define MTX_NOWITNESS	0x00000008
#define MTX_DUPOK	0x00000020
#define MTX_NOPROFILE	0x00000020
#define MTX_NEW		0x00000040

#define MA_OWNED	LA_LOCKED
#define MA_NOTOWNED	LA_UNLOCKED
#define MA_RECURSED	LA_RECURSED
#define MA_NOTRECURSED	LA_NOTRECURSED

void	mtx_init(struct mtx *m, const char *name, const char *type, int opts);
void	mtx_destroy(struct mtx *m);
void	mtx_lock(struct mtx *m);
void	mtx_unlock(struct mtx *m);
int	mtx_trylock(struct mtx *m);
void	mtx_lock_spin(struct mtx *m);
void	mtx_unlock_spin(struct mtx *m);
int	mtx_trylock_spin(struct mtx *m);
int	mtx_owned(const struct mtx *m);
int	mtx_recursed(const struct mtx *m);
int	mtx_initialized(const struct mtx *m);
void	_mtx_assert(const struct mtx *m, int what, const char *file, int line);
void	mtx_sysinit(void *arg);

#define mtx_lock_flags(m, opts)		mtx_lock(m)
#define mtx_unlock_flags(m, opts)	mtx_unlock(m)
#define mtx_trylock_flags(m, opts)	mtx_trylock(m)
#define mtx_lock_spin_flags(m, opts)	mtx_lock_spin(m)
#define mtx_unlock_spin_flags(m, opts)	mtx_unlock_spin(m)
#define mtx_assert(m, what)		_mtx_assert((m), (what), __FILE__, __LINE__)
#define mtx_name(m)			((m)->lock_object.lo_name)
#define mtx_sleep(chan, mtx, pri, wmesg, timo) \
	bsdkpi_msleep((chan), (mtx), (pri), (wmesg), (timo))
int	bsdkpi_msleep(const void *chan, struct mtx *mtx, int pri,
	    const char *wmesg, int timo);

struct mtx_args {
	void		*ma_mtx;
	const char	*ma_desc;
	int		 ma_opts;
};
#define MTX_SYSINIT(name, mtx, desc, opts)				\
	static struct mtx_args name##_args = {				\
		(mtx),							\
		(desc),							\
		(opts)							\
	};								\
	SYSINIT(name##_mtx_sysinit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    mtx_sysinit, &name##_args);					\
	SYSUNINIT(name##_mtx_sysuninit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    _mtx_destroy, (void *)(mtx))
void	_mtx_destroy(void *arg);

#define MTX_SYSINIT_FLAGS(name, mtx, desc, opts, flags) MTX_SYSINIT(name, mtx, desc, opts)

#define mtx_lock_padalign	mtx_lock
#define mtx_unlock_padalign	mtx_unlock

/* The Giant lock does not exist; keep the spellings compiling. */
extern struct mtx Giant;
#define mtx_assert_(m, what, file, line) _mtx_assert((m), (what), (file), (line))

struct mtx_padalign {
	struct mtx	mtx;
};

#endif /* _BSDKPI_SYS_MUTEX_H_ */
