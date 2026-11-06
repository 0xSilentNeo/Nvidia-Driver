/* SPDX-License-Identifier: MIT */
/* netinet/in.h - IPv4 address types and protocol numbers (FreeBSD values). */
#ifndef _BSDKPI_NETINET_IN_H_
#define _BSDKPI_NETINET_IN_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/endian.h>
#include <asm/byteorder.h>	/* htons/ntohs/htonl/ntohl */

#ifndef _IN_ADDR_T
typedef uint32_t	in_addr_t;
#define _IN_ADDR_T
#endif
#ifndef _IN_PORT_T
typedef uint16_t	in_port_t;
#define _IN_PORT_T
#endif

struct in_addr {
	in_addr_t	s_addr;
};

struct sockaddr_in {
	uint8_t		sin_len;
	sa_family_t	sin_family;
	in_port_t	sin_port;
	struct in_addr	sin_addr;
	char		sin_zero[8];
};

#define IPPROTO_IP		0
#define IPPROTO_HOPOPTS		0
#define IPPROTO_ICMP		1
#define IPPROTO_IGMP		2
#define IPPROTO_IPV4		4
#define IPPROTO_IPIP		IPPROTO_IPV4
#define IPPROTO_TCP		6
#define IPPROTO_UDP		17
#define IPPROTO_IPV6		41
#define IPPROTO_ROUTING		43
#define IPPROTO_FRAGMENT	44
#define IPPROTO_GRE		47
#define IPPROTO_ESP		50
#define IPPROTO_AH		51
#define IPPROTO_ICMPV6		58
#define IPPROTO_NONE		59
#define IPPROTO_DSTOPTS		60
#define IPPROTO_SCTP		132
#define IPPROTO_UDPLITE		136
#define IPPROTO_RAW		255
#define IPPROTO_MAX		256

#define INADDR_ANY		((in_addr_t)0x00000000)
#define INADDR_LOOPBACK		((in_addr_t)0x7f000001)
#define INADDR_BROADCAST	((in_addr_t)0xffffffff)
#define INADDR_NONE		((in_addr_t)0xffffffff)
#define INADDR_UNSPEC_GROUP	((in_addr_t)0xe0000000)
#define INADDR_ALLHOSTS_GROUP	((in_addr_t)0xe0000001)
#define INADDR_ALLRTRS_GROUP	((in_addr_t)0xe0000002)
#define INADDR_MAX_LOCAL_GROUP	((in_addr_t)0xe00000ff)

#define IN_CLASSA(i)		(((in_addr_t)(i) & 0x80000000) == 0)
#define IN_CLASSB(i)		(((in_addr_t)(i) & 0xc0000000) == 0x80000000)
#define IN_CLASSC(i)		(((in_addr_t)(i) & 0xe0000000) == 0xc0000000)
#define IN_CLASSD(i)		(((in_addr_t)(i) & 0xf0000000) == 0xe0000000)
#define IN_MULTICAST(i)		IN_CLASSD(i)
#define IN_EXPERIMENTAL(i)	(((in_addr_t)(i) & 0xf0000000) == 0xf0000000)
#define IN_BADCLASS(i)		(((in_addr_t)(i) & 0xf0000000) == 0xf0000000)
#define IN_LINKLOCAL(i)		(((in_addr_t)(i) & 0xffff0000) == 0xa9fe0000)
#define IN_LOOPBACK(i)		(((in_addr_t)(i) & 0xff000000) == 0x7f000000)
#define IN_ZERONET(i)		(((in_addr_t)(i) & 0xff000000) == 0)
#define IN_LOCAL_GROUP(i)	(((in_addr_t)(i) & 0xffffff00) == 0xe0000000)
#define IN_ANY_LOCAL(i)		(IN_LINKLOCAL(i) || IN_LOCAL_GROUP(i))

#define INET_ADDRSTRLEN		16

#define IP_OPTIONS		1
#define IP_HDRINCL		2
#define IP_TOS			3
#define IP_TTL			4
#define IP_RECVOPTS		5
#define IP_MULTICAST_IF		9
#define IP_MULTICAST_TTL	10
#define IP_MULTICAST_LOOP	11
#define IP_ADD_MEMBERSHIP	12
#define IP_DROP_MEMBERSHIP	13
#define IP_DEFAULT_MULTICAST_TTL 1
#define IP_DEFAULT_MULTICAST_LOOP 1
#define IP_MAX_MEMBERSHIPS	4095

struct ip_mreq {
	struct in_addr	imr_multiaddr;
	struct in_addr	imr_interface;
};
#endif /* _BSDKPI_NETINET_IN_H_ */
