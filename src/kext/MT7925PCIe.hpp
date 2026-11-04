/* SPDX-License-Identifier: MIT */
/*
 * MT7925PCIe.hpp - the IOService that owns one MediaTek MT7925/MT7927/MT7928
 * PCIe function and drives the C LinuxKPI layer for it.
 *
 * C++ side of the port.  Must never include a Linux header: it only sees
 * lkpi/os.h, lkpi/runtime.h and the four attach/detach prototypes from the
 * tail of linux/pci.h, re-declared here with an opaque pci_dev.
 */
#ifndef MT7925PCIE_HPP
#define MT7925PCIE_HPP

#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOLocks.h>
#include <lkpi/os.h>
#include <lkpi/runtime.h>

struct pci_dev;		/* opaque: owned by the C layer */

extern "C" {
/* src/linuxkpi/include/linux/pci.h (tail) */
struct pci_dev	*lkpi_pci_attach(lkpi_os_pci_t os);
void		 lkpi_pci_detach(struct pci_dev *pdev);
int		 lkpi_pci_suspend(struct pci_dev *pdev);
int		 lkpi_pci_resume(struct pci_dev *pdev);
void		 lkpi_pci_shutdown(struct pci_dev *pdev);
/* lkpi_params.c: module_param() tunables, value given as text ("0"/"1"/"42"). */
int		 lkpi_param_set(const char *name, const char *value);
}

class MT7925PCIe : public IOService {
	OSDeclareDefaultStructors(MT7925PCIe);

public:
	bool		init(OSDictionary *dict = NULL) APPLE_KEXT_OVERRIDE;
	void		free(void) APPLE_KEXT_OVERRIDE;
	IOService	*probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
	bool		start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void		stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOWorkLoop	*getWorkLoop(void) const APPLE_KEXT_OVERRIDE;
	IOReturn	setPowerState(unsigned long state, IOService *whatDevice) APPLE_KEXT_OVERRIDE;
	IOReturn	message(UInt32 type, IOService *provider, void *argument = NULL) APPLE_KEXT_OVERRIDE;

	/* Reachable through lkpi_os_pci_provider() by the C side. */
	IOPCIDevice	*pciDevice(void) const { return fPCI; }
	struct pci_dev	*linuxDevice(void) const { return fPdev; }

private:
	enum { kPowerOff = 0, kPowerOn = 1, kPowerStateCount = 2 };

	void		applyParameters(void);
	bool		setupPowerManagement(IOService *provider);
	void		teardown(void);

	IOPCIDevice	*fPCI;		/* retained */
	IOWorkLoop	*fWorkLoop;	/* per-device interrupt workloop, retained */
	lkpi_os_pci_t	 fHandle;	/* opaque handle handed to the C layer */
	struct pci_dev	*fPdev;		/* result of lkpi_pci_attach() */
	bool		 fRuntimeUp;	/* lkpi_runtime_init() succeeded */
	bool		 fOSUp;		/* lkpi_os_start() succeeded */
	bool		 fPMInited;
	unsigned long	 fPowerState;
	IOLock		*fLock;		/* serialises start/stop/power transitions */
};

#endif /* MT7925PCIE_HPP */
