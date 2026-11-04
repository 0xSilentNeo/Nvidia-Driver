/* SPDX-License-Identifier: MIT */
/*
 * kthreads.  mt76's TX worker (util.h) uses kthread_run + the
 * set_current_state/schedule idiom, plus park/unpark during resets.
 * Implementation: src/linuxkpi/src/kthread.c.
 */
#ifndef _LKPI_LINUX_KTHREAD_H_
#define _LKPI_LINUX_KTHREAD_H_
#include <linux/sched.h>

struct task_struct *kthread_create(int (*threadfn)(void *data), void *data, const char *namefmt, ...)
    __printf(3, 4);
#define kthread_create_on_node(fn, data, node, namefmt, arg...) kthread_create(fn, data, namefmt, ##arg)
int wake_up_process(struct task_struct *p);
#define kthread_run(threadfn, data, namefmt, ...) ({ \
	struct task_struct *__k = kthread_create(threadfn, data, namefmt, ##__VA_ARGS__); \
	if (!IS_ERR(__k)) wake_up_process(__k); \
	__k; })
int kthread_stop(struct task_struct *k);
bool kthread_should_stop(void);
bool kthread_should_park(void);
int kthread_park(struct task_struct *k);
void kthread_unpark(struct task_struct *k);
void kthread_parkme(void);
void *kthread_data(struct task_struct *k);
#define kthread_bind(k, cpu) do { } while (0)
#define kthread_freezable_should_stop(was_frozen) kthread_should_stop()
#endif
