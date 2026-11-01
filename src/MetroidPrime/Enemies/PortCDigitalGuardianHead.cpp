// Port-only: the DigitalGuardian REL's head module (module 14, CDigitalGuardianHead, script type
// DGHD, docs/module-analysis/DigitalGuardian.md sections b.5-b.7, c.3 and f.2). The retail class is
// 0x10F8 bytes, built by fn_14_18264. This slice is the loader, the constructor, the link to the
// body ("XALD", fn_14_13830), Think, AcceptScriptMsg, the state machine's tables, the 20 triggers
// and the two smallest states; the other FSM functions and the module callees they reach are
// once-logging stand-ins named after the retail function. The comments cite the REL's .text
// offsets.

#include "MetroidPrime/Enemies/CDigitalGuardianHead.hpp"

#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "MetroidPrime/Factories/CScannableObjectInfo.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CEchoEmitter.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/CObjectList.hpp"
#include "MetroidPrime/Player/CMorphBall.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Weapons/CBeamInfo.hpp"
#include "MetroidPrime/Weapons/CBomb.hpp"
#include "MetroidPrime/Weapons/CEnergyProjectile.hpp"
#include "MetroidPrime/Weapons/CImpactVisorEffect.hpp"
#include "MetroidPrime/Weapons/CPlasmaProjectile.hpp"
#include "MetroidPrime/Weapons/CWeaponAssetInfo.hpp"
#include "MetroidPrime/Weapons/CWeapon.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CValidEntityPredicate.hpp"
#include "MetroidPrime/Enemies/CDigitalGuardian.hpp"
#include "MetroidPrime/Enemies/CDigitalGuardianEchoTarget.hpp"
#include "MetroidPrime/Enemies/CPatternedInfo.hpp"
#include "MetroidPrime/Enemies/CSplitterCommandModule.hpp"
#include "MetroidPrime/Enemies/CSplitterMainChassis.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CScriptPlatform.hpp"
#include "MetroidPrime/ScriptObjects/CScriptPlayerHint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWaypoint.hpp"
#include "MetroidPrime/TCastTo.hpp"

#include "port_console.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

namespace {

// The head's render flag (lbl_14_bss_18): the head's render (fn_14_17B28) sets it while it
// renders, and its RenderSystemsToBeDrawnLast (fn_14_17A64) only draws while it is set. It is
// separate from the body's flag (lbl_14_bss_0).
uchar sHeadRendering = 0;

// The retail's GenerateParticlePOIMask (the call at 0x17B6C) is the host's
// CStateManager::GetCharacterRenderMaskAndTarget (as in PortCDigitalGuardian.cpp).
void GenerateParticlePOIMask(const CStateManager& mgr, uint& mask, uint& target) {
  mgr.GetCharacterRenderMaskAndTarget(mask, target);
}

// Reports a stand-in the first time it runs, so a log lists what the head still lacks. The
// message names the retail function the entry stands in for.
void StandInFn(const char* fn) {
  static const char* sSeen[128] = {};
  static int sCount = 0;
  for (int i = 0; i < sCount; ++i) {
    if (sSeen[i] == fn) {
      return;
    }
  }
  if (sCount < 128) {
    sSeen[sCount++] = fn;
  }
  fprintf(stderr, "[dghd] stand-in %s\n", fn);
}

// lbl_14_data_1024: the waypoint predicate fn_14_13830 and fn_14_119E4 pass (its IsValid is a
// TCastToPtr<CScriptWaypoint> of the object).
class CWaypointPredicate : public CValidEntityPredicate {
public:
  ~CWaypointPredicate() override {}
  // The neg/or/srwi 31 at 0x1AAA8..0x1AAB0: the cast's result != null.
  // @ fn_14_1AA84
  bool IsValid(const CStateManager& mgr, TUniqueId id) const override {
    return TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(id)) != nullptr;
  }
};

// lbl_14_data_1008: the platform predicate fn_14_13830 passes (TCastToPtr<CScriptPlatform>).
class CPlatformPredicate : public CValidEntityPredicate {
public:
  ~CPlatformPredicate() override {}
  // The neg/or/srwi 31 at 0x1AA68..0x1AA70: the cast's result != null.
  // @ fn_14_1AA44
  bool IsValid(const CStateManager& mgr, TUniqueId id) const override {
    return TCastToConstPtr< CScriptPlatform >(mgr.GetObjectById(id)) != nullptr;
  }
};

#define DGHD_TRIGGER(name)                                                                         \
  { #name, reinterpret_cast< CPatterned::StateMachine::TriggerFunc >(&CDigitalGuardianHead::name) }
#define DGHD_STATE(name)                                                                           \
  { #name, reinterpret_cast< CPatterned::StateMachine::StateFunc >(&CDigitalGuardianHead::name) }
#define DGHD_CODE(name)                                                                            \
  { #name, reinterpret_cast< CPatterned::StateMachine::CodeFunc >(&CDigitalGuardianHead::name) }

// The head's three FSM tables, in retail order: lbl_14_data_808 (20 triggers), lbl_14_data_A14
// (17 states) and lbl_14_data_C80 (29 codes). The host state machine matches a node by its name.
// fn_14_1AD2C (0x9CC, the .ctors entry, no branch) builds them: it copies each three-word record
// {0, -1, fn} (.data 0x718..0x7F0 and on) into the entries' last three words (0x1AE3C..0x1B6DC),
// and also stores the echo colour bss_34 (0.5, 0, 0, 1; CColor at 0x1AD60, stw 0x1AE2C), bss_38
// (2, 2, 2; .rodata 0x968, stfs 0x1AE30..0x1AE38) and data_E50 = {"LeftBombSlotDestroyed",
// "RightBombSlotDestroyed"} (.rodata 0x944/0x948, 0x1B6CC/0x1B6E0); the port's are
// sEchoTargetHitColor, sEchoTargetHalfExtent and skSlotDestroyed. tools/drivers/dg_tables.py runs
// the initialiser over the .data dump and compares every entry's name, order and retail function
// (by the member's marker) with these arrays: 20 + 17 + 29 entries, no mismatch.
// @ fn_14_1AD2C
CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    DGHD_TRIGGER(StateOver),           DGHD_TRIGGER(MainBodyDestroyed),
    DGHD_TRIGGER(ShouldProjectileAttack), DGHD_TRIGGER(IsBurstOver),
    DGHD_TRIGGER(IsIdle),              DGHD_TRIGGER(IsStunned),
    DGHD_TRIGGER(IsGuarding),          DGHD_TRIGGER(UseMachineGun),
    DGHD_TRIGGER(UseLockOnMissiles),   DGHD_TRIGGER(UseAnnihilatorPulse),
    DGHD_TRIGGER(UseAnnihilatorCharge), DGHD_TRIGGER(AllSplittersDead),
    DGHD_TRIGGER(HeadArmorDestroyed),  DGHD_TRIGGER(IsAntennaTransmitting),
    DGHD_TRIGGER(HasLineOfSight),      DGHD_TRIGGER(EchoTargetDestroyed),
    DGHD_TRIGGER(AllEchoTargetsDestroyed), DGHD_TRIGGER(ShouldRetreat),
    DGHD_TRIGGER(BombPitDestroyed),    DGHD_TRIGGER(IsDead),
};

CPatterned::StateMachine::SStateFunction skStates[] = {
    DGHD_STATE(Start),            DGHD_STATE(Idle),
    DGHD_STATE(FaceTarget),       DGHD_STATE(ProjectileTelegraph),
    DGHD_STATE(PulseAttack),      DGHD_STATE(ChargeAttack),
    DGHD_STATE(MissileAttack),    DGHD_STATE(MachineGunAttack),
    DGHD_STATE(AttachedToBody),   DGHD_STATE(DetachFromBody),
    DGHD_STATE(StunnedReaction),  DGHD_STATE(DeploySplitters),
    DGHD_STATE(BombedReaction),   DGHD_STATE(SeekAttackPosition),
    DGHD_STATE(SeekMotionPath),   DGHD_STATE(FollowRetreatPath),
    DGHD_STATE(Dead),
};

CPatterned::StateMachine::SCodeFunction skCodes[] = {
    DGHD_CODE(SetPlayerFaceTarget),      DGHD_CODE(ResetProjectileAttackTimes),
    DGHD_CODE(SelectProjectileAttack),   DGHD_CODE(SetPlatformPosition),
    DGHD_CODE(SetStunnedPosition),       DGHD_CODE(SeekLineOfSight),
    DGHD_CODE(DropShields),              DGHD_CODE(RaiseShields),
    DGHD_CODE(ExtractAntenna),           DGHD_CODE(ExtractEchoTargets),
    DGHD_CODE(RetractEchoTargets),       DGHD_CODE(DestroyActiveSplitters),
    DGHD_CODE(StartSplitterDeployment),  DGHD_CODE(EndSplitterDeployment),
    DGHD_CODE(Stage3Start),              DGHD_CODE(EnableDynamicCollision),
    DGHD_CODE(DisableDynamicCollision),  DGHD_CODE(EnableValidTarget),
    DGHD_CODE(DisableValidTarget),       DGHD_CODE(SelectDeathHeadAttack),
    DGHD_CODE(SetInitialAttackPosition), DGHD_CODE(SetAttackPosition),
    DGHD_CODE(ResetGravityRepulsorHP),   DGHD_CODE(EjectPlayerFromBombPit),
    DGHD_CODE(GrabPlayerHintControl),    DGHD_CODE(SetVulnerableToDamage),
    DGHD_CODE(SetImmuneToDamage),        DGHD_CODE(SelectRetreatPlatform),
    DGHD_CODE(StartVocalRecoverReaction),
};

#undef DGHD_TRIGGER
#undef DGHD_STATE
#undef DGHD_CODE

} // namespace

// fn_14_18264. The base call is the retail one: EPatternedAI 0xA (li r4 at 0x182B8), flavor 0
// (li r7 at 0x182A4), movement 1 (flyer), collider 1 and body 7 (kBT_AiMovedFlyer) in the stack
// words at 0x182B0..0x182BC. The ctor's other members (the visual knowledge at 0xD80, the four
// projectile infos at 0xE04..0xE7C, the 'PART' and 'SCAN' tokens, the head armour model and the
// locator ids) belong to the states this slice does not port, so the host keeps only what the
// ported functions read, with the values the retail stores.
// Coverage: every arm natural or forced (tools/cov/specs/digitalguardian_dg28_ctor.py forces the
// PART/SCAN ids to -1); the only unrun arms are the compiler's EH-unwind edges on the closing brace.
// @ fn_14_18264
CDigitalGuardianHead::CDigitalGuardianHead(TUniqueId uid, const rstl::string& name,
                                           const CEntityInfo& info, const CTransform4f& xf,
                                           const CModelData& modelData,
                                           const CActorParameters& actParms,
                                           const CPatternedInfo& pInfo,
                                           const SLdrDigitalGuardianHeadData& data)
: CPatterned(static_cast< EPatternedAI >(0xA), uid, name, kFT_Zero, info, xf, modelData, pInfo,
             kMT_Flyer, kCT_One, kBT_AiMovedFlyer, actParms)
, mData(data)
, xD70_(nullptr)
, xDC4_{kInvalidUniqueId, kInvalidUniqueId} // 0x18358
, xDD0_(LdrToDamageVulnerability(data.bombPitVulnerability)) // 0x18370
, xE00_(kInvalidUniqueId)
, xD80_(uid, CSegId(), 0.1f, 0.05f) // 0x18334: no locator (0xFF), .rodata 0x9F4 / 0x9F8
, xEF4_()
, xF04_()
, xF14_()
, xF34_(3, kInvalidUniqueId) // 0x188A8..0x188C8
, xF40_{CTransform4f::Identity(), CTransform4f::Identity(), CTransform4f::Identity()} // 0x188CC
, xFD0_()
, xFE0_(3)
, xFE8_(1)
, xFEC_(CVector3f::Zero())
, xFF8_(CVector3f::Zero())
// 0x1004: the translation, then the transform's column 1 and the negated column 0 (fn_14_190E0).
, x1004_(GetTranslation())
, x1010_(GetTransform().GetColumn(kDY))
, x101C_(-GetTransform().GetColumn(kDX))
, x1028_(CVector3f::Zero())
, x1034_(kInvalidUniqueId)
, x1036_(kInvalidUniqueId)
, x1038_(kInvalidUniqueId)
, x103A_(kInvalidUniqueId)
, x103C_(kInvalidUniqueId)
, x103E_(kInvalidUniqueId)
, x1040_(kInvalidUniqueId)
, x1044_()
, x10D0_(-1)
, x10D4_(0.f)
, x10D8_(0)
, x10DC_(-1)
, x10E0_(data.gravityRepulsorHP) // data +0x18
, x10E4_(2)
, x10E8_(0)
, x10EC_(0.f)
, x10F0_(0.f)
, x10F4_(0x08) // only bit 0x08 is set (the rlwimi stores at the ctor's tail)
, x10F5_(0)    // bit 0x80 cleared
// 0x18B98..0x18BA8: the lock-on beams' weapon (data +0x1BC) with a default damage (fn_14_FFFC:
// weapon -1, every amount 0.0f, the sounds 0xFFFF); 0x18BB4..0x18C00: no lasers, no locator
// yet (0xFF) and the sweep's floats 0.0f (.rodata 0x960) and its point the zero vector.
, x1054_(data.lockOnBeams.weaponSystem, CDamageInfo())
, x107C_()
, x1088_()
, x108C_(0.f)
, x1090_(0.f)
, x1094_(0.f)
, x1098_(CVector3f::Zero())
// 0x183A4..0x18408: the four projectile infos, each the data's weapon with its damage
// (LdrToDamageInfo); their tokens are locked below (0x18CD8..0x18CF0).
, xE04_(data.annihilatorPulse, LdrToDamageInfo(data.annihilatorPulseDamage))
, xE2C_(data.annihilatorCharge, LdrToDamageInfo(data.annihilatorChargeDamage))
, xE54_(data.lockOnMissiles, LdrToDamageInfo(data.lockOnMissilesDamage))
, xE7C_(data.machineGun, LdrToDamageInfo(data.machineGunDamage))
// 0x18C04..0x18C44: the gun's locator 0xFF until set below, both sweep ends the zero vector,
// side 0 (r8), phase -1 (r5), both times 0.0f (f1, .rodata 0x960).
, x10A4_()
, x10A8_(CVector3f::Zero())
, x10B4_(CVector3f::Zero())
, x10C0_(0)
, x10C4_(-1)
, x10C8_(0.f)
, x10CC_(0.f)
, xFE4_(mTurnSpeed) // 0x18A8C..0x18A9C: the CPatternedInfo turn speed (pInfo +0x8)
{
  xE04_.Token().Lock(); // 0x18CD8
  xE2C_.Token().Lock(); // 0x18CE0
  xE54_.Token().Lock(); // 0x18CE8
  xE7C_.Token().Lock(); // 0x18CF0
  KnockBackController().EnableAllAnimReactions(false); // 0x18CF4..0x18CFC: this +0x620, li r4,0
  // The drop-shields effect (data +0x48, 0x184D8): locked when set.
  if (mData.dropShieldsFx != kInvalidAssetId) {
    xEF4_ = TLockedToken< CGenDescription >(
        gpSimplePool->GetObj(SObjectTag('PART', mData.dropShieldsFx)));
  }
  // The 'PART' token (0x185A0..0x18654): locked when data +0x94 is set.
  if (mData.pART != kInvalidAssetId) {
    xF04_ = TLockedToken< CGenDescription >(gpSimplePool->GetObj(SObjectTag('PART', mData.pART)));
  }
  // The bomb-slot-destroyed effect (data +0x138, 0x18708) and the echo-target-destroyed one
  // (data +0xA4, 0x18900), each locked when set.
  if (mData.bombSlotDestroyedFx != kInvalidAssetId) {
    xF14_ = TLockedToken< CGenDescription >(
        gpSimplePool->GetObj(SObjectTag('PART', mData.bombSlotDestroyedFx)));
  }
  if (mData.echoTargetDestroyedFx != kInvalidAssetId) {
    xFD0_ = TLockedToken< CGenDescription >(
        gpSimplePool->GetObj(SObjectTag('PART', mData.echoTargetDestroyedFx)));
  }
  // 0x18D28 / 0x18D5C: the balls' locators (.rodata 0x954's names) at 0xDCC / 0xDCD.
  const CAnimData* anim = GetModelData()->GetAnimationData();
  xDCC_[0] = anim->GetLocatorSegId(rstl::string_l("L_ball_LCTR"));
  xDCC_[1] = anim->GetLocatorSegId(rstl::string_l("R_ball_LCTR"));
  // 0x18D70..0x18D9C: the machine gun's locator, the same name (.rodata 0x94C), at 0x10A4.
  x10A4_ = anim->GetLocatorSegId(rstl::string_l("T_lazor_LCTR"));
  // 0x18DA4..0x18DD0: the lasers' locator (.rodata 0x94C, "T_lazor_LCTR") at 0x1088.
  x1088_ = anim->GetLocatorSegId(rstl::string_l("T_lazor_LCTR"));
  // 0x18DD8..0x18DE8: the visual knowledge's locator (0xD82, lbz 0x1088 / stb 0xd82) becomes 0x1088.
  xD80_.SetLocator(x1088_);
  // 0x18DEC..0x18E5C: its filter (0xD88..0xD98) is rebuilt inline: include 1 << .data 0xE58
  // (0x3B, kMT_Solid; stfd 0xd88), exclude 1 << 0xE5C | 1 << 0xE60 (0x21 kMT_Character, 0x20
  // kMT_Player; stfd 0xd90), type 3 (li r0,0x3; stw 0xd98: kFT_IncludeExclude).
  xD80_.SetMaterialFilter(CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid), CMaterialList(kMT_Character, kMT_Player)));
  // fn_14_137A8 (0x88 bytes): the head's armour model (the retail's constructor calls it
  // at 0x18E60).
  x137A8_();
  mDamageColor = CPatterned::skHitsWithoutDamageColor; // 0x450, stored at 0x18E64..0x18E6C

  // The three 'SCAN' tokens (0x18E70..0x190B4): each id tested against -1 (addis/cmplwi 0xffff),
  // a 0xC-byte TCachedToken (operator new at 0x18E98) cached at once (GetObj at 0x18EF0).
  if (data.scannableInfoShieldHead != kInvalidAssetId) { // data +0, 0x18E70
    xD74_ = rs_new TCachedToken< CScannableObjectInfo >(
        gpSimplePool->GetObj(SObjectTag('SCAN', data.scannableInfoShieldHead)), true);
  }
  if (data.scannableInfoStunnedHead != kInvalidAssetId) { // data +4, 0x18F38
    xD78_ = rs_new TCachedToken< CScannableObjectInfo >(
        gpSimplePool->GetObj(SObjectTag('SCAN', data.scannableInfoStunnedHead)), true);
  }
  if (data.scannableInfoFinalHead != kInvalidAssetId) { // data +8, 0x19000
    xD7C_ = rs_new TCachedToken< CScannableObjectInfo >(
        gpSimplePool->GetObj(SObjectTag('SCAN', data.scannableInfoFinalHead)), true);
  }
}

// fn_14_17F64: the collision actor manager (0xD70) is deleted (the null test addic. at 0x18208,
// the dtor with r4 = 1 at 0x18218); the rest is the members' own dtors.
// @ fn_14_17F64
CDigitalGuardianHead::~CDigitalGuardianHead() { delete xD70_; }

// The body controller is activated by "XCRT" in retail; the port also activates it before the
// first Think, which can run first (docs/GOTCHAS.md, patterned actors).
void CDigitalGuardianHead::EnsureBodyActive(CStateManager& mgr) {
  if (!BodyController()->GetIsActive()) {
    BodyController()->Activate(mgr, pas::kAS_Invalid);
  }
}

// fn_14_17C98: only while active. The base's Think, then the collision actor manager's update
// (object space, the li r5, 0 at 0x17CD8), the visual knowledge's update (0xD80, 0x17CEC), the
// timers (fn_14_13AE0), fn_14_12818 and fn_14_1223C. The manager is not null-tested (0x17CCC):
// XCRT, which CStateManager::AddObject delivers before any Think, builds it (fn_14_13318).
// @ fn_14_17C98
void CDigitalGuardianHead::Think(float dt, CStateManager& mgr) {
  if (!GetActive()) {
    return;
  }
  EnsureBodyActive(mgr);
  CPatterned::Think(dt, mgr);
  xD70_->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace); // built on XCRT (fn_14_13318)
  xD80_.Update(mgr, dt);
  x13AE0_(dt);
  x12818_(mgr, dt);
  x1223C_(mgr);
}

// fn_14_13AE0: the projectile attack timer counts down, the two timers at 0x10EC and 0x10F0 up.
// @ fn_14_13AE0
void CDigitalGuardianHead::x13AE0_(float dt) {
  x10D4_ -= dt;
  x10EC_ += dt;
  x10F0_ += dt;
}

namespace {
// .rodata 0x928: the three spheres (name, radius); .rodata 0x9D4 the mass (1000).
struct SHeadSphereJoint {
  const char* name;
  float radius;
};
const SHeadSphereJoint skHeadSphereJoints[3] = {
    {"L_ball_LCTR", 1.5f},
    {"R_ball_LCTR", 1.5f},
    {"Skeleton_Root", 6.f},
};
const float kHeadJointMass = 1000.f;
// .rodata 0x954: the ball names fn_14_13284 compares.
const char* const skHeadBallJoints[2] = {"L_ball_LCTR", "R_ball_LCTR"};
} // namespace

// @ fn_14_13284
// The loop at 0x132AC: the name against the two ball names (fn_14_21A0, operator==); the id goes
// to 0xDC4 + 2 * index (0x132E8).
bool CDigitalGuardianHead::x13284_(const rstl::string& name, const TUniqueId& id) {
  for (int i = 0; i < 2; ++i) {
    if (name == rstl::string_l(skHeadBallJoints[i])) {
      xDC4_[i] = id;
      return true;
    }
  }
  return false;
}

// @ fn_14_13318
// The three spheres (the loop at 0x13378, room for 3 reserved at 0x13348), built active (li r8, 1
// at 0x1345C); every collider (the loop at 0x13490) gets the include list's high word | 0x4000000
// (material 58) first, then a ball the pass-through vulnerability and the exclude bits of .data
// 0xE64..0xE74 (33, 47, 49, 60, 32), the root the immune-ricochet one and the bits of 0xE78..0xE84
// (33, 47, 49, 60) and its id at 0xE00; then the health (HealthInfo +4, 0x136A8) is .rodata 0x96C
// (1, the HealthInfo slot at 0x13690). The head's own filter (0x136C0): include .data 0xE88 (59),
// exclude 0xE8C..0xE9C (33, 32, 47, 49, 60), type 3. __shl2i(0, 1, n) is the 64-bit 1 << n.
void CDigitalGuardianHead::x13318_(CStateManager& mgr) {
  rstl::vector< CJointCollisionDescription > joints;
  joints.reserve(3); // fn_14_D7DC
  const CAnimData* anim = GetModelData()->GetAnimationData();
  for (uint i = 0; i < 3; ++i) {
    const SHeadSphereJoint& joint = skHeadSphereJoints[i];
    const CSegId seg = anim->GetLocatorSegId(rstl::string_l(joint.name));
    // fn_14_5FA4 (the description's copy)
    // fn_14_5F24 (push_back)
    joints.push_back(CJointCollisionDescription::SphereCollision(
        seg, CVector3f::Zero(), joint.radius, rstl::string_l(joint.name), kHeadJointMass));
  }
  CCollisionActorManager* manager =
      rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), joints, true);
  delete xD70_;
  xD70_ = manager;

  for (uint i = 0; i < xD70_->GetNumCollisionActors(); ++i) {
    const CJointCollisionDescription& desc = xD70_->GetCollisionDescFromIndex(i);
    const TUniqueId id = desc.GetCollisionActorId();
    CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id));
    if (actor == nullptr) { // the TCastToPtr test at 0x134BC
      continue;
    }
    CMaterialFilter filter = actor->GetMaterialFilter();
    filter.IncludeList().Add(kMT_NoPlayerCollision);
    if (x13284_(desc.GetName(), id)) {
      actor->SetDamageVulnerability(CDamageVulnerability::PassThroughVulnerabilty());
      filter.ExcludeList().Add(kMT_Character);
      filter.ExcludeList().Add(kMT_CollisionActor);
      filter.ExcludeList().Add(kMT_Platform);
      filter.ExcludeList().Add(kMT_NoPlatformCollision);
      filter.ExcludeList().Add(kMT_Player);
    } else {
      actor->SetDamageVulnerability(CDamageVulnerability::ReflectVulnerabilty());
      filter.ExcludeList().Add(kMT_Character);
      filter.ExcludeList().Add(kMT_CollisionActor);
      filter.ExcludeList().Add(kMT_Platform);
      filter.ExcludeList().Add(kMT_NoPlatformCollision);
      xE00_ = id;
    }
    actor->SetMaterialFilter(filter);
    actor->HealthInfo()->SetHP(1.f); // stfs f0, 0x4(r3) at 0x136A8: +4 is mHealth
  }
  CMaterialList exclude;
  exclude.Add(kMT_Character);
  exclude.Add(kMT_Player);
  exclude.Add(kMT_CollisionActor);
  exclude.Add(kMT_Platform);
  exclude.Add(kMT_NoPlatformCollision);
  SetMaterialFilter(CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), exclude));
  // fn_14_60C0 (the vector's destructor, 0x13790)
}

// fn_14_12818: bit 0x80 of 0x10F5 (a ball held) is cleared, then only with HeadArmorDestroyed
// (0x10F4 bit 0x10) each ball (.rodata 0x928's radius): a destroyed slot's looping sound (0xF28)
// follows its locator (translation, column 1, volume 0x7F) and skips the rest; otherwise a fully
// morphed player (0x38C when 0x390 is 0) on an active ball collider within the radius
// (|player + (0, 0, ball radius) - ball|^2 <= r^2, the cror at 0x129E8) is stopped and moved onto
// it, the 0x1038 platform excludes the player (material 32, the ori at 0x12A38), the ball gets
// 0xDD0, bit 0x80 is set and the bombs there are pulled in (fn_14_126C8 with r^2); out of range
// the ball is pass-through again. .rodata 0x960 is 0.
// Area-53 runs ran every arm (run 37: in range with no platform); dg33 ran the inactive ball (beq
// at 0x1295C, a console DCTV of the held slot's ball: in a fight a slot's ball goes inactive in
// fn_14_1303C only after fn_14_12B04 set its 0xF28 sound) and forced the null ball (beq at
// 0x12950; the XCRT colliders are never deleted in a fight).
// @ fn_14_12818
void CDigitalGuardianHead::x12818_(CStateManager& mgr, float dt) {
  x10F5_ &= ~0x80;
  if ((x10F4_ & 0x10) == 0) {
    return;
  }
  CPlayer* player = mgr.Player(0);
  for (int i = 0; i < 2; ++i) {
    if (xF28_[i]) {
      const CTransform4f xf = GetLctrTransform(xDCC_[i]);
      CSfxManager::UpdateEmitter(xF28_[i], xf.GetTranslation(), xf.GetColumn(kDY), 0x7F);
      continue;
    }
    const CPlayer::EPlayerMorphBallState state =
        player->GetSpawnedMorphballState() == CPlayer::kMS_Unmorphed
            ? player->GetMorphballTransitionState()
            : CPlayer::kMS_Unmorphed;
    if (state != CPlayer::kMS_Morphed) {
      continue;
    }
    CCollisionActor* ball = TCastToPtr< CCollisionActor >(mgr.ObjectById(xDC4_[i]));
    if (ball == nullptr || !ball->GetActive()) {
      continue;
    }
    const CVector3f ballPos = ball->GetTranslation();
    const float ballRadius = player->GetMorphBall()->GetBallRadius();
    const float r2 = skHeadSphereJoints[i].radius * skHeadSphereJoints[i].radius;
    const CVector3f playerPos = player->GetTranslation();
    const float dx = (playerPos.GetX() + 0.f) - ballPos.GetX();
    const float dy = (playerPos.GetY() + 0.f) - ballPos.GetY();
    const float dz = (playerPos.GetZ() + ballRadius) - ballPos.GetZ();
    if (dz * dz + (dx * dx + dy * dy) <= r2) {
      player->Stop();
      player->MoveToWR(
          CVector3f(ballPos.GetX() - 0.f, ballPos.GetY() - 0.f, ballPos.GetZ() - ballRadius), dt);
      if (CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(mgr.ObjectById(x1038_))) {
        CMaterialFilter filter = plat->GetMaterialFilter();
        filter.ExcludeList().Add(kMT_Player);
        plat->SetMaterialFilter(filter);
      }
      ball->SetDamageVulnerability(xDD0_);
      x10F5_ |= 0x80;
      x126C8_(mgr, ball->GetTranslation(), r2);
    } else {
      ball->SetDamageVulnerability(CDamageVulnerability::PassThroughVulnerabilty());
    }
  }
}

// fn_14_126C8: the near list in the box pos -/+ half, filtered to include material 36 (.data
// 0xEA0, the bombs) only; every CBomb in it is moved to pos.
// @ fn_14_126C8
void CDigitalGuardianHead::x126C8_(CStateManager& mgr, const CVector3f& pos, float half) {
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  const CVector3f center = pos;
  const CAABox box(CVector3f(center.GetX() - half, center.GetY() - half, center.GetZ() - half),
                   CVector3f(center.GetX() + half, center.GetY() + half, center.GetZ() + half));
  mgr.BuildNearList(nearList, box, CMaterialFilter::MakeInclude(CMaterialList(kMT_Bomb)), this);
  for (rstl::reserved_vector< TUniqueId, kMaxObjects >::iterator it = nearList.begin();
       it != nearList.end(); ++it) {
    if (CBomb* bomb = TCastToPtr< CBomb >(mgr.ObjectById(*it))) {
      bomb->SetTranslation(center);
    }
  }
}

// fn_14_17D38: the active flag is read before the base's call (0x17D64..0x17D78).
// The 0xD70-null arms of ACTV/DCTV/XAUD/XDEL are forced (dg26): XCRT builds it first.
// @ fn_14_17D38
void CDigitalGuardianHead::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  const bool active = GetActive();
  CPatterned::AcceptScriptMsg(mgr, msg);
  switch (static_cast< uint >(msg.GetMessage())) {
  case kSM_Create: // "XCRT", 0x17E18
    BodyController()->Activate(mgr, pas::kAS_Invalid);
    x13318_(mgr);
    SetVisorOrbitableFlags(CVisorParameters::kVOF_All, false);
    RemoveMaterial(kMT_SeekerTarget, mgr); // li r4, 0x3F
    x12314_(mgr);
    break;
  case kSM_Delete: // "XDEL", 0x17E64
    if (xD70_ != nullptr) {
      xD70_->Destroy(mgr);
    }
    x11EA8_(); // fn_14_11EA8
    xFAEC_(mgr); // fn_14_FAEC
    x11F7C_(mgr); // fn_14_11F7C
    break;
  case kSM_Activate: // "ACTV", 0x17E9C
    if (!active && xD70_ != nullptr) {
      xD70_->SetActive(mgr, true);
    }
    break;
  case kSM_Deactivate: // "DCTV", 0x17EC0
    if (active && xD70_ != nullptr) {
      xD70_->SetActive(mgr, false);
    }
    break;
  case kSM_AIUpdateDisabled: // "XAUD", 0x17EE4
    if (xD70_ != nullptr) {
      xD70_->SetPhysicsActive(mgr, false);
    }
    break;
  case kSM_AreaLoaded: // "XALD", 0x17F00
    x13830_(mgr);
    break;
  case kSM_Damage: // "XDMG", 0x17F10
    x1303C_(mgr, msg.GetSenderId());
    break;
  case kSM_ResistedDamage: // "XRDG", 0x17F2C
    x12D98_(mgr, msg.GetSenderId());
    break;
  default:
    break;
  }
}

// fn_14_12D98: "XRDG". With the armour gone (0x10F4 bit 0x10) and the sender the root collider
// (0xE00), the weapon that last touched it (not the one already counted, 0x1040) and that its
// vulnerability (vtable 0x40) lets hit: the vulnerable damage, if above .rodata 0x960 (0), hurts
// the head along its column 1 (vtable 0xA4), flashes the echo emitter (0xC8) for
// skDamageHitTime and, above .rodata 0x9B0 (25) with the weapon still there, flinches the body
// (state cmd 0x1C, weight .rodata 0x96C = 1). Then the gravity repulsor's health (0x10E0) drops
// by it (even at 0), the weapon is remembered and "HeadModuleHit" (data +0x2C) plays at the
// collider, downward.
// Area-53 runs ran every arm but zero damage, which is data-dead (every entry WeaponHurts accepts
// here has multiplier 0.5..2.0, effect 3) and was forced in dg33 (ble at 0x12E90).
// review26 restored `> 0` / `> 25` (the ble is taken on NaN, so a NaN hit is skipped); the arm set
// is unchanged, so dg33's coverage stands (re-marked in batch31).
// @ fn_14_12D98
void CDigitalGuardianHead::x12D98_(CStateManager& mgr, TUniqueId sender) {
  if ((x10F4_ & 0x10) == 0 || sender != xE00_) {
    return;
  }
  const CCollisionActor* col = TCastToConstPtr< CCollisionActor >(mgr.GetObjectById(sender));
  if (col == nullptr) {
    return;
  }
  const TUniqueId touched = col->GetLastTouchedObject();
  const CWeapon* weapon = TCastToConstPtr< CWeapon >(mgr.GetObjectById(touched));
  if (weapon == nullptr || touched == x1040_) {
    return;
  }
  // Retail's one-argument WeaponHits (0x800DB834) is the host's WeaponHurts: an effect-3 entry
  // (every damaging entry of the head's table) counts, and its multiplier still applies.
  if (!col->GetDamageVulnerability()->WeaponHurts(weapon->GetCurrentDamageInfo().GetWeaponMode())) {
    return;
  }
  const float damage =
      weapon->GetCurrentDamageInfo().GetVulnerableDamage(*col->GetDamageVulnerability());
  // 0x12E8C / 0x12F04 fcmpo damage, 0 (25); ble skips (taken when GT is clear, so a NaN damage
  // skips the hit and the flinch too).
  if (damage > 0.f) {
    TakeDamage(GetTransform().GetColumn(kDY), damage);
    if (EchoEmitter() != nullptr) {
      EchoEmitter()->SetAutoReducingYellowDamage(CPatterned::skDamageHitTime);
    }
    if (TCastToConstPtr< CWeapon >(mgr.GetObjectById(touched)) && damage > 25.f) {
      BodyController()->CommandMgr().DeliverCmd(CBCAdditiveFlinchCmd(1.f));
    }
  }
  x10E0_ -= damage;
  x1040_ = touched;
  PlayCustomSound(col->GetTranslation(), CVector3f::Down(), mData.sound_HeadModuleHit, false);
}

// fn_14_13830: the body is the CONN/ATCH object; if it is a guardian it is linked to this head
// (fn_14_7168 with this head's id, 0x13968) and kept at 0x1034 (0x13970). Then the IS04/ACTV
// object (0x103E), the MDFY/FOLW waypoint (0x103C), the MDFY/PLAY platforms (0x1044) and the
// attack area (fn_14_119E4).
// @ fn_14_13830
void CDigitalGuardianHead::x13830_(CStateManager& mgr) {
  const TUniqueId bodyId = CheckConnectedObject(mgr, kSS_Connect, kSM_Attach);
  if (CDigitalGuardian* body = TCastToPtr< CDigitalGuardian >(mgr.ObjectById(bodyId))) {
    body->x7168_(mgr, GetUniqueId());
    x1034_ = bodyId;
  }
  x103E_ = FindConnectedObject(mgr, kSS_InternalState4, kSM_Activate);
  x103C_ = CheckConnectedObject_if(mgr, kSS_Modify, kSM_Follow, CWaypointPredicate());
  x1044_ = FindConnectedObjects_if(mgr, kSS_Modify, kSM_Play, CPlatformPredicate());
  x119E4_(mgr);
}

// fn_14_119E4: the IS03/FOLW waypoints' positions, raised by 15 (.rodata 0x988). With exactly two
// the area's centre is their midpoint (.rodata 0x9A0, 0.5), its first axis the horizontal step
// from the first to the second and its second axis that step turned a quarter (z is .rodata
// 0x960, 0). Retail keeps the positions in a three-slot stack array it does not bound; the host
// stops storing at three but keeps counting, so the "exactly two" test is the same.
// @ fn_14_119E4
void CDigitalGuardianHead::x119E4_(CStateManager& mgr) {
  const rstl::vector< TUniqueId > ids =
      FindConnectedObjects_if(mgr, kSS_InternalState3, kSM_Follow, CWaypointPredicate());
  CVector3f pos[3];
  int count = 0;
  for (rstl::vector< TUniqueId >::const_iterator it = ids.begin(); it != ids.end(); ++it) {
    const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(*it));
    if (actor == nullptr) { // retail's null test at 0x11AA0 (the predicate admits waypoints only)
      continue;
    }
    if (count < 3) {
      const CVector3f& t = actor->GetTranslation();
      pos[count] = CVector3f(t.GetX(), t.GetY(), t.GetZ() + 15.f);
    }
    ++count;
  }
  if (count == 2) {
    const float dx = pos[1].GetX() - pos[0].GetX();
    const float dy = pos[1].GetY() - pos[0].GetY();
    x1004_ = CVector3f(0.5f * (pos[0].GetX() + pos[1].GetX()),
                       0.5f * (pos[0].GetY() + pos[1].GetY()),
                       0.5f * (pos[0].GetZ() + pos[1].GetZ()));
    x1010_ = CVector3f(dx, dy, 0.f);
    x101C_ = CVector3f(-dy, dx, 0.f);
  }
}

// fn_14_13B38: the transform's forward column (0x28/0x38/0x48) flattened (z is .rodata 0x960, 0)
// and normalised; when that is not safe (the head looks straight up or down) the up column
// (0x2C/0x3C/0x4C), negated if forward's z is above 0.
// @ fn_14_13B38
CVector3f CDigitalGuardianHead::x13B38_() const {
  const CVector3f forward = GetTransform().GetColumn(kDY);
  const CVector3f flat(forward.GetX(), forward.GetY(), 0.f);
  if (flat.IsMagnitudeSafe()) {
    return flat.AsNormalized();
  }
  const CVector3f up = GetTransform().GetColumn(kDZ);
  if (forward.GetZ() > 0.f) {
    return -up;
  }
  return up;
}

// fn_14_11C08: with dir and its horizontal part both safe, dir normalised, slerped from the
// horizontal toward it by 55 degrees (.rodata 0x9B8, 0.9599311) when its pitch is larger;
// otherwise fn_14_13B38.
// @ fn_14_11C08
CVector3f CDigitalGuardianHead::x11C08_(const CVector3f& dir) const {
  const CVector3f fallback = x13B38_();
  const CVector3f flat(dir.GetX(), dir.GetY(), 0.f);
  if (dir.IsMagnitudeSafe() && flat.IsMagnitudeSafe()) {
    const CVector3f dirN = dir.AsNormalized();
    const CVector3f flatN = flat.AsNormalized();
    if (CVector3f::GetAngleDiff(flatN, dirN) > 0.9599311f) { // fcmpo/ble 0x11CE8: a NaN keeps it
      return CVector3f::Slerp(flatN, dirN, CRelAngle::FromRadians(0.9599311f));
    }
    return dirN;
  }
  return fallback;
}

// fn_14_11D64: only for dt > 0 (.rodata 0x960). 0x1028 becomes the direction to the face target
// (0x1036; its +0x54 minus this one's, through fn_14_11C08), or fn_14_13B38 with no target. Unless
// bit 0x08 of 0x10F4 is set (extrwi. 1, 28 at 0x11E54), the body controller faces it.
// @ fn_14_11D64
void CDigitalGuardianHead::x11D64_(CStateManager& mgr, float dt) {
  if (!(dt > 0.f)) { // 0x11D90 fcmpo/ble to the return (a NaN returns too)
    return;
  }
  if (const CEntity* ent = mgr.GetObjectById(x1036_)) {
    // Retail reads the entity's translation unchecked (lfs 0x54(r3) at 0x11DA8); the face target is
    // the player.
    const CActor* target = static_cast< const CActor* >(ent);
    x1028_ = x11C08_(target->GetTranslation() - GetTranslation());
  } else {
    x1028_ = x13B38_();
  }
  if ((x10F4_ & 0x08) == 0 && x1028_.IsMagnitudeSafe()) {
    BodyController()->FaceDirection(x1028_.AsNormalized(), dt);
  }
}

// fn_14_11698: with pos off the attack area's centre (0x1004), the angle from the first axis
// (0x1010) to it, negative when it lies behind the second axis (0x101C; the dot product below
// .rodata 0x960, 0). Returns false, leaving angle alone, when pos is the centre.
// @ fn_14_11698
bool CDigitalGuardianHead::x11698_(const CVector3f& pos, float& angle) const {
  const CVector3f d = pos - x1004_;
  if (d.GetX() == 0.f && d.GetY() == 0.f && d.GetZ() == 0.f) {
    return false;
  }
  angle = CVector3f::GetAngleDiff(x1010_, d);
  if (CVector3f::Dot(x101C_, d) < 0.f) {
    angle = -angle;
  }
  return true;
}

// fn_14_11788: the point (50 cos a, 40 sin a, 0) (.rodata 0x9C4, 0x9C0) in the frame looking from
// the centre (0x1004) along the second axis (0x101C, up is CVector3f::Up()), plus the centre.
// @ fn_14_11788
CVector3f CDigitalGuardianHead::x11788_(float angle) const {
  const float y = 40.f * CMath::FastSinR(angle);
  const float x = 50.f * CMath::FastCosR(angle);
  const CTransform4f frame = CTransform4f::LookAt(x1004_, x1004_ + x101C_, CVector3f::Up());
  return x1004_ + frame.Rotate(CVector3f(x, y, 0.f));
}

// fn_14_118B0: none when the head is at the attack area's centre (fn_14_11698); else the area's
// points at its angle, that plus 8 degrees and that minus 8 degrees (.rodata 0x9C8, 0.13962634).
// @ fn_14_118B0
int CDigitalGuardianHead::x118B0_(CVector3f* points) const {
  int count = 0;
  float angle = 0.f;
  if (x11698_(GetTranslation(), angle)) {
    points[count++] = x11788_(angle);
    points[count++] = x11788_(0.13962634f + angle);
    points[count++] = x11788_(angle - 0.13962634f);
  }
  return count;
}

// fn_14_114C8 (0x1D0 bytes): a point near the 40-unit circle (in x/y) round the player. Each
// point's score is its squared x/y distance to the player minus 1600 (.rodata 0x9BC), the head's
// own translation's first (0x114E4..0x11544). `outside` keeps the closest point outside the
// circle (while its score is still negative: any higher one), `inside` the closest point inside
// (while its score is still positive: any lower one). Nothing inside: `inside` (the closest
// outside one); nothing outside: `outside` (the closest inside one); else the two blended where
// the score crosses 0.
// Every return ran (area 53, `codecall <head> SetAttackPosition` with the player teleported
// round the head): the closest outside (far off), the closest inside (on a candidate), the blend
// (between); the last one forced (the player's x NaN at the entry: every compare false).
// @ fn_14_114C8
CVector3f CDigitalGuardianHead::x114C8_(const CVector3f& player, const CVector3f* points,
                                         int count) {
  const CVector3f self = GetTranslation(); // 0x54/0x58/0x5C at 0x114E4..0x11510
  const float selfDx = player.GetX() - self.GetX(); // 0x1150C
  const float selfDy = player.GetY() - self.GetY(); // 0x114FC
  // f29, 0x11540/0x11544
  float outsideScore = 0.f + (selfDx * selfDx + selfDy * selfDy) - 1600.f;
  float insideScore = outsideScore; // f30
  CVector3f outside = self;         // f7..f9 (0x11508..0x11528)
  CVector3f inside = self;          // f4..f6
  for (int i = 0; i < count; ++i) { // the 0xC stride (0x115E0), the end test at 0x115E4
    const CVector3f& p = points[i];
    const float dx = player.GetX() - p.GetX();          // 0x1155C
    const float dy = player.GetY() - p.GetY();          // 0x11554
    const float d = 0.f + (dx * dx + dy * dy) - 1600.f; // 0x11564..0x1156C
    // 0x11570..0x11594
    if ((d > 0.f && d < outsideScore) || (outsideScore < 0.f && d > outsideScore)) {
      outsideScore = d; // 0x11598
      outside = p;      // 0x1159C..0x115A4
    }
    // 0x115A8..0x115CC
    if ((d < 0.f && d > insideScore) || (insideScore > 0.f && d < insideScore)) {
      insideScore = d; // 0x115D0
      inside = p;      // 0x115D4..0x115DC
    }
  }
  if (insideScore > 0.f) { // fcmpo/ble at 0x115F4
    return inside;          // 0x115FC
  }
  if (outsideScore < 0.f) { // fcmpo/bge at 0x1160C
    return outside;         // 0x11614
  }
  if (outsideScore > insideScore) { // fcmpo/ble at 0x11624
    const float t = -insideScore / (outsideScore - insideScore); // 0x1162C..0x1163C
    return inside * (1.f - t) + outside * t; // .rodata 0x96C, 0x11640..0x11658
  }
  return outside; // 0x1166C
}

// fn_14_F390: the echo targets (0xF30/0xF34) that are gone or inactive (+0x20 bit 0x80), plus
// one, capped at 2.
// @ fn_14_F390
int CDigitalGuardianHead::xF390_(CStateManager& mgr) const {
  int active = 0;
  for (int i = 0; i < xF34_.size(); ++i) {
    const CEntity* ent = mgr.GetObjectById(xF34_[i]);
    if (ent != nullptr && ent->GetActive()) {
      ++active;
    }
  }
  const int count = xF34_.size() - active + 1;
  return count > 2 ? 2 : count;
}

// fn_14_F424: with the MDFY/FOLW waypoint (0x103C) and 4 s (.rodata 0x998) since the last one
// (0x10EC), a splitter is spent (0x10E8), the waypoint moves to the "B_ball_LCTR" locator
// (.rodata 0x940), IS01 goes out from this head, 0x10EC restarts, the 'PART' token (0xF04) becomes
// an "Eject Splitter Fx" explosion there (the model's scale, white) and sound_EjectSplitter (data
// +0x7C) plays along the forward column. With no waypoint none are left.
// @ fn_14_F424
void CDigitalGuardianHead::xF424_(CStateManager& mgr) {
  CScriptWaypoint* wp = TCastToPtr< CScriptWaypoint >(mgr.ObjectById(x103C_));
  if (wp == nullptr) {
    x10E8_ = 0;
    return;
  }
  if (!(x10EC_ >= 4.f)) { // 0xF468 fcmpo/cror eq,gt,eq/bne to the return (a NaN returns too)
    return;
  }
  --x10E8_;
  const CTransform4f xf = GetLctrTransform(rstl::string_l("B_ball_LCTR"));
  wp->SetTranslation(xf.GetTranslation());
  SendScriptMsgs(kSS_InternalState1, mgr, GetUniqueId(), kSM_None);
  x10EC_ = 0.f;
  if (xF04_.valid()) {
    const CVector3f scale = GetModelData()->GetScale();
    const rstl::string name = rstl::string_l("Eject Splitter Fx");
    const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, true,
                           kInvalidEditorId);
    const TUniqueId uid = mgr.AllocateUniqueId();
    CExplosion* explosion = rs_new CExplosion(xF04_.data(), uid, info, name, xf, 0, scale,
                                              CColor::White(), -1);
    if (explosion != nullptr) {
      mgr.AddObject(explosion);
    }
  }
  PlayCustomSound(xf.GetTranslation(), GetTransform().GetColumn(kDY), mData.sound_EjectSplitter,
                  false);
}

// fn_14_12314: the offsets (0xF40): RotateX(pi/2), RotateX(-pi/2) * RotateY(pi/2) and
// RotateX(-pi/2) * RotateY(-pi/2) (.rodata 0x9CC/0x9D0). With the echo-target model (data +0x9C)
// at this head's scale, each target ("EchoTarget", .rodata 0xF2E) is built inactive-info
// (li r6, 0) at locator 1 times its offset, solid, orbit, target and seeker-target (.data
// 0xEA4..0xEB0: 59, 41, 40, 63), with the echo-target vulnerability (data +0x454), this head as
// its owner and the parameter block; its id goes to 0xF34.
// The arms: the echo target asset id is invalid -> nothing is built, else the three echo targets
// are built. The run: area 53 (Sanctuary Fortress 09_cliff_temple_dark), 3000 frames; the
// "invalid" arm was forced (the asset id set to kInvalidAssetId at the entry, tools/cov/force.sh
// with /tmp/opencode/dgclose_12314.py).
// @ fn_14_12314
void CDigitalGuardianHead::x12314_(CStateManager& mgr) {
  xF40_[0] = CTransform4f::RotateX(CRelAngle::FromRadians(1.5707964f));
  xF40_[1] = CTransform4f::RotateX(CRelAngle::FromRadians(-1.5707964f)) *
             CTransform4f::RotateY(CRelAngle::FromRadians(1.5707964f));
  xF40_[2] = CTransform4f::RotateX(CRelAngle::FromRadians(-1.5707964f)) *
             CTransform4f::RotateY(CRelAngle::FromRadians(-1.5707964f));
  if (mData.echoTargets == kInvalidAssetId) {
    return;
  }
  const CModelData modelData(CStaticRes(mData.echoTargets, GetModelData()->GetScale()));
  const CTransform4f lctr = GetLctrTransform(CSegId(1));
  for (int i = 0; i < xF34_.size(); ++i) {
    const TUniqueId uid = mgr.AllocateUniqueId();
    const CTransform4f xf = lctr * xF40_[i];
    CDigitalGuardianEchoTarget* target = rs_new CDigitalGuardianEchoTarget(
        uid, rstl::string_l("EchoTarget"),
        CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, false, kInvalidEditorId), 0,
        xf, modelData, CMaterialList(kMT_Solid, kMT_Orbit, kMT_Target, kMT_SeekerTarget),
        LdrToDamageVulnerability(mData.echoTargetVulnerability), GetUniqueId(), mData);
    if (target != nullptr) {
      mgr.AddObject(target);
      xF34_[i] = uid;
    }
  }
}

// fn_14_1223C: unless the armour is gone (0x10F4 bit 0x10), each echo target is placed at
// locator 1 times its offset, pushed out by its extension (fn_14_19F18); a gone one's id is
// cleared.
// @ fn_14_1223C
void CDigitalGuardianHead::x1223C_(CStateManager& mgr) {
  if ((x10F4_ & 0x10) != 0) {
    return;
  }
  const CTransform4f lctr = GetLctrTransform(CSegId(1));
  for (int i = 0; i < xF34_.size(); ++i) {
    CDigitalGuardianEchoTarget* target =
        static_cast< CDigitalGuardianEchoTarget* >(mgr.ObjectById(xF34_[i]));
    if (target != nullptr) {
      target->SetExtendedTransform(lctr * xF40_[i]);
    } else {
      xF34_[i] = kInvalidUniqueId;
    }
  }
}

// fn_14_14CF4: every retracted or retracting echo target starts extending (fn_14_19DF8 with 3) and
// takes damage again (+0x78C bit 0x80 cleared); bit 0x02 (EchoTargetDestroyed) is cleared.
// @ fn_14_14CF4
void CDigitalGuardianHead::ExtractEchoTargets(CStateManager& mgr, float) {
  for (int i = 0; i < xF34_.size(); ++i) {
    CDigitalGuardianEchoTarget* target =
        static_cast< CDigitalGuardianEchoTarget* >(mgr.ObjectById(xF34_[i]));
    if (target != nullptr && (target->GetState() == CDigitalGuardianEchoTarget::kS_Retracted ||
                              target->GetState() == CDigitalGuardianEchoTarget::kS_Retracting)) {
      target->SetState(mgr, CDigitalGuardianEchoTarget::kS_Extending);
      target->SetDamageIgnored(false);
    }
  }
  x10F4_ &= ~0x02;
}

// fn_14_14E78: the body (0x1034) gets "OPEN" from this head; the head leaves every visor's
// orbit list (0xF) but the echo visor's (2), and every inactive echo target (+0x20 bit 0x80, the
// extrwi 1,24) is activated (vtable 0x1C).
// @ fn_14_14E78
void CDigitalGuardianHead::ExtractAntenna(CStateManager& mgr, float) {
  mgr.SendScriptMsg(x1034_, GetUniqueId(), kSM_Open, GetUniqueId());
  SetVisorOrbitableFlags(CVisorParameters::kVOF_All, false);
  SetVisorOrbitableFlags(CVisorParameters::kVOF_Echo, true);
  for (int i = 0; i < xF34_.size(); ++i) {
    CEntity* target = mgr.ObjectById(xF34_[i]);
    if (target != nullptr && !target->GetActive()) {
      target->SetActive(true);
    }
  }
}

// fn_14_14DA0: every extended or extending echo target starts retracting (fn_14_19DF8 with 2) and
// ignores damage (+0x78C bit 0x80); bits 0x40 and 0x02 are cleared, the deployment time (0x10F0)
// grows by data +0x20 (lfs 0x10F0 / lfs 0x7E0 / fadds / stfs at 0x14E48..0x14E54) and bit 0x04
// is set.
// @ fn_14_14DA0
void CDigitalGuardianHead::RetractEchoTargets(CStateManager& mgr, float) {
  for (int i = 0; i < xF34_.size(); ++i) {
    CDigitalGuardianEchoTarget* target =
        static_cast< CDigitalGuardianEchoTarget* >(mgr.ObjectById(xF34_[i]));
    if (target != nullptr && (target->GetState() == CDigitalGuardianEchoTarget::kS_Extended ||
                              target->GetState() == CDigitalGuardianEchoTarget::kS_Extending)) {
      target->SetState(mgr, CDigitalGuardianEchoTarget::kS_Retracting);
      target->SetDamageIgnored(true);
    }
  }
  x10F4_ &= ~0x40;
  x10F4_ &= ~0x02;
  x10F0_ += mData.maxStage2StunnedTime;
  x10F4_ |= 0x04;
}

// fn_14_17A44: the base's PreRender.
// @ fn_14_17A44
void CDigitalGuardianHead::PreRender(CStateManager& mgr) {
  CPatterned::PreRender(mgr);
}

// fn_14_17A00: the base's AddToRenderer, then the base's render (the relocations name both).
// @ fn_14_17A00
void CDigitalGuardianHead::AddToRenderer(const CStateManager& mgr) const {
  CPatterned::AddToRenderer(mgr);
  CPatterned::Render(mgr);
}

// fn_14_17B28 (vtable slot 11, Render): the head's render flag (lbl_14_bss_18) is set (0x17B48)
// and cleared (0x17BA4) around the head's own RenderSystemsToBeDrawnLast (the virtual call through
// vtable 0xBC at 0x17B94, fn_14_17A64). The mask and target start at zero (0x17B4C, 0x17B50) and
// are only filled with mDrawParticles (the byte at 0x421, clrlwi. at 0x17B58). The base's Render
// is not called.
// @ fn_14_17B28
void CDigitalGuardianHead::Render(const CStateManager& mgr) const {
  sHeadRendering = 1;
  uint mask = 0;
  uint target = 0;
  if (mDrawParticles) {
    GenerateParticlePOIMask(mgr, mask, target);
  }
  RenderSystemsToBeDrawnLast(mgr, mask, target);
  sHeadRendering = 0;
}

// fn_14_17A64 (vtable 0xBC): nothing unless the head's render flag is set (lbz/beq at 0x17A94).
// With 0x10F4 bit 0x10 clear (extrwi. 1, 27 / bne at 0x17AA4) and the armour model there (0xEF0,
// 0x17AAC), the armour (0xEA4) is drawn at the "segment 1" locator (GetLctrTransform at 0x17ACC)
// with the actor's lights (0xBC) and draw flags (0xFC), 0x17AF0. Then the base's (0x17B04).
// @ fn_14_17A64
void CDigitalGuardianHead::RenderSystemsToBeDrawnLast(const CStateManager& mgr, uint mask,
                                                      uint target) const {
  if (sHeadRendering == 0) {
    return;
  }
  if ((x10F4_ & 0x10) == 0 && xEA4_.valid()) {
    const CTransform4f xf = GetLctrTransform(CSegId(1));
    xEA4_.data().Render(mgr, xf, GetActorLights(), GetModelFlags());
  }
  CPatterned::RenderSystemsToBeDrawnLast(mgr, mask, target);
}

// fn_14_17840 (vtable 0xDC): the projectile attack 0x10D0 picks the weapon: 1 the charge (0xE2C),
// 2 the missiles (0xE54), 3 the gun (0xE7C); 0, negative and >= 4 the pulse (0xE04, the addi at
// 0x17844; cmpwi 2/0/4 at 0x1784C, 0x17858, 0x17864).
// @ fn_14_17840
CProjectileInfo* CDigitalGuardianHead::ProjectileInfo() {
  switch (x10D0_) {
  case 1:
    return &xE2C_;
  case 2:
    return &xE54_;
  case 3:
    return &xE7C_;
  default:
    return &xE04_;
  }
}

// fn_14_17890 (vtable 0x54): the "segment 1" locator's translation (GetLctrTransform at 0x17918),
// plus, for dt > 0.0f (.rodata 0x960, fcmpo/ble at 0x178E4), the predicted motion's translation
// (PredictMotion at 0x178EC, the CMotionState copy fn_14_17974); else the zero vector.
// @ fn_14_17890
CVector3f CDigitalGuardianHead::GetAimPosition(const CStateManager& mgr, float dt) const {
  CVector3f offset = CVector3f::Zero();
  if (dt > 0.f) {
    offset = PredictMotion(dt).GetTranslation();
  }
  return GetLctrTransform(CSegId(1)).GetTranslation() + offset;
}

// fn_14_17638 (vtable 0x11C): the anim data's vertex bounds through Translate(-translation) *
// the transform (0x24) * Scale(the model's scale), as the body's fn_14_1570.
// @ fn_14_17638
CAABox CDigitalGuardianHead::GetScanVisorRenderBounds(const CStateManager& mgr) const {
  const CAABox verts = GetModelData()->GetAnimationData()->CalcBoundingBoxFromModelVerts();
  const CTransform4f xf = CTransform4f::Translate(-GetTranslation()) * GetTransform() *
                          CTransform4f::Scale(GetModelData()->GetScale());
  return verts.GetTransformedAABox(xf);
}

// fn_14_174F4 (vtable 0x120): the transform (0x24) and draw flags (0xFC) are swapped for xf and
// flags (0x17520..0x17580), the base's Render runs (0x17584), and with 0x10F4 bit 0x10 clear
// (extrwi. 1, 27 at 0x1758C) and the armour there (0xEF0, 0x17594) the armour is drawn at xf * the
// "segment 1" scaled locator (0x175B0, 0x175C0) with the actor's lights and the given flags
// (0x175EC). The transform and flags are put back (0x175F0..0x17620).
// @ fn_14_174F4
void CDigitalGuardianHead::ScanVisorRender(const CStateManager& mgr, const CTransform4f& xf,
                                           const CModelFlags& flags) const {
  const CTransform4f savedXf = GetTransform();
  const CModelFlags savedFlags = GetModelFlags();
  PortRawTransform() = xf;
  const_cast< CDigitalGuardianHead* >(this)->SetModelFlags(flags);
  CPatterned::Render(mgr);
  if ((x10F4_ & 0x10) == 0 && xEA4_.valid()) {
    const CTransform4f armourXf = xf * GetScaledLocatorTransform(CSegId(1));
    xEA4_.data().Render(mgr, armourXf, GetActorLights(), flags);
  }
  PortRawTransform() = savedXf;
  const_cast< CDigitalGuardianHead* >(this)->SetModelFlags(savedFlags);
}

// fn_14_179E0: the pass-through vulnerability.
// @ fn_14_179E0
const CDamageVulnerability* CDigitalGuardianHead::GetDamageVulnerability() const {
  return &CDamageVulnerability::PassThroughVulnerabilty();
}

// fn_14_177C8: while bit 0x08 of 0x10F4 is clear, one of the three 'SCAN' tokens (0x10 set: the
// final one at 0xD7C, else 0x40 set: the stunned one at 0xD78, else the shield one at 0xD74);
// otherwise, or with no token, the base's. The ctor sets 0x08, so the base's until the head
// detaches (fn_14_16160 clears it).
// Every arm ran (area 53, `scaninfo <head>` x180 under gdb attach): bit 0x08 set (the base's,
// 4BA321EA) naturally; forced: 0x08 clear (the shield's 7D9FAEC7), +0x40 (the stunned
// AFE327ED), +0x10 (the final A23D895E), and each token nulled in its own arm (the base's
// again, three times).
// @ fn_14_177C8
CScannableObjectInfo* CDigitalGuardianHead::GetScannableObjectInfo() const {
  if ((x10F4_ & 0x08) == 0) {       // extrwi. 1, 28 / bne at 0x177D8
    if ((x10F4_ & 0x10) != 0) {     // 0x177E0
      if (xD7C_.get() != nullptr) { // cmplwi 0 / beq at 0x177EC
        return xD7C_->GetObject();  // the cached object (+8) at 0x177F4
      }
    } else if ((x10F4_ & 0x40) != 0) { // 0x177FC
      if (xD78_.get() != nullptr) {    // cmplwi 0 / beq at 0x17808
        return xD78_->GetObject();     // 0x17810
      }
    } else if (xD74_.get() != nullptr) { // cmplwi 0 / beq at 0x1781C
      return xD74_->GetObject();         // 0x17824
    }
  }
  return CPatterned::GetScannableObjectInfo(); // 0x1782C
}

// fn_14_17468: the base's setup, then the triggers (20 = 0x14), the states (17 = 0x11) and the
// codes (29 = 0x1D) on the state machine at 0x350 (the calls at 0x17498, 0x174B8, 0x174D8).
// @ fn_14_17468
void CDigitalGuardianHead::SetupStateMachine(CStateManager& mgr) {
  CPatterned::SetupStateMachine(mgr);
  StateMachine* stateMachine = mStateMachine.get();
  stateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  stateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  stateMachine->SetCodeFunctions(skCodes, ARRAY_SIZE(skCodes));
}

// ---------------------------------------------------------------------------------------------
// Triggers. The masks come from the rlwinm/extrwi bit numbers, MSB first.

// fn_14_17454: the animation state (0x6B4) is 3 (kAS_Over).
// @ fn_14_17454
bool CDigitalGuardianHead::StateOver(CStateManager& mgr, const CTriggerData& data) const {
  return mAnimationState.GetState() == CAnimationState::kAS_Over;
}

// fn_14_17408: the body's bit 0x40 of 0x128C (extrwi 1, 25; set by its CrippledReaction); with no
// body the answer is true (li r3, 1 at 0x17440).
// @ fn_14_17408
bool CDigitalGuardianHead::MainBodyDestroyed(CStateManager& mgr, const CTriggerData& data) const {
  if (const CDigitalGuardian* body = TCastToConstPtr< CDigitalGuardian >(mgr.GetObjectById(x1034_))) {
    return (body->x128C_ & 0x40) != 0;
  }
  return true;
}

// fn_14_173CC: attack mode 0 or 1 and the projectile timer (0x10D4) at most 0 (.rodata 0x960).
// The arms: modes 0 and 1 run in every area-53 fight; the other modes' false was forced
// (tools/cov/force.sh, xFE8_ = 2 at the entry from hit 41, 1911 hits), as SetVortexAttackMode
// sets it while the head's triggers are not asked.
// @ fn_14_173CC
bool CDigitalGuardianHead::ShouldProjectileAttack(CStateManager& mgr,
                                                  const CTriggerData& data) const {
  if (xFE8_ == 0 || xFE8_ == 1) {
    return x10D4_ <= 0.f;
  }
  return false;
}

// fn_14_173B8: the burst count (0x10D8, a word: cntlzw/rlwnm) at most 0.
// cntlzw; li 1; rlwnm 31,31 is 1 when the count is 0 or 32 (x < 0 or x == 0), i.e. x <= 0;
// asmcheck's XCMP there is a false positive.
// @ fn_14_173B8
bool CDigitalGuardianHead::IsBurstOver(CStateManager& mgr, const CTriggerData& data) const {
  return x10D8_ <= 0;
}

// fn_14_173AC: bit 0x80 of 0x10F4 (extrwi 1, 24).
// No branch: the extrwi result is returned. Runs 410-413 (forced `fsm <body> VortexAttack`) read
// it with the bit set; earlier runs read it clear.
// @ fn_14_173AC
bool CDigitalGuardianHead::IsIdle(CStateManager& mgr, const CTriggerData& data) const {
  return (x10F4_ & 0x80) != 0;
}

// fn_14_17398: the attack mode (0xFE8) is 4.
// @ fn_14_17398
bool CDigitalGuardianHead::IsStunned(CStateManager& mgr, const CTriggerData& data) const {
  return xFE8_ == 4;
}

// fn_14_17384: the attack mode (0xFE8) is 3.
// @ fn_14_17384
bool CDigitalGuardianHead::IsGuarding(CStateManager& mgr, const CTriggerData& data) const {
  return xFE8_ == 3;
}

// fn_14_17370: the projectile attack (0x10D0) is 3.
// @ fn_14_17370
bool CDigitalGuardianHead::UseMachineGun(CStateManager& mgr, const CTriggerData& data) const {
  return x10D0_ == 3;
}

// fn_14_1735C: the projectile attack (0x10D0) is 2.
// @ fn_14_1735C
bool CDigitalGuardianHead::UseLockOnMissiles(CStateManager& mgr, const CTriggerData& data) const {
  return x10D0_ == 2;
}

// fn_14_1734C: the projectile attack (0x10D0) is 0.
// @ fn_14_1734C
bool CDigitalGuardianHead::UseAnnihilatorPulse(CStateManager& mgr,
                                               const CTriggerData& data) const {
  return x10D0_ == 0;
}

// fn_14_17338: the projectile attack (0x10D0) is 1.
// @ fn_14_17338
bool CDigitalGuardianHead::UseAnnihilatorCharge(CStateManager& mgr,
                                                const CTriggerData& data) const {
  return x10D0_ == 1;
}

// fn_14_17298: no splitter command module or main chassis in the object list (retail walks the
// list at state manager +0x830, its first dynamic list, which holds every object).
// @ fn_14_17298
bool CDigitalGuardianHead::AllSplittersDead(CStateManager& mgr, const CTriggerData& data) const {
  CObjectList& list = mgr.ObjectListById(kOL_All);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    if (TCastToPtr< CSplitterCommandModule >(list[i]) != nullptr) {
      return false;
    }
    if (TCastToPtr< CSplitterMainChassis >(list[i]) != nullptr) {
      return false;
    }
  }
  return true;
}

// fn_14_1728C: bit 0x10 of 0x10F4 (extrwi 1, 27).
// @ fn_14_1728C
bool CDigitalGuardianHead::HeadArmorDestroyed(CStateManager& mgr, const CTriggerData& data) const {
  return (x10F4_ & 0x10) != 0;
}

// fn_14_17244: the body's fn_14_70B4; with no body the answer is false (li r3, 0 at 0x17278).
// @ fn_14_17244
bool CDigitalGuardianHead::IsAntennaTransmitting(CStateManager& mgr,
                                                 const CTriggerData& data) const {
  if (const CDigitalGuardian* body = TCastToConstPtr< CDigitalGuardian >(mgr.GetObjectById(x1034_))) {
    return body->x70B4_();
  }
  return false;
}

// fn_14_171F8: with no visual target (0xDA0 invalid) the answer is true; else the target must be
// seen (bit 0x80 of 0xDB8, extrwi. 1, 24) for longer than 2.25 (.rodata 0x9DC, 0xDB0).
// @ fn_14_171F8
bool CDigitalGuardianHead::HasLineOfSight(CStateManager& mgr, const CTriggerData& data) const {
  if (xD80_.GetVisualTarget() == kInvalidUniqueId) {
    return true;
  }
  return xD80_.IsTargetVisible() && xD80_.GetVisibleTime() > 2.25f;
}

// fn_14_171EC: bit 0x02 of 0x10F4 (extrwi 1, 30).
// @ fn_14_171EC
bool CDigitalGuardianHead::EchoTargetDestroyed(CStateManager& mgr,
                                               const CTriggerData& data) const {
  return (x10F4_ & 0x02) != 0;
}

// fn_14_171D8: the echo targets left (0xFE0, a word) at most 0.
// The cntlzw/rlwnm idiom (see 173B8) gives 1 for x <= 0; asmcheck's XCMP is a false positive.
// @ fn_14_171D8
bool CDigitalGuardianHead::AllEchoTargetsDestroyed(CStateManager& mgr,
                                                   const CTriggerData& data) const {
  return xFE0_ <= 0;
}

// fn_14_171B8: the gravity repulsor's health (0x10E0) at most 0 (.rodata 0x960).
// @ fn_14_171B8
bool CDigitalGuardianHead::ShouldRetreat(CStateManager& mgr, const CTriggerData& data) const {
  return x10E0_ <= 0.f;
}

// fn_14_17198: the id at 0x103A is set.
// subf/subf/or/srwi 31: a sign test of (id - 0xFFFF) | (0xFFFF - id), i.e. id != 0xFFFF.
// @ fn_14_17198
bool CDigitalGuardianHead::BombPitDestroyed(CStateManager& mgr, const CTriggerData& data) const {
  return x103A_ != kInvalidUniqueId;
}

// fn_14_17184: the lives left (0x10E4, a word) at most 0.
// @ fn_14_17184
bool CDigitalGuardianHead::IsDead(CStateManager& mgr, const CTriggerData& data) const {
  return x10E4_ <= 0;
}

// ---------------------------------------------------------------------------------------------
// States.

// fn_14_17180: empty.
// @ fn_14_17180
void CDigitalGuardianHead::Start(CStateManager& mgr, EStateMsg msg, float dt) {}

// fn_14_17134: bit 0x80 of 0x10F4 is set on activate (0) and cleared on deactivate (2); the
// update (1) does nothing (rlwimi r0, r4, 7, 24, 24 at 0x17160 / 0x17174).
// @ fn_14_17134
void CDigitalGuardianHead::Idle(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    x10F4_ |= 0x80;
    break;
  case kStateMsg_Deactivate:
    x10F4_ &= ~0x80;
    break;
  default:
    break;
  }
}

// fn_14_1709C: on activate the head faces its horizontal forward (fn_14_13B38, stored at
// 0x1028); each update turns it toward the face target (fn_14_11D64); on deactivate 0x1028 is
// cleared.
// @ fn_14_1709C
void CDigitalGuardianHead::FaceTarget(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    x1028_ = x13B38_();
    break;
  case kStateMsg_Update:
    x11D64_(mgr, dt);
    break;
  case kStateMsg_Deactivate:
    x1028_ = CVector3f::Zero();
    break;
  default:
    break;
  }
}

// fn_14_16160. Activate: the animation state becomes 1 (ready) and 0x1028 the horizontal facing
// (fn_14_13B38). Update: while the body (0x1034) still holds this head (its 0x1082), the body lets
// go (fn_14_70E4), bit 0x08 of 0x10F4 is cleared, the head moves to the IS05/FOLW waypoint and
// the locomotion type becomes 3 (combat); then once the state's time reaches data +0x44 (the
// field after sound_HeadModuleHit) the animation state is 3 (over). Deactivate: animation state 0,
// locomotion 3, 0x1028 cleared, bit 0x08 cleared and the attack mode (0xFE8) 1.
// @ fn_14_16160
void CDigitalGuardianHead::DetachFromBody(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    x1028_ = x13B38_();
    break;
  case kStateMsg_Update: {
    CDigitalGuardian* body = TCastToPtr< CDigitalGuardian >(mgr.ObjectById(x1034_));
    if (body != nullptr && body->x1082_ == GetUniqueId()) {
      body->x70E4_(mgr);
      x10F4_ &= ~0x08;
      const TUniqueId id =
          FindConnectedObject_if(mgr, kSS_InternalState5, kSM_Follow, CWaypointPredicate());
      if (const CScriptWaypoint* wp = TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(id))) {
        SetTranslation(wp->GetTranslation());
      }
      BodyController()->SetLocomotionType(pas::kLT_Combat);
    }
    if (mStateMachine->GetTime() >= mData.unknown_0x1cc6d870) {
      mAnimationState.SetState(CAnimationState::kAS_Over);
    }
    break;
  }
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    BodyController()->SetLocomotionType(pas::kLT_Combat);
    x1028_ = CVector3f::Zero();
    x10F4_ &= ~0x08;
    xFE8_ = 1;
    break;
  default:
    break;
  }
}

// fn_14_15B94. Activate: animation state 1. Update: the head flies toward the attack position
// (0xFEC) at data +0x14 (maxLinearVelocity) per second, in its own frame (TransposeRotate, then
// MoveInOneFrameOR); once a step would reach it, it is placed there and the animation state is 3.
// Then it turns toward the face target (fn_14_11D64). Deactivate: animation state 0.
// @ fn_14_15B94
void CDigitalGuardianHead::SeekAttackPosition(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    break;
  case kStateMsg_Update: {
    const CVector3f delta = xFEC_ - GetTranslation();
    const float step = mData.maxLinearVelocity * dt;
    if (step < delta.Magnitude()) {
      MoveInOneFrameOR(GetTransform().TransposeRotate(delta.AsNormalized() * step), dt);
    } else {
      SetTranslation(xFEC_);
      mAnimationState.SetState(CAnimationState::kAS_Over);
    }
    x11D64_(mgr, dt);
    break;
  }
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  default:
    break;
  }
}

// fn_14_15F3C. Activate: animation state 1. Update: while a knock-back may be issued
// (CanIssueCommand with 6 = kAS_KnockBack, 0x15F94) one is sent along the forward column
// (0x28/0x38/0x48) with severity 1 (kS_One); otherwise, once the body is in the knock-back state
// (its current state id 6, 0x16070), the locomotion type becomes 5 and the head drifts toward the
// attack position (0xFEC) at data +0x1C (maxStunnedLinearVelocity) per second, only while a step
// falls short of it. Deactivate: animation state 0, locomotion type 5.
// @ fn_14_15F3C
void CDigitalGuardianHead::StunnedReaction(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_KnockBack)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCKnockBackCmd(GetTransform().GetColumn(kDY), pas::kS_One));
    } else if (BodyController()->GetCurrentStateId() == pas::kAS_KnockBack) {
      BodyController()->SetLocomotionType(pas::kLT_Internal5);
      const CVector3f delta = xFEC_ - GetTranslation();
      if (delta.IsMagnitudeSafe()) {
        const float step = mData.maxStunnedLinearVelocity * dt;
        if (step < delta.Magnitude()) {
          MoveInOneFrameOR(GetTransform().TransposeRotate(delta.AsNormalized() * step), dt);
        }
      }
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    BodyController()->SetLocomotionType(pas::kLT_Internal5);
    break;
  default:
    break;
  }
}

// fn_14_15E18. Activate: locomotion type 5, animation state 1. Update: with splitters left
// (0x10E8 > 0) and the deployment time (0x10F0) at data +0x20 (maxStage2StunnedTime), one is
// ejected (fn_14_F424, after setting bit 0x04 of 0x10F4) when 0x04 is already set or every
// splitter is dead (fn_14_17298). Otherwise, with none left, that time reached and 4 s since the
// last (0x10EC, .rodata 0x998), the animation state is 3. Deactivate: animation state 0.
// @ fn_14_15E18
void CDigitalGuardianHead::DeploySplitters(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    BodyController()->SetLocomotionType(pas::kLT_Internal5);
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    break;
  case kStateMsg_Update:
    if (x10E8_ > 0 && x10F0_ >= mData.maxStage2StunnedTime &&
        ((x10F4_ & 0x04) != 0 || AllSplittersDead(mgr, CTriggerData(0.f)))) {
      x10F4_ |= 0x04;
      xF424_(mgr);
    } else if (x10E8_ <= 0 && x10F0_ >= mData.maxStage2StunnedTime && x10EC_ >= 4.f) {
      mAnimationState.SetState(CAnimationState::kAS_Over);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  default:
    break;
  }
}

// fn_14_1561C: on activate only, the boss bar is cleared (no boss, .rodata 0x960 = 0, string 0),
// then MassiveDeath (vtable 0xD4).
// @ fn_14_1561C
void CDigitalGuardianHead::Dead(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg != kStateMsg_Activate) {
    return;
  }
  mgr.SetBossParams(kInvalidUniqueId, 0.f, 0);
  MassiveDeath(mgr);
}

// fn_14_16D64: the annihilator pulse. The cmpwi chain at 0x16D88..0x16DB0 picks the message.
// Activate (0): Ready (0x6B4 = 1, 0x16DBC), flag 0x20 (rlwimi at 0x16DC8), the turn direction
// from fn_14_13B38 (0x16DD0, stores 0x16DD8..0x16DE8) and one burst fewer (0x10D8 - 1, 0x16DF4).
// Update (1): when a projectile attack can be issued (CanIssueCommand with 0x12, 0x16E08) it is
// sent (DeliverCmd 6 at 0x16E80): severity 0 (li r0 0 at 0x16E18), the target the head's position
// (0x54..0x5C) plus its Y column (0x28/0x38/0x48), no blend (stb 0 at 0x16E7C); then the head
// turns either way (fn_14_11D64 at 0x16ED8). Deactivate (2): NotReady (0x16EE4); while the body
// is in the projectile attack (0x37C == 0x12, 0x16EF0) it is aborted (0xD, DeliverCmd at 0x16F14);
// flag 0x20 clears (0x16F2C) and the turn direction is sZeroVector (0x16F38..0x16F4C).
// Every arm ran (dg22_gun2: uevents and statecalls 0/1/2, in and out of the projectile attack).
// @ fn_14_16D64
void CDigitalGuardianHead::PulseAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready); // 0x16DBC
    x10F4_ |= 0x20;                                        // 0x16DC8
    x1028_ = x13B38_();                                    // 0x16DD0
    x10D8_ -= 1;                                           // 0x16DF4
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_ProjectileAttack)) {
      BodyController()->CommandMgr().DeliverCmd(CBCProjectileAttackCmd(
          pas::kS_Zero, GetTranslation() + GetTransform().GetColumn(kDY), false)); // 0x16E80
    }
    x11D64_(mgr, dt); // 0x16ED8
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady); // 0x16EE4
    if (BodyController()->GetCurrentStateId() == pas::kAS_ProjectileAttack) { // 0x16EF0
      BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_AbortScripted)); // 0x16F14
    }
    x10F4_ &= ~0x20;            // 0x16F2C
    x1028_ = CVector3f::Zero(); // 0x16F38..0x16F4C
    break;
  default:
    break;
  }
}

// fn_14_167EC: the lock-on missiles. PulseAttack's body without the burst count (the cmpwi chain
// at 0x16810..0x16838): Activate (0) Ready (0x16844), flag 0x20 (0x16850), the turn direction from
// fn_14_13B38 (0x16858, stores 0x16860..0x16870); Update (1) the projectile attack
// (CanIssueCommand with 0x12 at 0x16884, DeliverCmd 6 at 0x16900) with severity 1 (li r5 1 at
// 0x168BC), then the turn (fn_14_11D64 at 0x16958); Deactivate (2) NotReady (0x16964), the abort
// while the body is in state 0x12 (0x16970, DeliverCmd 0xD at 0x16994), flag 0x20 cleared
// (0x169AC) and the turn direction zeroed (0x169B8..0x169CC).
// Every arm ran (dg22_gun2: statecalls 0/1/2, in and out of the projectile attack).
// @ fn_14_167EC
void CDigitalGuardianHead::MissileAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready); // 0x16844
    x10F4_ |= 0x20;                                        // 0x16850
    x1028_ = x13B38_();                                    // 0x16858
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_ProjectileAttack)) {
      BodyController()->CommandMgr().DeliverCmd(CBCProjectileAttackCmd(
          pas::kS_One, GetTranslation() + GetTransform().GetColumn(kDY), false)); // 0x16900
    }
    x11D64_(mgr, dt); // 0x16958
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady); // 0x16964
    if (BodyController()->GetCurrentStateId() == pas::kAS_ProjectileAttack) { // 0x16970
      BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_AbortScripted)); // 0x16994
    }
    x10F4_ &= ~0x20;            // 0x169AC
    x1028_ = CVector3f::Zero(); // 0x169B8..0x169CC
    break;
  default:
    break;
  }
}

// fn_14_16408: the machine gun (the cmpwi chain at 0x1642C..0x16454). Activate (0): Ready
// (0x16468); the side is 1 when CRandom16::Range(0.0f, 100.0f) (.rodata 0x960/0x978, 0x16478)
// is at most 50.0f (.rodata 0x9C4; fcmpo/cror at 0x16484..0x1648C), else 0 (0x1649C); the sweep's
// ends for it (fn_14_10798 at 0x164AC); phase 0 (0x164C0), its time 0.0f (0x164CC), the next
// bullet in machineGunFireTime (data +0x24C, 0x164D4); the turn direction toward the sweep's start
// from the head's position (fn_14_11C08 at 0x16520); flag 0x20 (rlwimi at 0x16544).
// Update (1): the phase time grows by dt (0x16554), then by phase (the cmpwi chain at 0x16560):
// 0 (charge): from machineGunChargeDuration (data +0x248; fcmpo/bge at 0x16580) phase 1 starts at
// 0.0f (0x16628/0x16630); before it the sweep's ends follow the player (fn_14_10798 at 0x1658C) and
// the head aims at the start from the locator (0x10A4, GetLctrTransform at 0x1659C; fn_14_11C08
// at 0x16600). 1 (sweep): from machineGunSweepDuration (data +0x244; fcmpo/bge at 0x16640) the
// state is Over (0x16754); before it the aim moves from start to end by t = time / duration
// (fdivs at 0x1667C, a (1 - t) + b t) seen from the locator (0x16654; fn_14_11C08 at 0x166F4), and
// the bullet timer counts down (0x1671C); at most 0.0f (fcmpo/cror at 0x16728) it is reset to the
// fire time (0x16744) and a bullet goes along the aim (fn_14_101E0 at 0x16748). Any other phase
// does nothing. Deactivate (2): NotReady (0x16760), the abort while the body is in state 0x12
// (0x1676C, DeliverCmd 0xD at 0x16790), flag 0x20 cleared (0x167A8), the turn direction zeroed
// (0x167B4..0x167C8).
// Every arm ran (dg22_gun2: both sides, phases 0/1/other, sweep end, bullet timer).
// @ fn_14_16408
void CDigitalGuardianHead::MachineGunAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);           // 0x16468
    x10C0_ = mgr.Random()->Range(0.f, 100.f) <= 50.f ? 1 : 0;       // 0x16478..0x1649C
    x10798_(mgr, x10C0_);                                           // 0x164AC
    x10C4_ = 0;                                                     // 0x164C0
    x10C8_ = 0.f;                                                   // 0x164CC
    x10CC_ = mData.machineGunFireTime;                              // 0x164D4
    x1028_ = x11C08_(x10A8_ - GetTranslation());                    // 0x16520
    x10F4_ |= 0x20;                                                 // 0x16544
    break;
  case kStateMsg_Update:
    x10C8_ += dt; // 0x16554
    switch (x10C4_) {
    case 0:
      if (!(x10C8_ < mData.machineGunChargeDuration)) { // 0x16580 bge (a NaN too)
        x10C4_ = 1;                                   // 0x16628
        x10C8_ = 0.f;                                 // 0x16630
      } else {
        x10798_(mgr, x10C0_); // 0x1658C
        x1028_ = x11C08_(x10A8_ - GetLctrTransform(x10A4_).GetTranslation()); // 0x16600
      }
      break;
    case 1:
      if (!(x10C8_ < mData.machineGunSweepDuration)) {        // 0x16640 bge (a NaN too)
        mAnimationState.SetState(CAnimationState::kAS_Over); // 0x16754
      } else {
        const CVector3f from = GetLctrTransform(x10A4_).GetTranslation(); // 0x16654
        const float t = x10C8_ / mData.machineGunSweepDuration;          // 0x1667C
        x1028_ = x11C08_(x10A8_ * (1.f - t) + x10B4_ * t - from);        // 0x166F4
        x10CC_ -= dt;                                                    // 0x1671C
        if (x10CC_ <= 0.f) {                                             // 0x16728
          x10CC_ = mData.machineGunFireTime;                             // 0x16744
          x101E0_(mgr, x1028_);                                          // 0x16748
        }
      }
      break;
    default:
      break;
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady); // 0x16760
    if (BodyController()->GetCurrentStateId() == pas::kAS_ProjectileAttack) { // 0x1676C
      BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_AbortScripted)); // 0x16790
    }
    x10F4_ &= ~0x20;            // 0x167A8
    x1028_ = CVector3f::Zero(); // 0x167B4..0x167C8
    break;
  default:
    break;
  }
}

// fn_14_10798: the machine gun's sweep. From the player's aim position now (vtable 0x54 with
// 0.0f, 0x1084C) and in half the sweep's duration (0.5f, .rodata 0x9A0, times data +0x244; 0x10890)
// the centre is 0.5f * (((later + the player's position) - now) + later) (0x108A4..0x108E8). The
// side axis is (c.y - o.y, -(c.x - o.x), 0.0f) from the gun's locator o (0x10A4, GetLctrTransform
// at 0x10810; stores 0x108D0..0x108FC) normalised when safe (IsMagnitudeSafe at 0x10900,
// AsNormalized at 0x10914), else the head's X column (0x24/0x34/0x44, 0x10920); times
// machineGunHalfSweepDist (data +0x240, 0x1093C) and negated for side 1 (cmpwi/bne at 0x10940).
// The start is the centre minus it, the end the centre plus it (0x109A8..0x109C8).
// Every arm ran (dg22_gun2; the X-column fallback forced (force.sh, locator invalid)).
// @ fn_14_10798
void CDigitalGuardianHead::x10798_(CStateManager& mgr, int side) {
  const CPlayer& player = *mgr.GetPlayer(0);
  const CVector3f o = GetLctrTransform(x10A4_).GetTranslation();             // 0x10810
  const CVector3f now = player.GetAimPosition(mgr, 0.f);                       // 0x1084C
  const CVector3f later = player.GetAimPosition(mgr, 0.5f * mData.machineGunSweepDuration); // 0x10890
  const CVector3f centre = 0.5f * (((later + player.GetTranslation()) - now) + later); // 0x108A4
  const CVector3f axis(centre.GetY() - o.GetY(), -(centre.GetX() - o.GetX()), 0.f); // 0x108D0
  const CVector3f dir = axis.IsMagnitudeSafe() ? axis.AsNormalized()                 // 0x10900
                                               : GetTransform().GetColumn(kDX);       // 0x10920
  CVector3f offset = mData.machineGunHalfSweepDist * dir; // 0x1093C
  if (side == 1) {                                        // 0x10940
    offset = -offset;
  }
  x10A8_ = centre - offset; // 0x109A8..0x109B0
  x10B4_ = centre + offset; // 0x109B4..0x109C8
}

// fn_14_101E0: one machine-gun bullet. Only with the gun's weapon cached (0xE7C; 0x1020C..0x10258)
// and room for one more kWT_AI projectile (CanCreateProjectile with 0xB and 0x40, 0x1027C). LookAt
// from the gun's locator (0x10A4, GetLctrTransform at 0x10294) along dir (0x102E8). The 0x568-byte
// CEnergyProjectile (operator new at 0x10310, ctor at 0x103E0) is fn_14_10A2C's with the gun's
// weapon (0xE7C) and damage (0xE88): kWT_AI (li r6 0xB), material 0x21 (li r8), homing on the
// player, no visor effect (fn_14_1047C), false, true, false, 1.0f, 4.0f, 4.0f (.rodata
// 0x96C/0x998). When built it is added (AddObject at 0x1040C) and sound_MachineGun (data +0x228)
// plays at the shot's position along its Y column, not looped (PlayCustomSound at 0x10458).
// Every arm ran (dg22_gun2 incl. CanCreate false; TryCache false forced (force.sh)).
// @ fn_14_101E0
void CDigitalGuardianHead::x101E0_(CStateManager& mgr, const CVector3f& dir) {
  if (!xE7C_.Token().TryCache()) { // 0x1020C..0x10258
    return;
  }
  if (!mgr.CanCreateProjectile(GetUniqueId(), kWT_AI, 64)) { // 0x1027C
    return;
  }
  const CVector3f pos = GetLctrTransform(x10A4_).GetTranslation();             // 0x10294
  const CTransform4f xf = CTransform4f::LookAt(pos, pos + dir, CVector3f::Up()); // 0x102E8
  CEnergyProjectile* projectile = rs_new CEnergyProjectile(
      true, xE7C_.Token(), kWT_AI, xf, kMT_Character, xE7C_.GetDamage(), mgr.AllocateUniqueId(),
      GetCurrentAreaId(), GetUniqueId(), mgr.GetPlayer(0)->GetUniqueId(), 0, false,
      CVector3f::One(), CImpactVisorEffect::None(), false, true, false, 1.f, 4.f, 4.f); // 0x103E0
  // The null arm is an allocation failure only (rs_new); it never happens in a run.
  if (projectile != nullptr) {
    mgr.AddObject(projectile);                                                       // 0x1040C
    PlayCustomSound(xf.GetTranslation(), xf.GetColumn(kDY), mData.sound_MachineGun, false); // 0x10458
  }
}

// fn_14_1633C: the head rides the body (the cmpwi chain at 0x16344..0x16360). Activate (0):
// relaxed locomotion (li r4 1, SetLocomotionType at 0x16370), the turn direction from fn_14_13B38
// (0x1637C, stores 0x16388..0x16398) and flag 0x08 (rlwimi r0,r3,3,28,28 at 0x163A0). Update (1):
// the head turns (fn_14_11D64 at 0x163AC), then the turn direction is flattened (z 0.0f, .rodata
// 0x960) when that is safe (IsMagnitudeSafe at 0x163D0, beq at 0x163D8; stores 0x163E0..0x163F0),
// not normalised. Deactivate (2) and any other message do nothing.
// @ fn_14_1633C
void CDigitalGuardianHead::AttachedToBody(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    BodyController()->SetLocomotionType(pas::kLT_Relaxed); // 0x16370
    x1028_ = x13B38_();                                     // 0x1637C
    x10F4_ |= 0x08;                                         // 0x163A0
    break;
  case kStateMsg_Update: {
    x11D64_(mgr, dt); // 0x163AC
    const CVector3f flat(x1028_.GetX(), x1028_.GetY(), 0.f); // .rodata 0x960, 0x163C4..0x163CC
    if (flat.IsMagnitudeSafe()) {                            // 0x163D0
      x1028_ = flat;                                         // 0x163E0..0x163F0
    }
    break;
  }
  default:
    break;
  }
}

// fn_14_15CE8: the bomb's knock-back (the cmpwi chain at 0x15CF0..0x15D18). Activate (0): Ready
// (0x6B4 = 1, 0x15D24). Update (1): when a knock-back may be issued (CanIssueCommand with 6 =
// kAS_KnockBack, 0x15D38) one is sent (DeliverCmd 4 at 0x15DA4) along the Y column
// (0x28/0x38/0x48) with severity 2 (li r6 2 at 0x15D64), no anim (-1) and no restart (stb 0).
// Deactivate (2): NotReady (0x6B4 = 0, 0x15E00). Messages 3+ do nothing (bge at 0x15D18).
// @ fn_14_15CE8
void CDigitalGuardianHead::BombedReaction(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready); // 0x15D24
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_KnockBack)) { // 0x15D38
      BodyController()->CommandMgr().DeliverCmd(
          CBCKnockBackCmd(GetTransform().GetColumn(kDY), pas::kS_Two)); // 0x15DA4
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady); // 0x15E00
    break;
  default:
    break;
  }
}

// fn_14_158F8: the head flies to the retreat platform (0x1038) and lines up with it (the cmpwi
// chain at 0x1592C..0x1594C). Activate (0): Ready (0x6B4 = 1, 0x1595C); with the platform
// (ObjectById at 0x1596C, TCastToPtr<CScriptPlatform> at 0x15970, null test 0x15978) and a speed
// above 1.1920929e-07f (.rodata 0x9E4; data +0x1C maxStunnedLinearVelocity at 0x7DC; fcmpo/ble at
// 0x159D0, so a NaN speed goes on) the turn speed (SetTurnSpeed at 0x15A30) is
// 1.1f (.rodata 0x9E8) * 57.295776f (.rodata 0x9EC) * the angle between the head's Y column and the
// platform's (GetAngleDiff at 0x159F8) over the flight time |platform - head| / speed (Magnitude at
// 0x15A04, fdivs at 0x15A14 / 0x15A2C). Update (1): with the platform (0x15A48..0x15A54) the step
// is speed * dt; when it reaches the platform (fcmpo/bge at 0x15AB0) the state is Over (0x15B14)
// and the head is put on it (SetTranslation at 0x15B1C), else it moves one step toward it in its
// own frame (AsNormalized 0x15AC0, TransposeRotate 0x15AF4, MoveInOneFrameOR 0x15B04); then it
// faces the platform's Y column (FaceDirection at 0x15B44). Deactivate (2): NotReady (0x15B50)
// and the turn speed back to the ctor's (0xFE4, SetTurnSpeed at 0x15B5C).
// @ fn_14_158F8
void CDigitalGuardianHead::SeekMotionPath(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate: {
    mAnimationState.SetState(CAnimationState::kAS_Ready); // 0x1595C
    const CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(mgr.ObjectById(x1038_)); // 0x15970
    if (plat == nullptr) {
      break;
    }
    const float speed = mData.maxStunnedLinearVelocity; // 0x7DC
    if (speed > 1.1920929e-07f) {                       // 0x159D0..0x159D4 (ble: a NaN skips)
      const CVector3f delta = plat->GetTranslation() - GetTranslation(); // 0x159D8..0x159F4
      const float angle = CVector3f::GetAngleDiff(GetTransform().GetColumn(kDY),
                                                  plat->GetTransform().GetColumn(kDY)); // 0x159F8
      const float time = delta.Magnitude() / speed;                                   // 0x15A14
      BodyController()->SetTurnSpeed(1.1f * (57.295776f * angle) / time);             // 0x15A30
    }
    break;
  }
  case kStateMsg_Update: {
    const CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(mgr.ObjectById(x1038_)); // 0x15A4C
    if (plat == nullptr) {
      break;
    }
    const CTransform4f xf = plat->GetTransform(); // 0x15A6C
    const CVector3f delta = xf.GetTranslation() - GetTranslation();
    const float step = mData.maxStunnedLinearVelocity * dt; // 0x15AA8
    if (!(step < delta.Magnitude())) {                      // 0x15AB0..0x15AB4 bge (a NaN too)
      mAnimationState.SetState(CAnimationState::kAS_Over); // 0x15B14
      SetTranslation(xf.GetTranslation());                 // 0x15B1C
    } else {
      MoveInOneFrameOR(GetTransform().TransposeRotate(delta.AsNormalized() * step), dt); // 0x15B04
    }
    BodyController()->FaceDirection(xf.GetColumn(kDY), dt); // 0x15B44
    break;
  }
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady); // 0x15B50
    BodyController()->SetTurnSpeed(xFE4_);                    // 0x15B5C
    break;
  default:
    break;
  }
}

// fn_14_16A50: the state message picks the work (the cmpwi chain at 0x16A74..0x16A9C).
// Activate (0): ready (0x6B4 = 1, 0x16AA8), flag 0x20 (the rlwimi at 0x16AB4) and the turn
// direction from fn_14_13B38 (0x16ABC, stores 0x16AC4..0x16AD4). Update (1): when a projectile
// attack can be issued (ShouldRetry with 0x12 = kAS_ProjectileAttack, 0x16AE8) it is sent
// (DeliverCmd 6 at 0x16B64): severity 2 (0x16B20), the target the head's position (0x54..0x5C)
// plus its transform's Y column (0x28, 0x38, 0x48), no blend (0x16B60). Otherwise with no lasers
// (0x107C == 0, 0x16BBC) the sweep starts (fn_14_FB70, 0x16BC8), else the lasers move
// (fn_14_F6A4, 0x16BDC). Then the head turns (fn_14_11D64, 0x16BEC).
// Deactivate (2): not ready (0x6B4 = 0, 0x16CB0); while the body is in the projectile attack
// (0x37C == 0x12, 0x16CBC) it is aborted (command 0xD = kBSC_AbortScripted, 0x16CC8, 0x16CE0);
// the lasers go (fn_14_FAEC, 0x16CF8), flag 0x20 clears (0x16D04) and the turn direction is the
// zero vector (0x16D18..0x16D2C).
// The player's lock-on ring (CPlayer +0x14C4 id, +0x14C6 count): after the turn on Update
// (0x16BF0..0x16C8C), unless flag 0x01 (clrlwi. 31 / bne at 0x16BF8..0x16BFC), the count is
// 3.0f (.rodata 0x9F0) times v = 1.0f - (0x1094 - 1.0f) / (0x1090 - 1.0f) (.rodata 0x96C,
// 0x16C08..0x16C20), v kept only when 1.0f >= v (fcmpo/bge at 0x16C24, so a NaN v is 1.0f) and
// raised to 0.0f when 0.0f >= v (0x16C44), truncated (fctiwz at 0x16C5C), at most 3 (cmpwi/bgt at
// 0x16C68) and at least 1 (cmpwi/ble at 0x16C74), with the head's id (0x16C84/0x16C8C). With flag
// 0x01 (0x16C94..0x16CA4) and on Deactivate (0x16D30..0x16D3C) the count is 0 and the id invalid.
// Every arm ran, msg 3 being the unhandled one (tools/cov/specs/digitalguardian_shots.con).
// @ fn_14_16A50
void CDigitalGuardianHead::ChargeAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready); // 0x16AA8
    x10F4_ |= 0x20;                                        // 0x16AB4
    x1028_ = x13B38_();                                    // 0x16ABC
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_ProjectileAttack)) {
      BodyController()->CommandMgr().DeliverCmd(CBCProjectileAttackCmd(
          pas::kS_Two, GetTranslation() + GetTransform().GetColumn(kDY), false)); // 0x16B64
    } else if (x107C_.empty()) { // 0x16BB8
      xFB70_(mgr);               // 0x16BC8
    } else {
      xF6A4_(mgr, dt); // 0x16BDC
    }
    x11D64_(mgr, dt); // 0x16BEC
    if ((x10F4_ & 0x01) == 0) { // 0x16BF8
      float v = 1.f - (x1094_ - 1.f) / (x1090_ - 1.f); // 0x16C08..0x16C20
      v = 1.f < v ? 1.f : v;                           // 0x16C24 bge to the fmr of v (NaN too)
      v = 0.f < v ? v : 0.f;                           // 0x16C44 bge to the fmr of 0 (NaN too)
      int count = static_cast< int >(3.f * v);         // .rodata 0x9F0, 0x16C5C
      if (count > 3) {                                 // 0x16C68
        count = 3;
      }
      if (count <= 1) { // 0x16C74
        count = 1;
      }
      mgr.GetPlayer(0)->SetEnemyLockOn(GetUniqueId(), static_cast< char >(count)); // 0x16C84
    } else {
      mgr.GetPlayer(0)->SetEnemyLockOn(kInvalidUniqueId, 0); // 0x16C9C..0x16CA4
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady); // 0x16CB0
    if (BodyController()->GetCurrentStateId() == pas::kAS_ProjectileAttack) { // 0x16CBC
      BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_AbortScripted)); // 0x16CE0
    }
    xFAEC_(mgr);                    // 0x16CF8
    x10F4_ &= ~0x20;                // 0x16D04
    x1028_ = CVector3f::Zero();     // 0x16D18..0x16D2C
    mgr.GetPlayer(0)->SetEnemyLockOn(kInvalidUniqueId, 0); // 0x16D30..0x16D3C
    break;
  default:
    break;
  }
}

// fn_14_FAEC: every laser id (0x1080, count 0x107C; the loop at 0xFB1C..0xFB44) is deleted
// (DeleteObjectRequest at 0xFB2C) and the count is 0 (0xFB4C).
// Every arm ran (the XDEL, a sweep's start and Deactivate; tools/cov/specs/digitalguardian_charge.con).
// @ fn_14_FAEC
void CDigitalGuardianHead::xFAEC_(CStateManager& mgr) {
  for (int i = 0; i < x107C_.size(); ++i) {
    mgr.DeleteObjectRequest(x107C_[i]);
  }
  x107C_.clear();
}

// fn_14_17BBC: a projectile event (type 0, the beq at 0x17BF4) never reaches the base: by the
// projectile attack at 0x10D0 (the cmpwi chain at 0x17C00..0x17C1C) the pulse fires (0,
// fn_14_111E0 at 0x17C28), the charge's shot fires and its lasers reset (1, fn_14_10CA0 at
// 0x17C34 and fn_14_FA50 at 0x17C40) or the missiles fire (2, fn_14_10A2C at 0x17C4C); any other
// attack does nothing, and the event is handled either way (li 1 at 0x17C50). Every other event
// goes to CPatterned::DoUserAnimEvent (0x17C70). Each spawner gets the node's locator name (node
// +0x30, the addi r5 at 0x17C24/0x17C30/0x17C48).
// Every arm ran (each attack, an unknown one and another event;
// tools/cov/specs/digitalguardian_shots.con).
// @ fn_14_17BBC
void CDigitalGuardianHead::DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node,
                                           EUserEventType type, float dt) {
  if (type != kUE_Projectile) {
    CPatterned::DoUserAnimEvent(mgr, node, type, dt);
    return;
  }
  switch (x10D0_) {
  case 0:
    x111E0_(mgr, node.GetLocatorName()); // 0x17C28
    break;
  case 1:
    x10CA0_(mgr, node.GetLocatorName()); // 0x17C34
    xFA50_(mgr);                         // 0x17C40
    break;
  case 2:
    x10A2C_(mgr, node.GetLocatorName()); // 0x17C4C
    break;
  default:
    break;
  }
}

// fn_14_FA50: each laser found (the ids at 0x1080, count 0x107C; the loop at 0xFA80..0xFAC8;
// ObjectById at 0xFA90, the null test at 0xFA94) resets (vtable 0x90, ResetBeam with false;
// 0xFAB0). The ids stay.
// Retail calls the vtable on whatever the id names; the host casts to CPlasmaProjectile.
// Every arm ran (lasers found, one deleted, none; tools/cov/specs/digitalguardian_shots.con).
// @ fn_14_FA50
void CDigitalGuardianHead::xFA50_(CStateManager& mgr) {
  for (int i = 0; i < x107C_.size(); ++i) {
    if (CPlasmaProjectile* laser = TCastToPtr< CPlasmaProjectile >(mgr.ObjectById(x107C_[i]))) {
      laser->ResetBeam(mgr, false);
    }
  }
}

// fn_14_111E0: the annihilator pulse, fired from the event's locator (GetLctrTransform at
// 0x1120C) at the player's aim position (vtable 0x54 with 0.0f, .rodata 0x960; 0x1124C). The
// direction's horizontal part (z 0.0f, 0x11298) normalised, or the head's facing (fn_14_13B38,
// 0x112C4) when it is not safe (0x112A4), is the reference; a direction more than 0.9599311f
// (.rodata 0x9B8) from it (fcmpo/ble at 0x112FC, taken on a NaN: a NaN angle keeps it) is the reference slerped
// toward the normalised direction by that angle (0x11320). LookAt from the locator along it
// (0x11380). No cache or projectile-count test. The 0x568-byte CEnergyProjectile (ctor at 0x11478):
// active (li r4 1), the pulse's weapon (0xE04), kWT_AI (li r6 0xB), material 0x21 (kMT_Character,
// li r8), the pulse's damage (0xE10), a new id (0x113F0), the head's area (0x4) and id (0x8), the
// player as homing target (0x113CC), attributes 0, not underwater, scale one (sOneVector), no
// visor effect (fn_14_1047C), then false, true, false and 1.0f, 4.0f, 4.0f (.rodata 0x96C/0x998);
// added when built (0x11494, AddObject at 0x114A4).
// Every arm ran: the head on the player (place ... player 0) takes the unsafe horizontal part and
// the slerp (tools/cov/specs/digitalguardian_shots.con).
// @ fn_14_111E0
void CDigitalGuardianHead::x111E0_(CStateManager& mgr, const rstl::string& locator) {
  const CVector3f pos = GetLctrTransform(locator).GetTranslation();
  const CPlayer& player = *mgr.GetPlayer(0);
  CVector3f dir = player.GetAimPosition(mgr, 0.f) - pos;
  const CVector3f flat(dir.GetX(), dir.GetY(), 0.f);
  const CVector3f ref = flat.IsMagnitudeSafe() ? flat.AsNormalized() : x13B38_();
  if (CVector3f::GetAngleDiff(dir, ref) > 0.9599311f) {
    dir = CVector3f::Slerp(ref, dir.AsNormalized(), CRelAngle::FromRadians(0.9599311f));
  }
  const CTransform4f xf = CTransform4f::LookAt(pos, pos + dir, CVector3f::Up());
  CEnergyProjectile* projectile = rs_new CEnergyProjectile(
      true, xE04_.Token(), kWT_AI, xf, kMT_Character, xE04_.GetDamage(), mgr.AllocateUniqueId(),
      GetCurrentAreaId(), GetUniqueId(), player.GetUniqueId(), 0, false, CVector3f::One(),
      CImpactVisorEffect::None(), false, true, false, 1.f, 4.f, 4.f);
  // The null arm is an allocation failure only (rs_new); it never happens in a run.
  if (projectile != nullptr) {
    mgr.AddObject(projectile);
  }
}

// The charge shot's projectile (guessed name; vtable lbl_14_data_1034, 0x568 bytes like its base,
// no members of its own). Slots after the RTTI pair: 2 dtor fn_14_1AAC4 (the base dtors only:
// fn_14_19908/1998C are the module's copies of ~CEnergyProjectile/~CGameProjectile), 5 Think
// fn_14_19784, 15 GetHealthInfo fn_14_10C (the CActor inline, HealthInfo through slot 14), 38
// GetImpactParticle fn_14_1AB24 (an empty optional), 39 GetExplosionNormal fn_14_1AB30
// (sUpVector). Every other slot is a base symbol. Only Think differs from CEnergyProjectile.
namespace {
class CGuardianProjectile : public CEnergyProjectile {
public:
  CGuardianProjectile(bool active, const TToken< CWeaponDescription >& description,
                      EWeaponType type, const CTransform4f& xf, EMaterialTypes excludeMaterial,
                      const CDamageInfo& damage, TUniqueId uid, TAreaId areaId, TUniqueId owner,
                      TUniqueId homingTarget, uint attribs, bool underwater,
                      const CVector3f& scale, const CImpactVisorEffect& visorEffect);
  // fn_14_1AAC4: the vtable lbl_14_data_1034 (0x1AAF0), then fn_14_19908 (~CEnergyProjectile).
  // @ fn_14_1AAC4
  ~CGuardianProjectile() override {}
  void Think(float dt, CStateManager& mgr) override;
  // fn_14_1AB24: an empty optional (li r0 0; stb r0 0xC(r3) at 0x1AB24).
  // @ fn_14_1AB24
  rstl::optional_object< TLockedToken< CGenDescription > >
  GetImpactParticle(CStateManager& mgr) override {
    return rstl::optional_object_null();
  }
  // fn_14_1AB30: CVector3f::sUpVector (0x1AB30..0x1AB48).
  // @ fn_14_1AB30
  CVector3f GetExplosionNormal() const override { return CVector3f::Up(); }
};

// fn_14_19838: the CEnergyProjectile ctor (0x198E0) with the caller's arguments and then false
// (li r12 0, stw 0x24 at 0x198D4), true (li r0 1, stw 0x28 at 0x198D8), false (stw 0x2C at
// 0x198DC), 1.0f (.rodata 0x96C) and 4.0f, 4.0f (.rodata 0x998, f2/f3); then the vtable
// lbl_14_data_1034 (0x198F0).
// @ fn_14_19838
CGuardianProjectile::CGuardianProjectile(bool active,
                                         const TToken< CWeaponDescription >& description,
                                         EWeaponType type, const CTransform4f& xf,
                                         EMaterialTypes excludeMaterial, const CDamageInfo& damage,
                                         TUniqueId uid, TAreaId areaId, TUniqueId owner,
                                         TUniqueId homingTarget, uint attribs, bool underwater,
                                         const CVector3f& scale,
                                         const CImpactVisorEffect& visorEffect)
: CEnergyProjectile(active, description, type, xf, excludeMaterial, damage, uid, areaId, owner,
                    homingTarget, attribs, underwater, scale, visorEffect, false, true, false,
                    1.f, 4.f, 4.f) {}

// fn_14_19784 (slot 5, Think): a homing target (+0x3F0, read at 0x197AC) that is the player
// (TCastToPtr at 0x197C0, null test 0x197C8) in the morph ball (spawned state +0x390 == 0 at
// 0x197D4, then the transition state +0x38C; == 1 at 0x197E4) with the boost shield up
// (+0x1174, IsBoostShieldActive at 0x197F0, beq at 0x197F8) is dropped: kInvalidUniqueId stored
// at 0x19808. Then CEnergyProjectile::Think (0x19818).
// @ fn_14_19784
void CGuardianProjectile::Think(float dt, CStateManager& mgr) {
  if (const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mHomingTargetId))) {
    const CPlayer::EPlayerMorphBallState morph =
        player->GetSpawnedMorphballState() == CPlayer::kMS_Unmorphed
            ? player->GetMorphballTransitionState()
            : CPlayer::kMS_Unmorphed;
    if (morph == CPlayer::kMS_Morphed && player->GetMorphBall()->IsBoostShieldActive()) {
      mHomingTargetId = kInvalidUniqueId;
    }
  }
  CEnergyProjectile::Think(dt, mgr);
}
} // namespace

// fn_14_10CA0: the annihilator charge's shot. Only with its weapon cached (the TCachedToken test
// at 0x10CC8..0x10D18) and room for one more kWT_AI projectile of this head (CanCreateProjectile
// with 0xB and 16 = li r6 0x10, 0x10D38). From the event's locator (0x10D54) toward the point the
// lasers circle (0x1098..0x10A0, 0x10D80..0x10D94), aimed like fn_14_111E0 (0x10DB0..0x10E94).
// The visor effect is a blur {1, 4.0f (.rodata 0x998), data +0x1AC (0x96C)} (fn_14_11170 at
// 0x10ECC) with the low-pass filter {data +0x1B0 (0x970), data +0x1AC} (fn_14_11044 at 0x10F08).
// The homing target is none with flag 0x01 (clrlwi. 31 / beq at 0x10F40) and the player
// otherwise (0x10F54). The projectile is fn_14_19838's (0x10FEC): CGuardianProjectile with the
// charge's weapon (0xE2C), kWT_AI, material 0x21, the charge's damage (0xE38), attributes 0,
// not underwater, scale one.
// When built it gets attribute 0x1000 (kPA_BigStrike; lwz/ori/stw 0x158 at 0x10FFC..0x11010)
// with data +0x1AC as its damage duration (stfs 0x1C0 at 0x11014): SetDamageDuration; added at
// 0x11018. The pair {0, data +0x1AC} at 0x10F20/0x10F2C is stored and never read.
// The object is the module's CGuardianProjectile (fn_14_19838 above, vtable lbl_14_data_1034).
// Every arm ran (tools/cov/specs/digitalguardian_shots.con; the TryCache-false arm forced,
// tools/cov/specs/digitalguardian_shots_force.py).
// @ fn_14_10CA0
void CDigitalGuardianHead::x10CA0_(CStateManager& mgr, const rstl::string& locator) {
  if (!xE2C_.Token().TryCache()) {
    return;
  }
  if (!mgr.CanCreateProjectile(GetUniqueId(), kWT_AI, 16)) {
    return;
  }
  const CPlayer& player = *mgr.GetPlayer(0);
  const CVector3f pos = GetLctrTransform(locator).GetTranslation();
  CVector3f dir = x1098_ - pos;
  const CVector3f flat(dir.GetX(), dir.GetY(), 0.f);
  const CVector3f ref = flat.IsMagnitudeSafe() ? flat.AsNormalized() : x13B38_();
  if (CVector3f::GetAngleDiff(dir, ref) > 0.9599311f) { // 0x10E10, fcmpo/ble: a NaN keeps it
    dir = CVector3f::Slerp(ref, dir.AsNormalized(), CRelAngle::FromRadians(0.9599311f));
  }
  const CTransform4f xf = CTransform4f::LookAt(pos, pos + dir, CVector3f::Up());
  const float amount = mData.unknown_0xff7688bf; // 0x96C
  const CImpactVisorEffect visor(
      rstl::optional_object< CImpactVisorEffect::SParticleEffect >(),
      rstl::optional_object< CImpactVisorEffect::SBlurEffect >(
          CImpactVisorEffect::SBlurEffect(1, 4.f, amount)),
      rstl::optional_object< rstl::pair< int, float > >(
          rstl::pair< int, float >(mData.unknown_0x12ebb390, amount)));
  const TUniqueId homing = (x10F4_ & 0x01) != 0 ? kInvalidUniqueId : player.GetUniqueId();
  CGuardianProjectile* projectile = rs_new CGuardianProjectile(
      true, xE2C_.Token(), kWT_AI, xf, kMT_Character, xE2C_.GetDamage(), mgr.AllocateUniqueId(),
      GetCurrentAreaId(), GetUniqueId(), homing, 0, false, CVector3f::One(), visor);
  // The null arm is an allocation failure only (rs_new); it never happens in a run.
  if (projectile != nullptr) {
    projectile->SetDamageDuration(mData.unknown_0xff7688bf);
    mgr.AddObject(projectile);
  }
}

// fn_14_10A2C: the lock-on missiles. Only with their weapon cached (0x10A58..0x10AA8) and room for
// one more kWT_AI projectile (CanCreateProjectile with 0xB and 16, 0x10AC8). From the event's
// locator (0x10AE0) along the head's facing (fn_14_13B38 at 0x10B10; LookAt at 0x10B58). The
// CEnergyProjectile (ctor at 0x10C50) is fn_14_111E0's with the missiles' weapon (0xE54) and
// damage (0xE60): homing on the player (0x10BA4), no visor effect (fn_14_1047C at 0x10B90);
// added when built (0x10C6C, AddObject at 0x10C7C).
// Every arm ran (tools/cov/specs/digitalguardian_shots.con; the TryCache-false arm forced,
// tools/cov/specs/digitalguardian_shots_force.py, CanCreateProjectile false with 20 shots out).
// @ fn_14_10A2C
void CDigitalGuardianHead::x10A2C_(CStateManager& mgr, const rstl::string& locator) {
  if (!xE54_.Token().TryCache()) {
    return;
  }
  if (!mgr.CanCreateProjectile(GetUniqueId(), kWT_AI, 16)) {
    return;
  }
  const CVector3f pos = GetLctrTransform(locator).GetTranslation();
  const CTransform4f xf = CTransform4f::LookAt(pos, pos + x13B38_(), CVector3f::Up());
  CEnergyProjectile* projectile = rs_new CEnergyProjectile(
      true, xE54_.Token(), kWT_AI, xf, kMT_Character, xE54_.GetDamage(), mgr.AllocateUniqueId(),
      GetCurrentAreaId(), GetUniqueId(), mgr.GetPlayer(0)->GetUniqueId(), 0, false,
      CVector3f::One(), CImpactVisorEffect::None(), false, true, false, 1.f, 4.f, 4.f);
  // The null arm is an allocation failure only (rs_new); it never happens in a run.
  if (projectile != nullptr) {
    mgr.AddObject(projectile);
  }
}

// Host: the console's `var <id> <name> [value]` for the head: dghd.attack is the projectile
// attack at 0x10D0 (0 pulse, 1 charge, 2 missiles, 3 gun), dghd.flags the flag byte 0x10F4,
// dghd.sweeptime the lasers' sweep time 0x1090 (whole seconds).
bool CDigitalGuardianHead::PortConsoleVar(CEntity& ent, const char* name, const int* value,
                                          int& current) {
  CDigitalGuardianHead* head = TCastToPtr< CDigitalGuardianHead >(&ent);
  if (head == nullptr) {
    return false;
  }
  if (strcmp(name, "dghd.attack") == 0) {
    if (value != nullptr) {
      head->x10D0_ = *value;
    }
    current = head->x10D0_;
    return true;
  }
  if (strcmp(name, "dghd.flags") == 0) {
    if (value != nullptr) {
      head->x10F4_ = static_cast< uchar >(*value);
    }
    current = head->x10F4_;
    return true;
  }
  if (strcmp(name, "dghd.sweeptime") == 0) {
    if (value != nullptr) {
      head->x1090_ = static_cast< float >(*value);
    }
    current = static_cast< int >(head->x1090_);
    return true;
  }
  // data +0x1C (0x7DC): SeekMotionPath's flight speed, StunnedReaction's drift speed.
  if (strcmp(name, "dghd.stunspeed") == 0) {
    if (value != nullptr) {
      head->mData.maxStunnedLinearVelocity = static_cast< float >(*value);
    }
    current = static_cast< int >(head->mData.maxStunnedLinearVelocity);
    return true;
  }
  // Host: calls ProjectileInfo (fn_14_17840) and reports which weapon it picked (0 pulse 0xE04,
  // 1 charge 0xE2C, 2 missiles 0xE54, 3 gun 0xE7C).
  if (strcmp(name, "dghd.projinfo") == 0) {
    const CProjectileInfo* info = head->ProjectileInfo();
    current = info == &head->xE2C_ ? 1 : info == &head->xE54_ ? 2 : info == &head->xE7C_ ? 3 : 0;
    return true;
  }
  return false;
}

namespace {
const bool sDghdConsoleVars = PortConsole::RegisterVar(&CDigitalGuardianHead::PortConsoleVar);
} // namespace

// fn_14_FB70: the lasers go (fn_14_FAEC, 0xFBB8) and the phase is 0.0f (.rodata 0x960, 0xFBCC).
// The sweep time is the current animation's (the body controller's 0x58C) projectile event
// (type 0, 0xFBC8; GetTimeOfUserEventForAnimation at 0xFBE0) when it differs from the infinite
// time {4, 1.0f} (.data 0x710/0x714, the __ne__ at 0xFC1C), else the animation's time left
// (0xFC34); it is stored at 0x1090 and 0x1094 (0xFC38, 0xFC54). The centre is half the player's
// aim position (vtable 0x54 with 0.0f, 0xFC6C) plus its position (0x54..0x5C; .rodata 0x9A0,
// 0.5f, 0xFCA4..0xFCB4). From the laser locator (0x1088, GetLctrTransform at 0xFCC0) a LookAt to
// the centre (up vector, 0xFCF0) turns the offsets (10.0f * cos, 0.0f, 10.0f * sin) of the phase
// (.rodata 0x980; FastSinR/FastCosR at 0xFD24/0xFD30, Rotate at 0xFD50), and three lasers fire
// from the locator at centre + offset (fn_14_FE14 at 0xFD94; the cmpwi 3 at 0xFDA0), the phase
// growing by 2.0943952f (.rodata 0x9B4) each. Flag 0x01 clears (0xFDB0) and the centre is kept at
// 0x1098 (0xFDBC..0xFDCC).
// Every arm ran (digitalguardian_charge.con): the projectile event's time on the attack's
// animation, the time left on an older one (the back-to-back statecalls).
// @ fn_14_FB70
void CDigitalGuardianHead::xFB70_(CStateManager& mgr) {
  xFAEC_(mgr);
  x108C_ = 0.f;
  const CAnimData& animData = *GetModelData()->GetAnimationData();
  const CCharAnimTime event =
      animData.GetTimeOfUserEventForAnimation(GetBodyController()->GetCurrentAnimId(), kUE_Projectile);
  const CCharAnimTime infinite(CCharAnimTime::kT_Infinity, 1.f);
  x1090_ = event != infinite ? event.GetSeconds() : GetBodyController()->GetAnimTimeRemaining();
  x1094_ = x1090_;
  const CPlayer& player = *mgr.GetPlayer(0);
  const CVector3f centre = (player.GetAimPosition(mgr, 0.f) + player.GetTranslation()) * 0.5f;
  const CVector3f from = GetLctrTransform(x1088_).GetTranslation();
  const CTransform4f look = CTransform4f::LookAt(from, centre, CVector3f::Up());
  float phase = x108C_;
  for (int i = 0; i < 3; ++i) {
    const float s = 10.f * CMath::FastSinR(phase);
    const float c = 10.f * CMath::FastCosR(phase);
    xFE14_(mgr, from, centre + look.Rotate(CVector3f(c, 0.f, s)));
    phase += 2.0943952f;
  }
  x10F4_ &= ~0x01;
  x1098_ = centre;
}

// fn_14_FE14: at most three lasers (the cmpwi 3 at 0xFE38). The lock-on beams' info (data
// +0x1B8) becomes a CBeamInfo with the attributes 0x291 (fn_80239B64, the DOL's TLdrToBeamInfo,
// 0xFE4C; fn_14_1008C copies it). A new id (AllocateUniqueId at 0xFE64) gets the 0x6A8-byte
// CPlasmaProjectile "RezBitCuttingLaser" (.rodata 0xA00 + 0x507; new at 0xFE88, ctor at 0xFF54):
// the weapon at 0x1054, weapon type 2 (0xFF4C), the identity transform, material 0x14
// (kMT_ProjectilePassthrough, 0xFF44), a default damage (fn_14_FFFC), the area (0x4) and this
// actor as owner (0x8), a default CWeaponAssetInfo (8 entries of -1, 0xFE94..0xFED0), drawn owner
// first (1, 0xFF48) and the attributes 0x20000 (0xFF2C). When it exists (0xFF74) it fires (vtable
// 0x94, Fire with false; 0xFFB8) along LookAt(from, to, up) (0xFF8C), is added (AddObject at
// 0xFFC4) and its id appended (0xFFD8..0xFFE4).
// Every live arm ran (digitalguardian_charge.con). The count >= 3 return is dead: the only caller
// (fn_14_FB70) empties the list (fn_14_FAEC at 0xFBB8) and calls three times.
// @ fn_14_FE14
void CDigitalGuardianHead::xFE14_(CStateManager& mgr, const CVector3f& from,
                                  const CVector3f& to) {
  if (x107C_.size() >= 3) {
    return;
  }
  const CBeamInfo beamInfo = TLdrToBeamInfo(mData.lockOnBeams, 0x291);
  const TUniqueId id = mgr.AllocateUniqueId();
  CPlasmaProjectile* laser = rs_new CPlasmaProjectile(
      x1054_.Token(), rstl::string_l("RezBitCuttingLaser"), EWeaponType(2), beamInfo,
      CTransform4f::Identity(), kMT_ProjectilePassthrough, CDamageInfo(), id, GetCurrentAreaId(),
      GetUniqueId(), CWeaponAssetInfo(), true, 0x20000);
  // The null arm is an allocation failure only (rs_new); it never happens in a run.
  if (laser != nullptr) {
    laser->Fire(CTransform4f::LookAt(from, to, CVector3f::Up()), mgr, false);
    mgr.AddObject(laser);
    x107C_.push_back(id);
  }
}

// fn_14_F6A4: nothing without lasers (0xF6E8) or with the sweep time at most 0.0f (.rodata 0x960,
// the fcmpo/ble at 0xF6FC, so a NaN time goes on). The phase grows by 2.0943952f * dt (.rodata
// 0x9B4, the fmadds at 0xF71C) and the time left shrinks by dt (0xF728). The centre and the
// locator's LookAt are fn_14_FB70's (0xF744..0xF7D4). The radius is 10.0f (.rodata 0x980) *
// (left - 1.0f) / time (.rodata 0x96C; 0xF7F4..0xF804), raised to 0.0f (the fcmpo/bge at 0xF808,
// a NaN radius stays). At a radius of at most 0.0f (the cror at 0xF828) the lasers but the first
// go, last first (0xF838..0xF8A0): each found one resets (vtable 0x90, ResetBeam with false;
// 0xF870) and is deleted (0xF888); the count drops either way (0xF894). Then, without flag 0x01,
// one laser left and a boosting player (0x390 zero, 0x38C == 1 and the morph ball's boost shield,
// 0xF8BC..0xF8E8) set flag 0x01 (0xF8F4); without flag 0x01 the circled point follows the centre
// (0xF908..0xF91C). Each laser found (0xF92C..0xFA18) is moved (vtable 0x8C, UpdateFx with dt;
// 0xF9F4) along a LookAt from the locator to the point plus the turned offset (radius * cos,
// 0.0f, radius * sin) of the phase (0xF948..0xF9C8), the phase growing by 2.0943952f (0xF9FC)
// only for a laser found.
// Retail calls the vtable on whatever the id names; the host casts to CPlasmaProjectile.
// Every arm ran (tools/cov/specs/digitalguardian_shots.con) but the no-laser return: dead, as the
// only call (fn_14_16A50 at 0x16BDC) is taken only with lasers (0x107C != 0, the bne at 0x16BBC).
// @ fn_14_F6A4
void CDigitalGuardianHead::xF6A4_(CStateManager& mgr, float dt) {
  if (x107C_.empty() || x1090_ <= 0.f) {
    return;
  }
  x108C_ = 2.0943952f * dt + x108C_;
  x1094_ = x1094_ - dt;
  const CPlayer& player = *mgr.GetPlayer(0);
  const CVector3f centre = (player.GetAimPosition(mgr, 0.f) + player.GetTranslation()) * 0.5f;
  const CVector3f from = GetLctrTransform(x1088_).GetTranslation();
  const CTransform4f look = CTransform4f::LookAt(from, centre, CVector3f::Up());
  float radius = 10.f * (x1094_ - 1.f) / x1090_;
  if (0.f >= radius) {
    radius = 0.f;
  }
  float phase = x108C_;
  if (radius <= 0.f) {
    while (x107C_.size() > 1) {
      if (CPlasmaProjectile* laser =
              TCastToPtr< CPlasmaProjectile >(mgr.ObjectById(x107C_[x107C_.size() - 1]))) {
        laser->ResetBeam(mgr, false);
        mgr.DeleteObjectRequest(laser->GetUniqueId());
      }
      x107C_.pop_back();
    }
  }
  if ((x10F4_ & 0x01) == 0 && x107C_.size() == 1) {
    const CPlayer::EPlayerMorphBallState morph =
        player.GetSpawnedMorphballState() == CPlayer::kMS_Unmorphed
            ? player.GetMorphballTransitionState()
            : CPlayer::kMS_Unmorphed;
    if (morph == CPlayer::kMS_Morphed && player.GetMorphBall()->IsBoostShieldActive()) {
      x10F4_ |= 0x01;
    }
  }
  if ((x10F4_ & 0x01) == 0) {
    x1098_ = centre;
  }
  for (int i = 0; i < x107C_.size(); ++i) {
    if (CPlasmaProjectile* laser = TCastToPtr< CPlasmaProjectile >(mgr.ObjectById(x107C_[i]))) {
      const float s = radius * CMath::FastSinR(phase);
      const float c = radius * CMath::FastCosR(phase);
      const CVector3f to = x1098_ + look.Rotate(CVector3f(c, 0.f, s));
      laser->UpdateFx(CTransform4f::LookAt(from, to, CVector3f::Up()), dt, mgr);
      phase += 2.0943952f;
    }
  }
}

// fn_14_16F74: the projectile telegraph. Activate (0): the animation state Ready (the store at
// 0x16FCC), bit 0x20 of 0x10F4 set (0x16FD8) and the turn direction 0x1028 the fallback one
// (fn_14_13B38, 0x16FE0). Update (1): the head turns to its target (fn_14_11D64, 0x17034) until
// the state's time reaches block 6's projectileTelegraphTime (fn_14_10120's +4, the fcmpo at
// 0x17020), then the state is Over (0x1703C). Deactivate (2): NotReady, bit 0x20 cleared and the
// turn direction zeroed (sZeroVector, 0x17060).
// The arms (local probes, area 53 (09_cliff_temple_dark), dg_steps_head_stun.sh): Activate, Update before the telegraph
// time (76 frames), Update at it (Over) and Deactivate all ran, five telegraphs in one fight.
// @ fn_14_16F74
void CDigitalGuardianHead::ProjectileTelegraph(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) { // the cmpwi chain at 0x16F98..0x16FBC
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready); // stw 1 -> 0x6B4 at 0x16FCC
    x10F4_ |= 0x20;                                        // rlwimi 5, 26, 26 at 0x16FD8
    x1028_ = x13B38_();                                    // 0x16FE0, stores 0x16FE8..0x16FF8
    break;
  case kStateMsg_Update: {
    const SLdrDigitalGuardianHeadStruct* attack = x10120_(mgr); // 0x17000
    if (!(mStateMachine->GetTime() < attack->projectileTelegraphTime)) { // vtable 0x30 of 0x350 at
                                                                       // 0x17014, +4, fcmpo/bge 0x17020
      mAnimationState.SetState(CAnimationState::kAS_Over); // 0x1703C
    } else {
      x11D64_(mgr, dt); // 0x17034
    }
    break;
  }
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady); // 0x17050
    x10F4_ &= ~0x20;                                          // 0x17058
    x1028_ = CVector3f::Zero();                               // sZeroVector, 0x17060..0x17074
    break;
  default:
    break;
  }
}

// fn_14_1569C: the ride on the retreat platform (0x1038). Activate (0): the animation state Ready
// (0x156EC), the platform, if there is one, gets STRT from the head (its AcceptScriptMsg, the
// vtable call at 0x15760), the bomb pit id 0x103A cleared (0x15770) and locomotion 5 (0x15778).
// Update (1): with no platform the state is Over (0x15840). Else the locomotion is relaxed (1)
// while the player rides the platform (CScriptPlatform::IsRider, 0x157B4) or bit 0x80 of 0x10F5
// is set, else 5; while the state's time is at most fn_14_F374's value (the fcmpo/cror at
// 0x1580C) the head takes the platform's transform (0x1582C), after it the state is Over.
// Deactivate (2): NotReady, combat locomotion (3) and the platform, if there is one, gets STOP.
// Every arm ran: with no platform after the head stun; after `codecall <head> SelectRetreatPlatform`
// and `fsm <head> FollowRetreatPath`, STRT, the ride (time <= limit), locomotion 5, STOP; forced
// (gdb attach): bit 0x80 of 0x10F5 (relaxed), the limit 0 (Over by time). IsRider itself stayed
// false (the player teleported onto the moving platform each 0.3 s was never counted a rider).
// @ fn_14_1569C
void CDigitalGuardianHead::FollowRetreatPath(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) { // the cmpwi chain at 0x156A4..0x156DC
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready); // stw 1 -> 0x6B4 at 0x156EC
    // ObjectById(0x1038) at 0x156FC, TCastToPtr<CScriptPlatform> at 0x15700, null test 0x15704
    if (CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(mgr.ObjectById(x1038_))) {
      // the message at 0x1570C..0x15738: sender and originator the head (0x5C, 0x5E), id the
      // platform (0x60), STRT (0x64), state -1 (0x68); AcceptScriptMsg (vtable 0x18) at 0x15760
      plat->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), plat->GetUniqueId(), kSM_Start,
                                            GetUniqueId()));
    }
    x103A_ = kInvalidUniqueId;                                  // 0x15770
    BodyController()->SetLocomotionType(pas::kLT_Internal5);    // li 5 at 0x15768, 0x15778
    break;
  case kStateMsg_Update: {
    // GetObjectById(0x1038) at 0x15790, the cast at 0x15794, null test (mr.) at 0x15798
    const CScriptPlatform* plat = TCastToConstPtr< CScriptPlatform >(mgr.GetObjectById(x1038_));
    if (plat == nullptr) {
      mAnimationState.SetState(CAnimationState::kAS_Over); // 0x15840
      break;
    }
    // IsRider(the player at mgr+0x14FC) at 0x157B4, bne 0x157BC; bit 0x80 of 0x10F5 at 0x157C4
    if (plat->IsRider(mgr.GetPlayer(0)->GetUniqueId()) || (x10F5_ & 0x80) != 0) {
      BodyController()->SetLocomotionType(pas::kLT_Relaxed); // li 1 at 0x157D0
    } else {
      BodyController()->SetLocomotionType(pas::kLT_Internal5); // li 5 at 0x157E0
    }
    const float limit = xF374_(); // 0x157F0
    if (mStateMachine->GetTime() <= limit) { // vtable 0x30 of 0x350 at 0x15808, fcmpo/cror 0x1580C
      SetTransform(plat->GetTransform()); // the platform's +0x24 at 0x1581C, 0x1582C
    } else {
      mAnimationState.SetState(CAnimationState::kAS_Over); // 0x15834
    }
    break;
  }
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);   // 0x15854
    BodyController()->SetLocomotionType(pas::kLT_Combat);      // li 3 at 0x15850, 0x1585C
    // ObjectById(0x1038) at 0x15870, the cast at 0x15874, null test 0x15878
    if (CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(mgr.ObjectById(x1038_))) {
      // as at Activate with STOP (0x5354_4F50, 0x1588C); the call at 0x158D4
      plat->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), plat->GetUniqueId(), kSM_Stop,
                                            GetUniqueId()));
    }
    break;
  default:
    break;
  }
}

// fn_14_13F0C: the root collider (0xE00) takes the head's own vulnerability (CPatterned's,
// called directly).
// @ fn_14_13F0C
void CDigitalGuardianHead::SetVulnerableToDamage(CStateManager& mgr, float dt) {
  if (CCollisionActor* root = TCastToPtr< CCollisionActor >(mgr.ObjectById(xE00_))) {
    root->SetDamageVulnerability(*CPatterned::GetDamageVulnerability());
  }
}

// fn_14_13EB8: the root collider (0xE00) is immune again (ImmuneRicochetVulnerability, the host's
// ReflectVulnerabilty, as fn_14_13318 gives it).
// @ fn_14_13EB8
void CDigitalGuardianHead::SetImmuneToDamage(CStateManager& mgr, float dt) {
  if (CCollisionActor* root = TCastToPtr< CCollisionActor >(mgr.ObjectById(xE00_))) {
    root->SetDamageVulnerability(CDamageVulnerability::ReflectVulnerabilty());
  }
}

// fn_14_1560C: the player (state manager +0x14FC) becomes the face target (0x1036).
// @ fn_14_1560C
void CDigitalGuardianHead::SetPlayerFaceTarget(CStateManager& mgr, float dt) {
  x1036_ = mgr.GetPlayer(0)->GetUniqueId();
}

// fn_14_143D8: the attack position is the attack area's point 45 degrees (.rodata 0x9E0,
// 0.7853982) on from the player's angle (fn_14_11698, 0 when the player is at the centre).
// @ fn_14_143D8
void CDigitalGuardianHead::SetInitialAttackPosition(CStateManager& mgr, float dt) {
  float angle = 0.f;
  x11698_(mgr.GetPlayer(0)->GetTranslation(), angle);
  xFEC_ = x11788_(angle + 0.7853982f);
}

// fn_14_15284: the attack position becomes the head's translation. With bit 0x10 of 0x10F4 set
// (extrwi. 1, 27; the head armour is gone) and 0x1038 a platform, it is instead the attack area's
// point at the angle (fn_14_11698, fn_14_11788) of the centre plus 50 (.rodata 0x9C4) along the
// direction to the platform.
// The arms: armour on and the platform arm ran in area-53 runs (run 37); armour off with no
// platform was forced (tools/cov/force.sh with the head-stun steps as its driver: bit 0x10 set and
// 0x1038 cleared at the entry). A failed fn_14_11698 cannot happen: once d is magnitude-safe,
// centre + 50 d/|d| is off the centre; a zero d needs the platform at the centre.
// @ fn_14_15284
void CDigitalGuardianHead::SetStunnedPosition(CStateManager& mgr, float dt) {
  xFEC_ = GetTranslation();
  if ((x10F4_ & 0x10) == 0) {
    return;
  }
  if (CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(mgr.ObjectById(x1038_))) {
    const CVector3f d = plat->GetTranslation() - x1004_;
    if (d.IsMagnitudeSafe()) {
      float angle = 0.f;
      if (x11698_(x1004_ + d.AsNormalized() * 50.f, angle)) {
        xFEC_ = x11788_(angle);
      }
    }
  }
}

// fn_14_14254: the attack position starts as the head's translation and the candidates come
// from fn_14_118B0. When the visual knowledge sees the player (flag 0x80 of 0xDB8) for over 2.25 s
// (.rodata 0x9DC; 0xDB0) fn_14_114C8 picks one; otherwise the candidate furthest along the
// previous move (translation minus 0xFF8, dot product >= the best so far, from 0) wins. 0xFF8
// then keeps the result. The fn_14_114C8 arm re-run since 114C8 was ported: forced (0xDB8 bit
// 0x80 set, 0xDB0 = 3 at the entry), its results the candidates or their blend.
// @ fn_14_14254
void CDigitalGuardianHead::SetAttackPosition(CStateManager& mgr, float dt) {
  xFEC_ = GetTranslation(); // 0x14278..0x1428C
  CVector3f points[3];
  const int count = x118B0_(points); // 0x14294, the count at sp+0x20
  // bit 0x80 of 0xDB8 at 0x1429C; 0xDB0 > 2.25 (.rodata 0x9DC), fcmpo/ble at 0x142B0
  if (xD80_.IsTargetVisible() && xD80_.GetVisibleTime() > 2.25f) {
    // the player (mgr+0x14FC)'s translation at 0x142C8..0x142E0, the call at 0x142E4
    xFEC_ = x114C8_(mgr.GetPlayer(0)->GetTranslation(), points, count); // 0x142EC..0x142FC
  } else {
    const CVector3f move = GetTranslation() - xFF8_; // 0x14304..0x14334
    float best = 0.f;                                 // .rodata 0x960 at 0x14330
    for (int i = 0; i < count; ++i) {                 // 0x14390..0x143A4
      const float dot = CVector3f::Dot(move, points[i] - xFF8_); // 0x1433C..0x14368
      if (dot >= best) {   // fcmpo/cror/bne at 0x1436C..0x14374
        xFEC_ = points[i]; // 0x14378..0x1438C
        best = dot;        // 0x1437C
      }
    }
  }
  xFF8_ = xFEC_; // 0x143A8..0x143BC
}

// fn_14_14B98: bit 0x04 of 0x10F4 cleared, bit 0x40 set (rlwimi 6, 25, 25), the deployment
// time (0x10F0) restarted; the splitters to deploy (0x10E8) are fn_14_F390's count when every
// splitter is dead (fn_14_17298), else none.
// @ fn_14_14B98
void CDigitalGuardianHead::StartSplitterDeployment(CStateManager& mgr, float dt) {
  x10F4_ &= ~0x04;
  x10F4_ |= 0x40;
  x10F0_ = 0.f;
  x10E8_ = AllSplittersDead(mgr, CTriggerData(0.f)) ? xF390_(mgr) : 0;
}

// fn_14_1524C: the player (the state manager's 0x14FC) becomes the visual knowledge's target
// (CVisualKnowledge::SetVisualTarget, 0x15334).
// @ fn_14_1524C
void CDigitalGuardianHead::SeekLineOfSight(CStateManager& mgr, float dt) {
  xD80_.SetVisualTarget(mgr.GetPlayer(0)->GetUniqueId());
}

// fn_14_14C24: the object list (state manager +0x830, the walk at 0x14C4C..0x14CD0) gives each
// command module (TCastToPtr at 0x14C60, null test 0x14C64) fn_75_5A54 (0x14C70) and else each
// main chassis (0x14C90, null test 0x14C94) fn_75_E660 (0x14CA0), with a delay that starts at 0.0
// (.rodata 0x960, 0x14C48) and grows by 0.7 per command module (.rodata 0x9A4, fadds at 0x14C7C)
// and by 0.15 per chassis (.rodata 0x9A8, fadds at 0x14CAC). Every arm ran: no splitters, then
// two (their layer-10 generators) given the delays.
// @ fn_14_14C24
void CDigitalGuardianHead::DestroyActiveSplitters(CStateManager& mgr, float dt) {
  CObjectList& list = mgr.ObjectListById(kOL_All);
  float delay = 0.f;
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    if (CSplitterCommandModule* module = TCastToPtr< CSplitterCommandModule >(list[i])) {
      module->PortAutoDestruct(delay);
      delay += 0.7f;
    } else if (CSplitterMainChassis* chassis = TCastToPtr< CSplitterMainChassis >(list[i])) {
      chassis->PortStartDeathTimer(delay);
      delay += 0.15f;
    }
  }
}

// fn_14_153D0: the attack position (0xFEC) is the head's translation, or the retreat platform's
// (0x1038; ObjectById at 0x1540C, TCastToPtr<CScriptPlatform> at 0x15410, null test 0x15418).
// @ fn_14_153D0
void CDigitalGuardianHead::SetPlatformPosition(CStateManager& mgr, float dt) {
  xFEC_ = GetTranslation(); // 0x153E4..0x15400
  if (CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(mgr.ObjectById(x1038_))) {
    xFEC_ = plat->GetTransform().GetTranslation(); // 0x1541C..0x15430
  }
}

// fn_14_149F8: the IS06/FOLW waypoint (FindConnectedObject_if with the waypoint predicate,
// lbl_14_data_1024, 0x14A4C) places the head at its translation, looking along its Y column, or
// the head's own Y column when that is not magnitude-safe (0x14AAC, clrlwi./beq at 0x14AB0);
// LookAt with the up vector (0x14B2C), SetTransform (0x14B38). Combat locomotion either way (li 3,
// 0x14B40). Retail's safe arm reads the direction from sp+0x30, which nothing in the body writes
// (the column was stored at sp+0x3C): the port uses the waypoint's column, the evident intent.
// The unsafe arm is forced (the area-53 waypoint 0x00350272 is orthonormal); the no-waypoint arm
// ran with that waypoint deleted.
// @ fn_14_149F8
void CDigitalGuardianHead::Stage3Start(CStateManager& mgr, float dt) {
  const TUniqueId id =
      FindConnectedObject_if(mgr, kSS_InternalState6, kSM_Follow, CWaypointPredicate());
  if (const CScriptWaypoint* wp = TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(id))) {
    CVector3f dir = wp->GetTransform().GetColumn(kDY); // 0x14A90..0x14AA8
    if (!dir.IsMagnitudeSafe()) {
      dir = GetTransform().GetColumn(kDY); // 0x14AC0..0x14AD8
    }
    const CVector3f& pos = wp->GetTranslation(); // wp+0x54 at 0x14AE8
    SetTransform(CTransform4f::LookAt(pos, pos + dir, CVector3f::Up())); // 0x14B08..0x14B38
  }
  BodyController()->SetLocomotionType(pas::kLT_Combat); // li 3 at 0x14B40, 0x14B44
}

// fn_14_14828: the retreat platform (0x1038), when it is one (null test 0x1485C), gets ACTV
// (lis 0x4143 / addi 0x5456 at 0x14874). With colliders built (0xD70, 0x148B4) the root (0xE00)
// is deactivated (SetActive, vtable 0x1C, li 0 at 0x148DC) and each ball (0xDC4, count 0xDC0,
// cmpw/blt at 0x14960) with health (GetHealthInfo, vtable 0x3C; HP at +4 > 0.0, fcmpo/ble at
// 0x14934) is activated; the manager is updated (dt 0.0, li 1 = kUO_WorldSpace, 0x14980). The
// head's filter excludes the player (exclude word 0x8 |= 1, bit 32, ori at 0x149A8), its shadow
// is off (0x149D4) and the damage colour is skDamageColor (0x149E0). Forced: the dead ball (HP 0)
// and no manager (0xD70 null; only fn_14_13318 on XCRT writes it, before any Think, and Think
// updates it unchecked). No root/balls ran with the colliders deleted.
// @ fn_14_14828
void CDigitalGuardianHead::EnableDynamicCollision(CStateManager& mgr, float dt) {
  if (TCastToPtr< CScriptPlatform >(mgr.ObjectById(x1038_)) != nullptr) {
    mgr.DeliverScriptMsg(CScriptMsg(GetUniqueId(), x1038_, kSM_Activate)); // 0x148A8
  }
  if (xD70_ != nullptr) {
    if (CCollisionActor* root = TCastToPtr< CCollisionActor >(mgr.ObjectById(xE00_))) {
      root->SetActive(false);
    }
    // the count at 0xDC0 (the ctor's 2 at 0x18340; nothing else writes it)
    for (int i = 0; i < ARRAY_SIZE(xDC4_); ++i) {
      if (CCollisionActor* ball = TCastToPtr< CCollisionActor >(mgr.ObjectById(xDC4_[i]))) {
        if (ball->GetHealthInfo()->GetHP() > 0.f) {
          ball->SetActive(true); // li 1 at 0x14940
        }
      }
    }
    xD70_->Update(0.f, mgr, CCollisionActorManager::kUO_WorldSpace);
  }
  CMaterialFilter filter = GetMaterialFilter();
  filter.ExcludeList().Add(kMT_Player);
  SetMaterialFilter(filter);
  SetDrawShadow(false); // li 0 at 0x149D0
  mDamageColor = CPatterned::skDamageColor;
}

// fn_14_14664: the retreat platform (0x1038) gets DCTV (lis 0x4443 / addi 0x5456 at 0x1469C,
// sent with no type check, 0x146D0) and the player's morph ball a 2 s damage timer (player
// +0x1174, .rodata 0x968, 0x146E8). With colliders built (0xD70, 0x146F4) the root (0xE00) is
// activated (li 1 at 0x1471C) and both balls (0xDC4, count 0xDC0, cmpw/blt at 0x14778)
// deactivated (li 0 at 0x1475C); the manager is updated (dt 0.0, li 1 = kUO_WorldSpace, 0x14798).
// The head's filter stops excluding the player (exclude word 0x8 &= ~1, bit 32, li -2/and at
// 0x147B0/0x147C8), its shadow is on (0x147F8) and the damage colour is
// skHitsWithoutDamageColor (0x80418FEC, 0x14804). Forced: no manager (as fn_14_14828).
// @ fn_14_14664
void CDigitalGuardianHead::DisableDynamicCollision(CStateManager& mgr, float dt) {
  mgr.DeliverScriptMsg(CScriptMsg(GetUniqueId(), x1038_, kSM_Deactivate));
  mgr.GetPlayer(0)->GetMorphBall()->SetDamageTimer(2.f);
  if (xD70_ != nullptr) {
    if (CCollisionActor* root = TCastToPtr< CCollisionActor >(mgr.ObjectById(xE00_))) {
      root->SetActive(true);
    }
    // the count at 0xDC0 (the ctor's 2 at 0x18340; nothing else writes it)
    for (int i = 0; i < ARRAY_SIZE(xDC4_); ++i) {
      if (CCollisionActor* ball = TCastToPtr< CCollisionActor >(mgr.ObjectById(xDC4_[i]))) {
        ball->SetActive(false);
      }
    }
    xD70_->Update(0.f, mgr, CCollisionActorManager::kUO_WorldSpace);
  }
  CMaterialFilter filter = GetMaterialFilter();
  filter.ExcludeList().Remove(kMT_Player);
  SetMaterialFilter(filter);
  SetDrawShadow(true); // li 1 at 0x147F4
  mDamageColor = CPatterned::skHitsWithoutDamageColor;
}

// fn_14_15598: the projectile timer 0x10D4 is block 6's projectileAttackTime (fn_14_10120's +8,
// 0x155EC) once every splitter is dead (fn_14_17298, the test at 0x155C8), else 6 (.rodata 0x990).
// Both arms ran in the area-53 head-stun runs (local probes): splitters alive and all dead.
// @ fn_14_15598
void CDigitalGuardianHead::ResetProjectileAttackTimes(CStateManager& mgr, float dt) {
  if (AllSplittersDead(mgr, CTriggerData(0.f))) { // 0.0 at 0x155C0, 0x155C4, bne at 0x155CC
    x10D4_ = x10120_(mgr)->projectileAttackTime;  // 0x155E8, +8 at 0x155EC, 0x155F0
  } else {
    x10D4_ = 6.f; // .rodata 0x990 at 0x155D4, 0x155D8
  }
}

// fn_14_15448: block 6 (fn_14_10120, by the body's knee count) picks the projectile attack. When
// the body's knee count (fn_14_705C; 0 with no body) differs from the one last seen (0x10DC, the
// cmpw at 0x154A8) and the block's firstShotType is not 4, that type; else a weighted pick over
// the block's odds (Range(0, sum), each bound inclusive): pulse 0, charge 1, missiles 2, else the
// gun 3. The knee count is kept in 0x10DC (0x15558); a pulse fires a burst of 3 (0x10D8), the rest
// none (the cntlzw at 0x15564).
// Every arm ran: the first shot (knee count 4 against the ctor's -1, firstShotType 0), then
// `codecall <head> SelectProjectileAttack` x24 with the count unchanged drew pulse 0, missiles 2
// and the gun 3; charge 1 (odds 0 in the 4-knee block 0xA64) forced, its odds set to 1000.
// @ fn_14_15448
void CDigitalGuardianHead::SelectProjectileAttack(CStateManager& mgr, float dt) {
  const SLdrDigitalGuardianHeadStruct* odds = x10120_(mgr); // 0x1546C
  // GetObjectById(0x1034) at 0x15484, the cast at 0x15488, null test 0x1548C, else 0 (0x154A0)
  const CDigitalGuardian* body = TCastToConstPtr< CDigitalGuardian >(mgr.GetObjectById(x1034_));
  const int knees = body != nullptr ? body->x705C_() : 0; // 0x15494
  // cmpw with 0x10DC at 0x154A8 (beq); firstShotType (+0) cmpwi 4 at 0x154B4 (beq)
  if (knees != x10DC_ && odds->firstShotType != 4) {
    x10D0_ = odds->firstShotType; // 0x154BC
  } else {
    // Range(mgr+0x16E4, 0 (.rodata 0x960), ((+0xC + +0x10) + +0x14) + +0x18) at 0x154F0
    const float r = mgr.Random()->Range(0.f, odds->annihilatorPulseOdds +
                                                  odds->annihilatorChargeOdds +
                                                  odds->machineGunOdds + odds->lockOnMissilesOdds);
    float bound = odds->annihilatorPulseOdds; // +0xC at 0x154F4
    if (r <= bound) {                         // fcmpo/cror/bne at 0x154F8..0x15500
      x10D0_ = 0;                             // 0x15508
    } else if (r <= (bound += odds->annihilatorChargeOdds)) { // +0x10, 0x15514..0x15520
      x10D0_ = 1;                                              // 0x15528
    } else if (r <= bound + odds->lockOnMissilesOdds) {        // +0x18, 0x15534..0x15540
      x10D0_ = 2;                                              // 0x15548
    } else {
      x10D0_ = 3; // 0x15554
    }
  }
  x10DC_ = knees;               // 0x15558
  x10D8_ = x10D0_ == 0 ? 3 : 0; // cntlzw/extrwi 1, 26/neg/and at 0x15564..0x15574
}

// fn_14_14604: orbitable in every visor (li 0xF / li 1 at 0x1461C/0x1460C) and in the echo visor
// (li 2 / li 1 at 0x14630/0x14634), and a seeker target (li 0x3F at 0x14644, AddMaterial).
// @ fn_14_14604
void CDigitalGuardianHead::EnableValidTarget(CStateManager& mgr, float dt) {
  SetVisorOrbitableFlags(CVisorParameters::kVOF_All, true);  // 0x14628
  SetVisorOrbitableFlags(CVisorParameters::kVOF_Echo, true); // 0x14638
  AddMaterial(kMT_SeekerTarget, mgr);                        // 0x14648
}

// fn_14_145A4: not orbitable in any visor (li 0xF / li 0 at 0x145BC/0x145AC), but the echo visor
// flag is set again (li 2 / li 1 at 0x145D0/0x145D4, as in fn_14_14604), and no seeker target
// (li 0x3F at 0x145E4, RemoveMaterial).
// @ fn_14_145A4
void CDigitalGuardianHead::DisableValidTarget(CStateManager& mgr, float dt) {
  SetVisorOrbitableFlags(CVisorParameters::kVOF_All, false); // 0x145C8
  SetVisorOrbitableFlags(CVisorParameters::kVOF_Echo, true); // 0x145D8
  RemoveMaterial(kMT_SeekerTarget, mgr);                     // 0x145E8
}

// fn_14_14248: the gravity repulsor's health (0x10E0) is the data's gravityRepulsorHP again
// (0x7D8 = data +0x18; lfs/stfs at 0x14248/0x1424C).
// @ fn_14_14248
void CDigitalGuardianHead::ResetGravityRepulsorHP(CStateManager& mgr, float dt) {
  x10E0_ = mData.gravityRepulsorHP;
}

// fn_14_14058: the player (mgr +0x14FC) stops (0x14080). The push goes along the head's X column
// (0x24/0x34/0x44), negated when the player is behind it (the dot of the player minus the head
// with the column >= 0.0, .rodata 0x960, fcmpo/cror/bne at 0x140D4..0x140DC), plus the Z column
// (0x2C/0x3C/0x4C, 0x14104..0x14134). When that is magnitude-safe (clrlwi./beq at 0x1413C) the
// player gets the impulse 30 (.rodata 0x98C) * mass (player +0x158) * its direction
// (ApplyImpulseWR with no angular part, 0x141A8) and the ApplyJump move state (li 2 at 0x141B4).
// Either way the retreat platform (0x1038), when it is one (mr./beq at 0x141D4), stops excluding
// the player (exclude word 0x8 &= ~1, bit 32, li -2/and at 0x141EC/0x14204). Both sides and both
// platform arms ran; the unsafe direction is forced (the head's columns are orthonormal).
// @ fn_14_14058
void CDigitalGuardianHead::EjectPlayerFromBombPit(CStateManager& mgr, float dt) {
  CPlayer* player = mgr.GetPlayer(0);
  player->Stop();
  CVector3f side = GetTransform().GetColumn(kDX);
  if (!(CVector3f::Dot(player->GetTranslation() - GetTranslation(), side) >= 0.f)) {
    side = -side; // 0x140E8..0x14100
  }
  const CVector3f dir = side + GetTransform().GetColumn(kDZ);
  if (dir.IsMagnitudeSafe()) {
    const CVector3f impulse = 30.f * (player->GetMass() * dir.AsNormalized()); // 0x14150..0x14194
    player->ApplyImpulseWR(impulse, CAxisAngle::Identity());                    // 0x141A8
    player->SetMoveState(NPlayer::kMS_ApplyJump, mgr); // 0x141B8
  }
  if (CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(mgr.ObjectById(x1038_))) {
    CMaterialFilter filter = plat->GetMaterialFilter();
    filter.ExcludeList().Remove(kMT_Player);
    plat->SetMaterialFilter(filter); // 0x14228
  }
}

// fn_14_13C1C: sound_Stage3Recover (this +0x92C = data +0x16C) at the translation (0x54) along the
// Y column (0x28/0x38/0x48), not looped (li 0 at 0x13C28); the handle is dropped.
// @ fn_14_13C1C
void CDigitalGuardianHead::StartVocalRecoverReaction(CStateManager& mgr, float dt) {
  PlayCustomSound(GetTranslation(), GetTransform().GetColumn(kDY), mData.sound_Stage3Recover,
                  false); // 0x13C58
}

// fn_14_15054: the "RaisedShields" effect state (.rodata 0x950) off; at locator 1 the drop-shields
// effect (0xEF4, "Drop Shields Fx", .rodata 0xA00) at this head's scale, then the drop-shields
// sound (data +0x64) facing the locator's column 1. The only DGHD placement (area 53, 0x0035000F)
// has dropShieldsFx (0x5FF0B26C) = PART 0xCF46E5D8, so 0xEF4 is always valid; rs_new does not
// return null on the host.
// @ fn_14_15054
void CDigitalGuardianHead::DropShields(CStateManager& mgr, float dt) {
  ModelData()->AnimationData()->SetEffectState(rstl::string_l("RaisedShields"), false, mgr);
  const CTransform4f xf = GetLctrTransform(CSegId(1));
  if (xEF4_.valid()) {
    const CVector3f scale = GetModelData()->GetScale();
    const rstl::string name = rstl::string_l("Drop Shields Fx");
    const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, true,
                           kInvalidEditorId);
    const TUniqueId uid = mgr.AllocateUniqueId();
    CExplosion* explosion = rs_new CExplosion(xEF4_.data(), uid, info, name, xf, 0, scale,
                                              CColor::White(), -1);
    if (explosion != nullptr) {
      mgr.AddObject(explosion);
    }
  }
  PlayCustomSound(xf.GetTranslation(), xf.GetColumn(kDY), mData.sound_DropShields, false);
}

// fn_14_14F74: the "RaisedShields" effect state on, then the raise-shields sound (data +0x4C) at
// locator 1, facing its column 1.
// @ fn_14_14F74
void CDigitalGuardianHead::RaiseShields(CStateManager& mgr, float dt) {
  ModelData()->AnimationData()->SetEffectState(rstl::string_l("RaisedShields"), true, mgr);
  const CTransform4f xf = GetLctrTransform(CSegId(1));
  PlayCustomSound(xf.GetTranslation(), xf.GetColumn(kDY), mData.sound_RaiseShields, false);
}

// fn_14_14B60: bit 0x40 of 0x10F4 cleared, the body back to combat locomotion.
// @ fn_14_14B60
void CDigitalGuardianHead::EndSplitterDeployment(CStateManager& mgr, float dt) {
  x10F4_ &= ~0x40;
  BodyController()->SetLocomotionType(pas::kLT_Combat);
}

// fn_14_14470: with splitters alive the gun (3). Else the first shot (block 6's firstShotType,
// data +0x2DC) once while 0x10DC is set and the type is not 4; else a weighted pick over block
// 6's odds (Range(0, sum), each bound inclusive): pulse 0, charge 1, missiles 2, else the gun 3.
// A pulse fires a burst of 3 (0x10D8), the rest none.
// Every arm ran (dg26 driver a); the first-shot test failing on a firstShotType of 4 (shipped 0)
// forced (dg27 force spec: x10DC_ 1, firstShotType 4 at entry).
// @ fn_14_14470
void CDigitalGuardianHead::SelectDeathHeadAttack(CStateManager& mgr, float dt) {
  const SLdrDigitalGuardianHeadStruct& odds = mData.digitalGuardianHeadStruct_0x48b46e55;
  if (!AllSplittersDead(mgr, CTriggerData(0.f))) {
    x10D0_ = 3;
  } else if (x10DC_ != 0 && odds.firstShotType != 4) {
    x10D0_ = odds.firstShotType;
    x10DC_ = 0;
  } else {
    const float r = mgr.Random()->Range(0.f, odds.annihilatorPulseOdds +
                                                  odds.annihilatorChargeOdds +
                                                  odds.machineGunOdds + odds.lockOnMissilesOdds);
    float bound = odds.annihilatorPulseOdds;
    if (r <= bound) {
      x10D0_ = 0;
    } else if (r <= (bound += odds.annihilatorChargeOdds)) {
      x10D0_ = 1;
    } else if (r <= bound + odds.lockOnMissilesOdds) {
      x10D0_ = 2;
    } else {
      x10D0_ = 3;
    }
  }
  x10D8_ = x10D0_ == 0 ? 3 : 0; // 0x14570..0x14588: cntlzw/extrwi 1,26, neg, and with 3
}

// fn_14_13F70: the IS02/ATCH player hint, unless already this head's, takes this head as its
// actor (0x1AC) and gets INCR from this head on the player's behalf. The area-53 placement's only
// IS02/ATCH link is PlayerHint 0x0C35001D, so the hint is always there.
// @ fn_14_13F70
void CDigitalGuardianHead::GrabPlayerHintControl(CStateManager& mgr, float dt) {
  CScriptPlayerHint* hint = TCastToPtr< CScriptPlayerHint >(
      mgr.ObjectById(CheckConnectedObject(mgr, kSS_InternalState2, kSM_Attach)));
  if (hint == nullptr || hint->GetActorId() == GetUniqueId()) {
    return;
  }
  hint->SetActorId(GetUniqueId());
  mgr.DeliverScriptMsg(CScriptMsg(GetUniqueId(), hint->GetUniqueId(), kSM_Increment,
                                  mgr.GetPlayer(0)->GetUniqueId()));
}

// fn_14_13C6C: 0x1038 is cleared (0x13CAC), then with any MDFY/PLAY platforms (0x1048) a random
// one (the state manager's CRandom16, Range(0, count - 1)) is picked. Along its CONN/ATCH
// waypoint's ARRV/NEXT chain (at most 30 waypoints: the counter starts at 1 and stops at 0x20)
// the one nearest the head (from .rodata 0x9D8, FLT_MAX; a strictly smaller squared distance
// wins) is where the platform teleports, and the platform becomes 0x1038 (0x13E80). The area-53
// placement's MDFY/PLAY links are platforms 0x0C350003 and 0x0C350302, both present, and both
// CONN/ATCH into the same 16-waypoint loop, so neither the empty, null-platform nor no-waypoint
// arm runs there.
// @ fn_14_13C6C
void CDigitalGuardianHead::SelectRetreatPlatform(CStateManager& mgr, float dt) {
  x1038_ = kInvalidUniqueId;
  if (x1044_.size() == 0) {
    return;
  }
  const TUniqueId platId = x1044_[mgr.Random()->Range(0, x1044_.size() - 1)];
  CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(mgr.ObjectById(platId));
  if (plat == nullptr) {
    return;
  }
  const TUniqueId firstId = plat->CheckConnectedObject_if(mgr, kSS_Connect, kSM_Attach,
                                                          CWaypointPredicate());
  const CVector3f headPos = GetTranslation();
  int count = 1;
  TUniqueId bestId = kInvalidUniqueId;
  float best = FLT_MAX;
  const CScriptWaypoint* wp = TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(firstId));
  while (wp != nullptr && ++count < 0x20) {
    const CVector3f& pos = wp->GetTranslation();
    const float dx = pos.GetX() - headPos.GetX();
    const float dy = pos.GetY() - headPos.GetY();
    const float dz = pos.GetZ() - headPos.GetZ();
    const float dist = dz * dz + (dx * dx + dy * dy);
    if (dist < best) {
      best = dist;
      bestId = wp->GetUniqueId();
    }
    const TUniqueId nextId = wp->CheckConnectedObject_if(mgr, kSS_Arrived, kSM_Next,
                                                         CWaypointPredicate());
    wp = TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(nextId));
  }
  if (bestId != kInvalidUniqueId) {
    plat->TeleportToWaypoint(bestId, mgr);
    x1038_ = platId;
  }
}

// ---------------------------------------------------------------------------------------------
// fn_14_DF7C: the DGHD loader. The generated reader fills the four property blocks; an empty model
// is skipped (null), as the body's loader does. The empty arm is data-dead (the one DGHD, 0x0035000F,
// has a model); forced (dg27 noload spec: the ANCS id invalid before LdrToModelData).
// @ fn_14_DF7C
CEntity* PortLoadDigitalGuardianHead(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrDigitalGuardianHead sldrThis;
  // Retail's loader default the generated SLdr ctor lacks: stb 1 to patterned +0x230 (0xE01C).
  sldrThis.patterned.echoParameters.isEchoEmitter = true;
#include "MetroidPrime/ScriptLoader/SLdrDigitalGuardianHead.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }

  return rs_new CDigitalGuardianHead(
      mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
      LdrToEntityInfo(info, sldrThis.editorProperties), LdrToTransform4f(sldrThis.editorProperties),
      *modelData, LdrToActorParameters(sldrThis.actorInformation),
      LdrToPatternedInfo(sldrThis.patterned, nullptr), sldrThis.digitalGuardianHeadProperties);
}

// fn_14_1303C: with the armour gone (0x10F4 bit 0x10, extrwi 1,27) a ball collider at no health
// (the cror at 0x13084: HP <= .rodata 0x960) hurts the head along its column 1 by the collider's
// full health (vtable 0xA4), becomes the destroyed bomb pit (0x103A), costs a life (0x10E4), moves
// the waypoint to it with IS08 and destroys its bomb slot, then is deactivated (vtable 0x1C).
// Before, an echo target in state 4 moves the waypoint to its aim position with IS09, is
// destroyed (fn_14_11FEC) and deleted, and sets bit 0x02 (EchoTargetDestroyed).
// @ fn_14_1303C
void CDigitalGuardianHead::x1303C_(CStateManager& mgr, TUniqueId sender) {
  if ((x10F4_ & 0x10) != 0) {
    CCollisionActor* col = TCastToPtr< CCollisionActor >(mgr.ObjectById(sender));
    if (col != nullptr && col->GetHealthInfo()->GetHP() <= 0.f) {
      TakeDamage(GetTransform().GetColumn(kDY), col->GetHealthInfo()->GetInitialHP());
      x103A_ = col->GetUniqueId();
      --x10E4_;
      xF2B8_(mgr, col->GetTranslation(), kSS_InternalState8);
      x12B04_(mgr, x13248_(sender));
      col->SetActive(false);
    }
  } else if (x11F30_(sender)) {
    CDigitalGuardianEchoTarget* target =
        static_cast< CDigitalGuardianEchoTarget* >(mgr.ObjectById(sender));
    if (target != nullptr && target->GetState() == CDigitalGuardianEchoTarget::kS_Destroyed) {
      xF2B8_(mgr, target->GetAimPosition(mgr, 0.f), kSS_InternalState9);
      x11FEC_(mgr, target->GetTransform());
      mgr.DeleteObjectRequest(target->GetUniqueId());
      x10F4_ |= 0x02;
    }
  }
}

// fn_14_F2B8: the IS07/FOLW object (0xF2C0..0xF2D8), if a waypoint, goes to pos; then state is
// sent with no message (-1).
// 'IS07' 0x49533037 (0xF2C4/0xF2D8), 'FOLW' 0x464F4C57 (0xF2C0/0xF2CC), msg -1 at 0xF348.
// @ fn_14_F2B8
void CDigitalGuardianHead::xF2B8_(CStateManager& mgr, const CVector3f& pos,
                                  EScriptObjectState state) {
  const TUniqueId wpId = CheckConnectedObject(mgr, kSS_InternalState7, kSM_Follow);
  if (CScriptWaypoint* wp = TCastToPtr< CScriptWaypoint >(mgr.ObjectById(wpId))) {
    wp->SetTranslation(pos);
    SendScriptMsgs(state, mgr, GetUniqueId(), kSM_None);
  }
}

// fn_14_13248: the index of id among the ball colliders (count 0xDC0, 2), or -1. The -1 arm
// forced (dg27 force spec: the second bomb hit's id 0xffff).
// @ fn_14_13248
int CDigitalGuardianHead::x13248_(TUniqueId id) const {
  for (int i = 0; i < 2; ++i) {
    if (xDC4_[i] == id) {
      return i;
    }
  }
  return -1;
}

// fn_14_11F30: a valid id among the echo targets (0xF30 count, 0xF34 ids).
// @ fn_14_11F30
bool CDigitalGuardianHead::x11F30_(TUniqueId id) const {
  if (id != kInvalidUniqueId) {
    for (int i = 0; i < xF34_.size(); ++i) {
      if (id == xF34_[i]) {
        return true;
      }
    }
  }
  return false;
}

// fn_14_11FEC: the echo-target-destroyed effect ("EchoTargetExplosion", .rodata 0xF1A) at xf at
// this head's scale, the IS04/ACTV object (0x103E) loses one health (.rodata 0x96C), one echo
// target fewer (0xFE0); at none the armour is gone (bit 0x10) and IS00 is sent. The survivors
// then ignore damage (0x78C bit 0x80).
// The unset-token arm is data-dead (the ctor locks xFD0_ only with the asset set, and the one
// placement sets it); forced (dg26 force spec).
// @ fn_14_11FEC
void CDigitalGuardianHead::x11FEC_(CStateManager& mgr, const CTransform4f& xf) {
  if (xFD0_.valid()) {
    const CVector3f scale = GetModelData()->GetScale();
    const rstl::string name = rstl::string_l("EchoTargetExplosion");
    const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, true,
                           kInvalidEditorId);
    const TUniqueId uid = mgr.AllocateUniqueId();
    CExplosion* explosion = rs_new CExplosion(xFD0_.data(), uid, info, name, xf, 0, scale,
                                              CColor::White(), -1);
    if (explosion != nullptr) {
      mgr.AddObject(explosion);
    }
  }
  // Retail calls the object's HealthInfo slots (vtable 0x3C, 0x38) unchecked; the host casts.
  if (CActor* act = TCastToPtr< CActor >(mgr.ObjectById(x103E_))) {
    act->HealthInfo()->SetHP(act->GetHealthInfo()->GetHP() - 1.f);
  }
  if (--xFE0_ <= 0) {
    x10F4_ |= 0x10;
    SendScriptMsgs(kSS_InternalState0, mgr, GetUniqueId(), kSM_None);
  }
  for (int i = 0; i < xF34_.size(); ++i) {
    CDigitalGuardianEchoTarget* target =
        static_cast< CDigitalGuardianEchoTarget* >(mgr.ObjectById(xF34_[i]));
    if (target != nullptr) {
      target->SetDamageIgnored(true);
    }
  }
}

// fn_14_12B04: at ball idx's locator (0xDCC) the bomb-pit effect ("BombPitExplosionFx", .rodata
// 0xF39) when set, the explosion sound (data +0x13C), the slot's destroyed effect state (.data
// 0xE50) and, once, its looping sound (data +0x154) at 0xF28; the IS04/ACTV object loses one
// health. idx -1 and the unset token forced (dg27 force spec); the rest ran (dg26 drivers a, b, c).
// @ fn_14_12B04
void CDigitalGuardianHead::x12B04_(CStateManager& mgr, int idx) {
  if (idx == -1) {
    return;
  }
  const CTransform4f xf = GetLctrTransform(xDCC_[idx]);
  const CVector3f pos = xf.GetTranslation();
  if (xF14_.valid()) {
    const CVector3f scale = GetModelData()->GetScale();
    const rstl::string name = rstl::string_l("BombPitExplosionFx");
    const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, true,
                           kInvalidEditorId);
    const TUniqueId uid = mgr.AllocateUniqueId();
    CExplosion* explosion = rs_new CExplosion(xF14_.data(), uid, info, name, xf, 0, scale,
                                              CColor::White(), -1);
    if (explosion != nullptr) {
      mgr.AddObject(explosion);
    }
  }
  PlayCustomSound(pos, CVector3f::Up(), mData.sound_BombSlotExplosion, false);
  static const char* const skSlotDestroyed[2] = {"LeftBombSlotDestroyed",
                                                  "RightBombSlotDestroyed"};
  ModelData()->AnimationData()->SetEffectState(rstl::string_l(skSlotDestroyed[idx]), true,
                                               mgr);
  if (!xF28_[idx]) {
    xF28_[idx] = PlayCustomSound(pos, CVector3f::Up(), mData.sound_BombSlotDestroyed, true);
  }
  // Retail calls the object's HealthInfo slots (vtable 0x3C, 0x38) unchecked; the host casts.
  if (CActor* act = TCastToPtr< CActor >(mgr.ObjectById(x103E_))) {
    act->HealthInfo()->SetHP(act->GetHealthInfo()->GetHP() - 1.f);
  }
}
