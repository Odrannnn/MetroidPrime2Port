// Mouse look: SDL relative motion feeds the aim state in `port_mouse.h`, which the game reads
// from CPlayer (body yaw) and CFirstPersonCamera (view direction). The Prime port drives the
// same state from its own hooks; this is the part that only needs SDL, so it lives in the
// platform layer and is built by `mp_port_audio`, the target with SDL on its include path.
//
// `MP2_MOUSE_AIM=1` turns it on. Off by default: a pad, or the keyboard map in
// `port_events.cpp`, is the retail-shaped way to play, and mouse aim changes movement and
// turning, so it stays opt-in until it has had more play time.

#include "port_mouse.h"
#include "port_flick_stick.h"
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

bool sEnabled = false;
bool sStickEnabled = false;
bool sKeyboardCStickHeld = false;
// The Java UI thread can flip this through a layout change while the game thread reads it in
// UpdateAim, so it is atomic unlike the mouse's and the stick's flags.
std::atomic<bool> sTouchEnabled{false};
bool sReadEnv = false;
bool sCaptured = false;
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
// Flick stick: the controller's switch, the touch twin-stick layout's own, and whether that layout
// is the active input (PortTouch, UI thread); the outer deadzone in percent.
bool sFlickStick = false;
bool sTouchFlickStick = false;
std::atomic<bool> sTouchTwinStickActive{false};
float sFlickOuterDeadZone = 10.f;
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

// Pending travel is written by the UI thread (AddTouchAim) and latched by the game thread
// (BeginFrame, and the clears), so every read or write of it takes this lock. The mouse's
// AddMotion and the stick's AddStickAim run on the game thread itself, which is also the only
// thread that latches, so those two need no lock of their own. The clear lives here rather
// than at each call site so that SetCapture - which Reset calls - takes the lock on its own.
std::mutex sPendingMutex;

constexpr float kTouchSpringHoldSeconds = 0.35f;

// One player's aim. In a match the player index is the pad port, so each pad's right stick feeds
// its own slot, and so does each pad's gyro; the mouse, touch and the phone's gyro feed slot 0
// only. Settings (sensitivity, rates, the source switches) stay global.
struct Slot {
  PortMouse::AimState aim;
  bool aiming = false;
  float pendingX = 0.f;
  float pendingY = 0.f;
  float frameX = 0.f;
  float frameY = 0.f;
  // "Gyro also turns the camera" (MP1's sGyroCameraPending): view travel in mouse pixels beside the
  // pointer's, pending and latched with it.
  float cameraPendingX = 0.f;
  float cameraPendingY = 0.f;
  float cameraFrameX = 0.f;
  float cameraFrameY = 0.f;
  // The stick's part of the latched frame: under pointer aim with the gyro driving it turns the
  // camera instead of moving the pointer (MP1's sStickFrame).
  float stickFrameX = 0.f;
  float stickFrameY = 0.f;
  bool gyroDrivesAim = false;
  // Reset aim (PollPortActions): 0 none, 1 the usual, 2 both pointer and camera.
  std::atomic< int > resetAimRequest{0};
  // The stick's part of pendingX/Y (PresentedAimDelta shows only a tick's fraction of it), and
  // whether the last tick applied the aim, with the dt it was given.
  float stickPendingX = 0.f;
  float stickPendingY = 0.f;
  bool appliedLastTick = false;
  float lastTickDt = 1.f / 60.f;
  // Pointer aim (MP1's sPointerAim/sPointerAimFree), configured each tick by CPlayer.
  PortMouse::PointerAim pointer;
  // The last update left the pointer free under a lock (lock-on free aim).
  bool lockFree = false;
  // That lock had no target (UpdateAim's pointerStays): its end leaves the pointer put.
  bool lockStays = false;
  bool pointerEnabled = false;
  bool pointerFree = false;
  // The console's `pointeraim <x> <y>` holds the pointer there (free, the view not turned) until
  // `pointeraim 0 0`, so a harness can aim it without a mouse.
  bool debugHold = false;
  float pointerBox = 0.7f;
  float pointerTurnRate = PortMouse::kPi;
  // Flick stick's state, and whether it was on last tick (on from the start: a pad that comes
  // tilted flicks on its first read). The engine's round-dead-zone right stick for this port.
  PortFlickStick::State flick;
  bool flickOn = true;
  float flickRawX = 0.f;
  float flickRawY = 0.f;
};

constexpr int kSlots = 4;
Slot sSlots[kSlots];

Slot& SlotFor(int player) { return sSlots[player < 0 || player >= kSlots ? 0 : player]; }

// A match's other players (slots 1-3) may override Player 1's twin-stick settings field by
// field; an unset field follows Player 1, so setups from before per-player settings keep working.
template <typename T>
struct PlayerValue {
  T value{};
  bool set = false;
};
struct PlayerStick {
  PlayerValue<bool> enabled;
  PlayerValue<float> rate;
  PlayerValue<bool> split;
  PlayerValue<float> rateY;
  PlayerValue<bool> flick;
};
PlayerStick sPlayerStick[kSlots]; // [0] unused: Player 1 is the global settings

template <typename T>
T ForPlayer(int player, PlayerValue<T> PlayerStick::*field, T p1) {
  if (player <= 0 || player >= kSlots) return p1;
  const PlayerValue<T>& v = sPlayerStick[player].*field;
  return v.set ? v.value : p1;
}

// Slot 0 follows every source; the others only a pad's right stick, and only with twin stick on.
bool SlotLive(int player, bool anySourceOn) {
  return player == 0 ? anySourceOn : PortMouse::StickEnabled(player);
}

void ClearPendingTravel(Slot& s) {
  std::lock_guard< std::mutex > lock(sPendingMutex);
  s.pendingX = s.pendingY = s.frameX = s.frameY = 0.f;
  s.stickPendingX = s.stickPendingY = s.stickFrameX = s.stickFrameY = 0.f;
  s.cameraPendingX = s.cameraPendingY = s.cameraFrameX = s.cameraFrameY = 0.f;
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
  sFlickStick = EnvFlagOr("MP2_FLICK_STICK", sFlickStick);
  sTouchFlickStick = EnvFlagOr("MP2_TOUCH_FLICK_STICK", sTouchFlickStick);
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
    ClearPendingTravel(sSlots[0]);
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

static void ResetSlot(Slot& s) {
  s.aim.Reset();
  s.aiming = false;
  ClearPendingTravel(s);
  s.pointer.Center();
  s.pointerFree = false;
}

// `keepOwnSticks`: Player 1's sources going off leaves alone a player whose own twin stick is on.
static void ResetSlots(bool keepOwnSticks) {
  for (int i = 0; i < kSlots; ++i) {
    if (!keepOwnSticks || i == 0 || !StickEnabled(i)) {
      ResetSlot(sSlots[i]);
    }
  }
  // A finger's state belongs to the aim being reset: a held spring must not re-open the view
  // the next frame, and a later down must not inherit a stale hold.
  sTouchAimDown = false;
  sTouchAimLatched = false;
  sTouchHoldUntilNs = 0;
  SetCapture(false);
}

bool Enabled() {
  ReadEnv();
  return sEnabled;
}

void SetEnabled(bool enabled) {
  ReadEnv();
  sEnabled = enabled;
  if (!enabled && !sStickEnabled && !sTouchEnabled.load()) {
    ResetSlots(true);
  }
}

bool StickEnabled(int player) {
  ReadEnv();
  return ForPlayer(player, &PlayerStick::enabled, sStickEnabled);
}

bool DirectAim(int player) {
  ReadEnv();
  // Mouse and touch are Player 1's; the others aim directly only with their own twin stick.
  return player <= 0 ? sEnabled || sStickEnabled || sTouchEnabled.load() : StickEnabled(player);
}

bool StickOwnsCStick(int player) {
  ReadEnv();
  // The keyboard's C-stick is player 0's.
  return StickEnabled(player) && !(player == 0 && sEnabled && sKeyboardCStickHeld);
}

void SetKeyboardCStickHeld(bool held) { sKeyboardCStickHeld = held; }

void SetStickEnabled(bool enabled) {
  ReadEnv();
  sStickEnabled = enabled;
  if (!enabled && !sEnabled && !sTouchEnabled.load()) {
    ResetSlots(true);
  }
}

float StickAimRate(int player) {
  ReadEnv();
  return ForPlayer(player, &PlayerStick::rate, sStickAimRate);
}

void SetStickAimRate(float pixelsPerSecond) {
  ReadEnv();
  if (std::isfinite(pixelsPerSecond) && pixelsPerSecond > 0.f) {
    sStickAimRate = pixelsPerSecond;
  }
}

bool StickAimSplit(int player) {
  ReadEnv();
  return ForPlayer(player, &PlayerStick::split, sStickAimSplit);
}

void SetStickAimSplit(bool enabled) {
  ReadEnv();
  sStickAimSplit = enabled;
}

float StickAimRateY(int player) {
  ReadEnv();
  return ForPlayer(player, &PlayerStick::rateY, sStickAimRateY);
}

void SetStickAimRateY(float pixelsPerSecond) {
  ReadEnv();
  if (std::isfinite(pixelsPerSecond) && pixelsPerSecond > 0.f) {
    sStickAimRateY = pixelsPerSecond;
  }
}

bool PlayerStickSet(int player, StickKey key) {
  if (player <= 0 || player >= kSlots) return false;
  const PlayerStick& p = sPlayerStick[player];
  switch (key) {
  case kStick_Enabled: return p.enabled.set;
  case kStick_Rate: return p.rate.set;
  case kStick_Split: return p.split.set;
  case kStick_RateY: return p.rateY.set;
  case kStick_Flick: return p.flick.set;
  }
  return false;
}

void ClearPlayerStick(int player) {
  if (player <= 0 || player >= kSlots) return;
  sPlayerStick[player] = PlayerStick{};
}

void ClearPlayerStick(int player, StickKey key) {
  if (player <= 0 || player >= kSlots) return;
  PlayerStick& p = sPlayerStick[player];
  switch (key) {
  case kStick_Enabled: p.enabled = {}; break;
  case kStick_Rate: p.rate = {}; break;
  case kStick_Split: p.split = {}; break;
  case kStick_RateY: p.rateY = {}; break;
  case kStick_Flick: p.flick = {}; break;
  }
}

void SetPlayerStickEnabled(int player, bool enabled) {
  if (player <= 0) {
    SetStickEnabled(enabled);
  } else if (player < kSlots) {
    sPlayerStick[player].enabled = {enabled, true};
  }
}

void SetPlayerStickAimRate(int player, float pixelsPerSecond) {
  if (player <= 0) {
    SetStickAimRate(pixelsPerSecond);
  } else if (player < kSlots && std::isfinite(pixelsPerSecond) && pixelsPerSecond > 0.f) {
    sPlayerStick[player].rate = {pixelsPerSecond, true};
  }
}

void SetPlayerStickAimSplit(int player, bool enabled) {
  if (player <= 0) {
    SetStickAimSplit(enabled);
  } else if (player < kSlots) {
    sPlayerStick[player].split = {enabled, true};
  }
}

void SetPlayerStickAimRateY(int player, float pixelsPerSecond) {
  if (player <= 0) {
    SetStickAimRateY(pixelsPerSecond);
  } else if (player < kSlots && std::isfinite(pixelsPerSecond) && pixelsPerSecond > 0.f) {
    sPlayerStick[player].rateY = {pixelsPerSecond, true};
  }
}

void SetPlayerFlickStick(int player, bool enabled) {
  if (player <= 0) {
    SetFlickStick(enabled);
  } else if (player < kSlots) {
    sPlayerStick[player].flick = {enabled, true};
  }
}

bool FlickStickSetting(int player) {
  ReadEnv();
  return ForPlayer(player, &PlayerStick::flick, sFlickStick);
}

bool FlickStick(int player) {
  if (!StickEnabled(player)) return false;
  // Touch keeps its own switch, as it keeps its own twin-stick layout (Player 1's only).
  if (player <= 0 && sTouchTwinStickActive.load(std::memory_order_relaxed)) return sTouchFlickStick;
  return FlickStickSetting(player);
}

void SetFlickStick(bool enabled) {
  ReadEnv();
  sFlickStick = enabled;
}

bool TouchFlickStick() {
  ReadEnv();
  return sTouchFlickStick;
}

void SetTouchFlickStick(bool enabled) {
  ReadEnv();
  sTouchFlickStick = enabled;
}

void SetTouchTwinStickActive(bool active) { sTouchTwinStickActive.store(active, std::memory_order_relaxed); }

float FlickOuterDeadZone() {
  ReadEnv();
  return sFlickOuterDeadZone;
}

void SetFlickOuterDeadZone(float percent) {
  ReadEnv();
  if (std::isfinite(percent)) sFlickOuterDeadZone = std::fmin(std::fmax(percent, 0.f), kFlickOuterDeadZoneMax);
}

void SetFlickRawStick(int port, float x, float y) {
  if (port < 0 || port >= kSlots || !std::isfinite(x) || !std::isfinite(y)) return;
  Slot& s = sSlots[port];
  s.flickRawX = x;
  s.flickRawY = y;
}

void AddStickAim(float x, float y, float dt, int player) {
  if (!StickEnabled(player) || dt <= 0.f || !std::isfinite(x) || !std::isfinite(y)) {
    return;
  }
  Slot& s = SlotFor(player);
  // Player 1's radial menu takes the right stick from every player.
  if (PortRadial::Capturing()) {
    s.flickOn = false;
    return;
  }
  // MP1's FlickStickTurn. Under pointer aim the stick moves the pointer unless the gyro drives
  // the aim; a lock with free aim takes the stick's travel as it is.
  const bool flickOn = FlickStick(player) && !sUiActive && !s.lockFree && (!s.pointerEnabled || s.gyroDrivesAim);
  // Off and back on, a stick still tilted must be let go before it flicks.
  if (flickOn != s.flickOn) s.flick.Reset();
  s.flickOn = flickOn;
  if (flickOn) {
    // It turns by the stick's angle, which PADClamp's dead zone per axis pulls onto the nearest
    // axis (a snap every 90 degrees while circling): the engine's stick from before it.
    if (x != 0.f || y != 0.f) {
      x = s.flickRawX;
      y = s.flickRawY;
    }
    const float yaw = s.flick.Update(x, y, dt);
    if (yaw == 0.f || !(sSensitivity > 0.f)) return;
    // An exact angle: the aim turns sSensitivity rad per count with the mouse's Invert X, so
    // divide that out and pre-undo the inversion.
    const float dx = yaw / sSensitivity * (sInvertX ? -1.f : 1.f);
    std::lock_guard< std::mutex > lock(sPendingMutex);
    s.pendingX += dx;
    s.stickPendingX += dx;
    return;
  }
  if (sUiActive) {
    return;
  }
  if (x * x + y * y < 0.04f) {
    return; // inside the dead zone; a resting stick must not creep the view
  }
  // Keep the stick's own pixels/second tuning independent of mouse sensitivity. Store
  // mouse-equivalent counts so pointer, gyro-camera routing and presentation can still
  // aggregate sources without rescaling the mouse, touch or gyro contributions.
  const float rate = StickAimRate(player);
  const float dx = AimPixelsAsCounts(x * rate * dt, sSensitivity);
  // The aim's dy is screen-down positive, the stick's y is up positive.
  const float dy = AimPixelsAsCounts(-y * (StickAimSplit(player) ? StickAimRateY(player) : rate) * dt,
                                    sSensitivity);
  // Same thread as the latch (see sPendingMutex), but PresentedAimDelta reads both under the lock.
  std::lock_guard< std::mutex > lock(sPendingMutex);
  s.pendingX += dx;
  s.pendingY += dy;
  s.stickPendingX += dx;
  s.stickPendingY += dy;
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
    ResetSlots(true);
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
  Slot& s = sSlots[0];
  std::lock_guard< std::mutex > lock(sPendingMutex);
  s.pendingX += dxDp * sTouchAimSpeed;
  s.pendingY += dyDp * sTouchAimSpeedY;
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
    sSlots[0].aiming = false;
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
  sSlots[0].pendingX += dx;
  sSlots[0].pendingY += dy;
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
  Slot& s = sSlots[0];
  std::lock_guard< std::mutex > lock(sPendingMutex);
  const float dx = s.frameX;
  const float dy = s.frameY;
  s.frameX = s.frameY = 0.f;
  if (dx == 0.f && dy == 0.f) {
    return false;
  }
  constexpr float kDegrees = 57.29578f;
  yawDegrees = -dx * sSensitivity * kDegrees * (sInvertX ? -1.f : 1.f);
  pitchDegrees = -dy * sSensitivity * kDegrees * (sInvertY ? -1.f : 1.f);
  return true;
}

bool TakeBallLook(float& yaw, float& pitch, bool stickPitch, int player) {
  ReadEnv();
  Slot& s = SlotFor(player);
  std::lock_guard< std::mutex > lock(sPendingMutex);
  const float dx = s.gyroDrivesAim ? s.stickFrameX : s.frameX;
  const float dy =
      (s.gyroDrivesAim ? s.stickFrameY : s.frameY) - (stickPitch ? 0.f : s.stickFrameY);
  s.frameX = s.frameY = s.stickFrameX = s.stickFrameY = 0.f;
  if (sUiActive || (dx == 0.f && dy == 0.f) || !std::isfinite(dx) || !std::isfinite(dy)) {
    return false;
  }
  yaw = -dx * sSensitivity * (sInvertX ? -1.f : 1.f);
  pitch = -dy * sSensitivity * (sInvertY ? -1.f : 1.f);
  return true;
}

void AddGyroPointer(float yaw, float pitch, float dt, float sweep, float scaleY, int player) {
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
  Slot& s = SlotFor(player);
  std::lock_guard< std::mutex > lock(sPendingMutex);
  s.pendingX += yaw * cursor;
  s.pendingY -= pitch * cursor * scaleY;
}

void AddGyroCamera(float yaw, float pitch, float dt, float sensitivity, float sensitivityY, int player) {
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
  Slot& s = SlotFor(player);
  std::lock_guard< std::mutex > lock(sPendingMutex);
  s.cameraPendingX += sx * yaw * scale;
  s.cameraPendingY -= sy * pitch * scaleY;
}

void SetGyroDrivesAim(bool drives, int player) { SlotFor(player).gyroDrivesAim = drives; }

void RequestResetAim(bool both, int player) {
  SlotFor(player).resetAimRequest.store(both ? 2 : 1, std::memory_order_release);
}

void BeginTick() {
  // The UI thread may be adding travel right now; latch and clear in one lock so a late
  // addition lands in this tick or the next, never in neither.
  std::lock_guard< std::mutex > lock(sPendingMutex);
  for (Slot& s : sSlots) {
    s.appliedLastTick = false;
    s.frameX = s.pendingX;
    s.frameY = s.pendingY;
    s.stickFrameX = s.stickPendingX;
    s.stickFrameY = s.stickPendingY;
    s.pendingX = 0.f;
    s.pendingY = 0.f;
    s.stickPendingX = 0.f;
    s.stickPendingY = 0.f;
    s.cameraFrameX = s.cameraPendingX;
    s.cameraFrameY = s.cameraPendingY;
    s.cameraPendingX = 0.f;
    s.cameraPendingY = 0.f;
  }
}

bool UpdateAim(bool active, bool locked, float x, float y, float z, float dt, bool pointerLock,
                bool pointerHold, bool pointerStays, int player) {
  ReadEnv();
  Slot& s = SlotFor(player);
  const bool first = &s == &sSlots[0];
  s.appliedLastTick = false;
  // The radial menu is player 0's; it takes the mouse and stick travel while it is open.
  if (first && PortRadial::Capturing()) {
    if (!PortRadial::TouchOwnsSelection())
      PortRadial::Transport().AddTravel(s.frameX - s.stickFrameX, s.frameY - s.stickFrameY);
    s.frameX = s.frameY = s.stickFrameX = s.stickFrameY = 0.f;
    s.cameraFrameX = s.cameraFrameY = 0.f;
    s.aiming = false;
    SetCapture(active && sEnabled);
    // Freeze both pointer and edge-turn; retain initialized yaw/pitch (no presentation snap).
    return s.aim.Update(active, locked, x, y, z, 0.f, 0.f, sSensitivity, sInvertX, sInvertY);
  }
  if (std::isfinite(dt) && dt > 0.f) {
    s.lastTickDt = dt;
  }
  const bool live = SlotLive(first ? 0 : player, sEnabled || sStickEnabled || sTouchEnabled.load());
  if (!live || sUiActive) {
    // Another player's slot drops its aim with its twin stick, so turning it back on starts
    // from the view rather than a stale yaw.
    if (!first && !live) {
      s.aim.Reset();
    }
    s.aiming = false;
    s.pointerFree = false;
    s.lockFree = false;
    s.lockStays = false;
    s.pointer.Center();
    if (first) {
      SetCapture(false);
    }
    return false;
  }
  // The touch source is on for a whole run but only owns the view while a finger is down (or a
  // flick's hold is live). Without that check the idle overlay would own it: CPlayer would turn
  // the body to the last drag's yaw every tick, pinning it so the C-stick and stick-free
  // movement could no longer turn the player - so idle must read as `locked` below.
  // Once a finger has aimed (or whenever mouse/stick aim is on) the aim stays the owner after
  // the lift - handing the view back snapped the pitch to the free-look level.
  const bool touchIdle = first && sTouchEnabled.load() && !sEnabled && !sStickEnabled &&
                         !sTouchAimLatched.load() && !TouchAimActive();
  // `locked` means something else owns the view (a lock-on, the morph ball, a cinematic): the
  // aim tracks that direction and the caller must not turn the player. `touchIdle` is the same
  // situation, so a drag that begins there does not snap the view either.
  const bool held = locked || touchIdle;
  const bool owns = active && !held;
  s.aiming = owns;
  // Only mouse aim needs the pointer grabbed; twin-stick and touch leave the cursor alone.
  // The grab has to survive a lock: a target lock (and a cinematic, and the morph ball) makes
  // `locked` true, and releasing the grab then would stop the mouse buttons reaching the pad
  // - `ReadMouseButtons` only reads them while the window is in relative mode - so the L that
  // started the lock would look released on the next tick and the lock would drop again. The
  // grab is the game's, not the free look's.
  if (first) {
    SetCapture((owns || locked) && sEnabled);
  }
  // MP1's Reset aim: under pointer aim recentre the pointer, otherwise look level (pitch 0; yaw
  // stays); `both` does both either way.
  if (const int reset = s.resetAimRequest.exchange(0, std::memory_order_acq_rel); reset != 0) {
    if (s.pointerEnabled || reset == 2) {
      s.pointer.Center();
    }
    if ((!s.pointerEnabled || reset == 2) && s.aim.initialized) {
      s.aim.pitch = 0.f;
    }
  }
  // Lock-on free aim (MP1 83ff2ecba, 1881b35d4): the lock owns the view and a pointer moves over
  // it, pointer aim's or, without pointer aim, one for the lock only. It starts and ends a lock at
  // the centre, where the target is. A touch source with no finger down re-centres it.
  const bool lockFree = pointerLock && active && locked && !touchIdle;
  if (lockFree != s.lockFree) {
    if (!(lockFree ? pointerStays : s.lockStays)) {
      s.pointer.Center();
    }
    s.lockFree = lockFree;
  }
  s.lockStays = lockFree && pointerStays;
  // A held pointer stays where the lock found it, and neither moves nor turns the view.
  const bool pointerHeld = (active && locked && !lockFree && pointerHold && s.pointerEnabled && !touchIdle) ||
                           (s.debugHold && s.pointerEnabled && !lockFree && !touchIdle);
  if (s.pointerEnabled || lockFree) {
    // MP1's pointer path: the travel moves the pointer, not the view. A lock (or anything that
    // stops free look) re-centres it (except under lock-on free aim), so lock-on, scan and grapple
    // still aim through the centre.
    // With the gyro driving the pointer, the stick turns the camera as it does without pointer
    // aim (MP1 4b8117d16).
    // A lock leaves the view nothing to turn, so the stick moves the pointer too.
    const bool stickTurns = s.gyroDrivesAim && !lockFree;
    const bool moves = (owns || lockFree) && !(s.debugHold && !lockFree);
    if (moves) {
      float sx = 0.f, sy = 0.f;
      PointerAimScale(sx, sy);
      if (stickTurns) {
        s.pointer.Move((s.frameX - s.stickFrameX) * sx, (s.frameY - s.stickFrameY) * sy);
      } else {
        s.pointer.Move(s.frameX * sx, s.frameY * sy);
      }
    } else if (!pointerHeld) {
      s.pointer.Center();
    }
    // The view turns only by the gyro's camera travel (zero unless it also turns the camera),
    // and the stick's when it turns the camera.
    const float cameraX = s.cameraFrameX + (stickTurns ? s.stickFrameX : 0.f);
    const float cameraY = s.cameraFrameY + (stickTurns ? s.stickFrameY : 0.f);
    const bool applied = s.aim.Update(active, held, x, y, z, owns ? cameraX : 0.f, owns ? cameraY : 0.f,
                                     sSensitivity, sInvertX, sInvertY);
    if (applied && owns && !s.debugHold) {
      float dyaw = 0.f, dpitch = 0.f;
      PortMouse::PointerAim::TurnAt(s.pointer.x, s.pointer.y, s.pointerBox, s.pointerTurnRate, dt,
                                    dyaw, dpitch);
      s.aim.Turn(dyaw, dpitch);
    }
    s.pointerFree = (applied && owns) || lockFree || pointerHeld;
    if (!s.pointerFree) {
      s.pointer.Center();
    }
    // The second update of a tick must not move the pointer by the same travel again.
    if (applied || lockFree) {
      s.frameX = s.frameY = s.stickFrameX = s.stickFrameY = 0.f;
      s.cameraFrameX = s.cameraFrameY = 0.f;
    }
    s.appliedLastTick = applied && owns;
    return applied && owns;
  }
  s.pointerFree = false;
  s.lockFree = false;
  s.lockStays = false;
  // Without pointer aim the gyro's travel turns the view beside the mouse's, stick's and touch's.
  const bool applied = s.aim.Update(active, held, x, y, z, s.frameX + s.cameraFrameX, s.frameY + s.cameraFrameY,
                                   sSensitivity, sInvertX, sInvertY);
  if (applied) {
    s.frameX = s.frameY = s.stickFrameX = s.stickFrameY = 0.f;
    s.cameraFrameX = s.cameraFrameY = 0.f;
  }
  s.appliedLastTick = applied && owns;
  return applied && owns;
}

bool PresentedAimDelta(float fraction, float& dyaw, float& dpitch, int player) {
  dyaw = dpitch = 0.f;
  Slot& s = SlotFor(player);
  if (&s == &sSlots[0] && PortRadial::Capturing()) return false;
  if (!s.appliedLastTick || !s.aiming || !s.aim.initialized || !std::isfinite(fraction) ||
      fraction < 0.f) {
    return false;
  }
  if (fraction > 1.f) {
    fraction = 1.f;
  }
  float dx = 0.f, dy = 0.f, cx = 0.f, cy = 0.f;
  {
    std::lock_guard< std::mutex > lock(sPendingMutex);
    dx = s.pendingX - s.stickPendingX * (1.f - fraction);
    dy = s.pendingY - s.stickPendingY * (1.f - fraction);
    cx = s.cameraPendingX;
    cy = s.cameraPendingY;
  }
  // The aim the next tick would reach, through the same Update/Turn as UpdateAim.
  PortMouse::AimState next = s.aim;
  if (s.pointerEnabled) {
    if (!s.pointerFree) {
      return false;
    }
    float sx = 0.f, sy = 0.f;
    PointerAimScale(sx, sy);
    PortMouse::PointerAim pointer = s.pointer;
    if (s.gyroDrivesAim) {
      // The stick turns the camera (UpdateAim's stickTurns): only its tick fraction shows.
      const float stickX = dx - (s.pendingX - s.stickPendingX);
      const float stickY = dy - (s.pendingY - s.stickPendingY);
      pointer.Move((dx - stickX) * sx, (dy - stickY) * sy);
      cx += stickX;
      cy += stickY;
    } else {
      pointer.Move(dx * sx, dy * sy);
    }
    next.Update(true, false, 0.f, 0.f, 0.f, cx, cy, sSensitivity, sInvertX, sInvertY);
    float turnYaw = 0.f, turnPitch = 0.f;
    PortMouse::PointerAim::TurnAt(pointer.x, pointer.y, s.pointerBox, s.pointerTurnRate,
                                  fraction * s.lastTickDt, turnYaw, turnPitch);
    next.Turn(turnYaw, turnPitch);
  } else {
    next.Update(true, false, 0.f, 0.f, 0.f, dx + cx, dy + cy, sSensitivity, sInvertX, sInvertY);
  }
  dyaw = static_cast< float >(std::remainder(double(next.yaw) - s.aim.yaw, 2.0 * PortMouse::kPi));
  dpitch = next.pitch - s.aim.pitch;
  return std::isfinite(dyaw) && std::isfinite(dpitch);
}

void SetPointerAim(bool enabled, float box, float turnRate, int player) {
  Slot& s = SlotFor(player);
  if (enabled != s.pointerEnabled) {
    // MP1's SetPointerAim: switching either way starts from the centre.
    s.pointer.Center();
    s.pointerFree = false;
    s.pointerEnabled = enabled;
  }
  if (std::isfinite(box)) {
    s.pointerBox = box < 0.f ? 0.f : box > 0.99f ? 0.99f : box;
  }
  if (std::isfinite(turnRate) && turnRate >= 0.f) {
    s.pointerTurnRate = turnRate;
  }
}

bool PointerAimEnabled(int player) { return SlotFor(player).pointerEnabled; }

bool LockFreeAimActive(int player) { return SlotFor(player).lockFree; }

bool PointerAimPosition(float& x, float& y, int player) {
  const Slot& s = SlotFor(player);
  x = s.pointer.x;
  y = s.pointer.y;
  return s.pointerFree;
}

void SetPointerAimPosition(float x, float y, int player) {
  Slot& s = SlotFor(player);
  s.pointer.Center();
  s.pointer.Move(x, y);
}

void SetPointerAimHold(bool hold, int player) { SlotFor(player).debugHold = hold; }

void Synchronize(float x, float y, float z, int player) {
  if (player == 0 ? Enabled() : StickEnabled(player)) {
    SlotFor(player).aim.Synchronize(x, y, z);
  }
}

void SetAimAngles(float yawDeg, float pitchDeg, int player) {
  ReadEnv();
  Slot& s = SlotFor(player);
  if (!std::isfinite(yawDeg) || !std::isfinite(pitchDeg)) {
    return;
  }
  // The camera builds its direction as (sin -yaw, cos -yaw, sin pitch) and CPlayer turns the
  // body by +yaw, so the world heading `face` names is -yaw here.
  const double radians = -double(yawDeg) * (PortMouse::kPi / 180.f);
  s.aim.yaw = static_cast< float >(std::remainder(radians, 2.0 * PortMouse::kPi));
  const double pitch = double(pitchDeg) * (PortMouse::kPi / 180.f);
  const double limit = double(kMaxPitch);
  s.aim.pitch = static_cast< float >(pitch < -limit ? -limit : pitch > limit ? limit : pitch);
  s.aim.initialized = true;
  // CFirstPersonCamera only consumes the aim while a direct source is on; mouse aim is the one
  // with no input of its own here, so a console run cannot turn it by accident.
  if (player == 0) {
    SetEnabled(true);
  }
}

void Reset() { ResetSlots(false); }

bool Aiming(int player) { return SlotFor(player).aiming; }
bool Initialized(int player) { return SlotFor(player).aim.initialized; }
float Yaw(int player) { return SlotFor(player).aim.yaw; }
float Pitch(int player) { return SlotFor(player).aim.pitch; }
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
