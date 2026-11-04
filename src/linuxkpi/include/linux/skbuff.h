/* SPDX-License-Identifier: MIT */
/*
 * struct sk_buff.
 *
 * A linear buffer plus an optional fragment list, allocated with kmalloc.
 * head/data/tail/end are pointers (as on 64-bit Linux configurations that
 * use pointer offsets this is the same layout the driver reasons about).
 * The shared info block lives at the end of the data area, exactly like
 * upstream, so skb_shinfo(), skb_end_pointer() and skb_tailroom() behave
 * identically.  mt76 uses the fragment list for A-MSDU RX and the frag
 * array for page-pool RX buffers (skb_add_rx_frag).
 *
 * Implementation: src/linuxkpi/src/skbuff.c.
 */
#ifndef _LKPI_LINUX_SKBUFF_H_
#define _LKPI_LINUX_SKBUFF_H_
#include <linux/kernel.h>
#include <linux/page.h>
#include <linux/list.h>
#include <linux/gfp.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/refcount.h>
#include <linux/netdev_features.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/cache.h>

struct net_device;
struct sock;
struct napi_struct;

#define MAX_SKB_FRAGS		17
#define NET_SKB_PAD		64
#define NET_IP_ALIGN		2
#define SKB_DATA_ALIGN(X)	ALIGN(X, SMP_CACHE_BYTES)
#define SKB_WITH_OVERHEAD(X)	((X) - SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
#define SKB_MAX_ORDER(X, ORDER)	SKB_WITH_OVERHEAD((PAGE_SIZE << (ORDER)) - (X))
#define SKB_MAX_HEAD(X)		SKB_MAX_ORDER((X), 0)
#define SKB_TRUESIZE(X)		((X) + SKB_DATA_ALIGN(sizeof(struct sk_buff)) + SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

enum {
	CHECKSUM_NONE = 0,
	CHECKSUM_UNNECESSARY = 1,
	CHECKSUM_COMPLETE = 2,
	CHECKSUM_PARTIAL = 3,
};
#define PACKET_HOST		0
#define PACKET_BROADCAST	1
#define PACKET_MULTICAST	2
#define PACKET_OTHERHOST	3

typedef struct bio_vec {
	struct page *bv_page;
	unsigned int bv_len;
	unsigned int bv_offset;
} skb_frag_t;

struct skb_shared_info {
	u8 flags;
	u8 nr_frags;
	u8 tx_flags;
	unsigned short gso_size;
	unsigned short gso_segs;
	struct sk_buff *frag_list;
	unsigned int gso_type;
	u32 tskey;
	atomic_t dataref;
	void *destructor_arg;
	skb_frag_t frags[MAX_SKB_FRAGS];
};

struct sk_buff {
	union {
		struct {
			struct sk_buff *next;
			struct sk_buff *prev;
		};
		struct list_head list;
	};
	struct net_device *dev;
	struct sock *sk;
	ktime_t tstamp;
	unsigned int len;
	unsigned int data_len;
	unsigned int truesize;
	u16 mac_len;
	u16 hdr_len;
	u16 queue_mapping;
	u8 cloned:1, head_frag:1, pp_recycle:1, pfmemalloc:1, ip_summed:2, pkt_type:3, nohdr:1;
	__be16 protocol;
	u32 priority;
	u32 mark;
	u32 hash;
	u16 transport_header;	/* offsets from head */
	u16 network_header;
	u16 mac_header;
	/* 64 bytes like FreeBSD: the vendored ieee80211_tx_info needs more than Linux's 48. */
	char cb[64] __aligned(8);
	void (*destructor)(struct sk_buff *skb);
	refcount_t users;
	u8 *head;
	u8 *data;
	u8 *tail;
	u8 *end;
};

struct sk_buff_head {
	union {
		struct {
			struct sk_buff *next;
			struct sk_buff *prev;
		};
		struct sk_buff_list_dummy { struct sk_buff *next, *prev; } list;
	};
	u32 qlen;
	spinlock_t lock;
};

#define skb_shinfo(skb)		((struct skb_shared_info *)(skb)->end)
#define skb_end_pointer(skb)	((skb)->end)
static inline unsigned int skb_end_offset(const struct sk_buff *skb) { return (unsigned int)(skb->end - skb->head); }

/* ---- allocation ---- */
struct sk_buff *__alloc_skb(unsigned int size, gfp_t gfp, int flags, int node);
static inline struct sk_buff *alloc_skb(unsigned int size, gfp_t gfp) { return __alloc_skb(size, gfp, 0, -1); }
static inline struct sk_buff *dev_alloc_skb(unsigned int len)
{
	struct sk_buff *skb = alloc_skb(len + NET_SKB_PAD, GFP_ATOMIC);
	if (skb)
		skb->data += NET_SKB_PAD, skb->tail += NET_SKB_PAD;
	return skb;
}
static inline struct sk_buff *__netdev_alloc_skb(struct net_device *dev, unsigned int len, gfp_t gfp)
{
	struct sk_buff *skb = alloc_skb(len + NET_SKB_PAD, gfp);
	if (skb) {
		skb->dev = dev;
		skb->data += NET_SKB_PAD;
		skb->tail += NET_SKB_PAD;
	}
	return skb;
}
static inline struct sk_buff *netdev_alloc_skb(struct net_device *dev, unsigned int len)
{
	return __netdev_alloc_skb(dev, len, GFP_ATOMIC);
}
#define napi_alloc_skb(napi, len) dev_alloc_skb(len)
#define __napi_alloc_skb(napi, len, gfp) dev_alloc_skb(len)
static inline struct page *dev_alloc_pages(unsigned int order)
{
	return alloc_pages(GFP_ATOMIC | __GFP_NOWARN | __GFP_COMP, order);
}
#define dev_alloc_page() dev_alloc_pages(0)
/* Wrap an existing buffer (page-pool fragment) of @frag_size bytes. */
struct sk_buff *build_skb(void *data, unsigned int frag_size);
struct sk_buff *napi_build_skb(void *data, unsigned int frag_size);
struct sk_buff *skb_clone(struct sk_buff *skb, gfp_t gfp);
struct sk_buff *skb_copy(const struct sk_buff *skb, gfp_t gfp);
struct sk_buff *pskb_copy(struct sk_buff *skb, gfp_t gfp);
struct sk_buff *skb_copy_expand(const struct sk_buff *skb, int newheadroom, int newtailroom, gfp_t gfp);
struct sk_buff *skb_realloc_headroom(struct sk_buff *skb, unsigned int headroom);
int pskb_expand_head(struct sk_buff *skb, int nhead, int ntail, gfp_t gfp);
void kfree_skb(struct sk_buff *skb);
#define kfree_skb_reason(skb, reason) kfree_skb(skb)
void kfree_skb_list(struct sk_buff *segs);
void consume_skb(struct sk_buff *skb);
#define dev_kfree_skb(skb)		kfree_skb(skb)
#define dev_kfree_skb_any(skb)		kfree_skb(skb)
#define dev_kfree_skb_irq(skb)		kfree_skb(skb)
#define dev_consume_skb_any(skb)	consume_skb(skb)
#define napi_consume_skb(skb, budget)	consume_skb(skb)
static inline struct sk_buff *skb_get(struct sk_buff *skb) { refcount_inc(&skb->users); return skb; }
static inline int skb_shared(const struct sk_buff *skb) { return refcount_read(&skb->users) != 1; }
static inline int skb_cloned(const struct sk_buff *skb) { return skb->cloned; }
static inline struct sk_buff *skb_share_check(struct sk_buff *skb, gfp_t gfp)
{
	if (skb_shared(skb)) {
		struct sk_buff *nskb = skb_clone(skb, gfp);
		kfree_skb(skb);
		return nskb;
	}
	return skb;
}
static inline struct sk_buff *skb_unshare(struct sk_buff *skb, gfp_t gfp)
{
	if (skb_cloned(skb)) {
		struct sk_buff *nskb = skb_copy(skb, gfp);
		kfree_skb(skb);
		return nskb;
	}
	return skb;
}
#define skb_mark_for_recycle(skb) do { (skb)->pp_recycle = 1; } while (0)

/* ---- geometry ---- */
static inline unsigned int skb_headlen(const struct sk_buff *skb) { return skb->len - skb->data_len; }
static inline bool skb_is_nonlinear(const struct sk_buff *skb) { return skb->data_len != 0; }
static inline unsigned int skb_headroom(const struct sk_buff *skb) { return (unsigned int)(skb->data - skb->head); }
static inline int skb_tailroom(const struct sk_buff *skb) { return skb_is_nonlinear(skb) ? 0 : (int)(skb->end - skb->tail); }
static inline int skb_availroom(const struct sk_buff *skb) { return skb_tailroom(skb); }
static inline unsigned int skb_pagelen(const struct sk_buff *skb)
{
	unsigned int i, len = 0;
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++)
		len += skb_shinfo(skb)->frags[i].bv_len;
	return len + skb_headlen(skb);
}
static inline u8 *skb_tail_pointer(const struct sk_buff *skb) { return skb->tail; }
static inline void skb_reset_tail_pointer(struct sk_buff *skb) { skb->tail = skb->data; }
static inline void skb_set_tail_pointer(struct sk_buff *skb, const int offset) { skb->tail = skb->data + offset; }
static inline void skb_reserve(struct sk_buff *skb, int len) { skb->data += len; skb->tail += len; }
static inline void *__skb_put(struct sk_buff *skb, unsigned int len)
{
	void *tmp = skb->tail;
	skb->tail += len;
	skb->len += len;
	return tmp;
}
void *skb_put(struct sk_buff *skb, unsigned int len);	/* panics on overrun */
static inline void *__skb_put_zero(struct sk_buff *skb, unsigned int len) { return memset(__skb_put(skb, len), 0, len); }
static inline void *skb_put_zero(struct sk_buff *skb, unsigned int len) { return memset(skb_put(skb, len), 0, len); }
static inline void *__skb_put_data(struct sk_buff *skb, const void *data, unsigned int len) { return memcpy(__skb_put(skb, len), data, len); }
static inline void *skb_put_data(struct sk_buff *skb, const void *data, unsigned int len) { return memcpy(skb_put(skb, len), data, len); }
static inline void skb_put_u8(struct sk_buff *skb, u8 val) { *(u8 *)skb_put(skb, 1) = val; }
static inline void *__skb_push(struct sk_buff *skb, unsigned int len) { skb->data -= len; skb->len += len; return skb->data; }
void *skb_push(struct sk_buff *skb, unsigned int len);	/* panics on underrun */
static inline void *__skb_pull(struct sk_buff *skb, unsigned int len)
{
	skb->len -= len;
	return skb->data += len;
}
static inline void *skb_pull(struct sk_buff *skb, unsigned int len)
{
	return unlikely(len > skb->len) ? NULL : __skb_pull(skb, len);
}
static inline void *skb_pull_inline(struct sk_buff *skb, unsigned int len) { return skb_pull(skb, len); }
static inline void *pskb_pull(struct sk_buff *skb, unsigned int len) { return skb_pull(skb, len); }
static inline bool pskb_may_pull(struct sk_buff *skb, unsigned int len) { return len <= skb_headlen(skb); }
static inline void __skb_trim(struct sk_buff *skb, unsigned int len)
{
	skb->len = len;
	skb->tail = skb->data + len;
}
static inline void skb_trim(struct sk_buff *skb, unsigned int len)
{
	if (skb->len > len)
		__skb_trim(skb, len);
}
int pskb_trim(struct sk_buff *skb, unsigned int len);
static inline int pskb_trim_rcsum(struct sk_buff *skb, unsigned int len) { return pskb_trim(skb, len); }
static inline int __skb_pad(struct sk_buff *skb, int pad, bool free_on_error)
{
	if (likely(skb_tailroom(skb) >= pad)) {
		memset(skb->tail, 0, pad);
		return 0;
	}
	if (free_on_error)
		kfree_skb(skb);
	return -ENOMEM;
}
static inline int skb_pad(struct sk_buff *skb, int pad) { return __skb_pad(skb, pad, true); }
static inline int skb_put_padto(struct sk_buff *skb, unsigned int len)
{
	unsigned int size = skb->len;
	if (unlikely(size < len)) {
		len -= size;
		if (__skb_pad(skb, (int)len, true))
			return -ENOMEM;
		__skb_put(skb, len);
	}
	return 0;
}
int skb_linearize(struct sk_buff *skb);
static inline int skb_cow_head(struct sk_buff *skb, unsigned int headroom)
{
	if (skb_headroom(skb) >= headroom && !skb_cloned(skb))
		return 0;
	return pskb_expand_head(skb, headroom > skb_headroom(skb) ? (int)(headroom - skb_headroom(skb)) : 0, 0, GFP_ATOMIC);
}
static inline int skb_cow(struct sk_buff *skb, unsigned int headroom) { return skb_cow_head(skb, headroom); }
static inline int skb_copy_bits(const struct sk_buff *skb, int offset, void *to, int len)
{
	if (offset + len > (int)skb_headlen(skb))
		return -EFAULT;
	memcpy(to, skb->data + offset, len);
	return 0;
}
int skb_copy_bits_frags(const struct sk_buff *skb, int offset, void *to, int len);
static inline void skb_copy_from_linear_data(const struct sk_buff *skb, void *to, const unsigned int len) { memcpy(to, skb->data, len); }
static inline void skb_copy_from_linear_data_offset(const struct sk_buff *skb, const int offset, void *to, const unsigned int len) { memcpy(to, skb->data + offset, len); }
static inline void skb_copy_to_linear_data(struct sk_buff *skb, const void *from, const unsigned int len) { memcpy(skb->data, from, len); }
static inline void skb_copy_to_linear_data_offset(struct sk_buff *skb, const int offset, const void *from, const unsigned int len) { memcpy(skb->data + offset, from, len); }

/* ---- header offsets ---- */
static inline unsigned char *skb_mac_header(const struct sk_buff *skb) { return skb->head + skb->mac_header; }
static inline void skb_reset_mac_header(struct sk_buff *skb) { skb->mac_header = (u16)(skb->data - skb->head); }
static inline void skb_set_mac_header(struct sk_buff *skb, const int offset) { skb_reset_mac_header(skb); skb->mac_header += offset; }
static inline int skb_mac_header_was_set(const struct sk_buff *skb) { return skb->mac_header != (u16)~0U; }
static inline int skb_mac_offset(const struct sk_buff *skb) { return (int)(skb_mac_header(skb) - skb->data); }
static inline unsigned char *skb_network_header(const struct sk_buff *skb) { return skb->head + skb->network_header; }
static inline void skb_reset_network_header(struct sk_buff *skb) { skb->network_header = (u16)(skb->data - skb->head); }
static inline void skb_set_network_header(struct sk_buff *skb, const int offset) { skb_reset_network_header(skb); skb->network_header += offset; }
static inline int skb_network_offset(const struct sk_buff *skb) { return (int)(skb_network_header(skb) - skb->data); }
static inline unsigned char *skb_transport_header(const struct sk_buff *skb) { return skb->head + skb->transport_header; }
static inline void skb_reset_transport_header(struct sk_buff *skb) { skb->transport_header = (u16)(skb->data - skb->head); }
static inline void skb_set_transport_header(struct sk_buff *skb, const int offset) { skb_reset_transport_header(skb); skb->transport_header += offset; }
static inline int skb_transport_offset(const struct sk_buff *skb) { return (int)(skb_transport_header(skb) - skb->data); }
static inline unsigned int skb_network_header_len(const struct sk_buff *skb) { return skb->transport_header - skb->network_header; }
static inline void skb_set_queue_mapping(struct sk_buff *skb, u16 q) { skb->queue_mapping = q; }
static inline u16 skb_get_queue_mapping(const struct sk_buff *skb) { return skb->queue_mapping; }
static inline void skb_record_rx_queue(struct sk_buff *skb, u16 q) { skb->queue_mapping = q + 1; }
static inline u16 skb_get_rx_queue(const struct sk_buff *skb) { return skb->queue_mapping - 1; }
static inline void skb_set_hash(struct sk_buff *skb, u32 hash, int type) { (void)type; skb->hash = hash; }
static inline u32 skb_get_hash(struct sk_buff *skb) { return skb->hash; }
static inline void skb_orphan(struct sk_buff *skb) { if (skb->destructor) { skb->destructor(skb); skb->destructor = NULL; } skb->sk = NULL; }
static inline void skb_checksum_none_assert(const struct sk_buff *skb) { (void)skb; }
static inline struct sk_buff *skb_frag_list_first(struct sk_buff *skb) { return skb_shinfo(skb)->frag_list; }
#define skb_walk_frags(skb, iter) \
	for (iter = skb_shinfo(skb)->frag_list; iter; iter = iter->next)
static inline bool skb_has_frag_list(const struct sk_buff *skb) { return skb_shinfo(skb)->frag_list != NULL; }
static inline void skb_frag_list_init(struct sk_buff *skb) { skb_shinfo(skb)->frag_list = NULL; }

/* ---- page fragments ---- */
static inline struct page *skb_frag_page(const skb_frag_t *frag) { return frag->bv_page; }
static inline unsigned int skb_frag_size(const skb_frag_t *frag) { return frag->bv_len; }
static inline unsigned int skb_frag_off(const skb_frag_t *frag) { return frag->bv_offset; }
static inline void *skb_frag_address(const skb_frag_t *frag) { return (u8 *)page_address(frag->bv_page) + frag->bv_offset; }
static inline void skb_frag_size_set(skb_frag_t *frag, unsigned int size) { frag->bv_len = size; }
static inline void skb_frag_size_add(skb_frag_t *frag, int delta) { frag->bv_len += delta; }
static inline void skb_frag_off_add(skb_frag_t *frag, int delta) { frag->bv_offset += delta; }
static inline void __skb_fill_page_desc(struct sk_buff *skb, int i, struct page *page, int off, int size)
{
	skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
	frag->bv_page = page;
	frag->bv_offset = off;
	frag->bv_len = size;
}
static inline void skb_fill_page_desc(struct sk_buff *skb, int i, struct page *page, int off, int size)
{
	__skb_fill_page_desc(skb, i, page, off, size);
	skb_shinfo(skb)->nr_frags = i + 1;
}
static inline void skb_add_rx_frag(struct sk_buff *skb, int i, struct page *page, int off, int size,
    unsigned int truesize)
{
	skb_fill_page_desc(skb, i, page, off, size);
	skb->len += size;
	skb->data_len += size;
	skb->truesize += truesize;
}
static inline void skb_coalesce_rx_frag(struct sk_buff *skb, int i, int size, unsigned int truesize)
{
	skb_frag_size_add(&skb_shinfo(skb)->frags[i], size);
	skb->len += size;
	skb->data_len += size;
	skb->truesize += truesize;
}
void skb_free_frag(void *addr);

/* ---- queues ---- */
static inline void __skb_queue_head_init(struct sk_buff_head *list)
{
	list->prev = list->next = (struct sk_buff *)list;
	list->qlen = 0;
}
static inline void skb_queue_head_init(struct sk_buff_head *list)
{
	spin_lock_init(&list->lock);
	__skb_queue_head_init(list);
}
static inline int skb_queue_empty(const struct sk_buff_head *list) { return list->next == (const struct sk_buff *)list; }
static inline bool skb_queue_empty_lockless(const struct sk_buff_head *list) { return READ_ONCE(list->next) == (const struct sk_buff *)list; }
static inline u32 skb_queue_len(const struct sk_buff_head *list) { return list->qlen; }
static inline u32 skb_queue_len_lockless(const struct sk_buff_head *list) { return READ_ONCE(list->qlen); }
static inline bool skb_queue_is_last(const struct sk_buff_head *list, const struct sk_buff *skb) { return skb->next == (const struct sk_buff *)list; }
static inline bool skb_queue_is_first(const struct sk_buff_head *list, const struct sk_buff *skb) { return skb->prev == (const struct sk_buff *)list; }
static inline struct sk_buff *skb_queue_next(const struct sk_buff_head *list, const struct sk_buff *skb) { (void)list; return skb->next; }
static inline struct sk_buff *skb_queue_prev(const struct sk_buff_head *list, const struct sk_buff *skb) { (void)list; return skb->prev; }
static inline struct sk_buff *skb_peek(const struct sk_buff_head *list)
{
	struct sk_buff *skb = list->next;
	return skb == (struct sk_buff *)list ? NULL : skb;
}
static inline struct sk_buff *skb_peek_tail(const struct sk_buff_head *list)
{
	struct sk_buff *skb = list->prev;
	return skb == (struct sk_buff *)list ? NULL : skb;
}
static inline void __skb_insert(struct sk_buff *newsk, struct sk_buff *prev, struct sk_buff *next,
    struct sk_buff_head *list)
{
	newsk->next = next;
	newsk->prev = prev;
	next->prev = prev->next = newsk;
	list->qlen++;
}
static inline void __skb_queue_after(struct sk_buff_head *list, struct sk_buff *prev, struct sk_buff *newsk)
{
	__skb_insert(newsk, prev, prev->next, list);
}
static inline void __skb_queue_before(struct sk_buff_head *list, struct sk_buff *next, struct sk_buff *newsk)
{
	__skb_insert(newsk, next->prev, next, list);
}
static inline void __skb_queue_head(struct sk_buff_head *list, struct sk_buff *newsk) { __skb_queue_after(list, (struct sk_buff *)list, newsk); }
static inline void __skb_queue_tail(struct sk_buff_head *list, struct sk_buff *newsk) { __skb_queue_before(list, (struct sk_buff *)list, newsk); }
static inline void __skb_unlink(struct sk_buff *skb, struct sk_buff_head *list)
{
	struct sk_buff *next = skb->next, *prev = skb->prev;
	list->qlen--;
	skb->next = skb->prev = NULL;
	next->prev = prev;
	prev->next = next;
}
static inline struct sk_buff *__skb_dequeue(struct sk_buff_head *list)
{
	struct sk_buff *skb = skb_peek(list);
	if (skb)
		__skb_unlink(skb, list);
	return skb;
}
static inline struct sk_buff *__skb_dequeue_tail(struct sk_buff_head *list)
{
	struct sk_buff *skb = skb_peek_tail(list);
	if (skb)
		__skb_unlink(skb, list);
	return skb;
}
static inline void skb_queue_head(struct sk_buff_head *list, struct sk_buff *newsk)
{
	unsigned long flags;
	spin_lock_irqsave(&list->lock, flags);
	__skb_queue_head(list, newsk);
	spin_unlock_irqrestore(&list->lock, flags);
}
static inline void skb_queue_tail(struct sk_buff_head *list, struct sk_buff *newsk)
{
	unsigned long flags;
	spin_lock_irqsave(&list->lock, flags);
	__skb_queue_tail(list, newsk);
	spin_unlock_irqrestore(&list->lock, flags);
}
static inline void skb_unlink(struct sk_buff *skb, struct sk_buff_head *list)
{
	unsigned long flags;
	spin_lock_irqsave(&list->lock, flags);
	__skb_unlink(skb, list);
	spin_unlock_irqrestore(&list->lock, flags);
}
static inline struct sk_buff *skb_dequeue(struct sk_buff_head *list)
{
	unsigned long flags;
	struct sk_buff *skb;
	spin_lock_irqsave(&list->lock, flags);
	skb = __skb_dequeue(list);
	spin_unlock_irqrestore(&list->lock, flags);
	return skb;
}
static inline struct sk_buff *skb_dequeue_tail(struct sk_buff_head *list)
{
	unsigned long flags;
	struct sk_buff *skb;
	spin_lock_irqsave(&list->lock, flags);
	skb = __skb_dequeue_tail(list);
	spin_unlock_irqrestore(&list->lock, flags);
	return skb;
}
static inline void __skb_queue_purge(struct sk_buff_head *list)
{
	struct sk_buff *skb;
	while ((skb = __skb_dequeue(list)) != NULL)
		kfree_skb(skb);
}
static inline void skb_queue_purge(struct sk_buff_head *list)
{
	struct sk_buff *skb;
	while ((skb = skb_dequeue(list)) != NULL)
		kfree_skb(skb);
}
#define __skb_queue_purge_reason(l, r) __skb_queue_purge(l)
#define skb_queue_purge_reason(l, r) skb_queue_purge(l)
static inline void __skb_queue_splice(const struct sk_buff_head *list, struct sk_buff *prev, struct sk_buff *next)
{
	struct sk_buff *first = list->next;
	struct sk_buff *last = list->prev;
	first->prev = prev;
	prev->next = first;
	last->next = next;
	next->prev = last;
}
static inline void skb_queue_splice(const struct sk_buff_head *list, struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, (struct sk_buff *)head, head->next);
		head->qlen += list->qlen;
	}
}
static inline void skb_queue_splice_init(struct sk_buff_head *list, struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, (struct sk_buff *)head, head->next);
		head->qlen += list->qlen;
		__skb_queue_head_init(list);
	}
}
static inline void skb_queue_splice_tail(const struct sk_buff_head *list, struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, head->prev, (struct sk_buff *)head);
		head->qlen += list->qlen;
	}
}
static inline void skb_queue_splice_tail_init(struct sk_buff_head *list, struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, head->prev, (struct sk_buff *)head);
		head->qlen += list->qlen;
		__skb_queue_head_init(list);
	}
}
#define skb_queue_walk(queue, skb) \
	for (skb = (queue)->next; skb != (struct sk_buff *)(queue); skb = skb->next)
#define skb_queue_walk_safe(queue, skb, tmp) \
	for (skb = (queue)->next, tmp = skb->next; skb != (struct sk_buff *)(queue); skb = tmp, tmp = skb->next)
#define skb_queue_walk_from(queue, skb) \
	for (; skb != (struct sk_buff *)(queue); skb = skb->next)
#define skb_queue_reverse_walk(queue, skb) \
	for (skb = (queue)->prev; skb != (struct sk_buff *)(queue); skb = skb->prev)
#define skb_queue_reverse_walk_safe(queue, skb, tmp) \
	for (skb = (queue)->prev, tmp = skb->prev; skb != (struct sk_buff *)(queue); skb = tmp, tmp = skb->prev)
#define skb_queue_walk_from_safe(queue, skb, tmp) \
	for (tmp = skb->next; skb != (struct sk_buff *)(queue); skb = tmp, tmp = skb->next)

/* list_head based lists (skb->list) used by RX batching */
static inline void skb_list_del_init(struct sk_buff *skb) { list_del_init(&skb->list); }
#define skb_list_walk_safe(first, skb, next_skb) \
	for ((skb) = (first), (next_skb) = (skb) ? (skb)->next : NULL; (skb); \
	     (skb) = (next_skb), (next_skb) = (skb) ? (skb)->next : NULL)
static inline void skb_mark_not_on_list(struct sk_buff *skb) { skb->next = NULL; }

/* ---- misc ---- */
static inline struct ethhdr *eth_hdr_lkpi(const struct sk_buff *skb) { return (struct ethhdr *)skb_mac_header(skb); }
#define skb_get_tstamp(skb) ((skb)->tstamp)
static inline void skb_set_tstamp(struct sk_buff *skb, ktime_t t) { skb->tstamp = t; }
static inline void skb_set_owner_w(struct sk_buff *skb, struct sock *sk) { skb->sk = sk; }
static inline bool skb_is_gso(const struct sk_buff *skb) { return skb_shinfo(skb)->gso_size != 0; }
#define skb_zcopy(skb) (NULL)
#define SKB_GSO_CB(skb) ((void *)(skb)->cb)
#define skb_checksum_help(skb) (0)
static inline void skb_copy_queue_mapping(struct sk_buff *to, const struct sk_buff *from) { to->queue_mapping = from->queue_mapping; }
static inline void skb_scrub_packet(struct sk_buff *skb, bool xnet) { (void)skb; (void)xnet; }
static inline int skb_ensure_writable(struct sk_buff *skb, unsigned int write_len)
{
	return pskb_may_pull(skb, write_len) ? skb_cow_head(skb, 0) : -ENOMEM;
}
struct sk_buff *skb_dequeue_frag(struct sk_buff *skb);
#endif /* _LKPI_LINUX_SKBUFF_H_ */
