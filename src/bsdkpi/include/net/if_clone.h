/* SPDX-License-Identifier: MIT */
/*
 * net/if_clone.h - interface cloning.  net80211 registers the "wlan"
 * cloner at MOD_LOAD; bsdkpi keeps the create/destroy callbacks so that the
 * kext creates a vap through the same path (bsdkpi_if_clone_create in
 * bsdkpi/bsdkpi.h) that ifconfig wlan0 create uses on FreeBSD.
 */
#ifndef _BSDKPI_NET_IF_CLONE_H_
#define _BSDKPI_NET_IF_CLONE_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <net/if.h>

struct if_clone;
struct vnet;

struct ifc_data {
	uint32_t	flags;
	uint32_t	unit;	/* Selected unit when IFC_F_AUTOUNIT set */
	void		*params;
	struct vnet	*vnet;
};

typedef int ifc_match_f(struct if_clone *ifc, const char *name);
typedef int ifc_create_f(struct if_clone *ifc, char *name, size_t maxlen,
    struct ifc_data *ifd, struct ifnet **ifpp);
typedef int ifc_destroy_f(struct if_clone *ifc, struct ifnet *ifp, uint32_t flags);

struct if_clone_addreq {
	uint16_t	version; /* Always 0 for now */
	uint16_t	spare;
	uint32_t	flags;
	uint32_t	maxunit; /* Maximum allowed unit number */
	ifc_match_f	*match_f;
	ifc_create_f	*create_f;
	ifc_destroy_f	*destroy_f;
};

#define IFC_F_SPARE	0x01
#define IFC_F_AUTOUNIT	0x02
#define IFC_F_SYSSPACE	0x04
#define IFC_F_FORCE	0x08
#define IFC_F_CREATE	0x10
#define IFC_F_LIMITUNIT	0x20
#define IFC_F_NOGROUP	0x40

#define IFC_IFLIST_HASH_SIZE	64

struct if_clone {
	LIST_ENTRY(if_clone)	 ifc_list;
	char			 ifc_name[IFNAMSIZ];
	uint32_t		 ifc_flags;
	uint32_t		 ifc_maxunit;
	ifc_match_f		*ifc_match;
	ifc_create_f		*ifc_create;
	ifc_destroy_f		*ifc_destroy;
	unsigned long		 ifc_units;	/* unit bitmap (64 units) */
	u_int			 ifc_refcnt;
};

struct if_clone	*ifc_attach_cloner(const char *name, struct if_clone_addreq *req);
void	ifc_detach_cloner(struct if_clone *ifc);
int	ifc_create_ifp(const char *name, struct ifc_data *ifd, struct ifnet **ifpp);
int	ifc_copyin(const struct ifc_data *ifd, void *target, size_t len);
int	ifc_name2unit(const char *name, int *unit);
int	ifc_alloc_unit(struct if_clone *ifc, int *unit);
void	ifc_free_unit(struct if_clone *ifc, int unit);
int	if_clone_create(char *name, size_t len, caddr_t params);
int	if_clone_destroy(const char *name);
int	if_clone_destroyif(struct if_clone *ifc, struct ifnet *ifp);
struct if_clone *bsdkpi_if_clone_lookup(const char *name);
#endif /* _BSDKPI_NET_IF_CLONE_H_ */
