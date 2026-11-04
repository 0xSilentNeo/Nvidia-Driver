/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_RWSEM_H_
#define _LKPI_LINUX_RWSEM_H_
#include <linux/mutex.h>
/* Writer-only semantics; readers serialise.  Correct, not concurrent. */
struct rw_semaphore {
	struct mutex m;
};
#define DECLARE_RWSEM(name) struct rw_semaphore name = { .m = __MUTEX_INITIALIZER(name) }
#define __RWSEM_INITIALIZER(name) { .m = __MUTEX_INITIALIZER(name) }
static inline void init_rwsem(struct rw_semaphore *s) { mutex_init(&s->m); }
static inline void down_read(struct rw_semaphore *s) { mutex_lock(&s->m); }
static inline void up_read(struct rw_semaphore *s) { mutex_unlock(&s->m); }
static inline void down_write(struct rw_semaphore *s) { mutex_lock(&s->m); }
static inline void up_write(struct rw_semaphore *s) { mutex_unlock(&s->m); }
static inline int down_read_trylock(struct rw_semaphore *s) { return mutex_trylock(&s->m); }
static inline int down_write_trylock(struct rw_semaphore *s) { return mutex_trylock(&s->m); }
static inline void downgrade_write(struct rw_semaphore *s) { (void)s; }
#define down_read_nested(s, n) down_read(s)
#define down_write_nested(s, n) down_write(s)
#endif
