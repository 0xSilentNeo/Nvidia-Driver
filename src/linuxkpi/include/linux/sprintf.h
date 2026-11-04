/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_SPRINTF_H_
#define _LKPI_LINUX_SPRINTF_H_
#include <stdarg.h>
#include <linux/types.h>
#include <linux/compiler.h>

#ifdef __APPLE__
#ifdef KERNEL
/* libkern: int snprintf(char *, size_t, const char *, ...); int vsnprintf(...) */
#include <string.h>
#include <stdio.h>
#else
#include <stdio.h>
#endif
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
