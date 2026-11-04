# Controls and settings

Native Trilogy radial menus: `trilogy_radial_selectors` (default `0`), F1 > Controls > Options >
Aiming, Trilogy discs only. Bind the new `beam_menu` / `visor_menu` held actions in Controls >
Remap (keyboard or gamepad; unbound by default, existing bindings preserved). Hold, point with
relative mouse or mapped right stick, release: center selects Power/Combat, upper selects
Annihilator/Scan, lower left Dark/Dark, lower right Light/Echo. Current/unowned picks do nothing;
pause/F1/morph and other gameplay interruptions cancel. See [TRILOGY.md](TRILOGY.md#native-desktop-beam--visor-selectors-opt-in)
for binding examples, lifecycle rules, harness commands and desktop-only limitations.

- GameCube controller adapters (issue #222): Nintendo's adapter and Mayflash's
  in Wii U mode (USB `057E:0337`) work as in Dolphin, four ports with rumble,
  read by SDL through libusb. On Windows, install the WinUSB driver for the
  adapter once with [Zadig](https://zadig.akeo.ie/) ("WUP-028"; Options > List
  All Devices); the Windows package ships `libusb-1.0.dll`, without which the
  adapter is not seen. Pads on it read as on a console: the stick and
  trigger values the pad sends, less the rest position taken when the pad is
  plugged in (hold X+Y+Start for 3 seconds to take it again), with no
  deadzone besides the game's own; the stick deadzone slider does not apply,
  and remapping a stick or trigger puts that pad back on the ordinary path. On Linux the device needs a udev rule giving your user
  access, the same one Dolphin documents. A Mayflash adapter switched to PC
  mode shows up as ordinary controllers instead, one per port, without any
  driver.
- F1: the settings overlay (the Prime 1 port's `platform/debug_ui/` layout). Pages: Game,
  Controls (Options, Mouse settings, Controller settings, Touch & gyro, Remap), Video (Display,
  Quality, Frame rate, Compatibility), Enhancements, Mods, Save states, System, Debug; the
  Randomizer page is built but hidden from the list.
  `MP2_DEBUG_TAB=<page>[/<sub-tab>]` opens the overlay on that page at launch; console
  `overlay <page>` does the same in a running game. `Cheats` (either way) opens Debug with Show
  cheats on.
- Settings search: the box at the top of the overlay (Ctrl+F) lists the settings whose name,
  page, section or keywords contain every typed word; picking one opens its page, sub-tab and
  section, scrolls to it and flashes it. The index is `platform/port_settings_index.inc` (one row
  per widget, in draw order; ctest `port_settings_index`, `tools/check_settings_index.py`, checks
  it against the sources, so a new widget must be added there). `MP2_DEBUG_SEARCH=<query>` types a
  query into the box when the overlay first opens; `MP2_DEBUG_SEARCH_AUDIT=1` opens every row in
  turn and logs `search audit: found|MISSED: <row>`. Rows drawn on one platform only sit in an
  `#if` block of the index.
- Settings changed in the overlay are saved to `<user>/settings.cfg` and restored on the next
  launch; System > Settings has **Save settings now**. Each key is one row of `kSettingKeys` in
  `platform/debug_ui/settings.cpp`, which both reads and writes it with two sample values;
  console `settings self-test` checks that every key reads back as written. An `MP2_<KEY>`
  environment variable (the key upper-cased, or the name the row here gives: `MP2_LOCK_ON_TOGGLE`
  for `lockon_toggle`, `MP2_MOUSE_SENS` for `mouse_sensitivity`) overrides the file for that run
  where the key's code reads one; most keys do, and the rows below name theirs. The file-only
  keys are `mouse_dpi`, `texture_pack`, `overlay_windowed`, `ui_theme`, `show_cheats`,
  `suit_colour_varia/dark/light`, `photo_supersample`, `photo_speed`, `discord`, `ui_sounds`,
  `mods_disabled`, `low_power_gpu`, `game_options`, `emulator_folder`, `share_emulator_card`,
  `mirror_trilogy_save` and `disc_path`. A new key needs a mention here as `key`, or the index check fails.
  `metroid_prime2_port --settings-selftest` runs the same check without starting the game
  (ctest `port_settings_roundtrip`).
- F1 > System > Settings > **Reset configuration...** puts every port setting and control mapping
  back to its default: after a confirmation it saves the current settings, moves `settings.cfg`,
  `touch.cfg`, `controls.toml`, `imgui.ini`, `controller_ports.dat`, `keyboard_bindings.dat` and
  the `*.controller` mapping files into a new `config-backup-<date>-<time>/` folder inside the
  user folder (older backups are never reused), and exits; the next launch starts from defaults
  (`platform/include/port_config_reset.h`, test `port_config_reset_tests`). Saves, save states and
  the card are not touched.
- `MP2_GODMODE=1` starts with Cheats > God mode on (not saved).
- `MP2_NO_DARK_AETHER_DAMAGE=1` starts with Cheats > No Dark Aether damage on (not saved): the air does not hurt, as with the Light Suit; light crystals still heal.
- `MP2_INFINITE_AMMO=1` starts with every Cheats > Infinite ammo box ticked (not saved): you can fire at zero, the HUD shows ∞, real counts and pickups are kept.

## Keys

Save states page:

- `quick_resume`: Save states > Quick resume (default on); env `MP2_QUICK_RESUME`. On quitting during
  play (and as the game keeps safe points, see Resume below) `savestates/resume.mp2ss` is written; the
  next launch asks "Resume where you left off?". Console: `resume [status|save|load|clear]`.
- Save state bindings and room objects: no settings. The Save state / Load state bindings always
  work (unbind them in Controls > Remap); env `MP2_SAVESTATE_HOTKEYS=0` turns them off. States and
  quick resume store the loaded rooms' objects; env `MP2_SAVESTATE_ROOM_STATE=0` stores none, and a
  load then rebuilds the room as a card load does.

Game page:

- `original_experience`: Cutscenes > Original experience (default off); env
  `MP2_ORIGINAL_EXPERIENCE`, console `original`. While on, the port's gameplay and cutscene tweaks read
  as retail and their rows are greyed: skippable cutscenes, seamless item pickups, world name on load, walk speed, hint
  delay, scan speed, elevator ride, portal transition, rotating rooms, fast morph, spring ball, the Enhancements page, toggle lock-on, sticky charge, remastered
  charge, auto-aim (on). Your saved values are kept and return when it is switched off. Mods, input,
  video and audio settings are not affected.
- `skip_cinematics`: Enhancements > Pace > Skippable cutscenes.
- `seamless_pickups`: Enhancements > Pace > Seamless item pickups: 0 Off (default), 1 All,
  2 Repeats only (a saved checkbox's 0/1 reads the same); env `MP2_SEAMLESS_PICKUPS` (a flag, or
  `2`/`repeats`), console `seamlesspickups`. Repeats only applies it to a pickup whose item the
  player already had (`CPlayerState::HasPowerUp` before the pickup: the second and later expansion,
  tank or beam ammo pickup), so each kind's first message box still explains it; map stations keep
  theirs. Single player, as in the Prime 1 randomizer:
  picking up an item keeps control (its "Disable Controls" hint is dropped), shows the message box's
  headline ("Missile Expansion acquired!") as a HUD memo instead of pausing, and skips the cinematic
  some items start (20 of the 124 item pickups) the way the skip button does, so the doors, platforms
  and layers the scene changes still change. A map station's message box after its download shows
  as a memo the same way (its cinematic and the map screen play as retail;
  `PortPickupPresentation::IsMapStationMemo`). `CScriptPickup::Touch` walks the pickup's Arrived chain
  through relays, memory relays, timers and sequence timers and marks those three targets
  (`platform/port_pickup_presentation.cpp`); `tools/pickup_presentation.py` checks the rule against
  every pickup and map station on the disc.
- Boot logos and the world name card on load: no setting; they end on A or Start (shown as retail
  under Original experience). Env `MP2_BOOT_LOGOS`, `MP2_WORLD_NAME` (`show`, `skip`, `off`).
- `walk_speed`: Enhancements > Pace > Walk speed (50 to 200 percent, default 100 = retail); env
  `MP2_WALK_SPEED`, console `walkspeed`. Scales how fast Samus moves on foot: the top speed,
  acceleration and friction of walking and strafing (on the ground, in the air and in water) and the
  orbit strafe and approach speeds are all multiplied by it, so she reaches k times the retail speed
  in the same time and stops in the same time. Jump height, sideways dashes, turning, the grapple
  and the Morph Ball are unchanged; speed carries into a jump. At 100 movement is exactly retail.
- `walk_speed_enabled`: Enhancements > Pace > Custom walk speed (default on); env
  `MP2_WALK_SPEED_ENABLED`, console `walkspeed on|off`. Off moves at retail speed whatever the
  slider says. For a sprint, set the speed and bind a custom action to this switch (Settings >
  Custom walk speed, Toggle or While held) in Controls.
- `scan_speed`: Enhancements > Pace > Scan speed (50 to 400 percent, default 100 = retail); env
  `MP2_SCAN_SPEED`, console `scanspeed`. Scales how fast a scan fills (CPlayer::UpdateScanningState);
  the visor, scan-point and hint text are unchanged.
- `beam_switch_speed`: Enhancements > Pace > Beam switch speed (100 to 400 percent, default 100 =
  retail); env `MP2_BEAM_SWITCH_SPEED`, console `beamswitch`. Scales a beam change's close, wipe
  and open animations while one runs; the rest of the gun keeps its speed. Retail under Original
  experience.
- `elevator_ride`: Enhancements > Pace > Elevator ride cutscene (0 original, 1 fast, 2 skip); env `MP2_ELEVATOR_RIDE`
  (`original|fast|skip`). Fast runs the world-transition ride at double speed; Skip ends it as soon as
  the next world has loaded (the load itself still takes its time). Text transitions are unchanged.
- `rotating_rooms`: Enhancements > Pace > Energy controller access cutscene (0 original, 1 fast); env `MP2_ROTATING_ROOMS`
  (`original|fast`), console `rooms`. Fast runs the energy controller rooms' bomb-slot cutscene (the
  room turning) at the game's own double speed: while a cinematic camera is active and a timed,
  one-shot CScriptActorRotate of 5 s or more plays (any such room, no list), `CStateManager::Update`
  doubles dt and the camera's dt. Other cutscenes and gameplay are unchanged.
- `combo_beam_cost`: Debug > Cheats > Combo beam ammo cost (0 to 30, default 30 = retail); env
  `MP2_COMBO_BEAM_COST`, console `combocost`. The beam ammo a combo (super missile) takes; a Reset button
  restores 30. Ignored with Original experience.
- `combo_missile_cost`: Debug > Cheats > Combo missile cost (0 to 5, default 5 = retail); env
  `MP2_COMBO_MISSILE_COST`, console `combomissiles`. The missiles a combo takes (0 lets one fire with none).
- `beam_ammo_per_expansion`: Debug > Cheats > Beam ammo per expansion (50 to 100, default 50 = retail); env
  `MP2_BEAM_AMMO_PER_EXPANSION`, console `beamammo`. What a Dark/Light ammo capacity pickup (expansion,
  Dark/Light Beam, Annihilator; a pickup that adds capacity) grants is multiplied by this / 50, and
  the cap for each is 250 * this / 50 (500 at 100). Ammo drops add no capacity and are unchanged.
  The save file keeps the retail format: it stores the amounts clamped to 250, and the real amounts
  live in the sidecar `beam_ammo.sidecar` next to the card (see PortSave `AmmoSidecar`), applied
  on load only when its record's hash matches the save's bytes. Without the sidecar (or after a
  copy through another tool) the save loads at the clamped 250. Lowering the slider mid-session
  only lowers the cap: amounts above the new cap are clamped the next time ammo is added.
- `portal_transition`: Enhancements > Pace > Portal transition (0 original, 1 fast, 2 skip); env
  `MP2_PORTAL_TRANSITION` (`original|fast|skip`), console `portal`. The ride through a light/dark world
  portal (`CPortalTransition`, run by `CMFGame` kFS_PortalTransition for a dock, or by `CWorldTransManager`
  from a teleporter). Fast runs it at double speed; Skip drops it, so the fade-in follows the load
  (about 10 s / 5.5 s / 2 s). Original
  experience forces Original.
- `language`: Cutscenes > Language (0 English, 1 German, 2 French, 3 Spanish, 4 Italian, 5 Dutch,
  6 Japanese); env `MP2_LANGUAGE`, console `language`. Picks the string-table language. Applied live:
  every STRG keeps all its sections and `GetString` picks the current one, so tables already loaded follow
  (a widget that copied its text keeps it until its screen reopens). A table without the language falls back
  to its ENGL section (else the first listed). PAL discs carry ENGL/FREN/GERM/SPAN/ITAL/JAPN, the USA disc ENGL
  only, so other languages show English there.
- `igt_timer`: Speedrun > On-screen in-game time; env `MP2_IGT_TIMER`. The save file's play time
  (stops in cutscenes, menus and loads) in the bottom-right corner while a game runs.
- `hint_delay`: Enhancements > Hints > Hint delay (0 to 30 minutes, default 0 = the game's own timing); env
  `MP2_HINT_DELAY`. Caps the wait before a hint first shows, also for hints already waiting in a
  loaded save. Needs the in-game hint system on. Show current hint (Cutscenes button; console
  `hintshow`) shows the hint the game would show next: a hint already on screen comes back,
  otherwise the waiting hint due soonest shows now.
- `unlock_hard_mode`, `unlock_galleries`, `stack_beams`: Game > Unlocks > Hard mode (offered when
  starting a file, as after finishing the game), Image galleries (all of them); Debug > Cheats >
  Stack beams (each shot also fires the other beams you own, at no extra ammo cost); env `MP2_UNLOCK_HARD_MODE`,
  `MP2_UNLOCK_GALLERIES`, `MP2_STACK_BEAMS`. Not written into the save, and Original experience
  greys them.
- `unlock_beam_doors`: Debug > Cheats > Unlock beam doors; env `MP2_UNLOCK_BEAM_DOORS`.
  Colour-locked doors (the door's own vulnerability, or a damageable-trigger lock) open to any of
  the four beams; missile locks stay. Saved, not written into the save file; Original experience
  greys it.
- `stack_beams_skip_dark`: Debug > Cheats > Leave out Dark Beam (#27): stacked shots skip the Dark Beam
  unless it is in hand, so they don't drain light beacons and crystals; env
  `MP2_STACK_BEAMS_SKIP_DARK`.
- `livesplit`, `livesplit_address`, `livesplit_split_upgrades`: no F1 rows (settings.cfg or
  console `livesplit <0|1> | addr <host:port>`); the address's env is `MP2_LIVESPLIT_ADDR`.
- `mods`, `mods_disabled`: Mods > Load mods and one checkbox per mod folder (`mods_disabled` holds
  the turned-off folder names, `/`-separated); take effect on restart. Folder: `<user>/mods`, or
  `MP2_MODS`. See docs/MODS.md.
- `suit_colour_varia`, `suit_colour_dark`, `suit_colour_light`: Video > Display > Suit colours (suit, colour per slot, Random). Each is one hex colour per slot (`FF4000,-`, `-` keeps that slot's original); empty is the original. The recoloured textures are built from the disc at startup, so a change applies on restart. Details: `docs/MODS.md` "Suit colours".
- `music_boost`, `sfx_boost`: Game > Audio > Music volume and Sound effects volume above 100
  percent (100 to 200, default 100 = no boost); env `MP2_MUSIC_BOOST`, `MP2_SFX_BOOST`. Up to 100
  the row sets the game's own volume (`game_options`); above it the game stays at its maximum and
  the port's mixer scales that path by percent / 100 (music = the streamed voices, sound effects =
  every other voice, and the movie audio's stream), so the level goes past what the game alone
  reaches. It can clip.
- `game_options`: the game's own Options menu (volumes, sound mode, brightness...), stored as a
  hex blob so they survive without a memory card; not an overlay row.

Controls > Options:

- Quick setup > Use mouse & keyboard: one click turns on mouse aim (`mouse_aim`, with
  `twin_stick`) and applies the Mouse & keyboard keyboard layout (Remap > Presets...),
  replacing your own keyboard bindings.
- Quick setup > Use twin-stick controller: one click turns on Twin Stick Aim (`twin_stick`) and
  applies the Remastered layout to all controllers (Remap > Presets...), replacing your own
  controller bindings; greyed out with a GameCube adapter.
- `lockon_toggle`: Buttons > Toggle Lock-On; env `MP2_LOCK_ON_TOGGLE`. An L binding whose own
  trigger is Toggle (F1 Remap) also lets go once a lock or grapple it held ends; one that never
  locked stays on until pressed again.
- `spider_toggle`: Buttons > Toggle Spider Ball (default off, off under Original Experience; env
  `MP2_SPIDER_TOGGLE`): an R press on a Spider Ball track clings until the next press; leaving the
  track (more than 0.15 s) or the morph ball ending lets go. Away from a track, R works as usual.
  Grapple key, Toggle Grapple and Shoot While Grappling (MP1 #243) are not ported: Echoes grapples
  with the fire button at a grapple point and never holsters the cannon for it.
- `aim_lock_on`: Buttons > Aim Lock-On (default off, off under Original Experience; env
  `MP2_AIM_LOCK_ON`): lock-on picks the target nearest the crosshair, measured as an angle (screen x
  weighed by the aspect ratio) with line of sight still required. Retail prefers a target nearer the
  player by more than the orbit distance threshold even when it is off to the side. Grapple points
  and the scan visor keep retail's pick.
- Dodge (Controls > Remap > Move and aim, binding `dodge`, unbound by default): pressed on the
  ground while strafing (strafe at least as strong as forward/back), it dashes. Locked on, it does
  what a jump tapped while strafing does: retail's dash around the target. Not locked on, it does
  the same dash (hop, speed, length, sound and rumble) straight sideways along the facing at the
  press, with mouse/stick look free during it. A press is kept for 0.12 s, so pressing just before
  the strafe works. Like any digital action it can be a touch button in the layout editor. Off under
  Original Experience; the jump-tap dash is unchanged.
- Next/Previous target (Controls > Remap > Move and aim, bindings `target_next` and `target_prev`,
  unbound by default; MP1 8c7d45fab): while locked on, a press moves the lock to the visible target
  nearest to the right (Next) or left (Previous) of the current one, wrapping to the far side; grapple
  points are skipped. The actions work only while locked on (input context `lockon`), where their
  inputs do nothing else, so e.g. the mouse wheel can move the lock while locked on and still cycle
  beams otherwise. Console `target next|prev`. Off under Original Experience.
- `sticky_charge`, `rapid_charge`: Buttons > Sticky Charge, Remastered charge (rapid fire).
- `auto_aim`: Aiming > Auto-aim.
- `pointer_aim`, `pointer_aim_box`, `pointer_aim_turn`: Aiming > Pointer aim (Trilogy style),
  Dead box, Edge turn speed. An empty lock (L with nothing targeted, outside the Scan Visor)
  holds the view and leaves the pointer free, as Trilogy's Z does, whatever Lock-on free aim says.
- `hand_on_cannon`: Aiming > Hand on cannon when still (default on; needs Mouse aim, Twin stick
  or Touch aim; env `MP2_HAND_ON_CANNON`), as in Trilogy and PrimeHack: after a second on the
  ground without walking the gun enters the R free-look pose with the left hand on the cannon.
  Turning, aiming and firing keep it; walking, jumping, lock-on, grapple and the morph ball drop it.
- `lock_on_free_aim`: Aiming > Lock-on free aim (default off; needs Mouse aim, Twin stick or
  Touch aim), as in Remastered; env `MP2_LOCK_ON_FREE_AIM`. Under a combat lock the mouse, the
  right stick, gyro aim or a touch aim drag moves a crosshair over the view and shots leave along
  the ray through it, as with pointer aim, while the lock steers the view (no edge turn). Without
  pointer aim the crosshair is there only during the lock. It starts and ends each lock at the
  centre, where shots and homing go for the locked target as before; off the centre beams and
  missiles head for the crosshair and do not home. Under pointer aim with gyro aim the right stick
  moves the crosshair too while locked. Scan locks and grapples still aim through the centre. A
  touch source re-centres it while no finger is down.
- `reset_aim_both`: Aiming > Reset aim resets pointer and camera (the Reset aim binding, unbound
  by default, recentres the pointer under pointer aim, else levels the view); env
  `MP2_RESET_AIM_BOTH`.
- `pointer_menus`, `map_swap_fingers`: Options > Menus > Point and click menus (mouse and touch in
  the pause screens and the map, where a click on a room selects it; default on; env
  `MP2_POINTER_MENUS`), Swap map finger gestures
  (touch: one finger pans the map, two rotate; env `MP2_MAP_SWAP_FINGERS`).
- `prompt_glyphs`: Options > On-screen prompts > Button glyphs (Auto follows the last-used device;
  Xbox, PlayStation, Switch, GameCube or Keyboard forces the set in the F1 UI and the game's
  prompts; env `MP2_PROMPT_GLYPHS` 0-5, and `MP2_TEXTURE_DEVICE` still wins).
- `minimap_tap`: Options > Menus > Tap minimap for map (a tap or click on the HUD minimap opens the
  map screen; default on; env `MP2_MINIMAP_TAP`; console `minimap`, `maptap`).
- `map_pickups`: Tracker > Map > Pickups on the map (0 off, 1 rooms: tint the map rooms that still
  hold a pickup, 2 all: a white dot per pickup not collected; default off; env `MP2_MAP_PICKUPS`
  off/rooms/all; console `tracker`; off under Original experience).
- `map_pickup_kinds`: Tracker > Map > Show pickup kinds (All mode: a ring for expansions and energy
  tanks, a grey cross for collected pickups; the seed's item when randomized; env
  `MP2_MAP_PICKUP_KINDS`).
- `reveal_map`: Tracker > Map > Reveal map (every room of every world shows as a map station shows
  it, Dark Aether's too, and the universe map lists every world; nothing is saved; default off; env
  `MP2_REVEAL_MAP`; console `tracker reveal 0|1`; off under Original experience).
- `fast_morph`, `spring_ball`: Enhancements > Morph Ball > Fast Morph, Spring Ball (one row; C-stick up, the Spring Ball binding on the Remap page, touch swipe up, or a gyro flick).
- `ball_cam`, `ball_cam_speed`: Controls > Options > Morph Ball camera (issue #54; off by default,
  speed 25-400%, 100 default; env `MP2_BALL_CAM`, `MP2_BALL_CAM_SPEED`; off under Original
  experience). The mouse, twin stick, touch aim swipes and the C-stick turn the Morph Ball camera
  around the ball and raise or lower it; the game's camera trails from where it is left, and the
  Chase/Boost swing behind the ball waits 1.5 s after a turn. Hint, spline and fixed cameras and
  the "no free look" hint keep control. With `spring_ball` on, the stick only turns it.
- `map_invert_x`, `map_invert_y`: Map > Invert map rotate X, Invert map rotate Y (off by
  default; env `MP2_MAP_INVERT_X`, `MP2_MAP_INVERT_Y`). They flip which way the map turns on
  the rotate inputs, left/right and up/down: the game turns the map opposite to the stick,
  so both on gives camera-style rotation. The pointer and touch map gestures are not
  flipped, as in Prime 1.

Controls > Mouse settings:

- `mouse_aim`, `mouse_invert_x`, `mouse_invert_y`: Mouse aim > Mouse aim, Invert X, Invert Y.
- `mouse_sensitivity`: Mouse aim > Sensitivity (radians per count; the row shows MP1's
  Quake/Source scale, 0.022 degrees per unit); env `MP2_MOUSE_SENS`.
- `mouse_sensitivity_split`, `mouse_sensitivity_y`: Mouse aim > Separate vertical speed, Vertical
  sensitivity (same units); env `MP2_MOUSE_SENS_SPLIT`, `MP2_MOUSE_SENS_Y`.
- `mouse_dpi`: Mouse aim > Mouse DPI (100..64000, default 800), only for the cm/360 readout.
- `aim_crosshair`, `crosshair_size`: Mouse aim > Crosshair, Size.

The cursor is not a setting: an uncaptured desktop cursor hides after 2 s without mouse input (a
controller player in full screen) and shows again on the next motion, click or wheel. It stays
while the overlay or photo mode's panel wants it, and mouse aim's capture hides it anyway
(`PortMouse::UpdateIdleCursor`, #32).

Controls > Controller settings:

- `twin_stick`: Stick aim > Twin stick (right stick aims).
- `stick_aim_rate`, `stick_aim_split`, `stick_aim_rate_y`: Stick aim > Stick aim speed,
  Separate vertical speed, Vertical stick aim speed (px/s, 50..10000; the slider runs
  100..8100).
- `flick_stick`: Stick aim > Flick stick (`MP2_FLICK_STICK`; twin stick only): tilting the right
  stick past 90% turns to face that way over 0.1 s, rotating it turns by the same angle; yaw only,
  pitch is left to gyro aim. Under Pointer aim it flicks only while the gyro aims. The stick-speed
  rows hide while it is on.
- `flick_stick_outer_deadzone`: Flick outer deadzone (percent, 0..50, default 10): the tilt that
  counts as full; the inner edge is the C-stick dead zone, round under flick stick.
- `touch_flick_stick`: Touch controls > Flick stick (`MP2_TOUCH_FLICK_STICK`): the same for the
  touch twin-stick layout while touch is the last-used input.

Controls > Remap:

- Presets... (beside Reset...): one click applies Classic or Mouse & keyboard to the keyboard, or
  GameCube, Remastered, Modern or Southpaw to all controllers, replacing your own bindings
  there; a selected kind or model keeps its own on top. Reset... removes a device's own
  bindings (after a confirmation), bringing back the Classic and GameCube layouts.
- `pad_preset` (`p2.`..`p4.pad_preset` for pad ports 2-4): the last controller preset picked
  there (`gamecube`, `remastered`, `modern`, `southpaw`; empty = none). Aurora keeps mappings per
  controller model, so a real controller connected to that port later, still on its defaults, gets
  the preset's mapping (dual-stick ones skip a GameCube adapter), and Remap shows it with no
  controller. Restoring My presets' controller mapping clears it.
- My presets... (beside Presets...): "Save current as..." keeps your bindings as a preset of your own. One list; a
  preset covers every device it was saved with: keyboard & mouse, touch (when it has bindings) and, with a
  controller connected, the controller (its bindings, custom actions, button/axis mapping), plus twin stick and
  mouse aim. One click applies every part it has (replacing your own bindings for those devices), x deletes it;
  saving over a name replaces it. One file per device part,
  `control_presets/<keyboard|pad|touch>-<name>.toml` in the user folder: `controls.toml` text with `#! key = value`
  header lines, so it can be shared.
- Morph Ball rows (issue #167: Bomb, Power Bomb, Boost Ball, Spider Ball, Unmorph;
  `ball_bomb`, `ball_power_bomb`, `ball_boost`, `ball_spider`, `ball_unmorph` in
  controls.toml) press A, Y, B, R and X only in Morph Ball, so the ball's controls are
  separate from gameplay's; the stick, Start, Z (map) and the C-stick are shared. On
  keyboard & mouse and controller they start unbound, and an unbound row follows gameplay:
  the game button's inputs do it in the ball. Once a row is set in one of those kinds, it
  replaces that button in the ball for that kind only (setting Bomb on the controller stops
  the pad's A bombing; the keyboard's A key still does). An input bound to a row does only
  that in the ball, held layers included (the same mouse button under any of its codes, an
  axis at any threshold); outside the ball its other bindings work as before, and the
  prompts in the ball follow it. A combo on a row replaces the same combo; its buttons alone
  keep what they do. A row given layers in controls.toml works in the ball while one is held
  and adds to the game button; without the layer its input does what it did. The touch
  overlay's Morph Ball page (Bomb, Boost, Power Bomb, Spider, Unmorph; touch codes 40..44)
  presses the rows; a remap moves a row off its button and relabels it. A touch: binding on a control
  counts as taking it over only where it applies.
- Map rows (issue #31: Map rotate up/down/left/right, Map pan up/down/left/right;
  `map_rotate_up`.. `map_rotate_right`, `map_pan_up`.. `map_pan_right` in controls.toml)
  tilt the control stick (rotate) or the C-stick (pan) only on the map screen. They start
  unbound and add to the sticks: their own inputs keep rotating and panning. An input bound
  to a map row does only that on the map, bindings scoped to the map included, and what it
  did before everywhere else, so WASD can pan the map without moving the gameplay C-stick.
  Zoom stays on L/R (or the "Menus and map" L/R rows). The map screen is its own context
  (Map), not Menus: a binding scoped to Menus alone no longer applies there.
- Custom actions (advanced) (as MP1 issue #218) holds actions the player makes, as many as
  wanted: "+ Add action" picks what one does from a searchable list, then it binds like any
  row (chords, layers, triggers and touch buttons included); Edit and Delete are on its row,
  and Delete drops its bindings in every profile too. The list: Settings (turn any on/off
  setting listed in the picker on, off or toggle it), Cheats (God mode, free camera, freeze
  the game, show Samus in free camera, refill energy, refill ammo, give or take an item,
  kill the locked-on enemy), Travel (warp to a room picked by its in-game name, or to where
  a world starts; another world reloads it, a loaded room of this world is instant),
  Save states (save to or load from a given slot), Display (show a message on the HUD, text
  language) and Advanced (any debug console command line, `docs/CONSOLE.md`; it must be
  given a name). The on/off ones take Toggle, Turn on, Turn off or While held (switched the
  other way while the binding is held and back on release, with no note). Its name is made from
  what it does ("God mode (on/off)", "Warp to Great Temple (Temple Grounds)") unless one is
  typed. A press shows what it did as a note at the top of the screen; Cheats, Travel and
  console commands refuse in the Original experience. Saved in `controls.toml` as
  `[[custom_action]]` tables (`id`, `name`, `preset`, `arg`, `mode`, `command`) and bound as
  action `custom:<id>`; a preset or setting this build doesn't know is kept and does nothing.
- `menu_standard_buttons`: Separate menu buttons (menus and the map read the "Menus and map"
  rows; ignored while the touch controls are on); env `MP2_MENU_STANDARD_BUTTONS`.

Controls > Touch & gyro (Gyro aim):

- `gyro_aim`, `gyro_mode`: Mode (Off, or one of the four modes; Off clears `gyro_aim`).
- `gyro_source`, `gyro_space`, `gyro_auto_calibrate`: Source, Space, Auto calibrate.
- `gyro_sensitivity`, `gyro_cursor_angle`, `gyro_cursor_camera`: Sensitivity, Pointer speed,
  Gyro also turns the camera.
- `gyro_split`, `gyro_sensitivity_y`: Separate vertical speed, Vertical sensitivity (under
  pointer aim it scales the pointer's vertical travel); env `MP2_GYRO_SPLIT`,
  `MP2_GYRO_SENSITIVITY_Y`.
- `gyro_smoothing`, `gyro_tightening`: Smoothing, Tightening.
- `spring_ball_flick`, `spring_ball_flick_rate`: Spring Ball on flick (an upward pitch past the rate springs the ball, re-armed below half; works with Gyro aim off; default off; env `MP2_SPRING_BALL_FLICK`) and Flick strength (2..20 rad/s, default 6; env `MP2_SPRING_BALL_FLICK_RATE`).

Gyro aim needs mouse aim, twin stick or the touch controls' aim; without pointer aim it turns the
camera at Sensitivity. Touch aim's Separate vertical speed and Vertical touch aim speed are
touch.cfg's `aim_split` and `aim_speed_y` (env `MP2_TOUCH_AIM_SPLIT`, `MP2_TOUCH_AIM_SPEED_Y`).
Beam and visor wheels: two round buttons, Visor and Beam, stock in the Default layout; any layout
takes them as added buttons (Edit layout > Add a button > Visor wheel / Beam wheel, keys
`visor_wheel` and `beam_wheel`, which only the overlay reads; a quick tap on Visor picks Scan with
Tap Visor for Scan Visor). touch.cfg's retired `wheels=1` put the stock pair in the Classic and Twin
layouts in place of the D-pad; such a layout becomes added wheel buttons where they were, without
the D-pad, on its first draw (either overlay), and the line is dropped.

Multiplayer (Player 1-4 selector; Player 1 edits the Controls keys above):

- `p2.`..`p4.` + `twin_stick`, `flick_stick`, `stick_aim_rate`, `stick_aim_split`, `stick_aim_rate_y`, `lockon_toggle`,
  `sticky_charge`, `rapid_charge`, `auto_aim`, `lock_on_free_aim`, `gyro_aim`, `gyro_mode`,
  `gyro_sensitivity`, `gyro_split`, `gyro_sensitivity_y`: that player's own value; a missing key follows
  Player 1 ("same as Player 1"; Reset and Reset to Player 1 remove keys). Each player's controller is the
  pad port assignment, not a settings key (its last controller preset is `p<N>.pad_preset`, above); Remap... opens Controls > Remap on that port.

Video > Display:

- `fullscreen`, `vsync`, `aspect`: Display > Fullscreen (F11), Vsync, Aspect ratio. `MP2_ASPECT`
  takes `4:3`, `16:9` or `window`; `MP2_WIDESCREEN=1` also gives 16:9.
- `hud_wide`, `hud_scale`: HUD and view > Widescreen HUD, HUD scale.
- `hud_spread_max` (`16:9`, `21:9`, `full`; default `21:9`, env `MP2_HUD_SPREAD_MAX`): HUD and view >
  Widest HUD spread; past that width the HUD stays centred.
- `scan_spread` (0-100; default 100, 0 on Android, env `MP2_SCAN_SPREAD`): HUD and view > Scan image
  spread; how far the widescreen HUD moves the scan visor's side panels. Scan text box and bar stay 4:3.
- `crosshair_style` (`0` classic, `1` remastered, `2` gamecube; env `MP2_CROSSHAIR_STYLE` also takes
  the names), `crosshair_opacity` (0-500; default 100, the game's own alpha; above 100 brightens the
  dim GameCube/Classic crosshair, #56): HUD and view > Crosshair style, Crosshair opacity.
- `crosshair_color`, `crosshair_custom_color`: HUD and view > Custom crosshair colour (off: the
  game's own), which shows the colour picker; env `MP2_CROSSHAIR_COLOR`.
- On Trilogy discs these crosshair settings act on Trilogy's own aiming cursor (centre and arms):
  a custom colour replaces their colour, opacity scales their alpha (the default 100% leaves them
  as on the Wii), the size slider scales them under mouse/pointer aim, and `remastered` draws the
  Remastered cursor where the aiming cursor is (MP1 2ee5a757c). `gamecube` draws the GameCube
  crosshair there, turned down the pointer ray (Classic on a GameCube disc; MP1 255afaf13).
- `no_helmet`, `no_visor_fx`: HUD and view > Hide helmet, Hide visor effects; env
  `MP2_NO_HELMET`, `MP2_NO_VISOR_FX`.
- `cinema_bars` (default on, env `MP2_CINEMA_BARS`): HUD and view > Cutscene black bars; off drops
  the cutscene letterbox and its viewport shrink. Original experience keeps them.
- `crosshair_dark`, `crosshair_echo` (default off, env `MP2_CROSSHAIR_DARK`, `MP2_CROSSHAIR_ECHO`):
  HUD and view > Crosshair in Dark Visor / Echo Visor; off fades the aiming crosshair (every style,
  Trilogy's aiming cursor too) out as the Scan Visor does. Original experience shows it (MP1 486f7a0df).
- `ball_bars` (default off, env `MP2_BALL_BARS`): HUD and view > Morph ball black bars; the ball
  view's narrowed viewport (tweak `BallViewportYReduction`). Off, the world fills the screen in ball
  mode and the ball HUD stays put. Original experience keeps the bars (MP1 535ccd192).
- `secondary_crosshairs` (default on, env `MP2_SECONDARY_CROSSHAIRS`): HUD and view > Secondary
  crosshairs; off hides the next-target reticle (lock-on and the scan box stay).
- `dark_visor_full` (default off, env `MP2_DARK_VISOR_FULL`): Enhancements > Full-screen Dark
  Visor; the Dark Visor's effect covers the whole view (the split-screen path, no lens mask),
  and the HUD's centre gear, backing, spinner and rings are hidden (the boss bar stays). Original
  experience keeps the lens.
- `dark_visor_static` (0-100, default 100, env `MP2_DARK_VISOR_STATIC`): Enhancements > Dark Visor
  static; how much of the visor's grey static covers the scene. Lower keeps the scene's colours,
  0 leaves only the highlights. Original experience keeps 100.
- `infinite_beam_ammo` (default off, env `MP2_INFINITE_BEAM_AMMO`): Enhancements > Infinite Light
  and Dark Ammo; the beams spend no ammo and the HUD shows infinity. Saved, unlike the Infinite
  cheat. Original experience turns it off.
- `dark_world_light_music` (default off, env `MP2_DARK_WORLD_LIGHT_MUSIC`, issue #36):
  Enhancements > Music > Light world music in Dark Aether; each region's Dark Aether theme plays
  its light world theme (Temple Grounds, Agon, Torvus and its underground, Sanctuary, energy
  controllers), so a portal hop keeps the track going. Boss, event and cutscene music is
  unchanged. Takes effect at the next track start. Original experience turns it off.
- `hud_opacity_echo`, `hud_opacity_dark`, `hud_opacity_scan` (`-1` = the game's Visor Opacity,
  default; else 0-100): HUD and view > Echo/Dark/Scan HUD opacity. Applies to that visor's HUD, its
  visor/beam menu, the radar and the map; the energy readout is part of the HUD frame and has no
  separate opacity. Console `hudopacity <echo|dark|scan> <visor|0..100>`. Original experience
  ignores them.
- `fov`: HUD and view > Field of view (30..120; 0, the **Retail** button, keeps the game's own);
  env `MP2_FOV`.
- `gun_offset`: HUD and view > Arm cannon right/forward/up, written `right,forward,up`; env
  `MP2_GUN_OFFSET`.

Video > Quality and Frame rate:

- `msaa`, `aniso`, `render_scale`: Quality > Anti-aliasing (1 or 4), Anisotropic filtering (env
  `MP2_ANISO`), Resolution scale (0 = Auto render scale).
- `frame_limit`: Frame rate > 60 FPS cap (target).
- `smooth_frames`, `pose_interpolation`, `particle_interpolation`, `look_interpolation`: Frame
  rate > Smooth frames, one switch that sets all four (all on by default).
  `MP2_PRESENT_T=<0..1|cycle|tick>` (also spelled `cycle` or `tick`; not a setting, not saved)
  forces the factor the camera and actors are drawn at between ticks, even with the cap on — a
  test aid, as the console's `present`. `MP2_TICKS_PER_FRAME=<1..16>` runs that many fixed ticks
  per loop for fast harness runs.
- `show_shader_compilation`, `show_menu_hint`: Quality > Show shader compilation (a toast while
  the startup pipeline queue compiles), Show F1 hint at launch; env `MP2_SHOW_SHADER_COMPILATION`,
  `MP2_SHOW_MENU_HINT`.
- `show_cheats`: Debug > Show cheats (only reveals the cheat rows; God mode itself is never
  saved).
- `per_pixel_lighting`: Quality > Per-pixel lighting (Aurora lights per pixel); env
  `MP2_PER_PIXEL_LIGHTING`.
- `MP2_PIPELINE_SEED=0` ignores the embedded pipeline cache seed (docs/RELEASING.md).
- Pipeline memory (env only, not saved; `platform/port_memory_log.cpp`): `MP2_PIPELINE_LAZY=0|1`
  builds pipelines when first drawn instead of all at startup (default on for Android, where a
  low-RAM device - `isLowRamDevice()` or under 4 GiB - is also named in the startup line; off on
  desktop). With it on, the memory log (Linux and Android) checks system memory every 5 s and evicts idle pipelines when
  available memory falls below `MP2_PIPELINE_TRIM_MIB` (default 1024: those unused for 3600
  frames) or `MP2_PIPELINE_TRIM_SEVERE_MIB` (default 512: those unused for 16 frames), at most
  every 30 s while it stays low. `MP2_PIPELINE_EVICT=<frames>` evicts pipelines unused for that
  many frames regardless of memory. Console `pipetrim [frames]` forces one eviction.
- `dynamic_res`, `dynamic_res_target`, `dynamic_res_min`: Quality > Dynamic resolution (Off, Auto,
  User; the mode is `dynamic_res` with `render_scale`: Auto goes up to the window's own scale
  (`render_scale` 0), User up to the "Maximum resolution scale" set), Frame rate target (0 =
  Display, the display's rate, else 30..500 fps; never above the frame rate cap), Minimum
  resolution scale (0.25..the top, 4 at most; the sliders show the lines drawn, 1x being 480p);
  env `MP2_DYNAMIC_RES`, `MP2_DYNAMIC_RES_TARGET`, `MP2_DYNAMIC_RES_MIN`. Auto starts over when the
  window is resized and does nothing while its top is not above the minimum (F1 says so).
  `MP2_DYNRES_GPU_TIMES=0` steers on the frame rate alone, without GPU timestamps.
- `upscale_sharpen` (`off`, `dynamic`, `always`), `upscale_sharpen_amount` (0..100): Quality >
  Sharpen when upscaled, Sharpening; env `MP2_UPSCALE_SHARPEN`, `MP2_UPSCALE_SHARPEN_AMOUNT`.
- `fps_cap`: Frame rate > FPS cap with the 60 FPS cap off (0 = Unlimited, else 30..500); env
  `MP2_FPS_CAP`.

Video > Compatibility (read once, when the GPU starts: a change applies after a restart):

- `graphics_backend` (`auto`, `d3d12`, `vulkan`, `d3d11`, `opengl`; Windows and desktop Linux, MP1
  sync 17, OpenGL since the Dawn fork switch): Renderer. Windows offers Auto, Direct3D 12,
  Vulkan, Direct3D 11 and OpenGL; Linux offers Auto, Vulkan and OpenGL. Auto leaves the choice to
  Aurora (Direct3D 12, then Vulkan, then Direct3D 11 on Windows; Vulkan on Linux); an explicit API is
  tried first and falls back to that order when it cannot start, leaving the setting alone. Env
  `MP2_GRAPHICS_BACKEND` overrides it for one run (an unknown value, or one this platform does not
  offer, warns and keeps the setting). A saved API the platform does not offer asks for Auto. Stored
  everywhere, ignored on Android. A desktop OpenGL start that crashes leaves `opengl_starting` in the
  user folder and the next start goes back to Auto. Debug env: `MP2_GL_VERSION=3.3` asks Dawn for a
  desktop GL 3.3 context (toggle `gl_force_desktop_gl_33`), `MP2_GPU_NO_CORE=1` skips the
  CoreFeaturesAndLimits adapter request (the compatibility-mode path). Desktop GL runs on 3.3 or
  newer; below 4.3 the driver also needs GL_ARB_shading_language_420pack,
  GL_ARB_explicit_uniform_location, GL_ARB_texture_storage and GL_ARB_texture_view. Without
  vertex-stage storage buffers or compute shaders (most 3.3 drivers, and any `MP2_GL_VERSION=3.3`
  run) Aurora reads vertex data from R32Uint textures and draws the volumetric fog and depth peek
  with render passes instead (same output; MP1 3c238f415, 1b2aaf269, 68a2b4ad3); game.log says
  `Vertex data from textures` and `No compute shaders`. `AURORA_VERTEX_FETCH_TEXTURE=1` and
  `AURORA_NO_COMPUTE=1` force either path on any backend. Auto tries desktop GL after the other
  GPU backends, before the software adapters. Desktop GL compiles pipelines on one thread unless
  `MP2_PIPELINE_THREADS` is set (MP1 c4db7377a: AMD's Windows driver serialises shared-context
  links and crashed with four).
- `opengles`: Use OpenGL ES (shown only where Aurora has the backend); env `MP2_OPENGLES`. A start
  that crashes leaves the marker `opengles_starting` in the user folder and the next start goes
  back to the default backend.
- `storage_clamp`: Adreno shader fix, -1 Auto, 0 Off, 1 On; sets Aurora's `MP2_STORAGE_CLAMP`
  unless that is already set.
- `low_power_gpu`: Low-power GPU (no MP1 row); sets Aurora's `MP2_GPU_POWER=low` unless set.
- `driver_workarounds`: Driver workarounds, comma-separated `keep_depth`, `gpu_constants`,
  `unmonitored_fence`, `validation` (validation lasts one start), `no_bc` ("Decode compressed
  textures on the CPU"; sets `AURORA_NO_BC=1`, so the device is made without BC texture compression
  and CMPR textures are decoded to RGBA8; Remastered BC replacements are then skipped).
  `MP2_DRIVER_WORKAROUNDS` replaces them for a run; a start that fails leaves `driver_workarounds_starting` and the next start
  clears them.
- `gpu_driver` (Android arm64 only, MP1 port): Vulkan driver, the id of a driver installed under
  `gpu_drivers/<id>/` (empty = System). Env `MP2_GPU_DRIVER` overrides it for one run. A start that
  crashes leaves `gpu_driver_starting` and the next start goes back to System; a driver Vulkan
  cannot start with is switched back to System in the same run. See docs/ANDROID.md.
- GPU self-test (button, no key): logs one PASS/FAIL line per feature; `MP2_GPU_SELFTEST=1` runs
  it at start. Console `gpuselftest` runs it too.

System and Debug:

- `overlay_windowed`, `ui_theme`, `ui_scale`: System > Overlay > Overlay as a floating window
  (desktop), Menu theme (`auto` = Echoes, `prime`, `plain`, `echoes`), UI scale.
- `texture_pack`: Mods > Textures > Use the texture pack (off keeps the folder but shows the game's own
  textures); the Texture pack binding (F7, Controls > Remap > Port) flips it in game.
- `emulator_folder`, `share_emulator_card`, `mirror_trilogy_save` (desktop only): System > Memory
  card > Installed emulators (which Dolphin or PrimeHack install to use; empty = Auto, the first
  one found), Share memory card with emulator (the port and the emulator use the same saves: never
  run both at once; takes effect at the front end), Mirror with the Trilogy save.bin (keeps the
  card and Dolphin's Metroid Prime Trilogy `Wii/title/00010000/52334d45/data/save.bin` in step).
  No env override.
- `card_backup`: System > Memory card > Back up the card: copies the save file into
  `save-backups` before each card write (console `save import latest` restores one).
- `ui_sounds`: settings.cfg only (no F1 row): the game's own pause-screen SFX for opening, closing,
  choosing, moving and backing out of the overlay (off by default; not with the Plain theme).
- `discord`: System > Discord > Rich Presence: area, energy, missiles and item percentage on the
  Discord profile through the local desktop client's IPC socket (off by default; silent without a
  client).
- System > Disc image > Check disc image (no key): hashes the mounted disc in the background against
  the known G2ME01 chunk table (`tools/disc_chunk_table.py`).
- `update_check`: System > Updates > Check for updates (default on); env `MP2_UPDATE_CHECK=0` turns
  it off for a run. Asks GitHub's releases API once at launch whether a newer release is out.
- `disc_path`: System > Disc image > Replace disc image...: the image to mount at the next start,
  checked before it is remembered (empty = the one given on the command line or beside the
  executable). No env of its own: a file on the command line or `MP2_DISC` still wins.
- System > Memory card: Import file... / Export as (`.gci` or a Wii Trilogy save.bin) / Export to
  folder... (a `.gci` of this release through PortSave, current save copied to save-backups first),
  Import from / Export to Dolphin or
  PrimeHack (their GC/USA/Card A folder), Open card folder. System > Data folder: user folder,
  card and backup paths, Open data folder. No keys.
- `log_file`: System > Log > Write the log to a file.
- `photo_supersample`, `photo_speed`: Debug > Photo > Supersample, Camera speed.
