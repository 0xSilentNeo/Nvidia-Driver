/* SPDX-License-Identifier: MIT */
/*
 * struct device and the dev_* helpers.  Every device in this port is a PCI
 * function owned by the kext; the generic object only carries what mt76
 * touches (drvdata, of_node, name, devres list).
 */
#ifndef _LKPI_LINUX_DEVICE_H_
#define _LKPI_LINUX_DEVICE_H_
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/dev_printk.h>
#include <lkpi/os.h>

struct device_node;
struct fwnode_handle;
struct device_driver;
struct bus_type;
struct class;

struct device {
	struct device *parent;
	struct kobject kobj;
	char name[48];
	void *driver_data;
	struct device_node *of_node;
	struct fwnode_handle *fwnode;
	struct device_driver *driver;
	const struct bus_type *bus;
	struct list_head devres;		/* lkpi devres entries */
	struct mutex devres_lock;
	u64 dma_mask_value;
	u64 *dma_mask;
	u64 coherent_dma_mask;
	u64 bus_dma_limit;
	lkpi_os_pci_t pci;			/* backend handle, NULL for virtual devs */
	const struct attribute_group **groups;
	bool power_can_wakeup;
	bool power_should_wakeup;
};

struct device_driver {
	const char *name;
	const struct dev_pm_ops *pm;
	struct module *owner;
	const void *of_match_table;
	const void *acpi_match_table;
};

struct device_attribute {
	struct attribute attr;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr, char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
};
#define DEVICE_ATTR(_name, _mode, _show, _store) \
	struct device_attribute dev_attr_##_name = __ATTR(_name, _mode, _show, _store)
#define DEVICE_ATTR_RO(_name) struct device_attribute dev_attr_##_name = __ATTR_RO(_name)
#define DEVICE_ATTR_RW(_name) struct device_attribute dev_attr_##_name = __ATTR_RW(_name)
#define DEVICE_ATTR_WO(_name) struct device_attribute dev_attr_##_name = \
	{ .attr = { .name = #_name, .mode = 0200 }, .store = _name##_store }
#define device_create_file(d, a) (0)
#define device_remove_file(d, a) do { } while (0)

static inline char *dev_name(const struct device *dev) { return (char *)dev->name; }
int dev_set_name(struct device *dev, const char *fmt, ...) __printf(2, 3);
static inline void *dev_get_drvdata(const struct device *dev) { return dev->driver_data; }
static inline void dev_set_drvdata(struct device *dev, void *data) { dev->driver_data = data; }
static inline struct device *get_device(struct device *dev) { return dev; }
static inline void put_device(struct device *dev) { (void)dev; }
static inline bool device_can_wakeup(struct device *dev) { return dev->power_can_wakeup; }
static inline int device_set_wakeup_enable(struct device *dev, bool enable) { dev->power_should_wakeup = enable; return 0; }
static inline int device_init_wakeup(struct device *dev, bool enable) { dev->power_can_wakeup = enable; dev->power_should_wakeup = enable; return 0; }
static inline bool device_may_wakeup(struct device *dev) { return dev->power_should_wakeup; }
static inline void device_set_wakeup_capable(struct device *dev, bool c) { dev->power_can_wakeup = c; }
static inline bool dev_is_pci(const struct device *dev) { return dev->pci != NULL; }
static inline void device_lock(struct device *dev) { (void)dev; }
static inline void device_unlock(struct device *dev) { (void)dev; }
static inline int device_add_groups(struct device *d, const struct attribute_group **g) { (void)d; (void)g; return 0; }
static inline void device_remove_groups(struct device *d, const struct attribute_group **g) { (void)d; (void)g; }
void lkpi_device_init(struct device *dev, struct device *parent, const char *name);
void lkpi_device_fini(struct device *dev);	/* runs devres release actions */

/* ---- devres ---- */
typedef void (*dr_release_t)(struct device *dev, void *res);
void *devres_alloc(dr_release_t release, size_t size, gfp_t gfp);
void devres_free(void *res);
void devres_add(struct device *dev, void *res);
int devres_release(struct device *dev, dr_release_t release, void *match_data);
int devres_destroy(struct device *dev, dr_release_t release, void *match_data);
int devm_add_action(struct device *dev, void (*action)(void *), void *data);
static inline int devm_add_action_or_reset(struct device *dev, void (*action)(void *), void *data)
{
	int ret = devm_add_action(dev, action, data);
	if (ret)
		action(data);
	return ret;
}
void devm_remove_action(struct device *dev, void (*action)(void *), void *data);
void *devm_kmalloc(struct device *dev, size_t size, gfp_t gfp);
static inline void *devm_kzalloc(struct device *dev, size_t size, gfp_t gfp) { return devm_kmalloc(dev, size, gfp | __GFP_ZERO); }
static inline void *devm_kmalloc_array(struct device *dev, size_t n, size_t size, gfp_t gfp)
{
	size_t bytes;
	if (check_mul_overflow(n, size, &bytes))
		return NULL;
	return devm_kmalloc(dev, bytes, gfp);
}
static inline void *devm_kcalloc(struct device *dev, size_t n, size_t size, gfp_t gfp)
{
	return devm_kmalloc_array(dev, n, size, gfp | __GFP_ZERO);
}
void devm_kfree(struct device *dev, const void *p);
void *devm_kmemdup(struct device *dev, const void *src, size_t len, gfp_t gfp);
char *devm_kstrdup(struct device *dev, const char *s, gfp_t gfp);
char *devm_kasprintf(struct device *dev, gfp_t gfp, const char *fmt, ...) __printf(3, 4);
#define devm_kvasprintf(dev, gfp, fmt, ap) kvasprintf(gfp, fmt, ap)

/* class/bus registration are not needed for a single-driver kext */
struct class { const char *name; };
struct bus_type { const char *name; };
#endif /* _LKPI_LINUX_DEVICE_H_ */
