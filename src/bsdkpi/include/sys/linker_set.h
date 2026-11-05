/* SPDX-License-Identifier: MIT */
/*
 * sys/linker_set.h - linker sets without linker support.
 *
 * FreeBSD collects TEXT_SET/DATA_SET members in an ELF section bracketed by
 * linker-generated __start_/__stop_ symbols.  Mach-O has no such thing (and
 * limits section names to 16 characters), so each member is a static entry
 * that a constructor links into one global registry keyed by the set name
 * (src/modules.c).  Constructors run at kext load / before main(), i.e.
 * before net80211_register_builtin_modules() walks the sets.
 *
 * SET_FOREACH(pvar, set) visits the registered members; *pvar is the
 * member, as in FreeBSD (pvar is a pointer to a pointer-sized slot).
 */
#ifndef _BSDKPI_SYS_LINKER_SET_H_
#define _BSDKPI_SYS_LINKER_SET_H_
#include <sys/cdefs.h>

struct bsdkpi_set_entry {
	const char			*set;
	void				*ptr;
	struct bsdkpi_set_entry		*next;
};

void	bsdkpi_set_register(struct bsdkpi_set_entry *e);
struct bsdkpi_set_entry *bsdkpi_set_first(const char *set);
struct bsdkpi_set_entry *bsdkpi_set_next(struct bsdkpi_set_entry *e,
	    const char *set);
int	bsdkpi_set_count(const char *set);

#define _BSDKPI_LINKER_SET_ENTRY(set, sym)				\
	static struct bsdkpi_set_entry __set_##set##_sym_##sym = {	\
		#set, (void *)&sym, NULL				\
	};								\
	__attribute__((constructor)) static void			\
	__set_##set##_sym_##sym##_ctor(void)				\
	{								\
		bsdkpi_set_register(&__set_##set##_sym_##sym);		\
	}

#define TEXT_SET(set, sym)	_BSDKPI_LINKER_SET_ENTRY(set, sym)
#define DATA_SET(set, sym)	_BSDKPI_LINKER_SET_ENTRY(set, sym)
#define DATA_WSET(set, sym)	_BSDKPI_LINKER_SET_ENTRY(set, sym)
#define BSS_SET(set, sym)	_BSDKPI_LINKER_SET_ENTRY(set, sym)
#define ABS_SET(set, sym)	_BSDKPI_LINKER_SET_ENTRY(set, sym)
#define SET_ENTRY(set, sym)	_BSDKPI_LINKER_SET_ENTRY(set, sym)

/* A repeated typedef of the same type is legal C11, so this may appear in
 * headers (ieee80211_freebsd.h does). */
#define SET_DECLARE(set, ptype)	typedef ptype __bsdkpi_set_type_##set

#define SET_FOREACH(pvar, set)						\
	for (struct bsdkpi_set_entry *__bsdkpi_e_##pvar =		\
	    bsdkpi_set_first(#set);					\
	    __bsdkpi_e_##pvar != NULL &&				\
	    (((pvar) = (void *)&__bsdkpi_e_##pvar->ptr), 1);		\
	    __bsdkpi_e_##pvar = bsdkpi_set_next(__bsdkpi_e_##pvar, #set))

#define SET_COUNT(set)		bsdkpi_set_count(#set)
#define SET_ITEM(set, i)	(bsdkpi_set_item(#set, (i)))
void	*bsdkpi_set_item(const char *set, int i);

#endif /* _BSDKPI_SYS_LINKER_SET_H_ */
