/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_TYPECHECK_H_
#define _LKPI_LINUX_TYPECHECK_H_
#define typecheck(type, x) \
({	type __dummy; \
	typeof(x) __dummy2; \
	(void)(&__dummy == &__dummy2); \
	1; \
})
#define typecheck_fn(type, function) \
({	typeof(type) __tmp = function; \
	(void)__tmp; \
})
#endif
