/* SPDX-License-Identifier: MIT */
/* net/vnet.h - VIMAGE is off: one network stack, the macros are inert. */
#ifndef _BSDKPI_NET_VNET_H_
#define _BSDKPI_NET_VNET_H_
#include <sys/cdefs.h>
#include <sys/types.h>

struct vnet {
	int	vnet_dummy;
};
extern struct vnet *vnet0;

#define curvnet			vnet0
#define CURVNET_SET(arg)	do { (void)(arg); } while (0)
#define CURVNET_SET_QUIET(arg)	do { (void)(arg); } while (0)
#define CURVNET_RESTORE()	do { } while (0)
#define CURVNET_ASSERT_SET()	do { } while (0)
#define VNET_ASSERT(exp, msg)	do { } while (0)
#define VNET_DEFINE(t, n)	t n
#define VNET_DEFINE_STATIC(t, n) static t n
#define VNET_DECLARE(t, n)	extern t n
#define VNET(n)			(n)
#define VNET_PTR(n)		(&(n))
#define VNET_VNET(vnet, n)	(n)
#define VNET_VNET_PTR(vnet, n)	(&(n))
#define VNET_ITERATOR_DECL(arg)	struct vnet *arg __unused
#define VNET_FOREACH(arg)	for ((arg) = vnet0; (arg) != NULL; (arg) = NULL)
#define VNET_LIST_RLOCK()	do { } while (0)
#define VNET_LIST_RUNLOCK()	do { } while (0)
#define VNET_LIST_RLOCK_NOSLEEP() do { } while (0)
#define VNET_LIST_RUNLOCK_NOSLEEP() do { } while (0)
#define IS_DEFAULT_VNET(arg)	(1)
#define VNET_SYSINIT(ident, subsystem, order, func, arg) \
	SYSINIT(ident, subsystem, order, func, arg)
#define VNET_SYSUNINIT(ident, subsystem, order, func, arg) \
	SYSUNINIT(ident, subsystem, order, func, arg)
#define VNET_PCPUSTAT_DEFINE(type, name) type name
#define VNET_PCPUSTAT_DECLARE(type, name) extern type name
#define TD_TO_VNET(td)		vnet0
#define P_TO_VNET(p)		vnet0
#define SYSCTL_VNET_INT		SYSCTL_INT
#define SYSCTL_VNET_UINT	SYSCTL_UINT
#define SYSCTL_VNET_PROC	SYSCTL_PROC
#define SYSCTL_VNET_STRING	SYSCTL_STRING
#define SYSCTL_VNET_STRUCT	SYSCTL_STRUCT
#endif
