/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_REFCOUNT_H_
#define _LKPI_LINUX_REFCOUNT_H_
#include <linux/atomic.h>
#include <linux/bug.h>
typedef struct refcount_struct {
	atomic_t refs;
} refcount_t;
#define REFCOUNT_INIT(n) { .refs = ATOMIC_INIT(n) }
static inline void refcount_set(refcount_t *r, int n) { atomic_set(&r->refs, n); }
static inline unsigned int refcount_read(const refcount_t *r) { return atomic_read(&r->refs); }
static inline void refcount_inc(refcount_t *r) { WARN_ON(atomic_inc_return(&r->refs) <= 1); }
static inline bool refcount_inc_not_zero(refcount_t *r) { return atomic_inc_not_zero(&r->refs); }
static inline void refcount_add(int i, refcount_t *r) { atomic_add(i, &r->refs); }
static inline bool refcount_add_not_zero(int i, refcount_t *r) { return atomic_add_unless(&r->refs, i, 0); }
static inline bool refcount_dec_and_test(refcount_t *r)
{
	int v = atomic_dec_return(&r->refs);
	WARN_ON(v < 0);
	return v == 0;
}
static inline bool refcount_sub_and_test(int i, refcount_t *r) { return atomic_sub_return(i, &r->refs) == 0; }
static inline void refcount_dec(refcount_t *r) { WARN_ON(atomic_dec_return(&r->refs) <= 0); }
static inline bool refcount_dec_not_one(refcount_t *r)
{
	int c = atomic_read(&r->refs);
	do {
		if (c == 1)
			return false;
	} while (!atomic_try_cmpxchg(&r->refs, &c, c - 1));
	return true;
}
static inline bool refcount_dec_if_one(refcount_t *r) { int one = 1; return atomic_try_cmpxchg(&r->refs, &one, 0); }
#endif
