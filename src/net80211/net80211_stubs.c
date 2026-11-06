// SPDX-License-Identifier: MIT
/*
 * Entry points of the net80211 modules that are not in the station-mode
 * build set (docs/PHASE3_NET80211.md): ieee80211_proto.c and ieee80211_output.c
 * call them unconditionally.  Doing nothing here leaves ic_vattach[] empty
 * for the HOSTAP/IBSS/AHDEMO/WDS opmodes, so creating such a vap fails
 * with "mode not supported" instead of linking in those modules.  Phase 5
 * replaces this file with ieee80211_hostap.c for AP mode.
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_hostap.h>
#include <net80211/ieee80211_adhoc.h>
#include <net80211/ieee80211_wds.h>

void
ieee80211_hostap_attach(struct ieee80211com *ic)
{
	(void)ic;
}

void
ieee80211_hostap_detach(struct ieee80211com *ic)
{
	(void)ic;
}

void
ieee80211_adhoc_attach(struct ieee80211com *ic)
{
	(void)ic;
}

void
ieee80211_adhoc_detach(struct ieee80211com *ic)
{
	(void)ic;
}

void
ieee80211_wds_attach(struct ieee80211com *ic)
{
	(void)ic;
}

void
ieee80211_wds_detach(struct ieee80211com *ic)
{
	(void)ic;
}

/*
 * DWDS multicast relay: only reached with IEEE80211_F_DWDS set on an AP
 * vap, which cannot exist without ieee80211_hostap.c.
 */
void
ieee80211_dwds_mcast(struct ieee80211vap *vap, struct mbuf *m)
{
	(void)vap;
	(void)m;
}
