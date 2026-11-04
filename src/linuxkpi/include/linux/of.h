/* SPDX-License-Identifier: MIT */
/* No device tree on macOS: every lookup fails cleanly. */
#ifndef _LKPI_LINUX_OF_H_
#define _LKPI_LINUX_OF_H_
#include <linux/types.h>
#include <linux/errno.h>
struct device_node {
	const char *name;
};
struct property {
	const char *name;
	int length;
	void *value;
};
#define of_property_read_u32(np, name, out) (-EINVAL)
#define of_property_read_u32_array(np, name, out, sz) (-EINVAL)
#define of_property_read_u8_array(np, name, out, sz) (-EINVAL)
#define of_property_read_string(np, name, out) (-EINVAL)
#define of_property_read_bool(np, name) (false)
#define of_property_count_u32_elems(np, name) (-EINVAL)
#define of_property_count_strings(np, name) (-EINVAL)
#define of_property_present(np, name) (false)
static inline struct property *of_find_property(const struct device_node *np, const char *name, int *lenp)
{
	(void)np; (void)name;
	if (lenp) *lenp = 0;
	return NULL;
}
static inline const void *of_get_property(const struct device_node *np, const char *name, int *lenp)
{
	(void)np; (void)name;
	if (lenp) *lenp = 0;
	return NULL;
}
static inline const char *of_prop_next_string(struct property *prop, const char *cur) { (void)prop; (void)cur; return NULL; }
static inline struct device_node *of_get_child_by_name(const struct device_node *np, const char *name) { (void)np; (void)name; return NULL; }
static inline struct device_node *of_find_node_by_phandle(u32 handle) { (void)handle; return NULL; }
static inline struct device_node *of_parse_phandle(const struct device_node *np, const char *name, int idx) { (void)np; (void)name; (void)idx; return NULL; }
static inline struct device_node *of_node_get(struct device_node *np) { return np; }
static inline void of_node_put(struct device_node *np) { (void)np; }
static inline bool of_device_is_available(const struct device_node *np) { (void)np; return false; }
static inline bool of_device_is_compatible(const struct device_node *np, const char *c) { (void)np; (void)c; return false; }
#define for_each_child_of_node(parent, child) for (child = NULL; child != NULL; )
#define for_each_available_child_of_node(parent, child) for (child = NULL; child != NULL; )
#define of_property_for_each_string(np, propname, prop, s) \
	for (prop = of_find_property(np, propname, NULL), s = NULL; prop && s; s = NULL)
#define of_match_ptr(p) NULL
#define of_match_device(m, d) NULL
#define of_get_mac_address(np, mac) (-ENODEV)
#define of_get_mac_address_nvmem(np, mac) (-ENODEV)
static inline bool is_of_node(const void *n) { (void)n; return false; }
#endif
