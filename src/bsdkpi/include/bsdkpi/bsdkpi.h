/* SPDX-License-Identifier: MIT */
/*
 * bsdkpi/bsdkpi.h - the shim's own entry points: start-up, the module
 * registration walk, and the hooks the IO80211 layer (phase 4) binds to
 * the interface object.  Plain C, safe to include from C++.
 */
#ifndef _BSDKPI_BSDKPI_H_
#define _BSDKPI_BSDKPI_H_
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ifnet;
struct mbuf;
struct ifc_data;

/*
 * Bring the shim up / down.  bsdkpi_init() creates the callout work queue
 * and the system task queues and runs every SYSINIT and DECLARE_MODULE
 * recorded at load time (net80211's cipher, scanner, rate-control and
 * auth tables are populated here).  Call once after lkpi_runtime_init().
 */
int	bsdkpi_init(void);
void	bsdkpi_fini(void);

/*
 * The hooks net80211's OS layer calls out through.  All optional; a NULL
 * hook keeps the default (drop the frame / log).  if_input and if_transmit
 * are function pointers in struct ifnet and are set directly on the vap's
 * ifp instead.
 */
struct bsdkpi_ifnet_hooks {
	/* ether_ifattach()/ether_ifdetach() were called for ifp */
	void	(*attach)(struct ifnet *ifp);
	void	(*detach)(struct ifnet *ifp);
	/* if_link_state_change(): LINK_STATE_UP/DOWN/UNKNOWN, on a task */
	void	(*link_state)(struct ifnet *ifp, int state);
	/* rt_ieee80211msg(): RTM_IEEE80211_* with the ieee80211_*_event */
	void	(*ieee80211msg)(struct ifnet *ifp, int what, const void *data,
		    size_t len);
	/* rt_ifmsg(): interface flags changed (mask of IFF_* that changed) */
	void	(*ifmsg)(struct ifnet *ifp, int flags_mask);
	/* if_setlladdr() changed the MAC address */
	void	(*lladdr)(struct ifnet *ifp);
};
void	bsdkpi_ifnet_set_hooks(const struct bsdkpi_ifnet_hooks *hooks);

/* Create / destroy a cloned interface ("wlan", unit, params) through the
 * cloner net80211 registered.  Returns 0 or an errno. */
int	bsdkpi_if_clone_create(const char *name, struct ifc_data *ifd,
	    struct ifnet **ifpp);
int	bsdkpi_if_clone_destroy(struct ifnet *ifp);

/* mbuf <-> flat buffer helpers for the glue layers. */
struct mbuf *bsdkpi_mbuf_from_buffer(const void *data, size_t len, int how);
size_t	bsdkpi_mbuf_to_buffer(const struct mbuf *m, void *buf, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* _BSDKPI_BSDKPI_H_ */
