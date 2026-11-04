/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_INIT_H_
#define _LKPI_LINUX_INIT_H_
#include <linux/compiler_types.h>
typedef int (*initcall_t)(void);
typedef void (*exitcall_t)(void);
#define early_initcall(fn)
#define core_initcall(fn)
#define subsys_initcall(fn)
#define device_initcall(fn)
#define late_initcall(fn)
#define __setup(str, fn)
#endif
