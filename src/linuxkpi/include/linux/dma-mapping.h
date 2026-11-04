/* SPDX-License-Identifier: MIT */
/*
 * DMA API.  Coherent allocations and streaming maps are performed by the
 * IOKit backend (IOBufferMemoryDescriptor / IODMACommand) through lkpi/os.h.
 * A bus address of 0 is the error marker (never produced by a real mapping).
 */
#ifndef _LKPI_LINUX_DMA_MAPPING_H_
#define _LKPI_LINUX_DMA_MAPPING_H_
#include <linux/types.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/err.h>
#include <linux/page.h>
#include <lkpi/os.h>

#define DMA_MAPPING_ERROR	((dma_addr_t)0)
#define DMA_BIT_MASK(n)		(((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))
#define DMA_ATTR_SKIP_CPU_SYNC	(1UL << 5)
#define DMA_ATTR_NO_WARN	(1UL << 8)
#define DMA_ATTR_WEAK_ORDERING	(1UL << 1)

static inline u64 lkpi_dev_dma_mask(struct device *dev)
{
	return dev->dma_mask && *dev->dma_mask ? *dev->dma_mask : DMA_BIT_MASK(32);
}
static inline int dma_set_mask(struct device *dev, u64 mask)
{
	dev->dma_mask_value = mask;
	dev->dma_mask = &dev->dma_mask_value;
	return 0;
}
static inline int dma_set_coherent_mask(struct device *dev, u64 mask) { dev->coherent_dma_mask = mask; return 0; }
static inline int dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
	int rc = dma_set_mask(dev, mask);
	if (rc == 0)
		dma_set_coherent_mask(dev, mask);
	return rc;
}
static inline u64 dma_get_mask(struct device *dev) { return lkpi_dev_dma_mask(dev); }
static inline int dma_supported(struct device *dev, u64 mask) { (void)dev; (void)mask; return 1; }
static inline int dma_set_max_seg_size(struct device *dev, unsigned int size) { (void)dev; (void)size; return 0; }
static inline unsigned int dma_get_max_seg_size(struct device *dev) { (void)dev; return 65536; }

static inline int dma_mapping_error(struct device *dev, dma_addr_t addr)
{
	(void)dev;
	return addr == DMA_MAPPING_ERROR ? -ENOMEM : 0;
}

void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t gfp);
void dma_free_coherent(struct device *dev, size_t size, void *va, dma_addr_t dma_handle);
static inline void *dma_zalloc_coherent(struct device *dev, size_t size, dma_addr_t *h, gfp_t gfp)
{
	return dma_alloc_coherent(dev, size, h, gfp | __GFP_ZERO);
}
#define dma_alloc_attrs(dev, size, h, gfp, attrs) dma_alloc_coherent(dev, size, h, gfp)
#define dma_free_attrs(dev, size, va, h, attrs) dma_free_coherent(dev, size, va, h)
void *dmam_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t gfp);
void dmam_free_coherent(struct device *dev, size_t size, void *va, dma_addr_t dma_handle);

dma_addr_t dma_map_single_attrs(struct device *dev, void *ptr, size_t size,
    enum dma_data_direction dir, unsigned long attrs);
void dma_unmap_single_attrs(struct device *dev, dma_addr_t addr, size_t size,
    enum dma_data_direction dir, unsigned long attrs);
#define dma_map_single(d, p, s, dir) dma_map_single_attrs(d, p, s, dir, 0)
#define dma_unmap_single(d, a, s, dir) dma_unmap_single_attrs(d, a, s, dir, 0)
static inline dma_addr_t dma_map_page_attrs(struct device *dev, struct page *page, size_t offset,
    size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	return dma_map_single_attrs(dev, (u8 *)page_address(page) + offset, size, dir, attrs);
}
#define dma_map_page(d, p, o, s, dir) dma_map_page_attrs(d, p, o, s, dir, 0)
#define dma_unmap_page_attrs(d, a, s, dir, attrs) dma_unmap_single_attrs(d, a, s, dir, attrs)
#define dma_unmap_page(d, a, s, dir) dma_unmap_single_attrs(d, a, s, dir, 0)
void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size, enum dma_data_direction dir);
void dma_sync_single_for_device(struct device *dev, dma_addr_t addr, size_t size, enum dma_data_direction dir);
#define dma_sync_single_range_for_cpu(d, a, off, s, dir) dma_sync_single_for_cpu(d, (a) + (off), s, dir)
#define dma_sync_single_range_for_device(d, a, off, s, dir) dma_sync_single_for_device(d, (a) + (off), s, dir)
static inline bool dma_need_sync(struct device *dev, dma_addr_t addr) { (void)dev; (void)addr; return false; }

/* scatterlists are not used by the mt76 PCIe path */
struct scatterlist;
#endif /* _LKPI_LINUX_DMA_MAPPING_H_ */
