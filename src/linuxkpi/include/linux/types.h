/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_TYPES_H_
#define _LKPI_LINUX_TYPES_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <linux/compiler.h>
#include <lkpi/bsd.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef u8  __u8;
typedef u16 __u16;
typedef u32 __u32;
typedef u64 __u64;
typedef s8  __s8;
typedef s16 __s16;
typedef s32 __s32;
typedef s64 __s64;

typedef u16 __bitwise __le16;
typedef u16 __bitwise __be16;
typedef u32 __bitwise __le32;
typedef u32 __bitwise __be32;
typedef u64 __bitwise __le64;
typedef u64 __bitwise __be64;
typedef u16 __bitwise __sum16;
typedef u32 __bitwise __wsum;

typedef u64 dma_addr_t;
typedef u64 phys_addr_t;
typedef u64 resource_size_t;
typedef unsigned long kernel_ulong_t;
typedef unsigned int gfp_t;
typedef unsigned int fmode_t;
typedef int64_t ktime_t;
typedef long __kernel_long_t;
typedef unsigned long __kernel_ulong_t;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned char uchar;
typedef unsigned short ushort;

#ifndef __APPLE__
/* XNU's sys/types.h already defines these. */
#ifndef _LKPI_HAVE_LOFF_T
typedef int64_t loff_t;
#endif
#endif
#ifdef __APPLE__
typedef int64_t loff_t;
#endif

typedef struct {
	int counter;
} atomic_t;

typedef struct {
	int64_t counter;
} atomic64_t;
typedef atomic64_t atomic_long_t;

struct list_head {
	struct list_head *next, *prev;
};

struct hlist_head {
	struct hlist_node *first;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};

struct callback_head {
	struct callback_head *next;
	void (*func)(struct callback_head *head);
};
#define rcu_head callback_head

typedef void (*rcu_callback_t)(struct rcu_head *head);

#define DECLARE_BITMAP(name, bits) \
	unsigned long name[BITS_TO_LONGS(bits)]

#define BITS_PER_LONG		(8 * sizeof(long))
#define BITS_PER_LONG_LONG	64
#define BITS_PER_BYTE		8
#define BITS_PER_TYPE(t)	(sizeof(t) * BITS_PER_BYTE)
#define BITS_TO_LONGS(n)	(((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define BITS_TO_U64(n)		(((n) + 63) / 64)
#define BITS_TO_U32(n)		(((n) + 31) / 32)
#define BITS_TO_BYTES(n)	(((n) + 7) / 8)

#endif /* _LKPI_LINUX_TYPES_H_ */
