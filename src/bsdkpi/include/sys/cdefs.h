/* SPDX-License-Identifier: MIT */
/*
 * sys/cdefs.h - FreeBSD compiler-definition macros on top of the host's.
 *
 * The host (glibc on the Linux test machine, XNU's Kernel.framework on
 * macOS) has its own <sys/cdefs.h> that its other headers rely on, so this
 * one chains to it with #include_next and then adds the FreeBSD spellings
 * that net80211 uses.  Everything is guarded: LinuxKPI's
 * linux/compiler_types.h defines some of the same names and a macro may be
 * redefined only with an identical replacement list.
 */
#ifndef _BSDKPI_SYS_CDEFS_H_
#define _BSDKPI_SYS_CDEFS_H_

#include_next <sys/cdefs.h>

/*
 * The vendored FreeBSD sources select their OS layer with __FreeBSD__
 * (net80211/ieee80211_var.h picks ieee80211_freebsd.h); on the Linux host
 * clang predefines __linux__ instead, and on macOS __APPLE__.  bsdkpi is the
 * FreeBSD kernel API, so every translation unit that includes it is a
 * FreeBSD one.  The vendored LinuxKPI 802.11 headers already treat
 * __FreeBSD__ and LKPI_MACOS alike.
 */
#ifndef __FreeBSD__
#define __FreeBSD__		15
#endif

#ifndef __FBSDID
#define __FBSDID(s)
#endif
#ifndef __RCSID
#define __RCSID(s)
#endif
#ifndef __COPYRIGHT
#define __COPYRIGHT(s)
#endif

#ifndef __BEGIN_DECLS
#ifdef __cplusplus
#define __BEGIN_DECLS		extern "C" {
#define __END_DECLS		}
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif
#endif

#ifndef __P
#define __P(protos)		protos
#endif
#ifndef __CONCAT
#define __CONCAT1(x, y)		x ## y
#define __CONCAT(x, y)		__CONCAT1(x, y)
#endif
#ifndef __STRING
#define __STRING(x)		#x
#endif
#ifndef __XSTRING
#define __XSTRING(x)		__STRING(x)
#endif
#ifndef __inline
#define __inline		inline
#endif
#ifndef __volatile
#define __volatile		volatile
#endif
#ifndef __restrict
#define __restrict		restrict
#endif
#ifndef __const
#define __const			const
#endif
#ifndef __signed
#define __signed		signed
#endif

/* Same replacement lists as linux/compiler_types.h, which defines these
 * unconditionally; identical redefinitions are silent. */
#ifndef __packed
#define __packed	__attribute__((packed))
#endif
#ifndef __aligned
#define __aligned(x)	__attribute__((aligned(x)))
#endif
#ifndef __section
#define __section(s)	__attribute__((section(s)))
#endif
#ifndef __used
#define __used		__attribute__((used))
#endif
#ifndef __unused
#define __unused	__attribute__((unused))
#endif
#ifndef __noinline
#define __noinline	__attribute__((noinline))
#endif
#ifndef __pure
#define __pure		__attribute__((pure))
#endif
#ifndef __weak
#define __weak		__attribute__((weak))
#endif
#ifndef __cold
#define __cold		__attribute__((cold))
#endif
#ifndef __deprecated
#define __deprecated
#endif
#ifndef __read_mostly
#define __read_mostly
#endif
#ifndef __always_inline
#define __always_inline	inline __attribute__((always_inline))
#endif
/*
 * FreeBSD kernel printf has %D (hex dump) and %b (bit field) conversions;
 * clang checks them with the freebsd_kprintf format kind, and the shim's
 * printf()/log() implement them (src/printf.c).  lkpi/bsd.h's plain printf
 * variant is only seen by translation units that never include this file.
 */
#ifndef __printflike
#define __printflike(a, b) __attribute__((format(freebsd_kprintf, a, b)))
#endif
#ifndef __scanflike
#define __scanflike(a, b) __attribute__((format(scanf, a, b)))
#endif

#ifndef __dead2
#define __dead2			__attribute__((__noreturn__))
#endif
#ifndef __pure2
#define __pure2			__attribute__((__const__))
#endif
#ifndef __exported
#define __exported		__attribute__((__visibility__("default")))
#endif
#ifndef __hidden
#define __hidden		__attribute__((__visibility__("hidden")))
#endif
#ifndef __diagused
#define __diagused
#endif
#ifndef __witness_used
#define __witness_used		__unused
#endif
#ifndef __result_use_check
#define __result_use_check	__attribute__((__warn_unused_result__))
#endif
#ifndef __returns_twice
#define __returns_twice		__attribute__((__returns_twice__))
#endif
#ifndef __alloc_size
#define __alloc_size(x)		__attribute__((__alloc_size__(x)))
#endif
#ifndef __alloc_size2
#define __alloc_size2(n, x)	__attribute__((__alloc_size__(n, x)))
#endif
#ifndef __alloc_align
#define __alloc_align(x)	__attribute__((__alloc_align__(x)))
#endif
#ifndef __malloc_like
#define __malloc_like		__attribute__((__malloc__))
#endif
#ifndef __nonnull_all
#define __nonnull_all		__attribute__((__nonnull__))
#endif
#ifndef __weak_symbol
#define __weak_symbol		__attribute__((__weak__))
#endif
#ifndef __nosanitizeaddress
#define __nosanitizeaddress
#endif
#ifndef __nosanitizethread
#define __nosanitizethread
#endif
#ifndef __nosanitizememory
#define __nosanitizememory
#endif
#ifndef __nosanitizecoverage
#define __nosanitizecoverage
#endif
#ifndef __noexcept
#define __noexcept
#endif
#ifndef __fastcall
#define __fastcall
#endif
#ifndef __min_size
#define __min_size(x)		(x)
#endif
#ifndef __guarded_by
#define __guarded_by(x)
#define __pt_guarded_by(x)
#define __lock_annotate(x)
#define __lockable
#define __locks_exclusive(...)
#define __locks_shared(...)
#define __trylocks_exclusive(...)
#define __trylocks_shared(...)
#define __unlocks(...)
#define __asserts_exclusive(...)
#define __asserts_shared(...)
#define __requires_exclusive(...)
#define __requires_shared(...)
#define __requires_unlocked(...)
#define __no_lock_analysis
#endif

#ifndef __predict_true
#define __predict_true(exp)	__builtin_expect((exp), 1)
#endif
#ifndef __predict_false
#define __predict_false(exp)	__builtin_expect((exp), 0)
#endif
#ifndef __unreachable
#define __unreachable()		__builtin_unreachable()
#endif

#ifndef __offsetof
#define __offsetof(type, field)	__builtin_offsetof(type, field)
#endif
#ifndef __rangeof
#define __rangeof(type, start, end) \
	(__offsetof(type, end) - __offsetof(type, start))
#endif
#ifndef __containerof
#define __containerof(x, s, m) ({ \
	const volatile __typeof(((s *)0)->m) *__x = (x); \
	__DEQUALIFY(s *, (const volatile char *)__x - __offsetof(s, m)); })
#endif
#ifndef __DECONST
#define __DECONST(type, var)	((type)(uintptr_t)(const void *)(var))
#endif
#ifndef __DEVOLATILE
#define __DEVOLATILE(type, var)	((type)(uintptr_t)(volatile void *)(var))
#endif
#ifndef __DEQUALIFY
#define __DEQUALIFY(type, var)	((type)(uintptr_t)(const volatile void *)(var))
#endif

#ifndef __va_list
#include <stdarg.h>
#define __va_list		va_list
#endif

#ifndef __GNUCLIKE_ASM
#define __GNUCLIKE_ASM			3
#define __GNUCLIKE___TYPEOF		1
#define __GNUCLIKE___OFFSETOF		1
#define __GNUCLIKE___SECTION		1
#define __GNUCLIKE_CTOR_SECTION_HANDLING 1
#define __GNUCLIKE_BUILTIN_CONSTANT_P	1
#define __GNUCLIKE_BUILTIN_VARARGS	1
#define __GNUCLIKE_BUILTIN_STDARG	1
#define __GNUCLIKE_BUILTIN_VAALIST	1
#define __GNUC_VA_LIST_COMPATIBILITY	1
#define __GNUCLIKE_BUILTIN_NEXT_ARG	1
#define __GNUCLIKE_MATH_BUILTIN_RELOPS	1
#define __GNUCLIKE_BUILTIN_MEMCPY	1
#define __CC_SUPPORTS_INLINE		1
#define __CC_SUPPORTS___INLINE		1
#define __CC_SUPPORTS___INLINE__	1
#define __CC_SUPPORTS___FUNC__		1
#define __CC_SUPPORTS_WARNING		1
#define __CC_SUPPORTS_VARADIC_XXX	1
#define __CC_SUPPORTS_DYNAMIC_ARRAY_INIT 1
#endif

#ifndef __has_feature
#define __has_feature(x)	0
#endif
#ifndef __has_extension
#define __has_extension		__has_feature
#endif

/* Linker sets are emulated with constructors; see sys/linker_set.h. */
#define __MAKE_SET(set, sym)	_BSDKPI_LINKER_SET_ENTRY(set, sym)

#endif /* _BSDKPI_SYS_CDEFS_H_ */
