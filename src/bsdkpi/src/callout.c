// SPDX-License-Identifier: MIT
/*
 * callout(9) on LinuxKPI timers and a work queue.
 *
 * Timeline of a callout_reset():
 *   reset  -> c_iflags = PENDING, c_gen++, mod_timer()
 *   timer  -> (timer thread, may not sleep) marks QUEUED, queue_work()
 *   work   -> takes c_mtx if any, re-checks PENDING|QUEUED under the global
 *             spinlock, clears PENDING, runs c_func(c_arg), drops c_mtx
 *             unless CALLOUT_RETURNUNLOCKED.
 * callout_stop() clears PENDING|QUEUED and deletes the timer; a work item
 * already queued finds the flags cleared and returns.  For MTX callouts the
 * caller holds c_mtx while stopping, so a handler that is about to run
 * blocks on the mutex and then sees the cancellation, which is exactly the
 * guarantee FreeBSD gives.  callout_drain() additionally waits for the
 * timer and the work item to finish; it must not be called with c_mtx held.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>

static DEFINE_SPINLOCK(bsdkpi_callout_lock);
static struct workqueue_struct *bsdkpi_callout_wq;

static struct workqueue_struct *
callout_wq(void)
{
	struct workqueue_struct *wq = READ_ONCE(bsdkpi_callout_wq);

	return (wq != NULL ? wq : system_wq);
}

int
bsdkpi_callout_init(void)
{
	if (bsdkpi_callout_wq == NULL)
		bsdkpi_callout_wq = alloc_ordered_workqueue("bsdkpi_callout", 0);
	return (bsdkpi_callout_wq != NULL ? 0 : -ENOMEM);
}

void
bsdkpi_callout_fini(void)
{
	struct workqueue_struct *wq = bsdkpi_callout_wq;

	bsdkpi_callout_wq = NULL;
	if (wq != NULL)
		destroy_workqueue(wq);
}

static void
callout_work_fn(struct work_struct *work)
{
	struct callout *c = container_of(work, struct callout, c_work);
	callout_func_t *fn;
	void *arg;
	struct mtx *mtx;
	unsigned long flags;

	mtx = c->c_mtx;
	if (mtx != NULL)
		mtx_lock(mtx);
	spin_lock_irqsave(&bsdkpi_callout_lock, flags);
	if ((c->c_iflags & (CALLOUT_PENDING | CALLOUT_QUEUED)) !=
	    (CALLOUT_PENDING | CALLOUT_QUEUED)) {
		/* stopped or re-armed between the timer and now */
		spin_unlock_irqrestore(&bsdkpi_callout_lock, flags);
		if (mtx != NULL)
			mtx_unlock(mtx);
		return;
	}
	c->c_iflags &= ~(CALLOUT_PENDING | CALLOUT_QUEUED);
	fn = c->c_func;
	arg = c->c_arg;
	spin_unlock_irqrestore(&bsdkpi_callout_lock, flags);

	fn(arg);

	if (mtx != NULL && (c->c_flags & CALLOUT_RETURNUNLOCKED) == 0)
		mtx_unlock(mtx);
}

static void
callout_timer_fn(struct timer_list *t)
{
	struct callout *c = container_of(t, struct callout, c_timer);
	unsigned long flags;

	spin_lock_irqsave(&bsdkpi_callout_lock, flags);
	if ((c->c_iflags & CALLOUT_PENDING) == 0) {
		spin_unlock_irqrestore(&bsdkpi_callout_lock, flags);
		return;
	}
	c->c_iflags |= CALLOUT_QUEUED;
	spin_unlock_irqrestore(&bsdkpi_callout_lock, flags);
	queue_work(callout_wq(), &c->c_work);
}

static void
callout_setup(struct callout *c, struct mtx *mtx, int flags)
{
	memset(c, 0, sizeof(*c));
	timer_setup(&c->c_timer, callout_timer_fn, 0);
	INIT_WORK(&c->c_work, callout_work_fn);
	c->c_mtx = mtx;
	c->c_flags = flags & (CALLOUT_RETURNUNLOCKED | CALLOUT_SHAREDLOCK |
	    CALLOUT_MPSAFE);
	c->c_iflags = CALLOUT_INITIALIZED;
}

void
callout_init(struct callout *c, int mpsafe)
{
	callout_setup(c, NULL, mpsafe ? CALLOUT_MPSAFE : 0);
}

void
callout_init_mtx(struct callout *c, struct mtx *mtx, int flags)
{
	KASSERT((flags & ~(CALLOUT_RETURNUNLOCKED | CALLOUT_SHAREDLOCK |
	    CALLOUT_TRYLOCK)) == 0, ("callout_init_mtx: bad flags %#x", flags));
	callout_setup(c, mtx, flags);
}

void
_callout_init_lock(struct callout *c, struct lock_object *lock, int flags)
{
	/* every lock type in this shim embeds a struct mtx first */
	callout_setup(c, (struct mtx *)lock, flags);
}

static int
callout_reset_locked(struct callout *c, int to_ticks, callout_func_t *fn,
    void *arg)
{
	int cancelled;

	cancelled = (c->c_iflags & CALLOUT_PENDING) != 0;
	c->c_func = fn;
	c->c_arg = arg;
	c->c_gen++;
	c->c_iflags = (c->c_iflags & ~CALLOUT_QUEUED) | CALLOUT_PENDING |
	    CALLOUT_INITIALIZED;
	c->c_flags |= CALLOUT_ACTIVE;
	c->c_time = ticks + to_ticks;
	return (cancelled);
}

int
callout_reset(struct callout *c, int to_ticks, callout_func_t *fn, void *arg)
{
	unsigned long flags;
	int cancelled;

	if ((c->c_iflags & CALLOUT_INITIALIZED) == 0)
		callout_setup(c, NULL, 0);
	if (c->c_mtx != NULL)
		KASSERT(mtx_owned(c->c_mtx), ("callout_reset: %s not owned",
		    c->c_mtx->lock_object.lo_name));
	if (to_ticks <= 0)
		to_ticks = 1;
	spin_lock_irqsave(&bsdkpi_callout_lock, flags);
	cancelled = callout_reset_locked(c, to_ticks, fn, arg);
	spin_unlock_irqrestore(&bsdkpi_callout_lock, flags);
	mod_timer(&c->c_timer, jiffies + (unsigned long)to_ticks);
	return (cancelled);
}

int
callout_reset_sbt(struct callout *c, sbintime_t sbt, sbintime_t pr,
    callout_func_t *fn, void *arg, int flags)
{
	int to_ticks;

	(void)pr;
	if (flags & C_ABSOLUTE)
		sbt -= sbinuptime();
	to_ticks = (int)(sbt / (SBT_1S / HZ));
	return (callout_reset(c, to_ticks, fn, arg));
}

int
callout_schedule(struct callout *c, int to_ticks)
{
	return (callout_reset(c, to_ticks, c->c_func, c->c_arg));
}

int
callout_schedule_sbt(struct callout *c, sbintime_t sbt, sbintime_t pr,
    int flags)
{
	return (callout_reset_sbt(c, sbt, pr, c->c_func, c->c_arg, flags));
}

/*
 * Returns 1 if the callout was pending and has been stopped, 0 if it was
 * not pending (FreeBSD also returns -1 when the handler is running and
 * cannot be stopped; a running handler here has already cleared PENDING).
 */
int
_callout_stop_safe(struct callout *c, int flags)
{
	unsigned long irq;
	int was_pending;

	if ((c->c_iflags & CALLOUT_INITIALIZED) == 0)
		return (0);
	spin_lock_irqsave(&bsdkpi_callout_lock, irq);
	was_pending = (c->c_iflags & CALLOUT_PENDING) != 0;
	c->c_iflags &= ~(CALLOUT_PENDING | CALLOUT_QUEUED);
	c->c_flags &= ~CALLOUT_ACTIVE;
	c->c_gen++;
	spin_unlock_irqrestore(&bsdkpi_callout_lock, irq);

	if (flags & CS_DRAIN) {
		KASSERT(c->c_mtx == NULL || !mtx_owned(c->c_mtx),
		    ("callout_drain: %s held", c->c_mtx->lock_object.lo_name));
		timer_delete_sync(&c->c_timer);
		cancel_work_sync(&c->c_work);
	} else {
		timer_delete(&c->c_timer);
	}
	return (was_pending);
}

void
callout_when(sbintime_t sbt, sbintime_t precision, int flags,
    sbintime_t *sbt_res, sbintime_t *pr_res)
{
	if ((flags & C_ABSOLUTE) == 0)
		sbt += sbinuptime();
	*sbt_res = sbt;
	*pr_res = precision;
}

void
bsdkpi_callout_destroy(struct callout *c)
{
	if ((c->c_iflags & CALLOUT_INITIALIZED) == 0)
		return;
	(void)_callout_stop_safe(c, CS_DRAIN);
	timer_destroy(&c->c_timer);
	c->c_iflags = 0;
}
