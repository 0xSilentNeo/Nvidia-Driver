/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_KOBJECT_H_
#define _LKPI_LINUX_KOBJECT_H_
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/list.h>
struct kobject {
	const char *name;
	struct kobject *parent;
	struct kref kref;
};
struct kobj_type { void (*release)(struct kobject *); };
static inline const char *kobject_name(const struct kobject *k) { return k->name; }
static inline struct kobject *kobject_get(struct kobject *k) { if (k) kref_get(&k->kref); return k; }
static inline void kobject_put(struct kobject *k) { (void)k; }
static inline void kobject_init(struct kobject *k, const struct kobj_type *t) { (void)t; kref_init(&k->kref); }
#define kobject_uevent(k, a) (0)
enum kobject_action { KOBJ_ADD, KOBJ_REMOVE, KOBJ_CHANGE };
#endif
