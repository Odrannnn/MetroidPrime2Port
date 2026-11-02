# Session status (detail)

The Status section of AGENTS.md links here: this is the per-session detail, rewritten rather than
appended, dated as in the heading there.

## 2026-11-02: Sweep A9 (retail vs decomp vs port), closed

Plan "Sweep A9" (Opus lanes, all steps ticked); rows A9-100..806 in docs/AUDIT.md (closing line in its sweep
log); reports `/tmp/opencode/reports/a9-*.md`. Pushed at 0e229952f.
- Tooling (step 1): retail_diff always flags retail ctor/dtor effects (CToken::Lock, Sfx, AddObject;
  the #59 class); `tools/retail_diff_regress.sh` (all PASS); `tools/retail_diff_allow.txt` holds
  asm-checked model differences. Default call findings 546 -> 338.
- Fixes: ElitePirate shakes use FindConnectedObject (A9-100); CPlayer/gun/dynamics/collision-actor
  constants and branch senses (A9-200..208); next-draw-node and scan-lock fixes in Chykka 1/2, Ing,
  Splitter (A9-301..305); cov tooling TUniqueId packing for 2048 slots (A9-300).
- Not run-verified: A9-201..205, 208, A9-302's off path, A9-303's reset case.
- Second round: SwampBossStage1 UpdateHeadTurn divides by retail's 0.43633232f (A9-500); CScriptPlatform
  look-at uses slot 0x5C GetScanObjectIndicatorPosition (A9-600); CPatterned ctor allocates the echo
  emitter (A9-601); CCubeMaterial TEV outReg tests use retail's `(ops & 3) << 9` (A9-700/701). Tool rules
  (A9-400, 520..522) and allowlist: `retail_diff.py` default findings 0 (48 allowlisted), regress 33/33.
  `--strict` still lists 1370 calls: the rule-hidden classes (inline copies, ctor members, port-model callees).
- Upstream tiers 2/3 read (A9-600..806): all 175 differing tier-3 functions screened, the rest matching-only;
  ~33 no-diff functions not read for branch sense, 33 unpaired symbols unassigned.
- Not run-verified: A9-201..205, 208, A9-302's off path, A9-303's reset case, A9-600, A9-700/701 (a stderr
  probe reached neither CCubeMaterial path in three 900-frame runs: both sit under `reflection`).

## 2026-11-02: Morph Ball camera control (#54)

`ball_cam` / `ball_cam_speed` (docs/SETTINGS.md), `CBallCamera::PortManualOrbit` (TARGET_PC; state in a
file-static per-player array, not fields, so the save-state schema is unchanged). Yaw rotates the camera and
`mDampedPos` about the ball (the trailing keeps it); pitch is an elevation offset; a 1.5 s hold skips Chase/Boost's
`ConstrainYawAngle`. Inputs: `PortMouse::TakeBallLook` (mouse, touch, twin stick; gyro excluded) and the raw C-stick
in `ProcessInput` without twin stick (Echoes' look inputs are R + main stick, so the mapper's Look reads are 0).
Verified headless at the new-game landing site (`cstick` -127 0 / 0 127, with and without `twin_stick`): ~130-180
degrees of yaw per second, pitch -16 -> +6 deg, both persist. Not checked: real mouse/touch, multiplayer, Chase/Boost.

## 2026-11-01: map pickup tracker (map/pickups, 23360d435)

MP1-style pickup dots: `platform/port_map_pickups.cpp`, `CMapWorld::DrawPortPickups` (TARGET_PC), F1 Tracker
page, settings `map_pickups` (Off/Rooms/All) and `map_pickup_kinds`, console `tracker`/`pickups` (docs/CONSOLE.md,
SETTINGS.md, QOL.md item 7). `tools/rando_world.py` emits `PORT_RANDO_LOCATION_MAP(relay, x, y, z)` for all 114
locations; collected = the memory relay in that world's `CScriptMailbox` (Bomb Guardian drop: the boss's DEAD relay
0x0C1A0062). Verified in Xvfb: new game 0/114; Dynamo Chamber dot sits under the player arrow after `tp` to the
pickup; Rooms tint amber, dark rims (PC `CColor(uint)` byte order: use the uchar constructor). Not verified on screen:
a collected pickup turning into a cross, a randomized seed's kinds.

Reveal map and rooms visited (2026-11-02): `reveal_map` (`MP2_REVEAL_MAP`, console `tracker reveal 0|1`) makes
`CMapWorldInfo::IsWorldVisible` and `IsAnythingSet` true (TARGET_PC; nothing saved). F1 Tracker "Rooms visited" per
world plus this world's unvisited list, console `visited [all]` (`rooms` was taken); counts are MLVL areas from
PortRandoWorld minus the 5 `game_end_part*` (279 rooms; dark areas count as rooms). Verified in Xvfb on a Landing
Site save: Light and Dark maps show every room with reveal on and only the visited one off (toggled live), `visited
all` 1/279. Not verified: the universe map listing every world.
Windows laptop not updated (unreachable).

## 2026-10-31: broad save-state sweep (every area)

statecheck round trip in every slot of Temple Grounds (0-52), Great Temple, Agon, Torvus and Sanctuary:
274 areas, one run each (`--world <id> --area <mrea> --headless --post-wait 30`, new-game card, direct
boot, at d3dbe57b5). 266 pass with 0 strict diffs. Table and repro: `/tmp/opencode/reports/sweep-all.md`.

- Load never restores the room (reproduced with one run alone): TG 62b0d67d (Landing Site), TG 9fb7503c
  (0q_temple_hall), Agon 50017152 (0f_sand_hall). Fixed by sc/load-timeout (merged f556fa724): a load skips the opening cinematic the rebuilt
  world replays, and a layer entry that streams nothing no longer waits for 256 free ids (Agon has 223).
- `door_open 1 -> 0`: TG fb843824 (01_temple_hive_04), Agon 845c85a7 (00_mapstation_sand). Fixed by sc/door-open (merged 7d3475de5): the
  restore keeps ticking (at most 120) while a door the save had open is still WaitingForArea, so the retail
  path opens it and streams the area behind it; walking through 000B0022 after a load reaches the next room.
- CScriptTriggerOrientated `active 0 -> 1`: Sanctuary df073157 (0j_cliff_hall_loop). Fixed by sc/trigger-active (merged): CScriptSwitch
  `opened` is saved (kTagSwitch), so a SetToZero after the load no longer re-fires Open.
- Rerun on 7d3475de5: all 6 formerly strict areas and 2 controls 0 strict. The sweep is clean apart from
  the 2 ending areas.
- TG game_end_part1/2 never reach gameplay with a direct boot (the ending script). Not a save-state result.

The area list comes from `tools/world_areas.py`, not the `modules.tsv` spawns column (that one is cut short at `(+N more)`).

## 2026-10-31: save-state sweep, sessions 2-3

Merged into port (pushed at 1e14b5f63):
- sc/restorehold: `src/MetroidPrime/main.cpp` fixed-tick loop counts ticks actually run and stops on
  `PortOverlay::TickHold()` (Rezbit timer / ai_time drift after a restore).
- kbres: knockback resistance saved after hp (optional trailing field; Dark Trooper).
- ds2doors: an hp <= 0 restore marks actors/platforms dead (`PortRoomState::MarkDead`).
- sc/quadraxis: FSM2 machines saved with sub-machine path + leaf node index; Quadraxis 3/3, 0 strict.
- sc/dg-collider: `tools/statecheck.py` `step_ticks()` (console `step N` only queues; polls `frame`),
  used by A1, C (`--post-wait`) and `--wait`. Quadraxis x10CC_ `cls:X -> none` in B is a lazy rebuild
  in `x61DC_` on the first Think: harmless; use `--post-wait ~30`.
- Ending slots 53→56, dg.kneecol/killknee route, `docs/SAVESTATE_SCHEMA.md` "what a save keeps",
  RUNNING.md names `MP_SAVESTATE_FIELDS=0`.
- Docs audits: GOTCHAS, TRILOGY, DECOMP_README, LANE_BRIEF, DECOMP_WORKFLOW, MODULES, SETTINGS,
  env claims (settings.cpp header, QOL idle cursor, console `(env X set; wins if the key reads it)`),
  CONSOLE.md + console help strings (state hold, crosshair gamecube, hint radius, place angles), MODS.md.

Session 3 lanes merged: sc/ing3stage1 (reset a live FSM whose saved machine never started),
sc/statecheck-setup (`--setup` chains with `;`), docs/mods-followup, sc/dg-pickups (not reproduced
in 8 runs; debris drops documented in SAVESTATE_SCHEMA.md). GPL LZO replaced by an MIT decoder.

sc/debris-pickup merged (a0fc2d185): a debris' dropped pickup is saved on the debris (`kTagDrop`);
the restored debris drops again and that pickup takes the saved fields (or is removed). killknee
route 3/3, 0 strict, pickups 3=3. Not saved: a drop kept by `mKeepGeneratedObject` after its debris
is gone.

Open: retail Puffer has no engaged state. `platform/debug_ui.cpp:296` has the old env claim but is unbuilt. Statecheck runs:
copy the userdir, own --port/--display, `--build <tree>/build-port-link`.

## 2026-10-31: behaviour audit 2 (A8) complete

- Ledger `docs/AUDIT.md` A8-1..A8-28 all closed. Fixes merged: A8-17 morph-ball Render PC arm
  (fix/a8-ballrender, reviewed vs asm 800C4404-800C4F40, review rework d00eab8b5), A8-24 snow forces,
  A8-25 rain splash generator, docs RUNNING/GOTCHAS staleness.
- User decisions: A8-16 keep the #30 multiplayer gate; A8-22 keep the once-per-system screen copy
  (perf); A8-26 keep the confirming START press; A8-28 keep Remastered crosshair default.
- Not seen on screen: morph-ball rain splash and spider glass (no raining area / magnet track reached).

## 2026-10-29: 0.6.3 released

- Bumped `versionCode` 9 / `versionName` "0.6.3", metainfo 0.6.3 entry, notes
  `docs/release-notes/0.6.3.md`; release commit `a158c2851` (fixes #10-#15 and a save-state layer fix since 0.6.2).
- CI on `a158c2851`, all `success`: linux-release 40930262879, windows 40930267321, macos
  40930272480, android 40930276804, ios 40930281275. Flatpak built locally from `a158c2851` (12,250,288 B).
- Crash soak (`build-port-link` at `a158c2851`): `build/s-release-2026-10-29c/soak/summary.tsv`,
  **96/96 rows `ok`**, exit 0. Log `/tmp/opencode/soak-063.log`.
- Artefact checks from `build/release-0.6.3/out` (12 assets): the version string 0.6.3 is in
  the Windows, macOS, Linux (tarballs and both AppImages) and APK binaries; IPA `Info.plist` 0.6.3; no game data in any zip, APK, IPA,
  tarball or AppImage; no `user/` folder in the Windows zips; macOS executable chmod +x before zipping.
- Tag `v0.6.3` = `a158c2851`; GitHub release
  https://github.com/Odrannnn/MetroidPrime2Port/releases/tag/v0.6.3, normal release
  (`isPrerelease` false, 12 assets); `releases/latest` answers `v0.6.3`. Not installed on a device.

## Open items carried from older sessions

Sessions older than 0.6.2 moved to `docs/STATUS-history.md`; what is still open from them:

- **Android verification pending** (2026-10-26 widescreen/ball/suit batch): touch layouts at 16:9/21:9, the pause Inventory recolor and the ball shadow have not been run on the phone.
- **Shared spare copy buffer** (2026-10-20 scan-visor session): camera filters, `CPlayerGun` and the particles still copy into the shared spare buffer without a `Destination` and could take each other's resolved texture; fold them into `Destination` when one shows the symptom.
- **Echoes' prompt-glyph `kKeys`** (2026-10-19 twenty-fourth session): the game draws prompts from GUI models (`model_abutton`), not 32x32 textures; building `kKeys` needs a dump taken while a prompt is up plus the model's own texture size/hash.

Older sessions: `docs/STATUS-history.md`.


## Smoke rules (2026-10-24)

Poke your own family's actor; no `=> err` proofs; strictly increasing @f; objs checked >=40f after a kill; editor ids, not uids; no forced Dead. Beam-free lanes = `worker` + model `opencode-go/beam-free`; their reports are unreliable, so check con_*.out.
