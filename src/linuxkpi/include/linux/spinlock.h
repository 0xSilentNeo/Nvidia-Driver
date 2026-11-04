/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_SPINLOCK_H_
#define _LKPI_LINUX_SPINLOCK_H_
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/lockdep.h>
#include <linux/spinlock_types.h>
#include <linux/preempt.h>
#include <linux/bottom_half.h>
#include <linux/atomic.h>

lkpi_os_spinlock_t __lkpi_spinlock_get_slow(spinlock_t *lock);
static inline lkpi_os_spinlock_t __lkpi_spinlock_get(spinlock_t *lock)
{
	lkpi_os_spinlock_t l = __atomic_load_n(&lock->lock, __ATOMIC_ACQUIRE);
	return likely(l) ? l : __lkpi_spinlock_get_slow(lock);
}
static inline void spin_lock_init(spinlock_t *lock)
{
	lock->lock = NULL;
	(void)__lkpi_spinlock_get(lock);
}
void spin_lock_destroy(spinlock_t *lock);
#define raw_spin_lock_init(l) spin_lock_init(l)

/*
 * Interrupt-context handlers in this port run on the IOKit interrupt
 * workloop thread, never in a primary interrupt context, so the _bh and
 * plain variants are the same lock operation.  The _irqsave variants do
 * disable interrupts so that the IOFilterInterruptEventSource filter (which
 * only reads/acks the IRQ status) stays safe.
 */
static inline void spin_lock(spinlock_t *lock) { lkpi_os_spin_lock(__lkpi_spinlock_get(lock)); }
static inline void spin_unlock(spinlock_t *lock) { lkpi_os_spin_unlock(__lkpi_spinlock_get(lock)); }
static inline int spin_trylock(spinlock_t *lock) { return lkpi_os_spin_trylock(__lkpi_spinlock_get(lock)); }
static inline void spin_lock_bh(spinlock_t *lock) { local_bh_disable(); spin_lock(lock); }
static inline void spin_unlock_bh(spinlock_t *lock) { spin_unlock(lock); local_bh_enable(); }
static inline int spin_trylock_bh(spinlock_t *lock) { local_bh_disable(); if (spin_trylock(lock)) return 1; local_bh_enable(); return 0; }
static inline void spin_lock_irq(spinlock_t *lock) { (void)lkpi_os_spin_lock_irq(__lkpi_spinlock_get(lock)); }
static inline void spin_unlock_irq(spinlock_t *lock) { lkpi_os_spin_unlock_irq(__lkpi_spinlock_get(lock), 1); }
#define spin_lock_irqsave(lock, flags) \
	do { (flags) = (unsigned long)lkpi_os_spin_lock_irq(__lkpi_spinlock_get(lock)); } while (0)
#define spin_unlock_irqrestore(lock, flags) \
	do { lkpi_os_spin_unlock_irq(__lkpi_spinlock_get(lock), (lkpi_os_irqstate_t)(flags)); } while (0)
#define spin_lock_nested(lock, subclass) spin_lock(lock)
#define spin_lock_bh_nested(lock, subclass) spin_lock_bh(lock)
#define spin_lock_irqsave_nested(lock, flags, subclass) spin_lock_irqsave(lock, flags)
#define spin_is_locked(lock) (0)
#define assert_spin_locked(lock) do { } while (0)
#define spin_lock_assert(lock) do { } while (0)

#define raw_spin_lock		spin_lock
#define raw_spin_unlock		spin_unlock
#define raw_spin_lock_bh	spin_lock_bh
#define raw_spin_unlock_bh	spin_unlock_bh
#define raw_spin_lock_irq	spin_lock_irq
#define raw_spin_unlock_irq	spin_unlock_irq
#define raw_spin_lock_irqsave	spin_lock_irqsave
#define raw_spin_unlock_irqrestore spin_unlock_irqrestore
#define raw_spin_trylock	spin_trylock

static inline int atomic_dec_and_lock(atomic_t *a, spinlock_t *lock)
{
	if (atomic_add_unless(a, -1, 1))
		return 0;
	spin_lock(lock);
	if (atomic_dec_and_test(a))
		return 1;
	spin_unlock(lock);
	return 0;
}

/* rwlocks: mt76 uses read_lock_bh/write_lock_bh on a few paths; map to a
 * spinlock.  Readers do not run concurrently, which is correct if slower. */
typedef struct {
	spinlock_t lock;
} rwlock_t;
#define DEFINE_RWLOCK(x) rwlock_t x = { .lock = __SPIN_LOCK_UNLOCKED(x) }
#define __RW_LOCK_UNLOCKED(x) { .lock = __SPIN_LOCK_UNLOCKED(x) }
static inline void rwlock_init(rwlock_t *rw) { spin_lock_init(&rw->lock); }
static inline void read_lock(rwlock_t *rw) { spin_lock(&rw->lock); }
static inline void read_unlock(rwlock_t *rw) { spin_unlock(&rw->lock); }
static inline void write_lock(rwlock_t *rw) { spin_lock(&rw->lock); }
static inline void write_unlock(rwlock_t *rw) { spin_unlock(&rw->lock); }
static inline void read_lock_bh(rwlock_t *rw) { spin_lock_bh(&rw->lock); }
static inline void read_unlock_bh(rwlock_t *rw) { spin_unlock_bh(&rw->lock); }
static inline void write_lock_bh(rwlock_t *rw) { spin_lock_bh(&rw->lock); }
static inline void write_unlock_bh(rwlock_t *rw) { spin_unlock_bh(&rw->lock); }
static inline void read_lock_irq(rwlock_t *rw) { spin_lock_irq(&rw->lock); }
static inline void read_unlock_irq(rwlock_t *rw) { spin_unlock_irq(&rw->lock); }
static inline void write_lock_irq(rwlock_t *rw) { spin_lock_irq(&rw->lock); }
static inline void write_unlock_irq(rwlock_t *rw) { spin_unlock_irq(&rw->lock); }
#define read_lock_irqsave(l, f) spin_lock_irqsave(&(l)->lock, f)
#define read_unlock_irqrestore(l, f) spin_unlock_irqrestore(&(l)->lock, f)
#define write_lock_irqsave(l, f) spin_lock_irqsave(&(l)->lock, f)
#define write_unlock_irqrestore(l, f) spin_unlock_irqrestore(&(l)->lock, f)
#endif /* _LKPI_LINUX_SPINLOCK_H_ */
