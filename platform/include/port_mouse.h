#pragma once

#include <cmath>
#include <cstdint>

namespace PortMouse {
constexpr float kMaxPitch = 1.52f; // approximately 87 degrees, short of the pole
constexpr float kPi = 3.14159265358979323846f;

// MP1's: mouse sensitivity on the Quake/Source/Apex scale (0.022 degrees a count per unit).
constexpr double kSourceRadiansPerUnit = 0.022 * 3.14159265358979323846 / 180.0;
inline float SourceFromRadians(float radiansPerCount) {
  return float(radiansPerCount / kSourceRadiansPerUnit);
}
inline float RadiansFromSource(float source) { return float(source * kSourceRadiansPerUnit); }
// Fixed stick travel scale (the existing mouse default), independent of the mouse slider.
// MP1 22b2c9255 / cbb69b962: convert before mixing with mouse/touch/gyro counts.
constexpr float kAimPixelRadians = 0.0035f;
inline float AimPixelsAsCounts(float pixels, float mouseSensitivity) {
  return pixels * kAimPixelRadians / mouseSensitivity;
}
inline float PointerUnitsPerCount(float extentPixels, float mouseSensitivity) {
  return 2.f / extentPixels * (mouseSensitivity / kAimPixelRadians);
}
// Hand travel for a full turn at a mouse's counts per inch.
inline float CentimetresPer360(float radiansPerCount, float dpi) {
  if (!(radiansPerCount > 0.f) || !(dpi > 0.f)) return 0.f;
  return float(2.0 * 3.14159265358979323846 / radiansPerCount / dpi * 2.54);
}

struct Planar {
  float right;
  float forward;
};
inline Planar ClampPlanar(Planar value, float maximum) {
  const float length = std::hypot(value.right, value.forward);
  if (!std::isfinite(length) || maximum <= 0.f) return {0.f, 0.f};
  if (length > maximum) {
    const float scale = maximum / length;
    value.right *= scale;
    value.forward *= scale;
  }
  return value;
}

// The retail forward-force law applied to either horizontal axis. Damping and
// collision integration remain in the player; this is not teleport movement.
inline float AxisForce(float input, float velocity, float maxSpeed, float friction,
                       float mass, float dt, float acceleration) {
  if (input == 0.f || maxSpeed <= 0.f || dt <= 0.f || acceleration <= 0.f) return 0.f;
  const float frictionSpeed = friction * mass * maxSpeed / (dt * acceleration);
  const float desired = input * (maxSpeed - frictionSpeed) + (input > 0.f ? frictionSpeed : -frictionSpeed);
  const float fraction = (desired - velocity) / maxSpeed;
  return (fraction < -1.f ? -1.f : fraction > 1.f ? 1.f : fraction) * acceleration;
}

// Apply the forward force law once along the movement vector, not once per
// component: its friction-compensation term is nonlinear in the input.
inline Planar MovementForce(Planar input, Planar velocity, float maxSpeed, float friction,
                            float mass, float dt, float acceleration) {
  input = ClampPlanar(input, 1.f);
  const float length = std::hypot(input.right, input.forward);
  if (length <= 0.00001f) return {0.f, 0.f};
  const Planar direction{input.right / length, input.forward / length};
  const float along = direction.right * velocity.right + direction.forward * velocity.forward;
  const float force = AxisForce(length, along, maxSpeed, friction, mass, dt, acceleration);
  return {direction.right * force, direction.forward * force};
}

inline Planar DampMovement(Planar velocity, float friction, float maxSpeed) {
  const float length = std::hypot(velocity.right, velocity.forward);
  if (length <= 0.00001f) return {0.f, 0.f};
  const float speed = std::fmin(std::fmax(length - friction, 0.f), maxSpeed);
  return {velocity.right * speed / length, velocity.forward * speed / length};
}

struct AimState {
  float yaw = 0.f;
  float pitch = 0.f;
  bool initialized = false;

  void Reset() { initialized = false; }

  void Synchronize(float x, float y, float z) {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (!std::isfinite(length) || length < 0.00001f) {
      Reset();
      return;
    }
    // A target directly overhead has no horizontal heading. Retain the last
    // heading rather than snapping to atan2(0, 0) when that lock is released.
    if (x * x + y * y > length * length * 0.00000001f) yaw = std::atan2(-x, y);
    const float up = z / length;
    pitch = ClampPitch(std::asin(up < -1.f ? -1.f : up > 1.f ? 1.f : up));
    initialized = true;
  }

  // A locked camera owns orientation. Track its effective direction, but never
  // consume hidden mouse movement or rotate the player's body underneath it.
  bool Update(bool active, bool locked, float x, float y, float z,
              float dx, float dy, float sensitivity, bool invertX, bool invertY) {
    if (!active) {
      Reset();
      return false;
    }
    if (!initialized || locked) Synchronize(x, y, z);
    if (!initialized || locked) return false;
    if (!std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(sensitivity) || sensitivity <= 0.f) return true;
    // SDL motion is right/down positive; world +pitch looks upward.
    const double nextYaw = yaw + double(dx) * sensitivity * (invertX ? 1.0 : -1.0);
    const double nextPitch = pitch + double(dy) * sensitivity * (invertY ? 1.0 : -1.0);
    yaw = static_cast<float>(std::remainder(nextYaw, 2.0 * kPi));
    pitch = ClampPitch(nextPitch);
    return true;
  }

  // A direct turn in radians (world yaw +left, pitch +up), as pointer aim's edge turn makes.
  void Turn(float dyaw, float dpitch) {
    if (!initialized || !std::isfinite(dyaw) || !std::isfinite(dpitch)) return;
    yaw = static_cast<float>(std::remainder(double(yaw) + dyaw, 2.0 * kPi));
    pitch = ClampPitch(double(pitch) + dpitch);
  }

private:
  static float ClampPitch(double angle) {
    return static_cast<float>(angle < -kMaxPitch ? -kMaxPitch : angle > kMaxPitch ? kMaxPitch : angle);
  }
};

// Pointer aim (Trilogy style), MP1's PortMouse::PointerAim: the aim travel moves a pointer over
// the screen (x right, y down, -1..1 at the edges) instead of the view. Inside the dead box
// the view holds still; past it the view turns toward that edge, faster the further out the
// pointer is. The gun aims along the ray through the pointer. Between ticks the view's turn
// is previewed by PresentedAimDelta; the pointer itself still moves once per tick.
struct PointerAim {
  float x = 0.f;
  float y = 0.f;

  void Center() { x = y = 0.f; }

  // dx, dy: window-space travel already scaled to the -1..1 range.
  void Move(float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    x = Clamp1(x + dx);
    y = Clamp1(y + dy);
  }

  // How far past the dead box (0..1) the pointer is on one axis, signed.
  static float Overshoot(float value, float box) {
    if (!(box >= 0.f && box < 1.f)) return 0.f;
    const float past = (std::fabs(value) - box) / (1.f - box);
    if (!(past > 0.f)) return 0.f;
    return value < 0.f ? -std::fmin(past, 1.f) : std::fmin(past, 1.f);
  }

  // The view turn for dt seconds at (px, py): radians, world yaw (+left) and
  // pitch (+up). maxRate is the turn speed in radians/second at the screen edge.
  static void TurnAt(float px, float py, float box, float maxRate, float dt, float& dyaw,
                     float& dpitch) {
    dyaw = dpitch = 0.f;
    if (!std::isfinite(maxRate) || !std::isfinite(dt) || maxRate <= 0.f || dt <= 0.f) return;
    dyaw = -Overshoot(px, box) * maxRate * dt;
    dpitch = -Overshoot(py, box) * maxRate * dt;
  }

  // The view ray through (px, py) in camera space as yaw (+right) and pitch (+up)
  // angles applied in that order, for a vertical fov (degrees) and aspect ratio.
  static void RayAngles(float px, float py, float fovYDeg, float aspect, float& yaw,
                        float& pitch) {
    yaw = pitch = 0.f;
    const float tanY = std::tan(0.5f * fovYDeg * (kPi / 180.f));
    if (!std::isfinite(tanY) || !(tanY > 0.f) || !(aspect > 0.f)) return;
    const float right = px * tanY * aspect;
    const float up = -py * tanY;
    yaw = std::atan(right);
    pitch = std::atan2(up, std::sqrt(1.f + right * right));
  }

  // The camera-space direction (x right, y forward, z up) of RayAngles' angles.
  static void RayDirection(float yaw, float pitch, float& x, float& y, float& z) {
    x = std::sin(yaw) * std::cos(pitch);
    y = std::cos(yaw) * std::cos(pitch);
    z = std::sin(pitch);
  }

  // Scales (yaw, pitch) so the direction stays within maxAngle of straight ahead.
  // The angle is not linear in the scale, so a few passes settle it onto maxAngle.
  static void ClampAngles(float& yaw, float& pitch, float maxAngle) {
    for (int pass = 0; pass < 4; ++pass) {
      const float angle = std::acos(std::cos(yaw) * std::cos(pitch));
      if (!std::isfinite(angle) || !(angle > maxAngle) || !(angle > 0.f)) return;
      const float scale = maxAngle / angle;
      yaw *= scale;
      pitch *= scale;
    }
  }

private:
  static float Clamp1(float v) { return v < -1.f ? -1.f : v > 1.f ? 1.f : v; }
};

class ButtonGate {
public:
  void Reset() { mReady = false; }
  uint32_t Poll(bool enabled, uint32_t held) {
    if (!enabled) {
      Reset();
      return 0;
    }
    // A UI click or a button held while acquiring capture is not a new shot.
    // Once neutral has been observed, preserve held states for charge/release.
    if (!mReady) {
      mReady = held == 0;
      return 0;
    }
    return held;
  }
private:
  bool mReady = false;
};

// Mouse look. The platform owns the aim angles and SDL's relative-mouse capture; the game
// reads them from CPlayer (which turns the body to Yaw()) and CFirstPersonCamera (which uses
// the angles for the view). Off unless MP2_MOUSE_AIM=1, so a pad or the keyboard alone stays
// the default. MP2_MOUSE_SENS sets radians per pixel and MP2_MOUSE_INVERT_X/_Y flip an axis.
bool Enabled();
void SetEnabled(bool enabled);
// Twin-stick: the game feeds the right stick here through AddStickAim, so the stick aims
// through the same state as the mouse. Works with or without mouse aim.
bool StickEnabled();
void SetStickEnabled(bool enabled);
// Twin stick takes the C-stick for aim, so the game's C-stick beam picks ignore it (issue #15).
// Under mouse aim a keyboard key held on a C-stick direction (the mouse-and-keyboard preset's
// 1-4) gives it back for that poll: the key picks a beam and does not aim, as in MP1.
// PortInputDevices sets the key state each poll.
bool StickOwnsCStick();
void SetKeyboardCStickHeld(bool held);
float StickAimRate();
void SetStickAimRate(float pixelsPerSecond);
// Separate vertical speed: with the split on, the stick's up and down use StickAimRateY instead.
bool StickAimSplit();
void SetStickAimSplit(bool enabled);
float StickAimRateY();
void SetStickAimRateY(float pixelsPerSecond);
// Feeds a normalised right-stick vector (-1..1, x right, y up) for one tick.
void AddStickAim(float x, float y, float dt);
// Touch aim: the Android overlay's drag-to-aim, a third source alongside the mouse and the
// stick. `PortTouch` decides when it is on (its own MP2_TOUCH_AIM switch, or the active
// layout) and calls SetTouchEnabled; like twin-stick it works with or without mouse aim. The
// Java UI thread calls AddTouchAim/SetTouchAimDown/SpringSwipe, so those are safe from either
// thread and the game thread only ever sees a whole tick's travel.
bool TouchEnabled();
void SetTouchEnabled(bool enabled);
// Finger travel in density-independent pixels (right/down positive), scaled here by
// TouchAimSpeed() because the UI thread has no view of the aim's units.
void AddTouchAim(float dxDp, float dyDp);
// A finger is down on the aim area: this is what makes the touch aim own the view at all
// (see TouchAimActive). Lifting without SpringSwipe stops the aim where the finger left.
void SetTouchAimDown(bool down);
// A quick flick that lifted: keep the aim "down" for a short hold afterwards, so a flick
// turns the view by its whole travel instead of only the part seen while the finger was down.
void SpringSwipe();
// A finger is down, or a spring hold is live, and the source is on: the per-frame answer to
// "does the touch own the view right now". False while the overlay sits idle, which is most
// of a run.
bool TouchAimActive();
// Aim pixels per dp, the Prime port's `touch_aim_speed` (2.25 by default). Clamped to
// 0.25..10; MP2_TOUCH_AIM_SPEED overrides it the lazy way MP2_MOUSE_SENS does.
float TouchAimSpeed();
void SetTouchAimSpeed(float pixelsPerDp);
// The vertical drag's speed (PortTouch's "Separate vertical speed"; the same as TouchAimSpeed
// with the split off), same clamp.
float TouchAimSpeedY();
void SetTouchAimSpeedY(float pixelsPerDp);
// The debug overlay owns the cursor while it is open: no capture, no aim applied.
void SetUiActive(bool active);
bool UiActive();
// Mouse aim holds the pointer (cursor hidden, motion turns the view). Our own state, not
// SDL's relative mode, which Android can report off while its pointer capture is on.
bool Captured();
// Once per loop iteration: hides the desktop cursor after a few seconds without mouse input
// (a controller player in full screen) and shows it again on the next motion, click or wheel.
// Never while the overlay or `keepVisible` (photo mode's panel) wants it, nor while captured
// (capture hides it already).
void UpdateIdleCursor(bool mouseInput, bool keepVisible);
// Relative motion as it arrives from SDL's event queue, and the once-per-loop latch that
// turns the pending deltas into this tick's deltas.
void AddMotion(float dx, float dy);
// Photo mode's mouse look: sets relative capture while photo mode is active (grab or release, whoever took it), and hands
// out the travel latched by BeginTick as a turn in degrees (false when there was none).
void SetPhotoCapture(bool want);
// Photo mode ends: release the grab and give SetCapture back to the aim.
void EndPhotoCapture();
bool TakePhotoLook(float& yawDegrees, float& pitchDegrees);
// Gyro aim on the pointer (MP1's PollGyro pointer branch): yaw (right) and pitch (up) in rad/s
// over dt seconds; `sweep` radians of pad turn move the pointer across the screen, and the
// vertical travel is scaled by `scaleY` (the gyro's vertical over horizontal sensitivity).
void AddGyroPointer(float yaw, float pitch, float dt, float sweep, float scaleY = 1.f);
// Gyro camera travel (MP1's gyro pending): the view turns by `sensitivity` (`sensitivityY` up and
// down) times the pad's turn (yaw right, pitch up, rad/s over dt). Without pointer aim this is
// the gyro's aim; with it, "Gyro also turns the camera" beside the pointer.
void AddGyroCamera(float yaw, float pitch, float dt, float sensitivity, float sensitivityY);
// Set by PollGyro each loop while the gyro is read and aiming: under pointer aim the stick then
// turns the camera instead of moving the pointer.
void SetGyroDrivesAim(bool drives);
// The Reset aim binding: the next UpdateAim recentres the pointer under pointer aim, otherwise
// levels the view (pitch 0); `both` does both.
void RequestResetAim(bool both);
// Latches the pending travel for the tick about to run (MP1's per-tick latch): called beside
// PortTiming::BeginTick in CMain, so a frame that runs no tick (the cap off) loses nothing.
void BeginTick();
// MP1's PresentedAimDelta (look input per frame): the turn, in radians (world yaw +left, pitch
// +up), that the travel not yet taken by a tick adds to the aim the last tick applied. Mouse,
// touch and gyro travel shows fully; the stick's share shows `fraction` of it, as the next tick
// would integrate it (MP1's issue #128 lag), and pointer aim's edge turn runs `fraction` of a
// tick from the presented pointer. False when the last tick did not apply the aim.
bool PresentedAimDelta(float fraction, float& dyaw, float& dpitch);
// Called every tick, from CPlayer::UpdateMouseAim. `active` means a pointer source may own the
// aim right now (first person, not morphed, not in a cinematic, not locked on); `locked` keeps
// the aim glued to the camera direction (x, y, z) instead of turning the player under it.
// An enabled touch source with no finger down counts as locked until TouchAimActive(), so the
// aim tracks the camera the rest of the time without turning the player.
// Returns true when the caller should rotate the player to Yaw().
// dt is the tick period, which pointer aim's edge turn integrates over.
// `pointerLock` is lock-on free aim: with `locked` and `active` it moves a pointer over the locked
// view (pointer aim's, or one for the lock only; no edge turn) and still returns false.
// `pointerHold`: under pointer aim, a lock that keeps the pointer where it is instead of
// re-centring it (the Scan Visor's point orbit, CPlayer::UpdateMouseAim).
bool UpdateAim(bool active, bool locked, float x, float y, float z, float dt = 0.f,
               bool pointerLock = false, bool pointerHold = false);
// Pointer aim: CPlayer pushes the F1 settings here every tick (this file is built without the
// overlay). box is the dead box as a fraction of the half-screen (0.2..0.95), turnRate the edge
// turn in radians per second. It needs a direct source (mouse, twin stick or touch aim) on.
void SetPointerAim(bool enabled, float box, float turnRate);
bool PointerAimEnabled();
// The last UpdateAim left a pointer free under a lock (lock-on free aim).
bool LockFreeAimActive();
// The pointer (x right, y down, -1..1) and whether it is free this tick: pointer aim is on and
// the last UpdateAim applied it. Outside that the pointer is centred and the aim is retail's.
bool PointerAimPosition(float& x, float& y);
// The console's `pointeraim <x> <y>` (MP1's PortDebug::SetPointerAimPosition): moves the pointer
// there from the centre, clamped to -1..1. A tick that does not leave it free re-centres it.
void SetPointerAimPosition(float x, float y);
// The console holds a pointer it placed there (free, the view not turned) until it is placed at 0 0.
void SetPointerAimHold(bool hold);
void Synchronize(float x, float y, float z);
// The console's `aim <yaw_deg> <pitch_deg>`: place the aim outright instead of turning it from
// travel, which is the only way a run with no stick can aim at a low target. yawDeg is the
// view's heading in the world's own terms (0 = +y, 90 = +x, as `face` reads it); pitchDeg is
// +up, so a negative pitch looks down. It arms a direct source (mouse aim) the rest of the run,
// so CPlayer::UpdateMouseAim is the one that turns the body and CFirstPersonCamera reads the
// pitch. A tick that has something else owning the view resynchronises and drops it.
void SetAimAngles(float yawDeg, float pitchDeg);
void Reset();
bool Aiming();
bool Initialized();
float Yaw();
float Pitch();
float Sensitivity();
void SetSensitivity(float radiansPerPixel);
// MP1's "Separate vertical speed": vertical mouse counts turn at SensitivityY (rad per count);
// `MP2_MOUSE_SENS_SPLIT`, `MP2_MOUSE_SENS_Y`.
bool SensitivitySplit();
void SetSensitivitySplit(bool enabled);
float SensitivityY();
void SetSensitivityY(float radiansPerPixel);
// The mouse's counts per inch, 100..64000; only for F1's cm/360 readout.
int MouseDpi();
void SetMouseDpi(int dpi);
bool InvertX();
bool InvertY();
void SetInvertX(bool invert);
void SetInvertY(bool invert);
} // namespace PortMouse
