#!/usr/bin/env python3
"""Digest a `tools/sweep_areas.sh` run into a per-area verdict table.

    python3 tools/sweep_report.py '/tmp/opencode/log_tg_*.log'
    python3 tools/sweep_report.py '/tmp/opencode/log_all_*_*.log'   # a WORLD=all sweep: every world

Logs of several sweeps (tags) can be read at once: a shard is named by its tag and index, and a
sweep run with `WORLD=` names its world and its slot list in the manifest, so areas are printed with
their MLVL names, and the `== worlds` table says per world (and per light/dark side) how many slots
were asked for, played, clean and failing.

The sweep's logs are the only record of what each area did, and a raw one is ~500 lines of
`[aram]`/`[ids]` noise per shard. This reads them and answers the two questions a sweep is for:

  * **coverage** - which area slots of the world were actually played, in what order, and which
    were missed (a `NEXTAREA` token that never fired, or a hop that never arrived);
  * **walls** - what stopped each area that did not simply play.

Everything is keyed on two line families the port already prints: `[area] N` marks each arrival
(`CStateManager::Update` prints it whenever the current area changes), and `[ids] frame N: ...` is
the 300-frame id-pool sample that says how much of the area was live. A "phase" is the block of log
between one `[area] N` and the next, so the id high-water, the missing resources and the warp route
are all attributed to the area they happened in. A hop's shape is read from its `[warp] area N:`
lines: "streaming (N of 1024 object ids free)" is the port's own direct route, "handing the hop to
the game's own transition" is the hand-over.

Verdicts, worst first:

    CRASH    the fault handler (`[port] caught`) fired in this phase, or the run's exit code is a
             signal (134 abort, 139 segfault).
    ABORT    an id-pool exhaustion or an assert stopped the run in this phase.
    DIED     the player was killed in this phase (`[player] killed ...`, printed by
             `CStateManager::ApplyLocalDamage`). The game then ends to the lose-game movie and its
             "continue" screen, which takes no `B`, so the harness's stall report that follows is
             only a symptom. A dark-world area (Dark Aether's atmosphere, weapon type 17) kills a
             suitless player that the sweep's `HEAL` cannot keep up with.
    CREDITS  the ending: `[end] the credits are rolling` is printed by the port when
             `CScriptSpecialFunction::AcceptCredits` puts `CCredits` on the window stack, and a
             played area walks into it (Temple Grounds: slot 53 starts the ending and hands over to 56,
             `game_end_part4`, the only area with the credits script). The credits scroll with
             no `CStateManager::Update`, so the harness's stall report that follows is the ending, not
             a wall. It ranks below DIED (a death is a real failure) and above RELOAD/STALL, which it
             is the explanation for; a real crash still wins over it (see `verdict`).
    STALL    no game update for the harness's 30 s window: the game stopped updating here.
    RELOAD   the update counter went backwards - the run quit to the front end and the port's
             front-end skip re-booted slot 0, so the sweep's `NEXTAREA` tokens then walked from the
             save's own area again. A sweep that presses `A` does this (the action button activates
             the save stations the arrival spawn points sit in); the reload is the finding, and the
             stall report next to it is only a symptom.
    STUB     a `[reach-stub ...]` (a body the port has not written was called) or a `... is a port
             stand-in` line was printed in this phase: the area played, but on code that is not the game's.
    POOL     the id pool came within 64 ids of full in this phase - legal in itself, but it is the
             headroom the next hop needs, which is what the port's pre-stream drop exists to keep.
    OK       the area loaded, played, and nothing above happened.

`--verbose` also prints each phase's notable lines (everything that is not a known-benign line),
which is how a wall's cause is read out without opening the log.
"""

from __future__ import annotations

import argparse
import glob
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

AREA_RE = re.compile(r"^\[area\] (\d+)$")
IDS_RE = re.compile(r"^\[ids\] frame (\d+): (\d+)/(\d+) ids allocated, (\d+) objects$")
WORLD_RE = re.compile(r"^\[warp\] world ([0-9a-f]{8}) areas:")
EXIT_RE = re.compile(r"^exit=(\d+) at ")
HOP_RE = re.compile(r"^\[warp\] area (\d+): (.*)$")
MOVED_RE = re.compile(r"^\[warp\] (?:moved to|the game's own transition arrived in) area (\d+)")
RES_RE = re.compile(r"^\[res\] (.*)$")
CREDITS_RE = re.compile(r"^\[end\] the credits are rolling$")
# The port serving a `MP2_WORLD` request: `[warp] world <id> area <mrea>`. Before it, a `WORLD=` sweep
# shard is still in the save's own world, whose `[area] 0` is not the target world's slot 0. A boot
# request (`PortWarp::DirectBoot`) prints `[warp] direct boot: world <id> area <mrea> ...` instead.
WARP_WORLD_RE = re.compile(r"^\[warp\] (?:direct boot: )?world ([0-9a-f]{8}) area ")
FATAL = ("[port] caught",)
ABORT = ("Object list full", "assert", "Assert", "ASSERT")
# Lines that are ordinary in every area and say nothing about the area being tested.
BENIGN = (
    "[aram]",
    "[save]",
    "[input]",
    "[warp] healed to",
    "[map] no map area",
    "[ids]",
    "[skip]",
    "[reach-stub",
    "metroid_prime2_port:",
)


@dataclass
class Phase:
    area: int
    shard: tuple[str, int]
    first_frame: int | None = None
    last_frame: int | None = None
    min_frame: int | None = None
    max_frame: int | None = None
    max_ids: int = 0
    pool_size: int = 0
    misses: list[str] = field(default_factory=list)
    route: str = ""
    fatal: list[str] = field(default_factory=list)
    abort: list[str] = field(default_factory=list)
    stall: bool = False
    stalling_index: int = -1
    died: list[str] = field(default_factory=list)
    credits: list[str] = field(default_factory=list)
    fatal_index: int = -1
    stubs: int = 0
    stub_lines: list[str] = field(default_factory=list)
    jumped_back: bool = False
    notable: list[str] = field(default_factory=list)

    @property
    def updates(self) -> int:
        if self.first_frame is None or self.last_frame is None:
            return 0
        return self.last_frame - self.first_frame

    @property
    def reloaded(self) -> bool:
        """`CStateManager`'s update counter going backwards inside one phase means a world/save
        reload: a run that quit to the front end and was re-booted by `MP2_SKIP_FRONTEND` comes up
        again at the save's own area with the counter back near 0, and the sweep's `NEXTAREA` tokens
        then walk from there - which reads as a mysterious stall plus a wrong area list unless the
        report says so."""
        if self.min_frame is None or self.max_frame is None or self.first_frame is None:
            return self.jumped_back
        return self.jumped_back or self.min_frame < self.first_frame

    @property
    def sampled(self) -> bool:
        """The fault handler fired *after* the stall report: someone sampled the stalled process with
        an external signal (the port prints a backtrace for one), so the phase is a stall, not a
        crash. A real crash prints no stall report before it."""
        return self.stall and self.fatal_index > self.stalling_index >= 0

    @property
    def verdict(self) -> str:
        # A death comes before everything: the game-over screen it ends on is what the stall (and a
        # reload, if something then picked "continue") in the same phase is made of.
        if self.died:
            return "DIED"
        # The ending comes next: it is a known, intended phase whose only symptom is a stall report,
        # so it outranks RELOAD/STALL there. A fault the handler caught *before* any stall report is a
        # real crash, though, and outranks the credits (`sampled` is the tester's own backtrace
        # signal, which is not a crash).
        if self.credits and not (self.fatal and not self.sampled):
            return "CREDITS"
        # A reload comes next: it is what a stall report in the same phase usually means (the run
        # was sitting on a quit/loading screen), and it explains an update counter that went backwards.
        if self.reloaded:
            return "RELOAD"
        if self.stall:
            return "STALL"
        if self.fatal:
            return "CRASH"
        if self.abort:
            return "ABORT"
        if self.reloaded:
            return "RELOAD"
        if self.stubs:
            return "STUB"
        if self.pool_size and self.max_ids >= self.pool_size - 64:
            return "POOL"
        return "OK"

    @property
    def evidence(self) -> list[str]:
        return (self.fatal + self.abort + self.died + self.credits + self.misses + self.stub_lines
                + self.notable)


@dataclass
class Shard:
    path: Path
    index: tuple[str, int]
    world: str = ""
    world_areas: list[int] = field(default_factory=list)
    exit_code: int | None = None
    head: str = ""
    tail: list[str] = field(default_factory=list)
    requested: list[int] = field(default_factory=list)
    arrived: list[int] = field(default_factory=list)
    # From the sweep's manifest: the slice this shard was asked to walk.
    expected_first: int | None = None
    expected_count: int | None = None


@dataclass
class Slice:
    first: int
    count: int
    world: str = "save"
    # slot -> (light|dark, MLVL name), from the sweep's `areas_<tag>.txt` (`tools/world_areas.py`).
    areas: dict[int, tuple[str, str]] = field(default_factory=dict)
    world_name: str = ""


def read_areas(path: str) -> tuple[str, dict[int, tuple[str, str]]]:
    areas: dict[int, tuple[str, str]] = {}
    name = ""
    try:
        lines = Path(path).read_text(errors="replace").splitlines()
    except OSError:
        return name, areas
    for line in lines:
        words = line.split()
        if words and words[0] == "world":
            name = " ".join(words[4:]) or (words[2] if len(words) > 2 else "")
        elif len(words) >= 4 and words[0].isdigit():
            areas[int(words[0])] = (words[2], words[3])
    return name, areas


def shard_label(key: tuple[str, int]) -> str:
    return f"{key[0]}/{key[1]}"


def read_manifest(logs: list[Path]) -> dict[tuple[str, int], Slice]:
    """`sweep_areas.sh` writes `sweep_<tag>.txt` next to the logs: shard -> its slice (first area,
    count, and for a `WORLD=` sweep the world id and the slot list's file)."""
    slices: dict[tuple[str, int], Slice] = {}
    tags = set()
    for path in logs:
        m = re.match(r"log_(.+)_(\d+)\.log$", path.name)
        if m:
            tags.add(m.group(1))
    for tag in tags:
        for manifest in glob.glob(str(log_dir(logs) / f"sweep_{tag}.txt")):
            for line in Path(manifest).read_text(errors="replace").splitlines():
                fields = dict(zip(line.split()[0::2], line.split()[1::2]))
                if "shard" in fields and "first" in fields:
                    entry = Slice(int(fields["first"]), int(fields.get("count", 0)),
                                  fields.get("world", "save"))
                    if fields.get("areas", "-") != "-":
                        entry.world_name, entry.areas = read_areas(fields["areas"])
                    slices[(tag, int(fields["shard"]))] = entry
    return slices


def log_dir(logs: list[Path]) -> Path:
    for path in logs:
        if path.parent.exists():
            return path.parent
    return Path("/tmp/opencode")


def shard_index(path: Path) -> tuple[str, int]:
    m = re.match(r"log_(.+)_(\d+)\.log$", path.name)
    return (m.group(1), int(m.group(2))) if m else (path.stem, 0)


def parse(path: Path, target: str = "save", first: int = 0) -> tuple[Shard, list[Phase]]:
    shard = Shard(path=path, index=shard_index(path))
    # A `WORLD=` sweep's phases start once the target world is warped to (see WARP_WORLD_RE).
    in_target = target == "save"
    warp_phase: Phase | None = None
    info = path.with_suffix(".log.info")
    if info.exists():
        shard.head = info.read_text(errors="replace").strip()
    phases: list[Phase] = []
    current: Phase | None = None
    for index, line in enumerate(path.read_text(errors="replace").splitlines()):
        if (m := EXIT_RE.match(line)):
            shard.exit_code = int(m.group(1))
        elif (m := WORLD_RE.match(line)):
            # The first world line is the save's own world; a run that warps with MP2_WORLD prints a
            # second one for the world it actually entered, so the last line is the one to report.
            shard.world = m.group(1)
            shard.world_areas = len(line.split("areas:", 1)[1].split())
        elif (m := WARP_WORLD_RE.match(line)) and m.group(1) == target and not in_target:
            # The warp lands in the shard's first slot. `[area] N` is printed only when the slot
            # index changes, so a first slot equal to the save's own (0) prints no arrival line.
            in_target = True
            current = warp_phase = Phase(area=first, shard=shard.index)
            phases.append(current)
            shard.arrived.append(current.area)
        elif (m := AREA_RE.match(line)) and not in_target:
            current = None
        elif (m := AREA_RE.match(line)) and warp_phase is not None and current is warp_phase \
                and current.area == int(m.group(1)):
            # The arrival line of the phase the warp line opened: the id samples taken before it
            # were the outgoing world's.
            warp_phase = None
            current.first_frame = current.last_frame = current.min_frame = current.max_frame = None
            current.max_ids = 0
        elif (m := AREA_RE.match(line)):
            warp_phase = None
            current = Phase(area=int(m.group(1)), shard=shard.index)
            phases.append(current)
            shard.arrived.append(current.area)
        else:
            if (m := IDS_RE.match(line)) and current is not None:
                frame, live, size = int(m.group(1)), int(m.group(2)), int(m.group(3))
                if current.first_frame is None:
                    current.first_frame = frame
                current.last_frame = frame
                current.min_frame = frame if current.min_frame is None else min(current.min_frame, frame)
                current.max_frame = frame if current.max_frame is None else max(current.max_frame, frame)
                current.max_ids = max(current.max_ids, live)
                current.pool_size = size
            elif (m := HOP_RE.match(line)) and current is not None:
                text = m.group(2)
                shard.requested.append(int(m.group(1)))
                if "streaming" in text:
                    current.route = "direct"
                elif "handing the hop" in text:
                    current.route = "handed"
                if "loading" in text or "holding" in text or "dropped the areas around" in text \
                        or "does not look like" in text:
                    pass                # the ordinary shape of every hop
                else:
                    current.notable.append(line.strip())
            elif (m := MOVED_RE.match(line)) and current is not None:
                pass
            elif current is None:
                continue
            elif (m := RES_RE.match(line)):
                current.misses.append(line.strip())
            elif line.startswith("[player] killed"):
                current.died.append(line.strip())
            elif CREDITS_RE.match(line):
                current.credits.append(line.strip())
            elif "no game update for" in line:
                current.stall = True
                current.stalling_index = index
                current.notable.append(line.strip())
            elif "reach-stub" in line or "is a port stand-in" in line:
                current.stubs += 1
                text = re.sub(r"^\[reach-stub \d+\]", "[reach-stub]", line.strip())
                if text not in current.stub_lines:
                    current.stub_lines.append(text)
            elif any(p in line for p in FATAL):
                current.fatal.append(line.strip())
                if current.fatal_index < 0:
                    current.fatal_index = index
            elif any(p in line for p in ABORT):
                current.abort.append(line.strip())
            elif not any(b in line for b in BENIGN):
                current.notable.append(line.strip())
    shard.tail = path.read_text(errors="replace").splitlines()[-14:]
    return shard, phases


def print_worlds(slices: dict[tuple[str, int], Slice], phases: list[Phase],
                 seen: dict[str, str]) -> None:
    """Per world and side: slots asked for, slots played, clean (every phase of the slot OK) and
    failing (some phase not OK). A slot played twice (two shards, or a reload) counts once, by its
    worst verdict. Logs without a manifest are left out: their slice is unknown."""
    if not slices:
        return
    rows: dict[str, dict[str, list[set[int]]]] = {}
    tag_world: dict[str, str] = {}
    for (tag, _), entry in slices.items():
        world = entry.world_name or entry.world
        if world == "save":
            world = "save " + seen.get(tag, "?")
        tag_world[tag] = world
        for area in range(entry.first, entry.first + entry.count):
            side = entry.areas.get(area, ("all", ""))[0]
            for key in (side, "total"):
                cell = rows.setdefault(world, {}).setdefault(key, [set(), set(), set(), set()])
                cell[0].add(area)
    for phase in phases:
        world = tag_world.get(phase.shard[0])
        entry = slices.get(phase.shard)
        if world is None:
            continue
        side = entry.areas.get(phase.area, ("all", ""))[0] if entry else "all"
        for key in (side, "total"):
            cell = rows.setdefault(world, {}).setdefault(key, [set(), set(), set(), set()])
            cell[1].add(phase.area)
            (cell[2] if phase.verdict == "OK" else cell[3]).add(phase.area)
    print("\n== worlds: world side asked played clean failing [failing slots]")
    for world in sorted(rows):
        for key in sorted(rows[world], key=lambda k: (k == "total", k)):
            asked, played, ok, bad = rows[world][key]
            clean = ok - bad
            print(f"  {world:<20} {key:<5} {len(asked):>4} {len(played):>6} {len(clean):>5} "
                  f"{len(bad):>7}  {sorted(bad) if bad else ''}")


def main() -> int:
    ap = argparse.ArgumentParser(description="per-area verdict table for a sweep_areas.sh run")
    ap.add_argument("logs", nargs="+", help="log paths or globs (quote a glob)")
    ap.add_argument("--verbose", action="store_true", help="print each phase's notable lines")
    args = ap.parse_args()

    paths: list[Path] = []
    for pattern in args.logs:
        hits = sorted(glob.glob(pattern))
        if not hits:
            print(f"[report] no log matched {pattern}", file=sys.stderr)
        paths.extend(Path(h) for h in hits)
    if not paths:
        return 2

    slices = read_manifest(paths)
    shards: list[Shard] = []
    phases: list[Phase] = []
    for path in paths:
        entry = slices.get(shard_index(path))
        shard, shard_phases = parse(path, entry.world if entry else "save",
                                    entry.first if entry else 0)
        if entry and entry.areas:
            # The port's `[warp] world <id> areas:` line now prints only once the loaded CWorld is
            # the game state's world, so its count is the target world's; the disc's slot list is
            # still the authority (a log from before that fix carries the save world's count).
            shard.world_areas = len(entry.areas)
        shards.append(shard)
        phases.extend(shard_phases)

    # The counter jumping *between* phases is a reload too (the phase before it was the last one the
    # old world state ran), which is how a quit-to-front-end and a skip re-boot show up.
    last_frame: dict[int, int] = {}
    for phase in phases:
        previous = last_frame.get(phase.shard)
        if previous is not None and phase.first_frame is not None and phase.first_frame < previous:
            phase.jumped_back = True
        if phase.last_frame is not None:
            last_frame[phase.shard] = phase.last_frame

    print("== shards")
    for shard in sorted(shards, key=lambda s: s.index):
        end = "still running" if shard.exit_code is None else f"exit={shard.exit_code}"
        bad = [p for p in phases if p.shard == shard.index and p.verdict != "OK"]
        played = sorted({p.area for p in phases if p.shard == shard.index})
        slice_text = ""
        if shard.index in slices:
            first, count = slices[shard.index].first, slices[shard.index].count
            if first not in played:
                slice_text = f", asked for {first}..{first + count - 1}, reached none of it"
            else:
                last = max(played)
                slice_text = (f", asked for {first}..{first + count - 1}, "
                              f"reached {first}..{last}")
        print(f"  {shard.path.name}: {end}, world {shard.world or '?'} "
              f"({shard.world_areas} areas), played {len(played)}{slice_text}, "
              f"{len(bad)} area(s) not OK")
        if shard.head:
            print(f"    {shard.head}")

    def area_name(phase: Phase) -> str:
        entry = slices.get(phase.shard)
        if entry is None or phase.area not in entry.areas:
            return ""
        side, name = entry.areas[phase.area]
        return f"  {side:<5} {name}"

    print("\n== areas: slot shard verdict updates id-high-water res route stubs [side name]")
    for phase in sorted(phases, key=lambda p: (p.shard[0], p.area, p.shard[1])):
        pool = f"{phase.max_ids}/{phase.pool_size}" if phase.pool_size else "?"
        note = ""
        if phase.reloaded:
            note = " (reloaded: the update counter went backwards in this phase)"
        elif phase.sampled:
            note = " (fault sampled after the stall)"
        print(f"  {phase.area:>3}  {shard_label(phase.shard)}  {phase.verdict:<6} {phase.updates:>5}  "
              f"{pool:>9}  {len(phase.misses):>2}  {phase.route or '-':<6}  {phase.stubs}"
              f"{area_name(phase)}{note}")

    # Coverage against the manifest: a shard's slice should have been walked in order, so the first
    # area it did not reach says where it stopped. An area inside the walked range that no shard
    # played is the interesting one - a token that never fired, or a hop that never arrived.
    requested = {(shard.index[0], a) for shard in shards for a in shard.requested}
    if slices:
        print("\n== coverage")
        for index in sorted(slices):
            first, count = slices[index].first, slices[index].count
            want = list(range(first, first + count))
            played = {p.area for p in phases if p.shard[0] == index[0]}
            missing = [a for a in want if a not in played]
            if not missing:
                continue
            shard = next((s for s in shards if s.index == index), None)
            end = "unknown" if shard is None or shard.exit_code is None else str(shard.exit_code)
            wall = [p for p in phases if p.shard == index and p.verdict != "OK"]
            inside = [a for a in missing if (index[0], a) in requested]
            if wall and wall[-1].verdict == "CREDITS":
                # The ending is a run of its own areas (`game_end_partN`): once one is entered the
                # game walks the rest itself, so the slots after it are never visited by the tokens.
                # They are not gaps in the sweep, and naming one as "first missing" reads as a hole.
                ending = wall[-1].area
                print(f"  shard {shard_label(index)}: reached {len(want) - len(missing)}/{len(want)} of "
                      f"{first}..{first + count - 1}; the ending took over at area {ending} "
                      f"(CREDITS)")
                print(f"     not walked once the ending took over: {missing} - the game runs the "
                      f"ending's own areas itself, so these are not gaps in the sweep")
                continue
            elif wall:
                reason = f"the run stopped in area {wall[-1].area} ({wall[-1].verdict})"
            elif inside:
                reason = "the run played to its frame limit, so a hop's stream never completed"
            else:
                reason = "the run ended before the slice was walked (exit %s)" % end
            print(f"  shard {shard_label(index)}: reached {len(want) - len(missing)}/{len(want)} of "
                  f"{first}..{first + count - 1}; first missing {missing[0]} - {reason}")
            if inside:
                print(f"     asked for but never arrived: {inside}")

    walls = [p for p in phases if p.verdict != "OK"]
    if walls:
        print("\n== walls")
        for phase in sorted(walls, key=lambda p: (p.verdict != "CRASH", p.shard, p.area)):
            print(f"  -- area {phase.area}{area_name(phase)} (shard {shard_label(phase.shard)}): "
                  f"{phase.verdict}")
            for line in phase.evidence[:10]:
                print(f"       {line}")
    elif args.verbose:
        print("\n== walls: none")

    if args.verbose:
        print("\n== every phase's notable lines")
        for phase in sorted(phases, key=lambda p: (p.shard, p.area)):
            if phase.notable:
                print(f"  area {phase.area} (shard {shard_label(phase.shard)}):")
                for line in phase.notable:
                    print(f"       {line}")

    misses = sorted({(p.shard[0], p.area, line) for p in phases for line in p.misses})
    if misses:
        print("\n== missing resources by area")
        for tag, area, line in misses:
            print(f"  {tag} area {area}: {line}")

    print_worlds(slices, phases, {s.index[0]: s.world for s in shards if s.world})

    bad_exits = [s for s in shards if s.exit_code not in (None, 0)]
    if bad_exits:
        print("\n== abnormal exits")
        for shard in bad_exits:
            print(f"  {shard.path.name}: exit={shard.exit_code}")
            for line in shard.tail:
                print(f"       {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
