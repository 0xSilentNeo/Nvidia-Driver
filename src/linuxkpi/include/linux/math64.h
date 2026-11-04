/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_MATH64_H_
#define _LKPI_LINUX_MATH64_H_
#include <linux/types.h>
#include <linux/math.h>

static inline u64 div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}
static inline s64 div_s64_rem(s64 dividend, s32 divisor, s32 *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}
static inline u64 div64_u64_rem(u64 dividend, u64 divisor, u64 *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}
static inline u64 div64_u64(u64 dividend, u64 divisor) { return dividend / divisor; }
static inline s64 div64_s64(s64 dividend, s64 divisor) { return dividend / divisor; }
static inline u64 div_u64(u64 dividend, u32 divisor) { return dividend / divisor; }
static inline s64 div_s64(s64 dividend, s32 divisor) { return dividend / divisor; }
static inline u64 mul_u32_u32(u32 a, u32 b) { return (u64)a * b; }
static inline u64 mul_u64_u32_shr(u64 a, u32 mul, unsigned int shift)
{
	return (u64)(((unsigned __int128)a * mul) >> shift);
}
static inline u64 mul_u64_u64_shr(u64 a, u64 mul, unsigned int shift)
{
	return (u64)(((unsigned __int128)a * mul) >> shift);
}
static inline u64 mul_u64_u32_div(u64 a, u32 mul, u32 divisor)
{
	return (u64)(((unsigned __int128)a * mul) / divisor);
}
#define do_div(n, base) ({ \
	u32 __base = (base); u32 __rem; \
	__rem = ((u64)(n)) % __base; (n) = ((u64)(n)) / __base; __rem; })
#define DIV64_U64_ROUND_UP(ll, d) ({ u64 _tmp = (d); div64_u64((ll) + _tmp - 1, _tmp); })
#define DIV64_U64_ROUND_CLOSEST(dividend, divisor) \
	({ u64 _tmp = (divisor); div64_u64((dividend) + _tmp / 2, _tmp); })
#define DIV_U64_ROUND_CLOSEST(dividend, divisor) \
	({ u32 _tmp = (divisor); div_u64((u64)(dividend) + _tmp / 2, _tmp); })
#endif
