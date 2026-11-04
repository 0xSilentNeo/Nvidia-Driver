// SPDX-License-Identifier: MIT
/*
 * struct page on top of lkpi_os_alloc_pages().
 *
 * A page is a descriptor (kmalloc) plus PAGE_SIZE << order bytes of
 * physically contiguous memory from the backend.  virt_to_head_page() has
 * no page table to walk, so every page-aligned sub-page of an allocation
 * is registered in a hash table keyed by (addr >> PAGE_SHIFT); interior
 * addresses of a higher-order page therefore resolve to its head.  An
 * address that was never registered (plain kmalloc memory wrapped by
 * build_skb(), for instance) resolves to NULL, which skb_free_frag() uses
 * to tell the two apart.
 */
#include <linux/kernel.h>
#include <linux/page.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/atomic.h>
#include <lkpi/runtime.h>
#include <lkpi/os.h>

#define LKPI_PAGE_HASH_BITS	10
#define LKPI_PAGE_HASH_SIZE	(1u << LKPI_PAGE_HASH_BITS)

/* One hash entry per page-aligned sub-page of an allocation. */
struct lkpi_page_tag {
	struct hlist_node node;
	struct page *page;
	unsigned long pfn;
};

/* Descriptor and its tags share one kmalloc so that `struct page *` is
 * also the allocation pointer. */
struct lkpi_page_desc {
	struct page page;
	struct lkpi_page_tag tags[];
};

static struct hlist_head lkpi_page_hash[LKPI_PAGE_HASH_SIZE];
static DEFINE_SPINLOCK(lkpi_page_lock);
static atomic_t lkpi_pages_live;

static inline unsigned int lkpi_page_hash_bucket(unsigned long pfn)
{
	return (unsigned int)((pfn * 0x9E3779B97F4A7C15ULL) >> (64 - LKPI_PAGE_HASH_BITS));
}

static inline struct lkpi_page_desc *lkpi_page_desc(struct page *page)
{
	return container_of(page, struct lkpi_page_desc, page);
}

static void lkpi_page_register(struct page *page)
{
	struct lkpi_page_desc *desc = lkpi_page_desc(page);
	unsigned long pfn = (unsigned long)page->virt >> PAGE_SHIFT;
	unsigned int i, n = 1u << page->order;
	unsigned long flags;

	spin_lock_irqsave(&lkpi_page_lock, flags);
	for (i = 0; i < n; i++) {
		struct lkpi_page_tag *tag = &desc->tags[i];

		tag->page = page;
		tag->pfn = pfn + i;
		hlist_add_head(&tag->node, &lkpi_page_hash[lkpi_page_hash_bucket(tag->pfn)]);
	}
	spin_unlock_irqrestore(&lkpi_page_lock, flags);
}

static void lkpi_page_unregister(struct page *page)
{
	struct lkpi_page_desc *desc = lkpi_page_desc(page);
	unsigned int i, n = 1u << page->order;
	unsigned long flags;

	spin_lock_irqsave(&lkpi_page_lock, flags);
	for (i = 0; i < n; i++)
		hlist_del_init(&desc->tags[i].node);
	spin_unlock_irqrestore(&lkpi_page_lock, flags);
}

struct page *alloc_pages(gfp_t gfp, unsigned int order)
{
	struct lkpi_page_desc *desc;
	struct page *page;
	size_t size;

	if (WARN_ON_ONCE(order > MAX_PAGE_ORDER))
		return NULL;

	size = PAGE_SIZE << order;
	desc = kzalloc(sizeof(*desc) + (sizeof(desc->tags[0]) << order), gfp & ~__GFP_ZERO);
	if (!desc)
		return NULL;

	page = &desc->page;
	page->virt = lkpi_os_alloc_pages(size, (gfp & __GFP_ZERO) != 0);
	if (!page->virt) {
		kfree(desc);
		return NULL;
	}
	if (WARN_ON_ONCE(!PAGE_ALIGNED(page->virt))) {
		lkpi_os_free_pages(page->virt, size);
		kfree(desc);
		return NULL;
	}

	page->order = order;
	atomic_set(&page->refcount, 1);
	atomic_set(&page->pp_ref_count, 0);
	INIT_LIST_HEAD(&page->lru);
	INIT_HLIST_NODE(&page->hash);
	lkpi_page_register(page);
	atomic_inc(&lkpi_pages_live);
	return page;
}

static void lkpi_page_release(struct page *page)
{
	WARN_ON_ONCE(page->pp != NULL);
	lkpi_page_unregister(page);
	lkpi_os_free_pages(page->virt, PAGE_SIZE << page->order);
	page->virt = NULL;
	kfree(lkpi_page_desc(page));
	atomic_dec(&lkpi_pages_live);
}

void put_page(struct page *page)
{
	if (!page)
		return;
	if (page_ref_dec_and_test(page))
		lkpi_page_release(page);
}

void __free_pages(struct page *page, unsigned int order)
{
	if (!page)
		return;
	WARN_ON_ONCE(order != page->order);
	put_page(page);
}

void free_pages(unsigned long addr, unsigned int order)
{
	struct page *page;

	if (!addr)
		return;
	page = virt_to_head_page((const void *)addr);
	if (WARN_ON_ONCE(!page))
		return;
	__free_pages(page, order);
}

struct page *virt_to_head_page(const void *addr)
{
	unsigned long pfn = (unsigned long)addr >> PAGE_SHIFT;
	struct lkpi_page_tag *tag;
	struct page *page = NULL;
	unsigned long flags;

	if (!addr)
		return NULL;

	spin_lock_irqsave(&lkpi_page_lock, flags);
	hlist_for_each_entry(tag, &lkpi_page_hash[lkpi_page_hash_bucket(pfn)], node) {
		if (tag->pfn == pfn) {
			page = tag->page;
			break;
		}
	}
	spin_unlock_irqrestore(&lkpi_page_lock, flags);
	return page;
}

int lkpi_page_init(void)
{
	unsigned int i;

	for (i = 0; i < LKPI_PAGE_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&lkpi_page_hash[i]);
	spin_lock_init(&lkpi_page_lock);
	atomic_set(&lkpi_pages_live, 0);
	return 0;
}

void lkpi_page_fini(void)
{
	int live = atomic_read(&lkpi_pages_live);

	if (live)
		pr_warn("lkpi: %d page(s) still allocated at shutdown\n", live);
	spin_lock_destroy(&lkpi_page_lock);
}
