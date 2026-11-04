/* SPDX-License-Identifier: MIT */
/*
 * Module boilerplate.  A kext has no module loader, so module_init() and
 * module_pci_driver() generate ordinary functions named after the argument
 * that src/linuxkpi/src/modules.c calls in order at kext start.
 */
#ifndef _LKPI_LINUX_MODULE_H_
#define _LKPI_LINUX_MODULE_H_
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

struct module {
	const char *name;
};
extern struct module lkpi_this_module;
#define THIS_MODULE (&lkpi_this_module)
#ifndef KBUILD_MODNAME
#define KBUILD_MODNAME "mt76"
#endif

#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_LICENSE(x)
#define MODULE_VERSION(x)
#define MODULE_ALIAS(x)
#define MODULE_DEVICE_TABLE(type, name)
#define MODULE_FIRMWARE(x)
#define MODULE_INFO(a, b)
#define MODULE_SOFTDEP(x)
#define MODULE_IMPORT_NS(x)
#define MODULE_PARM_DESC(a, b)

#define module_init(fn) int lkpi_modinit_##fn(void) { return fn(); }
#define module_exit(fn) void lkpi_modexit_##fn(void) { fn(); }
#define try_module_get(m) (1)
#define module_put(m) do { } while (0)
#define request_module(fmt, ...) (0)
#define __module_get(m) do { } while (0)
#define module_name(m) ((m)->name)
#endif
