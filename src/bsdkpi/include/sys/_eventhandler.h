/* SPDX-License-Identifier: MIT */
/*
 * sys/_eventhandler.h - EVENTHANDLER(9) declarations.  Handlers are kept in
 * a list keyed by event name (src/misc.c); the only invoker today is
 * if_setlladdr() -> iflladdr_event, which bsdkpi fires itself.
 */
#ifndef _BSDKPI_SYS__EVENTHANDLER_H_
#define _BSDKPI_SYS__EVENTHANDLER_H_
#include <sys/cdefs.h>

struct eventhandler_entry;
typedef struct eventhandler_entry *eventhandler_tag;
typedef void (*bsdkpi_eventhandler_fn)(void);

#define EVENTHANDLER_PRI_FIRST	0
#define EVENTHANDLER_PRI_ANY	10000
#define EVENTHANDLER_PRI_LAST	20000

#define EVENTHANDLER_DECLARE(name, type)	typedef type __bsdkpi_eh_##name##_t
#define EVENTHANDLER_DEFINE(name, func, arg, priority)			\
	static eventhandler_tag name##_tag;				\
	__attribute__((constructor)) static void			\
	name##_evh_init(void)						\
	{								\
		name##_tag = EVENTHANDLER_REGISTER(name, func, arg,	\
		    priority);						\
	}

eventhandler_tag bsdkpi_eventhandler_register(const char *name,
	    bsdkpi_eventhandler_fn func, void *arg, int priority);
void	bsdkpi_eventhandler_deregister(const char *name, eventhandler_tag tag);
struct eventhandler_entry *bsdkpi_eventhandler_first(const char *name);
struct eventhandler_entry *bsdkpi_eventhandler_next(
	    struct eventhandler_entry *e, const char *name);
void	*bsdkpi_eventhandler_func(struct eventhandler_entry *e);
void	*bsdkpi_eventhandler_arg(struct eventhandler_entry *e);

#define EVENTHANDLER_REGISTER(name, func, arg, priority)		\
	bsdkpi_eventhandler_register(#name,				\
	    (bsdkpi_eventhandler_fn)(uintptr_t)(func), (arg), (priority))
#define EVENTHANDLER_DEREGISTER(name, tag)				\
	bsdkpi_eventhandler_deregister(#name, (tag))
#define EVENTHANDLER_DEREGISTER_NOWAIT(name, tag)			\
	bsdkpi_eventhandler_deregister(#name, (tag))
/* Call every handler of @name as a function of type @type with (arg, ...). */
#define EVENTHANDLER_INVOKE_TYPED(name, type, ...) do {			\
	struct eventhandler_entry *__e;					\
	for (__e = bsdkpi_eventhandler_first(#name); __e != NULL;	\
	    __e = bsdkpi_eventhandler_next(__e, #name))			\
		((type)bsdkpi_eventhandler_func(__e))(			\
		    bsdkpi_eventhandler_arg(__e), __VA_ARGS__);		\
} while (0)
#define EVENTHANDLER_INVOKE(name, ...)					\
	EVENTHANDLER_INVOKE_TYPED(name, __bsdkpi_eh_##name##_t, __VA_ARGS__)
#endif /* _BSDKPI_SYS__EVENTHANDLER_H_ */
