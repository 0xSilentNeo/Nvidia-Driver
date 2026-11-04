/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_MUTEX_H_
#define _LKPI_LINUX_MUTEX_H_
#include <linux/types.h>
#include <linux/lockdep.h>
#include <lkpi/os.h>

/* Lazily allocated IOLock, same reasoning as spinlock_types.h. */
struct mutex {
	lkpi_os_mutex_t lock;
};
#define __MUTEX_INITIALIZER(name) { .lock = NULL }
#define DEFINE_MUTEX(name) struct mutex name = __MUTEX_INITIALIZER(name)

lkpi_os_mutex_t __lkpi_mutex_get_slow(struct mutex *m);
static inline lkpi_os_mutex_t __lkpi_mutex_get(struct mutex *m)
{
	lkpi_os_mutex_t l = __atomic_load_n(&m->lock, __ATOMIC_ACQUIRE);
	return likely(l) ? l : __lkpi_mutex_get_slow(m);
}
static inline void mutex_init(struct mutex *m) { m->lock = NULL; (void)__lkpi_mutex_get(m); }
#define __mutex_init(m, name, key) mutex_init(m)
void mutex_destroy(struct mutex *m);
static inline void mutex_lock(struct mutex *m) { lkpi_os_mutex_lock(__lkpi_mutex_get(m)); }
static inline void mutex_unlock(struct mutex *m) { lkpi_os_mutex_unlock(__lkpi_mutex_get(m)); }
static inline int mutex_trylock(struct mutex *m) { return lkpi_os_mutex_trylock(__lkpi_mutex_get(m)); }
static inline bool mutex_is_locked(struct mutex *m) { return lkpi_os_mutex_is_locked(__lkpi_mutex_get(m)); }
static inline int mutex_lock_interruptible(struct mutex *m) { mutex_lock(m); return 0; }
static inline int mutex_lock_killable(struct mutex *m) { mutex_lock(m); return 0; }
#define mutex_lock_nested(m, subclass) mutex_lock(m)
#define mutex_lock_nest_lock(m, nest) mutex_lock(m)
#define mutex_lock_interruptible_nested(m, s) mutex_lock_interruptible(m)
#define mutex_lock_io(m) mutex_lock(m)
#define might_lock(m) do { } while (0)
#define might_lock_read(m) do { } while (0)

#define DEFINE_GUARD(name, type, lock, unlock)
#define guard(name) __lkpi_guard_not_supported
#endif
