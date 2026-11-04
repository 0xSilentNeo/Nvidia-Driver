/* SPDX-License-Identifier: MIT */
/*
 * lkpi_param_stub.c - placeholder for lkpi_param_set() until the C layer's
 * parameter registry (lkpi_params.c, walking the __DATA,__lkpi_params
 * section declared by linux/moduleparam.h) lands.  The Makefile compiles
 * this file only when no definition exists under src/linuxkpi/src.
 */
int lkpi_param_set(const char *name, const char *value);

int
lkpi_param_set(const char *name, const char *value)
{
	(void)name;
	(void)value;
	return -38;	/* -ENOSYS: MT7925PCIe treats this as non-fatal */
}
