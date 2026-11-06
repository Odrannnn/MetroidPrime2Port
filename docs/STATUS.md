# Session status (detail)

The Status section of AGENTS.md links here: this is the per-session detail, rewritten rather than
appended, dated as in the heading there.

## 2026-11-06: third-person camera (MP1 package 33)

- `platform/port_third_person.cpp` (MP1's camera minus its body module; Echoes draws the real
  CPlayer body): F1 Video "Third-person camera", settings `third_person*`, console `thirdperson`.
  Single player, first-person camera only. Drawn view = blended `View`, simulation = `TickView`
  (orbit/aim searches, screen-space conversion, reticle clamping).
- The body renders and holds the cannon at its gun locator; shots leave that locator toward the
  locked target's assisted aim or `AimPoint`; grapple beam from the hand; helmet, reflection and the
  gun's own FOV pass are off while active; reticles project from the third-person view.
- Verified on Xvfb (new game, Temple Grounds): body over the shoulder, lock-on ring on the web
  target, locked shot hits it. `port_third_person_tests` pass, upstream 87 files OK.
- Body animates: `UpdateMorphBallTransition` skips `UpdateAnimation` in the first-person camera
  while locomotion runs; it now also runs while `ShowBody` (as in multiplayer).
- Follow-up (same day): morph-ball handoff (CBallCamera ToBall spline starts at the third-person
  camera, unmorph returns to `MorphCameraPosition` with a wall pull-in; `ShowBody` keeps the body
  drawn and solid while the ball camera takes over), charge/muzzle/missile/combo fx re-posed at the
  body's cannon (`CPlayerGun::PortRenderThirdPersonFx`), pointer aim (shots go to the pointer's aim
  point; picks use `TickView`), console `thirdperson` changes saved. Verified on Xvfb: smooth morph
  in/out, charge glow at the cannon, pointer shot hits off-centre, settings.cfg updated.
- Body animation: 91c2e3b89 advances the body's animation in third person. Then the jump/flip
  commit: single player's FSM (SinglePlayerAnimCtrl) has no Jump/Dash/Grapple/HeavyHit states, so
  in third person the body runs PlayerAnimCtrl (`CPlayerBodyController::PortUpdateThirdPersonFsm`,
  swapped only in Locomotion/Morphball). Samus_SinglePlayer (character 3, own CINF) lists only
  jump loop/land/hard-land; jump arc and `B_doublejump(_backwards)_samus` are borrowed from
  character 0 (`CAnimData::PortBorrowAnimation` appends the anim-set index to the player's list;
  savestates skip an anim id past the list). Verified on Xvfb: jump arc, double-jump flip
  (upside-down mid-flip, limbs intact), FSM swaps back on `thirdperson off`.
- Dash/grapple/knockback: every body-state lookup goes through
  `CPlayerBodyController::PortFindBestAnimation` (borrows from character 0 when the PlayerAnimCtrl
  FSM is active and character 3 has no match). Grappling in third person crashed (9038a8f6a): the
  body's beam uses the multiplayer segment/swoosh generators, which only existed in multiplayer;
  `CGrappleArm` now always creates them on PC and starts that beam at the scaled `L_wrist` locator.
  Verified on Xvfb: side dash into/loop/exit, grapple fire/pull/swing with the beam from the hand.
  Knockback is not checked in game (only `CPlayerKnockBackMgr` sends it; console `damage` doesn't).

## 2026-11-05: save states keep every script object's fields (why cutscene bugs kept coming)

- Cause: the room save's `kTagFields` was an allowlist (CPatterned + generated pickups/debris), so
  cutscene objects (keyframes, ColorModulate, SoundModifier, sound volumes, actor anims, platforms)
  came back fresh, and `statecheck` compared only saved fields, so nothing showed it. Mid-cutscene in
  Aerie: 718 differing unsaved fields before.
- Now every scripted object (valid editor id) except doors/docks (own blocks) gets the block.
  Denied (`SavedField`): CScriptTimer (kTagTimer + holdTimers own it), trigger/special-function
  inside/damage flags, sound per-frame flags, any `*ThisFrame*`, and whole classes RoomAcoustics,
  PlayerActor's own fields, StreamedMusic, RsfAudio, Midi, LayerController, WorldTeleporter.
- ApplyEntityFields/GetField/GetAnim count only writes that changed a value, in all passes.
  `statecheck --gaps` lists unsaved fields that differ; `--cinematics` lets cutscenes run.
- Verified on Xvfb: mid-cutscene Aerie round trip 0 strict (gaps only clock/token/bounds noise);
  calm rooms (sand 06/0b, swamp 07 with Puffer) 0 strict; #84 repro unchanged. Save grows ~12 KB ->
  ~114 KB per slot in Aerie. build-port-link OK, no upstream files touched.

## 2026-11-05: issue #84 (save during Dark Samus 2's death cutscene, load leaves her idle)

- Two causes, Aerie (1baa96c2 area 65): the room state had no CScriptSequenceTimer state, so after a
  load the shot chain (cameras 004103b2 on) never fired again and the stand-ins (idle Dark Samus
  004102c3, Samus player actors 004103bd/004103e1) stayed in gameplay; and the load's own cinematic
  fast-forward (its 0.5 s settle) was still running when TickPlacement let the restore resume the
  saved shot, so that shot was skipped too.
- Room block `kTagSequence` = 22 (time, running, start originator as editor id), applied with the
  deferred blocks after the dispatch (a camera's Activate can Start the timer); TickPlacement also
  waits for `CMFGame::PortCinematicSkipActive()`. Not saved: CScriptActorKeyframe state, actor anims.
- Verified on Xvfb (console `state save` at f900 mid-cutscene, load at f1500): the cutscene plays on
  to its end and gameplay has the same objects as a run without the load. build-port-link OK, upstream 87 files OK.

## 2026-11-05: pipeline key audit (follow-up to #78)

- `tools/pipeline_key_audit.cpp` perturbs each seed row's PipelineConfig fields one at a time and
  compares the generated shader source + pipeline descriptor: of 1741 GX rows, 1660 distinct
  pipelines; the 81 duplicates differed only in colorChannels[0] lighting/matSrc on an unsampled
  channel, unsampled tcgs (src/mtx/type) or a stage's channelId with no RASC/RASA input.
- Aurora `gx::pipeline_unused_inputs` resets those in populate_pipeline_config and queue_cached_row
  (perPixelLight recomputed after). `--check-unused`: 0 pipelines changed at either per-pixel setting.
- Seed rewritten (`--rewrite`, then `pipeline_seed.py merge`): 1744 -> 1667 rows, 9088 -> 8939 uses,
  `check` OK. Lagoon spot on Xvfb from an empty cache: seed loads 1667 rows, 0 pipelines outside it.
  Not run on Windows or the Poco. Not covered: attribute-format fields, texCoordId/texMapId, depthFunc with compare off
  (all dead in some rows, none split a pipeline in the seed).

## 2026-11-05: issue #78 (Torvus Lagoon water flickers, D3D12)

- Cause: CFluidPlaneCPU::RenderSetup sets only the last TEV stage's alpha (K0.a); the earlier stages
  keep whatever alpha the previous draw left, which is dead but part of the pipeline key, so each
  leftover combination was a new pipeline and a miss (draw skipped while it compiles; ~500 ms on D3D12).
  The #44 seed covered some combinations only. TARGET_PC now zeroes those stages' alpha (ZERO inputs,
  ADD, clamp), so one fluid setup is one pipeline; the seed's fluid rows were rewritten to that form.
- Seed: new spot `torvus_lagoon` (tools/drivers/pipeline_seed_torvus_lagoon.con: spawn + lagoon
  aim sweeps) and the Trilogy-only row ab1fe2747a9851b9; 1446 rows, `check` OK.
- Lagoon repro (Xvfb, empty cache + seed): GC 9 misses -> 0, Trilogy 10 misses/13 skipped draws -> 1/3
  (ab1fe2747a9851b9, then added to the seed; not re-run). Not run on Windows/D3D12. upstream build untouched (TARGET_PC only).
- Poco's cache (pulled read-only from /storage/emulated/0/MetroidPrime2/shader_cache, normalized
  like Aurora's queue_cached_row): 447 retail v15 configs not in the seed. Not leftovers: since
  config v15 (0ccaa2c17) only the #44 changes (normalized on load), 8864b3060 outReg (2 rows) and
  b0dea75e8 (removes pipelines) touched the key. One more dead-key class: `PipelineConfig::dstAlpha`
  held GXSetDstAlpha's value (CCubeRenderer's per-area PVS alpha, CPauseScreen layer masks,
  CMorphBallShadow `id*4`), which only reaches the blend constant; the pipeline tells off/0/other.
  Aurora now keys it as UINT32_MAX/0/0xFF (`gx::pipeline_dst_alpha`, gx.cpp and queue_cached_row);
  the seed's 52 such rows were rewritten (1446 -> 1417 rows).
- With that, 327 phone configs remain missing; none are produced by the `frontend`/`actions` passes
  (re-recorded). They sit in areas/phases the spots don't reach: Great Temple 07_mothlordsanctuary_dark
  (50), Torvus 04_swamp_temple light (45), TG 12_temple_finalgate_dark (36), Agon 06_alamofront_dark
  (34), Agon halls, plus fog on/off and dstAlpha 0/non-0 variants of seeded rows (real variants).
  Merged as-is (msaa bytes kept, rest normalized, rehashed, uses remapped): seed 1744 rows / 9088
  uses, `check` OK, hashes OK; lagoon repro on Xvfb with it: 0 misses, seed loads 1744 rows. A
  re-record drops these rows unless spots for those areas are added (`merge` the old seed in instead).

## 2026-11-05: issue #80 (Exit Game on the main menu)

- CScriptGuiScreen.cpp `PortUpdateExitLegend`: two clones of the file screen's Copy File legend (game
  font) draw "Exit Game" and the Z prompt (`&image=` of the first Z glyph TXTR the disc has; PortPrompts
  redraws it as the binding). Shown while an active pane still shows the STRG's "MainMenu" text and no
  Copy/Erase File legend is active (`CScriptTextPane::PortShowsString` compares the text: the title pane
  keeps index 0 on other screens). Holding Z (not with Start) 1.5 s fills the letters orange, then
  `PortOverlay::RequestQuit` (F1 > Game > Exit's path). Setting `menu_exit`, env `MP2_MENU_EXIT`.
- Checked on Xvfb: main menu shows it, the Single Player and Options screens don't, the hold quits
  (clean exit). Not checked: keyboard glyph, Multiplayer screen, Android. `&pop;` crashes upstream's
  CTextExecuteBuffer::AddPopState, so the fill re-sets the pane colour instead. Text is English only.
  No issue reply posted.
- Follow-up: default on everywhere (Android too). The touch overlay shows only Back and the cog on
  pointer-menu screens (the whole front end), so a finger or mouse button held on the legend or its
  prompt also counts (`PortPointerHoldsExit`; sliding off lets go). Pane hit rects cover the layout box,
  which this clone's text lies mostly outside of, so `PortTextPaneInkBounds` adds the drawn glyphs' rect.
  Prompt scale 1.7 (as tall as Confirm/Back's A and B). Console `pointer` takes GUI pixels: 448 high,
  796 wide at 16:9 (screenshot pixels x 0.622). Checked on Xvfb: touch hold fills and quits, a mouse
  press slid off doesn't.


## 2026-11-04: issue #44 (Dark Samus flicker with per-pixel lighting)

- Cause: with per_pixel_lighting=1 every lit ShaderConfig key has perPixelLight=1, but the embedded seed
  and every cache row loaded at boot carried 0 (the setting was applied after aurora_initialize), so each
  room's first lit draws compiled cold and were skipped. Fix: main.cpp sets it before aurora_initialize and
  `queue_cached_row` recomputes perPixelLight for the running setting (off for actor decals; the
  command_processor validity check matches). Forgotten Bridge walk, fresh cache, MP2_PER_PIXEL_LIGHTING=1:
  36 new pipelines before, 0 after. Boot precompile takes about 2x longer with it on (bigger shaders).
- Next: grow the seed with fight and boss SPOTS in every world (tools/pipeline_seed.py; candidates
  come from the tools/cov/specs run lines). No issue reply posted.

## 2026-11-04: issue #74 (Command Chamber Dark Troopers after a state load)

- 2c69718c0: patterned actors save their CMaterialList (kTagMaterial, set after the fields): a trooper
  saved after Rise came back with Sleep's passthrough. PlayerInAreaRelay special functions save
  mPlayerInArea (kTagInArea): 0013013a/0013013d re-sent Entered after the load and their Play of
  0013011c/0013013c replaced the restarted battle music 0013013b. Checked with tools/statecheck.py
  --area 19 (setups `fsm e00130013 Rise`, `send e0013008f Start`, `send e0013013d Activate` + `send
  e0013013b Play`; MP2_DEBUG_AUDIO=1 shows the channel order): 0 strict diffs.
- Not reproduced: the "one or two troopers idle" (the reporter couldn't either); the reporter's log shows
  30 generated copies in the first capture after the load, gone by the next one.
  No issue reply posted.


## 2026-11-03: rando terrain models Screw Attack

- tools/rando_terrain.py: ScrewAttack is an ability bit (UNMODELLED is grapple only). Screw Attack starts out
  of a double jump, dry; each hop (up to 5, 20 u/s across) peaks 5.0 over the double jump's spot and may land
  4.5 under it (CMorphBall::ComputeScrewAttackMovement), so it needs SJ and reaches 8.78 over the take-off,
  searched 40 away (spots thinned to one per component per 2-unit cell). SETS is 256 sets: cold run ~60 min.
- An edge only a Screw Attack set crosses reads nopath for its other terms (84 such edges: the mesh misses
  their route, mostly moving platforms); screw terms there are still judged. The cache key is now the file
  above `check()`, so editing the report logic keeps the cache.
- Rule fixes, measured: 0f_swamp_hall dock1 - dock3 and 0k_sand_hall_dark dock0 - dock1 drop `| ScrewAttack`
  (the bomb-jump climb out of the pit stays; the mesh's screw route morphs into the tunnel mid-hop);
  0f_swamp_hall dock1/dock3 > its pickup keep Space Jump & MorphBall with Screw Attack as a third breaker;
  10_sand_alamostorage_dark's Temple Key needs ScrewAttack & SpaceJumpBoots.
- tools/rando_rules.py IMPLIES: every ScrewAttack literal also asks for SpaceJumpBoots (barrier breakers and
  Temple Key chests named it alone); vanilla 114/114 in 9 spheres, gen 200/200 unchanged.
- Full run: ok 569, refuted 16 (all accepted), nopath 208, unanchored 28, virtual 138; vanilla 114/114 in
  9 spheres, gen 200/200.
- Next, one item per session: Grapple Beam, platform motion (and ball triggers/cannons), swimming. Loop:
  model it in rando_terrain.py, `python3 tools/rando_terrain.py` (cold ~60 min), triage refutations (rule
  fix or measured reason in terrain_accepted.txt), `python3 tools/rando_rules.py --write`, then the
  logic/gen C++ tests built directly: `c++ -std=c++20 -Iplatform/include tests/port_rando_logic.cpp
  platform/port_rando_{logic,world}.cpp` (gen: tests/port_rando_gen.cpp + port_rando_{gen,logic,world}.cpp).

## 2026-11-03: rando terrain models bomb jumps

- tools/rando_terrain.py: MorphBallBombs is an ability bit (no longer UNMODELLED). A bomb jump lifts the ball
  3.64 (CPlayer::BombJump: sqrt(2 * 35 * bombJumpHeight 7.9) against ballGravity 76), drifting at the ball's
  air speed 10 onto a spot up to 3.24 higher or 2 lower; one bomb (double bomb jumps are a trick). Power
  Bombs don't bomb-jump (only kPA_TriggerBomb calls BombJump, DOL 0x8003CA5C).
- Rule fixes, all measured: morph tunnels that drop into 5-unit pits and leave by a mouth 3 up need bombs
  (standing Samus hits the ceiling before morphing): 0q_temple_hall dock0 > dock3 (with Space Jump),
  0i_temple_hall's pickup, 0c_swamp_hall and its dark twin dock0 - dock2, 0f_swamp_hall dock1 - dock3 (Power
  Bomb term dropped) and its dark twin, 0k_sand_hall_dark dock0 - dock1 (Boost/PB terms dropped),
  07cliff_gyro dock0 > dock2. 0h_sand_hall dock2 > dock4: the 4.0 mouth is past a bomb jump, so bombs or a
  PB only break the grates after Space Jump. Accepted: 0l_sand_hall dock1 > dock0's bomb term (bomb jump into
  Ball Trigger 0x003300BE; ball triggers not modelled).
- Full run: ok 569, refuted 16 (all accepted), nopath 208, unanchored 28, virtual 138; vanilla 114/114 in
  9 spheres, gen 200/200 (tests built directly, as below).
- Same loop as below.

## 2026-11-03: rando terrain models Boost Ball half-pipes

- tools/rando_terrain.py: BoostBall is an ability bit. Half-pipes (material bit 13, kMT_HalfPipe) ride like
  spider tracks: on from a floor within 1.0 below a cell, off onto floors beside a cell up to 3.2 above
  (disc ballHalfPipeBoostHeight) or below. Boosted gap jumps, ramps and spider boosts are not modelled.
- Rule fixes: 09_temple_thirdleg dock0 > dock2 and 0q_sand_hall dock0/dock1 > dock2 need MorphBall & BoostBall
  (13.6 to 16 up, out of Space Jump reach; the half-pipe climbs there); dark 0q_sand_hall's Missile (21 up)
  likewise; 0l_sand_hall dock1 > dock0 also opens with Boost. Accepted: 0h_sand_hall dock2 > dock4's Boost
  term (no half-pipe, boost ramps not modelled), dark 0l_sand_hall dock1 > dock0's Space Jump (vanilla
  climbs it before Boost; Beam Platform PLAT 0x003A003A, motion not modelled).
- Full run: ok 551, refuted 15 (all accepted), nopath 227, unanchored 28, virtual 138; vanilla 114/114 in
  9 spheres, gen 200/200. The logic/gen tests aren't configured in build-port-link: build them directly,
  `c++ -std=c++20 -Iplatform/include tests/port_rando_logic.cpp platform/port_rando_{logic,world}.cpp`
  (gen: tests/port_rando_gen.cpp + port_rando_{gen,logic,world}.cpp).
- Next, one item per session: bombs and Power Bomb jumps (disc bombJumpHeight 7.9, bombJumpRadius 1.5),
  Screw Attack, Grapple Beam, platform motion, swimming. Same loop as below.

## 2026-11-03: rando terrain models water, Gravity Boost, dark water, Spider Ball

- tools/rando_terrain.py: verdicts are minimal sets of MorphBall, SpaceJumpBoots, GravityBoost, SpiderBall,
  LightSuit. WATR boxes (default layers, never deactivated) give per-depth-bucket jump curves simulated from
  CPlayerDynamics (disc Standard.ntwk: fluid gravity -10, waterJumpFactor 0.37; GB 45 for 1.5 s; dark water
  low gravity unless Light Suit), never below the dry jump: the vanilla route climbs 0r_swamp_hall's
  underwater stairs (1.5 to 2.5) before GB, past the simulated 1.65. Spider tracks (material bit 61) join
  floors within 1.9 below to any floor beside/below.
- Rule fixes: 07_swamp_shaft dock1/2/3 > dock0 GravityBoost only (15-unit climb; SJ reaches 7.7),
  0q_swamp_hall_dark key needs SpaceJumpBoots (ledge 12 up, no water). Accepted: 07_swamp_shaft dock6 > dock0
  (water cannon). Reactor Core spider lines stay nopath (a ball cannon starts the climb).
- Full run: ok 547, refuted 13 (all accepted), nopath 233, unanchored 28, virtual 138; vanilla 114/114 in
  9 spheres, gen 200/200.
- Next (not modelled), one item per session: Boost Ball, bombs and Power Bomb jumps, Screw Attack, Grapple
  Beam, platform motion, swimming. Loop: model it in rando_terrain.py, `python3 tools/rando_terrain.py`
  (cold ~17 min), triage refutations (rule fix or measured reason in terrain_accepted.txt),
  `python3 tools/rando_rules.py --write`, then the logic/gen C++ tests (vanilla 114/114, gen 200/200).

## 2026-11-03: terrain triage done (Sand, Swamp, Cliff)

- Rule fixes, Space Jump ledges measured on the mesh: 02_sand_sandfalls dock0/dock2 > dock1, 0g_sand_hall
  dock1 > dock0, 0h_sand_hall dock2 > dock4 (tunnel mouth 4.0 up: bombs, PB with SJ, or Boost), dark 0e/0g/0n
  sand halls, 02_sand_sandfalls_dark (ledges 5.0 up), 08_sand_casualpirates_dark (Beam Platforms only sink);
  0m_swamp_hall dock0 > dock1 (6.2 wall), 09_swamp_catacombs dock1 > dock0/dock2 (ledges 5.0 up),
  01_swamp_entrance_dark (unsure: jumps off the false floor), 02_swamp_greatbridge_dark dock0 > dock1 (unsure:
  dark-water pit 4.2 below), 0q_swamp_hall_dark dock1 > dock2 (5.8 step), 07cliff_gyro_dark (gyro static:
  rings and platforms 4.8 to 5.5 steps). 0f_swamp_hall's energy tank opens from the upper corridor (Tunnel
  Plug in its floor; the lower blocker only lets you out): from dock1/dock3 it needs Space Jump too.
- Accepted (platform motion): 08_sand_casualpirates Elevator, 01_swamp_entrance Swamp Bridge (scan-raised),
  10_swamp_hiddenhalfpipe_dark Sliding Coffins.
- Full run: ok 544, refuted 12 (all accepted with reasons, none unverified), nopath 237, unanchored 28,
  virtual 138. Vanilla 114/114 in 9 spheres, gen 200/200 (avg 9.38).

## 2026-11-03: terrain triage, Temple Grounds done

- Rule fixes (all Space Jump ledges, measured on the mesh): 0t_temple_hall (+dark) dock0 > dock3,
  0n_temple_hall (+dark) dock1 > dock2/dock0, 12_temple_finalgate dock1 > dock0, 06_temple_sacredpath_dark
  dock0 > dock1 and its pickup, 05_temple_federationsite_dark dock0 > dock2, Hive Chamber A
  (04_temple_unknownenemies) dock1/dock2 > the stasis-chamber pickup (new: dock0 drops onto its shelf).
  Accepted: Sacred Path dock1 > dock0 (Morph Ball cannon PLAT 0x00340038).
- Full run: ok 514, refuted 40, nopath 237, unanchored 28, virtual 138; 33 still "unverified" (Sand 16,
  Swamp 13, Cliff 4; next 02_sand_sandfalls). Vanilla 114/114 in 9 spheres, gen 200/200 (avg 9.37).

## 2026-11-03: terrain triage, first pass

- Model: floors are signed (a ceiling's top was a floor: 11 refutations cleared, several passes became honest
  nopath), a ball rolls off tunnel lips (DROP_REACH 1.5), tall doors anchor deeper. Box triangles wind outward.
- Rule fixes: Service Access (Meeting Grounds side needs a bomb jump into the tunnel), Landing Site
  dock1 > dock0 (SpaceJumpBoots), Transport A Access dock1 > dock0 (SpaceJumpBoots), Sanctum Access climb
  (ScrewAttack & SpaceJumpBoots). Accepted with reasons: Controller Transport's elevator, Sanctum Access's fall.
  Vanilla 114/114 in 9 spheres, gen 200/200. Refuted 49, 44 still "unverified" (next: 0t_temple_hall dock0 > dock3).
- Randovania's logic text was read only as a private cross-check (not committed, not cited).

## 2026-11-03: randomizer terrain check enforced

- Landing Site dock1 > dock0's earlier pass came only from intro-cinematic PLATs; platforms now count in two
  states (area start, switches thrown), solid ACTRs and breakable crates (start state) count too, terms with an
  unmodelled item are not refuted. Refuted 147 -> 65 (unanchored 183 -> 27 in 2ae1b7fec).
- `rando_rules.py` runs the cached check; `tools/rando_rules/terrain_accepted.txt` accepts all 65 as a baseline,
  only GFMC Compound's four (Kinetic Orb Cannon on a story layer) with a checked reason.

## 2026-11-03: AGENTS.md Status bullets as of 2026-11-03 (moved here verbatim)

- 2026-11-03: retail-vs-port audits (user-approved). Pushed at 2675e0bf9: 4f712e868 (culling uses the draw FOV),
  d72b71352 (portal PreRender uses the drawn view), 52bc2dc82 (CCameraFilter dialog box uniform scale),
  22e7ab8eb (CSamusHud damage ring + lock-on bind Load(TEXMAP0, kCM_Repeat) as retail; build-only: console
  `hurt` damage lands outside the update the HUD reads, so no headless ring capture). Geometry audit done except
  CStateManager ~1493. Asm audit done for DOL call targets, texture binds and literal state-setter args
  (CGraphics/CGX/GX blend/alpha/z/cull/stream); remaining hits benign. Not done: field-offset checks.
  `build-port` (headers-only) fails on SDL3 headers (DolphinCMemoryCardSys.cpp, main.cpp); build-port-link is fine.

- 2026-11-03: #64 MP results screen all red fixed 2675e0bf9 (CColor(0xRRGGBBAA) statics byte-reversed on LE;
  only unguarded instance left in src); headless 2p `mpend` verified; pushed 2675e0bf9, no reply.

- 2026-11-03: #65 Echo Visor waves filling the screen at Quadraxis fixed febec30e4 (Aurora line near-clip),
  repro-verified; pushed, no reply. Its bug 2 (white blocky bomb-slot smoke) not reproduced on Linux/Vulkan
  (slot effect states forced: flames render normally); reporter is Windows/RX 7900XTX/MSAA 4, likely D3D12.
  Their log's one-time "texcoord 1/2 reads UV set 1/2" warning fired after the fight (door to area 60), not
  during the smoke, so it is unrelated; not reproduced (door break, area 60, every beam's impact).

- 2026-11-02: ball-HUD threat fill overhung its bar's bottom edge (authored thicker than the track):
  fixed 31cc21392 (`PortFitThreatGauge`, CSamusHud.cpp), screenshot-verified 16:9; pushed.

- 2026-11-02: #44 water flicker (pipeline misses drop draws while compiling) worked further; all four pushed
  (3ca085e78 on 2026-11-02); no reply. c80cf83be (pushed): pipeline_seed.py `SPOTS`, repro spots the area
  sweep misses, played by `record` (`--only NAME` records one to merge into the seed):
  42fea9ab1 (seed queued at the running MSAA; fog type a uniform), 472b1cf1a (alpha compare refs a uniform),
  5e5d31411 (TEV konst selections a uniform), 3ca085e78 (clear key without sample count, clear config v5; seed
  re-recorded, 1370 rows incl. the Torvus underwater repro cache). Torvus repro (swamp entrance, tp 162.7 -78.4 40)
  misses ≥64 -> 0. Remaining class: TEV alphaPass.d (TEXA/RASA/KONST) content variants only the seed covers; an
  ubershader fallback was declined for now. User rejected blocking compiles (stutter). Next: user tests; reply on #44 only if asked.

- 2026-10-31: save states carry enemy fields generically (kTagFields, `tools/gen_entity_schema.py`, codecs in
  `port_savestate_fields.cpp`). Sweep session 2 merged restorehold (tick loop), knockback resist, hp<=0 dead marks,
  FSM2 sub-machine path; docs audits of GOTCHAS/TRILOGY/DECOMP_README/LANE_BRIEF/DECOMP_WORKFLOW. Session 3 merged
  Quadraxis 3/3, ending slots (53→56), dg.kneecol/killknee route, schema "what a save keeps", statecheck step_ticks
  (x10CC_ none in B = lazy rebuild, fine). GPL LZO removed (GOTCHAS "No GPL LZO"); port pushed at 15e9563ac (debris-pickup).
- Lanes (review diff, merge; reports /tmp/opencode/reports/): Ing3 Stage1 merged (unstarted machine reset); dg-pickups not reproduced (docs note merged, 2972e66ab), statecheck `;` chains + dead-Puffer recipe merged; debris-pickup merged (kTagDrop, 3/3 0 strict); all lanes free; SETTINGS, env-claims, CONSOLE (+help), MODS (+followup: texture search rows) merged.
- Broad sweep done (2026-10-31): all 274 campaign areas, 266 pass (STATUS.md, reports/sweep-all.md). Fix lanes
  sc/trigger-active, sc/load-timeout, sc/door-open merged; all 6 strict areas rerun 0 strict (7d3475de5).
  Only the 2 ending areas fail (direct boot never reaches gameplay). Pushed at b24927221.
- Multiplayer (#45): mp/multiplayer (379f7723d) merged into port 2026-11-01 (260868560, after reapplying
  players-ui 5655200c3; port's MP1 lock-stays, Spider toggle state and Dodge now per player, 9f08f114e;
  pushed at 703cf3c85 with prompt glyph, 4:3 menu, orb and touch back-arrow fixes). Done: GC front end 2/4p deathmatch/bounty/results (`mpend`), vpads
  (`MP2_VPADS`, `MP2_VPAD_INPUT`, `vpad`/`padport`/`padread`), per-player aim slots, per-port presets/remap
  (`padpreset`/`remapport`), per-pad gyro (step 1b; ports 1-3 gyro button R3), per-player settings (plan eb1c72a5,
  2026-11-01): `p2..p4.<key>` overrides in settings.cfg (unset = follow P1; `PortOverlay::SetPlayerSetting`,
  `PortMouse::SetPlayerStick*`), F1 Multiplayer page (`platform/debug_ui/multiplayer.cpp`), console `players`
  (each player's pos/yaw/aim/twin stick); P2-P4 remap = the selected player's Remap page embedded under Controller on F1 Multiplayer (the old Aurora "Buttons" rows are gone; Aurora mapping per
  controller; controls.toml is P1's only), console `padmap`, checked on a uinput fake Xbox pad (70dab01fe). Verified on vpads (P2-only twin stick, cfg round-trip, SP), gates,
  Windows build; tested by the user (2026-11-01).
  Full engine Remap per player (2026-11-01): user files are 4 slots (0 = controls.toml, 1-3 = `<user>/controls_p<N>.toml`); devices.cpp
  functions take a port (UserBindings*, BuildProfile, ReadRaw, ActivePad, BindController/BeamChords/MenuDefaults,
  GameContexts, cycle picks); `PortInputDevices::PollPlayer(port)` runs that player's Runtime and `port_events.cpp`
  sets `PADSetPortOverride(port)` each frame (re-armed in PortRearmPad0) while the port has a pad and the match has
  >port players. Beam/visor/spring/dodge picks per player via PortTouch (`player` args; BeginFrame applies p1-3 to
  PlayerState(p)); CPlayer publishes morph per player; no radial for P2-P4. Verified on vpads and on a uinput
  fake Xbox 360 pad given port 1 mid-match (`padport`). A hotplugged real pad takes port 0 from a virtual one (Aurora
  `prefer_real_controller_on_first_port`, intended), so such tests need `padport` after the plug.
  Limits: console `padread` calls PADRead itself, so it shows Aurora's native read, not the overrides. P2-P4 pad
  presets (Remap and `padpreset`) clear that player's pad rows and add the ball rows; Remastered = MPR Dual Sticks
  (298cf6156); prompts prefer a face button when an action has two pad inputs (379f7723d).
  Per-player gaps: port's Spider toggle, aim lock-on and hand-on-cannon settings are still global (P1's).
  Windows D:\mp2\src is port 703cf3c85 (bundle/port; touch back arrow = B past remaps), built; a waiting job copies it to run when the game closes.
  0.8.0 released 2026-11-02 (tag v0.8.0 = 0288f3ac0); 0.8.1 hotfix 2026-11-02 (tag v0.8.1 = 5e61efa46). 0.9.0 released as latest 2026-11-02 (tag v0.9.0 = bacf42f9d, soak 96/96, 12 assets; notes 768ac53b7). No issue replies posted.
- 2026-11-02: map pickup tracker merged into port (aacce5737, pushed 54d8e18d8; release APK installed on the Poco;
  STATUS.md has detail): F1 Tracker, `map_pickups`, `reveal_map`, rooms visited + unvisited list, console
  `tracker [reveal 0|1]`/`pickups`/`visited`. Unverified: collected cross, rando kinds, universe map under reveal.
- MP1 sync package 27 (MP1 through 7eda62884, docs/MP1_SYNC.md): s27a-core merged (2048 object slots, smoke 80/80 ok); s27b-input merged (in-game checks only for hand on cannon), s27c-small merged 1b8b590a3 (ball bars seen; visor crosshair toggles and Android band unverified); package 27 done, pushed at 7330047e0. Package 28 is in db628b366, and package 29 (touch stock buttons) is 68b2f1265, pushed and in 0.8.0. Package 30 (MP1 through 5e1a88514: flick stick, pad preset for later pads, mouse yields to pad, touch wheels as added buttons, empty-lock marker) merged 3df26fa44 2026-11-02, pushed at 3df26fa44; wheels/marker/mouse-yield build-only.
- 2026-11-01 remaster (`gfx/remaster-render`, wt-mp2-remaster, not merged/pushed): sun shadows 42fefff14; Poco lag
  (15 fps, GPU-bound 39 ms, MSAA4 16F HDR) fix 068261f04 drops dead MSAA stores/loads (GOTCHAS "Scene-HDR pass ops"),
  verified on Linux; APK built, NOT installed (ask first). Next: install, measure (`env.MP2_GPU_TIMES_LOG=1`,
  SurfaceFlinger), then maybe non-MSAA/lower-res HDR on mobile and a pipeline seed for the branch.
- 2026-11-02: #51 (unscannable pulsing scannables, 0.8.0) fixed in 04d51b497 (scan membership bitmap was 1024 bits
  vs 2048 slots); pushed, not run-verified, no issue reply yet. #52 (Mine Shaft enemies after a state load)
  fixed in e7fc04a73 (stale `mPortSpawnLayers` rebuilt a layer the saved room lacked); repro + statecheck ok; pushed, no reply.
- 2026-11-02: #58 (Android: pad left stick strafes with Twin Stick off) fixed 1cc820cff + df4fe1b35 (touch aim and
  touch-layout LB chords only while touch is the last-used input, MP1's TouchDirectAim); pushed, compile-only, no reply.
- 2026-11-02: #54 Morph Ball camera control committed 1f619a3fd, pushed (`ball_cam`, `ball_cam_speed`; mouse/twin stick/touch/C-stick),
  no issue reply; C-stick and twin stick verified headless; real mouse/touch, MP and Spring Ball not run-checked.
- 2026-11-02: #50 (ball-HUD boss bar off its bracket at wide aspects) fixed in 54d8e18d8 (boss tree anchored at the name), pushed;
  verified 21:9 ball + combat; no reply.
- 2026-11-02: #36 light world music in Dark Aether committed 46f550877 (`dark_world_light_music`, F1 Enhancements > Music;
  6 dark area themes swapped, boss/event tracks kept); headless Dark Agon run plays sand_soto.dsp; pushed, no reply.
- 2026-11-02: #59 Chykka spits never spawned (spit WPSC token unlocked, TryCache fails) fixed bfdb60dd9; headless smoke 0 -> 68
  projectiles; pushed, no reply. Audit: every other enemy/ScriptActor projectile info that reaches TryCache is locked
  (beams take the token directly), so no other instance.
- 2026-11-02: #60 Dark Visor red layer stuck at the 4:3 spot at widescreen fixed 5299105bd (frame centered);
  verified 16:9 Linux; 21:9 and the reporter's full-red fill (D3D12?) not reproduced; pushed, no reply.
  Follow-up (red outside the lens): the port never drew the lens mask (drew the frames' roots, which draw
  nothing; retail draws the HUD camera, then `model_dark_outermask`), and the frame now shrinks with the
  HUD scale like the mask; verified hudscale 70/100, 16:9 and 4:3. Pushed bacf42f9d; no reply posted yet.
- 2026-11-02: #61 (stuck in the Dark Torvus Temple elevator after a state load) fixed c313036e9: Samus re-placed once
  her area's room state is applied (she fell through the unrestored platform during the wait); headless repro ok;
  pushed at 0bf0b6c36, no reply. Unexplained: reporter's resume state had no layer-10 objects (elevator missing; saved spawn 10201).
- 2026-11-02: debug warp to a save-station room (Dark Falls) stayed black: fixed 0bf0b6c36 (PortWarpSettle settles the
  frame the area loads, dispatches the spawn's script chain before Think, keeps the area Alive); headless 23/14/47 ok; pushed 0bf0b6c36.
- 2026-11-02 Sweep A9 closed (plan all ticked, AUDIT.md A9-100..806, STATUS.md): game fixes A9-100/200s/300s/500/600/601/700/701,
  retail_diff default findings 0 (48 allowlisted, regress 33/33); pushed at 0e229952f.
  A9-700/701 (CCubeMaterial) unverified on screen; lane slots free.
- 2026-11-02: #46 Battleground fight ended after 4 Ing kills: CIng::Death resent RATL when already dead (knock-back
  DETH follow-up), 'Dead Ings' counter hit 6 early; fixed 485a11e89 (retail gates the send on alive). Build-only, pushed,
  no reply (user). Audit: GunTurretTop/Rezbit/OctapedeSegment ungated Death sends match retail asm; other death sends one-shot.
- 2026-11-02: debug warp to Battleground put Samus in a closed pocket under the floor (its first spawn); fixed 88d085415
  (`PortSpawnEnclosed`: open active spawn beats a boxed-in first one); screenshot-verified; pushed.
- Open: retail Puffer has no engaged state.
  Ask before removing wt-mp2-sc-harness, wt-mp2-sc-los, any `wt-mp2-*`.
- 2026-11-01: 0.7.0 released as latest (tag v0.7.0 = 96e402911, soak 96/96 ok; docs/RELEASING.md). #24 untested on real Android;
  iOS/macOS untested on hardware. No reply on #17 (user's choice). Audit 2 (A8) closed; A8-17/25 unverified on screen.
  Whole-game pipeline seed embedded (a3b5bdbcf, 1624 pipelines; RELEASING.md "Pipeline cache seed"); gaps: Options
  subpages, ending areas, MP, late Temple Grounds shard timed out. Android embeds it too (2b02ff618; 1608 rows
  queue at launch, 1040 pinned GLOBAL); pushed, android.yml run 41286788307 pending. #44 water flicker on splash
  not reproduced on Linux; likely-cause fix e8bdc6f99 (own EFB copy dests for fluid + particle copies), pushed; no reply posted. #46 (Battleground): save-state layer flags fix pushed 11474ec6a; fight replayed in port matches SCLY (key event only at 6/6 Ing, no live spawn), so Bug A is likely the same savestate bug; no reply posted (user).

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
