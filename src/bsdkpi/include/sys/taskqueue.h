/* SPDX-License-Identifier: MIT */
/*
 * sys/taskqueue.h - taskqueue(9) on LinuxKPI work queues.
 *
 * A taskqueue owns one ordered workqueue (one thread; taskqueue_start_threads
 * with count > 1 is accepted and still gives one thread, which is what
 * net80211 asks for).  taskqueue_swi/taskqueue_thread run on system_wq.
 * Implementation in src/taskqueue.c.
 */
#ifndef _BSDKPI_SYS_TASKQUEUE_H_
#define _BSDKPI_SYS_TASKQUEUE_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/_task.h>
#include <sys/_callout.h>
#include <sys/stdarg.h>

#define TASKQUEUE_NAMELEN	32
#define TASKQUEUE_FAIL_IF_PENDING	(1 << 0)
#define TASKQUEUE_FAIL_IF_CANCELING	(1 << 1)

typedef void (*taskqueue_enqueue_fn)(void *context);
typedef void (*taskqueue_callback_fn)(void *context);

enum taskqueue_callback_type {
	TASKQUEUE_CALLBACK_TYPE_INIT,
	TASKQUEUE_CALLBACK_TYPE_SHUTDOWN,
};
#define TASKQUEUE_CALLBACK_TYPE_MIN	TASKQUEUE_CALLBACK_TYPE_INIT
#define TASKQUEUE_CALLBACK_TYPE_MAX	TASKQUEUE_CALLBACK_TYPE_SHUTDOWN
#define TASKQUEUE_NUM_CALLBACKS		(TASKQUEUE_CALLBACK_TYPE_MAX + 1)

struct taskqueue *taskqueue_create(const char *name, int mflags,
	    taskqueue_enqueue_fn enqueue, void *context);
#define taskqueue_create_fast(name, mflags, enqueue, context) \
	taskqueue_create((name), (mflags), (enqueue), (context))
int	taskqueue_start_threads(struct taskqueue **tqp, int count, int pri,
	    const char *name, ...) __printflike(4, 5);
int	taskqueue_start_threads_in_proc(struct taskqueue **tqp, int count,
	    int pri, struct proc *p, const char *name, ...) __printflike(5, 6);
int	taskqueue_enqueue(struct taskqueue *queue, struct task *task);
int	taskqueue_enqueue_flags(struct taskqueue *queue, struct task *task,
	    int flags);
int	taskqueue_enqueue_timeout(struct taskqueue *queue,
	    struct timeout_task *timeout_task, int ticks);
int	taskqueue_enqueue_timeout_sbt(struct taskqueue *queue,
	    struct timeout_task *timeout_task, sbintime_t sbt, sbintime_t pr,
	    int flags);
int	taskqueue_poll_is_busy(struct taskqueue *queue, struct task *task);
int	taskqueue_cancel(struct taskqueue *queue, struct task *task,
	    u_int *pendp);
int	taskqueue_cancel_timeout(struct taskqueue *queue,
	    struct timeout_task *timeout_task, u_int *pendp);
void	taskqueue_drain(struct taskqueue *queue, struct task *task);
void	taskqueue_drain_timeout(struct taskqueue *queue,
	    struct timeout_task *timeout_task);
void	taskqueue_drain_all(struct taskqueue *queue);
void	taskqueue_quiesce(struct taskqueue *queue);
void	taskqueue_free(struct taskqueue *queue);
void	taskqueue_run(struct taskqueue *queue);
void	taskqueue_block(struct taskqueue *queue);
void	taskqueue_unblock(struct taskqueue *queue);
int	taskqueue_member(struct taskqueue *queue, struct thread *td);
void	taskqueue_set_callback(struct taskqueue *queue,
	    enum taskqueue_callback_type cb_type,
	    taskqueue_callback_fn callback, void *context);
void	taskqueue_thread_enqueue(void *context);
void	taskqueue_thread_loop(void *arg);
void	_timeout_task_init(struct taskqueue *queue,
	    struct timeout_task *timeout_task, int priority, task_fn_t func,
	    void *context);
void	_task_init_flags(struct task *task, int priority, task_fn_t func,
	    void *context, int flags);

#define TASK_INIT_FLAGS(task, priority, func, context, flags) \
	_task_init_flags((task), (priority), (func), (context), (flags))
#define TASK_INIT(task, priority, func, context) \
	TASK_INIT_FLAGS(task, priority, func, context, 0)
#define TIMEOUT_TASK_INIT(queue, timeout_task, priority, func, context) \
	_timeout_task_init((queue), (timeout_task), (priority), (func), (context))
#define TASK_INITIALIZER(priority, func, context) \
	{ .ta_pending = 0, .ta_priority = (priority), .ta_func = (func), .ta_context = (context) }

/* The system queues (src/taskqueue.c). */
extern struct taskqueue *taskqueue_fast;
extern struct taskqueue *taskqueue_swi;
extern struct taskqueue *taskqueue_swi_giant;
extern struct taskqueue *taskqueue_thread;

#define TASKQUEUE_DECLARE(name)	extern struct taskqueue *taskqueue_##name
#define TASKQUEUE_DEFINE(name, enqueue, context, init)			\
	struct taskqueue *taskqueue_##name;				\
	static void							\
	taskqueue_define_##name(void *arg)				\
	{								\
		taskqueue_##name =					\
		    taskqueue_create(#name, M_WAITOK, (enqueue), (context)); \
		init;							\
	}								\
	SYSINIT(taskqueue_##name, SI_SUB_TASKQ, SI_ORDER_SECOND,	\
	    taskqueue_define_##name, NULL)
#define TASKQUEUE_DEFINE_THREAD(name)					\
	TASKQUEUE_DEFINE(name, taskqueue_thread_enqueue, &taskqueue_##name, \
	    taskqueue_start_threads(&taskqueue_##name, 1, PWAIT,	\
	    "%s taskq", #name))
#define TASKQUEUE_FAST_DEFINE(name, enqueue, context, init) \
	TASKQUEUE_DEFINE(name, enqueue, context, init)
#define TASKQUEUE_FAST_DEFINE_THREAD(name) TASKQUEUE_DEFINE_THREAD(name)
#endif /* _BSDKPI_SYS_TASKQUEUE_H_ */
