/* SPDX-License-Identifier: MIT */
/*
 * Force-included (-include) in front of every file of src/lkpi80211.
 *
 * linux_80211.c is the one place where the FreeBSD kernel API (bsdkpi:
 * sys/param.h, net/if.h, net80211) and the Linux one (linux/..., net/mac80211.h)
 * meet in a single translation unit, and it includes the FreeBSD side
 * first.  A few macro names exist on both sides with different meanings
 * (ALIGN, LIST_HEAD, roundup, PAGE_SIZE, IFNAMSIZ, IFF_*); bsdkpi's
 * rule is "LinuxKPI headers are parsed first, FreeBSD's definitions win
 * afterwards" (docs/PHASE3_NET80211.md).  This prelude establishes that
 * order without touching the vendored file: every Linux header the glue
 * uses is parsed here, with Linux's macros in force, and becomes a guarded
 * no-op when the glue includes it again after sys/param.h.
 *
 * It also supplies what FreeBSD's kernel gives the glue implicitly through
 * header pollution (sx(9), refcount(9), curthread) and the names FreeBSD's
 * LinuxKPI spells differently from ours.
 */
#ifndef _LKPI80211_PRELUDE_H_
#define _LKPI80211_PRELUDE_H_

/*
 * bsdkpi's sys/types.h is the entry point of the ordering rule: it parses
 * the core LinuxKPI headers and only then FreeBSD's sys/queue.h.  ALIGN,
 * roundup, PAGE_SIZE etc. are still Linux's until sys/param.h below.
 */
#include <sys/types.h>

/* --- Linux side first ------------------------------------------------- */
/* As linux_80211.c does before <net/mac80211.h>: keep the mac80211 names
 * ieee80211_channel/ieee80211_regdomain from shadowing net80211's. */
#define LINUXKPI_NET80211
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

/* --- then the FreeBSD side -------------------------------------------- */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/refcount.h>
#include <sys/proc.h>
#include <sys/pcpu.h>
#include <sys/bus.h>

/*
 * net80211 declares ieee80211_send_bar(ni, tap, seq); mac80211.h has a
 * function-like macro of the same name with four arguments.  FreeBSD gets
 * away with it because the glue parses net80211 before mac80211.h; here
 * mac80211.h comes first, so hide the macro while net80211's prototypes
 * are parsed.  The glue's own includes of these headers are then no-ops.
 */
#pragma push_macro("ieee80211_send_bar")
#undef ieee80211_send_bar
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>
#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_proto.h>
#include <net80211/ieee80211_ratectl.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_vht.h>
#pragma pop_macro("ieee80211_send_bar")

/* FreeBSD LinuxKPI names the glue uses. */
#define linux_page_address(p)	page_address(p)

/* %b description of netdev_features_t, following linux/netdev_features.h. */
#define NETIF_F_BITS \
	"\20\1SG\2IP_CSUM\3HW_CSUM\4IPV6_CSUM\5HIGHDMA\6FRAGLIST" \
	"\7HW_VLAN_CTAG_TX\10HW_VLAN_CTAG_RX\11RXCSUM\12RXHASH\13TSO\14GRO" \
	"\15LRO\16RXFCS\17RXALL\20HW_TC"

#endif /* _LKPI80211_PRELUDE_H_ */
