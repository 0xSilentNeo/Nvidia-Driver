/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_RANDOM_H_
#define _LKPI_LINUX_RANDOM_H_
#include <linux/types.h>
#include <lkpi/os.h>
static inline void get_random_bytes(void *buf, size_t len) { lkpi_os_random_bytes(buf, len); }
static inline u32 get_random_u32(void) { u32 v; lkpi_os_random_bytes(&v, sizeof(v)); return v; }
static inline u64 get_random_u64(void) { u64 v; lkpi_os_random_bytes(&v, sizeof(v)); return v; }
static inline u16 get_random_u16(void) { u16 v; lkpi_os_random_bytes(&v, sizeof(v)); return v; }
static inline u8 get_random_u8(void) { u8 v; lkpi_os_random_bytes(&v, sizeof(v)); return v; }
static inline u32 get_random_u32_below(u32 ceil) { return ceil ? get_random_u32() % ceil : 0; }
static inline u32 get_random_u32_inclusive(u32 lo, u32 hi) { return lo + get_random_u32_below(hi - lo + 1); }
#define get_random_int() get_random_u32()
#define get_random_long() ((unsigned long)get_random_u64())
#define prandom_u32() get_random_u32()
#define prandom_u32_max(c) get_random_u32_below(c)
#define prandom_bytes(b, l) get_random_bytes(b, l)
#endif
