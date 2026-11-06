/* SPDX-License-Identifier: MIT */
/*
 * net/route.h - struct route (used by if_output to hand a preformed header
 * through) and the routing-socket notifications, which become
 * bsdkpi_ifnet_hooks callbacks for the IO80211 layer (src/ifnet.c).
 */
#ifndef _BSDKPI_NET_ROUTE_H_
#define _BSDKPI_NET_ROUTE_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/socket.h>

struct nhop_object;
struct llentry;
struct ifnet;
struct ifaddr;

struct route {
	struct nhop_object	*ro_nh;
	struct llentry		*ro_lle;
	char			*ro_prepend;
	uint16_t		 ro_plen;
	uint16_t		 ro_flags;
	uint16_t		 ro_mtu;
	uint16_t		 spare;
	struct sockaddr		 ro_dst;
};

#define RT_L2_ME_BIT		2
#define RT_MAY_LOOP_BIT		3
#define RT_HAS_HEADER_BIT	4
#define RT_REJECT		0x0020
#define RT_BLACKHOLE		0x0040
#define RT_HAS_GW		0x0080
#define RT_LLE_CACHE		0x0100
#define RT_L2_ME		(1 << RT_L2_ME_BIT)
#define RT_MAY_LOOP		(1 << RT_MAY_LOOP_BIT)
#define RT_HAS_HEADER		(1 << RT_HAS_HEADER_BIT)

#define RTM_ADD		0x1
#define RTM_DELETE	0x2
#define RTM_CHANGE	0x3
#define RTM_GET		0x4
#define RTM_LOSING	0x5
#define RTM_REDIRECT	0x6
#define RTM_MISS	0x7
#define RTM_LOCK	0x8
#define RTM_RESOLVE	0xb
#define RTM_NEWADDR	0xc
#define RTM_DELADDR	0xd
#define RTM_IFINFO	0xe
#define RTM_NEWMADDR	0xf
#define RTM_DELMADDR	0x10
#define RTM_IFANNOUNCE	0x11
#define RTM_IEEE80211	0x12
#define RTM_VERSION	5

void	rt_ieee80211msg(struct ifnet *ifp, int what, void *data, size_t data_len);
void	rt_ifmsg(struct ifnet *ifp, int if_flags_mask);
void	rt_ifannouncemsg(struct ifnet *ifp, int what);
void	rt_newaddrmsg(int cmd, struct ifaddr *ifa, void *rt);
#endif /* _BSDKPI_NET_ROUTE_H_ */
