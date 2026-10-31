# Running the port (desktop)

Moved out of AGENTS.md to keep the per-turn prompt small. `T` is the toolchain dir from AGENTS.md
Commands; the run scripts below start their own `Xvfb` (default `:86`, `-screen 0 1280x720x24 -ac`),
so a hand-started one (`pgrep -x Xvfb` + `xdpyinfo` to check) is only for a bare binary, as in the
`gdb` boot below.

## Boot under gdb (from build-port-link)

    DISPLAY=:86 SDL_VIDEODRIVER=x11 MESA_VK_WSI_DEBUG=sw SDL_AUDIODRIVER=dummy \
      MP2_DISC="/run/media/odran/Leo/Portable/roms/gc/Metroid Prime 2 - Echoes.iso" \
      MP2_USER_PATH=/tmp/mp2u MP2_CACHE_PATH=/tmp/mp2c \
      timeout -k 5 120 gdb -q -batch -ex run -ex bt ./metroid_prime2_port > /tmp/mp2boot.log 2>&1
    grep -n 'reach-stub\|signal\|^#[0-9]\|exited\|ssert\|fatal' /tmp/mp2boot.log

A disc that won't open, isn't G2ME01, G2MP01 (PAL, experimental: docs/PAL.md) or R3ME01/R3MP01 (Trilogy, experimental: docs/TRILOGY.md) revision 0, or is cut short (last 8 files unreadable) is refused at mount;
with a window and no `MP2_FRAMES`/`MP2_HEADLESS`/`MP2_NO_DISC_DIALOG=1` a message box then asks for
another image (blocks a scripted run until killed), otherwise the run exits 1.

Robustness logs: `port: settings:`/`port: env:`/`port: storage:` at start; `watchdog:` after 5 s without
a frame (`MP2_WATCHDOG_TEST_STALL=<frame>`); `port: crashed:` with the stack (`MP2_CRASH_TEST=segv`).

Sample a running boot: `gdb ... -x tools/gdb_interrupt.py -ex run -ex 'thread apply all bt 14'`
(`MP_SAMPLE_SECS=240`). Screenshot: `ffmpeg -f x11grab -video_size 1280x720 -i :86 -frames:v 1 x.png`.

## Straight into gameplay

Needs a save in `<user>/USA/Card A` (a copy of `/tmp/opencode/u_a10`). A PAL disc reads `<user>/EUR/Card A`. `MP2_NEW_GAME=1` starts a new game
when the slot (or the whole card) is empty, writing nothing, so any empty `MP2_USER_PATH` works. Same env as above plus
`MP2_USER_PATH=<copy> MP2_SKIP_FRONTEND=1 MP2_SKIP_CINEMATICS=1 MP2_SKIP_INTRO=1 MP2_TELEPORT=<area>`
-> `[harness] gameplay reached at 39.1 s` for area 5 (36.0 s without `MP2_TELEPORT`; first update
34.0 s; desktop 2026-10-17). Phone: the same four as `env.MP2_...` lines in `files/port_paths.txt`.

PC-first debugging: rendering, cinematics and HUD questions reproduce on desktop; keep the phone for
the final APK check. `MP2_USER_PATH` must be the directory that holds `USA/Card A` (one level above
it the runner warns "has no USA/" and falls back to the front end):

    USERDIR=/tmp/opencode/mp2card/user FRAMES=6000 TELEPORT=0 DISPLAY_NUM=:86 LOG=/tmp/opencode/pc.log \
      tools/run_gameplay.sh &        # SKIP_INTRO=0 leaves the intro cinematic playing (default 1)
    tools/shot.sh 86 /tmp/opencode/pc.png 50          # timed to the log's phase lines

`run_gameplay.sh` starts an Xvfb on `DISPLAY_NUM` when none is running there and stops it when the
run ends: don't start one by hand. A script that launches the game itself must kill the Xvfb it
started (the orchestrator's watcher reaps client-less ones older than 30 min).

`PRESENT_EVERY=<n>` (default 6) shows one frame in n on the Xvfb, and `RENDER_EVERY=<n>` (default:
`PRESENT_EVERY`) builds one frame in n: the frames in between are dropped before they are recorded,
so the game spends no CPU on the frames it never shows (a sandworm smoke run: 29.1 s of game CPU
with `RENDER_EVERY=1` against 8.6 s with the default 6). A frame that is not rendered keeps the last
rendered frame's EFB, so the read-backs (`GXPeekZ`, the EFB copy) return the last drawn values, and
a screenshot always comes from a rendered frame (the frame it is taken in and the one after it are
both rendered).

An EFB copy (`GXCopyTex`) is the one thing that still costs a frame: a copy into a destination that
was copied within the last N frames (N = the render interval, `AURORA_RENDERSKIP_COPYDEST_N`
overrides it) keeps its texture and is skipped with the frame, so its image lags by up to N frames;
a new or one-shot destination (a render-to-texture at an area load) is rendered in full, so the
texture is produced. `AURORA_RENDERSKIP_COPYDEST=0` turns that off: every copy then renders its
frame. The log says how much the copies cost at shutdown (`AURORA_RENDER_EVERY: N frames were
rendered in full because they hold an EFB copy (M of them a read-back), K copies were skipped
because their destination was fresh`).

## Fast runs and console scripts

`MP2_FRAME_LIMIT=0 MP2_TICKS_PER_FRAME=<1..16>` runs exactly that many fixed 1/60 s ticks per loop
iteration (`platform/include/port_timing.h`), as fast as the machine allows. Such a run is
*unpaced* (`PortHarness::Unpaced`): neither `CGraphics::EndScene`'s 60 Hz `PaceFrame` nor
`PortPaceFrame` waits, so a loop iteration costs only its work (~4500 updates/s in agon54; 600
updates from launch to exit in ~1.9 s, 2026-10-23). `MP2_FAST_LOAD=1` (with `MP2_SKIP_FRONTEND`;
`run_gameplay.sh` sets it, `FAST_LOAD=0` turns it off) does the same for a capped run until its first
game update, so the pre-loop pak loading isn't paced either. `MP2_FRAMES` still counts ticks.
Console scripts for `MP2_CONSOLE=<port>` runs: `tools/drivers/console.py <port> <cmdfile> [out]`, lines
`@f<n> <cmd>` or `@<secs> <cmd>`. Each run of `@f` lines is queued up front through the console's
`at <n> <cmd>` (one `at` per `;` part), so a command runs exactly at update n even unpaced;
`CON_POLL=1` restores the old polling of `frame` (late by many updates in a fast run).
`MP2_CONSOLE_WAIT=<secs>` (`1` = 600) holds the boot until a console client connects; the smoke
harness sets it for every console.py family, since an unpaced run is otherwise thousands of updates in
before console.py's 0.5 s retry and every overdue `at` line fires on one update.
`gallery [mask]` prints the slideshow gallery unlock mask (0..63) or sets it (via PercentScans and the
normal/hard-mode flags). `tools/drivers/slideshow_gallery.con` (`SMOKE_WORLD=69802220,cee3af0d`,
`SKIP_FRONTEND=0`, `SMOKE_DRIVER` generic) sets 63, opens the slideshow, steps slides and leaves it; the
slideshow stops the update counter, so that script's steps are wall-clock and it logs `[slideshow] ...`.
Reach states with `fsm <id> <state>` (an FSM2 state left at once reports `entered; now X`; a state in
an FSM2 sub-machine is entered through it, and the reply names the machine; a name the machine does
not have is reported as `unknown state 'X'; valid names: ...` with the names it has - the actor's
own states and, for an FSM2, the states of its sub-machines - a state that was entered and left
at once (the machine is back where it was) says `entered and left at once; now X`, and an actor
whose machine was never set up (it was not created yet) says `the actor has no state machine state
(is it active?)`), `place`,
`send`, `damage` (direct, no projectile; also the combos `darkburst`/`sunburst`/`sonicboom` and
`powerbomb`, which knockback rule sets tell apart) and `shoot <id> [charged|missile] [d] [dir x y z]` (a real beam projectile
from d units, travelling along dir, for modules that only count projectile hits on a joint; `missile`
instead of `charged` fires a real player missile through the launcher's own path
(`CPlayerGun::FireSecondary`: weapon type kWT_Missile, the missile's damage, aimed at the actor the
way player aim is and homing on its id, granting 10 missiles if the ammo has none; it leaves from
the gun, so `d` and `dir` do not apply), for the modules that only react to a missile such as
Rezbit's `DeflectMissiles`).
The default beam direction runs from the player's eye to the target; a shot still collides with
intervening walls/doors. `beam` changes asynchronously: allow the gun to finish switching before
testing a particular weapon. A completed `scan` soft-pauses projectile updates; `visor combat`
ends that scan and releases the console's held L before switching back.
`struggle <0..1>|off` holds the player's attached-actor struggle value (`CPlayer::
mAttachedActorStruggle`) at that value, applied in the player's own morph-ball update and cleared by
`off`: the game only grows it from held turn/jump buttons while the morph ball is grabbed, so 1
(Tryclops' `Think` reads exactly 1.f for its dizzy gore, then `BallCloseToCollision`) is otherwise
out of reach.
`place <id> <x> <y> <z>` moves an actor and `place <id> player [d]` puts it d (6) units in front
of the player; both take an optional rotation after the point: `place <id> <x> <y> <z> <yaw> [pitch
[roll]]` in degrees (the yaw about the world's up, the pitch about the right one, the roll about the
forward one) - without it the actor keeps its rotation.
`material <id> [+|-]<m> ...` prints an actor's material list and adds (`+`, the default) or removes
(`-`) materials, by name without `kMT_` (any case) or bit number: `material u0400 -solid` takes
kMT_Solid off the player the way entering a turret does, so a line-of-sight ray ending at its aim
point (ElitePirate ShouldFire) can come back clear.
`bodystate <id> <n>` sets a patterned actor's body controller state directly (n is the
`pas::EAnimationState` index: 0 ready, 2 lie on ground, 19 cover, 20 wall hang); the state machine
reaches only some of the body states, and the coverage runs need the others.
`statecall <id> <state> <msg> [dt]` runs one FSM2 state body once (msg 0 Activate, 1 Update, 2
Deactivate) outside the machine, from the actor's whole state table: states the FSM2 has no node for.
`codecall <id> <code> [dt]` does the same for a code function (codes otherwise run only on transitions).
`pathfail <id> on|off` makes a patterned actor's path search (its `GetSearchPath`) report a miss:
`CPathFindSearch::Search` returns kR_NoPath and `FindClosestReachablePoint` kR_NoSourcePoint (a
TARGET_PC hook), so the arms behind a failed search run on a mesh that reaches; pair it with
`codecall` (`pathfail u0454 on; codecall u0454 SelectTarget; pathfail u0454 off`).
`setarea <id> <area>` sets an entity's current area id (prints the old one), e.g. so a same-area
filter sees a neighbour from another area; put the old id back after. An area that exists but is
not loaded is refused (`CActor::UpdatePortalSystemState` reads its portal data on the next update).
`obj <id>` prints, besides hp, connections and a patterned actor's `fsm` state, its `body state`
(the `pas::EAnimationState` index `bodystate` takes). Non-patterned actors with their own private
FSM2 (CIngSnatchingSwarm) have no generic accessor, so `obj` prints no state for them.
`touch <mover id> <other id>` calls the mover's `CollidedWith` the way the collision code does
(`CGameCollision::MakeCollisionCallbacks`): with a one-entry collision list whose 0x30 slot holds
the *other* actor's material list. It needs no real collision, so a module whose own work is in
that callback can be driven (the SwampBossStage1 "player touched" flag, the retail's fn_78_9A44).
`touchact <actor id> <other id>` calls `actor->Touch(other)` itself (the call the collision code makes
for two overlapping actors that `SetCallTouch`): a trooper `Touch`ing a trigger, say.
`var <id> gren.<field> [n]` (CGrenchler, `PortConsole::RegisterVar`): `gren.scanned` (the scan flag a
guardian's crystals need), `gren.crystal` (crystal state; 4 = ForceGrapple), `gren.crystaldmg`,
`gren.grappledmg`, `gren.tailhp`, `gren.action` (read-only attack state), `gren.tail` (1 runs DestroyTail).
The guardian grapple chain: `var <id> gren.scanned 1; var <id> gren.crystal 4; fsm <id> Pursue`
(`tools/cov/specs/grenchler_grapple*.con`).
Quadraxis knees: `var <CDigitalGuardian> dg.kneecol0..3` reads the knee collider uids (u0580..u0583 in
Sanctuary 09_cliff_temple_dark), `dg.killknee0..3 1` sets that collider's HP to 0 and gives the body its
XDMG (fn_14_5838/5470): the boss-bar counter (e0035005f) drops 9 -> 8, IS07 runs and the FSM goes to
KneeDestroyedReaction. `damage` on the colliders cannot (they forward hits and lose no HP); boot
`1baa96c2,09_cliff_temple_dark`, then `layer 3`, `layer 11`, `send e00350060 Activate`, `send e0035000f Activate`.
Engaged CPuffer (Torvus 07_swamp_shaft): there is none. Retail's Puffer has no aggro: its FSM is
Start/Patrol/Dead with no triggers, so `fsm PathFind` returns to Patrol (CPatterned::PathFind needs a
search path). The states it has: roaming Patrol, and `send e002801f0 Action` (dies and is deleted).
`gameevent <score|kill|generic|scan|damage|stop|spawn> [src] [tgt] [value] [as <proxyType>]` delivers a
multiplayer game event (CGMMultiplayer::NotifyListeners) to every CScriptPlayerProxy listener whose mask
holds tgt (0), which CGMSinglePlayer never registers: CListener::OnGameEvent -> OnGameEvent, and through it
NotifyProxies/AddDOT/ClearDOT. value: float for damage, state for generic. `as N` retypes each proxy for the
call (shipped single-player ones are EchoVisorWatcher, which ignores events; not 1, CoinCollector, whose arm
casts the mode to CGMCoin). Prints the proxies reached.
`scaninfo <id>` prints the 'SCAN' asset an actor's GetScannableObjectInfo answers (no scanning needed).
`a; b; c` on one line runs all of them in the same game tick (a one-frame window); one reply each.
`grapple <id> [d]` (ids from `objs grapple`) wakes a grapple point, stands the player d (8) units
from it and holds L: with `give grappleBeam 1` the player locks on and grapples (`status`: orbit 5).
A bare `tp` + `press l` misses: the lock-on press needs the orbit candidate lists, rebuilt every 20 updates.
`lock <id> [d] [updates]` is the real lock-on press on any actor: it takes the first of 8 sides (d, default 10, away) with a clear eye-level line to the actor, faces it, rebuilds the candidates and holds L (`orbit`: state 1). A blocked line (a wall between) is why a plain `press l` never locks.
`scan <id> [d] [updates]` twice ~30 updates apart (scan visor + L, then tp d (40) units, forced orbit lock) scans an actor; `scan` alone prints progress (game state 1 = scan display up).
`spawn <fourcc> [n] [d <dist>] [ancs <id>|u<id> [char [anim]]] [fsm <id>] [rules <id>] [wpsc <id>]
[conn <state> <msg> <id>]... [model <id>|u<id>] [inactive] [prop <propid> <value>]...` creates n (1) script objects of a type through its `ScriptLoader` entry,
d (8) units in front of the player and facing it, for loaders no disc placement reaches (CHOG, COIN,
DBAR, RIPR, RSFA, SPNK): the property stream holds only EditorProperties (name, transform, active)
and what the options add, so every other SLdr default applies. Each gets editor id
`<area>FF00`+ in layer 0 (so `e0036ff00` names the first in area 0x36), AddObject and kSM_AreaLoaded.
`ancs`/`model` take a hex asset id or an actor (`u`/`e` id) whose ANCS+character or static model is
borrowed as a stand-in body (the types' own assets are in no pak); `fsm` sets the patterned state
machine; `ancs u<id>` keeps the donor's own character unless `char` follows; a patterned spawn
writes `knockbackRules` 393ca543 (in every Metroid*.pak; the SLdr's -1 supplies no rules), `rules <id>`
another RULE (`ffffffff` keeps -1); CHOG's two projectile WPSC props default to 681f3907 (refused
when absent), `wpsc <id>` sets them, or another type's common `projectile` prop (ef485db9);
`conn <state> <msg> <id>` adds a script connection (names as `obj` prints them; `<id>` is `u<id>`,
`e<hex>` or a bare editor id, e.g. a DBAR's GRNT); the reply lists ancs/char/anim, rules and wpsc
(`tools/drivers/con1_console.con`);
`prop` adds a raw 4-byte top-level property (`1.5` is a float, else an integer). Refusals
(logged, no crash): no loader for the type, an asset not in the resource factory, a loader that
returns null (patterned types without `ancs`), DBAR without `model` (its ctor needs the left chunk
model). A patterned actor whose body controller is still off after AreaLoaded (CHOG without its own
state machine) is activated by `spawn`, which says so. A COIN rolls its lifetime and impulse only on
the Activate that wakes it: `spawn COIN 3 inactive model u<actor> prop 54a8c481 20.0 prop 7dd63999 30.0`
then `send e<area>ff00 Activate`.
Ids may be `e<editor id>` (unique ids are reused across layer loads); `covdump` writes a coverage build's gcov counters mid-run (runs that hang in shutdown keep them).

## Save-state round-trip check

`tools/statecheck.py --userdir <dir with the card> --area 5 --area 12 [--headless] [--post-wait N]`
starts `run_gameplay.sh` with a console, and per room (first area = TELEPORT, later ones `warp`)
does: `hold 1`, `statedump A`, `state save`, `step 1`, `statedump A1` (natural one-tick drift),
`state load N hold` + `hold 0` (a load only advances while the game runs; `hold` makes the game hold
itself on the tick `[savestate] room state restored` shows, so nothing runs past it), `statedump B`. A diff A→B is STRICT only if B also differs
from A1 (so tick drift is filtered); the rest are warnings. `--post-wait N` also steps N ticks and
dumps C. Dumps land in `--out` (`<n>-area_<a>-A.jsonl`); `--detail`/`--all` list more. An empty
card runs as a new game (`MP2_NEW_GAME=1`). Use a copy of the card: the run saves slot 8 and loads it.
The player is held safe by default: every room runs `god 1` and `nodarkair 1` before its `--setup`
commands (then `--wait` unheld ticks to let the room settle), so an enemy hit or a dark-aether drain
is not a strict player hp diff; `--unsafe-player` leaves the player as the room found it.

## Driving pads 1-3 (multiplayer)

`MP2_INPUT` entries take a pad prefix: `p1.A@300`, `P2:START@320+4` (`p0.`-`p3.`, `.` or `:`); no
prefix is pad 0 as before, and warp tokens stay pad 0. A pad is connected and idle (the front end
counts `DeviceIsPresent`) once any entry names it. Parser test: `port_input_pad_tests`.

## Area regression sweep (any world, shards, START-free)

    USERDIR=/tmp/opencode/u_a10 tools/sweep_areas.sh            # TAG/SHARDS/DWELL/COUNT to taste
    python3 tools/sweep_report.py '/tmp/opencode/log_<tag>_*.log'
    # every campaign world, 2 games at a time, ~8 min in fast mode:
    MP2_FRAME_LIMIT=0 MP2_TICKS_PER_FRAME=8 USERDIR=... WORLD=all TAG=all SHARDS=2 tools/sweep_areas.sh
    python3 tools/sweep_report.py '/tmp/opencode/log_all_*_*.log'   # ends with a per-world light/dark table

Without `WORLD` the sweep walks the save's world (Temple Grounds, 63 slots) by `MP2_TELEPORT`.
`WORLD=<id|agon|torvus|sanctuary|great temple|temple grounds|MLVL name part|all>` sweeps that world:
`tools/world_areas.py` reads its slot list from the disc (`--list` shows every MLVL), `COUNT`
defaults to its slot count and each shard boots with `MP2_WORLD="<world>,<first slot's MREA>"`.
`AREAS=<first>:<count> SHARD=0` re-runs one slice. Temple Grounds' ending starts at slot 53
(`game_end_part3`): the game hands over to 56 (`game_end_part4`, the only area with the credits
script) and the credits end a shard, so 54, 55 and 57-62 need their own `AREAS=` runs
(`54:2`, `57:1`, `59:4`). `COUNT=53` sweeps slots 0-52, everything before the ending. Slots 47 and 50
(`game_end_part1/2`) are ordinary walks, no credits; 58 (`game_end_part5`) stops on its save prompt.

Parallel software-Vulkan shards can lose the device; confirm verdicts with lone runs.

## Booting into a world and an area

`MP2_WORLD` and `MP2_TELEPORT` take a hex asset id or a name, but they share one slot, and
`MP2_TELEPORT` is applied second, so it wins - and with the fast harness the separate teleport is
served *before* the world is loaded, so the area is looked up in the old world and the run ends
with "[warp] area -1 does not exist in this world". **Give the world and the area in one
request**: `MP2_WORLD="1baa96c2,2735236b"` (the world, a comma, the area) or
`MP2_WORLD="42b935e4,01_sand_entrance_dark"` (the area's MLVL name). The area can **not** be a
slot index here: `MP2_WORLD="863fcd72,7"` resolves `7` as asset id 00000007 and lands in slot 0 with
no error (measured 2026-10-23); `tools/world_areas.py <world>` gives each slot's MREA id.

**Direct boot** (default for a world+area `MP2_WORLD`): `PortSkipFrontEnd::StartGame` writes the world
and area into the game state before any world loads (`PortWarp::DirectBoot`), so the save's world is
never loaded and there is no 120-update wait or restart. The update count then starts at 120
(`[harness] direct boot: the update count starts at 120`), so `MP2_FRAMES`, `@f`, `frame`, `at` and
`MP2_INPUT` keep their meaning and gameplay is still frame 240. One difference: on a new-game card
(the 0% `mi_u`) the first spawn now happens at the destination, so the player gets that spawn
point's inventory instead of the save world's start loadout (ing area: power/dark/light beams, 6
tanks, 150 ammo; agon54: the start loadout). `MP2_DIRECT_BOOT=0` restores the old route and loadout;
`MP2_TELEPORT`, and a request that names no area of the world, always take it.

Startup, agon54, 600 updates (2026-10-23): capped 17.8 -> 10.0 s wall per run (gameplay ~11.7 ->
3.6 s after launch); fast 5.5 -> 2.0 s per run (10-run loop 55 -> 20 s). Aurora/Dawn init (~1.3 s)
is the floor. A save-state start was rejected: `port_savestate.cpp` states are a CGameState blob +
a world restart + room restore, not a memory image, so they reload the area from disc and can't
beat a direct boot.

## Batch smoke (every family, in parallel)

`tools/smoke/run.sh [--build <dir>] [--out <dir>] [--jobs N] [family ...]` boots every family area
(one game each, on its own X display, console port, card copy and cache dir) and drives it with the
console scripts in `tools/smoke/cmd_*.txt`; `--list` prints the family table. Results land in
`/tmp/opencode/smoke-<git hash>` by default (the card copied per family is `--save`, default
`/tmp/opencode/mi_u`), and `tools/smoke/compare.py <new out> <old out>` prints exit, caught,
reach-stub, stand-in and gameplay seconds per family, failing when a family regressed.
The generic console.py families run in fast mode by default (`MP2_FRAME_LIMIT=0 MP2_TICKS_PER_FRAME=8`,
~2-3 s each instead of ~45 s); ing, ep and darksamus (wall-clock drivers) stay capped and bound the
batch (~4.6 min, ing ~266 s), and so do the 16 `NC` families in `run.sh`'s `declare -A NC` table
(darktrooper, bacteriaswarm, tryclops, octapede, spa, wallwalker, spor, scriptgui, pssm, pbug, chog,
plac, atmb, shrd, fsw and rubk): their fast and capped transcripts differ, so they stay capped (~40-50
s each) - fsm state/timing (darktrooper, tryclops, pbug), object count or ids (bacteriaswarm 3 vs 1,
octapede 5 vs 6, pssm's 23 vs 17 boids, fsw 6 vs 2, rubk, plac's hp checks), a layer the fast stream
does not load (spa's layer 10, atmb's layer 0, wallwalker's trailing layer 3), spor's run too short,
scriptgui's Gui widget active vs inactive (its state follows the update rate), and chog's hp/fsm and
shrd's kill behaviour.
`MP2_CONSOLE_WAIT=120` (`tools/smoke/fam.sh`) holds the boot until console.py connects, which retired
the 25 thin-margin families that had been capped only because their unpaced updates ended before the
connect (emp1, ingswarm, amt, wisptentacle, isbg, kral, stod, szone, rptl, drkc, plct, spp, ming,
snak, eyeb, kroc, iss, ibs, puff, cball, fobj, skyr, isg, atma, mree); they run fast now.
`--capped` or `SMOKE_FAST=0` runs everything capped.
Forty-six of the 80 families in `run.sh`'s table keep a checked module's actors loading: grenchler
(Torvus 01_swamp_entrance), grenguard (the Grapple Guardian's 08_swamp_grappleguardian_dark),
flyingpirate (Agon 02_sand_sandfalls), mediuming (area 46), metroid (Agon 08_sand_casualpirates),
sporb (Torvus 0e_swamp_hall), blogg (Torvus 07_swamp_shaft), swarm (Agon 01_sand_entrance), gunturret
(Agon 0i_sand_hall), rezbit (Sanctuary 02_cliff_reactorcore), parasite (Agon 02_sand_sandfalls layer
9), lumite (Agon 0a_sand_hall), shrieker (Torvus 02_swamp_greatbridge), ingswarm (Temple Grounds
10_temple_pathofeyes), darktrooper (Temple Grounds 04_temple_unknownenemies), bacteriaswarm (Temple
Grounds 04_temple_unknownenemies_dark), tryclops (Sanctuary Fortress 0f_cliff_hall), wisptentacle
(Temple Grounds 0t_temple_hall_dark), octapede (Sanctuary Fortress 0p_cliff_hall), amt (Agon
06_sand_alamofront), dbar (Agon 08_sand_casualpirates, whose fifteen barriers no area places: the
family spawns them), sjguard (Agon 02_sand_sandfalls_dark), fishcloud (Temple Grounds
06_temple_sacredpath), spa (Agon 03_sand_temple_dark), wallwalker (Sanctuary Fortress 09_cliff_temple
layer 3), isbg (Sanctuary Fortress 06_cliff_spiderballguardian layer 13), spp (Temple Grounds dark
01_temple_landingsite_dark), mysf (Temple Grounds 0e_temple_hall), spor (Torvus dark
0d_swamp_hall_dark), scriptgui (the FrontEnd world 69802220, which is the one family that needs the
front end and so sets `SKIP_FRONTEND=0` in `run.sh`'s per-family environment table), kral (Temple
Grounds 0b_temple_hall), gbug (Great Temple 0i_temple_hall), pssm (Torvus Bog 0g_swamp_hall), stod
(Torvus Bog dark 09_swamp_catacombs_dark), pbug (Agon Wastes 0b_sand_hall), szone (Agon Wastes dark
02_sand_sandfalls_dark), splinter (Temple Grounds 02_temple_exploration), rptl (Temple Grounds
12_temple_finalgate), chog (Agon Wastes 08_sand_casualpirates, which spawns two ChozoGhosts with a
borrowed SpacePirate body and drives every state), drkc (Temple Grounds dark 06_temple_sacredpath_dark),
plct (Agon Wastes 05_portalpuzzle), atma (Great Temple 0l_temple_hall, two AtomicAlphas), atmb
(Sanctuary Fortress 0b_cliff_hall layer 3, AtomicBetas), fsw (Agon Wastes 0e_sand_hall, FlyerSwarms),
iss (Temple Grounds 10_temple_pathofeyes, IngSnatchingSwarm) and isg (Sanctuary Fortress
06_cliff_spiderballguardian layer 13, IngSpiderballGuardian)
boot the area the module lanes' coverage driver used and repeat its console steps
(`tools/cov/specs/*.con`, `tools/drivers/`), with a kill near the end - the console `kill` where a
weapon cannot take the actor (the Sporb base's vulnerability, the Grenguard's reflected weapons),
and for the swarm, which has no Death of its own (`CSwarmBasics` is a `CActor`), charged shots and
the driver's `Deactivate`/`Delete` teardown.
gunturret is not the `turret` family (that one runs M06_ShootingGallery, the player's own turret): it
fights the Luminoth bases u043d/u0423 with their tops, forces the base FSM states and the top's
PowerDown/PowerUp, and kills a top; rezbit wakes e0005016d with `send Activate`, walks its HP down
through BecomeDeRezzed/BecomeRezzed and reachability attacks and breaks its shield; parasite loads
layer 9 for the three Brizgees u05e5/u05d9/u05d6 and their shell collision actors, takes the shells'
beam hits, walks the state machine (GetUp, TelegraphAttack, PathFind, TargetPlayer, Crouch - never
Generate/Attack/Retreat/Halt/Jump, states a Brizgee's FSM never enters and whose body cmds its PAS
cannot play: the console drops such a cmd and says so since brz1; retail plays anim -1 into
CAnimData::BuildAnimationTree, unchecked at 0x80029BAC), kills one with the
console and one with damage; lumite wakes one of the four Lumites hopping through 0a_sand_hall,
walks Turn/SmallShot/BigShot/Taunt/Hop, takes a beam hit and a missile hit and finishes it with
lethal damage (never `fsm Dead`: a lethal Lumite is deleted outright, so Dead only runs forced);
shrieker walks up to the first of the three buried Shriekers on 02_swamp_greatbridge (it unburied
from Buried to TargetPlayer on its own), forces ShriekAttack/Melee/Dodge/Patrol/TargetPlayer, fires
a missile into the deflection field, damages it and kills it, leaving the second one buried as the
contrast; ingswarm wakes the three Space Pirates and fires the area's GENR `e000e018c` with
`SetToZero` so the Ing snatching swarm `e000e018b` pours out (it flies its arc at the pirates),
kills the pirates, generates again so the swarm takes the player, takes a 500 hit for the Damage
arm and the IsDead -> Dead teardown (it has no `fsm`/`kill`: `CIngSnatchingSwarm` is a `CActor`, so
its states stay the coverage driver's); darktrooper loads layers 2 and 3 for the three Dark
Troopers u049d/u0488/u0487, wakes and alerts them, walks Pursue/SmallShotAttack/MissileAttack/
MeleeAttack, takes a small-shot hit with god mode off (99 -> 92 health), damages them with power
and missile and kills all three (power, missile, dark burst); bacteriaswarm stands the player in the
dark side room's three Ingstorms and fires every beam at BSWM `e003d0023`, scans it with the scan
visor (its orbit lock prints a live boid position, and the locked boid follows the player walking
through the boid band with god mode off - it takes no damage: a boid box never overlaps the player,
and the swarm carries a `CDamageInfo` but no `CHealthInfo`, so `damage`/`kill` refuse it as they
refuse a `CSwarmBasics`), then ends with `Deactivate`/`Activate` and the `Delete` that removes it;
tryclops rides the Cyclops u047b's suck-in with the player in a morph ball on its own floor (the
`struggle` command prints the player's attached actor, `047b` while the vortex holds it and `ffff`
after the throw), forces PathFindEx/Suck/GetUp, then shows the vulnerability (`damage 25 power` is
refused, `obj` keeping hp 5/5, because `GetDamageVulnerability` returns `mPowerBombVulnerability`
while `mVulnerable` is false) and kills it with `damage 2000 powerbomb`; wisptentacle walks a
god-mode player in from the far end of 0t_temple_hall_dark, which takes tentacle e00240036 from
Sleep through Patrol and Search to Attack, stands at the arm end for the contact hit, shoots it
(Withdraw -> Sleep, `orbit` printing the live GetAimPosition), sends `Alert` for Grab/Pull, re-walks
the approach and forces the re-issue and Decrement arms with `statecall` before killing it (its
health is deliberately 1,000,000, `CWispTentacle.cpp:565`, so `damage` cannot show a hit - the state
does); octapede attacks the four wall chains of Sanctuary 0p_cliff_hall (16 segments, `objs
Octapede` printing each one's wall position), takes the shared Damage arms (power 8 -> 7, light ->
6.5, dark 8 -> 6), kills a head with a charged shot (left at hp -42 in fsm Dead), forces `fsm Dead`
on a live head, runs `kill` on a follower, and follows the chain counts down (16 -> 11 -> 4) as
Death, Bounce and Explode take the chains apart, the four chain-tail segments still crawling the
wall at the end; amt aims the AIMT e001d0055 at a `tp`-moving player, takes `mpforce 1` for the
multiplayer telegraph sfx, walks out past its 85-unit maxAttackRange so the 5 s leash reaches Patrol,
and takes two hits there (hp 5/5 -> 3/5); dbar `spawn`s the fifteen barriers e0036ff00..ff0e with
area CMDLs and PARTs and takes every damage arm (a small hit, beam/charged/missile shots, the chunk
lower, the 1 s and the zero delays, a hit with no damager, split-offs hit twice in one tick), the
Reset raise/lower cycle and a Delete while the barrier moves; sjguard fights the Jump Guardian u051f
from under its landing zone through nineteen uncharged, ten charged and ten missile shots
alternating the light and dark beams (hp 1060/1100 -> 0, `fsm Dead`), with the `noise 0`/`noise 6`
listener pair and a `kill` for the Death its own script deletes; fishcloud stands in each of Sacred
Path's three clouds and shoots it (uncharged, charged, missile, and the power and dark beams),
morphs and boosts inside e00340081 for Touch's spawned-morph arm, and deletes the attractor
e00340143 and the cloud; spa drives the two dark-temple suit actors e0024022f and e00240396 through
Activate (LoadSuit), Increment (the suit swap), Left, Deactivate's reset, a Reset while inactive and
a Delete, `objs PlayerActor` counting the suits down; wallwalker loads layer 3 of Sanctuary
09_cliff_temple for the four Mekenobites, shoots e002500f9's leg collision actors u05fc/u05fd with
the power beam and missiles (fsm ShootProjectile/RightLegHitReaction) and then the body (its own HP
is 1: the legs are the target, so nothing here needs `kill`), fires a charged dark shot at
e0025014e and `damage` at e00250156, and wakes the inactive e0025009e with Activate before its
Delete, `obj`/`objs WallWalker` counting four walkers down to three; isbg loads layer 13 of
Sanctuary 06_cliff_spiderballguardian for the Spider Guardian e0014007a, activates it once `objs
IngSpiderballGuardian` lists it, takes one hit per weapon (the power hit 600.0 -> 595.0 into
DamageReaction, the bomb, missile and 50-bomb hits reading 595.0 -> 595.0: console damage is
kSM_Damage, which the stun arm never takes), sends the relays' IM00-IM05, calls EnableEnergyBar and
six BeginNextPuzzleSection, places it in front of the player, kills it (hp 0/600 in fsm Dead) and
deletes it; spp drives the dark landingsite's DarkVisorWatcher e00230084 (it starts inactive, so the
Deactivate is a no-op and the later Activate is what brings it up) through the visor switches it
watches and the ones it does not, `obj`/`objs ScriptPlayerProxy` showing its relay map and the area's
two proxies; mysf drives 0e_temple_hall's three wasps e003100fe/e003100ff/e00310101, which start
inactive (`objs MysteryFlyer` listing all three with their spawn positions before the Activate),
places them around the player, alerts them into FollowAttackPath (hp 6/6, body state 5, each
sending Follow to its own waypoint on Attack), takes a 6 -> 5 knock-back on e00310101 (body state
6, hp 5/6 from then on), forces Approach (after a `tp` onto the waypoint's line), Dodge, Attack
(body state 18), Hover and GoToPatrol, runs both CalcLineOfSight/ResetAttack codecalls, kills
e00310101 and Deactivates/Deletes the other two, `objs MysteryFlyer` counting the three down to
none - a killed wasp is gone by its next `obj`, so the count is the check; spor drives Torvus dark
0d_swamp_hall_dark's only Phlogus e00150015 (the area loads it active) through the shell steps -
`place` 8 units from the player, the forced InActive and a 1-damage hit (hp 1000000 -> 999999, body
state 7), `touch`, Attack, the five `tp` steps onto its shell, Run, the forced body state 0, then
TurnAround, GetUp and the four diagonal `tp` steps, a `press x` while morphed on it, InActive again
for the press-x-while-hovering arm, Run, the dark, light and power beams and the aimed and missile
shots (hp 999982 by f1720) - with `objs PuddleSpore` listing it at f340 and `0 objects` after the
Delete; scriptgui drives the FrontEnd area's own short-id objects, e0's Unlock first (without it
StartSelectedGame faults on a null save screen), the menu e2ab through Activate/Alert and its
Open/Close/Lock/Unlock and SetToMax/SetToZero/InternalMessage arms plus the four menu pads and the
x/y/z/start presses, and the slider e21c through Activate/Alert, Increment/Decrement, a held right
and left, Deactivate/Activate and its Delete, `objs Gui` counting the area's 133 script objects down
to 132; kral drives Temple Grounds 0b_temple_hall's two Kralees e00100090/e0010008f through `place`
two units from the player, Increment/Decrement (WarpOut and the WarpIn that follows), the dark, echo
and combat visors and `kill` on e0010008f (the driver's `setarea` step is left out: area 0 is not
loaded there, so it could only answer an error), `objs Kralee` counting the two down to one - and gbug drives Great
Temple 0i_temple_hall's three Glowbugs e000500f0/f8/f9 through Create/Activate, the 6/12/3/20/3-unit
`place` window, a beam and a missile that kill e000500f8 (hp 30 -> 28 -> -2/30, fsm Dead, removed by its own
DeathDelete: the late checks are `obj` reads and the count, not an `obj` of a dead bug) and the
repeated `scan` of e000500f9 (orbit locked, visor flags f), then Deactivate/Delete it, `objs Glowbug`
counting three down to none. The post-scan shots at e000500f0 now hit: `visor combat` exits the scan's
soft pause, and the beam helper aims from eye height rather than through the floor from the feet.
The family checks the scan state, beam damage and missile kill before DeathDelete removes the bug.
pssm drives Torvus Bog 0g_swamp_hall's single PlantScarabSwarm e001e00b6 through the
boids launching, the attack animation and grenade drop, the shared-model render and the launched-boid
touch, the `tp` steps around and behind the swarm, the uncharged and charged shots and the missile
(hp 2/2 -> 0/2), `Deactivate`/`Activate`, and the `Delete` (`objs PlantScarabSwarm` counting one at
f340 and zero at f2960), with `obj` reads (hp, active/gen/pending, boid counts 23/50 -> 0/50) around
them; the swarm generates boids per update, so the family stays capped (NC). stod drives Torvus Bog
dark 09_swamp_catacombs_dark's two StoneToads e00410048/e00410066 (both hp 5, asleep) through the
safe zone waking e00410048 (ReactToSafeZone -> WakeUp -> MoveAway -> Wait), the direct hit and beam
shot on e00410066 (ReactToWeapon), the zone's Deactivate plus Decrement (MoveBack -> GoToSleep ->
Sleep), the missile and Increment/Decrement/AIUpdateDisabled messages, Deactivate/Activate, `kill`
on e00410066 and a final `obj`/`objs StoneToad`, with `obj` reads (hp, fsm, body state, message
map) around them; its 1500 updates finish in ~0.5 s, before console.py connects, so the family stays
capped (NC). pbug drives Agon Wastes 0b_sand_hall's three PillBugs e00280088/e00280089/e0028008a
(planar 1 looping paths) through a bug placed at the player (InDetectionRange -> FastPatrol), a
touch (CollidedWith -> HitPlayer -> Pause -> AnimOver), the power/dark/light beam and missile
shots (all resisted), a real morph-ball bomb laid with the bug put on it (ResistedDamage immune
-> Bombed -> Fall -> Landed -> Injured -> Delay -> HasReturnPath false -> Wander ->
CloseToPath -> Patrol), shots and bombs at the injured/wandering bug, a forced Turn (every
shipped path loops, so PathOver never sets), a bomb kill, and `objs PillBug` counting three at
f320 and zero at f3400, with `obj` reads (hp 25/25, fsm FastPatrol, body state 5, message map)
around them. The PillBug fsm reads follow the update rate, so the family stays capped (NC). szone drives Agon Wastes dark 02_sand_sandfalls_dark's safe zones u0474/u04d4/u04be/u04b0 and
their crystals u0473/u04d3/u04bf/u04b1 through beam shots (light/dark/power/annihilator, charged
and missile), the zone's Increment/Decrement and InternalMessage0-3, Deactivate/Activate/AreaLoaded,
the crystal's InternalMessage0-6 and Deactivate/Activate, two damage hits per crystal, the `noise`
listener and the three Activate/Deactivate sends, then the zone's Delete, with `objs SafeZone`
counting the zones at f340 and after the Delete at f2300, and `obj` reads (hp/fsm/body/message map)
around them. The shot ids and late `objs SafeZone` listings follow the update rate, so the
family stays capped (NC). Its added power-shot check forces crystal u04d3 Entangled after the gun
switch settles: 0.5/3-unit shots give hp 5 -> 3 -> 1; re-entangle and a clear 6-unit shot gives 5 -> 3.
Other crystal beam reactions reset HP via script connections, so unchanged HP alone is not a miss.
splinter wakes Temple Grounds 02_temple_exploration's three Splinters
u055b/u055c/u057c (inactive at load), alerts them, takes the 6 -> 4 -> -6 hp steps on u055b/u055c
and the missile hit on u057c, and `objs Splinter` counts three at f340 and zero at f3100, with `obj`
reads of u055b/u057c (hp/fsm/body/message map) at f1200. rptl drives Temple Grounds 12_temple_finalgate's rift portal e00070073 through Activate,
Increment (line animation until it opens), beam/missile/charged shots through its projectile box,
AreaLoaded (rebuilds the box), Decrement (stops the animations) and Deactivate (hides it), the
player stepping close (the portal overfills the screen), the centre turned off-screen (copy-rect
clamp), looking away (culled), dropping from above and laying morph-ball bombs in the projectile
box (a kMT_Projectile that is not a CGameProjectile), with `objs RiftPortal` before and after.
The portal's projectile box and late `objs RiftPortal` listings follow the update rate, so the
family stays capped (NC). chog spawns two ChozoGhosts (e0036ff00/e0036ff01) in Agon Wastes
08_sand_casualpirates with a borrowed SpacePirate body and AFSM 5652a12f, drives every state
(InActive -> Growth -> Generate -> WallDetach -> Run -> SelectTarget -> Attack -> Land ->
Shuffle -> Taunt -> Lurk -> Hurled -> Deactivate -> Start), the visors, touches, charged and
missile shots, damage and kills, then spawns a third ghost, with `objs ChozoGhost` before the
steps and at the end. drkc drives Temple Grounds dark 06_temple_sacredpath_dark's two Dark Commandos u051a and u041c through
Activate, placement, damage and `obj` reads, with `objs DarkCommando` listing both at f200 and f4000.
plac drives Agon Wastes 00_mapstation_sand's two inactive PlayerActors u065b and u0446 through Activate, Deactivate and `obj` reads, with `objs ScriptPlayerActor` listing both at f200 and f1800.
ming boots Agon Wastes 0a_sand_hall_dark with the two MinorIngs u0455/u0432 (hp 20/20), Activates the inactive one, forces Patrol/PointPathFind/Dead, shoots one (20 -> 15 hp), damages it (15 -> 13), deletes it and closes with `objs MinorIng` (1 object left); snak boots Torvus Bog 01_swamp_entrance with SnakeWeedSwarm u0481 (41 weeds), Create/Activates it, places it 4 units from the player, shoots it twice (beam + charged, ScareSnakeWeeds), Deactivates/Activates and Deletes it (0 objects at the end). Both are NC (fast mode would finish before console.py connects).
eyeb drives Temple Grounds dark 0q_temple_hall_dark's four Corrupted Sentreyes u047a/u0476/u0452/u041d:
activate and place u041d, force Active/Cover/Flinch, send Alert, shoot it (power beam and missile),
kill it, with `objs EyeBall` counting four down to three. kroc drives Torvus Bog 0h_swamp_hall's three
Krocusses u045b/u0447/u0441: activate and place u045b, force Sniff/Stop, send Increment/Decrement,
damage it (hp 60 -> 50 -> 40) once its wings open, kill it, with `objs Krocuss` counting three down
to two.
shrd drives Torvus Bog 01_swamp_entrance's three Shredders u04e6/u04e3/u04e2 (layer 3): create and
place u04e2, send Alert, force Generate/Lurk, send an XRDG message, damage it (hp 500 -> 500),
kill it and send XDMG to explode it, with `objs Shredder` counting three down to two.
ibs drives Sanctuary 0b_cliff_hall_dark's two IngBlobSwarms u0451/u044c (layer 4): activate u044c,
read its swarm state, damage it (hp 2 -> 1 -> 0), delete it, with `objs IngBlobSwarm` counting two
down to one.
mree wakes three of Agon Wastes 0c_sand_hall's eleven CMetarees (u044d/u044b/u044a): Create drops
them (hp 2/2), u044a is killed at once (11 -> 8 objects), u044b takes a shot, and u044d is forced
to Halt and takes a 1-damage hit (hp 2/2 -> 1/2) before it transitions to Attack, with `objs
Metaree` at f320 and f2200. puff wakes Agon Wastes dark 07_sand_alamoportalobservation_dark's
CPuffer u05d9 (hp 5/5), forces PathFind and Start, takes a 1-damage hit (hp 5/5 -> 4/5) and a
shot (hp 4/5 -> 2/5), then kills it (0 objects), with `objs CPuffer` at f320 and f2200.
rubk drives Sanctuary Fortress 07cliff_gyro's two Rubiks puzzles u05b3/u06c0 (layers 4 and 5):
load both layers, activate both, send InternalMessage0 to both, delete both, with `objs RubiksPuzzle`
counting two down to zero. emp2 drives Great Temple 07_temple_mothlordsanctuary_dark's eleven stage-2
tentacles (layer 3): create u05a1, force Attack/Retracted, damage and shoot it (hp 30 -> 30, unchanged),
kill it, with `objs EmperorIngStage2Tentacle` counting eleven down to ten.
cball drives Agon Wastes 04_sand_mining's two CannonBalls u0561/u04c1: activate u0561, send Create,
delete it, with `objs CannonBall` counting two down to one. fobj drives Agon Wastes 05_portalpuzzle's
single ForgottenObject u0423: activate it, delete it, with `objs ForgottenObject` counting one down to
zero. skyr drives Temple Grounds 03_temple_combattraining's SkyRipple (layer 5) u0589: load layer 5,
activate it, delete it, with `objs SkyRipple` counting one down to zero.
dsbs drives Agon Wastes 10_sand_alamostorage's three DarkSamusBattleStages u073e/u0739/u072f
(layer 6): load layer 6, activate u073e, read it and the Dark Samus u0732, deactivate and reactivate
it, read both again, delete it, with `objs DarkSamusBattleStage` counting three down to two.
ingp drives Agon Wastes 06_sand_alamofront_dark's seven IngPuddles (layer 2): activate them,
delete u0548 (u0526's waypoint) to end its patrol, shoot and damage them (hp 5 -> -6 on u04f7),
deactivate/reactivate u0541/u0513, delete u0517, with `objs IngPuddle` counting seven down to five.
A new family is appended at the end of the `ALL` table in
`tools/smoke/run.sh` (its index is its X display) with the next free console port, and added to
`tools/smoke/compare.py`.

## Crash soak (pre-release)

`tools/mpsoak.py` is the pre-release smoke: it drives the harness through the shipping matrix -- the
user's mods folder and a build with none (`MP2_MODS` at an empty directory), Dawn validation and
robustness back on (`MP2_DAWN_DISABLE=skip_validation,disable_robustness`, which makes any WebGPU
error fatal in game) and the release toggles -- at MSAA 1 and 4, switched inside the same game with
the console's `msaa N`. Every run also passes `MP_GX_CHECK_ARRAYS=1`: aurora then walks each indexed
draw's vertex indices and drops a draw that would read past one of its arrays, logging
`gx: vertex array overrun:` once per array.

    python3 tools/mpsoak.py --evidence-dir build/s-release-<date>/soak
    python3 tools/mpsoak.py --dry-run --evidence-dir /tmp/soak-dry    # the commands only, no game

MP1 has this soak on `tools/mprig.py`, a rig manager that keeps one game alive across a tour of
rooms; this tree has none, so it is built on `tools/run_gameplay.sh` instead: one game per (mode,
toggles), booted straight into the tour's first area (`WORLD=<world>,<area>`, "Booting into a world
and an area"), the other areas reached with the console's `world <world> <area>` and `warp <area>`
(`tools/mp2con.py`), and MSAA switched with `msaa N`, so one process covers both sample counts. The
screenshots come from the display that run's own Xvfb is on (`tools/shot.sh`); it takes display `:97`
and console port `4791` (`--display`/`--port`), away from the smoke suite's (`:760+`, its own ports)
and the coverage runs' (`:1100+`, `501xx`). Every cell first waits for a game slot
(`tools/games_running.sh --wait`) and `GODMODE=1` is on, so a hazard cannot end the tour.

A row FAILs on `port: crashed:`, `[port] caught`, `WebGPU error`, `Device lost:`, an aurora `[fatal]`
line (aurora logs `[level] [module] message`, so `Log.fatal`/`FATAL` starts `[fatal]`), on a warp
that lands nowhere, and on the game process going away; `not supported; skipped`, `[reach-stub` and
`is a port stand-in` lines are `warn`, and the shutdown's `Device lost: Device was destroyed` is a
warn (every run prints it). After a death it writes the whole run log to
`<evidence>/<mode>-<toggles>-msaa<N>-<area>-crash.txt`, restarts the run and carries on with the
next area. Screenshots land in `<evidence>/<mode>-<toggles>-msaa<N>/<NN>-<area>.png`; `summary.tsv`
has a row per (mode, toggles, msaa, area) with `ok`, `warn` or `FAIL` and the first matching log
line, and the exit status is 1 when any row FAILs. A cell's game is stopped with SIGTERM at the end
(the port's own clean-quit path); `--keep` leaves it running and prints its display and port.

The default tour is 12 areas across the five campaign worlds, light and dark (Dark Aether's
atmosphere and safe-zone volumes are what the MSAA/depth-copy path is for): `--rooms` takes
`world:area` pairs, where the world half is anything `tools/world_areas.py` resolves (`agon`,
`torvus`, an MLVL id, part of its name) and the area half is a slot index, an MREA id or (part of)
the area's name in that world. Both halves become ids before they reach the game, so no warp
depends on name resolution; a name that does not resolve, or that matches two areas, lists the
candidates. `--modes`/`--toggles`/`--msaa` narrow the matrix and `--settle` (game updates in an area
before its shot) trades time for a settled picture. Keep areas whose warp lands on a save station out
of the tour: the save prompt pauses the game, a paused game performs no update, and every later warp
then fails as "the game stopped updating". The full matrix takes a few minutes: the warps are the
cost (~2 s in a cached world, ~10 s into a cold one).

## Headless runs

For runs that need no picture (console scripts, logic checks, the smoke): `MP2_HEADLESS=1` (the
game sets `AURORA_HEADLESS=1` and `SDL_VIDEODRIVER=offscreen`), `HEADLESS=1 tools/run_gameplay.sh`
(no Xvfb, `DISPLAY`/`WAYLAND_DISPLAY` unset) or `tools/smoke/run.sh --headless`
(`SMOKE_HEADLESS=1`). Aurora makes Dawn's Null device and no surface, and renders no frame: the
FIFO is still processed (the register state the game reads back), but nothing is drawn, encoded,
submitted or presented. The host input scan is skipped too (no hidapi, no haptics: closing every
`/dev/input/event*` cost ~0.6 s of each boot); the console and `PADSetPortOverride` still drive pads.

What differs from a normal run (render side only; the game logic is the same):
- Screenshots (F12, console `screenshot`, `MP_SMOKE_SHOT`, PNG requests) are refused with a warning.
- Read-backs never land: scan peeks and EFB CPU copies are dropped and `GXPeekZRegion` returns
  false, so the game keeps its own "nothing read yet" values (Splitter beam length at the far
  plane, scan target 0xFF) and `CVisorFlare::UpdateOcclusion` takes its retail `GXCopyTex` path
  (console `status` shows ~30 copies and ~47 draws a frame instead of 1-2).
- No copy destination is ever recorded, so the `status` line's `perf` counters describe recording,
  not rendering.

Measured 2026-10-23 (agon54, one game): Aurora init 0.53 s -> 0.01 s, input probe 0.6 s -> 0,
first game update 5.2 s -> 4.1 s wall. "Gameplay reached" stays ~9.1 s: it is a frame-count wait
at 60 Hz (frame pacing, `CGraphics::EndScene`), which headless does not change. Full smoke
(`--jobs 4`, shared machine): the 18-family suite measured wall 364 s windowed -> 356 s headless,
CPU (user+sys of every game) 193 s -> 121 s; counts, positions and `[warp]`/`[ids]` lines match
within run-to-run noise. The 37-family suite (2026-10-23, `--jobs 2`, octapede added on top of
tryclops and wisptentacle) measured 418 s headless (18:26:15-18:33:13); the 36-family one 416 s
headless on a shared machine with a second full suite running; the 34-family one 314 s `--jobs 2`,
the 33-family one 326 s, the 31-family one (lumite and shrieker added) 269 s at `--jobs 4`. The
wall-clock drivers ing, ep and darksamus still bound it (ing ~266 s), not the family count, and the
three newest families cost ~42 s (tryclops, 2600 frames), ~103 s (wisptentacle, 6200) and ~49 s
(octapede, 3000, capped) on top of it.

## Branch coverage of the port module bodies (gcov)

The decomp lanes prove "every arm of a ported body ran" with gdb breakpoints, one spec per
function (`tools/cov/`). A coverage build answers the same question for *every* body at once: it
compiles the port's REL module bodies (the sources `tools/modules_port.txt` lists) with gcov, and
one ordinary run then reports the branch coverage of all of them.

    T=$PWD/../MetroidPrimePort/build/review-tools/bin      # no system cmake/ninja
    $T/cmake -S . -B build-port-cov -G Ninja -DCMAKE_MAKE_PROGRAM=$T/ninja \
      -DMP_SDK_HEADERS_ONLY=OFF -DMP_PORT_COVERAGE=ON \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_LINKER_TYPE=MOLD
    $T/ninja -C build-port-cov metroid_prime2_port

`MP_PORT_COVERAGE` is off by default and the normal build (`build-port-link`) is unchanged by it:
with the option off no flag is added and no source is compiled differently. The coverage build is
its own directory because a coverage build is not the port (the counters cost time and the code is
instrumented).

A run writes its profile (the `.gcda` files) into its own directory: the run scripts set
`GCOV_PREFIX` (and `GCOV_PREFIX_STRIP`, which drops the build directory's own path components so
the tree under the prefix mirrors the build tree) when the binary is a coverage build, so
concurrent runs do not write over each other. The game flushes the counters however the run ends -
a clean quit, the harness's `timeout` (SIGTERM) or a Ctrl-C (SIGINT) - through the hook in
`platform/port_coverage.cpp`, so a killed run still leaves a profile.

    BUILD=$PWD/build-port-cov tools/smoke/run.sh --build $PWD/build-port-cov --out /tmp/opencode/cov \
      --jobs 4 sandworm
    tools/cov/gcov_report.py $PWD/build-port-cov /tmp/opencode/cov/gcov_sandworm/gcov --module Sandworm

`tools/cov/gcov_report.py <build dir> <gcov prefix dir>... [--module <Name>] [--fn fn_10_1D794]`
prints one row per body: the retail id, the C++ name, the lines hit, the branches taken, the lines
whose branch was never taken, and `ALL-ARMS` when every branch of the body was taken. Several
prefix dirs may be given: their counters are merged, so the arms of a natural run and of a forced
one add up. The id of a body is the `// @ fn_<mod>_<hex>` marker above it (or the id its comment
block names). gdb is still what forces an arm no run reaches (`tools/cov/force.sh`): coverage says
which arm is missing, not how to reach it.
