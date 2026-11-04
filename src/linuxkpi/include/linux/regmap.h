/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_REGMAP_H_
#define _LKPI_LINUX_REGMAP_H_
#include <linux/types.h>
#include <linux/errno.h>
struct regmap;
static inline int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val) { (void)map; (void)reg; (void)val; return -ENODEV; }
static inline int regmap_write(struct regmap *map, unsigned int reg, unsigned int val) { (void)map; (void)reg; (void)val; return -ENODEV; }
static inline int regmap_update_bits(struct regmap *map, unsigned int reg, unsigned int mask, unsigned int val) { (void)map; (void)reg; (void)mask; (void)val; return -ENODEV; }
#endif
