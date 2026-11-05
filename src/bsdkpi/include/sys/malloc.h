/* SPDX-License-Identifier: MIT */
/*
 * sys/malloc.h - malloc(9) on kmalloc.  malloc/free are macros because the
 * libc/libkern functions of those names have different signatures; the
 * struct malloc_type argument is kept (and the per-type byte count
 * maintained) so that the statistics stay available for debugging.
 */
#ifndef _BSDKPI_SYS_MALLOC_H_
#define _BSDKPI_SYS_MALLOC_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <linux/slab.h>

#define M_NOWAIT	0x0001
#define M_WAITOK	0x0002
#define M_NORECLAIM	0x0080
#define M_ZERO		0x0100
#define M_NOVM		0x0200
#define M_USE_RESERVE	0x0400
#define M_NODUMP	0x0800
#define M_FIRSTFIT	0x1000
#define M_BESTFIT	0x2000
#define M_EXEC		0x4000
#define M_NEXTFIT	0x8000
#define M_NEVERFREED	0x10000
#define M_MAGIC		877983977

struct malloc_type {
	const char	*ks_shortdesc;
	const char	*ks_longdesc;
	volatile long	 ks_memuse;	/* bytes outstanding */
	volatile long	 ks_inuse;	/* allocations outstanding */
};

/* lkpi/bsd.h defines an empty MALLOC_DECLARE for headers that never see
 * this file; the real one wins once it is included. */
#undef MALLOC_DECLARE
#define MALLOC_DECLARE(type)	extern struct malloc_type type[1]
#define MALLOC_DEFINE(type, shortdesc, longdesc)			\
	struct malloc_type type[1] = {					\
		{ (shortdesc), (longdesc), 0, 0 }			\
	}

MALLOC_DECLARE(M_CACHE);
MALLOC_DECLARE(M_DEVBUF);
MALLOC_DECLARE(M_TEMP);
MALLOC_DECLARE(M_IFADDR);
MALLOC_DECLARE(M_IFMADDR);

void	*bsdkpi_malloc(size_t size, struct malloc_type *type, int flags)
	    __malloc_like __result_use_check __alloc_size(1);
void	*bsdkpi_mallocarray(size_t nmemb, size_t size, struct malloc_type *type,
	    int flags) __malloc_like __result_use_check __alloc_size2(1, 2);
void	*bsdkpi_realloc(void *addr, size_t size, struct malloc_type *type,
	    int flags) __result_use_check __alloc_size(2);
void	*bsdkpi_reallocf(void *addr, size_t size, struct malloc_type *type,
	    int flags) __result_use_check __alloc_size(2);
void	 bsdkpi_free(void *addr, struct malloc_type *type);
void	 bsdkpi_zfree(void *addr, struct malloc_type *type);
size_t	 bsdkpi_malloc_usable_size(const void *addr);
gfp_t	 bsdkpi_malloc_gfp(int flags);

#define malloc(size, type, flags)	bsdkpi_malloc((size), (type), (flags))
#define mallocarray(n, size, type, flags) bsdkpi_mallocarray((n), (size), (type), (flags))
#define realloc(addr, size, type, flags) bsdkpi_realloc((addr), (size), (type), (flags))
#define reallocf(addr, size, type, flags) bsdkpi_reallocf((addr), (size), (type), (flags))
#define free(addr, type)		bsdkpi_free((addr), (type))
#define zfree(addr, type)		bsdkpi_zfree((addr), (type))
#define malloc_usable_size(addr)	bsdkpi_malloc_usable_size(addr)
#define malloc_domainset(size, type, ds, flags) bsdkpi_malloc((size), (type), (flags))
#define malloc_exec(size, type, flags)	bsdkpi_malloc((size), (type), (flags))
#define malloc_aligned(size, align, type, flags) bsdkpi_malloc((size), (type), (flags))
#define free_domain(addr, type)		bsdkpi_free((addr), (type))

#endif /* _BSDKPI_SYS_MALLOC_H_ */
