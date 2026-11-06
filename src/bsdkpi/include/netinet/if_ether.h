/* SPDX-License-Identifier: MIT */
/* netinet/if_ether.h - Ethernet ARP glue; no ARP here (no IP stack). */
#ifndef _BSDKPI_NETINET_IF_ETHER_H_
#define _BSDKPI_NETINET_IF_ETHER_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <net/ethernet.h>
#include <net/if_arp.h>
#include <netinet/in.h>

struct ether_arp {
	struct	arphdr ea_hdr;		/* fixed-size header */
	u_char	arp_sha[ETHER_ADDR_LEN]; /* sender hardware address */
	u_char	arp_spa[4];		/* sender protocol address */
	u_char	arp_tha[ETHER_ADDR_LEN]; /* target hardware address */
	u_char	arp_tpa[4];		/* target protocol address */
} __packed;
#define arp_hrd	ea_hdr.ar_hrd
#define arp_pro	ea_hdr.ar_pro
#define arp_hln	ea_hdr.ar_hln
#define arp_pln	ea_hdr.ar_pln
#define arp_op	ea_hdr.ar_op

#define ETHER_MAP_IP_MULTICAST(ipaddr, enaddr) do {			\
	(enaddr)[0] = 0x01;						\
	(enaddr)[1] = 0x00;						\
	(enaddr)[2] = 0x5e;						\
	(enaddr)[3] = ((const u_char *)ipaddr)[1] & 0x7f;		\
	(enaddr)[4] = ((const u_char *)ipaddr)[2];			\
	(enaddr)[5] = ((const u_char *)ipaddr)[3];			\
} while (0)

struct ifnet;
struct ifaddr;

static __inline void
arp_ifinit(struct ifnet *ifp, struct ifaddr *ifa)
{
	(void)ifp;
	(void)ifa;
}

static __inline void
arp_announce_ifaddr(struct ifnet *ifp, struct in_addr addr, u_char *enaddr)
{
	(void)ifp;
	(void)addr;
	(void)enaddr;
}
#endif
