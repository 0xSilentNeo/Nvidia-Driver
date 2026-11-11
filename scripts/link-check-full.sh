#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Link-level proof for the complete C program of the kext: the vendored mt76
# driver, the LinuxKPI C layer (without the phase-1 mac80211_stub.c), bsdkpi,
# the vendored AES, the net80211 station-mode build set and FreeBSD's
# mac80211 -> net80211 glue (src/lkpi80211).  Everything is compiled to host
# objects; the script fails if any symbol is undefined beyond what lkpi/os.h
# declares (the IOKit half implements it) and libc/libkern, or if two
# objects define the same strong symbol.
#
# Works on any x86-64/arm64 Linux or macOS host with clang; needs no SDK.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OBJ=build/linkcheck-full
rm -rf "$OBJ"
mkdir -p "$OBJ"

M="make -s MAC80211_STUB=0"
LKPI_CFLAGS=$($M print-LKPI_CFLAGS)
N_CFLAGS=$($M print-NET80211_CFLAGS)
G_CFLAGS=$($M print-LKPI80211_CFLAGS)
N80211_DIR=$($M print-N80211_DIR)

fail=0
cc() {	# cc <object> <modname> <flags...> <source>
	o=$1; mod=$2; shift 2
	clang -c -O1 "$@" -DKBUILD_MODNAME="\"$mod\"" -o "$OBJ/$o" || { echo "FAIL $o"; fail=1; }
}
# shellcheck disable=SC2086
{
for f in $($M print-MT76_SRCS); do
	cc "mt76_$(echo "$f" | tr / _ | sed 's/\.c$//').o" mt76 $LKPI_CFLAGS "src/mt76/$f"
done
for f in $($M print-LKPI_SRCS); do
	cc "lkpi_$(basename "$f" .c).o" lkpi $LKPI_CFLAGS -Isrc/mt76/mt7925 "$f"
done
for f in $($M print-NET80211_SRCS); do
	cc "n80211_$(basename "$f" .c).o" net80211 $N_CFLAGS "$N80211_DIR/$f"
done
for f in $($M print-NET80211_EXTRA_SRCS) $($M print-BSDKPI_SRCS); do
	cc "bsdkpi_$(echo "$f" | tr / _ | sed 's/\.c$//').o" bsdkpi $N_CFLAGS "$f"
done
for f in $($M print-LKPI80211_SRCS); do
	cc "lkpi80211_$(basename "$f" .c).o" lkpi80211 $G_CFLAGS "$f"
done
}
[ $fail -eq 0 ] || exit 1

nm -u "$OBJ"/*.o | awk '$1 == "U" { print $2 }' | sort -u > "$OBJ/undef.txt"
nm --defined-only "$OBJ"/*.o | awk 'NF == 3 { print $3 }' | sort -u > "$OBJ/def.txt"
comm -23 "$OBJ/undef.txt" "$OBJ/def.txt" > "$OBJ/need.txt"
nm --defined-only "$OBJ"/*.o | \
    awk 'NF == 3 && $2 ~ /^[TDBR]$/ { print $3 }' | sort | uniq -d > "$OBJ/dup.txt"

grep -oE "\blkpi_os_[a-z0-9_]+" src/linuxkpi/include/lkpi/os.h | sort -u > "$OBJ/os.txt"
printf '%s\n' memcpy memset memmove memcmp strlen strcmp strncmp strcpy strncpy \
	strlcpy strlcat strchr strrchr strstr strnlen strcasecmp strncasecmp \
	snprintf vsnprintf sprintf vsprintf printf vprintf sscanf panic bcmp bzero \
	bcopy __stack_chk_fail __stack_chk_guard __divdi3 __udivdi3 __moddi3 \
	__umoddi3 __popcountdi2 __ffsdi2 __assert_fail __memcpy_chk __memset_chk \
	abort | sort -u > "$OBJ/libc.txt"
sort -u "$OBJ/os.txt" "$OBJ/libc.txt" > "$OBJ/known.txt"
comm -23 "$OBJ/need.txt" "$OBJ/known.txt" > "$OBJ/missing.txt"

echo "objects: $(ls "$OBJ"/*.o | wc -l) (mt76 $(ls "$OBJ"/mt76_*.o | wc -l), linuxkpi $(ls "$OBJ"/lkpi_*.o | wc -l), net80211 $(ls "$OBJ"/n80211_*.o | wc -l), bsdkpi+aes $(ls "$OBJ"/bsdkpi_*.o | wc -l), lkpi80211 $(ls "$OBJ"/lkpi80211_*.o | wc -l))"
echo "undefined after linking the set: $(wc -l < "$OBJ/need.txt")"
echo "provided by lkpi/os.h or libkern: $(comm -12 "$OBJ/need.txt" "$OBJ/os.txt" | wc -l) + $(comm -12 "$OBJ/need.txt" "$OBJ/libc.txt" | wc -l)"
echo "duplicate strong symbols ($(wc -l < "$OBJ/dup.txt")):"
sed 's/^/    /' "$OBJ/dup.txt"
echo "still missing ($(wc -l < "$OBJ/missing.txt")):"
sed 's/^/    /' "$OBJ/missing.txt"
[ ! -s "$OBJ/dup.txt" ] && [ ! -s "$OBJ/missing.txt" ]
