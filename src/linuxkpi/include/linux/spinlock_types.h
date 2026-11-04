/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_SPINLOCK_TYPES_H_
#define _LKPI_LINUX_SPINLOCK_TYPES_H_
#include <lkpi/os.h>
/*
 * Spinlocks are allocated lazily on first use because the Linux API has no
 * mandatory destructor and the driver embeds them in many structs.  A lock
 * that was never initialised (all-zero) is still usable.  Backend lock
 * objects are reclaimed by spin_lock_destroy() where the driver would have
 * freed the enclosing object; leaking a few small IOSimpleLocks across the
 * life of the kext is accepted rather than tracking every embedding.
 */
typedef struct spinlock {
	lkpi_os_spinlock_t lock;
} spinlock_t;
typedef spinlock_t raw_spinlock_t;
#define __SPIN_LOCK_UNLOCKED(x) { .lock = NULL }
#define __RAW_SPIN_LOCK_UNLOCKED(x) { .lock = NULL }
#define DEFINE_SPINLOCK(x) spinlock_t x = __SPIN_LOCK_UNLOCKED(x)
#define DEFINE_RAW_SPINLOCK(x) raw_spinlock_t x = __RAW_SPIN_LOCK_UNLOCKED(x)
#endif
