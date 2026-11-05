// SPDX-License-Identifier: MIT
/*
 * End-to-end host run of the real driver probe.
 *
 * The vendored mt7925 PCIe driver is linked as is and attached to a fake
 * PCI device (os_host.c) driven by the software device model of
 * mt7925_model.c: register side effects, the WFDMA rings and an MCU that
 * answers the firmware bring-up.  The probe therefore runs all the way
 * through PCI setup, firmware patch/RAM download, MCU initialisation,
 * EEPROM/MAC init and ieee80211_register_hw(), which lands in the phase-1
 * mac80211 stub.  The device then idles into firmware own (runtime PM) and
 * is removed exactly as the kext does on stop().  Run with -v to see every MCU command and event; -vv also lists
 * the firmware chunks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <lkpi/runtime.h>
#include "mt7925.h"		/* struct mt792x_dev: to wait for init_work */
#include "os_host.h"
#include "mt7925_model.h"
#include "test.h"

/* mac80211 stub: incremented by every ieee80211_register_hw() */
extern int lkpi_stub_hw_registered;

int main(int argc, char **argv)
{
	const struct mt7925_model_stats *st;
	lkpi_os_pci_t os;
	struct pci_dev *pdev;
	unsigned long t0;
	int verbose = 0;

	if (argc > 1 && !strcmp(argv[1], "-v"))
		verbose = 1;
	else if (argc > 1 && !strcmp(argv[1], "-vv"))
		verbose = 2;
	lkpi_host_log_level = verbose ? LKPI_LOG_DEBUG : LKPI_LOG_INFO;
	lkpi_host_start();
	CHECK_EQ(lkpi_runtime_init(), 0);	/* registers the mt7925 PCI driver */

	os = lkpi_host_pci_create(0x14c3, 0x7925, 2 * 1024 * 1024);
	mt7925_model_init(os, verbose);

	t0 = jiffies;
	pdev = lkpi_pci_attach(os);
	st = mt7925_model_stats();
	fprintf(stderr, "probe returned %p after %u ms; %u MMIO writes, %u LPCTL, %u WFSYS reset\n",
		(void *)pdev, jiffies_to_msecs(jiffies - t0), st->mmio_writes, st->lpctl_writes,
		st->wfsys_resets);

	/* The handshakes must have been exercised: fw own, drv own, wfsys reset. */
	CHECK(st->lpctl_writes >= 2);
	CHECK(st->wfsys_resets >= 1);
	CHECK(st->mmio_writes > 10);
	/*
	 * mt792x probes synchronously up to DMA setup and registers the device;
	 * firmware download and MCU init run from dev->init_work.  Wait for it,
	 * then check that the whole bring-up went through: patch + RAM
	 * download, NIC capabilities, EEPROM, MAC init, and finally
	 * ieee80211_register_hw() in the mac80211 stub.
	 */
	CHECK(pdev != NULL);
	if (pdev) {
		struct mt76_dev *mdev = pci_get_drvdata(pdev);
		struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);

		t0 = jiffies;
		flush_work(&dev->init_work);
		fprintf(stderr, "init_work finished after %u ms: %u MCU commands (%u unknown), "
			"%u fw chunks (%lu bytes), %u events, %u irqs, %u model errors\n",
			jiffies_to_msecs(jiffies - t0), st->cmds, st->cmds_unknown, st->fwdl_chunks,
			st->fwdl_bytes, st->events, st->irqs, st->errors);

		CHECK(test_bit(MT76_STATE_MCU_RUNNING, &mdev->phy.state));
		CHECK(test_bit(MT76_STATE_REGISTERED, &mdev->phy.state));
		CHECK_EQ(lkpi_stub_hw_registered, 1);
		CHECK(dev->hw_init_done);
		/* no interface is ever brought up here */
		CHECK(!test_bit(MT76_STATE_RUNNING, &mdev->phy.state));
		/* the patch and both RAM regions were downloaded through the FWDL ring */
		CHECK(st->fwdl_chunks > 100);
		CHECK(st->fwdl_bytes > 1000000);
		CHECK(st->events > 10);
		CHECK_EQ(st->errors, 0);
		/* the firmware init must have run exactly once: no MCU timeouts, no retry */
		CHECK_EQ(st->wfsys_resets, 1);
		/* the NIC capability event's MAC address made it into the phy */
		CHECK(mdev->phy.macaddr[0] == 0x00 && mdev->phy.macaddr[1] == 0x0c &&
		      mdev->phy.macaddr[2] == 0xe7);
		CHECK(mdev->phy.cap.has_2ghz && mdev->phy.cap.has_5ghz && mdev->phy.cap.has_6ghz);

		/*
		 * Let the device idle into firmware own before removing it, as
		 * happens on a real system: runtime PM (pm.ps_work) kicks in
		 * MT792x_PM_TIMEOUT after the last interrupt and hands the chip
		 * to the firmware (CONN_ON_LPCTL SET_OWN, answered by the model).
		 * This also drains the tasklet/NAPI threads: the driver cancels
		 * ps_work before it stops tx_napi, so a TX-done poll still in
		 * flight from the last MCU command would re-arm ps_work after
		 * the cancel and its timer would fire on freed memory (seen under
		 * valgrind, where the softirq threads are slow).
		 */
		t0 = jiffies;
		while (!test_bit(MT76_STATE_PM, &mdev->phy.state) &&
		       jiffies_to_msecs(jiffies - t0) < 2000)
			msleep(10);
		fprintf(stderr, "device in firmware own after %u ms (%u LPCTL writes)\n",
			jiffies_to_msecs(jiffies - t0), st->lpctl_writes);
		CHECK(test_bit(MT76_STATE_PM, &mdev->phy.state));
		CHECK(st->lpctl_writes >= 5);	/* probe: set, clr; mcu_init: set, clr; idle: set */

		lkpi_pci_detach(pdev);
		fprintf(stderr, "device detached: %u MCU commands, %u events, %u model errors in total\n",
			st->cmds, st->events, st->errors);
		CHECK_EQ(st->errors, 0);
		CHECK_EQ(lkpi_stub_hw_registered, 1);
	}

	mt7925_model_fini();
	lkpi_host_pci_destroy(os);
	lkpi_runtime_fini();
	lkpi_host_stop();
	return test_report("test_probe");
}
