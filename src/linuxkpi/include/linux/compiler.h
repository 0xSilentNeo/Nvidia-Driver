/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_COMPILER_H_
#define _LKPI_LINUX_COMPILER_H_

#include <linux/compiler_types.h>

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#define barrier()	__asm__ __volatile__("" : : : "memory")
#define barrier_data(p)	__asm__ __volatile__("" : : "r"(p) : "memory")
#define unreachable()	__builtin_unreachable()
#define OPTIMIZER_HIDE_VAR(var) __asm__ ("" : "=r" (var) : "0" (var))

#define ACCESS_ONCE(x)	(*(volatile typeof(x) *)&(x))
#define READ_ONCE(x)	__atomic_load_n(&(x), __ATOMIC_RELAXED)
#define WRITE_ONCE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELAXED)

#define smp_mb()	__atomic_thread_fence(__ATOMIC_SEQ_CST)
#define smp_rmb()	__atomic_thread_fence(__ATOMIC_ACQUIRE)
#define smp_wmb()	__atomic_thread_fence(__ATOMIC_RELEASE)
#define smp_mb__before_atomic()	smp_mb()
#define smp_mb__after_atomic()	smp_mb()
#define mb()		smp_mb()
#define rmb()		smp_rmb()
#define wmb()		smp_wmb()
#define dma_rmb()	smp_rmb()
#define dma_wmb()	smp_wmb()
#define smp_store_release(p, v)	__atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define smp_load_acquire(p)	__atomic_load_n((p), __ATOMIC_ACQUIRE)
#define smp_store_mb(var, value) do { WRITE_ONCE(var, value); smp_mb(); } while (0)

#define __is_constexpr(x) \
	(sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8)))

#define RELOC_HIDE(ptr, off) ({ unsigned long __ptr; \
	__ptr = (unsigned long)(ptr); (typeof(ptr))(__ptr + (off)); })

#define uninitialized_var(x) x = x
#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)
#define ___PASTE(a, b) a##b
#define __PASTE(a, b) ___PASTE(a, b)

#define __ADDRESSABLE(sym)
#define data_race(expr) (expr)
#define __no_kasan_or_inline inline
#define __no_sanitize_or_inline inline

#endif /* _LKPI_LINUX_COMPILER_H_ */
