/* SPDX-License-Identifier: MIT */
/* netinet/ip.h - the IPv4 header (little-endian bit-field layout). */
#ifndef _BSDKPI_NETINET_IP_H_
#define _BSDKPI_NETINET_IP_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <netinet/in.h>

#define IPVERSION	4

struct ip {
	u_char	ip_hl:4,		/* header length */
		ip_v:4;			/* version */
	u_char	ip_tos;			/* type of service */
	u_short	ip_len;			/* total length */
	u_short	ip_id;			/* identification */
	u_short	ip_off;			/* fragment offset field */
#define IP_RF 0x8000			/* reserved fragment flag */
#define IP_DF 0x4000			/* dont fragment flag */
#define IP_MF 0x2000			/* more fragments flag */
#define IP_OFFMASK 0x1fff		/* mask for fragmenting bits */
	u_char	ip_ttl;			/* time to live */
	u_char	ip_p;			/* protocol */
	u_short	ip_sum;			/* checksum */
	struct	in_addr ip_src, ip_dst;	/* source and dest address */
} __packed __aligned(2);

#define IP_MAXPACKET	65535

#define IPTOS_LOWDELAY		0x10
#define IPTOS_THROUGHPUT	0x08
#define IPTOS_RELIABILITY	0x04
#define IPTOS_MINCOST		0x02
#define IPTOS_PREC_NETCONTROL		0xe0
#define IPTOS_PREC_INTERNETCONTROL	0xc0
#define IPTOS_PREC_CRITIC_ECP		0xa0
#define IPTOS_PREC_FLASHOVERRIDE	0x80
#define IPTOS_PREC_FLASH		0x60
#define IPTOS_PREC_IMMEDIATE		0x40
#define IPTOS_PREC_PRIORITY		0x20
#define IPTOS_PREC_ROUTINE		0x00
#define IPTOS_DSCP_CS0		0x00
#define IPTOS_DSCP_CS1		0x20
#define IPTOS_DSCP_AF11		0x28
#define IPTOS_DSCP_AF12		0x30
#define IPTOS_DSCP_AF13		0x38
#define IPTOS_DSCP_CS2		0x40
#define IPTOS_DSCP_AF21		0x48
#define IPTOS_DSCP_AF22		0x50
#define IPTOS_DSCP_AF23		0x58
#define IPTOS_DSCP_CS3		0x60
#define IPTOS_DSCP_AF31		0x68
#define IPTOS_DSCP_AF32		0x70
#define IPTOS_DSCP_AF33		0x78
#define IPTOS_DSCP_CS4		0x80
#define IPTOS_DSCP_AF41		0x88
#define IPTOS_DSCP_AF42		0x90
#define IPTOS_DSCP_AF43		0x98
#define IPTOS_DSCP_CS5		0xa0
#define IPTOS_DSCP_VA		0xb0
#define IPTOS_DSCP_EF		0xb8
#define IPTOS_DSCP_CS6		0xc0
#define IPTOS_DSCP_CS7		0xe0
#define IPTOS_ECN_NOTECT	0x00
#define IPTOS_ECN_ECT1		0x01
#define IPTOS_ECN_ECT0		0x02
#define IPTOS_ECN_CE		0x03
#define IPTOS_ECN_MASK		0x03
#define IPTOS_DSCP(x)		((x) & 0xfc)
#define IPTOS_ECN(x)		((x) & IPTOS_ECN_MASK)

#define IPOPT_EOL		0
#define IPOPT_NOP		1
#define IPOPT_RR		7
#define IPOPT_TS		68
#define IPOPT_SECURITY		130
#define IPOPT_LSRR		131
#define IPOPT_SSRR		137
#define IPOPT_RA		148
#define MAXTTL			255
#define IPDEFTTL		64
#define IPFRAGTTL		60
#define IPTTLDEC		1
#define IP_MSS			576
#endif /* _BSDKPI_NETINET_IP_H_ */
