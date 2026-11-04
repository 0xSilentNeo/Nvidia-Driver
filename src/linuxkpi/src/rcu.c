// SPDX-License-Identifier: MIT
/*
 * RCU approximation.
 *
 * THIS IS NOT RCU.  XNU has no read-side epoch mechanism and the headers
 * make rcu_read_lock()/rcu_read_unlock() plain compiler barriers, so a
 * reader is never tracked.  synchronize_rcu() instead waits until every
 * thread this layer knows about has passed a point at which it cannot be
 * inside a read-side section:
 *
 *   - each system workqueue runs an empty work item (a worker between two
 *     items is not in a critical section);
 *   - the tasklet thread runs an empty tasklet;
 *   - every other thread (the IOKit interrupt and timer workloops, NAPI
 *     polling, user clients, driver kthreads) is covered only by a 2 ms
 *     sleep, on the assumption that read-side sections are short and never
 *     block, which holds for the mt76 code paths.
 *
 * What this guarantees: a reader that started before synchronize_rcu() was
 * called and that runs on a workqueue or the tasklet thread has finished
 * when it returns.  What it does not guarantee: a reader on a foreign
 * thread that was preempted or took longer than 2 ms may still see the old
 * pointer.  The driver's RCU-protected objects (wcid, link and vif tables)
 * are read on the RX/TX paths with short sections, which is why this is an
 * accepted trade-off; FreeBSD's LinuxKPI lived with the same approximation
 * before ck_epoch.
 *
 * The wait is bounded (LKPI_RCU_GP_TIMEOUT_MS) so that synchronize_rcu()
 * can not deadlock when it is called from a workqueue thread that another
 * grace period is waiting on; the calling thread's own queue is skipped
 * because the caller is, by definition, not inside a read-side section.
 *
 * call_rcu()/kfree_rcu() append to a list drained by a delayed work (20 ms
 * batching) that runs one grace period and then the callbacks.  kfree_rcu()
 * stores the offset in ->func, like upstream's __is_kvfree_rcu_offset().
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/rcupdate.h>
#include <lkpi/os.h>
#include <lkpi/runtime.h>

#define LKPI_RCU_GP_TIMEOUT_MS		200
#define LKPI_RCU_FOREIGN_SETTLE_MS	2
#define LKPI_RCU_BATCH_MS		20
#define LKPI_RCU_KFREE_OFFSET_MAX	4096

struct lkpi_rcu_qs {
	struct work_struct work;
	atomic_t done;
};

/* One per system_* pointer; aliases share a queue and are skipped. */
#define LKPI_RCU_NR_WQ	9
static struct lkpi_rcu_qs lkpi_rcu_qs[LKPI_RCU_NR_WQ];
static struct tasklet_struct lkpi_rcu_qs_tasklet;
static atomic_t lkpi_rcu_tasklet_done;
static DECLARE_WAIT_QUEUE_HEAD(lkpi_rcu_wait);

static DEFINE_SPINLOCK(lkpi_rcu_cb_lock);
static struct rcu_head *lkpi_rcu_cb_head;
static struct rcu_head **lkpi_rcu_cb_tail = &lkpi_rcu_cb_head;
static struct delayed_work lkpi_rcu_drain_work;
static bool lkpi_rcu_ready;

static void lkpi_rcu_qs_work_fn(struct work_struct *w)
{
	struct lkpi_rcu_qs *qs = container_of(w, struct lkpi_rcu_qs, work);

	atomic_inc(&qs->done);
	wake_up_all(&lkpi_rcu_wait);
}

static void lkpi_rcu_qs_tasklet_fn(struct tasklet_struct *t)
{
	atomic_inc(&lkpi_rcu_tasklet_done);
	wake_up_all(&lkpi_rcu_wait);
}

struct lkpi_rcu_gp {
	bool use[LKPI_RCU_NR_WQ];
	int target[LKPI_RCU_NR_WQ];
	int tasklet_target;
};

static bool lkpi_rcu_gp_done(const struct lkpi_rcu_gp *gp)
{
	int i;

	for (i = 0; i < LKPI_RCU_NR_WQ; i++)
		if (gp->use[i] && atomic_read(&lkpi_rcu_qs[i].done) - gp->target[i] < 0)
			return false;
	return atomic_read(&lkpi_rcu_tasklet_done) - gp->tasklet_target >= 0;
}

void synchronize_rcu(void)
{
	struct workqueue_struct *const wqs[LKPI_RCU_NR_WQ] = {
		system_wq, system_highpri_wq, system_long_wq, system_unbound_wq,
		system_freezable_wq, system_power_efficient_wq, system_percpu_wq,
		system_dfl_wq, system_bh_wq,
	};
	struct lkpi_rcu_gp gp;
	struct work_struct *cw;
	struct workqueue_struct *own_wq;
	int i, j;

	WARN_ON_ONCE(!lkpi_os_may_sleep());

	if (!READ_ONCE(lkpi_rcu_ready)) {
		/* Before init or after fini: only the settle time is left. */
		lkpi_os_sleep_ms(LKPI_RCU_FOREIGN_SETTLE_MS);
		return;
	}

	/* A caller on a workqueue thread is itself quiescent for that queue. */
	cw = current_work();
	own_wq = cw ? cw->wq : NULL;

	for (i = 0; i < LKPI_RCU_NR_WQ; i++) {
		gp.use[i] = wqs[i] && wqs[i] != own_wq;
		for (j = 0; j < i && gp.use[i]; j++)
			if (wqs[j] == wqs[i])
				gp.use[i] = false;
		if (!gp.use[i])
			continue;
		gp.target[i] = atomic_read(&lkpi_rcu_qs[i].done) + 1;
		queue_work(wqs[i], &lkpi_rcu_qs[i].work);
	}
	gp.tasklet_target = atomic_read(&lkpi_rcu_tasklet_done) + 1;
	tasklet_schedule(&lkpi_rcu_qs_tasklet);

	if (!wait_event_timeout(lkpi_rcu_wait, lkpi_rcu_gp_done(&gp),
				msecs_to_jiffies(LKPI_RCU_GP_TIMEOUT_MS)))
		pr_warn_once("lkpi: rcu grace period timed out after %d ms\n",
			     LKPI_RCU_GP_TIMEOUT_MS);

	/* Foreign threads: interrupt/timer workloops, NAPI, user clients. */
	lkpi_os_sleep_ms(LKPI_RCU_FOREIGN_SETTLE_MS);
}

static void lkpi_rcu_run_callbacks(struct rcu_head *head)
{
	struct rcu_head *next;

	while (head) {
		uintptr_t off = (uintptr_t)head->func;

		next = head->next;
		if (off < LKPI_RCU_KFREE_OFFSET_MAX)
			kfree((char *)head - off);
		else
			head->func(head);
		head = next;
	}
}

static struct rcu_head *lkpi_rcu_take_callbacks(void)
{
	struct rcu_head *head;

	spin_lock(&lkpi_rcu_cb_lock);
	head = lkpi_rcu_cb_head;
	lkpi_rcu_cb_head = NULL;
	lkpi_rcu_cb_tail = &lkpi_rcu_cb_head;
	spin_unlock(&lkpi_rcu_cb_lock);
	return head;
}

static void lkpi_rcu_drain_fn(struct work_struct *w)
{
	struct rcu_head *head = lkpi_rcu_take_callbacks();

	if (!head)
		return;
	synchronize_rcu();
	lkpi_rcu_run_callbacks(head);
}

void call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	head->func = func;
	head->next = NULL;

	spin_lock(&lkpi_rcu_cb_lock);
	*lkpi_rcu_cb_tail = head;
	lkpi_rcu_cb_tail = &head->next;
	spin_unlock(&lkpi_rcu_cb_lock);

	if (READ_ONCE(lkpi_rcu_ready))
		queue_delayed_work(system_wq, &lkpi_rcu_drain_work,
				   msecs_to_jiffies(LKPI_RCU_BATCH_MS));
}

void lkpi_kfree_rcu(struct rcu_head *head, size_t offset)
{
	BUG_ON(offset >= LKPI_RCU_KFREE_OFFSET_MAX);
	call_rcu(head, (rcu_callback_t)offset);
}

/* ---- runtime hooks -------------------------------------------------- */

int lkpi_rcu_init(void)
{
	int i;

	for (i = 0; i < LKPI_RCU_NR_WQ; i++) {
		INIT_WORK(&lkpi_rcu_qs[i].work, lkpi_rcu_qs_work_fn);
		atomic_set(&lkpi_rcu_qs[i].done, 0);
	}
	tasklet_setup(&lkpi_rcu_qs_tasklet, lkpi_rcu_qs_tasklet_fn);
	atomic_set(&lkpi_rcu_tasklet_done, 0);
	init_waitqueue_head(&lkpi_rcu_wait);
	/* Eager: creates the backend timer so call_rcu() never allocates. */
	INIT_DELAYED_WORK(&lkpi_rcu_drain_work, lkpi_rcu_drain_fn);
	WRITE_ONCE(lkpi_rcu_ready, true);
	return 0;
}

void lkpi_rcu_fini(void)
{
	struct rcu_head *head;

	if (!lkpi_rcu_ready)
		return;
	cancel_delayed_work_sync(&lkpi_rcu_drain_work);
	head = lkpi_rcu_take_callbacks();
	if (head) {
		synchronize_rcu();
		lkpi_rcu_run_callbacks(head);
	}
	WRITE_ONCE(lkpi_rcu_ready, false);
	tasklet_kill(&lkpi_rcu_qs_tasklet);
	timer_destroy(&lkpi_rcu_drain_work.timer);
	mutex_destroy(&lkpi_rcu_wait.m);
}
