/* SPDX-License-Identifier: MIT */
/*
 * sys/time.h - time types and the kernel clock accessors.  struct timeval
 * and struct timespec may already come from the host's <sys/types.h>
 * (glibc pulls them in through <sys/select.h>); their guards are honoured.
 */
#ifndef _BSDKPI_SYS_TIME_H_
#define _BSDKPI_SYS_TIME_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <lkpi/os.h>
#include <linux/jiffies.h>

#if !defined(__timeval_defined) && !defined(_STRUCT_TIMEVAL)
struct timeval {
	time_t		tv_sec;
	long		tv_usec;
};
#define __timeval_defined	1
#define _STRUCT_TIMEVAL		struct timeval
#endif
#if !defined(__timespec_defined) && !defined(_STRUCT_TIMESPEC)
struct timespec {
	time_t		tv_sec;
	long		tv_nsec;
};
#define __timespec_defined	1
#define _STRUCT_TIMESPEC	struct timespec
#endif

struct bintime {
	time_t		sec;
	uint64_t	frac;
};

#define SBT_1S	((sbintime_t)1 << 32)
#define SBT_1M	(SBT_1S * 60)
#define SBT_1MS	(SBT_1S / 1000)
#define SBT_1US	(SBT_1S / 1000000)
#define SBT_1NS	(SBT_1S / 1000000000)
#define SBT_MAX	0x7fffffffffffffffLL

static __inline sbintime_t tick_sbt_value(void) { return (SBT_1S / HZ); }
#define tick_sbt	tick_sbt_value()

static __inline sbintime_t
mstosbt(uint64_t ms)
{
	return ((sbintime_t)(ms * (SBT_1S / 1000)));
}

static __inline sbintime_t
ustosbt(uint64_t us)
{
	return ((sbintime_t)(us * (SBT_1S / 1000000)));
}

static __inline sbintime_t
nstosbt(uint64_t ns)
{
	return ((sbintime_t)((ns * SBT_1S) / 1000000000ULL));
}

static __inline int64_t
sbttons(sbintime_t sbt)
{
	return ((int64_t)((sbt * 1000000000LL) >> 32));
}

static __inline int64_t
sbttous(sbintime_t sbt)
{
	return ((int64_t)((sbt * 1000000LL) >> 32));
}

static __inline int64_t
sbttoms(sbintime_t sbt)
{
	return ((int64_t)((sbt * 1000LL) >> 32));
}

#define timevalcmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_usec cmp (uvp)->tv_usec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define timevaladd(vvp, uvp) do {					\
	(vvp)->tv_sec += (uvp)->tv_sec;					\
	(vvp)->tv_usec += (uvp)->tv_usec;				\
	if ((vvp)->tv_usec >= 1000000) {				\
		(vvp)->tv_sec++;					\
		(vvp)->tv_usec -= 1000000;				\
	}								\
} while (0)
#define timevalsub(vvp, uvp) do {					\
	(vvp)->tv_sec -= (uvp)->tv_sec;					\
	(vvp)->tv_usec -= (uvp)->tv_usec;				\
	if ((vvp)->tv_usec < 0) {					\
		(vvp)->tv_sec--;					\
		(vvp)->tv_usec += 1000000;				\
	}								\
} while (0)
#define timespecclear(tvp)	((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#define timespecisset(tvp)	((tvp)->tv_sec || (tvp)->tv_nsec)
#define timespeccmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define timevalclear(tvp)	((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define timevalisset(tvp)	((tvp)->tv_sec || (tvp)->tv_usec)

/* Kernel clocks on the OS boundary. */
static __inline time_t bsdkpi_time_uptime(void) { return ((time_t)(lkpi_os_uptime_ns() / 1000000000ULL)); }
static __inline time_t bsdkpi_time_second(void) { return ((time_t)(lkpi_os_realtime_ns() / 1000000000ULL)); }
#define time_uptime	bsdkpi_time_uptime()
#define time_second	bsdkpi_time_second()
extern struct timeval boottime;

static __inline void
bsdkpi_ns_to_timeval(uint64_t ns, struct timeval *tv)
{
	tv->tv_sec = (time_t)(ns / 1000000000ULL);
	tv->tv_usec = (long)((ns % 1000000000ULL) / 1000);
}

static __inline void
bsdkpi_ns_to_timespec(uint64_t ns, struct timespec *ts)
{
	ts->tv_sec = (time_t)(ns / 1000000000ULL);
	ts->tv_nsec = (long)(ns % 1000000000ULL);
}

static __inline void microtime(struct timeval *tv) { bsdkpi_ns_to_timeval(lkpi_os_realtime_ns(), tv); }
static __inline void getmicrotime(struct timeval *tv) { microtime(tv); }
static __inline void microuptime(struct timeval *tv) { bsdkpi_ns_to_timeval(lkpi_os_uptime_ns(), tv); }
static __inline void getmicrouptime(struct timeval *tv) { microuptime(tv); }
static __inline void nanotime(struct timespec *ts) { bsdkpi_ns_to_timespec(lkpi_os_realtime_ns(), ts); }
static __inline void getnanotime(struct timespec *ts) { nanotime(ts); }
static __inline void nanouptime(struct timespec *ts) { bsdkpi_ns_to_timespec(lkpi_os_uptime_ns(), ts); }
static __inline void getnanouptime(struct timespec *ts) { nanouptime(ts); }
static __inline sbintime_t sbinuptime(void) { return (nstosbt(lkpi_os_uptime_ns())); }
static __inline sbintime_t getsbinuptime(void) { return (sbinuptime()); }
static __inline void getboottime(struct timeval *tv) { *tv = boottime; }

int	ppsratecheck(struct timeval *lasttime, int *curpps, int maxpps);
int	ratecheck(struct timeval *lasttime, const struct timeval *mininterval);
int	tvtohz(struct timeval *tv);

/* FreeBSD's versions use the u_int max(); the result type is u_int. */
static __inline u_int
bsdkpi_umax(u_int a, u_int b)
{
	return (a > b ? a : b);
}
#define MSEC_2_TICKS(m)	bsdkpi_umax(1, (uint32_t)((hz == 1000) ? (m) : ((uint64_t)(m) * (uint64_t)hz) / (uint64_t)1000))
#define TICKS_2_MSEC(t)	bsdkpi_umax(1, (uint32_t)((hz == 1000) ? (t) : (((uint64_t)(t) * (uint64_t)1000) / (uint64_t)hz)))
#define TICKS_2_USEC(t)	bsdkpi_umax(1, (uint32_t)((hz == 1000) ? (t) * 1000 : (((uint64_t)(t) * (uint64_t)1000000) / (uint64_t)hz)))
#define USEC_2_TICKS(u)	bsdkpi_umax(1, (uint32_t)((hz == 1000) ? ((u) / 1000) : ((uint64_t)(u) * (uint64_t)hz) / (uint64_t)1000000))

extern int hz;
#endif /* _BSDKPI_SYS_TIME_H_ */
