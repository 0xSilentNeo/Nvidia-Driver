// SPDX-License-Identifier: MIT
#include <linux/kernel.h>
#include <linux/errno.h>
#include <lkpi/runtime.h>

/*
 * The 802.11 stack: bsdkpi (the FreeBSD kernel API net80211 runs on) is
 * brought up as one more subsystem, after the primitives it is built on
 * and before any driver can probe, so that net80211's module tables
 * (ciphers, scanners, rate control, the "wlan" cloner) are populated when
 * ieee80211_register_hw() attaches.  Declared here instead of including
 * <bsdkpi/bsdkpi.h>: that header lives on the bsdkpi include path, which
 * LinuxKPI translation units do not have.  LKPI_NO_NET80211 is for builds
 * without the stack (the phase-1 mac80211 stub, the LinuxKPI unit tests).
 */
#ifndef LKPI_NO_NET80211
int bsdkpi_init(void);
void bsdkpi_fini(void);

static int lkpi_net80211_init(void)
{
	/* bsdkpi returns a FreeBSD (positive) errno. */
	return -bsdkpi_init();
}
#endif

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
#ifndef LKPI_NO_NET80211
	{ "net80211",	lkpi_net80211_init,	bsdkpi_fini },
#endif
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
