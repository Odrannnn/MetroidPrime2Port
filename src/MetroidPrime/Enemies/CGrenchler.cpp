#include "MetroidPrime/Enemies/CGrenchler.hpp"

#include "Collision/CMaterialFilter.hpp"
#include "Collision/CRayCastResult.hpp"
#include "Kyoto/Animation/CSkinnedModel.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Math/CLineSeg.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/Math/CQuaternion.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "Kyoto/Text/CStringTable.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/BodyState/CBodyStateCmdMgr.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/CFluidPlaneManager.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Enemies/CGenericFSM2.hpp"
#include "MetroidPrime/Enemies/CGrenchlerTail.hpp"
#include "MetroidPrime/Factories/CScannableObjectInfo.hpp"
#include "MetroidPrime/PathFinding/CPathFindArea.hpp"
#include "MetroidPrime/Player/CMorphBall.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrGrenchler.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CHUDBillboardEffect.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAIHint.hpp"
#ifdef TARGET_PC
#include "MetroidPrime/ScriptObjects/CScriptCoverPoint.hpp"
#endif
#include "MetroidPrime/ScriptObjects/CScriptAIWaypoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAiJumpPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTeamAiMgr.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWater.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWaypoint.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CElectricBeamProjectile.hpp"
#include "MetroidPrime/Weapons/CWeapon.hpp"
#include "MetroidPrime/Weapons/WeaponTypes.hpp"
#include "REL/REL_Setup.h"
#include "rstl/math.hpp"

#include "MetroidPrime/ScriptObjects/CScriptAiJumpPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTeamAiMgr.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWater.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWaypoint.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CPlasmaProjectile.hpp"
#include "REL/REL_Setup.h"
#include "WorldFormat/CMetroidAreaCollider.hpp"
#include "rstl/math.hpp"
#include <float.h>
#include <math.h>

#ifdef TARGET_PC
#include "port_console.h"
#include <string.h>
#endif

CVector3f gGrenchlerAimOffset(0.f, 0.f, 0.5f); // Guessed name

// Retail `__sinit_CGrenchler_cpp` (0x14234-0x14F00) writes exactly these three tables and nothing
// else: `lis` 0x14298/0x14A08/0x14E80 take .data 0x2B0 (57 triggers), 0x7C0 (32 states), 0x9D8
// (2 code funcs); 273 word copies (`stw` 0x1431C-0x14EF0 = 91 entries x 3) put a **virtual** mfp
// {adjust 0, vtoff, fn 0}. All 91 vtoffs (0x148..0x2B0) index the class vtable at .data 0xA54
// (0x2B4 bytes; this REL ships no `__vt__` symbol for it - it is the one object whose every slot
// the vtoffs land on) and each slot's relocation names the same-named CGrenchler function, so the
// name/vtoff pairs are 1:1 (names .rodata 0x316..0x78B). The host declares the same 91 virtual and
// its mfps resolve to the same bodies; note `BreakGrappleLoop` and `CanGrapple` are folded to one
// address in the host binary (identical ported bodies), so the slot reads back as the latter.
// No call, no branch; the only other stores are stmw r14 0x14244 (frame 0x100, lmw 0x14EF4).
// @ fn_27_14234
static CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    {"AbortCharge", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::AbortCharge)},
    {"Alerted", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::Alerted)},
    {"Attacked", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::Attacked)},
    {"AttackPatternOver",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::AttackPatternOver)},
    {"BeamBadAngle",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::BeamBadAngle)},
    {"BeamHitPlayer",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::BeamHitPlayer)},
    {"BeamHitSticky",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::BeamHitSticky)},
    {"BeamHitWall", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::BeamHitWall)},
    {"Bored", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::Bored)},
    {"BreakGrappleLoop",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::BreakGrappleLoop)},
    {"CanBeamAttack",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::CanBeamAttack)},
    {"CanBite", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::CanBite)},
    {"CanBurstAttack",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::CanBurstAttack)},
    {"CancelManeuvering",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::CancelManeuvering)},
    {"CanCharge", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::CanCharge)},
    {"CanGrapple", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::CanGrapple)},
    {"ChargeFinished",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::ChargeFinished)},
    {"ClearLineOfFire",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::ClearLineOfFire)},
    {"ClearPathToPlayer",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::ClearPathToPlayer)},
    {"CrystalDamaged",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::CrystalDamaged)},
    {"EmergedFromWater",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::EmergedFromWater)},
    {"FacingJumpEnd",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::FacingJumpEnd)},
    {"FacingPlayer",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::FacingPlayer)},
    {"ForceGrapple",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::ForceGrapple)},
    {"Frustrated", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::Frustrated)},
    {"JumpLanded", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::JumpLanded)},
    {"JustBeamAttacked",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::JustBeamAttacked)},
    {"JustBiteAttacked",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::JustBiteAttacked)},
    {"JustBurstAttacked",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::JustBurstAttacked)},
    {"JustHit", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::JustHit)},
    {"GrapplingMorphball",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::GrapplingMorphball)},
    {"HasAttackPattern",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::HasAttackPattern)},
    {"HasValidJumpTarget",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::HasValidJumpTarget)},
    {"InBeamRange", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::InBeamRange)},
    {"InBiteRange", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::InBiteRange)},
    {"InBurstRange",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::InBurstRange)},
    {"InChargeRange",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::InChargeRange)},
    {"ManeuverDone",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::ManeuverDone)},
    {"PauseOver", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::PauseOver)},
    {"PlayerBehindMe",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::PlayerBehindMe)},
    {"PlayerStuck", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::PlayerStuck)},
    {"PlayerSubmerged",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::PlayerSubmerged)},
    {"PlayYellowHitReact",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::PlayYellowHitReact)},
    {"ReturnToPatrol",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::ReturnToPatrol)},
    {"ShouldBackstep",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::ShouldBackstep)},
    {"ShouldSlide", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::ShouldSlide)},
    {"ShouldTurn", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::ShouldTurn)},
    {"SlideOver", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::SlideOver)},
    {"SlideStop", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::SlideStop)},
    {"StopStruggling",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::StopStruggling)},
    {"StruggleOver",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::StruggleOver)},
    {"Stuck", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::Stuck)},
    {"Submerged", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::Submerged)},
    {"TailIntact", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::TailIntact)},
    {"TookDamage", static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::TookDamage)},
    {"TookKnockback",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::TookKnockback)},
    {"TooMuchTurning",
     static_cast< CPatterned::StateMachine::TriggerFunc >(&CGrenchler::TooMuchTurning)},
};

static CPatterned::StateMachine::SStateFunction skStates[] = {
    {"Backstep", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::Backstep)},
    {"BeamAttack", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::BeamAttack)},
    {"BiteAttack", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::BiteAttack)},
    {"BurstAttack", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::BurstAttack)},
    {"Charge", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::Charge)},
    {"ChargeFailed", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::ChargeFailed)},
    {"Dead", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::Dead)},
    {"FollowAttackPattern",
     static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::FollowAttackPattern)},
    {"GrappleAbort", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::GrappleAbort)},
    {"GrappleBite", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::GrappleBite)},
    {"GrappleBreak", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::GrappleBreak)},
    {"GrappleLoop", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::GrappleLoop)},
    {"GrapplePull", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::GrapplePull)},
    {"GrappleSlide", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::GrappleSlide)},
    {"GrappleSlideBonk",
     static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::GrappleSlideBonk)},
    {"GrappleStruggle",
     static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::GrappleStruggle)},
    {"Jump", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::Jump)},
    {"Lurk", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::Lurk)},
    {"Maneuver", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::Maneuver)},
    {"MorphballBite",
     static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::MorphballBite)},
    {"Null", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::Null)},
    {"Patrol", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::Patrol)},
    {"PauseBetweenBeams",
     static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::PauseBetweenBeams)},
    {"PauseBetweenBites",
     static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::PauseBetweenBites)},
    {"Pursue", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::Pursue)},
    {"ShakeOff", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::ShakeOff)},
    {"StopBeamAttack",
     static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::StopBeamAttack)},
    {"Taunt", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::Taunt)},
    {"Turn", static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::Turn)},
    {"TurnToJumpEnd",
     static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::TurnToJumpEnd)},
    {"WalkTowardPlayer",
     static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::WalkTowardPlayer)},
    {"YellowHitReact",
     static_cast< CPatterned::StateMachine::StateFunc >(&CGrenchler::YellowHitReact)},
};

static CPatterned::StateMachine::SCodeFunction skCodeFuncs[] = {
    {"PickJumpTarget",
     static_cast< CPatterned::StateMachine::CodeFunc >(&CGrenchler::PickJumpTarget)},
    {"SetLastActionAsBeam",
     static_cast< CPatterned::StateMachine::CodeFunc >(&CGrenchler::SetLastActionAsBeam)},
};

static const CGrenchler::SJointInfo skJointInfo[] = {
    {"Skeleton_Root", 1.3f, 1.6f, 0, kWCR_Unknown75, true},
    {"R_hip", 0.9f, 0.9f, 0, kWCR_Unknown75, true},
    {"L_hip", 0.9f, 0.9f, 0, kWCR_Unknown75, true},
    {"tailbone_1", 0.6f, 0.9f, 0, kWCR_Unknown75, true},
    {"tailbone_2", 0.45f, 0.55f, 0, kWCR_Unknown75, false},
    {"horn_LCTR", 0.8f, 0.7f, 1, kWCR_Unknown105, true},
    {"eye", 0.8f, 0.9f, 1, kWCR_Unknown105, true},
    {"jaw", 1.2f, 1.1f, 2, kWCR_Unknown105, true},
    {"R_knee", 0.9f, 0.9f, 2, kWCR_Unknown105, true},
    {"L_knee", 0.9f, 0.9f, 2, kWCR_Unknown105, true},
};

// Guessed name; per-joint collision actor table (shared with the constructor).
struct SCollisionJoint {
  const char* x0_;
  float x4_;
  float x8_;
  int xc_;
  EWeaponCollisionResponseTypes x10_;
  bool mAffectsBounds;
};

static const SCollisionJoint skCollisionJoints[10] = {
    {"Skeleton_Root", 1.3f, 1.6f, 0, kWCR_Unknown75, true},
    {"R_hip", 0.9f, 0.9f, 0, kWCR_Unknown75, true},
    {"L_hip", 0.9f, 0.9f, 0, kWCR_Unknown75, true},
    {"tailbone_1", 0.6f, 0.9f, 0, kWCR_Unknown75, true},
    {"tailbone_2", 0.45f, 0.55f, 0, kWCR_Unknown75, false},
    {"horn_LCTR", 0.8f, 0.7f, 1, kWCR_Unknown105, true},
    {"eye", 0.8f, 0.9f, 1, kWCR_Unknown105, true},
    {"jaw", 1.2f, 1.1f, 2, kWCR_Unknown105, true},
    {"R_knee", 0.9f, 0.9f, 2, kWCR_Unknown105, true},
    {"L_knee", 0.9f, 0.9f, 2, kWCR_Unknown105, true},
};

static EMaterialTypes sSolidMaterial = kMT_Solid; // Guessed name

static EMaterialTypes sCollisionIncludeMaterial = kMT_Solid; // Guessed name

static EMaterialTypes sCollisionExcludeMaterial0 = kMT_CollisionActor; // Guessed name

static EMaterialTypes sCollisionExcludeMaterial1 = kMT_AIPassthrough; // Guessed name

static EMaterialTypes sCollisionExcludeMaterial2 = kMT_Player; // Guessed name

// Guessed name; offset from the model transform to the aim point.
static CVector3f skAimOffset(0.f, 0.f, 0.5f);

static const char* const skBodyBubblesEffect = "BodyBubbles"; // Guessed name

static const char* const skElectricEffects[] = {
    "Electric", "ElectricBody", "ElectricHead", "ElectricLLeg", "ElectricRLeg",
}; // Guessed name

// Strings shared by several states; they sit at the start of .rodata in this order, with other
// strings in between (BossGrappleGuardian, head, Skeleton_Root...).
static const char* const skEyeLocator = "eye"; // Guessed name

static const char* const skHornLocator = "horn_LCTR"; // Guessed name

static const char* const skSkeletonRootLocator = "Skeleton_Root"; // Guessed name

static const char* const skJawLocator = "jaw"; // Guessed name

static EMaterialTypes sFloorMaterial = kMT_Floor; // Guessed name

static const char* const skAttachLocator = "attach_LCTR_SDK"; // Guessed name

static const char* const skGuardianBossName = "BossGrappleGuardian";

// Guessed name; per-bone collision actor description table.
struct SCollisionActorDesc {
  const char* mBoneName;
  float mRadius;
  float mScale;
  int mType;
  int x10_;
  bool x14_;
};

static const SCollisionActorDesc skCollisionActors[] = {
    {"Skeleton_Root", 1.3f, 1.6f, 0, 75, true}, {"R_hip", 0.9f, 0.9f, 0, 75, true},
    {"L_hip", 0.9f, 0.9f, 0, 75, true},         {"tailbone_1", 0.6f, 0.9f, 0, 75, true},
    {"tailbone_2", 0.45f, 0.55f, 0, 75, false}, {"horn_LCTR", 0.8f, 0.7f, 1, 105, true},
    {"eye", 0.8f, 0.9f, 1, 105, true},          {"jaw", 1.2f, 1.1f, 2, 105, true},
    {"R_knee", 0.9f, 0.9f, 2, 105, true},       {"L_knee", 0.9f, 0.9f, 2, 105, true},
};

// @ fn_27_13CA8
// @ fn_27_13DB4
// @ fn_27_13ED0
// @ fn_27_13FDC
template < typename T >
void CGrenchler::DeliverCommand(EStateMsg msg, pas::EAnimationState state, const T& cmd) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mBodyController->CommandMgr().DeliverCmd(cmd);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*mBodyController, state)) {
      mBodyController->CommandMgr().DeliverCmd(cmd);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  }
}

// @ fn_27_139E4
const CDamageVulnerability* CGrenchler::SConeVulnerability::GetDamageVulnerability(
    const CDamageVulnerability* vulnerability, const CVector3f& point, const CVector3f& direction,
    const CDamageInfo& info) {
  CVector3f flatDirection(direction.GetX(), direction.GetY(), 0.f);
  if (!flatDirection.CanBeNormalized()) {
    mResponse = mInsideResponse;
    return vulnerability;
  }

  if (CVector3f::GetAngleDiff(flatDirection.AsNormalized(), mDirection) > mMaxAngle) {
    mResponse = mOutsideResponse;
    return &CDamageVulnerability::ReflectVulnerabilty();
  }

  mResponse = mInsideResponse;
  return vulnerability;
}

// @ fn_27_139D4
bool CGrenchler::SConeVulnerability::GetCollisionResponseType(
    const CVector3f& point, const CVector3f& direction, const CWeaponMode& mode, int attributes,
    EWeaponCollisionResponseTypes& response) {
  response = mResponse;
  return true;
}

// @ fn_27_134FC
CGrenchler::SReflectInfo::SReflectInfo()
: mJumpPosition(CVector3f::Zero())
, mLandingPosition(CVector3f::Zero())
, mVulnerability(CDamageVulnerability::ReflectVulnerabilty())
, mJumpEndTime(-1000.f) {
  Reset();
}

// @ fn_27_1320C
CGrenchler::SBiteAttack::SBiteAttack(const CDamageInfo& damage, float minRange, float maxRange,
                                     float minPause, float maxPause, float damageRadius)
: mSubmerged(false)
, mDamage(damage)
, mMinRange(minRange)
, mMaxRange(maxRange)
, mMinPause(minPause)
, mMaxPause(maxPause)
, mJawXf(CTransform4f::Identity())
, mDamageRadius(damageRadius)
, mLastBiteTime(-1000.f) {}

// @ fn_27_130E8
CGrenchler::SBeamAttack::SBeamAttack(const CDamageInfo& damage, const SLdrAudioPlaybackParms& sound,
                                     float minRange, float maxRange, float minPause, float maxPause,
                                     float maxAngle)
: SAttackBase(damage, minRange, maxRange, minPause, maxPause)
, x2c_(false)
, mBeamDamage(damage)
, mLastBeamTime(-1000.f)
, mTargetPosition(CVector3f::Zero())
, mMaxAngle(maxAngle)
, mSound(sound)
, mSoundHandle()
, mHornXf(CTransform4f::Identity()) {}

// @ fn_27_13034
CGrenchler::SAttackBase::SAttackBase(const CDamageInfo& damage, float minRange, float maxRange,
                                     float minPause, float maxPause)
: mDamage(damage)
, mMinRange(minRange)
, mMaxRange(maxRange)
, mMinPause(minPause)
, mMaxPause(maxPause) {}

// @ fn_27_12FB4
CGrenchler::SBurstAttack::SBurstAttack(CAssetId projectile, const CDamageInfo& damage,
                                       float minRange, float maxRange, float minPause,
                                       float maxPause, float damageRadius)
: SAttackBase(damage, minRange, maxRange, minPause, maxPause)
, x2c_(false)
, mProjectile(projectile)
, mNextBurstTime(0.f)
, mDamageRadius(damageRadius)
, mLastBurstTime(-1000.f) {}

// @ fn_27_12EA8
CGrenchler::SChargeAttack::SChargeAttack(float minTimeBetweenCharges, float unknown, float minRange,
                                         float maxRange)
: mStartPosition(CVector3f::Zero())
, mTargetPosition(CVector3f::Zero())
, mMinRange(minRange)
, mMaxRange(maxRange)
, mLastChargeTime(-1000.f)
, mSavedSpeed(1.f)
, mMinTimeBetweenCharges(minTimeBetweenCharges)
, mAltPause(unknown) {
  mStartPosition = mTargetPosition = CVector3f::Zero();
  mStartHealth = 0.f;
}

// @ fn_27_12D50
CGrenchler::SGrappleEffect::SGrappleEffect(CAssetId id)
: mEffectId(id)
, mEffect(id != kInvalidAssetId ? rstl::optional_object< TToken< CGenDescription > >(
                                      gpSimplePool->GetObj(SObjectTag('PART', id)))
                                : rstl::optional_object< TToken< CGenDescription > >()) {}

// @ fn_27_127FC
CGrenchler::SEffectA::SEffectA(CAssetId surfaceRings, CAssetId electric, CAssetId beam,
                               CAssetId hitFx, CAssetId eyeGlow)
: x0_(surfaceRings)
, mSurfaceRing(rs_new CElementGen(gpSimplePool->GetObj(SObjectTag('PART', surfaceRings)),
                                  CElementGen::kMOT_Normal, CElementGen::kOSF_One))
, xc_(electric)
, x10_(gpSimplePool->GetObj(SObjectTag('ELSC', xc_)))
, mBeamProjectileId(kInvalidUniqueId)
, x1c_(gpSimplePool->GetObj(SObjectTag('WPSC', beam)))
, mExplosionAssetId(hitFx)
, mExplosion(hitFx != kInvalidAssetId ? rstl::optional_object< TLockedToken< CGenDescription > >(
                                            gpSimplePool->GetObj(SObjectTag('PART', hitFx)))
                                      : rstl::optional_object< TLockedToken< CGenDescription > >())
, mExplosionId(kInvalidUniqueId)
, mEyeGlow(eyeGlow == kInvalidAssetId
               ? nullptr
               : rs_new CElementGen(gpSimplePool->GetObj(SObjectTag('PART', eyeGlow)),
                                    CElementGen::kMOT_Normal, CElementGen::kOSF_One))
, mEyeGlowAlpha(0.f) {
  x10_.Lock();
  x1c_.Lock();
}

// @ fn_27_12628
CGrenchler::SEffectB::SEffectB(CAssetId a, CAssetId b, float c)
: mStruggleLimit(a)
, x10_(c)
, mLastGrappleTime(-1000.f)
, mGrappleSide(-1)
, mVisorEffectAssetId(b)
, mVisorEffectId(kInvalidUniqueId)
, mVisorEffect(b != kInvalidAssetId ? rstl::optional_object< TToken< CGenDescription > >(
                                          gpSimplePool->GetObj(SObjectTag('PART', b)))
                                    : rstl::optional_object< TToken< CGenDescription > >()) {
  x0_0_ = x0_1_ = x0_2_ = false;
  mStartHealth = mClosestDistance = mStuckTime = x20_ = x24_ = 0.f;
  mStruggleCount = 0;
}

// @ fn_27_12430
CGrenchler::SEffectC::SEffectC(CAssetId swoosh, CAssetId beamPart, const CDamageInfo& damage,
                               const SLdrAudioPlaybackParms& sound)
: mTargetPosition(CVector3f::Zero())
, mBeamLength(0.f)
, mSwooshAssetId(swoosh)
, mBeamPartAssetId(beamPart)
, mLastHornPosition(CVector3f::Zero())
, x44_(0.f)
, mTraceState(0)
, mHitPosition(CVector3f::Zero())
, mDamage(damage)
, mSound(sound)
, mSoundHandle() {}

// @ fn_27_121D8
CGrenchler::SDamageEffect::SDamageEffect(const CDamageInfo& damage, CAssetId effect)
: mDamage(damage)
, mEffect(effect != kInvalidAssetId ? rstl::optional_object< TToken< CGenDescription > >(
                                          gpSimplePool->GetObj(SObjectTag('PART', effect)))
                                    : rstl::optional_object< TToken< CGenDescription > >()) {}

// @ fn_27_12118
CGrenchler::SVectorTriple::SVectorTriple()
: mEye(CVector3f::Zero()), mHorn(CVector3f::Zero()), mHead(CVector3f::Zero()) {}

// @ fn_27_11FCC
CGrenchler::SEffectD::SEffectD(CAssetId effect)
: mWaterSplash(effect != kInvalidAssetId
                   ? rs_new CElementGen(gpSimplePool->GetObj(SObjectTag('PART', effect)),
                                        CElementGen::kMOT_Normal, CElementGen::kOSF_One)
                   : nullptr)
, mLastPosition(CVector3f::Zero())
, mAnchorPosition(CVector3f::Zero())
, mSplashTimer(0.f) {}

// @ fn_27_11580
CGrenchler::CGrenchler(
    TUniqueId uid, const rstl::string& name, CEntityInfo& info, const CTransform4f& xf,
    const CModelData& modelData, const CPatternedInfo& patternedInfo, CAssetId fsmId,
    float tailDestroyedHealth, float minTimeBetweenCharges, float unknown_0x7bd1a35f,
    float chargeAttackMinRange, float chargeAttackMaxRange, float biteAttackMinRange,
    float biteAttackMaxRange, float biteAttackMinPause, bool isGrappleGuardian, bool hasHealthBar,
    const CDamageVulnerability& vulnerability, CAssetId tailAncs, int tailCharacter,
    int tailInitialAnim, uint tailWhenUnderwater, CAssetId taillessModel,
    CAssetId taillessSkinRules, CAssetId tailDarkAncs, int tailDarkCharacter,
    int tailDarkInitialAnim, uint tailWhenUnderwaterDark, CAssetId taillessModelDark,
    CAssetId taillessSkinRulesDark, ushort tailHitSound, ushort tailDestroyedSound,
    float biteAttackMaxPause, float biteAttackDamageRadius, const CDamageInfo& biteDamage,
    const CDamageInfo& beamDamage, float beamAttackMinRange, float beamAttackMaxRange,
    float beamAttackMinPause, float beamAttackMaxPause, float beamAttackMaxAngle,
    const SLdrAudioPlaybackParms& beamAttackSound, const CDamageInfo& burstDamage,
    CAssetId burstProjectile, float burstAttackMinRange, float burstAttackMaxRange,
    float burstAttackMinPause, float burstAttackMaxPause, float burstAttackDamageRadius,
    CAssetId surfaceRingsEffect, CAssetId shallowWaterRing, CAssetId electricEffect, CAssetId pART,
    CAssetId grappleSwoosh, CAssetId grappleBeamPart, CAssetId grappleHitFx,
    const CDamageInfo& grappleDamage, const SLdrAudioPlaybackParms& grappleBeamSound,
    CAssetId beamEffect, int unknown_0xd4753ff4, float unknown_0x05fc6001, float unknown_0x13e5b580,
    float unknown_0xfc6f199d, CAssetId grappleVisorEffect, const CDamageInfo& damageInfo,
    CAssetId pART_0x54b6bfa1, const SLdrAudioPlaybackParms& audioPlaybackParms,
    CAssetId grappleGuardianEyeGlow, CAssetId alternateScannableInfo,
    const CActorParameters& actorParams)
: CPatterned(kPAI_Grenchler, uid, name, kFT_Zero, info, xf, modelData, patternedInfo, kMT_Ground,
             kCT_One, kBT_BiPedal, actorParams)
, mPathFindSearch(nullptr, 0x11 + (patternedInfo.GetIngPossessionData().isAnEncounter ? 0x200 : 0),
                  patternedInfo.GetPathfindingIndex(), 1.f, 1.f, 0, CPFRegion::kRP_Center)
, mElapsedTime(0.f)
, mPathDestination(CVector3f::Zero())
, mPathDestinationTime(-1000.f)
, mBoneTracking(*AnimationData(), rstl::string_l("head"), 0.5235988f, 0.9424779f, 1)
, mLastKnockBackTime(-1000.f)
, mLastHitTime(-1000.f)
, mLastLeashTeleportTime(-1000.f)
, mMarkedRegionIndex(-1)
, mTrackPlayer(false)
, mAlerted(false)
, mIsGrappleGuardian(isGrappleGuardian)
, mHasHealthBar(hasHealthBar)
, mReturnToPatrol(false)
, mScanned(false)
, mUsingJumpPoint(false)
, mSurfaceAlignment(CMaterialFilter::MakeExclude(CMaterialList(kMT_Ceiling, kMT_Wall)))
, mAlternateScanInfo(nullptr)
, mDamageVulnerability(vulnerability)
, mNonUniformVulnerability()
, mFsm(gpSimplePool->GetObj(SObjectTag('FSM2', fsmId)))
, mTaillessModel(taillessModel)
, mTaillessSkinRules(taillessSkinRules)
, mTaillessModelDark(taillessModelDark)
, mTaillessSkinRulesDark(taillessSkinRulesDark)
, mSubmerged(false)
, mEmergeTime(-1000.f)
, mWaterId(kInvalidUniqueId)
, mPlayerSubmerged(false)
, mCollisionManager(nullptr)
, mLastActionFallback(kGA_Invalid)
, mCurrentAction(kGA_Invalid)
, mPathBlockedTime(0.f)
, mStuckCheckTimer(100.f)
, mPursueTime(0.f)
, mNoPathTime(0.f)
, mBoredThreshold(0.f)
, mSteeringTimer(0.f)
, mDefaultTurnSpeed(160.f)
, mBiteAttack(biteDamage, biteAttackMinRange, biteAttackMaxRange, biteAttackMinPause,
              biteAttackMaxPause, biteAttackDamageRadius)
, mBeamAttack(beamDamage, beamAttackSound, beamAttackMinRange, beamAttackMaxRange,
              beamAttackMinPause, beamAttackMaxPause, beamAttackMaxAngle)
, mBurstAttack(burstProjectile, burstDamage, burstAttackMinRange, burstAttackMaxRange,
               burstAttackMinPause, burstAttackMaxPause, burstAttackDamageRadius)
, mSkeletonRootXf(CTransform4f::Identity())
, mTailDestroyed(0)
, xc70_(-1000.f)
, mTailHealth(tailDestroyedHealth)
, mTailDestroyedTime(-1000.f)
, mTailHitSound(tailHitSound)
, mTailDestroyedSound(tailDestroyedSound)
, mFadeActive(0)
, mChargeAttack(minTimeBetweenCharges, unknown_0x7bd1a35f, chargeAttackMinRange,
                chargeAttackMaxRange)
, mSinkTime(0.f)
, mSurfaced(0)
, mDeathFadeTimer(0.f)
, mSurfaceHeight(0.f)
, mSpinSeed(0)
, mTeamAiMgrId(kInvalidUniqueId)
, mManeuverTarget(CVector3f::Zero())
, mManeuverStartTime(0.f)
, mPauseEndTime(0.f)
, xce4_(0)
, mTurnStartTime(-1000.f)
, mSavedTurnSpeed(160.f)
, mLastTurnAnimTime(-1000.f)
, mTurnAnimActive(0)
, mGrappleEffect(pART)
, mEffectA(surfaceRingsEffect, electricEffect, beamEffect, grappleHitFx, grappleGuardianEyeGlow)
, mEffectB(unknown_0xd4753ff4, grappleVisorEffect, unknown_0x05fc6001)
, mEffectC(grappleSwoosh, grappleBeamPart, grappleDamage, grappleBeamSound)
, mBiteAttachTimer(0.f)
, mGuardianFacingBlend(0.f)
, mEyePosition(CVector3f::Zero())
, mTail(tailAncs, tailCharacter, tailInitialAnim)
, mTailWhenUnderwater(tailWhenUnderwater)
, mTailDark(tailDarkAncs, tailDarkCharacter, tailDarkInitialAnim)
, mTailWhenUnderwaterDark(tailWhenUnderwaterDark)
, mDamageFlashTimer(0.f)
, mGrappleCrystalDamage(0.f)
, mCrystalDamage(0.f)
, mGrappleCrystalThreshold(unknown_0x13e5b580)
, mCrystalDamageThreshold(unknown_0xfc6f199d)
, mCrystalState(0)
, mAttachXf(CTransform4f::Identity())
, mPlayerGrabbed(false)
, mCaptureActive(false)
, mPlayerInMouth(false)
, mSlideStartTime(-1000.f)
, mSlideOver(false)
, mSlideStopReady(false)
, mBiteCount(0)
, mAudioPlaybackParms(audioPlaybackParms)
, mElectricSfx()
, mDamageEffect(damageInfo, pART_0x54b6bfa1)
, mTauntType(pas::kTT_Five)
, mTeammateScanTimer(1.f)
, mTouchingTeammateId(kInvalidUniqueId)
, mEffectD(shallowWaterRing) {
  if (!mIsGrappleGuardian) {
    mSurfaceAlignment.SetMode(CSurfaceAlignmentHelper::kM_NearbySurface);
    mSurfaceAlignment.SetAngularRate(16.f);
  } else {
    KnockBackController().EnableFreeze(false);
    KnockBackController().EnableSlow(false);
    KnockBackController().EnableKnockBackPhysics(false);
    KnockBackController().EnableLaggedBurnDeath(false);
    KnockBackController().EnableBurnDeath(false);
    KnockBackController().EnableExplodeDeath(false);
  }

  if (mTailHealth < 0.f || mTailHealth > GetHealthInfo()->GetHP()) {
    mTailHealth = CMath::Clamp(0.f, mTailHealth, GetHealthInfo()->GetHP());
  }

  if (alternateScannableInfo != kInvalidAssetId) {
    mAlternateScanInfo = rs_new TLockedToken< CScannableObjectInfo >(
        gpSimplePool->GetObj(SObjectTag('SCAN', alternateScannableInfo)));
  }
}

// @ fn_27_10C70
CGrenchler::~CGrenchler() {}

// @ fn_27_10BF8
CScannableObjectInfo* CGrenchler::GetScannableObjectInfo() const {
  if (mTailDestroyed == 1 && GetAlive() == true && GetActive() == true) {
    if (mAlternateScanInfo.get() != nullptr) {
      return **mAlternateScanInfo;
    }
  }
  return CPatterned::GetScannableObjectInfo();
}

// @ fn_27_10B58
bool CGrenchler::IsBodyActor(TUniqueId id) {
  if (mCollisionManager.get() == nullptr) {
    return false;
  }
  for (uint i = 0; i < 10; ++i) {
    if (mCollisionManager->GetCollisionDescFromIndex(i).GetCollisionActorId() == id) {
      return skCollisionActors[i].mType == 0;
    }
  }
  return false;
}

// @ fn_27_10AB4
bool CGrenchler::IsCrystalActor(TUniqueId id) {
  if (mCollisionManager.get() == nullptr) {
    return false;
  }
  for (uint i = 0; i < 10; ++i) {
    if (mCollisionManager->GetCollisionDescFromIndex(i).GetCollisionActorId() == id) {
      return skCollisionActors[i].mType == 1;
    }
  }
  return false;
}

// @ fn_27_10AAC
float CGrenchler::GetTailHealth() const { return mTailHealth; }

// @ fn_27_10898
void CGrenchler::StartHitReaction(bool tailDestroyed) {
  KnockBackController().EnableAllAnimReactions(false);
  KnockBackController().EnableAnimReaction(CKnockBackMgr::kAR_Flinch, mTailDestroyed != 1);
  KnockBackController().EnableAnimReaction(CKnockBackMgr::kAR_KnockBack,
                                           tailDestroyed == true || mTailDestroyed != 1);
  pas::ESeverity severity;
  if (mIsGrappleGuardian == true && tailDestroyed == true) {
    severity = pas::kS_Ten;
  } else if (!mSubmerged) {
    severity = tailDestroyed == true ? pas::kS_Three : pas::kS_Zero;
  } else {
    severity = tailDestroyed == true ? pas::kS_Five : pas::kS_Two;
  }
  KnockBackController().SetSeverity(severity);
  if (tailDestroyed == true) {
    BodyController()->CommandMgr().DeliverCmd(
        CBCKnockBackCmd(CVector3f::Forward(), severity, -1, true));
  }
}

// @ fn_27_107EC
float CGrenchler::GetFacingAngleDiff(const CTransform4f& xf) const {
  CVector3f otherForward = xf.GetForward();
  CVector3f forward = GetTransform().GetForward();
  forward.SetZ(0.f);
  otherForward.SetZ(0.f);
  if (!otherForward.CanBeNormalized() || !forward.CanBeNormalized()) {
    return 6.2831855f;
  }
  return CVector3f::GetAngleDiff(otherForward.AsNormalized(), forward.AsNormalized());
}

// @ fn_27_FD2C
void CGrenchler::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  switch (msg.GetMessage()) {
  case kSM_Activate:
    if (mCollisionManager.get() != nullptr) {
      mCollisionManager->SetActive(mgr, true);
    }
    if (mIsGrappleGuardian == true) {
      if (mHasHealthBar == true) {
        mgr.SetBossParams(GetUniqueId(), GetHealthInfo()->GetInitialHP(),
                          gpStringTable->GetStringIndex(skGuardianBossName));
      }
      mChargeAttack.mLastChargeTime = mElapsedTime;
    }
    break;
  case kSM_Alert:
    mAlerted = true;
    break;
  case kSM_Deactivate:
    if (mCollisionManager.get() != nullptr) {
      mCollisionManager->SetActive(mgr, false);
    }
    break;
  case kSM_Delete:
    if (mCollisionManager.get() != nullptr) {
      mCollisionManager->Destroy(mgr);
      mCollisionManager = nullptr;
    }
    if (mEffectA.mBeamProjectileId != kInvalidUniqueId) {
      mgr.DeleteObjectRequest(mEffectA.mBeamProjectileId);
      mEffectA.mBeamProjectileId = kInvalidUniqueId;
    }
    UnmarkPathRegion(mgr);
    DestroyGrappleBeam(mgr);
    if (mElectricSfx) {
      CSfxManager::RemoveEmitter(mElectricSfx);
      mElectricSfx = CSfxHandle();
    }
    break;
  case kSM_Launching:
    NotifyFalling(mgr, msg.GetSenderId());
    break;
  case kSM_Landed:
    if (mCurrentAction == 12) {
#ifdef TARGET_PC
      // retail 0xFFF4: bl RestoreDefaultJointVulnerability
      RestoreDefaultJointVulnerability(mgr);
#else
      ResetBodyVulnerabilities(mgr);
#endif
    }
    break;
  case kSM_OffGround:
    if (mIsGrappleGuardian == true) {
      return;
    }
    break;
  case kSM_AreaLoaded:
    if (GetActive() && mTeamAiMgrId == kInvalidUniqueId) {
      mTeamAiMgrId = CScriptTeamAiMgr::GetAssociatedTeamId(*this, mgr);
      JoinTeam(mgr);
    }
    AddMaterial(kMT_GroundCollider, mgr);
    SetPathArea(mgr);
    UpdateChargeSteering();
    break;
  case kSM_AIUpdateDisabled:
    if (mCollisionManager.get() != nullptr) {
      mCollisionManager->SetPhysicsActive(mgr, false);
    }
    break;
  case kSM_ReflectedDamage:
    mHitByPlayerProjectile = true;
    break;
  case kSM_ResistedDamage: {
    TUniqueId senderId = msg.GetSenderId();
    if (CCollisionActor* collisionActor = TCastToPtr< CCollisionActor >(mgr.ObjectById(senderId))) {
      mHitByPlayerProjectile = true;
      if (IsCrystalActor(senderId) == true && CanCrystalTakeDamage() == true) {
        TUniqueId touchedId = collisionActor->GetLastTouchedObject();
        if (const CWeapon* weapon = TCastToConstPtr< CWeapon >(mgr.GetObjectById(touchedId))) {
          if (0.1f + mDamageFlashTimer < CPatterned::skDamageHitTime) {
            if (mCurrentAction == 8) {
              mGrappleCrystalDamage +=
                  weapon->GetCurrentDamageInfo().GetDamage(*GetDamageVulnerability());
              mDamageFlashTimer = CPatterned::skDamageHitTime;
            } else if (mCrystalState == 0) {
              mCrystalDamage += weapon->GetCurrentDamageInfo().GetDamage(*GetDamageVulnerability());
              mDamageFlashTimer = CPatterned::skDamageHitTime;
              if (mCrystalDamage >= mCrystalDamageThreshold) {
                mCrystalState = 4;
                mCrystalDamage = 0.f;
              }
            }
          }
        }
      }
    }
    break;
  }
  case kSM_Damage:
    if (GetAlive()) {
      mHitByPlayerProjectile = true;
      TUniqueId senderId = msg.GetSenderId();
      if (CCollisionActor* collisionActor =
              TCastToPtr< CCollisionActor >(mgr.ObjectById(senderId))) {
        CHealthInfo* collisionHealth = collisionActor->HealthInfo();
        bool inLanded = false;
        if (mCurrentAction == 12 && !mReflectInfo.mLanded && !mReflectInfo.x1c_5_) {
          inLanded = true;
        }
        bool canDamage = mFadeActive == 0;
        if (!mIsGrappleGuardian) {
          if (2.f + mTailDestroyedTime > mElapsedTime) {
            canDamage = false;
          }
        } else if (mTailDestroyed == 1 && mCurrentAction != 17) {
          canDamage = false;
        }
        if (!inLanded && canDamage == true) {
          TUniqueId touchedId = collisionActor->GetLastTouchedObject();
          if (const CWeapon* weapon = TCastToConstPtr< CWeapon >(mgr.GetObjectById(touchedId))) {
            if (mgr.GetPlayer(0)->GetUniqueId() != weapon->GetOwnerId()) {
              break;
            }
            CDamageInfo damage = weapon->GetCurrentDamageInfo();
            damage.SetRadius(0.f);
            bool tailDestroyed = false;
            if (mTailDestroyed != 1) {
              float scaledDamage = damage.GetDamage(*GetDamageVulnerability());
              float health = GetHealthInfo()->GetHP();
              if (health - scaledDamage < GetTailHealth()) {
                tailDestroyed = true;
                health = GetHealthInfo()->GetHP();
                const CDamageVulnerability* vulnerability = GetDamageVulnerability();
                damage.SetDamageFromVulnerability(*vulnerability, health - GetTailHealth());
                DestroyTail(mgr);
              } else {
                PlayTailHitSound();
              }
            }
            StartHitReaction(tailDestroyed);
            mgr.ApplyDamage(
                touchedId, GetUniqueId(), touchedId, damage,
                CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList()),
                CVector3f::Zero());
            mLastHitTime = mElapsedTime;
            mLastTurnAnimTime = -1000.f;
          } else {
            float damageAmount = 10000.f - collisionHealth->GetHP();
            bool tailDestroyed = false;
            if (mTailDestroyed != 1) {
              float health = GetHealthInfo()->GetHP();
              if (health - damageAmount < GetTailHealth()) {
                tailDestroyed = true;
                health = GetHealthInfo()->GetHP();
                damageAmount = health - GetTailHealth();
                DestroyTail(mgr);
              } else {
                PlayTailHitSound();
              }
            }
            StartHitReaction(tailDestroyed);
            TakeDamage(CVector3f::Zero(), damageAmount);
            mLastHitTime = mElapsedTime;
            mLastTurnAnimTime = -1000.f;
          }
        }
        collisionHealth->SetHP(10000.f);
        mHitByPlayerProjectile = true;
      }
    }
    break;
  case kSM_HitObject:
    if (mCurrentAction != 14 && mCurrentAction != 2 && mCurrentAction != 8) {
      TUniqueId senderId = msg.GetSenderId();
      if (CCollisionActor* collisionActor =
              TCastToPtr< CCollisionActor >(mgr.ObjectById(senderId))) {
        if (collisionActor->GetLastTouchedObject() == mgr.GetPlayer(0)->GetUniqueId() &&
            mCurDamageRemTime <= 0.f) {
          mgr.ApplyDamage(
              GetUniqueId(), mgr.GetPlayer(0)->GetUniqueId(), GetUniqueId(), GetContactDamage(),
              CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList()),
              CVector3f::Zero());
          mCurDamageRemTime = mDamageWaitTime;
          if (mIsGrappleGuardian == true && GetAlive() == true && mTailDestroyed == 1) {
            SendScriptMsgs(kSS_InternalState3, mgr, GetUniqueId(), kSM_None);
#ifdef TARGET_PC
            // retail 0x10758: bl CreateDamageVisorEffect
            CreateDamageVisorEffect(mgr);
#else
            CreateVisorEffect(mgr);
#endif
          }
        }
      }
    }
    break;
  case kSM_InternalMessage0:
    if (GetAlive() == true && GetActive() == true) {
      mReturnToPatrol = true;
      mAlerted = false;
    }
    break;
  case kSM_Create:
    break;
  }
  CPatterned::AcceptScriptMsg(mgr, msg);
}

// @ fn_27_FCA4
bool CGrenchler::CanCrystalTakeDamage() const {
  if (!mIsGrappleGuardian) {
    return false;
  }
  if (!mAlive) {
    return false;
  }
  switch (mCurrentAction) {
  case 8:
    return true;
  case 5:
  case 6:
  case 7:
  case 9:
  case 10:
  case 17:
    return false;
  }
  if (mFadeActive == 1) {
    return false;
  }
  return mScanned;
}

// @ fn_27_FC7C
float CGrenchler::GetVulnerableAngle() const {
  return mIsGrappleGuardian == true ? 0.7853982f : 1.3089969f;
}

// @ fn_27_F398
void CGrenchler::DestroyTail(CStateManager& mgr) {
  mTailDestroyed = 1;
  mFadeActive = 0;
  mTailDestroyedTime = mElapsedTime;
  PlayTailDestroyedSound();
  BodyController()->DouseFlames();
  if (mIsGrappleGuardian == true) {
    mCrystalDamage = 0.f;
    RemoveMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
    UpdateChargeSteering();
  } else {
#ifdef TARGET_PC
    // retail 0xF434: bl SetupBodyVulnerabilities
    SetupBodyVulnerabilities(mgr, CPatterned::GetDamageVulnerability()->MakeIgnoreRadius());
#else
    ResetBodyVulnerabilities(mgr, CPatterned::GetDamageVulnerability()->MakeIgnoreRadius());
#endif
  }
  const bool ingPossessed = IsIngPossessed() == true;
  rstl::optional_object< TLockedToken< CSkinnedModel > >& skinnedModel =
      ingPossessed ? mTaillessSkinnedModelDark : mTaillessSkinnedModel;
  const CAssetId modelRes = ingPossessed ? mTaillessModelDark : mTaillessModel;
  const CAssetId skinRes = ingPossessed ? mTaillessSkinRulesDark : mTaillessSkinRules;
  skinnedModel = TLockedToken< CSkinnedModel >(
      rs_new CSkinnedModel(gpSimplePool->GetObj(SObjectTag('CMDL', modelRes)),
                           gpSimplePool->GetObj(SObjectTag('CSKR', skinRes)),
                           AnimationData()->GetModelData()->GetLayoutInfo()));
  AnimationData()->SetSkinnedModel(*skinnedModel);
  SpawnTail(mgr);
  SendScriptMsgs(kSS_DeGenerate, mgr, kInvalidUniqueId, kSM_None);
  BodyController()->UnFreeze();
}

// @ fn_27_F1C8
void CGrenchler::SpawnTail(CStateManager& mgr) {
  const bool ingPossessed = IsIngPossessed();
  const CAnimationParameters& tailParams = ingPossessed ? mTailDark : mTail;
  const uint tailAnim = mSubmerged ? (ingPossessed ? mTailWhenUnderwaterDark : mTailWhenUnderwater)
                                   : tailParams.GetInitialAnimation();
  const TUniqueId uid = mgr.AllocateUniqueId();
  CTransform4f xf = GetTransform();
  xf.SetColumn(kDZ, CVector3f(0.f, 0.f, 1.f));
  xf.SetTranslation(mSkeletonRootXf.GetTranslation() + CVector3f(0.f, 0.f, 4.f));
  CGrenchlerTail* tail =
      rs_new CGrenchlerTail(uid, GetCurrentAreaId(),
                            CModelData(CAnimRes(tailParams.GetACSFile(), tailParams.GetCharacter(),
                                                GetModelData()->GetScale(), tailAnim, true)),
                            tailAnim, xf, mSubmerged);
  tail->SetModelFlags(GetModelFlags());
  tail->SetCalculateLighting(true);
  mgr.AddObject(tail);
}

// @ fn_27_F180
const CDamageVulnerability* CGrenchler::GetDamageVulnerability() const {
  if (mCurrentAction == 12 && !mReflectInfo.mLanded && !mReflectInfo.x1c_5_) {
    return &mReflectInfo.mVulnerability;
  }
  return CPatterned::GetDamageVulnerability();
}

// @ fn_27_F148
void CGrenchler::SetPathArea(CStateManager& mgr) {
  const TAreaId areaId = GetCurrentAreaId();
  mPathFindSearch.SetArea(mgr.GetWorld()->GetAreaAlways(areaId).GetPostConstructed()->mPathArea);
}

// @ fn_27_F128
CVector3f CGrenchler::GetTargetPosition(CStateManager& mgr) const {
  return mgr.GetPlayer(0)->GetTranslation();
}

// @ fn_27_F080
bool CGrenchler::Bored(CStateManager& mgr, const CTriggerData& data) const {
  if (mIsGrappleGuardian == true) {
    return false;
  }
  if (mUsingJumpPoint == true) {
    return false;
  }
  return FacingPlayer(mgr, CTriggerData(0.f)) ? mNoPathTime > mBoredThreshold : mNoPathTime > 1.f;
}

// @ fn_27_EEE0
bool CGrenchler::ClearPathToPlayer(CStateManager& mgr, const CTriggerData&) const {
  const CVector3f target = GetTargetPosition(mgr) + CVector3f(0.f, 0.f, 0.5f);
  const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid, kMT_AIBlock),
      CMaterialList(kMT_Player, kMT_CollisionActor, kMT_AIPassthrough));
  if (mgr.RayCollideWorld(GetTranslation() + CVector3f(0.f, 0.f, 0.5f), target, filter, this)) {
    return IsNearPath(target, 0.f);
  }
  return false;
}

// @ fn_27_EE1C
bool CGrenchler::InRange(CStateManager& mgr, float minRange, float maxRange) const {
  const float distanceSquared = (GetTargetPosition(mgr) - GetTranslation()).MagSquared();
  return distanceSquared > minRange * minRange && distanceSquared < maxRange * maxRange;
}

// @ fn_27_EDF4
bool CGrenchler::InChargeRange(CStateManager& mgr, const CTriggerData& data) const {
  return InRange(mgr, mChargeAttack.mMinRange, mChargeAttack.mMaxRange);
}

// @ fn_27_EDB0
bool CGrenchler::InBeamRange(CStateManager& mgr, const CTriggerData& data) const {
  float maxRange = mBeamAttack.mMaxRange;
  if (mNoPathTime > 2.f) {
    maxRange *= 2.f;
  }
  return InRange(mgr, mBeamAttack.mMinRange, maxRange);
}

// @ fn_27_ED88
bool CGrenchler::InBurstRange(CStateManager& mgr, const CTriggerData& data) const {
  return InRange(mgr, mBurstAttack.mMinRange, mBurstAttack.mMaxRange);
}

// @ fn_27_EC50
bool CGrenchler::InBiteRange(CStateManager& mgr, const CTriggerData& data) const {
  const CVector3f target = mgr.GetPlayer(0)->GetAimPosition(mgr, 0.f);
  const float dx = target.GetX() - mBiteAttack.mJawXf.Get03();
  const float dy = target.GetY() - mBiteAttack.mJawXf.Get13();
  const float dz = target.GetZ() - mBiteAttack.mJawXf.Get23();
  const float heightLimit = mIsGrappleGuardian ? 7.f : 3.5f;
  if (dz > heightLimit || dz < -heightLimit) {
    return false;
  }
  if (mIsGrappleGuardian == true && mCurrentAction != 8) {
    const float gx = target.GetX() - mVectors.mHorn.GetX();
    const float gy = target.GetY() - mVectors.mHorn.GetY();
    const float gz = target.GetZ() - mVectors.mHorn.GetZ();
    if (gx * gx + gy * gy + gz * gz < mBiteAttack.mMaxRange * mBiteAttack.mMaxRange) {
      return true;
    }
  }
  const float distanceSquared = 0.f + (dx * dx + dy * dy);
  return distanceSquared > mBiteAttack.mMinRange * mBiteAttack.mMinRange &&
         distanceSquared < mBiteAttack.mMaxRange * mBiteAttack.mMaxRange;
}

// @ fn_27_EB4C
void CGrenchler::Taunt(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    if (IsDeeplySubmerged(mgr) == true) {
      mTauntType = pas::kTT_Four;
    } else if (mgr.Random()->Range(0.f, 1.f) < 0.5f) {
      mTauntType = pas::kTT_Zero;
    } else {
      mTauntType = pas::kTT_Five;
    }
  }
  SetAttackState(kGA_Taunt, msg);
  DeliverCommand(msg, pas::kAS_Taunt, CBCTauntCmd(mTauntType));
}

// @ fn_27_EAE4
void CGrenchler::Null(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
#ifdef TARGET_PC
    // retail 0xEB24: bl SetupBodyVulnerabilities
    SetupBodyVulnerabilities(mgr, CPatterned::GetDamageVulnerability()->MakeIgnoreRadius());
#else
    ResetBodyVulnerabilities(mgr, CPatterned::GetDamageVulnerability()->MakeIgnoreRadius());
#endif
  }
}

// @ fn_27_E864
void CGrenchler::MoveToTarget(CStateManager& mgr, float dt, const CVector3f& target) {
  mPathDestination = target;
  if (mIsGrappleGuardian == true) {
    mPathDestination.SetZ(GetTranslation().GetZ());
  }
  mPathDestinationTime = mElapsedTime;
  mPathFindNavigation.SetDestination(mPathDestination);
  if (mCurrentAction == 4) {
    mPathFindSearch.SetAvoidanceFilter(0);
  } else {
    mPathFindSearch.SetAvoidanceFilter(1);
  }
  UnmarkPathRegion(mgr);
  mPathFindNavigation.PathFind(mgr, kStateMsg_Activate, dt, *this);
  mUsingJumpPoint = false;
  if (!mIsGrappleGuardian && PathShagged(mgr, CTriggerData(0.f)) == true) {
    CScriptAiJumpPoint* jumpPoint = FindJumpPoint(mgr, target, kInvalidUniqueId);
    if (jumpPoint != nullptr) {
      mPathDestination = jumpPoint->GetTranslation();
      mPathFindNavigation.SetDestination(mPathDestination);
      mPathFindNavigation.PathFind(mgr, kStateMsg_Activate, dt, *this);
      if (!PathShagged(mgr, CTriggerData(0.f))) {
        mUsingJumpPoint = true;
      } else {
        jumpPoint = FindJumpPoint(mgr, target, jumpPoint->GetUniqueId());
        if (jumpPoint != nullptr) {
          mPathDestination = jumpPoint->GetTranslation();
          mPathFindNavigation.SetDestination(mPathDestination);
          mPathFindNavigation.PathFind(mgr, kStateMsg_Activate, dt, *this);
          if (!PathShagged(mgr, CTriggerData(0.f))) {
            mUsingJumpPoint = true;
          }
        }
      }
    }
  }
  MarkPathRegion(mgr);
}

// @ fn_27_E634
CScriptAiJumpPoint* CGrenchler::FindJumpPoint(CStateManager& mgr, const CVector3f& target,
                                              TUniqueId exclude) {
  CScriptAiJumpPoint* best = nullptr;
  float bestCost = FLT_MAX;
  CObjectList& list = mgr.ObjectListById(kOL_AiWaypoint);
  for (int index = list.GetFirstObjectIndex(); index != -1;
       index = list.GetNextObjectIndex(index)) {
    CScriptAiJumpPoint* jumpPoint = TCastToPtr< CScriptAiJumpPoint >(list[index]);
    if (jumpPoint == nullptr || !jumpPoint->GetActive() ||
        jumpPoint->GetCurrentAreaId() != GetCurrentAreaId() ||
        jumpPoint->GetUniqueId() == exclude) {
      continue;
    }
    const CScriptWaypoint* waypoint =
        TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(jumpPoint->GetJumpPoint()));
    if (waypoint == nullptr) {
      continue;
    }
    if (GetSearchPath()->PathExists(GetTranslation(), jumpPoint->GetTranslation()) !=
        CPathFindSearch::kR_Success) {
      continue;
    }
    const CVector3f toJump = GetTranslation() - jumpPoint->GetTranslation();
    const CVector3f jumpToWaypoint = jumpPoint->GetTranslation() - waypoint->GetTranslation();
    const CVector3f waypointToTarget = waypoint->GetTranslation() - target;
    const float height = fabs(toJump.GetZ());
    float cost = toJump.Magnitude() + height;
    cost = jumpToWaypoint.Magnitude() + cost;
    cost = waypointToTarget.Magnitude() + cost;
    if (GetSearchPath()->PathExists(waypoint->GetTranslation(), target) !=
        CPathFindSearch::kR_Success) {
      cost += 10000.f;
    }
    if (cost < bestCost) {
      bestCost = cost;
      best = jumpPoint;
    }
  }
  return best;
}

// @ fn_27_E5A4
void CGrenchler::UnmarkPathRegion(CStateManager& mgr) {
  if (mMarkedRegionIndex == -1 || GetPathArea(mgr) == nullptr) {
    return;
  }
  CPFRegion* region = GetPathArea(mgr)->GetRegionPtr(mMarkedRegionIndex);
  if (region != nullptr) {
    region->Data()->SetAvoidanceFlags(region->Data()->GetAvoidanceFlags() & ~1);
  }
  mMarkedRegionIndex = -1;
}

// @ fn_27_E570
CPFArea* CGrenchler::GetPathArea(CStateManager& mgr) const {
  const TAreaId areaId = GetCurrentAreaId();
  return mgr.GetWorld()->GetAreaAlways(areaId).GetPostConstructed()->mPathArea;
}

// @ fn_27_E390
void CGrenchler::MarkPathRegion(CStateManager& mgr) {
  const rstl::reserved_vector< CVector3f, 16 >& waypoints = mPathFindSearch.GetWaypoints();
  if (waypoints.size() != 0) {
    const CVector3f lastPoint = waypoints[waypoints.size() - 1];
    const CPFRegion* lastRegion = GetPathArea(mgr)->FindClosestRegion(
        lastPoint, GetSearchPath()->GetRegionFlags(), GetSearchPath()->GetCreatureMask(), 2.f);
    for (int i = waypoints.size() - 1; i > 0; --i) {
      const CVector3f midpoint = 0.5f * (waypoints[i] + waypoints[i - 1]);
      CPFRegion* region = GetPathArea(mgr)->FindClosestRegion(
          midpoint, GetSearchPath()->GetRegionFlags(), GetSearchPath()->GetCreatureMask(), 2.f);
      if (region && region != lastRegion && region->Data()->GetAvoidanceFlags() == 0) {
        region->Data()->SetAvoidanceFlags(region->Data()->GetAvoidanceFlags() | 1);
        mMarkedRegionIndex = region->GetIndex();
        break;
      }
    }
  }
}

// @ fn_27_E2F4
void CGrenchler::SetSubmerged(bool submerged) {
  if (submerged == true) {
    BodyController()->SetLocomotionType(pas::kLT_Internal14);
    mSurfaceAlignment.SetMode(CSurfaceAlignmentHelper::kM_WorldUp);
  } else {
    if (mSubmerged == true && mCurrentAction != 12) {
      mEmergeTime = 1.f + mElapsedTime;
    }
    BodyController()->SetLocomotionType(pas::kLT_Relaxed);
    mSurfaceAlignment.SetMode(CSurfaceAlignmentHelper::kM_NearbySurface);
  }
  mSubmerged = submerged;
}

// @ fn_27_E2EC
bool CGrenchler::Submerged(CStateManager& mgr, const CTriggerData& data) const {
  return mSubmerged;
}

// @ fn_27_E2C0
void CGrenchler::WalkTowardPlayer(CStateManager& mgr, EStateMsg msg, float dt) {
  Pursue(mgr, msg, dt);
}

// @ fn_27_E1A4
void CGrenchler::YellowHitReact(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_YellowHitReact, msg);
  DeliverCommand(msg, pas::kAS_Taunt, CBCTauntCmd(pas::kTT_Nine));
#ifdef TARGET_PC
  // retail 0xE258 (Activate, tail destroyed): bl ResetBodyVulnerabilities;
  // 0xE294 (Deactivate): bl SetupBodyVulnerabilities
  if (msg == kStateMsg_Activate) {
    if (mTailDestroyed == 1) {
      ResetBodyVulnerabilities(mgr, CPatterned::GetDamageVulnerability()->MakeIgnoreRadius());
    }
  } else if (msg == kStateMsg_Deactivate) {
    SetupBodyVulnerabilities(mgr, CPatterned::GetDamageVulnerability()->MakeIgnoreRadius());
  }
#else
  if (msg == kStateMsg_Activate) {
    if (mTailDestroyed == 1) {
      SetupBodyVulnerabilities(mgr, CPatterned::GetDamageVulnerability()->MakeIgnoreRadius());
    }
  } else if (msg == kStateMsg_Deactivate) {
    ResetBodyVulnerabilities(mgr, CPatterned::GetDamageVulnerability()->MakeIgnoreRadius());
  }
#endif
}

// @ fn_27_E190
bool CGrenchler::PlayYellowHitReact(CStateManager& mgr, const CTriggerData& data) const {
  return mCrystalState == 4;
}

// @ fn_27_E188
bool CGrenchler::CanBeUnPossessed(CStateManager& mgr) const { return false; }

// @ fn_27_DA78
void CGrenchler::Pursue(CStateManager& mgr, EStateMsg msg, float dt) {
  CVector3f target = GetTargetPosition(mgr);
  if (mIsGrappleGuardian == true) {
    target.SetZ(GetTranslation().GetZ());
  }

  bool inWater = IsDeeplySubmerged(mgr);
  if (msg == kStateMsg_Activate || mSubmerged != inWater) {
    SetSubmerged(inWater);
  }

  switch (msg) {
  case kStateMsg_Activate:
    mTrackPlayer = true;
    mStuckCheckTimer = 100.f;
    mSteeringTimer = 0.f;
    mBoredThreshold = 0.f;
    mNoPathTime = 0.f;
    mPathBlockedTime = 0.f;
    mPursueTime = 0.f;
    mDefaultTurnSpeed = BodyController()->GetTurnSpeed();
    mReflectInfo.Reset();
    MoveToTarget(mgr, 0.f, target);
    mBoredThreshold = mgr.Random()->Range(5.f, 13.f);
#ifdef TARGET_PC
    // retail 0xDC08: bl SetupBodyVulnerabilities
    SetupBodyVulnerabilities(mgr, CPatterned::GetDamageVulnerability()->MakeIgnoreRadius());
#else
    ResetBodyVulnerabilities(mgr, CPatterned::GetDamageVulnerability()->MakeIgnoreRadius());
#endif
    break;
  case kStateMsg_Update: {
    if (!mIsGrappleGuardian) {
      if (!IsNearPath(GetTranslation() + CVector3f(0.f, 0.f, 1.f), 0.f)) {
        if (FindJumpTarget(mgr, true) == true) {
          return;
        }
        BodyController()->SetTurnSpeed(2.f * mDefaultTurnSpeed);
      } else {
        BodyController()->SetTurnSpeed(mDefaultTurnSpeed);
        if (mReflectInfo.mJumpEndTime + 3.f < mElapsedTime) {
          mStuckCheckTimer += dt;
          if (mStuckCheckTimer > 0.5f) {
            mStuckCheckTimer = 0.f;
            bool frustrated = false;
            if (Stuck(mgr, CTriggerData(0.f)) == true) {
              if (Frustrated(mgr, CTriggerData(0.f)) == true) {
                frustrated = true;
              }
            }
            if (FindJumpTarget(mgr, frustrated) == true) {
              return;
            }
          }
        }
      }
    } else if (mPredictedLeashTime > 5.f && mLastLeashTeleportTime + 0.5f < mElapsedTime) {
      const float height = mgr.Random()->Range(2.f, 4.f);
      SetTranslation(GetTranslation() + CVector3f(0.f, 0.f, height));
      ResolveCollision(mgr);
      ResetSteering();
      AddMaterial(kMT_GroundCollider, mgr);
      mLastLeashTeleportTime = mElapsedTime;
      mSteeringTimer = -1.f;
    }

    mSteeringTimer += dt;
    if (mSteeringTimer > 0.25f) {
      mSteeringTimer = 0.f;
      if (mPathFindSearch.RemainingPathDistance(GetTranslation()) > 25.f && !mIsGrappleGuardian) {
        ResetSteering();
      } else {
        UpdateChargeSteering();
      }
    }

    if (!mIsGrappleGuardian || mPathDestinationTime + 1.5f < mElapsedTime) {
      if (!mUsingJumpPoint && (target - mPathDestination).MagSquared() > 16.f) {
        MoveToTarget(mgr, dt, target);
      }
    }

    CVector3f moveVector = CVector3f::Zero();
    if (PathShagged(mgr, CTriggerData(0.f)) == true && !mIsGrappleGuardian) {
      BodyController()->CommandMgr().ClearLocomotionCmds();
      mUsingJumpPoint = false;
      mNoPathTime += dt;
    } else if (mPathFindSearch.IsOver()) {
      MoveToTarget(mgr, dt, target);
      mNoPathTime = 0.f;
    } else {
      mPathFindNavigation.PathFind(mgr, msg, dt, *this);
      moveVector = BodyController()->CommandMgr().GetMoveVector();
      mNoPathTime = 0.f;
    }

    if (PathShagged(mgr, CTriggerData(0.f)) == true &&
        (target - GetTranslation()).Magnitude() > 2.f) {
      mPathBlockedTime += dt;
    } else {
      mPathBlockedTime = 0.f;
    }

    BodyController()->CommandMgr().DeliverCmd(CBCLocomotionCmd(moveVector, CVector3f::Zero(), 1.f));
    mPursueTime += dt;
    if (mPursueTime > 1.f) {
      mBiteCount = 0;
    }
    break;
  }
  case kStateMsg_Deactivate:
    UpdateChargeSteering();
    BodyController()->SetTurnSpeed(mDefaultTurnSpeed);
    break;
  }
}

// @ fn_27_DA18
void CGrenchler::PreThink(float dt, CStateManager& mgr) {
  mBoneTracking.PreThink(*AnimationData());
  CPatterned::PreThink(dt, mgr);
}

// @ fn_27_D7E0
void CGrenchler::Think(float dt, CStateManager& mgr) {
  if (!GetActive()) {
    return;
  }

  if (!mStateMachine->HasState()) {
    SetupStateMachineHelper(mgr);
    return;
  }

  if (mCollisionManager.get() == nullptr) {
    CreateCollisionManager(mgr);
  }

  if (!mAlive) {
    CPatterned::Think(dt, mgr);
    return;
  }

  mElapsedTime += dt;
  if (!BodyController()->GetIsActive()) {
    BodyController()->Activate(mgr, pas::kAS_Invalid);
  }
  CPatterned::Think(dt, mgr);
  ThinkBoneTracking(dt, mgr);

  if (mCollisionManager.get() != nullptr) {
    AnimationData()->PreRender();
    mCollisionManager->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);
  }

  if (!mIsGrappleGuardian) {
    mSurfaceAlignment.Update(*this, mgr, dt);
  }

  if (InFluidId() != kInvalidUniqueId) {
    mWaterId = InFluidId();
  }

  UpdateEffects(dt, mgr);
  UpdateDamageFlash(dt);

  if (BodyController()->IsFrozen() == true && mTailDestroyed != 1 &&
      GetHealthInfo()->GetHP() < GetTailHealth()) {
    DestroyTail(mgr);
  }

  UpdateFacingDirection();
  UpdateGuardianBlend(dt, mgr);
  UpdateMovement(dt, mgr);
  UpdateTeammateScan(dt, mgr);
}

// @ fn_27_D3C4
void CGrenchler::UpdateMovement(float dt, CStateManager& mgr) {
  const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(CMaterialList(sSolidMaterial),
                                                                     CMaterialList(sFloorMaterial));
  if (mIsGrappleGuardian == true) {
    if (mHasHealthBar) {
      CVector3f toTarget = mVectors.mHorn - mVectors.mEye;
      CVector3f flat(toTarget.GetX(), toTarget.GetY(), 0.f);
      if (flat.CanBeNormalized() == true) {
        flat.Normalize();
      }
      const float flatY = flat.GetY();
      const float negFlatY = -flatY;
      const float flatX = flat.GetX();
      const CVector3f origin = mVectors.mEye + flat;
      const float distance = 3.6f * toTarget.Magnitude();
      toTarget.Normalize();
      if (!CGameCollision::RayStaticLineOfSightTest(
              mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()),
              origin + CVector3f(flatY, -flatX, 0.f), toTarget, distance, filter) ||
          !CGameCollision::RayStaticLineOfSightTest(
              mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()),
              origin + CVector3f(negFlatY, flatX, 0.f), toTarget, distance, filter)) {
        MoveInOneFrameOR(2.f * (0.15f * (-1.f * CVector3f::Forward())), dt);
      }
    }
  } else {
    float distance = 1.7f;
    if (mCurrentAction == 1 || mCurrentAction == 2) {
      distance += 0.5f;
    }
    if (!CGameCollision::RayStaticLineOfSightTest(mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()),
                                                  mBeamAttack.mHornXf.GetTranslation() -
                                                      CVector3f(0.f, 0.f, 1.f),
                                                  CVector3f::Up(), distance, filter)) {
#ifdef TARGET_PC
      // retail 0xD71C-0xD77C: 0.15 (rodata_254) * (-1 (rodata_148) * Forward), no 2x
      MoveInOneFrameOR(0.15f * (-1.f * CVector3f::Forward()), dt);
#else
      MoveInOneFrameOR(2.f * (0.15f * (-1.f * CVector3f::Forward())), dt);
#endif
    }
  }
}

// @ fn_27_D2D0
void CGrenchler::UpdateTeammateScan(float dt, CStateManager& mgr) {
  if (GetTeamAiMgr(mgr) != nullptr) {
    mTeammateScanTimer -= dt;
    if (mTeammateScanTimer < 0.f) {
      mTouchingTeammateId = GetTeamAiMgr(mgr)->TouchingAnyTeammates(mgr, GetUniqueId(), 0.5f);
      if (mTouchingTeammateId != kInvalidUniqueId) {
        mTeammateScanTimer = 2.f;
      } else {
        mTeammateScanTimer = mgr.Random()->Range(0.2f, 0.4f);
      }
    }
  }
}

// @ fn_27_D20C
void CGrenchler::UpdateGuardianBlend(float dt, CStateManager& mgr) {
  if (mIsGrappleGuardian) {
    if (mScanned) {
      float step = dt / 0.5f;
      if (IsFacingTarget(mgr, 1.5707964f) == true) {
        mGuardianFacingBlend -= step;
      } else {
        mGuardianFacingBlend += step;
      }
      mGuardianFacingBlend = 0.f > mGuardianFacingBlend
                                 ? 0.f
                                 : (1.f < mGuardianFacingBlend ? 1.f : mGuardianFacingBlend);
    }
  }
}

// @ fn_27_D178
void CGrenchler::UpdateFacingDirection() {
  if (mNonUniformVulnerability) {
    CVector3f direction = GetTransform().GetForward();
    direction.SetZ(0.f);
    if (direction.CanBeNormalized() == true) {
      direction.Normalize();
      static_cast< SConeVulnerability* >(mNonUniformVulnerability.GetPtr())
          ->SetDirection(direction);
    }
  }
}

// @ fn_27_D110
bool CGrenchler::IsWadingInWater(CStateManager& mgr) {
  if (mEffectD.mWaterSplash.mGen.get() != nullptr && !mSubmerged) {
    float waterHeight = GetWaterSurfaceHeight(mgr);
    if (0.2f + GetTranslation().GetZ() < waterHeight) {
      return true;
    }
  }
  return false;
}

// @ fn_27_D10C
void CGrenchler::FluidFXThink(EFluidState state, CScriptWater& water, CStateManager& mgr) {}

// @ fn_27_C2EC
void CGrenchler::UpdateEffects(float dt, CStateManager& mgr) {
  const float waterHeight = GetWaterSurfaceHeight(mgr);
  if (mEffectD.mWaterSplash.mGen.get() != nullptr) {
    mEffectD.mWaterSplash.mGen->SetParticleEmission(false);
    if (IsWadingInWater(mgr) == true) {
      mEffectD.mWaterSplash.mGen->SetGeneratorRate(0.f);
      mEffectD.mSplashTimer += dt;
      mEffectD.mWaterSplash.mGen->SetParticleEmission(true);
      mEffectD.mWaterSplash.mGen->SetGlobalScale(0.7f * CVector3f::One());
      float interval = 0.75f;
      CVector3f travelled = mEffectD.mLastPosition - GetTranslation();
      const float distance = travelled.Magnitude();
      if (dt > 0.f) {
        const float speed = distance / dt;
        if (speed > 4.5f) {
          interval = 0.05f;
        } else if (speed > 3.f) {
          interval = 0.11f;
        } else if (speed > 1.5f) {
          interval = 0.18f;
        }
      }
      interval *= (mEffectD.mAnchorPosition.GetZ() < waterHeight ? 0.333f : 0.5f);
      mEffectD.mLastPosition = GetTranslation();
      const float surfaceZ = 0.05f + waterHeight;
      while (mEffectD.mSplashTimer > interval) {
        mEffectD.mSplashTimer -= interval;
        int variant;
        if (mEffectD.mAnchorPosition.GetZ() < waterHeight) {
          const float roll = mgr.Random()->Range(0.f, 1.f);
          if (roll < 0.4f) {
            variant = 0;
          } else if (roll < 0.7f) {
            variant = 1;
          } else {
            variant = 2;
          }
        } else {
          const float roll = mgr.Random()->Range(0.f, 1.f);
          if (roll < 0.5f) {
            variant = 1;
          } else {
            variant = 2;
          }
        }
        CVector3f position = CVector3f::Zero();
        switch (variant) {
        case 0:
          position = mEffectD.mAnchorPosition;
          break;
        case 1:
          position = GetBlendedLocatorPosition(waterHeight, "L_knee", "L_knee_2");
          break;
        case 2:
          position = GetBlendedLocatorPosition(waterHeight, "R_knee", "R_knee_2");
          break;
        }
        position.SetZ(surfaceZ);
        mEffectD.mWaterSplash.mGen->SetTranslation(position);
        mEffectD.mWaterSplash.mGen->ForceParticleCreation(1);
      }
    }
    mEffectD.mWaterSplash.mGen->Update(0.75f * dt);
  }

  if (mEffectA.mSurfaceRing.mGen.get() != nullptr) {
    if (IsDeeplySubmerged(mgr) == true) {
      if (GetTranslation().GetZ() < waterHeight - 5.5f) {
        mEffectA.mSurfaceRing.mGen->SetParticleEmission(false);
      } else {
        mEffectA.mSurfaceRing.mGen->Update(dt);
        mEffectA.mSurfaceRing.mGen->SetParticleEmission(true);
        mEffectA.mSurfaceRing.mGen->SetTranslation(
            CVector3f(GetTranslation().GetX(), GetTranslation().GetY(), waterHeight));
      }
      if (GetTranslation().GetZ() > waterHeight - 4.f) {
        AnimationData()->SetEffectState(rstl::string_l(skBodyBubblesEffect), false, mgr);
      } else {
        AnimationData()->SetEffectState(rstl::string_l(skBodyBubblesEffect), true, mgr);
      }
    } else {
      mEffectA.mSurfaceRing.mGen->SetParticleEmission(false);
      AnimationData()->SetEffectState(rstl::string_l(skBodyBubblesEffect), false, mgr);
    }
  }

  if (mEffectA.mBeamProjectileId != kInvalidUniqueId) {
    CEntity* entity = mgr.ObjectById(mEffectA.mBeamProjectileId);
    if (entity != nullptr && entity->GetActive() == true) {
      CPlasmaProjectile* projectile = static_cast< CPlasmaProjectile* >(entity);
      CTransform4f xf(CTransform4f::LookAt(mBeamAttack.mHornXf.GetTranslation(),
                                           mBeamAttack.mTargetPosition, CVector3f::Up()));
      projectile->UpdateFx(xf, dt, mgr);
    }
  }

  CVector3f targetPosition = CVector3f::Zero();
  if (mEffectC.mSwoosh.mGen.get() != nullptr) {
    if (mEffectC.mSoundHandle) {
      CSfxManager::UpdateEmitter(mEffectC.mSoundHandle, mBeamAttack.mHornXf.GetTranslation(),
                                 GetTransform().GetForward(), 127);
    }
    mEffectC.mSwoosh.mGen->SetGlobalTranslation(mBeamAttack.mHornXf.GetTranslation());
    if (mCurrentAction == 14) {
      targetPosition = GetPlayerTargetPosition(mgr);
      CVector3f toTarget = targetPosition - mBeamAttack.mHornXf.GetTranslation();
      mEffectC.mSwoosh.mGen->SetGlobalScale(CVector3f(3.f, 0.16f * toTarget.Magnitude(), 3.f));
      CTransform4f xf(CTransform4f::LookAt(mBeamAttack.mHornXf.GetTranslation(), targetPosition,
                                           CVector3f::Up()));
      mEffectC.mSwoosh.mGen->SetGlobalOrientation(xf);
    } else {
      targetPosition = GetHornTargetPosition();
      if (mEffectC.mTraceState == 2 || mEffectC.mTraceState == 3) {
        targetPosition = mEffectC.mHitPosition;
        targetPosition.SetZ(mBeamAttack.mHornXf.GetTranslation().GetZ());
      }
      CVector3f toTarget = targetPosition - mBeamAttack.mHornXf.GetTranslation();
      mEffectC.mSwoosh.mGen->SetGlobalScale(CVector3f(3.f, 0.16f * toTarget.Magnitude(), 3.f));
      CTransform4f xf(CTransform4f::LookAt(mBeamAttack.mHornXf.GetTranslation(), targetPosition,
                                           CVector3f::Up()));
      mEffectC.mSwoosh.mGen->SetGlobalOrientation(xf);
    }
    mEffectC.mSwoosh.mGen->Update(dt);
  }

  if (mEffectC.mBeamParticles.mGen.get() != nullptr) {
    CVector3f step = targetPosition - mBeamAttack.mHornXf.GetTranslation();
    int count = static_cast< int >(step.Magnitude() / 0.4f);
    if (mEffectC.mBeamParticles.mGen->GetParticleCount() < count) {
      CElementGen* gen = mEffectC.mBeamParticles.mGen.get();
      gen->ForceParticleCreation(count - gen->GetParticleCount());
      count = mEffectC.mBeamParticles.mGen->GetParticleCount();
    }
    if (step.CanBeNormalized() == true) {
      step.Normalize();
      step *= 0.4f;
    }
    CVector3f position = mBeamAttack.mHornXf.GetTranslation();
    for (int i = 0; i < count; ++i) {
      CElementGen::CParticle& particle = mEffectC.mBeamParticles.mGen->mParticles[i];
      particle.mPos = position + mgr.Random()->Float() * step;
      particle.mLineLengthOrSize = 0.5f;
      position += step;
    }
    for (int i = count; i < mEffectC.mBeamParticles.mGen->GetParticleCount(); ++i) {
      CElementGen::CParticle& particle = mEffectC.mBeamParticles.mGen->mParticles[i];
      particle.mPos = CVector3f(0.f, 0.f, -10000.f);
      particle.mLineLengthOrSize = 0.f;
    }
    mEffectC.mBeamParticles.mGen->Update(dt);
  }

  if (mIsGrappleGuardian == true) {
    bool electric = false;
    bool alive = false;
    if (mTailDestroyed == 1 && GetAlive() == true) {
      alive = true;
    }
    if (alive && mCurrentAction != 17) {
      electric = true;
    }
    for (int i = 0; i < 5; ++i) {
      AnimationData()->SetEffectState(rstl::string_l(skElectricEffects[i]), electric, mgr);
    }
    if (electric == true) {
      if (!mElectricSfx) {
        mElectricSfx = PlayCustomSound(mSkeletonRootXf.GetTranslation(),
                                       GetTransform().GetForward(), mAudioPlaybackParms, true);
      }
      CSfxManager::UpdateEmitter(mElectricSfx, mSkeletonRootXf.GetTranslation(),
                                 GetTransform().GetForward(), 127);
    } else if (mElectricSfx) {
      CSfxManager::RemoveEmitter(mElectricSfx);
      mElectricSfx.Clear();
    }
    UpdateExplosionHeight(mgr);
    if (mEffectA.mEyeGlow.mGen.get() != nullptr) {
      if (CanCrystalTakeDamage() == true) {
        const float next = mEffectA.mEyeGlowAlpha + dt;
        mEffectA.mEyeGlowAlpha = 1.f < next ? 1.f : next;
        mEffectA.mEyeGlow.mGen->SetParticleEmission(true);
      } else {
        const float next = mEffectA.mEyeGlowAlpha - dt;
        mEffectA.mEyeGlowAlpha = next < 0.f ? 0.f : next;
        if (0.f == mEffectA.mEyeGlowAlpha) {
          mEffectA.mEyeGlow.mGen->SetParticleEmission(false);
        }
      }
      mEffectA.mEyeGlow.mGen->SetGlobalTranslation(mEyePosition);
      mEffectA.mEyeGlow.mGen->SetGlobalScale(mEffectA.mEyeGlowAlpha * CVector3f::One());
      mEffectA.mEyeGlow.mGen->Update(dt);
    }
  }

  if (BodyController()->IsFrozen() == true) {
    StopElectricBeam(mgr);
  }
}

// @ fn_27_C234
CVector3f CGrenchler::GetPlayerBeamTargetPosition(CStateManager& mgr) const {
  CPlayer* player = mgr.GetPlayer(0);
  CVector3f position = player->GetAimPosition(mgr, 0.f);
  if (player->GetMorphballTransitionState() == CPlayer::kMS_Morphed) {
    position += CVector3f(0.f, 0.f, -0.25f);
  } else {
    position += CVector3f(0.f, 0.f, -0.5f);
  }
  return position;
}

// @ fn_27_C0EC
CVector3f CGrenchler::GetHornTargetPosition() const {
  if (mEffectC.mTargetPosition == CVector3f::Zero()) {
    return mBeamAttack.mHornXf.GetTranslation();
  }
  CVector3f direction = mEffectC.mTargetPosition - mBeamAttack.mHornXf.GetTranslation();
  if (direction.CanBeNormalized() == true) {
    direction.Normalize();
    direction *= mEffectC.mBeamLength;
    return mBeamAttack.mHornXf.GetTranslation() + direction;
  }
  return mBeamAttack.mHornXf.GetTranslation();
}

// @ fn_27_BFF0
CVector3f CGrenchler::GetPlayerTargetPosition(CStateManager& mgr) const {
  CPlayer* player = mgr.GetPlayer(0);
  CVector3f position = player->GetAimPosition(mgr, 0.f);
  if (player->GetMorphballTransitionState() == CPlayer::kMS_Morphed) {
    const CAABox bounds = player->GetTouchBounds().data();
    position = bounds.GetCenterPoint();
  } else {
    position += CVector3f(0.f, 0.f, -0.5f);
  }
  return position;
}

// @ fn_27_BFE4
bool CGrenchler::ShouldTrackPlayer() const { return mTrackPlayer; }

// @ fn_27_BF54
void CGrenchler::ThinkBoneTracking(float dt, CStateManager& mgr) {
  if (!ShouldTrackPlayer()) {
    mBoneTracking.SetActive(false);
  } else {
    mBoneTracking.SetTarget(mgr.GetPlayer(0)->GetUniqueId());
    mBoneTracking.SetActive(true);
  }
  mBoneTracking.Think(dt);
}

// @ fn_27_BB80
void CGrenchler::PreRender(CStateManager& mgr) {
  CPatterned::PreRender(mgr);
  PreRenderBoneTracking(mgr);
  mSkeletonRootXf = GetLctrTransform(rstl::string_l(skSkeletonRootLocator));
  mBeamAttack.mHornXf = GetLctrTransform(rstl::string_l(skHornLocator));
  mBiteAttack.mJawXf = GetLctrTransform(rstl::string_l(skJawLocator));
  if (mIsGrappleGuardian == true) {
    mAttachXf = GetLctrTransform(rstl::string_l(skAttachLocator));
    mEyePosition = GetLctrTransform(rstl::string_l(skEyeLocator)).GetTranslation();
    if (mEffectA.mEyeGlow.mGen.get() != nullptr) {
      mEffectA.mEyeGlow.mGen->SetGlobalTranslation(mEyePosition);
      mEffectA.mEyeGlow.mGen->Update(0.0);
    }
    mVectors.mEye = GetLctrTransform(rstl::string_l("eye")).GetTranslation();
    mVectors.mHorn = GetLctrTransform(rstl::string_l("horn_LCTR")).GetTranslation();
    mVectors.mHead = GetLctrTransform(rstl::string_l("head")).GetTranslation();
    if (mCollisionManager.get() != nullptr) {
      mCollisionManager->Update(0.f, mgr, CCollisionActorManager::kUO_WorldSpace);
    }
  } else if (IsWadingInWater(mgr) == true) {
    const CVector3f first = GetLctrTransform(rstl::string_l("attach_LCTR_SDK")).GetTranslation();
    const CVector3f second = GetLctrTransform(rstl::string_l("attatch_LCTR")).GetTranslation();
    mEffectD.mAnchorPosition = first + (first - second) + CVector3f(0.f, 0.f, -0.35f);
  }
}

// @ fn_27_BA08
CVector3f CGrenchler::GetBlendedLocatorPosition(float height, const char* firstLocator,
                                                const char* secondLocator) const {
  const CVector3f first = GetLctrTransform(rstl::string_l(firstLocator)).GetTranslation();
  const CVector3f second = GetLctrTransform(rstl::string_l(secondLocator)).GetTranslation();
  if (height > first.GetZ() && height > second.GetZ()) {
    return CVector3f(first.GetX(), first.GetY(), height);
  }
  if (height < first.GetZ() && height < second.GetZ()) {
    return CVector3f(second.GetX(), second.GetY(), height);
  }
  return CVector3f(0.5f * (first.GetX() + second.GetX()), 0.5f * (first.GetY() + second.GetY()),
                   height);
}

// @ fn_27_B9B0
void CGrenchler::PreRenderBoneTracking(CStateManager& mgr) {
  mBoneTracking.PreRender(mgr, *AnimationData(), GetTransform(), GetModelData()->GetScale(),
                          *BodyController());
}

// @ fn_27_B8F0
float CGrenchler::GetWaterSurfaceHeight(CStateManager& mgr) {
  if (InFluidId() != kInvalidUniqueId) {
    mWaterId = InFluidId();
  }
  if (mWaterId == kInvalidUniqueId) {
    return -10000.f;
  }
  CScriptWater* water =
      TCastToPtr< CScriptWater >(const_cast< CEntity* >(mgr.GetObjectById(mWaterId)));
  if (water == nullptr) {
    return GetTranslation().GetZ();
  }
  return water->GetTriggerBoundsWR().GetMaxPoint().GetZ();
}

// @ fn_27_B824
bool CGrenchler::IsDeeplySubmerged(CStateManager& mgr, const CActor& actor) const {
  if (actor.InFluidId() == kInvalidUniqueId) {
    return false;
  }
  CScriptWater* water =
      TCastToPtr< CScriptWater >(const_cast< CEntity* >(mgr.GetObjectById(actor.InFluidId())));
  if (water == nullptr) {
    return false;
  }
  return water->GetTriggerBoundsWR().GetMaxPoint().GetZ() > actor.GetTranslation().GetZ() + 2.5f;
}

// @ fn_27_B800
bool CGrenchler::IsDeeplySubmerged(CStateManager& mgr) const {
  return IsDeeplySubmerged(mgr, *this);
}

// @ fn_27_B744
const CGenericFSM2* CGrenchler::GetStateMachine() const {
  if (!mFsm->IsLoaded()) {
    return nullptr;
  }
  TToken< CGenericFSM2 > machine(*mFsm);
  return *machine;
}

// @ fn_27_B6B8
void CGrenchler::SetupStateMachineHelper(CStateManager& mgr) {
  InitializeStateMachine(mgr);
  mStateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  mStateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  mStateMachine->SetCodeFunctions(skCodeFuncs, ARRAY_SIZE(skCodeFuncs));
}

// @ fn_27_B1F4
void CGrenchler::Dead(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    UnmarkPathRegion(mgr);
    QuitTeam(mgr);
    mFadeActive = 0;
    DestroyGrappleBeam(mgr);
    if (mElectricSfx) {
      CSfxManager::RemoveEmitter(mElectricSfx);
      mElectricSfx = CSfxHandle();
    }
    RemoveMaterial(kMT_Target, kMT_Orbit, kMT_SeekerTarget, mgr);
    SetSubmerged(IsDeeplySubmerged(mgr));
    if (mSubmerged == 1) {
      RemoveMaterial(kMT_GroundCollider, mgr);
      mVerticalMovement = true;
      mOnGround = false;
      mOnStaticGround = false;
      mSpinSeed = mgr.Random()->Next();
      mStateControlledMassiveDeath = false;
      ResetBodyVulnerabilities(mgr, CDamageVulnerability::ReflectVulnerabilty());
    }
    if (mEffectA.mSurfaceRing.mGen.get() != nullptr) {
      mEffectA.mSurfaceRing.mGen->SetParticleEmission(false);
    }
    if (mEffectD.mWaterSplash.mGen.get() != nullptr) {
      mEffectD.mWaterSplash.mGen->SetParticleEmission(false);
    }
    AnimationData()->SetEffectState(rstl::string_l(skBodyBubblesEffect), false, mgr);
    for (int i = 0; i < 5; ++i) {
      AnimationData()->SetEffectState(rstl::string_l(skElectricEffects[i]), false, mgr);
    }
    DestroyGrappleBeam(mgr);
    EndMorphballCapture(mgr);
  } else if (msg == kStateMsg_Update) {
    if (mIsGrappleGuardian == 1 &&
        mgr.GetPlayer(0)->GetMorphballTransitionState() == CPlayer::kMS_Morphed &&
        mCollisionManager.get() != nullptr) {
      mCollisionManager.get()->SetActive(mgr, false);
    }
  }

  if (mSubmerged == 0) {
    CPatterned::Dead(mgr, msg, dt);
    return;
  }

  float rotateRate = 10.f * dt;
  if ((mSpinSeed & 1) != 0) {
    rotateRate *= -1.f;
  }
  SetTransform((CQuaternion::ZRotation(CRelAngle::FromDegrees(rotateRate)) *
                CQuaternion::FromMatrix(GetTransform()))
                   .BuildTransform4f(GetTranslation()));

  if (mSurfaced == 1) {
    const float bob = CMath::FastSinR(2.5f * mDeathFadeTimer);
    SetTranslation(
        CVector3f(GetTranslation().GetX(), GetTranslation().GetY(), 0.25f * bob + mSurfaceHeight));
    mDeathFadeTimer += dt;
    if (mDeathFadeTimer > GetFadeOnDeathTime()) {
      MassiveDeath(mgr);
    }
  } else {
    const float waterHeight = GetWaterSurfaceHeight(mgr);
    if (GetTranslation().GetZ() >= waterHeight - 3.3f) {
      SetVelocityWR(CVector3f::Zero());
      mSurfaced = 1;
      mSurfaceHeight = GetTranslation().GetZ();
      mAlphaDelta = -1.f / GetFadeOnDeathTime();
    } else {
      mSinkTime += dt;
      SetVelocityWR(0.85f * CVector3f::Up());
      if (mSinkTime >= 10.f) {
        mAlphaDelta = -1.f / GetFadeOnDeathTime();
        mDeathFadeTimer += dt;
        if (mDeathFadeTimer > GetFadeOnDeathTime()) {
          MassiveDeath(mgr);
        }
      }
    }
  }
}

// @ fn_27_B15C
void CGrenchler::Patrol(CStateManager& mgr, EStateMsg msg, float dt) {
  bool submerged = IsDeeplySubmerged(mgr);
  if (msg == kStateMsg_Activate || mSubmerged != submerged) {
    SetSubmerged(submerged);
  }
  mReturnToPatrol = false;
  CPatterned::Patrol(mgr, msg, dt);
}

// @ fn_27_B110
void CGrenchler::SetAttackState(EAction state, EStateMsg msg) {
  if (msg == kStateMsg_Deactivate) {
    PushAttackHistory(mAttackHistory, mCurrentAction);
    mCurrentAction = kGA_None;
  } else {
    mCurrentAction = state;
  }
}

// @ fn_27_AFE8
void CGrenchler::Lurk(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_Lurk, msg);
  switch (msg) {
  case kStateMsg_Activate:
    RemoveMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
    mSubmerged = IsDeeplySubmerged(mgr);
    SetSubmerged(mSubmerged);
    mHitByPlayerProjectile = false;
    mKnockBackController.EnableAllAnimReactions(false);
    if (mSubmerged == 1) {
      DisableGroundCollision(mgr);
      mTrackPlayer = false;
    }
    break;
  case kStateMsg_Deactivate:
    AddMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
    mKnockBackController.EnableAllAnimReactions(true);
    if (mSubmerged == 1) {
      EnableGroundCollision(mgr, true);
    }
    break;
  }
}

// @ fn_27_AEAC
void CGrenchler::BiteAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_BiteAttack, msg);
  if (msg == kStateMsg_Activate) {
    mBiteAttack.mSubmerged = IsDeeplySubmerged(mgr);
  } else if (msg == kStateMsg_Deactivate) {
    mBiteAttack.mLastBiteTime = mElapsedTime;
  } else if (mIsGrappleGuardian == 1) {
    TurnToPlayer(mgr, dt, 1.0471976f);
  }
  SetSubmerged(mBiteAttack.mSubmerged);
  DeliverCommand(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_One));
}

// @ fn_27_ACF0
void CGrenchler::GrappleBite(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_BiteAttack, msg);
  switch (msg) {
  case kStateMsg_Activate:
    mBiteAttack.mSubmerged = false;
    mTrackPlayer = false;
    IncrementAttached(mgr);
    mBiteAttachTimer = 1.2f;
    break;
  case kStateMsg_Update:
    if (mBiteAttachTimer > 0.f) {
      mBiteAttachTimer -= dt;
      if (mBiteAttachTimer <= 0.f) {
        DecrementAttached(mgr);
      }
    }
    break;
  case kStateMsg_Deactivate:
    mBiteAttack.mLastBiteTime = mElapsedTime;
    mTrackPlayer = true;
    if (mBiteAttachTimer > 0.f) {
      DecrementAttached(mgr);
      mBiteAttachTimer = 0.f;
    }
    mEffectB.mLastGrappleTime = mElapsedTime;
    break;
  }
  SetSubmerged(false);
  DeliverCommand(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_Two));
}

// @ fn_27_AC9C
CVector3f CGrenchler::GetScanObjectIndicatorPosition(const CStateManager& mgr) const {
  if (mCurrentAction == 14) {
    return mVectors.mHead;
  }
  return CActor::GetScanObjectIndicatorPosition(mgr);
}

// @ fn_27_AC5C
CVector3f CGrenchler::GetOrbitPosition(const CStateManager& mgr) const {
  return GetAimPosition(mgr, 0.f);
}

// @ fn_27_AB94
CVector3f CGrenchler::GetAimPosition(const CStateManager& mgr, float dt) const {
  CVector3f aim = mSkeletonRootXf.GetTranslation() + skAimOffset;
  if (!mIsGrappleGuardian) {
    return aim;
  }
  if (!mScanned) {
    return mEyePosition + CVector3f(0.f, 0.f, 1.6f);
  }
  return CVector3f::Lerp(mEyePosition, aim, mGuardianFacingBlend);
}

// @ fn_27_AAC8
bool CGrenchler::IsFacing(const CVector3f& position, float maxAngle) const {
  CVector2f toTarget = (position - GetTranslation()).ToVec2f();
  CVector2f forward = GetTransform().GetForward().ToVec2f();
  return CVector2f::GetAngleDiff(forward, toTarget) < maxAngle;
}

// @ fn_27_AA74
bool CGrenchler::IsFacingTarget(CStateManager& mgr, float maxAngle) const {
  return IsFacing(GetTargetPosition(mgr), maxAngle);
}

// @ fn_27_AA60
bool CGrenchler::ForceGrapple(CStateManager& mgr, const CTriggerData& data) const {
  return mCrystalState > 0;
}

// @ fn_27_A994
bool CGrenchler::IsPlayerLookingAtMe(CStateManager& mgr) const {
  const CPlayer* player = mgr.GetPlayer(0);
  CVector2f playerForward = player->GetTransform().GetForward().ToVec2f();
  CVector2f toMe = (GetTranslation() - player->GetTranslation()).ToVec2f();
  return CVector2f::GetAngleDiff(playerForward, toMe) < 0.87266463f;
}

// @ fn_27_A96C
bool CGrenchler::FacingPlayer(CStateManager& mgr, const CTriggerData& data) const {
  return IsFacingTarget(mgr, 0.61086524f);
}

// @ fn_27_A938
bool CGrenchler::ShouldTurn(CStateManager& mgr, const CTriggerData& data) const {
  return !IsFacingTarget(mgr, M_PIF / 4.f);
}

// @ fn_27_A8F0
bool CGrenchler::ShouldSlide(CStateManager& mgr, const CTriggerData& data) const {
  return !mEffectB.mGrappleSide && !IsNearAvoidHint(mgr);
}

// @ fn_27_A7C4
bool CGrenchler::ShouldBackstep(CStateManager& mgr, const CTriggerData& data) const {
  if (IsGuardianCharging() == true) {
    return false;
  }
  if (mPredictedLeashTime > 1.f) {
    return true;
  }
  if (mSubmerged == 1) {
    return false;
  }
  if (mTouchingTeammateId != kInvalidUniqueId) {
    CActor* actor =
        TCastToPtr< CActor >(const_cast< CEntity* >(mgr.GetObjectById(mTouchingTeammateId)));
    if (actor != nullptr) {
      if (IsFacing(actor->GetTranslation(), 1.9198622f) == true) {
        return true;
      }
      if (CMath::AbsF(actor->GetTranslation().GetZ() - GetTranslation().GetZ()) > 4.f) {
        return true;
      }
    }
  }
  return mBiteCount >= 2 + (mTailDestroyed == 1 ? 1 : 0);
}

// @ fn_27_A6B0
void CGrenchler::Backstep(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_Backstep, msg);
  mBiteCount = 0;
  DeliverCommand(msg, pas::kAS_Step, CBCStepCmd(pas::kSD_Backward, pas::kStep_Normal));
}

// @ fn_27_A5C4
bool CGrenchler::SlideStop(CStateManager& mgr, const CTriggerData& data) const {
  const float slideStart = mSlideStartTime;
  const float time = mElapsedTime;
  if (0.5f + slideStart > time) {
    return false;
  }
  if (8.f + slideStart < time) {
    return true;
  }
  if (!mSlideStopReady) {
    return false;
  }
  CVector3f toTarget = mEffectC.mHitPosition - mBeamAttack.mHornXf.GetTranslation();
  toTarget.SetZ(0.f);
  const float threshold = 2.f * mPredictedLeashTime + 2.5f;
  return toTarget.Magnitude() < threshold;
}

// @ fn_27_A5B8
bool CGrenchler::SlideOver(CStateManager& mgr, const CTriggerData& data) const {
  return mSlideOver;
}

// @ fn_27_A398
void CGrenchler::ResolveCollision(CStateManager& mgr) {
  CAABox motionVolume = GetMotionVolume(0.f);
  CAreaCollisionCache cache(motionVolume);
  CGameCollision::BuildAreaCollisionCache(mgr, cache);
  rstl::reserved_vector< TUniqueId, 1024 > nearList;
  mgr.BuildColliderList(nearList, *this, motionVolume);
  CGameCollision::CollisionFailsafe(mgr, cache, *this, *GetCollisionPrimitive(), nearList, 0.f, 0,
                                    0.f);
}

// @ fn_27_A114
void CGrenchler::GrappleSlide(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_GrappleSlide, msg);
  BodyController()->SetLocomotionType(pas::kLT_Internal5);
  BodyController()->CommandMgr().ClearLocomotionCmds();
  switch (msg) {
  case kStateMsg_Activate:
    mSlideStartTime = mElapsedTime;
    mSlideOver = false;
    mSlideStopReady = false;
    SendScriptMsgs(kSS_Locked, mgr);
    mCrystalState = 0;
    break;
  case kStateMsg_Update: {
    CVector3f direction = mEffectC.mHitPosition - mBeamAttack.mHornXf.GetTranslation();
    direction.SetZ(0.f);
    if (direction.CanBeNormalized() == true) {
      direction.Normalize();
      const float elapsed = CMath::Clamp(0.f, mElapsedTime - mSlideStartTime, 2.f);
      const float step = dt * (10.f * (elapsed * 0.5f) + 10.f);
      direction *= step;
      mEffectC.mBeamLength = rstl::max_val(mEffectC.mBeamLength - step, 0.f);
      MoveInOneFrameOR(GetTransform().TransposeRotate(direction), dt);
    }
    if (10.f + mSlideStartTime < mElapsedTime) {
      mSlideOver = true;
    }
    break;
  }
  case kStateMsg_Deactivate:
    SendScriptMsgs(kSS_Unlocked, mgr);
    mBiteCount = 0;
    mEffectB.mLastGrappleTime = mElapsedTime;
    break;
  }
}

// @ fn_27_A02C
void CGrenchler::GrappleSlideBonk(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    SendScriptMsgs(kSS_InternalState2, mgr);
    break;
  case kStateMsg_Update:
    if (10.f + mSlideStartTime < mElapsedTime) {
      mSlideOver = true;
    }
    break;
  case kStateMsg_Deactivate:
    mEffectB.mLastGrappleTime = mElapsedTime;
    DestroyGrappleBeam(mgr);
    break;
  }
  SetAttackState(kGA_GrappleSlide, msg);
  BodyController()->SetLocomotionType(pas::kLT_Relaxed);
  BodyController()->CommandMgr().ClearLocomotionCmds();
}

// @ fn_27_9FA0
bool CGrenchler::AbortCharge(CStateManager& mgr, const CTriggerData& data) const {
  if (!mIsGrappleGuardian) {
    return false;
  }
  return !IsFacingTarget(mgr, 1.5882496f) ? true : ChargeFinished(mgr, data);
}

// @ fn_27_9C54
void CGrenchler::Turn(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    mTurnAnimActive = 0;
    mTurnStartTime = mElapsedTime;
    mSavedTurnSpeed = GetBodyController()->GetTurnSpeed();
    if (mSubmerged == 1) {
      mTurnAnimActive = 0;
    } else if (!mIsGrappleGuardian) {
      if (0.1f + mLastHitTime > mElapsedTime && !IsFacingTarget(mgr, 2.0943952f)) {
        mTurnAnimActive = 1;
      }
    } else {
      mTurnAnimActive = 0;
      if (2.f + mLastTurnAnimTime < mElapsedTime && !IsFacingTarget(mgr, 1.5882496f)) {
        mTurnAnimActive = 1;
      }
      if (mTurnAnimActive == 0) {
        BodyController()->SetTurnSpeed(2.5f * mSavedTurnSpeed);
      }
    }
  } else if (msg == kStateMsg_Deactivate) {
    BodyController()->SetTurnSpeed(mSavedTurnSpeed);
    if (mTurnAnimActive == 1) {
      mLastTurnAnimTime = mElapsedTime;
    }
  }

  if (mTurnAnimActive == 1) {
    DeliverCommand(msg, pas::kAS_Taunt, CBCTauntCmd(pas::kTT_Six));
  } else if (!mIsGrappleGuardian && mSubmerged == 0 && 0.2f + mLastHitTime > mElapsedTime &&
             !IsFacingTarget(mgr, 2.0943952f)) {
    mTurnStartTime = -1000.f;
    mLastTurnAnimTime = -1000.f;
    mTurnAnimActive = 0;
    mTurnStartTime = mElapsedTime;
    mTurnAnimActive = 1;
    DeliverCommand(kStateMsg_Activate, pas::kAS_Taunt, CBCTauntCmd(pas::kTT_Six));
  } else {
    CVector3f toTarget = GetTargetPosition(mgr) - GetTranslation();
    toTarget.SetZ(0.f);
    if (toTarget.CanBeNormalized() == true) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCLocomotionCmd(CVector3f::Zero(), toTarget.AsNormalized(), 1.f));
    }
  }
}

// @ fn_27_9C28
bool CGrenchler::IsGuardianCharging() const {
  return mIsGrappleGuardian == 1 && mCrystalState == 4;
}

// @ fn_27_9B78
bool CGrenchler::TooMuchTurning(CStateManager& mgr, const CTriggerData& data) const {
  if (IsGuardianCharging() == true) {
    return true;
  }
  if (mTurnAnimActive == 1) {
    return AnimOver(mgr, data);
  }
  return mTurnStartTime > 0.f && 2.f + mTurnStartTime < mElapsedTime;
}

// @ fn_27_9AD4
void CGrenchler::ResetBodyVulnerabilities(CStateManager& mgr,
                                          const CDamageVulnerability& vulnerability) {
  if (mCollisionManager.get() != nullptr) {
    for (uint i = 0; i < 10; ++i) {
      CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(
          mCollisionManager.get()->GetCollisionDescFromIndex(i).GetCollisionActorId()));
      actor->SetDamageVulnerability(vulnerability.MakeIgnoreRadius());
      actor->ResetNonUniformVulnerability();
    }
  }
}

// @ fn_27_990C
void CGrenchler::SetupBodyVulnerabilities(CStateManager& mgr,
                                          const CDamageVulnerability& vulnerability) {
  if (mCollisionManager.get() != nullptr) {
    for (uint i = 0; i < 10; ++i) {
      const SCollisionJoint& joint = skCollisionJoints[i];
      CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(
          mCollisionManager.get()->GetCollisionDescFromIndex(i).GetCollisionActorId()));
      if (actor != nullptr) {
        if (mTailDestroyed == 1 && (mIsGrappleGuardian == 1 || joint.xc_ == 0)) {
          if (joint.xc_ == 1 && mIsGrappleGuardian == 1) {
            actor->SetDamageVulnerability(CDamageVulnerability::ImmuneVulnerabilty());
          } else {
            actor->SetDamageVulnerability(mDamageVulnerability.MakeIgnoreRadius());
          }
          actor->SetResponseType(kWCR_Unknown45);
          if (joint.xc_ != 1 || !mIsGrappleGuardian) {
            actor->SetNonUniformVulnerability(mNonUniformVulnerability);
          }
        } else {
          if (joint.xc_ == 0) {
            actor->SetDamageVulnerability(vulnerability.MakeIgnoreRadius());
          } else if (joint.xc_ == 1 && mIsGrappleGuardian == 1) {
            actor->SetDamageVulnerability(CDamageVulnerability::ImmuneVulnerabilty());
          } else {
            actor->SetDamageVulnerability(CDamageVulnerability::ReflectVulnerabilty());
          }
          actor->SetResponseType(joint.x10_);
          if (joint.xc_ != 1 || !mIsGrappleGuardian) {
            actor->SetNonUniformVulnerability(mNonUniformVulnerability);
          }
        }
      }
    }
  }
}

// @ fn_27_98C4
void CGrenchler::DisableGroundCollision(CStateManager& mgr) {
  mVerticalMovement = true;
  mOnGround = false;
  RemoveMaterial(kMT_GroundCollider, mgr);
}

// @ fn_27_987C
void CGrenchler::EnableGroundCollision(CStateManager& mgr, bool onGround) {
  mVerticalMovement = false;
  mOnGround = onGround;
  AddMaterial(kMT_GroundCollider, mgr);
}

// @ fn_27_9840
void CGrenchler::SetGroundCollision(CStateManager& mgr, EStateMsg msg) {
  if (msg == kStateMsg_Activate) {
    DisableGroundCollision(mgr);
  } else if (msg == kStateMsg_Deactivate) {
    EnableGroundCollision(mgr, true);
  }
}

// @ fn_27_96E0
void CGrenchler::LaunchToJumpTarget() {
  if (mReflectInfo.mLaunched == 1) {
    return;
  }
  CVector3f velocity = CVector3f::Zero();
  const float dx = mReflectInfo.mLandingPosition.GetX() - GetTranslation().GetX();
  const float dy = mReflectInfo.mLandingPosition.GetY() - GetTranslation().GetY();
  const float gravity = GetGravityConstant();
  const float z = GetTranslation().GetZ();
  const float apex =
      mReflectInfo.mApexOffset + rstl::max_val(z, mReflectInfo.mLandingPosition.GetZ());
  const float climbSpeed = CMath::SqrtF(2.f * gravity * (apex - z));
  velocity.SetZ(climbSpeed);
  float flightTime = climbSpeed / gravity;
  flightTime += CMath::SqrtF(2.f * (apex - mReflectInfo.mLandingPosition.GetZ()) / gravity);
  const float invTime = 1.f / flightTime;
  velocity.SetX(invTime * dx);
  velocity.SetY(invTime * dy);
  SetVelocityWR(velocity);
  mReflectInfo.mLaunched = true;
}

// @ fn_27_9610
void CGrenchler::NotifyFalling(CStateManager& mgr, const TUniqueId& id) {
  if (GetBodyController()->GetCurrentStateId() != pas::kAS_Hurled) {
    CPatterned::AcceptScriptMsg(mgr, CScriptMsg(id, GetUniqueId(), kSM_OffGround));
    SetMomentumWR(CVector3f(0.f, 0.f, -GetGravityConstant() * GetMass()));
    LaunchToJumpTarget();
  }
}

// @ fn_27_94FC
void CGrenchler::TurnToJumpEnd(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    if (mSubmerged == 1) {
      DisableGroundCollision(mgr);
    }
  } else if (msg == kStateMsg_Deactivate && mSubmerged == 1) {
    EnableGroundCollision(mgr, true);
  }

  CVector3f toTarget = mReflectInfo.mLandingPosition - GetTranslation();
  toTarget.SetZ(0.f);
  if (toTarget.CanBeNormalized() == true) {
    BodyController()->CommandMgr().DeliverCmd(
        CBCLocomotionCmd(CVector3f::Zero(), toTarget.AsNormalized(), 1.f));
  }
}

// @ fn_27_94D0
bool CGrenchler::FacingJumpEnd(CStateManager& mgr, const CTriggerData& data) const {
  return IsFacing(mReflectInfo.mLandingPosition, M_PIF / 12.f);
}

// @ fn_27_93FC
void CGrenchler::KnockBack(CStateManager& mgr, const CKnockBackInfo& info) {
  if (mCurrentAction != 12 && GetBodyController()->IsFrozen() != 1 &&
      (mIsGrappleGuardian != 1 ||
       (!(mLastKnockBackTime + 3.f > mElapsedTime) && mFadeActive != 1))) {
    mKnockBackController.SetAdditiveFlinchWeight(mgr.Random()->Range(0.55f, 0.8f));
    CPatterned::KnockBack(mgr, info);
    mLastKnockBackTime = mElapsedTime;
  }
}

// @ fn_27_9018
void CGrenchler::Jump(CStateManager& mgr, EStateMsg msg, float) {
  SetAttackState(kGA_Jump, msg);
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
#ifdef TARGET_PC
    // retail 0x9080: bl ResetBodyVulnerabilities
    ResetBodyVulnerabilities(mgr, CDamageVulnerability::ReflectVulnerabilty());
#else
    SetupBodyVulnerabilities(mgr, CDamageVulnerability::ReflectVulnerabilty());
#endif
    mSurfaceAlignment.SetMode(CSurfaceAlignmentHelper::kM_WorldUp);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Jump)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCJumpCmd(mReflectInfo.mLandingPosition,
                     mReflectInfo.mStartedSubmerged == 1 ? pas::kJT_Normal : pas::kJT_Ambush,
                     pas::kJS_IntoJump, 0, CBCJumpCmd::kFF_AmbushJump));
    }
    if (mAnimationState.GetState() == CAnimationState::kAS_Over) {
      mReflectInfo.mLanded = true;
    }
    if (!mReflectInfo.mWaterCrossed) {
      if (!mReflectInfo.mStartedSubmerged) {
        if (IsDeeplySubmerged(mgr) == true) {
          SetVelocityWR(GetVelocityWR() * 0.33f);
          mReflectInfo.mWaterCrossed = true;
        }
      } else if (!IsDeeplySubmerged(mgr)) {
        mReflectInfo.mWaterCrossed = true;
      }
    }
    if (mReflectInfo.mWaterCrossed == 1) {
      CScriptWater* water =
          TCastToPtr< CScriptWater >(const_cast< CEntity* >(mgr.GetObjectById(mWaterId)));
      if (water != nullptr) {
        const CVector3f splashPosition(GetTranslation().GetX(), GetTranslation().GetY(),
                                       GetWaterSurfaceHeight(mgr));
        mgr.GetFluidPlaneManager()->CreateSplash(GetUniqueId(), mgr, *water, splashPosition, 1.f,
                                                 true);
      }
    }
    break;
  case kStateMsg_Deactivate:
    SetSubmerged(IsDeeplySubmerged(mgr));
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    mReflectInfo.Reset();
    mReflectInfo.mJumpEndTime = mElapsedTime;
    break;
  }
}

// @ fn_27_8FC0
void CGrenchler::RestoreDefaultJointVulnerability(CStateManager& mgr) {
#ifdef TARGET_PC
  // retail 0x8FEC: bl SetupBodyVulnerabilities
  SetupBodyVulnerabilities(mgr, *CPatterned::GetDamageVulnerability());
#else
  ResetBodyVulnerabilities(mgr, *CPatterned::GetDamageVulnerability());
#endif
  mReflectInfo.mLaunched = false;
}

// @ fn_27_8FB4
bool CGrenchler::JumpLanded(CStateManager&, const CTriggerData&) const {
  return mReflectInfo.mLanded;
}

// v2

// @ fn_27_8F9C
bool CGrenchler::Attacked(CStateManager&, const CTriggerData&) const {
  return mHitByPlayerProjectile;
}

// @ fn_27_8F90
bool CGrenchler::Alerted(CStateManager&, const CTriggerData&) const { return mAlerted; }

// @ fn_27_8CC0
void CGrenchler::AddJointCollisions(
    const SJointInfo* joints, int count,
    rstl::vector< CJointCollisionDescription >& descriptions) const {
  const CAnimData* animData = GetAnimationData();
  for (int i = 0; i < count; ++i) {
    const CSegId segId = animData->GetLocatorSegId(rstl::string_l(joints[i].mName));
    if (segId != CSegId::Invalid()) {
      const float radius = mIsGrappleGuardian ? joints[i].mGuardianRadius : joints[i].mRadius;
      CJointCollisionDescription description = CJointCollisionDescription::SphereCollision(
          segId, CVector3f::Zero(), radius, rstl::string_l(joints[i].mName), 10000.f);
      descriptions.push_back(description);
    }
  }
}

// @ fn_27_8858
void CGrenchler::CreateCollisionManager(CStateManager& mgr) {
  rstl::vector< CJointCollisionDescription > descriptions;
  descriptions.reserve(10);
  AddJointCollisions(skJointInfo, 10, descriptions);
  mCollisionManager = rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(),
                                                    descriptions, GetActive());

  CVector3f direction = GetTransform().GetForward();
  direction.SetZ(0.f);
  if (direction.CanBeNormalized() == true) {
    direction.Normalize();
  }
  if (mNonUniformVulnerability.GetPtr() == nullptr) {
    mNonUniformVulnerability =
        rs_new SConeVulnerability(direction, GetVulnerableAngle(), kWCR_Unknown75, kWCR_Unknown105);
  }

  for (uint i = 0; i < 10; ++i) {
    const SJointInfo& joint = skJointInfo[i];
    CCollisionActor* collisionActor = TCastToPtr< CCollisionActor >(
        mgr.ObjectById(mCollisionManager->GetCollisionDescFromIndex(i).GetCollisionActorId()));
    CHealthInfo* health = collisionActor->HealthInfo();
    health->SetKnockbackResistance(GetHealthInfo()->GetKnockBackResistance());
    health->SetHP(10000.f);
    if (joint.mVulnerabilityGroup == 0) {
      collisionActor->SetDamageVulnerability(
          CPatterned::GetDamageVulnerability()->MakeIgnoreRadius());
    } else {
      collisionActor->SetDamageVulnerability(CDamageVulnerability::ReflectVulnerabilty());
    }
    if (joint.mVulnerabilityGroup != 1 || !mIsGrappleGuardian) {
      collisionActor->SetNonUniformVulnerability(mNonUniformVulnerability);
    }
  }

  SetMaterialFilter(CMaterialFilter::MakeIncludeExclude(CMaterialList(sCollisionIncludeMaterial),
                                                        CMaterialList(sCollisionExcludeMaterial0,
                                                                      sCollisionExcludeMaterial1,
                                                                      sCollisionExcludeMaterial2)));
  AddMaterial(kMT_ProjectilePassthrough, mgr);
}

// @ fn_27_8660
void CGrenchler::BeamAttack(CStateManager& mgr, EStateMsg msg, float) {
  SetAttackState(kGA_BeamAttack, msg);
  if (msg == kStateMsg_Activate) {
    mBurstAttack.x2c_ = false;
    mBeamAttack.x2c_ = false;
    SetSubmerged(false);
    mBoneTracking.SetMaxBoneRotation(0.43633232f);
    if (mTailDestroyed == 1) {
      mSpeed = 1.2f;
    }
  } else if (msg == kStateMsg_Deactivate) {
    StopElectricBeam(mgr);
    mBoneTracking.SetMaxBoneRotation(0.87266463f);
    mBeamAttack.mLastBeamTime = mElapsedTime;
    mBiteCount = 0;
    mSpeed = 1.f;
    mBeamAttack.mSoundHandle = CSfxHandle();
  } else if (BodyController()->IsFrozen() == true) {
    StopElectricBeam(mgr);
  }
  const CVector3f target = GetTargetPosition(mgr) + CVector3f(0.f, 0.f, 2.5f);
  DeliverCommand(msg, pas::kAS_ProjectileAttack,
                 CBCProjectileAttackCmd(pas::kS_One, target, false));
}

// @ fn_27_860C
void CGrenchler::StopBeamAttack(CStateManager&, EStateMsg, float) {
  BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_NextState));
}

// @ fn_27_85C4
bool CGrenchler::PlayerBehindMe(CStateManager& mgr, const CTriggerData&) const {
  if (mBeamAttack.x2c_) {
    return false;
  }
  return !IsFacingTarget(mgr, M_PIF / 2.f);
}

// @ fn_27_8488
void CGrenchler::BurstAttack(CStateManager& mgr, EStateMsg msg, float) {
  SetAttackState(kGA_BurstAttack, msg);
  if (msg == kStateMsg_Activate) {
    mBurstAttack.x2c_ = true;
    mBeamAttack.x2c_ = true;
    SetSubmerged(true);
  } else if (msg == kStateMsg_Deactivate) {
    mBurstAttack.mLastBurstTime = mElapsedTime;
    mBurstAttack.mNextBurstTime = mElapsedTime + mBurstAttack.mMinPause;
    if (mgr.IsRandomAvailable() == true) {
      mBurstAttack.mNextBurstTime =
          mBurstAttack.mNextBurstTime +
          (mBurstAttack.mMaxPause - mBurstAttack.mMinPause) * mgr.Random()->Float();
    }
    mBiteCount = 0;
  }
  DeliverCommand(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_Eleven));
}

// @ fn_27_8378
void CGrenchler::AddToRenderer(const CStateManager& mgr) const {
  CPatterned::AddToRenderer(mgr);
  if (GetAlive() == true) {
    if (mEffectA.mSurfaceRing.mGen.get() != nullptr &&
        mEffectA.mSurfaceRing.mGen->GetParticleEmission() == true) {
      gpRender->AddParticleGen(*mEffectA.mSurfaceRing.mGen);
    }
    if (mEffectD.mWaterSplash.mGen.get() != nullptr) {
      gpRender->AddParticleGen(*mEffectD.mWaterSplash.mGen);
    }
    if (mEffectC.mSwoosh.mGen.get() != nullptr) {
      gpRender->AddParticleGen(*mEffectC.mSwoosh.mGen);
    }
    if (mEffectC.mBeamParticles.mGen.get() != nullptr) {
      gpRender->AddParticleGen(*mEffectC.mBeamParticles.mGen);
    }
  }
}

// @ fn_27_82E4
void CGrenchler::Render(const CStateManager& mgr) const {
  CPatterned::Render(mgr);
  if (mEffectC.mSwoosh.mGen.get() != nullptr) {
    mEffectC.mSwoosh.mGen->Render();
  }
  if (mEffectC.mBeamParticles.mGen.get() != nullptr) {
    mEffectC.mBeamParticles.mGen->Render();
  }
  if (mEffectA.mEyeGlow.mGen.get() != nullptr && mEffectA.mEyeGlowAlpha > 0.f) {
    mEffectA.mEyeGlow.mGen->Render();
  }
}

// @ fn_27_7F54
void CGrenchler::DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                                 float dt) {
  switch (type) {
  case kUE_Projectile:
    switch (mCurrentAction) {
    case kGA_BurstAttack:
      ApplyBurstDamage(mgr);
      break;
    case kGA_BiteAttack:
      ApplyBiteDamage(mgr);
      break;
    }
    return;
  case kUE_AlignTargetPos:
    switch (mCurrentAction) {
    case kGA_BeamAttack:
      UpdateBeamTarget(mgr);
      break;
    }
    break;
  case kUE_EffectOn:
    switch (mCurrentAction) {
    case kGA_ShakeOff:
      CreateGrappleHitVisorEffect(mgr);
      break;
    case kGA_BeamAttack:
      StartElectricBeam(mgr);
      break;
    case kGA_GrappleLoop:
    case kGA_GrapplePull:
    case kGA_GrappleStruggle:
      CreateGrappleBeam(mgr);
      break;
    }
    return;
  case kUE_EffectOff:
    switch (mCurrentAction) {
    case kGA_BeamAttack:
      StopElectricBeam(mgr);
      break;
    case kGA_GrappleLoop:
    case kGA_GrappleAbort:
    case kGA_GrappleBreak:
    case kGA_GrapplePull:
    case kGA_GrappleSlide:
    case kGA_GrappleStruggle:
      DestroyGrappleBeam(mgr);
      break;
    case kGA_MorphballBite:
      DestroyGrappleBeam(mgr);
      BeginMorphballCapture(mgr);
      break;
    }
    return;
  case kUE_EventStart:
    switch (mCurrentAction) {
    case kGA_MorphballBite:
      GrabPlayer(mgr);
      break;
    }
    return;
  case kUE_EventStop:
    if (mPlayerGrabbed == 1) {
      EndMorphballCapture(mgr);
      ReleasePlayer(mgr);
    }
    return;
  case kUE_EndAction:
    switch (mCurrentAction) {
    case kGA_GrappleAbort:
    case kGA_GrappleBreak:
    case kGA_GrappleStruggle:
      mEffectB.x0_1_ = true;
      break;
    case kGA_GrappleSlide:
      mSlideOver = true;
      break;
    }
    return;
  case kUE_BecomeShootThrough:
    for (uint i = 0; i < mCollisionManager->GetNumCollisionActors(); ++i) {
      const TUniqueId id = mCollisionManager->GetCollisionDescFromIndex(i).GetCollisionActorId();
      CCollisionActor* collisionActor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id));
      if (collisionActor != nullptr) {
        collisionActor->AddMaterial(kMT_ProjectilePassthrough, mgr);
      }
    }
    CPatterned::DoUserAnimEvent(mgr, node, type, dt);
    return;
  case kUE_EggLay:
    switch (mCurrentAction) {
    case kGA_GrappleStruggle:
      ++mEffectB.mStruggleCount;
      break;
    }
    return;
  case kUE_FadeIn:
    mFadeActive = true;
    return;
  case kUE_FadeOut:
    mFadeActive = false;
    AddMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
    StartHitReaction(false);
    return;
  case kUE_ObjectDrop:
    ApplyScreenShake(mgr, GetTranslation(), kInvalidUniqueId);
    return;
  case kUE_ObjectPickUp:
    return;
  case kUE_Landing:
    mReflectInfo.mLanded = true;
    break;
  case kUE_Unknown46:
    if (!IsDeeplySubmerged(mgr, *this)) {
      SendScriptMsgs(kSS_InternalState10, mgr, kInvalidUniqueId, kSM_None);
    }
    mReflectInfo.x1c_5_ = true;
    break;
  case kUE_Unknown37:
    if (mIsGrappleGuardian == 1) {
      mSlideStopReady = true;
    }
    break;
  case kUE_Unknown42:
    if (mIsGrappleGuardian == 1) {
      mEffectB.x0_2_ = true;
    }
    break;
  case kUE_Unknown45:
    if (mEffectC.mSwoosh.mGen.get() == nullptr) {
      CreateGrappleBeam(mgr);
    }
    break;
  default:
    break;
  }
  CPatterned::DoUserAnimEvent(mgr, node, type, dt);
}

// @ fn_27_7EB8
void CGrenchler::GrabPlayer(CStateManager& mgr) {
  CPlayer* player = mgr.GetPlayer(0);
  player->Stop();
  player->AttachActorToPlayer(GetUniqueId(), true);
  player->RemoveMaterial(kMT_Solid, mgr);
  player->EnableLeaveMorphBall(false);
  player->GetMorphBall()->DisableHalfPipeStatus();
  mPlayerGrabbed = true;
}

// @ fn_27_7D74
void CGrenchler::ReleasePlayer(CStateManager& mgr) {
  CPlayer* player = mgr.GetPlayer(0);
  if (player->GetAttachedActorId() == GetUniqueId()) {
    player->DetachActorFromPlayer();
  }
  player->AddMaterial(kMT_Solid, mgr);
  player->EnableLeaveMorphBall(true);
  mPlayerGrabbed = false;

  CVector3f direction = GetTransform().GetForward();
  direction.SetZ(0.f);
  direction.Normalize();
  direction *= 40.f;
  direction.SetZ(2.f);
  player->Stop();
  player->ApplyImpulseWR(player->GetMass() * direction, CAxisAngle::Identity());
  player->SetMoveState(NPlayer::kMS_ApplyJump, mgr);
}

// @ fn_27_7BA4
void CGrenchler::CreateGrappleHitVisorEffect(CStateManager& mgr) {
  if (IsPlayerLookingAtMe(mgr)) {
    const CVector3f offset(mgr.GetPlayer(0)->GetTranslation() - GetTranslation());
#ifdef TARGET_PC
    // retail 0x7C20: fcmpo mag, 23; bgt skip (NaN takes the effect arm)
    if (!(offset.Magnitude() > 23.f)) {
      const float nearClip = CHUDBillboardEffect::GetNearClipDistance(mgr, 0);
      const CVector3f scale = CHUDBillboardEffect::GetScaleForPOV(mgr);
      if (mGrappleEffect.mEffect) {
        CHUDBillboardEffect* effect = rs_new CHUDBillboardEffect(
            mGrappleEffect.mEffect, rstl::optional_object_null(), mgr.AllocateUniqueId(), true,
            rstl::string_l("Visor Grenchler Shakeoff Splotches"), nearClip, scale, 0,
            CColor::White(), CVector3f::One(), CVector3f::Zero(), false);
        mgr.AddObject(effect);
      }
    }
#else
    if (offset.Magnitude() <= 23.f) {
      const float nearClip = CHUDBillboardEffect::GetNearClipDistance(mgr, 0);
      const CVector3f scale = CHUDBillboardEffect::GetScaleForPOV(mgr);
      if (mGrappleEffect.mEffect) {
        CHUDBillboardEffect* effect = rs_new CHUDBillboardEffect(
            mGrappleEffect.mEffect, rstl::optional_object_null(), mgr.AllocateUniqueId(), true,
            rstl::string_l("Visor Grenchler Shakeoff Splotches"), nearClip, scale, 0,
            CColor::White(), CVector3f::One(), CVector3f::Zero(), false);
        mgr.AddObject(effect);
      }
    }
#endif
  }
}

// @ fn_27_7A38
void CGrenchler::CreateDamageVisorEffect(CStateManager& mgr) {
  const float nearClip = CHUDBillboardEffect::GetNearClipDistance(mgr, 0);
  const CVector3f scale = CHUDBillboardEffect::GetScaleForPOV(mgr);
  if (mDamageEffect.mEffect) {
    CHUDBillboardEffect* effect = rs_new CHUDBillboardEffect(
        mDamageEffect.mEffect, rstl::optional_object_null(), mgr.AllocateUniqueId(), true,
        rstl::string_l("Visor Electric Contact Damage"), nearClip, scale, 0, CColor::White(),
        CVector3f::One(), CVector3f::Zero(), false);
    mgr.AddObject(effect);
  }
}

// @ fn_27_7848
void CGrenchler::CreateVisorEffect(CStateManager& mgr) {
  if (mEffectB.mVisorEffectAssetId != kInvalidAssetId) {
    RemoveVisorEffect(mgr);
    const float nearClip = CHUDBillboardEffect::GetNearClipDistance(mgr, 0);
    const CVector3f scale = CHUDBillboardEffect::GetScaleForPOV(mgr);
    if (mEffectB.mVisorEffect) {
      mEffectB.mVisorEffectId = mgr.AllocateUniqueId();
      CHUDBillboardEffect* effect = rs_new CHUDBillboardEffect(
          mEffectB.mVisorEffect, rstl::optional_object_null(), mEffectB.mVisorEffectId, true,
          rstl::string_l("Visor Grenchler Grapple Effect"), nearClip, scale, 0, CColor::White(),
          CVector3f::One(), CVector3f::Zero(), false);
      mgr.AddObject(effect);
    }
  }
}

// @ fn_27_77E8
void CGrenchler::RemoveVisorEffect(CStateManager& mgr) {
  if (mEffectB.mVisorEffectId != kInvalidUniqueId) {
    mgr.DeleteObjectRequest(mEffectB.mVisorEffectId);
    mEffectB.mVisorEffectId = kInvalidUniqueId;
  }
}

// @ fn_27_7798
void CGrenchler::UpdateBeamTarget(CStateManager& mgr) {
  mBeamAttack.mTargetPosition = GetPlayerBeamTargetPosition(mgr);
}

// @ fn_27_745C
void CGrenchler::StartElectricBeam(CStateManager& mgr) {
  if (mEffectA.mBeamProjectileId != kInvalidUniqueId) {
    StopElectricBeam(mgr);
  }

  const CVector3f origin = mBeamAttack.mHornXf.GetTranslation();
  const CVector3f target = mBeamAttack.mTargetPosition;
  const float radius = mBeamAttack.mBeamDamage.GetRadius();
  const float length = 2.f * (target - origin).Magnitude();
  const CElectricBeamInfo beamInfo(mEffectA.x10_, length, radius, 20.f, kInvalidAssetId, 0.5f, 0.f);
  CElectricBeamProjectile* beam = rs_new CElectricBeamProjectile(
      mEffectA.x1c_, kWT_AI, beamInfo, CTransform4f::Identity(), kMT_Character,
      mBeamAttack.mBeamDamage, mgr.AllocateUniqueId(), GetCurrentAreaId(), GetUniqueId(), 0);
  mgr.AddObject(beam);
  mEffectA.mBeamProjectileId = beam->GetUniqueId();

  const CTransform4f xf =
      CTransform4f::LookAt(mBeamAttack.mHornXf.GetTranslation(), target, CVector3f::Up());
  beam->SetActive(true);
  beam->Fire(xf, mgr, false);
  mBeamAttack.mSoundHandle =
      PlayCustomSound(mBeamAttack.mHornXf.GetTranslation(), mBeamAttack.mHornXf.GetForward(),
                      mBeamAttack.mSound, false);
}

// @ fn_27_73A8
void CGrenchler::StopElectricBeam(CStateManager& mgr) {
  if (mEffectA.mBeamProjectileId != kInvalidUniqueId) {
    CEntity* beam = mgr.ObjectById(mEffectA.mBeamProjectileId);
    if (beam != nullptr) {
      beam->SetActive(false);
      mgr.DeleteObjectRequest(mEffectA.mBeamProjectileId);
    }
    mEffectA.mBeamProjectileId = kInvalidUniqueId;
    CSfxManager::RemoveEmitter(mBeamAttack.mSoundHandle);
    mBeamAttack.mSoundHandle.Clear();
  }
}

// @ fn_27_6FD0
void CGrenchler::CreateGrappleBeam(CStateManager& mgr) {
  DestroyGrappleBeam(mgr);
  mEffectC.mTargetPosition = GetPlayerTargetPosition(mgr);
  mEffectC.mSwoosh.mGen = rstl::auto_ptr< CElementGen >(
      rs_new CElementGen(gpSimplePool->GetObj(SObjectTag('PART', mEffectC.mSwooshAssetId)),
                         CElementGen::kMOT_Normal, CElementGen::kOSF_One));
  mEffectC.mSwoosh.mGen->SetParticleEmission(true);
  mEffectC.mBeamParticles.mGen = rstl::auto_ptr< CElementGen >(
      rs_new CElementGen(gpSimplePool->GetObj(SObjectTag('PART', mEffectC.mBeamPartAssetId)),
                         CElementGen::kMOT_Normal, CElementGen::kOSF_One));
  mEffectC.mBeamParticles.mGen->SetParticleEmission(false);
  mEffectC.mSoundHandle = PlayCustomSound(mBeamAttack.mHornXf.GetTranslation(),
                                          GetTransform().GetForward(), mEffectC.mSound, true);
}

// @ fn_27_6DD0
void CGrenchler::DestroyGrappleBeam(CStateManager& mgr) {
  mEffectC.mSwoosh.mGen = rstl::auto_ptr< CElementGen >();
  mEffectC.mBeamParticles.mGen = rstl::auto_ptr< CElementGen >();
  RemoveExplosion(mgr);
  RemoveVisorEffect(mgr);
  if (mEffectC.mSoundHandle) {
    CSfxManager::RemoveEmitter(mEffectC.mSoundHandle);
    mEffectC.mSoundHandle.Clear();
  }
}

// @ fn_27_6CA4
void CGrenchler::ApplyBurstDamage(CStateManager& mgr) {
  const CVector3f offset =
      mgr.GetPlayer(0)->GetTranslation() - mBeamAttack.mHornXf.GetTranslation();
  if (offset.Magnitude() < mBurstAttack.mDamageRadius &&
      PlayerSubmerged(mgr, CTriggerData(0)) == true) {
    mgr.ApplyDamage(GetUniqueId(), mgr.GetPlayer(0)->GetUniqueId(), GetUniqueId(),
                    mBurstAttack.mDamage,
                    CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList()),
                    CVector3f::Zero());
  }
}

// @ fn_27_6BB8
void CGrenchler::ApplyBiteDamage(CStateManager& mgr) {
  ++mBiteCount;
  if (InBiteRange(mgr, CTriggerData(0.f)) == true) {
    mgr.ApplyDamage(GetUniqueId(), mgr.GetPlayer(0)->GetUniqueId(), GetUniqueId(),
                    mBiteAttack.mDamage,
                    CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList()),
                    CVector3f::Zero());
  }
}

// @ fn_27_6B78
void CGrenchler::ResetSteering() {
  BodyController()->CommandMgr().SetSteeringBlendMode(kSBM_FullSpeed);
  BodyController()->CommandMgr().SetSteeringSpeedRange(1.f, 1.f);
}

// @ fn_27_6AD0
void CGrenchler::UpdateChargeSteering() {
  CBodyController* controller = BodyController();
  const float runSpeed = controller->GetBodyStateInfo().GetLocomotionSpeed(pas::kLA_Run);
  const float walkSpeed = controller->GetBodyStateInfo().GetLocomotionSpeed(pas::kLA_Walk);
  float ratio = walkSpeed / runSpeed;
  if (mIsGrappleGuardian == true && mTailDestroyed != 1) {
    ratio *= 0.05f;
  }
  BodyController()->CommandMgr().SetSteeringBlendMode(kSBM_FullSpeed);
  BodyController()->CommandMgr().SetSteeringSpeedRange(ratio, ratio);
}

// @ fn_27_67C0
void CGrenchler::Charge(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_Charge, msg);
  switch (msg) {
  case kStateMsg_Activate: {
    mChargeAttack.mSavedSpeed = mSpeed;
    mSpeed = 1.f;
    mChargeAttack.mTargetPosition = GetTargetPosition(mgr);
    SetSubmerged(false);
    ResetSteering();
    const CVector3f target = GetTargetPosition(mgr);
    CVector3f direction = target - GetTranslation();
    direction.SetZ(0.f);
    direction.Normalize();
    const float health = GetHealthInfo()->GetHP();
    mChargeAttack.mTargetPosition = CVector3f::Zero();
    mChargeAttack.mStartPosition = mChargeAttack.mTargetPosition;
    mChargeAttack.mStartHealth = health;
    mChargeAttack.mStartPosition = GetTranslation();
    mChargeAttack.mTargetPosition = target + 1.f * direction;
    MoveToTarget(mgr, dt, mChargeAttack.mTargetPosition);
    break;
  }
  case kStateMsg_Update: {
    CVector3f move;
    if (!PathShagged(mgr, CTriggerData(0.f)) && !mPathFindSearch.IsOver()) {
      mPathFindNavigation.PathFind(mgr, msg, dt, *this);
      move = BodyController()->CommandMgr().GetMoveVector();
    } else {
      move = mSteeringBehaviors.Arrival(*this, mChargeAttack.mTargetPosition, 5.f);
    }
    BodyController()->CommandMgr().DeliverCmd(CBCLocomotionCmd(move, CVector3f::Zero(), 1.f));
    break;
  }
  case kStateMsg_Deactivate:
    mSpeed = mChargeAttack.mSavedSpeed;
    UpdateChargeSteering();
    mChargeAttack.mLastChargeTime = mElapsedTime;
    break;
  }
}

// @ fn_27_6708
bool CGrenchler::ChargeFinished(CStateManager& mgr, const CTriggerData& data) const {
  CVector3f totalOffset = mChargeAttack.mTargetPosition - mChargeAttack.mStartPosition;
  totalOffset.SetZ(0.f);
  CVector3f traveledOffset = GetTranslation() - mChargeAttack.mStartPosition;
  traveledOffset.SetZ(0.f);
  return traveledOffset.Magnitude() > 0.9f * totalOffset.Magnitude();
}

// @ fn_27_66E0
void CGrenchler::TurnToPlayer(CStateManager& mgr, float dt, float turnSpeed) {
  RotateToPoint(mgr.GetPlayer(0)->GetTranslation(), dt, turnSpeed);
}

// @ fn_27_6358
void CGrenchler::IssueDeathBodyCommand(CStateManager& mgr, const CVector3f& direction) {
  if (mSubmerged == true) {
    BodyController()->SetLocomotionType(pas::kLT_Internal9);
    BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_NextState));
  } else if (BodyController()->ShouldPlayDeathAnims()) {
    if (BodyController()->HasBodyState(pas::kAS_Hurled) &&
        BodyController()->GetBodyType() == kBT_Flyer) {
      BodyController()->CommandMgr().DeliverCmd(CBCHurledCmd(-direction, CVector3f::Zero()));
    } else if (BodyController()->HasBodyState(pas::kAS_Fall)) {
      const EScriptObjectState state = IsIngPossessed() ? kSS_DarkXDamage : kSS_XDamage;
      if (!mPendingMassiveDeath || CheckConnectedObject(mgr, state, kSM_None) == kInvalidUniqueId) {
        pas::ESeverity severity = pas::kS_One;
        const float surfaceHeight = GetWaterSurfaceHeight(mgr);
        if (GetTranslation().GetZ() + 0.2f < surfaceHeight) {
          severity = pas::kS_Six;
        }
        BodyController()->CommandMgr().DeliverCmd(CBCKnockDownCmd(-direction, severity));
      }
    }
  }
}

// @ fn_27_6320
float CGrenchler::GetFadeOnDeathTime() const {
  if (mSubmerged == false) {
    return CPatterned::GetFadeOnDeathTime();
  }
  return 8.f;
}

// @ fn_27_6314
bool CGrenchler::ReturnToPatrol(CStateManager& mgr, const CTriggerData& data) const {
  return mReturnToPatrol;
}

// @ fn_27_62A8
bool CGrenchler::PlayerSubmerged(CStateManager& mgr, const CTriggerData& data) const {
  const CPlayer* player = mgr.GetPlayer(0);
  if (player->IsOnGround() == true) {
    mPlayerSubmerged = IsDeeplySubmerged(mgr, *player);
  }
  return mPlayerSubmerged;
}

// @ fn_27_6274
const CScriptTeamAiMgr* CGrenchler::GetTeamAiMgr(const CStateManager& mgr) const {
  return TCastToConstPtr< CScriptTeamAiMgr >(mgr.GetObjectById(mTeamAiMgrId));
}

// @ fn_27_6240
CScriptTeamAiMgr* CGrenchler::GetTeamAiMgr(CStateManager& mgr) const {
  return TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamAiMgrId));
}

// @ fn_27_61B4
void CGrenchler::JoinTeam(CStateManager& mgr) {
  if (mTeamAiMgrId != kInvalidUniqueId) {
    CScriptTeamAiMgr* teamMgr = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamAiMgrId));
    if (teamMgr != nullptr) {
      teamMgr->JoinTeam(*this, CTeamAiRole::kTAR_Melee, CTeamAiRole::kTAR_Projectile,
                        CTeamAiRole::kTAR_Unknown);
      teamMgr->SetPlayerForwardProjectionDistance(5.f);
    }
  }
}

// @ fn_27_6114
void CGrenchler::QuitTeam(CStateManager& mgr) {
  if (mTeamAiMgrId != kInvalidUniqueId) {
    CScriptTeamAiMgr* teamMgr = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamAiMgrId));
    if (teamMgr != nullptr && teamMgr->IsPartOfTeam(GetUniqueId()) == true) {
      teamMgr->QuitTeam(GetUniqueId());
    }
  }
}

// @ fn_27_60B8
CTeamAiRole::ETeamAiRole CGrenchler::GetTeamRole(CStateManager& mgr) const {
  const CScriptTeamAiMgr* teamMgr = GetTeamAiMgr(static_cast< const CStateManager& >(mgr));
  if (teamMgr == nullptr || teamMgr->GetRoleCount() < 2) {
    return CTeamAiRole::kTAR_Initial;
  }
  return teamMgr->GetTeamRole(GetUniqueId());
}

// @ fn_27_5FA8
bool CGrenchler::CanCharge(CStateManager& mgr, const CTriggerData& data) const {
  if (mgr.GetPlayer(0)->GetCurrentAreaId() != GetCurrentAreaId()) {
    return false;
  }
  if (mWaterId != kInvalidUniqueId) {
    const float z = GetTranslation().GetZ();
    if (z < GetWaterSurfaceHeight(mgr)) {
      return false;
    }
  }
  float pause = mChargeAttack.mMinTimeBetweenCharges;
  if (mChargeAttack.mAltPause >= 0.f && !mScanned) {
    pause = mChargeAttack.mAltPause;
  }
  return mChargeAttack.mLastChargeTime + pause > mElapsedTime ? false : HasMeleeRole(mgr);
}

// @ fn_27_5F40
bool CGrenchler::CanBite(CStateManager& mgr, const CTriggerData& data) const {
  return HasMeleeRole(mgr);
}

// @ fn_27_5DDC
bool CGrenchler::CanBeamAttack(CStateManager& mgr, const CTriggerData& data) const {
  if (mIsGrappleGuardian == true) {
    return false;
  }
  if (mSubmerged == true) {
    return false;
  }
  if (IsDeeplySubmerged(mgr, *mgr.GetPlayer(0)) == true) {
    return false;
  }
  if (mWaterId != kInvalidUniqueId) {
    const float z = GetTranslation().GetZ();
    if (z + 1.5f < GetWaterSurfaceHeight(mgr)) {
      return false;
    }
  }
  const CTeamAiRole::ETeamAiRole role = GetTeamRole(mgr);
  if (role == CTeamAiRole::kTAR_Projectile) {
    return true;
  }
  if (mNoPathTime < 2.f) {
    if (role != CTeamAiRole::kTAR_Initial) {
      return false;
    }
    const int maxBeamAttacks = (mTailDestroyed == 1 ? 1 : 0) + 3;
    if (CountAttacks(mAttackHistory, kGA_BeamAttack) >= maxBeamAttacks) {
      return false;
    }
  }
  return true;
}

// @ fn_27_5D40
bool CGrenchler::CanBurstAttack(CStateManager& mgr, const CTriggerData& data) const {
  if (!mSubmerged) {
    return false;
  }
  if (!IsDeeplySubmerged(mgr, *mgr.GetPlayer(0))) {
    return false;
  }
  if (mElapsedTime < mBurstAttack.mNextBurstTime) {
    return false;
  }
  const CTeamAiRole::ETeamAiRole role = GetTeamRole(mgr);
  if (role == CTeamAiRole::kTAR_Projectile) {
    return true;
  }
  return role == CTeamAiRole::kTAR_Initial;
}

// @ fn_27_5AD8
bool CGrenchler::HasClearShotAtPlayer(CStateManager& mgr, float maxAngle) const {
  const CVector3f origin = mBeamAttack.mHornXf.GetTranslation();
  const CVector3f target = GetTargetPosition(mgr) + CVector3f(0.f, 0.f, 0.5f);
  CVector3f direction = target - origin;
  if (direction.CanBeNormalized() == true) {
    direction.Normalize();
    const CVector2f directionXY = CVector2f(direction.GetX(), direction.GetY());
    const CVector2f forwardXY =
        CVector2f(GetTransform().GetForward().GetX(), GetTransform().GetForward().GetY());
    if (CVector2f::GetAngleDiff(forwardXY, directionXY) > maxAngle) {
      return false;
    }
  }
  const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid), CMaterialList(kMT_Player, kMT_CollisionActor));
  return mgr.RayCollideWorld(mBeamAttack.mHornXf.GetTranslation(),
                             target + CVector3f(0.f, 0.f, 0.5f), filter, nullptr);
}

// @ fn_27_5974
bool CGrenchler::IsPointVisibleToPlayer(CStateManager& mgr, const CVector3f& point) const {
  const CVector3f target = GetTargetPosition(mgr) + CVector3f(0.f, 0.f, 0.5f);
  const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid), CMaterialList(kMT_Player, kMT_CollisionActor));
  const CVector3f end = target + CVector3f(0.f, 0.f, 0.5f);
  return mgr.RayCollideWorld(point, end, filter, nullptr);
}

// @ fn_27_590C
bool CGrenchler::ClearLineOfFire(CStateManager& mgr, const CTriggerData& data) const {
  if (IsFacingTarget(mgr, 0.61086524f) == false) {
    return false;
  }
  return HasClearShotAtPlayer(mgr, 0.87266463f);
}

// @ fn_27_5854
float CGrenchler::GetManeuverRangeWeight(float range, const CVector3f& a,
                                         const CVector3f& b) const {
  const float distance = CVector3f(a - b).Magnitude();
  if (distance < range) {
    return distance / range;
  }
  if (distance > 2.f * range) {
    return 0.01f;
  }
  return 1.f - (distance - range) / range;
}

// @ fn_27_5640
CVector3f CGrenchler::GetManeuverTarget(CStateManager& mgr) const {
  const float heightOffset = mgr.Random()->Range(1.f, 2.f);
  float bestScore = 0.f;
  const CScriptAIHint* bestHint = nullptr;
  const CVector3f playerPos = mgr.GetPlayer(0)->GetTranslation();
  CObjectList& list = mgr.ObjectListById(kOL_AiWaypoint);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CScriptAIHint* hint = TCastToConstPtr< CScriptAIHint >(list[i]);
    if (hint != nullptr && hint->GetActive() == true &&
        hint->GetHintType() == CScriptAIHint::kHT_Maneuver &&
        !(CMath::AbsF(hint->GetTranslation().GetZ() - GetTranslation().GetZ()) > 4.1f)) {
      float score = GetManeuverRangeWeight(10.f, hint->GetTranslation(), GetTranslation());
      score *= GetManeuverRangeWeight(12.f, hint->GetTranslation(), playerPos);
      if (score > bestScore) {
        const CVector3f hintPoint = hint->GetTranslation() + CVector3f(0.f, 0.f, heightOffset);
        if (!IsPointVisibleToPlayer(mgr, hintPoint)) {
          score *= 0.1f;
        }
        if (score > bestScore) {
          bestScore = score;
          bestHint = hint;
        }
      }
    }
  }
  if (bestHint == nullptr) {
    return GetTargetPosition(mgr);
  }
  return bestHint->GetTranslation();
}

// @ fn_27_533C
void CGrenchler::Maneuver(CStateManager& mgr, EStateMsg msg, float dt) {
  if (mIsGrappleGuardian == true) {
    return;
  }
  switch (msg) {
  case kStateMsg_Activate: {
    const CVector3f zero = CVector3f::Zero();
    mManeuverTarget = zero;
    mManeuverStartTime = 0.f;
    mDefaultTurnSpeed = BodyController()->GetTurnSpeed();
    if (!HasClearShotAtPlayer(mgr, 1.7453293f)) {
      mManeuverStartTime = mElapsedTime;
      mManeuverTarget = GetManeuverTarget(mgr);
      MoveToTarget(mgr, dt, mManeuverTarget);
      CBodyController* controller = BodyController();
      const float runSpeed = controller->GetBodyStateInfo().GetLocomotionSpeed(pas::kLA_Run);
      const float walkSpeed = controller->GetBodyStateInfo().GetLocomotionSpeed(pas::kLA_Walk);
      const float ratio = walkSpeed / runSpeed;
      const float speed = 0.5f * (1.f + ratio);
      BodyController()->CommandMgr().SetSteeringBlendMode(kSBM_FullSpeed);
      BodyController()->CommandMgr().SetSteeringSpeedRange(speed, speed);
    }
    BodyController()->SetTurnSpeed(2.f * mDefaultTurnSpeed);
    break;
  }
  case kStateMsg_Update: {
    CVector3f move = CVector3f::Zero();
    if (!PathShagged(mgr, CTriggerData(0.f)) && !mPathFindSearch.IsOver()) {
      mPathFindNavigation.PathFind(mgr, msg, dt, *this);
      move = BodyController()->CommandMgr().GetMoveVector();
    } else {
      mManeuverTarget = GetManeuverTarget(mgr);
      MoveToTarget(mgr, dt, mManeuverTarget);
    }
    BodyController()->CommandMgr().DeliverCmd(CBCLocomotionCmd(move, CVector3f::Zero(), 1.f));
    break;
  }
  case kStateMsg_Deactivate:
    BodyController()->CommandMgr().SetSteeringSpeedRange(1.f, 1.f);
    BodyController()->SetTurnSpeed(mDefaultTurnSpeed);
    break;
  }
}

// @ fn_27_52A8
bool CGrenchler::ManeuverDone(CStateManager& mgr, const CTriggerData& data) const {
  if (mIsGrappleGuardian == true) {
    return true;
  }
  if (Stuck(mgr, CTriggerData(0)) == true) {
    return true;
  }
  return HasClearShotAtPlayer(mgr, 1.7453293f);
}

// @ fn_27_520C
bool CGrenchler::CancelManeuvering(CStateManager& mgr, const CTriggerData& data) const {
  if (mElapsedTime > 6.f + mManeuverStartTime) {
    return true;
  }
  return IsDeeplySubmerged(mgr, *mgr.GetPlayer(0)) != IsDeeplySubmerged(mgr, *this);
}

// @ fn_27_512C
void CGrenchler::PushAttackHistory(rstl::reserved_vector< EAction, 3 >& history, EAction attack) {
  if (history.size() == 3) {
    history.erase(history.begin());
  }
  history.push_back(attack);
}

// @ fn_27_50D8
void CGrenchler::SetLastActionAsBeam(CStateManager& mgr, float dt) {
  if (GetLastAttackState() != 1) {
    PushAttackHistory(mAttackHistory, kGA_BeamAttack);
  }
}

// @ fn_27_4EA8
float CGrenchler::GetJumpCost(float weight, const CVector3f& jumpPos, const CVector3f& waypointPos,
                              const CVector3f& targetPos, bool skipEndCheck) const {
  const CVector3f toJump(jumpPos - GetTranslation());
  const float jumpDistance = toJump.Magnitude();
  if (jumpDistance > 5.f) {
    return -1.f;
  }
  const CVector3f toWaypoint = waypointPos - GetTranslation();
  if (toWaypoint.Magnitude() < 5.f) {
    return -1.f;
  }
  const CVector3f waypointToTarget = targetPos - waypointPos;
  const float targetDistance = waypointToTarget.Magnitude();
  if (!skipEndCheck) {
    const CVector3f waypointToJump = jumpPos - waypointPos;
    if (waypointToJump.Magnitude() < 0.9f * targetDistance) {
      return -1.f;
    }
  }
  const CVector3f jumpToWaypoint = waypointPos - jumpPos;
  return jumpToWaypoint.Magnitude() * weight + (jumpDistance + targetDistance);
}

// @ fn_27_4E9C
bool CGrenchler::HasValidJumpTarget(CStateManager&, const CTriggerData&) const {
  return mReflectInfo.mTargetValid;
}

// @ fn_27_4E78
void CGrenchler::PickJumpTarget(CStateManager& mgr, float dt) { FindJumpTarget(mgr, false); }

// @ fn_27_4DD0
void CGrenchler::SReflectInfo::Reset() {
  mApexOffset = 2.f;
  mLaunched = mStartedSubmerged = mWaterCrossed = mTargetValid = mLanded = x1c_5_ = false;
  mLandingPosition = CVector3f::Zero();
  mJumpPosition = mLandingPosition;
}

// @ fn_27_4AA4
bool CGrenchler::FindJumpTarget(CStateManager& mgr, bool ignoreRange) {
  mReflectInfo.Reset();
  mReflectInfo.mLandingPosition = GetTranslation();
  mReflectInfo.mStartedSubmerged = mSubmerged;
  const CVector3f target = GetTargetPosition(mgr);
  const bool playerSubmerged = IsDeeplySubmerged(mgr, *mgr.GetPlayer(0));
  const bool selfSubmerged = IsDeeplySubmerged(mgr, *this);
  float maxCost;
  bool frustrated;
  if (ignoreRange == true) {
    maxCost = 1000.f;
    frustrated = true;
  } else {
    maxCost = (target - GetTranslation()).Magnitude();
    if (selfSubmerged != playerSubmerged) {
      maxCost *= 3.f;
    }
    frustrated = Frustrated(mgr, CTriggerData(0.f));
  }
  float bestCost = 10000.f * maxCost;
  const CScriptAiJumpPoint* bestJumpPoint = nullptr;
  const CScriptWaypoint* bestWaypoint = nullptr;
  CObjectList& list = mgr.ObjectListById(kOL_AiWaypoint);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CScriptAiJumpPoint* jumpPoint = TCastToConstPtr< CScriptAiJumpPoint >(list[i]);
    if (jumpPoint != nullptr && jumpPoint->GetActive() &&
        jumpPoint->GetInUse(kInvalidUniqueId) != true &&
        jumpPoint->GetCurrentAreaId() == GetCurrentAreaId()) {
      const TUniqueId waypointId = jumpPoint->CheckConnectedObject(mgr, kSS_Arrived, kSM_Next);
      if (waypointId != kInvalidUniqueId) {
        const CScriptWaypoint* waypoint =
            TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(waypointId));
        if (waypoint != nullptr && playerSubmerged == IsDeeplySubmerged(mgr, *waypoint)) {
          const float cost = GetJumpCost(0.3f, jumpPoint->GetTranslation(),
                                         waypoint->GetTranslation(), target, frustrated);
          if (!(cost < 0.f || cost > maxCost) && cost < bestCost) {
            bestCost = cost;
            bestJumpPoint = jumpPoint;
            bestWaypoint = waypoint;
          }
        }
      }
    }
  }
  if (bestJumpPoint != nullptr) {
    mReflectInfo.mJumpPosition = bestJumpPoint->GetTranslation();
    mReflectInfo.mApexOffset = bestJumpPoint->GetJumpApex();
    mReflectInfo.mLandingPosition = bestWaypoint->GetTranslation();
    mReflectInfo.mTargetValid = true;
    return true;
  }
  return false;
}

// @ fn_27_4A78
bool CGrenchler::IsNearPath(const CVector3f& pos, float padding) const {
  return mPathFindSearch.NearlyOnPath(pos, padding) == CPathFindSearch::kR_Success;
}

// @ fn_27_49C4
void CGrenchler::ChargeFailed(CStateManager& mgr, EStateMsg msg, float dt) {
  DeliverCommand(msg, pas::kAS_Taunt, CBCTauntCmd(pas::kTT_Zero));
  if (msg == kStateMsg_Deactivate) {
    BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_AbortScripted));
  }
}

// @ fn_27_4978
bool CGrenchler::TookDamage(CStateManager&, const CTriggerData&) const {
  return GetHealthInfo()->GetHP() < mChargeAttack.mStartHealth;
}

// @ fn_27_48C8
bool CGrenchler::JustHit(CStateManager& mgr, const CTriggerData&) const {
  if (!mIsGrappleGuardian) {
    if (0.2f + mLastHitTime > mElapsedTime) {
      if (!IsFacingTarget(mgr, 1.5882496f)) {
        return true;
      }
    }
    return false;
  }
  if (!IsFacingTarget(mgr, 1.675516f)) {
    return true;
  }
  return 0.2f + mLastHitTime > mElapsedTime;
}

// @ fn_27_4870
bool CGrenchler::JustBiteAttacked(CStateManager&, const CTriggerData&) const {
  if (GetLastAttackState() == 2) {
    if (0.2f + mBiteAttack.mLastBiteTime > mElapsedTime) {
      return true;
    }
  }
  return false;
}

// @ fn_27_4800
bool CGrenchler::JustBeamAttacked(CStateManager&, const CTriggerData&) const {
  if (GetLastAttackState() == 1) {
    float interval;
    if (mTailDestroyed == 1) {
      interval = 0.1f;
    } else {
      interval = 0.2f;
    }
    if (mBeamAttack.mLastBeamTime + interval > mElapsedTime) {
      return true;
    }
  }
  return false;
}

// @ fn_27_47A8
bool CGrenchler::JustBurstAttacked(CStateManager&, const CTriggerData&) const {
  if (GetLastAttackState() == 3) {
    if (0.2f + mBurstAttack.mLastBurstTime > mElapsedTime) {
      return true;
    }
  }
  return false;
}

// @ fn_27_46A8
void CGrenchler::TurnToFaceTarget(CStateManager& mgr) {
  CVector3f faceVec = CVector3f::Zero();
  if (!IsFacingTarget(mgr, 0.61086524f)) {
    faceVec = mgr.GetPlayer(0)->GetTranslation() - GetTranslation();
  }
  BodyController()->CommandMgr().DeliverCmd(CBCLocomotionCmd(CVector3f::Zero(), faceVec, 1.f));
}

// @ fn_27_45D4
void CGrenchler::PauseBetweenBites(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    float minPause = mBiteAttack.mMinPause;
    float maxPause = mBiteAttack.mMaxPause;
    if (mTailDestroyed == 1) {
      minPause *= 0.3f;
      maxPause *= 0.3f;
    }
    mPauseEndTime = mElapsedTime + minPause;
    mPauseEndTime += (maxPause - minPause) * mgr.Random()->Float();
  } else if (msg == kStateMsg_Deactivate) {
    mPauseEndTime = 0.f;
    mBiteAttack.mLastBiteTime = -1000.f;
  }
  TurnToFaceTarget(mgr);
}

// @ fn_27_4538
void CGrenchler::PauseBetweenBeams(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    mPauseEndTime = mElapsedTime + mBeamAttack.mMinPause;
    mPauseEndTime += (mBeamAttack.mMaxPause - mBeamAttack.mMinPause) * mgr.Random()->Float();
  } else if (msg == kStateMsg_Deactivate) {
    mPauseEndTime = 0.f;
    mBeamAttack.mLastBeamTime = -1000.f;
  }
  TurnToFaceTarget(mgr);
}

// @ fn_27_4520
bool CGrenchler::PauseOver(CStateManager&, const CTriggerData&) const {
  return mElapsedTime > mPauseEndTime;
}

// @ fn_27_44C8
bool CGrenchler::HasAttackPattern(CStateManager& mgr, const CTriggerData&) const {
  return GetConnectedObject(mgr, kSS_Attack, kSM_Follow) != kInvalidUniqueId;
}

// @ fn_27_44AC
bool CGrenchler::AttackPatternOver(CStateManager&, const CTriggerData&) const {
  return mWaypointNavigation.GetDestination() == kInvalidUniqueId;
}

// @ fn_27_425C
void CGrenchler::FollowAttackPattern(CStateManager& mgr, EStateMsg msg, float dt) {
  bool submerged = IsDeeplySubmerged(mgr);
  if (msg == kStateMsg_Activate || mSubmerged != submerged) {
    SetSubmerged(submerged);
  }
  mWaypointNavigation.Patrol(mgr, msg, dt, *this);
  switch (msg) {
  case kStateMsg_Activate: {
    const TUniqueId destination = GetConnectedObject(mgr, kSS_Attack, kSM_Follow);
    mWaypointNavigation.SetDestination(destination);
    const CScriptWaypoint* waypoint = TCastToPtr< CScriptWaypoint >(mgr.ObjectById(destination));
    if (waypoint != nullptr) {
      const CVector3f delta = waypoint->GetTranslation() - GetTranslation();
      if (CVector3f::Dot(delta, GetTransform().GetForward()) <= 0.f) {
        mWaypointNavigation.SetInPosition(true);
      }
    }
    ResetSteering();
    break;
  }
  case kStateMsg_Update: {
    const CScriptAIWaypoint* waypoint =
        TCastToPtr< CScriptAIWaypoint >(mgr.ObjectById(mWaypointNavigation.GetDestination()));
    if (BodyController()->GetCurrentStateId() == pas::kAS_Jump) {
      bool hasNext = true;
      if (waypoint != nullptr) {
        if (waypoint->CheckConnectedObject(mgr, kSS_Arrived, kSM_Next) != kInvalidUniqueId) {
          hasNext = false;
        }
      }
      if (hasNext) {
        BodyController()->CommandMgr().SetTargetVector(GetTargetPosition(mgr) - GetTranslation());
      }
    }
    mReflectInfo.mLandingPosition = mWaypointNavigation.GetDestinationPosition();
    break;
  }
  case kStateMsg_Deactivate:
    UpdateChargeSteering();
    break;
  }
}

// @ fn_27_41B4
bool CGrenchler::EmergedFromWater(CStateManager& mgr, const CTriggerData&) const {
  if (mSubmerged == true) {
    return false;
  }
  if (mEmergeTime <= 0.f || mEmergeTime > mElapsedTime) {
    return false;
  }
  if (mWaterId == kInvalidUniqueId) {
    return false;
  }
  return GetTranslation().GetZ() > GetWaterSurfaceHeight(mgr) - 0.5f;
}

// @ fn_27_419C
bool CGrenchler::TailIntact(CStateManager&, const CTriggerData&) const {
  return mTailDestroyed != 1;
}

// @ fn_27_4098
void CGrenchler::ShakeOff(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_ShakeOff, msg);
  mEmergeTime = -1000.f;
  mBiteCount = 0;
  DeliverCommand(msg, pas::kAS_Taunt, CBCTauntCmd(pas::kTT_One));
}

// @ fn_27_4050
void CGrenchler::PlayTailDestroyedSound() {
  CSfxManager::SfxStart(mTailDestroyedSound, 127, 64, GetCurrentAreaId().Value(), true, false,
                        CSfxManager::kMedPriority);
}

// @ fn_27_4008
void CGrenchler::PlayTailHitSound() {
  CSfxManager::SfxStart(mTailHitSound, 127, 64, GetCurrentAreaId().Value(), true, false,
                        CSfxManager::kMedPriority);
}

// @ fn_27_4000
bool CGrenchler::CanGrapple(CStateManager&, const CTriggerData&) const { return false; }

// @ fn_27_3EEC
bool CGrenchler::IsNearAvoidHint(CStateManager& mgr) {
  CObjectList& list = mgr.ObjectListById(kOL_AiWaypoint);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    CScriptAIHint* hint = TCastToPtr< CScriptAIHint >(list[i]);
    if (hint != nullptr && hint->GetActive() && hint->GetCurrentAreaId() == GetCurrentAreaId() &&
        hint->GetHintType() == CScriptAIHint::kHT_Unknown12) {
      CVector3f offset = GetTranslation() - hint->GetTranslation();
      offset.SetZ(0.f);
      if (offset.Magnitude() < hint->GetRadius()) {
        return true;
      }
    }
  }
  return false;
}

// @ fn_27_3C1C
bool CGrenchler::IsBeamBlockedByHint(CStateManager& mgr) {
  CVector3f start = mBeamAttack.mHornXf.GetTranslation();
  const CVector3f& end = GetPlayerTargetPosition(mgr);
  const CLineSeg line(start, end);
  CObjectList& list = mgr.ObjectListById(kOL_AiWaypoint);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    CScriptAIHint* hint = TCastToPtr< CScriptAIHint >(list[i]);
    if (hint != nullptr && hint->GetActive() && hint->GetCurrentAreaId() == GetCurrentAreaId() &&
        hint->GetHintType() == CScriptAIHint::kHT_Unknown13) {
      CVector3f direction(end - start);
      if (direction.CanBeNormalized()) {
        const float length = direction.Magnitude();
        direction.Normalize();
        const float radiusSq = hint->GetRadius() * hint->GetRadius();
        for (float dist = 0.f; dist < length; dist += 2.f) {
          const CVector3f point(start.GetX() + direction.GetX() * dist,
                                start.GetY() + direction.GetY() * dist,
                                hint->GetTranslation().GetZ());
          if ((point - hint->GetTranslation()).MagSquared() < radiusSq) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

// @ fn_27_3C14
bool CGrenchler::BreakGrappleLoop(CStateManager&, const CTriggerData&) const { return false; }

// @ fn_27_3B6C
void CGrenchler::PickGrappleSide(CStateManager& mgr) {
  switch (mEffectB.mGrappleSide) {
  case -1:
    if (mgr.Random()->Range(0.f, 1.f) < 0.5f) {
      mEffectB.mGrappleSide = 0;
    } else {
      mEffectB.mGrappleSide = 1;
    }
    break;
  case 0:
    mEffectB.mGrappleSide = 1;
    break;
  case 1:
    mEffectB.mGrappleSide = 0;
    break;
  }
}

// @ fn_27_3944
void CGrenchler::GrappleLoop(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_GrappleLoop, msg);
  switch (msg) {
  case kStateMsg_Activate: {
    mEffectC.mDamageTimer = 0.f;
    mEffectC.x44_ = 0.f;
    mEffectC.mTraceState = 0;
    mEffectC.mHitPosition = CVector3f::Zero();
    mEffectC.mBeamLength = 1.5f;
    mEffectB.x0_0_ = mEffectB.x0_1_ = mEffectB.x0_2_ = false;
    mEffectB.x24_ = 0.f;
    mEffectB.x20_ = 0.f;
    mEffectB.mStuckTime = 0.f;
    mEffectB.mClosestDistance = 0.f;
    mEffectB.mStartHealth = 0.f;
    mEffectB.mStruggleCount = 0;
    mEffectB.x20_ = mgr.Random()->Range(mBeamAttack.mMinPause, mBeamAttack.mMaxPause);
    BodyController()->SetLocomotionType(pas::kLT_Internal10);
    mTrackPlayer = false;
    mCrystalDamage = 0.f;
    PickGrappleSide(mgr);
    break;
  }
  case kStateMsg_Update: {
    if (mEffectC.mSwoosh.mGen.get() == nullptr) {
      TurnToPlayer(mgr, dt, 1.5707964f);
      mEffectB.x24_ += dt;
    } else {
      if (mgr.GetPlayer(0)->GetMorphballTransitionState() == CPlayer::kMS_Morphed) {
        mEffectC.mBeamLength += 50.f * dt;
      } else {
        mEffectC.mBeamLength += 35.f * dt;
      }
      UpdateBeamTrace(mgr);
      if (mEffectC.mBeamLength < 1.f) {
        mEffectB.x24_ += dt;
      }
    }
    break;
  }
  case kStateMsg_Deactivate:
    mEffectB.mLastGrappleTime = mElapsedTime;
    if (mCrystalState > 0) {
      --mCrystalState;
    }
    break;
  }
  BodyController()->CommandMgr().ClearLocomotionCmds();
}

// @ fn_27_32C4
void CGrenchler::UpdateBeamTrace(CStateManager& mgr) {
  static const CMaterialFilter skSolidFilter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid), CMaterialList(kMT_Player, kMT_CollisionActor));

  CVector3f direction = mEffectC.mTargetPosition - mBeamAttack.mHornXf.GetTranslation();
  if (!direction.CanBeNormalized()) {
    direction = GetTransform().GetForward();
  }
  direction.Normalize();

  if (mEffectC.mTraceState != 1) {
    static const CMaterialFilter skPlayerFilter =
        CMaterialFilter::MakeInclude(CMaterialList(kMT_Player));
    rstl::reserved_vector< TUniqueId, 1024 > nearList;
    mgr.BuildNearList(nearList, mBeamAttack.mHornXf.GetTranslation(), direction,
                      mEffectC.mBeamLength, skPlayerFilter, nullptr);
    TUniqueId hitId = kInvalidUniqueId;
    const CRayCastResult result = CGameCollision::RayDynamicIntersection(
        mgr, hitId, mBeamAttack.mHornXf.GetTranslation(), direction, mEffectC.mBeamLength,
        skPlayerFilter, nearList);
    if (hitId != kInvalidUniqueId) {
      mEffectC.mTraceState = 1;
      mEffectC.mHitPosition = GetHornTargetPosition();
      mEffectC.mBeamLength += 3.f;
      CreateVisorEffect(mgr);
      return;
    }
  }

  if (mEffectC.mTraceState == 0) {
    const CRayCastResult staticResult =
        mgr.RayStaticIntersection(mBeamAttack.mHornXf.GetTranslation(), direction,
                                  0.9f * mEffectC.mBeamLength, skSolidFilter);
    if (staticResult.IsValid()) {
      mEffectC.mTraceState = 2;
      mEffectC.mHitPosition = GetHornTargetPosition();
      mEffectC.mBeamLength += 3.f;
      return;
    }

    CVector3f sideA(direction.GetY(), -direction.GetX(), 0.f);
    CVector3f sideB(-direction.GetY(), direction.GetX(), 0.f);
    if (sideA.CanBeNormalized() == true) {
      sideA.Normalize();
    }
    if (sideB.CanBeNormalized() == true) {
      sideB.Normalize();
    }

    CVector3f origins[3] = {CVector3f::Zero(), CVector3f::Zero(), CVector3f::Zero()};
    origins[0] = mBeamAttack.mHornXf.GetTranslation() + sideA * 3.f;
    origins[1] = mBeamAttack.mHornXf.GetTranslation() + sideB * 3.f;
    origins[2] = mBeamAttack.mHornXf.GetTranslation();
    for (int i = 0; i < 3; ++i) {
      rstl::reserved_vector< TUniqueId, 1024 > nearList;
      mgr.BuildNearList(nearList, origins[i], direction, mEffectC.mBeamLength, skSolidFilter,
                        nullptr);
      TUniqueId hitId = kInvalidUniqueId;
      const CRayCastResult result = CGameCollision::RayDynamicIntersection(
          mgr, hitId, origins[i], direction, mEffectC.mBeamLength - 1.f, skSolidFilter, nearList);
      if (hitId != kInvalidUniqueId) {
        if (CActor* actor = TCastToPtr< CActor >(mgr.ObjectById(hitId))) {
          mEffectC.mHitPosition = result.GetPoint();
          mEffectC.mBeamLength += 3.f;
          SpawnBeamHitEffect(mgr);
          const CWeaponMode mode(kWT_PoisonWater1, false, false, false);
          if (actor->GetDamageVulnerability()->GetVulnerability(mode).mEffect ==
              CWeaponTypeVulnerability::kE_Immune) {
            mEffectC.mTraceState = 3;
          } else {
            mEffectC.mTraceState = 2;
          }
          return;
        }
      }
    }
  }
}

// @ fn_27_3048
void CGrenchler::SpawnBeamHitEffect(CStateManager& mgr) {
  if (mEffectA.mExplosionAssetId == kInvalidAssetId) {
    return;
  }

  RemoveExplosion(mgr);
  mEffectA.mExplosionId = mgr.AllocateUniqueId();
  CTransform4f xf(GetTransform());
  xf.SetTranslation(mEffectC.mHitPosition);
  CExplosion* explosion = rs_new CExplosion(
      *mEffectA.mExplosion, mEffectA.mExplosionId,
      CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true),
      rstl::string_l("CollisionEffect"), xf, 0, GetModelData()->GetScale(), CColor::White(), -1);
  mgr.AddObject(explosion);
}

// @ fn_27_2FAC
void CGrenchler::UpdateExplosionHeight(CStateManager& mgr) {
  if (mEffectA.mExplosionId != kInvalidUniqueId) {
    if (CExplosion* explosion = TCastToPtr< CExplosion >(mgr.ObjectById(mEffectA.mExplosionId))) {
      CVector3f position = explosion->GetTranslation();
      position.SetZ(mBeamAttack.mHornXf.GetTranslation().GetZ());
      explosion->SetTranslation(position);
    }
  }
}

// @ fn_27_2F4C
void CGrenchler::RemoveExplosion(CStateManager& mgr) {
  if (mEffectA.mExplosionId != kInvalidUniqueId) {
    mgr.DeleteObjectRequest(mEffectA.mExplosionId);
    mEffectA.mExplosionId = kInvalidUniqueId;
  }
}

// @ fn_27_2F1C
bool CGrenchler::BeamHitWall(CStateManager& mgr, const CTriggerData& data) const {
  if (mEffectC.mBeamLength > 90.f) {
    return true;
  }
  return mEffectC.mTraceState == 2;
}

// @ fn_27_2E08
bool CGrenchler::BeamBadAngle(CStateManager& mgr, const CTriggerData& data) const {
  if (mEffectB.x24_ > 4.f) {
    return true;
  }
  if (mEffectC.mSwoosh.mGen.get() != nullptr) {
    CVector3f toTarget = mEffectC.mTargetPosition - mBeamAttack.mHornXf.GetTranslation();
    if (toTarget.CanBeNormalized() == 1) {
      toTarget.Normalize();
      const CVector2f targetDir = CVector2f(toTarget.GetX(), toTarget.GetY());
      const CVector2f facing =
          CVector2f(GetTransform().GetForward().GetX(), GetTransform().GetForward().GetY());
      return CVector2f::GetAngleDiff(facing, targetDir) > 0.87266463f;
    }
  }
  return false;
}

// @ fn_27_2DF4
bool CGrenchler::BeamHitPlayer(CStateManager& mgr, const CTriggerData& data) const {
  return mEffectC.mTraceState == 1;
}

// @ fn_27_2DE0
bool CGrenchler::BeamHitSticky(CStateManager& mgr, const CTriggerData& data) const {
  return mEffectC.mTraceState == 3;
}

// @ fn_27_2CB4
void CGrenchler::GrappleStruggle(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_GrappleStruggle, msg);
  BodyController()->SetLocomotionType(pas::kLT_Internal11);
  BodyController()->CommandMgr().ClearLocomotionCmds();
  switch (msg) {
  case kStateMsg_Activate:
    mEffectB.mStartHealth = GetHealthInfo()->GetHP();
    mTrackPlayer = false;
    SendScriptMsgs(kSS_Locked, mgr, kInvalidUniqueId, kSM_None);
    mCrystalState = 0;
    break;
  case kStateMsg_Update:
    UpdateBeamTrace(mgr);
    break;
  case kStateMsg_Deactivate:
    SendScriptMsgs(kSS_Unlocked, mgr, kInvalidUniqueId, kSM_None);
    mBiteCount = 0;
    break;
  }
}

// @ fn_27_2A78
void CGrenchler::PullPlayer(float dt, CStateManager& mgr, const CVector3f& targetPos) {
  CPlayer* player = mgr.GetPlayer(0);
  CVector3f facing = player->GetTransform().GetForward();
  facing.SetZ(0.f);
  if (player->IsOnGround() == 1) {
    player->Stop();
    if (facing.CanBeNormalized()) {
      facing.Normalize();
      player->SetTransform(CTransform4f::LookAt(CVector3f::Zero(), facing, CVector3f::Up()));
      player->SetTranslation(targetPos);
    }
  } else {
    player->SetMoveState(NPlayer::kMS_ApplyJump, mgr);
  }
  const CRelAngle maxTurn = CRelAngle::FromDegrees(180.f * dt);
  CVector3f toPlayer = mBeamAttack.mHornXf.GetTranslation() - player->GetAimPosition(mgr, 0.f);
  if (toPlayer.CanBeNormalized() == 1) {
    const CVector3f dir = toPlayer.AsNormalized();
    const CVector3f forward = player->GetTransform().GetForward();
    CQuaternion rotation =
        CQuaternion::LookAt(CUnitVector3f(dir.GetX(), dir.GetY(), dir.GetZ()),
                            CUnitVector3f(forward.GetX(), forward.GetY(), forward.GetZ()), maxTurn);
    player->RotateInOneFrameOR(
        CQuaternion(rotation.GetScalar(),
                    player->GetTransform().TransposeRotate(rotation.GetVector())),
        dt);
  }
}

// @ fn_27_24A8
void CGrenchler::GrapplePull(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_GrapplePull, msg);
  BodyController()->SetLocomotionType(pas::kLT_Internal12);
  BodyController()->CommandMgr().ClearLocomotionCmds();
  CPlayer* player = mgr.GetPlayer(0);
  switch (msg) {
  case kStateMsg_Activate:
    mEffectC.mLastHornPosition = mBeamAttack.mHornXf.GetTranslation();
    mEffectB.mStuckTime = 0.f;
    mEffectB.mClosestDistance =
        (player->GetTranslation() - mBeamAttack.mHornXf.GetTranslation()).Magnitude();
    IncrementAttached(mgr);
    player->SetMoveState(NPlayer::kMS_ApplyJump, mgr);
    mTrackPlayer = false;
    mGrappleCrystalDamage = 0.f;
    mCrystalState = 0;
    break;
  case kStateMsg_Update: {
    const CVector3f pos = mBeamAttack.mHornXf.GetTranslation();
    const float distance = (player->GetTranslation() - pos).Magnitude();
    const float lastDistance = (player->GetTranslation() - mEffectC.mLastHornPosition).Magnitude();
    if (distance + 0.1f < mEffectB.mClosestDistance) {
      mEffectB.mClosestDistance = lastDistance;
      mEffectB.mStuckTime = 0.f;
    } else {
      mEffectB.mStuckTime += dt;
    }
    const float delta = distance - lastDistance;
    CVector3f direction = mBeamAttack.mHornXf.GetTranslation() - player->GetTranslation();
    direction.SetZ(0.f);
    if (direction.CanBeNormalized() == 1) {
      CTransform4f xf = player->GetTransform();
      CVector3f pull = direction.AsNormalized();
      pull *= 1.9f * dt;
      if (distance > lastDistance) {
        CVector3f extra = direction.Normalize() * (1.5f * delta);
        if (player->GetBackwardInput() > 0.1f) {
          extra *= 0.35f;
        }
        xf.AddTranslation(pull);
        xf.AddTranslation(extra);
      } else {
        if (player->GetBackwardInput() > 0.1f) {
          pull *= 0.35f;
        }
        xf.AddTranslation(pull);
      }
      PullPlayer(dt, mgr, xf.GetTranslation());
      mEffectC.mDamageTimer += dt;
      while (mEffectC.mDamageTimer > 0.25f) {
        mEffectC.mDamageTimer -= 0.25f;
        mgr.ApplyDamage(
            GetUniqueId(), mgr.GetPlayer(0)->GetUniqueId(), GetUniqueId(), mEffectC.mDamage,
            CMaterialFilter::MakeIncludeExclude(CMaterialList(sSolidMaterial), CMaterialList()),
            CVector3f::Zero());
      }
    }
    mEffectC.mTargetPosition = GetPlayerTargetPosition(mgr);
    mEffectC.mBeamLength = (GetPlayerTargetPosition(mgr) - pos).Magnitude();
    if (mgr.GetPlayer(0)->GetMorphballTransitionState() != CPlayer::kMS_Morphed) {
      mEffectC.mBeamLength += 1.f;
    }
    mEffectC.mLastHornPosition = pos;
    break;
  }
  case kStateMsg_Deactivate:
    DecrementAttached(mgr);
    BodyController()->SetLocomotionType(pas::kLT_Relaxed);
    mTrackPlayer = true;
    if (mgr.GetPlayer(0)->GetMorphballTransitionState() != CPlayer::kMS_Morphed) {
      DestroyGrappleBeam(mgr);
    }
    break;
  }
}

// @ fn_27_2390
void CGrenchler::SendAttachedMessage(CStateManager& mgr, EScriptObjectMessage msg) {
  rstl::vector< SConnection >::const_iterator it = GetConnectionList().begin();
  for (; it != GetConnectionList().end(); ++it) {
    if (it->state == kSS_Connect && it->msg == kSM_Attach) {
      if (CActor* actor = TCastToPtr< CActor >(mgr.ObjectById(mgr.GetIdForScript(it->objId)))) {
        actor->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), actor->GetUniqueId(), msg));
      }
    }
  }
}

// @ fn_27_2368
void CGrenchler::DecrementAttached(CStateManager& mgr) { SendAttachedMessage(mgr, kSM_Decrement); }

// @ fn_27_2340
void CGrenchler::IncrementAttached(CStateManager& mgr) { SendAttachedMessage(mgr, kSM_Increment); }

// @ fn_27_2298
void CGrenchler::GrappleBreak(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_GrappleBreak, msg);
  BodyController()->SetLocomotionType(pas::kLT_Relaxed);
  if (msg == kStateMsg_Activate && mEffectC.mTraceState == 2) {
    DestroyGrappleBeam(mgr);
  }
  if (msg == kStateMsg_Deactivate) {
    mTrackPlayer = true;
    DestroyGrappleBeam(mgr);
    mBiteCount = 0;
    mEffectB.mLastGrappleTime = mElapsedTime;
  }
}

// @ fn_27_21F4
void CGrenchler::GrappleAbort(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_GrappleAbort, msg);
  BodyController()->SetLocomotionType(pas::kLT_Relaxed);
  if (msg == kStateMsg_Activate) {
    DestroyGrappleBeam(mgr);
    mTrackPlayer = false;
  } else if (msg == kStateMsg_Deactivate) {
    mTrackPlayer = true;
    mBiteCount = 0;
    mEffectB.mLastGrappleTime = mElapsedTime;
  }
}

// @ fn_27_2188
bool CGrenchler::StopStruggling(CStateManager& mgr, const CTriggerData& data) const {
  if (mEffectB.mStruggleCount >= mEffectB.mStruggleLimit) {
    return true;
  }
  return mEffectB.x10_ + GetHealthInfo()->GetHP() < mEffectB.mStartHealth;
}

// @ fn_27_2144
bool CGrenchler::StruggleOver(CStateManager& mgr, const CTriggerData& data) const {
  if (mEffectB.x0_1_ == 1) {
    return true;
  }
  return TookKnockback(mgr, data);
}

// @ fn_27_212C
bool CGrenchler::TookKnockback(CStateManager& mgr, const CTriggerData& data) const {
  return GetBodyController()->GetCurrentStateId() == pas::kAS_KnockBack;
}

// @ fn_27_20A0
void CGrenchler::TakeDamage(const CVector3f& direction, float magnitude) {
  mDamageCooldownTimer = skDamageHitTime;
  if (!mIsGrappleGuardian && mTailDestroyedTime + 2.f > mElapsedTime) {
    HealthInfo()->SetHP(GetTailHealth());
  }
}

// @ fn_27_2084
bool CGrenchler::Frustrated(CStateManager& mgr, const CTriggerData& data) const {
  return mPathBlockedTime > 1.f;
}

// @ fn_27_1FAC
void CGrenchler::UpdateDamageFlash(float dt) {
  if (mDamageFlashTimer > 0.f) {
    mDamageFlashTimer = rstl::max_val(mDamageFlashTimer - dt, 0.f);
    const float t = CMath::Min(mDamageFlashTimer / skDamageHitTime, 1.f);
    const CColor& color = CColor::Lerp(CColor::Black(), CColor::Yellow(), t);
    mColor.SetRed(color.GetRedu8());
    mColor.SetGreen(color.GetGreenu8());
    mColor.SetBlue(color.GetBlueu8());
  }
}

// @ fn_27_1F80
bool CGrenchler::CrystalDamaged(CStateManager& mgr, const CTriggerData& data) const {
  if (mEffectB.x0_2_ != 1) {
    return false;
  }
  return mGrappleCrystalDamage > mGrappleCrystalThreshold;
}

// @ fn_27_1F64
bool CGrenchler::PlayerStuck(CStateManager& mgr, const CTriggerData& data) const {
  return mEffectB.mStuckTime > 1.5f;
}

// @ fn_27_1F44
bool CGrenchler::Stuck(CStateManager& mgr, const CTriggerData& data) const {
  return CPatterned::Stuck(mgr, data);
}

// @ fn_27_1F18
bool CGrenchler::GrapplingMorphball(CStateManager& mgr, const CTriggerData& data) const {
  if (mCurrentAction != 8) {
    return false;
  }
  return mgr.GetPlayer(0)->GetMorphballTransitionState() == CPlayer::kMS_Morphed;
}

// @ fn_27_1CE4
void CGrenchler::MorphballBite(CStateManager& mgr, EStateMsg msg, float dt) {
  SetAttackState(kGA_MorphballBite, msg);
  SetSubmerged(false);
  CPlayer* player = mgr.GetPlayer(0);
  switch (msg) {
  case kStateMsg_Activate:
    mTrackPlayer = false;
    mPlayerGrabbed = false;
    mPlayerInMouth = false;
    mCrystalState = 0;
    player->EnableLeaveMorphBall(false);
    break;
  case kStateMsg_Update:
    if (mPlayerGrabbed == 1) {
      UpdateCapturedPlayer(mgr);
    }
    break;
  case kStateMsg_Deactivate:
    EndMorphballCapture(mgr);
    mTrackPlayer = true;
    mBiteCount = 0;
    mEffectB.mLastGrappleTime = mElapsedTime;
    player->EnableLeaveMorphBall(true);
    break;
  }
  DeliverCommand(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_Six));
}

// @ fn_27_1BD0
void CGrenchler::UpdateCapturedPlayer(CStateManager& mgr) {
  CPlayer* player = mgr.GetPlayer(0);
  if (!mPlayerInMouth) {
    const CVector3f mouth = mBeamAttack.mHornXf.GetTranslation();
    const CVector3f anchor = mAttachXf.GetTranslation();
    if ((mouth - anchor).MagSquared() < (mouth - player->GetTranslation()).MagSquared()) {
      mPlayerInMouth = true;
    }
  }
  if (mPlayerInMouth) {
    CTransform4f xf = mAttachXf;
    xf.AddTranslationX(0.f);
    xf.AddTranslationY(0.f);
    xf.AddTranslationZ(-0.5f);
    player->Teleport(xf, mgr, false);
  }
}

// @ fn_27_1B74
void CGrenchler::BeginMorphballCapture(CStateManager& mgr) {
  if (mCaptureActive != 1) {
    mCaptureActive = true;
    SendScriptMsgs(kSS_InternalState0, mgr, GetUniqueId(), kSM_None);
  }
}

// @ fn_27_1B1C
void CGrenchler::EndMorphballCapture(CStateManager& mgr) {
  if (mCaptureActive) {
    mCaptureActive = false;
    SendScriptMsgs(kSS_InternalState1, mgr, GetUniqueId(), kSM_None);
  }
}

// @ fn_27_1A68
CDamageInfo CGrenchler::GetContactDamage() const {
  if (mIsGrappleGuardian == 1 && GetAlive() == 1 && mTailDestroyed == 1) {
    return mDamageEffect.mDamage;
  }
  return CPatterned::GetContactDamage();
}

// @ fn_27_1A2C
void CGrenchler::OnScanStateChange(EScanState state, CStateManager& mgr) {
  switch (state) {
  case kSS_Done:
    mScanned = true;
    break;
  default:
    break;
  }
  CActor::OnScanStateChange(state, mgr);
}

// @ fn_27_177C
void CGrenchler::PreRenderAllViewports(CStateManager& mgr) {
  CPatterned::PreRenderAllViewports(mgr);
  if (mIsGrappleGuardian == 1 && mCollisionManager.get()) {
    mGuardianBounds = CAABox::MakeMaxInvertedBox();
    for (uint i = 0; i < 10; ++i) {
      if (skCollisionJoints[i].mAffectsBounds) {
        const CJointCollisionDescription& desc = mCollisionManager->GetCollisionDescFromIndex(i);
        if (const CCollisionActor* actor =
                TCastToConstPtr< CCollisionActor >(mgr.GetObjectById(desc.GetCollisionActorId()))) {
          CAABox bounds = *actor->GetTouchBounds();
          CVector3f halfExtent = bounds.GetMaxPoint() - bounds.GetCenterPoint();
          halfExtent *= 0.85f;
          CVector3f max = bounds.GetCenterPoint() + halfExtent;
          CVector3f min = bounds.GetCenterPoint() - halfExtent;
          mGuardianBounds->Include(CAABox(min, max));
        }
      }
    }
  }
}

// @ fn_27_1700
CAABox CGrenchler::GetSortingBounds(const CStateManager& mgr) const {
  if (!mIsGrappleGuardian || !mGuardianBounds) {
    return CActor::GetSortingBounds(mgr);
  }
  return *mGuardianBounds;
}

// @ fn_27_0168
CEntity* LoadGrenchler(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrGrenchler sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrGrenchler.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }

  const ushort tailDestroyedSound = sldrThis.tailDestroyedSound == -1
                                        ? CSfxManager::kInternalInvalidSfxId
                                        : sldrThis.tailDestroyedSound;
  const ushort tailHitSound =
      sldrThis.tailHitSound == -1 ? CSfxManager::kInternalInvalidSfxId : sldrThis.tailHitSound;
  return rs_new CGrenchler(
      mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
      LdrToEntityInfo(info, sldrThis.editorProperties), LdrToTransform4f(sldrThis.editorProperties),
      *modelData, LdrToPatternedInfo(sldrThis.patterned, &sldrThis.ingPossessionData),
      sldrThis.patterned.stateMachine2, sldrThis.tailDestroyedHealth,
      sldrThis.minTimeBetweenCharges, sldrThis.unknown_0x7bd1a35f, sldrThis.chargeAttackMinRange,
      sldrThis.chargeAttackMaxRange, sldrThis.biteAttackMinRange, sldrThis.biteAttackMaxRange,
      sldrThis.biteAttackMinPause, sldrThis.isGrappleGuardian, sldrThis.hasHealthBar,
      LdrToDamageVulnerability(sldrThis.damageVulnerability), sldrThis.tail.ancs,
      sldrThis.tail.character_index, sldrThis.tail.initial_anim,
      sldrThis.tailWhenUnderwater.initial_anim, sldrThis.taillessModel, sldrThis.taillessSkinRules,
      sldrThis.tail_Dark.ancs, sldrThis.tail_Dark.character_index, sldrThis.tail_Dark.initial_anim,
#ifdef TARGET_PC
      // Retail passes the light underwater anim twice: 0xCB8 `lwz r26, 0xa08(r1)` (tailWhenUnderwater
      // .initial_anim) is stored at 0xD84 (`stw r26, 0x20(r1)`, the tailWhenUnderwater argument) and again
      // at 0xDB0 (`stw r26, 0x38(r1)`, the tailWhenUnderwaterDark argument); 0xa28 (tailWhenUnderwater_Dark
      // .initial_anim) is never read.
      sldrThis.tailWhenUnderwater.initial_anim, sldrThis.taillessModel_Dark,
#else
      sldrThis.tailWhenUnderwater_Dark.initial_anim, sldrThis.taillessModel_Dark,
#endif
      sldrThis.taillessSkinRules_Dark, tailHitSound, tailDestroyedSound,
      sldrThis.biteAttackMaxPause, sldrThis.biteAttackDamageRadius,
      LdrToDamageInfo(sldrThis.biteDamage), LdrToDamageInfo(sldrThis.beamDamage),
      sldrThis.beamAttackMinRange, sldrThis.beamAttackMaxRange, sldrThis.beamAttackMinPause,
      sldrThis.beamAttackMaxPause, sldrThis.beamAttackMaxAngle, sldrThis.beamAttackSound_OneShot,
      LdrToDamageInfo(sldrThis.burstDamage), sldrThis.burstProjectile, sldrThis.burstAttackMinRange,
      sldrThis.burstAttackMaxRange, sldrThis.burstAttackMinPause, sldrThis.burstAttackMaxPause,
      sldrThis.burstAttackDamageRadius, sldrThis.surfaceRingsEffect, sldrThis.shallowWaterRing,
      sldrThis.electricEffect, sldrThis.pART, sldrThis.grappleSwoosh, sldrThis.grappleBeamPart,
      sldrThis.grappleHitFx, LdrToDamageInfo(sldrThis.grappleDamage),
      sldrThis.grappleBeamSound_Loop, sldrThis.beamEffect, sldrThis.unknown_0xd4753ff4,
      sldrThis.unknown_0x05fc6001, sldrThis.unknown_0x13e5b580, sldrThis.unknown_0xfc6f199d,
      sldrThis.grappleVisorEffect, LdrToDamageInfo(sldrThis.damageInfo), sldrThis.pART_0x54b6bfa1,
      sldrThis.audioPlaybackParms, sldrThis.grappleGuardianEyeGlow, sldrThis.alternateScannableInfo,
      LdrToActorParameters(sldrThis.actorInformation));
}

#ifdef TARGET_PC
// The header declares these overloads (const and non-template variants) but upstream only
// defines the sibling ones, so forward to those.
void CGrenchler::DeliverCommand(EStateMsg msg, pas::EAnimationState state,
                                const CBCMeleeAttackCmd& cmd) {
  DeliverCommand< CBCMeleeAttackCmd >(msg, state, cmd);
}

CVector3f CGrenchler::GetPlayerTargetPosition(CStateManager& mgr) {
  return static_cast< const CGrenchler* >(this)->GetPlayerTargetPosition(mgr);
}

float CGrenchler::GetWaterSurfaceHeight(const CStateManager& mgr) const {
  return const_cast< CGrenchler* >(this)->GetWaterSurfaceHeight(const_cast< CStateManager& >(mgr));
}

bool CGrenchler::IsNearAvoidHint(CStateManager& mgr) const {
  return const_cast< CGrenchler* >(this)->IsNearAvoidHint(mgr);
}

// Host: the console's `var <id> gren.<field> [value]`. gren.scanned is the scan-done flag
// OnScanStateChange sets (a guardian's crystals take damage only once scanned), gren.crystal the
// crystal state (4 = ForceGrapple), gren.crystaldmg / gren.grappledmg the two crystal damage sums
// (whole units), gren.tailhp the health the tail breaks at, gren.action the attack state (read),
// gren.tail the tail-destroyed flag: setting it to 1 runs DestroyTail, as a breaking hit does.
bool CGrenchler::PortConsoleVar(CEntity& ent, const char* name, const int* value, int& current) {
  if (strncmp(name, "gren.", 5) != 0) {
    return false;
  }
  CGrenchler* gren = dynamic_cast< CGrenchler* >(&ent); // a host-only helper
  if (gren == nullptr) {
    return false;
  }
  if (strcmp(name, "gren.scanned") == 0) {
    if (value != nullptr) {
      gren->mScanned = *value != 0;
    }
    current = gren->mScanned ? 1 : 0;
    return true;
  }
  if (strcmp(name, "gren.action") == 0) {
    current = static_cast< int >(gren->mCurrentAction);
    return true;
  }
  if (strcmp(name, "gren.tail") == 0) {
    CStateManager* mgr = PortConsole::VarManager();
    if (value != nullptr && *value == 1 && gren->mTailDestroyed != 1 && mgr != nullptr) {
      gren->DestroyTail(*mgr);
    }
    current = gren->mTailDestroyed;
    return true;
  }
  if (strcmp(name, "gren.crystal") == 0) {
    if (value != nullptr) {
      gren->mCrystalState = *value;
    }
    current = gren->mCrystalState;
    return true;
  }
  // gren4 coverage vars: the attack-state fields a body branches on that the machine seldom moves.
  // gren.submerged mSubmerged, gren.side the grapple side, gren.trace the beam trace state,
  // gren.hitzero puts the grapple hit point on the horn (no slide direction), gren.attach attaches
  // the player to this Grenchler as GrabPlayer does (value 0 runs ReleasePlayer), gren.slideold backdates the
  // slide start by 100 s, gren.elapsed (read) the attack clock.
  // gren5 coverage vars. gren.hint <n> adds a CScriptAIHint at this Grenchler (1 type 12 radius 10,
  // 2 type 12 far away, 3 type 12 inactive, 4 type 12 in another area, 5 type 7, 6 type 13 radius 10,
  // 7 type 13 far away, 8 a cover point, which is no hint); gren.hintcall <n> runs 1 IsNearAvoidHint, 2 IsBeamBlockedByHint, 3 the same
  // with the horn put on the player target (no beam direction) and reads the result;
  // gren.jumpcost <n> runs GetJumpCost on case n (1 jump too far, 2 waypoint too near, 3 end check
  // fails, 4 the same skipping it, 5 end check passes) and reads 100 * the cost.
  if (strcmp(name, "gren.hint") == 0) {
    CStateManager* mgr = PortConsole::VarManager();
    if (value != nullptr && mgr != nullptr) {
      const int kind = *value;
      if (kind == 8) {
        mgr->AddObject(rs_new CScriptCoverPoint(
            mgr->AllocateUniqueId(), rstl::string_l("gren5 cover"),
            CEntityInfo(gren->GetCurrentAreaId(), CEntity::NullConnectionList, true),
            gren->GetTransform(), 0, false, 0.f, 0.f, 0.f));
        current = 0;
        return true;
      }
      const bool far = kind == 2 || kind == 7;
      const int type = (kind == 5) ? 7 : (kind >= 6 ? 13 : 12);
      const TAreaId area =
          kind == 4 ? TAreaId(gren->GetCurrentAreaId().Value() + 1) : gren->GetCurrentAreaId();
      const CTransform4f xf = CTransform4f::Translate(
          gren->GetTranslation() + (far ? CVector3f(1000.f, 0.f, 0.f) : CVector3f::Zero()));
      CScriptAIHint* hint = rs_new CScriptAIHint(
          mgr->AllocateUniqueId(), rstl::string_l("gren5 hint"),
          CEntityInfo(area, CEntity::NullConnectionList, kind != 3), xf,
          static_cast< CScriptAIHint::EHintType >(type), far ? 1.f : 10.f, 0.f, 0.f, 0.f);
      mgr->AddObject(hint);
    }
    current = 0;
    return true;
  }
  // gren3: gren.mhint <n> adds a maneuver hint (type 20): 1 midway between this Grenchler and the player,
  // 2 ten up (z test fails), 3 inactive, 4 1000 away, 5 four below (behind the floor), 6 a type 12 hint
  // midway (not a maneuver); gren.fncall 20 then reads GetManeuverTarget.
  if (strcmp(name, "gren.mhint") == 0) {
    CStateManager* mgr = PortConsole::VarManager();
    if (value != nullptr && mgr != nullptr) {
      const int kind = *value;
      CVector3f pos = (gren->GetTranslation() + mgr->GetPlayer(0)->GetTranslation()) * 0.5f;
      pos.SetZ(gren->GetTranslation().GetZ());
      if (kind == 2) {
        pos.SetZ(pos.GetZ() + 10.f);
      } else if (kind == 4) {
        pos = gren->GetTranslation() + CVector3f(1000.f, 0.f, 0.f);
      } else if (kind == 5) {
        pos.SetZ(pos.GetZ() - 4.f);
      }
      mgr->AddObject(rs_new CScriptAIHint(
          mgr->AllocateUniqueId(), rstl::string_l("gren3 hint"),
          CEntityInfo(gren->GetCurrentAreaId(), CEntity::NullConnectionList, kind != 3),
          CTransform4f::Translate(pos),
          kind == 6 ? CScriptAIHint::kHT_Unknown12 : CScriptAIHint::kHT_Maneuver, 10.f, 0.f, 0.f,
          0.f));
    }
    current = 0;
    return true;
  }
  // gren.jp <n> adds a jump point at this Grenchler: 1 inactive, 2 in the next area, 3 active with no
  // waypoint; gren.fncall 23 runs FindJumpPoint toward an unreachable point, 24 once more excluding the
  // jump point the plain call chose.
  if (strcmp(name, "gren.jp") == 0) {
    CStateManager* mgr = PortConsole::VarManager();
    if (value != nullptr && mgr != nullptr) {
      const TAreaId area =
          *value == 2 ? TAreaId(gren->GetCurrentAreaId().Value() + 1) : gren->GetCurrentAreaId();
      mgr->AddObject(rs_new CScriptAiJumpPoint(
          mgr->AllocateUniqueId(), rstl::string_l("gren3 jump"),
          CEntityInfo(area, CEntity::NullConnectionList, *value != 1),
          CTransform4f::Translate(gren->GetTranslation()), 5.f, 0));
    }
    current = 0;
    return true;
  }
  if (strcmp(name, "gren.hintcall") == 0) {
    CStateManager* mgr = PortConsole::VarManager();
    current = -1;
    if (value != nullptr && mgr != nullptr) {
      if (*value == 1) {
        current = gren->IsNearAvoidHint(*mgr) ? 1 : 0;
      } else if (*value == 2 || *value == 3) {
        if (*value == 3) {
          gren->mBeamAttack.mHornXf.SetTranslation(
              gren->GetPlayerTargetPosition(*mgr));
        }
        current = gren->IsBeamBlockedByHint(*mgr) ? 1 : 0;
      }
    }
    return true;
  }
  if (strcmp(name, "gren.jumpcost") == 0) {
    current = -1;
    if (value != nullptr) {
      const CVector3f t = gren->GetTranslation();
      float cost = 0.f;
      switch (*value) {
      case 1:
        cost = gren->GetJumpCost(0.3f, t + CVector3f(6.f, 0.f, 0.f), t + CVector3f(10.f, 0.f, 0.f),
                                 t + CVector3f(20.f, 0.f, 0.f), false);
        break;
      case 2:
        cost = gren->GetJumpCost(0.3f, t + CVector3f(1.f, 0.f, 0.f), t + CVector3f(2.f, 0.f, 0.f),
                                 t + CVector3f(20.f, 0.f, 0.f), false);
        break;
      case 3:
      case 4:
        cost = gren->GetJumpCost(0.3f, t + CVector3f(0.f, 1.f, 0.f), t + CVector3f(10.f, 0.f, 0.f),
                                 t + CVector3f(30.f, 0.f, 0.f), *value == 4);
        break;
      default:
        cost = gren->GetJumpCost(0.3f, t + CVector3f(4.f, 0.f, 0.f), t + CVector3f(10.f, 0.f, 0.f),
                                 t + CVector3f(11.f, 0.f, 0.f), false);
        break;
      }
      current = static_cast< int >(cost * 100.f);
    }
    return true;
  }
  // gren.slidestart <v> puts the slide start v seconds after now; gren.hitzero 2 puts the hit point
  // 5 m from the horn; gren.swoosh <n> builds the grapple beam's generators (n 1 target as the
  // player, 2 on the horn, 3 straight ahead, 4 straight behind) or drops them (0);
  // gren.contact <n> reads 100 * the contact damage on case n (0 not a guardian, 1 guardian with
  // the tail gone, 2 tail kept, 3 dead); gren.path <n> reads 2 * PathShagged + IsOver after
  // n = 1 searching to the player, 2 advancing a waypoint, 3 searching a point out of reach,
  // 4 searching a point 3 m off.
  if (strcmp(name, "gren.slidestart") == 0) {
    if (value != nullptr) {
      gren->mSlideStartTime = gren->mElapsedTime + static_cast< float >(*value);
    }
    current = 0;
    return true;
  }
  if (strcmp(name, "gren.swoosh") == 0) {
    CStateManager* mgr = PortConsole::VarManager();
    if (value != nullptr && mgr != nullptr) {
      if (*value == 0) {
        gren->DestroyGrappleBeam(*mgr);
      } else {
        gren->CreateGrappleBeam(*mgr);
        const CVector3f horn = gren->mBeamAttack.mHornXf.GetTranslation();
        const CVector3f fwd = gren->GetTransform().GetForward();
        if (*value == 2) {
          gren->mEffectC.mTargetPosition = horn;
        } else if (*value == 3) {
          gren->mEffectC.mTargetPosition = horn + 10.f * fwd;
        } else if (*value == 4) {
          gren->mEffectC.mTargetPosition = horn - 10.f * fwd;
        }
      }
    }
    current = gren->mEffectC.mSwoosh.mGen.get() != nullptr ? 1 : 0;
    return true;
  }
  if (strcmp(name, "gren.contact") == 0) {
    current = -1;
    if (value != nullptr) {
      const bool guardian = gren->mIsGrappleGuardian;
      const int tail = gren->mTailDestroyed;
      const uint alive = gren->mAlive;
      gren->mIsGrappleGuardian = *value != 0;
      gren->mTailDestroyed = *value == 2 ? 0 : 1;
      gren->mAlive = *value != 3;
      current = static_cast< int >(gren->GetContactDamage().GetDamage() * 100.f);
      gren->mIsGrappleGuardian = guardian;
      gren->mTailDestroyed = tail;
      gren->mAlive = alive;
    }
    return true;
  }
  if (strcmp(name, "gren.path") == 0) {
    CStateManager* mgr = PortConsole::VarManager();
    if (value != nullptr && mgr != nullptr) {
      if (*value == 1) {
        gren->mPathFindSearch.Search(gren->GetTranslation(),
                                     gren->GetPlayerTargetPosition(*mgr));
      } else if (*value == 2) {
        gren->mPathFindSearch.Advance();
      } else if (*value == 3) {
        gren->mPathFindSearch.Search(gren->GetTranslation(), CVector3f(1.0e6f, 1.0e6f, 1.0e6f));
      } else if (*value == 4) {
        gren->mPathFindSearch.Search(gren->GetTranslation(),
                                     gren->GetTranslation() + CVector3f(3.f, 0.f, 0.f));
      }
    }
    current = (gren->mPathFindSearch.IsShagged() ? 2 : 0) + (gren->mPathFindSearch.IsOver() ? 1 : 0);
    return true;
  }
  if (strcmp(name, "gren.submerged") == 0) {
    if (value != nullptr) {
      gren->mSubmerged = *value != 0;
    }
    current = gren->mSubmerged ? 1 : 0;
    return true;
  }
  if (strcmp(name, "gren.side") == 0) {
    if (value != nullptr) {
      gren->mEffectB.mGrappleSide = *value;
    }
    current = gren->mEffectB.mGrappleSide;
    return true;
  }
  if (strcmp(name, "gren.trace") == 0) {
    if (value != nullptr) {
      gren->mEffectC.mTraceState = *value;
    }
    current = gren->mEffectC.mTraceState;
    return true;
  }
  if (strcmp(name, "gren.hitzero") == 0) {
    if (value != nullptr && *value != 0) {
      gren->mEffectC.mHitPosition = gren->mBeamAttack.mHornXf.GetTranslation() +
                                    (*value == 2 ? CVector3f(5.f, 0.f, 0.f) : CVector3f::Zero());
    }
    current = 0;
    return true;
  }
  if (strcmp(name, "gren.slideold") == 0) {
    if (value != nullptr && *value != 0) {
      gren->mSlideStartTime = gren->mElapsedTime - 100.f;
    }
    current = 0;
    return true;
  }
  if (strcmp(name, "gren.elapsed") == 0) {
    current = static_cast< int >(gren->mElapsedTime);
    return true;
  }
  if (strcmp(name, "gren.struggle") == 0) {
    // struggle bookkeeping StopStruggling/StruggleOver read: <value> 0 count, 1 limit, 2 over flag
    // (x0_1_), 3 start health +1000 (so the health arm of StopStruggling fires)
    if (value != nullptr) {
      switch (*value) {
      case 0:
        gren->mEffectB.mStruggleCount = 5;
        gren->mEffectB.mStruggleLimit = 3;
        break;
      case 1:
        gren->mEffectB.mStruggleCount = 0;
        gren->mEffectB.mStruggleLimit = 3;
        break;
      case 2:
        gren->mEffectB.x0_1_ = true;
        break;
      case 3:
        gren->mEffectB.x0_1_ = false;
        gren->mEffectB.mStartHealth = gren->GetHealthInfo()->GetHP() + 1000.f;
        break;
      }
    }
    current = 0;
    return true;
  }
  if (strcmp(name, "gren.startbeam") == 0) {
    // runs StartElectricBeam <value> times, as that many kUE_EffectOn events of a BeamAttack would
    CStateManager* mgr = PortConsole::VarManager();
    for (int i = 0; value != nullptr && mgr != nullptr && i < *value; ++i) {
      gren->StartElectricBeam(*mgr);
    }
    current = gren->mEffectA.mBeamProjectileId != kInvalidUniqueId ? 1 : 0;
    return true;
  }
  if (strcmp(name, "gren.recreate") == 0) {
    // 1: destroy the collision manager as kSM_Delete does and build it again (vulnerability kept);
    // 2: the same with the Grenchler pitched straight up, so its forward has no horizontal part.
    CStateManager* mgr = PortConsole::VarManager();
    if (value != nullptr && *value != 0 && mgr != nullptr && gren->mCollisionManager.get() != nullptr) {
      gren->mCollisionManager->Destroy(*mgr);
      gren->mCollisionManager = nullptr;
      const CTransform4f saved = gren->GetTransform();
      if (*value == 2) {
        gren->SetTransform(CTransform4f::Translate(saved.GetTranslation()) *
                           CTransform4f::RotateX(CRelAngle::FromDegrees(90.f)));
      }
      gren->CreateCollisionManager(*mgr);
      gren->SetTransform(saved);
    }
    current = gren->mCollisionManager.get() != nullptr ? 1 : 0;
    return true;
  }
  if (strcmp(name, "gren.attach") == 0) {
    CStateManager* mgr = PortConsole::VarManager();
    if (value != nullptr && mgr != nullptr) {
      if (*value != 0) {
        gren->GrabPlayer(*mgr);
      } else {
        gren->ReleasePlayer(*mgr);
      }
    }
    current = gren->mPlayerGrabbed ? 1 : 0;
    return true;
  }
  // gren6 (FollowAttackPattern 425C, FindJumpTarget 4AA4): `gren.fap <n>` and `gren.jt <n>` build
  // scripted fixtures (waypoints, AI waypoints, jump points with editor ids), run the body and delete
  // them again; the result is a bit string in `current`.
  // fap 1 no attack connection (Activate, Update with the body in Jump, Deactivate, msg 3);
  // 2 plain waypoint ahead; 3 plain waypoint behind; 4 AI waypoint, body in Jump, no arrived/next
  // link; 5 AI waypoint with the link; 6 AI waypoint, body not in Jump; 7 Update with mSubmerged
  // flipped first. jt 1 ignoreRange; 2 plain; 3 self submerged; 4 frustrated; 5 jump too far;
  // 6 cost above maxCost (a Grenchler in the water triples maxCost: the waypoint is 3D+20 behind); 7 two jump points, second dearer; 8 second cheaper; 9 inactive jump point;
  // 10 other area; 11 no connection; 12 connection to a non-waypoint; 13 waypoint submerged, player
  // not; 14 everything submerged; 15 player submerged only.
  // gren8 (AcceptScriptMsg FD2C): pokes for the members a message arm reads, `var <id> gren.<name> <v>`; each answers
  // the old value. gren.curaction sets mCurrentAction (12 landing, 8 grapple, 17 ...), gren.fadeactive mFadeActive,
  // gren.tailtime 1 stamps mTailDestroyedTime now / 0 long ago, gren.alive mAlive, gren.landed bit mask (1 mLanded,
  // 2 x1c_5_), gren.team 0 no team manager id / 2 a valid one (this Grenchler's id), gren.cm 0 detaches the collision manager (kept
  // aside) / 1 puts it back, gren.tailflag sets mTailDestroyed raw, gren.fitems 1 sets mHasHealthBar and mIsGrappleGuardian for the Activate arm (restore 0).
  if (strncmp(name, "gren.", 5) == 0 && (strcmp(name, "gren.curaction") == 0 || strcmp(name, "gren.fadeactive") == 0 ||
                                           strcmp(name, "gren.tailtime") == 0 || strcmp(name, "gren.alive") == 0 ||
                                           strcmp(name, "gren.landed") == 0 || strcmp(name, "gren.team") == 0 ||
                                           strcmp(name, "gren.cm") == 0 || strcmp(name, "gren.healthbar") == 0 ||
                                           strcmp(name, "gren.tailflag") == 0 || strcmp(name, "gren.damagewait") == 0)) {
    static CCollisionActorManager* sAsideCm = nullptr;
    current = 0;
    if (strcmp(name, "gren.curaction") == 0) {
      current = static_cast< int >(gren->mCurrentAction);
      if (value != nullptr) {
        gren->mCurrentAction = static_cast< EAction >(*value);
      }
    } else if (strcmp(name, "gren.fadeactive") == 0) {
      current = gren->mFadeActive;
      if (value != nullptr) {
        gren->mFadeActive = static_cast< uchar >(*value);
      }
    } else if (strcmp(name, "gren.tailtime") == 0) {
      if (value != nullptr) {
        gren->mTailDestroyedTime = *value != 0 ? gren->mElapsedTime : -1000.f;
      }
    } else if (strcmp(name, "gren.alive") == 0) {
      current = gren->mAlive ? 1 : 0;
      if (value != nullptr) {
        gren->mAlive = *value != 0;
      }
    } else if (strcmp(name, "gren.landed") == 0) {
      current = (gren->mReflectInfo.mLanded ? 1 : 0) | (gren->mReflectInfo.x1c_5_ ? 2 : 0);
      if (value != nullptr) {
        gren->mReflectInfo.mLanded = (*value & 1) != 0;
        gren->mReflectInfo.x1c_5_ = (*value & 2) != 0;
      }
    } else if (strcmp(name, "gren.team") == 0) {
      current = gren->mTeamAiMgrId == kInvalidUniqueId ? 0 : 1;
      if (value != nullptr && *value == 0) {
        gren->mTeamAiMgrId = kInvalidUniqueId;
      } else if (value != nullptr && *value == 2) {
        gren->mTeamAiMgrId = gren->GetUniqueId(); // any valid id: AreaLoaded keeps it
      }
    } else if (strcmp(name, "gren.cm") == 0) {
      current = gren->mCollisionManager.get() != nullptr ? 1 : 0;
      if (value != nullptr && *value == 0 && gren->mCollisionManager.get() != nullptr) {
        sAsideCm = gren->mCollisionManager.get();
        gren->mCollisionManager.release();
      } else if (value != nullptr && *value != 0 && sAsideCm != nullptr) {
        gren->mCollisionManager = sAsideCm;
        sAsideCm = nullptr;
      }
    } else if (strcmp(name, "gren.damagewait") == 0) {
      // mCurDamageRemTime := value / 10 s (HitObject damages the player only once it is <= 0)
      current = static_cast< int >(gren->mCurDamageRemTime * 10.f);
      if (value != nullptr) {
        gren->mCurDamageRemTime = static_cast< float >(*value) / 10.f;
      }
    } else if (strcmp(name, "gren.tailflag") == 0) {
      // mTailDestroyed itself (0 intact / 1 destroyed), without DestroyTail's side effects
      current = gren->mTailDestroyed;
      if (value != nullptr) {
        gren->mTailDestroyed = *value;
      }
    } else if (strcmp(name, "gren.healthbar") == 0) {
      current = gren->mHasHealthBar ? 1 : 0;
      if (value != nullptr) {
        gren->mHasHealthBar = *value != 0;
      }
    }
    return true;
  }
  // gren8: `gren.sendmsg <m*100 + a*10 + w>` delivers Damage (m 1), ResistedDamage (m 2) or HitObject (m 3) to this
  // Grenchler from its collision actor index a (0 body, 5/6 crystals, see skCollisionActors), after setting that
  // actor's last-touched object by a ReflectedDamage message from: w 0 nothing, 1 a live weapon this Grenchler owns
  // (its electric beam projectile), 2 a live weapon the player owns, 3 the player itself. Answers -1 when the
  // weapon or actor does not exist, else 0.
  if (strcmp(name, "gren.sendmsg") == 0) {
    CStateManager* mgr = PortConsole::VarManager();
    current = -1;
    if (value != nullptr && mgr != nullptr && gren->mCollisionManager.get() != nullptr) {
      const int m = *value / 100, a = (*value / 10) % 10, w = *value % 10;
      const TUniqueId actorId =
          gren->mCollisionManager->GetCollisionDescFromIndex(static_cast< uint >(a)).GetCollisionActorId();
      TUniqueId touched = kInvalidUniqueId;
      bool found = w == 0;
      if (w == 3) {
        touched = mgr->GetPlayer(0)->GetUniqueId();
        found = true;
      } else if (w == 1 || w == 2) {
        CObjectList& all = mgr->ObjectListById(kOL_All);
        for (int i = all.GetFirstObjectIndex(); i != -1 && !found; i = all.GetNextObjectIndex(i)) {
          if (const CWeapon* weapon = TCastToConstPtr< CWeapon >(all[i])) {
            const bool playerOwned = weapon->GetOwnerId() == mgr->GetPlayer(0)->GetUniqueId();
            if ((w == 2) == playerOwned && weapon->GetOwnerId() != kInvalidUniqueId &&
                (w == 2 || weapon->GetOwnerId() == gren->GetUniqueId())) {
              touched = weapon->GetUniqueId();
              found = true;
            }
          }
        }
      }
      if (found && mgr->GetObjectById(actorId) != nullptr) {
        if (touched != kInvalidUniqueId) {
          mgr->DeliverScriptMsg(CScriptMsg(touched, actorId, kSM_ReflectedDamage));
        }
        mgr->DeliverScriptMsg(CScriptMsg(actorId, gren->GetUniqueId(),
                                         m == 1 ? kSM_Damage : m == 2 ? kSM_ResistedDamage : kSM_HitObject));
        current = 0;
      }
    }
    return true;
  }
  if (strcmp(name, "gren.fap") == 0 || strcmp(name, "gren.jt") == 0) {
    CStateManager* mgr = PortConsole::VarManager();
    current = -1;
    if (value == nullptr || mgr == nullptr) {
      return true;
    }
    static uint nextEditor = 0x01F60000;
    const bool fap = strcmp(name, "gren.fap") == 0;
    const int n = *value;
    const TAreaId area = gren->GetCurrentAreaId();
    CPlayer* player = mgr->GetPlayer(0);
    const CVector3f g = gren->GetTranslation();
    CVector3f u = player->GetTranslation() - g;
    if (u.CanBeNormalized()) {
      u.Normalize();
    } else {
      u = CVector3f(1.f, 0.f, 0.f);
    }
    const float dist = (player->GetTranslation() - g).Magnitude();
    rstl::vector< TUniqueId > made;
    CScriptWater* water = nullptr;
    {
      CObjectList& all = mgr->ObjectListById(kOL_All);
      for (int i = all.GetFirstObjectIndex(); i != -1; i = all.GetNextObjectIndex(i)) {
        CScriptWater* w = TCastToPtr< CScriptWater >(all[i]);
        if (w != nullptr) {
          water = w;
          break;
        }
      }
    }
    auto addWaypoint = [&](const CVector3f& pos, bool ai, const CVector3f* nextPos,
                           TEditorId& eidOut) -> CEntity* {
      // an arrived/next link from the waypoint when nextPos is given
      rstl::vector< SConnection > conns;
      TEditorId nextId(nextEditor++);
      CScriptWaypoint* next = nullptr;
      if (nextPos != nullptr) {
        conns.push_back(SConnection(kSS_Arrived, kSM_Next, nextId));
        next = rs_new CScriptWaypoint(mgr->AllocateUniqueId(), rstl::string_l("gren6 next"),
                                      CEntityInfo(area, CEntity::NullConnectionList, true, nextId),
                                      CTransform4f::Translate(*nextPos));
        mgr->AddObject(next);
        made.push_back(next->GetUniqueId());
      }
      eidOut = TEditorId(nextEditor++);
      CEntity* ent = nullptr;
      if (ai) {
        ent = rs_new CScriptAIWaypoint(mgr->AllocateUniqueId(), rstl::string_l("gren6 aiwp"),
                                       CEntityInfo(area, conns, true, eidOut),
                                       CTransform4f::Translate(pos), 1.f, 0.f, 0, 0, 0);
      } else {
        ent = rs_new CScriptWaypoint(mgr->AllocateUniqueId(), rstl::string_l("gren6 wp"),
                                     CEntityInfo(area, conns, true, eidOut),
                                     CTransform4f::Translate(pos));
      }
      mgr->AddObject(ent);
      made.push_back(ent->GetUniqueId());
      return ent;
    };
    auto setSub = [&](CActor& actor, bool on, float* savedZ) {
      if (water == nullptr) {
        return;
      }
      if (on) {
        CVector3f t = actor.GetTranslation();
        *savedZ = t.GetZ();
        t.SetZ(water->GetTriggerBoundsWR().GetMaxPoint().GetZ() - 10.f);
        actor.SetTranslation(t);
        actor.SetInFluid(*mgr, true, water->GetUniqueId());
      } else {
        actor.SetInFluid(*mgr, false, water->GetUniqueId());
        CVector3f t = actor.GetTranslation();
        t.SetZ(*savedZ);
        actor.SetTranslation(t);
      }
    };
    if (fap) {
      const bool prevInPosition = gren->mWaypointNavigation.IsInPosition();
      const TUniqueId prevDest = gren->mWaypointNavigation.GetDestination();
      const pas::EAnimationState prevBody = gren->BodyController()->GetCurrentStateId();
      const bool prevSub = gren->mSubmerged;
      const rstl::vector< SConnection > prevConns = gren->ConnectionList();
      TEditorId eid(0);
      CEntity* wp = nullptr;
      if (n == 2) {
        wp = addWaypoint(g + gren->GetTransform().GetForward() * 5.f, false, nullptr, eid);
      } else if (n == 3) {
        wp = addWaypoint(g - gren->GetTransform().GetForward() * 5.f, false, nullptr, eid);
      } else if (n >= 4) {
        const CVector3f nextPos = g + CVector3f(0.f, 8.f, 0.f);
        wp = addWaypoint(g + gren->GetTransform().GetForward() * 5.f, true,
                         n == 5 ? &nextPos : nullptr, eid);
      }
      if (wp != nullptr) {
        gren->ConnectionList().push_back(SConnection(kSS_Attack, kSM_Follow, eid));
      }
      int bits = 0;
      gren->FollowAttackPattern(*mgr, kStateMsg_Activate, 0.016f);
      bits |= gren->mWaypointNavigation.IsInPosition() ? 1 : 0;
      bits |= gren->mWaypointNavigation.GetDestination() != kInvalidUniqueId ? 2 : 0;
      if (n != 6) {
        gren->BodyController()->BodyStateInfo().SetState(pas::kAS_Jump);
      }
      if (n == 7) {
        gren->mSubmerged = !gren->IsDeeplySubmerged(*mgr);
      }
      gren->FollowAttackPattern(*mgr, kStateMsg_Update, 0.016f);
      bits |= gren->mSubmerged == prevSub ? 0 : 4;
      if (n == 1) {
        gren->FollowAttackPattern(*mgr, kStateMsg_Deactivate, 0.016f);
        gren->FollowAttackPattern(*mgr, static_cast< EStateMsg >(3), 0.016f);
      }
      gren->BodyController()->BodyStateInfo().SetState(prevBody);
      gren->mSubmerged = prevSub;
      gren->mWaypointNavigation.SetDestination(prevDest);
      gren->mWaypointNavigation.SetInPosition(prevInPosition);
      gren->ConnectionList() = prevConns;
      current = bits;
    } else {
      // Geometry (u = unit vector from this Grenchler to the player, D the distance, D >= 20): jump
      // point at 1 along u (jd 1), waypoint 4 short of the player (td 4), so the cost is
      // 0.3 * (D - 5) + 5, below maxCost = D and past the end check.
      float savedGz = 0.f, savedPz = 0.f, savedWz = 0.f;
      if (n == 3 || n == 14) {
        setSub(*gren, true, &savedGz);
      }
      if (n == 15 || n == 14) {
        setSub(*player, true, &savedPz);
      }
      const CVector3f g2 = gren->GetTranslation();
      CVector3f u2 = player->GetTranslation() - g2;
      if (u2.CanBeNormalized()) {
        u2.Normalize();
      } else {
        u2 = CVector3f(1.f, 0.f, 0.f);
      }
      const CVector3f jPos = g2 + u2 * (n == 5 ? 8.f : 1.f);
      const CVector3f wPos =
          n == 6 ? g2 - u2 * (3.f * dist + 20.f) : (player->GetTranslation() - u2 * 4.f);
      const float prevBlocked = gren->mPathBlockedTime;
      auto addJump = [&](const CVector3f& pos, const CVector3f& wp, int mode) {
        TEditorId weid(0);
        TEditorId jeid(nextEditor++);
        rstl::vector< SConnection > conns;
        if (mode != 3) {
          CEntity* w = nullptr;
          if (mode == 4) {
            // an AI hint carries the editor id: a valid object that is no CScriptWaypoint
            weid = TEditorId(nextEditor++);
            w = rs_new CScriptAIHint(mgr->AllocateUniqueId(), rstl::string_l("gren6 hint"),
                                     CEntityInfo(area, CEntity::NullConnectionList, true, weid),
                                     CTransform4f::Translate(wp), CScriptAIHint::kHT_Maneuver, 10.f,
                                     0.f, 0.f, 0.f);
            mgr->AddObject(w);
            made.push_back(w->GetUniqueId());
          } else {
            w = addWaypoint(wp, false, nullptr, weid);
            if (n == 13 || n == 14) {
              setSub(*static_cast< CActor* >(w), true, &savedWz);
            }
          }
          conns.push_back(SConnection(kSS_Arrived, kSM_Next, weid));
        }
        CScriptAiJumpPoint* jp = rs_new CScriptAiJumpPoint(
            mgr->AllocateUniqueId(), rstl::string_l("gren6 jump"),
            CEntityInfo(mode == 2 ? TAreaId(area.Value() + 1) : area, conns, mode != 1, jeid),
            CTransform4f::Translate(pos), 3.f + n, 0);
        mgr->AddObject(jp);
        made.push_back(jp->GetUniqueId());
      };
      // mode: 0 plain, 1 inactive, 2 other area, 3 no connection, 4 connection to a non-waypoint
      const int mode = n == 9 ? 1 : n == 10 ? 2 : n == 11 ? 3 : n == 12 ? 4 : 0;
      addJump(jPos, wPos, mode);
      if (n == 7) { // a second jump point, dearer: skipped by the best-cost compare
        addJump(g2 + u2 * 4.f, wPos, 0);
      } else if (n == 8) { // a second jump point, cheaper: replaces the first
        addJump(g2 + u2 * 0.5f, wPos, 0);
      }
      gren->mPathBlockedTime = (n == 4 || n == 6 || n == 13 || n == 14) ? 2.f : 0.f;
      const bool result = gren->FindJumpTarget(*mgr, n == 1);
      if (n == 3 || n == 14) {
        setSub(*gren, false, &savedGz);
      }
      if (n == 15 || n == 14) {
        setSub(*player, false, &savedPz);
      }
      gren->mPathBlockedTime = prevBlocked;
      current = (result ? 1 : 0) + (gren->mReflectInfo.mTargetValid ? 2 : 0) +
                static_cast< int >(gren->mReflectInfo.mApexOffset) * 4;
    }
    for (size_t i = 0; i < made.size(); ++i) {
      mgr->DeleteObjectRequest(made[i]);
    }
    return true;
  }
  // gren6 coverage vars: field pokes and body calls for the small predicates.
  // gren.lasthit 0 stamps mLastHitTime now, 1 backdates it; gren.guardian sets mIsGrappleGuardian;
  // gren.turn <1|0> turns it toward/away from its target; gren.x2c sets the beam attack flag; gren.launched sets the jump launch flag; gren.flash <n> sets
  // the damage flash timer to n/10 s; gren.fncall <n> runs 1 JustHit, 2 PlayerBehindMe,
  // 3 LaunchToJumpTarget, 4 UpdateDamageFlash(0.05), 5 SpawnBeamHitEffect, 6 SpawnBeamHitEffect
  // with no explosion asset, and reads the result (-1 for a void body).
  if (strcmp(name, "gren.lasthit") == 0) {
    if (value != nullptr) {
      gren->mLastHitTime = *value == 0 ? gren->mElapsedTime : -100.f;
    }
    current = 0;
    return true;
  }
  if (strcmp(name, "gren.guardian") == 0) {
    if (value != nullptr) {
      gren->mIsGrappleGuardian = *value != 0;
    }
    current = gren->mIsGrappleGuardian ? 1 : 0;
    return true;
  }
  if (strcmp(name, "gren.x2c") == 0) {
    if (value != nullptr) {
      gren->mBeamAttack.x2c_ = *value != 0;
    }
    current = gren->mBeamAttack.x2c_ ? 1 : 0;
    return true;
  }
  if (strcmp(name, "gren.launched") == 0) {
    if (value != nullptr) {
      gren->mReflectInfo.mLaunched = *value != 0;
    }
    current = gren->mReflectInfo.mLaunched ? 1 : 0;
    return true;
  }
  if (strcmp(name, "gren.flash") == 0) {
    if (value != nullptr) {
      gren->mDamageFlashTimer = static_cast< float >(*value) / 10.f;
    }
    current = static_cast< int >(gren->mDamageFlashTimer * 10.f);
    return true;
  }
  if (strcmp(name, "gren.turn") == 0) {
    // 1 turns this Grenchler to look at its target (IsFacingTarget true), 0 away from it
    CStateManager* mgr = PortConsole::VarManager();
    if (value != nullptr && mgr != nullptr) {
      CVector3f toTarget = gren->GetTargetPosition(*mgr) - gren->GetTranslation();
      toTarget.SetZ(0.f);
      if (toTarget.CanBeNormalized()) {
        toTarget.Normalize();
        const CVector3f dir = *value != 0 ? toTarget : -toTarget;
        gren->SetTransform(
            CTransform4f::LookAt(gren->GetTranslation(), gren->GetTranslation() + dir, CVector3f::Up()));
      }
    }
    current = 0;
    return true;
  }
  if (strcmp(name, "gren.fncall") == 0) {
    CStateManager* mgr = PortConsole::VarManager();
    current = -1;
    if (value != nullptr && mgr != nullptr) {
      switch (*value) {
      case 1:
        current = gren->JustHit(*mgr, CTriggerData(0.f)) ? 1 : 0;
        break;
      case 2:
        current = gren->PlayerBehindMe(*mgr, CTriggerData(0.f)) ? 1 : 0;
        break;
      case 3:
        gren->LaunchToJumpTarget();
        break;
      case 4:
        gren->UpdateDamageFlash(0.05f);
        break;
      case 5:
        gren->SpawnBeamHitEffect(*mgr);
        break;
      case 6: {
        const uint saved = gren->mEffectA.mExplosionAssetId;
        gren->mEffectA.mExplosionAssetId = kInvalidAssetId;
        gren->SpawnBeamHitEffect(*mgr);
        gren->mEffectA.mExplosionAssetId = saved;
        break;
      }
      case 7:
        gren->CreateVisorEffect(*mgr);
        break;
      case 8: { // CreateVisorEffect with the asset id invalid (retail 7848 outer beq)
        const uint saved = gren->mEffectB.mVisorEffectAssetId;
        gren->mEffectB.mVisorEffectAssetId = kInvalidAssetId;
        gren->CreateVisorEffect(*mgr);
        gren->mEffectB.mVisorEffectAssetId = saved;
        break;
      }
      case 9: { // CreateVisorEffect with the effect token unset (retail 7848 lbz 0xd8c)
        rstl::optional_object< TToken< CGenDescription > > saved = gren->mEffectB.mVisorEffect;
        gren->mEffectB.mVisorEffect.clear();
        gren->CreateVisorEffect(*mgr);
        gren->mEffectB.mVisorEffect = saved;
        break;
      }
      case 10:
        gren->CreateDamageVisorEffect(*mgr);
        break;
      case 11: { // retail 7A38 lbz 0xef0 == 0
        rstl::optional_object< TToken< CGenDescription > > saved = gren->mDamageEffect.mEffect;
        gren->mDamageEffect.mEffect.clear();
        gren->CreateDamageVisorEffect(*mgr);
        gren->mDamageEffect.mEffect = saved;
        break;
      }
      case 12:
        gren->CreateGrappleHitVisorEffect(*mgr);
        break;
      case 13: { // grapple-effect token unset
        rstl::optional_object< TToken< CGenDescription > > saved = gren->mGrappleEffect.mEffect;
        gren->mGrappleEffect.mEffect.clear();
        gren->CreateGrappleHitVisorEffect(*mgr);
        gren->mGrappleEffect.mEffect = saved;
        break;
      }
      case 14: { // CreateGrappleHitVisorEffect with the player 40 units away (retail 7C20 skip arm)
        const CTransform4f saved = gren->GetTransform();
        CTransform4f away = saved;
        CVector3f dir = saved.GetTranslation() - mgr->GetPlayer(0)->GetTranslation();
        dir.Normalize();
        away.SetTranslation(mgr->GetPlayer(0)->GetTranslation() + dir * 40.f);
        gren->SetTransform(away);
        gren->CreateGrappleHitVisorEffect(*mgr);
        gren->SetTransform(saved);
        break;
      }
      case 15: // SpawnTail, plain / 16 submerged / 17 ing-possessed / 18 both
      case 16:
      case 17:
      case 18: {
        const bool savedSub = gren->mSubmerged;
        const float savedBlend = gren->mIngPossessionBlend;
        gren->mSubmerged = *value == 16 || *value == 18;
        gren->mIngPossessionBlend = (*value == 17 || *value == 18) ? 1.f : 0.f;
        gren->SpawnTail(*mgr);
        gren->mSubmerged = savedSub;
        gren->mIngPossessionBlend = savedBlend;
        break;
      }
      case 19:
        gren->MarkPathRegion(*mgr);
        break;
      case 20: {
        const CVector3f v = gren->GetManeuverTarget(*mgr);
        current = static_cast< int >(v.GetX());
        break;
      }
      case 22:
        gren->UnmarkPathRegion(*mgr);
        break;
      case 23: {
        CScriptAiJumpPoint* jp =
            gren->FindJumpPoint(*mgr, CVector3f(1.0e6f, 1.0e6f, 1.0e6f), kInvalidUniqueId);
        current = jp != nullptr ? 1 : 0;
        break;
      }
      case 24: {
        const CVector3f target = mgr->GetPlayer(0)->GetTranslation();
        CScriptAiJumpPoint* jp = gren->FindJumpPoint(*mgr, target, kInvalidUniqueId);
        if (jp != nullptr) {
          jp = gren->FindJumpPoint(*mgr, target, jp->GetUniqueId());
        }
        current = jp != nullptr ? 1 : 0;
        break;
      }
      case 21: {
        CScriptAiJumpPoint* jp = gren->FindJumpPoint(*mgr, mgr->GetPlayer(0)->GetTranslation(),
                                                     kInvalidUniqueId);
        current = jp != nullptr ? 1 : 0;
        break;
      }
      }
    }
    return true;
  }
  float* field = nullptr;
  if (strcmp(name, "gren.beamlen") == 0) {
    field = &gren->mEffectC.mBeamLength;
  } else if (strcmp(name, "gren.x24") == 0) {
    field = &gren->mEffectB.x24_;
  } else if (strcmp(name, "gren.crystaldmg") == 0) {
    field = &gren->mCrystalDamage;
  } else if (strcmp(name, "gren.grappledmg") == 0) {
    field = &gren->mGrappleCrystalDamage;
  } else if (strcmp(name, "gren.tailhp") == 0) {
    field = &gren->mTailHealth;
  } else {
    return false;
  }
  if (value != nullptr) {
    *field = static_cast< float >(*value);
  }
  current = static_cast< int >(*field);
  return true;
}

namespace {
const bool sGrenchlerConsoleVars = PortConsole::RegisterVar(&CGrenchler::PortConsoleVar);
} // namespace
#endif

#ifndef MONOLITHIC
// @ fn_27_138
static void SetFuncPtrs() {
  static SGrenchler_FuncPtrs funcPtrs;
  funcPtrs.mLoader = &LoadGrenchler;
  SetSGrenchler_FuncPtrs(&funcPtrs);
}

extern "C" void RELMain() { SetFuncPtrs(); }

extern "C" void RELExit() { SetSGrenchler_FuncPtrs(nullptr); }
#endif
