/* SPDX-License-Identifier: MIT */
/*
 * sys/kernel.h - SYSINIT(9) and the global clock variables.
 *
 * SYSINITs are recorded by constructors (sys/linker_set.h explains why) and
 * run, sorted by (subsystem, order), from net80211_register_builtin_modules()
 * -- which lkpi_runtime_init() calls once the LinuxKPI runtime (work queues,
 * timers) is up.  DECLARE_MODULE() is a SYSINIT that sends MOD_LOAD to the
 * module's event handler; see sys/module.h.
 */
#ifndef _BSDKPI_SYS_KERNEL_H_
#define _BSDKPI_SYS_KERNEL_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/linker_set.h>
#include <linux/jiffies.h>

extern int hz;			/* HZ, 100 */
extern int tick;		/* microseconds per tick */
extern int bootverbose;
extern int cold;
extern char kernelname[];
extern const char *panicstr;
extern int ngroups_max;
extern int maxproc, maxusers, maxfiles;
extern unsigned long physmem;

#define hz_assert_100	_Static_assert(HZ == 100, "FreeBSD ticks and jiffies share HZ")

enum sysinit_sub_id {
	SI_SUB_DUMMY		= 0x0000000,
	SI_SUB_DONE		= 0x0000001,
	SI_SUB_TUNABLES		= 0x0700000,
	SI_SUB_COPYRIGHT	= 0x0800001,
	SI_SUB_VM		= 0x1000000,
	SI_SUB_COUNTER		= 0x1100000,
	SI_SUB_KMEM		= 0x1800000,
	SI_SUB_HYPERVISOR	= 0x1A40000,
	SI_SUB_WITNESS		= 0x1A80000,
	SI_SUB_MTX_POOL_DYNAMIC	= 0x1AC0000,
	SI_SUB_LOCK		= 0x1B00000,
	SI_SUB_EVENTHANDLER	= 0x1C00000,
	SI_SUB_VNET_PRELINK	= 0x1E00000,
	SI_SUB_KLD		= 0x2000000,
	SI_SUB_CPU		= 0x2100000,
	SI_SUB_RACCT		= 0x2110000,
	SI_SUB_KDTRACE		= 0x2140000,
	SI_SUB_RANDOM		= 0x2160000,
	SI_SUB_MAC		= 0x2180000,
	SI_SUB_MAC_POLICY	= 0x21C0000,
	SI_SUB_MAC_LATE		= 0x21D0000,
	SI_SUB_VNET		= 0x21E0000,
	SI_SUB_INTRINSIC	= 0x2200000,
	SI_SUB_VM_CONF		= 0x2300000,
	SI_SUB_DDB_SERVICES	= 0x2380000,
	SI_SUB_RUN_QUEUE	= 0x2400000,
	SI_SUB_KTRACE		= 0x2480000,
	SI_SUB_OPENSOLARIS	= 0x2490000,
	SI_SUB_AUDIT		= 0x24C0000,
	SI_SUB_CREATE_INIT	= 0x2500000,
	SI_SUB_SCHED_IDLE	= 0x2600000,
	SI_SUB_MBUF		= 0x2700000,
	SI_SUB_INTR		= 0x2800000,
	SI_SUB_TASKQ		= 0x2880000,
	SI_SUB_EPOCH		= 0x2888000,
	SI_SUB_SMP		= 0x2900000,
	SI_SUB_SOFTINTR		= 0x2A00000,
	SI_SUB_DEVFS		= 0x2F00000,
	SI_SUB_INIT_IF		= 0x3000000,
	SI_SUB_NETGRAPH		= 0x3010000,
	SI_SUB_DTRACE		= 0x3020000,
	SI_SUB_DTRACE_PROVIDER	= 0x3048000,
	SI_SUB_DTRACE_ANON	= 0x308C000,
	SI_SUB_DRIVERS		= 0x3100000,
	SI_SUB_CONFIGURE	= 0x3800000,
	SI_SUB_VFS		= 0x4000000,
	SI_SUB_CLOCKS		= 0x4800000,
	SI_SUB_SYSV_SHM		= 0x6400000,
	SI_SUB_SYSV_SEM		= 0x6800000,
	SI_SUB_SYSV_MSG		= 0x6C00000,
	SI_SUB_P1003_1B		= 0x6E00000,
	SI_SUB_PSEUDO		= 0x7000000,
	SI_SUB_EXEC		= 0x7400000,
	SI_SUB_PROTO_BEGIN	= 0x8000000,
	SI_SUB_PROTO_PFIL	= 0x8100000,
	SI_SUB_PROTO_IF		= 0x8400000,
	SI_SUB_PROTO_DOMAININIT	= 0x8600000,
	SI_SUB_PROTO_MC		= 0x8700000,
	SI_SUB_PROTO_DOMAIN	= 0x8800000,
	SI_SUB_PROTO_FIREWALL	= 0x8806000,
	SI_SUB_PROTO_IFATTACHDOMAIN = 0x8808000,
	SI_SUB_PROTO_END	= 0x8ffffff,
	SI_SUB_KPROF		= 0x9000000,
	SI_SUB_KICK_SCHEDULER	= 0xa000000,
	SI_SUB_INT_CONFIG_HOOKS	= 0xa800000,
	SI_SUB_ROOT_CONF	= 0xb000000,
	SI_SUB_INTRINSIC_POST	= 0xd000000,
	SI_SUB_SYSCALLS		= 0xd800000,
	SI_SUB_VNET_DONE	= 0xdc00000,
	SI_SUB_KTHREAD_INIT	= 0xe000000,
	SI_SUB_KTHREAD_PAGE	= 0xe400000,
	SI_SUB_KTHREAD_VM	= 0xe800000,
	SI_SUB_KTHREAD_BUF	= 0xea00000,
	SI_SUB_KTHREAD_UPDATE	= 0xec00000,
	SI_SUB_KTHREAD_IDLE	= 0xee00000,
	SI_SUB_SMP_LATE		= 0xf000000,
	SI_SUB_RACCTD		= 0xf100000,
	SI_SUB_LAST		= 0xfffffff
};

enum sysinit_elem_order {
	SI_ORDER_FIRST		= 0x0000000,
	SI_ORDER_SECOND		= 0x0000001,
	SI_ORDER_THIRD		= 0x0000002,
	SI_ORDER_FOURTH		= 0x0000003,
	SI_ORDER_FIFTH		= 0x0000004,
	SI_ORDER_SIXTH		= 0x0000005,
	SI_ORDER_SEVENTH	= 0x0000006,
	SI_ORDER_EIGHTH		= 0x0000007,
	SI_ORDER_MIDDLE		= 0x1000000,
	SI_ORDER_ANY		= 0xf000000
};

typedef void (*sysinit_nfunc_t)(void *);
typedef void (*sysinit_cfunc_t)(const void *);

struct sysinit {
	enum sysinit_sub_id	subsystem;
	enum sysinit_elem_order	order;
	sysinit_cfunc_t		func;
	const void		*udata;
	const char		*name;
	struct sysinit		*next;
	int			flags;		/* BSDKPI_SYSINIT_* */
};
#define BSDKPI_SYSINIT_UNINIT	0x1	/* a SYSUNINIT entry */
#define BSDKPI_SYSINIT_DONE	0x2	/* has run */

void	bsdkpi_sysinit_register(struct sysinit *si);

#define C_SYSINIT(uniquifier, subsystem, order, func, ident)		\
	static struct sysinit uniquifier##_sys_init = {			\
		(subsystem), (order), (func), (ident),			\
		#uniquifier, NULL, 0					\
	};								\
	__attribute__((constructor)) static void			\
	uniquifier##_sys_init_ctor(void)				\
	{								\
		bsdkpi_sysinit_register(&uniquifier##_sys_init);	\
	}
#define SYSINIT(uniquifier, subsystem, order, func, ident)		\
	C_SYSINIT(uniquifier, subsystem, order,				\
	    (sysinit_cfunc_t)(uintptr_t)(sysinit_nfunc_t)(func), (ident))

#define C_SYSUNINIT(uniquifier, subsystem, order, func, ident)		\
	static struct sysinit uniquifier##_sys_uninit = {		\
		(subsystem), (order), (func), (ident),			\
		#uniquifier, NULL, BSDKPI_SYSINIT_UNINIT		\
	};								\
	__attribute__((constructor)) static void			\
	uniquifier##_sys_uninit_ctor(void)				\
	{								\
		bsdkpi_sysinit_register(&uniquifier##_sys_uninit);	\
	}
#define SYSUNINIT(uniquifier, subsystem, order, func, ident)		\
	C_SYSUNINIT(uniquifier, subsystem, order,			\
	    (sysinit_cfunc_t)(uintptr_t)(sysinit_nfunc_t)(func), (ident))

/* Tunables: kenv does not exist, values keep their compile-time defaults. */
#define TUNABLE_INT(path, var)
#define TUNABLE_INT_FETCH(path, var)		(0)
#define TUNABLE_UINT(path, var)
#define TUNABLE_UINT_FETCH(path, var)		(0)
#define TUNABLE_LONG(path, var)
#define TUNABLE_LONG_FETCH(path, var)		(0)
#define TUNABLE_ULONG(path, var)
#define TUNABLE_ULONG_FETCH(path, var)		(0)
#define TUNABLE_INT64(path, var)
#define TUNABLE_INT64_FETCH(path, var)		(0)
#define TUNABLE_UINT64(path, var)
#define TUNABLE_UINT64_FETCH(path, var)		(0)
#define TUNABLE_QUAD(path, var)
#define TUNABLE_QUAD_FETCH(path, var)		(0)
#define TUNABLE_UQUAD(path, var)
#define TUNABLE_UQUAD_FETCH(path, var)		(0)
#define TUNABLE_STR(path, var, size)
#define TUNABLE_STR_FETCH(path, var, size)	(0)
#define TUNABLE_BOOL(path, var)
#define TUNABLE_BOOL_FETCH(path, var)		(0)

/* Entry points for the runtime (src/modules.c). */
int	net80211_register_builtin_modules(void);
void	net80211_unregister_builtin_modules(void);

#endif /* _BSDKPI_SYS_KERNEL_H_ */
