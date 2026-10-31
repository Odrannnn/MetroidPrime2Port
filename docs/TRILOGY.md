# Metroid Prime Trilogy (experimental, milestone 1)

The port accepts the Wii Trilogy disc (USA `R3ME01`, PAL `R3MP01`) and mounts its Echoes data,
like PAL: USA 1.00 code compiled natively, the disc supplies only data (`DiscVersion::TrilogyUsa/TrilogyPal`).

- Mount: `aurora_dvd_set_root("MP2", "rs5mp2_p.dol")` (vendored Aurora, ported from MP1 575f54d28) makes the
  FST subdirectory `MP2/` the root and serves `MP2/../rs5mp2_p.dol` (a root-level file) as the main DOL.
- Images: `.iso .gcm .rvz .wia .gcz .ciso .wbfs` (nod reads them; the Trilogy RVZ is Zstd-19, 128 KiB blocks, so first
  reads are slow). Not hash-checked: `port_disc_verify` has no table row, so the disc shows as "unknown disc".
- Saves: card region follows the 4th game-code char (`R3ME` -> USA, `R3MP` -> `EUR/`), same folders as the GC discs.
- Tested image: `Metroid Prime Trilogy (USA).rvz` (3903324160 bytes, rev 0).

## Echoes paks (`MP2/` in the FST) vs the USA GC disc

Resource versions match for every type sampled (MLVL 0x17, MREA 0x19, CMDL v4, ANCS, ANIM, PART, TXTR, STRG, FRME,
FONT, SCAN, SAVW, PATH, DCLN, CSKR, CINF, EGMC, MAPA/MAPW, AGSC): same headers on the same ids; FONT `0x37ff312`
(NoARAM) differs in size only.

| pak | GC bytes | Trilogy bytes |
|---|---|---|
| FrontEnd | 5628864 (146 res) | 1082144 (139 res) |
| NoARAM | 699136 | 866464 |
| Metroid1..6 | 184.0M/42.2M/174.4M/140.8M/134.2M/22.0M | within +-0.8%, same ids |
| AudioGrp, GGuiSys, LogBook, MiscData, SamusGun | | within 1% |
| MidiData, SamGunFx(Low), SamusGunLow, TestAnim | | identical size |
| SamGunFxMulti, SlideShow | present | absent (multiplayer / slideshow cut) |
| RS5.PAK, Strings.pak | absent | extra (Trilogy menu data; `SamGunFxMulti`/`SlideShow` names appear upper-case) |

Missing paks read as empty bytes (guarded), so the absent ones don't fail the mount.

## Boot results (USA 1.00 code, Trilogy data)

- Mount and identify: OK. `MP2_SKIP_FRONTEND=1 MP2_NEW_GAME=1` reaches gameplay at 5.0 s (frame 121) in Landing
  Site (world 3bfa3eff); a 2500-frame run ends cleanly (no caught signals, no reach stubs, no crash).
- **First failure, normal front end**: SIGSEGV in `CScriptGuiFrontEndScreen::CollectWidgets`
  (`src/MetroidPrime/ScriptObjects/CScriptGuiScreen.cpp:146`, log `/tmp/opencode/tri_boot.log:252`): Trilogy's
  `FrontEnd.pak` (139 resources, 1/5 of the GC size) drops the multiplayer menu connections the GC front-end layout expects, so an
  `invertIds[i]` index runs out of range. Needs a guard (or a Trilogy-specific front end) in a later milestone
  (superseded: the `TARGET_PC` guards in "Front end on Trilogy data" and "Multiplayer and used save slot" below).
- `PortReadDolResource` (`platform/disc.cpp`, fixed DOL addresses) has no callers in the tree today; if one is added,
  Trilogy's `rs5mp2_p.dol` is a different binary and needs its own address table. DOL-embedded data is otherwise
  unverified (the port compiles all code natively).
- Not checked: audio (`AudioGrp`/`Strings.pak` differences), PAL Trilogy, Wii-only features, RVZ load time.

## Regression (2026-10-26)

GC USA and PAL `MP2_SKIP_FRONTEND=1 MP2_NEW_GAME=1` boots (1500 frames) are unchanged: gameplay at frame 121,
exit 0, no caught signals. `python3 tools/gen_files_cmake.py --check` clean; no upstream `src/` file touched.

## Front end on Trilogy data (2026-10-26)

Trilogy's FrontEnd.pak has no per-player invert menus (`kSS_*` list empty, rumble 4, join 8 entries) and only 3
text panes in the save-slot list (GC: 9), so upstream's `CollectWidgets`/`UpdateSaveSlots` indexed past the lists.
TARGET_PC fix in `CScriptGuiScreen.cpp`: `PortIdAt` turns a missing list entry into `kInvalidUniqueId` (null
widget), and the slot-pane `SetText`s plus the multiplayer invert/rumble users null-check. The non-PC path is
byte-identical (strip-diff clean). Result: health warning -> main menu (Single Player / Options / Multiplayer) ->
file select -> new game -> opening cinematic -> first room, no `MP2_SKIP_FRONTEND`
(`MP2_INPUT=A@1300+3,A@2900+3,A@3400+3,A@3900+3,START@4300+3,...`, ~30 updates/s). Multiplayer and used slots: see "Multiplayer and used save slot" below.
Screenshots from the run: `/tmp/opencode/trilogy/shots/`. GC USA still reaches title and gameplay.

## Disc picker (Android)

`SupportedHeader` accepts a plain GC header (G2ME01/G2MP01/R3ME01/R3MP01) or a container magic
(RVZ/WIA/CISO/WBFS/GCZ, `port::entry::IsPickableDiscHeader`); nod verifies the disc after open. The picker
filter lists iso/gcm/rvz/wia/gcz/ciso/wbfs. Unit tests: `tests/port_entry_tests.cpp`.

## Area sweep sample (USA-code-on-Trilogy, fast mode, 6 slots per world, 2026-10-26)

`MP2_DISC=<rvz> MP2_LIST_DISC=<GC USA iso>` (`tools/world_areas.py` cannot read an rvz; `MP2_LIST_DISC` points
the slot lister at a plain ISO), `MP2_NEW_GAME=1`, `SHARDS=1 COUNT=6`, WORLD = each campaign world.
Result: 30/30 slots OK (Temple Grounds, Agon, Torvus, Sanctuary, Great Temple, slots 0..5), no crash, no stubs.

## Multiplayer and used save slot (2026-10-26)

Multiplayer (Trilogy USA, desktop): main menu, two held `SDOWN@f+40` steps select Multiplayer. Pads 1-3 are driven
with `MP2_INPUT` prefixes (`p1.A@f+3`, docs/RUNNING.md "Driving pads 1-3"); two pads join and the start works from
pad input alone. The earlier "single pad cannot start" was the pad-count gate (`UpdateControllerCount`), not a
missing MAXR/ZERO message (inference refuted; no SetToZero workaround needed). Arena is picked naturally
(`SRIGHT` on the arena screen: world 7e19ed26 instead of the first, a50a80cc); the `mTeleporters[0]` fallback is
removed. Split-screen 2-player deathmatch: `/tmp/opencode/trilogy/shots/mp2p_deathmatch.png`.
Real crash found by the trace: Trilogy FRME v5 text panes carry 9 more bytes (2 ints + 1 bool, behaviour unknown)
after `scaleToViewport`; `CGuiTextPane::Create` skips them for `version >= 5` (TARGET_PC).
TARGET_PC guards: `CScriptGuiScreen.cpp` `StartMultiplayerGame`/`UpdateUnlocks` (null switches/menus/teleporters;
the `[mp] start:` stderr line was dropped in `d90bd61c3`), `CWorldLayerState::GetLayerCount` (arena worlds have no SAVW layer table).
GC USA 2-player start was not reproduced through the GC front-end wheel (stick-direction selection, would not
reach Multiplayer in 5 attempts); GC FRME is v4 so the text-pane change does not touch it.

Used slot: save via `MP2_SKIP_FRONTEND=1 MP2_NEW_GAME=1` + `STATION@300,A@500+3` (card:
`USA/Card A/01-R3ME-MetroidPrime2.gci`, region follows the 4th game-code char). Trilogy's file select then shows
only "Samus 1": that earlier "all three panes" was stride-3 indexing writing slot 0's play time and world into
slots 1-2's title panes. Trilogy's pak has no world/time panes (the six GC ones do not exist), so
`UpdateSaveSlots` adds a second `CScriptTextPane` (`PortAddDetailsPane`, 0.5 scale, 2 heights below the title, followed each frame by `CScriptGuiFrontEndScreen::Think`) with the play time and the world flattened to one line; "Samus 1" stays where it was (2026-10-28, `fix/filesel-info`). GC (G2ME) file select is unchanged: `/tmp/opencode/filesel/d/gc_filesel_after.png`. Loading it reaches Temple Grounds
gameplay (world 3bfa3eff, area 0, 6500 frames). The GC USA side-by-side was not done (the GC ISO uses G2ME and
rejects the R3ME gci). Shots: `/tmp/opencode/trilogy/shots/mp_*.png`, `slot_*.png`.
Gotcha: launch under Xvfb with `WAYLAND_DISPLAY=` empty, else it hangs at "[aurora::gpu] Requesting adapter".

## Native desktop beam / visor selectors (opt-in)

`trilogy_radial_selectors=1` in `settings.cfg` (or `MP2_TRILOGY_RADIAL_SELECTORS=1`) enables
the native Echoes RS5 selector frames. Default **off**. F1 > Controls > Options > Aiming exposes
**Native Trilogy radial selectors** only with a Trilogy disc. RS5 is registered only on Trilogy;
the existing combat/helmet/passive HUD is not replaced. Missing frames, declared textures,
required widgets or incomplete loading disable capture and log `[radial] ... unavailable`.

**Latch rules (keyboard/pad buttons; MP1 `d7239a04e` radial half).** A wheel opens only after its
button has been held 0.175 s (wall clock, `PortRadial::Latch`), and only while eligible
(`SetEligible`, from `PortRadialHud.cpp`): first-person camera, unmorphed, not in a turret or
cinematic, and `CPlayerGun::PortRadialReady()` (morph wipe done, no beam change, no damage or strike
reaction); the beam wheel also not in the Scan visor. A latched wheel that turns ineligible is
cancelled without selecting, and re-latches if the button is still held once eligible again (as MP1).
A shorter tap opens nothing. Touch sessions and console `radial beam|visor` bypass the delay.
Centre release still selects Power Beam / Combat Visor. Not confirmed in the Echoes Trilogy DOL
(`/tmp/opencode/reports/radlatch.md`): it is MP1's rule applied to MP2's own state.

Bind **Beam menu (hold)** and **Visor menu (hold)** in F1 > Controls > Remap, under Beams/Visors,
for keyboard or gamepad. They are deliberately unbound by default: no custom binding is replaced.
Use the normal Press (while-held) trigger, not Tap/Toggle. Choose an unused key/button, or explicitly
remove its conflicting gameplay binding yourself. Example additive `controls.toml` profile:

```toml
[[profile]]
name = "Radial menus"
match = ""
[[profile.bind]]
action = "beam_menu"
inputs = ["key:F6"]
contexts = ["gameplay"]
[[profile.bind]]
action = "visor_menu"
inputs = ["key:F7"]
contexts = ["gameplay"]
[[profile.bind]]
action = "beam_menu"
inputs = ["pad:rightstick"]
contexts = ["gameplay"]
[[profile.bind]]
action = "visor_menu"
inputs = ["pad:leftstick"]
contexts = ["gameplay"]
```

Add the binding entries to your existing base profile rather than adding a second empty-match
profile if you already have one. Mouse aim must be enabled to provide captured relative motion;
the mapped C-stick/right-stick vector works with or without twin-stick aim. Hold a menu, point,
then release to request the owned item through the existing retryable beam/visor path. Returning
the selection stick to neutral selects the center. After release a still-deflected stick is drained
until neutral, so it cannot also trigger a GC beam change or turn the view.

| Position | Beam | Visor |
|---|---|---|
| Center | Power | Combat |
| Upper | Annihilator | Scan |
| Lower left | Dark | Dark |
| Lower right | Light | Echo |

These are the actual FRME icon positions, not Prime 1's item order. Center **selects**, it does
not cancel. Releasing on the current item is a no-op; unowned icons/highlights are hidden and their
regions cannot commit. An already-open wheel wins conflicts; Beam wins simultaneous first presses.
Relative mouse travel is normalized at 200 counts/radius, center radius 0.35. Fade/highlight timings
are donor-inspired port choices, not proven Echoes retail timing/colors. GUI proportions use the
existing MP2 camera projection policy without horizontal text/icon scaling.

Only player 0 in single-player, alive, unmorphed first-person running gameplay can capture
(not in a turret or photo mode).
Pause/map, F1, cinematic, death, morph, world teardown, disabling the setting and unavailable assets
cancel without committing. Cancellation requires a fresh menu press before reopening. Normal
simulation continues; no global pause is added. Mouse tick movement, pointer edge-turn, pending
view interpolation, normal C-stick and direct/cycle item actions are suppressed during capture.

### Deterministic console harness

With the setting enabled and `MP2_CONSOLE=<port>`:

* `radial` reports capture, eligibility, active kind (0 beam / 1 visor / -1 none), vector and fade fractions.
* `radial beam <x> <y>` / `radial visor <x> <y>` holds a normalized selection (X right / Y up).
* `radial release` releases that hold; `radial cancel` tears it down without selection.
  Cancel also invalidates released-but-unaccepted radial retries and queued gameplay requests;
  it never rolls back a selection the game already applied. Disabling the setting invalidates
  these requests immediately, even when re-enabled before the next input poll. Ordinary touch,
  direct and cycle selection requests are not withdrawn by radial cancellation.
* `radial mouse <dx> <dy>` injects relative travel through the existing captured mouse path.
* `radial pad beam <x> <y>` / `radial pad visor <x> <y>` simulates raw right-stick vector plus
  right/left stick click **through the input engine**. Requires the example pad bindings above.
  `radial padrelease` releases the click but keeps the stick vector; `radial padoff` then returns
  the simulated pad to neutral. This is an explicit test override, not a physical device install.

Use `at <frame> <command>` / `tools/drivers/console.py` for deterministic sequences; leave several
updates after release for the game's ordinary switch transition. `give` can grant test inventory.
Do not combine forced normalized selection with physical holds when testing arbitration.

No new cursor model is drawn.

## Native touch selectors (opt-in, device visuals unverified)

With the same `trilogy_radial_selectors` setting on, Trilogy gameplay/assets eligible, the Android
overlay's **existing Beam and Visor wheel buttons** now hold the native selector. Existing added
`beam_menu` / `visor_menu` buttons use the same path. No bindings, saved layouts, button positions,
touch options or defaults are changed. With the setting off or native assets/gameplay unavailable
on finger-down, stock buttons keep their old wheels (including their old center-cancel/Scan-tap
behavior). An in-progress gesture never changes implementation midway.

Slide from **that finger's down position**: 72 dp is one full radius, X right / Y up, independent
of window aspect and button placement/scale. Native center selects Power/Combat, the three outer
directions follow the table above, and unowned/current choices do nothing. The selector finger
does not also press an engine binding, fire, aim, pan the map or finish a legacy wheel. The old
four-sector overlay is hidden for a native gesture; only the real RS5 frame draws. Movement on
another finger remains independent. A second menu finger cannot steal an existing owner; a busy
menu-button hit consumes only that button gesture, never unrelated touches.

Java/JNI only writes a coherent mutex-protected mailbox with session token + pointer identity.
All PortRadial state, ownership, selection, release and retry consumption remain game-thread-only.
Short down/move/up gestures between polls retain their final point and release exactly once.
Late/duplicate/wrong-owner moves or lifts are ignored, including pointer-id reuse.

Ordinary View teardown (including physical-input auto-hide), ACTION_CANCEL and touch layout/
editor/size/controls changes cancel touch-owned capture and released-but-pending touch requests
only; they do not cancel a keyboard/pad menu or retry when there are no fingers down. Request
provenance survives removal of the Java target. True focus/background loss, F1/pause, disabling
the radial setting and lost gameplay/assets still invalidate all radial sources through the
global cancellation epoch. A held/revoked finger cannot commit or reopen on re-enable; it stays
consumed until lifted, and a fresh down is needed. Cancelled native fingers released by the Java
view are not forwarded to SDL as a new look/map gesture. Already-applied selections are not undone.

The C++ touch overlay (`platform/debug_ui/touch_overlay.cpp`, iOS and the `MP_TOUCH_OVERLAY_DESKTOP`
build, since `0c6d1988d`) calls these entry points itself; the desktop console reaches them too:

* `radial touch begin beam|visor <owner>` returns a session token (0 = legacy fallback, -1 = busy).
* `radial touch move|end <owner> <x> <y>` uses that synthetic session and normalized drag vector.
* `radial touch cancel <owner>` / `radial touch cancelall` exercise producer cancellation.

Synthetic desktop tests exercise the native resources/input/request pipeline, not Android
MotionEvent dispatch, density, layout hit-testing or on-device visuals. A real-nod **release APK
was built without installation**; Android end-to-end touch/device behavior remains unverified.

## Trilogy HUD frames (sync/trihud, 2026-10-27)
On a Trilogy disc the combat HUD is `RS5_SamusHud{1,2,4}Combat`, the memo frame `RS5_HudMessageUS` (fallback
`RS5_HudMessage`; layout 0 only), the helmet `FRME_RS5_Helmet` and the turret HUD `RS5_TurretHud{2,4}Combat`
(all in RS5.PAK, names from rs5mp2_p.dol). Ball HUD and boss frames stay GC. Scan keeps the RS5 combat frame and overlays `FRME_ScanHudFlat{,2,4}` (NoARAM.pak, same as GC): retail rs5mp2_p.dol matches upstream there (CHudDecoInterfaceScan ctor 0x800d32a0 loads the table at 0x804a29c0; state transition 0x8019dd..-0x8019de.. reloads only Ball<->Combat, from the RS5 combat table 0x804a5d2c / ball table 0x804a5d74; getter 0x80195518 returns RS5 names; no Trilogy-only scan frame exists in RS5.PAK). MP1's STRG_Hud text,
energy-word and instant-fill commits do not apply: Echoes looks strings up by name, has no energy-word pane,
and `UpdateEnergy` (0x80199774) picks Instant only for |delta|<1e-5. Combat shot: `/tmp/opencode/reports/trihud-combat.png`.

## Trilogy scan lens (2026-10-27)
Trilogy's Scan visor has no rectangular window: `CPlayerVisor::Draw` (0x80170a78) calls `DrawScanEffect` (0x80170e0c), which draws a lens in place of the dim (vignette rings, 360-degree flickering outline loop 0x80171dac-0x801723b0, spiky ticks, rotating arcs; helpers 0x80170cf8, 0x80170b2c) round the pointer. The pane and frame are drawn only while the window is open (gate 0x801723e8; same factor as MP1's `PortScanWindowOpen`). Port: `TriDrawScanLens` (`CPlayerVisor.cpp`, px = `fit`, the MP1 port's lens) plus a full-viewport scan window in `CInGameGuiManager::DrawScanVisor`, so `CCubeRenderer::DrawScanVisor` draws no grey outside rectangles (retail renderer's window size not located). The scan ring, brackets and progress are `CTargetReticles` (above). Shots: `/tmp/opencode/reports/scanhud-trilogy-{scan-idle,combat}.png`, GC unchanged `scanhud-gc-scan.png`.

## Trilogy reticles (2026-10-27)

`CTargetReticles` (`mPortTri`, TARGET_PC + IsTrilogy) draws Trilogy's reticles from RS5.PAK: scan ring and brackets that follow the pointer (no GC scan box), lock fade timer (`CPlayer`), aiming arms, locked and next-target brackets, a locked reticle on every Seeker lock, and the 6-segment scan progress. Pure math is in `platform/include/port_tri_reticle.h` (`tests/port_tri_reticle.cpp`). Confirmed in `rs5mp2_p.dol`: 7 combat CMDLs, 3 arms at 120°, the -60° progress loop (0x8025e438). MP1-derived: colours, 0.15 s fade, arm/centre curves, segment colour/blend. Off-screen targets draw nothing. Shots: /tmp/opencode/reports/tricombat-*.png; console `lock <id>` gives a real lock-on press.

## File select slots (2026-10-27)

Trilogy's FrontEnd GSCR links one title TXPN per slot (3 `kSS_Modify` connections; GC has 9:
title, world, play time per slot, in connection order). `CScriptGuiFrontEndScreen::CollectWidgets`
read stride 3, so on Trilogy slots 1-2 were null and only "New 1" drew. Under `TARGET_PC` it now
uses stride 1 (title only) when fewer than 9 links exist; GC data is unchanged (still 9 links).
The other slot states (`APRC`/`CLOS`/`DPHZ`/`DSCW`, 3 each) already matched. Slot 0 of an
empty save shows no extra panel text; that was not compared against hardware.

## Wii prompt glyphs in strings (2026-10-28)

Trilogy's `Strings.pak` draws prompts with Wii-remote TXTRs that have no GC id, so the RS5 skip could not fall back to GC art (Z / + / - / A / 2 showed the remote's glyphs). `CTextParser::GetImage` (`TARGET_PC` + `IsTrilogy`, as MP1 6545eb9c9) maps them to the GC prompt for the same action, with the crop the GC disc's copy of that string uses (diff of every Echoes STRG's `&image=` tags, matched by string id/index), so `port_prompts` then re-icons them: 574CDE37 (A) -> D523DE3B (A, .70/.68); 71AA55E3 (2, jump) -> 53B7AC95 (B, .70/.68); 60ACB985 (Z: lock-on, scan, hack, grapple, Spider Ball) -> 967A3516 (L, 1/1; the Spider Ball strings now map to R instead - see issue #39 below, which supersedes their "stay L"); B5F3DA09 (+) -> ADCBEDAE (C-stick, .6/.85); E2F4B915 (-) -> B4EDE4B6 (D-pad, .6/.85). Not mapped: the nunchuk stick animation (617D3700...), 7A8BD23B/FC1FA095/37437330 and a few menu icons. The Wii-pointer *wording* ("Move cursor into the top zone...", "point up") is separate string-set text and is unchanged. Shot: `/tmp/opencode/wiitext/shot_trilogy_memo_Z_after.png`.

Issue #39 (2026-10-31, reworked for languages/revisions): four Wii ids mean a different GC button by string, and no match uses English text any more. `CTextParser::PortFindWiiPrompt` decides by tag *shape*, which every Trilogy language shares (a multi-language STRG has the same indices per language): `SI,..,F1FA8D68` at crop .6875 ("Press X for Logbook", ~30 strings) is **Start**; `SI,..,3AA65ECD` at .6875 (map legend Z) is **Z**; a bare `4475EAD6` is the map Key, **Y**; a string made only of a bare `83A73184` (Dark/Light World toggle; the pause Exit string keeps Start) is **X**; an `SA,..,B4EDE4B6,13AD51E9` whose text is just a short word (<12 non-space chars, one line: `PortIsZoomText`, item-inspection "Zoom" in all 6 languages) is **X**, any other 13AD51E9 SA tag (Missile / Power Bomb prompts, which G2ME01 draws with Y) is **Y**. `60ACB985` (Wii Z) is lock-on L, but in the 6 Spider Ball strings and `13AD51E9` as a single-image Visor-select D-pad are marked by STRG id + index in `PortMarkTrilogyPrompts` (`CStringTable.cpp`, called from `FStringTableFactory`; it renames the id inside the string to `60ACB986`/`13AD51EA`, same length, and `kPortWiiPrompts` maps those to R / D-pad down). The old Z/Y art overrides in `port_prompts.cpp` are gone. Checked offline over every STRG of G2ME01, G2MP01 and R3ME01 (all 6 languages); R3MP01 is not available locally (its extra languages share the shapes; marks are by index, so a different index there would need a new row in `kMarks`). On screen (gamecube + xbox, ENGL and FREN/GERM/SPAN Missile strings, via console `memo`, which takes `{`/`}` for `&`/`;`): Key Y, Zoom X, Missile Y, Logbook Start, world toggle X, Spider Ball R, Visor D-pad, lock-on L/LT; G2ME01 unchanged. Not in the audit's reach: FREN/GERM/SPAN/ITAL on the USA disc use GC-style art (no mapping needed); (-)/(+) are a static D-pad / C-stick where GC animates the direction; JAPN-only ids 59CDE390->C6059D2F, AB564502, EDC423FE (no GC counterpart) and the nunchuk stick animation 617D3700 stay unmapped; on the xbox device the map Z glyph is blank (no Z on that pad).
