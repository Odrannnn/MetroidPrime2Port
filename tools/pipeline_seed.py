#!/usr/bin/env python3
"""The pipeline cache seed: assets/initial_pipeline_cache.db, embedded in desktop builds
(CMakeLists.txt, MP_EMBED_RESOURCES). Aurora merges it into an empty or stale cache at start-up
("Seeded pipeline cache from ..."), so a first start compiles the game's pipelines (GX, clear, RmlUi)
in the background instead of skipping draws as it meets them. Ported from the Prime 1 port's script.

    record [--out DIR]    play the campaign from empty caches (MP2_PIPELINE_SEED=0): the front end, an
                          item/visor/beam/ball/menu pass, then every area of the five campaign worlds
                          (tools/sweep_areas.sh WORLD=<w>), Temple Grounds first. Needs a built
                          build-port-link/metroid_prime2_port, the disc, a GPU (Xvfb is fine; headless
                          draws nothing and records nothing). Retail content only: no texture mods, no
                          settings file, none of Remastered's options.
    merge CACHE... [-o F] union the caches' rows (earliest wins; the argument order, then the first
                          use inside a cache, is the load order) and write the seed; `record` prints
                          the exact call.
    check [SEED]          schema, current config versions (read from extern/aurora), retail rows only.
                          The port_pipeline_seed ctest runs it.

Aurora ignores every row of a type whose config version it has bumped (lib/gfx/clear.hpp,
lib/gx/pipeline.hpp, lib/rmlui/pipeline.hpp); `check` then fails until the seed is recorded again
(docs/RELEASING.md). The rows' blobs hold the draw state of one build, so record with one binary.

`pipeline_use` (context, hash) rows are kept too: Aurora's lazy mode prefetches an area's rows when
it streams in. A context is (MLVL id << 32) | area slot, stable for one disc version, so a PAL or
Trilogy disc simply never matches them. Rows Remastered's content or mods produce (PBR, sun shadow,
depth prepass, volumetric fog, distance-field fonts) are rejected by `check` and dropped by `merge`.
"""

import argparse
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SEED = ROOT / "assets" / "initial_pipeline_cache.db"

TYPE_CLEAR, TYPE_GX, TYPE_RML = 0, 1, 2
TYPE_NAMES = {TYPE_CLEAR: "clear", TYPE_GX: "gx", TYPE_RML: "rml"}
VERSION_SOURCES = {  # type: (header, constant)
    TYPE_CLEAR: ("extern/aurora/lib/gfx/clear.hpp", "ClearPipelineConfigVersion"),
    TYPE_GX: ("extern/aurora/lib/gx/pipeline.hpp", "GXPipelineConfigVersion"),
    TYPE_RML: ("extern/aurora/lib/rmlui/pipeline.hpp", "RmlPipelineConfigVersion"),
}
GX_VA_NULL = 0xFF
# A first playthrough meets Temple Grounds, Agon, Torvus, Sanctuary, then the Great Temple.
WORLDS = [("3bfa3eff", "Temple Grounds"), ("42b935e4", "Agon Wastes"), ("3dfd2249", "Torvus Bog"),
          ("1baa96c2", "Sanctuary Fortress"), ("863fcd72", "Great Temple")]

SCHEMA = """
CREATE TABLE aurora_schema(value INTEGER);
CREATE TABLE pipeline_cache (
  type INTEGER NOT NULL,
  hash INTEGER NOT NULL,
  config_version INTEGER NOT NULL,
  config_size INTEGER NOT NULL,
  config BLOB NOT NULL,
  first_frame_used INTEGER NOT NULL,
  PRIMARY KEY (type, hash)
);
CREATE INDEX pipeline_cache_load_order_idx
  ON pipeline_cache(type, config_version, first_frame_used);
CREATE TABLE pipeline_use (
  context INTEGER NOT NULL,
  hash INTEGER NOT NULL,
  PRIMARY KEY (context, hash)
) WITHOUT ROWID;
CREATE INDEX pipeline_cache_hash_idx ON pipeline_cache(hash);
"""


def current_versions():
    out = {}
    for t, (header, name) in VERSION_SOURCES.items():
        m = re.search(rf"constexpr\s+uint32_t\s+{name}\s*=\s*(\d+)\s*;", (ROOT / header).read_text())
        if not m:
            sys.exit(f"pipeline_seed: {name} not found in {header}")
        out[t] = int(m.group(1))
    return out


def remastered_reason(config):
    """Why a GX row can't come from retail content, or None. The blob is gx::PipelineConfig:
    u32 version, u32 msaaSamples, then gx::ShaderConfig (extern/aurora/lib/gx/gx.hpp): fogType,
    vtxStride, a bitfield byte, then pbr, sdf, depthOnly, pbrKind, volFog, pbrLightmapAttr,
    hudSample, perPixelLight, actorDecal."""
    sc = config[8:]
    if len(sc) < 12:
        return "truncated"
    if (sc[2] >> 4) & 1:
        return "shadow"
    if sc[3]:
        return "pbr"
    if sc[4]:
        return "sdf"
    if sc[5]:
        return "depthOnly"
    if sc[6]:
        return "pbrKind"
    if sc[7]:
        return "volFog"
    if sc[8] != GX_VA_NULL:
        return "pbrLightmapAttr"
    if sc[10]:
        return "perPixelLight"
    if sc[11]:
        return "actorDecal"
    return None


def open_ro(path):
    return sqlite3.connect(f"file:{path}?mode=ro", uri=True)


def read_rows(path):
    db = open_ro(path)
    try:
        return db.execute("SELECT type, hash, config_version, config_size, config, first_frame_used "
                          "FROM pipeline_cache").fetchall()
    finally:
        db.close()


def read_uses(path):
    db = open_ro(path)
    try:
        return db.execute("SELECT context, hash FROM pipeline_use").fetchall()
    except sqlite3.OperationalError:
        return []
    finally:
        db.close()


def problems(rows, versions):
    """(row index, reason) for every row the seed must not hold; index -1 is about the whole set."""
    out = []
    sizes = {}
    for i, (t, _h, ver, size, config, _f) in enumerate(rows):
        if t not in versions:
            out.append((i, f"unknown type {t}"))
        elif ver != versions[t]:
            out.append((i, f"{TYPE_NAMES[t]} version {ver}, current {versions[t]}"))
        elif size != len(config):
            out.append((i, f"size {size} != blob {len(config)}"))
        elif t == TYPE_GX and int.from_bytes(config[0:4], "little") != ver:
            out.append((i, "blob version differs from its row"))
        elif t == TYPE_GX and remastered_reason(config):
            out.append((i, f"Remastered-only ({remastered_reason(config)})"))
        else:
            sizes.setdefault(t, set()).add(size)
    for t, s in sizes.items():
        if len(s) > 1:
            out.append((-1, f"{TYPE_NAMES[t]} rows of several sizes {sorted(s)} (recorded by different builds)"))
    return out


def cmd_merge(a):
    versions = current_versions()
    kept, dropped = {}, {}
    uses = set()
    schema = None
    for n, src in enumerate(a.inputs):
        rows = read_rows(src)
        probs = problems(rows, versions)
        if any(i < 0 for i, _ in probs):
            sys.exit(f"pipeline_seed: {src}: rows of several sizes; record with one build")
        bad = dict(probs)
        for i, row in enumerate(rows):
            if i in bad:
                key = bad[i].split(" version")[0]
                dropped[key] = dropped.get(key, 0) + 1
                continue
            t, h, ver, size, config, first = row
            row = (t, h, ver, size, config, (n, first))  # first_frame_used restarts with every run
            old = kept.get((t, h))
            if old is None or row[5] < old[5]:
                kept[(t, h)] = row
        uses.update(read_uses(src))
        if schema is None:
            db = open_ro(src)
            schema = db.execute("SELECT value FROM aurora_schema").fetchall()
            db.close()
    out = Path(a.output)
    tmp = out.with_suffix(".tmp")
    tmp.unlink(missing_ok=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(tmp)
    db.executescript(SCHEMA)
    db.executemany("INSERT INTO aurora_schema VALUES (?)", schema)
    # first_frame_used renumbered 0..n-1 in the order the recording met the rows: the load order.
    ordered = sorted(kept.values(), key=lambda r: (r[5], r[0], r[1]))
    db.executemany("INSERT INTO pipeline_cache VALUES (?, ?, ?, ?, ?, ?)",
                   [(t, h, v, s, c, n) for n, (t, h, v, s, c, _f) in enumerate(ordered)])
    hashes = {r[1] for r in ordered}
    keep_uses = sorted(u for u in uses if u[1] in hashes)
    db.executemany("INSERT INTO pipeline_use VALUES (?, ?)", keep_uses)
    db.commit()
    db.execute("VACUUM")
    db.close()
    os.replace(tmp, out)
    counts = {}
    for r in ordered:
        counts[TYPE_NAMES[r[0]]] = counts.get(TYPE_NAMES[r[0]], 0) + 1
    contexts = len({u[0] for u in keep_uses})
    print(f"{out}: {len(ordered)} rows {counts}, {len(keep_uses)} uses in {contexts} contexts, "
          f"{out.stat().st_size / 1048576:.2f} MiB; dropped {dropped or 'none'}")


def cmd_check(a):
    versions = current_versions()
    seed = Path(a.seed)
    if not seed.exists():
        sys.exit(f"{seed}: missing; record it (python3 tools/pipeline_seed.py --help)")
    db = open_ro(seed)
    try:
        tables = {r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type = 'table'")}
        if not {"aurora_schema", "pipeline_cache"} <= tables:
            sys.exit(f"{seed}: not an Aurora pipeline cache (tables {sorted(tables)})")
        if not db.execute("SELECT value FROM aurora_schema").fetchall():
            sys.exit(f"{seed}: empty aurora_schema")
    finally:
        db.close()
    rows = read_rows(seed)
    bad = problems(rows, versions)
    for i, reason in bad[:20]:
        print(f"{seed}: row {i}: {reason}" if i >= 0 else f"{seed}: {reason}")
    if bad:
        print(f"{len(bad)} bad rows; re-record the seed (python3 tools/pipeline_seed.py --help)")
        sys.exit(1)
    types = {r[0] for r in rows}
    for t in (TYPE_GX, TYPE_CLEAR):
        if t not in types:
            sys.exit(f"{seed}: no {TYPE_NAMES[t]} rows")
    size = seed.stat().st_size
    if size > 15 * 1048576:
        sys.exit(f"{seed}: {size / 1048576:.1f} MiB, over the 15 MiB the executable should carry")
    print(f"{seed}: {len(rows)} rows, {len(read_uses(seed))} uses, {size / 1048576:.2f} MiB, "
          f"all current and retail")


# --- record -----------------------------------------------------------------

def run(cmd, env, **kw):
    return subprocess.run(cmd, cwd=ROOT, env=env, **kw)


def cache_db(tag, shard):
    return Path(f"/tmp/opencode/c_{tag}_{shard}/pipeline_cache.db")


def cmd_record(a):
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    user = out / "empty"  # no memory card: MP2_NEW_GAME starts a fresh file
    user.mkdir(exist_ok=True)
    env = dict(os.environ, USERDIR=str(user), MP2_NEW_GAME="1", MP2_PIPELINE_SEED="0",
               RENDER_EVERY="1", PRESENT_EVERY=str(a.present_every), LOGDIR=str(out),
               SHARDS=str(a.shards), SHARD="all")
    for k in ("MP2_FRAME_LIMIT", "MP2_TICKS_PER_FRAME", "HEADLESS", "WORLD", "AREAS", "CACHEDIR"):
        env.pop(k, None)
    if a.fast:
        env.update(MP2_FRAME_LIMIT="0", MP2_TICKS_PER_FRAME="8")
    for k in ("MP2_TEXTURE_PACK", "MP2_MODS", "MP2_REMASTERED"):  # no mods, no Remastered options
        env.pop(k, None)
    caches = []
    t0 = time.monotonic()
    # Scripted passes through the console driver: the front end (logo, menu, file select, intro) and
    # an in-game pass (every beam x visor, charge, missile, morph ball, pause screens).
    for i, (name, con, front, secs) in enumerate([("frontend", "pipeline_seed_frontend.con", "0", "300"),
                                                  ("actions", "pipeline_seed.con", "1", "400")]):
        if a.world and not a.scripted:
            break
        cache = out / f"cache_{name}"
        shutil.rmtree(cache, ignore_errors=True)
        port = str(7790 + i)
        genv = dict(env, SKIP_FRONTEND=front, FRAMES="100000", SECS=secs, DISPLAY_NUM=f":{150 + i}",
                    CACHEDIR=str(cache), LOG=str(out / f"{name}.log"), MP2_CONSOLE=port, MP2_CONSOLE_WAIT="1")
        print(f"[record] {name} ...", flush=True)
        t1 = time.monotonic()
        game = subprocess.Popen(["bash", "tools/run_gameplay.sh"], env=genv)
        time.sleep(4)
        subprocess.run([sys.executable, "tools/drivers/console.py", port, f"tools/drivers/{con}",
                        str(out / f"{name}.con.out")], stdout=subprocess.DEVNULL)
        game.wait()
        print(f"[record] {name}: {time.monotonic() - t1:.0f} s", flush=True)
        caches.append(cache / "pipeline_cache.db")
    wanted = a.world or [w for w, _ in WORLDS]
    for w, name in WORLDS:
        if w not in wanted:
            continue
        tag = f"{a.tag}_{w}"
        for i in range(a.shards):
            cache_db(tag, i).parent.mkdir(parents=True, exist_ok=True)
            cache_db(tag, i).unlink(missing_ok=True)
        print(f"[record] {name} ({w}) ...", flush=True)
        t1 = time.monotonic()
        env["WORLD"] = w
        env["TAG"] = tag
        run(["bash", "tools/sweep_areas.sh"], env)
        print(f"[record] {name}: {time.monotonic() - t1:.0f} s", flush=True)
        caches += [cache_db(tag, i) for i in range(a.shards)]
    have = [str(c) for c in caches if c.exists()]
    print(f"[record] total {time.monotonic() - t0:.0f} s; merge with:\n"
          f"  python3 tools/pipeline_seed.py merge {' '.join(a.extra)} {' '.join(have)}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("record", help="sweep the campaign from empty caches (MP2_PIPELINE_SEED=0)")
    r.add_argument("--out", default="/tmp/opencode/pipeline-seed-rec")
    r.add_argument("--tag", default="pseed")
    r.add_argument("--shards", type=int, default=4)
    r.add_argument("--world", action="append", help="only this MLVL id (repeatable)")
    r.add_argument("--scripted", action="store_true", help="with --world, still run the front-end/in-game passes")
    r.add_argument("--fast", action="store_true", help="unpaced game (loses ~10%% of the rows)")
    r.add_argument("--present-every", type=int, default=6)
    r.add_argument("--extra", nargs="*", default=[], help="caches to put first in the merge line")
    m = sub.add_parser("merge", help="union caches into the seed, current retail rows only")
    m.add_argument("inputs", nargs="+")
    m.add_argument("-o", "--output", default=str(SEED))
    c = sub.add_parser("check", help="fail if the seed is malformed, stale or holds Remastered rows")
    c.add_argument("seed", nargs="?", default=str(SEED))
    a = p.parse_args()
    {"record": cmd_record, "merge": cmd_merge, "check": cmd_check}[a.cmd](a)


if __name__ == "__main__":
    main()
