/* SPDX-License-Identifier: MIT */
/*
 * sys/epoch.h - epoch(9) on the LinuxKPI RCU approximation: readers are
 * compiler barriers, epoch_wait is synchronize_rcu().  See
 * docs/ARCHITECTURE.md for the trade-off.
 */
#ifndef _BSDKPI_SYS_EPOCH_H_
#define _BSDKPI_SYS_EPOCH_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <linux/rcupdate.h>

struct epoch;
typedef struct epoch *epoch_t;

struct epoch_tracker {
	int	et_dummy;
};
typedef struct epoch_tracker *epoch_tracker_t;

struct epoch_context {
	void	*data[2];
};
typedef struct epoch_context *epoch_context_t;
typedef void epoch_callback_t(epoch_context_t);

#define EPOCH_PREEMPT	0x1
#define EPOCH_LOCKED	0x2

extern epoch_t global_epoch;
extern epoch_t global_epoch_preempt;
extern epoch_t net_epoch_preempt;

static __inline epoch_t epoch_alloc(const char *name, int flags) { (void)name; (void)flags; return (global_epoch_preempt); }
static __inline void epoch_free(epoch_t epoch) { (void)epoch; }
static __inline void epoch_enter_preempt(epoch_t epoch, epoch_tracker_t et) { (void)epoch; (void)et; rcu_read_lock(); }
static __inline void epoch_exit_preempt(epoch_t epoch, epoch_tracker_t et) { (void)epoch; (void)et; rcu_read_unlock(); }
static __inline void epoch_enter(epoch_t epoch) { (void)epoch; rcu_read_lock(); }
static __inline void epoch_exit(epoch_t epoch) { (void)epoch; rcu_read_unlock(); }
static __inline void epoch_wait_preempt(epoch_t epoch) { (void)epoch; synchronize_rcu(); }
static __inline void epoch_wait(epoch_t epoch) { (void)epoch; synchronize_rcu(); }
static __inline void epoch_drain_callbacks(epoch_t epoch) { (void)epoch; synchronize_rcu(); }
static __inline int in_epoch(epoch_t epoch) { (void)epoch; return (1); }
static __inline int in_epoch_verbose(epoch_t epoch, int dump) { (void)epoch; (void)dump; return (1); }
void	epoch_call(epoch_t epoch, epoch_callback_t *cb, epoch_context_t ctx);

#define NET_EPOCH_ENTER(et)		epoch_enter_preempt(net_epoch_preempt, &(et))
#define NET_EPOCH_EXIT(et)		epoch_exit_preempt(net_epoch_preempt, &(et))
#define NET_EPOCH_WAIT()		epoch_wait_preempt(net_epoch_preempt)
#define NET_EPOCH_CALL(f, c)		epoch_call(net_epoch_preempt, (f), (c))
#define NET_EPOCH_DRAIN_CALLBACKS()	epoch_drain_callbacks(net_epoch_preempt)
#define NET_EPOCH_ASSERT()		do { } while (0)
#define NET_EPOCH_UNLOCK_ASSERT()	do { } while (0)
#define NET_EPOCH_IS_LOCKED()		(1)
#define NET_EPOCH_ENTER_ET		NET_EPOCH_ENTER
#define NET_EPOCH_EXIT_ET		NET_EPOCH_EXIT
#endif /* _BSDKPI_SYS_EPOCH_H_ */
