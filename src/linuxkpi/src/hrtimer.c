// SPDX-License-Identifier: MIT
/*
 * hrtimer on top of timer.c.
 *
 * There is no high-resolution timer in this port: the expiry is rounded up
 * to the next jiffy (10 ms) and handed to a struct timer_list.  All three
 * clocks (MONOTONIC, BOOTTIME, REALTIME) are supported for computing the
 * relative delay; once armed, the timer runs on the uptime clock like every
 * other timer.  HRTIMER_RESTART is honoured: the callback is expected to
 * have moved ->expires forward (hrtimer_forward) and the timer is re-armed
 * for the new value.
 */
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>

static ktime_t lkpi_hrtimer_now(const struct hrtimer *t)
{
	switch (t->clock) {
	case CLOCK_REALTIME:
		return ktime_get_real();
	case CLOCK_BOOTTIME:
		return ktime_get_boottime();
	default:
		return ktime_get();
	}
}

static void lkpi_hrtimer_arm(struct hrtimer *t)
{
	ktime_t now = lkpi_hrtimer_now(t);
	s64 delta = ktime_to_ns(ktime_sub(t->expires, now));
	unsigned long j;

	if (delta <= 0)
		j = 0;
	else
		j = (unsigned long)DIV_ROUND_UP_ULL((u64)delta, NSEC_PER_MSEC * MSEC_PER_JIFFY);
	if (j > MAX_JIFFY_OFFSET)
		j = MAX_JIFFY_OFFSET;
	mod_timer(&t->timer, jiffies + j);
}

static void lkpi_hrtimer_fire(struct timer_list *tl)
{
	struct hrtimer *t = from_timer(t, tl, timer);
	enum hrtimer_restart r = HRTIMER_NORESTART;

	if (t->function)
		r = t->function(t);
	if (r == HRTIMER_RESTART)
		lkpi_hrtimer_arm(t);
}

void hrtimer_setup(struct hrtimer *t, enum hrtimer_restart (*fn)(struct hrtimer *),
		   int clock, enum hrtimer_mode mode)
{
	t->function = fn;
	t->clock = clock;
	t->mode = mode;
	t->expires = 0;
	timer_setup(&t->timer, lkpi_hrtimer_fire, 0);
}

void hrtimer_start(struct hrtimer *t, ktime_t tim, const enum hrtimer_mode mode)
{
	if (mode & HRTIMER_MODE_REL)
		tim = ktime_add(tim, lkpi_hrtimer_now(t));
	t->expires = tim;
	lkpi_hrtimer_arm(t);
}

int hrtimer_cancel(struct hrtimer *t)
{
	return timer_delete_sync(&t->timer);
}

int hrtimer_try_to_cancel(struct hrtimer *t)
{
	/* Linux returns -1 while the callback runs; we cannot tell, so a
	 * running callback reads as "was not active" (0). */
	return timer_delete(&t->timer);
}

bool hrtimer_active(const struct hrtimer *t)
{
	return timer_pending(&t->timer) != 0;
}

u64 hrtimer_forward(struct hrtimer *t, ktime_t now, ktime_t interval)
{
	u64 orun = 1;
	ktime_t delta;

	delta = ktime_sub(now, t->expires);
	if (delta < 0)
		return 0;

	if (interval < (ktime_t)(NSEC_PER_MSEC * MSEC_PER_JIFFY))
		interval = NSEC_PER_MSEC * MSEC_PER_JIFFY;	/* our resolution */

	if (unlikely(delta >= interval)) {
		s64 incr = ktime_to_ns(interval);

		orun = (u64)ktime_divns(delta, incr);
		t->expires = ktime_add_ns(t->expires, incr * (s64)orun);
		if (t->expires > now)
			return orun;
		orun++;
	}
	t->expires = ktime_add(t->expires, interval);
	return orun;
}
