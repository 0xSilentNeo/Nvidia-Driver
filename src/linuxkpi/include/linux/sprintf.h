/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_SPRINTF_H_
#define _LKPI_LINUX_SPRINTF_H_
#include <stdarg.h>
#include <linux/types.h>
#include <linux/compiler.h>

#if defined(__APPLE__) && defined(KERNEL)
/*
 * Kernel.framework has no <stdio.h>: a Mac build (no -nostdinc) would
 * silently take MacOSX.sdk/usr/include/stdio.h, the user-space one.  The
 * kernel's printf family is declared in <libkern/libkern.h>, which this
 * layer must not include: it also defines min()/max()/fls()/ffs() and (xnu
 * 12377) a non-static scnprintf()/vscnprintf() that the Linux headers define
 * differently.  These are libkern.h's prototypes (compatible redeclarations
 * should both ever meet); the symbols are exported by com.apple.kpi.libkern.
 */
#include <string.h>
int snprintf(char *, size_t, const char *, ...) __attribute__((format(printf, 3, 4)));
int vsnprintf(char *, size_t, const char *, va_list) __attribute__((format(printf, 3, 0)));
/* Linux has an unbounded sprintf() (mt7925/init.c uses it); libkern.h marks
 * its export __deprecated, which would warn in vendored code. */
int sprintf(char *, const char *, ...) __attribute__((format(printf, 2, 3)));
int vsprintf(char *, const char *, va_list) __attribute__((format(printf, 2, 0)));
int printf(const char *, ...) __attribute__((format(printf, 1, 2)));
int vprintf(const char *, va_list) __attribute__((format(printf, 1, 0)));
int sscanf(const char *, const char *, ...) __attribute__((format(scanf, 2, 3)));
int vsscanf(const char *, const char *, va_list);
#else
#include <stdio.h>
#endif

static inline int scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int i;
	va_start(ap, fmt);
	i = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	if (size == 0)
		return 0;
	if (i >= (int)size)
		return (int)size - 1;
	return i < 0 ? 0 : i;
}
static inline int vscnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	int i = vsnprintf(buf, size, fmt, ap);
	if (size == 0)
		return 0;
	if (i >= (int)size)
		return (int)size - 1;
	return i < 0 ? 0 : i;
}
char *kasprintf(gfp_t gfp, const char *fmt, ...) __printf(2, 3);
char *kvasprintf(gfp_t gfp, const char *fmt, va_list ap);
#endif
