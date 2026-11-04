/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_PM_H_
#define _LKPI_LINUX_PM_H_
#include <linux/types.h>
struct device;
struct dev_pm_ops {
	int (*prepare)(struct device *dev);
	void (*complete)(struct device *dev);
	int (*suspend)(struct device *dev);
	int (*resume)(struct device *dev);
	int (*freeze)(struct device *dev);
	int (*thaw)(struct device *dev);
	int (*poweroff)(struct device *dev);
	int (*restore)(struct device *dev);
	int (*suspend_late)(struct device *dev);
	int (*resume_early)(struct device *dev);
	int (*suspend_noirq)(struct device *dev);
	int (*resume_noirq)(struct device *dev);
	int (*runtime_suspend)(struct device *dev);
	int (*runtime_resume)(struct device *dev);
	int (*runtime_idle)(struct device *dev);
};
typedef struct pm_message {
	int event;
} pm_message_t;
#define PM_EVENT_SUSPEND 0x0002
#define PMSG_SUSPEND ((struct pm_message){ .event = PM_EVENT_SUSPEND })
#include <linux/kconfig.h>
#define PTR_IF(cond, ptr) ((cond) ? (ptr) : NULL)
#define pm_sleep_ptr(_ptr) PTR_IF(IS_ENABLED(CONFIG_PM_SLEEP), (_ptr))
#define pm_ptr(_ptr) PTR_IF(IS_ENABLED(CONFIG_PM), (_ptr))
#ifdef CONFIG_PM_SLEEP
#define SET_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	.suspend = suspend_fn, .resume = resume_fn, .freeze = suspend_fn, \
	.thaw = resume_fn, .poweroff = suspend_fn, .restore = resume_fn,
#else
#define SET_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)
#endif
#define SET_RUNTIME_PM_OPS(s, r, i)
#define SET_LATE_SYSTEM_SLEEP_PM_OPS(s, r)
#define SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(s, r)
#define SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) \
	const struct dev_pm_ops name = { SET_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) }
#define DEFINE_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) \
	const struct dev_pm_ops name = { \
		.suspend = suspend_fn, .resume = resume_fn, .freeze = suspend_fn, \
		.thaw = resume_fn, .poweroff = suspend_fn, .restore = resume_fn }
#define EXPORT_SIMPLE_DEV_PM_OPS(name, s, r) DEFINE_SIMPLE_DEV_PM_OPS(name, s, r)
#endif
