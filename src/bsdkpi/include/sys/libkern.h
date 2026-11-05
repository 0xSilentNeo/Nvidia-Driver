/* SPDX-License-Identifier: MIT */
/*
 * sys/libkern.h - libkern(9).  String functions come from libkern (kext)
 * or libc (host) through <string.h>; this adds the FreeBSD-only helpers.
 * min()/max() are LinuxKPI's type-generic macros from linux/minmax.h, which
 * give the same results as FreeBSD's u_int versions for net80211's uses.
 */
#ifndef _BSDKPI_SYS_LIBKERN_H_
#define _BSDKPI_SYS_LIBKERN_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <string.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/random.h>
#include <linux/minmax.h>

static __inline int imax(int a, int b) { return (a > b ? a : b); }
static __inline int imin(int a, int b) { return (a < b ? a : b); }
static __inline long lmax(long a, long b) { return (a > b ? a : b); }
static __inline long lmin(long a, long b) { return (a < b ? a : b); }
static __inline u_int umax(u_int a, u_int b) { return (a > b ? a : b); }
static __inline u_int umin(u_int a, u_int b) { return (a < b ? a : b); }
static __inline u_long ulmax(u_long a, u_long b) { return (a > b ? a : b); }
static __inline u_long ulmin(u_long a, u_long b) { return (a < b ? a : b); }
static __inline __uintmax_t ummax(__uintmax_t a, __uintmax_t b) { return (a > b ? a : b); }
static __inline __uintmax_t ummin(__uintmax_t a, __uintmax_t b) { return (a < b ? a : b); }
static __inline off_t omax(off_t a, off_t b) { return (a > b ? a : b); }
static __inline off_t omin(off_t a, off_t b) { return (a < b ? a : b); }
static __inline quad_t qmax(quad_t a, quad_t b) { return (a > b ? a : b); }
static __inline quad_t qmin(quad_t a, quad_t b) { return (a < b ? a : b); }
static __inline u_quad_t uqmax(u_quad_t a, u_quad_t b) { return (a > b ? a : b); }
static __inline u_quad_t uqmin(u_quad_t a, u_quad_t b) { return (a < b ? a : b); }
static __inline int abs(int a) { return (a < 0 ? -a : a); }
static __inline long labs(long a) { return (a < 0 ? -a : a); }
static __inline quad_t qabs(quad_t a) { return (a < 0 ? -a : a); }

#ifndef ARB_
static __inline int bitcount32(uint32_t x) { return (__builtin_popcount(x)); }
static __inline int bitcount16(uint32_t x) { return (__builtin_popcount(x & 0xffff)); }
static __inline int bitcount64(uint64_t x) { return (__builtin_popcountll(x)); }
static __inline int bitcountl(u_long x) { return (__builtin_popcountl(x)); }
static __inline int bitcount(u_int x) { return (__builtin_popcount(x)); }
#endif
/* ffs/fls: bit numbers 1..N, 0 for no bit set (ffs itself comes from libc). */
#ifndef __APPLE__
static __inline int ffsl(long mask) { return (__builtin_ffsl(mask)); }
static __inline int ffsll(long long mask) { return (__builtin_ffsll(mask)); }
#endif
static __inline int fls(int mask) { return (mask == 0 ? 0 : 32 - __builtin_clz((u_int)mask)); }
static __inline int flsl(long mask) { return (mask == 0 ? 0 : 64 - __builtin_clzl((u_long)mask)); }
static __inline int flsll(long long mask) { return (mask == 0 ? 0 : 64 - __builtin_clzll((unsigned long long)mask)); }
static __inline int ilog2_int(int x) { return (fls(x) - 1); }

/* arc4random(9) on the OS random source. */
static __inline uint32_t arc4random(void) { return (get_random_u32()); }
static __inline void arc4random_buf(void *p, size_t n) { get_random_bytes(p, n); }
static __inline uint32_t arc4random_uniform(uint32_t bound) { return (get_random_u32_below(bound)); }
static __inline void arc4rand(void *ptr, u_int len, int reseed) { (void)reseed; get_random_bytes(ptr, len); }
static __inline void read_random(void *p, u_int n) { get_random_bytes(p, n); }
static __inline u_long _bsdkpi_random(void) { return ((u_long)get_random_u32()); }
#define random()	_bsdkpi_random()

/* libkern / libc string functions FreeBSD code expects to find here. */
#ifndef __APPLE__
size_t	strlcpy(char *, const char *, size_t);
size_t	strlcat(char *, const char *, size_t);
#endif
char	*bsdkpi_strsep(char **, const char *);
#define strsep(s, d)	bsdkpi_strsep((s), (d))
void	*bsdkpi_memcchr(const void *, int, size_t);
#define memcchr(b, c, l) bsdkpi_memcchr((b), (c), (l))
#ifndef __APPLE__
long	strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
#endif
#define strtoq(s, e, b)	((quad_t)strtoll((s), (e), (b)))
#define strtouq(s, e, b) ((u_quad_t)strtoull((s), (e), (b)))
void	bsdkpi_hexdump(const void *ptr, int length, const char *hdr, int flags);
#define hexdump(p, l, h, f) bsdkpi_hexdump((p), (l), (h), (f))
#define HD_COLUMN_MASK	0xff
#define HD_DELIM_MASK	0xff00
#define HD_OMIT_COUNT	(1 << 16)
#define HD_OMIT_HEX	(1 << 17)
#define HD_OMIT_CHARS	(1 << 18)

static __inline char *
index(const char *p, int ch)
{
	return (strchr(p, ch));
}

static __inline char *
rindex(const char *p, int ch)
{
	return (strrchr(p, ch));
}

#define bsdkpi_memset_s(d, dl, c, n) ({ memset((d), (c), (n)); 0; })
#define memset_s(d, dl, c, n) bsdkpi_memset_s(d, dl, c, n)
#define explicit_bzero(b, l)	memzero_explicit((b), (l))

static __inline uint32_t
crc32_raw(const void *buf, size_t size, uint32_t crc)
{
	const uint8_t *p = (const uint8_t *)buf;
	int i;

	while (size--) {
		crc ^= *p++;
		for (i = 0; i < 8; i++)
			crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
	}
	return (crc);
}

static __inline uint32_t
crc32(const void *buf, size_t size)
{
	return (crc32_raw(buf, size, ~0U) ^ ~0U);
}

#endif /* _BSDKPI_SYS_LIBKERN_H_ */
