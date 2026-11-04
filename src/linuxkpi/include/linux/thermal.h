/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_THERMAL_H_
#define _LKPI_LINUX_THERMAL_H_
#include <linux/types.h>
#include <linux/err.h>
struct thermal_cooling_device;
struct thermal_zone_device;
struct thermal_cooling_device_ops {
	int (*get_max_state)(struct thermal_cooling_device *, unsigned long *);
	int (*get_cur_state)(struct thermal_cooling_device *, unsigned long *);
	int (*set_cur_state)(struct thermal_cooling_device *, unsigned long);
};
struct thermal_cooling_device {
	void *devdata;
	struct device *device;
};
static inline struct thermal_cooling_device *thermal_cooling_device_register(const char *type, void *devdata,
    const struct thermal_cooling_device_ops *ops)
{
	(void)type; (void)devdata; (void)ops;
	return ERR_PTR(-ENODEV);
}
static inline void thermal_cooling_device_unregister(struct thermal_cooling_device *cdev) { (void)cdev; }
#endif
