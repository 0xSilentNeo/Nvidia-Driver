/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_IN6_H_
#define _LKPI_LINUX_IN6_H_
#include <linux/types.h>
struct in6_addr {
	union {
		u8 u6_addr8[16];
		__be16 u6_addr16[8];
		__be32 u6_addr32[4];
	} in6_u;
#define s6_addr		in6_u.u6_addr8
#define s6_addr16	in6_u.u6_addr16
#define s6_addr32	in6_u.u6_addr32
};
#endif
