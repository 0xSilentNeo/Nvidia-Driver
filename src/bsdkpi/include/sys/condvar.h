/* SPDX-License-Identifier: MIT */
/*
 * sys/condvar.h - condvar(9).  cv_wait releases the caller's mutex and
 * sleeps on the condvar address through the sleep channel machinery in
 * src/misc.c (the same path mtx_sleep/wakeup use).
 */
#ifndef _BSDKPI_SYS_CONDVAR_H_
#define _BSDKPI_SYS_CONDVAR_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/mutex.h>

struct cv {
	const char	*cv_description;
	int		cv_waiters;
};

void	cv_init(struct cv *cvp, const char *desc);
void	cv_destroy(struct cv *cvp);
void	cv_wait(struct cv *cvp, struct mtx *mtx);
void	cv_wait_unlock(struct cv *cvp, struct mtx *mtx);
int	cv_wait_sig(struct cv *cvp, struct mtx *mtx);
int	cv_timedwait(struct cv *cvp, struct mtx *mtx, int timo);
int	cv_timedwait_sig(struct cv *cvp, struct mtx *mtx, int timo);
int	cv_timedwait_sbt(struct cv *cvp, struct mtx *mtx, sbintime_t sbt,
	    sbintime_t pr, int flags);
void	cv_signal(struct cv *cvp);
void	cv_broadcastpri(struct cv *cvp, int pri);
#define cv_broadcast(cvp)	cv_broadcastpri((cvp), 0)
#define cv_wmesg(cvp)		((cvp)->cv_description)
#define cv_timedwait_sig_sbt(cv, m, sbt, pr, f) cv_timedwait_sbt((cv), (m), (sbt), (pr), (f))
#endif /* _BSDKPI_SYS_CONDVAR_H_ */
