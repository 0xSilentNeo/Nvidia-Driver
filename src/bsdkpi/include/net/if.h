/* SPDX-License-Identifier: MIT */
/*
 * net/if.h - interface flags, ioctl request structures and the counter
 * enumeration, with FreeBSD's values (ifconfig-compatible).
 */
#ifndef _BSDKPI_NET_IF_H_
#define _BSDKPI_NET_IF_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#define IF_NAMESIZE	16
#ifndef IFNAMSIZ
#define IFNAMSIZ	IF_NAMESIZE
#endif
#define IF_MAXUNIT	0x7fff

struct ifnet;
typedef struct ifnet *if_t;

struct if_data {
	uint8_t		ifi_type;
	uint8_t		ifi_physical;
	uint8_t		ifi_addrlen;
	uint8_t		ifi_hdrlen;
	uint8_t		ifi_link_state;
	uint8_t		ifi_vhid;
	uint16_t	ifi_datalen;
	uint32_t	ifi_mtu;
	uint32_t	ifi_metric;
	uint64_t	ifi_baudrate;
	uint64_t	ifi_ipackets;
	uint64_t	ifi_ierrors;
	uint64_t	ifi_opackets;
	uint64_t	ifi_oerrors;
	uint64_t	ifi_collisions;
	uint64_t	ifi_ibytes;
	uint64_t	ifi_obytes;
	uint64_t	ifi_imcasts;
	uint64_t	ifi_omcasts;
	uint64_t	ifi_iqdrops;
	uint64_t	ifi_oqdrops;
	uint64_t	ifi_noproto;
	uint64_t	ifi_hwassist;
	union {
		time_t		tt;
		uint64_t	ph;
	} __ifi_epoch;
	union {
		struct timeval	tv;
		struct {
			uint64_t ph1;
			uint64_t ph2;
		} ph;
	} __ifi_lastchange;
};
#define ifi_epoch	__ifi_epoch.tt
#define ifi_lastchange	__ifi_lastchange.tv

#define IFF_UP		0x1
#define IFF_BROADCAST	0x2
#define IFF_DEBUG	0x4
#define IFF_LOOPBACK	0x8
#define IFF_POINTOPOINT	0x10
#define IFF_NEEDSEPOCH	0x20
#define IFF_DRV_RUNNING	0x40
#define IFF_NOARP	0x80
#define IFF_PROMISC	0x100
#define IFF_ALLMULTI	0x200
#define IFF_DRV_OACTIVE	0x400
#define IFF_SIMPLEX	0x800
#define IFF_LINK0	0x1000
#define IFF_LINK1	0x2000
#define IFF_LINK2	0x4000
#define IFF_ALTPHYS	IFF_LINK2
#define IFF_MULTICAST	0x8000
#define IFF_CANTCONFIG	0x10000
#define IFF_PPROMISC	0x20000
#define IFF_MONITOR	0x40000
#define IFF_STATICARP	0x80000
#define IFF_STICKYARP	0x100000
#define IFF_DYING	0x200000
#define IFF_RENAMING	0x400000
#define IFF_PALLMULTI	0x800000
#define IFF_NETLINK_1	0x1000000
#define IFF_RUNNING	IFF_DRV_RUNNING
#define IFF_OACTIVE	IFF_DRV_OACTIVE
#define IFF_CANTCHANGE \
	(IFF_BROADCAST | IFF_POINTOPOINT | IFF_DRV_RUNNING | IFF_DRV_OACTIVE | \
	 IFF_SIMPLEX | IFF_MULTICAST | IFF_ALLMULTI | IFF_PROMISC | \
	 IFF_DYING | IFF_CANTCONFIG | IFF_NEEDSEPOCH)

#define LINK_STATE_UNKNOWN	0
#define LINK_STATE_DOWN		1
#define LINK_STATE_UP		2

#define IF_Kbps(x)	((uintmax_t)(x) * 1000)
#define IF_Mbps(x)	(IF_Kbps((x) * 1000))
#define IF_Gbps(x)	(IF_Mbps((x) * 1000))

#define IFCAP_B_RXCSUM		0
#define IFCAP_B_TXCSUM		1
#define IFCAP_B_NETCONS		2
#define IFCAP_B_VLAN_MTU	3
#define IFCAP_B_VLAN_HWTAGGING	4
#define IFCAP_B_JUMBO_MTU	5
#define IFCAP_B_POLLING		6
#define IFCAP_B_VLAN_HWCSUM	7
#define IFCAP_B_TSO4		8
#define IFCAP_B_TSO6		9
#define IFCAP_B_LRO		10
#define IFCAP_B_WOL_UCAST	11
#define IFCAP_B_WOL_MCAST	12
#define IFCAP_B_WOL_MAGIC	13
#define IFCAP_B_TOE4		14
#define IFCAP_B_TOE6		15
#define IFCAP_B_VLAN_HWFILTER	16
#define IFCAP_B_NV		17
#define IFCAP_B_VLAN_HWTSO	18
#define IFCAP_B_LINKSTATE	19
#define IFCAP_B_NETMAP		20
#define IFCAP_B_RXCSUM_IPV6	21
#define IFCAP_B_TXCSUM_IPV6	22
#define IFCAP_B_HWSTATS		23
#define IFCAP_B_TXRTLMT		24
#define IFCAP_B_HWRXTSTMP	25
#define IFCAP_B_MEXTPG		26
#define IFCAP_B_TXTLS4		27
#define IFCAP_B_TXTLS6		28
#define IFCAP_B_VXLAN_HWCSUM	29
#define IFCAP_B_VXLAN_HWTSO	30
#define IFCAP_B_TXTLS_RTLMT	31
#define IFCAP_B_RXTLS4		32
#define IFCAP_B_RXTLS6		33
#define IFCAP_B_IPSEC_OFFLOAD	34
#define IFCAP_B_MAX		34
#define IFCAP_B_SIZE		64

#define IFCAP_BIT(x)		(1 << (x))
#define IFCAP_RXCSUM		IFCAP_BIT(IFCAP_B_RXCSUM)
#define IFCAP_TXCSUM		IFCAP_BIT(IFCAP_B_TXCSUM)
#define IFCAP_NETCONS		IFCAP_BIT(IFCAP_B_NETCONS)
#define IFCAP_VLAN_MTU		IFCAP_BIT(IFCAP_B_VLAN_MTU)
#define IFCAP_VLAN_HWTAGGING	IFCAP_BIT(IFCAP_B_VLAN_HWTAGGING)
#define IFCAP_JUMBO_MTU		IFCAP_BIT(IFCAP_B_JUMBO_MTU)
#define IFCAP_POLLING		IFCAP_BIT(IFCAP_B_POLLING)
#define IFCAP_VLAN_HWCSUM	IFCAP_BIT(IFCAP_B_VLAN_HWCSUM)
#define IFCAP_TSO4		IFCAP_BIT(IFCAP_B_TSO4)
#define IFCAP_TSO6		IFCAP_BIT(IFCAP_B_TSO6)
#define IFCAP_LRO		IFCAP_BIT(IFCAP_B_LRO)
#define IFCAP_WOL_UCAST		IFCAP_BIT(IFCAP_B_WOL_UCAST)
#define IFCAP_WOL_MCAST		IFCAP_BIT(IFCAP_B_WOL_MCAST)
#define IFCAP_WOL_MAGIC		IFCAP_BIT(IFCAP_B_WOL_MAGIC)
#define IFCAP_TOE4		IFCAP_BIT(IFCAP_B_TOE4)
#define IFCAP_TOE6		IFCAP_BIT(IFCAP_B_TOE6)
#define IFCAP_VLAN_HWFILTER	IFCAP_BIT(IFCAP_B_VLAN_HWFILTER)
#define IFCAP_NV		IFCAP_BIT(IFCAP_B_NV)
#define IFCAP_VLAN_HWTSO	IFCAP_BIT(IFCAP_B_VLAN_HWTSO)
#define IFCAP_LINKSTATE		IFCAP_BIT(IFCAP_B_LINKSTATE)
#define IFCAP_NETMAP		IFCAP_BIT(IFCAP_B_NETMAP)
#define IFCAP_RXCSUM_IPV6	IFCAP_BIT(IFCAP_B_RXCSUM_IPV6)
#define IFCAP_TXCSUM_IPV6	IFCAP_BIT(IFCAP_B_TXCSUM_IPV6)
#define IFCAP_HWSTATS		IFCAP_BIT(IFCAP_B_HWSTATS)
#define IFCAP_TXRTLMT		IFCAP_BIT(IFCAP_B_TXRTLMT)
#define IFCAP_HWRXTSTMP		IFCAP_BIT(IFCAP_B_HWRXTSTMP)
#define IFCAP_MEXTPG		IFCAP_BIT(IFCAP_B_MEXTPG)
#define IFCAP_TXTLS4		IFCAP_BIT(IFCAP_B_TXTLS4)
#define IFCAP_TXTLS6		IFCAP_BIT(IFCAP_B_TXTLS6)
#define IFCAP_VXLAN_HWCSUM	IFCAP_BIT(IFCAP_B_VXLAN_HWCSUM)
#define IFCAP_VXLAN_HWTSO	IFCAP_BIT(IFCAP_B_VXLAN_HWTSO)
#define IFCAP_TXTLS_RTLMT	IFCAP_BIT(IFCAP_B_TXTLS_RTLMT)
#define IFCAP_HWCSUM_IPV6	(IFCAP_RXCSUM_IPV6 | IFCAP_TXCSUM_IPV6)
#define IFCAP_HWCSUM		(IFCAP_RXCSUM | IFCAP_TXCSUM)
#define IFCAP_TSO		(IFCAP_TSO4 | IFCAP_TSO6)
#define IFCAP_WOL		(IFCAP_WOL_UCAST | IFCAP_WOL_MCAST | IFCAP_WOL_MAGIC)
#define IFCAP_TOE		(IFCAP_TOE4 | IFCAP_TOE6)
#define IFCAP_TXTLS		(IFCAP_TXTLS4 | IFCAP_TXTLS6)
#define IFCAP_CANTCHANGE	(IFCAP_NETMAP | IFCAP_NV)
#define IFCAP_ALLCAPS		0xffffffff
#define IFCAP2_BIT(x)		(1UL << ((x) - 32))
#define IFCAP2_RXTLS4		IFCAP2_BIT(IFCAP_B_RXTLS4)
#define IFCAP2_RXTLS6		IFCAP2_BIT(IFCAP_B_RXTLS6)
#define IFCAP2_IPSEC_OFFLOAD	IFCAP2_BIT(IFCAP_B_IPSEC_OFFLOAD)

#define IFQ_MAXLEN	50
#define IFNET_SLOWHZ	1

/* Interface counters (net/if_var.h in FreeBSD; here so if.h is enough). */
typedef enum {
	IFCOUNTER_IPACKETS = 0,
	IFCOUNTER_IERRORS,
	IFCOUNTER_OPACKETS,
	IFCOUNTER_OERRORS,
	IFCOUNTER_COLLISIONS,
	IFCOUNTER_IBYTES,
	IFCOUNTER_OBYTES,
	IFCOUNTER_IMCASTS,
	IFCOUNTER_OMCASTS,
	IFCOUNTER_IQDROPS,
	IFCOUNTER_OQDROPS,
	IFCOUNTER_NOPROTO,
	IFCOUNTERS /* Array size. */
} ift_counter;

struct ifreq_buffer {
	size_t	length;
	void	*buffer;
};

struct ifreq_nv_req {
	u_int	buf_length;
	u_int	length;
	void	*buffer;
};

struct ifreq {
	char	ifr_name[IFNAMSIZ];
	union {
		struct sockaddr	ifru_addr;
		struct sockaddr	ifru_dstaddr;
		struct sockaddr	ifru_broadaddr;
		struct ifreq_buffer ifru_buffer;
		short	ifru_flags[2];
		short	ifru_index;
		int	ifru_jid;
		int	ifru_metric;
		int	ifru_mtu;
		int	ifru_phys;
		int	ifru_media;
		caddr_t	ifru_data;
		int	ifru_cap[2];
		u_int	ifru_fib;
		u_char	ifru_vlan_pcp;
		struct ifreq_nv_req ifru_nv;
	} ifr_ifru;
#define ifr_addr	ifr_ifru.ifru_addr
#define ifr_dstaddr	ifr_ifru.ifru_dstaddr
#define ifr_broadaddr	ifr_ifru.ifru_broadaddr
#define ifr_flags	ifr_ifru.ifru_flags[0]
#define ifr_flagshigh	ifr_ifru.ifru_flags[1]
#define ifr_jid		ifr_ifru.ifru_jid
#define ifr_metric	ifr_ifru.ifru_metric
#define ifr_mtu		ifr_ifru.ifru_mtu
#define ifr_phys	ifr_ifru.ifru_phys
#define ifr_media	ifr_ifru.ifru_media
#define ifr_data	ifr_ifru.ifru_data
#define ifr_reqcap	ifr_ifru.ifru_cap[0]
#define ifr_curcap	ifr_ifru.ifru_cap[1]
#define ifr_index	ifr_ifru.ifru_index
#define ifr_fib		ifr_ifru.ifru_fib
#define ifr_vlan_pcp	ifr_ifru.ifru_vlan_pcp
#define ifr_lan_pcp	ifr_ifru.ifru_vlan_pcp
#define ifr_cap_nv	ifr_ifru.ifru_nv
};

static __inline void *
ifr_data_get_ptr(struct ifreq *ifrp)
{
	return (ifrp->ifr_ifru.ifru_data);
}

static __inline void *
ifr_buffer_get_buffer(struct ifreq *ifrp)
{
	return (ifrp->ifr_ifru.ifru_buffer.buffer);
}

static __inline size_t
ifr_buffer_get_length(struct ifreq *ifrp)
{
	return (ifrp->ifr_ifru.ifru_buffer.length);
}

static __inline void
ifr_buffer_set_length(struct ifreq *ifrp, size_t len)
{
	ifrp->ifr_ifru.ifru_buffer.length = len;
}

struct ifaliasreq {
	char	ifra_name[IFNAMSIZ];
	struct	sockaddr ifra_addr;
	struct	sockaddr ifra_broadaddr;
	struct	sockaddr ifra_mask;
	int	ifra_vhid;
};

struct ifmediareq {
	char	ifm_name[IFNAMSIZ];
	int	ifm_current;
	int	ifm_mask;
	int	ifm_status;
	int	ifm_active;
	int	ifm_count;
	int	*ifm_ulist;
};

struct ifdrv {
	char		ifd_name[IFNAMSIZ];
	unsigned long	ifd_cmd;
	size_t		ifd_len;
	void		*ifd_data;
};

struct ifstat {
	char	ifs_name[IFNAMSIZ];
	char	ascii[800];
};

struct ifgroupreq {
	char	ifgr_name[IFNAMSIZ];
	u_int	ifgr_len;
	union {
		char	ifgru_group[IFNAMSIZ];
		void	*ifgru_groups;
	} ifgr_ifgru;
};

struct if_msghdr {
	u_short	ifm_msglen;
	u_char	ifm_version;
	u_char	ifm_type;
	int	ifm_addrs;
	int	ifm_flags;
	u_short	ifm_index;
	u_short	_ifm_spare1;
	struct	if_data ifm_data;
};

struct if_announcemsghdr {
	u_short	ifan_msglen;
	u_char	ifan_version;
	u_char	ifan_type;
	u_short	ifan_index;
	char	ifan_name[IFNAMSIZ];
	u_short	ifan_what;
};
#define IFAN_ARRIVAL	0
#define IFAN_DEPARTURE	1

#define RSS_FUNC_NONE		0
#define RSS_FUNC_PRIVATE	1
#define RSS_FUNC_TOEPLITZ	2
#define RSS_TYPE_IPV4		0x00000001
#define RSS_TYPE_TCP_IPV4	0x00000002
#define RSS_TYPE_IPV6		0x00000004
#define RSS_TYPE_IPV6_EX	0x00000008
#define RSS_TYPE_TCP_IPV6	0x00000010
#define RSS_TYPE_TCP_IPV6_EX	0x00000020
#define RSS_TYPE_UDP_IPV4	0x00000040
#define RSS_TYPE_UDP_IPV6	0x00000080
#define RSS_TYPE_UDP_IPV6_EX	0x00000100
#define RSS_KEYLEN		128

#endif /* _BSDKPI_NET_IF_H_ */
