/* SPDX-License-Identifier: MIT */
/*
 * module_param() declares a tunable.  The kext reads matching keys from its
 * Info.plist / boot-args at start (see lkpi_params.c), so the variables keep
 * their Linux names and defaults.
 */
#ifndef _LKPI_LINUX_MODULEPARAM_H_
#define _LKPI_LINUX_MODULEPARAM_H_
#include <linux/types.h>
struct lkpi_param {
	const char *name;
	const char *type;
	void *ptr;
	size_t size;
};
#define __lkpi_module_param(name, var, type) \
	static const struct lkpi_param __lkpi_param_##name \
	__attribute__((used, section("__DATA,__lkpi_params"))) = \
	{ #name, #type, &(var), sizeof(var) }
#define module_param(name, type, perm)			__lkpi_module_param(name, name, type)
#define module_param_named(name, var, type, perm)	__lkpi_module_param(name, var, type)
#define module_param_unsafe(name, type, perm)		module_param(name, type, perm)
#define module_param_named_unsafe(name, var, type, perm) module_param_named(name, var, type, perm)
#define module_param_array(name, type, nump, perm)
#define module_param_array_named(name, var, type, nump, perm)
#define module_param_string(name, string, len, perm)
#define module_param_cb(name, ops, arg, perm)
#define core_param(name, var, type, perm)
#define __MODULE_PARM_TYPE(name, type)
#define kernel_param_lock(m) do { } while (0)
#define kernel_param_unlock(m) do { } while (0)
#endif
