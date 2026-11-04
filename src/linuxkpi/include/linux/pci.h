/* SPDX-License-Identifier: MIT */
/*
 * PCI device model.  The kext creates one struct pci_dev per matched IOKit
 * provider (src/linuxkpi/src/pci.c: lkpi_pci_attach) and calls the
 * registered pci_driver's probe().  Config space, BAR mapping and the
 * interrupt go through lkpi/os.h.
 */
#ifndef _LKPI_LINUX_PCI_H_
#define _LKPI_LINUX_PCI_H_
#include <linux/types.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/pci_ids.h>
#include <linux/pci_regs.h>
#include <linux/pm.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <lkpi/os.h>

struct pci_device_id {
	u32 vendor, device;
	u32 subvendor, subdevice;
	u32 class, class_mask;
	kernel_ulong_t driver_data;
};
#define PCI_DEVICE(vend, dev) \
	.vendor = (vend), .device = (dev), .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID
#define PCI_DEVICE_SUB(vend, dev, subvend, subdev) \
	.vendor = (vend), .device = (dev), .subvendor = (subvend), .subdevice = (subdev)
#define PCI_DEVICE_DATA(vend, dev, data) \
	.vendor = PCI_VENDOR_ID_##vend, .device = PCI_DEVICE_ID_##vend##_##dev, \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, .driver_data = (kernel_ulong_t)(data)
#define PCI_VDEVICE(vend, dev) \
	.vendor = PCI_VENDOR_ID_##vend, .device = (dev), .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, 0, 0

#define PCI_STD_NUM_BARS 6
#define PCI_IRQ_INTX		(1 << 0)
#define PCI_IRQ_LEGACY		PCI_IRQ_INTX
#define PCI_IRQ_MSI		(1 << 1)
#define PCI_IRQ_MSIX		(1 << 2)
#define PCI_IRQ_AFFINITY	(1 << 3)
#define PCI_IRQ_ALL_TYPES	(PCI_IRQ_INTX | PCI_IRQ_MSI | PCI_IRQ_MSIX)

struct pci_bus;
struct pci_driver;

struct pci_dev {
	struct device dev;
	struct pci_bus *bus;
	const struct pci_driver *driver;
	u16 vendor, device;
	u16 subsystem_vendor, subsystem_device;
	u8 revision;
	u8 hdr_type;
	u32 class;
	unsigned int devfn;
	unsigned int irq;
	int msi_enabled;
	int msix_enabled;
	pci_power_t current_state;
	bool is_busmaster;
	bool managed;			/* pcim_enable_device() was used */
	void __iomem *bar_va[PCI_STD_NUM_BARS];
	size_t bar_len[PCI_STD_NUM_BARS];
	void __iomem *iomap_table[PCI_STD_NUM_BARS];
	lkpi_os_pci_t os;
	struct list_head node;
	char name[32];
	int pcie_cap;			/* offset of the PCIe capability, 0 if none */
	irq_handler_t irq_handler;
	void *irq_dev_id;
};

struct pci_bus {
	struct pci_dev *self;		/* upstream bridge, NULL if root */
	struct pci_bus *parent;
	unsigned char number;
};

struct pci_driver {
	const char *name;
	const struct pci_device_id *id_table;
	int (*probe)(struct pci_dev *dev, const struct pci_device_id *id);
	void (*remove)(struct pci_dev *dev);
	int (*suspend)(struct pci_dev *dev, pm_message_t state);
	int (*resume)(struct pci_dev *dev);
	void (*shutdown)(struct pci_dev *dev);
	struct device_driver driver;
	struct list_head node;
};

#define to_pci_dev(d) container_of(d, struct pci_dev, dev)
#define dev_is_pci_lkpi(d) ((d)->pci != NULL)
static inline const char *pci_name(const struct pci_dev *pdev) { return pdev->name; }
static inline void *pci_get_drvdata(struct pci_dev *pdev) { return dev_get_drvdata(&pdev->dev); }
static inline void pci_set_drvdata(struct pci_dev *pdev, void *data) { dev_set_drvdata(&pdev->dev, data); }
#define PCI_SLOT(devfn)	(((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)	((devfn) & 0x07)
#define PCI_DEVFN(slot, func) ((((slot) & 0x1f) << 3) | ((func) & 0x07))
static inline u8 pci_domain_nr(struct pci_bus *bus) { (void)bus; return 0; }
static inline struct pci_dev *pci_upstream_bridge(struct pci_dev *dev) { return dev->bus ? dev->bus->self : NULL; }
static inline bool pci_is_pcie(struct pci_dev *dev) { return dev->pcie_cap != 0; }
static inline bool pci_is_root_bus(struct pci_bus *bus) { return bus->parent == NULL; }

int pci_register_driver(struct pci_driver *drv);
void pci_unregister_driver(struct pci_driver *drv);
#define module_pci_driver(__pci_driver) \
	int lkpi_modinit_##__pci_driver(void) { return pci_register_driver(&(__pci_driver)); } \
	void lkpi_modexit_##__pci_driver(void) { pci_unregister_driver(&(__pci_driver)); }

int pci_enable_device(struct pci_dev *dev);
void pci_disable_device(struct pci_dev *dev);
int pcim_enable_device(struct pci_dev *dev);
void pci_set_master(struct pci_dev *dev);
void pci_clear_master(struct pci_dev *dev);
int pci_read_config_byte(const struct pci_dev *dev, int where, u8 *val);
int pci_read_config_word(const struct pci_dev *dev, int where, u16 *val);
int pci_read_config_dword(const struct pci_dev *dev, int where, u32 *val);
int pci_write_config_byte(const struct pci_dev *dev, int where, u8 val);
int pci_write_config_word(const struct pci_dev *dev, int where, u16 val);
int pci_write_config_dword(const struct pci_dev *dev, int where, u32 val);
int pci_find_capability(struct pci_dev *dev, int cap);
int pcie_capability_read_word(struct pci_dev *dev, int pos, u16 *val);
int pcie_capability_read_dword(struct pci_dev *dev, int pos, u32 *val);
int pcie_capability_write_word(struct pci_dev *dev, int pos, u16 val);
int pcie_capability_write_dword(struct pci_dev *dev, int pos, u32 val);
int pcie_capability_clear_and_set_word(struct pci_dev *dev, int pos, u16 clear, u16 set);
static inline int pcie_capability_set_word(struct pci_dev *dev, int pos, u16 set)
{
	return pcie_capability_clear_and_set_word(dev, pos, 0, set);
}
static inline int pcie_capability_clear_word(struct pci_dev *dev, int pos, u16 clear)
{
	return pcie_capability_clear_and_set_word(dev, pos, clear, 0);
}
int pci_disable_link_state(struct pci_dev *dev, int state);
int pci_enable_link_state(struct pci_dev *dev, int state);
int pci_alloc_irq_vectors(struct pci_dev *dev, unsigned int min_vecs, unsigned int max_vecs,
    unsigned int flags);
void pci_free_irq_vectors(struct pci_dev *dev);
static inline int pci_irq_vector(struct pci_dev *dev, unsigned int nr) { (void)nr; return dev->irq; }
static inline int pci_enable_msi(struct pci_dev *dev) { return pci_alloc_irq_vectors(dev, 1, 1, PCI_IRQ_MSI) < 0 ? -EINVAL : 0; }
static inline void pci_disable_msi(struct pci_dev *dev) { pci_free_irq_vectors(dev); }
void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen);
void pci_iounmap(struct pci_dev *dev, void __iomem *addr);
int pcim_iomap_regions(struct pci_dev *dev, int mask, const char *name);
void __iomem * const *pcim_iomap_table(struct pci_dev *dev);
void __iomem *pcim_iomap_region(struct pci_dev *dev, int bar, const char *name);
void pcim_iounmap_regions(struct pci_dev *dev, int mask);
int pci_request_region(struct pci_dev *dev, int bar, const char *name);
void pci_release_region(struct pci_dev *dev, int bar);
int pci_request_regions(struct pci_dev *dev, const char *name);
void pci_release_regions(struct pci_dev *dev);
resource_size_t pci_resource_start(struct pci_dev *dev, int bar);
resource_size_t pci_resource_len(struct pci_dev *dev, int bar);
#define pci_resource_end(dev, bar) (pci_resource_start(dev, bar) + pci_resource_len(dev, bar) - 1)
#define pci_resource_flags(dev, bar) (0)
int pci_save_state(struct pci_dev *dev);
void pci_restore_state(struct pci_dev *dev);
int pci_set_power_state(struct pci_dev *dev, pci_power_t state);
pci_power_t pci_choose_state(struct pci_dev *dev, pm_message_t state);
int pci_enable_wake(struct pci_dev *dev, pci_power_t state, bool enable);
static inline int pci_wake_from_d3(struct pci_dev *dev, bool enable) { return pci_enable_wake(dev, PCI_D3hot, enable); }
int pci_prepare_to_sleep(struct pci_dev *dev);
int pci_back_from_sleep(struct pci_dev *dev);
static inline int pci_set_mwi(struct pci_dev *dev) { (void)dev; return 0; }
static inline void pci_clear_mwi(struct pci_dev *dev) { (void)dev; }
static inline int pci_pcie_cap(struct pci_dev *dev) { return dev->pcie_cap; }
static inline int pci_dev_present(const struct pci_device_id *ids) { (void)ids; return 0; }
static inline struct pci_dev *pci_dev_get(struct pci_dev *dev) { return dev; }
static inline void pci_dev_put(struct pci_dev *dev) { (void)dev; }
static inline bool pci_device_is_present(struct pci_dev *dev) { (void)dev; return true; }
static inline int pci_reset_function(struct pci_dev *dev) { (void)dev; return -ENOTSUPP; }
static inline bool pci_pme_capable(struct pci_dev *dev, pci_power_t s) { (void)dev; (void)s; return false; }
#define pci_set_dma_mask(d, m) dma_set_mask(&(d)->dev, m)
#define pci_set_consistent_dma_mask(d, m) dma_set_coherent_mask(&(d)->dev, m)

/* used by the kext to attach/detach a provider */
struct pci_dev *lkpi_pci_attach(lkpi_os_pci_t os);
void lkpi_pci_detach(struct pci_dev *pdev);
int lkpi_pci_suspend(struct pci_dev *pdev);
int lkpi_pci_resume(struct pci_dev *pdev);
void lkpi_pci_shutdown(struct pci_dev *pdev);
#endif /* _LKPI_LINUX_PCI_H_ */
