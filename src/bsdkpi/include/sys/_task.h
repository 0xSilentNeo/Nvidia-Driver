/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_SYS__TASK_H_
#define _BSDKPI_SYS__TASK_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/callout.h>
#include <linux/workqueue.h>

typedef void task_fn_t(void *context, int pending);
typedef void gtask_fn_t(void *context);

struct taskqueue;

struct task {
	struct work_struct	ta_work;	/* the work item it runs as */
	struct taskqueue	*ta_queue;	/* queue it was enqueued on */
	STAILQ_ENTRY(task)	ta_link;	/* blocked-queue link */
	volatile uint16_t	ta_pending;	/* times queued since last run */
	uint8_t			ta_priority;
	uint8_t			ta_flags;
	task_fn_t		*ta_func;
	void			*ta_context;
};

struct gtask {
	struct task		ta;
};

struct timeout_task {
	struct taskqueue	*q;
	struct task		 t;
	struct callout		 c;
	int			 f;
};
#endif
