# AirDrop, Continuity and what a third-party Wi-Fi driver can offer

This page sets expectations.  The goal of the project is a driver that is as
native as possible, and most Apple integration is reachable.  AirDrop is the
exception, and it is better to say so up front than to discover it after
months of work.

## How Apple's features map to the radio

| Feature | Discovery | Data path | Needs AWDL? |
|---|---|---|---|
| Wi-Fi menu, System Settings, Location Services | — | — | No (needs IO80211Family integration, phase 4) |
| Handoff | Bluetooth LE | Infrastructure Wi-Fi or AWDL | No (works over the LAN) |
| Universal Clipboard | Bluetooth LE | Infrastructure Wi-Fi or AWDL | No |
| Instant Hotspot | Bluetooth LE | Normal Wi-Fi association | No |
| iPhone cellular calls / SMS relay | Bluetooth LE | Infrastructure Wi-Fi | No |
| Unlock with Apple Watch / Auto Unlock | Bluetooth LE ranging | Bluetooth + Wi-Fi presence | Partially (ranging uses peer-to-peer Wi-Fi on recent macOS) |
| Continuity Camera (wired or Wi-Fi) | Bluetooth LE | Infrastructure Wi-Fi or AWDL | No for infrastructure |
| **AirDrop** | Bluetooth LE | **AWDL only** | **Yes** |
| **Sidecar** (wireless) | Bluetooth LE | **AWDL** | **Yes** |
| **AirPlay to this Mac** (peer-to-peer) | Bluetooth LE / Bonjour | **AWDL** | Yes (Bonjour-over-LAN AirPlay works without it) |

## What AWDL is

Apple Wireless Direct Link is Apple's proprietary peer-to-peer Wi-Fi
protocol.  The radio keeps its infrastructure association and, in
synchronized time slots negotiated through action frames, hops to a social
channel (6, 44 or 149) to talk to peers.  It needs:

1. **Hardware/firmware cooperation**: precise, synchronized channel switching
   while staying associated; multiple MAC addresses; buffering of
   infrastructure traffic during off-channel periods.  Apple's Broadcom and
   Apple-silicon radios implement this in firmware.
2. **A driver-visible interface** (`awdl0`) with Apple's private
   `IO80211Family` semantics that `sharingd`, `mDNSResponder` and `AirDrop`
   drive through undocumented ioctls.
3. **The protocol itself**: election, synchronization and availability
   windows.  It has been reverse-engineered (the Open Wireless Link project
   implements AWDL on Linux), so the on-air protocol is knowable.

## Why no third-party macOS driver has AirDrop

Every Intel and Broadcom-based third-party macOS driver (itlwm/AirportItlwm,
the Broadcom patch sets) stops at points 1–2: the firmware of non-Apple
chips does not provide AWDL scheduling, and even a software implementation of
the protocol would have to present itself through Apple's private `awdl0`
interface whose contract is not public and changes between releases.  Those
projects deliver everything else in the table above through the IO80211
integration we plan in phase 4.

## What this means for the MT7925

* The MT7925 firmware does support off-channel scheduling for its own
  features (hardware scan, remain-on-channel, NAN, P2P).  A software AWDL
  implementation on top of mt76's `remain_on_channel`/channel-context
  machinery is *conceivable*; it is a research project, not a port.
* The project therefore commits to: native Wi-Fi (menu bar, Settings,
  WPA2/WPA3, 6 GHz where macOS allows it), and all Continuity features that
  work over Bluetooth plus infrastructure Wi-Fi.  Pair the card's Bluetooth
  half with a working Bluetooth driver for those.
* AirDrop, wireless Sidecar and peer-to-peer AirPlay are tracked as a
  research item in `docs/ROADMAP.md`.  If they ever work, it will be after
  phases 3 and 4 are solid and on top of them, not instead of them.

## Practical workarounds today

* Files: use LocalSend / a shared folder / iCloud Drive; "AirDrop over
  the LAN" does not exist, but Finder's "Connect to iPhone" via USB works.
* Second display: Sidecar over USB works without AWDL.
