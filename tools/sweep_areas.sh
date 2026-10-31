#!/bin/bash
# Area regression sweep: boots the save's own world (no `MP2_WORLD`), teleports each shard to its
# first area with `MP2_TELEPORT`, then walks the rest of its slice with `NEXTAREA` tokens and ends
# every run with `MP2_FRAMES`. With `WORLD=<world>` it sweeps that world instead (see below). Built on `tools/run_gameplay.sh` (see its header for the traps it
# checks: a user dir without the memory card, and an input script that presses `START` - the generated
# script never presses `START`, but it does press `A`, see `write_input` below for why that matters).
#
#   USERDIR=<dir holding the card> [WORLD=<world>|all] [SHARD=<n>|all] [SHARDS=4] [COUNT=63] [DWELL=1400]
#   [TAIL=1400] [HEAL=400] [AREAS=first:count, one shard over that run] [TAG=sweep] [DISPLAY_BASE=86] [ASAN=1] tools/sweep_areas.sh
#
# `WORLD` sweeps any world: an MLVL id, a display name (`agon`, `torvus`, `sanctuary`, `great temple`,
# `temple grounds`) or a part of the MLVL name - whatever `tools/world_areas.py` resolves (it reads the
# disc's MLVL, so it needs no boot). `COUNT` then defaults to that world's own slot count, and every
# shard boots with the single world+area request `MP2_WORLD="<world id>,<first slot's MREA id>"`
# (docs/RUNNING.md, "Booting into a world and an area": a separate `MP2_TELEPORT` is served before the
# world loads, and a plain slot index in `MP2_WORLD` is read as an asset id). The slot list goes to
# `areas_<TAG>.txt` and the manifest names it, so `tools/sweep_report.py` can print area names and the
# light/dark split. `WORLD=all` sweeps the five campaign worlds one after another, each with the tag
# `<TAG>_<world id>`; `SHARDS` stays the number of games running at once. Unset, `WORLD` keeps the
# original sweep: the save's world (Temple Grounds) by `MP2_TELEPORT`.
#
# `COUNT` is the number of area *slots* in the world (Temple Grounds has 63, slots 0..62); the sweep
# asks for them by index, which is what `[warp] world <id> areas:` prints and what the warp's `kArea`
# handler takes as a plain number. `SHARDS` slices `COUNT` into contiguous runs so a full sweep is
# several short runs on several displays instead of one long one; `SHARD=all` (the default) launches
# all of them in parallel and waits.
#
# **The slot list includes the ending, and walking into it ends the shard.** Slot 53
# (`game_end_part3`) starts the ending and the game hands over to 56 (`game_end_part4`), the only
# area with the credits script (kSF_CompletionScreen): `CScriptSpecialFunction::AcceptCredits` puts
# `CCredits` up, the game stops updating and the port prints `[end] the credits are rolling`.
# `tools/sweep_report.py` calls that phase `CREDITS` rather than `STALL` (it is not a wall), and its
# coverage block says the slice ends there. So a shard whose slice runs past 52 - shard 3 of 4, with
# the defaults - stops in the credits and walks none of the slots after 53 (54, 55 and 57-62), which
# is expected rather than a batch of missed areas; they need their own runs (`AREAS=54:2`,
# `AREAS=57:1`, `AREAS=59:4`). Slots 47 and 50 (`game_end_part1/2`) are ordinary walks; 58
# (`game_end_part5`) opens a save prompt that stalls the run. `COUNT` stays 63 because a full sweep
# is what proves the ending path works; pass `COUNT=53` (slots 0..52) for a sweep of the areas
# before the ending.
#
# Every area gets `DWELL` game updates: the `NEXTAREA` that asks for it plus, `HEAL` updates later, a
# heal (a spawn in a hazard should not end the sweep) and then the rest of the dwell in the area,
# which is the gameplay the sweep is for. Tokens are counted in game updates, the same counter
# `MP2_FRAMES` uses (`platform/README.md`, "Input"), so the dwell and the frame budget below are one
# axis and a shard's own arithmetic says exactly where each area starts.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
USERDIR=${USERDIR:-}
WORLD=${WORLD:-}
if [ "$WORLD" = "all" ]; then
  rc=0
  for w in $(python3 "$ROOT/tools/world_areas.py" --campaign); do
    WORLD=$w TAG="${TAG:-sweep}_$w" "$0" || rc=1
  done
  exit $rc
fi
WORLD_ID=""
AREAFILE=""
if [ -n "$WORLD" ]; then
  AREAFILE="${LOGDIR:-/tmp/opencode}/areas_${TAG:-sweep}.txt"
  mkdir -p "$(dirname "$AREAFILE")"
  python3 "$ROOT/tools/world_areas.py" "$WORLD" > "$AREAFILE" || exit 2
  read -r _ WORLD_ID _ WORLD_COUNT _ < "$AREAFILE"
  COUNT=${COUNT:-$WORLD_COUNT}
  echo "[sweep] world $WORLD -> $(head -1 "$AREAFILE" | cut -d' ' -f2-); slots in $AREAFILE"
fi
COUNT=${COUNT:-63}
SHARDS=${SHARDS:-4}
DWELL=${DWELL:-1400}
# The tail has to cover the last area's own tokens too, not just leave it some gameplay: its second
# heal sits at `HEAL + DWELL/2` (1100 by default), and a tail shorter than that meant the last area of
# every shard went without the heal the others get - which is exactly the difference a death in a
# dark-world area turns on.
TAIL=${TAIL:-1400}
HEAL=${HEAL:-400}
# God mode by default: a hazard death (the dark-world atmosphere kills a suitless player in ~19 s)
# ends a run in the lose-game screen, where no game update runs, and it reads as a stall. A
# deliberate hazard-death run turns it off with GODMODE=0. The HEALs below stay: they cost nothing
# and still matter when the cheat is off.
GODMODE=${GODMODE:-1}
TAG=${TAG:-sweep}
DISPLAY_BASE=${DISPLAY_BASE:-86}
LOGDIR=${LOGDIR:-/tmp/opencode}
SHARD=${SHARD:-all}

if [ -z "$USERDIR" ] || [ ! -d "$USERDIR" ]; then
  echo "[sweep] USERDIR must name a directory that holds the memory card" >&2
  exit 2
fi
[ -d "$USERDIR/USA" ] || [ -d "$USERDIR/EUR" ] || [ -n "${MP2_NEW_GAME:-}" ] || echo "[sweep] warning: $USERDIR has no USA/ or EUR/ card (and MP2_NEW_GAME is unset) - a run would fall back to the front end" >&2
command -v python3 >/dev/null || { echo "[sweep] python3 is needed for the slice arithmetic" >&2; exit 2; }
mkdir -p "$LOGDIR"
# A fresh full sweep starts a fresh manifest; a single shard must not wipe its siblings' lines.
[ "$SHARD" = "all" ] && rm -f "$LOGDIR/sweep_$TAG.txt"

# slice(i) -> "<first> <count>": the contiguous run of area slots shard i walks.
slice() {
  if [ -n "${AREAS:-}" ]; then echo "${AREAS%:*} ${AREAS#*:}"; return; fi   # AREAS=first:count
  python3 - "$COUNT" "$SHARDS" "$1" <<'PY'
import sys
count, shards, i = (int(v) for v in sys.argv[1:4])
per = -(-count // shards)
first = i * per
print(first, max(0, min(per, count - first)))
PY
}

# The generated script for one shard. It presses no menu button - a `START` in game opens the pause
# menu and a paused game performs no `CStateManager::Update`, which is what `MP2_FRAMES` counts - and
# it dismisses whatever screen an area throws at it with `B`, twice per area. That is load-bearing and
# the button choice matters:
#
#   * an area whose script runs a `CScriptHUDMemo` (or a suit message) calls
#     `CStateManager::ShowPausedHUDMemo` -> `kSMT_MessageScreen` -> `CMFGame::EnterMessageScreen` ->
#     `SetFlowState(kFS_Paused)`, and a paused game updates nothing until the player dismisses the
#     screen. The token counter in `MP2_INPUT` keeps running while paused, so a periodic press gets
#     the run moving again (measured: the first Temple Grounds sweep, with no button tokens at all,
#     stopped dead in areas 21 and 35 and `tools/sweep_report.py` read both as STALL);
#   * `B` is the press, not `A`: `A` is the action button, and an arrival lands the player at the
#     area's first spawn point, which is often inside a save station's trigger volume -
#     `CScriptSpecialFunction::AcceptSaveStation` takes `kSM_Action` from `A`, calls
#     `CStateManager::EnterSaveGameScreen` and then the sweep's own next `A` walks the save screen into
#     a quit, which the port's front-end skip turns into a *reload of the save*. Measured 2026-10-17:
#     with `A` twice per area, three of the four shards quit to the front end mid-sweep, reloaded slot
#     0 (id count back to 352, update counter back to 0) and then walked `NEXTAREA` from area 0 again -
#     the logs looked like stalls in areas 14/21/35 and were nothing of the kind. `B` leaves a screen
#     without activating anything. What 14, 21 and 35 showed with `B` - no game update, no reload -
#     was the *player dying* there (the dark-world atmosphere, see the second `HEAL` below): a death
#     removes CMFGame for the game-over screen, `B` does not answer it, and that screen's "continue
#     from last save" is very likely what `A` answered there too (unverified for `A`).
write_input() { # <first> <n> <path>
  python3 - "$1" "$2" "$DWELL" "$HEAL" "$3" <<'PY'
import sys
first, n, dwell, heal = (int(v) for v in sys.argv[1:5])
path = sys.argv[5]
tokens = []
for k in range(n):
    t = k * dwell
    if k > 0:
        tokens.append("NEXTAREA@%d" % t)
    tokens.append("HEAL@%d" % (t + heal))
    # A second heal half a dwell later: a suitless player in a dark-flagged area (the flag carries
    # into the next area until that area's own attributes reset it) dies ~19 s after a full heal, and
    # one heal per dwell left a ~23 s gap - that killed the player in 13->14, 21 and 35.
    tokens.append("HEAL@%d" % (t + heal + dwell // 2))
    tokens.append("B@%d+4" % (t + heal + 120))
    # Not answered here: Temple Grounds 58 (`game_end_part5`) opens "Save progress to Memory Card?"
    # about 120 updates after arrival. B does nothing there (the table maps it to DoCancel but
    # CSaveGameScreen wires no cancel for kUIT_SaveReady), and DOWN+A (No) was measured to quit to
    # the front end (`[warp] world 69802220 areas: 0=...:FrontEnd04`, the report's RELOAD) - it is
    # the ending's save screen, so no token keeps the run going. STOP_ON_STALL below ends the shard
    # ~30 s after the pause instead of at the backstop timeout.
    tokens.append("B@%d+4" % (t + dwell // 2))
open(path, "w").write(",".join(tokens) + "\n")
print(len(tokens))
PY
}

# The manifest is what makes the report able to say whether an unplayed area is a hole in the token
# cycle or simply the point a run died at: the logs alone cannot tell those apart.
manifest_write() { # <index> <first> <count> <frames> <input> <log>
  printf 'shard %s first %s count %s frames %s input %s log %s world %s areas %s\n' "$1" "$2" "$3" \
    "$4" "$5" "$6" "${WORLD_ID:-save}" "${AREAFILE:--}" >> "$LOGDIR/sweep_$TAG.txt"
}

run_shard() { # <index>
  local i=$1 first n frames
  read -r first n <<<"$(slice "$i")"
  if [ "$n" -le 0 ]; then
    echo "[sweep] shard $i: nothing to do"
    return 0
  fi
  frames=$(( ( n - 1 ) * DWELL + TAIL ))
  local input="$LOGDIR/in_${TAG}_$i.txt"
  local tokens
  tokens=$(write_input "$first" "$n" "$input")
  # 32 KiB per shard, so parallel shards cannot race on one memory card.
  local user="$LOGDIR/u_${TAG}_$i"
  rm -rf "$user"
  cp -r "$USERDIR" "$user"
  local display=$(( DISPLAY_BASE + i ))
  local secs=$(( frames / 35 + 240 ))
  manifest_write "$i" "$first" "$n" "$frames" "$input" "$LOGDIR/log_${TAG}_$i.log"
  echo "[sweep] shard $i: areas $first..$(( first + n - 1 )) ($n), $frames updates, display :$display," \
       "$tokens tokens, log $LOGDIR/log_${TAG}_$i.log"
  # The boot: the save's world by slot index, or another world by its id and the slot's MREA id.
  local world="" teleport="$first"
  if [ -n "$WORLD_ID" ]; then
    world="$WORLD_ID,$(awk -v i="$first" '$1 == i { print $2 }' "$AREAFILE")"
    teleport=""
  fi
  RUNTAG="${TAG}_$i" DISPLAY_NUM=":$display" USERDIR="$user" INPUT_FILE="$input" \
    WORLD="$world" TELEPORT="$teleport" FRAMES="$frames" SECS="$secs" LOG="$LOGDIR/log_${TAG}_$i.log" \
    GODMODE="$GODMODE" ASAN="${ASAN:-}" STOP_ON_STALL=1 "$ROOT/tools/run_gameplay.sh"
  local rc=$?
  echo "[sweep] shard $i: exit=$rc (0 clean, 124 timeout, 125 stalled and stopped early, 134 abort, 139 crash)"
  return $rc
}

if [ "$SHARD" != "all" ]; then
  run_shard "$SHARD"
  exit $?
fi

pids=()
for i in $(seq 0 $(( SHARDS - 1 ))); do
  run_shard "$i" &
  pids+=($!)
done
rc=0
for p in "${pids[@]}"; do
  wait "$p" || rc=1
done
echo "[sweep] all shards done; report: python3 tools/sweep_report.py $LOGDIR/log_${TAG}_*.log"
exit $rc
