// SPDX-License-Identifier: MIT
/*
 * mbuf(9): FreeBSD's chain and packet-header semantics on a simple storage
 * model (see sys/mbuf.h).  Every mbuf is one kmalloc()ed block: the header
 * followed by its data buffer.  No clusters, no shared storage, so copies
 * are real copies and writability never needs checking.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <bsdkpi/bsdkpi.h>
#include <linux/slab.h>

struct mbstat mbstat = {
	.m_msize = MSIZE, .m_mclbytes = MCLBYTES, .m_minclsize = MINCLSIZE,
	.m_mlen = MLEN, .m_mhlen = MHLEN, .m_numtypes = MT_NTYPES,
};
int max_linkhdr = 16;
int max_protohdr = 40;
int max_hdr = 56;
int max_datalen = MHLEN - 56;

void
max_linkhdr_grow(u_int new_size)
{
	if ((int)new_size > max_linkhdr) {
		max_linkhdr = (int)new_size;
		max_hdr = max_linkhdr + max_protohdr;
		max_datalen = MHLEN - max_hdr;
	}
}

void
max_protohdr_grow(u_int new_size)
{
	if ((int)new_size > max_protohdr) {
		max_protohdr = (int)new_size;
		max_hdr = max_linkhdr + max_protohdr;
		max_datalen = MHLEN - max_hdr;
	}
}

/* ---- allocation ----------------------------------------------------- */

int
m_pkthdr_init(struct mbuf *m, int how)
{
	(void)how;
	memset(&m->m_pkthdr, 0, sizeof(m->m_pkthdr));
	SLIST_INIT(&m->m_pkthdr.tags);
	return (0);
}

static struct mbuf *
mbuf_alloc(int how, short type, int flags, u_int size)
{
	struct mbuf *m;

	if (size < MCLBYTES)
		size = MCLBYTES;
	m = kmalloc(sizeof(*m) + size, bsdkpi_malloc_gfp(how));
	if (m == NULL)
		return (NULL);
	m->m_next = NULL;
	m->m_nextpkt = NULL;
	m->m_bufstart = (caddr_t)(m + 1);
	m->m_bufsize = size;
	m->m_data = m->m_bufstart;
	m->m_len = 0;
	m->m_type = (uint32_t)type;
	m->m_flags = (uint32_t)(flags & (M_PKTHDR | M_EOR));
	m->m_bsdkpi_magic = BSDKPI_MBUF_MAGIC;
	if (flags & M_PKTHDR)
		m_pkthdr_init(m, how);
	else
		memset(&m->m_pkthdr, 0, sizeof(m->m_pkthdr));
	return (m);
}

int
m_init(struct mbuf *m, int how, short type, int flags)
{
	m->m_next = NULL;
	m->m_nextpkt = NULL;
	m->m_data = m->m_bufstart;
	m->m_len = 0;
	m->m_type = (uint32_t)type;
	m->m_flags = (uint32_t)flags & (M_PKTHDR | M_EOR);
	if (flags & M_PKTHDR)
		return (m_pkthdr_init(m, how));
	return (0);
}

struct mbuf *
m_get(int how, short type)
{
	return (mbuf_alloc(how, type, 0, MCLBYTES));
}

struct mbuf *
m_gethdr(int how, short type)
{
	return (mbuf_alloc(how, type, M_PKTHDR, MCLBYTES));
}

struct mbuf *
m_getcl(int how, short type, int flags)
{
	return (mbuf_alloc(how, type, flags, MCLBYTES));
}

struct mbuf *
m_getjcl(int how, short type, int flags, int size)
{
	return (mbuf_alloc(how, type, flags, (u_int)size));
}

/* Allocate an mbuf whose buffer holds at least @size bytes. */
struct mbuf *
m_get2(int size, int how, short type, int flags)
{
	u_int sz;

	if (size <= MCLBYTES)
		sz = MCLBYTES;
	else if (size <= MJUMPAGESIZE)
		sz = MJUMPAGESIZE;
	else if (size <= MJUM9BYTES)
		sz = MJUM9BYTES;
	else if (size <= MJUM16BYTES)
		sz = MJUM16BYTES;
	else
		return (NULL);
	return (mbuf_alloc(how, type, flags, sz));
}

struct mbuf *
m_get3(int size, int how, short type, int flags)
{
	return (m_get2(size, how, type, flags));
}

/* Append enough mbufs to @m (or start a chain) to hold @len more bytes. */
struct mbuf *
m_getm2(struct mbuf *m, int len, int how, short type, int flags)
{
	struct mbuf *mb, *nm = NULL, *mtail = NULL;

	KASSERT(len >= 0, ("%s: len is < 0", __func__));
	if (m != NULL)
		for (mtail = m; mtail->m_next != NULL; mtail = mtail->m_next)
			continue;
	else
		flags &= ~M_PKTHDR;	/* only the first mbuf of a chain */

	while (len > 0) {
		mb = m_get2(len > MCLBYTES ? MJUM16BYTES : MCLBYTES, how, type,
		    (flags & M_PKTHDR) && nm == NULL && m == NULL ? M_PKTHDR : 0);
		if (mb == NULL) {
			m_freem(nm);
			return (NULL);
		}
		len -= (int)M_SIZE(mb);
		if (mtail != NULL)
			mtail->m_next = mb;
		else
			nm = mb;
		mtail = mb;
	}
	if (nm == NULL)
		return (m);
	if (m != NULL)
		return (m);
	return (nm);
}

int
m_clget(struct mbuf *m, int how)
{
	(void)how;
	/* every mbuf already carries a cluster-sized buffer */
	m->m_data = m->m_bufstart;
	return (1);
}

void *
m_cljget(struct mbuf *m, int how, int size)
{
	(void)how;
	if (m == NULL || (u_int)size > m->m_bufsize)
		return (NULL);
	m->m_data = m->m_bufstart;
	return (m->m_bufstart);
}

struct mbuf *
m_free(struct mbuf *m)
{
	struct mbuf *n = m->m_next;

	M_ASSERTVALID(m);
	if ((m->m_flags & M_PKTHDR) && !SLIST_EMPTY(&m->m_pkthdr.tags))
		m_tag_delete_chain(m, NULL);
	m->m_bsdkpi_magic = 0;
	kfree(m);
	return (n);
}

void
m_freem(struct mbuf *mb)
{
	while (mb != NULL)
		mb = m_free(mb);
}

void
m_freemp(struct mbuf *m)
{
	struct mbuf *n;

	while (m != NULL) {
		n = m->m_nextpkt;
		m_freem(m);
		m = n;
	}
}

/* ---- header handling ------------------------------------------------ */

void
m_move_pkthdr(struct mbuf *to, struct mbuf *from)
{
	M_ASSERTPKTHDR(from);
	KASSERT((to->m_flags & M_PKTHDR) == 0 || SLIST_EMPTY(&to->m_pkthdr.tags),
	    ("m_move_pkthdr: to has tags"));
	to->m_flags = (from->m_flags & M_COPYFLAGS) |
	    (to->m_flags & (M_EXT | M_EXTPG));
	to->m_pkthdr = from->m_pkthdr;		/* especially tags */
	SLIST_INIT(&from->m_pkthdr.tags);	/* purge tags from src */
	from->m_flags &= ~M_PKTHDR;
	if (from->m_pkthdr.csum_flags & CSUM_SND_TAG)
		from->m_pkthdr.snd_tag = NULL;
}

int
m_dup_pkthdr(struct mbuf *to, const struct mbuf *from, int how)
{
	M_ASSERTPKTHDR(from);
	to->m_flags = (from->m_flags & M_COPYFLAGS) |
	    (to->m_flags & (M_EXT | M_EXTPG));
	to->m_pkthdr = from->m_pkthdr;
	SLIST_INIT(&to->m_pkthdr.tags);
	return (m_tag_copy_chain(to, from, how));
}

void
m_copy_pkthdr(struct mbuf *to, struct mbuf *from)
{
	(void)m_dup_pkthdr(to, from, M_NOWAIT);
}

void
m_demote_pkthdr(struct mbuf *m)
{
	M_ASSERTPKTHDR(m);
	m_tag_delete_chain(m, NULL);
	m->m_flags &= ~M_PKTHDR;
	memset(&m->m_pkthdr, 0, sizeof(m->m_pkthdr));
}

void
m_demote(struct mbuf *m0, int all, int flags)
{
	struct mbuf *m;

	for (m = all ? m0 : m0->m_next; m != NULL; m = m->m_next) {
		if (m->m_flags & M_PKTHDR)
			m_demote_pkthdr(m);
		m->m_flags &= (M_EXT | M_RDONLY | M_NOFREE | M_EXTPG | flags);
	}
}

void
m_chtype(struct mbuf *m, short new_type)
{
	m->m_type = (uint32_t)new_type;
}

void
m_clrprotoflags(struct mbuf *m)
{
	while (m != NULL) {
		m->m_flags &= ~M_PROTOFLAGS;
		m = m->m_next;
	}
}

struct mbuf *
m_last(struct mbuf *m)
{
	while (m->m_next != NULL)
		m = m->m_next;
	return (m);
}

void
m_rcvif_serialize(struct mbuf *m)
{
	(void)m;
}

struct ifnet *
m_rcvif_restore(struct mbuf *m)
{
	return (m->m_pkthdr.rcvif);
}

u_int
m_length(struct mbuf *m0, struct mbuf **last)
{
	struct mbuf *m;
	u_int len = 0;

	for (m = m0; m != NULL; m = m->m_next) {
		len += (u_int)m->m_len;
		if (m->m_next == NULL)
			break;
	}
	if (last != NULL)
		*last = m;
	return (len);
}

u_int
m_fixhdr(struct mbuf *m0)
{
	u_int len;

	len = m_length(m0, NULL);
	m0->m_pkthdr.len = (int32_t)len;
	return (len);
}

/* ---- data manipulation ---------------------------------------------- */

/*
 * Set the m_data pointer of a newly allocated mbuf so that @len bytes end
 * at the end of the buffer, aligned to sizeof(long).
 */
void
m_align(struct mbuf *m, int len)
{
	int adjust;

	KASSERT(m->m_data == M_START(m), ("m_align: not a virgin mbuf"));
	adjust = (int)M_SIZE(m) - len;
	m->m_data += adjust & ~(int)(sizeof(long) - 1);
}

void
m_adj(struct mbuf *mp, int req_len)
{
	int len = req_len;
	struct mbuf *m;
	int count;

	if ((m = mp) == NULL)
		return;
	if (len >= 0) {
		/* Trim from head. */
		while (m != NULL && len > 0) {
			if (m->m_len <= len) {
				len -= m->m_len;
				m->m_len = 0;
				m = m->m_next;
			} else {
				m->m_len -= len;
				m->m_data += len;
				len = 0;
			}
		}
		if (mp->m_flags & M_PKTHDR)
			mp->m_pkthdr.len -= (req_len - len);
	} else {
		/*
		 * Trim from tail.  Scan the mbuf chain, calculating its
		 * length and finding the last mbuf.  If the adjustment only
		 * affects this mbuf, then just adjust and return.
		 */
		len = -len;
		count = 0;
		for (;;) {
			count += m->m_len;
			if (m->m_next == NULL)
				break;
			m = m->m_next;
		}
		if (m->m_len >= len) {
			m->m_len -= len;
			if (mp->m_flags & M_PKTHDR)
				mp->m_pkthdr.len -= len;
			return;
		}
		count -= len;
		if (count < 0)
			count = 0;
		/*
		 * Correct length for chain is "count".  Find the mbuf with
		 * last data, adjust its length, and toss data from remaining
		 * mbufs on chain.
		 */
		m = mp;
		if (m->m_flags & M_PKTHDR)
			m->m_pkthdr.len = count;
		for (; m != NULL; m = m->m_next) {
			if (m->m_len >= count) {
				m->m_len = count;
				if (m->m_next != NULL) {
					m_freem(m->m_next);
					m->m_next = NULL;
				}
				break;
			}
			count -= m->m_len;
		}
	}
}

void
m_adj_decap(struct mbuf *mp, int len)
{
	uint8_t rsstype;

	m_adj(mp, len);
	if ((mp->m_flags & M_PKTHDR) != 0) {
		rsstype = mp->m_pkthdr.rsstype;
		if ((rsstype & M_HASHTYPE_INNER) != 0) {
			M_HASHTYPE_SET(mp, rsstype & ~M_HASHTYPE_INNER);
		} else {
			M_HASHTYPE_CLEAR(mp);
		}
	}
}

/*
 * Rearrange an mbuf chain so that @len bytes are contiguous and in the
 * data area of the first mbuf; the chain is freed on failure.
 */
struct mbuf *
m_pullup(struct mbuf *n, int len)
{
	struct mbuf *m;
	int count;
	int space;

	if (n == NULL)
		return (NULL);
	if (n->m_len >= len)
		return (n);
	if (len > (int)MHLEN)
		goto bad;
	/*
	 * If the first mbuf has room for @len bytes (counting what is
	 * already there), pull into it; otherwise start a fresh one.
	 */
	if ((int)(M_START(n) + M_SIZE(n) - n->m_data) >= len && n->m_next) {
		m = n;
		n = n->m_next;
		len -= m->m_len;
	} else {
		m = m_get(M_NOWAIT, n->m_type);
		if (m == NULL)
			goto bad;
		if (n->m_flags & M_PKTHDR)
			m_move_pkthdr(m, n);
	}
	space = (int)(M_START(m) + M_SIZE(m) - (m->m_data + m->m_len));
	do {
		count = min(min(max(len, max_protohdr), space), n->m_len);
		memcpy(mtod(m, caddr_t) + m->m_len, mtod(n, caddr_t), (size_t)count);
		len -= count;
		m->m_len += count;
		n->m_len -= count;
		space -= count;
		if (n->m_len)
			n->m_data += count;
		else
			n = m_free(n);
	} while (len > 0 && n);
	if (len > 0) {
		(void)m_free(m);
		goto bad;
	}
	m->m_next = n;
	return (m);
bad:
	m_freem(n);
	return (NULL);
}

/*
 * Like m_pullup, but a leading gap of @dstoff bytes stays available in
 * front of the pulled-up data.
 */
struct mbuf *
m_copyup(struct mbuf *n, int len, int dstoff)
{
	struct mbuf *m;
	int count, space;

	if (len > ((int)MHLEN - dstoff))
		goto bad;
	m = m_get(M_NOWAIT, n->m_type);
	if (m == NULL)
		goto bad;
	if (n->m_flags & M_PKTHDR)
		m_move_pkthdr(m, n);
	m->m_data += dstoff;
	space = (int)(M_START(m) + M_SIZE(m) - (m->m_data + m->m_len));
	do {
		count = min(min(max(len, max_protohdr), space), n->m_len);
		memcpy(mtod(m, caddr_t) + m->m_len, mtod(n, caddr_t), (size_t)count);
		len -= count;
		m->m_len += count;
		n->m_len -= count;
		space -= count;
		if (n->m_len)
			n->m_data += count;
		else
			n = m_free(n);
	} while (len > 0 && n);
	if (len > 0) {
		(void)m_free(m);
		goto bad;
	}
	m->m_next = n;
	return (m);
bad:
	m_freem(n);
	return (NULL);
}

/*
 * Ensure that [off, off + len) is contiguous; returns the mbuf holding it
 * and the offset within it in *offp.  Frees the chain on failure.
 */
struct mbuf *
m_pulldown(struct mbuf *m, int off, int len, int *offp)
{
	struct mbuf *n, *o;
	int hlen, tlen, olen;
	int sharedcluster;

	if (m == NULL)
		return (NULL);
	if (len > MCLBYTES) {
		m_freem(m);
		return (NULL);
	}
	n = m;
	while (n != NULL && off > 0) {
		if (n->m_len > off)
			break;
		off -= n->m_len;
		n = n->m_next;
	}
	if (n == NULL) {
		m_freem(m);
		return (NULL);
	}
	if (n->m_len >= off + len) {
		if (offp)
			*offp = off;
		return (n);
	}
	/* the first @hlen bytes are in n, @tlen are in the following mbufs */
	hlen = n->m_len - off;
	tlen = len - hlen;
	olen = 0;
	for (o = n->m_next; o != NULL; o = o->m_next) {
		olen += o->m_len;
		if (olen >= tlen)
			break;
	}
	if (o == NULL) {
		m_freem(m);
		return (NULL);
	}
	sharedcluster = 0;	/* storage is never shared here */
	(void)sharedcluster;
	if (M_TRAILINGSPACE(n) >= tlen) {
		m_copydata(n->m_next, 0, tlen, mtod(n, caddr_t) + n->m_len);
		n->m_len += tlen;
		m_adj(n->m_next, tlen);
		goto ok;
	}
	if (off == 0 && M_LEADINGSPACE(n) >= hlen && n->m_next != NULL &&
	    M_LEADINGSPACE(n->m_next) >= hlen) {
		n->m_next->m_data -= hlen;
		n->m_next->m_len += hlen;
		memcpy(mtod(n->m_next, caddr_t), mtod(n, caddr_t), (size_t)hlen);
		n->m_len -= hlen;
		n = n->m_next;
		off = 0;
		goto ok;
	}
	/* allocate a new mbuf and copy the region into it */
	o = m_get(M_NOWAIT, m->m_type);
	if (o == NULL) {
		m_freem(m);
		return (NULL);
	}
	m_copydata(n, off, len, mtod(o, caddr_t));
	o->m_len = len;
	m_adj(n->m_next, tlen);
	n->m_len = off;
	o->m_next = n->m_next;
	n->m_next = o;
	n = o;
	off = 0;
ok:
	if (offp)
		*offp = off;
	return (n);
}

/* Prepend @len bytes, allocating a new mbuf when there is no room. */
struct mbuf *
m_prepend(struct mbuf *m, int len, int how)
{
	struct mbuf *mn;

	if (m->m_flags & M_PKTHDR)
		mn = m_gethdr(how, m->m_type);
	else
		mn = m_get(how, m->m_type);
	if (mn == NULL) {
		m_freem(m);
		return (NULL);
	}
	if (m->m_flags & M_PKTHDR)
		m_move_pkthdr(mn, m);
	mn->m_next = m;
	m = mn;
	if (len < (int)M_SIZE(m))
		m_align(m, len);
	m->m_len = len;
	return (m);
}

/* Copy data from an mbuf chain starting @off bytes in, for @len bytes. */
void
m_copydata(const struct mbuf *m, int off, int len, void *vp)
{
	caddr_t cp = vp;
	u_int count;

	KASSERT(off >= 0, ("m_copydata, negative off %d", off));
	KASSERT(len >= 0, ("m_copydata, negative len %d", len));
	while (off > 0) {
		KASSERT(m != NULL, ("m_copydata, offset > size of mbuf chain"));
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = m->m_next;
	}
	while (len > 0) {
		KASSERT(m != NULL, ("m_copydata, length > size of mbuf chain"));
		count = (u_int)min(m->m_len - off, len);
		memcpy(cp, mtod(m, caddr_t) + off, count);
		len -= (int)count;
		cp += count;
		off = 0;
		m = m->m_next;
	}
}

/*
 * Copy @len bytes into the chain at @off, extending it with new mbufs as
 * needed.  Storage is never shared so no copy-on-write is required.
 */
void
m_copyback(struct mbuf *m0, int off, int len, const void *vp)
{
	int mlen;
	struct mbuf *m = m0, *n;
	int totlen = 0;
	const caddr_t cp = __DECONST(caddr_t, vp);
	const char *src = cp;

	if (m0 == NULL)
		return;
	while (off > (mlen = m->m_len)) {
		off -= mlen;
		totlen += mlen;
		if (m->m_next == NULL) {
			n = m_get(M_NOWAIT, m->m_type);
			if (n == NULL)
				goto out;
			memset(mtod(n, caddr_t), 0, MLEN);
			n->m_len = min((int)MLEN, len + off);
			m->m_next = n;
		}
		m = m->m_next;
	}
	while (len > 0) {
		if (m->m_next == NULL && (len > m->m_len - off)) {
			m->m_len += min((int)(M_SIZE(m) -
			    (m->m_data - M_START(m))) - m->m_len, len - (m->m_len - off));
		}
		mlen = min(m->m_len - off, len);
		memcpy(mtod(m, caddr_t) + off, src, (size_t)mlen);
		src += mlen;
		len -= mlen;
		mlen += off;
		off = 0;
		totlen += mlen;
		if (len == 0)
			break;
		if (m->m_next == NULL) {
			n = m_get(M_NOWAIT, m->m_type);
			if (n == NULL)
				break;
			n->m_len = min((int)MLEN, len);
			m->m_next = n;
		}
		m = m->m_next;
	}
out:
	if (((m = m0)->m_flags & M_PKTHDR) && (m->m_pkthdr.len < totlen))
		m->m_pkthdr.len = totlen;
}

/*
 * Make a copy of an mbuf chain starting @off0 bytes from the beginning,
 * continuing for @len bytes (M_COPYALL = -1 for the rest).  Data is
 * copied, not shared.
 */
#define M_COPYALL	1000000000
struct mbuf *
m_copym(struct mbuf *m, int off0, int len, int wait)
{
	struct mbuf *n, **np;
	int off = off0;
	struct mbuf *top;
	int copyhdr = 0;

	KASSERT(off >= 0, ("m_copym, negative off %d", off));
	KASSERT(len >= 0, ("m_copym, negative len %d", len));
	if (off == 0 && m->m_flags & M_PKTHDR)
		copyhdr = 1;
	while (off > 0) {
		KASSERT(m != NULL, ("m_copym, offset > size of mbuf chain"));
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = m->m_next;
	}
	np = &top;
	top = NULL;
	while (len > 0) {
		if (m == NULL) {
			KASSERT(len == M_COPYALL,
			    ("m_copym, length > size of mbuf chain"));
			break;
		}
		if (copyhdr)
			n = m_gethdr(wait, m->m_type);
		else
			n = m_get(wait, m->m_type);
		*np = n;
		if (n == NULL)
			goto nospace;
		if (copyhdr) {
			if (!m_dup_pkthdr(n, m, wait))
				goto nospace;
			if (len == M_COPYALL)
				n->m_pkthdr.len -= off0;
			else
				n->m_pkthdr.len = len;
			copyhdr = 0;
		}
		n->m_len = min(len, m->m_len - off);
		if ((u_int)n->m_len > M_SIZE(n)) {
			/* source mbuf larger than our buffer: split it */
			n->m_len = (int)M_SIZE(n);
		}
		memcpy(mtod(n, caddr_t), mtod(m, caddr_t) + off, (size_t)n->m_len);
		if (len != M_COPYALL)
			len -= n->m_len;
		off += n->m_len;
		if (off >= m->m_len) {
			off = 0;
			m = m->m_next;
		}
		np = &n->m_next;
	}
	return (top);
nospace:
	m_freem(top);
	return (NULL);
}

struct mbuf *
m_copypacket(struct mbuf *m, int how)
{
	return (m_copym(m, 0, M_COPYALL, how));
}

struct mbuf *
m_dup(const struct mbuf *m, int how)
{
	struct mbuf **p, *top = NULL;
	int remain, moff, nsize;

	if (m == NULL)
		return (NULL);
	M_ASSERTPKTHDR(m);
	remain = m->m_pkthdr.len;
	moff = 0;
	p = &top;
	while (remain > 0) {
		struct mbuf *n;

		if (top == NULL) {
			n = m_get2(remain, how, m->m_type, M_PKTHDR);
			if (n == NULL)
				goto nospace;
			if (!m_dup_pkthdr(n, m, how)) {
				m_free(n);
				goto nospace;
			}
		} else {
			n = m_get2(remain, how, m->m_type, 0);
			if (n == NULL)
				goto nospace;
		}
		nsize = (int)M_SIZE(n);
		*p = n;
		p = &n->m_next;
		/* copy as much as possible into this mbuf */
		while (n->m_len < nsize && m != NULL) {
			int chunk = min(nsize - n->m_len, m->m_len - moff);

			memcpy(mtod(n, caddr_t) + n->m_len, mtod(m, caddr_t) + moff,
			    (size_t)chunk);
			moff += chunk;
			n->m_len += chunk;
			remain -= chunk;
			if (moff == m->m_len) {
				m = m->m_next;
				moff = 0;
			}
		}
	}
	return (top);
nospace:
	m_freem(top);
	return (NULL);
}

/* Concatenate n onto m; data is copied into m where it fits. */
void
m_cat(struct mbuf *m, struct mbuf *n)
{
	while (m->m_next != NULL)
		m = m->m_next;
	while (n != NULL) {
		if (!M_WRITABLE(m) || (int)M_TRAILINGSPACE(m) < n->m_len) {
			/* just join the two chains */
			m->m_next = n;
			return;
		}
		/* splat the data from one into the other */
		memcpy(mtod(m, caddr_t) + m->m_len, mtod(n, caddr_t), (size_t)n->m_len);
		m->m_len += n->m_len;
		n = m_free(n);
	}
}

void
m_catpkt(struct mbuf *m, struct mbuf *n)
{
	M_ASSERTPKTHDR(m);
	M_ASSERTPKTHDR(n);
	m->m_pkthdr.len += n->m_pkthdr.len;
	m_demote(n, 1, 0);
	m_cat(m, n);
}

/*
 * Partition an mbuf chain in two pieces, returning the tail -- all but the
 * first @len0 bytes.
 */
struct mbuf *
m_split(struct mbuf *m0, int len0, int wait)
{
	struct mbuf *m, *n;
	u_int len = (u_int)len0, remain;

	for (m = m0; m != NULL && len > (u_int)m->m_len; m = m->m_next)
		len -= (u_int)m->m_len;
	if (m == NULL)
		return (NULL);
	remain = (u_int)m->m_len - len;
	if (m0->m_flags & M_PKTHDR && remain == 0) {
		n = m_gethdr(wait, m0->m_type);
		if (n == NULL)
			return (NULL);
		n->m_next = m->m_next;
		m->m_next = NULL;
		n->m_pkthdr.rcvif = m0->m_pkthdr.rcvif;
		n->m_pkthdr.len = m0->m_pkthdr.len - len0;
		m0->m_pkthdr.len = len0;
		return (n);
	} else if (m0->m_flags & M_PKTHDR) {
		n = m_gethdr(wait, m0->m_type);
		if (n == NULL)
			return (NULL);
		n->m_pkthdr.rcvif = m0->m_pkthdr.rcvif;
		n->m_pkthdr.len = m0->m_pkthdr.len - len0;
		m0->m_pkthdr.len = len0;
		if (remain > (u_int)MHLEN) {
			/* m can't be the lead packet */
			M_ALIGN(n, 0);
			n->m_next = m_split(m, (int)len, wait);
			if (n->m_next == NULL) {
				(void)m_free(n);
				return (NULL);
			} else {
				n->m_len = 0;
				return (n);
			}
		} else
			M_ALIGN(n, (int)remain);
	} else if (remain == 0) {
		n = m->m_next;
		m->m_next = NULL;
		return (n);
	} else {
		n = m_get(wait, m->m_type);
		if (n == NULL)
			return (NULL);
		M_ALIGN(n, (int)remain);
	}
	n->m_len = (int)remain;
	memcpy(mtod(n, caddr_t), mtod(m, caddr_t) + len, remain);
	m->m_len = (int)len;
	n->m_next = m->m_next;
	m->m_next = NULL;
	return (n);
}

/* Append @len bytes to the chain, allocating mbufs as needed. */
int
m_append(struct mbuf *m0, int len, const void *cpp)
{
	struct mbuf *m, *n;
	int remainder, space;
	const char *cp = cpp;

	for (m = m0; m->m_next != NULL; m = m->m_next)
		continue;
	remainder = len;
	space = (int)M_TRAILINGSPACE(m);
	if (space > 0) {
		/* Copy into available space. */
		if (space > remainder)
			space = remainder;
		memcpy(mtod(m, caddr_t) + m->m_len, cp, (size_t)space);
		m->m_len += space;
		cp += space, remainder -= space;
	}
	while (remainder > 0) {
		/* Allocate a new mbuf; could check space and allocate a
		 * cluster instead. */
		n = m_get(M_NOWAIT, m->m_type);
		if (n == NULL)
			break;
		n->m_len = min((int)M_SIZE(n), remainder);
		memcpy(mtod(n, caddr_t), cp, (size_t)n->m_len);
		cp += n->m_len, remainder -= n->m_len;
		m->m_next = n;
		m = n;
	}
	if (m0->m_flags & M_PKTHDR)
		m0->m_pkthdr.len += len - remainder;
	return (remainder == 0);
}

/* Apply @f to every contiguous piece of [off, off + len). */
int
m_apply(struct mbuf *m, int off, int len, int (*f)(void *, void *, u_int),
    void *arg)
{
	u_int count;
	int rval;

	KASSERT(off >= 0, ("m_apply, negative off %d", off));
	KASSERT(len >= 0, ("m_apply, negative len %d", len));
	while (off > 0) {
		KASSERT(m != NULL, ("m_apply, offset > size of mbuf chain"));
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = m->m_next;
	}
	while (len > 0) {
		KASSERT(m != NULL, ("m_apply, offset > size of mbuf chain"));
		count = (u_int)min(m->m_len - off, len);
		rval = (*f)(arg, mtod(m, caddr_t) + off, count);
		if (rval)
			return (rval);
		len -= (int)count;
		off = 0;
		m = m->m_next;
	}
	return (0);
}

struct mbuf *
m_getptr(struct mbuf *m, int loc, int *off)
{
	while (loc >= 0) {
		if (m->m_len > loc) {
			*off = loc;
			return (m);
		} else {
			loc -= m->m_len;
			if (m->m_next == NULL) {
				if (loc == 0) {
					*off = m->m_len;
					return (m);
				}
				return (NULL);
			}
			m = m->m_next;
		}
	}
	return (NULL);
}

/* Linearise a packet into one mbuf where it fits. */
struct mbuf *
m_defrag(struct mbuf *m0, int how)
{
	struct mbuf *m;
	int len;

	M_ASSERTPKTHDR(m0);
	len = m0->m_pkthdr.len;
	if (len > MJUM16BYTES)
		return (NULL);
	m = m_get2(len, how, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (NULL);
	if (!m_dup_pkthdr(m, m0, how)) {
		m_free(m);
		return (NULL);
	}
	m_copydata(m0, 0, len, mtod(m, caddr_t));
	m->m_len = len;
	m->m_pkthdr.len = len;
	m_freem(m0);
	return (m);
}

struct mbuf *
m_collapse(struct mbuf *m0, int how, int maxfrags)
{
	struct mbuf *m;
	int n = 0;

	for (m = m0; m != NULL; m = m->m_next)
		n++;
	if (n <= maxfrags)
		return (m0);
	return (m_defrag(m0, how));
}

/* Storage is private to each mbuf; nothing to unshare. */
struct mbuf *
m_unshare(struct mbuf *m, int how)
{
	(void)how;
	return (m);
}

struct mbuf *
m_fragment(struct mbuf *m0, int how, int length)
{
	(void)how;
	(void)length;
	return (m0);
}

struct mbuf *
m_devget(char *buf, int totlen, int off, struct ifnet *ifp,
    void (*copy)(char *from, caddr_t to, u_int len))
{
	struct mbuf *m;
	struct mbuf *top = NULL, **mp = &top;
	int len;

	if (off < 0 || off > MHLEN)
		return (NULL);
	while (totlen > 0) {
		if (top == NULL) {
			m = m_get2(totlen + off, M_NOWAIT, MT_DATA, M_PKTHDR);
			if (m == NULL)
				return (NULL);
			m->m_pkthdr.rcvif = ifp;
			m->m_pkthdr.len = totlen;
			m->m_data += off;
		} else {
			m = m_get2(totlen, M_NOWAIT, MT_DATA, 0);
			if (m == NULL) {
				m_freem(top);
				return (NULL);
			}
		}
		len = min(totlen, (int)(M_START(m) + M_SIZE(m) - m->m_data));
		if (copy)
			copy(buf, mtod(m, caddr_t), (u_int)len);
		else
			memcpy(mtod(m, caddr_t), buf, (size_t)len);
		m->m_len = len;
		buf += len;
		*mp = m;
		mp = &m->m_next;
		totlen -= len;
	}
	return (top);
}

int
m_sanity(struct mbuf *m0, int sanitize)
{
	struct mbuf *m;
	int pktlen = 0;

	(void)sanitize;
	for (m = m0; m != NULL; m = m->m_next) {
		if (m->m_bsdkpi_magic != BSDKPI_MBUF_MAGIC)
			return (0);
		if (m->m_data < M_START(m) ||
		    m->m_data + m->m_len > M_START(m) + M_SIZE(m))
			return (0);
		pktlen += m->m_len;
	}
	if ((m0->m_flags & M_PKTHDR) && pktlen != m0->m_pkthdr.len)
		return (0);
	return (1);
}

void
m_print(const struct mbuf *m, int maxlen)
{
	bsdkpi_mbuf_dump(m, "m_print");
	(void)maxlen;
}

void
bsdkpi_mbuf_dump(const struct mbuf *m, const char *what)
{
	int n = 0;

	printf("%s: mbuf %p%s", what, m,
	    (m != NULL && (m->m_flags & M_PKTHDR)) ? " [pkthdr]" : "");
	if (m != NULL && (m->m_flags & M_PKTHDR))
		printf(" len %d rcvif %p flowid %u", m->m_pkthdr.len,
		    m->m_pkthdr.rcvif, m->m_pkthdr.flowid);
	printf("\n");
	for (; m != NULL; m = m->m_next, n++)
		printf("  [%d] %p data %p len %d flags %b buf %u\n", n, m,
		    m->m_data, m->m_len, (int)m->m_flags, M_FLAG_PRINTF,
		    m->m_bufsize);
}

/* ---- packet tags ---------------------------------------------------- */

void
m_tag_free_default(struct m_tag *t)
{
	free(t, M_TEMP);
}

struct m_tag *
m_tag_alloc(uint32_t cookie, uint16_t type, int len, int wait)
{
	struct m_tag *t;

	if (len < 0)
		return (NULL);
	t = malloc(len + sizeof(struct m_tag), M_TEMP, wait);
	if (t == NULL)
		return (NULL);
	m_tag_setup(t, cookie, type, len);
	t->m_tag_free = m_tag_free_default;
	return (t);
}

void
m_tag_delete(struct mbuf *m, struct m_tag *t)
{
	m_tag_unlink(m, t);
	m_tag_free(t);
}

/* Unlink and free a packet tag chain, starting from given tag (or all). */
void
m_tag_delete_chain(struct mbuf *m, struct m_tag *t)
{
	struct m_tag *p, *q;

	if (t != NULL)
		p = t;
	else
		p = SLIST_FIRST(&m->m_pkthdr.tags);
	if (p == NULL)
		return;
	while ((q = SLIST_NEXT(p, m_tag_link)) != NULL)
		m_tag_delete(m, q);
	m_tag_delete(m, p);
}

void
m_tag_delete_nonpersistent(struct mbuf *m)
{
	struct m_tag *p, *q;

	SLIST_FOREACH_SAFE(p, &m->m_pkthdr.tags, m_tag_link, q)
		if ((p->m_tag_id & MTAG_PERSISTENT) == 0)
			m_tag_delete(m, p);
}

struct m_tag *
m_tag_locate(struct mbuf *m, uint32_t cookie, uint16_t type, struct m_tag *t)
{
	struct m_tag *p;

	if (t == NULL)
		p = SLIST_FIRST(&m->m_pkthdr.tags);
	else
		p = SLIST_NEXT(t, m_tag_link);
	while (p != NULL) {
		if (p->m_tag_cookie == cookie && p->m_tag_id == type)
			return (p);
		p = SLIST_NEXT(p, m_tag_link);
	}
	return (NULL);
}

struct m_tag *
m_tag_copy(struct m_tag *t, int how)
{
	struct m_tag *p;

	p = m_tag_alloc(t->m_tag_cookie, t->m_tag_id, t->m_tag_len, how);
	if (p == NULL)
		return (NULL);
	memcpy(p + 1, t + 1, t->m_tag_len);
	return (p);
}

int
m_tag_copy_chain(struct mbuf *to, const struct mbuf *from, int how)
{
	struct m_tag *p, *t, *tprev = NULL;

	m_tag_delete_chain(to, NULL);
	SLIST_FOREACH(p, &from->m_pkthdr.tags, m_tag_link) {
		t = m_tag_copy(p, how);
		if (t == NULL) {
			m_tag_delete_chain(to, NULL);
			return (0);
		}
		if (tprev == NULL)
			SLIST_INSERT_HEAD(&to->m_pkthdr.tags, t, m_tag_link);
		else
			SLIST_INSERT_AFTER(tprev, t, m_tag_link);
		tprev = t;
	}
	return (1);
}

/* ---- glue helpers --------------------------------------------------- */

struct mbuf *
bsdkpi_mbuf_from_buffer(const void *data, size_t len, int how)
{
	struct mbuf *m;

	if (len > MJUM16BYTES)
		return (NULL);
	m = m_get2((int)len, how, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (NULL);
	memcpy(mtod(m, void *), data, len);
	m->m_len = (int)len;
	m->m_pkthdr.len = (int)len;
	return (m);
}

size_t
bsdkpi_mbuf_to_buffer(const struct mbuf *m, void *buf, size_t len)
{
	size_t total = 0;

	for (; m != NULL && total < len; m = m->m_next) {
		size_t n = (size_t)m->m_len;

		if (n > len - total)
			n = len - total;
		memcpy((char *)buf + total, m->m_data, n);
		total += n;
	}
	return (total);
}
