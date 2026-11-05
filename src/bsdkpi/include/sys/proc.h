/* SPDX-License-Identifier: MIT */
/*
 * sys/proc.h - just enough of struct thread for priv(9) and the ioctl
 * paths.  There is one static thread descriptor; curthread always points
 * to it.
 */
#ifndef _BSDKPI_SYS_PROC_H_
#define _BSDKPI_SYS_PROC_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/ucred.h>
#include <sys/priority.h>
#include <sys/time.h>

struct proc;

struct thread {
	struct proc	*td_proc;
	struct ucred	*td_ucred;
	lwpid_t		 td_tid;
	int		 td_flags;
	int		 td_pflags;
	int		 td_intr_nesting_level;
	char		 td_name[MAXCOMLEN + 1];
};

struct proc {
	int		 p_pid;
	int		 p_flag;
	struct ucred	*p_ucred;
	struct thread	*p_singlethread;
	char		 p_comm[MAXCOMLEN + 1];
};

extern struct thread	bsdkpi_thread0;
extern struct proc	bsdkpi_proc0;
#define curthread	(&bsdkpi_thread0)
#define curproc		(&bsdkpi_proc0)
#define thread0		bsdkpi_thread0
#define proc0		bsdkpi_proc0
#define TD_IS_IDLETHREAD(td)	(0)
#define TD_IS_RUNNING(td)	(1)
#define P_KPROC		0x00004
#define TDP_NOFAULTING	0x00000400
#define TDP_KTHREAD	0x00200000

static __inline int
p_candebug(struct thread *td, struct proc *p)
{
	(void)td;
	(void)p;
	return (0);
}

static __inline void
kthread_exit(void)
{
}

static __inline int
kproc_create(void (*func)(void *), void *arg, struct proc **newpp, int flags,
    int pages, const char *fmt, ...)
{
	(void)func; (void)arg; (void)newpp; (void)flags; (void)pages; (void)fmt;
	return (ENOSYS);
}
#endif /* _BSDKPI_SYS_PROC_H_ */
