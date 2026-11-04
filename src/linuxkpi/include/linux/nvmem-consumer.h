/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_NVMEM_CONSUMER_H_
#define _LKPI_LINUX_NVMEM_CONSUMER_H_
#include <linux/types.h>
#include <linux/err.h>
struct nvmem_cell;
struct device_node;
static inline struct nvmem_cell *of_nvmem_cell_get(struct device_node *np, const char *id) { (void)np; (void)id; return ERR_PTR(-ENOENT); }
static inline struct nvmem_cell *nvmem_cell_get(struct device *dev, const char *id) { (void)dev; (void)id; return ERR_PTR(-ENOENT); }
static inline void nvmem_cell_put(struct nvmem_cell *cell) { (void)cell; }
static inline void *nvmem_cell_read(struct nvmem_cell *cell, size_t *len) { (void)cell; if (len) *len = 0; return ERR_PTR(-ENOENT); }
#endif
