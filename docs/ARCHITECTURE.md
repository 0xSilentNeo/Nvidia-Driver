# Architecture

This port keeps the Linux `mt76` driver for the MediaTek MT7925 (and the
MT7927/MT7928 siblings it covers) **unmodified** and gives it the kernel it
expects: a C compatibility layer that implements the Linux kernel API on top
of IOKit, exactly the strategy FreeBSD uses to run the same driver through its
LinuxKPI.  Nothing in this repository is GPL: the driver is
BSD-3-Clause-Clear, the 802.11 API headers are FreeBSD's BSD-2-Clause
re-implementations, and everything written here is MIT.

```
 ┌──────────────────────────────────────────────────────────────────────────┐
 │ macOS user space: System Settings, menu bar, airportd / wifid, sharingd  │
 ├──────────────────────────────────────────────────────────────────────────┤
 │ IO80211Family glue (phase 4)                 src/kext/io80211/           │
 │   IO80211Controller/IO80211Interface subclass so the interface is a      │
 │   native "AirPort" device.  Private, reverse-engineered interface.       │
 ├──────────────────────────────────────────────────────────────────────────┤
 │ 802.11 MLME stack (phase 3)                  src/net80211/, src/lkpi80211│
 │   FreeBSD net80211 (BSD-2) on an mbuf/ifnet shim + FreeBSD's             │
 │   mac80211 → net80211 translation (linux_80211.c, vendored for reference)│
 ├──────────────────────────────────────────────────────────────────────────┤
 │ LinuxKPI: Linux kernel API on IOKit         src/linuxkpi/                │
 │   include/linux, include/net   C headers (ours + FreeBSD 802.11 headers) │
 │   src/*.c                      C runtime: locks, work, timers, kthreads, │
 │                                tasklets, RCU, skbuff, page pool, NAPI,   │
 │                                idr, pci bus, dma, devres, firmware,      │
 │                                printk, debugfs stubs                     │
 │   include/lkpi/os.h            the ONLY boundary to the OS (C ABI)       │
 ├──────────────────────────────────────────────────────────────────────────┤
 │ mt76 driver, vendored verbatim               src/mt76/                   │
 │   core (dma, tx, mcu, mac80211 glue), connac lib, mt792x lib, mt7925    │
 ├──────────────────────────────────────────────────────────────────────────┤
 │ IOKit kext (C++)                             src/kext/                   │
 │   MT7925PCIe : IOService on IOPCIDevice — BAR0 MMIO, MSI, workloops,     │
 │   IOBufferMemoryDescriptor/IODMACommand, OSKextRequestResource firmware, │
 │   implements lkpi/os.h                                                   │
 └──────────────────────────────────────────────────────────────────────────┘
```

## Layer contracts

### `lkpi/os.h` — the OS boundary

Everything under `src/linuxkpi/include/{linux,net,asm}` is plain C and may
only touch the host kernel through the functions declared in
`src/linuxkpi/include/lkpi/os.h`: allocation, logging, clocks, spin and sleep
locks, threads with a lost-wakeup-free wait/wake pair, one-shot timers, MMIO,
PCI config/BAR/interrupt access, DMA, firmware loading and randomness.

* `src/kext/lkpi_os_iokit.cpp` implements it for the kernel.
* `tests/host/os_host.c` implements it with pthreads/libc so the whole C layer
  is unit-testable on a Linux or macOS development machine.

The C++ side never includes a Linux header, and the C side never sees an
IOKit type.  That keeps the two halves independently compilable and reviewable.

### Vendored code and patches

* `scripts/import-mt76.sh` copies the needed files from the pinned upstream
  commit recorded in `src/mt76/UPSTREAM`.  The copy is verbatim except for the
  hunks in `patches/0001-*.patch` (two include blocks under `LKPI_MACOS`, at
  the same spots FreeBSD patches).
* `scripts/import-freebsd.sh` copies the 802.11 API headers from the pinned
  FreeBSD commit in `src/linuxkpi/include/FREEBSD_UPSTREAM`.  Our adaptations
  are the `#if defined(LKPI_MACOS)` hunks in `patches/0002-*.patch`:
  embedded `struct device` in `struct wiphy` (Linux layout), the current
  action-frame layout, EML capability masks, NAN enumerators, and
  `lkpi/mac80211_compat.h` for API that FreeBSD does not have yet.
* `scripts/regen-patches.sh` rebuilds the patch series from the current tree,
  so re-importing a newer upstream is: import, apply-patches, fix conflicts,
  regen-patches, `make check-headers`.

`mt7925/nan.c` (Wi-Fi Aware) is not built; `src/linuxkpi/src/mt7925_nan_stubs.c`
satisfies its callers.  macOS has no NAN consumer and the mac80211 NAN API is
still changing upstream.

### Verification without a Mac

`make check-headers` runs `clang -fsyntax-only` over every vendored driver
file and every C file of the compatibility layer with the real flags
(`-std=gnu11`, Kconfig defines, `-Werror` on implicit declarations and pointer
type mismatches).  It needs no SDK and is the gate for any header change: all
28 driver files and the LinuxKPI sources compile with zero warnings.

`make link-check` goes one step further: it compiles the driver and the C
layer to host objects and prints every symbol nothing defines.  After the
phase-1 stub (`src/linuxkpi/src/mac80211_stub.c`) that list is empty apart
from libkern functions, which is the proof that the kext can link.

`scripts/api-inventory.py` regenerates `docs/LINUX_API_INVENTORY.md`, the
list of every external symbol the driver uses and whether the layer provides
it.

`make host-tests` builds the C layer against the libc backend and runs unit
tests for lists, bit operations, bitfields, idr, skbuff, workqueues, timers,
completions and kthreads.  `tests/host/test_probe` goes further and links the
real driver: with a 60-line register model (ownership handshake, WFSYS reset,
chip ID) the unmodified `mt7925_pci_probe()` runs through PCI setup, firmware
feature parsing, DMA ring allocation and MCU initialisation on a developer
machine, fails cleanly when no firmware answers, and detaches.  Set
`LKPI_FIRMWARE_DIR` to a directory holding the MediaTek blobs
(`scripts/fetch-firmware.sh`) to exercise the firmware parser.

## Design decisions worth knowing

| Topic | Decision | Why |
|---|---|---|
| Interrupt context | The Linux "hardirq" handler runs on an IOKit interrupt workloop thread; the `irq_tasklet` runs on a dedicated real-time thread; NAPI polls on a per-device thread. | Preserves the three-level split mt76's locking assumes (`spin_lock_bh`, `spin_lock_irqsave`) without primary-interrupt-context code. |
| `jiffies` | Real variable, `HZ = 100`, advanced by a 10 ms periodic timer. | mt76 also uses `jiffies` as a struct member name, so it cannot be a macro; 100 Hz matches FreeBSD's `ticks`. |
| Spinlocks / mutexes | `IOSimpleLock` / `IOLock`, allocated lazily on first use (compare-and-swap). | Linux has no mandatory destructor and embeds locks in many structs; lazy allocation keeps `DEFINE_SPINLOCK` and zeroed structs working. |
| RCU | `rcu_read_lock()` is a compiler barrier; `synchronize_rcu()` forces every LinuxKPI-managed thread through a quiescent point; `kfree_rcu` is deferred. | XNU has no RCU or public epoch mechanism. This is the same approximation FreeBSD shipped before ck_epoch; documented as a known risk for hot-unplug paths. |
| `sk_buff` | Own implementation, Linux layout semantics, 64-byte `cb` (FreeBSD's `ieee80211_tx_info` needs more than Linux's 48). | mt76 reaches into `skb->data/len/cb/priority/next` and uses `skb_shinfo()->frag_list`; the vendored 802.11 headers overlay their status structs on `cb`. |
| Page pool | Pages are kernel allocations with a descriptor found through a hash of the aligned address. DMA mapping goes through `IODMACommand`. | mt76 RX uses `page_pool_dev_alloc_frag` and recycles through `page->pp`. |
| debugfs / hwmon / leds / thermal / nvmem / of / acpi | Compile-time stubs that fail cleanly. | None exist on macOS; the driver treats their absence as optional. ACPI SAR tables may later be read through `IOACPIPlatformDevice`. |
| USB / SDIO / WED / NPU | Type-only stubs. | PCIe only. |
| Errno | Linux values are used throughout the C layer; translated to `IOReturn` at the kext boundary. | The driver compares against them symbolically; XNU's values differ. |

## Phase plan

See `docs/ROADMAP.md`.  Phases 0–1 (this tree) give a kext that enumerates
the device, maps it, downloads firmware and brings the MCU up through the
unmodified driver.  Phase 2 is DMA/TX/RX against a frame sink; phase 3 the
802.11 stack; phase 4 the native IO80211 integration; phase 5 power
management and Wi-Fi 7 features.
