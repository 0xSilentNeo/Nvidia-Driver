# `src/lkpi80211` — mac80211 → net80211 glue (phase 3 reference)

These files are FreeBSD's LinuxKPI 802.11 compatibility layer, vendored
verbatim (BSD-2-Clause, see `src/linuxkpi/include/FREEBSD_UPSTREAM` for the
pinned commit):

| File | Role |
|---|---|
| `linux_80211.c` | Implements the `ieee80211_*`/`wiphy_*` calls a mac80211 driver makes (register_hw, rx, tx_status, scan_completed, queue handling, key/sta/vif management) by translating them to FreeBSD's `net80211` stack. ~10k lines. |
| `linux_80211_macops.c` | Thin wrappers calling the driver's `struct ieee80211_ops` callbacks with tracing. |
| `linux_80211.h` | Private state: `lkpi_hw`, `lkpi_vif`, `lkpi_sta` that wrap net80211's `ieee80211com`/`ieee80211vap`/`ieee80211_node`. |
| `linuxkpi_80211_pm.c` | Suspend/resume plumbing. |

**They are not built yet.** They depend on FreeBSD's `net80211` (which will
be ported in phase 3 onto an mbuf/ifnet shim; XNU has a public `mbuf_t` KPI)
and on FreeBSD kernel primitives (`mtx`, `callout`, `taskqueue`, `epoch`).

## Phase-3 plan

1. Port `sys/net80211` (BSD-2) into `src/net80211/` with a `bsdkpi/` shim:
   `mtx`→`IOLock`, `callout`→timers, `taskqueue`→workqueues, `mbuf`→XNU
   `mbuf_t`, `ifnet`→an internal interface object that later binds to the
   IO80211 interface (phase 4).
2. Adapt `linux_80211.c` to our LinuxKPI (it already targets the same
   `net/mac80211.h` we vendor, so most of it compiles as is; the FreeBSD
   kernel calls inside are the work).
3. Replace the phase-1 recording stub (`ieee80211_alloc_hw`/`register_hw`
   logging the advertised capabilities) with the real glue.

Until then, phase 1 links against `src/linuxkpi/src/mac80211_stub.c`, which
implements the handful of `ieee80211_*` entry points the probe path touches
(`ieee80211_alloc_hw`, `SET_IEEE80211_DEV`, `ieee80211_register_hw`,
`wiphy_*`) well enough for the driver to initialise the hardware, and logs
everything else.
