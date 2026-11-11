// SPDX-License-Identifier: MIT
/*
 * End-to-end host run of the real driver probe.
 *
 * The vendored mt7925 PCIe driver is linked as is and attached to a fake
 * PCI device (os_host.c) driven by the software device model of
 * mt7925_model.c: register side effects, the WFDMA rings and an MCU that
 * answers the firmware bring-up.  The probe therefore runs all the way
 * through PCI setup, firmware patch/RAM download, MCU initialisation,
 * EEPROM/MAC init and ieee80211_register_hw().  That is FreeBSD's
 * mac80211 -> net80211 glue (src/lkpi80211) on the real net80211: the test
 * goes on to create a station vap, bring it up, scan (the model answers the
 * MCU scan command with a beacon and a scan-done event) and tear it down;
 * see wlan_stack_test().  The device then idles into firmware own (runtime
 * PM) and is removed exactly as the kext does on stop().  Run with -v to
 * see every MCU command and event; -vv also lists the firmware chunks and
 * turns on the glue's and net80211's scan tracing.
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
#include "wlan_host.h"
#include "test.h"

#define TEST_SSID	"lkpi-host-ap"
static const uint8_t test_bssid[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };

/* Poll (10 ms steps) until cond holds or ms elapsed; evaluates to cond. */
#define WAIT_FOR(cond, ms) ({						\
	unsigned long __t0 = jiffies;					\
	while (!(cond) && jiffies_to_msecs(jiffies - __t0) < (ms))	\
		msleep(10);						\
	(cond);								\
})

/*
 * The 802.11 stack on top of the registered device: net80211 has an
 * ieee80211com for the hw; create a station vap through the "wlan" cloner,
 * bring it up (the vap enters SCAN and the glue calls the driver's hw_scan,
 * which sends the MCU scan command to the model), let the model complete
 * the scan with one beacon, check the scan cache, scan again through the
 * ioctl path, then take everything down again.
 */
static void wlan_stack_test(struct mt76_dev *mdev, const struct mt7925_model_stats *st,
			    int verbose)
{
	struct wlan_host_ic_info ic;
	unsigned int cmds, scans, frames, phase1_scans;
	unsigned long t0;
	int tries;

	/* ieee80211_register_hw() -> linuxkpi_ieee80211_ifattach() -> ieee80211_ifattach() */
	CHECK_EQ(wlan_host_ic_info(&ic), 1);
	fprintf(stderr, "net80211: ieee80211com %s, %02x:%02x:%02x:%02x:%02x:%02x, %d channel entries "
		"(%d/%d/%d frequencies in 2.4/5/6 GHz), sta %d wpa %d ht %d vht %d, cryptocaps %#x\n",
		ic.name, ic.macaddr[0], ic.macaddr[1], ic.macaddr[2], ic.macaddr[3], ic.macaddr[4],
		ic.macaddr[5], ic.nchans, ic.freqs_2ghz, ic.freqs_5ghz, ic.freqs_6ghz, ic.cap_sta,
		ic.cap_wpa, ic.ht, ic.vht, ic.cryptocaps);
	CHECK(!strcmp(ic.name, "0000:00:00.0"));
	CHECK(!memcmp(ic.macaddr, mdev->phy.macaddr, 6));
	CHECK(ic.cap_sta);
	CHECK(ic.cap_wpa);
	/* what the driver advertised in its 2.4 and 5 GHz sbands */
	CHECK_EQ(ic.freqs_2ghz, mdev->phy.sband_2g.sband.n_channels);
	CHECK_EQ(ic.freqs_5ghz, mdev->phy.sband_5g.sband.n_channels);
	CHECK(ic.ht && ic.vht);
	/*
	 * The driver also registered a 6 GHz sband, but FreeBSD's glue only
	 * hands 2.4 and 5 GHz channels to net80211 (lkpi_ic_getradiocaps;
	 * net80211 at the pinned commit has no 6 GHz channel flags).
	 */
	CHECK(mdev->phy.sband_6g.sband.n_channels > 0);
	CHECK_EQ(ic.freqs_6ghz, 0);
	CHECK_EQ(ic.nvaps, 0);

	if (verbose > 1)
		wlan_host_set_debug(true);
	mt7925_model_set_scan_bss(TEST_SSID, test_bssid, 6);

	/* vap create: ic_vap_create -> mt7925_start (first vif) + add_interface */
	cmds = st->cmds;
	CHECK_EQ(wlan_host_vap_create(), 0);
	CHECK(!strcmp(wlan_host_vap_name(), "wlan0"));
	CHECK_EQ(wlan_host_vap_state(), 0);		/* IEEE80211_S_INIT */
	CHECK_EQ(wlan_host_ic_info(&ic), 1);
	CHECK_EQ(ic.nvaps, 1);
	CHECK(test_bit(MT76_STATE_RUNNING, &mdev->phy.state));
	fprintf(stderr, "vap %s created: %u MCU commands (start + add_interface)\n",
		wlan_host_vap_name(), st->cmds - cmds);
	CHECK(st->cmds > cmds);
	CHECK_EQ(st->scan_reqs, 0);

	/*
	 * 1. Automatic roaming (the default): up takes the vap INIT -> SCAN,
	 * net80211 starts a scan and the glue calls the driver's hw_scan.
	 */
	t0 = jiffies;
	CHECK_EQ(wlan_host_vap_updown(true), 0);
	CHECK(WAIT_FOR(st->scan_reqs >= 1, 2000));
	fprintf(stderr, "vap up: state %d, MCU scan request after %u ms\n",
		wlan_host_vap_state(), jiffies_to_msecs(jiffies - t0));
	CHECK_EQ(wlan_host_vap_state(), 1);		/* IEEE80211_S_SCAN */
	/*
	 * The model's beacon goes RX ring -> mt76 -> ieee80211_rx -> glue ->
	 * net80211 input -> scan cache; its scan-done event goes
	 * mt7925_scan_work -> ieee80211_scan_completed -> ieee80211_scan_done.
	 */
	CHECK(WAIT_FOR(wlan_host_scan_results(TEST_SSID) == 1, 2000));
	fprintf(stderr, "scan cache after %u ms: %d entries, %d with SSID \"%s\" (%u frames from the model)\n",
		jiffies_to_msecs(jiffies - t0), wlan_host_scan_results(NULL),
		wlan_host_scan_results(TEST_SSID), TEST_SSID, st->rx_frames);
	CHECK_EQ(wlan_host_scan_results(NULL), 1);
	CHECK(st->rx_frames >= 1);
	/* nothing to join (the entry has no usable RSSI): net80211 keeps scanning */
	CHECK_EQ(wlan_host_vap_updown(false), 0);
	CHECK(WAIT_FOR(wlan_host_vap_state() == 0, 2000));
	CHECK(WAIT_FOR(!wlan_host_hw_scan_running() && !wlan_host_scanning() &&
		       !test_bit(MT76_HW_SCANNING, &mdev->phy.state), 3000));
	fprintf(stderr, "vap down: %u MCU scan requests, %u cancels so far\n",
		st->scan_reqs, st->scan_cancels);
	phase1_scans = st->scan_reqs;
	CHECK_EQ(wlan_host_scan_results(NULL), 0);	/* INIT flushes the cache */

	/*
	 * 2. The way a supplicant (and the IO80211 layer in phase 4) drives
	 * it: manual roaming, so that up alone does nothing, then one scan
	 * through the ioctl path, SIOCS80211 / IEEE80211_IOC_SCAN_REQ.
	 */
	CHECK_EQ(wlan_host_set_roaming_manual(), 0);
	CHECK_EQ(wlan_host_vap_updown(true), 0);
	msleep(200);
	scans = st->scan_reqs;
	frames = st->rx_frames;
	CHECK_EQ(st->scan_reqs, phase1_scans);		/* up alone did not scan */
	CHECK_EQ(wlan_host_vap_state(), 0);
	/*
	 * A request in INIT only records the parameters and moves the vap to
	 * SCAN; net80211 then consults its cache first, and because a scan
	 * finished less than iv_scanvalid (60 s) ago and the FLUSH flag does
	 * not survive that path (iv_scanreq_flags is 16 bit), it reports "scan
	 * done" from the (empty) cache without asking the driver.
	 */
	CHECK_EQ(wlan_host_scan_request(), 0);
	CHECK(WAIT_FOR(wlan_host_vap_state() == 1 && !wlan_host_scanning(), 2000));
	msleep(100);
	CHECK_EQ(st->scan_reqs, scans);
	/*
	 * In SCAN state a request always starts a scan.  The model sends its
	 * beacon and the scan-done event at the same instant and the driver
	 * completes the scan 100 ms later; on a loaded machine the frame can
	 * reach net80211 after that, where it is (correctly) dropped as "not
	 * to bss" because no scan is running anymore.  Allow two retries.
	 */
	t0 = jiffies;
	for (tries = 1; tries <= 3; tries++) {
		CHECK_EQ(wlan_host_scan_request(), 0);
		CHECK(WAIT_FOR(st->scan_reqs == scans + tries, 2000));
		CHECK(WAIT_FOR(!wlan_host_hw_scan_running() && !wlan_host_scanning() &&
			       !test_bit(MT76_HW_SCANNING, &mdev->phy.state), 3000));
		if (WAIT_FOR(wlan_host_scan_results(TEST_SSID) == 1, 100))
			break;
	}
	fprintf(stderr, "ioctl scan: %d MCU scan request(s), completed after %u ms, "
		"%d scan cache entries, %d with SSID \"%s\"\n", min(tries, 3),
		jiffies_to_msecs(jiffies - t0), wlan_host_scan_results(NULL),
		wlan_host_scan_results(TEST_SSID), TEST_SSID);
	CHECK(tries <= 3);
	CHECK_EQ(wlan_host_scan_results(TEST_SSID), 1);
	CHECK_EQ(wlan_host_scan_results(NULL), 1);
	/* one request, one scan: nothing restarts it */
	msleep(300);
	CHECK_EQ(st->scan_reqs, scans + min(tries, 3));
	CHECK_EQ(st->rx_frames, frames + min(tries, 3));
	CHECK_EQ(wlan_host_vap_state(), 1);		/* IEEE80211_S_SCAN */

	/* down and destroy: remove_interface, and mt7925_stop for the last vif */
	cmds = st->cmds;
	CHECK_EQ(wlan_host_vap_updown(false), 0);
	CHECK(WAIT_FOR(wlan_host_vap_state() == 0, 2000));
	CHECK_EQ(wlan_host_vap_destroy(), 0);
	CHECK_EQ(wlan_host_ic_info(&ic), 1);
	CHECK_EQ(ic.nvaps, 0);
	CHECK(!test_bit(MT76_STATE_RUNNING, &mdev->phy.state));
	fprintf(stderr, "vap destroyed: %u MCU commands, %u unknown, %u model errors\n",
		st->cmds - cmds, st->cmds_unknown, st->errors);
	CHECK_EQ(st->cmds_unknown, 0);
	CHECK_EQ(st->errors, 0);
}

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
	 * ieee80211_register_hw() (checked through net80211 below).
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
		CHECK(dev->hw_init_done);
		/* no interface exists yet: the hw is registered but not started */
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
		 * Let the device idle into firmware own, as happens on a real
		 * system: runtime PM (pm.ps_work) kicks in MT792x_PM_TIMEOUT
		 * after the last interrupt and hands the chip to the firmware
		 * (CONN_ON_LPCTL SET_OWN, answered by the model).
		 */
		t0 = jiffies;
		while (!test_bit(MT76_STATE_PM, &mdev->phy.state) &&
		       jiffies_to_msecs(jiffies - t0) < 2000)
			msleep(10);
		fprintf(stderr, "device in firmware own after %u ms (%u LPCTL writes)\n",
			jiffies_to_msecs(jiffies - t0), st->lpctl_writes);
		CHECK(test_bit(MT76_STATE_PM, &mdev->phy.state));
		CHECK(st->lpctl_writes >= 5);	/* probe: set, clr; mcu_init: set, clr; idle: set */

		/* The 802.11 stack: vap, scan, teardown (wakes the device again). */
		wlan_stack_test(mdev, st, verbose);

		/*
		 * Quiesce before removing the device.  The driver cancels
		 * ps_work before it stops tx_napi, so a TX-done poll still in
		 * flight from the last MCU command would re-arm ps_work after
		 * the cancel and its timer would fire on freed memory (seen under
		 * valgrind, where the softirq threads are slow).  After the last
		 * interface is gone mt792x_stop() has cancelled ps_work, and it
		 * only runs again if such a late poll re-arms it, so firmware
		 * own is not guaranteed here: give the threads time to drain
		 * and take either outcome.
		 */
		t0 = jiffies;
		while (!test_bit(MT76_STATE_PM, &mdev->phy.state) &&
		       jiffies_to_msecs(jiffies - t0) < 500)
			msleep(10);
		fprintf(stderr, "before detach: device in %s own after %u ms (%u LPCTL writes)\n",
			test_bit(MT76_STATE_PM, &mdev->phy.state) ? "firmware" : "driver",
			jiffies_to_msecs(jiffies - t0), st->lpctl_writes);

		lkpi_pci_detach(pdev);
		fprintf(stderr, "device detached: %u MCU commands, %u events, %u model errors in total\n",
			st->cmds, st->events, st->errors);
		CHECK_EQ(st->errors, 0);
	}

	mt7925_model_fini();
	lkpi_host_pci_destroy(os);
	lkpi_runtime_fini();
	lkpi_host_stop();
	return test_report("test_probe");
}
