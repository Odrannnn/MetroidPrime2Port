#pragma once

#include <cstdint>
#include <string>

#include "port_graphics_backend.h"

// The F1 debug overlay: a small ImGui window with the port's live settings and the warp
// buttons, so the knobs do not need environment variables and the game can be driven around
// without walking. Aurora creates the ImGui context and feeds it the SDL events; the overlay
// only has to build its windows between `aurora_begin_frame` and `aurora_end_frame`.

class CGameOptions;
class CWorld;
class CWorldLayerState;

namespace PortOverlay {

// The game's own options (rumble, invert Y, brightness, volumes...), kept in settings.cfg as the
// Prime 1 port keeps them (`game_options`). Retail keeps them per save file, so a change the card
// never saved was lost; here the file holds the last options set anywhere and they are global.
// RestoreGameOptions applies the file to the live options at boot and after a reset, and starts
// SyncGameOptions recording; ApplySavedGameOptions overrides a save file's options as it loads
// (false when the file has none); SyncGameOptions, once a frame, records any change.
void RestoreGameOptions();
bool ApplySavedGameOptions(CGameOptions& options);
void SyncGameOptions();

// Published by the game every update, so the window can say where the player is and offer the
// warp and item pages: the ids are the game's own (a world asset id and an area index), and
// `world` is the live world, from which the room list is copied then and there - the list is a
// snapshot, because a world teleport frees the world while the overlay still draws the frames
// before the restart replaces it. `world` is null when there is none; the pages then say so
// instead of drawing an empty list.
void SetGameState(uint32_t worldId, int areaId, CWorld* world);
// The current area's script layers (name, active in the world's layer state), copied every update
// for the Cheats page's layer toggles; `layers` null or an area it lacks clears the list.
void SetAreaLayers(const CWorldLayerState* layers, int areaId);
// "Temple Grounds - Landing Site": the English world and room names, as far as they are known
// (rooms only for the current world, once their name tables have loaded); empty when the room's is not.
std::string RoomName(uint32_t worldId, int areaIndex);
// Opens the window on the first page whose name starts with `name` (case-insensitive: "save"
// is Save states), or closes it for "off"; false when no page matches (console `overlay`).
// "<page>/<sub-tab>" also selects the first sub-tab whose name starts with the part after the slash.
bool ShowPage(const std::string& name);
// Toggles the window; safe to call from the event loop (F1).
void Toggle();
// Toggles it on the game thread's next frame (MP1's PortDebug::RequestToggle): for the Android
// UI thread, whose direct Toggle raced the frame (ImGui's input queue, PortMouse) (#27).
void RequestToggle();
// Scrolls the open window by `steps` rows: the phone's two-finger drag, since a finger cannot turn
// a mouse wheel and the window is taller than the screen. Queued in ImGui's input queue.
void Wheel(float steps);
// Debug setting (`MP2_SKIP_CINEMATICS=1`): cinematics end as soon as they start, including ones
// the player has never seen, which the game otherwise refuses to skip. Testing only - it skips
// story beats along with the intro.
bool SkipCinematics();
void SetSkipCinematics(bool skip);
// Two-press cinematic skip: the first A/Start press only arms a "Skip [A]" prompt for
// kSkipConfirmSeconds (real time, bottom centre); a second press while armed skips. Armed state
// lives here so CMFGame, CMFGameLoader and the overlay (and a tap on the prompt) share it.
// Disarmed on expiry or when the cinematic ends. Harness and console skips never use it.
const float kSkipConfirmSeconds = 3.f;
bool SkipConfirmArmed();
void SkipConfirmArm();
void SkipConfirmDisarm();
// 0..1 for the prompt's draw (fades out over its last half second), 0 when not armed.
float SkipConfirmAlpha();
// A tap/click on the prompt happened since the last call (the game then counts it as the second press).
void SkipConfirmTap();
bool TakeSkipConfirmTap();
// Show / Skip with A or Start / Off, for the two settings below (F1 > Game > Cutscenes).
const int kScreen_Show = 0;
const int kScreen_Skippable = 1;
const int kScreen_Off = 2;
// The health warning and the Nintendo, Retro and Dolby logos at launch (CSplashScreen,
// `MP2_BOOT_LOGOS=show|skip|off`): Skippable ends the current one with its 0.5 s fade on A or
// Start (the default; Original experience shows them); Off goes straight from the progressive
// check to the front end.
int BootLogos();
// The world-name card while a game loads (CMFGameLoader/CWorldTransManager,
// `MP2_WORLD_NAME=show|skip|off`): Skippable types it out at once on A or Start, so it fades as
// soon as the load is done; Off loads behind the plain black transition. The first boot's intro
// text follows SkipCinematics (A or Start, seen or not), else the game's rule (Start once seen).
int WorldNameCard();
// Cheat (`MP2_GODMODE=1`, F1 > Cheats): the player takes no damage, so a hazard cannot end a
// test run in the lose-game screen (CStateManager::ApplyLocalDamage). Never saved.
bool GodMode();
void SetGodMode(bool enabled);
// Cheat (`MP2_NO_DARK_AETHER_DAMAGE=1`, F1 > Cheats): Dark Aether's air does not hurt, as with the
// Light Suit (CPlayer::UpdateDarkAetherDamage); safe zones still heal. Never saved.
bool NoDarkAetherDamage();
void SetNoDarkAetherDamage(bool enabled);
// Cheat (F1 > Cheats, a checkbox per counter; `MP2_INFINITE_AMMO=1` turns on all four): firing
// spends none of the chosen ammo (CPlayerState::DecrementAmmoAndDisplayAlertIfOut). Never saved.
enum : unsigned {
  kInfAmmoMissile = 1,
  kInfAmmoPowerBomb = 2,
  kInfAmmoDark = 4,
  kInfAmmoLight = 8,
  kInfAmmoAll = 15,
};
// The kInfAmmo bit of a CPlayerState::EItemType, 0 for an item that is not ammo.
unsigned InfiniteAmmoKind(int itemType);
// True when every kind in `kinds` is infinite, by the cheat or by InfiniteBeamAmmo.
bool InfiniteAmmo(unsigned kinds);
// Enhancement (F1 > Enhancements, saved, env `MP2_INFINITE_BEAM_AMMO`): Light and Dark Ammo are
// infinite, whatever the cheat's bits say. Original experience turns it off.
bool InfiniteBeamAmmo();
void SetInfiniteAmmo(unsigned kinds, bool enabled);
// Gameplay tweaks (F1 > Enhancements, the pause menu's Options > Controller, and one console command
// each), off by default and saved in the settings file.
//
// Fast Morph (`MP2_FAST_MORPH=1`): the morph and unmorph transitions take kFastMorphDuration
// seconds instead of the game's one, so rolling is not a second of animation each way
// (CPlayer::UpdateMorphBallState).
const float kFastMorphDuration = 0.2f;
bool FastMorph();
void SetFastMorph(bool enabled);
// Point and click menus (`MP2_POINTER_MENUS`, default on; port_pointer.h) and the map's finger
// swap (`MP2_MAP_SWAP_FINGERS`): touch pans the map with one finger and rotates with two.
bool PointerMenus();
void SetPointerMenus(bool enabled);
bool MapSwapFingers();
void SetMapSwapFingers(bool swap);
// Map rotate flips (Controls > Options > Map, `MP2_MAP_INVERT_X`/`_Y`): the game turns the map
// opposite to the stick, so flipping both gives camera-style rotation (CAutoMapper).
bool MapInvertX();
void SetMapInvertX(bool invert);
bool MapInvertY();
void SetMapInvertY(bool invert);
// Walk speed (`MP2_WALK_SPEED`, percent, default 100 = retail): scales Samus's on-foot top speed,
// acceleration and friction together (and the orbit strafe/approach speeds), so she moves k times
// as fast and gets there in the same time. Jumps, sideways dashes, turning and the Morph Ball are
// unchanged (CPlayerDynamics WalkScale).
const int kWalkSpeedMin = 50;
const int kWalkSpeedMax = 200;
const int kWalkSpeedDefault = 100;

// Ammo costs and beam ammo capacity (the F1 Cheats page, #27). The defaults are retail.
const int kComboBeamCostMax = 30;      // combo beam ammo cost: 0..30, 30 = retail
const int kComboMissileCostMax = 5;    // combo missile cost: 0..5, 5 = retail
const int kBeamAmmoPerExpansionMin = 50; // percent of 50 ammo per pickup: 50 (retail) .. 100
const int kBeamAmmoPerExpansionMax = 100;
const int kBeamAmmoPerExpansionDefault = 50;
int ComboBeamCost();
void SetComboBeamCost(int cost);
int ComboMissileCost();
void SetComboMissileCost(int cost);
// Dark/Light ammo a capacity pickup grants is multiplied by this / 50 (retail: 50).
int BeamAmmoPerExpansion();
void SetBeamAmmoPerExpansion(int ammo);
int WalkSpeed();
void SetWalkSpeed(int percent);
// Scan speed in percent (`MP2_SCAN_SPEED`, 100 = retail): ScanSpeed() scales the time a scan takes
// (CPlayer::UpdateScanningState).
const int kScanSpeedMin = 50;
const int kScanSpeedMax = 400;
const int kScanSpeedDefault = 100;
int ScanSpeed();
void SetScanSpeed(int percent);
// Music and sound-effects boost in percent (`MP2_MUSIC_BOOST`, `MP2_SFX_BOOST`, 100 = no boost):
// the port's mixer scales the music path (the streamed voices) or every other voice, so 150 plays
// that path louder than the game's own volume maximum alone. The F1 Game > Audio sliders use it
// for their part above 100 percent (#41).
const int kVolumeBoostMin = 100;
const int kVolumeBoostMax = 200;
const int kVolumeBoostDefault = 100;
int MusicBoost();
void SetMusicBoost(int percent);
int SfxBoost();
void SetSfxBoost(int percent);
// Glyph set the on-screen prompts are forced to (`MP2_PROMPT_GLYPHS`: 0 follow the input in use,
// 1 Xbox, 2 PlayStation, 3 Switch, 4 GameCube, 5 keyboard); PromptGlyphs() is the device name or
// nullptr to follow the input. Original experience shows the GameCube art.
enum EPromptGlyphs {
  kPromptGlyphs_Auto,
  kPromptGlyphs_Xbox,
  kPromptGlyphs_PlayStation,
  kPromptGlyphs_Switch,
  kPromptGlyphs_GameCube,
  kPromptGlyphs_Keyboard
};
const char* PromptGlyphs();
// Tap or click on the HUD minimap opens the map screen (default on; MP2_MINIMAP_TAP).
bool MinimapTap();
void SetMinimapTap(bool enabled);
// Elevator ride (`MP2_ELEVATOR_RIDE`: original|fast|skip): Fast doubles the ride's clock, Skip ends
// it as soon as the next world has loaded (CWorldTransManager).
enum EElevatorRide { kElevatorRide_Original, kElevatorRide_Fast, kElevatorRide_Skip };
int ElevatorRide();
void SetElevatorRide(int mode);
// Portal transition (the light/dark world portal ride): same modes as the elevator ride.
enum EPortalTransition { kPortalTransition_Original, kPortalTransition_Fast, kPortalTransition_Skip };
int PortalTransition();
void SetPortalTransition(int mode);
// Rotating rooms (`MP2_ROTATING_ROOMS`: original|fast): the energy controller access rooms'
// bomb-slot cutscene, where the room turns. Fast runs the cutscene's clock (camera, platforms,
// timers) at double speed while a timed, one-shot CScriptActorRotate plays under a cinematic camera.
enum ERotatingRooms { kRotatingRooms_Original, kRotatingRooms_Fast };
int RotatingRooms();
void SetRotatingRooms(int mode);
// CScriptActorRotate::Think reports a timed one-shot rotation each update; CStateManager::Update
// asks whether one played in the last updates (frame is CStateManager::GetUpdateFrameIdx).
void NoteRotatingSequence(unsigned frame);
bool RotatingSequenceActive(unsigned frame);
// Text language (`MP2_LANGUAGE`: 0 English, 1 German, 2 French, 3 Spanish, 4 Italian, 5 Dutch,
// 6 Japanese): CMain::GetLanguage returns it at startup and the overlay re-applies it every frame
// (CStringTable::SetLanguage), so a change shows at once in text read from then on; a screen that
// already copied its text shows the new language when it next opens. A string table without that
// language falls back to English (PAL discs carry English, French, German, Spanish, Italian and
// Japanese; the USA disc English only).
const int kLanguageMax = 6;
int Language();
void SetLanguage(int index);
// On-screen in-game time (`MP2_IGT_TIMER`): the save file's play time in the bottom-right corner.
// NoteGameTime is fed by the game tick, ClearGameTime when the game session ends.
bool IgtTimer();
void SetIgtTimer(bool enabled);
void NoteGameTime(double seconds);
void ClearGameTime();
bool GameTime(double& seconds);
// Return to the title (F1 > Game): RequestReturnToTitle asks the next game tick to quit as the
// pause menu's Quit does; ConsumeReturnToTitleRequest takes it.
void RequestReturnToTitle();
bool ConsumeReturnToTitleRequest();
// Original experience (`MP2_ORIGINAL_EXPERIENCE`): the port's gameplay and cutscene tweaks read as
// retail while on (skippable cutscenes, world name card, walk speed, hint delay, scan speed,
// elevator ride, fast morph, spring ball, toggle lock-on, sticky charge, rapid charge, auto-aim on).
// Mods, input, video and audio settings are not touched.
bool OriginalExperience();
void SetOriginalExperience(bool enabled);
// Unlocks that finishing the game gives (F1 > Game > Unlocks), applied without writing the save, so
// turning one off locks it again. UnlockHardMode (`MP2_UNLOCK_HARD_MODE`) offers the difficulty
// choice on an empty file slot as after NormalModeCompleted; UnlockGalleries
// (`MP2_UNLOCK_GALLERIES`) makes CSlideShow::GetGalleriesUnlocked() report every gallery. Both read
// false while Original experience is on.
bool UnlockHardMode();
void SetUnlockHardMode(bool enabled);
bool UnlockGalleries();
void SetUnlockGalleries(bool enabled);
// Beam doors (`MP2_UNLOCK_BEAM_DOORS`): a colour-locked door (a door or damageable trigger that some
// of the Power, Dark, Light and Annihilator beams hit and others do not) takes any of the four beams.
// Stack beams (`MP2_STACK_BEAMS`): a beam fired while another is selected also fires the other
// beams Samus owns, as in Prime 1's stacked beams. Both read false while Original experience is on.
// Skip Dark (`MP2_STACK_BEAMS_SKIP_DARK`): the stack leaves the Dark Beam out unless it is in hand,
// so stacked shots don't drain light beacons and crystals.
bool UnlockBeamDoors();
void SetUnlockBeamDoors(bool enabled);
bool StackBeams();
void SetStackBeams(bool enabled);
bool StackBeamsSkipDark();
void SetStackBeamsSkipDark(bool enabled);
// Hint delay in minutes (`MP2_HINT_DELAY`, 0 = the game's own timing): HintDelaySeconds() caps the
// wait before a hint first shows (CHintOptions::Update), 0 when off. RequestShowHint() (F1 button,
// `hintshow`) asks the next CHintOptions::Update to show the hint the game would show next;
// ConsumeShowHintRequest() takes the request.
const int kHintDelayMin = 0;
const int kHintDelayMax = 30;
float HintDelaySeconds();
void RequestShowHint();
bool ConsumeShowHintRequest();
// Spring Ball (`MP2_SPRING_BALL=1`): C-stick up on the ground in Morph Ball is a bomb jump from the
// ball's own position, as Trilogy's nunchuk flick is, so the ball can leave the ground without a
// bomb to spend (needs Morph Ball Bombs; CMorphBall::ComputeBallMovement).
// Quick resume (`MP2_QUICK_RESUME`, default on): quitting during play writes savestates/resume.mp2ss
// and the next launch offers "Resume where you left off?" (PortSaveState::Resume*).
bool QuickResume();
void SetQuickResume(bool enabled);
// `savestate_hotkeys` (`MP2_SAVESTATE_HOTKEYS`, default on): the Save state / Load state bindings act.
// Off, they do nothing; the Save states tab and the console `state` command still work.
bool SaveStateHotkeys();
// `savestate_room_state` (`MP2_SAVESTATE_ROOM_STATE`, default on): states and quick resume keep the
// loaded rooms' enemies and objects. Off, a load rebuilds the room as a memory card load does.
bool SaveStateRoomState();
bool SpringBall();
void SetSpringBall(bool enabled);
// Spring Ball on a gyro flick (`MP2_SPRING_BALL_FLICK`, MP1's `spring_ball_flick`): the pad or phone
// tilted up faster than the rate (`MP2_SPRING_BALL_FLICK_RATE`, rad/s) springs, like Trilogy's
// nunchuk flick. Works with gyro aim off (PortInputDevices::PollGyro); still needs Spring Ball.
const float kSpringFlickRateMin = 2.f;
const float kSpringFlickRateMax = 20.f;
const float kSpringFlickRateDefault = 6.f;
bool SpringBallFlick();
void SetSpringBallFlick(bool enabled);
float SpringBallFlickRate();
// Toggle Lock-On (`MP2_LOCK_ON_TOGGLE=1`): an L press that locks on latches L until the next press,
// and a lock that ends (target killed, out of range) lets go by itself. Sticky Charge
// (`MP2_STICKY_CHARGE=1`): letting go of an A hold of kStickyChargeAfter or more keeps the beam
// charging until the next press fires it; taps shoot as usual. Both are the state machines in
// port_hold_toggle.h, applied to the player's own CFinalInput in CStateManager::ProcessInput.
const float kStickyChargeAfter = 0.35f;
bool LockOnToggle();
void SetLockOnToggle(bool enabled);
bool StickyCharge();
void SetStickyCharge(bool enabled);
// Auto-aim (`MP2_AUTO_AIM=0` turns it off, default on): off, the arm cannon aims only at a
// locked or orbited target, as MP1's auto-aim switch does (CPlayer::UpdateAimTarget).
bool AutoAim();
void SetAutoAim(bool enabled);
// Pointer aim, Trilogy style (`MP2_POINTER_AIM=1`, default off; MP1's pointer aim): the mouse,
// twin stick or touch aim moves a pointer instead of the view. Inside the dead box
// (`MP2_POINTER_AIM_BOX`, percent of the half-screen) the view holds still; past it the view
// turns toward that edge, up to `MP2_POINTER_AIM_TURN` degrees per second at the edge. The arm
// cannon aims along the pointer's ray (CPlayer::UpdateGunTransform). Needs a direct aim source.
const int kPointerAimBoxMin = 20;
const int kPointerAimBoxMax = 95;
const int kPointerAimBoxDefault = 70;
const int kPointerAimTurnMin = 30;
const int kPointerAimTurnMax = 720;
const int kPointerAimTurnDefault = 180;
bool PointerAim();
bool TrilogyRadialSelectors();
void SetTrilogyRadialSelectors(bool enabled);
void SetPointerAim(bool enabled);
int PointerAimBox();
int PointerAimTurn();
// Gyro aim (`MP2_GYRO_AIM=1`, default off; MP1's gyro aim in "always aim" mode, pointer only): a
// controller's gyro (or the phone's, `MP2_GYRO_SOURCE` 0 auto, 1 controller, 2 phone) moves the
// pointer while pointer aim is on. `MP2_GYRO_CURSOR_ANGLE` degrees of pad turn sweep the pointer
// across the screen. Space (`MP2_GYRO_SPACE`, PortGyro::Space), auto calibrate
// (`MP2_GYRO_AUTO_CALIBRATE` 0 off, 1 always, 2 only in menus), smoothing and tightening
// (`MP2_GYRO_SMOOTHING`/`MP2_GYRO_TIGHTENING`, deg/s, 0 = off) are MP1's, ranges and defaults.
const int kGyroSourceDefault = 0;
const int kGyroSpaceDefault = 1;
const int kGyroAutoCalibrateDefault = 1;
const float kGyroThresholdMax = 50.f;
const float kGyroCursorAngleMin = 5.f;
const float kGyroCursorAngleMax = 180.f;
const float kGyroCursorAngleDefault = 40.f;
// The "Gyro aim" binding's mode (`MP2_GYRO_MODE`, MP1's numbers): 1 hold to aim, 2 always aim,
// 3 hold to pause, 4 toggle to aim. "Gyro also turns the camera" (`MP2_GYRO_CURSOR_CAMERA=1`)
// turns the view by `MP2_GYRO_SENSITIVITY` (0.1..10, x the pad's turn) while the gyro moves the
// pointer.
const int kGyroModeMin = 1;
const int kGyroModeMax = 4;
const int kGyroModeDefault = 2;
const float kGyroSensitivityMin = 0.1f;
const float kGyroSensitivityMax = 10.f;
const float kGyroSensitivityDefault = 2.f;
bool GyroAim();
int GyroMode();
float GyroSensitivity();
bool GyroCursorCamera();
// MP1's "Separate vertical speed" for the gyro (`MP2_GYRO_SPLIT`): tilting up and down turns at
// `MP2_GYRO_SENSITIVITY_Y` (same range); under pointer aim it scales the pointer's vertical travel.
bool GyroSplit();
float GyroSensitivityY();
// Lock-on free aim (Remastered): under a combat lock the mouse, twin stick, gyro or a touch drag
// moves a crosshair over the view and shots follow it, with or without pointer aim
// (`MP2_LOCK_ON_FREE_AIM`). CPlayer::PortLockFreeAim reads it.
bool LockOnFreeAim();
// The Reset aim binding recentres the pointer under pointer aim, else levels the camera;
// `MP2_RESET_AIM_BOTH` makes it do both either way.
bool ResetAimBoth();
// MP1's "Separate menu buttons" (`MP2_MENU_STANDARD_BUTTONS`): menus and the map read the Remap
// page's Menu rows instead of the gameplay layout. Off while the touch controls are in use.
bool MenuStandardButtons();
void SetMenuStandardButtons(bool enabled);
void SetGyroAim(bool enabled);
int GyroSource();
int GyroSpace();
int GyroAutoCalibrate();
float GyroSmoothing();
float GyroTightening();
float GyroCursorAngle();
// Remastered charge (`MP2_RAPID_CHARGE=1`), Power Beam only: holding fire after the press shot
// fires two quick shots, then charges faster (CPlayerGun::PortRapidChargeShot).
bool RapidCharge();
void SetRapidCharge(bool enabled);
// Back up the card before the game writes it (`MP2_CARD_BACKUP=1`, F1 > Saves): each write to the
// memory card copies the file as it is on disk first, into `<user>/save-backups`, so a save that
// goes wrong can be imported back (console `save import latest`). Off by default; never saved.
// Hooked in CMemoryCardSys::CCardFileInfo::WriteFile through PortSave::BeforeCardWrite.
bool CardBackup();
void SetCardBackup(bool enabled);
// Whether stdout and stderr also go to a file (`MP2_LOG_FILE=1`, F1 > Testing, console `logfile`).
// On unless the variable is `0`; PortLogFile::Start reads it at startup, so a change here only
// reports what the next launch will do.
bool LogFile();
void SetLogFile(bool enabled);
// First-person field of view in degrees (`MP_FOV=<deg>`, F1 > Render), or 0 for the game's own 55.
// Applied by CCameraManager::UpdateCameras, because the first-person camera takes its FOV when it is
// built; CCameraManager::mFirstPersonFov is what a rebuilt one reads.
float FirstPersonFov();
// Arm cannon offset from its retail place, in metres along the camera's right, forward and up axes
// (`MP2_GUN_OFFSET="<right>,<forward>,<up>"`, F1 > Render), each clamped to +-kGunOffsetMax. It moves
// the real gun, so shots leave from the moved muzzle (CPlayer::UpdateArmAndGunTransforms).
const float kGunOffsetMax = 1.f;
void GunOffset(float out[3]);
void SetGunOffset(const float offset[3]);
// Multisample anti-aliasing (1 = off, 4 = 4x, the only counts WebGPU guarantees) and the max texture
// anisotropy (1-16; retail-style mipmapped textures ask for the max). `MP2_MSAA` / `MP2_ANISO`, F1 >
// Render; applied by aurora_set_graphics_quality at the next frame start, so a change rebuilds the
// framebuffers and costs a frame.
int Msaa();
void SetMsaa(int samples);
int Anisotropy();
void SetAnisotropy(int maxAnisotropy);
// HUD scale in percent (`MP2_HUD_SCALE=<50-100>`, F1 > HUD), 100 = the layout the game authored. It
// scales the HUD's own projection about the screen centre, so every element shrinks toward the
// middle; the helmet, menus and memo screens are not scaled.
const int kHudScaleMin = 50;
const int kHudScaleMax = 100;
int HudScale();
void SetHudScale(int percent);
// True while CSamusHud::Draw is drawing the HUD: CGuiCamera::Draw scales its projection while it is
// set, so nothing else (pause screen, inventory, memo) is affected.
bool HudScalingActive();
void SetHudScalingActive(bool active);
// Draw without the HUD's view overlays (`MP2_NO_HELMET=1`, `MP2_NO_VISOR_FX=1`, F1 > HUD). Read by
// CInGameGuiManager::Draw (the helmet, through its own mHelmetVisMode gate) and CPlayerVisor::Draw
// (the per-visor effect: on this port that is the scan visor's window and edges).
bool HideHelmet();
bool HideVisorEffects();
// Cutscene black bars (`cinema_bars`, env `MP2_CINEMA_BARS`, F1 > HUD and view): the letterbox the
// game draws over a cinematic and the viewport shrink that goes with it. On by default (the
// game's own); off lets the cinematic fill the screen. Read by CCameraFilterPass. Original
// experience keeps them.
bool CinemaBars();
// Secondary crosshairs (`secondary_crosshairs`, env `MP2_SECONDARY_CROSSHAIRS`): the next-target
// reticle in combat and the seeker ring it shares; the lock-on reticle and the scan box stay. On
// by default; Original experience keeps them. Read by CCompoundTargetReticle::DrawSeeker.
bool SecondaryCrosshairs();
// Full-screen Dark Visor (`dark_visor_full`, env `MP2_DARK_VISOR_FULL`): the visor reveals and
// highlights over the whole view, as the game's own split-screen does, instead of the lens
// (FRME_DarkVisorMask) inside the tweak frame. Off by default; Original experience keeps the lens.
// Read by CStateManager::DrawDarkVisor; CSamusHud hides the centre gear, spinner and rings.
bool DarkVisorFullScreen();
// Dark Visor static (`dark_visor_static`, env `MP2_DARK_VISOR_STATIC`), in percent: how much of
// the grey static image covers the scene; below 100 the scene's own colours show through, 0 keeps
// only the highlights. 100 (the game's look) by default and under Original experience. Read by
// CCubeRenderer::DrawDarkWorldTransition through CStateManager::DrawDarkVisor.
const int kDarkVisorStaticMax = 100;
int DarkVisorStatic();
// HUD opacity per visor (`hud_opacity_echo/dark/scan`), in percent, or kHudOpacityVisor for the
// game's Visor Opacity (Options > Visor). The Combat and Ball HUDs always use the option. Original
// experience ignores these. Read by CGameOptions::GetVisorHudAlpha.
enum HudOpacityPart { kHudOpacityEcho, kHudOpacityDark, kHudOpacityScan, kHudOpacityPartCount };
const int kHudOpacityVisor = -1;
const int kHudOpacityMin = 0;
const int kHudOpacityMax = 100;
int HudOpacity(HudOpacityPart part);
void SetHudOpacity(HudOpacityPart part, int percent);
// The view's aim crosshair while the port's aim owns the body heading (`aim_crosshair`, on by
// default; F1 > HUD, console `crosshair`). The game's own crosshair is a digital input retail
// presses with the look control, which the port never presses - without this the reticle stays
// faded out whenever the mouse, the twin stick or a touch drag is aiming, which is always on the
// phone. The Prime port sets its crosshair bit from its `AimCrosshair` setting the same way.
bool AimCrosshair();
void SetAimCrosshair(bool enabled);
// The aim crosshair, with the Prime port's settings and its meanings (PortDebug's
// ECrosshairStyle/kCrosshairSizeMin..Max/kCrosshairOpacity): the classic style is the game's own
// reticle model with the port's size, opacity and colour applied, and `remastered` replaces it
// with the cursor drawn procedurally in CTargetReticles.cpp (that is the one this port can show:
// the classic model's token does not resolve here). Size is a percent of the model's own scale
// (25..100) and applies only while the port's aim owns the view; opacity is 0..100.
const int kCrosshairSizeMin = 25;
const int kCrosshairSizeMax = 100;
const int kCrosshairSizeDefault = 50;
const int kCrosshairOpacityMin = 0;
const int kCrosshairOpacityMax = 100;
const int kCrosshairStyle_Classic = 0;
const int kCrosshairStyle_Remastered = 1;
// On a Trilogy disc: the GameCube crosshair model in place of Trilogy's aiming cursor; Classic on
// a GameCube disc.
const int kCrosshairStyle_GameCube = 2;
int CrosshairStyle();
void SetCrosshairStyle(int style);
int CrosshairSize();
void SetCrosshairSize(int percent);
int CrosshairOpacity();
void SetCrosshairOpacity(int percent);
// A custom colour for both styles, as the Prime port's "custom colour" option; false keeps the
// tweak's own (the visor's colour).
bool CrosshairColor(float rgb[3]);
void SetCrosshairColor(float r, float g, float b);
void ClearCrosshairColor();
// The F1 window's size in percent of the display's own scale (`MP2_UI_SCALE=50..200`, F1 > Render,
// console `uiscale`); the product is clamped to 1..4 by PortDebug::UpdateUiScale.
const int kUiScaleMin = 50;
const int kUiScaleMax = 200;
int UiScale();
void SetUiScale(int percent);
// Vertical sync (`MP2_VSYNC=1`, F1 > Render, console `vsync`); off by default. Applied through
// aurora_enable_vsync from Draw, even with the window closed.
bool Vsync();
void SetVsync(bool enabled);
// Whether the window is fullscreen (on Android: the status and navigation bars hidden).
bool Fullscreen();
void SetFullscreen(bool enabled);
// MP1's 60 FPS cap (`MP2_FRAME_LIMIT=0|1`, F1 > Video > Frame rate, F10); on by default. The main
// loop sleeps to a 60 Hz deadline in PortPaceFrame (platform/port_events.cpp).
bool FrameLimit();
void SetFrameLimit(bool enabled);
// The time one main-loop iteration spent working, before PortPaceFrame's sleep; feeds the F1
// Frame rate row "Throughput" (frames per second with the pacing wait left out).
void NoteFrameWork(uint64_t ns);
// MP1's smooth frames (`MP2_SMOOTH_FRAMES=0|1`, F1 > Video > Frame rate, console `interp`); on by
// default. With the cap off, the camera is drawn between the last two 60 Hz ticks.
bool SmoothFrames();
void SetSmoothFrames(bool enabled);
// Frame-blend tests (MP1's PortDebug::PresentOverride). `MP2_PRESENT_T=<0..1|cycle|tick>` and the
// console's `present` force the presentation factor the camera and actors blend at, even with the
// cap on and smooth frames off; cycle steps through 0, 0.25, 0.5 and 0.75 per drawn frame. Not
// saved. PresentOverride replaces t and returns true when it did: a value in [0,1] is fixed,
// kPresentCycle cycles, kPresentTick draws the plain tick state (-1, as the cap does), negative
// leaves the clock's factor alone.
constexpr float kPresentCycle = 2.f;
constexpr float kPresentTick = 3.f;
bool PresentOverride(float& t);
// [0,1] fixed, kPresentCycle, kPresentTick, or negative for off.
void SetPresentOverride(float value);
float PresentOverrideValue();
// The console's `hold`/`step` (MP1's TickHold there): while held, the main loop runs no simulation
// ticks except one per queued step, so one tick state can be drawn at several factors. Not saved.
bool TickHold();
void SetTickHold(bool held);
void StepTicks(unsigned count);
unsigned PendingHeldTicks();
unsigned TakeHeldTicks();
// MP1's pose interpolation (`MP2_POSE_INTERPOLATION=0|1`, F1 > Video > Frame rate); off by default.
// With smooth frames on and the cap off, skinned models draw a blend of their last two tick poses
// (CAnimData::PortPresentedPose).
bool PoseInterpolation();
void SetPoseInterpolation(bool enabled);
// MP1's particle interpolation (`MP2_PARTICLE_INTERPOLATION=0|1`, F1 > Video > Frame rate); off by
// default. With smooth frames on and the cap off, particle systems draw between their last two
// tick states (CElementGen::PortBeginPresent) and rain/underwater flakes one step back.
bool ParticleInterpolation();
void SetParticleInterpolation(bool enabled);
// MP1's look input per frame (`MP2_LOOK_INTERPOLATION=0|1`, F1 > Video > Frame rate); on by
// default, as MP1's phase 1 is. With smooth frames on, the cap off and mouse/stick/touch aim
// owning the view, the camera turns between ticks by the pending look travel
// (CCameraManager::GetCurrentCameraTransform, PortMouse::PresentedAimDelta).
bool LookInterpolation();
void SetLookInterpolation(bool enabled);
// Widescreen HUD (`MP2_HUD_WIDE`, MP1's HudWide): on spreads the GUI over the whole framebuffer, off
// keeps the game's 4:3 proportions centred. Only matters while the framebuffer is wider than 4:3.
bool HudWide();
void SetHudWide(bool enabled);
// Widest screen the HUD spreads to (`MP2_HUD_SPREAD_MAX`, key `hud_spread_max`): 0 = 16:9, 1 = 21:9
// (default), 2 = full. Past it the HUD stays centred at that width. HudSpreadCap is the width
// relative to 4:3 that CGuiCamera clamps the spread to.
int HudSpreadMax();
void SetHudSpreadMax(int mode);
const char* HudSpreadMaxName(int mode);
bool ParseHudSpreadMax(const char* name, int& mode);
float HudSpreadCap();
// How much of the horizontal widescreen spread the scan visor's side panels get, in percent
// (`MP2_SCAN_SPREAD`, key `scan_spread`): 100 = fully spread (default), 0 = their 4:3 place near the
// centre, out from under a phone's touch controls (0 by default on Android).
const int kScanSpreadMin = 0;
const int kScanSpreadMax = 100;
int ScanSpread();
void SetScanSpread(int percent);
// MP1's static render scale (`MP2_RENDER_SCALE`, F1 > Video > Quality): 0 = the window's own size
// (default), else kRenderScaleMin-kRenderScaleMax x the render mode's base, through
// VISetFrameBufferScale from Draw. Photo mode's capture overrides it while it runs.
const float kRenderScaleMin = 0.5f;
const float kRenderScaleMax = 4.f;
float RenderScale();
void SetRenderScale(float scale);
// Rendering aspect ratio, as the Prime 1 port's (`MP2_ASPECT=4:3|16:9|window`, `MP2_WIDESCREEN=1`
// for 16:9, F1 > Video > Display, console `aspect`); Window by default. 4:3 keeps the game's own
// 640x448 picture; 16:9 widens the render mode to 16:9 at the same height, and Window follows the
// window's shape as it is resized. Applied by ApplyAspect.
const int kAspect_4_3 = 0;
const int kAspect_16_9 = 1;
const int kAspect_Window = 2;
int AspectMode();
void SetAspectMode(int mode);
// "4:3", "16:9", "window"; ParseAspect takes the same names (and "16x9"/"4x3") and leaves `mode`
// alone on anything else.
const char* AspectName(int mode);
bool ParseAspect(const char* name, int& mode);
// Widens (or narrows) the render mode's fbWidth to the aspect setting. Called every iteration of
// the game loop by PortPumpEvents, so a window resize and a setting change apply on the next frame.
void ApplyAspect();
// The render mode's width over the game's own (1 at 4:3): CGuiCamera::Draw widens its projection by
// it, so the GUI keeps its proportions on a wide framebuffer.
float AspectWiden();
// Below the game's 4:3 the world view keeps its width and gains height (Vert+, as the Prime 1 port
// does): the vertical field of view in degrees that shows the authored horizontal extent on a
// framebuffer `aspect` wide. Wider than the authored aspect, or non-positive input, returns fov.
float VertPlusFov(float fovDegrees, float aspect);
// The inverse of VertPlusFov: the vertical fov that VertPlusFov widens to `fovDegrees` at `aspect`
// (MP1 227c7fdaa's GetUnwidenedFov). A camera blended into one that gets Vert+ widened starts from
// this, so the blend does not widen twice below 4:3. Identity otherwise.
float UnwidenFov(float fovDegrees, float aspect);
// How far, per unit of view depth, the first-person arm cannon moves down so it stays on the
// screen's bottom edge as at 4:3 when the view is Vert+ widened (MP1 1a83a296d/af5e42ade's
// CGameCamera::VertPlusGunDrop): tan(fov/2) * (authored/aspect - 1), 0 at and above the authored
// aspect. `fovDegrees` is the fov the gun is drawn at (retail's).
float VertPlusGunDrop(float fovDegrees, float aspect);
// The framebuffer width that is shown at the game's 4:3 for the given height (MP1's
// PortDebug::FourThreeWidth): the width itself in 4:3 mode, where 640x448 is displayed at 4:3,
// else height * 4 / 3, since the widened framebuffers have square pixels. Fits 4:3 art into a
// wider viewport: 4:3 art keeps its shape in a band of that width, centred.
int FourThreeWidth(int width, int height);
bool Visible();
// The F1 window, the resume prompt or another ImGui window is taking the mouse (as of the last
// drawn frame), so a press or wheel there must not also reach the game's pointer menus.
bool PointerBlocked();
// Builds the window. Call once per presented frame, inside the open Aurora frame.
void Draw();

} // namespace PortOverlay

// F1 > Video rows from the Prime 1 port (f1b). The settings are in port_overlay.cpp, applied by
// platform/port_video.cpp (include/port_video.h); each `MP2_<KEY>` overrides the file for a run.
namespace PortOverlay {

// Dynamic resolution (`MP2_DYNAMIC_RES`, F1 > Video > Quality): draws the EFB between DynamicResMin
// (`MP2_DYNAMIC_RES_MIN`, kDynamicResMinLowest..4) and RenderScale (auto: the window's own scale) to
// hold DynamicResTarget fps (`MP2_DYNAMIC_RES_TARGET`; 0 = the display's rate, 60 under the cap).
const float kDynamicResMinLowest = 0.25f;
bool DynamicRes();
int DynamicResTarget();
float DynamicResMin();
// Sharpening of a frame drawn smaller than the window (`MP2_UPSCALE_SHARPEN=off|dynamic|always`,
// strength `MP2_UPSCALE_SHARPEN_AMOUNT` 0..100): 0 off, 1 while dynamic resolution has lowered the
// scale, 2 always.
int UpscaleSharpen();
int UpscaleSharpenAmount();
// The frame cap with the 60 FPS cap off (`MP2_FPS_CAP`, F1 > Video > Frame rate); 0 = unlimited.
const int kFpsCapMin = 30;
const int kFpsCapMax = 500;
int ClampFpsCap(int fps);
int FpsCap();
// The startup toasts (`MP2_SHOW_SHADER_COMPILATION`, `MP2_SHOW_MENU_HINT`); on by default.
bool ShowShaderCompilation();
bool ShowMenuHint();
// Aurora's per-pixel lighting (`MP2_PER_PIXEL_LIGHTING`, F1 > Video > Quality); off by default.
bool PerPixelLighting();
// F1 > Video > Compatibility, read when the GPU device is made (so after a restart):
// OpenGL ES instead of Vulkan (`MP2_OPENGLES`, only where Aurora has the backend), the Adreno
// storage clamp (-1 auto; Aurora reads `MP2_STORAGE_CLAMP` itself), the Dawn driver workarounds
// (`MP2_DRIVER_WORKAROUNDS`) and the low-power adapter (Aurora reads `MP2_GPU_POWER=low`).
bool OpenGles();
// The renderer choice (`graphics_backend`, `MP2_GRAPHICS_BACKEND` overrides it for one run;
// see port_graphics_backend.h). Stored on every platform, used on Windows and desktop Linux.
PortGraphicsBackend::EGraphicsBackend GraphicsBackend();
int StorageClamp();
unsigned DriverWorkarounds();
bool LowPowerGpu();

} // namespace PortOverlay
