/* SPDX-License-Identifier: MIT */
/*
 * The 802.11 stack as the host test sees it.  wlan_host.c is compiled like
 * the mac80211 -> net80211 glue (bsdkpi + LinuxKPI in one translation unit)
 * and drives net80211 through the entry points the IO80211 layer will use
 * in phase 4: the "wlan" cloner, the interface ioctl and the scan cache.
 * This header is plain C so that test_probe.c, a LinuxKPI translation
 * unit, can include it.
 */
#ifndef _LKPI_WLAN_HOST_H_
#define _LKPI_WLAN_HOST_H_
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct wlan_host_ic_info {
	char		name[32];	/* ic_name: the parent device's name */
	uint8_t		macaddr[6];
	int		nchans;		/* net80211 channel table entries */
	int		freqs_2ghz;	/* distinct centre frequencies per band */
	int		freqs_5ghz;
	int		freqs_6ghz;
	bool		cap_sta;	/* IEEE80211_C_STA */
	bool		cap_wpa;	/* IEEE80211_C_WPA */
	bool		ht, vht;	/* HT/VHT channels present */
	unsigned int	cryptocaps;
	int		nvaps;
};

/* Number of attached ieee80211coms; fills *info for the first one. */
int	wlan_host_ic_info(struct wlan_host_ic_info *info);

/* Create a station vap on the first com through the "wlan" cloner. */
int	wlan_host_vap_create(void);
const char *wlan_host_vap_name(void);
/* SIOCSIFFLAGS with or without IFF_UP, through the vap's if_ioctl. */
int	wlan_host_vap_updown(bool up);
/* enum ieee80211_state of the vap (0 INIT, 1 SCAN, ...), -1 without vap. */
int	wlan_host_vap_state(void);
/* The glue's view: the driver was started / a hw scan is running. */
bool	wlan_host_hw_scan_running(void);
/* IEEE80211_IOC_ROAMING = IEEE80211_ROAMING_MANUAL through SIOCS80211. */
int	wlan_host_set_roaming_manual(void);
/* IEEE80211_IOC_SCAN_REQ through SIOCS80211: active scan, all channels. */
int	wlan_host_scan_request(void);
/* True while net80211 has a scan in progress (IEEE80211_F_SCAN). */
bool	wlan_host_scanning(void);
/* Number of scan-cache entries; if ssid != NULL, of entries with that SSID. */
int	wlan_host_scan_results(const char *ssid);
int	wlan_host_vap_destroy(void);
/* Glue (D80211_SCAN) and net80211 (scan, state, input) tracing, all vaps. */
void	wlan_host_set_debug(bool on);
#endif
