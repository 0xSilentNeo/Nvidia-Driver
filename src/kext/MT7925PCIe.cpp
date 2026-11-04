/* SPDX-License-Identifier: MIT */
/*
 * MT7925PCIe.cpp - IOService glue for the mt76/MT7925 port.
 *
 * Life cycle:
 *   start():  enable memory/bus-master, create the interrupt workloop,
 *             build the lkpi_os_pci handle, apply Parameters -> lkpi_param_set,
 *             lkpi_os_start() + lkpi_runtime_init(), power management,
 *             registerService(), then lkpi_pci_attach() which runs the
 *             vendored mt7925_pci_probe() (BAR0, IRQ, DMA mask, firmware).
 *   stop():   lkpi_pci_detach() (mt7925_pci_remove), undo everything,
 *             lkpi_runtime_fini() + lkpi_os_stop() (both refcounted).
 */

#include <IOKit/IOLib.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <IOKit/IOMessage.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSCollectionIterator.h>
#include <libkern/libkern.h>
#include <pexpert/pexpert.h>

#include "MT7925PCIe.hpp"
#include "lkpi_os_iokit.hpp"

#define super IOService
OSDefineMetaClassAndStructors(MT7925PCIe, IOService);

#define LOG(fmt, ...) IOLog("MT7925: " fmt "\n", ##__VA_ARGS__)

bool
MT7925PCIe::init(OSDictionary *dict)
{
	if (!super::init(dict))
		return false;
	fPCI = NULL;
	fWorkLoop = NULL;
	fHandle = NULL;
	fPdev = NULL;
	fRuntimeUp = false;
	fOSUp = false;
	fPMInited = false;
	fPowerState = kPowerOn;
	fLock = IOLockAlloc();
	return fLock != NULL;
}

void
MT7925PCIe::free(void)
{
	teardown();
	if (fLock) {
		IOLockFree(fLock);
		fLock = NULL;
	}
	super::free();
}

IOService *
MT7925PCIe::probe(IOService *provider, SInt32 *score)
{
	if (!super::probe(provider, score))
		return NULL;
	if (!OSDynamicCast(IOPCIDevice, provider))
		return NULL;
	/* IOPCIPrimaryMatch already filtered vendor/device; nothing more to
	 * check without touching the hardware. */
	return this;
}

IOWorkLoop *
MT7925PCIe::getWorkLoop(void) const
{
	return fWorkLoop;
}

/*
 * Mirror the "Parameters" dictionary of the personality onto the Linux
 * module_param() variables.  Values may be OSBoolean, OSNumber or OSString;
 * everything is handed over as text.  Failure is non-fatal: the parameter
 * registry is being written separately and unknown names are just logged.
 */
void
MT7925PCIe::applyParameters(void)
{
	OSDictionary *params = OSDynamicCast(OSDictionary, getProperty("Parameters"));
	if (!params)
		return;
	OSCollectionIterator *it = OSCollectionIterator::withCollection(params);
	if (!it)
		return;
	OSObject *keyObj;
	while ((keyObj = it->getNextObject()) != NULL) {
		OSString *key = OSDynamicCast(OSString, keyObj);
		if (!key)
			continue;
		OSObject *val = params->getObject(key);
		char buf[64];
		const char *text = NULL;
		if (OSBoolean *b = OSDynamicCast(OSBoolean, val)) {
			text = b->isTrue() ? "1" : "0";
		} else if (OSNumber *n = OSDynamicCast(OSNumber, val)) {
			snprintf(buf, sizeof(buf), "%llu", n->unsigned64BitValue());
			text = buf;
		} else if (OSString *s = OSDynamicCast(OSString, val)) {
			text = s->getCStringNoCopy();
		}
		if (!text)
			continue;
		/* A boot-arg "mt7925_<name>=<value>" overrides the plist. */
		char argname[80];
		char argval[64];
		snprintf(argname, sizeof(argname), "mt7925_%s", key->getCStringNoCopy());
		if (PE_parse_boot_argn(argname, argval, sizeof(argval)))
			text = argval;
		int ret = lkpi_param_set(key->getCStringNoCopy(), text);
		if (ret)
			LOG("parameter %s=%s rejected (%d)", key->getCStringNoCopy(), text, ret);
		else
			LOG("parameter %s=%s", key->getCStringNoCopy(), text);
	}
	it->release();
}

bool
MT7925PCIe::setupPowerManagement(IOService *provider)
{
	static IOPMPowerState powerStates[kPowerStateCount] = {
		/* version, capabilityFlags, outputPowerCharacter, inputPowerRequirement, ... */
		{ kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ kIOPMPowerStateVersion1, kIOPMPowerOn | kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn,
		  0, 0, 0, 0, 0, 0, 0, 0 },
	};

	PMinit();
	provider->joinPMtree(this);
	fPMInited = true;
	fPowerState = kPowerOn;
	if (registerPowerDriver(this, powerStates, kPowerStateCount) != kIOReturnSuccess) {
		LOG("registerPowerDriver failed (continuing without PM)");
		return false;
	}
	return true;
}

bool
MT7925PCIe::start(IOService *provider)
{
	if (!super::start(provider))
		return false;

	fPCI = OSDynamicCast(IOPCIDevice, provider);
	if (!fPCI) {
		super::stop(provider);
		return false;
	}
	fPCI->retain();

	LOG("starting on %04x:%04x (rev %02x)",
	    fPCI->configRead16(kIOPCIConfigVendorID), fPCI->configRead16(kIOPCIConfigDeviceID),
	    fPCI->configRead8(kIOPCIConfigRevisionID));

	fPCI->setMemoryEnable(true);
	fPCI->setBusMasterEnable(true);

	fWorkLoop = IOWorkLoop::workLoop();
	if (!fWorkLoop) {
		LOG("cannot create workloop");
		goto fail;
	}

	fHandle = lkpi_os_pci_create(fPCI, fWorkLoop, this);
	if (!fHandle) {
		LOG("cannot allocate device handle");
		goto fail;
	}

	if (lkpi_os_start() != 0) {
		LOG("OS backend start failed");
		goto fail;
	}
	fOSUp = true;

	applyParameters();

	if (lkpi_runtime_init() != 0) {
		LOG("LinuxKPI runtime init failed");
		goto fail;
	}
	fRuntimeUp = true;

	setupPowerManagement(provider);
	registerService();

	/* Runs mt7925_pci_probe() synchronously: BAR0 mapping, MSI, DMA masks,
	 * MCU power handshake, firmware download may all happen here. */
	fPdev = lkpi_pci_attach(fHandle);
	if (!fPdev) {
		LOG("lkpi_pci_attach failed");
		goto fail;
	}
	LOG("attached");
	return true;

fail:
	teardown();
	super::stop(provider);
	return false;
}

void
MT7925PCIe::stop(IOService *provider)
{
	LOG("stopping");
	teardown();
	super::stop(provider);
}

/* Undo start() in reverse order; safe to call more than once. */
void
MT7925PCIe::teardown(void)
{
	if (fPdev) {
		lkpi_pci_detach(fPdev);
		fPdev = NULL;
	}
	if (fPMInited) {
		PMstop();
		fPMInited = false;
	}
	if (fHandle) {
		lkpi_os_pci_destroy(fHandle);	/* disables IRQ, unmaps BARs */
		fHandle = NULL;
	}
	if (fRuntimeUp) {
		lkpi_runtime_fini();
		fRuntimeUp = false;
	}
	if (fOSUp) {
		lkpi_os_stop();
		fOSUp = false;
	}
	if (fWorkLoop) {
		fWorkLoop->release();
		fWorkLoop = NULL;
	}
	if (fPCI) {
		fPCI->setBusMasterEnable(false);
		fPCI->release();
		fPCI = NULL;
	}
}

/*
 * Power management.  Two states: off (0) and on (1).  The real work lives in
 * the vendored mt7925_pci_suspend()/resume() reached through
 * lkpi_pci_suspend()/resume(); until CONFIG_PM is enabled in the C build
 * those are stubs and this just logs.  Future work for a complete sleep
 * path: pci_save_state/D3hot through IOPCIDevice::saveDeviceState() and
 * hasPCIPowerManagement()/enablePCIPowerManagement(kPCIPMCSPowerStateD3),
 * wake-on-WLAN, and re-enabling MSI after resume.
 */
IOReturn
MT7925PCIe::setPowerState(unsigned long state, IOService *whatDevice)
{
	if (whatDevice != this)
		return kIOPMAckImplied;

	IOLockLock(fLock);
	if (state == fPowerState) {
		IOLockUnlock(fLock);
		return kIOPMAckImplied;
	}
	if (fPdev) {
		if (state == kPowerOff) {
			LOG("suspend");
			int ret = lkpi_pci_suspend(fPdev);
			if (ret)
				LOG("suspend returned %d", ret);
		} else {
			LOG("resume");
			int ret = lkpi_pci_resume(fPdev);
			if (ret)
				LOG("resume returned %d", ret);
		}
	}
	fPowerState = state;
	IOLockUnlock(fLock);
	return kIOPMAckImplied;
}

IOReturn
MT7925PCIe::message(UInt32 type, IOService *provider, void *argument)
{
	switch (type) {
	case kIOMessageServiceIsTerminated:
	case kIOMessageServiceIsRequestingClose:
		/* Device going away (hot-unplug, Thunderbolt) - stop() follows. */
		break;
	case kIOMessageSystemWillPowerOff:
	case kIOMessageSystemWillRestart:
		if (fPdev)
			lkpi_pci_shutdown(fPdev);
		break;
	default:
		break;
	}
	return super::message(type, provider, argument);
}
