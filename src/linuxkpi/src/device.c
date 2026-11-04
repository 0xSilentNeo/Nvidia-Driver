// SPDX-License-Identifier: MIT
/*
 * struct device and devres (managed resources).
 *
 * Every devres entry is one allocation: a small header followed by the
 * caller's data, aligned so that the data may hold any structure.  The
 * header is linked on dev->devres in allocation order; lkpi_device_fini()
 * runs the release callbacks in reverse order, as the Linux driver core
 * does after remove().
 *
 * devres_release()/devres_destroy() take a plain pointer instead of a match
 * function: NULL matches the first entry with that release callback, a
 * pointer equal to an entry's data matches that entry, and otherwise the
 * entry whose data compares equal (memcmp over its size) is taken.  The
 * last rule lets callers that only know the contents (dmam_free_coherent)
 * find their record.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/sprintf.h>
#include <lkpi/os.h>

struct lkpi_devres {
	struct list_head node;
	dr_release_t release;
	size_t size;
	u8 data[] __aligned(16);
};

static inline struct lkpi_devres *to_devres(void *data)
{
	return container_of(data, struct lkpi_devres, data);
}

/* ---- struct device --------------------------------------------------- */

void lkpi_device_init(struct device *dev, struct device *parent, const char *name)
{
	memset(dev, 0, sizeof(*dev));
	dev->parent = parent;
	if (name)
		strscpy(dev->name, name, sizeof(dev->name));
	kobject_init(&dev->kobj, NULL);
	dev->kobj.name = dev->name;
	dev->kobj.parent = parent ? &parent->kobj : NULL;
	INIT_LIST_HEAD(&dev->devres);
	mutex_init(&dev->devres_lock);
	dev->coherent_dma_mask = 0;
	dev->dma_mask = NULL;
}

void lkpi_device_fini(struct device *dev)
{
	struct lkpi_devres *dr;

	/* Reverse order; each release may itself add/remove entries. */
	for (;;) {
		mutex_lock(&dev->devres_lock);
		if (list_empty(&dev->devres)) {
			mutex_unlock(&dev->devres_lock);
			break;
		}
		dr = list_last_entry(&dev->devres, struct lkpi_devres, node);
		list_del_init(&dr->node);
		mutex_unlock(&dev->devres_lock);
		if (dr->release)
			dr->release(dev, dr->data);
		kfree(dr);
	}
	mutex_destroy(&dev->devres_lock);
}

int dev_set_name(struct device *dev, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(dev->name, sizeof(dev->name), fmt, ap);
	va_end(ap);
	dev->kobj.name = dev->name;
	return n < 0 ? -EINVAL : 0;
}

/* ---- devres core ----------------------------------------------------- */

void *devres_alloc(dr_release_t release, size_t size, gfp_t gfp)
{
	struct lkpi_devres *dr;

	dr = kzalloc(sizeof(*dr) + size, gfp);
	if (!dr)
		return NULL;
	INIT_LIST_HEAD(&dr->node);
	dr->release = release;
	dr->size = size;
	return dr->data;
}

void devres_free(void *res)
{
	struct lkpi_devres *dr;

	if (!res)
		return;
	dr = to_devres(res);
	WARN_ON(!list_empty(&dr->node));
	kfree(dr);
}

void devres_add(struct device *dev, void *res)
{
	struct lkpi_devres *dr = to_devres(res);

	mutex_lock(&dev->devres_lock);
	list_add_tail(&dr->node, &dev->devres);
	mutex_unlock(&dev->devres_lock);
}

/* Caller holds dev->devres_lock. */
static struct lkpi_devres *lkpi_devres_find(struct device *dev, dr_release_t release,
    void *match_data)
{
	struct lkpi_devres *dr;

	list_for_each_entry_reverse(dr, &dev->devres, node) {
		if (dr->release != release)
			continue;
		if (!match_data || dr->data == match_data)
			return dr;
	}
	if (!match_data)
		return NULL;
	list_for_each_entry_reverse(dr, &dev->devres, node) {
		if (dr->release == release && dr->size &&
		    memcmp(dr->data, match_data, dr->size) == 0)
			return dr;
	}
	return NULL;
}

static struct lkpi_devres *lkpi_devres_remove(struct device *dev, dr_release_t release,
    void *match_data)
{
	struct lkpi_devres *dr;

	mutex_lock(&dev->devres_lock);
	dr = lkpi_devres_find(dev, release, match_data);
	if (dr)
		list_del_init(&dr->node);
	mutex_unlock(&dev->devres_lock);
	return dr;
}

int devres_release(struct device *dev, dr_release_t release, void *match_data)
{
	struct lkpi_devres *dr = lkpi_devres_remove(dev, release, match_data);

	if (!dr)
		return -ENOENT;
	if (dr->release)
		dr->release(dev, dr->data);
	kfree(dr);
	return 0;
}

int devres_destroy(struct device *dev, dr_release_t release, void *match_data)
{
	struct lkpi_devres *dr = lkpi_devres_remove(dev, release, match_data);

	if (!dr)
		return -ENOENT;
	kfree(dr);
	return 0;
}

/* ---- devm_add_action -------------------------------------------------- */

struct lkpi_devm_action {
	void (*action)(void *);
	void *data;
};

static void lkpi_devm_action_release(struct device *dev, void *res)
{
	struct lkpi_devm_action *a = res;

	(void)dev;
	a->action(a->data);
}

int devm_add_action(struct device *dev, void (*action)(void *), void *data)
{
	struct lkpi_devm_action *a;

	a = devres_alloc(lkpi_devm_action_release, sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;
	a->action = action;
	a->data = data;
	devres_add(dev, a);
	return 0;
}

void devm_remove_action(struct device *dev, void (*action)(void *), void *data)
{
	struct lkpi_devm_action key = { .action = action, .data = data };

	WARN_ON(devres_destroy(dev, lkpi_devm_action_release, &key));
}

/* ---- devm_kmalloc & co ------------------------------------------------ */

static void lkpi_devm_kmalloc_release(struct device *dev, void *res)
{
	(void)dev; (void)res;	/* memory goes away with the devres header */
}

void *devm_kmalloc(struct device *dev, size_t size, gfp_t gfp)
{
	void *p;

	if (!size)
		return ZERO_SIZE_PTR;
	p = devres_alloc(lkpi_devm_kmalloc_release, size, gfp);
	if (!p)
		return NULL;
	if (gfp & __GFP_ZERO)
		memset(p, 0, size);	/* devres_alloc() zeroes already; be explicit */
	devres_add(dev, p);
	return p;
}

void devm_kfree(struct device *dev, const void *p)
{
	if (ZERO_OR_NULL_PTR(p))
		return;
	WARN_ON(devres_destroy(dev, lkpi_devm_kmalloc_release, (void *)p));
}

void *devm_kmemdup(struct device *dev, const void *src, size_t len, gfp_t gfp)
{
	void *p = devm_kmalloc(dev, len, gfp);

	if (p && len)
		memcpy(p, src, len);
	return p;
}

char *devm_kstrdup(struct device *dev, const char *s, gfp_t gfp)
{
	size_t len;
	char *p;

	if (!s)
		return NULL;
	len = strlen(s) + 1;
	p = devm_kmalloc(dev, len, gfp);
	if (p)
		memcpy(p, s, len);
	return p;
}

char *devm_kasprintf(struct device *dev, gfp_t gfp, const char *fmt, ...)
{
	va_list ap, ap2;
	char *p;
	int len;

	va_start(ap, fmt);
	va_copy(ap2, ap);
	len = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	if (len < 0) {
		va_end(ap);
		return NULL;
	}
	p = devm_kmalloc(dev, (size_t)len + 1, gfp);
	if (p)
		vsnprintf(p, (size_t)len + 1, fmt, ap);
	va_end(ap);
	return p;
}
