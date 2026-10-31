// Host body for REL module SwampBossStage2 (module 79): Chykka's adult form (light flyer with
// wing growths, dark flyer with ovipositor). Ported from the retail REL pseudocode; see
// docs/module-analysis/SwampBoss-stage2.md for the function map and retail offsets.
//
// One retail body has no port body on purpose: fn_79_1B38 (0x1B38..0x1C44), the standalone
// "play the additive animation the PAS search returns" player. Nothing references it - the
// module's .text holds no `bl` for it, the .data/.rodata hold no word for it, and neither the
// vtable (lbl_79_data_424) nor the code list (lbl_79_data_3F4) lists it - so it is class T
// (dead code) and re-deriving it would add code no path can reach (the note's 7.27).

#include "MetroidPrime/Enemies/CSwampBossStage2.hpp"

#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CRandom16.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "Kyoto/Text/CStringTable.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Collision/CJointCollisionDescription.hpp"
#include "MetroidPrime/Enemies/CAiKnockBackMgr.hpp"
#include "MetroidPrime/Enemies/CPatternedInfo.hpp"
#include "MetroidPrime/Factories/CScannableObjectInfo.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CScriptGrapplePoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWater.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CEnergyProjectile.hpp"
#include "MetroidPrime/Weapons/CShockWave.hpp"
#include "MetroidPrime/Weapons/CWeapon.hpp"

#include "MetaRender/CCubeRenderer.hpp"

#include <float.h>

namespace {
// rodata: collision actors are kept at this HP and damage is read as the shortfall.
const float kCollisionActorHP = 500.f;
// rodata: wing-growth locators, in growth slot order (fn_79_67A4).
const char* const kGrowthLocators[4] = {
    "L_backWing_1",
    "L_frontWing_1",
    "R_backWing_1",
    "R_frontWing_1",
};
// rodata: wing-target effect names, in growth slot order (fn_79_5BC8).
const char* const kGrowthTargetEffects[4] = {
    "WingTarget_LB",
    "WingTarget_LF",
    "WingTarget_RB",
    "WingTarget_RF",
};
// fn_79_6E68: sphere joints, all with mass 1000.
struct SSphereJoint {
  const char* mName;
  float mRadius;
};
const SSphereJoint kSphereJoints[10] = {
    {"L_backWing_lockon_SDK_LCTR", 3.f}, {"L_frontWing_lockon_SDK_LCTR", 3.f},
    {"R_backWing_lockon_SDK_LCTR", 3.f}, {"R_frontWing_lockon_SDK_LCTR", 3.f},
    {"spine_5_scale2", 5.f},             {"L_mouth", 5.f},
    {"spine6_Lockon_LCTR", 3.f},         {"spine_5", 4.f},
    {"spine_3", 3.f},                    {"spine_1", 3.f},
};
const float kJointMass = 1000.f;
// Collision-actor indices into kSphereJoints.
const int kHeadActor = 5;      // L_mouth -> mHeadPos
const int kTailActor = 4;      // spine_5_scale2 -> mTailPos, ovipositor in head mode
const int kSplashActor = 6;    // spine6_Lockon_LCTR -> mSplashPos

float RandomRange(CStateManager& mgr, float lo, float hi) {
  return mgr.Random()->Float() * (hi - lo) + lo;
}

// fn_79_3E50: retail rebuilds the AABB collidable's vtable and copies its material list and box
// into the stack copy the water splash probe tests against; the host equivalent is a copy.
CCollidableAABox CopyCollidableAABox(const CCollidableAABox& source) { return source; }
} // namespace

// @ fn_79_BE3C
CSwampBossStage2::CSwampBossStage2(TUniqueId uid, const rstl::string& name,
                                   const CEntityInfo& info, const CTransform4f& xf,
                                   const CModelData& modelData, const CActorParameters& actParms,
                                   const CPatternedInfo& pInfo,
                                   const SLdrSwampBossStage2Data& data)
: CPatterned(static_cast< EPatternedAI >(0x3c), uid, name, kFT_Zero, info, xf, modelData, pInfo,
             kMT_Flyer, kCT_One, static_cast< EBodyType >(7), actParms)
, mData(data)
, mHeadPos(CVector3f::Zero())
, mTailPos(CVector3f::Zero())
, mSplashPos(CVector3f::Zero())
, mSwoopNode(kInvalidUniqueId)
// Retail fn_79_BE3C 0xC0A4-0xC0B0: data+0x44 (lightFlyer1.health) -> 0xC4C and data+0x80
// (darkFlyer1.health) -> 0xC50.
, mHeadHealth(data.lightFlyer1.health)
, mOvipositorHealth(data.darkFlyer1.health)
, mCenter(kInvalidUniqueId)
, mEdgeMarker(kInvalidUniqueId)
, mReelDir(CVector3f::Zero())
, mDashing(false)
, mSwoopDone(false)
, mSwoopDamage(false)
, mSwoopWindow(false)
, mSurfaced(false)
, mGrowthDestroyed(false)
, mGrowthsVulnerable(false)
, mHeadMode(false)
, mCircleDamaged(false)
, mSwoopRepeat(false)
, mFacePlayer(false)
, mRotateSign(false)
, mClampHealth(true)
, mPlayerInWater(false)
, mAttackReady(false)
, mLightFlyer(false) // the retail's ctor clears the flag (0xC0E8, mask 0x1 of the byte at 0xc76)
, mMoveRequested(false)
, mSwoopSide(false)
, mCircleDone(false)
, mSwooping(false)
, mCircleCounting(false)
, mSwoopSelected(false)
, mUnusedC77Bit0(false)
, mReelEffect(false)
, mTargetEffectsOn(false)
, mDarkFlyerForm(false)
, mBlowing(false)
, mBlowWindowOpen(false)
, mWindEffectPending(false)
, mBubbleTelegraphed(false)
, mAdditiveAnim(-1)
, mAdditiveWeight(0.f)
, mAdditiveRate(0.f)
, mAdditiveJitterX(0.f)
, mAdditiveJitterY(0.f)
, mChainDepth(0)
, mGrowthVulnerableTime(0.f)
, mPhase(0)
, mAttackIndex(0)
, mAttackType(8)
, mAttackDelay(0.f)
, mLightChain(kInvalidUniqueId)
, mSpitProjectile(data.spitProjectile, LdrToDamageInfo(data.spitDamage))
, mSpitNode(kInvalidUniqueId)
, mBarrageCount(1)
, mNodeSpacing(20.f)
, mSplashGen(gpSimplePool->GetObj(SObjectTag('PART', data.splash)))
, mWingDamageGen(gpSimplePool->GetObj(SObjectTag('PART', data.wingDamageEffect)))
, mBlowGen(gpSimplePool->GetObj(SObjectTag('PART', data.blowEffect)))
, mLastHeadSplash(CVector3f::Zero())
, mLastTailSplash(CVector3f::Zero())
, mWaterZ(0.f)
, mWater(kInvalidUniqueId)
, mSwoopDamageInfo(LdrToDamageInfo(data.swoopDamage))
, mSwoopTimer(0.f)
, mLevelTarget(CVector3f::Zero())
, mCollisionActorHP(kCollisionActorHP)
, mEdgeDistance(50.f)
, mFlyersLeft(0)
, mHeadBlend(0.f)
, mGrowthDeadTime(0.f)
, mSwoopCount(0)
, mCircleDamage(0.f)
, mGrappleTime(0.f)
, mDashTimer(0.f)
, mInterceptTime(0.2f)
, mDarkChain(kInvalidUniqueId)
, mSwoopTarget(CVector3f::Zero())
, mCircleCount(0)
, mMoveDir(xf.GetColumn(kDY))
, mMoveTarget(CVector3f::Zero())
, mMoveSpeed(0.f)
, mFlashTime(0.f)
, mLightVulnerability(CDamageVulnerability::NormalVulnerabilty())
, mDarkVulnerability(CDamageVulnerability::NormalVulnerabilty())
, mRepeller(data.unknown_0x9347820e.range, data.unknown_0x9347820e.turnRate,
            static_cast< ushort >(data.unknown_0x9347820e.soundEffect),
            data.unknown_0x9347820e.warpScale, data.unknown_0x9347820e.repelOffset)
, mBlowDamageInfo(LdrToDamageInfo(data.blowDamage))
, mWindNode(kInvalidUniqueId)
, mDropFlyerCooldown(0.f)
, mPendingFlinch(0.f)
, mLastTouched(kInvalidUniqueId) {
  if (data.scanInfoLight != kInvalidAssetId) {
    mScanLight = TLockedToken< CScannableObjectInfo >(
        gpSimplePool->GetObj(SObjectTag('SCAN', data.scanInfoLight)));
  }
  if (data.scanInfoDark != kInvalidAssetId) {
    mScanDark = TLockedToken< CScannableObjectInfo >(
        gpSimplePool->GetObj(SObjectTag('SCAN', data.scanInfoDark)));
  }
  if (data.spitVisorEffect != kInvalidAssetId) {
    mSpitVisorGen = TLockedToken< CGenDescription >(
        gpSimplePool->GetObj(SObjectTag('PART', data.spitVisorEffect)));
  }
  KnockBackController().EnableKnockBackPhysics(false);
  KnockBackController().EnableAllAnimReactions(false);
  InitGrowths();
  InitAdditiveAnim();
  for (int i = 0; i < 4; ++i) {
    mGrowthBroken.push_back(false);
  }
}

// @ fn_79_5AE4: the additive reaction (enum 5) UpdateAdditive drives.
void CSwampBossStage2::InitAdditiveAnim() {
  const CPASAnimParmData parms(
      pas::kAS_AdditiveReaction, CPASAnimParm::FromEnum(5), CPASAnimParm::NoParameter(),
      CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
      CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter());
  const rstl::pair< float, int > best =
      GetAnimationData()->GetPASDatabase().FindBestAnimation(parms, -1);
  if (best.first > FLT_EPSILON) {
    mAdditiveAnim = best.second;
  }
}

// fn_79_B8D4
CSwampBossStage2::~CSwampBossStage2() {}

// --------------------------------------------------------------------------------------------
// Helpers

// @ fn_79_3524
const SLdrSwampBossStage2Phase& CSwampBossStage2::CurrentPhase() const {
  if (mPhase == 2) {
    return mData.lightFlyer2;
  }
  if (mPhase < 2) {
    return mPhase == 0 ? mData.lightFlyer1 : mData.darkFlyer1;
  }
  if (mPhase < 4) {
    return mData.darkFlyer2;
  }
  // Retail falls back to the first light phase for any phase past the fourth.
  return mData.lightFlyer1;
}

// @ fn_79_34CC: the attack slot for an index; 8 means "no attack".
int CSwampBossStage2::AttackForIndex(int idx) const {
  const SLdrSwampBossStage2Phase& phase = CurrentPhase();
  if (idx == 2) {
    return phase.thirdAttack;
  }
  if (idx > 1) {
    if (idx == 4) {
      return phase.fifthAttack;
    }
    if (idx > 3) {
      return 8;
    }
    return phase.fourthAttack;
  }
  if (idx == 0) {
    return phase.firstAttack;
  }
  if (idx < 0) {
    return 8;
  }
  return phase.secondAttack;
}

// @ fn_79_3458: take the current slot's attack, wrap to the first slot on the "no attack" sentinel,
// then advance the index. Lurk raises mAttackReady itself once the attack delay runs out.
void CSwampBossStage2::NextAttack() {
  mAttackType = AttackForIndex(mAttackIndex);
  if (mAttackType == 8) {
    mAttackIndex = 0;
    mAttackType = AttackForIndex(mAttackIndex);
  }
  ++mAttackIndex;
}

// @ fn_79_6754: retail resolves the actor through
// CCollisionActorManager::GetCollisionDescFromIndex(idx) then ObjectById/TCastToPtr.
CCollisionActor* CSwampBossStage2::CollisionActor(CStateManager& mgr, int idx) const {
  if (mCollisionMgr.null() || idx >= mCollisionMgr->GetNumCollisionActors()) {
    return nullptr;
  }
  return TCastToPtr< CCollisionActor >(
      mgr.ObjectById(mCollisionMgr->GetCollisionDescFromIndex(idx).GetCollisionActorId()));
}

// @ fn_79_66DC: the collision actor's position. Retail reads CActor::mPosition (+0x54) and
// falls back to the zero vector (0x6724 loads sZeroVector), not to the boss's own position.
// The zero-vector arm is retail's "the collision actor is gone" case: the manager and its ten
// actors are built with the boss (fn_79_6E68) and live as long as it does, and every id in the
// manager's descriptions resolves, so no shipped case reaches it - a run only ever takes the
// arm that copies the actor's position.
CVector3f CSwampBossStage2::CollisionActorPosition(const CStateManager& mgr, int idx) const {
  if (mCollisionMgr.null() || idx >= mCollisionMgr->GetNumCollisionActors()) {
    return CVector3f::Zero();
  }
  const CActor* act = TCastToConstPtr< CActor >(
      mgr.GetObjectById(mCollisionMgr->GetCollisionDescFromIndex(idx).GetCollisionActorId()));
  if (!act) {
    return CVector3f::Zero();
  }
  return act->GetTranslation();
}

// @ fn_79_6054: the centre actor's position, or the origin when the chain has none.
CVector3f CSwampBossStage2::CenterPosition(const CStateManager& mgr) const {
  const CActor* center = TCastToConstPtr< CActor >(mgr.GetObjectById(mCenter));
  return center ? center->GetTranslation() : CVector3f::Zero();
}

// @ fn_79_60A0: the point the states steer at. fn_79_2E7C is its mSwoopTarget branch.
// @ fn_79_2E7C: the mSwoopTarget accessor (`addi r3,r3,0xdf4`), inlined as the head-mode return.
CVector3f CSwampBossStage2::TargetPoint(const CStateManager& mgr) const {
  if (mHeadMode) { // 0x60B4: xc74 & 0x80
    return mSwoopTarget;
  }
  const CActor* node = TCastToConstPtr< CActor >(mgr.GetObjectById(mSwoopNode));
  return node ? node->GetTranslation() : GetTranslation();
}

// @ fn_79_6108: remember a move target and raise the move request.
void CSwampBossStage2::RequestMove(const CVector3f& point) {
  mMoveTarget = point;
  mMoveRequested = true;
}

// @ fn_79_238C: the first chain node on the far side of the centre actor from the current target;
// the last node is taken unconditionally. Returns null for an empty or unresolved chain.
CActor* CSwampBossStage2::NextChainNode(CStateManager& mgr) const {
  const int count = static_cast< int >(mPathNodes.size());
  for (int i = 0; i < count; ++i) {
    CActor* node = TCastToPtr< CActor >(mgr.ObjectById(mPathNodes[i]));
    if (!node) {
      continue;
    }
    if (i == count - 1) {
      return node;
    }
    const CVector3f center = CenterPosition(mgr);
    if (CVector3f::Dot(node->GetTranslation() - center, TargetPoint(mgr) - center) < 0.f) {
      return node;
    }
  }
  return nullptr;
}

// @ fn_79_20D8: joints 4..9 follow the phase's vulnerability table while the body can be hurt, and
// reflect damage otherwise.
void CSwampBossStage2::SetBodyVulnerable(CStateManager& mgr, bool vulnerable) {
  const CDamageVulnerability& vuln =
      !vulnerable ? CDamageVulnerability::ReflectVulnerabilty()
                  : ((mPhase & 1) != 0 ? mDarkVulnerability : mLightVulnerability);
  for (int i = kTailActor; i < ARRAY_SIZE(kSphereJoints); ++i) {
    CCollisionActor* act = CollisionActor(mgr, i);
    if (act) {
      act->SetDamageVulnerability(vuln);
    }
  }
}

CVector3f CSwampBossStage2::PlayerAimPoint(const CStateManager& mgr) const {
  return mgr.GetPlayer(0)->GetAimPosition(mgr, 0.f);
}

// @ fn_79_4220: the chain node nearest the player (retail returns the actor, used as its uid).
TUniqueId CSwampBossStage2::NearestNodeToPlayer(CStateManager& mgr) const {
  const CVector3f playerPos = mgr.GetPlayer(0)->GetTranslation();
  TUniqueId best = kInvalidUniqueId;
  float bestDist = FLT_MAX;
  for (int i = 0; i < mPathNodes.size(); ++i) {
    const CActor* node = TCastToConstPtr< CActor >(mgr.GetObjectById(mPathNodes[i]));
    if (!node) {
      continue;
    }
    const float dist = (node->GetTranslation() - playerPos).MagSquared();
    if (dist < bestDist) {
      bestDist = dist;
      best = mPathNodes[i];
    }
  }
  return best;
}

// @ fn_79_67A4
void CSwampBossStage2::InitGrowths() {
  // Retail model slots: 0 = LB, 1 = LF, 2 = RB, 3 = RF.
  const CAssetId models[4] = {mData.wingGrowthLB, mData.wingGrowthLF, mData.wingGrowthRB,
                              mData.wingGrowthRF};
  const CVector3f scale = GetModelData()->GetScale();
  for (int i = 0; i < 4; ++i) {
    mGrowthLocators.push_back(GetAnimationData()->GetLocatorSegId(rstl::string_l(kGrowthLocators[i])));
    if (models[i] != kInvalidAssetId) {
      mGrowthModels.push_back(rstl::optional_object< CModelData >(
          CModelData(CStaticRes(models[i], scale))));
    } else {
      mGrowthModels.push_back(rstl::optional_object< CModelData >());
    }
    mGrowthHealth.push_back(mData.wingGrowthHealth);
    mGrowthFlash.push_back(0.f);
  }
}

// @ fn_79_6E68
void CSwampBossStage2::SetupCollisionManager(CStateManager& mgr) {
  rstl::vector< CJointCollisionDescription > joints;
  joints.reserve(ARRAY_SIZE(kSphereJoints));
  for (int i = 0; i < ARRAY_SIZE(kSphereJoints); ++i) {
    const CSegId seg = GetAnimationData()->GetLocatorSegId(rstl::string_l(kSphereJoints[i].mName));
    joints.push_back(CJointCollisionDescription::SphereCollision(
        seg, CVector3f::Zero(), kSphereJoints[i].mRadius, rstl::string_l(kSphereJoints[i].mName), kJointMass));
  }
  mCollisionMgr = rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), joints, false);
  // fn_79_6B78: light phase uses the patterned vulnerability, dark phase the Ing-possessed one.
  mLightVulnerability = *GetDamageVulnerability();
  mDarkVulnerability = LdrToDamageVulnerability(mIngPossessionData.ingVulnerability);
  SetupCollisionJoints(mgr);
  // The body itself collides with nothing; only its joints do.
  AddMaterial(kMT_Unknown54, kMT_Scannable, mgr);
  SetMaterialFilter(CMaterialFilter(CMaterialList(), CMaterialList(0x00000000FFFFFFFF),
                                    CMaterialFilter::kFT_Never));
}

// @ fn_79_6B78: the per-joint setup: every joint copies the boss's health info, keeps the light
// phase's vulnerability, holds the collision actor HP (500) and excludes the platforms
// (data_418); the four growth joints are also orbitable and seeker targets. It runs when the
// boss activates, so every run that reaches the fight exercises it.
void CSwampBossStage2::SetupCollisionJoints(CStateManager& mgr) {
  for (int i = 0; i < mCollisionMgr->GetNumCollisionActors(); ++i) {
    CCollisionActor* act = CollisionActor(mgr, i);
    if (!act) {
      break;
    }
    act->SetDamageVulnerability(mLightVulnerability);
    *act->HealthInfo() = *HealthInfo();
    act->HealthInfo()->SetHP(mCollisionActorHP);
    CMaterialFilter filter = act->GetMaterialFilter();
    filter.ExcludeList().Add(kMT_Platform);
    act->SetMaterialFilter(filter);
    if (i < 4) {
      act->AddMaterial(kMT_Orbit, kMT_SeekerTarget, mgr);
    }
  }
}

// @ fn_79_25C0
void CSwampBossStage2::Activate(CStateManager& mgr) {
  if (mCollisionMgr.null()) {
    SetupCollisionManager(mgr);
  }
  BodyController()->Activate(mgr, pas::kAS_Invalid);
  mCollisionMgr->SetActive(mgr, true);
  SetGrowthsActive(mgr, false);
  // Retail loads CHealthInfo +0 (initial HP), 0x000026F4 `lfs f31, 0x0(r3)`.
  mgr.SetBossParams(GetUniqueId(), HealthInfo()->GetInitialHP(),
                    gpStringTable->GetStringIndex("BossSwampBossStage2"));
}

// @ fn_79_5BC8
void CSwampBossStage2::SetGrowthTargetEffects(CStateManager& mgr, bool on) {
  for (int i = 0; i < 4; ++i) {
    AnimationData()->SetEffectState(rstl::string_l(kGrowthTargetEffects[i]),
                                    on && mGrowthHealth[i] > 0.f, mgr);
  }
}

// @ fn_79_5C74
void CSwampBossStage2::SetGrowthsActive(CStateManager& mgr, bool active) {
  for (int i = 0; i < 4; ++i) {
    if (!active || mGrowthHealth[i] > 0.f) {
      SetGrowthActive(mgr, i, active);
    }
  }
}

// @ fn_79_5D00: vtable+0x1c is CEntity::SetActive.
void CSwampBossStage2::SetGrowthActive(CStateManager& mgr, int idx, bool active) {
  CCollisionActor* act = CollisionActor(mgr, idx);
  if (act) {
    act->SetActive(active);
  }
}

// @ fn_79_24E0: the growths are only targetable while the player is BEHIND the boss, and the boss
// itself gains or drops its orbit/target/seeker materials with that side. Retail takes the dot of
// (player - boss) with the transform's forward column (0x28/0x38/0x48) and compares it with
// rodata_9C; `srwi r31,r0,31` (0x255C) turns that into the "playerBehind" flag, which goes
// straight to fn_79_5C74: it activates each live growth while the flag is set and deactivates all
// of them when it is clear, and the material removal follows the same flag. Retail calls this
// from Reel (fn_79_7870) only.
// Port limit: the retail reads the player straight out of the state manager (mgr+0x14FC) with no
// null test, but the host's GetPlayer(0) can be null in a frame without a player, so the port
// keeps the guard.
void CSwampBossStage2::UpdatePlayerSideMaterials(CStateManager& mgr) {
  const CPlayer* player = mgr.GetPlayer(0);
  if (!player) {
    return;
  }
  const bool playerBehind = CVector3f::Dot(player->GetTranslation() - GetTranslation(),
                                           GetTransform().GetForward()) < 0.f;
  SetGrowthsActive(mgr, playerBehind);
  if (playerBehind) {
    RemoveMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
  } else {
    AddMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
  }
}

// @ fn_79_5EC4: direction from a random chain node of the pair the boss picks between back towards
// the centre actor. The node farthest along the boss's forward axis is never the one picked.
CVector3f CSwampBossStage2::DirectionFromRandomNode(CStateManager& mgr) const {
  const int count = static_cast< int >(mPathNodes.size());
  if (count == 0) {
    return CVector3f::Zero();
  }
  const CVector3f center = CenterPosition(mgr);
  const CVector3f forward = GetTransform().GetForward();
  int farthest = 0;
  float farthestDot = FLT_MAX;
  for (int i = 0; i < count; ++i) {
    const CActor* node = TCastToConstPtr< CActor >(mgr.GetObjectById(mPathNodes[i]));
    if (!node) {
      continue;
    }
    const float dot = CVector3f::Dot(center - node->GetTranslation(), forward);
    if (dot < farthestDot) {
      farthestDot = dot;
      farthest = i;
    }
  }
  int pick = mgr.Random()->Next() & 1;
  if (pick == farthest) {
    ++pick;
  }
  if (pick >= count) {
    pick = 0;
  }
  const CActor* node = TCastToConstPtr< CActor >(mgr.GetObjectById(mPathNodes[pick]));
  return node ? (center - node->GetTranslation()).AsNormalized() : CVector3f::Zero();
}

// @ fn_79_6134: normalised direction from the body towards the player's aim point.
CVector3f CSwampBossStage2::DirectionToPlayerAim(const CStateManager& mgr) const {
  if (mgr.GetPlayer(0) == nullptr) {
    return GetTransform().GetForward();
  }
  return (PlayerAimPoint(mgr) - GetTranslation()).AsNormalized();
}

// @ fn_79_215C: hover steering. Without a move request the speed decays and the body keeps its
// heading; with one it eases towards hoverSpeed, turns towards the target and moves.
void CSwampBossStage2::UpdateHover(float dt) {
  const CVector3f toTarget = mMoveTarget - GetTranslation();
  const float step = dt * mData.hoverSpeed;
  if (!mMoveRequested) {
    const float eased = mMoveSpeed - step;
    mMoveSpeed = eased > 0.f ? eased : 0.f; // retail NaN arm (A8-nan-8)
    if (mMoveSpeed == 0.f) {
      mMoveDir = GetTransform().GetForward();
    }
  } else {
    // rodata: ease off within an eighth of a unit per unit of distance, capped at hoverSpeed.
    float speedLimit = mData.hoverSpeed;
    const float distance = toTarget.Magnitude();
    if (distance * 0.125f < 1.f) {
      speedLimit = mData.hoverSpeed * (distance * 0.125f);
    }
    if (!(mMoveSpeed > speedLimit)) { // retail NaN arm (A8-nan-8)
      mMoveSpeed += step;
    } else {
      mMoveSpeed -= step;
    }
    mMoveSpeed = rstl::min_val(mMoveSpeed, mData.hoverSpeed);
  }
  mMoveRequested = false;
  if (mMoveSpeed > 0.f && toTarget.IsMagnitudeSafe()) {
    mMoveDir.Normalize();
    // rodata: turn at 360 degrees per second.
    const CRelAngle turn = CRelAngle::FromRadians(static_cast< float >(dt * 360.0 * 0.017453292));
    mMoveDir = CVector3f::Slerp(mMoveDir, toTarget.AsNormalized(), turn);
    MoveToInOneFrameWR(GetTranslation() + mMoveDir * (mMoveSpeed * dt), dt);
  }
}

// @ fn_79_58F0: the additive animation ramps towards mAdditiveRate (capped at 0.85) or decays, with a
// short sine pulse from the flash timer on top; the tail joint's sphere follows the weight.
void CSwampBossStage2::UpdateAdditive(CStateManager& mgr, float dt) {
  bool active = false;
  if (mAdditiveRate <= 0.f) {
    if (mAdditiveRate < 0.f) {
      if (mAdditiveWeight <= 0.f) {
        mAdditiveRate = 0.f;
      } else {
        mAdditiveWeight = rstl::max_val(mAdditiveWeight + dt * mAdditiveRate, 0.f);
        active = true;
      }
    }
  } else if (mAdditiveWeight >= 0.85f) {
    mAdditiveRate = 0.f;
  } else {
    mAdditiveWeight = rstl::min_val(mAdditiveWeight + dt * mAdditiveRate, 0.85f);
    active = true;
  }
  // rodata: the flash is a 0.3 second quarter-sine pulse of 0.15 on top of the weight.
  float flashAmount = 0.f;
  if (mFlashTime > 0.f) {
    const float t = mFlashTime / 0.3f;
    flashAmount = CMath::FastSinR(static_cast< float >(t * t * 360.0 * 0.017453292)) * 0.15f;
    mFlashTime = rstl::max_val(mFlashTime - dt, 0.f);
    active = true;
  }
  CAnimData* animData = AnimationData();
  if (active) {
    animData->AddAdditiveAnimation(mAdditiveAnim, mAdditiveWeight + flashAmount, false, false);
    // rodata: the tail joint grows from radius 5 with twice the additive weight, at model scale.
    CCollisionActor* tail = CollisionActor(mgr, kTailActor);
    if (tail) {
      tail->SetSphereRadius((mAdditiveWeight * 2.f + 5.f) * GetModelData()->GetScale().GetY());
    }
  } else if (mAdditiveWeight == 0.f) {
    animData->DelAdditiveAnimationImmediately(mAdditiveAnim);
  }
}

// @ fn_79_3900: head/tail water splash. `pos` is the splash point and `lastSplash` the last one
// emitted, kept just above the water surface.
void CSwampBossStage2::UpdateWater(CStateManager& mgr, const CVector3f& pos,
                                   CVector3f& lastSplash) {
  const float heightAboveWater = mWaterZ - pos.GetZ();
  const bool hasEmitter = static_cast< bool >(mWaterSfx);
  bool aboveSurface = false;
  if (heightAboveWater > 0.f) {
    if (heightAboveWater < 4.f) {
      // Hovering just over the water: start the splash sounds (the second one loops and is kept).
      if (!hasEmitter && !mDarkFlyerForm) {
        const CVector3f forward = GetTransform().GetForward();
        PlayCustomSound(pos, forward, mData.audioPlaybackParms, false);
        mWaterSfx = PlayCustomSound(pos, forward, mData.audioPlaybackParms_0x2b3c923a, true);
      }
      aboveSurface = true;
    }
    // rodata: splash plane is a centimetre above the surface.
    const float splashZ = mWaterZ + 0.01f;
    const CVector3f delta(pos.GetX() - lastSplash.GetX(), pos.GetY() - lastSplash.GetY(),
                          splashZ - lastSplash.GetZ());
    // rodata: only splash again once the point has moved sqrt(8) from the last one.
    if (8.f < delta.MagSquared()) {
      CTransform4f xf(GetTransform());
      xf.SetTranslation(CVector3f(pos.GetX(), pos.GetY(), splashZ));
      const CAABox bounds(CVector3f(pos.GetX() - 1.f, pos.GetY() - 1.f, splashZ - 1.f),
                          CVector3f(pos.GetX() + 1.f, pos.GetY() + 1.f, splashZ + 1.f));
      rstl::reserved_vector< TUniqueId, 1024 > nearList;
      // rodata data_41C: the include filter the probe gathers nearby objects with.
      const CMaterialFilter nearFilter(CMaterialList((u64(1) << 32) | 0x31u), CMaterialList(),
                                       CMaterialFilter::kFT_Include);
      mgr.BuildNearList(nearList, bounds, nearFilter, this);
      bool blocked = false;
      if (nearList.size() != 0) {
        // rodata data_420: the probe's own material list.
        const CCollidableAABox probe(bounds, CMaterialList(0x3bu));
        blocked = CGameCollision::DetectDynamicCollisionBoolean(CopyCollidableAABox(probe),
                                                               CTransform4f::Identity(), nearList,
                                                               mgr);
      }
      if (!blocked) {
        // rodata: the splash grows with the drop height, up to 1.6.
        float scale = heightAboveWater * 0.2f + 0.8f;
        if (scale > 1.6f) {
          scale = 1.6f;
        }
        CExplosion* splash = rs_new CExplosion(
            mSplashGen, mgr.AllocateUniqueId(),
            CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true),
            rstl::string_l("Chykka head/tail splash"), xf, 0,
            CVector3f(scale, scale, scale), CColor::White(), -1);
        // TODO(uncertain): retail links the splash to the water actor (CExplosion +0x12a =
        // mWater); the host CExplosion exposes no such field.
        mgr.AddObject(splash);
      }
      lastSplash = CVector3f(pos.GetX(), pos.GetY(), splashZ);
    }
  }
  if (hasEmitter && !aboveSurface) {
    // Left the surface: stop the loop and splash out.
    CSfxManager::RemoveEmitter(mWaterSfx);
    mWaterSfx = CSfxHandle::NullHandle();
    if (!mDarkFlyerForm) {
      PlayCustomSound(pos, GetTransform().GetForward(), mData.audioPlaybackParms_0xc05d5c7a,
                      false);
    }
  }
  if (mDarkFlyerForm && mWaterSfx) {
    CSfxManager::RemoveEmitter(mWaterSfx);
    mWaterSfx = CSfxHandle::NullHandle();
  }
}

// @ fn_79_2928: the boss bar is quartered by phase. Phases 0 and 2 show the wing growths' health,
// phases 1 and 3 the ovipositor's; anything else contributes nothing.
void CSwampBossStage2::UpdateHealthFraction(CStateManager& mgr) {
  const float initialHealth = GetHealthInfo()->GetInitialHP();
  float fraction = 0.f;
  if (mPhase == 0 || mPhase == 2) {
    float growthHealth = 0.f;
    for (int i = 0; i < mGrowthHealth.size(); ++i) {
      if (mGrowthHealth[i] > 0.f) {
        growthHealth += mGrowthHealth[i];
      }
    }
    fraction = growthHealth / (mData.wingGrowthHealth * 4.f);
  } else if (mPhase == 1 || mPhase == 3) {
    fraction = mOvipositorHealth / CurrentPhase().health;
  }
  HealthInfo()->SetHP((static_cast< float >(3 - mPhase) + fraction) * 0.25f * initialHealth);
}

// The port's flinch helper: retail's inline clamp in the collision-damage body (0x65BC..0x65F8,
// mPendingFlinch at 0xf10 = max(0xf10, min(rodata_8C = 1, damage / rodata_84 = 20))), fed to
// Think's CBCAdditiveFlinchCmd. The module's separate additive-animation player is dead code
// and has no host body: see the file header.
void CSwampBossStage2::AddAdditiveFlinch(float amount) {
  mPendingFlinch = rstl::max_val(mPendingFlinch, rstl::min_val(amount / 20.f, 1.f));
}

// @ fn_79_1E40: (re)create the bubble telegraph generator and put it at the water surface. The
// position keeps the caller's X/Y and takes the water height as Z (0x1FC8); the generator's
// *global* translation is set (vtable +0x20 = CParticleGen::SetGlobalTranslation), and the old
// generator is deleted (vtable +0x08 = the deleting destructor, 0x1F40).
// A run that reached the dark phase (the anim event 0x28) took the body; the asset guard is the
// "this area has no bubble effect" case, which the shipped area never takes.
void CSwampBossStage2::StartBubbleTelegraph(CStateManager& mgr, const CVector3f& pos) {
  if (mData.bubbleTelegraphEffect == kInvalidAssetId) {
    return;
  }
  mBubbleGen = rs_new CElementGen(
      gpSimplePool->GetObj(SObjectTag('PART', mData.bubbleTelegraphEffect)),
      CElementGen::kMOT_Normal, CElementGen::kOSF_One);
  mBubbleGen->SetParticleEmission(true);
  mBubbleGen->SetGlobalTranslation(CVector3f(pos.GetX(), pos.GetY(), mWaterZ));
}

// @ fn_79_2A84: the splash shockwave, on the locator's X/Y at the centre actor's height. The
// CShockWaveInfo is the phase's splashShockWave loader struct (fn_79_2C20 is that conversion).
// The transform keeps the boss's rotation and takes the new translation (0x2AC0, 0x2AF0).
// A run that reached the dark phase (the anim event 0x23) took the body; the asset guard is the
// "this area has no shock wave" case, which the shipped area never takes.
void CSwampBossStage2::SpawnShockWave(CStateManager& mgr, const CVector3f& pos) {
  if (mData.splashShockWave.shockWaveEffect == kInvalidAssetId) {
    return;
  }
  CTransform4f xf(GetTransform());
  xf.SetTranslation(CVector3f(pos.GetX(), pos.GetY(), CenterPosition(mgr).GetZ()));
  CShockWave* wave = rs_new CShockWave(
      mgr.AllocateUniqueId(), rstl::string_l("SwampBossStage2 ShockWave"),
      CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true), xf, GetUniqueId(),
      CShockWaveInfo(mData.splashShockWave), mData.splashShockWaveMaxTime,
      mData.unknown_0xe57ca27c);
  mgr.AddObject(wave);
}

// @ fn_79_3578: the wind effect on the wind node. The direction is flat (the Z component is the
// constant 0 of rodata_9C, 0x35E8) and the effect sits 50 units short of the node (0x3614).
// The dark phase's Blow state calls it four times in a run; the wind node is there, so the body
// runs and the "no wind node" arm is the one the shipped area never takes.
void CSwampBossStage2::SpawnWindEffect(CStateManager& mgr) {
  const CActor* node = TCastToConstPtr< CActor >(mgr.GetObjectById(mWindNode));
  if (!node) {
    return;
  }
  CTransform4f xf(GetTransform());
  const CVector3f nodePos = node->GetTranslation();
  const CVector3f pos = GetTranslation();
  // Flat direction to the wind node; the effect sits 50 units short of it.
  const CVector3f dir =
      CVector3f(nodePos.GetX() - pos.GetX(), nodePos.GetY() - pos.GetY(), 0.f).AsNormalized();
  xf.SetTranslation(CVector3f(nodePos.GetX() - dir.GetX() * 50.f,
                              nodePos.GetY() - dir.GetY() * 50.f,
                              nodePos.GetZ() - dir.GetZ() * 50.f));
  // Scale 3, white, and -1 for "no player owner".
  CExplosion* explosion = rs_new CExplosion(
      mBlowGen, mgr.AllocateUniqueId(),
      CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true),
      rstl::string_l("Chykka wind effect"), xf, 0, CVector3f(3.f, 3.f, 3.f), CColor::White(), -1);
  mgr.AddObject(explosion);
}

// @ fn_79_3774: the wing damage burst, on the growth's own locator (rodata_50) at model scale.
// The burst runs when a growth dies, which a beam hit on a growth did four times in a run.
// The index guard is a port limit (the host indexes a C array; the retail trusts the growth
// slot) and never fires.
void CSwampBossStage2::SpawnWingDamageEffect(CStateManager& mgr, int idx) {
  if (idx < 0 || idx >= ARRAY_SIZE(kGrowthLocators)) {
    return;
  }
  const CTransform4f xf = GetLctrTransform(rstl::string_l(kGrowthLocators[idx]));
  const float scale = GetModelData()->GetScale().GetX();
  CExplosion* effect = rs_new CExplosion(
      mWingDamageGen, mgr.AllocateUniqueId(),
      CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true),
      rstl::string_l("Chykka wing damage effect"), xf, 0, CVector3f(scale, scale, scale),
      CColor::White(), -1);
  mgr.AddObject(effect);
}

// @ fn_79_3F30: the water height from the centre actor, the node spacing from the first chain node,
// the edge radius from the edge marker, and the surface height from the water trigger's bounds.
void CSwampBossStage2::ReadWaterAndNodes(CStateManager& mgr) {
  const CActor* centre = TCastToConstPtr< CActor >(mgr.GetObjectById(mCenter));
  if (centre) {
    const CVector3f centrePos = centre->GetTranslation();
    mWaterZ = centrePos.GetZ();
    if (!mPathNodes.empty()) {
      const CActor* first = TCastToConstPtr< CActor >(mgr.GetObjectById(mPathNodes[0]));
      if (first) {
        mNodeSpacing = (first->GetTranslation() - centrePos).Magnitude();
      }
    }
    const CActor* marker = TCastToConstPtr< CActor >(mgr.GetObjectById(mEdgeMarker));
    if (marker) {
      mEdgeDistance = (marker->GetTranslation() - centrePos).Magnitude();
    }
  }
  if (const CScriptWater* water = TCastToConstPtr< CScriptWater >(mgr.GetObjectById(mWater))) {
    mWaterZ = water->GetTriggerBoundsWR().GetMaxPoint().GetZ();
  }
}

// @ fn_79_2E84: pick the chain node to swoop at. The dark phase's DarkLurk calls it every frame
// it steers: a run took both the dive arm (attack 5) and the plain arm, and the "too close to
// the node nearest the player" skip (0x30AC) as well.
// Retail crosses the player-to-centre line with up
// (fn_79_3314, the host's CVector3f::Cross) to walk the chain round to the far side, then keeps the
// connected node that is farthest from the boss with a random spread (0x30F0: the distance is
// measured from the boss, not from the player). While diving, a candidate closer to the node
// nearest the player than the threshold is skipped (0x30AC, threshold 0x924 = the loader's
// unknown_0x9e116385).
void CSwampBossStage2::PickSwoopTarget(CStateManager& mgr) {
  const CPlayer* player = mgr.GetPlayer(0);
  if (!player) {
    return;
  }
  const CActor* nearest = TCastToConstPtr< CActor >(mgr.GetObjectById(NearestNodeToPlayer(mgr)));
  CActor* origin = nullptr;
  if (mAttackType == 5) {
    const CVector3f centre = CenterPosition(mgr);
    const CVector3f across =
        CVector3f::Cross(player->GetTranslation() - centre, CVector3f::Up());
    const float bossSide = CVector3f::Dot(across, GetTranslation() - centre);
    for (int i = 0; nearest && i < mPathNodes.size(); ++i) {
      // Skip the node the player is nearest to; take the first node past the halfway line.
      if (mPathNodes[i] == nearest->GetUniqueId()) {
        continue;
      }
      CActor* node = TCastToPtr< CActor >(mgr.ObjectById(mPathNodes[i]));
      if (!node) {
        continue;
      }
      origin = node;
      if (bossSide * CVector3f::Dot(across, node->GetTranslation() - centre) < 0.f) {
        break;
      }
    }
  } else {
    origin = TCastToPtr< CActor >(mgr.ObjectById(NearestNodeToPlayer(mgr)));
  }
  if (!origin) {
    return;
  }
  // The chain continues along the chosen node's ARRV/NEXT connections.
  const rstl::vector< TUniqueId > candidates =
      origin->FindConnectedObjects(mgr, kSS_Arrived, kSM_Next);
  float bestScore = 0.f;
  for (int i = 0; i < candidates.size(); ++i) {
    const CActor* node = TCastToConstPtr< CActor >(mgr.GetObjectById(candidates[i]));
    if (!node) {
      continue;
    }
    // rodata: while diving, nodes too close to the node nearest the player are not worth
    // swooping at (0x30AC).
    if (mAttackType == 5 && nearest &&
        (node->GetTranslation() - nearest->GetTranslation()).Magnitude() <
            mData.unknown_0x9e116385) {
      continue;
    }
    const float score = (GetTranslation() - node->GetTranslation()).Magnitude() +
                        mNodeSpacing * 0.5f * mgr.Random()->Float();
    if (score > bestScore) {
      mSwoopTarget = node->GetTranslation();
      bestScore = score;
    }
  }
}

// @ fn_79_4090: pick the spit node. Attack 4 (swoop) takes the follower of the node nearest the
// player; everything else takes the best-scoring entry of the swoop-start list.
void CSwampBossStage2::PickSpitNode(CStateManager& mgr) {
  if (mAttackType == 4) {
    const CActor* nearest = TCastToConstPtr< CActor >(mgr.GetObjectById(NearestNodeToPlayer(mgr)));
    if (nearest) {
      // 0xc48 <- the node's FOLW connection.
      mSwoopNode = nearest->FindConnectedObject(mgr, kSS_Arrived, kSM_Follow);
    }
  } else {
    const CPlayer* player = mgr.GetPlayer(0);
    if (!player) {
      return;
    }
    const CVector3f playerPos = player->GetTranslation();
    float bestScore = 0.f;
    for (int i = 0; i < mSwoopStarts.size(); ++i) {
      const CActor* node = TCastToConstPtr< CActor >(mgr.GetObjectById(mSwoopStarts[i]));
      if (!node) {
        continue;
      }
      const float score = (playerPos - node->GetTranslation()).Magnitude() +
                          mNodeSpacing * 0.5f * mgr.Random()->Float();
      if (score > bestScore) {
        mSwoopNode = mSwoopStarts[i];
        bestScore = score;
      }
    }
  }
}

// @ fn_79_2DE8: a point half the player's bounding-box height above the player.
CVector3f CSwampBossStage2::PlayerOverheadPosition(const CStateManager& mgr) const {
  const CPlayer* player = mgr.GetPlayer(0);
  if (!player) {
    return GetTranslation();
  }
  const CAABox& bounds = player->GetBaseBoundingBox();
  const float halfHeight = (bounds.GetMaxPoint().GetZ() - bounds.GetMinPoint().GetZ()) * 0.5f;
  return player->GetTranslation() + CVector3f::Up() * halfHeight;
}

// @ fn_79_4C1C: the spit origin: the current spit node, or above the player when there is none.
CVector3f CSwampBossStage2::SpitOrigin(const CStateManager& mgr) const {
  const CActor* node = TCastToConstPtr< CActor >(mgr.GetObjectById(mSpitNode));
  return node ? node->GetTranslation() : PlayerOverheadPosition(mgr);
}

// @ fn_79_4CA8: the spit node walks to its ARRV/NEXT follower.
void CSwampBossStage2::AdvanceSpitNode(CStateManager& mgr) {
  const CActor* node = TCastToConstPtr< CActor >(mgr.GetObjectById(mSpitNode));
  if (node) {
    // 0xcf0 <- the node's NEXT connection.
    mSpitNode = node->FindConnectedObject(mgr, kSS_Arrived, kSM_Next);
  }
}

// @ fn_79_4324: fire one spit projectile from the event's locator. Attack 3 (barrage) predicts the
// player's intercept and scatters it; otherwise it fires at the current spit node and advances the
// chain. fn_79_4670/fn_79_4A1C/fn_79_4BC8 are the impact visor effect's ctors and dtor here.
void CSwampBossStage2::FireSpit(CStateManager& mgr, const rstl::string& locator) {
  const CTransform4f lctrXf = GetLctrTransform(locator);
  CPlayer* player = mgr.GetPlayer(0);
  if (!player) {
    return;
  }
  CVector3f target = player->GetTranslation();
  if (mAttackType == 3) {
    target = mSpitProjectile.PredictInterceptPos(lctrXf.GetTranslation(),
                                                 PlayerOverheadPosition(mgr), *player, true,
                                                 mInterceptTime);
    const CActor* centre = TCastToConstPtr< CActor >(mgr.GetObjectById(mCenter));
    if (centre && target.GetZ() < centre->GetTranslation().GetZ() + 1.f) {
      target.SetZ(centre->GetTranslation().GetZ() + 1.f);
    }
    // rodata: 80% of barrage shots scatter up to +-10 units along both body axes.
    if (mgr.Random()->Float() < 0.8f) {
      target += GetTransform().GetForward() * ((mgr.Random()->Float() - 0.5f) * 20.f);
      target += GetTransform().GetRight() * ((mgr.Random()->Float() - 0.5f) * 20.f);
    }
    ++mBarrageCount;
  } else {
    target = SpitOrigin(mgr);
    AdvanceSpitNode(mgr);
  }
  const CTransform4f xf = CTransform4f::LookAt(lctrXf.GetTranslation(), target, CVector3f::Up());
  // 0xea0 selects between the spit visor particle and an empty impact visor effect.
  const CImpactVisorEffect visorEffect =
      mSpitVisorGen ? CImpactVisorEffect::ParticleEffect(
                          rstl::optional_object< TLockedToken< CGenDescription > >(mSpitVisorGen),
                          static_cast< TSfxId >(mData.sound_SpitVisor & 0xffff), false)
                    : CImpactVisorEffect::None();
  CEnergyProjectile* projectile =
      LaunchProjectile(xf, mgr, 0x40, 0x400, true, visorEffect, CVector3f(1.f, 1.f, 1.f));
  // TODO(uncertain): retail writes the phase's spitProjectileRadius (mData.spitProjectileRadius)
  // into the projectile at +0x3d4; the host CEnergyProjectile exposes no such field.
  (void)projectile;
}

// fn_79_5030: put every chain node on its recorded position. The retail walks the *nodes* vector
// and reads the position at the same index (0x505C-0x5080, the loop's bound at 0x5090/0x5094);
// the `if (node)` is the retail's own cast test (0x5074). The bound is the nodes count alone
// (0x5054 lwz r31,0x4(r5); 0x5090 cmpw r27,r31): the only writer, fn_79_50AC, pushes a position
// and a uid together, so the two vectors are always the same length.
// @ fn_79_5030
void CSwampBossStage2::PlaceChainNodes(CStateManager& mgr,
                                       const rstl::vector< TUniqueId >& nodes,
                                       const rstl::vector< CVector3f >& positions) const {
  const int count = static_cast< int >(nodes.size()); // 0x5054
  for (int i = 0; i < count; ++i) {                   // 0x5090 cmpw, 0x5094 blt
    CActor* node = TCastToPtr< CActor >(mgr.ObjectById(nodes[i]));
    if (node) {                                       // 0x5074 cmplwi, 0x5078 beq
      node->SetTranslation(positions[i]);
    }
  }
}

// @ fn_79_527C: swing each node about the chain origin, then pull it back inside the edge radius.
// The arm that pulls a node back inside the edge radius (0x53B0) ran on its own in the chain
// launch (anim event 0x1b, carried by the DropFlyer / Become*Flyer animations; note 7.24).
void CSwampBossStage2::SwingChainNodes(CStateManager& mgr,
                                       const rstl::vector< TUniqueId >& nodes,
                                       const CVector3f& origin,
                                       const CTransform4f& rotation) const {
  const int count = static_cast< int >(nodes.size());
  for (int i = 0; i < count; ++i) {
    CActor* node = TCastToPtr< CActor >(mgr.ObjectById(nodes[i]));
    if (!node) {
      continue;
    }
    node->SetTranslation(origin + rotation * (node->GetTranslation() - origin));
    const CVector3f centre = CenterPosition(mgr);
    const CVector3f nodePos = node->GetTranslation();
    if (mEdgeDistance * mEdgeDistance < (nodePos - centre).MagSquared()) {
      const CVector3f dir = (nodePos - centre).AsNormalized();
      // The node keeps its own height; only the horizontal distance is clamped.
      node->SetTranslation(CVector3f(centre.GetX() + dir.GetX() * mEdgeDistance,
                                     centre.GetY() + dir.GetY() * mEdgeDistance,
                                     nodePos.GetZ()));
    }
  }
}

// @ fn_79_543C: swing one chain segment (the node and its NEXT connections) about the origin, counting
// the depth and the nodes moved. fn_79_31C0/fn_79_32C0 are the vector copy and its free here.
void CSwampBossStage2::PlaceChainSegment(CStateManager& mgr, CActor& chain,
                                         const CVector3f& origin,
                                         const CTransform4f& rotation) {
  ++mChainDepth;
  ++mBarrageCount;
  chain.SetTranslation(origin + rotation * (chain.GetTranslation() - origin));
  const rstl::vector< TUniqueId > next = chain.FindConnectedObjects(mgr, kSS_Arrived, kSM_Next);
  for (int i = 0; i < static_cast< int >(next.size()); ++i) {
    CActor* child = TCastToPtr< CActor >(mgr.ObjectById(next[i]));
    if (child) {
      PlaceChainSegment(mgr, *child, origin, rotation);
    }
  }
  --mChainDepth;
}

// @ fn_79_5830: the chain's nodes: the chain actor's "PTRL/FOLW" node, then the walk. The engine
// delivers the boss's kSM_AreaLoaded at the area load, so the walk runs without any forcing
// (the console's `send <boss> AreaLoaded` only adds a second one).
void CSwampBossStage2::CollectChainNodes(CStateManager& mgr, TUniqueId chain,
                                         rstl::vector< TUniqueId >& nodes,
                                         rstl::vector< CVector3f >& positions) {
  CActor* chainActor = TCastToPtr< CActor >(mgr.ObjectById(chain));
  if (!chainActor) {
    return;
  }
  const TUniqueId nodeId = chainActor->FindConnectedObject(mgr, kSS_Patrol, kSM_Follow);
  CActor* node = TCastToPtr< CActor >(mgr.ObjectById(nodeId));
  if (!node) {
    return;
  }
  WalkChainNodes(mgr, node, nodes, positions);
}

// @ fn_79_50AC: the chain walk's recursion: the node's position and uid go on the lists and the
// walk follows the node's "ARRV/NEXT" connections. It runs with fn_79_5830 above.
void CSwampBossStage2::WalkChainNodes(CStateManager& mgr, CActor* node,
                                      rstl::vector< TUniqueId >& nodes,
                                      rstl::vector< CVector3f >& positions) {
  ++mChainDepth;
  positions.push_back(node->GetTranslation());
  nodes.push_back(node->GetUniqueId());
  const rstl::vector< TUniqueId > connected =
      node->FindConnectedObjects(mgr, kSS_Arrived, kSM_Next);
  for (const TUniqueId& uid : connected) {
    if (uid == node->GetUniqueId()) {
      continue;
    }
    CActor* next = TCastToPtr< CActor >(mgr.ObjectById(uid));
    if (!next) {
      continue;
    }
    WalkChainNodes(mgr, next, nodes, positions);
  }
  --mChainDepth;
}

// @ fn_79_4D20: take a random spit platform, walk its NEXT chain to the first active node, then swing
// that whole segment towards the predicted intercept of the player.
void CSwampBossStage2::AimSpitPlatforms(CStateManager& mgr,
                                        const rstl::vector< TUniqueId >& platforms,
                                        const CVector3f& from) {
  const int count = static_cast< int >(platforms.size());
  if (count < 1) {
    return;
  }
  CPlayer* player = mgr.GetPlayer(0);
  if (!player) {
    return;
  }
  const int random = mgr.Random()->Next();
  mSpitNode = platforms[random - (random / count) * count];
  CActor* first = TCastToPtr< CActor >(mgr.ObjectById(mSpitNode));
  if (!first) {
    return;
  }
  // Walk along the chain while the next entity is inactive; the first active one is the pivot.
  CEntity* current = first;
  while (true) {
    const TUniqueId nextId = current->FindConnectedObject(mgr, kSS_Arrived, kSM_Next);
    CEntity* nextEntity = mgr.ObjectById(nextId);
    if (!nextEntity) {
      return;
    }
    current = nextEntity;
    if (current->GetActive()) {
      break;
    }
  }
  CActor* pivot = TCastToPtr< CActor >(current);
  CActor* centre = TCastToPtr< CActor >(mgr.ObjectById(mCenter));
  if (!pivot || !centre) {
    return;
  }
  const CVector3f pivotPos = pivot->GetTranslation();
  CVector3f target = mSpitProjectile.PredictInterceptPos(
      from, PlayerOverheadPosition(mgr), *player, true, mInterceptTime);
  if (target.GetZ() < centre->GetTranslation().GetZ() + 1.f) {
    target.SetZ(centre->GetTranslation().GetZ() + 1.f);
  }
  // The segment swings by the difference between this actor's heading and the pivot's own heading.
  const float thisHeading = static_cast< float >(
      atan2(GetTransform().GetForward().GetY(), GetTransform().GetForward().GetX()));
  const float pivotHeading = static_cast< float >(
      atan2(pivot->GetTransform().GetForward().GetY(), pivot->GetTransform().GetForward().GetX()));
  // Retail also writes the pivot->target offset into the transform, so the segment swings about the
  // pivot and lands on the target.
  CTransform4f rotation =
      CTransform4f::RotateZ(CRelAngle::FromRadians(thisHeading - pivotHeading));
  rotation.SetTranslation(target - pivotPos);
  mBarrageCount = 0;
  PlaceChainSegment(mgr, *first, pivotPos, rotation);
  pivot->SetTransform(GetTransform());
  pivot->SetTranslation(target);
}

// @ fn_79_55B0: wake the platform chain (the retail's two virtual calls on the chain actor, the
// vtable slot +0x18 = CEntity::AcceptScriptMsg, at 0x5654 and 0x56B0; the second one's message
// id is the "DECR" fourcc at 0x5668-0x5670, the first is the activation),
// drop its nodes on their recorded positions, swing them by the
// heading difference to the chain's follow node and park that node at the event's locator.
void CSwampBossStage2::LaunchPlatformChain(CStateManager& mgr, TUniqueId chain,
                                           rstl::vector< TUniqueId >& nodes,
                                           rstl::vector< CVector3f >& positions,
                                           const CVector3f& from) {
  CActor* chainActor = TCastToPtr< CActor >(mgr.ObjectById(chain));
  if (!chainActor) {
    return;
  }
  chainActor->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), chain, kSM_Activate));
  chainActor->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), chain, kSM_Decrement));
  const TUniqueId patrolId = chainActor->FindConnectedObject(mgr, kSS_Patrol, kSM_Follow);
  CActor* patrol = TCastToPtr< CActor >(mgr.ObjectById(patrolId));
  if (!patrol) {
    return;
  }
  PlaceChainNodes(mgr, nodes, positions);
  const TUniqueId nextId = patrol->FindConnectedObject(mgr, kSS_Arrived, kSM_Next);
  const CActor* next = TCastToConstPtr< CActor >(mgr.GetObjectById(nextId));
  if (!next) {
    return;
  }
  const CVector3f patrolPos = patrol->GetTranslation();
  const CVector3f nextPos = next->GetTranslation();
  const float patrolHeading =
      static_cast< float >(atan2(nextPos.GetY() - patrolPos.GetY(), nextPos.GetX() - patrolPos.GetX()));
  const float thisHeading = static_cast< float >(
      atan2(GetTransform().GetForward().GetY(), GetTransform().GetForward().GetX()));
  // The transform also carries the horizontal patrol->locator offset (retail zeroes Z).
  CTransform4f rotation =
      CTransform4f::RotateZ(CRelAngle::FromRadians(thisHeading - patrolHeading));
  rotation.SetTranslation(
      CVector3f(from.GetX() - patrolPos.GetX(), from.GetY() - patrolPos.GetY(), 0.f));
  SwingChainNodes(mgr, nodes, patrolPos, rotation);
  patrol->SetTranslation(from);
}

// @ fn_79_4F88: send the "DECR" message to the light chain actor. The retail looks the actor up
// by mLightChain (0xca4) with ObjectById (0x4FB4), leaves when it is not there (0x4FBC) and
// otherwise sends the message (the vcall at 0x5014, the vtable slot +0x18 =
// CEntity::AcceptScriptMsg): the sender is the boss itself (its uid at 0x8), the message id is
// mLightChain, the message is the fourcc 0x44454352 (0x4FD4-0x4FD8, kSM_Decrement) and the
// state is -1 (0x4FCC, kSS_InvalidState).
// Nothing in the module calls it - the only reference to 0x4F88 in the retail's .text is its own
// definition - so the body was exercised from a temporary caller (the note's 7.26).
void CSwampBossStage2::SendDecrement(CStateManager& mgr) {
  CEntity* chain = mgr.ObjectById(mLightChain);
  if (chain == nullptr) {
    return;
  }
  chain->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), mLightChain, kSM_Decrement));
}

// @ fn_79_3390: total damage still inside the memory window.
float CSwampBossStage2::RecentDamage() const {
  float sum = 0.f;
  for (rstl::list< SDamageMemory >::const_iterator it = mDamageMemory.begin();
       it != mDamageMemory.end(); ++it) {
    sum += it->mDamage;
  }
  return sum;
}

// @ fn_79_33BC: the damage memory ages by dt; an entry that reaches zero is erased (0x3410).
// A run that hit the boss with the beam filled the memory and the entries then reached zero and
// were erased, so both arms ran.
void CSwampBossStage2::DecayDamageMemory(float dt) {
  rstl::list< SDamageMemory >::iterator it = mDamageMemory.begin();
  while (it != mDamageMemory.end()) {
    it->mTime -= dt;
    if (it->mTime <= 0.f) {
      it = mDamageMemory.erase(it);
    } else {
      ++it;
    }
  }
}

// @ fn_79_661C: append a hit to the damage memory list (fn_79_6644 is the list append itself). The
// window length comes from the phase data.
void CSwampBossStage2::AddDamageMemory(float damage) {
  SDamageMemory entry;
  entry.mTime = mData.unknown_0x13448a4a;
  entry.mDamage = damage;
  mDamageMemory.push_back(entry);
}

// @ fn_79_3354: the player's orbit target is a grapple point. Retail tests the player's uid at
// +0x3b0 (CPlayer::mOrbitTargetId) and returns whether it casts to CScriptGrapplePoint; Think
// calls it every frame to accrue mGrappleTime.
bool CSwampBossStage2::PlayerOnGrapplePoint(const CStateManager& mgr) const {
  const CPlayer* player = mgr.GetPlayer(0);
  if (player == nullptr) {
    return false;
  }
  return TCastToConstPtr< CScriptGrapplePoint >(
             mgr.GetObjectById(player->GetOrbitTargetId())) != nullptr;
}

// @ fn_79_61C4: collision-actor damage. Every joint is held at 500 HP, so the damage of a hit is the
// shortfall from that, and a hit counts once per touching object.
void CSwampBossStage2::ProcessDamage(CStateManager& mgr, TUniqueId colActorId) {
  CCollisionActor* colActor = TCastToPtr< CCollisionActor >(mgr.ObjectById(colActorId));
  if (!colActor) {
    return;
  }
  const TUniqueId lastTouched = colActor->GetLastTouchedObject();
  if (mLastTouched != kInvalidUniqueId && lastTouched == mLastTouched) {
    return;
  }
  mLastTouched = lastTouched;
  int hitIndex = -1;
  if (!mCollisionMgr.null()) {
    const int numActors = mCollisionMgr->GetNumCollisionActors();
    for (int i = 0; i < numActors; ++i) {
      if (mCollisionMgr->GetCollisionDescFromIndex(i).GetCollisionActorId() == colActorId) {
        hitIndex = i;
        break;
      }
    }
  }
  const float damage = mCollisionActorHP - colActor->HealthInfo()->GetHP();
  bool bodyHit = false;
  bool damageTaken = false;
  if (hitIndex < 0 || hitIndex > 3) {
    // Body joints: only the ovipositor (actor 4) counts in head mode, and the body only while the
    // growths are not vulnerable.
    if (mHeadMode) {
      if (hitIndex == kTailActor) {
        bodyHit = true;
        mOvipositorHealth -= damage;
      }
    } else if (!mGrowthsVulnerable) {
      mHeadHealth -= damage;
      if (!mClampHealth) {
        mHeadHealth = rstl::max_val(mHeadHealth, 3.f);
      }
      bodyHit = true;
      if (mCircleCounting) {
        mCircleDamage += damage;
      }
      AddDamageMemory(damage);
    }
  } else {
    // Wing growths: front hits only, and only while they are vulnerable.
    CWeapon* weapon = TCastToPtr< CWeapon >(mgr.ObjectById(lastTouched));
    if (weapon) {
      if (CVector3f::Dot(weapon->GetTransform().GetForward(), GetTransform().GetForward()) > 0.f &&
          mGrowthsVulnerable) {
        bodyHit = true;
        if (mGrowthHealth[hitIndex] > 0.f) {
          damageTaken = true;
          mGrowthHealth[hitIndex] -= damage;
          mGrowthFlash[hitIndex] = 1.f;
          if (mData.breakStunDamage <= damage) {
            mGrowthBroken[hitIndex] = true;
            int brokenCount = 0;
            for (int i = 0; i < mGrowthBroken.size(); ++i) {
              brokenCount += mGrowthBroken[i] ? 1 : 0;
            }
            // Two broken wings at once stun the boss outright.
            if (brokenCount > 1) {
              damageTaken = false;
              mGrowthDestroyed = true;
            }
          }
          if (mData.stunnedFlinchSoundDamageThreshold <= damage) {
            mReelEffect = true;
          }
          if (mGrowthHealth[hitIndex] > 0.f) {
            if (mReelEffect) {
              PlayCustomSound(colActor->GetTranslation(), CVector3f::Up(),
                              mData.audioPlaybackParms_0x692fa63c, false);
            }
          } else {
            mGrowthDestroyed = true;
            SpawnWingDamageEffect(mgr, hitIndex);
            SetGrowthActive(mgr, hitIndex, false);
            damageTaken = false;
            PlayCustomSound(colActor->GetTranslation(), CVector3f::Up(),
                            mData.audioPlaybackParms_0xbe3d39aa, false);
          }
        }
      }
    }
  }
  if (bodyHit) {
    mDamageColor = (mHeadMode || mGrowthsVulnerable) ? skDamageColor : skHitsWithoutDamageColor;
    if (!damageTaken) {
      TakeDamage(GetTransform().GetForward(), damage);
    }
    AddAdditiveFlinch(damage);
  }
  colActor->HealthInfo()->SetHP(mCollisionActorHP);
}

// --------------------------------------------------------------------------------------------
// Triggers

// @ fn_79_A498: the animation is over (retail: the animation state id at 0x6b4 == 3, which is
// the host's kAS_Over).
bool CSwampBossStage2::StateOver(CStateManager& mgr, const CTriggerData&) const {
  return mAnimationState.IsOver();
}

// @ fn_79_A438
bool CSwampBossStage2::AllGrowthsDead(CStateManager& mgr, const CTriggerData&) const {
  for (int i = 0; i < mGrowthHealth.size(); ++i) {
    if (mGrowthHealth[i] > 0.f) {
      return false;
    }
  }
  return true;
}

// @ fn_79_A418: the ovipositor's health (0xc50) is zero or less (retail: the compare with
// rodata_9C, the zero float, at 0xA424).
bool CSwampBossStage2::OvipositorDead(CStateManager& mgr, const CTriggerData&) const {
  return mOvipositorHealth <= 0.f;
}

// @ fn_79_A3D8: retail compares the phase (0xc94) with the field at 0x8cc - the loader's
// unknown_0x96ce7897 (the loader struct sits at 0x7c0, so 0x8cc is its 0x10c) - as
// "2 * field <= phase": `slwi r0, r4, 1` doubles the field, then subfc/adde with the sign bits
// (`extrwi` takes the doubled value's sign, `srawi` the phase's) is MWCC's signed `<=`
// (0xA3E0-0xA3F0). The arm that fires was reached with the loader's field forced to 0 (gdb:
// `set var this->mData.unknown_0x96ce7897 = 0` at the trigger's entry), so the compare takes
// its true side.
bool CSwampBossStage2::ZeroHealth(CStateManager& mgr, const CTriggerData&) const {
  return mData.unknown_0x96ce7897 * 2 <= mPhase;
}

// @ fn_79_A3F8
bool CSwampBossStage2::PlayerInWater(CStateManager& mgr, const CTriggerData&) const {
  return mAttackReady && mPlayerInWater;
}

// @ fn_79_A3A0
bool CSwampBossStage2::ShouldSpit(CStateManager& mgr, const CTriggerData&) const {
  return mAttackReady && mAttackType >= 0 && mAttackType <= 2;
}

// @ fn_79_A378
bool CSwampBossStage2::ShouldBarrage(CStateManager& mgr, const CTriggerData&) const {
  return mAttackReady && mAttackType == 3;
}

// @ fn_79_A350
bool CSwampBossStage2::ShouldTaunt(CStateManager& mgr, const CTriggerData&) const {
  return mAttackReady && mAttackType == 7;
}

// @ fn_79_A328
bool CSwampBossStage2::ShouldSwoop(CStateManager& mgr, const CTriggerData&) const {
  return mAttackReady && mAttackType == 4;
}

// @ fn_79_A31C
bool CSwampBossStage2::SwoopOver(CStateManager& mgr, const CTriggerData&) const {
  // Retail reads bit 5 of 0xc76, which is mCircleCounting.
  return mCircleCounting;
}

// @ fn_79_A310
bool CSwampBossStage2::CircleDamage(CStateManager& mgr, const CTriggerData&) const {
  return mCircleDamaged;
}

// @ fn_79_A08C
bool CSwampBossStage2::CircleOver(CStateManager& mgr, const CTriggerData&) const {
  return GetBodyController()->GetCurrentStateId() == pas::kAS_Locomotion || mCircleDone;
}

// @ fn_79_A064: the attack is ready and it is the blow (the attack type 6) - retail: xc75_80
// (the attack ready) at 0xA06C and the type at 0xA078.  The arm that fires was reached with
// the attack ready and the type 6 forced (gdb: `set var this->mAttackReady = 1`,
// `set var this->mAttackType = 6` at the trigger's entry).
bool CSwampBossStage2::ShouldBlow(CStateManager& mgr, const CTriggerData&) const {
  return mAttackReady && mAttackType == 6;
}

// @ fn_79_A13C
bool CSwampBossStage2::IsLevel(CStateManager& mgr, const CTriggerData&) const {
  if (GetBodyController()->GetCurrentStateId() != pas::kAS_Locomotion ||
      !(mMoveSpeed < 0.2f)) { // retail NaN arm (A8-nan-9)
    return false;
  }
  const CVector3f pos = GetTranslation();
  const CVector3f target = TargetPoint(mgr);
  if (!(CMath::AbsF(target.GetZ() - pos.GetZ()) < 1.f) || // retail NaN arm (A8-nan-9)
      !((target - pos).Magnitude() < 2.f)) {
    return false;
  }
  CVector2f toLevel(mLevelTarget.GetX() - pos.GetX(), mLevelTarget.GetY() - pos.GetY());
  toLevel.Normalize();
  const CVector3f fwd = GetTransform().GetForward();
  // rodata: 0x3fa1df46a0000000 (~2 degrees).
  return toLevel.GetX() * fwd.GetX() + toLevel.GetY() * fwd.GetY() >
         CMath::FastCosR(0.034906585f);
}

// @ fn_79_A0C8: the wind node (0xf00) is not the chain node nearest the player - retail: the
// wind node at 0xA0EC and the compare at 0xA11C.  The trigger is in the module's trigger table
// but no state's transition list uses it (the note's 6: the asset's edges), so the function is
// never called: its only arm is dead, and the shipped data scan is the proof.
bool CSwampBossStage2::SwitchedPlatform(CStateManager& mgr, const CTriggerData&) const {
  return mWindNode != NearestNodeToPlayer(mgr);
}

// @ fn_79_A044
bool CSwampBossStage2::Stunned(CStateManager& mgr, const CTriggerData&) const {
  return mHeadHealth <= 0.f;
}

// @ fn_79_9FD4
bool CSwampBossStage2::StunTimeOut(CStateManager& mgr, const CTriggerData&) const {
  return const_cast< CSwampBossStage2* >(this)->StateMachineState().GetTime() > mData.stunTime && mGrowthDeadTime == 0.f;
}

// @ fn_79_9FA4
bool CSwampBossStage2::OneGrowthDead(CStateManager& mgr, const CTriggerData&) const {
  return mGrowthDestroyed && mGrowthDeadTime > 0.3f;
}

// @ fn_79_9F7C: the attack is ready and it is the flyer drop (the attack type 5) - retail:
// xc75_80 (the attack ready) at 0x9F84 and the type at 0x9F90.
bool CSwampBossStage2::ShouldDropFlyer(CStateManager& mgr, const CTriggerData&) const {
  return mAttackReady && mAttackType == 5;
}

// --------------------------------------------------------------------------------------------
// States

// @ fn_79_9F4C
void CSwampBossStage2::Start(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg != kStateMsg_Activate) {
    return;
  }
  // The radar radius override lasts the whole fight; DeathSequence (fn_79_8FAC) clears it.
  mgr.GetPlayer(0)->SetRadarRadiusOverride(mData.radarRange, mData.unknown_0xfe97e835);
}

// @ fn_79_9534
void CSwampBossStage2::BecomeLightFlyer(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_KnockBack)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCKnockBackCmd(CVector3f::Forward(), pas::kS_Two, -1));
    }
  } else if (msg == kStateMsg_Activate) {
    // A new light flyer: wings back at full health, next phase in, at rest.
    for (int i = 0; i < mGrowthHealth.size(); ++i) {
      mGrowthHealth[i] = mData.wingGrowthHealth;
    }
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    ++mPhase;
    mAttackIndex = 0;
    BodyController()->SetLocomotionType(pas::kLT_Relaxed);
    mFlyersLeft = 0x14;
    mLightFlyer = true;
    if (mPhase == 4) {
      SendScriptMsgs(kSS_Dead, mgr, kInvalidUniqueId, kSM_None);
    } else {
      SendScriptMsgs(kSS_InternalState13, mgr, kInvalidUniqueId, kSM_None);
    }
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    mHeadHealth = CurrentPhase().health;
    mLightFlyer = false;
  }
}

// @ fn_79_91A4
void CSwampBossStage2::BecomeDarkFlyer(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_KnockBack)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCKnockBackCmd(CVector3f::Forward(), pas::kS_One, -1));
    }
    if (mSurfaced) {
      if (!mFacePlayer) {
        // rodata: face the player plus or minus 70 degrees, by mRotateSign.
        BodyController()->FaceDirection(
            CTransform4f::RotateZ(CRelAngle::FromDegrees(mRotateSign ? -70.f : 70.f)) *
                DirectionToPlayerAim(mgr),
            dt);
      } else {
        BodyController()->FaceDirection(DirectionToPlayerAim(mgr), dt);
      }
    }
    // rodata: per-frame additive jitter of 0.2 either side of zero.
    mAdditiveJitterX = (mgr.Random()->Float() - 0.5f) * 0.2f;
    mAdditiveJitterY = (mgr.Random()->Float() - 0.5f) * 0.2f;
    UpdateWater(mgr, mHeadPos, mLastTailSplash);
  } else if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mSurfaced = false;
    mRotateSign = mgr.Random()->Float() > 0.5f;
    mFacePlayer = false;
    BodyController()->SetLocomotionType(pas::kLT_Crouch);
    ++mPhase;
    mAttackIndex = 0;
    mBubbleTelegraphed = false;
    mOvipositorHealth = CurrentPhase().health;
    mPendingFlinch = 0.f;
    mDarkFlyerForm = true;
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    mDarkFlyerForm = false;
  }
}

// @ fn_79_8FAC
void CSwampBossStage2::DeathSequence(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Fall)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCKnockDownCmd(CVector3f::Forward(), pas::kS_One));
    }
  } else if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    if (CPlayer* player = mgr.GetPlayer(0)) {
      player->ClearRadarRadiusOverride();
    }
    // Clears the boss bar (invalid id, no energy) and tells the script the boss is dead.
    mgr.SetBossParams(kInvalidUniqueId, 0.f, 0);
    SendScriptMsgs(kSS_Dead, mgr, kInvalidUniqueId, kSM_None);
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
  }
}

// @ fn_79_9A3C
void CSwampBossStage2::Lurk(CStateManager& mgr, EStateMsg msg, float dt) {
  const SLdrSwampBossStage2Phase& phase = CurrentPhase();
  if (msg == kStateMsg_Activate) {
    mAttackDelay = RandomRange(mgr, phase.minTimeBetweenAttacks, phase.maxTimeBetweenAttacks);
    mAttackReady = false;
    if (mCircleDamaged) {
      if (mPhase == 0 || mSwoopSelected) {
        mAttackType = 3;
        mAttackReady = true;
      } else {
        mAttackType = 4;
        mSwoopSelected = true;
      }
      mCircleDamaged = false;
    } else if (mPhase >= 1 && mSwoopCount <= 2 && !mHeadMode && mAttackType == 4 &&
               !mSwoopDone && !mSwoopRepeat) {
      mAttackReady = true;
      mSwoopRepeat = true;
    } else if (mgr.Random()->Float() >= phase.tauntChance) {
      NextAttack();
      mSwoopRepeat = false;
      mSwoopSelected = false;
    } else {
      mAttackType = 7;
    }
    mSwoopCount = mAttackType == 4 ? mSwoopCount + 1 : 0;
    mDashing = mgr.Random()->Float() < phase.dashChance;
    mDashTimer = RandomRange(mgr, phase.unknown_0x29e6ead6, phase.unknown_0x1753225e);
    SetBodyVulnerable(mgr, true); // fn_79_20D8 (0x9C70)
    PickSpitNode(mgr);
  } else if (msg == kStateMsg_Update) {
    BodyController()->FaceDirection(DirectionToPlayerAim(mgr), dt);
    if (mDashing) {
      mDashing = false;
      BodyController()->CommandMgr().DeliverCmd(CBCStepCmd(pas::kSD_Up, pas::kStep_Dodge));
    }
    if (GetBodyController()->GetCurrentStateId() == pas::kAS_Locomotion) {
      RequestMove(TargetPoint(mgr));
      mDashTimer -= dt;
      if (mDashTimer <= 0.f) {
        BodyController()->CommandMgr().DeliverCmd(CBCStepCmd(pas::kSD_Up, pas::kStep_Dodge));
        mDashTimer = RandomRange(mgr, phase.unknown_0x29e6ead6, phase.unknown_0x1753225e);
      }
      if (const_cast< CSwampBossStage2* >(this)->StateMachineState().GetTime() > mAttackDelay) {
        mAttackReady = true;
      }
    }
    if (!mHeadMode && mAttackType != 4 && RecentDamage() > mData.unknown_0x7fc50ac2) {
      mAttackType = 4;
    }
  }
}

// @ fn_79_9754
void CSwampBossStage2::DarkLurk(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    if (mAttackType == 5) {
      BodyController()->FaceDirection(CenterPosition(mgr) - GetTranslation(), dt);
    } else {
      BodyController()->FaceDirection(DirectionToPlayerAim(mgr), dt);
    }
    RequestMove(TargetPoint(mgr));
    if (GetBodyController()->GetCurrentStateId() == pas::kAS_Locomotion) {
      mDashTimer -= dt;
      if (mDashTimer <= 0.f) {
        PickSwoopTarget(mgr);
        const SLdrSwampBossStage2Phase& phase = CurrentPhase();
        mDashTimer = RandomRange(mgr, phase.unknown_0x29e6ead6, phase.unknown_0x1753225e);
      }
    }
    if (StateMachineState().GetTime() > mAttackDelay &&
        GetBodyController()->GetCurrentStateId() == pas::kAS_Locomotion &&
        mDropFlyerCooldown <= 0.f) {
      mAttackReady = true;
      if (const CPlayer* player = mgr.GetPlayer(0)) {
        // Close enough while diving: switch the queued attack to the flyer drop.
        if (mAttackType == 3 &&
            (player->GetTranslation() - GetTranslation()).Magnitude() <
                mData.unknown_0x5a844633) { // 0x99FC: guest 0x91c
          mAttackType = 5;
        }
      }
    }
  } else if (msg == kStateMsg_Activate) {
    const SLdrSwampBossStage2Phase& phase = CurrentPhase();
    mAttackDelay = RandomRange(mgr, phase.minTimeBetweenAttacks, phase.maxTimeBetweenAttacks);
    mAttackReady = false;
    if (mgr.Random()->Float() >= phase.tauntChance) {
      NextAttack();
    } else {
      mAttackType = 7;
    }
    mDashTimer = RandomRange(mgr, phase.unknown_0x29e6ead6, phase.unknown_0x1753225e);
    PickSwoopTarget(mgr);
  }
}

// @ fn_79_8D90
void CSwampBossStage2::SpitWater(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_ProjectileAttack)) {
      const CPlayer* player = mgr.GetPlayer(0);
      const CVector3f target = player ? player->GetTranslation() : GetTranslation();
      const pas::ESeverity severity =
          mAttackType == 0 ? pas::kS_Zero : (mAttackType == 1 ? pas::kS_One : pas::kS_Two);
      BodyController()->CommandMgr().DeliverCmd(CBCProjectileAttackCmd(severity, target, false));
    }
    // Retail stores the direction to the player's aim point as the steering target vector.
    BodyController()->CommandMgr().SetTargetVector(DirectionToPlayerAim(mgr));
    RequestMove(TargetPoint(mgr));
  } else if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mSpitNode = kInvalidUniqueId;
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
  }
}

// @ fn_79_8BB4
void CSwampBossStage2::SpitBarrage(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_LoopAttack)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCLoopAttackCmd(mHeadMode ? pas::kLAT_Two : pas::kLAT_Zero, true));
    } else if (mBarrageCount == CurrentPhase().unknown_0x2b0bfd51) {
      BodyController()->CommandMgr().DeliverCmd(kBSC_ExitState);
    }
    BodyController()->CommandMgr().SetTargetVector(DirectionToPlayerAim(mgr));
    RequestMove(TargetPoint(mgr));
  } else if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mBarrageCount = 0;
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
  }
}

// @ fn_79_8A3C
void CSwampBossStage2::Taunt(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Taunt)) {
      BodyController()->CommandMgr().DeliverCmd(CBCTauntCmd(pas::kTT_Two));
    }
    RequestMove(TargetPoint(mgr));
  } else if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
  }
}

// @ fn_79_8958
void CSwampBossStage2::Level(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    const CVector3f toTarget = mLevelTarget - GetTranslation();
    BodyController()->FaceDirection(toTarget, dt);
    RequestMove(TargetPoint(mgr));
  } else if (msg == kStateMsg_Activate) {
    const CActor* node = NextChainNode(mgr);
    if (node) {
      mWindNode = node->GetUniqueId();
      mLevelTarget = node->GetTranslation();
    }
  }
}

// @ fn_79_82B8
void CSwampBossStage2::Swoop(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_MeleeAttack)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCMeleeAttackCmd(mSwoopSide ? pas::kS_Zero : pas::kS_Two));
    }
    // The swoop rides just above the water surface.
    const CVector3f pos = GetTranslation();
    if (pos.GetZ() < mWaterZ + 5.f) {
      SetTranslation(CVector3f(pos.GetX(), pos.GetY(), mWaterZ + 5.f));
    }
    CPlayer* player = mgr.GetPlayer(0);
    if (!player) {
      return;
    }
    const CVector3f rel = player->GetAimPosition(mgr, 0.f) - mHeadPos;
    const CVector3f forward = GetTransform().GetForward();
    const CVector3f right = GetTransform().GetRight();
    const float alongForward = CVector3f::Dot(rel, forward);
    const float alongRight = CVector3f::Dot(rel, right);
    // rodata: the hit window is 30 units ahead and 18 model scales to either side.
    if (mSwoopWindow && !mSwoopDamage && !mSwoopDone && mGrappleTime == 0.f && alongForward > 0.f &&
        alongForward < 30.f &&
        CMath::AbsF(alongRight) < GetModelData()->GetScale().GetX() * 18.f) {
      player->PushSustainedDamage();
      player->DisableControls(mgr, 0xe2, GetUniqueId(), mData.swoopDamageTime,
                              CGameHint::kBHT_None);
      mSwoopTimer = mData.swoopDamageTime;
      mSwoopDamage = true;
    }
    if (mSwoopTimer > 0.f) {
      const CMaterialFilter playerFilter(CMaterialList(kMT_Player), CMaterialList(),
                                         CMaterialFilter::kFT_IncludeExclude);
      if (player->GetMorphballTransitionState() == CPlayer::kMS_Morphed) {
        // Morph Ball takes half the swoop damage.
        CDamageInfo halfSwoop(mSwoopDamageInfo);
        halfSwoop.SetDamage(mSwoopDamageInfo.GetDamage() * 0.5f);
        mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(),
                        CDamageInfo(halfSwoop, dt), playerFilter, CVector3f::Zero());
      } else {
        const CVector3f impulse =
            (forward + right * (alongRight > 0.f ? 0.5f : -0.5f)) *
            (mData.swoopPush * player->GetMass());
        player->ApplyImpulseWR(impulse, CAxisAngle::Identity());
        mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(),
                        CDamageInfo(mSwoopDamageInfo, dt), playerFilter, CVector3f::Zero());
      }
      player->SetMoveState(NPlayer::kMS_ApplyJump, mgr);
      mSwoopTimer -= dt;
      if (mSwoopTimer <= 0.f) {
        player->PopSustainedDamage();
        mSwoopDamage = false;
        mSwoopDone = true;
      }
    }
  } else if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mSwoopWindow = false;
    mSwoopDamage = false;
    mSwoopDone = false;
    mSwoopTimer = 0.f;
    mCircleCounting = false;
    mSwooping = true;
    if (const CPlayer* player = mgr.GetPlayer(0)) {
      mSwoopSide =
          CVector3f::Dot(GetTransform().GetRight(), player->GetTranslation() - GetTranslation()) >
          0.f;
    }
  } else if (msg == kStateMsg_Deactivate) {
    mSwoopWindow = false;
    if (mSwoopDamage) {
      if (CPlayer* player = mgr.GetPlayer(0)) {
        player->PopSustainedDamage();
      }
      mSwoopDamage = false;
    }
  }
}

// @ fn_79_7E24: the Blow state. Its arms all ran: the window's three tests (in front, the
// 18 model scales to the side, the player not grappling), the push, the damage, the wind
// effect and the deactivate that pops the sustained damage while the window is still open
// (forced (fsm) - the window is open for fewer updates than a console round trip).
void CSwampBossStage2::Blow(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_MeleeAttack)) {
      BodyController()->CommandMgr().DeliverCmd(CBCMeleeAttackCmd(pas::kS_One));
    }
    if (mBlowWindowOpen) {
      if (CPlayer* player = mgr.GetPlayer(0)) {
        const CVector3f rel = player->GetAimPosition(mgr, 0.f) - mHeadPos;
        const CVector3f forward = GetTransform().GetForward();
        // rodata: the blow window is in front of the head and 18 model scales wide.
        if (CVector3f::Dot(rel, forward) > 0.f &&
            CMath::AbsF(CVector3f::Dot(rel, GetTransform().GetRight())) <
                GetModelData()->GetScale().GetX() * 18.f &&
            mGrappleTime == 0.f) {
          if (!mBlowing) {
            player->PushSustainedDamage();
            mBlowing = true;
            player->DisableControls(mgr, 0xe2, GetUniqueId(), 1.f, CGameHint::kBHT_None);
          }
          // Retail tests the pending flag again here, so the push is always the blow push.
          // fn_79_7E24 0x80B0 (0x9AC) / 0x80B8 (0x9A8)
          const float push = mBlowWindowOpen ? mData.blowPush : mData.blowTelegraphPush;
          player->ApplyImpulseWR(forward * (push * player->GetMass()), CAxisAngle::Identity());
        }
        if (mBlowing) {
          mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(),
                          CDamageInfo(mBlowDamageInfo, dt),
                          CMaterialFilter(CMaterialList(kMT_Player), CMaterialList(),
                                          CMaterialFilter::kFT_IncludeExclude),
                          CVector3f::Zero());
          player->SetMoveState(NPlayer::kMS_ApplyJump, mgr);
        }
      }
    }
    if (mBlowing && !mBlowWindowOpen) {
      if (CPlayer* player = mgr.GetPlayer(0)) {
        player->PopSustainedDamage();
      }
      mBlowing = false;
    }
    if (mWindEffectPending) {
      SpawnWindEffect(mgr);
      mWindEffectPending = false;
    }
  } else if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mWindEffectPending = false;
    mBlowWindowOpen = false;
    mBlowing = false;
  } else if (msg == kStateMsg_Deactivate) {
    if (mBlowing) {
      if (CPlayer* player = mgr.GetPlayer(0)) {
        player->PopSustainedDamage();
      }
      mBlowing = false;
    }
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
  }
}

// @ fn_79_7BE0
void CSwampBossStage2::Circle(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    // Light phases only, and only while the circle is being counted: enough damage in one circuit
    // ends it.
    const bool lightPhase = mPhase == 0 || mPhase == 2;
    const float threshold = mPhase == 0 ? mData.unknown_0xf55924da : mData.unknown_0x83bc1de7;
    if (lightPhase && threshold < mCircleDamage && mCircleCounting) {
      mCircleDamaged = true;
      mCircleDone = true;
    }
  } else if (msg == kStateMsg_Activate) {
    mClampHealth = false;
    mCircleDone = false;
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    if (mCircleDone) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCGenerateCmd(mSwoopSide ? pas::kGType_Three : pas::kGType_Four, CVector3f::Zero(),
                         false, false));
    }
    mClampHealth = true;
    mCircleCounting = false;
  }
}

// @ fn_79_7870
void CSwampBossStage2::Reel(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_LoopReaction)) {
      BodyController()->CommandMgr().DeliverCmd(CBCLoopHitReactionCmd(pas::kRT_Three));
    }
    BodyController()->FaceDirection(mReelDir, dt);
    RequestMove(CenterPosition(mgr));
    UpdatePlayerSideMaterials(mgr);
    // The same bit also holds the target effects on; they come up a second into the reel.
    if (!mTargetEffectsOn && StateMachineState().GetTime() > 1.f) {
      SetGrowthTargetEffects(mgr, true);
      mTargetEffectsOn = true;
    }
  } else if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mReelDir = DirectionFromRandomNode(mgr);
    SetGrowthsActive(mgr, true);
    SetBodyVulnerable(mgr, false);
    mGrowthDestroyed = false;
    mGrowthsVulnerable = true;
    mPendingFlinch = 0.f;
    BodyController()->SetTurnSpeed(BodyController()->GetTurnSpeed() * 0.125f);
    SendScriptMsgs(kSS_InternalState10, mgr, kInvalidUniqueId, kSM_None);
    mRepeller.SetActive(false);
    mTargetEffectsOn = false;
    for (int i = 0; i < mGrowthBroken.size(); ++i) {
      mGrowthBroken[i] = false;
    }
  } else if (msg == kStateMsg_Deactivate) {
    BodyController()->CommandMgr().DeliverCmd(kBSC_ExitState);
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    mGrowthVulnerableTime = 0.5f;
    mHeadHealth = CurrentPhase().health;
    BodyController()->SetTurnSpeed(BodyController()->GetTurnSpeed() * 8.f);
    mAttackIndex = 0;
    AddMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
    mRepeller.SetActive(true);
    SetGrowthTargetEffects(mgr, false);
  }
}

// @ fn_79_76EC
void CSwampBossStage2::Recover(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Step)) {
      BodyController()->CommandMgr().DeliverCmd(CBCStepCmd(pas::kSD_Forward, pas::kStep_Dodge));
    } else if (mAnimationState.GetState() == CAnimationState::kAS_Over) {
      // Recovered: tell the chain the reaction is over and take damage again.
      SendScriptMsgs(kSS_InternalState11, mgr, kInvalidUniqueId, kSM_None);
      SetBodyVulnerable(mgr, true);
    }
  } else if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
  }
}

// @ fn_79_755C
void CSwampBossStage2::Flinch(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Step)) {
      BodyController()->CommandMgr().DeliverCmd(CBCStepCmd(pas::kSD_Backward, pas::kStep_Dodge));
    }
  } else if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    SetBodyVulnerable(mgr, true);
  }
}

// @ fn_79_7308
void CSwampBossStage2::DropFlyer(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Update) {
    BodyController()->FaceDirection(CenterPosition(mgr) - GetTranslation(), dt);
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_LoopAttack)) {
      BodyController()->CommandMgr().DeliverCmd(CBCLoopAttackCmd(pas::kLAT_One, true));
    } else if (mFlyersLeft < 1) {
      BodyController()->CommandMgr().DeliverCmd(kBSC_ExitState);
    }
  } else if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mFlyersLeft = CurrentPhase().unknown_0x2e7e55f2;
    mDropFlyerCooldown = mData.unknown_0x8fe0bf01;
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
  }
}

// @ fn_79_72FC: the code node resets the attack index (retail: `li r0, 0` at 0x72FC, `stw r0,
// 0xc98(r3)` at 0x7300; the port's mAttackIndex is at 0xc98).
// It IS referenced, by the module's code list: lbl_79_data_3F4 (.data 0x3F4) is
// `{0x0, 0xFFFFFFFF, fn_79_72FC}` and fn_79_D60C installs it (`lis`/`addi r5,
// lbl_79_data_3F4@l` at 0xDAE4-0xDAE8). No state's transition list names the code node, so the
// fight never runs it: the marker rests on **forced (codecall <id> ResetAttackPhase)**, the
// console running it through the port's own code table - 1 hit in
// `3dfd2249,04_swamp_temple_dark` (note's 7.27).
void CSwampBossStage2::ResetAttackPhase(CStateManager& mgr, float dt) { mAttackIndex = 0; }

// --------------------------------------------------------------------------------------------
// State machine

#define SB2_TRIGGER(name)                                                                       \
  { #name, reinterpret_cast< CPatterned::StateMachine::TriggerFunc >(&CSwampBossStage2::name) }
#define SB2_STATE(name)                                                                         \
  { #name, reinterpret_cast< CPatterned::StateMachine::StateFunc >(&CSwampBossStage2::name) }
#define SB2_CODE(name)                                                                          \
  { #name, reinterpret_cast< CPatterned::StateMachine::CodeFunc >(&CSwampBossStage2::name) }

namespace {
CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    SB2_TRIGGER(StateOver),       SB2_TRIGGER(AllGrowthsDead),   SB2_TRIGGER(OvipositorDead),
    SB2_TRIGGER(ZeroHealth),      SB2_TRIGGER(PlayerInWater),    SB2_TRIGGER(ShouldSpit),
    SB2_TRIGGER(ShouldBarrage),   SB2_TRIGGER(ShouldTaunt),      SB2_TRIGGER(ShouldSwoop),
    SB2_TRIGGER(SwoopOver),       SB2_TRIGGER(CircleDamage),     SB2_TRIGGER(CircleOver),
    SB2_TRIGGER(ShouldBlow),      SB2_TRIGGER(IsLevel),          SB2_TRIGGER(SwitchedPlatform),
    SB2_TRIGGER(Stunned),         SB2_TRIGGER(StunTimeOut),      SB2_TRIGGER(OneGrowthDead),
    SB2_TRIGGER(ShouldDropFlyer),
};
CPatterned::StateMachine::SStateFunction skStates[] = {
    SB2_STATE(Start),       SB2_STATE(BecomeLightFlyer), SB2_STATE(BecomeDarkFlyer),
    SB2_STATE(DeathSequence), SB2_STATE(Lurk),           SB2_STATE(DarkLurk),
    SB2_STATE(SpitWater),   SB2_STATE(SpitBarrage),      SB2_STATE(Taunt),
    SB2_STATE(Level),       SB2_STATE(Swoop),            SB2_STATE(Blow),
    SB2_STATE(Circle),      SB2_STATE(Reel),             SB2_STATE(Recover),
    SB2_STATE(Flinch),      SB2_STATE(DropFlyer),
};
CPatterned::StateMachine::SCodeFunction skCodes[] = {
    SB2_CODE(ResetAttackPhase),
};
} // namespace

// @ fn_79_A4B4
void CSwampBossStage2::SetupStateMachine(CStateManager& mgr) {
  StateMachine* stateMachine = mStateMachine.get();
  CPatterned::SetupStateMachine(mgr);
  stateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  stateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  stateMachine->SetCodeFunctions(skCodes, ARRAY_SIZE(skCodes));
}

// --------------------------------------------------------------------------------------------
// Entity / actor overrides

// @ fn_79_AFB4
void CSwampBossStage2::Think(float dt, CStateManager& mgr) {
  CPatterned::Think(dt, mgr);
  if (!GetActive()) {
    return;
  }
  if (!mCollisionMgr.null()) {
    mCollisionMgr->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);
  }
  mHeadPos = CollisionActorPosition(mgr, kHeadActor);
  mTailPos = CollisionActorPosition(mgr, kTailActor);
  mSplashPos = CollisionActorPosition(mgr, kSplashActor);
  UpdateAdditive(mgr, dt);
  UpdateHover(dt);
  if (mGrowthVulnerableTime > 0.f) {
    mGrowthVulnerableTime -= dt;
    if (mGrowthVulnerableTime <= 0.f) {
      mGrowthsVulnerable = false;
      SetGrowthsActive(mgr, false);
    }
  }
  UpdateWater(mgr, mSplashPos, mLastHeadSplash);
  // 0xB100: a player standing on a platform (CPlayer+0x124c) is not in the water.
  if (mPlayerInWater) {
    const CPlayer* player = mgr.GetPlayer(0);
    if (player && player->GetRidingPlatform() != kInvalidUniqueId) {
      mPlayerInWater = false;
    }
  }
  DecayDamageMemory(dt);
  if (mHeadMode) {
    mHeadBlend = rstl::min_val(mHeadBlend + dt, 1.f);
  } else {
    mHeadBlend = rstl::max_val(mHeadBlend - dt, 0.f);
  }
  mGrowthDeadTime = mGrowthDestroyed ? mGrowthDeadTime + dt : 0.f;
  mGrappleTime = PlayerOnGrapplePoint(mgr) ? mGrappleTime + dt : 0.f;
  UpdateHealthFraction(mgr);
  for (int i = 0; i < mGrowthFlash.size(); ++i) {
    mGrowthFlash[i] = rstl::max_val(mGrowthFlash[i] - dt, 0.f);
  }
  mInterceptTime = dt;
  if (!mBubbleGen.null()) {
    mBubbleGen->Update(dt);
  }
  mRepeller.Update(mgr, *this, dt);
  if (mDropFlyerCooldown > 0.f) {
    mDropFlyerCooldown -= dt;
  }
  if (mPendingFlinch > 0.f) {
    BodyController()->CommandMgr().DeliverCmd(CBCAdditiveFlinchCmd(mPendingFlinch));
    mPendingFlinch = 0.f;
  }
}

// @ fn_79_B3CC
void CSwampBossStage2::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  const bool wasActive = GetActive();
  CPatterned::AcceptScriptMsg(mgr, msg);
  switch (msg.GetMessage()) {
  case kSM_AIUpdateDisabled:
    if (!mCollisionMgr.null()) {
      mCollisionMgr->SetPhysicsActive(mgr, false);
    }
    break;
  case kSM_InternalMessage0:
    mPlayerInWater = true;
    break;
  case kSM_Deactivate:
    if (!mCollisionMgr.null()) {
      mCollisionMgr->SetActive(mgr, false);
    }
    break;
  case kSM_Activate:
    if (!wasActive) {
      Activate(mgr);
    }
    break;
  case kSM_AreaLoaded:
    mSwoopStarts = FindConnectedObjects(mgr, kSS_InternalState0, kSM_None);
    mPathNodes = FindConnectedObjects(mgr, kSS_InternalState5, kSM_None);
    mCenter = FindConnectedObject(mgr, kSS_InternalState1, kSM_None);
    mEdgeMarker = FindConnectedObject(mgr, kSS_InternalState6, kSM_None);
    mPlatformsA = FindConnectedObjects(mgr, kSS_InternalState3, kSM_None);
    mPlatformsB = FindConnectedObjects(mgr, kSS_InternalState4, kSM_None);
    mWater = FindConnectedObject(mgr, kSS_InternalState12, kSM_None);
    mLightChain = FindConnectedObject(mgr, kSS_InternalState2, kSM_None);
    // fn_79_5830: each chain walks into its own node and position lists.
    CollectChainNodes(mgr, mLightChain, mChainANodes, mChainAPositions);
    mDarkChain = FindConnectedObject(mgr, kSS_InternalState7, kSM_None);
    CollectChainNodes(mgr, mDarkChain, mChainBNodes, mChainBPositions);
    ReadWaterAndNodes(mgr);
    break;
  case kSM_Delete:
    if (mWaterSfx) {
      CSfxManager::RemoveEmitter(mWaterSfx);
      mWaterSfx = CSfxHandle::NullHandle(); // 0xB6AC stw r0, 0xf08(r29) with r0 = 0
    }
    if (!mCollisionMgr.null()) {
      mCollisionMgr->Destroy(mgr);
    }
    if (!mBubbleGen.null()) {
      mBubbleGen->SetParticleEmission(false);
      mBubbleGen = nullptr;
    }
    break;
  case kSM_Create:
    if (GetActive()) {
      Activate(mgr);
    }
    break;
  case kSM_Damage:
    ProcessDamage(mgr, msg.GetSenderId());
    break;
  default:
    break;
  }
}

// @ fn_79_A838: animation user events, by event id (the kUE_ names are given where the host enum has
// one). Anything not handled here goes to CPatterned::DoUserAnimEvent, as retail does.
void CSwampBossStage2::DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node,
                                       EUserEventType type, float dt) {
  bool handled = true;
  switch (static_cast< int >(type)) {
  case 0x00: // kUE_Projectile: spit
    FireSpit(mgr, node.GetLocatorName());
    break;
  case 0x01: // kUE_EggLay: release the queued flyers from the active chain
    if (mFlyersLeft > 0) {
      // Retail picks 0xdd0 (mDarkChain) while 0xc76 bit 0 (mLightFlyer) is set; the chain names
      // may be swapped, but the selection follows retail.
      const TUniqueId chain = mLightFlyer ? mDarkChain : mLightChain;
      CActor* chainActor = TCastToPtr< CActor >(mgr.ObjectById(chain));
      if (chainActor) {
        for (int i = 0; i < CurrentPhase().unknown_0xef3efec0; ++i) {
          chainActor->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), chain, kSM_InternalMessage0));
          if (--mFlyersLeft < 1) {
            break;
          }
        }
        if (mFlashTime == 0.f) {
          mFlashTime = 0.3f;
        }
      }
    }
    break;
  case 0x07: // kUE_GenerateEnd: break the surface, one bubble telegraph
    mSurfaced = true;
    if (!mBubbleTelegraphed) {
      StartBubbleTelegraph(mgr, GetTranslation());
      mBubbleTelegraphed = true;
    }
    SetTranslation(CVector3f(GetTranslation().GetX(), GetTranslation().GetY(), mWaterZ + 0.01f));
    break;
  case 0x08: // kUE_DamageOn: the swoop damage window opens
    mSwoopWindow = true;
    mClampHealth = false;
    break;
  case 0x09: // kUE_DamageOff
    mSwoopWindow = false;
    break;
  case 0x0e: // kUE_FadeIn: head (ovipositor) mode on
    mHeadMode = true;
    SetIngPossessed(true, 15.f, mgr);
    mAdditiveRate = 0.2f;
    break;
  case 0x0f: // kUE_FadeOut: head mode off
    mHeadMode = false;
    SetIngPossessed(false, 20.f, mgr);
    mAdditiveRate = -0.5f;
    break;
  case 0x12: // kUE_EndAction: face the player and open the head
    mFacePlayer = true;
    SetBodyVulnerable(mgr, true);
    break;
  case 0x16: // kUE_BreakLockOn
    if (CPlayer* player = mgr.GetPlayer(0)) {
      player->SetOrbitRequestForTarget(GetUniqueId(), CPlayer::kOR_ActivateOrbitSource, mgr);
    }
    break;
  case 0x1b: { // kUE_EventStart: launch the platform chain from this locator
    const CVector3f from = GetLctrTransform(node.GetLocatorName()).GetTranslation();
    if (!mLightFlyer) { // same retail selection as kUE_EggLay
      LaunchPlatformChain(mgr, mLightChain, mChainANodes, mChainAPositions, from);
    } else {
      LaunchPlatformChain(mgr, mDarkChain, mChainBNodes, mChainBPositions, from);
    }
    break;
  }
  case 0x21: // kUE_EffectOn: start counting the circle, then test which side of it we are on
    if (!mCircleCounting) {
      mCircleCounting = true;
      mCircleDamage = 0.f;
      mCircleCount = 0;
    } else {
      const CActor* sideNode =
          TCastToConstPtr< CActor >(mgr.GetObjectById(NearestNodeToPlayer(mgr)));
      const CVector3f centre = CenterPosition(mgr);
      if (sideNode &&
          CVector3f::Dot(sideNode->GetTranslation() - centre, GetTranslation() - centre) < 0.f) {
        mCircleDone = true;
      }
      ++mCircleCount;
    }
    break;
  case 0x22: // kUE_EffectOff
    mCircleCounting = false;
    break;
  case 0x23: // drop the target materials and fire the splash shockwave at this locator
    SendScriptMsgs(kSS_InternalState8, mgr, kInvalidUniqueId, kSM_None);
    RemoveMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
    SpawnShockWave(mgr, GetLctrTransform(node.GetLocatorName()).GetTranslation());
    break;
  case 0x24: // and take them back
    SendScriptMsgs(kSS_InternalState9, mgr, kInvalidUniqueId, kSM_None);
    AddMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
    break;
  case 0x25:
    PickSpitNode(mgr);
    AimSpitPlatforms(mgr, mPlatformsA, mHeadPos);
    break;
  case 0x26:
    PickSpitNode(mgr);
    AimSpitPlatforms(mgr, mPlatformsB, mHeadPos);
    break;
  case 0x27:
    mSwooping = false;
    break;
  case 0x28: // bubble telegraph without breaking the surface
    if (!mBubbleTelegraphed) {
      StartBubbleTelegraph(mgr, GetTranslation());
      mBubbleTelegraphed = true;
    }
    break;
  case 0x29:
    mWindEffectPending = true;
    break;
  case 0x2a:
    mBlowWindowOpen = true;
    break;
  case 0x2b:
    mBlowWindowOpen = false;
    break;
  case 0x2d: { // flinch / stun sounds at this locator
    const CVector3f forward = GetTransform().GetForward();
    const CVector3f pos = GetLctrTransform(node.GetLocatorName()).GetTranslation();
    if (!mGrowthsVulnerable) {
      if (mgr.Random()->Float() < mData.flinchSoundChance) {
        PlayCustomSound(pos, forward, mData.flinchSound, false);
      }
    } else if (!mGrowthDestroyed) {
      if (mReelEffect) {
        PlayCustomSound(pos, forward, mData.stunnedFlinchSound, false);
      }
      mReelEffect = false;
    } else {
      PlayCustomSound(pos, forward, mData.stunnedReelSound, false);
    }
    break;
  }
  default:
    handled = false;
    break;
  }
  if (!handled) {
    CPatterned::DoUserAnimEvent(mgr, node, type, dt);
  }
}

// @ fn_79_A540
CVector3f CSwampBossStage2::GetAimPosition(const CStateManager& mgr, float dt) const {
  return mTailPos * mHeadBlend + mHeadPos * (1.f - mHeadBlend);
}

// @ fn_79_2690
CScannableObjectInfo* CSwampBossStage2::GetScannableObjectInfo() const {
  // Retail picks the scan by the phase, not by a flag (0x26A0-0x26F0): the phase 0 and 2 use
  // the light scan (0xe04), the phase 1 and 3 the dark one (0xe08) and the phase 4 and up the
  // base's scan.
  if (mPhase == 2 || mPhase == 0) {
    if (mScanLight) {
      return const_cast< CScannableObjectInfo* >(*mScanLight.data());
    }
  } else if (mPhase >= 4) {
    return CPatterned::GetScannableObjectInfo();
  } else if (mScanDark) {
    return const_cast< CScannableObjectInfo* >(*mScanDark.data());
  }
  return CPatterned::GetScannableObjectInfo();
}

void CSwampBossStage2::TakeDamage(const CVector3f& dir, float amount) {
  CPatterned::TakeDamage(dir, amount);
}

// @ fn_79_A76C
void CSwampBossStage2::PreRender(CStateManager& mgr) { CPatterned::PreRender(mgr); }

// @ fn_79_A59C: queue the body, draw it immediately (or its ing-snatch transition), then draw each
// live wing growth at its locator - flashed towards the damage colour while mGrowthFlash runs - and
// finally the bubble telegraph generator. fn_79_460/4A0/4C0 are the CModelData copy internals, so
// the stored models are used directly.
void CSwampBossStage2::AddToRenderer(const CStateManager& mgr) const {
  // The whole body is skipped while the actor is pre-render clipped (bit 0 of actor+0x150,
  // `clrlwi. r0, r0, 31; bne .L_A748` at 0xA5C8/0xA5CC, which lands on the epilogue).
  if (GetPreRenderClipped()) {
    return;
  }
  CPatterned::AddToRenderer(mgr);
  if (IsBeingSnatched()) {
    RenderIngSnatchingTransition(mgr);
  } else {
    CPhysicsActor::Render(mgr);
  }
  for (int i = 0; i < mGrowthModels.size(); ++i) {
    const rstl::optional_object< CModelData >& model = mGrowthModels[i];
    if (!model || !(mGrowthHealth[i] > 0.f)) { // retail NaN arm (A8-nan-10)
      continue;
    }
    const CTransform4f xf = GetLctrTransform(mGrowthLocators[i]);
    const float flash = mGrowthFlash[i];
    if (!(flash > 0.f)) { // retail NaN arm (A8-nan-10)
      model->Render(mgr, xf, GetActorLights(), GetModelFlags());
    } else {
      // rodata: a two-stage colour lerp from black to the damage colour by the flash amount.
      const CModelFlags flashFlags(CModelFlags::kT_Two,
                                   CColor::Lerp(CColor::Black(), mDamageColor, flash));
      model->Render(mgr, xf, GetActorLights(), flashFlags);
    }
  }
  if (!mBubbleGen.null()) {
    // TODO(uncertain): retail calls IRenderer::AddParticleGen slot +0x44, the three-argument
    // overload (generator, position, bounds), whose arguments the decompiler dropped; the host
    // draws it through the one-argument overload (slot +0x40).
    gpRender->AddParticleGen(*mBubbleGen);
  }
}

// @ fn_79_A78C
void CSwampBossStage2::Render(const CStateManager& mgr) const {
  uint mask = 0;
  uint target = 0;
  // TODO(uncertain): retail only queries the mask when mDrawParticles is set.
  mgr.GetCharacterRenderMaskAndTarget(mask, target);
  RenderSystemsToBeDrawnFirst(mgr, mask, target);
  RenderSystemsToBeDrawnLast(mgr, mask, target);
  mRepeller.Render(mgr, *this);
}

// @ fn_79_5E3C: down, tilted along the body's right axis by 0xc84 as the snatch blend falls.
CVector3f CSwampBossStage2::GetIngSnatchingNormal(float t) const {
  const CTransform4f& xf = GetTransform();
  const float tilt = (1.f - t) * mAdditiveJitterX;
  return (CVector3f::Down() + tilt * CVector3f(xf.Get00(), xf.Get10(), xf.Get20())).AsNormalized();
}

// @ fn_79_5D40: the snatch plane starts (0xc88 + 2) scaled heights from the splash position and
// moves back by the model's bounds height as the blend rises. The retail's body (0x5D40..0x5E38)
// takes the model's bounds (the GetBounds call at 0x5D78), asks for the normal with the virtual
// GetIngSnatchingNormal (the vcall at 0x5D94, the vtable slot +0x138, the module's own
// fn_79_5E3C - the port's call reaches that same body), and adds the splash position (0xab4)
// plus the distance along that normal.
CVector3f CSwampBossStage2::GetIngSnatchingPoint(float t) const {
  const CAABox bounds = GetModelData()->GetBounds();
  const CVector3f normal = GetIngSnatchingNormal(t);
  const float dist = (mAdditiveJitterY + 2.f) * GetModelData()->GetScale().GetZ() -
                     (bounds.GetMaxPoint().GetZ() - bounds.GetMinPoint().GetZ()) * t;
  return mSplashPos + dist * normal;
}

// --------------------------------------------------------------------------------------------
// Loader

// Retail fn_79_134C: SLdrSwampBossStage2Data's constructor, called by the loader at 0x00000864 on
// its stack object. Two of its `li r0,-0x1` runs feed sound ids the generated header stores as 0
// (the guest's "no sound" is -1): the 0x128 (0x000013B0) store's neighbour 0x124 (0x000013AC) is
// the spitVisorEffect CAssetId, already kInvalidAssetId in the header. The other sound is inside
// the block's SLdrUnknownStruct38 at 0x1b8: fn_79_134C calls its constructor fn_79_1714 at
// 0x000013E0, which stores -1 at its 0x8 (0x0000171C) - soundEffect. SLdrSwampBossStage2Data's
// layout is the guest's, and so is SLdrUnknownStruct38's, so the field paths below are the guest
// offsets. The .inc readers overwrite them when the instance's data carries the property.
//   fn_79_134C  li r0,-0x1 0x000013A4; stw r0,0x128(r31) 0x000013B0
//   fn_79_1714  li r0,-0x1 0x00001714; stw r0,0x8(r3)    0x0000171C  (block 0x1b8 + 0x8)
static void x79134C_SoundDefaults(SLdrSwampBossStage2& sldr) {
  sldr.swampBossStage2Properties.sound_SpitVisor = -1; // 0x000013B0 (field 0x128)
  sldr.swampBossStage2Properties.unknown_0x9347820e.soundEffect =
      -1; // 0x0000171C (block 0x1b8, field 0x8)
}

// fn_79_170: the property loop (0x1BC..0x2A4) dispatches the five ids the .inc reads
// (0x255A4580 -> 0x240, 0xB3774750 -> 0x250, 0x7E397FED -> 0x260, 0xE61748ED -> 0x270,
// 0xB31B771D -> 0x280 fn_79_980) and skips the rest with ReadBytes(nullptr, size) (0x298). The
// stack object's ctor fn_79_830 (0x190) runs the four struct ctors and fn_79_134C, then re-sets
// the patterned/actor defaults the generated ctor sets (0x868..0x968; the 2.f at 0x88, the white
// ambient at 0x294 and the visor flags 0xf at 0x2e4 are the shared ctors' own values).
// fn_79_134C/fn_79_1714/fn_79_1AA0 match SLdrSwampBossStage2Data/SLdrUnknownStruct38/
// SLdrSwampBossStage2Phase field by field except the two -1 sounds above. Then LdrToModelData
// (scale editor+0x28, no CMDL, the patterned+0x1d4 anim set, true; 0x2BC); null when the optional
// is empty (0x2E4 lbz 0x144, beq -> 0x33C li r3,0), else rs_new 0xf20 and fn_79_BE3C (0x3D8).
// @ fn_79_170
static CEntity* PortLoadSwampBossStage2(CStateManager& mgr, CInputStream& input,
                                        CEntityInfo& info) {
  SLdrSwampBossStage2 sldrThis;
  x79134C_SoundDefaults(sldrThis); // fn_79_134C's -1 sound ids (the call at 0x00000864)
#include "MetroidPrime/ScriptLoader/SLdrSwampBossStage2.inc"
  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }
  return rs_new CSwampBossStage2(
      mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
      LdrToEntityInfo(info, sldrThis.editorProperties), LdrToTransform4f(sldrThis.editorProperties),
      *modelData, LdrToActorParameters(sldrThis.actorInformation),
      LdrToPatternedInfo(sldrThis.patterned, &sldrThis.ingPossessionData),
      sldrThis.swampBossStage2Properties);
}

static SSwampBossStage2_FuncPtrs REL_loader_SwampBossStage2;

void RELMain() {
  REL_loader_SwampBossStage2.mLoader = PortLoadSwampBossStage2;
  SetSSwampBossStage2_FuncPtrs(&REL_loader_SwampBossStage2);
}

void RELExit() { SetSSwampBossStage2_FuncPtrs(nullptr); }
