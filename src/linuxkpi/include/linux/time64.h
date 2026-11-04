/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_TIME64_H_
#define _LKPI_LINUX_TIME64_H_
#include <linux/types.h>
typedef s64 time64_t;
typedef u64 timeu64_t;
struct timespec64 {
	time64_t tv_sec;
	long tv_nsec;
};
#define MSEC_PER_SEC	1000L
#define USEC_PER_MSEC	1000L
#define NSEC_PER_USEC	1000L
#define NSEC_PER_MSEC	1000000L
#define USEC_PER_SEC	1000000L
#define NSEC_PER_SEC	1000000000L
#define PSEC_PER_SEC	1000000000000LL
#define FSEC_PER_SEC	1000000000000000LL
#define TIME64_MAX	((s64)~((u64)1 << 63))
static inline s64 timespec64_to_ns(const struct timespec64 *ts)
{
	return ((s64)ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
}
static inline struct timespec64 ns_to_timespec64(s64 nsec)
{
	struct timespec64 ts;
	ts.tv_sec = nsec / NSEC_PER_SEC;
	ts.tv_nsec = nsec % NSEC_PER_SEC;
	return ts;
}
#endif
