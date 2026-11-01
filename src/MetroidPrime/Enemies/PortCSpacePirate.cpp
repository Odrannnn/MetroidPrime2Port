// Host body for the SpacePirate REL (module 72), milestones 1a-2: the actors load, stand and render;
// the 39 triggers / 37 states are declared and installed (PortCSpacePirate{Triggers,States}.cpp).
// The analysis is docs/module-analysis/SpacePirate.md. Every vtable override below forwards to
// CPatterned behind a once-per-method StandIn log; the retail function it stands in for is named
// on each. There is no combat AI yet - CPatterned's default state machine drives the pirate.
#include "MetroidPrime/Enemies/CSpacePirate.hpp"

#include "Kyoto/CTimeProvider.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Math/CVector2f.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "MetroidPrime/CActorLights.hpp"
#include "MetroidPrime/CPirateEchoEmitter.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CKnockBackInfo.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/CActorModelParticles.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Enemies/CTeamAiRole.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/CRagDoll.hpp"
#include "MetroidPrime/Enemies/CPirateRagDoll.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWater.hpp"
#include "MetroidPrime/ScriptObjects/CScriptCoverPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTrigger.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTargetingPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTeamAiMgr.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/Weapons/CEnergyProjectile.hpp"
#include "MetroidPrime/Weapons/CGameProjectile.hpp"
#include "MetroidPrime/Weapons/CImpactVisorEffect.hpp"
#include "MetroidPrime/Weapons/CWeapon.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "MetroidPrime/Enemies/CPatternedInfo.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/ScriptLoader/SLdrSpacePirate.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"

#include <float.h>
#include <stdio.h>
#include <string.h>

namespace {

static const SBurst skBursts0[] = {
    {20, {3, 4, 5, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {20, {2, 3, 4, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {20, {6, 5, 4, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {20, {1, 2, 3, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {20, {7, 6, 5, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, 0.0f, 0.0f},
};

static const SBurst skBursts1[] = {
    {15, {5, 3, 2, 1, -1, 0, 0, 0}, 0.1f, 0.05f},
    {20, {1, 2, 3, 4, -1, 0, 0, 0}, 0.1f, 0.05f},
    {20, {7, 6, 5, 4, -1, 0, 0, 0}, 0.1f, 0.05f},
    {15, {3, 4, 5, 6, -1, 0, 0, 0}, 0.1f, 0.05f},
    {15, {6, 5, 4, 3, -1, 0, 0, 0}, 0.1f, 0.05f},
    {15, {2, 3, 4, 5, -1, 0, 0, 0}, 0.1f, 0.05f},
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, 0.0f, 0.0f},
};

static const SBurst skBursts2[] = {
    {40, {1, 2, 3, 4, 5, 6, -1, 0}, 0.1f, 0.05f},
    {40, {7, 6, 5, 4, 3, 2, -1, 0}, 0.1f, 0.05f},
    {10, {2, 3, 4, 5, 4, 3, -1, 0}, 0.1f, 0.05f},
    {10, {6, 5, 4, 3, 4, 5, -1, 0}, 0.1f, 0.05f},
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, 0.0f, 0.0f},
};

static const SBurst skBursts3[] = {
    {20, {16, 4, -1, 0, 0, 0, 0, 0}, 0.1f, 0.05f},
    {40, {5, 7, -1, 0, 0, 0, 0, 0}, 0.1f, 0.05f},
    {40, {1, 10, -1, 0, 0, 0, 0, 0}, 0.1f, 0.05f},
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, 0.0f, 0.0f},
};

static const SBurst skBursts4[] = {
    {15, {16, 1, 3, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {20, {3, 4, 6, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {25, {7, 5, 4, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {25, {2, 6, 4, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {15, {7, 5, 3, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, 0.0f, 0.0f},
};

static const SBurst skBursts5[] = {
    {35, {7, 13, -1, 0, 0, 0, 0, 0}, 0.1f, 0.05f},
    {35, {9, 1, -1, 0, 0, 0, 0, 0}, 0.1f, 0.05f},
    {30, {16, 12, -1, 0, 0, 0, 0, 0}, 0.1f, 0.05f},
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, 0.0f, 0.0f},
};

static const SBurst skBursts6[] = {
    {10, {16, 15, 13, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {20, {13, 12, 10, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {30, {9, 11, 12, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {30, {14, 10, 12, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {10, {9, 11, 13, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, 0.0f, 0.0f},
};

static const SBurst skBursts7[] = {
    {26, {16, 8, 11, 14, -1, 0, 0, 0}, 0.1f, 0.05f},
    {26, {16, 13, 11, 12, -1, 0, 0, 0}, 0.1f, 0.05f},
    {16, {9, 11, 13, 10, -1, 0, 0, 0}, 0.1f, 0.05f},
    {16, {14, 13, 12, 11, -1, 0, 0, 0}, 0.1f, 0.05f},
    {8, {10, 11, 12, 13, -1, 0, 0, 0}, 0.1f, 0.05f},
    {8, {6, 8, 11, 13, -1, 0, 0, 0}, 0.1f, 0.05f},
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, 0.0f, 0.0f},
};

static const SBurst skBursts8[] = {
    {40, {1, 16, 14, 12, 10, 11, -1, 0}, 0.1f, 0.05f},
    {40, {9, 11, 12, 13, 11, 7, -1, 0}, 0.1f, 0.05f},
    {10, {8, 10, 11, 12, 13, 12, -1, 0}, 0.1f, 0.05f},
    {10, {15, 13, 12, 10, 12, 9, -1, 0}, 0.1f, 0.05f},
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, 0.0f, 0.0f},
};

static const SBurst skBursts9[] = {
    {40, {7, 13, -1, 0, 0, 0, 0, 0}, 0.1f, 0.05f},
    {40, {9, 1, -1, 0, 0, 0, 0, 0}, 0.1f, 0.05f},
    {20, {16, 12, -1, 0, 0, 0, 0, 0}, 0.1f, 0.05f},
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, 0.0f, 0.0f},
};

static const SBurst skBursts10[] = {
    {30, {9, 11, 13, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {10, {13, 12, 10, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {15, {9, 11, 12, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {15, {14, 10, 12, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {30, {16, 15, 13, -1, 0, 0, 0, 0}, 0.1f, 0.05f},
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, 0.0f, 0.0f},
};

static const SBurst skBursts11[] = {
    {35, {7, 13, -1, 0, 0, 0, 0, 0}, 0.1f, 0.05f},
    {35, {9, 1, -1, 0, 0, 0, 0, 0}, 0.1f, 0.05f},
    {30, {16, 12, -1, 0, 0, 0, 0, 0}, 0.1f, 0.05f},
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, 0.0f, 0.0f},
};

// fn_72_854: the table CBurstFire is built from; its order is retail's.
static const SBurst* skBursts[] = {skBursts0, skBursts1, skBursts2, skBursts3, skBursts4, skBursts5,
                                   skBursts6, skBursts7, skBursts8, skBursts9, skBursts10,
                                   skBursts11, nullptr};

} // namespace

// Reports a stand-in the first time it runs, so a log lists what is still a shell. Shared with
// PortCSpacePirateStates.cpp and PortCSpacePirateTriggers.cpp.
void PortSpacePirateStandIn(const char* what) {
  static const char* sSeen[256] = {};
  static int sCount = 0;
  for (int i = 0; i < sCount; ++i) {
    if (sSeen[i] == what || strcmp(sSeen[i], what) == 0) {
      return;
    }
  }
  if (sCount < 256) {
    sSeen[sCount++] = what;
  }
  fprintf(stderr, "[spacepirate] %s is a port stand-in (see PortCSpacePirate.cpp)\n", what);
}

namespace {
void StandIn(const char* what) { PortSpacePirateStandIn(what); }
} // namespace

// Retail keeps the pirates that are in a charge/melee attack in this module-level list (bss_40);
// DoubleSnap's Activate drops the pirate from it.
rstl::list< TUniqueId > CSpacePirate::mChargePlayerList;

// @ fn_72_F49C
CSpacePirate::CSpacePirate(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                           const CTransform4f& xf, const CModelData& modelData,
                           const CActorParameters& actParms, const CPatternedInfo& pInfo,
                           const CSpacePirateData& data)
// EPatternedAI 0x35, flavor 0, movement 0 (ground), collider 1, body 1 (bipedal): the retail
// constructor's arguments (fn_72_F49C).
: CPatterned(static_cast< EPatternedAI >(0x35), uid, name, kFT_Zero, info, xf, modelData, pInfo,
             kMT_Ground, kCT_One, kBT_BiPedal, actParms)
, mData(data) // fn_72_106D4
, x900_coverPoint(kInvalidUniqueId)
, x902_previousCoverPoint(kInvalidUniqueId)
, mPathFindSearch(nullptr, 1, pInfo.GetPathfindingIndex(), 1.f, 1.f, 0,
                  static_cast< CPFRegion::ERootPosition >(0))
, mHeadSegId(GetModelData()->GetAnimationData()->GetLocatorSegId(rstl::string_l("Head_1")))
, mBoneTracking(*GetModelData()->GetAnimationData(), rstl::string_l("Head_1"), 1.2217305f,
                3.1415927f, kBTF_None)
, mAttachedActor(kInvalidUniqueId)
, mAttackRemTime(1.f)
, mAttackTarget(kInvalidUniqueId)
, mBurstFire(skBursts, static_cast< int >(data.mFirstBurstCount))
, xb14_jumpPoint(kInvalidUniqueId)
, xb30_ragDoll(nullptr)
, xba0_teamAiMgrId(kInvalidUniqueId)
, mPortalPlane(0.f, CUnitVector3f(CVector3f::Forward())) {
  // Flag bits 0x8f4_24..0x8f6_24, unpacked from the data's flags word (0xF51C-0xF638); every other
  // bit of 0x8f6..0x8fa starts cleared (0xF63C-0xF838). The retail ctor skips two bits:
  // x8f6_31 (0xF680 clears 0x8f6 0x2, then 0xF684 starts on 0x8f7) and x8f9_29 (0xF7C8 clears
  // 0x8f9 0x2 after 0xF7BC's 0x8); the host heap isn't zeroed, so the port clears them too.
  const uint flags = data.mFlags;
  x8f4_24_pendingAmbush = (flags & 3) != 0;
  x8f4_25_ceilingAmbush = (flags & 0x2) != 0;
  x8f4_26_nonAggressive = (flags & 0x4) != 0;
  x8f4_27_melee = (flags & 0x8) != 0;
  x8f4_28_noShuffleCloseCheck = (flags & 0x10) != 0;
  x8f4_29_onlyAttackInRange = (flags & 0x20) != 0;
  x8f4_30_ = (flags & 0x40) != 0;
  x8f4_31_noKnockbackImpulseReset = (flags & 0x80) != 0;
  x8f5_24_noMeleeAttack = (flags & 0x200) != 0;
  x8f5_25_breakAttack = (flags & 0x400) != 0;
  x8f5_26_seated = (flags & 0x1000) != 0;
  x8f5_27_shadowPirate = (flags & 0x2000) != 0;
  x8f5_28_alertBeforeCloak = (flags & 0x4000) != 0;
  x8f5_29_noBreakDodge = (flags & 0x8000) != 0;
  x8f5_30_floatingCorpse = (flags & 0x10000) != 0;
  x8f5_31_ragdollNoAiCollision = (flags & 0x20000) != 0;
  x8f6_24_trooper = (flags & 0x40000) != 0;
  x8f6_25_hearNoise = false;
  x8f6_26_enableMeleeAttack = false;
  x8f6_27_ = false;
  x8f6_28_ = false;
  x8f6_29_enableRetreat = false;
  x8f6_30_shuffleClose = false;
  x8f6_31_inAttackState = false; // port: retail leaves this bit untouched (see above)
  x8f7_24_enablePatrol = false;
  mEnableAim = false;
  x8f7_26_hearPlayerFire = false;
  x8f7_27_inProjectilePath = false;
  x8f7_28_noPlayerLos = false;
  x8f7_29_inWallHang = false;
  x8f7_30_jumpVelSet = false;
  x8f7_31_prevInCineCam = false;
  x8f8_24_pendingFrenzyChance = false;
  x8f8_25_appliedBladeDamage = false;
  x8f8_26_alwaysAggressive = false;
  x8f8_27_coverCheck = false;
  x8f8_28_enableDodge = false;
  x8f8_29_noPlayerDodge = false;
  mAllEnergyDrained = false;
  x8f8_31_mayStartAttack = false;
  x8f9_24_ = false;
  x8f9_25_useJumpBackJump = false;
  x8f9_26_started = false;
  x8f9_27_inRange = false;
  x8f9_28_satUp = false;
  x8f9_29_enableBreakDodge = false; // port: retail leaves this bit untouched (see above)
  x8f9_30_closeMelee = false;
  x8f9_31_sentAttackMsg = false;
  x8fa_24_normalDodge = false;
  x8fa_25_ = false;
  x8fa_26_ = false;
  x8fa_27_ = false;
  x8fa_28_ = false;
  x8fa_29_ = false;
  x8fa_30_ = false;
  x8fa_31_ = false;

  x8fc_frenzyFrames = 0;
  x904_steeringSpeed = 1.f;
  x908_targetDelta = CVector3f::Forward();
  x914_coverPointRearDir = CVector3f::Zero();
  xa0c_unkTimer = 0.f; // port: the retail ctor never stores 0xa0c (Think counts it down)
  xa10_steeringDelayTimer = 0.f;
  xa14_ = 0;
  // 0xF89C lfs 0x58(r29) (r29 = pInfo), 0xF8B8 stfs 0xa18: CPatternedInfo +0x54 is the
  // CHealthInfo, whose +0x4 is the HP - the pirate's starting HP (ShouldFrenzy 3220 and the
  // Hurled chance read it).
  xa18_ = pInfo.GetHealthInfo().GetHP();
  xa1c_coverRange = 0.f;
  xa24_ = 0;
  xa28_taunt = -1;
  xa68_coverDir = -1;
  xa70_intoJumpDist = 1.f;
  xa74_eyeHeight = 2.f;
  xa78_ = 0.f;
  xa7c_timeNoPlayerLos = 0.f;
  xa80_cantSeePlayerCycleCounter = 0;
  xaf8_jumpHeight = 3.f;
  xafc_patrolDestPos = CVector3f::Zero();
  xb08_skidDir = -1;
  xb0c_strafeDelayTimer = 0.f;
  xb10_meleeSeverity = -1;
  xb18_dodgeDir = -1;
  xb1c_dodgeDist = 3.f;
  xb20_breakDodgeDist = 3.f;
  xb24_timeSinceHitByPlayer = FLT_MAX;
  xb28_lowHealthFrenzyTimer = FLT_MAX;
  xb2c_ragdollDelayTimer = 0.f;
  xb7c_cloakDelayTimer = 0.f;
  xb80_electricParticleTimer = 0.f;
  xb84_cloakStepTime = 0.f;
  xb88_shadowPirateAlpha = 0.5f;
  xb8c_minCloakAlpha = data.mCloakOpacity;
  xb90_maxCloakAlpha = data.mMaxCloakOpacity;
  xb94_dodgeDelayTimer = data.mBreakDodgeMinTime;
  xb98_aimDelayTimer = data.mGunTrackDelay;
  xb9c_ = 0.f;
  xba4_heldPositionX = 0.f;
  xba8_heldPositionY = 0.f;
  xbac_holdPositionTime = 0.f;
  xbb0_leashTimer = 0.f;
  xbb4_ = CVector3f::Zero();
  xbc0_playerIndex = -1;
  xbc4_ = CVector3f::Zero();
  xbd0_ = 0.f;
  xbfc_ = false;
  xc00_ = CSfxHandle::NullHandle();
  xc04_warpPoint = -1;
  xc58_ = 0;

  // 0xFB30: the launcher block (data+0xc8) builds the grenade launcher's model.
  InitLauncherModel(data);
  // 0xFB34-0xFB40: the data's projectile (+0x20) is not -1 (`addis 1` + `cmplwi 0xffff`).
  if (data.mProjectile != kInvalidAssetId) {
    // 0xFB4C CProjectileInfo(data+0x20, data+0x24), 0xFB58 assigned to 0xbd4, 0xFB6C Lock.
    mProjectileInfo = CProjectileInfo(data.mProjectile, data.mProjectileDamage);
    mProjectileInfo.data().Token().Lock();
  }
  mBurstFire.SetBurstType(1); // 0xFB78 stw 1, 0xa98

  const CVector3f scale = GetModelData()->GetScale();
  mBoneTracking.SetDisableTrackingDistance(25.f * scale.GetZ());

  const CAnimData* animData = GetModelData()->GetAnimationData();
  xa87_elbowSeg = animData->GetLocatorSegId(rstl::string_l("R_elbow"));
  xa88_wristSeg = animData->GetLocatorSegId(rstl::string_l("R_wrist"));
  xa89_swooshSeg = animData->GetLocatorSegId(rstl::string_l("Swoosh_LCTR"));
  xa86_gunSeg = animData->GetLocatorSegId(rstl::string_l("gun_LCTR"));
  xa8a_lHipSeg = animData->GetLocatorSegId(rstl::string_l("L_hip"));
  xa8b_rHipSeg = animData->GetLocatorSegId(rstl::string_l("R_hip"));
  xa8c_collarSeg = animData->GetLocatorSegId(rstl::string_l("Collar"));

  if (!x8f4_29_onlyAttackInRange) {
    const CPASAnimParmData jump(pas::kAS_Jump, CPASAnimParm::FromEnum(0), CPASAnimParm::FromEnum(0),
                                CPASAnimParm::FromEnum(0));
    xa70_intoJumpDist = scale.GetY() * GetAnimationDistance(jump);
    const CPASAnimParmData dodge(pas::kAS_Step, CPASAnimParm::FromEnum(3),
                                 CPASAnimParm::FromEnum(1));
    xb1c_dodgeDist = scale.GetX() * GetAnimationDistance(dodge);
    const CPASAnimParmData breakDodge(pas::kAS_Step, CPASAnimParm::FromEnum(3),
                                      CPASAnimParm::FromEnum(2));
    xb20_breakDodgeDist = scale.GetX() * GetAnimationDistance(breakDodge);
  } else {
    // 0xFF44-0xFF54: body controller +0x56c bit 0x80 set (its CBodyStateInfo's
    // mChangeLocoAtEndOfAnimOnly).
    BodyController()->BodyStateInfo().SetLocoAnimChangeAtEndOfAnimOnly(true);
  }

  const CAABox& baseBox = GetBaseBoundingBox();
  xa74_eyeHeight = 0.6f * (baseBox.GetMaxPoint().GetZ() - baseBox.GetMinPoint().GetZ());

  if (HasActorLights()) {
    ActorLights()->SetAmbienceGenerated(false);
  }
  // 0xFF98-0xFFA8: x682 |= 0x80. The knock-back controller is at 0x620 (DOL CPatterned ctor
  // 0x8007A214), so this is its +0x62 bit 0x80: CKnockBackMgr::mLocomotionDuringElectrocution.
  KnockBackController().SetLocomotionDuringElectrocution(true);
  // 0xFFA4 li r4,0x16; 0xFFB8 bne: only a body without state 0x16 forces melee (0xFFC4 0x8f4 0x10).
  if (!BodyController()->HasBodyState(pas::kAS_AdditiveAim)) {
    x8f4_27_melee = true;
  }
  // 0xFFCC-0xFFD4: pInfo+0x2a4 bit 0x80 (mIsEchoEmitter) set builds a CPirateEchoEmitter (new
  // 0x70 at 0x10008, ctor fn_802455CC at 0x1019C) at the actor's position (this+0x54) from the
  // rodata C38 segs below, then 0x101B0 SetEchoEmitter(true, emitter).
  const SEchoParameters& echo = pInfo.GetEchoParameters();
  if (echo.mIsEchoEmitter) {
    CPirateEchoEmitter* emitter = rs_new CPirateEchoEmitter(
        this, GetTranslation(), echo,
        animData->GetLocatorSegId(rstl::string_l("Head_1")),      // r7: C38+0x342
        animData->GetLocatorSegId(rstl::string_l("R_wing_LCTR")), // r8: C38+0x3a6
        animData->GetLocatorSegId(rstl::string_l("L_wing_LCTR")), // r9: C38+0x39a
        animData->GetLocatorSegId(rstl::string_l("gun_lctr")),    // r10: C38+0x391
        animData->GetLocatorSegId(rstl::string_l("Swoosh_LCTR")), // sp+8: C38+0x359
        animData->GetLocatorSegId(rstl::string_l("R_ankle")),     // sp+0xc: C38+0x389
        animData->GetLocatorSegId(rstl::string_l("L_ankle")));    // sp+0x10: C38+0x381
    SetEchoEmitter(true, emitter);
  }
  // 0x10224-0x1023C: a floating corpse (0x8f5 0x2) clears CActor 0x154 bit 0x40, the actor's
  // mHighlightedInDarkVisor (0x154: echo-emitter-enabled 0x80, highlighted 0x40, ...).
  if (x8f5_30_floatingCorpse) {
    SetHighlightedInDarkVisor(false);
  }
}

CSpacePirate::~CSpacePirate() {}

// A body state is only ever installed by CBodyController::Activate, and CPatterned::Think /
// ThinkAboutMove / Death / KnockBack all dereference the current state. Retail activates it from
// its own AcceptScriptMsg or Think (fn_72_E498 / fn_72_1980; the analysis does not say which), so
// the port activates on kSM_Create and, as the FlyingPirate does, again on demand in every entry
// point that can run first.
void CSpacePirate::EnsureBodyActive(CStateManager& mgr) {
  if (BodyController() != nullptr && !BodyController()->GetIsActive()) {
    BodyController()->Activate(mgr, pas::kAS_Invalid);
  }
}

// @ fn_72_E314
// A bare blr (0xE314): Dead's Activate (0x5C6C, flag 0) and AcceptScriptMsg's kSM_Activate when the
// corpse does not float (0xE91C, flag 1) call it; neither argument is read.
void CSpacePirate::EmptyE314(CStateManager&, bool) {}

// @ fn_72_E0D8 (AttachActorToPirate): the pirate takes hold of one actor at a time and
// refuses a second one. The ragdoll's flag byte at 0x118 is the "an actor is held" bit.
bool CSpacePirate::PortAttachActor(TUniqueId id) {
  if (mAttachedActor != kInvalidUniqueId) { // 0xE0E4 cmplw 0xa84 with kInvalidUniqueId
    return false;                            // 0xE118 li r3, 0
  }
  if (xb30_ragDoll.get() != nullptr) { // 0xE0EC-0xE0F4 cmplwi 0xb30, 0
    // 0xE0F8-0xE104: rlwimi r0,r5(1),6,25,25 sets the ragdoll's byte 0x118 0x40 (mActorAttached).
    xb30_ragDoll->SetActorAttached(true);
  }
  mAttachedActor = id; // 0xE108 lhz 0x0(r4), 0xE10C sth 0xa84
  return true;        // 0xE110 li r3, 1
}

// @ fn_72_E0AC (DetachActorFromPirate): the pirate lets go of the actor it was holding.
void CSpacePirate::PortDetachActor() {
  mAttachedActor = kInvalidUniqueId; // 0xE0B4 sth 0xa84
  if (xb30_ragDoll.get() != nullptr) { // 0xE0B8-0xE0C0 cmplwi 0xb30, 0; beqlr
    // 0xE0C4-0xE0D0: rlwimi r0,r3(0),6,25,25 clears the ragdoll's byte 0x118 0x40 (mActorAttached).
    xb30_ragDoll->SetActorAttached(false);
  }
}

// @ fn_72_24C4
void CSpacePirate::PreThink(float dt, CStateManager& mgr) {
  EnsureBodyActive(mgr);
  // 0x24E8-0x24FC: the head tracking pre-thinks unless a primed ragdoll (+0x70 0x40) is there.
  if (xb30_ragDoll.get() == nullptr || !xb30_ragDoll->IsPrimed()) {
    mBoneTracking.PreThink(*ModelData()->AnimationData()); // 0x250C
  }
  CPatterned::PreThink(dt, mgr);
}

namespace {
// MP2_DEBUG_SPACEPIRATE=1: one line per entry of the ported entry points (arg is a detail value).
void TraceCall(const CSpacePirate& self, const char* what, int arg) {
  static const bool sOn = getenv("MP2_DEBUG_SPACEPIRATE") != nullptr;
  if (sOn) {
    fprintf(stderr, "[spacepirate] %u %s %d\n", self.GetUniqueId().Value(), what, arg);
  }
}

// MP2_DEBUG_SPACEPIRATE=1: one line per state change of each pirate.
void TraceState(CSpacePirate& self, const char* name) {
  static const bool sOn = getenv("MP2_DEBUG_SPACEPIRATE") != nullptr;
  if (!sOn) {
    return;
  }
  static unsigned short sIds[256];
  static const char* sLast[256];
  static int sCount = 0;
  const unsigned short id = self.GetUniqueId().Value();
  int i = 0;
  while (i < sCount && sIds[i] != id) {
    ++i;
  }
  if (i == sCount) {
    if (sCount == 256) {
      return;
    }
    sIds[sCount] = id;
    sLast[sCount++] = nullptr;
  }
  if (sLast[i] == name || (sLast[i] && name && strcmp(sLast[i], name) == 0)) {
    return;
  }
  fprintf(stderr, "[spacepirate] %u state %s -> %s\n", (unsigned)id, sLast[i] ? sLast[i] : "(none)",
          name ? name : "(none)");
  sLast[i] = name;
}
} // namespace

static const float skSpacePirateRagDollRadii[14] = {
    0.45f, 0.52f, 0.35f, 0.1f,  0.15f, 0.35f, 0.1f,
    0.15f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f,
};  // lbl_72_rodata_B04 (0x38 bytes)
// @ fn_72_1980 (Think). The head: the echo bounds, the cinematic collision, the timers and the
// attack helpers (0x19A8-0x1C9C). Then the ragdoll (0xb30) picks the think: none, or one not yet
// primed on a sinking corpse, runs CPatterned's Think and the bone tracking (0x1CCC-0x1D00);
// a primed one, or any ragdoll on a floating corpse (0x8f5 0x02), runs only the actor's Think,
// the fade, the hit-damage timer and the Ing possession, and unfreezes the body (0x1D04-0x1D58).
// A ragdoll is primed from the actor (0x1D74-0x1DD0) or stepped (0x1DD8-0x1E94), and once it is
// over and still the pirate stops and starts its fade (0x1E98-0x1F04). The tail (0x1F08-0x2030)
// builds the ragdoll when the BecomeRagDoll delay (xb2c) runs out. Arms: SpacePirate.md section 14
// (natural runs, plus the forced ones of tools/cov/specs/spacepirate_ragdoll_force.py).
void CSpacePirate::Think(float dt, CStateManager& mgr) {
  if (!GetActive()) { // 0x19A8-0x19BC (0x20 0x80)
    return;
  }
  // 0x19C0-0x1A00: the echo emitter's bounds (this+0xc8, +0x14) collapse onto the translation.
  // Retail stores without a test; the host keeps one because the emitter exists only when the
  // pInfo's echo parameters ask for it (the ctor's 0xFFCC test), and a null store faults here.
  if (EchoEmitter() != nullptr) {
    EchoEmitter()->SetBounds(CAABox(GetTranslation(), GetTranslation()));
  }
  EnsureBodyActive(mgr); // 0x1A04-0x1A1C (0x594 0x40, Activate(mgr, -1))
  CBodyController* body = BodyController();
  // 0x1A20-0x1A44: not multiplayer and the camera manager (0x151c) in a cinematic.
  const bool inCineCam =
      !mgr.IsMultiplayer() && mgr.GetCameraManager(0)->IsInCinematicCamera();
  if (inCineCam && !x8f7_31_prevInCineCam) {
    SetCinematicCollision(mgr);
  } else if (!inCineCam && x8f7_31_prevInCineCam && !x8f5_31_ragdollNoAiCollision) {
    SetNonCinematicCollision(mgr);
  }
  x8f7_31_prevInCineCam = inCineCam;
  const float steeringSpeed = xa10_steeringDelayTimer != 0.f ? 0.f : x904_steeringSpeed;
  body->CommandMgr().SetSteeringSpeedRange(steeringSpeed, steeringSpeed);
  xa0c_unkTimer = CMath::Max(0.f, xa0c_unkTimer - dt);
  if (mAlive) {
    xb24_timeSinceHitByPlayer += dt;
    xb28_lowHealthFrenzyTimer += dt;
    if (x8f7_27_inProjectilePath) {
      xb28_lowHealthFrenzyTimer = 0.f;
      x8f7_27_inProjectilePath = false;
    }
    if (mHitByPlayerProjectile) {
      xb24_timeSinceHitByPlayer = 0.f;
      mHitByPlayerProjectile = false;
    }
  }
  UpdateCloak(dt, mgr);
  // 0x1B7C: the sfx emitter follows the pirate (fn_72_2EA8).
  UpdateSfxEmitter();
  if (body->GetPercentageFrozen() < 1.f) {
    if (mAlive) {
      xa10_steeringDelayTimer = CMath::Max(0.f, xa10_steeringDelayTimer - dt);
      if (x8f7_28_noPlayerLos) {
        xa7c_timeNoPlayerLos += dt;
      } else {
        xa7c_timeNoPlayerLos = 0.f;
      }
      xb0c_strafeDelayTimer = CMath::Max(0.f, xb0c_strafeDelayTimer - dt);
      xb94_dodgeDelayTimer = CMath::Max(0.f, xb94_dodgeDelayTimer - dt);
      CheckForProjectiles(mgr);
      if (mEnableAim) {
        mAttackTarget = ChooseTarget(mgr);
        UpdateTeamTarget(mgr);
      }
    }
    UpdateAttacks(dt, mgr);
    UpdateAimBodyState(dt, mgr);
    xb34_ikChain.Update(dt);
  }
  // 0x1CA0-0x1CAC: cntlzw/srwi, one when there is no ragdoll.
  const bool noRagDoll = xb30_ragDoll.get() == nullptr;
  if (noRagDoll ||
      (!x8f5_30_floatingCorpse &&           // 0x1CB4-0x1CBC (0x8f5 0x02)
       !xb30_ragDoll->IsPrimed())) {        // 0x1CC0-0x1CC8 (+0x70 0x40)
    CPatterned::Think(dt, mgr);             // 0x1CD8
    if (body->GetPercentageFrozen() != 1.f) { // 0x1CEC fcmpu B54 (1.0), beq
      mBoneTracking.Think(dt);              // 0x1CFC
    }
  } else {
    CActor::Think(dt, mgr);                 // 0x1D10
    UpdateAlphaDelta(mgr, dt);              // 0x1D20
    UpdateHitDamageTime(dt);                // vtable +0xec, 0x1D38
    UpdateIngPossession(dt);                // 0x1D44
    if (body->IsFrozen()) {                 // 0x1D4C-0x1D54 (0x594 0x20)
      body->UnFreeze();                     // 0x1D58
    }
  }
  if (!noRagDoll) {                         // 0x1D5C-0x1D60
    CPirateRagDoll* ragDoll = xb30_ragDoll.get();
    if (!ragDoll->IsPrimed()) {             // 0x1D68-0x1D70 (+0x70 0x40)
      ragDoll->Prime(mgr, GetTransform(), *ModelData()); // vtable +0x8, 0x1D8C
      const CVector3f translation = GetTranslation();    // 0x1D90-0x1DB0
      SetTransform(CTransform4f::Identity());            // 0x1DB4
      SetTranslation(translation);                       // 0x1DC0
      body->SetPlaybackRate(0.f);                        // rodata B9C, 0x1DD0
    } else {
      float waterTop = -1.7014118e38f;                   // rodata BA0 (-2^127), 0x1DE8
      if (InFluidId() != kInvalidUniqueId) {             // 0x1DEC-0x1E00
        CScriptWater* water = TCastToPtr< CScriptWater >(mgr.ObjectById(InFluidId())); // 0x1E24
        if (water != nullptr && water->GetActive()) {    // 0x1E28-0x1E38 (+0x20 0x80)
          waterTop = water->GetTriggerBoundsWR().GetMaxPoint().GetZ(); // 0x1E44-0x1E48
        }
      }
      ragDoll->Update(mgr, dt * GetDeathTimeScale(), waterTop); // +0x128, +0xc, 0x1E5C-0x1E80
      ModelData()->AdvanceParticles(GetTransform(), dt, mgr);   // 0x1E94
    }
    if (ragDoll->IsOver() &&                       // 0x1EA0 (+0x70 0x01)
        !ragDoll->WillContinueSmallMovements()) {  // 0x1EA8 (+0x70 0x20)
      SetMomentumWR(CVector3f::Zero());            // 0x1EB0-0x1ECC (0x1c0..0x1c8)
      Stop();                                      // 0x1ED0
      if (!mFadeToDeath) {                         // 0x1ED4-0x1EDC (0x420 0x10)
        mFadeToDeath = true;                       // 0x1EEC
        mAlphaDelta = -0.33333334f;                // rodata BA4, 0x1EF8 (0x40c)
        mAllEnergyDrained = true;                  // 0x1F00 rlwimi 1,30,30 (0x8f8 0x02)
      }
    }
  }
  // 0x1F08-0x1F18: `fcmpo; ble`, NaN skips the tail.
  if (xb2c_ragdollDelayTimer > 0.f) {
    xb2c_ragdollDelayTimer -= dt;                  // 0x1F1C-0x1F20
    if (xb2c_ragdollDelayTimer <= 0.f) {           // 0x1F28-0x1F30 `cror eq,lt,eq; bne`
      if (xb30_ragDoll.get() == nullptr) {         // 0x1F34-0x1F3C
        const rstl::reserved_vector< float, 14 > radii(
            skSpacePirateRagDollRadii, skSpacePirateRagDollRadii + 14); // rodata B04, 0x1F40-0x1F80
        // 0x1FA4-0x1FFC: 4 when 0x8f5 has 0x01 (ragdollNoAiCollision), | 3 when it has 0x02
        // (floatingCorpse).
        const uint flags = (x8f5_31_ragdollNoAiCollision ? 4u : 0u) |
                           (x8f5_30_floatingCorpse ? 3u : 0u);
        // 0x1F98 new 0x11c, 0x2004 fn_50_1938: impact sound 0x848, gravity rodata B70 (50.0),
        // floating gravity rodata BA8 (-3.0); 0x2010 fn_72_2060 stores it (the old one is null).
        xb30_ragDoll = rs_new CPirateRagDoll(mgr, this, mData.mImpactSfx, flags, 50.f, -3.f,
                                             radii);
        RemoveMaterial(kMT_Orbit, kMT_Target, mgr); // 0x29, 0x28, 0x2024
      }
      xb2c_ragdollDelayTimer = 0.f;                // rodata B9C, 0x2030
    }
  }
  TraceState(*this, StateMachineState().GetName());
}

// @ fn_72_CA94
// arms and the run: docs/module-analysis/SpacePirate_helpers.md (section 3).
void CSpacePirate::SetCinematicCollision(CStateManager& mgr) {
  RemoveMaterial(kMT_AIBlock, mgr);
  CMaterialList include = GetMaterialFilter().GetIncludeList();
  include.Remove(kMT_AIBlock);
  SetMaterialFilter(
      CMaterialFilter::MakeIncludeExclude(include, GetMaterialFilter().GetExcludeList()));
}

// @ fn_72_C9DC
// arms and the run: docs/module-analysis/SpacePirate_helpers.md (section 3).
void CSpacePirate::SetNonCinematicCollision(CStateManager& mgr) {
  AddMaterial(kMT_AIBlock, mgr);
  SetMaterialFilter(CMaterialFilter::MakeIncludeExclude(
      GetMaterialFilter().GetIncludeList().Union(CMaterialList(kMT_AIBlock)),
      GetMaterialFilter().GetExcludeList()));
}

// @ fn_72_DB48
bool CSpacePirate::LineOfSightTest(CStateManager& mgr, const CVector3f& start,
                                   const CVector3f& end, const CMaterialList& exclude) {
  const CMaterialFilter filter =
      CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), exclude);
  return mgr.RayCollideWorld(start, end, filter, this);
}

// @ fn_72_2F4C (0x2D4), called from the Think (fn_72_1980, asm 0x1B74) once per frame.
// Shadow pirates fade in and out; the cloak alpha lives in the actor's colour. The first test is
// the shadow bit (0x8f5 mask 0x10, asm 0x2F88 `extrwi. 1,27`) and a pirate without it leaves at
// once (asm 0x2F8C). The rest of the body is the cloak itself: the alpha byte at 0x44f is
// scaled by rodata 0xBB4 = 1/255 (asm 0x3178), the re-arm of the step timer is rodata 0xBBC =
// 0.08 times (1 - the random draw) (asm 0x3150-0x315C) and the frozen test compares against
// rodata 0xB54 = 1.0 (asm 0x3048).
// The shadow arm is unreachable in the shipped data: the scan of all 65 SpacePirate placements
// in all 12 worlds of the disc (tools/scan_pirt_props.py; every MLVL in every pak parses) finds
// none with the data flag 0x2000, so the only arm that runs is the early exit - 9120 calls in
// the 2026-10-21 area run, every one of them out at the shadow test (the trace's argument).
void CSpacePirate::UpdateCloak(float dt, CStateManager& mgr) {
  TraceCall(*this, "UpdateCloak", x8f5_27_shadowPirate ? 1 : 0);
  if (!x8f5_27_shadowPirate) {
    return;
  }
  if (mAlive) {
    if (xb7c_cloakDelayTimer > 0.f) {
      xb7c_cloakDelayTimer -= dt;
      if (xb7c_cloakDelayTimer <= 0.f) {
        mAlphaDelta = -0.4f;
      }
    }
  } else {
    xb8c_minCloakAlpha = 0.f;
    xb90_maxCloakAlpha = 1.f;
  }
  if (xb80_electricParticleTimer > 0.f) {
    xb80_electricParticleTimer -= dt;
    if (xb80_electricParticleTimer <= 0.f && !BodyController()->IsElectrocuting()) {
      mgr.ActorModelParticles()->StopElectric(*this);
    }
  }
  if (BodyController()->GetPercentageFrozen() != 1.f) {
    mAlphaDelta = 2.f;
  }
  if (mAlphaDelta < 0.f && mColor.GetAlpha() < xb8c_minCloakAlpha) {
    mColor.SetAlpha(xb8c_minCloakAlpha);
    mAlphaDelta = 0.f;
    RemoveMaterial(kMT_Target, mgr);
  }
  if (mAlphaDelta > 0.f && mColor.GetAlpha() > xb90_maxCloakAlpha) {
    mColor.SetAlpha(xb90_maxCloakAlpha);
    AddMaterial(kMT_Target, mgr);
  }
  xb84_cloakStepTime -= dt;
  if (xb84_cloakStepTime < 0.f) {
    const float random = mgr.Random()->Float();
    xb84_cloakStepTime = (1.f - random) * 0.08f;
    if (mAlphaDelta < 0.f) {
      xb88_shadowPirateAlpha = mColor.GetAlpha();
      if (mAlive) {
        xb88_shadowPirateAlpha -= random * (mColor.GetAlpha() - xb8c_minCloakAlpha);
      }
    } else if (mAlphaDelta > 0.f) {
      xb88_shadowPirateAlpha =
          mColor.GetAlpha() + random * (xb90_maxCloakAlpha - mColor.GetAlpha());
    } else {
      xb88_shadowPirateAlpha = mColor.GetAlpha();
    }
  }
}

// @ fn_72_E2DC (0x38): the pirate can be aimed at while its model is more than half opaque.
// Retail calls the vtable slot 0xC8 (GetModelAlphau8, fn_72_3AE8) and compares the byte with
// 127 - the `clrlwi`/`subfic`/`srwi` triple at asm 0xE2F8 is that comparison. The body has no
// branch, so every call does the same work and the area run's calls prove all of it.
bool CSpacePirate::CheckTargetable(CStateManager& mgr) {
  const bool targetable = GetModelAlphau8(mgr) > 127;
  TraceCall(*this, "CheckTargetable", targetable ? 1 : 0);
  return targetable;
}

// @ fn_72_DEDC (0x50): the pirate gives up its squad attack slot. The attack type comes from the
// melee bit (the 0x8f4 byte's bit 27, mask 0x10 in that byte, asm 0xDF0C `extrwi. 1,27`): the
// bit clear is 1 = kAT_Projectile and the bit set is 0 = kAT_Melee, the enum's own order (asm
// 0xDF14 `srwi` inverts the bit). The team is the id at 0xba0 and the member is the pirate's own
// id (asm 0xDEF4/0xDEFC). The last argument is a plain 1 (asm 0xDEE4 `li r7, 0x1`), so the team
// role is cleared for a melee pirate too.
void CSpacePirate::SquadReset(CStateManager& mgr) {
  TraceCall(*this, "SquadReset", x8f4_27_melee ? 1 : 0);
  CScriptTeamAiMgr::EndAttack(x8f4_27_melee ? CScriptTeamAiMgr::kAT_Melee
                                            : CScriptTeamAiMgr::kAT_Projectile,
                              mgr, xba0_teamAiMgrId, GetUniqueId(), true);
}

// Tells the squad manager who this member is shooting at.
// fn_72_DE5C (0x80): no squad (0xba0 == kInvalidUniqueId, asm 0xDE80-0xDE84) or a squad id that no
// longer resolves to a CScriptTeamAiMgr (asm 0xDEA0-0xDEA4) does nothing; otherwise the member is
// the pirate's own id (0x8, asm 0xDEB8) and its target the attack target (0xa94, asm 0xDEA8).
// All three arms ran (tools/cov, 07_sand_alamoportalobservation L3 with the squads deleted before
// round 2, 2026-10-21): no squad 10051 entries, the cast 20000 times and SetMemberTargetId 319.
// @ fn_72_DE5C
void CSpacePirate::UpdateTeamTarget(CStateManager& mgr) {
  if (xba0_teamAiMgrId == kInvalidUniqueId) {
    return;
  }
  if (CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(xba0_teamAiMgrId))) {
    team->SetMemberTargetId(GetUniqueId(), mAttackTarget);
  }
}

// fn_72_EFD4 (0xCC): a squad (0xba0 != kInvalidUniqueId, asm 0xF00C) that still resolves (asm
// 0xF028) and has this member within 20 units (rodata B74 = 20.0, asm 0xF040-0xF04C) picks the
// best individual target (asm 0xF060); every other case falls to ChoosePlayer (asm 0xF068-0xF07C).
// All four arms ran (tools/cov, 06_sand_alamofront L2, 2026-10-21): no squad, in range (2 hits) and
// out of range, and a squad id whose manager is gone (150 hits: `send <team> Delete`, then the player
// deactivated with `send <player> Deactivate` so ChooseTarget falls back to ChooseTeamTarget).
// @ fn_72_EFD4
TUniqueId CSpacePirate::ChooseTeamTarget(CStateManager& mgr) {
  if (xba0_teamAiMgrId != kInvalidUniqueId) {
    CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(xba0_teamAiMgrId));
    if (team != nullptr && team->IsTeamMemberInRange(mgr, *this, 20.f)) {
      return team->FindBestIndividualAttackTarget(mgr, *this);
    }
  }
  return CScriptTeamAiMgr::ChoosePlayer(mgr, *this);
}

// Retail fn_72_EDEC (0x1E8). The player loop stops at the attack target when that player is
// inactive: the team picks the target (fn_72_EFD4) and a flag is set (asm 0xEE74). An active target
// gives its distance; any other player within 10 units (rodata B58) is the nearest. A target more
// than 30 units away (rodata C2C) is dropped for that nearest player. Without the flag (asm 0xEF48),
// a squad (0xba0) that resolves and has this member within 50 units (rodata B70, not the 20 of
// ChooseTeamTarget) keeps the current attack target instead (asm 0xEF50-0xEFA4). Retail has no
// null check on the player.
// gcov 2026-10-21: every arm but the swap (it needs a second player: the target and another one),
// from 06_sand_alamofront with `send <player> Deactivate` and the helpers lane's squad run in
// 07_sand_alamoportalobservation L3 (spacepirate_helpers_force.py run 2).
// @ fn_72_EDEC
TUniqueId CSpacePirate::ChooseTarget(CStateManager& mgr) {
  float nearest = 10.f;
  float currentDist = 0.f;
  TUniqueId nearestId = kInvalidUniqueId;
  TUniqueId result = mAttackTarget;
  bool teamChosen = false;
  for (uint i = 0; i < mgr.GetNumPlayers(); ++i) {
    const CPlayer* player = mgr.GetPlayer(i);
    if (player->GetUniqueId() == mAttackTarget) {
      if (!player->GetActive()) {
        teamChosen = true;
        result = ChooseTeamTarget(mgr);
        break;
      } else {
        currentDist = (player->GetTranslation() - GetTranslation()).Magnitude();
      }
    } else {
      const float dist = (player->GetTranslation() - GetTranslation()).Magnitude();
      if (dist < nearest) {
        nearest = dist;
        nearestId = player->GetUniqueId();
      }
    }
  }
  if (currentDist > 30.f && nearest < 10.f) {
    result = nearestId;
  }
  if (!teamChosen && xba0_teamAiMgrId != kInvalidUniqueId) {
    CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(xba0_teamAiMgrId));
    if (team != nullptr && team->IsTeamMemberInRange(mgr, *this, 50.f)) {
      return mAttackTarget;
    }
  }
  return result;
}

// Whether the next shot is a frenzy burst; a roll of 2..4 shots is queued and counted down.
// Retail fn_72_3220. Arms FORCED by spacepirate_force7.py (sp20): the frenzy-chance bit (0x8f8 0x80;
// only the ctor writes it, as false, F6E8, so both rolls are dead in retail), an empty charge list, a
// morphed target, the low-HP arms (xa18_ raised) and no target; the natural runs give the rest.
// @ fn_72_3220
bool CSpacePirate::ShouldFrenzy(CStateManager& mgr) {
  bool reset = false;
  if (x8f8_24_pendingFrenzyChance) {
    x8f8_24_pendingFrenzyChance = false;
    if (mgr.Random()->Next() % 100 < 25) {
      reset = true;
    }
  }
  // The module's charge-player list (bss_40, asm 0x3290-0x32A4) not being empty also rolls a burst.
  if (!mChargePlayerList.empty()) {
    reset = true;
  }
  const CPlayer* target = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mAttackTarget));
  if (target != nullptr && target->GetMorphballTransitionState() == CPlayer::kMS_Morphed) {
    reset = true;
  }
  if (GetHealthInfo()->GetHP() < 0.3f * xa18_ && mgr.Random()->Next() % 100 < 60 &&
      xb28_lowHealthFrenzyTimer < 0.5f) {
    reset = true;
  }
  if (reset) {
    x8fc_frenzyFrames = mgr.Random()->Range(2, 4);
  }
  return --x8fc_frenzyFrames >= 0;
}

// @ fn_72_CB30 (0x150): where the pirate looks and aims - its target, or a point ahead if the
// target is gone.
CVector3f CSpacePirate::GetTargetPos(CStateManager& mgr) {
  CEntity* entity = mgr.ObjectById(mAttackTarget);
  if (const CPlayer* player = TCastToPtr< CPlayer >(entity)) {
    return player->GetTranslation();
  }
  if (const CActor* actor = TCastToPtr< CActor >(entity)) {
    if (actor->GetActive()) {
      return actor->GetTranslation();
    }
  }
  mAttackTarget = ChooseTeamTarget(mgr);
  UpdateTeamTarget(mgr);
  mBoneTracking.SetTarget(mAttackTarget);
  return GetTranslation() + 10.f * GetTransform().GetForward();
}

// Sets x8f7_27_inProjectilePath when a shot is flying at the pirate.
// @ fn_72_DBD4 (0x288): the body runs only while the 0x8f7 mask 0x20 bit (a player fired) is
// set; the near list is built over the box 0xbb4/0xbb8/0xbbc +- rodata 0xB78 = 5.0 around the
// noise position and the projectile's travel has to point at the pirate's box centre (rodata
// 0xC1C = 0.939, asm 0xDDF0). The trace's argument is the arm that ran: 0 = the early-out,
// 1 = the body ran and no projectile was in the near list, 2 = the projectile was at the box
// centre (the delta's magnitude is not safe), 3 = the projectile was not in front of the
// pirate, 4 = the projectile had not moved, 5 = the projectile was not heading at the pirate,
// 6 = the projectile was heading at the pirate (the flag is set). Arms 1, 3, 5 and 6 ran in the
// 2026-10-21 area run; arms 2 and 4 need a zero-length vector - a projectile exactly at the
// box centre, or one that never left its spawn point - which no natural shot produces.
void CSpacePirate::CheckForProjectiles(CStateManager& mgr) {
  if (!x8f7_26_hearPlayerFire) {
    TraceCall(*this, "CheckForProjectiles", 0);
    return;
  }
  const CVector3f extent(5.f, 5.f, 5.f);
  const CAABox bounds(xbb4_ - extent, xbb4_ + extent);
  x8f7_27_inProjectilePath = false;
  int arm = 1;
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, bounds, CMaterialFilter::MakeInclude(CMaterialList(kMT_Projectile)),
                    nullptr);
  for (int i = 0; i < nearList.size() && !x8f7_27_inProjectilePath; ++i) {
    const CGameProjectile* projectile =
        TCastToConstPtr< CGameProjectile >(mgr.GetObjectById(nearList[i]));
    if (projectile == nullptr) {
      continue;
    }
    CVector3f delta = GetBoundingBox().GetCenterPoint() - projectile->GetTranslation();
    if (!delta.IsMagnitudeSafe()) {
      x8f7_27_inProjectilePath = true;
      arm = 2;
    } else if (CVector3f::Dot(GetTransform().GetForward(), delta) < 0.f) {
      delta.Normalize();
      CVector3f travel = projectile->GetTranslation() - projectile->GetPreviousPos();
      if (travel.IsMagnitudeSafe()) {
        travel.Normalize();
        if (CVector3f::Dot(travel, delta) > 0.939f) {
          x8f7_27_inProjectilePath = true;
          arm = 6;
        } else {
          arm = 5;
        }
      } else {
        arm = 4;
      }
    } else {
      arm = 3;
    }
  }
  TraceCall(*this, "CheckForProjectiles", arm);
  x8f7_26_hearPlayerFire = false;
}

// Retail fn_72_3D7C: the launcher's ballistic solve. Ten candidate angles are tried from 0 (or from
// pi/4 downwards when the target is above the gun); the first one whose required speed lands inside
// [minLaunchSpeed, maxLaunchSpeed] wins, otherwise the closest miss is kept.
// @ fn_72_3D7C
static void SolveGrenadeLaunch(const CVector3f& target, const CVector3f& origin, float minLaunchSpeed,
                               float maxLaunchSpeed, float& angle, float& speed) {
  const float heightDelta = target.GetZ() - origin.GetZ();
  const float distance =
      CVector2f(target.GetX() - origin.GetX(), target.GetY() - origin.GetY()).Magnitude();
  const float minSpeedSq = minLaunchSpeed * minLaunchSpeed;
  const float maxSpeedSq = maxLaunchSpeed * maxLaunchSpeed;

  float startAngle = 0.f;
  float angleStep = 0.07853982f; // pi/40
  // Plain compares: 0x3EC4/0x3F2C/0x3F60 ble and 0x3F74 bge are taken on NaN, as C's > / < are.
  if (target.GetZ() > origin.GetZ()) {
    startAngle = 0.7853982f; // pi/4
    angleStep = -angleStep;
  }
  const float halfGravityDist = 0.5f * kDefaultGravityAccel * distance;
  const float lhs = distance * halfGravityDist;

  float bestAngle = 0.f;
  float bestSpeed = minLaunchSpeed;
  float bestError = FLT_MAX;
  for (float i = 0.f; i < 10.f; i += 1.f) {
    const float candidate = angleStep * i + startAngle;
    const float cosine = CMath::FastCosR(candidate);
    const float sine = CMath::FastSinR(candidate);
    // Retail's fmsubs leaves the terms in this order: distance*(cos*sin) - height*cos^2.
    const float denominator = distance * (cosine * sine) - heightDelta * (cosine * cosine);
    if (denominator > 1.1920929e-7f) {
      const float speedSq = lhs / denominator;
      if (speedSq >= minSpeedSq && speedSq <= maxSpeedSq) {
        bestAngle = candidate;
        bestSpeed = CMath::SqrtF(speedSq);
        break; // retail branches straight past the loop counter here
      }
      const float error = speedSq > maxSpeedSq ? speedSq - maxSpeedSq : minSpeedSq - speedSq;
      if (error < bestError) {
        bestAngle = candidate;
        bestSpeed = CMath::SqrtF(speedSq);
        bestError = error;
      }
    }
  }
  angle = bestAngle;
  speed = bestSpeed;
}

// Retail fn_72_4030: the point the grenade is thrown at - the target's aim position, lowered to half
// the player's eye height, then (for a bouncing launcher) pulled 70% of the way back towards the gun
// when the aim is at most 2 m above the pirate (asm: cror lt/eq -> <=, not an equality test).
// @ fn_72_4030
static CVector3f GetGrenadeAimPoint(const CActor& target, const CStateManager& mgr,
                                    const CVector3f& piratePos, uint numBounces) {
  CVector3f aim = target.GetAimPosition(mgr, 0.5f);
  const CPlayer* player = TCastToConstPtr< CPlayer >(target);
  if (player && player->GetMorphballTransitionState() == CPlayer::kMS_Unmorphed) {
    aim -= CVector3f(0.f, 0.f, 0.5f * player->GetEyeHeight());
  }
  if (numBounces != 0 && aim.GetZ() <= piratePos.GetZ() + 2.0f) {
    aim = piratePos + 0.7f * (aim - piratePos);
  }
  return aim;
}

// Retail fn_72_4174: launches one bouncy grenade from the gun locator, aimed by fn_72_4030 and
// fn_72_3D7C, with retail's default model / actor parameters and 5 m player-explode distance.
// @ fn_72_4174
void CSpacePirate::SpawnGrenade(CStateManager& mgr) {
  --xc58_;
  const CTransform4f gunXf = GetLctrTransform(xa86_gunSeg);
  const CVector3f origin = gunXf.GetTranslation();
  float angle = 0.f;
  float speed = mData.mGrenadeMinLaunchSpeed;
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget));
  if (!target) {
    return;
  }
  const CVector3f aim =
      GetGrenadeAimPoint(*target, mgr, GetTranslation(), mData.mGrenadeNumBounces);
  SolveGrenadeLaunch(aim, origin, mData.mGrenadeMinLaunchSpeed, mData.mGrenadeMaxLaunchSpeed,
                     angle, speed);

  CVector3f flat = aim - origin;
  flat.SetZ(0.f);
  const CVector3f forward = gunXf.GetForward();
  CVector3f horizontal = flat.CanBeNormalized() ? flat.AsNormalized() : forward;
  if (CVector3f::GetAngleDiff(forward, horizontal) > 0.7853982f) {
    horizontal = CVector3f::Slerp(forward, horizontal, CRelAngle::FromRadians(0.7853982f));
  }
  const CVector3f launchDir =
      CVector3f::Slerp(horizontal, CVector3f::Up(), CRelAngle::FromRadians(angle));
  const CTransform4f xf = CTransform4f::LookAt(origin, origin + launchDir, CVector3f::Up());

  CBouncyGrenade* grenade = rs_new CBouncyGrenade(
      mgr.AllocateUniqueId(), rstl::string_l("Bouncy Grenade"),
      CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true, kInvalidEditorId), xf,
      CModelData(), CActorParameters(), GetUniqueId(), speed, mData.BuildGrenadeData(), 5.f,
      CAABox::MakeMaxInvertedBox(), kInvalidUniqueId, 0.f, 0, nullptr, nullptr);
  if (grenade) {
    mgr.AddObject(*grenade);
  }
}

// Fires one shot from the gun locator at the target (or straight ahead if the pirate is dying).
// @ fn_72_45F8
bool CSpacePirate::FireProjectile(float dt, CStateManager& mgr) {
  bool fired = false;
  const CTransform4f gunXf = GetLctrTransform(xa86_gunSeg);
  if (!mAlive) {
    LaunchProjectile(gunXf, mgr, 6, CWeapon::kPA_None, false, CImpactVisorEffect::None(),
                     CVector3f(1.f, 1.f, 1.f));
    fired = true;
  } else if (const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget))) {
    CVector3f pos = actor->GetTranslation();
    bool isPlayer = false;
    if (const CPlayer* player = TCastToConstPtr< CPlayer >(actor)) {
      if (player->GetMorphballTransitionState() == CPlayer::kMS_Morphed) {
        isPlayer = true;
      } else {
        pos = ProjectileInfo()->PredictInterceptPos(gunXf.GetTranslation(),
                                                    player->GetAimPosition(mgr, 0.f), *player,
                                                    true, dt);
      }
    }
    CVector3f gunToPos = pos - gunXf.GetTranslation();
    const float distance = gunToPos.Magnitude();
    gunToPos *= 1.f / distance;
    const float dot = CVector3f::Dot((GetLctrTransform(xa88_wristSeg).GetTranslation() -
                                      GetLctrTransform(xa87_elbowSeg).GetTranslation())
                                         .AsNormalized(),
                                     gunToPos);
    if ((dot > 0.707f || (distance < 6.f && dot > 0.5f)) &&
        (isPlayer || LineOfSightTest(mgr, gunXf.GetTranslation(), pos,
                                     CMaterialList(kMT_Player, kMT_ProjectilePassthrough)))) {
      pos += GetTransform().Rotate(mBurstFire.GetDistanceCompensatedError(distance, 6.f));
      const CTransform4f shotXf = CTransform4f::LookAt(gunXf.GetTranslation(), pos, CVector3f::Up());
      LaunchProjectile(shotXf, mgr, 6, CWeapon::kPA_None, false, CImpactVisorEffect::None(),
                       CVector3f(1.f, 1.f, 1.f));
      fired = true;
    }
  }
  if (fired) {
    CSfxManager::AddEmitter(mData.mProjectileSfx, GetTranslation(), GetCurrentAreaId().Value(),
                            true, false, CSfxManager::kMedPriority);
  }
  return fired;
}

// Squad bookkeeping and the burst-fire pattern. Retail switches on the equipped weapon (0x2920
// lwz 0x888 = data +0xc8): 0 runs the burst fire below; 1 (the grenade launcher) only counts the
// attack timer down (0x2E60-0x2E70); any other weapon does nothing (0x2E74).
// @ fn_72_28F0
void CSpacePirate::UpdateAttacks(float dt, CStateManager& mgr) {
  if (mData.mEquippedWeapon != 0) {
    if (mData.mEquippedWeapon == 1) {
      mAttackRemTime -= dt;
    }
    return;
  }
  bool reset = true;
  const bool canShoot =
      !mAlive || (BodyController()->GetBodyStateInfo().GetCurrentState()->CanShoot() &&
                  mEnableAim && !x8f4_27_melee && !BodyController()->IsFrozen() &&
                  !x8f4_25_ceilingAmbush && !x8f9_26_started &&
                  !BodyController()->IsElectrocuting());
  if (canShoot && mBurstFire.GetBurstType() != -1) {
    const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mAttackTarget));
    if (mAlive && (!x8f4_29_onlyAttackInRange ||
                   (player != nullptr &&
                    (player->GetTranslation() - GetTranslation()).MagSquared() <
                        mLeashRadius * mLeashRadius))) {
      reset = false;
      mAttackRemTime -= dt;
      if (mAttackRemTime < 0.f) {
        const CTeamAiRole* role =
            CScriptTeamAiMgr::GetTeamAiRole(mgr, xba0_teamAiMgrId, GetUniqueId());
        if ((role == nullptr || role->GetTeamAiRole() == CTeamAiRole::kTAR_Projectile) &&
            (xba0_teamAiMgrId == kInvalidUniqueId ||
             CScriptTeamAiMgr::StartAttack(CScriptTeamAiMgr::kAT_Projectile, mgr,
                                           xba0_teamAiMgrId, GetUniqueId()))) {
          if (ShouldFrenzy(mgr)) {
            mBurstFire.SetBurstType(2);
          }
          if (x8f5_26_seated) {
            mBurstFire.SetBurstType(5);
          }
          // A player facing away from this pirate gets the "behind" bursts (6..12).
          if (player != nullptr &&
              CVector3f::Dot(GetTranslation() - player->GetTranslation(),
                             player->GetTransform().GetForward()) < 0.f &&
              mBurstFire.GetBurstType() < 6) {
            mBurstFire.SetBurstType(mBurstFire.GetBurstType() + 6);
          }
          mBurstFire.Start(mgr);
          mAttackRemTime = mgr.Random()->Float() * mAttackTimeVariation + GetAverageAttackTime();
          if (player != nullptr) {
            const CVector3f fromPlayer =
                (GetGunEyePos() - player->GetAimPosition(mgr, 0.f)).AsNormalized();
            if (CVector3f::Dot(fromPlayer, player->GetTransform().GetForward()) < 0.9f) {
              // Another armed pirate in the same area is already shooting: wait a little longer.
              const CObjectList& list = mgr.GetObjectListById(kOL_ListeningAi);
              for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
                const CSpacePirate* other = TCastToConstPtr< CSpacePirate >(list[i]);
                if (other != nullptr && other != this && other->mEnableAim &&
                    other->GetCurrentAreaId() == GetCurrentAreaId()) {
                  mAttackRemTime += 0.2f;
                }
              }
            }
          }
        }
      }
    }
    mBurstFire.Update(mgr, dt);
    if (mBurstFire.ShouldFire()) {
      if (player != nullptr && player->GetSidewaysDashing() && mgr.Random()->Float() < 0.5f) {
        mBurstFire.SetAvoidAccuracy(true);
      }
      FireProjectile(dt, mgr);
      mBurstFire.SetAvoidAccuracy(false);
      // 0x2D80: an Ing-possessed pirate uses its own intra-burst time and variation (data +0x94/+0x98).
      if (IsIngPossessed()) {
        mBurstFire.SetTimeToNextShot(mData.mIngIntraBurstVariation *
                                         (mgr.Random()->Float() - 0.5f) +
                                     mData.mIngIntraBurstTime);
      } else {
        mBurstFire.SetTimeToNextShot(mData.mIntraBurstVariation * (mgr.Random()->Float() - 0.5f) +
                                     mData.mIntraBurstTime);
      }
    } else if (!mBurstFire.IsBurstSet()) {
      reset = true;
    }
  }
  if (reset) {
    SquadReset(mgr);
  }
  for (uint i = 0; i < mgr.GetNumPlayers(); ++i) {
    SetValidTarget(static_cast< int >(i), CheckTargetable(mgr));
  }
}

// Points the additive aim animation at the target while the pirate may aim, and eases it back to
// straight ahead (then idle) once it may not. Retail also passes the wall-hang flag as the aim
// command's type; CBCAdditiveAimCmd has no setter for it yet.
// @ fn_72_253C
void CSpacePirate::UpdateAimBodyState(float dt, CStateManager& mgr) {
  CBodyController* body = BodyController();
  CBodyStateCmdMgr& cmds = body->CommandMgr();
  if (mAlive && mEnableAim && !body->IsFrozen() && !body->IsElectrocuting() && !x8f4_27_melee &&
      xb30_ragDoll.get() == nullptr && (!x8f5_26_seated || x8f9_28_satUp) && !x8fa_30_) {
    xb98_aimDelayTimer = CMath::Max(0.f, xb98_aimDelayTimer - dt);
    // 0x2614 fcmpo + 0x2618 cror eq,lt,eq: the block runs while xb98 <= 0.f, not only at 0.
    if (xb98_aimDelayTimer <= 0.f) {
      cmds.DeliverCmd(CBCAdditiveAimCmd());
      const CVector3f gunPos = GetLctrTransform(xa86_gunSeg).GetTranslation();
      // Only the offset's x and y are added (0x26FC/0x2704 load them; 0x2734 adds 0.0 to the
      // target's z), so the aim height is the target's above the pirate's origin, not the gun's.
      const CVector3f offset = x8f7_29_inWallHang ? CVector3f::Zero() : GetTranslation() - gunPos;
      CVector3f aim = GetTransform().TransposeMultiply(
          GetTargetPos(mgr) + CVector3f(offset.GetX(), offset.GetY(), 0.f));
      if (x8f7_29_inWallHang) {
        aim.SetX(-aim.GetX());
        aim.SetY(-aim.GetY());
      }
      cmds.DeliverAdditiveTargetVector(aim);
      xb9c_ = 0.5f;
    }
  } else if (xb9c_ > 0.f) {
    xb9c_ -= dt;
    cmds.DeliverAdditiveTargetVector(GetTransform().GetForward());
    if (xb9c_ <= 0.f) {
      xb9c_ = 0.f;
      cmds.DeliverCmd(kBSC_AdditiveIdle);
    }
  }
}

// Retail fn_72_E498, in two passes: the wall-hang / ceiling-ambush filters and the activation
// bookkeeping run before CPatterned's handler, the per-message state after it.
// @ fn_72_E498
void CSpacePirate::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  const EScriptObjectMessage message = msg.GetMessage();
  if (x8f7_29_inWallHang || x8f4_25_ceilingAmbush) {
    switch (message) {
    case kSM_OffGround: {
      CBodyController* body = BodyController();
      const pas::EAnimationState bodyState = body->GetCurrentStateId();
      if ((x8f7_29_inWallHang && bodyState == pas::kAS_WallHang &&
           !body->GetBodyStateInfo().GetCurrentState()->ApplyGravity()) ||
          (x8f4_25_ceilingAmbush &&
           (bodyState == pas::kAS_Locomotion ||
            (bodyState == pas::kAS_Jump &&
             !body->GetBodyStateInfo().GetCurrentState()->IsInAir(*body))))) {
        Stop();
        SetMomentumWR(CVector3f::Zero());
        return;
      }
      break;
    }
    case kSM_Landed:
      xb24_timeSinceHitByPlayer = FLT_MAX;
      break;
    default:
      break;
    }
  }
  if (message == kSM_Alert || message == kSM_Activate) {
    if (GetActive()) {
      if (x8f4_29_onlyAttackInRange) {
        x8f8_31_mayStartAttack = true;
      } else {
        mHitByPlayerProjectile = true;
      }
    } else if (x8f4_25_ceilingAmbush) {
      RemoveMaterial(kMT_GroundCollider, mgr);
      mOnGround = false;
    }
    if (message == kSM_Activate && x8f6_24_trooper) {
      // 0xE674-0xE69C: a trooper still inactive with no warp point (xc04 == -1) starts invisible
      // (colour alpha byte 0x44f = rodata B9C 0.f, quantized).
      if (!GetActive() && xc04_warpPoint == -1) {
        mColor.SetAlpha(static_cast< uchar >(0));
      }
      x8fa_26_ = true;
    }
  }
  CPatterned::AcceptScriptMsg(mgr, msg);
  switch (message) {
  case kSM_AreaLoaded: {
    const rstl::vector< SConnection >& connections = GetConnectionList();
    for (AUTO(it, connections.begin()); it != connections.end(); ++it) {
      if (it->state == kSS_Retreat && it->msg == kSM_Next) {
        TUniqueId id = mgr.GetIdForScript(it->objId);
        if (CScriptCoverPoint* cover = TCastToPtr< CScriptCoverPoint >(mgr.ObjectById(id))) {
          cover->Reserve(GetUniqueId());
        }
      } else if (it->state == kSS_Patrol && it->msg == kSM_Follow) {
        x8f7_24_enablePatrol = true;
        // The patrol state's CPatterned half (move along the waypoints) runs only under this bit.
        TraceCall(*this, "enablePatrol", 1);
      }
    }
    mPathFindSearch.SetArea(
        mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()).GetPostConstructed()->mPathArea);
    if (x8f5_30_floatingCorpse) {
      xb2c_ragdollDelayTimer = 0.01f;
      RemoveMaterial(kMT_Character, kMT_Solid, kMT_Target, kMT_Orbit, mgr);
      mAlive = false;
      HealthInfo()->SetHP(-1.f);
    } else {
      EmptyE314(mgr, true); // 0xE910-0xE91C: fn_72_E314(this, mgr, 1) when the bit is clear
    }
    break;
  }
  case kSM_Decrement:
    // 0xE924-0xE950: with a ragdoll, its byte 0x70 loses 0x04 (rlwimi 2,29,29: mNoOverTimer) and
    // then 0x20 (rlwimi 5,26,26: mContinueSmallMovements).
    if (xb30_ragDoll.get() != nullptr) { // 0xE928-0xE92C
      xb30_ragDoll->SetNoOverTimer(false);
      xb30_ragDoll->SetContinueSmallMovements(false);
    }
    break;
  case kSM_Create: {
    if (x8f4_25_ceilingAmbush && x8f5_27_shadowPirate) {
      mColor.SetAlpha(mData.mCloakOpacity);
      mAlphaDelta = -1.f;
    }
    xa24_ = mgr.Random()->Next() % 6;
    CMaterialList include = GetMaterialFilter().GetIncludeList();
    CMaterialList exclude = GetMaterialFilter().GetExcludeList();
    CMaterialList passthrough(kMT_AIPassthrough);
    include.Remove(passthrough);
    exclude.Add(passthrough);
    SetMaterialFilter(CMaterialFilter::MakeIncludeExclude(include, exclude));
    break;
  }
  case kSM_SetToZero:
    if (GetActive()) {
      x8f6_29_enableRetreat = true;
      TrooperWarpOut(mgr, false);
    }
    break;
  case kSM_OffGround:
    if (BodyController()->GetPercentageFrozen() != 1.f) {
      float momentum = GetGravityConstant() * GetMass();
      if (x8f4_25_ceilingAmbush) {
        momentum *= 3.f;
      }
      SetMomentumWR(CVector3f(0.f, 0.f, -momentum));
    }
    if (BodyController()->GetCurrentStateId() == pas::kAS_Step) {
      SetVelocityWR(CVector3f(0.f, 0.f, GetVelocityWR().GetZ()));
    }
    mBurstFire.SetBurstType(3);
    break;
  case kSM_Launching:
    // 0xEB10: skipped while the body is hurled (kAS_Hurled, 0xE).
    if (BodyController()->GetCurrentStateId() != pas::kAS_Hurled) {
      CScriptMsg offGround(msg.GetSenderId(), kInvalidUniqueId, kSM_OffGround);
      CPatterned::AcceptScriptMsg(mgr, offGround);
      SetMomentumWR(CVector3f(0.f, 0.f, -GetGravityConstant() * GetMass()));
      SetVelocityForJump();
    }
    break;
  case kSM_Landed:
    mBurstFire.SetBurstType(x8f4_29_onlyAttackInRange ? 4 : 1);
    x8f7_30_jumpVelSet = false;
    x8fa_29_ = false;
    if (x8f5_27_shadowPirate && GetVelocityWR().GetZ() < -1.f) {
      mAlphaDelta = 1.f;
      xb7c_cloakDelayTimer += -0.05f * GetVelocityWR().GetZ();
      xb7c_cloakDelayTimer = CMath::Clamp(0.f, xb7c_cloakDelayTimer, 1.f);
      xb90_maxCloakAlpha = 0.5f;
      if (mAlive) {
        mgr.ActorModelParticles()->StartElectric(*this);
        xb80_electricParticleTimer = 1.f + xb7c_cloakDelayTimer;
      }
    }
    break;
  case kSM_Action:
    if (CScriptTargetingPoint* point =
            TCastToPtr< CScriptTargetingPoint >(mgr.ObjectById(msg.GetSenderId()))) {
      if (point->GetActive()) {
        mBoneTracking.SetTarget(msg.GetSenderId());
        mAttackTarget = msg.GetSenderId();
        UpdateTeamTarget(mgr);
        mHitByPlayerProjectile = true;
      } else {
        mAttackTarget = ChooseTeamTarget(mgr);
        UpdateTeamTarget(mgr);
        mBoneTracking.SetTarget(mAttackTarget);
      }
      mAttackRemTime = 0.f;
    }
    break;
  case kSM_Delete:
  case kSM_Deactivate:
    SquadRemove(mgr);
    // 0xED6C: fn_72_9E64 = remove(uid) on the module's charge-player list (bss_40): fn_72_9EB0
    // erases every node holding this pirate's id.
    mChargePlayerList.remove(GetUniqueId());
    break;
  case kSM_Start:
    x8f9_26_started = false;
    break;
  case kSM_Stop:
    x8f9_26_started = true;
    break;
  case kSM_Escape:
    if (!GetActive()) { // fn_72_E498:149-154
      return;
    }
    TrooperWarpOut(mgr, true);
    break;
  default:
    break;
  }
}

// @ fn_72_DF2C: leave the team manager the pirate joined.
void CSpacePirate::SquadRemove(CStateManager& mgr) {
  if (xba0_teamAiMgrId == kInvalidUniqueId) {
    return;
  }
  if (CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(xba0_teamAiMgrId))) {
    if (team->IsPartOfTeam(GetUniqueId())) {
      team->QuitTeam(GetUniqueId());
      xba0_teamAiMgrId = kInvalidUniqueId;
    }
  }
}

// Retail fn_72_51C8: a live trooper with no warp point and no pending warp enters "WarpOut".
// @ fn_72_51C8
// arms and the run: docs/module-analysis/SpacePirate_helpers.md (section 3).
void CSpacePirate::TrooperWarpOut(CStateManager& mgr, bool flag) {
  if (mAlive && !x8fa_25_ && xc04_warpPoint == -1 && x8f6_24_trooper) {
    mStateMachine->SetState(mgr, *this, rstl::string_l("WarpOut"));
    x8fa_27_ = flag;
  }
}

// @ fn_72_E168 (MP1 SetVelocityForJump): ballistic launch to the patrol destination.
void CSpacePirate::SetVelocityForJump() {
  if (x8f7_30_jumpVelSet || x8fa_31_) {
    return;
  }
  const CVector3f& pos = GetTranslation();
  const float gravity = GetGravityConstant();
  const float peak = xaf8_jumpHeight + (pos.GetZ() < xafc_patrolDestPos.GetZ()
                                            ? xafc_patrolDestPos.GetZ()
                                            : pos.GetZ());
  const float vz = CMath::SqrtF(2.f * gravity * (peak - pos.GetZ()));
  const float rate = 1.f / (vz / gravity +
                            CMath::SqrtF(2.f * (peak - xafc_patrolDestPos.GetZ()) / gravity));
  SetVelocityWR(CVector3f(rate * (xafc_patrolDestPos.GetX() - pos.GetX()),
                          rate * (xafc_patrolDestPos.GetY() - pos.GetY()), vz));
  x8f7_30_jumpVelSet = true;
  x8fa_29_ = true;
}

// @ fn_72_38E4 (PreRender): the ragdoll pre-renders instead of the model while a ragdoll is
// live; then the patterned pre-render, the bone tracking and the ik chain.
void CSpacePirate::PreRender(CStateManager& mgr) {
  // 0x3900-0x3914: a primed ragdoll (+0x70 0x40) pre-renders first (its vtable slot 0x10, 0x392C)
  // with the actor's translation (0x54) and the model data (0x60).
  if (xb30_ragDoll.get() != nullptr && xb30_ragDoll->IsPrimed()) {
    xb30_ragDoll->PreRender(GetTranslation(), *ModelData());
  }
  // 0x3938: the patterned pre-render.
  CPatterned::PreRender(mgr);
  // 0x393C-0x3950: the bone tracking and the ik chain run only without a primed ragdoll.
  if (xb30_ragDoll.get() == nullptr || !xb30_ragDoll->IsPrimed()) {
    // 0x398C: the bone tracking: (mgr, the anim data, the transform, the model's scale, the body).
    mBoneTracking.PreRender(mgr, *ModelData()->AnimationData(), GetTransform(),
                            ModelData()->GetScale(), *BodyController());
    // 0x39C0: the ik chain: (the anim data, the transform, the model's scale).
    xb34_ikChain.PreRender(*ModelData()->AnimationData(), GetTransform(), ModelData()->GetScale());
  }
  // 0x39C4: while the portal plane is set (|its d| > 0), 0x39E8 writes an uninitialised word into
  // the draw flags' first word and 0x39F0 sets bit 0x8000 in their high half. The plane is never
  // set in the port and the retail's word is undefined, so that stays a comment.
}

// @ fn_72_4F28 (0x9C): an equipped weapon of type 1 with a valid launcher model gives the pirate the
// launcher's static model at the body model's scale.
void CSpacePirate::InitLauncherModel(const CSpacePirateData& data) {
  TraceCall(*this, "InitLauncherModel (fn_72_4F28) weapon", data.mEquippedWeapon);
  // 0x4F40: only weapon type 1 (cmpwi 1, beq).
  if (data.mEquippedWeapon == 1) {
    // 0x4F54: the launcher's model id must be valid.
    if (data.mGrenadeLauncher != kInvalidAssetId) {
      // 0x4F60-0x4FAC: CModelData(CStaticRes(id, the model's scale)) assigned into 0xc08 (4FC4).
      xc08_launcherModel = CModelData(CStaticRes(data.mGrenadeLauncher, GetModelData()->GetScale()));
    }
  }
}

// @ fn_72_36CC (0xF8): draws the launcher model at the R_wrist locator inside the actor's render
// alpha buffer pass.
void CSpacePirate::RenderLauncherModel(const CStateManager& mgr, const CTransform4f& xf,
                                       const CModelFlags& flags) const {
  // 0x36EC: the alpha buffer alpha; -1 means no destination-alpha pass.
  const int alpha = GetRenderAlphaBufferAlpha(mgr);
  if (alpha != -1) {
    // 0x3710: gpRender vtable slot 0x128 = SetDestinationAlpha.
    gpRender->SetDestinationAlpha(alpha);
  }
  // 0x3728: vtable slot 0xC8 = GetModelAlphau8; then the model's valid flag (0xc54).
  if (GetModelAlphau8(mgr) != 0 && xc08_launcherModel.valid()) {
    // 0x3754: the R_wrist locator (0xa88), 0x3764: xf * locator.
    const CTransform4f locatorXf = xf * GetScaledLocatorTransform(xa88_wristSeg);
    // 0x3788: the model renders with the actor lights (0xbc) and the caller's flags.
    xc08_launcherModel.data().Render(mgr, locatorXf, GetActorLights(), flags);
  }
  if (alpha != -1) {
    // 0x37A4: gpRender vtable slot 0x12C = DisableDestinationAlpha.
    gpRender->DisableDestinationAlpha();
  }
}

// @ fn_72_37C4 (Render): the space warp while warping in, the scoped time provider, the
// portal plane, the patterned render and the launcher model.
void CSpacePirate::Render(const CStateManager& mgr) const {
  // 0x37E0: the seconds mod 900 while alive (0x420 bit 0x40), else 0 (rodata B9C).
  const float time = mAlive ? CGraphics::GetSecondsMod900() : 0.f;
  // 0x3808: warp state 1 (cmpwi 1).
  if (xc04_warpPoint == 1) {
    // 0x3814-0x3830: sin(pi (rodata BC4) * mColor's alpha (u8 at 0x44f * rodata BB4 1/255)).
    const float warp = CMath::FastSinR(3.1415927f * mColor.GetAlpha());
    // 0x3840-0x385C: at the bounding box's centre.
    mgr.DrawSpaceWarp(PortPresentedPoint(GetBoundingBox().GetCenterPoint()), warp);
  }
  // 0x3868: the scoped time provider.
  CTimeProvider timeProvider(time);
  // 0x386C-0x3884: |the portal plane's d (0xc68)| > 0 (rodata B9C), fcmpo + ble.
  if (CMath::AbsF(mPortalPlane.GetConstant()) > 0.f) {
    // 0x3894: the model's world-space portal plane.
    GetModelData()->SetupWorldSpacePortalPlane(GetTransform(), mPortalPlane);
  }
  // 0x38A0: the patterned render.
  CPatterned::Render(mgr);
  // 0x38B4: the launcher model with the actor's transform (0x24) and draw flags (0xfc).
  RenderLauncherModel(mgr, GetTransform(), GetModelFlags());
}

// Retail fn_72_E3D8 (0xC0): the base class's touch first, then a force-field trigger's push
// onto the ragdoll's torso impulse (asm 0xE3F8-0xE47C).  The conditions, each one a branch that
// skips the push:
//   0xE3FC-0xE400: the ragdoll (0xb30) is there,
//   0xE404-0xE40C: the ragdoll's flag byte 0x70 carries 0x40 (CRagDoll::mPrimed: the DOL's
//     CRagDoll::Prime sets it, rlwimi r0,r3,6,25,25 at 0x80160610),
//   0xE410-0xE41C: the other actor is a CScriptTrigger,
//   0xE420-0xE428: the trigger's flag byte 0x20 carries 0x80 (CEntity::mActive),
//   0xE42C-0xE434: the trigger's word 0x1a0 carries 0x8000 (rlwinm. r0,r0,0,16,16 =
//     kTFL_DetectAI),
//   0xE438-0xE448: the trigger's float 0x19c is above zero (GetForceMagnitude()),
// and then the ragdoll's 0xb8, 0xbc and 0xc0 (CPirateRagDoll's torso impulse) add the trigger's
// 0x190, 0x194 and 0x198 (the force field, asm 0xE44C-0xE47C, three lfs/fadds/stfs).
// @ fn_72_E3D8
void CSpacePirate::Touch(CActor& actor, CStateManager& mgr) {
  TraceCall(*this, "Touch", 0);
  CPatterned::Touch(actor, mgr);
  CPirateRagDoll* ragdoll = xb30_ragDoll.get();
  if (ragdoll == nullptr) {
    return; // 0xE3FC-0xE400
  }
  if (!ragdoll->IsPrimed()) {
    return; // 0xE404-0xE40C
  }
  CScriptTrigger* trigger = TCastToPtr< CScriptTrigger >(&actor);
  if (trigger == nullptr) {
    return; // 0xE410-0xE41C
  }
  if (!trigger->GetActive()) {
    return; // 0xE420-0xE428
  }
  if ((trigger->GetTriggerFlags() & kTFL_DetectAI) == 0) {
    return; // 0xE42C-0xE434
  }
  // 0xE444 fcmpo / 0xE448 ble: a plain ble, so a NaN magnitude skips the push too.
  if (!(trigger->GetForceMagnitude() > 0.f)) {
    return; // 0xE438-0xE448
  }
  ragdoll->TorsoImpulse() += trigger->GetForceField(); // 0xE44C-0xE47C
}

// Retail fn_72_C538.
// @ fn_72_C538
void CSpacePirate::Death(CStateManager& mgr, const CVector3f& direction,
                         EScriptObjectState state) {
  if (!mAlive) {
    return;
  }
  CPatterned::Death(mgr, direction, state);
  TraceCall(*this, "Death", static_cast< int >(mAttachedActor != kInvalidUniqueId));
  if (mAttachedActor != kInvalidUniqueId) {
    BodyController()->CommandMgr().DeliverCmd(
        CBCKnockDownCmd(GetTransform().GetForward(), pas::kS_Two));
  }
}

// Retail fn_72_C684.
// @ fn_72_C684
void CSpacePirate::KnockBack(CStateManager& mgr, const CKnockBackInfo& info) {
  if ((x8f4_25_ceilingAmbush && mAlive) || xb30_ragDoll.get() != nullptr ||
      mAttachedActor != kInvalidUniqueId) { // fn_72_C684:17-18
    return;
  }
  mKnockBackController.EnableKnockBackPhysics(!x8f4_31_noKnockbackImpulseReset);
  mKnockBackController.EnableAnimReaction(CKnockBackMgr::kAR_KnockBack, IsOnGround());
  bool enableFreeze = true;
  if (IsIngPossessed() ||
      (x8f5_27_shadowPirate && !info.GetDamageInfo().GetWeaponMode().IsCharged() &&
       !info.GetDamageInfo().GetWeaponMode().IsComboed())) {
    enableFreeze = false;
  }
  mKnockBackController.EnableFreeze(enableFreeze);
  CPatterned::KnockBack(mgr, info);
  if (x8f5_27_shadowPirate) {
    if (mAlive) {
      const float power = info.GetDamageInfo().GetKnockBackPower(*GetDamageVulnerability(), 0.f);
      if (power >= 4.f && BodyController()->GetPercentageFrozen() != 1.f) {
        mAlphaDelta = 1.f;
        xb7c_cloakDelayTimer = CMath::Clamp(0.f, xb7c_cloakDelayTimer + 0.1f * power, 1.f);
        xb90_maxCloakAlpha = 0.5f;
        mgr.ActorModelParticles()->StartElectric(*this);
        xb80_electricParticleTimer = 1.f + xb7c_cloakDelayTimer;
      }
    } else {
      mAlphaDelta = 1.f;
      xb90_maxCloakAlpha = 1.f;
      xb8c_minCloakAlpha = 0.f;
      mgr.ActorModelParticles()->StartElectric(*this);
      xb80_electricParticleTimer = 2.f;
    }
  }
  const CKnockBackMgr::EAnimReaction reaction = mKnockBackController.GetActiveReaction();
  TraceCall(*this, mAlive ? "KnockBack alive reaction" : "KnockBack dead reaction",
            static_cast< int >(reaction));
  if (mAlive) {
    if (reaction == CKnockBackMgr::kAR_Hurled) {
      mStateMachine->SetState(mgr, *this, rstl::string_l("GetUpNow"));
      // The handle goes to 0xc00 (asm 0xC944), which fn_72_2EA8 reads.
      xc00_ = CSfxManager::AddEmitter(mData.mHurledSfx, GetTranslation(), 127,
                                      GetCurrentAreaId().Value(), false, false,
                                      CSfxManager::kMedPriority);
    }
  } else if (!x8f5_30_floatingCorpse && reaction == CKnockBackMgr::kAR_Hurled) {
    const CKnockBackMgr::EFollowUp followUp = mKnockBackController.GetFollowUp();
    if (followUp != CKnockBackMgr::kFU_ExplodeDeath && followUp != CKnockBackMgr::kFU_IceDeath &&
        followUp != CKnockBackMgr::kFU_BurnDeath &&
        followUp != CKnockBackMgr::kFU_LaggedBurnDeath) {
      xc00_ = CSfxManager::AddEmitter(mData.mDeathSfx, GetTranslation(), 127,
                                      GetCurrentAreaId().Value(), false, false,
                                      CSfxManager::kMedPriority); // asm 0xC9B8
    }
  }
}

namespace {

// Retail fn_72_10E50 builds these two tables once; the order is the table's. Own members are
// cast the way CAIMannedTurret does it (the base-class pointer-to-member types differ only by
// the class), which is fine for non-virtual members of a single-inheritance class.
#define SP_TRIGGER(name)                                                                           \
  { #name, reinterpret_cast< CPatterned::StateMachine::TriggerFunc >(&CSpacePirate::name) }
#define SP_STATE(name)                                                                             \
  { #name, reinterpret_cast< CPatterned::StateMachine::StateFunc >(&CSpacePirate::name) }

// fn_72_10E50 runs straight-line up to its first call: `tools/fsm_ctor_tables.py <asm dir>
// fn_72_10E50 lbl_72_data_1D8:39 lbl_72_data_604:37` replays it (271 stores, every entry
// {0, 0xFFFFFFFF, fn}) and the names and functions match the tables below in order.
// @ fn_72_10E50
CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    SP_TRIGGER(Stuck),           SP_TRIGGER(PatternShagged),  SP_TRIGGER(HearShot),
    SP_TRIGGER(HearPlayer),      SP_TRIGGER(AggressionCheck), SP_TRIGGER(CoverCheck),
    SP_TRIGGER(CoverFind),       SP_TRIGGER(CoverBlown),      SP_TRIGGER(CoverNearlyBlown),
    SP_TRIGGER(CoveringFire),    SP_TRIGGER(ShouldAttack),    SP_TRIGGER(LineOfSight),
    SP_TRIGGER(PatternOver),     SP_TRIGGER(SpotPlayer),      SP_TRIGGER(ShouldDodge),
    SP_TRIGGER(ShouldRetreat),   SP_TRIGGER(InRange),         SP_TRIGGER(ShouldCrouch),
    SP_TRIGGER(ShouldMove),      SP_TRIGGER(ShotAt),          SP_TRIGGER(Attacked),
    SP_TRIGGER(HasTargetingPoint), SP_TRIGGER(ShouldWallHang), SP_TRIGGER(AnimOver),
    SP_TRIGGER(ShouldStrafe),    SP_TRIGGER(ShouldSpecialAttack), SP_TRIGGER(StartAttack),
    SP_TRIGGER(BreakAttack),     SP_TRIGGER(LostInterest),    SP_TRIGGER(BounceFind),
    SP_TRIGGER(OffLine),         SP_TRIGGER(Landed),          SP_TRIGGER(ShouldJumpBack),
    SP_TRIGGER(Leash),           SP_TRIGGER(HasAttackPattern), SP_TRIGGER(IsAmbushing),
    SP_TRIGGER(ShouldWarpIn),    SP_TRIGGER(ShouldLaunchGrenade), SP_TRIGGER(InProjectileRange),
};

CPatterned::StateMachine::SStateFunction skStates[] = {
    SP_STATE(Ambushing), SP_STATE(WarpIn),      SP_STATE(WarpOut),    SP_STATE(PostWarpOut),
    SP_STATE(Attack),    SP_STATE(Crouch),      SP_STATE(CoverAttack), SP_STATE(Halt),
    SP_STATE(Run),       SP_STATE(PathFind),    SP_STATE(Patrol),     SP_STATE(TargetPatrol),
    SP_STATE(Shuffle),   SP_STATE(TurnAround),  SP_STATE(Dodge),      SP_STATE(Lurk),
    SP_STATE(Taunt),     SP_STATE(Cover),       SP_STATE(Dead),       SP_STATE(TargetCover),
    SP_STATE(TargetPlayer), SP_STATE(Approach), SP_STATE(WallHang),   SP_STATE(WallDetach),
    SP_STATE(GetUp),     SP_STATE(Generate),    SP_STATE(Skid),       SP_STATE(DoubleSnap),
    SP_STATE(JumpBack),  SP_STATE(Bounce),      SP_STATE(PathFindEx), SP_STATE(Enraged),
    SP_STATE(Jump),      SP_STATE(Deactivate),  SP_STATE(LaunchGrenade), SP_STATE(Captured),
    SP_STATE(RemoveFromWorld),
};

#undef SP_TRIGGER
#undef SP_STATE

} // namespace

// @ fn_72_F0A0 (0x6C): CPatterned's tables first, then its own 39 triggers (0x27) and 37 states
// (0x25). The state bodies live in PortCSpacePirateStates.cpp, the triggers in
// PortCSpacePirateTriggers.cpp.
void CSpacePirate::SetupStateMachine(CStateManager& mgr) {
  StateMachine* stateMachine = mStateMachine.get();
  CPatterned::SetupStateMachine(mgr);
  stateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  stateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
}

// @ fn_72_E120 (SetAttackTarget): the new target, the squad manager's copy of it, a fresh
// burst type and no attack time left.
void CSpacePirate::SetAttackTarget(CStateManager& mgr, TUniqueId target) {
  mAttackTarget = target;     // 0xE12C lhz 0x0(r5), 0xE138 sth 0xa94
  UpdateTeamTarget(mgr);      // 0xE13C bl fn_72_DE5C
  mBurstFire.SetBurstType(1); // 0xE148 stw 1, 0xa98 (CBurstFire's first field)
  mAttackRemTime = 0.f;       // 0xE150 stfs 0, 0xa90 (rodata B9C)
}

// @ fn_72_3A0C (PreRenderAllViewports): with a live ragdoll the ragdoll renders instead of
// the model and the portal system is updated; otherwise the patterned path.
void CSpacePirate::PreRenderAllViewports(CStateManager& mgr) {
  // 0x3A28-0x3A3C: a primed ragdoll (+0x70 0x40) renders instead (0x3A50, rodata B6C = 0.2) and
  // 0x3A5C updates the portal system state.
  if (xb30_ragDoll.get() != nullptr && xb30_ragDoll->IsPrimed()) {
    xb30_ragDoll->PreRenderAllViewports(*this, 0.2f);
    UpdatePortalSystemState(mgr);
  } else {
    CPatterned::PreRenderAllViewports(mgr); // 0x3A6C: the patterned path.
  }
}

// @ fn_72_64
const CHealthInfo* CSpacePirate::GetHealthInfo() const {
  return const_cast< CSpacePirate* >(this)->HealthInfo(); // fn_72_64
}

// @ fn_72_3AB4: a pirate that has warped (0xc04 is no longer -1) is pass-through; otherwise
// the patterned default decides.
const CDamageVulnerability* CSpacePirate::GetDamageVulnerability() const {
  if (xc04_warpPoint != -1) { // 0x3AC0 lwz 0xc04, 0x3AC4 cmpwi -1
    // 0x3ACC bl PassThroughVulnerabilty (the DOL's pass-through vulnerability record).
    return &CDamageVulnerability::PassThroughVulnerabilty();
  }
  // 0x3AD4 bl GetDamageVulnerability__10CPatternedCFv.
  return CPatterned::GetDamageVulnerability();
}

// @ fn_72_3A88: a tail call through the vtable slot 0x40, which for CSpacePirate is the
// no-argument GetDamageVulnerability (fn_72_3AB4) - the three arguments are not read.
const CDamageVulnerability* CSpacePirate::GetDamageVulnerability(const CVector3f& position,
                                                                 const CVector3f& direction,
                                                                 const CDamageInfo& damage) const {
  return GetDamageVulnerability();
}

// @ fn_72_17C8 (GetTouchBounds): the ragdoll's own bounds while a ragdoll is live, otherwise
// the actor's bounding box (fn_72_1854).
rstl::optional_object< CAABox > CSpacePirate::GetTouchBounds() const {
  // 0x17DC-0x1834: with a ragdoll whose byte 0x70 has 0x40 (0x17EC mPrimed) and 0x80 (0x17F4
  // mRenderBoundsValid), the touch bounds are the ragdoll's cached render bounds (+0x58..0x6c).
  const CRagDoll* ragdoll = xb30_ragDoll.get();
  if (ragdoll != nullptr && ragdoll->IsPrimed() && ragdoll->IsRenderBoundsValid()) {
    return ragdoll->GetCachedRenderBounds();
  }
  // 0x183C bl fn_72_1854: the actor's own bounding box (GetBoundingBox).
  return GetActorTouchBounds();
}

// @ fn_72_35CC (GetSortingBounds): the model's bounds in the actor's transform, shrunk to a
// quarter of their size around their centre.
CAABox CSpacePirate::GetSortingBounds(const CStateManager& mgr) const {
  // 0x35F0 bl GetBounds: the model's bounds in the actor's transform (this+0x24).
  const CAABox bounds = GetModelData()->GetBounds(GetTransform());
  // 0x362C bl GetCenterPoint.
  const CVector3f center = bounds.GetCenterPoint();
  // 0x364C-0x3684: the half size = 0.25 (rodata BC0) * the bounds' size.
  const CVector3f half = (bounds.GetMaxPoint() - bounds.GetMinPoint()) * 0.25f;
  // 0x36B4 bl CAABox: (centre - half, centre + half).
  return CAABox(center - half, center + half);
}

// Retail fn_72_3B28: a jump table over the event type; every other event is CPatterned's.
// @ fn_72_3B28
void CSpacePirate::DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node,
                                   EUserEventType type, float dt) {
  bool handled = false;
  TraceCall(*this, "UserAnimEvent", static_cast< int >(type));
  switch (type) {
  case kUE_Projectile:
    // Retail fn_72_4174 launches the launcher's grenade here.
    SpawnGrenade(mgr);
    handled = true;
    break;
  case kUE_BeginAction:
    RemoveMaterial(kMT_Solid, mgr);
    mAllEnergyDrained = true;
    handled = true;
    break;
  case kUE_EndAction:
    x8f9_30_closeMelee = false;
    handled = true;
    break;
  case kUE_DeGenerate:
  case kUE_BecomeRagDoll:
    if (x8f4_29_onlyAttackInRange || GetHealthInfo()->GetHP() <= 0.f) {
      xb2c_ragdollDelayTimer = mgr.Random()->Float() * 0.05f + 0.001f;
    }
    handled = true;
    break;
  case kUE_IkLock:
    if (!xb34_ikChain.GetActive()) {
      const CAnimData& animData = *GetModelData()->GetAnimationData();
      const CSegId bone = animData.GetLocatorSegId(node.GetLocatorName());
      if (bone != CSegId::Invalid()) {
        CTransform4f xf = GetLctrTransform(bone);
        xb34_ikChain.Activate(animData, bone, xf);
        x8f9_28_satUp = true;
      }
    }
    handled = true;
    break;
  case kUE_IkRelease:
    xb34_ikChain.Deactivate();
    handled = true;
    break;
  case kUE_ScreenShake:
    SendScriptMsgs(kSS_Play, mgr, kSM_None);
    handled = true;
    break;
  case kUE_FadeOut:
    if (x8f5_27_shadowPirate) {
      mAlphaDelta = -0.8f;
      mgr.ActorModelParticles()->StartElectric(*this);
      xb80_electricParticleTimer = 1.f;
    }
    handled = true;
    break;
  case kUE_BreakLockOn:
    handled = mAlive;
    break;
  default:
    break;
  }
  if (!handled) {
    CPatterned::DoUserAnimEvent(mgr, node, type, dt);
  }
}

// Retail fn_72_E318.
// @ fn_72_E318 (0xC0): a live pirate hears a noise inside the data's hearing radius (0x7d4)
// and inside the detection height range (0x3d4, the squared test at asm 0xE384), which sets the
// 0x8f6 mask 0x40 bit; a player-fire noise (type 0) also sets the 0x8f7 mask 0x20 bit and
// remembers the noise position in 0xbb4/0xbb8/0xbbc (asm 0xE3A8-0xE3C8). The trace's argument is
// the arm: 0 = the pirate is dead, 1 = the noise is out of the hearing radius, 2 = the height
// range test failed, 3 = heard. The 2026-10-21 runs proved 0 (`hurt u04d0 100` kills the pirate
// and the next shot reaches it), 1 and 3 (the awake trooper of 06_sand_alamofront, with its
// 100-unit radius, hears the player's shot). Arm 2 needs a pirate whose detection height range
// is set, and none of the 65 shipped SpacePirate placements has one (tools/scan_pirt_props.py
// reads the patterned block's property 0x513f04b8: all 65 are 0.0), so that arm is unreachable.
bool CSpacePirate::Listen(CStateManager& mgr, const CVector3f& position, EListenNoiseType type) {
  bool heard = false;
  if (mAlive) {
    const CVector3f delta = position - GetTranslation();
    const float radius = mData.mHearingRadius;
    if (delta.MagSquared() < radius * radius &&
        (mDetectionHeightRange == 0.f ||
         delta.GetZ() * delta.GetZ() < mDetectionHeightRange * mDetectionHeightRange)) {
      heard = true;
      x8f6_25_hearNoise = true;
    }
    if (type == kLNT_PlayerFire) {
      x8f7_26_hearPlayerFire = true;
      xbb4_ = position;
    }
    // The trace's argument is the arm: 0 = the pirate is dead, 1 = the noise is out of the
    // hearing radius, 2 = the height range test failed, 3 = heard.
    TraceCall(*this, "Listen", heard ? 3 : 1);
  } else {
    TraceCall(*this, "Listen", 0);
  }
  return heard;
}

// @ fn_72_E090 (GetOrigin, 0x1C): the pirate's own position. Branchless; the team AI of
// 07_sand_alamoportalobservation L3 called it 400+ times in the gdb run.
CVector3f CSpacePirate::GetOrigin(const CStateManager& mgr, const CTeamAiRole& role,
                                  const CVector3f& aimPos) const {
  // 0xE090-0xE0A4: this+0x54/0x58/0x5c, the actor's translation.
  return GetTranslation();
}

// @ fn_72_3AE8.
uchar CSpacePirate::GetModelAlphau8(const CStateManager& mgr) const {
  if (x8f5_27_shadowPirate) {
    return static_cast< uchar >(255.f * xb88_shadowPirateAlpha);
  }
  return mColor.GetAlphau8();
}

// fn_72_4F0C: the null arm is dead in the shipped data. The optional is built only for a valid
// projectile id (the ctor's 0xFB34-0xFB40) and every one of the 65 PIRT placements carries the
// projectile property 0xef485db9 = 0x681ed507 (tools/scan_pirt_props.py with that property added,
// 2026-10-21); the built arm ran in every run.
// @ fn_72_4F0C
CProjectileInfo* CSpacePirate::ProjectileInfo() {
  // 0x4F0C-0x4F14: the optional's built flag (0xbfc); set -> this+0xbd4 (0x4F18), clear -> null (0x4F20).
  return mProjectileInfo.valid() ? &mProjectileInfo.data() : nullptr;
}

// @ fn_72_343C (GetScanVisorRenderBounds): the model's vertex bounds, scaled by the model's
// own scale and moved into world space.
CAABox CSpacePirate::GetScanVisorRenderBounds(const CStateManager& mgr) const {
  // 0x3490: the model's anim data -> the vertex bounds in local space.
  const CAABox local = GetModelData()->GetAnimationData()->CalcBoundingBoxFromModelVerts();
  // 0x34D4: the model's scale.
  const CVector3f scale = GetModelData()->GetScale();
  // 0x34F0: the negated translation.
  const CVector3f offset = -GetTranslation();
  // 0x3514/0x3520: a scale transform and a translation transform; 0x3530 and 0x3540 multiply them
  // with the actor's transform.
  const CTransform4f xf =
      CTransform4f::Translate(offset) * GetTransform() * CTransform4f::Scale(scale);
  // 0x3550: the transformed box.
  return local.GetTransformedAABox(xf);
}

// @ fn_72_339C (ScanVisorRender): the body model with the scan flags and no lights, then the
// launcher model.
void CSpacePirate::ScanVisorRender(const CStateManager& mgr, const CTransform4f& xf,
                                   const CModelFlags& flags) const {
  // 0x33CC-0x33F0: unless the model data is null (no anim data at +0x10, no normal model at +0x28).
  if (!GetModelData()->IsNull()) {
    // 0x3404: which model 0, no lights.
    GetModelData()->Render(CModelData::kWM_Normal, xf, nullptr, flags);
  }
  // 0x3418: the launcher model.
  RenderLauncherModel(mgr, xf, flags);
}

// Retail fn_72_18BC (TryToBeCaptured): the pirate switches to the "Captured" state and says so.
// @ fn_72_18BC
// arms and the run: docs/module-analysis/SpacePirate_helpers.md (section 3).
bool CSpacePirate::TryToBeCaptured(CStateManager& mgr) {
  // 0x18D8: the state name "Captured" (rodata 0xF42), set through the state machine's virtual
  // slot 0x20 (SetState).
  mStateMachine->SetState(mgr, *this, rstl::string_l("Captured"));
  return true; // 0x1918 li r3, 1
}

// @ fn_72_10A10
// CSpacePirateData's constructor: the loader's arguments stored in order (0x10AB0-0x10C04), then
// the launcher block copied in (0x10C08 fn_72_108D8, a copy constructor; see PortInitLauncherBlock).
// The host struct keeps only the fields the module reads: retail's +0xbc (unknown_0x8708b7d3, loader
// stack 0xC24 -> 0x10BFC stfs 0xbc) is never read, and +0xc4 is the loader's constant 0.5 (0xA7C, rodata B44), which
// UpdateGuardPosition (0xDB14) reads as its literal.
static void PortInitSpacePirateData(CSpacePirateData& data, const SLdrSpacePirate& sldrThis) {
  data.mAggressiveness = sldrThis.aggressiveness;       // 0x10AB0 stfs f1, 0x0
  data.mCoverCheck = sldrThis.coverCheck;               // 0x10ABC stfs f2, 0x4
  data.mSearchRadius = sldrThis.searchRadius;           // 0x10AC4 stfs f3, 0x8
  data.mFallBackCheck = sldrThis.fallBackCheck;         // 0x10ACC stfs f4, 0xc
  data.mFallBackRadius = sldrThis.fallBackRadius;       // 0x10AD4 stfs f5, 0x10
  data.mHearingRadius = sldrThis.hearingRadius;         // 0x10ADC stfs f6, 0x14
  data.mFlags = sldrThis.flags;                         // 0x10AE4 stw r4, 0x18
  data.mUnknownCe670970 = sldrThis.unknown_0xce670970;  // 0x10AF0 stb r5, 0x1c
  data.mProjectile = sldrThis.projectile;               // 0x10AF8 stw r6, 0x20
  data.mProjectileDamage = LdrToDamageInfo(sldrThis.projectileDamage); // 0x10B00-0x10B40 0x24..0x3e
  data.mProjectileSfx = static_cast< ushort >(sldrThis.sound_Projectile); // 0x10B48 sth r8, 0x40
  data.mBladeDamage = LdrToDamageInfo(sldrThis.bladeDamage); // 0x10B4C-0x10B6C 0x44..0x5e
  data.mKneelAttackChance = sldrThis.kneelAttackChance; // 0x10B70 stfs f7, 0x60
  data.mKneelAttackShot = sldrThis.kneelAttackShot;     // 0x10B74 stw r10, 0x64
  data.mKneelAttackDamage = LdrToDamageInfo(sldrThis.kneelAttackDamage); // 0x10B7C-0x10BBC 0x68..0x82
  data.mDodgeCheck = sldrThis.dodgeCheck;               // 0x10BC0 stfs f8, 0x84
  data.mImpactSfx = static_cast< ushort >(sldrThis.sound_Impact); // 0x10BC4 sth 0x88
  data.mIntraBurstTime = sldrThis.intraBurstShotTime;   // 0x10BC8 stfs 0x8c
  data.mIntraBurstVariation = sldrThis.intraBurstShotVariation; // 0x10BCC stfs 0x90
  data.mIngIntraBurstTime = sldrThis.unknown_0x5080162a;       // 0x10BD0 stfs 0x94
  data.mIngIntraBurstVariation = sldrThis.unknown_0xc78b40e0;  // 0x10BD4 stfs 0x98
  data.mAlertSfx = static_cast< ushort >(sldrThis.sound_Alert); // 0x10BD8 sth 0x9c
  data.mGunTrackDelay = sldrThis.gunTrackDelay;         // 0x10BDC stfs 0xa0
  data.mFirstBurstCount = static_cast< uint >(sldrThis.unknown_0x1b454a27); // 0x10BE0 stw 0xa4
  data.mCloakOpacity = sldrThis.cloakOpacity;           // 0x10BE4 stfs 0xa8
  data.mMaxCloakOpacity = sldrThis.maxCloakOpacity;     // 0x10BE8 stfs 0xac
  data.mBreakDodgeMinTime = sldrThis.breakDodgeMinTime; // 0x10BEC stfs 0xb0
  data.mBreakDodgeMaxTime = sldrThis.breakDodgeMaxTime; // 0x10BF0 stfs 0xb4
  data.mHurledSfx = static_cast< ushort >(sldrThis.sound_Hurled); // 0x10BF4 sth 0xb8
  data.mDeathSfx = static_cast< ushort >(sldrThis.sound_Death);   // 0x10BF8 sth 0xba
  data.mAvoidDistance = sldrThis.avoidDistance;         // 0x10C00 stfs 0xc0 (loader stack 0xC28)
}

// @ fn_72_10C64
// The launcher block (data +0xc8): the equipped weapon (0x10C80 stw r4, 0x0), the launcher model
// (0x10C90 stw r5, 0x4), the bouncy-grenade data copied in (0x10CA8 fn_72_10954, a copy
// constructor), the grenade count (0x10CAC stw r7, 0x58) and four floats (0x10CB4-0x10CC0).
// The loader's arguments (fn_72_140 0x8D4-0x8F4) come from the weapon data in property order:
// 0xC2C equipped weapon, 0xC30 launcher, 0xC34 count, f1/f2 = 0xC64/0xC68 the launch speeds,
// f3/f4 = 0xC38/0xC3C the attack distances. The grenade data itself is fn_72_C0C (the host's
// CBouncyGrenadeData constructor, CBouncyGrenade.hpp), built from the loader's 0x828-0x8D0: mass
// 0xC5C, speed 0xC60, the damage 0xC40, bounces 0xC6C, the explosion 0xC50 twice, trail 0xC58,
// effect 0xC54, the sfx 0xC70/0xC74, and the constants 0.1 (rodata B3C, 0x84C `addi r9`) and
// 150 (rodata B40, 0x844 `addi r8`) loaded as f3/f5 and f4/f6 (0x874-0x890) for +0x3c..+0x48.
static void PortInitLauncherBlock(CSpacePirateData& data, const SLdrSpacePirateWeaponData& weapon) {
  data.mEquippedWeapon = static_cast< uint >(weapon.equippedWeapon); // block +0x00
  data.mGrenadeLauncher = weapon.grenadeLauncher;                    // block +0x04
  data.mGrenadeMass = weapon.grenadeMass;                            // grenade +0x00 (0xC0C f1)
  data.mGrenadeSpeed = weapon.unknown_0xed086ce0;                    // grenade +0x04 (0xC14 f2)
  data.mGrenadeDamage = LdrToDamageInfo(weapon.grenadeDamage);       // grenade +0x08..0x22
  data.mGrenadeNumBounces = static_cast< uint >(weapon.grenadeNumBounces); // grenade +0x24 (r5)
  data.mGrenadeExplosion = weapon.grenadeExplosion;                  // grenade +0x28/+0x2c (r6/r7)
  data.mGrenadeTrail = weapon.grenadeTrail;                          // grenade +0x30 (r8 = 0xC58)
  data.mGrenadeEffect = weapon.grenadeEffect;                        // grenade +0x34 (r9 = 0xC54)
  data.mGrenadeBounceSfx = static_cast< ushort >(weapon.sound_GrenadeBounce);   // grenade +0x38
  data.mGrenadeExplodeSfx = static_cast< ushort >(weapon.sound_GrenadeExplode); // grenade +0x3a
  data.mGrenadeSfxFalloff = 0.1f;  // grenade +0x3c = f3 = rodata B3C (0x880 lfs f3, 0x0(r9))
  data.mGrenadeSfxMaxDist = 150.f; // grenade +0x40 = f4 = rodata B40 (0x874 lfs f4, 0x0(r8))
  data.mGrenadeX44 = 0.1f;         // grenade +0x44 = f5 (0x890 fmr f5, f3)
  data.mGrenadeX48 = 150.f;        // grenade +0x48 = f6 (0x884 fmr f6, f4)
  data.mGrenadeX4c = true;         // grenade +0x4c bit 0x80 (0x85C li r10, 1; 0x8B0 stw r10, 0xc)
  data.mGrenadeCount = weapon.unknown_0xa95a025b;              // 0x10CAC stw r7, 0x58
  data.mGrenadeMinLaunchSpeed = weapon.grenadeMinLaunchSpeed;  // 0x10CB4 stfs f28 (f1), 0x5c
  data.mGrenadeMaxLaunchSpeed = weapon.grenadeMaxLaunchSpeed;  // 0x10CB8 stfs f29 (f2), 0x60
  data.mGrenadeMinAttackDist = weapon.grenadeMinAttackDist;    // 0x10CBC stfs f30 (f3), 0x64
  data.mGrenadeMaxAttackDist = weapon.grenadeMaxAttackDist;    // 0x10CC0 stfs f31 (f4), 0x68
}

// Retail fn_72_109C: SLdrSpacePirate's constructor, the loader's defaults (the call at fn_72_140
// 0x160, on the loader's stack object at sp+0x714). The generated header's inline constructor
// stores the same floats, damage infos and asset ids, except the sound ids: the guest's default
// for every sound id is -1 (no sound), the header's 0. Those -1 stores are written over it here.
// The generated struct is 0xC bytes larger than the guest's from the actor-parameters block on, so
// guest 0x4a8 is the port's sound_Projectile (0x4b4): sound_Projectile 0x4a8 (`li r0,-0x1`
// 0x000010DC, `stw` 0x000010E4), then the run the `li r0,-0x1` at 0x000010FC feeds, sound_Impact
// 0x4d8 (0x00001104), sound_Alert 0x4ec (0x00001108), sound_Hurled 0x508 (0x0000110C) and
// sound_Death 0x50c (0x00001110); and in the
// weapon-data block's constructor (fn_72_13A4, called at 0x00001114) the grenade pair +0x44/+0x48
// (`li r0,-0x1` 0x000013C8, `stw` 0x00001400/0x00001410). The constructors' other -1 stores (0x494,
// 0x4c0; the sub-constructor's 0x4, 0x24, 0x28, 0x2c) are CAssetIds, already kInvalidAssetId from
// the headers' initializer lists. A property in the instance's data overwrites any of them.
static void x109C_SoundDefaults(SLdrSpacePirate& sldr) {
  sldr.sound_Projectile = -1;                // 0x000010E4 (0x4a8)
  sldr.sound_Impact = -1;                    // 0x00001104 (0x4d8)
  sldr.sound_Alert = -1;                     // 0x00001108 (0x4ec)
  sldr.sound_Hurled = -1;                    // 0x0000110C (0x508)
  sldr.sound_Death = -1;                     // 0x00001110 (0x50c)
  sldr.weaponData.sound_GrenadeBounce = -1;  // fn_72_13A4 0x00001400 (+0x44)
  sldr.weaponData.sound_GrenadeExplode = -1; // fn_72_13A4 0x00001410 (+0x48)
}

// Retail fn_72_140. Property ids and order are the generated SLdrSpacePirate reader's, which the
// analysis checked against the REL id by id.
// Coverage (sp20, 2026-10-22): every arm but `!modelData` (retail 0x7A4, an empty optional when the
// ANCS is invalid) ran; no shipped placement has one, so that arm is data-dead. Retail's null check
// after `new` (0xAE0) has no host arm (the host's allocator does not return null).
// @ fn_72_140
static CEntity* PortLoadSpacePirate(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrSpacePirate sldrThis;
  x109C_SoundDefaults(sldrThis);  // fn_72_109C's -1 sound ids (0x160)
#include "MetroidPrime/ScriptLoader/SLdrSpacePirate.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }

  // 0x828-0x8F8: the bouncy-grenade data (the weapon data's damage at 0xC40, then fn_72_C0C) and
  // the launcher block (fn_72_10C64); 0xABC: fn_72_10A10 takes the rest of the properties.
  CSpacePirateData data;
  PortInitSpacePirateData(data, sldrThis);
  PortInitLauncherBlock(data, sldrThis.weaponData);

  return rs_new CSpacePirate(mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
                             LdrToEntityInfo(info, sldrThis.editorProperties),
                             LdrToTransform4f(sldrThis.editorProperties), *modelData,
                             LdrToActorParameters(sldrThis.actorInformation),
                             LdrToPatternedInfo(sldrThis.patterned, &sldrThis.ingPossessionData),
                             data);
}

static SSpacePirate_FuncPtrs REL_loader_SpacePirate;

// @ fn_72_D4
// Fills the module's table (bss_24): the loader (0xFC stwu fn_72_140), the attach PMF (data_8C0,
// 0x114-0x11C) and the detach PMF (data_8CC, 0x120-0x128), then registers it with the DOL's
// SetSSpacePirate_FuncPtrs (0x12C fn_80200E3C). The PMFs point at the Port* bodies:
// AttachActorToPirate and DetachActorFromPirate are the proxies that call through this table.
static void PortRegisterSpacePirateFuncPtrs() {
  REL_loader_SpacePirate.mLoadSpacePirate = PortLoadSpacePirate;
  REL_loader_SpacePirate.mAttachActor = &CSpacePirate::PortAttachActor;
  REL_loader_SpacePirate.mDetachActor = &CSpacePirate::PortDetachActor;
  SetSSpacePirate_FuncPtrs(&REL_loader_SpacePirate);
}

// @ fn_72_B4
// The module's entry: fn_72_D4 (0xC0) and nothing else.
void RELMain() { PortRegisterSpacePirateFuncPtrs(); }

// @ fn_72_90
// The module's exit: SetSSpacePirate_FuncPtrs(nullptr) (0x98 li r3, 0; 0xA0 fn_80200E3C).
void RELExit() { SetSSpacePirate_FuncPtrs(nullptr); }
