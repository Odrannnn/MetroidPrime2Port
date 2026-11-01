// Port-only: the IngBoostBallGuardian REL's second class (module 30), the boost attack's rolling
// ball (include/MetroidPrime/Enemies/PortCBoostBallGuardianBall.hpp). The behaviour is ported
// from the module's own bodies at 0x11CD8..0x164D0; the addresses are the module's own.
#include "MetroidPrime/Enemies/PortCBoostBallGuardianBall.hpp"

#include "Collision/CCollidableSphere.hpp"
#include "Collision/CCollisionInfo.hpp"
#include "Collision/CMaterialFilter.hpp"
#include "Collision/CMaterialList.hpp"
#include "Collision/CollisionUtil.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CColor.hpp"
#include "Kyoto/Graphics/CModelFlags.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "Kyoto/Math/CMayaSpline.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Math/CSphere.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "Kyoto/Particles/CGenDescription.hpp"
#include "Kyoto/Particles/CParticleSpawnSystem.hpp"
#include "Kyoto/Particles/CSpawnSystemDescription.hpp"
#include "Kyoto/SObjectTag.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#include "MetroidPrime/CAxisAngle.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/CObjectList.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/Enemies/PortBoostGuardianShared.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAIHint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptCoverPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptDamageableTrigger.hpp"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

// kSM_Create indexes the three phases as an array (fn_30_1466C).
static_assert(offsetof(CBoostBallGuardianData, mPhase1) ==
                  offsetof(CBoostBallGuardianData, mPhase0) + sizeof(CIngBoostBallGuardianPhase) &&
              offsetof(CBoostBallGuardianData, mPhase2) ==
                  offsetof(CBoostBallGuardianData, mPhase1) + sizeof(CIngBoostBallGuardianPhase),
              "boost phases must be adjacent");

namespace {

// 0x000159B4..0x00015A04: the caller's list (empty when null) with kMT_Solid (lbl_30_data_C04)
// and kMT_Projectile (lbl_30_data_C08) added.
CMaterialList BallMaterials(const CMaterialList* matList) {
  CMaterialList list(kMT_Solid, kMT_Projectile);
  if (matList != nullptr) {
    list.Add(*matList);
  }
  return list;
}

// 0x00015ABC..0x00015B14: radius 1.0 when the model data has neither an animation nor a static
// model, else half the model bounds' height.
float BallRadius(const CModelData& mData, const CModelData* own) {
  if (mData.GetAnimationData() == nullptr && !mData.HasNormalModel()) {
    return 1.f;
  }
  const CAABox bounds = own->GetBounds();
  return (bounds.GetMaxPoint().GetZ() - bounds.GetMinPoint().GetZ()) * 0.5f;
}

// fn_30_16238: a particle generator for an asset, by type: 'PART' makes a CElementGen
// (kMOT_Normal, kOSF_One; 0x00016318), 'SPSC' a CParticleSpawnSystem (kOSF_One, no model
// lights; 0x000163E0); an invalid id or any other type gives none.
// @ fn_30_16238
CParticleGen* MakeBallEffect(CAssetId id, FourCC type) {
  if (id == kInvalidAssetId) {
    return nullptr;
  }
  switch (type) {
  case 'PART': {
    TToken< CGenDescription > desc(gpSimplePool->GetObj(SObjectTag('PART', id)));
    return rs_new CElementGen(desc, CElementGen::kMOT_Normal, CElementGen::kOSF_One);
  }
  case 'SPSC': {
    TToken< CSpawnSystemDescription > desc(gpSimplePool->GetObj(SObjectTag('SPSC', id)));
    return rs_new CParticleSpawnSystem(desc, CElementGen::kOSF_One, false);
  }
  default:
    return nullptr;
  }
}

} // namespace

// @ fn_30_15924
CBoostBallGuardianBall::CBoostBallGuardianBall(
    TUniqueId uid, const rstl::string& name, const CEntityInfo& info, const CTransform4f& xf,
    const CVector3f& pos, const CModelData& modelData, const CActorParameters& actParams,
    TUniqueId ownerId, const CBoostBallGuardianData& data, int phaseIndex,
    const CDamageVulnerability& vulnerability, const rstl::vector< TUniqueId >& modifyTargets,
    const CMaterialList* matList, const CHealthInfo* health)
: CPhysicsActor(uid, name, info, 0, xf, modelData, BallMaterials(matList), CAABox::MakeNullBox(),
                SMoverData(data.mMass, CVector3f::Zero(), CAxisAngle::Identity(),
                           CVector3f::Zero(), CAxisAngle::Identity()),
                actParams, skDefaultStepData) // 0x00015A7C
, mPhaseIndex(phaseIndex)                     // 0x00015A98
, mState(0)                                   // 0x00015A9C
, mHomePos(GetTranslation())                  // 0x00015AA0
, mData(data)                                 // 0x00015AB8: fn_30_10250
, mSphere(CSphere(CVector3f::Zero(), BallRadius(modelData, GetModelData())),
          GetMaterialList())                  // 0x00015B38
, mOwnerId(ownerId)                           // 0x00015B6C
, mCollidedId(kInvalidUniqueId)
, mLastCollidedId(kInvalidUniqueId)
, mId4F6(kInvalidUniqueId)
, mTouchedId(kInvalidUniqueId)                // 0x00015B8C
, mModifyTargets(modifyTargets)               // 0x00015B90: fn_30_160D0
, mDamageTimer(1.f)                           // 0x00015BAC
, mSpeedDelta(0.f)
, mSplineDuration(mData.mSpeed.GetDuration()) // 0x00015BC8
, mHalfHeight(0.f)
, mSplineTime(0.f)
, mShieldTime(0.4f)                                // 0x00015C00
, mRollSpeed(0.f)
, mSplineEndTime(0.f)
, mStateTime(0.f)
, mShrink(1.f)                                // 0x00015C14
, mScale(CVector3f::One())                    // 0x00015C1C
, mBounceNormal(CVector3f::Zero())
, mLastVelocity(CVector3f::Zero())
, mBounces(0)                                   // 0x00015C60
, mMissBounces(0)
, mCoverBounces(0)
, mTrailGen(MakeBallEffect(mData.mTrailEffect, 'SPSC'))        // 0x00015C70: data+0xA8
, mShieldGen(MakeBallEffect(mData.mShieldEffect, 'PART'))      // 0x00015C84: data+0xAC
, mChargeUpGen(MakeBallEffect(mData.mChargeUpEffect, 'PART'))  // 0x00015C98: data+0xB0
, mVulnerability(vulnerability)               // 0x00015CA8: fn_30_AD4
, mRollingSfx()                               // 0x00015CBC
, mDirChosen(false)                              // 0x00015CCC..0x00015D28: 0x5D8 all clear
, mRandomDir(false)
, mEffectBounds(false)
, mBounced(false)
, mRolling(false)
, mDone(false)
, mOnSpline(false)
, mHalted(false)
, m5D9_80(false)                              // 0x00015D2C
, mGrown(false)
, mIntoBallSfx(true)                               // 0x00015D48
, mLoopSfx(true) {                             // 0x00015D54
  if (health != nullptr) {
    mHealthInfo = *health; // 0x00015D68: fn_30_15FC4
  }
  SetMomentumWR(CVector3f::Zero()); // 0x00015D74: 0x1C0
  if (mTrailGen.get() != nullptr) {
    mTrailGen->SetParticleEmission(true); // 0x00015DA4
  }
  if (mShieldGen.get() != nullptr) {
    mShieldGen->SetParticleEmission(true);
  }
  if (mChargeUpGen.get() != nullptr) {
    mChargeUpGen->SetParticleEmission(true);
  }
  SetCoefficientOfRestitutionModifier(1.f); // 0x00015DF4
  if (pos.GetX() != 0.f || pos.GetY() != 0.f || pos.GetZ() != 0.f) {
    // 0x00015E5C: the sphere's local box; the ball sits half its height (plus 0.02) above pos.
    const CAABox local = mSphere.CalculateLocalAABox();
    mHalfHeight = (local.GetMaxPoint().GetZ() - local.GetMinPoint().GetZ()) * 0.5f;
    const CVector3f start(pos.GetX(), pos.GetY(), pos.GetZ() + (0.02f + mHalfHeight));
    SetTranslation(start); // 0x00015EA4
    mHomePos = start;
  }
  // 0x00015EC0: with six knots or more the roll ends at the third-from-last knot's time, but no
  // later than the duration less 0.9.
  const int knots = static_cast< int >(mData.mSpeed.GetKnotCount());
  if (knots >= 6) {
    mSplineEndTime = mData.mSpeed.GetKnots()[knots - 3].GetTime();
    const float latest = mData.mSpeed.GetDuration() - 0.4f - 0.5f;
    if (!(mSplineEndTime < latest)) { // 0x15F18 fcmpo; bge
      mSplineEndTime = latest;
    }
  }
  // 0x00015F2C: the filter keeps its include list and excludes kMT_Character (lbl_30_data_C0C).
  CMaterialList exclude = GetMaterialFilter().GetExcludeList();
  exclude.Add(kMT_Character);
  SetMaterialFilter(
      CMaterialFilter::MakeIncludeExclude(GetMaterialFilter().GetIncludeList(), exclude));
}

// @ fn_30_11CD8
CBoostBallGuardianBall::~CBoostBallGuardianBall() {}

// The DOL's TypesMatch for type 148 (TypesMatch.cpp's TYPES_MATCH_IMPL shape).
CEntity* CBoostBallGuardianBall::TypesMatch(int typeId) const {
  if (typeId == kTypeId) {
    return const_cast< CBoostBallGuardianBall* >(this);
  }
  if (typeId > kTypeId) {
    return nullptr;
  }
  return CPhysicsActor::TypesMatch(typeId);
}

// fn_800979F0: TryCast(entity, 0x94).
template <>
CBoostBallGuardianBall* TCastToPtr< CBoostBallGuardianBall >(CEntity* entity) {
  return static_cast< CBoostBallGuardianBall* >(TryCast(entity, CBoostBallGuardianBall::kTypeId));
}
// fn_30_13E68: the actor's own draw, plus a forced one while the effects' bounds stand in for
// the render bounds and the ball is in view.
// @ fn_30_13E68
void CBoostBallGuardianBall::AddToRenderer(const CStateManager& mgr) const {
  CActor::AddToRenderer(mgr);
  if (mEffectBounds && mgr.IsActorVisible(*this)) {
    EnsureRendered(mgr);
  }
}

// fn_30_141B8: the model, then the three effects; through the dark visor with alpha modulation
// and destination alpha.
// @ fn_30_141B8
void CBoostBallGuardianBall::Render(const CStateManager& mgr) const {
  CPhysicsActor::Render(mgr);
  const bool darkVisor = mgr.GetPlayerState()->GetActiveVisor(mgr) == CPlayerState::kPV_Dark;
  if (darkVisor) {
    CElementGen::sEnableAlphaModulation = darkVisor;
    gpRender->SetDestinationAlpha(255);
  }
  if (mTrailGen.get() != nullptr) {
    mTrailGen->Render();
  }
  if (mShieldGen.get() != nullptr) {
    mShieldGen->Render();
  }
  if (mChargeUpGen.get() != nullptr) {
    mChargeUpGen->Render();
  }
  if (darkVisor) {
    gpRender->DisableDestinationAlpha();
    CElementGen::sEnableAlphaModulation = false;
  }
}

// fn_30_13ECC: the render bounds are the union of the effects' bounds, or the point at the
// ball's position when none has any.
// @ fn_30_13ECC
void CBoostBallGuardianBall::PreRenderAllViewports(CStateManager& mgr) {
  rstl::optional_object< CAABox > bounds;
  if (mTrailGen.get() != nullptr) {
    bounds = mTrailGen->GetBounds(); // fn_30_CD78: optional_object assignment
  }
  if (mShieldGen.get() != nullptr) {
    const rstl::optional_object< CAABox > shield = mShieldGen->GetBounds();
    if (shield.valid()) {
      if (bounds.valid()) {
        bounds.data().AccumulateBounds(shield.data().GetMinPoint());
        bounds.data().AccumulateBounds(shield.data().GetMaxPoint());
      } else {
        bounds = shield;
      }
    }
  }
  if (mChargeUpGen.get() != nullptr) {
    const rstl::optional_object< CAABox > chargeUp = mChargeUpGen->GetBounds();
    if (chargeUp.valid()) {
      if (bounds.valid()) {
        bounds.data().AccumulateBounds(chargeUp.data().GetMinPoint());
        bounds.data().AccumulateBounds(chargeUp.data().GetMaxPoint());
      } else {
        bounds = chargeUp;
      }
    }
  }
  if (bounds.valid()) {
    SetOtherBounds(bounds.data());
    SetRenderBounds(bounds.data());
    mEffectBounds = true;
  } else {
    const CVector3f pos = GetTranslation();
    const CAABox point(pos, pos);
    SetOtherBounds(point);
    SetRenderBounds(point);
    mEffectBounds = false;
  }
  UpdatePortalSystemState(mgr);
}


// @ fn_30_1548C
void CBoostBallGuardianBall::Think(float dt, CStateManager& mgr) {
  switch (mState) {
  case 0:
    GrowIn(dt, mgr);
    if (mGrown) {
      UpdateEffects(dt, mgr, 4); // 0x00015514: the charge-up only
    }
    if (!mRandomDir) {
      UpdateScale(dt, mgr);
    }
    break;
  case 1:
    if (mOnSpline) {
      mSplineTime += dt;
    }
    // 0x00015550: below the damage speed or past the spline's end the shield fades out.
    if ((GetVelocityWR().Magnitude() < mData.mMinDamageSpeed || mSplineTime > mSplineEndTime) &&
        mShieldGen.get() != nullptr) {
      mShieldTime = mShieldTime - dt > 0.f ? mShieldTime - dt : 0.f; // 0x15590 fcmpo 0,v; bge
      if (mShieldGen.get() != nullptr) {
        mShieldGen->SetModulationColor(CColor::White().WithAlphaOf(mShieldTime / 0.4f));
      }
    }
    UpdateEffects(dt, mgr, 7);
    Roll(dt, mgr);
    break;
  case 2:
    Shrink(dt);
    UpdateScale(dt, mgr);
    break;
  default:
    break;
  }

  // 0x00015638: contact damage to the actor CollidedWith recorded, at most every 0.2 s.
  mDamageTimer += dt;
  if (mTouchedId != kInvalidUniqueId) {
    if (mDamageTimer > 0.2f) {
      mDamageTimer = 0.f;
      bool firstPerson = true;
      CVector3f dir = CVector3f::Zero();
      const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mTouchedId));
      if (player != nullptr) {
        // 0x000156C0: the spawned state wins over the transition state (as in TargetIsBall).
        const CPlayer::EPlayerMorphBallState morph =
            player->GetSpawnedMorphballState() != CPlayer::kMS_Unmorphed
                ? CPlayer::kMS_Unmorphed
                : player->GetMorphballTransitionState();
        if (morph == CPlayer::kMS_Morphed) {
          firstPerson = false;
        }
        CVector3f toPlayer = player->GetTranslation() - GetTranslation();
        if (toPlayer.CanBeNormalized()) {
          toPlayer.Normalize();
          // 0x00015734: a player within 60 degrees of the heading is pushed 60 degrees aside,
          // towards the side it is on.
          if (CVector3f::GetAngleDiff(GetVelocityWR(), toPlayer) < 1.0471976f) {
            const float side =
                CVector3f::Dot(GetTransform().GetColumn(kDX), toPlayer) > 0.0 ? 1.f : -1.f;
            const CTransform4f rot =
                CTransform4f::RotateZ(CRelAngle::FromRadians(0.017453292f * (60.f * side)));
            CVector3f push = rot * GetVelocityWR();
            push.SetZ(0.f);
            if (push.CanBeNormalized()) {
              dir = push.AsNormalized();
            }
          }
        }
      }
      const CDamageInfo& info = firstPerson ? mData.mDamageToFirstPerson : mData.mDamageToMorphBall;
      mgr.ApplyDamage(GetUniqueId(), mTouchedId, GetUniqueId(), info,
                      CMaterialFilter(CMaterialList(kMT_Solid), CMaterialList(),
                                      CMaterialFilter::kFT_IncludeExclude), // lbl_30_data_C10
                      dir);
    }
    mTouchedId = kInvalidUniqueId;
  }
  mLastVelocity = GetVelocityWR(); // 0x000158C8
  UpdateRollingSfx();
}

// fn_30_137C0: the grow-in. The first time it picks a heading (the actor's forward, or a random
// yaw once a roll has run), steering round anything in the way, then fades in over 0.3 s and
// starts the roll once the speed spline is positive.
// @ fn_30_137C0
void CBoostBallGuardianBall::GrowIn(float dt, CStateManager& mgr) {
  const CVector3f pos = GetTranslation();
  const CVector3f& playerPos = mgr.GetPlayer(0)->GetTranslation(); // mgr+0x14FC
  const CVector3f target(playerPos.GetX(), playerPos.GetY(), pos.GetZ()); // unread (0x00013814)
  CTransform4f xf(CTransform4f::Identity());
  bool turn = false;
  if (!mDirChosen) {
    CVector3f dir;
    if (!mRandomDir) {
      dir = GetTransform().GetColumn(kDY);
    } else {
      const float yaw = mgr.Random()->Range(0.f, 360.f);
      dir = CTransform4f::RotateZ(CRelAngle::FromRadians(0.017453292f * yaw)).GetColumn(kDY);
    }
    const CVector3f heading = dir;
    mLastVelocity = heading;
    if (!IsPathClear(mgr, heading)) {
      CVector3f out = CVector3f::Zero();
      if (Steer(mgr, heading, out, 0.f)) {
        xf = CTransform4f::LookAt(pos, pos + out, CVector3f::Up());
        turn = true;
      }
    }
    mDirChosen = true;
  }
  if (turn) {
    xf.SetTranslation(pos);
    SetTransform(xf);
  }

  mStateTime += dt;
  mShrink = 1.f - mStateTime / 0.3f;
  if (mStateTime > 0.3f) {
    if (mLoopSfx) {
      PlaySound(1, true);
      mLoopSfx = false;
    }
    mGrown = true;
    mSplineTime += dt;
    mShrink = 0.f;
    const float speed = EvaluateSpeed(mSplineTime);
    if (speed > 0.f) {
      RemoveMaterial(kMT_Immovable, mgr); // 0x00013A9C: 0x2B
      mState = 1;
      mRolling = true;
      mRandomDir = true;
      SetVelocityWR(GetTransform().GetColumn(kDY) * speed);
      mRollSpeed = speed;
      PlaySound(2, true);
      PlaySound(3, true);
    }
  } else if (mIntoBallSfx) {
    PlaySound(0, true);
    mIntoBallSfx = false;
  }
  mScale.SetZ(1.f - mShrink); // 0x00013B54: 0x53C
}

// fn_30_13C78: places the effects on the ball and updates them; the mask picks the trail (1),
// the shield (2) and the charge-up (4). The trail takes a local translation, the other two a
// global one.
// @ fn_30_13C78
void CBoostBallGuardianBall::UpdateEffects(float dt, CStateManager& mgr, uint mask) {
  if (!GetActive()) {
    return;
  }
  const CTransform4f rot = GetTransform().GetRotation();
  const CVector3f pos = GetTranslation();
  const CVector3f scale = CVector3f::One();
  if ((mask & 1) != 0 && mTrailGen.get() != nullptr) {
    mTrailGen->SetOrientation(rot);
    mTrailGen->SetTranslation(pos);
    mTrailGen->SetGlobalScale(scale);
    mTrailGen->Update(dt);
  }
  if ((mask & 2) != 0 && mShieldGen.get() != nullptr) {
    mShieldGen->SetOrientation(rot);
    mShieldGen->SetGlobalTranslation(pos);
    mShieldGen->SetGlobalScale(scale);
    mShieldGen->Update(dt);
  }
  if ((mask & 4) != 0 && mChargeUpGen.get() != nullptr) {
    mChargeUpGen->SetOrientation(rot);
    mChargeUpGen->SetGlobalTranslation(pos);
    mChargeUpGen->SetGlobalScale(scale);
    mChargeUpGen->Update(dt);
  }
}

// fn_30_152D4: sinks the ball into the floor by mShrink of its half height, squashes the model
// and fades it (kT_Blend, black at alpha 1 - mShrink).
// @ fn_30_152D4
void CBoostBallGuardianBall::UpdateScale(float dt, CStateManager& mgr) {
  const CTransform4f rot(GetTransform().GetRotation());
  const CVector3f offset = (CVector3f(CVector3f::Up()) * -mShrink) * mHalfHeight;
  SetTransform(CTransform4f::Translate(mHomePos + offset) * rot);
  CVector3f scale = mData.mScale;
  scale.SetZ(scale.GetZ() * mScale.GetZ());
  ModelData()->SetScale(scale);
  // 0x000153D0 builds a copy of the old flags with depth compare/update and drops it.
  SetModelFlags(CModelFlags(
      CModelFlags::kT_Blend, 0,
      CModelFlags::EFlags(CModelFlags::kF_DepthCompare | CModelFlags::kF_DepthUpdate),
      CColor::Black().WithAlphaOf(1.f - mShrink)));
}

// fn_30_13760: the shrink - mShrink runs 0..1 over 0.3 s, then the ball is done.
// @ fn_30_13760
void CBoostBallGuardianBall::Shrink(float dt) {
  mStateTime += dt;
  mShrink = mStateTime / 0.3f;
  if (mStateTime > 0.3f) {
    mShrink = 1.f;
    mDone = true;
  }
  mScale.SetZ(1.f - mShrink);
}

// fn_30_12AB0: the roll (0xC00 bytes). Holds the ball flat at no more than the roll speed, runs
// the speed spline, and on a bounce counts it, damps the speed and re-aims: at the player
// (fn_30_11F2C) every mUnknown4B2D misses, at a cover point (fn_30_12510) every mUnknownEE69
// bounces, else away from the wall and round anything in the way. When the spline is over or
// the ball has stopped, the next roll's bounce count is dropped: with none left the ball shrinks
// (state 2), else it grows in again (state 0) with fresh shield and charge-up effects.
// @ fn_30_12AB0
void CBoostBallGuardianBall::Roll(float dt, CStateManager& mgr) {
  if (mSplineTime < mSplineDuration) {
    CVector3f vel = GetVelocityWR();
    if (vel.GetZ() != 0.f) {
      float speed = mRollSpeed;
      const float mag = vel.Magnitude();
      // 0x00012B2C fcmpo + bge: strict `<`; equal or NaN keeps mRollSpeed.
      if (mag < speed) {
        speed = mag;
      }
      vel.SetZ(0.f);
      if (vel.CanBeNormalized()) {
        vel.Normalize();
        SetVelocityWR(vel * speed);
      }
    }
    // 0x00012B98 fcmpo + ble: strict `>`; equal or NaN skips the clamp.
    if (GetVelocityWR().Magnitude() > mRollSpeed) {
      const CVector3f cur = GetVelocityWR();
      if (cur.CanBeNormalized()) {
        SetVelocityWR(cur.AsNormalized() * mRollSpeed);
      }
    }
    if (mOnSpline) {
      const float prev = EvaluateSpeed(mSplineTime - dt);
      mSpeedDelta = EvaluateSpeed(mSplineTime) - prev;
      ApplySpeedImpulse(mSpeedDelta);
    }

    bool turned = false;
    if (mBounced) {
      mBounced = false;
      turned = true;
      // 0x00012C68: a repeat bounce off the same actor, with no player touched, is not counted.
      if (mCollidedId != mLastCollidedId || mCollidedId == kInvalidUniqueId ||
          mTouchedId != kInvalidUniqueId) {
        ++mBounces;
        if (mTouchedId == kInvalidUniqueId) {
          ++mMissBounces;
        }
        ++mCoverBounces;
        if (mBounceCounts.size() == 0 || mBounces >= mBounceCounts[0]) {
          mOnSpline = true;
          AddMaterial(kMT_Immovable, mgr); // 0x00012D0C: 0x2B
        }
      }
      mLastCollidedId = mCollidedId;
      if (GetVelocityWR().Magnitude() > 7.f) {
        PlaySound(5, true); // bounce
      }
      if (mOnSpline) {
        SetVelocityWR(GetVelocityWR() * mData.mUnknownBEA9); // 0x00012D4C: data+0x10
      }
      if (GetVelocityWR().CanBeNormalized()) {
        bool free = true;
        if (mData.mUnknown4B2D > 0 && mMissBounces >= mData.mUnknown4B2D) {
          mMissBounces = 0;
          CVector3f out = CVector3f::Zero();
          if (AimAtPlayer(mgr, GetVelocityWR().AsNormalized(), out)) {
            SetVelocityWR(out * GetVelocityWR().Magnitude());
            free = false;
          }
        }
        if (free && mData.mUnknownEE69 > 0 && mCoverBounces >= mData.mUnknownEE69) {
          mCoverBounces = 0;
          CVector3f out = CVector3f::Zero();
          if (AimAtCoverPoint(mgr, GetVelocityWR().AsNormalized(), out)) {
            SetVelocityWR(out * GetVelocityWR().Magnitude());
            free = false;
          }
        }
        if (free) {
          if (mBounceNormal.GetX() != 0.f || mBounceNormal.GetY() != 0.f ||
              mBounceNormal.GetZ() != 0.f) {
            // 0x00012F48: heading back the way it came (within 15 degrees), turn 20-45 degrees.
            const CVector3f cur = GetVelocityWR();
            const float back =
                static_cast< float >(fabs(57.295776f * CVector3f::GetAngleDiff(cur, mLastVelocity) - 180.f));
            if (back < 15.f) {
              const float side = CVector3f::Cross(cur, mLastVelocity).GetZ() > 0.f ? 1.f : -1.f;
              const float turn = mgr.Random()->Range(20.f, 45.f);
              const CTransform4f rot = CTransform4f::RotateZ(CRelAngle::FromRadians(0.017453292f * (side * turn)));
              SetVelocityWR(rot * cur);
            }
            mBounceNormal = CVector3f::Zero();
          }
          CVector3f out = GetVelocityWR();
          if (Steer(mgr, GetVelocityWR().AsNormalized(), out, 0.f)) {
            SetVelocityWR(out * GetVelocityWR().Magnitude());
          }
        }
        if (mRolling && !mOnSpline && GetVelocityWR().CanBeNormalized()) {
          SetVelocityWR(GetVelocityWR().AsNormalized() * mRollSpeed);
        }
      } else if (mRolling && !mOnSpline) {
        const CVector3f& cur = GetVelocityWR();
        if (cur.GetX() == 0.f && cur.GetY() == 0.f && cur.GetZ() == 0.f &&
            (mLastVelocity.GetX() != 0.f || mLastVelocity.GetY() != 0.f ||
             mLastVelocity.GetZ() != 0.f)) {
          // 0x000131D4: stopped dead - the last velocity reflected off the bounce normal.
          const float d = 2.f * CVector3f::Dot(mBounceNormal, mLastVelocity);
          const CVector3f reflected = mLastVelocity - mBounceNormal * d;
          mBounceNormal = CVector3f::Zero();
          SetVelocityWR(reflected);
        }
      }
    }

    if (mRolling && !mOnSpline) {
      const CVector3f cur = GetVelocityWR();
      if (cur.GetX() == 0.f && cur.GetY() == 0.f && cur.GetZ() == 0.f &&
          (mLastVelocity.GetX() != 0.f || mLastVelocity.GetY() != 0.f ||
           mLastVelocity.GetZ() != 0.f)) {
        // 0x000132F8: looks up the two boost nav points and normalises the line between them
        // into a temporary that is never read; the velocity set is the (zero) current one.
        CScriptAIHint* from = CIngBoostBallGuardian::FindBoostNavPoint(mgr, *this, 0xF);
        CScriptAIHint* to = CIngBoostBallGuardian::FindBoostNavPoint(mgr, *this, 0xE);
        if (from != nullptr && to != nullptr) {
          const CVector3f line = to->GetTranslation() - from->GetTranslation();
          if (line.CanBeNormalized()) {
            (void)line.AsNormalized();
          }
        }
        SetVelocityWR(cur);
        turned = true;
      }
    }
    if (turned && GetVelocityWR().MagSquared() > 0.f) {
      // 0x000133C0: face the new heading.
      const CVector3f pos = GetTranslation();
      CTransform4f xf = CTransform4f::LookAt(pos, pos + GetVelocityWR(), CVector3f::Up());
      xf.SetTranslation(pos);
      SetTransform(xf);
    }
    ContactSweep(dt, mgr);
  } else if (mHalted) {
    Stop();
  } else {
    ApplySpeedImpulse(mSpeedDelta);
  }

  if (mRolling &&
      (mSplineTime >= mSplineDuration || GetVelocityWR().Magnitude() < 0.05f)) {
    // 0x000134AC: erase(begin()). The guest's erase runs off an empty vector; kSM_Create always
    // fills it, so the host only guards it.
    if (mBounceCounts.size() != 0) {
      mBounceCounts.erase(mBounceCounts.begin());
    }
    mBounces = 0;
    mMissBounces = 0;
    mCoverBounces = 0;
    if (mBounceCounts.size() == 0) {
      mState = 2;
      mStateTime = 0.f;
      mHomePos.SetX(GetTranslation().GetX());
      mHomePos.SetY(GetTranslation().GetY());
      PlaySound(4, true);  // out of ball
      PlaySound(3, false); // stop the rolling loop
      mTrailGen = nullptr;
      mShieldGen = nullptr;
      mChargeUpGen = nullptr;
    } else {
      mState = 0;
      mShieldGen = MakeBallEffect(mData.mShieldEffect, 'PART');
      mChargeUpGen = MakeBallEffect(mData.mChargeUpEffect, 'PART');
      PlaySound(3, false);
      mSplineTime = 0.f;
      mOnSpline = false;
      mRolling = false;
      mDirChosen = false;
      mLoopSfx = true;
      mShieldTime = 0.4f;
      Stop();
      mHalted = true;
    }
  }
}

// fn_30_14CCC: the roll's contact sweep. Above the minimum damage speed, a sphere of the
// first-person damage radius at the primitive's position is swept along this frame's motion. The
// first actor it meets (the guardian aside) is the player, kept for Think's contact damage, or is
// damaged here; the first non-actor hit ends the speed spline early (once). Damageable triggers
// in the near list closer than that hit are damaged too.
// @ fn_30_14CCC
void CBoostBallGuardianBall::ContactSweep(float dt, CStateManager& mgr) {
  const CDamageInfo& info = mData.mDamageToFirstPerson;
  const float speed = GetVelocityWR().Magnitude();
  if (!(speed >= mData.mMinDamageSpeed)) {
    return;
  }
  double distance = speed * dt;
  double triggerDistance = distance;
  const CVector3f dir = GetVelocityWR() * (1.f / speed);
  const CMaterialList material(kMT_Solid); // lbl_30_data_C14
  const CVector3f center = GetPrimitiveTransform().GetTranslation();
  const CCollidableSphere sphere(CSphere(center, info.GetRadius()), material);
  const CVector3f motion = dir * static_cast< float >(distance);
  const CAABox local = sphere.CalculateLocalAABox();
  CAABox box(local);
  box.AccumulateBounds(local.GetMinPoint() + motion);
  box.AccumulateBounds(local.GetMaxPoint() + motion);

  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, box,
                    CMaterialFilter::MakeInclude(CMaterialList(
                        kMT_Solid, kMT_NonSolidDamageable)), // lbl_30_data_C18, lbl_30_data_C1C
                    this);
  for (rstl::reserved_vector< TUniqueId, kMaxObjects >::iterator it = nearList.begin();
       it != nearList.end(); ++it) {
    if (*it == mOwnerId) {
      nearList.erase(it); // fn_30_14C54
      break;
    }
  }
  if (nearList.empty()) {
    return;
  }

  const CMaterialFilter damageFilter(CMaterialList(kMT_Solid), CMaterialList(),
                                     CMaterialFilter::kFT_IncludeExclude); // lbl_30_data_C00
  TUniqueId hitId = kInvalidUniqueId;
  CCollisionInfo collision(CCollisionInfo::kI_Invalid);
  if (CGameCollision::DetectDynamicCollisionMoving(sphere, CTransform4f::Identity(), nearList,
                                                   dir, hitId, collision, distance, mgr) &&
      hitId != kInvalidUniqueId) {
    if (TCastToConstPtr< CPlayer >(mgr.GetObjectById(hitId))) {
      mTouchedId = hitId;
    } else {
      mgr.ApplyDamage(GetUniqueId(), hitId, GetUniqueId(), info, damageFilter,
                      CVector3f::Zero());
    }
    if (!m5D9_80 && !TCastToConstPtr< CPatterned >(mgr.GetObjectById(hitId)) &&
        !TCastToConstPtr< CPlayer >(mgr.GetObjectById(hitId))) {
      m5D9_80 = true;
      if (mSplineEndTime > 0.f) {
        mSplineTime = mSplineEndTime;
      }
    }
  }

  for (rstl::reserved_vector< TUniqueId, kMaxObjects >::iterator it = nearList.begin();
       it != nearList.end(); ++it) {
    const CScriptDamageableTrigger* trigger =
        TCastToConstPtr< CScriptDamageableTrigger >(mgr.GetObjectById(*it));
    if (trigger == nullptr) {
      continue;
    }
    const rstl::optional_object< CAABox > bounds = trigger->GetTouchBounds();
    if (!bounds.valid()) {
      continue;
    }
    CVector3f point = CVector3f::Zero();
    CVector3f normal = CVector3f::Up();
    if ((CollisionUtil::AABoxSphereIntersection(bounds.data(), sphere.GetSphere()) ||
         CollisionUtil::MovingSphereAABox(sphere.GetSphere(), bounds.data(), dir, triggerDistance,
                                          point, normal)) &&
        triggerDistance < distance) {
      mgr.ApplyDamage(GetUniqueId(), *it, GetUniqueId(), info, damageFilter, CVector3f::Zero());
    }
  }
}

// fn_30_1282C: the rolling loop follows the ball; above 0.8 its pitch rises 500 per unit of
// speed (from 692, clamped to 0..0x4000).
// @ fn_30_1282C
void CBoostBallGuardianBall::UpdateRollingSfx() {
  const CVector3f pos = GetTranslation();
  const CVector3f dir = GetTransform().GetColumn(kDY);
  if (mRollingSfx) {
    const float speed = GetVelocityWR().Magnitude();
    if (speed > 0.8f) {
      const int raw = static_cast< int >(speed) * 500 + 0x2B4;
      const int pitch = raw < 0 ? 0 : raw > 0x4000 ? 0x4000 : raw;
      CSfxManager::PitchBend(mRollingSfx, pitch);
    }
    CSfxManager::UpdateEmitter(mRollingSfx, pos, dir, 0x7F);
  }
}

// fn_30_12904: starts (or, for the rolling loop, stops) one of the ball's sounds: 0 into ball,
// 1 the loop (0x9F7372B3), 2 boost, 3 rolling (looped, its handle kept), 4 out of ball,
// 5 bounce.
// @ fn_30_12904
void CBoostBallGuardianBall::PlaySound(int sound, bool start) {
  if (sound < 0 || sound > 5) {
    return;
  }
  if (start) {
    CSfxHandle* handle = nullptr;
    bool looped = false;
    ushort id = CSfxManager::kInternalInvalidSfxId;
    switch (sound) {
    case 0:
      id = mData.mSoundIntoBall;
      break;
    case 1:
      id = mData.mSoundLoop;
      break;
    case 2:
      id = mData.mSoundBoost;
      break;
    case 3:
      id = mData.mSoundRolling;
      handle = &mRollingSfx;
      looped = true;
      break;
    case 4:
      id = mData.mSoundOutOfBall;
      break;
    case 5:
      id = mData.mSoundBounce;
      break;
    }
    if (id == CSfxManager::kInternalInvalidSfxId) {
      return;
    }
    CAudioSys::C3DEmitterParmData parms(mData.mSoundMaxDistance, mData.mSoundDistanceComp, 1, 0x7F,
                                        0x14);
    parms.mPos = GetTranslation();
    parms.mDir = CVector3f::Zero();
    parms.mSfxId = id;
    const CSfxHandle h = CSfxManager::AddEmitter(parms, GetCurrentAreaId().Value(), true, looped,
                                                 CSfxManager::kMedPriority);
    if (handle != nullptr) {
      *handle = h;
    }
  } else if (sound == 3) {
    CSfxManager::SfxStop(mRollingSfx);
    mRollingSfx.Clear(); // 0x12B64: the handle is zeroed after the stop
  }
}

// @ fn_30_11E4C
float CBoostBallGuardianBall::EvaluateSpeed(float time) const {
  return mData.mSpeed.EvaluateAt(time);
}

// fn_30_11E70: an impulse along the velocity worth the speed change.
// @ fn_30_11E70
void CBoostBallGuardianBall::ApplySpeedImpulse(float delta) {
  if (delta == 0.f) {
    return;
  }
  const CVector3f vel = GetVelocityWR();
  if (vel.CanBeNormalized()) {
    // 0x00011EF0: the guest passes the angular momentum as the angular impulse.
    ApplyImpulseWR(vel.AsNormalized() * (delta * GetMass()), GetAngularMomentumWR());
  }
}

// fn_30_148DC: true when a sphere (the first-person damage radius + 0.01) at the primitive's
// position can move 100 units along dir without hitting anything solid, ignoring characters,
// collision actors and the guardian's modify targets.
// @ fn_30_148DC
bool CBoostBallGuardianBall::IsPathClear(CStateManager& mgr, const CVector3f& dir) {
  const CVector3f heading = dir;
  double dist = 100.0;
  const CTransform4f xf = GetPrimitiveTransform();
  const CCollidableSphere sphere(
      CSphere(xf.GetTranslation(), 0.01f + mData.mDamageToFirstPerson.GetRadius()),
      CMaterialList(kMT_Solid)); // lbl_30_data_C20
  const CVector3f motion = heading * static_cast< float >(dist);
  CAABox box = sphere.CalculateLocalAABox();
  const CVector3f minPt = box.GetMinPoint();
  const CVector3f maxPt = box.GetMaxPoint();
  box.AccumulateBounds(minPt + motion);
  box.AccumulateBounds(maxPt + motion);
  const CMaterialFilter filter(CMaterialList(kMT_Solid, kMT_NonSolidDamageable),  // C24, C28
                               CMaterialList(kMT_Character, kMT_CollisionActor),  // C2C, C30
                               CMaterialFilter::kFT_IncludeExclude);
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, box, filter, this);
  for (int i = 0; i < nearList.size();) {
    bool erased = false;
    for (int j = 0; j < mModifyTargets.size(); ++j) {
      if (nearList[i] == mModifyTargets[j]) {
        nearList.erase(nearList.begin() + i); // fn_30_14C54
        erased = true;
        break;
      }
    }
    if (!erased) {
      ++i;
    }
  }
  bool hit = false;
  if (nearList.size() != 0) {
    TUniqueId hitId = kInvalidUniqueId;
    CCollisionInfo info(CCollisionInfo::kI_Invalid);
    hit = CGameCollision::DetectDynamicCollisionMoving(sphere, CTransform4f::Identity(), nearList,
                                                       heading, hitId, info, dist, mgr);
  }
  return !hit;
}

// fn_30_12124: turns dir (towards the side it leans from the last heading) in 5 degree steps,
// alternating sides, until the path is clear; gives up past the search cone and past half the
// turn back to the last heading (less bias). out holds the last direction tried.
// @ fn_30_12124
bool CBoostBallGuardianBall::Steer(CStateManager& mgr, const CVector3f& dir, CVector3f& out,
                                   float bias) {
  if (dir.GetX() == 0.f && dir.GetY() == 0.f && dir.GetZ() == 0.f) {
    return false;
  }
  out = dir;
  float search = 0.f;
  const bool left = CVector3f::Cross(mLastVelocity, dir).GetZ() < 0.f;
  const float cone = mData.mSearchConeAngle;
  const float half = 57.295776f * CVector3f::GetAngleDiff(dir, -mLastVelocity) * 0.5f - bias;
  float limit;
  if (0.f > half) {
    limit = 0.f;
  } else if (cone < half) {
    limit = cone;
  } else {
    limit = half;
  }
  (void)CVector3f::GetAngleDiff(dir, -mLastVelocity); // 0x00012290: result unused
  bool flip = false;
  int step = 2;
  for (;;) {
    if (IsPathClear(mgr, out)) {
      return true;
    }
    if (step > 1) {
      step = 0;
      search += 5.f;
    }
    if (search > mData.mSearchConeAngle && search > limit) {
      return false;
    }
    if (search > limit) {
      flip = false;
      ++step;
    } else {
      flip = !flip;
    }
    const float angle = flip ? -search : search;
    const float side = left ? 1.f : -1.f;
    out = CTransform4f::RotateZ(CRelAngle::FromRadians(0.017453292f * (side * angle))) * dir;
    ++step;
  }
}

// fn_30_123CC: target is on the same side of the last heading as cur and within the cone (the
// search cone, capped at half the turn from cur back to the last heading); out is the room left.
// @ fn_30_123CC
bool CBoostBallGuardianBall::TurnWithinCone(const CVector3f& cur, const CVector3f& target,
                                            float& out) const {
  out = 0.f;
  const bool curLeft = CVector3f::Cross(mLastVelocity, cur).GetZ() < 0.f;
  float lim = mData.mSearchConeAngle;
  const float half = 57.295776f * CVector3f::GetAngleDiff(cur, -mLastVelocity) * 0.5f;
  if (!(lim < half)) { // 0x12474 fcmpo; bge
    lim = half;
  }
  const bool targetLeft = CVector3f::Cross(mLastVelocity, target).GetZ() < 0.f;
  const float angle = 57.295776f * CVector3f::GetAngleDiff(cur, target);
  if (targetLeft == curLeft && angle < lim && angle < mData.mSearchConeAngle) {
    out = lim - angle;
    return true;
  }
  return false;
}

// fn_30_11F2C: heads (flat) for the player, offset sideways by up to 4 units, if that is within
// the turn cone.
// @ fn_30_11F2C
bool CBoostBallGuardianBall::AimAtPlayer(CStateManager& mgr, const CVector3f& cur,
                                         CVector3f& out) {
  const CVector3f self = GetTranslation();
  CVector3f target = mgr.GetPlayer(0)->GetTranslation(); // mgr+0x14FC
  const CVector3f toPlayer = target - self;
  const float offset = mgr.Random()->Range(-4.f, 4.f);
  const CVector3f side = CVector3f::Cross(CVector3f::Up(), toPlayer); // fn_30_A91C
  if (side.CanBeNormalized()) {
    target += side.AsNormalized() * offset;
  }
  CVector3f aim = target - self;
  aim.SetZ(0.f);
  if (aim.CanBeNormalized()) {
    aim.Normalize();
    float room = 0.f;
    if (TurnWithinCone(cur, aim, room)) {
      out = aim;
      return true;
    }
  }
  return false;
}

// fn_30_12510: heads for an active cover point in the ball's area that is not behind it (more
// than 20 degrees off straight back) and not one of the last two picked.
// @ fn_30_12510
bool CBoostBallGuardianBall::AimAtCoverPoint(CStateManager& mgr, const CVector3f& cur,
                                             CVector3f& out) {
  static TUniqueId sRecent[2] = {kInvalidUniqueId, kInvalidUniqueId}; // lbl_30_bss_90
  // The guest's candidate buffer is 20 entries on the stack (0x40..0x90) with no bound check.
  CScriptCoverPoint* candidates[20];
  int count = 0;
  CObjectList& list = mgr.ObjectListById(kOL_AiWaypoint); // mgr+0x838
  int i = list.GetFirstObjectIndex();
  for (; i != -1; i = list.GetNextObjectIndex(i)) {
    CScriptCoverPoint* cover = TCastToPtr< CScriptCoverPoint >(list[i]);
    if (cover == nullptr || cover->GetUniqueId() == sRecent[0] ||
        cover->GetUniqueId() == sRecent[1] || !cover->GetActive() ||
        cover->GetCurrentAreaId() != GetCurrentAreaId()) {
      continue;
    }
    const CVector3f to = FlatDelta(cover->GetTranslation());
    const float back =
        static_cast< float >(fabs(57.295776f * CVector3f::GetAngleDiff(-mLastVelocity, to) - 180.f));
    if (back > 20.f && count < 20) {
      candidates[count++] = cover;
    }
  }
  if (count == 0) {
    return false;
  }
  const int start = mgr.Random()->Range(0, count - 1);
  const int tries = count < 3 ? count : 3;
  for (int k = 0; k < tries; ++k) {
    // 0x000126E0: the guest takes (start + k) modulo the exhausted list index (-1), so it always
    // picks the first candidate.
    (void)start;
    CScriptCoverPoint* cover = candidates[0];
    sRecent[0] = sRecent[1];
    sRecent[1] = cover->GetUniqueId();
    CVector3f to = FlatDelta(cover->GetTranslation());
    if (!to.CanBeNormalized()) {
      continue;
    }
    to.Normalize();
    float room = 0.f;
    if (!TurnWithinCone(cur, to, room)) {
      continue;
    }
    return Steer(mgr, to, out, room);
  }
  return false;
}

// @ fn_30_127FC
CVector3f CBoostBallGuardianBall::FlatDelta(const CVector3f& pos) const {
  return CVector3f(pos.GetX() - GetTranslation().GetX(), pos.GetY() - GetTranslation().GetY(), 0.f);
}

// fn_30_142BC: records what the ball hit. An actor other than the guardian (or one of its
// collision actors) is the bounce target; with health left it counts as a bounce, a platform
// aims the bounce at the boost nav point 0xE, and the player at damage speed is touched.
// Otherwise the first contact against solid, ceiling, wall or character gives the bounce normal
// (the one facing against the velocity).
// @ fn_30_142BC
void CBoostBallGuardianBall::CollidedWith(const TUniqueId& id, const CCollisionInfoList& list,
                                          CStateManager& mgr) {
  static const CMaterialList sBounceMaterials(kMT_Solid, kMT_Ceiling, kMT_Wall,
                                              kMT_Character); // lbl_30_bss_78: C34..C40
  bool hitActor = false;
  mCollidedId = kInvalidUniqueId;
  if (id != mOwnerId) {
    const CActor* act = TCastToConstPtr< CActor >(mgr.GetObjectById(id));
    if (act != nullptr && act->GetUniqueId() != mOwnerId) {
      const TUniqueId actId = act->GetUniqueId();
      const CCollisionActor* collision = TCastToConstPtr< CCollisionActor >(act);
      hitActor = collision != nullptr ? collision->GetOwnerId() != mOwnerId : true;
      if (hitActor) {
        mCollidedId = actId;
        const CHealthInfo* health = act->GetHealthInfo();
        if (health != nullptr && health->GetHP() > 0.f) {
          mBounced = true;
          if (act->GetMaterialList().HasMaterial(kMT_Platform)) {
            CScriptAIHint* hint = CIngBoostBallGuardian::FindBoostNavPoint(mgr, *this, 0xE);
            if (hint != nullptr) {
              mBounceNormal = hint->GetTranslation() - act->GetTranslation();
              if (mBounceNormal.CanBeNormalized()) {
                mBounceNormal.Normalize();
              }
            }
          }
        }
        if (TCastToConstPtr< CPlayer >(act) != nullptr &&
            GetVelocityWR().Magnitude() >= mData.mMinDamageSpeed) {
          mTouchedId = act->GetUniqueId();
        }
      }
    }
  }
  if (!hitActor) {
    for (int i = 0; i < list.GetCount(); ++i) {
      const CCollisionInfo& info = list[i];
      if (info.GetMaterialLeft().SharesMaterials(sBounceMaterials)) {
        mBounceNormal = CVector3f::Dot(GetVelocityWR(), info.GetNormalLeft()) > 0.f
                            ? info.GetNormalRight()
                            : info.GetNormalLeft();
        mBounced = true;
        break;
      }
    }
  }
  CPhysicsActor::CollidedWith(id, list, mgr);
}

// fn_30_1466C: kSM_Delete and kSM_Deactivate stop the rolling loop; kSM_Damage is passed on to
// the guardian; kSM_Create sets the scale and picks the bounce counts: mUnknown285D..6D5F rolls,
// one of them (the first, second, third or fourth by 10/20/70/rest odds out of 100) with
// mUnknownECB3..A9B1 bounces, the rest with mUnknownE3FF..A6FD.
// fn_30_F78 is the phase struct's implicit copy (0x38: six floats, six ints, two floats, as the host's).
// @ fn_30_F78
// @ fn_30_1466C
void CBoostBallGuardianBall::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  const EScriptObjectMessage message = msg.GetMessage();
  const TUniqueId sender = msg.GetSenderId();
  CPhysicsActor::AcceptScriptMsg(mgr, msg);
  switch (message) {
  case kSM_Delete:
  case kSM_Deactivate:
    PlaySound(3, false);
    break;
  case kSM_Damage: {
    CEntity* owner = mgr.ObjectById(mOwnerId);
    if (owner != nullptr) {
      mId4F6 = sender;
      mgr.DeliverScriptMsg(CScriptMsg(GetUniqueId(), owner->GetUniqueId(), kSM_Damage));
    }
    break;
  }
  case kSM_Create: {
    UpdateScale(0.f, mgr);
    // 0x00014764: the phase is indexed (0x38 bytes each from data+0x144) and copied (fn_30_F78).
    const CIngBoostBallGuardianPhase phase((&mData.mPhase0)[mPhaseIndex]);
    const int rolls = mgr.Random()->Range(phase.mUnknown285D, phase.mUnknown6D5F);
    mBounceCounts.reserve(rolls); // fn_30_1642C
    const float odds[3] = {10.f, 20.f, 70.f}; // lbl_30_rodata_7B0
    const float pick = mgr.Random()->Range(0.f, 100.f);
    int special = 0;
    float sum = 0.f + odds[0];
    if (!(pick <= sum)) {
      special = 1;
      sum += odds[1];
      if (!(pick <= sum)) {
        special = 2;
        sum += odds[2];
        if (!(pick <= sum)) {
          special = 3;
        }
      }
    }
    if (rolls - 1 < special) {
      special = rolls - 1;
    }
    for (int i = 0; i < rolls; ++i) {
      if (i == special) {
        mBounceCounts.push_back(mgr.Random()->Range(phase.mUnknownECB3, phase.mUnknownA9B1));
      } else {
        mBounceCounts.push_back(mgr.Random()->Range(phase.mUnknownE3FF, phase.mUnknownA6FD));
      }
    }
    break;
  }
  default:
    break;
  }
}

// @ fn_30_13C5C
CHealthInfo* CBoostBallGuardianBall::HealthInfo() {
  return mHealthInfo ? &mHealthInfo.data() : nullptr;
}

// @ fn_30_13C40
const CHealthInfo* CBoostBallGuardianBall::GetHealthInfo() const {
  return mHealthInfo ? &mHealthInfo.data() : nullptr;
}

// @ fn_30_13B90
const CDamageVulnerability* CBoostBallGuardianBall::GetDamageVulnerability() const {
  return &mVulnerability;
}

// @ fn_30_13B88
const CDamageVulnerability* CBoostBallGuardianBall::GetDamageVulnerability(
    const CVector3f&, const CVector3f&, const CDamageInfo&) const {
  return &mVulnerability;
}

// fn_30_13BA4: the collision primitive's box at the actor's transform.
// @ fn_30_13BA4
rstl::optional_object< CAABox > CBoostBallGuardianBall::GetTouchBounds() const {
  return GetCollisionPrimitive()->CalculateAABox(GetTransform());
}

// @ fn_30_1464C
void CBoostBallGuardianBall::Touch(CActor& actor, CStateManager& mgr) {
  CPhysicsActor::Touch(actor, mgr);
}

// @ fn_30_11E44
const CCollisionPrimitive* CBoostBallGuardianBall::GetCollisionPrimitive() const {
  return &mSphere;
}
