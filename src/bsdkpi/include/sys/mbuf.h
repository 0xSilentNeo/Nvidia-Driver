/* SPDX-License-Identifier: MIT */
/*
 * sys/mbuf.h - mbuf(9) with FreeBSD's field names and semantics, own
 * storage model.
 *
 * Every mbuf owns one data buffer that is allocated together with the
 * header: MCLBYTES (2 KiB) for m_get/m_gethdr/m_getcl, the requested jumbo
 * size for m_getjcl/m_get2.  There are no clusters, no reference-counted
 * external storage and no M_EXT; m_copym/m_copypacket/m_dup copy the data.
 * M_WRITABLE() is therefore true unless M_RDONLY was set by hand.  MLEN and
 * MHLEN are the real buffer size so that "fits in a plain mbuf" decisions
 * made by FreeBSD code stay valid.  Packet tags and mbufq are complete.
 * Implementation in src/mbuf.c.
 */
#ifndef _BSDKPI_SYS_MBUF_H_
#define _BSDKPI_SYS_MBUF_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/kassert.h>

struct ifnet;
struct m_snd_tag;

#define MSIZE		256
#define MCLSHIFT	11
#define MCLBYTES	(1 << MCLSHIFT)
#define MJUMPAGESIZE	PAGE_SIZE
#define MJUM9BYTES	(9 * 1024)
#define MJUM16BYTES	(16 * 1024)
#define MLEN		MCLBYTES	/* every mbuf carries a cluster-sized buffer */
#define MHLEN		MCLBYTES
#define MINCLSIZE	(MHLEN + 1)

/* Packet tags. */
struct m_tag {
	SLIST_ENTRY(m_tag)	m_tag_link;
	u_int16_t		m_tag_id;
	u_int16_t		m_tag_len;
	u_int32_t		m_tag_cookie;
	void			(*m_tag_free)(struct m_tag *);
};

/* Record/packet header in first mbuf of chain; valid only if M_PKTHDR. */
struct pkthdr {
	struct ifnet	*rcvif;		/* rcv interface (net80211: node) */
	SLIST_HEAD(packet_tags, m_tag) tags;
	int32_t		 len;		/* total packet length */
	uint32_t	 flowid;	/* packet's 4-tuple system */
	uint32_t	 csum_flags;	/* checksum and offload features */
	uint16_t	 fibnum;
	uint8_t		 numa_domain;
	uint8_t		 rsstype;	/* hash type */
	uint64_t	 rcv_tstmp;
	uint8_t		 l2hlen, l3hlen, l4hlen, l5hlen;
	uint32_t	 inner_l2hlen;
	/* FreeBSD keeps the following in unions; the common names are kept. */
	uint16_t	 ether_vtag;	/* Ethernet 802.1p+q vlan tag */
	uint16_t	 tso_segsz;	/* TSO segment size (net80211: seqno) */
	uint32_t	 csum_data;	/* checksum data (net80211: age) */
	struct m_snd_tag *snd_tag;
	uint32_t	 lro_nsegs;
	uint32_t	 lro_tcp_d_len;
	uint32_t	 lro_tcp_d_csum;
	uint32_t	 lro_tcp_h_off;
	uint32_t	 lro_etype;
	uint32_t	 lro_len;
	uint32_t	 lro_csum;
};

struct mbuf {
	struct mbuf	*m_next;	/* next buffer in chain */
	union {
		struct mbuf	*m_nextpkt;	/* next chain in queue/record */
		STAILQ_ENTRY(mbuf) m_stailqpkt;
	};
	caddr_t		 m_data;	/* location of data */
	int32_t		 m_len;		/* amount of data in this mbuf */
	uint32_t	 m_type:8,	/* type of data in this mbuf */
			 m_flags:24;	/* flags; see below */
	struct pkthdr	 m_pkthdr;	/* M_PKTHDR set */
	/* bsdkpi: the owned storage (FreeBSD: m_dat/m_pktdat/m_ext). */
	caddr_t		 m_bufstart;	/* start of the data buffer */
	u_int		 m_bufsize;	/* its size in bytes */
	u_int		 m_bsdkpi_magic;
};
#define BSDKPI_MBUF_MAGIC	0x6d627566u	/* "mbuf" */

/* mbuf flags; FreeBSD's values. */
#define M_EXT		0x00000001	/* never set here: no external storage */
#define M_PKTHDR	0x00000002	/* start of record */
#define M_EOR		0x00000004	/* end of record */
#define M_RDONLY	0x00000008	/* associated data is marked read-only */
#define M_BCAST		0x00000010	/* send/received as link-level broadcast */
#define M_MCAST		0x00000020	/* send/received as link-level multicast */
#define M_PROMISC	0x00000040	/* packet was not for us */
#define M_VLANTAG	0x00000080	/* ether_vtag is valid */
#define M_EXTPG		0x00000100	/* never set here */
#define M_NOFREE	0x00000200	/* do not free mbuf, embedded in cluster */
#define M_TSTMP		0x00000400	/* rcv_tstmp field is valid */
#define M_TSTMP_HPREC	0x00000800
#define M_TSTMP_LRO	0x00001000
#define M_PROTO1	0x00002000	/* protocol-specific */
#define M_PROTO2	0x00004000
#define M_PROTO3	0x00008000
#define M_PROTO4	0x00010000
#define M_PROTO5	0x00020000
#define M_PROTO6	0x00040000
#define M_PROTO7	0x00080000
#define M_PROTO8	0x00100000
#define M_PROTO9	0x00200000
#define M_PROTO10	0x00400000
#define M_PROTO11	0x00800000

#define M_PROTOFLAGS \
    (M_PROTO1 | M_PROTO2 | M_PROTO3 | M_PROTO4 | M_PROTO5 | M_PROTO6 | \
     M_PROTO7 | M_PROTO8 | M_PROTO9 | M_PROTO10 | M_PROTO11)
#define M_COPYFLAGS \
    (M_PKTHDR | M_EOR | M_RDONLY | M_BCAST | M_MCAST | M_PROMISC | \
     M_VLANTAG | M_TSTMP | M_TSTMP_HPREC | M_TSTMP_LRO | M_PROTOFLAGS)
#define M_FLAG_BITS \
    "\20\1M_EXT\2M_PKTHDR\3M_EOR\4M_RDONLY\5M_BCAST\6M_MCAST" \
    "\7M_PROMISC\10M_VLANTAG\11M_EXTPG\12M_NOFREE\13M_TSTMP\14M_TSTMP_HPREC\15M_TSTMP_LRO"
#define M_FLAG_PROTOBITS \
    "\16M_PROTO1\17M_PROTO2\20M_PROTO3\21M_PROTO4" \
    "\22M_PROTO5\23M_PROTO6\24M_PROTO7\25M_PROTO8\26M_PROTO9" \
    "\27M_PROTO10\28M_PROTO11"
#define M_FLAG_PRINTF	(M_FLAG_BITS M_FLAG_PROTOBITS)

/* Network interface cards are able to hash protocol fields. */
#define M_HASHTYPE_HASHPROP		0x80
#define M_HASHTYPE_INNER		0x40
#define M_HASHTYPE_HASH(t)		(M_HASHTYPE_HASHPROP | (t))
#define M_HASHTYPE_NONE			0
#define M_HASHTYPE_RSS_IPV4		M_HASHTYPE_HASH(1)
#define M_HASHTYPE_RSS_TCP_IPV4		M_HASHTYPE_HASH(2)
#define M_HASHTYPE_RSS_IPV6		M_HASHTYPE_HASH(3)
#define M_HASHTYPE_RSS_TCP_IPV6		M_HASHTYPE_HASH(4)
#define M_HASHTYPE_RSS_IPV6_EX		M_HASHTYPE_HASH(5)
#define M_HASHTYPE_RSS_TCP_IPV6_EX	M_HASHTYPE_HASH(6)
#define M_HASHTYPE_RSS_UDP_IPV4		M_HASHTYPE_HASH(7)
#define M_HASHTYPE_RSS_UDP_IPV6		M_HASHTYPE_HASH(9)
#define M_HASHTYPE_RSS_UDP_IPV6_EX	M_HASHTYPE_HASH(10)
#define M_HASHTYPE_OPAQUE		0x3f
#define M_HASHTYPE_OPAQUE_HASH		M_HASHTYPE_HASH(M_HASHTYPE_OPAQUE)
#define M_HASHTYPE_CLEAR(m)	((m)->m_pkthdr.rsstype = 0)
#define M_HASHTYPE_GET(m)	((m)->m_pkthdr.rsstype & ~M_HASHTYPE_INNER)
#define M_HASHTYPE_SET(m, v)	((m)->m_pkthdr.rsstype = (v))
#define M_HASHTYPE_TEST(m, v)	(M_HASHTYPE_GET(m) == (v))
#define M_HASHTYPE_ISHASH(m)	\
	(((m)->m_pkthdr.rsstype & M_HASHTYPE_HASHPROP) != 0)
#define M_HASHTYPE_SETINNER(m)	((m)->m_pkthdr.rsstype |= M_HASHTYPE_INNER)

/* csum_flags */
#define CSUM_IP			0x00000001
#define CSUM_IP_UDP		0x00000002
#define CSUM_IP_TCP		0x00000004
#define CSUM_IP_SCTP		0x00000008
#define CSUM_IP_TSO		0x00000010
#define CSUM_IP_ISCSI		0x00000020
#define CSUM_INNER_IP6_UDP	0x00000040
#define CSUM_INNER_IP6_TCP	0x00000080
#define CSUM_INNER_IP6_TSO	0x00000100
#define CSUM_INNER_IP		0x00000200
#define CSUM_INNER_IP_UDP	0x00000400
#define CSUM_INNER_IP_TCP	0x00000800
#define CSUM_INNER_IP_TSO	0x00001000
#define CSUM_ENCAP_VXLAN	0x00002000
#define CSUM_ENCAP_RSVD1	0x00004000
#define CSUM_IP6_UDP		0x00010000
#define CSUM_IP6_TCP		0x00020000
#define CSUM_IP6_SCTP		0x00040000
#define CSUM_IP6_TSO		0x00080000
#define CSUM_IP6_ISCSI		0x00100000
#define CSUM_INNER_L3_CALC	0x10000000
#define CSUM_INNER_L3_VALID	0x20000000
#define CSUM_INNER_L4_CALC	0x40000000
#define CSUM_INNER_L4_VALID	0x80000000
#define CSUM_L3_CALC		0x01000000
#define CSUM_L3_VALID		0x02000000
#define CSUM_L4_CALC		0x04000000
#define CSUM_L4_VALID		0x08000000
#define CSUM_L5_CALC		0x00200000
#define CSUM_L5_VALID		0x00400000
#define CSUM_COALESCED		0x00800000
#define CSUM_SND_TAG		0x80000000
#define CSUM_DELAY_DATA		(CSUM_TCP | CSUM_UDP)
#define CSUM_DELAY_IP		CSUM_IP
#define CSUM_DATA_VALID		CSUM_L4_VALID
#define CSUM_PSEUDO_HDR		CSUM_L4_CALC
#define CSUM_IP_CHECKED		CSUM_L3_CALC
#define CSUM_IP_VALID		CSUM_L3_VALID
#define CSUM_TCP		CSUM_IP_TCP
#define CSUM_UDP		CSUM_IP_UDP
#define CSUM_SCTP		CSUM_IP_SCTP
#define CSUM_TSO		(CSUM_IP_TSO | CSUM_IP6_TSO)
#define CSUM_UDP_IPV6		CSUM_IP6_UDP
#define CSUM_TCP_IPV6		CSUM_IP6_TCP
#define CSUM_SCTP_IPV6		CSUM_IP6_SCTP

/* mbuf types */
#define MT_NOTMBUF	0
#define MT_DATA		1
#define MT_HEADER	MT_DATA
#define MT_VENDOR1	4
#define MT_VENDOR2	5
#define MT_VENDOR3	6
#define MT_VENDOR4	7
#define MT_SONAME	8
#define MT_EXP1		9
#define MT_EXP2		10
#define MT_EXP3		11
#define MT_EXP4		12
#define MT_CONTROL	14
#define MT_EXTCONTROL	15
#define MT_OOBDATA	16
#define MT_NTYPES	16
#define MT_NOINIT	255

/* Compatibility with the old m_get* API: allocation never waits here but
 * M_WAITOK still maps to a sleeping kmalloc. */
#define MBTOM(how)	(how)
#define M_DONTWAIT	M_NOWAIT
#define M_TRYWAIT	M_WAITOK
#define M_WAIT		M_WAITOK

#define MBUF_CHECKSLEEP(how)	do { } while (0)

/* Allocation. */
struct mbuf	*m_get(int how, short type);
struct mbuf	*m_gethdr(int how, short type);
struct mbuf	*m_getcl(int how, short type, int flags);
struct mbuf	*m_getjcl(int how, short type, int flags, int size);
struct mbuf	*m_get2(int size, int how, short type, int flags);
struct mbuf	*m_get3(int size, int how, short type, int flags);
struct mbuf	*m_getm2(struct mbuf *m, int len, int how, short type, int flags);
#define m_getm(m, len, how, type) m_getm2((m), (len), (how), (type), M_PKTHDR)
struct mbuf	*m_free(struct mbuf *m);
void		 m_freem(struct mbuf *m);
void		 m_freemp(struct mbuf *m);
int		 m_clget(struct mbuf *m, int how);
void		*m_cljget(struct mbuf *m, int how, int size);
#define m_get_raw(how, type)	m_get((how), (type))
#define m_gethdr_raw(how, type)	m_gethdr((how), (type))
#define m_free_raw(m)		((void)m_free(m))

/* Manipulation. */
void		 m_adj(struct mbuf *mp, int req_len);
void		 m_adj_decap(struct mbuf *mp, int len);
void		 m_align(struct mbuf *m, int len);
int		 m_append(struct mbuf *m0, int len, const void *cp);
int		 m_apply(struct mbuf *m, int off, int len,
		    int (*f)(void *, void *, u_int), void *arg);
void		 m_cat(struct mbuf *m, struct mbuf *n);
void		 m_catpkt(struct mbuf *m, struct mbuf *n);
struct mbuf	*m_collapse(struct mbuf *m, int how, int maxfrags);
void		 m_copyback(struct mbuf *m0, int off, int len, const void *cp);
void		 m_copydata(const struct mbuf *m, int off, int len, void *cp);
struct mbuf	*m_copym(struct mbuf *m, int off0, int len, int wait);
struct mbuf	*m_copypacket(struct mbuf *m, int how);
void		 m_copy_pkthdr(struct mbuf *to, struct mbuf *from);
struct mbuf	*m_copyup(struct mbuf *n, int len, int dstoff);
struct mbuf	*m_defrag(struct mbuf *m0, int how);
void		 m_demote_pkthdr(struct mbuf *m);
void		 m_demote(struct mbuf *m0, int all, int flags);
struct mbuf	*m_devget(char *buf, int totlen, int off, struct ifnet *ifp,
		    void (*copy)(char *from, caddr_t to, u_int len));
struct mbuf	*m_dup(const struct mbuf *m, int how);
int		 m_dup_pkthdr(struct mbuf *to, const struct mbuf *from, int how);
u_int		 m_fixhdr(struct mbuf *m0);
struct mbuf	*m_fragment(struct mbuf *m0, int how, int length);
struct mbuf	*m_getptr(struct mbuf *m, int loc, int *off);
u_int		 m_length(struct mbuf *m0, struct mbuf **last);
int		 m_mbuftouio(void *uio, const struct mbuf *m, int len);
void		 m_move_pkthdr(struct mbuf *to, struct mbuf *from);
int		 m_pkthdr_init(struct mbuf *m, int how);
struct mbuf	*m_prepend(struct mbuf *m, int len, int how);
void		 m_print(const struct mbuf *m, int maxlen);
struct mbuf	*m_pulldown(struct mbuf *m, int off, int len, int *offp);
struct mbuf	*m_pullup(struct mbuf *m, int len);
int		 m_sanity(struct mbuf *m, int sanitize);
struct mbuf	*m_split(struct mbuf *m0, int len0, int wait);
struct mbuf	*m_uiotombuf(void *uio, int how, int len, int align, int flags);
struct mbuf	*m_unshare(struct mbuf *m, int how);
void		 m_clrprotoflags(struct mbuf *m);
int		 m_init(struct mbuf *m, int how, short type, int flags);
void		 m_chtype(struct mbuf *m, short new_type);
struct mbuf	*m_last(struct mbuf *m);
void		 m_rcvif_serialize(struct mbuf *m);
struct ifnet	*m_rcvif_restore(struct mbuf *m);

/* Statistics the FreeBSD stack publishes; kept so sysctls compile. */
struct mbstat {
	u_long	m_mbufs;
	u_long	m_mclusts;
	u_long	m_drain;
	u_long	m_mcfail;
	u_long	m_mpfail;
	u_long	m_msize;
	u_long	m_mclbytes;
	u_long	m_minclsize;
	u_long	m_mlen;
	u_long	m_mhlen;
	short	m_numtypes;
	u_long	sf_iocnt;
	u_long	sf_allocfail;
	u_long	sf_allocwait;
};
extern struct mbstat mbstat;

/* Protocol header reservation (FreeBSD: tuned by protocols at attach). */
extern int max_linkhdr;
extern int max_protohdr;
extern int max_hdr;
extern int max_datalen;
void	max_linkhdr_grow(u_int new_size);
void	max_protohdr_grow(u_int new_size);

/* Accessors / macros. */
#define mtod(m, t)	((t)((m)->m_data))
#define mtodo(m, o)	((void *)(((m)->m_data) + (o)))

#define M_START(m)	((m)->m_bufstart)
#define M_SIZE(m)	((m)->m_bufsize)

static __inline struct ifnet *
m_rcvif(const struct mbuf *m)
{
	return (m->m_pkthdr.rcvif);
}

#define M_WRITABLE(m)	(((m)->m_flags & M_RDONLY) == 0)
#define M_LEADINGSPACE(m) \
	(M_WRITABLE(m) ? ((m)->m_data - M_START(m)) : 0)
#define M_TRAILINGSPACE(m) \
	(M_WRITABLE(m) ? (M_START(m) + M_SIZE(m) - ((m)->m_data + (m)->m_len)) : 0)

/* Leave room at the front for the given length (m_data is moved). */
#define M_ALIGN(m, len)		m_align((m), (len))
#define MH_ALIGN(m, len)	m_align((m), (len))
#define MEXT_ALIGN(m, len)	m_align((m), (len))

#define M_PREPEND(m, plen, how) do {					\
	struct mbuf **_mmp = &(m);					\
	struct mbuf *_mm = *_mmp;					\
	int _mplen = (plen);						\
	int __mhow = (how);						\
									\
	MBUF_CHECKSLEEP(how);						\
	if (M_LEADINGSPACE(_mm) >= _mplen) {				\
		_mm->m_data -= _mplen;					\
		_mm->m_len += _mplen;					\
	} else								\
		_mm = m_prepend(_mm, _mplen, __mhow);			\
	if (_mm != NULL && _mm->m_flags & M_PKTHDR)			\
		_mm->m_pkthdr.len += _mplen;				\
	*_mmp = _mm;							\
} while (0)

#define M_MOVE_PKTHDR(to, from)	m_move_pkthdr((to), (from))
#define M_COPY_PKTHDR(to, from)	m_copy_pkthdr((to), (from))

#define MGET(m, how, type)	((m) = m_get((how), (type)))
#define MGETHDR(m, how, type)	((m) = m_gethdr((how), (type)))
#define MCLGET(m, how)		m_clget((m), (how))
#define MCHTYPE(m, t)		m_chtype((m), (t))

#define M_ASSERTPKTHDR(m)						\
	KASSERT((m) != NULL && (m)->m_flags & M_PKTHDR,			\
	    ("%s: no mbuf packet header!", __func__))
#define M_ASSERTVALID(m)						\
	KASSERT((m) != NULL && (m)->m_bsdkpi_magic == BSDKPI_MBUF_MAGIC,	\
	    ("%s: attempted use of a free mbuf!", __func__))
#define M_ASSERTEXTPG(m)						\
	KASSERT(0, ("%s: no unmapped mbufs here", __func__))
#define M_ASSERT_NO_SND_TAG(m) do { } while (0)
#define M_PROFILE(m)		do { } while (0)
#define M_SANITY_ACTION(m, a)	m_sanity((m), (a))

#define m_tag_id(tag)		((tag)->m_tag_id)
#define m_tag_len(tag)		((tag)->m_tag_len)
#define m_tag_cookie(tag)	((tag)->m_tag_cookie)
#define m_tag_data(tag)		((void *)((tag) + 1))

/* Packet tags. */
#define MTAG_ABI_COMPAT		0
#define MTAG_PERSISTENT		0x800

struct m_tag	*m_tag_alloc(uint32_t cookie, uint16_t type, int len, int wait);
void		 m_tag_delete(struct mbuf *m, struct m_tag *t);
void		 m_tag_delete_chain(struct mbuf *m, struct m_tag *t);
void		 m_tag_free_default(struct m_tag *t);
struct m_tag	*m_tag_locate(struct mbuf *m, uint32_t cookie, uint16_t type,
		    struct m_tag *t);
struct m_tag	*m_tag_copy(struct m_tag *t, int how);
int		 m_tag_copy_chain(struct mbuf *to, const struct mbuf *from,
		    int how);
void		 m_tag_delete_nonpersistent(struct mbuf *m);

static __inline void
m_tag_init(struct mbuf *m)
{
	SLIST_INIT(&m->m_pkthdr.tags);
}

static __inline void
m_tag_setup(struct m_tag *t, uint32_t cookie, uint16_t type, int len)
{
	t->m_tag_id = type;
	t->m_tag_len = (uint16_t)len;
	t->m_tag_cookie = cookie;
}

static __inline void
m_tag_free(struct m_tag *t)
{
	(*t->m_tag_free)(t);
}

static __inline struct m_tag *
m_tag_first(struct mbuf *m)
{
	return (SLIST_FIRST(&m->m_pkthdr.tags));
}

static __inline struct m_tag *
m_tag_next(struct mbuf *m __unused, struct m_tag *t)
{
	return (SLIST_NEXT(t, m_tag_link));
}

static __inline void
m_tag_prepend(struct mbuf *m, struct m_tag *t)
{
	SLIST_INSERT_HEAD(&m->m_pkthdr.tags, t, m_tag_link);
}

static __inline void
m_tag_unlink(struct mbuf *m, struct m_tag *t)
{
	SLIST_REMOVE(&m->m_pkthdr.tags, t, m_tag, m_tag_link);
}

static __inline struct m_tag *
m_tag_get(uint16_t type, int length, int wait)
{
	return (m_tag_alloc(MTAG_ABI_COMPAT, type, length, wait));
}

static __inline struct m_tag *
m_tag_find(struct mbuf *m, uint16_t type, struct m_tag *start)
{
	return (SLIST_EMPTY(&m->m_pkthdr.tags) ? (struct m_tag *)NULL :
	    m_tag_locate(m, MTAG_ABI_COMPAT, type, start));
}

/* Packet queues (mbufq) -- FreeBSD's inline implementation shape. */
struct mbufq {
	STAILQ_HEAD(, mbuf)	mq_head;
	int			mq_len;
	int			mq_maxlen;
};

static __inline void
mbufq_init(struct mbufq *mq, int maxlen)
{
	STAILQ_INIT(&mq->mq_head);
	mq->mq_maxlen = maxlen;
	mq->mq_len = 0;
}

static __inline struct mbuf *
mbufq_flush(struct mbufq *mq)
{
	struct mbuf *m;

	m = STAILQ_FIRST(&mq->mq_head);
	STAILQ_INIT(&mq->mq_head);
	mq->mq_len = 0;
	return (m);
}

static __inline void
mbufq_drain(struct mbufq *mq)
{
	struct mbuf *m, *n;

	n = mbufq_flush(mq);
	while ((m = n) != NULL) {
		n = STAILQ_NEXT(m, m_stailqpkt);
		m_freem(m);
	}
}

static __inline struct mbuf *
mbufq_first(const struct mbufq *mq)
{
	return (STAILQ_FIRST(&mq->mq_head));
}

static __inline struct mbuf *
mbufq_last(const struct mbufq *mq)
{
	return (STAILQ_LAST(&mq->mq_head, mbuf, m_stailqpkt));
}

static __inline bool
mbufq_empty(const struct mbufq *mq)
{
	return (mq->mq_len == 0);
}

static __inline int
mbufq_full(const struct mbufq *mq)
{
	return (mq->mq_maxlen > 0 && mq->mq_len >= mq->mq_maxlen);
}

static __inline int
mbufq_len(const struct mbufq *mq)
{
	return (mq->mq_len);
}

static __inline int
mbufq_enqueue(struct mbufq *mq, struct mbuf *m)
{
	if (mbufq_full(mq))
		return (ENOBUFS);
	STAILQ_INSERT_TAIL(&mq->mq_head, m, m_stailqpkt);
	mq->mq_len++;
	return (0);
}

static __inline void
mbufq_remove(struct mbufq *mq, struct mbuf *m)
{
	STAILQ_REMOVE(&mq->mq_head, m, mbuf, m_stailqpkt);
	mq->mq_len--;
}

static __inline struct mbuf *
mbufq_dequeue(struct mbufq *mq)
{
	struct mbuf *m;

	m = STAILQ_FIRST(&mq->mq_head);
	if (m) {
		STAILQ_REMOVE_HEAD(&mq->mq_head, m_stailqpkt);
		m->m_nextpkt = NULL;
		mq->mq_len--;
	}
	return (m);
}

static __inline void
mbufq_prepend(struct mbufq *mq, struct mbuf *m)
{
	STAILQ_INSERT_HEAD(&mq->mq_head, m, m_stailqpkt);
	mq->mq_len++;
}

/* Note: this doesn't enforce the maximum list size for dst. */
static __inline void
mbufq_concat(struct mbufq *mq_dst, struct mbufq *mq_src)
{
	mq_dst->mq_len += mq_src->mq_len;
	STAILQ_CONCAT(&mq_dst->mq_head, &mq_src->mq_head);
	mq_src->mq_len = 0;
}

/* Send tags do not exist; only the type and the reference helpers. */
struct m_snd_tag {
	struct ifnet	*ifp;
	volatile u_int	 refcount;
	int		 type;
};
#define IF_SND_TAG_TYPE_RATE_LIMIT	0
#define IF_SND_TAG_TYPE_UNLIMITED	1
#define IF_SND_TAG_TYPE_TLS		2
#define IF_SND_TAG_TYPE_TLS_RATE_LIMIT	3
#define IF_SND_TAG_TYPE_TLS_RX		4
#define IF_SND_TAG_TYPE_MAX		5
static __inline void m_snd_tag_ref(struct m_snd_tag *mst) { (void)__atomic_fetch_add(&mst->refcount, 1, __ATOMIC_SEQ_CST); }
static __inline void m_snd_tag_rele(struct m_snd_tag *mst) { (void)__atomic_fetch_sub(&mst->refcount, 1, __ATOMIC_SEQ_CST); }

/* Debugging aid: dump an mbuf chain with printf. */
void	bsdkpi_mbuf_dump(const struct mbuf *m, const char *what);

#endif /* _BSDKPI_SYS_MBUF_H_ */
