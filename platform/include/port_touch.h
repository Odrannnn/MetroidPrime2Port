// The Android on-screen controller's native side: milestone M3 of docs/ANDROID.md.
//
// The Java view (`android/app/src/main/java/org/metroidprime2/port/TouchControlsView.java`) is
// a copy of `MetroidPrimePort`'s, and this is the native side it expects. MP1 spreads that over
// `platform/touch_pad.cpp` (the SDL virtual gamepad), `platform/debug_ui/input.cpp` (the touch
// settings, the wheels, turbo and beam shift) and `platform/debug_ui/android_jni.cpp` (the JNI
// entry points). MP2 has no input-settings module, so the settings and the game hooks live
// here, and the JNI entry points are `platform/port_touch_jni.cpp`. The virtual gamepad itself
// is `platform/touch_pad.cpp`, ported from MP1 unchanged because it is host-testable there.
//
// What reaches the game:
//
//   - a *real* SDL virtual gamepad (see touch_pad.h): the game's own control mapper, its
//     prompts and its C-stick handling all apply to touch exactly as they do to a real pad;
//   - the touch *aim*, through `PortMouse`;
//   - the visor/beam wheels and the twin layout's beam shift, as `CPlayerState` switches made
//     on the game thread in `BeginFrame` (the console's `visor`/`beam` path);
//   - Turbo, as the pad's A mashed every other two iterations while the button is held.
//
// Settings are MP1's touch settings, persisted in `<user>/touch.cfg` (`key=value`) and
// overridable with `MP2_TOUCH_*` env vars (an env value wins over the file). MP2's F1 window
// has MP1's touch rows of Controls > Touch & gyro; the overlay's own Edit button opens the
// editor and the editor's Layout button cycles the layouts.
//
// Not ported, because MP2 has no backend for them: the map screen's
// pan/zoom/rotate (the minimap tap is `PortMinimap`, see port_pointer.h), pointer menus, the game's own wheel icons (the wheels show text), the
// update toast and `touch:` control bindings. Their natives answer "no" (see the JNI file).
//
// Every getter here is safe from either thread: the Java view polls on the Android UI thread
// while the game thread reads the same state each iteration.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class CGuiWidget;

namespace PortTouch {

// Called once from the entry, before Aurora and before the game exists, with the user
// directory from `main.cpp`'s path resolution. An empty path keeps the defaults, writes nothing.
void Initialize(const std::string& userDirectory);

// One per game-loop iteration, on the game thread, from `PortPumpEvents`: applies the aim
// source to `PortMouse`, the wheel and beam-shift requests to the player, Turbo to the pad,
// rebuilds the wheel state the Java side polls, and flushes a settings change to touch.cfg.
void BeginFrame();

// --- the virtual gamepad (`nativeVirtualButton` / `nativeVirtualAxis`) ---

// SDL_GamepadButton index and a normalised -1..1 axis value. The pad attaches on first use and
// `BeginFrame` retries a failed attach. While the beam shift is held a D-pad press picks a beam
// instead of reaching the pad.
void SetButton(int button, bool down);
void SetAxis(int axis, float value);

// --- aim (`nativeTouchAim` / `nativeTouchAimDown` / `nativeTouchSpringSwipe`) ---

void AddAim(float dxDp, float dyDp);
void SetAimDown(bool down);
void SpringSwipe();
// A swipe up, the Spring Ball binding or a gyro flick asked for a Spring Ball in the last 120 ms
// (200 ms for a flick, so one just before landing counts); read by CMorphBall. `player` 1-3:
// Players 2-4's own Spring Ball binding.
bool SpringBallRequested(int player = 0);
// MP1's PortDebug::RequestSpringBall: asks for one. CMorphBall springs on the request only on the
// ground in the ball with Spring Ball on, so elsewhere it expires. The clear is the one spring
// per request.
void RequestSpringBall(float seconds = 0.12f, int player = 0);
void ClearSpringBallRequest(int player = 0);
// The Dodge action (MP1 issue #236): a sideways dash, locked on or not. The request lasts 120 ms;
// That player's CPlayer takes it once, only on the ground while strafing (false under Original
// Experience).
void RequestDodge(int player = 0);
bool TakeDodgeRequest(int player = 0);

// Drag on the free screen to aim (MP1's `touch_aim`, on by default; `MP2_TOUCH_AIM`).
bool AimEnabled();
// Aim pixels per dp of drag (MP1's `touch_aim_speed`, 2.25); `MP2_TOUCH_AIM_SPEED`.
float AimSpeed();
// "Separate vertical speed##touch" (touch.cfg `aim_split`, `aim_speed_y`): up and down drags at
// their own speed, same range.
bool AimSplit();
float AimSpeedY();

// --- layouts and cosmetics (polled once per draw) ---

// Default (both false: MP1's layout with the wheels), Classic (GameCube) or Twin stick.
bool Classic();
bool TwinStick();
bool Labels();
bool Colors();
bool FloatingStick();
bool Turbo();
bool WheelsEnabled();
bool VisorTapScan();
bool ControlsEnabled();
// The F1 Controls > Touch & gyro rows' setters; each persists to touch.cfg.
void SetControlsEnabled(bool enabled);
void SetAimEnabled(bool enabled);
void SetColors(bool enabled);
void SetLabels(bool enabled);
void SetTurbo(bool enabled);
void SetFloatingStick(bool enabled);
void SetWheelsEnabled(bool enabled);
void SetVisorTapScan(bool enabled);
// A quick swipe up on the free area springs the ball (MP1's `touch_spring_swipe`; default on).
bool SpringSwipeEnabled();
void SetSpringSwipe(bool enabled);
void SetSideMarginDp(float dp);
void SetStickInsetDp(float dp);
void SetButtonInsetDp(float dp);
void SetOpacity(float percent);
void SetAimSpeed(float pxPerDp);
void SetAimSplit(bool enabled);
void SetAimSpeedY(float pxPerDp);
float SideMarginDp();
float StickInsetDp();
float ButtonInsetDp();
float Opacity();

// The per-control layout the Java editor edits: `<id>:<dx>,<dy>,<scale>;...`. Opaque here.
std::string Layout();
void SetLayout(const std::string& layout);

// The editor's entry points: the overlay's Edit button raises the request, the view takes it.
void RequestEdit();
bool TakeEditRequested();
// CPlayer::Update publishes whether each player is out of the unmorphed state (player 0's is bit
// 13 of the wheel state, the Java view's Ball page), as MP1's CPlayer does; Players 2-4's pick
// their pads' Morph Ball rows.
void SetMorphed(bool morphed, int player = 0);
bool Morphed(int player = 0);
// CStateManager::Update publishes whether the game is soft paused: the scan text is up (CPlayer's
// scan freeze, its only soft pause).
void SetScanTextUp(bool up);
bool ScanTextUp();
// Steps Default -> Classic -> Twin -> Default and persists it. The per-control offsets stay:
// the Java view keys them by control id, which differs between layouts, as in MP1.
void CycleLayout();

// --- the game state the overlay reads ---

// Bits 0-3 visors owned, 4-7 beams owned (item indices: visors Combat/Echo/Scan/Dark, beams
// Power/Dark/Light/Annihilator), 8-9 current visor, 10-11 current beam, 12 valid. 0 when no
// player is in play. Built on the game thread by `BeginFrame`.
int WheelState();
// Queue a switch to an owned visor or beam (item index); applied by the next `BeginFrame`.
// `player` 1-3: Players 2-4's, applied while their match is in play.
void RequestVisor(int visor, int player = 0);
void RequestBeam(int beam, int player = 0);
// Game thread, from CHudVisorBeamMenu::Update: stores the HUD's four icons of a wheel (0 visor,
// 1 beam; item order as RequestVisor/RequestBeam) as ARGB once they have loaded. A no-op for the
// ones already taken.
void CaptureWheelIcons(int wheel, CGuiWidget* const* icons);
// {width, height, ARGB pixels...} of a captured icon, or false until the HUD has loaded it.
bool WheelIcon(int wheel, int item, std::vector< uint32_t >& out);
// The twin layout's Beam button, and the Turbo button.
void SetBeamShift(bool held);
void SetTurboFire(bool held);
// The game is paused (kFS_Paused: the pause menu, or a HUD memo / message screen - MP2's flow
// state does not tell those apart).
bool PauseScreenOpen();

// True while the port's F1 overlay is up.
bool OverlayVisible();

// The overlay has just been used, and in which layout (for the prompt icons, not built yet).
void NoteDevice(bool xboxLayout);
bool DeviceIsXbox();
// True only while touch was last used, in a touch build and outside F1.
bool Active();

// Whether a real pad, keyboard or mouse has been used since the last call: the overlay hides.
void NotePhysicalInput();
bool TakePhysicalInput();

} // namespace PortTouch
