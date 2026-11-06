/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_NET_IF_ARP_H_
#define _BSDKPI_NET_IF_ARP_H_
#include <sys/cdefs.h>
#include <sys/types.h>

struct arphdr {
	u_short	ar_hrd;		/* format of hardware address */
#define ARPHRD_ETHER	1
#define ARPHRD_IEEE802	6
#define ARPHRD_ARCNET	7
#define ARPHRD_FRELAY	15
#define ARPHRD_IEEE1394	24
#define ARPHRD_INFINIBAND 32
	u_short	ar_pro;		/* format of protocol address */
	u_char	ar_hln;		/* length of hardware address */
	u_char	ar_pln;		/* length of protocol address */
	u_short	ar_op;		/* one of: */
#define ARPOP_REQUEST	1
#define ARPOP_REPLY	2
#define ARPOP_REVREQUEST 3
#define ARPOP_REVREPLY	4
#define ARPOP_INVREQUEST 8
#define ARPOP_INVREPLY	9
} __packed;
#define ar_sha(ap)	(((caddr_t)((ap) + 1)) + 0)
#define ar_spa(ap)	(((caddr_t)((ap) + 1)) + (ap)->ar_hln)
#define ar_tha(ap)	(((caddr_t)((ap) + 1)) + (ap)->ar_hln + (ap)->ar_pln)
#define ar_tpa(ap)	(((caddr_t)((ap) + 1)) + 2 * (ap)->ar_hln + (ap)->ar_pln)
#define arphdr_len2(ar_hln, ar_pln) (sizeof(struct arphdr) + 2 * (ar_hln) + 2 * (ar_pln))
#define arphdr_len(ap)	(arphdr_len2((ap)->ar_hln, (ap)->ar_pln))
#endif
