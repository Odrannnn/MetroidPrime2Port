#!/usr/bin/env python3
"""Static check of ported REL functions against their retail asm: dropped calls, lost constants,
shape and float-compare NaN arms. No build, no game run (whole port in a few seconds).

For each function marked `// @ fn_<mod>_<off>` (found as tools/port_progress.py finds markers) it
compares the port body with the retail body (asm and rodata read through tools/asmcheck.py):
  call   a retail `bl` target the port body never names: a ported module callee by its port name,
         an unported one by fn_<mod>_<off> or an x<OFF>_ name (in code or a comment), a DOL callee
         by its demangled base name (runtime helpers, rstl, ctors/dtors, operators skipped).
         Port helpers the body calls (any def of that name, the caller's class when several; else a
         short inline body of an unmarked include/ header) are expanded 2 levels; DOL callees the port names another way (DOL_ALIAS: ObjectById,
         AsNormalized, SetDrawShadow...) count as named. Hidden unless --strict: callees missed at
         >= half of their (>= 3) call sites (port-model differences), unported module callees of a
         ctor or dtor (member ctors/dtors), unported rstl/string/allocation template instances,
         unported value helpers (straight-line stores through r3 only: struct copies, Cross),
         CToken::GetObj. A marker inside a body (a switch arm) checks that body (no shape check)
  const  a float constant retail loads from module rodata that appears nowhere in the port file
         as a literal (0 and the int->float conversion magic skipped)
  shape  conditional-branch count far from the port's if/?:/loops/&&/|| count, or a big retail body
         behind a tiny port body
  nan    fcmpo/fcmpu consumers: a plain branch (blt/bgt/bge/ble, NaN arm = the ordered `<`/`>`
         result) needs a strict `<`/`>` in C, negated or not; `cror eq,lt|gt,eq` (+ beq/bne/mfcr)
         needs `<=`/`>=`. Negating a C compare flips its NaN arm together with its sense, so
         `!(a <= b)` is NOT `a > b` on a NaN: it matches a cror+beq, never a plain ble. Flagged:
         the port has fewer strict (or non-strict) compares than retail has plain (cror) float
         compares, or more negated `!(a <= b)` forms than retail has crors. rstl::min_val/max_val,
         CMath::Min/Max count as one strict compare, CMath::Clamp as two. Counts only: leads.
  args   a retail `bl` whose float arguments (each a rodata constant loaded in the straight
         line before the call, at least two resolved) are the same multiset as the literal float
         arguments of the port's one call to that callee, in a different order (swapped
         minActiveTime/knockback). Skipped: callees called more than once on either side, a float
         register set by anything but a rodata load or cleared by a label, fewer than two
         floats, equal values. Integer immediates are not compared (too noisy)
  effect a ctor/dtor the port performs a retail side effect in fewer times: CToken::Lock (EFFECTS:
         also Unlock, CSfxManager start/stop, AddObject/FreeScriptObject, SetActive, Add/Remove
         Material), counted through unported module callees (r3 = this + off followed through
         mr/addi). A Lock of a token at +0 of a member the header places (`x<OFF>_` name or a
         `// 0x<OFF>` comment) must be that member's in the port (`m.Lock()`, `m.Token().Lock()`,
         a TLockedToken member built/assigned); the others are compared by count against the port's
         Lock calls, TLockedToken / optional_object<TLockedToken> constructions, TLockedToken
         member inits/assignments/push_backs, locking default member initializers and member ctors
         the port defines that lock. Locks of stack temporaries and unplaced locks inside a callee
         (a host ctor's own) are dropped. Always on. tools/retail_diff_regress.sh pins it (#59)
Findings are hints for a human (inlined callees, constants named in another file show up); a clean
function is a good sign, not proof.

  --coverage: per module, how much was really checked (markers, bodies parsed, retail asm found, shape
         checked, float-compare sites retail vs port); with --verbose also the unparsed markers
         (file:line, why: data table, declaration, `= default`, no body found)
Bodies are parsed by asmcheck.parse_cpp(deep=True): also in-class (header / local-class) inline
bodies and `extern "C" void f() {}`. Still unparsed: markers on data tables, `= default`,
declarations, and a marker not directly above its definition.

usage: retail_diff.py [--module M ...] [--fn fn_X ...] [--diff BASE] [--kinds call,const,shape,nan,args,effect]
                      [--file PATH ...] [--coverage] [--verbose]
                      [--top N] [--strict]
  no selector: whole port, per-kind totals and the N (default 40) worst functions
  --file PATH: check only the marked bodies of these source files (e.g. an old `git show` copy)
  --diff BASE: only functions whose marked body is new or changed since BASE (lane review)
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import asmcheck as ac  # noqa: E402
import port_progress  # noqa: E402
import rel_manifest as rm  # noqa: E402

MARKER_FILE = re.compile(r"//\s*@\s*fn_\d+_[0-9A-Fa-f]+\b")
FNID = re.compile(r"fn_(\d+)_([0-9A-Fa-f]+)$")
FLOAT_LIT = re.compile(r"(?<![\w.])(\d+\.\d*(?:[eE][-+]?\d+)?|\.\d+(?:[eE][-+]?\d+)?|\d+[eE][-+]?\d+)[fFlL]?")
INT_LIT = re.compile(r"(?<![\w.])(\d+)(?![\w.])")
MAGIC = {4503601774854144.0, 4503599627370496.0}
SMALL = 0x10  # callee size at or under which a missing call is not reported
CONDB = re.compile(r"^b(?:eq|ne|lt|le|gt|ge|nl|ng|so|ns|un|nu)(?:lr|ctr)?$")
PORT_COND = re.compile(r"\bif\b|\bwhile\b|\bfor\b|\?|&&|\|\||\bcase\b")
# DOL callees the port names another way (same behaviour): base name -> regex over the port body
DOL_ALIAS = {
    "GetObjectById": r"(?:Get)?ObjectById", "ObjectById": r"(?:Get)?ObjectById",
    "Normalize": r"Normalize\w*|AsNormalized|Normalized|CUnitVector3f",
    "AsNormalized": r"Normalize\w*|AsNormalized|Normalized|CUnitVector3f",
    "IsActorVisibleInPortals": r"IsActorVisible\w*",
    "AccumulateBounds": r"AccumulateBounds|Include",
    "DeliverScriptMsg": r"(?:Deliver|Send)ScriptMsgs?",
    "CreateShadow": r"CreateShadow|SetDrawShadow",
    "SetTransformAlt": r"SetTransform\w*|SetRotation",
    "BuildTransform4f": r"BuildTransform4f|SetRotation",
    "CheckConnectedObject_if": r"(?:Check|Find)ConnectedObject_if",
    "InitializeStateMachine": r"InitializeStateMachine|SetupStateMachine",
    # CPASAnimParmData's ctor defaults its unused parameters to CPASAnimParm::NoParameter()
    "NoParameter": r"NoParameter|CPASAnimParmData",
    # the hosts' microsecond clock seeds the same CRandom16s
    "OSGetTime": r"OSGetTime|GetGlobalMicros",
    # 0x801F585C, CVisualKnowledge::SetVisualTarget before upstream renamed it
    "SetTarget": r"Set(?:Visual)?Target",
}
ALLOW = Path(__file__).with_name("retail_diff_allow.txt")


def load_allow():
    """retail_diff_allow.txt: `fn_<mod>_<off> <kind> <regex over the finding text>  # reason` per
    line; a reason is required. Returns [(fn key, kind, compiled regex, line number)]."""
    out = []
    if not ALLOW.exists():
        return out
    for n, line in enumerate(ALLOW.read_text().splitlines(), 1):
        body, _, why = line.partition("#")
        if not body.strip():
            continue
        parts = body.split(None, 2)
        if len(parts) != 3 or not why.strip():
            sys.exit(f"{ALLOW.name}:{n}: want `fn_<mod>_<off> <kind> <regex>  # reason`")
        out.append((norm_id(parts[0]), parts[1], re.compile(parts[2].strip()), n))
    return out
LIBM = {"sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sqrt", "fmod", "floor", "ceil", "pow"}
SKIP_DOL = ("GetObj",)
INBODY = set()  # CToken::GetObj: a token dereference (`*tok`, `tok->`), never a behaviour
# ctor/dtor side effects (`effect` kind, always on): retail callee (current DOL name) -> effect, and
# the port text that performs it. A ctor/dtor's own retail calls plus those of the unported module
# callees it reaches (member ctors/dtors, module copies of DOL ctors) are counted; the port body,
# its helpers and the TLockedToken members it builds must perform each effect as often.
EFFECTS = (
    ("Lock", re.compile(r"^Lock__6CToken"), r"\bLock\s*\("),
    ("Unlock", re.compile(r"^Unlock__6CToken"), r"\bUnlock\s*\("),
    ("SfxStart", re.compile(r"^(?:SfxStart|AddEmitter)__11CSfxManager"), r"\b(?:SfxStart|AddEmitter)\s*\("),
    ("SfxStop", re.compile(r"^(?:SfxStop|RemoveEmitter)__11CSfxManager"), r"\b(?:SfxStop|RemoveEmitter)\s*\("),
    ("AddObject", re.compile(r"^AddObject__13CStateManager"), r"\bAddObject\s*\("),
    ("FreeScriptObject", re.compile(r"^FreeScriptObject__13CStateManager"), r"\bFreeScriptObject\s*\("),
    ("SetActive", re.compile(r"^SetActive__(?:6CActor|7CEntity)F"), r"\bSetActive\s*\("),
    ("AddMaterial", re.compile(r"^AddMaterial__6CActor"), r"\bAddMaterial\s*\("),
    ("RemoveMaterial", re.compile(r"^RemoveMaterial__6CActor"), r"\bRemoveMaterial\s*\("),
)
PLUMB = re.compile(r"rstl|allocate|Free__7CMemory|string_l|internal_dereference|__ct__|__dt__|__nw|__dl")
SKIP_CALL = ("__ct__", "__dt__", "__nw", "__dl", "__as", "__pl", "__mi", "__ml", "__dv", "__eq", "__ne",
             "__lt", "__gt", "__le", "__ge", "__vc", "__cl", "__rf", "__apl", "__ami", "__aml", "__adv")


def rel(p):
    p = Path(p)
    return p.relative_to(ROOT) if p.is_relative_to(ROOT) else p


def f32(x):
    try:
        return struct.unpack(">f", struct.pack(">f", x))[0]
    except (OverflowError, struct.error):
        return x


def fid(mid, off):
    return f"fn_{mid}_{off:X}"


# ---------------------------------------------------------------- retail side

def load_retail(mods):
    """module -> (asmcheck Module, {offset: Func}), module id -> name, (mid, off) -> size."""
    out, ids, sizes = {}, {}, {}
    for m in mods:
        if not os.path.isdir(os.path.join(ac.ASM_ROOT, m, "asm")):
            continue
        mid = rm.module_id(m)
        mod = ac.load_module(m)
        out[m] = (mod, {f.start: f for f in mod.funcs.values() if f.ins})
        ids[mid] = m
        for off, size in rm.read_symbols(m):
            sizes[(mid, off)] = size
    return out, ids, sizes


def dol_symbols():
    """DOL address -> (current symbol name, size). The REL asm names a DOL callee as the symbols
    of the day it was split did (fn_8016C6CC, fn_80036F10__13CStateManagerCFv); the address in that
    name finds today's name (Update__8CIkChainFf)."""
    out, byname = {}, {}
    pat = re.compile(r"^(\S+) = \.\w+:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)")
    for line in (ROOT / "config/G2ME01/symbols.txt").read_text(errors="replace").splitlines():
        m = pat.match(line)
        if m:
            out[int(m.group(2), 16)] = (m.group(1), int(m.group(3), 16))
            byname[m.group(1)] = int(m.group(2), 16)
    return out, byname


SPLIT_FNS = None


def split_fn_addr(name):
    """Address of a DOL function under a name a split gave it, read from the split's DOL asm (its
    `.fn Name, global` and first address, else the target a `bl Name` there encodes) or the split
    checkout's symbols.txt: the REL asm calls DOL functions by those names, which upstream has
    renamed since (ShouldRetry__11CTryCommand is CanIssueCommand__15CAnimationState now,
    LoadActorParameters is LdrToActorParameters)."""
    global SPLIT_FNS
    if SPLIT_FNS is None:
        SPLIT_FNS, called = {}, {}
        for p in sorted((Path(ac.ASM_ROOT) / "asm").rglob("*.s")):
            text = p.read_text(errors="replace")
            for m in re.finditer(r'^\.fn "?([^\s"]+)"?, \w+\n/\* ([0-9A-F]{8}) ', text, re.M):
                SPLIT_FNS[m.group(1)] = int(m.group(2), 16)
            for m in re.finditer(r"^/\* ([0-9A-F]{8}) [0-9A-F]{8}  ([0-9A-F]{2}) ([0-9A-F]{2}) ([0-9A-F]{2}) "
                                 r'([0-9A-F]{2}) \*/\tbl "?([^\s"]+)"?$', text, re.M):
                ins = int("".join(m.group(2, 3, 4, 5)), 16)
                if ins >> 26 == 18 and not ins & 2:
                    off = ins & 0x3FFFFFC
                    called.setdefault(m.group(6), int(m.group(1), 16) + off - (off & 0x2000000) * 2)
        for k, a in called.items():
            SPLIT_FNS.setdefault(k, a)
        sy = Path(ac.ASM_ROOT).parents[1] / "config/G2ME01/symbols.txt"   # the split checkout's names
        if sy.is_file():
            for m in re.finditer(r"^(\S+) = \.text:0x([0-9A-Fa-f]{8});", sy.read_text(errors="replace"), re.M):
                SPLIT_FNS.setdefault(m.group(1), int(m.group(2), 16))
    return SPLIT_FNS.get(name)


REL_FNS = None


def rel_fn_size(name):
    """Size of a function another REL exports under a name the split gave it (PillBug calls the
    WallCrawler REL's 8-byte CWallCrawler_GetMember838), from the split checkout's rels/*/symbols.txt."""
    global REL_FNS
    if REL_FNS is None:
        REL_FNS = {}
        for p in (Path(ac.ASM_ROOT).parents[1] / "config/G2ME01/rels").glob("*/symbols.txt"):
            for m in re.finditer(r"^(\S+) = \.text:0x[0-9A-Fa-f]+; // type:function size:0x([0-9A-Fa-f]+)",
                                 p.read_text(errors="replace"), re.M):
                REL_FNS.setdefault(m.group(1), int(m.group(2), 16))
    return REL_FNS.get(name)


def dol_callee(c, dol):
    """(name to look for in the port, size) of a DOL call target."""
    syms, byname = dol
    m = re.match(r"fn_(8[0-9A-Fa-f]{7})", c)
    addr = int(m.group(1), 16) if m else byname.get(c) or split_fn_addr(c)
    name, size = syms.get(addr, (c, addr is None and rel_fn_size(c) or 0x100))
    if re.fullmatch(r"fn_8[0-9A-Fa-f]{7}", name) and not m:
        name = c        # today's symbols have no name for it; the split's says what it is
    return name, size


def crbit(s):
    s = s.strip()
    m = re.fullmatch(r"(?:4\*cr(\d)\+)?(lt|gt|eq|so|un)", s)
    if m:
        return int(m.group(1) or 0), {"un": "so"}.get(m.group(2), m.group(2))
    m = re.fullmatch(r"(\d+)", s)
    return (int(s) // 4, ac.COND_NAMES[int(s) % 4]) if m else (None, s)


def cr_field(a):
    m = re.fullmatch(r"cr(\d)", a.strip())
    return int(m.group(1)) if m else None


def retail_facts(mod, fn):
    """calls, float constants, conditional-branch count, float compare sites [(off, kind)]."""
    calls, consts, conds, fcmps = [], [], 0, []
    base = {}          # reg -> rodata label from `addi rD, rS, lbl@l`
    live = {}          # cr field -> (fcmp offset, has cror)
    for i in fn.ins:
        a = i.args
        if i.op == "bl" and a:
            calls.append(a[0].strip('"'))
        elif CONDB.match(i.op) and not i.op.startswith("bdnz"):
            conds += 1
        if i.op in ("fcmpo", "fcmpu") and a:
            cf = cr_field(a[0]) if len(a) == 3 else 0
            live[cf] = [i.off, False, i.op]
            continue
        if i.op in ("cror", "crnor", "crorc") and len(a) == 3:
            (df, db), (s1f, s1), (s2f, s2) = crbit(a[0]), crbit(a[1]), crbit(a[2])
            if s1f in live and db == "eq" and {s1, s2} in ({"lt", "eq"}, {"gt", "eq"}):
                live[s1f][1] = True
            continue
        if i.op == "mfcr" and live:
            for cf, (off, cr, op) in live.items():
                fcmps.append((off, "nonstrict" if cr else "strict", "mfcr"))
            live = {}
            continue
        m = re.match(r"^b(lt|le|gt|ge|nl|ng|eq|ne)(?:lr|ctr)?$", i.op)
        if m:
            cf = cr_field(a[0]) if a and re.fullmatch(r"cr\d", a[0].strip()) else 0
            if cf in live:
                off, cr, op = live.pop(cf)
                if cr:
                    fcmps.append((off, "nonstrict", "cror+" + i.op))
                elif m.group(1) in ("eq", "ne"):
                    pass          # == / != : NaN arm is C's either way
                else:
                    fcmps.append((off, "strict", i.op))
        if i.op in ("b", "blr", "bctr"):
            live = {}
        # rodata float loads
        if i.op == "addi" and len(a) == 3:
            mm = re.match(r"(lbl_\w+?)(?:\+0x([0-9A-Fa-f]+))?@l$", a[2])
            if mm:
                base[a[0]] = (mm.group(1), int(mm.group(2) or "0", 16))
                continue
        if i.op in ("lfs", "lfsu", "lfd", "lfdu") and len(a) == 2:
            kind = "d" if i.op.startswith("lfd") else "f"
            mm = re.match(r"(lbl_\w+?)(?:\+0x([0-9A-Fa-f]+))?@(?:l|sda21)\((r\d+)\)$", a[1])
            if mm:
                v = ac.rod_value(mod, mm.group(1), int(mm.group(2) or "0", 16), kind)
            else:
                mm = re.match(r"(-?(?:0x[0-9A-Fa-f]+|\d+))\((r\d+)\)$", a[1])
                v = None
                if mm and mm.group(2) in base:
                    lbl, d = base[mm.group(2)]
                    v = ac.rod_value(mod, lbl, d + int(mm.group(1), 0), kind)
            if isinstance(v, float):
                consts.append(v)
        if a and a[0] in base and i.op not in ("lfs", "lfd", "lfsu", "lfdu", "stfs", "stfd", "lwz", "lbz",
                                                "lhz", "lha", "stw", "stb", "sth", "cmpwi", "cmplwi"):
            base.pop(a[0], None)
    return calls, consts, conds, fcmps


# ---------------------------------------------------------------- port side

def marker_files():
    """Files with a `// @ fn_` marker, plus the port-only sources (Port*.cpp/.hpp: their helpers)."""
    out = []
    for src in port_progress.SOURCES:
        for p in src.rglob("*"):
            if p.suffix in (".c", ".cpp", ".h", ".hpp", ".inc"):
                try:
                    if p.name.startswith("Port") or MARKER_FILE.search(p.read_text(errors="replace")):
                        out.append(p)
                except OSError:
                    pass
    return sorted(out)


def norm_id(s):
    m = re.fullmatch(r"fn_(\d+)_([0-9A-Fa-f]+)", s, re.I)
    return (int(m.group(1)), int(m.group(2), 16)) if m else None


def port_functions(paths, display=None):
    """(mid, off) -> CppFunc for every marked body; also helpers (name -> [code]) and file texts.

    A body is found the way asmcheck finds it (`// @ fn_X` in the comment block above it); a marker
    that sits elsewhere (after the body, in a class) falls back to a def citing the id first or
    named x<OFF>_ in the same file. In-class bodies asmcheck does not parse are not checked.
    """
    funcs, helpers, texts = {}, {}, {}
    for p in paths:
        try:
            defs = ac.parse_cpp(str(p), deep=True)
            text = Path(p).read_text(errors="replace")
        except OSError:
            continue
        shown = display.get(str(p), p) if display else p
        texts[shown] = text
        mk = [(norm_id(m.group(0)[m.group(0).index("fn_"):]), text.count("\n", 0, m.start()) + 1)
              for m in MARKER_FILE.finditer(text)]
        want = {k for k, _ in mk}
        for f in defs:
            f.file = shown
            if f.marked:
                for x in f.ids:
                    k = norm_id(x)
                    if k:
                        funcs.setdefault(k, f)
            # every def, marked or not, is a helper a caller may have inlined
            helpers.setdefault(f.short, []).append((f.cls, f.code, shown))
        for k, ln in mk:   # a marker inside a body (a switch arm) is that body's
            if k is None or k in funcs:
                continue
            for f in defs:
                if f.line < ln <= f.end_line:
                    funcs[k] = f
                    INBODY.add(k)   # the enclosing body has more code than the retail fn: no shape
                    break
        for k in want - set(funcs):
            if k is None:
                continue
            for f in defs:
                if (f.ids and norm_id(f.ids[0]) == k) or re.fullmatch(rf"x0*{k[1]:X}_\w*", f.short or "", re.I):
                    funcs[k] = f
                    break
    return funcs, helpers, texts


def add_header_helpers(helpers, texts):
    """Inline bodies of the headers no marker sits in, as helpers of names the port defines nowhere
    else (CEnergyProjectile::AddCollisionCooldown is the CCollisionCooldowns::Add retail calls)."""
    have = set(helpers)
    for p in sorted((ROOT / "include").rglob("*.hpp")):
        if p in texts or str(p) in texts:
            continue
        try:
            defs = ac.parse_cpp(str(p), deep=True)
        except (OSError, RecursionError):
            continue
        for f in defs:
            if f.short and f.short not in have and f.code.count("\n") <= 12:
                helpers.setdefault(f.short, []).append((f.cls, f.code, p))


def pick_helper(cands, cls, file):
    """The def a call by this name reaches: the only one; else one in the caller's file (a
    file-static or anonymous-namespace helper, a helper struct's member; the caller's class or a
    free one when several); else the caller's class."""
    if len(cands) == 1:
        return cands
    local = [c for c in cands if c[2] == file]
    if len(local) > 1:
        local = [c for c in local if c[0] in (cls, None, "")]
    return local or [c for c in cands if c[0] == cls]


def expand(code, helpers, depth=2, cls=None, file=None):
    seen, todo, out = set(), [code], [code]
    for _ in range(depth):
        nxt = []
        for b in todo:
            for name in set(re.findall(r"(~?\w+)\s*\(", b)):
                if name not in helpers or name in seen:
                    continue
                cands = pick_helper(helpers[name], cls, file)
                if len(cands) == 1 and cands[0][1] != code:
                    seen.add(name)
                    nxt.append(cands[0][1])
        out += nxt
        todo = nxt
    return "\n".join(out)


INC_CACHE = {}


def included(f, ctx):
    """Text of the `#include "X.inc"` files a body pastes in (the generated SLdr property readers
    an object loader includes after `SLdrX sldrThis;`; the parsed body has them blanked), with the
    sibling X.hpp, whose inline typedef readers retail's loader inlines or emits as module locals."""
    out = []
    raw = "\n".join(ctx["texts"][f.file].split("\n")[f.line - 1:f.line + f.code.count("\n")])
    incs = re.findall(r'^\s*#\s*include\s+"([^"]+\.inc)"', raw, re.M)
    for inc in incs + [i[:-len(".inc")] + ".hpp" for i in incs]:
        if inc not in INC_CACHE:
            p = next((ROOT / top / inc for top in ("include", "src") if (ROOT / top / inc).is_file()), None)
            INC_CACHE[inc] = p.read_text(errors="replace") if p else ""
        out.append(INC_CACHE[inc])
    return "\n".join(out)


def strip_strings(s):
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', s)


def port_compares(code):
    """(strict, nonstrict, negated strict, negated nonstrict) relational compares in the code."""
    toks = ac.drop_templates(ac.lex(strip_strings(code), 1))
    t = [x[0] for x in toks]
    s = n = ns = nn = 0
    for i, x in enumerate(t):
        if x in ("<", ">"):
            s += 1
        elif x in ("<=", ">="):
            n += 1
        else:
            continue
        # negated: the compare sits directly inside `!( ... )`
        d, j = 0, i - 1
        while j >= 0:
            if t[j] == ")":
                d += 1
            elif t[j] == "(":
                if d == 0:
                    break
                d -= 1
            elif d == 0 and t[j] in ("&&", "||", "?", ":", ",", ";", "{", "}", "="):
                j = -1
                break
            j -= 1
        if j >= 1 and t[j - 1] == "!":
            if x in ("<", ">"):
                ns += 1
            else:
                nn += 1
    # inline helpers whose body is a strict compare (rstl/math.hpp, CMath.hpp): retail inlines them
    # as a plain fcmpo + b<cond>; Clamp is two
    s += len(re.findall(r"\b(?:min_val|max_val|CMath::Min|CMath::Max)\s*(?:<[^<>()]*>)?\s*\(", code))
    s += 2 * len(re.findall(r"\bCMath::Clamp\s*(?:<[^<>()]*>)?\s*\(", code))
    return s, n, ns, nn


# ---------------------------------------------------------------- effect

def locked_members():
    """Member names declared with a TLockedToken type (bare, optional_object<>, containers):
    name -> True when a container (push_back adds a lock)."""
    out = {}
    decl = re.compile(r"^\s*(?:mutable\s+)?[\w:]*(?:<.*TLockedToken.*>|TLockedToken\s*<.*>)\s*(\w+)\s*(?:\[[^\]]*\])?\s*;")
    for top in ("include", "src"):
        for p in (ROOT / top).rglob("*"):
            if p.suffix not in (".h", ".hpp", ".cpp"):
                continue
            try:
                text = p.read_text(errors="replace")
            except OSError:
                continue
            if "TLockedToken" not in text:
                continue
            for line in text.split("\n"):
                if "TLockedToken" not in line or "(" in line.split("//")[0]:
                    continue
                m = decl.match(line.split("//")[0])
                if m:
                    out[m.group(1)] = bool(re.search(r"vector\s*<", line))
    return out


def skip_template(s, i):
    """Index after the balanced `<...>` starting at s[i] == '<' (len(s) if unbalanced)."""
    d = 0
    while i < len(s):
        if s[i] == "<":
            d += 1
        elif s[i] == ">":
            d -= 1
            if d == 0:
                return i + 1
        elif s[i] in ";{}":
            return i
        i += 1
    return i


def args_at(s, i):
    """Text inside the balanced parens/braces opening at s[i]."""
    close = {"(": ")", "{": "}"}[s[i]]
    d, j = 0, i
    while j < len(s):
        if s[j] in "({":
            d += 1
        elif s[j] in ")}":
            d -= 1
            if d == 0:
                return s[i + 1:j]
        j += 1
    return s[i + 1:]


def locked_constructions(code):
    """TLockedToken<..>(x) temporaries and `TLockedToken<..> v(x)` / `v = x` locals."""
    n = 0
    for m in re.finditer(r"\bTLockedToken\s*<", code):
        j = skip_template(code, m.end() - 1)
        rest = code[j:j + 200]
        mm = re.match(r"\s*([({])", rest)
        if mm and args_at(rest, mm.start(1)).strip():
            n += 1
            continue
        mm = re.match(r"\s*\w+\s*(?:=(?!=)|([({]))", rest)
        if mm and (not mm.group(1) or args_at(rest, mm.start(1)).strip()):
            n += 1
    # optional_object<TLockedToken<..>>(token): the TLockedToken its ctor builds from the CToken
    for m in re.finditer(r"\boptional_object\s*(<)\s*TLockedToken\s*<", code):
        j = skip_template(code, m.start(1))
        rest = code[j:j + 200]
        mm = re.match(r"\s*\(", rest)
        if mm:
            a = args_at(rest, mm.end() - 1)
            if a.strip() and not re.search(r"\bTLockedToken\s*<", a):
                n += 1
    return n


NO_LOCK_INIT = re.compile(r"^\s*$|optional_object_null|^\s*rstl::optional_object\s*<[^()]*>\s*\(\s*\)\s*$")


def member_locks(code, ctx, ctor, cls=None):
    """member -> (locks, TLockedToken temporaries in the value) of its TLockedToken initializers /
    assignments / push_backs: one lock each (the value's temporary, or a port helper's, is that
    lock, not another one)."""
    out = {}
    own = ctx["members"].get(cls, {})
    lm = {n: c for n, c in ctx["locked"].items() if own.get(n, (True,))[0]}
    lm.update({n: v[1] for n, v in own.items() if v[0]})
    if not lm:
        return out
    names = "|".join(map(re.escape, sorted(lm)))
    pat = r"(?<![\w.>:])(?:this->)?(" + names + r")\s*(?:([({])|=(?!=)\s*|\.push_back\s*\()"
    for m in re.finditer(pat, code):
        name = m.group(1)
        if m.group(2):
            if not ctor:
                continue
            a = args_at(code, m.start(2))
        else:
            if "push_back" in m.group(0) and not lm[name]:
                continue
            a = code[m.end():code.find(";", m.end()) if ";" in code[m.end():] else len(code)]
        if NO_LOCK_INIT.search(a):
            continue
        n, t = out.get(name, (0, 0))
        out[name] = (n + 1, t + locked_constructions(a))
    return out


MEMBER_DECL = re.compile(r"^\s*(?:mutable\s+)?[\w:<>,\s*&\[\]]*?[\w>*&\]]\s+\**(\w+)\s*(?:\[[^\]]*\])?\s*;"
                         r"(?:\s*//\s*(?:\+|at\s+)?0x([0-9A-Fa-f]+)\b)?")
CLASS_HEAD = re.compile(r"^\s*(?:class|struct)\s+(?:\w+\s+)?(\w+)\s*(?:final\s*)?(?::[^{;]*)?\{?\s*$")


LOCKED_DECL = re.compile(r"^\s*(?:mutable\s+)?([\w:]*(?:<.*TLockedToken.*>|TLockedToken\s*<.*>))\s*(\w+)\s*"
                         r"(?:\[[^\]]*\])?\s*(=(?!=)|\{|;)")


def class_info():
    """(layouts, members) from header declarations. layouts: class -> [(offset, member)], an
    `x<hex>_` name or a trailing `// 0x<hex>` comment giving the offset. members: class ->
    {member: (TLockedToken type, container, default-initialized with a lock, plain class type)}."""
    lay, mem = {}, {}
    for top in ("include", "src"):
        for p in (ROOT / top).rglob("*"):
            if p.suffix not in (".h", ".hpp"):
                continue
            try:
                lines = ac.drop_non_pc_arms(p.read_text(errors="replace")).split("\n")
            except OSError:
                continue
            stack, depth, pending = [], 0, None
            for ln, line in enumerate(lines):
                code = line.split("//")[0]
                m = CLASS_HEAD.match(code)
                if m:
                    pending = m.group(1)
                opens, closes = code.count("{"), code.count("}")
                if pending and opens:
                    stack.append((pending, depth))
                    pending = None
                elif pending and ";" in code:
                    pending = None
                if stack and depth == stack[-1][1] + 1:
                    cls = stack[-1][0]
                    lm = LOCKED_DECL.match(code)
                    mm = MEMBER_DECL.match(line)
                    if lm:
                        init = code[lm.end(3):].strip() if lm.group(3) != ";" else ""
                        if lm.group(3) == "=" and not init and ln + 1 < len(lines):
                            init = lines[ln + 1].split("//")[0].strip()
                        nsdmi = lm.group(3) != ";" and not NO_LOCK_INIT.search(init.rstrip(";}").strip())
                        mem.setdefault(cls, {})[lm.group(2)] = (True, bool(re.search(r"vector\s*<", lm.group(1))), nsdmi, None)
                    elif mm and "(" not in code:
                        tm = re.match(r"\s*(?:mutable\s+)?(?:\w+::)*(\w+)\s+\w+\s*;", code)
                        mem.setdefault(cls, {}).setdefault(mm.group(1), (False, False, False, tm and tm.group(1)))
                    if mm and "(" not in code:
                        name = mm.group(1)
                        xm = re.match(r"x([0-9A-Fa-f]+)_", name)
                        off = int(xm.group(1), 16) if xm else (int(mm.group(2), 16) if mm.group(2) else None)
                        if off is not None:
                            lay.setdefault(cls, []).append((off, name))
                depth += opens - closes
                while stack and depth <= stack[-1][1]:
                    stack.pop()
    return {k: sorted(set(v)) for k, v in lay.items()}, mem


def member_at(layout, off):
    """The member a this-relative offset falls in (the last one at or below it)."""
    best = None
    for o, n in layout or ():
        if o <= off:
            best = (o, n)
    return best


GPR = re.compile(r"r\d+$")


def retail_effects(fn, ctx, depth=0, seen=None, base=0):
    """effect -> [(this-relative offset of the call's r3 or None, in a callee)] for a retail body's
    effect calls, through unported module callees (a member ctor called with r3 = this + off adds
    off). A token
    Locked/Unlocked on the stack is a temporary the port's TLockedToken expression stands for."""
    seen = set() if seen is None else seen
    out = {}
    regs = {"r3": base} if base is not None else {}   # GPR -> this + offset
    stack = set()                                      # GPRs pointing into the frame
    for i in fn.ins:
        a = i.args
        if i.op == "bl" and a:
            r3 = regs.get("r3")
            on_stack = "r3" in stack
            for r in ["r0"] + [f"r{n}" for n in range(3, 13)]:
                regs.pop(r, None)
                stack.discard(r)
            c = a[0].strip('"')
            m = FNID.match(c)
            if m:
                ck = (int(m.group(1)), int(m.group(2), 16))
                if ck in ctx["pnames"] or ck in seen or depth >= 3 or on_stack:
                    continue
                seen.add(ck)
                mname = ctx["ids"].get(ck[0])
                sub = ctx["retail"][mname][1].get(ck[1]) if mname in ctx["retail"] else None
                if sub is not None:
                    for k, v in retail_effects(sub, ctx, depth + 1, seen, r3).items():
                        out.setdefault(k, []).extend(v)
                continue
            name = ctx["dol_name"](c)
            for k, rx, _ in EFFECTS:
                if rx.match(name) and not (on_stack and k in ("Lock", "Unlock")):
                    out.setdefault(k, []).append((r3, depth > 0))
            continue
        if not a or not GPR.match(a[0]) or i.op.startswith(("st", "cmp", "b", "tw")):
            continue
        d = a[0]
        regs.pop(d, None)
        stack.discard(d)
        if i.op == "mr" and len(a) == 2:
            if a[1] in regs:
                regs[d] = regs[a[1]]
            elif a[1] in stack or a[1] == "r1":
                stack.add(d)
        elif i.op in ("addi", "addic", "addic.") and len(a) == 3 and re.fullmatch(r"-?(?:0x[0-9A-Fa-f]+|\d+)", a[2]):
            if a[1] in regs:
                regs[d] = regs[a[1]] + int(a[2], 0)
            elif a[1] == "r1" or a[1] in stack:
                stack.add(d)
    return out


def check_effects(fn, f, ctx):
    """`effect` findings of a ctor/dtor: retail effect calls the port performs fewer times. A Lock
    whose token offset falls in a member the class header places is that member's: the port must
    lock that member (`m.Lock()`, `m.Token().Lock()`, a TLockedToken member built or assigned),
    whatever else it locks; the other Locks are compared by count."""
    ctor = f.short == f.cls
    if not (ctor or (f.short or "").startswith("~")):
        return []
    want = retail_effects(fn, ctx)
    if not want:
        return []
    body = strip_strings(ac.strip_comments_keep(expand(f.code, ctx["helpers"], cls=f.cls, file=f.file)))
    own = strip_strings(ac.strip_comments_keep(f.code))
    out = []
    for k, _, prx in EFFECTS:
        if k not in want:
            continue
        have = len(re.findall(prx, body))
        need = want[k]
        if k == "Lock":
            ml = member_locks(own, ctx, ctor, f.cls)
            if ctor:     # default member initializers that lock run in every ctor
                for n, v in ctx["members"].get(f.cls, {}).items():
                    if v[2] and n not in ml:
                        ml[n] = (1, 0)
            have += locked_constructions(own) + sum(n - t for n, t in ml.values())
            if ctor:     # a member built by its own port ctor that locks (an SShotAttack's info)
                for n, v in ctx["members"].get(f.cls, {}).items():
                    if v[3] and re.search(rf"[:,]\s*{n}\s*\(", own):
                        for c, hc, _ in ctx["helpers"].get(v[3], []):
                            if c == v[3]:
                                have += len(re.findall(prx, hc)) + locked_constructions(hc)
                                break
            layout = ctx["layouts"].get(f.cls)
            rest = []
            for off, via in need:
                mem = member_at(layout, off) if off is not None else None
                if mem is None or mem[0] != off:     # a token sits at +0 of its member
                    if not via:     # a callee's unplaced lock is that (host) ctor's, as the port's
                        rest.append(off)
                    continue
                name = mem[1]
                if name in ml or re.search(rf"\b{re.escape(name)}\b[^;{{}}]*\bLock\s*\(", body):
                    have -= 1
                    continue
                out.append(("effect", f"Lock: retail locks +0x{off:X} ({name}), port does not"))
            need, have = rest, max(have, 0)
        if have < len(need):
            out.append(("effect", f"{k}: retail {len(need)}, port {have}"))
    return out


# ---------------------------------------------------------------- compare

def plumbing(ck, ctx, depth=0):
    """An unported module function whose calls are all rstl/string/allocation plumbing (or more
    such functions), at least one: a template instance (vector reserve, string copy)."""
    if ck in ctx["plumb"]:
        return ctx["plumb"][ck]
    ctx["plumb"][ck] = False
    m = ctx["ids"].get(ck[0])
    fn = ctx["retail"][m][1].get(ck[1]) if m else None
    ok = False
    if fn is not None and depth < 3:
        calls = [i.args[0].strip('"') for i in fn.ins if i.op == "bl" and i.args]
        ok = bool(calls)
        for c in calls:
            mm = FNID.match(c)
            if mm:
                sub = (int(mm.group(1)), int(mm.group(2), 16))
                if sub in ctx["pnames"] or not plumbing(sub, ctx, depth + 1):
                    ok = False
            elif not (PLUMB.search(c) or ac.is_runtime(c)):
                ok = False
            if not ok:
                break
    ctx["plumb"][ck] = ok
    return ok


def value_helper(ck, ctx):
    """An unported module function that only builds a value at r3 from its other arguments:
    straight line, no calls, every store through r3, r3 never read or moved (an implicit struct
    copy, a cross product, an aggregate's ctor) - the port's `=`, operator or brace init."""
    m = ctx["ids"].get(ck[0])
    fn = ctx["retail"][m][1].get(ck[1]) if m else None
    if fn is None:
        return False
    for i in fn.ins:
        a = i.args
        if i.op.startswith("b") and i.op != "blr":
            return False
        if i.op.startswith("st") and len(a) == 2 and not re.search(r"\(r[13]\)$", a[1]):
            return False
        if i.op.startswith("l") and len(a) == 2 and a[1].endswith("(r3)"):
            return False
        if a and a[0] == "r3" and not i.op.startswith(("st", "cmp")):
            return False
    return True


FREG = re.compile(r"f(\d+)$")
FLIT = re.compile(r"^[-+]?(?:\d+\.\d*(?:[eE][-+]?\d+)?|\.\d+(?:[eE][-+]?\d+)?|\d+[eE][-+]?\d+)[fF]?$")


def retail_call_floats(mod, fn):
    """[(callee, {fN: value})] per `bl`: the rodata constants the float argument registers hold
    there (a register another instruction wrote, or any state across a label, is dropped)."""
    out, base, fr = [], {}, {}
    targets = set(fn.labels.values())
    for i in fn.ins:
        a = i.args
        if i.idx in targets:
            fr = {}
        if i.op == "bl" and a:
            out.append((a[0].strip('"'), {k: v for k, v in fr.items() if v is not None}))
            fr = {}
            base = {}
            continue
        if i.op == "addi" and len(a) == 3:
            mm = re.match(r"(lbl_\w+?)(?:\+0x([0-9A-Fa-f]+))?@l$", a[2])
            if mm:
                base[a[0]] = (mm.group(1), int(mm.group(2) or "0", 16))
                continue
        if i.op in ("lfs", "lfd", "lfsu", "lfdu") and len(a) == 2 and FREG.match(a[0]):
            kind = "d" if i.op.startswith("lfd") else "f"
            v = None
            mm = re.match(r"(lbl_\w+?)(?:\+0x([0-9A-Fa-f]+))?@(?:l|sda21)\((r\d+)\)$", a[1])
            if mm:
                v = ac.rod_value(mod, mm.group(1), int(mm.group(2) or "0", 16), kind)
            else:
                mm = re.match(r"(-?(?:0x[0-9A-Fa-f]+|\d+))\((r\d+)\)$", a[1])
                if mm and mm.group(2) in base:
                    lbl, d = base[mm.group(2)]
                    v = ac.rod_value(mod, lbl, d + int(mm.group(1), 0), kind)
            fr[a[0]] = f32(v) if isinstance(v, float) and v == v else None
        elif a and FREG.match(a[0]) and not i.op.startswith(("st", "fcmp")):
            fr[a[0]] = None
        elif a and a[0] in base and i.op not in ("lfs", "lfd", "lfsu", "lfdu", "stfs", "stfd", "lwz", "lbz",
                                                  "lhz", "lha", "stw", "stb", "sth", "cmpwi", "cmplwi"):
            base.pop(a[0], None)
    return out


def split_call_args(code, name):
    """Top-level argument strings of every `name(...)` call in code."""
    res = []
    for m in re.finditer(rf"(?<![\w:~])(?:\w+::)*{re.escape(name)}\s*\(", strip_strings(code)):
        s = strip_strings(code)
        i, d, cur, args = m.end(), 1, "", []
        while i < len(s) and d:
            c = s[i]
            if c in "([{":
                d += 1
            elif c in ")]}":
                d -= 1
                if d == 0:
                    break
            if c == "," and d == 1:
                args.append(cur.strip())
                cur = ""
            else:
                cur += c
            i += 1
        if cur.strip():
            args.append(cur.strip())
        res.append(args)
    return res


def check_args(mod, fn, f, ctx):
    """`args` findings: swapped literal float arguments (see the docstring)."""
    out = []
    calls = retail_call_floats(mod, fn)
    for idx, (c, fr) in enumerate(calls):
        if len(fr) < 2 or sum(1 for x, _ in calls if x == c) != 1:
            continue
        regs = sorted(int(k[1:]) for k in fr)   # gaps are computed args: literal order is unchanged
        rv = [fr[f"f{n}"] for n in regs]
        m = FNID.match(c)
        if m:
            ck = (int(m.group(1)), int(m.group(2), 16))
            if ck not in ctx["pnames"]:
                continue
            name = ctx["pnames"][ck]
        else:
            name = ac.base_name(ctx["dol_name"](c))
        if not name or name.startswith(("operator", "~")):
            continue
        sites = split_call_args(f.code, name)
        if len(sites) != 1:
            continue
        pv = []
        for x in sites[0]:
            if FLIT.match(x):
                pv.append(f32(float(x.rstrip("fF"))))
        if len(pv) != len(rv) or len(set(rv)) < 2 or pv == rv:
            continue
        if sorted(pv) != sorted(rv):
            continue
        out.append(("args", f"call {name} at retail bl #{idx}: float args {','.join(f'f{n}' for n in regs)} = "
                            f"{', '.join(f'{v:g}' for v in rv)} but the port's literal floats are "
                            f"{', '.join(f'{v:g}' for v in pv)}"))
    return out


def check(key, f, retail, ctx):
    mod, fn = retail
    calls, consts, conds, fcmps = retail_facts(mod, fn)
    code = f.code
    body = expand(code, ctx["helpers"], cls=f.cls, file=f.file) + "\n" + included(f, ctx)
    comments = "\n".join(t for _, t in f.comment_lines)
    names_text = body + "\n" + comments
    out = []
    seen = set()
    mid = key[0]
    for c in calls:
        if c in seen or ac.is_runtime(c) or c.startswith(SKIP_CALL) or "rstl" in c or "basic_string" in c:
            continue
        seen.add(c)
        m = FNID.match(c)
        local = mod.funcs.get(c) if not m else None
        if local is not None and local.ins:     # a module function the split named (an inline accessor)
            m = FNID.match(fid(mid, local.start))
        if m:
            ck = (int(m.group(1)), int(m.group(2), 16))
            size = ctx["sizes"].get(ck, len(local.ins) * 4 if local is not None else 0x100)
        else:
            ck = c
            dname, size = dol_callee(c, ctx["dol"])
        ctx["sites"][ck] = ctx["sites"].get(ck, 0) + 1
        if size <= SMALL:
            continue
        if m:
            off = ck[1]
            if re.search(rf"fn_{ck[0]}_0*{off:X}\b|\bx0*{off:X}_|\b0x0*{off:X}\b", names_text, re.I):
                continue
            if local is not None and re.search(rf"\b{re.escape(ac.base_name(c))}\b", names_text):
                continue    # the split's name for it
            if ck not in ctx["pnames"] and plumbing(ck, ctx) and not ctx["strict"]:
                continue    # an rstl/string/allocation template instance the port's containers are
            if ck not in ctx["pnames"] and value_helper(ck, ctx) and not ctx["strict"]:
                continue    # a struct copy / vector op the port writes as an expression
            if ck in ctx["pnames"]:
                pn = ctx["pnames"][ck]
                cf = ctx["funcs"][ck]
                if pn.startswith(("operator", "~")) or pn == cf.cls:  # a ctor: initializer lists name the type
                    continue
                if re.search(rf"\b{re.escape(pn)}\b", body):
                    continue
                out.append(("call", f"{c} {pn} (ported) not called", ck))
            elif not (f.short.startswith("~") or f.short == f.cls) or ctx["strict"]:
                # in a ctor/dtor an unported module callee is mostly a member's ctor/dtor
                out.append(("call", f"{c} (unported) not named", ck))
        else:
            short = ac.base_name(dname)
            if short.startswith("TCastTo"):
                # or the port's own CastX for a REL class the DOL's TCastToPtr<X> is TryCast(e, id) of
                cast = re.search(r"<\d+C(\w+)>", dname)
                short = r"TCastTo(?:Const)?Ptr" + (f"|Cast{cast.group(1)}" if cast else "")
            if ac.is_runtime(dname) or dname.startswith(SKIP_CALL):
                continue
            if short in SKIP_DOL and not ctx["strict"]:
                continue
            pat = short if short.startswith("TCastTo") else DOL_ALIAS.get(short, re.escape(short))
            if short.startswith("SetLoader_"):
                pat = rf"{short}|Set\w*_FuncPtrs"  # the port's SetSX_FuncPtrs registers the loaders
            if short in LIBM:
                pat += "f?"  # the port's float overloads (sinf, acosf)
            if short.startswith("LoadTypedefSLdr") and short not in DOL_ALIAS:
                # the split's reader names; the port's generated readers drop the SLdr
                pat = r"LoadTypedef(?:SLdr)?" + re.escape(short[len("LoadTypedefSLdr"):])
            if not short or re.search(rf"\b(?:{pat})\b", names_text):
                continue
            out.append(("call", f"{dname[:60]} not named", ck))
    if consts:
        if f.file not in ctx["lits"]:
            text = ac.strip_comments_keep(ctx["texts"][f.file])
            ctx["lits"][f.file] = {f32(float(x)) for x in FLOAT_LIT.findall(text)}
        lits = ctx["lits"][f.file]
        lits = lits | {f32(1.0 / x) for x in lits if x} | {float(x) for x in INT_LIT.findall(body)} | {f32(float(x)) for x in FLOAT_LIT.findall(body)}
        missing = []
        for v in consts:
            if v != v or v == 0.0 or v in MAGIC or abs(v) > 1e30:
                continue
            fv = f32(v)
            if any(abs(fv - lt) <= 1e-6 * max(1.0, abs(fv)) or abs(-fv - lt) <= 1e-6 * max(1.0, abs(fv)) for lt in lits):
                continue
            missing.append(f"{v:.9g}")
        if missing:
            out.append(("const", "retail constant(s) not in the port file: " + ", ".join(sorted(set(missing))[:6])))
    out += check_args(mod, fn, f, ctx)
    out += check_effects(fn, f, ctx)
    nonblank = [x for x in code.split("\n") if x.strip()]
    shape_body = code if len(nonblank) > 5 else body
    pc = len(PORT_COND.findall(strip_strings(shape_body)))
    pc_own = len(PORT_COND.findall(strip_strings(code)))   # too many: the body's own, not its helpers'
    plines = len([x for x in shape_body.split("\n") if x.strip()])
    marked_ids = [norm_id(x) for x in f.ids if norm_id(x)]
    if f.short.startswith("~") or key in INBODY or (len(marked_ids) > 1 and key != marked_ids[0]):
        pass        # also a body that carries several ids: its shape is the first one's        # member dtors inline into retail's; an arm marked inside a bigger body
    elif conds >= 4 and pc == 0 and f.short != f.cls:   # a ctor's branches are its members' ctors
        out.append(("shape", f"{conds} retail conditional branches, no port conditions"))
    elif pc_own > 2 * conds + 4:
        pc = pc_own
        out.append(("shape", f"{pc} port conditions vs {conds} retail conditional branches"))
    if len(fn.ins) >= 80 and plines <= 6 and not (f.short.startswith("~") or f.short == f.cls):
        out.append(("shape", f"{len(fn.ins)} retail instructions behind a {plines}-line port body"))
    if fcmps:
        s, n, ns, nn = port_compares(body)
        rs = [x for x in fcmps if x[1] == "strict"]
        rn = [x for x in fcmps if x[1] == "nonstrict"]
        sites = lambda xs: " ".join(f"{o:X}:{w}" for o, _, w in xs[:6])  # noqa: E731
        if len(rs) > s:
            out.append(("nan", f"{len(rs)} retail plain float branches, port has {s} `<`/`>` "
                               f"(a `!(a <= b)`/`>=` where retail has b<cond> flips the NaN arm): {sites(rs)}"))
        if len(rn) > n:
            out.append(("nan", f"{len(rn)} retail cror float compares, port has {n} `<=`/`>=` "
                               f"(a `<`/`>` or `!(a < b)` there flips the NaN arm): {sites(rn)}"))
        if nn > len(rn):
            out.append(("nan", f"port has {nn} negated `!(a <= b)` forms, retail {len(rn)} cror float "
                               f"compares (a negated <=/>= is NaN-true; retail plain b<cond> is not): {sites(rs)}"))
    return out


# ---------------------------------------------------------------- --diff

def changed_since(base, cur_funcs):
    """Marked ids whose body is new or differs from BASE's."""
    r = subprocess.run(["git", "-C", str(ROOT), "diff", "--name-only", base, "--", "src", "platform", "include"],
                       capture_output=True, text=True, check=True)
    files = [ROOT / x for x in r.stdout.split() if x.endswith((".c", ".cpp", ".h", ".hpp", ".inc"))]
    files = set(files)
    with tempfile.TemporaryDirectory(prefix="rdiff-") as td:
        old_paths, display = [], {}
        for i, p in enumerate(sorted(files)):
            r = subprocess.run(["git", "-C", str(ROOT), "show", f"{base}:{rel(p)}"], capture_output=True)
            if r.returncode:
                continue
            q = Path(td) / f"{i}_{p.name}"
            q.write_bytes(r.stdout)
            old_paths.append(q)
            display[str(q)] = p
        old, _, _ = port_functions(old_paths, display)
    norm = lambda s: re.sub(r"\s+", " ", s).strip()  # noqa: E731
    return {k for k, f in cur_funcs.items()
            if Path(f.file) in files and (k not in old or norm(old[k].code) != norm(f.code))}


def marker_locations(paths):
    """(mid, off) -> [(file, line)] of every `// @ fn_` marker."""
    out = {}
    for p in paths:
        try:
            text = Path(p).read_text(errors="replace")
        except OSError:
            continue
        for m in MARKER_FILE.finditer(text):
            k = norm_id(m.group(0)[m.group(0).index("fn_"):])
            if k:
                out.setdefault(k, []).append((Path(p), text.count("\n", 0, m.start()) + 1))
    return out


def why_unparsed(path, line):
    """What follows a marker that has no parsed body: data table, declaration, `= default`, other."""
    lines = Path(path).read_text(errors="replace").split("\n")
    i = line
    while i < len(lines) and (not lines[i].strip() or lines[i].lstrip().startswith(("//", "#", "/*", "*"))):
        i += 1
    txt = ""
    while i < len(lines) and len(txt) < 400:
        txt += lines[i] + " "
        if re.search(r"[{;]", lines[i]):
            break
        i += 1
    head = re.split(r"[{;]", txt)[0]
    if re.match(r"\s*(?:template\s*<[^>]*>\s*)?(?:class|struct)\b", txt) and not re.search(r"\bclass\s+\w+\s*;", txt[:200]):
        return "implicit (marker on the class)"
    if "= default" in txt or "= delete" in txt:
        return "= default/delete"
    if re.search(r"\]\s*=|=\s*\{|=\s*&|^\s*(?:static\s+)?(?:const\s+)?[\w:<>]+\s+\w+\s*(?:=|;)", txt) and "(" not in head.split("=")[0]:
        return "data"
    if "[" in head.split("(")[0] and "=" in txt:
        return "data"
    if txt.rstrip().endswith(";") and "(" in head:
        return "declaration only"
    return "other"


def shape_checked(f, key):
    marked_ids = [norm_id(x) for x in f.ids if norm_id(x)]
    return not (f.short.startswith("~") or key in INBODY or (len(marked_ids) > 1 and key != marked_ids[0]))


def coverage(marks, funcs, locs, ids, retail, helpers):
    """module name -> counter dict (see --coverage)."""
    cov = {}
    unparsed = {}
    for k in sorted(marks):
        mname = ids.get(k[0], f"mod{k[0]}")
        c = cov.setdefault(mname, dict(markers=0, bodies=0, inbody=0, asm=0, shape=0, fc_sites=0, fc_cmp=0, fc_fns=0, fc_fns_cmp=0))
        c["markers"] += 1
        f = funcs.get(k)
        if f is None:
            unparsed.setdefault(mname, []).append(k)
            continue
        c["bodies"] += 1
        if k in INBODY:
            c["inbody"] += 1
        m = retail.get(mname)
        if m is None or k[1] not in m[1]:
            continue
        c["asm"] += 1
        if shape_checked(f, k):
            c["shape"] += 1
        fcmps = retail_facts(m[0], m[1][k[1]])[3]
        if fcmps:
            body = expand(f.code, helpers, cls=f.cls, file=f.file)
            s, n, _, _ = port_compares(body)
            c["fc_fns"] += 1
            c["fc_sites"] += len(fcmps)
            seen = min(len(fcmps), s + n)
            c["fc_cmp"] += seen
            c["fc_fns_cmp"] += bool(seen)
    return cov, unparsed


def print_coverage(cov, unparsed, locs, only, verbose):
    cols = ("markers", "bodies", "inbody", "asm", "shape", "fc_fns", "fc_fns_cmp", "fc_sites", "fc_cmp")
    print("# coverage: markers | bodies parsed | of them a marker inside a bigger body | with retail asm |"
          " shape-checked | fns with retail float compares | .. of them port compares seen |"
          " retail float-compare sites | .. matched by a port compare")
    print(f"{'module':28}" + "".join(f"{c:>11}" for c in cols))
    tot = dict.fromkeys(cols, 0)
    for mname in sorted(cov):
        if only and mname not in only:
            continue
        c = cov[mname]
        for x in cols:
            tot[x] += c[x]
        print(f"{mname:28}" + "".join(f"{c[x]:>11}" for x in cols))
    print(f"{'TOTAL':28}" + "".join(f"{tot[x]:>11}" for x in cols))
    if verbose:
        for mname in sorted(unparsed):
            if only and mname not in only:
                continue
            for k in unparsed[mname]:
                for path, ln in locs.get(k, [(None, 0)])[:1]:
                    where = f"{rel(path)}:{ln}" if path else "?"
                    print(f"unparsed {fid(*k)} {mname} {where} ({why_unparsed(path, ln) if path else '?'})")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--module", action="append", default=[], help="only this REL module (repeatable)")
    ap.add_argument("--fn", action="append", default=[], help="only fn_<mod>_<off> (repeatable)")
    ap.add_argument("--diff", metavar="BASE", help="only bodies new or changed since BASE")
    ap.add_argument("--kinds", help="comma list of call,const,shape,nan,args,effect")
    ap.add_argument("--file", action="append", default=[], help="check only the marked bodies in these files")
    ap.add_argument("--top", type=int, default=40, help="whole-port listing: N worst functions")
    ap.add_argument("--coverage", action="store_true", help="per-module parse coverage table")
    ap.add_argument("--verbose", action="store_true", help="with --coverage: list unparsed markers")
    ap.add_argument("--strict", action="store_true", help="also show calls to port-model callees")
    ap.add_argument("--no-allow", action="store_true", help="also show findings retail_diff_allow.txt accepts")
    args = ap.parse_args()

    mods = sorted(p.name for p in rm.RELS.iterdir() if p.is_dir())
    retail, ids, sizes = load_retail(mods)
    funcs, helpers, texts = port_functions([Path(x).resolve() for x in args.file] if args.file else marker_files())
    add_header_helpers(helpers, texts)
    dol = dol_symbols()
    layouts, members = class_info()
    ctx = {"helpers": helpers, "texts": texts, "sizes": sizes, "dol": dol, "lits": {}, "sites": {},
           "pnames": {k: f.short for k, f in funcs.items()}, "funcs": funcs, "strict": args.strict,
           "retail": retail, "ids": ids, "plumb": {}, "locked": locked_members(),
           "layouts": layouts, "members": members,
           "dol_name": lambda c: dol_callee(c, dol)[0]}
    marks = port_progress.markers()
    found_by, nobody = {}, []
    for k in sorted(marks):
        m = ids.get(k[0])
        if m is None or k[1] not in retail[m][1]:
            continue
        if k not in funcs:
            nobody.append(k)
            continue
        found_by[k] = check(k, funcs[k], (retail[m][0], retail[m][1][k[1]]), ctx)
    missed = {}
    for fs in found_by.values():
        for x in fs:
            if x[0] == "call":
                missed[x[2]] = missed.get(x[2], 0) + 1
    model = {c for c, n in missed.items() if ctx["sites"].get(c, 0) >= 3 and n * 2 >= ctx["sites"][c]}

    sel = set(found_by)
    whole = not (args.module or args.fn or args.diff)
    if args.module:
        want = {rm.module_id(m) for m in args.module}
        sel = {k for k in sel if k[0] in want}
    if args.fn:
        want = {norm_id(x) for x in args.fn}
        for x in want - set(found_by):
            print(f"{fid(*x)}: no marked port body found" if x else "bad --fn")
        sel &= want
    if args.diff:
        sel &= changed_since(args.diff, funcs)
    if args.coverage:
        locs = marker_locations(marker_files() if not args.file else [Path(x).resolve() for x in args.file])
        cov, unparsed = coverage(marks, funcs, locs, ids, retail, helpers)
        print_coverage(cov, unparsed, locs, set(args.module), args.verbose)
        return 0
    kinds = set(args.kinds.split(",")) if args.kinds else None
    totals, rows, hidden = {}, [], 0
    allow, used, allowed = [] if args.no_allow else load_allow(), set(), 0
    for k in sorted(sel):
        found = []
        for x in found_by[k]:
            hit = next((a for a in allow if a[0] == k and a[1] == x[0] and a[2].search(x[1])), None)
            if hit:
                used.add(hit[3])
                allowed += 1
                continue
            if x[0] == "call" and x[2] in model and not args.strict:
                hidden += 1
                continue
            if not kinds or x[0] in kinds:
                found.append(x)
        for x in found:
            totals[x[0]] = totals.get(x[0], 0) + 1
        if found:
            rows.append((k, found))
    nob = [k for k in nobody if k in sel or ((args.module and ids.get(k[0]) in args.module) or whole)]
    print(f"# {len(sel)} marked function(s) checked ({len(nob)} markers without a parsed body); findings: "
          + (", ".join(f"{a} {b}" for a, b in sorted(totals.items())) or "none")
          + f"; {hidden} calls to {len(model)} port-model callees hidden (--strict shows them)"
          + (f"; {allowed} allowlisted (--no-allow shows them)" if allowed else ""))
    if whole and not kinds:
        for a in allow:
            if a[3] not in used:
                print(f"# {ALLOW.name}:{a[3]}: stale, {fid(*a[0]) if a[0] else '?'} {a[1]} matches nothing")
    if whole:
        rows.sort(key=lambda r: -len(r[1]))
        rows = rows[:args.top]
    for k, found in rows:
        f = funcs[k]
        print(f"{fid(*k)} {ids[k[0]]} {f.name} ({rel(f.file)}:{f.line})")
        for x in found:
            print(f"  {x[0]:5} {x[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
