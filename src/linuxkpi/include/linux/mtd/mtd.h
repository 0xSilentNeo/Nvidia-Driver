/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_MTD_MTD_H_
#define _LKPI_LINUX_MTD_MTD_H_
#include <linux/types.h>
#include <linux/err.h>
struct mtd_info { int dummy; };
static inline struct mtd_info *get_mtd_device_nm(const char *name) { (void)name; return ERR_PTR(-ENODEV); }
static inline void put_mtd_device(struct mtd_info *mtd) { (void)mtd; }
static inline int mtd_read(struct mtd_info *mtd, u64 from, size_t len, size_t *retlen, u8 *buf)
{
	(void)mtd; (void)from; (void)len; (void)buf;
	if (retlen) *retlen = 0;
	return -ENODEV;
}
#define EUCLEAN_LKPI 117
static inline int mtd_is_bitflip(int err) { return err == -EUCLEAN_LKPI; }
#endif
