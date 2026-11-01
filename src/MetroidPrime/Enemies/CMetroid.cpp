#include "MetroidPrime/Enemies/CMetroid.hpp"

#include "MetroidPrime/CActorParameters.hpp"
#include "MetroidPrime/Enemies/CBabyMetroid.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrMetroidAlpha.hpp"

#include "Collision/CRayCastResult.hpp"
#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "Kyoto/Math/CQuaternion.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Math/CUnitVector3f.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/BodyState/CBodyState.hpp"
#include "MetroidPrime/BodyState/CBodyStateCmdMgr.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CKnockBackInfo.hpp"
#include "MetroidPrime/CSafeZoneManager.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Cameras/CFirstPersonCamera.hpp"
#include "MetroidPrime/Enemies/CSpacePirate.hpp"
#include "MetroidPrime/Player/CMorphBall.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTeamAiMgr.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWater.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "REL/REL_Setup.h"

#ifdef TARGET_PC
#include "MetroidPrime/ScriptObjects/CScriptSafeZone.hpp"
#include "MetroidPrime/Weapons/CBomb.hpp"
#include "MetroidPrime/Weapons/CGameProjectile.hpp"
#endif

static EMaterialTypes skSolidMaterial = kMT_Solid;

static const char* skJointNameList[] = {
    "Head_1",        "L_ankle",    "L_elbow",       "L_hip",   "L_knee",  "L_shoulder",
    "L_varias2_SDK", "L_wrist",    "Pelvis",        "R_ankle", "R_elbow", "R_hip",
    "R_knee",        "R_shoulder", "R_varias2_SDK", "Spine_1", "Spine_2",
};
static const char* const skPirateSuckJoint = "Head_1";
static const char* const skPirateRootJoint = "Skeleton_Root";

// Retail `__sinit_CMetroid_cpp` (0x8630-0x8A68, one call) builds exactly these four statics:
// `CWeaponTypeVulnerability(1.0 (.rodata 0x84), kE_Normal 0, false)` at 0x8658 copied to
// .bss 0x0 as {kWT_PowerBomb 5, 1.0, 0, 0} (skFaceHugOverrides); then 17 trigger, 9 state and
// 2 code-function entries (.data 0xD0, 0x24C, 0x2F4) - 16 B each: name .rodata reloc, then a
// {adjust 0, vtable offset -1, fn} mfp, or {0, vtable offset, 0} for the three virtuals
// (ShouldDodge +328 -> 0x6248, PathFind +332 -> 0x5A5C, Dodge +336 -> 0x4BF4 in the vtable at
// .data 0x364). Names, order and targets are identical to the tables below. Emulated from the
// retail asm: /tmp/opencode/reports/met1.md.
// @ fn_40_8630
static CDamageVulnerability::TWeaponVulnerability skFaceHugOverrides[] = {
    CDamageVulnerability::TWeaponVulnerability(
        kWT_PowerBomb, CWeaponTypeVulnerability(1.f, CWeaponTypeVulnerability::kE_Normal, false)),
};

// @ fn_40_85AC
static CDamageVulnerability FaceHugVulnerability() {
  return CDamageVulnerability(CDamageVulnerability::ImmuneVulnerabilty(), skFaceHugOverrides, 1,
                              CDamageVulnerability::kOF_Normal);
}

// @ fn_40_7B3C
CMetroid::CMetroid(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                   const CTransform4f& xf, const CModelData& mData, const CPatternedInfo& pInfo,
                   const CActorParameters& aParms, const CMetroidData& metroidData)
: CPatterned(kPAI_Metroid, uid, name, kFT_Zero, info, xf, mData, pInfo, kMT_Flyer, kCT_One,
             kBT_Flyer, aParms)
, x7c0_(CVector3f::Zero())
, mState(kAiState_Invalid)
, mAttackChance(0.f)
, mAttackState(0)
, mTeamAiManagerId(kInvalidUniqueId)
, mDodgeDirection(pas::kSD_Invalid)
, mMetroidData(metroidData)
, mCollisionPrimitive(CSphere(CVector3f::Zero(), 0.9f * GetModelData()->GetScale().GetY()),
                      GetMaterialList())
, mPathFindSearch(nullptr, 0x303, pInfo.GetPathfindingIndex(), 1.f, 1.f, 0, CPFRegion::kRP_Center)
, mAttackTarget(kInvalidUniqueId)
, mTelegraphAttackTime(0.f)
, mEnergyDrained(0.f)
, x9c0_(0.f)
, x9c4_(0.f)
, mScale1(GetModelData()->GetScale())
, mScale2(GetModelData()->GetScale())
, mScale3(GetModelData()->GetScale())
, mGrowthDuration(0.f)
, mGrowthEnergy(0.f)
, mLastGrowthEnergy(0.f)
, mSeekTime(0.f)
, mMaxSeekTime(0.f)
, mLoopAttackDistance(0.f)
, mDetachPos(CVector3f::Zero())
, mStandingFaceHugVulnerability(FaceHugVulnerability())
, mAlert(false)
, mGrowing(false)
, mShotAt(false)
, xa40_27_(false)
, xa40_28_(false)
, mIsAttacking(false)
, mRestoreSolidCollision(false)
, mRestoreCharacterCollision(false)
, mIsEnergyDrainVulnerable(false) {
  const CPASAnimParmData pasAnimParms(pas::kAS_LoopAttack, CPASAnimParm::FromEnum(2),
                                      CPASAnimParm::FromEnum(3));
  mLoopAttackDistance = GetAnimationDistance(pasAnimParms);
  SetCoefficientOfRestitutionModifier(0.9f);
}

// @ fn_40_781C
CMetroid::~CMetroid() {}

// @ fn_40_774C
void CMetroid::Think(float dt, CStateManager& mgr) {
  if (!GetActive()) {
    return;
  }

  if (CScriptTeamAiMgr::GetTeamAiRole(mgr, mTeamAiManagerId, GetUniqueId()) == nullptr) {
    SwarmAdd(mgr);
  }
  UpdateAILogicTimers(dt, mgr);
  SuckEnergyFromTarget(dt, mgr);
  PreventWorldCollisions(dt, mgr);
  RestoreSolidCollision(mgr);
  CPatterned::Think(dt, mgr);
}

// @ fn_40_75A0
void CMetroid::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  const EScriptObjectMessage message = msg.GetMessage();
  const TUniqueId sender = msg.GetSenderId();
  CPatterned::AcceptScriptMsg(mgr, msg);
  switch (message) {
  case kSM_Create:
    BodyController()->Activate(mgr, pas::kAS_Invalid);
    break;
  case kSM_Delete:
  case kSM_Deactivate:
    SwarmRemove(mgr);
    DetachFromTarget(mgr, false);
    break;
  case kSM_Damage:
  case kSM_ResistedDamage:
    ApplyDamageGrowth(mgr, sender);
    mShotAt = true;
    mAlert = true;
    break;
  case kSM_Alert:
    mAlert = true;
    break;
  case kSM_AreaLoaded:
    if (mTeamAiManagerId == kInvalidUniqueId) {
      mTeamAiManagerId = CScriptTeamAiMgr::GetAssociatedTeamId(*this, mgr);
    }
    {
      const TAreaId areaId = GetCurrentAreaId();
      mPathFindSearch.SetArea(
          mgr.GetWorld()->GetAreaAlways(areaId).GetPostConstructed()->mPathArea);
    }
    break;
  default:
    break;
  }
}

// @ fn_40_7580
void CMetroid::Render(const CStateManager& mgr) const { CPatterned::Render(mgr); }

// @ fn_40_74DC
const CDamageVulnerability* CMetroid::GetDamageVulnerability() const {
  if (IsSuckingEnergy()) {
    if (mIsEnergyDrainVulnerable) {
      return &mMetroidData.mEnergyDrainVulnerability;
    }
    return &mStandingFaceHugVulnerability;
  }
  if (mGrowing && !GetBodyController()->IsFrozen()) {
    return &mMetroidData.mEnergyDrainVulnerability;
  }
  if (GetBodyController()->GetPercentageFrozen() > 0.f) {
    return &mMetroidData.mFrozenVulnerability;
  }
  return CPatterned::GetDamageVulnerability();
}

// @ fn_40_74B0
const CDamageVulnerability* CMetroid::GetDamageVulnerability(const CVector3f&, const CVector3f&,
                                                             const CDamageInfo&) const {
  return GetDamageVulnerability();
}

// @ fn_40_741C
EWeaponCollisionResponseTypes CMetroid::GetCollisionResponseType(const CVector3f&, const CVector3f&,
                                                                 const CWeaponMode& mode,
                                                                 int) const {
  EWeaponCollisionResponseTypes response = static_cast< EWeaponCollisionResponseTypes >(33);
  const bool frozen = GetBodyController()->GetPercentageFrozen() > 0.f;
  if (!GetDamageVulnerability()->WeaponHits(mode, 0) && !frozen) {
    response = static_cast< EWeaponCollisionResponseTypes >(63);
  }
  return response;
}

// @ fn_40_7384
void CMetroid::DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                               float dt) {
  bool handled = false;
  switch (type) {
  case kUE_GenerateEnd:
    AddMaterial(kMT_Solid, mgr);
    handled = true;
    break;
  }
  if (!handled) {
    CPatterned::DoUserAnimEvent(mgr, node, type, dt);
  }
}

// @ fn_40_71B4
void CMetroid::Death(CStateManager& mgr, const CVector3f& direction, EScriptObjectState state) {
  CPatterned::Death(mgr, direction, state);
  mVerticalMovement = false;
  SetMuted(true);
  SwarmRemove(mgr);
}

// @ fn_40_7214
void CMetroid::KnockBack(CStateManager& mgr, const CKnockBackInfo& info) {
  const CWeaponMode& mode = info.GetDamageInfo().GetWeaponMode();
  const CDamageVulnerability* vulnerability = GetDamageVulnerability();
  const bool frozen = BodyController()->GetPercentageFrozen() > 0.f;
  if (mAttackState == 2) {
    if (vulnerability->WeaponHurts(mode)) {
      const float maxDrain = mMetroidData.mMaxEnergyDrainAllowed;
      mEnergyDrained = maxDrain * GetDamageMultiplier();
    }
  } else if (vulnerability->WeaponHits(mode, 0)) {
    const float variation = mAttackTimeVariation;
    const float average = GetAverageAttackTime();
    const float random = mgr.Random()->Float();
    mAttackChance = random * variation + average;
    if (frozen) {
      BodyController()->UnFreeze();
    }
    CPatterned::KnockBack(mgr, info);
  } else if (!frozen && vulnerability->WeaponHurts(mode) &&
             (mode.IsCharged() || mode.IsComboed() || mode.GetType() == kWT_Missile)) {
    CPatterned::KnockBack(mgr, info);
    mSeekTime = mMaxSeekTime;
  }
}

// @ fn_40_F50
bool CMetroid::CanBeIngPossessed(CStateManager& mgr) const {
  bool ret = false;
  if (CPatterned::CanBeIngPossessed(mgr) && !IsSuckingEnergy() && !mIsAttacking) {
    ret = true;
  }
  return ret;
}

// @ fn_40_6DBC
bool CMetroid::Attacked(CStateManager& mgr, const CTriggerData&) const {
  if (mGrowthEnergy - mLastGrowthEnergy > 0.f) {
    if (mLastGrowthEnergy < mMetroidData.mStage2GrowthEnergy) {
      return mGrowthEnergy >= mMetroidData.mStage2GrowthEnergy;
    }
    if (mGrowthEnergy >= mMetroidData.mExplosionGrowthEnergy) {
      return true;
    }
  }
  return false;
}

// @ fn_40_6D2C
bool CMetroid::ShouldAttack(CStateManager& mgr, const CTriggerData&) const {
  if (CanStartAttack(mgr)) {
    if (CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamAiManagerId))) {
      return team->StartMeleeAttack(GetUniqueId());
    }
    return true;
  }
  return false;
}

// @ fn_40_4780
bool CMetroid::IsSuckingEnergy() const {
  return mAttackState == 2 && !GetBodyController()->IsFrozen();
}

// @ fn_40_470C
bool CMetroid::IsPirateValidTarget(const CSpacePirate& pirate) const {
  if (pirate.GetAttachedActor() == kInvalidUniqueId) {
    const CHealthInfo* healthInfo = pirate.GetHealthInfo();
    return healthInfo != nullptr && healthInfo->GetHP() > 0.f;
  }
  return false;
}

// @ fn_40_15A8
bool CMetroid::IsPlayerInFluid(const CPlayer& player, const CStateManager& mgr) const {
  if (player.GetFluidCount() != 0) {
    if (player.InFluidId() != kInvalidUniqueId) {
      const CVector3f aimPos = player.GetAimPosition(mgr, 0.f);
      if (const CScriptWater* water =
              TCastToConstPtr< CScriptWater >(mgr.GetObjectById(player.InFluidId()))) {
        return aimPos.GetZ() < water->GetTriggerBoundsWR().GetMaxPoint().GetZ();
      }
    }
    return true;
  }
  return false;
}

// @ fn_40_1938
bool CMetroid::IsTargetGettingSucked(const CStateManager& mgr) const {
  if (const CEntity* target = mgr.GetObjectById(GetAttackTargetId())) {
    if (const CPlayer* player = TCastToConstPtr< CPlayer >(target)) {
      const TUniqueId attached = player->GetAttachedActorId();
      if (attached != kInvalidUniqueId && attached != GetUniqueId()) {
        return true;
      }
    } else if (const CSpacePirate* pirate = TCastToConstPtr< CSpacePirate >(target)) {
      const TUniqueId attached = pirate->GetAttachedActor();
      if (attached != kInvalidUniqueId && attached != GetUniqueId()) {
        return true;
      }
    }
  }
  return false;
}

// @ fn_40_1504
void CMetroid::UpdateAILogicTimers(float dt, CStateManager& mgr) {
  if (IsTargetGettingSucked(mgr)) {
    const float variation = mAttackTimeVariation;
    const float average = GetAverageAttackTime();
    const float random = mgr.Random()->Float();
    mAttackChance = random * variation + average;
  } else if (mAttackChance > 0.f) {
    mAttackChance -= dt;
  }
}

// @ fn_40_13D4
bool CMetroid::CanStartAttack(CStateManager& mgr) const {
  if (mAttackChance <= 0.f) {
    const CEntity* target = mgr.GetObjectById(mAttackTarget);
    if (const CPlayer* player = TCastToConstPtr< CPlayer >(target)) {
      if (IsPlayerInFluid(*player, mgr) ||
          mgr.GetSafeZoneManager()->IsObjectInHurtfulSafeZone(*player, mgr) ||
          player->GetMorphBall()->InScrewAttackMode() ||
          ((player->GetSpawnedMorphballState() == CPlayer::kMS_Unmorphed
                ? player->GetMorphballTransitionState()
                : CPlayer::kMS_Unmorphed) == CPlayer::kMS_Morphed &&
           player->GetMorphBall()->GetBallState() == CMorphBall::kBS_Spider)) {
        return false;
      }
    }
    if (target != nullptr && target->GetCurrentAreaId() == GetCurrentAreaId()) {
      return !IsTargetGettingSucked(mgr);
    }
  }
  return false;
}

// @ fn_40_3E88
void CMetroid::SwarmRemove(CStateManager& mgr) {
  if (mTeamAiManagerId != kInvalidUniqueId) {
    if (CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamAiManagerId))) {
      if (team->IsPartOfTeam(GetUniqueId())) {
        team->QuitTeam(GetUniqueId());
      }
    }
  }
}

// @ fn_40_3F24
void CMetroid::SwarmAdd(CStateManager& mgr) {
  if (mTeamAiManagerId != kInvalidUniqueId) {
    if (CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamAiManagerId))) {
      if (!team->IsPartOfTeam(GetUniqueId())) {
        team->JoinTeam(*this, CTeamAiRole::kTAR_Melee, CTeamAiRole::kTAR_Invalid,
                       CTeamAiRole::kTAR_Invalid);
      }
    }
  }
}

// @ fn_40_625C
bool CMetroid::Leash(CStateManager& mgr, const CTriggerData&) const {
  if (const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mAttackTarget))) {
    if (IsPlayerInFluid(*player, mgr)) {
      return true;
    }
  }
  const CVector3f leashDelta = mLatestLeashPosition - GetTranslation();
  if (leashDelta.MagSquared() > mLeashRadius * mLeashRadius) {
    if (mAttackTarget != kInvalidUniqueId) {
      if (const CEntity* target = mgr.GetObjectById(mAttackTarget)) {
        const CActor* actor = static_cast< const CActor* >(target);
        const CVector3f targetDelta = actor->GetTranslation() - GetTranslation();
        return targetDelta.MagSquared() > mPlayerLeashRadius * mPlayerLeashRadius &&
               mCurPlayerLeashTime > mPlayerLeashTime;
      }
    }
    return true;
  }
  return false;
}

// @ fn_40_6EE4
bool CMetroid::LostInterest(CStateManager& mgr, const CTriggerData&) const {
  if (mAttackTarget != kInvalidUniqueId) {
    if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget))) {
      if (const CSpacePirate* pirate = TCastToConstPtr< CSpacePirate >(actor)) {
        if (pirate->GetAttachedActor() != kInvalidUniqueId) {
          return true;
        }
      } else if (const CPlayer* player =
                     TCastToConstPtr< CPlayer >(mgr.GetObjectById(mAttackTarget))) {
        if (IsPlayerInFluid(*player, mgr) || player->GetCurrentAreaId() != GetCurrentAreaId() ||
            mgr.GetSafeZoneManager()->IsObjectInHurtfulSafeZone(*player, mgr)) {
          return true;
        }
      }
      return false;
    }
  }
  return true;
}

// @ fn_40_6E18
bool CMetroid::PatternShagged(CStateManager& mgr, const CTriggerData&) const {
  if (mAttackTarget != kInvalidUniqueId) {
    if (const CSpacePirate* pirate =
            TCastToConstPtr< CSpacePirate >(mgr.GetObjectById(mAttackTarget))) {
      if (!pirate->GetAlive()) {
        return true;
      }
    }
    if (!CanStartAttack(mgr)) {
      return true;
    }
    if (mState == kAiState_Two) {
      return mSeekTime >= mMaxSeekTime;
    }
    return false;
  }
  return true;
}

// @ fn_40_694C
bool CMetroid::InPosition(CStateManager& mgr, const CTriggerData&) const {
  if (mPathFindSearch.GetCurrentWaypoint() < mPathFindSearch.GetWaypoints().size() - 1) {
    const CVector3f delta = x7c0_ - GetTranslation();
    return delta.MagSquared() < 4.f;
  }
  return true;
}

// @ fn_40_6864
bool CMetroid::InRange(CStateManager& mgr, const CTriggerData&) const {
  if (mAttackTarget != kInvalidUniqueId) {
    if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget))) {
      if (const CSpacePirate* pirate = TCastToConstPtr< CSpacePirate >(actor)) {
        if (!IsPirateValidTarget(*pirate)) {
          return false;
        }
      }
      return (actor->GetTranslation() - GetTranslation()).MagSquared() <
             mMaxAttackRange * mMaxAttackRange;
    }
  }
  return false;
}

// @ fn_40_64A0
bool CMetroid::AggressionCheck(CStateManager& mgr, const CTriggerData&) const {
  if (mAttackTarget != kInvalidUniqueId) {
    const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget));
    if (const CSpacePirate* pirate = TCastToConstPtr< CSpacePirate >(actor)) {
      if (!IsPirateValidTarget(*pirate)) {
        return false;
      }
    }
    if (actor != nullptr) {
      const CVector3f delta = actor->GetTranslation() - GetTranslation();
      if (delta.MagSquared() < mDetectionRange * mDetectionRange) {
        if (mDetectionHeightRange > 0.f) {
          return delta.GetZ() * delta.GetZ() < mDetectionHeightRange * mDetectionHeightRange;
        }
        return true;
      }
    }
  }
  return false;
}

// @ fn_40_63A4
bool CMetroid::ShouldTurn(CStateManager& mgr, const CTriggerData&) const {
  if (mAttackTarget != kInvalidUniqueId) {
    if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget))) {
      const CVector2f direction = (actor->GetTranslation() - GetTranslation()).ToVec2f();
      const CVector2f forward = GetTransform().GetForward().ToVec2f();
      return CVector2f::GetAngleDiff(forward, direction) > CRelAngle::FromDegrees(15.f).AsRadians();
    }
  }
  return false;
}

// @ fn_40_61B8
void CMetroid::Patrol(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    BodyController()->SetLocomotionType(pas::kLT_Relaxed);
    mAttackTarget = kInvalidUniqueId;
    mShotAt = false;
    break;
  }
  CPatterned::Patrol(mgr, msg, dt);
}

// @ fn_40_5A5C
void CMetroid::PathFind(CStateManager& mgr, EStateMsg msg, float dt) {
  mPathFindNavigation.PathFind(mgr, msg, dt, *this);
  switch (msg) {
  case kStateMsg_Update:
    ApplySeparationBehavior(mgr);
    break;
  }
}

// @ fn_40_590C
void CMetroid::TurnAround(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Update:
    if (mAttackTarget != kInvalidUniqueId) {
      if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget))) {
        UpdateAttackTarget(mgr);
        const CVector3f direction = actor->GetTranslation() - GetTranslation();
        if (ShouldTurn(mgr, CTriggerData(0.f)) && direction.CanBeNormalized()) {
          BodyController()->CommandMgr().DeliverCmd(
              CBCLocomotionCmd(CVector3f::Zero(), direction.AsNormalized(), 1.f));
        }
      }
    }
    break;
  }
}

// @ fn_40_4E44
void CMetroid::WallHang(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    BodyController()->SetLocomotionType(pas::kLT_Crouch);
    mState = kAiState_Zero;
    RemoveMaterial(kMT_Solid, mgr);
    mRestoreSolidCollision = false;
    break;
  case kStateMsg_Update:
    switch (mState) {
    case kAiState_Zero:
      if (mAlert) {
        mState = kAiState_One;
        mRestoreSolidCollision = true;
        mDetachPos = CVector3f::Zero();
      }
      break;
    case kAiState_One:
      if (BodyController()->GetCurrentStateId() == pas::kAS_Generate) {
        mState = kAiState_Two;
      } else {
        BodyController()->CommandMgr().DeliverCmd(CBCGenerateCmd(pas::kGType_Zero, -1));
      }
      break;
    case kAiState_Two:
      if (BodyController()->GetCurrentStateId() != pas::kAS_Generate) {
        mState = kAiState_Over;
      }
      break;
    }
    break;
  case kStateMsg_Deactivate:
    BodyController()->SetLocomotionType(pas::kLT_Relaxed);
    xa40_27_ = true;
    break;
  }
}

// @ fn_40_4BF4
void CMetroid::Dodge(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    if (mDodgeDirection != pas::kSD_Invalid) {
      BodyController()->CommandMgr().DeliverCmd(CBCStepCmd(mDodgeDirection, pas::kStep_Dodge));
      mState = kAiState_Two;
    }
    break;
  case kStateMsg_Update:
    if (BodyController()->GetCurrentStateId() != pas::kAS_Step) {
      mState = kAiState_Over;
    } else if (mAttackTarget != kInvalidUniqueId) {
      if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget))) {
        BodyController()->CommandMgr().SetTargetVector(actor->GetTranslation() - GetTranslation());
      }
    }
    break;
  case kStateMsg_Deactivate:
    mDodgeDirection = pas::kSD_Invalid;
    break;
  }
}

// @ fn_40_44EC
float CMetroid::GetGrowthStage() const {
  if (mGrowthEnergy < mMetroidData.mStage2GrowthEnergy) {
    return 1.f + mGrowthEnergy / mMetroidData.mStage2GrowthEnergy;
  }
  if (mGrowthEnergy < mMetroidData.mExplosionGrowthEnergy) {
    return 2.f + (mGrowthEnergy - mMetroidData.mStage2GrowthEnergy) /
                     (mMetroidData.mExplosionGrowthEnergy - mMetroidData.mStage2GrowthEnergy);
  }
  return 3.f;
}

// @ fn_40_44B4
float CMetroid::GetDamageMultiplier() const {
  float result = 0.5f * (GetGrowthStage() - 1.f) + 1.f;
  return result;
}

// @ fn_40_3DCC
bool CMetroid::AttachToTarget(CStateManager& mgr) {
  if (CPlayer* player = TCastToPtr< CPlayer >(mgr.ObjectById(mAttackTarget))) {
    if (player->AttachActorToPlayer(GetUniqueId(), false)) {
      player->GetEnergyDrain().AddEnergyDrainSource(GetUniqueId(), 1.f);
      return true;
    }
  } else {
    return PreDamageSpacePirate(mgr);
  }
  return false;
}

#ifdef TARGET_PC
// Absent upstream. The pirate the metroid latches onto takes hold of it and is brought to one
// below its x-damage threshold, so its next hit knocks it into the reaction.
// @ fn_40_4300
bool CMetroid::PreDamageSpacePirate(CStateManager& mgr) {
  if (mAttackTarget == kInvalidUniqueId) { // 0x4328-0x4334 lhz 0x9b4; cmplw kInvalidUniqueId; beq
    return false;                          // 0x4494 li r3, 0
  }
  CSpacePirate* pirate = TCastToPtr< CSpacePirate >(mgr.ObjectById(mAttackTarget)); // 0x4348
  if (pirate == nullptr) {                                                         // 0x4354 beq
    return false;
  }
  if (!IsPirateValidTarget(*pirate)) { // 0x4360-0x4368 clrlwi.; beq
    return false;
  }
  if (!pirate->AttachActorToPirate(GetUniqueId())) { // 0x4380 bl fn_72_E0D8; 0x4388 beq
    return false;
  }
  // 0x43AC lfs f1, 0x3fc(r29): CPatterned 0x3fc is mXDamageThreshold (CPatterned ctor 0x80079F98
  // stfs from CPatternedInfo 0xbc), read through CPatterned::GetXDamageThreshold.
  // 0x4394-0x43D4: CWeaponMode type 0 (kWT_Power), the 0x8000/0x4000/0x2000 bits cleared.
  // 0x43A4 lbl_40_rodata_84 = 1.0f; 0x43BC fsubs: damage and radius damage = threshold - 1.
  // 0x43C0 lbl_40_rodata_A8 = 0.0f: radius and knockback. 0x43E8 rlwimi 7,24,24 sets byte 0x1a
  // 0x80 (mNoImmunity), 0x43FC rlwimi 6,25,25 clears 0x40 (mApplyRadiusDamage).
  const CDamageInfo info(CWeaponMode(kWT_Power), pirate->GetXDamageThreshold() - 1.f, 0.f, 0.f,
                         true);
  // 0x4428 __shl2i 1 << skSolidMaterial (include), 0x4454/0x4460 exclude 0, 0x4464 type 3
  // (kFT_IncludeExclude); 0x4488 ApplyDamage(this, mAttackTarget, this, ..., sZeroVector).
  mgr.ApplyDamage(GetUniqueId(), mAttackTarget, GetUniqueId(), info,
                  CMaterialFilter::MakeIncludeExclude(CMaterialList(skSolidMaterial),
                                                      CMaterialList()),
                  CVector3f::Zero());
  return true; // 0x448C li r3, 1
}
#endif

// @ fn_40_6FF0
bool CMetroid::AttackOver(CStateManager& mgr, const CTriggerData&) const {
  if (mState == kAiState_Two && !GetBodyController()->IsFrozen()) {
    const CVector3f targetPos = GetAttackTargetPos(mgr);
    const float heightDifference = CMath::AbsF(targetPos.GetZ() - GetTranslation().GetZ());
    const float scale = 0.8f * GetModelData()->GetScale().GetY();
    if (heightDifference < scale) {
      if (const CPhysicsActor* actor =
              TCastToConstPtr< CPhysicsActor >(mgr.GetObjectById(mAttackTarget))) {
        const CAABox actorBounds = actor->GetBoundingBox();
        const CAABox collisionBounds = mCollisionPrimitive.CalculateAABox(GetTransform());
        const CVector3f min = collisionBounds.GetMinPoint() - CVector3f(scale, scale, scale);
        const CVector3f max = collisionBounds.GetMaxPoint() + CVector3f(scale, scale, scale);
        const CAABox scaledBounds(min, max);
        return scaledBounds.DoBoundsOverlap(actorBounds);
      }
    }
  }
  return false;
}

// @ fn_40_4544
CVector3f CMetroid::GetAttackTargetPos(const CStateManager& mgr) const {
  if (mAttackTarget != kInvalidUniqueId) {
    const CEntity* target = mgr.GetObjectById(GetAttackTargetId());
    if (const CPlayer* player = TCastToConstPtr< CPlayer >(target)) {
      CVector3f pos = player->GetTranslation();
      if (player->GetMorphballTransitionState() != CPlayer::kMS_Morphed) {
        pos += CVector3f(0.f, 0.f, -0.6f + player->GetEyeHeight());
      } else {
        pos = player->GetMorphBall()->GetBallToWorld().GetTranslation();
      }
      return pos;
    }
    if (target != nullptr) {
      const CActor* actor = static_cast< const CActor* >(target);
      const CTransform4f locXf = actor->GetLocatorTransform(rstl::string_l(skPirateSuckJoint));
      const CVector3f locPos = locXf.GetTranslation();
      return actor->GetTranslation() +
             CVector3f(0.f, 0.f, locPos.GetZ() * actor->GetModelData()->GetScale().GetZ() + 0.4f);
    }
  }
  return GetTranslation();
}

// @ fn_40_40F4
void CMetroid::ApplySeparationBehavior(CStateManager& mgr) {
  const CObjectList& list = mgr.GetObjectListById(kOL_ListeningAi);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    if (const CPatterned* ai = TCastToConstPtr< CPatterned >(list[i])) {
      if (ai != this && ai->GetCurrentAreaId() == GetCurrentAreaId()) {
        const CVector3f separation = mSteeringBehaviors.Separation(
            *this, ai->GetTranslation(), 9.f * GetModelData()->GetScale().GetX());
        if (separation.IsMagnitudeSafe()) {
          BodyController()->CommandMgr().DeliverCmd(
              CBCLocomotionCmd(separation.AsNormalized(), CVector3f::Zero(), 0.5f));
        }
      }
    }
  }
}

// @ fn_40_3B18
void CMetroid::DetachFromTarget(CStateManager& mgr, bool fromDock) {
  CActor* target = nullptr;
  CVector3f direction = CVector3f::Forward();
  CTransform4f xf = CTransform4f::Identity();
  if (CPlayer* player = TCastToPtr< CPlayer >(mgr.ObjectById(mAttackTarget))) {
    if (player->GetAttachedActorId() == GetUniqueId()) {
      player->DetachActorFromPlayer();
      target = player;
      if (player->GetMorphballTransitionState() != CPlayer::kMS_Morphed) {
        direction = player->GetTransform().GetForward();
        xf = player->GetTransform();
      } else {
        const CQuaternion rot = CQuaternion::ZRotation(CRelAngle::FromRadians(GetYaw())) *
                                CQuaternion::ZRotation(CRelAngle::FromRadians(M_PIF));
        const CMatrix3f mat = rot.BuildTransform();
        direction = mat * CVector3f::Forward();
        xf = CTransform4f(mat, player->GetTranslation());
      }
      player->GetEnergyDrain().RemoveEnergyDrainSource(GetUniqueId());
      mDetachPos = player->GetAimPosition(mgr, 0.f);
    }
  } else if (mAttackTarget != kInvalidUniqueId) {
    if (CSpacePirate* pirate = TCastToPtr< CSpacePirate >(mgr.ObjectById(mAttackTarget))) {
      if (pirate->GetAttachedActor() == GetUniqueId()) {
        pirate->DetachActorFromPirate();
        target = pirate;
        direction = pirate->GetTransform().GetForward();
        xf = pirate->GetTransform();
        mDetachPos = GetTranslation();
      }
    }
  }
  if (fromDock) {
    SetupExitFaceHugDirection(target, mgr, direction, xf);
    mRestoreSolidCollision = mRestoreCharacterCollision = true;
  }
}

// @ fn_40_47AC
void CMetroid::SetPatrolDest(CStateManager& mgr, float) {
  BodyController()->SetLocomotionType(pas::kLT_Relaxed);
  BodyController()->CommandMgr().SetSteeringBlendMode(kSBM_Normal);
  mAttackTarget = kInvalidUniqueId;
  mShotAt = false;
  mAlert = false;
  if (HasPatrolPath(mgr, CTriggerData(0.f))) {
    x7c0_ = mWaypointNavigation.GetDestinationPosition();
  } else {
    x7c0_ = mLatestLeashPosition;
  }
  mPathFindNavigation.SetDestination(x7c0_);
  mPathFindNavigation.SetFaceTarget(kInvalidUniqueId);
}

// @ fn_40_1170
TUniqueId CMetroid::FindNearestTarget(CStateManager& mgr, TUniqueId ignore) const {
  CObjectList& list = mgr.ObjectListById(kOL_Actor);
  TUniqueId result = kInvalidUniqueId;
  float minDistSq = 3.4028235e38f;
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    CActor* actor = static_cast< CActor* >(list[i]);
    if (actor != nullptr && actor->GetUniqueId() != GetUniqueId() &&
        actor->GetUniqueId() != ignore) {
      const float distSq = (actor->GetTranslation() - GetTranslation()).MagSquared();
      if (distSq < minDistSq) {
        if (const CSpacePirate* pirate = TCastToConstPtr< CSpacePirate >(actor)) {
          if (IsPirateValidTarget(*pirate)) {
            minDistSq = distSq;
            result = pirate->GetUniqueId();
          }
        } else if (const CPlayer* player = TCastToConstPtr< CPlayer >(actor)) {
          if (!IsPlayerInFluid(*player, mgr)) {
            result = player->GetUniqueId();
            minDistSq = distSq;
          }
        }
      }
    }
  }
  return result;
}

// @ fn_40_12CC
void CMetroid::SelectNewTarget(CStateManager& mgr) {
  if (mAttackTarget == kInvalidUniqueId) {
    mAttackTarget = FindNearestTarget(mgr, kInvalidUniqueId);
  }
  if (CSpacePirate* pirate = TCastToPtr< CSpacePirate >(mgr.ObjectById(mAttackTarget))) {
    pirate->SetAttackTarget(mgr, GetUniqueId());
    mgr.DeliverScriptMsg(CScriptMsg(GetUniqueId(), pirate->GetUniqueId(), kSM_Alert));
  }
}

// @ fn_40_1080
void CMetroid::UpdateAttackTarget(CStateManager& mgr) {
  if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget))) {
    if (const CPlayer* player = TCastToConstPtr< CPlayer >(actor)) {
      if (player->GetAttachedActorId() != kInvalidUniqueId) {
        const TUniqueId next = FindNearestTarget(mgr, mAttackTarget);
        if (next != kInvalidUniqueId) {
          mAttackTarget = next;
        }
      }
    } else if (const CSpacePirate* pirate = TCastToConstPtr< CSpacePirate >(actor)) {
      if (!IsPirateValidTarget(*pirate)) {
        SelectNewTarget(mgr);
      }
    }
  } else {
    SelectNewTarget(mgr);
  }
}

// @ fn_40_5AC0
void CMetroid::SelectTarget(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mState = xa40_27_ ? kAiState_Over : kAiState_One;
    SelectNewTarget(mgr);
    SendScriptMsgs(kSS_Attack, mgr, kSM_None);
    break;
  case kStateMsg_Update:
    switch (mState) {
    case kAiState_One:
      if (BodyController()->GetCurrentStateId() == pas::kAS_Generate) {
        mState = kAiState_Two;
      } else {
        BodyController()->CommandMgr().DeliverCmd(CBCGenerateCmd(pas::kGType_Three, -1));
      }
      break;
    case kAiState_Two:
      if (BodyController()->GetCurrentStateId() != pas::kAS_Generate) {
        mState = kAiState_Over;
      } else if (mAttackTarget != kInvalidUniqueId) {
        if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget))) {
          BodyController()->CommandMgr().SetTargetVector(actor->GetTranslation() -
                                                         GetTranslation());
        }
      }
      break;
    }
    break;
  }
}

// @ fn_40_65AC
bool CMetroid::InDetectionRange(CStateManager& mgr, const CTriggerData&) const {
  if (mAttackTarget == kInvalidUniqueId) {
    if (mAlert) {
      return true;
    }
    const float rangeSq = mDetectionRange * mDetectionRange;
    CObjectList& list = mgr.ObjectListById(kOL_Actor);
    const CVector3f pos = GetTranslation();
    for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
      CActor* actor = static_cast< CActor* >(list[i]);
      if (actor != nullptr && actor->GetUniqueId() != GetUniqueId()) {
        const CVector3f delta = actor->GetTranslation() - pos;
        if (delta.MagSquared() < rangeSq) {
          CSpacePirate* pirate = TCastToPtr< CSpacePirate >(actor);
          if (pirate != nullptr && IsPirateValidTarget(*pirate)) {
            pirate->SetAttackTarget(mgr, GetUniqueId());
            return true;
          }
          const CPlayer* player = TCastToConstPtr< CPlayer >(actor);
          if (player != nullptr && !IsPlayerInFluid(*player, mgr) &&
              !mgr.GetSafeZoneManager()->IsObjectInHurtfulSafeZone(*player, mgr) &&
              player->GetCurrentAreaId() == GetCurrentAreaId()) {
            return true;
          }
        }
      }
    }
  } else {
    CEntity* target = mgr.ObjectById(mAttackTarget);
    const CPlayer* player = TCastToConstPtr< CPlayer >(target);
    if (player != nullptr && (IsPlayerInFluid(*player, mgr) ||
                              mgr.GetSafeZoneManager()->IsObjectInHurtfulSafeZone(*player, mgr) ||
                              player->GetCurrentAreaId() != GetCurrentAreaId())) {
      return false;
    }
    if (target != nullptr) {
      const CVector3f delta = static_cast< CActor* >(target)->GetTranslation() - GetTranslation();
      const float heightRange = mDetectionHeightRange;
      if (delta.MagSquared() < mDetectionRange * mDetectionRange && heightRange > 0.f) {
        return delta.GetZ() * delta.GetZ() < heightRange * heightRange;
      }
    }
  }
  return false;
}

// @ fn_40_1698
CVector3f CMetroid::GetOrigin(const CStateManager& mgr, const CTeamAiRole& role,
                              const CVector3f& aimPos) const {
  CVector3f result = GetTranslation();
  const float range = 0.5f * (mMinAttackRange + mMaxAttackRange);
  const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(GetAttackTargetId()));
  if (const CPlayer* player = TCastToConstPtr< CPlayer >(actor)) {
    if (player->GetMorphballTransitionState() == CPlayer::kMS_Morphed) {
      const CVector3f direction((GetTranslation() - player->GetTranslation()).ToVec2f(), 0.f);
      const CVector3f& face = direction.CanBeNormalized() ? direction.AsNormalized()
                                                          : player->GetTransform().GetForward();
      const float height = 0.5f + player->GetTranslation().GetZ();
      result = player->GetTranslation() + range * face;
      result.SetZ(height);
    } else {
      const CVector3f forward = player->GetTransform().GetForward();
      const float height = 0.5f + aimPos.GetZ();
      result = aimPos + range * forward;
      result.SetZ(height);
    }
  } else if (actor != nullptr) {
    const CVector3f direction((GetTranslation() - actor->GetTranslation()).ToVec2f(), 0.f);
    const CVector3f& face =
        direction.CanBeNormalized() ? direction.AsNormalized() : actor->GetTransform().GetForward();
    const float height = 0.5f + actor->GetTranslation().GetZ();
    result = actor->GetTranslation() + range * face;
    result.SetZ(height);
  }
  return result;
}

// @ fn_40_34D4
void CMetroid::PreventWorldCollisions(float dt, CStateManager& mgr) {
  const float size = 2.f * mCollisionPrimitive.GetSphere().GetRadius();
  if (IsSuckingEnergy()) {
    if (CPlayer* player = TCastToPtr< CPlayer >(mgr.ObjectById(mAttackTarget))) {
      float mass = 300.f;
      if (player->GetMorphballTransitionState() == CPlayer::kMS_Unmorphed) {
        const float scale = rstl::min_val(1.33f * x9c0_, 1.f);
        mass = 300.f * (1.f - scale) + 7500.f * scale;
      }
      CGameCollision::PushActorAwayFromWalls(mgr, *player, dt, 0.25f, size, mass, 8, 0.5f);
    }
    x9c4_ = 0.f;
  } else if (mRestoreSolidCollision || mRestoreCharacterCollision) {
    x9c4_ += dt;
    if (x9c4_ > 6.f) {
      MassiveDeath(mgr);
    } else if (mRestoreSolidCollision && x9c4_ > 0.25f) {
      RemoveMaterial(kMT_Solid, mgr);
    }
    CGameCollision::PushActorAwayFromWalls(mgr, *this, dt, 0.25f, size, 15000.f, 8, 0.5f);
  } else {
    x9c4_ = 0.f;
  }
}

// @ fn_40_3110
void CMetroid::RestoreSolidCollision(CStateManager& mgr) {
  const CMaterialFilter filter =
      CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid, kMT_AIBlock));
  if (mRestoreSolidCollision && !CGameCollision::DetectStaticCollisionBoolean(
                                    mgr, mCollisionPrimitive, GetTransform(), filter)) {
    bool add = true;
    if (mDetachPos.IsNonZero()) {
      const CVector3f dir = GetTranslation() - mDetachPos;
      const float mag = dir.Magnitude();
      if (mag > 0.f) {
        add = CGameCollision::RayStaticLineOfSightTest(mgr, mDetachPos, (1.f / mag) * dir, mag,
                                                       filter);
      }
    }
    if (add) {
      AddMaterial(kMT_Solid, mgr);
      mRestoreSolidCollision = false;
    }
  }
  if (mRestoreCharacterCollision) {
    rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
    const CMaterialFilter nearFilter =
        CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid, kMT_Player, kMT_Character));
    const float radius = mLoopAttackDistance * GetModelData()->GetScale().GetY();
    const CVector3f extent(radius, radius, radius);
    const CAABox box(GetTranslation() - extent, GetTranslation() + extent);
    mgr.BuildNearList(nearList, box, nearFilter, this);
    if (!CGameCollision::DetectDynamicCollisionBoolean(mCollisionPrimitive, GetTransform(),
                                                       nearList, mgr)) {
      mRestoreCharacterCollision = false;
      CMaterialFilter matFilter = GetMaterialFilter();
      matFilter.ExcludeList().Remove(CMaterialList(kMT_Character, kMT_Player));
      SetMaterialFilter(matFilter);
    }
  }
}

// @ fn_40_304C
void CMetroid::DisableSolidCollision(CMetroid& target) {
  CMaterialFilter filter = target.GetMaterialFilter();
  filter.ExcludeList().Add(CMaterialList(kMT_Character, kMT_Player));
  target.SetMaterialFilter(filter);
}

// @ fn_40_69B0
bool CMetroid::InAttackPosition(CStateManager& mgr, const CTriggerData&) const {
  if (mAttackTarget != kInvalidUniqueId) {
    const CActor* actor = static_cast< const CActor* >(mgr.GetObjectById(mAttackTarget));
    if (actor != nullptr && actor->GetCurrentAreaId() == GetCurrentAreaId()) {
      const CVector3f direction = GetTranslation() - actor->GetTranslation();
      const CVector3f actorForward = actor->GetTransform().GetForward();
      float maxAngle = M_PIF;
      if (const CPlayer* player = TCastToConstPtr< CPlayer >(actor)) {
        if (IsPlayerInFluid(*player, mgr)) {
          return false;
        }
        if (player->GetMorphballTransitionState() != CPlayer::kMS_Morphed && !xa40_28_) {
          maxAngle = CRelAngle::FromDegrees(45.f).AsRadians();
        }
      }
      if (CVector2f::GetAngleDiff(direction.ToVec2f(), actorForward.ToVec2f()) < maxAngle &&
          CVector3f::Dot(direction, GetTransform().GetForward()) < 0.f) {
        bool inPosition =
            (x7c0_ - GetTranslation()).MagSquared() < mMaxAttackRange * mMaxAttackRange;
        if (inPosition) {
          const float myZ = GetTranslation().GetZ();
          const float targetZ = actor->GetTranslation().GetZ();
          inPosition = myZ > targetZ && myZ < 0.5f + x7c0_.GetZ();
          if (inPosition) {
            const CVector3f start = GetTranslation();
            const CVector3f attackDelta = GetAttackTargetPos(mgr) - start;
            if (attackDelta.CanBeNormalized()) {
              const CMaterialFilter filter =
                  CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid, kMT_AIBlock));
              const float length = attackDelta.Magnitude();
              inPosition = CGameCollision::RayStaticLineOfSightTest(
                  mgr, start, (1.f / length) * attackDelta, length, filter);
            }
          }
        }
        return inPosition;
      }
    }
  }
  return false;
}

// @ fn_40_2718
void CMetroid::InterpolateToPosRot(CStateManager& mgr, float dt) {
  CVector3f targetPos = CVector3f::Zero();
  CQuaternion targetRot = CQuaternion::NoRotation();
  ComputeSuckTargetPosRot(mgr, targetPos, targetRot);
  const CVector3f pos = CVector3f::Lerp(GetTranslation(), targetPos, dt);
  const CQuaternion rot = CQuaternion::SlerpLocal(GetRotation(), targetRot, dt);
  SetTranslation(pos);
  SetRotation(rot.BuildNormalized());
}

// @ fn_40_263C
void CMetroid::ComputeSuckTargetPosRot(CStateManager& mgr, CVector3f& pos, CQuaternion& rot) const {
  pos = GetTranslation();
  rot = GetRotation();
  if (const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mAttackTarget))) {
    ComputeSuckPlayerPosRot(*player, mgr, pos, rot);
  } else {
    ComputeSuckPiratePosRot(mgr, pos, rot);
  }
}

// @ fn_40_19F8
float CMetroid::ComputeMorphingPlayerSuckUpPos(const CPlayer& player) const {
  float height = 0.f;
  if (player.HasModelData()) {
    for (uint i = 0; i < 17; ++i) {
      const CTransform4f xf = player.GetLocatorTransform(rstl::string_l(skJointNameList[i]));
      const float jointZ = xf.Get23();
      const float jointHeight = jointZ * player.GetModelData()->GetScale().GetZ();
      if (jointHeight > height) {
        height = jointHeight;
      }
    }
  }
  return height;
}

// @ fn_40_1B4C
void CMetroid::ComputeSuckPiratePosRot(CStateManager& mgr, CVector3f& pos, CQuaternion& rot) const {
  if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(GetAttackTargetId()))) {
    const CTransform4f headXf = actor->GetLocatorTransform(rstl::string_l(skPirateSuckJoint));
    const CTransform4f rootXf = actor->GetLocatorTransform(rstl::string_l(skPirateRootJoint));
    const CVector3f localPos = headXf.GetTranslation() + -0.5f * rootXf.GetUp();
    const CVector3f& scale = actor->GetModelData()->GetScale();
    const CVector3f scaledPos(scale.GetX() * localPos.GetX(), scale.GetY() * localPos.GetY(),
                              scale.GetZ() * localPos.GetZ());
    pos = actor->GetTranslation() + actor->GetTransform().Rotate(scaledPos);
    pos += 0.3f * actor->GetTransform().Rotate(rootXf.GetForward());
    const CQuaternion rotation = CQuaternion::FromMatrix(actor->GetTransform() * rootXf);
    rot = rotation * CQuaternion::ZRotation(CRelAngle::FromRadians(M_PIF));
  }
}

// @ fn_40_4890
void CMetroid::SetTargetDest(CStateManager& mgr, float) {
  BodyController()->SetLocomotionType(pas::kLT_Lurk);
  BodyController()->CommandMgr().SetSteeringBlendMode(kSBM_FullSpeed);
  BodyController()->CommandMgr().SetSteeringSpeedRange(1.f, 1.f);
  xa40_28_ = false;
  if (const CTeamAiRole* role =
          CScriptTeamAiMgr::GetTeamAiRole(mgr, mTeamAiManagerId, GetUniqueId())) {
    x7c0_ = role->GetTeamPosition();
  } else {
    UpdateAttackTarget(mgr);
    if (const CEntity* target = mgr.GetObjectById(mAttackTarget)) {
      x7c0_ = GetOrigin(mgr, CTeamAiRole(GetUniqueId()),
                        static_cast< const CActor* >(target)->GetTranslation());
    } else {
      x7c0_ = GetTranslation();
    }
  }
  const CVector3f targetPos = GetAttackTargetPos(mgr);
  const CVector3f dir = x7c0_ - targetPos;
  if (dir.CanBeNormalized()) {
    const CMaterialFilter filter =
        CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid, kMT_AIBlock));
    const float mag = dir.Magnitude();
    const CVector3f normDir = (1.f / mag) * dir;
    const CRayCastResult result = mgr.RayStaticIntersection(targetPos, normDir, mag, filter);
    if (result.IsValid()) {
      x7c0_ = targetPos + 0.5f * (result.GetTime() * normDir);
      xa40_28_ = true;
    }
  }
  mPathFindNavigation.SetDestination(x7c0_);
  mPathFindNavigation.SetFaceTarget(kInvalidUniqueId);
}

// @ fn_40_4264
void CMetroid::ApplyGrowth(float damage, CStateManager& mgr) {
  mGrowthEnergy += damage;
  const float scale = mMetroidData.mStage2GrowthScale - mScale3.GetY();
  const float energy = CMath::Clamp(0.f, mGrowthEnergy / mMetroidData.mStage2GrowthEnergy, 1.f);
  const float newScale = energy * scale + mScale3.GetY();
  mScale1 = CVector3f(newScale, newScale, newScale);
  TakeDamage(CVector3f::Zero(), 0.f);
}

#ifdef TARGET_PC
// @ fn_40_3FC0
// Absent upstream. kSM_Damage / kSM_ResistedDamage: a projectile that hurts the metroid, or any
// bomb, grows it by the damage it would take; an energized safe zone kills it outright.
void CMetroid::ApplyDamageGrowth(CStateManager& mgr, TUniqueId sender) {
  CEntity* entity = const_cast< CEntity* >(mgr.GetObjectById(sender)); // 0x3FF4
  if (entity == nullptr) {                                             // 0x3FFC beq
    return;
  }
  CGameProjectile* projectile = TCastToPtr< CGameProjectile >(entity); // 0x4000
  // 0x4014 vtable 0x40 = slot 16 GetDamageVulnerability(); 0x4020 addi 0x19c = mCurDamageInfo's
  // weapon mode; 0x4028 clrlwi.; beq falls through to the bomb test.
  if (projectile != nullptr &&
      GetDamageVulnerability()->WeaponHurts(projectile->GetCurrentDamageInfo().GetWeaponMode())) {
    // 0x404C GetVulnerableDamage(*GetDamageVulnerability()), 0x4058 ApplyGrowth.
    ApplyGrowth(projectile->GetCurrentDamageInfo().GetVulnerableDamage(*GetDamageVulnerability()),
                mgr);
  } else if (CBomb* bomb = TCastToPtr< CBomb >(entity)) { // 0x4064; 0x406C beq
    // 0x408C GetVulnerableDamage(*GetDamageVulnerability()), 0x4098 ApplyGrowth.
    ApplyGrowth(bomb->GetCurrentDamageInfo().GetVulnerableDamage(*GetDamageVulnerability()), mgr);
  } else if (CScriptSafeZone* zone = TCastToPtr< CScriptSafeZone >(entity)) { // 0x40A4; 0x40AC
    if (zone->GetZoneType() != CScriptSafeZone::kZT_Normal) { // 0x40B0-0x40B8 cmpwi 0x3f0, 0
      MassiveDeath(mgr);            // 0x40C8 vtable 0xd4 = slot 53 MassiveDeath
    }
  }
}
#endif

// @ fn_40_54C8
void CMetroid::Attack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    if (AttachToTarget(mgr)) {
      mState = kAiState_One;
      mEnergyDrained = 0.f;
      mAttackState = 1;
      mIsAttacking = true;
      RemoveMaterial(kMT_Orbit, kMT_Target, mgr);
      for (int i = 0; i < mgr.GetNumPlayers(); ++i) {
        mgr.Player(i)->SetOrbitRequestForTarget(GetUniqueId(), CPlayer::kOR_ActivateOrbitSource,
                                                mgr);
      }
      DisableSolidCollision(*this);
      AddMaterial(kMT_Trigger, mgr);
    } else {
      mState = kAiState_Over;
    }
    break;
  case kStateMsg_Update:
    switch (mState) {
    case kAiState_One:
      if (BodyController()->GetCurrentStateId() == pas::kAS_LoopAttack) {
        mState = kAiState_Two;
      } else {
        BodyController()->CommandMgr().DeliverCmd(CBCLoopAttackCmd(pas::kLAT_Three));
      }
      break;
    case kAiState_Two: {
      if (GetModelData()->GetAnimationData()->GetIsLoop()) {
        mAttackState = 2;
      }
      const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mAttackTarget));
      if (player != nullptr && player->GetAttachedActorId() == GetUniqueId() &&
          player->GetCurrentAreaId() != GetCurrentAreaId()) {
        DetachFromTarget(mgr, true);
        mPendingDeath = true;
      } else if (BodyController()->GetCurrentStateId() != pas::kAS_LoopAttack) {
        mState = kAiState_Over;
      } else if (ShouldReleaseFromTarget(mgr)) {
        BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_ExitState));
        DetachFromTarget(mgr, true);
        mAttackState = 3;
      }
      break;
    }
    }
    break;
  case kStateMsg_Deactivate: {
    CScriptTeamAiMgr::EndAttack(CScriptTeamAiMgr::kAT_Melee, mgr, mTeamAiManagerId, GetUniqueId(),
                                false);
    mAttackChance = GetAverageAttackTime();
    if (mgr.IsRandomAvailable() == true) {
      const float variation = mAttackTimeVariation;
      mAttackChance += mgr.Random()->Float() * variation;
    }
    mAttackState = 0;
    DetachFromTarget(mgr, true);
    mIsAttacking = false;
    const CQuaternion rotation = CQuaternion::ZRotation(CRelAngle::FromRadians(GetYaw()));
    SetTransform(rotation.BuildTransform4f(GetTranslation()));
    AddMaterial(kMT_Orbit, kMT_Target, mgr);
    RemoveMaterial(kMT_Trigger, mgr);
    break;
  }
  }
}

// @ fn_40_2EC8
bool CMetroid::ShouldReleaseFromTarget(CStateManager& mgr) {
  if (GetBodyController()->IsFrozen()) {
    return true;
  }
  if (const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mAttackTarget))) {
    const float maxDrain = mMetroidData.mMaxEnergyDrainAllowed;
    if (mEnergyDrained >= maxDrain * GetDamageMultiplier() || IsPlayerInFluid(*player, mgr) ||
        player->GetMorphBall()->InScrewAttackMode() ||
        mgr.GetSafeZoneManager()->IsObjectInHurtfulSafeZone(*player, mgr) ||
        !player->GetPlayerState()->IsPlayerAlive()) {
      return true;
    }
  } else if (mAttackTarget != kInvalidUniqueId) {
    if (const CSpacePirate* pirate =
            TCastToConstPtr< CSpacePirate >(mgr.GetObjectById(mAttackTarget))) {
      // TODO: the original also returns true when the pirate's energy is fully drained (bit 0x02
      // of CSpacePirate+0x8F8); CSpacePirate.hpp is claimed by the SpacePirate REL work.
      bool ret = true;
#ifdef TARGET_PC
      // fn_40_2EC8 +0x10C: lbz r0,0x8f8(r3); li r31,1; extrwi. r0,r0,1,30; bne -> return 1.
      if (pirate->AllEnergyDrained()) {
        return ret;
      }
#endif
      if (!pirate->GetBodyController()->GetBodyStateInfo().GetCurrentState()->IsDead()) {
        ret = false;
      }
      return ret;
    }
    return true;
  }
  return false;
}

// @ fn_40_50BC
void CMetroid::TelegraphAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mState = kAiState_Zero;
    mTelegraphAttackTime = mMetroidData.mTelegraphAttackTime;
    mSeekTime = 0.f;
    BodyController()->CommandMgr().ClearLocomotionCmds();
    BodyController()->SetLocomotionType(pas::kLT_Combat);
    break;
  case kStateMsg_Update:
    switch (mState) {
    case kAiState_Zero:
      mTelegraphAttackTime -= dt;
      if (mTelegraphAttackTime < 0.f) {
        mState = kAiState_Two;
        const CVector3f delta = GetAttackTargetPos(mgr) - GetTranslation();
        const float magnitude = delta.Magnitude();
        const float speed = mSpeed;
        float extraTime = 0.f;
        const float distance = 1.25f * magnitude;
        if (speed > 0.f) {
          extraTime = 1.15f / speed;
        }
        mMaxSeekTime = extraTime + distance / BodyController()->GetBodyStateInfo().GetMaxSpeed();
        BodyController()->SetTurnSpeed(speed > 0.f ? 20.f / speed : 20.f);
      } else if (mAttackTarget != kInvalidUniqueId) {
        if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget))) {
          const CVector3f direction = actor->GetTranslation() - GetTranslation();
          if (direction.CanBeNormalized()) {
            BodyController()->CommandMgr().DeliverCmd(
                CBCLocomotionCmd(CVector3f::Zero(), direction.AsNormalized(), 1.f));
          }
        }
      }
      break;
    case kAiState_One:
      break;
    case kAiState_Two: {
      mSeekTime += dt;
      const CVector3f targetPos = GetAttackTargetPos(mgr);
      const CVector3f move = mSteeringBehaviors.Seek(*this, targetPos);
      BodyController()->CommandMgr().DeliverCmd(CBCLocomotionCmd(move, CVector3f::Zero(), 1.f));
      break;
    }
    }
    break;
  case kStateMsg_Deactivate:
    BodyController()->SetTurnSpeed(mTurnSpeed);
    if (Attacked(mgr, CTriggerData(0.f))) {
      CScriptTeamAiMgr::EndAttack(CScriptTeamAiMgr::kAT_Melee, mgr, mTeamAiManagerId, GetUniqueId(),
                                  false);
    } else if (PatternShagged(mgr, CTriggerData(0.f))) {
      CScriptTeamAiMgr::EndAttack(CScriptTeamAiMgr::kAT_Melee, mgr, mTeamAiManagerId, GetUniqueId(),
                                  false);
      mAttackTarget = kInvalidUniqueId;
    }
    break;
  }
}

// @ fn_40_5CF4
void CMetroid::Generate(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    if (mGrowthEnergy >= mMetroidData.mExplosionGrowthEnergy) {
      MassiveDeath(mgr);
    }
    mState = kAiState_One;
    mScale2 = GetModelData()->GetScale();
    mGrowing = true;
    break;
  case kStateMsg_Update:
    switch (mState) {
    case kAiState_One:
      if (BodyController()->GetCurrentStateId() == pas::kAS_Generate) {
        mGrowthDuration = BodyController()->GetAnimTimeRemaining();
        mState = mGrowthDuration > 0.f ? kAiState_Two : kAiState_Over;
      } else if (Attacked(mgr, CTriggerData(0.f))) {
        BodyController()->CommandMgr().DeliverCmd(CBCGenerateCmd(pas::kGType_Two, -1));
      } else {
        BodyController()->CommandMgr().DeliverCmd(CBCGenerateCmd(pas::kGType_Seven, -1));
      }
      break;
    case kAiState_Two:
      if (BodyController()->GetCurrentStateId() != pas::kAS_Generate) {
        mState = kAiState_Over;
      } else if (!BodyController()->IsFrozen()) {
        if (Attacked(mgr, CTriggerData(0.f))) {
          const float timeRemaining = BodyController()->GetAnimTimeRemaining();
          CVector3f scale = mScale2;
          const float t = CMath::Clamp(0.f, 1.f - timeRemaining / mGrowthDuration, 1.f);
          if (t < 0.25f) {
            scale = CMath::Clamp(0.f, 1.f - 0.5f * (t / 0.25f), 1.f) * mScale2;
          } else {
            const float duration = 0.75f * mGrowthDuration;
            const CVector3f halfScale = 0.5f * mScale2;
            scale =
                halfScale + (duration - timeRemaining) * ((1.f / duration) * (mScale1 - halfScale));
          }
          ModelData()->SetScale(scale);
        }
      }
      break;
    }
    break;
  case kStateMsg_Deactivate:
    mGrowthDuration = 0.f;
    mGrowing = false;
    if (Attacked(mgr, CTriggerData(0.f))) {
      mLastGrowthEnergy = mGrowthEnergy;
      ModelData()->SetScale(mScale1);
    }
    break;
  }
}

// @ fn_40_36C4
void CMetroid::SetupExitFaceHugDirection(CActor* actor, CStateManager& mgr,
                                         const CVector3f& direction, const CTransform4f& xf) {
  if (actor == nullptr) {
    return;
  }
  if (mAttackState == 3) {
    return;
  }
  const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid, kMT_AIBlock), CMaterialList(kMT_Character, kMT_Player));
  const float length = mLoopAttackDistance * GetModelData()->GetScale().GetY();
  CVector3f bestDirection = -GetTransform().GetForward();
  float bestDistance = 0.f;
  static const float angles[] = {90.f, 135.f, 45.f, 180.f, 0.f, 225.f, 315.f, 270.f};
  for (uint i = 0; i < 8; ++i) {
    const float angle = (M_PIF / 180.f) * angles[i];
    const CVector3f localDirection(CMath::FastCosR(angle), CMath::FastSinR(angle), 0.f);
    const CVector3f rayDirection = xf.Rotate(localDirection).AsNormalized();
    CVector3f start = actor->GetTranslation();
    start.SetZ(GetTranslation().GetZ());
    TUniqueId hitId = kInvalidUniqueId;
    rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
    mgr.BuildNearList(nearList, start, rayDirection, length, filter, this);
    const CRayCastResult result =
        mgr.RayWorldIntersection(hitId, start, rayDirection, length, filter, nearList);
    if (!result.IsValid()) {
      bestDirection = rayDirection;
      bestDistance = length;
      CTransform4f testXf = GetTransform();
      testXf.AddTranslation(length * rayDirection);
      if (!CGameCollision::DetectStaticCollisionBoolean(mgr, mCollisionPrimitive, testXf, filter)) {
        break;
      }
    } else if (result.GetTime() > bestDistance) {
      bestDirection = rayDirection;
      bestDistance = result.GetTime();
    }
  }
  const CRelAngle maxAngle = CRelAngle::FromDegrees(360.f);
  const CQuaternion rot = CQuaternion::LookAt(
      CUnitVector3f(direction, CUnitVector3f::kN_No),
      CUnitVector3f(bestDirection.GetX(), bestDirection.GetY(), bestDirection.GetZ()), maxAngle);
  SetRotation(GetRotation() * rot);
}

// @ fn_40_2864
void CMetroid::SuckEnergyFromTarget(float dt, CStateManager& mgr) {
  mIsEnergyDrainVulnerable = false;
  if (mAttackTarget == kInvalidUniqueId) {
    return;
  }
  switch (mAttackState) {
  case 0:
    break;
  case 1: {
    InterpolateToPosRot(mgr, 0.4f);
    if (CPlayer* player = TCastToPtr< CPlayer >(mgr.ObjectById(mAttackTarget))) {
      mIsMakingBigStrike = true;
      mDamageDuration = 0.2f;
      mgr.SendScriptMsg(player, GetUniqueId(), kSM_Damage, kInvalidUniqueId);
    }
    x9c0_ = 0.f;
    break;
  }
  case 2: {
    if (CActor* actor = TCastToPtr< CActor >(mgr.ObjectById(mAttackTarget))) {
      if (actor->GetHealthInfo() != nullptr) {
        const float drainPerSecond = mMetroidData.mEnergyDrainPerSecond;
        const float damage = dt * drainPerSecond * GetDamageMultiplier();
        mEnergyDrained += damage;
        if (CPlayer* player = TCastToPtr< CPlayer >(mgr.ObjectById(mAttackTarget))) {
          const CDamageInfo info(CWeaponMode(kWT_PoisonWater1), damage, 0.f, 0.f, true);
          player->SetNoDamageLoopSfx(true);
          mgr.ApplyDamage(
              GetUniqueId(), mAttackTarget, GetUniqueId(), info,
              CMaterialFilter::MakeIncludeExclude(CMaterialList(skSolidMaterial), CMaterialList()),
              CVector3f::Zero());
          player->SetNoDamageLoopSfx(false);
          mIsEnergyDrainVulnerable = (player->GetSpawnedMorphballState() == CPlayer::kMS_Unmorphed
                                          ? player->GetMorphballTransitionState()
                                          : CPlayer::kMS_Unmorphed) == CPlayer::kMS_Morphed;
        } else {
          mIsEnergyDrainVulnerable = true;
          if (actor->GetHealthInfo()->GetHP() > 0.f) {
            const CDamageInfo info(CWeaponMode(kWT_Power), damage, 0.f, 0.f, true);
            mgr.ApplyDamage(GetUniqueId(), mAttackTarget, GetUniqueId(), info,
                            CMaterialFilter::MakeIncludeExclude(CMaterialList(skSolidMaterial),
                                                                CMaterialList()),
                            CVector3f::Zero());
          }
        }
        if (GetGrowthStage() < 2.f) {
          ApplyGrowth(damage, mgr);
        } else {
          TakeDamage(CVector3f::Zero(), 0.f);
        }
      }
    }
    float blend = 0.95f;
    if (CPlayer* player = TCastToPtr< CPlayer >(mgr.ObjectById(mAttackTarget))) {
      const CPlayer::EPlayerMorphBallState morphState = player->GetMorphballTransitionState();
      if (morphState != CPlayer::kMS_Unmorphed && morphState != CPlayer::kMS_Morphed) {
        blend = 0.4f;
      }
      if (morphState == CPlayer::kMS_Unmorphed) {
        const float magnitude =
            CMath::Clamp(0.f, CMath::AbsF(CMath::FastSinR((M_PIF / 2.f) * x9c0_)), 1.f);
        CPlayerState* playerState = mgr.PlayerState(mgr.MaskUIdNumPlayers(player->GetUniqueId()));
        playerState->StaticInterference().AddSource(GetUniqueId(), magnitude, 0.2f);
        if (player->GetStaticTimer() < 0.2f) {
          player->SetHudDisable(0.2f);
        }
      }
      mIsMakingBigStrike = true;
      mDamageDuration = 0.2f;
    }
    InterpolateToPosRot(mgr, blend);
    x9c0_ += dt;
    break;
  }
  case 3: {
    const CQuaternion zRot = CQuaternion::ZRotation(CRelAngle::FromRadians(GetYaw()));
    const CQuaternion rot = CQuaternion::SlerpLocal(GetRotation(), zRot, 0.95f);
    SetRotation(rot.BuildNormalized());
    break;
  }
  }
}

// @ fn_40_1DB0
void CMetroid::ComputeSuckPlayerPosRot(const CPlayer& player, CStateManager& mgr, CVector3f& pos,
                                       CQuaternion& rot) const {
  const CCameraManager* camMgr = mgr.GetCameraManager(mgr.MaskUIdNumPlayers(player.GetUniqueId()));
  pos = player.GetTranslation();
  const float scaleY = GetModelData()->GetScale().GetY();
  switch (player.GetMorphballTransitionState()) {
  case CPlayer::kMS_Unmorphed: {
    const CQuaternion camRotation =
        CQuaternion::FromMatrix(camMgr->GetFirstPersonCamera()->GetTransform());
    rot = camRotation * CQuaternion::ZRotation(CRelAngle::FromRadians(M_PIF));
    const CMatrix3f camMatrix = camRotation.BuildTransform();
    const CVector3f forward = 1.f * (camMatrix * CVector3f::Forward());
    const CVector3f up = (-0.6f * scaleY) * (camMatrix * CVector3f::Up());
    pos += CVector3f(0.f, 0.f, player.GetEyeHeight()) + up + forward;
    break;
  }
  case CPlayer::kMS_Morphing: {
    const float height = ComputeMorphingPlayerSuckUpPos(player);
    pos += CVector3f(0.f, 0.f, 0.4f + height);
    const float radius = player.GetMorphBall()->GetBallRadius();
    pos += 0.5f * player.GetTransform().GetForward() - radius * GetTransform().GetUp();
    rot = CQuaternion::FromMatrix(player.GetTransform()) *
          CQuaternion::YXZRotation(CRelAngle::FromRadians(0.f), CRelAngle::FromDegrees(-89.9f),
                                   CRelAngle::FromRadians(M_PIF));
    break;
  }
  case CPlayer::kMS_Unmorphing: {
    const float height = ComputeMorphingPlayerSuckUpPos(player);
    pos += CVector3f(0.f, 0.f, 0.4f + height);
    const float radius = player.GetMorphBall()->GetBallRadius();
    pos += 0.5f * player.GetTransform().GetForward() - radius * GetTransform().GetUp();
    rot = CQuaternion::FromMatrix(player.GetTransform()) *
          CQuaternion::YXZRotation(CRelAngle::FromRadians(0.f), CRelAngle::FromDegrees(-89.9f),
                                   CRelAngle::FromRadians(M_PIF));
    const float morphT = player.GetMorphBallTransitionFactor();
    if (morphT > 0.75f) {
      const CQuaternion camRotation =
          CQuaternion::FromMatrix(camMgr->GetFirstPersonCamera()->GetTransform());
      const CQuaternion targetRotation =
          camRotation * CQuaternion::ZRotation(CRelAngle::FromRadians(M_PIF));
      const CMatrix3f camMatrix = camRotation.BuildTransform();
      const CVector3f forward = 1.f * (camMatrix * CVector3f::Forward());
      const CVector3f up = (-0.6f * scaleY) * (camMatrix * CVector3f::Up());
      const CVector3f targetPos =
          player.GetTranslation() + CVector3f(0.f, 0.f, player.GetEyeHeight()) + up + forward;
      const float t = (morphT - 0.75f) / 0.25f;
      rot = CQuaternion::SlerpLocal(rot, targetRotation, t);
      pos = CVector3f::Lerp(pos, targetPos, t);
    }
    break;
  }
  case CPlayer::kMS_Morphed: {
    pos += (2.f * player.GetMorphBall()->GetBallRadius() + 0.25f) * CVector3f::Up();
    const float radius = player.GetMorphBall()->GetBallRadius();
    pos -= radius * (scaleY * GetTransform().GetUp());
    rot = CQuaternion::YXZRotation(CRelAngle::FromRadians(0.f), CRelAngle::FromDegrees(-89.9f),
                                   CRelAngle::FromRadians(GetYaw()));
    break;
  }
  }
}

// @ fn_40_7578
const CCollisionPrimitive* CMetroid::GetCollisionPrimitive() const { return &mCollisionPrimitive; }

// @ fn_40_71A0
bool CMetroid::StateOver(CStateManager&, const CTriggerData&) const {
  return mState == kAiState_Over;
}

// @ fn_40_6DB0
bool CMetroid::ShotAt(CStateManager&, const CTriggerData&) const { return mShotAt; }

// @ fn_40_65A4
bool CMetroid::SpotPlayer(CStateManager&, const CTriggerData&) const { return false; }

// @ fn_40_6250
bool CMetroid::ShouldWallHang(CStateManager&, const CTriggerData&) const {
  return mMetroidData.mStartsInWall;
}

// @ fn_40_6248
bool CMetroid::ShouldDodge(CStateManager&, const CTriggerData&) const { return false; }

// @ fn_40_FB4
void CMetroid::OnDockTouch(CStateManager& mgr) {
  DetachFromTarget(mgr, true);
  mPendingDeath = true;
}

static CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    {"StateOver", static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::StateOver)},
    {"AttackOver", static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::AttackOver)},
    {"LostInterest", static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::LostInterest)},
    {"PatternShagged",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::PatternShagged)},
    {"Attacked", static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::Attacked)},
    {"ShotAt", static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::ShotAt)},
    {"ShouldAttack", static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::ShouldAttack)},
    {"InAttackPosition",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::InAttackPosition)},
    {"InPosition", static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::InPosition)},
    {"InRange", static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::InRange)},
    {"InDetectionRange",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::InDetectionRange)},
    {"SpotPlayer", static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::SpotPlayer)},
    {"AggressionCheck",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::AggressionCheck)},
    {"ShouldTurn", static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::ShouldTurn)},
    {"Leash", static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::Leash)},
    {"ShouldWallHang",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::ShouldWallHang)},
    {"ShouldDodge", static_cast< CPatterned::StateMachine::TriggerFunc >(&CMetroid::ShouldDodge)},
};

static CPatterned::StateMachine::SStateFunction skStates[] = {
    {"Patrol", static_cast< CPatterned::StateMachine::StateFunc >(&CMetroid::Patrol)},
    {"Generate", static_cast< CPatterned::StateMachine::StateFunc >(&CMetroid::Generate)},
    {"SelectTarget", static_cast< CPatterned::StateMachine::StateFunc >(&CMetroid::SelectTarget)},
    {"PathFind", static_cast< CPatterned::StateMachine::StateFunc >(&CMetroid::PathFind)},
    {"TurnAround", static_cast< CPatterned::StateMachine::StateFunc >(&CMetroid::TurnAround)},
    {"TelegraphAttack",
     static_cast< CPatterned::StateMachine::StateFunc >(&CMetroid::TelegraphAttack)},
    {"Attack", static_cast< CPatterned::StateMachine::StateFunc >(&CMetroid::Attack)},
    {"WallHang", static_cast< CPatterned::StateMachine::StateFunc >(&CMetroid::WallHang)},
    {"Dodge", static_cast< CPatterned::StateMachine::StateFunc >(&CMetroid::Dodge)},
};

static CPatterned::StateMachine::SCodeFunction skCodeFuncs[] = {
    {"SetTargetDest", static_cast< CPatterned::StateMachine::CodeFunc >(&CMetroid::SetTargetDest)},
    {"SetPatrolDest", static_cast< CPatterned::StateMachine::CodeFunc >(&CMetroid::SetPatrolDest)},
};

// @ fn_40_FF4
void CMetroid::SetupStateMachine(CStateManager& mgr) {
  StateMachine* stateMachine = mStateMachine.get();
  CPatterned::SetupStateMachine(mgr);
  stateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  stateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  stateMachine->SetCodeFunctions(skCodeFuncs, ARRAY_SIZE(skCodeFuncs));
}

// @ fn_40_17C
CEntity* LoadMetroidAlpha(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrMetroidAlpha sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrMetroidAlpha.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }

  const CMetroidData metroidData(
      LdrToDamageVulnerability(sldrThis.frozenVulnerability),
      LdrToDamageVulnerability(sldrThis.energyDrainVulnerability),
      LdrToDamageVulnerability(sldrThis.babyMetroidGrowthVulnerability), sldrThis.energyDrainPerSec,
      sldrThis.maxEnergyDrainAllowed, sldrThis.telegraphAttackTime, sldrThis.babyMetroidScale,
      sldrThis.unknown_0x03362858, sldrThis.unknown_0x1c783744, sldrThis.unknown_0x852d3bb0,
      sldrThis.babyMetroidTransformationParticleEffect, sldrThis.stage2GrowthScale,
      sldrThis.stage2GrowthEnergy, sldrThis.explosionGrowthEnergy, sldrThis.dodgeCheckTimeInterval,
      sldrThis.chanceToDodge, sldrThis.metroidFlagsMetroid);

  if (metroidData.xc4_24_) {
    return rs_new CBabyMetroid(mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
                               LdrToEntityInfo(info, sldrThis.editorProperties),
                               LdrToTransform4f(sldrThis.editorProperties), *modelData,
                               LdrToPatternedInfo(sldrThis.patterned, &sldrThis.ingPossessionData),
                               LdrToActorParameters(sldrThis.actorInformation), metroidData);
  }
  return rs_new CMetroid(mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
                         LdrToEntityInfo(info, sldrThis.editorProperties),
                         LdrToTransform4f(sldrThis.editorProperties), *modelData,
                         LdrToPatternedInfo(sldrThis.patterned, &sldrThis.ingPossessionData),
                         LdrToActorParameters(sldrThis.actorInformation), metroidData);
}

#ifndef MONOLITHIC
SMetroid_FuncPtrs REL_loader_Metroid;

// @ fn_40_12C
void SetRelLoaderFunctionToLoader() {
  REL_loader_Metroid.mLoadMetroid = LoadMetroidAlpha;
  REL_loader_Metroid.mOnDockTouch = &CMetroid::OnDockTouch;
  SetSMetroid_FuncPtrs(&REL_loader_Metroid);
}

// @ fn_40_10C (objdiff 100%)
extern "C" void RELMain() { SetRelLoaderFunctionToLoader(); }

// @ fn_40_E8 (objdiff 100%)
extern "C" void RELExit() { SetSMetroid_FuncPtrs(nullptr); }
#endif
