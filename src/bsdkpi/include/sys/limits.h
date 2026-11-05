/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_SYS_LIMITS_H_
#define _BSDKPI_SYS_LIMITS_H_
#include <sys/cdefs.h>
#include <limits.h>
#include <stdint.h>
#ifndef CHAR_BIT
#define CHAR_BIT	8
#endif
#ifndef QUAD_MAX
#define QUAD_MAX	INT64_MAX
#define QUAD_MIN	INT64_MIN
#define UQUAD_MAX	UINT64_MAX
#endif
#ifndef OFF_MAX
#define OFF_MAX		INT64_MAX
#define OFF_MIN		INT64_MIN
#endif
#ifndef SIZE_T_MAX
#define SIZE_T_MAX	SIZE_MAX
#endif
#ifndef SSIZE_MAX
#define SSIZE_MAX	((ssize_t)(SIZE_MAX >> 1))
#endif
#ifndef GID_MAX
#define GID_MAX		UINT32_MAX
#define UID_MAX		UINT32_MAX
#endif
#ifndef LONG_BIT
#define LONG_BIT	(CHAR_BIT * (int)sizeof(long))
#endif
#ifndef WORD_BIT
#define WORD_BIT	(CHAR_BIT * (int)sizeof(int))
#endif
#endif
