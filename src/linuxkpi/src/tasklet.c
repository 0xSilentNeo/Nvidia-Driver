// SPDX-License-Identifier: MIT
/*
 * Tasklets on one high-priority backend thread.
 *
 * Linux runs tasklets from the softirq context that follows the hardirq;
 * here the interrupt handler runs on the IOKit interrupt workloop and
 * tasklet_schedule() hands the tasklet to the "lkpi-tasklet" realtime
 * thread, which runs scheduled tasklets in FIFO order.  The state machine is
 * the upstream one: TASKLET_STATE_SCHED is set by the scheduler and cleared
 * by the runner right before the callback, TASKLET_STATE_RUN is held while
 * the callback executes, and a tasklet whose disable count is non-zero (or
 * that is running elsewhere) stays scheduled and is retried later, which is
 * what the softirq re-raise does on Linux.  tasklet_kill() therefore waits
 * for SCHED to clear and must not be called on a disabled tasklet, exactly
 * like upstream.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <lkpi/os.h>
#include <lkpi/runtime.h>

static struct {
	spinlock_t lock;		/* queue, stop */
	struct list_head queue;
	lkpi_os_thread_t thread;
	lkpi_os_mutex_t exit_lock;
	bool stop;
	bool exited;
} lkpi_tasklets = {
	.lock = __SPIN_LOCK_UNLOCKED(lkpi_tasklets.lock),
	.queue = LIST_HEAD_INIT(lkpi_tasklets.queue),
};

#define LKPI_TASKLET_RETRY_NS	(10ULL * 1000 * 1000)	/* while a scheduled tasklet is disabled */

static void lkpi_tasklet_relax(void)
{
	if (lkpi_os_may_sleep())
		lkpi_os_sleep_ms(1);
	else
		lkpi_os_thread_yield();
}

static void lkpi_tasklet_thread(void *arg)
{
	lkpi_os_thread_t self;

	while (!(self = __atomic_load_n(&lkpi_tasklets.thread, __ATOMIC_ACQUIRE)))
		lkpi_os_thread_yield();

	for (;;) {
		struct tasklet_struct *t, *n;
		LIST_HEAD(local);
		bool deferred = false;
		bool stop;

		spin_lock(&lkpi_tasklets.lock);
		list_splice_init(&lkpi_tasklets.queue, &local);
		stop = lkpi_tasklets.stop;
		spin_unlock(&lkpi_tasklets.lock);

		if (list_empty(&local)) {
			if (stop)
				break;
			(void)lkpi_os_thread_wait(self, 0);	/* until scheduled */
			continue;
		}

		list_for_each_entry_safe(t, n, &local, entry) {
			list_del_init(&t->entry);
			if (tasklet_trylock(t)) {
				if (!atomic_read(&t->count)) {
					if (!test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
						WARN_ON_ONCE(1);
					if (t->use_callback)
						t->callback(t);
					else
						t->func(t->data);
					tasklet_unlock(t);
					continue;
				}
				tasklet_unlock(t);
			}
			/* Disabled, or running on another thread: keep it
			 * scheduled and try again on the next pass. */
			spin_lock(&lkpi_tasklets.lock);
			list_add_tail(&t->entry, &lkpi_tasklets.queue);
			spin_unlock(&lkpi_tasklets.lock);
			deferred = true;
		}

		/* Do not spin while a scheduled tasklet stays disabled;
		 * tasklet_enable() wakes us early. */
		if (deferred && !stop)
			(void)lkpi_os_thread_wait(self, LKPI_TASKLET_RETRY_NS);
	}

	lkpi_os_mutex_lock(lkpi_tasklets.exit_lock);
	lkpi_tasklets.exited = true;
	lkpi_os_wakeup(lkpi_tasklets.exit_lock, &lkpi_tasklets.exited, false);
	lkpi_os_mutex_unlock(lkpi_tasklets.exit_lock);
	lkpi_os_thread_exit();
}

void tasklet_init(struct tasklet_struct *t, void (*func)(unsigned long), unsigned long data)
{
	INIT_LIST_HEAD(&t->entry);
	t->state = 0;
	atomic_set(&t->count, 0);
	t->use_callback = false;
	t->func = func;
	t->data = data;
}

void tasklet_setup(struct tasklet_struct *t, void (*callback)(struct tasklet_struct *))
{
	INIT_LIST_HEAD(&t->entry);
	t->state = 0;
	atomic_set(&t->count, 0);
	t->use_callback = true;
	t->callback = callback;
	t->data = 0;
}

void tasklet_schedule(struct tasklet_struct *t)
{
	if (test_and_set_bit(TASKLET_STATE_SCHED, &t->state))
		return;
	spin_lock(&lkpi_tasklets.lock);
	list_add_tail(&t->entry, &lkpi_tasklets.queue);
	spin_unlock(&lkpi_tasklets.lock);
	if (lkpi_tasklets.thread)
		lkpi_os_thread_wake(lkpi_tasklets.thread);
}

void tasklet_unlock_wait(struct tasklet_struct *t)
{
	while (test_bit(TASKLET_STATE_RUN, &t->state))
		lkpi_tasklet_relax();
}

void tasklet_disable(struct tasklet_struct *t)
{
	atomic_inc(&t->count);
	smp_mb__after_atomic();
	tasklet_unlock_wait(t);
	smp_mb();
}

void tasklet_enable(struct tasklet_struct *t)
{
	smp_mb__before_atomic();
	atomic_dec(&t->count);
	/* A scheduled tasklet may have been deferred while disabled. */
	if (test_bit(TASKLET_STATE_SCHED, &t->state) && lkpi_tasklets.thread)
		lkpi_os_thread_wake(lkpi_tasklets.thread);
}

void tasklet_kill(struct tasklet_struct *t)
{
	WARN_ON_ONCE(!lkpi_os_may_sleep());

	/* Own SCHED so that nobody can re-schedule it while we wait, then
	 * wait for a run that is already in flight. */
	while (test_and_set_bit(TASKLET_STATE_SCHED, &t->state)) {
		do {
			lkpi_tasklet_relax();
		} while (test_bit(TASKLET_STATE_SCHED, &t->state));
	}
	tasklet_unlock_wait(t);
	clear_bit(TASKLET_STATE_SCHED, &t->state);
}

/* ---- runtime hooks -------------------------------------------------- */

int lkpi_tasklet_init(void)
{
	lkpi_os_thread_t thread;

	spin_lock_init(&lkpi_tasklets.lock);
	INIT_LIST_HEAD(&lkpi_tasklets.queue);
	lkpi_tasklets.stop = false;
	lkpi_tasklets.exited = false;
	lkpi_tasklets.exit_lock = lkpi_os_mutex_alloc();
	if (!lkpi_tasklets.exit_lock)
		return -ENOMEM;

	thread = lkpi_os_thread_create(lkpi_tasklet_thread, NULL, "lkpi-tasklet");
	if (!thread) {
		lkpi_os_mutex_free(lkpi_tasklets.exit_lock);
		lkpi_tasklets.exit_lock = NULL;
		return -ENOMEM;
	}
	lkpi_os_thread_set_realtime(thread, true);
	__atomic_store_n(&lkpi_tasklets.thread, thread, __ATOMIC_RELEASE);
	return 0;
}

void lkpi_tasklet_fini(void)
{
	lkpi_os_thread_t thread = lkpi_tasklets.thread;

	if (!thread)
		return;

	spin_lock(&lkpi_tasklets.lock);
	lkpi_tasklets.stop = true;
	spin_unlock(&lkpi_tasklets.lock);
	lkpi_os_thread_wake(thread);

	lkpi_os_mutex_lock(lkpi_tasklets.exit_lock);
	while (!lkpi_tasklets.exited)
		(void)lkpi_os_mutex_sleep(lkpi_tasklets.exit_lock, &lkpi_tasklets.exited,
					  10ULL * 1000 * 1000);
	lkpi_os_mutex_unlock(lkpi_tasklets.exit_lock);
	lkpi_os_thread_release(thread);
	lkpi_tasklets.thread = NULL;

	/* Anything still scheduled belongs to a driver that did not
	 * tasklet_kill() before unloading; just forget it. */
	INIT_LIST_HEAD(&lkpi_tasklets.queue);

	lkpi_os_mutex_free(lkpi_tasklets.exit_lock);
	lkpi_tasklets.exit_lock = NULL;
	spin_lock_destroy(&lkpi_tasklets.lock);
}
