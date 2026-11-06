/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_NETINET_IP6_H_
#define _BSDKPI_NETINET_IP6_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <linux/in6.h>

struct ip6_hdr {
	union {
		struct ip6_hdrctl {
			uint32_t ip6_un1_flow;	/* 20 bits of flow-ID */
			uint16_t ip6_un1_plen;	/* payload length */
			uint8_t	 ip6_un1_nxt;	/* next header */
			uint8_t	 ip6_un1_hlim;	/* hop limit */
		} ip6_un1;
		uint8_t ip6_un2_vfc;	/* 4 bits version, top 4 bits class */
	} ip6_ctlun;
	struct in6_addr ip6_src;	/* source address */
	struct in6_addr ip6_dst;	/* destination address */
} __packed;

#define ip6_vfc		ip6_ctlun.ip6_un2_vfc
#define ip6_flow	ip6_ctlun.ip6_un1.ip6_un1_flow
#define ip6_plen	ip6_ctlun.ip6_un1.ip6_un1_plen
#define ip6_nxt		ip6_ctlun.ip6_un1.ip6_un1_nxt
#define ip6_hlim	ip6_ctlun.ip6_un1.ip6_un1_hlim
#define ip6_hops	ip6_ctlun.ip6_un1.ip6_un1_hlim

#define IPV6_VERSION		0x60
#define IPV6_VERSION_MASK	0xf0
#define IPV6_FLOWINFO_MASK	0xffffff0f	/* little endian */
#define IPV6_FLOWLABEL_MASK	0xffff0f00
#define IPV6_FLOWLABEL_LEN	20
#define IPV6_MAXHLIM		255
#define IPV6_DEFHLIM		64
#define IPV6_MAXPACKET		65535
#define IPV6_MMTU		1280
#endif
