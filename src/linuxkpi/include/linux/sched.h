/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_SCHED_H_
#define _LKPI_LINUX_SCHED_H_
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <lkpi/os.h>

struct task_struct {
	lkpi_os_thread_t os;
	int (*fn)(void *);
	void *data;
	char comm[16];
	unsigned long state;		/* TASK_* */
	unsigned long flags;		/* KTHREAD_* bits in kthread.c */
	struct completion *exited;
	int exit_code;
	int pid;
	struct list_head entry;
};
#define TASK_COMM_LEN 16

struct task_struct *lkpi_current(void);
#define current lkpi_current()

void __set_current_state(long state);
#define set_current_state(s) __set_current_state(s)
void schedule(void);
long schedule_timeout(long timeout);
long schedule_timeout_interruptible(long timeout);
long schedule_timeout_uninterruptible(long timeout);
static inline void cond_resched(void) { lkpi_os_thread_yield(); }
#define cond_resched_lock(l) ({ spin_unlock(l); cond_resched(); spin_lock(l); 1; })
#define need_resched() (0)
#define yield() lkpi_os_thread_yield()
static inline void io_schedule(void) { schedule(); }
static inline int signal_pending(struct task_struct *p) { (void)p; return 0; }
static inline int fatal_signal_pending(struct task_struct *p) { (void)p; return 0; }
static inline int signal_pending_state(long state, struct task_struct *p) { (void)state; (void)p; return 0; }
static inline void sched_set_fifo_low(struct task_struct *p) { lkpi_os_thread_set_realtime(p->os, true); }
static inline void sched_set_fifo(struct task_struct *p) { lkpi_os_thread_set_realtime(p->os, true); }
static inline void sched_set_normal(struct task_struct *p, int nice) { (void)nice; lkpi_os_thread_set_realtime(p->os, false); }
#define task_pid_nr(p) ((p)->pid)
#define get_task_comm(buf, tsk) strscpy(buf, (tsk)->comm, sizeof(buf))
#define cpu_relax() lkpi_os_thread_yield()
#define num_online_cpus() lkpi_os_cpu_count()
#define num_possible_cpus() lkpi_os_cpu_count()
#define smp_processor_id() lkpi_os_cpu_id()
#define raw_smp_processor_id() lkpi_os_cpu_id()
#define get_cpu() lkpi_os_cpu_id()
#define put_cpu() do { } while (0)
#define PF_KTHREAD 0x00200000
#define MAX_SCHEDULE_TIMEOUT LONG_MAX
#endif
