/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_OVERFLOW_H_
#define _LKPI_LINUX_OVERFLOW_H_
#include <linux/compiler.h>
#include <linux/limits.h>
#include <linux/types.h>

#define check_add_overflow(a, b, d) __builtin_add_overflow(a, b, d)
#define check_sub_overflow(a, b, d) __builtin_sub_overflow(a, b, d)
#define check_mul_overflow(a, b, d) __builtin_mul_overflow(a, b, d)

static inline size_t __must_check size_mul(size_t factor1, size_t factor2)
{
	size_t bytes;
	if (check_mul_overflow(factor1, factor2, &bytes))
		return SIZE_MAX;
	return bytes;
}
static inline size_t __must_check size_add(size_t addend1, size_t addend2)
{
	size_t bytes;
	if (check_add_overflow(addend1, addend2, &bytes))
		return SIZE_MAX;
	return bytes;
}
static inline size_t __must_check size_sub(size_t minuend, size_t subtrahend)
{
	size_t bytes;
	if (minuend == SIZE_MAX || subtrahend == SIZE_MAX ||
	    check_sub_overflow(minuend, subtrahend, &bytes))
		return SIZE_MAX;
	return bytes;
}
#define array_size(a, b)	size_mul(a, b)
#define array3_size(a, b, c)	size_mul(size_mul(a, b), c)
#define flex_array_size(p, member, count) \
	__builtin_choose_expr(__is_constexpr(count), \
		(count) * sizeof(*(p)->member) + __must_be_array((p)->member), \
		size_mul(count, sizeof(*(p)->member) + __must_be_array((p)->member)))
#define struct_size(p, member, count) \
	__builtin_choose_expr(__is_constexpr(count), \
		sizeof(*(p)) + flex_array_size(p, member, count), \
		size_add(sizeof(*(p)), flex_array_size(p, member, count)))
#define struct_size_t(type, member, count) struct_size((type *)NULL, member, count)
#define __must_be_array(a) BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))
#define DEFINE_FLEX(TYPE, NAME, MEMBER, COUNTER, COUNT) \
	union { u8 bytes[struct_size_t(TYPE, MEMBER, COUNT)]; TYPE obj; } NAME##_u = {}; \
	TYPE *NAME = (TYPE *)&NAME##_u
#define DEFINE_RAW_FLEX(TYPE, NAME, MEMBER, COUNT) \
	union { u8 bytes[struct_size_t(TYPE, MEMBER, COUNT)]; TYPE obj; } NAME##_u = {}; \
	TYPE *NAME = (TYPE *)&NAME##_u
#endif
