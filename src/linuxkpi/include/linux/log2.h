/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_LOG2_H_
#define _LKPI_LINUX_LOG2_H_
#include <linux/types.h>
#include <linux/bitops.h>

static inline __attribute_const__ bool is_power_of_2(unsigned long n)
{
	return n != 0 && ((n & (n - 1)) == 0);
}
static inline unsigned long __roundup_pow_of_two(unsigned long n)
{
	return 1UL << fls_long(n - 1);
}
static inline unsigned long __rounddown_pow_of_two(unsigned long n)
{
	return 1UL << (fls_long(n) - 1);
}
#define ilog2(n) ( \
	__builtin_constant_p(n) ? \
		((n) < 2 ? 0 : 63 - __builtin_clzll(n)) : \
	(sizeof(n) <= 4) ? fls((u32)(n)) - 1 : fls64((u64)(n)) - 1)
#define roundup_pow_of_two(n) ( \
	(__builtin_constant_p(n) && ((n) == 1)) ? 1 : __roundup_pow_of_two(n))
#define rounddown_pow_of_two(n) __rounddown_pow_of_two(n)
#define order_base_2(n) ({ unsigned long __n = (n); __n > 1 ? ilog2(__n - 1) + 1 : 0; })
#define bits_per(n) ({ unsigned long __n = (n); __n == 0 ? 1 : ilog2(__n) + 1; })
static inline int get_order(unsigned long size)
{
	int order;
	size = (size - 1) >> 12; /* PAGE_SHIFT */
	order = 0;
	while (size) {
		order++;
		size >>= 1;
	}
	return order;
}
#endif
