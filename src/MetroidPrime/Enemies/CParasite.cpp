#include "MetroidPrime/Enemies/CParasite.hpp"

#include "Collision/CCollidableAABox.hpp"
#include "Collision/CCollisionInfoList.hpp"
#include "Kyoto/Animation/CSkinnedModel.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CRandom16.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/Math/CQuaternion.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Math/CUnitVector3f.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrBrizgee.hpp"
#include "MetroidPrime/ScriptLoader/SLdrCrystallite.hpp"
#include "MetroidPrime/ScriptLoader/SLdrParasite.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CScriptDynamicLight.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSafeZone.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWaypoint.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CWeapon.hpp"
#include "REL/REL_Setup.h"

#include <float.h>

float CParasite::skAttackTime = 2.f * CMath::SqrtF(2.5f / kDefaultGravityAccel);
float CParasite::skAttackVelocity = 15.f / skAttackTime;
float CParasite::skRetreatTime = 2.f * CMath::SqrtF(2.5f / kDefaultGravityAccel);
float CParasite::skRetreatVelocity = 3.f / skRetreatTime;

struct SSphereJointInfo {
  const char* name;
  float radius;
};
static const SSphereJointInfo skIceJoints[] = {{"Skeleton_Root", 0.f}};

static EMaterialTypes skContactMaterial = kMT_Solid;

// Retail `__sinit_CParasite_cpp` (0x766C-0x7A44) accounts for two tables, four float statics and
// `lastParasite`. Tables: `lis` 0x776C/0x78A4 take .data 0x64 (8 triggers) and 0x18C (14 states);
// 66 word copies (`stw` 0x77DC-0x7A28 = 22 entries x {adjust 0, vtoff -1, fn}) - same names, order
// and targets as the host, all 22 marked. The floats are the host's four initialisers evaluated in
// the same order: `lfs` .rodata 0x1C (2.5f) / DOL 0x8041B758 (24.525f = 9.81f*2.5f =
// kDefaultGravityAccel) -> SqrtF (DOL 0x802CDF30) at 0x7690, times .rodata 0x14 (2.0f) at 0x76B4 ->
// skAttackTime @ .bss 0x5C; .rodata 0x88 (15.0f) / skAttackTime at 0x76C8 -> skAttackVelocity
// @ 0x60; the same SqrtF again at 0x76D8 gives skRetreatTime @ 0x64; .rodata 0x60 (3.0f) /
// skRetreatTime at 0x7714 -> skRetreatVelocity @ 0x68, and `lastParasite` (.bss 0x6C) = 0 at
// 0x7A2C. Identical constants and operations to CParasite.cpp:37-40 and:82. Frame stmw r14 0x7684
// (0x80, lmw 0x7A30); no branch.
// @ fn_47_766C
static CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    {"AnimOver", static_cast< CPatterned::StateMachine::TriggerFunc >(&CParasite::AnimOver)},
    {"Landed", static_cast< CPatterned::StateMachine::TriggerFunc >(&CParasite::Landed)},
    {"HitSomething",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CParasite::HitSomething)},
    {"ShouldAttack",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CParasite::ShouldAttack)},
    {"Stuck", static_cast< CPatterned::StateMachine::TriggerFunc >(&CParasite::Stuck)},
    {"AttackOver", static_cast< CPatterned::StateMachine::TriggerFunc >(&CParasite::AttackOver)},
    {"ShotAt", static_cast< CPatterned::StateMachine::TriggerFunc >(&CParasite::ShotAt)},
    {"PatrolPathOver",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CParasite::PatrolPathOver)},
};

static CPatterned::StateMachine::SStateFunction skStates[] = {
    {"Generate", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::Generate)},
    {"Deactivate", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::Deactivate)},
    {"PathFind", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::PathFind)},
    {"TargetPatrol", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::TargetPatrol)},
    {"Patrol", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::Patrol)},
    {"Run", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::Run)},
    {"Attack", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::Attack)},
    {"TelegraphAttack",
     static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::TelegraphAttack)},
    {"Jump", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::Jump)},
    {"TargetPlayer", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::TargetPlayer)},
    {"Retreat", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::Retreat)},
    {"Halt", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::Halt)},
    {"Crouch", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::Crouch)},
    {"GetUp", static_cast< CPatterned::StateMachine::StateFunc >(&CParasite::GetUp)},
};

static TUniqueId lastParasite = TUniqueId(0, 0);

// @ fn_47_6770
CParasite::CParasite(TUniqueId uid, const rstl::string& name, EFlavorType flavor, CEntityInfo& info,
                     const CTransform4f& xf, const CModelData& mData, const CPatternedInfo& pInfo,
                     EBodyType bodyType, float maxTelegraphReactDist, float advanceWpRadius,
                     float f3, float alignAngVel, float f5, float stuckTimeThreshold,
                     float collisionCloseMargin, float parasiteSearchRadius,
                     float parasiteSeparationDist, float parasiteSeparationWeight,
                     float parasiteAlignmentWeight, float parasiteCohesionWeight,
                     float destinationSeekWeight, float forwardMoveWeight,
                     float playerSeparationDist, float playerSeparationWeight,
                     float playerObstructionMinDist, float haltDelay, bool disableMove,
                     EParasiteType type, const CDamageVulnerability& dVuln,
                     const CDamageInfo& dInfo, ushort haltSfx, ushort getUpSfx, ushort crouchSfx,
                     CAssetId modelRes, CAssetId skinRes, float iceZoomerJointHP,
                     float wallWalkerF6, const CDamageInfo& dInfo2, const CActorParameters& aParams)
: CWallCrawler(kPAI_Parasite, uid, name, flavor, info, xf, mData, pInfo, kMT_Flyer, kCT_Zero,
               bodyType, aParams, pInfo.GetHalfExtent(), collisionCloseMargin, alignAngVel,
               advanceWpRadius, playerObstructionMinDist, static_cast< EType >(type), disableMove,
               wallWalkerF6, 0.167f, 0.6f, 1.5f, 0.6f, 1.5f)
, mStateProgress(-1)
, x87c_(CVector3f::Zero())
, mTargetPos(CVector3f::Zero())
, mActiveSpeed(1.f)
, mTelegraphRemTime(0.f)
, mStuckTime(0.f)
, mLastStuckPos(CVector3f::Zero())
, mCollisionActorManager(nullptr)
, mExtraModel(nullptr)
, mParasiteSeparationMove(CVector3f::Zero())
, mParasiteCohesionMove(CVector3f::Zero())
, mParasiteAlignmentMove(CVector3f::Zero())
, mOculusHaltDVuln(dVuln)
, mOculusHaltDInfo(dInfo)
, x928_(dInfo2)
, x944_(0.f)
, mMaxTelegraphReactDist(maxTelegraphReactDist)
, x94c_(f3)
, x954_(f5)
, mStuckTimeThreshold(stuckTimeThreshold)
, mParasiteSearchRadius(parasiteSearchRadius)
, mParasiteSeparationDist(parasiteSeparationDist)
, mParasiteSeparationWeight(parasiteSeparationWeight)
, mParasiteAlignmentWeight(parasiteAlignmentWeight)
, mParasiteCohesionWeight(parasiteCohesionWeight)
, mDestinationSeekWeight(destinationSeekWeight)
, mForwardMoveWeight(forwardMoveWeight)
, mPlayerSeparationDist(playerSeparationDist)
, mPlayerSeparationWeight(playerSeparationWeight)
, mUnmorphedRadius(pInfo.GetHeight() / 2.f)
, mHaltDelay(haltDelay)
, mIceZoomerJointHP(iceZoomerJointHP)
, x990_(CVector3f::Zero())
, x99c_(CVector3f::Zero())
, x9a8_(CVector3f::Zero())
, mHaltSfx(haltSfx)
, mGetUpSfx(getUpSfx)
, mCrouchSfx(crouchSfx)
, x9ba_(kInvalidUniqueId)
, x9bc_(kInvalidUniqueId)
, x9c0_(0)
, x9c4_(0)
, mReceivedTelegraph(false)
, mJumpVelDirty(false)
, x9c8_26_(false)
, mLanded(false)
, mOnGround(true)
, x9c8_29_(false)
, mAttackOver(true)
, x9c8_31_(false)
, mHalted(false)
, mVulnerable(false)
, mOculusShotAt(false)
, mInJump(false)
, mLineOfSight(GetUniqueId(), CSegId(0xff), 0.2f, 0.05f) {
  SetCallTouch(false);
  switch (mType) {
  case kPT_Geemer:
    mKnockBackController.EnableFreeze(false);
  case kPT_Oculus:
    mKnockBackController.EnableKnockBackPhysics(false);
    break;
  case kPT_IceZoomer:
    mExtraModel = rs_new TLockedToken< CSkinnedModel >(
        rs_new CSkinnedModel(gpSimplePool->GetObj(SObjectTag('CMDL', modelRes)),
                             gpSimplePool->GetObj(SObjectTag('CSKR', skinRes)),
                             GetModelData()->GetAnimationData()->GetModelData()->GetLayoutInfo()));
    break;
  default:
    break;
  }
  if (mType == kPT_Oculus) {
    mKnockBackController.EnableShock(false);
    mKnockBackController.EnableBurn(false);
    mKnockBackController.EnableBurnDeath(false);
    mKnockBackController.EnableExplodeDeath(false);
  }
}

// @ fn_47_6628
CParasite::~CParasite() {}

#ifdef TARGET_PC
// Ported from the Prime 1 port's CWallWalker::ProjectVectorToPlane (its CWallWalker.cpp:53): the
// component of `vec` in the plane whose normal is `planeDir`. CParasite's locomotion wants a move
// vector that stays on the surface it is crawling, which is this projection.
CVector3f CParasite::ProjectVectorToPlane(const CVector3f& vec, const CVector3f& planeDir) {
  return vec - CVector3f::Dot(vec, planeDir) * planeDir;
}
#endif

// @ fn_47_5FD8
void CParasite::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  const TUniqueId uid = msg.GetSenderId();
  const EScriptObjectMessage message = msg.GetMessage();
  CWallCrawler::AcceptScriptMsg(mgr, msg);

  switch (message) {
  case kSM_Create: {
    mBodyController->Activate(mgr, pas::kAS_Invalid);
    SetDrawShadow(false);
    mActiveSpeed = mSpeed;
    const float radius = mColSphere.GetSphere().GetRadius();
    const CVector3f extent(radius, radius, radius);
    SetBoundingBox(CAABox(-extent, extent));
    lastParasite = GetUniqueId();
    AddDoorRepulsors(mgr);
    if (mType == kPT_IceZoomer) {
      SetupIceZoomerCollision(mgr);
      const CHealthInfo hInfo(mIceZoomerJointHP, GetHealthInfo()->GetKnockBackResistance());
      SetupIceZoomerVulnerability(mgr, mOculusHaltDVuln, hInfo);
    }
    break;
  }
  case kSM_Delete:
    switch (mType) {
    case kPT_IceZoomer:
      DestroyActorManager(mgr);
      break;
    case kPT_Crystallite:
      mgr.DeleteObjectRequest(x9ba_);
      mgr.DeleteObjectRequest(x9bc_);
      break;
    }
    break;
  case kSM_AreaLoaded:
    if (mType == kPT_Crystallite) {
      for (const SConnection* it = GetConnectionList().data();
           it != GetConnectionList().data() + GetConnectionList().size(); ++it) {
        if (it->state == kSS_GRNT && it->msg == kSM_Activate) {
          const CScriptObjectLoaderHelper::SGeneratedObject generated =
              mgr.ScriptObjectLoaderHelper().GenerateScriptObject(it->objId, mgr);
          const TUniqueId id = generated.mUniqueId;
          CEntity* entity = generated.mEntity;
          if (TCastToPtr< CScriptSafeZone >(entity)) {
            x9ba_ = id;
          } else if (TCastToPtr< CScriptDynamicLight >(entity)) {
            x9bc_ = id;
          } else {
            mgr.DeleteObjectRequest(id);
          }
        }
      }
    }
    mLineOfSight.SetTarget(mgr.GetPlayer(0)->GetUniqueId());
    break;
  case kSM_Launching:
    if (mJumpVelDirty) {
      UpdateJumpVelocity();
      mJumpVelDirty = false;
    }
    break;
  case kSM_Activate:
    mDisableMove = false;
    switch (mType) {
    case kPT_Parasite:
      mBodyController->SetLocomotionType(pas::kLT_Lurk);
      break;
    case kPT_Crystallite:
      UpdateShell(mgr, 0);
      break;
    }
    break;
  case kSM_Deactivate:
    if (mType == kPT_Crystallite) {
      UpdateShell(mgr, 1);
    }
    break;
  case kSM_ResistedDamage:
    switch (mType) {
    case kPT_Oculus:
      if (const CActor* act = TCastToConstPtr< CActor >(mgr.GetObjectById(uid))) {
        const float distSq = (act->GetTranslation() - GetTranslation()).MagSquared();
        const float maxComp = rstl::max_val(
            GetTouchBounds()->GetWidth(),
            rstl::max_val(GetTouchBounds()->GetDepth(), GetTouchBounds()->GetHeight()));
        const float maxCompSq = maxComp * maxComp + 1.f;
        if (distSq < maxCompSq * maxCompSq) {
          mOculusShotAt = true;
        }
      }
      break;
    case kPT_Crystallite: {
      int type = -1;
      if (const CWeapon* weapon = TCastToConstPtr< CWeapon >(mgr.GetObjectById(uid))) {
        type = weapon->GetType();
      }
      switch (type) {
      case 2:
        UpdateShell(mgr, 2);
        break;
      case 1:
        UpdateShell(mgr, 1);
        break;
      }
      break;
    }
    }
  case kSM_ReflectedDamage:
    if (mType == kPT_IceZoomer) {
      mBodyController->CommandMgr().DeliverCmd(CBCAdditiveFlinchCmd(1.f));
    }
    break;
  case kSM_AIUpdateDisabled:
    if (mCollisionActorManager.get()) {
      mCollisionActorManager->SetPhysicsActive(mgr, false);
    }
    break;
  default:
    break;
  }
}

// @ fn_47_5E10
void CParasite::AddDoorRepulsors(CStateManager& mgr) {
  const rstl::list< CEntity* >& doors = mgr.GetDoorList();
  int count = 0;
  for (rstl::list< CEntity* >::const_iterator it = doors.begin(); it != doors.end(); ++it) {
    if (*it && (*it)->GetCurrentAreaId() == GetCurrentAreaId()) {
      ++count;
    }
  }
  mDoorRepulsors.reserve(count);
  for (rstl::list< CEntity* >::const_iterator it = doors.begin(); it != doors.end(); ++it) {
    const CActor* door = static_cast< const CActor* >(*it);
    if (door && door->GetCurrentAreaId() == GetCurrentAreaId()) {
      rstl::optional_object< CAABox > bounds = door->GetTouchBounds();
      if (bounds.valid()) {
        float diagonal = (bounds->GetMinPoint() - bounds->GetMaxPoint()).Magnitude();
        mDoorRepulsors.push_back_unsafe(CRepulsor(bounds->GetCenterPoint(), 0.75f * diagonal));
      }
    }
  }
}

// @ fn_47_5DF0
void CParasite::PreThink(float dt, CStateManager& mgr) { CWallCrawler::PreThink(dt, mgr); }

// @ fn_47_5608
void CParasite::Think(float dt, CStateManager& mgr) {
  if (!GetActive()) {
    return;
  }

  ++mThinkCounter;
  switch (mType) {
  case kPT_IceZoomer:
    UpdateCollisionActors(dt, mgr);
    break;
  case kPT_Crystallite:
    if (CActor* act = TCastToPtr< CActor >(mgr.ObjectById(x9ba_))) {
      act->SetTransform(GetTransform());
    }
    if (CActor* act = TCastToPtr< CActor >(mgr.ObjectById(x9bc_))) {
      act->SetTransform(GetTransform());
    }
    break;
  }

  mPlayerObstructed = false;
  const CGameArea& area = mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId());
  if (area.GetOcclusionState() != CGameArea::kOS_Visible) {
    mPlayerObstructed = true;
  }

  if (!mPlayerObstructed) {
    const CPlayer* player = mgr.GetPlayer(0);
    if ((player->GetTranslation() - GetTranslation()).Magnitude() > mPlayerObstructionMinDist) {
      mLineOfSight.Update(dt, mgr);
      if (!mLineOfSight.HasLineOfSight()) {
        mPlayerObstructed = true;
      }
    }
  }

  if (mPlayerObstructed) {
    SetMovable(false);
    return;
  }

  SetMovable(!mAlignToFloor);

  if (!mDisableMove) {
    if (close_enough(mBodyController->GetPercentageFrozen(), 0.f)) {
      static const float ksMinMoveDistance = 0.3f * dt * mSpeed;
      const CVector3f stuckDelta = GetTranslation() - mLastStuckPos;
      if (stuckDelta.MagSquared() < ksMinMoveDistance * dt) {
        mStuckTime += dt;
      } else {
        mStuckTime = 0.f;
      }

      mLastStuckPos = GetTranslation();
      if (mTelegraphRemTime > 0.f) {
        mTelegraphRemTime -= dt;
      } else {
        mTelegraphRemTime = 0.f;
      }
    }
  }

  if (mAlive) {
    CPlayer* player = mgr.Player(0);
    bool useCollisionRadius =
        player->GetMorphballTransitionState() == CPlayer::kMS_Morphed || !mAttackOver;
    float radius = useCollisionRadius ? mColSphere.GetSphere().GetRadius() : mUnmorphedRadius;

    const CVector3f extent(radius, radius, radius);
    CAABox aabox(GetTranslation() - extent, GetTranslation() + extent);
    rstl::optional_object< CAABox > plBox = player->GetTouchBounds();

    if (plBox.valid() && plBox->DoBoundsOverlap(aabox)) {
      if (!mAttackOver) {
        mAttackOver = true;
        mLanded = false;
      }

      if (mCurDamageRemTime <= 0.f) {
        mgr.ApplyDamage(
            GetUniqueId(), player->GetUniqueId(), GetUniqueId(), GetContactDamage(),
            CMaterialFilter::MakeIncludeExclude(CMaterialList(skContactMaterial), CMaterialList()),
            CVector3f::Zero());
        if (mType == kPT_IceZoomer && mVulnerable) {
          CSfxManager::AddEmitter(mCrouchSfx, GetTranslation(), GetCurrentAreaId().Value(), true,
                                  false);
          CSfxManager::SfxStart(mGetUpSfx, 100, 64);
          x944_ = 2.f;
          CCameraBlurPass& blur = mgr.CameraBlurPass(0, 3);
          blur.SetBlur(CCameraBlurPass::kBT_LoBlur, 4.f, 0.f, false);
          blur.DisableBlur(2.f);
          CCameraFilterPass& filter = mgr.CameraFilterPass(0, 3);
          const CColor color(static_cast< uchar >(140), 255, 109);
          filter.SetFilter(CCameraFilterPass::kFT_Blend, CCameraFilterPass::kFS_Fullscreen, 0.f,
                           color.WithAlphaOf(0.25f), -1);
          filter.DisableFilter(2.f);
        }
        mCurDamageRemTime = mDamageWaitTime;
      }
    }

    if (x944_ > 0.f) {
      mgr.ApplyDamage(
          GetUniqueId(), player->GetUniqueId(), GetUniqueId(), x928_.WithNoImmunity(),
          CMaterialFilter::MakeIncludeExclude(CMaterialList(skContactMaterial), CMaterialList()),
          CVector3f::Zero());
      x944_ -= dt;
    }
  }

  CWallCrawler::Think(dt, mgr);

  if (mDisableMove) {
    return;
  }

  if (!close_enough(mBodyController->GetPercentageFrozen(), 0.f)) {
    return;
  }

  mSpeed = mActiveSpeed;
  if (mAlignToFloor) {
    AlignToFloor(mgr, mColSphere.GetSphere().GetRadius(),
                 GetTranslation() + 2.f * (dt * GetVelocityWR()), dt);
  }

  mLanded = false;
}

// @ fn_47_55D8
CAdvancementDeltas CParasite::UpdateWalkerAnimation(CStateManager& mgr, float dt) {
  return UpdateAnimation(dt, mgr, true);
}

// @ fn_47_5328
void CParasite::UpdateJumpVelocity() {
  SetMomentumWR(CVector3f(0.f, 0.f, -GetWeight()));
  CVector3f velocity = CVector3f::Zero();
  if (!mAttackOver) {
    float speed = skAttackVelocity;
    velocity = speed * GetTransform().GetForward();
    velocity.SetZ(0.5f * skAttackVelocity);
  } else {
    float speed = skRetreatVelocity;
    velocity = speed * GetTransform().GetForward();
    velocity.SetZ(0.5f * skRetreatVelocity);
  }
  const CVector3f& position = GetTranslation();
  float height = mTargetPos.GetZ() - position.GetZ();
  float acceleration = GetMomentumWR().GetZ() / GetMass();
  CVector3f delta(mTargetPos.GetX() - position.GetX(), mTargetPos.GetY() - position.GetY(), 0.f);
  float distance = delta.Magnitude();
  if (distance > FLT_EPSILON) {
    delta *= 1.f / distance;
    float speed = CVector3f::Dot(delta, velocity);
    if (speed > FLT_EPSILON) {
      float time = 0.f;
      bool below = height < 0.f;
      float positiveRoot, negativeRoot;
      if (CMath::SolveQuadratic(acceleration, velocity.GetZ(), -height, positiveRoot,
                                negativeRoot)) {
        time = below ? negativeRoot : positiveRoot;
      }
      if (!below) {
        time += distance / speed;
      }
      if (time < 10.f) {
        velocity = distance / time * delta;
        velocity.SetZ(-(0.5f * acceleration * time - height / time));
      }
    }
  }
  SetVelocityWR(velocity);
}

// @ fn_47_530C
CVector3f CParasite::GetAimPosition(const CStateManager&, float) const { return GetTranslation(); }

// @ fn_47_52C4
void CParasite::ThinkAboutMove(float dt) {
  if (!GetMaterialList().HasMaterial(kMT_GroundCollider)) {
    CPatterned::ThinkAboutMove(dt);
  }
}

// @ fn_47_52AC
bool CParasite::Stuck(CStateManager&, const CTriggerData&) const {
  return mStuckTime > mStuckTimeThreshold;
}

// @ fn_47_52A0
bool CParasite::AttackOver(CStateManager&, const CTriggerData&) const { return mAttackOver; }

// @ fn_47_5260
bool CParasite::ShotAt(CStateManager&, const CTriggerData&) const {
  switch (mType) {
  case kPT_Crystallite:
  case kPT_Oculus:
    return mOculusShotAt;
  default:
    return mHitByPlayerProjectile;
  }
}

// @ fn_47_5244
bool CParasite::PatrolPathOver(CStateManager&, const CTriggerData&) const {
  return mDestObj == kInvalidUniqueId;
}

static EMaterialTypes skWallMaterial = kMT_Solid;

// @ fn_47_5088
bool CParasite::CloseToWall(CStateManager& mgr) const {
  static const CMaterialFilter kSolidFilter =
      CMaterialFilter::MakeInclude(CMaterialList(skWallMaterial));
  const CAABox& bounds = GetBoundingBox();
  float radius = mColSphere.GetSphere().GetRadius();
  float margin = radius + mCollisionCloseMargin;
  CAABox expanded(bounds.GetMinPoint() - CVector3f(margin, margin, margin),
                  bounds.GetMaxPoint() + CVector3f(margin, margin, margin));
  CCollidableAABox collisionBox(expanded, GetMaterialList());
  return CGameCollision::DetectStaticCollisionBoolean(mgr, collisionBox, CTransform4f::Identity(),
                                                      kSolidFilter);
}

static EMaterialTypes skPlayerMaterial = kMT_Player;
static EMaterialTypes skCharacterMaterial = kMT_Character;

// @ fn_47_4F08
void CParasite::CollidedWith(const TUniqueId& id, const CCollisionInfoList& list,
                             CStateManager& mgr) {
  static const CMaterialList testList = CMaterialList(skPlayerMaterial, skCharacterMaterial);
  if (mInJump) {
    for (const CCollisionInfo* it = list.Begin(); it < list.End(); ++it) {
      const CCollisionInfo& info = *it;
      if (!mAlignToFloor && !testList.SharesMaterials(info.GetMaterialLeft())) {
        AlignToPlane(CUnitVector3f(info.GetNormalLeft(), CUnitVector3f::kN_No), 360.f);
        CPhysicsActor::Stop();
        SetVelocityWR(CVector3f::Zero());
        mLanded = true;
        mOnGround = true;
      }
    }
  }
}

// @ fn_47_4EFC
bool CParasite::IsOnGround() const { return mOnGround; }

// @ fn_47_4E28
TUniqueId CParasite::RecursiveFindClosestWayPoint(CStateManager& mgr, TUniqueId id,
                                                  float& dist) const {
  TUniqueId ret = id;
  CScriptWaypoint* wp = TCastToPtr< CScriptWaypoint >(mgr.ObjectById(id));
  if (!wp) {
    return ret;
  }
  wp->SetActive(false);
  dist = (wp->GetTranslation() - GetTranslation()).MagSquared();
  wp->SetActive(true);
  return ret;
}

// @ fn_47_4D3C
TUniqueId CParasite::GetClosestWaypointForState(EScriptObjectState state,
                                                CStateManager& mgr) const {
  float minDist = FLT_MAX;
  TUniqueId ret = kInvalidUniqueId;
  for (const SConnection* it = GetConnectionList().data();
       it != GetConnectionList().data() + GetConnectionList().size(); ++it) {
    const SConnection& conn = *it;
    if (state == conn.state && conn.msg == kSM_Follow) {
      TUniqueId id = mgr.GetIdForScript(conn.objId);
      float dist;
      TUniqueId closestWp = RecursiveFindClosestWayPoint(mgr, id, dist);
      if (dist < minDist) {
        minDist = dist;
        ret = closestWp;
      }
    }
  }
  return ret;
}

// @ fn_47_4D28
bool CParasite::AnimOver(CStateManager&, const CTriggerData&) const { return mStateProgress == 2; }

// @ fn_47_4D1C
bool CParasite::Landed(CStateManager&, const CTriggerData&) const { return mLanded; }

// @ fn_47_4CB8
bool CParasite::HitSomething(CStateManager& mgr, const CTriggerData&) const {
  if (mThinkCounter & 0x1) {
    return true;
  }
  return mTumbleAngle < 270.f && CloseToWall(mgr);
}

// @ fn_47_4BD4
bool CParasite::ShouldAttack(CStateManager& mgr, const CTriggerData& data) const {
  bool shouldAttack = mReceivedTelegraph && mTelegraphRemTime > 0.1f;
  return !TooClose(mgr, data) && InMaxRange(mgr, data) &&
         (shouldAttack || InDetectionRange(mgr, CTriggerData(0.f)));
}

// @ fn_47_4A68
void CParasite::Generate(CStateManager&, EStateMsg msg, float) {
  switch (msg) {
  case kStateMsg_Activate:
    mStateProgress = 0;
    break;
  case kStateMsg_Update:
    switch (mStateProgress) {
    case 0:
      if (mBodyController->GetCurrentStateId() == pas::kAS_Generate) {
        mStateProgress = 1;
      } else {
        mBodyController->CommandMgr().DeliverCmd(CBCGenerateCmd(pas::kGType_Zero, -1));
      }
      break;
    case 1:
      if (mBodyController->GetCurrentStateId() != pas::kAS_Generate) {
        mStateProgress = 2;
      }
      break;
    }
    break;
  case kStateMsg_Deactivate:
    break;
  }
}

// @ fn_47_4874
void CParasite::Deactivate(CStateManager& mgr, EStateMsg msg, float) {
  switch (msg) {
  case kStateMsg_Activate:
    mStateProgress = 0;
    SendScriptMsgs(kSS_DeGenerate, mgr, kInvalidUniqueId, kSM_None);
    mgr.DeleteObjectRequest(GetUniqueId());
    break;
  case kStateMsg_Update:
    switch (mStateProgress) {
    case 0:
      if (mBodyController->GetCurrentStateId() == pas::kAS_Generate) {
        mStateProgress = 1;
      } else {
        mBodyController->CommandMgr().DeliverCmd(CBCGenerateCmd(pas::kGType_One, -1));
      }
      break;
    }
    break;
  case kStateMsg_Deactivate:
    break;
  }
}

// @ fn_47_4870
void CParasite::Run(CStateManager&, EStateMsg, float) {}

// @ fn_47_4774
void CParasite::PathFind(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    x9c8_26_ = true;
    mAlignToFloor = true;
    if (mType == kPT_Parasite) {
      mBodyController->SetLocomotionType(pas::kLT_Lurk);
    }
    SetMomentumWR(CVector3f::Zero());
    SetMovable(false);
    break;
  case kStateMsg_Update:
    UpdatePFDestination(mgr);
    DoFlockingBehavior(mgr);
    break;
  case kStateMsg_Deactivate:
    SetMovable(true);
    mAlignToFloor = false;
    x9c8_26_ = false;
    break;
  }
}

// @ fn_47_4630
void CParasite::Jump(CStateManager& mgr, EStateMsg msg, float) {
  switch (msg) {
  case kStateMsg_Activate:
    AddMaterial(kMT_GroundCollider, mgr);
    SetMomentumWR(CVector3f(0.f, 0.f, -GetWeight()));
    mOnGround = false;
    mAlignToFloor = false;
    mLanded = false;
    mInJump = true;
    break;
  case kStateMsg_Update:
    SetMomentumWR(CVector3f(0.f, 0.f, -GetWeight()));
    break;
  case kStateMsg_Deactivate:
    RemoveMaterial(kMT_GroundCollider, mgr);
    SetMomentumWR(CVector3f::Zero());
    mOnGround = true;
    mLanded = false;
    mInJump = false;
    break;
  }
}

// @ fn_47_45A8
void CParasite::TargetPatrol(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate: {
    SetMomentumWR(CVector3f::Zero());
    TUniqueId wpId = GetClosestWaypointForState(kSS_Patrol, mgr);
    if (wpId != kInvalidUniqueId) {
      mDestObj = wpId;
    }
    break;
  }
  case kStateMsg_Deactivate:
    break;
  case kStateMsg_Update:
    break;
  }
}

// @ fn_47_4420
void CParasite::Patrol(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    x9c8_26_ = true;
    mAlignToFloor = true;
    if (!mDisableMove && mType == kPT_Parasite) {
      mBodyController->SetLocomotionType(pas::kLT_Lurk);
    }
    SetMomentumWR(CVector3f::Zero());
    mHasAlignSurface = false;
    SetMovable(false);
    break;
  case kStateMsg_Update: {
    const float& pause = mPatrolPauseRemTime;
    if (pause > 0.f) {
      mPatrolPauseRemTime -= dt;
      if (mPatrolPauseRemTime <= 0.f) {
        if (mType == kPT_Parasite) {
          mBodyController->SetLocomotionType(pas::kLT_Lurk);
        }
        mPatrolPauseRemTime = 0.f;
      }
    }
    UpdateWPDestination(mgr);
    if (mPatrolPauseRemTime <= 0.f && !mDisableMove) {
      DoFlockingBehavior(mgr);
    }
    break;
  }
  case kStateMsg_Deactivate:
    mAlignToFloor = false;
    SetMovable(true);
    break;
  }
}

// @ fn_47_4348
void CParasite::FaceTarget(CVector3f target) {
  CVector3f delta = target - GetTranslation();
  delta.SetZ(0.f);
  CQuaternion rotation = CQuaternion::LookAt(CVector3f::Forward(), CUnitVector3f(delta),
                                             CRelAngle::FromDegrees(360.f));
  SetTransform(rotation.BuildTransform4f(GetTranslation()));
}

// @ fn_47_4038
void CParasite::Attack(CStateManager& mgr, EStateMsg msg, float) {
  switch (msg) {
  case kStateMsg_Activate: {
    mTelegraphRemTime = 0.f;
    CRandom16& random = *mgr.Random();
    if (mgr.GetPlayer(0)->GetMorphballTransitionState() == CPlayer::kMS_Morphed) {
#ifdef TARGET_PC
      // retail draw order z,y,x (A8-13)
      const float offZ = random.Float() - 0.5f;
      const float offY = random.Float() - 0.5f;
      const float offX = random.Float() - 0.5f;
      mTargetPos = mgr.GetPlayer(0)->GetTranslation() + 0.5f * CVector3f(offX, offY, offZ);
#else
      mTargetPos =
          mgr.GetPlayer(0)->GetTranslation() +
          0.5f * CVector3f(random.Float() - 0.5f, random.Float() - 0.5f, random.Float() - 0.5f);
#endif
    } else {
#ifdef TARGET_PC
      // retail draw order z,y,x (A8-13); the +0.5 is on z as retail has it
      const float jitZ = random.Float() + 0.5f;
      const float jitY = random.Float() - 0.5f;
      const float jitX = random.Float() - 0.5f;
      mTargetPos = GetTranslation() +
                   15.f * (mgr.GetPlayer(0)->GetTranslation() + CVector3f(jitX, jitY, jitZ) -
                           GetTranslation())
                              .AsNormalized();
#else
      mTargetPos =
          GetTranslation() +
          15.f * (mgr.GetPlayer(0)->GetTranslation() +
                  CVector3f(random.Float() - 0.5f, random.Float() - 0.5f, random.Float() + 0.5f) -
                  GetTranslation())
                     .AsNormalized();
#endif
    }
    FaceTarget(mTargetPos);
    mStateProgress = 0;
    mAttackOver = false;
    mReceivedTelegraph = false;
    mOnGround = false;
    break;
  }
  case kStateMsg_Update:
    switch (mStateProgress) {
    case 0:
      if (mBodyController->GetCurrentStateId() == pas::kAS_Jump) {
        mStateProgress = 1;
      } else {
        mJumpVelDirty = true;
        FaceTarget(mTargetPos);
        mBodyController->CommandMgr().DeliverCmd(
            CBCJumpCmd(mTargetPos, pas::kJT_Normal, pas::kJS_IntoJump, 0, 2));
      }
      break;
    case 1:
      break;
    }
    break;
  case kStateMsg_Deactivate:
    mOnGround = true;
    mAttackOver = true;
    break;
  }
}

// @ fn_47_3CD4
void CParasite::Retreat(CStateManager& mgr, EStateMsg msg, float) {
  switch (msg) {
  case kStateMsg_Activate: {
    CVector3f dir = mgr.GetPlayer(0)->GetTranslation() - GetTranslation();
    dir.SetZ(0.f);
    if (dir.CanBeNormalized()) {
      dir = dir.AsNormalized();
    } else {
      dir = mgr.GetPlayer(0)->GetTransform().GetForward();
    }
    mTargetPos = GetTranslation() - 3.f * dir;
    FaceTarget(mTargetPos);
    mStateProgress = 0;
    mLanded = false;
    mOnGround = false;
    mJumpVelDirty = true;
    mBodyController->CommandMgr().DeliverCmd(
        CBCJumpCmd(mTargetPos, static_cast< pas::EJumpType >(1), pas::kJS_IntoJump, 0, 2));
    break;
  }
  case kStateMsg_Update:
    mSpeed = 1.f;
    break;
  case kStateMsg_Deactivate:
    mOnGround = true;
    break;
  }
}

// @ fn_47_3BC4
void CParasite::TargetPlayer(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mTargetPos = mgr.GetPlayer(0)->GetTranslation() + CVector3f(0.f, 0.f, 1.5f);
    break;
  case kStateMsg_Update:
    mBodyController->FaceDirectionOnSurface(
        ProjectVectorToPlane(mTargetPos - GetTranslation(), GetTransform().GetUp()),
        GetTransform().GetForward(), 2.f);
    break;
  case kStateMsg_Deactivate:
    break;
  }
}

// @ fn_47_3B14
void CParasite::Death(CStateManager& mgr, const CVector3f& direction, EScriptObjectState state) {
  CPhysicsActor::Stop();
  TelegraphAttack(mgr, kStateMsg_Activate, 0.f);
  SetMomentumWR(CVector3f(0.f, 0.f, -GetWeight()));
  CPatterned::Death(mgr, direction, state);
}

// @ fn_47_39F4
void CParasite::TelegraphAttack(CStateManager& mgr, EStateMsg msg, float) {
  switch (msg) {
  case kStateMsg_Activate: {
    const rstl::list< CEntity* >& parasites = mgr.GetParasiteList();
    for (rstl::list< CEntity* >::const_iterator it = parasites.begin(); it != parasites.end();
         ++it) {
      CParasite* other = TCastToPtr< CParasite >(*it);
      if (other && other != this && other->GetAlive() &&
          (other->GetTranslation() - GetTranslation()).MagSquared() <
              mMaxTelegraphReactDist * mMaxTelegraphReactDist) {
        other->mReceivedTelegraph = true;
        other->mTelegraphRemTime = mgr.Random()->Float() * 0.5f + 0.5f;
        other->mTargetPos = GetTranslation();
      }
    }
    mHitByPlayerProjectile = false;
    break;
  }
  case kStateMsg_Deactivate:
    break;
  case kStateMsg_Update:
    break;
  }
}

// @ fn_47_37F0
void CParasite::Halt(CStateManager& mgr, EStateMsg msg, float) {
  switch (msg) {
  case kStateMsg_Activate:
    mStateMachine->SetDelay(mHaltDelay);
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mHalted = true;
    mAlignToFloor = true;
    if (mType == kPT_Geemer) {
      CSfxManager::AddEmitter(mHaltSfx, GetTranslation(), GetCurrentAreaId().Value(), true, false);
    }
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*mBodyController, pas::kAS_LoopReaction)) {
      mBodyController->CommandMgr().DeliverCmd(
          CBCLoopReactionCmd(static_cast< pas::EReactionType >(1)));
    }
    mHitByPlayerProjectile = false;
    break;
  case kStateMsg_Deactivate:
    mBodyController->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_ExitState));
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    mHalted = false;
    mAlignToFloor = false;
    break;
  }
}

// @ fn_47_3710
void CParasite::Crouch(CStateManager& mgr, EStateMsg msg, float) {
  switch (msg) {
  case kStateMsg_Activate:
    switch (mType) {
    case kPT_Crystallite:
      mBodyController->SetLocomotionType(x9c0_ == 2 ? pas::kLT_Crouch : pas::kLT_Internal9);
      break;
    default:
      mBodyController->SetLocomotionType(pas::kLT_Crouch);
      break;
    }
    switch (mType) {
    case kPT_Geemer:
      CSfxManager::AddEmitter(mCrouchSfx, GetTranslation(), GetCurrentAreaId().Value(), true,
                              false);
      break;
    case kPT_Crystallite:
      mStateMachine->SetDelay(mHaltDelay);
      mOculusShotAt = false;
      break;
    }
    break;
  case kStateMsg_Deactivate:
    break;
  case kStateMsg_Update:
    break;
  }
}

// @ fn_47_3660
void CParasite::GetUp(CStateManager& mgr, EStateMsg msg, float) {
  switch (msg) {
  case kStateMsg_Activate:
    mBodyController->SetLocomotionType(pas::kLT_Relaxed);
    switch (mType) {
    case kPT_Geemer:
      CSfxManager::AddEmitter(mGetUpSfx, GetTranslation(), GetCurrentAreaId().Value(), true, false);
      break;
    case kPT_Crystallite:
      UpdateShell(mgr, 0);
      break;
    }
    break;
  case kStateMsg_Deactivate:
    break;
  }
}

// @ fn_47_3640
void CParasite::Touch(CActor& actor, CStateManager& mgr) { CPatterned::Touch(actor, mgr); }

// @ fn_47_363C
void CParasite::UpdatePFDestination(CStateManager&) {}

static EMaterialTypes skFlockMaterial = kMT_Character;

// @ fn_47_2E2C
void CParasite::DoFlockingBehavior(CStateManager& mgr) {
  CVector3f upVec = GetTransform().GetUp();
  rstl::reserved_vector< TUniqueId, 1024 > parasiteList;
  float radius = mParasiteSearchRadius;
  const CVector3f position = GetTranslation();
  CAABox aabb(position - CVector3f(radius, radius, radius),
              position + CVector3f(radius, radius, radius));
  if ((mThinkCounter % 6) == 0) {
    rstl::reserved_vector< TUniqueId, 1024 > nearList;
    nearList.clear();
    static const CMaterialFilter filter =
        CMaterialFilter::MakeInclude(CMaterialList(skFlockMaterial));
    CParasite* closestParasite = nullptr;
    float minDistSq = 2.f + mParasiteSeparationDist * mParasiteSeparationDist;
    mgr.BuildNearList(nearList, aabb, filter, nullptr);
    for (rstl::reserved_vector< TUniqueId, 1024 >::iterator it = nearList.begin();
         it != nearList.end(); ++it) {
      if (CParasite* parasite = TCastToPtr< CParasite >(mgr.ObjectById(*it))) {
        if (parasite->GetUniqueId() != GetUniqueId() && parasite->GetAlive()) {
          parasiteList.push_back(parasite->GetUniqueId());
          float distSq = (parasite->GetTranslation() - GetTranslation()).MagSquared();
          if (distSq < minDistSq) {
            minDistSq = distSq;
            closestParasite = parasite;
          }
        }
      }
    }
    if (closestParasite && mParasiteSeparationWeight > 0.f && mParasiteSeparationDist > 0.f) {
      mParasiteSeparationMove =
          mSteeringBehaviors.Separation(*this, closestParasite->GetTranslation(),
                                        mParasiteSeparationDist) *
          mActiveSpeed;
    } else {
      mParasiteSeparationMove = CVector3f::Zero();
    }
    mParasiteCohesionMove =
        mSteeringBehaviors.Cohesion(*this, parasiteList, 0.6f, mgr) * mActiveSpeed;
    mParasiteAlignmentMove = mSteeringBehaviors.Alignment(*this, parasiteList, mgr) * mActiveSpeed;
  }

  if ((mgr.GetPlayer(0)->GetTranslation() - GetTranslation()).MagSquared() <
      mPlayerSeparationDist * mPlayerSeparationDist) {
    const CVector3f playerSeparation =
        ProjectVectorToPlane(mSteeringBehaviors.Separation(
                                 *this, mgr.GetPlayer(0)->GetTranslation(), mPlayerSeparationDist),
                             upVec) *
        mActiveSpeed;
    mBodyController->CommandMgr().DeliverCmd(
        CBCLocomotionCmd(playerSeparation, CVector3f::Zero(), mPlayerSeparationWeight));
  }

  if (!(mParasiteSeparationMove == CVector3f::Zero())) {
    mBodyController->CommandMgr().DeliverCmd(
        CBCLocomotionCmd(ProjectVectorToPlane(mParasiteSeparationMove, upVec), CVector3f::Zero(),
                         mParasiteSeparationWeight));
  }

  for (rstl::vector< CRepulsor >::iterator it = mDoorRepulsors.begin(); it != mDoorRepulsors.end();
       ++it) {
    const CRepulsor& r = *it;
    if ((r.GetPos() - GetTranslation()).MagSquared() < r.GetRadius() * r.GetRadius()) {
      const CVector3f doorSeparation =
          mSteeringBehaviors.Separation(*this, r.GetPos(), r.GetRadius()) * mActiveSpeed;
      mBodyController->CommandMgr().DeliverCmd(
          CBCLocomotionCmd(ProjectVectorToPlane(doorSeparation, upVec), CVector3f::Zero(), 1.f));
    }
  }

  if (mTelegraphRemTime <= 0.f) {
    mBodyController->CommandMgr().DeliverCmd(
        CBCLocomotionCmd(ProjectVectorToPlane(mParasiteCohesionMove, upVec), CVector3f::Zero(),
                         mParasiteCohesionWeight));
    mBodyController->CommandMgr().DeliverCmd(
        CBCLocomotionCmd(ProjectVectorToPlane(mParasiteAlignmentMove, upVec), CVector3f::Zero(),
                         mParasiteAlignmentWeight));
    const CVector3f seek =
        ProjectVectorToPlane(mSteeringBehaviors.Seek(*this, mDestPos), upVec) * mActiveSpeed;
    mBodyController->CommandMgr().DeliverCmd(CBCLocomotionCmd(
        ProjectVectorToPlane(seek, upVec), CVector3f::Zero(), mDestinationSeekWeight));
    mBodyController->CommandMgr().DeliverCmd(CBCLocomotionCmd(
        GetTransform().GetForward() * mActiveSpeed, CVector3f::Zero(), mForwardMoveWeight));
  }
}

// @ fn_47_2DE4
void CParasite::PreRender(CStateManager& mgr) {
  CPatterned::PreRender(mgr);
  if (mType == kPT_Crystallite) {
    SetModelFlags(GetModelFlags().UseShaderSet(x9c4_));
  }
}

// @ fn_47_2DC4
void CParasite::Render(const CStateManager& mgr) const { CWallCrawler::Render(mgr); }

// @ fn_47_2D40
const CDamageVulnerability* CParasite::GetDamageVulnerability() const {
  switch (mType) {
  case kPT_Oculus:
    if (mHalted) {
      return &mOculusHaltDVuln;
    }
    break;
  case kPT_IceZoomer:
    if (!mVulnerable) {
      return &CDamageVulnerability::ImmuneVulnerabilty();
    }
    break;
  case kPT_Crystallite:
    if (x9c0_ == 1) {
      return &mOculusHaltDVuln;
    }
    break;
  }
  return CPatterned::GetDamageVulnerability();
}

// @ fn_47_2CB8
EWeaponCollisionResponseTypes CParasite::GetCollisionResponseType(const CVector3f& position,
                                                                  const CVector3f& direction,
                                                                  const CWeaponMode& mode,
                                                                  int attributes) const {
  switch (mType) {
  case kPT_Crystallite:
    if (GetDamageVulnerability()->GetEffect(mode) == 1) {
      return static_cast< EWeaponCollisionResponseTypes >(15);
    }
    break;
  }
  return CPatterned::GetCollisionResponseType(position, direction, mode, attributes);
}

// @ fn_47_2BA0
CDamageInfo CParasite::GetContactDamage() const {
  switch (mType) {
  case kPT_Oculus:
    if (mHalted) {
      return mOculusHaltDInfo;
    }
    return CPatterned::GetContactDamage();
  case kPT_IceZoomer:
    if (!mVulnerable) {
      return mOculusHaltDInfo;
    }
    return CPatterned::GetContactDamage();
  default:
    return CPatterned::GetContactDamage();
  }
}

// @ fn_47_26C8
void CParasite::SetupIceZoomerCollision(CStateManager& mgr) {
  rstl::vector< CJointCollisionDescription > descs;
  descs.reserve(2);
  CAnimData& animData = *ModelData()->AnimationData();
  for (int i = 0; i < ARRAY_SIZE(skIceJoints); ++i) {
    const SSphereJointInfo& joint = skIceJoints[i];
    const CJointCollisionDescription desc = CJointCollisionDescription::SphereCollision(
        animData.GetLocatorSegId(rstl::string_l(joint.name)), CVector3f::Zero(),
        0.01f + mColSphere.GetSphere().GetRadius(), rstl::string_l(joint.name), 0.001f);
    descs.push_back_unsafe(desc);
  }
  RemoveMaterial(kMT_Solid, mgr);
  AddMaterial(kMT_ProjectilePassthrough, mgr);
  mCollisionActorManager =
      rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), descs, GetActive());
}

// @ fn_47_26A4
void CParasite::DestroyActorManager(CStateManager& mgr) { mCollisionActorManager->Destroy(mgr); }

// @ fn_47_25B8
void CParasite::SetupIceZoomerVulnerability(CStateManager& mgr, const CDamageVulnerability& dVuln,
                                            const CHealthInfo& hInfo) {
  for (uint i = 0; i < mCollisionActorManager->GetNumCollisionActors(); ++i) {
    const CJointCollisionDescription& cDesc = mCollisionActorManager->GetCollisionDescFromIndex(i);
    const TUniqueId id = cDesc.GetCollisionActorId();
    if (CCollisionActor* const act = TCastToPtr< CCollisionActor >(mgr.ObjectById(id))) {
      act->SetDamageVulnerability(dVuln);
      *act->HealthInfo() = hInfo;
    }
  }
}

// @ fn_47_22D8
void CParasite::UpdateCollisionActors(float dt, CStateManager& mgr) {
  mCollisionActorManager->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);
  if (!mVulnerable) {
    float totalHP = 0.f;
    for (uint i = 0; i < mCollisionActorManager->GetNumCollisionActors(); ++i) {
      const CJointCollisionDescription& cDesc =
          mCollisionActorManager->GetCollisionDescFromIndex(i);
      const TUniqueId id = cDesc.GetCollisionActorId();
      if (CCollisionActor* cact = TCastToPtr< CCollisionActor >(mgr.ObjectById(id))) {
        totalHP += cact->HealthInfo()->GetHP();
      }
    }
    if (totalHP <= 0.f) {
      mVulnerable = true;
      AddMaterial(kMT_Solid, mgr);
      RemoveMaterial(kMT_ProjectilePassthrough, mgr);
      DestroyActorManager(mgr);
      ModelData()->AnimationData()->SetSkinnedModel(*mExtraModel);
      if (mType == kPT_IceZoomer) {
        CSfxManager::AddEmitter(mHaltSfx, GetTranslation(), GetCurrentAreaId().Value(), true,
                                false);
        mBodyController->SetLocomotionType(pas::kLT_Internal8);
        mBodyController->CommandMgr().DeliverCmd(CBCMeleeAttackCmd(pas::kS_Zero));
        SendScriptMsgs(kSS_SpawnResidue, mgr, kInvalidUniqueId, kSM_None);
      }
    }
  }
}

// @ fn_47_22B8
void CParasite::MassiveDeath(CStateManager& mgr) { CPatterned::MassiveDeath(mgr); }

// @ fn_47_2298
void CParasite::MassiveFrozenDeath(CStateManager& mgr) { CPatterned::MassiveFrozenDeath(mgr); }

// @ fn_47_222C
void CParasite::SetupStateMachine(CStateManager& mgr) {
  StateMachine* stateMachine = mStateMachine.get();
  CPatterned::SetupStateMachine(mgr);
  stateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  stateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
}

// @ fn_47_1F3C
void CParasite::UpdateShell(CStateManager& mgr, int state) {
  x9c0_ = state;
  x9c4_ = rstl::min_val(state, GetModelData()->GetNumShaders() - 1);
  switch (state) {
  case 0:
    mgr.SendScriptMsg(x9ba_, GetUniqueId(), kSM_Activate);
    mgr.SendScriptMsg(x9ba_, GetUniqueId(), kSM_Decrement);
    mgr.SendScriptMsg(x9bc_, GetUniqueId(), kSM_Activate);
    break;
  case 1:
    mgr.SendScriptMsg(x9ba_, GetUniqueId(), kSM_Decrement);
    mgr.SendScriptMsg(x9ba_, GetUniqueId(), kSM_Deactivate);
    mgr.SendScriptMsg(x9bc_, GetUniqueId(), kSM_Deactivate);
    mOculusShotAt = true;
    break;
  case 2:
    mgr.SendScriptMsg(x9ba_, GetUniqueId(), kSM_Activate);
    mgr.SendScriptMsg(x9ba_, GetUniqueId(), kSM_Increment);
    mgr.SendScriptMsg(x9bc_, GetUniqueId(), kSM_Activate);
    mOculusShotAt = true;
    break;
  }
}

// @ fn_47_1568
CEntity* LoadParasite(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrParasite sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrParasite.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }

  return rs_new CParasite(
      mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
      static_cast< CPatterned::EFlavorType >(sldrThis.flavor),
      LdrToEntityInfo(info, sldrThis.editorProperties), LdrToTransform4f(sldrThis.editorProperties),
      *modelData, LdrToPatternedInfo(sldrThis.patterned, nullptr), kBT_WallWalker,
      sldrThis.telegraphDistance, sldrThis.waypointApproachDistance, sldrThis.wallTurnSpeed,
      sldrThis.floorTurnSpeed, sldrThis.downTurnSpeed, sldrThis.stuckTime, sldrThis.stickyReach,
      sldrThis.behaviorInfluenceRadius, sldrThis.separationDistance, sldrThis.separationPriority,
      sldrThis.alignmentPriority, sldrThis.cohesionPriority, sldrThis.pathFollowingPriority,
      sldrThis.forwardMovingPriority, sldrThis.playerAvoidanceDistance,
      sldrThis.playerAvoidancePriority, sldrThis.parasiteVisibleDistance, 0.f,
      sldrThis.initiallyPaused, CParasite::kPT_Parasite, CDamageVulnerability::NormalVulnerabilty(),
      CDamageInfo(CWeaponMode(kWT_Power), 0.f, 0.f, 0.f, false, false),
      CSfxManager::kInternalInvalidSfxId, CSfxManager::kInternalInvalidSfxId,
      CSfxManager::kInternalInvalidSfxId, kInvalidAssetId, kInvalidAssetId, 0.f, 1.f, CDamageInfo(),
      LdrToActorParameters(sldrThis.actorInformation));
}

// @ fn_47_CF8
CEntity* LoadBrizgee(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrBrizgee sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrBrizgee.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }

  return rs_new CParasite(
      mgr.AllocateUniqueId(), sldrThis.editorProperties.name, CPatterned::kFT_Zero, info,
      LdrToTransform4f(sldrThis.editorProperties), *modelData,
      LdrToPatternedInfo(sldrThis.patterned, nullptr), kBT_WallWalker, 10.f,
      sldrThis.waypointApproachDistance, sldrThis.wallTurnSpeed, sldrThis.floorTurnSpeed,
      sldrThis.downTurnSpeed, 0.2f, 0.4f, 6.f, 2.6f, 1.f, 0.8f, 0.7f, 0.9f,
      sldrThis.forwardMovingPriority, 1.3f, 0.2f, sldrThis.visibleDistance,
      sldrThis.shellOffSpeedMultiplier, false, CParasite::kPT_IceZoomer,
      LdrToDamageVulnerability(sldrThis.shellVulnerability),
      LdrToDamageInfo(sldrThis.shellContactDamage), sldrThis.shellBreakSound,
      sldrThis.playerPoisonSound, sldrThis.poisonHitSound, sldrThis.noShellModel,
      sldrThis.noShellSkin, sldrThis.shellHealth, 1.f, LdrToDamageInfo(sldrThis.poisonDamage),
      LdrToActorParameters(sldrThis.actorInformation));
}

// @ fn_47_148
CEntity* LoadCrystallite(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrCrystallite sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrCrystallite.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }

  const CDamageInfo contactDamage = LdrToDamageInfo(sldrThis.patterned.contactDamage);
  CDamageVulnerability vulnerability(LdrToDamageVulnerability(sldrThis.patterned.vulnerability));
  vulnerability.SetVulnerability(6, CWeaponTypeVulnerability::Normal());
  vulnerability.SetComboVulnerability(0, CWeaponTypeVulnerability::Normal());

  return rs_new CParasite(
      mgr.AllocateUniqueId(), sldrThis.editorProperties.name, CPatterned::kFT_Zero,
      LdrToEntityInfo(info, sldrThis.editorProperties), LdrToTransform4f(sldrThis.editorProperties),
      *modelData, LdrToPatternedInfo(sldrThis.patterned, nullptr), kBT_WallWalker, 10.f,
      sldrThis.waypointApproachDistance, sldrThis.wallTurnSpeed, sldrThis.floorTurnSpeed,
      sldrThis.downTurnSpeed, 0.2f, 0.4f, 6.f, 2.6f, 1.f, 0.8f, 0.7f, 0.9f,
      sldrThis.forwardMovingPriority, 1.3f, 0.2f, sldrThis.visibleDistance, sldrThis.stunTime,
      false, CParasite::kPT_Crystallite, vulnerability, contactDamage,
      CSfxManager::kInternalInvalidSfxId, CSfxManager::kInternalInvalidSfxId,
      CSfxManager::kInternalInvalidSfxId, kInvalidAssetId, kInvalidAssetId, 0.f, 1.f, contactDamage,
      LdrToActorParameters(sldrThis.actorInformation));
}

#ifndef MONOLITHIC
// @ fn_47_100
static void SetFuncPtrs() {
  static SParasite_FuncPtrs funcPtrs;
  funcPtrs.mLoadParasite = &LoadParasite;
  funcPtrs.mLoadBrizgee = &LoadBrizgee;
  funcPtrs.mLoadCrystallite = &LoadCrystallite;
  SetSParasite_FuncPtrs(&funcPtrs);
}

void RELMain() { SetFuncPtrs(); }

void RELExit() { SetSParasite_FuncPtrs(nullptr); }
#endif
