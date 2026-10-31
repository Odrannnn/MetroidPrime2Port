#include "MetroidPrime/CTargetReticles.hpp"

#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Graphics/CModel.hpp"
#include "Kyoto/Graphics/CModelFlags.hpp"
#include "Kyoto/Graphics/CTexture.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/Math/CMatrix3f.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Math/CloseEnough.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#include "MetroidPrime/CActor.hpp"
#ifdef TARGET_PC
#include "MetroidPrime/CAnimData.hpp"
#endif
#ifdef TARGET_PC
#include "MetroidPrime/CCollisionActor.hpp"
#endif
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/CEulerAngles.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CObjectList.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Cameras/CGameCamera.hpp"
#include "MetroidPrime/Enemies/CSandworm.hpp"
#include "MetroidPrime/Factories/CScannableObjectInfo.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerGun.hpp"
#include "MetroidPrime/Player/CPlayerTargeting.hpp"
#include "MetroidPrime/SFX/UI.h"
#include "MetroidPrime/SFX/UI_MP.h"
#include "MetroidPrime/ScriptObjects/CScriptGrapplePoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptHUDHint.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Tweaks/CTweakPlayer.hpp"
#include "MetroidPrime/Tweaks/CTweakTargeting.hpp"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef TARGET_PC
#include "port_entry.h"
#include "port_mouse.h"
#include "port_overlay.h"
#include "port_tri_reticle.h"
#include "MetroidPrime/Enemies/CPatterned.hpp"

#include <algorithm>
#include <vector>
#endif

// Guessed name, corroborated by Prime.
static CTargetReticleRenderState skZeroRenderState(kInvalidUniqueId, 1.f, CVector3f::Zero(), 0.f,
                                                   1.f, true);

// Guessed name; the three layout factors precede the native asset-name pool.
static const float skViewportLayoutScale[] = {1.f, 0.8f, 0.6f};

// Guessed name; native scan-lock scaling is independent of viewport radius clamps.
static const float skScanLockLayoutScale[] = {1.f, 1.4f, 1.f};

static bool IsDamageOrbit(CPlayer::EPlayerOrbitRequest request) {
  // Native damage/lock-break requests; the remaining enumerator names are unresolved.
  switch (static_cast< int >(request)) {
  case 5:
  case 8:
  case 9:
  case 10:
  case 11:
    return true;
  default:
    return false;
  }
}

static float offshoot_func(float amplitude, float angularScale, float time) {
  return amplitude * CMath::FastSinR((time - 0.5f) * angularScale) + 0.5f;
}

static float calculate_premultiplied_overshoot_offset(float overshoot) {
  const float angle = static_cast< float >(asin(1.f / overshoot));
  return 2.f * (M_PIF - angle);
}

CCompoundTargetReticle::SOuterItemInfo::SOuterItemInfo(const char* modelName)
: mModel(gpSimplePool->GetObj(modelName))
, mOffshootBaseAngle(0.f)
, mRotationAngle(0.f)
, mBaseAngle(0.f)
, mOffshootAngleDelta(0.f) {}

CCompoundTargetReticle::CCompoundTargetReticle(const CStateManager& mgr, int playerIndex)
: mPlayerIndex(playerIndex)
, mLeadingOrientation(CQuaternion::FromMatrix(
      mgr.GetCameraManager(playerIndex)->GetCurrentCamera(mgr, true)->GetTransform()))
, mLaggingOrientation(CQuaternion::FromMatrix(
      mgr.GetCameraManager(playerIndex)->GetCurrentCamera(mgr, true)->GetTransform()))
, mPreviousState(kRS_Unspecified)
, mNextState(kRS_Unspecified)
, mNoDrawTicks(0)
, mOvershootOffsetHalf(0.5f * gpTweakTargeting->GetChargeGaugeOvershootOffset())
, mPremultipliedOvershootOffset(
      calculate_premultiplied_overshoot_offset(gpTweakTargeting->GetChargeGaugeOvershootOffset()))
, mCrosshairs(gpSimplePool->GetObj("CMDL_Crosshairs"))
, mSeeker(gpSimplePool->GetObj("CMDL_Seeker"))
, mTargetFlower(gpSimplePool->GetObj("CMDL_TargetFlower"))
, mMissileBracket(gpSimplePool->GetObj("CMDL_MissileBracket"))
, mInnerBeamIcon(gpSimplePool->GetObj("CMDL_InnerBeamIcon"))
, mLockConfirm(gpSimplePool->GetObj("CMDL_LockConfirm"))
, mLockFire(gpSimplePool->GetObj("CMDL_LockFire"))
, mLockDagger(gpSimplePool->GetObj("CMDL_LockDagger0"))
, mGrapple(gpSimplePool->GetObj("CMDL_Grapple"))
, mChargeTickFirst(gpSimplePool->GetObj("CMDL_ChargeTickFirst"))
, mScanTargetCenter(gpSimplePool->GetObj("CMDL_ScanTargetCenter"))
, mScanTargetLeft(gpSimplePool->GetObj("CMDL_ScanTargetLeft"))
, mScanTargetRight(gpSimplePool->GetObj("CMDL_ScanTargetRight"))
, mChargeGauge("CMDL_ChargeGauge")
, mQuarterCurve(gpSimplePool->GetObj("TXTR_QuaterCurve"))
, mSeekerMissileLockConfirm(gpSimplePool->GetObj("CMDL_SeekerMissileLockConfirm"))
, mSeekerMissileCrosshair(gpSimplePool->GetObj("CMDL_SeekerMissileCrosshair"))
, mRadarPaintFirst(gpSimplePool->GetObj("TXTR_RadarPaint"))
, mRadarPaintSecond(gpSimplePool->GetObj("TXTR_RadarPaint"))
, mTargetId(kInvalidUniqueId)
, mNextTargetId(kInvalidUniqueId)
, mTargetPosition(CalculateOrbitZoneReticlePosition(mgr, false))
, mLaggingTargetPosition(CalculateOrbitZoneReticlePosition(mgr, true))
, mCurrentGroupInterpolated(skZeroRenderState)
, mCurrentGroupA(skZeroRenderState)
, mCurrentGroupB(skZeroRenderState)
, mCurrentGroupDuration(0.f)
, mCurrentGroupTimer(0.f)
, mNextGroupInterpolated(skZeroRenderState)
, mNextGroupA(skZeroRenderState)
, mNextGroupB(skZeroRenderState)
, mNextGroupDuration(0.f)
, mNextGroupTimer(0.f)
, mGrapplePointA(kInvalidUniqueId)
, mGrapplePointB(kInvalidUniqueId)
, mGrapplePointFactorA(0.f)
, mGrapplePointFactorB(0.f)
, mVulnerabilityTarget(kInvalidUniqueId)
, mTargetVulnerability(CDamageVulnerability::ImmuneVulnerabilty())
, mTargetHealth(0.f)
, mTargetHealthShadow(0.f)
, mCrosshairsDrawScale(0.f)
, mSeekerRotationAngle(0.f)
, mFlowerRotationAngle(0.f)
, mMissileActive(false)
, mMissileBracketTimer(0.f)
, mMissileBracketScaleTimer(0.f)
, mBeam(CPlayerState::kBI_Power)
, mChargeGaugeOvershootTimer(0.f)
, mLockOnTimer(0.f)
, mNextTargetFade(0.f)
, mLockFireTimer(0.f)
, mFullChargeFadeTimer(0.f)
, mScanBracketFactor(0.f)
, mScanTargetFactor(0.f)
, mBeamShot(false)
, mMissileShot(false)
, mFullyCharged(false) {
#ifdef TARGET_PC
  if (port::entry::IsTrilogy(port::entry::CurrentDiscVersion())) {
    mPortTri = true;
    // Ctor tokens in rs5mp2_p.dol (0x80259b70); the same RS5 names as MP1's.
    new (&mPortTriScanRing) TCachedToken< CModel >(gpSimplePool->GetObj("CMDL_ScanReticleRing")); // default CToken leaves mObjRef unset
    new (&mPortTriScanBracket) TCachedToken< CModel >(gpSimplePool->GetObj("CMDL_ScanReticleBracket")); // default CToken leaves mObjRef unset
    new (&mPortTriScanProgress) TCachedToken< CModel >(gpSimplePool->GetObj("CMDL_ScanReticleProgress")); // default CToken leaves mObjRef unset
    // Combat tokens, same ctor 0x80259b70 (this+0x3c8..0x410): aim centre/arm, orbit lock arm /
    // tech / brackets / base, MP1 names.
    new (&mPortTriAimCenter) TCachedToken< CModel >(gpSimplePool->GetObj("CMDL_CombatAimingCenter")); // default CToken leaves mObjRef unset
    new (&mPortTriAimArm) TCachedToken< CModel >(gpSimplePool->GetObj("CMDL_CombatAimingArm")); // default CToken leaves mObjRef unset
    new (&mPortTriLockArm) TCachedToken< CModel >(gpSimplePool->GetObj("CMDL_OrbitLockArm")); // default CToken leaves mObjRef unset
    new (&mPortTriLockTech) TCachedToken< CModel >(gpSimplePool->GetObj("CMDL_OrbitLockTech")); // default CToken leaves mObjRef unset
    new (&mPortTriLockBrackets) TCachedToken< CModel >(gpSimplePool->GetObj("CMDL_OrbitLockBrackets")); // default CToken leaves mObjRef unset
    new (&mPortTriLockBase) TCachedToken< CModel >(gpSimplePool->GetObj("CMDL_OrbitLockBase")); // default CToken leaves mObjRef unset
    mPortTriAimCenter.Lock();
    mPortTriAimArm.Lock();
    mPortTriLockArm.Lock();
    mPortTriLockTech.Lock();
    mPortTriLockBrackets.Lock();
    mPortTriLockBase.Lock();
  }
#endif
  mOuterBeamIconSquares.reserve(9);
  for (int i = 0; i < 9; ++i) {
    char name[64];
    sprintf(name, "%s%d", "CMDL_BeamSquare", i);
    mOuterBeamIconSquares.push_back_unsafe(SOuterItemInfo(name));
  }
  mCrosshairs.Lock();
  mQuarterCurve.Lock();
  mSeeker.Lock();
  mGrapple.Lock();
  mSeekerMissileLockConfirm.Lock();
  mSeekerMissileCrosshair.Lock();
  mRadarPaintFirst.Lock();
  mRadarPaintSecond.Lock();
}

bool CCompoundTargetReticle::CheckLoadComplete() { return true; }

#ifdef TARGET_PC
namespace {
// rs5mp2_p.dol 0x8025fbcc/0x80260070 share MP1's Trilogy constants; the tweak colours below are
// MP1's Trilogy tweak ctor values (fn_802685B8), not confirmed in Echoes' tweak.
const float kPortTriScanFadeTime = 0.15f;
const float kPortTriScanRingScale = 0.25f;
const CColor kPortTriScanRingColor(uchar(0x56), uchar(0xC1), uchar(0xFB), uchar(0x9F));
const CColor kPortTriScanBracketColor(uchar(0x49), uchar(0xC3), uchar(0xF6), uchar(0xA0));
const CColor kPortTriScanLockedColor(uchar(0x49), uchar(0xC3), uchar(0xF6), uchar(0x31));

// psq_st ...,1,qr2: float to u8, saturating, truncating.
uchar PortTriAlphaU8(float a) { return static_cast< uchar >(CMath::Clamp(0.f, a, 255.f)); }

// The pointer ray (screen centre without a pointer) in world space.
CVector3f PortTriPointerDir(const CTransform4f& camXf, const CGameCamera& cam) {
  float x = 0.f;
  float y = 0.f;
  if (!PortMouse::PointerAimPosition(x, y) || (x == 0.f && y == 0.f)) {
    return camXf.GetForward();
  }
  float yaw = 0.f, pitch = 0.f, rx = 0.f, ry = 1.f, rz = 0.f;
  PortMouse::PointerAim::RayAngles(x, y, cam.GetFov(), cam.GetAspectRatio(), yaw, pitch);
  PortMouse::PointerAim::RayDirection(yaw, pitch, rx, ry, rz);
  return camXf.Rotate(CVector3f(rx, ry, rz));
}
} // namespace

void CCompoundTargetReticle::PortTriUpdateScanPointer(const CStateManager& mgr) {
  const CCameraManager* camMgr = mgr.GetCameraManager(mPlayerIndex);
  const CTransform4f camXf = camMgr->GetCurrentCameraTransform(mgr, true);
  const CVector3f start = camXf.GetTranslation();
  CVector3f dir = PortTriPointerDir(camXf, *camMgr->GetCurrentCamera(mgr, true));
  if (!dir.CanBeNormalized()) {
    return;
  }
  dir.Normalize();
  const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid, kMT_Character, kMT_NonSolidDamageable),
      CMaterialList(kMT_ProjectilePassthrough, kMT_Player));
  rstl::reserved_vector< TUniqueId, 1024 > nearList;
  mgr.BuildNearList(nearList, start, dir, 100.f, filter, mgr.GetPlayer(mPlayerIndex));
  TUniqueId hitId = kInvalidUniqueId;
  const CRayCastResult result = mgr.RayWorldIntersection(hitId, start, dir, 100.f, filter, nearList);
  float dist = 100.f;
  if (result.IsValid()) {
    dist = rstl::max_val(result.GetTime(), 0.25f);
  }
  mPortTriPointerPos = start + dir * dist;
  mPortTriPointerTarget = result.IsValid() ? hitId : kInvalidUniqueId;
}

void CCompoundTargetReticle::PortTriUpdateScan(float dt, const CStateManager& mgr) {
  const CPlayer* player = mgr.GetPlayer(mPlayerIndex);
  if (mgr.GetPlayerState(mPlayerIndex)->GetCurrentVisor() != CPlayerState::kPV_Scan) {
    mPortTriScanSize = CVector3f::Zero();
    mPortTriScanCentre = mPortTriPointerPos;
    mPortTriScanLerp = 0.f;
    mPortTriScanTargetCount = 0;
    return;
  }

  const TUniqueId scanningId = player->GetScanningObject();
  const TUniqueId id = scanningId != kInvalidUniqueId ? scanningId : mPortTriScanTarget;
  int count = 0;
  TUniqueId list[1] = {kInvalidUniqueId};
  if (id != kInvalidUniqueId) {
    list[count++] = id;
  }
  const bool same = count == mPortTriScanTargetCount &&
                    (count == 0 || list[0] == mPortTriScanTargets[0]);
  if (!same) {
    if (count != 0) {
      mPortTriScanTargets[0] = list[0];
    }
    mPortTriScanTargetCount = count;
    const float t = mPortTriScanLerp;
    mPortTriScanPrevSize = mPortTriScanSize * (1.f - t) + mPortTriScanPrevSize * t;
    mPortTriScanPrevCentre = mPortTriScanCentre * (1.f - t) + mPortTriScanPrevCentre * t;
    mPortTriScanLerp = 1.f;
  } else {
    mPortTriScanLerp = rstl::max_val(0.f, mPortTriScanLerp - dt / kPortTriScanFadeTime);
  }

  const bool anyActor =
      mPortTriScanTargetCount != 0 &&
      TCastToConstPtr< CActor >(mgr.GetObjectById(mPortTriScanTargets[0])) != nullptr;
  if (mPreviousState == kRS_Scan && anyActor) {
    mPortTriScanFade = rstl::min_val(1.f, mPortTriScanFade + dt / kPortTriScanFadeTime);
  } else {
    mPortTriScanFade = rstl::max_val(0.f, mPortTriScanFade - dt / kPortTriScanFadeTime);
  }

  if (scanningId != kInvalidUniqueId) {
    mPortTriScanning = rstl::min_val(1.f, mPortTriScanning + dt);
  } else {
    mPortTriScanning = rstl::max_val(0.f, mPortTriScanning - dt);
  }

  const TUniqueId next = player->GetOrbitNextTargetId();
  if (next != mPortTriLastNextTarget) {
    mPortTriLastNextTarget = next;
    mPortTriScanTarget = PortTriResolveScanTarget(next, mgr);
  }
}

TUniqueId CCompoundTargetReticle::PortTriResolveScanTarget(TUniqueId id,
                                                           const CStateManager& mgr) {
  // MP1 also follows an actor's Play->Activate connection to a point of interest; Echoes has no
  // such object, so only an active scannable actor qualifies.
  const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(id));
  if (actor && actor->GetScannableObjectInfo() && actor->GetActive()) {
    return id;
  }
  return kInvalidUniqueId;
}

void CCompoundTargetReticle::PortTriUpdateScanBounds(const CStateManager& mgr) {
  if (mgr.GetPlayerState(mPlayerIndex)->GetCurrentVisor() != CPlayerState::kPV_Scan) {
    return;
  }
  CVector3f centre = mPortTriPointerPos;
  CVector3f size = CVector3f::Zero();
  const CActor* actor =
      mPortTriScanTargetCount != 0
          ? TCastToConstPtr< CActor >(mgr.GetObjectById(mPortTriScanTargets[0]))
          : nullptr;
  const CModelData* modelData = actor ? actor->GetModelData() : nullptr;
  if (modelData && !modelData->IsNull()) {
    // Starts from the inverted box (MP1 959e9458b); both corners are accumulated.
    CAABox accum = CAABox::MakeMaxInvertedBox();
    const CVector3f scale = modelData->GetScale();
    if (const CAnimData* animData = modelData->GetAnimationData()) {
      const CAABox local = animData->GetBoundingBox();
      const CTransform4f& xf = actor->GetTransform();
      const CVector3f p0 = xf * CVector3f::ByElementMultiply(scale, local.GetMinPoint());
      const CVector3f p1 = xf * CVector3f::ByElementMultiply(scale, local.GetMaxPoint());
      accum.AccumulateBounds(CVector3f(rstl::min_val(p0.GetX(), p1.GetX()),
                                       rstl::min_val(p0.GetY(), p1.GetY()),
                                       rstl::min_val(p0.GetZ(), p1.GetZ())));
      accum.AccumulateBounds(CVector3f(rstl::max_val(p0.GetX(), p1.GetX()),
                                       rstl::max_val(p0.GetY(), p1.GetY()),
                                       rstl::max_val(p0.GetZ(), p1.GetZ())));
    } else {
      // GetBounds(xf) already scales; Trilogy scales the extent again (MP1 fn_80139524 note).
      const CAABox b = modelData->GetBounds(actor->GetTransform());
      const CVector3f c = b.GetCenterPoint();
      const CVector3f half =
          CVector3f::ByElementMultiply((b.GetMaxPoint() - b.GetMinPoint()) * 0.5f, scale);
      accum.AccumulateBounds(c - half);
      accum.AccumulateBounds(c + half);
    }
    centre = accum.GetCenterPoint();
    const CAABox t =
        accum.GetTransformedAABox(mgr.GetCameraManager(mPlayerIndex)->GetCurrentCameraTransform(mgr, true));
    size = t.GetMaxPoint() - t.GetMinPoint();
    // Behind the camera: fall back to the pointer and a unit size.
    const CVector3f proj =
        mgr.GetCameraManager(mPlayerIndex)->GetCurrentCamera(mgr, true)->ConvertToScreenSpace(centre);
    if (proj.GetZ() >= 1.f) {
      centre = mPortTriPointerPos;
      size = CVector3f(1.f, 1.f, 1.f);
    }
  }
  mPortTriScanCentre = centre;
  mPortTriScanSize = size;
}

// Corner pulled sideways in the camera's XZ plane onto the NDC unit disc (MP1 fn_80239DB0).
static CVector3f PortTriProjectScanCorner(const CVector3f& world, const CStateManager& mgr,
                                          int playerIndex, float radius) {
  const CGameCamera& cam = *mgr.GetCameraManager(playerIndex)->GetCurrentCamera(mgr, true);
  const CTransform4f camXf = mgr.GetCameraManager(playerIndex)->GetCurrentCameraTransform(mgr, true);
  const CVector3f view = camXf.TransposeRotate(world - camXf.GetTranslation());
  const CVector3f ndc = cam.GetPerspectiveMatrix().MultiplyOneOverW(view);
  const CVector3f flat(ndc.GetX(), ndc.GetY(), 0.f);
  if (flat.MagSquared() <= radius * radius) {
    return world;
  }
  const CVector3f c(view.GetZ(), 0.f, -view.GetX());
  const float tangLen = c.Magnitude();
  const CVector3f nc = c * (1.f / tangLen);
  const CVector3f n(nc.GetZ(), 0.f, -nc.GetX());
  const float flatLen = flat.Magnitude();
  const float k = tangLen * ((flatLen - radius) / flatLen);
  return camXf * (view + k * n);
}

// MP1 fn_80238DD4 analogue; Echoes' is 0x8025de8c. Same constants (1.8, 0.85, 120, +-100) but
// Echoes' draw has an extra 6-step loop rotated -60 degrees per step that is not ported.
void CCompoundTargetReticle::PortTriDrawScan(const CMatrix3f& rotation,
                                             const CStateManager& mgr) const {
  if (mNoDrawTicks > 0 ||
      mgr.GetPlayerState(mPlayerIndex)->GetCurrentVisor() != CPlayerState::kPV_Scan ||
      mPreviousState != kRS_Scan) {
    return;
  }
  const_cast< TCachedToken< CModel >& >(mPortTriScanRing).TryCache();
  const_cast< TCachedToken< CModel >& >(mPortTriScanBracket).TryCache();
  const_cast< TCachedToken< CModel >& >(mPortTriScanProgress).TryCache();
  const CModel* ring = mPortTriScanRing.GetObject();
  const CModel* bracket = mPortTriScanBracket.GetObject();
  if (!ring || !bracket || !mPortTriScanProgress.GetObject()) {
    return;
  }

  const CPlayer* player = mgr.GetPlayer(mPlayerIndex);
  const bool scanning = player->GetScanningObject() != kInvalidUniqueId;
  const float intensity = scanning ? 1.f - player->PortTriOrbitFade()
                                   : 1.f - player->PortTriOrbitFade();

  const CCameraManager* camMgr = mgr.GetCameraManager(mPlayerIndex);
  const CTransform4f camXf = camMgr->GetCurrentCameraTransform(mgr, true);
  const CGameCamera& cam = *camMgr->GetCurrentCamera(mgr, true);
  float ringDist = 224.f / static_cast< float >(player->GetTweakPlayer()->PortTriTargetingHalfHeight());
  ringDist /= static_cast< float >(tan(cam.GetRenderFov() * 0.5f * (1.f / 360.f) * (2.f * M_PIF)));
  const CVector3f ringPos = camXf.GetTranslation() + ringDist * PortTriPointerDir(camXf, cam);

  CColor ringColor =
      CColor::Lerp(kPortTriScanRingColor, kPortTriScanLockedColor, mPortTriScanFade);
  ringColor.SetAlpha(PortTriAlphaU8(intensity * ringColor.GetAlphau8()));
  // Brackets lerp the other way (MP1 ed42d564a): faint on the pointer, full once on a target.
  CColor bracketColor =
      CColor::Lerp(kPortTriScanLockedColor, kPortTriScanBracketColor, mPortTriScanFade);
  bracketColor.SetAlpha(PortTriAlphaU8(intensity * bracketColor.GetAlphau8()));

  const float t = mPortTriScanLerp;
  const float it = 1.f - t;
  CVector3f centre = mPortTriScanCentre * it + mPortTriScanPrevCentre * t;
  // Both centres were placed at the tick (an actor's drawn pose, or a point on the pointer ray from
  // the tick camera) but are drawn through the presented camera: carry them as the lock-on groups
  // are (PortPresentGroup), else the bracket steps with the sim (MP1 689c6cb1c, 54250c5bc).
  if (CCameraManager::GetPresentationInterpolation() >= 0.f) {
    const CActor* scanActor =
        mPortTriScanTargetCount != 0
            ? TCastToConstPtr< CActor >(mgr.GetObjectById(mPortTriScanTargets[0]))
            : nullptr;
    if (scanActor != nullptr) {
      centre = scanActor->PortPresentedPoint(centre);
    } else {
      centre = camXf * (camMgr->GetSimulationCameraTransform(mgr, true).GetInverse() * centre);
    }
  }
  const CVector3f size = mPortTriScanSize * it + mPortTriScanPrevSize * t;
  const float s = CalculateClampedScale(centre, 1.f, 120.f, 120.f, mgr, mPlayerIndex);
  const float hx = 0.5f * rstl::max_val(0.f, 0.85f * size.GetX());
  const float hz = 0.5f * rstl::max_val(0.f, 0.85f * size.GetZ());
  const CVector3f halfExt(hx, 0.f, -hz);

  gpRender->SetModelMatrix(CTransform4f(rotation, ringPos) *
                           CTransform4f::Scale(1.8f * kPortTriScanRingScale));
  ring->Draw(CModelFlags::Additive(ringColor).DepthCompareUpdate(false, false));
  if (scanning) {
    if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(player->GetScanningObject()))) {
      if (const CScannableObjectInfo* info = actor->GetScannableObjectInfo()) {
        const float progress = const_cast< CPlayerState* >(mgr.GetPlayerState(mPlayerIndex))
                                   ->GetScanTime(info->GetScannableObjectId());
        PortTriDrawScanProgress(rotation, ringPos, progress, ringColor);
      }
    }
  }

  CVector3f corners[4];
  int n = 0;
  if (mPortTriScanFade > 0.f) {
    for (int i = 0; i < 4; ++i) {
      const float cx = (i & 1) ? -1.f : 1.f;
      const float cz = i < 2 ? -1.f : 1.f;
      const CVector3f world =
          centre + rotation * CVector3f(halfExt.GetX() * cx, 0.f, halfExt.GetZ() * cz);
      const CVector3f proj = PortTriProjectScanCorner(world, mgr, mPlayerIndex, 1.f);
      const CVector3f local = rotation.GetTranspose() * (proj - centre);
      corners[n++] = CVector3f(local.GetX() * cx, local.GetY(), local.GetZ() * cz);
    }
  }
  if (n == 4) {
    const float xA = rstl::max_val(rstl::min_val(corners[0].GetX(), corners[2].GetX()), -100.f);
    const float xB = rstl::max_val(rstl::min_val(corners[1].GetX(), corners[3].GetX()), -100.f);
    const float zA = rstl::min_val(rstl::max_val(corners[1].GetZ(), corners[0].GetZ()), 100.f);
    const float zB = rstl::min_val(rstl::max_val(corners[3].GetZ(), corners[2].GetZ()), 100.f);
    corners[0] = CVector3f(xA, 0.f, zA);
    corners[1] = CVector3f(xB, 0.f, zA);
    corners[2] = CVector3f(xA, 0.f, zB);
    corners[3] = CVector3f(xB, 0.f, zB);
  }

  const float invS = 1.f / s;
  CColor drawColor = bracketColor;
  drawColor.SetAlpha(PortTriAlphaU8((1.f - mPortTriScanning) * bracketColor.GetAlphau8()));
  for (int i = 0; i < 4; ++i) {
    const float cx = (i & 1) ? -1.f : 1.f;
    const float cz = i < 2 ? -1.f : 1.f;
    const CVector3f pos = n != 0 ? corners[i] * mPortTriScanFade : halfExt;
    gpRender->SetModelMatrix(CTransform4f(rotation * CMatrix3f::Scale(s), centre) *
                             CTransform4f::Scale(cx, 1.f, cz) *
                             CTransform4f::Translate(pos * invS));
    bracket->Draw(CModelFlags::Additive(drawColor).DepthCompareUpdate(false, false));
  }
}

namespace {
// fn_802685B8-style packed tweak colours (RRGGBBAA), MP1 values; not confirmed in Echoes' tweak.
CColor PortTriColor(unsigned rgba) {
  return CColor(uchar(rgba >> 24), uchar(rgba >> 16), uchar(rgba >> 8), uchar(rgba));
}

// Blend model with the alpha replaced (not modulated), as MP1's fn_80237B04/80237EC4/802383C4.
void PortTriDrawModel(const TCachedToken< CModel >& token, const CTransform4f& xf, CColor color,
                      float alpha) {
  const_cast< TCachedToken< CModel >& >(token).TryCache();
  if (const CModel* model = token.GetObject()) {
    color.SetAlpha(PortTriAlphaU8(255.f * alpha));
    gpRender->SetModelMatrix(xf);
    model->Draw(CModelFlags(CModelFlags::kT_Blend, 0, static_cast< CModelFlags::EFlags >(0), color));
  }
}

// The port's crosshair settings on Trilogy's aiming cursor, as on GC's: a custom colour replaces
// theirs, opacity scales their alpha (100%, the default, is retail) and size scales them under
// direct aim (MP1's PortTriXhairColor/Alpha/Size).
CColor PortTriXhairColor(const CColor& color) {
  float rgb[3];
  return PortOverlay::CrosshairColor(rgb) ? CColor(rgb[0], rgb[1], rgb[2], color.GetAlpha()) : color;
}
float PortTriXhairAlpha(float alpha) {
  return alpha * static_cast< float >(PortOverlay::CrosshairOpacity()) / 100.f;
}
float PortTriXhairSize() {
  return PortMouse::Aiming() ? static_cast< float >(PortOverlay::CrosshairSize()) / 100.f : 1.f;
}
} // namespace

// The point on the 1.25-depth plane in front of the camera along the ray to `world`
// (MP1 fn_8023AC48, no behind-camera guard).
CVector3f CCompoundTargetReticle::PortTriProjectNear(const CStateManager& mgr,
                                                     const CVector3f& world) const {
  const CTransform4f cam = mgr.GetCameraManager(mPlayerIndex)->GetCurrentCameraTransform(mgr, true);
  const CVector3f dir = (world - cam.GetTranslation()).AsNormalized();
  return cam.GetTranslation() + dir * (1.25f / CVector3f::Dot(dir, cam.GetForward()));
}

void CCompoundTargetReticle::PortTriUpdateCombat(float dt, const CStateManager& mgr) {
  using PortTriReticle::Clamp;
  const CPlayer& player = *mgr.GetPlayer(mPlayerIndex);
  const CTransform4f cam = mgr.GetCameraManager(mPlayerIndex)->GetCurrentCameraTransform(mgr, true);
  const CVector3f eye = cam.GetTranslation();
  const float d = (mPortTriPointerPos - player.GetTranslation()).Magnitude();
  mPortTriAimArmScale = PortTriReticle::ScaleStep(mPortTriAimArmScale, PortTriReticle::ArmResponse(d));
  mPortTriAimCenterScale =
      PortTriReticle::ScaleStep(mPortTriAimCenterScale, PortTriReticle::CentreResponse(d));

  // A hostile (CPatterned, or a collision actor owned by one) under the pointer.
  const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mPortTriPointerTarget));
  bool ai = actor && TCastToConstPtr< CPatterned >(actor) != nullptr;
  if (!ai) {
    if (const CCollisionActor* collision = TCastToConstPtr< CCollisionActor >(actor)) {
      ai = TCastToConstPtr< CPatterned >(mgr.GetObjectById(collision->GetOwnerId())) != nullptr;
    }
  }
  const bool qualified = actor && actor->GetActive() && ai;
  mPortTriAimTarget = Clamp(mPortTriAimTarget + (qualified ? dt : -dt) / .25f, 0.f, 1.f);
  CColor armIdle = PortTriColor(0x00b328ff), armTarget = PortTriColor(0xa82a00ff);
  CColor centerIdle = PortTriColor(0x00ff00ff), centerTarget = PortTriColor(0xff0000ff);
  if (mPreviousState == kRS_Dark) {
    armIdle = centerIdle = PortTriColor(0xff3089ff);
    armTarget = centerTarget = PortTriColor(0xff2f28ff);
  } else if (mPreviousState == kRS_Echo) {
    armIdle = centerIdle = PortTriColor(0x93e9ffff);
    armTarget = centerTarget = PortTriColor(0xff6b60ff);
  }
  mPortTriAimArmColor = CColor::Lerp(armIdle, armTarget, mPortTriAimTarget);
  mPortTriAimCenterColor = CColor::Lerp(centerIdle, centerTarget, mPortTriAimTarget);
  mPortTriAimArmAlpha =
      (1.f - mPortTriAimTarget) * (200.f / 255.f) + mPortTriAimTarget * (160.f / 255.f);
  mPortTriAimCenterAlpha = 1.f;
  const CPlayerGun* gun = player.GetPlayerGun();
  if ((gun && gun->GetGunHolsterState() == CPlayerGunBase::kGHS_Holstered) ||
      !PortTriReticle::AimVisible(mPreviousState)) {
    mPortTriAimArmAlpha = mPortTriAimCenterAlpha = 0.f;
  }

  // Next target: projection and interpolation over 0.25 s.
  const TUniqueId next = player.GetOrbitNextTargetId();
  const CActor* nextActor = TCastToConstPtr< CActor >(mgr.GetObjectById(next));
  const CVector3f world = nextActor ? nextActor->GetOrbitPosition(mgr) : mPortTriNextWorld;
  const CVector3f projected = PortTriProjectNear(mgr, world);
  if (next != kInvalidUniqueId && next != mPortTriNext) {
    if (mPortTriNextAlpha == 0.f) {
      mPortTriNextFrom = projected;
    } else {
      mPortTriNextFrom = mPortTriNextWorld;
      mPortTriNextMoving = true;
      mPortTriNextTime = 0.f;
    }
  }
  mPortTriNextAlpha = Clamp(
      mPortTriNextAlpha + (next != kInvalidUniqueId ? dt : -dt) * (100.f / 255.f) / .25f, 0.f,
      100.f / 255.f);
  if (mPortTriNextMoving) {
    mPortTriNextTime = Clamp(mPortTriNextTime + dt, 0.f, .25f);
    if (mPortTriNextTime < .25f) {
      const float q = mPortTriNextTime / .25f;
      mPortTriNextPos = PortTriProjectNear(mgr, mPortTriNextFrom) * (1.f - q) + projected * q;
      mPortTriNextWorld =
          eye + (mPortTriNextPos - eye).AsNormalized() * (world - eye).Magnitude();
    } else {
      mPortTriNextMoving = false;
      mPortTriNextPos = projected;
      mPortTriNextWorld = world;
    }
  } else {
    mPortTriNextPos = projected;
    mPortTriNextWorld = world;
  }
  mPortTriNext = next;
  mPortTriNextAngle += dt * 120.f * (M_PIF / 180.f);

  // Lock: a release is a lock that ended without a damage orbit break.
  const TUniqueId locked = player.GetOrbitTargetId();
  const bool has = locked != kInvalidUniqueId;
  const bool release = !has && mPortTriLocked != kInvalidUniqueId &&
                       !IsDamageOrbit(player.GetOrbitRequest());
  bool hideArms = false;
  if (has) {
    if (!IsGrappleTarget(mPortTriNext, mgr)) {
      mPortTriNext = kInvalidUniqueId;
      mPortTriNextAlpha = 0.f;
    }
    hideArms = IsGrappleTarget(locked, mgr);
  }
  mPortTriLockPos = cam * CVector3f(0.f, 1.25f, 0.f);
  if (has && locked == mPortTriLocked) {
    mPortTriLockTime += dt;
  } else if (!has && mPortTriLocked != kInvalidUniqueId) {
    if (release && mPreviousState == kRS_Combat && !IsGrappleTarget(mPortTriLocked, mgr)) {
      mPortTriReleaseAlpha = 1.f;
    }
  } else {
    mPortTriLockTime = 0.f;
  }
  mPortTriLockColor = PortTriColor(0xa82a00ff);
  if (const CScriptGrapplePoint* grapple =
          TCastToConstPtr< CScriptGrapplePoint >(mgr.GetObjectById(locked))) {
    if (grapple->GetActive()) {
      mPortTriLockColor = PortTriColor(0xffca28ff);
    }
  }
  mPortTriLockFade = Clamp(mPortTriLockFade + (has ? dt : -dt), 0.f, .25f);
  mPortTriReleaseFade = Clamp(mPortTriReleaseFade + (has || release ? dt : -dt), 0.f, .125f);
  mPortTriReleaseAlpha = Clamp(
      mPortTriReleaseAlpha -
          (player.GetMorphballTransitionState() == CPlayer::kMS_Unmorphed ? dt * .65f / 1.5f : 1.f),
      0.f, .65f);

  // Bracket angle follows the player's yaw; tech spins at 45 deg/s once settled.
  const CVector3f forward = player.GetTransform().GetForward();
  const CVector3f h = CVector3f(forward.GetX(), forward.GetY(), 0.f).AsNormalized();
  float yaw = acosf(Clamp(h.GetY(), -1.f, 1.f));
  if (h.GetX() < 0.f) {
    yaw = 2.f * M_PIF - yaw;
  }
  yaw = fmodf(yaw, 2.f * M_PIF);
  if ((mPortTriLockTime - .25f) / .25f >= 1.f) {
    mPortTriBracketAngle += yaw - mPortTriPrevYaw;
  } else {
    mPortTriBracketAngle = 0.f;
  }
  mPortTriPrevYaw = yaw;
  if (PortTriReticle::LockTechSpins(mPortTriLockTime)) {
    mPortTriTechAngle += 45.f * dt * (M_PIF / 180.f);
  } else {
    mPortTriTechAngle = 0.f;
  }
  if (!PortTriReticle::LockVisible(mPreviousState)) {
    hideArms = true;
  }
  if (hideArms) {
    mPortTriLockAlpha = 0.f;
  } else {
    mPortTriLockAlpha = PortTriReticle::LockArmAlpha(mPortTriLockAlpha, mPortTriLockTime, dt);
  }
  mPortTriLocked = locked;
}

void CCompoundTargetReticle::PortTriDrawNext(const CMatrix3f& rotation) const {
  if (mNoDrawTicks > 0) {
    return;
  }
  for (int group = 0; group < 2; ++group) {
    const float alpha = group == 0 ? mPortTriNextAlpha : mPortTriReleaseAlpha;
    if (alpha <= 0.f) {
      continue;
    }
    const CTransform4f base(rotation, group == 0 ? mPortTriNextPos : mPortTriLockPos);
    for (int i = 0; i < 3; ++i) {
      const float angle = (group == 0 ? mPortTriNextAngle : 0.f) + i * 120.f * (M_PIF / 180.f);
      PortTriDrawModel(mPortTriLockArm,
                       base * CTransform4f::RotateY(CRelAngle::FromRadians(angle)) *
                           CTransform4f::Scale(.35f) * CTransform4f::Translate(0.f, 0.f, .46f),
                       PortTriColor(0xb6e6ffff), alpha);
    }
  }
}

void CCompoundTargetReticle::PortTriDrawLockAt(const CMatrix3f& rotation, const CVector3f& pos,
                                               float lockTime, const CColor& color,
                                               float alpha) const {
  const CTransform4f base(rotation, pos);
  const CColor blue = PortTriColor(0x89d6ffff);
  for (int i = 0; i < 3; ++i) {
    const CTransform4f xf = base * CTransform4f::RotateY(CRelAngle::FromDegrees(i * 120.f));
    PortTriDrawModel(mPortTriLockArm,
                     xf * CTransform4f::Scale(.35f) * CTransform4f::Translate(0.f, 0.f, .46f),
                     color, alpha);
    PortTriDrawModel(mPortTriLockBase,
                     xf * CTransform4f::Scale(PortTriReticle::LockBaseScale(lockTime)), blue,
                     50.f / 255.f);
  }
  PortTriDrawModel(mPortTriLockBrackets,
                   base * CTransform4f::RotateY(CRelAngle::FromRadians(mPortTriBracketAngle)) *
                       CTransform4f::Scale(.35f),
                   blue, PortTriReticle::LockBracketAlpha(lockTime));
  if (PortTriReticle::LockTechTime(lockTime) > 0.f) {
    PortTriDrawModel(mPortTriLockTech,
                     base * CTransform4f::RotateY(CRelAngle::FromRadians(mPortTriTechAngle)) *
                         CTransform4f::Scale(.35f),
                     blue, 50.f / 255.f);
  }
}

void CCompoundTargetReticle::PortTriDrawLocked(const CMatrix3f& rotation) const {
  if (mNoDrawTicks > 0 || mPortTriLocked == kInvalidUniqueId || mPortTriLockAlpha <= 0.f) {
    return;
  }
  PortTriDrawLockAt(rotation, mPortTriLockPos, mPortTriLockTime, mPortTriLockColor,
                    mPortTriLockAlpha);
}

void CCompoundTargetReticle::PortTriDrawAiming(const CMatrix3f& rotation,
                                               const CStateManager& mgr) const {
  // The Remastered and GameCube styles replace the cursor (DrawCrosshairs draws them).
  if (mNoDrawTicks > 0 || mPortTriAimArmAlpha <= 0.f ||
      PortOverlay::CrosshairStyle() != PortOverlay::kCrosshairStyle_Classic) {
    return;
  }
  const CCameraManager* camMgr = mgr.GetCameraManager(mPlayerIndex);
  const CTransform4f cam = camMgr->GetCurrentCameraTransform(mgr, true);
  const CVector3f dir = PortTriPointerDir(cam, *camMgr->GetCurrentCamera(mgr, true));
  const CVector3f pos = cam.GetTranslation() + dir * (1.25f / CVector3f::Dot(dir, cam.GetForward()));
  const CTransform4f base = CTransform4f(rotation, pos) * CTransform4f::Scale(PortTriXhairSize());
  PortTriDrawModel(mPortTriAimCenter, base * CTransform4f::Scale(.45f * mPortTriAimCenterScale),
                   PortTriXhairColor(mPortTriAimCenterColor),
                   PortTriXhairAlpha(mPortTriAimCenterAlpha));
  for (int i = 0; i < 3; ++i) {
    PortTriDrawModel(mPortTriAimArm,
                     base * CTransform4f::RotateY(CRelAngle::FromDegrees(i * 120.f)) *
                         CTransform4f::Scale(.45f) * CTransform4f::Translate(0.f, 0.f, .25f) *
                         CTransform4f::Scale(1.f, 1.f, mPortTriAimArmScale),
                     PortTriXhairColor(mPortTriAimArmColor),
                     PortTriXhairAlpha(mPortTriAimArmAlpha));
  }
}

// The six progress segments of the scan ring (rs5mp2_p.dol 0x8025e438): segment i rotated
// -60*i degrees about Y, scaled 1.8 like the ring; colour and blend are MP1-derived guesses.
void CCompoundTargetReticle::PortTriDrawScanProgress(const CMatrix3f& rotation,
                                                     const CVector3f& pos, float progress,
                                                     const CColor& color) const {
  const CModel* model = mPortTriScanProgress.GetObject();
  if (!model) {
    return;
  }
  for (int i = 0; i < 6; ++i) {
    const float v = PortTriReticle::ScanSegment(progress, i);
    if (v <= 0.f) {
      continue;
    }
    gpRender->SetModelMatrix(CTransform4f(rotation, pos) *
                             CTransform4f::RotateY(CRelAngle::FromDegrees(-60.f * i)) *
                             CTransform4f::Scale(1.8f * kPortTriScanRingScale));
    model->Draw(CModelFlags::Additive(color.WithAlphaOf(v * color.GetAlpha()))
                    .DepthCompareUpdate(false, false));
  }
}

#endif
EReticleState CCompoundTargetReticle::GetDesiredReticleState(const CStateManager& mgr) const {
  switch (mgr.GetPlayerState(mPlayerIndex)->GetCurrentVisor()) {
  case CPlayerState::kPV_Scan:
    return kRS_Scan;
  case CPlayerState::kPV_Echo:
    return kRS_Echo;
  case CPlayerState::kPV_Combat:
    return kRS_Combat;
  case CPlayerState::kPV_Dark:
    return kRS_Dark;
  default:
    return kRS_Combat;
  }
}

void CCompoundTargetReticle::Update(float dt, const CStateManager& mgr) {
#ifdef TARGET_PC
  // Port: the 2-frame hide counts game ticks, not drawn frames (see Draw). Decrementing at the top
  // of Update, before the state transition below sets it to 2, keeps the 60 FPS sequence of the
  // retail per-Draw decrement: tick N sets 2 and the draw after it hides (retail 2->1), tick N+1
  // makes it 1 and its draw hides (retail 1->0), tick N+2 makes it 0 and its draw shows. The
  // transition's `mNoDrawTicks <= 0` test also sees the same values on the same ticks.
  if (mNoDrawTicks > 0) {
    --mNoDrawTicks;
  }
#endif
  // Orientation slerp
  CRelAngle angle = mLaggingOrientation.AngleFrom(mLeadingOrientation);
  float angleDeg = angle.AsDegrees();
  bool extreme = false;
  if (angleDeg < 0.1f || angleDeg > 45.f) {
    extreme = true;
  }
  float t;
  if (extreme) {
    t = 1.f;
  } else {
    float lagSpeed = gpTweakTargeting->GetAngularLagSpeed();
    t = rstl::min_val(1.f, lagSpeed * dt / angleDeg);
  }
  mLaggingOrientation = t == 1.f ? mLeadingOrientation
                                 : CQuaternion::Slerp(mLaggingOrientation, mLeadingOrientation, t);

  // Target positions
  mTargetPosition = CalculateOrbitZoneReticlePosition(mgr, false);
  mLaggingTargetPosition = CalculateOrbitZoneReticlePosition(mgr, true);

  // Sub-updates
  UpdateCurrLockOnGroup(dt, mgr);
  UpdateNextLockOnGroup(dt, mgr);
  UpdateOrbitZoneGroup(dt, mgr);
#ifdef TARGET_PC
  if (mPortTri) {
    PortTriUpdateScanPointer(mgr);
    PortTriUpdateScan(dt, mgr);
    PortTriUpdateScanBounds(mgr);
    PortTriUpdateCombat(dt, mgr);
  }
  if (PortOverlay::CrosshairStyle() == PortOverlay::kCrosshairStyle_Remastered) {
    PortUpdateRemasteredCrosshair(dt, mgr);
  }
#endif

  // Reticle state transitions
  EReticleState desiredState = GetDesiredReticleState(mgr);
  if (desiredState != mPreviousState && mPreviousState == mNextState) {
    mNextState = desiredState;
    mNoDrawTicks = 2;
  }

  if (mPreviousState != mNextState && mNoDrawTicks <= 0) {
    mPreviousState = mNextState;
    bool combat = false;
    bool scan = false;
    switch (mNextState) {
    case kRS_Combat:
    case kRS_Echo:
    case kRS_Dark:
      combat = true;
      break;
    case kRS_Scan:
      scan = true;
      break;
    default:
      break;
    }

    if (combat) {
      mSeeker.Lock();
      mLockConfirm.Lock();
      mTargetFlower.Lock();
      mMissileBracket.Lock();
      mInnerBeamIcon.Lock();
      mLockFire.Lock();
      mLockDagger.Lock();
      mChargeTickFirst.Lock();
      mChargeGauge.mModel.Lock();
    } else {
      mSeeker.Unlock();
      mLockConfirm.Unlock();
      mTargetFlower.Unlock();
      mMissileBracket.Unlock();
      mInnerBeamIcon.Unlock();
      mLockFire.Unlock();
      mLockDagger.Unlock();
      mChargeTickFirst.Unlock();
      mChargeGauge.mModel.Unlock();
    }
    if (scan) {
      mGrapple.Unlock();
      mScanTargetCenter.Lock();
      mScanTargetLeft.Lock();
      mScanTargetRight.Lock();
    } else {
      mGrapple.Lock();
      mScanTargetCenter.Unlock();
      mScanTargetLeft.Unlock();
      mScanTargetRight.Unlock();
    }
    for (rstl::vector< SOuterItemInfo >::iterator it = mOuterBeamIconSquares.begin();
         it != mOuterBeamIconSquares.end(); ++it) {
      if (combat) {
        it->mModel.Lock();
      } else {
        it->mModel.Unlock();
      }
    }
  }

  // Charge gauge / fully charged
  const CPlayer* player = mgr.GetPlayer(mPlayerIndex);
  bool fullyCharged = player->GetPlayerState()->GetChargeBeamFactor() >= 1.f;
  if (fullyCharged != mFullyCharged) {
    mFullyCharged = fullyCharged;
  }
  if (mFullyCharged) {
    mFullChargeFadeTimer =
        rstl::min_val(gpTweakTargeting->GetChargeGaugeGlowTime(),
                      mFullChargeFadeTimer + dt / gpTweakTargeting->GetChargeGaugeGlowTime());
  } else {
    mFullChargeFadeTimer =
        rstl::max_val(0.f, mFullChargeFadeTimer - dt / gpTweakTargeting->GetChargeGaugeGlowTime());
  }

  // Missile active state
  bool missileActive = player->GetPlayerGun()->GetMissileMode();
  if (missileActive != mMissileActive) {
    if (mMissileBracketTimer != 0.f) {
      mMissileBracketTimer = FLT_EPSILON - mMissileBracketTimer;
    } else {
      mMissileBracketTimer = FLT_EPSILON;
    }
    mMissileActive = missileActive;
  }

  // Beam change
  CPlayerState::EBeamId beam = player->GetPlayerGun()->GetPrimaryWeaponId();
  if (beam != mBeam) {
    mChargeGaugeOvershootTimer = gpTweakTargeting->GetOuterBeamIconSwitchTime();
    for (int i = 0; i < 9; ++i) {
      SOuterItemInfo& icon = mOuterBeamIconSquares[i];
      float baseAngle = CMath::ClampRadians(gpTweakTargeting->GetOuterBeamIconAngle(beam, i));
      CRelAngle offshootAngleDelta = CRelAngle::FromRadians(baseAngle - icon.mRotationAngle);
      if (i % 2 == 1) {
        offshootAngleDelta =
            offshootAngleDelta.AsRadians() > 0.f
                ? CRelAngle::FromRadians(-1.f * (M_2PIF - offshootAngleDelta.AsRadians()))
                : CRelAngle::FromRadians(M_2PIF + offshootAngleDelta.AsRadians());
      }
      icon.mOffshootBaseAngle = icon.mRotationAngle;
      icon.mOffshootAngleDelta = offshootAngleDelta.AsRadians();
      icon.mBaseAngle = baseAngle;
    }

    float chargeBaseAngle = CMath::ClampRadians(gpTweakTargeting->GetChargeGaugeAngle(beam));
    bool odd = rand() % 2 == 1;
    CRelAngle chargeOffshootAngleDelta =
        CRelAngle::FromRadians(chargeBaseAngle - mChargeGauge.mRotationAngle);
    if (odd) {
      chargeOffshootAngleDelta =
          chargeOffshootAngleDelta.AsRadians() > 0.f
              ? CRelAngle::FromRadians(-1.f * (M_2PIF - chargeOffshootAngleDelta.AsRadians()))
              : CRelAngle::FromRadians(M_2PIF + chargeOffshootAngleDelta.AsRadians());
    }
    mChargeGauge.mOffshootBaseAngle = mChargeGauge.mRotationAngle;
    mChargeGauge.mOffshootAngleDelta = chargeOffshootAngleDelta.AsRadians();
    mChargeGauge.mBaseAngle = chargeBaseAngle;
    mBeam = beam;
    mLockOnTimer = 0.f;
  }

  // Beam shot / lock fire
  const CPlayerGun* gun = player->GetPlayerGun();
  if (gun->GetFiring() & 0x1) {
    if (!mBeamShot) {
      mLockFireTimer = gpTweakTargeting->GetLockFireAnimTime();
    }
    mBeamShot = true;
  } else {
    mBeamShot = false;
  }

  // Missile shot / missile bracket scale
  if (gun->GetFiring() & 0x2) {
    if (!mMissileShot) {
      mMissileBracketScaleTimer = gpTweakTargeting->GetMissileBracketMissileFireAnimTime();
    }
    mMissileShot = true;
  } else {
    mMissileShot = false;
  }

  // Grapple point tracking
  const CScriptGrapplePoint* grapplePoint =
      TCastToConstPtr< CScriptGrapplePoint >(mgr.GetObjectById(mNextTargetId));
  if (grapplePoint != nullptr) {
    TUniqueId gpId = grapplePoint->GetUniqueId();
    if (gpId != mGrapplePointA) {
      float tmp;
      if (gpId == mGrapplePointB) {
        tmp = rstl::max_val(FLT_EPSILON, mGrapplePointFactorB);
      } else {
        tmp = FLT_EPSILON;
      }
      mGrapplePointB = mGrapplePointA;
      mGrapplePointFactorB = mGrapplePointFactorA;
      mGrapplePointFactorA = tmp;
      mGrapplePointA = gpId;
    }
  } else {
    if (mGrapplePointA != kInvalidUniqueId) {
      mGrapplePointB = mGrapplePointA;
      mGrapplePointFactorB = mGrapplePointFactorA;
      mGrapplePointFactorA = 0.f;
      mGrapplePointA = kInvalidUniqueId;
    }
  }

  // Grapple point interpolation timers
  if (mGrapplePointFactorA > 0.f) {
    mGrapplePointFactorA = rstl::min_val(1.f, mGrapplePointFactorA + dt / 0.5f);
  }
  if (mGrapplePointFactorB > 0.f) {
    mGrapplePointFactorB = rstl::max_val(0.f, mGrapplePointFactorB - dt / 0.5f);
    if (mGrapplePointFactorB == 0.f) {
      mGrapplePointB = kInvalidUniqueId;
    }
  }

  // Xray/seeker angle updates
  mFlowerRotationAngle = CMath::ClampRadians(
      mFlowerRotationAngle +
      CRelAngle::FromDegrees(dt * gpTweakTargeting->GetXRayReticleRotationRate()).AsRadians());
  mSeekerRotationAngle = CMath::ClampRadians(
      mSeekerRotationAngle +
      CRelAngle::FromDegrees(dt * gpTweakTargeting->GetSeekerZRotationRate()).AsRadians());
}

void CCompoundTargetReticle::UpdateCurrLockOnGroup(float dt, const CStateManager& mgr) {
  const CPlayer* player = mgr.GetPlayer(mPlayerIndex);
  const TUniqueId targetId = player->GetOrbitTargetId();

  if (targetId != mTargetId) {
    if (mTargetId != targetId && targetId != kInvalidUniqueId) {
      if (TCastToConstPtr< CScriptGrapplePoint >(mgr.GetObjectById(targetId))) {
        CSfxManager::SfxStart(SFXui_x_graplock_00_oneshot, 127,
                              player->GetSoundPan(CPlayer::kMSP_4));
      } else {
        CSfxManager::SfxStart(mgr.ReturnFirstIfSingleElseSecond(SFXui_x_lockon_00_oneshot,
                                                                SFXui2_x_lockon_00_oneshot),
                              127, player->GetSoundPan(CPlayer::kMSP_4));
      }
    }

    mTargetVulnerability = CDamageVulnerability::ImmuneVulnerabilty();
    mVulnerabilityTarget = kInvalidUniqueId;

    if (kInvalidUniqueId == targetId) {
      CPlayer::EPlayerOrbitRequest orbitBrokenType = player->GetOrbitRequest();
      mCurrentGroupA = mCurrentGroupInterpolated;
      mCurrentGroupA.SetIsOrbitZoneIdlePosition(false);
      mCurrentGroupB.SetFactor(0.f);
      mCurrentGroupDuration =
          IsDamageOrbit(orbitBrokenType) ? 0.65f : gpTweakTargeting->GetCurrLockOnEnterDuration();
    } else {
      mCurrentGroupA = mCurrentGroupInterpolated;
      mCurrentGroupA.SetIsOrbitZoneIdlePosition(false);
      if (mTargetId == kInvalidUniqueId) {
        mCurrentGroupA.SetTargetId(targetId);
      }
      float scale =
          IsGrappleTarget(targetId, mgr) ? gpTweakTargeting->GetGrappleMinClampScale() : 1.f;
      mCurrentGroupB =
          CTargetReticleRenderState(targetId, 1.f, CVector3f::Zero(), 1.f, scale, false);
      mCurrentGroupDuration = (kInvalidUniqueId == mTargetId)
                                  ? gpTweakTargeting->GetCurrLockOnExitDuration()
                                  : gpTweakTargeting->GetCurrLockOnSwitchDuration();
      if (targetId != mgr.GetBossId()) {
        if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(targetId))) {
          if (const CScannableObjectInfo* info = actor->GetScannableObjectInfo()) {
            if (const_cast< CPlayerState* >(player->GetPlayerState())
                    ->GetScanTime(info->GetScannableObjectId()) >= 0.99999988f) {
              mTargetVulnerability = *actor->GetDamageVulnerability();
              mVulnerabilityTarget = actor->GetUniqueId();
              mTargetHealth = 0.f;
              mTargetHealthShadow = 0.f;
            }
          }
        }
      }
    }

    mCurrentGroupTimer = mCurrentGroupDuration;
    mTargetId = targetId;
  }

  if (mVulnerabilityTarget != kInvalidUniqueId) {
    if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mVulnerabilityTarget))) {
      if (actor->GetHealthInfo() && !close_enough(actor->GetHealthInfo()->GetInitialHP(), 0.f)) {
        float health = rstl::max_val(0.f, actor->GetHealthInfo()->GetHP());
        float initialHealth = rstl::max_val(0.f, actor->GetHealthInfo()->GetInitialHP());
        if (mTargetHealth > initialHealth) {
          mTargetHealth = health;
        }
        if (mTargetHealthShadow > initialHealth) {
          mTargetHealthShadow = health;
        }
        if (mTargetHealth > health) {
          mTargetHealth = rstl::max_val(
              health,
              mTargetHealth - initialHealth * dt / gpTweakTargeting->GetHealthMeterLagDrainTime());
        } else {
          mTargetHealth = rstl::min_val(
              health,
              mTargetHealth + initialHealth * dt / gpTweakTargeting->GetHealthMeterLagFillTime());
        }
        if (mTargetHealthShadow > mTargetHealth) {
          mTargetHealthShadow = rstl::max_val(
              mTargetHealth,
              mTargetHealthShadow -
                  initialHealth * dt / gpTweakTargeting->GetHealthMeterShadowDrainTime());
        } else {
          mTargetHealthShadow = mTargetHealth;
        }
      }
    }
  }

  if (mCurrentGroupTimer > 0.f) {
    UpdateTargetParameters(mCurrentGroupA, mgr);
    UpdateTargetParameters(mCurrentGroupB, mgr);
    mCurrentGroupTimer = rstl::max_val(0.f, mCurrentGroupTimer - dt);
    CTargetReticleRenderState::InterpolateWithClamp(
        mCurrentGroupA, mCurrentGroupInterpolated, mCurrentGroupB,
        1.f - mCurrentGroupTimer / mCurrentGroupDuration);
  } else {
    UpdateTargetParameters(mCurrentGroupInterpolated, mgr);
  }

  if (mMissileBracketTimer != 0.f &&
      mMissileBracketTimer < gpTweakTargeting->GetMissileBracketOpenHolsterTime()) {
    if (mMissileBracketTimer < 0.f) {
      mMissileBracketTimer = rstl::min_val(mMissileBracketTimer + dt, 0.f);
    } else {
      mMissileBracketTimer = rstl::min_val(mMissileBracketTimer + dt,
                                           gpTweakTargeting->GetMissileBracketOpenHolsterTime());
    }
  }

  if (mChargeGaugeOvershootTimer > 0.f) {
    mChargeGaugeOvershootTimer = rstl::max_val(mChargeGaugeOvershootTimer - dt, 0.f);
    if (mChargeGaugeOvershootTimer == 0.f) {
      for (int i = 0; i < 9; ++i) {
        mOuterBeamIconSquares[i].mRotationAngle = mOuterBeamIconSquares[i].mBaseAngle;
      }
      mChargeGauge.mRotationAngle = mChargeGauge.mBaseAngle;
      mLockOnTimer = FLT_EPSILON;
    } else {
      float offshoot = offshoot_func(mOvershootOffsetHalf, mPremultipliedOvershootOffset,
                                     1.f - mChargeGaugeOvershootTimer /
                                               gpTweakTargeting->GetOuterBeamIconSwitchTime());
      for (int i = 0; i < 9; ++i) {
        SOuterItemInfo& item = mOuterBeamIconSquares[i];
        float angleDelta = offshoot * item.mOffshootAngleDelta;
        item.mRotationAngle = CMath::ClampRadians(angleDelta + item.mOffshootBaseAngle);
      }
      mChargeGauge.mRotationAngle = CMath::ClampRadians(
          mChargeGauge.mOffshootBaseAngle + offshoot * mChargeGauge.mOffshootAngleDelta);
    }
  }

  if (mLockOnTimer > 0.f && mLockOnTimer < gpTweakTargeting->GetInnerBeamIconOpenTime()) {
    mLockOnTimer = rstl::min_val(mLockOnTimer + dt, gpTweakTargeting->GetInnerBeamIconOpenTime());
  }

  if (mLockFireTimer > 0.f) {
    mLockFireTimer = rstl::max_val(0.f, mLockFireTimer - dt);
  }

  if (mMissileBracketScaleTimer > 0.f) {
    mMissileBracketScaleTimer = rstl::max_val(0.f, mMissileBracketScaleTimer - dt);
  }

  player = mgr.GetPlayer(mPlayerIndex);
  if (mPreviousState == kRS_Scan &&
      player->GetTargeting()->GetResolvedTargetId() != kInvalidUniqueId) {
    mScanBracketFactor =
        rstl::min_val(1.f, mScanBracketFactor + dt / gpTweakTargeting->GetScanLockTransitionTime());
  } else {
    mScanBracketFactor =
        rstl::max_val(0.f, mScanBracketFactor - dt / gpTweakTargeting->GetScanLockTransitionTime());
  }
  if (player->GetScanningObject() == kInvalidUniqueId) {
    mScanTargetFactor = rstl::min_val(1.f, 4.f * dt + mScanTargetFactor);
  } else {
    mScanTargetFactor = rstl::max_val(0.f, mScanTargetFactor - 4.f * dt);
  }
}

void CCompoundTargetReticle::UpdateNextLockOnGroup(float dt, const CStateManager& mgr) {
  const CPlayer* player = mgr.GetPlayer(mPlayerIndex);
  TUniqueId nextTargetId = player->GetOrbitNextTargetId();
  if (mPreviousState == kRS_Scan) {
    nextTargetId = kInvalidUniqueId;
  }

  if (nextTargetId != mNextTargetId) {
    if (kInvalidUniqueId == nextTargetId) {
      mNextGroupA = mNextGroupInterpolated;
      mNextGroupA.SetIsOrbitZoneIdlePosition(false);
      bool lag = mPreviousState == kRS_Echo || mPreviousState == kRS_Dark;
      mNextGroupB = CTargetReticleRenderState(
          kInvalidUniqueId, 1.f, lag ? mLaggingTargetPosition : mTargetPosition, 0.f, 1.f, true);
      mNextGroupDuration = gpTweakTargeting->GetNextLockOnExitDuration();
      mNextGroupTimer = mNextGroupDuration;
      mNextTargetId = kInvalidUniqueId;
    } else {
      mNextGroupA = mNextGroupInterpolated;
      mNextGroupA.SetIsOrbitZoneIdlePosition(false);
      float scale =
          IsGrappleTarget(nextTargetId, mgr) ? gpTweakTargeting->GetGrappleMinClampScale() : 1.f;
      mNextGroupB =
          CTargetReticleRenderState(nextTargetId, 1.f, CVector3f::Zero(), 1.f, scale, true);
      mNextGroupDuration = kInvalidUniqueId == mNextTargetId
                               ? gpTweakTargeting->GetNextLockOnEnterDuration()
                               : gpTweakTargeting->GetNextLockOnSwitchDuration();
      mNextGroupTimer = mNextGroupDuration;
      mNextTargetId = nextTargetId;
    }
  }

  if (mNextGroupTimer > 0.f) {
    UpdateTargetParameters(mNextGroupA, mgr);
    UpdateTargetParameters(mNextGroupB, mgr);
    mNextGroupTimer = rstl::max_val(0.f, mNextGroupTimer - dt);
    CTargetReticleRenderState::InterpolateWithClamp(mNextGroupA, mNextGroupInterpolated,
                                                    mNextGroupB,
                                                    1.f - mNextGroupTimer / mNextGroupDuration);
  } else {
    UpdateTargetParameters(mNextGroupInterpolated, mgr);
  }
}

void CCompoundTargetReticle::UpdateOrbitZoneGroup(float dt, const CStateManager& mgr) {
  if (mTargetId == kInvalidUniqueId && mNextTargetId != kInvalidUniqueId) {
    mNextTargetFade = rstl::min_val(2.f * dt + mNextTargetFade, 1.f);
  } else {
    mNextTargetFade = rstl::max_val(mNextTargetFade - 2.f * dt, 0.f);
  }

  const CPlayer* player = mgr.GetPlayer(mPlayerIndex);
  if (player->GetDrawCrosshairs() &&
      player->GetPlayerState()->GetCurrentVisor() != CPlayerState::kPV_Scan) {
    mCrosshairsDrawScale = rstl::min_val(
        mCrosshairsDrawScale + dt / gpTweakTargeting->GetCrosshairsFadeInOutTime(), 1.f);
  } else {
    mCrosshairsDrawScale = rstl::max_val(
        mCrosshairsDrawScale - dt / gpTweakTargeting->GetCrosshairsFadeInOutTime(), 0.f);
  }
}

void CCompoundTargetReticle::Draw(const CStateManager& mgr, bool hideLockOn) const {
  if (mgr.GetPlayer(mPlayerIndex)->GetMorphballTransitionState() == CPlayer::kMS_Unmorphed &&
      !mgr.GetCameraManager(mPlayerIndex)->IsInCinematicCamera()) {
    CTransform4f cameraXf =
        mgr.GetCameraManager(mPlayerIndex)->GetCurrentCameraTransform(mgr, true);
    CGraphics::SetViewPointMatrix(cameraXf);
    CMatrix3f rotation = cameraXf.BuildMatrix3f();
    CGraphics::SetCullMode(kCM_None);

#ifdef TARGET_PC
    // MP1's frame interpolation (HUD and 2D): the groups were placed at the tick's poses. While
    // smooth frames draw actors and the camera between ticks, carry a lock-on with its target to
    // where it is drawn and an idle (camera-placed) group with the camera, else they shake
    // against what they mark. Restored below.
    CTargetReticleRenderState& portCurr =
        const_cast< CTargetReticleRenderState& >(mCurrentGroupInterpolated);
    CTargetReticleRenderState& portNext =
        const_cast< CTargetReticleRenderState& >(mNextGroupInterpolated);
    const CVector3f portSavedCurr = portCurr.GetTargetPositionWorld();
    const CVector3f portSavedNext = portNext.GetTargetPositionWorld();
    PortPresentGroup(portCurr, mgr);
    PortPresentGroup(portNext, mgr);

#endif
    if (!hideLockOn) {
#ifdef TARGET_PC
      if (mPortTri) {
        PortTriDrawNext(rotation);
        PortTriDrawLocked(rotation);
      } else
#endif
      DrawCurrLockOnGroup(rotation, mgr);
      DrawSeeker(rotation, mgr);
      DrawCrosshairs(rotation, mgr);
      DrawScanTargetGroup(rotation, mgr);
      DrawNextLockOnGroup(rotation, mgr);
#ifdef TARGET_PC
      if (mPortTri) {
        PortTriDrawScan(rotation, mgr);
        PortTriDrawAiming(rotation, mgr);
      }
#endif
      DrawOrbitZoneGroup(rotation, mgr);
    }
#ifdef TARGET_PC
    portCurr.SetTargetPositionWorld(portSavedCurr);
    portNext.SetTargetPositionWorld(portSavedNext);
#endif
    DrawGrappleGroup(rotation, mgr, hideLockOn);
    CGraphics::SetCullMode(kCM_Front);
  }

#ifndef TARGET_PC
  if (mNoDrawTicks > 0) {
    --mNoDrawTicks;
  }
#endif
}

void CCompoundTargetReticle::DrawGrappleGroup(const CMatrix3f& rotation, const CStateManager& mgr,
                                              bool hideLockOn) const {
  if (mNoDrawTicks > 0) {
    return;
  }
  const_cast< TCachedToken< CModel >& >(mGrapple).TryCache();
  if (mGrapple.GetObject() == nullptr) {
    return;
  }
  if (mPreviousState == kRS_Scan) {
    return;
  }

  const rstl::list< CEntity* >& list = mgr.GetGrapplePointList();
  if (!mgr.GetPlayerState(mPlayerIndex)->HasPowerUp(CPlayerState::kIT_GrappleBeam)) {
    return;
  }
  float depthNear = CGraphics::GetDepthNear();
  float depthFar = CGraphics::GetDepthFar();
  CGraphics::SetDepthRange(0.125f, 1.f);

  if (hideLockOn) {
    for (rstl::list< CEntity* >::const_iterator it = list.begin(); it != list.end(); ++it) {
      const CScriptGrapplePoint* point = static_cast< const CScriptGrapplePoint* >(*it);
      if (point == nullptr || !point->GetActive() ||
          !(point->GetValidTargetPlayers() & (1 << mPlayerIndex))) {
        continue;
      }
      TAreaId areaId = point->GetCurrentAreaId();
      if (areaId != kInvalidAreaId) {
        const CGameArea& area = mgr.GetWorld()->GetAreaAlways(areaId);
        if (area.GetOcclusionState() != CGameArea::kOS_Visible) {
          continue;
        }
      }

      float factor = 0.f;
      TUniqueId id = point->GetUniqueId();
      if (id == mGrapplePointA) {
        factor = mGrapplePointFactorA;
      } else if (id == mGrapplePointB) {
        factor = mGrapplePointFactorB;
      }
      if (close_enough(factor, 0.f, 0.00001f)) {
        DrawGrapplePoint(*point, factor, mgr, rotation, true);
      }
    }
  } else {
    const CScriptGrapplePoint* pointA =
        TCastToConstPtr< CScriptGrapplePoint >(mgr.GetObjectById(mGrapplePointA));
    const CScriptGrapplePoint* pointB =
        TCastToConstPtr< CScriptGrapplePoint >(mgr.GetObjectById(mGrapplePointB));
    for (int i = 0; i < 2; ++i) {
      const CScriptGrapplePoint* point = i == 0 ? pointA : pointB;
      float factor = i == 0 ? mGrapplePointFactorA : mGrapplePointFactorB;
      if (point != nullptr) {
        DrawGrapplePoint(*point, factor, mgr, rotation, true);
      }
    }
  }

  CGraphics::SetDepthRange(depthNear, depthFar);
}

void CCompoundTargetReticle::DrawGrapplePoint(const CScriptGrapplePoint& point, float factor,
                                              const CStateManager& mgr, const CMatrix3f& rotation,
                                              bool zEqual) const {
  CVector3f orbitPosition = point.GetOrbitPosition(mgr);
  CColor selectedColor;
  if (const CScriptGrapplePoint* grapple = TCastToConstPtr< CScriptGrapplePoint >(point)) {
    selectedColor = grapple->GetGrappleParameters().GetConstrainToAxis()
                        ? gpTweakTargeting->GetLockedGrapplePointColor()
                        : gpTweakTargeting->GetGrappleIconColor();
  } else {
    selectedColor = CColor::White();
  }
  CColor color =
      CColor::Lerp(gpTweakTargeting->GetGrappleIconColorInactive(), selectedColor, factor);
  const CTweakTargeting* tweak = gpTweakTargeting.get();
  factor =
      (1.f - factor) * tweak->GetGrappleIconScaleInactive() + factor * tweak->GetGrappleIconScale();
  float scale =
      CalculateClampedScale(orbitPosition, 1.f, gpTweakTargeting->GetGrappleIconMinRadiusViewport(),
                            gpTweakTargeting->GetGrappleIconMaxRadiusViewport(), mgr, mPlayerIndex);
  scale *= factor;

  CMatrix3f scaledRotation = rotation * CMatrix3f::Scale(scale);
  gpRender->SetModelMatrix(CTransform4f(scaledRotation, orbitPosition));
  const CModel* model = mGrapple.GetObject();
  model->Draw(CModelFlags::Additive(color).DepthCompareUpdate(zEqual, false));
}

void CCompoundTargetReticle::DrawCurrLockOnGroup(const CMatrix3f& rotation,
                                                 const CStateManager& mgr) const {
  if (mNoDrawTicks > 0)
    return;

  CVector3f position = mCurrentGroupInterpolated.GetTargetPositionWorld();
  float radius = mCurrentGroupInterpolated.GetRadiusWorld();

  if (mGrapplePointFactorA + mGrapplePointFactorB > 0.f)
    return;

  float factor = mCurrentGroupInterpolated.GetFactor();
  float lockBreakAlpha = factor;
  if (0.f == factor)
    return;

  float visorFactor = mgr.GetPlayerState(mPlayerIndex)->GetVisorTransitionFactor();
  float minVpClampScale = mCurrentGroupInterpolated.GetMinViewportClampScale();

  bool lockConfirm = false;
  bool lockReticule = false;

  bool healthMeter = false;
  switch (mPreviousState) {
  case kRS_Combat:
  case kRS_Echo:
  case kRS_Dark:
    lockConfirm = true;
    lockReticule = true;
    healthMeter = true;
    break;
  default:
    break;
  }

  CMatrix3f lockBreakXf(CMatrix3f::Identity());
  CColor lockBreakColor(0);

  if (IsDamageOrbit(mgr.GetPlayer(mPlayerIndex)->GetOrbitRequest()) &&
      mCurrentGroupB.GetFactor() == 0.f) {
    CVector3f columns[3] = {CVector3f::Right(), CVector3f::Forward(), CVector3f::Up()};

    for (int i = 0; i < 4; ++i) {
      int r1 = rand();
      int idx = rand() % 9;
      int col = idx % 3;
      int row = idx / 3;
      columns[col][row] += static_cast< float >(r1) / static_cast< float >(RAND_MAX) - 0.5f;
    }

    lockBreakXf = CMatrix3f(columns[0], columns[1], columns[2]);

    if (factor > 0.8f) {
      lockBreakColor = CColor::White().WithAlphaOf(0.3f * (factor - 0.8f) / 0.2f);
    }

    if (factor > 0.75f) {
      lockBreakAlpha = 1.f;
    } else {
      lockBreakAlpha = rstl::max_val((factor - 0.55f) / 0.2f, 0.f);
    }
  }

  if (lockConfirm) {
    const_cast< TCachedToken< CModel >& >(mLockConfirm).TryCache();
    if (CModel* const model = mLockConfirm.GetObject()) {
      CTweakTargeting* tweak = gpTweakTargeting.get();
      float scale = CalculateClampedScale(
          position, radius, minVpClampScale * tweak->GetLockOnConfirmMinRadiusViewport(),
          tweak->GetLockOnConfirmMaxRadiusViewport(), mgr, mPlayerIndex);
      scale *= gpTweakTargeting->GetLockOnConfirmReticleScale();
      scale /= factor;

      CMatrix3f combined = rotation *
                           CMatrix3f::RotateY(CRelAngle::FromRadians(mSeekerRotationAngle)) *
                           CMatrix3f::Scale(scale);

      gpRender->SetModelMatrix(
          CTransform4f(lockBreakXf * combined, mCurrentGroupInterpolated.GetTargetPositionWorld()));

      model->Draw(CModelFlags::Additive(
                      CColor::Add(lockBreakColor,
                                  tweak->GetLockOnConfirmReticleColor().WithAlphaModulatedBy(
                                      lockBreakAlpha)))
                      .DepthCompareUpdate(false, false));
    }
  }

  if (lockReticule) {
    // Target flower
    const_cast< TCachedToken< CModel >& >(mTargetFlower).TryCache();
    if (CModel* const model = mTargetFlower.GetObject()) {
      float scale = CalculateClampedScale(
          position, radius, minVpClampScale * gpTweakTargeting->GetFlowerMinRadiusViewport(),
          gpTweakTargeting->GetFlowerMaxRadiusViewport(), mgr, mPlayerIndex);
      CTweakTargeting* tweak = gpTweakTargeting.get();
      scale *= tweak->GetFlowerReticleScale();
      scale /= lockBreakAlpha;

      CMatrix3f combined = rotation *
                           CMatrix3f::RotateY(CRelAngle::FromRadians(mFlowerRotationAngle)) *
                           CMatrix3f::Scale(scale);

      gpRender->SetModelMatrix(
          CTransform4f(lockBreakXf * combined, mCurrentGroupInterpolated.GetTargetPositionWorld()));

      model->Draw(
          CModelFlags::Additive(
              CColor::Add(lockBreakColor, tweak->GetFlowerReticleColor().WithAlphaModulatedBy(
                                              lockBreakAlpha * visorFactor)))
              .DepthCompareUpdate(true, false));
    }

    // Missile bracket
    if (mMissileBracketTimer != 0.f) {
      const_cast< TCachedToken< CModel >& >(mMissileBracket).TryCache();
      if (CModel* const bracketModel = mMissileBracket.GetObject()) {
        float bracketScale = CalculateClampedScale(
            position, radius,
            minVpClampScale * gpTweakTargeting->GetMissileBracketMinRadiusViewport(),
            gpTweakTargeting->GetMissileBracketMaxRadiusViewport(), mgr, mPlayerIndex);
        CTweakTargeting* tweak = gpTweakTargeting.get();
        float halfDur = 0.5f * tweak->GetMissileBracketMissileFireAnimTime();
        float t = CMath::AbsF((mMissileBracketScaleTimer - halfDur) / halfDur);
        float tscale = (1.f - t) * tweak->GetMissileBracketScaleEnd() +
                       t * tweak->GetMissileBracketScaleStart();
        float bracketFactor =
            CMath::AbsF(mMissileBracketTimer) / tweak->GetMissileBracketOpenHolsterTime();
        float s = bracketFactor * bracketScale * tscale / factor;

        CMatrix3f scaleMtx = CMatrix3f::Scale(s);

        for (int i = 0; i < 4; ++i) {
          float xSign = i < 2 ? 1.f : -1.f;
          float zSign = (i & 1) != 0 ? 1.f : -1.f;
          CMatrix3f combined = lockBreakXf * rotation *
                               CMatrix3f(xSign, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, zSign) *
                               scaleMtx;

          gpRender->SetModelMatrix(
              CTransform4f(combined, mCurrentGroupInterpolated.GetTargetPositionWorld()));

          bracketModel->Draw(
              CModelFlags::Additive(
                  CColor::Add(lockBreakColor, tweak->GetMissileBracketColor().WithAlphaModulatedBy(
                                                  lockBreakAlpha * visorFactor)))
                  .DepthCompareUpdate(false, false));
        }
      }
    }

    // Outer beam icon squares
    {
      float outerScale = CalculateClampedScale(
          position, radius, minVpClampScale * gpTweakTargeting->GetOuterIconMinRadiusViewport(),
          gpTweakTargeting->GetOuterIconMaxRadiusViewport(), mgr, mPlayerIndex);
      outerScale = gpTweakTargeting->GetOuterBeamIconScale() * (1.f / factor * outerScale);

      CMatrix3f outerBeamXf = rotation * CMatrix3f::Scale(outerScale);
      int i;
      CTweakTargeting* tweak = gpTweakTargeting.get();

      for (i = 0; i < 9; ++i) {
        const SOuterItemInfo& info = mOuterBeamIconSquares[i];
        const_cast< TCachedToken< CModel >& >(info.mModel).TryCache();
        CModel* const outerModel = info.mModel.GetObject();
        if (outerModel != nullptr) {
          CRelAngle outerAngle = CRelAngle::FromRadians(info.mRotationAngle);
          CMatrix3f combined = outerBeamXf * CMatrix3f::RotateY(outerAngle);

          gpRender->SetModelMatrix(CTransform4f(
              lockBreakXf * combined, mCurrentGroupInterpolated.GetTargetPositionWorld()));

          outerModel->Draw(
              CModelFlags::Additive(
                  CColor::Add(lockBreakColor, tweak->GetOuterBeamIconColor().WithAlphaModulatedBy(
                                                  lockBreakAlpha * visorFactor)))
                  .DepthCompareUpdate(false, false));
        }
      }
    }

    // Charge gauge
    {
      const_cast< SOuterItemInfo& >(mChargeGauge).mModel.TryCache();
      if (CModel* const gaugeModel = mChargeGauge.mModel.GetObject()) {
        float gaugeScale = CalculateClampedScale(
            position, radius, minVpClampScale * gpTweakTargeting->GetOuterIconMinRadiusViewport(),
            gpTweakTargeting->GetOuterIconMaxRadiusViewport(), mgr, mPlayerIndex);
        gaugeScale = gaugeScale * gpTweakTargeting->GetChargeGaugeScale() / factor;

        CMatrix3f gaugeMtx = rotation * CMatrix3f::Scale(gaugeScale);
        CRelAngle gaugeAngle = CRelAngle::FromRadians(mChargeGauge.mRotationAngle);
        CMatrix3f chargeGaugeXf = gaugeMtx * CMatrix3f::RotateY(gaugeAngle);

        float chargeFadeFactor = mFullChargeFadeTimer / gpTweakTargeting->GetChargeGaugeGlowTime();
        float pulsePeriod = gpTweakTargeting->GetChargeGaugePulsePeriod();
        float secondsMod = CGraphics::GetSecondsMod900();
        float pulseT = CMath::AbsF(static_cast< float >(
            fmod(static_cast< double >(secondsMod), static_cast< double >(pulsePeriod))));
        float halfPeriod = 0.5f * pulsePeriod;
        float pulseRatio;
        if (pulseT < halfPeriod) {
          pulseRatio = pulseT / halfPeriod;
        } else {
          pulseRatio = (pulsePeriod - pulseT) / halfPeriod;
        }

        CColor pulseColor = CColor::Lerp(gpTweakTargeting->GetChargeGaugeGlowColor(),
                                         gpTweakTargeting->GetChargeGaugeGlowColorB(), pulseRatio);
        CColor gaugeColor =
            CColor::Lerp(gpTweakTargeting->GetChargeGaugeColor(), pulseColor, chargeFadeFactor);

        CTransform4f modelXf = CTransform4f(lockBreakXf * chargeGaugeXf,
                                            mCurrentGroupInterpolated.GetTargetPositionWorld());
        gpRender->SetModelMatrix(modelXf);

        gaugeModel->Draw(
            CModelFlags::Additive(CColor::Add(lockBreakColor, gaugeColor.WithAlphaModulatedBy(
                                                                  lockBreakAlpha * visorFactor)))
                .DepthCompareUpdate(false, false));

        // Charge ticks
        const_cast< TCachedToken< CModel >& >(mChargeTickFirst).TryCache();
        CModel* const tickModel = mChargeTickFirst.GetObject();
        if (tickModel != nullptr) {
          int numTicks =
              static_cast< int >(static_cast< float >(gpTweakTargeting->GetChargeTickCount()) *
                                 mgr.GetPlayerState(mPlayerIndex)->GetChargeBeamFactor());
          for (int i = 0; i < numTicks; ++i) {
            tickModel->Draw(CModelFlags::Additive(
                                CColor::Add(lockBreakColor, gaugeColor.WithAlphaModulatedBy(
                                                                lockBreakAlpha * visorFactor)))
                                .DepthCompareUpdate(false, false));
            modelXf.RotateLocalY(
                CRelAngle::FromRadians(gpTweakTargeting->GetChargeGaugeTickDeltaAngle()));
            gpRender->SetModelMatrix(modelXf);
          }
        }
      }
    }

    // Inner beam icon
    if (mLockOnTimer > 0.f) {
      const_cast< TCachedToken< CModel >& >(mInnerBeamIcon).TryCache();
      if (CModel* const beamModel = mInnerBeamIcon.GetObject()) {
        const CColor* iconColor = &CColor::White();

        float beamScale = CalculateClampedScale(
            position, radius, minVpClampScale * gpTweakTargeting->GetInnerIconMinRadiusViewport(),
            gpTweakTargeting->GetInnerIconMaxRadiusViewport(), mgr, mPlayerIndex);
        beamScale = beamScale * gpTweakTargeting->GetInnerBeamIconScale() *
                    (mLockOnTimer / gpTweakTargeting->GetInnerBeamIconOpenTime()) / factor;

        CMatrix3f beamMtx = rotation * CMatrix3f::Scale(beamScale);

        gpRender->SetModelMatrix(CTransform4f(lockBreakXf * beamMtx,
                                              mCurrentGroupInterpolated.GetTargetPositionWorld()));

        beamModel->Draw(
            CModelFlags::Additive(CColor::Add(lockBreakColor, iconColor->WithAlphaModulatedBy(
                                                                  lockBreakAlpha * visorFactor)))
                .DepthCompareUpdate(false, false));
      }
    }

    // Lock fire
    if (mLockFireTimer > 0.f) {
      const_cast< TCachedToken< CModel >& >(mLockFire).TryCache();
      if (CModel* const fireModel = mLockFire.GetObject()) {
        CTweakTargeting* tweak = gpTweakTargeting.get();
        float lockFireFactor = mLockFireTimer / tweak->GetLockFireAnimTime();

        float fireScale = CalculateClampedScale(
            position, radius, minVpClampScale * tweak->GetLockFireMinRadiusViewport(),
            tweak->GetLockFireMaxRadiusViewport(), mgr, mPlayerIndex);
        fireScale = fireScale * gpTweakTargeting->GetLockFireReticleScale() / factor;

        CMatrix3f combined = rotation * CMatrix3f::Scale(fireScale) *
                             CMatrix3f::RotateY(CRelAngle::FromRadians(mFlowerRotationAngle));

        gpRender->SetModelMatrix(CTransform4f(lockBreakXf * combined,
                                              mCurrentGroupInterpolated.GetTargetPositionWorld()));

        fireModel->Draw(
            CModelFlags::Additive(
                CColor::Add(lockBreakColor, tweak->GetLockFireColor().WithAlphaModulatedBy(
                                                lockBreakAlpha * lockFireFactor * visorFactor)))
                .DepthCompareUpdate(false, false));
      }
    }

    // Lock dagger
    if (mLockOnTimer > 0.f) {
      const_cast< TCachedToken< CModel >& >(mLockDagger).TryCache();
      if (CModel* const daggerModel = mLockDagger.GetObject()) {
        float daggerScale = CalculateClampedScale(
            position, radius, minVpClampScale * gpTweakTargeting->GetLockDaggerMinRadiusViewport(),
            gpTweakTargeting->GetLockDaggerMaxRadiusViewport(), mgr, mPlayerIndex);
        CTweakTargeting* tweak = gpTweakTargeting.get();
        float halfDur = 0.5f * tweak->GetLockFireAnimTime();
        float t = CMath::AbsF((mLockFireTimer - halfDur) / halfDur);
        float tscale =
            (1.f - t) * tweak->GetLockDaggerScaleEnd() + t * tweak->GetLockDaggerNormalScale();
        daggerScale =
            daggerScale * tscale * (mLockOnTimer / tweak->GetInnerBeamIconOpenTime()) / factor;

        CMatrix3f daggerMtx = rotation * CMatrix3f::Scale(daggerScale);

        for (int i = 0; i < 3; ++i) {
          float ang;
          if (i == 0) {
            ang = gpTweakTargeting->GetLockDagger0Angle();
          } else if (i == 1) {
            ang = gpTweakTargeting->GetLockDagger1Angle();
          } else {
            ang = gpTweakTargeting->GetLockDagger2Angle();
          }

          CMatrix3f combined = daggerMtx * CMatrix3f::RotateY(CRelAngle::FromRadians(ang));

          gpRender->SetModelMatrix(CTransform4f(
              lockBreakXf * combined, mCurrentGroupInterpolated.GetTargetPositionWorld()));

          daggerModel->Draw(
              CModelFlags::Additive(
                  CColor::Add(lockBreakColor, tweak->GetLockDaggerColor().WithAlphaModulatedBy(
                                                  lockBreakAlpha * visorFactor)))
                  .DepthCompareUpdate(false, false));
        }
      }
    }
  }

  if (healthMeter && mVulnerabilityTarget != kInvalidUniqueId) {
    if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mVulnerabilityTarget))) {
      if (actor->GetHealthInfo() && !close_enough(actor->GetHealthInfo()->GetInitialHP(), 0.f)) {
        float initialHealth = actor->GetHealthInfo()->GetInitialHP();
        const_cast< TCachedToken< CTexture >& >(mQuarterCurve).TryCache();
        if (CTexture* texture = mQuarterCurve.GetObject()) {
          texture->Load(GX_TEXMAP0, CTexture::kCM_Repeat);
          float shadowAlpha = 0.5f * lockBreakAlpha;
          for (int pass = 0; pass < 2; ++pass) {
            float health = pass == 0 ? mTargetHealthShadow : mTargetHealth;
            if (health > initialHealth) {
              return;
            }
            float healthFactor = health / initialHealth;
            CColor color = gpTweakTargeting->GetHealthColor();
            CTweakTargeting* tweak = gpTweakTargeting.get();
            float scale = CalculateClampedScale(
                position, radius, minVpClampScale * tweak->GetLockOnConfirmMinRadiusViewport(),
                tweak->GetLockOnConfirmMaxRadiusViewport(), mgr, mPlayerIndex);
            scale = scale * gpTweakTargeting->GetLockOnConfirmReticleScale() / factor;
            CMatrix3f combined = rotation * CMatrix3f::Scale(scale);
            gpRender->SetModelMatrix(CTransform4f(
                lockBreakXf * combined, mCurrentGroupInterpolated.GetTargetPositionWorld()));

            int segments = static_cast< int >(CMath::CeilingF(12.f * healthFactor));
            float innerRadius = 0.75f * gpTweakTargeting->GetHealthMeterRadius();
            float outerRadius = 1.25f * gpTweakTargeting->GetHealthMeterRadius();
            CGraphics::SetTevOp(kTS_Stage0, CGraphics::kEnvModulate);
            CGraphics::SetTevOp(kTS_Stage1, CGraphics::kEnvPassthru);
            CGraphics::SetCullMode(kCM_None);
            gpRender->SetBlendMode_AdditiveAlpha();
            CGraphics::SetDepthWriteMode(false, kE_LEqual, false);
            CGraphics::StreamColor(
                color.WithAlphaModulatedBy(pass == 0 ? shadowAlpha : lockBreakAlpha));

            if (segments > 0) {
              CGraphics::StreamBegin(kP_TriangleStrip);
              float sweep = static_cast< float >(2.0 * M_PI) * healthFactor;
              for (int i = 0; i < segments + 1; ++i) {
                float angle = static_cast< float >(i) * sweep / static_cast< float >(segments);
                float textureAngle = 0.01f;
                if (i & 1) {
                  textureAngle += sweep / static_cast< float >(segments);
                }
                float innerU = CMath::AbsF(0.6f * CMath::FastSinR(textureAngle));
                float innerV = CMath::AbsF(2.f * (0.6f * CMath::FastCosR(textureAngle)));
                float outerU = CMath::AbsF(CMath::FastSinR(textureAngle));
                float outerV = CMath::AbsF(CMath::FastCosR(textureAngle));
                CGraphics::StreamTexcoord(2.f * innerU, innerV);
                float innerZ = innerRadius * CMath::FastCosR(angle);
                CGraphics::StreamVertex(innerRadius * CMath::FastSinR(angle), 0.f, innerZ);
                CGraphics::StreamTexcoord(2.f * outerU, 2.f * outerV);
                float outerZ = outerRadius * CMath::FastCosR(angle);
                CGraphics::StreamVertex(outerRadius * CMath::FastSinR(angle), 0.f, outerZ);
              }
              CGraphics::StreamEnd();
              CGraphics::SetCullMode(kCM_Front);
              CGraphics::SetDepthWriteMode(true, kE_LEqual, true);
            }
          }
        }
      }
    }
  }
}

void CCompoundTargetReticle::DrawSeeker(const CMatrix3f& rotation, const CStateManager& mgr) const {
  if (mNoDrawTicks > 0) {
    return;
  }

  CVector3f position = mNextGroupInterpolated.GetTargetPositionWorld();
  float radius = mNextGroupInterpolated.GetRadiusWorld();
  float factor = mNextGroupInterpolated.GetFactor();
  bool scanReticle = false;
  switch (mPreviousState) {
  case kRS_Combat:
    break;
  case kRS_Scan:
    scanReticle = true;
    break;
  default:
    break;
  }

#ifdef TARGET_PC
  // Trilogy's scan reticle is the ring and brackets (PortTriDrawScan), not the Seeker box.
  if (mPortTri) {
    scanReticle = false;
  }
#endif
  float minimumScale = mNextGroupInterpolated.GetMinViewportClampScale();
  float visorFactor = mgr.GetPlayerState(mPlayerIndex)->GetVisorTransitionFactor();
  if (scanReticle && visorFactor > 0.f) {
    const_cast< TCachedToken< CModel >& >(mSeeker).TryCache();
    if (CModel* const model = mSeeker.GetObject()) {
      CTweakTargeting* tweak = gpTweakTargeting.get();
      float scale = CalculateClampedScale(position, radius,
                                          minimumScale * tweak->GetSeekerMinRadiusViewport(),
                                          tweak->GetSeekerMaxRadiusViewport(), mgr, mPlayerIndex);
      CColor color = gpTweakTargeting->GetSeekerReticleColor();
      scale *= gpTweakTargeting->GetSeekerTargetReticleScale();
      CMatrix3f combined = rotation *
                           CMatrix3f::RotateY(CRelAngle::FromRadians(mSeekerRotationAngle)) *
                           CMatrix3f::Scale(scale);
      gpRender->SetModelMatrix(
          CTransform4f(combined, mNextGroupInterpolated.GetTargetPositionWorld()));
      model->Draw(CModelFlags::Additive(color.WithAlphaModulatedBy(factor))
                      .DepthCompareUpdate(false, false));
    }
  }

#ifdef TARGET_PC
  // Port: F1 "Secondary crosshairs" off hides the next-target reticle; the scan box above stays.
  if (!PortOverlay::SecondaryCrosshairs()) {
    return;
  }
#endif
  if (factor > 0.f) {
    const_cast< TCachedToken< CModel >& >(mSeeker).TryCache();
    if (CModel* const model = mSeeker.GetObject()) {
      CTweakTargeting* tweak = gpTweakTargeting.get();
      float scale = CalculateClampedScale(position, radius,
                                          minimumScale * tweak->GetSeekerMinRadiusViewport(),
                                          tweak->GetSeekerMaxRadiusViewport(), mgr, mPlayerIndex);
      CColor color = gpTweakTargeting->GetSeekerReticleColor();
      scale *= gpTweakTargeting->GetSeekerTargetReticleScale();
      CMatrix3f combined = rotation *
                           CMatrix3f::RotateY(CRelAngle::FromRadians(mSeekerRotationAngle)) *
                           CMatrix3f::Scale(scale);
      gpRender->SetModelMatrix(
          CTransform4f(combined, mNextGroupInterpolated.GetTargetPositionWorld()));
      model->Draw(CModelFlags::Additive(color.WithAlphaModulatedBy(factor))
                      .DepthCompareUpdate(false, false));
    }
  }
}


#ifdef TARGET_PC
namespace {
// The Prime port's remastered aim cursor (`PortDrawRemasteredCrosshair` there): a soft hexagonal
// centre and three tapered spikes turned 0/120/240 degrees about it, the tip of each toward the
// centre. Units are Remastered's model units on its cursor plane; `k` scales them to the world at
// the depth the cursor is drawn at, and the outlines were read off its arm and centre textures.
struct PortXhairVertex {
  CVector3f pos;
  CColor color;
};
struct PortXhairStation {
  float y, core, glow;
};
const PortXhairStation kPortXhairArm[] = {
    {0.018f, 0.f, 0.f},         {0.014f, 0.004f, 0.040f},  {0.001f, 0.012f, 0.047f},
    {-0.010f, 0.016f, 0.047f},  {-0.022f, 0.016f, 0.048f}, {-0.045f, 0.013f, 0.047f},
    {-0.068f, 0.009f, 0.044f},  {-0.080f, 0.004f, 0.041f}, {-0.100f, 0.003f, 0.032f},
    {-0.125f, 0.0015f, 0.023f}, {-0.135f, 0.f, 0.f},
};
const float kPortXhairGlowAlpha = 0.3f;
const float kPortXhairCentreCore = 0.019f;  // hexagon circumradius, opaque
const float kPortXhairCentreOuter = 0.030f; // where the glow reaches zero
const float kPortXhairCentreAlpha = 0.92f;
const float kPortXhairArmOffset = 0.1f;
const float kPortXhairGlobalScale = 0.45f;

void PortXhairAddQuad(std::vector< PortXhairVertex >& v, const PortXhairVertex& a,
                      const PortXhairVertex& b, const PortXhairVertex& c, const PortXhairVertex& d) {
  v.insert(v.end(), {a, b, c, a, c, d});
}

void PortDrawRemasteredCrosshair(const CMatrix3f& rot, const CVector3f& pos, float k,
                                 float armScale, float centreScale, const CColor& color) {
  std::vector< PortXhairVertex > v;
  const float pi = 3.14159265f;
  const CColor tint(color.GetRed(), color.GetGreen(), color.GetBlue(), color.GetAlpha());
  auto col = [&](float a) { return CColor(tint.GetRed(), tint.GetGreen(), tint.GetBlue(),
                                          tint.GetAlpha() * a); };
  auto vert = [&](float x, float y, float a) {
    return PortXhairVertex{CVector3f(x * k, 0.f, y * k), col(a)};
  };

  // Centre: solid hexagon (point up) and a falloff ring out to the glow radius.
  {
    const float su = centreScale * kPortXhairGlobalScale;
    for (int i = 0; i < 6; ++i) {
      const float a0 = (90.f + 60.f * static_cast< float >(i)) * pi / 180.f;
      const float a1 = (90.f + 60.f * static_cast< float >(i + 1)) * pi / 180.f;
      const float ri = kPortXhairCentreCore * su, ro = kPortXhairCentreOuter * su;
      v.push_back(vert(0.f, 0.f, kPortXhairCentreAlpha));
      v.push_back(vert(ri * cosf(a0), ri * sinf(a0), kPortXhairCentreAlpha));
      v.push_back(vert(ri * cosf(a1), ri * sinf(a1), kPortXhairCentreAlpha));
      PortXhairAddQuad(v, vert(ri * cosf(a0), ri * sinf(a0), kPortXhairCentreAlpha),
                       vert(ro * cosf(a0), ro * sinf(a0), 0.f),
                       vert(ro * cosf(a1), ro * sinf(a1), 0.f),
                       vert(ri * cosf(a1), ri * sinf(a1), kPortXhairCentreAlpha));
    }
  }

  // Arms: glow strip (faint along the axis, zero at the edges), then the solid core.
  const float sa = armScale * kPortXhairGlobalScale;
  const int stations = static_cast< int >(sizeof(kPortXhairArm) / sizeof(kPortXhairArm[0]));
  for (int arm = 0; arm < 3; ++arm) {
    const float ang = static_cast< float >(arm) * 120.f * pi / 180.f;
    const float ca = cosf(ang), sn = sinf(ang);
    auto place = [&](float x, float y, float a) {
      const float px = x * sa, py = y * sa + kPortXhairArmOffset;
      return vert(px * ca - py * sn, px * sn + py * ca, a);
    };
    for (int i = 0; i + 1 < stations; ++i) {
      const PortXhairStation& p = kPortXhairArm[i];
      const PortXhairStation& q = kPortXhairArm[i + 1];
      PortXhairAddQuad(v, place(-p.glow, p.y, 0.f), place(0.f, p.y, kPortXhairGlowAlpha),
                       place(0.f, q.y, kPortXhairGlowAlpha), place(-q.glow, q.y, 0.f));
      PortXhairAddQuad(v, place(0.f, p.y, kPortXhairGlowAlpha), place(p.glow, p.y, 0.f),
                       place(q.glow, q.y, 0.f), place(0.f, q.y, kPortXhairGlowAlpha));
      PortXhairAddQuad(v, place(-p.core, p.y, 1.f), place(p.core, p.y, 1.f), place(q.core, q.y, 1.f),
                       place(-q.core, q.y, 1.f));
    }
  }

  gpRender->SetModelMatrix(CTransform4f(rot, pos));
  gpRender->SetBlendMode_AlphaBlended();
  CGraphics::SetCullMode(kCM_None);
  gpRender->BeginTriangles(static_cast< int >(v.size()));
  for (const PortXhairVertex& pv : v) {
    gpRender->PrimColor(pv.color);
    gpRender->PrimVertex(pv.pos);
  }
  gpRender->EndPrimitive();
}

CColor PortRemasteredCrosshairColor(EReticleState state, float targetT) {
  float idle[3] = {0.f, 0.7f, 1.f};
  if (state == kRS_Dark) {
    idle[0] = 1.f, idle[1] = 0.54f, idle[2] = 0.19f;
  } else if (state == kRS_Echo) {
    idle[0] = 0.58f, idle[1] = 0.91f, idle[2] = 1.f;
  } else if (state == kRS_Scan) {
    idle[0] = 0.55f, idle[1] = 1.f, idle[2] = 0.55f;
  }
  float rgb[3];
  if (PortOverlay::CrosshairColor(rgb)) {
    std::copy_n(rgb, 3, idle);
  }
  const float target[3] = {1.f, 0.f, 0.f};
  return CColor(idle[0] + (target[0] - idle[0]) * targetT,
                idle[1] + (target[1] - idle[1]) * targetT,
                idle[2] + (target[2] - idle[2]) * targetT, 1.f);
}
} // namespace
#endif

void CCompoundTargetReticle::DrawCrosshairs(const CMatrix3f& rotation,
                                            const CStateManager& mgr) const {
#ifdef TARGET_PC
  if (mNoDrawTicks > 0 || mCrosshairsDrawScale <= 0.f) {
    return;
  }
#else
  if (mNoDrawTicks <= 0 && mCrosshairsDrawScale > 0.f) {
#endif
#ifdef TARGET_PC
  // The Prime port's two styles, from the same place it draws them (its DrawOrbitZoneGroup):
  // `remastered` replaces the game's reticle model with the cursor drawn procedurally below, and
  // that path runs before the model is looked up because this port's classic model never loads
  // (`mCrosshairs.GetObject()` is null), so the classic style draws nothing here.
  // While Trilogy's own aiming cursor is up (PortTriDrawAiming), it is the crosshair, unless the
  // Remastered or GameCube style replaces it.
  if (mPortTri && mPortTriAimArmAlpha > 0.f &&
      PortOverlay::CrosshairStyle() == PortOverlay::kCrosshairStyle_Classic) {
    return;
  }
  float scale = mCrosshairsDrawScale;
  if (PortMouse::Aiming()) {
    scale *= static_cast< float >(PortOverlay::CrosshairSize()) / 100.f;
  }
  CColor color = gpTweakTargeting->GetCrosshairsColor();
  {
    float rgb[3];
    if (PortOverlay::CrosshairColor(rgb)) {
      color = CColor(rgb[0], rgb[1], rgb[2], color.GetAlpha());
    }
    const float opacity = static_cast< float >(PortOverlay::CrosshairOpacity()) / 100.f;
    // Retail's tweak alpha, faded by the draw scale as retail's Additive flags do; the option
    // scales it (100% = retail). Remastered builds its own colour below.
    color = CColor(color.GetRed(), color.GetGreen(), color.GetBlue(),
                   color.GetAlpha() * opacity * mCrosshairsDrawScale);
  }
  if (PortOverlay::CrosshairStyle() == PortOverlay::kCrosshairStyle_Remastered) {
    const CCameraManager* camMgr = mgr.GetCameraManager(mPlayerIndex);
    const CGameCamera* cam = camMgr->GetCurrentCamera(mgr, true);
    const CTransform4f camXf = camMgr->GetCurrentCameraTransform(mgr, true);
    // The cursor sits on its own plane in front of the camera, not on the game's reticle: the
    // game's reticle position is metres away by design (its model is scaled to compensate), and
    // scaling Remastered's geometry by that depth made it several times too large. At its own
    // 1.25 m plane, Remastered's model units map to the world with just the FOV correction.
    constexpr float kCursorPlane = 1.25f;
    constexpr float kRemasteredFov = 55.f; // the vertical FOV its cursor size is tuned for
    const float fovRad = cam->GetFov() * 3.14159265f / 180.f;
    const float units = tanf(0.5f * fovRad) / tanf(0.5f * kRemasteredFov * 3.14159265f / 180.f) *
                        static_cast< float >(PortOverlay::CrosshairSize()) / 100.f;
    CVector3f cursorLocal(0.f, kCursorPlane, 0.f);
    // Pointer aim: the cursor is the pointer, on the same plane where the ray through it meets.
    float pointerX = 0.f;
    float pointerY = 0.f;
    if (PortMouse::PointerAimPosition(pointerX, pointerY) && (pointerX != 0.f || pointerY != 0.f)) {
      float yaw = 0.f, pitch = 0.f, rayX = 0.f, rayY = 1.f, rayZ = 0.f;
      PortMouse::PointerAim::RayAngles(pointerX, pointerY, cam->GetFov(), cam->GetAspectRatio(),
                                       yaw, pitch);
      PortMouse::PointerAim::RayDirection(yaw, pitch, rayX, rayY, rayZ);
      if (rayY > 0.01f) {
        cursorLocal = CVector3f(rayX, rayY, rayZ) * (kCursorPlane / rayY);
      }
    }
    const CVector3f cursor = camXf * cursorLocal;
    CColor rc = PortRemasteredCrosshairColor(mPreviousState, mPortXhairTargetT);
    rc = CColor(rc.GetRed(), rc.GetGreen(), rc.GetBlue(),
                mCrosshairsDrawScale * static_cast< float >(PortOverlay::CrosshairOpacity()) / 100.f);
    CGraphics::SetDepthWriteMode(false, kE_Always, false);
    PortDrawRemasteredCrosshair(rotation, cursor, units * kCursorPlane, mPortXhairArmScale,
                                mPortXhairCentreScale, rc);
    gpRender->SetBlendMode_AlphaBlended();
    return;
  }
#endif
  const_cast< TCachedToken< CModel >& >(mCrosshairs).TryCache();
  CModel* const model = mCrosshairs.GetObject();
  if (model == nullptr) {
    return;
  }
#ifdef TARGET_PC
  CTransform4f xf(rotation, PortPresentedTarget(mgr));
  if (mPortTri && PortOverlay::CrosshairStyle() == PortOverlay::kCrosshairStyle_GameCube) {
    // In place of the aiming cursor: the model (built out along its Y) turned down the pointer
    // ray as drawn, so it marks the aim where the cursor would (MP1 255afaf13).
    const CCameraManager* camMgr = mgr.GetCameraManager(mPlayerIndex);
    const CTransform4f cam = camMgr->GetCurrentCameraTransform(mgr, true);
    const CVector3f eye = cam.GetTranslation();
    xf = CTransform4f::LookAt(
        eye, eye + PortTriPointerDir(cam, *camMgr->GetCurrentCamera(mgr, true)), cam.GetUp());
  }
  gpRender->SetModelMatrix(xf * CTransform4f::Scale(scale));
  model->Draw(CModelFlags::Additive(color).DepthCompareUpdate(false, false));
#else
  const CColor& color = gpTweakTargeting->GetCrosshairsColor();
  gpRender->SetModelMatrix(CTransform4f(rotation, mTargetPosition) *
                           CTransform4f::Scale(mCrosshairsDrawScale));
  model->Draw(CModelFlags::Additive(color.WithAlphaModulatedBy(mCrosshairsDrawScale))
                  .DepthCompareUpdate(false, false));
#endif
#ifndef TARGET_PC
  }
#endif
}

#ifdef TARGET_PC
void CCompoundTargetReticle::PortUpdateRemasteredCrosshair(float dt, const CStateManager& mgr) {
  const CCameraManager* camMgr = mgr.GetCameraManager(mPlayerIndex);
  CVector3f eye = camMgr->GetLastCameraTransform().GetTranslation();
  CVector3f dir = mTargetPosition - eye;
  // Pointer aim: what is under the pointer, not under the screen centre.
  float pointerX = 0.f;
  float pointerY = 0.f;
  if (PortMouse::PointerAimPosition(pointerX, pointerY) && (pointerX != 0.f || pointerY != 0.f)) {
    const CGameCamera* cam = camMgr->GetCurrentCamera(mgr, true);
    float yaw = 0.f, pitch = 0.f, rayX = 0.f, rayY = 1.f, rayZ = 0.f;
    PortMouse::PointerAim::RayAngles(pointerX, pointerY, cam->GetFov(), cam->GetAspectRatio(), yaw,
                                     pitch);
    PortMouse::PointerAim::RayDirection(yaw, pitch, rayX, rayY, rayZ);
    dir = camMgr->GetLastCameraTransform().Rotate(CVector3f(rayX, rayY, rayZ));
  }
  const float kRange = 100.f;
  float distance = kRange;
  bool overTarget = false;
  if (dir.CanBeNormalized()) {
    dir.Normalize();
    static const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
        CMaterialList(kMT_Solid), CMaterialList(kMT_Player, kMT_ProjectilePassthrough));
    rstl::reserved_vector< TUniqueId, 1024 > nearList;
    mgr.BuildNearList(nearList, eye, dir, kRange, filter, mgr.GetPlayer(mPlayerIndex));
    TUniqueId hitId = kInvalidUniqueId;
    const CRayCastResult hit = mgr.RayWorldIntersection(hitId, eye, dir, kRange, filter, nearList);
    if (hit.IsValid()) {
      distance = (hit.GetPoint() - mgr.GetPlayer(mPlayerIndex)->GetTranslation()).Magnitude();
      const CEntity* ent = mgr.GetObjectById(hitId);
      if (const CCollisionActor* col = TCastToConstPtr< CCollisionActor >(ent)) {
        ent = mgr.GetObjectById(col->GetOwnerId());
      }
      if (const CPatterned* pat = TCastToConstPtr< CPatterned >(ent)) {
        // MP2's CPatterned exposes these two; "targetable" is not decompiled here.
        overTarget = pat->GetAlive() && pat->GetActive();
      }
    }
  }
  const float f = rstl::min_val(rstl::max_val((distance - 1.f) / 49.f, 0.f), 1.f);
  auto step = [](float cur, float want, float lo) {
    return rstl::min_val(rstl::max_val(cur + rstl::min_val(rstl::max_val(want - cur, -0.2f), 0.2f), lo), 1.f);
  };
  mPortXhairArmScale = step(mPortXhairArmScale, 1.f + (0.45f - 1.f) * f, 0.45f);
  mPortXhairCentreScale = step(mPortXhairCentreScale, 1.f + (0.33f - 1.f) * f, 0.33f);
  mPortXhairTargetT = overTarget ? rstl::min_val(mPortXhairTargetT + dt / 0.25f, 1.f)
                                 : rstl::max_val(mPortXhairTargetT - dt / 0.25f, 0.f);
}
#endif

void CCompoundTargetReticle::DrawScanTargetGroup(const CMatrix3f& rotation,
                                                 const CStateManager& mgr) const {
  if (mNoDrawTicks > 0) {
    return;
  }

  switch (mgr.GetPlayerState(mPlayerIndex)->GetCurrentVisor()) {
  case CPlayerState::kPV_Scan:
    break;
  default:
    return;
  }
  if (mPreviousState != kRS_Scan) {
    return;
  }
#ifdef TARGET_PC
  // Trilogy replaces the GC scan target crosshair with the ring and brackets (PortTriDrawScan).
  if (mPortTri) {
    return;
  }
#endif

  const_cast< TCachedToken< CModel >& >(mScanTargetCenter).TryCache();
  const_cast< TCachedToken< CModel >& >(mScanTargetLeft).TryCache();
  const_cast< TCachedToken< CModel >& >(mScanTargetRight).TryCache();
  CModel* const center = mScanTargetCenter.GetObject();
  CModel* const left = mScanTargetLeft.GetObject();
  CModel* const right = mScanTargetRight.GetObject();
  if (center != nullptr && left != nullptr && right != nullptr) {

    const CColor& crosshairColor = gpTweakTargeting->GetScanLockCrossHairColor();
    float factor = mScanTargetFactor * skScanLockLayoutScale[mgr.GetViewportLayoutIndex()];
#ifdef TARGET_PC
    // Placed at the tick; drawn through the presented camera (MP1 689c6cb1c).
    const CVector3f portTarget = PortPresentedTarget(mgr);
#endif
    gpRender->SetModelMatrix(CTransform4f(rotation,
#ifdef TARGET_PC
                                       portTarget
#else
                                       mTargetPosition
#endif
                                       ) *
                             CTransform4f::Scale(factor * gpTweakTargeting->GetScanLockScale()));
    center->Draw(CModelFlags::Additive(crosshairColor.WithAlphaModulatedBy(factor))
                     .DepthCompareUpdate(false, false));

    CTweakTargeting* tweak = gpTweakTargeting.get();
    CColor bracketColor = CColor::Lerp(tweak->GetScanLockUnlockedColor(),
                                       tweak->GetScanLockLockedColor(), mScanBracketFactor);
    tweak = gpTweakTargeting.get();
    gpRender->SetModelMatrix(
        CTransform4f(rotation,
#ifdef TARGET_PC
                                       portTarget
#else
                                       mTargetPosition
#endif
                                       ) *
        CTransform4f::Scale(factor * tweak->GetScanLockScale()) *
        CTransform4f::Translate((1.f - mScanBracketFactor) * -tweak->GetScanLockTranslation(), 0.f,
                                0.f));
    left->Draw(CModelFlags::Additive(bracketColor.WithAlphaModulatedBy(factor))
                   .DepthCompareUpdate(false, false));

    tweak = gpTweakTargeting.get();
    gpRender->SetModelMatrix(
        CTransform4f(rotation,
#ifdef TARGET_PC
                                       portTarget
#else
                                       mTargetPosition
#endif
                                       ) *
        CTransform4f::Scale(factor * tweak->GetScanLockScale()) *
        CTransform4f::Translate((1.f - mScanBracketFactor) * tweak->GetScanLockTranslation(), 0.f,
                                0.f));
    right->Draw(CModelFlags::Additive(bracketColor.WithAlphaModulatedBy(factor))
                    .DepthCompareUpdate(false, false));
  }
}

void CCompoundTargetReticle::DrawNextLockOnGroup(const CMatrix3f& rotation,
                                                 const CStateManager& mgr) const {
  CColor seekingColor(static_cast< uchar >(244), static_cast< uchar >(103), static_cast< uchar >(5),
                      static_cast< uchar >(179));
  CColor unusedColor(0.f, 1.f, 0.7f, 1.f);
  CColor lockedColor(1.f, 0.5f, 0.5f, 1.f);
  CColor white = CColor::White();

  const CPlayer* player = mgr.GetPlayer(mPlayerIndex);
  const CPlayerGun* gun = player->GetPlayerGun();
  float lockedTime = gun->GetAllSeekersLockedTime() - 0.2f;
  float chargeFactor = gun->GetSeekerChargeFactor();
  int maximumTargets = gun->GetMaxSeekerTargets();
  if (chargeFactor > 0.75f) {
    const_cast< TCachedToken< CModel >& >(mSeekerMissileCrosshair).TryCache();
    if (CModel* const crosshair = mSeekerMissileCrosshair.GetObject()) {
      float factor = (chargeFactor - 0.75f) / 0.25f;
#ifdef TARGET_PC
      const CVector3f portTarget = PortPresentedTarget(mgr);
#endif
      gpRender->SetModelMatrix(CTransform4f(rotation,
#ifdef TARGET_PC
                                       portTarget
#else
                                       mTargetPosition
#endif
                                       ) *
                               CTransform4f::RotateY(CRelAngle::FromRadians(mSeekerRotationAngle)) *
                               CTransform4f::Scale(0.25f * factor));
      crosshair->Draw(CModelFlags::Additive(seekingColor.WithAlphaModulatedBy(factor))
                          .DepthCompareUpdate(false, false));
    }
  }

  const rstl::reserved_vector< rstl::pair< TUniqueId, float >, 5 >& targets =
      player->GetPlayerGun()->GetSeekerTargets();
  if (targets.empty()) {
    return;
  }

  bool allLocked = targets.size() == maximumTargets;
  CColor targetColor = seekingColor;
  if (allLocked) {
    for (int i = 0; i < targets.size(); ++i) {
      if (targets[i].second < 0.2f) {
        allLocked = false;
        break;
      }
    }
    if (allLocked) {
      if (lockedTime <= 0.1f) {
        targetColor = CColor::Lerp(seekingColor, white, lockedTime / 0.1f);
      } else {
        targetColor =
            CColor::Lerp(white, lockedColor, rstl::min_val((lockedTime - 0.1f) / 0.1f, 1.f));
      }
    }
  }

  const_cast< TCachedToken< CModel >& >(mSeekerMissileLockConfirm).TryCache();
  CModel* const confirm = mSeekerMissileLockConfirm.GetObject();
  if (confirm == nullptr) {
    return;
  }

  rstl::reserved_vector< int, 5 > counts(maximumTargets, 1);
  for (int i = 0; i < targets.size(); ++i) {
    bool first = true;
    for (int j = 0; j < i; ++j) {
      if (targets[i].first == targets[j].first) {
        counts[i] = 0;
        first = false;
        break;
      }
    }
    if (first) {
      for (int j = i + 1; j < targets.size(); ++j) {
        if (targets[i].first == targets[j].first) {
          ++counts[i];
        }
      }
    }
  }

  for (int i = 0; i < targets.size(); ++i) {
    if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(targets[i].first))) {
      bool paint = counts[i] != 0;
      bool drawConfirm = targets[i].second <= 0.2f || paint;
      if (!paint && !drawConfirm) {
        continue;
      }

      CVector3f position = CalculatePositionWorld(*actor, mgr);
      float radius = CalculateRadiusWorld(*actor, mgr);
      CTweakTargeting* tweak = gpTweakTargeting.get();
      float scale = CalculateClampedScale(position, radius, tweak->GetSeekerMinRadiusViewport(),
                                          tweak->GetSeekerMaxRadiusViewport(), mgr, mPlayerIndex);
      float factor = CMath::Clamp(1.2f, 8.f - 34.f * targets[i].second, 8.f);
      scale = factor * scale * gpTweakTargeting->GetSeekerTargetReticleScale();
      if (drawConfirm) {
        CMatrix3f combined = rotation *
                             CMatrix3f::RotateY(CRelAngle::FromRadians(mSeekerRotationAngle)) *
                             CMatrix3f::Scale(scale);
        gpRender->SetModelMatrix(CTransform4f(combined, position));
        float alpha = CMath::Clamp(0.f, 3.5f * targets[i].second, 0.7f);
        confirm->Draw(
            CModelFlags::Additive((allLocked ? targetColor : seekingColor).WithAlphaOf(alpha))
                .DepthCompareUpdate(false, false));
      }

#ifdef TARGET_PC
      // Trilogy: every Seeker lock gets the locked reticle, timed by its lock progress.
      if (mPortTri && paint && mPreviousState == kRS_Combat) {
        PortTriDrawLockAt(rotation, PortTriProjectNear(mgr, position), targets[i].second,
                          PortTriColor(0xa82a00ff), 200.f / 255.f);
      }
#endif
      const_cast< TCachedToken< CTexture >& >(mRadarPaintFirst).TryCache();
      CTexture* texture = mRadarPaintFirst.GetObject();
      if (paint && texture != nullptr) {
        texture->Load(GX_TEXMAP0, CTexture::kCM_Repeat);
        int count = counts[i];
        float angleDelta = 360.f / static_cast< float >(count);
        float baseAngle = 270.f + 180.f / static_cast< float >(count);
        for (int j = 0; j < count; ++j) {
          float x = count == 1
                        ? 0.f
                        : 0.5f * CMath::FastCosR(
                                     M_PIF *
                                     ((baseAngle + angleDelta * static_cast< float >(j)) / 180.f));
          float z = count == 1
                        ? 0.f
                        : 0.5f * CMath::FastSinR(
                                     M_PIF *
                                     ((baseAngle + angleDelta * static_cast< float >(j)) / 180.f));
          gpRender->SetModelMatrix(CTransform4f(rotation * CMatrix3f::Scale(scale), position));
          CGraphics::StreamBegin(kP_TriangleStrip);
          CGraphics::StreamColor(allLocked ? targetColor : seekingColor);
          CGraphics::StreamTexcoord(0.f, 1.f);
          CGraphics::StreamVertex(CVector3f(-0.1f + x, 0.f, 0.1f + z));
          CGraphics::StreamTexcoord(0.f, 0.f);
          CGraphics::StreamVertex(CVector3f(-0.1f + x, 0.f, -0.1f + z));
          CGraphics::StreamTexcoord(1.f, 1.f);
          CGraphics::StreamVertex(CVector3f(0.1f + x, 0.f, 0.1f + z));
          CGraphics::StreamTexcoord(1.f, 0.f);
          CGraphics::StreamVertex(CVector3f(0.1f + x, 0.f, -0.1f + z));
          CGraphics::StreamEnd();
        }
      }
    }
  }
}

void CCompoundTargetReticle::DrawOrbitZoneGroup(const CMatrix3f& rotation,
                                                const CStateManager& mgr) const {
  const rstl::vector< TUniqueId >& ids = mgr.GetPlayerState(mPlayerIndex)->GetIds();
  if (!ids.empty()) {
    gpRender->SetBlendMode_AdditiveAlpha();
    CGraphics::SetTevOp(kTS_Stage0, CGraphics::kEnvModulate);
    CGraphics::SetTevOp(kTS_Stage1, CGraphics::kEnvPassthru);
    CGraphics::SetDepthWriteMode(false, kE_LEqual, false);
  }

  for (rstl::vector< TUniqueId >::const_iterator it = ids.begin(); it != ids.end(); ++it) {
    if (const CScriptHUDHint* hint = TCastToConstPtr< CScriptHUDHint >(mgr.GetObjectById(*it))) {
      if (CTexture* texture = hint->GetTexture()) {
        CScriptHUDHint::TTextureCoordinates coordinates = hint->GetTextureCoordinates();
        CVector3f position = hint->GetTranslation();
        float scale =
            CalculateClampedScale(position, hint->GetIconScale(), hint->GetMinScreenSize(),
                                  hint->GetMaxScreenSize(), mgr, mPlayerIndex);
        gpRender->SetModelMatrix(CTransform4f(rotation * CMatrix3f::Scale(scale), position));

        texture->Load(GX_TEXMAP0, CTexture::kCM_Repeat);
        CGraphics::StreamBegin(kP_TriangleStrip);
        CGraphics::StreamColor(CColor::White());
        CGraphics::StreamTexcoord(coordinates.first.GetX(), coordinates.second.GetY());
        CGraphics::StreamVertex(CVector3f(-1.f, 0.f, 1.f));
        CGraphics::StreamTexcoord(coordinates.first.GetX(), coordinates.first.GetY());
        CGraphics::StreamVertex(CVector3f(-1.f, 0.f, -1.f));
        CGraphics::StreamTexcoord(coordinates.second.GetX(), coordinates.second.GetY());
        CGraphics::StreamVertex(CVector3f(1.f, 0.f, 1.f));
        CGraphics::StreamTexcoord(coordinates.second.GetX(), coordinates.first.GetY());
        CGraphics::StreamVertex(CVector3f(1.f, 0.f, -1.f));
        CGraphics::StreamEnd();
      }
    }
  }

  if (!ids.empty()) {
    CGraphics::SetDepthWriteMode(true, kE_LEqual, true);
  }
}

void CCompoundTargetReticle::UpdateTargetParameters(CTargetReticleRenderState& state,
                                                    const CStateManager& mgr) {
  if (const CActor* actor = TCastToConstPtr< CActor >(
          mgr.GetObjectListById(kOL_All).GetObjectById(state.GetTargetId()))) {
    state.SetRadiusWorld(CalculateRadiusWorld(*actor, mgr));
    CVector3f position = CalculatePositionWorld(*actor, mgr);
    state.SetTargetPositionWorld(position);
  } else if (state.GetIsOrbitZoneIdlePosition()) {
    state.SetRadiusWorld(1.f);
    state.SetTargetPositionWorld((mPreviousState == kRS_Echo || mPreviousState == kRS_Dark)
                                     ? mLaggingTargetPosition
                                     : mTargetPosition);
  }
}

float CCompoundTargetReticle::CalculateRadiusWorld(const CActor& actor,
                                                   const CStateManager& mgr) const {
  rstl::optional_object< CAABox > touchBounds = actor.GetTouchBounds();
  const CAABox& bounds =
      touchBounds.valid() ? *touchBounds
                          : CAABox(actor.GetAimPosition(mgr, 0.f), actor.GetAimPosition(mgr, 0.f));
  const CVector3f min = bounds.GetMinPoint();
  const CVector3f max = bounds.GetMaxPoint();

  float radius;
  switch (gpTweakTargeting->GetTargetRadiusMode()) {
  case 0:
    radius = rstl::min_val(max[0] - min[0], rstl::min_val(max[2] - min[2], max[1] - min[1])) * 0.5f;
    break;
  case 1:
    radius = rstl::max_val(max[0] - min[0], rstl::max_val(max[2] - min[2], max[1] - min[1])) * 0.5f;
    break;
  case 2:
  default: {
    float width = max[0] - min[0];
    float height = max[1] - min[1];
    float depth = max[2] - min[2];
    radius = (width + depth + height) * (1.f / 6.f);
    break;
  }
  }

  if (TCastToConstPtr< CSandwormEye >(actor)) {
    radius = 0.f;
  }
  return radius > 0.f ? radius : 1.f;
}

CVector3f CCompoundTargetReticle::CalculatePositionWorld(const CActor& actor,
                                                         const CStateManager& mgr) const {
  return mPreviousState == kRS_Scan ? actor.GetOrbitPosition(mgr) : actor.GetAimPosition(mgr, 0.f);
}

CVector3f CCompoundTargetReticle::CalculateOrbitZoneReticlePosition(const CStateManager& mgr,
                                                                    bool lag) const {
  const CGameCamera& camera = *mgr.GetCameraManager(mPlayerIndex)->GetCurrentCamera(mgr, true);
  const CPlayer* player = mgr.GetPlayer(mPlayerIndex);
#ifdef TARGET_PC
  float halfFov = camera.GetRenderFov() * 0.5f;
#else
  float halfFov = camera.GetFov() * 0.5f;
#endif
  float halfHeight =
      static_cast< float >(player->GetTweakPlayer()->GetOrbitZoneHeight(CPlayer::kZI_Targeting));
#ifdef TARGET_PC
  // Trilogy (0x8025fbcc): 224 over the 0.65 zone height in the 448 basis, unscaled by the viewport.
  if (port::entry::IsTrilogy(port::entry::CurrentDiscVersion())) {
    halfHeight = static_cast< float >(player->GetTweakPlayer()->PortTriTargetingHalfHeight());
  }
#endif
  float distance = 224.f / halfHeight;
  distance /= static_cast< float >(tan(halfFov * (1.f / 360.f) * (2.f * M_PIF)));

  CTransform4f cameraXf = mgr.GetCameraManager(mPlayerIndex)->GetCurrentCameraTransform(mgr, true);
  CVector3f forward = cameraXf.GetForward();
  if (lag) {
    forward = mLaggingOrientation.Transform(forward);
  }

  return cameraXf.GetTranslation() + distance * forward;
}

#ifdef TARGET_PC
void CCompoundTargetReticle::PortPresentGroup(CTargetReticleRenderState& state,
                                              const CStateManager& mgr) const {
  if (CCameraManager::GetPresentationInterpolation() < 0.f) {
    return;
  }
  // Same sources as UpdateTargetParameters: an actor's point moves with the actor's drawn pose,
  // the idle point with the drawn camera.
  if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(state.GetTargetId()))) {
    state.SetTargetPositionWorld(actor->PortPresentedPoint(state.GetTargetPositionWorld()));
  } else if (state.GetIsOrbitZoneIdlePosition()) {
    const CCameraManager* cameraManager = mgr.GetCameraManager(mPlayerIndex);
    state.SetTargetPositionWorld(
        cameraManager->GetCurrentCameraTransform(mgr, true) *
        (cameraManager->GetSimulationCameraTransform(mgr, true).GetInverse() *
         state.GetTargetPositionWorld()));
  }
}


CVector3f CCompoundTargetReticle::PortPresentedTarget(const CStateManager& mgr) const {
  if (CCameraManager::GetPresentationInterpolation() < 0.f) {
    return mTargetPosition;
  }
  const CCameraManager* cameraManager = mgr.GetCameraManager(mPlayerIndex);
  return cameraManager->GetCurrentCameraTransform(mgr, true) *
         (cameraManager->GetSimulationCameraTransform(mgr, true).GetInverse() * mTargetPosition);
}

#endif
bool CCompoundTargetReticle::IsGrappleTarget(TUniqueId id, const CStateManager& mgr) {
  return TCastToConstPtr< CScriptGrapplePoint >(mgr.GetObjectById(id)) != nullptr;
}

float CCompoundTargetReticle::CalculateClampedScale(CVector3f position, float scale, float clampMin,
                                                    float clampMax, const CStateManager& mgr,
                                                    int playerIndex) {
  float layoutScale = skViewportLayoutScale[mgr.GetViewportLayoutIndex()];
  const float minScale = layoutScale * clampMin;
  const float maxScale = layoutScale * clampMax;
  const CCameraManager* cameraManager = mgr.GetCameraManager(playerIndex);
  const CGameCamera& camera = *cameraManager->GetCurrentCamera(mgr, true);
  CTransform4f cameraXf = cameraManager->GetCurrentCameraTransform(mgr, true);
  const CTransform4f& camXf = camera.GetTransform();
#ifdef TARGET_PC
  // MP1's frame interpolation: draws pass points placed for the presented camera
  // (PortPresentGroup), but the scale is measured against the tick's camera, so between ticks the
  // clamped size pulsed at every step. Measure the point where it sits relative to the drawn one.
  position = cameraManager->GetSimulationCameraTransform(mgr, true) * cameraXf.GetInverse() *
             position;
#endif
  CVector3f viewSpace = camXf.TransposeRotate(position - camXf.GetTranslation());
  float projectedX = camera.GetPerspectiveMatrix().MultiplyOneOverW(viewSpace).GetX();
  float pixelScale = camera.GetPerspectiveMatrix()
                         .MultiplyOneOverW(viewSpace + CVector3f(scale, 0.f, 0.f))
                         .GetX() -
                     projectedX;
  pixelScale *= 640.f * layoutScale;
  return scale * (CMath::Clamp(minScale, pixelScale, maxScale) / pixelScale);
}

CTargetReticleRenderState::CTargetReticleRenderState(TUniqueId target, float radius,
                                                     CVector3f position, float factor,
                                                     float minimumViewportScale,
                                                     bool orbitZoneIdlePosition)
: mTarget(target)
, mRadius(radius)
, mPosition(position)
, mFactor(factor)
, mMinimumViewportScale(minimumViewportScale)
, mOrbitZoneIdlePosition(orbitZoneIdlePosition) {}

void CTargetReticleRenderState::InterpolateWithClamp(const CTargetReticleRenderState& a,
                                                     CTargetReticleRenderState& out,
                                                     const CTargetReticleRenderState& b, float t) {
  float clampedT = CMath::Clamp(0.f, t, 1.f);
  float oneMinusT = 1.f - clampedT;
  out.mRadius = oneMinusT * a.mRadius + clampedT * b.mRadius;
  out.mFactor = oneMinusT * a.mFactor + clampedT * b.mFactor;
  out.mMinimumViewportScale =
      oneMinusT * a.mMinimumViewportScale + clampedT * b.mMinimumViewportScale;
  out.mPosition = CVector3f::Lerp(a.mPosition, b.mPosition, clampedT);
  if (clampedT == 1.f) {
    out.SetTargetId(b.GetTargetId());
  } else if (clampedT == 0.f) {
    out.SetTargetId(a.GetTargetId());
  } else {
    out.SetTargetId(kInvalidUniqueId);
  }
}

CTargetingManager::CTargetingManager(const CStateManager& mgr, int playerIndex)
: mPlayerIndex(playerIndex), mTargetReticle(mgr, playerIndex), mOrbitPointMarker(playerIndex) {}

bool CTargetingManager::CheckLoadComplete() {
  return mTargetReticle.CheckLoadComplete() && mOrbitPointMarker.CheckLoadComplete();
}

void CTargetingManager::Update(float dt, const CStateManager& mgr) {
  mTargetReticle.Update(dt, mgr);
  mOrbitPointMarker.Update(dt, mgr);
}

void CTargetingManager::Draw(const CStateManager& mgr, bool hideLockOn) const {
  CGraphics::SetAmbientColor(CColor::White());
  CGraphics::DisableAllLights();
  mOrbitPointMarker.Draw(mgr);
  const CGameCamera& camera = *mgr.GetCameraManager(mPlayerIndex)->GetCurrentCamera(mgr, true);
  CTransform4f cameraXf = mgr.GetCameraManager(mPlayerIndex)->GetCurrentCameraTransform(mgr, true);
  CGraphics::SetViewPointMatrix(cameraXf);
  float height = static_cast< float >(CGraphics::GetViewport().mHeight);
  float width = static_cast< float >(CGraphics::GetViewport().mWidth);
#ifdef TARGET_PC
  gpRender->SetPerspective(camera.GetRenderFov(), width, height, camera.GetNearClipDistance(),
                           camera.GetFarClipDistance());
#else
  gpRender->SetPerspective(camera.GetFov(), width, height, camera.GetNearClipDistance(),
                           camera.GetFarClipDistance());
#endif
  mTargetReticle.Draw(mgr, hideLockOn);
}

void CCompoundTargetReticle::Touch() const {
  if (mCrosshairs.GetObject()) {
    mCrosshairs.GetObject()->Touch(0);
  }
  if (mSeeker.GetObject()) {
    mSeeker.GetObject()->Touch(0);
  }
  if (mGrapple.GetObject()) {
    mGrapple.GetObject()->Touch(0);
  }
  if (mSeekerMissileLockConfirm.GetObject()) {
    mSeekerMissileLockConfirm.GetObject()->Touch(0);
  }
  if (mSeekerMissileCrosshair.GetObject()) {
    mSeekerMissileCrosshair.GetObject()->Touch(0);
  }
  if (mTargetFlower.GetObject()) {
    mTargetFlower.GetObject()->Touch(0);
  }
  if (mMissileBracket.GetObject()) {
    mMissileBracket.GetObject()->Touch(0);
  }
  if (mInnerBeamIcon.GetObject()) {
    mInnerBeamIcon.GetObject()->Touch(0);
  }
  if (mLockFire.GetObject()) {
    mLockFire.GetObject()->Touch(0);
  }
  if (mLockDagger.GetObject()) {
    mLockDagger.GetObject()->Touch(0);
  }
  if (mGrapple.GetObject()) {
    mGrapple.GetObject()->Touch(0);
  }
  if (mChargeTickFirst.GetObject()) {
    mChargeTickFirst.GetObject()->Touch(0);
  }
  if (mChargeGauge.mModel.GetObject()) {
    mChargeGauge.mModel.GetObject()->Touch(0);
  }
  if (mScanTargetCenter.GetObject()) {
    mScanTargetCenter.GetObject()->Touch(0);
  }
  if (mScanTargetLeft.GetObject()) {
    mScanTargetLeft.GetObject()->Touch(0);
  }
  if (mScanTargetRight.GetObject()) {
    mScanTargetRight.GetObject()->Touch(0);
  }
  for (rstl::vector< SOuterItemInfo >::const_iterator it = mOuterBeamIconSquares.begin();
       it != mOuterBeamIconSquares.end(); ++it) {
    if (it->mModel.GetObject()) {
      it->mModel.GetObject()->Touch(0);
    }
  }
}

void CTargetingManager::Touch() const { mTargetReticle.Touch(); }

COrbitPointMarker::COrbitPointMarker(int playerIndex)
: mPlayerIndex(playerIndex)
, mZOffset(gpTweakTargeting->GetOrbitPointZOffset())
, mCameraRelativeZ(true)
, mLagAzimuth(0.f)
, mAzimuth(0.f)
, mLagTargetPosition(CVector3f::Zero())
, mLastFreeOrbit(false)
, mInterpolationTimer(0.f)
, mCurrentTime(0.f)
, mOrbitPointModel(gpSimplePool->GetObj("CMDL_OrbitPoint")) {
  mOrbitPointModel.Lock();
}

bool COrbitPointMarker::CheckLoadComplete() { return mOrbitPointModel.TryCache(); }

void COrbitPointMarker::Update(float dt, const CStateManager& mgr) {
  mCurrentTime += dt;
  const CPlayer* player = mgr.GetPlayer(mPlayerIndex);
  CPlayer::EPlayerOrbitState orbitState = player->GetOrbitState();
  const CGameCamera& camera = *mgr.GetCameraManager(mPlayerIndex)->GetCurrentCamera(mgr, true);
  bool freeOrbit = orbitState == CPlayer::kOS_OrbitPoint || orbitState == CPlayer::kOS_OrbitCarcass;

  if (mLastFreeOrbit != freeOrbit) {
    if (orbitState == CPlayer::kOS_OrbitPoint || orbitState == CPlayer::kOS_OrbitCarcass) {
      ResetInterpolationTimer(gpTweakTargeting->GetOrbitPointInterpolateInTime());
      mLagTargetPosition = !mCameraRelativeZ
                               ? player->GetHUDOrbitTargetPosition() + CVector3f(0.f, 0.f, mZOffset)
                               : CVector3f(player->GetHUDOrbitTargetPosition().GetX(),
                                           player->GetHUDOrbitTargetPosition().GetY(),
                                           mZOffset + camera.GetTranslation().GetZ());
      CEulerAngles euler =
          CEulerAngles::FromQuaternion(CQuaternion::FromMatrix(camera.GetTransform()));
      mLagAzimuth = CMath::Deg2Rad(45.f) + euler.GetZ();
    } else if (orbitState == CPlayer::kOS_NoOrbit) {
      ResetInterpolationTimer(gpTweakTargeting->GetOrbitPointInterpolateOutTime());
    } else {
      ResetInterpolationTimer(0.01f);
    }
    mLastFreeOrbit = !mLastFreeOrbit;
  }

  if (mInterpolationTimer > 0.f) {
    mInterpolationTimer = rstl::max_val(0.f, mInterpolationTimer - dt);
  }

  if (!mCameraRelativeZ) {
    CVector3f orbitPosition = player->GetHUDOrbitTargetPosition();
    float targetZ = mZOffset + orbitPosition.GetZ();
    float delta = targetZ - mLagTargetPosition.GetZ();
    if (delta < 0.1f) {
      mLagTargetPosition = orbitPosition + CVector3f(0.f, 0.f, mZOffset);
    } else if (delta < 0.f) {
      mLagTargetPosition =
          CVector3f(orbitPosition.GetX(), orbitPosition.GetY(), mLagTargetPosition.GetZ() - 0.1f);
    } else {
      mLagTargetPosition =
          CVector3f(orbitPosition.GetX(), orbitPosition.GetY(), mLagTargetPosition.GetZ() + 0.1f);
    }
  } else {
    mLagTargetPosition = CVector3f(player->GetHUDOrbitTargetPosition().GetX(),
                                   player->GetHUDOrbitTargetPosition().GetY(),
                                   mZOffset + player->GetHUDOrbitTargetPosition().GetZ());
  }

  if (mLastFreeOrbit) {
    CEulerAngles euler =
        CEulerAngles::FromQuaternion(CQuaternion::FromMatrix(camera.GetTransform()));
    float newAzimuth = CMath::Deg2Rad(45.f) + euler.GetZ();
    float delta = newAzimuth - mAzimuth;
    if (player->IsInFreeLook()) {
      mLagAzimuth += delta;
    }
    mAzimuth = newAzimuth;
  }
}

void COrbitPointMarker::Draw(const CStateManager& mgr) const {
  if ((mLastFreeOrbit || mInterpolationTimer > 0.f) && gpTweakTargeting->GetDrawOrbitPoint()) {
    const_cast< TCachedToken< CModel >& >(mOrbitPointModel).TryCache();
    if (mOrbitPointModel.GetObject() != nullptr) {
      const CGameCamera& camera = *mgr.GetCameraManager(mPlayerIndex)->GetCurrentCamera(mgr, true);
      CTransform4f cameraXf =
          mgr.GetCameraManager(mPlayerIndex)->GetCurrentCameraTransform(mgr, true);
      CGraphics::SetViewPointMatrix(cameraXf);
      float height = static_cast< float >(CGraphics::GetViewport().mHeight);
      float width = static_cast< float >(CGraphics::GetViewport().mWidth);
#ifdef TARGET_PC
      gpRender->SetPerspective(camera.GetRenderFov(), width, height, camera.GetNearClipDistance(),
                               camera.GetFarClipDistance());
#else
      gpRender->SetPerspective(camera.GetFov(), width, height, camera.GetNearClipDistance(),
                               camera.GetFarClipDistance());
#endif

      float scale;
      if (mLastFreeOrbit) {
        scale = 1.f - mInterpolationTimer / gpTweakTargeting->GetOrbitPointInterpolateInTime();
      } else {
        scale = mInterpolationTimer / gpTweakTargeting->GetOrbitPointInterpolateOutTime();
      }

      CColor color = gpTweakTargeting->GetOrbitPointModelColor();
      CTransform4f modelXf = CTransform4f::RotateZ(CRelAngle::FromRadians(mLagAzimuth));
      modelXf.ScaleBy(scale);
      modelXf.AddTranslation(mLagTargetPosition);
      gpRender->SetModelMatrix(modelXf);
      CModel* model = mOrbitPointModel.GetObject();
      CModelFlags flags =
          CModelFlags::Additive(color.WithAlphaModulatedBy(scale)).DepthCompareUpdate(false, false);
      model->Draw(flags);
    }
  }
}

void COrbitPointMarker::ResetInterpolationTimer(float time) { mInterpolationTimer = time; }
