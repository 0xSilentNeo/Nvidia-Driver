// SPDX-License-Identifier: MIT
/*
 * Host-test driver for the 802.11 stack: what the IO80211 layer will do in
 * phase 4, reduced to the calls a test needs.  Compiled with the flags of
 * src/lkpi80211 (the prelude gives it both the FreeBSD and the Linux view).
 */
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_private.h>
#include <net/if_clone.h>
#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_scan.h>
#include <bsdkpi/bsdkpi.h>

#define	LINUXKPI_NET80211
#include <net/mac80211.h>
#include "linux_80211.h"

#include "wlan_host.h"

static struct ifnet *wlan_ifp;		/* the one vap this file creates */
static bool wlan_debug;

struct ic_iter {
	struct ieee80211com *first;
	int count;
};

static void
ic_iter_cb(void *arg, struct ieee80211com *ic)
{
	struct ic_iter *it = arg;

	if (it->first == NULL)
		it->first = ic;
	it->count++;
}

static struct ieee80211com *
first_ic(int *countp)
{
	struct ic_iter it = { NULL, 0 };

	ieee80211_iterate_coms(ic_iter_cb, &it);
	if (countp != NULL)
		*countp = it.count;
	return (it.first);
}

static struct ieee80211vap *
the_vap(void)
{
	return (wlan_ifp != NULL ? wlan_ifp->if_softc : NULL);
}

/* Count the distinct centre frequencies in [lo, hi]. */
static int
count_freqs(const struct ieee80211com *ic, int lo, int hi)
{
	int i, j, n = 0;

	for (i = 0; i < ic->ic_nchans; i++) {
		int f = ic->ic_channels[i].ic_freq;

		if (f < lo || f > hi)
			continue;
		for (j = 0; j < i; j++)
			if (ic->ic_channels[j].ic_freq == f)
				break;
		if (j == i)
			n++;
	}
	return (n);
}

int
wlan_host_ic_info(struct wlan_host_ic_info *info)
{
	struct ieee80211com *ic;
	struct ieee80211vap *vap;
	int count, i;

	ic = first_ic(&count);
	memset(info, 0, sizeof(*info));
	if (ic == NULL)
		return (0);
	strlcpy(info->name, ic->ic_name, sizeof(info->name));
	memcpy(info->macaddr, ic->ic_macaddr, sizeof(info->macaddr));
	info->nchans = ic->ic_nchans;
	info->freqs_2ghz = count_freqs(ic, 2400, 2500);
	info->freqs_5ghz = count_freqs(ic, 5000, 5924);
	info->freqs_6ghz = count_freqs(ic, 5925, 7200);
	info->cap_sta = (ic->ic_caps & IEEE80211_C_STA) != 0;
	info->cap_wpa = (ic->ic_caps & IEEE80211_C_WPA) != 0;
	info->cryptocaps = ic->ic_cryptocaps;
	for (i = 0; i < ic->ic_nchans; i++) {
		if (IEEE80211_IS_CHAN_HT(&ic->ic_channels[i]))
			info->ht = true;
		if (IEEE80211_IS_CHAN_VHT(&ic->ic_channels[i]))
			info->vht = true;
	}
	IEEE80211_LOCK(ic);
	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next)
		info->nvaps++;
	IEEE80211_UNLOCK(ic);
	return (count);
}

int
wlan_host_vap_create(void)
{
	struct ieee80211_clone_params cp;
	struct ifc_data ifd;
	struct ieee80211com *ic;
	int error;

	ic = first_ic(NULL);
	if (ic == NULL)
		return (ENXIO);
	if (wlan_ifp != NULL)
		return (EEXIST);
	memset(&cp, 0, sizeof(cp));
	strlcpy(cp.icp_parent, ic->ic_name, sizeof(cp.icp_parent));
	cp.icp_opmode = IEEE80211_M_STA;
	memset(&ifd, 0, sizeof(ifd));
	ifd.flags = IFC_F_SYSSPACE;
	ifd.params = &cp;
	error = bsdkpi_if_clone_create("wlan", &ifd, &wlan_ifp);
	if (error == 0)
		wlan_host_set_debug(wlan_debug);
	return (error);
}

const char *
wlan_host_vap_name(void)
{
	return (wlan_ifp != NULL ? wlan_ifp->if_xname : "");
}

int
wlan_host_vap_updown(bool up)
{
	struct ifreq ifr;

	if (wlan_ifp == NULL)
		return (ENXIO);
	/* What ifhwioctl() does before it calls the driver. */
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, wlan_ifp->if_xname, sizeof(ifr.ifr_name));
	if (up)
		wlan_ifp->if_flags |= IFF_UP;
	else
		wlan_ifp->if_flags &= ~IFF_UP;
	return (wlan_ifp->if_ioctl(wlan_ifp, SIOCSIFFLAGS, (caddr_t)&ifr));
}

int
wlan_host_vap_state(void)
{
	struct ieee80211vap *vap = the_vap();

	return (vap != NULL ? (int)vap->iv_state : -1);
}

bool
wlan_host_hw_scan_running(void)
{
	struct ieee80211com *ic = first_ic(NULL);
	struct lkpi_hw *lhw;

	if (ic == NULL)
		return (false);
	lhw = ic->ic_softc;
	return ((lhw->scan_flags & (LKPI_LHW_SCAN_RUNNING | LKPI_LHW_SCAN_HW)) ==
	    (LKPI_LHW_SCAN_RUNNING | LKPI_LHW_SCAN_HW));
}

int
wlan_host_set_roaming_manual(void)
{
	struct ieee80211req ireq;

	if (wlan_ifp == NULL)
		return (ENXIO);
	memset(&ireq, 0, sizeof(ireq));
	strlcpy(ireq.i_name, wlan_ifp->if_xname, sizeof(ireq.i_name));
	ireq.i_type = IEEE80211_IOC_ROAMING;
	ireq.i_val = IEEE80211_ROAMING_MANUAL;
	return (wlan_ifp->if_ioctl(wlan_ifp, SIOCS80211, (caddr_t)&ireq));
}

int
wlan_host_scan_request(void)
{
	struct ieee80211_scan_req sr;
	struct ieee80211req ireq;

	if (wlan_ifp == NULL)
		return (ENXIO);
	memset(&sr, 0, sizeof(sr));
	sr.sr_flags = IEEE80211_IOC_SCAN_ACTIVE | IEEE80211_IOC_SCAN_ONCE |
	    IEEE80211_IOC_SCAN_NOPICK | IEEE80211_IOC_SCAN_NOJOIN |
	    IEEE80211_IOC_SCAN_FLUSH;	/* or a "hot" cache answers it */
	sr.sr_duration = IEEE80211_IOC_SCAN_FOREVER;
	sr.sr_mindwell = 0;
	sr.sr_maxdwell = 0;
	sr.sr_nssid = 0;
	memset(&ireq, 0, sizeof(ireq));
	strlcpy(ireq.i_name, wlan_ifp->if_xname, sizeof(ireq.i_name));
	ireq.i_type = IEEE80211_IOC_SCAN_REQ;
	ireq.i_data = &sr;
	ireq.i_len = sizeof(sr);
	return (wlan_ifp->if_ioctl(wlan_ifp, SIOCS80211, (caddr_t)&ireq));
}

bool
wlan_host_scanning(void)
{
	struct ieee80211com *ic = first_ic(NULL);

	return (ic != NULL && (ic->ic_flags & IEEE80211_F_SCAN) != 0);
}

struct scan_count {
	const char *ssid;
	int n;
};

static void
scan_count_cb(void *arg, const struct ieee80211_scan_entry *se)
{
	struct scan_count *sc = arg;

	/* se_ssid is the SSID element: id, length, bytes. */
	if (sc->ssid != NULL &&
	    (se->se_ssid[1] != strlen(sc->ssid) ||
	    memcmp(se->se_ssid + 2, sc->ssid, se->se_ssid[1]) != 0))
		return;
	sc->n++;
}

int
wlan_host_scan_results(const char *ssid)
{
	struct ieee80211vap *vap = the_vap();
	struct scan_count sc = { ssid, 0 };

	if (vap == NULL)
		return (-1);
	ieee80211_scan_iterate(vap, scan_count_cb, &sc);
	return (sc.n);
}



void
wlan_host_set_debug(bool on)
{
	struct ieee80211vap *vap = the_vap();

	wlan_debug = on;
	linuxkpi_debug_80211 = on ? (D80211_SCAN | D80211_SCAN_BEACON) : 0;
	if (vap != NULL)
		vap->iv_debug = on ? (IEEE80211_MSG_SCAN | IEEE80211_MSG_STATE |
		    IEEE80211_MSG_INPUT) : 0;
}

int
wlan_host_vap_destroy(void)
{
	int error;

	if (wlan_ifp == NULL)
		return (ENXIO);
	error = bsdkpi_if_clone_destroy(wlan_ifp);
	wlan_ifp = NULL;
	return (error);
}
