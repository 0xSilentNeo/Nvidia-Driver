/* SPDX-License-Identifier: MIT */
/* net/debugnet.h - netdump/netgdb transport: not built (no DEBUGNET). */
#ifndef _BSDKPI_NET_DEBUGNET_H_
#define _BSDKPI_NET_DEBUGNET_H_
#include <sys/cdefs.h>
#include <sys/types.h>

struct ifnet;
struct mbuf;

enum debugnet_ev {
	DEBUGNET_START,
	DEBUGNET_END,
};

typedef void debugnet_init_t(struct ifnet *, int *nrxr, int *ncl, int *clsize);
typedef void debugnet_event_t(struct ifnet *, enum debugnet_ev);
typedef int debugnet_transmit_t(struct ifnet *, struct mbuf *);
typedef int debugnet_poll_t(struct ifnet *, int);

struct debugnet_methods {
	debugnet_init_t		*dn_init;
	debugnet_event_t	*dn_event;
	debugnet_transmit_t	*dn_transmit;
	debugnet_poll_t		*dn_poll;
};

#define DEBUGNET_DEFINE(driver)
#define DEBUGNET_SET(ifp, driver)	do { } while (0)
#define DEBUGNET_NOTIFY_MTU(ifp)	do { } while (0)
#define DEBUGNET_SUPPORTED_NIC(ifp)	(0)
#endif
