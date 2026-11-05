/* SPDX-License-Identifier: MIT */
/*
 * sys/socket.h - the BSD sockaddr and address families net80211 refers to.
 * No sockets exist here; the ioctl surface (SIOCS80211) is driven from the
 * kext and the sockaddr types only carry link-layer and 802.11 addresses.
 */
#ifndef _BSDKPI_SYS_SOCKET_H_
#define _BSDKPI_SYS_SOCKET_H_
#include <sys/cdefs.h>
#include <sys/types.h>

#ifndef _SA_FAMILY_T
typedef uint8_t		sa_family_t;
#define _SA_FAMILY_T
#endif
#ifndef _SOCKLEN_T
typedef uint32_t	socklen_t;
#define _SOCKLEN_T
#endif

#define SOCK_STREAM	1
#define SOCK_DGRAM	2
#define SOCK_RAW	3
#define SOCK_RDM	4
#define SOCK_SEQPACKET	5

#define SO_DEBUG	0x00000001

#define AF_UNSPEC	0
#define AF_LOCAL	1
#define AF_UNIX		AF_LOCAL
#define AF_INET		2
#define AF_IMPLINK	3
#define AF_PUP		4
#define AF_CHAOS	5
#define AF_NETBIOS	6
#define AF_ISO		7
#define AF_OSI		AF_ISO
#define AF_ECMA		8
#define AF_DATAKIT	9
#define AF_CCITT	10
#define AF_SNA		11
#define AF_DECnet	12
#define AF_DLI		13
#define AF_LAT		14
#define AF_HYLINK	15
#define AF_APPLETALK	16
#define AF_ROUTE	17
#define AF_LINK		18
#define pseudo_AF_XTP	19
#define AF_COIP		20
#define AF_CNT		21
#define pseudo_AF_RTIP	22
#define AF_IPX		23
#define AF_SIP		24
#define pseudo_AF_PIP	25
#define AF_ISDN		26
#define AF_E164		AF_ISDN
#define pseudo_AF_KEY	27
#define AF_INET6	28
#define AF_NATM		29
#define AF_ATM		30
#define pseudo_AF_HDRCMPLT 31
#define AF_NETGRAPH	32
#define AF_SLOW		33
#define AF_SCLUSTER	34
#define AF_ARP		35
#define AF_BLUETOOTH	36
#define AF_IEEE80211	37
#define AF_NETLINK	38
#define AF_INET_SDP	40
#define AF_INET6_SDP	42
#define AF_HYPERV	43
#define AF_DIVERT	44
#define AF_IPFWLOG	46
#define AF_MAX		47
#define AF_VENDOR00	39

#define PF_UNSPEC	AF_UNSPEC
#define PF_LOCAL	AF_LOCAL
#define PF_UNIX		PF_LOCAL
#define PF_INET		AF_INET
#define PF_ROUTE	AF_ROUTE
#define PF_LINK		AF_LINK
#define PF_INET6	AF_INET6
#define PF_IEEE80211	AF_IEEE80211
#define PF_MAX		AF_MAX

struct sockaddr {
	unsigned char	sa_len;		/* total length */
	sa_family_t	sa_family;	/* address family */
	char		sa_data[14];	/* actually longer; address value */
};
#define SOCK_MAXADDRLEN	255

struct sockproto {
	unsigned short	sp_family;
	unsigned short	sp_protocol;
};

#define _SS_MAXSIZE	128U
#define _SS_ALIGNSIZE	(sizeof(int64_t))
#define _SS_PAD1SIZE	(_SS_ALIGNSIZE - sizeof(unsigned char) - sizeof(sa_family_t))
#define _SS_PAD2SIZE	(_SS_MAXSIZE - sizeof(unsigned char) - sizeof(sa_family_t) - \
			    _SS_PAD1SIZE - _SS_ALIGNSIZE)

struct sockaddr_storage {
	unsigned char	ss_len;
	sa_family_t	ss_family;
	char		__ss_pad1[_SS_PAD1SIZE];
	int64_t		__ss_align;
	char		__ss_pad2[_SS_PAD2SIZE];
};

#define MSG_OOB		0x00000001
#define MSG_PEEK	0x00000002
#define MSG_DONTROUTE	0x00000004
#define MSG_EOR		0x00000008
#define MSG_TRUNC	0x00000010
#define MSG_CTRUNC	0x00000020
#define MSG_WAITALL	0x00000040
#define MSG_DONTWAIT	0x00000080
#define MSG_EOF		0x00000100
#define MSG_NOSIGNAL	0x00020000

#define SHUT_RD		0
#define SHUT_WR		1
#define SHUT_RDWR	2

struct iovec_bsdkpi {
	void	*iov_base;
	size_t	 iov_len;
};
#endif /* _BSDKPI_SYS_SOCKET_H_ */
