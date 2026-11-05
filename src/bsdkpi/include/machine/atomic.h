/* SPDX-License-Identifier: MIT */
/*
 * machine/atomic.h - FreeBSD atomic(9) on the compiler's __atomic builtins.
 * The names do not collide with LinuxKPI's atomic_t API (atomic_add vs
 * atomic_add_int, atomic_set vs atomic_set_int, ...).  FreeBSD's
 * atomic_set_* is a bit-wise OR, atomic_clear_* a bit-wise AND-NOT.
 */
#ifndef _BSDKPI_MACHINE_ATOMIC_H_
#define _BSDKPI_MACHINE_ATOMIC_H_
#include <sys/cdefs.h>
#include <sys/types.h>

#define _BSDKPI_ATOMIC_OPS(NAME, TYPE)					\
static __inline void							\
atomic_add_##NAME(volatile TYPE *p, TYPE v)				\
{									\
	(void)__atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);		\
}									\
static __inline void							\
atomic_subtract_##NAME(volatile TYPE *p, TYPE v)			\
{									\
	(void)__atomic_fetch_sub(p, v, __ATOMIC_SEQ_CST);		\
}									\
static __inline void							\
atomic_set_##NAME(volatile TYPE *p, TYPE v)				\
{									\
	(void)__atomic_fetch_or(p, v, __ATOMIC_SEQ_CST);		\
}									\
static __inline void							\
atomic_clear_##NAME(volatile TYPE *p, TYPE v)				\
{									\
	(void)__atomic_fetch_and(p, (TYPE)~v, __ATOMIC_SEQ_CST);	\
}									\
static __inline TYPE							\
atomic_fetchadd_##NAME(volatile TYPE *p, TYPE v)			\
{									\
	return (__atomic_fetch_add(p, v, __ATOMIC_SEQ_CST));		\
}									\
static __inline TYPE							\
atomic_load_##NAME(volatile TYPE *p)					\
{									\
	return (__atomic_load_n(p, __ATOMIC_RELAXED));			\
}									\
static __inline TYPE							\
atomic_load_acq_##NAME(volatile TYPE *p)				\
{									\
	return (__atomic_load_n(p, __ATOMIC_ACQUIRE));			\
}									\
static __inline void							\
atomic_store_##NAME(volatile TYPE *p, TYPE v)				\
{									\
	__atomic_store_n(p, v, __ATOMIC_RELAXED);			\
}									\
static __inline void							\
atomic_store_rel_##NAME(volatile TYPE *p, TYPE v)			\
{									\
	__atomic_store_n(p, v, __ATOMIC_RELEASE);			\
}									\
static __inline TYPE							\
atomic_swap_##NAME(volatile TYPE *p, TYPE v)				\
{									\
	return (__atomic_exchange_n(p, v, __ATOMIC_SEQ_CST));		\
}									\
static __inline TYPE							\
atomic_readandclear_##NAME(volatile TYPE *p)				\
{									\
	return (__atomic_exchange_n(p, (TYPE)0, __ATOMIC_SEQ_CST));	\
}									\
static __inline int							\
atomic_cmpset_##NAME(volatile TYPE *p, TYPE old, TYPE new)		\
{									\
	return (__atomic_compare_exchange_n(p, &old, new, 0,		\
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));			\
}									\
static __inline int							\
atomic_fcmpset_##NAME(volatile TYPE *p, TYPE *old, TYPE new)		\
{									\
	return (__atomic_compare_exchange_n(p, old, new, 0,		\
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));			\
}									\
static __inline int							\
atomic_testandset_##NAME(volatile TYPE *p, u_int bit)			\
{									\
	TYPE m = (TYPE)1 << (bit % (sizeof(TYPE) * 8));			\
	return ((__atomic_fetch_or(p, m, __ATOMIC_SEQ_CST) & m) != 0);	\
}									\
static __inline int							\
atomic_testandclear_##NAME(volatile TYPE *p, u_int bit)			\
{									\
	TYPE m = (TYPE)1 << (bit % (sizeof(TYPE) * 8));			\
	return ((__atomic_fetch_and(p, (TYPE)~m, __ATOMIC_SEQ_CST) & m) != 0); \
}

_BSDKPI_ATOMIC_OPS(char, u_char)
_BSDKPI_ATOMIC_OPS(short, u_short)
_BSDKPI_ATOMIC_OPS(int, u_int)
_BSDKPI_ATOMIC_OPS(long, u_long)
_BSDKPI_ATOMIC_OPS(8, uint8_t)
_BSDKPI_ATOMIC_OPS(16, uint16_t)
_BSDKPI_ATOMIC_OPS(32, uint32_t)
_BSDKPI_ATOMIC_OPS(64, uint64_t)
_BSDKPI_ATOMIC_OPS(ptr, uintptr_t)
#undef _BSDKPI_ATOMIC_OPS

/* acq/rel variants: the sequentially-consistent ones above are stronger. */
#define atomic_add_acq_int	atomic_add_int
#define atomic_add_rel_int	atomic_add_int
#define atomic_subtract_acq_int	atomic_subtract_int
#define atomic_subtract_rel_int	atomic_subtract_int
#define atomic_set_acq_int	atomic_set_int
#define atomic_set_rel_int	atomic_set_int
#define atomic_clear_acq_int	atomic_clear_int
#define atomic_clear_rel_int	atomic_clear_int
#define atomic_cmpset_acq_int	atomic_cmpset_int
#define atomic_cmpset_rel_int	atomic_cmpset_int
#define atomic_fcmpset_acq_int	atomic_fcmpset_int
#define atomic_fcmpset_rel_int	atomic_fcmpset_int
#define atomic_add_acq_32	atomic_add_32
#define atomic_add_rel_32	atomic_add_32
#define atomic_subtract_acq_32	atomic_subtract_32
#define atomic_subtract_rel_32	atomic_subtract_32
#define atomic_set_acq_32	atomic_set_32
#define atomic_set_rel_32	atomic_set_32
#define atomic_clear_acq_32	atomic_clear_32
#define atomic_clear_rel_32	atomic_clear_32
#define atomic_cmpset_acq_32	atomic_cmpset_32
#define atomic_cmpset_rel_32	atomic_cmpset_32
#define atomic_fcmpset_acq_32	atomic_fcmpset_32
#define atomic_fcmpset_rel_32	atomic_fcmpset_32
#define atomic_add_acq_long	atomic_add_long
#define atomic_add_rel_long	atomic_add_long
#define atomic_subtract_acq_long atomic_subtract_long
#define atomic_subtract_rel_long atomic_subtract_long
#define atomic_set_acq_long	atomic_set_long
#define atomic_set_rel_long	atomic_set_long
#define atomic_clear_acq_long	atomic_clear_long
#define atomic_clear_rel_long	atomic_clear_long
#define atomic_cmpset_acq_long	atomic_cmpset_long
#define atomic_cmpset_rel_long	atomic_cmpset_long
#define atomic_fcmpset_acq_long	atomic_fcmpset_long
#define atomic_fcmpset_rel_long	atomic_fcmpset_long
#define atomic_cmpset_acq_ptr	atomic_cmpset_ptr
#define atomic_cmpset_rel_ptr	atomic_cmpset_ptr
#define atomic_fcmpset_acq_ptr	atomic_fcmpset_ptr
#define atomic_fcmpset_rel_ptr	atomic_fcmpset_ptr
#define atomic_cmpset_acq_64	atomic_cmpset_64
#define atomic_cmpset_rel_64	atomic_cmpset_64
#define atomic_add_acq_64	atomic_add_64
#define atomic_add_rel_64	atomic_add_64
#define atomic_subtract_acq_64	atomic_subtract_64
#define atomic_subtract_rel_64	atomic_subtract_64

#define atomic_load_acq_ptr(p)	((uintptr_t)__atomic_load_n((p), __ATOMIC_ACQUIRE))
#define atomic_load_consume_ptr(p) ((uintptr_t)__atomic_load_n((p), __ATOMIC_ACQUIRE))
#define atomic_store_ptr(p, v)	__atomic_store_n((p), (v), __ATOMIC_RELAXED)

#define atomic_thread_fence_acq()	__atomic_thread_fence(__ATOMIC_ACQUIRE)
#define atomic_thread_fence_rel()	__atomic_thread_fence(__ATOMIC_RELEASE)
#define atomic_thread_fence_acq_rel()	__atomic_thread_fence(__ATOMIC_ACQ_REL)
#define atomic_thread_fence_seq_cst()	__atomic_thread_fence(__ATOMIC_SEQ_CST)
#define atomic_interrupt_fence()	__atomic_signal_fence(__ATOMIC_SEQ_CST)

/* mb()/rmb()/wmb() come from linux/compiler.h with the same meaning. */
#ifndef mb
#define mb()	__atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif
#ifndef rmb
#define rmb()	__atomic_thread_fence(__ATOMIC_ACQUIRE)
#endif
#ifndef wmb
#define wmb()	__atomic_thread_fence(__ATOMIC_RELEASE)
#endif

#endif /* _BSDKPI_MACHINE_ATOMIC_H_ */
