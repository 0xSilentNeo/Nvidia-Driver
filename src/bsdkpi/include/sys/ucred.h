/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_SYS_UCRED_H_
#define _BSDKPI_SYS_UCRED_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#ifndef __APPLE__
typedef uint32_t	bsdkpi_uid_t;
typedef uint32_t	bsdkpi_gid_t;
#else
typedef uid_t		bsdkpi_uid_t;
typedef gid_t		bsdkpi_gid_t;
#endif
struct ucred {
	u_int		cr_ref;
	bsdkpi_uid_t	cr_uid;
	bsdkpi_uid_t	cr_ruid;
	bsdkpi_uid_t	cr_svuid;
	bsdkpi_gid_t	cr_rgid;
	bsdkpi_gid_t	cr_svgid;
	int		cr_ngroups;
	bsdkpi_gid_t	cr_groups[16];
	u_int		cr_flags;
	void		*cr_prison;
};
#define NOCRED		((struct ucred *)0)
#define FSCRED		((struct ucred *)-1)
#define CRED_FLAG_CAPMODE 0x00000001
#define cr_gid		cr_groups[0]
static __inline int groupmember(bsdkpi_gid_t gid, struct ucred *cred) { (void)gid; (void)cred; return (1); }
static __inline struct ucred *crhold(struct ucred *cr) { return (cr); }
static __inline void crfree(struct ucred *cr) { (void)cr; }
#endif
