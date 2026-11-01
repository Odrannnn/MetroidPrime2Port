// Host bodies for the MediumIng REL (module 41), slice 1: CMediumIng's and CMediumIngTentacle's
// non-state functions, ported for behaviour from the retail asm
// (build/G2ME01/MediumIng/asm/MetroidPrime/Enemies/CMediumIng.s; docs/module-analysis/MediumIng.md).
// Functions marked `// @ fn_41_<off>` ran in play and were checked against that retail function.
// The upstream file keeps its empty stubs under #ifndef TARGET_PC. The FSM states, SelectTarget*,
// Set*Destination, FindSafeZoneRetreatPoint and FindClosest* are in CMediumIng.cpp's TARGET_PC block.
#include "MetroidPrime/Enemies/CMediumIng.hpp"
#include "port_console.h"

#include "Collision/CCollidableAABox.hpp"
#include "Collision/CCollisionPrimitive.hpp"
#include "Collision/CMaterialFilter.hpp"
#include "Collision/CRayCastResult.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CRandom16.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CLight.hpp"
#include "Kyoto/Graphics/CModelFlags.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Particles/CGenDescription.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CAnimRes.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CGameLight.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/CSafeZoneManager.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWeaponMgr.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/Cameras/CCameraShakerManager.hpp"
#include "MetroidPrime/Enemies/CSurfaceParticleEffect.hpp"
#include "MetroidPrime/PathFinding/CPathFindArea.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSafeZone.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CGameProjectile.hpp"
#include "MetroidPrime/Weapons/CWeapon.hpp"

#include "MetaRender/CCubeRenderer.hpp"

#include <float.h>

namespace {
// Retail's three mist loops and its two one-shots (li r4 in fn_41_6078).
const ushort kSfxMistIdle = 0x20D5; // 0x0000624C / 0x00006520
const ushort kSfxMistIn = 0x20D7;   // 0x00006308
const ushort kSfxMistLoop = 0x20D6; // 0x000063A4
const ushort kSfxMistOut = 0x20D8;  // 0x000064F0
const ushort kSfxTentacle = 0x20D4; // fn_41_9A34 0x00009A70

// The listening-AI list's patterned neighbours in the same area (CalculateSeparation 0x00002068-0x00002094
// and the same walk in the Ing family).
template < typename F >
void ForEachSameAreaPatterned(CStateManager& mgr, const CPatterned& self, F f) {
  const CObjectList& list = mgr.GetObjectListById(kOL_ListeningAi);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CPatterned* other = TCastToConstPtr< CPatterned >(list[i]);
    if (other == nullptr || other == &self || other->GetCurrentAreaId() != self.GetCurrentAreaId()) {
      continue;
    }
    f(*other);
  }
}

CSfxHandle AddLoop(ushort id, const CActor& actor) {
  return CSfxManager::AddEmitter(id, actor.GetTranslation(), actor.GetCurrentAreaId().Value(),
                                 true, true, CSfxManager::kMedPriority);
}

void UpdateLoop(CSfxHandle handle, const CActor& actor) {
  // UpdateEmitter's direction is the transform's forward column (0x28/0x38/0x48).
  CSfxManager::UpdateEmitter(handle, actor.GetTranslation(), actor.GetTransform().GetForward(),
                             0x7f);
}

void RemoveLoop(CSfxHandle& handle) {
  if (handle) {
    CSfxManager::RemoveEmitter(handle);
    handle = CSfxHandle();
  }
}
} // namespace

// The retail dtors are compiler-generated (fn_41_8144/fn_41_81C0, fn_41_A674); the upstream `{}`
// bodies in CMediumIng.cpp are kept.

// @ fn_41_3274
void CMediumIng::UpdateTimers(CStateManager& mgr, float dt) {
  mAggressionTimer += dt;
  // rodata_30 (5); bgt-free form: `fcmpo; ble` skips when not greater (NaN skips).
  if (mAggressionTimer > 5.f) {
    // Taunt chance >= Range(0, rodata_58 100): `fcmpo; cror gt|eq` (NaN gives false).
    mC2 = mData.mTauntChance >= mgr.Random()->Range(0.f, 100.f);
    mAggressionTimer = 0.f;
  }
  mFC8 = mLineOfSightTracker.HasLineOfSight() ? 0.f : mFC8 + dt;
  mFB4 += dt;
  mFB8 += dt;
  mFBC += dt;
  mFC0 += dt;
  mFC4 += dt;
}

// @ fn_41_7804
void CMediumIng::Think(float dt, CStateManager& mgr) {
  CPatterned::Think(dt, mgr);
  if (!GetActive()) {
    return;
  }
  mLineOfSightTracker.Update(dt, mgr);
  if (!mC1) {
    ScanProjectileThreats(mgr, dt);
    UpdateMistState(mgr, dt);
  }
  if (mC0) {
    mSurfaceAlignment.Update(*this, mgr, dt);
  }
  UpdateMistEffect(mgr);

  // The commanded move (BC + 4) against rodata_98 (1e-4); speed mFA8 * rodata_14 (1) times
  // rodata_9C (8) when aggressive, else rodata_30 (5), plus mFAC up.
  const CVector3f& move = GetBodyController()->GetCommandMgr().GetMoveVector();
  if (!close_enough(move, CVector3f::Zero(), 1e-4f)) {
    const float speed = mFA8 * 1.f * (mC2 ? 8.f : 5.f);
    SetVelocityWR(move * speed + CVector3f::Up() * mFAC);
  }
  UpdateTimers(mgr, dt);
  ValidateSafeZone(mgr);
}

// @ fn_41_4644
void CMediumIng::ScanProjectileThreats(CStateManager& mgr, float dt) {
  mD5 = false;
  mD6 = false;
  if (mMistState != 0) {
    return;
  }
  const CVector3f pos = GetTranslation();
  const CVector3f extent(30.f, 30.f, 30.f); // rodata_2C
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, CAABox(pos - extent, pos + extent),
                    CMaterialFilter::MakeInclude(CMaterialList(kMT_Projectile)), this);
  const CAABox bounds = GetBoundingBox();
  const CVector3f center = bounds.GetCenterPoint();
  for (int i = 0; i < nearList.size(); ++i) {
    const CGameProjectile* proj = TCastToConstPtr< CGameProjectile >(mgr.GetObjectById(nearList[i]));
    if (proj == nullptr) {
      continue;
    }
    const CWeaponMode& mode = proj->GetCurrentDamageInfo().GetWeaponMode();
    // A plain power-beam shot that would hurt the solid Ing and keeps it under the no-mist damage
    // threshold is not a threat (`fcmpo; blt` at 0x4870: NaN counts as a threat).
    if (GetDamageVulnerability()->WeaponHits(mode, 0) && mode.GetType() == 0 &&
        !mode.IsCharged() && !mode.IsComboed() &&
        mDamageSinceMist < mData.mNoMistDamageThreshold) {
      continue;
    }
    const CVector3f projPos = proj->GetTranslation();
    CVector3f toCenter = center - projPos;
    CVector3f travel = projPos - proj->GetPreviousPos();
    bool accept = false;
    if (toCenter.IsMagnitudeSafe() && travel.IsMagnitudeSafe()) {
      toCenter.Normalize();
      travel.Normalize();
      // rodata_84 (pi/6): within 30 degrees of heading at the Ing.
      if (CVector3f::Dot(toCenter, travel) >= CMath::FastCosR(0.5235988f)) {
        accept = true;
      }
    }
    if (!accept && bounds.PointInside(projPos)) {
      accept = true;
    }
    if (accept) {
      mD5 = true;
      mD6 = mD6 || mode.IsCharged() || mode.IsComboed() || mode.GetType() == 6;
      mDamageSinceMist = 0.f;
      break;
    }
  }
  // EWeaponType 5 (power bombs) in flight from the player force the strong mist.
  if (mgr.GetWeaponMgr()->GetNumActive(mgr.GetPlayer(0)->GetUniqueId(), EWeaponType(5)) > 0) {
    mD6 = true;
  }
}

// @ fn_41_5DC8
bool CMediumIng::IsMistDecayed() const {
  // 0x5DEC fcmpo + blt: |mF64 - 0| < 1e-5 (rodata_40 0, rodata_8C), else the C7 bit (0x5DF8).
  return CMath::AbsF(mF64) < 1e-5f || mC7; // rodata_8C
}

// @ fn_41_49CC
void CMediumIng::SetMistCollision(bool solid) {
  CMaterialFilter filter = GetMaterialFilter();
  // 0x49F8 lis 0x800 on the high word = 1ull << 59 (kMT_Solid); data_5A8/5AC/5B0 (and
  // 0x5B4/5B8/5BC) = 0x20 Player, 0x23 Projectile, 0x24 Bomb.
  const CMaterialList passThrough(kMT_Player, kMT_Projectile, kMT_Bomb);
  if (solid) {
    filter.IncludeList().Add(kMT_Solid);
    filter.ExcludeList().Remove(passThrough);
  } else {
    filter.IncludeList().Remove(kMT_Solid);
    filter.ExcludeList().Add(passThrough);
  }
  SetMaterialFilter(filter);
}

// @ fn_41_5E0C
bool CMediumIng::IsBlocked(CStateManager& mgr, const CVector3f& offset) {
  // 0x5E34/0x5E48: data_5A0 = 0x20 (Player), data_5A4 = 0x3C (NoPlatformCollision); kFT_Include.
  const CMaterialFilter filter = CMaterialFilter::MakeInclude(
      CMaterialList(kMT_Player, kMT_NoPlatformCollision));
  const CCollisionPrimitive* prim = GetCollisionPrimitive();
  const CTransform4f primXf = GetPrimitiveTransform();
  CAABox box = prim->CalculateAABox(primXf);
  const CAABox moved = prim->CalculateAABox(CTransform4f::Translate(offset) * primXf);
  box.AccumulateBounds(moved.GetMinPoint());
  box.AccumulateBounds(moved.GetMaxPoint());
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, box, filter, this);
  return CGameCollision::DetectCollisionBoolean(mgr, CCollidableAABox(box, GetMaterialList()),
                                                CTransform4f::Identity(), filter, nearList);
}

// @ fn_41_6078
void CMediumIng::UpdateMistState(CStateManager& mgr, float dt) {
  if (!mAlive) {
    return;
  }
  bool want = false;
  if (!mC6) {
    if (mI74 != 0) {
      want = true;
    } else {
      // Host guard: an area without a path area reads as no region.
      CPFArea* area = GetPathArea(mgr);
      if (area != nullptr) {
        rstl::reserved_vector< CPFRegion*, 8 > regions;
        const CAABox box =
            GetBoundingBox().GetTransformedAABox(area->GetTransform().GetQuickInverse());
        if (area->FindRegions(regions, box, 8, mPathFindSearch.GetCreatureMask(), false) != 0) {
          want = true;
        }
      }
      if (!want && (mgr.GetSafeZoneManager()->IsObjectInSafeZone(*this, mgr) || mD5 || mD7)) {
        want = true;
      }
    }
  }
  if (mC7) {
    want = true;
  }

  switch (mMistState) {
  case 0:
    if (!mSfx2) {
      mSfx2 = AddLoop(kSfxMistIdle, *this);
    } else {
      UpdateLoop(mSfx2, *this);
    }
    if (IsMistDecayed() && want) {
      mMistState = 1;
    }
    break;
  case 1:
    mC5 = true;
    mMistState = 2;
    mF60 = 0.f;
    CSfxManager::AddEmitter(kSfxMistIn, GetTranslation(), GetCurrentAreaId().Value(), true, false,
                            CSfxManager::kMedPriority);
    SetMistCollision(false);
    break;
  case 2:
    mD7 = false;
    RemoveLoop(mSfx2);
    if (!mSfx1) {
      mSfx1 = AddLoop(kSfxMistLoop, *this);
    } else {
      UpdateLoop(mSfx1, *this);
    }
    if (!want) {
      mF60 = 0.f;
      mMistState = 3;
    }
    break;
  case 3:
    mF60 += dt;
    if (mF60 > 2.f || mC6) { // rodata_10
      mMistState = 4;
    }
    break;
  case 4:
    if (!IsBlocked(mgr, CVector3f::Zero())) {
      RemoveLoop(mSfx1);
      CSfxManager::AddEmitter(kSfxMistOut, GetTranslation(), GetCurrentAreaId().Value(), true,
                              false, CSfxManager::kMedPriority);
      mSfx2 = AddLoop(kSfxMistIdle, *this);
      mC5 = false;
      mMistState = 0;
      SetMistCollision(true);
    }
    break;
  default:
    break;
  }

  if (HasMist()) {
    SetVisorOrbitableFlags(CVisorParameters::kVOF_All, false);
    SetVisorOrbitableFlags(CVisorParameters::kVOF_Scan, true);
  } else {
    SetVisorOrbitableFlags(CVisorParameters::kVOF_All, true);
  }

  // Mist amount at rodata_1C (4)/s to 1, the decay timer at rodata_38 (0.5)/s to rodata_10 (2).
  if (mC5) {
    mMistAmount = CMath::Min(mMistAmount + 4.f * dt, 1.f);
    mF64 = CMath::Min(mF64 + 0.5f * dt, 2.f);
  } else {
    mMistAmount = CMath::Max(mMistAmount - 4.f * dt, 0.f);
    const bool wasDecayed = IsMistDecayed();
    mF64 = CMath::Max(mF64 - dt, 0.f);
    if (wasDecayed != IsMistDecayed()) {
      mC4 = true;
    }
  }

  if (!mC1) {
    if (CGameLight* light = TCastToPtr< CGameLight >(mgr.ObjectById(mLightId))) {
      const CColor& base = mData.mLightColor;
      const CColor dim = CColor::Modulate(base, CColor(uchar(0x80), uchar(0x80), uchar(0x80), uchar(0xFF)));
      const CColor color = CColor::Lerp(CColor::Black(), CColor::Lerp(base, dim, mMistAmount), mFCC);
      CLight l = CLight::BuildPoint(GetTranslation() + CVector3f::Up() * 4.f, color); // rodata_1C
      l.SetAttenuation(0.f, 1.f / mData.mLightAttenuation, 0.f);
      light->SetLight(l);
    }
  }

  // Model alpha (CPatterned::mColor's alpha byte, 0x44F): psq_st u8 saturates.
  const float alpha = 255.f * (1.f - 0.65f * mMistAmount) * mFD0; // rodata_90, rodata_94
  mColor.SetAlpha(static_cast< uchar >(alpha <= 0.f ? 0.f : (alpha >= 255.f ? 255.f : alpha)));
  ModelData()->AnimationData()->SetEffectState(rstl::string_l("Misting"), HasMist(), mgr);
}

// @ fn_41_7478
bool CMediumIng::Listen(CStateManager& mgr, const CVector3f& pos, EListenNoiseType type) {
  if (mAlive) {
    if (type == 4) {
      mC4 = true;
      return true;
    }
    if (type == 6) {
      UpdateCurrentSafeZone(mgr, pos);
    }
  }
  return false;
}

// @ fn_41_155C
void CMediumIng::UpdateCurrentSafeZone(const CStateManager& mgr, const CVector3f& point) {
  const CVector3f pos = GetTranslation();
  // `fcmpo; cror lt|eq` (NaN fails).
  if (!((point - pos).MagSquared() <= mData.mSafeZoneRangeSq)) {
    return;
  }
  const TUniqueId zone = mgr.GetSafeZoneManager()->PointIsInWhichSafeZone(mgr, point);
  // 0x15D8..0x15E8: retail has no invalid-zone early out (the id is stored as is).
  const float d2 = (point - pos).MagSquared();
  const CScriptSafeZone* current = // 0x160C
      TCastToConstPtr< CScriptSafeZone >(mgr.GetObjectById(mSafeZoneId));
  // Retail keeps the current zone unless this one is nearer (0x1648 `fcmpo; bge` keeps: NaN keeps).
  if (mSafeZoneId == kInvalidUniqueId || current == nullptr ||
      d2 < (current->GetTranslation() - pos).MagSquared()) {
    mSafeZoneId = zone;
  }
}

// @ fn_41_198C
void CMediumIng::CreateMistEffect(CStateManager& mgr, const TLockedToken< CGenDescription >& desc) {
  mMistEffectId = mgr.AllocateUniqueId();
  if (mMistEffectId == kInvalidUniqueId) {
    return;
  }
  CSurfaceParticleEffect* effect = rs_new CSurfaceParticleEffect(
      desc, mMistEffectId, GetCurrentAreaId(), true, rstl::string_l("MediumIngBlobEffect"),
      CTransform4f::Translate(GetTranslation()), GetUniqueId(), 0);
  effect->SetGeneratorRate(0.f);
  effect->SetNextDrawNode(GetUniqueId());
  mgr.AddObject(effect);
}

// @ fn_41_1680
void CMediumIng::UpdateMistEffect(CStateManager& mgr) {
  // Retail tests the object for null (0x16C0) but not its type: mMistEffectId is only ever the
  // CSurfaceParticleEffect.
  CSurfaceParticleEffect* effect =
      static_cast< CSurfaceParticleEffect* >(mgr.ObjectById(mMistEffectId));
  if (effect == nullptr) {
    return;
  }
  if (mC0) {
    if (!mSfx0) {
      mSfx0 = AddLoop(mData.mIngSpotSound, *this);
    } else {
      UpdateLoop(mSfx0, *this);
    }
  } else {
    RemoveLoop(mSfx0);
  }
  float rate = mC0 ? 1.f : 0.f;
  // `fcmpo; ble` past the placement (rate 0 skips it).
  if (rate > 0.f) {
    const CRayCastResult hit =
        mgr.RayStaticIntersection(GetTranslation(), CVector3f::Down(), 10.f, // rodata_20
                                  CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid)));
    CVector3f point = GetTranslation();
    if (hit.IsValid()) {
      point = hit.GetPoint();
    } else {
      rate = 0.f;
    }
    // The normal is the surface-alignment helper's (this + 0x8F8), not the ray's.
    const CVector3f normal = mSurfaceAlignment.GetSurface().GetNormal();
    // rodata_44 (0.95): a steep surface takes the right vector as up (NaN: right).
    const CVector3f& up = normal.GetZ() < 0.95f ? CVector3f::Up() : CVector3f::Right();
    effect->SetTransform(CTransform4f::LookAt(point, point + normal, up));
  }
  effect->SetGeneratorRate(rate);
}

// The flee CanBeNormalized-false arm (0x2298) is dead: retail Flee (0x800FA6xx) returns the
// normalized delta or, when that can't be normalized, the transform's forward column, a unit vector.
// @ fn_41_2024
void CMediumIng::CalculateSeparation(CStateManager& mgr) {
  // CPatterned's steering (0x61C); radius rodata_30 (5) times the model's x scale; speed
  // rodata_38 (0.5); one command per neighbour.
  const float radius = 5.f * GetModelData()->GetScale().GetX();
  CBodyStateCmdMgr& cmds = BodyController()->CommandMgr();
  ForEachSameAreaPatterned(mgr, *this, [&](const CPatterned& other) {
    const CVector3f sep = mSteeringBehaviors.Separation(*this, other.GetTranslation(), radius);
    if (sep.IsMagnitudeSafe()) {
      cmds.DeliverCmd(CBCLocomotionCmd(sep.AsNormalized(), CVector3f::Zero(), 0.5f));
    }
  });
  if (HasMist()) {
    return;
  }
  // Flee the safe zones the player stands near: weight falls from 1 at the zone's radius to 0 at
  // rodata_54 (3) beyond it. Every compare is `fcmpo; bge` (taken on NaN), hence the `!(a < b)`.
  const CVector3f playerPos = mgr.GetPlayer(0)->GetTranslation();
  const rstl::reserved_vector< CSafeZoneManager::SZone, 64 >& zones =
      mgr.GetSafeZoneManager()->GetZones();
  for (int i = 0; i < zones.size(); ++i) {
    const CSafeZoneManager::SZone& zone = zones[i];
    const float hx = zone.mHalfExtents.GetX();
    const float hy = zone.mHalfExtents.GetY();
    const float r = !(hx < hy) ? hx : hy; // 0x21B4 fcmpo hx,hy; bge -> hx
    const float outer = 3.f + r;
    const float dist = (zone.mPosition - playerPos).Magnitude();
    if (!(dist < outer)) { // 0x2210 fcmpo dist,outer; bge skip
      continue;
    }
    float over = dist - r;
    if (!(0.f < over)) { // 0x2224 fcmpo rodata_40,over; bge
      over = 0.f;
    }
    float w = 1.f - over / (outer - r);
    if (!(0.f < w)) { // 0x2250 fcmpo rodata_40,w; bge
      w = 0.f;
    }
    const CVector3f flee = mSteeringBehaviors.Flee(*this, zone.mPosition);
    if (flee.CanBeNormalized()) {
      cmds.DeliverCmd(CBCLocomotionCmd(flee, CVector3f::Zero(), w));
    }
  }
}

// @ fn_41_79C8
void CMediumIng::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  CPatterned::AcceptScriptMsg(mgr, msg);
  switch (msg.GetMessage()) {
  case kSM_Create: {
    // Host guard: no blob effect without its PART (retail locks the token unconditionally).
    if (mData.mIngSpotBlobFx != kInvalidAssetId) {
      CreateMistEffect(mgr, TLockedToken< CGenDescription >(
                                gpSimplePool->GetObj(SObjectTag('PART', mData.mIngSpotBlobFx))));
    }
    BodyController()->SetLocomotionType(static_cast< pas::ELocomotionType >(9));
    BodyController()->Activate(mgr, static_cast< pas::EAnimationState >(-1));
    SetMaterialFilter(CMaterialFilter::MakeInclude(
        CMaterialList(kMT_Player, kMT_Projectile, kMT_Solid)));
    // Retail asks GetResourceTypeById(mTentacleModel) here and drops the answer.
    for (int i = 0; i < 4; ++i) {
      const TUniqueId id = mgr.AllocateUniqueId();
      mTentacleIds.push_back(id);
      mgr.AddObject(rs_new CMediumIngTentacle(
          id, GetCurrentAreaId(), rstl::string_l("Medium Ing Tentacle"),
          CModelData(CAnimRes(mData.mTentacleModel, mData.mTentacleCharacterIndex,
                              GetModelData()->GetScale(), 0, true)),
          mData));
    }
    mLightId = mgr.AllocateUniqueId();
    mgr.AddObject(rs_new CGameLight(mLightId, GetAreaIdForPersistence(), GetActive(),
                                    rstl::string_l("Medium Ing Light"), CTransform4f::Identity(),
                                    GetUniqueId(),
                                    CLight::BuildPoint(CVector3f::Zero(), CColor::Black()), 0, 0,
                                    0.f));
    break;
  }
  case kSM_Alert:
    mD4 = true;
    break;
  case kSM_Escape:
    mE2 = true;
    break;
  case kSM_AreaLoaded:
    mPathFindSearch.SetArea(GetPathArea(mgr));
    break;
  case kSM_Activate:
    if (CEntity* light = mgr.ObjectById(mLightId)) {
      light->SetActive(true);
    }
    break;
  case kSM_Deactivate:
    QuitTeam(mgr);
    if (CEntity* light = mgr.ObjectById(mLightId)) {
      light->SetActive(false);
    }
    break;
  case kSM_Delete:
    QuitTeam(mgr);
    for (int i = 0; i < mTentacleIds.size(); ++i) {
      mgr.DeleteObjectRequest(mTentacleIds[i]);
    }
    mTentacleIds.clear();
    mgr.DeleteObjectRequest(mLightId);
    mgr.DeleteObjectRequest(mMistEffectId);
    break;
  case kSM_XENZ: {
    const CScriptSafeZone* zone =
        TCastToConstPtr< CScriptSafeZone >(mgr.GetObjectById(msg.GetSenderId()));
    if (zone != nullptr && zone->IsHurtful()) {
      mD1 = true;
    }
    ++mSafeZoneReactionCount;
    break;
  }
  case kSM_XEXZ:
    --mSafeZoneReactionCount;
    break;
  case kSM_Damage:
    if (GetHealthInfo()->GetLastDamageWeaponMode().GetType() == 2) {
      mD7 = true;
      mD6 = true;
    }
    break;
  default:
    break;
  }
}

// @ fn_41_75A4
void CMediumIng::PreRender(CStateManager& mgr) {
  CPatterned::PreRender(mgr);
  int matSet = mC3 ? 0 : 1;
  const int last = GetModelData()->GetNumShaders() - 1;
  if (last < matSet) {
    matSet = last;
  }
  SetModelFlags(GetModelFlags().UseShaderSet(matSet));
  SetPreRenderClipped(mC1);
}

// @ fn_41_74F8
void CMediumIng::Render(const CStateManager& mgr) const {
  if (HasMist()) {
    // Only the destination-alpha write is skipped in the scan visor (0x00007530 beq 0x7554);
    // the render and the disable (vt 0x128/0x12C) always run.
    if (mgr.GetPlayerState()->GetActiveVisor(mgr) != CPlayerState::kPV_Scan) {
      gpRender->SetDestinationAlpha(0xFF);
    }
    CPatterned::Render(mgr);
    gpRender->DisableDestinationAlpha();
  } else {
    CPatterned::Render(mgr);
  }
}

// @ fn_41_761C
void CMediumIng::Touch(CActor& actor, CStateManager& mgr) {
  CPatterned::Touch(actor, mgr);
  if (mC5) {
    if (CGameProjectile* proj = TCastToPtr< CGameProjectile >(actor)) {
      if (proj->GetHomingTargetId() == GetUniqueId()) {
        proj->SetMinHomingDistance(FLT_MAX); // rodata_50
      }
    }
  }
  if (mD2 && !mD3) {
    if (CPlayer* player = TCastToPtr< CPlayer >(actor)) {
      // Retail's 0x150C player state (0x000076BC); the host has one player.
      mgr.PlayerState(0)->StaticInterference().AddSource(GetUniqueId(), 0.5f, 2.f); // rodata_38/10
      mIsMakingBigStrike = true;
      mDamageDuration = 2.f; // rodata_10
      mgr.DeliverScriptMsg(CScriptMsg(GetUniqueId(), player->GetUniqueId(), kSM_Damage,
                                      kInvalidUniqueId, kSS_InvalidState));
      mIsMakingBigStrike = false;
      mDamageDuration = 0.f;
      mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(), mData.mMistDamage,
                      CMaterialFilter(CMaterialList(kMT_Solid), CMaterialList(),
                                      CMaterialFilter::kFT_IncludeExclude),
                      CVector3f::Zero());
      mD3 = true;
    }
  }
}

// @ fn_41_1B18
void CMediumIng::Death(CStateManager& mgr, const CVector3f& direction, EScriptObjectState state) {
  CPatterned::Death(mgr, direction, state);
  RemoveLoop(mSfx1);
  RemoveLoop(mSfx2);
  AddMaterial(kMT_GroundCollider, mgr);
}

void CMediumIng::SetFlagE1() { mE1 = true; }

// --- CMediumIngTentacle ---

// @ fn_41_94A4
float CMediumIngTentacle::GetProgress() const {
  const float progress = mElapsedTime / mAttackTime;
  return progress < 1.f ? progress : 1.f; // `bltlr` (NaN gives 1)
}

// @ fn_41_93FC
rstl::optional_object< CAABox > CMediumIngTentacle::GetTouchBounds() const {
  if (GetActive() && GetProgress() < 1.f && mTipBounds) {
    return mTipBounds;
  }
  return rstl::optional_object< CAABox >();
}

// @ fn_41_9F84
void CMediumIngTentacle::Think(float dt, CStateManager& mgr) {
  if (!GetActive()) {
    return;
  }
  ModelData()->AdvanceParticles(GetTransform(), dt, mgr);
  if (mExtending) {
    mElapsedTime += dt;
    const int n = mSplinePoints.size() - 1;
    if (n > 0 &&
        !mgr.RayCollideWorld(mSplinePoints[n - 1], mSplinePoints[n], GetMaterialFilter(), this)) {
      mExtending = false;
    }
  }
  if (mRetracting) {
    mExtending = false;
    mRetractTime += dt;
    if (mRetractTime > 0.5f) { // rodata_38; `ble` skips
      mLengthScale -= dt / 0.5f;
    }
    if (mLengthScale < 0.f) { // 0xA090 `fcmpo; bge` skips: NaN skips
      UpdateShake(mgr, mLengthScale);
    }
  }
}

// @ fn_41_9ED0
void CMediumIngTentacle::UpdateShake(CStateManager& mgr, float dt) {
  const CVector3f pos = mSpline.GetPositionByTime(mAttackMotion.EvaluateAt(GetProgress()));
  mgr.CameraManager(0)->CameraShakerManager()->AddCameraShaker(mImpactShake.NewTranslation(pos),
                                                               mgr, false, false);
  SetActive(false);
}

// @ fn_41_91B0
void CMediumIngTentacle::Touch(CActor& actor, CStateManager& mgr) {
  if (!mCanDamage) {
    return;
  }
  CPlayer* player = TCastToPtr< CPlayer >(actor);
  if (player == nullptr) {
    return;
  }
  const rstl::optional_object< CAABox > playerBounds = player->GetTouchBounds();
  if (!playerBounds || mSplinePoints.size() == 0) {
    return;
  }
  const CVector3f& tip = mSplinePoints.back();
  const CVector3f extent = CVector3f::One() * 2.f; // rodata_10
  if (playerBounds->DoBoundsOverlap(CAABox(tip - extent, tip + extent))) {
    mCanDamage = false;
    mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(), mDamage,
                    CMaterialFilter(CMaterialList(kMT_Solid), CMaterialList(),
                                    CMaterialFilter::kFT_IncludeExclude),
                    CVector3f::Zero());
  }
}

// @ fn_41_9A34
void CMediumIngTentacle::Extend(CStateManager& mgr, const CVector3f& target) {
  CSfxManager::AddEmitter(kSfxTentacle, GetTranslation(), GetCurrentAreaId().Value(), true, false,
                          CSfxManager::kMedPriority);
  mExtending = true;
  mCanDamage = true;
  mLengthScale = 1.f;
  const CVector3f right = GetTransform().GetRight();
  const CVector3f base = GetTranslation() + right * 1.078f; // rodata_AC
  rstl::vector< CVector3f > points;
  points.reserve(4);
  points.push_back(base);
  const CVector3f p1 = base + right * 8.f; // rodata_9C
  points.push_back(p1);
  const CVector3f dir = (target - p1).AsNormalized();
  points.push_back(target - dir * 4.f); // rodata_1C
  points.push_back(target + dir);
  mSpline.Initialise(points);
}

// @ fn_41_9CE0
void CMediumIngTentacle::Retract(CStateManager& mgr) {
  mExtending = false;
  mRetracting = false;
  mCanDamage = false;
  mElapsedTime = 0.f;
  mRetractTime = 0.f;
  const CVector3f right = GetTransform().GetRight();
  const CVector3f base = GetTranslation() + right * 1.078f; // rodata_AC
  rstl::vector< CVector3f > points;
  points.reserve(4);
  points.push_back(base);
  points.push_back(base + right * 1.f);
  points.push_back(base + right * 2.f);
  points.push_back(base + right * 3.f);
  mSpline.Initialise(points);
}

// @ fn_41_94C8
void CMediumIngTentacle::PreRender(CStateManager& mgr) {
  CActor::PreRender(mgr);
  // White while extending, else black; alpha 255 * max(lengthScale, 0) (`fcmpo; bge`: NaN
  // clamps to 0).
  CColor color = mExtending ? CColor::White() : CColor::Black();
  float alpha = mLengthScale;
  if (!(0.f < alpha)) {
    alpha = 0.f;
  }
  alpha *= 255.f;
  color.SetAlpha(static_cast< uchar >(alpha >= 255.f ? 255.f : (alpha > 0.f ? alpha : 0.f)));
  SetModelFlags(CModelFlags(CModelFlags::kT_Blend, 0, CModelFlags::EFlags(3), color));

  CAnimData* animData = ModelData()->AnimationData();
  if (animData == nullptr) {
    return; // host guard
  }
  CPoseAsTransforms_Linear& pose = animData->Pose();
  const int n = pose.GetElements().size() - 2;
  mSplinePoints.clear();
  if (n <= 0) {
    return; // host guard: retail divides by n
  }
  mSplinePoints.reserve(n + 1);
  const float step = mAttackMotion.EvaluateAt(GetProgress()) / static_cast< float >(n);
  float retract = mRetractTime;
  if (!(0.f < retract)) { // `fcmpo; bge` at 0x9608: NaN clamps to 0
    retract = 0.f;
  }
  const float swell = 5.f * (retract / 0.5f); // rodata_30, rodata_38
  const CMatrix3f rot = CMatrix3f::RotateZ(CRelAngle::FromRadians(1.5707964f)); // rodata_A8
  const CTransform4f invXf = GetTransform().GetInverse();
  CVector3f prev = mSpline.GetPositionByTime(0.f);
  float t = 0.f;
  for (int i = 0; i < n; ++i) {
    const CVector3f cur = mSpline.GetPositionByTime(t + step);
    if (i == 0) {
      mSplinePoints.push_back(prev);
    }
    mSplinePoints.push_back(cur);
    t += step;
    const CTransform4f xf = close_enough(prev, cur, 1e-4f) // rodata_98
                                ? CTransform4f::Translate(prev)
                                : CTransform4f::LookAt(prev, cur, CVector3f::Up());
    const CTransform4f local = invXf * xf;
    // Per-joint swell 1 + s * (rodata_78 0.25 + i/n); the local rotation is dropped, as in retail.
    const float scale = 1.f + swell * (0.25f + static_cast< float >(i) / static_cast< float >(n));
    CPoseAsTransforms_Linear::CElementType& elem = pose.PortElement(i + 2);
    elem.mRotation = rot * CMatrix3f::Scale(1.f, scale, scale);
    elem.mOffset = local.GetTranslation();
    prev = cur;
  }
  CAABox bounds = CAABox::MakeMaxInvertedBox();
  for (int i = 0; i < mSplinePoints.size(); ++i) {
    bounds.AccumulateBounds(mSplinePoints[i]);
  }
  const CVector3f pad = CVector3f::One() * 10.f; // rodata_20
  const CVector3f lo = bounds.GetMinPoint() - pad;
  const CVector3f hi = bounds.GetMaxPoint() + pad;
  bounds.AccumulateBounds(lo);
  bounds.AccumulateBounds(hi);
  mTipBounds = bounds;
}

// The console's `var` fields, so the slice-1 arms run without the slice-2 states:
// ming.c1 is the flag SubStart clears (0xcfc bit 0x40: set, the Ing is pre-render clipped and
// cannot be shot); ming.extend / ming.retract do what ArmAttack does to a tentacle: Extend towards
// the player (one unit above its feet), or Retract and set mRetracting (0x41CC).
bool CMediumIng::PortConsoleVar(CEntity& ent, const char* name, const int* value, int& current) {
  CMediumIng* ing = dynamic_cast< CMediumIng* >(&ent); // a host-only helper
  if (ing == nullptr || strncmp(name, "ming.", 5) != 0) {
    return false;
  }
  const char* n = name + 5;
  // Float timers/ranges (int values as float units) the trigger arms read.
#define MING_F(field, label)                                                                       \
  if (strcmp(n, label) == 0) {                                                                     \
    if (value != nullptr) {                                                                        \
      ing->field = static_cast< float >(*value);                                                   \
    }                                                                                              \
    current = static_cast< int >(ing->field);                                                      \
    return true;                                                                                   \
  }
  MING_F(mFB0, "fb0") MING_F(mFB4, "fb4") MING_F(mFB8, "fb8") MING_F(mFBC, "fbc") MING_F(mFC0, "fc0")
  MING_F(mFC4, "fc4") MING_F(mFC8, "fc8") MING_F(mMistAmount, "mist")
#undef MING_F
#define MING_B(field, label)                                                                       \
  if (strcmp(n, label) == 0) {                                                                     \
    if (value != nullptr) {                                                                        \
      ing->field = *value != 0;                                                                    \
    }                                                                                              \
    current = ing->field ? 1 : 0;                                                                  \
    return true;                                                                                   \
  }
  MING_B(mC0, "c0") MING_B(mC1, "c1") MING_B(mC2, "c2") MING_B(mC3, "c3") MING_B(mC4, "c4")
  MING_B(mC5, "c5") MING_B(mC6, "c6") MING_B(mC7, "c7") MING_B(mD0, "d0") MING_B(mD1, "d1")
  MING_B(mD2, "d2") MING_B(mD3, "d3") MING_B(mD4, "d4") MING_B(mD5, "d5") MING_B(mD6, "d6")
  MING_B(mInPosition, "inpos") MING_B(mD7, "d7") MING_B(mE0, "e0") MING_B(mE1, "e1") MING_B(mE2, "e2")
#undef MING_B
  if (strcmp(n, "los") == 0) { // read-only: the line-of-sight tracker's last result
    current = ing->mLineOfSightTracker.HasLineOfSight() ? 1 : 0;
    return true;
  }
  if (strcmp(n, "safezone") == 0) { // mSafeZoneId (-1 = none)
    if (value != nullptr) {
      ing->mSafeZoneId = *value < 0 ? kInvalidUniqueId : TUniqueId(static_cast< ushort >(*value));
    }
    current = static_cast< int >(ing->mSafeZoneId.value);
    return true;
  }
  CStateManager* vmgr = PortConsole::VarManager();
  if (vmgr != nullptr) {
    // Calls with no trigger/code name: ming.quitteam / jointeam / validate run the body once.
    if (strcmp(n, "quitteam") == 0) {
      if (value != nullptr && *value != 0) {
        ing->QuitTeam(*vmgr);
      }
      current = ing->mTeamAiMgrId.value;
      return true;
    }
    if (strcmp(n, "jointeam") == 0) {
      if (value != nullptr && *value != 0) {
        ing->JoinTeam(*vmgr);
      }
      current = ing->mTeamAiMgrId.value;
      return true;
    }
    if (strcmp(n, "validate") == 0) {
      if (value != nullptr && *value != 0) {
        ing->ValidateSafeZone(*vmgr);
      }
      current = ing->mSafeZoneId.value;
      return true;
    }
    // ming.conn is a bitmask of the connections the pattern/team triggers look for, all pointing at
    // the Ing itself: 1 Approach/Follow, 2 GeneratorConnection/Follow, 4 Active/Play (the team
    // link; clearing it keeps the originals and setting it restores them).
    if (strcmp(n, "conn") == 0) {
      static rstl::vector< SConnection > sSavedTeam;
      rstl::vector< SConnection >& list = ing->ConnectionList();
      auto has = [&](EScriptObjectState s, EScriptObjectMessage m) {
        for (const SConnection& c : list) {
          if (c.state == s && c.msg == m) {
            return true;
          }
        }
        return false;
      };
      auto drop = [&](EScriptObjectState s, EScriptObjectMessage m, bool save) {
        for (size_t i = list.size(); i-- > 0;) {
          if (list[i].state == s && list[i].msg == m) {
            if (save) {
              sSavedTeam.push_back(list[i]);
            }
            list.erase(list.begin() + i);
          }
        }
      };
      if (value != nullptr) {
        const int bits = *value;
        drop(kSS_Approach, kSM_Follow, false);
        drop(kSS_GeneratorConnection, kSM_Follow, false);
        if ((bits & 1) != 0) {
          list.push_back(SConnection(kSS_Approach, kSM_Follow, ing->GetEditorId()));
        }
        if ((bits & 2) != 0) {
          list.push_back(SConnection(kSS_GeneratorConnection, kSM_Follow, ing->GetEditorId()));
        }
        if ((bits & 4) == 0) {
          drop(kSS_Active, kSM_Play, true);
        } else if (!has(kSS_Active, kSM_Play)) {
          for (const SConnection& c : sSavedTeam) {
            list.push_back(c);
          }
          sSavedTeam.clear();
        }
      }
      current = (has(kSS_Approach, kSM_Follow) ? 1 : 0) |
                (has(kSS_GeneratorConnection, kSM_Follow) ? 2 : 0) |
                (has(kSS_Active, kSM_Play) ? 4 : 0);
      return true;
    }
    if (strncmp(n, "dest", 4) == 0 && n[4] != '\0' && n[5] == '\0') { // destx/desty/destz
      CVector3f& d = ing->mDestPos;
      if (value != nullptr) {
        const float v = static_cast< float >(*value);
        n[4] == 'x' ? d.SetX(v) : n[4] == 'y' ? d.SetY(v) : d.SetZ(v);
      }
      current = static_cast< int >(n[4] == 'x' ? d.GetX() : n[4] == 'y' ? d.GetY() : d.GetZ());
      return true;
    }
    // ming.connrt / ming.conndg <unique id> append a Retreat ('RTRT') / DeGenerate ('DGNR')
    // connection to that object (the Find*Actor arms); an id >= 0xFFFF connects to an editor id no
    // object has; -1 removes every connection of that state.
    if (strcmp(n, "connrt") == 0 || strcmp(n, "conndg") == 0) {
      const EScriptObjectState st = n[4] == 'r' ? kSS_Retreat : kSS_DeGenerate;
      rstl::vector< SConnection >& list = ing->ConnectionList();
      if (value != nullptr && *value < 0) {
        for (size_t i = list.size(); i-- > 0;) {
          if (list[i].state == st) {
            list.erase(list.begin() + i);
          }
        }
      } else if (value != nullptr) {
        const CEntity* target =
            *value >= 0xFFFF ? nullptr
                             : vmgr->GetObjectById(TUniqueId(static_cast< ushort >(*value)));
        list.push_back(SConnection(st, kSM_Follow,
                                   target != nullptr ? target->GetEditorId() : TEditorId(0x00ABCDEF)));
      }
      int count = 0;
      for (const SConnection& c : list) {
        count += c.state == st ? 1 : 0;
      }
      current = count;
      return true;
    }
    if (strcmp(n, "destme") == 0) { // mDestPos = the Ing's own position
      if (value != nullptr && *value != 0) {
        ing->mDestPos = ing->GetTranslation();
      }
      current = 0;
      return true;
    }
  }
  if (strcmp(n, "team") == 0) { // mTeamAiMgrId
    if (value != nullptr) {
      ing->mTeamAiMgrId = *value < 0 ? kInvalidUniqueId : TUniqueId(static_cast< ushort >(*value));
    }
    current = static_cast< int >(ing->mTeamAiMgrId.value);
    return true;
  }
  return false;
}

bool CMediumIngTentacle::PortConsoleVar(CEntity& ent, const char* name, const int* value,
                                        int& current) {
  CMediumIngTentacle* t = dynamic_cast< CMediumIngTentacle* >(&ent);
  CStateManager* mgr = PortConsole::VarManager();
  if (t == nullptr || mgr == nullptr) {
    return false;
  }
  if (strcmp(name, "ming.extend") == 0) {
    if (value != nullptr && *value != 0) {
      t->Extend(*mgr, mgr->GetPlayer(0)->GetTranslation() + CVector3f::Up());
    }
    current = t->mExtending ? 1 : 0;
    return true;
  }
  if (strcmp(name, "ming.retract") == 0) {
    if (value != nullptr && *value != 0) {
      t->Retract(*mgr);
      t->mRetracting = true;
    }
    current = t->mRetracting ? 1 : 0;
    return true;
  }
  return false;
}

namespace {
const bool sMediumIngConsoleVars = PortConsole::RegisterVar(&CMediumIng::PortConsoleVar);
const bool sMediumIngTentacleConsoleVars =
    PortConsole::RegisterVar(&CMediumIngTentacle::PortConsoleVar);
} // namespace
