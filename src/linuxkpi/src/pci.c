// SPDX-License-Identifier: MIT
/*
 * PCI bus layer.
 *
 * The kext owns the IOPCIDevice and hands this file an opaque lkpi_os_pci_t
 * (lkpi/os.h).  lkpi_pci_attach() turns it into a struct pci_dev, matches it
 * against the drivers that module_pci_driver() registered and runs probe().
 * Config space, BAR mapping, the interrupt and bus mastering go back through
 * lkpi/os.h; nothing here touches the hardware directly.
 *
 * There is no struct pci_dev for the upstream bridge: macOS does not expose
 * the parent IOPCIDevice to a leaf driver, so pdev->bus->self stays NULL
 * (mt76/pci.c guards every parent access with "if (parent)").
 *
 * Interrupts: pci_alloc_irq_vectors() hands out a small integer token as
 * pdev->irq and records the pci_dev it belongs to; request_threaded_irq()
 * looks the device up by that token and installs a trampoline with the
 * backend.  The backend calls the trampoline on its interrupt workloop
 * thread, so a thread_fn (IRQ_WAKE_THREAD) can simply run inline.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <lkpi/os.h>
#include <lkpi/runtime.h>

#define LKPI_PCI_SAVED_DWORDS	64	/* 256-byte legacy config header */
#define LKPI_PCI_NIRQ		8	/* irq tokens 1..LKPI_PCI_NIRQ */
#define LKPI_PCI_CAP_TTL	48	/* max capability list length */
#define LKPI_PCI_STATUS_CAP_LIST 0x10	/* PCI_STATUS: capability list bit */

/* Private state that struct pci_dev (linux/pci.h) does not carry. */
struct lkpi_pci_dev {
	struct pci_dev pdev;		/* must stay first */
	struct pci_bus bus;
	u32 saved_cfg[LKPI_PCI_SAVED_DWORDS];
	bool state_saved;
	bool want_msi;			/* PCI_IRQ_MSI/MSIX requested */
	bool irq_vectors;		/* pci_alloc_irq_vectors() done */
	bool irq_installed;		/* backend handler currently enabled */
	irq_handler_t irq_thread_fn;
	void *irq_devres;		/* devm_request_threaded_irq() record */
	void *iomap_devres;		/* pcim_iomap_regions() release record */
	int pm_cap;			/* PCI_CAP_ID_PM offset, 0 if none */
};

static inline struct lkpi_pci_dev *to_lkpi(struct pci_dev *pdev)
{
	return container_of(pdev, struct lkpi_pci_dev, pdev);
}

static LIST_HEAD(lkpi_pci_drivers);
static LIST_HEAD(lkpi_pci_devices);
static DEFINE_MUTEX(lkpi_pci_lock);
static struct pci_dev *lkpi_irq_table[LKPI_PCI_NIRQ];

/* ---- config space ---------------------------------------------------- */

static u32 lkpi_cfg_read(const struct pci_dev *dev, int where, int width)
{
	if (!dev || !dev->os)
		return width == 4 ? 0xffffffffu : (width == 2 ? 0xffffu : 0xffu);
	return lkpi_os_pci_cfg_read(dev->os, (u32)where, width);
}

static void lkpi_cfg_write(const struct pci_dev *dev, int where, int width, u32 val)
{
	if (dev && dev->os)
		lkpi_os_pci_cfg_write(dev->os, (u32)where, width, val);
}

int pci_read_config_byte(const struct pci_dev *dev, int where, u8 *val)
{
	*val = (u8)lkpi_cfg_read(dev, where, 1);
	return dev && dev->os ? 0 : -ENODEV;
}

int pci_read_config_word(const struct pci_dev *dev, int where, u16 *val)
{
	*val = (u16)lkpi_cfg_read(dev, where, 2);
	return dev && dev->os ? 0 : -ENODEV;
}

int pci_read_config_dword(const struct pci_dev *dev, int where, u32 *val)
{
	*val = lkpi_cfg_read(dev, where, 4);
	return dev && dev->os ? 0 : -ENODEV;
}

int pci_write_config_byte(const struct pci_dev *dev, int where, u8 val)
{
	lkpi_cfg_write(dev, where, 1, val);
	return dev && dev->os ? 0 : -ENODEV;
}

int pci_write_config_word(const struct pci_dev *dev, int where, u16 val)
{
	lkpi_cfg_write(dev, where, 2, val);
	return dev && dev->os ? 0 : -ENODEV;
}

int pci_write_config_dword(const struct pci_dev *dev, int where, u32 val)
{
	lkpi_cfg_write(dev, where, 4, val);
	return dev && dev->os ? 0 : -ENODEV;
}

int pci_find_capability(struct pci_dev *dev, int cap)
{
	int ttl = LKPI_PCI_CAP_TTL;
	u16 status;
	u8 pos, id;

	if (!dev || !dev->os)
		return 0;
	status = (u16)lkpi_cfg_read(dev, PCI_STATUS, 2);
	if (!(status & LKPI_PCI_STATUS_CAP_LIST))
		return 0;
	pos = (u8)lkpi_cfg_read(dev, PCI_CAPABILITY_LIST, 1) & ~3;
	while (ttl-- > 0 && pos >= 0x40) {
		id = (u8)lkpi_cfg_read(dev, pos + PCI_CAP_LIST_ID, 1);
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;
		pos = (u8)lkpi_cfg_read(dev, pos + PCI_CAP_LIST_NEXT, 1) & ~3;
	}
	return 0;
}

int pcie_capability_read_word(struct pci_dev *dev, int pos, u16 *val)
{
	*val = 0;
	if (!dev || !dev->pcie_cap)
		return -EINVAL;
	return pci_read_config_word(dev, dev->pcie_cap + pos, val);
}

int pcie_capability_read_dword(struct pci_dev *dev, int pos, u32 *val)
{
	*val = 0;
	if (!dev || !dev->pcie_cap)
		return -EINVAL;
	return pci_read_config_dword(dev, dev->pcie_cap + pos, val);
}

int pcie_capability_write_word(struct pci_dev *dev, int pos, u16 val)
{
	if (!dev || !dev->pcie_cap)
		return -EINVAL;
	return pci_write_config_word(dev, dev->pcie_cap + pos, val);
}

int pcie_capability_write_dword(struct pci_dev *dev, int pos, u32 val)
{
	if (!dev || !dev->pcie_cap)
		return -EINVAL;
	return pci_write_config_dword(dev, dev->pcie_cap + pos, val);
}

int pcie_capability_clear_and_set_word(struct pci_dev *dev, int pos, u16 clear, u16 set)
{
	u16 val;
	int ret;

	ret = pcie_capability_read_word(dev, pos, &val);
	if (ret)
		return ret;
	val = (val & ~clear) | set;
	return pcie_capability_write_word(dev, pos, val);
}

int pci_disable_link_state(struct pci_dev *dev, int state)
{
	u16 clear = 0;

	if (!dev || !dev->pcie_cap)
		return -EINVAL;
	if (state & PCIE_LINK_STATE_L0S)
		clear |= PCI_EXP_LNKCTL_ASPM_L0S;
	if (state & (PCIE_LINK_STATE_L1 | PCIE_LINK_STATE_L1_1 | PCIE_LINK_STATE_L1_2 |
		     PCIE_LINK_STATE_L1_1_PCIPM | PCIE_LINK_STATE_L1_2_PCIPM))
		clear |= PCI_EXP_LNKCTL_ASPM_L1;
	if (state & PCIE_LINK_STATE_CLKPM)
		clear |= PCI_EXP_LNKCTL_CLKREQ_EN;
	if (!clear)
		return 0;
	/*
	 * Linux also clears the bits in the upstream bridge; we have no handle
	 * on it (bus->self == NULL), so only the endpoint side is touched.
	 */
	return pcie_capability_clear_word(dev, PCI_EXP_LNKCTL, clear);
}

int pci_enable_link_state(struct pci_dev *dev, int state)
{
	(void)dev; (void)state;
	/* ASPM policy belongs to the platform; never re-enable behind its back. */
	return 0;
}

/* ---- enable / bus master --------------------------------------------- */

int pci_enable_device(struct pci_dev *dev)
{
	u16 cmd;

	if (!dev || !dev->os)
		return -ENODEV;
	cmd = (u16)lkpi_cfg_read(dev, PCI_COMMAND, 2);
	if (!(cmd & PCI_COMMAND_MEMORY))
		lkpi_cfg_write(dev, PCI_COMMAND, 2, cmd | PCI_COMMAND_MEMORY);
	dev->current_state = PCI_D0;
	return 0;
}

void pci_disable_device(struct pci_dev *dev)
{
	if (dev && dev->is_busmaster)
		pci_clear_master(dev);
}

int pcim_enable_device(struct pci_dev *dev)
{
	int ret = pci_enable_device(dev);

	if (!ret)
		dev->managed = true;
	return ret;
}

void pci_set_master(struct pci_dev *dev)
{
	if (!dev || !dev->os)
		return;
	lkpi_os_pci_set_bus_master(dev->os, true);
	dev->is_busmaster = true;
}

void pci_clear_master(struct pci_dev *dev)
{
	if (!dev || !dev->os)
		return;
	lkpi_os_pci_set_bus_master(dev->os, false);
	dev->is_busmaster = false;
}

/* ---- BARs ------------------------------------------------------------ */

static void __iomem *lkpi_pci_map_bar(struct pci_dev *dev, int bar)
{
	size_t len = 0;
	void *va;

	if (bar < 0 || bar >= PCI_STD_NUM_BARS || !dev->os)
		return NULL;
	if (dev->bar_va[bar])
		return dev->bar_va[bar];
	va = lkpi_os_pci_map_bar(dev->os, bar, &len);
	if (!va || !len)
		return NULL;
	dev->bar_va[bar] = va;
	dev->bar_len[bar] = len;
	return va;
}

static void lkpi_pci_unmap_bar(struct pci_dev *dev, int bar)
{
	if (bar < 0 || bar >= PCI_STD_NUM_BARS || !dev->bar_va[bar])
		return;
	lkpi_os_pci_unmap_bar(dev->os, bar);
	dev->bar_va[bar] = NULL;
	dev->bar_len[bar] = 0;
	dev->iomap_table[bar] = NULL;
}

void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	(void)maxlen;	/* partial mappings are not supported; map the whole BAR */
	return lkpi_pci_map_bar(dev, bar);
}

void pci_iounmap(struct pci_dev *dev, void __iomem *addr)
{
	int bar;

	if (!dev || !addr)
		return;
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (dev->bar_va[bar] == addr) {
			lkpi_pci_unmap_bar(dev, bar);
			return;
		}
	}
}

static void lkpi_pcim_iomap_release(struct device *d, void *res)
{
	struct pci_dev *pdev = to_pci_dev(d);
	int bar;

	(void)res;
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		if (pdev->iomap_table[bar])
			lkpi_pci_unmap_bar(pdev, bar);
	to_lkpi(pdev)->iomap_devres = NULL;
}

static int lkpi_pcim_iomap_prepare(struct pci_dev *dev)
{
	struct lkpi_pci_dev *lp = to_lkpi(dev);
	void *res;

	if (lp->iomap_devres)
		return 0;
	res = devres_alloc(lkpi_pcim_iomap_release, sizeof(int), GFP_KERNEL);
	if (!res)
		return -ENOMEM;
	devres_add(&dev->dev, res);
	lp->iomap_devres = res;
	return 0;
}

int pcim_iomap_regions(struct pci_dev *dev, int mask, const char *name)
{
	int bar, ret;

	(void)name;
	if (!dev)
		return -ENODEV;
	ret = lkpi_pcim_iomap_prepare(dev);
	if (ret)
		return ret;
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (!(mask & (1 << bar)))
			continue;
		if (!lkpi_pci_map_bar(dev, bar)) {
			dev_err(&dev->dev, "failed to map BAR %d\n", bar);
			ret = -ENOMEM;
			goto err;
		}
		dev->iomap_table[bar] = dev->bar_va[bar];
	}
	return 0;
err:
	while (bar-- > 0)
		if (mask & (1 << bar))
			lkpi_pci_unmap_bar(dev, bar);
	return ret;
}

void __iomem * const *pcim_iomap_table(struct pci_dev *dev)
{
	return dev->iomap_table;
}

void __iomem *pcim_iomap_region(struct pci_dev *dev, int bar, const char *name)
{
	int ret;

	(void)name;
	if (!dev || bar < 0 || bar >= PCI_STD_NUM_BARS)
		return ERR_PTR(-EINVAL);
	ret = lkpi_pcim_iomap_prepare(dev);
	if (ret)
		return ERR_PTR(ret);
	if (!lkpi_pci_map_bar(dev, bar))
		return ERR_PTR(-ENOMEM);
	dev->iomap_table[bar] = dev->bar_va[bar];
	return dev->bar_va[bar];
}

void pcim_iounmap_regions(struct pci_dev *dev, int mask)
{
	int bar;

	if (!dev)
		return;
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		if ((mask & (1 << bar)) && dev->iomap_table[bar])
			lkpi_pci_unmap_bar(dev, bar);
}

int pci_request_region(struct pci_dev *dev, int bar, const char *name)
{
	(void)dev; (void)bar; (void)name;
	return 0;	/* IOKit already owns the resource on our behalf */
}

void pci_release_region(struct pci_dev *dev, int bar)
{
	(void)dev; (void)bar;
}

int pci_request_regions(struct pci_dev *dev, const char *name)
{
	(void)dev; (void)name;
	return 0;
}

void pci_release_regions(struct pci_dev *dev)
{
	(void)dev;
}

resource_size_t pci_resource_start(struct pci_dev *dev, int bar)
{
	(void)dev; (void)bar;
	return 0;	/* physical BAR addresses are not exposed by the backend */
}

resource_size_t pci_resource_len(struct pci_dev *dev, int bar)
{
	if (!dev || bar < 0 || bar >= PCI_STD_NUM_BARS)
		return 0;
	return dev->bar_len[bar];
}

/* ---- interrupts ------------------------------------------------------ */

static struct pci_dev *lkpi_irq_lookup(unsigned int irq)
{
	if (irq == 0 || irq > LKPI_PCI_NIRQ)
		return NULL;
	return lkpi_irq_table[irq - 1];
}

int pci_alloc_irq_vectors(struct pci_dev *dev, unsigned int min_vecs, unsigned int max_vecs,
    unsigned int flags)
{
	struct lkpi_pci_dev *lp;
	unsigned int i;

	if (!dev || !dev->os || min_vecs > 1)
		return -EINVAL;
	(void)max_vecs;
	lp = to_lkpi(dev);
	if (lp->irq_vectors)
		return 1;	/* already allocated: one vector is all we offer */

	mutex_lock(&lkpi_pci_lock);
	for (i = 0; i < LKPI_PCI_NIRQ; i++) {
		if (!lkpi_irq_table[i]) {
			lkpi_irq_table[i] = dev;
			break;
		}
	}
	mutex_unlock(&lkpi_pci_lock);
	if (i == LKPI_PCI_NIRQ)
		return -ENOSPC;

	lp->want_msi = (flags & (PCI_IRQ_MSI | PCI_IRQ_MSIX)) != 0;
	lp->irq_vectors = true;
	dev->irq = i + 1;
	dev->msi_enabled = lp->want_msi;
	return 1;
}

static void lkpi_pci_irq_uninstall(struct pci_dev *dev)
{
	struct lkpi_pci_dev *lp = to_lkpi(dev);

	if (lp->irq_installed) {
		lkpi_os_pci_irq_disable(dev->os);
		lp->irq_installed = false;
	}
}

void pci_free_irq_vectors(struct pci_dev *dev)
{
	struct lkpi_pci_dev *lp;

	if (!dev)
		return;
	lp = to_lkpi(dev);
	if (!lp->irq_vectors)
		return;
	if (dev->irq_handler) {
		dev_warn(&dev->dev, "freeing irq vectors with handler still installed\n");
		lkpi_pci_irq_uninstall(dev);
		dev->irq_handler = NULL;
		dev->irq_dev_id = NULL;
		lp->irq_thread_fn = NULL;
	}
	mutex_lock(&lkpi_pci_lock);
	if (dev->irq >= 1 && dev->irq <= LKPI_PCI_NIRQ && lkpi_irq_table[dev->irq - 1] == dev)
		lkpi_irq_table[dev->irq - 1] = NULL;
	mutex_unlock(&lkpi_pci_lock);
	lp->irq_vectors = false;
	lp->want_msi = false;
	dev->msi_enabled = 0;
	dev->irq = 0;
}

/* Runs on the backend's interrupt workloop thread. */
static void lkpi_pci_irq_trampoline(void *arg)
{
	struct pci_dev *dev = arg;
	struct lkpi_pci_dev *lp = to_lkpi(dev);
	irq_handler_t handler = dev->irq_handler;
	irqreturn_t ret;

	if (!handler)
		return;
	ret = handler((int)dev->irq, dev->irq_dev_id);
	if (ret == IRQ_WAKE_THREAD && lp->irq_thread_fn)
		lp->irq_thread_fn((int)dev->irq, dev->irq_dev_id);
}

int request_threaded_irq(unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn,
    unsigned long flags, const char *name, void *dev_id)
{
	struct pci_dev *dev = lkpi_irq_lookup(irq);
	struct lkpi_pci_dev *lp;
	int ret;

	(void)flags; (void)name;
	if (!dev)
		return -EINVAL;
	if (!handler && !thread_fn)
		return -EINVAL;
	lp = to_lkpi(dev);
	if (dev->irq_handler)
		return -EBUSY;	/* one handler per device */

	dev->irq_handler = handler ? handler : thread_fn;
	dev->irq_dev_id = dev_id;
	lp->irq_thread_fn = handler ? thread_fn : NULL;
	ret = lkpi_os_pci_irq_enable(dev->os, lkpi_pci_irq_trampoline, dev, lp->want_msi);
	if (ret < 0) {
		dev->irq_handler = NULL;
		dev->irq_dev_id = NULL;
		lp->irq_thread_fn = NULL;
		return ret;
	}
	lp->irq_installed = true;
	return 0;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct pci_dev *dev = lkpi_irq_lookup(irq);
	struct lkpi_pci_dev *lp;

	if (!dev || !dev->irq_handler)
		return;
	if (dev->irq_dev_id != dev_id) {
		dev_warn(&dev->dev, "free_irq: dev_id mismatch\n");
		return;
	}
	lp = to_lkpi(dev);
	lkpi_pci_irq_uninstall(dev);
	lkpi_os_pci_irq_sync(dev->os);
	dev->irq_handler = NULL;
	dev->irq_dev_id = NULL;
	lp->irq_thread_fn = NULL;
}

struct lkpi_devm_irq {
	unsigned int irq;
	void *dev_id;
};

static void lkpi_devm_irq_release(struct device *d, void *res)
{
	struct lkpi_devm_irq *r = res;
	struct pci_dev *pdev = lkpi_irq_lookup(r->irq);

	if (pdev && to_lkpi(pdev)->irq_devres == res)
		to_lkpi(pdev)->irq_devres = NULL;
	(void)d;
	free_irq(r->irq, r->dev_id);
}

int devm_request_threaded_irq(struct device *dev, unsigned int irq, irq_handler_t handler,
    irq_handler_t thread_fn, unsigned long flags, const char *name, void *dev_id)
{
	struct pci_dev *pdev = lkpi_irq_lookup(irq);
	struct lkpi_devm_irq *r;
	int ret;

	if (!pdev)
		return -EINVAL;
	r = devres_alloc(lkpi_devm_irq_release, sizeof(*r), GFP_KERNEL);
	if (!r)
		return -ENOMEM;
	r->irq = irq;
	r->dev_id = dev_id;
	ret = request_threaded_irq(irq, handler, thread_fn, flags, name, dev_id);
	if (ret) {
		devres_free(r);
		return ret;
	}
	to_lkpi(pdev)->irq_devres = r;
	devres_add(dev, r);
	return 0;
}

void devm_free_irq(struct device *dev, unsigned int irq, void *dev_id)
{
	struct pci_dev *pdev = lkpi_irq_lookup(irq);
	void *res;

	if (!pdev)
		return;
	res = to_lkpi(pdev)->irq_devres;
	if (res) {
		to_lkpi(pdev)->irq_devres = NULL;
		devres_destroy(dev, lkpi_devm_irq_release, res);
	}
	free_irq(irq, dev_id);
}

void disable_irq(unsigned int irq)
{
	struct pci_dev *dev = lkpi_irq_lookup(irq);

	if (!dev)
		return;
	lkpi_pci_irq_uninstall(dev);
	lkpi_os_pci_irq_sync(dev->os);
}

void disable_irq_nosync(unsigned int irq)
{
	struct pci_dev *dev = lkpi_irq_lookup(irq);

	if (dev)
		lkpi_pci_irq_uninstall(dev);
}

void enable_irq(unsigned int irq)
{
	struct pci_dev *dev = lkpi_irq_lookup(irq);
	struct lkpi_pci_dev *lp;

	if (!dev || !dev->irq_handler)
		return;
	lp = to_lkpi(dev);
	if (lp->irq_installed)
		return;
	if (lkpi_os_pci_irq_enable(dev->os, lkpi_pci_irq_trampoline, dev, lp->want_msi) == 0)
		lp->irq_installed = true;
	else
		dev_err(&dev->dev, "enable_irq: backend refused to re-enable the interrupt\n");
}

void synchronize_irq(unsigned int irq)
{
	struct pci_dev *dev = lkpi_irq_lookup(irq);

	if (dev && dev->os)
		lkpi_os_pci_irq_sync(dev->os);
}

/* ---- power management ------------------------------------------------ */

int pci_save_state(struct pci_dev *dev)
{
	struct lkpi_pci_dev *lp;
	int i;

	if (!dev || !dev->os)
		return -ENODEV;
	lp = to_lkpi(dev);
	for (i = 0; i < LKPI_PCI_SAVED_DWORDS; i++)
		lp->saved_cfg[i] = lkpi_cfg_read(dev, i * 4, 4);
	lp->state_saved = true;
	return 0;
}

void pci_restore_state(struct pci_dev *dev)
{
	struct lkpi_pci_dev *lp;
	int i;

	if (!dev || !dev->os)
		return;
	lp = to_lkpi(dev);
	if (!lp->state_saved)
		return;
	/*
	 * Same order as Linux: everything above the header first (capabilities,
	 * BARs), then the command register last so decoding is re-enabled only
	 * once the BARs are back in place.
	 */
	for (i = LKPI_PCI_SAVED_DWORDS - 1; i >= 0; i--) {
		if (lkpi_cfg_read(dev, i * 4, 4) != lp->saved_cfg[i])
			lkpi_cfg_write(dev, i * 4, 4, lp->saved_cfg[i]);
	}
	lp->state_saved = false;
}

int pci_set_power_state(struct pci_dev *dev, pci_power_t state)
{
	struct lkpi_pci_dev *lp;
	u16 pmcsr;

	if (!dev || !dev->os)
		return -ENODEV;
	lp = to_lkpi(dev);
	if (state == PCI_D3cold)
		state = PCI_D3hot;	/* D3cold needs platform help we do not have */
	if (state < PCI_D0 || state > PCI_D3hot)
		return -EINVAL;
	if (dev->current_state == state)
		return 0;
	if (!lp->pm_cap) {
		dev->current_state = state;
		return 0;
	}
	pmcsr = (u16)lkpi_cfg_read(dev, lp->pm_cap + PCI_PM_CTRL, 2);
	pmcsr = (pmcsr & ~PCI_PM_CTRL_STATE_MASK) | (u16)state;
	lkpi_cfg_write(dev, lp->pm_cap + PCI_PM_CTRL, 2, pmcsr);
	/* PCI PM 1.2 requires 10 ms after a D3hot->D0 transition, 200 us otherwise */
	if (state == PCI_D3hot || dev->current_state == PCI_D3hot)
		lkpi_os_sleep_ms(10);
	else
		lkpi_os_delay_us(200);
	dev->current_state = state;
	return 0;
}

pci_power_t pci_choose_state(struct pci_dev *dev, pm_message_t state)
{
	(void)dev; (void)state;
	return PCI_D3hot;
}

int pci_enable_wake(struct pci_dev *dev, pci_power_t state, bool enable)
{
	struct lkpi_pci_dev *lp;
	u16 pmcsr;

	(void)state;
	if (!dev || !dev->os)
		return -ENODEV;
	lp = to_lkpi(dev);
	if (!lp->pm_cap)
		return enable ? -EINVAL : 0;
	pmcsr = (u16)lkpi_cfg_read(dev, lp->pm_cap + PCI_PM_CTRL, 2);
	if (enable)
		pmcsr |= PCI_PM_CTRL_PME_ENABLE;
	else
		pmcsr &= ~PCI_PM_CTRL_PME_ENABLE;
	pmcsr |= PCI_PM_CTRL_PME_STATUS;	/* write-one-to-clear */
	lkpi_cfg_write(dev, lp->pm_cap + PCI_PM_CTRL, 2, pmcsr);
	return 0;
}

int pci_prepare_to_sleep(struct pci_dev *dev)
{
	(void)dev;
	return 0;	/* IOKit power management drives the D-state transitions */
}

int pci_back_from_sleep(struct pci_dev *dev)
{
	(void)dev;
	return 0;
}

/* ---- driver registry ------------------------------------------------- */

int pci_register_driver(struct pci_driver *drv)
{
	if (!drv || !drv->probe)
		return -EINVAL;
	if (!drv->driver.name)
		drv->driver.name = drv->name;
	mutex_lock(&lkpi_pci_lock);
	list_add_tail(&drv->node, &lkpi_pci_drivers);
	mutex_unlock(&lkpi_pci_lock);
	return 0;
}

void pci_unregister_driver(struct pci_driver *drv)
{
	struct pci_dev *pdev, *tmp;

	if (!drv)
		return;
	/* Devices still bound to the driver lose it, as Linux would remove them. */
	list_for_each_entry_safe(pdev, tmp, &lkpi_pci_devices, node) {
		if (pdev->driver == drv) {
			if (drv->remove)
				drv->remove(pdev);
			pdev->driver = NULL;
			pdev->dev.driver = NULL;
		}
	}
	mutex_lock(&lkpi_pci_lock);
	if (drv->node.next && drv->node.prev)
		list_del_init(&drv->node);
	mutex_unlock(&lkpi_pci_lock);
}

static bool lkpi_pci_id_match(const struct pci_device_id *id, const struct pci_dev *dev)
{
	const u32 any = (u32)PCI_ANY_ID;

	if (id->vendor != any && id->vendor != dev->vendor)
		return false;
	if (id->device != any && id->device != dev->device)
		return false;
	if (id->subvendor != any && id->subvendor != dev->subsystem_vendor)
		return false;
	if (id->subdevice != any && id->subdevice != dev->subsystem_device)
		return false;
	if ((id->class ^ dev->class) & id->class_mask)
		return false;
	return true;
}

static const struct pci_device_id *lkpi_pci_match_driver(const struct pci_driver *drv,
    const struct pci_dev *dev)
{
	const struct pci_device_id *id;

	if (!drv->id_table)
		return NULL;
	for (id = drv->id_table; id->vendor || id->device || id->subvendor ||
	     id->subdevice || id->class || id->class_mask || id->driver_data; id++) {
		if (lkpi_pci_id_match(id, dev))
			return id;
	}
	return NULL;
}

/* ---- attach / detach -------------------------------------------------- */

static void lkpi_pci_release(struct pci_dev *pdev)
{
	struct lkpi_pci_dev *lp = to_lkpi(pdev);
	int bar;

	/* devres runs devm_free_irq / pcim releases; the rest is defensive. */
	lkpi_device_fini(&pdev->dev);
	if (pdev->irq_handler) {
		lkpi_pci_irq_uninstall(pdev);
		lkpi_os_pci_irq_sync(pdev->os);
		pdev->irq_handler = NULL;
		pdev->irq_dev_id = NULL;
		lp->irq_thread_fn = NULL;
	}
	pci_free_irq_vectors(pdev);
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		lkpi_pci_unmap_bar(pdev, bar);
	if (pdev->is_busmaster)
		pci_clear_master(pdev);

	mutex_lock(&lkpi_pci_lock);
	if (pdev->node.next && pdev->node.prev)
		list_del_init(&pdev->node);
	mutex_unlock(&lkpi_pci_lock);

	lkpi_os_pci_set_priv(pdev->os, NULL);
	kfree(lp);
}

struct pci_dev *lkpi_pci_attach(lkpi_os_pci_t os)
{
	struct lkpi_os_pci_info info;
	struct lkpi_pci_dev *lp;
	struct pci_dev *pdev;
	struct pci_driver *drv;
	const struct pci_device_id *id = NULL;
	char name[32];
	u32 class_rev;
	int ret;

	if (!os)
		return NULL;

	lp = kzalloc(sizeof(*lp), GFP_KERNEL);
	if (!lp)
		return NULL;
	pdev = &lp->pdev;

	memset(&info, 0, sizeof(info));
	lkpi_os_pci_get_info(os, &info);
	snprintf(name, sizeof(name), "0000:%02x:%02x.%u", info.bus, info.slot, info.func & 7);

	lkpi_device_init(&pdev->dev, NULL, name);
	pdev->dev.pci = os;
	pdev->os = os;
	strscpy(pdev->name, name, sizeof(pdev->name));
	INIT_LIST_HEAD(&pdev->node);

	pdev->vendor = info.vendor;
	pdev->device = info.device;
	pdev->subsystem_vendor = info.subsystem_vendor;
	pdev->subsystem_device = info.subsystem_device;
	pdev->revision = info.revision;
	pdev->devfn = PCI_DEVFN(info.slot, info.func);
	/* class: 24-bit base/sub/prog-if straight from config space */
	class_rev = lkpi_os_pci_cfg_read(os, PCI_CLASS_REVISION, 4);
	pdev->class = class_rev >> 8;
	if (!pdev->class)
		pdev->class = ((u32)info.class_code[0] << 16) | ((u32)info.class_code[1] << 8) |
			      info.class_code[2];
	pdev->hdr_type = (u8)lkpi_os_pci_cfg_read(os, PCI_HEADER_TYPE, 1) & 0x7f;
	pdev->current_state = PCI_UNKNOWN;

	/* no upstream bridge object on macOS: bus->self stays NULL */
	lp->bus.self = NULL;
	lp->bus.parent = NULL;
	lp->bus.number = info.bus;
	pdev->bus = &lp->bus;

	pdev->pcie_cap = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	lp->pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM);

	lkpi_os_pci_set_priv(os, pdev);
	mutex_lock(&lkpi_pci_lock);
	list_add_tail(&pdev->node, &lkpi_pci_devices);
	mutex_unlock(&lkpi_pci_lock);

	dev_info(&pdev->dev, "PCI device %04x:%04x (subsystem %04x:%04x, rev %02x)%s\n",
		 pdev->vendor, pdev->device, pdev->subsystem_vendor, pdev->subsystem_device,
		 pdev->revision, pdev->pcie_cap ? " PCIe" : "");

	list_for_each_entry(drv, &lkpi_pci_drivers, node) {
		id = lkpi_pci_match_driver(drv, pdev);
		if (!id)
			continue;
		pdev->driver = drv;
		pdev->dev.driver = &drv->driver;
		ret = drv->probe(pdev, id);
		if (ret == 0)
			return pdev;
		dev_err(&pdev->dev, "%s: probe failed: %d\n", drv->name, ret);
		pdev->driver = NULL;
		pdev->dev.driver = NULL;
		lkpi_pci_release(pdev);
		return NULL;
	}

	dev_warn(&pdev->dev, "no registered driver matches %04x:%04x\n", pdev->vendor,
		 pdev->device);
	lkpi_pci_release(pdev);
	return NULL;
}

void lkpi_pci_detach(struct pci_dev *pdev)
{
	if (!pdev)
		return;
	if (pdev->driver) {
		if (pdev->driver->remove)
			pdev->driver->remove(pdev);
		pdev->driver = NULL;
		pdev->dev.driver = NULL;
	}
	lkpi_pci_release(pdev);
}

int lkpi_pci_suspend(struct pci_dev *pdev)
{
	const struct pci_driver *drv;

	if (!pdev || !pdev->driver)
		return 0;
	drv = pdev->driver;
	if (drv->driver.pm && drv->driver.pm->suspend)
		return drv->driver.pm->suspend(&pdev->dev);
	if (drv->suspend)
		return drv->suspend(pdev, PMSG_SUSPEND);
	return 0;
}

int lkpi_pci_resume(struct pci_dev *pdev)
{
	const struct pci_driver *drv;

	if (!pdev || !pdev->driver)
		return 0;
	drv = pdev->driver;
	if (drv->driver.pm && drv->driver.pm->resume)
		return drv->driver.pm->resume(&pdev->dev);
	if (drv->resume)
		return drv->resume(pdev);
	return 0;
}

void lkpi_pci_shutdown(struct pci_dev *pdev)
{
	if (pdev && pdev->driver && pdev->driver->shutdown)
		pdev->driver->shutdown(pdev);
}

/* ---- subsystem hooks -------------------------------------------------- */

int lkpi_pci_init(void)
{
	memset(lkpi_irq_table, 0, sizeof(lkpi_irq_table));
	INIT_LIST_HEAD(&lkpi_pci_drivers);
	INIT_LIST_HEAD(&lkpi_pci_devices);
	return 0;
}

void lkpi_pci_fini(void)
{
	struct pci_dev *pdev, *tmp;

	list_for_each_entry_safe(pdev, tmp, &lkpi_pci_devices, node)
		lkpi_pci_detach(pdev);
	INIT_LIST_HEAD(&lkpi_pci_drivers);
	mutex_destroy(&lkpi_pci_lock);
}
