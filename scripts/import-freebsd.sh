#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Vendor the BSD-2-Clause pieces we reuse from FreeBSD's LinuxKPI:
#
#   * the Linux 802.11 API headers (mac80211.h, cfg80211.h, ieee80211.h,
#     nl80211.h, regulatory.h, netlink.h) that Bjoern Zeeb re-implemented
#     for FreeBSD so that Linux wireless drivers compile without GPL headers;
#   * FreeBSD's net80211/ieee80211.h frame definitions they depend on;
#   * the mac80211 -> net80211 glue (linux_80211*.c/h), kept as the
#     reference implementation for the phase-3 802.11 stack.
#
# Files land in src/linuxkpi/include/freebsd-* and src/lkpi80211/ unchanged;
# our adaptations are kept in #if defined(__APPLE__) blocks via patches/.
#
# Usage: scripts/import-freebsd.sh [/path/to/freebsd-src]
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:-}
if [ -z "$SRC" ]; then
	TMP=$(mktemp -d)
	trap 'rm -rf "$TMP"' EXIT
	git clone --quiet --depth 1 --filter=blob:none --sparse \
	    https://github.com/freebsd/freebsd-src "$TMP/freebsd"
	git -C "$TMP/freebsd" sparse-checkout set --quiet \
	    sys/compat/linuxkpi/common sys/net80211
	SRC="$TMP/freebsd"
fi
HEAD=$(git -C "$SRC" rev-parse HEAD)
DATE=$(git -C "$SRC" log -1 --format=%cd --date=short)
LK="$SRC/sys/compat/linuxkpi/common"
INC="$ROOT/src/linuxkpi/include"

mkdir -p "$INC/net" "$INC/linux" "$INC/net80211" "$ROOT/src/lkpi80211"
cp "$LK/include/net/mac80211.h"     "$INC/net/mac80211.h"
cp "$LK/include/net/cfg80211.h"     "$INC/net/cfg80211.h"
cp "$LK/include/net/regulatory.h"   "$INC/net/regulatory.h"
cp "$LK/include/linux/ieee80211.h"  "$INC/linux/ieee80211.h"
cp "$LK/include/linux/nl80211.h"    "$INC/linux/nl80211.h"
cp "$LK/include/linux/kconfig.h"    "$INC/linux/kconfig.h"
cp "$LK/include/linux/tracepoint.h" "$INC/linux/tracepoint.h"
cp "$SRC/sys/net80211/ieee80211.h"  "$INC/net80211/ieee80211.h"
cp "$SRC/sys/net80211/ieee80211_crypto.h" "$INC/net80211/ieee80211_crypto.h"
cp "$SRC/sys/net80211/_ieee80211.h" "$INC/net80211/_ieee80211.h"
cp "$LK/src/linux_80211.c"          "$ROOT/src/lkpi80211/linux_80211.c"
cp "$LK/src/linux_80211.h"          "$ROOT/src/lkpi80211/linux_80211.h"
cp "$LK/src/linux_80211_macops.c"   "$ROOT/src/lkpi80211/linux_80211_macops.c"
cp "$LK/src/linuxkpi_80211_pm.c"    "$ROOT/src/lkpi80211/linuxkpi_80211_pm.c"

cat > "$INC/FREEBSD_UPSTREAM" <<EOT
# BSD-2-Clause files vendored from FreeBSD (scripts/import-freebsd.sh)
url=https://github.com/freebsd/freebsd-src
commit=$HEAD
date=$DATE
files=net/mac80211.h net/cfg80211.h net/regulatory.h linux/ieee80211.h linux/nl80211.h linux/kconfig.h linux/tracepoint.h net80211/ieee80211.h net80211/ieee80211_crypto.h net80211/_ieee80211.h ../../lkpi80211/*
EOT
echo "Imported FreeBSD $HEAD ($DATE)"
