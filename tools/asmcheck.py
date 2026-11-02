#!/usr/bin/env python3
"""PowerPC (Gekko/MWCC) asm annotator and asm-vs-C++ structure check for decomp review.

Standalone, python3 stdlib only. Asm is dtk-style: `.fn NAME, ...` .. `.endfn NAME`, one
instruction per line as `/* OFF ADDR  B0 B1 B2 B3 */ op args`, rodata as `.obj LABEL` .. `.endobj`
(`.float` / `.double` / `.4byte` ...). OFF, the first column, is what everything cites (MP2 REL
asm: module offset; MP3 DOL asm: absolute address).

  asmcheck.py annotate <asm> <fn> [--header H.hpp --class C] [--all-ins]
  asmcheck.py check <asm> <file.cpp> [fn ...] [--all] [--quiet] [--root DIR] [-I DIR] [--only TAGS]
                    [--module NAME]

<asm>  a .s file, a directory searched recursively for .s, or a module name looked up as
       $ASMCHECK_ASM_ROOT/<name>/asm (default root below). Rodata is read from the asm dir.
<fn>   a `.fn` name (fn_72_52F8, fn_80012345, a symbol) or a hex offset inside a function.

annotate: every bit op (rlwinm family incl. extrwi/clrlwi/insrwi, rlwimi, andi./andis.,
ori/oris/xori, andc, cntlzw idioms), every CR use (cmp, record forms, cror family, mfcr extract,
conditional branches), rodata float loads and calls, with the mask as a C constant and a C
expression. Values are tracked through registers (merged at labels) back to their load, so a
bit is named from load offset + mask: `x8f4_40` is byte 0x8f4, mask 0x40 (PowerPC bit N of a
word is 1u << (31 - N)). Bases other than the function's r3 are spelled `a1->x24_80`,
`x60->x4_01` (field loaded from x60), `sp+0x20`. `--header/--class` adds the header member for
each bit: its position comes from the member comment (`// 0xc10 0x80`, `// 0xe5e bit 5` (LSB
numbering), `// 0x8f7_25`), else its name (`x8f4_25_foo` = byte 0x8f4 mask 0x40, `xc10_10_` =
mask 0x10), else the MWCC big-endian MSB-first layout of its bitfield run anchored on a
positioned member. A comment may give one position per module, `;`-separated segments labelled
with a module (`CIng: 0x9D4; IngBoostBallGuardian: 0xAE4`, a leading `C` is optional): the
segment of the module being checked (`--module`, default the `<Module>/asm` of <asm>) applies,
an unlabelled segment applies to any module, and labelled-only comments naming other modules
give no position. `HDR?` marks a member whose layout position disagrees with its stated one,
or a bit the layout and the stated positions assign to different members. Never trusted.

check: per function, the C++ body (defs marked `// @ fn_X` or whose comment block above cites
fn_X first; a fn id in a comment on the signature lines only maps fn -> def for CALL) against
the asm. Members resolve through the method's class (and bases) found via the cpp's #include
closure, member-of-member chains (`mInfo.mX8`) through the member's type, and a trivial getter
(`GetX() { return mX; }`) reads its member. Port helpers called from the body (defs citing no fn) are merged in,
two levels deep, but only to cover asm ops: a helper may be its own asm function, so correctness
tags come from the body alone. Output: `<TAG> <fn> 0x<asm off>: text [file:line]`.

  correctness tags, reported for every checked function:
  MASK   same byte, asm and C++ disagree on the bit (read or write), or set vs clear
  XBIT   C++ reads/writes a bitfield byte the asm never loads or stores
  CROP   compare operator: asm compare on a field (exact for mfcr/cntlzw extracts, op-or-negation
         for branches; operands normalized field-first) vs every C++ comparison on that field
  XCMP   C++ compares a (non-bit) field the asm never compares or tests (for-loop headers
         excluded: counted loops become bdnz)
  XSTORE C++ assigns a field the asm never stores to nor passes by address to a call
  FCONST a float/double the asm loads from rodata (`lfs/lfd`, `lbl@l` or `lis/addi` + `0x0(rX)`) with
         no C++ literal of the body/merged helpers within 1e-6 relative (or equal as float32), and
         a C++ float literal no asm load matches. Negations match by |x|; 0, 1, 0.5, the int->float
         conversion magics, FLT_MAX/EPSILON, 255/256 and their reciprocals, degree<->radian factors,
         literal squares and same-line products/quotients/sums are not reported
  OFFS   a cited asm offset (`0xCE2C`: >= 4 hex digits or after `asm`/`at`, 4-aligned, inside
         the function) whose quoted instruction or same-line bit mask does not match the asm
  completeness tags, reported only for `// @`-marked functions (or with --all); unmarked ones get
  a NOTE line with the suppressed count:
  BIT    asm tests/sets/clears a bit of a `this` field the C++ never touches
  CMP    asm compares a field the C++ never compares, tests or switches on
  STORE  asm stores a field the header names and the C++ never writes or passes by reference
  CALL   asm calls a named function (mangled base name, or fn_X resolved through the defs citing
         it in the cpp's directory) the C++ body never names; `unported` when nothing cites it
  XCALL  C++ calls a port function that cites an fn id, and the asm calls neither it nor that fn
  BRANCH asm conditions (cond branches + mfcr/cntlzw extracts, bdnz excluded) >= 2*C++ + 4
         (missing blocks), or C++ (if/?:/&&/||/case/while/for) >= 2*asm + 4 (extra logic)
  CALLS  asm calls (bl/bctrl, runtime helpers excluded) >= 2*C++ call tokens + 3
  info tag, never in the exit code:
  NOPOS  a bitfield the C++ uses has no stated position for this module (comment/name); its bits
         are not checked for correctness (completeness BIT still credits its layout position)
Inlined callees: a local `fn_X` the asm calls is folded into the caller (one level): its `this`
loads/stores/compares/bit ops, with r3..r10 mapped from the call (`this`, `&this->m`, a field by
value, a stack copy of a field), count as the caller's for XSTORE/XCMP/MASK/XBIT, and the call
needs no C++ call when a `// fn_X` comment cites it or the C++ uses a folded field.
Tolerances: an integer relational asm compare where the C++ only has ==/!= on that field is
skipped (MWCC lowers switches and == chains to bge/blt trees); integer CROP compares only the
direction (`< 1` == `<= 0`); a ctor's zero/false/nullptr/kInvalidUniqueId init of a field the
retail ctor leaves to the zeroed heap is not XSTORE/XBIT; `== / != nullptr` guards are not XCMP;
a field whose address the asm passes to a call (incl. `addic. rX, this, off` placement new) is
not XCMP/XSTORE; a whole-line `// fn_X` comment in a body marks the block below it (to the next
case/dedent) as another function, left out, but on (or right above) a call statement it names
that call; compares inside `return` count; a compare of a computed value the asm then stores to
a field (`x += d; if (x > 1) x = 1;`, through the join of both values) is a compare of that
field, and `x = Max(0.f, x - dt)` (Max/Min/Clamp) tests x; branchless bool idioms (`subfic`,
neg/or/andc ... `srwi 31`, cntlzw of a field-derived value) are compares of the field; debug-only
statements (`if (Debug...())`, logging-only if bodies, debug `#if`) don't count for BRANCH; copy
ctors are no CALL; a def
citing several fns judges absence over all of them; `x & ~M` reads none of M; extracts wider
than 8 bits (index scaling) are not bit tests. Bits are compared per bit within a byte, so a C++
bit read MWCC merged into a byte test still matches; loads/stores only count with `this` as root
(r3, or r4 when the def returns a class by value: `--sret` for annotate), through `addi` pointers
and saved-register copies; a def whose r3 is unused while r4 is read is analysed as sret too.
Exit 1 when anything but NOPOS/NOTE is reported, 0 when clean, 2 on errors.
Known limits: folding is one level deep; a duplicate C++ write next to a port of a folded fn_X is
hidden; a retail fn whose signature differs (id taken by pointer, port takes it by value) still
XCMP/XSTOREs (EP fn_15_2394); mfcr extracts stay exact (`==` vs `<= 0` on a value known >= 0 is
reported); a C++ helper's conditions don't count for its caller's BRANCH (a wrapper delegating
to a shared helper flags); when several unmarked defs cite one fn first, the last one is checked
(MinorIng's RELMain comment naming fn_44_110 captures the loader's id; mark the loader `// @`).
"""
import argparse
import os
import math
import re
import struct
import sys
from collections import defaultdict

ASM_ROOT = os.environ.get(
    'ASMCHECK_ASM_ROOT',
    '/run/media/odran/Leo/projects/Restored-projects/Chatgpt/MetroidPrime2Port/build/G2ME01')

INSN_RE = re.compile(r'^/\*\s*([0-9A-Fa-f]{8})\s+(?:[0-9A-Fa-f]{8}\s+)?(?:[0-9A-Fa-f]{2}\s+){4}\*/'
                     r'\s*([a-z][\w.]*[+-]?)\s*(.*?)\s*$')
FN_RE = re.compile(r'^\.fn\s+("[^"]+"|[^,\s]+)')
LABEL_RE = re.compile(r'^(\.L\w+|[A-Za-z_][\w.$]*):\s*$')

M32 = 0xffffffff


def num(s):
    s = s.strip()
    m = re.fullmatch(r'(-?)(0x[0-9a-fA-F]+|\d+)', s)
    if not m:
        return None
    v = int(m.group(2), 0)
    return -v if m.group(1) else v


def rotl(x, n):
    n %= 32
    return ((x << n) | (x >> (32 - n))) & M32 if n else x


def mask32(mb, me):
    m, i = 0, mb
    while True:
        m |= 1 << (31 - i)
        if i == me:
            return m
        i = (i + 1) % 32


def hx(v):
    return '0x%x' % v if v > 9 or v < 0 else str(v)


def popcount(v):
    return bin(v).count('1')


# ---------------------------------------------------------------------------------------------
# asm loading

class Ins:
    __slots__ = ('off', 'op', 'args', 'text', 'idx')

    def __init__(s, off, op, args, text, idx):
        s.off, s.op, s.args, s.text, s.idx = off, op, args, text, idx


class Func:
    def __init__(s, name, path):
        s.name, s.path, s.ins, s.labels = name, path, [], {}

    @property
    def start(s):
        return s.ins[0].off if s.ins else 0

    @property
    def end(s):
        return s.ins[-1].off + 4 if s.ins else 0


class Module:
    def __init__(s):
        s.funcs, s.rodata, s.paths = {}, {}, []

    def find(s, sel):
        if sel in s.funcs:
            return s.funcs[sel]
        m = re.fullmatch(r'fn_([0-9A-Za-z]+)_([0-9A-Fa-f]+)', sel)
        if m:
            key = 'fn_%s_%X' % (m.group(1), int(m.group(2), 16))
            for n in (key, key.lower()):
                if n in s.funcs:
                    return s.funcs[n]
            for n, f in s.funcs.items():
                if n.lower() == sel.lower():
                    return f
            return None
        m = re.fullmatch(r'(?:fn_)?(?:0x)?([0-9A-Fa-f]+)', sel)
        if m:
            v = int(m.group(1), 16)
            for f in s.funcs.values():
                if f.start <= v < f.end:
                    return f
        return None

    def call_names(s):
        if not hasattr(s, '_calls'):
            names = set()
            for f in s.funcs.values():
                for i in f.ins:
                    if i.op == 'bl' and i.args:
                        names.add(base_name(i.args[0]))
            s._calls = names
        return s._calls


def split_args(a):
    a = re.sub(r'\s*#.*$', '', a)
    if not a:
        return []
    out, depth, cur, q = [], 0, '', False
    for ch in a:
        if ch == '"':
            q = not q
        if ch == '(' and not q:
            depth += 1
        elif ch == ')' and not q:
            depth -= 1
        if ch == ',' and depth == 0 and not q:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    out.append(cur.strip())
    return out


def resolve_asm(arg):
    if os.path.exists(arg):
        return arg
    for cand in (os.path.join(ASM_ROOT, arg, 'asm'), os.path.join(ASM_ROOT, arg)):
        if os.path.isdir(cand):
            return cand
    sys.exit('asmcheck: no asm at %s (nor %s/%s/asm)' % (arg, ASM_ROOT, arg))


def load_module(arg):
    path = resolve_asm(arg)
    mod = Module()
    if os.path.isdir(path):
        files, datadir = [], path
        for dp, dn, fn in sorted(os.walk(path)):
            dn.sort()
            files += [os.path.join(dp, f) for f in sorted(fn) if f.endswith('.s')]
        data = files
    else:
        files = [path]
        d = os.path.dirname(os.path.abspath(path))
        while d != '/' and os.path.basename(d) != 'asm':
            d = os.path.dirname(d)
        if os.path.basename(d) != 'asm':
            d = os.path.dirname(os.path.abspath(path))
        data = []
        for dp, dn, fn in os.walk(d):
            data += [os.path.join(dp, f) for f in fn if f.endswith('.s')]
    mod.paths = files
    for p in files:
        parse_text(mod, p)
    for p in data:
        parse_data(mod, p)
    return mod


def parse_text(mod, path):
    cur = None
    pend = []
    with open(path, errors='replace') as fh:
        for line in fh:
            line = line.rstrip('\n')
            m = FN_RE.match(line)
            if m:
                name = m.group(1).strip('"')
                cur = None if name in mod.funcs else Func(name, path)
                if cur:
                    mod.funcs[name] = cur
                pend = []
                continue
            if line.startswith('.endfn'):
                cur = None
                continue
            if cur is None:
                continue
            m = LABEL_RE.match(line)
            if m:
                pend.append(m.group(1))
                continue
            m = INSN_RE.match(line)
            if m:
                op = m.group(2).rstrip('+-')
                ins = Ins(int(m.group(1), 16), op, split_args(m.group(3)), m.group(2) + ' ' + m.group(3),
                          len(cur.ins))
                for lb in pend:
                    cur.labels[lb] = ins.idx
                pend = []
                cur.ins.append(ins)


def parse_data(mod, path):
    cur = None
    off = 0
    with open(path, errors='replace') as fh:
        for line in fh:
            s = line.strip()
            if s.startswith('.obj '):
                name = s[5:].split(',')[0].strip().strip('"')
                cur = mod.rodata.setdefault(name, [])
                off = 0
                continue
            if s.startswith('.endobj'):
                cur = None
                continue
            if cur is None or not s.startswith('.'):
                continue
            d, _, v = s.partition(' ')
            v = v.strip()
            size = {'.float': 4, '.4byte': 4, '.double': 8, '.8byte': 8, '.2byte': 2, '.byte': 1}.get(d)
            if size:
                for part in v.split(','):
                    cur.append((off, d, part.strip()))
                    off += size
            elif d == '.skip':
                off += num(v.split(',')[0]) or 0
            elif d in ('.string', '.asciz'):
                cur.append((off, d, v))
                off += len(v)


def rod_value(mod, label, delta, kind):
    ents = mod.rodata.get(label)
    if not ents:
        return None
    for i, (o, d, v) in enumerate(ents):
        if o != delta:
            continue
        try:
            if d in ('.float', '.double'):
                return float(v)
            if d == '.4byte':
                iv = num(v)
                if iv is None:
                    return v
                if kind == 'f':
                    return struct.unpack('>f', struct.pack('>I', iv & M32))[0]
                if kind == 'd' and i + 1 < len(ents):
                    lo = num(ents[i + 1][2]) or 0
                    return struct.unpack('>d', struct.pack('>II', iv & M32, lo & M32))[0]
                return iv
            if d == '.8byte' and kind == 'd':
                return struct.unpack('>d', struct.pack('>Q', num(v) & 0xffffffffffffffff))[0]
            return num(v)
        except (ValueError, struct.error, TypeError):
            return None
    return None


def fmt_float(v):
    if isinstance(v, float):
        s = '%.9g' % v
        if 'e' not in s and '.' not in s and 'n' not in s:
            s += '.0'
        return s + 'f'
    return str(v)


# ---------------------------------------------------------------------------------------------
# mangled names

OPERATOR_BASES = {'__as', '__pl', '__mi', '__ml', '__dv', '__eq', '__ne', '__lt', '__gt', '__le', '__ge',
                  '__vc', '__nw', '__dl', '__nwa', '__dla', '__cl', '__rf', '__apl', '__ami', '__amu', '__adv',
                  '__ad', '__or', '__er', '__co', '__nt', '__aa', '__oo', '__md', '__ls', '__rs', '__pp', '__mm',
                  '__op', '__aor', '__aad', '__aer', '__als', '__ars', '__amd'}
RUNTIME_PREFIX = ('_savegpr', '_restgpr', '_savefpr', '_restfpr', '__save', '__rest', '__cvt_', '__div2',
                  '__mod2', '__shl2', '__shr2', '__construct_', '__destroy_', '__ptmf', '__dt__', '__va_arg',
                  '__register_global', 'memset', 'memcpy', '__copy', '__fill_mem')


def base_name(target):
    t = re.sub(r'<[^<>]*(?:<[^<>]*>[^<>]*)*>', '', target.strip('"'))
    if t.startswith('__ct__'):
        m = re.match(r'__ct__(?:Q\d)?(\d+)(\w+)', t)
        return m.group(2)[:int(m.group(1))] if m else '__ct'
    m = re.match(r'^(.+?)__(?:Q\d|\d|F|C|$)', t)
    return m.group(1) if m and not m.group(1).startswith('_') else (m.group(1) if m else t)


def is_runtime(target):
    t = target.strip('"')
    if t.startswith(RUNTIME_PREFIX):
        return True
    b = base_name(t)
    return b in OPERATOR_BASES or (t.startswith('__') and not t.startswith('__ct__'))


# ---------------------------------------------------------------------------------------------
# dataflow values

class V:
    """k: in | sp | c | ha | sym | ptr | ld | rod | ret | bits | crs | crb | clz | diff | x"""
    __slots__ = ('k', 'a', 'b', 'c', 'd')

    def __init__(s, k, a=None, b=None, c=None, d=None):
        s.k, s.a, s.b, s.c, s.d = k, a, b, c, d


class Bits:
    """rotl(src, sh) & mask, plus pending field writes (mods) when sh == 0."""
    k = 'bits'

    def __init__(s, src, sh, mask, mods=None, idxs=None, const=None):
        s.src, s.sh, s.mask = src, sh, mask
        s.mods = list(mods or [])
        s.idxs = list(idxs or [])
        s.const = const


class Cmp:
    __slots__ = ('idx', 'lhs', 'rhs', 'kind')

    def __init__(s, idx, lhs, rhs, kind):
        s.idx, s.lhs, s.rhs, s.kind = idx, lhs, rhs, kind


TAINT_OPS = {'neg', 'or', 'nor', 'and', 'andc', 'orc', 'subf', 'subfc', 'subfe', 'subfic', 'subfze', 'subfme',
             'add', 'addc', 'adde', 'addze', 'addme', 'addi', 'addic', 'subi', 'xor', 'xori', 'cntlzw', 'srawi',
             'sraw', 'extsb', 'extsh'}
WIDTH = {'b': 1, 'h': 2, 'w': 4, 'f': 4, 'd': 8, 'q': 8, 'x': 4}
LOADS = {'lbz': 'b', 'lbzu': 'b', 'lhz': 'h', 'lha': 'h', 'lhzu': 'h', 'lhau': 'h', 'lwz': 'w', 'lwzu': 'w',
         'lfs': 'f', 'lfsu': 'f', 'lfd': 'd', 'lfdu': 'd', 'psq_l': 'q', 'psq_lu': 'q'}
STORES = {'stb': 1, 'stbu': 1, 'sth': 2, 'sthu': 2, 'stw': 4, 'stwu': 4, 'stfs': 4, 'stfsu': 4, 'stfd': 8,
          'stfdu': 8, 'psq_st': 8, 'psq_stu': 8, 'stfiwx': 4}
CONDS = {'lt': (0, True), 'ge': (0, False), 'nl': (0, False), 'gt': (1, True), 'le': (1, False),
         'ng': (1, False), 'eq': (2, True), 'ne': (2, False), 'so': (3, True), 'ns': (3, False),
         'un': (3, True), 'nu': (3, False)}
BC_RE = re.compile(r'^b(lt|le|eq|ge|gt|ne|nl|ng|so|ns|un|nu)(lr|ctr)?(l)?$')
COND_NAMES = ('lt', 'gt', 'eq', 'so')
ALL3 = frozenset(('lt', 'gt', 'eq'))
OPS = {frozenset(['lt']): '<', frozenset(['gt']): '>', frozenset(['eq']): '==',
       frozenset(['lt', 'eq']): '<=', frozenset(['gt', 'eq']): '>=', frozenset(['lt', 'gt']): '!=',
       frozenset(): 'false', ALL3: 'true'}
NEG = {'<': '>=', '>=': '<', '>': '<=', '<=': '>', '==': '!=', '!=': '=='}
SWAP = {'<': '>', '>': '<', '<=': '>=', '>=': '<=', '==': '==', '!=': '!='}
VOLATILE = ['r0'] + ['r%d' % i for i in range(3, 13)] + ['f%d' % i for i in range(0, 14)]


def valid_bits(v):
    if isinstance(v, V) and v.k == 'ld':
        return {'b': 0xff, 'h': 0xffff}.get(v.c, M32)
    return M32


def as_bits(v):
    if isinstance(v, Bits):
        return v
    if isinstance(v, V) and v.k == 'ld' and v.c in ('b', 'h', 'w'):
        return Bits(v, 0, valid_bits(v))
    if isinstance(v, V) and v.k == 'c':
        return Bits(None, 0, M32, const=v.a & M32)
    return Bits(None, 0, M32)


def root_of(v):
    """(root name, base offset) of a pointer value, or None."""
    if not isinstance(v, V):
        return None
    if v.k == 'in':
        return (v.a, 0)
    if v.k == 'sp':
        return ('sp', 0)
    if v.k == 'ptr':
        return (v.a, v.b)
    if v.k == 'ld':
        return (field_name(v.a, v.b), 0)
    if v.k == 'ret':
        return (v.a + '()', 0)
    if v.k == 'sym':
        return (v.a, 0)
    return None


def field_name(root, off, mask=None):
    if root == 'sp':
        s = 'sp+%s' % hx(off)
    else:
        s = ('' if root == 'this' else root + '->') + 'x%x' % off
    if mask is not None:
        s += '_%02x' % mask
    return s


def byte_keys(ld, valmask):
    """{(byte off, byte mask)} for value bits valmask (LSB = bit 0) of load ld."""
    wb = WIDTH.get(ld.c, 4)
    out = defaultdict(int)
    for p in range(wb * 8):
        if valmask >> p & 1:
            out[ld.b + (wb - 1 - p // 8)] |= 1 << (p % 8)
    return dict(out)


def keys_name(root, keys):
    return '|'.join(field_name(root, b, m) for b, m in sorted(keys.items()))


def desc(v, depth=0):
    if v is None:
        return '?'
    if depth > 4:
        return '...'
    if isinstance(v, Bits):
        return bits_expr(v, depth)
    k = v.k
    if k == 'in':
        return v.a
    if k == 'sp':
        return 'sp'
    if k == 'c':
        return hx(v.a)
    if k == 'rod':
        return fmt_float(v.b) if v.b is not None else v.a
    if k == 'ret':
        return base_name(v.a) + '()'
    if k == 'ld':
        return field_name(v.a, v.b)
    if k == 'ptr':
        return '&' + field_name(v.a, v.b)
    if k in ('sym', 'ha'):
        return '&' + v.a
    if k == 'crb':
        cmp, cs = v.a
        return '(%s)' % cmp_text(cmp, cs)
    if k == 'clz':
        return 'cntlzw(%s)' % desc(v.a, depth + 1)
    if k == 'diff':
        return '(%s %s %s)' % (desc(v.a, depth + 1), v.c or '-', desc(v.b, depth + 1))
    return v.a if isinstance(v.a, str) else '?'


def bits_expr(b, depth=0):
    if b.src is None:
        if b.const is not None:
            return hx(b.const)
        return '?'
    x = desc(b.src, depth + 1)
    valid = valid_bits(b.src)
    srcmask = rotl(b.mask, 32 - b.sh) & valid
    if b.mask == 1 and popcount(srcmask) == 1:
        return '(%s & 0x%x) != 0' % (x, srcmask)
    if b.sh == 0:
        return '%s & 0x%x' % (x, b.mask) if b.mask != valid else x
    r = (32 - b.sh) % 32
    if (srcmask >> r) == b.mask:
        return '(%s >> %d) & 0x%x' % (x, r, b.mask)
    if (srcmask << b.sh) & M32 == b.mask:
        return '(%s << %d) & 0x%x' % (x, b.sh, b.mask)
    return 'rotl(%s, %d) & 0x%x' % (x, b.sh, b.mask)


def cmp_text(cmp, cs):
    op = OPS.get(frozenset(cs) & ALL3 if 'so' not in cs else frozenset(cs), None)
    if 'so' in cs:
        return 'unordered(%s, %s)' % (desc(cmp.lhs), desc(cmp.rhs))
    if cmp.kind == 'rec':
        lhs = desc(cmp.lhs)
        if lhs.endswith(' != 0') and op in ('==', '!='):
            core = lhs[:-5]
            return core if op == '!=' else '!' + (core if core.startswith('(') else '(%s)' % core)
        return '%s %s 0' % (lhs, op)
    return '%s %s %s%s' % (desc(cmp.lhs), op, desc(cmp.rhs), ' (unsigned)' if cmp.kind == 'u' else '')


def rlw_decode(op, a):
    """-> (dst, src, sh, mb, me, insert) for the rotate-and-mask family, else None."""
    base = op.rstrip('.')
    try:
        n = [num(x) for x in a[2:]]
        if base == 'rlwinm':
            return a[0], a[1], n[0], n[1], n[2], False
        if base == 'rlwimi':
            return a[0], a[1], n[0], n[1], n[2], True
        if base == 'extrwi':
            return a[0], a[1], (n[1] + n[0]) % 32, 32 - n[0], 31, False
        if base == 'extlwi':
            return a[0], a[1], n[1], 0, n[0] - 1, False
        if base == 'clrlwi':
            return a[0], a[1], 0, n[0], 31, False
        if base == 'clrrwi':
            return a[0], a[1], 0, 0, 31 - n[0], False
        if base == 'slwi':
            return a[0], a[1], n[0], 0, 31 - n[0], False
        if base == 'srwi':
            return a[0], a[1], (32 - n[0]) % 32, n[0], 31, False
        if base == 'rotlwi':
            return a[0], a[1], n[0], 0, 31, False
        if base == 'rotrwi':
            return a[0], a[1], (32 - n[0]) % 32, 0, 31, False
        if base == 'clrlslwi':
            return a[0], a[1], n[1], n[0] - n[1], 31 - n[1], False
        if base == 'inslwi':
            return a[0], a[1], (32 - n[1]) % 32, n[1], n[1] + n[0] - 1, True
        if base == 'insrwi':
            return a[0], a[1], (32 - (n[1] + n[0])) % 32, n[1], n[1] + n[0] - 1, True
    except (TypeError, IndexError):
        return None
    return None


CRBIT_NAMES = {'lt': 0, 'gt': 1, 'eq': 2, 'so': 3, 'un': 3}


def crbit_index(s):
    """CR bit operand -> 0..31, or None for a form we do not know.

    `6`, `cr1eq` (dtk's short form for 4*cr1+eq), `4*cr1+eq` and a bare `eq`/`lt`/`gt`/`so`
    (an unnumbered compare field, 4*cr0+bit) all name one bit."""
    s = s.strip()
    m = re.fullmatch(r'(?:4\s*\*\s*cr(\d)\s*\+\s*|cr(\d))?(lt|gt|eq|so|un)', s)
    if m:
        field = m.group(1) or m.group(2) or 0
        return 4 * int(field) + CRBIT_NAMES[m.group(3)]
    return num(s)


class Analysis:
    def __init__(s, func, mod, sret=False):
        s.f, s.mod, s.sret = func, mod, sret
        s.ann = defaultdict(list)
        s.reads = {}         # idx -> (root, keys, expr)
        s.writes = []        # (idx, root, byte, mask, how, vdesc, store idx)
        s.stores = []        # (idx, root, off, width, V)
        s.loads = []         # (idx, root, off, width)
        s.cmps = []          # Cmp (explicit compares and record forms)
        s.conds = []         # (idx, cmp, condset, exact)
        s.calls = []         # (idx, target)
        s.loops = 0
        s.switches = 0
        s.addr_args = []     # (call idx, root, off) pointers passed to calls
        s.call_args = []     # (call idx, target, {r3..r10: value}, {sp off: value stored there})
        s.spslots = {}       # sp offset -> last value stored there
        s.stored_vals = {}   # id(value) -> (value, {this offsets it was stored to})
        s.taint = {}         # id(value) -> (value, {this offsets}) for arithmetic on field loads
        s.idiom_fields = {}  # this offset -> idx: field in a branchless bool (neg/or/andc/subf; srwi 31)
        s.ret_conds = set()  # cntlzw-idiom conds whose bool is returned as is
        s.run()
        # a cntlzw `x == y` bool is exact only when returned; otherwise its use may negate it
        s.conds = [(a, b, c, (k in s.ret_conds) if d == 'clz' else d) for k, (a, b, c, d) in enumerate(s.conds)]

    # state: (regs, crf, crx)
    def run(s):
        ins = s.f.ins
        n = len(ins)
        labels_at = defaultdict(list)
        for lb, i in s.f.labels.items():
            labels_at[i].append(lb)
        backward = set()
        for i in ins:
            if i.op.startswith('b') and i.args:
                t = i.args[-1]
                if t in s.f.labels and s.f.labels[t] <= i.idx:
                    backward.add(t)
        # a class returned by value is written through a hidden pointer in r3; `this` moves to r4
        regs = {'r1': V('sp'), 'r3': V('in', 'sret' if s.sret else 'this')}
        for k in range(4, 11):
            regs['r%d' % k] = V('in', 'a%d' % (k - 3 - s.sret)) if k > 4 or not s.sret else V('in', 'this')
        for k in range(1, 9):
            regs['f%d' % k] = V('in', 'fa%d' % k)
        st = (regs, {}, {})
        pending = defaultdict(list)
        dead = False
        for i in ins:
            if i.idx in labels_at:
                preds = [] if dead else [st]
                back = False
                for lb in labels_at[i.idx]:
                    preds += pending.pop(lb, [])
                    back |= lb in backward
                s.last = st
                st = s.merge(preds, back)
            dead = False
            st = (dict(st[0]), dict(st[1]), dict(st[2]))
            s.step(i, st, pending)
            if i.op in ('b', 'blr', 'bctr', 'rfi') or i.op.endswith('ctr') and i.op == 'bctr':
                dead = True

    def merge(s, preds, back):
        if not preds:
            return ({r: v for r, v in s.last[0].items() if r[0] in 'rf' and r[1:].isdigit() and int(r[1:]) >= 14},
                    {}, {})
        regs = {}
        first = preds[0]
        for r, v in first[0].items():
            saved = (r[0] == 'r' and r[1:].isdigit() and int(r[1:]) >= 14) or r == 'r1' or \
                    (r[0] == 'f' and r[1:].isdigit() and int(r[1:]) >= 14)
            if all(p[0].get(r) is v for p in preds) and (saved or not back):
                regs[r] = v
            elif saved:
                regs[r] = v if all(p[0].get(r) is v for p in preds) else V('x', r)
            elif not back and all(r in p[0] for p in preds):
                # a forward join of different values (`x = c > x ? x : c`): keep the incoming ones
                regs[r] = V('x', r, d=('phi', tuple(p[0][r] for p in preds)))
        crf = {} if back else {k: v for k, v in first[1].items() if all(p[1].get(k) is v for p in preds)}
        crx = {} if back else {k: v for k, v in first[2].items() if all(p[2].get(k) is v for p in preds)}
        return (regs, crf, crx)

    def crbit(s, st, b):
        if b is None:
            return None         # unparsed condition-bit operand
        crf, crx = st[1], st[2]
        if b in crx:
            return crx[b]
        cmp = crf.get(b // 4)
        if cmp is None:
            return None
        return (cmp, frozenset([COND_NAMES[b % 4]]))

    def set_cr(s, st, field, cmp):
        st[1][field] = cmp
        for b in range(4 * field, 4 * field + 4):
            st[2].pop(b, None)

    def note(s, i, text):
        s.ann[i.idx].append(text)

    def read_candidate(s, i, b):
        if b.src is None or not isinstance(b.src, V) or b.src.k != 'ld':
            return
        valid = valid_bits(b.src)
        srcmask = rotl(b.mask, 32 - b.sh) & valid
        if not srcmask or (srcmask == valid and b.sh == 0):
            return
        if bin(srcmask).count('1') > 8:
            return      # wide extract / index scaling (slwi r3,r5,3), not a flag test
        keys = byte_keys(b.src, srcmask)
        s.reads[i.idx] = (b.src.a, keys, bits_expr(b))

    def step(s, i, st, pending):
        regs = st[0]
        op, a = i.op, i.args
        g = lambda r: regs.get(r) if r != 'r0' or True else None
        rec = op.endswith('.') and op not in ('stwcx.',)
        base = op.rstrip('.')
        # ---- branches and calls
        if op == 'bl':
            t = a[0] if a else '?'
            s.call(i, st, t)
            return
        if op in ('bctrl', 'blrl'):
            r12 = regs.get('r12') if op == 'bctrl' else regs.get('lr')
            t = 'indirect'
            if isinstance(r12, V) and r12.k == 'ld' and (r12.a.endswith('x0') or r12.a.endswith('->x0')):
                obj = r12.a[:-len('->x0')] if r12.a.endswith('->x0') else 'this'
                t = 'vcall(%s)+0x%x' % (obj, r12.b)
            s.call(i, st, t)
            return
        if op == 'b':
            t = a[0] if a else ''
            if t in s.f.labels:
                pending[t].append((dict(regs), dict(st[1]), dict(st[2])))
            else:
                s.call(i, st, t)
            return
        if op in ('blr', 'bctr', 'rfi'):
            if op == 'bctr':
                s.switches += 1
            r3 = regs.get('r3')
            if op == 'blr' and isinstance(r3, V) and r3.k == 'crb' and r3.c is not None:
                s.ret_conds.add(r3.c)
            return
        if op in ('bdnz', 'bdz', 'bdnzt', 'bdnzf', 'bdzt', 'bdzf'):
            s.loops += 1
            if a and a[-1] in s.f.labels:
                pending[a[-1]].append((dict(regs), dict(st[1]), dict(st[2])))
            return
        m = BC_RE.match(op)
        if m:
            cr = 0
            args = list(a)
            if args and re.fullmatch(r'cr\d', args[0]):
                cr = int(args[0][2:])
                args = args[1:]
            bit, pol = CONDS[m.group(1)]
            cb = s.crbit(st, 4 * cr + bit)
            target = args[0] if args else ('return' if m.group(2) == 'lr' else m.group(2) or '?')
            if cb:
                cmp, cs = cb
                cs = cs if pol else ALL3 - cs
                s.conds.append((i.idx, cmp, cs, False))
                s.note(i, 'if (%s) goto %s' % (cmp_text(cmp, cs), target))
            else:
                s.conds.append((i.idx, None, None, False))
            if target in s.f.labels:
                pending[target].append((dict(regs), dict(st[1]), dict(st[2])))
            if m.group(3):
                s.call(i, st, 'cond-call')
            return
        if base in ('bc', 'bcl', 'bclr', 'bcctr'):
            s.conds.append((i.idx, None, None, False))
            return
        # ---- compares
        if base in ('cmpw', 'cmplw', 'cmpwi', 'cmplwi', 'cmp', 'cmpl', 'cmpi', 'cmpli', 'fcmpu', 'fcmpo'):
            args = list(a)
            field = 0
            if args and re.fullmatch(r'cr\d', args[0]):
                field = int(args[0][2:])
                args = args[1:]
            if base in ('cmp', 'cmpl', 'cmpi', 'cmpli') and len(args) == 3:
                args = args[1:]
            lhs = regs.get(args[0], V('x', args[0]))
            if base.endswith('i'):
                v = num(args[1])
                rhs = V('c', v if v is not None else 0) if v is not None else V('x', args[1])
            else:
                rhs = regs.get(args[1], V('x', args[1]))
            kind = 'f' if base.startswith('f') else ('u' if 'l' in base[3:] else 's')
            cmp = Cmp(i.idx, lhs, rhs, kind)
            s.cmps.append(cmp)
            s.set_cr(st, field, cmp)
            s.note(i, 'cr%d = cmp(%s, %s)%s' % (field, desc(lhs), desc(rhs), ' unsigned' if kind == 'u' else ''))
            return
        # ---- CR logic
        if base in ('cror', 'crnor', 'crand', 'crandc', 'crxor', 'creqv', 'crorc', 'crnand', 'crset', 'crclr',
                    'crnot', 'crmove'):
            try:
                bt = crbit_index(a[0])
                ba = crbit_index(a[1]) if len(a) > 1 else bt
                bb = crbit_index(a[2]) if len(a) > 2 else ba
            except IndexError:
                return
            if bt is None:
                return          # destination bit not understood: record nothing
            if base in ('crset', 'crclr'):
                st[2].pop(bt, None)     # a constant bit: no compare behind it
                s.note(i, 'cr%d%s = %d' % (bt // 4, COND_NAMES[bt % 4], 1 if base == 'crset' else 0))
                return
            A, B = s.crbit(st, ba), s.crbit(st, bb)
            res = None
            if A and B and A[0] is B[0]:
                x, y = A[1], B[1]
                res = {'cror': x | y, 'crmove': x, 'crnor': ALL3 - (x | y), 'crnot': ALL3 - x, 'crand': x & y,
                       'crandc': x - y, 'crxor': x ^ y, 'creqv': ALL3 - (x ^ y), 'crorc': x | (ALL3 - y),
                       'crnand': ALL3 - (x & y)}[base]
                res = (A[0], frozenset(res))
            if res:
                st[2][bt] = res
                s.note(i, 'crbit %d = %s' % (bt, cmp_text(res[0], res[1])))
            else:
                st[2].pop(bt, None)
            return
        if base == 'mfcr':
            regs[a[0]] = V('crs', (dict(st[1]), dict(st[2])))
            return
        # ---- loads
        if base in LOADS:
            w = LOADS[base]
            dst = a[0]
            mm = re.fullmatch(r'(.*)\((r\d+)\)', a[1]) if len(a) > 1 else None
            val = V('x', base)
            if mm:
                disp, br = mm.group(1), mm.group(2)
                lab = re.fullmatch(r'"?([^"@+]+)"?(?:\+(0x[0-9a-fA-F]+|\d+))?@(l|sda21|sda2|ha)', disp)
                bv = regs.get(br)
                if lab:
                    label, delta = lab.group(1), int(lab.group(2) or '0', 0)
                    rv = rod_value(s.mod, label, delta, w if w in ('f', 'd') else 'w')
                    val = V('rod', label, rv, delta)
                    if w in ('f', 'd'):
                        s.note(i, '%s = %s (%s%s)' % (dst, fmt_float(rv) if rv is not None else '?', label,
                                                      '+%s' % hx(delta) if delta else ''))
                    elif rv is not None:
                        s.note(i, '%s = %s (%s)' % (dst, rv if isinstance(rv, str) else hx(rv), label))
                else:
                    d = num(disp)
                    ro = root_of(bv) if d is not None else None
                    if ro:
                        val = V('ld', ro[0], ro[1] + d, w, i.idx)
                        s.loads.append((i.idx, ro[0], ro[1] + d, WIDTH[w]))
                    elif d is not None and br == 'r0':
                        val = V('ld', 'abs', d, w, i.idx)
            if base.endswith('u') and mm:
                regs[mm.group(2)] = V('x', 'upd')
            regs[dst] = val
            return
        if base in STORES:
            mm = re.fullmatch(r'(.*)\((r\d+)\)', a[1]) if len(a) > 1 else None
            if not mm:
                return
            d = num(mm.group(1))
            ro = root_of(regs.get(mm.group(2))) if d is not None else None
            if base.endswith('u') and mm.group(2) == 'r1':
                return
            if not ro:
                return
            root, off, w = ro[0], ro[1] + d, STORES[base]
            v = regs.get(a[0])
            s.stores.append((i.idx, root, off, w, v))
            if root == 'sp':
                s.spslots[off] = v
                return
            if root == 'this' and isinstance(v, V) and v.k == 'x':
                todo, seen = [v], 0
                while todo and seen < 8:
                    u = todo.pop()
                    seen += 1
                    if isinstance(u, V) and u.k == 'x':
                        s.stored_vals.setdefault(id(u), (u, set()))[1].add(off)
                        if u.d and u.d[0] == 'phi':
                            todo += list(u.d[1])
            if isinstance(v, Bits) and v.sh == 0 and (v.mods or v.src is not None):
                src = v.src
                same = (src is not None and isinstance(src, V) and src.k == 'ld' and src.a == root and
                        src.b == off and WIDTH.get(src.c) == w)
                tgt = src if same else V('ld', root, off, {1: 'b', 2: 'h', 4: 'w'}.get(w, 'w'))
                valid = valid_bits(tgt)
                for (mask, how, vd, midx) in v.mods:
                    for byte, bm in byte_keys(tgt, mask & valid).items():
                        s.writes.append((midx, root, byte, bm, how, vd, i.idx))
                    s.ann[midx].append('-> stored to %s at 0x%X' % (field_name(root, off), i.off))
                if same:
                    clr = valid & ~v.mask
                    for idx in v.idxs:
                        s.reads.pop(idx, None)
                    if clr:
                        cidx = v.idxs[-1] if v.idxs else i.idx
                        for byte, bm in byte_keys(tgt, clr).items():
                            s.writes.append((cidx, root, byte, bm, 'clr', '0', i.idx))
                        s.ann[cidx].append('%s &= ~0x%x -> stored at 0x%X' % (field_name(root, off), clr, i.off))
                elif src is None and v.const is not None:
                    rest = valid & ~sum(mk for mk, _, _, _ in v.mods)
                    for byte, bm in byte_keys(tgt, rest).items():
                        s.writes.append((i.idx, root, byte, bm, 'const', hx(v.const), i.idx))
                if v.mods:
                    s.note(i, 'store %s (bit writes above)' % field_name(root, off))
            else:
                s.note(i, '%s = %s' % (field_name(root, off), desc(v)))
            return
        if base in ('stmw', 'lmw', 'stswi', 'lswi', 'dcbf', 'dcbi', 'dcbst', 'dcbz', 'icbi', 'sync', 'isync',
                    'mtlr', 'mtctr', 'mflr', 'mtspr', 'mfspr', 'mtfsf', 'mtfsb0', 'mtfsb1', 'nop', 'eieio'):
            if base == 'mtctr' and a:
                regs['ctr'] = regs.get(a[0])
            if base == 'mtlr' and a:
                regs['lr'] = regs.get(a[0])
            if base == 'mflr' and a:
                regs[a[0]] = V('x', 'lr')
            return
        # ---- value ops
        dst = a[0] if a else None
        val = None
        pre = [regs.get(x) for x in a[1:] if re.fullmatch(r'r\d+', x)] if base in TAINT_OPS else []
        if base == 'li':
            v = num(a[1])
            val = V('c', v) if v is not None else V('x', a[1])
        elif base == 'lis':
            if '@h' in a[1]:
                val = V('ha', a[1].split('@')[0].strip('"'))
            else:
                v = num(a[1])
                val = V('c', (v << 16) & M32) if v is not None else V('x', a[1])
        elif base in ('addi', 'subi', 'addic'):
            src = regs.get(a[1]) if a[1] != 'r0' or base != 'addi' else V('c', 0)
            imm = num(a[2])
            if imm is None:
                lab = re.fullmatch(r'"?([^"@+]+)"?(?:\+(0x[0-9a-fA-F]+|\d+))?@(l|sda21)', a[2])
                val = V('sym', lab.group(1)) if lab else V('x', a[2])
            else:
                if base == 'subi':
                    imm = -imm
                if isinstance(src, V) and src.k == 'c':
                    val = V('c', (src.a + imm) & M32)
                else:
                    ro = root_of(src)
                    val = V('ptr', ro[0], ro[1] + imm) if ro and base == 'addi' or ro and base == 'subi' else \
                        V('x', '%s + %s' % (desc(src), hx(imm)))
                    if base == 'addic' and ro and ro[0] in ('this', 'sp') and imm > 0:
                        val = V('ptr', ro[0], ro[1] + imm)  # `addic. rX, this, off`: placement new of a member
                    elif base in ('subi', 'addic') or (isinstance(src, V) and src.k in ('ld', 'ret', 'x')):
                        val = V('x', '%s %s %s' % (desc(src), '-' if imm < 0 else '+', hx(abs(imm))))
                        val.d = ('add', src, imm)
        elif base in ('addis',):
            val = V('x', 'addis')
        elif base in ('mr', 'fmr', 'frsp', 'extsb', 'extsh', 'fabs'):
            val = regs.get(a[1], V('x', a[1]))
        elif base == 'subf':
            val = V('diff', regs.get(a[2]), regs.get(a[1]), '-')
        elif base == 'subfic':
            val = V('diff', V('c', num(a[2]) or 0), regs.get(a[1]), '-')
        elif base == 'xor':
            val = V('diff', regs.get(a[1]), regs.get(a[2]), '^')
        elif base == 'cntlzw':
            val = V('clz', regs.get(a[1]))
        elif base in ('ori', 'oris', 'xori', 'xoris') and len(a) == 3:
            if base == 'ori' and a[0] == a[1] == 'r0' and num(a[2]) == 0:
                return
            src = regs.get(a[1])
            imm = (num(a[2]) or 0) << (16 if base.endswith('is') else 0)
            if base.startswith('x') and isinstance(src, V) and src.k == 'crb' and imm == 1:
                cmp, cs = src.a
                val = V('crb', (cmp, ALL3 - cs), src.b)
                s.note(i, '%s = !%s -> (%s)' % (dst, desc(src), cmp_text(cmp, ALL3 - cs)))
                s.conds.append((i.idx, cmp, ALL3 - cs, True)) if False else None
            elif base.startswith('o') and (isinstance(src, Bits) and src.sh == 0 or
                                           isinstance(src, V) and src.k in ('ld', 'c')):
                b = as_bits(src)
                if b.const is not None and b.src is None:
                    val = V('c', b.const | imm)
                else:
                    val = Bits(b.src, 0, b.mask, b.mods + [(imm, 'set', '1', i.idx)], b.idxs, b.const)
                    root = b.src.a if b.src is not None else '?'
                    nm = keys_name(root, byte_keys(b.src, imm & valid_bits(b.src))) if b.src is not None else '?'
                    s.note(i, 'mask 0x%x: %s |= 0x%x  [set %s]' % (imm, desc(b.src), imm, nm))
            elif base.startswith('x') and isinstance(src, Bits) and src.mask == 1 and imm == 1:
                val = V('x', '!(%s)' % bits_expr(src))
                s.note(i, '%s = !(%s)' % (dst, bits_expr(src)))
            else:
                val = V('x', '%s %s 0x%x' % (desc(src), '|' if base[0] == 'o' else '^', imm))
        elif base in ('andi', 'andis'):
            src = regs.get(a[1])
            imm = (num(a[2]) or 0) << (16 if base == 'andis' else 0)
            if isinstance(src, V) and src.k == 'crs':
                val = V('x', 'cr & 0x%x' % imm)
            else:
                b = as_bits(src)
                val = Bits(b.src, b.sh, b.mask & imm, b.mods if b.sh == 0 else [], b.idxs + [i.idx])
                s.read_candidate(i, val)
                s.note(i, 'mask 0x%x: %s%s' % (imm, bits_expr(val), s.key_note(val)))
        elif base == 'andc':
            src, mk = regs.get(a[1]), regs.get(a[2])
            if isinstance(mk, V) and mk.k == 'c' and (isinstance(src, Bits) or isinstance(src, V) and src.k == 'ld'):
                b = as_bits(src)
                val = Bits(b.src, 0, b.mask & ~mk.a & M32, b.mods, b.idxs + [i.idx])
                s.note(i, 'mask ~0x%x: %s &= ~0x%x' % (mk.a, desc(b.src), mk.a))
            else:
                val = V('x', 'andc')
        elif base == 'or' and len(a) == 3:
            x, y = regs.get(a[1]), regs.get(a[2])
            if a[1] == a[2]:
                val = x
            elif isinstance(x, Bits) and isinstance(y, Bits) and x.src is None and y.src is None:
                val = Bits(None, 0, M32, x.mods + y.mods, [], (x.const or 0) | (y.const or 0))
            elif isinstance(y, V) and y.k == 'c' and (isinstance(x, Bits) or isinstance(x, V) and x.k == 'ld'):
                b = as_bits(x)
                val = Bits(b.src, 0, b.mask, b.mods + [(y.a, 'set', '1', i.idx)], b.idxs)
            else:
                val = V('x', '%s | %s' % (desc(x), desc(y)))
        else:
            d = rlw_decode(op, a)
            if d:
                val = s.rotate(i, st, d)
            elif dst and re.fullmatch(r'[rf]\d+', dst):
                val = V('x', '%s(%s)' % (base, ', '.join(a[1:])))
        if dst and re.fullmatch(r'[rf]\d+', dst):
            regs[dst] = val if val is not None else V('x', base)
            if pre:
                offs = set()
                for v in pre:
                    offs |= s.taint_of(v)
                if offs:
                    s.taint[id(regs[dst])] = (regs[dst], offs)
            if rec:
                cmp = Cmp(i.idx, regs[dst], V('c', 0), 'rec')
                s.cmps.append(cmp)
                s.set_cr(st, 0, cmp)

    def taint_of(s, v, depth=0):
        """this offsets a value is computed from (field loads through arithmetic)."""
        if depth > 4 or v is None:
            return set()
        if isinstance(v, Bits):
            return s.taint_of(v.src, depth + 1) if v.src is not None else set()
        if v.k == 'ld' and v.a == 'this':
            return {v.b}
        t = s.taint.get(id(v))
        if t and t[0] is v:
            return set(t[1])
        if v.k == 'diff':
            return s.taint_of(v.a, depth + 1) | s.taint_of(v.b, depth + 1)
        if v.k == 'x' and v.d and v.d[0] == 'add':
            return s.taint_of(v.d[1], depth + 1)
        if v.k == 'clz':
            return s.taint_of(v.a, depth + 1)
        return set()

    def key_note(s, b):
        if not isinstance(b, Bits) or b.src is None or not isinstance(b.src, V) or b.src.k != 'ld':
            return ''
        srcmask = rotl(b.mask, 32 - b.sh) & valid_bits(b.src)
        if not srcmask:
            return ''
        return '  [%s]' % keys_name(b.src.a, byte_keys(b.src, srcmask))

    def rotate(s, i, st, d):
        dst, srcr, sh, mb, me, insert = d
        regs = st[0]
        src = regs.get(srcr)
        M = mask32(mb, me)
        if not insert:
            if M == 1 and sh == 1 and not (isinstance(src, V) and src.k == 'ld'):
                # srwi 31 of arithmetic on a field: a branchless compare (x != 0, x > 0, x != y)
                for off in s.taint_of(src):
                    s.idiom_fields.setdefault(off, i.idx)
            if isinstance(src, V) and src.k == 'crs':
                if M == 1:
                    bit = (sh - 1) % 32
                    crf, crx = src.a
                    cb = crx.get(bit) or ((crf[bit // 4], frozenset([COND_NAMES[bit % 4]])) if bit // 4 in crf else None)
                    if cb:
                        s.conds.append((i.idx, cb[0], cb[1], True))
                        s.note(i, 'cr%d.%s -> %s' % (bit // 4, COND_NAMES[bit % 4].upper(), cmp_text(cb[0], cb[1])))
                        return V('crb', cb, True)
                    s.note(i, 'CR bit %d (cr%d.%s), compare not found' % (bit, bit // 4, COND_NAMES[bit % 4].upper()))
                return V('x', 'cr bits')
            if isinstance(src, V) and src.k == 'clz' and sh == 27 and mb == 5 and me == 31:
                x = src.a
                if isinstance(x, V) and x.k == 'diff':
                    cmp = Cmp(i.idx, x.a, x.b, 's')
                else:
                    cmp = Cmp(i.idx, x, V('c', 0), 's')
                s.cmps.append(cmp)
                s.conds.append((i.idx, cmp, frozenset(['eq']), 'clz'))
                for off in s.taint_of(x):
                    s.idiom_fields.setdefault(off, i.idx)
                s.note(i, 'cntlzw idiom -> %s' % cmp_text(cmp, frozenset(['eq'])))
                return V('crb', (cmp, frozenset(['eq'])), True, len(s.conds) - 1)
            if isinstance(src, V) and src.k == 'crb' and M == 1 and sh == 0:
                return src
            b = as_bits(src)
            if b.src is None and b.const is not None:
                return V('c', rotl(b.const, sh) & M)
            nb = Bits(b.src, (b.sh + sh) % 32, rotl(b.mask, sh) & M, b.mods if sh == 0 else [],
                      (b.idxs + [i.idx]) if sh == 0 else [])
            if b.src is None:
                s.note(i, 'mask 0x%x: (%s)%s' % (M, desc(src), '' if sh == 0 else ' rotl %d' % sh))
                return V('x', '(%s & 0x%x)' % (desc(src), rotl(M, 32 - sh)))
            valid = valid_bits(b.src)
            srcmask = rotl(nb.mask, 32 - nb.sh) & valid
            if srcmask == valid and nb.sh == 0:
                return src  # plain zero-extension of the load
            s.read_candidate(i, nb)
            clear = nb.sh == 0 and popcount(nb.mask & valid) > popcount(valid) // 2
            s.note(i, 'mask 0x%x: %s%s%s' % (srcmask, bits_expr(nb), s.key_note(nb),
                                               '  (clear 0x%x if stored back)' % (valid & ~nb.mask) if clear else ''))
            return nb
        # rlwimi: dst = (rotl(src, sh) & M) | (dst & ~M)
        old = regs.get(dst)
        ob = as_bits(old)
        if isinstance(src, V) and src.k == 'c':
            v = rotl(src.a & M32, sh) & M
            how, vd = ('set', '1') if v == M else ('clr', '0') if v == 0 else ('const', hx(v))
        else:
            how, vd = 'ins', desc(src)
            sb = as_bits(src)
            if sb.src is not None:
                rb = Bits(sb.src, (sb.sh + sh) % 32, rotl(sb.mask, sh) & M)
                s.read_candidate(i, rb)
                vd = bits_expr(rb) if rb.mask != 0 else vd
        if ob.src is None and ob.const is not None and not ob.mods:
            nb = Bits(None, 0, M32, [(M, how, vd, i.idx)], [], ob.const & ~M & M32)
        else:
            nb = Bits(ob.src, ob.sh, ob.mask, (ob.mods if ob.sh == 0 else []) + [(M, how, vd, i.idx)], ob.idxs,
                      ob.const)
        if ob.src is not None and isinstance(ob.src, V) and ob.src.k == 'ld':
            keys = keys_name(ob.src.a, byte_keys(ob.src, M & valid_bits(ob.src)))
            tgt = desc(ob.src)
        else:
            keys, tgt = 'value bits 0x%x' % M, dst
        txt = {'set': '%s |= 0x%x' % (tgt, M), 'clr': '%s &= ~0x%x' % (tgt, M)}.get(
            how, '%s = (%s & ~0x%x) | ((%s) -> 0x%x)' % (tgt, tgt, M, vd, M))
        s.note(i, 'mask 0x%x: %s  [%s %s]' % (M, txt, how, keys))
        return nb

    def call(s, i, st, target):
        regs = st[0]
        s.call_args.append((i.idx, target, {r: regs.get(r) for r in ('r3', 'r4', 'r5', 'r6', 'r7', 'r8', 'r9', 'r10')},
                            dict(s.spslots)))
        for r in ('r3', 'r4', 'r5', 'r6', 'r7', 'r8', 'r9', 'r10'):
            ro = root_of(regs.get(r))
            v = regs.get(r)
            if isinstance(v, V) and v.k in ('ptr', 'in') and ro:
                s.addr_args.append((i.idx, ro[0], ro[1]))
        s.calls.append((i.idx, target))
        for r in VOLATILE:
            regs.pop(r, None)
        regs['r3'] = V('ret', target)
        regs['f1'] = V('ret', target)
        for fld in (0, 1, 5, 6, 7):
            s.set_cr(st, fld, None)
            st[1].pop(fld, None)
        if not target.startswith(('vcall', 'indirect', 'cond-call')):
            s.note(i, 'call %s' % (target if is_runtime(target) else base_name(target)))
        else:
            s.note(i, 'call %s' % target)


# ---------------------------------------------------------------------------------------------
# headers

TYPE_SIZE = {'bool': 1, 'char': 1, 'uchar': 1, 'u8': 1, 's8': 1, 'signed char': 1, 'unsigned char': 1,
             'short': 2, 'ushort': 2, 'u16': 2, 's16': 2, 'unsigned short': 2, 'TUniqueId': 2, 'TEditorId': 4,
             'int': 4, 'uint': 4, 'u32': 4, 's32': 4, 'unsigned int': 4, 'unsigned': 4, 'long': 4,
             'float': 4, 'f32': 4, 'double': 8, 'f64': 8, 'TAreaId': 4, 'CVector3f': 12, 'CTransform4f': 48,
             'CQuaternion': 16, 'CColor': 4, 'CAABox': 24, 'CVector2f': 8, 'CRelAngle': 4, 'CSegId': 1}


class Member:
    def __init__(s, name, typ, bits, comment, line, cls):
        s.name, s.type, s.bits, s.comment, s.line, s.cls = name, typ, bits, comment, line, cls
        s.off = s.mask = None      # stated (comment or name)
        s.src = None
        s.loff = s.lmask = None    # layout-derived (bitfields)
        t = typ.replace('const ', '').strip()
        s.size = 4 if t.endswith('*') or t.endswith('&') else TYPE_SIZE.get(t)

    def pos(s):
        if s.off is not None:
            return s.off, s.mask
        return s.loff, s.lmask


MODULE = None       # module being checked: picks the per-module position in a member comment


def module_label(label):
    """A comment label naming a module (`CIng`, `IngBoostBallGuardian`) -> module name, else None."""
    for cand in (label, label[1:] if label.startswith('C') else None):
        if cand and (cand == MODULE or os.path.isdir(os.path.join(ASM_ROOT, cand))):
            return cand
    return None


def module_comment(c):
    """The part of a member comment that holds this module's position. `;`-separated segments
    may start with a module label (`0xe5e bit 6; IngBoostBallGuardian 0x11ee bit 2`, `CIng: 0xE5C
    bit 3`): the segment labelled with MODULE wins, else the unlabelled segments."""
    if not c or ';' not in c and not re.match(r'^\s*C?[A-Z]\w+\s*:?\s+0x', c):
        return c
    plain, own = [], None
    for seg in c.split(';'):
        m = re.match(r'^\s*(C?[A-Z][A-Za-z0-9]+)\s*:?\s+(?=0x)', seg)
        lab = module_label(m.group(1)) if m else None
        if lab is None:
            plain.append(seg)
        elif lab == MODULE:
            own = seg[m.end():]
    if own is not None:
        return own
    return ';'.join(plain)


def stated_position(name, comment, is_bit):
    c = module_comment(comment or '')
    m = re.search(r'0x([0-9a-fA-F]+)[ _](2[4-9]|3[01])\b', c)
    if m and is_bit:
        return int(m.group(1), 16), 1 << (31 - int(m.group(2))), 'comment'
    m = re.search(r'0x([0-9a-fA-F]+)\s+(?:byte\s+)?bit\s+(\d)\b', c)
    if m and is_bit:
        return int(m.group(1), 16), 1 << int(m.group(2)), 'comment'
    m = re.search(r'0x([0-9a-fA-F]+)\s+(?:byte\s+)?(?:bit\s+|mask\s+|&\s*)?0x([0-9a-fA-F]{1,2})\b', c)
    if m and is_bit and popcount(int(m.group(2), 16)) == 1:
        return int(m.group(1), 16), int(m.group(2), 16), 'comment'
    m = re.match(r'x([0-9a-fA-F]+)_(2[4-9]|3[01])(?:_|$)', name)
    if m and is_bit:
        return int(m.group(1), 16), 1 << (31 - int(m.group(2))), 'name'
    m = re.match(r'x([0-9a-fA-F]+)_(01|02|04|08|10|20|40|80)_', name)
    if m and is_bit:
        return int(m.group(1), 16), int(m.group(2), 16), 'name'
    m = re.match(r'^\s*(?:\(\w+\)\s*)?0x([0-9a-fA-F]+)\b', c)
    if m:
        return int(m.group(1), 16), None, 'comment'
    m = re.match(r'x([0-9a-fA-F]+)(?:_|$)', name)
    if m:
        return int(m.group(1), 16), None, 'name'
    return None


def strip_comments_keep(text):
    """Blank comments, strings and char literals (same length, newlines kept)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j]))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n)
            out.append(c + re.sub(r'[^\n]', ' ', text[i + 1:j - 1]) + c if j - i >= 2 else text[i:j])
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


CLASS_RE = re.compile(r'\b(class|struct)\s+((?:[A-Z_][A-Z0-9_]*\s+)*)(\w+)\s*(?:final\s*)?(:[^{;()]*)?\{')


class HeaderIndex:
    def __init__(s, roots):
        s.roots = [r for r in roots if r and os.path.isdir(r)]
        s.files = {}
        s.classes = {}
        s.parsed = {}
        s.getters = {}

    def add_closure(s, path, limit=4000):
        todo = [os.path.abspath(path)]
        while todo and len(s.files) < limit:
            p = todo.pop()
            if p in s.files:
                continue
            try:
                txt = open(p, errors='replace').read()
            except OSError:
                continue
            s.files[p] = txt
            for inc in re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', txt, re.M):
                for r in [os.path.dirname(p)] + s.roots:
                    q = os.path.normpath(os.path.join(r, inc))
                    if os.path.isfile(q):
                        todo.append(q)
                        break

    def index(s):
        for p, txt in s.files.items():
            code = strip_comments_keep(txt)
            for m in CLASS_RE.finditer(code):
                s.classes.setdefault(m.group(3), []).append((p, m.start(), m))

    def members(s, cls, depth=0):
        """name -> Member for cls and its bases (derived wins)."""
        if cls in s.parsed:
            return s.parsed[cls]
        s.parsed[cls] = {}
        out = {}
        for p, pos, m in s.classes.get(cls, []):
            mem, bases = parse_class(s.files[p], m, cls)
            if not mem and not bases:
                continue
            if depth < 8:
                for b in bases:
                    for k, v in s.members(b, depth + 1).items():
                        out.setdefault(k, v)
            out.update(mem)
            break
        s.parsed[cls] = out
        return out

    def accessors(s, cls, depth=0):
        """getter name -> member name: trivial inline `T Get() const { return mX; }` in the class
        or its bases (derived wins)."""
        if cls in s.getters:
            return s.getters[cls]
        s.getters[cls] = {}
        out = {}
        mem = s.members(cls)
        for p, pos, m in s.classes.get(cls, []):
            mems, bases = parse_class(s.files[p], m, cls)
            if not mems and not bases:
                continue
            if depth < 8:
                for b in bases:
                    out.update(s.accessors(b, depth + 1))
            code = strip_comments_keep(s.files[p])
            body = code[m.end():class_end(code, m.end())]
            for g in re.finditer(r'\b(\w+)\s*\(\s*\)\s*(?:const\s*)?\{\s*return\s+(?:this\s*->\s*)?(\w+)\s*;\s*\}', body):
                if g.group(2) in mem:
                    out[g.group(1)] = g.group(2)
            break
        s.getters[cls] = out
        return out

    def expanded(s, cls):
        """members(cls) plus `m.sub` for each member of a header class type with a stated offset:
        the sub-member's stated offset is added to the member's (mData.mFlags210 = 0x7c0 + 0x210)."""
        key = ('expanded', cls)
        if key in s.parsed:
            return s.parsed[key]
        out = dict(s.members(cls))
        for name, mb in list(out.items()):
            if mb.off is None or mb.bits is not None or mb.type.rstrip().endswith(('*', '&')):
                continue
            t = re.sub(r'<.*', '', mb.type.replace('const ', '')).strip().split('::')[-1]
            if t == cls or t not in s.classes:
                continue
            for sn, sm in s.members(t).items():
                if sm.off is None or '.' in sn:
                    continue
                c = Member(name + '.' + sn, sm.type, sm.bits, sm.comment, sm.line, sm.cls)
                c.off, c.mask, c.src, c.size = mb.off + sm.off, sm.mask, sm.src, sm.size
                out[c.name] = c
        s.parsed[key] = out
        return out


def class_end(code, start):
    depth, i = 1, start
    while i < len(code) and depth:
        if code[i] == '{':
            depth += 1
        elif code[i] == '}':
            depth -= 1
        i += 1
    return i - 1


def parse_class(text, m, cls):
    code = strip_comments_keep(text)
    start = m.end()
    depth, i = 1, start
    while i < len(code) and depth:
        if code[i] == '{':
            depth += 1
        elif code[i] == '}':
            depth -= 1
        i += 1
    bases = re.findall(r'(?:public|private|protected)?\s*(?:virtual\s+)?(?:\w+::)*(\w+)(?:<[^>]*>)?\s*(?:,|$)',
                       (m.group(4) or '')[1:].strip()) if m.group(4) else []
    lines_code = code[start:i - 1].split('\n')
    lines_orig = text[start:i - 1].split('\n')
    line0 = text.count('\n', 0, start) + 1
    mems = []
    d = 0
    for k, (lc, lo) in enumerate(zip(lines_code, lines_orig)):
        if d == 0 and '(' not in lc and lc.strip().endswith(';'):
            mm = re.match(r'^\s*(?:mutable\s+|volatile\s+)?([\w:<>,\s\*&]+?)\s*\b(\w+)\s*(\[[^\]]*\])?\s*'
                          r'(?::\s*(\d+))?\s*;\s*$', lc)
            if mm and not re.match(r'\s*(static|typedef|friend|using|enum|return|class|struct|public|private)\b', lc):
                cpos = lo.find('//')
                comment = lo[cpos + 2:].strip() if cpos >= 0 else ''
                mems.append(Member(mm.group(2), mm.group(1).strip(), int(mm.group(4)) if mm.group(4) else None,
                                   comment, line0 + k, cls))
            elif d == 0 and re.match(r'^\s*\w[\w\s,:<>\*&]*\b\w+\s*:\s*\d+\s*,', lc):
                pass
        d += lc.count('{') - lc.count('}')
        if d < 0:
            d = 0
    # stated positions
    for mb in mems:
        sp = stated_position(mb.name, mb.comment, mb.bits is not None)
        if sp:
            mb.off, mb.mask, mb.src = sp
            if mb.bits and mb.bits > 1 and mb.mask is not None:
                pass
    # bitfield runs, MWCC big-endian MSB-first, anchored on the first stated member
    run = []
    runs = []
    prev_line = None
    for mb in mems + [None]:
        if mb is not None and mb.bits is not None:
            run.append(mb)
            continue
        if run:
            runs.append(run)
        run = []
    for run in runs:
        rel, pos = [], 0
        for mb in run:
            rel.append(pos)
            pos += mb.bits
        anchor = None
        for mb, r in zip(run, rel):
            if mb.off is not None and mb.mask is not None and mb.bits == 1:
                anchor = (mb.off * 8 + (7 - (mb.mask.bit_length() - 1))) - r
                break
        if anchor is None:
            continue
        for mb, r in zip(run, rel):
            ab = anchor + r
            mb.loff = ab // 8
            mb.lmask = 0
            for q in range(ab, ab + mb.bits):
                if q // 8 == mb.loff:
                    mb.lmask |= 1 << (7 - q % 8)
    return {mb.name: mb for mb in mems}, bases


def bit_members(members):
    """(byte, mask) -> [members stated there], (byte, mask) -> [members laid out there]."""
    stated, layout = defaultdict(list), defaultdict(list)
    for mb in members.values():
        if mb.bits is None:
            continue
        if mb.off is not None and mb.mask is not None:
            for b in range(8):
                if mb.mask >> b & 1:
                    stated[(mb.off, 1 << b)].append(mb)
        if mb.loff is not None:
            for b in range(8):
                if mb.lmask >> b & 1:
                    layout[(mb.loff, 1 << b)].append(mb)
    return stated, layout


def hdr_note(key, stated, layout):
    st, ly = stated.get(key, []), layout.get(key, [])
    if st:
        mb = st[0]
        s = 'hdr %s' % mb.name
        flags = []
        if mb.loff is not None and (mb.loff, mb.lmask) != (mb.off, mb.mask):
            flags.append('HDR? layout puts it at x%x_%02x' % (mb.loff, mb.lmask))
        if ly and ly[0] is not mb:
            flags.append('HDR? layout says %s' % ly[0].name)
        return s + (' ' + '; '.join(flags) if flags else '')
    if ly:
        return 'hdr~ %s (layout only)' % ly[0].name
    return 'hdr: none'


# ---------------------------------------------------------------------------------------------
# annotate

def cmd_annotate(args):
    global MODULE
    MODULE = module_name(args.asm)
    mod = load_module(args.asm)
    f = mod.find(args.fn)
    if not f:
        sys.exit('asmcheck: %s not found in %s' % (args.fn, args.asm))
    an = Analysis(f, mod, getattr(args, 'sret', False))
    stated = layout = None
    if args.header:
        hi = HeaderIndex([os.path.dirname(os.path.abspath(args.header))] + (args.I or []))
        hi.add_closure(args.header, limit=1 if not args.I else 4000)
        hi.index()
        cls = args.cls
        if not cls:
            names = [n for p, lst in [(None, None)] for n in hi.classes]
            cls = names[0] if names else None
        mem = hi.members(cls) if cls else {}
        if not mem:
            print('# header: class %s not found in %s' % (cls, args.header))
        stated, layout = bit_members(mem)
    print('## %s  0x%X..0x%X  (%s)' % (f.name, f.start, f.end, os.path.basename(f.path)))
    inv = defaultdict(list)
    for lb, i in f.labels.items():
        inv[i].append(lb)
    rd = {}
    for idx, (root, keys, expr) in an.reads.items():
        rd[idx] = (root, keys)
    wr = defaultdict(list)
    for (idx, root, byte, bm, how, vd, sidx) in an.writes:
        wr[idx].append((root, byte, bm, how, vd))
    for i in f.ins:
        notes = list(an.ann.get(i.idx, []))
        hdr = []
        if stated is not None:
            if i.idx in rd and rd[i.idx][0] == 'this':
                for b, m in sorted(rd[i.idx][1].items()):
                    for k in range(8):
                        if m >> k & 1:
                            hdr.append('x%x_%02x %s' % (b, 1 << k, hdr_note((b, 1 << k), stated, layout)))
            for root, byte, bm, how, vd in wr.get(i.idx, []):
                if root == 'this':
                    hdr.append('x%x_%02x %s' % (byte, bm, hdr_note((byte, bm), stated, layout)))
        if i.idx in rd and not any('mask' in x for x in notes):
            notes.append('read %s' % keys_name(*rd[i.idx]))
        for root, byte, bm, how, vd in wr.get(i.idx, []):
            notes.append('%s %s%s' % ({'set': 'SET', 'clr': 'CLEAR', 'ins': 'ASSIGN', 'const': 'STORE'}[how],
                                      field_name(root, byte, bm), '' if how in ('set', 'clr') else ' = ' + vd))
        if notes or args.all_ins:
            for lb in inv.get(i.idx, []):
                if args.all_ins:
                    print(lb + ':')
            line = '0x%X  %-34s' % (i.off, i.text)
            if notes:
                line += ' ; ' + ' ; '.join(dict.fromkeys(notes))
            if hdr:
                line += ' ; ' + ' ; '.join(dict.fromkeys(hdr))
            print(line.rstrip())
    return 0


# ---------------------------------------------------------------------------------------------
# C++ side

TOK_RE = re.compile(r'\s*(0[xX][0-9a-fA-F]+[uUlL]*|\d+\.\d*(?:[eE][-+]?\d+)?[fF]?|\.\d+(?:[eE][-+]?\d+)?[fF]?|'
                    r'\d+(?:[eE][-+]?\d+)?[fFuUlL]*|[A-Za-z_]\w*|->|::|<<=|>>=|<=|>=|==|!=|&&|\|\||\+\+|--|'
                    r'\+=|-=|\*=|/=|%=|&=|\|=|\^=|<<|>>|\S)')
KEYWORDS = {'if', 'else', 'while', 'for', 'do', 'switch', 'case', 'default', 'return', 'break', 'continue',
            'sizeof', 'static_cast', 'reinterpret_cast', 'const_cast', 'dynamic_cast', 'new', 'delete', 'this',
            'true', 'false', 'const', 'static', 'goto', 'nullptr', 'NULL', 'void', 'int', 'float', 'bool',
            'uint', 'char', 'unsigned', 'signed', 'short', 'long', 'double', 'operator', 'template', 'typename',
            'struct', 'class', 'union', 'enum', 'auto', 'volatile', 'inline', 'u32', 's32', 'u8', 's8', 'u16',
            's16', 'f32', 'uchar', 'ushort', 'TARGET_PC', 'defined', 'assert', 'ASSERT', 'OSReport'}
CMPOPS = {'==', '!=', '<', '>', '<=', '>='}
ASSIGN = {'=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>='}


def lex(code, line0):
    toks = []
    pos = 0
    line = line0
    for m in TOK_RE.finditer(code):
        line += code.count('\n', pos, m.start(1))
        pos = m.start(1)
        toks.append((m.group(1), line))
    return toks


def drop_templates(toks):
    out = []
    i, n = 0, len(toks)
    allowed = re.compile(r'^([A-Za-z_]\w*|::|,|\*|&|\d+|<|>|>>)$')
    while i < n:
        t = toks[i][0]
        if t == '<' and out and re.match(r'^[A-Za-z_]\w*$', out[-1][0]) and out[-1][0] not in KEYWORDS - {
                'static_cast', 'reinterpret_cast', 'const_cast', 'dynamic_cast'}:
            depth, j, ok = 1, i + 1, False
            while j < n and j - i < 40:
                tj = toks[j][0]
                if not allowed.match(tj):
                    break
                if tj == '<':
                    depth += 1
                elif tj == '>':
                    depth -= 1
                elif tj == '>>':
                    depth -= 2
                if depth <= 0:
                    nxt = toks[j + 1][0] if j + 1 < n else ';'
                    ok = nxt in ('(', '::', '{', '*', '&', ')', ',', '>', ';') or re.match(r'^[A-Za-z_]', nxt)
                    break
                j += 1
            if ok:
                i = j + 1
                continue
        out.append(toks[i])
        i += 1
    return out


def num_literal(t):
    t2 = t.rstrip('uUlLfF') if not t.lower().startswith('0x') else t.rstrip('uUlL')
    try:
        if t2.lower().startswith('0x'):
            return int(t2, 16)
        if re.fullmatch(r'\d+', t2):
            return int(t2)
        return float(t2)
    except ValueError:
        return None


class CppFunc:
    def __init__(s):
        s.name = s.cls = s.short = None
        s.file = None
        s.line = s.end_line = 0
        s.ids = []
        s.foreign = set()
        s.sret = False
        s.marked = False
        s.comment_lines = []   # (line, text) comment block above + comments in body
        s.code = ''
        s.code_line0 = 0
        s.init_end = 0         # token index where body '{' starts
        s.sig_ids = []         # fn ids in a trailing comment on the signature (fn -> def map only)
        s.call_ids = set()     # fn ids a body comment names as called (`// fn_X` on/above a call)
        s.debug_lines = set()  # lines inside a debug/logging #if


FNID_RE = re.compile(r'\bfn_[0-9A-Za-z]+_[0-9A-Fa-f]+\b')
DEBUG_PP_RE = re.compile(r'(?i)debug|verbose|trace|logging|\blog')


def debug_pp_lines(lines):
    """Line numbers inside a debug/logging-only #if/#ifdef (TARGET_PC blocks are port code, kept)."""
    out, stack = set(), []      # stack of [this branch is debug, the #if names a debug macro, negated]
    for ln, t in enumerate(lines, 1):
        m = re.match(r'^\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)$', t)
        if m:
            d, expr = m.group(1), m.group(2)
            if d in ('if', 'ifdef', 'ifndef'):
                dbg = bool(DEBUG_PP_RE.search(expr)) and 'TARGET_PC' not in expr
                neg = d == 'ifndef' or (d == 'if' and re.match(r'\s*!', expr) is not None)
                stack.append([dbg and not neg, dbg, neg])
            elif d in ('else', 'elif') and stack:
                stack[-1][0] = stack[-1][1] and stack[-1][2]
            elif d == 'endif' and stack:
                stack.pop()
            continue
        if any(e[0] for e in stack):
            out.add(ln)
    return out


def drop_non_pc_arms(code):
    """Blank (keeping newlines) the arm a PC build does not compile of a plain TARGET_PC
    conditional: the `#else` of `#ifdef TARGET_PC` / `#if defined(TARGET_PC)`, the `#if` arm of
    `#ifndef TARGET_PC` / `#if !defined(TARGET_PC)`. Both arms usually open braces the other
    closes, which otherwise ends a body at the wrong line."""
    out, stack, drop = [], [], False   # stack: (kind, dropping before) ; kind 'pc'/'npc'/'other'
    for line in code.split('\n'):
        m = re.match(r'^[ \t]*#[ \t]*(\w+)(.*)$', line)
        if m:
            d, rest = m.group(1), m.group(2).strip()
            if d in ('if', 'ifdef', 'ifndef'):
                if d == 'ifdef' and rest == 'TARGET_PC' or re.fullmatch(r'defined\s*\(?\s*TARGET_PC\s*\)?', rest):
                    kind = 'pc'
                elif d == 'ifndef' and rest == 'TARGET_PC' or re.fullmatch(r'!\s*defined\s*\(?\s*TARGET_PC\s*\)?', rest):
                    kind = 'npc'
                else:
                    kind = 'other'
                stack.append((kind, drop))
                if kind == 'npc':
                    drop = True
                out.append(line)
                continue
            if d in ('else', 'elif') and stack:
                kind, before = stack[-1]
                if kind == 'pc':
                    drop = True
                elif kind == 'npc':
                    drop = before
                out.append(line)
                continue
            if d == 'endif' and stack:
                drop = stack.pop()[1]
                out.append(line)
                continue
        out.append(re.sub(r'\S', ' ', line) if drop else line)
    return '\n'.join(out)


def _marker_in_if(lines, k):
    """lines[k] is an `#endif`: only comments sit between it and the `#if` that opens it."""
    j = k - 1
    while j >= 0 and (lines[j].strip().startswith(('//', '/*', '*')) or not lines[j].strip()):
        j -= 1
    return j >= 0 and re.match(r'^\s*#\s*if', lines[j]) is not None


def parse_cpp(path, deep=False):
    """Function definitions of a C++ file. deep=True (retail_diff): also bodies defined inside a class
    (named Class::name; access labels skipped) and `extern "C" void f() {...}` (not a linkage block)."""
    text = open(path, errors='replace').read()
    code = strip_comments_keep(drop_non_pc_arms(text))
    dbg_lines = debug_pp_lines(text.split('\n'))
    # blank preprocessor lines (same length) so `#include` / `#if` never join a definition head
    code = re.sub(r'(?m)^[ \t]*#.*$', lambda m: ' ' * len(m.group(0)), code)
    lines = text.split('\n')
    starts = [0] + [m.end() for m in re.finditer('\n', text)]
    funcs = []
    stack = []
    i, n = 0, len(code)
    stmt_start = 0
    while i < n:
        c = code[i]
        if c in ';':
            stmt_start = i + 1
        elif c == '{':
            head = code[stmt_start:i]
            eff = sum(1 for t in stack if t != 'ns')
            clsnames = [t[4:] for t in stack if t != 'ns']
            incls = deep and eff > 0 and all(t.startswith('cls:') for t in stack if t != 'ns')
            if deep:
                head = re.sub(r'^(\s*)extern\s*"[^"]*"(?=\s*[\w])', lambda m: m.group(1) + ' ' * (len(m.group(0)) - len(m.group(1))), head)
                head = re.sub(r'(?<![\w:])(?:public|private|protected)\s*:(?!:)', lambda m: ' ' * len(m.group(0)), head)
            if eff == 0 and re.search(r'\bnamespace\b|\bextern\s*"\s*"', head) and not (deep and '(' in head):
                stack.append('ns')
                stmt_start = i + 1
                i += 1
                continue
            is_fn = False
            if eff == 0 or incls:
                h = head.strip()
                mm = re.match(r'^(?:template\s*<[^>]*>\s*)?(?:[\w:<>,\s\*&~]+?\s+|)\**&?((?:\w+::)*~?\w+|(?:\w+::)*operator\s*[^\s(]+)\s*\(',
                              h, re.S)
                if mm and not re.match(r'^(class|struct|enum|union|namespace)\b', h) and '=' not in h.split('(')[0] \
                        and mm.group(1) not in ('if', 'while', 'for', 'switch'):
                    is_fn = True
            if is_fn and re.search(r'[\w>]\s*$', head):
                # a ctor initializer list's brace member (`, mUids{a, b}`), not the body: skip it
                d, q = 0, h.find('(', mm.end(1))
                while 0 <= q < len(h):
                    d += {'(': 1, ')': -1}.get(h[q], 0)
                    if d == 0:
                        break
                    q += 1
                if 0 <= q < len(h) and re.match(r'\s*:(?!:)', h[q + 1:]):
                    d, j = 0, i
                    while j < n:
                        d += {'{': 1, '}': -1}.get(code[j], 0)
                        j += 1
                        if d == 0:
                            break
                    i = j
                    continue
            if is_fn:
                depth, j = 1, i + 1
                while j < n and depth:
                    if code[j] == '{':
                        depth += 1
                    elif code[j] == '}':
                        depth -= 1
                    j += 1
                hs = stmt_start + (len(head) - len(head.lstrip()))
                f = CppFunc()
                f.file = path
                f.name = mm.group(1).replace(' ', '')
                if incls:
                    f.name = '::'.join(clsnames) + '::' + f.name
                parts = f.name.split('::')
                f.short = parts[-1]
                rt = re.sub(r'\b(static|inline|virtual|const|explicit|extern|template\s*<[^>]*>)\b', ' ', h[:mm.start(1)]).strip()
                last = re.findall(r'\w+', rt.split('<')[0])
                f.sret = bool(rt) and not re.search(r'[*&]\s*$', rt) and bool(last) and \
                    ('<' in rt or re.match(r'^[CST][A-Z]', last[-1]) is not None)
                f.cls = parts[-2] if len(parts) > 1 else None
                f.line = text.count('\n', 0, hs) + 1
                f.end_line = text.count('\n', 0, j) + 1
                f.code = code[hs:j]
                f.code_line0 = f.line
                f.body_off = i - hs
                # comment block above
                k = f.line - 2
                block = []
                while k >= 0:
                    s = lines[k].strip()
                    if s.startswith('//') or s.startswith('/*') or s.startswith('*') or s.endswith('*/'):
                        block.append((k + 1, lines[k]))
                        k -= 1
                    elif s.startswith('template') or (s.startswith('#') and 'TARGET_PC' not in s and False):
                        k -= 1
                    elif deep and re.match(r'^#\s*endif\b', s) and _marker_in_if(lines, k):
                        k -= 1      # `#ifdef TARGET_PC / // @ fn_X / #endif` right above the definition
                    else:
                        break
                block.reverse()
                f.block = block
                for ln, t in block:
                    mk = re.match(r'^\s*//\s*@\s*(fn_\w+)', t)
                    if mk:
                        f.ids.append(mk.group(1))
                        f.marked = True
                if not f.ids:
                    for ln, t in block:
                        ids = re.findall(r'\bfn_[0-9A-Za-z]+_[0-9A-Fa-f]+\b', t)
                        if ids:
                            f.ids = [ids[0]]
                            break
                brace_line = text.count('\n', 0, i) + 1
                body_comments = []
                for ln in range(f.line, f.end_line + 1):
                    t = lines[ln - 1]
                    cp = code_comment_start(code, text, starts, ln)
                    if cp is not None:
                        body_comments.append((ln, t[cp:]))
                        if ln <= brace_line and not f.ids:
                            f.sig_ids += [x for x in FNID_RE.findall(t[cp:]) if x not in f.sig_ids]
                f.comment_lines = [(ln, t) for ln, t in block] + body_comments
                f.debug_lines = {ln for ln in range(f.line, f.end_line + 1) if ln in dbg_lines}
                # a whole-line `// fn_X` comment inside the body says the block below it is retail
                # fn_X (another asm function): its lines are left out of this function's facts.
                # On (or right above) a plain call statement it names the call instead.
                f.foreign = set()
                own = {x.lower() for x in f.ids}
                for ln, t in body_comments:
                    if ln <= brace_line:
                        continue
                    full = lines[ln - 1]
                    mf = re.match(r'^(\s*)//\s*(?:retail\s+)?(fn_[0-9A-Za-z]+_[0-9A-Fa-f]+)\b', full, re.I)
                    if not mf:
                        cpos = len(full) - len(t)
                        if re.search(r'\b\w+\s*\(', code[starts[ln - 1]:starts[ln - 1] + cpos]):
                            f.call_ids.update(x.lower() for x in FNID_RE.findall(t))
                        continue
                    if mf.group(2).lower() in own:
                        continue
                    f.call_ids.add(mf.group(2).lower())
                    nxt = ln + 1
                    while nxt < f.end_line and (not lines[nxt - 1].strip() or lines[nxt - 1].strip().startswith('//')):
                        nxt += 1
                    st = code[starts[nxt - 1]:starts[nxt] - 1 if nxt < len(starts) else len(code)].strip()
                    if re.search(r'\b\w+\s*\(', st) and not re.match(
                            r'^(if|for|while|switch|case|default|do|else|return\b.*\?|\{|\})\b', st):
                        continue
                    ind = len(mf.group(1))
                    for l2 in range(ln + 1, f.end_line):
                        t2 = lines[l2 - 1]
                        st = t2.strip()
                        if not st:
                            continue
                        ind2 = len(t2) - len(t2.lstrip())
                        if ind2 < ind or (ind2 == ind and re.match(r'^(case\b|default\b)', st)):
                            break
                        f.foreign.add(l2)
                        if ind2 == ind and st in ('break;', 'return;'):
                            break
                funcs.append(f)
                i = j
                stmt_start = j
                continue
            cm = deep and (eff == 0 or incls) and re.match(
                r'^\s*(?:template\s*<[^>]*>\s*)?(?:class|struct)\s+(?:[A-Za-z_]\w*\s+)*?(\w+)\s*(?:final\s*)?(?::[^{;()]*)?$', head)
            stack.append('cls:' + cm.group(1) if cm else 'blk')
            stmt_start = i + 1
        elif c == '}':
            if stack:
                stack.pop()
            stmt_start = i + 1
        i += 1
    return funcs


def code_comment_start(code, text, starts, ln):
    a = starts[ln - 1]
    b = starts[ln] - 1 if ln < len(starts) else len(text)
    seg_t, seg_c = text[a:b], code[a:b]
    p = seg_t.find('//')
    while p >= 0:
        if seg_c[p:p + 2] != '//':
            return p
        p = seg_t.find('//', p + 1)
    p = seg_t.find('/*')
    if p >= 0 and seg_c[p:p + 2] != '/*':
        return p
    return None


class CppFacts:
    def __init__(s):
        s.calls = defaultdict(list)     # name -> [line]
        s.qcalls = set()                # names called through another object (x.f(), p->f())
        s.call_tokens = 0
        s.conds = 0
        s.cmps = []                     # (line, op, lhs, rhs, in_for)
        s.reads = defaultdict(list)     # member -> [line]
        s.writes = defaultdict(list)    # member -> [(line, value token)]
        s.touch = defaultdict(list)     # member -> [line] (address taken / method call / sub-field write)
        s.tested = defaultdict(list)    # member -> [line] used in a condition
        s.masks = []                    # (line, value, member or None, kind 'r'|'set'|'clr')
        s.idents = set()


ARITH = ('*', '/', '+', '-', '%', '<<', '>>')


def is_word(t):
    return re.match(r'^[A-Za-z_]\w*$', t) is not None or num_literal(t) is not None


def operand_left(toks, i):
    """Describe the postfix expression ending just before toks[i]."""
    j = i - 1
    chain = []
    while j >= 0:
        t = toks[j][0]
        if chain and is_word(t) and is_word(chain[-1]):
            break       # `return x`, `case X`: two words never sit side by side in one operand
        if t in (')', ']'):
            close, opn, d = t, '(' if t == ')' else '[', 0
            while j >= 0:
                if toks[j][0] == close:
                    d += 1
                elif toks[j][0] == opn:
                    d -= 1
                    if d == 0:
                        break
                j -= 1
            chain.append(close)
            j -= 1
            continue
        if re.match(r'^[A-Za-z_]\w*$', t) or t in ('.', '->', '::') or num_literal(t) is not None:
            chain.append(t)
            j -= 1
            continue
        break
    chain.reverse()
    if j >= 0 and toks[j][0] in ARITH:
        return None
    return describe_chain(chain)


def operand_right(toks, i):
    j = i + 1
    while j < len(toks) and toks[j][0] in ('-', '!', '~', '*', '&', '+'):
        j += 1
    chain = []
    if j < len(toks) and toks[j][0] == '(':
        d = 0
        k = j
        while k < len(toks):
            if toks[k][0] == '(':
                d += 1
            elif toks[k][0] == ')':
                d -= 1
                if d == 0:
                    break
            k += 1
        inner = [t for t, _ in toks[j + 1:k]]
        if len(inner) <= 3:
            chain = [t for t in inner if t not in ('(', ')')]
        return describe_chain(chain)
    while j < len(toks):
        t = toks[j][0]
        if chain and is_word(t) and is_word(chain[-1]):
            break
        if re.match(r'^[A-Za-z_]\w*$', t) or t in ('.', '->', '::') or num_literal(t) is not None:
            chain.append(t)
            j += 1
            continue
        if t in ('(', '['):
            opn, close, d = t, ')' if t == '(' else ']', 0
            while j < len(toks):
                if toks[j][0] == opn:
                    d += 1
                elif toks[j][0] == close:
                    d -= 1
                    if d == 0:
                        break
                j += 1
            chain.append(close)
            j += 1
            continue
        break
    if j < len(toks) and toks[j][0] in ARITH:
        return None
    return describe_chain(chain)


def describe_chain(chain):
    if not chain:
        return None
    if len(chain) == 1 and num_literal(chain[0]) is not None:
        return ('num', num_literal(chain[0]))
    c = list(chain)
    if c[:2] == ['this', '->']:
        c = c[2:]
    if not c:
        return None
    if c[-1] == ')':
        names = [t for t in c if re.match(r'^[A-Za-z_]\w*$', t)]
        return ('call', names[-1] if names else '?', c[0])
    if len(c) == 3 and c[1] == '.' and re.match(r'^[A-Za-z_]\w*$', c[0]) and re.match(r'^[A-Za-z_]\w*$', c[2]):
        return ('id', c[0] + '.' + c[2], True)      # member of a member (expanded members)
    if re.match(r'^[A-Za-z_]\w*$', c[0]):
        return ('id', c[0], len(c) == 1)
    return None


DEBUG_ID_RE = re.compile(r'Debug|DEBUG|^debug|Verbose|VERBOSE')
LOG_FN_RE = re.compile(r'^(?:fprintf|printf|puts|OSReport|\w*Log\w*|Debug\w*)$')


def tok_match(toks, k, opn, close):
    d = 0
    for j in range(k, len(toks)):
        if toks[j][0] == opn:
            d += 1
        elif toks[j][0] == close:
            d -= 1
            if d == 0:
                return j
    return len(toks) - 1


def stmt_end(toks, k):
    d = 0
    for j in range(k, len(toks)):
        t = toks[j][0]
        if t in ('(', '[', '{'):
            d += 1
        elif t in (')', ']', '}'):
            d -= 1
        elif t == ';' and d == 0:
            return j
    return len(toks) - 1


def log_only(body):
    """Every statement of the block is a logging call (fprintf/OSReport/Log*)."""
    toks = [t for t, _ in body]
    if toks and toks[0] == '{':
        toks = toks[1:-1]
    if not toks:
        return False
    k = 0
    while k < len(toks):
        if not (LOG_FN_RE.match(toks[k]) and k + 1 < len(toks) and toks[k + 1] == '('):
            return False
        d = 0
        while k < len(toks) and not (toks[k] == ';' and d == 0):
            d += toks[k] in '([{' and len(toks[k]) == 1
            d -= toks[k] in ')]}' and len(toks[k]) == 1
            k += 1
        k += 1
    return True


def drop_debug_blocks(toks):
    """Drop `if (<debug flag>) ...` and `if (...) { <logging calls only> }` statements."""
    drop, k, n = set(), 0, len(toks)
    while k < n:
        if toks[k][0] == 'if' and k + 1 < n and toks[k + 1][0] == '(':
            p = tok_match(toks, k + 1, '(', ')')
            if p + 1 < n:
                q = tok_match(toks, p + 1, '{', '}') if toks[p + 1][0] == '{' else stmt_end(toks, p + 1)
                cond = [t for t, _ in toks[k + 2:p]]
                dbg = '||' not in cond and any(re.match(r'^[A-Za-z_]', c) and DEBUG_ID_RE.search(c) for c in cond)
                if dbg or log_only(toks[p + 1:q + 1]):
                    drop.update(range(k, q + 1))
                    k = q + 1
                    continue
        k += 1
    return [t for i, t in enumerate(toks) if i not in drop] if drop else toks


def cpp_facts(f, members, getters=None):
    getters = getters or {}
    toks = drop_templates(lex(f.code, f.code_line0))
    if getattr(f, 'foreign', None) or getattr(f, 'debug_lines', None):
        toks = [t for t in toks if t[1] not in f.foreign and t[1] not in f.debug_lines]
    toks = drop_debug_blocks(toks)
    F = CppFacts()
    F.cited_calls = set(getattr(f, 'call_ids', ()))
    n = len(toks)
    body_start = None
    # find the body '{' token: first '{' at paren depth 0 after the parameter list
    d = 0
    for k, (t, ln) in enumerate(toks):
        if t == '(':
            d += 1
        elif t == ')':
            d -= 1
        elif t == '{' and d == 0:
            body_start = k
            break
    body_start = body_start or 0
    cond_parens = []      # stack of (depth, kind) for if/while/switch/for parens
    pdepth = 0
    for k, (t, ln) in enumerate(toks):
        prev = toks[k - 1][0] if k else ''
        nxt = toks[k + 1][0] if k + 1 < n else ''
        if t == '(':
            pdepth += 1
            if prev in ('if', 'while', 'switch', 'for'):
                cond_parens.append((pdepth, prev))
        elif t == ')':
            if cond_parens and cond_parens[-1][0] == pdepth:
                cond_parens.pop()
            pdepth -= 1
        in_cond = bool(cond_parens)
        in_for = any(kd == 'for' for _, kd in cond_parens)
        if k >= body_start and t in ('if', 'while', 'for', '?', '&&', '||', 'case'):
            F.conds += 1
        if t in CMPOPS and k >= body_start:
            F.cmps.append((ln, t, operand_left(toks, k), operand_right(toks, k), in_for))
        if re.match(r'^[A-Za-z_]\w*$', t):
            F.idents.add(t)
            if nxt == '(' and t not in KEYWORDS and k >= body_start - 0:
                if k >= body_start:
                    F.calls[t].append(ln)
                    F.call_tokens += 1
                    if prev in ('.', '->') and not (prev == '->' and k >= 2 and toks[k - 2][0] == 'this'):
                        F.qcalls.add(t)
            unq = prev not in ('.', '->', '::') or (prev == '->' and k >= 2 and toks[k - 2][0] == 'this')
            if t in getters and unq and nxt == '(' and k + 2 < n and toks[k + 2][0] == ')' and k >= body_start:
                # a trivial inline getter reads its member
                g = getters[t]
                F.reads[g].append(ln)
                n3 = toks[k + 3][0] if k + 3 < n else ''
                if in_cond or prev in ('!', '&&', '||', '?') or n3 in ('&&', '||', '?'):
                    F.tested[g].append(ln)
            if t in members and unq:
                kk = k
                if nxt == '.' and k + 2 < n and t + '.' + toks[k + 2][0] in members:
                    t, kk = t + '.' + toks[k + 2][0], k + 2       # member of a member
                    nxt = toks[kk + 1][0] if kk + 1 < n else ''
                if k < body_start:
                    if nxt in ('(', '{') and prev in (':', ','):
                        val = toks[kk + 2][0] if kk + 2 < n else ''
                        F.writes[t].append((ln, val))
                    continue
                if nxt in ASSIGN or nxt in ('++', '--') or prev in ('++', '--'):
                    val = toks[kk + 2][0] if nxt in ASSIGN and kk + 2 < n else nxt
                    F.writes[t].append((ln, val if nxt == '=' else nxt))
                    if nxt in ('|=', '&=', '^=', '+=', '-=', '++', '--') or prev in ('++', '--'):
                        F.reads[t].append(ln)
                elif prev == '&' and (k < 2 or toks[k - 2][0] in ('(', ',', '=', 'return', '?', ':')):
                    F.touch[t].append(ln)
                elif nxt in ('.', '->', '['):
                    # sub-field write or method call on the member
                    j = kk + 1
                    while j < n and toks[j][0] in ('.', '->', '[', ']') or (j < n and re.match(r'^\w+$', toks[j][0])):
                        if toks[j][0] == '[':
                            dd = 0
                            while j < n:
                                if toks[j][0] == '[':
                                    dd += 1
                                elif toks[j][0] == ']':
                                    dd -= 1
                                    if dd == 0:
                                        break
                                j += 1
                        j += 1
                    if j < n and (toks[j][0] in ASSIGN or toks[j][0] == '('):
                        F.touch[t].append(ln)
                    F.reads[t].append(ln)
                else:
                    F.reads[t].append(ln)
                if in_cond or prev in ('!', '&&', '||', '?') or nxt in ('&&', '||', '?'):
                    F.tested[t].append(ln)
        if t in ('&', '|', '^', '&=', '|=', '^=') and k >= body_start:
            if t in ('&', '|', '^') and not (re.match(r'^[\w\)\]]', prev)):
                continue
            j = k + 1
            neg = False
            while j < n and toks[j][0] in ('~', '('):
                neg |= toks[j][0] == '~'
                j += 1
            val = None
            if j < n:
                v = num_literal(toks[j][0])
                if isinstance(v, int):
                    val = v
                    if j + 2 < n and toks[j + 1][0] == '<<' and isinstance(num_literal(toks[j + 2][0]), int):
                        val = v << num_literal(toks[j + 2][0])
            if val is None:
                continue
            lhs = operand_left(toks, k)
            mem = lhs[1] if lhs and lhs[0] == 'id' and lhs[2] and lhs[1] in members else None
            kind = 'r'
            if t == '|=':
                kind = 'set'
            elif t == '&=' and neg:
                kind = 'clr'
            elif t == '&' and neg:
                continue        # x & ~M keeps the other bits; it reads none of M
            elif t in ('&=', '^='):
                kind = 'w'
            F.masks.append((ln, val, mem, kind))
    return F


# ---------------------------------------------------------------------------------------------
# check

INCL = {'<': '<=', '>': '>=', '<=': '<', '>=': '>'}
COMPLETENESS = {'BIT', 'CMP', 'STORE', 'CALL', 'XCALL', 'BRANCH', 'CALLS'}
CORRECTNESS = {'MASK', 'XBIT', 'CROP', 'XCMP', 'XSTORE', 'OFFS', 'FCONST'}
INFO = {'NOPOS'}


# ---- FCONST: float constants the asm loads from rodata vs the float literals of the C++ body
FC_IGNORE = (0.0, 1.0, 0.5)
# MWCC int->float conversion constants, FLT_MAX / FLT_EPSILON (named in C++), byte<->unit colour scales
FC_MAGIC = (4503601774854144.0, 4503599627370496.0, 3.4028234663852886e+38, 1.1920928955078125e-07,
            255.0, 1 / 255.0, 256.0, 1 / 256.0)
FC_DEG = (math.pi / 180.0, 180.0 / math.pi)              # degree <-> radian factors
FLOAT_LIT_RE = re.compile(r'^(\d+\.\d*(?:[eE][-+]?\d+)?[fF]?|\.\d+(?:[eE][-+]?\d+)?[fF]?|\d+(?:[eE][-+]?\d+)[fF]?|\d+[fF])$')
FC_LABEL_RE = re.compile(r'"?([^"@+]+)"?(?:\+(0x[0-9a-fA-F]+|\d+))?@(?:l|sda21|sda2|ha)')


def f32(v):
    try:
        return struct.unpack('>f', struct.pack('>f', v))[0]
    except (OverflowError, struct.error):
        return v


def fc_close(a, b):
    if a == b or f32(a) == f32(b):
        return True
    return abs(a - b) <= 1e-6 * max(abs(a), abs(b))


def fc_ignored(v):
    return v != v or abs(v) in FC_IGNORE or any(fc_close(abs(v), m) for m in FC_MAGIC) or abs(v) == float('inf')


def asm_float_loads(mod, af):
    """[(off, |value|)] first load of each distinct rodata float/double constant of an asm function.
    Both `lfs f, lbl@l(r)` and `addi rX, rY, lbl@l` + `lfs f, off(rX)` (linear scan, any write kills rX)."""
    out, seen = [], []
    addr = {}       # gpr -> (label, delta) from `addi rX, rY, lbl[+d]@l`
    for i in af.ins:
        a = i.args
        base = i.op.rstrip('.')
        if base == 'addi' and len(a) == 3:
            lab = FC_LABEL_RE.fullmatch(a[2])
            if lab and a[2].endswith('@l'):
                addr[a[0]] = (lab.group(1), int(lab.group(2) or '0', 0))
            else:
                addr.pop(a[0], None)
            continue
        if base in ('lfs', 'lfd', 'lfsu', 'lfdu'):
            m = re.fullmatch(r'(.*)\((r\d+)\)', a[1]) if len(a) > 1 else None
            if not m:
                continue
            lab = FC_LABEL_RE.fullmatch(m.group(1))
            if lab:
                ref = (lab.group(1), int(lab.group(2) or '0', 0))
            else:
                d = num(m.group(1))
                ref = (addr[m.group(2)][0], addr[m.group(2)][1] + d) if d is not None and m.group(2) in addr else None
            if not ref:
                continue
            v = rod_value(mod, ref[0], ref[1], 'f' if base.startswith('lfs') else 'd')
            if not isinstance(v, float) or fc_ignored(v):
                continue
            v = abs(v)
            if not any(fc_close(v, x) for x in seen):
                seen.append(v)
                out.append((i.off, v))
            continue
        if a and re.fullmatch(r'r\d+', a[0]) and not base.startswith(('st', 'cmp', 'b', 'mt')):
            addr.pop(a[0], None)
    return out


def cpp_float_lits(f):
    """[(|value|, line, is_float_syntax)] numeric literals in the body (comments/strings already blank)."""
    toks = drop_templates(lex(f.code, f.code_line0))
    if getattr(f, 'foreign', None) or getattr(f, 'debug_lines', None):
        toks = [t for t in toks if t[1] not in f.foreign and t[1] not in f.debug_lines]
    toks = drop_debug_blocks(toks)
    out = []
    for k, (t, ln) in enumerate(toks):
        if not t or not (t[0].isdigit() or t[0] == '.') or t.lower().startswith('0x'):
            continue
        v = num_literal(t)
        if v is None:
            continue
        out.append((abs(float(v)), ln, bool(FLOAT_LIT_RE.match(t))))
    return out


def is_copy_ctor(target):
    """`__ct__12CTransform4fFRC12CTransform4f`: a struct copy (C++ `a = b`, init, return)."""
    t = target.strip('"')
    m = re.match(r'__ct__(\d+)', t)
    if not m:
        return False
    n = int(m.group(1))
    cls = t[m.end():m.end() + n]
    return len(cls) == n and t[m.end() + n:] == 'FRC%d%s' % (n, cls)


def operand_key(v):
    if isinstance(v, Bits):
        if v.src is not None and isinstance(v.src, V) and v.src.k == 'ld' and v.src.a == 'this':
            return ('f', v.src.b)
        return None
    if not isinstance(v, V):
        return None
    if v.k == 'ld' and v.a == 'this':
        return ('f', v.b)
    if v.k == 'rod' and isinstance(v.b, float):
        return ('c', round(v.b, 5))
    if v.k == 'ret':
        return ('call', base_name(v.a))
    if v.k == 'x' and v.d and v.d[0] == 'add':
        return operand_key(v.d[1])
    return None


def canon_insn(text):
    """Normalize an instruction to (op, args) in rlwinm form where applicable."""
    t = text.strip().strip('`').strip()
    m = re.match(r'^([a-z][\w.]*)\s+(.*)$', t)
    if not m:
        return None
    op, args = m.group(1), split_args(m.group(2))
    d = rlw_decode(op, args)
    rec = '.' if op.endswith('.') else ''
    if d:
        dst, src, sh, mb, me, ins = d
        return ('rlwimi' if ins else 'rlwinm') + rec, (dst, src, sh % 32, mb, me)
    out = []
    for a in args:
        v = num(a)
        out.append(v if v is not None else a.replace(' ', '').lower())
    return op, tuple(out)


class Checker:
    def __init__(s, mod, cpp, args):
        s.mod, s.cpp, s.args = mod, cpp, args
        root = args.root
        if not root:
            d = os.path.dirname(os.path.abspath(cpp))
            while d != '/' and not os.path.isdir(os.path.join(d, 'include')):
                d = os.path.dirname(d)
            root = d if d != '/' else os.path.dirname(os.path.abspath(cpp))
        s.hi = HeaderIndex([os.path.join(root, 'include'), os.path.join(root, 'src'),
                            os.path.dirname(os.path.abspath(cpp))] + (args.I or []))
        s.hi.add_closure(cpp)
        for h in args.header or []:
            s.hi.add_closure(h)
        s.hi.index()
        s.funcs = parse_cpp(cpp)
        # fn id -> C++ short names across the cpp's directory (for local callees)
        s.fn_names = defaultdict(set)
        s.cited = set()
        d = os.path.dirname(os.path.abspath(cpp))
        others = [os.path.join(d, x) for x in os.listdir(d) if x.endswith('.cpp')] + (args.src or [])
        for p in set(others) | {os.path.abspath(cpp)}:
            try:
                fl = s.funcs if os.path.abspath(p) == os.path.abspath(cpp) else parse_cpp(p)
            except (OSError, UnicodeError):
                continue
            for f in fl:
                for fid in f.ids + f.sig_ids:
                    s.fn_names[fid.lower()].add(f.short)
            try:
                for m in re.finditer(r'\bfn_[0-9A-Za-z]+_[0-9A-Fa-f]+\b', open(p, errors='replace').read()):
                    s.cited.add(m.group(0).lower())
            except OSError:
                pass
        s.helpers = defaultdict(list)
        for f in s.funcs:
            if not f.ids and not f.sig_ids:
                s.helpers[f.short].append(f)
        s.findings = []
        s.analyses = {}
        s.nopos = set()

    def analysis(s, af):
        if af.name not in s.analyses:
            s.analyses[af.name] = Analysis(af, s.mod, False)
        return s.analyses[af.name]

    def emit(s, tag, fn, off, text, line=None, marked=True):
        s.findings.append((tag, fn, off, text, line, marked))

    def run(s, wanted):
        todo = []
        for f in s.funcs:
            for fid in f.ids:
                af = s.mod.find(fid)
                if af is None:
                    continue
                if wanted and not any(s.mod.find(w) is af for w in wanted):
                    continue
                todo.append((f, af))
        if wanted:
            for w in wanted:
                if not any(s.mod.find(w) is af for _, af in todo):
                    print('MISSING %s 0x0: no C++ definition cites it in %s' % (w, s.cpp))
        for f, af in todo:
            s.check(f, af)
        return todo

    def facts_with_helpers(s, f, members, getters=None, depth=0, seen=None):
        F = cpp_facts(f, members, getters)
        seen = seen or {id(f)}
        if depth >= 2:
            return F
        for name in list(F.calls):
            for h in s.helpers.get(name, []):
                if id(h) in seen:
                    continue
                seen.add(id(h))
                H = s.facts_with_helpers(h, members, getters, depth + 1, seen)
                F.cited_calls |= H.cited_calls
                for k, v in H.calls.items():
                    F.calls[k] += v
                F.call_tokens += H.call_tokens
                F.conds += H.conds
                F.cmps += H.cmps
                for dct in ('reads', 'writes', 'touch', 'tested'):
                    for k, v in getattr(H, dct).items():
                        getattr(F, dct)[k] += v
                F.masks += H.masks
                F.idents |= H.idents
        return F

    def check(s, f, af):
        n0 = len(s.findings)
        s.check1(f, af)
        prim = next((o for o in (s.mod.find(x) for x in f.ids) if o is not None), None)
        if prim is not None and prim is not af:
            s.findings[n0:] = [x for x in s.findings[n0:] if x[0] not in ('XBIT', 'XCMP', 'XSTORE')]

    def check1(s, f, af):
        fn = af.name
        mark = f.marked
        an = Analysis(af, s.mod, f.sret)
        if not f.sret and not any(r == 'this' for _, r, _, _ in an.loads) and \
                not any(r == 'this' for _, r, _, _, _ in an.stores):
            # the port returns a reference/void where retail returns a class by value (r4 = this)
            a2 = Analysis(af, s.mod, True)
            if any(r == 'this' for _, r, _, _ in a2.loads):
                an = a2
        members = s.hi.expanded(f.cls) if f.cls else {}
        getters = s.hi.accessors(f.cls) if f.cls else {}
        # F (body + merged helpers) decides what the C++ covers; Fo (body only) is what it claims.
        # A helper may be its own asm function, so it never produces a correctness finding here.
        F = s.facts_with_helpers(f, members, getters)
        Fo = cpp_facts(f, members, getters)
        loc = lambda ln: '%s:%d' % (os.path.basename(f.file), ln)
        offs = lambda idx: af.ins[idx].off
        stated, layout = bit_members(members)
        by_off = defaultdict(list)
        for mb in members.values():
            if mb.bits is None and mb.off is not None:
                by_off[mb.off].append(mb)

        is_ctor = f.cls is not None and f.short == f.cls

        def zero_init(vals):
            # a ctor zero-initialising what retail leaves to its zeroed heap (host heap isn't zeroed)
            return is_ctor and bool(vals) and all(
                v in ('false', 'nullptr', 'NULL', 'kInvalidUniqueId') or num_literal(v or 'x') == 0 for v in vals)

        def mname(byte, mask):
            st = stated.get((byte, mask)) or layout.get((byte, mask))
            return ' (%s)' % st[0].name if st else ''

        # ---------------- bits
        A_r, A_w = defaultdict(dict), defaultdict(dict)   # byte -> mask -> idx / (idx, how)
        for idx, (root, keys, expr) in an.reads.items():
            if root != 'this':
                continue
            for b, m in keys.items():
                for k in range(8):
                    if m >> k & 1:
                        A_r[b].setdefault(1 << k, idx)
        for (idx, root, byte, bm, how, vd, sidx) in an.writes:
            if root == 'this':
                for k in range(8):
                    if bm >> k & 1:
                        A_w[byte].setdefault(1 << k, []).append((idx, how))
        touched = set()
        for idx, root, off, w in an.loads:
            if root == 'this':
                touched.update(range(off, off + w))
        stored = set()
        for idx, root, off, w, v in an.stores:
            if root == 'this':
                stored.update(range(off, off + w))
        for idx, root, off in an.addr_args:
            if root == 'this':
                stored.update(range(off, off + 4))
        addr_offs = {off for idx, root, off in an.addr_args if root == 'this'}
        # a def citing several fns (ctor + split-out helper): absence is judged over all of them
        others = []
        for fid in f.ids:
            o = s.mod.find(fid)
            if o is not None and o is not af and all(o is not x for x in others):
                others.append(o)
        other_fields = set()
        for o in others:
            ao = Analysis(o, s.mod, f.sret)
            touched.update(b for i2, r2, off, w in ao.loads if r2 == 'this' for b in range(off, off + w))
            stored.update(b for i2, r2, off, w, v in ao.stores if r2 == 'this' for b in range(off, off + w))
            stored.update(b for i2, r2, off in ao.addr_args if r2 == 'this' for b in range(off, off + 4))
            addr_offs.update(off for i2, r2, off in ao.addr_args if r2 == 'this')
            for c2 in ao.cmps:
                for v2 in (c2.lhs, c2.rhs):
                    k2 = operand_key(v2)
                    if k2 and k2[0] == 'f':
                        other_fields.add(k2[1])
            for i2, (r2, keys2, e2) in ao.reads.items():
                if r2 == 'this':
                    other_fields.update(keys2)
        # inlined callees: a local fn_X the asm calls and the C++ body never calls by name (or that
        # the def cites) is folded in: its this-relative loads/stores/compares/bit reads, mapped
        # through the call's argument registers (r3 = this or &this->member, by-value fields,
        # pointers to stack copies of fields), count as the caller's for XBIT/XCMP/XSTORE absence.
        fold_fields = defaultdict(set)
        fold_r, fold_w = defaultdict(int), defaultdict(int)     # byte -> folded bit reads / writes
        for idx, t, argv, slots in an.call_args:
            nm = base_name(t)
            if not re.fullmatch(r'fn_[0-9A-Za-z]+_[0-9A-Fa-f]+', nm):
                continue
            callee = s.mod.find(nm)
            if callee is None or callee is af:
                continue
            ca = s.analysis(callee)

            def tr(root, off, argv=argv):
                reg = 'r3' if root == 'this' else 'r%d' % (3 + int(root[1:])) if re.fullmatch(r'a[1-7]', root) else None
                v = argv.get(reg)
                if isinstance(v, V) and v.k == 'in' and v.a == 'this':
                    return off
                if isinstance(v, V) and v.k == 'ptr' and v.a == 'this':
                    return v.b + off
                return None

            def tr_val(v, argv=argv, slots=slots):
                if isinstance(v, Bits):
                    v = v.src
                if not isinstance(v, V):
                    return None
                if v.k == 'ld':
                    x = tr(v.a, v.b)
                    if x is not None:
                        return x
                    reg = 'r%d' % (3 + int(v.a[1:])) if re.fullmatch(r'a[1-7]', v.a) else 'r3' if v.a == 'this' else None
                    p = argv.get(reg)
                    if isinstance(p, V) and p.k == 'ptr' and p.a == 'sp':
                        v = slots.get(p.b + v.b)       # a field copied to the stack, passed by pointer
                    else:
                        return None
                elif v.k == 'in' and re.fullmatch(r'a[1-7]', v.a):
                    v = argv.get('r%d' % (3 + int(v.a[1:])))     # a field passed by value
                else:
                    return None
                if isinstance(v, Bits):
                    v = v.src
                return v.b if isinstance(v, V) and v.k == 'ld' and v.a == 'this' else None

            got = fold_fields[nm]
            for i2, r2, o2, w2 in ca.loads:
                x = tr(r2, o2)
                if x is not None:
                    touched.update(range(x, x + w2))
                    got.update(range(x, x + w2))
            for i2, r2, o2, w2, v2 in ca.stores:
                x = tr(r2, o2)
                if x is not None:
                    stored.update(range(x, x + w2))
                    got.update(range(x, x + w2))
            for i2, r2, o2 in ca.addr_args:
                x = tr(r2, o2)
                if x is not None:
                    stored.update(range(x, x + 4))
                    addr_offs.add(x)
                    got.add(x)
            for c2 in ca.cmps:
                for v2 in (c2.lhs, c2.rhs):
                    x = tr_val(v2)
                    if x is not None:
                        other_fields.add(x)
                        got.add(x)
            for i2, (r2, keys2, e2) in ca.reads.items():
                for b2, m2 in keys2.items():
                    x = tr(r2, b2)
                    if x is not None:
                        other_fields.add(x)
                        touched.add(x)
                        got.add(x)
                        fold_r[x] |= m2
            for (i2, r2, b2, m2, how2, vd2, si2) in ca.writes:
                x = tr(r2, b2)
                if x is not None:
                    fold_w[x] |= m2
            for x in ca.idiom_fields:
                x = tr('this', x)
                if x is not None:
                    other_fields.add(x)
        cpp_offs = set()
        for name in set(Fo.reads) | set(Fo.writes) | set(Fo.touch):
            mb = members.get(name)
            if mb is not None and mb.off is not None:
                cpp_offs.update(range(mb.off, mb.off + (1 if mb.bits is not None else mb.size or 4)))
        for ln, op, lhs, rhs, in_for in Fo.cmps:
            for o in (lhs, rhs):
                if o and o[0] == 'id' and o[1] in members and members[o[1]].off is not None:
                    cpp_offs.add(members[o[1]].off)

        def c_bits(FF, lay=False):
            # correctness uses stated positions only (no host-layout guesses: NOPOS); completeness
            # (lay) also credits a bit the layout places, so an unpositioned read is no BIT miss
            C_r, C_w = defaultdict(dict), defaultdict(dict)    # byte -> mask -> (line, name, value)
            for name, lines in FF.reads.items():
                mb = members.get(name)
                if mb and mb.bits is not None:
                    off, mask = mb.pos() if lay else (mb.off, mb.mask)
                    if off is not None and mask:
                        for k in range(8):
                            if mask >> k & 1:
                                C_r[off].setdefault(1 << k, (lines[0], name, None))
            for name, lst in FF.writes.items():
                mb = members.get(name)
                if mb and mb.bits is not None:
                    off, mask = mb.pos() if lay else (mb.off, mb.mask)
                    if off is not None and mask:
                        for k in range(8):
                            if mask >> k & 1:
                                C_w[off].setdefault(1 << k, (lst[0][0], name, [v for _, v in lst]))
            for ln, val, mem, kind in FF.masks:
                mb = members.get(mem) if mem else None
                if not mb or mb.off is None or mb.bits is not None:
                    continue
                size = mb.size or 4
                if val >> (size * 8):
                    continue
                for p in range(size * 8):
                    if val >> p & 1:
                        byte = mb.off + (size - 1 - p // 8)
                        tgt = C_w if kind in ('set', 'clr', 'w') else C_r
                        tgt[byte].setdefault(1 << (p % 8), (ln, '%s & 0x%x' % (mem, val),
                                                            ['true' if kind == 'set' else 'false' if kind == 'clr' else '?']))
            return C_r, C_w
        C_r, C_w = c_bits(F, True)
        Co_r, Co_w = c_bits(Fo)
        # C++ bit uses an inlined callee (fold) explains are no MASK/XBIT
        for byte, fm in fold_r.items():
            for m in [m for m in Co_r.get(byte, {}) if m & fm]:
                del Co_r[byte][m]
        for byte, fm in fold_w.items():
            for m in [m for m in Co_w.get(byte, {}) if m & fm]:
                del Co_w[byte][m]
        for name in sorted(set(Fo.reads) | set(Fo.writes)):
            mb = members.get(name)
            if mb is not None and mb.bits is not None and (mb.off is None or not mb.mask) and \
                    (f.cls, name) not in s.nopos:
                s.nopos.add((f.cls, name))
                ln = (Fo.reads.get(name) or [w[0] for w in Fo.writes.get(name, [])] or [f.line])[0]
                s.emit('NOPOS', fn, af.start, 'bitfield %s has no stated bit position%s; unchecked' % (
                    name, ' for ' + MODULE if MODULE else ''), loc(ln), mark)
        for byte in sorted(set(A_r) | set(C_r)):
            ua = {m: i for m, i in A_r.get(byte, {}).items() if m not in C_r.get(byte, {})}
            uc = {m: v for m, v in Co_r.get(byte, {}).items() if m not in A_r.get(byte, {})}
            # a C++ read the asm only does as part of a write (lbz + rlwimi) is not a read mismatch
            if ua and uc:
                for m, (ln, name, _) in sorted(uc.items()):
                    am = sorted(ua)
                    s.emit('MASK', fn, offs(ua[am[0]]), 'C++ reads %s = x%x_%02x, asm tests %s' % (
                        name, byte, m, ', '.join('x%x_%02x%s at 0x%X' % (byte, x, mname(byte, x), offs(ua[x]))
                                                 for x in am)), loc(ln), mark)
            elif uc:
                for m, (ln, name, _) in sorted(uc.items()):
                    if byte not in touched and byte not in stored:
                        s.emit('XBIT', fn, af.start, 'C++ reads %s = x%x_%02x; asm never loads x%x' % (
                            name, byte, m, byte), loc(ln), mark)
                    elif byte in touched and A_r.get(byte):
                        s.emit('MASK', fn, offs(min(A_r[byte].values())), 'C++ reads %s = x%x_%02x, asm tests only %s' % (
                            name, byte, m, ', '.join('x%x_%02x' % (byte, x) for x in sorted(A_r[byte]))), loc(ln), mark)
            if ua and not uc:
                for m, idx in sorted(ua.items()):
                    if m in C_w.get(byte, {}):
                        continue
                    s.emit('BIT', fn, offs(idx), 'asm tests x%x_%02x%s; C++ never reads it' % (byte, m, mname(byte, m)),
                           None, mark)
        for byte in sorted(set(A_w) | set(C_w)):
            aw, cw = A_w.get(byte, {}), C_w.get(byte, {})
            consts = {m for m, lst in aw.items() if all(h == 'const' for _, h in lst)}
            ua = {m: v for m, v in aw.items() if m not in cw and m not in consts}
            uc = {m: v for m, v in Co_w.get(byte, {}).items() if m not in aw}
            if ua and uc:
                for m, (ln, name, vals) in sorted(uc.items()):
                    s.emit('MASK', fn, offs(ua[sorted(ua)[0]][0][0]), 'C++ writes %s = x%x_%02x, asm writes %s' % (
                        name, byte, m, ', '.join('x%x_%02x%s' % (byte, x, mname(byte, x)) for x in sorted(ua))),
                        loc(ln), mark)
            else:
                for m, (ln, name, vals) in sorted(uc.items()):
                    if byte not in stored and not zero_init(vals):
                        s.emit('XBIT', fn, af.start, 'C++ writes %s = x%x_%02x; asm never stores x%x' % (
                            name, byte, m, byte), loc(ln), mark)
                for m, lst in sorted(ua.items()):
                    idx, how = lst[0]
                    s.emit('BIT', fn, offs(idx), 'asm %s x%x_%02x%s; C++ never writes it' % (
                        {'set': 'sets', 'clr': 'clears', 'ins': 'assigns', 'const': 'stores'}[how], byte, m,
                        mname(byte, m)), None, mark)
            for m in set(aw) & set(Co_w.get(byte, {})):
                hows = {h for _, h in aw[m]}
                ln, name, vals = Co_w[byte][m]
                vals = set(vals or [])
                if hows == {'set'} and vals and vals <= {'false', '0'}:
                    s.emit('MASK', fn, offs(aw[m][0][0]), 'asm sets x%x_%02x, C++ %s = false' % (byte, m, name), loc(ln), mark)
                if hows == {'clr'} and vals and vals <= {'true', '1'}:
                    s.emit('MASK', fn, offs(aw[m][0][0]), 'asm clears x%x_%02x, C++ %s = true' % (byte, m, name), loc(ln), mark)

        # ---------------- compares
        asm_fields = defaultdict(list)    # field off -> [(idx)]
        for cmp in an.cmps:
            for v in (cmp.lhs, cmp.rhs):
                k = operand_key(v)
                if k and k[0] == 'f':
                    asm_fields[k[1]].append(cmp.idx)
        for b in A_r:
            asm_fields.setdefault(b, []).extend(A_r[b].values())
        for off, idx in an.idiom_fields.items():
            asm_fields[off].append(idx)
        # MWCC compares a computed value it then stores (`x += d; if (x > 1) x = 1;`): the field counts
        for cmp in an.cmps:
            for v in (cmp.lhs, cmp.rhs):
                sv = an.stored_vals.get(id(v))
                if sv and sv[0] is v:
                    for off in sv[1]:
                        asm_fields[off].append(cmp.idx)

        def member_key(op):
            if not op:
                return None
            if op[0] == 'call' and op[1] == op[2] and op[1] in getters:
                op = ('id', getters[op[1]], True)
            if op[0] == 'id' and op[1] in members and op[2]:
                mb = members[op[1]]
                if mb.off is not None and mb.bits is None:
                    return ('f', mb.off)
            if op[0] == 'num' and isinstance(op[1], float):
                return ('c', round(op[1], 5))
            if op[0] == 'call':
                return ('call', op[1])
            return None

        def c_cmps(FF):
            out = defaultdict(list)      # key -> [(op normalized key-on-left, line, in_for)]
            for ln, op, lhs, rhs, in_for in FF.cmps:
                kl, kr = member_key(lhs), member_key(rhs)
                # null guards (== / != nullptr) are port safety checks: kept out of XCMP like for headers
                null = op in ('==', '!=') and any(o and o[0] == 'id' and o[1] in ('nullptr', 'NULL')
                                                  for o in (lhs, rhs))
                if kl:
                    out[kl].append((op, ln, in_for or null))
                if kr:
                    out[kr].append((SWAP[op], ln, in_for or null))
            return out
        cpp_cmp = c_cmps(F)
        own_cmp = c_cmps(Fo)
        for idx, cmp, cs, exact in an.conds:
            if cmp is None or cmp.kind == 'rec' or not cs or 'so' in cs:
                continue
            op = OPS.get(frozenset(cs))
            if op in (None, 'true', 'false'):
                continue
            for side, v in (('l', cmp.lhs), ('r', cmp.rhs)):
                k = operand_key(v)
                if not k or k[0] != 'f':
                    continue
                aop = op if side == 'l' else SWAP[op]
                cands = {aop} if exact else {aop, NEG[aop]}
                other = cmp.rhs if side == 'l' else cmp.lhs
                if isinstance(other, V) and other.k == 'c' and other.a == 0 and not exact:
                    cands |= {'==', '!='} if aop in ('==', '!=') else set()
                cc = cpp_cmp.get(k, [])
                # integer relational vs C++ ==/!= against constants is MWCC switch/if-chain lowering
                if cmp.kind != 'f' and aop not in ('==', '!=') and cc and all(o in ('==', '!=') for o, _, _ in cc):
                    continue
                if cmp.kind != 'f':
                    # integer `x > 0` is `x >= 1`: only the direction is compared
                    cands |= {INCL.get(o, o) for o in cands} | {o for o in INCL if INCL[o] in cands}
                if own_cmp.get(k) and not any(o in cands for o, _, _ in cc):
                    nm = by_off.get(k[1])
                    s.emit('CROP', fn, offs(cmp.idx), 'asm %s: %s %s %s (%s), C++ %s' % (
                        'extracts' if exact else 'branches on', 'x%x' % k[1] + (' (%s)' % nm[0].name if nm else ''),
                        aop, desc(other), 'exact' if exact else 'or its negation',
                        ', '.join('%s at %s' % (o, loc(l)) for o, l, _ in cc)), loc(cc[0][1]), mark)
        for k, lst in own_cmp.items():
            if k[0] != 'f':
                continue
            if k[1] in asm_fields or k[1] in other_fields or k[1] in addr_offs:
                continue
            real = [(o, l) for o, l, fr in lst if not fr]
            if not real:
                continue
            nm = by_off.get(k[1])
            first_load = [i for i, r, o, w in an.loads if r == 'this' and o == k[1]]
            s.emit('XCMP', fn, offs(first_load[0]) if first_load else af.start,
                   'C++ compares %s (x%x) at %s; asm never compares or tests x%x' % (
                       nm[0].name if nm else '?', k[1], ', '.join(loc(l) for _, l in real), k[1]), loc(real[0][1]), mark)
        cpp_tested_offs = set(k[1] for k in cpp_cmp if k[0] == 'f')
        for name in list(F.tested) + list(F.reads):
            mb = members.get(name)
            if mb and mb.off is not None and mb.bits is None and name in F.tested:
                cpp_tested_offs.add(mb.off)
        sw_members = set()
        for k2, (t, ln) in enumerate(lex(f.code, f.code_line0)):
            pass
        for m in re.finditer(r'\bswitch\s*\(\s*(?:this\s*->\s*)?(\w+)', f.code):
            mb = members.get(m.group(1))
            if mb and mb.off is not None:
                cpp_tested_offs.add(mb.off)
        # `x = Max(0.f, x - dt)`: the clamp's compare is inside the helper (asm inlines it)
        for m in re.finditer(r'\b(\w+)\s*=\s*(?:\w+\s*::\s*)*(?:Max|Min|Clamp|max|min|clamp|Limit)\s*[<(]', f.code):
            mb = members.get(m.group(1))
            if mb and mb.off is not None:
                cpp_tested_offs.add(mb.off)
        for off, idxs in sorted(asm_fields.items()):
            if off in A_r:
                continue
            if off in cpp_tested_offs:
                continue
            nm = by_off.get(off)
            if not nm:
                continue
            cmpd = [c for c in an.cmps if c.idx in idxs]
            if cmpd and all(isinstance(c.rhs, V) and c.rhs.k == 'c' and c.rhs.a == 0 and c.kind != 'f' for c in cmpd) \
                    and nm[0].name in F.reads:
                continue
            s.emit('CMP', fn, offs(idxs[0]), 'asm compares x%x (%s); C++ never compares or tests it' % (off, nm[0].name),
                   None, mark)

        # ---------------- stores
        asm_st = defaultdict(list)
        for idx, root, off, w, v in an.stores:
            if root == 'this':
                asm_st[off].append(idx)
        cpp_written = set()
        for name in set(F.writes) | set(F.touch):
            mb = members.get(name)
            if mb and mb.off is not None:
                cpp_written.add(name)
        for off, idxs in sorted(asm_st.items()):
            nm = by_off.get(off)
            if not nm or nm[0].bits is not None:
                continue
            if any(m.name in cpp_written for m in nm):
                continue
            if any(m.name in F.writes or m.name in F.touch for m in nm):
                continue
            s.emit('STORE', fn, offs(idxs[0]), 'asm stores x%x (%s); C++ never writes it' % (off, nm[0].name), None, mark)
        for name, lst in Fo.writes.items():
            mb = members.get(name)
            if not mb or mb.off is None or mb.bits is not None:
                continue
            size = mb.size or 4
            if any(o in stored for o in range(mb.off, mb.off + size)):
                continue
            if zero_init([v for _, v in lst]):
                continue
            s.emit('XSTORE', fn, af.start, 'C++ assigns %s (x%x) at %s; asm never stores x%x' % (
                name, mb.off, loc(lst[0][0]), mb.off), loc(lst[0][0]), mark)

        # ---------------- calls
        asm_names = {}
        nv = 0
        for idx, t in an.calls:
            if t.startswith(('vcall', 'indirect', 'cond-call')):
                nv += 1
                continue
            if is_runtime(t) or is_copy_ctor(t):
                continue
            nm = base_name(t)
            asm_names.setdefault(nm, (idx, t))
        n_asm_calls = sum(1 for _, t in an.calls if not is_runtime(t) and not is_copy_ctor(t))
        for nm, (idx, t) in sorted(asm_names.items(), key=lambda x: x[1][0]):
            if re.fullmatch(r'(fn|func|lbl)_[0-9A-Fa-f]{8}', nm):
                continue
            if re.fullmatch(r'fn_[0-9A-Za-z]+_[0-9A-Fa-f]+', nm):
                cands = s.fn_names.get(nm.lower(), set())
                # named by a `// fn_X` comment on/above a call, or inlined (folded fields the C++ uses)
                if nm.lower() in F.cited_calls or fold_fields.get(nm, set()) & cpp_offs:
                    continue
                if not cands:
                    s.emit('CALL', fn, offs(idx), 'asm calls %s; no C++ definition cites it (unported)' % nm, None, mark)
                elif not (cands & F.idents):
                    s.emit('CALL', fn, offs(idx), 'asm calls %s (%s); C++ never calls it' % (nm, '/'.join(sorted(cands))),
                           None, mark)
                continue
            if nm in F.idents or nm.lstrip('_') in F.idents:
                continue
            s.emit('CALL', fn, offs(idx), 'asm calls %s; C++ never names it' % t.strip('"')[:90], None, mark)
        # XCALL only for C++ callees that cite an fn id themselves (never inlined accessors)
        cited_names = {g.short for g in s.funcs if g.ids and g.cls == f.cls}
        asm_local = {nm.lower() for nm in asm_names}
        for name, lines in sorted(Fo.calls.items()):
            if name in asm_names or name in members or name not in cited_names or name in Fo.qcalls:
                continue
            if any(name in s.fn_names.get(a, ()) for a in asm_local):
                continue
            if re.fullmatch(r'fn_\w+', name):
                continue
            s.emit('XCALL', fn, af.start, 'C++ calls %s at %s; this asm function never does' % (name, loc(lines[0])),
                   loc(lines[0]), mark)
        if n_asm_calls >= 2 * F.call_tokens + 3:
            s.emit('CALLS', fn, af.start, 'asm makes %d calls, C++ body %d' % (n_asm_calls, F.call_tokens), None, mark)
        asm_conds = sum(1 for c in an.conds)
        if asm_conds >= 2 * F.conds + 4:
            s.emit('BRANCH', fn, af.start, 'asm has %d conditions, C++ %d (missing blocks?)' % (asm_conds, F.conds),
                   None, mark)
        elif F.conds >= 2 * asm_conds + 4:
            s.emit('BRANCH', fn, af.start, 'C++ has %d conditions, asm %d (extra logic?)' % (F.conds, asm_conds),
                   None, mark)

        # ---------------- float constants
        s.fconst(f, af, F, mark, loc)

        # ---------------- cited offsets
        at = {i.off: i for i in af.ins}
        masks_at = defaultdict(set)
        for idx, (root, keys, expr) in an.reads.items():
            for b, m in keys.items():
                masks_at[af.ins[idx].off].add(m)
        for (idx, root, byte, bm, how, vd, sidx) in an.writes:
            masks_at[af.ins[idx].off].add(bm)
        for ln, text in f.comment_lines:
            for m in re.finditer(r'0x([0-9A-Fa-f]+)', text):
                v = int(m.group(1), 16)
                pre = text[max(0, m.start() - 8):m.start()].lower()
                if v % 4 or not (af.start <= v < af.end):
                    continue
                if len(m.group(1)) < 4 and not re.search(r'(asm|at|@|\+)\s*$', pre):
                    continue
                ins = at.get(v)
                if ins is None:
                    continue
                for q in re.finditer(r'`([a-z][a-z.]*\s+[^`]*)`', text):
                    if '..' in q.group(1):
                        continue
                    qa = canon_insn(q.group(1))
                    ia = canon_insn(ins.text)
                    if not ia:
                        continue
                    if not qa or qa[0].rstrip('.') != ia[0].rstrip('.'):
                        # quoted op is for another offset on the line unless it is the only citation
                        if len(re.findall(r'0x[0-9A-Fa-f]{4,}', text)) == 1 and qa and \
                                re.fullmatch(r'[a-z]+\.?', qa[0]) and qa[0] in ('rlwinm', 'rlwinm.', 'rlwimi', 'rlwimi.'):
                            s.emit('OFFS', fn, v, 'cites `%s` at 0x%X; asm has `%s`' % (q.group(1), v, ins.text), loc(ln), mark)
                        continue
                    if qa != canon_insn(ins.text):
                        s.emit('OFFS', fn, v, 'cites `%s` at 0x%X; asm has `%s`' % (q.group(1), v, ins.text), loc(ln), mark)
                if v in masks_at:
                    lm = set()
                    for mm in re.finditer(r'(?:&=?\s*~?\s*|\|=\s*|bit\s+|byte\s+bit\s+)0x([0-9A-Fa-f]{1,2})\b', text):
                        lm.add(int(mm.group(1), 16))
                    lm = {x for x in lm if popcount(x) == 1}
                    if lm and not (lm & masks_at[v]):
                        s.emit('OFFS', fn, v, 'line cites mask %s at 0x%X; that op uses %s' % (
                            '/'.join('0x%x' % x for x in sorted(lm)), v, '/'.join('0x%x' % x for x in sorted(masks_at[v]))),
                            loc(ln), mark)

    def fconst(s, f, af, F, mark, loc):
        loads = asm_float_loads(s.mod, af)
        body = cpp_float_lits(f)
        every = list(body)
        for g in s.funcs:       # other defs citing the same fn (split / helper defs) share its asm
            if g is not f and any(s.mod.find(x) is af for x in g.ids):
                every += cpp_float_lits(g)
        for name in F.calls:
            for h in s.helpers.get(name, []):
                every += cpp_float_lits(h)
        deg = any(fc_close(v, d) for _, v in loads for d in FC_DEG)
        loads = [(o, v) for o, v in loads if not any(fc_close(v, d) for d in FC_DEG)]
        folds = set()       # values a same-line pair of literals folds to (`a * b`, `a / b`, `a + b`)
        for i, (a, la, _) in enumerate(every):
            for b, lb, _ in every[i + 1:]:
                if la == lb:
                    folds.update((a * b, a / b if b else 0.0, b / a if a else 0.0, a + b, abs(a - b)))
            folds.add(a * a)
            folds.update((a * math.pi / 180, a * 180 / math.pi))    # degrees <-> radians

        def explained(v):
            return any(fc_close(v, c) for c, _, _ in every) or any(fc_close(v, x) for x in folds)

        def angle(c):
            return deg and (c in (180.0, 360.0, 90.0, 45.0) or any(
                fc_close(c, k * math.pi / 8) for k in range(1, 17)))

        for off, v in loads:
            # a body with no float literal at all (loader defaults in the generated reader, a
            # stub, a call into code with the constants) has nothing to compare against
            if any(isf for _, _, isf in every) and not explained(v):
                s.emit('FCONST', af.name, off, 'asm loads %s, no C++ literal within tolerance' % fmt_float(v)[:-1],
                       None, mark)
        code_lines = f.code.split('\n')

        def src_of(ln):
            k = ln - f.code_line0
            return code_lines[k] if 0 <= k < len(code_lines) else ''
        color_lines = {ln for _, ln, _ in body if re.search(r'\bCColor\w*\s*\(|\bmGuest\w+\s*\(', src_of(ln))}
        seen = []
        for c, ln, isf in body:
            if not isf or fc_ignored(c) or angle(c) or ln in color_lines or any(fc_close(c, d) for d in FC_DEG) or any(fc_close(c, x) for x in seen):
                continue
            seen.append(c)
            same = [d for d, l2, _ in every if l2 == ln and d != c]
            if any(fc_close(c, v) or fc_close(c * c, v) or fc_close(c * math.pi / 180, v) or any(
                    fc_close(c * d, v) or (d and (fc_close(c / d, v) or fc_close(d / c, v))) or fc_close(c + d, v)
                    for d in same) for _, v in loads):
                continue
            s.emit('FCONST', af.name, af.start, 'C++ literal %s not loaded by the asm' % fmt_float(c)[:-1],
                   loc(ln), mark)

    def report(s, quiet, show_all, only):
        shown = 0
        per = defaultdict(lambda: defaultdict(int))
        suppressed = defaultdict(int)
        seen = set()
        for tag, fn, off, text, line, marked in s.findings:
            key = (tag, fn, off, text)
            if key in seen:
                continue
            seen.add(key)
            if only and tag not in only:
                continue
            if tag in COMPLETENESS and not marked and not show_all:
                suppressed[fn] += 1
                continue
            per[fn][tag] += 1
            shown += tag not in INFO
            if not quiet:
                print('%s %s 0x%X: %s%s' % (tag, fn, off, text, ' [%s]' % line if line else ''))
        for fn, n in sorted(suppressed.items()):
            if not quiet:
                print('NOTE %s 0x0: unmarked; %d completeness finding(s) suppressed (--all shows them)' % (fn, n))
        return shown, per, suppressed


def module_name(arg):
    if not os.path.exists(arg):
        return os.path.basename(arg.rstrip('/'))
    parts = os.path.abspath(arg).split(os.sep)
    if 'asm' in parts[1:]:
        return parts[len(parts) - 1 - parts[::-1].index('asm') - 1]  # the last `<Module>/asm`
    return os.path.basename(arg.rstrip('/'))


def cmd_check(args):
    global MODULE
    MODULE = args.module or module_name(args.asm)
    mod = load_module(args.asm)
    ck = Checker(mod, args.cpp, args)
    todo = ck.run(args.fns)
    only = set(args.only.split(',')) if args.only else None
    shown, per, suppressed = ck.report(args.quiet, args.all, only)
    marks = {}
    for f, af in todo:
        marks[af.name] = ('marked' if f.marked else 'cited', '%s:%d' % (os.path.basename(f.file), f.line))
    if args.quiet or args.summary:
        for fn, (m, where) in sorted(marks.items()):
            tags = per.get(fn, {})
            if tags or args.summary:
                print('== %s %s %s: %d (%s)' % (fn, m, where, sum(tags.values()),
                                                ' '.join('%s=%d' % kv for kv in sorted(tags.items()))))
    print('== total %d function(s), %d finding(s)%s' % (len(marks), shown,
                                                        ', %d suppressed (unmarked)' % sum(suppressed.values())
                                                        if suppressed else ''))
    return 1 if shown else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('annotate', help='annotate one function')
    a.add_argument('asm')
    a.add_argument('fn')
    a.add_argument('--header')
    a.add_argument('--class', dest='cls')
    a.add_argument('-I', action='append')
    a.add_argument('--all-ins', action='store_true', help='print every instruction')
    a.add_argument('--sret', action='store_true', help='function returns a class by value: r3 is the result, r4 this')
    c = sub.add_parser('check', help='asm-vs-C++ structure check')
    c.add_argument('asm')
    c.add_argument('cpp')
    c.add_argument('fns', nargs='*')
    c.add_argument('--all', action='store_true', help='report completeness tags for unmarked functions too')
    c.add_argument('--quiet', action='store_true', help='summary lines only')
    c.add_argument('--summary', action='store_true', help='findings plus a summary line per function')
    c.add_argument('--only', help='comma-separated tags to report')
    c.add_argument('--root', help='repo root (include/ and src/ are include roots)')
    c.add_argument('-I', action='append', help='extra include root')
    c.add_argument('--header', action='append', help='extra header to index')
    c.add_argument('--src', action='append', help='extra .cpp to index for fn_X -> name')
    c.add_argument('--module', help='module whose per-module member positions apply (default: from <asm>)')
    args = ap.parse_args()
    try:
        sys.exit(cmd_annotate(args) if args.cmd == 'annotate' else cmd_check(args))
    except BrokenPipeError:
        sys.exit(0)


if __name__ == '__main__':
    main()
