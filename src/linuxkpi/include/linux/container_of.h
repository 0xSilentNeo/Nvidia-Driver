/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_CONTAINER_OF_H_
#define _LKPI_LINUX_CONTAINER_OF_H_
#include <linux/build_bug.h>
#include <linux/stddef.h>
#define container_of(ptr, type, member) ({ \
	void *__mptr = (void *)(ptr); \
	_Static_assert(__same_type(*(ptr), ((type *)0)->member) || \
		       __same_type(*(ptr), void), \
		       "pointer type mismatch in container_of()"); \
	((type *)(__mptr - offsetof(type, member))); })
#define container_of_const(ptr, type, member) container_of(ptr, type, member)
#define container_of_safe(ptr, type, member) ({ \
	void *__mptr = (void *)(ptr); \
	IS_ERR_OR_NULL(__mptr) ? ((type *)__mptr) : \
	((type *)(__mptr - offsetof(type, member))); })
#endif
