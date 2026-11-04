/* SPDX-License-Identifier: MIT */
/*
 * kmalloc & co. on top of lkpi_os_malloc.  kfree() recovers the size from a
 * header the backend writes in front of the buffer, so no size tracking is
 * needed in the C layer.
 */
#ifndef _LKPI_LINUX_SLAB_H_
#define _LKPI_LINUX_SLAB_H_
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/overflow.h>
#include <lkpi/os.h>

#define ARCH_KMALLOC_MINALIGN 16
#define ZERO_SIZE_PTR ((void *)16)
#define ZERO_OR_NULL_PTR(x) ((unsigned long)(x) <= (unsigned long)ZERO_SIZE_PTR)

static inline void *kmalloc(size_t size, gfp_t flags)
{
	return lkpi_os_malloc(size, (flags & __GFP_ZERO) != 0, gfpflags_allow_blocking(flags));
}
static inline void *kzalloc(size_t size, gfp_t flags) { return kmalloc(size, flags | __GFP_ZERO); }
static inline void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
	size_t bytes;
	if (check_mul_overflow(n, size, &bytes))
		return NULL;
	return kmalloc(bytes, flags);
}
static inline void *kcalloc(size_t n, size_t size, gfp_t flags)
{
	return kmalloc_array(n, size, flags | __GFP_ZERO);
}
static inline void kfree(const void *ptr)
{
	if (!ZERO_OR_NULL_PTR(ptr))
		lkpi_os_free((void *)ptr);
}
static inline void kfree_sensitive(const void *ptr)
{
	if (!ZERO_OR_NULL_PTR(ptr)) {
		memset((void *)ptr, 0, lkpi_os_malloc_size(ptr));
		lkpi_os_free((void *)ptr);
	}
}
#define kzfree kfree_sensitive
static inline void *krealloc(const void *p, size_t new_size, gfp_t flags)
{
	void *n;
	size_t old;
	if (!new_size) {
		kfree(p);
		return ZERO_SIZE_PTR;
	}
	n = kmalloc(new_size, flags);
	if (!n)
		return NULL;
	if (!ZERO_OR_NULL_PTR(p)) {
		old = lkpi_os_malloc_size(p);
		memcpy(n, p, old < new_size ? old : new_size);
		kfree(p);
	}
	return n;
}
static inline void *krealloc_array(void *p, size_t n, size_t size, gfp_t flags)
{
	size_t bytes;
	if (check_mul_overflow(n, size, &bytes))
		return NULL;
	return krealloc(p, bytes, flags);
}
#define kmalloc_node(s, f, n)	kmalloc(s, f)
#define kzalloc_node(s, f, n)	kzalloc(s, f)
#define kcalloc_node(n, s, f, nd) kcalloc(n, s, f)
#define kvmalloc(s, f)		kmalloc(s, f)
#define kvzalloc(s, f)		kzalloc(s, f)
#define kvcalloc(n, s, f)	kcalloc(n, s, f)
#define kvmalloc_array(n, s, f)	kmalloc_array(n, s, f)
#define kvfree(p)		kfree(p)
#define vmalloc(s)		kmalloc(s, GFP_KERNEL)
#define vzalloc(s)		kzalloc(s, GFP_KERNEL)
#define vmalloc_user(s)		kzalloc(s, GFP_KERNEL)
#define vfree(p)		kfree(p)
#define kmalloc_size_roundup(s)	(s)
#define kmalloc_track_caller(s, f) kmalloc(s, f)

static inline void *kmemdup(const void *src, size_t len, gfp_t gfp)
{
	void *dst = kmalloc(len, gfp);
	if (dst)
		memcpy(dst, src, len);
	return dst;
}
static inline void *kvmemdup(const void *src, size_t len, gfp_t gfp) { return kmemdup(src, len, gfp); }
static inline char *kstrdup(const char *s, gfp_t gfp)
{
	size_t len;
	char *buf;
	if (!s)
		return NULL;
	len = strlen(s) + 1;
	buf = (char *)kmalloc(len, gfp);
	if (buf)
		memcpy(buf, s, len);
	return buf;
}
static inline char *kstrndup(const char *s, size_t max, gfp_t gfp)
{
	size_t len;
	char *buf;
	if (!s)
		return NULL;
	len = strnlen(s, max);
	buf = (char *)kmalloc(len + 1, gfp);
	if (buf) {
		memcpy(buf, s, len);
		buf[len] = '\0';
	}
	return buf;
}
static inline const char *kstrdup_const(const char *s, gfp_t gfp) { return kstrdup(s, gfp); }
static inline void kfree_const(const void *p) { kfree(p); }

/* kmem_cache: a trivial size-tagged wrapper around kmalloc. */
struct kmem_cache {
	size_t size;
	unsigned int flags;
	void (*ctor)(void *);
	const char *name;
};
#define SLAB_HWCACHE_ALIGN	0x2000u
#define SLAB_TYPESAFE_BY_RCU	0x80000u
#define SLAB_PANIC		0x40000u
struct kmem_cache *kmem_cache_create(const char *name, size_t size, size_t align,
    unsigned int flags, void (*ctor)(void *));
void kmem_cache_destroy(struct kmem_cache *c);
static inline void *kmem_cache_alloc(struct kmem_cache *c, gfp_t gfp)
{
	void *p = kmalloc(c->size, gfp);
	if (p && c->ctor)
		c->ctor(p);
	return p;
}
static inline void *kmem_cache_zalloc(struct kmem_cache *c, gfp_t gfp)
{
	return kmem_cache_alloc(c, gfp | __GFP_ZERO);
}
static inline void kmem_cache_free(struct kmem_cache *c, void *p) { (void)c; kfree(p); }
#define KMEM_CACHE(__struct, __flags) \
	kmem_cache_create(#__struct, sizeof(struct __struct), 0, (__flags), NULL)
#endif /* _LKPI_LINUX_SLAB_H_ */
