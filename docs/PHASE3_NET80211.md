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
  `struct ieee80211_ops`.  About 10k lines; it already compiles against the
  `net/mac80211.h` we vendor.

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

## Build set

Station mode first.  Built: `ieee80211.c, _proto, _node, _input, _output,
_sta, _scan, _scan_sta, _scan_sw, _ht, _vht, _crypto, _crypto_none/_wep/
_tkip/_ccmp/_gcmp/_gcm, _power, _phy, _regdomain, _action, _ageq, _ratectl,
_radiotap, _monitor, _ioctl, _dfs, _freebsd` and `linux_80211.c`,
`linux_80211_macops.c`.  Deferred: `_hostap` (AP mode, phase 5),
`_mesh/_hwmp`, `_tdma`, `_wds`, `_adhoc`, `_superg`, `_amrr/_rssadapt`
(rate control stays in the driver/firmware), `_acl`, `_alq`, `_ddb`.

`make check-net80211` front-end compiles that set against bsdkpi on any host
(the same gate as `check-headers`); `make link-check` then shows what the
IO80211 layer must still provide (`if_input`/`if_transmit` consumers).

## Milestones

1. bsdkpi headers and mbuf/callout/taskqueue/ifnet implementations; the
   net80211 build set compiles front-end clean.
2. `linux_80211.c` compiles against bsdkpi + LinuxKPI with a patch series
   (`patches/0003-*`) for the macOS-specific spots; the phase-1 stub
   `mac80211_stub.c` is dropped from the build.
3. Host test: `test_probe` grows a software MCU model that answers the
   handful of commands needed to reach `ieee80211_register_hw`; net80211
   attaches a `wlan0` vap; a scan request flows driver-ward.
4. On hardware: scan results, open-system association, WPA2-PSK
   (net80211 runs the 4-way handshake in kernel through `ieee80211_crypto`
   and the key install goes to `mt7925_set_key`), WPA3-SAE.
5. Data traffic through the interim `IOEthernetInterface` (phase 4 replaces
   it with `IO80211Interface`).
