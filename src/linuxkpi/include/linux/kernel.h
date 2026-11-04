/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_KERNEL_H_
#define _LKPI_LINUX_KERNEL_H_
#include <stdarg.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/stddef.h>
#include <linux/limits.h>
#include <linux/array_size.h>
#include <linux/build_bug.h>
#include <linux/container_of.h>
#include <linux/stringify.h>
#include <linux/typecheck.h>
#include <linux/minmax.h>
#include <linux/math.h>
#include <linux/math64.h>
#include <linux/log2.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kconfig.h>
#include <linux/export.h>
#include <linux/printk.h>
#include <linux/bug.h>
#include <linux/kstrtox.h>
#include <linux/hex.h>
#include <linux/wordpart.h>
#include <linux/ctype.h>
#include <linux/numa.h>
#include <linux/instruction_pointer.h>
#include <asm/byteorder.h>

#define ALIGN(x, a)		__ALIGN_KERNEL((x), (a))
#define ALIGN_DOWN(x, a)	__ALIGN_KERNEL((x) - ((a) - 1), (a))
#define __ALIGN_KERNEL(x, a)	__ALIGN_KERNEL_MASK(x, (typeof(x))(a) - 1)
#define __ALIGN_KERNEL_MASK(x, mask) (((x) + (mask)) & ~(mask))
#define __ALIGN_MASK(x, mask)	__ALIGN_KERNEL_MASK((x), (mask))
#define PTR_ALIGN(p, a)		((typeof(p))ALIGN((unsigned long)(p), (a)))
#define PTR_ALIGN_DOWN(p, a)	((typeof(p))ALIGN_DOWN((unsigned long)(p), (a)))
#define IS_ALIGNED(x, a)	(((x) & ((typeof(x))(a) - 1)) == 0)

#define upper_32_bits(n)	((u32)(((n) >> 16) >> 16))
#define lower_32_bits(n)	((u32)((n) & 0xffffffff))
#define upper_16_bits(n)	((u16)((n) >> 16))
#define lower_16_bits(n)	((u16)((n) & 0xffff))

#define might_sleep()		do { } while (0)
#define might_sleep_if(c)	do { } while (0)
#define might_fault()		do { } while (0)
#define cant_sleep()		do { } while (0)
#define sched_annotate_sleep()	do { } while (0)
#define non_block_start()	do { } while (0)
#define non_block_end()		do { } while (0)

#define VERIFY_OCTAL_PERMISSIONS(perms) (perms)

/* Used by driver code to find the enclosing object from a member pointer. */
#define container_of_const(ptr, type, member) container_of(ptr, type, member)

#define REPEAT_BYTE(x)	((~0ul / 0xff) * (x))

#define TAINT_WARN 9
#define add_taint(flag, lockdep_ok) do { } while (0)

#define SI_PREFIX_KILO 1000
#define KILO 1000
#define MEGA 1000000
#define GIGA 1000000000

#define __DISABLE_LKPI_HOST_WARNINGS \
	_Pragma("clang diagnostic ignored \"-Wgnu\"")

/* snprintf / scnprintf / sprintf: libkern provides snprintf and vsnprintf. */
#include <linux/sprintf.h>

#endif /* _LKPI_LINUX_KERNEL_H_ */
