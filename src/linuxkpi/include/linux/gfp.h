/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_GFP_H_
#define _LKPI_LINUX_GFP_H_
#include <linux/types.h>
/* Only the bits we inspect: __GFP_ZERO and whether sleeping is allowed. */
#define __GFP_ZERO	0x100u
#define __GFP_NOWARN	0x200u
#define __GFP_NORETRY	0x400u
#define __GFP_NOFAIL	0x800u
#define __GFP_HIGH	0x20u
#define __GFP_ATOMIC	0x80000u
#define __GFP_DMA	0x01u
#define __GFP_DMA32	0x04u
#define __GFP_COMP	0x40000u
#define __GFP_RECLAIM	0x10u
#define __GFP_DIRECT_RECLAIM 0x10u
#define __GFP_KSWAPD_RECLAIM 0x20000u
#define __GFP_IO	0x40u
#define __GFP_FS	0x80u
#define __GFP_MEMALLOC	0x20000u
#define GFP_NOWAIT	(__GFP_KSWAPD_RECLAIM)
#define GFP_ATOMIC	(__GFP_HIGH | __GFP_ATOMIC | __GFP_KSWAPD_RECLAIM)
#define GFP_KERNEL	(__GFP_RECLAIM | __GFP_IO | __GFP_FS)
#define GFP_NOIO	(__GFP_RECLAIM)
#define GFP_NOFS	(__GFP_RECLAIM | __GFP_IO)
#define GFP_USER	GFP_KERNEL
#define GFP_DMA		__GFP_DMA
#define GFP_DMA32	__GFP_DMA32
#define GFP_HIGHUSER	GFP_USER
#define GFP_TRANSHUGE	GFP_KERNEL
#define gfpflags_allow_blocking(f) (((f) & __GFP_DIRECT_RECLAIM) != 0)
#endif
