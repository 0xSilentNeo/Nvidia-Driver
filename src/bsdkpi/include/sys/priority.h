/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_SYS_PRIORITY_H_
#define _BSDKPI_SYS_PRIORITY_H_
#define PRI_ITHD	1
#define PRI_REALTIME	2
#define PRI_TIMESHARE	3
#define PRI_IDLE	4
#define PRI_MIN		0
#define PRI_MAX		255
#define PRI_MIN_ITHD	PRI_MIN
#define PRI_MAX_ITHD	(PRI_MIN_REALTIME - 1)
#define PI_REALTIME	(PRI_MIN_ITHD + 0)
#define PI_INTR		(PRI_MIN_ITHD + 4)
#define PI_AV		PI_INTR
#define PI_NET		PI_INTR
#define PI_DISK		PI_INTR
#define PI_TTY		PI_INTR
#define PI_DULL		PI_INTR
#define PI_SOFT		(PRI_MIN_ITHD + 8)
#define PI_SOFTCLOCK	PI_SOFT
#define PI_SWI(x)	(PI_SOFT + (x) * RQ_PPQ)
#define PRI_MIN_REALTIME	48
#define PRI_MAX_REALTIME	(PRI_MIN_KERN - 1)
#define PRI_MIN_KERN	80
#define PRI_MAX_KERN	(PRI_MIN_TIMESHARE - 1)
#define PSWP		(PRI_MIN_KERN + 0)
#define PVM		(PRI_MIN_KERN + 4)
#define PINOD		(PRI_MIN_KERN + 8)
#define PRIBIO		(PRI_MIN_KERN + 12)
#define PVFS		(PRI_MIN_KERN + 16)
#define PZERO		(PRI_MIN_KERN + 20)
#define PSOCK		(PRI_MIN_KERN + 24)
#define PWAIT		(PRI_MIN_KERN + 28)
#define PLOCK		(PRI_MIN_KERN + 32)
#define PPAUSE		(PRI_MIN_KERN + 36)
#define PRI_MIN_TIMESHARE	120
#define PRI_MAX_TIMESHARE	(PRI_MIN_IDLE - 1)
#define PUSER		(PRI_MIN_TIMESHARE)
#define PRI_MIN_IDLE	224
#define PRI_MAX_IDLE	PRI_MAX
#define RQ_PPQ		4
#endif
