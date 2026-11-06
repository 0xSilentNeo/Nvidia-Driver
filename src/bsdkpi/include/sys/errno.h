/* SPDX-License-Identifier: MIT */
/*
 * sys/errno.h - errno values.  The whole port uses Linux's numbers (see
 * linux/errno.h); the kext translates them to IOReturn at the IOKit
 * boundary.  FreeBSD-only names get FreeBSD's values where Linux has none.
 */
#ifndef _BSDKPI_SYS_ERRNO_H_
#define _BSDKPI_SYS_ERRNO_H_
#include <linux/errno.h>

#ifndef ENOTBLK
#define ENOTBLK		15
#endif
#ifndef ETXTBSY
#define ETXTBSY		26
#endif
#ifndef EDESTADDRREQ
#define EDESTADDRREQ	89
#endif
#ifndef EPROTOTYPE
#define EPROTOTYPE	91
#endif
#ifndef ENOPROTOOPT
#define ENOPROTOOPT	92
#endif
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT	94
#endif
#ifndef EPFNOSUPPORT
#define EPFNOSUPPORT	96
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT	97
#endif
#ifndef ENETRESET
#define ENETRESET	102
#endif
#ifndef ETOOMANYREFS
#define ETOOMANYREFS	109
#endif
#ifndef ELOOP
#define ELOOP		40
#endif
#ifndef ENOLCK
#define ENOLCK		37
#endif
#ifndef ENOMSG
#define ENOMSG		42
#endif
#ifndef EIDRM
#define EIDRM		43
#endif
#ifndef ESTALE
#define ESTALE		116
#endif
#ifndef EREMOTE
#define EREMOTE		66
#endif
#ifndef EDQUOT
#define EDQUOT		122
#endif
#ifndef EMULTIHOP
#define EMULTIHOP	72
#endif
#ifndef ENOLINK
#define ENOLINK		67
#endif
#ifndef EOWNERDEAD
#define EOWNERDEAD	130
#endif
#ifndef ENOTRECOVERABLE
#define ENOTRECOVERABLE	131
#endif
#ifndef ENOATTR
#define ENOATTR		ENODATA
#endif
/* FreeBSD-only, no Linux equivalent: FreeBSD's own numbers. */
#define EPROCLIM	67000
#define EBADRPC		72000
#define ERPCMISMATCH	73000
#define EPROGUNAVAIL	74000
#define EPROGMISMATCH	75000
#define EPROCUNAVAIL	76000
#define EFTYPE		79000
#define EAUTH		80000
#define ENEEDAUTH	81000
#define EDOOFUS		88000
#define ENOTCAPABLE	93000
#define ECAPMODE	94000
#define EINTEGRITY	97000
#define ELAST		97000
/* pseudo-errors returned inside the kernel */
#define ERESTART	(-1)
#define EJUSTRETURN	(-2)
#define ENOIOCTL	(-3)
#define EDIRIOCTL	(-4)
#define ERELOOKUP	(-5)
#endif /* _BSDKPI_SYS_ERRNO_H_ */
