/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_KTIME_H_
#define _LKPI_LINUX_KTIME_H_
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/time64.h>
#include <lkpi/os.h>

#define KTIME_MAX	((s64)~((u64)1 << 63))
#define KTIME_MIN	(-KTIME_MAX - 1)
#define KTIME_SEC_MAX	(KTIME_MAX / NSEC_PER_SEC)

static inline ktime_t ktime_set(const s64 secs, const unsigned long nsecs)
{
	return secs * NSEC_PER_SEC + (s64)nsecs;
}
#define ktime_sub(lhs, rhs)	((lhs) - (rhs))
#define ktime_add(lhs, rhs)	((lhs) + (rhs))
#define ktime_add_unsafe(l, r)	((l) + (r))
#define ktime_add_ns(kt, nsval)	((kt) + (nsval))
#define ktime_sub_ns(kt, nsval)	((kt) - (nsval))
static inline ktime_t ktime_add_us(const ktime_t kt, const u64 usec) { return kt + usec * NSEC_PER_USEC; }
static inline ktime_t ktime_add_ms(const ktime_t kt, const u64 msec) { return kt + msec * NSEC_PER_MSEC; }
static inline ktime_t ktime_sub_us(const ktime_t kt, const u64 usec) { return kt - usec * NSEC_PER_USEC; }
static inline ktime_t ktime_sub_ms(const ktime_t kt, const u64 msec) { return kt - msec * NSEC_PER_MSEC; }
static inline ktime_t ns_to_ktime(u64 ns) { return (ktime_t)ns; }
static inline ktime_t us_to_ktime(u64 us) { return (ktime_t)(us * NSEC_PER_USEC); }
static inline ktime_t ms_to_ktime(u64 ms) { return (ktime_t)(ms * NSEC_PER_MSEC); }
static inline s64 ktime_to_ns(const ktime_t kt) { return kt; }
static inline s64 ktime_to_us(const ktime_t kt) { return kt / NSEC_PER_USEC; }
static inline s64 ktime_to_ms(const ktime_t kt) { return kt / NSEC_PER_MSEC; }
static inline s64 ktime_divns(const ktime_t kt, s64 div) { return kt / div; }
static inline int ktime_compare(const ktime_t a, const ktime_t b) { return a < b ? -1 : a > b ? 1 : 0; }
static inline bool ktime_after(const ktime_t a, const ktime_t b) { return ktime_compare(a, b) > 0; }
static inline bool ktime_before(const ktime_t a, const ktime_t b) { return ktime_compare(a, b) < 0; }
static inline s64 ktime_us_delta(const ktime_t later, const ktime_t earlier) { return ktime_to_us(ktime_sub(later, earlier)); }
static inline s64 ktime_ms_delta(const ktime_t later, const ktime_t earlier) { return ktime_to_ms(ktime_sub(later, earlier)); }
static inline bool ktime_to_timespec64_cond(const ktime_t kt, struct timespec64 *ts)
{
	if (kt) {
		ts->tv_sec = kt / NSEC_PER_SEC;
		ts->tv_nsec = kt % NSEC_PER_SEC;
		return true;
	}
	return false;
}
static inline struct timespec64 ktime_to_timespec64(ktime_t kt)
{
	struct timespec64 ts;
	ts.tv_sec = kt / NSEC_PER_SEC;
	ts.tv_nsec = kt % NSEC_PER_SEC;
	return ts;
}
static inline ktime_t timespec64_to_ktime(struct timespec64 ts) { return ktime_set(ts.tv_sec, ts.tv_nsec); }

static inline ktime_t ktime_get(void) { return (ktime_t)lkpi_os_uptime_ns(); }
static inline ktime_t ktime_get_boottime(void) { return (ktime_t)lkpi_os_boottime_ns(); }
static inline ktime_t ktime_get_real(void) { return (ktime_t)lkpi_os_realtime_ns(); }
static inline ktime_t ktime_get_coarse(void) { return ktime_get(); }
static inline ktime_t ktime_get_raw(void) { return ktime_get(); }
static inline u64 ktime_get_ns(void) { return lkpi_os_uptime_ns(); }
static inline u64 ktime_get_boottime_ns(void) { return lkpi_os_boottime_ns(); }
static inline u64 ktime_get_real_ns(void) { return lkpi_os_realtime_ns(); }
static inline u64 ktime_get_raw_ns(void) { return lkpi_os_uptime_ns(); }
static inline u64 ktime_get_boottime_seconds(void) { return lkpi_os_boottime_ns() / NSEC_PER_SEC; }
static inline time64_t ktime_get_real_seconds(void) { return lkpi_os_realtime_ns() / NSEC_PER_SEC; }
static inline time64_t ktime_get_seconds(void) { return lkpi_os_uptime_ns() / NSEC_PER_SEC; }
static inline u64 ktime_get_coarse_boottime_ns(void) { return lkpi_os_boottime_ns(); }
static inline u64 ktime_get_coarse_ns(void) { return lkpi_os_uptime_ns(); }
static inline u64 ktime_get_mono_fast_ns(void) { return lkpi_os_uptime_ns(); }
static inline void ktime_get_ts64(struct timespec64 *ts) { *ts = ktime_to_timespec64(ktime_get()); }
static inline void ktime_get_real_ts64(struct timespec64 *ts) { *ts = ktime_to_timespec64(ktime_get_real()); }
static inline void ktime_get_boottime_ts64(struct timespec64 *ts) { *ts = ktime_to_timespec64(ktime_get_boottime()); }
static inline void ktime_get_coarse_real_ts64(struct timespec64 *ts) { ktime_get_real_ts64(ts); }
#define ktime_get_coarse_ts64(ts) ktime_get_ts64(ts)
#define do_gettimeofday(tv) do { } while (0)
static inline u64 local_clock(void) { return lkpi_os_uptime_ns(); }
static inline u64 sched_clock(void) { return lkpi_os_uptime_ns(); }
#endif
