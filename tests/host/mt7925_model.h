/* SPDX-License-Identifier: MIT */
/*
 * Software model of an MT7925 behind the fake PCI device of os_host.c:
 * the register side effects, the WFDMA rings and an MCU that answers the
 * firmware bring-up commands.  See mt7925_model.c for what is modelled.
 */
#ifndef _LKPI_MT7925_MODEL_H_
#define _LKPI_MT7925_MODEL_H_
#include <stdbool.h>
#include <stdint.h>
#include "os_host.h"

struct mt7925_model_stats {
	unsigned int mmio_writes;	/* every 32-bit MMIO write seen */
	unsigned int lpctl_writes;	/* CONN_ON_LPCTL ownership handshakes */
	unsigned int wfsys_resets;	/* WFSYS_SW_RST_B assertions */
	unsigned int cmds;		/* MCU commands taken from the WM TX ring */
	unsigned int cmds_unknown;	/* ... that had no entry in the command table */
	unsigned int fwdl_chunks;	/* FW_SCATTER chunks taken from the FWDL ring */
	unsigned long fwdl_bytes;
	unsigned int events;		/* events written to the MCU RX ring */
	unsigned int irqs;		/* interrupt assertions */
	unsigned int errors;		/* ring/buffer problems; must stay 0 */
};

/*
 * Attach the model to a fake device created with lkpi_host_pci_create().
 * Installs lkpi_host_mmio_write_hook.  verbose > 0 logs every MCU command
 * and event to stderr, verbose > 1 also every firmware chunk.
 */
void mt7925_model_init(lkpi_os_pci_t pci, int verbose);
void mt7925_model_fini(void);

const struct mt7925_model_stats *mt7925_model_stats(void);

/* The MMIO write hook; exposed so a test can chain its own hook in front. */
void mt7925_model_mmio_write(volatile void *addr, uint32_t val);
#endif
