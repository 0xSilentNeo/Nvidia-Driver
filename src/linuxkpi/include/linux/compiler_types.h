/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_COMPILER_TYPES_H_
#define _LKPI_LINUX_COMPILER_TYPES_H_

#define __user
#define __kernel
#define __iomem
#define __rcu
#define __percpu
#define __force
#define __bitwise
#define __must_hold(x)
#define __acquires(x)
#define __releases(x)
#define __acquire(x)	(void)0
#define __release(x)	(void)0
#define __cond_lock(x, c) (c)
#define __nocast
#define __private
#define __chk_user_ptr(x) (void)0
#define __chk_io_ptr(x)	(void)0
#define __printf(a, b)	__attribute__((format(printf, a, b)))
#define __scanf(a, b)	__attribute__((format(scanf, a, b)))
#define __packed	__attribute__((packed))
#define __aligned(x)	__attribute__((aligned(x)))
#define __aligned_largest __attribute__((aligned))
#define __always_unused	__attribute__((unused))
#define __maybe_unused	__attribute__((unused))
#define __used		__attribute__((used))
#define __unused	__attribute__((unused))
#define __must_check	__attribute__((warn_unused_result))
#define __malloc	__attribute__((malloc))
#define __pure		__attribute__((pure))
#define __noreturn	__attribute__((noreturn))
#define __cold		__attribute__((cold))
#define __weak		__attribute__((weak))
#define __alias(s)	__attribute__((alias(#s)))
#define __section(s)	__attribute__((section(s)))
#define __deprecated
#ifndef __always_inline
#define __always_inline	inline __attribute__((always_inline))
#endif
#define __noinline	__attribute__((noinline))
#define noinline	__attribute__((noinline))
#define __no_sanitize_address
#define __designated_init
#define __visible
#define __init
#define __exit
#define __initdata
#define __initconst
#define __exitdata
#define __devinit
#define __devexit
#define __ref
#define __read_mostly
#define __cacheline_aligned __attribute__((aligned(64)))
#define ____cacheline_aligned __attribute__((aligned(64)))
#define __cacheline_aligned_in_smp __cacheline_aligned
#define ____cacheline_aligned_in_smp ____cacheline_aligned
#define __randomize_layout
#define __no_randomize_layout
#define __counted_by(m)
#define __counted_by_le(m)
#define __counted_by_be(m)
#define __nonstring
/* FreeBSD-derived headers use fallthrough where no label follows; keep it inert. */
#define __fallthrough	do { } while (0)
#define fallthrough	do { } while (0)
#define __diag_push()
#define __diag_pop()
#define __diag_ignore_all(a, b)
#define __assume_aligned(a, ...)
#ifndef __attribute_const__
#define __attribute_const__ __attribute__((const))
#endif
#define __compiletime_warning(msg)
#define __compiletime_error(msg)
#define __stringify_1(x...) #x
#define __stringify(x...)  __stringify_1(x)

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#define asm_inline asm
#define __asm_inline

#define notrace

#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#define __native_word(t) \
	(sizeof(t) == sizeof(char) || sizeof(t) == sizeof(short) || \
	 sizeof(t) == sizeof(int) || sizeof(t) == sizeof(long))

#endif /* _LKPI_LINUX_COMPILER_TYPES_H_ */
