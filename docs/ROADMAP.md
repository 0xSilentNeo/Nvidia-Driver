# Roadmap

Milestones are ordered so that each one is observable on real hardware
through `log show --predicate 'sender == "MT7925"'`.  A box is checked only
when the behaviour was seen on a machine, not when the code was written.

## Phase 0 — Foundation (this repository state)

- [x] Vendor the Linux mt76/mt7925 PCIe driver verbatim, pinned to an upstream commit, with a patch series for the two include hunks.
- [x] Vendor FreeBSD's BSD-licensed mac80211/cfg80211/ieee80211 API headers, pinned, with a patch series for the Linux-layout adaptations.
- [x] Write the LinuxKPI headers (120+ files) so that **all 28 driver files compile front-end clean** (`make check-headers`, zero warnings).
- [x] Generate the Linux API inventory (`docs/LINUX_API_INVENTORY.md`).
- [x] Define the OS boundary (`lkpi/os.h`) and the runtime init sequence (`lkpi/runtime.h`).
- [x] C runtime implementations: locks, jiffies, timers, workqueues, kthreads, tasklets, RCU, skbuff, page pool, NAPI, idr, PCI bus layer, DMA, devres, firmware, printk, debugfs stubs.
- [x] IOKit kext shell written: `MT7925PCIe` IOService, `lkpi/os.h` backend, Info.plist, Makefile (not yet compiled: needs a Mac).
- [x] Host unit tests against the libc backend (`make host-tests`): 371 checks.
- [x] Host integration test `tests/host/test_probe`: the **real `mt7925_pci_probe()`** runs against a modelled fake device through the whole C stack (PCI setup, firmware blob parsing, DMA rings, MCU commands with timeouts, clean remove).

## Phase 1 — Device bring-up

Goal: `mt7925_pci_probe()` runs to completion inside the kext.

- [ ] Kext builds with Xcode command line tools on an Intel Mac / Hackintosh (macOS 11–15).
- [ ] Kext loads, matches `14c3:7925` (and 0717/7927/6639/0738/7928/7935), maps BAR0, logs `ASIC revision: 7925xxxx`.
- [ ] MSI interrupt fires and reaches `mt792x_irq_handler` → `mt792x_irq_tasklet`.
- [ ] DMA rings allocated (`mt7925_dma_init`), coherent and streaming mappings verified with VT-d enabled and disabled.
- [ ] Firmware loaded from the bundle: ROM patch + RAM code, `mt7925_run_firmware` succeeds, firmware version logged.
- [ ] MCU request/response round-trip (`mt76_mcu_send_msg` with wait) works; `mt7925_register_device` reaches `mt76_register_device`.
- [ ] The mac80211 registration call lands in a **recording stub** (`src/lkpi80211/` phase-3 skeleton) that logs the advertised bands, channels and capabilities instead of registering a radio.
- [ ] Clean unload: `mt7925_pci_remove`, workqueues drained, no leaked DMA mappings (counters logged on stop).

## Phase 2 — Data path

- [ ] Monitor-mode style RX: frames from the RX ring reach a frame sink; radiotap headers correct.
- [ ] TX of raw management frames through `mt76_tx` with completion events and token recycling.
- [ ] Stress: 24 h RX/TX loop with no leaked skbs/pages (debug counters).
- [ ] Reset/recovery path (`mt792x_mac_reset_work`) exercised by injecting an MCU timeout.

## Phase 3 — 802.11 stack

Design: `docs/PHASE3_NET80211.md`.

- [x] Vendor FreeBSD net80211 (`src/net80211/upstream`, pinned) and the BSD support files (`sys/queue.h`, `if_media.h`, rijndael AES).
- [ ] `bsdkpi`: FreeBSD kernel API on LinuxKPI; the station-mode net80211 set compiles front-end clean (`make check-net80211`). *(in progress)*

- [ ] Adapt FreeBSD's `linux_80211.c` (mac80211 → net80211 glue, already vendored in `src/lkpi80211/`) to that stack.
- [ ] Scan: results visible through a debug IOUserClient tool.
- [ ] Association to an open network, then WPA2-PSK and WPA3-SAE (SAE is done in the stack, 4-way handshake in kernel like FreeBSD).
- [ ] Data traffic through a plain `IOEthernetInterface` as an interim (shows up as Ethernet; DHCP works).
- [ ] Hardware offloads mt7925 expects: hardware scan/sched-scan, roaming events, key install, A-MPDU/A-MSDU, power save.

## Phase 4 — Native macOS integration

Design: `docs/PHASE4_IO80211.md`.

- [ ] `IO80211Controller`/`IO80211Interface` subclass (reverse-engineered private interface, as AirportItlwm does) so the device is an AirPort interface: menu bar, System Settings Wi-Fi pane, `wdutil`, Location Services.
- [ ] Apple's `airportd` drives scan/associate through `APPLE80211_IOC_*`; map to net80211.
- [ ] Country code and regulatory from the firmware CLC tables mapped to Apple's expectations.
- [ ] Continuity that does not need AWDL: Handoff, Universal Clipboard, Instant Hotspot, iPhone cellular calls (BT discovery + infrastructure Wi-Fi). See `docs/AIRDROP.md`.
- [ ] Sleep/wake with reconnection (`IOService::setPowerState` → `mt7925_pci_suspend/resume`).

## Phase 5 — Features and polish

- [ ] Wi-Fi 6E / 6 GHz and Wi-Fi 7 (320 MHz, MLO) as the macOS side allows.
- [ ] Bluetooth coexistence notes (the MT7925's BT half is a separate USB function handled by a separate driver).
- [ ] ACPI SAR tables through `IOACPIPlatformDevice`.
- [ ] Signed/notarized release builds, OpenCore injection recipe, installer.

## Research track (no promise)

- [ ] AWDL (Apple Wireless Direct Link) for AirDrop/Sidecar/AirPlay-to-Mac. Requires implementing Apple's proprietary synchronized channel-hopping protocol and the `awdl0` interface semantics `sharingd` expects. No third-party driver has achieved this; see `docs/AIRDROP.md` for what is known.
