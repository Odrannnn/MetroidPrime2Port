# MP1 port sync plan (2026-10-23)

What `../MetroidPrimePort` (MP1, 4,529 commits, HEAD `174975c2c`) has gained that this tree can take.
The owner of MP1 is the owner of this port: its code may be copied verbatim, then adapted (TARGET_PC
rules still apply to upstream files). Feature inventory against MP1 is `docs/QOL.md`; this file is
the work list.

## How this was measured

- Aurora: our `extern/aurora` was vendored from MP1 near `2b2dadac8` (2026-09-29; 26 blob
  differences, the closest MP1 tree). MP1 has 353 aurora-touching commits after it; none were
  taken by subject. About 200 are not Remastered/PBR. List: `git -C ../MetroidPrimePort log
  --no-merges 2b2dadac8..HEAD -- extern/aurora`.
- F1 menu: MP2 already ported MP1's frame (page window, theme, sub-tabs, search, pad navigation;
  `0ca0a556b`, 2026-10-18) inside `platform/port_overlay*.cpp`. MP1's menu is
  `platform/debug_ui/*.cpp` (26 files, 12.5k lines). Settings index: MP1 351 rows, MP2 83.
- Platform: ~360 MP1 commits under `platform/`, `android/`, `tools/` since 2026-09-29 in
  portable areas (input, settings, debug_ui, console, savestate, android, log, disc, audio).

## Work packages, in order

1. **F1 menu = MP1's `platform/debug_ui/`** (the request). Copy the directory and
   `platform/port_settings_index.inc` + `tools/check_settings_index.py`; replace the drawing in
   `platform/port_overlay*.cpp`/`port_ui_common.cpp` with it, keeping our settings storage and
   getters (`platform/include/port_overlay.h`) as the backend. Keep MP2-only pages (Cheats, Save
   states); drop pages with no backend here (Remastered, Archipelago, Tracker, Mods until 8);
   a row whose feature MP2 lacks is left out, not stubbed, and listed in the report.
   - F1a: frame + Game, System, Debug, Cheats, Save states, Randomizer.
   - F1b (after 2 and 3): Controls and Video sub-tabs with MP1's full rows.
2. **Aurora resync to MP1 HEAD**, then re-apply our additions (GOTCHAS "Vendored-Aurora additions").
   Fixes: GXCopyTex partial clears, real GXSetDstAlpha, EFB alpha by pixel format, missing colour
   0/1 read as white, TEV packed alpha compare, GX_TG_COLOR/TEXCOORDn texgen, LOD bias and
   minification, GXGetTexObjLODAll, GXSetScissorBoxOffset, SET_REG_FIELD, imgui upload race,
   scissor rounding, overflowing draw dropped instead of abort, missing UV set as zero, THP bounds,
   DVD short-read retry. Perf/memory: pipeline thread pool + idle eviction, GPU-resident vertex
   arrays/display lists, growable frame staging, texture bind-group memo, damaged cache rebuild.
   Features: dynamic resolution, GPU self-test, driver workarounds, low-power adapter.
3. **Input engine sync** (`platform/input/`, gyro via GamepadMotionHelpers): manual/auto gyro
   calibration, smoothing/tightening, gyro spaces and modes; separate vertical sensitivity (mouse,
   touch, gyro); Quake/Source mouse scale and DPI; pointer aim; turbo; Nintendo pads by label;
   port 0 to a pad connected mid-game; lock-on toggle fixes; menu/map rebindable buttons.
4. **Robustness and logging**: hang watchdog (phase, stack), crash report into the log,
   exceptions out of the main loop reported, settings/GPU caps/storage logged at start, console
   and log never block on a stalled reader, disc: refuse an incomplete/wrong image and ask again,
   shader-compilation toast, "Press F1" launch hint, stacked toasts.
5. **Settings plumbing**: one key table with a round-trip test, Reset configuration (F1 System),
   every ini key documented and checked.
6. **Console**: dispatch table, documented `help` checked by a test; `title`, `window`, `kill`,
   `fx`, `status --json`.
7. **Audio**: music/SFX volume and sound mode in F1 Game (if not already wired), music keeps
   playing through a state load, mutes not saved.
8. Later, bigger: pointer menus (mouse/touch in the front end and pause screens; game GUI code),
   suit colours, mods/texture-pack loader, update check (since done: F1 System Updates, c4ab4cb1c), Android touch layout editor additions,
   quick resume.

### MP1 after 174975c2c (to `3dde164eb`, 2026-10-25, 172 commits)

About 120 are Remastered/HDR, Dread Samus import or third-person camera (not portable). The rest:

9. **Follow-ups to merged packages** (small, low risk): dynamic resolution UI rework and review
   fixes (`5cf4ac1f4`, `429fdb7bf`, `278f7923c`, issue #186); Remap Presets button (`ffaa3d4ab`,
   #196); Morph Ball binding rows split from gameplay, touch bindings only where they work
   (`7d8840bb4`, `7beb75226`, `3d1f7cbf6`, `9c2baa5e4`, `4b04a4a61`, #167) and the Android Morph
   Ball touch page (`2034083d7`); lock-on free aim with and without pointer aim (`83ff2ecba`,
   `1881b35d4`, `1b414b923`, #188; extends inp1's empty-lock path).
10. **Aurora/pipelines**: failed pipeline reads as unavailable (`8e81db43e`); lazy pipelines kept
    until memory runs short (`c401d338d`); per-context pipeline use recorded and an area's pipelines
    prefetched as it streams (`98438c0c6`, `02145b77b`; MP2's CGameArea/CStateManager hooks in
    TARGET_PC blocks); Compatibility devices under validation (`e452fc3a1`, only the non-HDR parts).
11. **Game-side fixes, adapted to MP2 code**: fog volumes copy depth, not alpha (`184b9965e`,
    CCubeRenderer, #191); HUD billboard effects spread past 4:3 instead of stretching (`a7edba309`,
    `239f95483`, `91b5c12f5`, `c1106efab`; particle headers); scan window follows pointer aim
    (`312b693c7`, `588b0577e`, `3dc11112f`; MP2's scan visor differs, check first).
12. **Save states / audio** (merges into 7): restore the playing music (`bf86ab937`, `26d51db53`);
    a save mid-cinematic resumes it (`7478ebbbd`).
    DONE (sav1): music (CScriptStreamedMusic restart) and cinematic shot time restored in
    `platform/port_savestate_room.cpp`; no MP2 hold/CScriptMidi hook; audio rows already present.
13. **Game options**: walk speed 50-200% (`62d94159f`); hint delay and Show current hint
    (`319685eed`; only if MP2's hint system has the same hook).

### Remaining QoL (user 2026-10-25: all of it, except the not-portable list)

Each needs MP2 game-side hooks found fresh; a row with no possible MP2 backend is reported, not stubbed.

14. **ptr2 pointer menus, rest**: front end (title/file select, pointer -> CScriptGuiMenu), CAutoMapper
    map gestures (drag, pinch, tap), quit screen.
15. **game1 Game rows**: scan speed, elevator ride skip/speed, on-screen IGT, language, restart to
    menu, Original experience toggle.
16. **vid1 HUD/scan**: cutscene black bars, sharp scan window, HUD opacity, custom crosshair colour,
    secondary crosshairs (scan image spread moved to 23).
17. **vid2 rendering/system video**: sun and projected shadows, HD font, shader-compilation toast and
    F1-hint switches, GPU self-test / renderer / driver pickers (guarded to their platforms).
18. **sys1 System**: Discord presence, disc image check, menu sounds, memory-card import/export,
    data folder panel.
19. **ctl1 Controls**: button glyphs and prompts, swap visor buttons, tap minimap, Reset aim resets both.
20. **misc1**: throughput display and simulation rate, save-state hotkeys switch, Keep room objects,
    console `fx`, pipeline memory-pressure trim (`c401d338d`).
21. **mod2 mods follow-ups**: model reload, combined Mods page with texture packs, Open folder,
    Android mod picker.
22. **spr1 Spring Ball** (user 2026-10-25): MP1's Spring Ball option (jump in Morph Ball; Echoes has
    none natively), its binding, gyro flick and Android swipe-up triggers (`8773bfd0b`, `c35a19db4`
    area; find the rest by `git log -i --grep=spring` in MP1).
23. **hud1 widescreen HUD, all of it** (user 2026-10-25, next free slot): every HUD state spreads
    correctly at 16:9 and 21:9 (and Vert+ below 4:3): combat, scan, dark, echo visor frames, morph
    ball HUD and its viewport reduction, helmet, boss energy, threat/missile clusters, radar,
    minimap, scan text box/bar kept 4:3, scan image spread, message boxes and save prompt keeping
    proportions, front end pillarboxed, HUD lag and HUD scale <100% composing with the spread.
    MP1: `0f1d9b7f3 f359df2d8 c29d9dcc8 284ca7e18 7a7aadd8c fc5bf0b70 ba2db2200 c70260b06 e64c9e4d3
    f420a5862 8ae7c9b44 45ea32a48 cbf3bc8f7 eb6c6a61f 6fa512c72 11bf57639 a653c4390`. MP2 base:
    `6ae31083e`, `9acd7f669` (gaps in docs/STATUS-history.md, 2026-10-19).

24. **tail1** (user 2026-10-25): portable MP1 commits after 3dde164eb to 8451b25ae: mod-reload
    disc-read reset (`5aba2cad4`), Aurora no-pipeline copy skip, MSAA depth copies, GX array check,
    door-pair/enemy-command savestate fixes, analog-L scan lock, gyro free-aim pointer, cheat sliders.
25. **aud1/aud2 audits** (user 2026-10-25): every MP1 TARGET_PC fix to src/ over its whole history,
    and MP1 platform/ fixes before 2026-09-29, classified against MP2 (reports
    /tmp/opencode/reports/aud1_src.md, aud2_platform.md); APPLIES rows become fix packages.

26. **wide1 MP1 widescreen gaps** (hud1's leftovers, 2026-10-26): the 4:3 assumptions outside the
    HUD: splash logos keep their shape, the face reflection's ortho widens with the view, movies fit
    the view with their shape kept, credits text keeps the movie's 4:3 width, a cinematic cut blends
    from its unwidened FOV, the arm cannon is drawn at retail FOV and anchored to the 4:3 bottom
    below 4:3, the scan window keeps its share of the width below 4:3. MP1: `f5796fc2e 10d237a43
    6b7384a85 227c7fdaa 5fb236a91 1a83a296d af5e42ade 629170f37 cfa8e060b`; `7a7aadd8c` (listed
    under 23) is the warp/cinematic half.
    DONE (2026-10-26, merged as `82b768ae8`): `4f84326e0` splash/face reflection, `b0bdb0627`
    movies, `511ba7bfe` credits, `0a01d41a7` cinematic FOV, `cdeb6495b` scan window, `7ebd852b8`
    arm cannon. Warp/cinematic widescreen `7b82b73c9` + `fcd80eb77` (MP1 `7a7aadd8c`); the pause
    and map frames filling a wide window are our own (`a3dbfec15`, `CGuiFrame::SetSpreadPanel`), not
    an MP1 port. Phone verification pending for all of it.

27. **Range `e3d8c8940..7eda62884`** (user 2026-11-01). Already taken: `31a614420 1a4faab94
    e515e1d6b 5c64b3265 94e47b082 503e0e936 a6c092e90 cc3485d7f d57a819a2`; `23f68dc64` came from MP2.
    - **s27a core**: 2048 object slots, 11 index + 5 version id bits (`e1f85fc89`, `e7b97f6a4`;
      regenerates the entity schema, old save states stop loading); pipeline cache refuses rows
      whose shadow pass the shader lacks (`e560a42ee`); adaptive/non-60 Hz clocks (`733b09e03`,
      audit against our `sim_adaptive`); Windows in-process log copy fallback (`898c9b458`);
      Aurora shader-anchor checks (`6effecb93`).
    - **s27b input**: Aim Lock-On nearest the crosshair (`7fca0ee79`); Dodge (`87b0a7e43
      5dff869f3`); Grapple binding/toggle, Spider Ball toggle, Shoot While Grappling (`35c2c420c
      ddf94f1ff 31ffa6430 559266ef8 31f1a0dc3`, #243); hand on cannon (`976e914eb de9281a79`);
      empty lock frees the pointer (`bfaa241d6`).
      DONE (2026-11-01, merged `9e62d3b56`): all five ported; grapple key, Toggle Grapple
      and Shoot While Grappling skipped (Echoes grapples with the fire button), Spider toggle + Grace helper only.
    - **s27c small**: per-visor crosshair toggles mapped to Echoes' visors (`486f7a0df 2d2ba0a5e`);
      morph ball bars option (`535ccd192`); console `freeze` (`aee3e53ac`); `[motion]` dumps
      (`105f65deb`); Android settings top band (`5d83055aa`).
      DONE (2026-11-01, merged `1b8b590a3`): Dark/Echo crosshair toggles (not seen on screen: needs a
      target in view), ball bars verified on screen, `freeze`, `[motion]` (single player); Android
      Java/JNI not compiled or tested.
    - Skipped: Remastered/PBR/effects, third person, Dread suits, Archipelago/rando, `34899e497`
      (Remastered particles), `005ae8a6e` (we have infinite ammo), artifact cheats `904552326
      0dadb6af9`, `881e1e899` (Frigate), `09ea6ad2d` (Trilogy corner layout; check ours if seen).
28. **Range `7eda62884..96529013d`** (2026-11-01). Taken: `8f353245d` world pipeline prefetch (Aurora
    `AURORA_PIPELINE_CONTEXT_WORLD`, an area's rows promoted ahead of its world's backlog; CGameArea
    prefetches the world once per mlvl; seen with `MP2_PIPELINE_LAZY=1`: world context 330 rows).
    Not taken: `e4a0af0bd` + `4ab18ced5` (60 Hz movement at any sim rate): MP2's Sim Hz has none of
    MP1's step-scaling base (`68fd1fb4c` friction, `bcf3065a2` counters, `fa3dfa649` jumps,
    `1b51e5efd`, `733b09e03` clocks/aim), so the force-law fix alone would be wrong; port that chain
    as one package if Sim Hz is to work off 60. Skipped: `22bff9633` (Remastered rider), third person
    `a74355f2f 5047a0b01 559301f5c`, from-MP2 `8a58764bf 112ae170d e971467ac`, `951a93f5a` (we have
    the boosts), `6fb5e0871` (done in `703cf3c85`), `67d13d552` (MP1 seed; ours has pipeline_use),
    `96529013d` release.
29. **Range `96529013d..b6b6f60e8`** (2026-11-01). Taken: `b6b6f60e8` stock touch buttons press fixed
    GameCube actions (`kTouchStockControls`, bound after the user overlay; their Remap rows are gone):
    they pressed the virtual pad, which port 0's preset remaps (Remastered: touch B dead, START = map).
    Turbo's pulse is touch code 33 (A) for the same reason. Console `touchctl` holds a touch code;
    `touchctl b 1` jumps on a new game. Skipped: `c3d086188` (third-person shoulder).

Skipped from this range: Phazon Beam cheat and stacked beam rate (MP1 items), mods page/import
fixes (wait for package 8), decomp matches.

Not portable: Remastered import/PBR/HDR, MP1 boss/enemy fixes, MP1 savestate schema (MP2's object
set differs; the approach can be reused), Archipelago, MP1 randomizer logic.

## Status

Packages 1-29 are merged; the dated per-merge log moved verbatim to
`docs/history/MP1_SYNC-to-2026-10-29.md`. MP1 was last reviewed through `e3d8c8940`
(2026-10-29), then through `b6b6f60e8` (2026-11-01, package 29); take later commits by `git -C ../MetroidPrimePort log --no-merges b6b6f60e8..HEAD`.

Still unrun/unverified per that log (no device or hardware available then):

- Phone verification of the widescreen work (package 23/26), memory logging (`memlog`) and the
  touch editor on a device.
- A real Wii U GameCube adapter, Windows CI with libusb, playing a mod-added `.thp` movie, and
  `memo pause` error paths.
- The Flatpak SDL patch build and a real Xwayland mouse (MP1 `6976247fb` backport).
- Custom-action editor screenshot and touch-overlay actions on device; in-game map invert and
  2x prompt icons on screen.
