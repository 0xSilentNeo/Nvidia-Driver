/* SPDX-License-Identifier: MIT */
/*
 * String helpers.  memcpy/memset/strlen and friends come from libkern in the
 * kernel and from libc on the host; this only adds the Linux-only ones.
 */
#ifndef _LKPI_LINUX_STRING_H_
#define _LKPI_LINUX_STRING_H_
#include <string.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/err.h>


static inline ssize_t __lkpi_strscpy(char *dst, const char *src, size_t size)
{
	size_t len;
	if (size == 0)
		return -E2BIG;
	len = strnlen(src, size);
	if (len >= size) {
		memcpy(dst, src, size - 1);
		dst[size - 1] = '\0';
		return -E2BIG;
	}
	memcpy(dst, src, len + 1);
	return (ssize_t)len;
}
#define strscpy_pad(dst, src, size) ({ \
	ssize_t __r = __lkpi_strscpy(dst, src, size); \
	if (__r >= 0 && (size_t)__r < (size)) memset((dst) + __r, 0, (size) - __r); __r; })
#define sized_strscpy __lkpi_strscpy
/* Linux accepts strscpy(dst, src) and sizes it from the destination array. */
#define __lkpi_strscpy2(d, s)		__lkpi_strscpy(d, s, sizeof(d))
#define __lkpi_strscpy3(d, s, n)	__lkpi_strscpy(d, s, n)
#define __lkpi_strscpy_sel(_1, _2, _3, NAME, ...) NAME
#define strscpy(...) __lkpi_strscpy_sel(__VA_ARGS__, __lkpi_strscpy3, __lkpi_strscpy2)(__VA_ARGS__)

static inline void memzero_explicit(void *s, size_t count)
{
	memset(s, 0, count);
	barrier_data(s);
}
static inline bool strstarts(const char *str, const char *prefix)
{
	return strncmp(str, prefix, strlen(prefix)) == 0;
}
static inline char *skip_spaces(const char *str)
{
	while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')
		++str;
	return (char *)str;
}
static inline char *strim(char *s)
{
	size_t size;
	char *end;
	size = strlen(s);
	if (!size)
		return s;
	end = s + size - 1;
	while (end >= s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
		end--;
	*(end + 1) = '\0';
	return skip_spaces(s);
}
static inline void *memchr_inv(const void *start, int c, size_t bytes)
{
	const u8 *p = (const u8 *)start;
	while (bytes--) {
		if (*p != (u8)c)
			return (void *)p;
		p++;
	}
	return NULL;
}
static inline int match_string(const char * const *array, size_t n, const char *string)
{
	size_t i;
	for (i = 0; i < n; i++) {
		if (!array[i])
			break;
		if (!strcmp(array[i], string))
			return (int)i;
	}
	return -EINVAL;
}
static inline void *memscan(void *addr, int c, size_t size)
{
	u8 *p = (u8 *)addr;
	while (size) {
		if (*p == (u8)c)
			return p;
		p++;
		size--;
	}
	return p;
}
#define memcpy_and_pad(dest, dest_len, src, count, pad) do { \
	if ((dest_len) > (count)) { \
		memcpy(dest, src, count); \
		memset((u8 *)(dest) + (count), pad, (dest_len) - (count)); \
	} else \
		memcpy(dest, src, dest_len); \
} while (0)
#define unsafe_memcpy(dst, src, bytes, justification) memcpy(dst, src, bytes)
#define memtostr(dest, src) strscpy(dest, src, sizeof(dest))
#define strtomem_pad(dest, src, pad) do { \
	memcpy_and_pad(dest, sizeof(dest), src, strnlen(src, sizeof(dest)), pad); } while (0)

/* Allocating variants live in slab.h (kstrdup, kmemdup). */
#endif /* _LKPI_LINUX_STRING_H_ */
