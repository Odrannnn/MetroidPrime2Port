// Host body for REL module SwampBossStage1 (module 78): the Chykka Larva fought in the Dark
// Torvus Bog arena. Ported from the retail REL pseudocode for behaviour; see
// docs/module-analysis/SwampBoss.md for the function map, members and constants.

#include "MetroidPrime/Enemies/CSwampBossStage1.hpp"

#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "MetroidPrime/CAxisAngle.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/Player/CMorphBall.hpp"
#include "MetroidPrime/Weapons/CWeapon.hpp"
#include "Collision/CRayCastResult.hpp"
#include "Kyoto/Input/CRumbleVoice.hpp"
#include "Kyoto/RumbleFxTable.hpp"
#include "MetroidPrime/StateMachineCommon.hpp"
#include <float.h>
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CRandom16.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CModelFlags.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/Math/CQuaternion.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Math/CUnitVector3f.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "Kyoto/Text/CStringTable.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CRumbleManager.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Collision/CJointCollisionDescription.hpp"
#include "MetroidPrime/Enemies/CAiKnockBackMgr.hpp"
#include "MetroidPrime/Enemies/CPatternedInfo.hpp"
#include "MetroidPrime/Factories/CScannableObjectInfo.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CHUDBillboardEffect.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAIHint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWater.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CEnergyProjectile.hpp"
#include "MetroidPrime/Weapons/CGameProjectile.hpp"
#include "MetroidPrime/Weapons/CImpactVisorEffect.hpp"
#include "MetroidPrime/Weapons/CShockWave.hpp"

namespace {
// rodata_8: the 16 joint spheres. mRadius is the in-water radius, mBeachRadius the radius while
// beached (fn_78_8868). mType: 0 body, 1 belly weak spot, 2 tongue (fn_78_9A1C: 3 matches all).
// mFlags bit 1 = projectile passthrough while beached, bit 4 = left out of the sort bounds.
struct SJoint {
  const char* mName;
  float mRadius;
  float mBeachRadius;
  int mType;
  uint mFlags;
};
const SJoint kJoints[16] = {
    {"head", 5.f, 5.5f, 0, 0},
    {"tongue_LCTR_SDK", 3.2f, 3.2f, 2, 0},
    {"L_upper_1", 2.f, 2.f, 0, 4},
    {"R_upper_1", 2.f, 2.f, 0, 4},
    {"Spine_2", 5.f, 5.f, 0, 0},
    {"Spine_3", 5.f, 5.f, 0, 2},
    {"Spine_4", 4.1f, 4.1f, 0, 0},
    {"Spine_5", 3.7f, 15.f, 0, 5},
    {"Spine_6", 3.f, 15.f, 0, 5},
    {"Spine_7", 2.5f, 2.5f, 0, 0},
    {"Spine_8", 2.f, 2.f, 0, 0},
    {"Spine_9", 1.8f, 1.8f, 0, 4},
    {"Spine_11", 1.6f, 1.6f, 0, 4},
    {"Spine_13", 1.5f, 1.5f, 0, 4},
    {"Spine_15", 1.5f, 1.5f, 0, 4},
    {"L_middle2_boob_LCTR", 2.5f, 0.1f, 1, 0},
};
const float kJointMass = 10000.f;       // fn_78_8CD8
const float kCollisionActorHP = 10000.f; // fn_78_8954, reset after every hit in fn_78_9B74
const int kAllJoints = 3;

// SetJointsActive flags (fn_78_9744).
enum {
  kJF_AddTarget = 1,
  kJF_RemoveTarget = 2,
  kJF_Vulnerable = 4,
  kJF_Reflect = 8,
  kJF_AddRadar = 0x10,
  kJF_RemoveRadar = 0x20,
};

// @ fn_78_9A1C: either side being kAllJoints matches everything; otherwise the joint's type
// must equal the requested type.
// `cmpwi r4, 0x3` at 0x9A1C tests the joint's own type against kAllJoints.
// `cmpwi r5, 0x3` at 0x9A24 tests the requested type against kAllJoints.
// `subf r0, r4, r5` at 0x9A34 is the difference of the two types.
// `cntlzw r0, r0` at 0x9A38 counts that difference's leading zeros.
// `srwi r3, r0, 5` at 0x9A3C turns the count into the equality flag.
bool JointMatches(int jointType, int type) {
  if (jointType == kAllJoints || type == kAllJoints) {
    return true;
  }
  return jointType == type;
}

CMaterialFilter SolidFilter() {
  return CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList());
}

void StopSfx(CSfxHandle& handle) {
  if (handle) {
    CSfxManager::RemoveEmitter(handle);
    handle.Clear();
  }
}

CElementGen* MakeGen(CAssetId id) {
  if (id == kInvalidAssetId) {
    return nullptr;
  }
  return rs_new CElementGen(gpSimplePool->GetObj(SObjectTag('PART', id)),
                            CElementGen::kMOT_Normal, CElementGen::kOSF_One);
}

rstl::optional_object< TLockedToken< CGenDescription > > MakeGenToken(CAssetId id) {
  if (id == kInvalidAssetId) {
    return rstl::optional_object_null();
  }
  return TLockedToken< CGenDescription >(gpSimplePool->GetObj(SObjectTag('PART', id)));
}

rstl::optional_object< TToken< CGenDescription > > MakeGenTok(CAssetId id) {
  if (id == kInvalidAssetId) {
    return rstl::optional_object_null();
  }
  return TToken< CGenDescription >(gpSimplePool->GetObj(SObjectTag('PART', id)));
}

rstl::optional_object< CModelData > MakeModel(CAssetId id) {
  if (id == kInvalidAssetId) {
    return rstl::optional_object_null();
  }
  return CModelData(CStaticRes(id, CVector3f(1.f, 1.f, 1.f)));
}

float XYDistance(const CVector3f& a, const CVector3f& b) {
  return CVector3f(a.GetX() - b.GetX(), a.GetY() - b.GetY(), 0.f).Magnitude();
}
} // namespace

// @ fn_78_A5B4: the splash locomotion is 11 when the alt-animation flag is set, 10 otherwise.
// `lwz r0, 0xb6c(r3)` at 0xA5B4 loads xb6c.
// `cmpwi r0, 0x0` at 0xA5BC tests it.
// `li r3, 0xb` at 0xA5B8 is the alt-animation locomotion.
// `li r3, 0xa` at 0xA5C4 is the plain one.
int CSwampBossStage1::SplashLocomotion() const { return xb6c_altAnims ? 11 : 10; }

// @ fn_78_A5CC: the rise locomotion is 14 when the alt-animation flag is set, 1 otherwise.
// `lwz r0, 0xb6c(r3)` at 0xA5CC loads xb6c.
// `cmpwi r0, 0x0` at 0xA5D4 tests it.
// `li r3, 0xe` at 0xA5D0 is the alt-animation locomotion.
// `li r3, 0x1` at 0xA5DC is the plain one.
int CSwampBossStage1::RiseLocomotion() const { return xb6c_altAnims ? 14 : 1; }

// @ fn_78_AA50
CSwampBossStage1::CSwampBossStage1(TUniqueId uid, const rstl::string& name,
                                   const CEntityInfo& info, const CTransform4f& xf,
                                   const CModelData& modelData, const CActorParameters& actParms,
                                   const CPatternedInfo& pInfo,
                                   const SLdrSwampBossStage1Data& data)
: CPatterned(static_cast< EPatternedAI >(0x3b), uid, name, kFT_Zero, info, xf, modelData, pInfo,
             static_cast< EMovementType >(1), static_cast< EColliderType >(1),
             static_cast< EBodyType >(3), actParms)
, x7c0_data(data)
, xb10_(-1)
, xb14_state(-1)
, xb18_time(0.f)
, xb1c_clock(0.f)
, xb20_rootPos(CVector3f::Zero())
, xb2c_headPos(CVector3f::Zero())
, xb3c_painAccum(10000.f)
, xb40_sortBounds(CAABox::MakeNullBox())
, xb58_scanned(false)
, xb58_radarPresent(true)
, xb5a_lastHitter(kInvalidUniqueId)
, xb5c_shredders(1)
, xb60_shredderOrigin(CVector3f::Zero())
, xb6c_altAnims(0)
, xb7c_nextBobTime(-1000.f)
, xb84_phase(0)
, xb88_attackIdx(0)
, xb8c_nextAttackTime(-1000.f)
, xb90_shockWave(data.splashShockWave)
, xbc8_jumps(0)
, xbcc_jumpTarget(0)
, xbd0_splashRotDeg(0.f)
, xbd4_telegraphPart(data.preJumpTelegraphEffect)
, xbd8_splashOver(false)
, xbda_shockWave(kInvalidUniqueId)
, xbe0_shockWaveSfxPos(CVector3f::Zero())
, xbec_splashPhase(0)
, xbf0_platformIdx(-1)
, xbf4_platformPick(-1)
, xbf8_platformTime(0.f)
, xbfc_misses(0)
, xc00_tongues(0)
, xc04_beachOver(false)
, xc08_beachPhase(0)
// The tongue block (this+0xc0c..0xcf3) is built by the module's own ctor, called at 0xAD18 with
// the block and the two model ids (data 0x44/0x48 at 0xAD10/0xAD14): the three vectors from
// sZeroVector (0x1A38..0x1A80), 0x34 = rodata_15C 1.f (0x1A84), the two CModelData from
// CStaticRes(id, sOneVector) each copied into an engaged optional (0x1AA8/0x1AB4,
// 0x1AEC/0x1AF8; fn_78_1B54 sets the engaged byte 0x4c at 0x1B6C), the 0xd8 bits 0x80 and 0x40
// cleared (0x1B10 rlwimi 7,24,24 and 0x1B20 rlwimi 6,25,25), 0xe0/0xe4 = 0 (0x1B28/0x1B2C), and
// fn_78_19A4 (ResetTongueState) on the block at 0x1B30, called at the end of the body below.
// Port limit: MakeModel leaves the optional empty for an invalid id, where the retail builds a
// model-less CModelData; the host CModelData would request CMDL -1. Render tests the engaged
// byte either way (0x7B04/0x7B28).
// @ fn_78_19FC
, xc0c_tonguePos(CVector3f::Zero())
, xc18_tongueTarget(CVector3f::Zero())
, xc24_tongueTip(CVector3f::Zero())
, xc30_extend(0.f)
, xc34_tongueState(kTS_Idle)
, xc38_pullTime(0.f)
, xc3c_pullSpeed(0.f)
, xc40_tongueAmp(1.f) // 0x1A84: stfs f0,0x34(r29), f0 = rodata_15C (1.f, 0x1A34)
, xc44_tongueModel(MakeModel(data.tongueParticleModel))
, xc94_tipModel(MakeModel(data.tongueTipModel))
, xce4_orbitMsg(false)
, xce4_playerTouched(false)
, xce8_vomitTimer(0.f)
, xcf4_headWeight(0.f)
, xcf8_headDir(CVector3f::Zero())
, xd04_headLatch(true)
, xd08_headBlend(100.f)
, xd0c_hpSnapshot(-1000.f)
, xd10_bellyAccum(0.f)
, xd14_heavyHit(false)
, xd18_weakVuln(LdrToDamageVulnerability(data.weakSpotVulnerability).MakeIgnoreRadius())
, xd48_goo(MakeGenTok(data.pART))
, xd54_vomitHits(0)
, xd58_savedStepUp(-1.f)
, xd60_ringGen(MakeGen(data.darkWaterRingEffect))
, xd64_water(kInvalidUniqueId)
, xd66_ringOn(false)
, xd68_headOffset(CVector3f::Zero())
, xd74_ringRate(1.f)
, xd78_waitStart(-1000.f)
, xd7c_projectileInfo(data.spitProjectile, LdrToDamageInfo(data.spitDamage))
, xda4_spitVisor(MakeGenToken(data.spitVisorEffect)) {
  // Retail clears these +0x681 bits (0x40/0x10/0x08/0x04/0x02, then 0x80); shock stays enabled.
  CAiKnockBackMgr& kb = KnockBackController();
  kb.EnableFreeze(false);
  kb.EnableBurn(false);
  kb.EnableBurnDeath(false);
  kb.EnableExplodeDeath(false);
  kb.EnableLaggedBurnDeath(false);
  kb.EnableKnockBackPhysics(false);
  kb.EnableSlow(false);
  ResetTongueState(); // 0x1B30: the tongue block ctor's tail, bl fn_78_19A4
}

CSwampBossStage1::~CSwampBossStage1() {
  StopSfx(xb38_telegraphSfx);
  StopSfx(xbdc_shockWaveSfx);
  StopSfx(xcec_retractSfx);
  StopSfx(xcf0_pullSfx);
}

// @ fn_78_5518
const SLdrSwampBossStage1Struct& CSwampBossStage1::CurrentPhase() const {
  if (xb84_phase == 0) {
    return x7c0_data.swampBossStage1Struct;
  }
  if (xb84_phase == 1) {
    return x7c0_data.swampBossStage1Struct_0x3e1e7597;
  }
  return x7c0_data.swampBossStage1Struct_0xa1c4f609;
}

// @ fn_78_549C
int CSwampBossStage1::CurrentAttack() const {
  const SLdrSwampBossStage1Struct& phase = CurrentPhase();
  switch (xb88_attackIdx) {
  case 1:
    return phase.secondAttack;
  case 2:
    return phase.thirdAttack;
  case 3:
    return phase.fourthAttack;
  default:
    return phase.firstAttack;
  }
}

// @ fn_78_568C
void CSwampBossStage1::ScheduleNextAttack(CStateManager& mgr) {
  if (xb88_attackIdx > 3 || CurrentAttack() == 2) {
    xb88_attackIdx = 0;
  }
  while (HealthInfo()->GetHP() < CurrentPhase().unknown_0x98106ee2 && xb84_phase <= 1) {
    ++xb84_phase;
    xb88_attackIdx = 0;
  }
  const SLdrSwampBossStage1Struct& phase = CurrentPhase();
  xb8c_nextAttackTime = xb1c_clock;
  if (mgr.IsRandomAvailable()) {
    xb8c_nextAttackTime +=
        mgr.Random()->Range(phase.minTimeBetweenAttacks, phase.maxTimeBetweenAttacks);
  }
}

// @ fn_78_5560
void CSwampBossStage1::RecordState(int state, EStateMsg msg) {
  if (msg == kStateMsg_Deactivate) {
    // fn_78_55AC: push the leaving state into the three-deep history, dropping the oldest.
    if (xafc_stateHistory.size() == 3) {
      xafc_stateHistory.erase(xafc_stateHistory.begin());
    }
    xafc_stateHistory.push_back(xb14_state);
    xb14_state = -1;
  } else {
    xb14_state = state;
  }
}

// @ fn_78_A528
bool CSwampBossStage1::CanTakeHits() const {
  switch (xb14_state) {
  case 0:
  case 3:
  case 4:
  case 5:
  case 6:
  case 9:
  case 10:
    return true;
  default:
    return false;
  }
}

// --------------------------------------------------------------------------------------------
// Body controller helpers

// @ fn_78_BA9C
void CSwampBossStage1::BodyMelee(CStateManager& mgr, EStateMsg msg, int severity) {
  if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    BodyController()->CommandMgr().DeliverCmd(
        CBCMeleeAttackCmd(static_cast< pas::ESeverity >(severity)));
  } else if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_MeleeAttack)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCMeleeAttackCmd(static_cast< pas::ESeverity >(severity)));
    }
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
  }
}

// @ fn_78_B9D0
void CSwampBossStage1::BodyTaunt(CStateManager& mgr, EStateMsg msg, int taunt) {
  if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    BodyController()->CommandMgr().DeliverCmd(CBCTauntCmd(static_cast< pas::ETauntType >(taunt)));
  } else if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Taunt)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCTauntCmd(static_cast< pas::ETauntType >(taunt)));
    }
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
  }
}

void CSwampBossStage1::SetLocomotion(int type) {
  BodyController()->SetLocomotionType(static_cast< pas::ELocomotionType >(type));
}

// --------------------------------------------------------------------------------------------
// Collision actors

CCollisionActor* CSwampBossStage1::JointActor(CStateManager& mgr, int idx) const {
  if (xb80_collision.null()) {
    return nullptr;
  }
  return TCastToPtr< CCollisionActor >(
      mgr.ObjectById(xb80_collision->GetCollisionDescFromIndex(idx).GetCollisionActorId()));
}

// @ fn_78_8954
// @ fn_78_8CD8
void CSwampBossStage1::CreateCollision(CStateManager& mgr) {
  rstl::vector< CJointCollisionDescription > joints;
  joints.reserve(ARRAY_SIZE(kJoints));
  for (int i = 0; i < ARRAY_SIZE(kJoints); ++i) {
    const CSegId seg = GetAnimationData()->GetLocatorSegId(rstl::string_l(kJoints[i].mName));
    if (seg == CSegId(-1)) {
      continue;
    }
    joints.push_back(CJointCollisionDescription::SphereCollision(
        seg, CVector3f::Zero(), kJoints[i].mRadius, rstl::string_l(kJoints[i].mName), kJointMass));
  }
  xb80_collision =
      rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), joints, GetActive());
  for (int i = 0; i < ARRAY_SIZE(kJoints); ++i) {
    CCollisionActor* act = JointActor(mgr, i);
    if (!act) {
      continue;
    }
    act->HealthInfo()->SetKnockbackResistance(HealthInfo()->GetKnockBackResistance());
    act->HealthInfo()->SetHP(kCollisionActorHP);
    switch (kJoints[i].mType) {
    case 1:
      act->SetDamageVulnerability(
          LdrToDamageVulnerability(x7c0_data.weakSpotVulnerability).MakeIgnoreRadius());
      act->AddMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
      act->SetResponseType(static_cast< EWeaponCollisionResponseTypes >(0x4e));
      break;
    case 2:
      act->AddMaterial(kMT_RadarObject, mgr);
      // fallthrough
    case 0:
      act->SetDamageVulnerability(CDamageVulnerability::ReflectVulnerabilty());
      act->SetResponseType(static_cast< EWeaponCollisionResponseTypes >(0x6c));
      break;
    default:
      break;
    }
    // The exclude list is the second u64 of the filter, so the word at +8 is its high half
    // (big-endian); ORing 2 there excludes kMT_Character (33).
    CMaterialFilter filter = act->GetMaterialFilter();
    filter.ExcludeList().Add(kMT_Character);
    act->SetMaterialFilter(filter);
  }
}

// @ fn_78_9744
void CSwampBossStage1::SetJointsActive(CStateManager& mgr, int type, int flags) {
  if (xb80_collision.null()) {
    return;
  }
  for (int i = 0; i < ARRAY_SIZE(kJoints); ++i) {
    CCollisionActor* act = JointActor(mgr, i);
    if (!act || !JointMatches(kJoints[i].mType, type)) {
      continue;
    }
    if (kJoints[i].mType == 1 && !xb58_scanned) {
      act->RemoveMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
    } else if (flags & kJF_AddTarget) {
      act->AddMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
    } else if (flags & kJF_RemoveTarget) {
      act->RemoveMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
    }
    if (flags & kJF_Vulnerable) {
      if (kJoints[i].mType == 1) {
        act->SetDamageVulnerability(xd18_weakVuln);
        act->SetResponseType(static_cast< EWeaponCollisionResponseTypes >(0x6c));
      } else {
        act->SetDamageVulnerability(GetDamageVulnerability()->MakeIgnoreRadius());
        act->SetResponseType(static_cast< EWeaponCollisionResponseTypes >(0x30));
      }
    } else if (flags & kJF_Reflect) {
      act->SetDamageVulnerability(CDamageVulnerability::ReflectVulnerabilty());
      act->SetResponseType(static_cast< EWeaponCollisionResponseTypes >(0x6c));
    }
    if (flags & kJF_AddRadar) {
      act->AddMaterial(kMT_RadarObject, mgr);
    } else if (flags & kJF_RemoveRadar) {
      act->RemoveMaterial(kMT_RadarObject, mgr);
    }
  }
}

// @ fn_78_8868
void CSwampBossStage1::SetBeachMaterials(CStateManager& mgr, bool beached) {
  if (xb80_collision.null()) {
    return;
  }
  for (int i = 0; i < ARRAY_SIZE(kJoints); ++i) {
    CCollisionActor* act = JointActor(mgr, i);
    if (!act) {
      continue;
    }
    if (beached) {
      act->SetSphereRadius(kJoints[i].mBeachRadius);
      if (kJoints[i].mFlags & 1) {
        act->AddMaterial(kMT_ProjectilePassthrough, mgr);
      }
    } else {
      act->SetSphereRadius(kJoints[i].mRadius);
      if (kJoints[i].mFlags & 1) {
        act->RemoveMaterial(kMT_ProjectilePassthrough, mgr);
      }
    }
  }
}

// @ fn_78_993C
bool CSwampBossStage1::AnyJointOnRadar(CStateManager& mgr) const {
  if (xb80_collision.null() || !GetActive()) {
    return false;
  }
  for (int i = 0; i < ARRAY_SIZE(kJoints); ++i) {
    CCollisionActor* act = JointActor(mgr, i);
    if (act && act->GetMaterialList().HasMaterial(kMT_RadarObject)) {
      return true;
    }
  }
  return false;
}

// @ fn_78_9AD8
bool CSwampBossStage1::IsJointOfType(CStateManager& mgr, const CCollisionActor* act,
                                     int type) const {
  for (int i = 0; i < ARRAY_SIZE(kJoints); ++i) {
    if (JointActor(mgr, i) == act) {
      return JointMatches(kJoints[i].mType, type);
    }
  }
  return false;
}

// --------------------------------------------------------------------------------------------
// World queries

// @ fn_78_8F7C
float CSwampBossStage1::WaterHeight(const CStateManager& mgr) const {
  const CScriptWater* water = TCastToConstPtr< CScriptWater >(mgr.GetObjectById(xd64_water));
  if (!water) {
    return -10000.f;
  }
  return water->GetTriggerBoundsWR().GetMaxPoint().GetZ();
}

// @ fn_78_77C4
CVector3f CSwampBossStage1::TongueTargetPos(const CStateManager& mgr) const {
  const CPlayer* player = mgr.GetPlayer(0);
  CVector3f pos = player->GetAimPosition(mgr, 0.f);
  if (player->GetMorphballTransitionState() != CPlayer::kMS_Morphed) {
    pos.SetZ(pos.GetZ() - 1.5f);
  }
  return pos;
}

// @ fn_78_66E8
float CSwampBossStage1::TongueAngleToPlayer(const CStateManager& mgr) const {
  const CVector3f toPlayer = mgr.GetPlayer(0)->GetTranslation() - xc0c_tonguePos;
  const CVector3f toSelf = GetTranslation() - xc0c_tonguePos;
  CVector3f a(toPlayer.GetX(), toPlayer.GetY(), 0.f);
  CVector3f b(toSelf.GetX(), toSelf.GetY(), 0.f);
  if (!a.CanBeNormalized() || !b.CanBeNormalized()) {
    return 360.f;
  }
  return CMath::Rad2Deg(CVector3f::GetAngleDiff(a.AsNormalized(), b.AsNormalized()));
}

// @ fn_78_1F60
bool CSwampBossStage1::PlayerInsideHint(CStateManager& mgr) const {
  CObjectList& list = mgr.ObjectListById(kOL_AiWaypoint);
  for (int id = list.GetFirstObjectIndex(); id != -1; id = list.GetNextObjectIndex(id)) {
    CScriptAIHint* hint = TCastToPtr< CScriptAIHint >(list[id]);
    if (hint && hint->GetCurrentAreaId() == GetCurrentAreaId() &&
        hint->GetHintType() == static_cast< CScriptAIHint::EHintType >(21)) {
      return XYDistance(mgr.GetPlayer(0)->GetTranslation(), hint->GetTranslation()) <
             hint->GetRadius();
    }
  }
  return true;
}

// --------------------------------------------------------------------------------------------
// Head turn

// @ fn_78_7224
void CSwampBossStage1::HeadWeightDown(float dt) {
  xcf4_headWeight = rstl::max_val(xcf4_headWeight - dt / 0.2f, 0.f);
}

// @ fn_78_7260
void CSwampBossStage1::HeadWeightUp(float dt) {
  xcf4_headWeight = rstl::min_val(xcf4_headWeight + dt / 0.3f, 1.f);
}

// @ fn_78_30C8
CVector3f CSwampBossStage1::UpdateHeadLook(CStateManager& mgr, float dt) {
  const CVector3f target = TongueTargetPos(mgr);
  if (xc34_tongueState == 0) {
    xd04_headLatch = true;
    xd08_headBlend += dt;
    if (!(xd08_headBlend > 1.f)) { // 0x3178 fcmpo/ble: NaN takes the Lerp arm
      return CVector3f::Lerp(xcf8_headDir, target, xd08_headBlend);
    }
    return target;
  }
  xd08_headBlend = 0.f;
  if (xd04_headLatch) {
    xcf8_headDir = target;
    xd04_headLatch = false;
  }
  return xcf8_headDir;
}

// @ fn_78_2B20
void CSwampBossStage1::ApplyHeadAdditive(CStateManager& mgr, float weight, int side) {
  if (!mgr.IsRandomAvailable()) {
    return;
  }
  const CPASAnimParmData parms(
      static_cast< pas::EAnimationState >(0x16), CPASAnimParm::FromEnum(side),
      CPASAnimParm::FromEnum(0), CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
      CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
      CPASAnimParm::NoParameter());
  const rstl::pair< float, int > best =
      GetAnimationData()->GetPASDatabase().FindBestAnimation(parms, *mgr.Random(), -1);
  AnimationData()->AddAdditiveAnimation(best.second, weight, false, false);
}

// @ fn_78_2E38
void CSwampBossStage1::UpdateHeadTurn(CStateManager& mgr, float dt) {
  if (xcf4_headWeight > 0.f) {
    const CVector3f toSelf(GetTranslation().GetX() - xc0c_tonguePos.GetX(),
                           GetTranslation().GetY() - xc0c_tonguePos.GetY(), 0.f);
    if (toSelf.CanBeNormalized()) {
      const CVector3f look = UpdateHeadLook(mgr, dt);
      const CVector3f toLook(look.GetX() - xc0c_tonguePos.GetX(),
                             look.GetY() - xc0c_tonguePos.GetY(), 0.f);
      if (toLook.CanBeNormalized()) {
        const float frac = rstl::min_val(
            CMath::Rad2Deg(CVector3f::GetAngleDiff(toSelf.AsNormalized(), toLook.AsNormalized())) /
                25.f,
            1.f);
        const CVector3f cross = CVector3f::Cross(toSelf, toLook);
        // The mangled signature (EaseInOut(float, EEaseTypes, f, f, f, f, f)) fixes the order:
        // t, ease type 0 (sinusoidal), ease in 0.25, ease out 0.75, 0..1, scale 2.
        const float weight =
            frac * CMath::EaseInOut(xcf4_headWeight, CMath::kET_Sinusoidal, 0.25f, 0.75f, 0.f, 1.f,
                                    2.f);
        ApplyHeadAdditive(mgr, weight, cross.GetZ() <= 0.f ? 1 : 0);
        ApplyHeadAdditive(mgr, 0.f, cross.GetZ() > 0.f ? 1 : 0);
      }
    }
  } else {
    ApplyHeadAdditive(mgr, 0.f, 0);
    ApplyHeadAdditive(mgr, 0.f, 1);
  }
}

// --------------------------------------------------------------------------------------------
// Tongue

// @ fn_78_716C
void CSwampBossStage1::UpdateTongueTip(CStateManager& mgr, float amp, bool ray) {
  if (xc34_tongueState == 0) {
    return;
  }
  xc24_tongueTip =
      CVector3f::Lerp(xc0c_tonguePos, xc18_tongueTarget, rstl::max_val(xc30_extend, 0.01f));
  xc40_tongueAmp = amp;
  if (ray) {
    TongueRayCheck(mgr, xc24_tongueTip);
  }
}

// @ fn_78_729C
void CSwampBossStage1::RetractTongue(CStateManager& mgr, float dt) {
  xc30_extend = rstl::max_val(xc30_extend - dt / 0.25f, 0.f);
  if (xc30_extend >= 0.1f) {
    if (!xcec_retractSfx) {
      CVector3f dir = xc18_tongueTarget - xc0c_tonguePos;
      dir.SetZ(0.f);
      if (dir.CanBeNormalized()) {
        dir.Normalize();
      }
      xcec_retractSfx =
          PlayCustomSound(xc0c_tonguePos, dir, x7c0_data.sounds.tongueRetract_Loop, true);
    }
  } else {
    StopTongue();
  }
}

// @ fn_78_73A4
void CSwampBossStage1::ExtendTongue(CStateManager& mgr, float dt) {
  xc30_extend = rstl::min_val(xc30_extend + dt / 0.4f, 1.f);
  if (xc30_extend < 0.6f) {
    xc18_tongueTarget = TongueTargetPos(mgr);
  }
}

// @ fn_78_7440
void CSwampBossStage1::TongueRayCheck(CStateManager& mgr, const CVector3f& tip) {
  if (xc34_tongueState != 1) {
    return;
  }
  CVector3f dir = tip - xc0c_tonguePos;
  if (!dir.CanBeNormalized()) {
    return;
  }
  const float length = dir.Magnitude() + 5.f;
  dir.Normalize();
  // rodata data_50C: the tongue only collides with the player.
  const CMaterialFilter filter = CMaterialFilter::MakeInclude(CMaterialList(kMT_Player));
  rstl::reserved_vector< TUniqueId, 1024 > nearList;
  mgr.BuildNearList(nearList, xc0c_tonguePos, dir, length, filter, nullptr);
  CVector3f side0(dir.GetY(), -dir.GetX(), 0.f);
  CVector3f side1(-dir.GetY(), dir.GetX(), 0.f);
  if (side0.CanBeNormalized()) {
    side0.Normalize();
  }
  if (side1.CanBeNormalized()) {
    side1.Normalize();
  }
  const CVector3f origins[3] = {xc0c_tonguePos, xc0c_tonguePos + side0 * 0.7f,
                                xc0c_tonguePos + side1 * 0.7f};
  for (int i = 0; i < 3; ++i) {
    TUniqueId hit = kInvalidUniqueId;
    CGameCollision::RayDynamicIntersection(mgr, hit, origins[i], dir, length, filter, nearList);
    if (hit == mgr.GetPlayer(0)->GetUniqueId()) {
      xc34_tongueState = 2;
      PlayCustomSound(xc0c_tonguePos, dir, x7c0_data.sounds.tongueHitPlayer_OneShot, false);
      xcf0_pullSfx =
          PlayCustomSound(xc0c_tonguePos, dir, x7c0_data.sounds.tonguePullPlayer_Loop, true);
      return;
    }
  }
}

// @ fn_78_6B7C
void CSwampBossStage1::StopTongueSounds() {
  StopSfx(xcec_retractSfx);
  StopSfx(xcf0_pullSfx);
}

// @ fn_78_6BEC
void CSwampBossStage1::StopTongue() {
  if (xb14_state != 5 && xb14_state != 9) {
    xc34_tongueState = 0;
    StopTongueSounds();
  }
}

// @ fn_78_6C2C
void CSwampBossStage1::LaunchTongue(CStateManager& mgr) {
  StopTongueSounds();
  xc18_tongueTarget = TongueTargetPos(mgr);
  xc34_tongueState = 1;
  // The one-shot is oriented in the ground plane only (retail zeroes z before normalising).
  CVector3f dir(xc18_tongueTarget.GetX() - xc0c_tonguePos.GetX(),
                xc18_tongueTarget.GetY() - xc0c_tonguePos.GetY(), 0.f);
  if (dir.CanBeNormalized()) {
    dir.Normalize();
  }
  PlayCustomSound(xc0c_tonguePos, dir, x7c0_data.sounds.tongueOut_OneShot, false);
}

// --------------------------------------------------------------------------------------------
// Player motion

// @ fn_78_511C
void CSwampBossStage1::Rumble(CStateManager& mgr) {
  mgr.RumbleManager(mgr.MaskUIdNumPlayers(mgr.GetPlayer(0)->GetUniqueId()))
      ->Rumble(mgr, static_cast< ERumbleFxId >(0x17), 1.f, kRP_Two);
}

// @ fn_78_2A4C
void CSwampBossStage1::KnockPlayer(CStateManager& mgr, const CVector3f& velocity) {
  CPlayer* player = mgr.GetPlayer(0);
  player->Stop();
  player->ApplyImpulseWR(velocity * player->GetMass(), CAxisAngle::Identity());
  player->SetMoveState(NPlayer::kMS_ApplyJump, mgr);
  Rumble(mgr);
}

// @ fn_78_39C0: the morphed player is dragged to the tongue at 30 units/s and floats.
// `lfs f3, 0x4(r5)` at 0x39F8 reads the target's y.
// `lfs f1, 0x0(r5)` at 0x3A08 reads the target's x.
// `lfs f1, 0x8(r5)` at 0x39EC reads its z, which the store at 0x3A24 replaces with 0.0.
// `bl CanBeNormalized__9CVector3fCFv` at 0x3A28 guards the whole body.
// `lfs f3, lbl_78_rodata_184@l(r3)` at 0x3A48 is the 30 units/s the direction is scaled by.
// `bl SetVelocityWR__13CPhysicsActorFRC9CVector3f` at 0x3A74 takes that velocity.
// `bl GetGravity__7CPlayerCFv` at 0x3A80 and `fmuls f1, f31, f1` at 0x3A88 give mass * gravity.
// `stfs f0, 0x1c0(r31)` at 0x3AA0 stores the force's x.
// `stfs f0, 0x1c4(r31)` at 0x3AA4 stores the force's y.
// `stfs f1, 0x1c8(r31)` at 0x3AA8 stores its z.
// `li r4, 0x4` at 0x3A9C is the movement state of the call at 0x3AAC (kMS_FallingMorphed).
// The CanBeNormalized arm that is never taken: fn_78_44CC passes the player's position plus a
// normalized direction times dt * 7.5 (dt > 0), so the difference is never zero - 12 calls in a
// run measured 0.084..0.106 units. The other arm is unreachable in the shipped data.
void CSwampBossStage1::PullMorphedPlayer(CStateManager& mgr, const CVector3f& pos) {
  CPlayer* player = mgr.GetPlayer(0);
  CVector3f dir(pos.GetX() - player->GetTranslation().GetX(),
                pos.GetY() - player->GetTranslation().GetY(), 0.f);
  if (dir.CanBeNormalized()) {
    dir.Normalize();
    player->SetVelocityWR(dir * 30.f);
    player->SetConstantForceWR(CVector3f(0.f, 0.f, player->GetMass() * player->GetGravity()));
    player->SetMoveState(NPlayer::kMS_FallingMorphed, mgr);
  }
}

// @ fn_78_3AD0
void CSwampBossStage1::PullPlayer(CStateManager& mgr, float dt, const CVector3f& pos) {
  CPlayer* player = mgr.GetPlayer(0);
  if (player->IsOnGround()) {
    player->Stop();
    const CTransform4f& pxf = player->GetTransform();
    CVector3f fwd(pxf.Get01(), pxf.Get11(), 0.f);
    if (fwd.CanBeNormalized()) {
      CTransform4f xf = CTransform4f::LookAt(CVector3f::Zero(), fwd.AsNormalized(), CVector3f::Up());
      xf.SetTranslation(pos);
      player->SetTransform(xf);
    }
  } else {
    player->SetMoveState(NPlayer::kMS_ApplyJump, mgr);
  }
  const float angle = dt * CMath::Deg2Rad(220.f);
  const CVector3f aim = player->GetAimPosition(mgr, 0.f);
  CVector3f toTongue = xc0c_tonguePos - aim;
  if (toTongue.CanBeNormalized()) {
    const CTransform4f& pxf = player->GetTransform();
    const CQuaternion q =
        CQuaternion::LookAt(CUnitVector3f(pxf.Get01(), pxf.Get11(), pxf.Get21()),
                            CUnitVector3f(toTongue.AsNormalized()), CRelAngle::FromRadians(angle));
    player->RotateInOneFrameOR(
        CQuaternion(q.GetScalar(), pxf.TransposeRotate(q.GetVector())), dt);
  }
}

// --------------------------------------------------------------------------------------------
// Platforms and messages

namespace {
EScriptObjectState PlatformResetState(int idx) {
  switch (idx) {
  case 0:
    return kSS_InternalState5;
  case 1:
    return kSS_InternalState4;
  case 2:
    return kSS_InternalState6;
  case 3:
    return kSS_InternalState7;
  default:
    return kSS_InternalState0;
  }
}

EScriptObjectState PlatformTargetState(int idx) {
  switch (idx) {
  case 0:
    return kSS_InternalState1;
  case 2:
    return kSS_InternalState2;
  case 3:
    return kSS_InternalState3;
  default:
    return kSS_InternalState0;
  }
}
} // namespace

// @ fn_78_260C
void CSwampBossStage1::ResetPlatformConnection(CStateManager& mgr) {
  if (xbf0_platformIdx == -1) {
    return;
  }
  const TUniqueId uid = CheckConnectedObject(mgr, PlatformResetState(xbf0_platformIdx), kSM_None);
  if (uid != kInvalidUniqueId) {
    mgr.SendScriptMsg(CScriptMsg(GetUniqueId(), uid, kSM_SetToZero));
    SendScriptMsgs(kSS_InternalState13, mgr, kSM_None);
    xbf0_platformIdx = -1;
    KnockPlayer(mgr, CVector3f(0.f, 0.f, 30.f));
  }
}

// @ fn_78_2728
void CSwampBossStage1::SetPlatformTarget(CStateManager& mgr) {
  if (xbf0_platformIdx != -1) {
    return;
  }
  const TUniqueId uid =
      CheckConnectedObject(mgr, PlatformTargetState(xbf4_platformPick), kSM_None);
  if (uid != kInvalidUniqueId) {
    mgr.SendScriptMsg(CScriptMsg(GetUniqueId(), uid, kSM_SetToZero));
    SendScriptMsgs(kSS_InternalState12, mgr, kSM_None);
    xbf0_platformIdx = xbf4_platformPick;
    KnockPlayer(mgr, CVector3f(0.f, 0.f, 30.f));
  }
}

// @ fn_78_2908
void CSwampBossStage1::FacePlatform() {
  float deg = 0.f;
  switch (xbf4_platformPick) {
  case 1:
    deg = 180.f;
    break;
  case 2:
    deg = 90.f;
    break;
  case 3:
    deg = 270.f;
    break;
  default:
    break;
  }
  SetTransform(CQuaternion::ZRotation(CRelAngle::FromDegrees(deg))
                   .BuildTransform4f(GetTranslation()));
}

// @ fn_78_29C4
void CSwampBossStage1::PickPlatform(CStateManager& mgr) {
  xbf4_platformPick = mgr.IsRandomAvailable() ? mgr.Random()->Range(0, 3) : 0;
}

// @ fn_78_3D34
void CSwampBossStage1::DeliverToConnected(CStateManager& mgr, EScriptObjectMessage msg) {
  const rstl::vector< SConnection >& conns = GetConnectionList();
  for (int i = 0; i < conns.size(); ++i) {
    if (conns[i].state != kSS_Connect || conns[i].msg != kSM_Attach) {
      continue;
    }
    const TUniqueId uid = mgr.GetIdForScript(conns[i].objId);
    CActor* act = TCastToPtr< CActor >(mgr.ObjectById(uid));
    if (act) {
      act->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), act->GetUniqueId(), msg));
    }
  }
}

// @ fn_78_3E4C
void CSwampBossStage1::IncrementAttached(CStateManager& mgr) {
  DeliverToConnected(mgr, kSM_Increment);
  mgr.GetPlayer(0)->SetMoveState(NPlayer::kMS_ApplyJump, mgr);
}

// @ fn_78_3E90
void CSwampBossStage1::DecrementAttached(CStateManager& mgr) {
  DeliverToConnected(mgr, kSM_Decrement);
}

// @ fn_78_390C
void CSwampBossStage1::ReleaseOrbitMessage(CStateManager& mgr) {
  if (xce4_orbitMsg) {
    xce4_orbitMsg = false;
    SendScriptMsgs(kSS_InternalState11, mgr, GetUniqueId(), kSM_None);
  }
}

// @ fn_78_3964: the orbit message goes out once, the first time the tongue grabs a morphed player.
// `lbz r5, 0xce4(r3)` at 0x3970 reads the flag byte.
// `extrwi r0, r5, 1, 24` at 0x3974 tests its bit 7 (the orbit message flag).
// `rlwimi r5, r0, 7, 24, 24` at 0x3988 sets that bit, stored by `stb r5, 0xce4(r3)` at 0x3990.
// `addi r4, r6, 0x3130` at 0x3998 is the "IS10" state sent by the call at 0x39AC.
// The "already sent" arm is never taken: the flag is cleared again by the matching IS11 message
// (fn_78_390C, the port's ReleaseOrbitMessage, which ran once with the flag set) when the pull
// ends, so every activation sees it clear - one call in a run, with the flag clear. The other
// arm is unreachable in the shipped data.
void CSwampBossStage1::SendOrbitMessage(CStateManager& mgr) {
  if (!xce4_orbitMsg) {
    xce4_orbitMsg = true;
    SendScriptMsgs(kSS_InternalState10, mgr, GetUniqueId(), kSM_None);
  }
}

// @ fn_78_4870
void CSwampBossStage1::ReleaseOrbitLock(CStateManager& mgr) {
  CPlayer* player = mgr.GetPlayer(0);
  if (player->GetOrbitState() == CPlayer::kOS_ForcedOrbitObject) {
    player->SetOrbitState(CPlayer::kOS_NoOrbit, mgr);
  }
}

// @ fn_78_48A8
void CSwampBossStage1::LockOrbitOnTongue(CStateManager& mgr) {
  CPlayer* player = mgr.GetPlayer(0);
  if (player->GetOrbitTargetId() != kInvalidUniqueId) {
    return;
  }
  for (int i = 0; i < ARRAY_SIZE(kJoints); ++i) {
    if (kJoints[i].mType != 2) {
      continue;
    }
    CCollisionActor* act = JointActor(mgr, i);
    if (act) {
      player->SetOrbitState(CPlayer::kOS_ForcedOrbitObject, mgr);
      player->SetOrbitTargetId(act->GetUniqueId(), mgr);
      return;
    }
  }
}

// --------------------------------------------------------------------------------------------
// Triggers

// @ fn_78_6548
bool CSwampBossStage1::IsMinHealth() const { return GetHealthInfo()->GetHP() <= 1.01f; }

// @ fn_78_653C
bool CSwampBossStage1::BeachAttackOver(CStateManager&, const CTriggerData&) const {
  return xc04_beachOver;
}

// @ fn_78_5E00
bool CSwampBossStage1::EnoughSplashing(CStateManager&, const CTriggerData&) const {
  return IsMinHealth() || xb84_phase != xbec_splashPhase || xbc8_jumps >= xbcc_jumpTarget;
}

// @ fn_78_65F0
bool CSwampBossStage1::GivingUp(CStateManager&, const CTriggerData&) const {
  return xbf8_platformTime > 8.f || (xbfc_misses >= 3 && xbf8_platformTime > 0.5f);
}

// @ fn_78_65E8
bool CSwampBossStage1::HeavyHit(CStateManager&, const CTriggerData&) const {
  return xd14_heavyHit;
}

// @ fn_78_624C
bool CSwampBossStage1::InVomitRange(CStateManager& mgr, const CTriggerData&) const {
  if (xce4_playerTouched) {
    return true;
  }
  const CVector3f target = TongueTargetPos(mgr);
  if (mgr.GetPlayer(0)->GetMorphballTransitionState() == CPlayer::kMS_Morphed) {
    return XYDistance(xc0c_tonguePos, target) < 6.f;
  }
  return (xc0c_tonguePos - target).Magnitude() < 8.5f;
}

// @ fn_78_6590
bool CSwampBossStage1::LightHit(CStateManager&, const CTriggerData&) const {
  return GetHealthInfo()->GetHP() <= xd0c_hpSnapshot - x7c0_data.unknown_0x1f4e7c2c;
}

bool CSwampBossStage1::MinHealth(CStateManager&, const CTriggerData&) const {
  return IsMinHealth();
}

// @ fn_78_67C8
bool CSwampBossStage1::ReadyToTongue(CStateManager& mgr, const CTriggerData&) const {
  return xbfc_misses < 3 && xbf8_platformTime >= 0.65f && TongueAngleToPlayer(mgr) <= 85.f &&
         PlayerInsideHint(mgr);
}

// @ fn_78_6358
bool CSwampBossStage1::ShouldBeach(CStateManager&, const CTriggerData&) const {
  return IsMinHealth() || (!(xb8c_nextAttackTime > xb1c_clock) && // retail NaN arm (A8-nan-5)
                           CurrentAttack() == 1);
}

// @ fn_78_64D0
bool CSwampBossStage1::ShouldBob(CStateManager&, const CTriggerData&) const {
  return !(GetHealthInfo()->GetHP() <= 1.f) && // retail NaN arm (A8-nan-6)
         xb7c_nextBobTime < xb1c_clock;
}

// @ fn_78_6458
bool CSwampBossStage1::ShouldExposeBelly(CStateManager&, const CTriggerData&) const {
  return xd10_bellyAccum >
         (IsMinHealth() ? x7c0_data.unknown_0x74e1a041 : x7c0_data.unknown_0x78755da3);
}

// @ fn_78_63D8
bool CSwampBossStage1::ShouldSplash(CStateManager&, const CTriggerData&) const {
  return !(GetHealthInfo()->GetHP() <= 1.f) && // retail NaN arm (A8-4)
         !(xb8c_nextAttackTime > xb1c_clock) && CurrentAttack() == 0;
}

// @ fn_78_5CD4
bool CSwampBossStage1::SplashAttackOver(CStateManager&, const CTriggerData&) const {
  return xbd8_splashOver;
}

// @ fn_78_66D4
bool CSwampBossStage1::TongueHit(CStateManager&, const CTriggerData&) const {
  return xc34_tongueState == kTS_Hit;
}

// @ fn_78_6638
bool CSwampBossStage1::TongueMissed(CStateManager& mgr, const CTriggerData&) const {
  return TongueAngleToPlayer(mgr) > 85.f || !PlayerInsideHint(mgr) ||
         (xc30_extend >= 1.f && xc34_tongueState != kTS_Hit);
}

// @ fn_78_44B0
bool CSwampBossStage1::TooMuchPulling(CStateManager&, const CTriggerData&) const {
  return xc38_pullTime > 8.f;
}

// @ fn_78_4D1C
bool CSwampBossStage1::Wait(CStateManager&, const CTriggerData& data) const {
  return xd78_waitStart + data.GetFloat() < xb18_time;
}

// --------------------------------------------------------------------------------------------
// Code functions

// @ fn_78_25F4: the miss counter goes up and the tongue's state becomes 3 (not out).
// `lwz r4, 0xbfc(r3)` at 0x25F4 loads the miss counter.
// `addi r4, r4, 0x1` at 0x25FC and `stw r4, 0xbfc(r3)` at 0x2600 are the increment.
// `li r0, 0x3` at 0x25F8 is the value stored by `stw r0, 0xc34(r3)` at 0x2604.
void CSwampBossStage1::IncrementMisses(CStateManager&, float) {
  ++xbfc_misses;
  // Retail writes 3 here; the reads only distinguish 0/1/2 and TongueMissed counts anything but
  // kTS_Hit as a miss, so 3 is a plain "no longer out" marker with no name in the data.
  xc34_tongueState = 3;
}

// @ fn_78_5C30
void CSwampBossStage1::SetSplashRotation(CStateManager& mgr, float) {
  SetTransform(CQuaternion::ZRotation(CRelAngle::FromDegrees(xbd0_splashRotDeg))
                   .BuildTransform4f(GetTranslation()));
  xbd0_splashRotDeg += mgr.Random()->Range(140.f, 220.f);
  PickSplashLocomotion(mgr);
}

// @ fn_78_5CDC
void CSwampBossStage1::PickSplashLocomotion(CStateManager& mgr) {
  if (mgr.Random()->Range(0.f, 1.f) < 0.7f) {
    xb6c_altAnims = xb6c_altAnims == 0 ? 1 : 0;
  }
  SetLocomotion(SplashLocomotion());
}

// @ fn_78_252C
void CSwampBossStage1::ShreddersOff(CStateManager& mgr, float) {
  if (xb5c_shredders != 1) {
    SendScriptMsgs(kSS_AILogicState2, mgr, kInvalidUniqueId, kSM_None);
    xb5c_shredders = 1;
  }
}

// @ fn_78_2590
void CSwampBossStage1::ShreddersOn(CStateManager& mgr, float) {
  if (xb5c_shredders != 0) {
    SendScriptMsgs(kSS_AILogicState1, mgr, kInvalidUniqueId, kSM_None);
    xb5c_shredders = 0;
  }
}

// --------------------------------------------------------------------------------------------
// States

// @ fn_78_19A4: the tongue-state reset the retail's TongueLoop (fn_78_31E8) calls on Activate
// (0x3254: `addi r3, r29, 0xc0c`, the tongue block) and the CModelData member's constructor
// (fn_78_19FC) calls as well: the tip and the target go to the zero vector, the extend, the pull
// time and the pull speed to 0, the tongue state to kTS_Idle and the vomit timer to 0 (the retail
// zeroes the tip from sZeroVector and copies it into the target, 0x19C8-0x19F4).
// From the block base: 0x24/0x28/0x2c/0x30 are the extend, the tongue state, the pull time and
// the pull speed (0x19B8-0x19C4); 0xdc is the vomit timer (0x19B4).
void CSwampBossStage1::ResetTongueState() {
  xc18_tongueTarget = CVector3f::Zero();
  xc24_tongueTip = CVector3f::Zero();
  xc30_extend = 0.f;
  xc34_tongueState = kTS_Idle;
  xc38_pullTime = 0.f;
  xc3c_pullSpeed = 0.f;
  xce8_vomitTimer = 0.f;
}

// @ fn_78_31E8
void CSwampBossStage1::TongueLoop(CStateManager& mgr, EStateMsg msg, float dt) {
  RecordState(9, msg);
  SetLocomotion(3);
  if (msg == kStateMsg_Activate) {
    BodyController()->CommandMgr().ClearLocomotionCmds();
    ResetTongueState();
    ++xc00_tongues;
    SetJointsActive(mgr, 1, kJF_RemoveTarget | kJF_Reflect);
    SetJointsActive(mgr, 0, kJF_RemoveTarget | kJF_Vulnerable);
    SetJointsActive(mgr, 2, kJF_AddTarget | kJF_Vulnerable);
  } else if (msg == kStateMsg_Update) {
    if (xc34_tongueState != kTS_Idle) {
      ExtendTongue(mgr, dt);
      UpdateTongueTip(mgr, 1.f, true);
    }
    HeadWeightUp(dt);
    UpdateHeadTurn(mgr, dt);
  }
}

// @ fn_78_35DC: the hit-react body the light and heavy react states share. It records state 3
// and the locomotion 5, and on Update it retracts the tongue, updates the tip, drops the head
// weight and turns the head; the other messages do nothing else.
// `li r4, 0x3` at 0x360C is the state the call at 0x3610 records.
// `li r4, 0x5` at 0x3618 is the locomotion the call at 0x361C sets.
// `cmpwi r31, 0x1` at 0x3620 is the Update test.
// `bne .L_00003670` at 0x3624 skips the body for the other messages.
// `bl fn_78_729C` at 0x3634 retracts the tongue.
// `bl fn_78_716C` at 0x3650 updates the tip.
// `bl fn_78_7224` at 0x365C drops the head weight.
// `bl fn_78_2E38` at 0x366C turns the head.
// A run exercised every arm: 1 x Activate, 104 x Update, 1 x Deactivate.
void CSwampBossStage1::HitReactBody(CStateManager& mgr, EStateMsg msg, float dt) {
  RecordState(3, msg);
  SetLocomotion(5);
  if (msg == kStateMsg_Update) {
    RetractTongue(mgr, dt);
    UpdateTongueTip(mgr, 1.f, false);
    HeadWeightDown(dt);
    UpdateHeadTurn(mgr, dt);
  }
}

// @ fn_78_3320: the light hit react - the shared hit-react body plus, on Activate, the additive
// reaction command.
// `bl fn_78_35DC` at 0x333C is the shared body.
// `cmpwi r31, 0x0` at 0x3340 is the Activate test.
// `bne .L_000033D0` at 0x3344 skips the command for the other messages.
// `bl DeliverCmd__16CBodyStateCmdMgrF13EBodyStateCmd` at 0x3394 is the additive reaction.
// A run exercised every arm: 8 x Activate, 56 x Update, 8 x Deactivate.
void CSwampBossStage1::LightHitReact(CStateManager& mgr, EStateMsg msg, float dt) {
  HitReactBody(mgr, msg, dt);
  if (msg == kStateMsg_Activate) {
    BodyController()->CommandMgr().DeliverCmd(
        CBCAdditiveReactionCmd(pas::kART_Five, 1.f, false));
  }
}

// @ fn_78_348C: the heavy hit react - on Activate the joints go to kAllJoints with
// RemoveTarget|Reflect and the body gets the next state command, then the shared hit-react body
// and the taunt.
// `bne .L_000034F8` at 0x34B8 is the Activate test.
// `bl fn_78_9744` at 0x34C4 sets the joints.
// `bl DeliverCmd__16CBodyStateCmdMgrFRC13CBodyStateCmd` at 0x34E8 is the next state command.
// `bl fn_78_35DC` at 0x3508 is the shared body.
// A run exercised every arm: 1 x Activate, 104 x Update, 1 x Deactivate.
void CSwampBossStage1::HeavyHitReact(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Reflect);
    BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_NextState));
  }
  HitReactBody(mgr, msg, dt);
  BodyTaunt(mgr, msg, 4);
}

// @ fn_78_3694
void CSwampBossStage1::ExposeBelly(CStateManager& mgr, EStateMsg msg, float dt) {
  RecordState(3, msg);
  SetLocomotion(5);
  if (msg == kStateMsg_Activate) {
    xd0c_hpSnapshot = HealthInfo()->GetHP();
    DecrementAttached(mgr);
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Vulnerable);
    SetJointsActive(mgr, 1, kJF_AddTarget | kJF_Vulnerable);
    StopTongueSounds();
  } else if (msg == kStateMsg_Update) {
    RetractTongue(mgr, dt);
    UpdateTongueTip(mgr, 1.f, false);
    HeadWeightDown(dt);
    UpdateHeadTurn(mgr, dt);
  } else if (msg == kStateMsg_Deactivate) {
    StopTongue();
  }
  BodyMelee(mgr, msg, 9);
}

// @ fn_78_38A4
void CSwampBossStage1::ResetPlatform(CStateManager& mgr, EStateMsg msg, float) {
  if (msg == kStateMsg_Activate) {
    ResetPlatformConnection(mgr);
    xd14_heavyHit = false;
    SetLocomotion(7);
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Reflect | kJF_RemoveRadar);
  }
}

// @ fn_78_4280
void CSwampBossStage1::Vomit(CStateManager& mgr, EStateMsg msg, float dt) {
  RecordState(10, msg);
  SetLocomotion(3);
  // Retail runs these before the message switch (0x42C8..0x4368: fn_78_729C, fn_78_716C,
  // fn_78_7224, fn_78_2E38, then the melee call fn_78_BA9C; the compare of msg is at 0x4378).
  RetractTongue(mgr, dt);
  UpdateTongueTip(mgr, 1.f, false);
  HeadWeightDown(dt);
  UpdateHeadTurn(mgr, dt);
  BodyMelee(mgr, msg, 1);
  if (msg == kStateMsg_Activate) {
    SetJointsActive(mgr, 1, kJF_RemoveTarget | kJF_Reflect);
    SetJointsActive(mgr, 2, kJF_AddTarget | kJF_Vulnerable);
    SetJointsActive(mgr, 0, kJF_RemoveTarget | kJF_Vulnerable);
    xd54_vomitHits = 0;
    if (xce8_vomitTimer <= 0.f) {
      DecrementAttached(mgr);
    }
    Rumble(mgr);
    StopTongueSounds();
  } else if (msg == kStateMsg_Update) {
    if (xce8_vomitTimer > 0.f) {
      xce8_vomitTimer -= dt;
      if (xce8_vomitTimer < 0.f) {
        DecrementAttached(mgr);
        xce8_vomitTimer = 0.f;
      }
    }
  } else if (msg == kStateMsg_Deactivate) {
    DecrementAttached(mgr);
    xce8_vomitTimer = 0.f;
    StopTongue();
  }
}

// @ fn_78_44CC
void CSwampBossStage1::TonguePull(CStateManager& mgr, EStateMsg msg, float dt) {
  RecordState(5, msg);
  CPlayer* player = mgr.GetPlayer(0);
  if (msg == kStateMsg_Activate) {
    xc38_pullTime = 0.f;
    xc3c_pullSpeed = 1.f;
    xce4_playerTouched = false;
    xc30_extend = 1.f;
    SetJointsActive(mgr, 1, kJF_RemoveTarget | kJF_Reflect);
    SetJointsActive(mgr, 0, kJF_RemoveTarget | kJF_Vulnerable);
    SetJointsActive(mgr, 2, kJF_AddTarget | kJF_Vulnerable);
    IncrementAttached(mgr);
    SetLocomotion(6);
    xce8_vomitTimer = 0.f;
    if (player->GetMorphballTransitionState() == CPlayer::kMS_Morphed) {
      SendOrbitMessage(mgr);
    }
    LockOrbitOnTongue(mgr);
  } else if (msg == kStateMsg_Update) {
    xc38_pullTime += dt;
    xc3c_pullSpeed = rstl::max_val(xc3c_pullSpeed - dt / 0.3f, 0.1f);
    const CVector3f target = TongueTargetPos(mgr);
    const CVector3f toTongue = xc0c_tonguePos - target;
    if (toTongue.CanBeNormalized()) {
      const CVector3f dir = toTongue.AsNormalized();
      if (player->GetMorphballTransitionState() == CPlayer::kMS_Morphed) {
        PullMorphedPlayer(mgr, player->GetTranslation() + dir * (dt * 7.5f));
      } else {
        const float speed = player->GetBackwardInput() > 0.1f ? 3.f : 7.5f;
        PullPlayer(mgr, dt, player->GetTranslation() + dir * (dt * speed));
      }
    }
    xc18_tongueTarget = target;
    UpdateTongueTip(mgr, xc3c_pullSpeed, false);
    UpdateHeadTurn(mgr, dt);
  } else if (msg == kStateMsg_Deactivate) {
    player->EnableLeaveMorphBall(true);
    player->GetMorphBall()->SetBoostEnabled(true);
    player->Stop();
    ReleaseOrbitMessage(mgr);
    SetLocomotion(3);
    xce8_vomitTimer = 1.2f;
    ReleaseOrbitLock(mgr);
  }
}

// @ fn_78_4984
void CSwampBossStage1::PlatformReady(CStateManager& mgr, EStateMsg msg, float dt) {
  RecordState(4, msg);
  if (msg == kStateMsg_Activate) {
    SetLocomotion(2);
    xbf8_platformTime = 0.f;
    xce4_playerTouched = false;
    SetJointsActive(mgr, 1, kJF_RemoveTarget | kJF_Reflect);
    SetJointsActive(mgr, 2, kJF_AddTarget | kJF_Vulnerable);
    SetJointsActive(mgr, 0, kJF_RemoveTarget | kJF_Vulnerable);
  } else {
    xbf8_platformTime += dt;
    RetractTongue(mgr, dt);
    UpdateTongueTip(mgr, 1.f, false);
    HeadWeightUp(dt);
    UpdateHeadTurn(mgr, dt);
  }
}

// @ fn_78_4AA8
void CSwampBossStage1::Beach(CStateManager& mgr, EStateMsg msg, float) {
  RecordState(0, msg);
  if (msg == kStateMsg_Activate) {
    xcf4_headWeight = 0.f;
    xcf8_headDir = CVector3f::Zero();
    xd04_headLatch = true;
    xd08_headBlend = 100.f;
    UpdateHeadTurn(mgr, 1000.f);
    xbfc_misses = 0;
    xc00_tongues = 0;
    xc08_beachPhase = xb84_phase;
    SetJointsActive(mgr, 1, kJF_RemoveTarget | kJF_Reflect);
    SetJointsActive(mgr, 2, kJF_AddTarget | kJF_Vulnerable);
    SetJointsActive(mgr, 0, kJF_RemoveTarget | kJF_Vulnerable);
    SetLocomotion(2);
    SetBeachMaterials(mgr, true);
    ScheduleNextAttack(mgr);
    Rumble(mgr);
    StopSfx(xb38_telegraphSfx);
  } else if (msg == kStateMsg_Deactivate) {
    SetBeachMaterials(mgr, false);
  }
  // The descriptor retail passes is {data_700, 0x15, 6}: taunt type 6.
  BodyTaunt(mgr, msg, 6);
}

// @ fn_78_4C90
void CSwampBossStage1::StopBeaching(CStateManager& mgr, EStateMsg msg, float) {
  SetLocomotion(7);
  xc04_beachOver = true;
  if (msg == kStateMsg_Activate && xb84_phase == xc08_beachPhase) {
    ++xb88_attackIdx;
    ScheduleNextAttack(mgr);
  }
}

// @ fn_78_4D3C
void CSwampBossStage1::SplashWaitBody(CStateManager& mgr, EStateMsg msg) {
  RecordState(-1, msg);
  xd66_ringOn = false;
  if (msg == kStateMsg_Activate) {
    xd78_waitStart = xb18_time;
    SetJointsActive(mgr, 2, kJF_RemoveRadar);
    const CPASAnimParmData parms(
        static_cast< pas::EAnimationState >(7), CPASAnimParm::FromEnum(6),
        CPASAnimParm::FromEnum(SplashLocomotion()), CPASAnimParm::NoParameter(),
        CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
        CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter());
    BodyController()->CommandMgr().Reset();
    BodyController()->LoopBestAnimation(parms, *mgr.Random());
  }
}

void CSwampBossStage1::SplashWait(CStateManager& mgr, EStateMsg msg, float) {
  SplashWaitBody(mgr, msg);
}

// @ fn_78_5088
void CSwampBossStage1::SplashTelegraph(CStateManager& mgr, EStateMsg msg, float) {
  if (msg == kStateMsg_Activate) {
    SetJointsActive(mgr, 2, kJF_AddRadar);
    xd78_waitStart = xb18_time;
    xd66_ringOn = false;
    StartTelegraph(mgr, xb20_rootPos);
    Rumble(mgr);
  }
  RecordState(8, msg);
}

// @ fn_78_5188
void CSwampBossStage1::BeachBubbles(CStateManager& mgr, EStateMsg msg, float) {
  SetLocomotion(7);
  if (msg == kStateMsg_Activate) {
    xd78_waitStart = xb18_time;
    const CVector3f& pos = GetTranslation();
    StartTelegraph(mgr, CVector3f(pos.GetX() + (xb20_rootPos.GetX() - pos.GetX()) * 2.f,
                                  pos.GetY() + (xb20_rootPos.GetY() - pos.GetY()) * 2.f,
                                  pos.GetZ()));
    SetJointsActive(mgr, 2, kJF_AddRadar);
    Rumble(mgr);
  }
}

// @ fn_78_5268
void CSwampBossStage1::BeachWait(CStateManager& mgr, EStateMsg msg, float) {
  SetLocomotion(7);
  if (msg == kStateMsg_Activate) {
    xd78_waitStart = xb18_time;
    xd10_bellyAccum = 0.f;
    xd14_heavyHit = false;
    PickPlatform(mgr);
    FacePlatform();
    SetJointsActive(mgr, kAllJoints, kJF_RemoveRadar);
  }
}

// @ fn_78_5304
void CSwampBossStage1::Null(CStateManager& mgr, EStateMsg msg, float) {
  SetLocomotion(7);
  if (msg == kStateMsg_Activate) {
    xd78_waitStart = xb18_time;
    SetJointsActive(mgr, 2, kJF_RemoveRadar);
  }
}

// @ fn_78_5374
void CSwampBossStage1::Dive(CStateManager& mgr, EStateMsg msg, float) {
  RecordState(2, msg);
  if (msg == kStateMsg_Activate) {
    xc04_beachOver = false;
    xd10_bellyAccum = 0.f;
    xd14_heavyHit = false;
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Reflect);
    SetJointsActive(mgr, 0, kJF_Vulnerable);
  } else if (msg == kStateMsg_Update) {
    SetLocomotion(7);
  }
  BodyMelee(mgr, msg, 0xb);
}

// @ fn_78_5784: the larva slides into the water. On Activate the joints go to kAllJoints with
// RemoveTarget|Reflect, the boss rumbles and the tongue stops; every message falls through to the
// body part.
// `li r4, 0x6` at 0x57A0 is the state the call at 0x57AC records.
// `cmpwi r31, 0x0` at 0x57B0 is the Activate test for the joint and rumble branch.
// `li r5, 0x3` at 0x57C0 is the joint type (kAllJoints).
// `li r6, 0xa` at 0x57C4 is the flags (RemoveTarget|Reflect).
// `bl fn_78_9744` at 0x57C8 is the SetJointsActive call.
// `bl fn_78_511C` at 0x57D4 is the rumble.
// `cmpwi r31, 0x0` at 0x57D8 is the Activate test for the tongue.
// `bl fn_78_6BEC` at 0x57E4 stops the tongue.
// A run exercised every arm: 9 x Activate, 909 x Update, 9 x Deactivate.
void CSwampBossStage1::SlideIntoWater(CStateManager& mgr, EStateMsg msg, float) {
  RecordState(6, msg);
  if (msg == kStateMsg_Activate) {
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Reflect);
    Rumble(mgr);
    StopTongue();
  }
  BodyMelee(mgr, msg, 3);
}

// @ fn_78_587C
void CSwampBossStage1::BeachRise(CStateManager& mgr, EStateMsg msg, float) {
  RecordState(7, msg);
  if (msg == kStateMsg_Activate) {
    SetLocomotion(RiseLocomotion());
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Reflect | kJF_RemoveRadar);
    ScheduleNextAttack(mgr);
  }
  BodyMelee(mgr, msg, 8);
}

// @ fn_78_5984
void CSwampBossStage1::SplashRise(CStateManager& mgr, EStateMsg msg, float) {
  RecordState(7, msg);
  if (msg == kStateMsg_Activate) {
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Reflect);
    ScheduleNextAttack(mgr);
  } else {
    SetLocomotion(RiseLocomotion());
  }
  BodyMelee(mgr, msg, 8);
}

// @ fn_78_5A88
void CSwampBossStage1::SplashJump(CStateManager& mgr, EStateMsg msg, float) {
  RecordState(7, msg);
  if (msg == kStateMsg_Activate) {
    if (xb58_scanned) {
      SetJointsActive(mgr, 2, kJF_RemoveTarget);
      SetJointsActive(mgr, 1, kJF_AddTarget);
    } else {
      SetJointsActive(mgr, 2, kJF_AddTarget);
      SetJointsActive(mgr, 1, kJF_RemoveTarget);
    }
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Vulnerable);
    StopSfx(xb38_telegraphSfx);
    ++xbc8_jumps;
    xd74_ringRate = 2.f;
  } else if (msg == kStateMsg_Deactivate) {
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Reflect);
    xd74_ringRate = 1.f;
  }
  BodyMelee(mgr, msg, 7);
}

// @ fn_78_5D6C
void CSwampBossStage1::SetSplashOver(CStateManager& mgr, EStateMsg msg, float) {
  SplashWaitBody(mgr, msg);
  xbd8_splashOver = true;
  if (msg == kStateMsg_Activate) {
    SetLocomotion(SplashLocomotion());
    if (xb84_phase == xbec_splashPhase) {
      ++xb88_attackIdx;
      ScheduleNextAttack(mgr);
    }
  }
}

// @ fn_78_5E84
void CSwampBossStage1::SplashDescend(CStateManager& mgr, EStateMsg msg, float) {
  RecordState(7, msg);
  if (msg == kStateMsg_Activate) {
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Reflect);
    xbc8_jumps = 0;
    xbd0_splashRotDeg = mgr.Random()->Range(0.f, 360.f);
    xbd8_splashOver = false;
    xbec_splashPhase = xb84_phase;
    const SLdrSwampBossStage1Struct& phase = CurrentPhase();
    xbcc_jumpTarget = mgr.Random()->Range(phase.unknown_0xbb0ffdd6, phase.unknown_0x60b0ae31);
    SetJointsActive(mgr, 0, kJF_Vulnerable);
  } else if (msg == kStateMsg_Update) {
    SetLocomotion(SplashLocomotion());
  } else if (msg == kStateMsg_Deactivate) {
    SetJointsActive(mgr, 0, kJF_Reflect);
  }
  BodyMelee(mgr, msg, 0xb);
}

// @ fn_78_6034
void CSwampBossStage1::Bob(CStateManager& mgr, EStateMsg msg, float) {
  RecordState(1, msg);
  if (msg == kStateMsg_Activate) {
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Reflect);
    SetJointsActive(mgr, 0, kJF_Vulnerable);
    xb7c_nextBobTime =
        xb1c_clock + mgr.Random()->Range(x7c0_data.unknown_0x27a06f6a, x7c0_data.unknown_0x233a5e40);
  } else if (msg == kStateMsg_Update) {
    AlertShredders(mgr);
  } else if (msg == kStateMsg_Deactivate) {
    SetJointsActive(mgr, 0, kJF_RemoveTarget | kJF_Reflect);
  }
  BodyMelee(mgr, msg, 0);
}

// @ fn_78_6198
void CSwampBossStage1::Swim(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    SetLocomotion(RiseLocomotion());
    SetBeachMaterials(mgr, false);
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Reflect);
    SetJointsActive(mgr, 2, kJF_AddRadar);
  } else if (msg == kStateMsg_Update) {
    xb1c_clock += dt;
    AlertShredders(mgr);
  }
}

// @ fn_78_22C8
void CSwampBossStage1::Dead(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    DecrementAttached(mgr);
    if (xd58_savedStepUp >= 0.f) {
      mgr.GetPlayer(0)->SetStepUpHeight(xd58_savedStepUp);
    }
    if (!xd60_ringGen.null()) {
      xd60_ringGen->SetParticleEmission(false);
      xd60_ringGen = nullptr;
    }
    if (!xbc4_telegraphGen.null()) {
      xbc4_telegraphGen->SetParticleEmission(false);
      xbc4_telegraphGen = nullptr;
    }
    StopSfx(xb38_telegraphSfx);
    StopSfx(xbdc_shockWaveSfx);
    StopTongueSounds();
    SendScriptMsgs(kSS_Dead, mgr, kInvalidUniqueId, kSM_None);
  }
  CPatterned::Dead(mgr, msg, dt);
}

// --------------------------------------------------------------------------------------------
// Attacks

// @ fn_78_3EB8
void CSwampBossStage1::VomitHit(CStateManager& mgr) {
  if (xd54_vomitHits >= 1 || TongueAngleToPlayer(mgr) > 100.f) {
    return;
  }
  const CVector3f target = TongueTargetPos(mgr);
  const CVector3f away(xc0c_tonguePos.GetX() - target.GetX(),
                       xc0c_tonguePos.GetY() - target.GetY(), 0.f);
  if (away.Magnitude() > 10.5f) {
    return;
  }
  // Retail keeps the config's damage and sounds but zeroes the radial damage amount and the
  // knockback (the knockback below is applied through KnockPlayer instead).
  CDamageInfo vomit = LdrToDamageInfo(x7c0_data.damageInfo);
  vomit.SetRadiusDamage(0.f);
  vomit.SetKnockBackPower(0.f);
  mgr.ApplyDamage(GetUniqueId(), mgr.GetPlayer(0)->GetUniqueId(), GetUniqueId(), vomit,
                  SolidFilter(), CVector3f::Zero());
  if (xd54_vomitHits == 0) {
    mgr.AddObject(rs_new CHUDBillboardEffect(
        xd48_goo, rstl::optional_object< TToken< CElectricDescription > >(),
        mgr.AllocateUniqueId(), true, rstl::string_l("Swamp Boss 1 barf visor goo"),
        CHUDBillboardEffect::GetNearClipDistance(mgr, 0), CHUDBillboardEffect::GetScaleForPOV(mgr),
        0, CColor::White(), CVector3f(1.f, 1.f, 1.f), CVector3f::Zero(), false));
  }
  ++xd54_vomitHits;
  if (away.CanBeNormalized()) {
    const CVector3f dir = away.AsNormalized();
    KnockPlayer(mgr, CVector3f(dir.GetX() * -12.f, dir.GetY() * -12.f, 12.f));
  }
}

// @ fn_78_800C
void CSwampBossStage1::Spit(CStateManager& mgr) {
  const CVector3f aim = mgr.GetPlayer(0)->GetAimPosition(mgr, 0.f);
  const CTransform4f tongueXf = GetLocatorTransform(rstl::string_l("tongue_LCTR_SDK"));
  const CTransform4f xf = CTransform4f::LookAt(tongueXf.GetTranslation(), aim, CVector3f::Up());
  const CImpactVisorEffect visorEffect =
      xda4_spitVisor
          ? CImpactVisorEffect::ParticleEffect(
                xda4_spitVisor, static_cast< TSfxId >(x7c0_data.sound_SpitVisor & 0xffff), false)
          : CImpactVisorEffect::None();
  CEnergyProjectile* projectile =
      LaunchProjectile(xf, mgr, 0x40, 0x400, true, visorEffect, CVector3f(1.f, 1.f, 1.f));
  if (projectile) {
    projectile->SetProjExtent(x7c0_data.spitProjectileRadius);
  }
}

// @ fn_78_7C2C
void CSwampBossStage1::LaunchShockWave(CStateManager& mgr) {
  SendScriptMsgs(kSS_InternalState14, mgr, kInvalidUniqueId, kSM_None);
  if (xb90_shockWave.shockWaveEffect == kInvalidAssetId) {
    return;
  }
  CTransform4f xf(GetTransform());
  const CVector3f pos(xb20_rootPos.GetX(), xb20_rootPos.GetY(), WaterHeight(mgr) - 4.f);
  xf.SetTranslation(pos);
  CShockWave* wave = rs_new CShockWave(
      mgr.AllocateUniqueId(), rstl::string_l("Swamp Boss 1 Shockwave"),
      CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true), xf, GetUniqueId(),
      CShockWaveInfo(xb90_shockWave), 2.f, 0.4f);
  mgr.AddObject(wave);
  xbda_shockWave = wave->GetUniqueId();
  StopSfx(xbdc_shockWaveSfx);
  xbdc_shockWaveSfx =
      PlayCustomSound(pos, CVector3f::Up(), x7c0_data.sounds.shockWaveVolumetric_Loop, true);
}

// @ fn_78_4E58
void CSwampBossStage1::StartTelegraph(CStateManager& mgr, const CVector3f& pos) {
  const CVector3f p(pos.GetX(), pos.GetY(), WaterHeight(mgr));
  if (xbd4_telegraphPart != kInvalidAssetId) {
    xbc4_telegraphGen = MakeGen(xbd4_telegraphPart);
    xbc4_telegraphGen->SetParticleEmission(true);
    xbc4_telegraphGen->SetGlobalTranslation(p);
  }
  StopSfx(xb38_telegraphSfx);
  xb38_telegraphSfx = PlayCustomSound(p, CVector3f::Up(), x7c0_data.sounds.telegraph_Loop, true);
}

// @ fn_78_8FF0
void CSwampBossStage1::UpdateRing(CStateManager& mgr, float dt) {
  if (!xd60_ringGen.null()) {
    if (xd66_ringOn) {
      const float water = WaterHeight(mgr);
      xd60_ringGen->SetParticleEmission(true);
      xd68_headOffset.SetZ(0.f);
      if (xd68_headOffset.CanBeNormalized()) {
        xd68_headOffset.Normalize();
        const CTransform4f look = CTransform4f::LookAt(
            CVector3f::Zero(),
            CVector3f(xd68_headOffset.GetY(), -xd68_headOffset.GetX(), 0.f), CVector3f::Up());
        xd60_ringGen->SetOrientation(look.GetRotation());
      }
      xd60_ringGen->SetTranslation(
          CVector3f(xb20_rootPos.GetX(), xb20_rootPos.GetY(), water + 0.2f));
    } else {
      xd60_ringGen->SetParticleEmission(false);
    }
    xd60_ringGen->Update(dt * xd74_ringRate);
  }
  if (!xbc4_telegraphGen.null()) {
    xbc4_telegraphGen->Update(dt);
  }
}

// @ fn_78_91A8
void CSwampBossStage1::UpdateShockWaveSound(CStateManager& mgr) {
  if (xbda_shockWave != kInvalidUniqueId && xbdc_shockWaveSfx) {
    // Retail reads the object straight from GetObjectById, without a cast (as CPuddleSpore does).
    const CShockWave* wave = static_cast< const CShockWave* >(mgr.GetObjectById(xbda_shockWave));
    if (wave) {
      const CVector3f aim = mgr.GetPlayer(0)->GetAimPosition(mgr, 0.f);
      CVector3f fromWave = aim - wave->GetTranslation();
      if (!fromWave.CanBeNormalized()) {
        return;
      }
      if (fromWave.Magnitude() < wave->GetRadius()) {
        CSfxManager::UpdateEmitter(xbdc_shockWaveSfx, aim, CVector3f::Up(), 0x7f);
        xbe0_shockWaveSfxPos = aim;
        return;
      }
      fromWave.Normalize();
      const CVector3f edge = wave->GetTranslation() + fromWave * wave->GetRadius();
      CSfxManager::UpdateEmitter(xbdc_shockWaveSfx, edge, fromWave, 0x7f);
      xbe0_shockWaveSfxPos = edge;
      return;
    }
  }
  StopSfx(xbdc_shockWaveSfx);
  xbda_shockWave = kInvalidUniqueId;
  xbe0_shockWaveSfxPos = CVector3f::Zero();
}

// @ fn_78_93D4
void CSwampBossStage1::AlertShredders(CStateManager& mgr) {
  if (xb5c_shredders != 0) {
    return;
  }
  const CVector3f fwd = CVector3f(xb2c_headPos.GetX() - xb20_rootPos.GetX(),
                                  xb2c_headPos.GetY() - xb20_rootPos.GetY(), 0.f)
                            .AsNormalized();
  xb60_shredderOrigin = xb2c_headPos + fwd * 20.f;
  CObjectList& list = mgr.ObjectListById(kOL_All);
  for (int idx = list.GetFirstObjectIndex(); idx != -1; idx = list.GetNextObjectIndex(idx)) {
    CActor* act = TCastToPtr< CActor >(list[idx]);
    if (act == nullptr || !act->GetActive() || act == this ||
        act->GetCurrentAreaId() != GetCurrentAreaId()) {
      continue;
    }
    CPatterned* pat = TCastToPtr< CPatterned >(act);
    if (pat == nullptr) {
      continue;
    }
    const float dx = pat->GetTranslation().GetX() - xb60_shredderOrigin.GetX();
    const float dy = pat->GetTranslation().GetY() - xb60_shredderOrigin.GetY();
    if (dx * dx + dy * dy + 0.f < 400.f) {
      mgr.DeliverScriptMsg(CScriptMsg(GetUniqueId(), pat->GetUniqueId(), kSM_Alert));
    }
  }
}

// --------------------------------------------------------------------------------------------
// Entity / actor overrides

// @ fn_78_95D8
void CSwampBossStage1::Think(float dt, CStateManager& mgr) {
  if (!GetActive()) {
    xb58_radarPresent = false;
    return;
  }
  // Retail first runs SetupStateMachine (fn_78_6864) and returns while the FSM is uninitialised;
  // the host CPatterned::Think does that set-up itself.
  if (xb80_collision.null()) {
    CreateCollision(mgr);
  }
  if (!xb80_collision.null()) {
    xb80_collision->Update(dt, mgr, CCollisionActorManager::kUO_WorldSpace);
  }
  CPatterned::Think(dt, mgr);
  xb18_time += dt;
  UpdateRing(mgr, dt);
  UpdateShockWaveSound(mgr);
  if (GetHealthInfo()->GetHP() < CurrentPhase().unknown_0x98106ee2 && xb84_phase < 2) {
    ScheduleNextAttack(mgr);
  }
  xb58_radarPresent = AnyJointOnRadar(mgr);
}

// fn_78_9B74's kSM_Damage branch ('XDMG', 0x9E10; the function's marker is on AcceptScriptMsg):
// route a collision joint's hit to the boss body. Returns false when retail returns before
// CPatterned::AcceptScriptMsg (the same projectile hit again).
bool CSwampBossStage1::ApplyDamageMsg(CStateManager& mgr, const CScriptMsg& msg) {
  if (!GetAlive()) {
    return true;
  }
  CCollisionActor* col = TCastToPtr< CCollisionActor >(mgr.ObjectById(msg.GetSenderId()));
  if (!col) {
    return true;
  }
  const TUniqueId lastTouched = col->GetLastTouchedObject();
  const CGameProjectile* proj =
      TCastToConstPtr< CGameProjectile >(mgr.GetObjectById(lastTouched));
  if (xb5a_lastHitter != kInvalidUniqueId && lastTouched == xb5a_lastHitter) {
    return false;
  }
  xb5a_lastHitter = lastTouched;

  if (proj && col->GetHealthInfo()->GetHP() < kCollisionActorHP &&
      !col->GetMaterialList().HasMaterial(kMT_ProjectilePassthrough)) {
    // Retail copies the CDamageInfo at projectile+0x19c (CWeapon::mCurDamageInfo).
    CDamageInfo info = proj->GetCurrentDamageInfo();
    bool canHit = CanTakeHits();
    const bool weak = IsJointOfType(mgr, col, 1);
    float dmg = info.GetDamage(*GetDamageVulnerability());
    const float origDmg = info.GetDamage();
    if (weak) {
      info.SetDamage(info.GetDamage() * x7c0_data.weakSpotDamageMultiplier);
      dmg = info.GetDamage(xd18_weakVuln);
      info = CDamageInfo(CWeaponMode(kWT_UnknownSource), dmg, 0.f, 0.f, false, false);
      if (xb14_state == 3) {
        canHit = true;
        // Retail fn_78_9B74 0xA06C: the threshold is guest 0x82C (unknown_0xee6b6f47; 0x828 is the
        // light-hit threshold, fn_78_6590 0x65B8), compared with the original damage.
        if (x7c0_data.unknown_0xee6b6f47 <= origDmg) {
          xd14_heavyHit = true;
          const float extra = x7c0_data.unknown_0x3ce96c9d; // 0xA09C: guest 0x830
          CDamageInfo heavy(CWeaponMode(kWT_UnknownSource), extra, 0.f, 0.f, false, false);
          heavy.SetRadiusDamage(extra);
          mgr.ApplyDamage(lastTouched, GetUniqueId(), lastTouched, heavy, SolidFilter(),
                          CVector3f::Zero());
          dmg += extra;
        }
      }
    }
    info.SetRadius(0.f);
    info.SetRadiusDamage(0.f);

    if (dmg > 0.f) {
      mDamageCooldownTimer = skDamageHitTime;
      const CVector3f dir = -proj->GetTransform().GetForward();
      const CVector3f& pos = proj->GetTranslation();
      const SLdrAudioPlaybackParms* extraSound = nullptr;
      if (xd14_heavyHit) {
        PlayCustomSound(pos, dir, x7c0_data.sounds.painSound_OneShot, false);
        extraSound = &x7c0_data.sounds.weakSpotHitLarge_OneShot;
      } else if (weak) {
        PlayCustomSound(pos, dir, x7c0_data.sounds.painSound_OneShot, false);
        extraSound = &x7c0_data.sounds.weakSpotHitSmall_OneShot;
      } else {
        switch (xb14_state) {
        case -1:
        case 0:
        case 1:
        case 2:
        case 4:
        case 5:
        case 9:
          xb3c_painAccum += dmg;
          break;
        default:
          break;
        }
        if (xb3c_painAccum > x7c0_data.sounds.painSoundDamageThreshold) {
          extraSound = &x7c0_data.sounds.painSound_OneShot;
          xb3c_painAccum = 0.f;
        }
      }
      if (extraSound) {
        PlayCustomSound(pos, dir, *extraSound, false);
      }
    }

    // Outside the vulnerable states the larva never drops below 1 HP.
    if (!canHit && GetHealthInfo()->GetHP() - dmg < 1.f) {
      info.SetDamage(rstl::max_val(0.f, GetHealthInfo()->GetHP() - 1.f));
    }
    mgr.ApplyDamage(lastTouched, GetUniqueId(), lastTouched, info, SolidFilter(),
                    CVector3f::Zero());

    switch (xb14_state) {
    case 0:
    case 4:
    case 5:
    case 9:
    case 10:
      xd10_bellyAccum += dmg;
      // The cap is half the *min health* threshold: the retail reads it from 0x824
      // (`lfs f1, 0x824(r31)` at 0xA3C0 and `fmuls f1, f2, f1` at 0xA3CC with the 0.5 of
      // lbl_78_rodata_1E0), which is the field the ShouldExposeBelly trigger compares against at
      // min health - not the 50.0 threshold.
      if (xc00_tongues < 1 && xd10_bellyAccum > x7c0_data.unknown_0x74e1a041 * 0.5f) {
        xd10_bellyAccum = x7c0_data.unknown_0x74e1a041 * 0.5f;
      }
      break;
    default:
      break;
    }
  }
  col->HealthInfo()->SetHP(kCollisionActorHP);
  return true;
}

// @ fn_78_9B74
void CSwampBossStage1::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  switch (msg.GetMessage()) {
  case kSM_Create:
    BodyController()->Activate(mgr, pas::kAS_Invalid);
    break;
  case kSM_AreaLoaded: {
    xb8c_nextAttackTime = xb1c_clock;
    if (mgr.IsRandomAvailable()) {
      const SLdrSwampBossStage1Struct& phase = CurrentPhase();
      xb8c_nextAttackTime +=
          mgr.Random()->Range(phase.minTimeBetweenAttacks, phase.maxTimeBetweenAttacks);
    }
    xb7c_nextBobTime = xb1c_clock + mgr.Random()->Range(x7c0_data.unknown_0x27a06f6a,
                                                        x7c0_data.unknown_0x233a5e40);
    CPlayer* player = mgr.GetPlayer(0);
    xd58_savedStepUp = player->GetStepUpHeight();
    player->SetStepUpHeight(0.5f);
    xd64_water = FindConnectedObject(mgr, kSS_InternalState8, kSM_None);
    AddMaterial(kMT_ProjectilePassthrough, kMT_ExcludeFromRadar, mgr);
    RemoveMaterial(kMT_Solid, kMT_CollisionActor, kMT_Player, mgr);
    RemoveMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
    break;
  }
  case kSM_Deactivate:
    if (!xb80_collision.null()) {
      xb80_collision->SetActive(mgr, false);
    }
    break;
  case kSM_Activate:
    if (!xb80_collision.null()) {
      xb80_collision->SetActive(mgr, true);
    }
    // Retail passes *GetHealthInfo(), i.e. the initial HP field at +0.
    mgr.SetBossParams(GetUniqueId(), GetHealthInfo()->GetInitialHP(),
                      gpStringTable->GetStringIndex("BossSwampBossStage1"));
    break;
  case kSM_AIUpdateDisabled:
    if (!xb80_collision.null()) {
      xb80_collision->SetPhysicsActive(mgr, false);
    }
    break;
  case kSM_Delete:
    if (!xb80_collision.null()) {
      xb80_collision->Destroy(mgr);
      xb80_collision = nullptr;
    }
    break;
  case kSM_HitObject: {
    // Contact damage when the player touches a joint sphere.
    const CCollisionActor* col =
        TCastToConstPtr< CCollisionActor >(mgr.ObjectById(msg.GetSenderId()));
    if (col && col->GetLastTouchedObject() == mgr.GetPlayer(0)->GetUniqueId() &&
        mCurDamageRemTime <= 0.f) {
      mgr.ApplyDamage(GetUniqueId(), mgr.GetPlayer(0)->GetUniqueId(), GetUniqueId(),
                      GetContactDamage(), SolidFilter(), CVector3f::Zero());
      mCurDamageRemTime = mDamageWaitTime;
    }
    break;
  }
  case kSM_Damage:
    if (!ApplyDamageMsg(mgr, msg)) {
      return;
    }
    break;
  default:
    break;
  }
  CPatterned::AcceptScriptMsg(mgr, msg);
}

// @ fn_78_69F8
void CSwampBossStage1::PreRender(CStateManager& mgr) {
  CPatterned::PreRender(mgr);
  xb20_rootPos = GetLctrTransform(rstl::string_l("Skeleton_Root")).GetTranslation();
  xc0c_tonguePos = GetLctrTransform(rstl::string_l("tongue_LCTR_SDK")).GetTranslation();
  xb2c_headPos = GetLctrTransform(rstl::string_l("head")).GetTranslation();
  const CVector3f spine = GetLctrTransform(rstl::string_l("Spine_5")).GetTranslation();
  xd68_headOffset = xb2c_headPos - spine;
}

// @ fn_78_2204
void CSwampBossStage1::AddToRenderer(const CStateManager& mgr) const {
  CPatterned::AddToRenderer(mgr);
  if (!GetAlive()) {
    return;
  }
  // TODO(uncertain): retail queues both generators through IRenderer slot +0x44 (the overload
  // with position and bounds, whose arguments the decompiler dropped); the host uses the
  // one-argument overload.
  if (!xd60_ringGen.null()) {
    gpRender->AddParticleGen(*xd60_ringGen);
  }
  if (!xbc4_telegraphGen.null()) {
    gpRender->AddParticleGen(*xbc4_telegraphGen);
  }
  if (xb14_state == 9 || xb14_state == 5) {
    EnsureRendered(mgr);
  }
}

// The tongue is 256 copies of the tongue model strung along the line from the mouth towards the
// tip, swaying on two sine waves that fade out towards the tip.
// @ fn_78_6D08
void CSwampBossStage1::RenderTongue(const CStateManager& mgr) const {
  const CColor cA = CColor::White();
  const CColor cB = CColor::Lerp(CColor::White(), CColor::Red(), 0.8f);
  const float extend = rstl::max_val(0.01f, xc30_extend);
  const CVector3f d = (xc0c_tonguePos * (1.f - extend) + xc18_tongueTarget * extend) - xc0c_tonguePos;
  const CTransform4f look = CTransform4f::LookAt(CVector3f::Zero(), d, CVector3f::Up());
  const float len = rstl::min_val(d.Magnitude(), 100.f);
  const float t = xb18_time;
  for (int i = 0; i < 256; ++i) {
    const float f = static_cast< float >(i) - 0.5f;
    const float u = f / 256.f;
    const float amp = (1.f - u) * 0.4f;
    const float ph = t * 2.f * 6.2831855f + (f * 0.18f - 46.08f) * 0.8f;
    const float s = CMath::FastSinR(ph);
    const float c = CMath::FastSinR(ph + 1.f);
    const CVector3f local(xc40_tongueAmp * amp * s, len * u, xc40_tongueAmp * amp * c * 0.2f);
    CTransform4f xf = GetTransform();
    xf.SetRotation(look.GetRotation());
    xf.SetTranslation(xc0c_tonguePos + look * local);
    const float a = CMath::Clamp(0.f, 1.f - (1.f - u) * (1.f - u), 1.f);
    CColor col = CColor::Lerp(cA, cB, a);
    col.SetAlpha(a);
    if (u < 0.4f) {
      const float k = 1.f - u / 0.4f;
      xf.ScaleBy(k * k * 4.f + 1.f);
    }
    xc44_tongueModel->Render(mgr, xf, GetActorLights(),
                             CModelFlags(static_cast< CModelFlags::ETrans >(5), 0,
                                         static_cast< CModelFlags::EFlags >(3), col));
  }
}

// @ fn_78_7AE4
void CSwampBossStage1::Render(const CStateManager& mgr) const {
  CPatterned::Render(mgr);
  if (xc44_tongueModel && xc34_tongueState != kTS_Idle) {
    RenderTongue(mgr);
  }
  if (xc94_tipModel && xc30_extend > 0.f) {
    const CVector3f d = xc24_tongueTip - xc0c_tonguePos;
    if (d.CanBeNormalized()) {
      CTransform4f xf = GetTransform();
      xf.SetRotation(CTransform4f::LookAt(CVector3f::Zero(), d.AsNormalized(), CVector3f::Up()));
      xf.SetTranslation(xc24_tongueTip);
      xc94_tipModel->Render(mgr, xf, GetActorLights(), GetModelFlags());
    }
  }
}

// @ fn_78_7890: the sort bounds are the joint spheres (shrunk to 85%) that are not flagged out.
void CSwampBossStage1::PreRenderAllViewports(CStateManager& mgr) {
  CPatterned::PreRenderAllViewports(mgr);
  if (xb80_collision.null()) {
    return;
  }
  xb40_sortBounds = CAABox::MakeMaxInvertedBox();
  for (int i = 0; i < ARRAY_SIZE(kJoints); ++i) {
    if (kJoints[i].mFlags & 4) {
      continue;
    }
    const CCollisionActor* act = JointActor(mgr, i);
    if (!act) {
      continue;
    }
    const rstl::optional_object< CAABox > box = act->GetTouchBounds();
    if (!box) {
      continue;
    }
    const CVector3f c = box->GetCenterPoint();
    const CVector3f h = (box->GetMaxPoint() - c) * 0.85f;
    xb40_sortBounds.AccumulateBounds(c - h);
    xb40_sortBounds.AccumulateBounds(c + h);
  }
}

// @ fn_78_8
rstl::optional_object< CAABox > CSwampBossStage1::GetTouchBounds() const {
  return GetBoundingBox();
}

// @ fn_78_2100: the module's own orbit position - the head's locator, not the base class's
// (CPatterned::GetOrbitPosition returns the aim position, CPatterned.cpp:1183). The retail builds
// the "head" string from its rodata (0x4953's "head" at +0x24, 0x211C) and calls GetLctrTransform
// with it (0x213C).
CVector3f CSwampBossStage1::GetOrbitPosition(const CStateManager& mgr) const {
  return GetLctrTransform(rstl::string_l("head")).GetTranslation();
}

// @ fn_78_21C8: the scan change - when the scan finishes (kSS_Done, 2) the scanned flag goes up,
// and the base class still runs. The retail: `cmpwi r4, 0x2` (0x21D0), `rlwimi r0, r6, 7, 24, 24`
// (0x21E8, the flag's bit 7 of the byte at 0xb58) and the base call at 0x21F0.
void CSwampBossStage1::OnScanStateChange(EScanState state, CStateManager& mgr) {
  if (state == kSS_Done) {
    xb58_scanned = true;
  }
  CActor::OnScanStateChange(state, mgr);
}

// @ fn_78_785C
CAABox CSwampBossStage1::GetSortingBounds(const CStateManager& mgr) const {
  return xb40_sortBounds;
}

// @ fn_78_86E8: animation user events. Retail returns without the base call for 0x08, 0x16, 0x1b
// and 0x1c.
void CSwampBossStage1::DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node,
                                       EUserEventType type, float dt) {
  switch (static_cast< int >(type)) {
  case 0x00:
    if (xb14_state == 7) {
      Spit(mgr);
    }
    break;
  case 0x08:
    if (xb14_state == 10) {
      VomitHit(mgr);
    }
    return;
  case 0x0c:
    if (xb14_state == 7) {
      LaunchShockWave(mgr);
    }
    break;
  case 0x11:
    if (xb14_state == 0) {
      SetPlatformTarget(mgr);
    }
    break;
  case 0x12:
    ResetPlatformConnection(mgr);
    break;
  case 0x16:
    SetJointsActive(mgr, kAllJoints, kJF_RemoveTarget | kJF_Reflect);
    if (xb14_state == 7) {
      SetJointsActive(mgr, 2, kJF_RemoveRadar);
    }
    return;
  case 0x1b:
    if (xb14_state == 9 && xc34_tongueState == kTS_Idle) {
      LaunchTongue(mgr);
    }
    return;
  case 0x1c:
    StopTongue();
    return;
  case 0x21:
    if (GetAlive()) {
      xd66_ringOn = true;
    }
    break;
  case 0x22:
    xd66_ringOn = false;
    break;
  default:
    break;
  }
  CPatterned::DoUserAnimEvent(mgr, node, type, dt);
}

// @ fn_78_A570
CScannableObjectInfo* CSwampBossStage1::GetScannableObjectInfo() const {
  // TODO(uncertain): retail hides the scan while body-controller state id 7 is current (the
  // field at +0x580 of the object at this+0x48c, taken as the body state id).
  if (static_cast< int >(GetBodyController()->GetCurrentStateId()) == 7 || !xb58_radarPresent) {
    return nullptr;
  }
  return CPatterned::GetScannableObjectInfo();
}

// @ fn_78_9A44 (CollidedWith): the retail's own work runs only while the state is 5
// (TonguePull): it walks the collision list and sets the "player touched" flag when the list's
// 0x30 slot has u64 bit 32 = kMT_Player (the u64 is big-word-first, so bit 0 of that word is
// bit 32). The state id is 5 only while the boss pulls its tongue, and the list is in the
// mover's frame, so the flag means "the boss's body hit the player while it pulled".
void CSwampBossStage1::CollidedWith(const TUniqueId& id, const CCollisionInfoList& list,
                                    CStateManager& mgr) {
  if (xb14_state == 5) {
    for (int i = 0; i < list.GetCount(); ++i) {
      // The retail keeps the material u64 big-word-first: the word at the list's base+0 holds
      // the u64's bits 63..32 and the word at base+4 holds bits 31..0 (DOL
      // `CActor::AddMaterial` 0x8004C340 shifts 1<<material and ORs the shift's high half
      // into the word at the list base+0, the low half into base+4). So bit 0 of the word at
      // collision-info+0x30 is u64 bit 32 = kMT_Player, not bit 63 = kMT_SeekerTarget.
      // The retail masks it with `li r8, 0x1` (0x9A70), `lwz r0, 0x30(r10)` (0x9A78) and
      // `and r0, r0, r8` (0x9A80).
      if (list[i].GetMaterialLeft().HasMaterial(kMT_Player)) {
        xce4_playerTouched = true;
      }
    }
  }
  CPatterned::CollidedWith(id, list, mgr);
}

// @ fn_78_2078
void CSwampBossStage1::TakeDamage(const CVector3f& dir, float amount) {
  mDamageCooldownTimer = skDamageHitTime;
  if (!CanTakeHits() && GetHealthInfo()->GetHP() < 1.f) {
    HealthInfo()->SetHP(1.f);
  }
}

// --------------------------------------------------------------------------------------------
// State machine

#define SB1_TRIGGER(name)                                                                       \
  { #name, reinterpret_cast< CPatterned::StateMachine::TriggerFunc >(&CSwampBossStage1::name) }
#define SB1_STATE(name)                                                                         \
  { #name, reinterpret_cast< CPatterned::StateMachine::StateFunc >(&CSwampBossStage1::name) }
#define SB1_CODE(name)                                                                          \
  { #name, reinterpret_cast< CPatterned::StateMachine::CodeFunc >(&CSwampBossStage1::name) }

namespace {
// The module's static initialiser fn_78_BD0C builds these three tables: `tools/fsm_ctor_tables.py
// <asm dir> fn_78_BD0C lbl_78_data_D0:17 lbl_78_data_30C:25 lbl_78_data_4CC:4` replays it (164
// stores, every entry {0, 0xFFFFFFFF, fn}) and the names and functions match in order; MinHealth
// and SplashWait are adapters over the marked fn_78_6548 / fn_78_4D3C bodies.
// @ fn_78_BD0C
CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    SB1_TRIGGER(BeachAttackOver), SB1_TRIGGER(EnoughSplashing),   SB1_TRIGGER(GivingUp),
    SB1_TRIGGER(HeavyHit),        SB1_TRIGGER(InVomitRange),      SB1_TRIGGER(LightHit),
    SB1_TRIGGER(MinHealth),       SB1_TRIGGER(ReadyToTongue),     SB1_TRIGGER(ShouldBeach),
    SB1_TRIGGER(ShouldBob),       SB1_TRIGGER(ShouldExposeBelly), SB1_TRIGGER(ShouldSplash),
    SB1_TRIGGER(SplashAttackOver), SB1_TRIGGER(TongueHit),        SB1_TRIGGER(TongueMissed),
    SB1_TRIGGER(TooMuchPulling),  SB1_TRIGGER(Wait),
};
CPatterned::StateMachine::SStateFunction skStates[] = {
    SB1_STATE(Beach),          SB1_STATE(BeachBubbles),    SB1_STATE(BeachRise),
    SB1_STATE(BeachWait),      SB1_STATE(Bob),             SB1_STATE(Dead),
    SB1_STATE(Dive),           SB1_STATE(ExposeBelly),     SB1_STATE(HeavyHitReact),
    SB1_STATE(LightHitReact),  SB1_STATE(Null),            SB1_STATE(PlatformReady),
    SB1_STATE(ResetPlatform),  SB1_STATE(SetSplashOver),   SB1_STATE(SlideIntoWater),
    SB1_STATE(SplashDescend),  SB1_STATE(SplashJump),      SB1_STATE(SplashRise),
    SB1_STATE(SplashTelegraph), SB1_STATE(SplashWait),     SB1_STATE(StopBeaching),
    SB1_STATE(Swim),           SB1_STATE(TongueLoop),      SB1_STATE(TonguePull),
    SB1_STATE(Vomit),
};
CPatterned::StateMachine::SCodeFunction skCodes[] = {
    SB1_CODE(IncrementMisses),
    SB1_CODE(SetSplashRotation),
    SB1_CODE(ShreddersOn),
    SB1_CODE(ShreddersOff),
};
} // namespace

// @ fn_78_6864
void CSwampBossStage1::SetupStateMachine(CStateManager& mgr) {
  StateMachine* stateMachine = mStateMachine.get();
  CPatterned::SetupStateMachine(mgr);
  stateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  stateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  stateMachine->SetCodeFunctions(skCodes, ARRAY_SIZE(skCodes));
}

// --------------------------------------------------------------------------------------------
// Loader

// Retail fn_78_D50: SLdrSwampBossStage1Data's constructor, called by the loader at 0x0000019C on
// its stack object. Its `li r0,-0x1` at 0x00000DB0 feeds two stores: 0x1e8 (0x00000DB8), the
// spitVisorEffect CAssetId the generated header already has as kInvalidAssetId, and 0x1ec
// (0x00000DBC), sound_SpitVisor, which is a sound id - the guest's default is -1 (no sound) and
// the header stores 0. SLdrSwampBossStage1Data's layout is the guest's (no editor-properties
// padding in the sub-struct), so 0x1ec is the port offset too. The .inc reader below overwrites
// it when the instance's data carries the property.
//   fn_78_D50  li r0,-0x1 0x00000DB0; stw r0,0x1ec(r31) 0x00000DBC
static void x78D50_SoundDefaults(SLdrSwampBossStage1& sldr) {
  sldr.swampBossStage1Properties.sound_SpitVisor = -1; // 0x00000DBC (field 0x1ec)
}

// @ fn_78_160
static CEntity* PortLoadSwampBossStage1(CStateManager& mgr, CInputStream& input,
                                        CEntityInfo& info) {
  SLdrSwampBossStage1 sldrThis;
  x78D50_SoundDefaults(sldrThis); // fn_78_D50's -1 sound id (the call at 0x0000019C)
#include "MetroidPrime/ScriptLoader/SLdrSwampBossStage1.inc"
  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }
  return rs_new CSwampBossStage1(
      mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
      LdrToEntityInfo(info, sldrThis.editorProperties), LdrToTransform4f(sldrThis.editorProperties),
      *modelData, LdrToActorParameters(sldrThis.actorInformation),
      LdrToPatternedInfo(sldrThis.patterned, nullptr), sldrThis.swampBossStage1Properties);
}

static SSwampBossStage1_FuncPtrs REL_loader_SwampBossStage1;

void RELMain() {
  REL_loader_SwampBossStage1.mLoader = PortLoadSwampBossStage1;
  SetSSwampBossStage1_FuncPtrs(&REL_loader_SwampBossStage1);
}

void RELExit() { SetSSwampBossStage1_FuncPtrs(nullptr); }
