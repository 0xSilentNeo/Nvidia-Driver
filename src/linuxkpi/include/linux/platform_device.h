/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_PLATFORM_DEVICE_H_
#define _LKPI_LINUX_PLATFORM_DEVICE_H_
#include <linux/device.h>
struct platform_device {
	struct device dev;
	const char *name;
	int id;
};
#define to_platform_device(d) container_of(d, struct platform_device, dev)
static inline void *platform_get_drvdata(const struct platform_device *pdev) { return dev_get_drvdata(&pdev->dev); }
static inline void platform_set_drvdata(struct platform_device *pdev, void *data) { dev_set_drvdata(&pdev->dev, data); }
#endif
