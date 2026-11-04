// SPDX-License-Identifier: MIT
#include <linux/kernel.h>
#include <linux/errno.h>
#include <lkpi/runtime.h>

struct lkpi_subsys {
	const char *name;
	int (*init)(void);
	void (*fini)(void);
};

static const struct lkpi_subsys lkpi_subsystems[] = {
	{ "lock",	lkpi_lock_init,		lkpi_lock_fini },
	{ "kthread",	lkpi_kthread_init,	lkpi_kthread_fini },
	{ "work",	lkpi_work_init,		lkpi_work_fini },
	{ "tasklet",	lkpi_tasklet_init,	lkpi_tasklet_fini },
	{ "rcu",	lkpi_rcu_init,		lkpi_rcu_fini },
	{ "page",	lkpi_page_init,		lkpi_page_fini },
	{ "napi",	lkpi_napi_init,		lkpi_napi_fini },
	{ "pci",	lkpi_pci_init,		lkpi_pci_fini },
	{ "modules",	lkpi_modules_init,	lkpi_modules_fini },
};

static int lkpi_runtime_users;
static unsigned int lkpi_runtime_started;

int lkpi_runtime_init(void)
{
	unsigned int i;
	int ret;

	if (lkpi_runtime_users++ > 0)
		return 0;

	for (i = 0; i < ARRAY_SIZE(lkpi_subsystems); i++) {
		ret = lkpi_subsystems[i].init();
		if (ret) {
			pr_err("lkpi: %s init failed: %d\n", lkpi_subsystems[i].name, ret);
			while (i-- > 0)
				lkpi_subsystems[i].fini();
			lkpi_runtime_users = 0;
			return ret;
		}
		lkpi_runtime_started = i + 1;
	}
	pr_info("lkpi: runtime ready (%u subsystems)\n", lkpi_runtime_started);
	return 0;
}

void lkpi_runtime_fini(void)
{
	unsigned int i;

	if (lkpi_runtime_users == 0 || --lkpi_runtime_users > 0)
		return;
	for (i = lkpi_runtime_started; i-- > 0;)
		lkpi_subsystems[i].fini();
	lkpi_runtime_started = 0;
}
