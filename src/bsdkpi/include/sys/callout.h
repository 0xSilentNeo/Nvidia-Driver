/* SPDX-License-Identifier: MIT */
/*
 * sys/callout.h - callout(9) on LinuxKPI timers.
 *
 * The timer fires on the backend's timer thread, which may not sleep; the
 * callout handler then runs on a work queue so that MTX_DEF callouts
 * (callout_init_mtx) can take their mutex, exactly as FreeBSD's softclock
 * thread does.  State is protected by one global spinlock; see
 * src/callout.c for the stop/reset races.
 */
#ifndef _BSDKPI_SYS_CALLOUT_H_
#define _BSDKPI_SYS_CALLOUT_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

struct mtx;
struct rwlock;
struct rmlock;
struct lock_object;

typedef void callout_func_t(void *);

struct callout {
	struct timer_list	c_timer;
	struct work_struct	c_work;
	struct mtx		*c_mtx;
	callout_func_t		*c_func;
	void			*c_arg;
	unsigned int		c_gen;		/* bumped by every reset/stop */
	int			c_flags;	/* CALLOUT_* (user visible) */
	int			c_iflags;	/* CALLOUT_PENDING | _QUEUED */
	int			c_time;		/* ticks at arming time */
};

#define CALLOUT_LOCAL_ALLOC	0x0001
#define CALLOUT_ACTIVE		0x0002
#define CALLOUT_PENDING		0x0004
#define CALLOUT_MPSAFE		0x0008
#define CALLOUT_RETURNUNLOCKED	0x0010
#define CALLOUT_SHAREDLOCK	0x0020
#define CALLOUT_DFRMIGRATION	0x0040
#define CALLOUT_PROCESSED	0x0080
#define CALLOUT_DIRECT		0x0100
#define CALLOUT_QUEUED		0x1000	/* bsdkpi: handler work queued */
#define CALLOUT_INITIALIZED	0x2000	/* bsdkpi: callout_init done */

#define C_DIRECT_EXEC		0x0001
#define C_PRELBITS		7
#define C_PRELRANGE		((1 << C_PRELBITS) - 1)
#define C_PREL(x)		(((x) + 1) << 1)
#define C_PRELGET(x)		(int)((((x) >> 1) & C_PRELRANGE) - 1)
#define C_HARDCLOCK		0x0100
#define C_ABSOLUTE		0x0200
#define C_PRECALC		0x0400
#define C_CATCH			0x0800

#define CS_DRAIN		0x0001
#define CS_EXECUTING		0x0002
#define CS_MIGRBLOCK		0x0004

void	callout_init(struct callout *c, int mpsafe);
void	_callout_init_lock(struct callout *c, struct lock_object *lock,
	    int flags);
void	callout_init_mtx(struct callout *c, struct mtx *mtx, int flags);
#define callout_init_rw(c, rw, flags)	callout_init_mtx((c), &(rw)->rw_mtx, (flags))
#define callout_init_rm(c, rm, flags)	callout_init_mtx((c), &(rm)->rm_mtx, (flags))
int	callout_reset(struct callout *c, int to_ticks, callout_func_t *fn,
	    void *arg);
int	callout_reset_sbt(struct callout *c, sbintime_t sbt, sbintime_t pr,
	    callout_func_t *fn, void *arg, int flags);
#define callout_reset_on(c, t, fn, arg, cpu)	callout_reset((c), (t), (fn), (arg))
#define callout_reset_curcpu(c, t, fn, arg)	callout_reset((c), (t), (fn), (arg))
#define callout_reset_sbt_on(c, sbt, pr, fn, arg, cpu, flags) \
	callout_reset_sbt((c), (sbt), (pr), (fn), (arg), (flags))
#define callout_reset_sbt_curcpu(c, sbt, pr, fn, arg, flags) \
	callout_reset_sbt((c), (sbt), (pr), (fn), (arg), (flags))
int	callout_schedule(struct callout *c, int to_ticks);
#define callout_schedule_on(c, t, cpu)		callout_schedule((c), (t))
#define callout_schedule_curcpu(c, t)		callout_schedule((c), (t))
int	callout_schedule_sbt(struct callout *c, sbintime_t sbt, sbintime_t pr,
	    int flags);
#define callout_schedule_sbt_on(c, sbt, pr, cpu, flags) callout_schedule_sbt((c), (sbt), (pr), (flags))
#define callout_schedule_sbt_curcpu(c, sbt, pr, flags) callout_schedule_sbt((c), (sbt), (pr), (flags))
int	_callout_stop_safe(struct callout *c, int flags);
#define callout_stop(c)		_callout_stop_safe((c), 0)
#define callout_drain(c)	_callout_stop_safe((c), CS_DRAIN)
#define callout_async_drain(c, d) _callout_stop_safe((c), CS_DRAIN)
#define callout_pending(c)	(((c)->c_iflags & CALLOUT_PENDING) != 0)
#define callout_active(c)	(((c)->c_flags & CALLOUT_ACTIVE) != 0)
#define callout_deactivate(c)	((c)->c_flags &= ~CALLOUT_ACTIVE)
void	callout_when(sbintime_t sbt, sbintime_t precision, int flags,
	    sbintime_t *sbt_res, sbintime_t *pr_res);
/* bsdkpi: release the backend timer of a callout whose memory is about to
 * be freed (FreeBSD has no destructor; this is optional but avoids leaks). */
void	bsdkpi_callout_destroy(struct callout *c);
#endif /* _BSDKPI_SYS_CALLOUT_H_ */
