/* SPDX-License-Identifier: MIT */
/* sys/counter.h - counter(9) as a single atomic 64-bit word. */
#ifndef _BSDKPI_SYS_COUNTER_H_
#define _BSDKPI_SYS_COUNTER_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/malloc.h>

typedef uint64_t *counter_u64_t;

counter_u64_t	counter_u64_alloc(int flags);
void		counter_u64_free(counter_u64_t c);

static __inline void
counter_u64_add(counter_u64_t c, int64_t inc)
{
	(void)__atomic_fetch_add(c, (uint64_t)inc, __ATOMIC_RELAXED);
}

static __inline uint64_t
counter_u64_fetch(counter_u64_t c)
{
	return (__atomic_load_n(c, __ATOMIC_RELAXED));
}

static __inline void
counter_u64_zero(counter_u64_t c)
{
	__atomic_store_n(c, 0, __ATOMIC_RELAXED);
}

static __inline void
counter_enter(void)
{
}

static __inline void
counter_exit(void)
{
}

#define counter_u64_add_protected(c, i)	counter_u64_add((c), (i))
#define counter_u64_fetch_protected(c)	counter_u64_fetch(c)

#define COUNTER_ARRAY_ALLOC(a, n, wait) do {				\
	for (int _i = 0; _i < (n); _i++)				\
		(a)[_i] = counter_u64_alloc(wait);			\
} while (0)
#define COUNTER_ARRAY_FREE(a, n) do {					\
	for (int _i = 0; _i < (n); _i++)				\
		counter_u64_free((a)[_i]);				\
} while (0)
#define COUNTER_ARRAY_COPY(a, dstp, n) do {				\
	for (int _i = 0; _i < (n); _i++)				\
		((uint64_t *)(dstp))[_i] = counter_u64_fetch((a)[_i]);	\
} while (0)
#define COUNTER_ARRAY_ZERO(a, n) do {					\
	for (int _i = 0; _i < (n); _i++)				\
		counter_u64_zero((a)[_i]);				\
} while (0)

struct sysctl_req;
struct sysctl_oid;
int	sysctl_handle_counter_u64(struct sysctl_oid *oidp, void *arg1,
	    intmax_t arg2, struct sysctl_req *req);
int	sysctl_handle_counter_u64_array(struct sysctl_oid *oidp, void *arg1,
	    intmax_t arg2, struct sysctl_req *req);
#endif /* _BSDKPI_SYS_COUNTER_H_ */
