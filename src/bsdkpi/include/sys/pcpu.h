/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_SYS_PCPU_H_
#define _BSDKPI_SYS_PCPU_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <lkpi/os.h>
#define MAXCPU		64
#define curcpu		(lkpi_os_cpu_id())
#define PCPU_GET(member)	(bsdkpi_pcpu_##member())
static __inline int bsdkpi_pcpu_cpuid(void) { return (lkpi_os_cpu_id()); }
extern int mp_ncpus;
extern int mp_maxid;
#define CPU_FOREACH(i)	for ((i) = 0; (i) < mp_ncpus; (i)++)
#define CPU_ABSENT(i)	(0)
#define DPCPU_DEFINE(t, n)	t n
#define DPCPU_DECLARE(t, n)	extern t n
#define DPCPU_GET(n)		(n)
#define DPCPU_SET(n, v)		((n) = (v))
#define DPCPU_PTR(n)		(&(n))
#define DPCPU_ID_GET(i, n)	(n)
#define DPCPU_ID_PTR(i, n)	(&(n))
#define DPCPU_DEFINE_STATIC(t, n) static t n
#endif
