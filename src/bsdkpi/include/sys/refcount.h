/* SPDX-License-Identifier: MIT */
/* sys/refcount.h - refcount(9) on plain atomics (no saturation checks). */
#ifndef _BSDKPI_SYS_REFCOUNT_H_
#define _BSDKPI_SYS_REFCOUNT_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/kassert.h>
#include <machine/atomic.h>

#define REFCOUNT_SATURATED(val)	(((val) & (1U << 31)) != 0)
#define REFCOUNT_SATURATION_VALUE (3U << 30)
#define REFCOUNT_WAITER		(1U << 31)
#define REFCOUNT_COUNT(x)	((x) & ~REFCOUNT_WAITER)

static __inline void
refcount_init(volatile u_int *count, u_int value)
{
	__atomic_store_n(count, value, __ATOMIC_RELAXED);
}

static __inline u_int
refcount_load(volatile u_int *count)
{
	return (__atomic_load_n(count, __ATOMIC_RELAXED));
}

static __inline u_int
refcount_acquire(volatile u_int *count)
{
	return (__atomic_fetch_add(count, 1, __ATOMIC_SEQ_CST));
}

static __inline u_int
refcount_acquiren(volatile u_int *count, u_int n)
{
	return (__atomic_fetch_add(count, n, __ATOMIC_SEQ_CST));
}

static __inline bool
refcount_acquire_checked(volatile u_int *count)
{
	u_int old = refcount_load(count);

	for (;;) {
		if (__predict_false(old + 1 < old))
			return (false);
		if (__atomic_compare_exchange_n(count, &old, old + 1, 0,
		    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
			return (true);
	}
}

static __inline bool
refcount_acquire_if_gt(volatile u_int *count, u_int n)
{
	u_int old = refcount_load(count);

	for (;;) {
		if (old <= n)
			return (false);
		if (__atomic_compare_exchange_n(count, &old, old + 1, 0,
		    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
			return (true);
	}
}

static __inline bool
refcount_acquire_if_not_zero(volatile u_int *count)
{
	return (refcount_acquire_if_gt(count, 0));
}

static __inline bool
refcount_releasen(volatile u_int *count, u_int n)
{
	u_int old = __atomic_fetch_sub(count, n, __ATOMIC_SEQ_CST);

	KASSERT(old >= n, ("refcount %p is zero", count));
	return (old == n);
}

static __inline bool
refcount_release(volatile u_int *count)
{
	return (refcount_releasen(count, 1));
}

static __inline bool
refcount_release_if_gt(volatile u_int *count, u_int n)
{
	u_int old = refcount_load(count);

	for (;;) {
		if (old <= n)
			return (false);
		if (__atomic_compare_exchange_n(count, &old, old - 1, 0,
		    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
			return (true);
	}
}

static __inline bool
refcount_release_if_not_last(volatile u_int *count)
{
	return (refcount_release_if_gt(count, 1));
}

static __inline bool
refcount_release_if_last(volatile u_int *count)
{
	u_int one = 1;

	return (__atomic_compare_exchange_n(count, &one, 0, 0,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
}

static __inline void
refcount_wait(volatile u_int *count, const char *wmesg, int prio)
{
	(void)wmesg;
	(void)prio;
	while (refcount_load(count) != 0)
		lkpi_os_sleep_ms(1);
}
#endif /* _BSDKPI_SYS_REFCOUNT_H_ */
