/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_DEV_PRINTK_H_
#define _LKPI_LINUX_DEV_PRINTK_H_
#include <linux/printk.h>
struct device;
void dev_printk_emit(int level, const struct device *dev, const char *fmt, ...) __printf(3, 4);
#define dev_printk(level, dev, fmt, ...) dev_printk_emit((level)[1] - '0', dev, fmt, ##__VA_ARGS__)
#define dev_emerg(dev, fmt, ...)	dev_printk_emit(0, dev, fmt, ##__VA_ARGS__)
#define dev_alert(dev, fmt, ...)	dev_printk_emit(1, dev, fmt, ##__VA_ARGS__)
#define dev_crit(dev, fmt, ...)		dev_printk_emit(2, dev, fmt, ##__VA_ARGS__)
#define dev_err(dev, fmt, ...)		dev_printk_emit(3, dev, fmt, ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...)		dev_printk_emit(4, dev, fmt, ##__VA_ARGS__)
#define dev_notice(dev, fmt, ...)	dev_printk_emit(5, dev, fmt, ##__VA_ARGS__)
#define dev_info(dev, fmt, ...)		dev_printk_emit(6, dev, fmt, ##__VA_ARGS__)
#ifdef DEBUG
#define dev_dbg(dev, fmt, ...)		dev_printk_emit(7, dev, fmt, ##__VA_ARGS__)
#else
#define dev_dbg(dev, fmt, ...)		do { if (0) dev_printk_emit(7, dev, fmt, ##__VA_ARGS__); } while (0)
#endif
#define dev_vdbg(dev, fmt, ...)		do { } while (0)
#define dev_err_once(dev, fmt, ...)	({ static bool __w; if (!__w) { __w = true; dev_err(dev, fmt, ##__VA_ARGS__); } })
#define dev_warn_once(dev, fmt, ...)	({ static bool __w; if (!__w) { __w = true; dev_warn(dev, fmt, ##__VA_ARGS__); } })
#define dev_info_once(dev, fmt, ...)	({ static bool __w; if (!__w) { __w = true; dev_info(dev, fmt, ##__VA_ARGS__); } })
#define dev_notice_once(dev, fmt, ...)	({ static bool __w; if (!__w) { __w = true; dev_notice(dev, fmt, ##__VA_ARGS__); } })
#define dev_dbg_once(dev, fmt, ...)	do { } while (0)
#define dev_err_ratelimited(dev, fmt, ...)	dev_err(dev, fmt, ##__VA_ARGS__)
#define dev_warn_ratelimited(dev, fmt, ...)	dev_warn(dev, fmt, ##__VA_ARGS__)
#define dev_info_ratelimited(dev, fmt, ...)	dev_info(dev, fmt, ##__VA_ARGS__)
#define dev_notice_ratelimited(dev, fmt, ...)	dev_notice(dev, fmt, ##__VA_ARGS__)
#define dev_dbg_ratelimited(dev, fmt, ...)	dev_dbg(dev, fmt, ##__VA_ARGS__)
int dev_err_probe(const struct device *dev, int err, const char *fmt, ...) __printf(3, 4);
#define dev_WARN(dev, fmt, ...) WARN(1, fmt, ##__VA_ARGS__)
#define dev_WARN_ONCE(dev, cond, fmt, ...) WARN_ONCE(cond, fmt, ##__VA_ARGS__)
#endif
