/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_PREEMPT_H_
#define _LKPI_LINUX_PREEMPT_H_
#include <lkpi/os.h>
#define preempt_disable()	do { } while (0)
#define preempt_enable()	do { } while (0)
#define preempt_enable_no_resched() do { } while (0)
#define in_interrupt()		(!lkpi_os_may_sleep())
#define in_irq()		(!lkpi_os_may_sleep())
#define in_softirq()		(0)
#define in_atomic()		(!lkpi_os_may_sleep())
#define in_task()		(lkpi_os_may_sleep())
#define irqs_disabled()		(!lkpi_os_may_sleep())
#define in_hardirq()		(!lkpi_os_may_sleep())
#define in_serving_softirq()	(0)
#endif
