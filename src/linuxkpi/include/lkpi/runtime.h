/* SPDX-License-Identifier: MIT */
/*
 * lkpi/runtime.h - start-up and shut-down of the C LinuxKPI layer.
 *
 * The kext calls lkpi_runtime_init() once before the first lkpi_pci_attach()
 * and lkpi_runtime_fini() after the last lkpi_pci_detach().  The subsystem
 * hooks below are implemented in the matching src/linuxkpi/src/<name>.c files and
 * run in the order listed in runtime.c.  Plain C, safe to include from C++.
 */
#ifndef _LKPI_RUNTIME_H_
#define _LKPI_RUNTIME_H_
#ifdef __cplusplus
extern "C" {
#endif

int	lkpi_runtime_init(void);
void	lkpi_runtime_fini(void);

/* subsystem hooks (return 0 or negative Linux errno) */
int	lkpi_lock_init(void);	void	lkpi_lock_fini(void);	/* lock.c */
int	lkpi_rcu_init(void);	void	lkpi_rcu_fini(void);	/* rcu.c */
int	lkpi_work_init(void);	void	lkpi_work_fini(void);	/* work.c: system_wq & co */
int	lkpi_tasklet_init(void); void	lkpi_tasklet_fini(void);/* tasklet.c */
int	lkpi_kthread_init(void); void	lkpi_kthread_fini(void);/* kthread.c */
int	lkpi_page_init(void);	void	lkpi_page_fini(void);	/* page.c */
int	lkpi_napi_init(void);	void	lkpi_napi_fini(void);	/* netdevice.c */
int	lkpi_pci_init(void);	void	lkpi_pci_fini(void);	/* pci.c */
int	lkpi_modules_init(void); void	lkpi_modules_fini(void);/* modules.c: driver module_init()s */

/* module_init()/module_pci_driver() entry points generated in the vendored
 * driver (see linux/module.h and linux/pci.h). */
int	lkpi_modinit_mt7925_pci_driver(void);
void	lkpi_modexit_mt7925_pci_driver(void);

/* Set a module_param() tunable by name from its textual value (modules.c).
 * Returns 0, -ENOENT if no such parameter is known, or a kstrto* error. */
int	lkpi_param_set(const char *name, const char *value);

#ifdef __cplusplus
}
#endif
#endif
