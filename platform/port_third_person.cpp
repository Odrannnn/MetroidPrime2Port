// The opt-in third-person gameplay camera; see port_third_person.h.
#include "port_third_person.h"

#include "port_freecam.h"
#include "Collision/CMaterialFilter.hpp"
#include "Collision/CRayCastResult.hpp"
#include "Kyoto/Math/CTransform4f.hpp"
#include "Kyoto/Math/CVector3f.hpp"
#include "MetroidPrime/CActor.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Cameras/CBallCamera.hpp"
#include "MetroidPrime/Cameras/CFirstPersonCamera.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"

namespace PortThirdPerson {
namespace {

constexpr float kReleaseTime = 0.15f; // seconds to ease back out after a wall lets go

bool sEnabled = false;
bool sActive = false;
Params sParams;
Params sFramed; // sParams for the live view (Frame)
// The distance behind the pivot the camera sits at: this tick's, and the tick's start, which
// frames between ticks blend from (as CCameraManager's snapshot does).
float sLength = 0.f;
float sPrevLength = 0.f;
uint sPrevGeneration = 0;
CVector3f sPosition = CVector3f::Zero();
CVector3f sAimPoint = CVector3f::Zero();
bool sAimValid = false;

float Clamp(float v, float lo, float hi, float old) {
  return std::isfinite(v) ? std::clamp(v, lo, hi) : old;
}

Vec3 ToVec(const CVector3f& v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
CVector3f FromVec(const Vec3& v) { return CVector3f(v.x, v.y, v.z); }

struct Geometry {
  CVector3f pivot;
  CVector3f dir; // unit, pivot toward the wanted eye
  float full;    // pivot to the wanted eye
};

Geometry Compute(const CTransform4f& xf) {
  const Vec3 eye = ToVec(xf.GetTranslation());
  const Vec3 fwd = ToVec(xf.GetForward());
  const Vec3 right = ToVec(xf.GetRight());
  const Vec3 up = ToVec(xf.GetUp());
  const Vec3 pivot{eye.x + up.x * sFramed.height, eye.y + up.y * sFramed.height,
                   eye.z + up.z * sFramed.height};
  const Vec3 want = ComputeEye(eye, fwd, right, up, sFramed);
  Geometry g;
  g.pivot = FromVec(pivot);
  const CVector3f delta = FromVec(want) - g.pivot;
  g.full = delta.Magnitude();
  g.dir = delta.CanBeNormalized() ? delta.AsNormalized() : CVector3f(0.f, 0.f, 1.f);
  return g;
}

void MarkTick() {
  if (sPrevGeneration != CActor::PortTickGeneration()) {
    sPrevLength = sLength;
    sPrevGeneration = CActor::PortTickGeneration();
  }
}

float Blend() {
  const float t = CCameraManager::GetPresentationInterpolation();
  if (t < 0.f || t >= 1.f || sPrevGeneration != CActor::PortTickGeneration()) {
    return -1.f;
  }
  return t;
}

// What the camera stops short of.
const CMaterialFilter& WallFilter() {
  static const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid), CMaterialList(kMT_ProjectilePassthrough));
  return filter;
}

// sFramed for the first-person camera's live FOV and aspect.
void Frame(const CFirstPersonCamera& fp) {
  constexpr float kDegToRad = 3.14159265f / 180.f;
  sFramed = Framed(sParams, std::tan(0.5f * fp.GetRenderFov() * kDegToRad), fp.GetAspectRatio());
}

} // namespace

// Single player only: each split-screen viewport has its own camera manager and the HUD,
// reticles and shots below read player 0's.
bool ViewAllowed(const CStateManager& mgr) {
  return sEnabled && !PortFreeCam::Active() && mgr.GetNumPlayers() == 1 && !mgr.IsMultiplayer() &&
         mgr.GetPlayer(0) != nullptr && mgr.GetCameraManager(0) != nullptr &&
         !mgr.GetCameraManager(0)->IsInCinematicCamera();
}

namespace {

bool StateAllows(const CStateManager& mgr) {
  if (!ViewAllowed(mgr)) {
    return false;
  }
  const CPlayer* player = mgr.GetPlayer(0);
  const CCameraManager* cams = mgr.GetCameraManager(0);
  const CFirstPersonCamera* fp = cams->GetFirstPersonCamera();
  return fp != nullptr && player->GetMorphballTransitionState() == CPlayer::kMS_Unmorphed &&
         player->GetCameraState() == CPlayer::kCS_FirstPerson &&
         cams->GetCurrentCameraId(true) == fp->GetUniqueId();
}

} // namespace

bool Enabled() { return sEnabled; }
void SetEnabled(bool on) {
  sEnabled = on;
  if (!on) {
    sActive = false;
  }
}
bool Active() { return sEnabled && sActive; }

bool ShowBody(const CStateManager& mgr) {
  if (Active()) {
    return true;
  }
  if (!ViewAllowed(mgr)) {
    return false;
  }
  const CCameraManager* cams = mgr.GetCameraManager(0);
  const CBallCamera* ball = cams->GetBallCamera();
  return ball != nullptr && mgr.GetPlayer(0)->GetCameraState() == CPlayer::kCS_FirstPerson &&
         cams->GetCurrentCameraId(true) == ball->GetUniqueId();
}

Params GetParams() { return sParams; }
void SetDistance(float m) { sParams.distance = Clamp(m, kDistanceMin, kDistanceMax, sParams.distance); }
void SetShoulder(float m) { sParams.shoulder = Clamp(m, kShoulderMin, kShoulderMax, sParams.shoulder); }
void SetHeight(float m) { sParams.height = Clamp(m, kHeightMin, kHeightMax, sParams.height); }

void Reset() {
  sActive = false;
  sAimValid = false;
  sLength = sPrevLength = 0.f;
}

void Update(CStateManager& mgr, float dt) {
  if (!StateAllows(mgr)) {
    sActive = false;
    sAimValid = false;
    return;
  }
  const CFirstPersonCamera& fp = *mgr.GetCameraManager(0)->GetFirstPersonCamera();
  Frame(fp);
  const CTransform4f xf = fp.GetTransform();
  const Geometry g = Compute(xf);
  float target = g.full;
  if (g.full > 0.f) {
    const CRayCastResult hit = mgr.RayStaticIntersection(g.pivot, g.dir, g.full, WallFilter());
    target = PullInLength(g.full, hit.IsValid() ? hit.GetTime() : -1.f);
  }
  if (!sActive) {
    sActive = true;
    sLength = sPrevLength = target;
    sPrevGeneration = CActor::PortTickGeneration();
  } else {
    MarkTick();
    if (target < sLength) {
      sLength = target; // a wall: straight in
    } else {
      sLength += (target - sLength) * (1.f - std::exp(-std::max(dt, 0.f) / kReleaseTime));
    }
  }
  sPosition = g.pivot + g.dir * sLength;

  // The centre of the view is what the player points at: a ray from the camera along its
  // facing, stopped by the world or a solid actor like a shot.
  const CVector3f camFwd = xf.GetForward();
  if (camFwd.CanBeNormalized()) {
    const CVector3f dir = camFwd.AsNormalized();
    static const CMaterialFilter aimFilter = CMaterialFilter::MakeIncludeExclude(
        CMaterialList(kMT_Solid), CMaterialList(kMT_Player, kMT_ProjectilePassthrough));
    rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
    mgr.BuildNearList(nearList, sPosition, dir, kAimRange, aimFilter, mgr.GetPlayer(0));
    TUniqueId hitId = kInvalidUniqueId;
    const CRayCastResult hit =
        mgr.RayWorldIntersection(hitId, sPosition, dir, kAimRange, aimFilter, nearList);
    sAimPoint = hit.IsValid() ? hit.GetPoint() : sPosition + dir * kAimRange;
    sAimValid = true;
  } else {
    sAimValid = false;
  }
}

bool AimPoint(CVector3f& out) {
  if (!Active() || !sAimValid) {
    return false;
  }
  out = sAimPoint;
  return true;
}

void GetCameraPosition(float out[3]) {
  out[0] = sPosition.GetX();
  out[1] = sPosition.GetY();
  out[2] = sPosition.GetZ();
}

CTransform4f TickView(const CTransform4f& game) {
  if (!Active()) {
    return game;
  }
  const Geometry g = Compute(game);
  CTransform4f xf = game;
  xf.SetTranslation(g.pivot + g.dir * std::min(sLength, g.full));
  return xf;
}

bool MorphCameraPosition(const CStateManager& mgr, const CTransform4f& fp, CVector3f& out) {
  if (Active()) {
    out = TickView(fp).GetTranslation();
    return true;
  }
  if (!ViewAllowed(mgr)) {
    return false;
  }
  if (const CFirstPersonCamera* cam = mgr.GetCameraManager(0)->GetFirstPersonCamera()) {
    Frame(*cam);
  }
  const Geometry g = Compute(fp);
  float len = g.full;
  if (g.full > 0.f) {
    const CRayCastResult hit = mgr.RayStaticIntersection(g.pivot, g.dir, g.full, WallFilter());
    len = PullInLength(g.full, hit.IsValid() ? hit.GetTime() : -1.f);
  }
  out = g.pivot + g.dir * len;
  return true;
}

CTransform4f View(const CTransform4f& game) {
  if (!Active()) {
    return game;
  }
  const Geometry g = Compute(game);
  float len = sLength;
  const float t = Blend();
  if (t >= 0.f) {
    len = sPrevLength + (sLength - sPrevLength) * t;
  }
  CTransform4f xf = game;
  xf.SetTranslation(g.pivot + g.dir * std::min(len, g.full));
  return xf;
}

} // namespace PortThirdPerson
