/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_SYSFS_H_
#define _LKPI_LINUX_SYSFS_H_
#include <linux/types.h>
struct kobject;
struct attribute {
	const char *name;
	unsigned short mode;
};
struct attribute_group {
	const char *name;
	struct attribute **attrs;
	struct bin_attribute **bin_attrs;
	unsigned short (*is_visible)(struct kobject *, struct attribute *, int);
};
struct bin_attribute { struct attribute attr; size_t size; };
#define __ATTR(_name, _mode, _show, _store) { .attr = { .name = #_name, .mode = _mode }, .show = _show, .store = _store }
#define __ATTR_RO(_name) { .attr = { .name = #_name, .mode = 0444 }, .show = _name##_show }
#define __ATTR_RW(_name) { .attr = { .name = #_name, .mode = 0644 }, .show = _name##_show, .store = _name##_store }
#define __ATTR_NULL { .attr = { .name = NULL } }
#define ATTRIBUTE_GROUPS(_name) \
	static const struct attribute_group _name##_group = { .attrs = _name##_attrs }; \
	static const struct attribute_group *_name##_groups[] = { &_name##_group, NULL }
#define sysfs_create_group(k, g) (0)
#define sysfs_remove_group(k, g) do { } while (0)
#define sysfs_create_file(k, a) (0)
#define sysfs_remove_file(k, a) do { } while (0)
#define sysfs_emit(buf, fmt, ...) snprintf(buf, 4096, fmt, ##__VA_ARGS__)
#define sysfs_emit_at(buf, at, fmt, ...) snprintf((buf) + (at), 4096 - (at), fmt, ##__VA_ARGS__)
#endif
