// SPDX-License-Identifier: MIT
/*
 * idr: integer ID -> pointer map backed by a growable pointer array.
 *
 * Slot i holds the pointer for ID base + i.  The array doubles on demand
 * (allocated outside the lock, since callers pass GFP_KERNEL as well as
 * GFP_ATOMIC) and is capped at LKPI_IDR_MAX_SLOTS entries.  A reserved ID
 * with a NULL payload (idr_alloc(idr, NULL, ...)) is stored as a private
 * sentinel so that it still counts as allocated; iteration skips it.
 *
 * idr->lock protects the array.  Callbacks (idr_for_each) and the pointers
 * returned by idr_get_next() are handed out with the lock dropped, like
 * upstream, so callers keep their own lock around iteration.
 */
#include <linux/kernel.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/limits.h>

#define LKPI_IDR_MIN_SLOTS	64u
#define LKPI_IDR_MAX_SLOTS	(1u << 22)

static char lkpi_idr_null_sentinel;
#define LKPI_IDR_NULL		((void *)&lkpi_idr_null_sentinel)

static inline void *lkpi_idr_store(void *ptr)
{
	return ptr ? ptr : LKPI_IDR_NULL;
}

static inline void *lkpi_idr_load(void *slot)
{
	return slot == LKPI_IDR_NULL ? NULL : slot;
}

void idr_init_base(struct idr *idr, int base)
{
	idr->slots = NULL;
	idr->nslots = 0;
	idr->count = 0;
	idr->next = 0;
	idr->base = base < 0 ? 0 : (unsigned int)base;
	spin_lock_init(&idr->lock);
}

void idr_destroy(struct idr *idr)
{
	unsigned long flags;
	void **slots;

	spin_lock_irqsave(&idr->lock, flags);
	slots = idr->slots;
	idr->slots = NULL;
	idr->nslots = 0;
	idr->count = 0;
	idr->next = 0;
	spin_unlock_irqrestore(&idr->lock, flags);
	kfree(slots);
}

/* Grow the array so that slot @idx exists.  Lock not held. */
static int lkpi_idr_grow(struct idr *idr, unsigned int idx, gfp_t gfp)
{
	unsigned long flags;
	void **slots, **old;
	unsigned int n;

	if (idx >= LKPI_IDR_MAX_SLOTS)
		return -ENOMEM;

	spin_lock_irqsave(&idr->lock, flags);
	n = idr->nslots ? idr->nslots : LKPI_IDR_MIN_SLOTS;
	spin_unlock_irqrestore(&idr->lock, flags);
	while (n <= idx)
		n *= 2;
	if (n > LKPI_IDR_MAX_SLOTS)
		n = LKPI_IDR_MAX_SLOTS;

	slots = kcalloc(n, sizeof(*slots), gfp);
	if (!slots)
		return -ENOMEM;

	spin_lock_irqsave(&idr->lock, flags);
	if (idr->nslots >= n) {
		/* someone else grew it meanwhile */
		spin_unlock_irqrestore(&idr->lock, flags);
		kfree(slots);
		return 0;
	}
	old = idr->slots;
	if (old)
		memcpy(slots, old, idr->nslots * sizeof(*slots));
	idr->slots = slots;
	idr->nslots = n;
	spin_unlock_irqrestore(&idr->lock, flags);
	kfree(old);
	return 0;
}

int idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp)
{
	unsigned int first, last, idx, limit;
	unsigned long flags;

	if (WARN_ON_ONCE(start < 0))
		return -EINVAL;
	if (end <= 0)
		end = INT_MAX;
	if (start >= end)
		return -ENOSPC;

	if ((unsigned int)start < idr->base)
		start = (int)idr->base;
	if ((unsigned int)start >= (unsigned int)end)
		return -ENOSPC;
	first = (unsigned int)start - idr->base;	/* inclusive slot */
	last = (unsigned int)end - idr->base;		/* exclusive slot */

	for (;;) {
		spin_lock_irqsave(&idr->lock, flags);
		limit = min(last, idr->nslots);
		for (idx = first; idx < limit; idx++) {
			if (!idr->slots[idx]) {
				idr->slots[idx] = lkpi_idr_store(ptr);
				idr->count++;
				spin_unlock_irqrestore(&idr->lock, flags);
				return (int)(idx + idr->base);
			}
		}
		spin_unlock_irqrestore(&idr->lock, flags);

		/* every existing slot in range is taken */
		if (idx >= last)
			return -ENOSPC;
		if (lkpi_idr_grow(idr, idx, gfp))
			return idx >= LKPI_IDR_MAX_SLOTS ? -ENOSPC : -ENOMEM;
		first = idx;
	}
}

int idr_alloc_cyclic(struct idr *idr, void *ptr, int start, int end, gfp_t gfp)
{
	int cur = (int)READ_ONCE(idr->next), id;

	if (cur < start || (end > 0 && cur >= end))
		cur = start;
	id = idr_alloc(idr, ptr, cur, end, gfp);
	if (id == -ENOSPC && cur != start)
		id = idr_alloc(idr, ptr, start, end, gfp);
	if (id >= 0)
		WRITE_ONCE(idr->next, (unsigned int)id + 1);
	return id;
}

void *idr_remove(struct idr *idr, unsigned long id)
{
	unsigned long flags;
	void *p = NULL;

	spin_lock_irqsave(&idr->lock, flags);
	if (id >= idr->base && id - idr->base < idr->nslots) {
		unsigned int idx = (unsigned int)(id - idr->base);

		p = idr->slots[idx];
		if (p) {
			idr->slots[idx] = NULL;
			idr->count--;
		}
	}
	spin_unlock_irqrestore(&idr->lock, flags);
	return lkpi_idr_load(p);
}

void *idr_find(const struct idr *idr, unsigned long id)
{
	struct idr *i = (struct idr *)idr;
	unsigned long flags;
	void *p = NULL;

	spin_lock_irqsave(&i->lock, flags);
	if (id >= idr->base && id - idr->base < idr->nslots)
		p = idr->slots[id - idr->base];
	spin_unlock_irqrestore(&i->lock, flags);
	return lkpi_idr_load(p);
}

void *idr_replace(struct idr *idr, void *ptr, unsigned long id)
{
	unsigned long flags;
	void *old;

	spin_lock_irqsave(&idr->lock, flags);
	if (id < idr->base || id - idr->base >= idr->nslots || !idr->slots[id - idr->base]) {
		spin_unlock_irqrestore(&idr->lock, flags);
		return ERR_PTR(-ENOENT);
	}
	old = idr->slots[id - idr->base];
	idr->slots[id - idr->base] = lkpi_idr_store(ptr);
	spin_unlock_irqrestore(&idr->lock, flags);
	return lkpi_idr_load(old);
}

/* Next non-empty slot at or after @from (a slot index), with its payload. */
static void *lkpi_idr_next(struct idr *idr, unsigned long from, unsigned long *found)
{
	unsigned long flags;
	unsigned int idx;
	void *p;

	spin_lock_irqsave(&idr->lock, flags);
	for (idx = from < idr->nslots ? (unsigned int)from : idr->nslots; idx < idr->nslots; idx++) {
		p = lkpi_idr_load(idr->slots[idx]);
		if (p) {
			*found = idx;
			spin_unlock_irqrestore(&idr->lock, flags);
			return p;
		}
	}
	spin_unlock_irqrestore(&idr->lock, flags);
	return NULL;
}

void *idr_get_next_ul(struct idr *idr, unsigned long *nextid)
{
	unsigned long from = *nextid >= idr->base ? *nextid - idr->base : 0, idx;
	void *p = lkpi_idr_next(idr, from, &idx);

	if (p)
		*nextid = idx + idr->base;
	return p;
}

void *idr_get_next(struct idr *idr, int *nextid)
{
	unsigned long id = *nextid < 0 ? 0 : (unsigned long)*nextid;
	void *p = idr_get_next_ul(idr, &id);

	if (p) {
		if (id > INT_MAX)
			return NULL;
		*nextid = (int)id;
	}
	return p;
}

int idr_for_each(const struct idr *idr, int (*fn)(int id, void *p, void *data), void *data)
{
	struct idr *i = (struct idr *)idr;
	unsigned long idx = 0;
	void *p;
	int ret;

	while ((p = lkpi_idr_next(i, idx, &idx)) != NULL) {
		if (idx + idr->base > INT_MAX)
			break;
		ret = fn((int)(idx + idr->base), p, data);
		if (ret)
			return ret;
		idx++;
	}
	return 0;
}
