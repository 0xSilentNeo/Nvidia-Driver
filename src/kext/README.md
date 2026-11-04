# MT7925.kext — IOKit half of the mt76/MT7925 macOS port

This directory holds everything that is compiled as C++ against
Kernel.framework.  The vendored Linux driver (`src/mt76`) and the C
compatibility layer (`src/linuxkpi`) are compiled as plain C into the same
kext binary; they reach the OS only through the C ABI in
`src/linuxkpi/include/lkpi/os.h`, which `lkpi_os_iokit.cpp` implements.

| file | role |
|---|---|
| `Info.plist` | bundle metadata, `MT7925PCIe` personality (IOPCIPrimaryMatch for MT7925/MT7927/MT7928), `Parameters` dictionary mirroring Linux module params |
| `MT7925PCIe.hpp/.cpp` | `class MT7925PCIe : public IOService` — match, start/stop, power states, drives `lkpi_runtime_init()` / `lkpi_pci_attach()` |
| `lkpi_os_iokit.hpp/.cpp` | every `lkpi_os_*` function from `lkpi/os.h` on IOKit/libkern, plus `lkpi_os_start()/stop()` and the `lkpi_os_pci` handle |
| `MT7925_info.c` | the `kmod_info` block Xcode would generate |
| `lkpi_param_stub.c` | fallback `lkpi_param_set()` compiled only if the C layer does not provide one |
| `gen-fw-table.sh` | generates the embedded-firmware table (`EMBED_FIRMWARE=1`) |
| `Makefile` | builds `build/kext/MT7925.kext` with the command line tools only |

## Building

Prerequisites (on the Mac that builds):

* Xcode command line tools (`xcode-select --install`) providing `xcrun`,
  `clang`, `lipo` and a macOS SDK with `Kernel.framework/Headers`.  Only the
  public headers are used — no PrivateHeaders.
* Firmware blobs under `../../firmware/mediatek/...` (run
  `scripts/fetch-firmware.sh`).  They are redistributable but not part of
  the repository.

```sh
make -C src/kext                    # x86_64, macOS 11.0+ deployment target
make -C src/kext ARCHS="x86_64 arm64e"   # fat binary; arm64e is untested
make -C src/kext check-syntax       # clang -fsyntax-only on the C++ files
make -C src/kext clean
```

Useful variables: `KEXT_SDK=/path/to/MacOSX.sdk`, `MIN_OS=11.0`,
`EMBED_FIRMWARE=0|1`, `V=1`.  The top-level `make kext` forwards here.

The resulting bundle is `build/kext/MT7925.kext` with
`Contents/Resources/mediatek/mt7925/*.bin` (and a flat copy of each blob,
see "Firmware" below).

### Loading an unsigned kext

Third-party kexts need user consent and, without a Developer ID with the
kext entitlement, reduced System Integrity Protection:

1. Boot to Recovery and run `csrutil enable --without kext` (or `csrutil
   disable` while developing).  Apple Silicon additionally needs
   `csrutil enable --without kext` plus "Reduced Security" with "Allow user
   management of kernel extensions" in the Startup Security Utility.
2. `make -C src/kext load` runs `sudo kmutil load -p build/kext/MT7925.kext`.
   The first load of a new bundle identifier is rejected until it is
   approved in System Settings > Privacy & Security, after which the
   auxiliary kext collection is rebuilt and a reboot is required.
3. `make -C src/kext unload` runs `kmutil unload -b org.mt7925port.MT7925`.

### Hackintosh / OpenCore

Most users of this port will inject the kext with OpenCore
(`Kernel > Add` in `config.plist`, kext copied to `EFI/OC/Kexts`).  Injected
kexts are linked into the boot kext collection by the bootloader, so:

* SIP and the Privacy & Security approval dance are not needed.
* **`OSKextRequestResource` cannot serve bundle resources** because the kext
  has no bundle on disk for kernelmanagerd to look up.  Build with
  `EMBED_FIRMWARE=1` (the default whenever `../../firmware` holds the blobs)
  so the blobs are linked into the binary and `lkpi_os_firmware_load()` finds
  them without user space.
* If VT-d is disabled (`DisableIoMapper` quirk or `dart=0`) there is no
  IOMMU: DMA addresses are physical addresses.  The backend allocates page
  pool pages below 4 GiB and lets `IODMACommand` bounce anything else, so
  this configuration works, just with an extra copy for stray buffers.

## What is implemented

`lkpi_os_iokit.cpp` covers **every** function declared in `lkpi/os.h`:

* **memory** — `IOMalloc` with a 16-byte `{size, magic}` header so
  `lkpi_os_free(ptr)` and `lkpi_os_malloc_size()` work; page allocations
  through `IOBufferMemoryDescriptor` (`kIOMemoryPhysicallyContiguous`,
  32-bit page-aligned physical mask) tracked in a registry keyed by VA.
* **logging** — `IOLog` with a `MT7925: ` prefix (512-byte stack buffer);
  `LKPI_LOG_DEBUG` is suppressed unless boot-arg `mt7925_debug=1`;
  `panic()`, `OSReportWithBacktrace`.
* **time** — `mach_absolute_time` (uptime), `mach_continuous_time`
  (boottime), `clock_get_calendar_nanotime` (realtime), `IODelay`, `IOSleep`,
  `may_sleep = !ml_at_interrupt_context() && ml_get_interrupts_enabled()`.
* **locks** — `IOSimpleLock` spinlocks with
  `IOSimpleLockLockDisableInterrupt` for the `_irq` variants; `IOLock`
  mutexes with an owner field; `IOLockSleep[Deadline]`/`IOLockWakeup`.
* **threads** — `kernel_thread_start` with a trampoline, a spinlock-protected
  `thread_t → wrapper` hash (XNU has no TLS for kexts), lost-wakeup-safe
  `thread_wait/wake` via a pending flag, refcounted release, `thread_terminate`
  on exit, lazily created wrappers for foreign (IOKit) threads so the
  per-thread slot works for `current` everywhere.
* **timers** — one shared timer `IOWorkLoop`; each timer is an
  `IOTimerEventSource` armed with `wakeAtTime()` on the uptime clock;
  `cancel` reports whether it was pending, `cancel_sync` additionally waits
  for a running callback (unless called from the timer thread, where it
  would deadlock), `destroy` removes the event source.  The 10 ms jiffies
  tick (`lkpi_jiffies_tick()`) is a self-rearming timer on the same loop,
  catching up after late wakeups.
* **MMIO** — volatile accesses with `OSSynchronizeIO` (`dsb sy` on arm64).
* **PCI** — config space via `configRead/Write*`, BARs via
  `mapDeviceMemoryWithRegister(..., kIOMapInhibitCache)`, interrupt through
  an `IOInterruptEventSource` on the per-device workloop (first
  `kIOInterruptTypePCIMessaged` source when MSI is requested, legacy index
  otherwise); `irq_sync` runs a no-op `IOWorkLoop::runAction` to wait for a
  handler in flight; `setBusMasterEnable`.
* **DMA** — coherent buffers from
  `IOBufferMemoryDescriptor::inTaskWithPhysicalMask` + `IODMACommand`
  (`OutputHost64`, address bits derived from the mask, system mapper when
  present); streaming maps from `IOMemoryDescriptor::withAddressRange` +
  `IODMACommand`; both kept in a registry keyed by bus address;
  `dma_sync` calls `IODMACommand::synchronize()` unconditionally.
* **firmware** — embedded table first, then `OSKextRequestResource` with the
  full relative name and, if that fails, the basename.
* **random** — `read_random`.  **cpu** — count 1, id 0 (hints only).

## Known approximations (IOKit side)

* `IOMalloc` may sleep; there is no atomic allocator in XNU.  Small
  allocations come from zones and do not block in practice, so GFP_ATOMIC
  callers are served, but it is not a guarantee.
* `lkpi_os_mutex_is_locked()` reads an owner field written after the lock is
  taken; it is a debugging hint like Linux's, not a synchronisation point.
* `lkpi_os_thread_yield()` sleeps 1 ms (`thread_block()` is behind
  `LKPI_OS_HAVE_THREAD_BLOCK`).
* `lkpi_os_thread_set_realtime()` is a no-op unless
  `LKPI_OS_HAVE_THREAD_POLICY=1` (then `thread_policy_set` with
  `THREAD_PRECEDENCE_POLICY`).
* `lkpi_os_cpu_count()` returns 1 and `lkpi_os_cpu_id()` 0 unless
  `LKPI_OS_HAVE_CPU_NUMBER=1`.
* Foreign-thread wrappers (threads not created by `lkpi_os_thread_create`)
  are never freed until `lkpi_os_stop()`.
* Streaming DMA requires a buffer to map to **one** bus segment.  Without an
  IOMMU a `kmalloc` buffer that straddles discontiguous physical pages
  cannot be mapped and `lkpi_os_dma_map()` returns 0; page-pool pages are
  physically contiguous by construction so RX is unaffected.
* Coherent memory is cached on x86 (like Linux) and `kIOMapInhibitCache` on
  arm64 (`LKPI_OS_COHERENT_INHIBIT_CACHE`).
* The interrupt handler runs on the workloop thread (secondary interrupt),
  not as a filter: Linux hardirq semantics (no sleeping, spinlocks ok) hold,
  latency is a few microseconds higher than a filter handler.
* Power management is two states (off/on) forwarding to
  `lkpi_pci_suspend/resume`; a complete sleep path (D3hot, PCI state
  save/restore, wake-on-WLAN) is future work and the C build has
  `CONFIG_PM` off for now.
* RCU, work queues, tasklets and NAPI are entirely in the C layer; the
  backend only supplies threads, locks and timers for them.

## Compile-time knobs

Set with `make CXXFLAGS_EXTRA=...` or by editing `lkpi_os_iokit.hpp`:
`LKPI_OS_HAVE_THREAD_POLICY`, `LKPI_OS_HAVE_CPU_NUMBER`,
`LKPI_OS_HAVE_THREAD_BLOCK`, `LKPI_OS_HAVE_MACH_CONTINUOUS_TIME`,
`LKPI_OS_COHERENT_INHIBIT_CACHE`, `LKPI_OS_PAGES_PHYS_MASK`.

Boot-args: `mt7925_debug=1` (enable debug log level),
`mt7925_<param>=<value>` overrides a `Parameters` entry (e.g.
`mt7925_disable_aspm=1`).
