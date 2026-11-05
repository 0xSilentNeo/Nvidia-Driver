/* SPDX-License-Identifier: MIT */
/*
 * sys/module.h - module(9).  DECLARE_MODULE() becomes a SYSINIT that
 * delivers MOD_LOAD to the module's event handler when
 * net80211_register_builtin_modules() runs; MOD_UNLOAD is sent in reverse
 * order by net80211_unregister_builtin_modules().  There is no loader, so
 * MODULE_VERSION/MODULE_DEPEND only record metadata.
 */
#ifndef _BSDKPI_SYS_MODULE_H_
#define _BSDKPI_SYS_MODULE_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/linker_set.h>

typedef enum modeventtype {
	MOD_LOAD,
	MOD_UNLOAD,
	MOD_SHUTDOWN,
	MOD_QUIESCE
} modeventtype_t;

struct module;
typedef struct module *module_t;
typedef int (*modeventhand_t)(module_t, int, void *);

typedef struct moduledata {
	const char	*name;
	modeventhand_t	 evhand;
	void		*priv;
} moduledata_t;

struct module {
	moduledata_t	*data;
	int		 loaded;	/* MOD_LOAD returned 0 */
	int		 error;
	struct module	*next;
};

#define MDT_DEPEND	1
#define MDT_MODULE	2
#define MDT_VERSION	3
#define MDT_PNP_INFO	4

struct mod_depend {
	int	md_ver_minimum;
	int	md_ver_preferred;
	int	md_ver_maximum;
};

struct mod_version {
	int	mv_version;
};

/* DECLARE_MODULE: the SYSINIT runs bsdkpi_module_sysinit(&data). */
void	bsdkpi_module_sysinit(void *arg);
void	bsdkpi_module_sysuninit(void *arg);
#define DECLARE_MODULE_WITH_MAXVER(name, data, sub, order, maxver)	\
	SYSINIT(name##module, sub, order, bsdkpi_module_sysinit, &data);\
	SYSUNINIT(name##module, sub, order, bsdkpi_module_sysuninit, &data)
#define DECLARE_MODULE(name, data, sub, order)				\
	DECLARE_MODULE_WITH_MAXVER(name, data, sub, order, 0)
#define DECLARE_MODULE_TIED(name, data, sub, order)			\
	DECLARE_MODULE(name, data, sub, order)

#define MODULE_METADATA(uniquifier, type, data, cval)
#define MODULE_DEPEND(module, mdepend, vmin, vpref, vmax)		\
	static const struct mod_depend _##module##_depend_on_##mdepend	\
	    __attribute__((used)) = { vmin, vpref, vmax }
#define MODULE_VERSION(module, version)					\
	static const struct mod_version _##module##_version		\
	    __attribute__((used)) = { version }
#define MODULE_VERSION_CONCAT(module, version) MODULE_VERSION(module, version)
#define MODULE_PNP_INFO(d, b, unique, t, n)

int	module_register_init(const void *arg);
const char *module_getname(module_t mod);
void	*module_getfnext(module_t mod);
int	module_getid(module_t mod);
module_t module_lookupbyname(const char *name);
void	module_setspecific(module_t mod, void *data);

#endif /* _BSDKPI_SYS_MODULE_H_ */
