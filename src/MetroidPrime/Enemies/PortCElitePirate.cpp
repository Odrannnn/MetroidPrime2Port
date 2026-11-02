// Host body for the ElitePirate REL (module 15); in game the class is the Ingsmasher (its scan
// text). The analysis is docs/module-analysis/ElitePirate.md. This slice ports the constructor, the
// vtable overrides (Think, AcceptScriptMsg, PreRender, Render, DoUserAnimEvent, the accessors) and
// the helpers they call from the retail pseudocode; the FSM is installed from fn_15_9840's tables,
// but the 54 trigger/state/code bodies at the end of the file are still StandIn shells.
//
// A `// @ fn_15_XXXX` line above a function marks a body checked against that retail function.
// `[?]` marks a literal reading of retail whose purpose is not established.
#include "MetroidPrime/Enemies/CElitePirate.hpp"

#include "Collision/CMaterialFilter.hpp"
#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "Kyoto/Animation/CSkinnedModel.hpp"
#include "Kyoto/CRandom16.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Math/CPlane.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/BodyState/CBodyStateCmdMgr.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CAxisAngle.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CRumbleManager.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Cameras/CFirstPersonCamera.hpp"
#include "MetroidPrime/Collision/CJointCollisionDescription.hpp"
#include "MetroidPrime/Enemies/CElitePirateGrenadeLauncher.hpp"
#include "MetroidPrime/Enemies/CPatternedInfo.hpp"
#include "MetroidPrime/PathFinding/CPathFindArea.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrElitePirate.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAIHint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTeamAiMgr.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CEnergyProjectile.hpp"
#include "MetroidPrime/Weapons/CGameProjectile.hpp"
#include "MetroidPrime/Weapons/CImpactVisorEffect.hpp"
#include "MetroidPrime/Weapons/WeaponTypes.hpp"
#include "port_console.h"

#include <float.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

namespace {

// Reports a stand-in the first time it runs, so a log lists what is still a shell.
void StandIn(const char* what) {
  static const char* sSeen[64] = {};
  static int sCount = 0;
  for (int i = 0; i < sCount; ++i) {
    if (sSeen[i] == what) {
      return;
    }
  }
  if (sCount < 64) {
    sSeen[sCount++] = what;
  }
  fprintf(stderr, "[elitepirate] %s is a port stand-in (see PortCElitePirate.cpp)\n", what);
}

// One subdivided arm/leg segment (lbl_15_rodata_0 left, lbl_15_rodata_30 right).
struct SJointPair {
  const char* mPivot;
  const char* mNext;
  float mRadius;
  float mMaxSeparation;
};

const SJointPair skLeftArmJoints[3] = {
    {"L_shoulder", "L_elbow", 1.f, 1.5f},
    {"L_elbow", "L_wrist", 1.1f, 1.3f},
    {"L_knee", "L_ankle", 0.9f, 1.2f},
};
const SJointPair skRightArmJoints[3] = {
    {"R_shoulder", "R_elbow", 1.f, 1.5f},
    {"R_elbow", "R_wrist", 1.1f, 1.3f},
    {"R_knee", "R_ankle", 0.9f, 1.2f},
};

// lbl_15_rodata_60: the single-joint spheres.
struct SJointSphere {
  const char* mName;
  float mRadius;
};
const SJointSphere skJointSpheres[8] = {
    {"Head_1", 1.2f},  {"L_Palm_LCTR", 1.5f}, {"R_Palm_LCTR", 1.5f},
    {"Spine_1", 1.8f}, {"Collar", 1.5f},      {"L_ball", 0.8f},
    {"R_ball", 0.8f},  {"Grenade_Collision_LCTR", 1.f},
};

// @ fn_15_2F10
// True for the palm locator itself or any joint that pivots a subdivided segment of that arm.
bool IsArmClawCollider(const rstl::string& name, const char* locator, const SJointPair* joints) {
  if (name == rstl::string_l(locator)) {
    return true;
  }
  for (int i = 0; i < 3; ++i) {
    if (name == rstl::string_l(joints[i].mPivot)) {
      return true;
    }
  }
  return false;
}

CMaterialFilter SolidFilter() {
  return CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList());
}

} // namespace

// The state files share the stand-in log (PortCElitePirateCombat.cpp / PortCElitePirateMove.cpp).
void ElitePirateStandIn(const char* what) { StandIn(what); }

// @ fn_15_A190
// @ fn_15_2CFC
CElitePirate::CElitePirate(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                           const CTransform4f& xf, const CModelData& modelData,
                           const CPatternedInfo& pInfo, const CActorParameters& actParms,
                           const CElitePirateData& data)
// EPatternedAI 0xc, flavor 0, movement 0 (ground), collider 1, body 1 (bipedal).
: CPatterned(static_cast< EPatternedAI >(0xc), uid, name, kFT_Zero, info, xf, modelData, pInfo,
             kMT_Ground, kCT_One, kBT_BiPedal, actParms)
, x7c0_(-1)
, mBodyVulnerability(pInfo.GetDamageVulnerability())
, mLeftPalmPos(CVector3f::Zero())
, mRightPalmPos(CVector3f::Zero())
, x810_(CVector3f::Zero())
, x81c_(CVector3f::Zero())
, mData(data)
, mCollidableBox(GetBoundingBox(), GetMaterialList())
, mHeadColliderId(kInvalidUniqueId)
, mLauncherId(kInvalidUniqueId)
, mShieldColliderId(kInvalidUniqueId)
, xa26_(kInvalidUniqueId)
, mBaseSpeed(mSpeed)
, mSteeringSpeed(1.f)
, mLastHP(0.f)
, mTimer(0.f)
, mXDamagedTimer(0.f)
, mTime(0.f)
, mStuckTime(0.f)
, mPursueStartTime(0.f) // not written by the ctor; the console's heap is zeroed
, mLastPushTime(0.f)
, mClaimedRegion(0) // [?] retail stores 0, though fn_15_1328 tests it against -1
// The 0x200 flag is the patterned info's encounter byte (+0x120); the radii are rodata 1.0f.
, mPathFindSearch(nullptr, pInfo.IsAnEncounter() ? 0x201 : 1, pInfo.GetPathfindingIndex(), 1.f,
                  1.f, 0, CPFRegion::kRP_Center)
, mPatrolGoal(CVector3f::Zero())
, mPositionHistory(5.f)
, mEvent8Active(false)
, mXDamaged(false)
, mAlerted(false)
, xc10_10_(false)
, mAlerted2(false)
, xc10_04_(false)
, mLauncherIngPossessed(false)
, xc10_01_(false)
, xc14_(0)
, xc28_(-1)
, mAttackKind(-1)
, mFsm2(gpSimplePool->GetObj(SObjectTag('FSM2', data.mStateMachine)))
, xc3c_(6)
, mInitialAnimState(1)
, mEvent7(false)
, xc48_(1)
, xc4c_(1)
, mLastMeleeTime(-1000.f)
, mMeleeDamagePending(false)
, mKnockPending(false)
, xc58_(0.f)
, mShieldUpTime(-1000.f)
, mShieldDamage(0.f)
, mShieldHitTime(-1000.f)
, mShieldAlpha(0.f)
, mShieldPolarity(2)
, mShieldPopPending(false)
, mDoubleSpeed(false)
, xcb0_(1)
, xcb4_(7)
, xcb8_(CVector3f::Zero())
, xcc4_(CVector3f::Zero())
, mProjectileInfo(data.mRocket, data.mRocketDamage)
, mRocketsFired(0)
, mRocketsToFire(0)
, mRocketPattern(0)
, mDefaultRocketPattern(1)
, mRocketEventSeen(false)
, mAimBlend(0.f)
, mShieldAimPos(CVector3f::Zero())
, xd1c_(0.f)
, mAngryCount(0)
, xd24_80_(false)
, xd24_40_(false)
, xd24_20_(false)
, xd24_10_(false)
, mEnergyGlow(0.f)
, mAbsorbing(false) {
  if (mData.mPartAsset != kInvalidAssetId) {
    mPart = TLockedToken< CGenDescription >(
        gpSimplePool->GetObj(SObjectTag('PART', mData.mPartAsset)));
  }
  mProjectileInfo.Token().Lock();

  CKnockBackMgr& knockBack = KnockBackController();
  knockBack.EnableAllAnimReactions(false);
  knockBack.EnableBurnDeath(false);
  knockBack.EnableExplodeDeath(false);
  knockBack.EnableBurn(false);
  knockBack.EnableSlow(false);
  knockBack.EnableFreeze(false);
  knockBack.EnableAnimReaction(CKnockBackMgr::kAR_Flinch, true);

  // fn_15_2CFC: a 3 x 3 x 6 box scaled by the model, shared with the path search.
  const CVector3f scale = GetModelData()->GetScale();
  const float radius = 1.5f * scale.GetY();
  const float height = 6.f * scale.GetZ();
  // The primitive keeps the local box (0x9d0 = the box fn_15_2CFC built); GetBoundingBox() is the
  // world-space box, and an offset primitive never meets the floor.
  const CAABox localBox(-radius, -radius, 0.f, radius, radius, height);
  SetBoundingBox(localBox);
  mCollidableBox = CCollidableAABox(localBox, GetMaterialList());
  mPathFindSearch.SetCharacterRadius(radius);
  mPathFindSearch.SetCharacterHeight(height);

  // The idle variant whose animation is the editor's initial one: retail asks fn_15_9988 for 10,
  // 11 and 12 in turn (0xA780/0xA7A4/0xA7C8, `li r4, 0xa/0xb/0xc`) and stores the first match to
  // 0xc40; none leaves the ctor's 1.
  for (int n = 10; n <= 12; ++n) {
    if (IsInitialIdle(n)) {
      mInitialAnimState = n;
      break;
    }
  }

  if (mData.mShieldedModel != kInvalidAssetId) {
    mShieldModel = TLockedToken< CSkinnedModel >(
        rs_new CSkinnedModel(gpSimplePool->GetObj(SObjectTag('CMDL', mData.mShieldedModel)),
                             gpSimplePool->GetObj(SObjectTag('CSKR', mData.mShieldedSkinRules)),
                             AnimationData()->GetModelData()->GetLayoutInfo()));
  }
}

// @ fn_15_9C70
// The class destructor (vtable slot 2). Retail's body is the generated teardown: the vtable
// pointer (0x9C90), the token at 0xcd0 (0x9CA4), the 0xc58 block (0x9CC0), the FSM2 token at
// 0xc30 (0x9CCC), the attack-kind vector at 0xc14 (0x9CF4), the optional at 0x9e8 (0x9D50), the
// collidable box at 0x9c0 (0x9D7C), the collision managers at 0x9b8 and 0x7f4 (0x9D88, 0x9DBC),
// the data block (0x9D9C), the vulnerability at 0x7c4 (0x9DD0), the base destructor (0x9DDC) and
// the deleting-dtor free (0x9DF0). The port's members are those objects, so their destructors
// are that body.
CElitePirate::~CElitePirate() {}

// @ fn_15_9988
// Is the editor's initial animation (data+4) the best match for state 5 with parms FromEnum(0)
// and FromEnum(idle)? The == test is subf/cntlzw/srwi 5 (0x9A50..0x9A60).
// 0x9A48 `lwz r0, 0x82c(r29)`: data+4.
// 0x9A28 `li r4, 0x5`: the state.
// 0x99F4 `li r4, 0x0`: the first parm; 0x99EC: FromEnum(idle), r4 = the argument.
bool CElitePirate::IsInitialIdle(int idle) const {
  const rstl::pair< float, int > best = GetAnimationData()->GetPASDatabase().FindBestAnimation(
      CPASAnimParmData(pas::EAnimationState(5), CPASAnimParm::FromEnum(0),
                       CPASAnimParm::FromEnum(idle)),
      -1); // 0x9A3C li r6, -0x1
  return mData.mInitialAnim == best.second;
}

// @ fn_15_2284
// The launcher's lock-on locator in world space: the launcher's transform (+0x24, 0x22E0) rotates
// the "lockon_target_LCTR" locator's translation (lbl_15_rodata_4FA, 0x2294; the column-3 words
// 0x6c/0x7c/0x8c of the copy, 0x22D4..0x22E4) and its position (0x54/0x58/0x5c, 0x22FC..0x2310)
// is added. Retail passes this (r4) and never reads it.
CVector3f CElitePirate::GetLockOnPosition(const CActor& launcher) const {
  return launcher.GetTransform().Rotate(
             launcher.GetLocatorTransform("lockon_target_LCTR").GetTranslation()) +
         launcher.GetTranslation();
}

// @ fn_15_1A48
const CShockWaveInfo& CElitePirate::GetShockWaveInfo() const {
  return xcb0_ == 5 ? mData.mDoubleShockWaveInfo : mData.mSingleShockWaveInfo;
}

// @ fn_15_2CE8
bool CElitePirate::IsShieldUp() const { return GetBodyController()->GetLocomotionType() == 0; }

// @ fn_15_9598
void CElitePirate::Think(float dt, CStateManager& mgr) {
  if (!GetActive()) {
    return;
  }
  if (!mStateMachine->HasState()) {
    InstallStateMachine(mgr);
    return;
  }
  if (mShieldPopPending) {
    PopShield(mgr);
    mShieldPopPending = false;
  }
  CPatterned::Think(dt, mgr);

  if (CElitePirateGrenadeLauncher* launcher =
          static_cast< CElitePirateGrenadeLauncher* >(mgr.ObjectById(mLauncherId))) {
    launcher->SetDamageAddColor(mColor);
    launcher->SetSlowedSpeed(GetBodyController()->GetTimeScale());
    launcher->SetFollowPlayer(!GetBodyController()->IsFrozen());
  }
  mBodyCollisionManager->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);
  mShieldCollisionManager->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);

  if (mDoubleSpeed && IsShieldUp()) {
    mSpeed = 2.f * mBaseSpeed;
  } else {
    mSpeed = mBaseSpeed;
  }
  UpdateTimer(dt);
  UpdatePositionHistory();
  UpdateLauncherTransform(mgr);
  UpdateAimBlend(dt);
  UpdateParticleGen(dt);
  mTime += dt;

  if (!mLauncherIngPossessed && mIngPossessionBlend > 0.15f) {
    DeleteGrenadeLauncher(mgr);
    mLauncherId = mgr.AllocateUniqueId();
    CreateGrenadeLauncher(mgr, mLauncherId);
    if (CEntity* launcher = mgr.ObjectById(mLauncherId)) {
      launcher->SetActive(GetActive());
    }
  }
  UpdateShieldAlpha(dt);
  UpdatePushBack(mgr, dt);
}

// @ fn_15_27DC
void CElitePirate::UpdateTimer(float dt) {
  if (mTimer > 0.f) {
    mTimer -= dt;
  }
}

// @ fn_15_2204
void CElitePirate::UpdatePositionHistory() {
  const CVector3f pos = GetTranslation();
  if (mPathFindSearch.OnPath(pos) == CPathFindSearch::kR_Success) {
    mPositionHistory.Clear();
  }
  mPositionHistory.AddValue(pos);
}

// @ fn_15_2394
void CElitePirate::UpdateLauncherTransform(CStateManager& mgr) {
  if (mLauncherId == kInvalidUniqueId) {
    return;
  }
  CActor* launcher = TCastToPtr< CActor >(mgr.ObjectById(mLauncherId));
  if (launcher == nullptr) {
    mLauncherId = kInvalidUniqueId;
  } else {
    launcher->SetTransform(GetLctrTransform(rstl::string_l("grenadeLauncher_LCTR")));
  }
}

// @ fn_15_8D84
void CElitePirate::UpdateAimBlend(float dt) {
  if (IsShieldUp()) {
    mAimBlend += dt / 0.8f;
    if (mAimBlend > 1.f) {
      mAimBlend = 1.f;
    }
  } else {
    mAimBlend -= dt / 0.4f;
    if (mAimBlend < 0.f) {
      mAimBlend = 0.f;
    }
  }
}

// @ fn_15_8E34
void CElitePirate::UpdateParticleGen(float dt) {
  if (mHandGen.get() != nullptr) {
    mHandGen->SetTranslation(mLeftPalmPos);
    mHandGen->Update(dt);
  }
}

// @ fn_15_8EA0
void CElitePirate::UpdateShieldAlpha(float dt) {
  if (IsShieldUp()) {
    mShieldAlpha += dt / 0.8f;
    if (mShieldAlpha > 1.f) {
      mShieldAlpha = 1.f;
    }
  } else {
    mShieldAlpha -= dt * 0.5f;
    if (mShieldAlpha < 0.f) {
      mShieldAlpha = 0.f;
    }
  }
  if (mAbsorbing) {
    mEnergyGlow += dt / 3.f;
    if (mEnergyGlow > 1.f) {
      mEnergyGlow = 1.f;
    }
  } else {
    mEnergyGlow -= dt * 0.5f;
    if (mEnergyGlow < 0.f) {
      mEnergyGlow = 0.f;
    }
  }
}

// @ fn_15_8FB8
// Steps back when the shield-hand line or the launcher's firing line is blocked.
void CElitePirate::UpdatePushBack(CStateManager& mgr, float dt) {
  if (mAttackKind == 3 || mAttackKind == 5 || mAttackKind == 6) {
    return;
  }
  const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid, kMT_AIBlock),
      CMaterialList(kMT_Floor, kMT_Player, kMT_AIPassthrough, kMT_CollisionActor));
  const CVector3f fwd = GetTransform().GetForward();
  const CVector3f pushBack(0.f, -0.15f, 0.f);

  const CVector3f start = mLeftPalmPos + fwd * 1.8f;
  CVector3f dir = (mRightPalmPos + fwd * 1.8f) - start;
  if (dir.CanBeNormalized()) {
    const float len = dir.Magnitude();
    dir.Normalize();
    if (!CGameCollision::RayStaticLineOfSightTest(
            mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()), start, dir, len, filter)) {
      MoveInOneFrameOR(pushBack, dt);
      mLastPushTime = mTime;
      return;
    }
  }

  if (IsOnJumpHint(mgr)) {
    return;
  }
  const CElitePirateGrenadeLauncher* launcher =
      static_cast< const CElitePirateGrenadeLauncher* >(mgr.GetObjectById(mLauncherId));
  if (launcher == nullptr) {
    return;
  }
  CVector3f r(fwd.GetX(), fwd.GetY(), 0.f);
  r.Normalize();
  const CVector3f up = CVector3f::Up();
  const CVector3f head = GetLctrTransform(rstl::string_l("Head_1")).GetTranslation();
  const CVector3f from =
      head + r * 1.5f + CVector3f(r.GetY(), -r.GetX(), 0.f) * 2.2f - up * 0.6f;
  const CVector3f to = launcher->GetTurretTransform().GetTranslation() + r * 2.f + up * 0.1f;
  CVector3f delta = to - from;
  if (!delta.CanBeNormalized()) {
    return;
  }
  const float len = delta.Magnitude();
  delta.Normalize();
  CAABox box = CAABox::MakeMaxInvertedBox();
  box.AccumulateBounds(from);
  box.AccumulateBounds(to);
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, box, filter, this);
  if (!CGameCollision::RayDynamicLineOfSightTest(mgr, from, delta, len, filter, nearList, this)) {
    MoveInOneFrameOR(pushBack, dt);
    mLastPushTime = mTime;
  }
}

// @ fn_15_1788
// Inside an active same-area AI hint of type 0x1a (XY distance).
bool CElitePirate::IsOnJumpHint(const CStateManager& mgr) const {
  const CObjectList& list = mgr.GetObjectListById(kOL_AiWaypoint);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CScriptAIHint* hint = TCastToConstPtr< CScriptAIHint >(list[i]);
    if (hint != nullptr && hint->GetHintType() == 0x1a &&
        hint->GetCurrentAreaId() == GetCurrentAreaId() && hint->GetActive()) {
      const CVector3f d(hint->GetTranslation().GetX() - GetTranslation().GetX(),
                        hint->GetTranslation().GetY() - GetTranslation().GetY(), 0.f);
      if (d.Magnitude() < hint->GetRadius()) {
        return true;
      }
    }
  }
  return false;
}

// @ fn_15_15F8
// The type of the nearest 0x16/0x17/0x1b hint containing the player; a 0x1b wins at once.
int CElitePirate::GetPlayerHintType(const CStateManager& mgr) const {
  const CVector3f playerPos = mgr.GetPlayer(0)->GetTranslation();
  const CObjectList& list = mgr.GetObjectListById(kOL_AiWaypoint);
  const CScriptAIHint* nearest = nullptr;
  float nearestDist = FLT_MAX;
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CScriptAIHint* hint = TCastToConstPtr< CScriptAIHint >(list[i]);
    if (hint == nullptr || hint->GetCurrentAreaId() != GetCurrentAreaId() || !hint->GetActive()) {
      continue;
    }
    const int type = hint->GetHintType();
    if (type != 0x16 && type != 0x1b && type != 0x17) {
      continue;
    }
    const float dist = (hint->GetTranslation() - playerPos).Magnitude();
    if (dist < hint->GetRadius()) {
      if (type == 0x1b) {
        return 0x1b;
      }
      if (dist < nearestDist) {
        nearest = hint;
        nearestDist = dist;
      }
    }
  }
  return nearest != nullptr ? nearest->GetHintType() : -1;
}

// @ fn_15_1598
CPFArea* CElitePirate::GetPathArea(CStateManager& mgr) const {
  return mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()).GetPostConstructed()->mPathArea;
}

// @ fn_15_1328
void CElitePirate::ReleaseClaimedRegion(CStateManager& mgr) {
  if (mClaimedRegion != -1 && GetPathArea(mgr) != nullptr) {
    CPFRegionData* region = GetPathArea(mgr)->GetRegion(mClaimedRegion).Data();
    if (region != nullptr) {
      region->SetAvoidanceFlags(region->GetAvoidanceFlags() & ~4u);
    }
    mClaimedRegion = -1;
  }
}

// @ fn_15_36CC
void CElitePirate::SetupCollisionManagers(CStateManager& mgr) {
  const CAnimData* animData = GetAnimationData();
  rstl::vector< CJointCollisionDescription > descs;
  const SJointPair* tables[2] = {skLeftArmJoints, skRightArmJoints};
  for (int t = 0; t < 2; ++t) {
    for (int i = 0; i < 3; ++i) {
      const SJointPair& joint = tables[t][i];
      const CSegId pivot = animData->GetLocatorSegId(rstl::string_l(joint.mPivot));
      const CSegId next = animData->GetLocatorSegId(rstl::string_l(joint.mNext));
      if (pivot != CSegId::Invalid() && next != CSegId::Invalid()) {
        descs.push_back(CJointCollisionDescription::SphereSubdivideCollision(
            pivot, next, joint.mRadius, joint.mMaxSeparation,
            CJointCollisionDescription::kOT_BetweenJoints, rstl::string_l(joint.mPivot), 5.f));
      }
    }
  }
  for (int i = 0; i < 8; ++i) {
    const CSegId seg = animData->GetLocatorSegId(rstl::string_l(skJointSpheres[i].mName));
    if (seg != CSegId::Invalid()) {
      descs.push_back(CJointCollisionDescription::SphereCollision(
          seg, CVector3f::Zero(), skJointSpheres[i].mRadius,
          rstl::string_l(skJointSpheres[i].mName), 5.f));
    }
  }
  mBodyCollisionManager =
      rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), descs, true);
  mBodyCollisionManager->SetActive(mgr, GetActive());
  mRightArmColliders.clear();
  mLeftArmColliders.clear();

  rstl::vector< CJointCollisionDescription > shieldDescs;
  const CSegId palm = animData->GetLocatorSegId(rstl::string_l("L_Palm_LCTR"));
  shieldDescs.push_back(CJointCollisionDescription::OBBCollision(
      palm, CVector3f(4.f, 4.f, 2.f), CVector3f::Zero(), rstl::string_l("Shield"), 5.f));
  mShieldCollisionManager =
      rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), shieldDescs, false);

  ClassifyCollisionActors(mgr);
  SetMaterialFilter(CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid), CMaterialList(kMT_ProjectilePassthrough, kMT_CollisionActor,
                                              kMT_AIPassthrough, kMT_Player, kMT_Platform)));
  AddMaterial(kMT_ProjectilePassthrough, mgr);

  mShieldColliderId = mShieldCollisionManager->GetCollisionDescFromIndex(0).GetCollisionActorId();
  if (CCollisionActor* shield = TCastToPtr< CCollisionActor >(mgr.ObjectById(mShieldColliderId))) {
    shield->SetResponseType(EWeaponCollisionResponseTypes(0));
  }
  SetShieldVulnerability(mgr, false);
  mShieldCollisionManager->AddMaterialList(mgr, CMaterialList(kMT_AIJoint, kMT_CameraPassthrough));
}

// @ fn_15_3574
void CElitePirate::ClassifyCollisionActors(CStateManager& mgr) {
  for (uint i = 0; i < mBodyCollisionManager->GetNumCollisionActors(); ++i) {
    const CJointCollisionDescription& desc = mBodyCollisionManager->GetCollisionDescFromIndex(i);
    if (TCastToPtr< CCollisionActor >(mgr.ObjectById(desc.GetCollisionActorId())) == nullptr) {
      continue;
    }
    if (desc.GetName() == rstl::string_l("Head_1")) {
      mHeadColliderId = desc.GetCollisionActorId();
    } else if (IsArmClawCollider(desc.GetName(), "R_Palm_LCTR", skRightArmJoints)) {
      mRightArmColliders.push_back(desc.GetCollisionActorId());
    } else if (IsArmClawCollider(desc.GetName(), "L_Palm_LCTR", skLeftArmJoints)) {
      mLeftArmColliders.push_back(desc.GetCollisionActorId());
    }
  }
}

// @ fn_15_27FC
// The body colliders share the Ingsmasher's health; with the shield up they and the shield
// reflect, except that the shield lets the opposite beam (and the Annihilator) through.
void CElitePirate::SetShieldVulnerability(CStateManager& mgr, bool shielded) {
  mLastHP = HealthInfo()->GetHP();
  const CSegId palm = GetAnimationData()->GetLocatorSegId(rstl::string_l("L_Palm_LCTR"));
  for (uint i = 0; i < mBodyCollisionManager->GetNumCollisionActors(); ++i) {
    const CJointCollisionDescription& desc = mBodyCollisionManager->GetCollisionDescFromIndex(i);
    CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(desc.GetCollisionActorId()));
    if (actor == nullptr) {
      continue;
    }
    *actor->HealthInfo() = *HealthInfo();
    actor->SetDamageVulnerability(shielded ? CDamageVulnerability::ReflectVulnerabilty()
                                           : mBodyVulnerability.MakeIgnoreRadius());
    if (desc.GetPivotId() == palm) {
      if (shielded) {
        actor->AddMaterial(kMT_ProjectilePassthrough, mgr);
      } else {
        actor->RemoveMaterial(kMT_ProjectilePassthrough, mgr);
      }
    }
  }

  CCollisionActor* shield = TCastToPtr< CCollisionActor >(mgr.ObjectById(mShieldColliderId));
  if (shield == nullptr) {
    return;
  }
  if (!shielded) {
    shield->SetDamageVulnerability(mBodyVulnerability.MakeIgnoreRadius());
    return;
  }
  CDamageVulnerability vulnerability = CDamageVulnerability::ReflectVulnerabilty();
  const CWeaponTypeVulnerability normal(1.f, CWeaponTypeVulnerability::kE_Normal, false);
  for (int type = 1; type <= 3; ++type) {
    if ((mShieldPolarity == 0 && type == 1) || (mShieldPolarity == 1 && type == 2)) {
      continue;
    }
    vulnerability.SetVulnerability(type, normal);
    vulnerability.SetChargedVulnerability(type, normal);
    vulnerability.SetComboVulnerability(type, normal);
  }
  shield->SetDamageVulnerability(vulnerability);
}

// @ fn_15_2564
void CElitePirate::CreateGrenadeLauncher(CStateManager& mgr, TUniqueId uid) {
  const CAnimationParameters& anim =
      IsIngPossessed() ? mData.mUnknown7E6E0D38 : mData.mRocketLauncherAnimInfo;
  if (anim.GetACSFile() == kInvalidAssetId) {
    return;
  }
  const CModelData modelData(CAnimRes(anim.GetACSFile(), anim.GetCharacter(),
                                      GetModelData()->GetScale(), anim.GetInitialAnimation(),
                                      true));
  mgr.AddObject(rs_new CElitePirateGrenadeLauncher(
      uid, rstl::string_l("Rocket Launcher"),
      CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true), GetTransform(),
      modelData, mData.mRocketLauncherActorInfo, GetUniqueId(), 0.f));
  mLauncherIngPossessed = IsIngPossessed();
}

// @ fn_15_251C
void CElitePirate::DeleteGrenadeLauncher(CStateManager& mgr) {
  mgr.DeleteObjectRequest(mLauncherId);
  mLauncherId = kInvalidUniqueId;
}

// @ fn_15_24F0
// @ fn_15_2448
void CElitePirate::SendLauncherMsg(CStateManager& mgr, bool start) {
  if (mLauncherId == kInvalidUniqueId) {
    return;
  }
  if (CEntity* launcher = mgr.ObjectById(mLauncherId)) {
    mgr.SendScriptMsg(launcher, GetUniqueId(), start ? kSM_Start : kSM_Stop, kInvalidUniqueId);
  }
}

// @ fn_15_7004
void CElitePirate::ApplyMeleeDamage(CStateManager& mgr, TUniqueId target) {
  mgr.ApplyDamage(GetUniqueId(), target, GetUniqueId(), mData.mMeleeDamage, SolidFilter(),
                  CVector3f::Zero());
  mMeleeDamagePending = false;
  mLastMeleeTime = mTime;
}

// @ fn_15_1B60
void CElitePirate::PushPlayer(CStateManager& mgr, float horizontal, float vertical) {
  CPlayer* player = mgr.GetPlayer(0);
  CVector3f dir(player->GetTranslation().GetX() - GetTranslation().GetX(),
                player->GetTranslation().GetY() - GetTranslation().GetY(), 0.f);
  dir.Normalize();
  dir = CVector3f(dir.GetX() * horizontal, dir.GetY() * horizontal, vertical);
  player->Stop();
  player->ApplyImpulseWR(dir * player->GetMass(), CAxisAngle::Identity());
  player->SetMoveState(NPlayer::EPlayerMovementState(2), mgr);
}

// @ fn_15_1C7C
// The stomp: nearby players in morph ball are bounced, others get a camera-shake rumble.
void CElitePirate::ShakePlayers(CStateManager& mgr) {
  const bool notShockwave = mAttackKind != 7;
  for (uint i = 0; i < mgr.GetNumPlayers(); ++i) {
    CPlayer* player = mgr.GetPlayer(i);
    const float dist = (GetTranslation() - player->GetTranslation()).Magnitude();
    const float strength = notShockwave ? 0.25f : 1.f;
    if (!(strength * GetModelData()->GetScale().Magnitude() - 0.05f * dist > 0.f) ||
        player->GetSurfaceRestraint() == 1 || player->GetFluidCount() != 0) {
      continue;
    }
    const int morph = player->GetSpawnedMorphballState() == 0
                          ? static_cast< int >(player->GetMorphballTransitionState())
                          : 0;
    if (morph == 1) {
      const float impulse = notShockwave ? 10.f : 20.f;
      player->Stop();
      player->ApplyImpulseWR(CVector3f::Up() * impulse * player->GetMass(), CAxisAngle::Identity());
      player->SetMoveState(NPlayer::EPlayerMovementState(2), mgr);
    } else if (mgr.CameraManager(i)->GetCurrentCameraId(true) ==
               mgr.CameraManager(i)->GetFirstPersonCamera()->GetUniqueId()) {
      mgr.RumbleManager(mgr.MaskUIdNumPlayers(player->GetUniqueId()))
          ->Rumble(mgr, kRFX_CameraShake, 1.f, kRP_Two);
    }
  }
}

// @ fn_15_3C5C
void CElitePirate::PopShield(CStateManager& mgr) {
  rstl::optional_object< TLockedToken< CGenDescription > >& pop =
      mShieldPolarity == 1 ? mLightShieldPop : mDarkShieldPop;
  const CAssetId asset = mShieldPolarity == 1 ? mData.mLightShieldPop : mData.mDarkShieldPop;
  if (!pop && asset != kInvalidAssetId) {
    pop = TLockedToken< CGenDescription >(gpSimplePool->GetObj(SObjectTag('PART', asset)));
  }
  if (!pop) {
    return;
  }
  CTransform4f xf = GetTransform();
  xf.SetTranslation(mLeftPalmPos);
  mgr.AddObject(rs_new CExplosion(*pop, mgr.AllocateUniqueId(),
                                  CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true),
                                  rstl::string_l("IngSmasher shield pop"), xf, 0,
                                  CVector3f(1.f, 1.f, 1.f), CColor::White(), -1));
}

// @ fn_15_70D4
void CElitePirate::CreateShockWave(CStateManager& mgr, const CInt32POINode& node) {
  CTransform4f xf = GetTransform();
  const CVector3f& palm =
      node.GetLocatorName() == rstl::string_l("R_ball") ? mRightPalmPos : mLeftPalmPos;
  xf.SetTranslation(CVector3f(palm.GetX(), palm.GetY(), GetTranslation().GetZ()));
  mgr.AddObject(rs_new CShockWave(mgr.AllocateUniqueId(), rstl::string_l("Shock Wave"),
                                  CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true),
                                  xf, GetUniqueId(), GetShockWaveInfo(), 2.f, 0.4f));
}

// @ fn_15_728C
// Pattern 1 spreads four rockets across the player (offsets indexed by the rocket count, 1..4);
// pattern 0 fires one homing rocket.
void CElitePirate::FireRocket(CStateManager& mgr) {
  ++mRocketsFired;
  const CElitePirateGrenadeLauncher* launcher =
      static_cast< const CElitePirateGrenadeLauncher* >(mgr.GetObjectById(mLauncherId));
  if (launcher == nullptr || !mProjectileInfo.Token().TryCache()) {
    return;
  }
  CTransform4f xf = launcher->GetTurretTransform();
  const CVector3f origin = xf.GetTranslation();
  TUniqueId homing = kInvalidUniqueId;
  CPlayer* player = mgr.GetPlayer(0);
  if (mRocketPattern == 1) {
    const CVector3f aim = player->GetAimPosition(mgr, 0.f);
    CVector3f d(aim.GetX() - origin.GetX(), aim.GetY() - origin.GetY(), 0.f);
    if (d.CanBeNormalized()) {
      d.Normalize();
      const int hint = GetPlayerHintType(mgr);
      const float s = hint == 0x17 ? 1.5f : 1.f;
      const CVector3f offsets[4] = {
          CVector3f(-d.GetY() * 3.2f * s, d.GetX() * 3.2f * s, 0.f),
          CVector3f(-d.GetY() * 1.2f * s, d.GetX() * 1.2f * s, 0.f),
          CVector3f(d.GetY() * 1.2f * s, -d.GetX() * 1.2f * s, 0.f),
          CVector3f(d.GetY() * 3.2f * s, -d.GetX() * 3.2f * s, 0.f),
      };
      // port limit: fn_15_728C indexes the offsets with `iVar7 = 0xcf8 - 1` and does not clamp -
      // the retail would read past its four-entry rodata table. The volley is armed at four
      // (SpreadShot fn_15_4D18 stores 0xcfc = 4, ProjectileAttack fires only while 0xcf8 < 0xcfc),
      // so 0..3 is unchanged behaviour and the clamp only keeps the port's own array in range.
      const int shot = mRocketsFired < 1 ? 0 : mRocketsFired - 1;
      const int index = shot > 3 ? 3 : shot;
      CVector3f target = player->GetAimPosition(mgr, 0.f) + offsets[index];
      if (hint == 0x17) {
        target = CVector3f(target.GetX(), target.GetY(),
                           target.GetZ() + mgr.Random()->Range(6.f, 12.f));
        homing = player->GetUniqueId();
      }
      xf = CTransform4f::LookAt(origin, target, CVector3f::Up());
    }
  } else if (mRocketPattern == 0) {
    homing = player->GetUniqueId();
  }
  mgr.AddObject(rs_new CEnergyProjectile(
      true, mProjectileInfo.Token(), kWT_AI, xf, kMT_Character, mProjectileInfo.GetDamage(),
      mgr.AllocateUniqueId(), GetCurrentAreaId(), GetUniqueId(), homing, 0, false,
      CVector3f(1.f, 1.f, 1.f), CImpactVisorEffect::None(), false, true, false, 1.f, 4.f, 4.f));
}

// @ fn_15_8580
void CElitePirate::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  const TUniqueId sender = msg.GetSenderId();
  switch (msg.GetMessage()) {
  case kSM_Delete:
    ReleaseClaimedRegion(mgr);
    if (mBodyCollisionManager.get() != nullptr) {
      mBodyCollisionManager->Destroy(mgr);
    }
    if (mShieldCollisionManager.get() != nullptr) {
      mShieldCollisionManager->Destroy(mgr);
    }
    mgr.DeleteObjectRequest(mLauncherId);
    mLauncherId = kInvalidUniqueId;
    break;
  case kSM_Deactivate:
    mBodyCollisionManager->SetActive(mgr, false);
    mShieldCollisionManager->SetActive(mgr, false);
    break;
  case kSM_Alert:
    mAlerted = true;
    mAlerted2 = true;
    break;
  case kSM_Activate:
    mBodyCollisionManager->SetActive(mgr, true);
    if (CEntity* launcher = mgr.ObjectById(mLauncherId)) {
      launcher->SetActive(true);
    }
    break;
  case kSM_Create: {
    BodyController()->Activate(mgr, pas::kAS_Invalid);
    SetupCollisionManagers(mgr);
    mLauncherId = mgr.AllocateUniqueId();
    CreateGrenadeLauncher(mgr, mLauncherId);
    if (CEntity* launcher = mgr.ObjectById(mLauncherId)) {
      launcher->SetActive(GetActive());
    }
    const CBodyStateInfo& info = GetBodyController()->GetBodyStateInfo();
    if (info.GetMaxSpeed() > 0.f) {
      mSteeringSpeed = info.GetLocomotionSpeed(pas::ELocomotionAnim(1)) * 0.99f / info.GetMaxSpeed();
    }
    BodyController()->CommandMgr().SetSteeringBlendMode(kSBM_FullSpeed);
    BodyController()->CommandMgr().SetSteeringSpeedRange(mSteeringSpeed, mSteeringSpeed);
    break;
  }
  case kSM_AreaLoaded:
    mPathFindSearch.SetArea(GetPathArea(mgr));
    break;
  case kSM_ResistedDamage:
  case kSM_ReflectedDamage:
    SetXDamaged(true);
    break;
  case kSM_HitObject: {
    if (!mAlive) {
      break;
    }
    const CCollisionActor* collider = TCastToConstPtr< CCollisionActor >(mgr.GetObjectById(sender));
    if (HealthInfo()->GetHP() > 0.f && mAttackKind == 4 && collider != nullptr) {
      if (const CPlayer* player =
              TCastToConstPtr< CPlayer >(mgr.GetObjectById(collider->GetLastTouchedObject()))) {
        if (mMeleeDamagePending) {
          ApplyMeleeDamage(mgr, player->GetUniqueId());
        }
        if (mKnockPending) {
          PushPlayer(mgr, mgr.GetPlayer(0)->GetMorphballTransitionState() == 1 ? 60.f : 25.f,
                     15.f);
        }
      }
    }
    if (collider != nullptr && collider->GetLastTouchedObject() != GetUniqueId()) {
      const CPlayer* player =
          TCastToConstPtr< CPlayer >(mgr.GetObjectById(collider->GetLastTouchedObject()));
      if (player != nullptr && mCurDamageRemTime <= 0.f && mLastMeleeTime + 1.5f < mTime) {
        mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(), GetContactDamage(),
                        SolidFilter(), CVector3f::Zero());
        mCurDamageRemTime = mDamageWaitTime;
      }
    }
    break;
  }
  case kSM_Damage: {
    const CCollisionActor* collider = TCastToConstPtr< CCollisionActor >(mgr.GetObjectById(sender));
    const CGameProjectile* projectile =
        collider != nullptr
            ? TCastToConstPtr< CGameProjectile >(mgr.GetObjectById(collider->GetLastTouchedObject()))
            : nullptr;
    if (IsShieldUp()) {
      if (projectile != nullptr) {
        const int type = projectile->GetCurrentDamageInfo().GetWeaponMode().GetType();
        if ((mShieldPolarity == 1 && (type == 1 || type == 3)) ||
            (mShieldPolarity == 0 && (type == 2 || type == 3))) {
          mShieldDamage += projectile->GetCurrentDamageInfo().GetDamage();
          mShieldHitTime = mTime;
        }
      }
    } else if (!mAbsorbing) {
      // Retail 0x8B8C: the absorbing check (0xD2C) skips straight to 0x8D50; otherwise the
      // projectile damage (if any) is applied and control reaches 0x8CD0 regardless.
      if (projectile != nullptr) {
        CDamageInfo damage = projectile->GetCurrentDamageInfo();
        damage.SetRadius(0.f);
        mgr.ApplyDamage(projectile->GetUniqueId(), GetUniqueId(), projectile->GetUniqueId(),
                        damage, SolidFilter(), CVector3f::Zero());
        SetXDamaged(true);
      }
      // Freeze/slow update (0x8CD0-0x8D44) runs only with the shield down and not absorbing.
      const float pct = HealthInfo()->GetHP() / HealthInfo()->GetInitialHP() * 100.f;
      KnockBackController().EnableFreeze(pct <= 25.f);
      KnockBackController().EnableSlow(pct <= 25.f);
    }
    return;
  }
  default:
    break;
  }
  CPatterned::AcceptScriptMsg(mgr, msg);
}

// @ fn_15_83C0
void CElitePirate::PreRender(CStateManager& mgr) {
  CPatterned::PreRender(mgr);
  mLeftPalmPos = GetLctrTransform(rstl::string_l("L_Palm_LCTR")).GetTranslation();
  mRightPalmPos = GetLctrTransform(rstl::string_l("R_Palm_LCTR")).GetTranslation();
  if (CActor* launcher = TCastToPtr< CActor >(mgr.ObjectById(mLauncherId))) {
    launcher->SetModelFlags(GetModelFlags());
  }
  SetModelFlags(GetModelFlags().UseShaderSet(0));
  UpdateParticleGen(0.f);
}

// @ fn_15_84EC
void CElitePirate::AddToRenderer(const CStateManager& mgr) const {
  CPatterned::AddToRenderer(mgr);
}

// @ fn_15_8294
// Register 1 flashes white when the shield is hit and fades to black over a second.
void CElitePirate::Render(const CStateManager& mgr) const {
  if (!(mShieldHitTime + 1.f > mTime)) {
    gpRender->SetGXRegister1Color(CColor::Black());
  } else {
    gpRender->SetGXRegister1Color(
        CColor::Lerp(CColor::White(), CColor::Black(), mTime - mShieldHitTime));
  }
  CPatterned::Render(mgr);
  RenderShield(mgr);
  if (mAlive && mHandGen.get() != nullptr) {
    mHandGen->Render();
  }
}

// @ fn_15_8080
void CElitePirate::RenderShield(const CStateManager& mgr) const {
  if ((mShieldAlpha == 0.f && mEnergyGlow == 0.f) || !mShieldModel) {
    return;
  }
  gpRender->SetModelMatrix(GetTransform() *
                           CTransform4f::Scale(GetModelData()->GetScale()));
  CColor color = CColor::White();
  float alpha;
  if (mEnergyGlow > 0.f) {
    alpha = mEnergyGlow * ((sinf(8.f * mTime) + 1.f) * 0.5f * 0.15f + 0.25f);
  } else {
    float freq;
    if (mShieldPolarity == 1) {
      freq = 10.f;
    } else if (mShieldPolarity == 0) {
      color = CColor::Purple();
      freq = 16.f;
    } else {
      return;
    }
    alpha = mShieldAlpha;
    if (alpha == 1.f) {
      alpha = (sinf(freq * mTime) + 1.f) * 0.5f * 0.4f + 0.6f;
    }
  }
  color.SetAlpha(alpha);
  GetAnimationData()->Render(*(**mShieldModel),
                             CModelFlags(CModelFlags::kT_Blend, 0, CModelFlags::EFlags(3), color));
}

// @ fn_15_800C
void CElitePirate::RenderIngSnatchingTransition(const CStateManager& mgr) const {
  CPatterned::RenderIngSnatchingTransition(mgr);
  const CUnitVector3f up = CVector3f::Up();
  GetModelData()->SetupWorldSpacePortalPlane(
      GetTransform(), CPlane(CVector3f::Dot(up, GetTranslation()), up));
}

// @ fn_15_7EEC
// Blends towards the shield collider while the shield is up.
CVector3f CElitePirate::GetAimPosition(const CStateManager& mgr, float dt) const {
  const CVector3f base = CPatterned::GetAimPosition(mgr, dt);
  if (mAimBlend == 0.f) {
    return base;
  }
  if (const CActor* shield = TCastToConstPtr< CActor >(mgr.GetObjectById(mShieldColliderId))) {
    mShieldAimPos = shield->GetTranslation();
  }
  return base * (1.f - mAimBlend) + mShieldAimPos * mAimBlend;
}

// @ fn_15_7BFC
void CElitePirate::DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node,
                                   EUserEventType type, float dt) {
  bool handled = false;
  switch (static_cast< int >(type)) {
  case 0:
    // 0x7C48: only kinds 7, 2 and 8 handle the event; any other kind falls through to
    // CPatterned's projectile (0x7C60/0x7C68 -> 0x7EB4 with r31 = 0).
    if (mAttackKind == 7) {
      CreateShockWave(mgr, node);
      handled = true;
    } else if (mAttackKind == 2 || mAttackKind == 8) {
      FireRocket(mgr);
      handled = true;
    }
    break;
  case 7:
    mEvent7 = true;
    break;
  case 8:
    mEvent8Active = true;
    handled = true;
    break;
  case 9:
    mEvent8Active = false;
    handled = true;
    break;
  case 16:
    // 0x7D90..0x7DC8: the shaker is CEntity::FindConnectedObject's first IS01/ATCH link, not
    // CPatterned::GetConnectedObject's random active one (that draws from the RNG every shake)
    ApplyScreenShake(mgr, GetTranslation(),
                     FindConnectedObject(mgr, kSS_InternalState1, kSM_Attach));
    ShakePlayers(mgr);
    handled = true;
    break;
  case 17:
    if (mAttackKind == 4) {
      mMeleeDamagePending = true;
      mKnockPending = true;
      SetArmTouchBounds(mgr, CVector3f(1.5f, 1.5f, 1.5f));
    }
    break;
  case 18:
    if (mAttackKind == 2) {
      mRocketEventSeen = true;
    } else if (mAttackKind == 4) {
      mMeleeDamagePending = false;
      mKnockPending = false;
      SetArmTouchBounds(mgr, CVector3f::Zero());
    }
    break;
  case 23:
    for (uint i = 0; i < mBodyCollisionManager->GetNumCollisionActors(); ++i) {
      if (CActor* actor = TCastToPtr< CActor >(mgr.ObjectById(
              mBodyCollisionManager->GetCollisionDescFromIndex(i).GetCollisionActorId()))) {
        actor->AddMaterial(kMT_ProjectilePassthrough, mgr);
      }
    }
    handled = true;
    break;
  case 24:
    mBodyCollisionManager->SetActive(mgr, false);
    RemoveMaterial(kMT_Solid, mgr);
    break;
  case 26:
    // 0x7DE0..0x7E18: FOOT/ATCH through FindConnectedObject as well
    ApplyScreenShake(mgr, GetTranslation(), FindConnectedObject(mgr, kSS_Footstep, kSM_Attach));
    handled = true;
    break;
  default:
    break;
  }
  if (!handled) {
    CPatterned::DoUserAnimEvent(mgr, node, type, dt);
  }
}

// @ fn_15_2E04
// Part of fn_15_7BFC (events 17/18): both arms' colliders grow or lose their touch margin.
// The retail's fn_15_2E04 walks one collider list - the count at +0 and the u16 ids after it
// (0x2E50 lhz, 0x2E60 ObjectById, 0x2E64 the CCollisionActor cast) - and writes the three bounds
// floats to the actor's 0x35c/0x360/0x364 (0x2E70, 0x2E74, 0x2E78; the host's
// mExtendedTouchBounds, see CCollisionActor.hpp). The retail calls it with this+0x9fc (the right
// arm) and this+0xa10 (the left); the port does both lists in one call.
void CElitePirate::SetArmTouchBounds(CStateManager& mgr, const CVector3f& bounds) {
  for (int i = 0; i < mRightArmColliders.size(); ++i) {
    if (CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(mRightArmColliders[i]))) {
      actor->SetExtendedTouchBounds(bounds);
    }
  }
  for (int i = 0; i < mLeftArmColliders.size(); ++i) {
    if (CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(mLeftArmColliders[i]))) {
      actor->SetExtendedTouchBounds(bounds);
    }
  }
}

namespace {

// fn_15_9840's tables (lbl_15_data_178, _464, _5CC), in their order. Own members are cast to the
// base-class pointer-to-member types, as CSpacePirate's tables do.
#define EP_TRIGGER(name)                                                                           \
  { #name, reinterpret_cast< CPatterned::StateMachine::TriggerFunc >(&CElitePirate::name) }
#define EP_STATE(name)                                                                             \
  { #name, reinterpret_cast< CPatterned::StateMachine::StateFunc >(&CElitePirate::name) }
#define EP_CODE(name)                                                                              \
  { #name, reinterpret_cast< CPatterned::StateMachine::CodeFunc >(&CElitePirate::name) }

// @ fn_15_C0D0
// The tables fn_15_C0D0 (the module's static initialiser) fills: it copies the 12-byte PTMFs
// {0, vtbl_off, 0} of lbl_15_data_4.. into lbl_15_data_178 (31 triggers, +0x8 per 0x10 entry,
// 0xC18C..0xC3F0), lbl_15_data_464 (21 states, 0xC438..0xC844) and lbl_15_data_5CC (2 codes,
// 0xC85C..0xC8C8); the vtable words lbl_15_data_634+vtbl_off name every entry's function, which
// match the names below one for one (Alerted = vt+0x14c = fn_15_60C8 .. PickAttackType =
// fn_15_54AC). Its trailing float stores (0xC89C..0xC8C8) build two file-static CVector3f:
// lbl_15_bss_10 = (4, 4, 2) (rodata_140, rodata_D8), the shield OBB extent fn_15_36CC passes
// (0x3834; the CVector3f(4.f, 4.f, 2.f) in SetupCollisionManagers), and lbl_15_bss_4, a splat
// read at 0x7CAC/0x7CCC, inlined at its use.
CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    EP_TRIGGER(Alerted), EP_TRIGGER(AngryAttackOver), EP_TRIGGER(AttackPatternOver),
    EP_TRIGGER(BreakProjectileAttack), EP_TRIGGER(CanShockwave), EP_TRIGGER(ClearLineOfSight),
    EP_TRIGGER(DonePursuing), EP_TRIGGER(DoneTurning), EP_TRIGGER(HasAttackPattern),
    EP_TRIGGER(InDetectionRange), EP_TRIGGER(InPosition), EP_TRIGGER(NotReachedTarget),
    EP_TRIGGER(PickedSpreadShot), EP_TRIGGER(PlayerInNoAttack), EP_TRIGGER(PoweredDown),
    EP_TRIGGER(PoweredUp), EP_TRIGGER(ReadyToCharge), EP_TRIGGER(ReturnedToPatrol),
    EP_TRIGGER(ShieldKilled), EP_TRIGGER(ShockwaveIsNext), EP_TRIGGER(ShotAt),
    EP_TRIGGER(ShouldAlert), EP_TRIGGER(ShouldFire), EP_TRIGGER(ShouldMeleeAttack),
    EP_TRIGGER(ShouldShockwave), EP_TRIGGER(ShouldTurn), EP_TRIGGER(SpotPlayer),
    EP_TRIGGER(StillAngry), EP_TRIGGER(TargetNotOnMesh), EP_TRIGGER(TargetUnreachable),
    EP_TRIGGER(TooClose),
};

CPatterned::StateMachine::SStateFunction skStates[] = {
    EP_STATE(Alert), EP_STATE(AngryAttackBegin), EP_STATE(AngryAttackEnd),
    EP_STATE(Dead), EP_STATE(FollowAttackPattern), EP_STATE(InvulnAlert),
    EP_STATE(MeleeAttack), EP_STATE(Patrol), EP_STATE(PowerDown),
    EP_STATE(PowerUp), EP_STATE(ProjectileAttack), EP_STATE(Pursue),
    EP_STATE(Shielding), EP_STATE(ShieldUp), EP_STATE(Shockwave),
    EP_STATE(SpreadShot), EP_STATE(Stunned), EP_STATE(TargetPatrol),
    EP_STATE(Taunt), EP_STATE(Turn), EP_STATE(Wait),
};

CPatterned::StateMachine::SCodeFunction skCodes[] = {
    EP_CODE(SelectTarget), EP_CODE(PickAttackType),
};

#undef EP_TRIGGER
#undef EP_STATE
#undef EP_CODE

} // namespace

// @ fn_15_9840
// CPatterned's tables come from InitializeStateMachine -> SetupStateMachine; the Ingsmasher's own
// (31 triggers, 21 states, 2 code functions) are installed after it, as retail does.
void CElitePirate::InstallStateMachine(CStateManager& mgr) {
  InitializeStateMachine(mgr);
  mStateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  mStateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  mStateMachine->SetCodeFunctions(skCodes, ARRAY_SIZE(skCodes));
}

// @ fn_15_B82C
// The data builder: every argument goes to its slot, nothing is computed. The register arguments:
// r4 -> +0x0 (0xB894), r5 -> +0x4 (0xB8A4), r6 = the melee CDamageInfo -> +0x8..+0x22
// (0xB8A0..0xB914), f1..f5 -> +0x154..+0x164 (0xBA24..0xBA34), f6/f7 -> +0xc4/+0xc8 (0xB9E0/0xB9E4),
// r7 -> +0xcc (0xB9E8), r8 -> +0xd0 (sth, 0xB9EC), r9 -> +0xd4 (0xB9F0), r10 -> +0xd8 (0xB9F4),
// f8 -> +0x24 (0xB91C). The stack words (callee sp+0x118 = caller sp+0x8): 0x11a -> +0xdc (lhz,
// 0xB9F8), 0x11c -> +0xe0 (0xB9FC), 0x120 -> +0x28 (0xB924), 0x124..0x130 -> +0x144..+0x150
// (0xBA10..0xBA20), 0x134 -> +0x2c (0xB92C), 0x138 -> +0x30 (0xB934), 0x13c -> +0x34 (0xB93C),
// 0x142 -> +0x38 (lhz/sth, 0xB94C), 0x144 -> +0x3c (fn_15_B5E0 = CActorParameters copy, 0xB990),
// 0x148 -> +0x9c (0xB9A0..0xB9B0), 0x14c -> +0xa8 (0xB9B8..0xB9C8), 0x150 -> +0x168 (0xBA38),
// 0x154 -> +0x16c..+0x186 (0xBA40..0xBA80), 0x158/0x15c/0x160 -> +0xb4/+0xb8/+0xbc
// (0xB9D0..0xB9D8), 0x166 -> +0xc0 (lhz/sth, 0xB9DC), 0x168/0x16c -> +0xe4/+0x114 (fn_15_B57C,
// 0xBA00/0xBA0C), 0x170/0x174 -> +0x188/+0x18c (0xBA84/0xBA88).
CElitePirateData::CElitePirateData(
    CAssetId stateMachine, int initialAnim, const CDamageInfo& meleeDamage, float maxMeleeRange,
    float minShockwaveRange, float maxShockwaveRange, float minRocketRange, float maxRocketRange,
    float unknown5236C2B6, float unknown01EAAB17, CAssetId darkShield, ushort darkShieldSound,
    CAssetId darkShieldPop, CAssetId lightShield, float tauntInterval, ushort lightShieldSound,
    CAssetId lightShieldPop, float tauntVariance, float shockWaveWeight0, float shockWaveWeight1,
    float shockWaveWeight2, float shockWaveWeight5, float repeatedAttackChance,
    float energyAttractionForce, CAssetId partAsset, ushort alwaysFF23F5E1EE,
    const CActorParameters& rocketLauncherActorInfo,
    const CAnimationParameters& rocketLauncherAnimInfo,
    const CAnimationParameters& unknown7E6E0D38, CAssetId rocket, const CDamageInfo& rocketDamage,
    int unknown624222F8, int unknown31E43A1C, CAssetId visorElectricEffect,
    ushort soundVisorElectric, const SLdrShockWaveInfo& singleShockWaveInfo,
    const SLdrShockWaveInfo& doubleShockWaveInfo, CAssetId shieldedModel,
    CAssetId shieldedSkinRules)
: mStateMachine(stateMachine)
, mInitialAnim(initialAnim)
, mMeleeDamage(meleeDamage)
, mTauntInterval(tauntInterval)
, mTauntVariance(tauntVariance)
, mRepeatedAttackChance(repeatedAttackChance)
, mEnergyAttractionForce(energyAttractionForce)
, mPartAsset(partAsset)
, mAlwaysFF23F5E1EE(alwaysFF23F5E1EE)
, mRocketLauncherActorInfo(rocketLauncherActorInfo)
, mRocketLauncherAnimInfo(rocketLauncherAnimInfo)
, mUnknown7E6E0D38(unknown7E6E0D38)
, mUnknown624222F8(unknown624222F8)
, mUnknown31E43A1C(unknown31E43A1C)
, mVisorElectricEffect(visorElectricEffect)
, mSoundVisorElectric(soundVisorElectric)
, mUnknown5236C2B6(unknown5236C2B6)
, mUnknown01EAAB17(unknown01EAAB17)
, mDarkShield(darkShield)
, mDarkShieldSound(darkShieldSound)
, mDarkShieldPop(darkShieldPop)
, mLightShield(lightShield)
, mLightShieldSound(lightShieldSound)
, mLightShieldPop(lightShieldPop)
, mSingleShockWaveInfo(singleShockWaveInfo)
, mDoubleShockWaveInfo(doubleShockWaveInfo)
, mUnknown28B39197(shockWaveWeight0)
, mUnknownE27DE71B(shockWaveWeight1)
, mUnknown665E7ACE(shockWaveWeight2)
, mUnknownACD4D06D(shockWaveWeight5)
, mMaxMeleeRange(maxMeleeRange)
, mMinShockwaveRange(minShockwaveRange)
, mMaxShockwaveRange(maxShockwaveRange)
, mMinRocketRange(minRocketRange)
, mMaxRocketRange(maxRocketRange)
, mRocket(rocket)
, mRocketDamage(rocketDamage)
, mShieldedModel(shieldedModel)
, mShieldedSkinRules(shieldedSkinRules) {}

// @ fn_15_178
// The loader. The SLdrElitePirate members sit at r1+0x694 (the property dispatch 0x1E4..0x88C reads
// all 43 ids into the generated struct's fields in its order); fn_15_10C0 is its constructor.
static CEntity* PortLoadElitePirate(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrElitePirate sldrThis;
  // fn_15_10C0's defaults that the generated constructor does not apply: the three sound ids are
  // -1 (0x10F0 li r0, -0x1; 0x1104 stw 0x330 and 0x1110 stw 0x33c = darkShieldSound and
  // lightShieldSound; 0x1158 li r0, -0x1; 0x1164 stw 0x484 = sound_VisorElectric), which the
  // u16 masks below make 0xffff (no sound). Its other stores are the generated defaults.
  sldrThis.darkShieldSound = -1;
  sldrThis.lightShieldSound = -1;
  sldrThis.sound_VisorElectric = -1;
#include "MetroidPrime/ScriptLoader/SLdrElitePirate.inc"

  // 0x8AC fn_8023A0B8 = LdrToModelData(scale, kInvalidAssetId (li r5, -0x1), animation set, true).
  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    // 0x8BC lbz 0x1fc / 0x8C0 cmplwi 0: the optional's valid byte; clear returns null (0x974).
    return nullptr;
  }

  // 0x97C..0x9B4, in retail's order: the rocket damage, the two animation sets (0x7e6e0d38 at
  // r1+0x70, the launcher's at r1+0x7c), the launcher's actor parameters, the melee damage.
  const CDamageInfo rocketDamage = LdrToDamageInfo(sldrThis.rocketDamage);
  const CAnimationParameters unknown7E6E0D38 =
      LdrToAnimationParameters(sldrThis.unknown_0x7e6e0d38);
  const CAnimationParameters rocketLauncherAnimInfo =
      LdrToAnimationParameters(sldrThis.rocketLauncherAnimInfo);
  const CActorParameters rocketLauncherActorInfo =
      LdrToActorParameters(sldrThis.rocketLauncherActorInfo);
  const CDamageInfo meleeDamage = LdrToDamageInfo(sldrThis.meleeDamage);
  // 0x9B8..0xAC0: r4 = patterned+0x1e4 (lwz 0x8b4), r5 = patterned+0x1dc (lwz 0x8ac, the
  // animation set's initial_anim), f1..f7 = SLdr 0x99c..0x9b4, f8 = 0x9d8; the sound ids go in
  // clrlwi 16 (0x9C8, 0xA28, 0xA64, 0xA90). energyAbsorbDuration/Variance (0xa70/0xa74) are not
  // passed.
  const CElitePirateData data(
      sldrThis.patterned.stateMachine2, sldrThis.patterned.animationInformation.initial_anim,
      meleeDamage, sldrThis.maxMeleeRange, sldrThis.minShockwaveRange,
      sldrThis.maxShockwaveRange, sldrThis.minRocketRange, sldrThis.maxRocketRange,
      sldrThis.unknown_0x5236c2b6, sldrThis.unknown_0x01eaab17, sldrThis.darkShield,
      static_cast< ushort >(sldrThis.darkShieldSound), sldrThis.darkShieldPop,
      sldrThis.lightShield, sldrThis.tauntInterval,
      static_cast< ushort >(sldrThis.lightShieldSound), sldrThis.lightShieldPop,
      sldrThis.tauntVariance, sldrThis.unknown_0x28b39197, sldrThis.unknown_0xe27de71b,
      sldrThis.unknown_0x665e7ace, sldrThis.unknown_0xacd4d06d, sldrThis.repeatedAttackChance,
      sldrThis.energyAttractionForce, sldrThis.alwaysFF,
      static_cast< ushort >(sldrThis.alwaysFF_0x23f5e1ee), rocketLauncherActorInfo,
      rocketLauncherAnimInfo, unknown7E6E0D38, sldrThis.rocket, rocketDamage,
      sldrThis.unknown_0x624222f8, sldrThis.unknown_0x31e43a1c, sldrThis.visorElectricEffect,
      static_cast< ushort >(sldrThis.sound_VisorElectric), sldrThis.singleShockWaveInfo,
      sldrThis.doubleShockWaveInfo, sldrThis.shieldedModel, sldrThis.shieldedSkinRules);

  // 0xAD0 li r3, 0xd30: rs_new; then (0xAF4..0xB5C) LdrToPatternedInfo(patterned, &ing
  // possession data), AllocateUniqueId, LoadActorParameters, LoadEditorTransform, LdrToEntityInfo,
  // and fn_15_A190 with patternedInfo (r9) before actorParameters (r10).
  return rs_new CElitePirate(mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
                             LdrToEntityInfo(info, sldrThis.editorProperties),
                             LdrToTransform4f(sldrThis.editorProperties), *modelData,
                             LdrToPatternedInfo(sldrThis.patterned, &sldrThis.ingPossessionData),
                             LdrToActorParameters(sldrThis.actorInformation), data);
}

static SElitePirate_FuncPtrs REL_loader_ElitePirate;

// Retail's RELMain calls fn_15_148, which stores the loader fn_15_178 into lbl_15_bss_0 (the
// module's SElitePirate_FuncPtrs, a single loader slot) and calls the DOL's SetSElitePirate_FuncPtrs
// (fn_80218AD4). The proxy in SElitePirate_FuncPtrs.cpp calls through it.
void RELMain() {
  REL_loader_ElitePirate.mLoader = PortLoadElitePirate;
  SetSElitePirate_FuncPtrs(&REL_loader_ElitePirate);
}

void RELExit() { SetSElitePirate_FuncPtrs(nullptr); }

// ---- FSM triggers/states (later slices)

namespace {

// Retail's 0x17/0x1b hint types: a jump-down zone / a "no attack" zone around the player.
const int kHintJumpDown = 0x17;
const int kHintNoAttack = 0x1b;
// Angle (rad) past which the Ingsmasher must turn before it attacks (lbl_804... 0.5497787).
const float kTurnAngle = 0.5497787f;

// The 2D angle between this actor's forward vector and the direction to `target`.
float AngleToTarget(const CActor& self, const CActor& target) {
  const CVector3f d = target.GetTranslation() - self.GetTranslation();
  const CVector3f fwd = self.GetTransform().GetForward();
  return CVector2f::GetAngleDiff(CVector2f(fwd.GetX(), fwd.GetY()), CVector2f(d.GetX(), d.GetY()));
}

float DistSq(const CVector3f& a, const CVector3f& b) {
  const CVector3f d = a - b;
  return d.GetX() * d.GetX() + d.GetY() * d.GetY() + d.GetZ() * d.GetZ();
}

} // namespace

// @ fn_15_60C8
bool CElitePirate::Alerted(CStateManager&, const CTriggerData&) const { return mAlerted2; }

// @ fn_15_18CC
bool CElitePirate::AngryAttackOver(CStateManager&, const CTriggerData&) const {
  return xd24_40_;
}

// @ fn_15_6F28
bool CElitePirate::AttackPatternOver(CStateManager&, const CTriggerData&) const {
  return mWaypointNavigation.GetDestination() == kInvalidUniqueId;
}

// @ fn_15_50B4
bool CElitePirate::BreakProjectileAttack(CStateManager&, const CTriggerData&) const {
  return mRocketEventSeen;
}

// @ fn_15_5084
bool CElitePirate::CanShockwave(CStateManager& mgr, const CTriggerData&) const {
  return GetPlayerHintType(mgr) != kHintJumpDown;
}

// @ fn_15_4E90
// A static line of sight from the launcher's turret to the player, when the player is within
// 1/sqrt(2) of the shock wave range in the XY plane.
bool CElitePirate::ClearLineOfSight(CStateManager& mgr, const CTriggerData&) const {
  const CElitePirateGrenadeLauncher* launcher =
      static_cast< const CElitePirateGrenadeLauncher* >(mgr.GetObjectById(mLauncherId));
  if (launcher == nullptr) {
    return false;
  }
  const CVector3f start = launcher->GetTurretTransform().GetTranslation();
  CVector3f d = mgr.GetPlayer(0)->GetAimPosition(mgr, 0.f) - start;
  if (!d.CanBeNormalized()) {
    return false;
  }
  if (d.GetX() * d.GetX() + d.GetY() * d.GetY() >
      mData.mMaxShockwaveRange * mData.mMaxShockwaveRange * 0.5f) {
    return false;
  }
  const CMaterialFilter filter =
      CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList(kMT_Floor));
  const float length = d.Magnitude();
  d.Normalize();
  return CGameCollision::RayStaticLineOfSightTest(
      mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()), start, d, length, filter);
}

// @ fn_15_691C
bool CElitePirate::DonePursuing(CStateManager& mgr, const CTriggerData& data) const {
  if (mStuckTime > 0.5f) {
    return true;
  }
  const int hint = GetPlayerHintType(mgr);
  if (hint == kHintNoAttack) {
    return true;
  }
  if (hint == kHintJumpDown) {
    if (PathShagged(mgr, data) || mPathFindSearch.IsOver()) {
      return true;
    }
  }
  const CPlayer* player = mgr.GetPlayer(0);
  if (!(mLastPushTime > mPursueStartTime)) {
    const CVector3f d = player->GetAimPosition(mgr, 0.f) - GetTranslation();
    if (!(DistSq(d, CVector3f::Zero()) < mData.mMaxMeleeRange * mData.mMaxMeleeRange * 4.f)) {
      if (mTime < mPursueStartTime + 2.5f) {
        if (ClearLineOfSight(mgr, data) || IsOnJumpHint(mgr)) {
          return true;
        }
      }
      return mPredictedLeashTime > 3.f;
    }
    return true;
  }
  if (GetCurrentAreaId() != player->GetCurrentAreaId()) {
    return true;
  }
  if (hint != kHintJumpDown) {
    const CVector3f d = player->GetAimPosition(mgr, 0.f) - GetTranslation();
    if (DistSq(d, CVector3f::Zero()) < mData.mMaxShockwaveRange * mData.mMaxShockwaveRange) {
      return true;
    }
  }
  if (IsOnJumpHint(mgr)) {
    return true;
  }
  return ClearLineOfSight(mgr, data);
}

// @ fn_15_6C64
bool CElitePirate::DoneTurning(CStateManager& mgr, const CTriggerData& data) const {
  return AnimOver(mgr, data);
}

// @ fn_15_6F44
bool CElitePirate::HasAttackPattern(CStateManager& mgr, const CTriggerData&) const {
  return GetConnectedObject(mgr, kSS_Attack, kSM_Follow) != kInvalidUniqueId;
}

// @ fn_15_6F9C
bool CElitePirate::InDetectionRange(CStateManager& mgr, const CTriggerData& data) const {
  return mAlerted ? true : CPatterned::InDetectionRange(mgr, data);
}

// @ fn_15_6368
bool CElitePirate::InPosition(CStateManager&, const CTriggerData&) const {
  return DistSq(mPatrolGoal, GetTranslation()) < 25.f;
}

// @ fn_15_6D94
bool CElitePirate::NotReachedTarget(CStateManager&, const CTriggerData&) const {
  if ((mShieldUpTime + mTime) * 0.5f < mLastPushTime) {
    return true;
  }
  return mShieldUpTime + 15.f < mTime;
}

// @ fn_15_4E7C
bool CElitePirate::PickedSpreadShot(CStateManager&, const CTriggerData&) const {
  return mRocketPattern == 1;
}

// @ fn_15_15CC
bool CElitePirate::PlayerInNoAttack(CStateManager& mgr, const CTriggerData&) const {
  return GetPlayerHintType(mgr) == kHintNoAttack;
}

// @ fn_15_61CC
bool CElitePirate::PoweredDown(CStateManager&, const CTriggerData&) const {
  return mInitialAnimState != 1;
}

// @ fn_15_61C0
bool CElitePirate::PoweredUp(CStateManager&, const CTriggerData&) const { return mEvent7; }

// @ fn_15_60D4
// Retail's virtual call at vt+0x194 is word 101 of lbl_15_data_634 = fn_15_6D68 (ShieldKilled);
// its neighbours are fn_15_60D4 itself (word 100) and fn_15_18AC (102, ShockwaveIsNext). So in a
// no-attack hint region (fn_15_15F8 answers 0x1b) the shield has to be broken first.
// The test is `0xc5c + (HealthInfo[1] / HealthInfo[0]) * 1.7 + 0.3 < 0xa3c` (fn_15_60D4): more
// than 0.3 s plus 1.7 s scaled by the remaining HP fraction (current HP at +4 over the initial HP
// at +0, the same ratio fn_15_8580 scales to percent) since the shield went up, so a damaged
// Ingsmasher charges sooner.
bool CElitePirate::ReadyToCharge(CStateManager& mgr, const CTriggerData& data) const {
  if (GetPlayerHintType(mgr) == kHintNoAttack) {
    if (!ShieldKilled(mgr, data)) {
      return false;
    }
  }
  const CHealthInfo* health = GetHealthInfo();
  return mShieldUpTime + (health->GetHP() / health->GetInitialHP()) * 1.7f + 0.3f < mTime;
}

// @ fn_15_60BC
bool CElitePirate::ReturnedToPatrol(CStateManager&, const CTriggerData&) const {
  return xc10_04_;
}

// @ fn_15_6D68
bool CElitePirate::ShieldKilled(CStateManager&, const CTriggerData&) const {
  const float limit =
      mShieldPolarity == 1 ? mData.mUnknown5236C2B6 : mData.mUnknown01EAAB17;
  return limit < mShieldDamage;
}

// @ fn_15_18AC
bool CElitePirate::ShockwaveIsNext(CStateManager&, const CTriggerData&) const {
  return xd24_80_;
}

// @ fn_15_63B0
bool CElitePirate::ShotAt(CStateManager&, const CTriggerData&) const { return mXDamaged; }

// @ fn_15_684C
// False once the alert latch (0xc10 bit 0) is set, and false while it stands within 5 m of x81c_ -
// the place it powered up in: fn_15_5F08 copies the actor's own translation there at PowerUp, and
// the ctor only zeroes it; nothing else in the REL writes it. The distance is measured from this
// actor's own position (0x54/0x58/0x5c), not the player's, so the reading is "do not alert at the
// spot where I powered up", not an alert position for the player.
bool CElitePirate::ShouldAlert(CStateManager&, const CTriggerData&) const {
  if (xc10_01_) {
    return false;
  }
  if (x81c_ != CVector3f::Zero() && !(DistSq(x81c_, GetTranslation()) > 25.f)) {
    return false;
  }
  return true;
}

// @ fn_15_6618
// @ fn_15_6820
// Every arm ran (ElitePirate.md section 11): the early-outs forced with `var ep.*`, the `true`
// return 0x67F4 forced with `material -solid` (the ray ends at the player's aim point, inside its
// own kMT_Solid primitive, and here also crosses two closed doors and the pirate's own collision
// actors), as entering a turret does to the player (CPlayer.cpp:4337); retail's ray is the same (ray1).
// The launcher can fire: no cooldown, a target in the rocket range band, the body already turned
// to it, and (retail's RayCollideWorld result) a clear shot from the launcher's lock-on locator.
// fn_15_6820 is the thunk that passes &x9fa (mLauncherId) as `id`'s source (0x6830 lhz 0x9fa).
bool CElitePirate::ShouldFire(CStateManager& mgr, const CTriggerData& data) const {
  // 0x6644..0x6650 `cror eq, lt, eq; bne`: anything but xa34 <= 0 (NaN too) returns false
  if (!(mTimer <= 0.f) || mLauncherId == kInvalidUniqueId) { // 0x6660 cmplw
    return false;
  }
  const CElitePirateGrenadeLauncher* launcher =
      static_cast< const CElitePirateGrenadeLauncher* >(mgr.GetObjectById(mLauncherId));
  if (launcher == nullptr) { // 0x6678 mr. r28
    return false;
  }
  // 0x6680..0x6698: retail only null-checks the xa26 entity and calls its vtable +0x54
  // (GetAimPosition) directly; there is no actor cast.
  const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(xa26_));
  if (target == nullptr) { // 0x6694 cmplwi r3,0
    return false;
  }
  const CVector3f aim = target->GetAimPosition(mgr, 0.f); // 0x66C0 bctrl +0x54, f1 = 0.0f
  const float distSq = DistSq(aim, GetTranslation());    // 0x66DC..0x6704 vs 0x54/0x58/0x5c
  // 0x6708 `ble` (0x988) and 0x6718 `bge` (0x98c) to the false return: both are taken on a
  // NaN, so a NaN distance returns false
  if (!(mData.mMinRocketRange * mData.mMinRocketRange < distSq &&
        distSq < mData.mMaxRocketRange * mData.mMaxRocketRange)) {
    return false;
  }
  // 0x6740 vtable +0x1a4 = ShouldTurn (with a zeroed CTriggerData at 0x10; ShouldTurn ignores it)
  if (ShouldTurn(mgr, data)) { // 0x674C clrlwi. / bne
    return false;
  }
  const CVector3f lockon = GetLockOnPosition(*launcher); // 0x6760 bl fn_15_2284
  // 0x6778..0x67B4: (1 << data_604 = 0x3B kMT_Solid) | (1 << data_608 = 0x21 kMT_Character);
  // 0x67DC/0x67E0 exclude 0, 0x67E4 type 1 = kFT_Include.
  const CMaterialFilter filter =
      CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid, kMT_Character));
  return mgr.RayCollideWorld(lockon, aim, filter, this); // 0x67E8, 0x67F0 beq, 0x67F4 true
}

// Host: the console's `var <id> <name> [value]` for the Ingsmasher, so ShouldFire's early-out
// arms run without gdb: ep.timer is the attack cooldown 0xa34 (whole seconds), ep.launcher the
// launcher uid 0x9fa, ep.target the target uid 0xa26 (65535 = kInvalidUniqueId), ep.yaw the facing (degrees,
// the console `face` convention) so a placed pirate can be turned to the player.
bool CElitePirate::PortConsoleVar(CEntity& ent, const char* name, const int* value, int& current) {
  CElitePirate* ep = TCastToPtr< CElitePirate >(&ent);
  if (ep == nullptr) {
    return false;
  }
  if (strcmp(name, "ep.timer") == 0) {
    if (value != nullptr) {
      ep->mTimer = static_cast< float >(*value);
    }
    current = static_cast< int >(ep->mTimer);
    return true;
  }
  if (strcmp(name, "ep.yaw") == 0) {
    // the console `face` convention: 0 faces +y, 90 faces +x; the position is kept
    const CVector3f pos = ep->GetTranslation();
    if (value != nullptr) {
      const float radians = static_cast< float >(*value) * 3.14159265358979f / 180.f;
      const CVector3f dir(sinf(radians), cosf(radians), 0.f);
      ep->SetTransform(CTransform4f::LookAt(pos, pos + dir, CVector3f::Up()));
    }
    const CVector3f fwd = ep->GetTransform().GetForward();
    current = static_cast< int >(atan2f(fwd.GetX(), fwd.GetY()) * 180.f / 3.14159265358979f);
    return true;
  }
  TUniqueId* uid = nullptr;
  if (strcmp(name, "ep.launcher") == 0) {
    uid = &ep->mLauncherId;
  } else if (strcmp(name, "ep.target") == 0) {
    uid = &ep->xa26_;
  } else {
    return false;
  }
  if (value != nullptr) {
    *uid = TUniqueId(static_cast< ushort >(*value));
  }
  current = uid->value;
  return true;
}

namespace {
const bool sEliteConsoleVars = PortConsole::RegisterVar(&CElitePirate::PortConsoleVar);
} // namespace

// @ fn_15_6500
bool CElitePirate::ShouldMeleeAttack(CStateManager& mgr, const CTriggerData&) const {
  if (GetPlayerHintType(mgr) != -1) {
    return false;
  }
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(xa26_));
  if (target == nullptr ||
      !(DistSq(target->GetTranslation(), GetTranslation()) <=
        mData.mMaxMeleeRange * mData.mMaxMeleeRange)) {
    return false;
  }
  return AngleToTarget(*this, *target) < kTurnAngle;
}

// @ fn_15_63BC
bool CElitePirate::ShouldShockwave(CStateManager& mgr, const CTriggerData&) const {
  if (!(mTimer <= 0.f)) {
    return false;
  }
  for (uint i = 0; i < mgr.GetNumPlayers(); ++i) {
    const CPlayer* player = mgr.GetPlayer(i);
    if (GetCurrentAreaId() != player->GetCurrentAreaId()) {
      continue;
    }
    const CVector3f aim = player->GetAimPosition(mgr, 0.f);
    const float distSq = DistSq(aim, GetTranslation());
    if (!(distSq <= mData.mMaxShockwaveRange * mData.mMaxShockwaveRange)) {
      continue;
    }
    const bool morphed =
        player->GetSpawnedMorphballState() == CPlayer::kMS_Unmorphed &&
        player->GetMorphballTransitionState() == CPlayer::kMS_Morphed;
    if (morphed) {
      return true;
    }
    if (mData.mMinShockwaveRange * mData.mMinShockwaveRange <= distSq) {
      return fabsf(aim.GetZ() - GetTranslation().GetZ()) < 3.f;
    }
  }
  return false;
}

// @ fn_15_6C84
bool CElitePirate::ShouldTurn(CStateManager& mgr, const CTriggerData&) const {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(xa26_));
  return target != nullptr && AngleToTarget(*this, *target) > kTurnAngle;
}

// @ fn_15_6FD0
bool CElitePirate::SpotPlayer(CStateManager& mgr, const CTriggerData& data) const {
  return mAlerted ? true : CPatterned::SpotPlayer(mgr, data);
}

// @ fn_15_18B8
bool CElitePirate::StillAngry(CStateManager&, const CTriggerData&) const {
  return mAngryCount > 0;
}

// @ fn_15_6240
bool CElitePirate::TargetNotOnMesh(CStateManager& mgr, const CTriggerData&) const {
  const int hint = GetPlayerHintType(mgr);
  if (hint == kHintNoAttack) {
    return false;
  }
  if (hint == kHintJumpDown) {
    return true;
  }
  return mPathFindSearch.NearlyOnPath(mgr.GetPlayer(0)->GetTranslation(), 4.f) !=
         CPathFindSearch::kR_Success;
}

// @ fn_15_61E4
bool CElitePirate::TargetUnreachable(CStateManager& mgr, const CTriggerData&) const {
  const int hint = GetPlayerHintType(mgr);
  if (hint == kHintJumpDown || hint == kHintNoAttack) {
    return true;
  }
  return mStuckTime > 1.f;
}

// @ fn_15_62CC
bool CElitePirate::TooClose(CStateManager& mgr, const CTriggerData&) const {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(xa26_));
  return target != nullptr &&
         DistSq(GetTranslation(), target->GetTranslation()) <
             mData.mMaxMeleeRange * mData.mMaxMeleeRange * 0.5f;
}

// @ fn_15_555C
void CElitePirate::SelectTarget(CStateManager& mgr, float) {
  xa26_ = CScriptTeamAiMgr::ChoosePlayer(mgr, *this);
}

// @ fn_15_54AC
// Sets the rocket pattern for the coming attack state: a jump-down hint (0x17) forces the spread,
// otherwise Random(0,1) >= 0.8 copies mDefaultRocketPattern (0xd04) and the other 80% takes its
// opposite. fn_15_A190 (the ctor, `param_1[0x341] = 1`) is 0xd04's only writer in the REL, so the
// 20% is a fixed spread and the 80% a homing salvo - not a repeat of the previous pick.
void CElitePirate::PickAttackType(CStateManager& mgr, float) {
  if (!mgr.IsRandomAvailable()) {
    return;
  }
  if (GetPlayerHintType(mgr) == kHintJumpDown) {
    mRocketPattern = 1;
  } else if (!(mgr.Random()->Range(0.f, 1.f) < 0.8f)) {
    mRocketPattern = mDefaultRocketPattern;
  } else {
    mRocketPattern = mDefaultRocketPattern == 0 ? 1 : 0;
  }
}
