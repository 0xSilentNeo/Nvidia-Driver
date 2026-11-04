// SPDX-License-Identifier: MIT
/*
 * Module entry points and module parameters.
 *
 * A kext has no module loader.  module_init()/module_pci_driver() in the
 * vendored driver expand to ordinary functions (see linux/module.h and
 * linux/pci.h) that lkpi_modules_init() calls in dependency order; the
 * mt7925 PCIe driver is the only module_init in this source set.
 *
 * Module parameters: module_param() (linux/moduleparam.h) places a
 * struct lkpi_param descriptor in the "__DATA,__lkpi_params" section.
 * lkpi_param_set() walks that section if the link step provides its
 * bounds as the weak symbols below.
 *
 * TODO(kext): on Mach-O the bounds are the linker-synthesised symbols
 * "section$start$__DATA$__lkpi_params" / "section$end$__DATA$__lkpi_params";
 * alias them to __lkpi_params_start/__lkpi_params_end from the C++ side
 * (asm labels) once the kext link is in place.  On the Linux host and
 * until then the weak references resolve to NULL and lkpi_param_set()
 * reports -ENOENT for every name, which keeps the defaults.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/kstrtox.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <lkpi/runtime.h>

extern const struct lkpi_param __lkpi_params_start[] __attribute__((weak));
extern const struct lkpi_param __lkpi_params_end[] __attribute__((weak));

static int lkpi_param_store(const struct lkpi_param *p, const char *value)
{
	const char *type = p->type;

	if (!strcmp(type, "bool")) {
		bool b;
		int ret = kstrtobool(value, &b);

		if (ret)
			return ret;
		if (p->size == sizeof(bool))
			*(bool *)p->ptr = b;
		else if (p->size == sizeof(int))
			*(int *)p->ptr = b;
		else
			return -EINVAL;
		return 0;
	}
	if (!strcmp(type, "int") || !strcmp(type, "short") || !strcmp(type, "long") ||
	    !strcmp(type, "longlong")) {
		long long v;
		int ret = kstrtoll(value, 0, &v);

		if (ret)
			return ret;
		switch (p->size) {
		case 1: *(s8 *)p->ptr = (s8)v; break;
		case 2: *(s16 *)p->ptr = (s16)v; break;
		case 4: *(s32 *)p->ptr = (s32)v; break;
		case 8: *(s64 *)p->ptr = v; break;
		default: return -EINVAL;
		}
		return 0;
	}
	if (!strcmp(type, "uint") || !strcmp(type, "ushort") || !strcmp(type, "ulong") ||
	    !strcmp(type, "byte") || !strcmp(type, "hexint")) {
		unsigned long long v;
		int ret = kstrtoull(value, 0, &v);

		if (ret)
			return ret;
		switch (p->size) {
		case 1: *(u8 *)p->ptr = (u8)v; break;
		case 2: *(u16 *)p->ptr = (u16)v; break;
		case 4: *(u32 *)p->ptr = (u32)v; break;
		case 8: *(u64 *)p->ptr = v; break;
		default: return -EINVAL;
		}
		return 0;
	}
	if (!strcmp(type, "charp")) {
		/* The string must outlive the driver; the caller owns it. */
		*(const char **)p->ptr = value;
		return 0;
	}
	return -EOPNOTSUPP;
}

int lkpi_param_set(const char *name, const char *value)
{
	const struct lkpi_param *p;

	if (!name || !value)
		return -EINVAL;
	if (!__lkpi_params_start || !__lkpi_params_end)
		return -ENOENT;	/* section bounds not wired up (see TODO above) */
	for (p = __lkpi_params_start; p < __lkpi_params_end; p++) {
		if (p->name && !strcmp(p->name, name))
			return lkpi_param_store(p, value);
	}
	return -ENOENT;
}

/* ---- module_init() / module_exit() sequencing -------------------------- */

static bool lkpi_mt7925_loaded;

int lkpi_modules_init(void)
{
	int ret;

	ret = lkpi_modinit_mt7925_pci_driver();
	if (ret) {
		pr_err("lkpi: mt7925 pci driver init failed: %d\n", ret);
		return ret;
	}
	lkpi_mt7925_loaded = true;
	return 0;
}

void lkpi_modules_fini(void)
{
	if (lkpi_mt7925_loaded) {
		lkpi_modexit_mt7925_pci_driver();
		lkpi_mt7925_loaded = false;
	}
}
