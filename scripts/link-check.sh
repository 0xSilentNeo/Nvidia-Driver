#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Compile the vendored driver and the C half of the LinuxKPI layer to host
# objects and print the symbols that nothing in that set defines: the
# interface between the driver + LinuxKPI and the 802.11 stack (net80211,
# bsdkpi and the src/lkpi80211 glue), plus libkern functions.
# scripts/link-check-full.sh adds the stack and requires the list to be
# empty.  With MAC80211_STUB=1 the phase-1 recording stub is built instead
# of the stack and this list must be empty by itself.
#
# Works on any x86-64/arm64 Linux or macOS host with clang; needs no SDK.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OBJ=build/linkcheck
rm -rf "$OBJ"
mkdir -p "$OBJ"

CFLAGS=$(make -s print-LKPI_CFLAGS)
MT76_SRCS=$(make -s print-MT76_SRCS)
LKPI_SRCS=$(make -s print-LKPI_SRCS)

fail=0
for f in $MT76_SRCS; do
	o="$OBJ/mt76_$(echo "$f" | tr / _).o"
	# shellcheck disable=SC2086
	clang -c -O1 $CFLAGS -DKBUILD_MODNAME='"mt76"' -o "$o" "src/mt76/$f" || { echo "FAIL $f"; fail=1; }
done
for f in $LKPI_SRCS; do
	o="$OBJ/lkpi_$(basename "$f" .c).o"
	# shellcheck disable=SC2086
	clang -c -O1 $CFLAGS -Isrc/mt76/mt7925 -DKBUILD_MODNAME='"lkpi"' -o "$o" "$f" || { echo "FAIL $f"; fail=1; }
done
[ $fail -eq 0 ] || exit 1

nm -u "$OBJ"/*.o | awk '$1 == "U" { print $2 }' | sort -u > "$OBJ/undef.txt"
nm --defined-only "$OBJ"/*.o | awk 'NF == 3 { print $3 }' | sort -u > "$OBJ/def.txt"
comm -23 "$OBJ/undef.txt" "$OBJ/def.txt" > "$OBJ/need.txt"

# Symbols the OS provides (libkern on macOS, libc here) or lkpi/os.h declares.
grep -oE "\blkpi_os_[a-z0-9_]+" src/linuxkpi/include/lkpi/os.h | sort -u > "$OBJ/os.txt"
printf '%s\n' memcpy memset memmove memcmp strlen strcmp strncmp strcpy strncpy \
	strlcpy strlcat strchr strrchr strstr strnlen strcasecmp strncasecmp \
	snprintf vsnprintf sprintf vsprintf printf vprintf sscanf panic bcmp bzero \
	__stack_chk_fail __stack_chk_guard __divdi3 __udivdi3 __moddi3 __umoddi3 \
	__popcountdi2 __ffsdi2 __assert_fail __memcpy_chk __memset_chk abort \
	> "$OBJ/libc.txt"
sort -u "$OBJ/os.txt" "$OBJ/libc.txt" > "$OBJ/known.txt"
comm -23 "$OBJ/need.txt" "$OBJ/known.txt" > "$OBJ/missing.txt"

echo "objects: $(ls "$OBJ"/*.o | wc -l), undefined after linking the set: $(wc -l < "$OBJ/need.txt")"
echo "provided by lkpi/os.h or libkern: $(comm -12 "$OBJ/need.txt" "$OBJ/known.txt" | wc -l)"
if [ "${MAC80211_STUB:-0}" = 1 ]; then
	echo "still missing ($(wc -l < "$OBJ/missing.txt")):"
else
	echo "expected from the 802.11 stack, verified by 'make link-check-full' ($(wc -l < "$OBJ/missing.txt")):"
fi
echo "  802.11 glue: $(grep -cE '^(ieee80211_|cfg80211_|wiphy_|linuxkpi_|lkpi_80211|regulatory_|__ieee80211|rate_)' "$OBJ/missing.txt")"
grep -E '^(ieee80211_|cfg80211_|wiphy_|linuxkpi_|lkpi_80211|regulatory_|__ieee80211|rate_)' "$OBJ/missing.txt" | sed 's/^/    /'
echo "  other:"
grep -vE '^(ieee80211_|cfg80211_|wiphy_|linuxkpi_|lkpi_80211|regulatory_|__ieee80211|rate_)' "$OBJ/missing.txt" | sed 's/^/    /'
if [ "${MAC80211_STUB:-0}" = 1 ]; then
	[ ! -s "$OBJ/missing.txt" ]
fi
