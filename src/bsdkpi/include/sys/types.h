/* SPDX-License-Identifier: MIT */
/*
 * sys/types.h - FreeBSD type names on top of the host's <sys/types.h>.
 *
 * This header is found first (src/bsdkpi/include precedes everything else
 * on the include path), chains to the host's with #include_next for the
 * POSIX and BSD basics it already has (u_int, caddr_t, ssize_t, ...) and
 * adds the FreeBSD-only names.  It also pulls in linux/types.h so that the
 * LinuxKPI side (lkpi/bsd.h with KASSERT, panic, byte order) is in place
 * before any FreeBSD header defines its own versions.
 */
#ifndef _BSDKPI_SYS_TYPES_H_
#define _BSDKPI_SYS_TYPES_H_

#include <sys/cdefs.h>
#include_next <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
/*
 * Every LinuxKPI header the shim builds on is pulled in here, first, so
 * that the FreeBSD macros that share a name (ALIGN, roundup, LIST_HEAD,
 * ...) are defined afterwards and win for the rest of the translation
 * unit.  LinuxKPI headers included again later are guarded no-ops.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/random.h>
#include <linux/delay.h>
#include <lkpi/bsd.h>
#undef LIST_HEAD		/* linux/list.h's one-argument form */
#include <sys/queue.h>		/* FreeBSD's LIST_HEAD(name, type) */

#if !defined(__APPLE__)
/* glibc defines these under __USE_MISC and records it with these guards;
 * XNU's sys/types.h always has them. */
#ifndef __u_char_defined
typedef unsigned char	u_char;
typedef unsigned short	u_short;
typedef unsigned int	u_int;
typedef unsigned long	u_long;
typedef int64_t		quad_t;
typedef uint64_t	u_quad_t;
#define __u_char_defined
#endif
#ifndef __caddr_t_defined
typedef char		*caddr_t;
#define __caddr_t_defined
#endif
#endif /* !__APPLE__ */

typedef const char	*c_caddr_t;
typedef uint8_t		__uint8_t;
typedef uint16_t	__uint16_t;
typedef uint32_t	__uint32_t;
typedef uint64_t	__uint64_t;
typedef int8_t		__int8_t;
typedef int16_t		__int16_t;
typedef int32_t		__int32_t;
typedef int64_t		__int64_t;

#if !defined(__APPLE__)
typedef uintptr_t	vm_offset_t;
typedef uint64_t	vm_ooffset_t;
typedef uintptr_t	vm_size_t;
typedef uint64_t	vm_paddr_t;
typedef int		boolean_t;
typedef uint8_t		u_int8_t;
typedef uint16_t	u_int16_t;
typedef uint32_t	u_int32_t;
typedef uint64_t	u_int64_t;
#endif
typedef int64_t		sbintime_t;
typedef int64_t		__sbintime_t;
typedef uint32_t	__fixpt_t;
typedef __fixpt_t	fixpt_t;
typedef int		cpuwhich_t;
typedef int		cpulevel_t;
typedef int		cpusetid_t;
typedef unsigned long	__cpusetid_t;
typedef uint32_t	__accmode_t;
typedef __accmode_t	accmode_t;
typedef int		lwpid_t;

struct thread;
struct proc;
struct ucred;
struct malloc_type;
struct sbuf;
struct mbuf;
struct ifnet;

#endif /* _BSDKPI_SYS_TYPES_H_ */
