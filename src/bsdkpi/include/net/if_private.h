/* SPDX-License-Identifier: MIT */
/*
 * net/if_private.h - struct ifnet.
 *
 * A minimal network interface object: the fields and methods net80211
 * touches directly (if_softc, if_flags, if_drv_flags, if_init/if_ioctl/
 * if_transmit/if_output/if_input/if_qflush/if_get_counter, if_mtu, ...),
 * the link-level address kept as a sockaddr_dl under if_addr so that
 * IF_LLADDR() works, per-interface counters and the link-state task.
 * There is no interface list, routing or address management: phase 4's
 * IO80211 layer owns the system-visible interface and binds if_input /
 * if_transmit and the bsdkpi_ifnet_hooks (bsdkpi/bsdkpi.h).
 */
#ifndef _BSDKPI_NET_IF_PRIVATE_H_
#define _BSDKPI_NET_IF_PRIVATE_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/mutex.h>
#include <sys/_task.h>
#include <sys/counter.h>
#include <net/if.h>
#include <net/if_dl.h>

struct mbuf;
struct route;
struct sockaddr;
struct bpf_if;
struct vnet;
struct ifmultiaddr;
struct ifaddr;
struct ifnet;

typedef void	(*if_start_fn_t)(if_t);
typedef int	(*if_ioctl_fn_t)(if_t, u_long, caddr_t);
typedef void	(*if_init_fn_t)(void *);
typedef void	(*if_input_fn_t)(struct ifnet *, struct mbuf *);
typedef int	(*if_output_fn_t)(struct ifnet *, struct mbuf *,
		    const struct sockaddr *, struct route *);
typedef void	(*if_qflush_fn_t)(if_t);
typedef int	(*if_transmit_fn_t)(if_t, struct mbuf *);
typedef uint64_t (*if_get_counter_t)(if_t, ift_counter);
typedef void	(*if_reassign_fn_t)(if_t, struct vnet *, char *);
typedef void	(*if_vf_status_fn_t)(if_t, void *);

struct ifaddr {
	struct sockaddr	*ifa_addr;	/* address of interface */
	struct sockaddr	*ifa_dstaddr;	/* other end of p-to-p link */
	struct sockaddr	*ifa_netmask;	/* used to determine subnet */
	struct ifnet	*ifa_ifp;	/* back-pointer to interface */
	STAILQ_ENTRY(ifaddr) ifa_link;
	u_short		 ifa_flags;
	int		 ifa_metric;
	u_int		 ifa_refcnt;
};
#define ifa_broadaddr	ifa_dstaddr

struct ifmultiaddr {
	STAILQ_ENTRY(ifmultiaddr) ifma_link;
	struct sockaddr	*ifma_addr;
	struct sockaddr	*ifma_lladdr;
	struct ifnet	*ifma_ifp;
	u_int		 ifma_refcount;
	int		 ifma_flags;
	void		*ifma_protospec;
	struct ifmultiaddr *ifma_llifma;
	struct sockaddr_dl ifma_sdl;	/* storage for ifma_addr */
};

STAILQ_HEAD(ifaddrhead, ifaddr);
STAILQ_HEAD(ifmultihead, ifmultiaddr);

struct ifnet {
	/* identity */
	void		*if_softc;	/* pointer to driver state (the vap) */
	void		*if_llsoftc;	/* link layer softc */
	void		*if_l2com;	/* pointer to protocol bits */
	const char	*if_dname;	/* driver name */
	int		 if_dunit;	/* unit or IF_DUNIT_NONE */
	u_short		 if_index;	/* numeric abbreviation for this if */
	short		 if_index_reserved;
	char		 if_xname[IFNAMSIZ]; /* external name (name + unit) */
	char		*if_description;
	struct vnet	*if_vnet;	/* always NULL: no vnets */
	struct vnet	*if_home_vnet;
	void		*if_bridge;	/* bridge glue (always NULL) */
	struct bpf_if	*if_bpf;	/* packet filter structure */
	void		*if_afdata[AF_MAX];
	u_int		 if_refcount;

	/* type and status */
	int		 if_flags;	/* up/down, broadcast, etc. */
	int		 if_drv_flags;	/* driver-managed status flags */
	int		 if_capabilities;
	int		 if_capenable;
	u_int		 if_capabilities2;
	u_int		 if_capenable2;
	uint8_t		 if_type;	/* IFT_* */
	uint8_t		 if_alloctype;
	uint8_t		 if_addrlen;	/* media address length */
	uint8_t		 if_hdrlen;	/* media header length */
	uint8_t		 if_link_state;	/* LINK_STATE_* */
	uint32_t	 if_mtu;
	uint32_t	 if_metric;
	uint64_t	 if_baudrate;
	uint64_t	 if_hwassist;	/* HW offload capabilities */
	u_int		 if_fib;
	u_int		 if_hw_tsomax;
	u_int		 if_hw_tsomaxsegcount;
	u_int		 if_hw_tsomaxsegsize;
	time_t		 if_epoch;	/* uptime second this interface attached */
	struct timeval	 if_lastchange;
	uint8_t		 if_pcp;

	/* methods */
	if_output_fn_t	 if_output;	/* output routine (enqueue) */
	if_input_fn_t	 if_input;	/* input routine (from h/w driver) */
	if_start_fn_t	 if_start;	/* initiate output routine */
	if_ioctl_fn_t	 if_ioctl;	/* ioctl routine */
	if_init_fn_t	 if_init;	/* Init routine */
	if_qflush_fn_t	 if_qflush;	/* flush any queue */
	if_transmit_fn_t if_transmit;	/* initiate output routine */
	if_get_counter_t if_get_counter; /* get counter values */
	if_reassign_fn_t if_reassign;

	/* addresses */
	struct ifaddrhead if_addrhead;	/* linked list of addresses per if */
	struct ifaddr	*if_addr;	/* pointer to link-level address */
	struct ifaddr	 if_lladdr_ifa;	/* storage for if_addr */
	struct sockaddr_dl if_lladdr_sdl; /* storage for its sockaddr_dl */
	const uint8_t	*if_broadcastaddr;
	struct ifmultihead if_multiaddrs;
	int		 if_amcount;	/* number of all-multicast requests */
	struct mtx	 if_addr_lock;

	/* counters and deferred work */
	uint64_t	 if_counters[IFCOUNTERS];
	struct task	 if_linktask;	/* task for link change events */
	struct task	 if_addmultitask;
	struct mtx	 if_lock;	/* generic per-interface lock */
	void		*if_netmap;
	void		*if_debugnet_methods;
	void		*if_pspare[4];
};

#define IF_DUNIT_NONE	-1

/* if_private.h-level helpers used in-tree. */
#define IF_AFDATA_LOCK_INIT(ifp)	do { } while (0)
#define IF_AFDATA_LOCK_DESTROY(ifp)	do { } while (0)
#define IF_ADDR_LOCK_INIT(if)		mtx_init(&(if)->if_addr_lock, "if_addr_lock", NULL, MTX_DEF)
#define IF_ADDR_LOCK_DESTROY(if)	mtx_destroy(&(if)->if_addr_lock)
#define IF_ADDR_WLOCK(if)		mtx_lock(&(if)->if_addr_lock)
#define IF_ADDR_WUNLOCK(if)		mtx_unlock(&(if)->if_addr_lock)
#define IF_ADDR_RLOCK(if)		mtx_lock(&(if)->if_addr_lock)
#define IF_ADDR_RUNLOCK(if)		mtx_unlock(&(if)->if_addr_lock)
#define IF_ADDR_LOCK_ASSERT(if)		mtx_assert(&(if)->if_addr_lock, MA_OWNED)
#define IF_ADDR_WLOCK_ASSERT(if)	mtx_assert(&(if)->if_addr_lock, MA_OWNED)

#define IF_LLADDR(ifp)	LLADDR((struct sockaddr_dl *)((ifp)->if_addr->ifa_addr))

#endif /* _BSDKPI_NET_IF_PRIVATE_H_ */
