/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_IPV6_H_
#define _LKPI_LINUX_IPV6_H_
#include <linux/in6.h>
#include <linux/list.h>
#include <linux/rwlock.h>
struct inet6_ifaddr {
	struct in6_addr addr;
	u32 flags;
	struct list_head if_list;
};
struct inet6_dev {
	struct net_device *dev;
	struct list_head addr_list;
	rwlock_t lock;
};
#define IFA_F_TENTATIVE 0x40
#endif
