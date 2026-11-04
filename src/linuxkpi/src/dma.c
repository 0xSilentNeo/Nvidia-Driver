// SPDX-License-Identifier: MIT
/*
 * DMA API on top of lkpi/os.h.
 *
 * Coherent buffers and streaming mappings are created by the IOKit backend
 * (IOBufferMemoryDescriptor / IODMACommand) against the device's
 * lkpi_os_pci_t handle.  The Linux direction enum is translated explicitly
 * to the LKPI_DMA_* values so the two headers may drift independently.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <lkpi/os.h>

static int lkpi_dma_dir(enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		return LKPI_DMA_TO_DEVICE;
	case DMA_FROM_DEVICE:
		return LKPI_DMA_FROM_DEVICE;
	case DMA_NONE:
		return LKPI_DMA_NONE;
	case DMA_BIDIRECTIONAL:
	default:
		return LKPI_DMA_BIDIRECTIONAL;
	}
}

static u64 lkpi_coherent_mask(const struct device *dev)
{
	return dev->coherent_dma_mask ? dev->coherent_dma_mask : DMA_BIT_MASK(32);
}

void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t gfp)
{
	lkpi_os_dma_addr_t bus = 0;
	void *va;

	(void)gfp;	/* coherent memory is always zeroed, __GFP_ZERO or not */
	if (!dev || !dev->pci || !size) {
		if (dma_handle)
			*dma_handle = DMA_MAPPING_ERROR;
		return NULL;
	}
	va = lkpi_os_dma_alloc_coherent(dev->pci, size, lkpi_coherent_mask(dev), &bus);
	if (!va || bus == 0) {
		if (va)
			lkpi_os_dma_free_coherent(dev->pci, size, va, bus);
		if (dma_handle)
			*dma_handle = DMA_MAPPING_ERROR;
		return NULL;
	}
	memset(va, 0, size);
	*dma_handle = bus;
	return va;
}

void dma_free_coherent(struct device *dev, size_t size, void *va, dma_addr_t dma_handle)
{
	if (!dev || !dev->pci || !va)
		return;
	lkpi_os_dma_free_coherent(dev->pci, size, va, dma_handle);
}

struct lkpi_dmam_coherent {
	size_t size;
	void *va;
	dma_addr_t dma;
};

static void lkpi_dmam_release(struct device *dev, void *res)
{
	struct lkpi_dmam_coherent *r = res;

	dma_free_coherent(dev, r->size, r->va, r->dma);
}

void *dmam_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t gfp)
{
	struct lkpi_dmam_coherent *r;
	void *va;

	r = devres_alloc(lkpi_dmam_release, sizeof(*r), gfp);
	if (!r)
		return NULL;
	va = dma_alloc_coherent(dev, size, dma_handle, gfp);
	if (!va) {
		devres_free(r);
		return NULL;
	}
	r->size = size;
	r->va = va;
	r->dma = *dma_handle;
	devres_add(dev, r);
	return va;
}

void dmam_free_coherent(struct device *dev, size_t size, void *va, dma_addr_t dma_handle)
{
	struct lkpi_dmam_coherent key = { .size = size, .va = va, .dma = dma_handle };

	/*
	 * devres_destroy() (device.c) falls back to matching the record by
	 * content, so a copy with the same fields unlinks it without running
	 * the release; then free explicitly, as Linux does.
	 */
	devres_destroy(dev, lkpi_dmam_release, &key);
	dma_free_coherent(dev, size, va, dma_handle);
}

dma_addr_t dma_map_single_attrs(struct device *dev, void *ptr, size_t size,
    enum dma_data_direction dir, unsigned long attrs)
{
	(void)attrs;
	if (!dev || !dev->pci || !ptr || !size)
		return DMA_MAPPING_ERROR;
	return lkpi_os_dma_map(dev->pci, ptr, size, lkpi_dma_dir(dir), lkpi_dev_dma_mask(dev));
}

void dma_unmap_single_attrs(struct device *dev, dma_addr_t addr, size_t size,
    enum dma_data_direction dir, unsigned long attrs)
{
	(void)attrs;
	if (!dev || !dev->pci || addr == DMA_MAPPING_ERROR)
		return;
	lkpi_os_dma_unmap(dev->pci, addr, size, lkpi_dma_dir(dir));
}

void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size,
    enum dma_data_direction dir)
{
	if (!dev || !dev->pci || addr == DMA_MAPPING_ERROR)
		return;
	lkpi_os_dma_sync(dev->pci, addr, size, lkpi_dma_dir(dir), true);
}

void dma_sync_single_for_device(struct device *dev, dma_addr_t addr, size_t size,
    enum dma_data_direction dir)
{
	if (!dev || !dev->pci || addr == DMA_MAPPING_ERROR)
		return;
	lkpi_os_dma_sync(dev->pci, addr, size, lkpi_dma_dir(dir), false);
}
