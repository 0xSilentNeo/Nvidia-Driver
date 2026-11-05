/* SPDX-License-Identifier: MIT */
/* sys/priv.h - priv(9): the caller is always the kernel here, checks pass. */
#ifndef _BSDKPI_SYS_PRIV_H_
#define _BSDKPI_SYS_PRIV_H_
#include <sys/cdefs.h>
#include <sys/types.h>

#define PRIV_DRIVER		14
#define PRIV_NET_BRIDGE		390
#define PRIV_NET_IFCREATE	397
#define PRIV_NET_IFDESTROY	398
#define PRIV_NET_SETIFFLAGS	406
#define PRIV_NET_SETIFMTU	405
#define PRIV_NET_SETIFMEDIA	404
#define PRIV_NET_SETLLADDR	420
#define PRIV_NET80211_VAP_GETKEY	440
#define PRIV_NET80211_VAP_MANAGE	441
#define PRIV_NET80211_VAP_SETMAC	442
#define PRIV_NET80211_CREATE_VAP	443
#define _PRIV_HIGHEST		800

struct thread;
struct ucred;

static __inline int
priv_check(struct thread *td, int priv)
{
	(void)td;
	(void)priv;
	return (0);
}

static __inline int
priv_check_cred(struct ucred *cred, int priv)
{
	(void)cred;
	(void)priv;
	return (0);
}
#endif
