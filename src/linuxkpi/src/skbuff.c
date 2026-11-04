// SPDX-License-Identifier: MIT
/*
 * struct sk_buff: allocation, cloning, copying and trimming.
 *
 * The struct comes from kmalloc, the data area from lkpi_os_malloc() with
 * the shared info block at its end (skb->end), exactly like upstream.
 * build_skb() wraps a buffer the caller owns (a page-pool fragment or a
 * kmalloc block); head_frag records that the data area must go back
 * through skb_free_frag(), which finds the owning page with
 * virt_to_head_page() and recycles it into its page pool when page->pp is
 * set.  Pages that belong to a pool are always returned to that pool,
 * whether or not the skb carries pp_recycle: the pool holds the only
 * struct page reference and a plain put_page() would break its
 * accounting.
 *
 * Clones share the data area through skb_shinfo()->dataref; the frag_list
 * is a singly linked chain through skb->next, which mt76 also walks
 * directly for A-MSDU subframes.
 */
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/page.h>
#include <linux/string.h>
#include <net/page_pool/helpers.h>
#include <lkpi/os.h>

static inline bool skb_gfp_may_sleep(gfp_t gfp)
{
	return gfpflags_allow_blocking(gfp);
}

static void skb_init_shinfo(struct sk_buff *skb)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);

	memset(shinfo, 0, sizeof(*shinfo));
	atomic_set(&shinfo->dataref, 1);
}

static struct sk_buff *skb_alloc_struct(gfp_t gfp)
{
	struct sk_buff *skb = kzalloc(sizeof(*skb), gfp);

	if (!skb)
		return NULL;
	refcount_set(&skb->users, 1);
	skb->mac_header = (u16)~0U;
	skb->transport_header = (u16)~0U;
	return skb;
}

/* Point a fresh skb at a data area of @size bytes followed by the shinfo. */
static void skb_set_data_area(struct sk_buff *skb, u8 *data, unsigned int size)
{
	skb->head = data;
	skb->data = data;
	skb->tail = data;
	skb->end = data + size;
	skb->truesize = SKB_TRUESIZE(size);
	skb_init_shinfo(skb);
}

struct sk_buff *__alloc_skb(unsigned int size, gfp_t gfp, int flags, int node)
{
	struct sk_buff *skb;
	unsigned int dsize;
	u8 *data;

	skb = skb_alloc_struct(gfp);
	if (!skb)
		return NULL;

	dsize = SKB_DATA_ALIGN(size);
	data = lkpi_os_malloc(dsize + sizeof(struct skb_shared_info), false, skb_gfp_may_sleep(gfp));
	if (!data) {
		kfree(skb);
		return NULL;
	}
	skb_set_data_area(skb, data, dsize);
	return skb;
}

struct sk_buff *build_skb(void *data, unsigned int frag_size)
{
	struct sk_buff *skb;
	unsigned int size;

	if (!data)
		return NULL;
	if (frag_size) {
		if (WARN_ON_ONCE(frag_size < SKB_DATA_ALIGN(sizeof(struct skb_shared_info))))
			return NULL;
		size = SKB_WITH_OVERHEAD(frag_size);
	} else {
		size_t ksize = lkpi_os_malloc_size(data);

		if (WARN_ON_ONCE(ksize < SKB_DATA_ALIGN(sizeof(struct skb_shared_info))))
			return NULL;
		size = SKB_WITH_OVERHEAD((unsigned int)ksize);
	}

	skb = skb_alloc_struct(GFP_ATOMIC);
	if (!skb)
		return NULL;
	skb_set_data_area(skb, data, size);
	skb->head_frag = frag_size != 0;
	return skb;
}

struct sk_buff *napi_build_skb(void *data, unsigned int frag_size)
{
	return build_skb(data, frag_size);
}

/* ---- release -------------------------------------------------------- */

static inline bool skb_page_is_pp(struct page *page)
{
	return page->pp && page_pool_page_is_pp(page);
}

static void skb_page_ref(struct page *page)
{
	if (skb_page_is_pp(page))
		page_pool_ref_page(page);
	else
		get_page(page);
}

static void skb_page_unref(struct page *page)
{
	if (!page)
		return;
	if (skb_page_is_pp(page))
		page_pool_put_full_page(page->pp, page, false);
	else
		put_page(page);
}

void skb_free_frag(void *addr)
{
	struct page *page;

	if (!addr)
		return;
	page = virt_to_head_page(addr);
	if (!page) {
		/* page_frag_alloc() is kmalloc() in this port. */
		kfree(addr);
		return;
	}
	skb_page_unref(page);
}

static void skb_free_head(struct sk_buff *skb)
{
	if (skb->head_frag)
		skb_free_frag(skb->head);
	else
		lkpi_os_free(skb->head);
	skb->head = NULL;
}

static void skb_drop_frags_from(struct sk_buff *skb, int from)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	int i, n = shinfo->nr_frags;

	shinfo->nr_frags = from;
	for (i = from; i < n; i++)
		skb_page_unref(skb_frag_page(&shinfo->frags[i]));
}

static void skb_drop_fraglist(struct sk_buff *skb)
{
	kfree_skb_list(skb_shinfo(skb)->frag_list);
	skb_shinfo(skb)->frag_list = NULL;
}

/* Take references on the frags and frag_list before giving up a shared data area. */
static void skb_ref_shared_data(struct sk_buff *skb)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	struct sk_buff *list;
	int i;

	for (i = 0; i < shinfo->nr_frags; i++)
		skb_page_ref(skb_frag_page(&shinfo->frags[i]));
	skb_walk_frags(skb, list)
		skb_get(list);
}

static void skb_release_data(struct sk_buff *skb)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);

	if (!atomic_dec_and_test(&shinfo->dataref))
		return;
	skb_drop_frags_from(skb, 0);
	if (shinfo->frag_list)
		skb_drop_fraglist(skb);
	skb_free_head(skb);
}

static void skb_release(struct sk_buff *skb)
{
	if (skb->destructor) {
		skb->destructor(skb);
		skb->destructor = NULL;
	}
	skb_release_data(skb);
	kfree(skb);
}

static inline bool skb_unref(struct sk_buff *skb)
{
	if (likely(refcount_read(&skb->users) == 1))
		return true;
	return refcount_dec_and_test(&skb->users);
}

void kfree_skb(struct sk_buff *skb)
{
	if (!skb)
		return;
	if (!skb_unref(skb))
		return;
	skb_release(skb);
}

void consume_skb(struct sk_buff *skb)
{
	kfree_skb(skb);
}

void kfree_skb_list(struct sk_buff *segs)
{
	while (segs) {
		struct sk_buff *next = segs->next;

		kfree_skb(segs);
		segs = next;
	}
}

struct sk_buff *skb_dequeue_frag(struct sk_buff *skb)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	struct sk_buff *frag = shinfo->frag_list;

	if (frag) {
		shinfo->frag_list = frag->next;
		frag->next = NULL;
	}
	return frag;
}

/* ---- clone / copy --------------------------------------------------- */

struct sk_buff *skb_clone(struct sk_buff *skb, gfp_t gfp)
{
	struct sk_buff *n;

	if (!skb)
		return NULL;
	n = kmalloc(sizeof(*n), gfp);
	if (!n)
		return NULL;

	memcpy(n, skb, sizeof(*n));
	n->next = NULL;
	n->prev = NULL;
	n->sk = NULL;
	n->destructor = NULL;
	refcount_set(&n->users, 1);
	n->cloned = 1;
	n->nohdr = 0;
	skb->cloned = 1;
	atomic_inc(&skb_shinfo(skb)->dataref);
	return n;
}

/* Copy the metadata of @old into @new, whose data area may sit at a
 * different headroom; header offsets are relative to head. */
static void skb_copy_header(struct sk_buff *new, const struct sk_buff *old)
{
	int off = (int)skb_headroom(new) - (int)skb_headroom(old);

	new->dev = old->dev;
	new->tstamp = old->tstamp;
	new->protocol = old->protocol;
	new->priority = old->priority;
	new->mark = old->mark;
	new->hash = old->hash;
	new->queue_mapping = old->queue_mapping;
	new->mac_len = old->mac_len;
	new->hdr_len = old->hdr_len;
	new->pkt_type = old->pkt_type;
	new->ip_summed = old->ip_summed;
	memcpy(new->cb, old->cb, sizeof(new->cb));

	new->network_header = (u16)(old->network_header + off);
	new->transport_header = old->transport_header == (u16)~0U ?
	    (u16)~0U : (u16)(old->transport_header + off);
	new->mac_header = skb_mac_header_was_set(old) ?
	    (u16)(old->mac_header + off) : (u16)~0U;
}

int skb_copy_bits_frags(const struct sk_buff *skb, int offset, void *to, int len)
{
	int start = (int)skb_headlen(skb);
	struct sk_buff *frag_iter;
	int i, copy;

	if (offset < 0 || len < 0 || offset > (int)skb->len - len)
		return -EFAULT;

	copy = start - offset;
	if (copy > 0) {
		if (copy > len)
			copy = len;
		memcpy(to, skb->data + offset, copy);
		len -= copy;
		if (!len)
			return 0;
		offset += copy;
		to = (u8 *)to + copy;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		int end = start + (int)skb_frag_size(frag);

		copy = end - offset;
		if (copy > 0) {
			if (copy > len)
				copy = len;
			memcpy(to, (u8 *)skb_frag_address(frag) + (offset - start), copy);
			len -= copy;
			if (!len)
				return 0;
			offset += copy;
			to = (u8 *)to + copy;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter) {
		int end = start + (int)frag_iter->len;

		copy = end - offset;
		if (copy > 0) {
			if (copy > len)
				copy = len;
			if (skb_copy_bits_frags(frag_iter, offset - start, to, copy))
				return -EFAULT;
			len -= copy;
			if (!len)
				return 0;
			offset += copy;
			to = (u8 *)to + copy;
		}
		start = end;
	}

	return len ? -EFAULT : 0;
}

struct sk_buff *skb_copy(const struct sk_buff *skb, gfp_t gfp)
{
	unsigned int headerlen = skb_headroom(skb);
	unsigned int size = skb_end_offset(skb) + skb->data_len;
	struct sk_buff *n;

	n = __alloc_skb(size, gfp, 0, -1);
	if (!n)
		return NULL;

	skb_reserve(n, (int)headerlen);
	skb_put(n, skb->len);
	if (WARN_ON_ONCE(skb_copy_bits_frags(skb, 0, n->data, (int)skb->len))) {
		kfree_skb(n);
		return NULL;
	}
	skb_copy_header(n, skb);
	return n;
}

struct sk_buff *pskb_copy(struct sk_buff *skb, gfp_t gfp)
{
	struct sk_buff *n;
	int i;

	n = __alloc_skb(skb_end_offset(skb), gfp, 0, -1);
	if (!n)
		return NULL;

	skb_reserve(n, (int)skb_headroom(skb));
	skb_put(n, skb_headlen(skb));
	memcpy(n->data, skb->data, skb_headlen(skb));
	skb_copy_header(n, skb);

	n->truesize += skb->data_len;
	n->data_len = skb->data_len;
	n->len = skb->len;
	n->pp_recycle = skb->pp_recycle;

	if (skb_shinfo(skb)->nr_frags) {
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			skb_shinfo(n)->frags[i] = skb_shinfo(skb)->frags[i];
			skb_page_ref(skb_frag_page(&skb_shinfo(n)->frags[i]));
		}
		skb_shinfo(n)->nr_frags = (u8)i;
	}
	if (skb_has_frag_list(skb)) {
		struct sk_buff *list;

		skb_shinfo(n)->frag_list = skb_shinfo(skb)->frag_list;
		skb_walk_frags(n, list)
			skb_get(list);
	}
	return n;
}

struct sk_buff *skb_copy_expand(const struct sk_buff *skb, int newheadroom, int newtailroom, gfp_t gfp)
{
	struct sk_buff *n;

	if (newheadroom < 0 || newtailroom < 0)
		return NULL;
	n = __alloc_skb((unsigned int)newheadroom + skb->len + (unsigned int)newtailroom, gfp, 0, -1);
	if (!n)
		return NULL;

	skb_reserve(n, newheadroom);
	skb_put(n, skb->len);
	if (WARN_ON_ONCE(skb_copy_bits_frags(skb, 0, n->data, (int)skb->len))) {
		kfree_skb(n);
		return NULL;
	}
	skb_copy_header(n, skb);
	return n;
}

int pskb_expand_head(struct sk_buff *skb, int nhead, int ntail, gfp_t gfp)
{
	unsigned int osize = skb_end_offset(skb);
	unsigned int data_off, tail_off, size;
	struct skb_shared_info *shinfo;
	u8 *data;

	if (WARN_ON_ONCE(nhead < 0 || ntail < 0))
		return -EINVAL;
	if (WARN_ON_ONCE(skb_shared(skb)))
		return -EINVAL;

	size = SKB_DATA_ALIGN(osize + (unsigned int)nhead + (unsigned int)ntail);
	data = lkpi_os_malloc(size + sizeof(struct skb_shared_info), false, skb_gfp_may_sleep(gfp));
	if (!data)
		return -ENOMEM;

	data_off = skb_headroom(skb) + (unsigned int)nhead;
	tail_off = (unsigned int)(skb->tail - skb->head) + (unsigned int)nhead;

	/* Copy only the used part of the old area, then the whole shinfo. */
	memcpy(data + nhead, skb->head, (size_t)(skb->tail - skb->head));
	shinfo = (struct skb_shared_info *)(data + size);
	memcpy(shinfo, skb_shinfo(skb), sizeof(*shinfo));

	if (skb_cloned(skb)) {
		/* The other clones keep the old area; the frags and the
		 * frag_list are now referenced from both. */
		skb_ref_shared_data(skb);
		skb_release_data(skb);
	} else {
		skb_free_head(skb);
	}

	skb->head = data;
	skb->head_frag = 0;
	skb->data = data + data_off;
	skb->tail = data + tail_off;
	skb->end = data + size;

	skb->network_header = (u16)(skb->network_header + nhead);
	if (skb->transport_header != (u16)~0U)
		skb->transport_header = (u16)(skb->transport_header + nhead);
	if (skb_mac_header_was_set(skb))
		skb->mac_header = (u16)(skb->mac_header + nhead);

	skb->cloned = 0;
	skb->nohdr = 0;
	atomic_set(&skb_shinfo(skb)->dataref, 1);
	return 0;
}

struct sk_buff *skb_realloc_headroom(struct sk_buff *skb, unsigned int headroom)
{
	int delta = (int)headroom - (int)skb_headroom(skb);
	struct sk_buff *skb2;

	if (delta <= 0 && !skb_cloned(skb))
		return pskb_copy(skb, GFP_ATOMIC);

	skb2 = skb_clone(skb, GFP_ATOMIC);
	if (!skb2)
		return NULL;
	if (pskb_expand_head(skb2, delta > 0 ? (int)SKB_DATA_ALIGN((unsigned int)delta) : 0, 0,
	    GFP_ATOMIC)) {
		kfree_skb(skb2);
		return NULL;
	}
	return skb2;
}

/* ---- geometry ------------------------------------------------------- */

void *skb_put(struct sk_buff *skb, unsigned int len)
{
	void *tmp = skb->tail;

	skb->tail += len;
	skb->len += len;
	if (unlikely(skb->tail > skb->end || skb_is_nonlinear(skb))) {
		pr_emerg("skb_put: over: len %u, head %p data %p tail %p end %p (add %u)\n",
		    skb->len, skb->head, skb->data, skb->tail, skb->end, len);
		BUG();
	}
	return tmp;
}

void *skb_push(struct sk_buff *skb, unsigned int len)
{
	skb->data -= len;
	skb->len += len;
	if (unlikely(skb->data < skb->head)) {
		pr_emerg("skb_push: under: len %u, head %p data %p tail %p end %p (add %u)\n",
		    skb->len, skb->head, skb->data, skb->tail, skb->end, len);
		BUG();
	}
	return skb->data;
}

static int ___pskb_trim(struct sk_buff *skb, unsigned int len)
{
	int offset = (int)skb_headlen(skb);
	int nfrags = skb_shinfo(skb)->nr_frags;
	struct sk_buff **fragp, *frag;
	int i, err;

	if (skb_cloned(skb)) {
		err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
		if (err)
			return err;
	}

	if (offset >= (int)len) {
		i = 0;
		goto drop_pages;
	}

	for (i = 0; i < nfrags; i++) {
		int end = offset + (int)skb_frag_size(&skb_shinfo(skb)->frags[i]);

		if (end < (int)len) {
			offset = end;
			continue;
		}
		if ((int)len > offset)
			skb_frag_size_set(&skb_shinfo(skb)->frags[i++], (unsigned int)((int)len - offset));
		goto drop_pages;
	}

	/* The cut falls inside the frag_list. */
	for (fragp = &skb_shinfo(skb)->frag_list; (frag = *fragp) != NULL; fragp = &frag->next) {
		int end = offset + (int)frag->len;

		if (skb_shared(frag)) {
			struct sk_buff *nfrag = skb_clone(frag, GFP_ATOMIC);

			if (!nfrag)
				return -ENOMEM;
			nfrag->next = frag->next;
			consume_skb(frag);
			frag = nfrag;
			*fragp = frag;
		}
		if (end < (int)len) {
			offset = end;
			continue;
		}
		if (end > (int)len) {
			err = pskb_trim(frag, (unsigned int)((int)len - offset));
			if (err)
				return err;
		}
		if (frag->next) {
			kfree_skb_list(frag->next);
			frag->next = NULL;
		}
		break;
	}
	goto done;

drop_pages:
	skb_drop_frags_from(skb, i);
	if (skb_has_frag_list(skb))
		skb_drop_fraglist(skb);

done:
	if (len > skb_headlen(skb)) {
		skb->data_len -= skb->len - len;
		skb->len = len;
	} else {
		skb->len = len;
		skb->data_len = 0;
		skb_set_tail_pointer(skb, (int)len);
	}
	return 0;
}

int pskb_trim(struct sk_buff *skb, unsigned int len)
{
	return len < skb->len ? ___pskb_trim(skb, len) : 0;
}

int skb_linearize(struct sk_buff *skb)
{
	unsigned int delta, headlen, tailroom;
	int err;

	if (!skb_is_nonlinear(skb))
		return 0;

	delta = skb->data_len;
	headlen = skb_headlen(skb);
	tailroom = (unsigned int)(skb->end - skb->tail);
	if (skb_cloned(skb) || tailroom < delta) {
		err = pskb_expand_head(skb, 0, tailroom < delta ? (int)(delta - tailroom) : 0, GFP_ATOMIC);
		if (err)
			return err;
	}

	if (skb_copy_bits_frags(skb, (int)headlen, skb->tail, (int)delta))
		return -EFAULT;

	skb_drop_frags_from(skb, 0);
	if (skb_has_frag_list(skb))
		skb_drop_fraglist(skb);
	skb->tail += delta;
	skb->data_len = 0;
	return 0;
}
