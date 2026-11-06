/* SPDX-License-Identifier: MIT */
/*
 * net/if_var.h - kernel interface API: if_alloc/if_attach/ether_ifattach,
 * the if_* accessors and the link-state/counter helpers.  Implementation in
 * src/ifnet.c.  struct ifnet itself is in net/if_private.h (FreeBSD keeps
 * it private; net80211 includes that header where it dereferences ifp).
 */
#ifndef _BSDKPI_NET_IF_VAR_H_
#define _BSDKPI_NET_IF_VAR_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/mbuf.h>
#include <sys/epoch.h>
#include <net/if.h>
#include <net/if_private.h>
#include <net/ifq.h>
#include <net/vnet.h>

struct route;
struct thread;
struct ifaddr;
struct ifmultiaddr;
struct bpf_if;
struct netmap_adapter;
struct debugnet_methods;
struct if_snd_tag_alloc_params;
struct label;

#define V_if_index	bsdkpi_if_index
extern int bsdkpi_if_index;
extern int ifqmaxlen;

typedef void (*if_com_alloc_t)(u_char type, struct ifnet *ifp);
typedef void (*if_com_free_t)(void *com, u_char type);
typedef void (*if_snd_tag_alloc_t)(void);
typedef u_int (*iflladdr_cb_t)(void *, struct sockaddr_dl *, u_int);
typedef u_int (*if_addr_cb_t)(void *, struct ifaddr *, u_int);
typedef int (*if_foreach_cb_t)(if_t, void *);
typedef bool (*if_foreach_match_t)(if_t, void *);
typedef int (*if_foreach_group_cb_t)(if_t, const char *, void *);

/* lifecycle */
if_t	if_alloc(u_char type);
if_t	if_alloc_dev(u_char type, void *dev);
void	if_free(if_t ifp);
void	if_attach(if_t ifp);
void	if_detach(if_t ifp);
void	if_dead(if_t ifp);
void	if_initname(if_t ifp, const char *name, int unit);
void	if_ref(if_t ifp);
void	if_rele(if_t ifp);
void	if_up(if_t ifp);
void	if_down(if_t ifp);
void	if_purgeaddrs(if_t ifp);
void	if_delallmulti(if_t ifp);
int	if_addmulti(if_t ifp, struct sockaddr *sa, struct ifmultiaddr **retifma);
int	if_delmulti(if_t ifp, struct sockaddr *sa);
void	if_delmulti_ifma(struct ifmultiaddr *ifma);
void	if_delmulti_ifma_flags(struct ifmultiaddr *ifma, int flags);
void	if_freemulti(struct ifmultiaddr *ifma);
int	if_allmulti(if_t ifp, int on);
int	if_setlladdr(if_t ifp, const u_char *lladdr, int len);
void	if_link_state_change(if_t ifp, int link_state);
int	if_printf(if_t ifp, const char *fmt, ...) __printflike(2, 3);
int	if_log(if_t ifp, int pri, const char *fmt, ...) __printflike(3, 4);
void	if_data_copy(if_t ifp, struct if_data *ifd);
uint64_t if_get_counter_default(if_t ifp, ift_counter cnt);
void	if_inc_counter(if_t ifp, ift_counter cnt, int64_t inc);
int	if_simloop(if_t ifp, struct mbuf *m, int af, int hlen);
int	if_setupmultiaddr(if_t ifp, void *mta, int *cnt, int max);
int	if_multiaddr_array(if_t ifp, void *mta, int *cnt, int max);
int	if_multiaddr_count(if_t ifp, int max);
u_int	if_foreach_lladdr(if_t ifp, iflladdr_cb_t cb, void *arg);
u_int	if_foreach_llmaddr(if_t ifp, iflladdr_cb_t cb, void *arg);
u_int	if_lladdr_count(if_t ifp);
u_int	if_llmaddr_count(if_t ifp);
bool	if_maddr_empty(if_t ifp);
int	if_foreach(if_foreach_cb_t cb, void *arg);
int	if_foreach_sleep(if_foreach_match_t m, void *ma, if_foreach_cb_t cb, void *arg);
if_t	ifunit(const char *name);
if_t	ifunit_ref(const char *name);
void	if_register_com_alloc(u_char type, if_com_alloc_t a, if_com_free_t f);
void	if_deregister_com_alloc(u_char type);
struct ifaddr *ifa_ifwithaddr(const struct sockaddr *addr);

/* accessors (net/if_var.h style) */
uint64_t if_setbaudrate(if_t ifp, uint64_t baudrate);
uint64_t if_getbaudrate(const if_t ifp);
int	if_setcapabilities(if_t ifp, int capabilities);
int	if_setcapabilitiesbit(if_t ifp, int setbit, int clearbit);
int	if_getcapabilities(const if_t ifp);
int	if_togglecapenable(if_t ifp, int togglecap);
int	if_setcapenable(if_t ifp, int capenable);
int	if_setcapenablebit(if_t ifp, int setcap, int clearcap);
int	if_getcapenable(const if_t ifp);
int	if_setcapabilities2(if_t ifp, int capabilities);
int	if_setcapabilities2bit(if_t ifp, int setbit, int clearbit);
int	if_getcapabilities2(const if_t ifp);
int	if_togglecapenable2(if_t ifp, int togglecap);
int	if_setcapenable2(if_t ifp, int capenable);
int	if_setcapenable2bit(if_t ifp, int setcap, int clearcap);
int	if_getcapenable2(const if_t ifp);
int	if_getdunit(const if_t ifp);
int	if_getindex(const if_t ifp);
int	if_getidxgen(const if_t ifp);
const char *if_getdname(const if_t ifp);
void	if_setdname(if_t ifp, const char *name);
const char *if_name(if_t ifp);
int	if_setname(if_t ifp, const char *name);
int	if_rename(if_t ifp, char *new_name);
const char *if_getdescr(if_t ifp);
void	if_setdescr(if_t ifp, char *descrbuf);
char	*if_allocdescr(size_t sz, int malloc_flag);
void	if_freedescr(char *descrbuf);
void	if_setlastchange(if_t ifp);
int	if_getalloctype(const if_t ifp);
int	if_gettype(const if_t ifp);
int	if_setdev(if_t ifp, void *dev);
int	if_setdrvflagbits(if_t ifp, int if_setflags, int clear_flags);
int	if_getdrvflags(const if_t ifp);
int	if_setdrvflags(if_t ifp, int flags);
int	if_getlinkstate(if_t ifp);
int	if_clearhwassist(if_t ifp);
int	if_sethwassistbits(if_t ifp, int toset, int toclear);
int	if_sethwassist(if_t ifp, int hwassist_bit);
int	if_gethwassist(const if_t ifp);
int	if_togglehwassist(if_t ifp, int toggle_bits);
int	if_setsoftc(if_t ifp, void *softc);
void	*if_getsoftc(if_t ifp);
int	if_setflags(if_t ifp, int flags);
int	if_setflagbits(if_t ifp, int set, int clear);
int	if_getflags(const if_t ifp);
void	if_setllsoftc(if_t ifp, void *softc);
void	*if_getllsoftc(if_t ifp);
u_int	if_getfib(if_t ifp);
uint8_t	if_getaddrlen(if_t ifp);
int	if_gethwaddr(const if_t ifp, struct ifreq *ifr);
const uint8_t *if_getbroadcastaddr(const if_t ifp);
void	if_setbroadcastaddr(if_t ifp, const uint8_t *addr);
int	if_setmtu(if_t ifp, int mtu);
int	if_getmtu(const if_t ifp);
int	if_getmtu_family(if_t ifp, int family);
void	if_notifymtu(if_t ifp);
void	if_setppromisc(const if_t ifp, bool ppromisc);
int	if_getnumadomain(if_t ifp);
int	if_sendq_empty(if_t ifp);
int	if_setsendqready(if_t ifp);
int	if_setsendqlen(if_t ifp, int tx_desc_count);
int	if_sethwtsomax(if_t ifp, u_int if_hw_tsomax);
int	if_sethwtsomaxsegcount(if_t ifp, u_int if_hw_tsomaxsegcount);
int	if_sethwtsomaxsegsize(if_t ifp, u_int if_hw_tsomaxsegsize);
u_int	if_gethwtsomax(const if_t ifp);
u_int	if_gethwtsomaxsegcount(const if_t ifp);
u_int	if_gethwtsomaxsegsize(const if_t ifp);
void	if_setnetmapadapter(if_t ifp, struct netmap_adapter *na);
struct netmap_adapter *if_getnetmapadapter(if_t ifp);
void	if_input(if_t ifp, struct mbuf *sendmp);
int	if_sendq_prepend(if_t ifp, struct mbuf *m);
struct mbuf *if_dequeue(if_t ifp);
int	if_setifheaderlen(if_t ifp, int len);
void	if_setrcvif(struct mbuf *m, if_t ifp);
void	if_setvtag(struct mbuf *m, u_int16_t tag);
u_int16_t if_getvtag(struct mbuf *m);
int	if_vlantrunkinuse(if_t ifp);
char	*if_getlladdr(const if_t ifp);
struct vnet *if_getvnet(const if_t ifp);
void	*if_gethandle(u_char type);
void	if_vlancap(if_t ifp);
int	if_transmit(if_t ifp, struct mbuf *m);
void	if_init(if_t ifp, void *ctx);
int	if_ioctl(if_t ifp, u_long cmd, void *data);
int	if_resolvemulti(if_t ifp, struct sockaddr **sap, struct sockaddr *sa);
uint64_t if_getcounter(if_t ifp, ift_counter counter);
struct label *if_getmaclabel(if_t ifp);
void	if_setmaclabel(if_t ifp, struct label *label);
struct bpf_if *if_getbpf(if_t ifp);
uint8_t	if_getpcp(if_t ifp);
void	*if_getl2com(if_t ifp);
struct ifaddr *if_getifaddr(const if_t ifp);
u_int	if_foreach_addr_type(if_t ifp, int type, if_addr_cb_t cb, void *cb_arg);
void	if_setinitfn(if_t ifp, if_init_fn_t fn);
void	if_setinputfn(if_t ifp, if_input_fn_t fn);
if_input_fn_t if_getinputfn(if_t ifp);
void	if_setioctlfn(if_t ifp, if_ioctl_fn_t fn);
void	if_setoutputfn(if_t ifp, if_output_fn_t fn);
void	if_setstartfn(if_t ifp, if_start_fn_t fn);
if_start_fn_t if_getstartfn(if_t ifp);
void	if_settransmitfn(if_t ifp, if_transmit_fn_t fn);
if_transmit_fn_t if_gettransmitfn(if_t ifp);
void	if_setqflushfn(if_t ifp, if_qflush_fn_t fn);
if_qflush_fn_t if_getqflushfn(if_t ifp);
void	if_setgetcounterfn(if_t ifp, if_get_counter_t fn);
void	if_setreassignfn(if_t ifp, if_reassign_fn_t fn);
void	if_setvfstatusfn(if_t ifp, if_vf_status_fn_t fn);
void	if_setdebugnet_methods(if_t ifp, struct debugnet_methods *m);
void	if_bpfmtap(if_t ifp, struct mbuf *m);
void	if_etherbpfmtap(if_t ifp, struct mbuf *m);

/* FreeBSD's ifnet lock helpers. */
#define IF_LOCK_INIT(ifp)	mtx_init(&(ifp)->if_lock, "ifnet", NULL, MTX_DEF)
#define IF_LOCK_DESTROY(ifp)	mtx_destroy(&(ifp)->if_lock)
#define IFNET_RLOCK()		do { } while (0)
#define IFNET_RUNLOCK()		do { } while (0)
#define IFNET_WLOCK()		do { } while (0)
#define IFNET_WUNLOCK()		do { } while (0)
#define IFNET_RLOCK_ASSERT()	do { } while (0)
#define IFNET_WLOCK_ASSERT()	do { } while (0)
#define IFNET_FOREACH(ifp)	for ((ifp) = bsdkpi_ifnet_first(); (ifp) != NULL; (ifp) = bsdkpi_ifnet_next(ifp))
if_t	bsdkpi_ifnet_first(void);
if_t	bsdkpi_ifnet_next(if_t ifp);

#define ifa_ref(ifa)	do { } while (0)
#define ifa_free(ifa)	do { } while (0)

#endif /* _BSDKPI_NET_IF_VAR_H_ */
