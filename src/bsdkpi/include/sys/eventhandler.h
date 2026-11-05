/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_SYS_EVENTHANDLER_H_
#define _BSDKPI_SYS_EVENTHANDLER_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/_eventhandler.h>

struct ifnet;
typedef void (*iflladdr_event_handler_t)(void *, struct ifnet *);
EVENTHANDLER_DECLARE(iflladdr_event, iflladdr_event_handler_t);
typedef void (*ifnet_arrival_event_handler_t)(void *, struct ifnet *);
EVENTHANDLER_DECLARE(ifnet_arrival_event, ifnet_arrival_event_handler_t);
typedef void (*ifnet_departure_event_handler_t)(void *, struct ifnet *);
EVENTHANDLER_DECLARE(ifnet_departure_event, ifnet_departure_event_handler_t);
typedef void (*ifnet_link_event_handler_t)(void *, struct ifnet *, int);
EVENTHANDLER_DECLARE(ifnet_link_event, ifnet_link_event_handler_t);
typedef void (*shutdown_fn)(void *, int);
EVENTHANDLER_DECLARE(shutdown_pre_sync, shutdown_fn);
EVENTHANDLER_DECLARE(shutdown_post_sync, shutdown_fn);
EVENTHANDLER_DECLARE(shutdown_final, shutdown_fn);
typedef void (*power_change_fn)(void *);
EVENTHANDLER_DECLARE(power_resume, power_change_fn);
EVENTHANDLER_DECLARE(power_suspend, power_change_fn);
EVENTHANDLER_DECLARE(power_suspend_early, power_change_fn);
#endif
