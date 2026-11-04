/* SPDX-License-Identifier: MIT */
/*
 * lkpi/os.h - the OS glue ABI between the C LinuxKPI layer and the
 * C++ IOKit kext.
 *
 * Everything under linux/ and net/ is plain C and may only reach the host
 * kernel through the functions declared here.  The kext implements them in
 * src/kext/lkpi_os_iokit.cpp; tests/host implements them with libc so the
 * headers can be exercised on a developer machine.  Keep this file free of
 * Linux and IOKit types: only C scalars, opaque handles and callbacks.
 */
#ifndef _LKPI_OS_H_
#define _LKPI_OS_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- memory --------------------------------------------------------- */
/* Allocation never fails for small sizes in XNU; NULL is still checked. */
void	*lkpi_os_malloc(size_t size, bool zero, bool may_sleep);
void	 lkpi_os_free(void *ptr);		/* size recovered from header */
size_t	 lkpi_os_malloc_size(const void *ptr);
/* Page-sized, physically contiguous buffers used for DMA and page pools. */
void	*lkpi_os_alloc_pages(size_t size, bool zero);
void	 lkpi_os_free_pages(void *ptr, size_t size);
size_t	 lkpi_os_page_size(void);

/* ---- logging -------------------------------------------------------- */
void	 lkpi_os_vlog(int level, const char *fmt, va_list ap);
void	 lkpi_os_log(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void	 lkpi_os_panic(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
void	 lkpi_os_dump_stack(void);
enum { LKPI_LOG_EMERG, LKPI_LOG_ERR, LKPI_LOG_WARN, LKPI_LOG_NOTICE,
       LKPI_LOG_INFO, LKPI_LOG_DEBUG };

/* ---- time ----------------------------------------------------------- */
uint64_t lkpi_os_uptime_ns(void);	/* monotonic, excludes sleep */
uint64_t lkpi_os_boottime_ns(void);	/* monotonic, includes sleep */
uint64_t lkpi_os_realtime_ns(void);
void	 lkpi_os_delay_us(uint32_t us);	/* busy wait */
void	 lkpi_os_sleep_ms(uint32_t ms);	/* sleeping wait */
bool	 lkpi_os_may_sleep(void);	/* false in interrupt context */

/* ---- locks ----------------------------------------------------------
 * Spinlocks are IOSimpleLock, mutexes are IOLock.  Both are allocated by
 * the backend because their sizes are opaque to C.  The "irq" variants
 * disable interrupts on the local CPU and return the previous state.
 */
typedef struct lkpi_os_spinlock *lkpi_os_spinlock_t;
typedef struct lkpi_os_mutex *lkpi_os_mutex_t;
typedef uintptr_t lkpi_os_irqstate_t;

lkpi_os_spinlock_t lkpi_os_spinlock_alloc(void);
void	 lkpi_os_spinlock_free(lkpi_os_spinlock_t);
void	 lkpi_os_spin_lock(lkpi_os_spinlock_t);
bool	 lkpi_os_spin_trylock(lkpi_os_spinlock_t);
void	 lkpi_os_spin_unlock(lkpi_os_spinlock_t);
lkpi_os_irqstate_t lkpi_os_spin_lock_irq(lkpi_os_spinlock_t);
void	 lkpi_os_spin_unlock_irq(lkpi_os_spinlock_t, lkpi_os_irqstate_t);

lkpi_os_mutex_t lkpi_os_mutex_alloc(void);
void	 lkpi_os_mutex_free(lkpi_os_mutex_t);
void	 lkpi_os_mutex_lock(lkpi_os_mutex_t);
bool	 lkpi_os_mutex_trylock(lkpi_os_mutex_t);
void	 lkpi_os_mutex_unlock(lkpi_os_mutex_t);
bool	 lkpi_os_mutex_is_locked(lkpi_os_mutex_t);

/* Sleep/wakeup on an arbitrary address while holding a mutex
 * (IOLockSleepDeadline / IOLockWakeup).  Returns false on timeout. */
bool	 lkpi_os_mutex_sleep(lkpi_os_mutex_t, const void *event, uint64_t timeout_ns);
void	 lkpi_os_wakeup(lkpi_os_mutex_t, const void *event, bool one);

/* ---- threads -------------------------------------------------------- */
typedef struct lkpi_os_thread *lkpi_os_thread_t;
typedef void (*lkpi_os_thread_fn)(void *arg);
lkpi_os_thread_t lkpi_os_thread_create(lkpi_os_thread_fn fn, void *arg, const char *name);
void	 lkpi_os_thread_exit(void) __attribute__((noreturn));
void	 lkpi_os_thread_set_realtime(lkpi_os_thread_t, bool rt);
lkpi_os_thread_t lkpi_os_thread_self(void);
void	 lkpi_os_thread_yield(void);
/* Block the calling thread until lkpi_os_thread_wake() or timeout.  Returns
 * false on timeout.  Wakeups that arrive before the wait are not lost. */
bool	 lkpi_os_thread_wait(lkpi_os_thread_t self, uint64_t timeout_ns);
void	 lkpi_os_thread_wake(lkpi_os_thread_t);
void	 lkpi_os_thread_release(lkpi_os_thread_t); /* drop reference after exit */

/* ---- one-shot timers ------------------------------------------------ */
typedef struct lkpi_os_timer *lkpi_os_timer_t;
typedef void (*lkpi_os_timer_fn)(void *arg);
lkpi_os_timer_t lkpi_os_timer_create(lkpi_os_timer_fn fn, void *arg);
void	 lkpi_os_timer_arm(lkpi_os_timer_t, uint64_t deadline_uptime_ns);
bool	 lkpi_os_timer_cancel(lkpi_os_timer_t);	/* true if it was pending */
/* Cancel and wait for a callback that is already running (not from the
 * callback itself).  Returns true if the timer was pending. */
bool	 lkpi_os_timer_cancel_sync(lkpi_os_timer_t);
/* 10 ms periodic tick that advances jiffies (linux/jiffies.h); started by
 * the backend once at lkpi_runtime_init() time. */
void	 lkpi_os_timer_destroy(lkpi_os_timer_t);	/* waits for a running callback */

/* ---- MMIO ----------------------------------------------------------- */
uint32_t lkpi_os_mmio_read32(const volatile void *addr);
void	 lkpi_os_mmio_write32(volatile void *addr, uint32_t val);
uint16_t lkpi_os_mmio_read16(const volatile void *addr);
void	 lkpi_os_mmio_write16(volatile void *addr, uint16_t val);
uint8_t	 lkpi_os_mmio_read8(const volatile void *addr);
void	 lkpi_os_mmio_write8(volatile void *addr, uint8_t val);
void	 lkpi_os_mmio_barrier(void);

/* ---- PCI ------------------------------------------------------------
 * The kext hands the LinuxKPI bus layer an opaque device handle; all
 * accesses go back through these calls.
 */
typedef struct lkpi_os_pci *lkpi_os_pci_t;
struct lkpi_os_pci_info {
	uint16_t vendor, device, subsystem_vendor, subsystem_device;
	uint8_t  revision, bus, slot, func;
	uint8_t  class_code[3];
	char	 name[32];
};
void	 lkpi_os_pci_get_info(lkpi_os_pci_t, struct lkpi_os_pci_info *);
uint32_t lkpi_os_pci_cfg_read(lkpi_os_pci_t, uint32_t off, int width);
void	 lkpi_os_pci_cfg_write(lkpi_os_pci_t, uint32_t off, int width, uint32_t val);
/* Map BAR @bar; returns kernel virtual address and size (0 on failure). */
void	*lkpi_os_pci_map_bar(lkpi_os_pci_t, int bar, size_t *size);
void	 lkpi_os_pci_unmap_bar(lkpi_os_pci_t, int bar);
/* Interrupt.  The handler runs in the kext's interrupt workloop, so it may
 * take spinlocks and schedule work but must not sleep. */
typedef void (*lkpi_os_irq_fn)(void *arg);
int	 lkpi_os_pci_irq_enable(lkpi_os_pci_t, lkpi_os_irq_fn fn, void *arg, bool want_msi);
void	 lkpi_os_pci_irq_disable(lkpi_os_pci_t);
void	 lkpi_os_pci_irq_sync(lkpi_os_pci_t);	/* wait for running handler */
void	 lkpi_os_pci_set_bus_master(lkpi_os_pci_t, bool on);
/* Opaque private pointer the C bus layer stores on the handle. */
void	 lkpi_os_pci_set_priv(lkpi_os_pci_t, void *priv);
void	*lkpi_os_pci_get_priv(lkpi_os_pci_t);

/* ---- DMA ------------------------------------------------------------
 * Coherent buffers come from IOBufferMemoryDescriptor; streaming mappings
 * are IODMACommand instances keyed by the bus address they produced.
 */
typedef uint64_t lkpi_os_dma_addr_t;
void	*lkpi_os_dma_alloc_coherent(lkpi_os_pci_t, size_t size, uint64_t mask,
	    lkpi_os_dma_addr_t *bus_addr);
void	 lkpi_os_dma_free_coherent(lkpi_os_pci_t, size_t size, void *va, lkpi_os_dma_addr_t bus_addr);
/* Returns 0 on failure (never a valid bus address). */
lkpi_os_dma_addr_t lkpi_os_dma_map(lkpi_os_pci_t, void *va, size_t len, int dir, uint64_t mask);
void	 lkpi_os_dma_unmap(lkpi_os_pci_t, lkpi_os_dma_addr_t, size_t len, int dir);
void	 lkpi_os_dma_sync(lkpi_os_pci_t, lkpi_os_dma_addr_t, size_t len, int dir, bool for_cpu);
enum { LKPI_DMA_BIDIRECTIONAL = 0, LKPI_DMA_TO_DEVICE = 1,
       LKPI_DMA_FROM_DEVICE = 2, LKPI_DMA_NONE = 3 };

/* ---- firmware --------------------------------------------------------
 * Blobs ship in the kext bundle under Contents/Resources/<name>; the
 * backend uses OSKextRequestResource.  Returns 0 and a buffer owned by the
 * caller (free with lkpi_os_free) or a negative errno.
 */
int	 lkpi_os_firmware_load(const char *name, void **data, size_t *size);

/* ---- randomness ----------------------------------------------------- */
void	 lkpi_os_random_bytes(void *buf, size_t len);

/* ---- per-thread slot ------------------------------------------------- */
/* One pointer the C layer may attach to the current thread (used to find
 * struct task_struct for `current`).  Threads that never set it read NULL. */
void	*lkpi_os_thread_get_slot(void);
void	 lkpi_os_thread_set_slot(void *p);

/* ---- misc ------------------------------------------------------------ */
int	 lkpi_os_cpu_count(void);
int	 lkpi_os_cpu_id(void);
/* Opaque per-instance pointer back to the IOKit provider, for the few
 * places (ieee80211 glue, debug) that need to reach the kext. */
void	*lkpi_os_pci_provider(lkpi_os_pci_t);

#ifdef __cplusplus
}
#endif
#endif /* _LKPI_OS_H_ */
