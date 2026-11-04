/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_HWMON_H_
#define _LKPI_LINUX_HWMON_H_
#include <linux/types.h>
#include <linux/err.h>
struct device;
struct attribute_group;
struct hwmon_chip_info;
static inline struct device *devm_hwmon_device_register_with_groups(struct device *dev, const char *name,
    void *drvdata, const struct attribute_group **groups)
{
	(void)dev; (void)name; (void)drvdata; (void)groups;
	return NULL;	/* mt76 treats NULL/ERR as "no hwmon", not fatal */
}
static inline struct device *devm_hwmon_device_register_with_info(struct device *dev, const char *name,
    void *drvdata, const struct hwmon_chip_info *info, const struct attribute_group **groups)
{
	(void)dev; (void)name; (void)drvdata; (void)info; (void)groups;
	return NULL;
}
static inline bool hwmon_is_bad_char(const char c) { (void)c; return false; }
#endif
