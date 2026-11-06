#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Regenerate patches/*.patch from the differences between the pinned upstream
# trees and our vendored copies.  Run after editing anything under src/mt76,
# the FreeBSD-derived headers or src/net80211/upstream so that
# scripts/import-*.sh + apply-patches.sh reproduce the tree exactly.
#
# Usage: scripts/regen-patches.sh [/path/to/mt76.git] [/path/to/freebsd-src]
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MT76_SRC=${1:-}
FB_SRC=${2:-}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

MT76_COMMIT=$(sed -n 's/^commit=//p' "$ROOT/src/mt76/UPSTREAM")
FB_COMMIT=$(sed -n 's/^commit=//p' "$ROOT/src/linuxkpi/include/FREEBSD_UPSTREAM")

if [ -z "$MT76_SRC" ]; then
	git clone --quiet --depth 1 https://github.com/openwrt/mt76 "$TMP/mt76"
	git -C "$TMP/mt76" fetch --quiet --depth 1 origin "$MT76_COMMIT"
	git -C "$TMP/mt76" checkout --quiet "$MT76_COMMIT"
	MT76_SRC="$TMP/mt76"
fi
if [ -z "$FB_SRC" ]; then
	git clone --quiet --depth 1 --filter=blob:none --sparse https://github.com/freebsd/freebsd-src "$TMP/freebsd"
	git -C "$TMP/freebsd" sparse-checkout set --quiet sys/compat/linuxkpi/common sys/net80211
	FB_SRC="$TMP/freebsd"
fi

mkdir -p "$ROOT/patches"
rm -f "$ROOT/patches"/*.patch

# 0001: mt76 include additions (same spots FreeBSD patches).
: > "$ROOT/patches/0001-mt76-lkpi-macos-includes.patch"
for f in mt76.h util.h; do
	diff -u --label "a/src/mt76/$f" --label "b/src/mt76/$f" "$MT76_SRC/$f" "$ROOT/src/mt76/$f" \
	    >> "$ROOT/patches/0001-mt76-lkpi-macos-includes.patch" || true
done

# 0002: FreeBSD LinuxKPI 802.11 header adaptations.
: > "$ROOT/patches/0002-freebsd-80211-headers-lkpi-macos.patch"
LK="$FB_SRC/sys/compat/linuxkpi/common/include"
INC="$ROOT/src/linuxkpi/include"
for f in net/mac80211.h net/cfg80211.h net/regulatory.h linux/ieee80211.h linux/nl80211.h linux/kconfig.h linux/tracepoint.h; do
	diff -u --label "a/src/linuxkpi/include/$f" --label "b/src/linuxkpi/include/$f" "$LK/$f" "$INC/$f" \
	    >> "$ROOT/patches/0002-freebsd-80211-headers-lkpi-macos.patch" || true
done

# 0003: FreeBSD net80211 adaptations (#if defined(LKPI_MACOS) hunks only).
: > "$ROOT/patches/0003-net80211-lkpi-macos.patch"
for f in "$ROOT"/src/net80211/upstream/*.[ch]; do
	b=$(basename "$f")
	diff -u --label "a/src/net80211/upstream/$b" --label "b/src/net80211/upstream/$b" \
	    "$FB_SRC/sys/net80211/$b" "$f" \
	    >> "$ROOT/patches/0003-net80211-lkpi-macos.patch" || true
done
for p in "$ROOT/patches"/*.patch; do
	[ -s "$p" ] || rm -f "$p"
done
ls -l "$ROOT/patches"
