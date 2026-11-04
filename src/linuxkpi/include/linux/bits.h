/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_BITS_H_
#define _LKPI_LINUX_BITS_H_
#include <linux/types.h>

#define BIT(nr)			(1UL << (nr))
#define BIT_ULL(nr)		(1ULL << (nr))
#define BIT_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BIT_ULL_MASK(nr)	(1ULL << ((nr) % BITS_PER_LONG_LONG))
#define BIT_ULL_WORD(nr)	((nr) / BITS_PER_LONG_LONG)

#define __GENMASK(h, l) \
	(((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK(h, l)		__GENMASK(h, l)
#define __GENMASK_ULL(h, l) \
	(((~0ULL) - (1ULL << (l)) + 1) & (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))
#define GENMASK_ULL(h, l)	__GENMASK_ULL(h, l)
#define GENMASK_U8(h, l)	((u8)GENMASK(h, l))
#define GENMASK_U16(h, l)	((u16)GENMASK(h, l))
#define GENMASK_U32(h, l)	((u32)GENMASK(h, l))
#define GENMASK_U64(h, l)	GENMASK_ULL(h, l)
#endif
