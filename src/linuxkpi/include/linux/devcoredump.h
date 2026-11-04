/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_DEVCOREDUMP_H_
#define _LKPI_LINUX_DEVCOREDUMP_H_
#include <linux/types.h>
#include <linux/slab.h>
struct device;
/* Firmware coredumps are logged and discarded; the buffer is owned by us. */
void dev_coredumpv(struct device *dev, void *data, size_t datalen, gfp_t gfp);
static inline void dev_coredumpm(struct device *dev, struct module *owner, void *data, size_t datalen,
    gfp_t gfp, ssize_t (*read)(char *, loff_t, size_t, void *, size_t), void (*free)(void *))
{
	(void)dev; (void)owner; (void)datalen; (void)gfp; (void)read;
	free(data);
}
#endif
