/* SPDX-License-Identifier: MIT */
/*
 * sys/bus.h - newbus.  There is no device tree here: the one device is
 * owned by the IOKit/LinuxKPI PCI layer, and FreeBSD's "linuxkpi_wlan PM
 * child" devices (linuxkpi_80211_pm.c) have no equivalent, since suspend
 * and resume arrive through IOService::setPowerState in phase 4.  The
 * type and the handful of calls the mac80211 glue makes exist so that the
 * vendored code compiles; no device_t is ever non-NULL.
 */
#ifndef _BSDKPI_SYS_BUS_H_
#define _BSDKPI_SYS_BUS_H_
#include <sys/cdefs.h>
#include <sys/types.h>

struct bsdkpi_device;
typedef struct bsdkpi_device	*device_t;

static __inline void bus_topo_lock(void) { }
static __inline void bus_topo_unlock(void) { }
static __inline void bus_identify_children(device_t dev) { (void)dev; }
static __inline void bus_enumerate_hinted_children(device_t dev) { (void)dev; }
static __inline void bus_attach_children(device_t dev) { (void)dev; }
static __inline int bus_generic_detach(device_t dev) { (void)dev; return (0); }

#endif /* _BSDKPI_SYS_BUS_H_ */
