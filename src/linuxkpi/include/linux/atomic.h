/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_ATOMIC_H_
#define _LKPI_LINUX_ATOMIC_H_
#include <linux/types.h>
#include <linux/compiler.h>

#define ATOMIC_INIT(i) { (i) }
#define ATOMIC64_INIT(i) { (i) }

static inline int atomic_read(const atomic_t *v) { return __atomic_load_n(&v->counter, __ATOMIC_RELAXED); }
static inline void atomic_set(atomic_t *v, int i) { __atomic_store_n(&v->counter, i, __ATOMIC_RELAXED); }
#define atomic_read_acquire(v) __atomic_load_n(&(v)->counter, __ATOMIC_ACQUIRE)
#define atomic_set_release(v, i) __atomic_store_n(&(v)->counter, i, __ATOMIC_RELEASE)
static inline void atomic_add(int i, atomic_t *v) { __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline void atomic_sub(int i, atomic_t *v) { __atomic_fetch_sub(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline void atomic_inc(atomic_t *v) { atomic_add(1, v); }
static inline void atomic_dec(atomic_t *v) { atomic_sub(1, v); }
static inline int atomic_add_return(int i, atomic_t *v) { return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline int atomic_sub_return(int i, atomic_t *v) { return __atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline int atomic_fetch_add(int i, atomic_t *v) { return __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline int atomic_fetch_sub(int i, atomic_t *v) { return __atomic_fetch_sub(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline int atomic_fetch_or(int i, atomic_t *v) { return __atomic_fetch_or(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline int atomic_fetch_and(int i, atomic_t *v) { return __atomic_fetch_and(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline int atomic_fetch_xor(int i, atomic_t *v) { return __atomic_fetch_xor(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline void atomic_or(int i, atomic_t *v) { (void)atomic_fetch_or(i, v); }
static inline void atomic_and(int i, atomic_t *v) { (void)atomic_fetch_and(i, v); }
static inline void atomic_xor(int i, atomic_t *v) { (void)atomic_fetch_xor(i, v); }
static inline void atomic_andnot(int i, atomic_t *v) { (void)atomic_fetch_and(~i, v); }
static inline int atomic_inc_return(atomic_t *v) { return atomic_add_return(1, v); }
static inline int atomic_dec_return(atomic_t *v) { return atomic_sub_return(1, v); }
static inline int atomic_fetch_inc(atomic_t *v) { return atomic_fetch_add(1, v); }
static inline int atomic_fetch_dec(atomic_t *v) { return atomic_fetch_sub(1, v); }
static inline bool atomic_inc_and_test(atomic_t *v) { return atomic_inc_return(v) == 0; }
static inline bool atomic_dec_and_test(atomic_t *v) { return atomic_dec_return(v) == 0; }
static inline bool atomic_sub_and_test(int i, atomic_t *v) { return atomic_sub_return(i, v) == 0; }
static inline bool atomic_add_negative(int i, atomic_t *v) { return atomic_add_return(i, v) < 0; }
static inline int atomic_xchg(atomic_t *v, int new) { return __atomic_exchange_n(&v->counter, new, __ATOMIC_SEQ_CST); }
static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	__atomic_compare_exchange_n(&v->counter, &old, new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return old;
}
static inline bool atomic_try_cmpxchg(atomic_t *v, int *old, int new)
{
	return __atomic_compare_exchange_n(&v->counter, old, new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static inline int atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
	int c = atomic_read(v);
	do {
		if (unlikely(c == u))
			break;
	} while (!atomic_try_cmpxchg(v, &c, c + a));
	return c;
}
static inline bool atomic_add_unless(atomic_t *v, int a, int u) { return atomic_fetch_add_unless(v, a, u) != u; }
static inline bool atomic_inc_not_zero(atomic_t *v) { return atomic_add_unless(v, 1, 0); }
static inline bool atomic_inc_unless_negative(atomic_t *v)
{
	int c = atomic_read(v);
	do {
		if (unlikely(c < 0))
			return false;
	} while (!atomic_try_cmpxchg(v, &c, c + 1));
	return true;
}
static inline bool atomic_dec_unless_positive(atomic_t *v)
{
	int c = atomic_read(v);
	do {
		if (unlikely(c > 0))
			return false;
	} while (!atomic_try_cmpxchg(v, &c, c - 1));
	return true;
}
static inline int atomic_dec_if_positive(atomic_t *v)
{
	int dec, c = atomic_read(v);
	do {
		dec = c - 1;
		if (unlikely(dec < 0))
			break;
	} while (!atomic_try_cmpxchg(v, &c, dec));
	return dec;
}

static inline s64 atomic64_read(const atomic64_t *v) { return __atomic_load_n(&v->counter, __ATOMIC_RELAXED); }
static inline void atomic64_set(atomic64_t *v, s64 i) { __atomic_store_n(&v->counter, i, __ATOMIC_RELAXED); }
static inline void atomic64_add(s64 i, atomic64_t *v) { __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline void atomic64_sub(s64 i, atomic64_t *v) { __atomic_fetch_sub(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline void atomic64_inc(atomic64_t *v) { atomic64_add(1, v); }
static inline void atomic64_dec(atomic64_t *v) { atomic64_sub(1, v); }
static inline s64 atomic64_add_return(s64 i, atomic64_t *v) { return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline s64 atomic64_sub_return(s64 i, atomic64_t *v) { return __atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline s64 atomic64_fetch_add(s64 i, atomic64_t *v) { return __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline s64 atomic64_inc_return(atomic64_t *v) { return atomic64_add_return(1, v); }
static inline s64 atomic64_dec_return(atomic64_t *v) { return atomic64_sub_return(1, v); }
static inline bool atomic64_dec_and_test(atomic64_t *v) { return atomic64_dec_return(v) == 0; }
static inline bool atomic64_inc_and_test(atomic64_t *v) { return atomic64_inc_return(v) == 0; }
static inline s64 atomic64_xchg(atomic64_t *v, s64 new) { return __atomic_exchange_n(&v->counter, new, __ATOMIC_SEQ_CST); }
static inline s64 atomic64_cmpxchg(atomic64_t *v, s64 old, s64 new)
{
	__atomic_compare_exchange_n(&v->counter, &old, new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return old;
}
static inline bool atomic64_try_cmpxchg(atomic64_t *v, s64 *old, s64 new)
{
	return __atomic_compare_exchange_n(&v->counter, old, new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
#define atomic_long_read	atomic64_read
#define atomic_long_set		atomic64_set
#define atomic_long_add		atomic64_add
#define atomic_long_sub		atomic64_sub
#define atomic_long_inc		atomic64_inc
#define atomic_long_dec		atomic64_dec
#define atomic_long_inc_return	atomic64_inc_return
#define atomic_long_dec_return	atomic64_dec_return
#define atomic_long_add_return	atomic64_add_return
#define atomic_long_cmpxchg	atomic64_cmpxchg
#define atomic_long_xchg	atomic64_xchg
#define atomic_long_dec_and_test atomic64_dec_and_test

#define xchg(ptr, v)		__atomic_exchange_n((ptr), (v), __ATOMIC_SEQ_CST)
#define cmpxchg(ptr, o, n) ({ \
	typeof(*(ptr)) __o = (o); \
	__atomic_compare_exchange_n((ptr), &__o, (n), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
	__o; })
#define cmpxchg64(ptr, o, n)	cmpxchg(ptr, o, n)
#define cmpxchg_relaxed(ptr, o, n) cmpxchg(ptr, o, n)
#define try_cmpxchg(ptr, pold, n) \
	__atomic_compare_exchange_n((ptr), (pold), (n), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define atomic_cmpxchg_relaxed	atomic_cmpxchg
#define atomic_cmpxchg_acquire	atomic_cmpxchg
#define atomic_xchg_relaxed	atomic_xchg
#define atomic_inc_return_relaxed atomic_inc_return
#define atomic_fetch_add_relaxed atomic_fetch_add
#endif /* _LKPI_LINUX_ATOMIC_H_ */
