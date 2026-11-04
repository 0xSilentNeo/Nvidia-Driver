// SPDX-License-Identifier: MIT
/*
 * Workqueues.
 *
 * Every workqueue_struct owns exactly one backend thread that executes the
 * queued work items in FIFO order, i.e. every queue behaves like a Linux
 * ordered workqueue.  The system queues are shared instances created in
 * lkpi_work_init(): "lkpi-wq" backs system_wq and its aliases, "lkpi-wq-hi"
 * (realtime priority) backs system_highpri_wq and system_bh_wq, and
 * "lkpi-wq-long" backs system_long_wq so that slow items do not starve the
 * short ones.
 *
 * State lives in work->state:
 *   PENDING   set atomically by the queueing side (test_and_set_bit), owned
 *             by whoever grabbed it, cleared by the worker just before the
 *             function runs so the item may re-queue itself.  Cancellers hold
 *             it while they wait, which makes queue_work() fail meanwhile,
 *             exactly like upstream.
 *   DISABLED  Linux 6.10 disable_work(): queueing refuses while set.
 *   CANCELING informational only.
 *   RUNNING   is deliberately never used on the work item: a work function
 *             may free its own work_struct (upstream guarantees the core does
 *             not touch it after the call), so "running" is tracked by the
 *             queue's ->running pointer instead.
 *
 * Waiting (flush_work, cancel_work_sync, flush_workqueue) uses a per-queue
 * IOLock plus lkpi_os_mutex_sleep(); the worker takes that lock around its
 * wakeup only when ->waiters is non-zero, so the common path pays one
 * atomic read per item.  A flush issued from the queue's own thread cannot
 * block without deadlocking; a pending instance is run inline instead, which
 * is what a multi-worker pool would have done concurrently.
 *
 * delayed_work arms a struct timer_list whose expiry inserts the work; the
 * PENDING bit is held from queue_delayed_work() until the function runs.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/rcupdate.h>
#include <linux/sprintf.h>
#include <lkpi/os.h>
#include <lkpi/runtime.h>

struct workqueue_struct {
	char name[32];
	unsigned int flags;
	lkpi_os_thread_t thread;	/* creation handle, used for wakeups */
	lkpi_os_thread_t self;		/* lkpi_os_thread_self() of the worker */
	spinlock_t lock;		/* pending, running, stop */
	struct list_head pending;
	struct work_struct *running;	/* item being executed, or NULL */
	bool stop;
	bool draining;
	struct mutex wait_lock;		/* sleepers in flush/cancel/destroy */
	int waiters;
	bool exited;			/* written under wait_lock by the worker */
	struct list_head node;		/* lkpi_workqueues */
};

struct workqueue_struct *system_wq;
struct workqueue_struct *system_highpri_wq;
struct workqueue_struct *system_long_wq;
struct workqueue_struct *system_unbound_wq;
struct workqueue_struct *system_freezable_wq;
struct workqueue_struct *system_power_efficient_wq;
struct workqueue_struct *system_percpu_wq;
struct workqueue_struct *system_dfl_wq;
struct workqueue_struct *system_bh_wq;

static LIST_HEAD(lkpi_workqueues);
static DEFINE_SPINLOCK(lkpi_workqueues_lock);

#define LKPI_WQ_WAIT_SLICE_NS	(100ULL * 1000 * 1000)	/* re-check period, belt and braces */

static bool lkpi_wq_on_thread(struct workqueue_struct *wq)
{
	return READ_ONCE(wq->self) == lkpi_os_thread_self();
}

/* Worker side: an item finished (or the queue changed state); wake sleepers. */
static void lkpi_wq_notify(struct workqueue_struct *wq, const void *event)
{
	lkpi_os_mutex_t m;

	smp_mb();
	if (!READ_ONCE(wq->waiters))
		return;
	m = __lkpi_mutex_get(&wq->wait_lock);
	lkpi_os_mutex_lock(m);
	lkpi_os_wakeup(m, event, false);
	lkpi_os_mutex_unlock(m);
}

/*
 * Sleep until @cond (evaluated under wq->lock) holds.  The waiter registers
 * itself before testing the condition and the worker checks ->waiters after
 * changing state, so a wakeup can not be lost; the bounded sleep is only a
 * safety net.
 */
static void lkpi_wq_wait(struct workqueue_struct *wq,
			 bool (*cond)(struct workqueue_struct *, void *), void *arg)
{
	lkpi_os_mutex_t m = __lkpi_mutex_get(&wq->wait_lock);

	lkpi_os_mutex_lock(m);
	__atomic_fetch_add(&wq->waiters, 1, __ATOMIC_SEQ_CST);
	for (;;) {
		bool done;

		spin_lock(&wq->lock);
		done = cond(wq, arg);
		spin_unlock(&wq->lock);
		if (done)
			break;
		(void)lkpi_os_mutex_sleep(m, &wq->running, LKPI_WQ_WAIT_SLICE_NS);
	}
	__atomic_fetch_sub(&wq->waiters, 1, __ATOMIC_SEQ_CST);
	lkpi_os_mutex_unlock(m);
}

static bool lkpi_wq_cond_not_running(struct workqueue_struct *wq, void *arg)
{
	return wq->running != (struct work_struct *)arg;
}

static bool lkpi_wq_cond_work_idle(struct workqueue_struct *wq, void *arg)
{
	struct work_struct *work = arg;

	return list_empty(&work->entry) && wq->running != work;
}

static bool lkpi_wq_cond_queue_idle(struct workqueue_struct *wq, void *arg)
{
	return list_empty(&wq->pending) && !wq->running;
}

static void lkpi_wq_thread(void *arg)
{
	struct workqueue_struct *wq = arg;
	lkpi_os_thread_t self;
	lkpi_os_mutex_t m;

	/* alloc_workqueue() publishes the handle right after creating us. */
	while (!(self = __atomic_load_n(&wq->thread, __ATOMIC_ACQUIRE)))
		lkpi_os_thread_yield();
	WRITE_ONCE(wq->self, lkpi_os_thread_self());

	for (;;) {
		struct work_struct *w;

		spin_lock(&wq->lock);
		while (!list_empty(&wq->pending)) {
			w = list_first_entry(&wq->pending, struct work_struct, entry);
			list_del_init(&w->entry);
			wq->running = w;
			/* Not pending while it runs; the function may re-queue it. */
			clear_bit(WORK_STRUCT_PENDING_BIT, &w->state);
			spin_unlock(&wq->lock);

			w->func(w);
			/* @w may have been freed by its function: do not touch it. */

			spin_lock(&wq->lock);
			wq->running = NULL;
			spin_unlock(&wq->lock);
			lkpi_wq_notify(wq, &wq->running);
			spin_lock(&wq->lock);
		}
		if (wq->stop) {
			spin_unlock(&wq->lock);
			break;
		}
		spin_unlock(&wq->lock);
		(void)lkpi_os_thread_wait(self, 0);	/* 0: until woken */
	}

	/* Hand-off to destroy_workqueue(): nothing below touches @wq after
	 * the unlock, so the destroyer may free it once it sees ->exited. */
	m = __lkpi_mutex_get(&wq->wait_lock);
	lkpi_os_mutex_lock(m);
	wq->exited = true;
	lkpi_os_wakeup(m, &wq->exited, false);
	lkpi_os_mutex_unlock(m);
	lkpi_os_thread_exit();
}

/* Append @work, whose PENDING bit the caller already owns, and kick the worker. */
static void lkpi_wq_insert(struct workqueue_struct *wq, struct work_struct *work)
{
	spin_lock(&wq->lock);
	work->wq = wq;
	if (!work->entry.next || list_empty(&work->entry))
		list_add_tail(&work->entry, &wq->pending);
	else
		WARN_ON_ONCE(1);	/* already on a list with PENDING owned?! */
	spin_unlock(&wq->lock);
	lkpi_os_thread_wake(wq->thread);
}

/*
 * Take ownership of PENDING.  Returns true if the work was pending (on a
 * timer or on a queue) and has been removed from it, false if it was idle.
 * Either way the caller owns PENDING afterwards and must clear it (or hand
 * it to lkpi_wq_insert()).
 */
static bool lkpi_work_grab(struct work_struct *work, struct timer_list *timer)
{
	struct workqueue_struct *wq;

	for (;;) {
		if (timer && timer_delete(timer))
			return true;
		if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, &work->state))
			return false;

		wq = READ_ONCE(work->wq);
		if (wq) {
			spin_lock(&wq->lock);
			if (work->entry.next && !list_empty(&work->entry)) {
				list_del_init(&work->entry);
				spin_unlock(&wq->lock);
				return true;
			}
			spin_unlock(&wq->lock);
		}
		/*
		 * PENDING is set but the item is in flight: being inserted by
		 * a timer callback, being dequeued by the worker, or owned by
		 * another canceller.  Back off and retry.
		 */
		if (lkpi_os_may_sleep())
			lkpi_os_sleep_ms(1);
		else
			lkpi_os_thread_yield();
	}
}

/* Wait until @work is no longer being executed (not from its own thread). */
static void lkpi_work_wait_running(struct work_struct *work)
{
	struct workqueue_struct *wq = READ_ONCE(work->wq);

	if (!wq || lkpi_wq_on_thread(wq))
		return;
	lkpi_wq_wait(wq, lkpi_wq_cond_not_running, work);
}

static bool lkpi_work_cancel_sync(struct work_struct *work, struct timer_list *timer)
{
	bool was_pending;

	was_pending = lkpi_work_grab(work, timer);
	set_bit(WORK_STRUCT_CANCELING_BIT, &work->state);
	/* PENDING stays ours while we wait: re-queues from the running
	 * function fail, as upstream guarantees. */
	lkpi_work_wait_running(work);
	clear_bit(WORK_STRUCT_CANCELING_BIT, &work->state);
	clear_bit(WORK_STRUCT_PENDING_BIT, &work->state);
	return was_pending;
}

/* ---- work_struct ---------------------------------------------------- */

void __lkpi_init_work(struct work_struct *w, work_func_t fn)
{
	INIT_LIST_HEAD(&w->entry);
	w->func = fn;
	w->wq = NULL;
	w->state = 0;
}

bool queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	if (test_bit(WORK_STRUCT_DISABLED_BIT, &work->state))
		return false;
	if (test_and_set_bit(WORK_STRUCT_PENDING_BIT, &work->state))
		return false;
	lkpi_wq_insert(wq, work);
	return true;
}

bool flush_work(struct work_struct *work)
{
	struct workqueue_struct *wq = READ_ONCE(work->wq);
	bool busy;

	if (!wq)
		return false;

	if (lkpi_wq_on_thread(wq)) {
		/*
		 * Called from the queue's own worker: blocking would deadlock.
		 * Run a pending instance inline, as a second pool worker
		 * would have done concurrently on Linux.
		 */
		spin_lock(&wq->lock);
		if (work->entry.next && !list_empty(&work->entry)) {
			list_del_init(&work->entry);
			clear_bit(WORK_STRUCT_PENDING_BIT, &work->state);
			spin_unlock(&wq->lock);
			work->func(work);
			return true;
		}
		spin_unlock(&wq->lock);
		return false;
	}

	spin_lock(&wq->lock);
	busy = !lkpi_wq_cond_work_idle(wq, work);
	spin_unlock(&wq->lock);
	if (!busy)
		return false;
	lkpi_wq_wait(wq, lkpi_wq_cond_work_idle, work);
	return true;
}

bool cancel_work(struct work_struct *work)
{
	bool was_pending = lkpi_work_grab(work, NULL);

	clear_bit(WORK_STRUCT_PENDING_BIT, &work->state);
	return was_pending;
}

bool cancel_work_sync(struct work_struct *work)
{
	return lkpi_work_cancel_sync(work, NULL);
}

bool disable_work(struct work_struct *work)
{
	set_bit(WORK_STRUCT_DISABLED_BIT, &work->state);
	return cancel_work(work);
}

bool disable_work_sync(struct work_struct *work)
{
	set_bit(WORK_STRUCT_DISABLED_BIT, &work->state);
	return lkpi_work_cancel_sync(work, NULL);
}

bool enable_work(struct work_struct *work)
{
	/* A single bit stands in for Linux's disable depth counter; the
	 * driver never nests disable_work(). */
	clear_bit(WORK_STRUCT_DISABLED_BIT, &work->state);
	return true;
}

bool work_pending(struct work_struct *work)
{
	return test_bit(WORK_STRUCT_PENDING_BIT, &work->state);
}

bool work_busy(struct work_struct *work)
{
	struct workqueue_struct *wq;

	if (work_pending(work))
		return true;
	wq = READ_ONCE(work->wq);
	return wq && READ_ONCE(wq->running) == work;
}

struct work_struct *current_work(void)
{
	lkpi_os_thread_t self = lkpi_os_thread_self();
	struct workqueue_struct *wq;
	struct work_struct *w = NULL;

	spin_lock(&lkpi_workqueues_lock);
	list_for_each_entry(wq, &lkpi_workqueues, node) {
		if (READ_ONCE(wq->self) == self) {
			w = READ_ONCE(wq->running);
			break;
		}
	}
	spin_unlock(&lkpi_workqueues_lock);
	return w;
}

/* ---- delayed_work --------------------------------------------------- */

static void lkpi_delayed_work_timer_fn(struct timer_list *t)
{
	struct delayed_work *dw = from_timer(dw, t, timer);
	struct workqueue_struct *wq = READ_ONCE(dw->wq);

	if (WARN_ON_ONCE(!wq))
		wq = system_wq;
	lkpi_wq_insert(wq, &dw->work);	/* PENDING held since queueing */
}

void __lkpi_init_delayed_work(struct delayed_work *dw, work_func_t fn)
{
	__lkpi_init_work(&dw->work, fn);
	timer_setup(&dw->timer, lkpi_delayed_work_timer_fn, 0);
	dw->wq = NULL;
}

/* PENDING is owned by the caller. */
static void lkpi_queue_delayed(struct workqueue_struct *wq, struct delayed_work *dw,
			       unsigned long delay)
{
	dw->wq = wq;
	dw->work.wq = wq;
	if (!delay) {
		lkpi_wq_insert(wq, &dw->work);
		return;
	}
	/* DECLARE_DELAYED_WORK() leaves the timer without a function. */
	if (unlikely(!dw->timer.function))
		timer_setup(&dw->timer, lkpi_delayed_work_timer_fn, 0);
	mod_timer(&dw->timer, jiffies + delay);
}

bool queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dw, unsigned long delay)
{
	if (test_bit(WORK_STRUCT_DISABLED_BIT, &dw->work.state))
		return false;
	if (test_and_set_bit(WORK_STRUCT_PENDING_BIT, &dw->work.state))
		return false;
	lkpi_queue_delayed(wq, dw, delay);
	return true;
}

bool mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *dw, unsigned long delay)
{
	bool was_pending;

	if (test_bit(WORK_STRUCT_DISABLED_BIT, &dw->work.state))
		return false;
	was_pending = lkpi_work_grab(&dw->work, &dw->timer);
	lkpi_queue_delayed(wq, dw, delay);
	return was_pending;
}

bool flush_delayed_work(struct delayed_work *dw)
{
	/* Expire the timer now, then flush like a plain work (upstream). */
	if (timer_delete(&dw->timer))
		lkpi_wq_insert(dw->wq ? dw->wq : system_wq, &dw->work);
	return flush_work(&dw->work);
}

bool cancel_delayed_work(struct delayed_work *dw)
{
	bool was_pending = lkpi_work_grab(&dw->work, &dw->timer);

	clear_bit(WORK_STRUCT_PENDING_BIT, &dw->work.state);
	return was_pending;
}

bool cancel_delayed_work_sync(struct delayed_work *dw)
{
	return lkpi_work_cancel_sync(&dw->work, &dw->timer);
}

bool disable_delayed_work(struct delayed_work *dw)
{
	set_bit(WORK_STRUCT_DISABLED_BIT, &dw->work.state);
	return cancel_delayed_work(dw);
}

bool disable_delayed_work_sync(struct delayed_work *dw)
{
	set_bit(WORK_STRUCT_DISABLED_BIT, &dw->work.state);
	return lkpi_work_cancel_sync(&dw->work, &dw->timer);
}

bool enable_delayed_work(struct delayed_work *dw)
{
	return enable_work(&dw->work);
}

/* ---- rcu_work ------------------------------------------------------- */

static void lkpi_rcu_work_rcufn(struct rcu_head *rcu)
{
	struct rcu_work *rw = container_of(rcu, struct rcu_work, rcu);

	lkpi_wq_insert(rw->wq ? rw->wq : system_wq, &rw->work);
}

bool queue_rcu_work(struct workqueue_struct *wq, struct rcu_work *rw)
{
	if (test_bit(WORK_STRUCT_DISABLED_BIT, &rw->work.state))
		return false;
	if (test_and_set_bit(WORK_STRUCT_PENDING_BIT, &rw->work.state))
		return false;
	rw->wq = wq;
	rw->work.wq = wq;
	call_rcu(&rw->rcu, lkpi_rcu_work_rcufn);
	return true;
}

bool flush_rcu_work(struct rcu_work *rw)
{
	if (test_bit(WORK_STRUCT_PENDING_BIT, &rw->work.state)) {
		rcu_barrier();
		flush_work(&rw->work);
		return true;
	}
	return flush_work(&rw->work);
}

/* ---- workqueue_struct ----------------------------------------------- */

struct workqueue_struct *alloc_workqueue(const char *fmt, unsigned int flags, int max_active, ...)
{
	struct workqueue_struct *wq;
	lkpi_os_thread_t thread;
	va_list ap;

	wq = kzalloc(sizeof(*wq), GFP_KERNEL);
	if (!wq)
		return NULL;

	va_start(ap, max_active);
	vsnprintf(wq->name, sizeof(wq->name), fmt, ap);
	va_end(ap);

	wq->flags = flags;
	INIT_LIST_HEAD(&wq->pending);
	INIT_LIST_HEAD(&wq->node);
	spin_lock_init(&wq->lock);
	mutex_init(&wq->wait_lock);

	spin_lock(&lkpi_workqueues_lock);
	list_add_tail(&wq->node, &lkpi_workqueues);
	spin_unlock(&lkpi_workqueues_lock);

	thread = lkpi_os_thread_create(lkpi_wq_thread, wq, wq->name);
	if (!thread) {
		pr_err("lkpi: cannot create workqueue thread %s\n", wq->name);
		spin_lock(&lkpi_workqueues_lock);
		list_del(&wq->node);
		spin_unlock(&lkpi_workqueues_lock);
		mutex_destroy(&wq->wait_lock);
		spin_lock_destroy(&wq->lock);
		kfree(wq);
		return NULL;
	}
	if (flags & (WQ_HIGHPRI | WQ_BH))
		lkpi_os_thread_set_realtime(thread, true);
	__atomic_store_n(&wq->thread, thread, __ATOMIC_RELEASE);
	return wq;
}

void flush_workqueue(struct workqueue_struct *wq)
{
	if (!wq || lkpi_wq_on_thread(wq))
		return;	/* from its own worker: nothing sensible to wait for */
	lkpi_wq_wait(wq, lkpi_wq_cond_queue_idle, NULL);
}

void drain_workqueue(struct workqueue_struct *wq)
{
	if (!wq || lkpi_wq_on_thread(wq))
		return;
	spin_lock(&wq->lock);
	wq->draining = true;
	spin_unlock(&wq->lock);
	/* Items may re-queue themselves while draining; wait them out. */
	do {
		lkpi_wq_wait(wq, lkpi_wq_cond_queue_idle, NULL);
	} while (!list_empty(&wq->pending) || READ_ONCE(wq->running));
	spin_lock(&wq->lock);
	wq->draining = false;
	spin_unlock(&wq->lock);
}

void flush_scheduled_work(void)
{
	flush_workqueue(system_wq);
}

void destroy_workqueue(struct workqueue_struct *wq)
{
	lkpi_os_mutex_t m;

	if (!wq)
		return;
	if (WARN_ON(lkpi_wq_on_thread(wq)))
		return;

	drain_workqueue(wq);

	spin_lock(&wq->lock);
	wq->stop = true;
	spin_unlock(&wq->lock);
	lkpi_os_thread_wake(wq->thread);

	/* Join: the worker sets ->exited under wait_lock just before exiting. */
	m = __lkpi_mutex_get(&wq->wait_lock);
	lkpi_os_mutex_lock(m);
	while (!wq->exited)
		(void)lkpi_os_mutex_sleep(m, &wq->exited, 10ULL * 1000 * 1000);
	lkpi_os_mutex_unlock(m);
	lkpi_os_thread_release(wq->thread);

	spin_lock(&lkpi_workqueues_lock);
	list_del(&wq->node);
	spin_unlock(&lkpi_workqueues_lock);

	mutex_destroy(&wq->wait_lock);
	spin_lock_destroy(&wq->lock);
	kfree(wq);
}

/* ---- runtime hooks -------------------------------------------------- */

int lkpi_work_init(void)
{
	system_wq = alloc_workqueue("lkpi-wq", 0, 0);
	system_highpri_wq = alloc_workqueue("lkpi-wq-hi", WQ_HIGHPRI, 0);
	system_long_wq = alloc_workqueue("lkpi-wq-long", 0, 0);
	if (!system_wq || !system_highpri_wq || !system_long_wq) {
		lkpi_work_fini();
		return -ENOMEM;
	}
	system_unbound_wq = system_wq;
	system_freezable_wq = system_wq;
	system_power_efficient_wq = system_wq;
	system_percpu_wq = system_wq;
	system_dfl_wq = system_wq;
	system_bh_wq = system_highpri_wq;
	return 0;
}

void lkpi_work_fini(void)
{
	destroy_workqueue(system_long_wq);
	destroy_workqueue(system_highpri_wq);
	destroy_workqueue(system_wq);
	system_wq = system_highpri_wq = system_long_wq = NULL;
	system_unbound_wq = system_freezable_wq = system_power_efficient_wq = NULL;
	system_percpu_wq = system_dfl_wq = system_bh_wq = NULL;
}
