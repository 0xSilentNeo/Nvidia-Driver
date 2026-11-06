/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_NET_IF_VLAN_VAR_H_
#define _BSDKPI_NET_IF_VLAN_VAR_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <net/ethernet.h>

/* struct ether_vlan_header comes from net/ethernet.h. */

#define EVL_VLID_MASK	0x0FFF
#define EVL_PRI_MASK	0xE000
#define EVL_VLANOFTAG(tag) ((tag) & EVL_VLID_MASK)
#define EVL_PRIOFTAG(tag) (((tag) >> 13) & 7)
#define EVL_CFIOFTAG(tag) (((tag) >> 12) & 1)
#define EVL_MAKETAG(vlid, pri, cfi) \
	((((((pri) & 7) << 1) | ((cfi) & 1)) << 12) | ((vlid) & EVL_VLID_MASK))

#define VLAN_CAPABILITIES(_ifp)	do { } while (0)
#define VLAN_TRUNKDEV(_ifp)	(NULL)
#define VLAN_TAG(_ifp, _vid)	(EINVAL)
#define VLAN_PCP(_ifp, _pcp)	(EINVAL)
#define VLAN_COOKIE(_ifp)	(NULL)
#define VLAN_SETCOOKIE(_ifp, _cookie) do { } while (0)
#define VLAN_DEVAT(_ifp, _vid)	(NULL)
#define VLAN_SNAPSHOT(_ifp, _vids, _n) (0)

struct vlanreq {
	char	vlr_parent[IFNAMSIZ];
	u_short	vlr_tag;
	u_short	vlr_proto;
};
#define SIOCSETVLAN	SIOCSIFGENERIC
#define SIOCGETVLAN	SIOCGIFGENERIC
#endif
