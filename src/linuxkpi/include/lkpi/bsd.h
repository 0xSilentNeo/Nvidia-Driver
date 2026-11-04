/* SPDX-License-Identifier: MIT */
/* FreeBSD idioms the vendored LinuxKPI 802.11 headers rely on. */
#ifndef _LKPI_BSD_H_
#define _LKPI_BSD_H_
#include <lkpi/os.h>
#include <stdint.h>

/* FreeBSD's net80211/ieee80211.h hides the 11ac/11ax definitions unless
 * built for the kernel or explicitly requested. */
#ifndef WANT_NET80211
#define WANT_NET80211 1
#endif

/* Constant-expression byte-order helpers: glibc's are function calls and the
 * vendored headers use htole16() in case labels.  Little-endian only. */
#undef htole16
#undef htole32
#undef htole64
#undef le16toh
#undef le32toh
#undef le64toh
#undef htobe16
#undef htobe32
#undef htobe64
#undef be16toh
#undef be32toh
#undef be64toh
#define htole16(x) ((uint16_t)(x))
#define htole32(x) ((uint32_t)(x))
#define htole64(x) ((uint64_t)(x))
#define le16toh(x) ((uint16_t)(x))
#define le32toh(x) ((uint32_t)(x))
#define le64toh(x) ((uint64_t)(x))
#define htobe16(x) __builtin_bswap16((uint16_t)(x))
#define htobe32(x) __builtin_bswap32((uint32_t)(x))
#define htobe64(x) __builtin_bswap64((uint64_t)(x))
#define be16toh(x) __builtin_bswap16((uint16_t)(x))
#define be32toh(x) __builtin_bswap32((uint32_t)(x))
#define be64toh(x) __builtin_bswap64((uint64_t)(x))
#ifndef KASSERT
#define KASSERT(exp, msg) do { if (__builtin_expect(!(exp), 0)) lkpi_os_panic msg; } while (0)
#endif
#ifndef LKPI_MACOS
#define LKPI_MACOS 1
#endif
#ifndef nitems
#define nitems(x) (sizeof((x)) / sizeof((x)[0]))
#endif
#ifndef CTASSERT
#define CTASSERT(x) _Static_assert((x), #x)
#endif
#ifndef panic
#define panic(...) lkpi_os_panic(__VA_ARGS__)
#endif
#ifndef __printflike
#define __printflike(a, b) __attribute__((format(printf, a, b)))
#endif
#ifndef MALLOC_DECLARE
#define MALLOC_DECLARE(type)
#endif
#ifndef __DECONST
#define __DECONST(type, var) ((type)(uintptr_t)(const void *)(var))
#endif
#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif
#ifndef MPASS
#define MPASS(exp) KASSERT((exp), ("%s:%d: assertion %s failed", __FILE__, __LINE__, #exp))
#endif
/* The kernel already exports printf(); the host has stdio. */
#if !defined(KERNEL) && !defined(__KERNEL__)
#include <stdio.h>
#endif
#endif
