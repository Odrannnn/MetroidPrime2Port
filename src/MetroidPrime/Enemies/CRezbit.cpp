#include "MetroidPrime/Enemies/CRezbit.hpp"

#include "Collision/CMaterialFilter.hpp"
#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/Math/CQuaternion.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Math/CUnitVector3f.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/BodyState/CBodyStateCmdMgr.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/CObjectList.hpp"
#include "MetroidPrime/CRezbitEffect.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrRezbit.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTeamAiMgr.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CBeamInfo.hpp"
#include "MetroidPrime/Weapons/CGameProjectile.hpp"
#include "MetroidPrime/Weapons/CPlasmaProjectile.hpp"
#include "MetroidPrime/Weapons/CWeaponAssetInfo.hpp"
#include "REL/REL_Setup.h"
#include "rstl/algorithm.hpp"

static const char* const skDeRezzedEffect = "DeRezzed";

static const pas::EStepDirection skStrafeDirections[] = {pas::kSD_Left, pas::kSD_Right, pas::kSD_Up,
                                                         pas::kSD_Down};

// @ fn_53_88E0
static CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    {"StateOver", static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::StateOver)},
    {"IsDeRezzed", static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::IsDeRezzed)},
    {"IsAlert", static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::IsAlert)},
    {"HasAttackPath",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::HasAttackPath)},
    {"AttackPathOver",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::AttackPathOver)},
    {"ShouldBecomeRezzed",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::ShouldBecomeRezzed)},
    {"ShouldBecomeDeRezzed",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::ShouldBecomeDeRezzed)},
    {"HasLineOfSight",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::HasLineOfSight)},
    {"ShouldStrafe", static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::ShouldStrafe)},
    {"FoundStrafeDirection",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::FoundStrafeDirection)},
    {"ShouldLaserAttack",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::ShouldLaserAttack)},
    {"ShouldEnergyBoltAttack",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::ShouldEnergyBoltAttack)},
    {"ShouldVirusAttack",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CRezbit::ShouldVirusAttack)},
};

static CPatterned::StateMachine::SStateFunction skStates[] = {
    {"Start", static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::Start)},
    {"Idle", static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::Idle)},
    {"Alert", static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::Alert)},
    {"Patrol", static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::Patrol)},
    {"FollowAttackPath",
     static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::FollowAttackPath)},
    {"PathFind", static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::PathFind)},
    {"Strafe", static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::Strafe)},
    {"BecomeRezzed", static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::BecomeRezzed)},
    {"BecomeDeRezzed",
     static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::BecomeDeRezzed)},
    {"CuttingLaserAttack",
     static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::CuttingLaserAttack)},
    {"EnergyBoltAttack",
     static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::EnergyBoltAttack)},
    {"VirusAttack", static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::VirusAttack)},
    {"Dead", static_cast< CPatterned::StateMachine::StateFunc >(&CRezbit::Dead)},
};

static CPatterned::StateMachine::SCodeFunction skCodeFuncs[] = {
    {"SelectTarget", static_cast< CPatterned::StateMachine::CodeFunc >(&CRezbit::SelectTarget)},
    {"SelectStrafeDirection",
     static_cast< CPatterned::StateMachine::CodeFunc >(&CRezbit::SelectStrafeDirection)},
    {"SetTargetDest", static_cast< CPatterned::StateMachine::CodeFunc >(&CRezbit::SetTargetDest)},
    {"ResetAttackTimes",
     static_cast< CPatterned::StateMachine::CodeFunc >(&CRezbit::ResetAttackTimes)},
    {"RaiseShields", static_cast< CPatterned::StateMachine::CodeFunc >(&CRezbit::RaiseShields)},
    {"SetNormalRezState",
     static_cast< CPatterned::StateMachine::CodeFunc >(&CRezbit::SetNormalRezState)},
    {"SetDeRezzedState",
     static_cast< CPatterned::StateMachine::CodeFunc >(&CRezbit::SetDeRezzedState)},
};

static CVector3f skShieldHalfExtents(3.5f, 0.5f, 5.f);

// @ fn_53_785C
CRezbit::CRezbit(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                 const CTransform4f& xf, const CModelData& modelData,
                 const CActorParameters& actorParams, const CPatternedInfo& patternedInfo,
                 const SLdrRezbitData& data)
: CPatterned(kPAI_Rezbit, uid, name, kFT_Zero, info, xf, modelData, patternedInfo, kMT_Flyer,
             kCT_One, kBT_Flyer, actorParams)
, mData(data)
, mPathFindSearch(nullptr, 3, patternedInfo.GetPathfindingIndex(), 1.f, 1.f, 0,
                  CPFRegion::kRP_Center)
, mRezState(0)
, mLineOfSight(GetUniqueId(), CSegId::Invalid(), 0.1f, 0.05f)
, mShieldActorId(kInvalidUniqueId)
, mShieldBottomLeftSeg(CSegId::Invalid())
, mShieldTopRightSeg(CSegId::Invalid())
, mShieldExplodeEffect(data.shieldExplodeEffect == kInvalidAssetId
                           ? rstl::optional_object< TLockedToken< CGenDescription > >()
                           : rstl::optional_object< TLockedToken< CGenDescription > >(
                                 TLockedToken< CGenDescription >(gpSimplePool->GetObj(
                                     SObjectTag('PART', data.shieldExplodeEffect)))))
, mShieldSfx()
, mDeflectSfx()
, mDeflectSfxTime(0.f)
, mDerezTimer(0.f)
, mShieldTimer(0.f)
, mDerezHealth(GetHealthInfo()->GetHP() - data.unknown_0x4a6c4b40)
, mAlertDelay(0.f)
, x0c40_(4)
, mStrafeTimer(data.unknown_0x70e597d4)
, mStrafeDistance(10.f)
, mStrafeIndex(-1)
, mTeamAiMgrId(kInvalidUniqueId)
, mTargetId(kInvalidUniqueId)
, mAttackKind(-1)
, mWobbleTransform(CTransform4f::Identity())
, mAttackTimer(data.normalRezAttackTime)
, mAttackSelection(-1)
, mCuttingLaserInfo(data.cuttingLaserBeamInfo.weaponSystem,
                    LdrToDamageInfo(data.cuttingLaserDamage))
, mLaserId(kInvalidUniqueId)
, mLaserSfx()
, mLaserStart(CVector3f::Zero())
, mLaserEnd(CVector3f::Zero())
, mLaserDuration(0.f)
, mLaserElapsed(0.f)
, mEnergyBoltInfo(data.energyBoltProjectile, LdrToDamageInfo(data.energyBoltDamage))
, mBoltDuration(0.f)
, mBoltTimer(0.f)
, mBoltCount(-1)
, mLastBoltDirection(CVector3f::Zero())
, mVirusTimer(0.f)
, mVirusDamage(LdrToDamageInfo(data.virusDamage))
, mAlert(false)
, mHasPathDestination(false)
, mShieldBroken(false) {
  mEnergyBoltInfo.Token().Lock();
  BuildDerezModel(mData.derezModel, mData.derezSkinRules);
  const CAnimData* animData = GetModelData()->GetAnimationData();
  mShieldBottomLeftSeg = animData->GetLocatorSegId(rstl::string_l("BottomLeft_SDK"));
  mShieldTopRightSeg = animData->GetLocatorSegId(rstl::string_l("TopRight_SDK"));
  mLineOfSight.SetSegment(CSegId(1));
  mStrafeDirections[0] = -GetTransform().GetRight();
  mStrafeDirections[1] = GetTransform().GetRight();
  mStrafeDirections[2] = CVector3f::Up();
  mStrafeDirections[3] = CVector3f::Down();
  mStrafeDistance =
      GetAnimationDistance(CPASAnimParmData(pas::kAS_Step, CPASAnimParm::FromEnum(pas::kSD_Right),
                                            CPASAnimParm::FromEnum(pas::kStep_BreakDodge))) *
      GetModelData()->GetScale().GetX();
  mPathFindSearch.SetCharacterRadius(3.f);
  mPathFindSearch.SetCharacterHeight(7.f);
}

// @ fn_53_74D4
// @ fn_53_75E4
CRezbit::~CRezbit() {}

// @ fn_53_6B78
void CRezbit::SetupStateMachine(CStateManager& mgr) {
  StateMachine* stateMachine = mStateMachine.get();
  CPatterned::SetupStateMachine(mgr);
  stateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  stateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  stateMachine->SetCodeFunctions(skCodeFuncs, ARRAY_SIZE(skCodeFuncs));
}

// @ fn_53_6C04
bool CRezbit::Listen(CStateManager& mgr, const CVector3f& position, EListenNoiseType type) {
  bool heard = false;
  if (mAlive) {
    if (type == kLNT_PlayerFire) {
      if (!mAlert) {
        const CVector3f offset = position - GetTranslation();
        const float distanceSq = offset.MagSquared();
        if (distanceSq < mData.hearingRadius * mData.hearingRadius) {
          mAlert = true;
          mAlertDelay = 0.04f * CMath::SqrtF(distanceSq);
          heard = true;
        }
      }
    }
  }
  return heard;
}

// @ fn_53_6CBC
void CRezbit::Death(CStateManager& mgr, const CVector3f& direction, EScriptObjectState state) {
  if (mStateMachine->HasState()) {
    mStateMachine->SetState(mgr, *this, rstl::string_l("Dead"));
  }
  if (state != kSS_InvalidState) {
    SendScriptMsgs(state, mgr, kInvalidUniqueId, kSM_None);
  }
}

// @ fn_53_6D84
CVector3f CRezbit::GetAimPosition(const CStateManager& mgr, float dt) const {
  return CPatterned::GetAimPosition(mgr, dt);
}

// @ fn_53_6DB0
const CDamageVulnerability* CRezbit::GetDamageVulnerability() const {
  return CPatterned::GetDamageVulnerability();
}

// @ fn_53_6DD0
CProjectileInfo* CRezbit::ProjectileInfo() { return &mEnergyBoltInfo; }

// @ fn_53_6DD8
void CRezbit::AddToRenderer(const CStateManager& mgr) const { CPatterned::AddToRenderer(mgr); }

// @ fn_53_6DF8
void CRezbit::PreRender(CStateManager& mgr) { CPatterned::PreRender(mgr); }

// @ fn_53_6E18
void CRezbit::Render(const CStateManager& mgr) const {
  const CTransform4f savedTransform = GetTransform();
  CTransform4f& transform = const_cast< CTransform4f& >(GetTransform());
  transform = transform * mWobbleTransform;
  CPatterned::Render(mgr);
  transform = savedTransform;

  if (mDeflectedMissiles.size() != 0 && mData.missileDeflectRadius > 0.f) {
    const CVector3f center = GetAimPosition(mgr, 0.f);
    for (rstl::reserved_vector< TUniqueId, 5 >::const_iterator it = mDeflectedMissiles.begin();
         it != mDeflectedMissiles.end(); ++it) {
      const CGameProjectile* projectile =
          TCastToConstPtr< CGameProjectile >(mgr.GetObjectById(*it));
      if (projectile) {
        const float distance = (center - projectile->GetTranslation()).Magnitude();
        const float ratio = distance / mData.missileDeflectRadius;
#ifdef TARGET_PC
        mgr.DrawSpaceWarp(projectile->PortPresentedPoint(projectile->GetTranslation()),
                          1.f - CMath::Clamp(0.f, ratio, 1.f));
#else
        mgr.DrawSpaceWarp(projectile->GetTranslation(), 1.f - CMath::Clamp(0.f, ratio, 1.f));
#endif
      }
    }
  }
}

// @ fn_53_6FC4
void CRezbit::DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                              float dt) {
  bool handled = false;
  switch (type) {
  case kUE_EffectOn: {
    mLaserDuration = BodyController()->GetAnimTimeRemaining();
    const int animId = BodyController()->GetCurrentAnimId();
    const CCharAnimTime start =
        GetModelData()->GetAnimationData()->GetTimeOfUserEventForAnimation(animId, kUE_EffectOn);
    if (start != CCharAnimTime::Infinity()) {
      const CCharAnimTime stop =
          GetModelData()->GetAnimationData()->GetTimeOfUserEventForAnimation(animId, kUE_EffectOff);
      if (stop != CCharAnimTime::Infinity()) {
        mLaserDuration = rstl::max_val(0.f, stop.GetSeconds() - start.GetSeconds());
      }
    }
    if (SetupLaserTargets(mgr)) {
      SpawnLaser(mgr, dt);
    }
    handled = true;
    break;
  }
  case kUE_EffectOff:
    DeleteLaser(mgr);
    handled = true;
    break;
  case kUE_BeginAction:
    StartVirusAttack(mgr, node.GetLocatorName());
    handled = true;
    break;
  }
  if (!handled) {
    CPatterned::DoUserAnimEvent(mgr, node, type, dt);
  }
}

// @ fn_53_718C
void CRezbit::Think(float dt, CStateManager& mgr) {
  if (GetActive()) {
    CPatterned::Think(dt, mgr);
    mLineOfSight.Update(dt, mgr);
    UpdateShield(mgr, dt);
    UpdateWobble(mgr);
    UpdateTimers(dt);
    if (mRezState == 1 && !BodyController()->IsFrozen()) {
      DeflectMissiles(mgr);
    }
  }
}

// @ fn_53_7240
void CRezbit::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  const TUniqueId sender = msg.GetSenderId();
  const EScriptObjectMessage message = msg.GetMessage();
  const bool wasActive = GetActive();
  CPatterned::AcceptScriptMsg(mgr, msg);
  switch (message) {
  case kSM_Create:
    BodyController()->Activate(mgr, pas::kAS_Invalid);
    CreateShieldCollisionActor(mgr);
    break;
  case kSM_Delete:
    mgr.DeleteObjectRequest(mShieldActorId);
    QuitTeam(mgr);
    if (mShieldSfx) {
      CSfxManager::RemoveEmitter(mShieldSfx);
    }
    if (mLaserSfx) {
      CSfxManager::RemoveEmitter(mLaserSfx);
    }
    break;
  case kSM_Activate:
    if (!wasActive) {
      if (CEntity* shield = mgr.ObjectById(mShieldActorId)) {
        if (BodyController()->BodyStateInfo().GetCurrentAdditiveStateId() ==
            pas::kAS_AdditiveLoopReaction) {
          shield->SetActive(true);
        }
      }
    }
    break;
  case kSM_Deactivate:
    if (wasActive) {
      if (CEntity* shield = mgr.ObjectById(mShieldActorId)) {
        shield->SetActive(false);
      }
      QuitTeam(mgr);
    }
    break;
  case kSM_Alert:
#ifdef TARGET_PC
    // 0x7434 lbz/rlwimi r0,r3,7,24,24/stb 0x420: retail sets CPatterned's hit-by-player bit;
    // UpdateTimers turns it into mAlert on the next Think.
    SetHitByPlayerProjectile(true);
#else
    mAlert = true;
#endif
    break;
  case kSM_Damage:
#ifdef TARGET_PC
    SetHitByPlayerProjectile(true); // 0x7448 stb 0x420
#else
    mAlert = true;
#endif
    HandleShieldHit(mgr, sender);
    break;
  case kSM_ResistedDamage:
#ifdef TARGET_PC
    SetHitByPlayerProjectile(true); // 0x7474 stb 0x420
#else
    mAlert = true;
#endif
    break;
  case kSM_AreaLoaded:
    mPathFindSearch.SetArea(
        mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()).GetPostConstructed()->mPathArea);
    break;
  }
}

// @ fn_53_6544
void CRezbit::Start(CStateManager& mgr, EStateMsg msg, float dt) {}

// @ fn_53_6540
void CRezbit::Idle(CStateManager& mgr, EStateMsg msg, float dt) {}

// @ fn_53_62E8
void CRezbit::Alert(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mAlert = true;
    LowerShields(mgr);
    mShieldTimer = FLT_MAX;
    mgr.InformListeners(GetTranslation(), kLNT_PlayerFire);
    break;
  case kStateMsg_Update:
    if (StateMachineState().GetTime() > mAlertDelay) {
      if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Taunt)) {
        BodyController()->CommandMgr().DeliverCmd(CBCTauntCmd(pas::kTT_Four));
      } else if (BodyController()->GetCurrentStateId() == pas::kAS_Taunt) {
        BodyController()->SetLocomotionType(pas::kLT_Lurk);
      }
    }
    if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mTargetId))) {
      const CVector3f toTarget = target->GetTranslation() - GetTranslation();
      if (toTarget.IsMagnitudeSafe()) {
        BodyController()->FaceDirection(toTarget.AsNormalized(), dt);
      }
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    BodyController()->SetLocomotionType(pas::kLT_Lurk);
    break;
  }
}

// @ fn_53_6278
void CRezbit::Patrol(CStateManager& mgr, EStateMsg msg, float dt) {
  mWaypointNavigation.Patrol(mgr, msg, dt, *this);
  switch (msg) {
  case kStateMsg_Activate:
    BodyController()->SetLocomotionType(pas::kLT_Relaxed);
    break;
  case kStateMsg_Update:
    UpdatePatrolMovement();
    break;
  }
}

// @ fn_53_61C4
void CRezbit::FollowAttackPath(CStateManager& mgr, EStateMsg msg, float dt) {
  mWaypointNavigation.Patrol(mgr, msg, dt, *this);
  switch (msg) {
  case kStateMsg_Activate:
#ifdef TARGET_PC
    // 0x6240 sth r3,0x6c0(r29): retail sets the destination, not the last destination (0x6c2).
    mWaypointNavigation.SetDestination(GetConnectedObject(mgr, kSS_Attack, kSM_Follow));
#else
    mWaypointNavigation.SetLastDestination(GetConnectedObject(mgr, kSS_Attack, kSM_Follow));
#endif
    mAttackKind = 4;
    break;
  case kStateMsg_Deactivate:
    mAttackKind = -1;
    break;
  }
}

// @ fn_53_60B8
void CRezbit::PathFind(CStateManager& mgr, EStateMsg msg, float dt) {
  if (mHasPathDestination) {
    mPathFindNavigation.PathFind(mgr, msg, dt, *this);
    UpdateSeparation(mgr);
  }
  if (msg == kStateMsg_Update && !mHasPathDestination) {
    if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mTargetId))) {
      const CVector3f toTarget = target->GetTranslation() - GetTranslation();
      if (toTarget.IsMagnitudeSafe()) {
        BodyController()->FaceDirection(toTarget.AsNormalized(), dt);
      }
    }
  }
}

// @ fn_53_5E4C
void CRezbit::Strafe(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    if (mStrafeIndex != -1) {
      mAttackKind = 0;
      mAnimationState.SetState(CAnimationState::kAS_Ready);
    } else {
      mAnimationState.SetState(CAnimationState::kAS_Over);
    }
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Step)) {
      const pas::EStepDirection direction = skStrafeDirections[mStrafeIndex];
      BodyController()->CommandMgr().DeliverCmd(
          CBCStepCmd(direction, mRezState == 0 ? pas::kStep_Normal : pas::kStep_BreakDodge));
    } else if (BodyController()->GetCurrentStateId() == pas::kAS_Step) {
      if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mTargetId))) {
        BodyController()->CommandMgr().SetTargetVector(target->GetTranslation() - GetTranslation());
      }
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    mAttackKind = -1;
    mStrafeIndex = -1;
    mStrafeTimer = mRezState == 0 ? mData.unknown_0x70e597d4 : mData.strafeDerezInterval;
    break;
  }
}

// @ fn_53_5C90
void CRezbit::BecomeRezzed(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(mRezState == 1 ? CAnimationState::kAS_Ready
                                            : CAnimationState::kAS_Over);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Generate)) {
      CBCGenerateCmd cmd(pas::kGType_Zero, -1);
      cmd.SetInterruptKnockBack(true);
      BodyController()->CommandMgr().DeliverCmd(cmd);
    } else if (BodyController()->GetCurrentStateId() == pas::kAS_Generate) {
      BodyController()->SetLocomotionType(pas::kLT_Lurk);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    BodyController()->SetLocomotionType(pas::kLT_Lurk);
    SetNormalRezState(mgr, dt);
    break;
  }
}

// @ fn_53_5A78
void CRezbit::BecomeDeRezzed(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(mRezState == 0 ? CAnimationState::kAS_Ready
                                            : CAnimationState::kAS_Over);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Generate)) {
      CBCGenerateCmd cmd(pas::kGType_Zero, -1);
      cmd.SetInterruptKnockBack(true);
      BodyController()->CommandMgr().DeliverCmd(cmd);
    } else if (BodyController()->GetCurrentStateId() == pas::kAS_Generate) {
      BodyController()->SetLocomotionType(pas::kLT_Combat);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    BodyController()->SetLocomotionType(pas::kLT_Combat);
    SetDeRezzedState(mgr, dt);
    break;
  }
}

// @ fn_53_5824
void CRezbit::CuttingLaserAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mLaserElapsed = 0.f;
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mAttackKind = 1;
    CScriptTeamAiMgr::StartAttack(CScriptTeamAiMgr::kAT_Projectile, mgr, mTeamAiMgrId,
                                  GetUniqueId());
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_ProjectileAttack)) {
      BodyController()->CommandMgr().DeliverCmd(CBCProjectileAttackCmd(
          pas::kS_One, GetTranslation() + GetTransform().GetForward(), false));
    } else if (mLaserId != kInvalidUniqueId && mLaserDuration > 0.f) {
      UpdateLaser(mgr, dt);
    } else if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mTargetId))) {
      BodyController()->CommandMgr().SetTargetVector(target->GetTranslation() - GetTranslation());
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    DeleteLaser(mgr);
    mAttackKind = -1;
    CScriptTeamAiMgr::EndAttack(CScriptTeamAiMgr::kAT_Projectile, mgr, mTeamAiMgrId, GetUniqueId(),
                                false);
    break;
  }
}

// @ fn_53_54CC
void CRezbit::EnergyBoltAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mAttackKind = 2;
    mBoltCount = 0;
    mBoltTimer = mData.energyBoltBurstTime;
    mLastBoltDirection = GetTransform().GetForward();
    mBoltDuration =
        mData.energyBoltAttackDuration + mgr.Random()->Range(0.f, mData.energyBoltAttackVariance);
    CScriptTeamAiMgr::StartAttack(CScriptTeamAiMgr::kAT_Projectile, mgr, mTeamAiMgrId,
                                  GetUniqueId());
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_LoopAttack)) {
      BodyController()->CommandMgr().DeliverCmd(CBCLoopAttackCmd(pas::kLAT_Zero, false));
    } else if (BodyController()->GetCurrentStateId() == pas::kAS_LoopAttack) {
      if (StateMachineState().GetTime() < mBoltDuration) {
        if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mTargetId))) {
          BodyController()->CommandMgr().SetTargetVector(target->GetTranslation() -
                                                         GetTranslation());
        }
        mBoltTimer -= dt;
        if (mBoltTimer <= 0.f) {
          FireEnergyBolt(mgr, dt);
          mBoltTimer = mData.energyBoltBurstTime;
        }
      } else {
        BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_ExitState));
      }
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_ExitState));
    mAttackKind = -1;
    CScriptTeamAiMgr::EndAttack(CScriptTeamAiMgr::kAT_Projectile, mgr, mTeamAiMgrId, GetUniqueId(),
                                false);
    break;
  }
}

// @ fn_53_5270
void CRezbit::VirusAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mAttackKind = 3;
    CScriptTeamAiMgr::StartAttack(CScriptTeamAiMgr::kAT_Melee, mgr, mTeamAiMgrId, GetUniqueId());
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_ProjectileAttack)) {
      BodyController()->CommandMgr().DeliverCmd(CBCProjectileAttackCmd(
          pas::kS_Two, GetTranslation() + GetTransform().GetForward(), false));
    } else if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mTargetId))) {
      BodyController()->CommandMgr().SetTargetVector(target->GetTranslation() - GetTranslation());
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    mAttackKind = -1;
    mDerezTimer = 0.f;
    CScriptTeamAiMgr::EndAttack(CScriptTeamAiMgr::kAT_Melee, mgr, mTeamAiMgrId, GetUniqueId(),
                                false);
    break;
  }
}

// @ fn_53_51EC
void CRezbit::Dead(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    if (mShieldSfx) {
      CSfxManager::RemoveEmitter(mShieldSfx);
    }
    if (mLaserSfx) {
      CSfxManager::RemoveEmitter(mLaserSfx);
    }
    DeathDelete(mgr);
    break;
  }
}

// @ fn_53_51AC
void CRezbit::SelectTarget(CStateManager& mgr, float dt) {
  mTargetId = mgr.GetPlayer(0)->GetUniqueId();
  mLineOfSight.SetTarget(mTargetId);
}

// @ fn_53_50F0
void CRezbit::SelectStrafeDirection(CStateManager& mgr, float dt) {
  mStrafeIndex = -1;
  const float chance = mRezState == 0 ? mData.unknown_0x94980a67 : mData.strafeDerezChance;
  if (mgr.Random()->Range(0.f, 100.f) <= chance) {
    mStrafeIndex = FindStrafeDirection(mgr);
  } else {
    mStrafeTimer = mRezState == 0 ? mData.unknown_0x70e597d4 : mData.strafeDerezInterval;
  }
}

// @ fn_53_4F68
void CRezbit::SetTargetDest(CStateManager& mgr, float dt) {
  const CVector3f position = GetTranslation();
  CVector3f destination = position;
  mHasPathDestination = false;
  if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mTargetId))) {
    const CVector3f targetPos = target->GetAimPosition(mgr, 0.f);
    const CVector3f offset = targetPos - position;
    float maxRangeSq;
    if (mRezState == 0) {
#ifdef TARGET_PC
      // Retail keeps the laser range unless the energy-bolt one is strictly smaller (NaN: laser).
      maxRangeSq = rstl::min_val(mData.cuttingLaserMaxAttackDist * mData.cuttingLaserMaxAttackDist,
                                 mData.energyBoltMaxAttackDist * mData.energyBoltMaxAttackDist);
#else
      maxRangeSq = rstl::min_val(mData.energyBoltMaxAttackDist * mData.energyBoltMaxAttackDist,
                                 mData.cuttingLaserMaxAttackDist * mData.cuttingLaserMaxAttackDist);
#endif
    } else {
      maxRangeSq = mData.virusMaxAttackDist * mData.virusMaxAttackDist;
    }
    if (offset.MagSquared() > maxRangeSq || !mLineOfSight.HasLineOfSight()) {
      destination = targetPos;
      mHasPathDestination = true;
    }
  }
  mPathFindNavigation.SetDestination(destination);
  mPathFindNavigation.SetFaceTarget(mTargetId);
  mPathFindNavigation.SetUseLocomotionFacing(true);
  JoinTeam(mgr);
}

// @ fn_53_4ED0
void CRezbit::ResetAttackTimes(CStateManager& mgr, float dt) {
  mAttackTimer = mData.normalRezAttackTime;
  mVirusTimer = mData.virusAttackTime;
  const float roll = mgr.Random()->Range(0.f, 100.f);
  if (roll <= mData.cuttingLaserChance) {
    mAttackSelection = 1;
  } else if (roll <= mData.cuttingLaserChance + mData.energyBoltChance) {
    mAttackSelection = 2;
  } else {
    mAttackSelection = -1;
  }
}

// @ fn_53_4D28
void CRezbit::RaiseShields(CStateManager& mgr, float dt) {
  if (mShieldBroken) {
    return;
  }
  if (BodyController()->BodyStateInfo().GetCurrentAdditiveStateId() !=
      pas::kAS_AdditiveLoopReaction) {
    BodyController()->CommandMgr().DeliverCmd(CBCAdditiveLoopReactionCmd(0, 1.f));
    mShieldTimer = mData.shieldUpTime;
  }
  if (CEntity* shield = mgr.ObjectById(mShieldActorId)) {
    shield->SetActive(true);
    if (!mShieldSfx) {
      mShieldSfx = PlayCustomSound(GetTranslation(), GetTransform().GetForward(),
                                   mData.sound_ShieldOn, true);
    }
  }
}

// @ fn_53_4C60
void CRezbit::SetNormalRezState(CStateManager& mgr, float dt) {
  if (mRezState != 0) {
    mRezState = 0;
    CAnimData* animData = AnimationData();
    animData->SetSkinnedModel(mNormalModel);
    animData->SetEffectState(rstl::string_l(skDeRezzedEffect), false, mgr);
    mDerezHealth = GetHealthInfo()->GetHP() - mData.unknown_0x4a6c4b40;
    SetVisorOrbitableFlags(CVisorParameters::kVOF_All, true);
  }
}

// @ fn_53_4B98
void CRezbit::SetDeRezzedState(CStateManager& mgr, float dt) {
  if (mRezState != 1) {
    mRezState = 1;
    CAnimData* animData = AnimationData();
    if (mDerezModel.valid()) {
      animData->SetSkinnedModel(*mDerezModel);
      animData->SetEffectState(rstl::string_l(skDeRezzedEffect), true, mgr);
    }
    mDerezTimer = mData.derezTime;
    SetVisorOrbitableFlags(CVisorParameters::kVOF_All, false);
    SetVisorOrbitableFlags(CVisorParameters::kVOF_Dark, true);
  }
}

// @ fn_53_6B64
bool CRezbit::StateOver(CStateManager& mgr, const CTriggerData& data) const {
  return mAnimationState.IsOver();
}

// @ fn_53_6B50
bool CRezbit::IsDeRezzed(CStateManager& mgr, const CTriggerData& data) const {
  return mRezState == 1;
}

// @ fn_53_6B44
bool CRezbit::IsAlert(CStateManager& mgr, const CTriggerData& data) const { return mAlert; }

// @ fn_53_6AEC
bool CRezbit::HasAttackPath(CStateManager& mgr, const CTriggerData& data) const {
  return GetConnectedObject(mgr, kSS_Attack, kSM_Follow) != kInvalidUniqueId;
}

// @ fn_53_6AD0
bool CRezbit::AttackPathOver(CStateManager& mgr, const CTriggerData& data) const {
  return mWaypointNavigation.GetDestination() == kInvalidUniqueId;
}

// @ fn_53_6A90
bool CRezbit::ShouldBecomeRezzed(CStateManager& mgr, const CTriggerData& data) const {
  return mAttackKind == -1 && mRezState != 0 && mDerezTimer <= 0.f;
}

// @ fn_53_6A00
bool CRezbit::ShouldBecomeDeRezzed(CStateManager& mgr, const CTriggerData& data) const {
  if (mAttackKind == -1 && GetHealthInfo()->GetHP() <= mDerezHealth && mAlert && mRezState != 1) {
    return GetBodyController()->GetBodyStateInfo().GetCurrentAdditiveStateId() !=
           pas::kAS_AdditiveLoopReaction;
  }
  return false;
}

// @ fn_53_69F4
bool CRezbit::HasLineOfSight(CStateManager& mgr, const CTriggerData& data) const {
  return mLineOfSight.HasLineOfSight();
}

// @ fn_53_6964
bool CRezbit::ShouldStrafe(CStateManager& mgr, const CTriggerData& data) const {
  if (!mHasPathDestination && mStrafeTimer <= 0.f && mLineOfSight.GetClearTime() > 2.f) {
    const CEntity* shield = mgr.GetObjectById(mShieldActorId);
    return !shield || !shield->GetActive();
  }
  return false;
}

// @ fn_53_694C
bool CRezbit::FoundStrafeDirection(CStateManager& mgr, const CTriggerData& data) const {
  return mStrafeIndex != -1;
}

// @ fn_53_67E0
bool CRezbit::ShouldLaserAttack(CStateManager& mgr, const CTriggerData& data) const {
  if (mAttackTimer <= 0.f && mAttackSelection == 1 && mLaserId == kInvalidUniqueId &&
      !mgr.GetCameraManager(0)->IsInCinematicCamera()) {
    if (mTeamAiMgrId == kInvalidUniqueId ||
        CScriptTeamAiMgr::CanStartAttack(CScriptTeamAiMgr::kAT_Projectile, mgr, mTeamAiMgrId,
                                         GetUniqueId())) {
      const CEntity* shield = mgr.GetObjectById(mShieldActorId);
      if (!shield || !shield->GetActive()) {
        if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mTargetId))) {
          const float dy = target->GetTranslation().GetY() - GetTranslation().GetY();
          const float dx = target->GetTranslation().GetX() - GetTranslation().GetX();
          const float dz = 0.f;
          const float distanceSq = dx * dx + dy * dy + dz * dz;
          return distanceSq >= mData.cuttingLaserMinAttackDist * mData.cuttingLaserMinAttackDist &&
                 distanceSq <= mData.cuttingLaserMaxAttackDist * mData.cuttingLaserMaxAttackDist;
        }
      }
    }
  }
  return false;
}

// @ fn_53_6688
bool CRezbit::ShouldEnergyBoltAttack(CStateManager& mgr, const CTriggerData& data) const {
  if (mAttackTimer <= 0.f && mAttackSelection == 2 &&
      !mgr.GetCameraManager(0)->IsInCinematicCamera()) {
    if (mTeamAiMgrId == kInvalidUniqueId ||
        CScriptTeamAiMgr::CanStartAttack(CScriptTeamAiMgr::kAT_Projectile, mgr, mTeamAiMgrId,
                                         GetUniqueId())) {
      const CEntity* shield = mgr.GetObjectById(mShieldActorId);
      if (!shield || !shield->GetActive()) {
        if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mTargetId))) {
          const float dy = target->GetTranslation().GetY() - GetTranslation().GetY();
          const float dx = target->GetTranslation().GetX() - GetTranslation().GetX();
          const float dz = 0.f;
          const float distanceSq = dx * dx + dy * dy + dz * dz;
          return distanceSq >= mData.energyBoltMinAttackDist * mData.energyBoltMinAttackDist &&
                 distanceSq <= mData.energyBoltMaxAttackDist * mData.energyBoltMaxAttackDist;
        }
      }
    }
  }
  return false;
}

// @ fn_53_6548
bool CRezbit::ShouldVirusAttack(CStateManager& mgr, const CTriggerData& data) const {
  if (mVirusTimer <= 0.f && !mgr.GetCameraManager(0)->IsInCinematicCamera()) {
    if (mTeamAiMgrId == kInvalidUniqueId ||
        CScriptTeamAiMgr::CanStartAttack(CScriptTeamAiMgr::kAT_Melee, mgr, mTeamAiMgrId,
                                         GetUniqueId())) {
      if (const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mTargetId))) {
        if (player->GetRezbitState() == CPlayer::kRS_None &&
            player->GetSpawnedMorphballState() != CPlayer::kMS_Morphed) {
          const float dy = player->GetTranslation().GetY() - GetTranslation().GetY();
          const float dx = player->GetTranslation().GetX() - GetTranslation().GetX();
          const float dz = 0.f;
          const float distanceSq = dx * dx + dy * dy + dz * dz;
          return distanceSq >= mData.virusMinAttackDist * mData.virusMinAttackDist &&
                 distanceSq <= mData.virusMaxAttackDist * mData.virusMaxAttackDist;
        }
      }
    }
  }
  return false;
}

// @ fn_53_12C8
void CRezbit::QuitTeam(CStateManager& mgr) {
  if (mTeamAiMgrId != kInvalidUniqueId) {
    if (CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamAiMgrId))) {
      if (team->IsPartOfTeam(GetUniqueId())) {
        team->QuitTeam(GetUniqueId());
        mTeamAiMgrId = kInvalidUniqueId;
      }
    }
  }
}

// @ fn_53_1370
void CRezbit::JoinTeam(CStateManager& mgr) {
  if (mTeamAiMgrId == kInvalidUniqueId) {
    mTeamAiMgrId = CScriptTeamAiMgr::GetAssociatedTeamId(*this, mgr);
  }
  if (mTeamAiMgrId != kInvalidUniqueId) {
    if (CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamAiMgrId))) {
#ifdef TARGET_PC
      // 0x13F4 li r5,1 / 0x13F8 li r6,2 / 0x13FC li r7,-1: Melee, Projectile, Invalid.
      team->JoinTeam(*this, CTeamAiRole::kTAR_Melee, CTeamAiRole::kTAR_Projectile,
                     CTeamAiRole::kTAR_Invalid);
#else
      team->JoinTeam(*this, CTeamAiRole::kTAR_Projectile, CTeamAiRole::kTAR_Unknown,
                     CTeamAiRole::kTAR_Invalid);
#endif
    }
  }
}

// @ fn_53_141C
void CRezbit::CheckVerticalClearance(CStateManager& mgr,
                                     rstl::reserved_vector< bool, 4 >& available) {
  const CVector3f position = GetTranslation();
  const CEntity* target = mgr.GetObjectById(mTargetId);
#ifdef TARGET_PC
  // 0x146C cmplwi r3,0 / beq 0x14C0: retail skips the target-height checks without a target.
  if (target) {
#endif
  const float heightAboveTarget =
      position.GetZ() - static_cast< const CActor* >(target)->GetTranslation().GetZ();
  if (mStrafeDistance + heightAboveTarget > 16.f) {
    available[2] = false;
  }
  if (heightAboveTarget - mStrafeDistance < 3.f) {
    available[3] = false;
  }
#ifdef TARGET_PC
  }
#endif
  if (available[2] || available[3]) {
    const CRayCastResult result = mgr.RayStaticIntersection(
        position, CVector3f::Down(), 16.f, CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid)));
    if (result.IsValid()) {
      const float groundDistance = result.GetTime();
      if (groundDistance + mStrafeDistance > 16.f) {
        available[2] = false;
      }
      if (groundDistance - mStrafeDistance < 3.f) {
        available[3] = false;
      }
    } else {
      available[2] = false;
    }
  }
}

// @ fn_53_1614
void CRezbit::CheckNeighbors(CStateManager& mgr, rstl::reserved_vector< bool, 4 >& available) {
  const float radiusSq = mStrafeDistance * mStrafeDistance;
  const CVector3f position = GetTranslation();
  const CVector3f right = GetTransform().GetRight();
  const CObjectList& list = mgr.GetObjectListById(kOL_ListeningAi);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CActor* other = static_cast< const CActor* >(list[i]);
    if (other && other != this && other->GetCurrentAreaId() == GetCurrentAreaId()) {
      const CVector3f toOther = other->GetTranslation() - position;
      if (toOther.MagSquared() < radiusSq) {
        if (CVector3f::Dot(toOther, right) >= 0.f) {
          if (available[1] && CVector3f::GetAngleDiff(right, toOther) < 1.0471976f) {
            available[1] = false;
          }
        } else if (available[0] && CVector3f::GetAngleDiff(-right, toOther) < 1.0471976f) {
          available[1] = false;
        }
        if (toOther.GetZ() >= 0.f) {
          if (available[2] && CVector3f::GetAngleDiff(CVector3f::Up(), toOther) < 1.0471976f) {
            available[2] = false;
          }
        } else if (available[3] &&
                   CVector3f::GetAngleDiff(CVector3f::Down(), toOther) < 1.0471976f) {
          available[3] = false;
        }
      }
    }
  }
}

// @ fn_53_188C
int CRezbit::FindStrafeDirection(CStateManager& mgr) {
  rstl::reserved_vector< bool, 4 > available(4, true);
  CheckVerticalClearance(mgr, available);
  CheckNeighbors(mgr, available);
  rstl::reserved_vector< long, 4 > valid;
  const CVector3f* direction = mStrafeDirections;
  for (int i = 0; i < available.size(); ++direction, ++i) {
    if (available[i]) {
      available[i] = CanStrafe(mgr, mStrafeDistance, *direction);
    }
    if (available[i]) {
      valid.push_back(i);
    }
  }
  if (valid.size() != 0) {
    return valid[mgr.Random()->Range(0, valid.size() - 1)];
  }
  return -1;
}

// @ fn_53_1AF8
bool CRezbit::CanStrafe(CStateManager& mgr, float distance, const CVector3f& direction) {
  const CVector3f center = GetBoundingBox().GetCenterPoint();
  const CVector3f end = center + direction * distance;
  if (mgr.RayCollideWorld(center, end,
                          CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid),
                                                              CMaterialList(kMT_CollisionActor)),
                          this) &&
      !mPathFindSearch.OnPath(end)) {
    return true;
  }
  return false;
}

// @ fn_53_1C40
void CRezbit::UpdateSeparation(CStateManager& mgr) {
  const CObjectList& list = mgr.GetObjectListById(kOL_ListeningAi);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CPatterned* other = TCastToConstPtr< CPatterned >(list[i]);
    if (other && other != this && other->GetCurrentAreaId() == GetCurrentAreaId()) {
      const CVector3f separation = mSteeringBehaviors.Separation(
          *this, other->GetTranslation(), 20.f * GetModelData()->GetScale().GetX());
      if (separation.IsMagnitudeSafe()) {
        BodyController()->CommandMgr().DeliverCmd(
            CBCLocomotionCmd(separation.AsNormalized(), CVector3f::Zero(), 0.5f));
      }
    }
  }
}

// @ fn_53_1DB0
void CRezbit::UpdateDeflectSfx(float dt) {
  if (CSfxManager::IsPlaying(mDeflectSfx)) {
    mDeflectSfxTime += dt;
    CSfxManager::UpdateEmitter(mDeflectSfx, GetTranslation(), GetTransform().GetForward(),
                               mData.sound_DeflectMissile.maxVolume);
  }
}

// @ fn_53_1E48
void CRezbit::DeflectMissiles(CStateManager& mgr) {
  const CVector3f center = GetAimPosition(mgr, 0.f);
  const CMaterialFilter filter = CMaterialFilter::MakeInclude(CMaterialList(kMT_Projectile));
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList,
                    CAABox(center - mData.missileDeflectRadius * CVector3f::One(),
                           center + mData.missileDeflectRadius * CVector3f::One()),
                    filter, this);

  const rstl::reserved_vector< TUniqueId, 5 > previousMissiles = mDeflectedMissiles;
  mDeflectedMissiles.clear();
  if (nearList.size() == 0) {
    return;
  }

  const float radiusSq = mData.missileDeflectRadius * mData.missileDeflectRadius;
  for (rstl::reserved_vector< TUniqueId, kMaxObjects >::const_iterator it = nearList.begin();
       it != nearList.end(); ++it) {
    CGameProjectile* projectile = TCastToPtr< CGameProjectile >(mgr.ObjectById(*it));
    if (!projectile ||
        !(projectile->GetType() == kWT_Missile ||
          (projectile->GetType() == kWT_Power && projectile->HasAttrib(CWeapon::kPA_ComboShot)))) {
      continue;
    }
    const CVector3f delta = projectile->GetTranslation() - center;
    if (delta.MagSquared() < radiusSq) {
      mDeflectedMissiles.push_back(*it);
      projectile->SetMinHomingDistance(mData.missileDeflectRadius);
      CProjectileWeapon& weapon = projectile->Projectile();
      const CVector3f dir = delta + (projectile->GetTranslation() - projectile->GetPreviousPos());
      const CVector3f axis = CVector3f::Cross(dir, delta);
      if (axis.CanBeNormalized()) {
#ifdef TARGET_PC
        // 0x2170 fmuls f0,f1(0.017453292),f0: one degrees-to-radians multiply, not two.
        const CQuaternion rotation = CQuaternion::AxisAngle(
            CUnitVector3f(axis), CRelAngle::FromDegrees(mData.missileDeflectRate));
#else
        const CQuaternion rotation = CQuaternion::AxisAngle(
            CUnitVector3f(axis), CRelAngle::FromDegrees(0.017453292f * mData.missileDeflectRate));
#endif
        weapon.SetWorldSpaceOrientation(rotation.BuildTransform4f() *
                                        weapon.GetTransform().GetRotation());
      }
#ifdef TARGET_PC
      // 0x2204 lwz r0,0xc08 / cmpwi r0,5 / beq 0x2228: stop once mDeflectedMissiles is full.
      if (mDeflectedMissiles.size() == 5) {
        break;
      }
#endif
    }
  }

  for (rstl::reserved_vector< TUniqueId, 5 >::const_iterator it = mDeflectedMissiles.begin();
       it != mDeflectedMissiles.end(); ++it) {
    if (rstl::find(previousMissiles.begin(), previousMissiles.end(), *it) !=
        previousMissiles.end()) {
      continue;
    }
    bool playSound = true;
    if (CSfxManager::IsPlaying(mDeflectSfx)) {
      if (mDeflectSfxTime > 0.5f) {
        CSfxManager::SfxStop(mDeflectSfx);
      } else {
        playSound = false;
      }
    }
    if (playSound) {
      mDeflectSfx = PlayCustomSound(GetTranslation(), GetTransform().GetForward(),
                                    mData.sound_DeflectMissile, false);
      mDeflectSfxTime = 0.f;
    }
    break;
  }
}

// @ fn_53_2414
void CRezbit::StartVirusAttack(CStateManager& mgr, const rstl::string& locator) {
  if (CPlayer* player = TCastToPtr< CPlayer >(mgr.ObjectById(mTargetId))) {
    if (player->GetSpawnedMorphballState() != CPlayer::kMS_Morphed &&
        !player->GetCameraManager()->IsInCinematicCamera()) {
      const CVector3f start = GetLctrTransform(locator).GetTranslation();
      const CVector3f end = player->GetTranslation();
      if (mgr.RayCollideWorld(start, end,
#ifdef TARGET_PC
                              // 0x24CC-0x2574: include 0x80 = Solid|Character|CollisionActor
                              // (data_3A8/3AC/3B0), exclude 0x88 = Player|ProjectilePassthrough
                              // (data_3B4/3B8), type 0x90 = 3.
                              CMaterialFilter::MakeIncludeExclude(
                                  CMaterialList(kMT_Solid, kMT_Character, kMT_CollisionActor),
                                  CMaterialList(kMT_Player, kMT_ProjectilePassthrough)),
#else
                              CMaterialFilter::MakeInclude(
                                  CMaterialList(kMT_Solid, kMT_Character, kMT_CollisionActor,
                                                kMT_Player, kMT_ProjectilePassthrough)),
#endif
                              this)) {
        player->StartRezbitState(
            mgr, CRezbitEffectOptions(mData.virusMorphballFx, mData.sound_VirusHUD,
                                      mData.sound_HUDReboot, 3.f, 1.5f, 2.5f, true));
        mgr.ApplyDamage(GetUniqueId(), mTargetId, GetUniqueId(), mVirusDamage,
                        CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid)), CVector3f::Zero());
      }
    }
  }
}

// @ fn_53_2660
void CRezbit::FireEnergyBolt(CStateManager& mgr, float dt) {
  const CEntity* target = mgr.GetObjectById(mTargetId);
#ifdef TARGET_PC
  // 0x26B0 mr. r29,r3 / beq 0x295C: no target, no bolt.
  if (!target) {
    return;
  }
#endif
  const CVector3f gunPos = GetLctrTransform(CSegId(1)).GetTranslation();
  CVector3f aimPos = static_cast< const CActor* >(target)->GetAimPosition(mgr, 0.f);
  if (const CPlayer* player = TCastToConstPtr< CPlayer >(target)) {
    aimPos = ProjectileInfo()->PredictInterceptPos(gunPos, aimPos, *player, false, dt);
  }
  CVector3f dir = aimPos - gunPos;
  if (dir.IsMagnitudeSafe()) {
    if (mBoltCount > 0 && mLastBoltDirection.IsMagnitudeSafe() &&
        CVector3f::GetAngleDiff(dir, mLastBoltDirection) > 0.17453292f) {
      const CVector3f limited =
          CVector3f::Slerp(mLastBoltDirection.AsNormalized(), dir.AsNormalized(),
                           CRelAngle::FromRadians(0.17453292f));
      aimPos = gunPos + dir.Magnitude() * limited;
    }
    const CTransform4f xf = CTransform4f::LookAt(gunPos, aimPos, CVector3f::Up());
    LaunchProjectile(xf, mgr, 6, 0, false, CImpactVisorEffect::None(), CVector3f(1.f, 1.f, 1.f));
#ifdef TARGET_PC
    // 0x2900 addi r5,r1,0x84: the sound plays at gunPos (stored 0x26F4-0x2708).
    PlayCustomSound(gunPos, GetTransform().GetForward(), mData.sound_EnergyBolt, false);
#else
    PlayCustomSound(GetTranslation(), GetTransform().GetForward(), mData.sound_EnergyBolt, false);
#endif
    ++mBoltCount;
    mLastBoltDirection = aimPos - gunPos;
  }
}

// @ fn_53_2DE4
void CRezbit::DeleteLaser(CStateManager& mgr) {
  if (CPlasmaProjectile* laser = static_cast< CPlasmaProjectile* >(mgr.ObjectById(mLaserId))) {
    laser->ResetBeam(mgr, false);
    CSfxManager::RemoveEmitter(mLaserSfx);
    mgr.DeleteObjectRequest(mLaserId);
    mLaserId = kInvalidUniqueId;
    mLaserSfx = CSfxHandle();
  }
}

// @ fn_53_2E88
void CRezbit::UpdateLaser(CStateManager& mgr, float dt) {
  if (CPlasmaProjectile* laser = static_cast< CPlasmaProjectile* >(mgr.ObjectById(mLaserId))) {
    const float t = mLaserElapsed / mLaserDuration;
    const CVector3f position = (1.f - t) * mLaserStart + t * mLaserEnd;
    mLaserElapsed += dt;
    laser->UpdateFx(CTransform4f::LookAt(GetTranslation(), position, CVector3f::Up()), dt, mgr);
    CSfxManager::UpdateEmitter(mLaserSfx, laser->GetCurrentPos(), CVector3f::Up(), 127);
  }
}

// @ fn_53_2FB8
void CRezbit::SpawnLaser(CStateManager& mgr, float dt) {
  const CBeamInfo beamInfo = TLdrToBeamInfo(mData.cuttingLaserBeamInfo, 0x291);
  const TUniqueId uid = mgr.AllocateUniqueId();
  CDamageInfo damage = mCuttingLaserInfo.GetDamage();
  damage.SetDamage(dt * damage.GetDamage());
  damage.SetNoImmunity(true);
  CPlasmaProjectile* laser = rs_new CPlasmaProjectile(
      mCuttingLaserInfo.Token(), rstl::string_l("RezBitCuttingLaser"), kWT_Light, beamInfo,
      CTransform4f::Identity(), kMT_ProjectilePassthrough, damage, uid, GetCurrentAreaId(),
      GetUniqueId(), CWeaponAssetInfo(), false,
      CWeapon::kPA_KeepInCinematic | CWeapon::kPA_BigStrike);
  if (laser) {
    laser->Fire(CTransform4f::LookAt(GetTranslation(), mLaserStart, CVector3f::Up()), mgr, false);
    laser->SetDamageDuration(1.f);
    mgr.AddObject(*laser);
    laser->SetNextDrawNode(GetUniqueId());
#ifdef TARGET_PC
    // 0x31F8 addi r5,r29,0xcfc: the laser sound starts at mLaserEnd.
    mLaserSfx = PlayCustomSound(mLaserEnd, CVector3f::Up(), mData.sound_CuttingLaser, true);
#else
    mLaserSfx = PlayCustomSound(GetTranslation(), CVector3f::Up(), mData.sound_CuttingLaser, true);
#endif
    mLaserId = uid;
  }
}

// @ fn_53_332C
bool CRezbit::SetupLaserTargets(CStateManager& mgr) {
  if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mTargetId))) {
    const CVector3f startAim = target->GetAimPosition(mgr, 0.f);
    const CVector3f targetPos = target->GetTranslation();
    const CVector3f endAim = target->GetAimPosition(mgr, 0.5f * mLaserDuration);
    const CVector3f center = 0.5f * (((endAim + targetPos) - startAim) + endAim);
    CVector3f perpendicular(center.GetY() - GetTranslation().GetY(),
                            -(center.GetX() - GetTranslation().GetX()), 0.f);
    if (perpendicular.IsMagnitudeSafe()) {
      const CVector3f direction = perpendicular.AsNormalized();
      mLaserStart = center + 15.f * direction;
      mLaserEnd = center - 15.f * direction;
      return true;
    }
  }
  return false;
}

// @ fn_53_3538
void CRezbit::UpdatePatrolMovement() {
  CVector3f move = BodyController()->CommandMgr().GetMoveVector();
  BodyController()->CommandMgr().ClearLocomotionCmds();
  const pas::EStepDirection direction = FindBestStepDirection(move);
  if (move.IsMagnitudeSafe() && direction != pas::kSD_Forward) {
    CVector3f face = CVector3f::Zero();
    switch (direction) {
    case pas::kSD_Backward:
      face = -move;
      break;
    case pas::kSD_Left:
      face = CVector3f(move.GetY(), -move.GetX(), move.GetZ());
      break;
    case pas::kSD_Right:
      face = CVector3f(-move.GetY(), move.GetX(), move.GetZ());
      break;
    }
    BodyController()->CommandMgr().DeliverCmd(CBCLocomotionCmd(move, face.AsNormalized(), 1.f));
  } else {
    BodyController()->CommandMgr().DeliverCmd(CBCLocomotionCmd(move, CVector3f::Zero(), 1.f));
  }
}

// @ fn_53_3704
void CRezbit::BreakShield(CStateManager& mgr) {
  if (!mShieldBroken) {
    mShieldBroken = true;
    if (BodyController()->BodyStateInfo().GetCurrentAdditiveStateId() ==
        pas::kAS_AdditiveLoopReaction) {
      BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_Unknown33));
    }
    if (CActor* shield = static_cast< CActor* >(mgr.ObjectById(mShieldActorId))) {
      if (mShieldExplodeEffect.valid()) {
        const CTransform4f explosionXf(GetTransform().BuildMatrix3f(), shield->GetTranslation());
        CExplosion* explosion = rs_new CExplosion(
            *mShieldExplodeEffect, mgr.AllocateUniqueId(),
            CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true, kInvalidEditorId),
            rstl::string_l("RezbitShieldExplosionFx"), explosionXf, 0, GetModelData()->GetScale(),
            CColor::White(), -1);
        if (explosion) {
          mgr.AddObject(explosion);
#ifdef TARGET_PC
          // 0x3790 lbz r0,0xc00 / 0x3794 beq 0x3928 (effect invalid) and 0x38BC cmplwi r28,0 /
          // 0x38C0 beq 0x3928 (no explosion): the explode sound (0x3900) and the shield emitter
          // removal (0x3904-0x3920) sit inside the valid-effect block; without an effect only
          // SetActive(false) (0x3928) runs.
          PlayCustomSound(GetTranslation(), GetTransform().GetForward(), mData.sound_ShieldExplode,
                          false);
          if (mShieldSfx) {
            CSfxManager::RemoveEmitter(mShieldSfx);
            mShieldSfx = CSfxHandle();
          }
#endif
        }
      }
#ifndef TARGET_PC
      PlayCustomSound(GetTranslation(), GetTransform().GetForward(), mData.sound_ShieldExplode,
                      false);
      if (mShieldSfx) {
        CSfxManager::RemoveEmitter(mShieldSfx);
        mShieldSfx = CSfxHandle();
      }
#endif
      shield->SetActive(false);
    }
  }
}

// @ fn_53_3A48
void CRezbit::LowerShields(CStateManager& mgr) {
  if (mShieldBroken) {
    return;
  }
  if (BodyController()->BodyStateInfo().GetCurrentAdditiveStateId() ==
      pas::kAS_AdditiveLoopReaction) {
    BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_AdditiveIdle));
#ifdef TARGET_PC
    // 0x3A7C bne 0x3AD8: the timer is only rearmed when the shield was up.
    mShieldTimer = mData.shieldDownTime + mgr.Random()->Range(0.f, mData.shieldDownTimeVariance);
#endif
  }
#ifndef TARGET_PC
  mShieldTimer = mData.shieldDownTime + mgr.Random()->Range(0.f, mData.shieldDownTimeVariance);
#endif
  if (CEntity* shield = mgr.ObjectById(mShieldActorId)) {
    shield->SetActive(false);
  }
  if (mShieldSfx) {
    CSfxManager::RemoveEmitter(mShieldSfx);
    mShieldSfx = CSfxHandle();
    PlayCustomSound(GetTranslation(), GetTransform().GetForward(), mData.sound_ShieldOff, false);
  }
}

// @ fn_53_3B78
void CRezbit::UpdateWobble(CStateManager& mgr) {
  const float a = mgr.Random()->Range(-1.f, 1.f);
  const float b = mgr.Random()->Range(-1.f, 1.f);
  const float xz = 2.f * a * rstl::max_val(0.f, mDamageCooldownTimer);
  const float yz = 2.f * b * rstl::max_val(0.f, mDamageCooldownTimer);
  mWobbleTransform = CTransform4f::Shear(0.f, xz, 0.f, yz, 0.f, 0.f);
}

// @ fn_53_3C68
void CRezbit::UpdateShield(CStateManager& mgr, float dt) {
  if (!mShieldBroken) {
    if (mRezState == 0 && mAlert) {
      if (mAttackKind > 0 && mAttackKind < 5) {
        if (BodyController()->BodyStateInfo().GetCurrentAdditiveStateId() ==
            pas::kAS_AdditiveLoopReaction) {
          LowerShields(mgr);
        }
      } else {
        mShieldTimer -= dt;
        if (mShieldTimer <= 0.f) {
          if (BodyController()->BodyStateInfo().GetCurrentAdditiveStateId() ==
              pas::kAS_AdditiveLoopReaction) {
            LowerShields(mgr);
          } else {
            RaiseShields(mgr, dt);
          }
        }
      }
    } else if (BodyController()->BodyStateInfo().GetCurrentAdditiveStateId() ==
               pas::kAS_AdditiveLoopReaction) {
      LowerShields(mgr);
    }
    if (CCollisionActor* shield = static_cast< CCollisionActor* >(mgr.ObjectById(mShieldActorId))) {
      shield->SetTransform(GetTransform());
      const CTransform4f bottomLeft = GetLctrTransform(mShieldBottomLeftSeg);
      const CTransform4f topRight = GetLctrTransform(mShieldTopRightSeg);
      shield->SetTranslation(0.5f * (bottomLeft.GetTranslation() + topRight.GetTranslation()));
#ifndef TARGET_PC
      if (mShieldSfx) {
        CSfxManager::UpdateEmitter(mShieldSfx, GetTranslation(), GetTransform().GetForward(), 0x7f);
      }
#endif
    }
#ifdef TARGET_PC
    // 0x3D5C beq 0x3E0C: the emitter update (0x3E0C-0x3E48) runs with or without the shield actor.
    if (mShieldSfx) {
      CSfxManager::UpdateEmitter(mShieldSfx, GetTranslation(), GetTransform().GetForward(), 0x7f);
    }
#endif
  } else if (BodyController()->BodyStateInfo().GetCurrentAdditiveStateId() ==
             pas::kAS_AdditiveLoopReaction) {
    BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_Unknown33));
  }
}

// @ fn_53_3EA4
void CRezbit::UpdateTimers(float dt) {
  if (mHitByPlayerProjectile) {
    mAlert = true;
    mHitByPlayerProjectile = false;
  }
  mAttackTimer -= dt;
  mVirusTimer -= dt;
  mStrafeTimer -= dt;
  mDerezTimer -= dt;
}

// @ fn_53_3F04
void CRezbit::CheckShieldBroken(CStateManager& mgr, TUniqueId id) {
  if (id == mShieldActorId) {
    if (CActor* shield = static_cast< CActor* >(mgr.ObjectById(mShieldActorId))) {
      if (shield->GetHealthInfo()->GetHP() <= 0.f) {
        BreakShield(mgr);
      }
    }
  }
}

// @ fn_53_3F98
void CRezbit::HandleShieldHit(CStateManager& mgr, const TUniqueId& id) {
  if (id == mShieldActorId) {
    CheckShieldBroken(mgr, id);
  } else {
    if (mRezState == 1 && mAttackKind == 3) {
      const CWeapon* weapon = TCastToConstPtr< CWeapon >(mgr.GetObjectById(id));
      if (weapon && weapon->HasAttrib(CWeapon::kPA_Charged)) {
        mDerezTimer = 0.f;
        BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_AbortScripted));
      }
    }
    PlayCustomSound(GetTranslation(), GetTransform().GetForward(), mData.sound_Flinch, false);
  }
}

// @ fn_53_4094
void CRezbit::CreateShieldCollisionActor(CStateManager& mgr) {
  const TUniqueId uid = mgr.AllocateUniqueId();
  const CVector3f halfExtents = GetModelData()->GetScale() * skShieldHalfExtents;
  CCollisionActor* shield =
      rs_new CCollisionActor(uid, GetCurrentAreaId(), GetUniqueId(), halfExtents,
                             CVector3f(0.f, -0.5f * halfExtents.GetY(), 0.f), false, 1000.f);
  if (shield) {
    mShieldActorId = uid;
    shield->SetDamageVulnerability(LdrToDamageVulnerability(mData.shieldVulnerability));
    shield->SetResponseType(kWCR_Unknown15);
    *shield->HealthInfo() = CHealthInfo(mData.shieldHitPoints, 10.f);
    mgr.AddObject(*shield);
  }
}

// @ fn_53_4274
void CRezbit::BuildDerezModel(CAssetId model, CAssetId skinRules) {
  if (model != kInvalidAssetId && skinRules != kInvalidAssetId) {
    mDerezModel = rstl::optional_object< TLockedToken< CSkinnedModel > >(
        rs_new CSkinnedModel(gpSimplePool->GetObj(SObjectTag('CMDL', model)),
                             gpSimplePool->GetObj(SObjectTag('CSKR', skinRules)),
                             GetAnimationData()->GetModelData()->GetLayoutInfo()));
    (*mDerezModel)->SetLayoutInfo(GetAnimationData()->GetModelData()->GetLayoutInfo());
  }
}

// @ fn_53_168
CEntity* LoadRezbit(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrRezbit sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrRezbit.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }

  return rs_new CRezbit(mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
                        LdrToEntityInfo(info, sldrThis.editorProperties),
                        LdrToTransform4f(sldrThis.editorProperties), *modelData,
                        LdrToActorParameters(sldrThis.actorInformation),
                        LdrToPatternedInfo(sldrThis.patterned, nullptr), sldrThis.rezbitProperties);
}

#ifndef MONOLITHIC
static void SetFuncPtrs() {
  static SRezbit_FuncPtrs funcPtrs;
  funcPtrs.mLoader = &LoadRezbit;
  SetSRezbit_FuncPtrs(&funcPtrs);
}

extern "C" void RELMain() { SetFuncPtrs(); }

extern "C" void RELExit() { SetSRezbit_FuncPtrs(nullptr); }
#endif
