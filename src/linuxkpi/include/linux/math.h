/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_MATH_H_
#define _LKPI_LINUX_MATH_H_
#include <linux/types.h>
#include <linux/compiler.h>

#define __round_mask(x, y)	((typeof(x))((y) - 1))
#define round_up(x, y)		((((x) - 1) | __round_mask(x, y)) + 1)
#define round_down(x, y)	((x) & ~__round_mask(x, y))
#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define DIV_ROUND_UP_ULL(ll, d) DIV_ROUND_UP((unsigned long long)(ll), (d))
#define DIV_ROUND_DOWN_ULL(ll, d) ({ unsigned long long _tmp = (ll); _tmp / (d); })
#define roundup(x, y) ({ typeof(y) __y = (y); (((x) + (__y - 1)) / __y) * __y; })
#define rounddown(x, y) ({ typeof(x) __x = (x); __x - (__x % (y)); })
#define DIV_ROUND_CLOSEST(x, divisor) ({ \
	typeof(x) __x = x; typeof(divisor) __d = divisor; \
	(((typeof(x))-1) > 0 || ((typeof(divisor))-1) > 0 || \
	 (((__x) > 0) == ((__d) > 0))) ? \
		(((__x) + ((__d) / 2)) / (__d)) : (((__x) - ((__d) / 2)) / (__d)); })
#define DIV_ROUND_CLOSEST_ULL(x, divisor) ({ \
	typeof(divisor) __d = divisor; \
	unsigned long long _tmp = (x) + (__d) / 2; _tmp / __d; })
#define mult_frac(x, n, d) ({ \
	typeof(x) q = (x) / (d); typeof(x) r = (x) % (d); \
	q * (n) + r * (n) / (d); })
#define sector_div(a, b) do_div(a, b)
#define abs(x) ({ typeof(x) __x = (x); __x < 0 ? -__x : __x; })
#define abs_diff(a, b) ({ typeof(a) __a = (a); typeof(b) __b = (b); \
	__a > __b ? __a - __b : __b - __a; })

static inline u32 reciprocal_scale(u32 val, u32 ep_ro)
{
	return (u32)(((u64)val * ep_ro) >> 32);
}

static inline int int_sqrt(unsigned long x)
{
	unsigned long b, m, y = 0;
	if (x <= 1)
		return x;
	m = 1UL << (BITS_PER_LONG - 2);
	while (m != 0) {
		b = y + m;
		y >>= 1;
		if (x >= b) {
			x -= b;
			y += m;
		}
		m >>= 2;
	}
	return y;
}
#endif
