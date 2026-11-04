// SPDX-License-Identifier: MIT
/*
 * kthreads and the scheduler idioms built on them.
 *
 * A struct task_struct is allocated per kthread_create(); the backend thread
 * starts parked and runs the thread function only after the first
 * wake_up_process() (kthread_run() does that immediately).  The trampoline
 * stores the task pointer in the backend's per-thread slot so that
 * `current` finds it; threads this layer did not create (IOKit workloops,
 * user clients, the workqueue and tasklet threads) get the static
 * lkpi_init_task instead.
 *
 * set_current_state()/schedule() follow the Linux contract: schedule()
 * blocks only when the recorded state is not TASK_RUNNING, and
 * wake_up_process() sets TASK_RUNNING and wakes the thread.  The backend's
 * lkpi_os_thread_wait()/wake() pair keeps a wakeup that arrives before the
 * wait, so the usual "set state, test condition, schedule()" sequence has no
 * lost-wakeup window.  A stale stored wakeup only causes a spurious return
 * from schedule(), which every such loop tolerates.
 *
 * Exit and park hand-offs use the task's own IOLock: the thread publishes
 * the flag and wakes sleepers with the lock held and never touches the task
 * again afterwards, so kthread_stop() may free it as soon as it observes
 * the flag.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/sprintf.h>
#include <lkpi/os.h>
#include <lkpi/runtime.h>

/* task_struct::flags bits (PF_KTHREAD is bit 21 and does not collide) */
enum {
	KTHREAD_SHOULD_STOP = 0,
	KTHREAD_SHOULD_PARK,
	KTHREAD_IS_PARKED,
	KTHREAD_STARTED,	/* first wake_up_process() seen */
	KTHREAD_EXITED,
};

struct lkpi_kthread {
	struct task_struct task;
	lkpi_os_mutex_t lock;		/* exited/parked publication */
	bool exited;
};

#define LKPI_KTHREAD_JOIN_SLICE_NS	(10ULL * 1000 * 1000)

static struct task_struct lkpi_init_task = {
	.comm = "lkpi-init",
	.state = TASK_RUNNING,
	.entry = LIST_HEAD_INIT(lkpi_init_task.entry),
};

static LIST_HEAD(lkpi_kthreads);
static DEFINE_SPINLOCK(lkpi_kthreads_lock);
static int lkpi_kthread_next_pid = 1;

struct task_struct *lkpi_current(void)
{
	struct task_struct *t = lkpi_os_thread_get_slot();

	return t ? t : &lkpi_init_task;
}

static inline bool lkpi_is_kthread(const struct task_struct *t)
{
	return t && t != &lkpi_init_task;
}

static inline struct lkpi_kthread *to_kthread(struct task_struct *t)
{
	return container_of(t, struct lkpi_kthread, task);
}

static void lkpi_kthread_main(void *arg)
{
	struct lkpi_kthread *k = arg;
	struct task_struct *t = &k->task;
	lkpi_os_thread_t os;
	int ret = -EINTR;

	/* kthread_create() publishes the handle right after creating us. */
	while (!(os = __atomic_load_n(&t->os, __ATOMIC_ACQUIRE)))
		lkpi_os_thread_yield();
	lkpi_os_thread_set_slot(t);

	/* Parked until the first wake_up_process(). */
	while (!test_bit(KTHREAD_STARTED, &t->flags))
		(void)lkpi_os_thread_wait(os, 0);
	WRITE_ONCE(t->state, TASK_RUNNING);

	if (!test_bit(KTHREAD_SHOULD_STOP, &t->flags)) {
		kthread_parkme();	/* kthread_park() before the first run */
		ret = t->fn(t->data);
	}
	t->exit_code = ret;

	lkpi_os_mutex_lock(k->lock);
	set_bit(KTHREAD_EXITED, &t->flags);
	k->exited = true;
	lkpi_os_wakeup(k->lock, &k->exited, false);
	lkpi_os_wakeup(k->lock, &t->flags, false);	/* a parker, if any */
	lkpi_os_mutex_unlock(k->lock);
	/* @k may be freed from here on. */
	lkpi_os_thread_exit();
}

static void lkpi_kthread_free(struct lkpi_kthread *k)
{
	spin_lock(&lkpi_kthreads_lock);
	list_del(&k->task.entry);
	spin_unlock(&lkpi_kthreads_lock);
	if (k->task.os)
		lkpi_os_thread_release(k->task.os);
	lkpi_os_mutex_free(k->lock);
	kfree(k);
}

struct task_struct *kthread_create(int (*threadfn)(void *data), void *data, const char *namefmt, ...)
{
	struct lkpi_kthread *k;
	struct task_struct *t;
	lkpi_os_thread_t os;
	va_list ap;

	k = kzalloc(sizeof(*k), GFP_KERNEL);
	if (!k)
		return ERR_PTR(-ENOMEM);
	k->lock = lkpi_os_mutex_alloc();
	if (!k->lock) {
		kfree(k);
		return ERR_PTR(-ENOMEM);
	}

	t = &k->task;
	t->fn = threadfn;
	t->data = data;
	t->state = TASK_UNINTERRUPTIBLE;	/* not started yet */
	t->flags = PF_KTHREAD;
	va_start(ap, namefmt);
	vsnprintf(t->comm, sizeof(t->comm), namefmt, ap);
	va_end(ap);

	spin_lock(&lkpi_kthreads_lock);
	t->pid = lkpi_kthread_next_pid++;
	list_add_tail(&t->entry, &lkpi_kthreads);
	spin_unlock(&lkpi_kthreads_lock);

	os = lkpi_os_thread_create(lkpi_kthread_main, k, t->comm);
	if (!os) {
		pr_err("lkpi: cannot create kthread %s\n", t->comm);
		lkpi_kthread_free(k);
		return ERR_PTR(-ENOMEM);
	}
	__atomic_store_n(&t->os, os, __ATOMIC_RELEASE);
	return t;
}

int wake_up_process(struct task_struct *p)
{
	unsigned long old;

	if (!lkpi_is_kthread(p) || !p->os)
		return 0;
	set_bit(KTHREAD_STARTED, &p->flags);
	old = __atomic_exchange_n(&p->state, TASK_RUNNING, __ATOMIC_SEQ_CST);
	lkpi_os_thread_wake(p->os);
	return old != TASK_RUNNING;
}

int kthread_stop(struct task_struct *t)
{
	struct lkpi_kthread *k;
	int ret;

	if (!lkpi_is_kthread(t))
		return -EINVAL;
	k = to_kthread(t);

	set_bit(KTHREAD_SHOULD_STOP, &t->flags);
	kthread_unpark(t);
	wake_up_process(t);

	lkpi_os_mutex_lock(k->lock);
	while (!k->exited)
		(void)lkpi_os_mutex_sleep(k->lock, &k->exited, LKPI_KTHREAD_JOIN_SLICE_NS);
	lkpi_os_mutex_unlock(k->lock);

	ret = t->exit_code;
	lkpi_kthread_free(k);
	return ret;
}

bool kthread_should_stop(void)
{
	struct task_struct *t = lkpi_current();

	return lkpi_is_kthread(t) && test_bit(KTHREAD_SHOULD_STOP, &t->flags);
}

bool kthread_should_park(void)
{
	struct task_struct *t = lkpi_current();

	return lkpi_is_kthread(t) && test_bit(KTHREAD_SHOULD_PARK, &t->flags);
}

int kthread_park(struct task_struct *t)
{
	struct lkpi_kthread *k;

	if (!lkpi_is_kthread(t))
		return -EINVAL;
	k = to_kthread(t);
	if (test_bit(KTHREAD_EXITED, &t->flags))
		return -ENOSYS;
	if (test_and_set_bit(KTHREAD_SHOULD_PARK, &t->flags))
		return -EBUSY;

	if (t != lkpi_current()) {
		wake_up_process(t);
		lkpi_os_mutex_lock(k->lock);
		while (!test_bit(KTHREAD_IS_PARKED, &t->flags) && !k->exited)
			(void)lkpi_os_mutex_sleep(k->lock, &t->flags, LKPI_KTHREAD_JOIN_SLICE_NS);
		lkpi_os_mutex_unlock(k->lock);
	}
	return 0;
}

void kthread_unpark(struct task_struct *t)
{
	if (!lkpi_is_kthread(t))
		return;
	clear_bit(KTHREAD_SHOULD_PARK, &t->flags);
	if (t->os)
		lkpi_os_thread_wake(t->os);
}

void kthread_parkme(void)
{
	struct task_struct *t = lkpi_current();
	struct lkpi_kthread *k;

	if (!lkpi_is_kthread(t))
		return;
	k = to_kthread(t);

	while (test_bit(KTHREAD_SHOULD_PARK, &t->flags)) {
		if (!test_bit(KTHREAD_IS_PARKED, &t->flags)) {
			lkpi_os_mutex_lock(k->lock);
			set_bit(KTHREAD_IS_PARKED, &t->flags);
			lkpi_os_wakeup(k->lock, &t->flags, false);
			lkpi_os_mutex_unlock(k->lock);
		}
		WRITE_ONCE(t->state, TASK_UNINTERRUPTIBLE);
		(void)lkpi_os_thread_wait(t->os, 0);
	}
	clear_bit(KTHREAD_IS_PARKED, &t->flags);
	WRITE_ONCE(t->state, TASK_RUNNING);
}

void *kthread_data(struct task_struct *t)
{
	return lkpi_is_kthread(t) ? t->data : NULL;
}

/* ---- sched ---------------------------------------------------------- */

void __set_current_state(long state)
{
	struct task_struct *t = lkpi_current();

	if (lkpi_is_kthread(t))
		WRITE_ONCE(t->state, (unsigned long)state);
}

void schedule(void)
{
	struct task_struct *t = lkpi_current();

	if (!lkpi_is_kthread(t)) {
		/* Foreign thread: nothing to block on, give the CPU away. */
		if (lkpi_os_may_sleep())
			lkpi_os_sleep_ms(1);
		else
			lkpi_os_thread_yield();
		return;
	}
	if (READ_ONCE(t->state) == TASK_RUNNING) {
		lkpi_os_thread_yield();
		return;
	}
	(void)lkpi_os_thread_wait(t->os, 0);
	WRITE_ONCE(t->state, TASK_RUNNING);
}

long schedule_timeout(long timeout)
{
	struct task_struct *t = lkpi_current();
	unsigned long deadline;
	long remaining;

	if (timeout == MAX_SCHEDULE_TIMEOUT) {
		schedule();
		return timeout;
	}
	if (timeout <= 0) {
		if (timeout < 0)
			pr_err("lkpi: schedule_timeout: wrong timeout value %lx\n", timeout);
		__set_current_state(TASK_RUNNING);
		lkpi_os_thread_yield();
		return 0;
	}

	deadline = jiffies + (unsigned long)timeout;
	if (!lkpi_is_kthread(t)) {
		lkpi_os_sleep_ms(jiffies_to_msecs((unsigned long)timeout));
	} else if (READ_ONCE(t->state) == TASK_RUNNING) {
		lkpi_os_thread_yield();
	} else {
		(void)lkpi_os_thread_wait(t->os, jiffies_to_nsecs((unsigned long)timeout));
		WRITE_ONCE(t->state, TASK_RUNNING);
	}
	remaining = (long)(deadline - jiffies);
	return remaining > 0 ? remaining : 0;
}

long schedule_timeout_interruptible(long timeout)
{
	__set_current_state(TASK_INTERRUPTIBLE);
	return schedule_timeout(timeout);
}

long schedule_timeout_uninterruptible(long timeout)
{
	__set_current_state(TASK_UNINTERRUPTIBLE);
	return schedule_timeout(timeout);
}

/* ---- runtime hooks -------------------------------------------------- */

int lkpi_kthread_init(void)
{
	lkpi_init_task.state = TASK_RUNNING;
	lkpi_kthread_next_pid = 1;
	return 0;
}

void lkpi_kthread_fini(void)
{
	struct task_struct *t, *n;
	LIST_HEAD(dead);

	/* Reap threads that returned on their own and were never
	 * kthread_stop()ed; anything still running is a driver bug. */
	spin_lock(&lkpi_kthreads_lock);
	list_for_each_entry_safe(t, n, &lkpi_kthreads, entry) {
		if (test_bit(KTHREAD_EXITED, &t->flags))
			list_move_tail(&t->entry, &dead);
		else
			pr_warn("lkpi: kthread %s (pid %d) still alive at shutdown\n",
				t->comm, t->pid);
	}
	spin_unlock(&lkpi_kthreads_lock);

	list_for_each_entry_safe(t, n, &dead, entry) {
		struct lkpi_kthread *k = to_kthread(t);

		list_del(&t->entry);
		if (t->os)
			lkpi_os_thread_release(t->os);
		lkpi_os_mutex_free(k->lock);
		kfree(k);
	}
}
