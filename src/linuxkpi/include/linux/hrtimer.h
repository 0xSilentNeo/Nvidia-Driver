/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_HRTIMER_H_
#define _LKPI_LINUX_HRTIMER_H_
#include <linux/ktime.h>
#include <linux/timer.h>
enum hrtimer_mode {
	HRTIMER_MODE_ABS = 0x00,
	HRTIMER_MODE_REL = 0x01,
	HRTIMER_MODE_PINNED = 0x02,
	HRTIMER_MODE_SOFT = 0x04,
	HRTIMER_MODE_HARD = 0x08,
	HRTIMER_MODE_ABS_PINNED = HRTIMER_MODE_ABS | HRTIMER_MODE_PINNED,
	HRTIMER_MODE_REL_PINNED = HRTIMER_MODE_REL | HRTIMER_MODE_PINNED,
	HRTIMER_MODE_REL_SOFT = HRTIMER_MODE_REL | HRTIMER_MODE_SOFT,
};
enum hrtimer_restart { HRTIMER_NORESTART, HRTIMER_RESTART };
enum { CLOCK_REALTIME = 0, CLOCK_MONOTONIC = 1, CLOCK_BOOTTIME = 7 };
typedef int clockid_t_lkpi;
struct hrtimer {
	enum hrtimer_restart (*function)(struct hrtimer *);
	struct timer_list timer;
	ktime_t expires;
	int clock;
	enum hrtimer_mode mode;
};
void hrtimer_setup(struct hrtimer *t, enum hrtimer_restart (*fn)(struct hrtimer *), int clock, enum hrtimer_mode mode);
static inline void hrtimer_init(struct hrtimer *t, int clock, enum hrtimer_mode mode) { hrtimer_setup(t, NULL, clock, mode); }
void hrtimer_start(struct hrtimer *t, ktime_t tim, const enum hrtimer_mode mode);
#define hrtimer_start_range_ns(t, tim, delta, mode) hrtimer_start(t, tim, mode)
int hrtimer_cancel(struct hrtimer *t);
int hrtimer_try_to_cancel(struct hrtimer *t);
bool hrtimer_active(const struct hrtimer *t);
#define hrtimer_is_queued(t) hrtimer_active(t)
static inline void hrtimer_set_expires(struct hrtimer *t, ktime_t time) { t->expires = time; }
static inline ktime_t hrtimer_get_expires(const struct hrtimer *t) { return t->expires; }
u64 hrtimer_forward(struct hrtimer *t, ktime_t now, ktime_t interval);
static inline u64 hrtimer_forward_now(struct hrtimer *t, ktime_t interval) { return hrtimer_forward(t, ktime_get(), interval); }
#endif
