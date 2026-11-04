/* SPDX-License-Identifier: MIT */
/*
 * struct page.  Pages here are plain kernel allocations of PAGE_SIZE << order
 * bytes with a small descriptor; the only consumers are the mt76 RX page
 * pool and skb fragments.  virt_to_head_page() finds the descriptor via a
 * hash of the page-aligned address (src/linuxkpi/src/page.c).
 */
#ifndef _LKPI_LINUX_PAGE_H_
#define _LKPI_LINUX_PAGE_H_
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/gfp.h>

#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE - 1))
#define PAGE_ALIGN(x)	(((x) + PAGE_SIZE - 1) & PAGE_MASK)
#define PAGE_ALIGNED(x)	(((unsigned long)(x) & ~PAGE_MASK) == 0)
#define offset_in_page(p) ((unsigned long)(p) & ~PAGE_MASK)
#define PAGE_FRAG_CACHE_MAX_SIZE 32768
#define MAX_ORDER 11
#define MAX_PAGE_ORDER 10

struct page_pool;
struct page {
	void *virt;
	unsigned int order;
	atomic_t refcount;
	unsigned long pp_magic;
	struct page_pool *pp;		/* owning pool when recycled (mt76 uses page->pp) */
	unsigned long dma_addr;
	unsigned int pp_frag_offset;
	atomic_t pp_ref_count;
	struct list_head lru;		/* pool free list */
	struct hlist_node hash;		/* virt -> page lookup */
};
#define PP_MAGIC_MASK 0x3UL
#define PP_SIGNATURE 0x40UL

struct page *alloc_pages(gfp_t gfp, unsigned int order);
#define alloc_page(gfp) alloc_pages(gfp, 0)
#define alloc_pages_node(nid, gfp, order) alloc_pages(gfp, order)
#define __get_free_pages(gfp, order) ((unsigned long)page_address(alloc_pages(gfp, order)))
#define __get_free_page(gfp) __get_free_pages(gfp, 0)
#define get_zeroed_page(gfp) __get_free_pages((gfp) | __GFP_ZERO, 0)
void __free_pages(struct page *page, unsigned int order);
#define __free_page(p) __free_pages(p, 0)
void free_pages(unsigned long addr, unsigned int order);
#define free_page(addr) free_pages(addr, 0)
static inline void *page_address(const struct page *page) { return page->virt; }
#define page_to_virt(p) page_address(p)
struct page *virt_to_head_page(const void *addr);
#define virt_to_page(addr) virt_to_head_page(addr)
static inline struct page *compound_head(struct page *page) { return page; }
static inline void get_page(struct page *page) { atomic_inc(&page->refcount); }
static inline int page_ref_count(struct page *page) { return atomic_read(&page->refcount); }
static inline void page_ref_inc(struct page *page) { atomic_inc(&page->refcount); }
static inline int page_ref_dec_and_test(struct page *page) { return atomic_dec_and_test(&page->refcount); }
static inline void page_ref_add(struct page *page, int n) { atomic_add(n, &page->refcount); }
static inline int page_ref_sub_and_test(struct page *page, int n) { return atomic_sub_and_test(n, &page->refcount); }
void put_page(struct page *page);
static inline unsigned int compound_order(struct page *page) { return page->order; }
static inline unsigned int page_size(struct page *page) { return PAGE_SIZE << page->order; }
static inline bool page_is_pfmemalloc(const struct page *page) { (void)page; return false; }
static inline unsigned long page_to_pfn(struct page *page) { return (unsigned long)page->virt >> PAGE_SHIFT; }
#define nth_page(page, n) (page)
#define page_frag_alloc(nc, sz, gfp) kmalloc(sz, gfp)
#define page_frag_free(p) kfree(p)
#endif /* _LKPI_LINUX_PAGE_H_ */
