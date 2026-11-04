/* SPDX-License-Identifier: MIT */
/*
 * Workqueues.  Each workqueue_struct owns one kernel thread that runs work
 * items in FIFO order; system_wq and friends are shared instances created at
 * module load.  delayed_work arms a timer that queues the work on expiry.
 * Implementation: src/linuxkpi/src/work.c.
 */
#ifndef _LKPI_LINUX_WORKQUEUE_H_
#define _LKPI_LINUX_WORKQUEUE_H_
#include <linux/types.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/lockdep.h>
#include <linux/atomic.h>

struct workqueue_struct;
struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);

struct work_struct {
	struct list_head entry;
	work_func_t func;
	struct workqueue_struct *wq;	/* queue it is pending on, or NULL */
	unsigned long state;		/* WORK_* bits */
};
#define WORK_STRUCT_PENDING_BIT	0
#define WORK_STRUCT_RUNNING_BIT	1
#define WORK_STRUCT_CANCELING_BIT 2
#define WORK_STRUCT_DISABLED_BIT 3

struct delayed_work {
	struct work_struct work;
	struct timer_list timer;
	struct workqueue_struct *wq;
};
struct rcu_work {
	struct work_struct work;
	struct rcu_head rcu;
	struct workqueue_struct *wq;
};

enum {
	WQ_UNBOUND		= 1 << 1,
	WQ_FREEZABLE		= 1 << 2,
	WQ_MEM_RECLAIM		= 1 << 3,
	WQ_HIGHPRI		= 1 << 4,
	WQ_CPU_INTENSIVE	= 1 << 5,
	WQ_SYSFS		= 1 << 6,
	WQ_POWER_EFFICIENT	= 1 << 7,
	WQ_BH			= 1 << 8,
	__WQ_ORDERED		= 1 << 17,
	WQ_MAX_ACTIVE		= 512,
	WQ_UNBOUND_MAX_ACTIVE	= WQ_MAX_ACTIVE,
	WQ_DFL_ACTIVE		= WQ_MAX_ACTIVE / 2,
};

extern struct workqueue_struct *system_wq;
extern struct workqueue_struct *system_highpri_wq;
extern struct workqueue_struct *system_long_wq;
extern struct workqueue_struct *system_unbound_wq;
extern struct workqueue_struct *system_freezable_wq;
extern struct workqueue_struct *system_power_efficient_wq;
extern struct workqueue_struct *system_percpu_wq;
extern struct workqueue_struct *system_dfl_wq;
extern struct workqueue_struct *system_bh_wq;

#define __WORK_INITIALIZER(n, f) { .entry = LIST_HEAD_INIT((n).entry), .func = (f) }
#define DECLARE_WORK(n, f) struct work_struct n = __WORK_INITIALIZER(n, f)
#define __DELAYED_WORK_INITIALIZER(n, f, tflags) { .work = __WORK_INITIALIZER((n).work, (f)) }
#define DECLARE_DELAYED_WORK(n, f) struct delayed_work n = __DELAYED_WORK_INITIALIZER(n, f, 0)

void __lkpi_init_work(struct work_struct *w, work_func_t fn);
void __lkpi_init_delayed_work(struct delayed_work *dw, work_func_t fn);
#define INIT_WORK(w, fn)		__lkpi_init_work(w, fn)
#define INIT_WORK_ONSTACK(w, fn)	__lkpi_init_work(w, fn)
#define INIT_DELAYED_WORK(dw, fn)	__lkpi_init_delayed_work(dw, fn)
#define INIT_DELAYED_WORK_ONSTACK(dw, fn) __lkpi_init_delayed_work(dw, fn)
#define INIT_DEFERRABLE_WORK(dw, fn)	__lkpi_init_delayed_work(dw, fn)
#define INIT_RCU_WORK(rw, fn)		__lkpi_init_work(&(rw)->work, fn)
#define destroy_work_on_stack(w)	do { } while (0)
#define destroy_delayed_work_on_stack(dw) timer_destroy(&(dw)->timer)

#define to_delayed_work(w) container_of(w, struct delayed_work, work)
#define to_rcu_work(w) container_of(w, struct rcu_work, work)

struct workqueue_struct *alloc_workqueue(const char *fmt, unsigned int flags, int max_active, ...)
    __printf(1, 4);
#define alloc_ordered_workqueue(fmt, flags, args...) \
	alloc_workqueue(fmt, __WQ_ORDERED | (flags), 1, ##args)
#define create_workqueue(name)		alloc_workqueue("%s", WQ_MEM_RECLAIM, 1, (name))
#define create_singlethread_workqueue(name) alloc_ordered_workqueue("%s", WQ_MEM_RECLAIM, name)
#define create_freezable_workqueue(name) alloc_workqueue("%s", WQ_FREEZABLE | WQ_UNBOUND | WQ_MEM_RECLAIM, 1, (name))
void destroy_workqueue(struct workqueue_struct *wq);
void flush_workqueue(struct workqueue_struct *wq);
void drain_workqueue(struct workqueue_struct *wq);

bool queue_work(struct workqueue_struct *wq, struct work_struct *work);
#define queue_work_on(cpu, wq, work) queue_work(wq, work)
bool queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dw, unsigned long delay);
#define queue_delayed_work_on(cpu, wq, dw, delay) queue_delayed_work(wq, dw, delay)
bool mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *dw, unsigned long delay);
#define mod_delayed_work_on(cpu, wq, dw, delay) mod_delayed_work(wq, dw, delay)
bool queue_rcu_work(struct workqueue_struct *wq, struct rcu_work *rw);

static inline bool schedule_work(struct work_struct *work) { return queue_work(system_wq, work); }
#define schedule_work_on(cpu, work) schedule_work(work)
static inline bool schedule_delayed_work(struct delayed_work *dw, unsigned long delay)
{
	return queue_delayed_work(system_wq, dw, delay);
}
#define schedule_delayed_work_on(cpu, dw, delay) schedule_delayed_work(dw, delay)
void flush_scheduled_work(void);

bool flush_work(struct work_struct *work);
bool flush_delayed_work(struct delayed_work *dw);
bool flush_rcu_work(struct rcu_work *rw);
bool cancel_work(struct work_struct *work);
bool cancel_work_sync(struct work_struct *work);
bool cancel_delayed_work(struct delayed_work *dw);
bool cancel_delayed_work_sync(struct delayed_work *dw);
bool disable_work(struct work_struct *work);
bool disable_work_sync(struct work_struct *work);
bool enable_work(struct work_struct *work);
bool disable_delayed_work(struct delayed_work *dw);
bool disable_delayed_work_sync(struct delayed_work *dw);
bool enable_delayed_work(struct delayed_work *dw);
bool work_pending(struct work_struct *work);
bool work_busy(struct work_struct *work);
#define delayed_work_pending(dw) work_pending(&(dw)->work)
struct work_struct *current_work(void);
#endif /* _LKPI_LINUX_WORKQUEUE_H_ */
