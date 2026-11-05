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
| `printf`, `log`, `KASSERT`, `panic` | `printk`, `lkpi_os_panic`. |
| `sysctl(9)`, `SYSCTL_*`, `TUNABLE_*` | no-ops that keep the variables. |
| `module(9)`, `MODULE_VERSION/DEPEND`, `SYSINIT`, `DECLARE_MODULE` | no-ops; net80211's module-registration tables (crypto ciphers, scanners, rate control, auth) are populated by an explicit `net80211_register_builtin_modules()` called from `lkpi_runtime_init`. |
| `struct mbuf` and `m_*`/`mbufq_*`/`m_tag_*` | **own implementation** with FreeBSD's field names (`m_next`, `m_nextpkt`, `m_data`, `m_len`, `m_flags`, `m_pkthdr.len/rcvif/flowid`) and semantics; every mbuf owns a 2 KiB (or jumbo) buffer, no clusters/refcounted externals.  Kept separate from `sk_buff`; the glue copies between them at the two crossing points (`linux_80211.c` already does `mbuf ↔ skb` conversion). XNU `mbuf_t` is used only at the IO80211 boundary in phase 4, where the frame is copied once into the network stack's mbuf. |
| `struct ifnet` + `if_*()` accessors, `ether_ifattach`, `if_input`, `if_transmit`, `if_link_state_change`, `if_media` | **own minimal implementation**: an interface object with the accessor API net80211 uses (`if_private.h` style), `if_input` and `if_transmit` function pointers that phase 4 binds to the IO80211 interface, and the `ifmedia` list used for mode/rate reporting. |
| `bpf(9)`, `route(9)` (`rt_ieee80211msg`), `vnet`, `netisr`, `debugnet` | no-ops (`bpf_peers_present()` is 0; `rt_ieee80211msg` becomes a notification hook the IO80211 layer subscribes to for Apple's state-change events). |
| `sockio.h` ioctls (`SIOCS80211`/`SIOCG80211`, `ieee80211req`) | kept: `ieee80211_ioctl.c` is the control-plane API; phase 4 drives it from `IO80211Controller` requests, and a debug `IOUserClient` exposes it for host-style tools. |
| `crypto/rijndael` | vendored from FreeBSD (public-domain reference AES) for CCMP/GCMP. |

Everything in bsdkpi is MIT and written here except `sys/queue.h`,
`net/ethernet.h`, `net/if_llc.h`, `net/if_media.h` and the rijndael files,
which are vendored (BSD / public domain).

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
