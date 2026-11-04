/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_NET_PAGE_POOL_TYPES_H_
#define _LKPI_NET_PAGE_POOL_TYPES_H_
#include <linux/types.h>
#include <linux/dma-direction.h>
#include <linux/spinlock.h>
#include <linux/list.h>

#define PP_FLAG_DMA_MAP		BIT(0)
#define PP_FLAG_DMA_SYNC_DEV	BIT(1)
#define PP_FLAG_PAGE_FRAG	BIT(2)
#define PP_FLAG_SYSTEM_POOL	BIT(3)
#define PP_FLAG_ALL		(PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV | PP_FLAG_PAGE_FRAG | PP_FLAG_SYSTEM_POOL)
struct device;
struct napi_struct;

struct page_pool_params {
	unsigned int flags;
	unsigned int order;
	unsigned int pool_size;
	int nid;
	struct device *dev;
	struct napi_struct *napi;
	enum dma_data_direction dma_dir;
	unsigned int max_len;
	unsigned int offset;
	void (*init_callback)(struct page *page, void *arg);
	void *init_arg;
	unsigned int queue_idx;
};

struct page_pool_stats {
	struct {
		u64 fast, slow, slow_high_order, empty, refill, waive;
	} alloc_stats;
	struct {
		u64 cached, cache_full, ring, ring_full, released_refcnt;
	} recycle_stats;
};

struct page_pool {
	struct page_pool_params p;
	spinlock_t lock;
	struct list_head free;	/* cached pages */
	unsigned int nfree;
	struct page *frag_page;	/* current page handed out as fragments */
	unsigned int frag_offset;
	long frag_users;
	atomic_t pages_outstanding;
	struct page_pool_stats stats;
	bool destroyed;
};
#endif
