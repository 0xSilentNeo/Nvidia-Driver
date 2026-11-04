/* SPDX-License-Identifier: MIT */
/*
 * jiffies is a real variable because the driver also uses the word as a
 * struct member name (struct mt76_tx_cb).  The backend advances it from a
 * 10 ms periodic timer (HZ = 100, the same rate FreeBSD's ticks run at), so
 * every arithmetic helper converts explicitly.
 */
#ifndef _LKPI_LINUX_JIFFIES_H_
#define _LKPI_LINUX_JIFFIES_H_
#include <linux/types.h>
#include <linux/limits.h>
#include <linux/typecheck.h>
#include <lkpi/os.h>

#define HZ		100
#define USER_HZ		100
#define CLOCK_TICK_RATE	HZ
#define MAX_JIFFY_OFFSET ((LONG_MAX >> 1) - 1)
#define INITIAL_JIFFIES	0UL

extern volatile unsigned long jiffies;
extern volatile u64 jiffies_64;
void lkpi_jiffies_tick(void);		/* called by the backend every 1000/HZ ms */
static inline u64 get_jiffies_64(void) { return jiffies_64; }

#define time_after(a, b) \
	(typecheck(unsigned long, a) && typecheck(unsigned long, b) && ((long)((b) - (a)) < 0))
#define time_before(a, b)	time_after(b, a)
#define time_after_eq(a, b) \
	(typecheck(unsigned long, a) && typecheck(unsigned long, b) && ((long)((a) - (b)) >= 0))
#define time_before_eq(a, b)	time_after_eq(b, a)
#define time_in_range(a, b, c)	(time_after_eq(a, b) && time_before_eq(a, c))
#define time_after64(a, b)	((s64)((b) - (a)) < 0)
#define time_before64(a, b)	time_after64(b, a)
#define time_after_eq64(a, b)	((s64)((a) - (b)) >= 0)
#define time_before_eq64(a, b)	time_after_eq64(b, a)
#define time_after32(a, b)	((s32)((u32)(b) - (u32)(a)) < 0)
#define time_before32(a, b)	time_after32(b, a)
#define time_is_before_jiffies(a)	time_after(jiffies, a)
#define time_is_after_jiffies(a)	time_before(jiffies, a)
#define time_is_before_eq_jiffies(a)	time_after_eq(jiffies, a)
#define time_is_after_eq_jiffies(a)	time_before_eq(jiffies, a)

#define MSEC_PER_JIFFY	(1000 / HZ)
static inline unsigned int jiffies_to_msecs(const unsigned long j) { return (unsigned int)(j * MSEC_PER_JIFFY); }
static inline unsigned int jiffies_to_usecs(const unsigned long j) { return (unsigned int)(j * MSEC_PER_JIFFY * 1000); }
static inline u64 jiffies_to_nsecs(const unsigned long j) { return (u64)j * MSEC_PER_JIFFY * 1000000ULL; }
static inline unsigned long msecs_to_jiffies(const unsigned int m)
{
	if (m > (unsigned int)MAX_JIFFY_OFFSET)
		return MAX_JIFFY_OFFSET;
	return (m + MSEC_PER_JIFFY - 1) / MSEC_PER_JIFFY;	/* round up like Linux */
}
static inline unsigned long usecs_to_jiffies(const unsigned int u) { return (u + MSEC_PER_JIFFY * 1000 - 1) / (MSEC_PER_JIFFY * 1000); }
static inline unsigned long nsecs_to_jiffies(u64 n) { return (unsigned long)(n / (MSEC_PER_JIFFY * 1000000ULL)); }
static inline unsigned long secs_to_jiffies(unsigned long s) { return s * HZ; }
static inline u64 jiffies64_to_nsecs(u64 j) { return j * MSEC_PER_JIFFY * 1000000ULL; }
static inline u64 nsecs_to_jiffies64(u64 n) { return n / (MSEC_PER_JIFFY * 1000000ULL); }
static inline unsigned long round_jiffies(unsigned long j) { return j; }
static inline unsigned long round_jiffies_relative(unsigned long j) { return j; }
static inline unsigned long round_jiffies_up(unsigned long j) { return j; }
#endif
