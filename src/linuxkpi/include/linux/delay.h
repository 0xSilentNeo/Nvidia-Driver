/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_DELAY_H_
#define _LKPI_LINUX_DELAY_H_
#include <linux/types.h>
#include <linux/jiffies.h>
#include <lkpi/os.h>
static inline void udelay(unsigned long us) { lkpi_os_delay_us((uint32_t)us); }
static inline void ndelay(unsigned long ns) { lkpi_os_delay_us((uint32_t)((ns + 999) / 1000)); }
static inline void mdelay(unsigned long ms) { while (ms--) lkpi_os_delay_us(1000); }
static inline void msleep(unsigned int ms) { lkpi_os_sleep_ms(ms ? ms : 1); }
static inline unsigned long msleep_interruptible(unsigned int ms) { msleep(ms); return 0; }
static inline void ssleep(unsigned int s) { msleep(s * 1000); }
static inline void usleep_range(unsigned long min, unsigned long max)
{
	(void)max;
	if (min < 2000)
		lkpi_os_delay_us((uint32_t)min);
	else
		lkpi_os_sleep_ms((uint32_t)((min + 999) / 1000));
}
#define usleep_range_state(mn, mx, st) usleep_range(mn, mx)
#define fsleep(us) usleep_range(us, us)
#endif
