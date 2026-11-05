/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_SYS_LINKER_H_
#define _BSDKPI_SYS_LINKER_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/linker_set.h>

struct linker_file;
typedef struct linker_file *linker_file_t;
struct thread;

static __inline int
kern_kldload(struct thread *td, const char *file, int *fileid)
{
	(void)td;
	(void)file;
	if (fileid != NULL)
		*fileid = 0;
	return (ENOENT);
}

static __inline int
kern_kldunload(struct thread *td, int fileid, int flags)
{
	(void)td;
	(void)fileid;
	(void)flags;
	return (0);
}

static __inline int
linker_reference_module(const char *modname, void *verinfo,
    linker_file_t *result)
{
	(void)modname;
	(void)verinfo;
	*result = NULL;
	return (ENOENT);
}

static __inline int
linker_release_module(const char *modname, void *verinfo, linker_file_t lf)
{
	(void)modname;
	(void)verinfo;
	(void)lf;
	return (0);
}
#endif
