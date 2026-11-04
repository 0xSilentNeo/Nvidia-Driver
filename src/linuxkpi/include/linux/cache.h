/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_CACHE_H_
#define _LKPI_LINUX_CACHE_H_
#include <linux/kernel.h>
#define L1_CACHE_SHIFT	6
#define L1_CACHE_BYTES	(1 << L1_CACHE_SHIFT)
#define SMP_CACHE_BYTES	L1_CACHE_BYTES
#define L1_CACHE_ALIGN(x) __ALIGN_KERNEL(x, L1_CACHE_BYTES)
#define cache_line_size() L1_CACHE_BYTES
#define INTERNODE_CACHE_SHIFT L1_CACHE_SHIFT
#define ____cacheline_internodealigned_in_smp __attribute__((aligned(L1_CACHE_BYTES)))
#endif
