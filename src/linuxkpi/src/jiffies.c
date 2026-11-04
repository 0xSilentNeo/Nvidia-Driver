// SPDX-License-Identifier: MIT
/*
 * jiffies.  The backend runs a periodic 1000/HZ ms (10 ms) tick from
 * lkpi_runtime_init() onwards and calls lkpi_jiffies_tick() from it; nothing
 * else writes these variables.  On the 64-bit targets this port supports
 * `jiffies` and `jiffies_64` hold the same value, but both are kept so the
 * header can offer the upstream API unchanged.
 */
#include <linux/kernel.h>
#include <linux/jiffies.h>

volatile unsigned long jiffies = INITIAL_JIFFIES;
volatile u64 jiffies_64 = INITIAL_JIFFIES;

void lkpi_jiffies_tick(void)
{
	u64 next = jiffies_64 + 1;

	/* Two plain stores: readers tolerate the brief skew, and only the
	 * backend's tick thread ever writes here. */
	jiffies_64 = next;
	jiffies = (unsigned long)next;
}
