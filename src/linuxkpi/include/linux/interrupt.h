/* SPDX-License-Identifier: MIT */
/*
 * IRQ registration and tasklets.
 *
 * mt76 requests exactly one IRQ (devm_request_irq in mt7925/pci.c) whose
 * handler acks the hardware and schedules a tasklet.  Here the handler runs
 * on the interrupt workloop and the tasklet runs on a dedicated
 * high-priority thread (src/linuxkpi/src/tasklet.c), mirroring the
 * hardirq/softirq split closely enough for the driver's locking to hold.
 */
#ifndef _LKPI_LINUX_INTERRUPT_H_
#define _LKPI_LINUX_INTERRUPT_H_
#include <linux/types.h>
#include <linux/irqreturn.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/preempt.h>

struct device;
typedef irqreturn_t (*irq_handler_t)(int irq, void *dev_id);

#define IRQF_SHARED		0x00000080
#define IRQF_TRIGGER_NONE	0x00000000
#define IRQF_ONESHOT		0x00002000
#define IRQF_NO_SUSPEND		0x00004000
#define IRQF_NO_THREAD		0x00010000
#define IRQF_NOBALANCING	0x00000800
#define IRQF_TIMER		0x00000200

int request_threaded_irq(unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn,
    unsigned long flags, const char *name, void *dev_id);
static inline int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
    const char *name, void *dev_id)
{
	return request_threaded_irq(irq, handler, NULL, flags, name, dev_id);
}
void free_irq(unsigned int irq, void *dev_id);
int devm_request_threaded_irq(struct device *dev, unsigned int irq, irq_handler_t handler,
    irq_handler_t thread_fn, unsigned long flags, const char *name, void *dev_id);
static inline int devm_request_irq(struct device *dev, unsigned int irq, irq_handler_t handler,
    unsigned long flags, const char *name, void *dev_id)
{
	return devm_request_threaded_irq(dev, irq, handler, NULL, flags, name, dev_id);
}
void devm_free_irq(struct device *dev, unsigned int irq, void *dev_id);
void disable_irq(unsigned int irq);
void disable_irq_nosync(unsigned int irq);
void enable_irq(unsigned int irq);
void synchronize_irq(unsigned int irq);
static inline int irq_set_affinity_hint(unsigned int irq, const void *m) { (void)irq; (void)m; return 0; }

/* ---- tasklets ---- */
struct tasklet_struct {
	struct list_head entry;
	unsigned long state;
	atomic_t count;
	bool use_callback;
	union {
		void (*func)(unsigned long data);
		void (*callback)(struct tasklet_struct *t);
	};
	unsigned long data;
};
enum {
	TASKLET_STATE_SCHED,
	TASKLET_STATE_RUN,
};
#define DECLARE_TASKLET(name, _callback) \
	struct tasklet_struct name = { .count = ATOMIC_INIT(0), .callback = _callback, .use_callback = true }
#define DECLARE_TASKLET_DISABLED(name, _callback) \
	struct tasklet_struct name = { .count = ATOMIC_INIT(1), .callback = _callback, .use_callback = true }
#define DECLARE_TASKLET_OLD(name, _func) \
	struct tasklet_struct name = { .count = ATOMIC_INIT(0), .func = _func }
#define from_tasklet(var, callback_tasklet, tasklet_fieldname) \
	container_of(callback_tasklet, typeof(*var), tasklet_fieldname)

void tasklet_init(struct tasklet_struct *t, void (*func)(unsigned long), unsigned long data);
void tasklet_setup(struct tasklet_struct *t, void (*callback)(struct tasklet_struct *));
void tasklet_schedule(struct tasklet_struct *t);
#define tasklet_hi_schedule(t) tasklet_schedule(t)
void tasklet_kill(struct tasklet_struct *t);
void tasklet_disable(struct tasklet_struct *t);
#define tasklet_disable_nosync(t) tasklet_disable(t)
#define tasklet_disable_in_atomic(t) tasklet_disable(t)
void tasklet_enable(struct tasklet_struct *t);
static inline int tasklet_trylock(struct tasklet_struct *t) { return !test_and_set_bit(TASKLET_STATE_RUN, &t->state); }
static inline void tasklet_unlock(struct tasklet_struct *t) { clear_bit(TASKLET_STATE_RUN, &t->state); }
void tasklet_unlock_wait(struct tasklet_struct *t);

/* softirq helpers used by a few drivers; here they are no-ops */
#define local_softirq_pending() (0)
#define __raise_softirq_irqoff(nr) do { } while (0)
#define raise_softirq(nr) do { } while (0)
#endif /* _LKPI_LINUX_INTERRUPT_H_ */
