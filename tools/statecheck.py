#!/usr/bin/env python3
"""statecheck: save-state round-trip check. Dumps the world, saves, loads, dumps again and diffs.

    statecheck.py --userdir <dir holding the card> --area 5 --area 12   # starts a game, one round trip per area
    statecheck.py --port 4780 --area 5                                  # a run already up (MP2_CONSOLE=4780)

The first --area is the run's MP2_TELEPORT (tools/run_gameplay.sh), the others `warp`. Without --area
the room the run is in is checked. --setup 'cmd' (repeatable) runs unheld in each room first (`wait N` = N game updates;
ids are uids or `e<editor id>`, a bare editor id is not a uid and the command fails). One --setup may chain
commands with `;` (`'send e0 A; send e1 B'`): they run in one tick and each is checked. The player is held
safe by default: each room runs `god 1` and `nodarkair 1` before --setup, so an enemy hit or a dark-aether
drain is not a strict player hp diff; --unsafe-player leaves the player as the room found it.

Sequence (all with the game held, so ticks are exact; `statedump` runs between ticks, also while held):
  1. setup commands run unheld, then `hold 1`;
  2. `statedump A`: the world exactly as the save will see it. PortSaveState::Tick runs at the start of
     CStateManager::Update, so the `state save` queued now is taken by the first stepped tick;
  3. `state save N`, `step 1`, `statedump A1` (the same world one tick later: the natural drift);
  4. `state load N hold` + `hold 0`: the game holds itself on the tick the log shows `[savestate] room
     state restored` (or `room: giving up`), and `statedump B` follows.
A field that differs between A and B is STRICT only if B also differs from A1: a value B shares with A1
is one that just moves each tick (a timer, a position), not a save-state gap. --post-wait N also dumps
C N ticks after B and prints C against A, uncounted (for a restore that hands over late).
A lazily rebuilt reference (the Digital Guardian's collision actors, built by its first Think) shows in B as
`cls:X -> none` and is back in C: use --post-wait ~30 to tell it from a lost one.

Objects are matched by (area MREA, editor id, class, generated); objects with no editor id (effects,
projectiles: not saved by design) match by class only and differ as warnings. Missing or extra objects
with an editor id are strict, except one the room restore drops on purpose: a dead patterned actor
(`alive` 0, or `hp` <= 0 in an older dump) is counted as expected, not strict. A generated copy of a
generator (`:gen`) is strict only for the classes the room record saves: CScriptSound and
CScriptEffect copies are left to their generators (platform/port_savestate_room.cpp SavesCopy), so a
copy count that changes across the round trip is expected there and not counted.

Exit 0 when nothing strict differs, 1 otherwise, 2 on a harness failure.
"""
import argparse
import collections
import json
import os
import re
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
RESTORE_RE = re.compile(r"\[savestate\] (room state restored|room: giving up)")

# STRICT: any difference (floats within their tolerance) is a save-state gap.
STRICT = ["active", "hp", "kbres", "door_open", "door_lock", "timer", "timing", "counter", "ai", "body", "loco",
          "plat_flags", "rot_playing", "rot_actors", "dock_refs", "dock_load", "music",
          "plat_time", "plat_delay", "rot_time", "cine", "ai_time"]
# LOOSE: only reported beyond a tolerance, as warnings.
LOOSE = ["pos", "vel", "mom", "fwd", "anim"]
# The mark printed per severity: expected (a dead actor the restore drops) is not a diff.
MARKS = {"STRICT": "!", "WARN": "~", "INFO": "."}
# Generated copies of these classes are re-made by their own generators, not restored from the room
# record (platform/port_savestate_room.cpp SavesCopy returns false for them), so a copy count that
# differs across the round trip is not a save-state gap. Every other class stays strict.
GEN_REGENERATED = {"CScriptSound", "CScriptEffect"}
PLAYER_STRICT = ["morph", "hp"]
PLAYER_LOOSE = ["pos", "fwd"]
TIME_FIELDS = ("timer", "ai_time", "plat_time", "plat_delay", "rot_time", "cine")


class Console:
    """The port's debug console (platform/port_console.cpp): one persistent connection."""

    def __init__(self, port, timeout):
        self.timeout = timeout
        deadline = time.time() + 600
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
                break
            except OSError:
                if time.time() > deadline:
                    raise RuntimeError(f"nothing listens on 127.0.0.1:{port} (MP2_CONSOLE={port}?)")
                time.sleep(1)
        self.buf = b""

    def cmd(self, command, check=True):
        """Run one command line; returns (ok, text). Raises on failure when `check`.

        `a; b; c` is one line the console runs in one tick but answers once per `;` segment
        (platform/port_console.cpp RunCommands), so it waits for that many `=> ok`/`=> err:` replies:
        ok is true when all were ok, text is every reply body in order."""
        self.sock.sendall((command.rstrip("\r\n") + "\n").encode())
        replies = command.rstrip("\r\n").count(";") + 1
        texts, ok_all = [], True
        while replies:
            lines = self.buf.split(b"\n")
            for i, line in enumerate(lines):
                if line.startswith((b"=> ok", b"=> err:")):
                    texts.append(b"\n".join(lines[:i]).decode(errors="replace"))
                    self.buf = b"\n".join(lines[i + 1:])
                    ok = line.startswith(b"=> ok")
                    if check and not ok:
                        raise RuntimeError(f"command '{command}' failed: {line.decode(errors='replace')}")
                    ok_all = ok_all and ok
                    replies -= 1
                    break
            else:
                try:
                    chunk = self.sock.recv(65536)
                except TimeoutError:
                    raise RuntimeError(f"no reply to '{command}' in {self.timeout}s")
                if not chunk:
                    raise RuntimeError("the game closed the connection (crashed? see its log)")
                self.buf += chunk
        return ok_all, "\n".join(t for t in texts if t)


def wait_settled(con, stable=3, limit=300.0):
    """After a `layer`/`send` setup command: poll the game (unheld, so it ticks) until its warp/layer
    request is served and the free object-id count stayed the same over `stable` polls a few frames
    apart. Replaces a fixed real-time sleep; a game without the `settled` verb is slept on as before."""
    ok, text = con.cmd("settled", check=False)
    if not ok:
        time.sleep(float(os.environ.get("LAYER_SLEEP", "25")))
        return
    deadline = time.time() + limit
    last, same, frame = None, 0, -1
    while time.time() < deadline:
        m = re.search(r"busy (\d) free_ids (\d+) frame (\d+)", text)
        if m and m.group(1) == "0":
            key = int(m.group(2))
            same = same + 1 if key == last and int(m.group(3)) > frame else 0
            last = key
        else:
            same, last = 0, None
        if same >= stable:
            return
        frame = int(m.group(3)) if m else 0
        time.sleep(0.5)
        ok, text = con.cmd("settled", check=False)
    raise RuntimeError(f"setup did not settle in {limit}s: {text.strip()}")


class Game:
    """A run started through tools/run_gameplay.sh, or one already up."""

    def __init__(self, a):
        self.a = a
        self.proc = None
        self.log = Path(a.log)

    def start(self, teleport):
        a = self.a
        env = dict(os.environ, USERDIR=a.userdir, LOG=str(self.log), RUNTAG="statecheck", SECS=str(a.secs),
                   FRAMES="1000000", DISPLAY_NUM=a.display, MP2_CONSOLE=str(a.port), MP2_CONSOLE_WAIT="1")
        if teleport:
            env["TELEPORT"] = teleport
        cards = [c for c in Path(a.userdir).glob("*/Card A") if c.is_file() or any(c.iterdir())]
        if not cards:
            env["MP2_NEW_GAME"] = "1"  # an empty card: a new game, writing nothing
        if a.headless:
            env["HEADLESS"] = "1"
        if a.build:
            env["BUILD"] = a.build
        self.proc = subprocess.Popen(["bash", str(HERE / "run_gameplay.sh")], env=env,
                                     start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def count(self, text):
        return self.log.read_text(errors="replace").count(text) if self.log.exists() else 0

    def wait_for(self, text, before, secs=300):
        """Block until the log has more than `before` lines with `text` (the boot or a warp finished)."""
        deadline = time.time() + secs
        while self.count(text) <= before:
            if time.time() > deadline:
                raise RuntimeError(f"'{text}' never appeared in the log {self.log}")
            if self.proc is not None and self.proc.poll() is not None and self.count(text) <= before:
                raise RuntimeError(f"the run ended before '{text}'; see {self.log}")
            time.sleep(0.5)

    def restores(self):
        return len(RESTORE_RE.findall(self.log.read_text(errors="replace"))) if self.log.exists() else 0

    def last_restore_line(self):
        lines = [l for l in self.log.read_text(errors="replace").splitlines() if RESTORE_RE.search(l)] \
            if self.log.exists() else []
        return lines[-1] if lines else ""

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
                self.proc.wait(timeout=30)
            except Exception:
                os.killpg(self.proc.pid, signal.SIGKILL)


def parse_dump(text):
    objs, player = [], None
    for l in text.splitlines():
        d = json.loads(l)
        if d["t"] == "player":
            player = d
        else:
            objs.append(d)
    return {"player": player, "objs": objs}


def read_dump(path, timeout=30.0, stable=0.5):
    """Read a statedump, waiting for the game to finish writing it.

    `statedump` returns once the dump is queued, not written, so the file can still be absent or
    half written: wait until it exists, its size has not changed for `stable` seconds and its last
    line is complete JSON (a trailing newline parses every line).
    """
    deadline = time.time() + timeout
    size, since = None, time.time()
    while True:
        now = time.time()
        try:
            cur = os.path.getsize(path)
        except OSError:
            cur = None
        if cur is None or cur == 0 or cur != size:
            size, since = cur, now
        elif now - since >= stable:
            text = Path(path).read_text(errors="replace")
            if text.endswith("\n"):
                try:
                    return parse_dump(text)
                except ValueError:
                    pass
        if now >= deadline:
            raise RuntimeError(f"{path} was not complete after {timeout:.0f}s")
        time.sleep(0.05)


def step_ticks(con, n, timeout=120):
    """`step n` and wait until the game has run them: `step` only queues the ticks, and a
    `statedump` sent right after it reads the world before they ran."""
    def frame():
        return int(con.cmd("frame")[1].split()[-1])
    goal = frame() + n
    while n > 0:
        con.cmd(f"step {min(600, n)}")
        n -= 600
    deadline = time.time() + timeout
    while frame() < goal:
        if time.time() > deadline:
            raise RuntimeError(f"step: frame {goal} not reached in {timeout}s")
        time.sleep(0.02)


def roundtrip(con, game, tag, outdir, a):
    """Run the A / A1 / B (and with --post-wait, C) sequence on the current world. Returns the dump paths."""
    paths = {k: str(Path(outdir).resolve() / f"{tag}-{k}.jsonl") for k in ("A", "A1", "B", "C")}
    con.cmd("hold 1")
    con.cmd(f"statedump {paths['A']}")
    con.cmd(f"state save {a.slot}")
    step_ticks(con, 1)
    con.cmd(f"statedump {paths['A1']}")
    n = game.restores()
    # `hold` stops the game on the tick the room state is restored: releasing the hold and re-taking it
    # on seeing the log line let the world free-run past the restore (timers read seconds too far)
    con.cmd(f"state load {a.slot} hold")
    con.cmd("hold 0")
    deadline = time.time() + 120
    while game.restores() <= n:
        if time.time() > deadline:
            return paths, "no room-state restore line within 120s of the load"
        time.sleep(0.05)
    con.cmd(f"statedump {paths['B']}")
    if a.post_wait:
        step_ticks(con, a.post_wait)
        con.cmd(f"statedump {paths['C']}")
    return paths, None


# --- diff --------------------------------------------------------------------------------

def is_dynamic(o):
    return o["eid"][-6:] == "FFFFFF" or o["eid"] == "FFFFFFFF"


def keyed(objs):
    """{key: [obj...]} with each list in uid order; the key is (mrea, eid, class, generated)."""
    out = collections.defaultdict(list)
    # copies of one template pair by their copy index when the dump has it (a uid is a reusable slot)
    for o in sorted(objs, key=lambda o: (o.get("copy", 0), o["uid"])):
        eid = "-" if is_dynamic(o) else o["eid"]
        out[(o["mrea"], eid, o["class"], bool(o.get("generated")))].append(o)
    return out


def vec_dist(p, q):
    return sum((x - y) ** 2 for x, y in zip(p, q)) ** 0.5


def is_dead_patterned(o):
    """A patterned actor the room restore drops on purpose (port_savestate_room.cpp IsGone).

    Dead enemies are gone for the room even while the corpse still runs, so the restored room has
    no such object: `alive` (new dumps) says it, `hp` <= 0 is the same thing on older ones.
    """
    if "alive" not in o and "ai" not in o:
        return False  # not a patterned record
    if "alive" in o:
        return not o["alive"]
    hp = o.get("hp")
    return hp is not None and hp <= 0.0


def fields_differ(field, x, y, a):
    """None when equal (within tolerance), else a short text. x or y may be None (absent)."""
    if x is None and y is None:
        return None
    if x is None or y is None:
        return "absent" if x is None else "gone"
    if field in ("pos", "vel", "mom"):
        tol = a.tol_pos if field == "pos" else a.tol_vel
        return f"{vec_dist(x, y):.2f}" if vec_dist(x, y) > tol else None
    if field == "fwd":
        dot = sum(p * q for p, q in zip(x, y))
        return f"dot {dot:.3f}" if dot < 0.98 else None
    if field in ("hp", "kbres"):
        return None if abs(x - y) <= a.tol_hp else f"{abs(x - y):.2f}"
    if field in TIME_FIELDS:
        return None if abs(x - y) <= a.tol_time else f"{abs(x - y):.2f}"
    return None if x == y else "differs"


def schema_differ(x, y, a):
    """None when a `fields` entry is equal (numbers within --tol-field, relative above 1)."""
    if x == y:
        return None
    if isinstance(x, list) and isinstance(y, list) and len(x) == len(y):
        if all(schema_differ(p, q, a) is None for p, q in zip(x, y)):
            return None
    elif isinstance(x, (int, float)) and isinstance(y, (int, float)) and not isinstance(x, bool):
        if abs(x - y) <= a.tol_field * max(1.0, abs(x), abs(y)):
            return None
    return "differs"


def show(v):
    if v is None:
        return "(none)"
    if isinstance(v, list):
        return "(" + ", ".join("none" if p is None else f"{p:.1f}" for p in v) + ")"
    if isinstance(v, float):
        return f"{v:.2f}"
    if isinstance(v, str) and len(v) > 40:
        return v[:40] + f"..({len(v)}c)"
    return str(v)


class Report:
    def __init__(self):
        self.rows = []  # (severity, class, label, kind, text)
        self.drift = collections.Counter()
        self.total = 0
        self.per_class_total = collections.Counter()
        self.fields = []  # saved schema fields that differ: (class, field, label, text); informational

    def add(self, sev, cls, label, kind, text):
        self.rows.append((sev, cls, label, kind, text))

    def strict(self):
        return [r for r in self.rows if r[0] == "STRICT"]


def compare(da, da1, db, a, with_drift=True):
    rep = Report()
    ka, ka1, kb = keyed(da["objs"]), keyed(da1["objs"]), keyed(db["objs"])
    rep.total = len(da["objs"])
    rep.per_class_total = collections.Counter(o["class"] for o in da["objs"])
    for key in sorted(set(ka) | set(kb)):
        mrea, eid, cls, gen = key
        la, lb = ka.get(key, []), kb.get(key, [])
        l1 = ka1.get(key, [])
        dyn = eid == "-"
        tag = "gen" if gen else ("dyn" if dyn else eid)
        if len(la) != len(lb):
            sev = "WARN" if dyn else "STRICT"
            kind = "missing" if len(lb) < len(la) else "extra"
            if kind == "missing" and la and all(is_dead_patterned(o) for o in la):
                sev, kind = "INFO", "dead"  # dropped by design: not a restore gap
            elif gen and cls in GEN_REGENERATED:
                sev = "INFO"  # re-made by its generator: not a restore gap
            elif not dyn and len(la) > 0 and len(l1) == len(lb):
                kind, sev = "gone-in-tick", "WARN"  # A1 (one tick later) already differed: not a restore gap
            rep.add(sev, cls, f"{mrea}:{tag}", kind, f"{len(la)} before, {len(lb)} after the round trip")
        if dyn:
            continue
        for i, (x, y) in enumerate(zip(la, lb)):
            z = l1[i] if i < len(l1) else None
            label = f"{eid}{'#%d' % i if gen or len(la) > 1 else ''} {x.get('name') or ''}".strip()
            for strict, fields in ((True, STRICT), (False, LOOSE)):
                for f in fields:
                    vx, vy = x.get(f), y.get(f)
                    d = fields_differ(f, vx, vy, a)
                    if d is None:
                        continue
                    if with_drift and z is not None and fields_differ(f, z.get(f), vy, a) is None:
                        rep.drift[(cls, f)] += 1  # B equals A1: the field just moved a tick
                        continue
                    rep.add("STRICT" if strict else "WARN", cls, label, f, f"{show(vx)} -> {show(vy)} ({d})")
            fx, fy, fz = x.get("fields") or {}, y.get("fields") or {}, (z or {}).get("fields") or {}
            for name in sorted(set(fx) | set(fy)):
                if schema_differ(fx.get(name), fy.get(name), a) is None:
                    continue
                if with_drift and z is not None and name in fz and schema_differ(fz[name], fy.get(name), a) is None:
                    continue  # B equals A1: the field just moved a tick
                rep.fields.append((cls, name, label, f"{show(fx.get(name))} -> {show(fy.get(name))}"))
    pa, pb, p1 = da["player"], db["player"], da1["player"]
    for strict, fields in ((True, PLAYER_STRICT), (False, PLAYER_LOOSE)):
        for f in fields:
            d = fields_differ(f, pa.get(f), pb.get(f), a)
            if d is None:
                continue
            if with_drift and fields_differ(f, p1.get(f), pb.get(f), a) is None:
                rep.drift[("player", f)] += 1
                continue
            rep.add("STRICT" if strict else "WARN", "player", "player", f,
                    f"{show(pa.get(f))} -> {show(pb.get(f))} ({d})")
    return rep


def print_report(title, rep, a):
    strict = rep.strict()
    warn = [r for r in rep.rows if r[0] == "WARN"]
    info = [r for r in rep.rows if r[0] == "INFO"]
    head = f"=== {title}: {rep.total} objects, {len(strict)} strict diff(s), {len(warn)} warning(s)"
    if info:
        head += f", {len(info)} expected (dead actors the restore drops, re-made generated copies)"
    print(head + " ===")
    if rep.drift:
        print("tick drift (B equals the dump one tick after the save; ignored): " +
              ", ".join(f"{c}.{f} x{n}" for (c, f), n in sorted(rep.drift.items())))
    classes = sorted({r[1] for r in rep.rows})
    if rep.fields:
        print(f"\nschema fields (saved by kTagFields; informational, not in the exit code): {len(rep.fields)} differ")
        groups = collections.defaultdict(list)
        for cls, name, label, text in rep.fields:
            groups[(cls, name)].append((label, text))
        for (cls, name), items in sorted(groups.items()):
            print(f"  {cls}.{name} x{len(items)}: {items[0][0]} {items[0][1]}")
    if not classes:
        print("no differences")
        return
    print("\nby class (objects in the room; ! strict / ~ warning / . expected, by field):")
    for cls in classes:
        kinds = collections.Counter((r[0], r[3]) for r in rep.rows if r[1] == cls)
        text = ", ".join(f"{MARKS.get(s, '~')}{k} x{n}" for (s, k), n in sorted(kinds.items()))
        print(f"  {cls} ({rep.per_class_total.get(cls, 1)}): {text}")
    print("\ndetails:")
    for cls in classes:
        rows = sorted((r for r in rep.rows if r[1] == cls), key=lambda r: (r[0] != "STRICT", r[3], r[2]))
        print(f"  {cls}")
        for sev, _, label, kind, text in (rows if a.all else rows[:a.detail]):
            print(f"    {MARKS.get(sev, '~')} {label}: {kind} {text}")
        if not a.all and len(rows) > a.detail:
            print(f"    ... {len(rows) - a.detail} more (--all)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=4781, help="MP2_CONSOLE port (default %(default)s); with no --userdir, "
                    "a run already listening there")
    ap.add_argument("--userdir", help="directory holding the memory card: start a run with tools/run_gameplay.sh")
    ap.add_argument("--area", action="append", default=[], help="area (slot, id or name) to check; repeatable")
    ap.add_argument("--setup", action="append", default=[], metavar="CMD",
                    help="console command run unheld in each room before the round trip (repeatable); "
                    "`wait N` lets the game run N more updates (the game must be running, not held)")
    ap.add_argument("--unsafe-player", action="store_true",
                    help="leave the player unsafe: skip the `god 1`/`nodarkair 1` each room runs first, so a "
                    "dark-aether drain or an enemy hit shows up as a strict player hp diff")
    ap.add_argument("--out", default="statecheck-out", help="directory for the JSONL dumps (default %(default)s)")
    ap.add_argument("--log", default="/tmp/opencode/statecheck.log", help="the run's stderr log (the restore is read from it)")
    ap.add_argument("--build", help="build dir for run_gameplay.sh (default build-port-link)")
    ap.add_argument("--display", default=":87", help="Xvfb display for the run (default %(default)s)")
    ap.add_argument("--headless", action="store_true", help="HEADLESS=1: no picture")
    ap.add_argument("--secs", type=int, default=900, help="backstop that ends a started run (default %(default)s)")
    ap.add_argument("--wait", type=int, default=240, metavar="TICKS",
                    help="unheld ticks to let a room settle before the round trip (default %(default)s)")
    ap.add_argument("--slot", type=int, default=8, help="scratch save slot (default 8)")
    ap.add_argument("--max-steps", type=int, default=600, help="ticks to wait for the restore (default 600)")
    ap.add_argument("--post-wait", type=int, default=0, metavar="N",
                    help="also dump C N ticks after B and report C against A (informational, not in the exit code)")
    ap.add_argument("--timeout", type=float, default=120.0, help="seconds per console command")
    ap.add_argument("--tol-pos", type=float, default=0.5)
    ap.add_argument("--tol-vel", type=float, default=0.5)
    ap.add_argument("--tol-hp", type=float, default=0.01)
    ap.add_argument("--tol-field", type=float, default=0.01, help="schema field difference (relative above 1)")
    ap.add_argument("--tol-time", type=float, default=0.1, help="timer-like difference in seconds")
    ap.add_argument("--detail", type=int, default=8, help="detail lines per class (default 8)")
    ap.add_argument("--all", action="store_true", help="print every detail line")
    a = ap.parse_args()
    Path(a.out).mkdir(parents=True, exist_ok=True)
    Path(a.log).parent.mkdir(parents=True, exist_ok=True)

    game = Game(a)
    failed, summary = False, []
    try:
        if a.userdir:
            game.start(a.area[0] if a.area else "")
        con = Console(a.port, a.timeout)
        if a.userdir:
            game.wait_for("[harness] gameplay reached", 0)
        for i, area in enumerate(a.area or [""]):
            title = f"area {area}" if area else "current room"
            con.cmd("hold 0")
            if area and (i or not a.userdir):
                n = game.count("[warp] arrival check")
                con.cmd(f"warp {area}")
                game.wait_for("[warp] arrival check", n)
            if not a.unsafe_player:
                # the player must not take damage between A and B: a dark-aether drain or an enemy hit
                # would show up as a strict player hp diff, not a save-state gap
                for c in ("god 1", "nodarkair 1"):
                    con.cmd(c)
            for c in a.setup:
                m = re.fullmatch(r"wait\s+(\d+)", c.strip())
                if m:
                    # `wait N`: let the game run N more updates (a boss fight takes seconds to get going)
                    _, text = con.cmd("frame")
                    goal = int(text.split()[-1]) + int(m.group(1))
                    while int(con.cmd("frame")[1].split()[-1]) < goal:
                        time.sleep(0.5)
                else:
                    con.cmd(c)
                    if any(seg.split()[:1] in (["layer"], ["send"]) for seg in c.split(";")):
                        wait_settled(con)
            if a.wait:
                # settle: the room's script start-up, spawns and the first AI states run unheld
                con.cmd("hold 1")
                step_ticks(con, a.wait)
            tag = f"{i + 1:02d}-" + re.sub(r"\W+", "_", title)[:32]
            paths, err = roundtrip(con, game, tag, a.out, a)
            if err:
                print(f"=== {title}: FAIL: {err}")
                failed = True
                continue
            da, da1, db = (read_dump(paths[k]) for k in ("A", "A1", "B"))
            rep = compare(da, da1, db, a)
            print_report(title, rep, a)
            if a.post_wait:
                rc = compare(da, da1, read_dump(paths["C"]), a, with_drift=False)
                print(f"\n--- C ({a.post_wait} ticks after B) against A, uncounted: {len(rc.rows)} diff(s)")
                kinds = collections.Counter((r[1], r[3]) for r in rc.rows)
                print("  " + (", ".join(f"{c}.{k} x{n}" for (c, k), n in sorted(kinds.items())) or "none"))
            print(f"{game.last_restore_line()}\ndumps: {paths['A']} {paths['A1']} {paths['B']}\n")
            failed = failed or bool(rep.strict())
            kinds = collections.Counter((r[1], r[3]) for r in rep.strict())
            summary.append((title, f"{rep.total} objects: " + (", ".join(
                f"{c}.{k}" + (f" x{n}" if n > 1 else "") for (c, k), n in sorted(kinds.items())) or "ok")))
        con.cmd("hold 0", check=False)
        if len(summary) > 1:
            print("=== summary (strict diffs per room) ===")
            for title, text in summary:
                print(f"  {title}: {text}")
    except RuntimeError as e:
        print(f"statecheck: {e}", file=sys.stderr)
        return 2
    finally:
        game.stop()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
