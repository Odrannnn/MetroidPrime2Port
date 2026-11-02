// Host body for the MinorIng REL (module 44): CMinorIng's state machine, ported for behaviour
// from the REL's pseudocode (docs/module-analysis/IngFamily.md section B.2). Functions marked
// `// @ fn_44_<off>` were checked against that retail function; `[?]` marks what is not proven.
// Not modelled on the host: the blob-effect actor and the collision actor retail creates on
// kSM_Create (fn_44_48F0/fn_44_319C), the touch AABBs (fn_44_3E8C), the inglet spawn in
// MassiveDeath and the damage-effect particles (fn_44_25E4/fn_44_29D4).
#include "MetroidPrime/Enemies/CMinorIng.hpp"

#include "Collision/CCollisionPrimitive.hpp"
#include "Collision/CMaterialFilter.hpp"
#include "Collision/CRayCastResult.hpp"
#include "Kyoto/Animation/CInt32POINode.hpp"
#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "Kyoto/Animation/CPASDatabase.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Math/CQuaternion.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "Kyoto/Particles/CGenDescription.hpp"
#include "Kyoto/CRandom16.hpp"
#include "Kyoto/Math/CPlane.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/CKnockBackInfo.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CSafeZoneManager.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Enemies/CCollisionTracker.hpp"
#include "MetroidPrime/Enemies/CPatternedInfo.hpp"
#include "MetroidPrime/Enemies/CGenericFSM2.hpp"
#include "MetroidPrime/Enemies/PortIngShared.hpp"
#include "MetroidPrime/Enemies/CTeamAiRole.hpp"
#include "MetroidPrime/PathFinding/CPathFindArea.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrMinorIng.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAIHint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAIWaypoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSafeZone.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSpecialFunction.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTeamAiMgr.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWaypoint.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CImpactVisorEffect.hpp"
#include "MetroidPrime/Weapons/CWeapon.hpp"
#include "WorldFormat/CCollisionSurface.hpp"

#include <stdio.h>
#include <stdlib.h>

typedef CPatterned::StateMachine::TriggerFunc TriggerFunc;
typedef CPatterned::StateMachine::StateFunc StateFunc;
typedef CPatterned::StateMachine::CodeFunc CodeFunc;

// Retail data_1A8 (34 triggers), data_47C (15 states) and data_5A8 (5 code functions), installed
// by fn_44_9758; the names are the REL's own strings.
//
// fn_44_ABCC (0x8b8) is the REL's .ctors entry, the compiler's static initialiser: it copies the
// three tables' records into the name tables in .data and registers the two global strings it
// builds (fn_44_2984 is the destructor it registers with them). The port's three arrays below
// are that initialiser's result - the same 34 trigger names, the same 15 state names and the
// same 5 code-function names, in the same order, each pointing at the retail function the
// record names. `tools/drivers/minoring_tables.py` reads both sides and compares them entry for
// entry (34 triggers, 15 states, 5 code functions, no mismatch), so the marker sits above the
// first of them, the way the EmperorIngStage3 port marks fn_18_E180.
//
// @ fn_44_ABCC
static CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    {"PathShagged", static_cast< TriggerFunc >(&CMinorIng::PathShagged)},
    {"ShouldMoveUnderBall", static_cast< TriggerFunc >(&CMinorIng::ShouldMoveUnderBall)},
    {"HasPatrolPath", static_cast< TriggerFunc >(&CMinorIng::HasPatrolPath)},
    {"PlayerLeashReached", static_cast< TriggerFunc >(&CMinorIng::PlayerLeashReached)},
    {"LeavePatrol", static_cast< TriggerFunc >(&CMinorIng::LeavePatrol)},
    {"LineOfSight", static_cast< TriggerFunc >(&CMinorIng::LineOfSight)},
    {"InMaxRange", static_cast< TriggerFunc >(&CMinorIng::InMaxRange)},
    {"ShouldFire", static_cast< TriggerFunc >(&CMinorIng::ShouldFire)},
    {"PuddleHitByWeapon", static_cast< TriggerFunc >(&CMinorIng::PuddleHitByWeapon)},
    {"CancelStun", static_cast< TriggerFunc >(&CMinorIng::CancelStun)},
    {"IsBeingKnockedback", static_cast< TriggerFunc >(&CMinorIng::IsBeingKnockedback)},
    {"TargetInSafeZone", static_cast< TriggerFunc >(&CMinorIng::TargetInSafeZone)},
    {"HasCoverPoint", static_cast< TriggerFunc >(&CMinorIng::HasCoverPoint)},
    {"PointPathOver", static_cast< TriggerFunc >(&CMinorIng::PointPathOver)},
    {"FoundPointPath", static_cast< TriggerFunc >(&CMinorIng::FoundPointPath)},
    {"IsPointPathFinding", static_cast< TriggerFunc >(&CMinorIng::IsPointPathFinding)},
    {"StunOver", static_cast< TriggerFunc >(&CMinorIng::StunOver)},
    {"ShouldPathFind", static_cast< TriggerFunc >(&CMinorIng::ShouldPathFind)},
    {"CancelAnim", static_cast< TriggerFunc >(&CMinorIng::CancelAnim)},
    {"HasAttackPattern", static_cast< TriggerFunc >(&CMinorIng::HasAttackPattern)},
    {"AttackPatternOver", static_cast< TriggerFunc >(&CMinorIng::AttackPatternOver)},
    {"IsFollowingAttackPath", static_cast< TriggerFunc >(&CMinorIng::IsFollowingAttackPath)},
    {"AllowAttackPatternProjectile",
     static_cast< TriggerFunc >(&CMinorIng::AllowAttackPatternProjectile)},
    {"AllowAttackPatternStun", static_cast< TriggerFunc >(&CMinorIng::AllowAttackPatternStun)},
    {"RecalculatePath", static_cast< TriggerFunc >(&CMinorIng::RecalculatePath)},
    {"InsideSafeZone", static_cast< TriggerFunc >(&CMinorIng::InsideSafeZone)},
    {"IsMoveAwayFromSafeZoneOver",
     static_cast< TriggerFunc >(&CMinorIng::IsMoveAwayFromSafeZoneOver)},
    {"FoundMovementPos", static_cast< TriggerFunc >(&CMinorIng::FoundMovementPos)},
    {"ShouldEvaporate", static_cast< TriggerFunc >(&CMinorIng::ShouldEvaporate)},
    {"IsInCorporealForm", static_cast< TriggerFunc >(&CMinorIng::IsInCorporealForm)},
    {"StayOnPointPathFinding", static_cast< TriggerFunc >(&CMinorIng::StayOnPointPathFinding)},
    {"AllowProjectileDuringStayOnPointPathFinding",
     static_cast< TriggerFunc >(&CMinorIng::AllowProjectileDuringStayOnPointPathFinding)},
    {"StateOver", static_cast< TriggerFunc >(&CMinorIng::StateOver)},
    {"IsLuredBySafeZone", static_cast< TriggerFunc >(&CMinorIng::IsLuredBySafeZone)},
};

static CPatterned::StateMachine::SStateFunction skStates[] = {
    {"PathFind", static_cast< StateFunc >(&CMinorIng::PathFind)},
    {"PointPathFind", static_cast< StateFunc >(&CMinorIng::PointPathFind)},
    {"Stunned", static_cast< StateFunc >(&CMinorIng::Stunned)},
    {"ProjectileAttack", static_cast< StateFunc >(&CMinorIng::ProjectileAttack)},
    {"Patrol", static_cast< StateFunc >(&CMinorIng::Patrol)},
    {"Dead", static_cast< StateFunc >(&CMinorIng::Dead)},
    {"SetWallPointCoverDest", static_cast< StateFunc >(&CMinorIng::SetWallPointCoverDest)},
    {"SetCoverDest", static_cast< StateFunc >(&CMinorIng::SetCoverDest)},
    {"IntoPuddleForm", static_cast< StateFunc >(&CMinorIng::IntoPuddleForm)},
    {"IntoCorporealForm", static_cast< StateFunc >(&CMinorIng::IntoCorporealForm)},
    {"Idle", static_cast< StateFunc >(&CMinorIng::Idle)},
    {"FollowAttackPattern", static_cast< StateFunc >(&CMinorIng::FollowAttackPattern)},
    {"MoveAwayFromSafeZone", static_cast< StateFunc >(&CMinorIng::MoveAwayFromSafeZone)},
    {"AlwaysPointPathFind", static_cast< StateFunc >(&CMinorIng::AlwaysPointPathFind)},
    {"WaitForFSMTransition", static_cast< StateFunc >(&CMinorIng::WaitForFSMTransition)},
};

static CPatterned::StateMachine::SCodeFunction skCodeFuncs[] = {
    {"SelectTarget", static_cast< CodeFunc >(&CMinorIng::SelectTarget)},
    {"FindCoverPoint", static_cast< CodeFunc >(&CMinorIng::FindCoverPoint)},
    {"SetPointCoverDest", static_cast< CodeFunc >(&CMinorIng::SetPointCoverDest)},
    {"SetLuredDest", static_cast< CodeFunc >(&CMinorIng::SetLuredDest)},
    {"SetLuredPointDest", static_cast< CodeFunc >(&CMinorIng::SetLuredPointDest)},
};

namespace {

// The ten inglet angles (the REL's lbl_44_rodata_0): the value in tens of degrees, less 50.
const int skIngletAngles[] = {0, 8, 2, 6, 9, 3, 0, 5, 1, 7};

bool DebugMinorIng() {
  static int sEnabled = -1;
  if (sEnabled < 0) {
    const char* env = getenv("MP2_DEBUG_MINORING");
    sEnabled = env != nullptr && env[0] != '\0' && env[0] != '0' ? 1 : 0;
  }
  return sEnabled != 0;
}

// Material filters from the REL's .data: data_0 (Touch), data_5F8/5FC (the move-clear check) and
// data_60C (the snap-to-floor ray) are material numbers shifted into masks: 0x3b Solid, 0x20
// Player. The ids are on the bodies that use them, not here.
CMaterialFilter SolidFilter() {
  return CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList());
}


const char* Fsm2NodeName(const CState2* node) {
  if (node == nullptr) {
    return "?";
  }
  switch (node->GetType()) {
  case CState2::kType_State:
    return static_cast< const CState2State* >(node)->GetName().data();
  case CState2::kType_Code:
    return static_cast< const CState2Code* >(node)->GetName().data();
  case CState2::kType_Trigger:
    return static_cast< const CState2Trigger* >(node)->GetName().data();
  case CState2::kType_SubMachine:
    return static_cast< const CState2SubMachine* >(node)->GetName().data();
  }
  return "?";
}

template < class Node >
void DumpFsm2Nodes(const char* kind, const rstl::vector< Node >& nodes) {
  for (int i = 0; i < nodes.size(); ++i) {
    const rstl::vector< SStateMachine2Transition >& out = nodes[i].GetTransitions();
    fprintf(stderr, "[minoring] fsm %s %s:", kind, nodes[i].GetName().data());
    for (int t = 0; t < out.size(); ++t) {
      fprintf(stderr, " %s->%s", out[t].mName.data(), Fsm2NodeName(out[t].mTarget));
    }
    fprintf(stderr, "\n");
  }
}

void DumpFsm2(const CGenericFSM2& machine) {
  DumpFsm2Nodes("state", machine.GetStates());
  DumpFsm2Nodes("code", machine.GetCodes());
  const rstl::vector< CState2Trigger >& triggers = machine.GetTriggers();
  for (int i = 0; i < triggers.size(); ++i) {
    const rstl::vector< SStateMachine2Transition >& out = triggers[i].GetTransitions();
    fprintf(stderr, "[minoring] fsm trigger %s%s(%g):", triggers[i].IsNegated() ? "!" : "",
            triggers[i].GetName().data(), triggers[i].GetArgument().GetFloat());
    for (int t = 0; t < out.size(); ++t) {
      fprintf(stderr, " %s->%s", out[t].mName.data(), Fsm2NodeName(out[t].mTarget));
    }
    fprintf(stderr, "\n");
  }
}

// @ fn_44_3614
// The retail copy moves the whole ray result from the source to the destination: the eight floats
// at +0x00..+0x1c, the two words at +0x20 and +0x24 and the valid byte at +0x28 (asm 0x3614-0x366C).
void CopyRayCastResult(CRayCastResult& dest, const CRayCastResult& src) { dest = src; }

// @ fn_44_AB78
// The retail finds the best animation for a parm and keeps it only when its score is above
// FLT_EPSILON (asm 0xAB78-0xABCC): the score lands in the caller's stack slot and the id
// beside it, and the id is -1 when the score is too small.
int FindBestAnimationId(const CPASDatabase& pas, const CPASAnimParmData& parm) {
  const rstl::pair< float, int > best = pas.FindBestAnimation(parm, -1);
  return best.first > 1.1920929e-07f ? best.second : -1;
}

// @ fn_44_D00
// The retail copy moves a whole CHealthInfo from the source to the destination: the three floats
// at +0x00..+0x08, the two weapon modes at +0x0c and +0x14, the four ids at +0x10..+0x1a and the
// two flag bits at +0x1c (asm 0xD00-0xD4C). The host's CHealthInfo has that layout.
CHealthInfo CopyHealthInfo(const CHealthInfo& src) { return src; }

// @ fn_44_FE8
// The retail reads the inglet's bouncing-projectile block into the grenade data (asm 0xFE8-0x107C):
// the mass at +0x00 and the speed at +0x04 (asm 0xFE8/0xFF0), the damage info at +0x08..+0x23 (the
// block's damage, converted by LdrToDamageInfo), the bounce count at +0x24 (asm 0x1040), the four
// ids at +0x28..+0x34 (asm 0x1044-0x1050: the explosion twice, then the trail and the effect), the
// two sounds at +0x38/+0x3a (asm 0x1054/0x1058) and the four floats at +0x3c..+0x48 (asm
// 0x105C-0x1068: 0.1 from the rodata at 0x28 and 150 from the rodata at 0x2c, asm 0x6A4/0x698).
// The flag byte at +0x4c takes the caller's bool argument: fn_44_FE8 is a leaf, so its +0xf is
// the caller's stack byte, and the caller's `li r0, 1` (asm 0x680) stored at +0xc (asm 0x6D4)
// puts the word's low byte - the one the callee reads (asm 0x103C) - at +0xf (big-endian).
CBouncyGrenadeData BuildGrenadeData(const SLdrUnknownStruct34& sldr) {
  return CBouncyGrenadeData(sldr.mass, sldr.bounciness, LdrToDamageInfo(sldr.damage),
                             static_cast< uint >(sldr.numBounces), sldr.explosion, sldr.explosion,
                             sldr.trail, sldr.effect, static_cast< ushort >(sldr.sound_Bounce),
                             static_cast< ushort >(sldr.sound_Explode), 0.1f, 150.f, 0.1f, 150.f,
                             true);
}

// @ fn_44_C44
// The retail copies that struct into the data's 0xd0 (asm 0xC44-0xD00): the two floats at
// +0x00/+0x04, the damage info at +0x08..+0x23, the bounce count at +0x24, the ids at
// +0x28..+0x34, the sounds at +0x38/+0x3a, the four floats at +0x3c..+0x48 and the flag byte at
// +0x4c. The host's CBouncyGrenadeData has the retail's layout, so the copy moves the whole
// struct.
CBouncyGrenadeData CopyGrenadeData(const CBouncyGrenadeData& src) { return src; }

} // namespace

// The retail's vtable slots that are the base class's own inline functions, which the REL
// carries its own copies of (the host's CPatterned and CActor have the same base functions, so
// the host inherits them; no marker): fn_44_18 (it stores the rodata constant lbl_8041AAB8 in the
// base's field at 0x448), fn_44_28 (the base's flag byte at 0x44f), fn_44_30 (it returns false),
// fn_44_38 (it writes the invalid id), fn_44_48 (the base's flag at 0x34c, the byte's 0x10),
// fn_44_54 (it returns the rodata constant lbl_8041B758), fn_44_60 (the base's member at 0x754),
// fn_44_68 (it returns true) and fn_44_59E8 (it copies a CVector3f).
//
// The retail's FSM2 node destructors: fn_44_87E0 (the most derived node), fn_44_8404 and
// fn_44_78BC (the nodes derived from it) put the node's vtable back and free it, and fn_44_7918
// is the base node's destructor. The host's CState2 node classes own their destructors, so the
// host has them; no marker.

// @ fn_44_0
// The retail returns its path find search (this+0x960) and the check finds no difference.
CPathFindSearch* CMinorIng::GetSearchPath() { return &mPathFindSearch; }

// @ fn_44_8
// The retail returns its point path find search (this+0xa4c); the check finds no difference.
CPathFindPointSearch* CMinorIng::GetPointSearchPath() { return &mPointPathFindSearch; }

// @ fn_44_5AB8
// The retail returns its projectile info (this+0x918); the check finds no difference.
CProjectileInfo* CMinorIng::ProjectileInfo() { return &mProjectileInfo; }

// @ fn_44_10
// The retail returns true; the check finds no difference.
bool CMinorIng::IsListening() const { return true; }

// The retail constructor (the module's 0x9D58) locks the ing-spot hit effects (0x940/0x950)
// only when the asset is set.
static rstl::optional_object< TLockedToken< CGenDescription > > OptionalPart(CAssetId id) {
  if (id == kInvalidAssetId) {
    return rstl::optional_object< TLockedToken< CGenDescription > >();
  }
  return rstl::optional_object< TLockedToken< CGenDescription > >(
      TLockedToken< CGenDescription >(gpSimplePool->GetObj(SObjectTag('PART', id))));
}

// EPatternedAI 0x4b, flavor 0, movement 0 (ground), collider 1, body 1 (bipedal): the retail
// constructor's arguments to CPatterned (fn_44_9D58).
//
// The retail's constructor (fn_44_9D58, asm 0x9D58) fills the members in this order; the host
// does the same, so the stored state matches:
// - the base class (asm 0x9DBC), the data copy (asm 0x9DD4) and the projectile info (asm 0x9DE4),
// - the two hit-effect tokens and their "is set" bytes (asm 0x9EB0 for the normal one, asm 0xA02C
//   for the heavy one: a valid asset id locks the token, an invalid one leaves it empty),
// - the path find search (asm 0xA108), the point search (asm 0xA114), the surface helper
//   (asm 0xA11C) and the ing-spot navigation (asm 0xA124),
// - the zero move direction (asm 0xA148), the two empty touch bounds (asm 0xA174/0xA178) and the
//   line-of-sight tracker (asm 0xA18C: the uid, the invalid segment, 0.1 and 0.05),
// - the form (asm 0xA198) and the ten ids (asm 0xA1B8-0xA204),
// - the two-int container (asm 0xA5B4), the zero target position (asm 0xA210), the model scale
//   (asm 0xA344) and the timers (asm 0xA23C-0xA278),
// - the health tracker (asm 0xA24C: the patterned info's health), the animation ids (asm 0xA264)
//   and the lock-on locator (asm 0xA27C).
// @ fn_44_9D58
CMinorIng::CMinorIng(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                     const CTransform4f& xf, const CModelData& modelData,
                     const CPatternedInfo& pInfo, const CActorParameters& actParms,
                     const CMinorIngData& data)
: CPatterned(static_cast< EPatternedAI >(0x4b), uid, name, kFT_Zero, info, xf, modelData, pInfo,
             kMT_Ground, kCT_One, kBT_BiPedal, actParms)
, mData(data)
, mProjectileInfo(data.mProjectile, data.mProjectileDamage)
, mNormalHitEffect(OptionalPart(data.mIngSpot.GetNormalHitEffect()))
, mHeavyHitEffect(OptionalPart(data.mIngSpot.GetHeavyHitEffect()))
, mPathFindSearch(nullptr, 0x301, pInfo.GetPathfindingIndex(), 1.f, 1.f, 0, CPFRegion::kRP_Center)
, mPointPathFindSearch(nullptr)
, mSurfaceAlignment()
, mIngSpotPathFindNavigation()
, mSurfaceMoveDir(CVector3f::Zero())
, mLineOfSight(uid, CSegId(), 0.1f, 0.05f)
, mForm(kF_None)
, mTeamId(kInvalidUniqueId)
, mTargetId(kInvalidUniqueId)
, mCoverHintId(kInvalidUniqueId)
, mLastCoverId(kInvalidUniqueId)
, mAttackPathLastId(kInvalidUniqueId)
, mAttackPathId(kInvalidUniqueId)
, mSafeZoneEnterId(kInvalidUniqueId)
, mSafeZoneId(kInvalidUniqueId)
, mBlobEffectId(kInvalidUniqueId)
, mCollisionActorId(kInvalidUniqueId)
, mUnusedZeros() // value-initialised: both zero (0xA5B4 fn_44_A5D0 fills two zero entries)
, mTargetPos(CVector3f::Zero())
, mModelScale(modelData.GetScale())
, mAttackTimer(0.f)
, mLeashTimer(0.f)
, mStunTimer(0.f)
, mPuddleHitTimer(0.f)
// The retail takes the health out of the patterned info, not out of the actor (asm 0xA1BC reads
// the CPatternedInfo's health at +0x54), so the actor's own health info is not read here.
, mHpTracker(pInfo.GetHealthInfo().GetHP())
, mKnockDamage(0.f)
, mTimerD1C(0.f)
, mMoveAwayTimer(0.f)
// 0xA25C stfs f1 (0.0f), 0xd24: the REL's only access of that word, so the host has no member.
, mSafeZoneEnterCount(0)
, mEvaporate(0.f)
, mDamageFxTimer(0.f)
, mIntoCorporealAnim(-1)
, mIntoStunnedAnim(-1)
, mLockOnLocator(CSegId::Invalid())
, mLastLoggedState(nullptr)
, mPathArea(nullptr)
// The retail clears every flag of the byte at 0xd3d and every flag of the byte at 0xd3e in this
// order (asm 0xA284-0xA2DC and asm 0xA2E4-0xA330), most significant bit first. The flags are
// the retail's own bits: the host's bitfields are declared in the guest's LSB-first order, so
// the host's two bytes match the retail's byte for byte.
, mHasPatrolPath(false)           // 0xd3d 0x80 (asm 0xA284)
, mPathFindActive(false)          // 0xd3d 0x40 (asm 0xA290)
, mPointPathFinding(false)        // 0xd3d 0x20 (asm 0xA29C)
, mFoundMovementPos(false)        // 0xd3d 0x10 (asm 0xA2A8)
, mRecalculatePath(false)         // 0xd3d 0x08 (asm 0xA2B4)
, mIdle(false)                    // 0xd3d 0x04 (asm 0xA2C0)
, mFollowingAttackPath(false)     // 0xd3d 0x02 (asm 0xA2CC)
, mAttackPatternProjectile(false) // 0xd3d 0x01 (asm 0xA2D8)
, mPuddleHitByWeapon(false)       // 0xd3e 0x80 (asm 0xA2E4)
, mCancelStun(false)              // 0xd3e 0x40 (asm 0xA2F0)
, mKnockAnimCancel(false)         // 0xd3e 0x20 (asm 0xA2FC)
, mTouched(false)                 // 0xd3e 0x10 (asm 0xA308)
, mShouldMoveUnderBall(false)     // 0xd3e 0x08 (asm 0xA314)
, mInsideSafeZone(false)          // 0xd3e 0x04 (asm 0xA320)
, mShouldEvaporate(false)         // 0xd3e 0x02 (asm 0xA32C)
{
  // fn_44_9D58 locks the projectile's WPSC token (0x918, 0xA350 bl Lock); without it
  // LaunchProjectile's TryCache never succeeds.
  mProjectileInfo.Token().Lock();
  // 0xA354 li r0,1; 0xA35C stw r0,0xc24: the surface helper (0xbd8) mode word at +0x4c is 1.
  mSurfaceAlignment.SetMode(static_cast< CSurfaceAlignmentHelper::EMode >(1));
  // The search's size from the model scale copied to 0xcf8: 0xA364 lfs f2, rodata_38 (3.0f);
  // 0xA374 lfs f1, rodata_48 (5.0f); 0xA37C f0 = 3 * scale.y (0xcfc), 0xA388 stfs 0xa34
  // (search+0xd4, mChRadius); 0xA390 f0 = 5 * scale.z (0xd00), 0xA394 stfs 0xa30 (search+0xd0,
  // mChHeight). So height 5*z, radius 3*y (the port had them swapped).
  mPathFindSearch.SetCharacterRadius(mModelScale.GetY() * 3.f);
  mPathFindSearch.SetCharacterHeight(mModelScale.GetZ() * 5.f);
  // 0xA39C rlwimi r0,r5,6,25,25 (r5 = 0) on 0x6b0 (knock-back manager 0x620 + 0x90, bit 0x40):
  // the hurl velocity off.
  KnockBackController().SetHurlVelocityEnabled(false);
  // 0xA3A4 knock-back physics off; 0xA3B4 the animation reaction range (2, 2).
  KnockBackController().EnableKnockBackPhysics(false);
  KnockBackController().SetAnimReactionRange(CKnockBackMgr::kAR_KnockBack,
                                             CKnockBackMgr::kAR_KnockBack);
  // 0xA3C0 rlwimi r0,r3,4,27,27 (r3 = 0) on 0x681 (manager + 0x61, bit 0x10): burn off.
  KnockBackController().EnableBurn(false);
  // The lock-on locator ("lockon_target_SDK", 0xA3E8 stb 0xd3c) replaces mLockOnTarget (0x7b8)
  // when present (0xA3F0 cmplwi 0xff / 0xA3F4 beq), and the line-of-sight tracker casts from
  // "Eye_LCTR" (bss_40, 0xA420 stb 0xc8a). The retail reads the anim data with no null test
  // (0xA3D8); the host's guard is for a model without animation.
  if (const CAnimData* animData = GetModelData()->GetAnimationData()) {
    mLockOnLocator = animData->GetLocatorSegId(rstl::string_l("lockon_target_SDK"));
    if (mLockOnLocator != CSegId::Invalid()) {
      mLockOnTarget = mLockOnLocator;
    }
    mLineOfSight.SetSegment(animData->GetLocatorSegId(rstl::string_l("Eye_LCTR")));
  }
  // 0xA430-0xA444: the tracker's filter (0xc90..0xca0) = bss_0, which fn_44_ABCC builds as
  // include 1<<59 (Solid, data_4 = 0x3B), exclude 1<<32 | 1<<47 (Player 0x20 / CollisionActor
  // 0x2F, data_8/data_C), type 3 (IncludeExclude, 0xAC30 li r0,3).
  mLineOfSight.SetRayFilter(CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid), CMaterialList(kMT_Player, kMT_CollisionActor)));
  // 0xA44C rlwimi r0,r5,7,24,24 (r5 = 0) on 0x34d (bit 0x80): mLookAtDeathDir off.
  mLookAtDeathDir = false;
  // 0xA454-0xA468: 0x1c0..0x1c8 (CPhysicsActor's momentum) = sZeroVector.
  SetMomentumWR(CVector3f::Zero());
  if (const CAnimData* animData = GetModelData()->GetAnimationData()) {
    // fn_44_AB78 keeps an animation only when its score is above FLT_EPSILON.
    // 0xA4E4 li r4,0xc (Generate), FromEnum(0) -> 0xd2c; 0xA57C li r4,0xa (LoopReaction),
    // FromEnum(3), FromEnum(0) -> 0xd30.
    const CPASDatabase& pas = animData->GetPASDatabase();
    mIntoCorporealAnim =
        FindBestAnimationId(pas, CPASAnimParmData(pas::kAS_Generate, CPASAnimParm::FromEnum(0)));
    mIntoStunnedAnim = FindBestAnimationId(
        pas, CPASAnimParmData(pas::kAS_LoopReaction, CPASAnimParm::FromEnum(3),
                              CPASAnimParm::FromEnum(0)));
  }
  // 0xA5B4 fn_44_A5D0 resizes the reserved vector at 0xce0 (count) to two zero entries: the two
  // loop sfx handles at 0xce4/0xce8 (mLoopSfx, empty handles on the host).
}

// @ fn_44_97E4
// The retail destructor (fn_44_97E4, asm 0x97E4) puts the module's vtable back, tears the members
// down - the two-entry container at guest 0xce0, the surface helper at 0xbd8 (asm 0x986C), the
// two locked hit effects at 0x940/0x950 (asm 0x9898/0x98C4), the projectile's token at 0x918
// (asm 0x98E8) and the ing-spot vulnerability at 0x834 (asm 0x9904) - runs the base destructor
// (asm 0x9910) and frees the object (asm 0x9920). On the host every member owns its destruction
// and the base destructor follows them, so the body is empty. The teardown order (the container
// at 0xce0, the surface helper at 0xbd8, the two hit effects at 0x950/0x940, the projectile's
// token at 0x918, the data's vulnerability at 0x834, then the base) is the reverse of the member
// order, which is what the host's C++ destruction does. The check finds the members' own
// teardown (3 calls, 20 conditions) against the host's empty body: a tool limit, not a mismatch.
// The container at 0xce0 holds two zero ints that nothing reads (fn_44_A5D0 only fills them in the
// constructor).
// The retail's destructor runs the module's own copies of the members' destructors as well (asm
// 0x9910): fn_44_9940 -> fn_44_9B24 (the vtable restore and the CToken at +0x04) and
// fn_44_9BB8 -> fn_44_9C10 -> fn_44_9C68 -> fn_44_9CC0 (the two-bit counter in the byte at +0x00,
// its bit 5 the "alive" flag, and the release of the pointer at +0x0c). The host's members own
// their destruction, so the body stays empty.
CMinorIng::~CMinorIng() {}

void CMinorIng::LogState(const char* name) {
  if (DebugMinorIng() && mLastLoggedState != name) {
    mLastLoggedState = name;
    fprintf(stderr, "[minoring] %04x state %s form %d\n", GetUniqueId().Value(), name, mForm);
  }
}

// Retail activates the body controller on the create message. Kept as a guard for the
// entry points that can run before it on the host.
void CMinorIng::EnsureBodyActive(CStateManager& mgr) {
  if (BodyController() != nullptr && !BodyController()->GetIsActive()) {
    BodyController()->Activate(mgr, pas::kAS_Invalid);
  }
}

// ---- helpers -------------------------------------------------------------------------------

// @ fn_44_8BF4
CVector3f CMinorIng::RaisedPoint(const CVector3f& pos) const {
  return CVector3f(pos.GetX(), pos.GetY(), pos.GetZ() + 1.f);
}

// @ fn_44_8B38
bool CMinorIng::ProjectOntoMesh(CVector3f& pos) {
  pos = RaisedPoint(pos);
  float height = 0.f;
  if (mPathFindSearch.GetHeightOfPointAboveMesh(pos, height, 0.f) != CPathFindSearch::kR_Success) {
    return false;
  }
  if (height > 0.f) {
    pos.SetZ(pos.GetZ() - (height - 0.5f));
  }
  return true;
}

// @ fn_44_5D70
bool CMinorIng::IsTargetInRange(CStateManager& mgr, const CVector3f& pos) const {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId));
  if (target == nullptr) {
    return false;
  }
  const bool moving = mPathFindActive || mPointPathFinding || mIdle;
  const float range = mMaxAttackRange - (moving ? mMaxAttackRange * 0.2f : 0.f);
  return (target->GetTranslation() - pos).MagSquared() < range * range;
}

// @ fn_44_5E94
bool CMinorIng::IsTargetInAttackAngle(CStateManager& mgr) const {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId));
  if (target == nullptr) {
    return false;
  }
  const CVector3f toSelf = -(target->GetTranslation() - GetTranslation());
  return CVector3f::GetAngleDiff(target->GetTransform().GetForward(), toSelf) <
         mData.mAttackAngleLimit * 0.017453292f;
}

// @ fn_44_88DC
bool CMinorIng::IsMoveClear(CStateManager& mgr, const CVector3f& offset, bool checkStatic) const {
  const_cast< CMinorIng* >(this)->SnapToFloor(mgr); // 0x88FC: retail snaps first, even here
  if ((mData.mFlags156 & 0x20) != 0) {
    return true;
  }
  const CCollisionPrimitive* prim = GetCollisionPrimitive();
  if (prim == nullptr) {
    return true;
  }
  CTransform4f xf = PrimitiveTransform(true);
  xf.SetTranslation(xf.GetTranslation() + offset);
  const CMaterialFilter filter =
      CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList(kMT_Player));
  if (checkStatic && CGameCollision::DetectStaticCollisionBoolean(mgr, *prim, xf, filter)) {
    return false;
  }
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, prim->CalculateAABox(xf), filter, this);
  if (nearList.empty()) {
    return !mgr.GetSafeZoneManager()->PointIsInSafeZone(mgr, xf.GetTranslation());
  }
  for (int i = 0; i < nearList.size(); ++i) {
    const CCollisionActor* colAct =
        TCastToConstPtr< CCollisionActor >(mgr.GetObjectById(nearList[i]));
    if (colAct == nullptr || colAct->GetOwnerId() != GetUniqueId()) {
      return false;
    }
  }
  return true;
}

// @ fn_44_2598
CScriptAIHint* CMinorIng::GetCoverHint(CStateManager& mgr, TUniqueId id) const {
  if (id == kInvalidUniqueId) {
    return nullptr;
  }
  return TCastToPtr< CScriptAIHint >(mgr.ObjectById(id));
}

// @ fn_44_2550
// The retail helper puts the hint into use (the DOL call at asm 0x2574) and writes the hint's own
// id through the out parameter (asm 0x2578-0x257C); the caller passes its cover-hint slot.
void CMinorIng::ClaimCoverHint(CScriptAIHint* hint, TUniqueId& id) {
  hint->SetInUse(true);
  id = hint->GetUniqueId();
}

// @ fn_44_24D4
void CMinorIng::ReleaseCoverHint(CStateManager& mgr, TUniqueId& id, bool keepTimer) {
  if (CScriptAIHint* hint = GetCoverHint(mgr, id)) {
    hint->SetInUse(false);
    if (!keepTimer) {
      hint->SetTimeRemaining(0.f); // hint+0x170
    }
    id = kInvalidUniqueId;
  }
}

// @ fn_44_343C
void CMinorIng::ResetAttackTimer(CStateManager& mgr) {
  mAttackTimer = GetAverageAttackTime();
  // retail tests bit 7 of mgr+0x16e8, the byte after the CRandom16 at 0x16e4.
  if (mgr.IsRandomAvailable()) {
    mAttackTimer += mgr.Random()->Float() * mAttackTimeVariation;
  }
}

// @ fn_44_3B0C
void CMinorIng::PlaySfx(int idx, bool start) {
  if (idx < 0 || idx >= 6) {
    return;
  }
  const bool looped = idx == 0 || idx == 1;
  if (!start) {
    if (looped) {
      CSfxManager::SfxStop(mLoopSfx[idx]);
      mLoopSfx[idx] = CSfxHandle();
    }
    return;
  }
  ushort id = CSfxManager::kInternalInvalidSfxId;
  switch (idx) {
  case 0:
    id = mData.mIngSpotIdleSound;
    break;
  case 1:
    id = mData.mIngSpotMoveSound;
    break;
  case 3:
    id = mData.mIngSpotDeathSound;
    break;
  case 4:
    id = mData.mIngSpotHitNormalSound;
    break;
  case 5:
    id = mData.mIngSpotHitHeavySound;
    break;
  default:
    break;
  }
  if (id == CSfxManager::kInternalInvalidSfxId) {
    return;
  }
  CAudioSys::C3DEmitterParmData parms(mData.mIngSpotUnknown7569, mData.mIngSpotSfxFallOff, 1, 0x7f,
                                      0x14);
  parms.mPos = GetTranslation();
  parms.mDir = CVector3f::Zero();
  parms.mSfxId = id;
  const CSfxHandle handle =
      CSfxManager::AddEmitter(parms, GetCurrentAreaId().Value(), true, looped,
                              CSfxManager::kMedPriority);
  if (looped) {
    mLoopSfx[idx] = handle;
  }
}

// @ fn_44_39C4
void CMinorIng::UpdateLoopSfx() {
  bool stopMove = true;
  bool stopIdle = true;
  if (mForm == kF_Puddle) {
    if (!mPathFindActive && !mPointPathFinding && !mFollowingAttackPath) {
      stopIdle = false;
      if (!mLoopSfx[0]) {
        PlaySfx(0, true);
      } else {
        CSfxManager::UpdateEmitter(mLoopSfx[0], GetTranslation(), GetTransform().GetForward(),
                                   0x7f);
      }
    } else {
      stopMove = false;
      if (!mLoopSfx[1]) {
        PlaySfx(1, true);
      } else {
        CSfxManager::UpdateEmitter(mLoopSfx[1], GetTranslation(), GetTransform().GetForward(),
                                   0x7f);
      }
    }
  }
  if (stopMove) {
    PlaySfx(1, false);
  }
  if (stopIdle) {
    PlaySfx(0, false);
  }
}

// @ fn_44_396C
void CMinorIng::StopSfx() {
  PlaySfx(0, false);
  PlaySfx(1, false);
}

// @ fn_44_3CB4
void CMinorIng::UpdateTimers(float dt) {
  if (mForm == kF_Puddle && mCurDamageRemTime > 0.f) {
    mCurDamageRemTime -= dt;
  }
  mAttackTimer -= dt;
  mPuddleHitTimer -= dt;
  mTimerD1C -= dt;
  mDamageFxTimer -= dt;
}

// @ fn_44_3E20
void CMinorIng::UpdateLocomotionType() {
  const pas::ELocomotionType type =
      (mForm == kF_Puddle || mForm == kF_IntoPuddle) // 0x3E34-0x3E58: forms 1 and 3
          ? pas::kLT_Relaxed
          : pas::kLT_Combat;
  if (type != BodyController()->GetLocomotionType()) {
    BodyController()->SetLocomotionType(type);
  }
}

// @ fn_44_43F4
void CMinorIng::SetForm(CStateManager& mgr, int form, bool setLocomotion) {
  if (form == mForm) {
    return;
  }
  const bool keepTargetable = (mData.mFlags156 & 0x04) != 0;
  if (form == kF_Puddle) {
    if (!keepTargetable) {
      RemoveMaterial(kMT_Target, kMT_SeekerTarget, mgr);
    }
    AddMaterial(kMT_NonSolidDamageable, mgr);
  } else {
    if (!keepTargetable) {
      AddMaterial(kMT_Target, kMT_SeekerTarget, mgr);
    }
    RemoveMaterial(kMT_NonSolidDamageable, mgr);
  }
  SetCollisionActorActive(mgr, form != kF_Puddle);
  if (form == kF_IntoCorporeal) {
    SnapToFloor(mgr);
  }
  const float fade = mPuddleHitByWeapon ? 0.021f : 0.33f;
  switch (form) {
  case kF_Corporeal:
  case kF_IntoCorporeal:
    mAlphaDelta = 1.f / fade;
    break;
  case kF_Puddle:
    mAlphaDelta = -(1.f / fade);
    break;
  case kF_IntoPuddle:
    mAlphaDelta = 0.f;
    break;
  default:
    break;
  }
  mForm = form;
  if (setLocomotion) {
    UpdateLocomotionType();
  }
}

// @ fn_44_34B0
// The retail helper fires a ray from one unit above the actor, straight down its up axis, ten
// units long, against Solid only - the material 0x3b of the module's data, shifted into the mask
// at asm 0x34E8-0x350C - and writes the hit point, or the zero vector when the ray hits nothing
// (asm 0x35A8-0x35DC). The copy of the result is the retail's fn_44_3614.
void CMinorIng::RayDown(CVector3f& out, CStateManager& mgr) const {
  const CVector3f up = GetTransform().GetUp();
  const CRayCastResult result =
      mgr.RayStaticIntersection(GetTranslation() + up, up * -1.f, 10.f, SolidFilter());
  CRayCastResult copy;
  CopyRayCastResult(copy, result);
  out = copy.IsValid() ? copy.GetPoint() : CVector3f::Zero();
}

// @ fn_44_3670
void CMinorIng::SnapToFloor(CStateManager& mgr) {
  CVector3f point;
  RayDown(point, mgr);
  if (point != CVector3f::Zero()) {
    SetTranslation(point);
  }
}

// @ fn_44_883C
bool CMinorIng::UpdateTargetPos(CStateManager& mgr) {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId));
  if (target != nullptr) {
    mTargetPos = target->GetAimPosition(mgr, 0.2f);
  }
  return target != nullptr;
}

// @ fn_44_5AC0
void CMinorIng::UpdateBallPursuit(CStateManager& mgr) {
  CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamId));
  const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mTargetId));
  // The morph state at player+0x38c counts only when the word at +0x390 is 0 (asm 0x5BF4-0x5C0C).
  const CPlayer::EPlayerMorphBallState morphState =
      player != nullptr && player->GetSpawnedMorphballState() == CPlayer::kMS_Unmorphed
          ? player->GetMorphballTransitionState()
          : CPlayer::kMS_Unmorphed;
  if (player != nullptr && morphState == CPlayer::kMS_Morphed &&
      (player->GetTranslation() - GetTranslation()).MagSquared() <
          mData.mMorphballPursuitDistance * mData.mMorphballPursuitDistance) {
    const CTeamAiRole* role = CScriptTeamAiMgr::GetTeamAiRole(mgr, mTeamId, GetUniqueId());
    if (role == nullptr || role->GetTeamAiRole() != CTeamAiRole::kTAR_Initial) {
      if (mTeamId == kInvalidUniqueId ||
          (team != nullptr && team->IsMeleeAttacking(GetUniqueId())) ||
          CScriptTeamAiMgr::StartAttack(CScriptTeamAiMgr::kAT_Melee, mgr, mTeamId,
                                        GetUniqueId())) {
        mShouldMoveUnderBall = true;
        return;
      }
    }
  }
  if (team != nullptr && team->IsMeleeAttacking(GetUniqueId())) {
    EndAttack(mgr, false);
  }
  mShouldMoveUnderBall = false;
}

// @ fn_44_6FCC
void CMinorIng::UpdateValidTarget(CStateManager& mgr) {
  if (mForm == kF_Puddle) {
    SetValidTarget(0, mgr.GetPlayerState(0)->GetActiveVisor(mgr) == CPlayerState::kPV_Dark ||
                          (mData.mFlags156 & 0x04) != 0);
  } else {
    SetValidTarget(0, true);
  }
}

// @ fn_44_36FC
void CMinorIng::UpdateSafeZone(CStateManager& mgr) {
  if (mSafeZoneId == kInvalidUniqueId) {
    return;
  }
  const CScriptSafeZone* zone = TCastToConstPtr< CScriptSafeZone >(mgr.GetObjectById(mSafeZoneId));
  if (zone == nullptr || !zone->GetActive() ||
      zone->GetZoneType() != static_cast< CScriptSafeZone::EZoneType >(2)) {
    mSafeZoneId = kInvalidUniqueId;
  }
}

// @ fn_44_3780
void CMinorIng::PickSafeZone(CStateManager& mgr, const CVector3f& pos) {
  const float distSq = (pos - GetTranslation()).MagSquared();
  // 0x37E4..0x37EC fcmpo, cror eq,lt,eq, bne to the return: a NaN distance returns too.
  if (!(distSq <= mData.mHearingRadius * mData.mHearingRadius)) {
    return;
  }
  const TUniqueId zoneId = mgr.GetSafeZoneManager()->PointIsInWhichSafeZone(mgr, pos);
  if (mSafeZoneId == kInvalidUniqueId) {
    mSafeZoneId = zoneId;
    return;
  }
  const CScriptSafeZone* zone = TCastToConstPtr< CScriptSafeZone >(mgr.GetObjectById(mSafeZoneId));
  if (zone == nullptr || distSq < (zone->GetTranslation() - GetTranslation()).MagSquared()) {
    mSafeZoneId = zoneId;
  }
}

// @ fn_44_593C
void CMinorIng::JoinTeam(CStateManager& mgr) {
  if (mTeamId == kInvalidUniqueId) {
    mTeamId = CScriptTeamAiMgr::GetAssociatedTeamId(*this, mgr);
  }
  if (mTeamId != kInvalidUniqueId) {
    if (CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamId))) {
      team->JoinTeam(*this, CTeamAiRole::kTAR_Projectile, CTeamAiRole::kTAR_Melee,
                     CTeamAiRole::kTAR_Invalid);
    }
  }
}

// @ fn_44_5894
// The retail helper leaves the team the actor is in: it looks the team up (asm 0x58D4), asks
// whether the actor is still one of its members (asm 0x58F4), leaves it (asm 0x5914) and clears
// the team id (asm 0x5920).
void CMinorIng::LeaveTeam(CStateManager& mgr) {
  if (mTeamId == kInvalidUniqueId) {
    return;
  }
  CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamId));
  if (team == nullptr || !team->IsPartOfTeam(GetUniqueId())) {
    return;
  }
  team->QuitTeam(GetUniqueId());
  mTeamId = kInvalidUniqueId;
}

// @ fn_44_5844
void CMinorIng::EndAttack(CStateManager& mgr, bool projectile) {
  CScriptTeamAiMgr::EndAttack(projectile ? CScriptTeamAiMgr::kAT_Projectile
                                         : CScriptTeamAiMgr::kAT_Melee,
                              mgr, mTeamId, GetUniqueId(), true);
}

// @ fn_44_55E0
void CMinorIng::ApplySeparation(CStateManager& mgr) {
  CVector3f total = CVector3f::Zero();
  const CObjectList& list = mgr.GetObjectListById(kOL_ListeningAi);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CPatterned* other = TCastToConstPtr< CPatterned >(list[i]);
    if (other != nullptr && other != this && other->GetCurrentAreaId() == GetCurrentAreaId()) {
      const CVector2f separation = mSteeringBehaviors.Separation2D(
          *this, CVector2f(other->GetTranslation().GetX(), other->GetTranslation().GetY()),
          mModelScale.GetX() * 10.f);
      const CVector3f separation3(separation.GetX(), separation.GetY(), 0.f);
      if (separation3.IsMagnitudeSafe()) {
        total += separation3;
      }
    }
  }
  if (total != CVector3f::Zero()) {
    BodyController()->CommandMgr().DeliverCmd(CBCLocomotionCmd(total, CVector3f::Zero(), 0.5f));
  }
}

// @ fn_44_6900
void CMinorIng::MoveAlongPath(CStateManager& mgr, float dt) {
  CVector3f move = BodyController()->CommandMgr().GetMoveVector();
  BodyController()->CommandMgr().ClearLocomotionCmds();
  if (!move.IsMagnitudeSafe() || !(dt > 0.f)) {
    return;
  }
  const CVector3f face = move.AsNormalized();
  float speed = mData.mIngSpotMaxSpeed;
  if (mSafeZoneId == kInvalidUniqueId && mSafeZoneEnterCount == 0) {
    if (mShouldMoveUnderBall) {
      speed = mData.mIngSpotBallPursuitSpeed;
    }
  } else {
    speed *= mData.mSpeedModifier;
  }
  MoveOnSurface(speed, dt, move);
  BodyController()->FaceDirection(face, dt);
}

// @ fn_44_5384
void CMinorIng::MoveOnSurface(float speed, float dt, const CVector3f& moveIn) {
  const CVector3f pos = GetTranslation();
  const CVector3f normal = mSurfaceAlignment.GetSurface().GetNormal();
  CVector3f move = moveIn;
  const float along = CVector3f::Dot(move, normal);
  if (along * along < move.MagSquared() * 0.6f) {
    move = move - normal * along;
  }
  if (!move.IsMagnitudeSafe()) {
    mSurfaceMoveDir = CVector3f::Zero();
  } else {
    const CVector3f dir = move.AsNormalized();
    const CVector3f next = pos + dir * (speed * dt);
    bool apply = true;
    if (!mPointPathFinding && !mFollowingAttackPath &&
        mPathFindSearch.OnPath(RaisedPoint(pos)) == CPathFindSearch::kR_Success &&
        mPathFindSearch.OnPath(RaisedPoint(next)) != CPathFindSearch::kR_Success) {
      apply = false;
    }
    if (apply) {
      SetTranslation(next);
      mSurfaceMoveDir = dir;
    }
  }
  Stop();
}

// ---- triggers ------------------------------------------------------------------------------

// @ fn_44_6184
bool CMinorIng::PathShagged(CStateManager& mgr, const CTriggerData& data) const {
  return CPatterned::PathShagged(mgr, data) ||
         mPathFindSearch.OnPath(RaisedPoint(GetTranslation())) != CPathFindSearch::kR_Success;
}

// @ fn_44_5D34
bool CMinorIng::ShouldMoveUnderBall(CStateManager&, const CTriggerData&) const {
  return mShouldMoveUnderBall;
}

// @ fn_44_6178
bool CMinorIng::HasPatrolPath(CStateManager&, const CTriggerData&) const { return mHasPatrolPath; }

// @ fn_44_6144
bool CMinorIng::PlayerLeashReached(CStateManager&, const CTriggerData&) const {
  return mTargetId == kInvalidUniqueId || mPlayerLeashTime < mLeashTimer;
}

// @ fn_44_60F4
bool CMinorIng::LeavePatrol(CStateManager& mgr, const CTriggerData& data) const {
  return mTargetId != kInvalidUniqueId || InDetectionRange(mgr, data);
}

// @ fn_44_60C4
bool CMinorIng::LineOfSight(CStateManager&, const CTriggerData&) const {
  // 0xcc0 bit 7 and 0xcb8 are the tracker's flag and clear time (tracker at 0xc88).
  return mLineOfSight.HasLineOfSight() && mLineOfSight.GetClearTime() > 0.f;
}

// @ fn_44_5E70
bool CMinorIng::InMaxRange(CStateManager& mgr, const CTriggerData&) const {
  return IsTargetInRange(mgr, GetTranslation());
}

// @ fn_44_5F60
bool CMinorIng::ShouldFire(CStateManager& mgr, const CTriggerData&) const {
  const CTriggerData zero(0.f);
  // 0x5F9C..0x5FA4 fcmpo, cror eq,lt,eq, bne to false: a NaN timer answers false too.
  if (mTouched || !(mAttackTimer <= 0.f) || !IsTargetInAttackAngle(mgr) || !InMaxRange(mgr, zero) ||
      !LineOfSight(mgr, zero) || !IsMoveClear(mgr, CVector3f::Zero(), true)) {
    return false;
  }
  const CTeamAiRole* role = CScriptTeamAiMgr::GetTeamAiRole(mgr, mTeamId, GetUniqueId());
  if (role != nullptr && role->GetTeamAiRole() == CTeamAiRole::kTAR_Initial) {
    return false;
  }
  if (mTeamId != kInvalidUniqueId &&
      !CScriptTeamAiMgr::StartAttack(CScriptTeamAiMgr::kAT_Projectile, mgr, mTeamId,
                                     GetUniqueId())) {
    return false;
  }
  return true;
}

// @ fn_44_5D64
bool CMinorIng::PuddleHitByWeapon(CStateManager&, const CTriggerData&) const {
  return mPuddleHitByWeapon;
}

// @ fn_44_5D58
bool CMinorIng::CancelStun(CStateManager&, const CTriggerData&) const { return mCancelStun; }

// @ fn_44_5D40
bool CMinorIng::IsBeingKnockedback(CStateManager&, const CTriggerData&) const {
  return GetBodyController()->GetCurrentStateId() == pas::kAS_KnockBack;
}

// @ fn_44_4D84
bool CMinorIng::TargetInSafeZone(CStateManager& mgr, const CTriggerData&) const {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId));
  return target != nullptr && mgr.GetSafeZoneManager()->IsObjectInSafeZone(*target, mgr);
}

// @ fn_44_4D4C
bool CMinorIng::HasCoverPoint(CStateManager& mgr, const CTriggerData&) const {
  return GetCoverHint(mgr, mCoverHintId) != nullptr;
}

// @ fn_44_4A74
bool CMinorIng::PointPathOver(CStateManager&, const CTriggerData&) const {
  const bool over = mIngSpotPathFindNavigation.IsPathOver(*this);
  if (over) {
    const_cast< CMinorIng* >(this)->mPointPathFinding = false;
  }
  return over;
}

// @ fn_44_4A40
bool CMinorIng::FoundPointPath(CStateManager&, const CTriggerData&) const {
  return mIngSpotPathFindNavigation.HasPath(*this);
}

// @ fn_44_4A68
bool CMinorIng::IsPointPathFinding(CStateManager&, const CTriggerData&) const {
  return mPointPathFinding;
}

// @ fn_44_3E00
bool CMinorIng::StunOver(CStateManager&, const CTriggerData&) const { return mStunTimer <= 0.f; }

// @ fn_44_3D10
bool CMinorIng::ShouldPathFind(CStateManager& mgr, const CTriggerData& data) const {
  if (!InMaxRange(mgr, data) || !LineOfSight(mgr, data) || ShouldMoveUnderBall(mgr, data)) {
    return true;
  }
  // 0x3DB0: true when the raised point is *off* the mesh (OnPath != kR_Success).
  return !mPointPathFinding &&
         mPathFindSearch.OnPath(RaisedPoint(GetTranslation())) != CPathFindSearch::kR_Success;
}

// @ fn_44_3DE0
bool CMinorIng::CancelAnim(CStateManager&, const CTriggerData&) const {
  return mKnockAnimCancel || mShouldEvaporate;
}

// @ fn_44_62A8
bool CMinorIng::HasAttackPattern(CStateManager& mgr, const CTriggerData&) const {
  return GetConnectedObject(mgr, kSS_Attack, kSM_Follow) != kInvalidUniqueId;
}

// @ fn_44_6268
bool CMinorIng::AttackPatternOver(CStateManager&, const CTriggerData&) const {
  if (mFollowingAttackPath) {
    const_cast< CMinorIng* >(this)->mFollowingAttackPath =
        mWaypointNavigation.GetDestination() != kInvalidUniqueId;
  }
  return !mFollowingAttackPath;
}

// @ fn_44_6210
bool CMinorIng::IsFollowingAttackPath(CStateManager&, const CTriggerData&) const {
  return mFollowingAttackPath;
}

// @ fn_44_6240
bool CMinorIng::AllowAttackPatternProjectile(CStateManager&, const CTriggerData&) const {
  return mAttackPatternProjectile || (mData.mFlags156 & 0x80) != 0;
}

// @ fn_44_6234
bool CMinorIng::AllowAttackPatternStun(CStateManager&, const CTriggerData&) const {
  return (mData.mFlags156 & 0x40) != 0;
}

// @ fn_44_6228
bool CMinorIng::RecalculatePath(CStateManager&, const CTriggerData&) const {
  return mRecalculatePath;
}

// @ fn_44_621C
bool CMinorIng::InsideSafeZone(CStateManager&, const CTriggerData&) const {
  return mInsideSafeZone;
}

// @ fn_44_6300
bool CMinorIng::IsMoveAwayFromSafeZoneOver(CStateManager& mgr, const CTriggerData& data) const {
  if (!mFoundMovementPos) {
    return false;
  }
  if (!mPointPathFinding) {
    const bool shagged = PathShagged(mgr, data);
    return CPatterned::PathOver(mgr, data) || shagged;
  }
  return PointPathOver(mgr, data);
}

// @ fn_44_3430
bool CMinorIng::FoundMovementPos(CStateManager&, const CTriggerData&) const {
  return mFoundMovementPos;
}

// @ fn_44_5D28
bool CMinorIng::ShouldEvaporate(CStateManager&, const CTriggerData&) const {
  return mShouldEvaporate;
}

// @ fn_44_5D08
bool CMinorIng::IsInCorporealForm(CStateManager&, const CTriggerData&) const {
  return mForm == kF_Corporeal || mForm == kF_IntoCorporeal;
}

// @ fn_44_5CFC
bool CMinorIng::StayOnPointPathFinding(CStateManager&, const CTriggerData&) const {
  return (mData.mFlags156 & 0x10) != 0;
}

// @ fn_44_5CF0
bool CMinorIng::AllowProjectileDuringStayOnPointPathFinding(CStateManager&,
                                                            const CTriggerData&) const {
  return (mData.mFlags156 & 0x08) != 0;
}

// @ fn_44_2038
bool CMinorIng::StateOver(CStateManager&, const CTriggerData&) const {
  return mAnimationState.GetState() == CAnimationState::kAS_Over;
}

// @ fn_44_5CAC
bool CMinorIng::IsLuredBySafeZone(CStateManager&, const CTriggerData&) const {
  return mSafeZoneId != kInvalidUniqueId && mForm >= kF_Corporeal && mForm < kF_IntoCorporeal;
}

// ---- states --------------------------------------------------------------------------------

// @ fn_44_8128
void CMinorIng::PathFind(CStateManager& mgr, EStateMsg msg, float dt) {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId));
  if (msg == kStateMsg_Activate) {
    if (!mIdle) {
      LogState("PathFind");
    }
    ReleaseCoverHint(mgr, mCoverHintId, false);
    mRecalculatePath = false;
    mPathFindActive = true;
    if (!mIdle && mSafeZoneId == kInvalidUniqueId) {
      SelectTarget(mgr, dt);
    }
  } else if (msg == kStateMsg_Update) {
    if (target == nullptr) {
      mTargetId = kInvalidUniqueId;
    }
  } else if (msg == kStateMsg_Deactivate) {
    mPathFindActive = false;
    mFoundMovementPos = false;
  }
  if (mPathFindActive && mFoundMovementPos) {
    mPathFindNavigation.PathFind(mgr, msg, dt, *this);
    ApplySeparation(mgr);
    MoveAlongPath(mgr, dt);
  }
}

// @ fn_44_7F98
void CMinorIng::PointPathFind(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    if (!mIdle) {
      LogState("PointPathFind");
    }
    mPathFindActive = true;
    mPointPathFinding = true;
  } else if (msg == kStateMsg_Deactivate) {
    mPathFindActive = false;
    mFoundMovementPos = false;
    mLastCoverId = kInvalidUniqueId;
  }
  if (msg == kStateMsg_Activate || mIngSpotPathFindNavigation.HasPath(*this)) {
    mIngSpotPathFindNavigation.PathFind(mgr, msg, dt, *this);
    const CVector3f move = BodyController()->CommandMgr().GetMoveVector();
    BodyController()->CommandMgr().ClearLocomotionCmds();
    float speed = CVector3f::GetAngleDiff(CVector3f::Up(), GetTransform().GetUp()) / 3.1415927f *
                      (mData.mIngSpotMaxWallSpeed - mData.mIngSpotMaxSpeed) +
                  mData.mIngSpotMaxSpeed;
    if (mSafeZoneId != kInvalidUniqueId || mSafeZoneEnterCount != 0) {
      speed *= mData.mSpeedModifier;
    }
    MoveOnSurface(speed, dt, move);
  }
}

// @ fn_44_85DC
void CMinorIng::Stunned(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    LogState("Stunned");
    mStunTimer = mSafeZoneEnterCount == 0 ? mData.mBombStunDuration : 2.f;
    SetForm(mgr, kF_IntoCorporeal, false);
    mAnimationState.SetState(CAnimationState::kAS_Ready);
  } else if (msg == kStateMsg_Update) {
    mStunTimer -= dt;
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_LoopReaction)) {
      BodyController()->CommandMgr().DeliverCmd(CBCLoopReactionCmd(pas::kRT_Three));
    }
    if (StunOver(mgr, CTriggerData(0.f))) {
      BodyController()->CommandMgr().DeliverCmd(kBSC_ExitState);
      SetForm(mgr, kF_IntoPuddle, mCancelStun);
    }
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    SetForm(mgr, kF_Puddle, false);
    mPuddleHitByWeapon = false;
    mCancelStun = false;
    mPuddleHitTimer = 0.f;
    mTimerD1C = 1.f;
    ResetAttackTimer(mgr);
  }
}

// @ fn_44_777C
void CMinorIng::ProjectileAttack(CStateManager& mgr, EStateMsg msg, float) {
  if (msg == kStateMsg_Activate) {
    LogState("ProjectileAttack");
    mAnimationState.SetState(CAnimationState::kAS_Ready);
  } else if (msg == kStateMsg_Update) {
    if (mForm == kF_Corporeal &&
        mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_ProjectileAttack)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCProjectileAttackCmd(pas::kS_Zero, mTargetPos, false));
    }
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    EndAttack(mgr, true);
    mAttackPatternProjectile = false;
  }
}

// @ fn_44_7740
void CMinorIng::Patrol(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    LogState("Patrol");
    mTargetId = kInvalidUniqueId;
  }
  CPatterned::Patrol(mgr, msg, dt);
}

// @ fn_44_6EB8
void CMinorIng::Dead(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    LogState("Dead");
  }
  if (msg != kStateMsg_Update) {
    return;
  }
  mEvaporate = mEvaporate + dt;
  if (mEvaporate > 2.f) {
    mEvaporate = 2.f;
  }
  if (mShouldEvaporate) {
    // Retail waits while the blob effect's particle system still has particles (CElementGen
    // vtable 0x74, GetParticleCount).
    bool remove = true;
    if (CEntity* ent = mgr.ObjectById(mBlobEffectId)) {
      CElementGen* gen = static_cast< CCollisionTracker* >(ent)->ParticleSystem();
      if (gen != nullptr && gen->GetParticleCount() != 0) {
        remove = false;
      }
    }
    if (remove) {
      mgr.DeleteObjectRequest(GetUniqueId());
    }
  }
}

// @ fn_44_5264
void CMinorIng::SetWallPointCoverDest(CStateManager& mgr, EStateMsg msg, float) {
  if (msg != kStateMsg_Activate) {
    return;
  }
  LogState("SetWallPointCoverDest");
  mFoundMovementPos = false;
  mPointPathFindSearch.ClearWaypoints();
  const CScriptAIHint* hint = GetCoverHint(mgr, mCoverHintId);
  if (hint == nullptr || mPathArea == nullptr) {
    return;
  }
  int from = -1;
  mPointPathFindSearch.FindClosestPhysicalPoint(GetTranslation(), from,
                                                CPathFindPointSearchFilter(20.f, 0, -1));
  if (from == -1) {
    return;
  }
  // The second filter is connected to the first point (asm 0x53B8-0x53D8: r5 = from).
  int to = -1;
  mPointPathFindSearch.FindClosestPhysicalPoint(hint->GetTranslation(), to,
                                                CPathFindPointSearchFilter(20.f, 0, from));
  if (to != -1 && PortIngPointSearch(mPointPathFindSearch, *mPathArea, from, to)) {
    mFoundMovementPos = true;
  }
}

// @ fn_44_4C9C
void CMinorIng::SetCoverDest(CStateManager& mgr, EStateMsg msg, float) {
  if (msg != kStateMsg_Activate) {
    return;
  }
  LogState("SetCoverDest");
  CVector3f dest = GetTranslation();
  mFoundMovementPos = false;
  if (const CScriptAIHint* hint = GetCoverHint(mgr, mCoverHintId)) {
    dest = hint->GetTranslation();
    mFoundMovementPos = true;
  }
  mPathFindNavigation.SetDestination(dest);
  mPathFindNavigation.SetFaceTarget(kInvalidUniqueId);
}

// @ fn_44_8460
void CMinorIng::IntoPuddleForm(CStateManager& mgr, EStateMsg msg, float) {
  if (msg == kStateMsg_Activate) {
    LogState("IntoPuddleForm");
    SetForm(mgr, kF_IntoPuddle, true);
    mAnimationState.SetState(CAnimationState::kAS_Ready);
  } else if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Generate)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCGenerateCmd(pas::kGType_One, CVector3f::Zero()));
    }
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    SetForm(mgr, kF_Puddle, true);
    ResetAttackTimer(mgr);
  }
}

// @ fn_44_82A0
void CMinorIng::IntoCorporealForm(CStateManager& mgr, EStateMsg msg, float) {
  if (msg == kStateMsg_Activate) {
    LogState("IntoCorporealForm");
    SetForm(mgr, kF_IntoCorporeal, true);
    mAnimationState.SetState(CAnimationState::kAS_Ready);
  } else if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Generate)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCGenerateCmd(pas::kGType_Zero, CVector3f::Zero()));
    }
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    SetForm(mgr, kF_Corporeal, true);
  }
}

// @ fn_44_7960
void CMinorIng::Idle(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    LogState("Idle");
    mIdle = true;
    if (!mPointPathFinding) {
      // A random point 2..10 units away at a random heading, on the mesh and clear.
      const float angle = mgr.Random()->Range(0.f, 360.f);
      const float dist = mgr.Random()->Range(2.f, 10.f);
      const CVector3f offset =
          CTransform4f::RotateZ(CRelAngle::FromDegrees(angle)) * (CVector3f::Forward() * dist);
      CVector3f point = GetTranslation() + offset + CVector3f::Up();
      if (ProjectOntoMesh(point) && IsMoveClear(mgr, point - GetTranslation(), false) &&
          mPathFindSearch.PathExists(RaisedPoint(GetTranslation()), RaisedPoint(point)) ==
              CPathFindSearch::kR_Success) {
        mPathFindNavigation.SetDestination(point);
        mFoundMovementPos = true;
      }
    } else if (CPFArea* area = mPathArea) {
      // A random neighbour of the closest physical point in the point graph.
      int closest = -1;
      mPointPathFindSearch.FindClosestPhysicalPoint(GetTranslation(), closest,
                                                    CPathFindPointSearchFilter(20.f, 0, -1));
      if (closest != -1) {
        const CPFPoint& from = area->GetPoint(closest);
        if (from.GetNumLinks() != 0) {
          const int link = from.GetLink(mgr.Random()->Range(0, from.GetNumLinks() - 1));
          if (link != -1) {
            const CPFPoint& to = area->GetPoint(link);
            if (IsMoveClear(mgr, to.GetPosition() - GetTranslation(), false) &&
                mPointPathFindSearch.Search(from, to) == CPathFindPointSearch::kR_Success) {
              mFoundMovementPos = true;
            }
          }
        }
      }
    }
  } else if (msg == kStateMsg_Deactivate) {
    mIdle = false;
  }
  if (!mPointPathFinding) {
    PathFind(mgr, msg, dt);
  } else {
    PointPathFind(mgr, msg, dt);
  }
}

// @ fn_44_6A40
void CMinorIng::FollowAttackPattern(CStateManager& mgr, EStateMsg msg, float dt) {
  mWaypointNavigation.Patrol(mgr, msg, dt, *this);
  if (msg == kStateMsg_Activate) {
    LogState("FollowAttackPattern");
    const TUniqueId dest = mAttackPathId == kInvalidUniqueId
                               ? GetConnectedObject(mgr, kSS_Attack, kSM_Follow)
                               : mAttackPathId;
    mWaypointNavigation.SetDestination(dest);
    if (const CScriptWaypoint* waypoint =
            TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(dest))) {
      if (CVector3f::Dot(waypoint->GetTranslation() - GetTranslation(),
                         GetTransform().GetForward()) <= 0.f) {
        mWaypointNavigation.SetInPosition(true);
      }
    }
    mFollowingAttackPath = true;
  } else if (msg == kStateMsg_Update) {
    if (!mAttackPatternProjectile) {
      MoveAlongPath(mgr, dt);
    }
  } else if (msg == kStateMsg_Deactivate) {
    mAttackPathLastId = mWaypointNavigation.GetLastDestination();
    mAttackPathId = mWaypointNavigation.GetDestination();
  }
}

// @ fn_44_6BD4
void CMinorIng::MoveAwayFromSafeZone(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    LogState("MoveAwayFromSafeZone");
    mInsideSafeZone = false;
    mFoundMovementPos = false;
    mPathFindSearch.SetFlags(mPathFindSearch.GetRegionFlags() & ~0x300u);
    if (mFollowingAttackPath && (mData.mFlags156 & 0x40) != 0) {
      mFollowingAttackPath = false;
    }
    mPointPathFinding = false;
    mMoveAwayTimer = 0.1f;
    for (int i = 0; i < 3; ++i) {
      FindCoverPoint(mgr, dt);
      if (!HasCoverPoint(mgr, CTriggerData(0.f))) {
        continue;
      }
      SetPointCoverDest(mgr, dt);
      if (FoundPointPath(mgr, CTriggerData(0.f))) {
        mFoundMovementPos = true;
        mPointPathFinding = true;
        break;
      }
      if (const CScriptAIHint* hint = GetCoverHint(mgr, mCoverHintId)) {
        // Retail also projects its own position onto the mesh here (fn_44_8B38) and never reads
        // the result.
        if (mPathFindSearch.PathExists(RaisedPoint(GetTranslation()), hint->GetTranslation()) ==
            CPathFindSearch::kR_Success) {
          mPathFindNavigation.SetDestination(hint->GetTranslation());
          mFoundMovementPos = true;
          break;
        }
      }
    }
    if (!mFoundMovementPos) {
      MassiveDeath(mgr); // vtable slot 0xd4
    }
  } else if (msg == kStateMsg_Update) {
    mMoveAwayTimer -= dt;
  } else if (msg == kStateMsg_Deactivate) {
    mFoundMovementPos = false;
    mMoveAwayTimer = 0.f;
    mPathFindSearch.SetFlags(mPathFindSearch.GetRegionFlags() | 0x300);
    ReleaseCoverHint(mgr, mCoverHintId, false);
  }
  if (mFoundMovementPos && (msg == kStateMsg_Activate || mMoveAwayTimer < 0.f)) {
    if (!mPointPathFinding) {
      PathFind(mgr, msg, dt);
    } else {
      PointPathFind(mgr, msg, dt);
    }
  }
}

// @ fn_44_7D20
void CMinorIng::AlwaysPointPathFind(CStateManager& mgr, EStateMsg msg, float dt) {
  const CPathFindPointSearchFilter nearSelf(mData.mUnknownD6C8EAC2, 0, -1);
  const CPathFindPointSearchFilter nearTarget(mData.mUnknown2A5449BA, 0, -1);
  if (msg == kStateMsg_Update) {
    int from = -1;
    if (mPointPathFindSearch.FindClosestPhysicalPoint(GetTranslation(), from, nearSelf) ==
            CPathFindPointSearch::kCPR_Success &&
        from != -1) {
      CVector3f toTarget = mTargetPos - GetTranslation();
      if (toTarget.CanBeNormalized() &&
          toTarget.MagSquared() < mData.mUnknownD6C8EAC2 * mData.mUnknownD6C8EAC2) {
        toTarget.Normalize();
        BodyController()->CommandMgr().DeliverCmd(CBCLocomotionCmd(toTarget, toTarget, 2.f));
      }
    }
  } else if (msg == kStateMsg_Activate) {
    LogState("AlwaysPointPathFind");
    mPointPathFinding = true;
    mFoundMovementPos = false;
    int from = -1;
    mPointPathFindSearch.FindClosestPhysicalPoint(GetTranslation(), from, nearSelf);
    if (from != -1) {
      SelectTarget(mgr, dt);
      int to = -1;
      if (mPointPathFindSearch.FindClosestPhysicalPoint(mTargetPos, to, nearTarget) ==
              CPathFindPointSearch::kCPR_Success &&
          to != -1 && mPathArea != nullptr &&
          PortIngPointSearch(mPointPathFindSearch, *mPathArea, from, to)) {
        mFoundMovementPos = true;
      }
    }
  }
  if (mFoundMovementPos) {
    PointPathFind(mgr, msg, dt);
  }
}

// @ fn_44_6A3C
void CMinorIng::WaitForFSMTransition(CStateManager&, EStateMsg msg, float) {
  if (msg == kStateMsg_Activate) {
    LogState("WaitForFSMTransition");
  }
}

// ---- code functions (the FSM2 runs them: CGenericFSM2State::SetCodeFunctions) -----------

// @ fn_44_8C1C
void CMinorIng::SelectTarget(CStateManager& mgr, float) {
  if (!mPathFindActive) {
    mFoundMovementPos = false;
  }
  SetHitByPlayerProjectile(false);
  CPlayer* player = mgr.GetPlayer(0);
  if (player == nullptr) {
    return;
  }
  CVector3f dest = player->GetTranslation();
  mTargetPos = dest;
  mTargetId = player->GetUniqueId();
  mLineOfSight.SetTarget(mTargetId);
  if (ShouldPathFind(mgr, CTriggerData(0.f)) && ProjectOntoMesh(dest)) {
    mPathFindNavigation.SetDestination(dest);
    mPathFindNavigation.SetFaceTarget(player->GetUniqueId());
    mFoundMovementPos = true;
  }
}

// @ fn_44_4DDC
// The retail's fn_44_4DDC (asm 0x4DDC) reads the target id at 0xcce, gets the object and calls
// the vtable's 0x54 slot with dt 0 (the target's aim position); the host's body is the same call.
// The host casts to CActor because the host's GetAimPosition is CActor's.
bool CMinorIng::TargetAimPosition(CStateManager& mgr, CVector3f& out) const {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId));
  if (target == nullptr) {
    return false;
  }
  out = target->GetAimPosition(mgr, 0.f);
  return true;
}

// @ fn_44_4E84
// The retail's cover scan keeps its four best candidates in a global list, and fn_44_51D8 is
// that list's destructor (it frees the array and then the block). The host's keeps the four on
// the stack, so the list's teardown is the host's own.
void CMinorIng::FindCoverPoint(CStateManager& mgr, float) {
  ReleaseCoverHint(mgr, mCoverHintId, false);
  if (!TargetAimPosition(mgr, mTargetPos)) {
    return;
  }
  // Retail keeps the four best candidates, sorted by score, in a static reserved_vector.
  struct SCandidate {
    SCandidate() : id(kInvalidUniqueId), score(0.f) {}
    TUniqueId id;
    float score;
  };
  SCandidate best[4];
  int count = 0;
  // [?] retail walks the object list at mgr+0x838, taken to be kOL_AiWaypoint (the list the other
  // AI-hint scans use).
  const CObjectList& list = mgr.GetObjectListById(kOL_AiWaypoint);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CScriptAIHint* hint = TCastToConstPtr< CScriptAIHint >(list[i]);
    // [?] hint type 5 has no name in CScriptAIHint::EHintType.
    if (hint == nullptr || !hint->GetActive() ||
        hint->GetHintType() != static_cast< CScriptAIHint::EHintType >(5) ||
        hint->GetInUse(GetUniqueId()) || hint->GetCurrentAreaId() != GetCurrentAreaId() ||
        hint->GetUniqueId() == mLastCoverId ||
        mgr.GetSafeZoneManager()->PointIsInSafeZone(mgr, hint->GetTranslation())) {
      continue;
    }
    const CVector3f delta = mTargetPos - hint->GetTranslation();
    const float up = CVector3f::Dot(CVector3f::Up(), delta);
    float score = delta.MagSquared();
    if (up > 0.f) {
      score += up * 4.f * up;
    }
    int slot;
    if (count < 4) {
      slot = count++;
    } else if (score < best[3].score) {
      slot = 3;
    } else {
      continue;
    }
    best[slot].id = hint->GetUniqueId();
    best[slot].score = score;
    for (; slot > 0 && best[slot].score < best[slot - 1].score; --slot) {
      const SCandidate swap = best[slot];
      best[slot] = best[slot - 1];
      best[slot - 1] = swap;
    }
  }
  if (count != 0) {
    if (CScriptAIHint* hint = TCastToPtr< CScriptAIHint >(mgr.ObjectById(best[0].id))) {
      ClaimCoverHint(hint, mCoverHintId);
      mLastCoverId = mCoverHintId;
    }
  }
}

// @ fn_44_4AC4
void CMinorIng::SetPointCoverDest(CStateManager& mgr, float) {
  mFoundMovementPos = false;
  mPointPathFindSearch.ClearWaypoints();
  const CScriptAIHint* hint = GetCoverHint(mgr, mCoverHintId);
  if (hint == nullptr || mPathArea == nullptr) {
    return;
  }
  int from = -1;
  mPointPathFindSearch.FindClosestPhysicalPoint(GetTranslation(), from,
                                                CPathFindPointSearchFilter(20.f, 0, -1));
  if (from == -1) {
    return;
  }
  const CVector3f cover = hint->GetTranslation();
  float bestDistSq = 400.f;
  int best = -1;
  int previous = -1;
  for (int i = 0; i < mPathArea->GetNumPoints(); ++i) {
    const CPFPoint& point = mPathArea->GetPoint(i);
    if (!mPathArea->PointPathExists(from, i)) {
      continue;
    }
    const float distSq = (cover - point.GetPosition()).MagSquared();
    if (distSq < bestDistSq &&
        mPathFindSearch.PathExists(point.GetPosition(), cover) == CPathFindSearch::kR_Success) {
      previous = best;
      best = i;
      bestDistSq = distSq;
    }
  }
  if (best == -1) {
    return;
  }
  if (best == from && previous != -1) {
    best = previous;
  }
  if (PortIngPointSearch(mPointPathFindSearch, *mPathArea, from, best)) {
    mFoundMovementPos = true;
  }
}

// @ fn_44_21A8
// The retail's lured destination (asm 0x21A8) asks the nav mesh for the point under the lure
// zone first (fn_44_204C, the REL's own copy of the shared helper) and then searches a path to
// it. The host's PortIngLuredDest does the same through PortIngLuredMeshPoint.
void CMinorIng::SetLuredDest(CStateManager& mgr, float) {
  CVector3f dest;
  mFoundMovementPos =
      PortIngLuredDest(mgr, mPathFindSearch, mSafeZoneId, GetTranslation(), dest);
  mPathFindNavigation.SetDestination(dest);
  mPathFindNavigation.SetFaceTarget(kInvalidUniqueId);
}

// @ fn_44_22B0
// The retail's lured point destination (asm 0x22B0) asks the same question (fn_44_204C) and then
// searches a point path to it. The host's PortIngLuredPointDest does the same through
// PortIngLuredMeshPoint.
void CMinorIng::SetLuredPointDest(CStateManager& mgr, float) {
  mFoundMovementPos = PortIngLuredPointDest(mgr, mPathFindSearch, mPointPathFindSearch, mPathArea,
                                            mSafeZoneId, GetTranslation());
}

// ---- collision actor, touch bounds, blob effect, damage -------------------------------------

// @ fn_44_4080
CTransform4f CMinorIng::PrimitiveTransform(bool raised) const {
  CTransform4f xf = GetTransform();
  if (!raised) {
    const CAnimData* animData = GetModelData()->GetAnimationData();
    if (animData != nullptr) {
      const CVector3f lctr = animData->GetLocatorTransform(mLockOnTarget, nullptr).GetTranslation();
      xf.SetTranslation(GetTransform() * CVector3f::ByElementMultiply(mModelScale, lctr));
    }
  } else {
    xf.SetTranslation(xf.GetTranslation() + xf.GetUp() * 1.8f);
  }
  return xf;
}

// @ fn_44_4050
CTransform4f CMinorIng::GetPrimitiveTransform() const { return PrimitiveTransform(false); }

// @ fn_44_319C
void CMinorIng::CreateCollisionActor(CStateManager& mgr) {
  const CVector3f extent(mModelScale.GetX() * 0.675f, mModelScale.GetY() * 0.675f,
                         mModelScale.GetZ() * 2.8f);
  const CVector3f center = CVector3f::Up() * (extent.GetZ() / -2.f + 0.5f);
  const TUniqueId uid = mgr.AllocateUniqueId();
  CCollisionActor* colAct = rs_new CCollisionActor(uid, GetCurrentAreaId(), GetUniqueId(), extent,
                                                   center, false, GetMass());
  if (colAct != nullptr) {
    mCollisionActorId = uid;
    mgr.AddObject(colAct);
    // fn_44_319C copies the Ing's CHealthInfo whole into the collision actor's (asm 0x32F4).
    *colAct->HealthInfo() = CopyHealthInfo(*GetHealthInfo());
    colAct->SetDamageVulnerability(*CPatterned::GetDamageVulnerability());
    colAct->SetCallTouch(true);
  }
  SetMaterialFilter(CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid), CMaterialList(kMT_CollisionActor, kMT_AIPassthrough, kMT_Player)));
}

// @ fn_44_305C
void CMinorIng::SetCollisionActorActive(CStateManager& mgr, bool active) {
  if (CCollisionActor* colAct = TCastToPtr< CCollisionActor >(mgr.ObjectById(mCollisionActorId))) {
    colAct->SetActive(active);
    colAct->SetTransform(
        CQuaternion::FromMatrix(GetTransform()).BuildTransform4f(colAct->GetTranslation()));
  }
}

// @ fn_44_30F4
void CMinorIng::UpdateCollisionActor(CStateManager& mgr, float dt) {
  CCollisionActor* colAct = TCastToPtr< CCollisionActor >(mgr.ObjectById(mCollisionActorId));
  if (colAct != nullptr && colAct->GetActive()) {
    colAct->MoveToWR(GetLctrTransform(mLockOnLocator).GetTranslation(), dt);
  }
}

// @ fn_44_48F0
void CMinorIng::CreateBlobEffect(CStateManager& mgr, const TLockedToken< CGenDescription >& desc) {
  const TUniqueId uid = mgr.AllocateUniqueId();
  CCollisionTracker* effect = rs_new CCollisionTracker(
      desc, uid, GetCurrentAreaId(), true, rstl::string_l("MinorIngBlobEffect"),
      CTransform4f::Translate(GetTranslation()), GetUniqueId(), 0, 10.f);
  if (effect != nullptr) {
    mBlobEffectId = uid;
    effect->SetNextDrawNode(GetUniqueId());
    effect->SetActive(GetActive());
    // [?] fn_25_2554(0.5): by elimination the tracker's other float setter.
    effect->SetMinimumPathLength(0.5f);
    mgr.AddObject(effect);
  }
}

// @ fn_44_3004
void CMinorIng::SetBlobEffectActive(CStateManager& mgr, bool active) {
  if (CEntity* effect = mgr.ObjectById(mBlobEffectId)) {
    effect->SetActive(active);
  }
}

// @ fn_44_458C
void CMinorIng::UpdateBlobEffect(CStateManager& mgr) {
  CEntity* ent = mgr.ObjectById(mBlobEffectId);
  if (ent == nullptr) {
    return;
  }
  CCollisionTracker* effect = static_cast< CCollisionTracker* >(ent);
  float size = 1.f;
  float density = 1.f;
  float rate = 1.f;
  if (GetActive()) {
    if (mForm == kF_IntoCorporeal || mForm == kF_Corporeal) {
      float progress = 1.f;
      const CAnimData* animData = GetModelData()->GetAnimationData();
      if (mForm == kF_IntoCorporeal && animData != nullptr) {
        const int anim = mPuddleHitByWeapon ? mIntoStunnedAnim : mIntoCorporealAnim;
        if (anim == animData->GetCurrentAnimation()) {
          progress = 1.f - animData->GetAnimTimeRemaining(rstl::string_l("Whole Body")) /
                               animData->GetAnimationDuration(anim);
        }
      }
      density = 1.f - progress * 0.3f;
      size = density < 0.1f ? 0.1f : density;
    }
  } else {
    rate = 0.f;
  }
  if (mShouldEvaporate) {
    rate *= (2.f - mEvaporate) * 0.5f;
  }
  if (rate > 0.f) {
    const CVector3f normal = mSurfaceAlignment.GetSurface().GetNormal();
    const CVector3f pos = GetTranslation();
    effect->SetTransform(CTransform4f::LookAt(pos + CVector3f::Up() * 0.1f, pos + normal,
                                              normal.GetZ() < 0.95f ? CVector3f::Up()
                                                                    : CVector3f::Right()));
    if (CElementGen* gen = effect->GetParticleSystem()) {
      gen->SetExternalParam(0, size);
      gen->SetExternalParam(1, density);
    }
  }
  effect->SetParticleEmissionRateScalar(rate);
  // [?] the colour is the RGB at this+0x44c, taken to be CPatterned::mColor.
  effect->SetModelFlags(CModelFlags(CModelFlags::kT_Two, 0, static_cast< CModelFlags::EFlags >(3),
                                    CColor(mColor.GetRedu8(), mColor.GetGreenu8(),
                                           mColor.GetBlueu8(), 0xff)));
}

// @ fn_44_3E8C
void CMinorIng::UpdateTouchBounds() {
  const float half = mModelScale.GetX() * 0.5625f;
  mPuddleTouchBounds = CAABox(CVector3f(-half, -half, 0.f),
                              CVector3f(half, half, mModelScale.GetX()))
                           .GetTransformedAABox(GetTransform());
  if (const CCollisionPrimitive* prim = GetCollisionPrimitive()) {
    mBodyTouchBounds = prim->CalculateAABox(GetPrimitiveTransform());
  }
}

// @ fn_44_41AC
rstl::optional_object< CAABox > CMinorIng::GetTouchBounds() const {
  return mForm == kF_Puddle ? mPuddleTouchBounds : mBodyTouchBounds;
}

// @ fn_44_25E4
void CMinorIng::PuddleDamage(CStateManager& mgr, TUniqueId sender) {
  // 0x2608..0x2610 fcmpo, cror eq,lt,eq, bne to the return: a NaN timer returns too.
  if (!(mDamageFxTimer <= 0.f)) {
    return;
  }
  const CWeapon* weapon = TCastToConstPtr< CWeapon >(mgr.GetObjectById(sender));
  if (weapon == nullptr) {
    return;
  }
  // 0x264C..0x2654 fcmpo, cror eq,gt,eq, bne to the normal arm: a NaN damage is normal.
  const bool heavy =
      weapon->GetCurrentDamageInfo().GetDamage(mData.mIngSpotVulnerability) >= 10.f;
  const rstl::optional_object< TLockedToken< CGenDescription > >& hitEffect =
      heavy ? mHeavyHitEffect : mNormalHitEffect;
  CEntity* fx = nullptr;
  if (hitEffect) {
    fx = rs_new CExplosion(
        *hitEffect, mgr.AllocateUniqueId(),
        CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true, kInvalidEditorId),
        rstl::string_l("Ing Damage Fx"), GetTransform(), 0, GetModelData()->GetScale(),
        CColor::White(), -1);
  }
  PlaySfx(heavy ? 5 : 4, true);
  if (DebugMinorIng()) {
    fprintf(stderr, "[minoring] %04x puddle hit%s, fx %d\n", GetUniqueId().Value(),
            heavy ? " (heavy)" : "", fx != nullptr);
  }
  if (fx != nullptr) {
    mgr.AddObject(fx);
  }
  mDamageFxTimer = 0.111f;
}

// @ fn_44_2CD8
// The retail's damage source hands the damage info out through a copy of it (asm 0x2D48, 0x2E44
// and 0x2EC8): fn_44_2EF4 -> fn_44_2F24 -> fn_44_2FB0 is the REL's own copy of CDamageInfo
// (0x1c bytes: the weapon mode, the four floats, the three sound ids and the two flag bits; the
// byte at +0x1c is the caller's own). The host's CDamageInfo owns its copy, so the body is the
// assignment.
bool CMinorIng::DamageSource(CStateManager& mgr, TUniqueId touched, TUniqueId& attacker,
                             CVector3f& direction,
                             rstl::optional_object< CDamageInfo >& damage) const {
  const CEntity* ent = mgr.GetObjectById(touched);
  if (const CWeapon* weapon = TCastToConstPtr< CWeapon >(ent)) {
    direction = weapon->GetTransform().GetForward();
    damage = weapon->GetCurrentDamageInfo();
    attacker = weapon->GetOwnerId();
    return true;
  }
  const CActor* actor = TCastToConstPtr< CActor >(ent);
  if (const CScriptSafeZone* zone = TCastToConstPtr< CScriptSafeZone >(actor)) {
    direction = GetTranslation() - zone->GetTranslation();
    if (direction.CanBeNormalized()) {
      direction.Normalize();
    }
    damage = CDamageInfo(CWeaponMode(kWT_AreaLight), 100.f, 0.f, 0.f, false, false);
    attacker = touched;
    return true;
  }
  const CScriptSpecialFunction* func = TCastToConstPtr< CScriptSpecialFunction >(actor);
  if (func == nullptr || func->GetFunction() != CScriptSpecialFunction::kSF_RadialDamage) {
    return false;
  }
  direction = GetTranslation() - func->GetTranslation();
  if (direction.CanBeNormalized()) {
    direction.Normalize();
  }
  damage = func->GetDamageInfo();
  attacker = func->GetUniqueId();
  return true;
}

// @ fn_44_29D4
void CMinorIng::CollisionActorDamage(CStateManager& mgr, TUniqueId sender) {
  if (!GetAlive()) {
    return;
  }
  CCollisionActor* colAct = TCastToPtr< CCollisionActor >(mgr.ObjectById(sender));
  if (colAct == nullptr) {
    return;
  }
  const TUniqueId touched = colAct->GetLastTouchedObject();
  TUniqueId attacker = kInvalidUniqueId;
  CVector3f direction = CVector3f::Zero();
  rstl::optional_object< CDamageInfo > damage;
  DamageSource(mgr, touched, attacker, direction, damage);
  CHealthInfo* colHealth = colAct->HealthInfo();
  CHealthInfo* health = HealthInfo();
  if (colHealth != nullptr && health != nullptr) {
    // The collision actor carries a copy of the Ing's health: what it lost is the hit.
    const float initial = health->GetInitialHP();
    const float hp = health->GetHP();
    const float amount = initial - colHealth->GetHP();
    health->SetHP(hp - amount);
    TakeDamage(direction, amount);
    if (hp <= amount) {
      Death(mgr, direction, kSS_DeathRattle);
      // [?] retail's UpdateHealthCause(actor, attacker, CWeaponMode(none), true, false); the host's
      // equivalent is RecordDamageSource.
      mgr.RecordDamageSource(*this, attacker, CDamageInfo(CWeaponMode(), 0.f, 0.f, 0.f), true,
                             false);
    }
    colHealth->SetHP(initial);
    if (DebugMinorIng()) {
      fprintf(stderr, "[minoring] %04x body hit %.1f, hp %.1f\n", GetUniqueId().Value(), amount,
              health->GetHP());
    }
    mKnockDamage += mHpTracker - health->GetHP();
    mHpTracker = health->GetHP();
    if (mPuddleHitByWeapon) {
      mCancelStun = true;
      mStunTimer = 0.3f;
    }
  }
  if (!damage) {
    if (!GetAlive()) {
      MassiveDeath(mgr);
    }
  } else {
    KnockBack(mgr, CKnockBackInfo(direction, touched, attacker, *damage, true));
  }
}

// ---- overrides -----------------------------------------------------------------------------

// @ fn_44_8FCC
void CMinorIng::PreThink(float dt, CStateManager& mgr) {
  EnsureBodyActive(mgr);
  // Retail tests bit 0x20 of the material list's high word (+0x68): material 37.
  if (mForm == kF_Puddle && !GetMaterialList().HasMaterial(kMT_GroundCollider)) {
    const CPlane plane = mSurfaceAlignment.GetSurface().GetPlane();
    if (CVector3f::Dot(mSurfaceMoveDir, plane.GetNormal()) < 0.95f) {
      const CVector3f pos = GetTranslation();
      const float t = dt * 0.6f;
      const float dist = plane.GetHeight(pos) - 0.1f;
      const CVector3f onPlane = pos - plane.GetNormal() * dist;
      SetTranslation(pos * (1.f - t) + onPlane * t);
    }
    mSurfaceMoveDir = CVector3f::Zero();
  }
  CPatterned::PreThink(dt, mgr);
}

// @ fn_44_8E5C
void CMinorIng::Think(float dt, CStateManager& mgr) {
  if (GetActive()) {
    EnsureBodyActive(mgr);
    CPatterned::Think(dt, mgr);
    UpdateTargetPos(mgr);
    UpdateCollisionActor(mgr, dt);
    mLineOfSight.Update(dt, mgr);
    UpdateBallPursuit(mgr);
    UpdateTimers(dt);
    mSurfaceAlignment.Update(*this, mgr, dt);
    UpdateValidTarget(mgr);
    UpdateTouchBounds();
    UpdateLoopSfx();
    mTouched = false;
    if (GetAlive()) {
      mKnockAnimCancel = false;
      if (mForm == kF_Puddle) {
        mKnockDamage = 0.f;
      } else if (mData.mUnknownA03E450C < mKnockDamage) {
        mKnockAnimCancel = true;
      }
      KnockBackController().EnableAnimReaction(CKnockBackMgr::kAR_KnockBack, !mKnockAnimCancel);
    }
    UpdateSafeZone(mgr);
  }
  UpdateBlobEffect(mgr);
}

// @ fn_44_9150
void CMinorIng::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  CPatterned::AcceptScriptMsg(mgr, msg);
  const TUniqueId sender = msg.GetSenderId();
  switch (msg.GetMessage()) {
  case kSM_Create:
    SnapToFloor(mgr);
    AddMaterial(kMT_NoStaticCollision, mgr);
    RemoveMaterial(kMT_Solid, mgr);
    BodyController()->Activate(mgr, pas::kAS_Invalid);
    CreateCollisionActor(mgr);
    SetForm(mgr, kF_Puddle, true);
    // Host guard: retail asks the pool for the PART without checking the id.
    if (mData.mIngSpot.GetBlobEffect() != kInvalidAssetId) {
      CreateBlobEffect(mgr, TLockedToken< CGenDescription >(gpSimplePool->GetObj(
                                SObjectTag('PART', mData.mIngSpot.GetBlobEffect()))));
    }
    UpdateTouchBounds();
    UpdateLoopSfx();
    ResetAttackTimer(mgr);
    break;
  case kSM_Deactivate:
    StopSfx();
    SetCollisionActorActive(mgr, false);
    SetBlobEffectActive(mgr, false);
    break;
  case kSM_Activate:
    JoinTeam(mgr);
    SetBlobEffectActive(mgr, true);
    break;
  case kSM_AreaLoaded: {
    const rstl::vector< SConnection >& conns = GetConnectionList();
    for (int i = 0; i < conns.size(); ++i) {
      if (conns[i].state == kSS_Patrol && conns[i].msg == kSM_Follow) {
        mHasPatrolPath = true;
        break;
      }
    }
    CPFArea* area =
        mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()).GetPostConstructed()->mPathArea;
    mPathArea = area;
    mPathFindSearch.SetArea(area);
    mPointPathFindSearch = CPathFindPointSearch(area);
    JoinTeam(mgr);
    break;
  }
  case kSM_Increment: {
    const CScriptAIWaypoint* wp = TCastToConstPtr< CScriptAIWaypoint >(mgr.GetObjectById(sender));
    if (wp != nullptr && (wp->GetFlags() & 1) != 0) {
      mAttackPatternProjectile = true;
    }
    break;
  }
  case kSM_XENZ:
    // fn_44_9150 (asm 0x9610): the timer the safe-zone entry waits for is the one at guest 0xd1c,
    // not the move-away timer at 0xd20 - the retail compares 0xd1c against zero (asm 0x9618-0x9620).
    if (TCastToConstPtr< CScriptSafeZone >(mgr.GetObjectById(sender)) != nullptr &&
        mTimerD1C <= 0.f) {
      ++mSafeZoneEnterCount;
      // fn_44_9150 (asm 0x9638): the zone the message names lands in the word at guest 0xcd8; the
      // zone that lured the actor is the separate word at 0xcda (fn_44_36FC/fn_44_3780).
      mSafeZoneEnterId = sender;
      mInsideSafeZone = true;
    }
    break;
  case kSM_XEXZ:
    if (TCastToConstPtr< CScriptSafeZone >(mgr.GetObjectById(sender)) != nullptr &&
        --mSafeZoneEnterCount == 0) {
      mSafeZoneEnterId = kInvalidUniqueId;
      mInsideSafeZone = false;
    }
    break;
  case kSM_Damage:
    if (mForm == kF_Puddle) {
      PuddleDamage(mgr, sender);
    } else {
      CollisionActorDamage(mgr, sender);
    }
    break;
  case kSM_Delete:
    ReleaseCoverHint(mgr, mCoverHintId, false);
    LeaveTeam(mgr);
    StopSfx();
    mgr.DeleteObjectRequest(mBlobEffectId);
    mgr.DeleteObjectRequest(mCollisionActorId);
    break;
  case kSM_ResistedDamage:
    if (mForm == kF_Puddle && mPuddleHitTimer <= 0.f) {
      const CWeapon* weapon = TCastToConstPtr< CWeapon >(mgr.GetObjectById(sender));
      if (weapon != nullptr &&
          TCastToConstPtr< CPlayer >(mgr.GetObjectById(weapon->GetOwnerId())) != nullptr) {
        const CWeaponTypeVulnerability vuln = GetDamageVulnerability()->GetVulnerability(
            weapon->GetCurrentDamageInfo().GetWeaponMode());
        if (!close_enough(vuln.mDamageMultiplier, 0.f) ||
            vuln.mEffect == CWeaponTypeVulnerability::kE_Immune) {
          mPuddleHitByWeapon = true;
        }
      }
    }
    break;
  default:
    break;
  }
}

// @ fn_44_8E10
// The retail's PreRender is the base's (asm 0x8E10-0x8E2C); the check finds no difference.
void CMinorIng::PreRender(CStateManager& mgr) { CPatterned::PreRender(mgr); }

// @ fn_44_8DF0
// The retail's AddToRenderer is the base's (asm 0x8DF0-0x8E0C); the check finds no difference.
void CMinorIng::AddToRenderer(const CStateManager& mgr) const { CPatterned::AddToRenderer(mgr); }

// @ fn_44_6FA0
// The retail's GetAimPosition is the base's (asm 0x6FA0-0x6FC8); the check finds no difference.
CVector3f CMinorIng::GetAimPosition(const CStateManager& mgr, float dt) const {
  return CPatterned::GetAimPosition(mgr, dt);
}

// @ fn_44_70
// The retail's GetHealthInfo calls the base's HealthInfo virtual (asm 0x70-0x98); the check finds
// no difference.
const CHealthInfo* CMinorIng::GetHealthInfo() const {
  return const_cast< CMinorIng* >(this)->HealthInfo();
}

// @ fn_44_8E30
void CMinorIng::Render(const CStateManager& mgr) const {
  if (mForm != kF_Puddle) {
    CPatterned::Render(mgr);
  }
}

// @ fn_44_424C
void CMinorIng::Touch(CActor& actor, CStateManager& mgr) {
  EnsureBodyActive(mgr);
  if (mCurDamageRemTime <= 0.f && actor.GetUniqueId() == mTargetId) {
    const CDamageInfo damage = mForm == kF_Puddle ? mData.mIngSpotDamage : GetContactDamage();
    mgr.ApplyDamage(GetUniqueId(), mTargetId, GetUniqueId(), damage, SolidFilter(),
                    CVector3f::Zero());
    mTouched = true;
    mCurDamageRemTime = mDamageWaitTime;
  }
  CPatterned::Touch(actor, mgr);
}

// @ fn_44_76CC
void CMinorIng::Death(CStateManager& mgr, const CVector3f& direction, EScriptObjectState state) {
  EnsureBodyActive(mgr);
  if (GetAlive()) {
    SetBlobEffectActive(mgr, false);
  }
  CPatterned::Death(mgr, direction, state);
}

// @ fn_44_8D2C
void CMinorIng::KnockBack(CStateManager& mgr, const CKnockBackInfo& info) {
  // 0x8D84..0x8D8C fcmpo, cror eq,lt,eq, bne to the return: a NaN HP returns too.
  if ((mForm == kF_Puddle || mForm == kF_IntoPuddle) && !(GetHealthInfo()->GetHP() <= 0.f)) {
    return;
  }
  EnsureBodyActive(mgr);
  CPatterned::KnockBack(mgr, info);
}

// @ fn_44_7058
// The retail's massive death spawns the inglets (asm 0x7058-0x76A8). The number of
// them falls off with the distance to the player, and each one is a CBouncyGrenade named "Inglet"
// that leaves the Ing's top at the angle the ten-entry table gives.
// The retail's inglet gets its CEntityInfo on the stack and tears it down again (asm 0x75F4):
// fn_44_28D8 -> fn_44_2930 is the REL's own copy of the members' destructors (it frees the
// pointer at +0x0c and then the block itself). The host's CEntityInfo owns its destruction.
void CMinorIng::MassiveDeath(CStateManager& mgr) {
  const CHealthInfo& health = *GetHealthInfo();
  // The retail's first guard (asm 0x70CC): the health info's cause-of-death weapon mode (the
  // CWeaponMode at +0x0c) keeps its flags in the byte at +0x0e, and that byte must not be 2 or
  // 0x12. The host's CWeaponMode has three flags (the charged, the comboed and the instant-kill
  // bits), so its byte can never read 0x12.
  const CWeaponMode& deathWeapon = health.GetCauseOfDeathWeapon();
  const uint flags = (deathWeapon.IsCharged() ? 1u : 0u) | (deathWeapon.IsComboed() ? 2u : 0u) |
                      (deathWeapon.IsInstantKill() ? 4u : 0u);
  if (flags != 2 && flags != 0x12) {
    // The retail's second guard (asm 0x70DC): an Ing in the puddle form only spawns inglets when
    // the ing spot's flag byte at 0x155 is set.
    if (mForm != kF_Puddle || mData.mFlags155 != 0) {
      // The distance (asm 0x710C-0x7140): the damage the Ing took (the base class's mLastHP, the
      // health at the end of the last frame, less the health info's HP) clamped to 0..the inglet's
      // spawn distance (the data's 0x134).
      const float maxDist = mData.mIngletSpawnDistance;
      const float raw = mLastHP - health.GetHP();
      float dist = 0.f;
      if (raw >= 0.f) {
        dist = raw < maxDist ? raw : maxDist;
      }
      // The count (asm 0x71D8-0x7238): the fraction of the maximum count the distance leaves,
      // rounded up, and never below the minimum count.
      const int count = static_cast< int >(
          CMath::CeilingF((1.f - dist / maxDist) * static_cast< float >(mData.mIngletMaxCount)));
      const int total = count < mData.mIngletMinCount ? mData.mIngletMinCount : count;
      if (total > 0) {
        // The materials (asm 0x7140-0x71D8): the inglet collides with the seeker-target material
        // and, when the ing spot allows lock-on, with the target and orbit materials too.
        CMaterialList materials(kMT_SeekerTarget);
        if (mData.mIngSpotAllowLockOn) {
          materials.Add(kMT_Target);
          materials.Add(kMT_Orbit);
        }
        // The direction (asm 0x723C-0x7270): from the Ing's top towards the target.
        CVector3f dir = mTargetPos - GetTranslation();
        dir.Normalize();
        // The spawn point (asm 0x7274-0x7300): the Ing's top, half the up vector above its
        // position, and the up vector is what the look-at takes.
        const CVector3f up = GetTransform().GetUp();
        const CVector3f at = GetTranslation() + up * 0.5f;
        // The first angle (asm 0x73F4-0x7414) is a random 35..60 degrees around the local X, the
        // second (asm 0x7418-0x7464) comes from the table: the value in tens of degrees, less 50.
        const int base = mgr.Random()->Range(0, 9);
        for (int i = 0; i < total; ++i) {
          CTransform4f xf = CTransform4f::LookAt(at, at + dir, up);
          const float pitch =
              mgr.Random()->Range(35.f, 60.f) * 0.017453292f;
          xf.RotateLocalX(CRelAngle::FromRadians(pitch));
          const float yaw = (skIngletAngles[(base + i) % 10] * 10.f - 50.f) * 0.017453292f;
          xf.RotateLocalZ(CRelAngle::FromRadians(yaw));
          // Two units along the forward column (asm 0x7550-0x757C, f28 = rodata 0x3C = 2.0).
          xf.SetTranslation(xf.GetTranslation() + xf.GetForward() * 2.f);
          // The inglet (asm 0x74A8-0x75CC): the launch speed is random out of the data's range,
          // the model data and the actor parameters are the empty ones and the bounds are the
          // inverted box.
          CBouncyGrenade* const inglet = rs_new CBouncyGrenade(
              mgr.AllocateUniqueId(), rstl::string_l("Inglet"),
              CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true, kInvalidEditorId),
              xf, CModelData::CModelDataNull(), CActorParameters::None(), GetUniqueId(),
              mgr.Random()->Range(mData.mIngletMinLaunchSpeed, mData.mIngletMaxLaunchSpeed),
              mData.mGrenadeData, mData.mIngletExplodeDistance, CAABox::MakeMaxInvertedBox(),
              mTargetId, mData.mIngletMaxTurnAngle, 0xf, &materials, &mData.mIngletHealth);
          mgr.AddObject(inglet);
          // The inglet does not collide with the other projectiles (asm 0x762C).
          inglet->RemoveMaterial(kMT_Projectile, mgr);
        }
      }
    }
  }
  PlaySfx(3, true);
  CPatterned::MassiveDeath(mgr);
}

// The projectile's impact visor effect: fn_44_65C4 (asm 0x6418) is the REL's copy of
// CImpactVisorEffect::None() - three empty parts, the invalid forced visor - and fn_44_64AC
// (0x645C) its destructor; 6748/6720/6700/66C0/660C/657C/655C/6500 are the copies of its
// optional members' copy, assign and destroy. The host's CImpactVisorEffect::None() is the same
// effect, so they are cited, not marked.
// @ fn_44_6384
void CMinorIng::DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node,
                                EUserEventType type, float dt) {
  if (type == kUE_Projectile) {
    const CTransform4f lctr = GetLctrTransform(node.GetLocatorName());
    const CEnergyProjectile* shot = LaunchProjectile(
        CTransform4f::LookAt(lctr.GetTranslation(), mTargetPos, CVector3f::Up()), mgr, 1,
        CWeapon::kPA_None, false, CImpactVisorEffect::None(), CVector3f(1.f, 1.f, 1.f));
    if (DebugMinorIng()) {
      fprintf(stderr, "[minoring] %04x projectile %s\n", GetUniqueId().Value(),
              shot != nullptr ? "launched" : "not launched");
    }
    return;
  }
  CPatterned::DoUserAnimEvent(mgr, node, type, dt);
}

// @ fn_44_8DBC
const CDamageVulnerability* CMinorIng::GetDamageVulnerability() const {
  if (mForm == kF_Puddle) {
    return &mData.mIngSpotVulnerability;
  }
  return CPatterned::GetDamageVulnerability();
}

// @ fn_44_38A8
bool CMinorIng::Listen(CStateManager& mgr, const CVector3f& position, EListenNoiseType type) {
  if (!GetAlive() || !GetActive()) {
    return false;
  }
  if (type == kLNT_PathObstruction) {
    if ((position - GetTranslation()).MagSquared() < 1600.f) {
      mRecalculatePath = true;
      return true;
    }
  } else if (type == kLNT_SafeZone) {
    PickSafeZone(mgr, position);
  }
  return false;
}

// @ fn_44_9758
void CMinorIng::SetupStateMachine(CStateManager& mgr) {
  StateMachine* stateMachine = mStateMachine.get();
  CPatterned::SetupStateMachine(mgr);
  stateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  stateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  stateMachine->SetCodeFunctions(skCodeFuncs, ARRAY_SIZE(skCodeFuncs));
  // Host: MP2_DEBUG_MINORING dumps the FSM2 graph once, so a log shows which triggers moved an
  // Ing between the states it prints.
  static bool sDumped = false;
  const CGenericFSM2* machine = GetStateMachine2();
  if (DebugMinorIng() && !sDumped && machine != nullptr) {
    sDumped = true;
    DumpFsm2(*machine);
  }
}

// The retail's loader copies the model data it read (asm 0x51C): fn_44_10D4 -> fn_44_1114 ->
// fn_44_1134 -> fn_44_115C is the REL's own copy of CModelData (the three model slots, the scale
// and the id; the copy clears the source's byte at +0x0c). The host's CModelData owns its copy,
// so the body is the class's own.
// @ fn_44_D54
// The retail copy moves a whole CIngSpotData from the source to the destination: the blob
// effect, the three damage infos, the vulnerability and the floats at +0x00..+0x2c, the sounds
// at +0x2e..+0x36 and the rest of the block (asm 0xD54-0xE08). The host's CIngSpotData has the
// retail's layout, so the copy is the class's own copy.
// The retail's copy runs in three steps (asm 0xD54 -> fn_44_E08 -> fn_44_EB8): the last one is
// the REL's own copy of the ing spot's vector member (it allocates the array and copies the
// 12-byte entries), so the class's own copy covers it too.
CIngSpotData CopyIngSpotData(const CIngSpotData& src) { return src; }

// @ fn_44_9B4
// The retail's fn_44_9B4 (asm 0x9B4-0xC44) builds the data block the actor runs on: the
// projectile and its damage, the ing spot, the inglet's health, the grenade data and the
// inglet's spawn values. The host's block is its own shape (CMinorIngData), so the body is the
// constructor call and not a field-by-field copy of the retail's offsets.
static CMinorIngData BuildMinorIngData(const SLdrMinorIng& sldrThis, const CIngSpotData& ingSpotIn,
                                       const CBouncyGrenadeData& grenadeDataIn, uchar flags155,
                                       uchar flags156) {
  // The retail's fn_44_9B4 copies the three blocks the loader read into the data: the ing spot
  // (fn_44_D54), the inglet's health (fn_44_D00) and the grenade data (fn_44_C44).
  const CIngSpotData ingSpot = CopyIngSpotData(ingSpotIn);
  const CHealthInfo ingletHealth =
      CopyHealthInfo(LdrToHealthInfo(sldrThis.unknown_0x3da35851.health));
  const CBouncyGrenadeData grenadeData = CopyGrenadeData(grenadeDataIn);

  return CMinorIngData(
      sldrThis.projectile, LdrToDamageInfo(sldrThis.projectileDamage),
      LdrToDamageInfo(sldrThis.ingSpot.damage), ingSpot, sldrThis.unknown_0xa03e450c,
      sldrThis.ingSpot.morphballPursuitDistance, sldrThis.ingSpot.bombStunDuration,
      sldrThis.ingSpot.unknown_0x7569fdba, sldrThis.ingSpot.sfxFallOff,
      sldrThis.attackAngleLimit, sldrThis.lineOfSightHeightOffset, sldrThis.unknown_0xd6c8eac2,
      sldrThis.unknown_0x2a5449ba, sldrThis.hearingRadius, sldrThis.ingSpot.speedModifier,
      sldrThis.unknown_0x3da35851.allowLockOn,
      static_cast< uchar >(flags155), flags156, sldrThis.ingSpot.maxSpeed,
      sldrThis.ingSpot.maxWallSpeed, sldrThis.ingSpot.ballPursuitSpeed,
      LdrToDamageVulnerability(sldrThis.ingSpot.vulnerability),
      static_cast< ushort >(sldrThis.ingSpot.sound_Idle),
      static_cast< ushort >(sldrThis.ingSpot.sound_Move),
      static_cast< ushort >(sldrThis.ingSpot.sound_HitNormalDamage),
      static_cast< ushort >(sldrThis.ingSpot.sound_HitHeavyDamage),
      static_cast< ushort >(sldrThis.ingSpot.sound_Death),
      ingletHealth, grenadeData, sldrThis.unknown_0x3da35851.minLaunchSpeed,
      sldrThis.unknown_0x3da35851.maxLaunchSpeed, sldrThis.unknown_0x3da35851.minGeneration,
      sldrThis.unknown_0x3da35851.maxGeneration, sldrThis.unknown_0x3da35851.maxTurnAngle,
      sldrThis.unknown_0x3da35851.unknown_0xfbf8ea0a,
      sldrThis.unknown_0x3da35851.unknown_0x47f99fbc);
}

// Retail fn_44_1830: SLdrMinorIngIngSpot's constructor, called by SLdrMinorIng's (fn_44_14D8
// 0x1514) from the loader fn_44_110 (0x130). The generated headers' inline constructors store the
// same floats, damage info and asset ids, except the sound ids: the guest's default for every sound
// id is -1 (no sound), the header's 0. Those -1 stores are written over it here. The ing-spot struct
// has the guest's layout (its sounds sit at 0x3c..0x4c in both), so the `li r0,-0x1` at 0x00001848
// feeds nine stores, the last five of them sound ids: sound_Idle 0x3c (0x00001860), sound_Move 0x40
// (0x00001864), sound_HitNormalDamage 0x44 (0x00001868), sound_HitHeavyDamage 0x48 (0x0000186C) and
// sound_Death 0x4c (0x00001870); the first four (0x2c..0x38, 0x00001850..0x0000185C) are the
// CAssetIds blobEffect, hitNormalDamage, hitHeavyDamage and death, already kInvalidAssetId from the
// header's initializer list. The inglet block (SLdrUnknownStruct34, the constructor fn_44_16C4
// called at fn_44_14D8 0x151C) keeps its two sound ids for x16C4_SoundDefaults below. A property in
// the instance's data overwrites any of them.
static void x1830_SoundDefaults(SLdrMinorIng& sldr) {
  sldr.ingSpot.sound_Idle = -1;            // fn_44_1830 0x00001860 (+0x3c)
  sldr.ingSpot.sound_Move = -1;            // fn_44_1830 0x00001864 (+0x40)
  sldr.ingSpot.sound_HitNormalDamage = -1; // fn_44_1830 0x00001868 (+0x44)
  sldr.ingSpot.sound_HitHeavyDamage = -1;  // fn_44_1830 0x0000186C (+0x48)
  sldr.ingSpot.sound_Death = -1;           // fn_44_1830 0x00001870 (+0x4c)
}

// Retail fn_44_16C4: SLdrUnknownStruct34's (the inglet block's) constructor, called from
// SLdrMinorIng's at 0x151C. Same story as x1830_SoundDefaults: its two sound ids default to -1 in
// the guest and 0 in the generated header. This struct also has the guest's layout, and the
// `li r0,-0x1` at 0x000016E4 feeds five stores: sound_Bounce 0x38 (0x0000171C), sound_Explode 0x3c
// (0x0000172C), and the CAssetIds explosion 0x18, effect 0x1c and trail 0x20
// (0x000016EC..0x000016F4), already kInvalidAssetId from the header.
static void x16C4_SoundDefaults(SLdrMinorIng& sldr) {
  sldr.unknown_0x3da35851.sound_Bounce = -1;  // fn_44_16C4 0x0000171C (+0x38)
  sldr.unknown_0x3da35851.sound_Explode = -1; // fn_44_16C4 0x0000172C (+0x3c)
}

// @ fn_44_110
// The retail's loader is the property loop over the REL's own format; the host reads the same
// properties with the generated SLdrMinorIng reader, whose ids and their order the analysis
// checked against the REL id by id: it reads all 20 and fn_44_9B4's packing gives each bool its
// bit.
static CEntity* PortLoadMinorIng(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrMinorIng sldrThis;
  x1830_SoundDefaults(sldrThis);  // fn_44_1830's -1 sound ids (fn_44_14D8 0x1514)
  x16C4_SoundDefaults(sldrThis);  // fn_44_16C4's -1 sound ids (fn_44_14D8 0x151C)
#include "MetroidPrime/ScriptLoader/SLdrMinorIng.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }

  const CIngSpotData ingSpotBuilt(sldrThis.ingSpot.blobEffect, sldrThis.ingSpot.hitNormalDamage,
                                  sldrThis.ingSpot.hitHeavyDamage, sldrThis.ingSpot.death,
                                  sldrThis.ingSpot.maxSpeed, sldrThis.ingSpot.maxWallSpeed,
                                  sldrThis.ingSpot.ballPursuitSpeed, sldrThis.ingSpot.speedModifier,
                                  sldrThis.ingSpot.turnSpeed,
                                  LdrToDamageVulnerability(sldrThis.ingSpot.vulnerability),
                                  static_cast< ushort >(sldrThis.ingSpot.sound_Idle),
                                  static_cast< ushort >(sldrThis.ingSpot.sound_Move),
                                  static_cast< ushort >(sldrThis.ingSpot.sound_HitNormalDamage),
                                  static_cast< ushort >(sldrThis.ingSpot.sound_HitHeavyDamage),
                                  static_cast< ushort >(sldrThis.ingSpot.sound_Death));

  // The retail's loader (fn_44_110) reads seven bool properties and fn_44_9B4 packs them into
  // three bytes of the data (fn_44_9B4, asm 0x9B4-0xC44): the byte at 0x154's bit 7 is the inglet block's
  // allow-lock-on, the byte at 0x155 is the property 0x9207f51 on its own and the byte at 0x156
  // holds the other six, most significant bit first. The property ids are the ones the generated
  // SLdrMinorIng reader uses, so each bool lands in the bit fn_44_9B4 gives it.
  const uchar flags155 = static_cast< uchar >(sldrThis.unknown_0x09207f51);
  uchar flags156 = 0;
  if (sldrThis.allowProjectileDuringAttackPattern) {
    flags156 |= 0x80; // 0x399d1eaa
  }
  if (sldrThis.unknown_0xbce16644) {
    flags156 |= 0x40; // 0xbce16644
  }
  if (sldrThis.unknown_0x142433d3) {
    flags156 |= 0x20; // 0x142433d3
  }
  if (sldrThis.stayOnPointPathFinding) {
    flags156 |= 0x10; // 0xb6cc0063
  }
  if (sldrThis.allowProjectileDuringStayOnPointPathFinding) {
    flags156 |= 0x08; // 0xe601f7bd
  }
  if (sldrThis.allowPuddleLockOn) {
    flags156 |= 0x04; // 0x2107e4fb
  }

  // The inglet's spawn values (the retail's data 0x120..0x138): the inglets' count is the
  // minimum and the maximum of the generation range, and the massive death falls off with the
  // distance. The log says the count once per spawn, so a run shows which placement has any.
  if (DebugMinorIng()) {
    fprintf(stderr, "[minoring] inglets %d..%d spawn distance %g\n", sldrThis.unknown_0x3da35851.minGeneration,
            sldrThis.unknown_0x3da35851.maxGeneration,
            static_cast< double >(sldrThis.unknown_0x3da35851.unknown_0xfbf8ea0a));
  }

  // The retail's loader reads the ing spot (fn_44_194C) and builds the bouncing-projectile
  // struct out of the inglet's block (fn_44_FE8); the data build copies both into the data.
  const CBouncyGrenadeData grenadeData = BuildGrenadeData(sldrThis.unknown_0x3da35851);

  const CMinorIngData data =
      BuildMinorIngData(sldrThis, ingSpotBuilt, grenadeData, flags155, flags156);

  return rs_new CMinorIng(mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
                          LdrToEntityInfo(info, sldrThis.editorProperties),
                          LdrToTransform4f(sldrThis.editorProperties), *modelData,
                          LdrToPatternedInfo(sldrThis.patterned, nullptr),
                          LdrToActorParameters(sldrThis.actorInformation), data);
}

static SMinorIng_FuncPtrs REL_loader_MinorIng;

// @ fn_44_E0
// The retail module writes its loader into the module's own function table (lbl_44_bss_84,
// asm 0xF8) and hands that table to the DOL's SetSMinorIng_FuncPtrs (asm 0xFC). The host's
// SMinorIng_FuncPtrs holds the loader alone, so the table is the static below.
static void RegisterMinorIngModule() {
  REL_loader_MinorIng.mLoader = PortLoadMinorIng;
  SetSMinorIng_FuncPtrs(&REL_loader_MinorIng);
}

// The retail RELMain (the module's entry at 0xC0) does nothing but call the registration helper.
void RELMain() {
  RegisterMinorIngModule();
}

void RELExit() { SetSMinorIng_FuncPtrs(nullptr); }
