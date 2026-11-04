/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_PM_RUNTIME_H_
#define _LKPI_LINUX_PM_RUNTIME_H_
#include <linux/pm.h>
#define pm_runtime_enable(d) do { } while (0)
#define pm_runtime_disable(d) do { } while (0)
#define pm_runtime_get_sync(d) (0)
#define pm_runtime_put(d) (0)
#define pm_runtime_put_sync(d) (0)
#define pm_runtime_put_autosuspend(d) (0)
#define pm_runtime_mark_last_busy(d) do { } while (0)
#define pm_runtime_set_active(d) (0)
#define pm_runtime_set_autosuspend_delay(d, m) do { } while (0)
#define pm_runtime_use_autosuspend(d) do { } while (0)
#define pm_runtime_allow(d) do { } while (0)
#define pm_runtime_forbid(d) do { } while (0)
#define pm_runtime_resume_and_get(d) (0)
#endif
