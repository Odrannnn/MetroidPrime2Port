# Port layer

The port's host layer. It began as the Metroid Prime port's `platform/` and has been reworked for
Echoes; `../PORT_NOTES.md` explains the build shape (`mp_platform`, `mp_port_audio`, the REL
modules). Sections below cover what is non-obvious; the CMake target lists in `../CMakeLists.txt`
are the source of truth for what is built.

## In use

- `compat.h` — force-included ahead of every C++ game translation unit. Restores
  the SDK macros Aurora omits (`AUTO*`, `nofralloc`, `__abs`) and maps the game's
  `triggerL/R` onto Aurora's `TARGET_PC` `PADStatus` members. C++-only.
- `include/dolphin/` — SDK headers Aurora omits: `arq.h`, `gba.h`, `PPCArch.h`,
  `thp/`, and `gx/GXShims.h` (GX entry points Aurora does not provide).
- `shims.cpp`, `sdk_stubs.cpp`, `glibc_compat.c` — GX/SI/PAD/PPCArch shims, stubs
  for the SDK entry points Aurora lacks (17 of the 20 the compiled game needs),
  and the glibc floor. Built as `mp_platform`, so they stay compiled and verified
  against Aurora's headers even while the game cannot link.
- `entry.cpp`, `include/port_entry.h` — which disc the port accepts and how it
  finds the image. Game-free, so it is covered by `port_entry_tests`.
- `main.cpp` — the process entry point: Aurora up, disc mounted and checked,
  `InvokeCMain` called, platform down. It drives the decompilation's `InvokeCMain` seam and
  builds the OS context and memory system that seam takes.
- `rel.cpp`, `include/port_rel.h` — the REL module runtime (arena, loader, linker,
  registry), tested against a module extracted from an owned disc.
- `port_events.cpp` — the game loop's per-iteration Aurora poll (built by
  `mp_port_audio`, since it needs SDL). It installs the port's keyboard map and
  drives pad 0 from `MP2_INPUT` scripts.
- `port_warp.cpp`, `port_harness.cpp` — the warp request slot and the test harness's
  run length (`MP2_FRAMES`), both built by `mp_port_audio` and served by the game in
  `CStateManager`; see "Warps and the debug overlay" and "Test runs: skipping the
  front end" below.

## Input

Aurora reads a real controller by itself, but its keyboard map is empty
(`g_defaultKeys` is all `PAD_KEY_INVALID`), so pad 0 comes from the Prime port's input
engine instead (`platform/input/`): `port_input_devices.cpp` reads the keyboard, mouse,
pad and touch through one profile and hands Aurora the result. The built-in keyboard
layout is `MakeKeyboardProfile` there, the Prime port's:

| Key | Pad | Key | Pad |
| --- | --- | --- | --- |
| WASD | main stick | X / Z | A / B |
| IJKL | C-stick | C / V | X / Y |
| Q / E | L / R (also the analog triggers) | F | Z |
| arrows | D-pad | Enter | Start |

Keyboard state merges with a connected controller's and with `MP2_INPUT`'s virtual
pad, so neither replaces the other.

Your changes are saved in `controls.toml`, in the same user directory as `touch.cfg`
(`MP2_USER_PATH`, else Aurora's prefer-path user dir); a missing file means the built-in
layout. `keybinds.cfg` is no longer read.

F1 > Controls is the Prime port's Controls page (`platform/port_controls.cpp` and
`platform/input/port_input_remap_ui.cpp`): the stick deadzones, then the remap page, where
clicking a row's input records the next key, button or axis, and the Reset popup holds the
keyboard presets (Classic, Mouse & keyboard) and the controller presets (GameCube,
Remastered, Modern, Southpaw).

`tools/keytap.py <display> <key> [...]` puts a real key press on an X display (XTEST), which is
the only way to test the keyboard map headless: `MP2_INPUT` and the console's `press` write the
*virtual* pad, past it. The script unsets `XAUTHORITY`, since a bare Xvfb needs none.

### `MP2_INPUT` frames

**Pads 1-3:** prefix an entry with `p1.`, `p2.` or `p3.` (or `P2:`) to drive that pad instead
(`p1.A@300,p2.START@320+4`); no prefix is pad 0. A pad is connected and idle once an entry names
it. Warp tokens are pad 0 only. Needed for Trilogy/GC multiplayer (the front end counts pads).

`MP2_INPUT="BUTTON@frame[+frames],..."` holds a button (or a stick: `SUP`, `SLEFT`, `CRIGHT`, ...)
on pad 0 from that frame, and a warp token (`NEXTAREA`, `PREVAREA`, `NEXTWORLD`, `PREVWORLD`,
`KILL`, `HEAL`, `LAYER<layer>`, `ATDOOR`, `DOOR`, `STATION`, `WORLDPORTAL`) queues a `PortWarp`
request at its frame instead. **A frame is a game update** (`CStateManager::Update`, the counter
`MP2_FRAMES` uses), not a loop iteration:

| Frame | What it means |
| --- | --- |
| `0`, `1` | the game's first update: the pad state is set before it and the game starts with it |
| `N` | game update `N`; `[input] <TOKEN> at frame N (S s)` prints as the pump feeding update N |
| `N+4` | a hold lasts that many game updates, so `B@3000+4` is down for updates 3000..3003 |

The `MP2_FRAMES` limit counts the same thing, so a script's frames and its run length are one axis.
Counting loop iterations instead (what the port did before) put every early token in the boot - the
game's first update is ~2000 iterations and ~34 s in (`[harness] the first game update is at ...`) -
so a token framed against the game's updates landed in the wrong place and one aimed at the start was
lost entirely (`docs/STATUS.md` wall 3). A token whose frame has already gone by fires at the first
frame the script sees, so nothing is dropped. Before the first update the frame stays at 1, which
means a screen that precedes every game update - the front end - cannot be driven by frame:
`MP2_SKIP_FRONTEND` is what gets a run past it. Once the game has updated, an iteration it spends
*not* updating still advances the frame by one (a HUD memo pauses the game, and a paused game
performs no `CStateManager::Update`), so a later token can still press the button that dismisses the
screen - `tools/sweep_areas.sh`'s periodic `B` depends on that.

A button token presses once, where its frame is: `START@300+200` is down from update 300 and the
game's edge detector sees the press there (measured: it opens the pause menu and the run stalls,
`[harness] no game update for 30.0 s`). A token at frame 0 or 1 is down from the game's *first*
update, which the boot flow has no use for - the press happens where nothing is listening - so a
screen later in the run wants a token at its own update.

## Mouse look

`MP2_MOUSE_AIM=1` hands the aim to the mouse; `MP2_MOUSE_SENS` sets radians per
pixel (default 0.0035, as in the Prime port) and `MP2_MOUSE_INVERT_X`/`_Y` flip an
axis. The platform side is `port_mouse.cpp` (built by `mp_port_audio`, because it
needs SDL): it owns SDL's relative capture and the aim angles in
`include/port_mouse.h`. The game side is a `#ifdef TARGET_PC` hook in two files,
the same shape as the Prime port's:

- `CPlayer::UpdateMouseAim` turns the player body to the mouse yaw, so movement
  stays body-relative and the gun (which follows the camera) points where the
  crosshair is. It runs in `Think`, after `ProcessInput`, whose stick turn would
  otherwise win the frame. Nothing there sets `mInFreeLook`: that flag stops
  movement and changes gun animations, and the mouse should not pretend that R is
  held. `MouseAimAllowed` gates it - first person, playing, unmorphed, unfrozen,
  no lock-on, no cinematic - and while something else owns the view the aim
  tracks the camera direction instead, so re-engaging does not snap.
- `CFirstPersonCamera::UpdateTransform` uses the aim for the view direction,
  bypassing the free-look clamps (retail limits how far the C-stick travel may
  go), skips the jump/fall pitch-down, and rebuilds the gun-follow basis directly
  from `lookDir` so pixel deltas get no controller-style easing.

Verified on Xvfb by injecting relative X pointer motion (`/tmp/xkey mouse 400 0`):
the view turned, pitch followed the mouse down, and walking afterwards went in the
new facing. It is opt-in until it has had more play time; a pad or the keyboard map
stays the default.

## Warps and the debug overlay

`port_warp.cpp` (with `include/port_warp.h`) is a one-slot request queue; the game
serves one request per update in `CStateManager::PortWarpTick` (`#ifdef TARGET_PC`),
because only the game can read the resource factory, the world and the memory
card's world table:

- an **area warp** does what an area load does - `TravelToArea(kATT_SkipAdjacent)`,
  the `WorldLoaded` script messages, the first active spawn point through
  `SpawnPlayer`, `ResetCameras` - so it lands the player where the area expects;
- a **world warp** is `CScriptWorldTeleporter`'s restart: pause the outgoing world's
  loading, `gpGameState->SetCurrentWorldId` + `SetDesiredAreaAssetId`,
  `gpMain->SetRestartMode(kRM_None)`, `QuitGame()`, and CMainFlow loads the new world.

Requests come from `MP2_WORLD="<world>[,<area>]"` and `MP2_TELEPORT="<area>"` at
start-up, the F5/F6 (area) and F7/F8 (world) hotkeys, the `MP2_INPUT` tokens
`NEXTAREA`/`PREVAREA`/`NEXTWORLD`/`PREVWORLD`, and the F1 overlay's buttons. World
and area text is an asset name or a hex asset id; an area may also be its MLVL name (`0b_sand_hall`), a
world a part of its MLVL resource name (`sand_world`), and `MP2_WORLD=0b_sand_hall` finds the world that has
the area (display names such as "Agon Wastes" are not resolved). The same slot also carries the gameplay-test
tokens `KILL` (the state manager's own kill, so a run reaches death and respawn), `HEAL`
(refill the player's energy, so a sweep survives a damaging atmosphere) and `LAYER<layer>`
(activate that layer of the current area, which is how a module whose actors live in a layer
other than 0 - every one of ElitePirate's five areas, for instance - becomes visible at all:
`ActivateLayerDynamic` runs the layer's script objects, and the layer's data has to stream in
first, so the request is finished over the following frames).

### What a transition has to get right

Walking through a doorway works like this in the game, and a warp has to follow it:

1. The player touches a `CScriptDock` (`Touch`), which starts the destination area
   loading and moves the dock's state machine to `kDS_EnterNextArea`.
2. When the player crosses the dock's plane **and the destination area reports
   `IsLoaded()`**, the dock calls `SetCurrentAreaId(nextArea)`. That also moves
   `mNextAreaId`, which is what the world update travels to every frame.
3. `CStateManager::Update` ends with `TravelToArea(mNextAreaId, kATT_LoadAdjacent)`,
   so the destination becomes the current area and the previous one is queued for
   unloading.

A warp that changes the area id *before* the area is loaded breaks two ways: the
update calls `Area(GetNextAreaId())->UpdateDocks`, which segfaults on an area that is
still streaming, and the per-frame travel then drags the world back to the old area
while the warp drags it forward, which makes the loop crawl. So `PortWarpToArea`
starts the load (`ScheduleAreaToLoad` + `StartStreamIn` every frame until
`ScheduleAreaToLoad` reports done, exactly like `CStateManager::InitializeState`)
and `PortWarpSettle` moves the player only once `IsLoaded()`.

The area's script objects allocate their object ids *as* the stream runs, so the free-id
floor `PortWarpSettle` applies only guards **starting** a stream: a warp that withholds
`StartStreamIn` from a stream already under way stalls the load forever and keeps the ids
the area has already taken (a walk of world `1baa96c2` stopped there with 600 of the 1024
ids held by a half-loaded area). Once a stream has started, the only thing that gives it
room is dropping the areas the warp left behind, so `PortWarpSettle` does that while the
stream runs rather than only before it starts.

Two more things the warp exposed, both fixed in the game files:

- `CAutoMapper::GetDesiredMiniMapCameraDistance` dereferenced `GetMapArea()` results
  that are null until an area's map data is resident; a warp can arrive first, so the
  port skips areas whose map is not there yet.
- An area slot with an invalid MREA is not a real area (a world can have many more
  slots than areas); `CycleArea` skips them, and `PortWarpTick` prints the world's
  area list once so a sweep can see which slots are real.

`ATDOOR` (F9) stands the player in the nearest doorway facing through it, and `DOOR`
stands them in front of the nearest door: with `SUP` to walk and `A` to shoot, those
two tokens exercise the real door-then-dock path instead of the warp's shortcut.

`port_overlay.cpp` is the F1 window (ImGui, which Aurora sets up; `mp_port_audio`
links `aurora::core` for its headers). It draws inside the open Aurora frame from
`DolphinCGraphics.cpp`, and every control in it is wired to something that exists:
the warp buttons, mouse aim with inversion and sensitivity, the keyboard map, and
the current world/area, which `CStateManager::Update` publishes every update. While
it is open the cursor belongs to the window (`PortMouse::SetUiActive`). Otherwise an
uncaptured cursor hides after 2 s without mouse input (`PortMouse::UpdateIdleCursor`).

## Test runs: skipping the front end

`port_harness.cpp` (with `include/port_harness.h`) is the test harness's run length,
and the game-side half of it lives in `src/MetroidPrime/PortSkipFrontEnd.cpp`:

- **`MP2_GODMODE=1`** (also F1 > Cheats) makes the player take no damage:
  `CStateManager::ApplyLocalDamage` returns early for a `CPlayer` and nothing else, so
  enemies still die. A death stops game updates (the lose-game screen), so sweeps set it
  (`tools/sweep_areas.sh`, off with `GODMODE=0`; `tools/run_gameplay.sh` takes `GODMODE=1`).
  With it off, `[player] killed ...` names the death. Never saved.
- **`MP2_SKIP_FRONTEND=1`** boots straight into the memory card's save instead of
  playing the front end's menu. It reads the save itself through
  `CMemoryCardDriver` (the same read the file select panel performs through
  `CSaveGameScreen::StartGame`) and leaves it where `CMain::StreamNewGameState`
  expects it, then starts the game the way retail's single-player start does.
  `MP2_SLOT=<0|1|2>` picks the slot; without it the slot the card's own header
  points at is used, falling back to the first slot that has a save. The log's
  `[skip]` lines name every slot and say where the run is. Any failure - no card, no
  file, an empty slot, a read that never settles - falls back to the front end with
  a line saying so, so a skipped run is never a stuck one.
  It also dismisses the boot's splash chain, which otherwise blocks the flow until a
  button is pressed or the health-warning screen times out: measured, the flow
  reached `CPreFrontEnd` at 56.6 s without a button press and at 1.1 s with the
  splash dismissed, and the save's first area was loaded and reached at ~15-20 s
  where the menu path needs ~65 s.
- **`MP2_FRAMES=<n>`** ends a run cleanly once n *game* updates (`CStateManager::Update`)
  have happened: the game loop takes its own quit path, Aurora shuts down and the
  process exits 0, so a log's last line is the run's verdict instead of the script's
  `timeout` (which stays in place as the backstop for a run that never gets there).
  `[harness]` lines report the limit and the world/area the run ended in.
- **A script's frames are game updates too** (see "Input" above): frame `N` is the pad state game
  update `N` runs on, so a token at frame 0 or 1 is the state the game starts with, and a script's
  tokens and its `MP2_FRAMES` limit are one axis.
- **A script must not press `START` while in game.** It opens the pause menu, and a
  paused game calls no `CStateManager::Update`, so the frame limit is never reached
  and only the script's `timeout` ends the run. The harness reports that case as
  `[harness] no game update for <n> s (... updates so far, limit ...)`, once per
  stall.

- **Quick resume** (setting Save states > Quick resume, `MP2_QUICK_RESUME`, default on; `MP2_RESUME=0|1`
  is the test gate, default 1, and an `MP2_FRAMES` run is inactive unless `MP2_RESUME=1`). MP1's
  `resume.mpss`, ported: `<user>/savestates/resume.mp2ss` is a save state of the running game in an
  `MPRS` container (file index, card serial, save time). A snapshot is written 2 s into a room, again
  every 30 s, and at quit or backgrounding, atomically; none while loading, dead, in a cutscene or with
  a screen about to open (`[savestate] resume: no snapshot - <why>`, logged once per change). A
  `running` file beside it marks a live run: if it is there at launch the last run crashed, and after a
  resume it reads `resumed <savedAt>`, so a crash before the next snapshot makes the prompt say "again"
  and default to No. The next launch shows "Resume where you left off?" over the splash
  (`PortSaveState::ResumePromptTick`, `DrawResumePrompt`; pad left/right + A/Start, or click). Yes boots
  the card file the snapshot came from (`PortSkipFrontEnd::ArmFromResume`) and loads the snapshot;
  No boots normally. The file stays until the next snapshot replaces it. `MP2_SKIP_FRONTEND` runs take
  it with no prompt; `MP2_WORLD`/`MP2_TELEPORT` and `MP2_SKIP_FRONTEND` skip the prompt.
  `MP2_RESUME_ANSWER=yes|no` answers it after ~3 s (tests). Console: `resume [status|save|load|clear]`.
  Code: `port_savestate.cpp`, pure logic in `port_resume_logic.h`.

`tools/run_gameplay.sh` is the wrapper the port's tests use for all of the above: it passes
`MP2_SKIP_FRONTEND`, an optional `MP2_WORLD` restart, an `MP2_INPUT` script read from a file, the
frame limit and the user/cache paths, and it warns about the two mistakes that quietly waste a run -
a user dir without the memory card (the run then plays the front end) and a script that presses
`START` (the pause menu stops the game updating, so the frame limit is never reached). Its log's last
line is the run's `exit=`. `tools/shot.sh <display> <out.png> [secs]` captures a frame of the run's
display, which is how the log's phase times turn into a picture of what the game was doing.

A load test is the two together, e.g.

    MP2_SKIP_FRONTEND=1 MP2_WORLD=1baa96c2,b666b655 MP2_FRAMES=800
    MP2_INPUT="LAYER3@4200+4" ./metroid_prime2_port

which now costs tens of seconds rather than a menu's worth of scripted button
presses. `[harness]`/`[skip]` lines carry the wall seconds since the loop's first
iteration, so a log says how long each phase took.

## Texture replacements and button prompts

`port_textures.cpp` and `port_prompts.cpp` are the Prime 1 port's, started from `main.cpp` after
Aurora and polled once per iteration from `PortPumpEvents`. Replacements are Aurora packs
(`aurora/texture.hpp`): files named after the texture's hash, in priority order built-in set (0),
user pack (1), binding icons (2).

- `MP2_TEXTURES=<dir>` — the built-in set, overriding the executable's own. Desktop builds
  (`MP_EMBED_RESOURCES`, default ON except Android; `tools/embed_resources.py` generates
  `port_embedded_data.cpp` from `textures/`) carry that set inside the executable and ignore a stale
  `textures` folder beside it; with `-DMP_EMBED_RESOURCES=OFF` (and on Android) the default is a
  `textures` folder beside the executable (on Android, in the user directory), used only if it exists. A `<dir>/<device>` subfolder
  (`keyboard`, `xbox`, `playstation`, `switch`, `gamecube`, `standard`) is used when present;
  `<dir>` itself only when it holds no device folders at all.
- `MP2_USER_TEXTURES=<dir>` — the user's own pack, over the built-in set; default
  `<user>/user_textures`. Same per-device layout.
- `MP2_TEXTURE_DEVICE=<name>` — forces the device set (and the binding icons) instead of following
  the input used last.
- `MP2_DUMP_TEXTURES=1` — Aurora writes every texture the game loads to
  `<cache>/texture_dumps`, named as a pack must name it.
- `<textures>/bindings/` — key and button icons that `PortPrompts` draws over the game's prompt
  textures, so a prompt shows the key actually bound. Generated by `tools/make_prompt_glyphs.py`
  (Kenney CC0 art in `tools/prompt_icons/`), at twice the game texture's size so they stay sharp on
  a scaled-up HUD: an icon may be any whole multiple of the bound texture's size, the same on both
  axes, and the file name's `_64x32` suffix is the game texture it serves, not the file's own size.
  As in Prime 1, prompts bind per `CTexture` by asset
  id (`kAssets` in `port_prompts.cpp`, both discs; Trilogy's 3AA65ECD "1" maps to Z) through an
  Aurora pointer key, swapped live when the device or a binding changes; an action without an
  icon shows the game's own art. `kKeys` keeps only four prompts still matched by texel hash, and
  the device sets hold just those.

Not ported: the Prime 1 build's embedded copy of the set (`PortEmbedded`), pointer-screen click
icons (`PortPointer`), the debug menu's forced-glyph and Original Experience overrides, and the
Android folder picker that installs a user pack (the request API is kept for it).

## LiveSplit

`port_livesplit.cpp` is MP1's LiveSplit Server client: a background TCP thread that connects to
LiveSplit (Control > Start TCP Server, port 16834) and sends `reset`/`starttimer`/`pausegametime`
on a new file (play time under a second), `setgametime` as the in-game time advances, `split` on
each new upgrade, and a final `split` on the EndGame special function. Compare against Game Time.
Settings are environment variables (not saved): `MP2_LIVESPLIT=1`, `MP2_LIVESPLIT_ADDR=<host:port>`
(default `127.0.0.1:16834`), `MP2_LIVESPLIT_SPLIT_UPGRADES=0` to split only at the end. F1 > Game >
Speedrun and the console's `livesplit <0|1>|addr <host:port>|send <command>|status` set the same
values for a run. The hooks are in `CStateManager` (tick, destructor) and
`CScriptSpecialFunction::AcceptEndGame`; the upgrade list is `CountsAsUpgrade` in
`include/port_livesplit.h`.

## Not built

`disc.cpp` (with `include/port_disc.h`), `debug_ui.cpp` and `smoke.cpp` are carried over from the
Prime port and are in no CMake target (checked against `CMakeLists.txt`); the F1 overlay is
`platform/debug_ui/*.cpp`. `ai_dma.cpp`, `port_randomizer.cpp` and `port_rando_*.cpp` are built, in
`mp_port_audio`. Randomizer env vars (see `docs/RANDOMIZER.md`): `MP_RANDO_DUMP=1` logs every pickup
location to `<user>/randomizer_locations.log`; `MP_RANDO_GEN=<name>` builds a no-logic seed from that
log; `MP_RANDO_SEED=<path>` overrides the seed file; `MP_USER_PATH` sets `<user>` for these.
