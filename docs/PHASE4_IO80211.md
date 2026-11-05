# Phase 4 — native macOS integration (IO80211Family)

After phase 3 the kext has a working 802.11 stack but macOS does not know it
is Wi-Fi.  Phase 4 makes the interface a first-class AirPort device: menu bar
Wi-Fi item, System Settings › Wi-Fi, `wdutil`, Location Services, and the
Continuity features that work over Bluetooth plus infrastructure Wi-Fi.

## Two interfaces, one stack

| | Interim (phase 3 end) | Native (phase 4) |
|---|---|---|
| IOKit class | `IOEthernetController` + `IOEthernetInterface` (public API) | `IO80211Controller` + `IO80211Interface` (private, reverse-engineered) |
| What macOS sees | An Ethernet port (`en1`) with a link | A Wi-Fi interface (`en0`-style AirPort device) |
| Who drives scan/join | Our own `IOUserClient` + a CLI/daemon (`mt7925ctl`) | Apple's `airportd`/`wifid` through `APPLE80211_IOC_*` requests |
| Menu bar / Settings | No | Yes |
| Handoff, Universal Clipboard, Instant Hotspot | Work (they need an IP network and Bluetooth, not a Wi-Fi interface type) | Work |
| Location Services (Wi-Fi positioning) | No | Yes |
| AirDrop / Sidecar / peer AirPlay | No | No (AWDL, see `docs/AIRDROP.md`) |

The interim interface is cheap, uses only public KPIs and gives a usable
network early.  It also stays as a debugging path.  Both bind to the same
net80211 `vap` through `if_input`/`if_transmit` in `src/bsdkpi/src/ifnet.c`.

## IO80211Family facts that shape the design

* The headers are private.  Every third-party macOS Wi-Fi driver
  (AirportItlwm, the Broadcom patch projects) ships reverse-engineered class
  declarations, derived from the framework's symbol tables and from Apple's
  historical open-source `IO80211Family` drops (10.4–10.6 era) and public
  `apple80211_ioctl.h` constants.  We will write our own under
  `src/kext/io80211/` from the same public sources; nothing is copied from
  other projects.
* The vtable layout of `IO80211Controller` changes between macOS releases.
  AirportItlwm builds one binary per OS generation for this reason.  Plan
  for per-generation header variants selected at build time
  (`IO80211_API_VERSION`), starting with the release the first tester runs.
* On macOS 13+ Apple's own drivers moved to the Skywalk-based
  `IO80211SkywalkInterface`, but the legacy `IO80211Interface` path remained
  loadable through macOS 15 and is what the third-party drivers use.  Watch
  this on every new release.
* `airportd` talks to the driver through `IO80211Controller::apple80211Request()`
  with `SIOCGA80211`/`SIOCSA80211` carrying `APPLE80211_IOC_*` request numbers.
  The driver implements a getter/setter table; the ones that must work for a
  usable system are roughly: `CARD_CAPABILITIES`, `OP_MODE`, `POWER`,
  `SSID`, `BSSID`, `CHANNEL`, `SUPPORTED_CHANNELS`, `PHY_MODE`, `STATE`,
  `RSSI`, `NOISE`, `RATE`, `MCS`, `AUTH_TYPE`, `CIPHER_KEY`, `RSN_IE`,
  `SCAN_REQ`/`SCAN_RESULT`, `ASSOCIATE`, `DISASSOCIATE`, `ASSOCIATION_STATUS`,
  `COUNTRY_CODE`, `LOCALE`, `TX_ANTENNA`/`ANTENNA_DIVERSITY`, `DRIVER_VERSION`,
  `HARDWARE_VERSION`, `MCS_INDEX_SET`, `ROAM_THRESH`, `POWERSAVE`,
  `LINK_QUAL_EVENT_PARAMS`, `IE`, `SCANCACHE_CLEAR`, `VIRTUAL_IF_CREATE`
  (return unsupported), and the event posts `APPLE80211_M_SCAN_DONE`,
  `_LINK_CHANGED`, `_SSID_CHANGED`, `_BSSID_CHANGED`, `_POWER_CHANGED`,
  `_ASSOC_DONE`, `_RSSI_CHANGED`.
* Key handling: `airportd` performs the WPA/RSN negotiation decisions in user
  space but expects the driver to run the 4-way handshake and install keys
  when given the PMK/PSK through `APPLE80211_IOC_CIPHER_KEY`/`RSN_IE`.
  net80211 does exactly this in kernel, so the mapping is natural: the
  association request becomes a net80211 `IEEE80211_IOC_SSID`/`BSSID`/
  `WPAKEY` sequence followed by `IEEE80211_IOC_SCAN_REQ` and the state
  machine's join.

## Layering

```
 airportd / wifid / sharingd (user space)
        │  APPLE80211_IOC_* (SIOC[GS]A80211)      events: postMessage(APPLE80211_M_*)
 ┌──────▼─────────────────────────────────────────────────────────────────┐
 │ MT7925Controller : IO80211Controller          src/kext/io80211/        │
 │   request table → net80211 ieee80211_ioctl() (IEEE80211_IOC_*)        │
 │   scan cache ← net80211 scan table; state ← vap state machine        │
 │ MT7925Interface : IO80211Interface  (inputPacket / outputPacket)      │
 └──────┬──────────────────────────────────▲──────────────────────────────┘
        │ mbuf_t → bsdkpi mbuf (copy)      │ bsdkpi mbuf → mbuf_t (copy)
 ┌──────▼──────────────────────────────────┴──────────────────────────────┐
 │ bsdkpi ifnet (if_transmit / if_input)   net80211 vap (wlan0, STA)     │
 └────────────────────────────────────────────────────────────────────────┘
```

* **Control**: every `APPLE80211_IOC_*` setter maps onto one or more
  `IEEE80211_IOC_*` net80211 ioctls, so the control plane reuses
  `ieee80211_ioctl.c` instead of inventing a new one.  Getters read the vap
  / scan table under the net80211 locks.  This is the same shape itlwm uses
  with OpenBSD's net80211, so it is known to satisfy `airportd`.
* **Data**: one copy per frame at the boundary (`mbuf_t` ↔ bsdkpi mbuf).
  Measure before optimising; a later step can make bsdkpi mbufs wrap
  `mbuf_t` on the kext build only.
* **Events**: net80211's `rt_ieee80211msg()` hook (phase 3 stub) becomes the
  source for `postMessage()` events, and `if_link_state_change` drives the
  link status.
* **Power**: `IO80211Controller` participates in system sleep; map to
  `mt7925_pci_suspend/resume` through the existing `setPowerState` path and
  re-join on wake through net80211's `ieee80211_resume_all`.

## Steps

1. Interim `IOEthernetInterface` + debug `IOUserClient` + `mt7925ctl` CLI
   (scan / join / status).  Validates the full stack on hardware with
   public APIs only.
2. Write `src/kext/io80211/IO80211*.hpp` declarations for the first target
   macOS generation, from public sources; verify against the installed
   framework's symbols (`nm` on `IO80211Family.kext`) on the test machine.
3. `MT7925Controller` with the request table above; bring up scanning and
   display in the menu bar.
4. Association paths: open, WPA2-PSK, WPA3-SAE; roaming events.
5. Country code / regulatory mapping from the firmware's CLC tables.
6. Sleep/wake, then per-OS-generation builds and an OpenCore recipe.
