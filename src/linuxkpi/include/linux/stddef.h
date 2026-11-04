/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_STDDEF_H_
#define _LKPI_LINUX_STDDEF_H_
#include <stddef.h>
#include <linux/compiler_types.h>

#define sizeof_field(T, m)	sizeof(((T *)0)->m)
#define offsetofend(T, m)	(offsetof(T, m) + sizeof_field(T, m))

/*
 * struct_group(): wrap a set of members in an anonymous union so they can be
 * addressed as a block.  Identical to upstream's definition.
 */
#define __struct_group(TAG, NAME, ATTRS, MEMBERS...) \
	union { \
		struct { MEMBERS } ATTRS; \
		struct TAG { MEMBERS } ATTRS NAME; \
	} ATTRS
#define struct_group(NAME, MEMBERS...) \
	__struct_group(/* no tag */, NAME, /* no attrs */, MEMBERS)
#define struct_group_attr(NAME, ATTRS, MEMBERS...) \
	__struct_group(/* no tag */, NAME, ATTRS, MEMBERS)
#define struct_group_tagged(TAG, NAME, MEMBERS...) \
	__struct_group(TAG, NAME, /* no attrs */, MEMBERS)

#define DECLARE_FLEX_ARRAY(TYPE, NAME) \
	struct { \
		struct { } __empty_ ## NAME; \
		TYPE NAME[]; \
	}
#define TRAILING_OVERLAP(TYPE, NAME, FAM, MEMBERS...) \
	union { TYPE NAME; struct { unsigned char __offset_to_##FAM[offsetof(TYPE, FAM)]; MEMBERS }; }

#endif
