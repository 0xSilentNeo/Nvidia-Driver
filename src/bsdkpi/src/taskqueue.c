// SPDX-License-Identifier: MIT
/*
 * taskqueue(9) on LinuxKPI work queues.
 *
 * struct task embeds a work_struct; taskqueue_enqueue() bumps ta_pending
 * and queues the work, the work function hands the accumulated count to
 * the task.  A taskqueue owns one ordered workqueue created by
 * taskqueue_start_threads(); until then (and for the taskqueue_swi /
 * taskqueue_thread globals) tasks run on system_wq.  taskqueue_block()
 * parks newly enqueued tasks on a list that taskqueue_unblock() releases.
 * Timeout tasks are a callout that enqueues the task when it fires.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/taskqueue.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>

struct taskqueue {
	struct workqueue_struct	*tq_wq;		/* NULL: system_wq */
	STAILQ_HEAD(, task)	 tq_blocked;	/* enqueued while blocked */
	spinlock_t		 tq_lock;
	char			 tq_name[TASKQUEUE_NAMELEN];
	int			 tq_flags;
	int			 tq_threads;
	taskqueue_enqueue_fn	 tq_enqueue;
	void			*tq_context;
	taskqueue_callback_fn	 tq_callbacks[TASKQUEUE_NUM_CALLBACKS];
	void			*tq_cb_contexts[TASKQUEUE_NUM_CALLBACKS];
};
#define TQ_FLAGS_ACTIVE		(1 << 0)
#define TQ_FLAGS_BLOCKED	(1 << 1)
#define TQ_FLAGS_STATIC		(1 << 2)

#define TA_FLAGS_BLOCKED	0x01	/* on tq_blocked */

static struct workqueue_struct *
tq_wq(struct taskqueue *tq)
{
	struct workqueue_struct *wq = READ_ONCE(tq->tq_wq);

	return (wq != NULL ? wq : system_wq);
}

static void
taskqueue_work_fn(struct work_struct *work)
{
	struct task *task = container_of(work, struct task, ta_work);
	int pending;

	pending = __atomic_exchange_n(&task->ta_pending, 0, __ATOMIC_SEQ_CST);
	if (pending == 0)
		return;
	task->ta_func(task->ta_context, pending);
}

void
_task_init_flags(struct task *task, int priority, task_fn_t func,
    void *context, int flags)
{
	(void)flags;
	memset(task, 0, sizeof(*task));
	INIT_WORK(&task->ta_work, taskqueue_work_fn);
	task->ta_priority = (uint8_t)priority;
	task->ta_func = func;
	task->ta_context = context;
}

static struct taskqueue *
taskqueue_alloc(const char *name, int mflags, taskqueue_enqueue_fn enqueue,
    void *context, int flags)
{
	struct taskqueue *tq;

	tq = malloc(sizeof(*tq), M_TEMP, mflags | M_ZERO);
	if (tq == NULL)
		return (NULL);
	STAILQ_INIT(&tq->tq_blocked);
	spin_lock_init(&tq->tq_lock);
	strlcpy(tq->tq_name, name != NULL ? name : "taskqueue", sizeof(tq->tq_name));
	tq->tq_enqueue = enqueue;
	tq->tq_context = context;
	tq->tq_flags = TQ_FLAGS_ACTIVE | flags;
	return (tq);
}

struct taskqueue *
taskqueue_create(const char *name, int mflags, taskqueue_enqueue_fn enqueue,
    void *context)
{
	return (taskqueue_alloc(name, mflags & ~M_ZERO, enqueue, context, 0));
}

int
taskqueue_start_threads(struct taskqueue **tqp, int count, int pri,
    const char *name, ...)
{
	struct taskqueue *tq = *tqp;
	char buf[TASKQUEUE_NAMELEN];
	va_list ap;

	(void)pri;
	if (tq == NULL)
		return (EINVAL);
	if (count < 1)
		count = 1;
	va_start(ap, name);
	vsnprintf(buf, sizeof(buf), name, ap);
	va_end(ap);
	if (tq->tq_wq != NULL)
		return (0);
	/* one thread per queue keeps FreeBSD's per-queue ordering */
	tq->tq_wq = alloc_ordered_workqueue("%s", 0, buf);
	if (tq->tq_wq == NULL)
		return (ENOMEM);
	tq->tq_threads = count;
	return (0);
}

int
taskqueue_start_threads_in_proc(struct taskqueue **tqp, int count, int pri,
    struct proc *p, const char *name, ...)
{
	char buf[TASKQUEUE_NAMELEN];
	va_list ap;

	(void)p;
	va_start(ap, name);
	vsnprintf(buf, sizeof(buf), name, ap);
	va_end(ap);
	return (taskqueue_start_threads(tqp, count, pri, "%s", buf));
}

void
taskqueue_free(struct taskqueue *tq)
{
	struct workqueue_struct *wq;

	if (tq == NULL)
		return;
	taskqueue_unblock(tq);
	wq = tq->tq_wq;
	tq->tq_wq = NULL;
	if (wq != NULL)
		destroy_workqueue(wq);	/* flushes pending work */
	else
		flush_workqueue(system_wq);
	spin_lock_destroy(&tq->tq_lock);
	if ((tq->tq_flags & TQ_FLAGS_STATIC) == 0)
		free(tq, M_TEMP);
}

int
taskqueue_enqueue_flags(struct taskqueue *tq, struct task *task, int flags)
{
	unsigned long irq;
	uint16_t pending;

	if (tq == NULL)
		return (EPIPE);
	task->ta_queue = tq;
	spin_lock_irqsave(&tq->tq_lock, irq);
	pending = task->ta_pending;
	if (pending > 0 && (flags & TASKQUEUE_FAIL_IF_PENDING)) {
		spin_unlock_irqrestore(&tq->tq_lock, irq);
		return (EEXIST);
	}
	task->ta_pending = pending + 1;
	if (tq->tq_flags & TQ_FLAGS_BLOCKED) {
		if ((task->ta_flags & TA_FLAGS_BLOCKED) == 0) {
			task->ta_flags |= TA_FLAGS_BLOCKED;
			STAILQ_INSERT_TAIL(&tq->tq_blocked, task, ta_link);
		}
		spin_unlock_irqrestore(&tq->tq_lock, irq);
		return (0);
	}
	spin_unlock_irqrestore(&tq->tq_lock, irq);
	queue_work(tq_wq(tq), &task->ta_work);
	return (0);
}

int
taskqueue_enqueue(struct taskqueue *tq, struct task *task)
{
	return (taskqueue_enqueue_flags(tq, task, 0));
}

void
taskqueue_block(struct taskqueue *tq)
{
	unsigned long irq;

	spin_lock_irqsave(&tq->tq_lock, irq);
	tq->tq_flags |= TQ_FLAGS_BLOCKED;
	spin_unlock_irqrestore(&tq->tq_lock, irq);
}

void
taskqueue_unblock(struct taskqueue *tq)
{
	unsigned long irq;
	struct task *task;

	spin_lock_irqsave(&tq->tq_lock, irq);
	tq->tq_flags &= ~TQ_FLAGS_BLOCKED;
	while ((task = STAILQ_FIRST(&tq->tq_blocked)) != NULL) {
		STAILQ_REMOVE_HEAD(&tq->tq_blocked, ta_link);
		task->ta_flags &= ~TA_FLAGS_BLOCKED;
		spin_unlock_irqrestore(&tq->tq_lock, irq);
		queue_work(tq_wq(tq), &task->ta_work);
		spin_lock_irqsave(&tq->tq_lock, irq);
	}
	spin_unlock_irqrestore(&tq->tq_lock, irq);
}

/*
 * Returns 0 if the task was pending and has been removed, or was not
 * pending at all; EBUSY if it is currently running.  *pendp gets the
 * pending count that was cancelled.
 */
int
taskqueue_cancel(struct taskqueue *tq, struct task *task, u_int *pendp)
{
	unsigned long irq;
	u_int pending;
	int error = 0;

	spin_lock_irqsave(&tq->tq_lock, irq);
	pending = task->ta_pending;
	task->ta_pending = 0;
	if (task->ta_flags & TA_FLAGS_BLOCKED) {
		task->ta_flags &= ~TA_FLAGS_BLOCKED;
		STAILQ_REMOVE(&tq->tq_blocked, task, task, ta_link);
	}
	spin_unlock_irqrestore(&tq->tq_lock, irq);

	if (!cancel_work(&task->ta_work) && work_busy(&task->ta_work))
		error = EBUSY;
	if (pendp != NULL)
		*pendp = pending;
	return (error);
}

void
taskqueue_drain(struct taskqueue *tq, struct task *task)
{
	(void)tq;
	flush_work(&task->ta_work);
}

void
taskqueue_drain_all(struct taskqueue *tq)
{
	flush_workqueue(tq_wq(tq));
}

void
taskqueue_quiesce(struct taskqueue *tq)
{
	taskqueue_drain_all(tq);
}

int
taskqueue_poll_is_busy(struct taskqueue *tq, struct task *task)
{
	(void)tq;
	return (work_busy(&task->ta_work) ? 1 : 0);
}

int
taskqueue_member(struct taskqueue *tq, struct thread *td)
{
	(void)tq;
	(void)td;
	return (0);
}

void
taskqueue_set_callback(struct taskqueue *tq,
    enum taskqueue_callback_type cb_type, taskqueue_callback_fn callback,
    void *context)
{
	tq->tq_callbacks[cb_type] = callback;
	tq->tq_cb_contexts[cb_type] = context;
}

void
taskqueue_run(struct taskqueue *tq)
{
	flush_workqueue(tq_wq(tq));
}

void
taskqueue_thread_enqueue(void *context)
{
	/* FreeBSD wakes the queue thread here; work queues do it themselves. */
	(void)context;
}

void
taskqueue_thread_loop(void *arg)
{
	(void)arg;
}

/* ---- timeout tasks -------------------------------------------------- */

static void
taskqueue_timeout_func(void *arg)
{
	struct timeout_task *timeout_task = arg;

	timeout_task->f &= ~1;	/* no longer armed */
	(void)taskqueue_enqueue(timeout_task->q, &timeout_task->t);
}

void
_timeout_task_init(struct taskqueue *queue, struct timeout_task *timeout_task,
    int priority, task_fn_t func, void *context)
{
	TASK_INIT(&timeout_task->t, priority, func, context);
	callout_init(&timeout_task->c, 1);
	timeout_task->q = queue;
	timeout_task->f = 0;
}

int
taskqueue_enqueue_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task, int to_ticks)
{
	int res;

	timeout_task->q = queue;
	res = timeout_task->t.ta_pending;
	if (to_ticks == 0) {
		(void)taskqueue_enqueue(queue, &timeout_task->t);
		return (res);
	}
	if (to_ticks < 0)
		to_ticks = -to_ticks;
	timeout_task->f |= 1;
	callout_reset(&timeout_task->c, to_ticks, taskqueue_timeout_func,
	    timeout_task);
	return (res);
}

int
taskqueue_enqueue_timeout_sbt(struct taskqueue *queue,
    struct timeout_task *timeout_task, sbintime_t sbt, sbintime_t pr, int flags)
{
	(void)pr;
	(void)flags;
	return (taskqueue_enqueue_timeout(queue, timeout_task,
	    (int)(sbt / (SBT_1S / HZ))));
}

int
taskqueue_cancel_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task, u_int *pendp)
{
	u_int pending, pending1;
	int error;

	pending = callout_stop(&timeout_task->c) > 0 ? 1 : 0;
	timeout_task->f &= ~1;
	error = taskqueue_cancel(queue, &timeout_task->t, &pending1);
	if (pendp != NULL)
		*pendp = pending + pending1;
	return (error);
}

void
taskqueue_drain_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task)
{
	callout_drain(&timeout_task->c);
	taskqueue_drain(queue, &timeout_task->t);
}

/* ---- the system queues ---------------------------------------------- */

static struct taskqueue bsdkpi_tq_swi = {
	.tq_blocked = STAILQ_HEAD_INITIALIZER(bsdkpi_tq_swi.tq_blocked),
	.tq_name = "swi", .tq_flags = TQ_FLAGS_ACTIVE | TQ_FLAGS_STATIC,
};
static struct taskqueue bsdkpi_tq_thread = {
	.tq_blocked = STAILQ_HEAD_INITIALIZER(bsdkpi_tq_thread.tq_blocked),
	.tq_name = "thread", .tq_flags = TQ_FLAGS_ACTIVE | TQ_FLAGS_STATIC,
};
struct taskqueue *taskqueue_fast = &bsdkpi_tq_swi;
struct taskqueue *taskqueue_swi = &bsdkpi_tq_swi;
struct taskqueue *taskqueue_swi_giant = &bsdkpi_tq_swi;
struct taskqueue *taskqueue_thread = &bsdkpi_tq_thread;

int
bsdkpi_taskqueue_init(void)
{
	if (bsdkpi_tq_thread.tq_wq == NULL)
		bsdkpi_tq_thread.tq_wq =
		    alloc_ordered_workqueue("bsdkpi_taskq_thread", 0);
	return (bsdkpi_tq_thread.tq_wq != NULL ? 0 : -ENOMEM);
}

void
bsdkpi_taskqueue_fini(void)
{
	struct workqueue_struct *wq = bsdkpi_tq_thread.tq_wq;

	bsdkpi_tq_thread.tq_wq = NULL;
	if (wq != NULL)
		destroy_workqueue(wq);
}
