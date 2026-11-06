#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Vendor FreeBSD's net80211 802.11 stack (BSD-2/3-Clause) and the small set of
# BSD support files it needs, pinned to the commit in
# src/linuxkpi/include/FREEBSD_UPSTREAM:
#
#   sys/net80211/*.[ch]            -> src/net80211/upstream/ (verbatim)
#   src/net80211/net80211 -> upstream  so that <net80211/...> resolves there
#                                   for net80211 itself and for LinuxKPI
#   sys/sys/queue.h                -> src/bsdkpi/include/sys/queue.h
#   sys/net/{ethernet,if_llc,if_media}.h -> src/bsdkpi/include/net/
#   sys/crypto/rijndael/*          -> src/bsdkpi/crypto/rijndael/ (AES for CCMP/GCMP)
#
# ieee80211_freebsd.[ch] are the OS-specific layer upstream.  They are kept
# and built: src/bsdkpi provides the FreeBSD kernel primitives they use, and
# the few spots that need macOS behaviour are patched (patches/0003-*,
# re-applied with scripts/apply-patches.sh after an import).
#
# Usage: scripts/import-net80211.sh [/path/to/freebsd-src]
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:-}
if [ -z "$SRC" ]; then
	TMP=$(mktemp -d)
	trap 'rm -rf "$TMP"' EXIT
	git clone --quiet --depth 1 --filter=blob:none --sparse https://github.com/freebsd/freebsd-src "$TMP/freebsd"
	git -C "$TMP/freebsd" sparse-checkout set --quiet sys/net80211 sys/sys sys/net sys/crypto/rijndael
	SRC="$TMP/freebsd"
fi
HEAD=$(git -C "$SRC" rev-parse HEAD)
DATE=$(git -C "$SRC" log -1 --format=%cd --date=short)

N80="$ROOT/src/net80211"
BK="$ROOT/src/bsdkpi"
rm -rf "$N80/upstream"
mkdir -p "$N80/upstream" "$BK/include/sys" "$BK/include/net" "$BK/crypto/rijndael"

cp "$SRC"/sys/net80211/*.c "$SRC"/sys/net80211/*.h "$N80/upstream/"
ln -sfn upstream "$N80/net80211"
cp "$SRC/sys/sys/queue.h"      "$BK/include/sys/queue.h"
cp "$SRC/sys/net/ethernet.h"   "$BK/include/net/ethernet.h"
cp "$SRC/sys/net/if_llc.h"     "$BK/include/net/if_llc.h"
cp "$SRC/sys/net/if_media.h"   "$BK/include/net/if_media.h"
for f in rijndael.h rijndael_local.h rijndael-api-fst.h rijndael-alg-fst.c rijndael-api-fst.c rijndael-api.c; do
	cp "$SRC/sys/crypto/rijndael/$f" "$BK/crypto/rijndael/$f"
done

cat > "$N80/UPSTREAM" <<EOT
# FreeBSD net80211 vendored by scripts/import-net80211.sh (do not edit by hand)
url=https://github.com/freebsd/freebsd-src
commit=$HEAD
date=$DATE
layout=upstream/ (verbatim sys/net80211)
EOT
echo "Imported net80211 from FreeBSD $HEAD ($DATE): $(ls "$N80/upstream" | wc -l) files"
