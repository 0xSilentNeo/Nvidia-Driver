/* SPDX-License-Identifier: MIT */
/*
 * mac80211/cfg80211 API pieces that the vendored FreeBSD headers do not have
 * yet (they track Linux with some lag) but that current mt76 uses.  The
 * vendored net/mac80211.h includes this file at its end.  Values and
 * layouts follow the Linux API so the driver's expectations hold.
 */
#ifndef _LKPI_MAC80211_COMPAT_H_
#define _LKPI_MAC80211_COMPAT_H_

#define IEEE80211_DEFAULT_AIRTIME_WEIGHT	256

/* EML capabilities field (802.11be 9.4.2.312.2.3).  FreeBSD's header carries
 * placeholder values; these bits are sent to the firmware, so use the real
 * field masks. */
#undef IEEE80211_EML_CAP_EMLSR_SUPP
#undef IEEE80211_EML_CAP_TRANSITION_TIMEOUT
#undef IEEE80211_EML_CAP_TRANSITION_TIMEOUT_128TU
#define IEEE80211_EML_CAP_EMLSR_SUPP			0x0001
#define IEEE80211_EML_CAP_EML_PADDING_DELAY		0x000e
#define IEEE80211_EML_CAP_EML_TRANSITION_DELAY		0x0070
#define IEEE80211_EML_CAP_EMLMR_SUPPORT			0x0080
#define IEEE80211_EML_CAP_EMLMR_DELAY			0x0700
#define IEEE80211_EML_CAP_TRANSITION_TIMEOUT		0x7800
#define IEEE80211_EML_CAP_TRANSITION_TIMEOUT_128TU	0x0800

/* ---- NAN (Wi-Fi Aware) schedule plumbing ------------------------------
 * mt7925 carries NAN support; macOS has no consumer for it, so the glue
 * never starts a NAN interface, but the types must exist for the driver to
 * compile and its structures to be sized.
 */
#define CFG80211_NAN_SCHED_NUM_TIME_SLOTS	32
#define CFG80211_NAN_MAX_PEER_MAPS		2
#define CFG80211_NAN_INVALID_MAP_ID		0xff
#define IEEE80211_NAN_MAX_CHANNELS		3
#define IEEE80211_NAN_AVAIL_BLOB_MAX_LEN	54


struct ieee80211_nan_channel {
	struct ieee80211_chanreq chanreq;	/* FreeBSD spells the Linux ieee80211_chan_req this way */
	u8 needed_rx_chains;
	struct ieee80211_chanctx_conf *chanctx_conf;
	u8 channel_entry[6];
	bool no_evacuate;
};
struct ieee80211_nan_peer_map {
	u8 map_id;
	struct ieee80211_nan_channel *slots[CFG80211_NAN_SCHED_NUM_TIME_SLOTS];
};
struct ieee80211_nan_peer_sched {
	u8 seq_id;
	u16 committed_dw;
	u16 max_chan_switch;
	const u8 *init_ulw;
	u16 ulw_size;
	struct ieee80211_nan_peer_map maps[CFG80211_NAN_MAX_PEER_MAPS];
	u8 n_channels;
	struct ieee80211_nan_channel channels[];
};
struct ieee80211_nan_sched_cfg {
	struct ieee80211_nan_channel channels[IEEE80211_NAN_MAX_CHANNELS];
	struct ieee80211_nan_channel *schedule[CFG80211_NAN_SCHED_NUM_TIME_SLOTS];
	u8 avail_blob[IEEE80211_NAN_AVAIL_BLOB_MAX_LEN];
	u16 avail_blob_len;
	bool deferred;
};
void ieee80211_nan_cluster_joined(struct ieee80211_vif *vif, const u8 *cluster_id,
    bool new_cluster, gfp_t gfp);
void ieee80211_nan_sched_update_done(struct ieee80211_vif *vif);
void cfg80211_nan_ulw_update(struct wireless_dev *wdev, const u8 *ulw, size_t ulw_len, gfp_t gfp);

/* ---- QoS flow info (SCS) ---- */
struct cfg80211_flow_info {
	u8 dialog_token;
	u8 tid;
	u8 user_prio;
	u16 flags;
};

/* ---- airtime queue limits ---- */
u32 ieee80211_txq_aql_pending(struct ieee80211_hw *hw, struct ieee80211_txq *txq);

/* ---- throughput LED trigger (no LED on macOS; returns NULL = no trigger) ---- */
struct ieee80211_tpt_blink {
	int throughput;
	int blink_time;
};
enum ieee80211_tpt_led_trigger_flags {
	IEEE80211_TPT_LEDTRIG_FL_RADIO		= BIT(0),
	IEEE80211_TPT_LEDTRIG_FL_WORK		= BIT(1),
	IEEE80211_TPT_LEDTRIG_FL_CONNECTED	= BIT(2),
};
static inline const char *
ieee80211_create_tpt_led_trigger(struct ieee80211_hw *hw, unsigned int flags,
    const struct ieee80211_tpt_blink *blink_table, unsigned int blink_table_len)
{
	(void)hw; (void)flags; (void)blink_table; (void)blink_table_len;
	return NULL;
}
#endif /* _LKPI_MAC80211_COMPAT_H_ */
