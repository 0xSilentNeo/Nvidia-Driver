/* SPDX-License-Identifier: MIT */
/*
 * sys/systm.h - kernel-wide miscellany: printf/log (-> printk), KASSERT,
 * panic, sleep/wakeup, copyin/copyout, DELAY, pause, ticks.
 */
#ifndef _BSDKPI_SYS_SYSTM_H_
#define _BSDKPI_SYS_SYSTM_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/kassert.h>
#include <sys/libkern.h>
#include <sys/stdarg.h>
#include <sys/callout.h>
#include <machine/atomic.h>
#include <linux/kernel.h>	/* printk, stdio before printf becomes a macro */
#include <linux/jiffies.h>
#include <linux/delay.h>

extern int cold;
extern int rebooting;
extern int bootverbose;
extern int suspend_blocked;
extern int hz;
extern int tick;

/*
 * ticks is jiffies: both run at 100 Hz (see linux/jiffies.h).  A macro
 * because the backend advances jiffies, so the two cannot be separate
 * variables; net80211 only ever reads it.
 */
#define ticks			((int)jiffies)

/*
 * printf(9) family: formatted by the shim's kvprintf (FreeBSD's %D, %b and
 * %r conversions included) and handed to printk() one line at a time.
 */
int	bsdkpi_printf(const char *fmt, ...) __printflike(1, 2);
int	bsdkpi_vprintf(const char *fmt, va_list ap) __printflike(1, 0);
int	bsdkpi_snprintf(char *buf, size_t size, const char *fmt, ...) __printflike(3, 4);
int	bsdkpi_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) __printflike(3, 0);
int	bsdkpi_sprintf(char *buf, const char *fmt, ...) __printflike(2, 3);
int	bsdkpi_vsprintf(char *buf, const char *fmt, va_list ap) __printflike(2, 0);
#define printf(...)		bsdkpi_printf(__VA_ARGS__)
#define vprintf(fmt, ap)	bsdkpi_vprintf((fmt), (ap))
#define uprintf(...)		bsdkpi_printf(__VA_ARGS__)
#define tprintf(p, pri, ...)	bsdkpi_printf(__VA_ARGS__)
/* snprintf and friends: the libc/libkern ones are fine for the formats
 * net80211 uses with them; the bsdkpi_ variants add %D/%b when needed. */
void	bsdkpi_log(int level, const char *fmt, ...) __printflike(2, 3);
void	bsdkpi_vlog(int level, const char *fmt, va_list ap) __printflike(2, 0);
#define log(level, ...)		bsdkpi_log((level), __VA_ARGS__)
#define vlog(level, fmt, ap)	bsdkpi_vlog((level), (fmt), (ap))

#define critical_enter()	do { } while (0)
#define critical_exit()		do { } while (0)
#define cpu_spinwait()		do { } while (0)

/* Kernel-internal callers only: both sides are kernel memory. */
static __inline int
copyin(const void *uaddr, void *kaddr, size_t len)
{
	memcpy(kaddr, uaddr, len);
	return (0);
}

static __inline int
copyout(const void *kaddr, void *uaddr, size_t len)
{
	memcpy(uaddr, kaddr, len);
	return (0);
}

static __inline int
copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done)
{
	size_t n = strnlen((const char *)uaddr, len);

	if (n == len)
		return (ENAMETOOLONG);
	memcpy(kaddr, uaddr, n + 1);
	if (done != NULL)
		*done = n + 1;
	return (0);
}

static __inline int
copystr(const void *kfaddr, void *kdaddr, size_t len, size_t *done)
{
	return (copyinstr(kfaddr, kdaddr, len, done));
}

#define copyin_nofault	copyin
#define copyout_nofault	copyout
#define fubyte(b)	(*(const volatile uint8_t *)(b))
#define subyte(b, v)	(*(volatile uint8_t *)(b) = (v), 0)

#define ovbcopy(f, t, l)	memmove((t), (f), (l))

/*
 * sleep(9)/wakeup(9): one global sleep channel mutex (see src/misc.c).
 * The caller's mutex is released while sleeping and re-taken on wakeup.
 */
struct mtx;
struct lock_object;
int	_sleep(const void *chan, struct lock_object *lock, int pri,
	    const char *wmesg, sbintime_t sbt, sbintime_t pr, int flags);
int	bsdkpi_msleep(const void *chan, struct mtx *mtx, int pri,
	    const char *wmesg, int timo);
#define msleep(chan, mtx, pri, wmesg, timo) \
	bsdkpi_msleep((chan), (mtx), (pri), (wmesg), (timo))
#define mtx_sleep(chan, mtx, pri, wmesg, timo) \
	bsdkpi_msleep((chan), (mtx), (pri), (wmesg), (timo))
#define tsleep(chan, pri, wmesg, timo) \
	bsdkpi_msleep((chan), NULL, (pri), (wmesg), (timo))
int	pause_sbt(const char *wmesg, sbintime_t sbt, sbintime_t pr, int flags);
int	pause(const char *wmesg, int timo);
#define pause_sig(wmesg, timo)	pause((wmesg), (timo))
void	wakeup(const void *chan);
void	wakeup_one(const void *chan);
void	wakeup_any(const void *chan);

static __inline void
DELAY(int usec)
{
	lkpi_os_delay_us((uint32_t)usec);
}

/* Boot time, uptime (sys/time.h) */
#include <sys/time.h>

static __inline uint64_t
get_cyclecount(void)
{
	return (lkpi_os_uptime_ns());
}

/* bcopy/bzero/bcmp are declared by <string.h> on both hosts (libkern/libc). */

/* kvprintf(9): the formatter behind all of the above. */
int	kvprintf(char const *fmt, void (*func)(int, void *), void *arg,
	    int radix, va_list ap) __printflike(1, 0);

struct malloc_type;
struct thread;
int	nullop(void);
int	eopnotsupp(void);

#define KERNEL_PANICKED()	(0)
#define SCHEDULER_STOPPED()	(0)
#define SCHEDULER_STOPPED_TD(td) (0)

#define BSDKPI_EXTERNAL_API	1
#endif /* _BSDKPI_SYS_SYSTM_H_ */
