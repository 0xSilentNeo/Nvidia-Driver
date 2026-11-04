/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_ERR_H_
#define _LKPI_LINUX_ERR_H_
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/errno.h>

#define MAX_ERRNO	4095
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

static inline void * __must_check ERR_PTR(long error) { return (void *)error; }
static inline long __must_check PTR_ERR(const void *ptr) { return (long)ptr; }
static inline bool __must_check IS_ERR(const void *ptr) { return IS_ERR_VALUE((unsigned long)ptr); }
static inline bool __must_check IS_ERR_OR_NULL(const void *ptr)
{
	return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}
static inline void * __must_check ERR_CAST(const void *ptr) { return (void *)ptr; }
static inline int __must_check PTR_ERR_OR_ZERO(const void *ptr)
{
	return IS_ERR(ptr) ? (int)PTR_ERR(ptr) : 0;
}
#endif
