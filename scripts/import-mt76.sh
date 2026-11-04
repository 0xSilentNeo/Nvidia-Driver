#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Vendor the upstream Linux mt76 driver (BSD-3-Clause-Clear) into src/mt76.
#
# Only the files needed for the MT7925/MT7927/MT7928 PCIe path are copied.
# The copy is unmodified; local changes live in patches/*.patch and are
# applied by scripts/apply-patches.sh so that re-importing a newer upstream
# is a mechanical operation.
#
# Usage: scripts/import-mt76.sh [/path/to/mt76.git] [commit]
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST="$ROOT/src/mt76"
UPSTREAM_URL="https://github.com/openwrt/mt76"
PIN_FILE="$ROOT/src/mt76/UPSTREAM"

SRC=${1:-}
COMMIT=${2:-}

if [ -z "$SRC" ]; then
	TMP=$(mktemp -d)
	trap 'rm -rf "$TMP"' EXIT
	git clone --quiet --depth 1 "$UPSTREAM_URL" "$TMP/mt76"
	SRC="$TMP/mt76"
fi
if [ -n "$COMMIT" ]; then
	git -C "$SRC" fetch --quiet --depth 1 origin "$COMMIT"
	git -C "$SRC" checkout --quiet "$COMMIT"
fi
HEAD=$(git -C "$SRC" rev-parse HEAD)
DATE=$(git -C "$SRC" log -1 --format=%cd --date=short)

# Core library (mt76.ko), connac library, mt792x library, mt7925 PCIe.
FILES="
LICENSE
mt76.h dma.h util.h trace.h
mmio.c util.c dma.c mac80211.c eeprom.c tx.c agg-rx.c mcu.c scan.c channel.c pci.c
mt76_connac.h mt76_connac2_mac.h mt76_connac3_mac.h mt76_connac_mcu.h
mt76_connac_mcu.c mt76_connac_mac.c mt76_connac3_mac.c
mt792x.h mt792x_regs.h mt792x_acpi_sar.h mt792x_trace.h
mt792x_core.c mt792x_mac.c mt792x_dma.c mt792x_acpi_sar.c
testmode.h
mt7925/mt7925.h mt7925/mac.h mt7925/mcu.h mt7925/regs.h mt7925/regd.h mt7925/nan.h mt7925/cached_cal.h
mt7925/mac.c mt7925/mcu.c mt7925/regd.c mt7925/main.c mt7925/init.c mt7925/nan.c
mt7925/pci.c mt7925/pci_mac.c mt7925/pci_mcu.c mt7925/cached_cal.c
"

rm -rf "$DEST"
mkdir -p "$DEST/mt7925"
for f in $FILES; do
	cp "$SRC/$f" "$DEST/$f"
done

cat > "$PIN_FILE" <<EOT
# Upstream source of src/mt76 (do not edit by hand; see scripts/import-mt76.sh)
url=$UPSTREAM_URL
commit=$HEAD
date=$DATE
EOT

echo "Imported mt76 $HEAD ($DATE) into $DEST"
echo "Now run: scripts/apply-patches.sh"
