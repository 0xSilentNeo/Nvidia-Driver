// SPDX-License-Identifier: MIT
/*
 * struct timer_list on top of one-shot backend timers.
 *
 * The backend object (lkpi_os_timer_t, an IOTimerEventSource in the kext) is
 * created lazily on the first timer_setup()/mod_timer() so that timers which
 * are embedded in driver structures and never armed cost nothing.  One
 * global spinlock serialises the pending/expires/function transitions of all
 * timers; it plays the role of the Linux timer base lock and is never held
 * while sleeping, so it may be taken from the backend callback as well.
 *
 * Linux semantics preserved:
 *  - the timer is not pending while its callback runs, and the callback may
 *    re-arm it with mod_timer();
 *  - mod_timer() returns 1 if the timer was pending, 0 otherwise;
 *  - timer_delete_sync() waits for a running callback;
 *  - timer_shutdown_sync() additionally clears ->function so that later
 *    mod_timer() calls become no-ops (exactly what upstream does).
 *
 * `expires` is in jiffies; it is converted to an absolute uptime deadline
 * relative to the current jiffies value, so a timer armed for jiffies+N
 * fires about N*10 ms from now.  Sub-tick precision is not available.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <lkpi/os.h>

static DEFINE_SPINLOCK(lkpi_timer_lock);

static void lkpi_timer_fire(void *arg)
{
	struct timer_list *t = arg;
	void (*fn)(struct timer_list *);

	spin_lock(&lkpi_timer_lock);
	/* Not pending inside the callback; it may re-arm itself. */
	t->pending = 0;
	fn = t->function;
	spin_unlock(&lkpi_timer_lock);

	if (fn)
		fn(t);
}

/*
 * Returns the backend timer, creating it on first use.  Creation may sleep
 * (the kext attaches the event source to a workloop), so this is called
 * before lkpi_timer_lock is taken; two racing first users agree through a
 * compare-exchange and the loser's object is destroyed again.
 */
static lkpi_os_timer_t lkpi_timer_get(struct timer_list *t)
{
	lkpi_os_timer_t os, expected = NULL;

	os = __atomic_load_n(&t->os, __ATOMIC_ACQUIRE);
	if (likely(os))
		return os;

	os = lkpi_os_timer_create(lkpi_timer_fire, t);
	if (!os)
		lkpi_os_panic("lkpi: cannot allocate timer backing object");
	if (__atomic_compare_exchange_n(&t->os, &expected, os, false,
				       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return os;
	lkpi_os_timer_destroy(os);
	return expected;
}

static uint64_t lkpi_timer_deadline(unsigned long expires)
{
	unsigned long now = jiffies;
	long delta = (long)(expires - now);

	if (delta < 0)
		delta = 0;
	return lkpi_os_uptime_ns() + jiffies_to_nsecs((unsigned long)delta);
}

void __lkpi_timer_setup(struct timer_list *t, void (*fn)(struct timer_list *),
			unsigned int flags)
{
	bool fresh;

	/* Only trust a backend pointer in a struct we initialised before;
	 * on-stack timers arrive with garbage in every field. */
	if (t->magic != LKPI_TIMER_MAGIC) {
		t->os = NULL;
		t->pending = 0;
		t->magic = LKPI_TIMER_MAGIC;
	}
	fresh = !__atomic_load_n(&t->os, __ATOMIC_ACQUIRE);

	(void)lkpi_timer_get(t);
	spin_lock(&lkpi_timer_lock);
	t->function = fn;
	t->flags = flags;
	t->expires = 0;
	if (fresh)
		t->pending = 0;	/* zero-initialised or re-setup: start clean */
	spin_unlock(&lkpi_timer_lock);
}

/* Arms the timer.  Returns the previous pending state. */
static int lkpi_timer_arm(struct timer_list *t, unsigned long expires, bool only_if_pending)
{
	lkpi_os_timer_t os;
	int was_pending;

	if (!READ_ONCE(t->function))
		return 0;	/* shut down or never set up: Linux WARNs and bails */

	os = lkpi_timer_get(t);
	spin_lock(&lkpi_timer_lock);
	if (!t->function) {
		spin_unlock(&lkpi_timer_lock);
		return 0;
	}
	was_pending = t->pending;
	if (only_if_pending && !was_pending) {
		spin_unlock(&lkpi_timer_lock);
		return 0;
	}
	t->expires = expires;
	t->pending = 1;
	lkpi_os_timer_arm(os, lkpi_timer_deadline(expires));
	spin_unlock(&lkpi_timer_lock);
	return was_pending;
}

int mod_timer(struct timer_list *t, unsigned long expires)
{
	return lkpi_timer_arm(t, expires, false);
}

int mod_timer_pending(struct timer_list *t, unsigned long expires)
{
	return lkpi_timer_arm(t, expires, true);
}

int timer_pending(const struct timer_list *t)
{
	return __atomic_load_n(&t->pending, __ATOMIC_ACQUIRE) != 0;
}

int timer_delete(struct timer_list *t)
{
	lkpi_os_timer_t os;
	int was_pending;

	spin_lock(&lkpi_timer_lock);
	os = t->os;
	was_pending = t->pending;
	t->pending = 0;
	spin_unlock(&lkpi_timer_lock);

	if (os && lkpi_os_timer_cancel(os))
		was_pending = 1;
	return was_pending;
}

int timer_delete_sync(struct timer_list *t)
{
	lkpi_os_timer_t os;
	int was_pending;

	spin_lock(&lkpi_timer_lock);
	os = t->os;
	was_pending = t->pending;
	t->pending = 0;
	spin_unlock(&lkpi_timer_lock);

	if (!os)
		return was_pending;
	/* Waits for a callback that is already running on the timer thread. */
	if (lkpi_os_timer_cancel_sync(os))
		was_pending = 1;
	return was_pending;
}

int timer_shutdown_sync(struct timer_list *t)
{
	int was_pending;

	/* Clear ->function first so a callback that is just starting, or a
	 * concurrent mod_timer(), cannot re-arm the timer any more. */
	spin_lock(&lkpi_timer_lock);
	t->function = NULL;
	spin_unlock(&lkpi_timer_lock);

	was_pending = timer_delete_sync(t);
	return was_pending;
}

void timer_destroy(struct timer_list *t)
{
	lkpi_os_timer_t os;

	spin_lock(&lkpi_timer_lock);
	t->function = NULL;
	t->pending = 0;
	os = t->os;
	t->os = NULL;
	spin_unlock(&lkpi_timer_lock);

	if (os) {
		(void)lkpi_os_timer_cancel_sync(os);
		lkpi_os_timer_destroy(os);
	}
}
