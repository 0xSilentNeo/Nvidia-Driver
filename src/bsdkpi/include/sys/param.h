/* SPDX-License-Identifier: MIT */
/*
 * sys/param.h - the FreeBSD kernel's "everything" header.
 */
#ifndef _BSDKPI_SYS_PARAM_H_
#define _BSDKPI_SYS_PARAM_H_

#define BSD		199506
#define BSD4_3		1
#define BSD4_4		1
#define __FreeBSD_version	1500000

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/limits.h>
#include <sys/time.h>
#include <sys/priority.h>
#include <sys/syslimits.h>

#ifndef LOCORE
#include <sys/queue.h>
#endif

#define MAXCOMLEN	19
#define MAXINTERP	32
#define MAXLOGNAME	33
#define MAXUPRC		100
#define NCARGS		262144
#define NGROUPS		1023
#define NOFILE		1024
#define NOGROUP		65535
#define MAXHOSTNAMELEN	256
#define SPECNAMELEN	255
#define MAXPATHLEN	1024

#define NBBY		8
#define NBPW		sizeof(int)

#define CMASK		022
#define NODEV		(dev_t)(-1)

#ifndef PAGE_SHIFT
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1 << PAGE_SHIFT)
#define PAGE_MASK	(PAGE_SIZE - 1)
#endif
#define DEV_BSHIFT	9
#define DEV_BSIZE	(1 << DEV_BSHIFT)
#define BLKDEV_IOSIZE	PAGE_SIZE
#define MAXPHYS		(1024 * 1024)
#define MAXBSIZE	65536
#define MAXDUMPPGS	(MAXPHYS / PAGE_SIZE)

/* Priorities / sleep flags. */
#define PRIMASK		0x0ff
#define PCATCH		0x100
#define PDROP		0x200
#define PNOLOCK		0x400
#define PRILASTFLAG	0x400

#define NZERO		0

/*
 * ALIGN: FreeBSD's is the one-argument pointer-rounding macro; LinuxKPI's
 * linux/kernel.h has the two-argument ALIGN(x, a).  Both cannot coexist in
 * one translation unit and this header is the FreeBSD side, so the FreeBSD
 * meaning wins from here on (LinuxKPI headers that are already parsed are
 * unaffected; those parsed later must not use ALIGN(x, a)).
 */
#define ALIGNBYTES	(sizeof(void *) - 1)
#undef ALIGN
#define ALIGN(p)	(((uintptr_t)(p) + ALIGNBYTES) & ~ALIGNBYTES)
#define ALIGNED_POINTER(p, t)	((((uintptr_t)(p)) & (sizeof(t) - 1)) == 0)

#define btoc(x)		(((vm_offset_t)(x) + PAGE_MASK) >> PAGE_SHIFT)
#define ctob(x)		((x) << PAGE_SHIFT)
#define btodb(bytes)	((bytes) >> DEV_BSHIFT)
#define dbtob(db)	((db) << DEV_BSHIFT)

#ifndef howmany
#define howmany(x, y)	(((x) + ((y) - 1)) / (y))
#endif
#ifndef nitems
#define nitems(x)	(sizeof((x)) / sizeof((x)[0]))
#endif
#ifndef rounddown
#define rounddown(x, y)	(((x) / (y)) * (y))
#endif
#ifndef rounddown2
#define rounddown2(x, y) ((x) & ~((y) - 1))
#endif
#ifndef roundup
#define roundup(x, y)	((((x) + ((y) - 1)) / (y)) * (y))
#endif
#ifndef roundup2
#define roundup2(x, y)	(((x) + ((y) - 1)) & (~((y) - 1)))
#endif
#ifndef powerof2
#define powerof2(x)	((((x) - 1) & (x)) == 0)
#endif
#ifndef MIN
#define MIN(a, b)	(((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)	(((a) > (b)) ? (a) : (b))
#endif

#define CBLOCK		128
#define CBQSIZE		(CBLOCK / NBBY)
#define CBSIZE		(CBLOCK - sizeof(struct cblockhead))
#define CROUND		(CBLOCK - 1)

#define FSHIFT		11
#define FSCALE		(1 << FSHIFT)

#define dbtoc(db)	((db + (ctodb(1) - 1)) >> (PAGE_SHIFT - DEV_BSHIFT))
#define ctodb(db)	((db) << (PAGE_SHIFT - DEV_BSHIFT))

#define member2struct(s, m, x)	((struct s *)(void *)((char *)(x) - offsetof(struct s, m)))

#define SET_FOREACH_LIMIT	/* see sys/linker_set.h */

#endif /* _BSDKPI_SYS_PARAM_H_ */
