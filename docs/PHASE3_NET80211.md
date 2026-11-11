# Phase 3 — the 802.11 stack

mt76 is a mac80211 driver: it expects the kernel to own the MLME (scanning,
authentication, association, key handshakes, power save, BA sessions) and to
hand it fully formed 802.11 frames.  macOS offers nothing equivalent to a
third-party driver, so the port carries its own stack.  The choice, as for
everything else here, is to reuse what already runs this exact driver:

* **FreeBSD net80211** (BSD-2/3-Clause): the MLME stack, vendored verbatim
  into `src/net80211/upstream/` and pinned to the same FreeBSD commit as the
  802.11 API headers.
* **FreeBSD `linux_80211.c`** (BSD-2-Clause, in `src/lkpi80211/`): the
  translation from the `ieee80211_*` calls a mac80211 driver makes to
  net80211, and from net80211's state machine back to the driver's
  `struct ieee80211_ops`.  About 10k lines; built with six
  `#if defined(LKPI_MACOS)` hunks (`patches/0004-*`), see "Milestone 2 as
  built" below.

**Status.** Milestones 1 to 3 are done on the host: the whole stack (driver,
LinuxKPI, bsdkpi, net80211, glue) compiles warning-free, links with nothing
undefined (`make link-check-full`) and runs in `tests/host/test_probe`:
`ieee80211_register_hw()` attaches an `ieee80211com`, a station vap is
created through the "wlan" cloner, brought up, and a scan goes down to the
(modelled) firmware and comes back as a scan-cache entry.  The kext itself
still builds the phase-1 stub (`src/kext/Makefile`); nothing here has run on
hardware.

Both sit on FreeBSD kernel primitives.  Phase 3 therefore adds one more
compatibility layer, **bsdkpi**, implementing those primitives on the
LinuxKPI we already have, so that the whole stack is one C program with a
single OS boundary (`lkpi/os.h`) and stays testable on the host.

```
 mt76 driver ──(net/mac80211.h)──► linux_80211.c ──(net80211/*.h)──► net80211
      │                                │                                  │
      └── LinuxKPI ◄────── bsdkpi (sys/mbuf.h, sys/mutex.h, callout, …) ──┘
                 └─────────────── lkpi/os.h ──────────────────────────────┘
```

## bsdkpi — FreeBSD kernel API on LinuxKPI (`src/bsdkpi/`)

Headers live under `src/bsdkpi/include/{sys,net,netinet,machine,crypto}` and
are included by the vendored FreeBSD sources unchanged (the include paths put
`src/bsdkpi/include` before `src/linuxkpi/include`).  The mapping:

| FreeBSD | Implementation |
|---|---|
| `struct mtx` (`mtx_init/lock/unlock/assert`, `MTX_DEF` / `MTX_SPIN`) | `MTX_SPIN` → LinuxKPI `spinlock_t`; `MTX_DEF` → `struct mutex` (sleepable; net80211's `IEEE80211_LOCK` family is `MTX_DEF`). |
| `struct sx`, `struct rwlock`, `struct rmlock` | `struct mutex` (writers and readers serialise; correct, not concurrent). |
| `struct cv` | `wait_queue_head_t` + predicate. |
| `struct callout` (`callout_init_mtx`, `callout_reset`, `callout_stop/drain`) | `struct timer_list`; the mtx variant takes the mutex around the callback on the timer thread, which is why `MTX_DEF` callouts run from a workqueue, not the timer thread itself. |
| `struct task`/`taskqueue` (`TASK_INIT`, `taskqueue_enqueue`, `taskqueue_drain`, `taskqueue_thread`) | `struct work_struct` on a `workqueue_struct`; `taskqueue_create_fast` → `alloc_ordered_workqueue`. |
| `NET_EPOCH_ENTER/EXIT`, `epoch_wait_preempt` | `rcu_read_lock/unlock`, `synchronize_rcu` (same approximation as the rest of the port). |
| `malloc(9)`/`free(9)`, `MALLOC_DEFINE` | `kmalloc`/`kfree`; `M_WAITOK` → `GFP_KERNEL`, `M_NOWAIT` → `GFP_ATOMIC`, `M_ZERO` → `__GFP_ZERO`. |
| `ticks`, `hz`, `time_uptime` | `jiffies`, `HZ` (both 100 Hz), uptime seconds. |
| `atomic_*` (machine/atomic.h), `refcount(9)`, `counter(9)` | compiler atomics / `atomic_t`; counters are plain atomics. |
| `printf`, `log`, `KASSERT`, `panic` | FreeBSD `kvprintf` (`%D`, `%b`) feeding `printk`; `lkpi_os_panic`. |
| `sysctl(9)`, `SYSCTL_*`, `TUNABLE_*` | a real OID tree with handlers (no `sysctl(2)`; reachable through `bsdkpi_sysctl_find/invoke` for a debug user client); tunables keep their defaults. |
| `module(9)`, `MODULE_VERSION/DEPEND`, `SYSINIT`, `DECLARE_MODULE`, `TEXT_SET` | constructor-recorded lists (no linker sets on Mach-O); `net80211_register_builtin_modules()`, called from `bsdkpi_init()`, runs the SYSINITs and MOD_LOAD handlers that populate net80211's module-registration tables (crypto ciphers, scanners, rate control, auth). |
| `struct mbuf` and `m_*`/`mbufq_*`/`m_tag_*` | **own implementation** with FreeBSD's field names (`m_next`, `m_nextpkt`, `m_data`, `m_len`, `m_flags`, `m_pkthdr.len/rcvif/flowid`) and semantics; every mbuf owns a 2 KiB (or jumbo) buffer, no clusters/refcounted externals.  Kept separate from `sk_buff`; the glue copies between them at the two crossing points (`linux_80211.c` already does `mbuf ↔ skb` conversion). XNU `mbuf_t` is used only at the IO80211 boundary in phase 4, where the frame is copied once into the network stack's mbuf. |
| `struct ifnet` + `if_*()` accessors, `ether_ifattach`, `if_input`, `if_transmit`, `if_link_state_change`, `if_media` | **own minimal implementation**: an interface object with the accessor API net80211 uses (`if_private.h` style), `if_input` and `if_transmit` function pointers that phase 4 binds to the IO80211 interface, and the `ifmedia` list used for mode/rate reporting. |
| `bpf(9)`, `route(9)` (`rt_ieee80211msg`), `vnet`, `netisr`, `debugnet` | no-ops (`bpf_peers_present()` is 0; `rt_ieee80211msg` becomes a notification hook the IO80211 layer subscribes to for Apple's state-change events). |
| `sockio.h` ioctls (`SIOCS80211`/`SIOCG80211`, `ieee80211req`) | kept: `ieee80211_ioctl.c` is the control-plane API; phase 4 drives it from `IO80211Controller` requests, and a debug `IOUserClient` exposes it for host-style tools. |
| `crypto/rijndael` | vendored from FreeBSD (public-domain reference AES) for CCMP/GCMP. |

Everything in bsdkpi is MIT and written here except `sys/queue.h`,
`net/ethernet.h`, `net/if_llc.h`, `net/if_media.h` and the rijndael files,
which are vendored (BSD / public domain).

## Milestone 1 as built (`src/bsdkpi`, `make check-net80211`)

Layout:

* `src/net80211/upstream/` is FreeBSD's `sys/net80211` verbatim (plus the
  `#if defined(LKPI_MACOS)` hunks in `patches/0003-net80211-lkpi-macos.patch`);
  `src/net80211/net80211` is a symlink to it so that `<net80211/...>`
  resolves there for net80211 itself and for the LinuxKPI 802.11 headers,
  which previously carried their own copies of `ieee80211.h`,
  `_ieee80211.h` and `ieee80211_crypto.h`.  `src/net80211/net80211_stubs.c`
  provides the attach/detach entry points of the deferred hostap/adhoc/wds
  modules that `ieee80211_proto.c` calls unconditionally (those opmodes then
  fail vap creation with "mode not supported").
* `src/bsdkpi/include/{sys,net,netinet,machine,security}` plus
  `opt_wlan.h`/`opt_inet.h`/`opt_inet6.h` (IEEE80211_DEBUG and
  IEEE80211_AMPDU_AGE on; INET on; INET6, SUPERG, MESH, TDMA off) and
  `bsdkpi/bsdkpi.h`, the shim's own API (`bsdkpi_init/fini`,
  `bsdkpi_ifnet_set_hooks`, `bsdkpi_if_clone_create`).
* `src/bsdkpi/src/`: `mbuf.c`, `callout.c`, `taskqueue.c`, `mutex.c`
  (mtx/sx/rw/rm/cv and sleep channels), `ifnet.c` (ifnet, ether_*,
  if_clone, route notifications), `ifmedia.c`, `sbuf.c`, `sysctl.c`,
  `modules.c` (SYSINIT, module(9), linker sets, eventhandler), `printf.c`
  (FreeBSD `kvprintf` with `%D`/`%b`, feeding `printk`), `misc.c`.
* Flags: `NET80211_CFLAGS` puts `src/bsdkpi/include` first, then
  `src/bsdkpi` (for `<crypto/rijndael/...>`), `src/net80211`, then the
  LinuxKPI flags, and defines `_KERNEL`.  `sys/cdefs.h` defines
  `__FreeBSD__`, which `ieee80211_var.h` uses to select
  `ieee80211_freebsd.h` (clang predefines `__linux__`/`__APPLE__`).

Rules the shim relies on:

* `sys/types.h` and `sys/cdefs.h` chain to the host's with `#include_next`
  and pull in every LinuxKPI header the shim uses *first*; FreeBSD macros
  that share a name with a LinuxKPI one (`ALIGN`, `roundup`, `LIST_HEAD`,
  `min`/`max` are kept as LinuxKPI's) are defined afterwards and win for the
  rest of the translation unit.  A LinuxKPI header included *after*
  `sys/param.h` that uses `ALIGN(x, a)` or Linux's `LIST_HEAD(name)` in a
  macro will not compile; `linux_80211.c` (milestone 2) must include its
  LinuxKPI headers through `sys/param.h`'s pre-include or before it.
* Linker sets, SYSINITs and `DECLARE_MODULE` are recorded by constructors
  (`__attribute__((constructor))`, which Mach-O kexts run from
  `__mod_init_func` at load) into static lists; nothing is allocated before
  `bsdkpi_init()`.  `net80211_register_builtin_modules()` (called from
  `bsdkpi_init()`) runs the SYSINITs in `(subsystem, order)` sequence, which
  delivers MOD_LOAD and fills net80211's cipher/scanner/ratectl/auth tables
  via `SET_FOREACH`.  If constructors ever turn out not to run in the kext,
  the fallback is an explicit list in `src/bsdkpi/src/modules.c`.
* Callout handlers run on a dedicated ordered work queue, never on the
  timer thread, because `callout_init_mtx` handlers take an `MTX_DEF`
  mutex (an IOLock).  `callout_stop()` under the callout's mutex has
  FreeBSD's guarantee; `callout_drain()` must not be called with it held.
* `wakeup(chan)`/`mtx_sleep`/`cv_*` share one sleep-channel mutex, since the
  backend's sleep/wakeup pair is tied to a mutex (`src/bsdkpi/src/mutex.c`).
* `ticks` is `(int)jiffies` (both 100 Hz); `hz` is a real `int`.
* mbuf: every mbuf is one allocation with at least a 2 KiB buffer, so
  `MLEN == MHLEN == MCLBYTES`; `M_WRITABLE()` is true unless `M_RDONLY`.
* `printf`/`log`/`vprintf` are macros onto `bsdkpi_printf` & co.;
  `__printflike` is clang's `format(freebsd_kprintf)` so `%6D` checks.
* `sysctl(9)` is real but has no `sysctl(2)`: `bsdkpi_sysctl_find()` and
  `bsdkpi_sysctl_invoke()` are for a debug user client.
* `bpf_attach()` returns NULL (no radiotap listeners); `rt_ieee80211msg`,
  `rt_ifmsg`, `if_link_state_change`, `ether_ifattach/detach` and
  `if_setlladdr` call the `bsdkpi_ifnet_hooks` the IO80211 layer installs.

Link-level proof (`make link-check-net80211`): the 29 net80211 files,
`net80211_stubs.c`, the 11 bsdkpi sources and the 3 rijndael files compile
to 44 host objects with no duplicate strong symbols; the 55 symbols they
leave undefined are all provided by `src/linuxkpi/src` (work queues,
timers, locks, RCU, `printk`, `jiffies`), `lkpi/os.h` or libkern.  Nothing
is left for the glue to *define*.  What the later layers must still *do*:

* `linux_80211.c` (milestone 2) supplies the driver side net80211 calls
  into through function pointers: `ic_vap_create/ic_vap_delete`,
  `ic_transmit`, `ic_raw_xmit`, `ic_parent`, `ic_scan_start/end`,
  `ic_set_channel`, `ic_newassoc`, `iv_key_alloc/set/delete`, ... and calls
  `ieee80211_ifattach()` with a filled-in `struct ieee80211com`.
* The runtime calls `bsdkpi_init()` (done in milestone 2: it is the
  "net80211" subsystem of `lkpi_runtime_init()`, started after NAPI and
  before the PCI bus and the driver's module init, stopped in reverse).
* The IO80211 layer (phase 4) creates the vap with
  `bsdkpi_if_clone_create("wlan", ...)`, sets `ifp->if_input` (frames up;
  the default counts and drops) and feeds Ethernet frames down through
  `ifp->if_transmit` (net80211 sets it to `ieee80211_vap_transmit`),
  installs `bsdkpi_ifnet_hooks` for link state and `RTM_IEEE80211_*`
  events, and drives `ifp->if_ioctl` with `SIOCS80211`/`SIOCG80211`.

## Milestone 2 as built (`src/lkpi80211`, `make check-lkpi80211`, `make link-check-full`)

`linux_80211.c` and `linux_80211_macops.c` are built; `lkpi80211_compat.c`
(ours) adds what the vendored `net/mac80211.h` declares but FreeBSD's glue
lacks: `ieee80211_txq_aql_pending()` (0: no airtime accounting), the three
NAN notifications (warn once; `mt7925/nan.c` is not built) and the
`compat.linuxkpi` sysctl node FreeBSD defines in `linux_compat.c`.
`linuxkpi_80211_pm.c` is **not** built: it is a newbus driver
(`device_t` methods, `DRIVER_MODULE`) for FreeBSD's suspend/resume child
devices; here power events will come from `IOService::setPowerState` and
call `lkpi_80211_suspend()/lkpi_80211_resume()` (in `linux_80211.c`)
directly.

**The header-order problem** (the caveat of milestone 1) is solved without
touching the vendored file by `src/lkpi80211/lkpi80211_prelude.h`,
force-included (`-include`) for this directory (`LKPI80211_CFLAGS`):

1. bsdkpi's `sys/types.h` first (it parses the core LinuxKPI headers before
   FreeBSD's `sys/queue.h`);
2. `#define LINUXKPI_NET80211` and every Linux header the glue uses
   (`linux/netdevice.h` with its two-argument `ALIGN`, `linux/pci.h`,
   `net/cfg80211.h`, `net/mac80211.h`, ...) while Linux's macros are still
   in force;
3. `sys/param.h` and the FreeBSD headers the glue relies on implicitly
   (`sys/sx.h`, `sys/refcount.h`, `sys/proc.h`, `sys/pcpu.h`, `sys/bus.h`);
4. the net80211 headers, with mac80211.h's function-like macro
   `ieee80211_send_bar` hidden (`#pragma push_macro`) while net80211's
   prototype of the same name is parsed (FreeBSD has the opposite include
   order and therefore no clash).

The glue's own `#include`s are then guarded no-ops.  The prelude also
defines the two FreeBSD-LinuxKPI names ours spells differently
(`linux_page_address`, the `%b` string `NETIF_F_BITS`).

**Shim changes** made so that the glue compiles unmodified:

| Where | What |
|---|---|
| `linux/skbuff.h`, `skbuff.c` | `struct sk_buff` gains FreeBSD's `m`/`m_free_func` (the mbuf a TX skb was built from, released when the skb is freed); `qmap` aliases `queue_mapping`; `skb_frag_t` members also answer to `page/size/offset`. |
| `linux/netdevice.h` | `addr_list` aliases the `list` linkage of `netdev_hw_addr(_list)`; `__hw_addr_init()`. |
| `linux/device.h` | `void *bsddev` (always NULL: no newbus); the glue tests it before `bus_*`. |
| `linux/list.h` | `LINUX_LIST_HEAD` spelled out (FreeBSD's `LIST_HEAD` is in force where it expands). |
| `net/ieee80211_radiotap.h` | now includes net80211's definitions (as FreeBSD's LinuxKPI does) instead of carrying a second, enum-based copy that collided with them. |
| bsdkpi `sys/bus.h` (new) | `device_t` and no-op `bus_*` for the PM-children calls. |
| bsdkpi `sys/mbuf.h` | `m_pkthdr.PH_loc` (the glue keeps a node reference in `PH_loc.ptr`). |
| bsdkpi `net/if.h` | `#undef` before `IFF_MULTICAST`/`IFF_RUNNING` (linux/if.h has other values; FreeBSD's win afterwards). |
| bsdkpi `bpf_attach()` | returns an opaque non-NULL token: `ieee80211_radiotap_vattach()` takes an interface reference that `vdetach` only drops when `iv_rawbpf != NULL`, so NULL leaked every vap's ifnet. |
| bsdkpi `modules.c` | module records are freed at `bsdkpi_fini()`. |

**`patches/0004-lkpi80211-lkpi-macos.patch`**: six hunks, all in
`linux_80211.c`, all `#if defined(LKPI_MACOS)`:

| Hunks | Why |
|---|---|
| 3: `linuxkpi_wiphy_new`, `linuxkpi_wiphy_free`, `linuxkpi_set_ieee80211_dev` | `struct wiphy` embeds its `struct device` (patches/0002): initialise it, run its devres on free, name it `<parent>-phy` (the glue names its work queue with `wiphy_name()`). |
| 1: `linuxkpi_ieee80211_ifattach` | `%b` takes an `int`; `netdev_features_t` is 64 bit here. |
| 1: `lkpi_lsta_free` | **upstream leak**: the 16 per-TID `lkpi_txq` of every station were never freed (`XXX-BZ free resources`). They are unlinked from the scheduling list, purged and freed. |
| 1: `linuxkpi_ieee80211_iffree` | **upstream leak**: the per-hw work queue (and its thread) was never destroyed. |

`patches/0003` gained one hunk (`ieee80211_ageq_remove()` read `head`
uninitialised when no frame matched; harmless, but valgrind flags it), and
the build set gained `ieee80211_ratectl_none.c` and `ieee80211_amrr.c`:
`lkpi_ic_vap_create()` calls `ieee80211_ratectl_init()`, which dereferences
a NULL `iv_rate` when neither rate-control module is registered (FreeBSD
kernels always have both).

**Code generation.** `-fwrapv -fno-strict-aliasing` are now part of
`LKPI_CFLAGS` (`KERNEL_CODEGEN`), as in both kernels this code comes from.
This is not cosmetic: net80211 computes `ieee80211_time_after(a, b)` as
`((int)(b) - (int)(a) < 0)` and schedules "scan forever" as
`ticks + INT_MAX`; without `-fwrapv` clang folds the comparison to
`(int)(b) < (int)(a)` and every scan ended before its first channel (found
by the host test: frames were dropped as "not to bss").

**Build integration.**

* `LKPI80211_SRCS`, `LKPI80211_CFLAGS` (= `NET80211_CFLAGS` + the prelude +
  `-Wno-format-security`, because the glue passes `wiphy_name()` as the
  format of `alloc_ordered_workqueue()`); `make check-lkpi80211`.
* `mac80211_stub.c` stays in the tree but out of `LKPI_SRCS`;
  `MAC80211_STUB=1` selects the old behaviour (stub in, `LKPI_NO_NET80211`
  so that `runtime.c` does not start bsdkpi).  `check-headers` still
  compiles the stub so it cannot rot.  The stub's `rfc1042_header`,
  `bridge_tunnel_header` and channel helpers needed no new home: the glue
  defines them.
* `make link-check-full` (`scripts/link-check-full.sh`): 98 objects (mt76
  27, LinuxKPI 22, net80211 31, bsdkpi + AES 15, glue 3), no duplicate
  strong symbol, 80 undefined symbols, all of them `lkpi/os.h` (66) or
  libc/libkern (14).  `make link-check` (driver + LinuxKPI only) now lists
  the 43 symbols it expects from the stack, and with `MAC80211_STUB=1`
  must be empty as before.

## Milestone 3 as built (`tests/host/test_probe`)

The probe test links the same objects as `link-check-full` plus the libc
backend and the chip model, and `tests/host/wlan_host.c` (compiled with the
glue's flags) drives net80211 the way the IO80211 layer will.  Sequence
observed, in order:

1. `lkpi_runtime_init()`: ..., `bsdkpi_init()` (SYSINITs, MOD_LOAD: ciphers,
   scanners, ratectl, the "wlan" cloner), PCI, driver module init.
2. Probe, firmware download, `mt7925_init_work` →
   `ieee80211_register_hw()` → `linuxkpi_ieee80211_ifattach()` →
   `ieee80211_ifattach()`: one `ieee80211com` named after the PCI device
   with the EFUSE MAC address, STA and WPA capabilities, HT and VHT, 378
   channel table entries covering the 14 + 28 frequencies of the driver's
   2.4 and 5 GHz sbands.
3. `bsdkpi_if_clone_create("wlan")` → `wlan_clone_create` →
   `lkpi_ic_vap_create` → `mt7925_start` + `mt7925_add_interface`: 7 MCU
   commands (SET_DOMAIN_INFO, BAND_CONFIG ×3, DEV_INFO_UPDATE,
   BSS_INFO_UPDATE, EDCA_UPDATE); `wlan0` exists in INIT.
4. `SIOCSIFFLAGS` up → INIT → SCAN → `ieee80211_check_scan` →
   `lkpi_ic_scan_start` (hw scan, 42 channels) → `mt7925_hw_scan` → MCU
   `UNI SCAN_REQ`.  The model answers with one beacon on the data RX ring
   and an unsolicited `SCAN_DONE` event.
5. Beacon: RX ring → `mt7925_queue_rx_skb` → `mt76_rx` →
   `linuxkpi_ieee80211_rx` → rxq task → `ieee80211_input_mimo_all` →
   `sta_recv_mgmt` → `ieee80211_add_scan`: **a scan-cache entry with the
   model's SSID** (`ieee80211_scan_iterate`).  Scan done:
   `mt7925_scan_work` → `ieee80211_scan_completed` → `ieee80211_scan_done`.
6. Down (SCAN → INIT, cache flushed), manual roaming
   (`IEEE80211_IOC_ROAMING`), up, then `SIOCS80211`/`IEEE80211_IOC_SCAN_REQ`
   twice: the first, in INIT, only moves the vap to SCAN (see below), the
   second sends exactly one more MCU scan request and fills the cache again.
7. Down, `bsdkpi_if_clone_destroy` → `lkpi_ic_vap_delete` →
   `mt7925_remove_interface` + `mt7925_stop` (2 MCU commands), then
   `lkpi_pci_detach` → `ieee80211_unregister_hw` → `ieee80211_ifdetach`.

The model's assertions hold throughout (0 unknown commands, 0 model
errors); valgrind memcheck reports 0 errors.  With `--leak-check=full` the
only definite leaks are the lazily allocated lock objects of driver
structures that are freed without a destroy call (accepted by design, see
`src/linuxkpi/src/lock.c`).

**What the test showed about the stack** (relevant for the kext):

* **No 6 GHz in net80211.**  The driver registers a 6 GHz sband, but
  `lkpi_ic_getradiocaps()` only translates 2.4 and 5 GHz and net80211 at the
  pinned commit has no 6 GHz channel flags; the hw-scan request carries
  `n_6ghz_params = 0`.  Wi-Fi 6E needs work in both (phase 5).
* **`cryptocaps` is 0** after attach although the driver lists cipher
  suites: the glue's `hw_crypto` tunable defaults to off, so net80211 would
  do CCMP/GCMP in software and frames would reach the driver already
  encrypted.  To be decided (and tested on hardware) in milestone 4.
* **Scan results only count while net80211 is scanning.**  A beacon that
  reaches `sta_input` after `ieee80211_scan_done()` is dropped as "not to
  bss"; with a hw scan the firmware's results and its scan-done event race
  through different threads (RX NAPI → rxq task versus `scan_work`).  The
  driver's 100 ms deferral of the completion normally hides it.
* **`IEEE80211_IOC_SCAN_REQ` in INIT** stores the request and enters SCAN,
  but `iv_scanreq_flags` is 16 bits wide, so `IEEE80211_IOC_SCAN_FLUSH`
  (0x10000) is lost and a "hot" cache (a scan finished within
  `iv_scanvalid`, 60 s) answers the request without scanning.  Requests in
  SCAN/RUN always scan.  The IO80211 layer should bring the vap to SCAN
  first (or lower `iv_scanvalid`).
* **With automatic roaming and nothing to join, net80211 rescans in a
  tight loop** (every 100 ms against the model, which completes scans
  instantly); the IO80211 layer should run the vap with
  `IEEE80211_ROAMING_MANUAL`, like wpa_supplicant does.
* `curthread` is one static descriptor in bsdkpi, so the thread ids in the
  glue's traces are all 0, and `sx(9)` is a recursive mutex (the glue
  creates `lvif_sx`/`mc_sx` with `SX_RECURSE`).
* After the last interface is removed `mt792x_stop()` cancels `pm.ps_work`
  and nothing re-arms it, so the chip can stay in driver own until the next
  activity (upstream behaviour; visible in the test as "driver own" before
  detach on some runs).

## Build set

Station mode first.  Built: `ieee80211.c, _proto, _node, _input, _output,
_sta, _scan, _scan_sta, _scan_sw, _ht, _vht, _crypto, _crypto_none/_wep/
_tkip/_ccmp/_gcmp/_gcm, _power, _phy, _regdomain, _action, _ageq, _ratectl,
_radiotap, _ratectl_none, _amrr, _monitor, _ioctl, _dfs, _freebsd` and
`linux_80211.c`, `linux_80211_macops.c`, `lkpi80211_compat.c`.  Deferred:
`_hostap` (AP mode, phase 5), `_mesh/_hwmp`, `_tdma`, `_wds`, `_adhoc`,
`_superg`, `_rssadapt`, `_acl`, `_alq`, `_ddb`.  (`_ratectl_none` and
`_amrr` are in because the glue initialises net80211 rate control for every
vap; the rates themselves come from the firmware.)

`make check-net80211` front-end compiles that set against bsdkpi on any host
(the same gate as `check-headers`), `make check-lkpi80211` does the same
for the glue, and `make link-check-full` proves the complete program links.

## Milestones

1. **Done.** bsdkpi headers and mbuf/callout/taskqueue/ifnet
   implementations; the net80211 build set compiles front-end clean.
2. **Done (host).** `linux_80211.c` compiles against bsdkpi + LinuxKPI with
   a patch series (`patches/0004-*`) for the macOS-specific spots; the
   phase-1 stub `mac80211_stub.c` is dropped from the default build.  Open:
   the same switch in `src/kext/Makefile` (needs a Mac to validate bsdkpi's
   `#include_next` against Kernel.framework).
3. **Done (host).** `test_probe`'s software MCU model reaches
   `ieee80211_register_hw`; net80211 attaches a `wlan0` vap; a scan request
   flows driver-ward and a modelled beacon comes back into the scan cache.
4. On hardware: scan results, open-system association, WPA2-PSK
   (net80211 runs the 4-way handshake in kernel through `ieee80211_crypto`
   and the key install goes to `mt7925_set_key`), WPA3-SAE.
5. Data traffic through the interim `IOEthernetInterface` (phase 4 replaces
   it with `IO80211Interface`).
