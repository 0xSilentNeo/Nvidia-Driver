/* SPDX-License-Identifier: MIT */
/*
 * sys/kassert.h - KASSERT and friends.  lkpi/bsd.h provides KASSERT, MPASS,
 * CTASSERT and panic (always on, there is no INVARIANTS build switch here).
 */
#ifndef _BSDKPI_SYS_KASSERT_H_
#define _BSDKPI_SYS_KASSERT_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <lkpi/bsd.h>

#ifndef KASSERT
#define KASSERT(exp, msg) do {						\
	if (__predict_false(!(exp)))					\
		lkpi_os_panic msg;					\
} while (0)
#endif
#ifndef MPASS
#define MPASS(ex)		MPASS4(ex, #ex, __FILE__, __LINE__)
#endif
#define MPASS2(ex, what)	MPASS4(ex, what, __FILE__, __LINE__)
#define MPASS3(ex, file, line)	MPASS4(ex, #ex, file, line)
#define MPASS4(ex, what, file, line) \
	KASSERT((ex), ("Assertion %s failed at %s:%d", what, file, line))
#ifndef CTASSERT
#define CTASSERT(x)		_Static_assert((x), #x)
#endif
#define __assert_unreachable() \
	panic("executing segment marked as unreachable at %s:%d (%s)\n", \
	    __FILE__, __LINE__, __func__)
#define VNASSERT(exp, vp, msg)	KASSERT(exp, msg)
#define VNPASS(exp, vp)		KASSERT(exp, (#exp))
#endif
