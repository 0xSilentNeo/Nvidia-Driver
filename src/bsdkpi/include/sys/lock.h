/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_SYS_LOCK_H_
#define _BSDKPI_SYS_LOCK_H_
#include <sys/cdefs.h>
#include <sys/types.h>

struct lock_object {
	const char	*lo_name;
	u_int		lo_flags;
	u_int		lo_data;
	void		*lo_witness;
};

#define LO_CLASSFLAGS	0x0000ffff
#define LO_INITIALIZED	0x00010000
#define LO_WITNESS	0x00020000
#define LO_QUIET	0x00040000
#define LO_RECURSABLE	0x00080000
#define LO_SLEEPABLE	0x00100000
#define LO_UPGRADABLE	0x00200000
#define LO_DUPOK	0x00400000
#define LO_IS_VNODE	0x00800000
#define LO_CLASSSHIFT	24
#define LO_NOPROFILE	0x10000000
#define LO_NEW		0x20000000

#define LA_MASKASSERT	0x000000ff
#define LA_UNLOCKED	0x00000000
#define LA_LOCKED	0x00000001
#define LA_SLOCKED	0x00000002
#define LA_XLOCKED	0x00000004
#define LA_RECURSED	0x00000008
#define LA_NOTRECURSED	0x00000010

#define LOCK_FILE	__FILE__
#define LOCK_LINE	__LINE__
#define LOCK_FILE_LINE_ARG_DEF
#define LOCK_FILE_LINE_ARG
#define LOCK_FILE_LINE
#define LOCK_FILE_LINE_FUNC

#define WITNESS_WARN(flags, lock, fmt, ...)	do { } while (0)
#define WITNESS_CHECK(flags, lock, fmt, ...)	(0)
#define WITNESS_DEFINEORDER(lock1, lock2)	(0)
#define WITNESS_SAVE_DECL(n)
#define WITNESS_SAVE(lock, n)			do { } while (0)
#define WITNESS_RESTORE(lock, n)		do { } while (0)
#define WITNESS_NORELEASE(lock)			do { } while (0)
#define WITNESS_RELEASEOK(lock)			do { } while (0)
#define WARN_GIANTOK	0x01
#define WARN_PANIC	0x02
#define WARN_SLEEPOK	0x04

static __inline void
lock_init(struct lock_object *lock, void *class, const char *name,
    const char *type, int flags)
{
	(void)class;
	(void)type;
	lock->lo_name = name;
	lock->lo_flags = (u_int)flags | LO_INITIALIZED;
}

static __inline void
lock_destroy(struct lock_object *lock)
{
	lock->lo_flags = 0;
}

#define lock_initialized(lo)	(((lo)->lo_flags & LO_INITIALIZED) != 0)
#endif /* _BSDKPI_SYS_LOCK_H_ */
