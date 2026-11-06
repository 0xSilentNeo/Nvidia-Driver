// SPDX-License-Identifier: MIT
/*
 * struct ifnet: the network interface object net80211 attaches each vap to.
 *
 * There is no system interface list behind this: the IO80211 layer owns
 * the macOS-visible interface and binds the vap's ifp to it through the
 * if_input/if_transmit methods and the bsdkpi_ifnet_hooks.  What lives
 * here is the state net80211 reads and writes (flags, MTU, link-level
 * address, counters, link state), the if_* accessor API, ether_ifattach's
 * Ethernet defaults, the "wlan" interface cloner and the routing-socket
 * notifications turned into hook callbacks.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/mutex.h>
#include <sys/taskqueue.h>
#include <sys/eventhandler.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_private.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_clone.h>
#include <net/if_media.h>
#include <net/ethernet.h>
#include <net/route.h>
#include <net/bpf.h>
#include <bsdkpi/bsdkpi.h>

int bsdkpi_if_index;
int ifqmaxlen = IFQ_MAXLEN;

static struct bsdkpi_ifnet_hooks bsdkpi_hooks;
static STAILQ_HEAD(, ifnet_entry) bsdkpi_ifnets =
    STAILQ_HEAD_INITIALIZER(bsdkpi_ifnets);
static DEFINE_MUTEX(bsdkpi_ifnet_mtx);

struct ifnet_entry {
	struct ifnet			*ifp;
	STAILQ_ENTRY(ifnet_entry)	 link;
};

void
bsdkpi_ifnet_set_hooks(const struct bsdkpi_ifnet_hooks *hooks)
{
	if (hooks != NULL)
		bsdkpi_hooks = *hooks;
	else
		memset(&bsdkpi_hooks, 0, sizeof(bsdkpi_hooks));
}

/* ---- defaults ------------------------------------------------------- */

static const uint8_t etherbroadcastaddr[ETHER_ADDR_LEN] =
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static void
if_input_default(struct ifnet *ifp, struct mbuf *m)
{
	/* nobody above us yet: count and drop */
	if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
	m_freem(m);
}

static int
if_transmit_default(struct ifnet *ifp, struct mbuf *m)
{
	if_inc_counter(ifp, IFCOUNTER_OQDROPS, 1);
	m_freem(m);
	return (ENETDOWN);
}

static int
if_output_default(struct ifnet *ifp, struct mbuf *m,
    const struct sockaddr *dst, struct route *ro)
{
	(void)dst;
	(void)ro;
	return (ifp->if_transmit(ifp, m));
}

static void
if_qflush_default(struct ifnet *ifp)
{
	(void)ifp;
}

static int
if_ioctl_default(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	(void)ifp;
	(void)cmd;
	(void)data;
	return (EOPNOTSUPP);
}

static void
if_init_default(void *arg)
{
	(void)arg;
}

static void
do_link_state_change(void *arg, int pending)
{
	struct ifnet *ifp = arg;

	(void)pending;
	if (bsdkpi_hooks.link_state != NULL)
		bsdkpi_hooks.link_state(ifp, ifp->if_link_state);
	rt_ifmsg(ifp, 0);
	EVENTHANDLER_INVOKE(ifnet_link_event, ifp, ifp->if_link_state);
}

/* ---- lifecycle ------------------------------------------------------ */

if_t
if_alloc(u_char type)
{
	struct ifnet *ifp;
	struct ifnet_entry *e;

	ifp = malloc(sizeof(*ifp), M_IFADDR, M_WAITOK | M_ZERO);
	if (ifp == NULL)
		return (NULL);
	e = malloc(sizeof(*e), M_IFADDR, M_WAITOK | M_ZERO);
	if (e == NULL) {
		free(ifp, M_IFADDR);
		return (NULL);
	}
	ifp->if_type = type;
	ifp->if_alloctype = type;
	ifp->if_refcount = 1;
	ifp->if_dunit = IF_DUNIT_NONE;
	ifp->if_link_state = LINK_STATE_UNKNOWN;
	ifp->if_fib = 0;
	ifp->if_epoch = time_uptime;
	ifp->if_mtu = ETHERMTU;
	STAILQ_INIT(&ifp->if_addrhead);
	STAILQ_INIT(&ifp->if_multiaddrs);
	IF_LOCK_INIT(ifp);
	IF_ADDR_LOCK_INIT(ifp);
	TASK_INIT(&ifp->if_linktask, 0, do_link_state_change, ifp);
	ifp->if_input = if_input_default;
	ifp->if_transmit = if_transmit_default;
	ifp->if_output = if_output_default;
	ifp->if_qflush = if_qflush_default;
	ifp->if_ioctl = if_ioctl_default;
	ifp->if_init = if_init_default;
	ifp->if_get_counter = if_get_counter_default;
	ifp->if_broadcastaddr = etherbroadcastaddr;

	/* the link-level address lives in the embedded sockaddr_dl */
	ifp->if_lladdr_sdl.sdl_len = sizeof(ifp->if_lladdr_sdl);
	ifp->if_lladdr_sdl.sdl_family = AF_LINK;
	ifp->if_lladdr_sdl.sdl_type = type;
	ifp->if_lladdr_ifa.ifa_addr = (struct sockaddr *)&ifp->if_lladdr_sdl;
	ifp->if_lladdr_ifa.ifa_ifp = ifp;
	ifp->if_lladdr_ifa.ifa_refcnt = 1;
	ifp->if_addr = &ifp->if_lladdr_ifa;
	STAILQ_INSERT_HEAD(&ifp->if_addrhead, &ifp->if_lladdr_ifa, ifa_link);

	mutex_lock(&bsdkpi_ifnet_mtx);
	ifp->if_index = (u_short)++bsdkpi_if_index;
	ifp->if_lladdr_sdl.sdl_index = ifp->if_index;
	e->ifp = ifp;
	STAILQ_INSERT_TAIL(&bsdkpi_ifnets, e, link);
	mutex_unlock(&bsdkpi_ifnet_mtx);
	return (ifp);
}

if_t
if_alloc_dev(u_char type, void *dev)
{
	(void)dev;
	return (if_alloc(type));
}

static void
if_free_internal(struct ifnet *ifp)
{
	struct ifnet_entry *e;

	mutex_lock(&bsdkpi_ifnet_mtx);
	STAILQ_FOREACH(e, &bsdkpi_ifnets, link) {
		if (e->ifp == ifp) {
			STAILQ_REMOVE(&bsdkpi_ifnets, e, ifnet_entry, link);
			free(e, M_IFADDR);
			break;
		}
	}
	mutex_unlock(&bsdkpi_ifnet_mtx);
	if_delallmulti(ifp);
	taskqueue_drain(taskqueue_swi, &ifp->if_linktask);
	IF_ADDR_LOCK_DESTROY(ifp);
	IF_LOCK_DESTROY(ifp);
	if (ifp->if_description != NULL)
		free(ifp->if_description, M_IFADDR);
	free(ifp, M_IFADDR);
}

void
if_free(if_t ifp)
{
	ifp->if_flags |= IFF_DYING;
	if_rele(ifp);
}

void
if_ref(if_t ifp)
{
	(void)__atomic_fetch_add(&ifp->if_refcount, 1, __ATOMIC_SEQ_CST);
}

void
if_rele(if_t ifp)
{
	if (__atomic_fetch_sub(&ifp->if_refcount, 1, __ATOMIC_SEQ_CST) == 1)
		if_free_internal(ifp);
}

if_t
bsdkpi_ifnet_first(void)
{
	struct ifnet_entry *e = STAILQ_FIRST(&bsdkpi_ifnets);

	return (e != NULL ? e->ifp : NULL);
}

if_t
bsdkpi_ifnet_next(if_t ifp)
{
	struct ifnet_entry *e;

	STAILQ_FOREACH(e, &bsdkpi_ifnets, link)
		if (e->ifp == ifp) {
			e = STAILQ_NEXT(e, link);
			return (e != NULL ? e->ifp : NULL);
		}
	return (NULL);
}

if_t
ifunit(const char *name)
{
	struct ifnet_entry *e;
	struct ifnet *ifp = NULL;

	mutex_lock(&bsdkpi_ifnet_mtx);
	STAILQ_FOREACH(e, &bsdkpi_ifnets, link) {
		if (strncmp(e->ifp->if_xname, name, IFNAMSIZ) == 0) {
			ifp = e->ifp;
			break;
		}
	}
	mutex_unlock(&bsdkpi_ifnet_mtx);
	return (ifp);
}

if_t
ifunit_ref(const char *name)
{
	struct ifnet *ifp = ifunit(name);

	if (ifp != NULL)
		if_ref(ifp);
	return (ifp);
}

void
if_initname(if_t ifp, const char *name, int unit)
{
	ifp->if_dname = name;
	ifp->if_dunit = unit;
	if (unit != IF_DUNIT_NONE)
		snprintf(ifp->if_xname, IFNAMSIZ, "%s%d", name, unit);
	else
		strlcpy(ifp->if_xname, name, IFNAMSIZ);
	ifp->if_lladdr_sdl.sdl_nlen = (u_char)strlen(ifp->if_xname);
	memcpy(ifp->if_lladdr_sdl.sdl_data, ifp->if_xname,
	    ifp->if_lladdr_sdl.sdl_nlen);
}

void
if_attach(if_t ifp)
{
	ifp->if_epoch = time_uptime;
	getmicrotime(&ifp->if_lastchange);
	EVENTHANDLER_INVOKE(ifnet_arrival_event, ifp);
}

void
if_detach(if_t ifp)
{
	EVENTHANDLER_INVOKE(ifnet_departure_event, ifp);
	taskqueue_drain(taskqueue_swi, &ifp->if_linktask);
}

void
if_dead(if_t ifp)
{
	ifp->if_input = if_input_default;
	ifp->if_transmit = if_transmit_default;
	ifp->if_output = if_output_default;
	ifp->if_ioctl = if_ioctl_default;
	ifp->if_qflush = if_qflush_default;
	ifp->if_init = if_init_default;
}

void
if_up(if_t ifp)
{
	ifp->if_flags |= IFF_UP;
	rt_ifmsg(ifp, IFF_UP);
}

void
if_down(if_t ifp)
{
	ifp->if_flags &= ~IFF_UP;
	ifp->if_qflush(ifp);
	rt_ifmsg(ifp, IFF_UP);
}

void
if_purgeaddrs(if_t ifp)
{
	(void)ifp;
}

void
if_register_com_alloc(u_char type, if_com_alloc_t a, if_com_free_t f)
{
	(void)type; (void)a; (void)f;
}

void
if_deregister_com_alloc(u_char type)
{
	(void)type;
}

struct ifaddr *
ifa_ifwithaddr(const struct sockaddr *addr)
{
	(void)addr;
	return (NULL);
}

/* ---- Ethernet glue -------------------------------------------------- */

static int
ether_output_default(struct ifnet *ifp, struct mbuf *m,
    const struct sockaddr *dst, struct route *ro)
{
	struct ether_header *eh;
	const char *hdr = NULL;
	u_int hlen = 0;

	/*
	 * The upper layer (the IO80211 interface in phase 4) hands down
	 * complete Ethernet frames through if_transmit; this path is only
	 * reached by net80211's own raw-frame injection (bpf write) and by
	 * callers that pass a preformed header through struct route or an
	 * AF_UNSPEC sockaddr, exactly as FreeBSD's ether_output accepts.
	 */
	if (ro != NULL && (ro->ro_flags & RT_HAS_HEADER)) {
		hdr = ro->ro_prepend;
		hlen = ro->ro_plen;
	} else if (dst != NULL && (dst->sa_family == AF_UNSPEC ||
	    dst->sa_family == pseudo_AF_HDRCMPLT)) {
		hdr = dst->sa_data;
		hlen = ETHER_HDR_LEN;
	} else if (dst != NULL) {
		m_freem(m);
		return (EAFNOSUPPORT);
	}
	if (hlen != 0) {
		M_PREPEND(m, (int)hlen, M_NOWAIT);
		if (m == NULL)
			return (ENOBUFS);
		memcpy(mtod(m, void *), hdr, hlen);
		if (dst != NULL && dst->sa_family == AF_UNSPEC) {
			eh = mtod(m, struct ether_header *);
			memcpy(eh->ether_shost, IF_LLADDR(ifp), ETHER_ADDR_LEN);
		}
	}
	return (ifp->if_transmit(ifp, m));
}

void
ether_ifattach(struct ifnet *ifp, const u_int8_t *lla)
{
	ifp->if_addrlen = ETHER_ADDR_LEN;
	ifp->if_hdrlen = ETHER_HDR_LEN;
	ifp->if_mtu = ETHERMTU;
	if (ifp->if_output == if_output_default)
		ifp->if_output = ether_output_default;
	ifp->if_input = if_input_default;
	ifp->if_broadcastaddr = etherbroadcastaddr;
	ifp->if_lladdr_sdl.sdl_type = IFT_ETHER;
	ifp->if_lladdr_sdl.sdl_alen = ETHER_ADDR_LEN;
	memcpy(LLADDR(&ifp->if_lladdr_sdl), lla, ETHER_ADDR_LEN);
	if_attach(ifp);
	if (bsdkpi_hooks.attach != NULL)
		bsdkpi_hooks.attach(ifp);
	EVENTHANDLER_INVOKE(ether_ifattach_event, ifp);
	rt_ifannouncemsg(ifp, IFAN_ARRIVAL);
}

void
ether_ifdetach(struct ifnet *ifp)
{
	if (bsdkpi_hooks.detach != NULL)
		bsdkpi_hooks.detach(ifp);
	rt_ifannouncemsg(ifp, IFAN_DEPARTURE);
	if_detach(ifp);
}

int
ether_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch (command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		ifp->if_init(ifp->if_softc);
		break;
	case SIOCGIFADDR:
		memcpy(&ifr->ifr_addr.sa_data[0], IF_LLADDR(ifp), ETHER_ADDR_LEN);
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu > ETHERMTU)
			error = EINVAL;
		else
			ifp->if_mtu = (uint32_t)ifr->ifr_mtu;
		break;
	case SIOCSLANPCP:
		error = EINVAL;
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

int
ether_output(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *dst,
    struct route *ro)
{
	return (ether_output_default(ifp, m, dst, ro));
}

int
ether_output_frame(struct ifnet *ifp, struct mbuf *m)
{
	return (ifp->if_transmit(ifp, m));
}

void
ether_demux(struct ifnet *ifp, struct mbuf *m)
{
	if_input(ifp, m);
}

char *
ether_sprintf(const u_int8_t *ap)
{
	static char etherbuf[8][18];
	static volatile unsigned int next;
	char *cp;

	cp = etherbuf[__atomic_fetch_add(&next, 1, __ATOMIC_RELAXED) % 8];
	snprintf(cp, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
	    ap[0], ap[1], ap[2], ap[3], ap[4], ap[5]);
	return (cp);
}

uint32_t
ether_crc32_le(const uint8_t *buf, size_t len)
{
	size_t i;
	uint32_t crc = 0xffffffffU;
	int bit;
	uint8_t data;

	for (i = 0; i < len; i++) {
		for (data = *buf++, bit = 0; bit < 8; bit++, data >>= 1) {
			crc = (crc >> 1) ^ (((crc ^ data) & 1) ? 0xedb88320U : 0);
		}
	}
	return (crc);
}

uint32_t
ether_crc32_be(const uint8_t *buf, size_t len)
{
	size_t i;
	uint32_t crc, carry;
	int bit;
	uint8_t data;

	crc = 0xffffffffU;
	for (i = 0; i < len; i++) {
		for (data = *buf++, bit = 0; bit < 8; bit++, data >>= 1) {
			carry = ((crc & 0x80000000U) ? 1 : 0) ^ (data & 0x01);
			crc <<= 1;
			if (carry)
				crc = (crc ^ 0x04c11db6U) | carry;
		}
	}
	return (crc);
}

struct mbuf *
ether_vlanencap_proto(struct mbuf *m, uint16_t tag, uint16_t proto)
{
	struct ether_vlan_header *evl;

	M_PREPEND(m, ETHER_VLAN_ENCAP_LEN, M_NOWAIT);
	if (m == NULL)
		return (NULL);
	if (m->m_len < (int)sizeof(*evl)) {
		m = m_pullup(m, (int)sizeof(*evl));
		if (m == NULL)
			return (NULL);
	}
	evl = mtod(m, struct ether_vlan_header *);
	memmove(evl, (char *)evl + ETHER_VLAN_ENCAP_LEN, ETHER_HDR_LEN - ETHER_TYPE_LEN);
	evl->evl_encap_proto = htons(proto);
	evl->evl_tag = htons(tag);
	return (m);
}

void
ether_gen_addr(struct ifnet *ifp, struct ether_addr *hwaddr)
{
	(void)ifp;
	arc4random_buf(hwaddr->octet, ETHER_ADDR_LEN);
	hwaddr->octet[0] &= 0xfe;	/* unicast */
	hwaddr->octet[0] |= 0x02;	/* locally administered */
}

/* ---- the if_* accessors --------------------------------------------- */

uint64_t
if_setbaudrate(if_t ifp, uint64_t baudrate)
{
	uint64_t old = ifp->if_baudrate;

	ifp->if_baudrate = baudrate;
	return (old);
}

uint64_t if_getbaudrate(const if_t ifp) { return (ifp->if_baudrate); }

int
if_setcapabilities(if_t ifp, int capabilities)
{
	ifp->if_capabilities = capabilities;
	return (0);
}

int
if_setcapabilitiesbit(if_t ifp, int setbit, int clearbit)
{
	ifp->if_capabilities &= ~clearbit;
	ifp->if_capabilities |= setbit;
	return (0);
}

int if_getcapabilities(const if_t ifp) { return (ifp->if_capabilities); }

int
if_setcapenable(if_t ifp, int capabilities)
{
	ifp->if_capenable = capabilities;
	return (0);
}

int
if_setcapenablebit(if_t ifp, int setcap, int clearcap)
{
	ifp->if_capenable &= ~clearcap;
	ifp->if_capenable |= setcap;
	return (0);
}

int if_getcapenable(const if_t ifp) { return (ifp->if_capenable); }

int
if_togglecapenable(if_t ifp, int togglecap)
{
	ifp->if_capenable ^= togglecap;
	return (0);
}

int
if_setcapabilities2(if_t ifp, int capabilities)
{
	ifp->if_capabilities2 = (u_int)capabilities;
	return (0);
}

int
if_setcapabilities2bit(if_t ifp, int setbit, int clearbit)
{
	ifp->if_capabilities2 &= ~(u_int)clearbit;
	ifp->if_capabilities2 |= (u_int)setbit;
	return (0);
}

int if_getcapabilities2(const if_t ifp) { return ((int)ifp->if_capabilities2); }

int
if_togglecapenable2(if_t ifp, int togglecap)
{
	ifp->if_capenable2 ^= (u_int)togglecap;
	return (0);
}

int
if_setcapenable2(if_t ifp, int capabilities)
{
	ifp->if_capenable2 = (u_int)capabilities;
	return (0);
}

int
if_setcapenable2bit(if_t ifp, int setcap, int clearcap)
{
	ifp->if_capenable2 &= ~(u_int)clearcap;
	ifp->if_capenable2 |= (u_int)setcap;
	return (0);
}

int if_getcapenable2(const if_t ifp) { return ((int)ifp->if_capenable2); }
int if_getdunit(const if_t ifp) { return (ifp->if_dunit); }
int if_getindex(const if_t ifp) { return (ifp->if_index); }
int if_getidxgen(const if_t ifp) { (void)ifp; return (0); }
const char *if_getdname(const if_t ifp) { return (ifp->if_dname); }
void if_setdname(if_t ifp, const char *dname) { ifp->if_dname = dname; }
const char *if_name(if_t ifp) { return (ifp->if_xname); }

int
if_setname(if_t ifp, const char *name)
{
	if (strlen(name) > sizeof(ifp->if_xname) - 1)
		return (ENAMETOOLONG);
	strcpy(ifp->if_xname, name);
	return (0);
}

int
if_rename(if_t ifp, char *new_name)
{
	return (if_setname(ifp, new_name));
}

const char *if_getdescr(if_t ifp) { return (ifp->if_description); }

void
if_setdescr(if_t ifp, char *descrbuf)
{
	char *old = ifp->if_description;

	ifp->if_description = descrbuf;
	if (old != NULL)
		if_freedescr(old);
}

char *
if_allocdescr(size_t sz, int malloc_flag)
{
	return (malloc(sz, M_IFADDR, malloc_flag | M_ZERO));
}

void
if_freedescr(char *descrbuf)
{
	free(descrbuf, M_IFADDR);
}

void if_setlastchange(if_t ifp) { getmicrotime(&ifp->if_lastchange); }
int if_getalloctype(const if_t ifp) { return (ifp->if_alloctype); }
int if_gettype(const if_t ifp) { return (ifp->if_type); }
int if_setdev(if_t ifp, void *dev) { (void)ifp; (void)dev; return (0); }

int
if_setdrvflagbits(if_t ifp, int set_flags, int clear_flags)
{
	ifp->if_drv_flags &= ~clear_flags;
	ifp->if_drv_flags |= set_flags;
	return (0);
}

int if_getdrvflags(const if_t ifp) { return (ifp->if_drv_flags); }

int
if_setdrvflags(if_t ifp, int flags)
{
	ifp->if_drv_flags = flags;
	return (0);
}

int
if_setflags(if_t ifp, int flags)
{
	ifp->if_flags = flags;
	return (0);
}

int
if_setflagbits(if_t ifp, int set, int clear)
{
	ifp->if_flags &= ~clear;
	ifp->if_flags |= set;
	return (0);
}

int if_getflags(const if_t ifp) { return (ifp->if_flags); }
int if_getlinkstate(if_t ifp) { return (ifp->if_link_state); }

int
if_clearhwassist(if_t ifp)
{
	ifp->if_hwassist = 0;
	return (0);
}

int
if_sethwassistbits(if_t ifp, int toset, int toclear)
{
	ifp->if_hwassist &= ~(uint64_t)toclear;
	ifp->if_hwassist |= (uint64_t)toset;
	return (0);
}

int
if_sethwassist(if_t ifp, int hwassist_bit)
{
	ifp->if_hwassist = (uint64_t)hwassist_bit;
	return (0);
}

int if_gethwassist(const if_t ifp) { return ((int)ifp->if_hwassist); }

int
if_togglehwassist(if_t ifp, int toggle_bits)
{
	ifp->if_hwassist ^= (uint64_t)toggle_bits;
	return (0);
}

int
if_setsoftc(if_t ifp, void *softc)
{
	ifp->if_softc = softc;
	return (0);
}

void *if_getsoftc(if_t ifp) { return (ifp->if_softc); }
void if_setllsoftc(if_t ifp, void *llsoftc) { ifp->if_llsoftc = llsoftc; }
void *if_getllsoftc(if_t ifp) { return (ifp->if_llsoftc); }
u_int if_getfib(if_t ifp) { return (ifp->if_fib); }
uint8_t if_getaddrlen(if_t ifp) { return (ifp->if_addrlen); }

int
if_gethwaddr(const if_t ifp, struct ifreq *ifr)
{
	if (ifp->if_addrlen > sizeof(ifr->ifr_addr.sa_data))
		return (EINVAL);
	memcpy(ifr->ifr_addr.sa_data, IF_LLADDR(ifp), ifp->if_addrlen);
	return (0);
}

const uint8_t *if_getbroadcastaddr(const if_t ifp) { return (ifp->if_broadcastaddr); }
void if_setbroadcastaddr(if_t ifp, const uint8_t *addr) { ifp->if_broadcastaddr = addr; }

int
if_setmtu(if_t ifp, int mtu)
{
	ifp->if_mtu = (uint32_t)mtu;
	return (0);
}

int if_getmtu(const if_t ifp) { return ((int)ifp->if_mtu); }
int if_getmtu_family(if_t ifp, int family) { (void)family; return ((int)ifp->if_mtu); }
void if_notifymtu(if_t ifp) { rt_ifmsg(ifp, 0); }

void
if_setppromisc(const if_t ifp, bool ppromisc)
{
	if (ppromisc)
		ifp->if_flags |= IFF_PPROMISC;
	else
		ifp->if_flags &= ~IFF_PPROMISC;
}

int if_getnumadomain(if_t ifp) { (void)ifp; return (0); }
int if_sendq_empty(if_t ifp) { (void)ifp; return (1); }
int if_setsendqready(if_t ifp) { (void)ifp; return (0); }
int if_setsendqlen(if_t ifp, int tx_desc_count) { (void)ifp; (void)tx_desc_count; return (0); }
int if_sethwtsomax(if_t ifp, u_int v) { ifp->if_hw_tsomax = v; return (0); }
int if_sethwtsomaxsegcount(if_t ifp, u_int v) { ifp->if_hw_tsomaxsegcount = v; return (0); }
int if_sethwtsomaxsegsize(if_t ifp, u_int v) { ifp->if_hw_tsomaxsegsize = v; return (0); }
u_int if_gethwtsomax(const if_t ifp) { return (ifp->if_hw_tsomax); }
u_int if_gethwtsomaxsegcount(const if_t ifp) { return (ifp->if_hw_tsomaxsegcount); }
u_int if_gethwtsomaxsegsize(const if_t ifp) { return (ifp->if_hw_tsomaxsegsize); }
void if_setnetmapadapter(if_t ifp, struct netmap_adapter *na) { ifp->if_netmap = na; }
struct netmap_adapter *if_getnetmapadapter(if_t ifp) { return (ifp->if_netmap); }

void
if_input(if_t ifp, struct mbuf *m)
{
	ifp->if_input(ifp, m);
}

int
if_sendq_prepend(if_t ifp, struct mbuf *m)
{
	(void)ifp;
	m_freem(m);
	return (ENOBUFS);
}

struct mbuf *if_dequeue(if_t ifp) { (void)ifp; return (NULL); }

int
if_setifheaderlen(if_t ifp, int len)
{
	ifp->if_hdrlen = (uint8_t)len;
	return (0);
}

void if_setrcvif(struct mbuf *m, if_t ifp) { m->m_pkthdr.rcvif = ifp; }

void
if_setvtag(struct mbuf *m, u_int16_t tag)
{
	m->m_pkthdr.ether_vtag = tag;
}

u_int16_t if_getvtag(struct mbuf *m) { return (m->m_pkthdr.ether_vtag); }
int if_vlantrunkinuse(if_t ifp) { (void)ifp; return (0); }
char *if_getlladdr(const if_t ifp) { return (IF_LLADDR(ifp)); }
struct vnet *if_getvnet(const if_t ifp) { return (ifp->if_vnet); }
void *if_gethandle(u_char type) { return (if_alloc(type)); }
void if_vlancap(if_t ifp) { (void)ifp; }
int if_transmit(if_t ifp, struct mbuf *m) { return (ifp->if_transmit(ifp, m)); }
void if_init(if_t ifp, void *ctx) { ifp->if_init(ctx); }
int if_ioctl(if_t ifp, u_long cmd, void *data) { return (ifp->if_ioctl(ifp, cmd, data)); }

int
if_resolvemulti(if_t ifp, struct sockaddr **sap, struct sockaddr *sa)
{
	(void)ifp; (void)sap; (void)sa;
	return (EOPNOTSUPP);
}

uint64_t if_getcounter(if_t ifp, ift_counter counter) { return (ifp->if_get_counter(ifp, counter)); }
struct label *if_getmaclabel(if_t ifp) { (void)ifp; return (NULL); }
void if_setmaclabel(if_t ifp, struct label *label) { (void)ifp; (void)label; }
struct bpf_if *if_getbpf(if_t ifp) { return (ifp->if_bpf); }
uint8_t if_getpcp(if_t ifp) { return (ifp->if_pcp); }
void *if_getl2com(if_t ifp) { return (ifp->if_l2com); }
struct ifaddr *if_getifaddr(const if_t ifp) { return (ifp->if_addr); }

u_int
if_foreach_addr_type(if_t ifp, int type, if_addr_cb_t cb, void *cb_arg)
{
	struct ifaddr *ifa;
	u_int count = 0;

	STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != type)
			continue;
		count += cb(cb_arg, ifa, count);
	}
	return (count);
}

void if_setinitfn(if_t ifp, if_init_fn_t fn) { ifp->if_init = fn; }
void if_setinputfn(if_t ifp, if_input_fn_t fn) { ifp->if_input = fn; }
if_input_fn_t if_getinputfn(if_t ifp) { return (ifp->if_input); }
void if_setioctlfn(if_t ifp, if_ioctl_fn_t fn) { ifp->if_ioctl = fn; }
void if_setoutputfn(if_t ifp, if_output_fn_t fn) { ifp->if_output = fn; }
void if_setstartfn(if_t ifp, if_start_fn_t fn) { ifp->if_start = fn; }
if_start_fn_t if_getstartfn(if_t ifp) { return (ifp->if_start); }
void if_settransmitfn(if_t ifp, if_transmit_fn_t fn) { ifp->if_transmit = fn; }
if_transmit_fn_t if_gettransmitfn(if_t ifp) { return (ifp->if_transmit); }
void if_setqflushfn(if_t ifp, if_qflush_fn_t fn) { ifp->if_qflush = fn; }
if_qflush_fn_t if_getqflushfn(if_t ifp) { return (ifp->if_qflush); }
void if_setgetcounterfn(if_t ifp, if_get_counter_t fn) { ifp->if_get_counter = fn; }
void if_setreassignfn(if_t ifp, if_reassign_fn_t fn) { ifp->if_reassign = fn; }
void if_setvfstatusfn(if_t ifp, if_vf_status_fn_t fn) { (void)ifp; (void)fn; }
void if_setdebugnet_methods(if_t ifp, struct debugnet_methods *m) { ifp->if_debugnet_methods = m; }
void if_bpfmtap(if_t ifp, struct mbuf *m) { (void)ifp; (void)m; }
void if_etherbpfmtap(if_t ifp, struct mbuf *m) { (void)ifp; (void)m; }

/* ---- counters ------------------------------------------------------- */

void
if_inc_counter(if_t ifp, ift_counter cnt, int64_t inc)
{
	KASSERT(cnt < IFCOUNTERS, ("%s: invalid cnt %d", __func__, cnt));
	(void)__atomic_fetch_add(&ifp->if_counters[cnt], (uint64_t)inc,
	    __ATOMIC_RELAXED);
}

uint64_t
if_get_counter_default(if_t ifp, ift_counter cnt)
{
	KASSERT(cnt < IFCOUNTERS, ("%s: invalid cnt %d", __func__, cnt));
	return (__atomic_load_n(&ifp->if_counters[cnt], __ATOMIC_RELAXED));
}

void
if_data_copy(if_t ifp, struct if_data *ifd)
{
	memset(ifd, 0, sizeof(*ifd));
	ifd->ifi_type = ifp->if_type;
	ifd->ifi_addrlen = ifp->if_addrlen;
	ifd->ifi_hdrlen = ifp->if_hdrlen;
	ifd->ifi_link_state = ifp->if_link_state;
	ifd->ifi_datalen = sizeof(struct if_data);
	ifd->ifi_mtu = ifp->if_mtu;
	ifd->ifi_metric = ifp->if_metric;
	ifd->ifi_baudrate = ifp->if_baudrate;
	ifd->ifi_hwassist = ifp->if_hwassist;
	ifd->ifi_epoch = ifp->if_epoch;
	ifd->ifi_lastchange = ifp->if_lastchange;
	ifd->ifi_ipackets = ifp->if_get_counter(ifp, IFCOUNTER_IPACKETS);
	ifd->ifi_ierrors = ifp->if_get_counter(ifp, IFCOUNTER_IERRORS);
	ifd->ifi_opackets = ifp->if_get_counter(ifp, IFCOUNTER_OPACKETS);
	ifd->ifi_oerrors = ifp->if_get_counter(ifp, IFCOUNTER_OERRORS);
	ifd->ifi_collisions = ifp->if_get_counter(ifp, IFCOUNTER_COLLISIONS);
	ifd->ifi_ibytes = ifp->if_get_counter(ifp, IFCOUNTER_IBYTES);
	ifd->ifi_obytes = ifp->if_get_counter(ifp, IFCOUNTER_OBYTES);
	ifd->ifi_imcasts = ifp->if_get_counter(ifp, IFCOUNTER_IMCASTS);
	ifd->ifi_omcasts = ifp->if_get_counter(ifp, IFCOUNTER_OMCASTS);
	ifd->ifi_iqdrops = ifp->if_get_counter(ifp, IFCOUNTER_IQDROPS);
	ifd->ifi_oqdrops = ifp->if_get_counter(ifp, IFCOUNTER_OQDROPS);
	ifd->ifi_noproto = ifp->if_get_counter(ifp, IFCOUNTER_NOPROTO);
}

/* ---- link state, address, multicast --------------------------------- */

void
if_link_state_change(if_t ifp, int link_state)
{
	if (ifp->if_link_state == link_state)
		return;
	ifp->if_link_state = (uint8_t)link_state;
	(void)taskqueue_enqueue(taskqueue_swi, &ifp->if_linktask);
}

int
if_setlladdr(if_t ifp, const u_char *lladdr, int len)
{
	struct sockaddr_dl *sdl = &ifp->if_lladdr_sdl;

	if (len != ETHER_ADDR_LEN || len > (int)sizeof(sdl->sdl_data) - sdl->sdl_nlen)
		return (EINVAL);
	memcpy(LLADDR(sdl), lladdr, (size_t)len);
	sdl->sdl_alen = (u_char)len;
	/* net80211 picks the new address up here (wlan_iflladdr) */
	EVENTHANDLER_INVOKE(iflladdr_event, ifp);
	if (bsdkpi_hooks.lladdr != NULL)
		bsdkpi_hooks.lladdr(ifp);
	return (0);
}

int
if_addmulti(if_t ifp, struct sockaddr *sa, struct ifmultiaddr **retifma)
{
	struct ifmultiaddr *ifma;

	if (sa->sa_len > sizeof(ifma->ifma_sdl))
		return (EINVAL);
	IF_ADDR_WLOCK(ifp);
	STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_len == sa->sa_len &&
		    memcmp(ifma->ifma_addr, sa, sa->sa_len) == 0) {
			ifma->ifma_refcount++;
			IF_ADDR_WUNLOCK(ifp);
			if (retifma != NULL)
				*retifma = ifma;
			return (0);
		}
	}
	ifma = malloc(sizeof(*ifma), M_IFMADDR, M_NOWAIT | M_ZERO);
	if (ifma == NULL) {
		IF_ADDR_WUNLOCK(ifp);
		return (ENOMEM);
	}
	memcpy(&ifma->ifma_sdl, sa, sa->sa_len);
	ifma->ifma_addr = (struct sockaddr *)&ifma->ifma_sdl;
	ifma->ifma_lladdr = ifma->ifma_addr;
	ifma->ifma_ifp = ifp;
	ifma->ifma_refcount = 1;
	STAILQ_INSERT_HEAD(&ifp->if_multiaddrs, ifma, ifma_link);
	IF_ADDR_WUNLOCK(ifp);
	if (retifma != NULL)
		*retifma = ifma;
	if (ifp->if_ioctl != NULL)
		(void)ifp->if_ioctl(ifp, SIOCADDMULTI, NULL);
	return (0);
}

void
if_freemulti(struct ifmultiaddr *ifma)
{
	free(ifma, M_IFMADDR);
}

void
if_delmulti_ifma_flags(struct ifmultiaddr *ifma, int flags)
{
	struct ifnet *ifp = ifma->ifma_ifp;

	(void)flags;
	if (--ifma->ifma_refcount > 0)
		return;
	if (ifp != NULL) {
		IF_ADDR_WLOCK(ifp);
		STAILQ_REMOVE(&ifp->if_multiaddrs, ifma, ifmultiaddr, ifma_link);
		IF_ADDR_WUNLOCK(ifp);
	}
	if_freemulti(ifma);
	if (ifp != NULL && ifp->if_ioctl != NULL)
		(void)ifp->if_ioctl(ifp, SIOCDELMULTI, NULL);
}

void
if_delmulti_ifma(struct ifmultiaddr *ifma)
{
	if_delmulti_ifma_flags(ifma, 0);
}

int
if_delmulti(if_t ifp, struct sockaddr *sa)
{
	struct ifmultiaddr *ifma;

	IF_ADDR_WLOCK(ifp);
	STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_len == sa->sa_len &&
		    memcmp(ifma->ifma_addr, sa, sa->sa_len) == 0)
			break;
	}
	IF_ADDR_WUNLOCK(ifp);
	if (ifma == NULL)
		return (ENOENT);
	if_delmulti_ifma(ifma);
	return (0);
}

void
if_delallmulti(if_t ifp)
{
	struct ifmultiaddr *ifma, *next;

	IF_ADDR_WLOCK(ifp);
	STAILQ_FOREACH_SAFE(ifma, &ifp->if_multiaddrs, ifma_link, next) {
		STAILQ_REMOVE(&ifp->if_multiaddrs, ifma, ifmultiaddr, ifma_link);
		if_freemulti(ifma);
	}
	IF_ADDR_WUNLOCK(ifp);
}

int
if_allmulti(if_t ifp, int onswitch)
{
	if (onswitch) {
		if (ifp->if_amcount++ == 0) {
			ifp->if_flags |= IFF_ALLMULTI;
			if (ifp->if_ioctl != NULL)
				(void)ifp->if_ioctl(ifp, SIOCSIFFLAGS, NULL);
		}
	} else {
		if (ifp->if_amcount > 1) {
			ifp->if_amcount--;
		} else {
			ifp->if_amcount = 0;
			ifp->if_flags &= ~IFF_ALLMULTI;
			if (ifp->if_ioctl != NULL)
				(void)ifp->if_ioctl(ifp, SIOCSIFFLAGS, NULL);
		}
	}
	return (0);
}

u_int
if_foreach_lladdr(if_t ifp, iflladdr_cb_t cb, void *cb_arg)
{
	return (cb(cb_arg, &ifp->if_lladdr_sdl, 0));
}

u_int
if_foreach_llmaddr(if_t ifp, iflladdr_cb_t cb, void *cb_arg)
{
	struct ifmultiaddr *ifma;
	u_int count = 0;

	IF_ADDR_WLOCK(ifp);
	STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		count += cb(cb_arg, (struct sockaddr_dl *)ifma->ifma_addr, count);
	}
	IF_ADDR_WUNLOCK(ifp);
	return (count);
}

u_int if_lladdr_count(if_t ifp) { (void)ifp; return (1); }

u_int
if_llmaddr_count(if_t ifp)
{
	struct ifmultiaddr *ifma;
	u_int count = 0;

	STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link)
		count++;
	return (count);
}

bool if_maddr_empty(if_t ifp) { return (STAILQ_EMPTY(&ifp->if_multiaddrs)); }

int
if_foreach(if_foreach_cb_t cb, void *arg)
{
	struct ifnet_entry *e;
	int error = 0;

	mutex_lock(&bsdkpi_ifnet_mtx);
	STAILQ_FOREACH(e, &bsdkpi_ifnets, link) {
		error = cb(e->ifp, arg);
		if (error != 0)
			break;
	}
	mutex_unlock(&bsdkpi_ifnet_mtx);
	return (error);
}

int
if_foreach_sleep(if_foreach_match_t match, void *match_arg, if_foreach_cb_t cb,
    void *arg)
{
	struct ifnet_entry *e;
	int error = 0;

	STAILQ_FOREACH(e, &bsdkpi_ifnets, link) {
		if (match != NULL && !match(e->ifp, match_arg))
			continue;
		error = cb(e->ifp, arg);
		if (error != 0)
			break;
	}
	return (error);
}

int
if_simloop(if_t ifp, struct mbuf *m, int af, int hlen)
{
	(void)af;
	m_adj(m, hlen);
	m->m_pkthdr.rcvif = ifp;
	if_input(ifp, m);
	return (0);
}

int
if_printf(if_t ifp, const char *fmt, ...)
{
	char if_fmt[256];
	va_list ap;
	int r;

	snprintf(if_fmt, sizeof(if_fmt), "%s: %s", ifp->if_xname, fmt);
	va_start(ap, fmt);
	r = vprintf(if_fmt, ap);
	va_end(ap);
	return (r);
}

int
if_log(if_t ifp, int pri, const char *fmt, ...)
{
	char if_fmt[256];
	va_list ap;

	snprintf(if_fmt, sizeof(if_fmt), "%s: %s", ifp->if_xname, fmt);
	va_start(ap, fmt);
	vlog(pri, if_fmt, ap);
	va_end(ap);
	return (0);
}

/* ---- sockaddr_dl helpers (net/if_dl.h) ------------------------------ */

struct sockaddr_dl *
link_alloc_sdl(size_t size, int flags)
{
	return (malloc(size, M_TEMP, flags | M_ZERO));
}

void
link_free_sdl(struct sockaddr *sa)
{
	free(sa, M_TEMP);
}

struct sockaddr_dl *
link_init_sdl(struct ifnet *ifp, struct sockaddr *paddr, u_char iftype)
{
	struct sockaddr_dl *sdl = (struct sockaddr_dl *)paddr;

	memset(sdl, 0, sizeof(*sdl));
	sdl->sdl_len = sizeof(*sdl);
	sdl->sdl_family = AF_LINK;
	sdl->sdl_index = ifp->if_index;
	sdl->sdl_type = iftype;
	return (sdl);
}

/* ---- routing socket notifications ----------------------------------- */

void
rt_ieee80211msg(struct ifnet *ifp, int what, void *data, size_t data_len)
{
	if (bsdkpi_hooks.ieee80211msg != NULL)
		bsdkpi_hooks.ieee80211msg(ifp, what, data, data_len);
}

void
rt_ifmsg(struct ifnet *ifp, int if_flags_mask)
{
	if (bsdkpi_hooks.ifmsg != NULL)
		bsdkpi_hooks.ifmsg(ifp, if_flags_mask);
}

void
rt_ifannouncemsg(struct ifnet *ifp, int what)
{
	(void)ifp;
	(void)what;
}

void
rt_newaddrmsg(int cmd, struct ifaddr *ifa, void *rt)
{
	(void)cmd; (void)ifa; (void)rt;
}

/* ---- interface cloning ---------------------------------------------- */

static LIST_HEAD(, if_clone) bsdkpi_cloners = LIST_HEAD_INITIALIZER(bsdkpi_cloners);
static DEFINE_MUTEX(bsdkpi_cloner_mtx);

struct if_clone *
ifc_attach_cloner(const char *name, struct if_clone_addreq *req)
{
	struct if_clone *ifc;

	if (req->create_f == NULL || req->destroy_f == NULL)
		return (NULL);
	ifc = malloc(sizeof(*ifc), M_IFADDR, M_WAITOK | M_ZERO);
	if (ifc == NULL)
		return (NULL);
	strlcpy(ifc->ifc_name, name, IFNAMSIZ);
	ifc->ifc_flags = req->flags;
	ifc->ifc_maxunit = req->maxunit != 0 ? req->maxunit : 63;
	if (ifc->ifc_maxunit > 63)
		ifc->ifc_maxunit = 63;
	ifc->ifc_match = req->match_f;
	ifc->ifc_create = req->create_f;
	ifc->ifc_destroy = req->destroy_f;
	ifc->ifc_refcnt = 1;
	mutex_lock(&bsdkpi_cloner_mtx);
	LIST_INSERT_HEAD(&bsdkpi_cloners, ifc, ifc_list);
	mutex_unlock(&bsdkpi_cloner_mtx);
	return (ifc);
}

void
ifc_detach_cloner(struct if_clone *ifc)
{
	if (ifc == NULL)
		return;
	mutex_lock(&bsdkpi_cloner_mtx);
	LIST_REMOVE(ifc, ifc_list);
	mutex_unlock(&bsdkpi_cloner_mtx);
	free(ifc, M_IFADDR);
}

struct if_clone *
bsdkpi_if_clone_lookup(const char *name)
{
	struct if_clone *ifc;

	mutex_lock(&bsdkpi_cloner_mtx);
	LIST_FOREACH(ifc, &bsdkpi_cloners, ifc_list) {
		if (strncmp(ifc->ifc_name, name, strlen(ifc->ifc_name)) == 0)
			break;
	}
	mutex_unlock(&bsdkpi_cloner_mtx);
	return (ifc);
}

int
ifc_name2unit(const char *name, int *unit)
{
	const char *cp;
	int cutoff = INT_MAX / 10;
	int cutlim = INT_MAX % 10;

	for (cp = name; *cp != '\0' && (*cp < '0' || *cp > '9'); cp++)
		continue;
	if (*cp == '\0') {
		*unit = -1;
	} else if (cp[0] == '0' && cp[1] != '\0') {
		*unit = -1;
		return (EINVAL);
	} else {
		*unit = 0;
		while (*cp != '\0') {
			if (*cp < '0' || *cp > '9') {
				*unit = -1;
				return (EINVAL);
			}
			if (*unit > cutoff || (*unit == cutoff && *cp - '0' > cutlim)) {
				*unit = -1;
				return (EINVAL);
			}
			*unit = (*unit * 10) + (*cp++ - '0');
		}
	}
	return (0);
}

int
ifc_alloc_unit(struct if_clone *ifc, int *unit)
{
	int u;

	if (*unit < 0) {
		for (u = 0; u <= (int)ifc->ifc_maxunit; u++)
			if ((ifc->ifc_units & (1UL << u)) == 0)
				break;
		if (u > (int)ifc->ifc_maxunit)
			return (ENOSPC);
		*unit = u;
	} else if (*unit > (int)ifc->ifc_maxunit) {
		return (ENOSPC);
	} else if (ifc->ifc_units & (1UL << *unit)) {
		return (EEXIST);
	}
	ifc->ifc_units |= 1UL << *unit;
	return (0);
}

void
ifc_free_unit(struct if_clone *ifc, int unit)
{
	if (unit >= 0 && unit <= (int)ifc->ifc_maxunit)
		ifc->ifc_units &= ~(1UL << unit);
}

int
ifc_copyin(const struct ifc_data *ifd, void *target, size_t len)
{
	if (ifd->params == NULL)
		return (EINVAL);
	memcpy(target, ifd->params, len);
	return (0);
}

int
ifc_create_ifp(const char *name, struct ifc_data *ifd, struct ifnet **ifpp)
{
	struct if_clone *ifc;
	char ifname[IFNAMSIZ];
	int unit, error;

	ifc = bsdkpi_if_clone_lookup(name);
	if (ifc == NULL)
		return (EINVAL);
	error = ifc_name2unit(name, &unit);
	if (error != 0)
		return (error);
	if (unit < 0 && (ifd->flags & IFC_F_AUTOUNIT) == 0 &&
	    (ifc->ifc_flags & IFC_F_AUTOUNIT) == 0)
		return (EINVAL);
	mutex_lock(&bsdkpi_cloner_mtx);
	error = ifc_alloc_unit(ifc, &unit);
	mutex_unlock(&bsdkpi_cloner_mtx);
	if (error != 0)
		return (error);
	ifd->unit = (uint32_t)unit;
	snprintf(ifname, sizeof(ifname), "%s%d", ifc->ifc_name, unit);
	*ifpp = NULL;
	error = ifc->ifc_create(ifc, ifname, sizeof(ifname), ifd, ifpp);
	if (error != 0) {
		mutex_lock(&bsdkpi_cloner_mtx);
		ifc_free_unit(ifc, unit);
		mutex_unlock(&bsdkpi_cloner_mtx);
	}
	return (error);
}

int
if_clone_create(char *name, size_t len, caddr_t params)
{
	struct ifc_data ifd = { .flags = IFC_F_AUTOUNIT, .params = params };
	struct ifnet *ifp;
	int error;

	error = ifc_create_ifp(name, &ifd, &ifp);
	if (error == 0)
		strlcpy(name, ifp->if_xname, len);
	return (error);
}

int
if_clone_destroyif(struct if_clone *ifc, struct ifnet *ifp)
{
	int unit = ifp->if_dunit;
	int error;

	error = ifc->ifc_destroy(ifc, ifp, 0);
	if (error == 0) {
		mutex_lock(&bsdkpi_cloner_mtx);
		ifc_free_unit(ifc, unit);
		mutex_unlock(&bsdkpi_cloner_mtx);
	}
	return (error);
}

int
if_clone_destroy(const char *name)
{
	struct ifnet *ifp;
	struct if_clone *ifc;

	ifp = ifunit(name);
	if (ifp == NULL)
		return (ENXIO);
	ifc = bsdkpi_if_clone_lookup(name);
	if (ifc == NULL)
		return (EINVAL);
	return (if_clone_destroyif(ifc, ifp));
}

int
bsdkpi_if_clone_create(const char *name, struct ifc_data *ifd,
    struct ifnet **ifpp)
{
	return (ifc_create_ifp(name, ifd, ifpp));
}

int
bsdkpi_if_clone_destroy(struct ifnet *ifp)
{
	struct if_clone *ifc;

	if (ifp->if_dname == NULL)
		return (EINVAL);
	ifc = bsdkpi_if_clone_lookup(ifp->if_dname);
	if (ifc == NULL)
		return (EINVAL);
	return (if_clone_destroyif(ifc, ifp));
}
