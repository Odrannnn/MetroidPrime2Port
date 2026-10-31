// Host body for the DigitalGuardian REL (module 14), milestone 1: the actor loads, registers and
// thinks. Every vtable override that belongs to retail's AI forwards to CPatterned behind a
// once-only StandIn log, and the REL function it stands in for is named on each. The state
// machine's tables are registered (fn_14_9DFC installs the 12 triggers, 14 states and 13 codes),
// but the 39 functions they point at are stand-ins that log the retail function they stand in
// for and return the neutral value. The vortex (fn_14_458C), the knee armour, the transmission
// beacon, the toe targets and the head module (CDigitalGuardianHead, guest 0x10F8) are not
// written yet - docs/module-analysis/DigitalGuardian.md section i has the plan.
//
// Which methods the REL overrides is not guessed: its vtable lbl_14_data_484 lists them by address
// (/tmp/opencode/pseudo/DigitalGuardian and the module's asm dump) and the slots line up with
// CPatterned's declaration order, the inherited entries being spelled out by name in the same
// table. The tiny accessors at the head of the module's .text (fn_14_0, fn_14_8, fn_14_78,
// fn_14_88, fn_14_90, fn_14_98, fn_14_A0, fn_14_A8, fn_14_B0, fn_14_C0, fn_14_CC, fn_14_D8,
// fn_14_E0, fn_14_E8) have four-to-twelve-instruction bodies, so they are real rather than
// stand-ins.
#include "MetroidPrime/Enemies/CDigitalGuardian.hpp"

#include "Collision/CMaterialFilter.hpp"
#include "Collision/CMaterialList.hpp"
#include "Kyoto/Animation/CCharLayoutInfo.hpp"
#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "Kyoto/Animation/CPASDatabase.hpp"
#include "Kyoto/Animation/CSegId.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/Graphics/CColor.hpp"
#include "Kyoto/Graphics/CModelFlags.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "Kyoto/Particles/CParticleGen.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CParticleDatabase.hpp"
#include "rstl/StringExtras.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "MetroidPrime/Enemies/CDigitalGuardianHead.hpp"
#include "MetroidPrime/Enemies/CDigitalGuardianKneeArmor.hpp"
#include "MetroidPrime/CEchoEmitter.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Enemies/CPatternedInfo.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWaypoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptCameraShaker.hpp"
#include "MetroidPrime/Weapons/CShockWave.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/Cameras/CCameraShakerManager.hpp"
#include "MetroidPrime/CAxisAngle.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/CRandom16.hpp"
#include "Kyoto/Text/CStringTable.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrDigitalGuardian.hpp"
#include "port_console.h"
#include "MetroidPrime/ScriptLoaderRel.hpp"

#include <float.h>
#include <stdio.h>

namespace {

// The module's render flag (lbl_14_bss_0): the module's render (fn_14_A3D4) sets it while it
// renders, and its RenderSystemsToBeDrawnLast (fn_14_A3A4) only forwards to the base's while
// it is set.
uchar sRendering = 0;

// The module's GX register colour (lbl_14_bss_30): the static initialiser (fn_14_D904) sets
// it to (0.25, 0.25, 0.25, 1).
const CColor sGXRegister1Color(0.25f, 0.25f, 0.25f, 1.f);

// The retail's GenerateParticlePOIMask (the call at 0xA420) is the host's
// CStateManager::GetCharacterRenderMaskAndTarget; the retail's name is kept here so the
// asm-vs-C++ check can match the call.
void GenerateParticlePOIMask(const CStateManager& mgr, uint& mask, uint& target) {
  mgr.GetCharacterRenderMaskAndTarget(mask, target);
}

// Reports a stand-in the first time it runs, so a log lists what is still a shell.
void StandIn(const char* what) {
  static const char* sSeen[32] = {};
  static int sCount = 0;
  for (int i = 0; i < sCount; ++i) {
    if (sSeen[i] == what) {
      return;
    }
  }
  if (sCount < 32) {
    sSeen[sCount++] = what;
  }
  fprintf(stderr, "[dgrd] %s is a port stand-in (see PortCDigitalGuardian.cpp)\n", what);
}

// The same, for the state machine's own functions: the message names the retail function the
// entry stands in for, so the log says which of the module's 39 FSM functions the run reached.
void StandInFn(const char* fn) {
  static const char* sSeen[64] = {};
  static int sCount = 0;
  for (int i = 0; i < sCount; ++i) {
    if (sSeen[i] == fn) {
      return;
    }
  }
  if (sCount < 64) {
    sSeen[sCount++] = fn;
  }
  fprintf(stderr, "[dg] stand-in %s\n", fn);
}

// The body's three FSM tables. Retail builds them in a static initialiser (fn_14_D904, .ctors):
// the tables are lbl_14_data_9C (12 triggers, .data 0x9C), lbl_14_data_204 (14 states, .data
// 0x204) and lbl_14_data_380 (13 codes, .data 0x380), each entry 16 bytes
// {name, 0, 0xFFFFFFFF, fn}, and the function pointers come from the three-word records
// {0, 0xFFFFFFFF, fn} in .data 0xC..0x374 (fn_14_D904's stores at 0xDB50..0xDCC8 for the
// triggers, 0xDCD0..0xDEB8 for the states and 0xDEBC..0xE00C for the codes).
//
// On the host the table addresses are irrelevant: the state machine matches a node by its name,
// so the name strings are retail's and the entry's function is the module's member function.
// The members are cast the way CAIMannedTurret and the SpacePirate port do it.
// fn_14_D904 (0x668, no branch) also stores the knee colours bss_1C (0.75, 0, 0, 1; 0xD938/0xD964)
// and bss_20 (0, 0, 0.75, 1; 0xD974), the toe bounds bss_24 (0, 0, 2; 0xDEEC..0xDF50) and the GX
// colour bss_30 (0.25, 0.25, 0.25, 1; 0xDF54): sHitColor/sVulnerableColor (knee armour file),
// skToeTouchBounds and sGXRegister1Color. tools/drivers/dg_tables.py runs the initialiser over the
// .data dump and compares every entry (name, order, retail function by the member's marker) with
// the arrays below: 12 + 14 + 13 entries, no mismatch.
#define DG_TRIGGER(name)                                                                           \
  { #name, reinterpret_cast< CPatterned::StateMachine::TriggerFunc >(&CDigitalGuardian::name) }
#define DG_STATE(name)                                                                             \
  { #name, reinterpret_cast< CPatterned::StateMachine::StateFunc >(&CDigitalGuardian::name) }
#define DG_CODE(name)                                                                              \
  { #name, reinterpret_cast< CPatterned::StateMachine::CodeFunc >(&CDigitalGuardian::name) }

// @ fn_14_D904
CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    DG_TRIGGER(StateOver),           DG_TRIGGER(IsIdle),
    DG_TRIGGER(HasFaceTarget),       DG_TRIGGER(KneeDestroyed),
    DG_TRIGGER(AllKneesDestroyed),   DG_TRIGGER(ShouldProtectKnees),
    DG_TRIGGER(ShouldVortexAttack),  DG_TRIGGER(ShouldMeleeAttack),
    DG_TRIGGER(ShouldProjectileAttack), DG_TRIGGER(ShouldJumpAttack),
    DG_TRIGGER(VortexStruckPlayer),  DG_TRIGGER(BreakOutOfVortex),
};

CPatterned::StateMachine::SStateFunction skStates[] = {
    DG_STATE(Start),               DG_STATE(Idle),
    DG_STATE(FaceTarget),          DG_STATE(VortexAttack),
    DG_STATE(VortexEndAttack),     DG_STATE(VortexRetreat),
    DG_STATE(ExitVortexAttack),    DG_STATE(MeleeAttackTelegraph),
    DG_STATE(MeleeAttack),         DG_STATE(ProjectileAttack),
    DG_STATE(JumpAttack),          DG_STATE(KneeDestroyedReaction),
    DG_STATE(CrippledReaction),    DG_STATE(Crippled),
};

CPatterned::StateMachine::SCodeFunction skCodes[] = {
    DG_CODE(SetupBossEnergyBar),      DG_CODE(ResetProtectKneesTime),
    DG_CODE(ResetJumpTime),           DG_CODE(ResetAttackTimes),
    DG_CODE(SetProtectKneesFaceTarget), DG_CODE(SetVortexAttackMode),
    DG_CODE(SetNormalAttackMode),     DG_CODE(DisableHeadAttacks),
    DG_CODE(ResetHeadAttacks),        DG_CODE(SelectExitVortexTarget),
    DG_CODE(SelectBestMeleeAttackLeg), DG_CODE(SelectMeleeAttackRange),
    DG_CODE(SetJumpFaceTarget),
};

#undef DG_TRIGGER
#undef DG_STATE
#undef DG_CODE

// The joint colliders fn_14_61DC builds. The spheres are .rodata 0x60 (11 entries of
// {locator, radius}); the boxes are .rodata 0xB8 (24 entries of {from, to, size}). fn_14_5C30's
// four knee-to-ankle boxes are .rodata 0x1D8. Every collider's mass is .rodata 0x2C0 (1000).
struct SSphereJoint {
  const char* name;
  float radius;
};

struct SBoxJoint {
  const char* from;
  const char* to;
  float size;
};

const SSphereJoint skSphereJoints[11] = {
    {"Head_1", 4.f},          {"Skeleton_Root", 5.f},   {"L_front_knee_2", 5.f},
    {"L_back_knee_2", 5.f},   {"R_front_knee_2", 5.f},  {"R_back_knee_2", 5.f},
    {"L_front_col_LCTR", 3.f}, {"L_back_col_LCTR", 3.f}, {"R_front_col_LCTR", 3.f},
    {"R_back_col_LCTR", 3.f}, {"root", 5.f},
};

const SBoxJoint skBoxJoints[24] = {
    {"L_front_hip", "L_front_knee", 3.f},
    {"L_back_hip", "L_back_knee", 3.f},
    {"R_front_hip", "R_front_knee", 3.f},
    {"R_back_hip", "R_back_knee", 3.f},
    {"L_front_knee", "L_front_knee_2", 3.f},
    {"L_back_knee", "L_back_knee_2", 3.f},
    {"R_front_knee", "R_front_knee_2", 3.f},
    {"R_back_knee", "R_back_knee_2", 3.f},
    {"L_front_knee_2", "L_front_ankle", 4.f},
    {"L_back_knee_2", "L_back_ankle", 4.f},
    {"R_front_knee_2", "R_front_ankle", 4.f},
    {"R_back_knee_2", "R_back_ankle", 4.f},
    {"L_front_B_toe", "L_front_B_toe_end", 1.25f},
    {"L_front_L_toe", "L_front_L_toe_end", 1.25f},
    {"L_front_R_toe", "L_front_R_toe_end", 1.25f},
    {"L_back_B_toe", "L_back_B_toe_end", 1.25f},
    {"L_back_L_toe", "L_back_L_toe_end", 1.25f},
    {"L_back_R_toe", "L_back_R_toe_end", 1.25f},
    {"R_front_B_toe", "R_front_B_toe_end", 1.25f},
    {"R_front_L_toe", "R_front_L_toe_end", 1.25f},
    {"R_front_R_toe", "R_front_R_toe_end", 1.25f},
    {"R_back_B_toe", "R_back_B_toe_end", 1.25f},
    {"R_back_L_toe", "R_back_L_toe_end", 1.25f},
    {"R_back_R_toe", "R_back_R_toe_end", 1.25f},
};

const SBoxJoint skShinJoints[4] = {
    {"L_front_knee_2", "L_front_ankle", 6.f},
    {"L_back_knee_2", "L_back_ankle", 6.f},
    {"R_front_knee_2", "R_front_ankle", 6.f},
    {"R_back_knee_2", "R_back_ankle", 6.f},
};

const float kJointMass = 1000.f; // .rodata 0x2C0

// The four knee locators (.rodata 0x0, fn_14_44F8) and toe target locators (.rodata 0x10,
// fn_14_4464).
const char* const skKneeLocators[4] = {"L_front_knee_2", "L_back_knee_2", "R_front_knee_2",
                                       "R_back_knee_2"};
const char* const skToeLocators[4] = {"L_front_col_LCTR", "L_back_col_LCTR", "R_front_col_LCTR",
                                      "R_back_col_LCTR"};

// lbl_14_bss_24: the toe colliders' extended touch bounds before the model scale, (0, 0, 2)
// (the static initialiser fn_14_D904 stores .rodata 0x264 = 0 twice and 0x26C = 2 at 0xDEEC).
const CVector3f skToeTouchBounds(0.f, 0.f, 2.f);

// The two strings joined (a copy of the first with the second appended): the box colliders'
// names are "<from>-><to>".
// @ fn_14_42F4
rstl::string Concat(const rstl::string& a, const rstl::string& b) {
  rstl::string out(a);
  out.append(b);
  return rstl::string(out);
}

// The box collider name: (from + "->") + to, the two fn_14_42F4 calls at 0x63AC and 0x63C8 with
// the "->" string at .rodata 0x905.
rstl::string BoxJointName(const SBoxJoint& joint) {
  return Concat(Concat(rstl::string_l(joint.from), rstl::string("->")),
                rstl::string_l(joint.to));
}







} // namespace

// fn_14_B100. The base call is the retail one: EPatternedAI 9 (li r4, 0x9 at 0x0000B154),
// flavor 0 (li r7, 0x0 at 0x0000B13C), movement 0 / collider 1 / body 1 (the stack words at
// 0x0000B150..0x0000B15C), all passed to CPatterned at 0x0000B164.
// Every arm ran: the asset-set arms naturally, the five "id != -1" skips, the null generator and
// the no-beacon-model echo skip forced (tools/cov/specs/digitalguardian_dg33_ctor.py; the one
// DGRD placement sets every id, so those arms are data-dead in the retail).
// @ fn_14_B100
CDigitalGuardian::CDigitalGuardian(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                                   const CTransform4f& xf, const CModelData& modelData,
                                   const CActorParameters& actParms, const CPatternedInfo& pInfo,
                                   const CDigitalGuardianData& data)
: CPatterned(static_cast< EPatternedAI >(9), uid, name, kFT_Zero, info, xf, modelData, pInfo,
             kMT_Ground, kCT_One, kBT_BiPedal, actParms)
, xFB0_(kInvalidUniqueId.value)
, xFB2_(kInvalidUniqueId.value)
, xFB4_(kInvalidUniqueId.value)
, xFB6_(kInvalidUniqueId.value)
, x10C4_(4)
, x10C8_{CSegId::Invalid(), CSegId::Invalid(), CSegId::Invalid(), CSegId::Invalid()}
, x1080_(kInvalidUniqueId.value)
, x1082_(kInvalidUniqueId) // the stores at 0xB2B0..0xB2BC
, x1084_(kInvalidUniqueId)
, x1086_(kInvalidUniqueId)
, x1010_(0)
, x1011_(0)
, x1012_(0)
, x1013_(0)
, x1168_(kInvalidUniqueId) // the store at 0xB5B8
, x116A_(kInvalidUniqueId) // the store at 0xB5C0
, x116C_(LdrToDamageInfo(data.mVortexDamage)) // the stores at 0xB5D4..0xB614
, x11B8_(0)
, x11BC_(0.f) // the store at 0xB65C
, x11C0_(kInvalidUniqueId) // the store at 0xB668
, x11C4_(-1) // the store at 0xB66C
, x11C8_(data.mUnknown4F5D725C) // the store at 0xB674
, x11CC_(CVector3f::Zero()) // the stores at 0xB67C..0xB68C
, x11D8_(-1) // the store at 0xB690
, x11DC_(-1) // the store at 0xB694
, x11E0_(0.f)
, x11E4_(0.f)
, x11E8_(1) // the store at 0xB6A0
, x1274_(0.f)
, x1278_(CVector3f::Zero())
, x1284_(-1)
, x1288_(0.f) // set to the speed in the body (the store at 0xBA28)
, x128C_(0x80) // bit 7 set (r6 = 1, the rlwimi at 0xBA30), bits 6..0 clear (0xBA3C..0xBA84)
, x128D_(0) // bits 7 and 6 clear (the rlwimis at 0xBA90 and 0xBA9C)
, xE60_(nullptr) // 0xE60: the first collision actor manager (x61DC_ builds it)
, xE64_(nullptr) // 0xE64: the second collision actor manager (x5C30_ builds it)
, xE6C_() // 0xE6C: the leg model (x6864_ fills it in)
, xEBC_() // 0xEBC: the shin model (x6864_ fills it in)
, xF0C_() // 0xF0C: the knee model (x6864_ fills it in)
, xF5C_() // 0xF5C: the toe target's model (x6864_ fills it in)
, x11EC_() // 0x11EC: the transmission beacon's model (x6864_ fills it in)
, x10C0_(0xFF)
, x10C1_(0xFF)
, x10C2_(0xFF)
, x10C3_(0xFF)
, x10D4_(0xFF)
, x10D5_(0xFF)
, x10D6_(0xFF)
, x10D7_(0xFF)
, x10DC_{-1, -1, -1, -1}
, x123C_(nullptr)
, x10CC_(kInvalidUniqueId)
, x1240_(CSfxHandle())
, x1244_()
, x1254_()
, x1264_(0.f)
, x126C_(0.f) // the store at 0xB9C4
, x1268_(0)
, x1270_(CColor::Black().GetColor_u32()) // Black() at 0xB9C8, the word stored at 0xB9DC
, xFBC_{kInvalidUniqueId.value, kInvalidUniqueId.value, kInvalidUniqueId.value,
        kInvalidUniqueId.value}
, x1078_{kInvalidUniqueId.value, kInvalidUniqueId.value, kInvalidUniqueId.value,
         kInvalidUniqueId.value}
, x1014_(LdrToDamageVulnerability(data.mKneeVulnerability)) // fn_14_3B4C, 0xB248
, x1044_(CDamageVulnerability::ReflectVulnerabilty()) // ImmuneRicochetVulnerability, 0xB258
, x1110_(LdrToDamageVulnerability(data.mToeTargetVulnerability))
, x1188_(LdrToDamageVulnerability(data.mVortexVulnerability))
, xE00_(actParms) // fn_14_C57C (0xB188)
, x10CE_(kInvalidUniqueId) // the store at 0xB31C
, x1100_()
, mData(data) // guest 0x7C0: the 0x640-byte parameter block fn_14_C68C copies in
{
  // The toe target's state (the stores at 0xB388..0xB3A4 and 0xB574..0xB5A0): no sound, the
  // full toeTargetHP (data +0x20) and a zero timer (.data 0x458).
  for (int i = 0; i < 4; ++i) {
    x10F0_[i] = CSfxHandle();
    x1144_[i] = mData.mToeTargetHP;
    x1158_[i] = 0.f;
  }
  // The explosion token (0xB3B0..0xB4A8): locked when toeTargetExplosion (data +0x28) is set.
  if (mData.mToeTargetExplosion != kInvalidAssetId) {
    x1100_ = TLockedToken< CGenDescription >(
        gpSimplePool->GetObj(SObjectTag('PART', mData.mToeTargetExplosion)));
  }
  // The beacon's explosion and echo-hit tokens (0xB6CC..0xB7B0 and 0xB834..0xB914), each
  // locked when its id is set.
  if (mData.mTransmissionBeaconExplosion != kInvalidAssetId) {
    x1244_ = TLockedToken< CGenDescription >(
        gpSimplePool->GetObj(SObjectTag('PART', mData.mTransmissionBeaconExplosion)));
  }
  if (mData.mTransmissionBeaconEchoHit != kInvalidAssetId) {
    x1254_ = TLockedToken< CGenDescription >(
        gpSimplePool->GetObj(SObjectTag('PART', mData.mTransmissionBeaconEchoHit)));
  }

  // The base speed: CPatterned's speed (guest 0x3C8) as the base left it (0xBA24..0xBA28).
  x1288_ = mSpeed;

  // The locator ids the retail constructor resolves with GetLocatorSegId (the calls from 0xBAB4
  // on): 0x10B8 is "Head_1" (.rodata 0x6BF, the store at 0xBAC8), 0x10B9 "neck_SDK" (0xBAFC), 0x10C0..0x10C3 the four knee locators
  // ("L_front_knee_2", "L_back_knee_2", "R_front_knee_2", "R_back_knee_2" - the stores at
  // 0xBB30, 0xBB64, 0xBB98 and 0xBBCC) and 0x10D4..0x10D7 the four ankle locators
  // ("L_front_ankle", "L_back_ankle", "R_front_ankle", "R_back_ankle" - the stores at 0xBCD0,
  // 0xBD04, 0xBD38 and 0xBD6C). The ids at 0x10C8..0x10CB (the "*_col_LCTR" locators) belong
  // to the melee trigger and are not resolved yet.
  const CAnimData* anim = GetAnimationData();
  x10B8_ = anim->GetLocatorSegId(rstl::string_l("Head_1")).val();
  x10B9_ = anim->GetLocatorSegId(rstl::string_l("neck_SDK")).val();
  x10C0_ = anim->GetLocatorSegId(rstl::string_l("L_front_knee_2")).val();
  x10C1_ = anim->GetLocatorSegId(rstl::string_l("L_back_knee_2")).val();
  x10C2_ = anim->GetLocatorSegId(rstl::string_l("R_front_knee_2")).val();
  x10C3_ = anim->GetLocatorSegId(rstl::string_l("R_back_knee_2")).val();
  x10D4_ = anim->GetLocatorSegId(rstl::string_l("L_front_ankle")).val();
  x10D5_ = anim->GetLocatorSegId(rstl::string_l("L_back_ankle")).val();
  x10D6_ = anim->GetLocatorSegId(rstl::string_l("R_front_ankle")).val();
  x10D7_ = anim->GetLocatorSegId(rstl::string_l("R_back_ankle")).val();
  // The melee trigger's locators (the stores at 0xBC00, 0xBC34, 0xBC68 and 0xBC9C, the names
  // from the pointer table at .rodata 0x10).
  x10C8_[0] = anim->GetLocatorSegId(rstl::string_l("L_front_col_LCTR"));
  x10C8_[1] = anim->GetLocatorSegId(rstl::string_l("L_back_col_LCTR"));
  x10C8_[2] = anim->GetLocatorSegId(rstl::string_l("R_front_col_LCTR"));
  x10C8_[3] = anim->GetLocatorSegId(rstl::string_l("R_back_col_LCTR"));

  // The AI weapon type passes through the immune-ricochet vulnerability (the SetVulnerability
  // call at 0xBEF4: weapon type 0xB, multiplier .rodata 0x264 = 0, effect 2, no ignore radius).
  x1044_.SetVulnerability(kWT_AI,
                          CWeaponTypeVulnerability(0.f, CWeaponTypeVulnerability::kE_PassThrough,
                                                   false));

  // fn_14_6864 (the call at 0xBD78): the module's five models.
  x6864_();

  // fn_14_D664 (the call at 0xB6AC): the beacon's particle generator, from the beacon's effect
  // asset (guest 0x950, the data block's mTransmissionBeaconFx).
  x123C_ = xD664_(mData.mTransmissionBeaconFx);
  // 0xBEF8..0xBF48: a built generator starts with its emission off (vtable 0x2C, r4 = 0; fn_14_28D0
  // turns it on) and the model's scale (the CModelData at 0x60, its first three floats) as its
  // global scale (vtable 0x24).
  if (x123C_ != nullptr) {
    x123C_->SetParticleEmission(false);
    x123C_->SetGlobalScale(GetModelData()->GetScale());
  }

  // The retail also fills two reserved vectors here (0xBD7C..0xBEE0): the knee directions at
  // 0xFC4 ((-1, 1, 0), (-1, -1, 0), (1, 1, 0), (1, -1, 0); only fn_14_77EC reads them, the host
  // keeps them as its kKneeDirections) and four angles at 0xFF8 (.data 0x45C..0x468: pi/4, 3pi/4,
  // -pi/4, -3pi/4) that no other code of the REL reads (its only 0xFF8 accesses are this ctor's).

  // The crippled 'SCAN' token (0xBF4C..0xC00C): the id (data +0) tested against -1 (addis r0,
  // r23, 0x1 / cmplwi r0, 0xffff / beq at 0xBF50..0xBF58), a 0xC-byte TCachedToken (operator new
  // at 0xBF74) cached at once (GetObj at 0xBFC8), stored at 0xE68 (0xBFFC).
  if (mData.mScannableInfoCrippled != kInvalidAssetId) {
    xE68_ = rs_new TCachedToken< CScannableObjectInfo >(
        gpSimplePool->GetObj(SObjectTag('SCAN', mData.mScannableInfoCrippled)), true);
  }

  mDamageColor = skHitsWithoutDamageColor; // 0x450 at 0xC010..0xC018

  // The beacon's echo emitter (0xC01C..0xC074): with the beacon's model (0x1238, 0x11EC's valid
  // flag), enabled, over the model's bounds at fn_14_2578's transform of the actor's (0x24), with
  // the beacon's echo parameters (0x954, the data block's +0x194).
  if (x11EC_.valid()) {
    CTransform4f xf = CTransform4f::Identity();
    x2578_(xf, GetTransform());
    AllocateEchoEmitter(true, x11EC_.data().GetBounds(xf),
                        LdrToEchoParameters(mData.mTransmissionBeaconEchoParameters));
  }
}

// fn_14_AB98: besides the members' own dtors, the beacon's particle generator (0x123C) is deleted
// through its vtable (+0x8 with r4 = 1, 0xAC34..0xAC44) and both collision actor managers (0xE64
// at 0xAE28, 0xE60 at 0xAE3C, the dtor with r4 = 1); the 0xE68 token (0xADF4..0xAE14) is
// xE68_'s own dtor.
// @ fn_14_AB98
CDigitalGuardian::~CDigitalGuardian() {
  delete x123C_;
  delete xE64_;
  delete xE60_;
}

// A body state is only ever installed by CBodyController::Activate, and CPatterned::Think /
// ThinkAboutMove / Death / KnockBack all dereference the current state. The analysis does not say
// where retail activates it, so the port activates on kSM_Create and, as the other patterned
// actors in this port do, again on demand in every entry point that can run first. The body
// controller itself is always built by CPatterned's constructor (CPatterned.cpp's BuildBodyController
// call), so the port limit here is only the activation point, not the pointer.
void CDigitalGuardian::EnsureBodyActive(CStateManager& mgr) {
  if (!BodyController()->GetIsActive()) {
    BodyController()->Activate(mgr, pas::kAS_Invalid);
  }
}

// fn_14_A688 (vtable slot 4): the base's PreThink (the call at 0xA694) and nothing else. The
// port keeps the body activation the base's PreThink does not do (CPatterned::PreThink only
// forwards to CEntity::PreThink), so the guardian still thinks.
// @ fn_14_A688
void CDigitalGuardian::PreThink(float dt, CStateManager& mgr) {
  EnsureBodyActive(mgr);
  CPatterned::PreThink(dt, mgr);
}

// vtable slot 5 = fn_14_A5BC. It tests the actor's active bit (the byte at 0x20, bit 0, read at
// 0x0000A5E0) and returns when it is clear, then runs CPatterned::Think.
// The arms: the actor's active bit (the byte at guest 0x20, bit 7, the extrwi at 0xA5E4) and
// the collision manager's state (guest this+0xE60 is null -> the managers are built with
// fn_14_676C, else they are updated). The run: area 53 (Sanctuary Fortress 09_cliff_temple_dark),
// 3000 frames, the coverage build (tools/cov/gcov_report.py: 4/4 branches, ALL-ARMS).
// @ fn_14_A5BC
void CDigitalGuardian::Think(float dt, CStateManager& mgr) {
  if (!GetActive()) {
    return;
  }
  EnsureBodyActive(mgr);
  CPatterned::Think(dt, mgr);
  // fn_14_A5BC (0xCC bytes) then looks at the first collision actor manager (the load of
  // guest 0xE60 at 0xA5F0): the retail builds both managers with fn_14_676C (the call at
  // 0xA604) when that one is null, and updates them instead when it is not (the two
  // CCollisionActorManager::Update calls at 0xA610 and 0xA620, with the options word 0).
  // The trigger is not an animation event: it is the manager at guest 0xE60 being null.
  if (xE60_ == nullptr) {
    x676C_(mgr);
  } else {
    xE60_->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);
    xE64_->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);
  }
  // The retail's four calls after the managers (fn_14_A5BC): the body turns to the head
  // (fn_14_6E04, the call at 0xA63C), the toe targets' sound emitters and hit timers
  // (fn_14_2358, the call at 0xA64C), the leg-stab, jump and protect-the-knees timers
  // (fn_14_38BC, the call at 0xA65C) and the knee armours follow the knee locators
  // (fn_14_3F90, the call at 0xA668).
  x6E04_(mgr, dt); // fn_14_6E04
  x2358_(mgr, dt); // fn_14_2358
  x38BC_(mgr, dt); // fn_14_38BC
  x3F90_(mgr);     // fn_14_3F90
}

// vtable slot 6 = fn_14_A6A8 (0x4F0), the biggest body function: it switches on the script
// message's four-character code (the message is a big-endian u32 of the four characters, the
// compare at 0xA6E0 and the ones after it). The ported cases are "ACTV" (0x41435456, the case
// at 0xA8DC), "DCTV" (0x44435456, the case at 0xA94C), the "IM00".."IM08" family
// (0x494D3030..0x494D3038, the cases at 0xAAB8, 0xAAC8, 0xAAE0, 0xAAF8, 0xAB10, 0xAB28,
// 0xAB40, 0xAB58 and 0xAB70), "OPEN" (0x4F50454E, the case at 0xAA3C), "CLOS" (0x434C4F53,
// the case at 0xAA80), "XDEL" (0x5844454C, the case at 0xA880), "XAUD" (0x58415544, the case
// at 0xA9A8), "XALD" (0x58414C44, the case at 0xA998), "XDMG" (0x58444D47, the case at
// 0xA9DC), "XRDG" (0x58524447, the case at 0xA9FC) and "XHIT" (0x58484954, the case at
// 0xAA1C) and "XCRT" (0x58435254, the case at 0xA81C).
// @ fn_14_A6A8
void CDigitalGuardian::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  // The retail reads the actor's active bit before it calls the base (the byte at guest 0x20,
  // bit 7, read at 0xA6C4) and the "ACTV" and "DCTV" cases test that value.
  const bool active = GetActive();
  CPatterned::AcceptScriptMsg(mgr, msg); // 0xA6DC
  switch (static_cast< uint >(msg.GetMessage())) { // the message (+8) at 0xA6BC
  case 0x41435456: // "ACTV"
    if (!active) { // 0xA8DC
      if (xE60_ != nullptr) { // the retail's test at 0xA8E8
        xE60_->SetActive(mgr, true); // 0xA8F8
      }
      // The second manager is only woken when the flag byte's bit 6 is set (the extrwi at
      // 0xA90C, the byte's bit 6 = 0x40).
      if (xE64_ != nullptr && (x128C_ & 0x40) != 0) { // the tests at 0xA900 and 0xA910
        xE64_->SetActive(mgr, true); // 0xA91C
      }
      x3E88_(mgr, true); // 0xA92C
      // The retail's call at 0xA944 passes the float at .rodata 0x264 (0.0f) as its last
      // argument and a pointer to that word as its state message; the code function ignores
      // both.
      ResetAttackTimes(mgr, 0.f); // fn_14_7B24
    }
    break;
  case 0x44435456: // "DCTV"
    if (active) {             // 0xA94C
      if (xE60_ != nullptr) { // the retail's test at 0xA958
        xE60_->SetActive(mgr, false); // 0xA968
      }
      if (xE64_ != nullptr) { // the retail's test at 0xA970
        xE64_->SetActive(mgr, false); // 0xA980
      }
      x3E88_(mgr, false); // 0xA990
    }
    break;
  case 0x4F50454E: // "OPEN"
    // The flag byte's bit 3 must be clear (the extrwi at 0xAA40, the byte's bit 3 = 0x08) and
    // the beacon's state word must not be 1 (the cmpwi at 0xAA4C).
    if ((x128C_ & 0x08) == 0 && x1268_ != 1) {
      x1268_ = 2; // 0xAA5C
      // The sound (the call at 0xAA78): the position is the actor's translation (guest 0x54),
      // the direction is the up vector and the parms are the data block's +0x1C0
      // (sound_BeaconExtract).
      PlayCustomSound(GetTranslation(), CVector3f::Up(), mData.mSoundBeaconExtract, false);
    }
    break;
  case 0x434C4F53: // "CLOS"
    // The flag byte's bit 3 and bit 4 (the rlwimi at 0xAA88 and 0xAA9C) and the object
    // request for the actor's own id (the halfword load at 0xAAA4).
    x128C_ |= 0x08;
    x128C_ |= 0x10;
    mgr.DeleteObjectRequest(GetUniqueId());
    break;
  case 0x5844454C: // "XDEL"
    if (xE60_ != nullptr) { // the retail's test at 0xA884
      xE60_->Destroy(mgr);  // 0xA890
    }
    if (xE64_ != nullptr) { // the retail's test at 0xA898
      xE64_->Destroy(mgr);  // 0xA8A4
    }
    if (x1240_ != CSfxHandle()) {        // cmplwi 0 at 0xA8AC
      CSfxManager::RemoveEmitter(x1240_); // 0xA8C0
      x1240_ = CSfxHandle();             // 0 at 0xA8C8
    }
    x3F14_(mgr); // fn_14_3F14 at 0xA8D4
    break;
  case 0x58415544: // "XAUD"
    if (xE60_ != nullptr) { // the retail's test at 0xA9AC
      xE60_->SetPhysicsActive(mgr, false); // 0xA9BC
    }
    if (xE64_ != nullptr) { // the retail's test at 0xA9C4
      xE64_->SetPhysicsActive(mgr, false); // 0xA9D4
    }
    break;
  case 0x58414C44: // "XALD"
    x5910_(mgr); // fn_14_5910 at 0xA9A0
    break;
  case 0x58444D47: // "XDMG": the sender (the message's +0, 0xA9DC)
    x5838_(mgr, msg.GetSenderId()); // fn_14_5838 at 0xA9F4
    break;
  case 0x58524447: // "XRDG"
    x5644_(mgr, msg.GetSenderId()); // fn_14_5644 at 0xAA14
    break;
  case 0x58484954: // "XHIT"
    x4F2C_(mgr, msg.GetSenderId()); // fn_14_4F2C at 0xAA34
    break;
  case 0x494D3030: // "IM00", 0xAAB8 (the bge at 0xA74C)
    x3F14_(mgr); // fn_14_3F14 at 0xAAC0
    break;
  case 0x494D3031: // "IM01", 0xAAC8
    x1010_ = 1;  // stb at 0xAAD0
    x3A44_(mgr); // fn_14_3A44 at 0xAAD8
    break;
  case 0x494D3032: // "IM02", 0xAAE0
    x1010_ = 0;  // stb at 0xAAE8
    x3A44_(mgr); // fn_14_3A44 at 0xAAF0
    break;
  case 0x494D3033: // "IM03", 0xAAF8
    x1012_ = 1;  // stb at 0xAB00
    x3A44_(mgr); // fn_14_3A44 at 0xAB08
    break;
  case 0x494D3034: // "IM04", 0xAB10
    x1012_ = 0;  // stb at 0xAB18
    x3A44_(mgr); // fn_14_3A44 at 0xAB20
    break;
  case 0x494D3035: // "IM05", 0xAB28
    x1013_ = 1;  // stb at 0xAB30
    x3A44_(mgr); // fn_14_3A44 at 0xAB38
    break;
  case 0x494D3036: // "IM06", 0xAB40
    x1013_ = 0;  // stb at 0xAB48
    x3A44_(mgr); // fn_14_3A44 at 0xAB50
    break;
  case 0x494D3037: // "IM07", 0xAB58
    x1011_ = 1;  // stb at 0xAB60
    x3A44_(mgr); // fn_14_3A44 at 0xAB68
    break;
  case 0x494D3038: // "IM08", 0xAB70
    x1011_ = 0;  // stb at 0xAB78
    x3A44_(mgr); // fn_14_3A44 at 0xAB80
    break;
  case 0x58435254: // "XCRT", the case at 0xA81C
    BodyController()->Activate(mgr, pas::kAS_Invalid);          // -1 at 0xA828
    x4080_(mgr);                                                 // fn_14_4080 at 0xA834
    x24E4_(mgr);                                                 // fn_14_24E4 at 0xA840
    SetVisorOrbitableFlags(CVisorParameters::kVOF_All, false);  // 0xF, 0 at 0xA850
    SetVisorOrbitableFlags(CVisorParameters::kVOF_Echo, true);  // 2, 1 at 0xA860
    RemoveMaterial(kMT_SeekerTarget, mgr);                       // 0x3F at 0xA870
    x14F8_();                                                    // fn_14_14F8 at 0xA878
    break;
  default:
    break;
  }
}

// fn_14_3E88 (0x88 bytes): the knee armours' active flag. The four armour ids (0xFB0, 0xFB2,
// 0xFB4 and 0xFB6 - the loop at 0x3EB0, the ids two bytes apart) are walked; every knee that
// exists (the compare with kInvalidUniqueId at 0x3EB4) is looked up (the ObjectById call at
// 0x3ED0) and its active flag is set (the CEntity::SetActive call through the
// vtable slot 0x1C at 0x3EE4) to the flag the message carries (the clrlwi at 0x3EA4, the low
// byte of the third argument).
// Every arm ran (tools/cov/specs/digitalguardian_arms.con).
// @ fn_14_3E88
void CDigitalGuardian::x3E88_(CStateManager& mgr, bool active) {
  const u16 ids[4] = {xFB0_, xFB2_, xFB4_, xFB6_};
  for (int i = 0; i < 4; ++i) {
    if (ids[i] == kInvalidUniqueId.value) {
      continue;
    }
    // The armours (fn_14_4080) are not cast: a plain ObjectById and the virtual SetActive.
    CEntity* armour = mgr.ObjectById(TUniqueId(ids[i]));
    if (armour != nullptr) { // the retail's test at 0x3ED4
      armour->SetActive(active);
    }
  }
}

// fn_14_3974 (0x58 bytes): the object with the id at guest 0x1080 (the halfword load at
// 0x3988) is looked up (the ObjectById call at 0x3998) and its active flag is set (the virtual
// call at 0x39B4, the vtable slot 0x1C, which is CEntity::SetActive) to the flag the third
// argument carries (the clrlwi at 0x39A8, its low byte).
// The arms: the root collider (the uid at guest this+0x1080) is there -> its active flag is
// set, or it is gone -> nothing happens (the retail's test at 0x398C). The run: area 53, 3000
// frames; the "it is gone" arm was forced (the id at 0x1080 pointed at an actor that does not
// exist, tools/cov/force.sh with /tmp/opencode/dgclose_3974.py: the CStateManager::ObjectById
// call site without the CEntity::SetActive one).
// @ fn_14_3974
void CDigitalGuardian::x3974_(CStateManager& mgr, bool active) {
  CEntity* actor = mgr.ObjectById(TUniqueId(x1080_));
  if (actor != nullptr) {
    actor->SetActive(active);
  }
}

// fn_14_39CC: the toe target colliders from 0x1078 up to the count at 0x1074 (the lwz at
// 0x3A1C; 4 from the ctor's store at 0xB270), each looked up (0x3A04) and, when it exists
// (0x3A08), given the vulnerability (SetDamageVulnerability, 0x3A14).
// @ fn_14_39CC
void CDigitalGuardian::x39CC_(CStateManager& mgr, const CDamageVulnerability& vuln) {
  for (int i = 0; i < 4; ++i) {
    CCollisionActor* collider =
        static_cast< CCollisionActor* >(mgr.ObjectById(TUniqueId(x1078_[i])));
    if (collider != nullptr) {
      collider->SetDamageVulnerability(vuln);
    }
  }
}

// fn_14_3DA8: the four knee armour ids from 0xFB0 (the loop's cmpwi 4 at 0x3E10); each id that is
// not kInvalidUniqueId (0x3DD8) and whose object exists (0x3DF4) is made orbitable or not
// (fn_14_CE1C, 0x3E04).
// @ fn_14_3DA8
void CDigitalGuardian::x3DA8_(CStateManager& mgr, bool orbitable) {
  const u16* const armor[4] = {&xFB0_, &xFB2_, &xFB4_, &xFB6_};
  for (int i = 0; i < 4; ++i) {
    if (*armor[i] == kInvalidUniqueId.value) {
      continue;
    }
    CDigitalGuardianKneeArmor* knee =
        static_cast< CDigitalGuardianKneeArmor* >(mgr.ObjectById(TUniqueId(*armor[i])));
    if (knee != nullptr) {
      knee->SetOrbitable(mgr, orbitable); // fn_14_CE1C
    }
  }
}

// fn_14_34E8: with the exit vortex waypoint (0x1168, looked up at 0x3534; none -> false), the
// speed 0x11BC grows by the data's vortexLinearAcceleration (guest 0x8A4) times dt up to its
// vortexLinearVelocity (0x8A0) (the fmadds at 0x354C and the fcmpo at 0x3550). The flat
// distance (z is 0.0f, .rodata 0x264, 0x3580) is compared with minDist (0x359C): beyond it, the
// body moves (MoveInOneFrameOR, 0x361C) along the flat direction - rotated into the body's frame
// (TransposeRotate, 0x360C) and only when the magnitude is safe (0x35C4) - by speed * dt or the
// distance, whichever is smaller (0x35AC), and the answer is whether speed * dt was smaller
// (the fcmpo/mfcr at 0x3620); within it, the speed is zeroed (0x3638) and the answer is false.
// @ fn_14_34E8
bool CDigitalGuardian::x34E8_(CStateManager& mgr, float dt, float minDist) {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(x1168_));
  if (target == nullptr) {
    return false;
  }
  // 0x3550 fcmpo x8A0, v; bge to the fmr of v: x8A0 >= v and a NaN give v.
  x11BC_ = rstl::min_val(mData.mVortexLinearAcceleration * dt + x11BC_,
                         mData.mVortexLinearVelocity);
  const CVector3f delta(target->GetTranslation().GetX() - GetTranslation().GetX(),
                        target->GetTranslation().GetY() - GetTranslation().GetY(), 0.f);
  const float dist = delta.Magnitude();
  // 0x359C..0x35A0 fcmpo, ble (taken on a NaN): a NaN distance stops.
  if (dist > minDist) {
    const float step = x11BC_ * dt;
    const float move = step < dist ? step : dist;
    if (delta.IsMagnitudeSafe()) {
      MoveInOneFrameOR(GetTransform().TransposeRotate(delta.AsNormalized() * move), dt);
    }
    return step < dist;
  }
  x11BC_ = 0.f;
  return false;
}

// fn_14_3674: only with bit 6 of 0x128D (the mask 0x40, the extrwi. at 0x36A4) set, bit 2 of
// 0x128C (the mask 0x04, the extrwi. at 0x36B0) clear and the data's vortexAttackDuration (guest
// 0x894) above 0.0f (0x36C4). The player (0x14FC) is pulled when its distance to the body is
// above 1.1920929e-7 (.rodata 0x2B0, 0x3710) and below maxVortexAttractionDistance (0x89C,
// 0x371C): the impulse is the unit direction times vortexAttractionForce (0x898), the player's
// mass (0x158) and dt (0x3734..0x3770), applied with no angular part (0x3790); then the player's
// collision impulses (0x3798), its minimal acceleration timer at 2 * dt (.rodata 0x26C, 0x37AC)
// and a static interference source from the body at 0.1 for 0.1 s (.rodata 0x2B4, 0x37D4).
// The arms: the pull and the 0x04 arm ran (fn_14_4F2C sets the bit on a vortex hit; the body
// placed on the player during VortexAttack); the player beyond maxVortexAttractionDistance was
// forced (tools/cov/force.sh, the distance set to 0.001 at the entry, 530 frames).
// @ fn_14_3674
void CDigitalGuardian::x3674_(CStateManager& mgr, float dt) {
  // 0x40: extrwi. at 0x36A4; 0x04: extrwi. at 0x36B0; the duration: fcmpo at 0x36C4.
  if ((x128D_ & 0x40) == 0 || (x128C_ & 0x04) != 0 || !(mData.mVortexAttackDuration > 0.f)) {
    return;
  }
  CPlayer* player = mgr.GetPlayer(0);
  const CVector3f delta = GetTranslation() - player->GetTranslation();
  const float dist = delta.Magnitude();
  if (dist > 1.1920929e-7f && dist < mData.mMaxVortexAttractionDistance) {
    const CVector3f dir = delta * (1.f / dist);
    const CVector3f impulse = dir * mData.mVortexAttractionForce * player->GetMass() * dt;
    player->ApplyImpulseWR(impulse, CAxisAngle::Identity());
    player->UseCollisionImpulses();
    player->SetMinimalAccelerationTimer(2.f * dt);
    mgr.PlayerState(0)->StaticInterference().AddSource(GetUniqueId(), 0.1f, 0.1f);
  }
}

// fn_14_4D54: the shake id (0x11C4) is reset to -1 (0x4D78); with the IS00/Attach object
// (0x11C0) a camera shaker (the TCastToPtr at 0x4D8C), its shake data is copied (fn_14_4A2C),
// moved to the body's translation (0x4DA4..0x4DC8) and added to player 0's camera shaker
// manager (guest mgr+0x151C, +0x88) without sound flags (r6 = r7 = 0, 0x4DD4); the id it returns
// is kept (0x4DD8).
// @ fn_14_4D54
void CDigitalGuardian::x4D54_(CStateManager& mgr) {
  x11C4_ = -1;
  if (CScriptCameraShaker* shaker = TCastToPtr< CScriptCameraShaker >(mgr.ObjectById(x11C0_))) {
    x11C4_ = mgr.CameraManager(0)->CameraShakerManager()->AddCameraShaker(
        shaker->GetShakeData().NewTranslation(GetTranslation()), mgr, false, false);
  }
}

// fn_14_4878: the same copy moved to the body's translation (0x48C0..0x48D8) updates the shake
// 0x11C4 (UpdateCameraShaker, 0x48E8), when 0x11C0 is a camera shaker (0x48A8).
// @ fn_14_4878
void CDigitalGuardian::x4878_(CStateManager& mgr) {
  if (CScriptCameraShaker* shaker = TCastToPtr< CScriptCameraShaker >(mgr.ObjectById(x11C0_))) {
    mgr.CameraManager(0)->CameraShakerManager()->UpdateCameraShaker(
        x11C4_, shaker->GetShakeData().NewTranslation(GetTranslation()));
  }
}

// fn_14_4838: the shake 0x11C4 is removed (RemoveCameraShaker, 0x4858) and the id reset to -1
// (0x4860).
// @ fn_14_4838
void CDigitalGuardian::x4838_(CStateManager& mgr) {
  mgr.CameraManager(0)->CameraShakerManager()->RemoveCameraShaker(x11C4_);
  x11C4_ = -1;
}

// fn_14_3300: with the IS05/Follow object (0x116A, GetObjectById at 0x3334; none: nothing, 0x333C)
// bit 7 of 0x128D is set (the rlwimi at 0x3348). The drift time is the body controller's
// animation time left (0x3354); when the current animation (the body controller's 0x58C) has a
// take-off event (13, 0x3378) and a landing event (12, 0x33D4), each unlike the infinite time
// {kT_Infinity, 1.0} (.data 0x0/0x4, the compares at 0x33B4 and 0x3410), it is landing minus
// take-off, no lower than 0.0f (.rodata 0x264, the fcmpo at 0x3430). With the time above 0.0f
// (0x3470) 0x11CC is the object's offset from the body times 1.0f (.rodata 0x270) / time
// (0x3454, 0x3484..0x349C); otherwise zero (0x34A4). The stores are 0x34B0..0x34C0.
// Retail reads the object's translation (0x54) without a type check; the host casts to CActor.
// The vortex's TakeOff comes from A_into_vortex_fast (meta 30), which the transition database plays
// into the locomotion anim A_vortex_fast (char 12, no events): the lookups ask about anim 12 and
// find no take-off. Only char 29 (meta 30) has both events, and no PAS state lists it, so the span
// arm is data-dead; B_jump (char 38) has a TakeOff and no Landing.
// gcov (dg34: tools/cov/specs/digitalguardian_dg34_3300.con natural, digitalguardian_dg34_3300.py
// forced anim 29 / landing 0): 8/8 branches, ALL-ARMS.
// @ fn_14_3300
void CDigitalGuardian::x3300_(CStateManager& mgr) {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(x116A_));
  if (target == nullptr) {
    return;
  }
  x128D_ |= 0x80;
  float time = GetBodyController()->GetAnimTimeRemaining();
  const int anim = GetBodyController()->GetCurrentAnimId();
  const CAnimData& animData = *GetModelData()->GetAnimationData();
  const CCharAnimTime infinite(CCharAnimTime::kT_Infinity, 1.f);
  const CCharAnimTime takeOff = animData.GetTimeOfUserEventForAnimation(anim, kUE_TakeOff);
  if (takeOff != infinite) {
    const CCharAnimTime landing = animData.GetTimeOfUserEventForAnimation(anim, kUE_Landing);
    if (landing != infinite) {
      const float span = landing.GetSeconds() - takeOff.GetSeconds();
      // 0x3430 fcmpo 0, span; bge to the 0: 0 >= span and a NaN give 0 (max_val(0, span)).
      time = rstl::max_val(0.f, span);
    }
  }
  if (time > 0.f) {
    x11CC_ = (target->GetTranslation() - GetTranslation()) * (1.f / time);
  } else {
    x11CC_ = CVector3f::Zero();
  }
}

// fn_14_4E00: the object connected on IS03 (FindConnectedObject at 0x4E70) with Attach while
// 0x11E8 is set, else Action (the cmpwi at 0x4E4C); when it is a camera shaker (the TCastToPtr at
// 0x4E8C), a copy of its shake data (fn_14_4A2C) placed at the event's locator (GetLctrTransform
// at 0x4EA4, the translation 0x4EC0..0x4ED8) is added to player 0's camera shaker manager without
// sound flags (r6 = r7 = 0, 0x4EE4); the returned id is dropped.
// @ fn_14_4E00
void CDigitalGuardian::x4E00_(CStateManager& mgr, const rstl::string& locator) {
  const TUniqueId id =
      FindConnectedObject(mgr, kSS_InternalState3, x11E8_ != 0 ? kSM_Attach : kSM_Action);
  if (CScriptCameraShaker* shaker = TCastToPtr< CScriptCameraShaker >(mgr.ObjectById(id))) {
    mgr.CameraManager(0)->CameraShakerManager()->AddCameraShaker(
        shaker->GetShakeData().NewTranslation(GetLctrTransform(locator).GetTranslation()), mgr,
        false, false);
  }
}

// fn_14_20E4: the leg whose ankle (.rodata 0x20) or toe collider locator (.rodata 0x10) is the
// name (fn_14_21A0, the string compare), the ankles first (0x2120, 0x2154); -1 when none of the
// four matches (the cmpwi 4 at 0x217C, 0x2188).
// gcov (tools/cov/specs/digitalguardian_arms.con, one run): 6/6 branches, ALL-ARMS.
// @ fn_14_20E4
int CDigitalGuardian::x20E4_(const rstl::string& name) const {
  static const char* const kAnkleLocators[4] = {"L_front_ankle", "L_back_ankle", "R_front_ankle",
                                                "R_back_ankle"};
  for (int i = 0; i < 4; ++i) {
    if (name == rstl::string_l(kAnkleLocators[i])) {
      return i;
    }
    if (name == rstl::string_l(skToeLocators[i])) {
      return i;
    }
  }
  return -1;
}

// fn_14_458C: the leg of the event's locator (fn_14_20E4); nothing without one (0x45B8) or when its
// toe target is destroyed (0x10DC[leg] == 3, 0x45C8). A "Shock Wave" (.rodata 0x2F4 + 0x606) is
// spawned at the locator's x and y with the body's z (0x45E0..0x4604), identity rotation
// (CTransform4f::Translate, 0x46B0), the body as its parent, the data's shockWaveInfo (guest
// 0x864, fn_14_476C) and 2.8f / 0.4f (.rodata 0x2B8 / 0x2BC); sound_ShockWave (guest 0x84C) plays
// there (0x4740) and the toe target goes to state 1 (fn_14_1A68, 0x4754).
// gcov (tools/cov/specs/digitalguardian_arms.con, one run): 12/12 branches, ALL-ARMS.
// @ fn_14_458C
void CDigitalGuardian::x458C_(CStateManager& mgr, const rstl::string& locator) {
  const int leg = x20E4_(locator);
  if (leg == -1 || x10DC_[leg] == 3) {
    return;
  }
  const CVector3f lctr = GetLctrTransform(locator).GetTranslation();
  const CVector3f pos(lctr.GetX(), lctr.GetY(), GetTranslation().GetZ());
  // The retail's order: the info (fn_14_476C, 0x4608) before the new (0x4628), then the entity
  // info (0x4674), the name (0x468C), the id (0x469C) and the transform (0x46B0). They are locals
  // here, not temporaries of the new expression, so no conditional-destruction flags.
  const CShockWaveInfo waveInfo(mData.mShockWaveInfo);
  const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, true);
  const rstl::string name = rstl::string_l("Shock Wave");
  const TUniqueId id = mgr.AllocateUniqueId();
  const CTransform4f xf = CTransform4f::Translate(pos);
  mgr.AddObject(rs_new CShockWave(id, name, info, xf, GetUniqueId(), waveInfo, 2.8f, 0.4f));
  PlayCustomSound(pos, CVector3f::Up(), mData.mSoundShockWave, false);
  x1A68_(mgr, leg, 1); // fn_14_1A68
}


// @ fn_14_44F8
bool CDigitalGuardian::x44F8_(const rstl::string& name, TUniqueId id) {
  for (int i = 0; i < 4; ++i) {
    if (name == rstl::string_l(skKneeLocators[i])) {
      xFBC_[i] = id.value;
      return true;
    }
  }
  return false;
}

// @ fn_14_4464
bool CDigitalGuardian::x4464_(const rstl::string& name, TUniqueId id) {
  for (int i = 0; i < 4; ++i) {
    if (name == rstl::string_l(skToeLocators[i])) {
      x1078_[i] = id.value;
      return true;
    }
  }
  return false;
}

// @ fn_14_4408
bool CDigitalGuardian::x4408_(TUniqueId id) const {
  return xFBC_[0] == id.value || xFBC_[1] == id.value || xFBC_[2] == id.value ||
         xFBC_[3] == id.value;
}

// @ fn_14_43AC
bool CDigitalGuardian::x43AC_(TUniqueId id) const {
  return x1078_[0] == id.value || x1078_[1] == id.value || x1078_[2] == id.value ||
         x1078_[3] == id.value;
}

// @ fn_14_61DC
// The first collision actor manager (guest 0xE60): 11 spheres and 24 boxes (the two loops at
// 0x6254 and 0x6324, room for 0x23 reserved at 0x6224), built active (li r8, 1 at 0x64B0); then
// every collider gets its vulnerability by name (the loop at 0x64FC).
void CDigitalGuardian::x61DC_(CStateManager& mgr) {
  // The vector's helpers are the module's own copies of rstl's.
  rstl::vector< CJointCollisionDescription > joints;
  joints.reserve(35); // fn_14_D7DC
  const CAnimData* anim = GetModelData()->GetAnimationData();
  for (uint i = 0; i < 11; ++i) {
    const SSphereJoint& joint = skSphereJoints[i];
    const CSegId seg = anim->GetLocatorSegId(rstl::string_l(joint.name));
    // fn_14_5FA4 (the description's copy)
    // fn_14_5F24 (push_back)
    joints.push_back(CJointCollisionDescription::SphereCollision(
        seg, CVector3f::Zero(), joint.radius, rstl::string_l(joint.name), kJointMass));
  }
  for (uint i = 0; i < 24; ++i) {
    const SBoxJoint& joint = skBoxJoints[i];
    const float size = joint.size;
    const CSegId from = anim->GetLocatorSegId(rstl::string_l(joint.from));
    const CSegId to = anim->GetLocatorSegId(rstl::string_l(joint.to));
    joints.push_back(CJointCollisionDescription::OBBAutoSizeCollision(
        from, to, CVector3f(size, size, size), CJointCollisionDescription::kOT_BetweenJoints,
        BoxJointName(joint), kJointMass));
  }
  CCollisionActorManager* manager =
      rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), joints, true);
  delete xE60_;
  xE60_ = manager;

  const CVector3f scale = GetModelData()->GetScale();
  for (uint i = 0; i < xE60_->GetNumCollisionActors(); ++i) {
    const CJointCollisionDescription& desc = xE60_->GetCollisionDescFromIndex(i);
    const TUniqueId id = desc.GetCollisionActorId();
    CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id));
    if (actor == nullptr) { // the TCastToPtr test at 0x652C
      continue;
    }
    if (x44F8_(desc.GetName(), id)) {
      // A knee joint (0x6550).
      actor->SetDamageVulnerability(x1044_);
    } else if (desc.GetName() == rstl::string_l("Head_1")) { // fn_14_21A0 (operator==)
      // The guard (0x6594): asleep until the beacon arrives (fn_14_28D0), the body's own
      // vulnerability (the non-virtual CPatterned call at 0x65B8).
      x10CC_ = id;
      actor->SetActive(false);
      actor->SetDamageVulnerability(*CPatterned::GetDamageVulnerability());
    } else if (x4464_(desc.GetName(), id)) {
      // A toe target (0x65EC): its touch bounds reach (0, 0, 2) times the model scale further.
      actor->SetDamageVulnerability(x1110_);
      actor->SetExtendedTouchBounds(CVector3f(skToeTouchBounds.GetX() * scale.GetX(),
                                              skToeTouchBounds.GetY() * scale.GetY(),
                                              skToeTouchBounds.GetZ() * scale.GetZ()));
    } else if (desc.GetName() == rstl::string_l("root")) {
      // The root (0x6658): the vortex's vulnerability, and platforms are excluded (the exclude
      // list's high word | 0x20000 at 0x6694, material 49).
      x1080_ = id.value;
      actor->SetDamageVulnerability(x1188_);
      CMaterialFilter filter = actor->GetMaterialFilter();
      filter.ExcludeList().Add(kMT_Platform);
      actor->SetMaterialFilter(filter);
    } else {
      actor->SetDamageVulnerability(x1044_);
    }
    // Every collider (0x66C8): the include list's high word | 0x4000000 (material 58) and the
    // exclude list's high word | 0x800 (material 43).
    CMaterialFilter filter = actor->GetMaterialFilter();
    filter.IncludeList().Add(kMT_NoPlayerCollision);
    filter.ExcludeList().Add(kMT_Immovable);
    actor->SetMaterialFilter(filter);
  }
  // fn_14_60C0 (the vector's destructor, 0x673C)
}

// @ fn_14_5C30
// The second collision actor manager (guest 0xE64): the four knee-to-ankle boxes (the loop at
// 0x5C98, .rodata 0x1D8), built inactive (li r8, 0 at 0x5E24); every collider is immune-ricochet
// and gets the same material bits as the first manager's (the loop at 0x5E58).
void CDigitalGuardian::x5C30_(CStateManager& mgr) {
  rstl::vector< CJointCollisionDescription > joints;
  joints.reserve(4); // fn_14_D7DC
  const CAnimData* anim = GetModelData()->GetAnimationData();
  for (uint i = 0; i < 4; ++i) {
    const SBoxJoint& joint = skShinJoints[i];
    const float size = joint.size;
    const CSegId from = anim->GetLocatorSegId(rstl::string_l(joint.from));
    const CSegId to = anim->GetLocatorSegId(rstl::string_l(joint.to));
    // fn_14_5FA4 (the description's copy)
    // fn_14_5F24 (push_back)
    joints.push_back(CJointCollisionDescription::OBBAutoSizeCollision(
        from, to, CVector3f(size, size, size), CJointCollisionDescription::kOT_BetweenJoints,
        BoxJointName(joint), kJointMass));
  }
  CCollisionActorManager* manager =
      rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), joints, false);
  delete xE64_;
  xE64_ = manager;

  for (uint i = 0; i < xE64_->GetNumCollisionActors(); ++i) {
    const TUniqueId id = xE64_->GetCollisionDescFromIndex(i).GetCollisionActorId();
    CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id));
    if (actor == nullptr) { // the TCastToPtr test at 0x5E84
      continue;
    }
    actor->SetDamageVulnerability(x1044_);
    // The include list's high word | 0x4000000 (material 58) and the exclude list's high word
    // | 0x800 (material 43), as at 0x5E98.
    CMaterialFilter filter = actor->GetMaterialFilter();
    filter.IncludeList().Add(kMT_NoPlayerCollision);
    filter.ExcludeList().Add(kMT_Immovable);
    actor->SetMaterialFilter(filter);
  }
  // fn_14_60C0 (the vector's destructor, 0x5F04)
}

// @ fn_14_59BC
// The first manager's colliders get their health (the loop at 0x59DC): a knee joint the knee
// armour's HP (guest 0x8E4), a toe target or the root the toe target's HP (0x7E0), the guard
// ("Head_1") the beacon's HP (0x9B0); each with the knockback resistance .rodata 0x274 (10).
void CDigitalGuardian::x59BC_(CStateManager& mgr) {
  for (uint i = 0; i < xE60_->GetNumCollisionActors(); ++i) {
    const CJointCollisionDescription& desc = xE60_->GetCollisionDescFromIndex(i);
    const TUniqueId id = desc.GetCollisionActorId();
    CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id));
    if (actor == nullptr) { // the TCastToPtr test at 0x5A0C
      continue;
    }
    if (x4408_(id)) {
      *actor->HealthInfo() = CHealthInfo(mData.mKneeArmorHP, 10.f);
    } else if (x43AC_(id) || id.value == x1080_) {
      *actor->HealthInfo() = CHealthInfo(mData.mToeTargetHP, 10.f);
    } else if (desc.GetName() == rstl::string_l("Head_1")) { // fn_14_21A0 (operator==)
      *actor->HealthInfo() = CHealthInfo(mData.mTransmissionBeaconHP, 10.f);
    }
  }
}

// fn_14_676C (0xF8 bytes): the collision actor managers' builder, in retail order: fn_14_61DC
// (the call at 0x678C), fn_14_5C30 (0x6798), fn_14_59BC (0x67A4), fn_14_3974 (0x67B4, with the
// third argument 0) and the material filter (the SetMaterialFilter call at 0x6844).
// @ fn_14_676C
void CDigitalGuardian::x676C_(CStateManager& mgr) {
  x61DC_(mgr);
  x5C30_(mgr);
  x59BC_(mgr);
  x3974_(mgr, false);
  // The material filter: each list is 1ull << a .data word (li r3,0; li r4,1; lwz r5; bl __shl2i,
  // a 64-bit shift): exclude 1 << 0x2F (.data 0x470, 0x67CC) | 1 << 0x1C (0x474, 0x67E8) |
  // 1 << 0x20 (0x478, 0x6804), include 1 << 0x3B (0x46C, 0x6820); type 3 (li r0,0x3 at 0x6824),
  // kFT_IncludeExclude; CActor::SetMaterialFilter at 0x6844.
  SetMaterialFilter(CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid),
      CMaterialList(kMT_CollisionActor, kMT_AIPassthrough, kMT_Player)));
}

// fn_14_A384 (vtable slot 9): the base's PreRender (the call at 0xA390) and nothing else.
// @ fn_14_A384
void CDigitalGuardian::PreRender(CStateManager& mgr) {
  CPatterned::PreRender(mgr);
}

// fn_14_A004 (vtable slot 10, AddToRenderer): the base's AddToRenderer (the call at 0xA01C),
// the module's GX register colour (the call at 0xA020), the base's render (the direct call at
// 0xA04C - this is where the guardian's model is drawn; the REL's own Render does not draw it),
// the character's alpha buffer alpha (the call at 0xA058), which is pushed to the renderer when
// it is not -1 (the call at 0xA068). Then the module's own models are drawn (the calls at 0xA0A0,
// 0xA0B4, 0xA0E8 and 0xA0FC), the GX register colour goes back to black (the Black() call at
// 0xA0B8 and the renderer call at 0xA0BC), and the alpha is pushed again (the call at 0xA108; the
// cmpwi at 0xA100 skips it when the alpha is -1), and the end turns the destination alpha off
// again (0xA118: vtable 0x12C = DisableDestinationAlpha; 0x128 at 0xA07C is SetDestinationAlpha).
//
// The four module calls, by size and by what their pseudo-C does:
//   fn_14_1994 (0xD4): if the flag byte at guest this+0xEB8 is set, every locator id in the
//     array at this+0x10C0 is walked and the model at this+0xE6C is drawn at that locator's
//     scaled transform.
//   fn_14_18B4 (0xE0): the same walk, but when bit 4 of the flag byte at this+0x128C is clear
//     and the byte at this+0xF08 is set, drawing the model at this+0xEBC.
//   fn_14_17DC (0xD8): when bit 3 of the flag byte at this+0x128C is clear, the byte at
//     this+0x1238 is set and the float at this+0x1264 is positive, the model at this+0x11EC is
//     drawn at the transform fn_14_2578 builds; the echo emitter at this+0x123C is then drawn
//     through the renderer.
//   fn_14_1700 (0xDC): the locator ids at this+0x10D4 are walked and the model at this+0xF5C
//     is drawn at each of their scaled transforms.
//
// The four calls get the actor's transform (guest this+0x24, the CActor's mTransform) and the
// actor's draw flags (guest this+0xFC, the CActor's mDrawFlags) - the retail's r5 and r6.
// @ fn_14_A004
void CDigitalGuardian::AddToRenderer(const CStateManager& mgr) const {
  CPatterned::AddToRenderer(mgr);
  gpRender->SetGXRegister1Color(sGXRegister1Color);
  CPatterned::Render(mgr);
  const int alpha = GetRenderAlphaBufferAlpha(mgr);
  if (alpha != -1) {
    gpRender->SetDestinationAlpha(alpha);
  }
  // fn_14_1994 (0xA0A0): the module's leg model render.
  x1994_(mgr, GetTransform(), GetModelFlags());
  // fn_14_18B4 (0xA0B4): the module's second model render.
  x18B4_(mgr, GetTransform(), GetModelFlags());
  gpRender->SetGXRegister1Color(CColor::Black());
  // fn_14_17DC (0xA0E8): the module's echo emitter render.
  x17DC_(mgr, GetTransform());
  // fn_14_1700 (0xA0FC): the module's toe model render.
  x1700_(mgr, GetTransform(), GetModelFlags());
  if (alpha != -1) { // cmpwi r29,-0x1 at 0xA100
    gpRender->DisableDestinationAlpha(); // 0xA118: gpRender vtable 0x12C, no argument
  }
}

// The retail's per-segment rotation: the character's rest pose rotation for that segment. The
// retail reads it from the actor's model data (guest this+0x60, the CActor's mModelData), the
// anim data (that+0x10), the character layout (that+0x10C) and the layout's linear rotations
// (that+0x68), indexed with the segment id - the chain
// CScriptSpecialFunction::ThinkObjectFollowJoint walks as well.
//
// Port limit: the locator ids are still invalid in the port (the constructor does not resolve
// them - the model data's builder is not ported yet), and the retail would index the rotation
// array with 0xFF, so an invalid id has no rotation here.
CTransform4f CDigitalGuardian::SegmentRotation_(const CSegId& seg) const {
  if (seg == CSegId::Invalid()) {
    return CTransform4f::Identity();
  }
  const CModelData& model = *GetModelData();
  if (!model.HasAnimation()) {
    return CTransform4f::Identity();
  }
  const CCharLayoutInfo& layout = *model.GetAnimationData()->GetCharLayoutInfo();
  const rstl::vector< CQuaternion >& rotations = layout.GetLinearRotations();
  return rotations[seg.val()].BuildTransform4f();
}

// fn_14_1994 (0xD4 bytes): the leg model. The flag byte at guest this+0xEB8 has to be set (the
// test at 0x19B4); then each of the four locator ids at this+0x10C0 is walked (the loop at
// 0x19D4-0x1A50): the segment's rotation (the BuildTransform4f call at 0x19EC), the locator's
// scaled transform (the GetScaledLocatorTransform call at 0x19FC), the actor's transform times
// the locator's (the __ml__ at 0x1A0C), times the segment's rotation (the __ml__ at 0x1A1C), and
// the model at this+0xE6C is drawn there (the Render call at 0x1A40).
// Arms (dg30): model there (natural) / absent (forced: dg30_models, the five ids unset at
// fn_14_6864's entry). Retail indexes the rotations with the raw byte; SegmentRotation_'s
// invalid-id and no-animation arms are port limits, not retail arms.
// @ fn_14_1994
void CDigitalGuardian::x1994_(const CStateManager& mgr, const CTransform4f& xf,
                              const CModelFlags& flags) const {
  if (!xE6C_.valid()) {
    return;
  }
  // The ids are the bytes the retail loads (the lbz at 0x19D4): the first one is the byte the
  // load reads (the mask is the whole byte), the retail steps the pointer for the other three.
  const uchar ids[4] = {x10C0_ & 0xFF, x10C1_, x10C2_, x10C3_};
  for (int i = 0; i < 4; ++i) {
    const CSegId seg(ids[i]);
    const CTransform4f modelXf = xf * GetScaledLocatorTransform(seg) * SegmentRotation_(seg);
    xE6C_.data().Render(mgr, modelXf, GetActorLights(), flags);
  }
}

// fn_14_18B4 (0xE0 bytes): the shin model. The same walk as fn_14_1994 (the ids at this+0x10C0,
// the locator transform at 0x1928, the two __ml__ calls at 0x1938 and 0x1948), but the model at
// this+0xEBC is drawn (the Render call at 0x196C) and only when bit 4 of the flag byte at
// this+0x128C is clear (the extrwi at 0x18D8 - the byte's bit 4, the mask 1u << (31 - 27) =
// 0x10) and the byte at this+0xF08 is set (the test at 0x18E4).
// @ fn_14_18B4
void CDigitalGuardian::x18B4_(const CStateManager& mgr, const CTransform4f& xf,
                              const CModelFlags& flags) const {
  if ((x128C_ & 0x10) != 0 || !xEBC_.valid()) {
    return;
  }
  // The ids are the bytes the retail loads (the lbz at 0x1900): the first one is the byte the
  // load reads (the mask is the whole byte), the retail steps the pointer for the other three.
  const uchar ids[4] = {x10C0_ & 0xFF, x10C1_, x10C2_, x10C3_};
  for (int i = 0; i < 4; ++i) {
    const CSegId seg(ids[i]);
    const CTransform4f modelXf = xf * GetScaledLocatorTransform(seg) * SegmentRotation_(seg);
    xEBC_.data().Render(mgr, modelXf, GetActorLights(), flags);
  }
}

// fn_14_17DC (0xD8 bytes): the echo emitter's model. When bit 3 of the flag byte at this+0x128C
// is clear (the extrwi at 0x17FC), the byte at this+0x1238 is set (the test at 0x1808) and the
// float at this+0x1264 is positive (the fcmpo at 0x181C), the transform fn_14_2578 builds (the
// call at 0x182C) is copied (0x1838) and the model at this+0x11EC is drawn there (the Render
// call at 0x1870) with the flags the retail builds on the stack: the blend mode 2 (the stb at
// 0x1848), the shader set 0 (the stb at 0x1854), the flags 3 (the sth at 0x1860) and the packed
// colour at this+0x1270 (the stw at 0x1868).
//
// Then the echo emitter at this+0x123C, when it is there (the test at 0x1878), is drawn through
// the renderer: the retail calls the renderer's vtable slot at 0x44 (the lwz at 0x1954 and the
// bctrl at 0x1958).
// The ble at 0x1820 also skips on a NaN height, as the port's `> 0.f` does.
// Arms (dg30): flag 0x08 (dg29 msgs), no beacon model (forced: dg30_models), height 0 / up
// (natural), no emitter (forced: dg30_beacon, fn_14_D664's id unset).
// @ fn_14_17DC
void CDigitalGuardian::x17DC_(const CStateManager& mgr, const CTransform4f& xf) const {
  if ((x128C_ & 0x08) == 0 && x11EC_.valid() && x1264_ > 0.f) {
    CTransform4f modelXf = CTransform4f::Identity();
    x2578_(modelXf, xf);
    const CModelFlags flags(
        CModelFlags::kT_Two, 0,
        static_cast< CModelFlags::EFlags >(CModelFlags::kF_DepthCompare |
                                           CModelFlags::kF_DepthUpdate),
        CColor(x1270_));
    x11EC_.data().Render(mgr, modelXf, GetActorLights(), flags);
  }
  if (x123C_ != nullptr) {
    // The retail calls the renderer's vtable slot at 0x44 (the lwz at 0x1954 and the bctrl at
    // 0x1958) with the echo emitter in r4 (the lwz at 0x1874). The DOL's __vt__13CCubeRenderer
    // has CCubeRenderer::AddParticleGen(const CParticleGen&) in that slot (the entry at 0x44,
    // the 18th - the counting the SpacePirate lane used for 0x128), and the echo emitter at
    // 0x123C is a CElementGen - a CParticleGen.
    gpRender->AddParticleGen(*x123C_);
  }
}

// fn_14_1700 (0xDC bytes): the toe target's model. The four locator ids at this+0x10D4 are
// walked (the loop at 0x1738-0x17C4) and each one is drawn (the Render call at 0x17B0) unless
// the word at this+0x10DC is 3 (the cmpwi at 0x173C). The transform is the actor's, times the
// locator's scaled transform (the GetScaledLocatorTransform call at 0x176C), times the segment's
// rotation (the two __ml__ calls at 0x177C and 0x178C).
// Arms (dg30): toe state 3 / other (natural); the port-only no-model guard forced (dg30_models).
// @ fn_14_1700
void CDigitalGuardian::x1700_(const CStateManager& mgr, const CTransform4f& xf,
                              const CModelFlags& flags) const {
  // The ids are the bytes the retail loads (the lbz at 0x1744): the first one is the byte the
  // load reads (the mask is the whole byte), the retail steps the pointer for the other three.
  const uchar ids[4] = {x10D4_ & 0xFF, x10D5_, x10D6_, x10D7_};
  // The retail draws the model without testing the optional's flag (0xFA8): its optional
  // default-constructs the model, so the draw is of an empty model. The host's optional leaves
  // the data unconstructed while it is invalid, so the draw is skipped instead.
  if (!xF5C_.valid()) {
    return;
  }
  for (int i = 0; i < 4; ++i) {
    if (x10DC_[i] == 3) {
      continue;
    }
    const CSegId seg(ids[i]);
    const CTransform4f modelXf = xf * GetScaledLocatorTransform(seg) * SegmentRotation_(seg);
    xF5C_.data().Render(mgr, modelXf, GetActorLights(), flags);
  }
}

// fn_14_2578 (0xF4 bytes): the beacon's transform. The retail walks the locator at guest
// this+0x10B9 (the GetScaledLocatorTransform call at 0x26A4), multiplies the actor's transform
// with it (the __ml__ at 0x26B4) and, when the beacon's model is there (the byte at this+0x1238,
// the model's valid flag - the test at 0x26C4), moves the result along its own z axis by
// (this+0x1264 - 0.5f) * (0.8f * the model's height) - the constants are .rodata 0x29C (0.5f)
// and 0x2A0 (0.8f) and the height comes from the model's bounds (the GetBounds call at 0x25D8:
// the fsubs at 0x25F0 and 0x25FC).
// The no-model arm (0x25CC) is dead: all four callers test 0x1238 before the call - fn_14_17DC
// (0x1804, call 0x182C), fn_14_9ABC (0x9BF4, call 0x9C20), fn_14_A138 (0xA2B0,
// call 0xA2DC) and the constructor fn_14_B100 (0xC01C, call 0xC034). The model arm ran (natural).
// @ fn_14_2578
void CDigitalGuardian::x2578_(CTransform4f& out, const CTransform4f& xf) const {
  CTransform4f result = xf * GetScaledLocatorTransform(CSegId(x10B9_));
  if (x11EC_.valid()) {
    const CAABox bounds = x11EC_.data().GetBounds();
    const float height = bounds.GetMaxPoint().GetZ() - bounds.GetMinPoint().GetZ();
    const float scale = (x1264_ - 0.5f) * (0.8f * height);
    // The transform's z axis (the retail's m02, m12 and m22, read at 0x2600, 0x2608 and 0x260C).
    const CVector3f axis = result.GetUp();
    result.SetTranslation(result.GetTranslation() + axis * scale);
  }
  out = result;
}

// fn_14_D664 (0x178 bytes): the beacon's particle generator. The asset id is the "PART" object
// (the SObjectTag the retail builds at 0xD690: the type "PART" and the id) and the generator is a
// CElementGen with the normal orientation and the kOSF_One flag (the li r5, 0 and the li r6, 1
// at 0xD720). An invalid asset id (the cmplwi at 0xD674) gives no generator. The retail's
// allocation also carries a memory-node name (a .rodata label); the host's rs_new has none.
// The arms: the asset id is invalid (kInvalidAssetId) -> no generator, else the generator is
// built from the beacon's effect. The run: area 53 (Sanctuary Fortress 09_cliff_temple_dark), 3000
// frames; the "invalid" arm was forced (the asset id set to kInvalidAssetId at the entry,
// tools/cov/force.sh with /tmp/opencode/dgclose_D664.py).
// @ fn_14_D664
CElementGen* CDigitalGuardian::xD664_(CAssetId id) {
  if (id == kInvalidAssetId) {
    return nullptr;
  }
  const TToken< CGenDescription > token(gpSimplePool->GetObj(SObjectTag('PART', id)));
  return rs_new CElementGen(token);
}

// fn_14_2CC4 (0x19C bytes): the beacon's update. It runs while bit 3 of the flag byte at guest
// this+0x128C is clear (the extrwi at 0x2CF0 - the byte's bit 3, the mask 1u << (31 - 28) =
// 0x08). The state word at 0x1268 drives the beacon's height (0x1264): in state 2 it rises by
// the frame time and is clamped to 1.0 (the fcmpo at 0x2D28, the constant 1.0f at .rodata
// 0x270), and when it reaches 1.0 (the fcmpo at 0x2D48 with the cror eq, gt, eq) the beacon's
// arrival is handled (fn_14_28D0) and the state becomes 1 (the store at 0x2D64); in state 3 it
// falls by twice the frame time (the constant 2.0f at .rodata 0x26C) and is clamped to 0.0 (the
// fcmpo at 0x2D8C), and when it reaches 0.0 (the fcmpo at 0x2DA8 with the cror eq, lt, eq) the
// state becomes 0 (the store at 0x2DB8).
//
// The sound emitter (the handle at 0x1240, set by fn_14_28D0) follows the neck locator: the
// GetLctrTransform call at 0x2DD4, the position at 0x24(sp), 0x34(sp) and 0x44(sp) (the
// transform's translation) and the volume is 0x7F while the echo visor is active (the
// GetActiveVisor call at 0x2DF8 - the retail's visor 1) and the low byte of 0x974 otherwise
// (the clrlwi at 0x2E10; the guest's 0x974 is the data block's +0x1B4, the transmission
// beacon's sound parms' maxVolume).
// Every arm ran (dg33): flag 0x08, states 2/3/other with both ends, the handle with both visors.
// @ fn_14_2CC4
void CDigitalGuardian::x2CC4_(CStateManager& mgr, float dt) {
  if ((x128C_ & 0x08) != 0) {
    return;
  }
  if (x1268_ == 2) { // 0x2CFC; 3 is 0x2D0C..0x2D14, anything else skips (0x2D08, 0x2D10)
    // 0x2D28 fcmpo 1, v; bge to the fmr of v: 1 >= v and a NaN give v (min_val(v, 1)).
    x1264_ = rstl::min_val(x1264_ + dt, 1.f); // .rodata 0x270
    if (x1264_ >= 1.f) {                      // 0x2D48 cror eq, gt, eq
      x28D0_(mgr);
      x1268_ = 1; // 0x2D64
    }
  } else if (x1268_ == 3) {
    // 0x2D88 fcmpo 0, v; bge to the fmr of 0: 0 >= v and a NaN give 0 (max_val(0, v)).
    x1264_ = rstl::max_val(0.f, x1264_ - 2.f * dt); // .rodata 0x264, 0x26C
    if (x1264_ <= 0.f) {                            // 0x2DA8 cror eq, lt, eq
      x1268_ = 0;                                   // 0x2DB8
    }
  }
  if (x1240_) {
    const CVector3f position = GetLctrTransform(CSegId(x10B9_)).GetTranslation();
    // The retail's visor 1 is the echo visor (the host's EPlayerVisor keeps the retail's
    // order: combat, echo, scan, dark).
    const uchar volume = mgr.GetPlayerState()->GetActiveVisor(mgr) == CPlayerState::kPV_Echo
                             ? 0x7F
                             : static_cast< uchar >(mData.mSoundTransmissionBeacon.maxVolume);
    CSfxManager::UpdateEmitter(x1240_, position, CVector3f::Up(), volume);
  }
  x2A28_(mgr, dt);
}

// fn_14_28D0 (0x158 bytes): the beacon's arrival. The guard's collision actor (the uid at
// 0x10CC - the lhz at 0x28F0 and the ObjectById call at 0x28FC) is woken: when its flag
// word's bit 24 is clear (the extrwi at 0x2910 - the byte's bit 7, the mask 0x80 - the CEntity's
// first bitfield, mActive) the CEntity::SetActive(true) runs (the vtable slot 0x1C). Then the
// particle generator starts emitting (the CParticleGen's vtable 0x2C, SetParticleEmission, with
// r4 = 1 at 0x293C; SetGeneratorRate would take f1), the arrival sound plays (the PlayCustomSound call at 0x29E8: the position is
// the neck locator's translation, the direction the up vector and the parms are the
// transmission beacon's sound parms - the guest's 0x968..0x97F - with the maxVolume at 0x7F
// while the echo visor is active) and its handle goes to 0x1240, and the dark visor may orbit
// the beacon (the SetVisorOrbitableFlags call at 0x2A0C - the flag 8 is the dark visor).
// Every arm ran (dg33): the collider inactive/active and (forced) unset, the emitter set and
// (forced, D664) null, the handle set/clear, both visors.
// @ fn_14_28D0
void CDigitalGuardian::x28D0_(CStateManager& mgr) {
  CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(x10CC_));
  if (actor != nullptr && !actor->GetActive()) {
    actor->SetActive(true);
  }
  if (x123C_ != nullptr) {
    x123C_->SetParticleEmission(true); // vtable 0x2C
  }
  if (!x1240_) {
    const CVector3f position = GetLctrTransform(CSegId(x10B9_)).GetTranslation();
    SLdrAudioPlaybackParms parms = mData.mSoundTransmissionBeacon;
    if (mgr.GetPlayerState()->GetActiveVisor(mgr) == CPlayerState::kPV_Echo) {
      parms.maxVolume = 0x7F;
    }
    x1240_ = PlayCustomSound(position, CVector3f::Up(), parms, true);
  }
  SetVisorOrbitableFlags(CVisorParameters::kVOF_Dark, true);
}

// fn_14_2A28 (0x29C bytes): the beacon's effect update. While the beacon is up the echo flash
// (0x126C) fades and tints the beacon model (0x1270), and the effect's external parameter 0 is
// set; the effect always updates. With the linked head (the TCastToPtr<CDigitalGuardianHead> of
// 0x1084 at 0x2B04) the effect is aimed at it: the start is the neck locator's (0x10B9,
// 0x2B1C) translation plus 1.5f (.rodata 0x2A4, 0x2B4C) times the beacon model's bounds height
// (x11EC's GetBounds at 0x2B34, max.z - min.z at 0x2B50) along the locator's z column (0xC0/0xD0/
// 0xE0, 0x2B68..0x2B98); the end is the head's GetAimPosition(mgr, 0.0f) (vtable 0x54 at 0x2BB4,
// .rodata 0x264). The effect's global orientation (CParticleGen vtable 0x1C, 0x2C54) is the
// LookAt from start to end with the up vector (0x2C3C), its global translation (0x20, 0x2C6C) the
// start, and the parameter is dt times the distance (Magnitude at 0x2BEC, the fmuls at 0x2BF4);
// without a head the parameter is the 1.0f loaded at 0x2AF4 (.rodata 0x270).
// asmcheck's CMP x126C (0x2AB0) is rstl::max_val's compare. Arms (dg30): no emitter / no model
// (forced: dg30_beacon / dg30_models), beacon down / up, head there / gone (dg29 r1).
// @ fn_14_2A28
void CDigitalGuardian::x2A28_(CStateManager& mgr, float dt) {
  if (x123C_ == nullptr || !x11EC_.valid()) { // the tests at 0x2A60 and 0x2A6C
    return;
  }
  if (x1268_ != 0) { // 0x2A78
    // 0x2A90..0x2A9C fcmpo 0, v; bge to the fmr of 0: 0 >= v and a NaN give 0.
    x126C_ = rstl::max_val(0.f, x126C_ - dt); // .rodata 0x264
    // 0x2AB0..0x2ABC fcmpo 1, v; bge to the fmr of v: 1 >= v and a NaN give v.
    const float t = rstl::min_val(x126C_, 1.f); // .rodata 0x270
    x1270_ = CColor::Lerp(CColor::Black(), CPatterned::skDamageColor, t).GetColor_u32();
    float param = 1.f; // .rodata 0x270, 0x2AF4
    if (CDigitalGuardianHead* head =
            TCastToPtr< CDigitalGuardianHead >(mgr.ObjectById(x1084_))) { // 0x2B0C
      const CTransform4f neck = GetLctrTransform(CSegId(x10B9_));
      const CAABox bounds = x11EC_->GetBounds();
      const float height = 1.5f * (bounds.GetMaxPoint().GetZ() - bounds.GetMinPoint().GetZ());
      const CVector3f start = neck.GetTranslation() + neck.GetColumn(kDZ) * height;
      const CVector3f dir = head->GetAimPosition(mgr, 0.f) - start;
      param = dt * dir.Magnitude();
      x123C_->SetGlobalOrientation(CTransform4f::LookAt(start, start + dir, CVector3f::Up()));
      x123C_->SetGlobalTranslation(start);
    }
    static_cast< CElementGen* >(x123C_)->SetExternalParam(0, param); // 0x2C7C
  }
  x123C_->Update(dt); // vtable 0xC
}

// fn_14_6864 (0x1BC bytes): the constructor's tail builds the module's five models - the leg
// model at 0xE6C (the asset id at 0x8DC, the data block's mLegModel), the shin model at 0xEBC
// (0x8E0, mShinArmor), the knee model at 0xF0C (0x918, mKneeArmor), the transmission beacon's
// model at 0x11EC (0x94C, mTransmissionBeacon) and the toe target's model at 0xF5C (0x7E4,
// mToeTargetModel). Each one is built from a CStaticRes {the asset id, the actor's model scale}
// - the scale comes from the actor's model data at 0x60 (the three lfs at 0x68A0, 0x68A4 and
// 0x68A8) - and assigned into the member's optional (fn_14_6A20 at 0x68D4: the optional's
// assignment - fn_14_6A50 reads the valid flag at +0x4C, constructs the model when the optional
// was empty (fn_14_528) and copies it otherwise (fn_14_6A98); the host's optional_object does
// the same). The temporary is destroyed right after (the __dt__ at 0x68E0). An invalid asset id
// (the cmplwi at 0x689C) leaves the optional empty.
// Arms (dg30): every id set (natural: the one DGRD placement sets all five) / unset (forced:
// dg30_models). The compares are 0x689C, 0x68EC, 0x6930, 0x6974, 0x69B8.
// @ fn_14_6864
void CDigitalGuardian::x6864_() {
  const CVector3f scale = GetModelData()->GetScale();
  if (mData.mLegModel != kInvalidAssetId) {
    xE6C_ = CModelData(CStaticRes(mData.mLegModel, scale));
  }
  if (mData.mShinArmor != kInvalidAssetId) {
    xEBC_ = CModelData(CStaticRes(mData.mShinArmor, scale));
  }
  if (mData.mKneeArmor != kInvalidAssetId) {
    xF0C_ = CModelData(CStaticRes(mData.mKneeArmor, scale));
  }
  if (mData.mTransmissionBeacon != kInvalidAssetId) {
    x11EC_ = CModelData(CStaticRes(mData.mTransmissionBeacon, scale));
  }
  if (mData.mToeTargetModel != kInvalidAssetId) {
    xF5C_ = CModelData(CStaticRes(mData.mToeTargetModel, scale));
  }
}

// fn_14_A3D4 (vtable slot 11, Render): the module's render flag is set (the store at 0xA3FC)
// and cleared again (the store at 0xA44C) around the module's own RenderSystemsToBeDrawnLast
// (the virtual call at 0xA440 - vtable slot 47, the REL's fn_14_A3A4), which forwards to the
// base's while the flag is set. The character's render mask and target start at zero (the
// stores at 0xA400 and 0xA404) and are only filled when the particles are drawn (the flag byte
// at guest this+0x421, bit 0 - mDrawParticles, the clrlwi. at 0xA40C): the retail's
// GenerateParticlePOIMask (the call at 0xA420) is the host's
// CStateManager::GetCharacterRenderMaskAndTarget. The base's Render is never called: the
// retail's guardian draws its model through the render list (fn_14_A004), not through the
// render path.
// Arms (dg30): mDrawParticles set (natural) / clear (forced: dg30_beacon after 40 renders).
// @ fn_14_A3D4
void CDigitalGuardian::Render(const CStateManager& mgr) const {
  sRendering = 1;
  uint mask = 0;
  uint target = 0;
  if (mDrawParticles) {
    GenerateParticlePOIMask(mgr, mask, target);
  }
  RenderSystemsToBeDrawnLast(mgr, mask, target);
  sRendering = 0;
}

// vtable slot 13 = fn_14_A138: after the base, the other bounds (0xCC) take in the model's own
// bounds (0xA18C) and the particles' total bounds when there are any (0xA1B8); they become the
// other and render bounds (0xE4). The echo emitter (0xC8), if any, goes inactive with bit 0x08 of
// 0x128C (extrwi. 1, 28); else, with the beacon model (0x11EC) and its height (0x1264) above 0
// (.rodata 0x264), it takes the model's bounds at fn_14_2578's transform.
// Runs 37-39 ran the no-emitter arm (with and without particle bounds); with the constructor's
// emitter, run 402 ran the beacon arm and the emitter-without-beacon-height arm. Bit 0x08 is set
// only by "CLOS", which also requests the body's deletion; run 404 (forced: send <body> CLOS)
// shows the body gone before its next pre-render, so the inactive arm cannot run.
// @ fn_14_A138
void CDigitalGuardian::PreRenderAllViewports(CStateManager& mgr) {
  CPatterned::PreRenderAllViewports(mgr);
  CAABox bounds = GetOtherBounds();
  bounds.Include(GetModelData()->GetBounds());
  const rstl::optional_object< CAABox > particles =
      GetModelData()->GetAnimationData()->GetParticleDB().GetTotalBounds();
  if (particles) {
    bounds.Include(*particles);
  }
  SetOtherBounds(bounds);
  SetRenderBounds(bounds);
  if (CEchoEmitter* echo = EchoEmitter()) {
    if ((x128C_ & 0x08) != 0) {
      echo->SetActive(false);
    } else if (x11EC_ && x1264_ > 0.f) {
      CTransform4f xf = CTransform4f::Identity();
      x2578_(xf, GetTransform());
      echo->SetBounds(x11EC_->GetBounds(xf));
    }
  }
}

// vtable slot 15 = fn_14_10C (0x2C): it calls the vtable's HealthInfo slot (0x38) and returns
// its result, which is what the base class's own accessor does: a REL copy of the DOL's
// CActor::GetHealthInfo (0x8000B900, the same 11 instructions), so it is cited, not marked.
const CHealthInfo* CDigitalGuardian::GetHealthInfo() const { return CPatterned::GetHealthInfo(); }

// fn_14_9FE4 (vtable slot 16): the "pass through" vulnerability (the call at 0x9FF0) is the
// answer - the guardian is not hurt by anything, the shots pass through it.
// @ fn_14_9FE4
const CDamageVulnerability* CDigitalGuardian::GetDamageVulnerability() const {
  return &CDamageVulnerability::PassThroughVulnerabilty();
}

// fn_14_10 (vtable slot 18): the actor's bounding box (the GetBoundingBox call at 0x28) is the
// touch bounds, and the optional is always valid (the byte at 0x18 is set to 1 at 0x30). A REL
// copy of the DOL's CPatterned::GetTouchBounds (0x80073BF0, the same 26 instructions).
// @ fn_14_10
rstl::optional_object< CAABox > CDigitalGuardian::GetTouchBounds() const {
  return rstl::optional_object< CAABox >(GetBoundingBox());
}

// vtable slot 21 = fn_14_9E88: the "Head_1" locator's position (0x10B8, CPatterned's
// GetLctrTransform). With the guard's collision actor (0x10CC) active, its translation plus 4
// (.rodata 0x2A8) times the scale's z along up; else, with 0x11B8 set, only z becomes the
// translation's z plus 5 (.rodata 0x27C) times the scale's z. dt is unused.
// Runs 37-39 ran the active collision actor arm and the locator arm; run 423 ran the 0x11B8 arm,
// forced (`fsm <body> VortexAttack`, then `shoot <body>`, which aims at GetAimPosition).
// @ fn_14_9E88
CVector3f CDigitalGuardian::GetAimPosition(const CStateManager& mgr, float dt) const {
  const CVector3f locator = GetLctrTransform(CSegId(x10B8_)).GetTranslation();
  const float scaleZ = GetModelData()->GetScale().GetZ();
  const CCollisionActor* col = TCastToConstPtr< CCollisionActor >(mgr.GetObjectById(x10CC_));
  if (col != nullptr && col->GetActive()) {
    return col->GetTranslation() + CVector3f::Up() * (4.f * scaleZ);
  }
  if (x11B8_ != 0) {
    return CVector3f(locator.GetX(), locator.GetY(), GetTranslation().GetZ() + 5.f * scaleZ);
  }
  return locator;
}

// vtable slot 28 = fn_14_A468: types 3..27 (subi 3, cmplwi 0x18 at 0xA49C) go through
// jumptable_14_data_5CC; a handled type skips CPatterned's (the flag r6, 0xA574):
// 3 AlignTargetPos (+0xC4): fn_14_73D4 (0xA534). 5 ChangeMaterial (+0x60): bit 5 of 0x128C off
// (fn_14_3D2C with 0, 0xA4CC) and the knees' vulnerabilities (fn_14_3A44, 0xA4D8).
// 12 Landing (+0xAC): bit 7 of 0x128D cleared (the rlwimi at 0xA51C). 13 TakeOff (+0x7C): in the
// vortex (0x11B8 == 1, 0xA4E8) the drift (fn_14_3300, 0xA4F0), else bit 5 of 0x128C on
// (fn_14_3D2C with 1, 0xA4FC) and fn_14_3A44 (0xA508). 16 ScreenShake (+0xD8): fn_14_4E00 with
// the locator (node +0x30, 0xA544). 27 EventStart (+0xE8): fn_14_458C with the locator (0xA554).
// 17 BeginAction (+0xF8): bit 6 of 0x128D set (the rlwimi at 0xA568). Every other type: CPatterned
// (0xA590).
// gcov (tools/cov/specs/digitalguardian_arms.con, one run): 10/10 branches, ALL-ARMS.
// @ fn_14_A468
void CDigitalGuardian::DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node,
                                       EUserEventType type, float dt) {
  switch (type) {
  case kUE_AlignTargetPos:
    SelectMeleeAttackRange(mgr, 0.f); // fn_14_73D4
    return;
  case kUE_ChangeMaterial:
    x3D2C_(mgr, false); // fn_14_3D2C
    x3A44_(mgr);        // fn_14_3A44
    return;
  case kUE_Landing:
    x128D_ &= ~0x80; // the rlwimi at 0xA51C
    return;
  case kUE_TakeOff:
    if (x11B8_ == 1) {
      x3300_(mgr); // fn_14_3300
    } else {
      x3D2C_(mgr, true); // fn_14_3D2C
      x3A44_(mgr);       // fn_14_3A44
    }
    return;
  case kUE_ScreenShake:
    x4E00_(mgr, node.GetLocatorName()); // fn_14_4E00
    return;
  case kUE_EventStart:
    x458C_(mgr, node.GetLocatorName()); // fn_14_458C
    return;
  case kUE_BeginAction:
    x128D_ |= 0x40; // the rlwimi at 0xA568
    return;
  default:
    break;
  }
  CPatterned::DoUserAnimEvent(mgr, node, type, dt);
}

// fn_14_9DBC (vtable slot 29): when bit 6 (the mask 0x40, the extrwi at 0x9DCC) of the flag byte
// at guest this+0x128C is set and the scannable token (guest this+0xE68, the lwz at 0x9DD4) is
// there, that token's object is the answer (the lwz at 0x9DE0); otherwise the base's scannable
// info (the call at 0x9DE8). The ctor builds the token (0xBF4C..0xC00C).
// @ fn_14_9DBC
CScannableObjectInfo* CDigitalGuardian::GetScannableObjectInfo() const {
  if ((x128C_ & 0x40) != 0 && xE68_.get() != nullptr) {
    return xE68_->GetObject();
  }
  return CPatterned::GetScannableObjectInfo();
}

// vtable slot 41 = fn_14_78 (0x10): retail stores skDamageHitTime into the damage-cooldown timer
// at 0x448 and ignores the damage, exactly as CPatterned::TakeDamage does.
void CDigitalGuardian::TakeDamage(const CVector3f& direction, float magnitude) {
  CPatterned::TakeDamage(direction, magnitude);
}

// fn_14_F0 (vtable slot 45): the actor's translation (guest this+0x54, the three loads at
// 0xF0/0xF8/0x100) is the origin.
CVector3f CDigitalGuardian::GetOrigin(const CStateManager& mgr, const CTeamAiRole& role,
                                      const CVector3f& point) const {
  return GetTranslation();
}

// fn_14_A3A4 (vtable slot 47): the base's call, but only while the module's render flag is set
// (the byte at lbl_14_bss_0, the lbz at 0xA3B4). The flag is set by the module's render
// around its own body (fn_14_A3D4, the stores at 0xA3FC and 0xA44C).
// Every arm ran (tools/cov/specs/digitalguardian_arms.con).
// @ fn_14_A3A4
void CDigitalGuardian::RenderSystemsToBeDrawnLast(const CStateManager& mgr, uint mask,
                                                  uint target) const {
  if (sRendering) {
    CPatterned::RenderSystemsToBeDrawnLast(mgr, mask, target);
  }
}

// vtable slot 60 = fn_14_9DFC (0x9DFC..0x9E88): the base's setup first (the call at 0x9E10), then
// the module's three tables on the state manager at this+0x350 - the triggers (lbl_14_data_9C,
// 12 = 0xC, li r5 at 0x9E20) through the vtable slot at +0x18 (the bctrl at 0x9E30), the states
// (lbl_14_data_204, 14 = 0xE at 0x9E40) through +0x14 (0x9E50) and the codes (lbl_14_data_380,
// 13 = 0xD at 0x9E60) through +0x1C (0x9E70). No branches; ran in every run.
// @ fn_14_9DFC
void CDigitalGuardian::SetupStateMachine(CStateManager& mgr) {
  CPatterned::SetupStateMachine(mgr);
  StateMachine* stateMachine = mStateMachine.get();
  stateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  stateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  // The host's FSM1 does not dispatch code nodes yet (TStateMachineState::SetCodeFunctions is
  // empty), so this only records the table retail registers.
  stateMachine->SetCodeFunctions(skCodes, ARRAY_SIZE(skCodes));
}

// ---------------------------------------------------------------------------------------------
// The body's FSM functions. Retail's entries are the module's own functions; their bodies are
// not ported in this slice, so each one logs the retail function it stands in for and returns
// the neutral value (false for a trigger, nothing for a state or a code).
//
// The 12 triggers of lbl_14_data_9C (.data 0x9C; fn_14_D904's stores at 0xDB50..0xDCC8).

// fn_14_9AA8: the base's animation state id (guest this+0x6B4) with 3. The asm tests it with
// "subfic r0, r0, 3; cntlzw r0, r0; srwi r3, r0, 5" (0x9AAC..0x9AB4), i.e. the answer is true
// when the id is 3 - the host enum's kAS_Over (the ctor's state id 0 is "not ready").
// @ fn_14_9AA8
bool CDigitalGuardian::StateOver(CStateManager& mgr, const CTriggerData& data) const {
  return mAnimationState.GetState() == CAnimationState::kAS_Over;
}

// fn_14_9A9C: bit 7 of the flag byte at guest this+0x128C. The asm loads the byte (0x9A9C) and
// extracts it with "rlwinm r3, r0, 25, 31, 31" (0x9AA0), which keeps the byte's bit 7 (the
// mask 1u << (31 - 24) = 0x80).
// @ fn_14_9A9C
bool CDigitalGuardian::IsIdle(CStateManager& mgr, const CTriggerData& data) const {
  return (x128C_ & 0x80) != 0;
}

// fn_14_9A60: the three floats at guest this+0x1278, 0x127C and 0x1280 against 0.0f
// (lbl_14_rodata_264, loaded at 0x9A68 and 0x9A9C); the answer is true when any of them is
// not zero (the three fcmpu/bne pairs at 0x9A70..0x9A8C).
// @ fn_14_9A60
bool CDigitalGuardian::HasFaceTarget(CStateManager& mgr, const CTriggerData& data) const {
  return x1278_.GetX() != 0.f || x1278_.GetY() != 0.f || x1278_.GetZ() != 0.f;
}

// fn_14_9A48: the word at guest this+0x1284 (0x9A48). The asm computes
// "~value | (value + 1)" ("subfic r3, r4, -1; addi r0, r4, 1; or r0, r3, r0" at 0x9A4C..0x9A54)
// and keeps its top bit ("srwi r3, r0, 31" at 0x9A58). That bit is clear only when the value
// is -1 (0xFFFFFFFF): ~(-1) is 0 and (-1) + 1 is 0, and for every other value one of the two
// terms has its top bit set. So the answer is "the knee index is set".
// @ fn_14_9A48
bool CDigitalGuardian::KneeDestroyed(CStateManager& mgr, const CTriggerData& data) const {
  return x1284_ != -1;
}

// fn_14_9A0C: the word at guest this+0x1284 with -1 (the cmpwi at 0x9A1C); when it is -1 the
// answer is false (the li at 0x9A34), otherwise it is "fn_14_705C() == 0" (the call at
// 0x9A24 and the cntlzw/srwi pair at 0x9A28..0x9A2C).
// @ fn_14_9A0C
bool CDigitalGuardian::AllKneesDestroyed(CStateManager& mgr, const CTriggerData& data) const {
  if (x1284_ == -1) {
    return false;
  }
  return x705C_() == 0;
}

// fn_14_9994: the head module's uid (guest this+0x1082, the lhz at 0x99A8) is looked up in the
// state manager (the calls at 0x99B8 and 0x99BC); if the head exists and its flag byte
// (head+0x10F4) has bit 5 set (the rlwinm. at 0x99CC keeps the byte's bit 5, the mask
// 1u << (31 - 26) = 0x20, and the bne at 0x99D0 takes the "no" branch), the answer is false.
// Otherwise the answer is whether the float at guest this+0x1274 is at most zero (the fcmpo
// cr0 at 0x99E0, the "cror eq, lt, eq" at 0x99E4 and the mfcr/extrwi pair at 0x99E8..0x99EC:
// the mfcr puts CR0 in the top four bits of r0, so the extracted bit 29 is CR0's eq, which the
// cror has just made "f1 <= f0").
// The arms: gcov had the body's answer; the head's bit 0x20 (set by its ProjectileTelegraph)
// answered false in the area-53 head-stun runs (local probe).
// @ fn_14_9994
bool CDigitalGuardian::ShouldProtectKnees(CStateManager& mgr, const CTriggerData& data) const {
  if (HeadFlag20_(mgr)) {
    return false;
  }
  return x1274_ <= 0.0f;
}

// fn_14_9930: the head module's uid (guest this+0x1082, the lhz at 0x9944) is looked up; if the
// head exists and its flag byte (head+0x10F4) has bit 5 set (the extrwi. at 0x9968 keeps the
// byte's bit 5, the mask 1u << (31 - 26) = 0x20), the answer is false (the bne at 0x996C and
// the li at 0x997C). Otherwise the answer is bit 0 (the mask 1u << (31 - 30) = 0x1) of the flag
// byte at guest this+0x128C (the clrlwi at 0x9974).
// The arms: gcov had the body's answer; the head's bit 0x20 (set by its ProjectileTelegraph)
// answered false in the area-53 head-stun runs (local probe).
// @ fn_14_9930
bool CDigitalGuardian::ShouldVortexAttack(CStateManager& mgr, const CTriggerData& data) const {
  if (HeadFlag20_(mgr)) {
    return false;
  }
  return (x128C_ & 0x1) != 0;
}

// fn_14_97F0: the head's flag byte (head+0x10F4, bit 5) is tested first (the extrwi. at
// 0x9854); when it is set the answer is false. Then the float at guest this+0x11E0
// must be at most zero (the fcmpo cr0 at 0x9868, the cror eq, lt, eq at 0x986C and the bne at
// 0x9870). Then the four locator ids at guest this+0x10C8 (the count at 0x10C4) are walked
// (the loop at 0x98E0..0x98EC): each locator's world position comes from GetLctrTransform
// (the call at 0x98A0) and the player's (state manager+0x14FC, the lwz at 0x987C) is compared
// with it in the horizontal plane; the squared distance (the fsubs/fmuls/fmadds at
// 0x98B0..0x98C8) must be below the far leg stab range squared (guest this+0x7C8 = the data
// block's +0x08, the fmuls at 0x9884).
// Every arm ran (tools/cov/specs/digitalguardian_arms.con).
// @ fn_14_97F0
bool CDigitalGuardian::ShouldMeleeAttack(CStateManager& mgr, const CTriggerData& data) const {
  if (HeadFlag20_(mgr)) {
    return false;
  }
  if (!(x11E0_ <= 0.0f)) { // the cror/bne at 0x986C: a NaN answers false too
    return false;
  }
  const float range = mData.mFarLegStabRange;
  const float rangeSq = range * range;
  const CVector3f player = mgr.GetPlayer(0)->GetTranslation();
  for (int i = 0; i < x10C4_; ++i) {
    const CVector3f locator = GetLctrTransform(x10C8_[i]).GetTranslation();
    const float dx = player.GetX() - locator.GetX();
    const float dy = player.GetY() - locator.GetY();
    // 0.0f (.rodata 0x264) + dx * dx + dy * dy (0x98C4/0x98C8); the bge at 0x98D0 is taken on a
    // NaN too, so a NaN distance does not answer true.
    if (0.0f + (dx * dx + dy * dy) < rangeSq) {
      return true;
    }
  }
  return false;
}

// fn_14_97A4: the head's flag byte (head+0x10F4, bit 5) is the whole answer (the extrwi at
// 0x97D4 keeps the byte's bit 5, the mask 1u << (31 - 26) = 0x20), but only when the head
// exists (the cmplwi at 0x97C8 and the beq at 0x97CC answer false without it).
// @ fn_14_97A4
bool CDigitalGuardian::ShouldProjectileAttack(CStateManager& mgr, const CTriggerData& data) const {
  return HeadFlag20_(mgr);
}

// The head test fn_14_9994, fn_14_9930, fn_14_97F0 and fn_14_97A4 share: the head (0x1082) exists
// and bit 0x20 of its flag byte (head+0x10F4) is set.
bool CDigitalGuardian::HeadFlag20_(CStateManager& mgr) const {
  const CDigitalGuardianHead* head =
      TCastToConstPtr< CDigitalGuardianHead >(mgr.GetObjectById(x1082_));
  return head != nullptr && (head->x10F4_ & 0x20) != 0;
}

// fn_14_308C: the objects connected with the state kSS_InternalState2 (0x49533032) and the
// message kSM_Follow (0x464F4C57) are collected (the FindConnectedObjects call at 0x30CC; the
// retail's own vector copy and free, fn_14_31AC and fn_14_32AC, are the host's rstl::vector),
// and the closest active waypoint to the given position wins: each id is looked up (0x3108) and
// cast to CScriptWaypoint (0x310C); a waypoint that is not active (the byte at +0x20, bit 7,
// 0x3118) is skipped, and its squared distance to the position (the fsubs at 0x3130..0x3150)
// has to be below the best so far (3.4028235e+38f, lbl_14_rodata_2AC, loaded at 0x30F0). The
// winner's id (the u16 at +0x8, 0x3160) is the answer (the sth at 0x3180).
// Every arm ran (dg33): an inactive waypoint (console DCTV of 00350017); the non-waypoint arm
// (beq at 0x3114) is data-dead (all four IS02/FOLW links are waypoints) and was forced.
// @ fn_14_308C
TUniqueId CDigitalGuardian::x308C_(CStateManager& mgr, const CVector3f& position) const {
  const rstl::vector< TUniqueId > objects =
      FindConnectedObjects(mgr, kSS_InternalState2, kSM_Follow);
  TUniqueId best = kInvalidUniqueId;
  float bestSq = FLT_MAX;
  for (uint i = 0; i < objects.size(); ++i) {
    const CScriptWaypoint* waypoint = TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(objects[i]));
    if (waypoint == nullptr || !waypoint->GetActive()) {
      continue;
    }
    const float sq = (waypoint->GetTranslation() - position).MagSquared();
    if (sq < bestSq) {
      bestSq = sq;
      best = waypoint->GetUniqueId();
    }
  }
  return best;
}

// fn_14_9678: the float at guest this+0x11E4 must be at most zero (the fcmpo cr0 at 0x96A0,
// the cror eq, lt, eq at 0x96A4 and the bne at 0x96A8). Then the closest active waypoint
// (fn_14_308C, the call at 0x96D8) is looked up (0x96F0); without it the answer is false
// (the cmplwi at 0x96F4). The target must be further than 100.0f (lbl_14_rodata_290, the
// fcmpo at 0x9734) from the guardian, and then the player must be closer to the target than to
// the guardian (the two distance sums at 0x973C..0x9774, compared at 0x9778).
// @ fn_14_9678
bool CDigitalGuardian::ShouldJumpAttack(CStateManager& mgr, const CTriggerData& data) const {
  // 0x96A0..0x96A8 fcmpo, cror eq, lt, eq; bne: only x11E4 <= 0 goes on, so a NaN returns false.
  if (!(x11E4_ <= 0.0f)) {
    return false;
  }
  const CVector3f player = mgr.GetPlayer(0)->GetTranslation();
  const TUniqueId targetId = x308C_(mgr, player);
  const CEntity* target = mgr.GetObjectById(targetId);
  if (target == nullptr) {
    return false;
  }
  // The id comes from x308C_, which only ever returns waypoint ids, so the cast cannot fail.
  const CVector3f targetPos = TCastToConstPtr< CActor >(target)->GetTranslation();
  const CVector3f selfPos = GetTranslation();
  if ((targetPos - selfPos).MagSquared() <= 100.0f) {
    return false;
  }
  return (targetPos - player).MagSquared() < (selfPos - player).MagSquared();
}

// fn_14_966C: bit 2 (the mask 1u << (31 - 29) = 0x4) of the flag byte at guest this+0x128C (the
// lbz at 0x966C and the extrwi at 0x9670).
// @ fn_14_966C
bool CDigitalGuardian::VortexStruckPlayer(CStateManager& mgr, const CTriggerData& data) const {
  return (x128C_ & 0x4) != 0;
}

// fn_14_9638: the word at guest this+0x11B8 must be 1 (the cmpwi at 0x963C and the bne at
// 0x9640), and then the float at guest this+0x11C8 (the data block's +0x100, i.e.
// mUnknown4F5D725C) must be at most zero (the fcmpo cr0 at 0x9650, the cror eq, lt, eq at
// 0x9654 and the mfcr/extrwi pair at 0x9658..0x965C).
// @ fn_14_9638
bool CDigitalGuardian::BreakOutOfVortex(CStateManager& mgr, const CTriggerData& data) const {
  if (x11B8_ != 1) {
    return false;
  }
  return x11C8_ <= 0.0f;
}

// @ fn_14_3A44
void CDigitalGuardian::x3A44_(CStateManager& mgr) {
  const uchar flags[4] = {x1010_, x1011_, x1012_, x1013_};
  const u16* const armor[4] = {&xFB0_, &xFB2_, &xFB4_, &xFB6_};
  for (int i = 0; i < 4; ++i) {
    if (*armor[i] == kInvalidUniqueId.value) {
      continue;
    }
    // Open when the knee's flag byte is set and bit 5 of 0x128C (the extrwi at 0x3A8C) is clear.
    bool open = false;
    if ((x128C_ & 0x20) == 0 && flags[i] != 0) {
      open = true;
    }
    CDigitalGuardianKneeArmor* knee =
        static_cast< CDigitalGuardianKneeArmor* >(mgr.ObjectById(TUniqueId(*armor[i])));
    if (knee != nullptr) { // the retail's test at 0x3AB8
      knee->SetArmored(mgr, !open); // fn_14_CE94
    }
    const CDamageVulnerability vuln(open ? x1014_ : x1044_); // fn_14_3B4C (0x3AEC)
    CCollisionActor* collider =
        static_cast< CCollisionActor* >(mgr.ObjectById(TUniqueId(xFBC_[i])));
    if (collider != nullptr) { // the retail's test at 0x3B04
      collider->SetDamageVulnerability(vuln);
    }
  }
}

// @ fn_14_3E2C
int CDigitalGuardian::x3E2C_(TUniqueId id) const {
  if (xFBC_[0] == id.value) {
    return 0;
  }
  if (xFBC_[1] == id.value) {
    return 1;
  }
  if (xFBC_[2] == id.value) {
    return 2;
  }
  if (xFBC_[3] == id.value) {
    return 3;
  }
  return -1;
}

// @ fn_14_143C
void CDigitalGuardian::x143C_(CStateManager& mgr, const CVector3f& pos,
                              EScriptObjectState state) {
  const TUniqueId id = CheckConnectedObject(mgr, kSS_InternalState6, kSM_Follow);
  CScriptWaypoint* waypoint = TCastToPtr< CScriptWaypoint >(mgr.ObjectById(id));
  if (waypoint != nullptr) { // the TCastToPtr test at 0x14A4
    waypoint->SetTranslation(pos);
    SendScriptMsgs(state, mgr, GetUniqueId(), kSM_None);
  }
}

// fn_14_5838: the head collider (0x10CC) goes to fn_14_5274, a knee collider (fn_14_3E2C) to
// fn_14_5470, a toe target (fn_14_43AC) or the root collider (0x1080) to fn_14_5384.
// @ fn_14_5838
void CDigitalGuardian::x5838_(CStateManager& mgr, TUniqueId sender) {
  if (sender == x10CC_) {
    x5274_(mgr, sender); // fn_14_5274
    return;
  }
  const int knee = x3E2C_(sender); // fn_14_3E2C
  if (knee != -1) {
    x5470_(mgr, sender, knee); // fn_14_5470
  } else if (x43AC_(sender) || sender.value == x1080_) {
    x5384_(mgr, sender); // fn_14_5384
  }
}

// @ fn_14_4F2C
// "XHIT": a collider (the sender, TCastToPtr at 0x4F6C) whose last touched object is the player
// (0x4F80..0x4F94) damages the player, unless bit 6 of 0x128C is set (0x4F50). In the vortex
// (0x11B8 == 1) with bit 2 of 0x128C clear (0x4FA8) it is a big strike (0x421, 0x4FC0) for 1 s
// (0x73C, .rodata 0x270) with the vortex damage (0x116C), and bit 2 is set (0x5058); otherwise,
// once the damage wait (0x440 <= 0.0f, 0x507C) is over and the player is not morphed (0x38C,
// 0x5068), with the contact damage (vtable 0xE8, 0x50F4). Both apply the damage from the body to
// the player with an include filter of material 0x3B (.data 0x8, __shl2i) and no knockback
// (sZeroVector), then restart the damage wait (0x444 -> 0x440).
void CDigitalGuardian::x4F2C_(CStateManager& mgr, TUniqueId sender) {
  if ((x128C_ & 0x40) != 0) {
    return;
  }
  CCollisionActor* collider = TCastToPtr< CCollisionActor >(mgr.ObjectById(sender));
  if (collider == nullptr) {
    return;
  }
  const CPlayer* player = mgr.GetPlayer(0);
  if (collider->GetLastTouchedObject() != player->GetUniqueId()) {
    return;
  }
  const CMaterialFilter filter =
      CMaterialFilter::MakeIncludeExclude(CMaterialList(1ULL << 0x3B), CMaterialList());
  if (x11B8_ == 1 && (x128C_ & 0x04) == 0) {
    mIsMakingBigStrike = true;
    mDamageDuration = 1.f;
    mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(), x116C_, filter,
                    CVector3f::Zero());
    mCurDamageRemTime = mDamageWaitTime;
    x128C_ |= 0x04;
  } else if (player->GetMorphballTransitionState() != CPlayer::kMS_Morphed &&
             mCurDamageRemTime <= 0.f) {
    mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(), GetContactDamage(),
                    filter, CVector3f::Zero());
    mCurDamageRemTime = mDamageWaitTime;
  }
}

// @ fn_14_5274
void CDigitalGuardian::x5274_(CStateManager& mgr, TUniqueId sender) {
  CCollisionActor* collider = TCastToPtr< CCollisionActor >(mgr.ObjectById(sender));
  if (collider == nullptr) { // the TCastToPtr test at 0x52C0
    return;
  }
  x126C_ = 1.f; // .rodata 0x270
  const float hp = collider->GetHealthInfo()->GetHP();
  const float initial = collider->GetHealthInfo()->GetInitialHP();
  if (hp <= 0.f) { // .rodata 0x264
    collider->HealthInfo()->SetHP(initial);
    x266C_(mgr); // fn_14_266C: destroyed
  } else {
    x2E60_(mgr); // fn_14_2E60: hit
  }
  if (EchoEmitter() != nullptr) { // the test at 0x5348
    EchoEmitter()->SetAutoReducingDamage(x126C_);
  }
}

// @ fn_14_4350
int CDigitalGuardian::x4350_(TUniqueId id) const {
  if (x1078_[0] == id.value) {
    return 0;
  }
  if (x1078_[1] == id.value) {
    return 1;
  }
  if (x1078_[2] == id.value) {
    return 2;
  }
  if (x1078_[3] == id.value) {
    return 3;
  }
  return -1;
}

// fn_14_5140: a toe collider's damage (from fn_14_5384). The collider-null arm (0x5180) is dead
// here: fn_14_5384, the only caller, has already matched the sender against the toe ids.
// @ fn_14_5140
void CDigitalGuardian::x5140_(CStateManager& mgr, TUniqueId sender, float damage) {
  CCollisionActor* collider = TCastToPtr< CCollisionActor >(mgr.ObjectById(sender));
  if (collider == nullptr) { // the TCastToPtr test at 0x5180
    return;
  }
  const int toe = x4350_(sender); // fn_14_4350
  if (toe == -1 || x10DC_[toe] == 3) { // 3: already destroyed
    return;
  }
  x1144_[toe] -= damage;
  x1158_[toe] = mDamageWaitTime; // CPatterned 0x444
  if (x1144_[toe] <= 0.f) { // .rodata 0x264
    x1A68_(mgr, toe, 3); // fn_14_1A68: destroyed
    x143C_(mgr, collider->GetTranslation(), kSS_InternalState8); // fn_14_143C
  } else {
    PlayCustomSound(collider->GetTranslation(), CVector3f::Up(), mData.mSoundToeTargetHit, false);
    if (x10DC_[toe] == 1) {
      x1A68_(mgr, toe, 2); // fn_14_1A68: the flinch
    }
  }
}

// @ fn_14_5384
void CDigitalGuardian::x5384_(CStateManager& mgr, TUniqueId sender) {
  CCollisionActor* collider = TCastToPtr< CCollisionActor >(mgr.ObjectById(sender));
  if (collider == nullptr) { // the TCastToPtr test at 0x53C4
    return;
  }
  // The collider's damage is what it lost; its health goes back to full (0x53DC..0x53E8).
  CHealthInfo* health = collider->HealthInfo();
  const float initial = health->GetInitialHP();
  const float damage = initial - health->GetHP();
  health->SetHP(initial);
  if (x11B8_ == 1) {
    // In the vortex (0x11B8 == 1) the hit goes to the vortex's health.
    x11C8_ -= damage;
    mDamageCooldownTimer = skDamageHitTime; // CPatterned 0x448
    PlayCustomSound(collider->GetTranslation(), CVector3f::Up(), mData.mSoundVortexFlash, false);
  } else {
    x5140_(mgr, sender, damage); // fn_14_5140
  }
}

// @ fn_14_5470
void CDigitalGuardian::x5470_(CStateManager& mgr, TUniqueId sender, int knee) {
  CCollisionActor* collider = TCastToPtr< CCollisionActor >(mgr.ObjectById(sender));
  if (collider == nullptr) { // the TCastToPtr test at 0x54B0
    return;
  }
  u16* const armor[4] = {&xFB0_, &xFB2_, &xFB4_, &xFB6_};
  CActor* armour = static_cast< CActor* >(mgr.ObjectById(TUniqueId(*armor[knee])));
  if (armour == nullptr) { // the retail's test at 0x54D4
    return;
  }
  if (collider->GetHealthInfo()->GetHP() <= 0.f) {
    // The knee is gone (0x5508): KneeDestroyed reads 0x1284; the IS06/Follow waypoint goes to
    // the armour and IS07 is sent (fn_14_143C), the armour is deleted and its id cleared.
    x1284_ = knee;
    x143C_(mgr, armour->GetTranslation(), kSS_InternalState7); // fn_14_143C
    mgr.DeleteObjectRequest(armour->GetUniqueId());
    *armor[x1284_] = kInvalidUniqueId.value; // the sthx at 0x5558 (xFB0_ + 2 * 0x1284)
    CActor* counter = static_cast< CActor* >(mgr.ObjectById(x10CE_));
    if (counter != nullptr) { // the retail's test at 0x5568
      const float hp = counter->GetHealthInfo()->GetHP();
      counter->HealthInfo()->SetHP(hp - 1.f); // .rodata 0x270
    }
  } else {
    // The armour flashes ("XDMG" from the knee collider, 0x55AC) and the hit sound plays
    // (sound_KneeArmorHit, data +0x128) once at the collider.
    armour->AcceptScriptMsg(mgr, CScriptMsg(sender, armour->GetUniqueId(), kSM_Damage));
    PlayCustomSound(collider->GetTranslation(), CVector3f::Up(), mData.mSoundKneeArmorHit, false);
  }
}

// @ fn_14_3D2C
void CDigitalGuardian::x3D2C_(CStateManager& mgr, bool set) {
  // Bit 5 of 0x128C (the rlwimi at 0x3D40).
  if (set) {
    x128C_ |= 0x20;
    SendScriptMsgs(kSS_InternalState10, mgr, GetUniqueId(), kSM_None);
  } else {
    x128C_ &= ~0x20;
    SendScriptMsgs(kSS_InternalState11, mgr, GetUniqueId(), kSM_None);
  }
}

// @ fn_14_3F14
// The knee armours are deleted; 0x1284 is left at the last one's index (the store at 0x3F4C).
void CDigitalGuardian::x3F14_(CStateManager& mgr) {
  u16* const ids[4] = {&xFB0_, &xFB2_, &xFB4_, &xFB6_};
  for (int i = 0; i < 4; ++i) {
    if (*ids[i] == kInvalidUniqueId.value) {
      continue;
    }
    x1284_ = i;
    mgr.DeleteObjectRequest(TUniqueId(*ids[i]));
    *ids[i] = kInvalidUniqueId.value;
  }
}

// @ fn_14_5910
void CDigitalGuardian::x5910_(CStateManager& mgr) {
  x11C0_ = FindConnectedObject(mgr, kSS_InternalState0, kSM_Attach);
  x10CE_ = FindConnectedObject(mgr, kSS_InternalState4, kSM_Activate);
  x116A_ = CheckConnectedObject(mgr, kSS_InternalState5, kSM_Follow);
}

// fn_14_4080 (0x274 bytes): the four knee armours. Only with the knee model (the optional at
// 0xF0C, its flag at 0xF58, the test at 0x4098): for each knee locator (0x10C0..0x10C3) a new
// id, the locator's transform, and a CDigitalGuardianKneeArmor named "Knee Armor <i>" (.rodata
// 0x8EE) with the guardian as owner, armoured, the actor parameters (0xE00), kneeEchoParameters
// (data +0x15C) and sound_KneeVulnerable (data +0x140); its id goes to 0xFB0 + 2i (0x42C8).
// @ fn_14_4080
void CDigitalGuardian::x4080_(CStateManager& mgr) {
  if (!xF0C_.valid()) {
    return;
  }
  const uchar ids[4] = {x10C0_, x10C1_, x10C2_, x10C3_};
  for (int i = 0; i < 4; ++i) {
    const TUniqueId uid = mgr.AllocateUniqueId();
    const CTransform4f xf = GetLctrTransform(CSegId(static_cast< uint >(ids[i])));
    // The material list: kMT_Solid (.data 0x47C, 0x3B) and kMT_Orbit (.data 0x480, 0x29).
    const CMaterialList list(kMT_Solid, kMT_Orbit);
    // fn_14_1FE8 (the 0x42AC call) is this info's destructor.
    const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, GetActive(),
                           kInvalidEditorId);
    const rstl::string index = CStringExtras::CreateFromInteger(i);
    const rstl::string name = rstl::string_l("Knee Armor ") + index; // fn_14_42F4
    const SEchoParameters echo = LdrToEchoParameters(mData.mKneeEchoParameters);
    CDigitalGuardianKneeArmor* knee = rs_new CDigitalGuardianKneeArmor(
        uid, name, info, 0, xf, xF0C_.data(), list, GetUniqueId(), false, xE00_, echo,
        mData.mSoundKneeVulnerable);
    if (knee != nullptr) { // the retail's test at 0x42B0
      mgr.AddObject(*knee);
      // The sth at 0x42C8 goes through r28 = this+0xFB0 (0x40AC), +2 per knee (0x42D0): xFB0_ + 2i.
      switch (i) {
      case 0: xFB0_ = uid.value; break;
      case 1: xFB2_ = uid.value; break;
      case 2: xFB4_ = uid.value; break;
      default: xFB6_ = uid.value; break;
      }
    }
  }
}

// fn_14_24E4 (0x94 bytes): each toe target gets the full toeTargetHP (data +0x20, the lfs at
// 0x2520), no state (-1) and a zero timer (.rodata 0x264), then state 1 (fn_14_1A68, 0x2540).
// @ fn_14_24E4
void CDigitalGuardian::x24E4_(CStateManager& mgr) {
  for (int i = 0; i < 4; ++i) {
    x1144_[i] = mData.mToeTargetHP;
    x10DC_[i] = -1;
    x1158_[i] = 0.f;
    x1A68_(mgr, i, 1);
  }
}

// fn_14_1A68 (0x580 bytes): a toe target's state. Nothing for a leg outside 0..3, the state it
// already has, or a destroyed target (3). Otherwise the old sound stops (0x1ACC), and at the
// ankle locator (0x10D4 + leg; its transform is the locator's times the segment's rest
// rotation, 0x1B00..0x1B20) the three effects are set from the .rodata tables 0x30
// ("ToeTargetXX"), 0x50 ("ToeFlinchXX") and 0x40 ("SWBuildUpXX"), in that order, and a sound
// starts.
// @ fn_14_1A68
void CDigitalGuardian::x1A68_(CStateManager& mgr, int leg, int state) {
  static const char* const kToeTarget[4] = {"ToeTargetFL", "ToeTargetBL", "ToeTargetFR",
                                            "ToeTargetBR"}; // .rodata 0x30
  static const char* const kSWBuildUp[4] = {"SWBuildUpFL", "SWBuildUpBL", "SWBuildUpFR",
                                            "SWBuildUpBR"}; // .rodata 0x40
  static const char* const kToeFlinch[4] = {"ToeFlinchFL", "ToeFlinchBL", "ToeFlinchFR",
                                            "ToeFlinchBR"}; // .rodata 0x50
  if (leg < 0 || leg > 3) {
    return;
  }
  if (state == x10DC_[leg] || x10DC_[leg] == 3) {
    return;
  }
  x10DC_[leg] = state;
  if (x10F0_[leg] != CSfxHandle()) {
    CSfxManager::RemoveEmitter(x10F0_[leg]);
    x10F0_[leg] = CSfxHandle();
  }

  CAnimData* const anim = AnimationData();
  const uchar ids[4] = {x10D4_, x10D5_, x10D6_, x10D7_};
  const CSegId seg(static_cast< uint >(ids[leg]));
  const CTransform4f rot = SegmentRotation_(seg);
  const CTransform4f xf = GetLctrTransform(seg) * rot;

  switch (state) {
  case 1: // 0x1B58: the target shows; sound_ToeTarget (data +0x2C), looped
    anim->SetEffectState(rstl::string_l(kToeTarget[leg]), true, mgr);
    anim->SetEffectState(rstl::string_l(kToeFlinch[leg]), false, mgr);
    anim->SetEffectState(rstl::string_l(kSWBuildUp[leg]), false, mgr);
    x10F0_[leg] = PlayCustomSound(xf.GetTranslation(), CVector3f::Up(), mData.mSoundToeTarget, true);
    break;
  case 0: // 0x1C30: the attack build-up; sound_ToeTargetAttack (data +0x44), looped
    anim->SetEffectState(rstl::string_l(kToeTarget[leg]), false, mgr);
    anim->SetEffectState(rstl::string_l(kToeFlinch[leg]), false, mgr);
    anim->SetEffectState(rstl::string_l(kSWBuildUp[leg]), true, mgr);
    x10F0_[leg] =
        PlayCustomSound(xf.GetTranslation(), CVector3f::Up(), mData.mSoundToeTargetAttack, true);
    break;
  case 2: // 0x1D08: the flinch; sound_ToeTarget (data +0x2C), looped
    anim->SetEffectState(rstl::string_l(kToeTarget[leg]), false, mgr);
    anim->SetEffectState(rstl::string_l(kToeFlinch[leg]), true, mgr);
    anim->SetEffectState(rstl::string_l(kSWBuildUp[leg]), false, mgr);
    x10F0_[leg] = PlayCustomSound(xf.GetTranslation(), CVector3f::Up(), mData.mSoundToeTarget, true);
    break;
  case 3: { // 0x1DE0: destroyed; the explosion, then sound_ToeTargetExplosion (data +0x5C) once
    anim->SetEffectState(rstl::string_l(kToeTarget[leg]), false, mgr);
    anim->SetEffectState(rstl::string_l(kToeFlinch[leg]), false, mgr);
    anim->SetEffectState(rstl::string_l(kSWBuildUp[leg]), false, mgr);
    if (x1100_.valid()) {
      // "Toe Target Explosion Fx" (.rodata 0x8B4) at the ankle, the model's scale, white.
      const CVector3f scale = GetModelData()->GetScale();
      const rstl::string name = rstl::string_l("Toe Target Explosion Fx");
      // fn_14_1FE8 (the 0x1F74 call) is this info's destructor.
      const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, true,
                             kInvalidEditorId);
      const TUniqueId uid = mgr.AllocateUniqueId();
      CExplosion* explosion = rs_new CExplosion(x1100_.data(), uid, info, name, xf, 0, scale,
                                                CColor::White(), -1);
      if (explosion != nullptr) { // the retail's test at 0x1F88
        mgr.AddObject(explosion);
      }
    }
    PlayCustomSound(xf.GetTranslation(), CVector3f::Up(), mData.mSoundToeTargetExplosion, false);
    break;
  }
  default:
    break;
  }
}

// @ fn_14_266C
void CDigitalGuardian::x266C_(CStateManager& mgr) {
  if (x1268_ == 0) {
    return;
  }
  x1268_ = 3;
  const CTransform4f xf = CTransform4f::Translate(GetAimPosition(mgr, 0.f)); // vtable 0x54
  if (x1244_.valid()) {
    // "Beacon Explosion Fx" (.rodata 0x8CC) at the aim position, the model's scale, white.
    const CVector3f scale = GetModelData()->GetScale();
    const rstl::string name = rstl::string_l("Beacon Explosion Fx");
    const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, true,
                           kInvalidEditorId);
    const TUniqueId uid = mgr.AllocateUniqueId();
    CExplosion* explosion = rs_new CExplosion(x1244_.data(), uid, info, name, xf, 0, scale,
                                              CColor::White(), -1);
    if (explosion != nullptr) { // the retail's test at 0x27E4
      mgr.AddObject(explosion);
    }
  }
  PlayCustomSound(GetTranslation(), CVector3f::Up(), mData.mSoundBeaconExplode, false);
  CCollisionActor* head = TCastToPtr< CCollisionActor >(mgr.ObjectById(x10CC_));
  if (head != nullptr && head->GetActive()) { // the tests at 0x2830 and 0x283C
    head->SetActive(false);
  }
  if (x123C_ != nullptr) {
    x123C_->SetParticleEmission(false); // vtable 0x2C
  }
  if (x1240_ != CSfxHandle()) {
    CSfxManager::RemoveEmitter(x1240_);
    x1240_ = CSfxHandle();
  }
  SetVisorOrbitableFlags(CVisorParameters::kVOF_All, false);
  SetVisorOrbitableFlags(CVisorParameters::kVOF_Echo, true);
}

// @ fn_14_2E60
void CDigitalGuardian::x2E60_(CStateManager& mgr) {
  if ((x128C_ & 0x08) != 0) { // the extrwi at 0x2E7C
    return;
  }
  const CCollisionActor* head = TCastToConstPtr< CCollisionActor >(mgr.GetObjectById(x10CC_));
  if (head == nullptr || !head->GetActive()) { // the tests at 0x2E9C and 0x2EA8
    return;
  }
  // Above the head collider by 4 (.rodata 0x2A8) times the model's z scale.
  const CVector3f scale = GetModelData()->GetScale();
  const CVector3f pos = head->GetTranslation() + CVector3f::Up() * (4.f * scale.GetZ());
  // "Beacon Hit Fx" (.rodata 0x8E0); the retail does not test the token (0x1260) first.
  const rstl::string name = rstl::string_l("Beacon Hit Fx");
  const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, true,
                         kInvalidEditorId);
  const TUniqueId uid = mgr.AllocateUniqueId();
  CExplosion* explosion = rs_new CExplosion(x1254_.data(), uid, info, name,
                                            CTransform4f::Translate(pos), 0, scale,
                                            CColor::White(), -1);
  if (explosion != nullptr) { // the retail's test at 0x3038
    mgr.AddObject(explosion);
  }
  if (x1254_.valid()) { // the present byte at 0x304C
    PlayCustomSound(pos, CVector3f::Up(), mData.mSoundBeaconHit, false);
  }
}

// fn_14_14F8 (0x74 bytes): CPatterned's speed (guest 0x3C8) for the number of knee armours left
// (fn_14_705C, 0x150C): 4 -> data +0x17C, 3 -> +0x180, 2 -> +0x184, otherwise +0x188.
// @ fn_14_14F8
void CDigitalGuardian::x14F8_() {
  switch (x705C_()) {
  case 4:
    mSpeed = mData.mUnknown8106CDA9;
    break;
  case 3:
    mSpeed = mData.mUnknown9E1B8105;
    break;
  case 2:
    mSpeed = mData.mUnknownA08FCC70;
    break;
  default:
    mSpeed = mData.mUnknown3254A16B;
    break;
  }
}

// fn_14_705C: how many of the four knee ids (guest this+0xFB0, 0xFB2, 0xFB4 and 0xFB6) are not
// kInvalidUniqueId (the four lhz/cmplw pairs at 0x7060..0x709C).
// Every arm ran (tools/cov/specs/digitalguardian_arms.con).
// @ fn_14_705C
int CDigitalGuardian::x705C_() const {
  int count = 0;
  if (xFB0_ != kInvalidUniqueId.value) {
    ++count;
  }
  if (xFB2_ != kInvalidUniqueId.value) {
    ++count;
  }
  if (xFB4_ != kInvalidUniqueId.value) {
    ++count;
  }
  if (xFB6_ != kInvalidUniqueId.value) {
    ++count;
  }
  return count;
}

// The 14 states of lbl_14_data_204 (.data 0x204; fn_14_D904's stores at 0xDCD0..0xDEB8).

// fn_14_9634: the body is empty (the single blr at 0x9634), like CPatterned::Start.
// @ fn_14_9634
void CDigitalGuardian::Start(CStateManager& mgr, EStateMsg msg, float dt) {}

// fn_14_95B8: the state message picks the work (the cmpwi chain at 0x95C0..0x95E8): on Activate
// (0) the body controller's locomotion type becomes 1 (pas::kLT_Relaxed, the call at 0x95F8)
// and bit 7 (the mask 0x80) of the flag byte at guest this+0x128C is set (the rlwimi at 0x9604);
// on Deactivate (2) that bit is cleared (the rlwimi at 0x9618); Update (1) and every other
// message do nothing. gcov (the helpers lane, area 53): 3/3 branches, ALL-ARMS.
// @ fn_14_95B8
void CDigitalGuardian::Idle(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    BodyController()->SetLocomotionType(pas::kLT_Relaxed);
    x128C_ |= 0x80;
    break;
  case kStateMsg_Deactivate:
    x128C_ &= ~0x80;
    break;
  default:
    break;
  }
}

// fn_14_94BC: the state message picks the work (the cmpwi chain at 0x94C4..0x94EC).
// Activate (0): ready (0x6B4 = 1, 0x94F8) and 0x11E8 = 1 (0x94FC). Update (1): when a turn can be
// issued (CanIssueCommand with 8 = kAS_Turn, 0x9510) the body turns to the face direction 0x1278
// - a locomotion command with no move (sZeroVector, 0x9540), 0x1278 normalised (0x9534) as the
// face and weight 1.0f (.rodata 0x270, 0x9560), DeliverCmd at 0x9588 - when 0x1278's magnitude
// is safe (0x9520); otherwise the state is over (0x6B4 = 3, 0x9594). Deactivate (2): not ready
// (0x6B4 = 0, 0x95A0). Other messages do nothing.
// gcov (tools/cov/specs/digitalguardian_arms.con, one run): 8/8 branches, ALL-ARMS.
// @ fn_14_94BC
void CDigitalGuardian::FaceTarget(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    x11E8_ = 1;
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Turn)) {
      if (x1278_.IsMagnitudeSafe()) {
        BodyController()->CommandMgr().DeliverCmd(
            CBCLocomotionCmd(CVector3f::Zero(), x1278_.AsNormalized(), 1.f));
      } else {
        mAnimationState.SetState(CAnimationState::kAS_Over);
      }
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  default:
    break;
  }
}

// fn_14_9260: the state message picks the work (the cmpwi chain at 0x9280..0x92A4).
// Activate (0): ready (0x92B4), vortex mode 1 (0x11B8, 0x92C4), the drift velocity 0x11CC zeroed
// (0x92CC..0x92DC), bits 7 and 6 of 0x128D cleared (0x92E4, 0x92F0), the vortex health 0x11C8
// reset from the data's +0x100 (guest 0x8C0, 0x92FC), locomotion 3 (kLT_Combat, 0x9304), every
// visor orbitable flag on (0xF, 0x9314), 0x11E8 cleared (0x9320), the camera shake started
// (fn_14_4D54), the toe colliders given the vortex vulnerability (0x1188, fn_14_39CC), the root
// collider activated (fn_14_3974) and the speed reset to the base speed (0x1288, 0x9350).
// Update (1): while the state's time (the machine's vtable slot 0x30 at 0x9368) is below the
// data's vortexAttackDuration (guest 0x894, the fcmpo at 0x9370): with bit 7 of 0x128D (the mask
// 0x80, the extrwi. at 0x937C) and dt above 0.0f (.rodata 0x264, 0x938C) the body drifts by
// 0x11CC * dt in its own frame (0x93C4, 0x93D4); the speed is the base speed plus the time
// fraction (0x9408), the sound pitch bend 0x2000 plus the fraction times the data's int at +0xFC
// (guest 0x8BC, converted at 0x9414..0x9420; __cvt_fp2unsigned at 0x9428); then the pull
// (fn_14_3674) and the camera shake follows the body (fn_14_4878). At the duration: over (0x9460).
// Deactivate (2): the shake removed (fn_14_4838), the toe colliders given the toe target
// vulnerability (0x1110, fn_14_39CC), the root collider deactivated (fn_14_3974), not ready
// (0x9494) and stopped (0x9498). Other messages do nothing.
// @ fn_14_9260
void CDigitalGuardian::VortexAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    x11B8_ = 1;
    x11CC_ = CVector3f::Zero();
    x128D_ &= ~0x80;
    x128D_ &= ~0x40;
    x11C8_ = mData.mUnknown4F5D725C;
    BodyController()->SetLocomotionType(pas::kLT_Combat);
    SetVisorOrbitableFlags(CVisorParameters::kVOF_All, true);
    x11E8_ = 0;
    x4D54_(mgr);         // fn_14_4D54
    x39CC_(mgr, x1188_); // fn_14_39CC
    x3974_(mgr, true);   // fn_14_3974
    mSpeed = x1288_;
    break;
  case kStateMsg_Update:
    if (mStateMachine->GetTime() < mData.mVortexAttackDuration) {
      if ((x128D_ & 0x80) != 0 && dt > 0.f) { // extrwi. at 0x937C, fcmpo at 0x938C
        MoveInOneFrameOR(GetTransform().TransposeRotate(x11CC_ * dt), dt);
      }
      const float frac = mStateMachine->GetTime() / mData.mVortexAttackDuration;
      mSpeed = x1288_ + frac;
      SetSoundEventPitchBend(
          static_cast< int >(static_cast< uint >(
              frac * static_cast< float >(mData.mUnknown6AAF33E3))) + 0x2000);
      x3674_(mgr, dt); // fn_14_3674
      x4878_(mgr);     // fn_14_4878
    } else {
      mAnimationState.SetState(CAnimationState::kAS_Over);
    }
    break;
  case kStateMsg_Deactivate:
    x4838_(mgr);         // fn_14_4838
    x39CC_(mgr, x1110_); // fn_14_39CC
    x3974_(mgr, false);  // fn_14_3974
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    Stop();
    break;
  default:
    break;
  }
}

// fn_14_9164: the state message picks the work (the cmpwi chain at 0x916C..0x9194).
// Activate (0): ready (0x91A0). Update (1): the speed falls by 2/3 (.rodata 0x2D8) of dt (the
// fnmsubs at 0x91B8), no lower than the base speed 0x1288 (the fcmpo at 0x91BC); the sound pitch
// bend is 0x2000 plus (speed - base) / base times the data's int at +0xFC (guest 0x8BC, 0x91E0..
// 0x9218); at or below the base speed (the fcmpo/cror at 0x9224..0x9228), over (0x9234).
// Deactivate (2): not ready (0x9240) and the base speed back (0x9248). Other messages do nothing.
// @ fn_14_9164
void CDigitalGuardian::VortexEndAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    break;
  case kStateMsg_Update: {
    const float speed = mSpeed - 0.6666667f * dt;
    mSpeed = x1288_ < speed ? speed : x1288_; // 0x91BC fcmpo/bge to the fmr of x1288 (NaN too)
    SetSoundEventPitchBend(
        static_cast< int >(static_cast< uint >(
            (mSpeed - x1288_) / x1288_ * static_cast< float >(mData.mUnknown6AAF33E3))) +
        0x2000);
    if (mSpeed <= x1288_) {
      mAnimationState.SetState(CAnimationState::kAS_Over);
    }
    break;
  }
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    mSpeed = x1288_;
    break;
  default:
    break;
  }
}

// fn_14_9070: the state message picks the work (the cmpwi chain at 0x908C..0x90AC).
// Activate (0): ready (0x90BC), vortex mode 2 (0x11B8, 0x90CC), the exit speed 0x11BC zeroed
// (.rodata 0x264, 0x90D0), locomotion 2 (kLT_Lurk, 0x90D8), 0x11E8 set (0x90E4), the sound pitch
// bend 0x2000 plus the data's int at +0xF8 (guest 0x8B8, 0x90F0) and the base speed (0x1288,
// 0x90F8). Update (1): a step towards the exit target with no minimum distance (fn_14_34E8 with
// 0.0f, 0x9108); when that is done (0x9110) and the animation's time left is at most dt (the
// fcmpo/cror at 0x911C..0x9120), over (0x912C). Deactivate (2): not ready (0x9138), stopped
// (0x913C) and the pitch bend cleared (0x9144). Other messages do nothing.
// @ fn_14_9070
void CDigitalGuardian::VortexRetreat(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    x11B8_ = 2;
    x11BC_ = 0.f;
    BodyController()->SetLocomotionType(pas::kLT_Lurk);
    x11E8_ = 1;
    SetSoundEventPitchBend(mData.mUnknownFB5263E8 + 0x2000);
    mSpeed = x1288_;
    break;
  case kStateMsg_Update:
    if (!x34E8_(mgr, dt, 0.f) && BodyController()->GetAnimTimeRemaining() <= dt) {
      mAnimationState.SetState(CAnimationState::kAS_Over);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    Stop();
    ClearSoundEventPitchBend();
    break;
  default:
    break;
  }
}

// fn_14_8E44: the state message picks the work (the cmpwi chain at 0x8E4C..0x8E7C).
// Activate (0): ready (0x8E8C), vortex mode 0 (0x11B8, 0x8E98), 0x11E8 set (0x8E9C), every visor
// orbitable flag off (0xF, 0x8EA0) and then the echo one (2) on (0x8EB0). Update (1): when a
// generate can be issued (CanIssueCommand with 12 = kAS_Generate, 0x8EC4), a generate command
// of type 1 (kGType_One, the li at 0x8F04), the zero target and no animation (-1, 0x8F0C) is
// delivered (0x8F4C); otherwise, once the body state is the generate (0x37C == 12, 0x8FAC),
// locomotion 1 (kLT_Relaxed, 0x8FB8). Deactivate (2): not ready (0x8FC8), locomotion 1 (0x8FD0),
// bit 0 of 0x128C cleared (0x8FDC), the speed for the knees left (fn_14_14F8) and the knee
// armours orbitable (fn_14_3DA8 with 1, 0x8FF8). Other messages do nothing.
// @ fn_14_8E44
void CDigitalGuardian::ExitVortexAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    x11B8_ = 0;
    x11E8_ = 1;
    SetVisorOrbitableFlags(CVisorParameters::kVOF_All, false);
    SetVisorOrbitableFlags(CVisorParameters::kVOF_Echo, true);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Generate)) {
      BodyController()->CommandMgr().DeliverCmd(CBCGenerateCmd(pas::kGType_One, -1));
    } else if (BodyController()->GetCurrentStateId() == pas::kAS_Generate) {
      BodyController()->SetLocomotionType(pas::kLT_Relaxed);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    BodyController()->SetLocomotionType(pas::kLT_Relaxed);
    x128C_ &= ~0x01;
    x14F8_();              // fn_14_14F8
    x3DA8_(mgr, true);     // fn_14_3DA8
    break;
  default:
    break;
  }
}

// @ fn_14_8CCC
// fn_14_8CCC: the state message picks the work (the cmpwi chain at 0x8CD4..0x8CFC).
// Activate (0): with a melee leg (0x11D8 != -1, 0x8D08) ready (0x8D18) and the leg's toe target
// to state 0 (fn_14_1A68 with r6 = 0, 0x8D20), else over (3, 0x8D2C); then 0x11E8 = 1 (0x8D34).
// Update (1): when CTryCommand allows kAS_MeleeAttack (li r5, 7 and the call at 0x8D48) a
// CBCMeleeAttackCmd (kBSC_MeleeAttack, li r4, 5 at 0x8D98) with the leg's severity (.rodata
// 0x21C = {0, 1, 2, 3}, the lwzx at 0x8D74) goes out (DeliverCmd at 0x8DC0); otherwise over
// (3, 0x8E14), whatever CanIssueCommand left. Deactivate (2): not ready (0x8E24) and the toe
// target to state 1 (r6 = 1, fn_14_1A68 at 0x8E2C).
void CDigitalGuardian::MeleeAttackTelegraph(CStateManager& mgr, EStateMsg msg, float dt) {
  static const pas::ESeverity kTelegraphSeverity[4] = {pas::kS_Zero, pas::kS_One, pas::kS_Two,
                                                       pas::kS_Three}; // .rodata 0x21C
  switch (msg) {
  case kStateMsg_Activate:
    if (x11D8_ != -1) {
      mAnimationState.SetState(CAnimationState::kAS_Ready);
      x1A68_(mgr, x11D8_, 0); // fn_14_1A68
    } else {
      mAnimationState.SetState(CAnimationState::kAS_Over);
    }
    x11E8_ = 1;
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_MeleeAttack)) {
      BodyController()->CommandMgr().DeliverCmd(CBCMeleeAttackCmd(kTelegraphSeverity[x11D8_]));
    } else {
      mAnimationState.SetState(CAnimationState::kAS_Over);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    x1A68_(mgr, x11D8_, 1); // fn_14_1A68
    break;
  default:
    break;
  }
}

// @ fn_14_89D4
// fn_14_89D4: the animation the state started on is a function static (lbl_14_bss_4, -1 on the
// first call: the guard byte lbl_14_bss_8 at 0x89F0..0x8A0C). The state message picks the work
// (the cmpwi chain at 0x8A10..0x8A2C).
// Activate (0): with a melee leg (0x11D8 != -1, 0x8A38) and a range (0x11DC != -1, 0x8A44) ready
// (0x8A54), the body controller's current animation (0x58C) kept (0x8A6C) and the toe target to
// state 0 (fn_14_1A68 with r6 = 0, 0x8A74); else over (3, 0x8A80). Then 0x11E8 = 0 (0x8A88).
// Update (1): while the animation is still the kept one (the cmpw at 0x8AA4) a CBCMeleeAttackCmd
// (li r4, 5 at 0x8B00 / 0x8BB8) goes out every update with the severity of .rodata 0x22C
// ({4, 5}, {6, 7}, {8, 9}, {10, 11}; 8 bytes a leg, slwi 3 at 0x8AD4 / 0x8B90): the far one
// (+4, the lwz 0x4(r5) at 0x8AE0) when the range is 1 (0x8AB0), else the near one (the lwzx at
// 0x8B9C). Once the animation changed, over (3, 0x8C3C) unless the body state is still
// kAS_MeleeAttack (0x37C == 7, 0x8C30). Deactivate (2): not ready (0x8C4C) and the toe target to
// state 1 (r6 = 1, fn_14_1A68 at 0x8C58).
void CDigitalGuardian::MeleeAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  static const pas::ESeverity kAttackSeverity[4][2] = {
      {pas::kS_Four, pas::kS_Five},
      {pas::kS_Six, pas::kS_Seven},
      {pas::kS_Eight, pas::kS_Nine},
      {pas::kS_Ten, pas::kS_Eleven},
  }; // .rodata 0x22C
  static int sAttackAnim = -1; // lbl_14_bss_4
  switch (msg) {
  case kStateMsg_Activate:
    if (x11D8_ != -1 && x11DC_ != -1) {
      mAnimationState.SetState(CAnimationState::kAS_Ready);
      sAttackAnim = GetBodyController()->GetCurrentAnimId();
      x1A68_(mgr, x11D8_, 0); // fn_14_1A68
    } else {
      mAnimationState.SetState(CAnimationState::kAS_Over);
    }
    x11E8_ = 0;
    break;
  case kStateMsg_Update:
    if (sAttackAnim == GetBodyController()->GetCurrentAnimId()) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCMeleeAttackCmd(kAttackSeverity[x11D8_][x11DC_ == 1 ? 1 : 0]));
    } else if (GetBodyController()->GetCurrentStateId() != pas::kAS_MeleeAttack) {
      mAnimationState.SetState(CAnimationState::kAS_Over);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    x1A68_(mgr, x11D8_, 1); // fn_14_1A68
    break;
  default:
    break;
  }
}

// fn_14_8794: the state message picks the work (the cmpwi chain at 0x879C..0x87CC).
// Activate (0): ready (0x6B4 = 1, 0x87D8) and 0x11E8 = 1 (0x87DC). Update (1): when a loop attack
// can be issued (CanIssueCommand with 9 = kAS_LoopAttack, 0x87F0) it is sent (DeliverCmd at
// 0x8878, kBSC_LoopAttack = 7): type 1 when the head (0x1082) is there and its projectile attack
// (head+0x10D0) is 2, the missiles (the cmpwi at 0x8824), else 0 (0x8808); no wait (0x8870),
// skip-into clear (the rlwimi at 0x8848). Otherwise, while the body is in the loop attack (body
// state 0x37C == 9, 0x88C0), it exits the state (kBSC_ExitState = 10, 0x88F8, DeliverCmd at
// 0x8914) unless the head is there with its flag 0x20 (head+0x10F4, the extrwi. at 0x88EC).
// Deactivate (2): not ready (0x6B4 = 0, 0x8930) and the exit command (0x8938, 0x8950).
// gcov (tools/cov/specs/digitalguardian_arms.con, one run): 16/16 branches, ALL-ARMS.
// @ fn_14_8794
void CDigitalGuardian::ProjectileAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    x11E8_ = 1;
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_LoopAttack)) {
      pas::ELoopAttackType type = pas::kLAT_Zero;
      const CDigitalGuardianHead* head =
          TCastToConstPtr< CDigitalGuardianHead >(mgr.GetObjectById(x1082_));
      if (head != nullptr && head->x10D0_ == 2) {
        type = pas::kLAT_One;
      }
      BodyController()->CommandMgr().DeliverCmd(CBCLoopAttackCmd(type, false));
    } else if (BodyController()->GetCurrentStateId() == pas::kAS_LoopAttack) {
      const CDigitalGuardianHead* head =
          TCastToConstPtr< CDigitalGuardianHead >(mgr.GetObjectById(x1082_));
      if (head == nullptr || (head->x10F4_ & 0x20) == 0) {
        BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_ExitState));
      }
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_ExitState));
    break;
  default:
    break;
  }
}

// @ fn_14_8580
// fn_14_8580: the state message picks the work (the cmpwi chain at 0x8588..0x85BC).
// Activate (0): ready (0x85CC), 0x11E8 = 0 (0x85D4), every toe target to state 0 (fn_14_1A68
// with r6 = 0, the loop to 4 at 0x85D8..0x85F4) and the speed back to the base speed (0x1288 ->
// 0x3C8, 0x85FC). Update (1): when CTryCommand allows kAS_Step (li r5, 3 and the call at
// 0x8610) a CBCStepCmd (kBSC_Step, li r4, 1 at 0x8658) forward (0, 0x8664) as a break dodge
// (2, 0x8668) goes out (DeliverCmd at 0x867C); nothing otherwise. Deactivate (2): not ready
// (0x86DC), bit 5 of 0x128C off (fn_14_3D2C with 0, 0x86E0), the knees' vulnerabilities
// (fn_14_3A44, 0x86EC), every toe target to state 1 (0x86F4..0x8710) and the speed for the
// knee armours left (fn_14_14F8, 0x8718).
void CDigitalGuardian::JumpAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    x11E8_ = 0;
    for (int i = 0; i < 4; ++i) {
      x1A68_(mgr, i, 0); // fn_14_1A68
    }
    mSpeed = x1288_;
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Step)) {
      BodyController()->CommandMgr().DeliverCmd(
          CBCStepCmd(pas::kSD_Forward, pas::kStep_BreakDodge));
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    x3D2C_(mgr, false); // fn_14_3D2C
    x3A44_(mgr);        // fn_14_3A44
    for (int i = 0; i < 4; ++i) {
      x1A68_(mgr, i, 1); // fn_14_1A68
    }
    x14F8_(); // fn_14_14F8
    break;
  default:
    break;
  }
}

// @ fn_14_8090
void CDigitalGuardian::KneeDestroyedReaction(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    // Ready when a knee was destroyed (0x1284), else already over (0x80D0..0x80EC).
    mAnimationState.SetState(x1284_ != -1 ? CAnimationState::kAS_Ready
                                          : CAnimationState::kAS_Over);
    x3D2C_(mgr, true); // fn_14_3D2C
    x3A44_(mgr);       // fn_14_3A44
    x24E4_(mgr);       // fn_14_24E4
    x11E8_ = 0;
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_KnockBack)) {
      // The knee's angle (.rodata 0x20C: 135, 225, 45, 315) picks the knock-back (fn_14_83C0
      // builds the parm data at 0x81D4); it is sent only when an animation scores above 0.
      static const float kKneeAngles[4] = {135.f, 225.f, 45.f, 315.f};
      const rstl::pair< float, int > best =
          BodyController()->GetPASDatabase().FindBestAnimation(
              CPASAnimParmData(pas::kAS_KnockBack, CPASAnimParm::FromReal32(kKneeAngles[x1284_]),
                               CPASAnimParm::FromEnum(2)),
              -1);
      if (best.first > 0.f) { // .rodata 0x264, fcmpo/ble at 0x81FC
        // The direction is the transform's forward column (0x28/0x38/0x48 at 0x8208..0x8214),
        // severity 2 (0x8224), the best animation (0x8258), no force restart (0x825C);
        // DeliverCmd at 0x8260.
        BodyController()->CommandMgr().DeliverCmd(
            CBCKnockBackCmd(GetTransform().GetForward(), pas::kS_Two, best.second, false));
      }
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    x14F8_(); // fn_14_14F8
    x1284_ = -1;
    SendScriptMsgs(kSS_InternalState9, mgr, GetUniqueId(), kSM_None);
    x3D2C_(mgr, false); // fn_14_3D2C
    x3A44_(mgr);        // fn_14_3A44
    break;
  default:
    break;
  }
}

// fn_14_7D8C (0x260 bytes): the guardian falls. On Activate, after the move to the
// DeathRattle/Follow waypoint (0x7E6C..0x7EA8), the head (the TCastToPtr<CDigitalGuardianHead>
// of 0x1082 at 0x7EC0; none: done, 0x7EC8) is put at the "Head_1" locator (0x10B8,
// GetLctrTransform at 0x7ED8, SetTransform at 0x7EF0) and bit 1 of 0x128C (the mask 0x02, the
// rlwimi r0, r3(0), 1, 30, 30 at 0x7EFC) is cleared.
// @ fn_14_7D8C
void CDigitalGuardian::CrippledReaction(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate: {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    x3D2C_(mgr, true); // fn_14_3D2C
    x3A44_(mgr);       // fn_14_3A44
    x11E8_ = 0;
    x128C_ |= 0x40; // the rlwimi at 0x7E00
    for (int i = 0; i < 4; ++i) {
      x1A68_(mgr, i, 3); // fn_14_1A68: every toe target destroyed
    }
    SendScriptMsgs(kSS_DeathRattle, mgr, GetUniqueId(), kSM_None);
    // The guardian moves to the DeathRattle/Follow waypoint.
    const TUniqueId id = CheckConnectedObject(mgr, kSS_DeathRattle, kSM_Follow);
    const CScriptWaypoint* waypoint = TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(id));
    if (waypoint == nullptr) { // the TCastToPtr test at 0x7E8C
      break;
    }
    SetTransform(waypoint->GetTransform());
    CDigitalGuardianHead* head = TCastToPtr< CDigitalGuardianHead >(mgr.ObjectById(x1082_));
    if (head == nullptr) { // 0x7EC8
      break;
    }
    head->SetTransform(GetLctrTransform(CSegId(x10B8_)));
    x128C_ &= ~0x02; // 0x7EFC
    break;
  }
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Fall)) {
      // The hit direction is the transform's column 1 (asm 0x7F24-0x7F30: lfs +0x28/+0x38/+0x48), not the position.
      BodyController()->CommandMgr().DeliverCmd(
          CBCKnockDownCmd(GetTransform().GetForward(), pas::kS_Two, false));
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  default:
    break;
  }
}

// @ fn_14_7CB4
// The Crippled state: on Activate the second manager (0xE64) wakes and every collider of the
// first (0xE60) but the guard's (0x10CC) sleeps; on Update the beacon's update runs.
void CDigitalGuardian::Crippled(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    xE64_->SetActive(mgr, true); // 0x7CF4, no null test
    for (uint i = 0; i < xE60_->GetNumCollisionActors(); ++i) {
      const TUniqueId id = xE60_->GetCollisionDescFromIndex(i).GetCollisionActorId();
      CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id));
      if (actor != nullptr && id != x10CC_) { // the tests at 0x7D28 and 0x7D38
        actor->SetActive(false);
      }
    }
    break;
  case kStateMsg_Update:
    x2CC4_(mgr, dt);
    break;
  default:
    break;
  }
}

// The 13 codes of lbl_14_data_380 (.data 0x380; fn_14_D904's stores at 0xDEBC..0xE00C).

// fn_14_7BD4: when the IS04/Activate object (0x10CE, ObjectById at 0x7C0C) is there (0x7C14),
// the boss bar shows its initial health (GetHealthInfo, vtable +0x3C at 0x7C24, the float at +0
// read at 0x7C30) under the name "BossCliffsideBoss" (.rodata 0x2F4 + 0x614 = 0x908,
// GetStringIndexByName at 0x7C50; the host's CStringTable has GetStringIndex) for that object's
// id (SetBossParams at 0x7C64). Then IS09 (0x49533039, 0x7C6C/0x7C7C) is sent with no message
// (-1, 0x7C84; SendScriptMsgs at 0x7C8C).
// gcov (tools/cov/specs/digitalguardian_arms.con, one run): 2/2 branches, ALL-ARMS.
// @ fn_14_7BD4
void CDigitalGuardian::SetupBossEnergyBar(CStateManager& mgr, float dt) {
  if (CEntity* counter = mgr.ObjectById(x10CE_)) {
    const CActor* actor = static_cast< const CActor* >(counter);
    const float health = actor->GetHealthInfo()->GetInitialHP();
    mgr.SetBossParams(counter->GetUniqueId(), health,
                      gpStringTable->GetStringIndex("BossCliffsideBoss"));
  }
  SendScriptMsgs(kSS_InternalState9, mgr, GetUniqueId(), kSM_None);
}

// fn_14_7B90: the number of knees that are set (fn_14_705C, the call at 0x7BA4) picks the
// float that is copied into the member at guest this+0x1274 (the store at 0x7BBC): the data
// block's +0x174 (mUnknown6A754EBD) when all four knees are set (the cmpwi with 4 at 0x7BA8),
// otherwise the block's +0x170 (mUnknownA324E26C) (the two loads at 0x7BB0/0x7BB8).
// Every arm ran (tools/cov/specs/digitalguardian_arms.con).
// @ fn_14_7B90
void CDigitalGuardian::ResetProtectKneesTime(CStateManager& mgr, float dt) {
  x1274_ = x705C_() >= 4 ? mData.mUnknown6A754EBD : mData.mUnknownA324E26C;
}

// fn_14_7B84: the jump timer (the float at guest this+0x938 = the data block's +0x178,
// mJumpTimer, the load at 0x7B84) is copied into the member at guest this+0x11E4 (the store at
// 0x7B88).
// @ fn_14_7B84
void CDigitalGuardian::ResetJumpTime(CStateManager& mgr, float dt) {
  x11E4_ = mData.mJumpTimer;
}

// fn_14_7B24: the minimum leg stab attack time (the float at guest this+0x7CC = the data
// block's +0x0C, mMinLegStabAttackTime, the load at 0x7B3C) goes into the member at guest
// this+0x11E0 (the store at 0x7B40) and the jump timer (guest this+0x938) into the member at
// guest this+0x11E4 (the load at 0x7B44 and the store at 0x7B48). Then the head module is
// asked to reset its attack times (the head's fn_14_13B08, the call at 0x7B6C).
// @ fn_14_7B24
void CDigitalGuardian::ResetAttackTimes(CStateManager& mgr, float dt) {
  x11E0_ = mData.mMinLegStabAttackTime;
  x11E4_ = mData.mJumpTimer;
  if (CDigitalGuardianHead* head = TCastToPtr< CDigitalGuardianHead >(mgr.ObjectById(x1082_))) {
    head->x13B08_(mgr);
  }
}

// fn_14_7168: the head's id goes to 0x1084, then 0x1082 = 0x1084 (0x7194..0x71A0), and 0x1086 is
// cleared (0x71A8). The body stops being a target (li r4, 0x28 = kMT_Target, 0x71AC), its attack
// times reset (fn_14_7B24 with .rodata 0x264, 0), and the head's attack mode becomes 0
// (fn_14_13B30 at 0x71EC).
// @ fn_14_7168
void CDigitalGuardian::x7168_(CStateManager& mgr, TUniqueId head) {
  x1084_ = head;
  x1082_ = x1084_;
  x1086_ = kInvalidUniqueId;
  RemoveMaterial(kMT_Target, mgr);
  ResetAttackTimes(mgr, 0.f);
  if (CDigitalGuardianHead* linked = TCastToPtr< CDigitalGuardianHead >(mgr.ObjectById(x1082_))) {
    linked->SetAttackMode(0);
  }
}

// fn_14_70E4: the head's attack mode becomes 1 (fn_14_13B30 at 0x7124), 0x1082 and 0x1086 are
// cleared, and the body is a target again (AddMaterial with 0x28 = kMT_Target, 0x7148).
// @ fn_14_70E4
void CDigitalGuardian::x70E4_(CStateManager& mgr) {
  if (CDigitalGuardianHead* head = TCastToPtr< CDigitalGuardianHead >(mgr.ObjectById(x1082_))) {
    head->SetAttackMode(1);
  }
  x1082_ = kInvalidUniqueId;
  x1086_ = kInvalidUniqueId;
  AddMaterial(kMT_Target, mgr);
}

// fn_14_70B4: bit 0x08 of 0x128C (extrwi. 1, 28) clear and the beacon's state 2 or 1.
// @ fn_14_70B4
bool CDigitalGuardian::x70B4_() const {
  if ((x128C_ & 0x08) != 0) {
    return false;
  }
  return x1268_ == 2 || x1268_ == 1;
}

// fn_14_77EC: the face direction 0x1278 is cleared (sZeroVector, 0x7818..0x7848). The knee
// directions (guest 0xFC8, 12 bytes each, the ctor's pushes at 0xBD7C..0xBE3C: (-1, 1, 0),
// (-1, -1, 0), (1, 1, 0), (1, -1, 0) with -1.0f = .rodata 0x2E8, 1.0f = .rodata 0x270) of the knees
// whose armour id (0xFB0 + 2i) is valid (the cmplw at 0x786C) and names an object (0x788C) are
// summed (the loop's cmpwi 4 at 0x78C8). The body-from-player vector (the positions at +0x54,
// 0x78F4..0x7904) goes into the body's frame (TransposeRotate at 0x7914).
// With a non-zero sum (the three fcmpu with 0.0f, .rodata 0x264, at 0x7934..0x7958): the angle
// between the two (GetAngleDiff at 0x7974) snapped to 45 degrees (fn_14_37FC, 0x797C); only when
// it is above 0.0f (0x798C) the side - local.x * sum.y - local.y * sum.x + local.z * sum.z above
// 0.0f (0x79B8) - picks pi/2 (.rodata 0x2D0) minus (0x79C8) or plus (0x79D8) it, and 0x1278 is the
// body's rotation of (cos, sin, 0) of that angle (0x79E0..0x7A2C).
// With a zero sum: a random angle in [pi/2, pi] (.rodata 0x2D0/0x2D4, Range at 0x7A5C), the side
// is local . the transform's first column (0x24/0x34/0x44, 0x7A6C..0x7A7C) above 0.0f (0x7A80),
// and 0x1278 the same rotation (0x7AA4..0x7AF4).
// gcov (tools/cov/specs/digitalguardian_arms.con, one run): 14/14 branches, ALL-ARMS.
// @ fn_14_77EC
void CDigitalGuardian::SetProtectKneesFaceTarget(CStateManager& mgr, float dt) {
  static const float kKneeDirections[4][2] = {{-1.f, 1.f}, {-1.f, -1.f}, {1.f, 1.f}, {1.f, -1.f}};
  x1278_ = CVector3f::Zero();
  const u16 knees[4] = {xFB0_, xFB2_, xFB4_, xFB6_};
  CVector3f sum = CVector3f::Zero();
  for (int i = 0; i < 4; ++i) {
    if (knees[i] != kInvalidUniqueId.value && mgr.ObjectById(TUniqueId(knees[i])) != nullptr) {
      sum += CVector3f(kKneeDirections[i][0], kKneeDirections[i][1], 0.f);
    }
  }
  const CVector3f local =
      GetTransform().TransposeRotate(GetTranslation() - mgr.GetPlayer(0)->GetTranslation());
  float angle;
  if (sum.IsNonZero()) {
    const float snapped = x37FC_(CVector3f::GetAngleDiff(local, sum));
    if (!(snapped > 0.f)) {
      return;
    }
    if (local.GetX() * sum.GetY() - local.GetY() * sum.GetX() + local.GetZ() * sum.GetZ() > 0.f) {
      angle = 1.5707964f - snapped;
    } else {
      angle = 1.5707964f + snapped;
    }
  } else {
    const CVector3f right = GetTransform().GetRight();
    const float offset = mgr.Random()->Range(1.5707964f, 3.1415927f);
    if (local.GetX() * right.GetX() + local.GetY() * right.GetY() + local.GetZ() * right.GetZ() >
        0.f) {
      angle = 1.5707964f - offset;
    } else {
      angle = 1.5707964f + offset;
    }
  }
  const float s = CMath::FastSinR(angle);
  const float c = CMath::FastCosR(angle);
  x1278_ = GetTransform().Rotate(CVector3f(c, s, 0.f));
}

// fn_14_775C: bit 5 of 0x128C set (fn_14_3D2C with 1, 0x777C), the knees' vulnerabilities
// (fn_14_3A44, 0x7788), the knee armours not orbitable (fn_14_3DA8 with 0, 0x7798), bit 2 of
// 0x128C cleared (the rlwimi at 0x77A4) and the head (uid 0x1082, 0x77B4), when there is one
// (0x77C4), set to attack mode 2 (the li at 0x77CC, fn_14_13B30).
// Every arm ran (tools/cov/specs/digitalguardian_arms.con).
// @ fn_14_775C
void CDigitalGuardian::SetVortexAttackMode(CStateManager& mgr, float dt) {
  x3D2C_(mgr, true);   // fn_14_3D2C
  x3A44_(mgr);         // fn_14_3A44
  x3DA8_(mgr, false);  // fn_14_3DA8
  x128C_ &= ~0x04;
  if (CDigitalGuardianHead* head = TCastToPtr< CDigitalGuardianHead >(mgr.ObjectById(x1082_))) {
    head->SetAttackMode(2);
  }
}

// fn_14_76D0: locomotion 1 (kLT_Relaxed, 0x76F4), vortex mode 0 (0x11B8, 0x7700), bit 5 of
// 0x128C cleared (fn_14_3D2C with 0, 0x770C), the knees' vulnerabilities (fn_14_3A44, 0x7718)
// and the head (uid 0x1082, 0x771C), when there is one (0x7734), set to attack mode 0 (the li
// at 0x773C, fn_14_13B30).
// @ fn_14_76D0
void CDigitalGuardian::SetNormalAttackMode(CStateManager& mgr, float dt) {
  BodyController()->SetLocomotionType(pas::kLT_Relaxed);
  x11B8_ = 0;
  x3D2C_(mgr, false); // fn_14_3D2C
  x3A44_(mgr);        // fn_14_3A44
  if (CDigitalGuardianHead* head = TCastToPtr< CDigitalGuardianHead >(mgr.ObjectById(x1082_))) {
    head->SetAttackMode(0);
  }
}

// fn_14_768C: the head module is looked up by the uid at guest this+0x1082 (the lhz at 0x7698)
// and, if it exists, asked to disable its attacks with the argument 3 (the li r4, 3 at 0x76B8
// and the call at 0x76BC, fn_14_13B30).
// @ fn_14_768C
void CDigitalGuardian::DisableHeadAttacks(CStateManager& mgr, float dt) {
  if (CDigitalGuardianHead* head = TCastToPtr< CDigitalGuardianHead >(mgr.ObjectById(x1082_))) {
    head->SetAttackMode(3);
  }
}

// fn_14_7648: the head module is looked up by the uid at guest this+0x1082 (the lhz at 0x7654)
// and, if it exists, asked to reset its attacks with the argument 4 (the li r4, 4 at 0x7674 and
// the call at 0x7678, fn_14_13B30).
// Every arm ran (tools/cov/specs/digitalguardian_arms.con).
// @ fn_14_7648
void CDigitalGuardian::ResetHeadAttacks(CStateManager& mgr, float dt) {
  if (CDigitalGuardianHead* head = TCastToPtr< CDigitalGuardianHead >(mgr.ObjectById(x1082_))) {
    head->SetAttackMode(4);
  }
}

// fn_14_7600: the closest active waypoint to the player (the state manager's player at
// +0x14FC, the lwz at 0x761C, and its position at +0x54, the addi at 0x7624) is picked by
// fn_14_308C (the call at 0x7628) and its id is stored in the member at guest this+0x1168 (the
// store at 0x7630).
// @ fn_14_7600
void CDigitalGuardian::SelectExitVortexTarget(CStateManager& mgr, float dt) {
  const CVector3f player = mgr.GetPlayer(0)->GetTranslation();
  x1168_ = x308C_(mgr, player);
}

// @ fn_14_7504
// fn_14_7504: 0x11D8 starts at -1 (0x7544); the leg whose locator (0x10C8[i], GetLctrTransform at
// 0x7578) is nearest the player (0x14FC, its x and y at 0x7564/0x7568) on the xy plane (the
// square sum plus 0.0f, .rodata 0x264, 0x75A0) wins: a leg replaces the best only when strictly
// nearer (the fcmpo/bge at 0x75A4, so a NaN sum never wins), the best starting at FLT_MAX
// (.rodata 0x2AC). Four legs (the cmpwi 4 at 0x75BC). dt is unused.
void CDigitalGuardian::SelectBestMeleeAttackLeg(CStateManager& mgr, float dt) {
  x11D8_ = -1;
  float best = FLT_MAX;
  const CVector3f player = mgr.GetPlayer(0)->GetTranslation();
  for (int i = 0; i < 4; ++i) {
    const CVector3f foot = GetLctrTransform(x10C8_[i]).GetTranslation();
    const float dx = player.GetX() - foot.GetX();
    const float dy = player.GetY() - foot.GetY();
    const float distSq = 0.f + (dx * dx + dy * dy);
    if (distSq < best) { // 0x75A4 fcmpo/bge skips (a NaN too)
      best = distSq;
      x11D8_ = i;
    }
  }
}

// fn_14_73D4: 0x11DC is -1 (0x740C), and with no melee leg (0x11D8 == -1, 0x7414) stays so. With
// one, on the xy plane (each square sum plus 0.0f, .rodata 0x264): when the player (0x14FC) is
// nearer the body than the leg's locator (0x10C8[0x11D8], GetLctrTransform at 0x7440) is (the
// fcmpo at 0x7488), 0 (0x7494); otherwise 0 when the player is within the data's
// nearLegStabRange (guest 0x7C4, squared) of the locator (0x74B8), else 1 (0x74CC).
// dt is unused (fn_14_A468 passes 0.0f, .rodata 0x264).
// gcov (tools/cov/specs/digitalguardian_arms.con, one run): 10/10 branches, ALL-ARMS.
// @ fn_14_73D4
void CDigitalGuardian::SelectMeleeAttackRange(CStateManager& mgr, float dt) {
  x11DC_ = -1;
  if (x11D8_ == -1) {
    return;
  }
  const CVector3f body = GetTranslation();
  const CVector3f player = mgr.GetPlayer(0)->GetTranslation();
  const CVector3f foot = GetLctrTransform(x10C8_[x11D8_]).GetTranslation();
  const float playerX = player.GetX() - body.GetX();
  const float playerY = player.GetY() - body.GetY();
  const float footX = foot.GetX() - body.GetX();
  const float footY = foot.GetY() - body.GetY();
  // 0x7488 fcmpo/bge to the range test (a NaN goes there too).
  if (0.f + (playerX * playerX + playerY * playerY) < 0.f + (footX * footX + footY * footY)) {
    x11DC_ = 0;
    return;
  }
  const float dx = player.GetX() - foot.GetX();
  const float dy = player.GetY() - foot.GetY();
  // 0x74B8 fcmpo/bge to the li 1 (a NaN gives 1 too).
  x11DC_ = 0.f + (dx * dx + dy * dy) < mData.mNearLegStabRange * mData.mNearLegStabRange ? 0 : 1;
}

// @ fn_14_7208
// fn_14_7208: the face target 0x1278 is zeroed (0x724C..0x7260). The waypoint nearest the player
// (fn_14_308C with the player's translation, 0x7280) is looked up (GetObjectById at 0x7298; none:
// done, 0x72A0). The angle between the body's offset to it (0x72D0..0x72F0) and the body's
// forward column (0x28/0x38/0x48, 0x72A4..0x72BC; GetAngleDiff at 0x72F4) is snapped to the
// nearest 45 degrees (fn_14_37FC, 0x72FC); with a snapped angle above 0.0f (.rodata 0x264, the
// fcmpo/ble at 0x7308) the turn is pi/2 (.rodata 0x2D0) minus it when the offset is on the
// right (its dot with the right column 0x24/0x34/0x44 above 0.0f, 0x7334), else pi/2 plus it
// (0x7354), and 0x1278 is the transform's rotation of (cos, sin, 0) of that turn (0x735C..0x7390,
// stores 0x7398..0x73A8). Retail reads the object's translation (0x54) without a type check;
// the host casts to CActor (fn_14_308C only returns waypoints).
void CDigitalGuardian::SetJumpFaceTarget(CStateManager& mgr, float dt) {
  x1278_ = CVector3f::Zero();
  const CVector3f player = mgr.GetPlayer(0)->GetTranslation();
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(x308C_(mgr, player)));
  if (target == nullptr) {
    return;
  }
  const CVector3f forward = GetTransform().GetForward();
  const CVector3f offset = target->GetTranslation() - GetTranslation();
  const float angle = x37FC_(CVector3f::GetAngleDiff(offset, forward)); // fn_14_37FC
  if (angle > 0.f) {
    const float turn = CVector3f::Dot(offset, GetTransform().GetRight()) > 0.f
                           ? 1.5707964f - angle
                           : 1.5707964f + angle; // .rodata 0x2D0
    const float sinTurn = CMath::FastSinR(turn);
    const float cosTurn = CMath::FastCosR(turn);
    x1278_ = GetTransform().Rotate(CVector3f(cosTurn, sinTurn, 0.f));
  }
}

// fn_14_1570: the anim data's vertex bounds (0x15CC) through Translate(-translation) (the fnegs
// at 0x1630..0x1638, 0x1654) * the transform (0x24, 0x1664) * Scale(the model's scale, 0x1648;
// 0x1674), GetTransformedAABox at 0x1684. The translation cancels: the box is about the origin.
// @ fn_14_1570
CAABox CDigitalGuardian::x1570_() const {
  const CAABox verts = GetModelData()->GetAnimationData()->CalcBoundingBoxFromModelVerts();
  const CTransform4f xf = CTransform4f::Translate(-GetTranslation()) * GetTransform() *
                          CTransform4f::Scale(GetModelData()->GetScale());
  return verts.GetTransformedAABox(xf);
}

// vtable slot 71 = fn_14_9CA4: fn_14_1570's box, with the head (0x1082, TCastToPtr at 0x9CE0) the
// head's own scan bounds (vtable 0x11C at 0x9D40) taken in (AccumulateBounds at 0x9D4C, 0x9D58).
// @ fn_14_9CA4
CAABox CDigitalGuardian::GetScanVisorRenderBounds(const CStateManager& mgr) const {
  if (const CDigitalGuardianHead* head =
          TCastToConstPtr< CDigitalGuardianHead >(mgr.GetObjectById(x1082_))) {
    CAABox bounds = x1570_();                                   // 0x9CF4
    const CAABox headBounds = head->GetScanVisorRenderBounds(mgr); // 0x9D40
    bounds.AccumulateBounds(headBounds.GetMinPoint());          // 0x9D4C
    bounds.AccumulateBounds(headBounds.GetMaxPoint());          // 0x9D58
    return bounds;
  }
  return x1570_(); // 0x9D98
}

// vtable slot 72 = fn_14_9ABC: the head (0x1082, 0x9AF0) draws itself at xf * the "Head_1"
// locator's scaled transform (0x10B8, 0x9B08..0x9B18; vtable 0x120 at 0x9B44). The body then
// draws as AddToRenderer does, but at xf with the given flags: the transform (0x24) and draw
// flags (0xFC) are swapped for them (0x9B48..0x9BB8) and put back at the end (0x9C5C..0x9C8C).
// @ fn_14_9ABC
void CDigitalGuardian::ScanVisorRender(const CStateManager& mgr, const CTransform4f& xf,
                                       const CModelFlags& flags) const {
  if (const CDigitalGuardianHead* head =
          TCastToConstPtr< CDigitalGuardianHead >(mgr.GetObjectById(x1082_))) {
    head->ScanVisorRender(mgr, xf * GetScaledLocatorTransform(CSegId(x10B8_)), flags);
  }
  const CTransform4f savedXf = GetTransform();   // 0x9B50
  const CModelFlags savedFlags = GetModelFlags(); // 0x9B54..0x9B84
  PortRawTransform() = xf;                        // 0x9B88
  const_cast< CDigitalGuardian* >(this)->SetModelFlags(flags); // 0x9B8C..0x9BB8
  CPatterned::Render(mgr);  // 0x9BBC
  x1994_(mgr, xf, flags);   // 0x9BD0
  x18B4_(mgr, xf, flags);   // 0x9BE4
  // The beacon: 0x128C bit 0x08 clear (0x9BEC), the model there (0x1238, 0x9BF8) and its height
  // above 0 (.rodata 0x264, fcmpo/ble at 0x9C0C); at fn_14_2578's transform of xf (0x9C20),
  // with the actor's lights (0xBC) and the given flags (Render at 0x9C44).
  if ((x128C_ & 0x08) == 0 && x11EC_.valid() && x1264_ > 0.f) {
    CTransform4f modelXf = CTransform4f::Identity();
    x2578_(modelXf, xf);
    x11EC_.data().Render(mgr, modelXf, GetActorLights(), flags);
  }
  x1700_(mgr, xf, flags);                     // 0x9C58
  PortRawTransform() = savedXf;               // 0x9C64
  const_cast< CDigitalGuardian* >(this)->SetModelFlags(savedFlags); // 0x9C68..0x9C8C
}

// vtable slot 73 = fn_14_D8 (0x8): it returns the base member at 0x754, which is what the base
// class's own accessor returns.
const rstl::optional_object< TCachedToken< CGenDescription > >&
CDigitalGuardian::GetDeathExplosionParticle() const {
  return CPatterned::GetDeathExplosionParticle();
}

// ---------------------------------------------------------------------------------------------
// The module's loaders (fn_14_1B8 and fn_14_DF7C) and entry points.

// Retail fn_14_1B8. The property ids and their order are the generated SLdrDigitalGuardian
// reader's, which the analysis checked against the REL's switch id by id: the loader's dispatch
// tree reads all four blocks (0x255A4580 editor properties, 0xB3774750 patterned AI, 0x7E397FED
// actor parameters, 0xC55918CC guardian data; any other id is skipped, ReadBytes at 0x338).
// The defaults stored before the read (0x1F8..0x264) are the generated SLdrDigitalGuardian ctor's:
// patterned +0x8 turnSpeed 360 (.rodata 0x260), +0x18 minAttackRange 0 (0x264), +0x48 health 150
// (0x268), +0x4C knock-back resistance 2 (0x26C), +0x22C creatureSize 2 (li 0x214); the actor
// parameters' ambient colour (1, 1, 1, 1; CColor 0x248 -> +0x14) and visor flags 0xF (0x250 ->
// +0x64) are already the SLdrLightParameters / SLdrVisorParameters ctor defaults.
// The guardian data goes to the ctor as the SLdr struct (stw r29 0x448, copied by fn_14_C68C);
// the host copies its 51 properties into CDigitalGuardianData, every field.
// The empty-model arm (0x38C) is data-dead (the shipped DGRD, 0x00350060, has a model); forced
// (tools/cov/specs/digitalguardian_dg33_noload.py). Retail's null test after __nw__ (0x3F4) has no
// host arm (the host's allocator does not return null).
// @ fn_14_1B8
static CEntity* PortLoadDigitalGuardian(CStateManager& mgr, CInputStream& input,
                                         CEntityInfo& info) {
  SLdrDigitalGuardian sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrDigitalGuardian.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    // The REL checks the optional's valid byte (sp+0xF8, read at 0x0000036C) and the model copy's
    // own valid byte (sp+0x148, read at 0x00000384), and returns null when the model is empty.
    return nullptr;
  }

  // The parameter block, field by field: the loader's stack struct is copied into the object
  // verbatim by fn_14_C68C, so the host keeps the loader's own values.
  const SLdrDigitalGuardianData& sldr = sldrThis.digitalGuardianProperties;
  CDigitalGuardianData data;
  data.mScannableInfoCrippled = sldr.scannableInfoCrippled;
  data.mNearLegStabRange = sldr.nearLegStabRange;
  data.mFarLegStabRange = sldr.farLegStabRange;
  data.mMinLegStabAttackTime = sldr.minLegStabAttackTime;
  data.mLegStabDamage = sldr.legStabDamage;
  data.mToeTargetHP = sldr.toeTargetHP;
  data.mToeTargetModel = sldr.toeTargetModel;
  data.mToeTargetExplosion = sldr.toeTargetExplosion;
  data.mSoundToeTarget = sldr.sound_ToeTarget;
  data.mSoundToeTargetAttack = sldr.sound_ToeTargetAttack;
  data.mSoundToeTargetExplosion = sldr.sound_ToeTargetExplosion;
  data.mSoundToeTargetHit = sldr.sound_ToeTargetHit;
  data.mSoundShockWave = sldr.sound_ShockWave;
  data.mShockWaveInfo = sldr.shockWaveInfo;
  data.mVortexAttackDuration = sldr.vortexAttackDuration;
  data.mVortexAttractionForce = sldr.vortexAttractionForce;
  data.mMaxVortexAttractionDistance = sldr.maxVortexAttractionDistance;
  data.mVortexLinearVelocity = sldr.vortexLinearVelocity;
  data.mVortexLinearAcceleration = sldr.vortexLinearAcceleration;
  data.mVortexDamage = sldr.vortexDamage;
  data.mUnknownFB5263E8 = sldr.unknown_0xfb5263e8;
  data.mUnknown6AAF33E3 = sldr.unknown_0x6aaf33e3;
  data.mUnknown4F5D725C = sldr.unknown_0x4f5d725c;
  data.mSoundVortexFlash = sldr.sound_VortexFlash;
  data.mLegModel = sldr.legModel;
  data.mShinArmor = sldr.shinArmor;
  data.mKneeArmorHP = sldr.kneeArmorHP;
  data.mSoundKneeArmorHit = sldr.sound_KneeArmorHit;
  data.mSoundKneeVulnerable = sldr.sound_KneeVulnerable;
  data.mKneeArmor = sldr.kneeArmor;
  data.mKneeEchoParameters = sldr.kneeEchoParameters;
  data.mUnknownA324E26C = sldr.unknown_0xa324e26c;
  data.mUnknown6A754EBD = sldr.unknown_0x6a754ebd;
  data.mJumpTimer = sldr.jumpTimer;
  data.mUnknown8106CDA9 = sldr.unknown_0x8106cda9;
  data.mUnknown9E1B8105 = sldr.unknown_0x9e1b8105;
  data.mUnknownA08FCC70 = sldr.unknown_0xa08fcc70;
  data.mUnknown3254A16B = sldr.unknown_0x3254a16b;
  data.mTransmissionBeacon = sldr.transmissionBeacon;
  data.mTransmissionBeaconFx = sldr.transmissionBeaconFx;
  data.mTransmissionBeaconEchoParameters = sldr.transmissionBeaconEchoParameters;
  data.mSoundTransmissionBeacon = sldr.sound_TransmissionBeacon;
  data.mSoundBeaconExtract = sldr.sound_BeaconExtract;
  data.mSoundBeaconRetract = sldr.sound_BeaconRetract;
  data.mTransmissionBeaconHP = sldr.transmissionBeaconHP;
  data.mTransmissionBeaconExplosion = sldr.transmissionBeaconExplosion;
  data.mTransmissionBeaconEchoHit = sldr.transmissionBeaconEchoHit;
  data.mSoundBeaconExplode = sldr.sound_BeaconExplode;
  data.mSoundBeaconHit = sldr.sound_BeaconHit;
  data.mKneeVulnerability = sldr.kneeVulnerability;
  data.mVortexVulnerability = sldr.vortexVulnerability;
  data.mToeTargetVulnerability = sldr.toeTargetVulnerability;

  return rs_new CDigitalGuardian(mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
                                  LdrToEntityInfo(info, sldrThis.editorProperties),
                                  LdrToTransform4f(sldrThis.editorProperties), *modelData,
                                  LdrToActorParameters(sldrThis.actorInformation),
                                  LdrToPatternedInfo(sldrThis.patterned, nullptr), data);
}

// The head module's loader (fn_14_DF7C) is PortLoadDigitalGuardianHead in
// PortCDigitalGuardianHead.cpp.

static SDigitalGuardian_FuncPtrs REL_loader_DigitalGuardian;

// Retail's RELMain calls fn_14_17C, which stores the two loaders (fn_14_1B8 and fn_14_DF7C) into
// lbl_14_bss_C (the module's SDigitalGuardian_FuncPtrs) and calls the DOL's
// SetSDigitalGuardian_FuncPtrs (fn_8021F9B0). The proxy in SDigitalGuardian_FuncPtrs.cpp calls
// through it.
void RELMain() {
  REL_loader_DigitalGuardian.mLoadDigitalGuardian = PortLoadDigitalGuardian;
  REL_loader_DigitalGuardian.mLoadDigitalGuardianHead = PortLoadDigitalGuardianHead;
  SetSDigitalGuardian_FuncPtrs(&REL_loader_DigitalGuardian);
}

void RELExit() { SetSDigitalGuardian_FuncPtrs(nullptr); }

// Host: the console's `var <id> dg.<name> [value]` for the body, so the trigger and helper arms
// a fight rarely draws run without gdb (tools/cov/specs/digitalguardian_dg29.con).
bool CDigitalGuardian::PortConsoleVar(CEntity& ent, const char* name, const int* value,
                                      int& current) {
  CDigitalGuardian* dg = TCastToPtr< CDigitalGuardian >(&ent);
  if (dg == nullptr) {
    return false;
  }
  struct SIntVar {
    const char* name;
    int* field;
  };
  const SIntVar ints[] = {{"dg.vortex", &dg->x11B8_}, {"dg.knee", &dg->x1284_},
                          {"dg.beacon", &dg->x1268_}};
  for (const SIntVar& var : ints) {
    if (strcmp(name, var.name) == 0) {
      if (value != nullptr) {
        *var.field = *value;
      }
      current = *var.field;
      return true;
    }
  }
  struct SByteVar {
    const char* name;
    uchar* field;
  };
  const SByteVar bytes[] = {{"dg.flags", &dg->x128C_}, {"dg.flags2", &dg->x128D_}};
  for (const SByteVar& var : bytes) {
    if (strcmp(name, var.name) == 0) {
      if (value != nullptr) {
        *var.field = static_cast< uchar >(*value);
      }
      current = *var.field;
      return true;
    }
  }
  struct SFloatVar {
    const char* name;
    float* field;
  };
  const SFloatVar floats[] = {{"dg.vortexhp", &dg->x11C8_}, {"dg.jumptime", &dg->x11E4_}};
  for (const SFloatVar& var : floats) {
    if (strcmp(name, var.name) == 0) {
      if (value != nullptr) {
        *var.field = static_cast< float >(*value);
      }
      current = static_cast< int >(*var.field);
      return true;
    }
  }
  // dg.toe0..3: the toe collider uids at 0x1078 (read them for `hurt`); dg.toestate0..3: the
  // per-leg toe target state 0x10DC (3 = destroyed); dg.shaker: the camera shaker uid 0x11C0.
  for (int i = 0; i < 4; ++i) {
    char key[16];
    snprintf(key, sizeof(key), "dg.toe%d", i);
    if (strcmp(name, key) == 0) {
      if (value != nullptr) {
        dg->x1078_[i] = static_cast< u16 >(*value);
      }
      current = dg->x1078_[i];
      return true;
    }
    snprintf(key, sizeof(key), "dg.toestate%d", i);
    if (strcmp(name, key) == 0) {
      if (value != nullptr) {
        dg->x10DC_[i] = *value;
      }
      current = dg->x10DC_[i];
      return true;
    }
  }
  // dg.kneecol0..3: the knee collider uids at 0xFBC (fn_14_3E2C's table). dg.killknee0..3
  // (write 1): the collider's HP goes to 0 and the body takes its XDMG (fn_14_5838), as a
  // shot that breaks the knee armour does, so the boss-bar counter drops by one and IS07 /
  // KneeDestroyed run. `damage` cannot: the colliders forward hits to the body without losing HP.
  for (int i = 0; i < 4; ++i) {
    char key[20];
    snprintf(key, sizeof(key), "dg.kneecol%d", i);
    if (strcmp(name, key) == 0) {
      if (value != nullptr) {
        dg->xFBC_[i] = static_cast< u16 >(*value);
      }
      current = dg->xFBC_[i];
      return true;
    }
    snprintf(key, sizeof(key), "dg.killknee%d", i);
    if (strcmp(name, key) == 0) {
      CStateManager* mgr = PortConsole::VarManager();
      CCollisionActor* collider =
          mgr != nullptr ? TCastToPtr< CCollisionActor >(mgr->ObjectById(TUniqueId(dg->xFBC_[i])))
                         : nullptr;
      if (value != nullptr && *value != 0 && collider != nullptr) {
        collider->HealthInfo()->SetHP(0.f);
        dg->x5838_(*mgr, collider->GetUniqueId());
      }
      current = collider != nullptr ? static_cast< int >(collider->GetHealthInfo()->GetHP()) : -1;
      return true;
    }
  }
  // dg.armor0..3: the knee armour ids 0xFB0..0xFB6 (fn_14_3DA8).
  u16* const armor[4] = {&dg->xFB0_, &dg->xFB2_, &dg->xFB4_, &dg->xFB6_};
  for (int i = 0; i < 4; ++i) {
    char key[16];
    snprintf(key, sizeof(key), "dg.armor%d", i);
    if (strcmp(name, key) == 0) {
      if (value != nullptr) {
        *armor[i] = static_cast< u16 >(*value);
      }
      current = *armor[i];
      return true;
    }
  }
  // dg.exit 0x1168 (fn_14_34E8's waypoint), dg.follow 0x116A (fn_14_3300's object), dg.root
  // 0x1080 (the root collider), dg.beaconh: the beacon's height 0x1264 in thousandths.
  struct SIdVar {
    const char* name;
    u16* field;
  };
  const SIdVar ids[] = {{"dg.exit", &dg->x1168_.value}, {"dg.follow", &dg->x116A_.value},
                        {"dg.root", &dg->x1080_}};
  for (const SIdVar& var : ids) {
    if (strcmp(name, var.name) == 0) {
      if (value != nullptr) {
        *var.field = static_cast< u16 >(*value);
      }
      current = *var.field;
      return true;
    }
  }
  if (strcmp(name, "dg.beaconh") == 0) {
    if (value != nullptr) {
      dg->x1264_ = static_cast< float >(*value) / 1000.f;
    }
    current = static_cast< int >(dg->x1264_ * 1000.f);
    return true;
  }
  if (strcmp(name, "dg.shaker") == 0) {
    if (value != nullptr) {
      dg->x11C0_ = TUniqueId(static_cast< u16 >(*value));
    }
    current = dg->x11C0_.value;
    return true;
  }
  // dg.facex/facey/facez: one component of 0x1278 each (fn_14_9A60 tests them in turn).
  for (int i = 0; i < 3; ++i) {
    static const char* const kFace[3] = {"dg.facex", "dg.facey", "dg.facez"};
    if (strcmp(name, kFace[i]) == 0) {
      float xyz[3] = {dg->x1278_.GetX(), dg->x1278_.GetY(), dg->x1278_.GetZ()};
      if (value != nullptr) {
        xyz[i] = static_cast< float >(*value);
        dg->x1278_ = CVector3f(xyz[0], xyz[1], xyz[2]);
      }
      current = static_cast< int >(xyz[i]);
      return true;
    }
  }
  return false;
}

namespace {
const bool sDgConsoleVars = PortConsole::RegisterVar(&CDigitalGuardian::PortConsoleVar);
} // namespace
