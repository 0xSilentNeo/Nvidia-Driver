/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_ASM_BYTEORDER_H_
#define _LKPI_ASM_BYTEORDER_H_
#include <linux/types.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "only little-endian hosts are supported (x86-64, arm64)"
#endif
#define __LITTLE_ENDIAN 1234
#define __LITTLE_ENDIAN_BITFIELD
#define __BIG_ENDIAN 4321

#define ___constant_swab16(x) ((u16)( \
	(((u16)(x) & (u16)0x00ffU) << 8) | \
	(((u16)(x) & (u16)0xff00U) >> 8)))
#define ___constant_swab32(x) ((u32)( \
	(((u32)(x) & (u32)0x000000ffUL) << 24) | \
	(((u32)(x) & (u32)0x0000ff00UL) <<  8) | \
	(((u32)(x) & (u32)0x00ff0000UL) >>  8) | \
	(((u32)(x) & (u32)0xff000000UL) >> 24)))
#define ___constant_swab64(x) ((u64)( \
	(((u64)(x) & (u64)0x00000000000000ffULL) << 56) | \
	(((u64)(x) & (u64)0x000000000000ff00ULL) << 40) | \
	(((u64)(x) & (u64)0x0000000000ff0000ULL) << 24) | \
	(((u64)(x) & (u64)0x00000000ff000000ULL) <<  8) | \
	(((u64)(x) & (u64)0x000000ff00000000ULL) >>  8) | \
	(((u64)(x) & (u64)0x0000ff0000000000ULL) >> 24) | \
	(((u64)(x) & (u64)0x00ff000000000000ULL) >> 40) | \
	(((u64)(x) & (u64)0xff00000000000000ULL) >> 56)))

static inline u16 __swab16(u16 x) { return __builtin_bswap16(x); }
static inline u32 __swab32(u32 x) { return __builtin_bswap32(x); }
static inline u64 __swab64(u64 x) { return __builtin_bswap64(x); }
#define swab16(x) (__builtin_constant_p((u16)(x)) ? ___constant_swab16(x) : __swab16(x))
#define swab32(x) (__builtin_constant_p((u32)(x)) ? ___constant_swab32(x) : __swab32(x))
#define swab64(x) (__builtin_constant_p((u64)(x)) ? ___constant_swab64(x) : __swab64(x))

#define __cpu_to_le16(x) ((__le16)(u16)(x))
#define __le16_to_cpu(x) ((u16)(__le16)(x))
#define __cpu_to_le32(x) ((__le32)(u32)(x))
#define __le32_to_cpu(x) ((u32)(__le32)(x))
#define __cpu_to_le64(x) ((__le64)(u64)(x))
#define __le64_to_cpu(x) ((u64)(__le64)(x))
#define __cpu_to_be16(x) ((__be16)swab16(x))
#define __be16_to_cpu(x) swab16((u16)(__be16)(x))
#define __cpu_to_be32(x) ((__be32)swab32(x))
#define __be32_to_cpu(x) swab32((u32)(__be32)(x))
#define __cpu_to_be64(x) ((__be64)swab64(x))
#define __be64_to_cpu(x) swab64((u64)(__be64)(x))
#define __constant_cpu_to_le16(x) ((__le16)(u16)(x))
#define __constant_le16_to_cpu(x) ((u16)(__le16)(x))
#define __constant_cpu_to_le32(x) ((__le32)(u32)(x))
#define __constant_le32_to_cpu(x) ((u32)(__le32)(x))
#define __constant_cpu_to_be16(x) ((__be16)___constant_swab16(x))
#define __constant_be16_to_cpu(x) ___constant_swab16((u16)(__be16)(x))
#define __constant_cpu_to_be32(x) ((__be32)___constant_swab32(x))
#define __constant_be32_to_cpu(x) ___constant_swab32((u32)(__be32)(x))
#define __constant_cpu_to_be64(x) ((__be64)___constant_swab64(x))
#define __constant_be64_to_cpu(x) ___constant_swab64((u64)(__be64)(x))

#define cpu_to_le16 __cpu_to_le16
#define le16_to_cpu __le16_to_cpu
#define cpu_to_le32 __cpu_to_le32
#define le32_to_cpu __le32_to_cpu
#define cpu_to_le64 __cpu_to_le64
#define le64_to_cpu __le64_to_cpu
#define cpu_to_be16 __cpu_to_be16
#define be16_to_cpu __be16_to_cpu
#define cpu_to_be32 __cpu_to_be32
#define be32_to_cpu __be32_to_cpu
#define cpu_to_be64 __cpu_to_be64
#define be64_to_cpu __be64_to_cpu

static inline u16 le16_to_cpup(const __le16 *p) { return le16_to_cpu(*p); }
static inline u32 le32_to_cpup(const __le32 *p) { return le32_to_cpu(*p); }
static inline u64 le64_to_cpup(const __le64 *p) { return le64_to_cpu(*p); }
static inline u16 be16_to_cpup(const __be16 *p) { return be16_to_cpu(*p); }
static inline u32 be32_to_cpup(const __be32 *p) { return be32_to_cpu(*p); }
static inline u64 be64_to_cpup(const __be64 *p) { return be64_to_cpu(*p); }
#define cpu_to_le16p(p) cpu_to_le16(*(p))
#define cpu_to_le32p(p) cpu_to_le32(*(p))
#define cpu_to_be16p(p) cpu_to_be16(*(p))
#define cpu_to_be32p(p) cpu_to_be32(*(p))
static inline void le16_add_cpu(__le16 *var, u16 val) { *var = cpu_to_le16(le16_to_cpu(*var) + val); }
static inline void le32_add_cpu(__le32 *var, u32 val) { *var = cpu_to_le32(le32_to_cpu(*var) + val); }
static inline void le64_add_cpu(__le64 *var, u64 val) { *var = cpu_to_le64(le64_to_cpu(*var) + val); }
static inline void be16_add_cpu(__be16 *var, u16 val) { *var = cpu_to_be16(be16_to_cpu(*var) + val); }
static inline void be32_add_cpu(__be32 *var, u32 val) { *var = cpu_to_be32(be32_to_cpu(*var) + val); }
static inline void cpu_to_le32_array(u32 *buf, unsigned int words) { (void)buf; (void)words; }
static inline void le32_to_cpu_array(u32 *buf, unsigned int words) { (void)buf; (void)words; }
#define htons(x) cpu_to_be16(x)
#define ntohs(x) be16_to_cpu(x)
#define htonl(x) cpu_to_be32(x)
#define ntohl(x) be32_to_cpu(x)
#endif /* _LKPI_ASM_BYTEORDER_H_ */
