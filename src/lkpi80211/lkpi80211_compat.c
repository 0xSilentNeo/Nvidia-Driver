/* SPDX-License-Identifier: MIT */
/*
 * mac80211/cfg80211 entry points that the vendored net/mac80211.h declares
 * (through lkpi/mac80211_compat.h) but FreeBSD's linux_80211.c does not
 * implement yet, plus the one FreeBSD LinuxKPI object the glue expects
 * from linux_compat.c.  Everything here is deliberately small: when the
 * FreeBSD glue grows the real thing, the function moves out of this file.
 */
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#define	LINUXKPI_NET80211
#include <net/mac80211.h>

#include "linux_80211.h"

/*
 * compat.linuxkpi: FreeBSD defines this node in linux_compat.c; the glue
 * hangs compat.linuxkpi.80211 (debug, hw_crypto, tkip, per-vap dumps) off it.
 */
SYSCTL_NODE(_compat, OID_AUTO, linuxkpi, CTLFLAG_RW | CTLFLAG_MPSAFE, 0,
    "LinuxKPI parameters");

/*
 * Airtime queue limits: the glue does no airtime accounting (FreeBSD has
 * no AQL), so nothing is ever pending.  mt76 only uses the value to decide
 * whether a txq may be skipped in its scheduling round.
 */
u32
ieee80211_txq_aql_pending(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{

	(void)hw;
	(void)txq;
	return (0);
}

/*
 * NAN (Wi-Fi Aware) notifications.  mt7925/nan.c is not built and the glue
 * never creates a NAN interface (macOS has no consumer), so these can only
 * be reached if that changes; say so instead of silently dropping them.
 */
void
ieee80211_nan_cluster_joined(struct ieee80211_vif *vif, const u8 *cluster_id,
    bool new_cluster, gfp_t gfp)
{

	(void)vif;
	(void)cluster_id;
	(void)new_cluster;
	(void)gfp;
	pr_warn_once("lkpi80211: %s: NAN is not supported\n", __func__);
}

void
ieee80211_nan_sched_update_done(struct ieee80211_vif *vif)
{

	(void)vif;
	pr_warn_once("lkpi80211: %s: NAN is not supported\n", __func__);
}

void
cfg80211_nan_ulw_update(struct wireless_dev *wdev, const u8 *ulw,
    size_t ulw_len, gfp_t gfp)
{

	(void)wdev;
	(void)ulw;
	(void)ulw_len;
	(void)gfp;
	pr_warn_once("lkpi80211: %s: NAN is not supported\n", __func__);
}
