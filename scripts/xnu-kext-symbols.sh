#!/bin/sh
# SPDX-License-Identifier: MIT
#
# xnu-kext-symbols.sh KPI_DIR INFO_PLIST OBJECT...
#
# Link-level check of a kext without Apple's linker: every symbol the objects
# leave undefined (after resolving them against each other) must be exported
# by a bundle listed in OSBundleLibraries of INFO_PLIST, or come from
# libkmod.a (which the kext links with -lkmod -lkmodc++).
#
# KPI_DIR holds one <bundle-identifier>.exports per bundle, as written by
# scripts/xnu-kernel-headers.sh into <tree>/KPI/<arch>: xnu's
# config/<Set>.exports + <Set>.<arch>.exports + <Set>.<arch>.MacOSX.exports
# (the symbol sets the kernel exports under com.apple.kpi.*), IOPCIFamily's
# config/IOPCIFamily.exports, and libkmod.exports.  Entries ending in `*` are
# prefixes; `name:alias` exports `name`.
#
# Reports, per declared bundle, what it resolves; then every symbol nothing
# declared resolves, with the objects that reference it and any undeclared
# bundle that would (com.apple.kpi.private is not available to third-party
# kexts), and any symbol two objects define strongly (ld would refuse).
# Exits 1 if anything is unresolved or duplicated.  Needs llvm-nm (or a Mach-O
# capable nm: NM=...) and python3.  V=1 lists every resolved symbol.
#
# What this cannot prove: that the kernel version the kext runs on still
# exports the symbol (the lists are those of the given xnu release), symbol
# versioning against OSBundleLibraries' version numbers, and anything the
# real linker (ld -kext) or kmutil would check beyond names.
set -eu
if [ $# -lt 3 ]; then
	echo "usage: $0 KPI_DIR INFO_PLIST OBJECT..." >&2
	exit 2
fi
KPI=$1
PLIST=$2
shift 2
[ -d "$KPI" ] || { echo "$0: $KPI: no such directory" >&2; exit 2; }
[ -f "$PLIST" ] || { echo "$0: $PLIST: no such file" >&2; exit 2; }

if [ -z "${NM:-}" ]; then
	for n in llvm-nm llvm-nm-20 llvm-nm-19 llvm-nm-18 llvm-nm-17; do
		if command -v "$n" >/dev/null 2>&1; then NM=$n; break; fi
	done
	if [ -z "${NM:-}" ] && [ "$(uname -s)" = Darwin ]; then NM=nm; fi
fi
[ -n "${NM:-}" ] || { echo "$0: llvm-nm not found (set NM=)" >&2; exit 2; }

TMPD=$(mktemp -d "${TMPDIR:-/tmp}/xnu-kext-symbols.XXXXXX")
trap 'rm -rf "$TMPD"' EXIT INT TERM
# "<object>: <type> <name>" for undefined and for defined external symbols.
"$NM" -A -u "$@" > "$TMPD/undef"
"$NM" -A -g --defined-only "$@" > "$TMPD/def"

cat > "$TMPD/resolve.py" <<'PY'
import os, re, sys
kpi, plist, undef_f, def_f, verbose = sys.argv[1:6]

with open(plist, encoding='utf-8', errors='replace') as f:
    text = f.read()
m = re.search(r'<key>OSBundleLibraries</key>\s*<dict>(.*?)</dict>', text, re.S)
if not m:
    sys.exit('%s: no OSBundleLibraries' % plist)
body = re.sub(r'<!--.*?-->', '', m.group(1), flags=re.S)
declared = re.findall(r'<key>([^<]+)</key>\s*<string>([^<]*)</string>', body)

def load(path):
    exact, prefixes = set(), []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            name = line.split(':', 1)[0]
            if name.endswith('*'):
                prefixes.append(name[:-1])
            else:
                exact.add(name)
    return exact, prefixes

sets = {}
for fn in sorted(os.listdir(kpi)):
    if fn.endswith('.exports'):
        sets[fn[:-len('.exports')]] = load(os.path.join(kpi, fn))

def exports(bundle, sym):
    exact, prefixes = sets[bundle]
    return sym in exact or any(sym.startswith(p) for p in prefixes)

defined = set()
strong = {}
with open(def_f) as f:
    for line in f:
        obj, _, rest = line.partition(': ')
        parts = rest.split()
        if len(parts) >= 2:
            defined.add(parts[-1])
            # T/D/B/S/R: strong external definitions (V/W: weak, C: common)
            if parts[-2] in ('T', 'D', 'B', 'S', 'R'):
                strong.setdefault(parts[-1], set()).add(os.path.basename(obj))
dups = sorted(s for s, objs in strong.items() if len(objs) > 1)
refs = {}
with open(undef_f) as f:
    for line in f:
        obj, _, rest = line.partition(': ')
        parts = rest.split()
        if not parts:
            continue
        refs.setdefault(parts[-1], set()).add(os.path.basename(obj))
needed = sorted(s for s in refs if s not in defined)
# ld64 defines section$start$SEG$sect / section$end$... and segment$start$SEG /
# segment$end$SEG itself for whatever the image contains.
synthesised = [s for s in needed if re.match(r'(section|segment)\$(start|end)\$', s)]
needed = [s for s in needed if s not in synthesised]

usable = [b for b, _ in declared if b in sets]
missing_sets = [b for b, _ in declared if b not in sets]
if 'libkmod' in sets:
    usable.append('libkmod')
by_bundle = {b: [] for b in usable}
unresolved = []
for s in needed:
    hit = next((b for b in usable if exports(b, s)), None)
    if hit:
        by_bundle[hit].append(s)
    else:
        unresolved.append(s)

print('externals: %d symbols referenced and not defined by the %s' % (len(needed), 'objects'))
for b in usable:
    ver = dict(declared).get(b, '(linked: -lkmod)')
    print('  %-32s %-8s resolves %d' % (b, ver, len(by_bundle[b])))
    if verbose == '1':
        for s in by_bundle[b]:
            print('      ' + s)
for b in missing_sets:
    print('  %-32s          no exports list in %s (not checked)' % (b, kpi))
if synthesised:
    print('  %-32s          provides %d (%s)' % ('ld64 (section bounds)', len(synthesised), ', '.join(synthesised)))
unused = [b for b in usable if not by_bundle[b] and b != 'libkmod']
if unused:
    print('declared but resolving nothing: %s' % ', '.join(unused))
if unresolved:
    print('UNRESOLVED (%d):' % len(unresolved))
    for s in unresolved:
        elsewhere = [b for b in sets if b not in usable and exports(b, s)]
        where = ('; exported only by ' + ', '.join(elsewhere)) if elsewhere else '; exported by no KPI bundle'
        print('  %-40s referenced by %s%s' % (s, ', '.join(sorted(refs[s])[:4]) + (' ...' if len(refs[s]) > 4 else ''), where))
if dups:
    print('DUPLICATE strong definitions (%d):' % len(dups))
    for s in dups:
        print('  %-40s %s' % (s, ', '.join(sorted(strong[s]))))
if unresolved or dups:
    sys.exit(1)
print('all externals resolved by OSBundleLibraries + libkmod; no duplicate definitions')
PY
python3 -I "$TMPD/resolve.py" "$KPI" "$PLIST" "$TMPD/undef" "$TMPD/def" "${V:-0}"
