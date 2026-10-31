// Mouse look: SDL relative motion feeds the aim state in `port_mouse.h`, which the game reads
// from CPlayer (body yaw) and CFirstPersonCamera (view direction). The Prime port drives the
// same state from its own hooks; this is the part that only needs SDL, so it lives in the
// platform layer and is built by `mp_port_audio`, the target with SDL on its include path.
//
// `MP2_MOUSE_AIM=1` turns it on. Off by default: a pad, or the keyboard map in
// `port_events.cpp`, is the retail-shaped way to play, and mouse aim changes movement and
// turning, so it stays opt-in until it has had more play time.

#include "port_mouse.h"
#include "port_radial.h"

#include "port_overlay_internal.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

PortMouse::AimState sAim;
bool sEnabled = false;
bool sStickEnabled = false;
bool sKeyboardCStickHeld = false;
// The Java UI thread can flip this through a layout change while the game thread reads it in
// UpdateAim, so it is atomic unlike the mouse's and the stick's flags.
std::atomic<bool> sTouchEnabled{false};
bool sReadEnv = false;
bool sCaptured = false;
bool sAiming = false;
bool sUiActive = false;
float sSensitivity = 0.0035f; // radians per pixel, the Prime port's default
// MP1's "Separate vertical speed" for the mouse: vertical counts turn at sSensitivityY.
bool sSensitivitySplit = false;
float sSensitivityY = 0.0035f;
int sMouseDpi = 800; // only for F1's cm/360 readout
float sStickAimRate = 900.f;  // aim pixels per second at full stick deflection
// MP1's "Separate vertical speed": with the split on, up and down use their own rate.
bool sStickAimSplit = false;
float sStickAimRateY = 900.f;
float sTouchAimSpeed = 2.25f; // aim pixels per dp, the Prime port's touch_aim_speed default
float sTouchAimSpeedY = 2.25f; // PortTouch pushes its vertical speed (the same one, unsplit)
bool sInvertX = false;
bool sInvertY = false;
// The aim's down state is set by the UI thread (a finger lift) and read every tick by the game
// thread, so it is atomic rather than mutex-guarded: it is a flag, not a value to accumulate.
std::atomic<bool> sTouchAimDown{false};
// Set by the first finger-down and kept until Reset: like MP1's direct aim, a drag leaves the
// view where it was aimed instead of handing it back to the free-look pitch (which is level).
std::atomic<bool> sTouchAimLatched{false};
std::atomic<uint64_t> sTouchHoldUntilNs{0};
float sPendingX = 0.f;
float sPendingY = 0.f;
float sFrameX = 0.f;
float sFrameY = 0.f;
// "Gyro also turns the camera" (MP1's sGyroCameraPending): view travel in mouse pixels beside the
// pointer's, pending and latched with it.
float sCameraPendingX = 0.f;
float sCameraPendingY = 0.f;
float sCameraFrameX = 0.f;
float sCameraFrameY = 0.f;
// The stick's part of the latched frame: under pointer aim with the gyro driving it turns the
// camera instead of moving the pointer (MP1's sStickFrame).
float sStickFrameX = 0.f;
float sStickFrameY = 0.f;
bool sGyroDrivesAim = false;
// Reset aim (PollPortActions): 0 none, 1 the usual, 2 both pointer and camera.
std::atomic<int> sResetAimRequest{0};
// The stick's part of sPendingX/Y (PresentedAimDelta shows only a tick's fraction of it), and
// whether the last tick applied the aim, with the dt it was given.
float sStickPendingX = 0.f;
float sStickPendingY = 0.f;
bool sAppliedLastTick = false;
float sLastTickDt = 1.f / 60.f;
// Pointer aim (MP1's sPointerAim/sPointerAimFree), configured each tick by CPlayer.
PortMouse::PointerAim sPointer;
// The last update left the pointer free under a lock (lock-on free aim).
bool sLockFree = false;
bool sPointerEnabled = false;
bool sPointerFree = false;
// The console's `pointeraim <x> <y>` holds the pointer there (free, the view not turned) until
// `pointeraim 0 0`, so a harness can aim it without a mouse.
bool sDebugHold = false;
float sPointerBox = 0.7f;
float sPointerTurnRate = PortMouse::kPi;

// Pending travel is written by the UI thread (AddTouchAim) and latched by the game thread
// (BeginFrame, and the clears), so every read or write of it takes this lock. The mouse's
// AddMotion and the stick's AddStickAim run on the game thread itself, which is also the only
// thread that latches, so those two need no lock of their own. The clear lives here rather
// than at each call site so that SetCapture - which Reset calls - takes the lock on its own.
std::mutex sPendingMutex;

constexpr float kTouchSpringHoldSeconds = 0.35f;

void ClearPendingTravel() {
  std::lock_guard< std::mutex > lock(sPendingMutex);
  sPendingX = sPendingY = sFrameX = sFrameY = 0.f;
  sStickPendingX = sStickPendingY = sStickFrameX = sStickFrameY = 0.f;
  sCameraPendingX = sCameraPendingY = sCameraFrameX = sCameraFrameY = 0.f;
}

float ClampTouchAimSpeed(float pixelsPerDp) {
  return pixelsPerDp < 0.25f ? 0.25f : pixelsPerDp > 10.f ? 10.f : pixelsPerDp;
}

bool EnvFlag(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
}

// A flag that falls back to the value the settings file loaded: unset means "the file's", and
// anything set wins, including a literal 0.
bool EnvFlagOr(const char* name, bool fallback) {
  return std::getenv(name) != nullptr ? EnvFlag(name) : fallback;
}

void ReadEnv() {
  if (sReadEnv) {
    return;
  }
  sReadEnv = true;
  // The settings file first, then the MP2_* environment on top, so a file value is the default
  // and an environment variable still wins for one run. The file's keys reach these variables
  // through the setters below; they see sReadEnv already set, so nothing recurses.
  PortOverlaySettings::LoadSettings();
  sEnabled = EnvFlagOr("MP2_MOUSE_AIM", sEnabled);
  sStickEnabled = EnvFlagOr("MP2_TWIN_STICK", sStickEnabled);
  sInvertX = EnvFlagOr("MP2_MOUSE_INVERT_X", sInvertX);
  sInvertY = EnvFlagOr("MP2_MOUSE_INVERT_Y", sInvertY);
  sStickAimSplit = EnvFlagOr("MP2_STICK_AIM_SPLIT", sStickAimSplit);
  if (const char* rate = std::getenv("MP2_STICK_AIM_RATE")) {
    const float value = static_cast< float >(std::atof(rate));
    if (std::isfinite(value) && value > 0.f) {
      sStickAimRate = value;
    }
  }
  if (const char* rate = std::getenv("MP2_STICK_AIM_RATE_Y")) {
    const float value = static_cast< float >(std::atof(rate));
    if (std::isfinite(value) && value > 0.f) {
      sStickAimRateY = value;
    }
  }
  if (const char* sens = std::getenv("MP2_MOUSE_SENS")) {
    const float value = static_cast< float >(std::atof(sens));
    if (std::isfinite(value) && value > 0.f) {
      sSensitivity = value;
    }
  }
  sSensitivitySplit = EnvFlagOr("MP2_MOUSE_SENS_SPLIT", sSensitivitySplit);
  if (const char* sens = std::getenv("MP2_MOUSE_SENS_Y")) {
    const float value = static_cast< float >(std::atof(sens));
    if (std::isfinite(value) && value > 0.f) {
      sSensitivityY = value;
    }
  }
  // No MP2_TOUCH_AIM flag here: `PortTouch` owns that switch (its AimEnabled) and turns this
  // source on through SetTouchEnabled, so there is one answer to "is the layout aiming by
  // drag".
  if (const char* speed = std::getenv("MP2_TOUCH_AIM_SPEED")) {
    const float value = static_cast< float >(std::atof(speed));
    if (std::isfinite(value)) {
      sTouchAimSpeed = ClampTouchAimSpeed(value);
    }
  }
}

// Aurora owns the only window, so the first one SDL knows about is the game's (the same
// approach the Prime port's debug overlay uses).
SDL_Window* GameWindow() {
  static SDL_Window* sWindow = nullptr;
  if (sWindow == nullptr) {
    int count = 0;
    if (SDL_Window** windows = SDL_GetWindows(&count)) {
      for (int i = 0; i < count; ++i) {
        if ((SDL_GetWindowFlags(windows[i]) & SDL_WINDOW_INPUT_FOCUS) != 0) {
          sWindow = windows[i];
          break;
        }
      }
      if (sWindow == nullptr && count > 0) {
        sWindow = windows[0];
      }
    }
  }
  return sWindow;
}

bool sPhotoOwnsCapture = false;

// Relative capture is what makes the motion relative and keeps the desktop cursor out of the
// game; leaving it off outside gameplay keeps the cursor usable for menus and the desktop.
void ApplyCapture(bool captured) {
  if (captured == sCaptured) {
    return;
  }
  sCaptured = captured;
  // One line per change, so a run's log says whether the mouse ever took the view.
  std::fprintf(stderr, "[mouse] %s\n", captured ? "captured: aim owns the view" : "released");
  if (SDL_Window* window = GameWindow()) {
    SDL_SetWindowRelativeMouseMode(window, captured);
  }
  if (captured) {
    SDL_HideCursor();
  } else {
    SDL_ShowCursor();
    ClearPendingTravel();
  }
}

// Everything but photo mode (UpdateAim, SetUiActive, Reset, which a disabled touch source runs
// every frame) is shut out while photo mode owns the grab.
void SetCapture(bool captured) {
  if (!sPhotoOwnsCapture) {
    ApplyCapture(captured);
  }
}

// MP1's PointerAimScale: aim travel (pixels at the reference sensitivity) to the pointer's
// -1..1 range, so a mouse crosses the screen in one window's width of travel.
void PointerAimScale(float& sx, float& sy) {
  int width = 0, height = 0;
  SDL_Window* window = GameWindow();
  if (window == nullptr || !SDL_GetWindowSize(window, &width, &height) || width <= 0 ||
      height <= 0) {
    width = 640;
    height = 480;
  }
  sx = PortMouse::PointerUnitsPerCount(static_cast< float >(width), sSensitivity);
  sy = PortMouse::PointerUnitsPerCount(static_cast< float >(height), sSensitivity);
}

} // namespace

namespace PortMouse {

bool Enabled() {
  ReadEnv();
  return sEnabled;
}

void SetEnabled(bool enabled) {
  ReadEnv();
  sEnabled = enabled;
  if (!enabled && !sStickEnabled && !sTouchEnabled.load()) {
    Reset();
  }
}

bool StickEnabled() {
  ReadEnv();
  return sStickEnabled;
}

bool StickOwnsCStick() {
  ReadEnv();
  return sStickEnabled && !(sEnabled && sKeyboardCStickHeld);
}

void SetKeyboardCStickHeld(bool held) { sKeyboardCStickHeld = held; }

void SetStickEnabled(bool enabled) {
  ReadEnv();
  sStickEnabled = enabled;
  if (!enabled && !sEnabled && !sTouchEnabled.load()) {
    Reset();
  }
}

float StickAimRate() {
  ReadEnv();
  return sStickAimRate;
}

void SetStickAimRate(float pixelsPerSecond) {
  ReadEnv();
  if (std::isfinite(pixelsPerSecond) && pixelsPerSecond > 0.f) {
    sStickAimRate = pixelsPerSecond;
  }
}

bool StickAimSplit() {
  ReadEnv();
  return sStickAimSplit;
}

void SetStickAimSplit(bool enabled) {
  ReadEnv();
  sStickAimSplit = enabled;
}

float StickAimRateY() {
  ReadEnv();
  return sStickAimRateY;
}

void SetStickAimRateY(float pixelsPerSecond) {
  ReadEnv();
  if (std::isfinite(pixelsPerSecond) && pixelsPerSecond > 0.f) {
    sStickAimRateY = pixelsPerSecond;
  }
}

void AddStickAim(float x, float y, float dt) {
  if (!sStickEnabled || sUiActive || dt <= 0.f || !std::isfinite(x) || !std::isfinite(y)) {
    return;
  }
  if (x * x + y * y < 0.04f) {
    return; // inside the dead zone; a resting stick must not creep the view
  }
  // Keep the stick's own pixels/second tuning independent of mouse sensitivity. Store
  // mouse-equivalent counts so pointer, gyro-camera routing and presentation can still
  // aggregate sources without rescaling the mouse, touch or gyro contributions.
  const float dx = AimPixelsAsCounts(x * sStickAimRate * dt, sSensitivity);
  // The aim's dy is screen-down positive, the stick's y is up positive.
  const float dy = AimPixelsAsCounts(-y * (sStickAimSplit ? sStickAimRateY : sStickAimRate) * dt,
                                    sSensitivity);
  // Same thread as the latch (see sPendingMutex), but PresentedAimDelta reads both under the lock.
  std::lock_guard< std::mutex > lock(sPendingMutex);
  sPendingX += dx;
  sPendingY += dy;
  sStickPendingX += dx;
  sStickPendingY += dy;
}

bool TouchEnabled() {
  ReadEnv();
  return sTouchEnabled.load();
}

void SetTouchEnabled(bool enabled) {
  ReadEnv();
  sTouchEnabled = enabled;
  // Turning the layout's drag-to-aim off is the same shape as releasing the mouse: with no
  // source left, the aim must not keep a stale yaw for the next one to snap to.
  if (!enabled && !sEnabled && !sStickEnabled) {
    Reset();
  }
}

void AddTouchAim(float dxDp, float dyDp) {
  // A disabled source must not accumulate travel: the game thread early-outs before it
  // latches, so the pixels would sit here and burst the moment the source came back.
  if (!sTouchEnabled.load() || !std::isfinite(dxDp) || !std::isfinite(dyDp)) {
    return;
  }
  // Plain drags feed deltas only (MP1's model); the deltas themselves claim the view.
  sTouchAimLatched = true;
  std::lock_guard< std::mutex > lock(sPendingMutex);
  sPendingX += dxDp * sTouchAimSpeed;
  sPendingY += dyDp * sTouchAimSpeedY;
}

void SetTouchAimDown(bool down) {
  sTouchAimDown = down;
  if (down) {
    sTouchAimLatched = true;
  }
}

void SpringSwipe() {
  sTouchHoldUntilNs = SDL_GetTicksNS() + static_cast< uint64_t >(kTouchSpringHoldSeconds * 1e9);
}

bool TouchAimActive() {
  return sTouchEnabled.load() &&
         (sTouchAimDown.load() || SDL_GetTicksNS() < sTouchHoldUntilNs.load());
}

float TouchAimSpeed() {
  ReadEnv();
  return sTouchAimSpeed;
}

void SetTouchAimSpeed(float pixelsPerDp) {
  ReadEnv();
  if (std::isfinite(pixelsPerDp)) {
    sTouchAimSpeed = ClampTouchAimSpeed(pixelsPerDp);
  }
}

float TouchAimSpeedY() {
  ReadEnv();
  return sTouchAimSpeedY;
}

void SetTouchAimSpeedY(float pixelsPerDp) {
  ReadEnv();
  if (std::isfinite(pixelsPerDp)) {
    sTouchAimSpeedY = ClampTouchAimSpeed(pixelsPerDp);
  }
}

void SetUiActive(bool active) {
  sUiActive = active;
  if (active) {
    PortRadial::Cancel();
    sAiming = false;
    SetCapture(false);
  }
}

bool UiActive() { return sUiActive; }

bool Captured() { return sCaptured; }

void UpdateIdleCursor(bool mouseInput, bool keepVisible) {
  constexpr Uint64 kIdleNs = 2'000'000'000;
  static Uint64 sLastInputNs = 0;
  static bool sIdleHidden = false;
  const Uint64 now = SDL_GetTicksNS();
  if (mouseInput || keepVisible || sUiActive || sLastInputNs == 0) {
    sLastInputNs = now;
  }
  if (sCaptured) {
    // ApplyCapture hid it, and shows it again on release; the idle state starts over from there.
    sIdleHidden = false;
    return;
  }
  const bool hide = now - sLastInputNs >= kIdleNs;
  if (hide != sIdleHidden) {
    sIdleHidden = hide;
    if (hide) {
      SDL_HideCursor();
    } else {
      SDL_ShowCursor();
    }
  }
}

void AddMotion(float dx, float dy) {
  if (!sCaptured || !std::isfinite(dx) || !std::isfinite(dy)) {
    return;
  }
  if (sSensitivitySplit) {
    // Vertical counts become the horizontal-equivalent counts that turn at the Y sensitivity.
    const float ratio = sSensitivityY / sSensitivity;
    if (std::isfinite(ratio) && ratio > 0.f) {
      dy *= ratio;
    }
  }
  sPendingX += dx;
  sPendingY += dy;
}

// Photo mode (PortPhoto) owns the grab while active: UpdateAim is not reached then, so whatever
// gameplay left grabbed is released here unless the free camera's mouse look wants it, and
// nothing else (SetCapture) can change it. EndPhotoCapture hands it back, released, so
// gameplay's next UpdateAim re-grabs normally.
void SetPhotoCapture(bool want) {
  sPhotoOwnsCapture = true;
  ApplyCapture(want);
}

void EndPhotoCapture() {
  if (sPhotoOwnsCapture) {
    sPhotoOwnsCapture = false;
    ApplyCapture(false);
  }
}

bool TakePhotoLook(float& yawDegrees, float& pitchDegrees) {
  ReadEnv();
  std::lock_guard< std::mutex > lock(sPendingMutex);
  const float dx = sFrameX;
  const float dy = sFrameY;
  sFrameX = sFrameY = 0.f;
  if (dx == 0.f && dy == 0.f) {
    return false;
  }
  constexpr float kDegrees = 57.29578f;
  yawDegrees = -dx * sSensitivity * kDegrees * (sInvertX ? -1.f : 1.f);
  pitchDegrees = -dy * sSensitivity * kDegrees * (sInvertY ? -1.f : 1.f);
  return true;
}

void AddGyroPointer(float yaw, float pitch, float dt, float sweep, float scaleY) {
  if (!std::isfinite(yaw) || !std::isfinite(pitch) || !(dt > 0.f) || !(sweep > 0.f) || !std::isfinite(scaleY)) {
    return;
  }
  // MP1's PollGyro pointer branch: `sweep` radians of pad turn cross the screen (2 pointer units)
  // at the same pixels per degree both ways, so divide out the window and mouse scaling the
  // pointer applies to pending travel. No inversion.
  float px = 0.f, py = 0.f;
  PointerAimScale(px, py);
  if (!(px > 0.f)) {
    return;
  }
  const float cursor = 2.f / sweep * dt / px;
  std::lock_guard< std::mutex > lock(sPendingMutex);
  sPendingX += yaw * cursor;
  sPendingY -= pitch * cursor * scaleY;
}

void AddGyroCamera(float yaw, float pitch, float dt, float sensitivity, float sensitivityY) {
  ReadEnv();
  if (!std::isfinite(yaw) || !std::isfinite(pitch) || !(dt > 0.f) || !(sSensitivity > 0.f)) {
    return;
  }
  // MP1's natural scale: the pending pixels become sSensitivity rad each, with the mouse inversion
  // applied, so divide that out and pre-undo the inversion: the camera turns `sensitivity` times
  // the pad's angle whatever the mouse is set to. x right / y up; the aim state expects right/down.
  const float sx = sInvertX ? -1.f : 1.f;
  const float sy = sInvertY ? -1.f : 1.f;
  const float scale = sensitivity * dt / sSensitivity;
  const float scaleY = sensitivityY * dt / sSensitivity;
  std::lock_guard< std::mutex > lock(sPendingMutex);
  sCameraPendingX += sx * yaw * scale;
  sCameraPendingY -= sy * pitch * scaleY;
}

void SetGyroDrivesAim(bool drives) { sGyroDrivesAim = drives; }

void RequestResetAim(bool both) { sResetAimRequest.store(both ? 2 : 1, std::memory_order_release); }

void BeginTick() {
  // The UI thread may be adding travel right now; latch and clear in one lock so a late
  // addition lands in this tick or the next, never in neither.
  std::lock_guard< std::mutex > lock(sPendingMutex);
  sAppliedLastTick = false;
  sFrameX = sPendingX;
  sFrameY = sPendingY;
  sStickFrameX = sStickPendingX;
  sStickFrameY = sStickPendingY;
  sPendingX = 0.f;
  sPendingY = 0.f;
  sStickPendingX = 0.f;
  sStickPendingY = 0.f;
  sCameraFrameX = sCameraPendingX;
  sCameraFrameY = sCameraPendingY;
  sCameraPendingX = 0.f;
  sCameraPendingY = 0.f;
}

bool UpdateAim(bool active, bool locked, float x, float y, float z, float dt, bool pointerLock,
                bool pointerHold) {
  ReadEnv();
  sAppliedLastTick = false;
  if (PortRadial::Capturing()) {
    if (!PortRadial::TouchOwnsSelection())
      PortRadial::Transport().AddTravel(sFrameX - sStickFrameX, sFrameY - sStickFrameY);
    sFrameX = sFrameY = sStickFrameX = sStickFrameY = 0.f;
    sCameraFrameX = sCameraFrameY = 0.f;
    sAiming = false;
    SetCapture(active && sEnabled);
    // Freeze both pointer and edge-turn; retain initialized yaw/pitch (no presentation snap).
    return sAim.Update(active, locked, x, y, z, 0.f, 0.f, sSensitivity, sInvertX, sInvertY);
  }
  if (std::isfinite(dt) && dt > 0.f) {
    sLastTickDt = dt;
  }
  if ((!sEnabled && !sStickEnabled && !sTouchEnabled.load()) || sUiActive) {
    sAiming = false;
    sPointerFree = false;
    sLockFree = false;
    sPointer.Center();
    SetCapture(false);
    return false;
  }
  // The touch source is on for a whole run but only owns the view while a finger is down (or a
  // flick's hold is live). Without that check the idle overlay would own it: CPlayer would turn
  // the body to the last drag's yaw every tick, pinning it so the C-stick and stick-free
  // movement could no longer turn the player - so idle must read as `locked` below.
  // Once a finger has aimed (or whenever mouse/stick aim is on) the aim stays the owner after
  // the lift - handing the view back snapped the pitch to the free-look level.
  const bool touchIdle = sTouchEnabled.load() && !sEnabled && !sStickEnabled &&
                         !sTouchAimLatched.load() && !TouchAimActive();
  // `locked` means something else owns the view (a lock-on, the morph ball, a cinematic): the
  // aim tracks that direction and the caller must not turn the player. `touchIdle` is the same
  // situation, so a drag that begins there does not snap the view either.
  const bool held = locked || touchIdle;
  const bool owns = active && !held;
  sAiming = owns;
  // Only mouse aim needs the pointer grabbed; twin-stick and touch leave the cursor alone.
  // The grab has to survive a lock: a target lock (and a cinematic, and the morph ball) makes
  // `locked` true, and releasing the grab then would stop the mouse buttons reaching the pad
  // - `ReadMouseButtons` only reads them while the window is in relative mode - so the L that
  // started the lock would look released on the next tick and the lock would drop again. The
  // grab is the game's, not the free look's.
  SetCapture((owns || locked) && sEnabled);
  // MP1's Reset aim: under pointer aim recentre the pointer, otherwise look level (pitch 0; yaw
  // stays); `both` does both either way.
  if (const int reset = sResetAimRequest.exchange(0, std::memory_order_acq_rel); reset != 0) {
    if (sPointerEnabled || reset == 2) {
      sPointer.Center();
    }
    if ((!sPointerEnabled || reset == 2) && sAim.initialized) {
      sAim.pitch = 0.f;
    }
  }
  // Lock-on free aim (MP1 83ff2ecba, 1881b35d4): the lock owns the view and a pointer moves over
  // it, pointer aim's or, without pointer aim, one for the lock only. It starts and ends a lock at
  // the centre, where the target is. A touch source with no finger down re-centres it.
  const bool lockFree = pointerLock && active && locked && !touchIdle;
  if (lockFree != sLockFree) {
    sPointer.Center();
    sLockFree = lockFree;
  }
  // A held pointer stays where the lock found it, and neither moves nor turns the view.
  const bool pointerHeld = (active && locked && !lockFree && pointerHold && sPointerEnabled && !touchIdle) ||
                           (sDebugHold && sPointerEnabled && !lockFree && !touchIdle);
  if (sPointerEnabled || lockFree) {
    // MP1's pointer path: the travel moves the pointer, not the view. A lock (or anything that
    // stops free look) re-centres it (except under lock-on free aim), so lock-on, scan and grapple
    // still aim through the centre.
    // With the gyro driving the pointer, the stick turns the camera as it does without pointer
    // aim (MP1 4b8117d16).
    // A lock leaves the view nothing to turn, so the stick moves the pointer too.
    const bool stickTurns = sGyroDrivesAim && !lockFree;
    const bool moves = (owns || lockFree) && !(sDebugHold && !lockFree);
    if (moves) {
      float sx = 0.f, sy = 0.f;
      PointerAimScale(sx, sy);
      if (stickTurns) {
        sPointer.Move((sFrameX - sStickFrameX) * sx, (sFrameY - sStickFrameY) * sy);
      } else {
        sPointer.Move(sFrameX * sx, sFrameY * sy);
      }
    } else if (!pointerHeld) {
      sPointer.Center();
    }
    // The view turns only by the gyro's camera travel (zero unless it also turns the camera),
    // and the stick's when it turns the camera.
    const float cameraX = sCameraFrameX + (stickTurns ? sStickFrameX : 0.f);
    const float cameraY = sCameraFrameY + (stickTurns ? sStickFrameY : 0.f);
    const bool applied = sAim.Update(active, held, x, y, z, owns ? cameraX : 0.f, owns ? cameraY : 0.f,
                                     sSensitivity, sInvertX, sInvertY);
    if (applied && owns && !sDebugHold) {
      float dyaw = 0.f, dpitch = 0.f;
      PortMouse::PointerAim::TurnAt(sPointer.x, sPointer.y, sPointerBox, sPointerTurnRate, dt,
                                    dyaw, dpitch);
      sAim.Turn(dyaw, dpitch);
    }
    sPointerFree = (applied && owns) || lockFree || pointerHeld;
    if (!sPointerFree) {
      sPointer.Center();
    }
    // The second update of a tick must not move the pointer by the same travel again.
    if (applied || lockFree) {
      sFrameX = sFrameY = sStickFrameX = sStickFrameY = 0.f;
      sCameraFrameX = sCameraFrameY = 0.f;
    }
    sAppliedLastTick = applied && owns;
    return applied && owns;
  }
  sPointerFree = false;
  sLockFree = false;
  // Without pointer aim the gyro's travel turns the view beside the mouse's, stick's and touch's.
  const bool applied = sAim.Update(active, held, x, y, z, sFrameX + sCameraFrameX, sFrameY + sCameraFrameY,
                                   sSensitivity, sInvertX, sInvertY);
  if (applied) {
    sFrameX = sFrameY = sStickFrameX = sStickFrameY = 0.f;
    sCameraFrameX = sCameraFrameY = 0.f;
  }
  sAppliedLastTick = applied && owns;
  return applied && owns;
}

bool PresentedAimDelta(float fraction, float& dyaw, float& dpitch) {
  dyaw = dpitch = 0.f;
  if (PortRadial::Capturing()) return false;
  if (!sAppliedLastTick || !sAiming || !sAim.initialized || !std::isfinite(fraction) ||
      fraction < 0.f) {
    return false;
  }
  if (fraction > 1.f) {
    fraction = 1.f;
  }
  float dx = 0.f, dy = 0.f, cx = 0.f, cy = 0.f;
  {
    std::lock_guard< std::mutex > lock(sPendingMutex);
    dx = sPendingX - sStickPendingX * (1.f - fraction);
    dy = sPendingY - sStickPendingY * (1.f - fraction);
    cx = sCameraPendingX;
    cy = sCameraPendingY;
  }
  // The aim the next tick would reach, through the same Update/Turn as UpdateAim.
  PortMouse::AimState next = sAim;
  if (sPointerEnabled) {
    if (!sPointerFree) {
      return false;
    }
    float sx = 0.f, sy = 0.f;
    PointerAimScale(sx, sy);
    PortMouse::PointerAim pointer = sPointer;
    if (sGyroDrivesAim) {
      // The stick turns the camera (UpdateAim's stickTurns): only its tick fraction shows.
      const float stickX = dx - (sPendingX - sStickPendingX);
      const float stickY = dy - (sPendingY - sStickPendingY);
      pointer.Move((dx - stickX) * sx, (dy - stickY) * sy);
      cx += stickX;
      cy += stickY;
    } else {
      pointer.Move(dx * sx, dy * sy);
    }
    next.Update(true, false, 0.f, 0.f, 0.f, cx, cy, sSensitivity, sInvertX, sInvertY);
    float turnYaw = 0.f, turnPitch = 0.f;
    PortMouse::PointerAim::TurnAt(pointer.x, pointer.y, sPointerBox, sPointerTurnRate,
                                  fraction * sLastTickDt, turnYaw, turnPitch);
    next.Turn(turnYaw, turnPitch);
  } else {
    next.Update(true, false, 0.f, 0.f, 0.f, dx + cx, dy + cy, sSensitivity, sInvertX, sInvertY);
  }
  dyaw = static_cast< float >(std::remainder(double(next.yaw) - sAim.yaw, 2.0 * PortMouse::kPi));
  dpitch = next.pitch - sAim.pitch;
  return std::isfinite(dyaw) && std::isfinite(dpitch);
}

void SetPointerAim(bool enabled, float box, float turnRate) {
  if (enabled != sPointerEnabled) {
    // MP1's SetPointerAim: switching either way starts from the centre.
    sPointer.Center();
    sPointerFree = false;
    sPointerEnabled = enabled;
  }
  if (std::isfinite(box)) {
    sPointerBox = box < 0.f ? 0.f : box > 0.99f ? 0.99f : box;
  }
  if (std::isfinite(turnRate) && turnRate >= 0.f) {
    sPointerTurnRate = turnRate;
  }
}

bool PointerAimEnabled() { return sPointerEnabled; }

bool LockFreeAimActive() { return sLockFree; }

bool PointerAimPosition(float& x, float& y) {
  x = sPointer.x;
  y = sPointer.y;
  return sPointerFree;
}

void SetPointerAimPosition(float x, float y) {
  sPointer.Center();
  sPointer.Move(x, y);
}

void SetPointerAimHold(bool hold) { sDebugHold = hold; }

void Synchronize(float x, float y, float z) {
  if (Enabled()) {
    sAim.Synchronize(x, y, z);
  }
}

void SetAimAngles(float yawDeg, float pitchDeg) {
  ReadEnv();
  if (!std::isfinite(yawDeg) || !std::isfinite(pitchDeg)) {
    return;
  }
  // The camera builds its direction as (sin -yaw, cos -yaw, sin pitch) and CPlayer turns the
  // body by +yaw, so the world heading `face` names is -yaw here.
  const double radians = -double(yawDeg) * (PortMouse::kPi / 180.f);
  sAim.yaw = static_cast< float >(std::remainder(radians, 2.0 * PortMouse::kPi));
  const double pitch = double(pitchDeg) * (PortMouse::kPi / 180.f);
  const double limit = double(kMaxPitch);
  sAim.pitch = static_cast< float >(pitch < -limit ? -limit : pitch > limit ? limit : pitch);
  sAim.initialized = true;
  // CFirstPersonCamera only consumes the aim while a direct source is on; mouse aim is the one
  // with no input of its own here, so a console run cannot turn it by accident.
  SetEnabled(true);
}

void Reset() {
  sAim.Reset();
  sAiming = false;
  ClearPendingTravel();
  sPointer.Center();
  sPointerFree = false;
  // A finger's state belongs to the aim being reset: a held spring must not re-open the view
  // the next frame, and a later down must not inherit a stale hold.
  sTouchAimDown = false;
  sTouchAimLatched = false;
  sTouchHoldUntilNs = 0;
  SetCapture(false);
}

bool Aiming() { return sAiming; }
bool Initialized() { return sAim.initialized; }
float Yaw() { return sAim.yaw; }
float Pitch() { return sAim.pitch; }
float Sensitivity() {
  ReadEnv();
  return sSensitivity;
}

void SetSensitivity(float radiansPerPixel) {
  ReadEnv();
  if (std::isfinite(radiansPerPixel) && radiansPerPixel > 0.f) {
    sSensitivity = radiansPerPixel;
  }
}

bool SensitivitySplit() {
  ReadEnv();
  return sSensitivitySplit;
}

void SetSensitivitySplit(bool enabled) {
  ReadEnv();
  sSensitivitySplit = enabled;
}

float SensitivityY() {
  ReadEnv();
  return sSensitivityY;
}

void SetSensitivityY(float radiansPerPixel) {
  ReadEnv();
  if (std::isfinite(radiansPerPixel) && radiansPerPixel > 0.f) {
    sSensitivityY = radiansPerPixel;
  }
}

int MouseDpi() {
  ReadEnv();
  return sMouseDpi;
}

void SetMouseDpi(int dpi) {
  ReadEnv();
  sMouseDpi = dpi < 100 ? 100 : dpi > 64000 ? 64000 : dpi;
}

bool InvertX() {
  ReadEnv();
  return sInvertX;
}

bool InvertY() {
  ReadEnv();
  return sInvertY;
}

void SetInvertX(bool invert) {
  ReadEnv();
  sInvertX = invert;
}

void SetInvertY(bool invert) {
  ReadEnv();
  sInvertY = invert;
}

} // namespace PortMouse
