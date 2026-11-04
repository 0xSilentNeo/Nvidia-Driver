/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_COMPLETION_H_
#define _LKPI_LINUX_COMPLETION_H_
#include <linux/wait.h>

struct completion {
	unsigned int done;
	wait_queue_head_t wait;
};
#define COMPLETION_INITIALIZER(work) { 0, __WAIT_QUEUE_HEAD_INITIALIZER((work).wait) }
#define DECLARE_COMPLETION(work) struct completion work = COMPLETION_INITIALIZER(work)
#define DECLARE_COMPLETION_ONSTACK(work) DECLARE_COMPLETION(work)

static inline void init_completion(struct completion *x)
{
	x->done = 0;
	init_waitqueue_head(&x->wait);
}
static inline void reinit_completion(struct completion *x) { x->done = 0; }
static inline void complete(struct completion *x)
{
	lkpi_os_mutex_t m = __lkpi_mutex_get(&x->wait.m);
	lkpi_os_mutex_lock(m);
	if (x->done != UINT_MAX)
		x->done++;
	lkpi_os_mutex_unlock(m);
	__wake_up(&x->wait, true);
}
static inline void complete_all(struct completion *x)
{
	lkpi_os_mutex_t m = __lkpi_mutex_get(&x->wait.m);
	lkpi_os_mutex_lock(m);
	x->done = UINT_MAX;
	lkpi_os_mutex_unlock(m);
	__wake_up(&x->wait, false);
}
static inline bool completion_done(struct completion *x) { return READ_ONCE(x->done) != 0; }
static inline bool try_wait_for_completion(struct completion *x)
{
	lkpi_os_mutex_t m = __lkpi_mutex_get(&x->wait.m);
	bool ret = true;
	lkpi_os_mutex_lock(m);
	if (!x->done)
		ret = false;
	else if (x->done != UINT_MAX)
		x->done--;
	lkpi_os_mutex_unlock(m);
	return ret;
}
static inline unsigned long wait_for_completion_timeout(struct completion *x, unsigned long timeout)
{
	unsigned long ret = wait_event_timeout(x->wait, completion_done(x), timeout);
	if (ret)
		(void)try_wait_for_completion(x);
	return ret;
}
static inline void wait_for_completion(struct completion *x)
{
	wait_event(x->wait, completion_done(x));
	(void)try_wait_for_completion(x);
}
static inline int wait_for_completion_interruptible(struct completion *x) { wait_for_completion(x); return 0; }
static inline int wait_for_completion_killable(struct completion *x) { wait_for_completion(x); return 0; }
static inline long wait_for_completion_interruptible_timeout(struct completion *x, unsigned long t)
{
	return (long)wait_for_completion_timeout(x, t);
}
static inline long wait_for_completion_killable_timeout(struct completion *x, unsigned long t)
{
	return (long)wait_for_completion_timeout(x, t);
}
#endif
