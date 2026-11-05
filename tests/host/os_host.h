/* SPDX-License-Identifier: MIT */
/* Test-only extensions of the libc lkpi/os.h backend. */
#ifndef _LKPI_OS_HOST_H_
#define _LKPI_OS_HOST_H_
#include <lkpi/os.h>

/* Start/stop the 10 ms jiffies tick thread. */
void lkpi_host_start(void);
void lkpi_host_stop(void);

/* In-memory fake PCI device: 4 KiB config space, one BAR, software IRQ. */
lkpi_os_pci_t lkpi_host_pci_create(uint16_t vendor, uint16_t device, size_t bar0_len);
void lkpi_host_pci_destroy(lkpi_os_pci_t p);
void *lkpi_host_pci_bar0(lkpi_os_pci_t p);
void lkpi_host_pci_raise_irq(lkpi_os_pci_t p);

extern int lkpi_host_log_level;

/*
 * Bus -> host address for a DMA mapping made through the os.h DMA calls
 * (coherent or streaming).  NULL when [bus, bus + len) is not mapped.
 */
void *lkpi_host_dma_to_va(uint64_t bus, size_t len);

/* Called after every 32-bit MMIO write; lets a test model register side effects. */
typedef void (*lkpi_host_mmio_hook_t)(volatile void *addr, uint32_t val);
extern lkpi_host_mmio_hook_t lkpi_host_mmio_write_hook;
#endif
