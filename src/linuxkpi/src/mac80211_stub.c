// SPDX-License-Identifier: MIT
/*
 * Phase-1 "recording" implementation of the mac80211/cfg80211 entry points
 * the driver links against (the linuxkpi_* functions the vendored FreeBSD
 * headers forward to, see scripts/link-check.sh for the exact list).
 *
 * There is no 802.11 stack yet (phase 3 ports FreeBSD net80211 and its
 * linux_80211.c glue, vendored in src/lkpi80211/).  Until then this file
 * lets mt7925_pci_probe() run end to end: hw/wiphy allocation keeps the
 * Linux layout the driver relies on, SET_IEEE80211_DEV/register_hw log the
 * capabilities the driver advertises, work queued through the hw lands on a
 * real workqueue so firmware events and the init_work still run, and every
 * data-path call is a no-op that frees what it is handed.
 *
 * Nothing here must be reachable once the real glue exists: the file is
 * simply dropped from the build then.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>

int linuxkpi_debug_80211;
/* Number of ieee80211_register_hw() calls that reached the stub; for host tests. */
int lkpi_stub_hw_registered;

/* mac80211 exports these LLC/SNAP headers for the 802.3 <-> 802.11 conversion. */
const unsigned char rfc1042_header[] __aligned(2) = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
const unsigned char bridge_tunnel_header[] __aligned(2) = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };

struct lkpi_stub_wiphy {
	const struct cfg80211_ops *ops;
	bool registered;
	struct wiphy wiphy;		/* flexible priv[] follows: must stay last */
};

struct lkpi_stub_hw {
	const struct ieee80211_ops *ops;
	struct workqueue_struct *workq;
	bool emulate_chanctx;
	bool registered;
	struct ieee80211_hw hw;
} __aligned(CACHE_LINE_SIZE);

#define WIPHY_TO_LWIPHY(w)	container_of(w, struct lkpi_stub_wiphy, wiphy)
#define HW_TO_LHW(h)		container_of(h, struct lkpi_stub_hw, hw)

static const char *band_name(enum nl80211_band band)
{
	switch (band) {
	case NL80211_BAND_2GHZ: return "2.4 GHz";
	case NL80211_BAND_5GHZ: return "5 GHz";
	case NL80211_BAND_6GHZ: return "6 GHz";
	case NL80211_BAND_60GHZ: return "60 GHz";
	default: return "?";
	}
}

/* ---- wiphy ---- */
struct wiphy *linuxkpi_wiphy_new(const struct cfg80211_ops *ops, size_t priv_len)
{
	struct lkpi_stub_wiphy *lw;

	lw = kzalloc(sizeof(*lw) + priv_len, GFP_KERNEL);
	if (!lw)
		return NULL;
	lw->ops = ops;
	INIT_LIST_HEAD(&lw->wiphy.wdev_list);
	mutex_init(&lw->wiphy.mtx);
	lkpi_device_init(&lw->wiphy.dev, NULL, "phy0");
	return &lw->wiphy;
}

void linuxkpi_wiphy_free(struct wiphy *wiphy)
{
	struct lkpi_stub_wiphy *lw;

	if (!wiphy)
		return;
	lw = WIPHY_TO_LWIPHY(wiphy);
	lkpi_device_fini(&wiphy->dev);
	mutex_destroy(&wiphy->mtx);
	kfree(lw);
}

int linuxkpi_80211_wiphy_register(struct wiphy *wiphy)
{
	struct lkpi_stub_wiphy *lw = WIPHY_TO_LWIPHY(wiphy);
	int band;

	lw->registered = true;
	pr_info("mac80211-stub: wiphy %s registered: iftypes 0x%x, max_scan_ssids %u, "
		"max_sched_scan_ssids %u, n_cipher_suites %d, flags 0x%x\n",
		wiphy_name(wiphy), wiphy->interface_modes, wiphy->max_scan_ssids,
		wiphy->max_sched_scan_ssids, wiphy->n_cipher_suites, wiphy->flags);
	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		struct ieee80211_supported_band *sband = wiphy->bands[band];
		int i, usable = 0;

		if (!sband)
			continue;
		for (i = 0; i < sband->n_channels; i++)
			if (!(sband->channels[i].flags & IEEE80211_CHAN_DISABLED))
				usable++;
		pr_info("mac80211-stub:   %s: %d channels (%d enabled), %d legacy rates, "
			"HT %d, VHT %d, %d iftype-data entries (HE/EHT)\n",
			band_name(band), sband->n_channels, usable, sband->n_bitrates,
			sband->ht_cap.ht_supported, sband->vht_cap.vht_supported,
			sband->n_iftype_data);
	}
	return 0;
}

struct ieee80211_hw *linuxkpi_wiphy_to_ieee80211_hw(struct wiphy *wiphy)
{
	struct lkpi_stub_hw *lhw = wiphy_priv(wiphy);

	return &lhw->hw;
}

/* ---- hw ---- */
struct ieee80211_hw *linuxkpi_ieee80211_alloc_hw(size_t priv_len, const struct ieee80211_ops *ops)
{
	struct lkpi_stub_hw *lhw;
	struct wiphy *wiphy;
	struct ieee80211_hw *hw;
	bool emu;

	emu = ops->add_chanctx == ieee80211_emulate_add_chanctx &&
	      ops->change_chanctx == ieee80211_emulate_change_chanctx &&
	      ops->remove_chanctx == ieee80211_emulate_remove_chanctx;
	if (!emu && (!ops->add_chanctx || !ops->change_chanctx || !ops->remove_chanctx ||
		     !ops->assign_vif_chanctx || !ops->unassign_vif_chanctx)) {
		pr_err("mac80211-stub: driver ops lack channel-context callbacks\n");
		return NULL;
	}

	wiphy = linuxkpi_wiphy_new(NULL, sizeof(*lhw) + priv_len);
	if (!wiphy)
		return NULL;

	lhw = wiphy_priv(wiphy);
	lhw->ops = ops;
	lhw->emulate_chanctx = emu;
	lhw->workq = alloc_ordered_workqueue("lkpi-80211", 0);
	if (!lhw->workq) {
		linuxkpi_wiphy_free(wiphy);
		return NULL;
	}

	hw = &lhw->hw;
	hw->wiphy = wiphy;
	hw->conf.flags |= IEEE80211_CONF_IDLE;
	hw->priv = (void *)(lhw + 1);
	return hw;
}

void linuxkpi_ieee80211_iffree(struct ieee80211_hw *hw)
{
	struct lkpi_stub_hw *lhw;

	if (!hw)
		return;
	lhw = HW_TO_LHW(hw);
	if (lhw->workq)
		destroy_workqueue(lhw->workq);
	lhw->workq = NULL;
	/* The wiphy (and with it lhw and the driver priv) is freed by ieee80211_free_hw -> wiphy_free. */
}

void linuxkpi_set_ieee80211_dev(struct ieee80211_hw *hw)
{
	struct device *dev = wiphy_dev(hw->wiphy);

	if (dev)
		dev_set_name(&hw->wiphy->dev, "%s-phy", dev_name(dev));
}

int linuxkpi_ieee80211_ifattach(struct ieee80211_hw *hw)
{
	struct lkpi_stub_hw *lhw = HW_TO_LHW(hw);

	lhw->registered = true;
	lkpi_stub_hw_registered++;
	pr_info("mac80211-stub: hw registered (no 802.11 stack yet): queues %u, "
		"max_rates %u, max_rate_tries %u, extra_tx_headroom %u, vif_data_size %zu, "
		"sta_data_size %zu, chanctx_data_size %zu, txq_data_size %zu, "
		"max_rx_aggregation %u, max_tx_aggregation %u%s\n",
		hw->queues, hw->max_rates, hw->max_rate_tries, hw->extra_tx_headroom,
		(size_t)hw->vif_data_size, (size_t)hw->sta_data_size, (size_t)hw->chanctx_data_size,
		(size_t)hw->txq_data_size, hw->max_rx_aggregation_subframes,
		hw->max_tx_aggregation_subframes,
		lhw->emulate_chanctx ? ", chanctx emulated" : "");
	return 0;
}

void linuxkpi_ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct lkpi_stub_hw *lhw = HW_TO_LHW(hw);

	lhw->registered = false;
	if (lhw->workq)
		flush_workqueue(lhw->workq);
	pr_info("mac80211-stub: hw unregistered\n");
}

/* ---- work ---- */
void linuxkpi_ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *w)
{
	struct lkpi_stub_hw *lhw = HW_TO_LHW(hw);

	queue_work(lhw->workq ? lhw->workq : system_wq, w);
}

void linuxkpi_ieee80211_queue_delayed_work(struct ieee80211_hw *hw, struct delayed_work *w, int delay)
{
	struct lkpi_stub_hw *lhw = HW_TO_LHW(hw);

	queue_delayed_work(lhw->workq ? lhw->workq : system_wq, w, delay);
}

/* ---- channel contexts: upstream helpers for drivers that emulate them ---- */
int ieee80211_emulate_add_chanctx(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx)
{
	hw->conf.radar_enabled = ctx->radar_enabled;
	hw->conf.chandef = ctx->def;
	return 0;
}

void ieee80211_emulate_change_chanctx(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx,
    uint32_t changed)
{
	(void)changed;
	hw->conf.radar_enabled = ctx->radar_enabled;
	hw->conf.chandef = ctx->def;
}

void ieee80211_emulate_remove_chanctx(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx)
{
	(void)hw;
	(void)ctx;
}

int ieee80211_emulate_switch_vif_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_vif_chanctx_switch *vifs, int n_vifs, enum ieee80211_chanctx_switch_mode mode)
{
	(void)hw; (void)vifs; (void)n_vifs; (void)mode;
	return 0;
}

/* ---- channel / frequency helpers (pure arithmetic, 802.11-2020 17.3.8.4.2) ---- */
uint32_t linuxkpi_ieee80211_channel_to_frequency(uint32_t chan, enum nl80211_band band)
{
	switch (band) {
	case NL80211_BAND_2GHZ:
		if (chan == 14)
			return 2484;
		if (chan < 14)
			return 2407 + chan * 5;
		return 0;
	case NL80211_BAND_5GHZ:
		if (chan >= 182 && chan <= 196)
			return 4000 + chan * 5;
		return 5000 + chan * 5;
	case NL80211_BAND_6GHZ:
		if (chan == 2)
			return 5935;
		if (chan <= 233)
			return 5950 + chan * 5;
		return 0;
	case NL80211_BAND_60GHZ:
		if (chan < 7)
			return 56160 + chan * 2160;
		return 0;
	default:
		return 0;
	}
}

uint32_t linuxkpi_ieee80211_frequency_to_channel(uint32_t freq, uint32_t flags)
{
	(void)flags;
	if (freq == 2484)
		return 14;
	if (freq < 2484)
		return (freq - 2407) / 5;
	if (freq >= 4910 && freq <= 4980)
		return (freq - 4000) / 5;
	if (freq < 5925)
		return (freq - 5000) / 5;
	if (freq == 5935)
		return 2;
	if (freq <= 45000)
		return (freq - 5950) / 5;
	if (freq >= 58320 && freq <= 70200)
		return (freq - 56160) / 2160;
	return 0;
}

/* ---- regulatory ---- */
struct linuxkpi_ieee80211_regdomain *lkpi_get_linuxkpi_ieee80211_regdomain(size_t n_rules)
{
	return kzalloc(sizeof(struct linuxkpi_ieee80211_regdomain) +
		       n_rules * sizeof(struct ieee80211_reg_rule), GFP_KERNEL);
}

/* ---- everything below needs a station/interface model: no-ops for now ---- */
#define STUB_ONCE(fmt, ...) pr_info_once("mac80211-stub: %s called before the 802.11 stack exists" fmt "\n", __func__, ##__VA_ARGS__)

void linuxkpi_ieee80211_rx(struct ieee80211_hw *hw, struct sk_buff *skb, struct ieee80211_sta *sta,
    struct napi_struct *napi, struct list_head *list)
{
	(void)hw; (void)sta; (void)napi; (void)list;
	STUB_ONCE(" (RX frames are dropped)");
	kfree_skb(skb);
}

void linuxkpi_ieee80211_tx_status_ext(struct ieee80211_hw *hw, struct ieee80211_tx_status *st)
{
	(void)hw;
	STUB_ONCE("");
	if (st && st->skb)
		kfree_skb(st->skb);
}

void linuxkpi_ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb, int status)
{
	(void)hw; (void)status;
	kfree_skb(skb);
}

void linuxkpi_ieee80211_beacon_loss(struct ieee80211_vif *vif) { (void)vif; STUB_ONCE(""); }
void linuxkpi_ieee80211_connection_loss(struct ieee80211_vif *vif) { (void)vif; STUB_ONCE(""); }

struct ieee80211_sta *linuxkpi_ieee80211_find_sta(struct ieee80211_vif *vif, const u8 *addr)
{
	(void)vif; (void)addr;
	return NULL;
}

struct ieee80211_sta *linuxkpi_ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw,
    const uint8_t *addr, const uint8_t *localaddr)
{
	(void)hw; (void)addr; (void)localaddr;
	return NULL;
}

void linuxkpi_ieee80211_iterate_interfaces(struct ieee80211_hw *hw, enum ieee80211_iface_iter flags,
    void (*iterfunc)(void *, uint8_t *, struct ieee80211_vif *), void *arg)
{
	(void)hw; (void)flags; (void)iterfunc; (void)arg;	/* no interfaces exist */
}

void linuxkpi_ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
    void (*iterfunc)(void *, struct ieee80211_sta *), void *arg)
{
	(void)hw; (void)iterfunc; (void)arg;
}

struct ieee80211_txq *linuxkpi_ieee80211_next_txq(struct ieee80211_hw *hw, uint8_t ac)
{
	(void)hw; (void)ac;
	return NULL;
}

void linuxkpi_ieee80211_schedule_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq, bool withoutpkts)
{
	(void)hw; (void)txq; (void)withoutpkts;
}

void linuxkpi_ieee80211_txq_schedule_start(struct ieee80211_hw *hw, uint8_t ac) { (void)hw; (void)ac; }

struct sk_buff *linuxkpi_ieee80211_tx_dequeue(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
	(void)hw; (void)txq;
	return NULL;
}

void linuxkpi_ieee80211_txq_get_depth(struct ieee80211_txq *txq, unsigned long *frame_cnt,
    unsigned long *byte_cnt)
{
	(void)txq;
	if (frame_cnt)
		*frame_cnt = 0;
	if (byte_cnt)
		*byte_cnt = 0;
}

u32 ieee80211_txq_aql_pending(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
	(void)hw; (void)txq;
	return 0;
}

struct sk_buff *linuxkpi_ieee80211_nullfunc_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    int link_id, bool qos)
{
	(void)hw; (void)vif; (void)link_id; (void)qos;
	STUB_ONCE("");
	return NULL;
}

struct sk_buff *linuxkpi_ieee80211_probereq_get(struct ieee80211_hw *hw, const uint8_t *addr,
    const uint8_t *ssid, size_t ssid_len, size_t tailroom)
{
	(void)hw; (void)addr; (void)ssid; (void)ssid_len; (void)tailroom;
	STUB_ONCE("");
	return NULL;
}

void linuxkpi_ieee80211_scan_completed(struct ieee80211_hw *hw, struct cfg80211_scan_info *info)
{
	(void)hw;
	pr_info("mac80211-stub: scan completed (aborted=%d)\n", info ? info->aborted : 0);
}

int linuxkpi_ieee80211_start_tx_ba_session(struct ieee80211_sta *sta, uint8_t tid, int timeout)
{
	(void)sta; (void)tid; (void)timeout;
	return -EOPNOTSUPP;
}

void linuxkpi_ieee80211_stop_queues(struct ieee80211_hw *hw) { (void)hw; }
void linuxkpi_ieee80211_wake_queues(struct ieee80211_hw *hw) { (void)hw; }
