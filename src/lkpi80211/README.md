# `src/lkpi80211` — mac80211 → net80211 glue

FreeBSD's LinuxKPI 802.11 compatibility layer (BSD-2-Clause, pinned to the
commit in `src/linuxkpi/include/FREEBSD_UPSTREAM`), plus two files of ours.
Design and status: `docs/PHASE3_NET80211.md`.

| File | Origin | Role |
|---|---|---|
| `linux_80211.c` | FreeBSD + `patches/0004` | Implements the `ieee80211_*`/`wiphy_*` calls a mac80211 driver makes (register_hw, rx, tx_status, scan_completed, queue handling, key/sta/vif management) by translating them to net80211, and net80211's driver hooks (`ic_vap_create`, `ic_scan_start`, `iv_newstate`, ...) to the driver's `struct ieee80211_ops`. ~10k lines. |
| `linux_80211_macops.c` | FreeBSD, verbatim | Thin wrappers calling the driver's `struct ieee80211_ops` callbacks with tracing. |
| `linux_80211.h` | FreeBSD, verbatim | Private state: `lkpi_hw`, `lkpi_vif`, `lkpi_sta` that wrap net80211's `ieee80211com`/`ieee80211vap`/`ieee80211_node`. |
| `linuxkpi_80211_pm.c` | FreeBSD, verbatim, **not built** | newbus driver for FreeBSD's suspend/resume child devices. No newbus here: phase 4 calls `lkpi_80211_suspend()/lkpi_80211_resume()` from `IOService::setPowerState`. Kept so that a re-import diffs cleanly. |
| `lkpi80211_prelude.h` | ours (MIT) | Force-included for this directory (`LKPI80211_CFLAGS`). Parses the Linux headers before `sys/param.h` so that the macros both kernels define (`ALIGN`, `LIST_HEAD`, `PAGE_SIZE`, `IFF_*`, `ieee80211_send_bar`) do not collide, and pulls in the FreeBSD headers the glue gets implicitly in a FreeBSD kernel. |
| `lkpi80211_compat.c` | ours (MIT) | What `net/mac80211.h` declares but the glue lacks: `ieee80211_txq_aql_pending`, the NAN notifications, the `compat.linuxkpi` sysctl node. |

Build and check:

```
make check-lkpi80211     # front-end compile, zero warnings
make link-check-full     # driver + LinuxKPI + bsdkpi + net80211 + glue link
make -C tests/host run   # test_probe runs the stack against the chip model
```

Rules for changes: prefer the prelude or a shim header (`src/linuxkpi`,
`src/bsdkpi`) over editing the vendored files; an unavoidable edit goes
under `#if defined(LKPI_MACOS)` and into `patches/0004-lkpi80211-lkpi-macos.patch`
(`scripts/regen-patches.sh` regenerates it).
