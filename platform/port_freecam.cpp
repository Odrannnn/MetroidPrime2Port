// The debug free camera (F1 > Debug > Camera > Free camera, the Photo mode key); see port_freecam.h.
// Stage 1: no tick-blended presentation (MP2 has no per-actor tick generation), no mouse look.
#include "port_freecam.h"

#include "port_overlay.h"
#include "port_third_person.h"
#include "Kyoto/Input/CFinalInput.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Math/CTransform4f.hpp"
#include "Kyoto/Math/CVector3f.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include <algorithm>
#include <cmath>

namespace PortFreeCam {
namespace {

constexpr float kDegrees = 57.29578f;
constexpr float kTurnRate = 120.f; // degrees per second at a full stick
constexpr float kPitchLimit = 89.f;

bool sActive = false;
bool sFrozen = false;
float sSpeed = 10.f;
bool sShowPlayer = true;
Pose sPose{0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
float sFov = 0.f;
float sTouchMoveX = 0.f;
float sTouchMoveY = 0.f;
float sTouchYaw = 0.f;
float sTouchPitch = 0.f;

CTransform4f Rotation(const Pose& pose) {
  return CTransform4f::RotateZ(CRelAngle::FromDegrees(pose.yaw)) *
         CTransform4f::RotateX(CRelAngle::FromDegrees(pose.pitch));
}

CTransform4f ViewRotation(const Pose& pose) {
  return Rotation(pose) * CTransform4f::RotateY(CRelAngle::FromDegrees(pose.roll));
}

Pose PresentedPose() { return sPose; }

// True when the camera was switched on without a state manager (the F8 key and the F1 row have
// none): the next View() starts the pose at the game camera it is given.
bool sPendingStart = false;

void StartAt(const CTransform4f& xf) {
  const CVector3f fwd = xf.GetForward();
  const CVector3f pos = xf.GetTranslation();
  sPose.x = pos.GetX();
  sPose.y = pos.GetY();
  sPose.z = pos.GetZ();
  sPose.yaw = std::atan2(-fwd.GetX(), fwd.GetY()) * kDegrees;
  sPose.pitch = std::asin(std::clamp(fwd.GetZ(), -1.f, 1.f)) * kDegrees;
  sPose.roll = 0.f;
}

} // namespace

bool Active() { return sActive; }

void SetActive(bool on, const CStateManager* mgr) {
  if (on && !sActive) {
    if (mgr != nullptr) {
      StartAt(mgr->GetCameraManager(0)->GetCurrentCameraTransform(*mgr, true));
      sPendingStart = false;
    } else {
      sPendingStart = true;
    }
  }
  sActive = on;
  SetTouchMove(0.f, 0.f);
  sTouchYaw = 0.f;
  sTouchPitch = 0.f;
}

bool Frozen() { return sActive && sFrozen; }
void SetFrozen(bool frozen) { sFrozen = frozen; }

float Speed() { return sSpeed; }
void SetSpeed(float speed) {
  if (std::isfinite(speed)) {
    sSpeed = std::clamp(speed, 0.1f, 500.f);
  }
}

bool ShowPlayer() { return sActive && sShowPlayer; }
void SetShowPlayer(bool show) { sShowPlayer = show; }

Pose GetPose() { return sPose; }

void SetPose(const Pose& pose, bool /*snap*/) {
  sPose = pose;
  sPose.pitch = std::clamp(sPose.pitch, -kPitchLimit, kPitchLimit);
  sPose.roll = std::isfinite(sPose.roll) ? std::clamp(sPose.roll, -180.f, 180.f) : 0.f;
}

float FovOverride() { return sActive ? sFov : 0.f; }
float PresentedFovOverride() { return FovOverride(); }
void SetTouchMove(float x, float y) {
  sTouchMoveX = std::isfinite(x) ? std::clamp(x, -1.f, 1.f) : 0.f;
  sTouchMoveY = std::isfinite(y) ? std::clamp(y, -1.f, 1.f) : 0.f;
}

void AddTouchTurn(float yaw, float pitch) {
  if (std::isfinite(yaw) && std::isfinite(pitch)) {
    sTouchYaw += yaw;
    sTouchPitch += pitch;
  }
}

void SetFovOverride(float degrees, bool /*snap*/) {
  sFov = std::isfinite(degrees) && degrees > 0.f ? std::clamp(degrees, 1.f, 170.f) : 0.f;
}

bool Input(const CFinalInput& input) {
  if (!sActive) {
    return false;
  }
  if (sPendingStart || PortOverlay::Visible()) {
    return true; // the overlay has the pad
  }
  const float dt = std::clamp(input.DeltaTime(), 0.f, 0.1f);
  sPose.yaw -= (input.ARARight() - input.ARALeft()) * kTurnRate * dt;
  sPose.pitch += (input.ARAUp() - input.ARADown()) * kTurnRate * dt;
  sPose.yaw += sTouchYaw;
  sPose.pitch += sTouchPitch;
  sTouchYaw = 0.f;
  sTouchPitch = 0.f;
  sPose.yaw = std::fmod(sPose.yaw, 360.f);
  sPose.pitch = std::clamp(sPose.pitch, -kPitchLimit, kPitchLimit);

  const float step = sSpeed * (input.DRTrigger() || input.DR() ? 4.f : 1.f) * dt;
  const float rise = (input.DZ() || input.DDPUp() ? 1.f : 0.f) -
                     (input.DLTrigger() || input.DL() || input.DDPDown() ? 1.f : 0.f);
  const CTransform4f rot = Rotation(sPose);
  const float ahead = std::clamp(input.GetAnalogLeftY() + sTouchMoveY, -1.f, 1.f);
  const float side = std::clamp(input.GetAnalogLeftX() + sTouchMoveX, -1.f, 1.f);
  const CVector3f move = rot.GetForward() * ahead + rot.GetRight() * side;
  sPose.x += move.GetX() * step;
  sPose.y += move.GetY() * step;
  sPose.z += move.GetZ() * step + rise * step;
  return true;
}

CTransform4f View(const CTransform4f& game) {
  if (!sActive) {
    return PortThirdPerson::View(game);
  }
  if (sPendingStart) {
    StartAt(game);
    sPendingStart = false;
  }
  const Pose pose = PresentedPose();
  CTransform4f xf = ViewRotation(pose);
  xf.SetTranslation(CVector3f(pose.x, pose.y, pose.z));
  return xf;
}

} // namespace PortFreeCam
