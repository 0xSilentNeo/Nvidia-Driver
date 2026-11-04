/* SPDX-License-Identifier: MIT */
/*
 * RCU approximation.
 *
 * XNU has no RCU and no public epoch mechanism.  Readers are therefore
 * unsynchronised (rcu_read_lock() is a compiler barrier) and writers wait
 * for a conservative grace period in synchronize_rcu(): every thread that
 * could be inside a read-side section (the IRQ workloop, the NAPI and
 * tasklet threads, the workqueues and the mt76 TX worker) is made to pass a
 * quiescent point by the implementation in src/linuxkpi/src/rcu.c, which
 * also drives kfree_rcu()/call_rcu() through a deferred list.
 *
 * This is the same trade-off FreeBSD made before ck_epoch landed, and is
 * flagged in docs/ARCHITECTURE.md as a known approximation.
 */
#ifndef _LKPI_LINUX_RCUPDATE_H_
#define _LKPI_LINUX_RCUPDATE_H_
#include <linux/types.h>
#include <linux/compiler.h>

#define rcu_read_lock()		barrier()
#define rcu_read_unlock()	barrier()
#define rcu_read_lock_bh()	barrier()
#define rcu_read_unlock_bh()	barrier()
#define rcu_read_lock_sched()	barrier()
#define rcu_read_unlock_sched()	barrier()
#define rcu_read_lock_held()	(1)
#define rcu_read_lock_bh_held()	(1)
#define rcu_read_lock_any_held() (1)
#define rcu_lockdep_assert(c, s) do { } while (0)
#define RCU_LOCKDEP_WARN(c, s)	do { } while (0)
#define rcu_sleep_check()	do { } while (0)
#define rcu_pointer_handoff(p)	(p)
#define rcu_barrier()		synchronize_rcu()
#define rcu_barrier_bh()	synchronize_rcu()
#define init_rcu_head(h)	do { } while (0)
#define destroy_rcu_head(h)	do { } while (0)

#define __rcu_dereference_check(p, c, space) \
	({ typeof(*p) *________p1 = (typeof(*p) *)READ_ONCE(p); ________p1; })
#define rcu_dereference(p)		__rcu_dereference_check(p, 1, __rcu)
#define rcu_dereference_bh(p)		rcu_dereference(p)
#define rcu_dereference_sched(p)	rcu_dereference(p)
#define rcu_dereference_check(p, c)	rcu_dereference(p)
#define rcu_dereference_protected(p, c)	((typeof(*p) *)(p))
#define rcu_dereference_raw(p)		rcu_dereference(p)
#define rcu_dereference_raw_check(p)	rcu_dereference(p)
#define rcu_access_pointer(p)		((typeof(*p) *)READ_ONCE(p))
#define rcu_assign_pointer(p, v)	smp_store_release(&(p), (v))
#define RCU_INIT_POINTER(p, v)		do { (p) = (v); } while (0)
#define RCU_POINTER_INITIALIZER(p, v)	.p = (v)
#define rcu_replace_pointer(rcu_ptr, ptr, c) ({ \
	typeof(ptr) __tmp = rcu_dereference_protected((rcu_ptr), (c)); \
	rcu_assign_pointer((rcu_ptr), (ptr)); __tmp; })
#define unrcu_pointer(p)		((typeof(*p) *)(p))
#define rcu_swap_protected(rcu_ptr, ptr, c) do { \
	typeof(ptr) __tmp = rcu_dereference_protected((rcu_ptr), (c)); \
	rcu_assign_pointer((rcu_ptr), (ptr)); (ptr) = __tmp; } while (0)

void synchronize_rcu(void);
#define synchronize_rcu_expedited() synchronize_rcu()
#define synchronize_net() synchronize_rcu()
#define synchronize_srcu(s) synchronize_rcu()
void call_rcu(struct rcu_head *head, rcu_callback_t func);
#define call_rcu_hurry(h, f) call_rcu(h, f)
void lkpi_kfree_rcu(struct rcu_head *head, size_t offset);
#define kfree_rcu(ptr, rhf...) \
	lkpi_kfree_rcu(&(ptr)->rhf, offsetof(typeof(*(ptr)), rhf))
#define kvfree_rcu(ptr, rhf...) kfree_rcu(ptr, rhf)
#define kfree_rcu_mightsleep(ptr) do { synchronize_rcu(); kfree(ptr); } while (0)
#define cond_synchronize_rcu(s) synchronize_rcu()
static inline unsigned long get_state_synchronize_rcu(void) { return 0; }

struct srcu_struct { int dummy; };
#define DEFINE_SRCU(n) struct srcu_struct n
#define init_srcu_struct(s) (0)
#define cleanup_srcu_struct(s) do { } while (0)
#define srcu_read_lock(s) (0)
#define srcu_read_unlock(s, i) do { } while (0)
#define srcu_dereference(p, s) rcu_dereference(p)
#define srcu_dereference_check(p, s, c) rcu_dereference(p)
#endif /* _LKPI_LINUX_RCUPDATE_H_ */
