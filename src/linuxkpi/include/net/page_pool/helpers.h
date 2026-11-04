/* SPDX-License-Identifier: MIT */
/*
 * page_pool: recycling allocator for RX buffers.  mt76 maps the pool's
 * pages for DMA (PP_FLAG_DMA_MAP) and hands fragments to the hardware.
 * Implementation: src/linuxkpi/src/page_pool.c.
 */
#ifndef _LKPI_NET_PAGE_POOL_HELPERS_H_
#define _LKPI_NET_PAGE_POOL_HELPERS_H_
#include <linux/kernel.h>
#include <linux/page.h>
#include <linux/dma-mapping.h>
#include <net/page_pool/types.h>

struct page_pool *page_pool_create(const struct page_pool_params *params);
void page_pool_destroy(struct page_pool *pool);
struct page *page_pool_alloc_pages(struct page_pool *pool, gfp_t gfp);
struct page *page_pool_alloc_frag(struct page_pool *pool, unsigned int *offset, unsigned int size, gfp_t gfp);
static inline struct page *page_pool_dev_alloc_pages(struct page_pool *pool)
{
	return page_pool_alloc_pages(pool, GFP_ATOMIC | __GFP_NOWARN);
}
static inline struct page *page_pool_dev_alloc_frag(struct page_pool *pool, unsigned int *offset,
    unsigned int size)
{
	return page_pool_alloc_frag(pool, offset, size, GFP_ATOMIC | __GFP_NOWARN);
}
void page_pool_put_unrefed_page(struct page_pool *pool, struct page *page, unsigned int dma_sync_size,
    bool allow_direct);
static inline void page_pool_put_page(struct page_pool *pool, struct page *page,
    unsigned int dma_sync_size, bool allow_direct)
{
	page_pool_put_unrefed_page(pool, page, dma_sync_size, allow_direct);
}
static inline void page_pool_put_full_page(struct page_pool *pool, struct page *page, bool allow_direct)
{
	page_pool_put_page(pool, page, (unsigned int)-1, allow_direct);
}
static inline void page_pool_recycle_direct(struct page_pool *pool, struct page *page)
{
	page_pool_put_full_page(pool, page, true);
}
static inline dma_addr_t page_pool_get_dma_addr(const struct page *page) { return page->dma_addr; }
static inline void page_pool_set_dma_addr(struct page *page, dma_addr_t addr) { page->dma_addr = addr; }
static inline enum dma_data_direction page_pool_get_dma_dir(const struct page_pool *pool)
{
	return pool->p.dma_dir;
}
static inline void page_pool_fragment_page(struct page *page, long nr)
{
	atomic_set(&page->pp_ref_count, (int)nr);
}
static inline bool page_pool_is_last_ref(struct page *page)
{
	return atomic_dec_and_test(&page->pp_ref_count);
}
static inline void page_pool_ref_page(struct page *page) { atomic_inc(&page->pp_ref_count); }
static inline bool page_pool_page_is_pp(struct page *page) { return (page->pp_magic & ~PP_MAGIC_MASK) == PP_SIGNATURE; }
#define page_pool_use_xdp_mem(p, d, m) do { } while (0)

/* ethtool stats glue */
int page_pool_ethtool_stats_get_count(void);
u8 *page_pool_ethtool_stats_get_strings(u8 *data);
u64 *page_pool_ethtool_stats_get(u64 *data, const void *stats);
bool page_pool_get_stats(const struct page_pool *pool, struct page_pool_stats *stats);
#endif
