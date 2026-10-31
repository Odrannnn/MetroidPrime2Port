#!/usr/bin/python3
"""gen_entity_schema: the per-class field schema of every CEntity subclass, for save states.

    gen_entity_schema.py                  # regenerate platform/generated/port_entity_schema.inc
                                          # and docs/SAVESTATE_SCHEMA.md
    gen_entity_schema.py --check          # exit 1 if either is stale (77 when clang-19 is absent)
    gen_entity_schema.py --add-friends    # add `friend struct PortEntitySchema;` where needed

Parses the game headers with clang (`-ast-dump=json`, the flags and forced include mp_game
compiles with), finds every class that derives from CEntity, and lists each class's own fields
in declaration order with a kind (platform/include/port_entity_schema.h) and accessors the
compiler builds by name, so nothing depends on one compiler's layout. A class's fingerprint
hashes its base and its fields' names, kinds, types and bit widths (not offsets): a later save
format can tell when a class it saved has changed.

The AST is ~800 MB of JSON; it is read one top-level declaration at a time (clang indents it, so
each `    {` ... `    }` block at depth 1 is one declaration).
"""
import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_INC = ROOT / "platform/generated/port_entity_schema.inc"
OUT_DOC = ROOT / "docs/SAVESTATE_SCHEMA.md"
CLANG = os.environ.get("CLANG", "/usr/bin/clang-19")
FRIEND = "friend struct PortEntitySchema;"
# Upstream headers stay byte-identical off TARGET_PC: the friend line is a three-line block.
FRIEND_BLOCK = ("#ifdef TARGET_PC\n{ind}{friend} // save-state field schema (platform/include/port_entity_schema.h)\n#endif\n")

# mp_game's flags (CMakeLists.txt): definitions, include directories and the forced include.
FLAGS = ["-x", "c++", "-std=gnu++20", "-fsigned-char", "-fsyntax-only", "-Wno-everything",
         "-DAURORA", "-DAURORA_ENABLE_GX", "-DTARGET_PC", "-DMUSY_TARGET=MUSY_TARGET_PC",
         "-Iplatform/include", "-Iinclude", "-Iextern/aurora/include", "-Iextern/musyx-port/include", "-Iinclude/LZO",
         "-include", "platform/compat.h"]

# Kinds, in PortSchemaKind order.
KINDS = ["bool", "int", "float", "bitfield", "math", "uid", "pod", "pointer", "smart_ptr",
         "container", "state_machine", "body_controller", "anim_data", "path_search", "other"]
PLAIN = {"bool", "int", "float", "bitfield", "math", "uid"}

MATH = {"CVector2f", "CVector3f", "CVector3d", "CUnitVector3f", "CTransform4f", "CQuaternion",
        "CUnitQuaternion", "CColor", "CAABox", "COBBox", "CMatrix3f", "CMatrix4f", "CRelAngle",
        "CAxisAngle", "CPlane", "CSphere", "CLineSeg", "CVector2i", "CFrustumPlanes"}
SMART = {"rstl::auto_ptr", "rstl::single_ptr", "rstl::optional_object", "rstl::rc_ptr", "rstl::ncrc_ptr", "TToken",
         "TLockedToken", "TCachedToken", "CToken", "TCachedToken", "TSubAnimTypeToken"}
# Plain integers that name a runtime resource: copying one restores nothing.
HANDLES = {"CSfxHandle"}
INTS = {"char", "signed char", "unsigned char", "short", "unsigned short", "int", "unsigned int",
        "long", "unsigned long", "long long", "unsigned long long", "wchar_t", "char16_t",
        "char32_t", "short int", "long int", "unsigned short int", "unsigned long int"}
FLOATS = {"float", "double"}


def run_clang(headers):
    """Yield the parsed top-level declarations of one TU that includes every header."""
    with tempfile.NamedTemporaryFile("w", suffix=".cpp", delete=False) as tu:
        for h in headers:
            tu.write(f'#include "{ROOT / h}"\n')
    try:
        p = subprocess.Popen([CLANG, *FLAGS, "-Xclang", "-ast-dump=json", tu.name], cwd=ROOT,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1 << 20)
        buf = None
        for line in p.stdout:
            if buf is None:
                if line == "    {\n":
                    buf = [line]
            else:
                buf.append(line)
                if line.startswith("    }"):
                    head = "".join(buf[:4])
                    text = "".join(buf)
                    buf = None
                    if re.search(r'"kind": "(CXXRecordDecl|NamespaceDecl|EnumDecl|ClassTemplateDecl|TypedefDecl|LinkageSpecDecl)"', head):
                        yield json.loads(text.rstrip().rstrip(","))
        err = p.stderr.read()
        if p.wait() != 0:
            sys.exit(f"gen_entity_schema: clang failed:\n{err[-4000:]}")
    finally:
        os.unlink(tu.name)


class Model:
    def __init__(self):
        self.records = {}  # qualified name -> record
        self.enums = set()  # qualified and simple names
        self.templates = []  # class templates (name, bases)
        self.namespaces = set()
        self.typedefs = {}  # global typedef name -> its type

    def walk(self, d, scope):
        kind = d.get("kind")
        if kind == "LinkageSpecDecl":
            for c in d.get("inner", []):
                self.walk(c, scope)
        elif kind == "NamespaceDecl":
            self.namespaces.add(d.get("name", ""))
            for c in d.get("inner", []):
                self.walk(c, scope + [d.get("name", "")])
        elif kind == "TypedefDecl" and d.get("name") and not scope:
            t = d["type"]
            self.typedefs[d["name"]] = t.get("desugaredQualType", t["qualType"])
        elif kind == "EnumDecl" and d.get("name"):
            self.enums.add("::".join(scope + [d["name"]]))
            self.enums.add(d["name"])
        elif kind == "ClassTemplateDecl":
            for c in d.get("inner", []):
                if c.get("kind") == "CXXRecordDecl" and c.get("completeDefinition"):
                    self.templates.append(("::".join(scope + [d.get("name", "")]),
                                           [b["type"]["qualType"] for b in c.get("bases", [])]))
        elif kind == "CXXRecordDecl" and d.get("completeDefinition") and d.get("name"):
            q = "::".join(scope + [d["name"]])
            self.records[q] = self.record(d, q)
            for c in d.get("inner", []):
                if c.get("kind") in ("CXXRecordDecl", "EnumDecl") and c.get("name") != d["name"]:
                    self.walk(c, scope + [d["name"]])

    def record(self, d, q):
        access = "private" if d.get("tagUsed") == "class" else "public"
        fields = []
        anon = None
        for c in d.get("inner", []):
            k = c.get("kind")
            if k == "AccessSpecDecl":
                access = c["access"]
            elif k == "CXXRecordDecl" and not c.get("name") and c.get("completeDefinition"):
                anon = c  # an anonymous struct/union: its fields are the class's own
            elif k == "FieldDecl":
                t = c["type"]
                if not c.get("name") and anon is not None:
                    for f in self.record(anon, q)["fields"]:
                        fields.append(dict(f, access=access, union=anon.get("tagUsed") == "union"))
                    anon = None
                    continue
                if not c.get("name"):
                    continue  # unnamed padding bitfield: nothing to save or address
                bits = 0
                if c.get("isBitfield"):
                    for e in c.get("inner", []):
                        if "value" in e:
                            bits = int(e["value"])
                fields.append({"name": c.get("name", ""), "type": t["qualType"],
                               "canon": t.get("desugaredQualType", t["qualType"]),
                               "bits": bits, "bitfield": bool(c.get("isBitfield")), "access": access,
                               "mutable": bool(c.get("mutable")), "union": False})
        dd = d.get("definitionData", {})
        return {"name": q, "tag": d.get("tagUsed"), "bases": [b["type"]["qualType"] for b in d.get("bases", [])],
                "virtual_bases": any(b.get("isVirtual") for b in d.get("bases", [])),
                "fields": fields, "trivial": bool(dd.get("isTriviallyCopyable")),
                "polymorphic": bool(dd.get("isPolymorphic"))}

    def resolve(self, t):
        """A global typedef's underlying type (clang leaves typedef'd array elements sugared)."""
        t = strip(t)
        for _ in range(8):
            if t not in self.typedefs:
                break
            t = strip(self.typedefs[t])
        return t

    def lookup(self, name):
        name = strip(name)
        return self.records.get(name) or self.records.get(name.split("<")[0])

    def is_entity(self, name, seen=()):
        if name == "CEntity":
            return True
        r = self.lookup(name)
        return r is not None and name not in seen and any(self.is_entity(strip(b), seen + (name,)) for b in r["bases"])

    def flat_scalar(self, name, want, depth=0):
        """True if record `name` holds only fields of the scalar set `want` (recursively)."""
        r = self.lookup(name)
        if r is None or r["polymorphic"] or depth > 8:
            return False
        for b in r["bases"]:
            if not self.flat_scalar(b, want, depth + 1):
                return False
        for f in r["fields"]:
            c = strip(f["canon"])
            m = re.fullmatch(r"(.*?)\s*((\[\d+\])+)", c)
            if m:
                c = strip(m.group(1))
            c = self.resolve(c)
            if c in want or (want is INTS and c in self.enums):
                continue
            if not self.flat_scalar(c, want, depth + 1):
                return False
        return True

    def pod(self, name, depth=0):
        """No vtable and nothing but plain data inside, so copying its bytes copies its value
        (some have a user copy constructor that copies the same scalars)."""
        r = self.lookup(name)
        if r is None or r["polymorphic"] or strip(name) in HANDLES or depth > 8:
            return False
        for b in r["bases"]:
            if not self.pod(b, depth + 1):
                return False
        for f in r["fields"]:
            if self.kind(f, depth + 1) not in PLAIN | {"pod"}:
                return False
        return True

    def uid_paths(self, name, expr, owners, depth=0):
        """Named references inside a POD, including arrays; never infer IDs from byte patterns."""
        name = self.resolve(name)
        if name == "TUniqueId":
            return [expr]
        if depth > 8:
            return []
        a = re.fullmatch(r"(.*?)\[(\d+)\](.*)", name)
        if a:
            return [p for i in range(int(a[2]))
                    for p in self.uid_paths(a[1] + a[3], f"{expr}[{i}]", owners, depth + 1)]
        r = self.lookup(name)
        if r is None:
            return []
        paths = []
        for b in r["bases"]:
            paths += self.uid_paths(b, f"static_cast< {b}& >({expr})", owners, depth + 1)
        for f in r["fields"]:
            paths += self.uid_paths(f["canon"], f"{expr}.{f['name']}", owners, depth + 1)
        if paths:
            owners.add(r["name"])
        return paths

    def kind(self, f, depth=0):
        if f["bitfield"]:
            return "bitfield"
        t = strip(f["type"])
        c = self.resolve(f["canon"])
        if c.endswith("*") or "(*)" in c or c.endswith("&"):
            return "pointer"
        m = re.fullmatch(r"(.*?)\s*((\[\d+\])+)", c)
        if m:  # a fixed array: raw bytes when its element is plain data
            el = dict(f, type=m.group(1), canon=m.group(1))
            return "pod" if self.kind(el, depth + 1) in PLAIN | {"pod"} else "other"
        if c == "bool":
            return "bool"
        if c in INTS or c in self.enums or t in self.enums or c.startswith("enum ") or t.split("::")[-1] in self.enums:
            return "int"
        if c in FLOATS:
            return "float"
        tmpl = c.split("<")[0]
        arg = c[len(tmpl) + 1:-1] if "<" in c else ""
        if tmpl in SMART:
            a = strip(arg.split(",")[0])
            if a.startswith("TStateMachineState") or a == "StateMachine":
                return "state_machine"
            if a == "CBodyController":
                return "body_controller"
            if a in ("CAnimData", "CModelData"):
                return "anim_data"
            return "smart_ptr"
        if tmpl.startswith("rstl::"):
            return "container"
        if c == "TUniqueId":
            return "uid"
        if re.fullmatch(r"TStateMachineState(Base)?<.*>", c) or c.startswith("CGenericFSM2State"):
            return "state_machine"
        if c == "CPathFindSearch":
            return "path_search"
        if c == "CBodyController":
            return "body_controller"
        if c in ("CAnimData", "CModelData"):
            return "anim_data"
        if c in MATH and self.flat_scalar(c, FLOATS):
            return "math"
        if self.pod(c, depth):
            return "pod"
        return "other"


def strip(t):
    t = t.strip()
    for p in ("const ", "volatile ", "class ", "struct ", "enum ", "union "):
        while t.startswith(p):
            t = t[len(p):].strip()
    return t.removesuffix(" const").strip()


def fnv1a64(text):
    h = 0xcbf29ce484222325
    for b in text.encode():
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def fingerprint(base, fields):
    """Must match PortEntitySchema::Fingerprint (platform/port_entity_schema.cpp)."""
    text = base + "\n" + "".join(f"{f['name']}:{f['kind']}:{f['canon']}:{f['bits']}\n" for f in fields)
    return fnv1a64(text)


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def find_headers():
    """Headers that may define a class with a base, under include/ and src/."""
    out = []
    for top in ("include", "src"):
        for p in sorted((ROOT / top).rglob("*")):
            if p.suffix == ".hpp" and p.is_file():
                body = p.read_text(errors="replace")
                if '"zlib/zlib.h"' in body:
                    continue  # the zlib port header is not in the tree; no entity lives there
                if p.name == "static_assert.hpp":
                    continue  # unguarded (static_assert.hpp): the TU already includes it
                if re.search(r"(class|struct)\s+\w+[^;{(]*:\s*(public|protected|private)?\s*\w",
                             p.read_text(errors="replace")):
                    out.append(str(p.relative_to(ROOT)))
    return out


def class_def_re(name):
    """The definition of class `name` (qualified): at column 0, or indented when nested."""
    indent = r"[ \t]+" if "::" in name else ""
    return re.compile(r"^" + indent + r"(class|struct)\s+(?:[A-Z][A-Z0-9_]*\s+)*" + re.escape(name.split("::")[-1]) +
                      r"\b(\s+final)?\s*(:[^;{]*)?\{", re.M)


def locate(name, headers, texts):
    outer = name.rsplit("::", 1)[0] if "::" in name else None
    for h in headers:
        if outer is not None and not class_def_re(outer).search(texts[h]):
            continue
        m = class_def_re(name).search(texts[h])
        if m:
            return h, m
    return None, None


def build(args):
    if not shutil.which(CLANG):
        print(f"gen_entity_schema: {CLANG} not found", file=sys.stderr)
        return None
    headers = find_headers()
    model = Model()
    for d in run_clang(headers):
        model.walk(d, [])
    texts = {h: (ROOT / h).read_text(errors="replace") for h in headers}
    classes = []
    friends_only = []  # outer classes of nested entities: they need the friend line, no table entry
    for q, r in sorted(model.records.items()):
        if q == "CEntity" or not model.is_entity(q):
            continue
        if any(part in model.namespaces for part in q.split("::")[:-1]):
            sys.exit(f"gen_entity_schema: {q} is in a namespace; its friend line would need ::PortEntitySchema")
        if r["virtual_bases"]:
            sys.exit(f"gen_entity_schema: {q} has a virtual base; static_cast from CEntity won't compile")
        entity_bases = [model.lookup(b)["name"] for b in r["bases"] if model.is_entity(strip(b))]
        if len(entity_bases) != 1:
            sys.exit(f"gen_entity_schema: {q} reaches CEntity through {entity_bases}")
        header, _ = locate(q, headers, texts)
        if header is None:
            sys.exit(f"gen_entity_schema: no header defines {q}")
        fields = [dict(f) for f in r["fields"]]
        mixins = [strip(b) for b in r["bases"] if not model.is_entity(strip(b))]
        for f in fields:
            f["kind"] = model.kind(f)
        classes.append({"name": q, "base": entity_bases[0], "header": header, "fields": fields,
                        "mixins": mixins, "private": any(f["access"] != "public" for f in fields)})
    # A nested entity is named from the generated code through its outer class.
    for c in list(classes):
        outer = c["name"].rsplit("::", 1)[0] if "::" in c["name"] else None
        if outer and outer not in {x["name"] for x in classes} and outer not in ([o["name"] for o in friends_only]):
            friends_only.append({"name": outer, "header": locate(outer, headers, texts)[0]})
    # CEntity itself is the root.
    root = model.records["CEntity"]
    cent_header = "include/MetroidPrime/CEntity.hpp"
    fields = [dict(f) for f in root["fields"]]
    for f in fields:
        f["kind"] = model.kind(f)
    classes.insert(0, {"name": "CEntity", "base": "", "header": cent_header, "fields": fields, "mixins": [],
                       "private": any(f["access"] != "public" for f in fields)})
    for c in classes:
        c["fingerprint"] = fingerprint(c["base"], c["fields"])
    # A POD can be pointer-free but still contain runtime object references. Expose them
    # through compiler-checked member access, just like top-level fields.
    uid_owners = set()
    for c in classes:
        for f in c["fields"]:
            f["uids"] = (model.uid_paths(f["canon"], f"static_cast< {c['name']}& >(ent).{f['name']}", uid_owners)
                         if f["kind"] == "pod" else [])
    all_headers = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "include").rglob("*.hpp"))
    all_texts = {h: (ROOT / h).read_text(errors="replace") for h in all_headers}
    for name in sorted(uid_owners):
        if name not in {c["name"] for c in classes + friends_only}:
            header, _ = locate(name, all_headers, all_texts)
            if header is None:
                sys.exit(f"gen_entity_schema: no header defines POD {name}")
            friends_only.append({"name": name, "header": header})
    # Entities only a .cpp defines are out of reach of the generated table.
    cpp_only = []
    names = {c["name"] for c in classes}
    for p in sorted((ROOT / "src").rglob("*.cpp")):
        for m in re.finditer(r"^(class|struct)\s+(\w+)\s*(final)?\s*:\s*public\s+(\w+)", p.read_text(errors="replace"), re.M):
            if m.group(4) in names and m.group(2) not in names:
                cpp_only.append((m.group(2), str(p.relative_to(ROOT))))
    templates = [(n, b) for n, b in model.templates if any(model.is_entity(strip(x.split("<")[0])) for x in b)]
    return {"classes": classes, "friends_only": friends_only, "headers": headers, "texts": texts, "cpp_only": cpp_only, "templates": templates}


def has_friend(text, m):
    """The friend line goes on the line after the class's opening brace (--add-friends)."""
    eol = text.index("\n", m.end() - 1) + 1
    return FRIEND in text[eol:text.find("\n", text.find("\n", eol) + 1)]


def need_friend(res):
    """(class name, header) of every class the generated code must be a friend of."""
    out = [(c["name"], c["header"]) for c in res["classes"] if c["private"]]
    out += [(c["name"], c["header"]) for c in res["friends_only"]]
    for c in res["classes"]:
        if "::" in c["name"]:
            outer = c["name"].rsplit("::", 1)[0]
            hdr = next((x["header"] for x in res["classes"] + res["friends_only"] if x["name"] == outer), None)
            out.append((outer, hdr))
    return sorted(set(out))


def add_friends(res):
    changed = []
    for name, h in need_friend(res):
        text = (ROOT / h).read_text()
        m = class_def_re(name).search(text)
        if has_friend(text, m):
            continue
        eol = text.index("\n", m.end() - 1) + 1
        text = text[:eol] + FRIEND_BLOCK.format(ind="  ", friend=FRIEND) + text[eol:]
        (ROOT / h).write_text(text)
        changed.append(h)
    return changed


def missing_friends(res):
    out = []
    for name, h in need_friend(res):
        text = (ROOT / h).read_text()
        m = class_def_re(name).search(text)
        if not has_friend(text, m):
            out.append(name)
    return out


def emit_inc(res):
    classes = res["classes"]
    o = ["// Generated by tools/gen_entity_schema.py from the game headers. Do not edit.",
         "// Included by platform/port_entity_schema.cpp only.", ""]
    for h in sorted({c["header"] for c in classes}):
        o.append(f'#include "{h.removeprefix("include/")}"' if h.startswith("include/") else f'#include "../../{h}"')
    o.append("")
    nfields = 0
    for c in classes:
        n = c["name"]
        o.append(f"template <> const PortSchemaClass& PortEntitySchema::Describe< {n} >() {{")
        if c["fields"]:
            o.append("  static const PortSchemaField fields[] = {")
            for f in c["fields"]:
                k = f["kind"]
                flags = []
                if f["union"]:
                    flags.append("kPortSchemaUnion")
                if f["access"] != "public":
                    flags.append("kPortSchemaPrivate")
                const = f["type"].strip().startswith("const ") or f["canon"].strip().startswith("const ")
                if const:
                    flags.append("kPortSchemaConst")
                ref = f["canon"].strip().endswith("&")
                integral = k in ("bool", "int", "bitfield")
                size = "0" if f["bitfield"] else f"sizeof({n}::{f['name']})"
                addr = "nullptr" if f["bitfield"] or ref else f"PES_ADDR({n}, {f['name']})"
                geti = f"PES_GETI({n}, {f['name']})" if integral else "nullptr"
                seti = f"PES_SETI({n}, {f['name']})" if integral and not const else "nullptr"
                uids = ""
                if f.get("uids"):
                    cases = " ".join(f"case {i}: return &({p});" for i, p in enumerate(f["uids"]))
                    uids = f", [](CEntity& ent, unsigned i) -> TUniqueId* {{ switch (i) {{ {cases} default: return nullptr; }} }}"
                o.append(f"      {{{cstr(f['name'])}, {cstr(f['canon'])}, PortSchemaKind::{k + '_' if k in ('bool', 'int', 'float') else k}, {f['bits']}, "
                         f"{' | '.join(flags) or '0'}, {size}, {addr}, {geti}, {seti}{uids}}},")
                nfields += 1
            o.append("  };")
            o.append(f"  static const PortSchemaClass desc = {{{cstr(n)}, {cstr(c['base'])}, fields, "
                     f"{len(c['fields'])}, 0x{c['fingerprint']:016X}ull}};")
        else:
            o.append(f"  static const PortSchemaClass desc = {{{cstr(n)}, {cstr(c['base'])}, nullptr, 0, "
                     f"0x{c['fingerprint']:016X}ull}};")
        o.append("  return desc;")
        o.append("}")
    o.append("")
    o.append("const PortSchemaClass* const* PortEntitySchema::Classes(int& count) {")
    o.append("  static const PortSchemaClass* const table[] = {")
    for c in classes:
        o.append(f"      &Describe< {c['name']} >(),")
    o.append("  };")
    o.append("  count = static_cast< int >(sizeof(table) / sizeof(table[0]));")
    o.append("  return table;")
    o.append("}")
    o.append("")
    o.append(f"const int PortEntitySchema::kClassCount = {len(classes)};")
    o.append(f"const int PortEntitySchema::kFieldCount = {nfields};")
    return "\n".join(o) + "\n"


def emit_doc(res):
    classes = res["classes"]
    kinds = collections.Counter(f["kind"] for c in classes for f in c["fields"])
    unknown = collections.Counter(strip(f["canon"]) for c in classes for f in c["fields"] if f["kind"] == "other")
    pods = collections.Counter(strip(f["canon"]) for c in classes for f in c["fields"] if f["kind"] == "pod")
    smart = collections.Counter(strip(f["canon"]).split("<")[0] for c in classes for f in c["fields"]
                                if f["kind"] in ("smart_ptr", "container"))
    total = sum(kinds.values())
    o = ["# Save-state entity schema (generated)", "",
         "Written by `tools/gen_entity_schema.py`; do not edit. The table itself is",
         "`platform/generated/port_entity_schema.inc`, its types `platform/include/port_entity_schema.h`.", "",
         f"{len(classes)} classes (CEntity and every subclass a header defines), {total} own fields; "
         f"{sum(kinds[k] for k in PLAIN)} of them plain data (`statedump` writes those under `fields` when the object's `kTagFields` block saves them, "
         "else under `fields_other`).", "",
         "| kind | fields |", "|---|---|"]
    for k in KINDS:
        o.append(f"| {k} | {kinds.get(k, 0)} |")
    o += ["", "## `other`: types without a codec yet (by field count)", ""]
    for t, n in unknown.most_common():
        o.append(f"- {n} `{t}`")
    o += ["", "## `pod`: trivially copyable, pointer-free structs and arrays", "",
          "Scalar bytes are copied; embedded `TUniqueId` members have generated accessors and are",
          "saved by stable identity, never restored as raw runtime IDs. This includes line-of-sight",
          "and visual trackers, waypoint/path navigation, health damage owners, sac/zone targets,",
          "camera interpolation, melee contacts, script messages and fixed ID arrays.", "",
          "Field policy 2 stores self/player/editor identities after the POD bytes (whose IDs are",
          "scrubbed). Unnamed dynamic references retain only a valid rebuilt reference. Policy 1",
          "states remain readable, but their embedded raw IDs are replaced with rebuilt references.", ""]
    o.append(", ".join(f"`{t}` {n}" for t, n in pods.most_common()))
    o += ["", "## `smart_ptr` / `container` templates", ""]
    o.append(", ".join(f"`{t}` {n}" for t, n in smart.most_common()))
    if res["cpp_only"] or res["templates"]:
        o += ["", "## Not in the table", ""]
        for n, p in res["cpp_only"]:
            o.append(f"- `{n}`: defined in `{p}` only")
        for n, b in res["templates"]:
            o.append(f"- `{n}`: a class template (bases {', '.join(b)})")
    return "\n".join(o) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="fail if the committed output is stale")
    ap.add_argument("--add-friends", action="store_true", help="add the friend line to classes that need it")
    a = ap.parse_args()
    res = build(a)
    if res is None:
        return 77 if a.check else 1
    if a.add_friends:
        for h in add_friends(res):
            print(f"friend added: {h}")
    missing = missing_friends(res)
    if missing:
        print(f"gen_entity_schema: no `{FRIEND}` in {', '.join(missing)} (run --add-friends)", file=sys.stderr)
        return 1
    outputs = {OUT_INC: emit_inc(res), OUT_DOC: emit_doc(res)}
    if a.check:
        stale = [str(p.relative_to(ROOT)) for p, t in outputs.items() if not p.exists() or p.read_text() != t]
        if stale:
            print(f"gen_entity_schema: stale {', '.join(stale)}; run tools/gen_entity_schema.py", file=sys.stderr)
            return 1
        print(f"gen_entity_schema: up to date ({len(res['classes'])} classes)")
        return 0
    for p, t in outputs.items():
        p.parent.mkdir(parents=True, exist_ok=True)
        if not p.exists() or p.read_text() != t:
            p.write_text(t)
            print(f"wrote {p.relative_to(ROOT)}")
    n = sum(len(c["fields"]) for c in res["classes"])
    print(f"{len(res['classes'])} classes, {n} fields")
    return 0


if __name__ == "__main__":
    sys.exit(main())
