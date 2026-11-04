/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_BOTTOM_HALF_H_
#define _LKPI_LINUX_BOTTOM_HALF_H_
/* Softirqs do not exist here; "bottom halves" run on ordinary threads. */
#define local_bh_disable()	do { } while (0)
#define local_bh_enable()	do { } while (0)
#define local_irq_save(f)	do { (f) = 0; } while (0)
#define local_irq_restore(f)	do { (void)(f); } while (0)
#define local_irq_disable()	do { } while (0)
#define local_irq_enable()	do { } while (0)
#endif
