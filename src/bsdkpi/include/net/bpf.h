/* SPDX-License-Identifier: MIT */
/*
 * net/bpf.h - bpf(9) with no listeners.  bpf_attach() returns NULL, so
 * bpf_peers_present() is 0 and every tap is a no-op; the radiotap
 * machinery in net80211 then stays dormant (IEEE80211_FEXT_BPF unset).
 */
#ifndef _BSDKPI_NET_BPF_H_
#define _BSDKPI_NET_BPF_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <net/dlt.h>

struct mbuf;
struct ifnet;
struct bpf_if;
struct bpf_d;

#define BPF_ALIGNMENT	sizeof(long)
#define BPF_WORDALIGN(x) (((x) + (BPF_ALIGNMENT - 1)) & ~(BPF_ALIGNMENT - 1))
#define BPF_MAXINSNS	512
#define BPF_MAXBUFSIZE	0x80000
#define BPF_MINBUFSIZE	32

enum bpf_direction {
	BPF_D_IN,	/* See incoming packets */
	BPF_D_INOUT,	/* See incoming and outgoing packets */
	BPF_D_OUT	/* See outgoing packets */
};

struct bpf_ts {
	int64_t		bt_sec;
	uint64_t	bt_frac;
};

struct bpf_xhdr {
	struct bpf_ts	bh_tstamp;
	uint32_t	bh_caplen;
	uint32_t	bh_datalen;
	u_short		bh_hdrlen;
};

typedef void		bif_attachd_t(void *);
typedef void		bif_detachd_t(void *);
typedef bool		bif_chkdir_t(void *, const struct mbuf *, int);
typedef int		bif_write_t(void *, struct mbuf *, struct mbuf *, int);
typedef uint32_t	bif_wrsize_t(void *);
typedef int		bif_promisc_t(void *, bool);
typedef int		bif_mac_check_receive_t(void *, struct bpf_d *);

struct bif_methods {
	bif_attachd_t	*bif_attachd;
	bif_detachd_t	*bif_detachd;
	bif_chkdir_t	*bif_chkdir;
	bif_promisc_t	*bif_promisc;
	bif_write_t	*bif_write;
	bif_wrsize_t	*bif_wrsize;
	bif_mac_check_receive_t *bif_mac_check_receive;
};

/* Ifnet methods shared with net80211 (src/misc.c). */
extern bif_wrsize_t bpf_ifnet_wrsize;
extern bif_promisc_t bpf_ifnet_promisc;

struct bpf_if *bpf_attach(const char *name, u_int dlt, u_int hdrlen,
	    const struct bif_methods *methods, void *arg);
void	bpf_detach(struct bpf_if *bp);
void	bpfattach(struct ifnet *ifp, u_int dlt, u_int hdrlen);
void	bpfattach2(struct ifnet *ifp, u_int dlt, u_int hdrlen, struct bpf_if **bpp);
void	bpfdetach(struct ifnet *ifp);
void	bpf_mtap(struct bpf_if *bp, struct mbuf *m);
void	bpf_mtap_if(struct ifnet *ifp, struct mbuf *m);
void	bpf_mtap2(struct bpf_if *bp, void *data, u_int dlen, struct mbuf *m);
void	bpf_mtap2_if(struct ifnet *ifp, void *data, u_int dlen, struct mbuf *m);
void	bpf_tap(struct bpf_if *bp, u_char *pkt, u_int pktlen);
void	bpf_tap_if(struct ifnet *ifp, u_char *pkt, u_int pktlen);
bool	bpf_peers_present_if(struct ifnet *ifp);
int	bpf_validate(const void *f, int len);
u_int	bpf_filter(const void *pc, u_char *p, u_int wirelen, u_int buflen);

static __inline bool
bpf_peers_present(const struct bpf_if *bpf)
{
	(void)bpf;
	return (false);
}

#define BPF_MTAP(_ifp, _m)	do { } while (0)
#define BPF_MTAP2(_ifp, _data, _dlen, _m) do { } while (0)
#define BPF_TAP(_ifp, _pkt, _pktlen) do { } while (0)

#endif /* _BSDKPI_NET_BPF_H_ */
