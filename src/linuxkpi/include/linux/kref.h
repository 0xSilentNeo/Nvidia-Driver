/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_KREF_H_
#define _LKPI_LINUX_KREF_H_
#include <linux/refcount.h>
struct kref {
	refcount_t refcount;
};
#define KREF_INIT(n) { .refcount = REFCOUNT_INIT(n) }
static inline void kref_init(struct kref *k) { refcount_set(&k->refcount, 1); }
static inline unsigned int kref_read(const struct kref *k) { return refcount_read(&k->refcount); }
static inline void kref_get(struct kref *k) { refcount_inc(&k->refcount); }
static inline int kref_put(struct kref *k, void (*release)(struct kref *))
{
	if (refcount_dec_and_test(&k->refcount)) {
		release(k);
		return 1;
	}
	return 0;
}
static inline int kref_get_unless_zero(struct kref *k) { return refcount_inc_not_zero(&k->refcount); }
#endif
