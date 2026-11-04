// SPDX-License-Identifier: MIT
/*
 * page_pool: recycling allocator for RX buffers.
 *
 * Pages come from alloc_pages() and, when PP_FLAG_DMA_MAP is set, stay
 * mapped for DMA for as long as the pool owns them.  A page handed out by
 * page_pool_alloc_pages() carries one pp_ref_count; page_pool_alloc_frag()
 * slices the current page into `size`-byte fragments, each holding one
 * pp_ref_count, while the pool itself keeps one reference until the page
 * is exhausted.  page_pool_put_unrefed_page() drops a reference and, when
 * the last one goes, parks the page on the free list (up to pool_size
 * entries, DMA-synced for the device) or unmaps and frees it.
 *
 * The pool holds exactly one struct page refcount on every page it owns.
 * A page whose refcount is higher at recycle time escaped through
 * get_page(); it is released from the pool and freed by the other holder.
 *
 * Linux defers page_pool_destroy() while pages are in flight.  Here the
 * cached pages are released immediately, the pool is marked destroyed and
 * the struct is freed by whichever call returns the last outstanding page
 * (pages_outstanding counts pages in flight, including the current
 * fragment page).  All state is protected by pool->lock; callers run from
 * NAPI poll threads and from the irq tasklet, never in primary interrupt
 * context, so the irqsave variants are plenty.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/cache.h>
#include <linux/page.h>
#include <linux/dma-mapping.h>
#include <linux/ethtool.h>
#include <net/page_pool/types.h>
#include <net/page_pool/helpers.h>

#define LKPI_PP_DEFAULT_POOL_SIZE	1024
#define LKPI_PP_MAX_POOL_SIZE		32768

static inline unsigned int page_pool_page_size(const struct page_pool *pool)
{
	return PAGE_SIZE << pool->p.order;
}

static void page_pool_dma_sync_for_device(const struct page_pool *pool, struct page *page,
    unsigned int dma_sync_size)
{
	dma_addr_t dma = page_pool_get_dma_addr(page);

	dma_sync_size = min(dma_sync_size, pool->p.max_len);
	dma_sync_single_for_device(pool->p.dev, dma + pool->p.offset, dma_sync_size, pool->p.dma_dir);
}

/* Map a freshly allocated page.  Returns false when the mapping failed. */
static bool page_pool_dma_map(struct page_pool *pool, struct page *page)
{
	dma_addr_t dma;

	dma = dma_map_page(pool->p.dev, page, 0, page_pool_page_size(pool), pool->p.dma_dir);
	if (dma_mapping_error(pool->p.dev, dma))
		return false;
	page_pool_set_dma_addr(page, dma);
	if (pool->p.flags & PP_FLAG_DMA_SYNC_DEV)
		page_pool_dma_sync_for_device(pool, page, pool->p.max_len);
	return true;
}

/* Give a page back to the system: unmap it and drop the pool's refcount. */
static void page_pool_release_page(struct page_pool *pool, struct page *page)
{
	if (pool->p.flags & PP_FLAG_DMA_MAP) {
		dma_unmap_page(pool->p.dev, page_pool_get_dma_addr(page), page_pool_page_size(pool),
		    pool->p.dma_dir);
		page_pool_set_dma_addr(page, 0);
	}
	page->pp = NULL;
	page->pp_magic = 0;
	put_page(page);
}

struct page_pool *page_pool_create(const struct page_pool_params *params)
{
	struct page_pool *pool;

	if (!params)
		return ERR_PTR(-EINVAL);
	if (params->flags & ~PP_FLAG_ALL)
		return ERR_PTR(-EINVAL);
	if (params->order > MAX_PAGE_ORDER)
		return ERR_PTR(-EINVAL);
	if (params->pool_size > LKPI_PP_MAX_POOL_SIZE)
		return ERR_PTR(-E2BIG);
	if (params->flags & PP_FLAG_DMA_MAP) {
		if (!params->dev)
			return ERR_PTR(-EINVAL);
		if (params->dma_dir != DMA_FROM_DEVICE && params->dma_dir != DMA_BIDIRECTIONAL)
			return ERR_PTR(-EINVAL);
	}
	if (params->flags & PP_FLAG_DMA_SYNC_DEV) {
		if (!(params->flags & PP_FLAG_DMA_MAP))
			return ERR_PTR(-EINVAL);
		if (!params->max_len)
			return ERR_PTR(-EINVAL);
		if (params->offset + params->max_len > (PAGE_SIZE << params->order))
			return ERR_PTR(-EINVAL);
	}

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	pool->p = *params;
	if (!pool->p.pool_size)
		pool->p.pool_size = LKPI_PP_DEFAULT_POOL_SIZE;
	spin_lock_init(&pool->lock);
	INIT_LIST_HEAD(&pool->free);
	pool->nfree = 0;
	pool->frag_page = NULL;
	pool->frag_offset = 0;
	pool->frag_users = 0;
	atomic_set(&pool->pages_outstanding, 0);
	pool->destroyed = false;
	return pool;
}

/* Take a page from the cache; NULL when it is empty.  Called with the lock held. */
static struct page *page_pool_take_cached(struct page_pool *pool)
{
	struct page *page;

	if (list_empty(&pool->free))
		return NULL;
	page = list_first_entry(&pool->free, struct page, lru);
	list_del_init(&page->lru);
	pool->nfree--;
	return page;
}

struct page *page_pool_alloc_pages(struct page_pool *pool, gfp_t gfp)
{
	struct page *page;
	unsigned long flags;

	if (WARN_ON_ONCE(!pool))
		return NULL;

	spin_lock_irqsave(&pool->lock, flags);
	if (unlikely(pool->destroyed)) {
		spin_unlock_irqrestore(&pool->lock, flags);
		WARN_ON_ONCE(1);
		return NULL;
	}
	page = page_pool_take_cached(pool);
	if (page) {
		pool->stats.alloc_stats.fast++;
		atomic_inc(&pool->pages_outstanding);
		spin_unlock_irqrestore(&pool->lock, flags);
		page_pool_fragment_page(page, 1);
		return page;
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	/* Slow path: fresh page.  No lock held, the mapping may take a while. */
	page = alloc_pages(gfp, pool->p.order);
	if (!page) {
		spin_lock_irqsave(&pool->lock, flags);
		pool->stats.alloc_stats.empty++;
		spin_unlock_irqrestore(&pool->lock, flags);
		return NULL;
	}
	page->pp = pool;
	page->pp_magic = PP_SIGNATURE;
	if ((pool->p.flags & PP_FLAG_DMA_MAP) && !page_pool_dma_map(pool, page)) {
		page->pp = NULL;
		page->pp_magic = 0;
		__free_pages(page, pool->p.order);
		spin_lock_irqsave(&pool->lock, flags);
		pool->stats.alloc_stats.empty++;
		spin_unlock_irqrestore(&pool->lock, flags);
		return NULL;
	}
	page_pool_fragment_page(page, 1);
	if (pool->p.init_callback)
		pool->p.init_callback(page, pool->p.init_arg);

	spin_lock_irqsave(&pool->lock, flags);
	pool->stats.alloc_stats.slow++;
	if (pool->p.order)
		pool->stats.alloc_stats.slow_high_order++;
	atomic_inc(&pool->pages_outstanding);
	spin_unlock_irqrestore(&pool->lock, flags);
	return page;
}

struct page *page_pool_alloc_frag(struct page_pool *pool, unsigned int *offset, unsigned int size,
    gfp_t gfp)
{
	unsigned int max_size;
	struct page *page, *drain = NULL;
	unsigned long flags;

	if (WARN_ON_ONCE(!pool || !offset))
		return NULL;

	max_size = page_pool_page_size(pool);
	size = ALIGN(size, SMP_CACHE_BYTES);
	if (WARN_ON_ONCE(size > max_size))
		return NULL;

	/* A fragment as large as the page is just the page. */
	if (size == max_size) {
		*offset = 0;
		return page_pool_alloc_pages(pool, gfp);
	}

	spin_lock_irqsave(&pool->lock, flags);
	page = pool->frag_page;
	if (page && pool->frag_offset + size > max_size) {
		/* Exhausted: the pool drops its own reference below. */
		drain = page;
		pool->frag_page = NULL;
		pool->frag_offset = 0;
		pool->frag_users = 0;
		page = NULL;
	}
	if (page) {
		*offset = pool->frag_offset;
		pool->frag_offset += size;
		pool->frag_users++;
		page_pool_ref_page(page);
		spin_unlock_irqrestore(&pool->lock, flags);
		return page;
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	if (drain)
		page_pool_put_unrefed_page(pool, drain, (unsigned int)-1, false);

	page = page_pool_alloc_pages(pool, gfp);
	if (!page)
		return NULL;

	spin_lock_irqsave(&pool->lock, flags);
	if (unlikely(pool->frag_page) || unlikely(pool->destroyed)) {
		/* Raced with another allocator (or destroy): hand the whole
		 * page to the caller instead of fragmenting it. */
		spin_unlock_irqrestore(&pool->lock, flags);
		*offset = 0;
		return page;
	}
	/* The pool keeps the reference page_pool_alloc_pages() gave it and
	 * the caller gets a second one for this fragment. */
	pool->frag_page = page;
	pool->frag_offset = size;
	pool->frag_users = 1;
	*offset = 0;
	page_pool_ref_page(page);
	spin_unlock_irqrestore(&pool->lock, flags);
	return page;
}

void page_pool_put_unrefed_page(struct page_pool *pool, struct page *page, unsigned int dma_sync_size,
    bool allow_direct)
{
	unsigned long flags;
	bool free_pool;

	if (WARN_ON_ONCE(!page))
		return;
	if (WARN_ON_ONCE(!pool))
		pool = page->pp;
	if (WARN_ON_ONCE(!pool || page->pp != pool))
		return;

	if (!page_pool_is_last_ref(page))
		return;

	spin_lock_irqsave(&pool->lock, flags);
	if (page_ref_count(page) != 1) {
		/* Somebody else still holds the page (get_page()); it cannot
		 * be recycled.  Release the pool's reference. */
		pool->stats.recycle_stats.released_refcnt++;
	} else if (!pool->destroyed && pool->nfree < pool->p.pool_size) {
		if (pool->p.flags & PP_FLAG_DMA_SYNC_DEV)
			page_pool_dma_sync_for_device(pool, page, dma_sync_size);
		list_add(&page->lru, &pool->free);
		pool->nfree++;
		pool->stats.recycle_stats.cached++;
		atomic_dec(&pool->pages_outstanding);
		spin_unlock_irqrestore(&pool->lock, flags);
		return;
	} else if (pool->destroyed) {
		pool->stats.recycle_stats.ring++;
	} else {
		pool->stats.recycle_stats.ring_full++;
	}
	free_pool = atomic_dec_return(&pool->pages_outstanding) == 0 && pool->destroyed;
	spin_unlock_irqrestore(&pool->lock, flags);

	page_pool_release_page(pool, page);
	if (free_pool) {
		spin_lock_destroy(&pool->lock);
		kfree(pool);
	}
}

void page_pool_destroy(struct page_pool *pool)
{
	struct page *page, *tmp, *frag;
	unsigned long flags;
	LIST_HEAD(cached);
	bool free_pool;

	if (!pool)
		return;

	spin_lock_irqsave(&pool->lock, flags);
	if (WARN_ON_ONCE(pool->destroyed)) {
		spin_unlock_irqrestore(&pool->lock, flags);
		return;
	}
	pool->destroyed = true;
	list_splice_init(&pool->free, &cached);
	pool->nfree = 0;
	frag = pool->frag_page;
	pool->frag_page = NULL;
	pool->frag_offset = 0;
	pool->frag_users = 0;
	/* With a fragment page the pool itself still holds a reference, so
	 * pages_outstanding cannot reach zero before the put below. */
	free_pool = !frag && atomic_read(&pool->pages_outstanding) == 0;
	spin_unlock_irqrestore(&pool->lock, flags);

	list_for_each_entry_safe(page, tmp, &cached, lru) {
		list_del_init(&page->lru);
		page_pool_release_page(pool, page);
	}

	if (frag) {
		page_pool_put_unrefed_page(pool, frag, (unsigned int)-1, false);
		return;
	}
	if (free_pool) {
		spin_lock_destroy(&pool->lock);
		kfree(pool);
	} else {
		pr_debug("page_pool %p: %d page(s) in flight, deferring free\n", pool,
		    atomic_read(&pool->pages_outstanding));
	}
}

/* ---- stats -------------------------------------------------------- */

static const char lkpi_pp_stats_strings[][ETH_GSTRING_LEN] = {
	"rx_pp_alloc_fast",
	"rx_pp_alloc_slow",
	"rx_pp_alloc_slow_ho",
	"rx_pp_alloc_empty",
	"rx_pp_alloc_refill",
	"rx_pp_alloc_waive",
	"rx_pp_recycle_cached",
	"rx_pp_recycle_cache_full",
	"rx_pp_recycle_ring",
	"rx_pp_recycle_ring_full",
	"rx_pp_recycle_released_ref",
};

int page_pool_ethtool_stats_get_count(void)
{
	return ARRAY_SIZE(lkpi_pp_stats_strings);
}

u8 *page_pool_ethtool_stats_get_strings(u8 *data)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(lkpi_pp_stats_strings); i++) {
		memcpy(data, lkpi_pp_stats_strings[i], ETH_GSTRING_LEN);
		data += ETH_GSTRING_LEN;
	}
	return data;
}

u64 *page_pool_ethtool_stats_get(u64 *data, const void *stats)
{
	const struct page_pool_stats *s = stats;

	*data++ = s->alloc_stats.fast;
	*data++ = s->alloc_stats.slow;
	*data++ = s->alloc_stats.slow_high_order;
	*data++ = s->alloc_stats.empty;
	*data++ = s->alloc_stats.refill;
	*data++ = s->alloc_stats.waive;
	*data++ = s->recycle_stats.cached;
	*data++ = s->recycle_stats.cache_full;
	*data++ = s->recycle_stats.ring;
	*data++ = s->recycle_stats.ring_full;
	*data++ = s->recycle_stats.released_refcnt;
	return data;
}

bool page_pool_get_stats(const struct page_pool *pool, struct page_pool_stats *stats)
{
	struct page_pool *p = (struct page_pool *)pool;
	unsigned long flags;

	if (!pool || !stats)
		return false;

	spin_lock_irqsave(&p->lock, flags);
	stats->alloc_stats.fast += pool->stats.alloc_stats.fast;
	stats->alloc_stats.slow += pool->stats.alloc_stats.slow;
	stats->alloc_stats.slow_high_order += pool->stats.alloc_stats.slow_high_order;
	stats->alloc_stats.empty += pool->stats.alloc_stats.empty;
	stats->alloc_stats.refill += pool->stats.alloc_stats.refill;
	stats->alloc_stats.waive += pool->stats.alloc_stats.waive;
	stats->recycle_stats.cached += pool->stats.recycle_stats.cached;
	stats->recycle_stats.cache_full += pool->stats.recycle_stats.cache_full;
	stats->recycle_stats.ring += pool->stats.recycle_stats.ring;
	stats->recycle_stats.ring_full += pool->stats.recycle_stats.ring_full;
	stats->recycle_stats.released_refcnt += pool->stats.recycle_stats.released_refcnt;
	spin_unlock_irqrestore(&p->lock, flags);
	return true;
}
