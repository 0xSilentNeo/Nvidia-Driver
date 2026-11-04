/* SPDX-License-Identifier: MIT */
/*
 * idr: integer-ID -> pointer map.  mt76 uses it for TX tokens (up to a few
 * thousand entries, dense).  Backed by a growable pointer array; see
 * src/linuxkpi/src/idr.c.
 */
#ifndef _LKPI_LINUX_IDR_H_
#define _LKPI_LINUX_IDR_H_
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/gfp.h>

struct idr {
	void **slots;
	unsigned int nslots;
	unsigned int count;
	unsigned int next;	/* cyclic allocation hint */
	unsigned int base;
	spinlock_t lock;
};
#define IDR_INIT_BASE(name, base) { .slots = NULL, .nslots = 0, .count = 0, .next = 0, .base = (base), .lock = __SPIN_LOCK_UNLOCKED(name) }
#define IDR_INIT(name) IDR_INIT_BASE(name, 0)
#define DEFINE_IDR(name) struct idr name = IDR_INIT(name)

void idr_init_base(struct idr *idr, int base);
static inline void idr_init(struct idr *idr) { idr_init_base(idr, 0); }
void idr_destroy(struct idr *idr);
int idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp);
int idr_alloc_cyclic(struct idr *idr, void *ptr, int start, int end, gfp_t gfp);
void *idr_remove(struct idr *idr, unsigned long id);
void *idr_find(const struct idr *idr, unsigned long id);
void *idr_replace(struct idr *idr, void *ptr, unsigned long id);
void *idr_get_next(struct idr *idr, int *nextid);
void *idr_get_next_ul(struct idr *idr, unsigned long *nextid);
int idr_for_each(const struct idr *idr, int (*fn)(int id, void *p, void *data), void *data);
static inline bool idr_is_empty(const struct idr *idr) { return READ_ONCE(idr->count) == 0; }
#define idr_for_each_entry(idr, entry, id) \
	for (id = 0; ((entry) = idr_get_next(idr, &(id))) != NULL; id += 1U)
#define idr_for_each_entry_ul(idr, entry, tmp, id) \
	for (tmp = 0, id = 0; ((entry) = idr_get_next_ul(idr, &(id))) != NULL; tmp = id, ++id)
#define idr_for_each_entry_continue(idr, entry, id) \
	for ((entry) = idr_get_next((idr), &(id)); entry; ++id, (entry) = idr_get_next((idr), &(id)))
#define idr_preload(gfp) do { } while (0)
#define idr_preload_end() do { } while (0)

/* ida: ID allocator without payloads. */
struct ida {
	struct idr idr;
};
#define DEFINE_IDA(name) struct ida name = { .idr = IDR_INIT(name) }
static inline void ida_init(struct ida *ida) { idr_init(&ida->idr); }
static inline void ida_destroy(struct ida *ida) { idr_destroy(&ida->idr); }
static inline int ida_alloc_range(struct ida *ida, unsigned int min, unsigned int max, gfp_t gfp)
{
	return idr_alloc(&ida->idr, (void *)1, min, max == ~0U ? 0 : (int)max + 1, gfp);
}
static inline int ida_alloc(struct ida *ida, gfp_t gfp) { return ida_alloc_range(ida, 0, ~0U, gfp); }
static inline int ida_alloc_min(struct ida *ida, unsigned int min, gfp_t gfp) { return ida_alloc_range(ida, min, ~0U, gfp); }
static inline int ida_alloc_max(struct ida *ida, unsigned int max, gfp_t gfp) { return ida_alloc_range(ida, 0, max, gfp); }
static inline void ida_free(struct ida *ida, unsigned int id) { idr_remove(&ida->idr, id); }
#define ida_simple_get(ida, start, end, gfp) ida_alloc_range(ida, start, (end) ? (end) - 1 : ~0U, gfp)
#define ida_simple_remove(ida, id) ida_free(ida, id)
static inline bool ida_is_empty(const struct ida *ida) { return idr_is_empty(&ida->idr); }
#endif
