#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Link-level proof for the 802.11 stack: compile the net80211 station-mode
# build set, the bsdkpi shim and the vendored AES to host objects, compile
# the C half of LinuxKPI next to them, and print every symbol that nothing
# in that set, lkpi/os.h or libc/libkern defines.  What remains is exactly
# what the linux_80211.c glue / IO80211 layer still has to provide.
#
# Works on any x86-64/arm64 Linux or macOS host with clang; needs no SDK.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OBJ=build/linkcheck-net80211
rm -rf "$OBJ"
mkdir -p "$OBJ"

CFLAGS=$(make -s print-NET80211_CFLAGS)
LKPI_CFLAGS=$(make -s print-LKPI_CFLAGS)
N80211_DIR=$(make -s print-N80211_DIR)
NET80211_SRCS=$(make -s print-NET80211_SRCS)
EXTRA_SRCS="$(make -s print-NET80211_EXTRA_SRCS) $(make -s print-BSDKPI_SRCS)"
LKPI_SRCS=$(make -s print-LKPI_SRCS)

fail=0
for f in $NET80211_SRCS; do
	# shellcheck disable=SC2086
	clang -c -O1 $CFLAGS -DKBUILD_MODNAME='"net80211"' \
	    -o "$OBJ/n80211_$(basename "$f" .c).o" "$N80211_DIR/$f" || { echo "FAIL $f"; fail=1; }
done
for f in $EXTRA_SRCS; do
	# shellcheck disable=SC2086
	clang -c -O1 $CFLAGS -DKBUILD_MODNAME='"bsdkpi"' \
	    -o "$OBJ/bsdkpi_$(echo "$f" | tr / _ | sed 's/\.c$//').o" "$f" || { echo "FAIL $f"; fail=1; }
done
[ $fail -eq 0 ] || exit 1

# LinuxKPI C runtime: only its definitions matter here (its own undefined
# symbols are scripts/link-check.sh's subject).
mkdir -p "$OBJ/lkpi"
for f in $LKPI_SRCS; do
	# shellcheck disable=SC2086
	clang -c -O1 -w $LKPI_CFLAGS -Isrc/mt76/mt7925 -DKBUILD_MODNAME='"lkpi"' \
	    -o "$OBJ/lkpi/$(basename "$f" .c).o" "$f" || { echo "FAIL $f"; exit 1; }
done

nm -u "$OBJ"/*.o | awk '$1 == "U" { print $2 }' | sort -u > "$OBJ/undef.txt"
nm --defined-only "$OBJ"/*.o | awk 'NF == 3 { print $3 }' | sort -u > "$OBJ/def.txt"
nm --defined-only "$OBJ"/lkpi/*.o | awk 'NF == 3 { print $3 }' | sort -u > "$OBJ/lkpidef.txt"
comm -23 "$OBJ/undef.txt" "$OBJ/def.txt" > "$OBJ/need.txt"

# Strong symbols defined twice inside net80211 + bsdkpi, or by both that
# set and LinuxKPI, would be link errors.
nm --defined-only "$OBJ"/*.o "$OBJ"/lkpi/*.o | \
    awk 'NF == 3 && $2 ~ /^[TDBR]$/ { print $3 }' | sort | uniq -d > "$OBJ/dup.txt"

grep -oE "\blkpi_os_[a-z0-9_]+" src/linuxkpi/include/lkpi/os.h | sort -u > "$OBJ/os.txt"
printf '%s\n' memcpy memset memmove memcmp strlen strcmp strncmp strcpy strncpy \
	strlcpy strlcat strchr strrchr strstr strnlen strcasecmp strncasecmp \
	snprintf vsnprintf sprintf vsprintf printf vprintf sscanf panic bcmp bzero \
	bcopy __stack_chk_fail __stack_chk_guard __divdi3 __udivdi3 __moddi3 \
	__umoddi3 __popcountdi2 __ffsdi2 __assert_fail __memcpy_chk __memset_chk \
	abort | sort -u > "$OBJ/libc.txt"
sort -u "$OBJ/os.txt" "$OBJ/libc.txt" "$OBJ/lkpidef.txt" > "$OBJ/known.txt"
comm -23 "$OBJ/need.txt" "$OBJ/known.txt" > "$OBJ/missing.txt"

echo "objects: $(ls "$OBJ"/*.o | wc -l) (net80211 + bsdkpi + rijndael), undefined after linking the set: $(wc -l < "$OBJ/need.txt")"
echo "provided by src/linuxkpi/src: $(comm -12 "$OBJ/need.txt" "$OBJ/lkpidef.txt" | wc -l)"
echo "provided by lkpi/os.h or libkern: $(comm -12 "$OBJ/need.txt" "$OBJ/os.txt" | wc -l) + $(comm -12 "$OBJ/need.txt" "$OBJ/libc.txt" | wc -l)"
echo "duplicate strong symbols ($(wc -l < "$OBJ/dup.txt")):"
sed 's/^/    /' "$OBJ/dup.txt"
echo "still missing ($(wc -l < "$OBJ/missing.txt")):"
sed 's/^/    /' "$OBJ/missing.txt"
[ ! -s "$OBJ/dup.txt" ]
