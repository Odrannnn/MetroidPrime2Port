#include "MetroidPrime/Enemies/CSandworm.hpp"

#include "Collision/CMaterialFilter.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CColor.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "Kyoto/Text/CStringTable.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/BodyState/CBodyStateCmdMgr.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CProjectedShadow.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Enemies/CPatternedInfo.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CGameProjectile.hpp"

// WP1 of the retail Sandworm REL (module 56, G2ME01): loader, both constructors, Think and its
// per-frame helpers, the small virtual overrides and the member-struct constructors. Read from
// the retail asm for behaviour, not for code shape; docs/module-analysis/Sandworm.md and
// Sandworm_functions.tsv have the function map. The other packages' bodies live in the
// PortCSandworm*.cpp files next to this one.
//
// The destructors of x1528 (fn_56_126EC), x1770 (fn_56_12780) and x1490 (fn_56_285C) are the
// compiler-generated ones of SSandwormSpit, SSandwormPincers and SSandwormAttackHistory.

namespace {

CPathFindSearch MakeSearch(const CPatternedInfo& pInfo, uint flags) {
  return CPathFindSearch(nullptr, flags, pInfo.GetPathfindingIndex(), 1.f, 1.f, 0,
                         CPFRegion::kRP_Center);
}

// Retail reads CPatternedInfo+0x120 (taken to be SLdrIngPossessionData::isAnEncounter, as in the
// Grenchler) and passes 0x301 when it is set.
uint SearchFlags(const CPatternedInfo& pInfo) { return pInfo.IsAnEncounter() ? 0x301 : 1; }

CElementGen* MakeGlowGen(CAssetId id) {
  if (id == kInvalidAssetId) {
    return nullptr;
  }
  return rs_new CElementGen(TToken< CGenDescription >(gpSimplePool->GetObj(SObjectTag('PART', id))),
                            CElementGen::kMOT_Normal, CElementGen::kOSF_One);
}

} // namespace

// SSandwormAttackHistory (x1490)

// @ fn_56_28F0
SSandwormAttackHistory::SSandwormAttackHistory() : x0_picked(-1), x4_current(-1) {}

// @ fn_56_26C4
bool SSandwormAttackHistory::EndsWith(int type, int count) const {
  for (int i = 0; i < count; ++i) {
    const int idx = x8_history.size() - (i + 1);
    if (idx < 0 || x8_history[idx] != type) {
      return false;
    }
  }
  return true;
}

// @ fn_56_13B80
SSandwormCharge::SSandwormCharge(float rangeMin, float rangeMax, float impulseH, float impulseV)
: x0_(CVector3f::Zero())
, xc_rangeMin(rangeMin)
, x10_rangeMax(rangeMax)
, x14_impulseH(impulseH)
, x18_impulseV(impulseV)
, x1c_(CVector3f::Zero())
, x28_(0.f)
, x2c_(0.f)
, x30_flags(0) {}

// @ fn_56_138F0
SSandwormSpit::SSandwormSpit(CAssetId projectile, const CDamageInfo& damage, CAssetId visorEffect,
                             float x5c, float rangeMin, float rangeMax)
: x0_info(projectile, damage)
, x28_xf(CTransform4f::Identity())
, x58_(0.f)
, x5c_(x5c)
, x60_rangeMin(rangeMin)
, x64_rangeMax(rangeMax)
, x68_(CVector3f::Zero()) {
  if (visorEffect != kInvalidAssetId) {
    x74_visorEffect = TLockedToken< CGenDescription >(
        TToken< CGenDescription >(gpSimplePool->GetObj(SObjectTag('PART', visorEffect))));
  }
  x0_info.Token().Lock();
}

// @ fn_56_137F8
SSandwormToss::SSandwormToss(const CDamageInfo& damage, float impulseH, float impulseV)
: x0_xf(CTransform4f::Identity())
, x30_damage(damage)
, x4c_impulseH(impulseH)
, x50_impulseV(impulseV)
, x54_(0.f)
, x58_flags(0) {}

// @ fn_56_13790
SSandwormMelee::SSandwormMelee(const CDamageInfo& damage, float impulseH, float impulseV)
: x0_damage(damage), x1c_(0.f), x20_(true), x24_impulseH(impulseH), x28_impulseV(impulseV) {}

// @ fn_56_136E0
SSandworm1670::SSandworm1670()
: xd4_(CVector3f::Zero()), xe0_(CQuaternion::NoRotation()), xf0_(CVector3f::Zero()), xfc_(0.f) {}

// @ fn_56_13668
SSandwormSfx::SSandwormSfx(ushort walk, ushort walkVocal, ushort melee, ushort eyeKilled,
                           ushort bombBounce, ushort bombExplode)
: x0_walk(walk)
, x2_walkVocal(walkVocal)
, x4_melee(melee)
, x6_eyeKilled(eyeKilled)
, x8_bombBounce(bombBounce)
, xa_bombExplode(bombExplode)
, xc_(0.f)
, x10_(0.f)
, x14_(0)
, x18_(0)
, x1c_(0) {}

// @ fn_56_1359C
SSandwormBombPhase::SSandwormBombPhase()
: x0_(CVector3f::Zero())
, xc_flags(0)
, x10_(0.f)
, x14_(0.f)
, x18_(0.f)
, x1c_(0.f)
, x20_(0.f)
, x24_(-1000.f) {}

// @ fn_56_149D4
SSandwormBombPhase::SSandwormBombPhase(float x18, float x10)
: x0_(CVector3f::Zero())
, xc_flags(0)
, x10_(x10)
, x14_(0.f)
, x18_(x18)
, x1c_(0.f)
, x20_(0.f)
, x24_(-1000.f) {}

// @ fn_56_1A38
SSandwormGlow::SSandwormGlow(CAssetId glow, CAssetId part1, CAssetId part2) {
  x4_gen0 = MakeGlowGen(glow);
  x0_has0 = x4_gen0 != nullptr;
  xc_gen1 = MakeGlowGen(glow);
  x8_has1 = xc_gen1 != nullptr;
  x14_gen2 = MakeGlowGen(part1);
  x10_has2 = x14_gen2 != nullptr;
  x1c_gen3 = MakeGlowGen(part2);
  x18_has3 = x1c_gen3 != nullptr;
}

// @ fn_56_191C
SSandwormGlow::~SSandwormGlow() {
  if (x18_has3) {
    delete x1c_gen3;
  }
  if (x10_has2) {
    delete x14_gen2;
  }
  if (x8_has1) {
    delete xc_gen1;
  }
  if (x0_has0) {
    delete x4_gen0;
  }
}

// CSandworm

// @ fn_56_12D68
CSandworm::CSandworm(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                     const CTransform4f& xf, const CModelData& modelData,
                     const CPatternedInfo& pInfo, CAssetId stateMachine2, float pincerScale,
                     float spitMin, float spitMax, float spitUnknown, float chargeMin,
                     float chargeMax, float chargeImpulseH, float chargeImpulseV, bool flag,
                     CAssetId pincerL, CAssetId pincerR, ushort walkSfx, ushort walkVocalSfx,
                     ushort meleeSfx, ushort eyeKilledSfx, CAssetId visorEffect,
                     float tossImpulseH, float tossImpulseV, float meleeImpulseH,
                     float meleeImpulseV, float unknownE593, float lurkMin, float lurkMax,
                     float frustrationRadius, float frustrationTimer, CAssetId projectile,
                     const CDamageInfo& spitDamage, const CDamageInfo& tossDamage,
                     const CDamageInfo& meleeDamage, CAssetId eyeGlow,
                     const CActorParameters& actorParams)
: CPatterned(static_cast< EPatternedAI >(0x32), uid, name, kFT_Zero, info, xf, modelData, pInfo,
             kMT_Ground, kCT_One, static_cast< EBodyType >(1), actorParams)
, x7C0_(0)
, x7C4_search(MakeSearch(pInfo, SearchFlags(pInfo)))
, x8B0_activeTime(0.f)
, x8B4_(CVector3f::Zero())
, x8C0_flags(((flag ? 1 : 0) << 7) | 0x20 | 0x08)
, x8C1_flags(0)
, x964_(0)
, x968_(0)
, x96C_(-1000.f)
, x984_segCount(0)
, x12B0_(13, 0.f)
, x134C_(-1000.f)
, x1350_(0.f)
, x1354_(CTransform4f::Identity())
, x1384_(0.f)
, x1388_(CVector3f::Zero())
, x1394_snatchStart(CVector3f::Zero())
, x13A0_snatchEnd(CVector3f::Zero())
, x13B0_(CVector3f::Zero())
, x13C0_(4, CTransform4f::Identity())
, x1484_stateMachine2(CToken(gpSimplePool->GetObj(SObjectTag('FSM2', stateMachine2))))
, x14B0_(0.f)
, x14B4_frustrationRadius(frustrationRadius)
, x14B8_frustrationTimer(frustrationTimer)
, x14BC_deadTime(0.f)
, x14C0_flags(0)
, x14C4_headEye(kInvalidUniqueId)
, x14C6_tailEye(kInvalidUniqueId)
, x14C8_(0)
, x14CC_(-1)
, x14D0_(-1)
, x14D4_(0)
, x14D8_charge(chargeMin, chargeMax, chargeImpulseH, chargeImpulseV)
, x150C_cycle(0.f)
, x1510_cycleRate(0.f)
, x1514_(0)
, x1518_bodyCmdFirst(-1)
, x151C_bodyCmdLast(13)
, x1520_flags(0)
, x1524_fadeTarget(0.f)
, x1528_spit(projectile, spitDamage, visorEffect, spitUnknown, spitMin, spitMax)
, x15AC_toss(tossDamage, tossImpulseH, tossImpulseV)
, x1608_residuePos(CVector3f::Zero())
, x1614_residuePosHighHealth(CVector3f::Zero())
, x1620_melee(meleeDamage, meleeImpulseH, meleeImpulseV)
, x164C_(unknownE593)
, x1650_lurkMin(lurkMin)
, x1654_lurkMax(lurkMax)
, x1658_(100.f)
, x165C_(0.f)
, x1660_(-1000.f)
, x1664_(150.f)
, x1668_flags(0)
, x166C_(100000)
, x181C_(0.f)
, x1820_(0.f)
, x1824_(0.f)
, x1828_sfx(walkSfx, walkVocalSfx, meleeSfx, eyeKilledSfx, CSfxManager::kInternalInvalidSfxId,
            CSfxManager::kInternalInvalidSfxId)
, x1848_(kInvalidUniqueId)
, x184A_(kInvalidUniqueId)
, x184C_(0)
, x1850_(1)
, x1854_(-1000.f)
, x1858_search2(MakeSearch(pInfo, 1))
, x1980_bombDropRate(0.f)
, x1984_bombDropTimer(0.f)
, x1988_(-1000.f)
, x198C_(CVector3f::Zero())
, x1998_(kInvalidUniqueId)
, x199A_(0)
, x199C_(CVector3f::Zero())
, x19D0_glow(eyeGlow, kInvalidAssetId, kInvalidAssetId)
, x19F0_(CVector3f::Zero())
, x19FC_(-1000.f)
, x1A00_(0.f)
, x1A04_(0.f)
, x1A08_(0.f)
, x1A0C_bounds(CAABox::MakeNullBox())
, x1A24_(-1000.f) {
  x1770_pincers.x0_left = pincerL;
  x1770_pincers.x4_right = pincerR;
  x1770_pincers.x8_scale = pincerScale;
  FinishConstruction();
}

// @ fn_56_14010
CSandworm::CSandworm(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                     const CTransform4f& xf, const CModelData& modelData,
                     const CPatternedInfo& pInfo, CAssetId stateMachine2, float pincerScale,
                     float spitMin, float spitMax, float spitUnknown, float chargeMin,
                     float chargeMax, float chargeImpulseH, float chargeImpulseV, bool flag,
                     CAssetId pincerL, CAssetId pincerR, ushort walkSfx, ushort walkVocalSfx,
                     ushort meleeSfx, ushort eyeKilledSfx, ushort bombBounceSfx,
                     ushort bombExplodeSfx, CAssetId visorEffect, float tossImpulseH,
                     float tossImpulseV, float meleeImpulseH, float meleeImpulseV,
                     float unknownE593, float lurkMin, float lurkMax, float frustrationRadius,
                     float frustrationTimer, CAssetId projectile, const CDamageInfo& spitDamage,
                     const CDamageInfo& tossDamage, const CDamageInfo& meleeDamage,
                     CAssetId eyeGlow, CAssetId part, CAssetId part8b2a, CAssetId bombFx,
                     CAssetId bombExplosionFx, const CDamageInfo& bombDamage, float bombDropRate,
                     float unknown547f, float unknownEfef, const SLdrSandwormStruct& phase0,
                     const SLdrSandwormStruct& phase1, const SLdrSandwormStruct& phase2,
                     const SLdrSandwormStruct& phase3, const SLdrSandwormStruct& phase4,
                     const CActorParameters& actorParams)
: CPatterned(static_cast< EPatternedAI >(0x32), uid, name, kFT_Zero, info, xf, modelData, pInfo,
             kMT_Ground, kCT_One, static_cast< EBodyType >(1), actorParams)
, x7C0_(0)
, x7C4_search(MakeSearch(pInfo, SearchFlags(pInfo)))
, x8B0_activeTime(0.f)
, x8B4_(CVector3f::Zero())
, x8C0_flags(((flag ? 1 : 0) << 7) | 0x20 | 0x10)
, x8C1_flags(0)
, x964_(0)
, x968_(0)
, x96C_(-1000.f)
, x984_segCount(0)
, x12B0_(13, 0.f)
, x134C_(-1000.f)
, x1350_(0.f)
, x1354_(CTransform4f::Identity())
, x1384_(0.f)
, x1388_(CVector3f::Zero())
, x1394_snatchStart(CVector3f::Zero())
, x13A0_snatchEnd(CVector3f::Zero())
, x13B0_(CVector3f::Zero())
, x13C0_(4, CTransform4f::Identity())
, x1484_stateMachine2(CToken(gpSimplePool->GetObj(SObjectTag('FSM2', stateMachine2))))
, x14B0_(0.f)
, x14B4_frustrationRadius(frustrationRadius)
, x14B8_frustrationTimer(frustrationTimer)
, x14BC_deadTime(0.f)
, x14C0_flags(0)
, x14C4_headEye(kInvalidUniqueId)
, x14C6_tailEye(kInvalidUniqueId)
, x14C8_(0)
, x14CC_(-1)
, x14D0_(-1)
, x14D4_(0)
, x14D8_charge(chargeMin, chargeMax, chargeImpulseH, chargeImpulseV)
, x150C_cycle(0.f)
, x1510_cycleRate(0.f)
, x1514_(0)
, x1518_bodyCmdFirst(-1)
, x151C_bodyCmdLast(13)
, x1520_flags(0)
, x1524_fadeTarget(0.f)
, x1528_spit(projectile, spitDamage, visorEffect, spitUnknown, spitMin, spitMax)
, x15AC_toss(tossDamage, tossImpulseH, tossImpulseV)
, x1608_residuePos(CVector3f::Zero())
, x1614_residuePosHighHealth(CVector3f::Zero())
, x1620_melee(meleeDamage, meleeImpulseH, meleeImpulseV)
, x164C_(unknownE593)
, x1650_lurkMin(lurkMin)
, x1654_lurkMax(lurkMax)
, x1658_(100.f)
, x165C_(0.f)
, x1660_(-1000.f)
, x1664_(150.f)
, x1668_flags(0)
, x166C_(100000)
, x181C_(0.f)
, x1820_(0.f)
, x1824_(0.f)
, x1828_sfx(walkSfx, walkVocalSfx, meleeSfx, eyeKilledSfx, bombBounceSfx, bombExplodeSfx)
, x1848_(kInvalidUniqueId)
, x184A_(kInvalidUniqueId)
, x184C_(0)
, x1850_(1)
, x1854_(-1000.f)
, x1858_search2(MakeSearch(pInfo, 1))
, x1944_bombs(bombFx, bombExplosionFx, bombDamage)
, x1980_bombDropRate(bombDropRate)
, x1984_bombDropTimer(0.f)
, x1988_(-1000.f)
, x198C_(CVector3f::Zero())
, x1998_(kInvalidUniqueId)
, x199A_(0)
, x199C_(CVector3f::Zero())
, x19A8_bombPhase(unknown547f, unknownEfef)
, x19D0_glow(eyeGlow, part, part8b2a)
, x19F0_(CVector3f::Zero())
, x19FC_(-1000.f)
, x1A00_(0.f)
, x1A04_(0.f)
, x1A08_(0.f)
, x1A0C_bounds(CAABox::MakeNullBox())
, x1A24_(-1000.f) {
  x8C4_phases[0] = phase0;
  x8C4_phases[1] = phase1;
  x8C4_phases[2] = phase2;
  x8C4_phases[3] = phase3;
  x8C4_phases[4] = phase4;
  x1770_pincers.x0_left = pincerL;
  x1770_pincers.x4_right = pincerR;
  x1770_pincers.x8_scale = pincerScale;
  FinishConstruction();
}

// @ fn_56_1223C
CSandworm::~CSandworm() {}

// @ fn_56_128B0
void CSandworm::FinishConstruction() {
  SetDrawShadow(false);
  AnimationData()->SetKeepJSPose(true);
  CreateProjectedShadow();
  for (int i = 0; i < 12; ++i) {
    x131C_[i] = 0.f;
  }

  const CVector3f modelScale = GetModelData()->GetScale();
  if (x1770_pincers.x0_left != kInvalidAssetId) {
    x1770_pincers.xc_leftModel =
        CModelData(CStaticRes(x1770_pincers.x0_left, x1770_pincers.x8_scale * modelScale));
  }
  if (x1770_pincers.x4_right != kInvalidAssetId) {
    x1770_pincers.x5c_rightModel =
        CModelData(CStaticRes(x1770_pincers.x4_right, x1770_pincers.x8_scale * modelScale));
  }

  ResetFade();
  KnockBackController().EnableAllAnimReactions(false);
  KnockBackController().EnableKnockBackPhysics(false);
  // Retail also clears bit 0x10 of the knock-back manager byte at this+0x681 (burn).
  KnockBackController().EnableBurn(false);

  for (int i = 0; i < 13; ++i) {
    x970_segIds.push_back(GetSegLocatorId(i));
  }

  x14D8_charge.x14_impulseH = rstl::min_val(x14D8_charge.x14_impulseH, 50.f);
  x14D8_charge.x18_impulseV = rstl::min_val(x14D8_charge.x18_impulseV, 50.f);
  x15AC_toss.x4c_impulseH = rstl::min_val(x15AC_toss.x4c_impulseH, 50.f);
  x15AC_toss.x50_impulseV = rstl::min_val(x15AC_toss.x50_impulseV, 50.f);
  x1620_melee.x24_impulseH = rstl::min_val(x1620_melee.x24_impulseH, 50.f);
  x1620_melee.x28_impulseV = rstl::min_val(x1620_melee.x28_impulseV, 50.f);
}

// @ fn_56_121D8
void CSandworm::ResetFade() {
  for (int i = 0; i < 13; ++i) {
    x12E8_fade[i] = 0.f;
  }
}

namespace {

// Retail fn_56_D548 hands these tables (data_1C0, data_524, data_6A0) to the FSM2 dispatcher; the
// order is the tables'. Own members are cast as PortCSpacePirate.cpp does it.
#define SW_TRIGGER(name)                                                                           \
  { #name, reinterpret_cast< CPatterned::StateMachine::TriggerFunc >(&CSandworm::name) }
#define SW_STATE(name)                                                                             \
  { #name, reinterpret_cast< CPatterned::StateMachine::StateFunc >(&CSandworm::name) }

CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    SW_TRIGGER(Activated),          SW_TRIGGER(BeginUnderground),
    SW_TRIGGER(CanAttack),          SW_TRIGGER(CanCharge),
    SW_TRIGGER(CanDescend),         SW_TRIGGER(CanGrabMorphball),
    SW_TRIGGER(CanSpitAgain),       SW_TRIGGER(CanMeleeAgain),
    SW_TRIGGER(ChargeOver),         SW_TRIGGER(DoneLurking),
    SW_TRIGGER(DoneStraightening),  SW_TRIGGER(DoneSulking),
    SW_TRIGGER(FacingPlayer),       SW_TRIGGER(FacingPlayerForCharge),
    SW_TRIGGER(ForceUnderground),   SW_TRIGGER(GrabAttackFinished),
    SW_TRIGGER(HitDuringWindUp),    SW_TRIGGER(InAttackRange),
    SW_TRIGGER(InChargeRange),      SW_TRIGGER(InSpitRange),
    SW_TRIGGER(MeleeAttackRange),   SW_TRIGGER(OneEyeKilled),
    SW_TRIGGER(PickedBombFountain), SW_TRIGGER(PickedBombToss),
    SW_TRIGGER(PickedBombSpread),   SW_TRIGGER(PickedSpitAttack),
    SW_TRIGGER(PlayerHiding),       SW_TRIGGER(PlayerIsMorphball),
    SW_TRIGGER(PlayerIsOnPathMesh), SW_TRIGGER(PlayerReachable),
    SW_TRIGGER(Primed),             SW_TRIGGER(ReadyToCharge),
    SW_TRIGGER(SequenceAttack),     SW_TRIGGER(ShouldSulk),
    SW_TRIGGER(SnatchStarted),      SW_TRIGGER(SnatchEnded),
    SW_TRIGGER(SpitAngleOK),
};

CPatterned::StateMachine::SStateFunction skStates[] = {
    SW_STATE(BombFountainAttack), SW_STATE(BombSpreadAttack), SW_STATE(BombTossAttack),
    SW_STATE(ChargePlayer),       SW_STATE(ChargeWindUp),     SW_STATE(Deactivate),
    SW_STATE(Dead),               SW_STATE(Descend),          SW_STATE(EyeKilledReaction),
    SW_STATE(GrabAttack),         SW_STATE(LurkUnderground),  SW_STATE(MeleeAttack),
    SW_STATE(Null),               SW_STATE(Patrol),           SW_STATE(PickMediumAttack),
    SW_STATE(Prime),              SW_STATE(Pursue),           SW_STATE(Rise),
    SW_STATE(Snatch),             SW_STATE(SpitAttack),       SW_STATE(StopGrabAttack),
    SW_STATE(Straighten),         SW_STATE(Sulk),
};

CPatterned::StateMachine::SCodeFunction skCodeFuncs[] = {
    {"Interrupt", reinterpret_cast< CPatterned::StateMachine::CodeFunc >(&CSandworm::Interrupt)},
};

#undef SW_TRIGGER
#undef SW_STATE

} // namespace

// @ fn_56_D548
void CSandworm::InitStateMachine(CStateManager& mgr) {
  InitializeStateMachine(mgr);
  mStateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  mStateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  mStateMachine->SetCodeFunctions(skCodeFuncs, ARRAY_SIZE(skCodeFuncs));
}

// @ fn_56_110C0
void CSandworm::Think(float dt, CStateManager& mgr) {
  x8C0_flags &= ~0x20;
  if (!mStateMachine->HasState()) {
    InitStateMachine(mgr);
    return;
  }
  if ((x15AC_toss.x58_flags & 0x80) != 0) {
    TeleportGrabbedPlayer(mgr);
  }
  UpdateEyes(mgr, dt);
  if (!GetActive()) {
    return;
  }
  if (x998_collisionActors.get() == nullptr) {
    CreateCollisionActorManager(mgr);
  }
  UpdateCollisionActors(mgr, dt);
  if (!GetAlive()) {
    x14BC_deadTime += dt;
    DecayX181C(dt);
    UpdateFade(dt);
    CPatterned::Think(dt, mgr);
    return;
  }

  x8B0_activeTime += dt;
  CPatterned::Think(dt, mgr);
  UpdatePose();
  UpdateTimers(mgr, dt);
  DecayX1350(dt);
  UpdateFade(dt);
  UpdateContactDamage(mgr, dt);
  DecayX181C(dt);
  UpdateColorLerp(dt);
  DetectIncomingProjectiles(mgr);
  UpdateLoopedSfx(mgr);
  if ((x8C0_flags & 0x10) != 0) {
    if (mIngPossessionBlend > 0.99f) {
      UpdateBossParams(mgr);
    }
  } else if (x1770_pincers.x8_scale < 1.f) {
    float speed = 1.f;
    if (x134C_ + 0.8f > x8B0_activeTime) {
      speed = (x8B0_activeTime - x134C_) / 0.8f;
    }
    mSpeed = speed;
  }
}

// @ fn_56_3990
const SLdrSandwormStruct& CSandworm::GetPhaseParams() const {
  const float hp = GetHealthInfo()->GetHP();
  for (int i = 0; i < 4; ++i) {
    if (hp > x8C4_phases[i]->unknown_0x98106ee2) {
      return *x8C4_phases[i];
    }
  }
  return *x8C4_phases[4];
}

// @ fn_56_3A20
bool CSandworm::IsPossessedBombGuardian() const {
  return (x8C0_flags & 0x10) != 0 && IsIngPossessed() && !IsBeingSnatched();
}

// @ fn_56_A9F4
void CSandworm::SetBodyCmdRequest(int first, int last) {
  x1518_bodyCmdFirst = first;
  x151C_bodyCmdLast = last;
}

// @ fn_56_AA00
void CSandworm::ClearBodyCmdRequest() { SetBodyCmdRequest(-1, 13); }

// @ fn_56_AA28
CVector3f CSandworm::GetMoveTarget(const CStateManager& mgr) const {
  if ((x8C0_flags & 0x40) != 0) {
    return mWaypointNavigation.GetDestinationPosition();
  }
  return mgr.Player(0)->GetTranslation();
}

// @ fn_56_10AD8
void CSandworm::UpdateTimers(CStateManager& mgr, float dt) {
  x150C_cycle += dt;
  const float dist = (GetMoveTarget(mgr) - GetTranslation()).Magnitude();
  if (x1514_ == 1 || dist > 25.f) {
    x1510_cycleRate = 1.f;
  } else {
    x1510_cycleRate = dist / 25.f;
  }
  float period = x1510_cycleRate * (1.2f - 0.4f) + 0.4f;
  if (x1514_ == 1) {
    period *= 0.25f;
  }
  while (x150C_cycle > period) {
    x150C_cycle -= period;
  }
}

// @ fn_56_10BE8
void CSandworm::UpdateFade(float dt) {
  const float step = 3.f * dt;
  for (int i = 0; i < 13; ++i) {
    const float target = (i > x1518_bodyCmdFirst && i < x151C_bodyCmdLast) ? x1524_fadeTarget : 0.f;
    x12E8_fade[i] = ApproachValue(x12E8_fade[i], target, step);
  }
}

// @ fn_56_10CA4
float CSandworm::ApproachValue(float cur, float target, float step) {
  if (target < cur) {
    const float next = cur - step;
    return next < target ? target : next;
  }
  const float next = cur + step;
  return next > target ? target : next;
}

// @ fn_56_10CD4
void CSandworm::DecayX181C(float dt) {
  const float step = 40.f * dt;
  x181C_ -= step;
  if (x181C_ < 0.f) {
    x181C_ = 0.f;
  }
  x1820_ -= step;
  if (x1820_ < 0.f) {
    x1820_ = 0.f;
  }
}

// @ fn_56_10D2C
void CSandworm::UpdateColorLerp(float dt) {
  if (x19A8_bombPhase.x20_ > 0.f) {
    x19A8_bombPhase.x20_ = rstl::max_val(0.f, x19A8_bombPhase.x20_ - dt);
    const float t = rstl::min_val(1.f, x19A8_bombPhase.x20_ / skDamageHitTime);
    const CColor color = CColor::Lerp(CColor::Black(), CColor::Yellow(), t);
    mColor = CColor(color.GetRedu8(), color.GetGreenu8(), color.GetBlueu8(), mColor.GetAlphau8());
  }
}

// @ fn_56_10E04
void CSandworm::DetectIncomingProjectiles(CStateManager& mgr) {
  const CVector3f pos = GetTranslation();
  const CAABox box(pos - CVector3f(100.f, 100.f, 100.f), pos + CVector3f(100.f, 100.f, 100.f));
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, box, CMaterialFilter::MakeInclude(CMaterialList(kMT_Projectile)),
                    this);
  for (int i = 0; i < nearList.size(); ++i) {
    if (CGameProjectile* proj = TCastToPtr< CGameProjectile >(mgr.ObjectById(nearList[i]))) {
      proj->SetHomingTurnRateScale(10.f);
    }
  }
}

// @ fn_56_10F90
void CSandworm::UpdateBossParams(CStateManager& mgr) {
  if ((x8C0_flags & 0x08) == 0) {
    SpawnResidue(mgr, 0);
    x8C0_flags |= 0x08;
  }
  if ((x8C0_flags & 0x04) == 0) {
    mgr.SetBossParams(GetUniqueId(), GetHealthInfo()->GetInitialHP(),
                      gpStringTable->GetStringIndex("BossBombGuardian"));
    x8C0_flags |= 0x04;
  }
  mSpeed = GetPhaseParams().moveSpeedMultiplier;
}

// @ fn_56_11FAC
void CSandworm::SpawnResidue(CStateManager& mgr, int) {
  const float zOffset = x1770_pincers.x8_scale * (0.5f * GetModelData()->GetScale().GetZ());
  const rstl::vector< SConnection >& conns = GetConnectionList();
  for (rstl::vector< SConnection >::const_iterator it = conns.begin(); it != conns.end(); ++it) {
    if (it->state != kSS_SpawnResidue || it->msg != kSM_Attach) {
      continue;
    }
    CActor* actor = TCastToPtr< CActor >(mgr.ObjectById(mgr.GetIdForScript(it->objId)));
    if (actor == nullptr) {
      continue;
    }
    if ((x8C0_flags & 0x10) != 0 &&
        GetHealthInfo()->GetHP() > 0.5f * GetHealthInfo()->GetInitialHP()) {
      actor->SetTranslation(x1614_residuePosHighHealth + CVector3f(0.f, 0.f, zOffset));
    } else {
      actor->SetTranslation(x1608_residuePos + CVector3f(0.f, 0.f, zOffset));
    }
    CSfxManager::AddEmitter(x1828_sfx.x6_eyeKilled, actor->GetTranslation(),
                            GetCurrentAreaId().Value(), true, false, CSfxManager::kMedPriority);
  }
  SendScriptMsgs(kSS_SpawnResidue, mgr, kInvalidUniqueId, static_cast< EScriptObjectMessage >(-1));
}

// @ fn_56_5C7C
void CSandworm::DecayX1350(float dt) {
  x1350_ -= dt;
  if (x1350_ < 0.f) {
    x1350_ = 0.f;
  }
}

// @ fn_56_5E94
void CSandworm::TeleportGrabbedPlayer(CStateManager& mgr) {
  CPlayer* player = mgr.Player(0);
  const CVector3f pos =
      x15AC_toss.x0_xf.GetTranslation() - 0.5f * x15AC_toss.x0_xf.GetColumn(kDZ);
  CTransform4f xf = player->GetTransform();
  xf.SetTranslation(pos);
  player->Teleport(xf, mgr, false);
}

// @ fn_56_14B58
void CSandworm::IssueTauntCmd(EStateMsg msg, pas::EAnimationState state, const CBCTauntCmd& cmd) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    BodyController()->CommandMgr().DeliverCmd(cmd);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), state)) {
      BodyController()->CommandMgr().DeliverCmd(cmd);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  }
}

// @ fn_56_14C24
void CSandworm::IssueGenerateCmd(EStateMsg msg, pas::EAnimationState state,
                                 const CBCGenerateCmd& cmd) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    BodyController()->CommandMgr().DeliverCmd(cmd);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), state)) {
      BodyController()->CommandMgr().DeliverCmd(cmd);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  }
}

// @ fn_56_14D40
void CSandworm::IssueMeleeAttackCmd(EStateMsg msg, pas::EAnimationState state,
                                    const CBCMeleeAttackCmd& cmd) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    BodyController()->CommandMgr().DeliverCmd(cmd);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), state)) {
      BodyController()->CommandMgr().DeliverCmd(cmd);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  }
}

// Radar callbacks.

// @ fn_56_5910
int CSandworm::GetRadarPointCount() const { return (GetActive() && GetAlive()) ? 13 : 0; }

// @ fn_56_5938
CVector3f CSandworm::GetRadarPointPosition(int index) const {
  if (index >= x1210_.size()) {
    return GetLocatorPosition(index);
  }
  return GetSegmentPosition(index);
}

// Virtual overrides.

// @ fn_56_14A74
rstl::optional_object< CAABox > CSandworm::GetTouchBounds() const { return GetBoundingBox(); }

// @ fn_56_14ADC
uchar CSandworm::GetModelAlphau8(const CStateManager&) const { return mColor.GetAlphau8(); }

// @ fn_56_6790
CProjectileInfo* CSandworm::ProjectileInfo() { return &x1528_spit.x0_info; }

// @ fn_56_14A64
CPathFindSearch* CSandworm::GetSearchPath() { return &x7C4_search; }

// fn_56_14AE4 (ported, not exercised yet)
// Dead slot: CIngSpotPathFindNavigation is the only reader, and the sandworm navigates with
// mWaypointNavigation, so nothing asks for a point search path.
CPathFindPointSearch* CSandworm::GetPointSearchPath() { return nullptr; }

// fn_56_14AEC (ported, not exercised yet)
// Dead slot: its only caller is CGameProjectile::CanCollideWithGameObject (CGameProjectile.cpp:141)
// and a projectile's near list never holds the worm itself. Traced with a probe at
// CGameProjectile::CanCollideWith over beam bursts through both area-2 worms: 29 distinct uids, all
// patterned=0 — the triggers (0488, 052c) and the worms' own collision actors — so the projectile
// path never reaches the pattern.
CRagDoll* CSandworm::GetRagDoll() const { return nullptr; }

// @ fn_56_30F8
bool CSandworm::CanBeIngPossessed(CStateManager& mgr) const {
  if ((x8C0_flags & 0x10) == 0) {
    return false;
  }
  return CPatterned::CanBeIngPossessed(mgr);
}

// @ fn_56_30F0
bool CSandworm::CanBeUnPossessed(CStateManager&) const { return false; }

// @ fn_56_14AF4
TUniqueId CSandworm::GetAttackTarget() const { return kInvalidUniqueId; }

// @ fn_56_14B04
bool CSandworm::IsOnGround() const { return mOnGround; }

// fn_56_14B10 (ported, not exercised yet)
// Dead slot: only CBSJump and CBSHurled read it, and neither worm's body database has the jump or
// hurled state (probed on 03_sand_temple L2: HasBodyState(kAS_Jump)=0, kAS_Hurled=0 every sample).
float CSandworm::GetGravityConstant() const { return kDefaultGravityAccel; }

// fn_56_14B1C (ported, not exercised yet)
// Data-gated: only CPatterned::CreateXDamageParticles (CPatterned.cpp:467) reads it, and that runs
// only on a kFU_ExplodeDeath follow-up. The worm's knock-back rules never select one: a probe at
// CKnockBackMgr::KnockBack over six weapon modes (power, dark, light, missile, bomb, power bomb)
// saw followups None, BurnPhase, BurnDeath and LaggedBurnDeath — never 7 (ExplodeDeath).
const rstl::optional_object< TCachedToken< CGenDescription > >&
CSandworm::GetDeathExplosionParticle() const {
  return mDeathExplosionParticle;
}

// @ fn_56_3180
CVector3f CSandworm::GetIngSnatchingNormal(float) const {
  const CVector3f delta = x1394_snatchStart - x13A0_snatchEnd;
  if (!delta.CanBeNormalized()) {
    // this+0x28/0x38/0x48: the transform's second column.
    return GetTransform().GetColumn(kDY);
  }
  return delta.AsNormalized();
}

// @ fn_56_312C
CVector3f CSandworm::GetIngSnatchingPoint(float t) const {
  const float t3 = t * (t * t);
  return (1.f - t3) * x1394_snatchStart + t3 * x13A0_snatchEnd;
}

// @ fn_56_9860
float CSandworm::GetIngSnatchingModelOverlapSize() const { return 6.f; }

// @ fn_56_11CE8
void CSandworm::TakeDamage(const CVector3f&, float) { mDamageCooldownTimer = skDamageHitTime; }

// fn_56_14B24 (ported, not exercised yet)
// Dead slot, same trace as GetRagDoll above: CGameProjectile::CanCollideWithGameObject (:161) is the
// only caller and it never sees the worm. Morph-ball bombs are CWeapon, not CGameProjectile, so a
// bomb does not reach it either.
bool CSandworm::CanBeShot(const CStateManager&, int) { return true; }

// @ fn_56_14B2C
bool CSandworm::IsListening() const { return false; }

// fn_56_14B34 (ported, not exercised yet)
// Dead slot: CStateManager::InformListeners only walks kOL_ListeningAi, which admits a patterned
// only while IsListening() is true; fn_56_14B2C returns false, so Listen is never called.
bool CSandworm::Listen(CStateManager&, const CVector3f&, EListenNoiseType) { return false; }

// fn_56_14B3C (ported, not exercised yet)
// Dead slot: the only caller is CScriptTeamAiMgr::PositionTeam (position mode 0), and an actor
// joins a team only through an ACTV/PLAY connection to an MRLY. No area that spawns a CSandworm
// has one: the WORM and MRLY connection lists of 01_sand_entrance, 03_sand_temple and
// 04_sand_mining hold no PLAY link into a worm.
CVector3f CSandworm::GetOrigin(const CStateManager&, const CTeamAiRole&, const CVector3f&) const {
  return GetTranslation();
}

// fn_56_14A6C (ported, not exercised yet)
// Dead slot: not virtual and nothing in the tree calls it; the header keeps it for the REL layout.
int CSandworm::Unknown23C() const { return 0; }

// Loader and REL entry points.

// Retail fn_56_1478: SLdrSandworm's constructor, the loader's defaults (the call at fn_56_DC
// 0xFC, on the loader's stack object). The generated header's inline constructor stores the same
// floats, damage infos and asset ids, except the sound ids: the guest's default for every sound id
// is -1 (no sound), the header's 0. Those -1 stores are written over it here. The generated struct
// is 0xC bytes larger than the guest's from the actor-parameters block on, so the guest offsets
// below are the port's +0xC. The `li r0,-0x1` at 0x000014A0 feeds eight stores: walkSound 0x300
// (0x000014A8), walkVocalSound 0x304 (0x000014AC), meleeAttackSound 0x308 (0x000014B0),
// eyeKilledSound 0x30c (0x000014B4), then guest 0x310/0x314/0x320/0x330 (0x000014B8/0x000014BC/
// 0x000014C0/0x000014C4), which are CAssetIds in the port (pincerL, pincerR, spitAttackVisorEffect,
// projectile), already kInvalidAssetId from the header's initializer list. The five stores of the
// `li r0,-0x1` at 0x000014DC (guest 0x394..0x3a4, 0x000014E4..0x000014F4) are the five CAssetIds
// eyeGlow, pART, pART_0x8b2a15ee, ingBossBombFX and ingBossBombExplosionFX, also already set. The
// pair the `li r0,-0x1` at 0x000014FC feeds are the last two sound ids: bombBounceSound 0x3bc
// (0x00001504) and bombExplodeSound 0x3c0 (0x00001508). A property in the instance's data
// overwrites any of them.
static void x1478_SoundDefaults(SLdrSandworm& sldr) {
  sldr.walkSound = -1;        // 0x000014A8 (0x300)
  sldr.walkVocalSound = -1;   // 0x000014AC (0x304)
  sldr.meleeAttackSound = -1; // 0x000014B0 (0x308)
  sldr.eyeKilledSound = -1;   // 0x000014B4 (0x30c)
  sldr.bombBounceSound = -1;  // 0x00001504 (0x3bc)
  sldr.bombExplodeSound = -1; // 0x00001508 (0x3c0)
}

// @ fn_56_DC
static CEntity* PortLoadSandworm(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrSandworm sldrThis;
  x1478_SoundDefaults(sldrThis);  // fn_56_1478's -1 sound ids (0xFC)
#include "MetroidPrime/ScriptLoader/SLdrSandworm.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }

#define SW_SFX(value)                                                                              \
  ((value) == -1 ? CSfxManager::kInternalInvalidSfxId : static_cast< ushort >(value))

  const bool flag = (sldrThis.unknown_0x06dee4c5 & 1) != 0;
  if (sldrThis.canLinkTransfer == 1) {
    return rs_new CSandworm(
        mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
        LdrToEntityInfo(info, sldrThis.editorProperties),
        LdrToTransform4f(sldrThis.editorProperties), *modelData,
        LdrToPatternedInfo(sldrThis.patterned, &sldrThis.ingPossessionData),
        sldrThis.patterned.stateMachine2, sldrThis.pincerScale, sldrThis.spitAttackMinRange,
        sldrThis.spitAttackMaxRange, sldrThis.unknown_0x61f75902, sldrThis.chargeRangeMin,
        sldrThis.chargeRangeMax, sldrThis.chargeImpulseHorizontal,
        sldrThis.chargeImpulseVertical, flag, sldrThis.pincerL, sldrThis.pincerR,
        SW_SFX(sldrThis.walkSound), SW_SFX(sldrThis.walkVocalSound),
        SW_SFX(sldrThis.meleeAttackSound), SW_SFX(sldrThis.eyeKilledSound),
        SW_SFX(sldrThis.bombBounceSound), SW_SFX(sldrThis.bombExplodeSound),
        sldrThis.spitAttackVisorEffect, sldrThis.morphballTossImpulseHorizontal,
        sldrThis.morphballTossImpulseVertical, sldrThis.meleeImpulseHorizontal,
        sldrThis.meleeImpulseVertical, sldrThis.unknown_0xe593f1c6,
        sldrThis.lurkUndergroundTimeMin, sldrThis.lurkUndergroundTimeMax,
        sldrThis.pursuitFrustrationRadius, sldrThis.pursuitFrustrationTimer, sldrThis.projectile,
        LdrToDamageInfo(sldrThis.projectileDamage), LdrToDamageInfo(sldrThis.morphballTossDamage),
        LdrToDamageInfo(sldrThis.pincerSwipeDamage), sldrThis.eyeGlow, sldrThis.pART,
        sldrThis.pART_0x8b2a15ee, sldrThis.ingBossBombFX, sldrThis.ingBossBombExplosionFX,
        LdrToDamageInfo(sldrThis.ingBossBombDamage), sldrThis.ingBossBombDropRate,
        sldrThis.unknown_0x547f9400, sldrThis.unknown_0xefef7b45, sldrThis.sandwormStruct,
        sldrThis.sandwormStruct_0xce246628, sldrThis.sandwormStruct_0x55578cfc,
        sldrThis.sandwormStruct_0x23ee1452, sldrThis.sandwormStruct_0xb89dfe86,
        LdrToActorParameters(sldrThis.actorInformation));
  }
  return rs_new CSandworm(
      mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
      LdrToEntityInfo(info, sldrThis.editorProperties),
      LdrToTransform4f(sldrThis.editorProperties), *modelData,
      LdrToPatternedInfo(sldrThis.patterned, &sldrThis.ingPossessionData),
      sldrThis.patterned.stateMachine2, sldrThis.pincerScale, sldrThis.spitAttackMinRange,
      sldrThis.spitAttackMaxRange, sldrThis.unknown_0x61f75902, sldrThis.chargeRangeMin,
      sldrThis.chargeRangeMax, sldrThis.chargeImpulseHorizontal, sldrThis.chargeImpulseVertical,
      flag, sldrThis.pincerL, sldrThis.pincerR, SW_SFX(sldrThis.walkSound),
      SW_SFX(sldrThis.walkVocalSound), SW_SFX(sldrThis.meleeAttackSound),
      SW_SFX(sldrThis.eyeKilledSound), sldrThis.spitAttackVisorEffect,
      sldrThis.morphballTossImpulseHorizontal, sldrThis.morphballTossImpulseVertical,
      sldrThis.meleeImpulseHorizontal, sldrThis.meleeImpulseVertical, sldrThis.unknown_0xe593f1c6,
      sldrThis.lurkUndergroundTimeMin, sldrThis.lurkUndergroundTimeMax,
      sldrThis.pursuitFrustrationRadius, sldrThis.pursuitFrustrationTimer, sldrThis.projectile,
      LdrToDamageInfo(sldrThis.projectileDamage), LdrToDamageInfo(sldrThis.morphballTossDamage),
      LdrToDamageInfo(sldrThis.pincerSwipeDamage), sldrThis.eyeGlow,
      LdrToActorParameters(sldrThis.actorInformation));

#undef SW_SFX
}

static SSandworm_FuncPtrs REL_loader_Sandworm;

// @ fn_56_70
// @ fn_56_50
void RELMain() {
  REL_loader_Sandworm.mLoadSandworm = PortLoadSandworm;
  REL_loader_Sandworm.mGetRadarPointPosition = &CSandworm::GetRadarPointPosition;
  REL_loader_Sandworm.mGetRadarPointCount = &CSandworm::GetRadarPointCount;
  SetSSandworm_FuncPtrs(&REL_loader_Sandworm);
}

// @ fn_56_2C
void RELExit() { SetSSandworm_FuncPtrs(nullptr); }
