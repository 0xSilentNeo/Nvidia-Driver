// SPDX-License-Identifier: MIT
/*
 * Stand-ins for mt7925/nan.c (Wi-Fi Aware / NAN).
 *
 * macOS has no consumer for NAN and the mac80211 NAN API is still being
 * reshaped upstream, so the real nan.c is not built.  The 802.11 glue never
 * creates an NL80211_IFTYPE_NAN interface, which means none of these can be
 * reached in practice; they exist so that main.c, mcu.c and init.c link.
 */
#include "mt7925.h"
#include "nan.h"
#include "mcu.h"

int mt7925_nan_update_phy_setting(struct mt792x_dev *dev)
{
	(void)dev;
	return 0;
}

struct ieee80211_chanctx_conf *
mt7925_nan_seed_link_sta(struct mt792x_dev *dev, struct ieee80211_link_sta *link_sta)
{
	(void)dev;
	(void)link_sta;
	return NULL;
}

int mt7925_nan_enable(struct ieee80211_vif *vif, struct mt792x_dev *dev,
		      struct cfg80211_nan_conf *conf)
{
	(void)vif;
	(void)conf;
	dev_warn(dev->mt76.dev, "NAN is not supported in this port\n");
	return -EOPNOTSUPP;
}

int mt7925_nan_disable(struct ieee80211_vif *vif, struct mt792x_dev *dev)
{
	(void)vif;
	(void)dev;
	return 0;
}

int mt7925_nan_change_configure(struct ieee80211_vif *vif, struct mt792x_dev *dev,
				struct cfg80211_nan_conf *conf)
{
	(void)vif;
	(void)dev;
	(void)conf;
	return -EOPNOTSUPP;
}

void mt7925_nan_mcu_event(struct mt792x_dev *dev, struct sk_buff *skb)
{
	/* The event is consumed by the caller; nothing to decode here. */
	(void)dev;
	(void)skb;
}

void mt7925_nan_local_sched_changed(struct mt792x_dev *dev, struct ieee80211_vif *vif)
{
	(void)dev;
	(void)vif;
}

int mt792x_nan_set_peer_schedule(struct mt792x_dev *dev, struct ieee80211_sta *sta)
{
	(void)dev;
	(void)sta;
	return -EOPNOTSUPP;
}

void mt7925_nan_deferred_work(struct work_struct *work)
{
	(void)work;
}

int mt792x_nan_set_peer_rec(struct mt76_dev *mdev, struct ieee80211_sta *sta)
{
	(void)mdev;
	(void)sta;
	return 0;
}

int mt792x_nan_map_sta_rec(struct mt76_dev *mdev, struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	(void)mdev;
	(void)vif;
	(void)sta;
	return 0;
}

/* mt7925/debugfs.c is not built either: there is no debugfs to populate. */
int mt7925_init_debugfs(struct mt792x_dev *dev)
{
	(void)dev;
	return 0;
}
