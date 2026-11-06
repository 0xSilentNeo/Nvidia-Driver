// SPDX-License-Identifier: MIT
/*
 * SYSINIT(9), module(9), linker sets and EVENTHANDLER(9) registries.
 *
 * All four are populated by constructors generated from the FreeBSD macros
 * (sys/kernel.h, sys/module.h, sys/linker_set.h, sys/_eventhandler.h) at
 * image load time and only link static objects into lists, so nothing here
 * allocates or locks.  net80211_register_builtin_modules() then runs the
 * SYSINITs in FreeBSD's (subsystem, order) sequence; DECLARE_MODULE entries
 * are SYSINITs that deliver MOD_LOAD, which is where net80211's crypto,
 * scanner, rate-control and authenticator tables get filled in by
 * SET_FOREACH over the registered TEXT_SET members.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/linker_set.h>
#include <sys/_eventhandler.h>
#include <sys/malloc.h>

/* ---- linker sets ---------------------------------------------------- */

static struct bsdkpi_set_entry *bsdkpi_sets;

void
bsdkpi_set_register(struct bsdkpi_set_entry *e)
{
	struct bsdkpi_set_entry **pp;

	/* append, so that SET_FOREACH visits members in registration order */
	for (pp = &bsdkpi_sets; *pp != NULL; pp = &(*pp)->next)
		continue;
	e->next = NULL;
	*pp = e;
}

static struct bsdkpi_set_entry *
bsdkpi_set_match(struct bsdkpi_set_entry *e, const char *set)
{
	while (e != NULL && strcmp(e->set, set) != 0)
		e = e->next;
	return (e);
}

struct bsdkpi_set_entry *
bsdkpi_set_first(const char *set)
{
	return (bsdkpi_set_match(bsdkpi_sets, set));
}

struct bsdkpi_set_entry *
bsdkpi_set_next(struct bsdkpi_set_entry *e, const char *set)
{
	return (bsdkpi_set_match(e->next, set));
}

int
bsdkpi_set_count(const char *set)
{
	struct bsdkpi_set_entry *e;
	int n = 0;

	for (e = bsdkpi_set_first(set); e != NULL; e = bsdkpi_set_next(e, set))
		n++;
	return (n);
}

void *
bsdkpi_set_item(const char *set, int i)
{
	struct bsdkpi_set_entry *e;

	for (e = bsdkpi_set_first(set); e != NULL; e = bsdkpi_set_next(e, set))
		if (i-- == 0)
			return (e->ptr);
	return (NULL);
}

/* ---- SYSINIT -------------------------------------------------------- */

static struct sysinit *bsdkpi_sysinits;
static int bsdkpi_sysinits_done;

void
bsdkpi_sysinit_register(struct sysinit *si)
{
	struct sysinit **pp;

	/*
	 * Keep the list sorted by (subsystem, order); equal keys keep
	 * registration order, as FreeBSD's sysinit sort does.
	 */
	for (pp = &bsdkpi_sysinits; *pp != NULL; pp = &(*pp)->next) {
		if ((*pp)->subsystem > si->subsystem ||
		    ((*pp)->subsystem == si->subsystem &&
		    (*pp)->order > si->order))
			break;
	}
	si->next = *pp;
	*pp = si;
}

int
net80211_register_builtin_modules(void)
{
	struct sysinit *si;

	if (bsdkpi_sysinits_done)
		return (0);
	bsdkpi_sysinits_done = 1;
	for (si = bsdkpi_sysinits; si != NULL; si = si->next) {
		if (si->flags & BSDKPI_SYSINIT_UNINIT)
			continue;
		if (si->flags & BSDKPI_SYSINIT_DONE)
			continue;
		si->flags |= BSDKPI_SYSINIT_DONE;
		if (si->func != NULL)
			si->func(si->udata);
	}
	return (0);
}

void
net80211_unregister_builtin_modules(void)
{
	struct sysinit *si, *last;

	if (!bsdkpi_sysinits_done)
		return;
	/* SYSUNINITs run in reverse (subsystem, order); the list is singly
	 * linked and short, so walk it backwards the slow way. */
	last = NULL;
	for (;;) {
		struct sysinit *pick = NULL;

		for (si = bsdkpi_sysinits; si != last && si != NULL;
		    si = si->next)
			pick = si;
		if (pick == NULL)
			break;
		last = pick;
		if ((pick->flags & BSDKPI_SYSINIT_UNINIT) && pick->func != NULL)
			pick->func(pick->udata);
	}
	for (si = bsdkpi_sysinits; si != NULL; si = si->next)
		si->flags &= ~BSDKPI_SYSINIT_DONE;
	bsdkpi_sysinits_done = 0;
}

/* ---- module(9) ------------------------------------------------------ */

static struct module *bsdkpi_modules;

static struct module *
bsdkpi_module_find(const moduledata_t *data)
{
	struct module *mod;

	for (mod = bsdkpi_modules; mod != NULL; mod = mod->next)
		if (mod->data == data)
			return (mod);
	return (NULL);
}

void
bsdkpi_module_sysinit(void *arg)
{
	moduledata_t *data = arg;
	struct module *mod;
	int error;

	mod = bsdkpi_module_find(data);
	if (mod == NULL) {
		mod = malloc(sizeof(*mod), M_TEMP, M_WAITOK | M_ZERO);
		if (mod == NULL)
			return;
		mod->data = data;
		mod->next = bsdkpi_modules;
		bsdkpi_modules = mod;
	}
	if (mod->loaded)
		return;
	error = data->evhand != NULL ? data->evhand(mod, MOD_LOAD, data->priv) : 0;
	mod->error = error;
	if (error != 0)
		printf("module %s: MOD_LOAD failed: %d\n", data->name, error);
	else
		mod->loaded = 1;
}

void
bsdkpi_module_sysuninit(void *arg)
{
	moduledata_t *data = arg;
	struct module *mod;

	mod = bsdkpi_module_find(data);
	if (mod == NULL || !mod->loaded)
		return;
	if (data->evhand != NULL)
		(void)data->evhand(mod, MOD_UNLOAD, data->priv);
	mod->loaded = 0;
}

int
module_register_init(const void *arg)
{
	bsdkpi_module_sysinit(__DECONST(void *, arg));
	return (0);
}

const char *
module_getname(module_t mod)
{
	return (mod->data->name);
}

void *
module_getfnext(module_t mod)
{
	return (mod->data->priv);
}

int
module_getid(module_t mod)
{
	int id = 1;
	struct module *m;

	for (m = bsdkpi_modules; m != NULL; m = m->next, id++)
		if (m == mod)
			return (id);
	return (0);
}

module_t
module_lookupbyname(const char *name)
{
	struct module *mod;

	for (mod = bsdkpi_modules; mod != NULL; mod = mod->next)
		if (strcmp(mod->data->name, name) == 0)
			return (mod);
	return (NULL);
}

void
module_setspecific(module_t mod, void *data)
{
	mod->data->priv = data;
}

/* ---- EVENTHANDLER(9) ------------------------------------------------ */

struct eventhandler_entry {
	const char			*ee_name;
	bsdkpi_eventhandler_fn		 ee_func;
	void				*ee_arg;
	int				 ee_priority;
	struct eventhandler_entry	*ee_next;
};

static struct eventhandler_entry *bsdkpi_eventhandlers;

eventhandler_tag
bsdkpi_eventhandler_register(const char *name, bsdkpi_eventhandler_fn func,
    void *arg, int priority)
{
	struct eventhandler_entry *e, **pp;

	e = malloc(sizeof(*e), M_TEMP, M_WAITOK | M_ZERO);
	if (e == NULL)
		return (NULL);
	e->ee_name = name;
	e->ee_func = func;
	e->ee_arg = arg;
	e->ee_priority = priority;
	for (pp = &bsdkpi_eventhandlers; *pp != NULL; pp = &(*pp)->ee_next)
		if ((*pp)->ee_priority > priority)
			break;
	e->ee_next = *pp;
	*pp = e;
	return (e);
}

void
bsdkpi_eventhandler_deregister(const char *name, eventhandler_tag tag)
{
	struct eventhandler_entry **pp;

	(void)name;
	if (tag == NULL)
		return;
	for (pp = &bsdkpi_eventhandlers; *pp != NULL; pp = &(*pp)->ee_next) {
		if (*pp == tag) {
			*pp = tag->ee_next;
			free(tag, M_TEMP);
			return;
		}
	}
}

static struct eventhandler_entry *
bsdkpi_eventhandler_match(struct eventhandler_entry *e, const char *name)
{
	while (e != NULL && strcmp(e->ee_name, name) != 0)
		e = e->ee_next;
	return (e);
}

struct eventhandler_entry *
bsdkpi_eventhandler_first(const char *name)
{
	return (bsdkpi_eventhandler_match(bsdkpi_eventhandlers, name));
}

struct eventhandler_entry *
bsdkpi_eventhandler_next(struct eventhandler_entry *e, const char *name)
{
	return (bsdkpi_eventhandler_match(e->ee_next, name));
}

void *
bsdkpi_eventhandler_func(struct eventhandler_entry *e)
{
	return ((void *)(uintptr_t)e->ee_func);
}

void *
bsdkpi_eventhandler_arg(struct eventhandler_entry *e)
{
	return (e->ee_arg);
}
