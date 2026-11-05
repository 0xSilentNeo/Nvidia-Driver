// SPDX-License-Identifier: MIT
/*
 * End-to-end host run of the real driver probe.
 *
 * The vendored mt7925 PCIe driver is linked as is and attached to a fake
 * PCI device whose BAR0 is plain memory plus a tiny register model:
 *
 *   - CONN_ON_LPCTL driver/firmware ownership handshake (OWN_SYNC follows
 *     SET_OWN / CLR_OWN), so both pmctrl calls in probe succeed;
 *   - WFSYS_SW_RST_B reports INIT_DONE once the reset bit is set;
 *   - HW_CHIPID / HW_REV read 0x7925 / 0x0001 through the L1 remap window.
 *
 * With that the probe gets through PCI setup, firmware feature parsing
 * (if the blobs are in $LKPI_FIRMWARE_DIR), mt76 device allocation, DMA
 * ring allocation and MCU initialisation, where it must fail cleanly because
 * nothing answers MCU commands.  The point is to exercise every layer of
 * the C stack on a developer machine; the assertion is "no crash, clean
 * unwind, expected log sequence", not success.
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
#include "test.h"

/* BAR0 offsets after the mt7925 fixed address map (mt7925/pci.c). */
#define OFF_CONN_ON_LPCTL	0x0e0010	/* 0x7c060010 */
#define OFF_WFSYS_SW_RST_B	0x0f0140	/* 0x7c000140 */
#define OFF_REMAP_L1_WINDOW	0x130000	/* MT_HIF_REMAP_BASE_L1 */
#define OFF_HW_CHIPID		(OFF_REMAP_L1_WINDOW + 0x0200)	/* 0x70010200 */
#define OFF_HW_REV		(OFF_REMAP_L1_WINDOW + 0x0204)
#define LPCR_HOST_SET_OWN	BIT(0)
#define LPCR_HOST_CLR_OWN	BIT(1)
#define LPCR_HOST_OWN_SYNC	BIT(2)
#define WFSYS_SW_RST_B_BIT	BIT(0)
#define WFSYS_SW_INIT_DONE_BIT	BIT(4)

static u8 *bar0;
static int lpctl_writes, rst_writes, total_writes;

static void model_write(volatile void *addr, uint32_t val)
{
	size_t off = (size_t)((volatile u8 *)addr - bar0);
	volatile u32 *reg = (volatile u32 *)addr;

	total_writes++;
	switch (off) {
	case OFF_CONN_ON_LPCTL:
		lpctl_writes++;
		if (val & LPCR_HOST_SET_OWN)
			*reg = LPCR_HOST_OWN_SYNC;
		else if (val & LPCR_HOST_CLR_OWN)
			*reg = 0;
		break;
	case OFF_WFSYS_SW_RST_B:
		rst_writes++;
		if (val & WFSYS_SW_RST_B_BIT)
			*reg = val | WFSYS_SW_INIT_DONE_BIT;
		break;
	default:
		break;
	}
}

int main(int argc, char **argv)
{
	lkpi_os_pci_t os;
	struct pci_dev *pdev;
	unsigned long t0;

	lkpi_host_log_level = (argc > 1 && !strcmp(argv[1], "-v")) ? LKPI_LOG_DEBUG : LKPI_LOG_INFO;
	lkpi_host_start();
	CHECK_EQ(lkpi_runtime_init(), 0);	/* registers the mt7925 PCI driver */

	os = lkpi_host_pci_create(0x14c3, 0x7925, 2 * 1024 * 1024);
	bar0 = lkpi_host_pci_bar0(os);
	*(u32 *)(bar0 + OFF_HW_CHIPID) = 0x7925;
	*(u32 *)(bar0 + OFF_HW_REV) = 0x0001;
	lkpi_host_mmio_write_hook = model_write;

	t0 = jiffies;
	pdev = lkpi_pci_attach(os);
	fprintf(stderr, "probe returned %p after %u ms; %d MMIO writes, %d LPCTL, %d WFSYS reset\n",
		(void *)pdev, jiffies_to_msecs(jiffies - t0), total_writes, lpctl_writes, rst_writes);

	/* The handshakes must have been exercised: fw own, drv own, wfsys reset. */
	CHECK(lpctl_writes >= 2);
	CHECK(rst_writes >= 2);
	CHECK(total_writes > 10);
	/*
	 * mt792x probes synchronously up to DMA setup and registers the device;
	 * firmware download and MCU init run from dev->init_work.  Nothing
	 * answers MCU commands here, so that work ends in "firmware init
	 * failed" after the MCU timeouts.  Wait for it, then remove the device
	 * exactly as the kext does on stop().
	 */
	CHECK(pdev != NULL);
	if (pdev) {
		struct mt76_dev *mdev = pci_get_drvdata(pdev);
		struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);

		t0 = jiffies;
		flush_work(&dev->init_work);
		fprintf(stderr, "init_work finished after %u ms\n", jiffies_to_msecs(jiffies - t0));
		/* Firmware init cannot have succeeded without an MCU; the device must
		 * nevertheless be removable, exactly what the kext does on stop(). */
		CHECK(!test_bit(MT76_STATE_RUNNING, &mdev->phy.state));
		lkpi_pci_detach(pdev);
		fprintf(stderr, "device detached\n");
	}

	lkpi_host_mmio_write_hook = NULL;
	lkpi_host_pci_destroy(os);
	lkpi_runtime_fini();
	lkpi_host_stop();
	return test_report("test_probe");
}
