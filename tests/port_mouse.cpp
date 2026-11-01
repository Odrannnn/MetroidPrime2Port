// Standalone integration test: compile with platform/port_mouse.cpp and SDL's include path.
// SDL capture/window stubs keep this test deterministic and device-free; no SDL library needed.
#include "port_env.h"
#include "port_mouse.h"
#include "port_radial.h"
#include "port_test.h"
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace PortOverlaySettings { void LoadSettings() {} }

extern "C" {
SDL_Window** SDL_GetWindows(int* count) { *count = 0; return nullptr; }
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window*) { return 0; }
bool SDL_GetWindowSize(SDL_Window*, int*, int*) { return false; }
bool SDL_SetWindowRelativeMouseMode(SDL_Window*, bool) { return true; }
bool SDL_HideCursor() { return true; }
bool SDL_ShowCursor() { return true; }
Uint64 SDL_GetTicksNS() { return 0; }
}

namespace {
using namespace PortMouse;
bool Near(float a, float b) { return std::fabs(a - b) < 1e-6f; }
constexpr float dt = 1.f / 60.f;

void Start(float sensitivity, bool pointer = true, bool gyro = false) {
  Reset();
  SetEnabled(true);
  SetStickEnabled(true);
  SetTouchEnabled(true);
  SetSensitivity(sensitivity);
  SetSensitivitySplit(true);
  SetSensitivityY(sensitivity * 2.f);
  SetStickAimRate(600.f);
  SetStickAimSplit(true);
  SetStickAimRateY(300.f);
  SetTouchAimSpeed(2.25f);
  SetTouchAimSpeedY(3.f);
  SetInvertX(false);
  SetInvertY(false);
  SetGyroDrivesAim(gyro);
  SetPointerAim(pointer, 0.7f, 0.f);
  REQUIRE(UpdateAim(true, false, 0.f, 1.f, 0.f, dt));
}

void CheckPointer(float x, float y) {
  float px, py;
  REQUIRE(PointerAimPosition(px, py));
  REQUIRE(Near(px, x));
  REQUIRE(Near(py, y));
}

void Apply(bool locked = false, bool lockFree = false) {
  BeginTick();
  REQUIRE(UpdateAim(true, locked, 0.f, 1.f, 0.f, dt, lockFree) == !locked);
}
}

int main() {
  // Normalizing input alone was insufficient: the retail axis force includes
  // a friction-compensation offset, so evaluating it twice boosts diagonals.
  // Run the actual planar force/damping helpers at many headings, on the ground
  // and with air drag; each must reproduce the straight-ahead speed history.
  for (bool air : {false, true}) {
    for (float travel : {0.25f, 0.7f, 1.f}) {
      for (int heading = 0; heading < 16; ++heading) {
        const float angle = heading * kPi / 8.f;
        const Planar direction{std::sin(angle), std::cos(angle)};
        Planar velocity{0.f, 0.f};
        float straight = 0.f;
        for (int tick = 0; tick < 240; ++tick) {
          constexpr float mass = 90.f, maxSpeed = 11.8f, acceleration = 60.f;
          const float drag = air ? 3.5f * std::fabs(straight) / mass : 0.02f;
          const float speed = std::hypot(velocity.right, velocity.forward);
          const float planarDrag = air ? 3.5f * speed / mass : 0.02f;
          straight = std::fmin(std::fmax(straight - drag, 0.f), maxSpeed);
          velocity = DampMovement(velocity, planarDrag, maxSpeed);
          const float forwardForce = AxisForce(travel, straight, maxSpeed, 0.02f,
                                               mass, dt, acceleration);
          const Planar force = MovementForce({direction.right * travel, direction.forward * travel},
                                             velocity, maxSpeed, 0.02f, mass, dt, acceleration);
          straight += forwardForce * dt / mass;
          velocity.right += force.right * dt / mass;
          velocity.forward += force.forward * dt / mass;
          REQUIRE(std::fabs(std::hypot(velocity.right, velocity.forward) - straight) < 0.0001f);
        }
      }
    }
  }
  {
    constexpr float diagonal = 0.70710678f;
    const float straight = AxisForce(1.f, 0.f, 11.8f, 0.02f, 90.f, dt, 60.f);
    const float oldDiagonal = std::sqrt(2.f) * AxisForce(diagonal, 0.f, 11.8f, 0.02f, 90.f, dt, 60.f);
    REQUIRE(oldDiagonal > straight); // This reproduces the old force-law defect.
    const Planar fixed = MovementForce({diagonal, diagonal}, {0.f, 0.f},
                                       11.8f, 0.02f, 90.f, dt, 60.f);
    REQUIRE(std::fabs(std::hypot(fixed.right, fixed.forward) - straight) < 0.0001f);
  }
  // Ignore the user's per-run overrides: no settings, devices or window are consulted.
  for (const char* key : {"MP2_MOUSE_AIM", "MP2_TWIN_STICK", "MP2_MOUSE_SENS",
                          "MP2_MOUSE_SENS_Y", "MP2_MOUSE_SENS_SPLIT", "MP2_MOUSE_INVERT_X",
                          "MP2_MOUSE_INVERT_Y", "MP2_STICK_AIM_RATE", "MP2_STICK_AIM_RATE_Y",
                          "MP2_STICK_AIM_SPLIT", "MP2_TOUCH_AIM_SPEED"}) port::UnsetEnv(key);
  REQUIRE(Near(Sensitivity(), 0.0035f)); // Do not import MP1's changed default.
  REQUIRE(Near(SensitivityY(), 0.0035f));
  for (float sensitivity : {0.0001f, 0.0035f, 0.01f}) {
    const float sx = PointerUnitsPerCount(640.f, sensitivity);
    const float sy = PointerUnitsPerCount(480.f, sensitivity);
    const float stickX = 10.f * 2.f / 640.f;
    const float stickY = -5.f * 2.f / 480.f;

    Start(sensitivity);
    AddMotion(3.f, -2.f);
    Apply();
    CheckPointer(3.f * sx, -4.f * sy); // Split mouse Y only.

    Start(sensitivity);
    AddStickAim(1.f, 1.f, dt);
    Apply();
    CheckPointer(stickX, stickY); // Both stick axes invariant, including split rate.

    Start(sensitivity);
    AddMotion(3.f, -2.f);
    AddStickAim(1.f, 1.f, dt);
    AddTouchAim(2.f, -1.f);
    Apply();
    // Preserve MP2 touch tuning, including its existing mouse-relative scaling.
    CheckPointer(stickX + (3.f + 4.5f) * sx, stickY + (-4.f - 3.f) * sy);
    REQUIRE(UpdateAim(true, false, 0.f, 1.f, 0.f, dt));
    CheckPointer(stickX + 7.5f * sx, stickY - 7.f * sy); // No second consumption.

    Start(sensitivity, true, true);
    AddMotion(3.f, -2.f);
    AddStickAim(1.f, 1.f, dt);
    AddGyroPointer(0.3f, 0.2f, dt, 2.f, 1.5f);
    AddGyroCamera(0.3f, 0.2f, dt, 2.f, 3.f);
    Apply();
    CheckPointer(3.f * sx + 0.3f * dt, -4.f * sy - 0.2f * dt * 1.5f * 640.f / 480.f);
    REQUIRE(Near(Yaw(), -10.f * kAimPixelRadians - 0.3f * dt * 2.f));
    REQUIRE(Near(Pitch(), 5.f * kAimPixelRadians + 0.2f * dt * 3.f));
    AddStickAim(1.f, 1.f, dt);
    float previewYaw, previewPitch;
    REQUIRE(PresentedAimDelta(0.5f, previewYaw, previewPitch));
    REQUIRE(Near(previewYaw, -5.f * kAimPixelRadians));
    REQUIRE(Near(previewPitch, 2.5f * kAimPixelRadians));

    // During lock-on free aim the same stick moves the pointer, not the locked view.
    for (bool pointer : {false, true}) {
      Start(sensitivity, pointer, true);
      AddMotion(3.f, -2.f);
      AddStickAim(1.f, 1.f, dt);
      AddGyroPointer(0.3f, 0.2f, dt, 2.f);
      Apply(true, true);
      REQUIRE(LockFreeAimActive());
      CheckPointer(stickX + 3.f * sx + 0.3f * dt,
                   stickY - 4.f * sy - 0.2f * dt * 640.f / 480.f);
      REQUIRE(Near(Yaw(), 0.f) && Near(Pitch(), 0.f));
    }

    // An empty lock under pointer aim keeps the pointer where it was as the lock starts and
    // ends; a target lock re-centres it.
    for (bool empty : {true, false}) {
      Start(sensitivity, true);
      AddMotion(3.f, -2.f);
      Apply();
      float px, py;
      REQUIRE(PointerAimPosition(px, py));
      BeginTick();
      REQUIRE(!UpdateAim(true, true, 0.f, 1.f, 0.f, dt, true, false, empty));
      REQUIRE(LockFreeAimActive());
      if (empty) {
        CheckPointer(px, py);
      } else {
        CheckPointer(0.f, 0.f);
      }
      BeginTick();
      UpdateAim(true, false, 0.f, 1.f, 0.f, dt);
      if (empty) {
        CheckPointer(px, py);
      }
    }

    Start(sensitivity, false);
    AddMotion(3.f, -2.f);
    AddStickAim(1.f, 1.f, dt);
    AddTouchAim(2.f, -1.f);
    Apply();
    REQUIRE(Near(Yaw(), -10.f * kAimPixelRadians - 7.5f * sensitivity));
    REQUIRE(Near(Pitch(), 5.f * kAimPixelRadians + 7.f * sensitivity));
    AddMotion(3.f, -2.f);
    AddStickAim(1.f, 1.f, dt);
    float yaw, pitch;
    REQUIRE(PresentedAimDelta(0.5f, yaw, pitch));
    REQUIRE(Near(yaw, -5.f * kAimPixelRadians - 3.f * sensitivity));
    REQUIRE(Near(pitch, 2.5f * kAimPixelRadians + 4.f * sensitivity));
  }
  // A captured wheel consumes tick travel and freezes an off-center pointer's edge turn,
  // pending mouse/stick/gyro interpolation, and the release-consumption tick.
  Start(.0035f);
  SetPointerAim(true, .2f, 3.f);
  SetPointerAimPosition(.9f, .7f);
  float px, py;
  REQUIRE(PointerAimPosition(px, py));
  const float beforeYaw = Yaw(), beforePitch = Pitch();
  auto& radial = PortRadial::Transport();
  radial.SetAvailable(true);
  radial.Tick(true, false, 0, 0);
  AddMotion(100, -40);
  AddStickAim(1, 1, dt);
  AddGyroCamera(1, 1, dt, 2, 2);
  Apply();
  REQUIRE(Near(Yaw(), beforeYaw) && Near(Pitch(), beforePitch));
  CheckPointer(px, py);
  REQUIRE(radial.Vector().x > .4f && radial.Vector().y > .3f);
  AddMotion(20, 30);
  float dyaw = 99, dpitch = 99;
  REQUIRE(!PresentedAimDelta(.5f, dyaw, dpitch));
  REQUIRE(dyaw == 0 && dpitch == 0);
  radial.Tick(false, false, 0, 0);
  const PortRadial::Point geometry[] = {{0,0},{-1,-1},{1,-1},{0,1}};
  radial.Resolve(geometry, 15, 0);
  radial.TakePick(PortRadial::kBeam);
  REQUIRE(PortRadial::Capturing());
  REQUIRE(!PresentedAimDelta(.5f, dyaw, dpitch));
  Apply();
  REQUIRE(Near(Yaw(), beforeYaw) && Near(Pitch(), beforePitch));
  radial.Tick(false, false, 0, 0);
  REQUIRE(!PortRadial::Capturing());
  AddMotion(10, 0);
  Apply();
  REQUIRE(!Near(Yaw(), beforeYaw)); // ordinary simulation/input resumes
  // A different aiming finger or mouse must not change the selector owner's vector.
  Start(.0035f);
  PortRadial::CancelAllTouches();
  PortRadial::ServiceTouchCancellation();
  PortRadial::SetTouchContext(true);
  radial.SetAvailable(true);
  PortRadial::Poll(false, false, 0, 0);
  const auto touch = PortRadial::BeginTouch(PortRadial::kBeam, 17);
  REQUIRE(touch > 0);
  PortRadial::MoveTouch(touch, 17, -.8f, -.5f);
  PortRadial::Poll(false, false, 0, 0);
  REQUIRE(PortRadial::TouchOwnsSelection());
  AddMotion(200, 200);
  AddTouchAim(200, 200);
  Apply();
  REQUIRE(Near(radial.Vector().x, -.8f) && Near(radial.Vector().y, -.5f));
  PortRadial::CancelAllTouches();
  PortRadial::ServiceTouchCancellation();
  REQUIRE(!PortRadial::Capturing());
  std::puts("port_mouse: scaling, gyro, lock-on, radial tick/pointer/edge-turn/interpolation passed");
}
