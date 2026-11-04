/* SPDX-License-Identifier: MIT */
/*
 * timer_list on top of one-shot backend timers (IOTimerEventSource).  The
 * callback runs on the kext's timer workloop thread, so it may take
 * spinlocks and queue work but must not sleep -- the same contract as a
 * Linux timer softirq.
 */
#ifndef _LKPI_LINUX_TIMER_H_
#define _LKPI_LINUX_TIMER_H_
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/container_of.h>
#include <lkpi/os.h>

struct timer_list {
	void (*function)(struct timer_list *);
	unsigned long expires;
	unsigned int flags;
	lkpi_os_timer_t os;		/* lazily created */
	int pending;
	u32 magic;			/* LKPI_TIMER_MAGIC once initialised; lets
					 * timer_setup() on uninitialised memory
					 * (on-stack timers) start clean */
};
#define LKPI_TIMER_MAGIC 0x74696d72u	/* "timr" */
#define TIMER_DEFERRABLE	0x00080000
#define TIMER_IRQSAFE		0x00200000
#define TIMER_PINNED		0x00100000

void __lkpi_timer_setup(struct timer_list *t, void (*fn)(struct timer_list *), unsigned int flags);
int mod_timer(struct timer_list *t, unsigned long expires);
int mod_timer_pending(struct timer_list *t, unsigned long expires);
int timer_delete(struct timer_list *t);
int timer_delete_sync(struct timer_list *t);
int timer_shutdown_sync(struct timer_list *t);
int timer_pending(const struct timer_list *t);
void timer_destroy(struct timer_list *t);	/* frees the backend object */

#define timer_setup(t, fn, flags)	__lkpi_timer_setup(t, fn, flags)
#define timer_setup_on_stack(t, fn, flags) __lkpi_timer_setup(t, fn, flags)
#define destroy_timer_on_stack(t)	timer_destroy(t)
#define add_timer(t)			mod_timer(t, (t)->expires)
#define del_timer(t)			timer_delete(t)
#define del_timer_sync(t)		timer_delete_sync(t)
#define timer_shutdown(t)		timer_delete(t)
#define from_timer(var, callback_timer, timer_fieldname) \
	container_of(callback_timer, typeof(*var), timer_fieldname)
#define timer_container_of(var, callback_timer, timer_fieldname) \
	container_of(callback_timer, typeof(*var), timer_fieldname)
#define DEFINE_TIMER(name, fn) struct timer_list name = { .function = (fn) }
#define init_timer(t) do { } while (0)
#define setup_timer(t, fn, data) timer_setup(t, fn, 0)
#define timer_reduce(t, exp) mod_timer(t, exp)
#endif
