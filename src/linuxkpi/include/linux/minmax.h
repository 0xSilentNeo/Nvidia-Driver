/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_MINMAX_H_
#define _LKPI_LINUX_MINMAX_H_
#include <linux/compiler.h>
#include <linux/types.h>

#define __cmp(op, x, y)	((x) op (y) ? (x) : (y))
#define __cmp_once(op, x, y, ux, uy) ({ \
	typeof(x) ux = (x); typeof(y) uy = (y); __cmp(op, ux, uy); })
#define min(x, y)	__cmp_once(<, x, y, __UNIQUE_ID(min1_), __UNIQUE_ID(min2_))
#define max(x, y)	__cmp_once(>, x, y, __UNIQUE_ID(max1_), __UNIQUE_ID(max2_))
#define umin(x, y)	min((u64)(x), (u64)(y))
#define umax(x, y)	max((u64)(x), (u64)(y))
#define min_t(type, x, y) __cmp_once(<, (type)(x), (type)(y), __UNIQUE_ID(min1_), __UNIQUE_ID(min2_))
#define max_t(type, x, y) __cmp_once(>, (type)(x), (type)(y), __UNIQUE_ID(max1_), __UNIQUE_ID(max2_))
#define min3(x, y, z)	min(min(x, y), z)
#define max3(x, y, z)	max(max(x, y), z)
#define clamp(val, lo, hi) min(max(val, lo), hi)
#define clamp_t(type, val, lo, hi) min_t(type, max_t(type, val, lo), hi)
#define clamp_val(val, lo, hi) clamp_t(typeof(val), val, lo, hi)
#define min_not_zero(x, y) ({ \
	typeof(x) __x = (x); typeof(y) __y = (y); \
	__x == 0 ? __y : ((__y == 0) ? __x : min(__x, __y)); })
#define swap(a, b) do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN_T(type, a, b) __cmp(<, (type)(a), (type)(b))
#define MAX_T(type, a, b) __cmp(>, (type)(a), (type)(b))
#endif
