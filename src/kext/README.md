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

Build with Apple clang (`xcrun clang`, the default).  Upstream LLVM 15 and
later lowers Mach-O global destructors (the `gMetaClass` object every
`OSDefineMetaClassAndStructors` creates) to `__cxa_atexit()`, which no KPI
exports; XNU's OSRuntime runs a kext's `__mod_term_func` instead.  A
non-Apple clang needs `-mllvm -disable-atexit-based-global-dtor-lowering`
(`CXXFLAGS_EXTRA=`).

### Checking without a Mac

`scripts/xnu-kernel-headers.sh` assembles a stand-in macOS SDK from Apple's
open-source [xnu](https://github.com/apple-oss-distributions/xnu) and
[IOPCIFamily](https://github.com/apple-oss-distributions/IOPCIFamily), and
`check-xnu` compiles every source of the kext against it with the real
build's flags (`-target <arch>-apple-darwin` replaces `-arch`):

```sh
scripts/xnu-kernel-headers.sh /src/xnu /src/IOPCIFamily /tmp/xnu-sdk
make -C src/kext check-xnu XNU_HDRS=/tmp/xnu-sdk [XNU_ARCHS="x86_64 arm64e"]
make -C src/kext check-xnu-symbols XNU_HDRS=/tmp/xnu-sdk [STACK=real]
# or let the Makefile assemble it into build/kext/xnu-sdk:
make -C src/kext check-xnu XNU_SRC=/src/xnu IOPCIFAMILY_SRC=/src/IOPCIFamily
```

The tree follows Apple's own `installhdrs` rules: the
`INSTALL_KF_MI_LIST`/`INSTALL_KF_MD_LIST` of every directory xnu's Makefiles
reach through `INSTINC_SUBDIRS[_ARCH]`, installed under
`EXPORT_MI_DIR`/`EXPORT_MD_DIR` (which is what merges `bsd/sys` and osfmk
pieces into `sys/`, `bsd/machine` and `osfmk/machine` into `machine/`,
`EXTERNAL_HEADERS` and `osfmk/libsa` into the top level) and filtered with
`unifdef $(KINCFRAME_UNIFDEF)` (`-UKERNEL_PRIVATE -UPRIVATE -DKERNEL
-UXNU_KERNEL_PRIVATE ...`, read from `makedefs/MakeInc.def`; the real
`unifdef` when installed, else a built-in equivalent).  So
`KERNEL_PRIVATE`-only declarations are absent exactly as in the SDK, and the
`-DKERNEL_PRIVATE` in our flags cannot bring them back.  The tree's
`MANIFEST` lists every header and its source.  What it cannot reproduce, and
marks as HAND-WRITTEN STAND-IN:

* MIG output (`mach/thread_act.h`, `mach/task.h`, ...): `mach/thread_act.h`
  declares the routines the kext uses, copied from `thread_act.defs`; the
  others only carry MIG's includes.  Other build-generated headers
  (`libkern/version.h`, `sys/syscall.h`) are `#error` placeholders.
* iig output (`DriverKit/OSObject.h`, `DriverKit/IOService.h`, ...; iig is
  not open source): the `.iig` with its class bodies removed and the
  `<Class>_Methods` macros empty, so class layouts are not the SDK's.
* `usr/include/TargetConditionals.h` and `os/availability.h` (not in xnu).
  Only the user-space headers Kernel.framework itself includes are present;
  a kext that includes anything else from `usr/include` fails here, as it
  should (on a Mac it would silently get the user-space header).

`check-xnu-symbols` compiles the objects and resolves every external symbol
against xnu's `config/*.exports` symbol sets for the `OSBundleLibraries` in
`Info.plist`, IOPCIFamily's `config/IOPCIFamily.exports` and libkmod
(`scripts/xnu-kext-symbols.sh`).  It is a name-level link check: there is
no Apple linker, no code signing and no loading here.

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

## Kernel KPIs, checked against the headers

What xnu-12377.1.9 and IOPCIFamily-726.0.5 say about the interfaces this port
was unsure of (headers as installed in Kernel.framework/Headers, exports
from xnu `config/<Set>[.<arch>[.MacOSX]].exports`):

| KPI | declaration | exported by |
|---|---|---|
| `thread_terminate` | `kern_return_t thread_terminate(thread_act_t)`, MIG-generated `<mach/thread_act.h>` (`thread_act.defs`) | `com.apple.kpi.mach` |
| `THREAD_UNINT` | `#define THREAD_UNINT 0` in `<kern/kern_types.h>` (with `THREAD_TIMED_OUT`, `THREAD_CONTINUE_NULL`); `IOLockSleep[Deadline]()` take it as `UInt32 interType` | macro |
| `mach_continuous_time` | `uint64_t mach_continuous_time(void)` in `<mach/mach_time.h>` | `com.apple.kpi.mach` |
| `kernel_task` | `extern task_t kernel_task` in `<kern/task.h>` | `com.apple.kpi.mach` |
| `PE_parse_boot_argn` | `boolean_t PE_parse_boot_argn(const char *, void *, int max_arg)` in `<pexpert/pexpert.h>` | `com.apple.kpi.iokit` |
| `OSKextGetCurrentIdentifier` | `const char *OSKextGetCurrentIdentifier(void)` in `<libkern/OSKextLib.h>` ("visible to kexts only") | not a kernel export: defined `__private_extern__` in libkmod.a (`libkern/kmod/c_start.c`), returns `kmod_info.name`; linked by `-lkmod` |
| IOPCIDevice config/map | `UIntNN configReadNN(IOByteCount)` / `configWriteNN(IOByteCount, v)` are inlines over `extendedConfigReadNN/WriteNN` (the `UInt8 offset` virtuals are `__IOPCIDEVICE_INTERNAL__`); `IOMemoryMap *mapDeviceMemoryWithRegister(UInt8 reg, IOOptionBits = 0)`; `UInt8 getBusNumber()/getDeviceNumber()/getFunctionNumber()`; `setBusMasterEnable()` is deprecated from macOS 12.4 (`setBusLeadEnable()` replaces it); the class is `__kpi_deprecated("Use PCIDriverKit")` | `com.apple.iokit.IOPCIFamily` |
| `wakeAtTime(AbsoluteTime)` | `virtual IOReturn wakeAtTime(AbsoluteTime)`, also `(UInt32, UInt32 scale = kNanosecondScale)`, `(uint32_t options, AbsoluteTime, AbsoluteTime leeway)`; `AbsoluteTime` is `UInt64` in LP64 kernel code; `AbsoluteTime_to_scalar()` is `KERNEL_PRIVATE` (not usable) | `com.apple.kpi.iokit` |
| `IODMACommand::withSpecification` | `(SegmentFunction, UInt8 addressBits, UInt64 maxSegment, MappingOptions = kMapped, UInt64 maxTransfer = 0, UInt32 alignment = 1, IOMapper * = NULL, void *refCon = NULL)` and a `SegmentOptions *` overload; returns `OSPtr<IODMACommand>` (a raw pointer without smart pointers) | `com.apple.kpi.iokit` |
| `thread_policy_set` | `kern_return_t thread_policy_set(thread_act_t, thread_policy_flavor_t, thread_policy_t, mach_msg_type_number_t)` in MIG `<mach/thread_act.h>`; `THREAD_PRECEDENCE_POLICY` in `<mach/thread_policy.h>` | `com.apple.kpi.mach` |
| `cpu_number` | no public prototype (`kern/cpu_number.h` is `XNU_KERNEL_PRIVATE`) | `com.apple.kpi.unsupported` on x86_64; only `com.apple.kpi.private` on arm64, so `LKPI_OS_HAVE_CPU_NUMBER` is an `#error` there |
| `thread_block` | `wait_result_t thread_block(thread_continue_t)` in `<kern/sched_prim.h>` | `com.apple.kpi.mach` |
| kmod info / `_realmain` | `KMOD_EXPLICIT_DECL(name, version, start, stop)` in `<mach/kmod.h>` defines `kmod_info`; libkmod's `_start`/`_stop` call through `extern kmod_start_func_t *_realmain` / `kmod_stop_func_t *_antimain`, which `MT7925_info.c` defines (NULL); `_kext_apple_cc` is not referenced | libkmod.a |

Other Kernel.framework facts the check turned up: `strrchr()` is declared
only for `XNU_KERNEL_PRIVATE`; `strcpy()`/`strncpy()`/`strcat()`/`strncat()`
are deprecated for kexts on x86_64 and `__unavailable` on arm64 (and
`strcpy` is a three-argument macro under `__XNU_FORTIFY_SOURCE`); there is
no `<stdio.h>`; `<sys/cdefs.h>`, `<ptrcheck.h>` and `<sys/_endian.h>` define
`__unused`, `__counted_by`, `htonl` and friends that the Linux and FreeBSD
headers also define.  With the default knobs `com.apple.kpi.bsd` and
`com.apple.kpi.unsupported` resolve no symbol of the kext.

## Compile-time knobs

Set with `make CXXFLAGS_EXTRA=...` or by editing `lkpi_os_iokit.hpp`:
`LKPI_OS_HAVE_THREAD_POLICY`, `LKPI_OS_HAVE_CPU_NUMBER`,
`LKPI_OS_HAVE_THREAD_BLOCK`, `LKPI_OS_HAVE_MACH_CONTINUOUS_TIME`,
`LKPI_OS_COHERENT_INHIBIT_CACHE`, `LKPI_OS_PAGES_PHYS_MASK`.  Per the table
above, `thread_policy_set()` and `thread_block()` are declared and exported
for kexts; `cpu_number()` only on x86_64.

Boot-args: `mt7925_debug=1` (enable debug log level),
`mt7925_<param>=<value>` overrides a `Parameters` entry (e.g.
`mt7925_disable_aspm=1`).
