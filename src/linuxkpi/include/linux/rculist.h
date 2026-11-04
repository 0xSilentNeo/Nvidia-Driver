/* SPDX-License-Identifier: MIT */
/* RCU list variants.  See rcupdate.h for how RCU is approximated here. */
#ifndef _LKPI_LINUX_RCULIST_H_
#define _LKPI_LINUX_RCULIST_H_
#include <linux/list.h>
#include <linux/rcupdate.h>

#define list_next_rcu(list) (*((struct list_head __rcu **)(&(list)->next)))
#define list_tail_rcu(head) (*((struct list_head __rcu **)(&(head)->prev)))

static inline void __list_add_rcu(struct list_head *new, struct list_head *prev,
    struct list_head *next)
{
	new->next = next;
	new->prev = prev;
	rcu_assign_pointer(list_next_rcu(prev), new);
	next->prev = new;
}
static inline void list_add_rcu(struct list_head *new, struct list_head *head)
{
	__list_add_rcu(new, head, head->next);
}
static inline void list_add_tail_rcu(struct list_head *new, struct list_head *head)
{
	__list_add_rcu(new, head->prev, head);
}
static inline void list_del_rcu(struct list_head *entry)
{
	__list_del_entry(entry);
	entry->prev = (struct list_head *)LIST_POISON2;
}
static inline void list_replace_rcu(struct list_head *old, struct list_head *new)
{
	new->next = old->next;
	new->prev = old->prev;
	rcu_assign_pointer(list_next_rcu(new->prev), new);
	new->next->prev = new;
	old->prev = (struct list_head *)LIST_POISON2;
}
#define list_entry_rcu(ptr, type, member) \
	container_of(READ_ONCE(ptr), type, member)
#define list_first_or_null_rcu(ptr, type, member) ({ \
	struct list_head *__ptr = (ptr); \
	struct list_head *__next = READ_ONCE(__ptr->next); \
	likely(__ptr != __next) ? list_entry_rcu(__next, type, member) : NULL; })
#define list_for_each_entry_rcu(pos, head, member, cond...) \
	for (pos = list_entry_rcu((head)->next, typeof(*pos), member); \
	     &pos->member != (head); \
	     pos = list_entry_rcu(pos->member.next, typeof(*pos), member))
#define list_for_each_entry_continue_rcu(pos, head, member) \
	for (pos = list_entry_rcu(pos->member.next, typeof(*pos), member); \
	     &pos->member != (head); \
	     pos = list_entry_rcu(pos->member.next, typeof(*pos), member))

static inline void hlist_del_rcu(struct hlist_node *n)
{
	__hlist_del(n);
	n->pprev = (struct hlist_node **)LIST_POISON2;
}
static inline void hlist_add_head_rcu(struct hlist_node *n, struct hlist_head *h)
{
	struct hlist_node *first = h->first;
	n->next = first;
	n->pprev = &h->first;
	rcu_assign_pointer(h->first, n);
	if (first)
		first->pprev = &n->next;
}
#define hlist_first_rcu(head) (*((struct hlist_node __rcu **)(&(head)->first)))
#define hlist_next_rcu(node) (*((struct hlist_node __rcu **)(&(node)->next)))
#define hlist_for_each_entry_rcu(pos, head, member, cond...) \
	for (pos = hlist_entry_safe(READ_ONCE((head)->first), typeof(*(pos)), member); \
	     pos; \
	     pos = hlist_entry_safe(READ_ONCE((pos)->member.next), typeof(*(pos)), member))
#endif
