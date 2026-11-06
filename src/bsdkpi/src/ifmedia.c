// SPDX-License-Identifier: MIT
/*
 * ifmedia(4): the media list net80211 uses to report its operating mode
 * and rate (IFM_IEEE80211_*), with SIOCSIFMEDIA/SIOCGIFMEDIA handling.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

static struct ifmedia_entry *
ifmedia_match(struct ifmedia *ifm, int target, int mask)
{
	struct ifmedia_entry *match, *next;

	match = NULL;
	mask = ~mask;
	LIST_FOREACH(next, &ifm->ifm_list, ifm_list) {
		if ((next->ifm_media & mask) == (target & mask)) {
			if (match != NULL)
				printf("ifmedia_match: multiple match for 0x%x/0x%x\n",
				    target, mask);
			match = next;
		}
	}
	return (match);
}

void
ifmedia_init(struct ifmedia *ifm, int dontcare_mask,
    ifm_change_cb_t change_callback, ifm_stat_cb_t status_callback)
{
	LIST_INIT(&ifm->ifm_list);
	ifm->ifm_cur = NULL;
	ifm->ifm_media = 0;
	ifm->ifm_mask = dontcare_mask;
	ifm->ifm_change = change_callback;
	ifm->ifm_status = status_callback;
}

void
ifmedia_removeall(struct ifmedia *ifm)
{
	struct ifmedia_entry *entry;

	while ((entry = LIST_FIRST(&ifm->ifm_list)) != NULL) {
		LIST_REMOVE(entry, ifm_list);
		free(entry, M_IFADDR);
	}
	ifm->ifm_cur = NULL;
}

void
ifmedia_add(struct ifmedia *ifm, int mword, int data, void *aux)
{
	struct ifmedia_entry *entry;

	entry = malloc(sizeof(*entry), M_IFADDR, M_NOWAIT);
	if (entry == NULL)
		panic("ifmedia_add: can't malloc entry");
	entry->ifm_media = mword;
	entry->ifm_data = data;
	entry->ifm_aux = aux;
	LIST_INSERT_HEAD(&ifm->ifm_list, entry, ifm_list);
}

void
ifmedia_list_add(struct ifmedia *ifm, struct ifmedia_entry *lp, int count)
{
	int i;

	for (i = 0; i < count; i++)
		ifmedia_add(ifm, lp[i].ifm_media, lp[i].ifm_data, lp[i].ifm_aux);
}

void
ifmedia_set(struct ifmedia *ifm, int target)
{
	struct ifmedia_entry *match;

	match = ifmedia_match(ifm, target, ifm->ifm_mask);
	if (match == NULL) {
		printf("ifmedia_set: no match for 0x%x/0x%x\n", target,
		    ~ifm->ifm_mask);
		panic("ifmedia_set");
	}
	ifm->ifm_cur = match;
	ifm->ifm_media = target;
}

static int
compat_media(int media)
{
	if (IFM_TYPE(media) == IFM_ETHER && IFM_SUBTYPE(media) > IFM_OTHER) {
		media &= ~IFM_ETH_XTYPE;
		media |= IFM_OTHER;
	}
	return (media);
}

int
ifmedia_ioctl(struct ifnet *ifp, struct ifreq *ifr, struct ifmedia *ifm,
    u_long cmd)
{
	struct ifmedia_entry *match;
	struct ifmediareq *ifmr = (struct ifmediareq *)ifr;
	int error = 0;

	if (ifp == NULL || ifr == NULL || ifm == NULL)
		return (EINVAL);

	switch (cmd) {
	case SIOCSIFMEDIA: {
		struct ifmedia_entry *oldentry;
		int oldmedia;
		int newmedia = ifr->ifr_media;

		match = ifmedia_match(ifm, newmedia, ifm->ifm_mask);
		if (match == NULL)
			return (ENXIO);
		/*
		 * If no change, we're done.  XXX Automedia may involve
		 * software intervention; keep going for IFM_AUTO.
		 */
		if ((IFM_SUBTYPE(newmedia) != IFM_AUTO) &&
		    (newmedia == ifm->ifm_media) && (match == ifm->ifm_cur))
			return (0);
		oldentry = ifm->ifm_cur;
		oldmedia = ifm->ifm_media;
		ifm->ifm_cur = match;
		ifm->ifm_media = newmedia;
		error = (*ifm->ifm_change)(ifp);
		if (error) {
			ifm->ifm_cur = oldentry;
			ifm->ifm_media = oldmedia;
		}
		break;
	}
	case SIOCGIFMEDIA:
	case SIOCGIFXMEDIA: {
		struct ifmedia_entry *ep;
		int i;

		if (ifmr->ifm_count < 0)
			return (EINVAL);

		if (cmd == SIOCGIFMEDIA) {
			ifmr->ifm_active = ifmr->ifm_current = ifm->ifm_cur ?
			    compat_media(ifm->ifm_cur->ifm_media) : IFM_NONE;
		} else {
			ifmr->ifm_active = ifmr->ifm_current = ifm->ifm_cur ?
			    ifm->ifm_cur->ifm_media : IFM_NONE;
		}
		ifmr->ifm_mask = ifm->ifm_mask;
		ifmr->ifm_status = 0;
		(*ifm->ifm_status)(ifp, ifmr);

		/* Copy the media words into the caller's array (already in
		 * kernel memory here). */
		i = 0;
		LIST_FOREACH(ep, &ifm->ifm_list, ifm_list) {
			if (i < ifmr->ifm_count && ifmr->ifm_ulist != NULL)
				ifmr->ifm_ulist[i] = (cmd == SIOCGIFMEDIA) ?
				    compat_media(ep->ifm_media) : ep->ifm_media;
			i++;
		}
		if (ifmr->ifm_count != 0 && i > ifmr->ifm_count)
			error = E2BIG;
		ifmr->ifm_count = i;
		break;
	}
	default:
		return (EINVAL);
	}
	return (error);
}

uint64_t
ifmedia_baudrate(int mword)
{
	/* Only the 802.11 rates matter here: the subtype encodes the rate. */
	if (IFM_TYPE(mword) != IFM_IEEE80211)
		return (0);
	switch (IFM_SUBTYPE(mword)) {
	case IFM_IEEE80211_FH1:		return (IF_Mbps(1));
	case IFM_IEEE80211_FH2:		return (IF_Mbps(2));
	case IFM_IEEE80211_DS1:		return (IF_Mbps(1));
	case IFM_IEEE80211_DS2:		return (IF_Mbps(2));
	case IFM_IEEE80211_DS5:		return (IF_Kbps(5500));
	case IFM_IEEE80211_DS11:	return (IF_Mbps(11));
	case IFM_IEEE80211_DS22:	return (IF_Mbps(22));
	case IFM_IEEE80211_OFDM6:	return (IF_Mbps(6));
	case IFM_IEEE80211_OFDM9:	return (IF_Mbps(9));
	case IFM_IEEE80211_OFDM12:	return (IF_Mbps(12));
	case IFM_IEEE80211_OFDM18:	return (IF_Mbps(18));
	case IFM_IEEE80211_OFDM24:	return (IF_Mbps(24));
	case IFM_IEEE80211_OFDM36:	return (IF_Mbps(36));
	case IFM_IEEE80211_OFDM48:	return (IF_Mbps(48));
	case IFM_IEEE80211_OFDM54:	return (IF_Mbps(54));
	case IFM_IEEE80211_OFDM72:	return (IF_Mbps(72));
	default:			return (0);
	}
}
