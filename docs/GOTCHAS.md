# Gotchas (port branch)

Things that took real effort to find out. AGENTS.md keeps a one-line index; the detail lives here.


- **`tools/link_check.sh` can report "nothing to relink; re-reading the existing link result" even
  after a source changed** (when ninja has nothing to do it keeps the previous `build-port-link/build.log`
  and re-derives the counts from it, so a tree someone built by hand keeps the old numbers). The
  runnable binary is `build-port-link/metroid_prime2_port` - the *full* build (`MP_SDK_HEADERS_ONLY=OFF`;
  build-port is the same sources with `MP_SDK_HEADERS_ONLY=ON`, which builds every target against
  Aurora's headers but pulls in no Aurora and links no executable, `CMakeLists.txt:599-602` -
  `platform/` compiles there too, `mp_platform` is a target in both), so build it directly:
  `$T/ninja -C build-port-link -k 0 metroid_prime2_port`. Check the binary's mtime against your
  sources before trusting a run.
- **Do not launch a tool a lane is editing.** A subagent mid-write in `tools/` breaks a run you start
  from there: a shard launched while `run_gameplay.sh` was being edited died with
  `line 76: LAY_NUM: command not found` and `exit=127`, which reads like a game failure in the sweep's
  own summary line. `git status --short` before launching, and a lane editing `tools/` should own it
  until it reports.
- **`pgrep`/`pkill -f` match your own shell**: the process name is truncated, so match the full
  command line with a bracket (`pgrep -af 'metroid_prime2_por[t]'`), or `pkill -f '<pattern>'` kills
  the shell that is running it. The bracket only helps when the pattern is the *only* place the name
  appears in the command line: a guard like `pgrep -f 'Xvf[b] :86' || (Xvfb :86 ...)` both matches
  itself (the literal `Xvfb :86` it is about to start) and needs no start, so the display silently
  never comes up - use `pgrep -x Xvfb -a` and check the display with `xdpyinfo` instead.
- **A patterned actor crashes on its first Think unless its body controller is activated**
  (`CPatterned::ThinkAboutMove` dereferences a null `CBodyStateInfo::GetCurrentState()`; only
  `CBodyController::Activate` installs one). See Status, and `PortCSpacePirate.cpp` for the shape.
- **A resource in no loaded pak is served as *empty bytes*** (`CResLoader::WarnMissingResource`, and
  the report now names the resource's type). Parsers of such a resource read zeros: an animation
  source's frame count comes out 0, and anything that divides by a parsed count must guard it under
  TARGET_PC (`CAnimSource` did not, and an integer divide by zero ended a run). When a sweep sees a
  stream of these after a world change, the question to answer is *which pak family* is missing.
- **A save load comes up with ~1008 of the 1024 object ids allocated** and only settles near ~350 a
  few hundred frames later (`CStateManager.cpp:5420` has the same numbers; `MP2_DEBUG_IDS=1` prints
  the count every 300 frames). A warp that lands in that window can exhaust the pool; `PortWarpSettle`
  waits for 256 free and drops the areas around the player, which is what frees them (the first
  threshold, 512, was tuned for the boot spike and blocked ordinary hops - 499 of 526 waits in one
  run were for an unreachable area).
- **The console's heap is zeroed and the host's is not**, and audio code depends on it: MusyX's
  `DSPvoice` array is `salMalloc`'d and `studio` is written only by `salActivateVoice`, but the
  synth's zero-offset pass calls `hwSetVolume` for a voice that has never sounded, so an
  uninitialised `studio` indexed `dspStudio` past its 8 entries - that was the intermittent
  audio-thread SIGSEGV in `hwSetVolume` (`ed5b2518a` fixes it with the zeroing the console had plus a
  guard). Suspect the same assumption in any other ported allocator that hands back memory read
  before it is written.
- **Never put a run pattern and the binary's literal name in one shell command**: `pkill -f
  '<pattern>'` matches the shell that is running it, so the command kills itself (this cost two
  builds and a launcher). Use `pgrep -af 'metroid_prime2_por[t]'` and put launch commands in a script
  file (`/tmp/launch_sweep.sh`) instead.

- **A host-generated vertex array must not go through the guest array path.** `CGX::SetArray(attr, data, stride)` is the *disc* path: it passes a size of 0 and marks the data big-endian, so Aurora uploads nothing or byte-swaps it. Anything the port computes itself (the swarm's skinned positions and normals, for instance) needs `CGX::SetArrayNative` with its real byte size, plus `CGX::ClearArray` first when the pointer is reused, since the backend's cache keys on pointer and size and the array is rewritten in place. `CSwarmDisplayList::Draw` had this wrong and every boid in a swarm drew as stretched slabs and strands until `23c928626`.
- **A port guard must cover the helper too, not just the call site.** Adding `SwarmModelReady` and an `if` in `src/MetroidPrime/Enemies/CSwarmBasics.cpp` without `#ifdef TARGET_PC` moved the upstream matching build's *matched* percentage from 63.29% to 63.25% - under one file, which is why "87 files OK" still printed and why the number has to be read after every upstream edit. The cheap check: strip `#ifdef TARGET_PC`/`#else`/`#endif` from the file and diff against the pre-session revision (`git show <base>:<path>`); the non-PC text must be identical.
- `orig/G2ME01/{files,sys}` are symlinks to the archive tree's extracted disc (git-ignored).
- musyx-port's typedefs clash with Aurora's unless `MUSY_TARGET=MUSY_TARGET_PC` (set in CMakeLists.txt).
- `src/MetroidPrime/PortLinkStubs.cpp` is generated reach stubs (`asm("<mangled>")` entries,
  blank-line separated; `reachdata_N` zero arrays for data). When a real definition lands, delete
  its entries or the link reports duplicates. Zeroed data stubs crash at runtime: non-zero
  globals belong in `src/MetroidPrime/PortDataGlobals.cpp` (values from retail .sdata).
- GCC "specialization after instantiation" in rstl: declare the specialization in the header's
  `TARGET_PC` block (e.g. `include/rstl/string.hpp`), not the .cpp.
- Host objects outgrow retail sizes: hard-coded `Alloc(0x20)` etc. need `sizeof` under TARGET_PC.
- `src/MetroidPrime/PortGlobals.cpp`, `PortBoot.cpp`, `PortReachStubs.cpp` are old-tree files
  not built; mine them for values only. So is `platform/debug_ui.cpp`: it is in no CMake target
  (`CMakeLists.txt:690` says why: the F1 overlay is built in `mp_port_audio` instead), so editing it
  changes nothing and a setting added there will not link. The live F1 overlay is two pieces:
  `platform/debug_ui/*.cpp` is the window and its pages (`CMakeLists.txt:661-666`, built into
  `mp_port_audio`, :650), and
  `platform/port_overlay.cpp` holds the `PortOverlayState` knobs and the game-facing API behind them;
  it keeps no settings file at all (every knob is an `MP2_*` env var). `tools/boot_probe.sh` is the
  build that enables `MP_BOOT_STUBS=ON` itself (`:78-82`), so `PortReachStubs.cpp` is linked there and
  nowhere else (`CMakeLists.txt:141-156`).
- Upstream code casting pointers to `u32`/`uint` fails on 64-bit; most excluded units are that.
- Disc data is big-endian: every raw read of resource bytes needs `CBasics::SwapBytes` under
  TARGET_PC (material data, CMDL/TXTR headers, compressed-block lengths in `CResFactory`).
- **Legacy port saves have host-endian CRC/generation words; console, Dolphin and new port writes
  use big-endian** (issue #3, 2026-10-29). `port_save_framing.h` makes the runtime loader accept
  either CRC order and decode the generation in that order; writes always use console order.
  Checking both orders only in the import inspector did not fix the game's card loader. Never swap
  the payload, and reject generations other than 0/1 before using their XOR as a slot index.
  In the import inspector (`platform/port_save.cpp`), the newer of the two copies is
  the one with the greater slot *timestamp*, not generation: the generations alternate 0/1.
- **Saving at a save station pauses the run, and a paused game runs no update.**
  `MP2_INPUT="STATION@100,A@220+3,..."` gets into the save screen and its next A writes the card
  (that is how the card-backup hook was verified), but from then on the port logs
  `[harness] no game update` and `MP2_FRAMES` never arrives. The console still answers while paused
  (since `d82b8002b` `CMFGame`'s timer tick serves it whenever the state manager's update is
  skipped - pause, portal transition, cinematic skip; `PortConsole::IdleTick`, `CMFGame.cpp:194,204`),
  so it is a command that needs an update, not the console, that has to wait for a restart.
- **A model's position/normal arrays are disc data in memory**: the GPU's guest array path swaps
  them, so anything that skins or measures them on the *CPU* has to byte-swap each component first
  (`SwarmRenderHelpers.cpp`, `CSkinnedModel::GetSkinnedPosition`). Read raw they are NaNs and values
  near 1e6 - which the desktop drops and the phone's GPU draws as one enormous gray triangle.
- **Freeing a texture's data while Aurora's FIFO thread is still resolving texture objects is a
  `resolve_static_texture` SIGSEGV** (seen on the phone from the scan visor and the map screen): the
  game's GX is synchronous and the port's is not, so every free the game assumes is safe needs a
  `GXDrawDone()` first. `CTexture::UnloadBitmapData` (before the `CARAMToken` is dropped) is the one
  choke point for texture data; `CInGameGuiManager::DestroyAreaTextures` needs one too.
- `link_check.sh` after a direct `ninja` run reads a stale `build-port-link/build.log` and reports
  false undefined counts; touch a source first. `drop_defined_stubs.py` crashes on missing .o paths.
- **A sweep is only as good as what it walks.** The post-fix sweep (`TAG=postfix`, 4 shards) came back
  with 48 of 53 reached areas `OK` and none of them a crash - 14/21/35 among them - and area 46 in
  `STALL` with `[warning] [aurora::gpu] Device lost: Device was destroyed` just before it, which is
  what four parallel software-Vulkan shards do to the renderer rather than a game fault. Check a lone
  `AREA=46` run before believing that one.
- **A dead player is what "the run stopped updating" usually means.** `MP2_GODMODE=1` (F1 > Cheats; `tools/sweep_areas.sh` sets it, `GODMODE=1` for `run_gameplay.sh`) keeps it out of a run. `PlayerDied` -> `CMFGame::EndGame`
  -> `kRM_EndMovie2` -> `CMainFlow` puts `CPlayMovie(kWM_LoseGame)` on the IO stack, a "continue from
  last save" screen that takes no `B`: the loop keeps rendering, no game update runs, and the harness
  reports "no game update for 30 s". Dark-world areas (21 and 35 in the Temple Grounds, and 13 and 28
  beside them) kill a suitless player in about 19 s, sooner than one `HEAL` per area keeps up, so a
  sweep has to heal twice per area and read `[player] killed ...` in the log. This is what the old
  "areas 14/21/35 stop updating" wall was (`05efe2ecc`). Whether `A` picks "continue" there is still
  open - if it does, the RELOAD verdict's "walks into a save station" story below is wrong.
- **A test script with no button tokens stalls the first time an area shows a memo, and a script that
  presses `A` walks into a save station.** An area's script (`CScriptHUDMemo`, and `CPlayer`'s suit
  messages) calls `CStateManager::ShowPausedHUDMemo` -> `kSMT_MessageScreen` ->
  `CMFGame::EnterMessageScreen` -> `SetFlowState(kFS_Paused)`, and a paused game performs no
  `CStateManager::Update`, so `MP2_FRAMES` never arrives and only the timeout ends the run (the
  harness's `no game update for 30.0 s` is the only line that says why - and note that
  `CMFGame::OnMessage` is then not called at all, so a breakpoint there proves nothing about *which*
  screen is up). The token counter in `MP2_INPUT` keeps advancing while paused, so a periodic press
  dismisses it - but choose the button: an arrival places the player at the area's first spawn point,
  which is often inside a save station's trigger volume, and `A` is the *action* button
  (`CScriptSpecialFunction::AcceptSaveStation` takes `kSM_Action` -> `EnterSaveGameScreen`), so a
  sweep that presses `A` twice per area quits to the front end, the port's skip re-boots slot 0, and
  the run then walks `NEXTAREA` from the save's own area with the update counter back at 0 - measured
  2026-10-17 in three of four shards, and it read as "STALL in areas 14/21/35" until
  `tools/sweep_report.py` learned to call that RELOAD. `B` is the cancel button: `tools/sweep_areas.sh`
  presses it twice per area. The three traps compose: `START` pauses the game, *no* buttons leaves it
  paused, and `A` leaves it in a save screen.
- `MP_PORT_FRAMES` is only read by the unbuilt `PortBoot.cpp`: the current loop never exits, so
  `timeout` ends boots (exit 124/137 = still running, not a crash). The fault handler prints
  `port: crashed: <signal>` + stack and breadcrumbs on real crashes, **and on an external
  `kill -SEGV`** (`platform/main.cpp:1174` installs `PortCrash::Install`, `platform/port_crash.cpp`,
  for SEGV/BUS/ILL/FPE/ABRT/TRAP, and re-raises with the handler that was there before, so the exit
  code still says "died on signal 11"; Android's crash buffer also gets `[port] caught signal N`,
  `platform/main.cpp:1046`, which is the line `tools/sweep_report.py` and `mpsoak.py` grep for).
  That makes it the way to sample a *hung* process without ptrace on this
  machine (`/proc/sys/kernel/yama/ptrace_scope` is 1, so `gdb -p` is refused - launch the run under
  gdb and SIGINT it instead, `tools/gdb_interrupt.py`): SIGSEGV the stuck run, and the backtrace lands
  in its log, which is how the two update stalls in the Temple Grounds sweep were placed in
  `CGraphics::EndScene`.
- On LP64 `u64` is `unsigned long`: `CInputStream::Get<unsigned long>` reads 64 bits under
  TARGET_PC (a 32-bit read there broke COBBTree materials). Spell 32-bit fields `uint`.
- Allocator pools scale by `kAllocatorPointerSize` (8 on host): retail fixed buffer sizes overrun.
- The default font (`gpDefaultFont`) is read from the mounted disc's boot DOL at run time
  (`src/MetroidPrime/PortDefaultFont.cpp`: 16-byte prefix scan + SHA-256 per blob, so USA, PAL and
  Trilogy's `rs5mp2_p.dol` all resolve; no `DefaultFont*.inc`, no retail bytes in the binary, and the
  host build no longer needs the upstream build first). A miss logs `port: default font: ERROR ...`
  and leaves `gpDefaultFont` null: `CSplashScreen::Draw`, `CErrorOutputWindow::Draw` and the camera
  filter's placeholder label return early under TARGET_PC. A new host user of `gpDefaultFont` needs the same test.
- The GC compiler has no C++ standard headers: `#include <cstdio>`/`<cstring>` build on the host and
  break the matching build ("the file 'cstdio' cannot be opened"). Use the C headers and guard them,
  e.g. `#ifdef TARGET_PC` / `#include <string.h>` / `#endif`, and run the matching build after every
  change that touches a shared header or a port edit in an upstream file.
- **`PADSetPortOverride` is a vendored-Aurora addition** (`extern/aurora/lib/dolphin/pad/pad.cpp`,
  `include/dolphin/pad.h`): pad 0's status comes from the input engine
  (`platform/input/port_input_devices.cpp`, polled in `PortPumpEvents`), and Aurora then skips its
  own controller/keyboard read for the port but still merges the virtual pad (console, `MP2_INPUT`,
  touch) on top. A re-vendor of Aurora must keep the hook, or pad 0 silently loses the keyboard.
  The override is one-shot (the next `PADRead` consumes it), so every game tick re-arms it
  (`PortRearmPad0`, called in both `UpdateTicks` loops); a new loop that reads the pad needs the
  same call, or its reads see `PAD_ERR_NO_CONTROLLER` and `CDolphinController` clears every button.
  Keyboard bindings live in `<user>/controls.toml`; `keybinds.cfg` is no longer read.
- **The game's EFB copies all land in one spare buffer, and Aurora resolves each copy into a texture
  keyed by the destination pointer** - so two consumers copying into that buffer in the same frame end
  up sharing one resolved texture, whichever copy was recorded last. The scan visor's A8 palette-index
  copy and the scan window's RGB565 pane copy were exactly that pair: the highlight colours on
  scannables flashed between the two. Give a copy its own destination with
  `PortEfbCopy::Destination(shared, w, h, texelBytes)` and use that pointer for the `GXCopyTex`, the
  `Request`/`Fill` and the texture object. Do **not** instead bind the texture to a copy of the bytes:
  Aurora then samples the guest buffer as tiled C8/RGB565 and the overlay turns into diagonal stripes.
  `MP2_DEBUG_SCAN_PALETTE=1` reports the palette-index map's foreign bytes (`bad=`), which is how this
  was measured (~34,500 of 356,608 on half the frames before, 0 after).
- **A REL's BSS string is filled by the module's static initializer, so "the module only reads
  it" does not mean it is empty.** The ScriptPlayerTurret's muzzle locator (`lbl_63_bss_C`) is a
  static `rstl::string` that `fn_63_376C` builds from `lbl_63_rodata_60 + 0x26` =
  `muzzleend_LCTR` (0x00003788); reading the BSS as zero bytes gave the port an empty locator
  name and a muzzle transform off the base actor. The initializer is a separate asm file:
  `grep -n bss_X auto_fn_*_text.s` finds the function that fills it.
- **Other vendored-Aurora additions** to keep on re-vendor: the partial `GXCopyTex` clear
  (`recording.cpp`, from upstream aurora 9c0bf66f1e), the dstAlpha alpha-only pass
  (`command_processor.cpp` `push_gx_draw`, `67f337a57`) and `AURORA_PRESENT_EVERY` (`aurora.cpp`
  end_frame) and the empty-string fallback when `SDL_GetPrefPath` returns null for `userPath`/
  `cachePath` (`aurora.cpp` initialize). Windows: the user folder is `Metroid Prime 2 Echoes` (no `:`, which
  Windows rejects); with the old name Aurora's cachePath was null and the first run crashed in the
  Dawn cache's `fs_path_from_string` (win-startup-crash, CI 40572636384).
- **Dawn is MP1's fork, not encounter/dawn.** `extern/aurora/cmake/AuroraDependencyVersions.cmake`
  pins `Odrannnn/dawn` branch `mpp`, release `v20261029.114630` (`118436b3`, bumped from
  `v20261026.185535`/`fef22023`), with a sha256 per
  prebuilt; `AuroraDawnProvider.cmake` builds it with `DAWN_ENABLE_DESKTOP_GL` on Windows. Both files
  are MP1's verbatim - copy them from MP1 on the next bump rather than editing here. An existing build
  dir migrates the cached old version on reconfigure. `gpu.cpp` keeps Dawn's blob cache off for GL on
  Linux (Mesa `glProgramBinary` crash) but on for desktop GL on Windows.
  `MP2_GRAPHICS_BACKEND=opengl` on Xvfb (llvmpipe, no DRI3) starts the device and runs the game but
  presents black (MP1 `docs/DEBUGGING.md` says the same); judge a GL picture on a real display. `MP2_GL_VERSION=3.3`
  on Mesa needs no `MESA_*_OVERRIDE`: the Dawn fork caps the context itself (log: `treated as 3.3`)
  and the adapter reports 0 vertex storage buffers and 0 compute workgroups, so both fallbacks run.
  The fallbacks are vendored-Aurora additions from MP1 (frame.cpp/encoding.cpp/shader.cpp fetch
  textures, volfog.cpp/depth_peek.cpp render passes, `g_vertexFetchTexture`/`g_noCompute` in
  resources.hpp): keep them on a re-vendor.
- **Full list of vendored-Aurora additions** (keep all on re-vendor; last re-vendor: MP1 174975c2c, three-way on base 11e96d280, branch `sync/aur1`): 32 MiB uniform buffer, uniform dedup (compares the previous bytes in `frame.uniforms` or MP1's `spillUniforms`; `MP2_GX_STORAGE_LOG`), efb_copy and scan_peek shutdowns outside the lock, dstAlpha alpha-only second draw in `push_gx_draw` (only a fallback now: MP1's `dst_alpha_mode`/dual-source handles `GXSetDstAlpha`, the extra draw runs when that gives None and the alpha-only config gives Replace, i.e. no dual-source or `AURORA_DUAL_SOURCE=0`), `GXSnapshotDepth`/`GXPeekZRegion` (flare occlusion) (also under MSAA: depth_peek.cpp has a `texture_depth_multisampled_2d` pipeline pair reading sample 0, and `split_pass_for_depth_snapshot` no longer skips MSAA passes; before, flares at MSAA>1 fell back to the EFB-copy test, #35), `GXRequestScanPeekEx` flags (`GX_SCAN_PEEK_DEPTH_ONLY` no alpha warning, `GX_SCAN_PEEK_IF_RENDERED` no forced full frame; Splitter beam), `aurora/perf.h` counters (`MP2_PERF_COUNTERS=1`), `AURORA_PRESENT_EVERY`, `AURORA_RENDER_EVERY` (fifo copy-destination table; ops recorded in a skipped frame wait in `g_deferredOps` and are flushed in order once the frame is rendered or at `enqueue_frame_ops`), copy-texture cache `kSizesPerDest = 4` (MP1 has 16; tablet OOM in the scan visor), Dawn `ImplicitDeviceSynchronization` (`AURORA_IMPLICIT_SYNC=0` = off; without it GPU ring resets; MP1's unconditional block removed), retired-surface reuse, `kGciFolderSerial` (constant GCI-folder serial; MP1 hashes the path, which would orphan saves), SDK `GXAdjustForOverscan` arithmetic, no blit for depth `GXCopyTex` (encoding.cpp `isDepth`), scaled A8/R8/G8/B8 `GXCopyTex` copies sample Nearest (encoding.cpp `is_index_copy_format`; index/mask values must not be averaged), and under MSAA>1 those index copies read sample 0 of the multisampled colour attachment (`tex_copy_conv.cpp` `g_indexPipelinesMS`/`index_copy_ms_supported`, `encoding.cpp` `indexMs`, `recording.cpp` `discard_dead_stores` keeps that colour store; Core devices only, a compat device keeps the averaged resolve; fixes the scan palette `bad=` at 4x), `AURORA_HEADLESS` (Null device, no surface, no frame rendered, read-backs dropped, screenshots refused, no host input scan; aurora.cpp, gpu.cpp, fifo, recording.cpp, input.cpp; RUNNING.md "Headless runs"), `copy_tex`/`copy_probe_face` return without a render pass (GXFrameBuffer.cpp; a declined frame's EFB copy read the null frame packet: Android resume SIGSEGV at 0x20; `CMain::RsMain` also skips the scene when `CGraphics::PortSceneOpen()` is false, as MP1's does). Now MP1-native (don't re-add): `PADSetPortOverride`/`PADGetRawState`, partial `GXCopyTex` clear, real `GXSetDstAlpha`. MP1's `MP_*` env vars in aurora are renamed `MP2_*` (`MP2_LOG_TEX_INVALID`, `MP2_LOG_COPY_TEX`, `MP2_PASS_LOG`, `MP2_DAWN_ENABLE/DISABLE`, `MP2_GPU_POWER`, `MP2_STORAGE_CLAMP`, `MP2_RESIZE_WAIT`, `MP2_ACTOR_SHADOW_DEBUG`); redo the rename on every re-vendor. Gx test stubs (`extern/aurora/tests/gx_test_stubs.cpp`) carry stubs for MP2's renderer hooks. `aurora_dvd_set_root(dir, dol)` (dvd.cpp/dvd.hpp/fst.cpp, ported from MP1 575f54d28; Trilogy `MP2/` root + `rs5mp2_p.dol`, docs/TRILOGY.md) must survive a re-vendor.
- **Empty-pass elision (`encoding.cpp` `render`, issue #33).** A single-sampled continuation pass with no draws that loads (never clears) its colour/depth is not begun: its copy reads the attachment the pass before stored. The scan visor's full-screen A8 copy made one such pass per frame (a full-target load and store on a tiler). Keep on a re-vendor.
- **World-shadow copy as a continuation clear (`recording.cpp` `resolve_pass_into`, issue #33).** A clearing copy of a rect (the 103x103 shadow copy) after a pass that only cleared the whole target and drew inside that rect leaves the target uniformly cleared, so it is treated as a full-target clear: the continuation clears with its load ops instead of loading what the pass stored (no full-target store, load or depth store per combat frame). Needs an EFB without alpha (RGB8_Z24) or `clearAlpha`; the pass must have only scissored draws inside the rect. Keep on a re-vendor.
- **Scan peek is throttled (`PortScanTarget::Request`, issue #33).** Every `MP2_SCAN_PEEK_EVERY`-th (default 2, `1` = every frame) call issues the read-back; a skipped call must skip `SetPick` too (the pick has to match the data in the buffers), a new region size always issues. The Splitter beam's `GX_SCAN_PEEK_IF_RENDERED` peek is separate and not throttled. The A8 id-strip `CopyTextureRegion` stays: it is the pass boundary that freezes the id draws for the peek (without it the gun/fog draws of the same pass would land in the ids).
- **Aurora's scan peek (`extern/aurora/lib/gfx/scan_peek.cpp`) is a vendored addition with a
  shutdown fix.** `GXRequestScanPeek` feeds the scan visor and, since split26, the Splitter beam's
  depth read-back (every rendered frame, through `GXRequestScanPeekEx` with `GX_SCAN_PEEK_IF_RENDERED`, so it does not force
  frames rendered under `AURORA_RENDER_EVERY`; a map is nearly always pending at exit). `shutdown()` must free
  the read-back buffers *after* releasing `g_mutex`: freeing one with a map pending runs its
  callback at once, which takes the same lock and deadlocks the exit (`932feb7c4`). Keep both on a
  re-vendor. Its depth words come back byte-swapped (big-endian in memory), like the GX Z16 copy.
- **Scan IDs must not use an averaged MSAA colour resolve (issue #4).** `scan_peek` reads
  sample zero from both the original EFB colour attachment and depth, then renders the requested
  region to single-sample RGBA8/R32Float read-back targets. Alpha is a discrete scan ID: averaging
  samples can invent another target's ID at an edge. Keep both source attachment stores alive
  when a peek follows the pass, even if the next pass clears them. Core devices use the region
  conversion at 1x too: WebGPU forbids partial texture-to-buffer copies of depth subresources.
  Compatibility devices (`MP2_GPU_NO_CORE=1` to test; Android Adreno/Mali, issue #24) use the
  same resolve pass with `[port #24]` shader variants in scan_peek.cpp: compat rejects
  `textureLoad` and non-comparison samplers on depth and any multisampled depth binding, so 1x
  bisects depth (24 steps) with a LessEqual comparison sampler, and 4x reads the already-resolved
  single-sample colour (ids averaged at silhouettes) with depth fixed at the far plane (z24 0xFFFFFF, the Splitter beam's 'no data' value; scan POI placement then lands at the far-clip distance along the sight ray, off-axis corrected by the pick centroid). `write_snapshot` clamps z24 to 0xFFFFFF (float32 rounded far depth up to 2^24, which wrapped to 0). The old compat path (partial depth
  copy, and no peek at all at MSAA) never delivered ids. Test with `MP2_DAWN_DISABLE=skip_validation`
  to turn validation on (release builds hide these errors).
  Preserve this resolve, its encoder input view and the store guards on an Aurora re-vendor.
- **A model shadow's light mask is built from the actor-light array slot, not the area-light index.**
  `CWorldShadow::EnableModelProjectedShadow` does `const uchar lightMask = 1 << lightIndex` and hands
  it to `CCubeModel::EnableShadowMaps`, so the index must be the slot in the actor's light array
  (`CActorLights::GetShadowLightArrayIndex`, `mShadowLightArrIdx`), which is what `CPlayerGun` passes.
  The morph ball passed `GetShadowLightIndex` (`mShadowLightIdx`, the area-light index), a different
  number: the mask came out 0 or named an unrelated light, so channel 1 stayed unlit/white and the
  ball rendered flat bright wherever a shadow light existed (`4cc49dc49`, `CMorphBall::Render`).
  `GetShadowLightIndex` is still the right one for `BuildLightShadowTexture`'s light lookup.

## Exit 13 and the log copier

`PortLogFile` forks a copier (comm `mp2-log-copier`) that tees the game's stdout/stderr to the
terminal and `<user>/metroid_prime2_port.log`. The game's main thread renames itself "Main thread",
so name-based `pkill`/`pgrep metroid_prime2_port` used to hit only copiers, on every run on the
machine; the game then died of SIGPIPE on its next print, silently. uutils `timeout` reports a
signal death as the bare number (13 SIGPIPE, 11 SEGV, 6 ABRT), GNU's as 128+N. Since 62aa05378 the
game ignores SIGPIPE, the copier ignores SIGTERM/SIGHUP, and Aurora logs through a stdio callback
(`platform/main.cpp`): its default `fmt::println` throws on EPIPE inside a noexcept function.
- **Prompt icons bind per `CTexture` by asset id; `register_virtual_replacement(ReplacementKey, ...)` is a
  vendored-Aurora addition** (`extern/aurora/include/aurora/texture.hpp`, `lib/gfx/texture_replacement.cpp`):
  `FTextureFactory` calls `CTexture::PortSetAssetId`, `PortPrompts::BindTexture` looks the id up in
  `kAssets` (platform/port_prompts.cpp) and registers a `TexturePointerKey{CTexture*}` entry that
  matches the `userData` `CTexture` sets with `GXInitTexObjUserData` (re-set in `Load`). A re-vendor must
  keep the key overload. The hint memo's button blinks on purpose (game behaviour): screenshot several
  frames. `kKeys` (texel hashes) still serves the few prompt textures without an asset id.
- **`replacement_usage(registrations)` is a vendored-Aurora addition** (same files): `s_matchedIds` records
  each registration a cache-missing lookup selected (erased with the registration, cleared with the runtime
  state); F1 > Mods shows the user pack's matched/failed counts from it (`PortTextures::UserPackMatches`).
  A pack's file count says nothing: a Trilogy (R3M) pack loads in full on a GameCube disc and matches nothing.
- **Save files were purged from public history on 2026-10-28** (`tests/data/01-G2ME-MetroidPrime2.gci`,
  `tests/fixtures/trilogy/*.bin`): `port`, `ios`, `portable`, `portable-ci` and `winparity` were rewritten with
  filter-branch (upstream commits excluded) and force-pushed; old `origin/port` 3ad668104 is now d4a0e1681.
  Local branches and worktrees cut before that still carry the files: rebase or cherry-pick their own commits
  onto the new `origin/port`, never merge them. The shared `.git/hooks/pre-push` rejects any push that carries
  those paths. The files stay on disk (gitignored); the tests that use them skip when they're missing.
  Backup of the old refs: `../mp2-purge-2026-10-28/pre-purge-all.bundle`.

## No GPL LZO (2026-10-31)

- Upstream vendors LZO 1.08 (GPL) in `src/LZO`/`include/LZO`; this tree deletes both. In an upstream merge
  that touches them, keep the deletion (`git rm`). Their units and `Kyoto/Streams/CLZOSupport.cpp` stay
  NonMatching in `configure.py` (keep that too), so the matching build links the original objects. The port
  decodes with `src/Kyoto/Streams/CLZOSupportPort.cpp` (MIT, checked byte-identical on every disc chunk).
