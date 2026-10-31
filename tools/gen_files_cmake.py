#!/usr/bin/env python3
"""Generate files.cmake (MP_GAME_SOURCES) from upstream's configure.py.

Every unit of every lib whose source exists under src/ is listed, except:
  * libs the native port replaces (Dolphin SDK, MusyX, MetroTRK, MSL/Runtime), and
  * paths in tools/files_cmake_excluded.txt ("path | reason", one per line).
Port-only sources (src/**/Port*, *PortStub.cpp, CARAMManagerPort.cpp, ...) listed in
PORT_ONLY below are appended.

    python3 tools/gen_files_cmake.py            # rewrite files.cmake
    python3 tools/gen_files_cmake.py --check    # exit 1 if files.cmake is stale
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CONFIGURE = ROOT / "configure.py"
OUT = ROOT / "files.cmake"
EXCLUDED = ROOT / "tools" / "files_cmake_excluded.txt"

# Libs replaced by Aurora / the host libc / separately built musyx-port.
SKIP_LIBS = {"musyx", "MetroTRK", "Runtime.PPCEABI.H"}
SKIP_LIB_PREFIXES = ("MSL_C",)

PORT_ONLY = [
    "src/MetroidPrime/HUD/PortRadialHud.cpp",
    "src/Kyoto/Alloc/PortMwccNew.cpp",
    "src/Kyoto/CARAMManagerPort.cpp",
    "src/Kyoto/CFactoryFunctionsPort.cpp",
    "src/Kyoto/Streams/CLZOSupportPort.cpp",
    "src/Kyoto/Graphics/PortDrawDebug.cpp",
    "src/Kyoto/PortCMemoryCardSys.cpp",
    "src/MetroidPrime/Player/PortSPersistentOptionsValue.cpp",
    "src/MetroidPrime/PortBoot.cpp",
    "src/MetroidPrime/PortCGMSinglePlayer.cpp",
    "src/MetroidPrime/PortCHintManager.cpp",
    "src/MetroidPrime/PortCMorphBallVtables.cpp",
    "src/MetroidPrime/PortCPersistentOptionsMap.cpp",
    "src/MetroidPrime/PortCPlayerDynamicsVtables.cpp",
    "src/MetroidPrime/PortCTweakPlayerControls.cpp",
    "src/MetroidPrime/PortCVisualKnowledge.cpp",
    "src/MetroidPrime/PortDataGlobals.cpp",
    "src/MetroidPrime/PortDefaultFont.cpp",
    "src/MetroidPrime/PortEfbCopy.cpp",
    "src/MetroidPrime/PortFrameTimeHistory.c",
    "src/MetroidPrime/PortGlobals.cpp",
    "src/MetroidPrime/PortLinkStubs.cpp",
    "src/MetroidPrime/PortMakeMsg.cpp",
    "src/MetroidPrime/PortNonElfStubs.cpp",
    "src/MetroidPrime/PortPoolStandIns.cpp",
    "src/MetroidPrime/PortScanTarget.cpp",
    "src/MetroidPrime/PortSkipFrontEnd.cpp",
    "src/MetroidPrime/PortStateMachineBase.cpp",
    "src/MetroidPrime/PortStreamNewGameState.cpp",
    "src/MetroidPrime/PortTenthSyncStubs.cpp",
    "src/MetroidPrime/PortTweakGlobals.cpp",
]
# src/MetroidPrime/PortReachStubs.cpp is diagnostic-only (MP_BOOT_STUBS), never listed.

# The first argument may be a call with its own commas: MatchingFor("G2ME01", "G2MP01").
OBJ_RE = re.compile(r'Object\(\s*(?:[^,()]|\([^)]*\))+,\s*"([^"]+)"([^)]*)\)')
SRC_RE = re.compile(r'source\s*=\s*"([^"]+)"')


REL_MARK_RE = re.compile(r'Rel\(')


def excluded() -> dict:
    out = {}
    if EXCLUDED.exists():
        for line in EXCLUDED.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                path, _, reason = line.partition("|")
                out[path.strip()] = reason.strip()
    return out


def units(with_modules: bool = False) -> list:
    text = CONFIGURE.read_text()
    text = text[text.index("config.libs = ["):text.index("def link_order_callback")]
    # Offsets of lib markers; DolphinLib/MusyX helpers are always skipped libs.
    marks = []
    for m in re.finditer(r'"lib":\s*"([^"]+)"|\b(DolphinLib)\(|\b(MusyX)\(|\bRel\(\s*"([^"]+)"', text):
        lib = m.group(1) or m.group(2) or m.group(3) or m.group(4)
        marks.append((m.start(), lib))
    rel_of = {l: l for p, l in marks if REL_MARK_RE.match(text, p)}
    res = []
    for m in OBJ_RE.finditer(text):
        lib = [l for p, l in marks if p < m.start()][-1]
        if lib in SKIP_LIBS or lib in ("DolphinLib", "MusyX") or lib.startswith(SKIP_LIB_PREFIXES):
            continue
        s = SRC_RE.search(m.group(2))
        path = "src/" + (s.group(1) if s else m.group(1))
        if (ROOT / path).exists():
            res.append((path, rel_of.get(lib)) if with_modules else path)
    return res


def render() -> str:
    ex = excluded()
    mods = {}
    flat = set()
    # Port-only modules: the host body for a REL module upstream has not decompiled (see
    # tools/modules_port.txt). Merged here so the module list, and the CMake loop over it, treat
    # them like any other module.
    extra = ROOT / "tools" / "modules_port.txt"
    if extra.exists():
        for line in extra.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            name, _, src = line.partition(";")
            src = src.strip()
            if name.strip() and src and (ROOT / src).exists():
                mods.setdefault(name.strip(), set()).add(src)
    for p, mod in units(True):
        if p in ex:
            continue
        if mod:
            mods.setdefault(mod, set()).add(p)
        else:
            flat.add(p)
    listed = sorted(flat)
    port = [p for p in PORT_ONLY if (ROOT / p).exists() and p not in ex]
    body = "".join(f"    {p}\n" for p in listed)
    pbody = "".join(f"    {p}\n" for p in port)
    # One list per REL module (configure.py's Rel(...) blocks, in order). CMakeLists.txt builds
    # each as its own symbol namespace; see tools/scope_module.py.
    names = list(mods)
    mbody = "".join(
        f"set(MP_MODULE_{n}_SOURCES\n" + "".join(f"    {p}\n" for p in sorted(mods[n])) + ")\n"
        for n in names)
    mbody += "set(MP_MODULES " + " ".join(names) + ")\n"
    return (
        "# GENERATED by tools/gen_files_cmake.py from configure.py - do not edit.\n"
        "# Exclusions (with reasons): tools/files_cmake_excluded.txt\n"
        f"set(MP_GAME_SOURCES\n{body})\n"
        f"# REL modules, each its own symbol namespace\n{mbody}\n"
        f"# Port-only sources\nlist(APPEND MP_GAME_SOURCES\n{pbody})\n"
    )


if __name__ == "__main__":
    new = render()
    if "--check" in sys.argv:
        sys.exit(0 if OUT.exists() and OUT.read_text() == new else 1)
    OUT.write_text(new)
    print(f"wrote {OUT.name}: {new.count(chr(10)+'    src/')} sources")
