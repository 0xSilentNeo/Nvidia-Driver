#!/bin/sh
# SPDX-License-Identifier: MIT
#
# xnu-kernel-headers.sh [-a "x86_64 arm64"] [-B] XNU_DIR IOPCIFAMILY_DIR OUT_DIR
#
# Assembles a stand-in for Kernel.framework/Headers from Apple's open-source
# xnu and IOPCIFamily trees, so that the kext (src/kext) can be compiled with
# -fsyntax-only on a machine without macOS or Xcode:
#
#   make -C src/kext check-xnu XNU_HDRS=OUT_DIR
#
# What it does, following Apple's own installhdrs rules (xnu/makedefs):
#
#  * walks the xnu Makefile tree the way MakeInc.dir does: machine-independent
#    headers through INSTINC_SUBDIRS, machine-dependent ones through
#    INSTINC_SUBDIRS_<ARCH> (one walk per arch in -a; the SDK is universal);
#  * in each directory takes INSTALL_KF_MI_LIST / INSTALL_KF_MD_LIST (falling
#    back to EXPORT_MI_LIST / EXPORT_MD_LIST exactly like MakeInc.rule's
#    `ifndef`) and installs them under EXPORT_MI_DIR / EXPORT_MD_DIR, which is
#    what merges bsd/sys + osfmk pieces into sys/, bsd/machine + osfmk/machine
#    into machine/, EXTERNAL_HEADERS and osfmk/libsa into the top level, ...;
#  * filters every header through `unifdef $(KINCFRAME_UNIFDEF)` (the flags are
#    read from makedefs/MakeInc.def: -UKERNEL_PRIVATE -UPRIVATE -DKERNEL
#    -UXNU_KERNEL_PRIVATE ... -DXNU_PLATFORM_MacOSX ...) and drops headers that
#    are empty afterwards, as the INSTALLHDRS_RULE_template does.  The real
#    unifdef is used when it is on PATH (Xcode command line tools ship it);
#    otherwise a built-in equivalent (-B forces it) evaluates the directives
#    with the same three-valued logic: known symbols are folded, directives
#    that still depend on unknown symbols are kept with the known symbols
#    replaced by their constant values;
#  * adds IOPCIFamily's Public headers (ATTRIBUTES = (Public, ) in its Xcode
#    project) as IOKit/pci/*.h, copied verbatim like Xcode's header phase;
#  * headers that xnu generates at build time (INSTALL_KF_*_GEN_LIST: MIG
#    output such as mach/thread_act.h, libkern/version.h, ...) cannot be
#    produced without MIG or the xnu build.  The few the kext needs get a
#    HAND-WRITTEN STAND-IN below (only the routines actually used, copied from
#    the .defs); every other one gets a placeholder that fails with #error so
#    that a new dependency is noticed instead of silently missing.
#
# Nothing from the checkouts is executed: Makefiles are evaluated by a small
# GNU make interpreter that never runs $(shell ...), and no git command is run
# in them.  OUT_DIR gets a MANIFEST listing every header, its source, the
# unifdef flags and the stand-ins.
#
# Limits (also printed in MANIFEST): no iig, so PCIDriverKit/*.h and
# DriverKit/*.h (generated from .iig) are absent; IOPCIDevice.h copes through
# __has_include.  The tree matches the xnu release given, not a particular
# SDK; newer xnu headers carry API_AVAILABLE() markers for later releases.

set -eu

ARCHS="x86_64 arm64"
FORCE_BUILTIN=0
while getopts "a:B" opt; do
	case $opt in
	a) ARCHS=$OPTARG ;;
	B) FORCE_BUILTIN=1 ;;
	*) echo "usage: $0 [-a archs] [-B] XNU_DIR IOPCIFAMILY_DIR OUT_DIR" >&2; exit 2 ;;
	esac
done
shift $((OPTIND - 1))
if [ $# -ne 3 ]; then
	echo "usage: $0 [-a archs] [-B] XNU_DIR IOPCIFAMILY_DIR OUT_DIR" >&2
	exit 2
fi
XNU=$1
IOPCI=$2
OUT=$3
[ -f "$XNU/makedefs/MakeInc.def" ] || { echo "$0: $XNU is not an xnu checkout (no makedefs/MakeInc.def)" >&2; exit 1; }
[ -d "$IOPCI/IOKit/pci" ] || { echo "$0: $IOPCI is not an IOPCIFamily checkout (no IOKit/pci)" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "$0: python3 is required" >&2; exit 1; }

UNIFDEF_BIN=
if [ "$FORCE_BUILTIN" = 0 ] && command -v unifdef >/dev/null 2>&1; then
	UNIFDEF_BIN=$(command -v unifdef)
fi

# A scratch directory for the program text: python must not be started with
# the untrusted checkout as its current or script directory.
TMPD=$(mktemp -d "${TMPDIR:-/tmp}/xnu-kernel-headers.XXXXXX")
trap 'rm -rf "$TMPD"' EXIT INT TERM
cat > "$TMPD/xnu_kernel_headers.py" <<'PY'
import os, re, sys, shutil, subprocess, glob

XNU, IOPCI, OUT, ARCHS, UNIFDEF_BIN = sys.argv[1:6]
XNU = os.path.abspath(XNU)
IOPCI = os.path.abspath(IOPCI)
OUT = os.path.abspath(OUT)
ARCHS = ARCHS.split()
ARCH_CONFIG = {'x86_64': 'X86_64', 'x86_64h': 'X86_64H', 'arm64': 'ARM64', 'arm64e': 'ARM64'}
warnings = []

def warn(msg):
    warnings.append(msg)

# ===================================================================
# A small GNU make evaluator: assignments (= := ::= ?= +=), conditionals
# (ifeq/ifneq/ifdef/ifndef/else/endif), define/endef, rules and recipes
# (skipped), and the functions xnu's header Makefiles use.  $(shell) is
# never run.
# ===================================================================

def words(s):
    return s.split()

def find_close(s, i):
    """s[i] is '(' or '{'; return the index of its matching closer
    (make counts nesting of the same delimiter only)."""
    op = s[i]
    cl = ')' if op == '(' else '}'
    depth = 0
    j = i
    while j < len(s):
        if s[j] == op:
            depth += 1
        elif s[j] == cl:
            depth -= 1
            if depth == 0:
                return j
        j += 1
    return len(s)

def split_args(s, maxsplit=-1):
    """Split function arguments at top-level commas."""
    out, depth, cur = [], 0, []
    for ch in s:
        if ch in '({':
            depth += 1
        elif ch in ')}':
            depth -= 1
        if ch == ',' and depth == 0 and (maxsplit < 0 or len(out) < maxsplit):
            out.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
    out.append(''.join(cur))
    return out

def pat_match(pat, word):
    if '%' not in pat:
        return pat == word
    pre, post = pat.split('%', 1)
    return len(word) >= len(pre) + len(post) and word.startswith(pre) and word.endswith(post)

def pat_subst(pat, rep, word):
    if '%' not in pat:
        return rep if word == pat else word
    pre, post = pat.split('%', 1)
    if len(word) >= len(pre) + len(post) and word.startswith(pre) and word.endswith(post):
        stem = word[len(pre):len(word) - len(post)] if post else word[len(pre):]
        return rep.replace('%', stem, 1) if '%' in rep else rep
    return word

class Make:
    def __init__(self, base, where):
        self.vars = {}          # name -> [flavor, text]
        self.cmdline = set(base)
        for k, v in base.items():
            self.vars[k] = ['simple', v]
        self.where = where

    def raw(self, name):
        v = self.vars.get(name)
        return v[1] if v else ''

    def value(self, name):
        v = self.vars.get(name)
        if not v:
            return ''
        return self.expand(v[1]) if v[0] == 'recursive' else v[1]

    def expand(self, s):
        out, i, n = [], 0, len(s)
        while i < n:
            c = s[i]
            if c != '$':
                out.append(c)
                i += 1
                continue
            if i + 1 >= n:
                i += 1
                continue
            d = s[i + 1]
            if d == '$':
                out.append('$')
                i += 2
            elif d in '({':
                j = find_close(s, i + 1)
                out.append(self.ref(s[i + 2:j]))
                i = j + 1
            else:
                out.append(self.value(d))
                i += 2
        return ''.join(out)

    FUNCS = ('sort', 'filter', 'filter-out', 'notdir', 'dir', 'basename', 'suffix',
             'addprefix', 'addsuffix', 'wildcard', 'patsubst', 'subst', 'strip',
             'findstring', 'firstword', 'lastword', 'words', 'word', 'foreach',
             'if', 'or', 'and', 'call', 'shell', 'error', 'warning', 'info',
             'origin', 'value', 'eval', 'abspath', 'realpath', 'join', 'flavor')

    def ref(self, inner):
        m = re.match(r'([a-z-]+)[ \t]+(.*)$', inner, re.S)
        if m and m.group(1) in self.FUNCS:
            return self.func(m.group(1), m.group(2))
        # substitution reference $(VAR:pat=rep)
        depth = 0
        for k, ch in enumerate(inner):
            if ch in '({':
                depth += 1
            elif ch in ')}':
                depth -= 1
            elif ch == ':' and depth == 0 and '=' in inner[k:]:
                name = self.expand(inner[:k]).strip()
                pat, rep = inner[k + 1:].split('=', 1)
                pat, rep = self.expand(pat), self.expand(rep)
                if '%' not in pat:
                    pat, rep = '%' + pat, '%' + rep
                return ' '.join(pat_subst(pat, rep, w) for w in words(self.value(name)))
        return self.value(self.expand(inner).strip())

    def func(self, name, argtext):
        if name in ('foreach',):
            a = split_args(argtext, 2)
            var = self.expand(a[0]).strip()
            lst = words(self.expand(a[1]))
            saved = self.vars.get(var)
            res = []
            for w in lst:
                self.vars[var] = ['simple', w]
                res.append(self.expand(a[2]))
            if saved is None:
                self.vars.pop(var, None)
            else:
                self.vars[var] = saved
            return ' '.join(res)
        if name == 'if':
            a = split_args(argtext, 2)
            if self.expand(a[0]).strip():
                return self.expand(a[1]) if len(a) > 1 else ''
            return self.expand(a[2]) if len(a) > 2 else ''
        if name == 'or':
            for a in split_args(argtext):
                v = self.expand(a).strip()
                if v:
                    return v
            return ''
        if name == 'and':
            v = ''
            for a in split_args(argtext):
                v = self.expand(a).strip()
                if not v:
                    return ''
            return v
        if name == 'call':
            a = split_args(argtext)
            fn = self.expand(a[0]).strip()
            saved = {}
            for k, arg in enumerate(a[1:], 1):
                saved[str(k)] = self.vars.get(str(k))
                self.vars[str(k)] = ['simple', self.expand(arg)]
            body = self.raw(fn)
            res = self.expand(body)
            for k, v in saved.items():
                if v is None:
                    self.vars.pop(k, None)
                else:
                    self.vars[k] = v
            return res
        if name == 'value':
            return self.raw(self.expand(argtext).strip())
        if name == 'origin':
            return 'file' if self.expand(argtext).strip() in self.vars else 'undefined'
        if name == 'flavor':
            v = self.vars.get(self.expand(argtext).strip())
            return v[0] if v else 'undefined'
        if name == 'eval':
            return ''
        if name in ('error', 'warning', 'info'):
            return ''
        if name == 'shell':
            cmd = self.expand(argtext)
            m = re.match(r'\s*\(\s*cd\s+(\S+)\s*;\s*echo\s+\*\.h\s*\)\s*$', cmd)
            if m:   # iokit/System: $(shell (cd $(SOURCE); echo *.h))
                return ' '.join(sorted(os.path.basename(p) for p in glob.glob(os.path.join(m.group(1), '*.h'))))
            warn('%s: $(shell %s) not run (treated as empty)' % (self.where, cmd.strip()))
            return ''
        a = [self.expand(x) for x in split_args(argtext)]
        if name == 'sort':
            return ' '.join(sorted(set(words(a[0]))))
        if name == 'filter':
            pats = words(a[0])
            return ' '.join(w for w in words(a[1]) if any(pat_match(p, w) for p in pats))
        if name == 'filter-out':
            pats = words(a[0])
            return ' '.join(w for w in words(a[1]) if not any(pat_match(p, w) for p in pats))
        if name == 'notdir':
            return ' '.join(w.rsplit('/', 1)[-1] for w in words(a[0]))
        if name == 'dir':
            return ' '.join((w.rsplit('/', 1)[0] + '/') if '/' in w else './' for w in words(a[0]))
        if name == 'basename':
            return ' '.join(re.sub(r'\.[^./]*$', '', w) for w in words(a[0]))
        if name == 'suffix':
            return ' '.join(m.group(0) for m in (re.search(r'\.[^./]*$', w) for w in words(a[0])) if m)
        if name == 'addprefix':
            return ' '.join(a[0].strip() + w for w in words(a[1]))
        if name == 'addsuffix':
            return ' '.join(w + a[0].strip() for w in words(a[1]))
        if name == 'wildcard':
            res = []
            for p in words(a[0]):
                res.extend(sorted(glob.glob(p)))
            return ' '.join(res)
        if name == 'patsubst':
            return ' '.join(pat_subst(a[0].strip(), a[1].strip(), w) for w in words(a[2]))
        if name == 'subst':
            return a[2].replace(a[0], a[1])
        if name == 'strip':
            return ' '.join(words(a[0]))
        if name == 'findstring':
            return a[0] if a[0] in a[1] else ''
        if name == 'firstword':
            w = words(a[0])
            return w[0] if w else ''
        if name == 'lastword':
            w = words(a[0])
            return w[-1] if w else ''
        if name == 'words':
            return str(len(words(a[0])))
        if name == 'word':
            w = words(a[1])
            k = int(a[0].strip() or 0)
            return w[k - 1] if 0 < k <= len(w) else ''
        if name in ('abspath', 'realpath'):
            return ' '.join(os.path.abspath(w) for w in words(a[0]))
        if name == 'join':
            x, y = words(a[0]), words(a[1])
            return ' '.join((x[k] if k < len(x) else '') + (y[k] if k < len(y) else '') for k in range(max(len(x), len(y))))
        return ''

    # ---- statements ----
    def logical_lines(self, text):
        out, cur = [], None
        for line in text.split('\n'):
            if cur is not None:
                cur = cur + ' ' + line.lstrip()
            else:
                cur = line
            if cur.endswith('\\') and not cur.endswith('\\\\'):
                cur = cur[:-1].rstrip()
                continue
            out.append(cur)
            cur = None
        if cur is not None:
            out.append(cur)
        return out

    @staticmethod
    def strip_comment(line):
        res, i = [], 0
        while i < len(line):
            ch = line[i]
            if ch == '\\' and i + 1 < len(line) and line[i + 1] == '#':
                res.append('#')
                i += 2
                continue
            if ch == '#':
                break
            res.append(ch)
            i += 1
        return ''.join(res)

    def cond_value(self, kw, arg):
        arg = arg.strip()
        if kw in ('ifdef', 'ifndef'):
            name = self.expand(arg).strip()
            v = self.vars.get(name)
            defined = bool(v and v[1].strip() != '')
            return defined if kw == 'ifdef' else not defined
        if arg.startswith('('):
            j = find_close(arg, 0)
            parts = split_args(arg[1:j], 1)
            if len(parts) != 2:
                parts.append('')
            a, b = parts
        else:
            m = re.match(r'''(["'])(.*?)\1\s+(["'])(.*?)\3''', arg)
            if not m:
                warn('%s: cannot parse conditional "%s %s"' % (self.where, kw, arg))
                return False
            a, b = m.group(2), m.group(4)
        eq = self.expand(a).strip() == self.expand(b).strip()
        return eq if kw == 'ifeq' else not eq

    def classify(self, s):
        depth, i = 0, 0
        while i < len(s):
            ch = s[i]
            if ch == '$' and i + 1 < len(s) and s[i + 1] in '({':
                i = find_close(s, i + 1) + 1
                continue
            if ch == '=':
                op = '='
                if i > 0 and s[i - 1] in '+?!':
                    op = s[i - 1] + '='
                    name = s[:i - 1]
                else:
                    name = s[:i]
                return ('assign', name.strip(), op, s[i + 1:].lstrip())
            if ch == ':':
                if s[i + 1:i + 2] == '=':
                    return ('assign', s[:i].strip(), ':=', s[i + 2:].lstrip())
                if s[i + 1:i + 3] == ':=':
                    return ('assign', s[:i].strip(), ':=', s[i + 3:].lstrip())
                return ('rule',)
            i += 1
        return ('other',)

    def assign(self, name, op, val):
        name = self.expand(name).strip()
        if not name or name in self.cmdline:
            return
        cur = self.vars.get(name)
        if op == '=':
            self.vars[name] = ['recursive', val]
        elif op == ':=':
            self.vars[name] = ['simple', self.expand(val)]
        elif op == '?=':
            if cur is None:
                self.vars[name] = ['recursive', val]
        elif op == '+=':
            if cur is None:
                self.vars[name] = ['recursive', val]
            elif cur[0] == 'recursive':
                cur[1] = (cur[1] + ' ' + val) if cur[1] else val
            else:
                cur[1] = (cur[1] + ' ' + self.expand(val)) if cur[1] else self.expand(val)
        elif op == '!=':
            warn('%s: "%s != ..." (shell assignment) not run' % (self.where, name))

    def parse_text(self, text):
        cond = []           # [branch_active, branch_taken, parent_active]
        in_define = None
        define_body = []
        in_rule = False
        for raw in self.logical_lines(text):
            if in_define is not None:
                if re.match(r'^\s*endef\b', raw):
                    if all(c[0] for c in cond):
                        self.vars[in_define] = ['recursive', '\n'.join(define_body)]
                    in_define = None
                    define_body = []
                else:
                    define_body.append(raw)
                continue
            if raw.startswith('\t') and in_rule:
                continue
            s = self.strip_comment(raw).strip()
            if not s:
                continue
            m = re.match(r'(ifeq|ifneq|ifdef|ifndef)\b(.*)$', s)
            if m:
                parent = all(c[0] for c in cond)
                v = parent and self.cond_value(m.group(1), m.group(2))
                cond.append([v, v, parent])
                continue
            if re.match(r'else\b', s):
                if not cond:
                    warn('%s: else without if' % self.where)
                    continue
                top = cond[-1]
                rest = s[4:].strip()
                if top[1] or not top[2]:
                    top[0] = False
                else:
                    if rest:
                        m2 = re.match(r'(ifeq|ifneq|ifdef|ifndef)\b(.*)$', rest)
                        v = bool(m2) and self.cond_value(m2.group(1), m2.group(2))
                    else:
                        v = True
                    top[0] = v
                    top[1] = v
                continue
            if re.match(r'endif\b', s):
                if cond:
                    cond.pop()
                continue
            md = re.match(r'(?:(?:export|override)\s+)*define\s+(\S+)', s)
            if md:
                in_define = self.expand(md.group(1))
                continue
            if not all(c[0] for c in cond):
                continue
            if re.match(r'-?(include|sinclude)\b', s) or re.match(r'\$[({](eval|info|warning|error)\b', s):
                in_rule = False
                continue
            s2 = re.sub(r'^((export|override|unexport|private)\s+)+', '', s)
            k = self.classify(s2)
            if k[0] == 'assign':
                in_rule = False
                self.assign(k[1], k[2], k[3])
            elif k[0] == 'rule':
                in_rule = True
            else:
                in_rule = False
        if cond:
            warn('%s: %d unterminated conditional(s)' % (self.where, len(cond)))
        if in_define is not None:
            warn('%s: unterminated define %s' % (self.where, in_define))

    def parse(self, path):
        with open(path, encoding='utf-8', errors='replace') as f:
            self.parse_text(f.read())

# ===================================================================
# Platform and unifdef flags from makedefs (single-line assignments only).
# ===================================================================

def extract_assignments(path, names):
    out = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.match(r'\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*(\?=|\+=|:=|=)', line)
            if m and m.group(1) in names and not line.rstrip().endswith('\\'):
                out.append(line.strip())
    return out

plat = Make({'PLATFORM': 'MacOSX'}, 'makedefs')
plat.parse_text('\n'.join(extract_assignments(os.path.join(XNU, 'makedefs/MakeInc.cmd'),
    {'SUPPORTED_EMBEDDED_PLATFORMS', 'SUPPORTED_SIMULATOR_PLATFORMS', 'SUPPORTED_PLATFORMS'})))
plat.parse_text('\n'.join(extract_assignments(os.path.join(XNU, 'makedefs/MakeInc.def'),
    {'XNU_PRIVATE_UNIFDEF', 'PLATFORM_UNIFDEF', 'KERNEL_ONLY_GUARDS_UNIFDEF',
     'TEST_ONLY_GUARDS_UNIFDEF', 'KINCFRAME_UNIFDEF'})))
UNIFDEF_FLAGS = words(plat.value('KINCFRAME_UNIFDEF'))
if '-DKERNEL' not in UNIFDEF_FLAGS or '-UKERNEL_PRIVATE' not in UNIFDEF_FLAGS:
    warn('KINCFRAME_UNIFDEF not found in makedefs/MakeInc.def; using the xnu-12377 values')
    UNIFDEF_FLAGS = ['-DXNU_PLATFORM_MacOSX', '-UMACH_KERNEL_PRIVATE', '-UBSD_KERNEL_PRIVATE',
                     '-UIOKIT_KERNEL_PRIVATE', '-ULIBKERN_KERNEL_PRIVATE', '-ULIBSA_KERNEL_PRIVATE',
                     '-UPEXPERT_KERNEL_PRIVATE', '-UXNU_KERNEL_PRIVATE', '-UMODULES_SUPPORTED',
                     '-UKERNEL_PRIVATE', '-DKERNEL', '-UPRIVATE', '-UDRIVERKIT', '-UEXCLAVEKIT',
                     '-UEXCLAVECORE', '-D_OPEN_SOURCE_', '-D__OPEN_SOURCE__', '-USCHED_TEST_HARNESS']
SUPPORTED = {k: plat.value(k) for k in ('SUPPORTED_EMBEDDED_PLATFORMS', 'SUPPORTED_SIMULATOR_PLATFORMS', 'SUPPORTED_PLATFORMS')}

# ===================================================================
# Built-in unifdef (used when the real one is not installed).
# ===================================================================

KNOWN = {}
for fl in UNIFDEF_FLAGS:
    if fl.startswith('-D'):
        n, _, v = fl[2:].partition('=')
        KNOWN[n] = v if v else '1'
    elif fl.startswith('-U'):
        KNOWN[fl[2:]] = None

TOK = re.compile(r'\s*(?:(0[xX][0-9a-fA-F]+|\d+)[uUlL]*|([A-Za-z_]\w*)|(&&|\|\||==|!=|<=|>=|<<|>>|[-+*/%<>!~&|^?:(),])|(\S))')

class Unknown(Exception):
    pass

class ExprEval:
    """Three-valued C preprocessor expression evaluation: returns an int, or
    None when the value depends on a symbol unifdef was not told about."""
    def __init__(self, text):
        self.toks = []
        pos = 0
        while pos < len(text):
            m = TOK.match(text, pos)
            if not m or m.end() == pos:
                break
            pos = m.end()
            if m.group(1):
                self.toks.append(('num', int(m.group(1), 0) if not re.match(r'0\d', m.group(1)) else int(m.group(1), 8)))
            elif m.group(2):
                self.toks.append(('id', m.group(2)))
            elif m.group(3):
                self.toks.append(('op', m.group(3)))
            elif m.group(4):
                self.toks.append(('op', m.group(4)))
        self.i = 0

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else ('end', None)

    def take(self):
        t = self.peek()
        self.i += 1
        return t

    def run(self):
        try:
            v = self.cond()
            if self.peek()[0] != 'end':
                return None
            return v
        except (Unknown, IndexError, ValueError, ZeroDivisionError):
            return None

    # precedence climbing; None = unknown
    def cond(self):
        c = self.lor()
        if self.peek() == ('op', '?'):
            self.take()
            a = self.cond()
            if self.take() != ('op', ':'):
                raise Unknown
            b = self.cond()
            if c is None:
                return a if (a is not None and a == b) else None
            return a if c else b
        return c

    def lor(self):
        v = self.land()
        while self.peek() == ('op', '||'):
            self.take()
            w = self.land()
            if v == 1 or (v is not None and v != 0):
                v = 1
            elif w is not None and w != 0:
                v = 1
            elif v is None or w is None:
                v = None
            else:
                v = 0
        return v

    def land(self):
        v = self.binop(0)
        while self.peek() == ('op', '&&'):
            self.take()
            w = self.binop(0)
            if v == 0 or w == 0:
                v = 0
            elif v is None or w is None:
                v = None
            else:
                v = 1
        return v

    LEVELS = [('|',), ('^',), ('&',), ('==', '!='), ('<', '>', '<=', '>='), ('<<', '>>'), ('+', '-'), ('*', '/', '%')]

    def binop(self, lvl):
        if lvl == len(self.LEVELS):
            return self.unary()
        v = self.binop(lvl + 1)
        while self.peek()[0] == 'op' and self.peek()[1] in self.LEVELS[lvl]:
            op = self.take()[1]
            w = self.binop(lvl + 1)
            if v is None or w is None:
                v = None
                continue
            v = {'|': lambda a, b: a | b, '^': lambda a, b: a ^ b, '&': lambda a, b: a & b,
                 '==': lambda a, b: int(a == b), '!=': lambda a, b: int(a != b),
                 '<': lambda a, b: int(a < b), '>': lambda a, b: int(a > b),
                 '<=': lambda a, b: int(a <= b), '>=': lambda a, b: int(a >= b),
                 '<<': lambda a, b: a << b, '>>': lambda a, b: a >> b,
                 '+': lambda a, b: a + b, '-': lambda a, b: a - b, '*': lambda a, b: a * b,
                 '/': lambda a, b: int(a / b), '%': lambda a, b: a % b}[op](v, w)
        return v

    def unary(self):
        t = self.take()
        if t == ('op', '!'):
            v = self.unary()
            return None if v is None else int(not v)
        if t == ('op', '-'):
            v = self.unary()
            return None if v is None else -v
        if t == ('op', '+'):
            return self.unary()
        if t == ('op', '~'):
            v = self.unary()
            return None if v is None else ~v
        if t == ('op', '('):
            v = self.cond()
            if self.take() != ('op', ')'):
                raise Unknown
            return v
        if t[0] == 'num':
            return t[1]
        if t[0] == 'id':
            if t[1] == 'defined':
                paren = self.peek() == ('op', '(')
                if paren:
                    self.take()
                n = self.take()
                if n[0] != 'id':
                    raise Unknown
                if paren and self.take() != ('op', ')'):
                    raise Unknown
                if n[1] in KNOWN:
                    return 0 if KNOWN[n[1]] is None else 1
                return None
            if self.peek() == ('op', '('):     # function-like macro / __has_include
                depth = 0
                while True:
                    x = self.take()
                    if x[0] == 'end':
                        raise Unknown
                    if x == ('op', '('):
                        depth += 1
                    elif x == ('op', ')'):
                        depth -= 1
                        if depth == 0:
                            break
                return None
            if t[1] in KNOWN:
                val = KNOWN[t[1]]
                if val is None:
                    return 0
                try:
                    return int(val, 0)
                except ValueError:
                    return None
            return None
        raise Unknown

def fold_known(expr):
    """Replace known symbols by their constant values in an expression that
    still depends on unknown ones.  This is semantically what unifdef's
    simplification yields: the result no longer depends on whether the
    compiler is given -DKERNEL_PRIVATE etc."""
    def rep_defined(m):
        n = m.group(1) or m.group(2)
        if n in KNOWN:
            return '0' if KNOWN[n] is None else '1'
        return m.group(0)
    e = re.sub(r'\bdefined\s*\(\s*([A-Za-z_]\w*)\s*\)|\bdefined\s+([A-Za-z_]\w*)', rep_defined, expr)
    def bare(m):
        n = m.group(1)
        if n in KNOWN:
            return '0' if KNOWN[n] is None else KNOWN[n]
        return n
    # Leave arguments of defined() and of function-like macros alone.
    parts = re.split(r'(\bdefined\s*\(\s*[A-Za-z_]\w*\s*\)|\bdefined\s+[A-Za-z_]\w*|\b[A-Za-z_]\w*\s*\([^()]*\))', e)
    for k in range(0, len(parts), 2):
        parts[k] = re.sub(r'\b([A-Za-z_]\w*)\b', bare, parts[k])
    return re.sub(r'[ \t]+', ' ', ''.join(parts)).strip()

def strip_c_comments(s):
    return re.sub(r'/\*.*?\*/|//[^\n]*', ' ', s, flags=re.S)

DIRECTIVE = re.compile(r'^\s*#\s*(ifdef|ifndef|if|elifdef|elifndef|elif|else|endif)\b(.*)$', re.S)

def builtin_unifdef(text):
    lines = text.split('\n')
    out = []
    stack = []      # dicts: emit(bool: group directives are emitted), state, keep(bool), parent(bool)
    in_comment = False
    i = 0
    def active():
        return all(g['keep'] for g in stack)
    while i < len(lines):
        line = lines[i]
        phys = [line]
        # comment state at line start decides whether this can be a directive
        if not in_comment and DIRECTIVE.match(line):
            while phys[-1].endswith('\\') and i + 1 < len(lines):
                i += 1
                phys.append(lines[i])
            joined = ' '.join(p[:-1] if p.endswith('\\') else p for p in phys)
            # an unterminated /* on a directive line opens a block comment
            tail = re.sub(r'/\*.*?\*/', '', joined, flags=re.S)
            opens = '/*' in tail
            body = re.sub(r'/\*.*$', '', strip_c_comments(joined), flags=re.S)
            m = DIRECTIVE.match(body)
            kw, rest = m.group(1), m.group(2).strip()
            handled = handle_directive(kw, rest, stack, out, phys, active)
            if not handled and active():
                out.extend(phys)
            if opens:
                in_comment = True
                if handled and active():
                    out.append('/*')    # keep the comment that line opened balanced
            i += 1
            continue
        if active():
            out.append(line)
        # track block comments (strings with /* are rare in headers)
        scan = re.sub(r'"(\\.|[^"\\])*"', '""', line)
        pos = 0
        while True:
            if in_comment:
                e = scan.find('*/', pos)
                if e < 0:
                    break
                in_comment = False
                pos = e + 2
            else:
                s = scan.find('/*', pos)
                c = scan.find('//', pos)
                if s < 0 or (0 <= c < s):
                    break
                in_comment = True
                pos = s + 2
        i += 1
    if stack:
        raise ValueError('unterminated #if')
    return '\n'.join(out)

def eval_dir(kw, rest):
    if kw in ('ifdef', 'ifndef', 'elifdef', 'elifndef'):
        m = re.match(r'([A-Za-z_]\w*)', rest)
        if not m or m.group(1) not in KNOWN:
            return None, rest
        v = KNOWN[m.group(1)] is not None
        return int(v if kw in ('ifdef', 'elifdef') else not v), rest
    v = ExprEval(rest).run()
    return v, rest

def handle_directive(kw, rest, stack, out, phys, active):
    """Returns True when the directive line was consumed (dropped or
    rewritten); False when it must be copied unchanged (if still active)."""
    if kw in ('if', 'ifdef', 'ifndef'):
        parent = active()
        if not parent:
            stack.append({'emit': False, 'state': 'dead', 'keep': False})
            return True
        v, _ = eval_dir(kw, rest)
        if v is None:
            stack.append({'emit': True, 'state': 'unknown', 'keep': True})
            if kw == 'if':
                out.append('#if ' + fold_known(rest))
                return True
            return False
        stack.append({'emit': False, 'state': 'taken' if v else 'pending', 'keep': bool(v)})
        return True
    if not stack:
        raise ValueError('#%s without #if' % kw)
    g = stack[-1]
    if g['state'] == 'dead':
        if kw == 'endif':
            stack.pop()
        return True
    if kw in ('elif', 'elifdef', 'elifndef'):
        if g['state'] == 'taken':
            g['keep'] = False
            return True
        v, _ = eval_dir('if' if kw == 'elif' else kw[2:], rest)
        if g['state'] == 'pending':           # every earlier branch was known false
            if v is None:
                g.update(emit=True, state='unknown', keep=True)
                if kw == 'elif':
                    out.append('#if ' + fold_known(rest))
                else:
                    out.append('#' + kw[2:] + ' ' + rest)
                return True
            g.update(state='taken' if v else 'pending', keep=bool(v))
            return True
        # state unknown: the group's directives are being emitted
        if v is None:
            g['keep'] = True
            if kw == 'elif':
                out.append('#elif ' + fold_known(rest))
                return True
            return False
        if v:
            out.append('#else')
            g.update(state='taken', keep=True)
        else:
            g['keep'] = False
        return True
    if kw == 'else':
        if g['state'] == 'taken':
            g['keep'] = False
            return True
        if g['state'] == 'pending':
            g.update(state='taken', keep=True)
            return True
        g['keep'] = True
        g['state'] = 'taken'
        out.append('#else')
        return True
    if kw == 'endif':
        stack.pop()
        if g['emit']:
            out.append('#endif')
        return True
    return False

def run_unifdef(path, text=None):
    """unifdef $(KINCFRAME_UNIFDEF) on a file (or on `text`, named `path`)."""
    if text is None:
        with open(path, encoding='utf-8', errors='replace') as f:
            text = f.read()
    if UNIFDEF_BIN:
        p = subprocess.run([UNIFDEF_BIN] + UNIFDEF_FLAGS, input=text.encode('utf-8'),
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd='/', check=False)
        if p.returncode == 2:
            raise ValueError(p.stderr.decode(errors='replace').strip())
        return p.stdout.decode('utf-8', errors='replace')
    return builtin_unifdef(text)

def empty_after_decomment(text):
    return strip_c_comments(text).strip() == ''

# ===================================================================
# Walk the xnu Makefile tree.
# ===================================================================

def base_vars(reldir, arch):
    ac = ARCH_CONFIG.get(arch, arch.upper())
    v = {
        'PLATFORM': 'MacOSX', 'SDKROOT': 'macosx', 'SRCROOT': XNU,
        'SOURCE': os.path.join(XNU, reldir) + '/' if reldir else XNU + '/',
        'CURRENT_ARCH_CONFIG': ac, 'CURRENT_ARCH_CONFIG_LC': ac.lower(),
        'CURRENT_KERNEL_CONFIG': 'RELEASE', 'CURRENT_KERNEL_CONFIG_LC': 'release',
        'CURRENT_BUILD_CONFIG': 'RELEASE_' + ac, 'CURRENT_MACHINE_CONFIG': 'NONE',
        'RC_ProjectName': 'xnu', 'OBJROOT': '/nonexistent/obj', 'SYMROOT': '/nonexistent/sym',
        'DSTROOT': '/nonexistent/dst', 'VERSDIR': XNU,
    }
    v.update(SUPPORTED)
    return v

installs = {}       # dest -> (src, kind)
generated = {}      # dest -> producing dir
collisions = []
visited = set()

def add_install(dest, src, why):
    dest = os.path.normpath(dest)
    if dest in installs and installs[dest][0] != src:
        collisions.append((dest, installs[dest][0], src))
    installs[dest] = (src, why)

def evaluate(reldir, arch):
    mk = os.path.join(XNU, reldir, 'Makefile')
    if not os.path.isfile(mk):
        warn('missing %s (sparse checkout?)' % os.path.relpath(mk, XNU))
        return None
    m = Make(base_vars(reldir, arch), os.path.join(reldir, 'Makefile'))
    m.parse(mk)
    return m

def kf_list(m, name, fallback):
    # MakeInc.rule: ifndef INSTALL_KF_MI_LIST / INSTALL_KF_MI_LIST = $(EXPORT_MI_LIST)
    v = m.vars.get(name)
    if v and v[1].strip():
        return words(m.value(name))
    return words(m.value(fallback))

def walk(reldir, arch, mode):
    key = (reldir, arch, mode)
    if key in visited:
        return
    visited.add(key)
    m = evaluate(reldir, arch)
    if m is None:
        return
    if reldir:
        if mode == 'MI':
            files = kf_list(m, 'INSTALL_KF_MI_LIST', 'EXPORT_MI_LIST')
            gens = kf_list(m, 'INSTALL_KF_MI_GEN_LIST', 'EXPORT_MI_GEN_LIST')
            ddir = m.value('EXPORT_MI_DIR').strip()
        else:
            files = kf_list(m, 'INSTALL_KF_MD_LIST', 'EXPORT_MD_LIST')
            gens = kf_list(m, 'INSTALL_KF_MD_GEN_LIST', 'EXPORT_MD_GEN_LIST')
            ddir = m.value('EXPORT_MD_DIR').strip()
        ddir = '' if ddir in ('', '.') else ddir
        for f in files:
            src = os.path.join(XNU, reldir, f)
            if not os.path.isfile(src):
                warn('%s lists %s, which does not exist' % (reldir or '.', f))
                continue
            add_install(os.path.join(ddir, f), src, '%s %s' % (mode, reldir))
        for f in gens:
            src = os.path.join(XNU, reldir, f)
            if os.path.isfile(src):     # VPATH = .:$(SOURCE) (MakeInc.def) finds it in the source
                add_install(os.path.join(ddir, f), src, '%s %s (gen list, from source)' % (mode, reldir))
            else:
                generated.setdefault(os.path.normpath(os.path.join(ddir, f)), reldir)
    sub = 'INSTINC_SUBDIRS' if mode == 'MI' else 'INSTINC_SUBDIRS_' + ARCH_CONFIG.get(arch, arch.upper())
    for s in words(m.value(sub)):
        walk(os.path.normpath(os.path.join(reldir, s)), arch, mode)

for a in ARCHS:
    walk('', a, 'MI')
    walk('', a, 'MD')

# IOPCIFamily: Public headers of the Xcode project that live in IOKit/pci.
pbx = os.path.join(IOPCI, 'IOPCIFamily.xcodeproj', 'project.pbxproj')
public = set()
with open(pbx, encoding='utf-8', errors='replace') as f:
    for line in f:
        m = re.search(r'/\* (\S+\.h) in Headers \*/.*ATTRIBUTES = \(([^)]*)\)', line)
        if m and 'Public' in m.group(2):
            public.add(m.group(1))
for h in sorted(public):
    src = os.path.join(IOPCI, 'IOKit', 'pci', h)
    if os.path.isfile(src):
        add_install(os.path.join('IOKit', 'pci', h), src, 'IOPCIFamily Public')

# ===================================================================
# Hand-written stand-ins for build-generated headers the kext includes.
# ===================================================================

STANDIN_NOTE = '''/*
 * HAND-WRITTEN STAND-IN generated by scripts/xnu-kernel-headers.sh.
 *
 * In Kernel.framework this header is produced by MIG from %s at xnu
 * build time.  This stand-in declares only the routines the MT7925 kext
 * uses, with the C signatures MIG derives from the .defs routine
 * declarations quoted below (array arguments get a trailing
 * mach_msg_type_number_t count).  It is NOT the real header.
 */
'''

def mig_routine(defs_path, name):
    with open(defs_path, encoding='utf-8', errors='replace') as f:
        text = f.read()
    # The name may be wrapped in a naming macro: PREFIX(thread_policy_set),
    # KERNEL_SERVER_SUFFIX_SANDBOX(thread_terminate).
    m = re.search(r'\broutine\s+(?:\w+\(\s*)?%s(?:\s*\))?\s*\((.*?)\)\s*;' % re.escape(name), text, re.S)
    return re.sub(r'\s+', ' ', m.group(0)).strip() if m else None

STANDINS = {}
ta_defs = os.path.join(XNU, 'osfmk', 'mach', 'thread_act.defs')
if os.path.isfile(ta_defs):
    quoted = []
    for r in ('thread_terminate', 'thread_policy_set', 'thread_policy_get'):
        q = mig_routine(ta_defs, r)
        quoted.append(' *   %s' % (q or ('(routine %s not found in thread_act.defs)' % r)))
    STANDINS['mach/thread_act.h'] = (STANDIN_NOTE % 'osfmk/mach/thread_act.defs') + '''/*
 * Source routines (osfmk/mach/thread_act.defs).  MIG runs with xnu's DEFINES
 * (-DKERNEL, no KERNEL_SERVER), so PREFIX() and KERNEL_SERVER_SUFFIX_SANDBOX()
 * are the identity, and thread_act_consume_ref_t's C type is thread_act_t:
%s
 */
#ifndef _thread_act_user_
#define _thread_act_user_

#include <mach/ndr.h>
#include <mach/boolean.h>
#include <mach/kern_return.h>
#include <mach/notify.h>
#include <mach/mach_types.h>
#include <mach/message.h>
#include <mach/mig_errors.h>
#include <mach/port.h>
#include <mach/std_types.h>
#include <mach/mig.h>

#ifdef __BeforeMigUserHeader
__BeforeMigUserHeader
#endif

#include <sys/cdefs.h>
__BEGIN_DECLS

/* Routine thread_terminate */
#ifdef mig_external
mig_external
#else
extern
#endif
kern_return_t thread_terminate
(
	thread_act_t target_act
);

/* Routine thread_policy_set */
#ifdef mig_external
mig_external
#else
extern
#endif
kern_return_t thread_policy_set
(
	thread_act_t thread,
	thread_policy_flavor_t flavor,
	thread_policy_t policy_info,
	mach_msg_type_number_t policy_infoCnt
);

/* Routine thread_policy_get */
#ifdef mig_external
mig_external
#else
extern
#endif
kern_return_t thread_policy_get
(
	thread_inspect_t thread,
	thread_policy_flavor_t flavor,
	thread_policy_t policy_info,
	mach_msg_type_number_t *policy_infoCnt,
	boolean_t *get_default
);

__END_DECLS

#ifdef __AfterMigUserHeader
__AfterMigUserHeader
#endif

#endif /* _thread_act_user_ */
''' % '\n'.join(quoted)

MIG_INCLUDES = '''#include <string.h>
#include <mach/ndr.h>
#include <mach/boolean.h>
#include <mach/kern_return.h>
#include <mach/notify.h>
#include <mach/mach_types.h>
#include <mach/message.h>
#include <mach/mig_errors.h>
#include <mach/port.h>
#include <mach/std_types.h>
#include <mach/mig.h>
'''

def mig_standin(dest, defs):
    guard = '_%s_STANDIN_' % re.sub(r'\W', '_', dest)
    return '''/*
 * HAND-WRITTEN STAND-IN generated by scripts/xnu-kernel-headers.sh.
 *
 * In Kernel.framework %s is produced by MIG from %s at xnu
 * build time.  This stand-in has the includes a MIG header starts with but
 * declares NONE of the routines: the kext uses none of them.  A new use
 * fails as an undeclared identifier; add its prototype here, copied from the
 * .defs, the way mach/thread_act.h does.
 */
#ifndef %s
#define %s
%s#endif
''' % (dest, os.path.relpath(defs, XNU), guard, guard, MIG_INCLUDES)

def iig_scan_to(text, i, chars):
    """Index of the first of `chars` at or after i, skipping comments and
    string/char literals."""
    n = len(text)
    while i < n:
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        c = text[i]
        if c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            i = j + 1
            continue
        if c in chars:
            return i
        i += 1
    return n

def iig_standin(dest, iig):
    """Approximation of iig's kernel-side output: the .iig with the bodies of
    the classes it describes removed (Kernel.framework declares those classes
    in IOKit/ and libkern/c++/), .iig includes spelled .h, and the
    <Class>_Methods / _KernelMethods / _VirtualMethods macros that
    OSDeclare*StructorsWithDispatch() splices into the kernel class bodies
    defined empty.  Real iig output puts the DriverKit RPC methods (Start(),
    Dispatch(), ...) there; the kext uses none of them."""
    with open(iig, encoding='utf-8', errors='replace') as f:
        text = f.read()
    out, classes, i, n, depth = [], [], 0, len(text), 0
    cls = re.compile(r'class\s+(?:[A-Z][A-Z0-9_]*(?:\([^)]*\))?\s+)*([A-Za-z_]\w*)\b')
    while i < n:
        if text.startswith('/*', i) or text.startswith('//', i) or text[i] in '"\'':
            # copy the comment / literal verbatim
            if text.startswith('/*', i):
                e = text.find('*/', i + 2)
                e = n if e < 0 else e + 2
            elif text.startswith('//', i):
                e = text.find('\n', i)
                e = n if e < 0 else e
            else:
                q = text[i]
                e = i + 1
                while e < n and text[e] != q:
                    e += 2 if text[e] == '\\' else 1
                e += 1
            out.append(text[i:e])
            i = e
            continue
        if text[i] == '#' and text[text.rfind('\n', 0, i) + 1:i].strip() == '':
            e = i
            while True:
                nl = text.find('\n', e)
                nl = n if nl < 0 else nl
                if nl < n and text[nl - 1] == '\\':
                    e = nl + 1
                    continue
                break
            out.append(re.sub(r'(#\s*include\s*<DriverKit/[\w.]+)\.iig>', r'\1.h>', text[i:nl]))
            i = nl
            continue
        if depth == 0 and text.startswith('class', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            m = cls.match(text, i)
            k = iig_scan_to(text, i, '{;')
            if m and k < n and text[k] == '{':
                d, e = 0, k
                while e < n:
                    e = iig_scan_to(text, e, '{}')
                    if e >= n:
                        break
                    d += 1 if text[e] == '{' else -1
                    e += 1
                    if d == 0:
                        break
                semi = iig_scan_to(text, e, ';')
                classes.append(m.group(1))
                out.append('/* class %s: body omitted (stand-in for iig output) */' % m.group(1))
                i = semi + 1
                continue
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
        out.append(text[i])
        i += 1
    macros = []
    for c in classes:
        for suffix in ('_Methods', '_KernelMethods', '_VirtualMethods'):
            macros.append('#ifndef %s%s\n#define %s%s\n#endif' % (c, suffix, c, suffix))
    return '''/*
 * HAND-WRITTEN STAND-IN generated by scripts/xnu-kernel-headers.sh.
 *
 * In Kernel.framework %s is generated by iig (not open source) from
 * %s.  This approximation keeps the file-scope declarations
 * of the .iig and defines the per-class method macros empty; see
 * iig_standin() in the script.  Class layouts derived from it are NOT
 * those of the real SDK.
 */
%s
/* per-class macros spliced in by OSDeclare*StructorsWithDispatch() */
%s
''' % (dest, os.path.relpath(iig, XNU), ''.join(out), '\n'.join(macros))

def classify_gen(dest, reldir):
    d = os.path.join(XNU, reldir)
    stem = os.path.basename(dest)[:-2] if dest.endswith('.h') else None
    if not stem:
        return None
    if os.path.isfile(os.path.join(d, stem + '.iig')):
        return ('iig', os.path.join(d, stem + '.iig'))
    for s in (stem, stem[:-len('_server')] if stem.endswith('_server') else None):
        if s and os.path.isfile(os.path.join(d, s + '.defs')):
            return ('mig', os.path.join(d, s + '.defs'))
    return None

PLACEHOLDER = '''/*
 * PLACEHOLDER generated by scripts/xnu-kernel-headers.sh.
 *
 * Kernel.framework ships %s, but xnu generates it at build time
 * (listed in an INSTALL_KF_*_GEN_LIST of %s/Makefile: MIG output or a
 * template).  This stand-in tree has no generated copy.  If the kext needs
 * it, add a hand-written stand-in with just the declarations used to
 * scripts/xnu-kernel-headers.sh.
 */
#error "%s is generated at xnu build time; no stand-in in this header tree"
'''

# ===================================================================
# SDK user-space headers that Kernel.framework's own headers include.
#
# A kext build on a Mac runs with -isysroot MacOSX.sdk and without
# -nostdinc, so headers Kernel.framework lacks fall through to
# MacOSX.sdk/usr/include (Availability.h from mach/message.h, ...).  Only
# those that Kernel.framework headers reference are provided here; anything
# else the kext includes from usr/include fails to resolve, which is the
# point: kernel code must not depend on user-space headers.
# ===================================================================

USR_FROM_XNU = ['Availability.h', 'AvailabilityInternal.h', 'AvailabilityMacros.h', 'ptrcheck.h']

USR_STANDINS = {}
USR_STANDINS['TargetConditionals.h'] = """/*
 * HAND-WRITTEN STAND-IN generated by scripts/xnu-kernel-headers.sh for
 * MacOSX.sdk/usr/include/TargetConditionals.h (not part of xnu): the macOS
 * values only.  clang >= 18 predefines these (-fdefine-target-os-macros),
 * hence the guards.
 */
#ifndef __TARGETCONDITIONALS__
#define __TARGETCONDITIONALS__
#ifndef TARGET_OS_MAC
#define TARGET_OS_MAC 1
#endif
#ifndef TARGET_OS_OSX
#define TARGET_OS_OSX 1
#endif
#ifndef TARGET_OS_IPHONE
#define TARGET_OS_IPHONE 0
#endif
#ifndef TARGET_OS_IOS
#define TARGET_OS_IOS 0
#endif
#ifndef TARGET_OS_MACCATALYST
#define TARGET_OS_MACCATALYST 0
#endif
#ifndef TARGET_OS_WATCH
#define TARGET_OS_WATCH 0
#endif
#ifndef TARGET_OS_TV
#define TARGET_OS_TV 0
#endif
#ifndef TARGET_OS_VISION
#define TARGET_OS_VISION 0
#endif
#ifndef TARGET_OS_BRIDGE
#define TARGET_OS_BRIDGE 0
#endif
#ifndef TARGET_OS_DRIVERKIT
#define TARGET_OS_DRIVERKIT 0
#endif
#ifndef TARGET_OS_SIMULATOR
#define TARGET_OS_SIMULATOR 0
#endif
#ifndef TARGET_OS_EMBEDDED
#define TARGET_OS_EMBEDDED 0
#endif
#ifndef TARGET_OS_EXCLAVECORE
#define TARGET_OS_EXCLAVECORE 0
#endif
#ifndef TARGET_OS_EXCLAVEKIT
#define TARGET_OS_EXCLAVEKIT 0
#endif
#ifndef TARGET_OS_UNIX
#define TARGET_OS_UNIX 0
#endif
#ifndef TARGET_OS_LINUX
#define TARGET_OS_LINUX 0
#endif
#ifndef TARGET_OS_WINDOWS
#define TARGET_OS_WINDOWS 0
#endif
#ifndef TARGET_OS_WIN32
#define TARGET_OS_WIN32 0
#endif
#ifndef TARGET_IPHONE_SIMULATOR
#define TARGET_IPHONE_SIMULATOR 0
#endif
#ifndef TARGET_OS_NANO
#define TARGET_OS_NANO 0
#endif
#define TARGET_CPU_PPC 0
#define TARGET_CPU_PPC64 0
#define TARGET_CPU_68K 0
#define TARGET_CPU_X86 0
#if defined(__x86_64__)
#define TARGET_CPU_X86_64 1
#define TARGET_CPU_ARM 0
#define TARGET_CPU_ARM64 0
#elif defined(__arm64__)
#define TARGET_CPU_X86_64 0
#define TARGET_CPU_ARM 0
#define TARGET_CPU_ARM64 1
#else
#error "TargetConditionals.h stand-in: unsupported architecture"
#endif
#define TARGET_CPU_MIPS 0
#define TARGET_CPU_SPARC 0
#define TARGET_CPU_ALPHA 0
#define TARGET_RT_MAC_CFM 0
#define TARGET_RT_MAC_MACHO 1
#define TARGET_RT_LITTLE_ENDIAN 1
#define TARGET_RT_BIG_ENDIAN 0
#define TARGET_RT_64_BIT 1
#endif /* __TARGETCONDITIONALS__ */
"""
USR_STANDINS['os/availability.h'] = """/*
 * HAND-WRITTEN STAND-IN generated by scripts/xnu-kernel-headers.sh for
 * MacOSX.sdk/usr/include/os/availability.h (not part of xnu; xnu's own
 * EXTERNAL_HEADERS/Availability.h is too old for the up-to-six-platform
 * lists IOPCIFamily uses).  Each platform spec becomes its own
 * __attribute__((availability(...))), so deprecation and
 * unguarded-availability diagnostics for macOS behave as with the SDK.
 * Platforms upstream clang does not know (bridgeos) or that never matter
 * for a macOS compile (visionos/xros) expand to nothing.  SPI_* markers
 * expand to nothing, as they do outside Apple's builds.
 */
#ifndef __OS_AVAILABILITY__
#define __OS_AVAILABILITY__

#define __OSAV_CAT(a, b)	__OSAV_CAT_(a, b)
#define __OSAV_CAT_(a, b)	a ## b
#define __OSAV_N(...)		__OSAV_N_(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define __OSAV_N_(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
/* __OSAV_MAP(M, x, a, b, ...) -> M(x, a) M(x, b) ... */
#define __OSAV_MAP(M, x, ...)	__OSAV_CAT(__OSAV_MAP, __OSAV_N(__VA_ARGS__))(M, x, __VA_ARGS__)
#define __OSAV_MAP1(M, x, a)	M(x, a)
#define __OSAV_MAP2(M, x, a, ...) M(x, a) __OSAV_MAP1(M, x, __VA_ARGS__)
#define __OSAV_MAP3(M, x, a, ...) M(x, a) __OSAV_MAP2(M, x, __VA_ARGS__)
#define __OSAV_MAP4(M, x, a, ...) M(x, a) __OSAV_MAP3(M, x, __VA_ARGS__)
#define __OSAV_MAP5(M, x, a, ...) M(x, a) __OSAV_MAP4(M, x, __VA_ARGS__)
#define __OSAV_MAP6(M, x, a, ...) M(x, a) __OSAV_MAP5(M, x, __VA_ARGS__)
#define __OSAV_MAP7(M, x, a, ...) M(x, a) __OSAV_MAP6(M, x, __VA_ARGS__)
#define __OSAV_MAP8(M, x, a, ...) M(x, a) __OSAV_MAP7(M, x, __VA_ARGS__)

/* platform(args) -> platform-name, args  (pasting picks the spelling) */
#define __OSAV_P_macos(...)	macos, __VA_ARGS__
#define __OSAV_P_macosx(...)	macos, __VA_ARGS__
#define __OSAV_P_ios(...)	ios, __VA_ARGS__
#define __OSAV_P_tvos(...)	tvos, __VA_ARGS__
#define __OSAV_P_watchos(...)	watchos, __VA_ARGS__
#define __OSAV_P_driverkit(...)	driverkit, __VA_ARGS__
#define __OSAV_P_maccatalyst(...) maccatalyst, __VA_ARGS__
#define __OSAV_P_macCatalyst(...) maccatalyst, __VA_ARGS__
#define __OSAV_P_bridgeos(...)	__osav_drop, __VA_ARGS__
#define __OSAV_P_visionos(...)	__osav_drop, __VA_ARGS__
#define __OSAV_P_xros(...)	__osav_drop, __VA_ARGS__
#define __OSAV_EXP(...)		__VA_ARGS__

/* introduced */
#define __OSAV_AV(x, p)		__OSAV_EXP(__OSAV_AV_ __OSAV_EXP((__OSAV_P_ ## p)))
#define __OSAV_AV_(plat, v)	__OSAV_AV_ ## plat(v)
#define __OSAV_AVX(plat, v)	__attribute__((availability(plat, introduced = v)))
#define __OSAV_AV_macos(v)	__OSAV_AVX(macos, v)
#define __OSAV_AV_ios(v)	__OSAV_AVX(ios, v)
#define __OSAV_AV_tvos(v)	__OSAV_AVX(tvos, v)
#define __OSAV_AV_watchos(v)	__OSAV_AVX(watchos, v)
#define __OSAV_AV_driverkit(v)	__OSAV_AVX(driverkit, v)
#define __OSAV_AV_maccatalyst(v) __OSAV_AVX(maccatalyst, v)
#define __OSAV_AV___osav_drop(v)

/* deprecated, with a message or a replacement */
#define __OSAV_DM(m, p)		__OSAV_EXP(__OSAV_DM_ __OSAV_EXP((m, __OSAV_P_ ## p)))
#define __OSAV_DM_(m, plat, i, d) __OSAV_DM_ ## plat(m, i, d)
#define __OSAV_DMX(plat, m, i, d) __attribute__((availability(plat, introduced = i, deprecated = d, message = m)))
#define __OSAV_DM_macos(m, i, d)	__OSAV_DMX(macos, m, i, d)
#define __OSAV_DM_ios(m, i, d)		__OSAV_DMX(ios, m, i, d)
#define __OSAV_DM_tvos(m, i, d)		__OSAV_DMX(tvos, m, i, d)
#define __OSAV_DM_watchos(m, i, d)	__OSAV_DMX(watchos, m, i, d)
#define __OSAV_DM_driverkit(m, i, d)	__OSAV_DMX(driverkit, m, i, d)
#define __OSAV_DM_maccatalyst(m, i, d)	__OSAV_DMX(maccatalyst, m, i, d)
#define __OSAV_DM___osav_drop(m, i, d)
#define __OSAV_DR(r, p)		__OSAV_EXP(__OSAV_DR_ __OSAV_EXP((r, __OSAV_P_ ## p)))
#define __OSAV_DR_(r, plat, i, d) __OSAV_DR_ ## plat(r, i, d)
#define __OSAV_DRX(plat, r, i, d) __attribute__((availability(plat, introduced = i, deprecated = d, replacement = r)))
#define __OSAV_DR_macos(r, i, d)	__OSAV_DRX(macos, r, i, d)
#define __OSAV_DR_ios(r, i, d)		__OSAV_DRX(ios, r, i, d)
#define __OSAV_DR_tvos(r, i, d)		__OSAV_DRX(tvos, r, i, d)
#define __OSAV_DR_watchos(r, i, d)	__OSAV_DRX(watchos, r, i, d)
#define __OSAV_DR_driverkit(r, i, d)	__OSAV_DRX(driverkit, r, i, d)
#define __OSAV_DR_maccatalyst(r, i, d)	__OSAV_DRX(maccatalyst, r, i, d)
#define __OSAV_DR___osav_drop(r, i, d)

/* unavailable: bare platform names */
#define __OSAV_UN(x, p)		__OSAV_UN_ ## p
#define __OSAV_UNX(plat)	__attribute__((availability(plat, unavailable)))
#define __OSAV_UN_macos		__OSAV_UNX(macos)
#define __OSAV_UN_macosx	__OSAV_UNX(macos)
#define __OSAV_UN_ios		__OSAV_UNX(ios)
#define __OSAV_UN_tvos		__OSAV_UNX(tvos)
#define __OSAV_UN_watchos	__OSAV_UNX(watchos)
#define __OSAV_UN_driverkit	__OSAV_UNX(driverkit)
#define __OSAV_UN_maccatalyst	__OSAV_UNX(maccatalyst)
#define __OSAV_UN_macCatalyst	__OSAV_UNX(maccatalyst)
#define __OSAV_UN_bridgeos
#define __OSAV_UN_visionos
#define __OSAV_UN_xros

#define API_AVAILABLE(...)			__OSAV_MAP(__OSAV_AV, _, __VA_ARGS__)
#define API_DEPRECATED(m, ...)			__OSAV_MAP(__OSAV_DM, m, __VA_ARGS__)
#define API_DEPRECATED_WITH_REPLACEMENT(r, ...)	__OSAV_MAP(__OSAV_DR, r, __VA_ARGS__)
#define API_UNAVAILABLE(...)			__OSAV_MAP(__OSAV_UN, _, __VA_ARGS__)
#define API_AVAILABLE_BEGIN(...)
#define API_AVAILABLE_END
#define API_DEPRECATED_BEGIN(...)
#define API_DEPRECATED_END
#define API_UNAVAILABLE_BEGIN(...)
#define API_UNAVAILABLE_END
#define SPI_AVAILABLE(...)
#define SPI_DEPRECATED(...)
#define SPI_DEPRECATED_WITH_REPLACEMENT(...)
#endif /* __OS_AVAILABILITY__ */
"""

# ===================================================================
# KPI symbol sets (for the link-level check): what each bundle a kext can
# list in OSBundleLibraries exports, combined the way config/Makefile's
# symbol_set_template does (X.exports + X.<arch>.exports +
# X.<arch>.MacOSX.exports), plus IOPCIFamily's own exports list and the
# symbols libkmod.a defines (libkern/kmod/*.c, linked with -lkmod).
# ===================================================================

def plist_string(path, key):
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            t = f.read()
    except OSError:
        return None
    m = re.search(r'<key>%s</key>\s*<string>([^<]*)</string>' % re.escape(key), t)
    return m.group(1).strip() if m else None

def read_exports(path):
    out = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#'):
                out.append(line)
    return out

def kpi_sets(arch):
    lc = 'x86_64' if arch.startswith('x86_64') else 'arm64'
    cfg = os.path.join(XNU, 'config')
    m = Make(base_vars('config', arch), 'config/Makefile')
    m.parse(os.path.join(cfg, 'Makefile'))
    comps = words(m.value('SYMBOL_COMPONENT_LIST')) or ['BSDKernel', 'IOKit', 'Libkern', 'Mach', 'MACFramework', 'Unsupported', 'Private']
    sets = {}
    for c in comps:
        bid = plist_string(os.path.join(cfg, 'System.kext', 'PlugIns', c + '.kext', 'Info.plist'), 'CFBundleIdentifier')
        if not bid:
            warn('config: no bundle identifier for %s' % c)
            continue
        files = [c + '.exports', '%s.%s.exports' % (c, lc), '%s.%s.MacOSX.exports' % (c, lc)]
        syms, used = [], []
        for fn in files:
            fp = os.path.join(cfg, fn)
            if os.path.isfile(fp):
                syms += read_exports(fp)
                used.append(fn)
        sets[bid] = (syms, 'xnu config/' + ' + '.join(used))
    ipx = os.path.join(IOPCI, 'config', 'IOPCIFamily.exports')
    if os.path.isfile(ipx):
        bid = plist_string(os.path.join(IOPCI, 'Info-IOPCIFamily.plist'), 'CFBundleIdentifier') or 'com.apple.iokit.IOPCIFamily'
        if '$' in bid:
            bid = 'com.apple.iokit.IOPCIFamily'
        sets[bid] = (read_exports(ipx), 'IOPCIFamily config/IOPCIFamily.exports')
    kmod = []
    for fn in sorted(glob.glob(os.path.join(XNU, 'libkern', 'kmod', '*.c'))):
        with open(fn, encoding='utf-8', errors='replace') as f:
            t = f.read()
        for mm in re.finditer(r'^__private_extern__[^\n;]*\n([A-Za-z_]\w*)\s*\(', t, re.M):
            kmod.append('_' + mm.group(1))
    if kmod:
        sets['libkmod'] = (sorted(set(kmod)), 'xnu libkern/kmod/*.c (-lkmod / -lkmodc++)')
    else:
        warn('libkern/kmod/*.c not found: the symbol check will not know libkmod.a')
    return sets

# ===================================================================
# Install.
# ===================================================================

KF_REL = os.path.join('System', 'Library', 'Frameworks', 'Kernel.framework', 'Headers')
KF = os.path.join(OUT, KF_REL)
USR = os.path.join(OUT, 'usr', 'include')
if os.path.isdir(OUT):
    if os.listdir(OUT) and not os.path.isfile(os.path.join(OUT, 'MANIFEST')):
        sys.exit('xnu-kernel-headers: %s exists and was not made by this script; refusing to replace it' % OUT)
    shutil.rmtree(OUT)
os.makedirs(KF)
os.makedirs(USR)
manifest = []
dropped = []
errors = 0
for dest in sorted(installs):
    src, why = installs[dest]
    try:
        if why.startswith('IOPCIFamily'):
            with open(src, encoding='utf-8', errors='replace') as f:
                text = f.read()
        else:
            text = run_unifdef(src)
    except ValueError as e:
        warn('unifdef failed on %s: %s' % (os.path.relpath(src, XNU), e))
        errors += 1
        continue
    if empty_after_decomment(text):
        dropped.append(dest)
        continue
    d = os.path.join(KF, dest)
    os.makedirs(os.path.dirname(d), exist_ok=True)
    with open(d, 'w', encoding='utf-8') as f:
        f.write(text if text.endswith('\n') else text + '\n')
    base = XNU if src.startswith(XNU + '/') else IOPCI
    manifest.append('%-60s %s:%s' % (dest, 'xnu' if base == XNU else 'IOPCIFamily', os.path.relpath(src, base)))

gen_lines = []
for dest in sorted(generated):
    if dest in installs:
        continue
    d = os.path.join(KF, dest)
    os.makedirs(os.path.dirname(d), exist_ok=True)
    kind = classify_gen(dest, generated[dest])
    if dest in STANDINS:
        body, what = STANDINS[dest], 'HAND-WRITTEN STAND-IN (MIG routines used by the kext)'
    elif kind and kind[0] == 'mig':
        body, what = mig_standin(dest, kind[1]), 'stand-in: MIG includes only, no routines (%s)' % os.path.relpath(kind[1], XNU)
    elif kind and kind[0] == 'iig':
        try:
            body = run_unifdef(kind[1], iig_standin(dest, kind[1]))
        except ValueError as e:
            warn('unifdef failed on the iig stand-in for %s: %s' % (dest, e))
            errors += 1
            continue
        what = 'stand-in: approximated iig output (%s)' % os.path.relpath(kind[1], XNU)
    else:
        body, what = PLACEHOLDER % (dest, generated[dest], dest), 'placeholder (#error), generated by %s' % generated[dest]
    with open(d, 'w', encoding='utf-8') as f:
        f.write(body)
    gen_lines.append('%-60s %s' % (dest, what))
for dest in sorted(STANDINS):
    if dest not in generated:
        warn('stand-in %s is not in any INSTALL_KF_*_GEN_LIST' % dest)

usr_lines = []
for h in USR_FROM_XNU:
    src = os.path.join(XNU, 'EXTERNAL_HEADERS', h)
    if os.path.isfile(src):
        shutil.copyfile(src, os.path.join(USR, h))
        usr_lines.append('%-60s xnu:EXTERNAL_HEADERS/%s (verbatim)' % (h, h))
    else:
        warn('EXTERNAL_HEADERS/%s missing' % h)
for h in sorted(USR_STANDINS):
    d = os.path.join(USR, h)
    os.makedirs(os.path.dirname(d), exist_ok=True)
    with open(d, 'w', encoding='utf-8') as f:
        f.write(USR_STANDINS[h])
    usr_lines.append('%-60s HAND-WRITTEN STAND-IN' % h)

kpi_lines = []
for a in ARCHS:
    kd = os.path.join(OUT, 'KPI', a)
    os.makedirs(kd, exist_ok=True)
    for bid, (syms, origin) in sorted(kpi_sets(a).items()):
        with open(os.path.join(kd, bid + '.exports'), 'w', encoding='utf-8') as f:
            f.write('# %s (%s)\n' % (bid, origin))
            f.write('\n'.join(syms) + '\n')
        kpi_lines.append('KPI/%s/%-40s %5d symbols  %s' % (a, bid + '.exports', len(syms), origin))

ver = ''
mv = os.path.join(XNU, 'config', 'MasterVersion')
if os.path.isfile(mv):
    with open(mv) as f:
        ver = next((l.strip() for l in f if l.strip() and not l.startswith('#')), '')
with open(os.path.join(OUT, 'MANIFEST'), 'w', encoding='utf-8') as f:
    f.write('# Stand-in macOS SDK root (Kernel.framework/Headers only) assembled by\n')
    f.write('# scripts/xnu-kernel-headers.sh.  Use as KEXT_SDK / XNU_HDRS.\n')
    f.write('# xnu: %s%s\n# IOPCIFamily: %s\n' % (XNU, (' (MasterVersion %s)' % ver) if ver else '', IOPCI))
    f.write('# archs (MD walks, KPI sets): %s\n' % ' '.join(ARCHS))
    f.write('# unifdef: %s\n' % (UNIFDEF_BIN or 'built-in evaluator'))
    f.write('# KINCFRAME_UNIFDEF: %s\n' % ' '.join(UNIFDEF_FLAGS))
    f.write('# not provided: PCIDriverKit/ and DriverKit/ (iig output)\n')
    f.write('\n[%s]\n' % KF_REL)
    f.write('\n'.join(manifest) + '\n')
    f.write('\n[%s: build-generated]\n' % KF_REL)
    f.write('\n'.join(gen_lines) + '\n')
    f.write('\n[%s: dropped, empty after unifdef]\n' % KF_REL)
    f.write('\n'.join(dropped) + '\n')
    f.write('\n[%s: collisions, later source wins]\n' % KF_REL)
    f.write('\n'.join('%s: %s vs %s' % (d, os.path.relpath(a, XNU), os.path.relpath(b, XNU)) for d, a, b in collisions) + '\n')
    f.write('\n[usr/include: SDK user headers referenced by Kernel.framework]\n')
    f.write('\n'.join(usr_lines) + '\n')
    f.write('\n[KPI symbol sets]\n')
    f.write('\n'.join(kpi_lines) + '\n')
    f.write('\n[warnings]\n')
    f.write('\n'.join(warnings) + '\n')

print('xnu-kernel-headers: %d headers, %d build-generated (%d stand-ins), %d dropped, %d collisions, '
      '%d usr/include, %d KPI sets, %d warnings -> %s'
      % (len(manifest), len(gen_lines), sum(1 for g in gen_lines if 'placeholder' not in g), len(dropped),
         len(collisions), len(usr_lines), len(kpi_lines), len(warnings), OUT))
for w in warnings:
    print('  warning: ' + w)
sys.exit(1 if errors else 0)
PY

cd "$TMPD"
python3 -I "$TMPD/xnu_kernel_headers.py" "$XNU" "$IOPCI" "$OUT" "$ARCHS" "$UNIFDEF_BIN"
