#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Download the MediaTek firmware blobs the driver loads at runtime.
#
# The blobs are redistributable binaries (see LICENCE.mediatek in
# linux-firmware) but are NOT part of this repository; the build copies them
# into the kext bundle's Resources directory with the same relative path the
# driver asks for (e.g. mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin).
#
# Usage: scripts/fetch-firmware.sh [commit-or-tag]   (default: pinned below)
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST="$ROOT/firmware"
REF=${1:-main}
BASE="https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain"

FILES="
mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin
mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin
mediatek/mt7927/WIFI_MT6639_PATCH_MCU_2_1_hdr.bin
mediatek/mt7927/WIFI_RAM_CODE_MT6639_2_1.bin
LICENCE.mediatek
"

for f in $FILES; do
	mkdir -p "$DEST/$(dirname "$f")"
	echo "fetching $f"
	if ! curl -sSfL -o "$DEST/$f" "$BASE/$f?h=$REF"; then
		case "$f" in
		LICENCE.mediatek) echo "  (license text not found at $REF; see WHENCE in linux-firmware)";;
		*) echo "  failed: $f"; exit 1;;
		esac
	fi
done
echo "firmware stored under $DEST"
