// SPDX-License-Identifier: MIT
/*
 * Lazily allocated spinlocks and mutexes.
 *
 * The Linux lock types have no mandatory destructor and are embedded all
 * over the driver, often zero-initialised with __SPIN_LOCK_UNLOCKED or a
 * kzalloc().  The backend lock objects are opaque to C, so they are
 * allocated on first use: the inline fast path in linux/spinlock.h and
 * linux/mutex.h reads the pointer with acquire semantics and falls into the
 * slow path below when it is still NULL.  Two threads racing on the first
 * use both allocate; a compare-exchange decides which object wins and the
 * loser is freed again, so every user ends up with the same lock.
 *
 * Objects that are explicitly destroyed (spin_lock_destroy, mutex_destroy)
 * are returned to the backend.  Locks whose embedding structure is freed
 * without a destroy call leak one small backend object; that is accepted,
 * see the comment in linux/spinlock_types.h.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <lkpi/os.h>
#include <lkpi/runtime.h>

lkpi_os_spinlock_t __lkpi_spinlock_get_slow(spinlock_t *lock)
{
	lkpi_os_spinlock_t fresh, expected = NULL;

	fresh = lkpi_os_spinlock_alloc();
	if (!fresh)
		lkpi_os_panic("lkpi: cannot allocate spinlock backing object");

	if (__atomic_compare_exchange_n(&lock->lock, &expected, fresh, false,
				       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return fresh;

	/* Somebody else got there first; use theirs. */
	lkpi_os_spinlock_free(fresh);
	return expected;
}

void spin_lock_destroy(spinlock_t *lock)
{
	lkpi_os_spinlock_t l;

	l = __atomic_exchange_n(&lock->lock, NULL, __ATOMIC_ACQ_REL);
	if (l)
		lkpi_os_spinlock_free(l);
}

lkpi_os_mutex_t __lkpi_mutex_get_slow(struct mutex *m)
{
	lkpi_os_mutex_t fresh, expected = NULL;

	fresh = lkpi_os_mutex_alloc();
	if (!fresh)
		lkpi_os_panic("lkpi: cannot allocate mutex backing object");

	if (__atomic_compare_exchange_n(&m->lock, &expected, fresh, false,
				       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return fresh;

	lkpi_os_mutex_free(fresh);
	return expected;
}

void mutex_destroy(struct mutex *m)
{
	lkpi_os_mutex_t l;

	l = __atomic_exchange_n(&m->lock, NULL, __ATOMIC_ACQ_REL);
	if (l)
		lkpi_os_mutex_free(l);
}

int lkpi_lock_init(void)
{
	return 0;
}

void lkpi_lock_fini(void)
{
}
