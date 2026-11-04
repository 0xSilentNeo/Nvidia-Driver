// SPDX-License-Identifier: MIT
/* The runtime's module list normally registers the mt7925 PCI driver; the
 * host tests register their own fake driver instead. */
#include <lkpi/runtime.h>

int lkpi_modules_init(void) { return 0; }
void lkpi_modules_fini(void) { }
int lkpi_modinit_mt7925_pci_driver(void) { return 0; }
void lkpi_modexit_mt7925_pci_driver(void) { }
