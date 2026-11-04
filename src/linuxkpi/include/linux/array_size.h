/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_ARRAY_SIZE_H_
#define _LKPI_LINUX_ARRAY_SIZE_H_
#include <linux/build_bug.h>
#define __must_be_array(a) BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))
#endif
