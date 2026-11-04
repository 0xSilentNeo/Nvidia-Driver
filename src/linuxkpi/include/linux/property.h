/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_PROPERTY_H_
#define _LKPI_LINUX_PROPERTY_H_
#include <linux/of.h>
struct fwnode_handle { int dummy; };
#define device_property_read_u32(d, n, v) (-EINVAL)
#define device_property_read_bool(d, n) (false)
#define device_property_present(d, n) (false)
#define device_property_read_string(d, n, v) (-EINVAL)
#define device_property_read_u8_array(d, n, v, l) (-EINVAL)
#define dev_fwnode(d) ((struct fwnode_handle *)NULL)
#endif
