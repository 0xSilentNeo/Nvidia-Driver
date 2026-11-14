#!/bin/bash
# SPDX-License-Identifier: MIT
#
# First hardware test on a Mac: check prerequisites, build MT7925.kext, load
# it for a fixed time, capture its log output, unload it, and write
# everything to one report file to send back.
#
#   scripts/mac-try.sh              # checks + build only (safe)
#   scripts/mac-try.sh --load       # also load for 60 s, then unload
#   scripts/mac-try.sh --load 180   # load for 180 s
#
# Loading a kernel extension can panic the machine.  Save your work first.
# The kext is loaded from the build directory and never installed into
# /Library/Extensions, so a reboot always returns the system to normal.
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
LOAD=0
SECS=60
if [ "${1:-}" = "--load" ]; then
	LOAD=1
	SECS=${2:-60}
fi
REPORT="$ROOT/build/mac-try-$(date +%Y%m%d-%H%M%S).log"
mkdir -p "$ROOT/build"
exec > >(tee "$REPORT") 2>&1

section() { printf '\n==== %s ====\n' "$*"; }
fail() { printf 'STOP: %s\n' "$*"; printf '\nReport: %s\n' "$REPORT"; exit 1; }

section "system"
[ "$(uname -s)" = "Darwin" ] || fail "this script runs on macOS"
sw_vers
uname -m
sysctl -n machdep.cpu.brand_string 2>/dev/null
ARCH=$(uname -m)

section "security (needed to load an unsigned kext)"
csrutil status 2>&1
if [ "$ARCH" = "arm64" ]; then
	echo "Apple silicon: third-party kexts need 'Reduced Security' + 'Allow user"
	echo "management of kernel extensions' in Startup Security Utility (Recovery)."
	bputil -d 2>/dev/null | grep -i -E "kext|security" || true
fi
nvram boot-args 2>/dev/null || echo "boot-args: (none)"

section "MediaTek Wi-Fi hardware on the PCI bus"
# vendor 0x14c3; devices handled by the driver
FOUND=$(ioreg -r -c IOPCIDevice -l 2>/dev/null | grep -i -E '"vendor-id" = <c3140000>' | wc -l | tr -d ' ')
ioreg -r -c IOPCIDevice -l 2>/dev/null | awk '
	/^\+-o / { name=$0 }
	/"vendor-id" = <c3140000>/ { v=1 }
	/"device-id"/ { d=$0 }
	/^  \}/ { if (v) { print name; print "   " d }; v=0 }' | head -20
system_profiler SPPCIDataType 2>/dev/null | grep -i -B2 -A8 -E "mediatek|14c3" | head -40
if [ "$FOUND" = "0" ]; then
	echo "No device with vendor 14c3 found. Check that the card is seated and, on"
	echo "a Hackintosh, that the slot is enabled. Building anyway."
fi

section "other Wi-Fi drivers that could claim the card"
kmutil showloaded 2>/dev/null | grep -i -E "itlwm|airport|80211|wifi|mt79|mediatek" || echo "(none of the usual suspects loaded)"

section "toolchain"
xcode-select -p 2>&1 || fail "install the command line tools: xcode-select --install"
xcrun --sdk macosx --show-sdk-path 2>&1 || fail "no macOS SDK found by xcrun"
xcrun clang --version | head -1

section "firmware"
if ! ls firmware/mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin >/dev/null 2>&1; then
	scripts/fetch-firmware.sh || fail "firmware download failed"
fi
ls -l firmware/mediatek/mt7925/

section "build"
make -C src/kext ARCHS="$ARCH" 2>&1 | tail -200
KEXT=$(find build src/kext -maxdepth 3 -name MT7925.kext -type d 2>/dev/null | head -1)
[ -n "$KEXT" ] || fail "build did not produce MT7925.kext (errors above)"
echo "built: $KEXT"
plutil -lint "$KEXT/Contents/Info.plist"
file "$KEXT/Contents/MacOS/MT7925"
ls -lR "$KEXT/Contents/Resources" 2>/dev/null | head -20

if [ "$LOAD" != "1" ]; then
	section "done (build only)"
	echo "Re-run with --load to load it for ${SECS}s and capture the log."
	printf '\nReport: %s\n' "$REPORT"
	exit 0
fi

section "load"
echo "Loading for ${SECS}s. If macOS asks to allow the extension in System"
echo "Settings > Privacy & Security, allow it, reboot if asked, and re-run."
TMPK=$(mktemp -d)/MT7925.kext
cp -R "$KEXT" "$TMPK"
sudo chown -R root:wheel "$TMPK"
START=$(date "+%Y-%m-%d %H:%M:%S")
sudo kmutil load -p "$TMPK" 2>&1
kmutil showloaded 2>/dev/null | grep -i mt7925 || echo "(not shown as loaded)"
sleep "$SECS"

section "driver log since load"
log show --start "$START" --style compact \
	--predicate 'sender == "MT7925" OR eventMessage CONTAINS "MT7925" OR eventMessage CONTAINS "mt7925" OR eventMessage CONTAINS "lkpi"' \
	2>/dev/null | tail -400

section "IORegistry"
ioreg -r -c MT7925PCIe -l 2>/dev/null | head -60 || true

section "unload"
sudo kmutil unload -b org.mt7925port.MT7925 2>&1
kmutil showloaded 2>/dev/null | grep -i mt7925 || echo "unloaded"

printf '\nReport: %s\n' "$REPORT"
