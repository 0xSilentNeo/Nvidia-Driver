/* SPDX-License-Identifier: MIT */
/*
 * Wait queues on top of an IOLock + IOLockSleep/IOLockWakeup pair.  The
 * condition is re-evaluated by the caller's loop, exactly like upstream.
 */
#ifndef _LKPI_LINUX_WAIT_H_
#define _LKPI_LINUX_WAIT_H_
#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <lkpi/os.h>

typedef struct wait_queue_head {
	struct mutex m;		/* lazily created IOLock used for the sleep */
	int waiters;
} wait_queue_head_t;
struct wait_queue_entry {
	unsigned int flags;
	void *private;
	struct list_head entry;
};
typedef struct wait_queue_entry wait_queue_entry_t;

#define __WAIT_QUEUE_HEAD_INITIALIZER(name) { .m = __MUTEX_INITIALIZER(name), .waiters = 0 }
#define DECLARE_WAIT_QUEUE_HEAD(name) wait_queue_head_t name = __WAIT_QUEUE_HEAD_INITIALIZER(name)
#define DECLARE_WAIT_QUEUE_HEAD_ONSTACK(name) DECLARE_WAIT_QUEUE_HEAD(name)
#define DEFINE_WAIT(name) wait_queue_entry_t name = { 0, NULL, LIST_HEAD_INIT((name).entry) }
#define DECLARE_WAITQUEUE(name, tsk) DEFINE_WAIT(name)

static inline void init_waitqueue_head(wait_queue_head_t *q)
{
	mutex_init(&q->m);
	q->waiters = 0;
}
#define __init_waitqueue_head(q, name, key) init_waitqueue_head(q)
static inline void init_waitqueue_entry(wait_queue_entry_t *e, void *p) { e->private = p; INIT_LIST_HEAD(&e->entry); }
static inline int waitqueue_active(wait_queue_head_t *q) { return READ_ONCE(q->waiters) != 0; }
static inline bool wq_has_sleeper(wait_queue_head_t *q) { smp_mb(); return waitqueue_active(q); }

static inline void __wake_up(wait_queue_head_t *q, bool one)
{
	if (!waitqueue_active(q))
		return;
	lkpi_os_wakeup(__lkpi_mutex_get(&q->m), q, one);
}
#define wake_up(q)			__wake_up(q, false)
#define wake_up_nr(q, nr)		__wake_up(q, false)
#define wake_up_all(q)			__wake_up(q, false)
#define wake_up_interruptible(q)	__wake_up(q, false)
#define wake_up_interruptible_nr(q, nr)	__wake_up(q, false)
#define wake_up_interruptible_all(q)	__wake_up(q, false)
#define wake_up_interruptible_sync(q)	__wake_up(q, false)
#define wake_up_poll(q, m)		__wake_up(q, false)
#define wake_up_locked(q)		__wake_up(q, false)

/*
 * Sleep until @timeout_ns (0 = forever) or a wake_up().  Returns false on
 * timeout.  The caller re-checks its condition.
 */
static inline bool __lkpi_wait_once(wait_queue_head_t *q, u64 timeout_ns)
{
	lkpi_os_mutex_t m = __lkpi_mutex_get(&q->m);
	bool woke;
	lkpi_os_mutex_lock(m);
	__atomic_fetch_add(&q->waiters, 1, __ATOMIC_SEQ_CST);
	woke = lkpi_os_mutex_sleep(m, q, timeout_ns);
	__atomic_fetch_sub(&q->waiters, 1, __ATOMIC_SEQ_CST);
	lkpi_os_mutex_unlock(m);
	return woke;
}

#define ___wait_cond_timeout(condition, ret) ({ \
	bool __cond = (condition); \
	if (__cond && !(ret)) (ret) = 1; \
	__cond || !(ret); })

#define wait_event(wq, condition) do { \
	while (!(condition)) \
		__lkpi_wait_once(&(wq), 0); \
} while (0)
#define wait_event_interruptible(wq, condition) ({ wait_event(wq, condition); 0; })
#define wait_event_killable(wq, condition) ({ wait_event(wq, condition); 0; })
#define wait_event_freezable(wq, condition) ({ wait_event(wq, condition); 0; })

/* Returns remaining jiffies (>=1) if condition became true, 0 on timeout. */
#define wait_event_timeout(wq, condition, timeout) ({ \
	unsigned long __ret = (timeout); \
	unsigned long __deadline = jiffies + __ret; \
	for (;;) { \
		long __left; \
		if (condition) { \
			__left = (long)(__deadline - jiffies); \
			__ret = __left > 0 ? (unsigned long)__left : 1; \
			break; \
		} \
		__left = (long)(__deadline - jiffies); \
		if (__left <= 0) { __ret = (condition) ? 1 : 0; break; } \
		__lkpi_wait_once(&(wq), jiffies_to_nsecs((unsigned long)__left)); \
	} \
	__ret; })
#define wait_event_interruptible_timeout(wq, condition, timeout) \
	((long)wait_event_timeout(wq, condition, timeout))
#define wait_event_killable_timeout(wq, condition, timeout) \
	((long)wait_event_timeout(wq, condition, timeout))
#define wait_event_freezable_timeout(wq, condition, timeout) \
	((long)wait_event_timeout(wq, condition, timeout))

/* Explicit prepare/finish style used by mt76_worker and kthread loops. */
#define prepare_to_wait(q, w, state) do { (void)(w); } while (0)
#define prepare_to_wait_exclusive(q, w, state) do { (void)(w); } while (0)
#define finish_wait(q, w) do { (void)(q); (void)(w); } while (0)
#define add_wait_queue(q, w) do { } while (0)
#define remove_wait_queue(q, w) do { } while (0)
#define wait_woken(w, mode, timeout) ((timeout))
#define woken_wake_function NULL
#define autoremove_wake_function NULL
#define default_wake_function NULL
#define TASK_RUNNING		0x0000
#define TASK_INTERRUPTIBLE	0x0001
#define TASK_UNINTERRUPTIBLE	0x0002
#define TASK_KILLABLE		(TASK_UNINTERRUPTIBLE)
#define TASK_NORMAL		(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)
#define TASK_IDLE		(TASK_UNINTERRUPTIBLE)
#define WQ_FLAG_EXCLUSIVE	0x01
#endif /* _LKPI_LINUX_WAIT_H_ */
