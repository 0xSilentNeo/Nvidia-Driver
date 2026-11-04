/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_ETHTOOL_H_
#define _LKPI_LINUX_ETHTOOL_H_
#include <linux/types.h>
#define ETH_GSTRING_LEN	32
#define ETHTOOL_FWVERS_LEN 32
#define ETHTOOL_BUSINFO_LEN 32
enum ethtool_stringset {
	ETH_SS_TEST = 0,
	ETH_SS_STATS,
	ETH_SS_PRIV_FLAGS,
};
struct ethtool_stats {
	u32 cmd;
	u32 n_stats;
	u64 data[];
};
struct ethtool_ops { int dummy; };
static inline void ethtool_puts(u8 **data, const char *str)
{
	strscpy((char *)*data, str, ETH_GSTRING_LEN);
	*data += ETH_GSTRING_LEN;
}
#define ethtool_sprintf(data, fmt, ...) do { \
	snprintf((char *)*(data), ETH_GSTRING_LEN, fmt, ##__VA_ARGS__); \
	*(data) += ETH_GSTRING_LEN; } while (0)
#endif
