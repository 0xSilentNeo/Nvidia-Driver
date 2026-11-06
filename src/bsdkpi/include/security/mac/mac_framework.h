/* SPDX-License-Identifier: MIT */
/* security/mac/mac_framework.h - the MAC framework is not built (no MAC). */
#ifndef _BSDKPI_SECURITY_MAC_MAC_FRAMEWORK_H_
#define _BSDKPI_SECURITY_MAC_MAC_FRAMEWORK_H_
struct label;
#define mac_ifnet_create(ifp)			do { } while (0)
#define mac_ifnet_destroy(ifp)			do { } while (0)
#define mac_ifnet_create_mbuf(ifp, m)		do { } while (0)
#define mac_ifnet_check_transmit(ifp, m)	(0)
#define mac_bpfdesc_check_receive(d, ifp)	(0)
#endif
