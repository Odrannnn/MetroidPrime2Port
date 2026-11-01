# F1 menu and touch overlay: gap list against MP1

Reference: `../MetroidPrimePort` at `1ad267825` (read-only). Measured 2026-10-27 on `port` @
`d40c57701`. MP1 paths are under `MetroidPrimePort/`, MP2 paths under this tree; `debug_ui/` means
`platform/debug_ui/`. Line numbers are those at the two commits above.

Size: MP1 `debug_ui/` has 27 files / 17,434 lines; MP2 has 16 files / 5,739 lines. The widget
labels were extracted mechanically. 222 MP1 labels have an MP2 counterpart. 177 have none;
about 60 of those are Remastered, Dread, Archipelago or tracker rows. The rest of the gap is in
`touch_overlay.cpp` (4,118 lines, iOS and desktop only; see (c)) and in `options.cpp`,
`input.cpp` and `runtime.cpp`, whose MP2 counterparts are spread across `settings.cpp` and
`platform/port_*.cpp`.

Statuses:
- **SAME**: the same in both.
- **DIFF**: present, but different; the cell says how.
- **MISSING**: MP1 has it, MP2 does not, and it could exist in Echoes.
- **MP1-ONLY**: cannot exist in Echoes; the cell says why.

## Answers

**(a) What closes F1 on Edit.**
- MP1 has no Edit button on the touch overlay. The editor opens from F1: Controls › Touch & gyro ›
  "Edit layout" (`debug_ui/controls.cpp:293-297`). That button does two things:
  - sets `sTouchEditRequested`;
  - calls `RequestToggle()` (`ui_common.cpp:415`), which closes F1.
- The Java view polls `nativeTouchEditRequested` (`TouchControlsView.java:656`). That call is
  `TakeTouchEditRequested` (`runtime.cpp:595`, through `android_jni.cpp:396`). The view then opens
  the editor once `nativeDebugOverlayVisible()` is false. `touch_overlay.cpp:3750-3764` does the
  same thing in C++.
- MP2's "Edit layout" (`debug_ui/controls.cpp:260-262`) only calls `PortTouch::RequestEdit()`
  (`port_touch.cpp:678-681`). It never calls `RequestToggle()`.
- The Java view polls the request and waits for F1 to close (`TouchControlsView.java:682,710-716`),
  so in MP2 the editor opens only after the user closes F1 by hand.
- MP2 also added an "Edit" button beside the eye (`C_EDIT`, `TouchControlsView.java:342,2066-2072`).
  This works around F1 once taking no touch input (`docs/ANDROID.md:317-321`). That reason no longer
  holds: MP2's F1 is a touch page window. The header comment (`TouchControlsView.java:39-45`) still
  says the settings are "not in the F1 window", which is stale.
- **Fix:** add `RequestToggle()` after `PortTouch::RequestEdit()` in `controls.cpp:261`. Whether to
  keep the eye-row Edit button is the user's call; MP1 has none.

**(b) Morph Ball detection.**
- MP1 Java (`TouchControlsView.java:2439-2441`):
  `inBall() = !map && !pause && !dialog && (nativeWheelOwned() & WHEEL_MORPHED_BIT)`.
  - `drawControls` switches pages: `page = editing ? ballPage : inBall()` (`:825-827`).
  - The poll redraws on a change (`:677`).
  - `labelContext` switches to the morph labels (`:2434`).
- Bit 13 is set by the game in `src/MetroidPrime/Player/CPlayer.cpp:994-997`
  (`PublishWheelState`, when `GetMorphballTransitionState() != kMS_Unmorphed`). It is published
  through `PortDebug::SetWheelState` (`debug_ui/input.cpp:780`).
- MP2's Java already has all of this, copied verbatim (`TouchControlsView.java:263,703,873,2681,2686-2688`,
  ball buttons `:191-207,325-341`).
- But `PortTouch::BuildWheelState` (`port_touch.cpp:402-424`) never sets bit 13 (comment at
  `:421-422`: "the morph state is CPlayer's, out of reach"). So `inBall()` is always false, and the
  ball page and morph labels never show.
- `CPlayer::GetMorphballTransitionState()` exists in MP2 (`include/MetroidPrime/Player/CPlayer.hpp:265`).
- **Fix:** publish the bit from `CPlayer`, as MP1 does, or reach the player through the state
  manager in `port_touch.cpp:517-521`.

**(c) Native overlay versus Java: not a root cause.**
- MP1 Android draws its touch controls in Java too: `CMakeLists.txt:450-461` says "Android draws
  the controls in Java (TouchControlsView); iOS draws them in C++ (touch_overlay.cpp)".
  `PORT_TOUCH_OVERLAY` is defined only for iOS, or for desktop testing with
  `MP_TOUCH_OVERLAY_DESKTOP`.
- MP2's `TouchControlsView.java` (4,260 lines) is MP1's (3,994 lines) copied. After renaming the
  package, 354 lines differ: the Edit button, a Layout edit tool, F1 two-finger scrolling, and
  MP2's wheel names.
- So on Android the overlays have the same engine. The gaps are in the native side it calls: the
  morph bit (b), the Edit flow (a), and the natives that answer "not there" (minimap rect, map
  screen, pointer menus, `touch:` bindings; header `:42-45`).
- `touch_overlay.cpp` matters only for an iOS or desktop-touch build. It is not needed for Android
  parity.

**(d) Why syncs left the menu different.**
- `docs/MP1_SYNC.md` item 1 (`d11bdce45`, 2026-10-23) said to copy MP1's `platform/debug_ui/`
  directory. It was carried out as feature packages instead (syncs 15-23: Game rows, vid1/vid2,
  ctl1, hud1, misc1, mod1/mod2, cfg1, spr1). Each package ported rows into MP2's own layout.
- That rebuild kept three MP2-only decisions:
  - a Cheats page;
  - Mods as a Game section;
  - Log and disc image inside `game_sections.cpp`.
- It also dropped rows as "not portable":
  - with a reason that holds: Remastered backends, simulation rate;
  - with no recorded reason: Unlocks/galleries, emulator card, randomizer seed UI,
    debug audio and voices.
- The touch settings stayed in `PortTouch` (`touch.cfg`) rather than MP1's `sInput`. So the
  "Edit layout" row was rewired to `PortTouch::RequestEdit` and lost MP1's `RequestToggle`.
- `docs/ANDROID.md:292-302` records the missing `debug_ui`/JNI split. `:317-321` records the Edit
  button workaround that replaced F1's Edit.
- No sync diffed the result against MP1 label by label. This file is that diff.

## Frame, look and window

| Item | MP1 | MP2 | Status |
| --- | --- | --- | --- |
| Page list | search.cpp:35-44: Game, Controls, Video, Mods, Randomizer, Archipelago, Tracker, Save states, System, Debug | search.cpp:32-39: Game, Controls, Video, Randomizer, Cheats, Save states, System, Debug | SAME (f1-frame): Game, Controls, Video, Mods, Randomizer, Save states, System, Debug, plus Tracker (map/pickups); Archipelago is MP1-ONLY |
| Mods page | tabs.cpp:256-259 | own page: mods.cpp `DrawMods` + `DrawTexturesSection` (tabs.cpp `DrawModsTab`) | SAME (f1-frame) |
| Cheats | Debug page, behind the "Show cheats" checkbox (debug.cpp:149-157) | Debug page behind "Show cheats (items, health, teleport)" (debug.cpp `DrawDebugTab`); `overlay cheats` and `MP2_DEBUG_TAB=Cheats` alias to Debug | SAME (f1-frame); the checkbox is not saved, as a cheat must not persist (MP1 saves it) |
| Archipelago page | archipelago.cpp (290 lines) | — | MP1-ONLY: Archipelago has no Echoes world |
| Tracker page | tracker.cpp (251 lines) | tracker.cpp `DrawTrackerTab`: pickups on the map (Off/Rooms/All, kinds), collected counts per world | PARTIAL (map/pickups): "Reveal map" and the visited counts are not ported |
| Search aliases | search.cpp:68-73 (Input, Render, Performance, Extras, States) | search.cpp `GetStartTarget`: Input, Render, Performance, Extras, States (+ Cheats) | SAME (f1-frame) (the Session, Chat, Remastered and Dread aliases are MP1-ONLY) |
| Page-window / desktop-window switch | ui_common.cpp:51 PageLayout | ui_common.cpp:260 | SAME |
| Touch scroll, ClearTouchHover | window.cpp:53-160 | window.cpp:36-140 | SAME |
| Touch window title | window.cpp:182-197: "Metroid Prime Port" + build revision | window.cpp: "Metroid Prime 2 Port" + build revision | SAME (f1-frame) |
| Desktop menu bar | window.cpp:271-281: hotkey hint + revision | window.cpp: hotkey hint + build revision | SAME (f1-frame) |
| Desktop size | 440x200 at (8,8); 520x620 with MP_DEBUG_TAB | same; MP2_DEBUG_TAB | SAME |
| Tab bar | FittingPolicyScroll | same | SAME |
| Touch style | ui_common.cpp:84-99 | ui_common.cpp:111-126 | SAME |
| UI scale | ui_common.cpp:386-414: display scale clamped to 1-4, compounding ratio | ui_common.cpp:198-244: × user scale, capped at 2 on Android, rebuilt from base | DIFF: MP2 adds a user slider (game_sections.cpp:432) and the Android cap |
| Themes | Auto / Metroid Prime / Remastered / Plain (ui_common.cpp:102; game_sections.cpp:374) | Auto / Metroid Prime / Plain (ui_common.cpp:135; game_sections.cpp:420) | MP1-ONLY: the Remastered theme needs a Remastered import |
| Prime palette and warn/good/bad colours | ui_common.cpp:153-296 | ui_common.cpp:45-110,323-334 | SAME |
| Menu sounds | ui_common.cpp:320-385 | ui_common.cpp:371-430 | SAME |
| Pad navigation | ui_common.cpp:465 | ui_common.cpp:269 | SAME |
| DrawUI: toasts | window.cpp:351-358: stale import, disc read failed, update, note | window.cpp: disc read failed (`DrawDiscReadFailedAlert`), note (`DrawNoteToast`, `PortDebug::ShowNote`) | SAME for the two (f1-alerts): `main.cpp` registers `aurora_dvd_set_read_error_callback` (the Echoes tree's Aurora already calls it on a failed image read), writes `<user>/disc-read-failed` naming the image, and the next launch calls `NoteDiscReadFailedLastSession`, refuses that image like MP1 and shows the red alert for 20 s. `ShowNote` has no Echoes caller: MP1's only callers are the Remastered toggle (MP1-ONLY). The stale-import toast is MP1-ONLY (Remastered); the update toast goes with the Updates section; the update toast is `DrawUpdateToast` (Updates section) |
| DrawUI: resume prompt | window.cpp:360 | window.cpp:305-358,407 | SAME |
| DrawUI: IGT timer | (speedrun) | window.cpp:363-387 | SAME |
| DrawUI: card/GPU picks, fullscreen hotkey, setting-change log | window.cpp:313-404 | partly in settings.cpp:693 LogSettingChanges | DIFF |
| DrawUI: native touch overlay | window.cpp:361-363 (PORT_TOUCH_OVERLAY, iOS only) | — | MP1-ONLY for now: iOS build only, and MP2 has no iOS target |

## Game page

| Item | MP1 | MP2 | Status |
| --- | --- | --- | --- |
| Section order | tabs.cpp:196-204: Original, Game, Cutscenes, Unlocks, Audio, Language, Speedrun | tabs.cpp `DrawGameTab`: Game, Cutscenes, Audio, Speedrun (Mods moved to its own page, f1-frame) | DIFF: no Original, Unlocks, Language sections |
| Original experience | tabs.cpp:197 (own section) | inside Game, game_sections.cpp:100 | DIFF |
| Restart to menu | tabs.cpp:38-80 | "Restart to menu" (`RequestReset`), game_sections.cpp | SAME (f1-frame) |
| Screenshot / Photo mode buttons | tabs.cpp:38-80 | photo.cpp | SAME |
| Exit game + "Exit game?" modal | tabs.cpp:38-80 | game_sections.cpp:77-82 | SAME |
| Unlock beam doors once opened | game_sections.cpp:85 | cheats.cpp `DrawCheatsSection` (Debug > Cheats); `CScriptDoor::GetDamageVulnerability`, `CScriptDamageableTrigger::PortActiveVulnerability` | DONE (console-tested 2026-10-30: Agon light door u050f opens to a Power shot with it on, not with it off; `obj <door>` prints its beams): colour-locked doors (a door or lock whose vulnerability hits 1-3 of the 4 beams) take a per-object any-beam vulnerability. Persisted as `unlock_beam_doors`
| Stack beams | game_sections.cpp:92 | game_sections.cpp; `CPlayerGun::PortFireStackedBeams`, `CGunWeapon::PortFireStackedProjectile` | DONE (build-verified, not play-tested): each shot also fires the other owned beams from their own locked weapon tokens. Persisted as `stack_beams`
| 1.00 revision compatibility | game_sections.cpp:107-111 | — | SKIPPED, with evidence: the G2ME-vs-G2MP differences are script-graph rewrites in Temple Grounds areas 6 and 19, in the disc's MREA data, not in code. The only `VERSION_G2MP01` code differences are lighting and shadows (CActor.cpp, CScriptWater.cpp). A G2MP disc has no G2ME scripts to restore, and layer 8 cannot be dropped alone because layer-0 objects were rewired. A toggle would need G2ME MREA patches shipped with the port
| Hard mode unlock | game_sections.cpp:138 | game_sections.cpp `DrawUnlocksSection` (called from the Cutscenes section until package 2 gives it a tab); hook `CScriptGuiScreen.cpp` `UpdateSaveSlots` | SAME (package 3) |
| Fusion Suit, Wear Fusion Suit | game_sections.cpp:144,166 | — | MP1-ONLY: Prime 1's Fusion Suit unlock |
| Image galleries + gallery window | game_sections.cpp:175; gallery.cpp (138 lines) | unlock row in `DrawUnlocksSection`; hook `CSlideShow::GetGalleriesUnlocked` (mask 0x3F) | SAME for the unlock (package 3). Gallery window SKIPPED: it shows Remastered-import pictures (`port_gallery`), Echoes has no such source |
| Cutscenes | game_sections.cpp | game_sections.cpp | SAME |
| Game audio | audio.cpp | video.cpp:512 area | SAME |
| Flaahgra boss theme | audio.cpp:40 | — | MP1-ONLY: Prime 1 boss |
| Language | own section, tabs.cpp:202; rows remastered.cpp:131-143 | inside Game section, game_sections.cpp:154 | DIFF: placement |
| Speedrun | tabs.cpp:203 | tabs.cpp:16 | SAME |
| Updates (Check for updates, Check now, Open release page) | game_sections.cpp:271,301,307 | game_sections.cpp `DrawUpdateSection`, `ApplyUpdateCheck`; `port_update_check`, `port_ws`, `port_json` copied from MP1; setting `update_check`, env `MP2_UPDATE_CHECK=0` | SAME (feed: GitHub releases of `Odrannnn/MetroidPrime2Port`; OpenSSL via `find_package` on desktop, built from source on Android; a repo with no release yet shows the HTTP 404 as a failed check) |

## Controls page

| Item | MP1 | MP2 | Status |
| --- | --- | --- | --- |
| Sub-tabs: Options, Mouse, Controller, Touch & gyro, Remap | tabs.cpp:206-231 | tabs.cpp:21-46 | SAME |
| Swap Scan / X-Ray visor buttons | controls.cpp:64 | — | REMOVED after v0.6.1 (Superseded 2026-10-29: this row said SAME; the option, the Weapon buttons toggle and the empty Mouse buttons section are gone from `controls.cpp`; rebinding is on the Remap page) |
| Radial menus label | controls.cpp:70 "Radial beam and visor menus (Trilogy)" | controls.cpp:62 "Native Trilogy radial selectors" | DIFF: label |
| Remastered movement | controls.cpp:241 | — | MP1-ONLY: Remastered control scheme |
| Menus / Point and click / Swap map gestures | — | controls.cpp:100-107 | DIFF: MP2-only rows |
| Edit layout | controls.cpp:293-297 (closes F1) | controls.cpp:260-262 (F1 stays open) | DIFF: see (a) |
| Reset margins | controls.cpp:299-304 | controls.cpp:264-268 | SAME |
| Touch rows (On-screen controls … Button inset) | controls.cpp:305-421 | controls.cpp:272-370 | SAME |
| Tap minimap | controls.cpp:363 | controls.cpp:104 | DIFF: moved; the native minimap rect answers "none" |
| Gyro rows | controls.cpp:429-574 | controls.cpp:376-519 | SAME |
| Spring Ball on gyro flick | controls.cpp:567 | "Spring Ball on flick", :392 | DIFF: label |

## Video page

| Item | MP1 | MP2 | Status |
| --- | --- | --- | --- |
| Sub-tabs: Display, Quality, Frame rate, Compatibility | tabs.cpp:233-254 | tabs.cpp:48-69 | SAME |
| Sharp scan window | video.cpp:72 | — | SKIPPED (f1-video): MP1 re-renders the world through a zoomed camera inside `CPlayerVisor::PortScanZoom`/`PortCopyScanZoom` (own GX copies and a scan-pane hook). Echoes' `CPlayerVisor` is a different class with no counterpart; a row would do nothing without that renderer. |
| Suit-coloured morph balls | video.cpp:81 | — | MP1-ONLY: `PortBallLooks` recolours Prime 1's per-suit morph ball models from the disc (Power Suit spider ball); Echoes has one ball model and no per-suit borrowing. |
| Apply suit colours | video.cpp:141 | video.cpp, "Apply suit colours" beside Random | SAME (f1-video): label, always shown, disabled until pending, MP1's tooltip role; shows "Applying..." during the reload |
| Scan image spread | video.cpp:164 | "Scan image spread" (video.cpp), "Widest HUD spread" | SAME (f1-video): label and tooltip as MP1; Echoes keeps the slider disabled while the widescreen HUD is off |
| Custom crosshair colour | video.cpp:216 | video.cpp, after Crosshair style (inside the aim-crosshair block) | SAME (f1-video): checkbox + colour picker; the backend (`PortOverlay::CrosshairColor`, `crosshair_color` keys, CTargetReticles) already existed |
| Third-person camera | video.cpp:239 | — | SKIPPED (f1-video): MP1 `PortThirdPerson` (202 + body lines) hooks CStateManager, CGameCamera, CTargetReticles and a Samus body model; none of those hooks exist in Echoes and the body/arm-hiding needs its player model. Too large for a row package; needs its own package. |
| Field of view | video.cpp:254 | video.cpp:196 | SAME |
| Sun shadows, projected actor shadows | video.cpp:459,465 | — | MP1-ONLY: Remastered render backends |
| HD font | video.cpp:602 | — | MP1-ONLY: font from a Remastered import |
| Simulation rate | performance.cpp:55-66 | — | MISSING: misc1 called it not portable (fixed 60 Hz tick). That is "hard", not "impossible". |
| Smooth frames (one switch: camera, animation, effects, look) | — | performance.cpp:40-53 | DIFF: MP2 merges MP1's rows |
| Low-power GPU | — | video.cpp:647 | DIFF: MP2-only row |
| Remaining Display / Quality / Frame rate / Compatibility rows | video.cpp | video.cpp | SAME (matched by the label diff) |

## Mods page

| Item | MP1 | MP2 | Status |
| --- | --- | --- | --- |
| Mods list, Load mods, Open mods folder | mods.cpp | mods.cpp (own page) | SAME (f1-frame) |
| Reload mods | mods.cpp:533 | mods.cpp `DrawMods` | SAME (f1-mods): after the "Reload the mods to apply." warning (warn colour, as MP1), then Open mods folder. DIFF kept: disabled and "Reloading..." while a reload runs; the tooltip says it needs a running game. MP1's Suspended (import) and pendingRestart warnings are MP1-ONLY |
| Other mods | mods.cpp:559 | mods.cpp `DrawMods` | SAME (f1-mods): "Other mods" separator, per-mod switches, "No other mods.", Folder in the disabled colour. SKIPPED: importStale and the native-texture count (no such fields in Echoes' PortMods), the Remastered/Dread exclusions (no such mods) |
| Textures section | tabs.cpp:258 | game_sections.cpp `DrawTexturesSection`, drawn under the Mods page | SAME (f1-frame) |
| Prime Remastered and Metroid Dread importers | mods.cpp, remastered.cpp | — | MP1-ONLY: Prime 1 asset importers |

## Randomizer page

| Item | MP1 | MP2 | Status |
| --- | --- | --- | --- |
| Seed generator: Goal, Start, Items, World, Logic | randomizer.cpp:157-262 | — | MISSING: the structure ports; the options must become Echoes' (keys, beams, Dark Aether) |
| New seed | randomizer.cpp:263 | Generate from dump log, :26 | DIFF |
| Seeds list (Play, Delete), Spoiler | randomizer.cpp:304-412 | Seed, :17 | DIFF: one seed only, no list or spoiler |

## Save states page

| Item | MP1 | MP2 | Status |
| --- | --- | --- | --- |
| Slots, save, load | savestates.cpp | savestates.cpp | SAME |

## System page

| Item | MP1 | MP2 | Status |
| --- | --- | --- | --- |
| Section order | tabs.cpp:261-274: Overlay, Settings, Memory card, Data folder, Log, Disc check, Discord, Update, About | tabs.cpp `DrawSystemTab`: Overlay, Settings, Memory card, Data folder, Log, Disc image, Discord, Update, About | SAME order |
| Overlay (floating window, Menu theme, Menu sounds) | game_sections.cpp | game_sections.cpp:415-427 | SAME |
| Memory card, Import file, Import from Dolphin, Export to Dolphin, Open card folder | file_dialogs.cpp | card_data.cpp | SAME |
| Emulator card (Choose…, Auto, Installed emulators, Share card, Copy port saves) | file_dialogs.cpp:542-603 | card_data.cpp DrawEmulatorCard; port_save.cpp; DolphinCMemoryCardSys.cpp Initialize | SAME (sharing switches live in the front end via CSaveGameScreen::Update + CMemoryCardSys::PortSwitchCard, never in game; imports remount via PortSave::RemountIfChanged). Trilogy mirror: PARTIAL - `PortSave::SyncTrilogyShare` + "Mirror with the Trilogy save.bin" checkbox + `CSaveGameScreen::Update` hook (container, mtime sync, failure markers done); the slot conversion is `port_save_trilogy_payload.cpp` (G2ME <-> R3ME by asset ID; G2MP refused; an import needs a card save to take its framing from). Gated by `mirror_trilogy_save` (off), not by the disc as in MP1 |
| Export as / Export into save.bin / Export… | file_dialogs.cpp:680-693 card_data.cpp DrawCardTransfer ("Export as" combo, "Export into save.bin…", Dolphin Wii NAND target); port_save.cpp `ExportTrilogy`; port_save_trilogy.cpp | PARTIAL - superseded the earlier "no Echoes counterpart" SKIPPED: Echoes has slots at 0x3660 in save.bin. Container done and tested on real save.bins (`port_save_trilogy_tests`): layout, Mii-owned file choice, summary line, 0x76C constant, CRC, `.bak`. The G2ME stream <-> part A/B conversion is `port_save_trilogy_payload.cpp` (tables from `tools/trilogy_tables.py`); tested on the 5 fixtures (payload -> gci -> payload byte-identical) and a real G2ME card save (gci -> payload -> gci differs in 4 bytes: the Wii's cut-off last world). Not yet checked against the Trilogy running it |
| Data folder (Android) | PortDataFolder::DrawPanel | tabs.cpp | SAME |
| Log | debug.cpp | game_sections.cpp:631 | SAME |
| Disc image | debug.cpp | game_sections.cpp:522 | SAME |
| Replace disc image… | debug.cpp:122 | game_sections.cpp (DrawDiscImageSection) | DONE: PortDiscReplace; saved `disc_path` used at next start |
| Update | tabs.cpp:271 | tabs.cpp `DrawSystemTab` → `DrawUpdateSection` | SAME (see Game › Updates) |
| About | tabs.cpp:272-273: MP_BUILD_VERSION + MP_BUILD_REVISION | tabs.cpp `DrawSystemTab`: "Version %s, build %s" (MP_BUILD_VERSION from android/app/build.gradle via cmake/BuildRevision.cmake) | SAME (f1-frame) |

## Debug page

| Item | MP1 | MP2 | Status |
| --- | --- | --- | --- |
| Sections | debug.cpp:148-172: Cheats, Camera, Rendering, Audio backends, Sounds playing | debug.cpp: Game, Camera (with Photo), Rendering, Audio backends, Sounds playing, Console; Cheats stays its own page (`DrawCheatsSection()` is the call for Debug) | DIFF: Cheats not in Debug yet (package 2) |
| Free camera / Freeze / Show Samus / Speed | cheats.cpp:301-319 | debug.cpp:22-41 | SAME |
| Collision combo | cheats.cpp:341-346 | cheats.cpp DrawRendering | DONE: PortCollisionView (off/overlay/only), hooks in CStateManager::DrawWorld |
| Frame statistics | cheats.cpp:348-352 | cheats.cpp `DrawRendering` (fps, draws, passes, buffers, textures, pipelines; no PBR or room-geometry lines) | DIFF: PBR and room-geometry counters are MP1's |
| Streamed audio / MusyX audio | audio.cpp:26,30 | audio.cpp `DrawAudio` | SAME |
| Sounds playing (Freeze list, Unmute all) | audio.cpp:111,161 | audio.cpp `DrawVoices` | SAME |
| Remastered debug | remastered.cpp | — | MP1-ONLY: Remastered |
| Full health / Grant everything / Abilities / Energy Tanks / Teleport / Reload | cheats.cpp:88,92,115,176,181,247 | cheats.cpp:163,167,183,210,217,287 | SAME |
| Invulnerable | cheats.cpp:103 | "God mode", cheats.cpp:151 | DIFF: label |
| Phazon Beam | cheats.cpp:108 | — | MP1-ONLY: Prime 1 item |
| Missiles / Power Bombs sliders | cheats.cpp:168,172 | cheats.cpp `counter()` (Echoes ids kIT_Missile, kIT_Powerbomb; also Dark/Light Ammo) | SAME |
| Replace disc image… | debug.cpp:122 | game_sections.cpp | DONE: validates, remembers path, applies on next start (no live swap) |

## Touch overlay (Android)

| Item | MP1 | MP2 | Status |
| --- | --- | --- | --- |
| Engine | TouchControlsView.java (3,994 lines) | TouchControlsView.java (4,260 lines) | SAME (copied) |
| JNI bridge | debug_ui/android_jni.cpp (102 natives) | port_touch_jni.cpp (94 natives) | DIFF: several answer "not there" |
| Morph Ball page and labels | Java :2434-2441 + CPlayer.cpp:994-997 | Java has it; port_touch.cpp:421 never sets bit 13 | MISSING: see (b) |
| Edit from F1 closes F1 | controls.cpp:293-297 | controls.cpp:260-262 | DIFF: see (a) |
| Edit button beside the eye | — | Java :342,2066 | DIFF: MP2-only |
| Layout tool in the editor | F1 "Layout" combo | Java EDIT_LAYOUT | DIFF: MP2-only |
| Minimap tap, map gestures, pointer menus, `touch:` bindings | android_jni.cpp | natives return none | MISSING |
| Native ImGui overlay | touch_overlay.cpp (iOS) | — | MP1-ONLY for now: iOS build only |

## Counts

| Status | Rows |
| --- | --- |
| SAME | 34 |
| DIFF | 30 |
| MISSING | 25 |
| MP1-ONLY | 13 |

Row counts from the tables above. One row can stand for many widgets; the label diff gives the
widget-level count, 222 matched / 177 missing.

## Work packages

Each package is independent unless it says otherwise. Each one runs its own quality pass and
checks.
- **Verification (desktop):** F1 screenshots of the same page, MP1 beside MP2.
- **Verification (touch):** an APK on the device, installed only with the user's go-ahead.

1. **Touch fixes (small, do first).**
   - Files: `controls.cpp:260` (add `RequestToggle()`); `port_touch.cpp:402-424` plus a publish
     from `CPlayer` or the state manager (morph bit 13); Java header comment `:39-45`.
   - MP1 source: `CPlayer.cpp:976-1009`, `controls.cpp:293-297`.
   - Verify on the APK:
     - "Edit layout" closes F1 and the editor opens.
     - Morphing swaps to the Bomb/Boost/P.Bomb/Spider/Unmorph page, and the labels change.
2. **Frame and page set.**
   - Files: `search.cpp` (Mods page, Cheats into Debug behind "Show cheats", aliases), `tabs.cpp`
     (Game and System order, About with the build revision, "Restart to menu"), `window.cpp`
     (title, menu-bar revision, disc-read alert, note toast).
   - Size: about 1k MP1 lines.
   - Depends on: 5, if Cheats moves into Debug.
3. **Game page rows.**
   - Files: `game_sections.cpp`, new `gallery.cpp`.
   - Rows: beam doors, stack beams, revision compatibility, Hard mode, image galleries.
     (The Updates section with its toast is done.)
   - Size: about 1k lines.
4. **Video rows.**
   - Files: `video.cpp`, `settings.cpp`.
   - Rows: sharp scan window, suit-coloured morph balls, apply suit colours, scan image spread,
     custom crosshair colour, third-person camera. Simulation rate is a separate decision.
   - Size: about 0.8k lines.
5. **Debug page.**
   - Files: `debug.cpp`, `cheats.cpp`, new `audio.cpp`.
   - Rows: Rendering (collision, frame statistics), audio backends, sounds playing, Replace disc (collision and Replace disc image: done 2026-10-27)
     image, Missiles and Power Bombs sliders.
   - Size: about 0.7k lines.
6. **Memory card.**
   - Files: `card_data.cpp`.
   - Rows: emulator card, Export as / into save.bin (container and G2ME payload conversion done: see the row above).
   - MP1 source: `file_dialogs.cpp` (981 lines).
7. **Randomizer page.**
   - Files: `randomizer.cpp`.
   - Work: the seed generator, seed list and spoiler, rebuilt on Echoes' item pool.
   - Size: about 0.4k lines; needs a generator backend.
8. **Mods page rows.**
   - Files: `game_sections.cpp`, or a new `mods.cpp` if package 2 adds the page.
   - Rows: Reload mods, Other mods.
   - Depends on: 2.
9. **Touch natives (optional).**
   - Files: `port_touch_jni.cpp`.
   - Work: minimap rect, map-screen state, pointer menus, `touch:` bindings.
   - MP1 source: `android_jni.cpp`.

Not planned: the Archipelago, tracker, Remastered and Dread rows, Fusion Suit, Phazon Beam,
Flaahgra, and `touch_overlay.cpp` (iOS).
