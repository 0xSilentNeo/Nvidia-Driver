/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_NET_H_
#define _LKPI_LINUX_NET_H_
#include <linux/types.h>
struct net { int dummy; };
extern struct net init_net;
#define net_eq(a, b) (1)
#define possible_net_t struct net *
#define read_pnet(p) (*(p))
#define write_pnet(p, n) (*(p) = (n))
#endif
