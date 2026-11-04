/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_LOCKDEP_H_
#define _LKPI_LINUX_LOCKDEP_H_
struct lock_class_key { };
struct lockdep_map { };
#define lockdep_assert_held(l)		do { (void)(l); } while (0)
#define lockdep_assert_held_once(l)	do { (void)(l); } while (0)
#define lockdep_assert_not_held(l)	do { (void)(l); } while (0)
#define lockdep_assert_held_write(l)	do { (void)(l); } while (0)
#define lockdep_assert_held_read(l)	do { (void)(l); } while (0)
#define lockdep_assert(c)		do { } while (0)
#define lockdep_assert_once(c)		do { } while (0)
#define lockdep_assert_in_softirq()	do { } while (0)
#define lockdep_assert_irqs_disabled()	do { } while (0)
#define lockdep_is_held(l)		(1)
#define lockdep_set_class(l, k)		do { (void)(l); (void)(k); } while (0)
#define lockdep_set_class_and_name(l, k, n) do { } while (0)
#define lockdep_set_subclass(l, s)	do { } while (0)
#define lockdep_init_map(m, n, k, s)	do { } while (0)
#define lockdep_register_key(k)		do { } while (0)
#define lockdep_unregister_key(k)	do { } while (0)
#define lock_acquire(...)		do { } while (0)
#define lock_release(...)		do { } while (0)
#define SINGLE_DEPTH_NESTING 1
#define lockdep_off() do { } while (0)
#define lockdep_on() do { } while (0)
#endif
