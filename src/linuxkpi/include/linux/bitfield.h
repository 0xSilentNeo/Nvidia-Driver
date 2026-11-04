/* SPDX-License-Identifier: MIT */
/*
 * FIELD_{GET,PREP,FIT,MAX} and the le/be/u* encode/get helpers mt76 uses
 * everywhere for register and descriptor packing.
 */
#ifndef _LKPI_LINUX_BITFIELD_H_
#define _LKPI_LINUX_BITFIELD_H_
#include <linux/build_bug.h>
#include <linux/types.h>
#include <asm/byteorder.h>

#define __bf_shf(x) (__builtin_ffsll(x) - 1)

#define __BF_FIELD_CHECK(_mask, _reg, _val, _pfx) \
	({ \
		BUILD_BUG_ON_MSG(!__builtin_constant_p(_mask), _pfx "mask is not constant"); \
		BUILD_BUG_ON_MSG((_mask) == 0, _pfx "mask is zero"); \
		BUILD_BUG_ON_MSG(__builtin_constant_p(_val) ? \
				 ~((_mask) >> __bf_shf(_mask)) & (_val) : 0, \
				 _pfx "value too large for the field"); \
		BUILD_BUG_ON_MSG(__bf_cast_unsigned(_mask, _mask) > \
				 __bf_cast_unsigned(_reg, ~0ull), \
				 _pfx "type of reg too small for mask"); \
		__BUILD_BUG_ON_NOT_POWER_OF_2((_mask) + \
					      (1ULL << __bf_shf(_mask))); \
	})
#define __bf_cast_unsigned(type, x) ((__unsigned_scalar_typeof(type))(x))
#define __unsigned_scalar_typeof(x) typeof( \
	_Generic((x), \
		char: (unsigned char)0, \
		unsigned char: (unsigned char)0, \
		signed char: (unsigned char)0, \
		unsigned short: (unsigned short)0, \
		signed short: (unsigned short)0, \
		unsigned int: (unsigned int)0, \
		signed int: (unsigned int)0, \
		unsigned long: (unsigned long)0, \
		signed long: (unsigned long)0, \
		unsigned long long: (unsigned long long)0, \
		signed long long: (unsigned long long)0, \
		default: (x)))
#define __BUILD_BUG_ON_NOT_POWER_OF_2(n) BUILD_BUG_ON(((n) & ((n) - 1)) != 0)

#define FIELD_MAX(_mask) ({ \
	__BF_FIELD_CHECK(_mask, 0ULL, 0ULL, "FIELD_MAX: "); \
	(typeof(_mask))((_mask) >> __bf_shf(_mask)); })

#define FIELD_FIT(_mask, _val) ({ \
	__BF_FIELD_CHECK(_mask, 0ULL, 0ULL, "FIELD_FIT: "); \
	!((((typeof(_mask))_val) << __bf_shf(_mask)) & ~(_mask)); })

#define FIELD_PREP(_mask, _val) ({ \
	__BF_FIELD_CHECK(_mask, 0ULL, _val, "FIELD_PREP: "); \
	((typeof(_mask))(_val) << __bf_shf(_mask)) & (_mask); })

#define FIELD_GET(_mask, _reg) ({ \
	__BF_FIELD_CHECK(_mask, _reg, 0U, "FIELD_GET: "); \
	(typeof(_mask))(((_reg) & (_mask)) >> __bf_shf(_mask)); })

/* Non-constant-mask variants. */
static inline u64 field_multiplier(u64 field)
{
	return field & -field;
}
static inline u64 field_mask(u64 field)
{
	return field / field_multiplier(field);
}
#define field_max(field) ((typeof(field))field_mask(field))
#define field_prep(_mask, _val) (((_val) * field_multiplier(_mask)) & (_mask))
#define field_get(_mask, _reg) ((typeof(_mask))(((_reg) & (_mask)) / field_multiplier(_mask)))

#define ____MAKE_OP(type, base, to, from) \
static inline __##type type##_encode_bits(base v, base field) \
{ \
	return to((v & field_mask(field)) * field_multiplier(field)); \
} \
static inline __##type type##_replace_bits(__##type old, base val, base field) \
{ \
	return (old & ~to(field)) | type##_encode_bits(val, field); \
} \
static inline void type##p_replace_bits(__##type *p, base val, base field) \
{ \
	*p = (*p & ~to(field)) | type##_encode_bits(val, field); \
} \
static inline base type##_get_bits(__##type v, base field) \
{ \
	return (from(v) & field) / field_multiplier(field); \
}
#define __MAKE_OP(size) \
	____MAKE_OP(le##size, u##size, cpu_to_le##size, le##size##_to_cpu) \
	____MAKE_OP(be##size, u##size, cpu_to_be##size, be##size##_to_cpu) \
	____MAKE_OP(u##size, u##size, , )
____MAKE_OP(u8, u8, , )
__MAKE_OP(16)
__MAKE_OP(32)
__MAKE_OP(64)
#undef __MAKE_OP
#undef ____MAKE_OP
#endif /* _LKPI_LINUX_BITFIELD_H_ */
