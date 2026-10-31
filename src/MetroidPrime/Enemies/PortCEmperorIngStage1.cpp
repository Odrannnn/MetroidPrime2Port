// Host body for the EmperorIngStage1 REL (module 16). The state machine's tables are registered
// (fn_16_8AF4 installs the 6 triggers, 12 states and 3 code functions fn_16_BF88 builds). Ported:
// the tentacles' build, motion, effects, hits-to-retraction bookkeeping and contact damage; the
// states TrackPlayer, Attack, ProjectileAttack, Taunt, JumpRope, BeginHeartStage, HeartStage and
// EndHeartStage; the heart stage's beam (FireBeam), the heart's damage reaching the boss
// (fn_16_51D4), the charged projectile of DoUserAnimEvent, the boss's own sound handling
// (fn_16_7EA8), DoUserAnimEvent's cases 7, 17, 22, 27 and 28 and the boss's own render path
// (fn_16_531C, with fn_16_1D38's draw flags). What is left over logs once
// and names the REL function behind it (WarpAttack, TauntAttack) -
// docs/module-analysis/EmperorIngStage1.md has the map.
//
// Which methods the REL overrides is not guessed: its vtable lbl_16_data_3C8 lists them by
// address, and the slots line up with CPatterned's declaration order (the inherited entries are
// spelled out by name in the same table). The tiny accessors at the head of the module's .text
// (fn_16_B994, fn_16_B9A4, fn_16_B9AC, fn_16_B9B4, fn_16_B9BC, fn_16_B9CC, fn_16_B9C4, fn_16_B9DC,
// fn_16_B9E8, fn_16_B9F4 and fn_16_B994's neighbours in EmperorIngStage1Accessors.s) have
// four-to-twelve-instruction bodies, so they are real rather than stand-ins. Two of the slots
// the module's vtable replaces are the host base's own, so the port leaves them to the
// base: slot 13
// (fn_16_60) is CActor::GetHealthInfo and slot 16 (fn_16_B958) is CActor::GetTouchBounds
// (the host's CPatterned::GetTouchBounds returns GetBoundingBox(), the retail's body).
#include "MetroidPrime/Enemies/CEmperorIngStage1.hpp"

#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CActorModelParticles.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CAnimRes.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "Collision/CMaterialFilter.hpp"
#include "Collision/CRayCastResult.hpp"
#include "MetroidPrime/CParticleGenInfo.hpp"
#include "MetroidPrime/CParticleGenInfoGeneric.hpp"
#include "Kyoto/Particles/CElementGen.hpp"

#include "Kyoto/Animation/CPASAnimParm.hpp"
#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "Kyoto/Animation/CPASDatabase.hpp"

#include "Kyoto/Text/CStringTable.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Enemies/CPatternedInfo.hpp"
#include "MetroidPrime/CAnimPlaybackParms.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CBeamProjectile.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "MetroidPrime/Weapons/CImpactVisorEffect.hpp"
#include "MetroidPrime/Weapons/CEnergyProjectile.hpp"
#include "MetroidPrime/Weapons/CPlasmaProjectile.hpp"
#include "MetroidPrime/Weapons/CWeaponAssetInfo.hpp"
#include "MetroidPrime/Weapons/CBeamInfo.hpp"

#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/Animation/CAnimTreeNode.hpp"
#include "Kyoto/Animation/CCECharacterInfo.hpp"
#include "Kyoto/Animation/CSkinnedModel.hpp"
#include "Kyoto/Math/CloseEnough.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Math/CUnitVector3f.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#include "MetroidPrime/CParticleDatabase.hpp"
#include "MetroidPrime/Weapons/CShockWave.hpp"
#include "rstl/StringExtras.hpp"
#include "rstl/algorithm.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrEmperorIngStage1.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CScriptEffect.hpp"

#include <stdio.h>
#include <string.h>

namespace {

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
  fprintf(stderr, "[emp1] %s is a port stand-in (see PortCEmperorIngStage1.cpp)\n", what);
}

// The same, for the state machine's own functions: the message names the retail function the
// entry stands in for, so the log says which of the module's 21 FSM functions the run reached.
void StandInFn(const char* fn) {
  static const char* sSeen[32] = {};
  static int sCount = 0;
  for (int i = 0; i < sCount; ++i) {
    if (sSeen[i] == fn) {
      return;
    }
  }
  if (sCount < 32) {
    sSeen[sCount++] = fn;
  }
  fprintf(stderr, "[emp1] stand-in %s\n", fn);
}

// The body's three FSM tables. Retail builds them in a static initialiser (fn_16_BF88, .ctors at
// 0x0000C2A4): the tables are lbl_16_data_19C (6 triggers, .data 0x19C), lbl_16_data_94 (12
// states, .data 0x94) and lbl_16_data_220 (3 codes, .data 0x220), each entry 16 bytes
// {name, 0, 0xFFFFFFFF, fn}, and the function pointers come from the three-word records
// {0, 0xFFFFFFFF, fn} in .data: the states' twelve at 0x4..0x88 (fn_16_BF88's stores at
// 0x0000C0C0..0x0000C124 and 0x0000C214), the triggers' six at 0x154..0x190 (0x0000C234..
// 0x0000C284) and the codes' three at 0x1FC..0x214 (0x0000C284..0x0000C294).
//
// On the host the table addresses are irrelevant: the state machine matches a node by its name,
// so the name strings are retail's and the entry's function is the module's member function.
// The members are cast the way the DigitalGuardian and SpacePirate ports do it.
#define EMP1_TRIGGER(name)                                                                          \
  { #name, reinterpret_cast< CPatterned::StateMachine::TriggerFunc >(&CEmperorIngStage1::name) }
#define EMP1_STATE(name)                                                                            \
  { #name, reinterpret_cast< CPatterned::StateMachine::StateFunc >(&CEmperorIngStage1::name) }
#define EMP1_CODE(name)                                                                             \
  { #name, reinterpret_cast< CPatterned::StateMachine::CodeFunc >(&CEmperorIngStage1::name) }

// @ fn_16_BF88
CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    EMP1_TRIGGER(AllTentaclesRetracted),
    EMP1_TRIGGER(HeartStageOver),
    EMP1_TRIGGER(IsDead),
    EMP1_TRIGGER(ShouldMeleeAttack),
    EMP1_TRIGGER(ShouldAttack),
    EMP1_TRIGGER(ShouldTaunt),
};

CPatterned::StateMachine::SStateFunction skStates[] = {
    EMP1_STATE(TrackPlayer),
    EMP1_STATE(BeginHeartStage),
    EMP1_STATE(HeartStage),
    EMP1_STATE(EndHeartStage),
    EMP1_STATE(Dead),
    EMP1_STATE(MeleeAttack),
    EMP1_STATE(Attack),
    EMP1_STATE(ProjectileAttack),
    EMP1_STATE(WarpAttack),
    EMP1_STATE(TauntAttack),
    EMP1_STATE(Taunt),
    EMP1_STATE(JumpRope),
};

CPatterned::StateMachine::SCodeFunction skCodes[] = {
    EMP1_CODE(BeginSubStage),
    EMP1_CODE(ResetTentacles),
    EMP1_CODE(ChooseRandomTentacleCount),
};

#undef EMP1_TRIGGER
#undef EMP1_STATE
#undef EMP1_CODE

// The eight portal locators, lbl_16_rodata_1E0 (.rodata 0x1E0): the tentacle's portal that the
// animation's EventStart/EventStop events name. The list is the retail's order (the seventh
// portal comes before the sixth, like the tentacle order in fn_16_36E8).
// fn_16_5DBC's two function-local statics (the retail's lbl_16_bss_C and lbl_16_bss_14, each
// with its own "initialized" guard byte, lbl_16_bss_10 and lbl_16_bss_18).
int sTauntState = 0; // lbl_16_bss_C: the retail never reads it
int sTauntRetracted = 0; // lbl_16_bss_14: the retracted tentacle count at the activate

const char* const skPortalNames[8] = {"portal1_LCTR_SDK", "portal2_LCTR_SDK", "portal3_LCTR_SDK",
                                      "portal4_LCTR_SDK", "portal5_LCTR_SDK", "portal7_LCTR_SDK",
                                      "portal6_LCTR_SDK", "portal8_LCTR_SDK"};

} // namespace

// fn_16_900C. The base call is the retail one: EPatternedAI 0x0D (`li r4, 0xd` at 0x00009068),
// flavor 0 (`li r7, 0x0` at 0x0000905C), movement 1 (kMT_Flyer), collider 1 (kCT_One) and body 2
// (kBT_Restricted) - the three constants stored to the stack arguments at 0x00009060..0x00009070.
// The retail's own helpers it calls: fn_16_9AB0 (the health info, 0x9204), fn_16_98D0 (the
// damage vulnerability, 0x921C), fn_16_9700 (the shock wave block, 0x955C) and fn_16_9610 (the
// plasma beam info, 0x958C) - the host's LdrToHealthInfo and LdrToDamageVulnerability and the
// verbatim copies do them. The token it locks (0x9444) is the host's TLockedToken's own
// constructor and the projectile info (0x9580) the host's CProjectileInfo.
// @ fn_16_900C
CEmperorIngStage1::CEmperorIngStage1(TUniqueId uid, const rstl::string& name,
                                     const CEntityInfo& info, const CTransform4f& xf,
                                     const CModelData& modelData,
                                     const CActorParameters& actParms,
                                     const CPatternedInfo& pInfo,
                                     const SLdrEmperorIngStage1Data& data)  // fn_16_A5BC and fn_16_A5FC: the loader's model data and the script data block // (fn_16_ABD0, fn_16_AEDC, fn_16_AF7C, fn_16_B014, fn_16_B130, fn_16_B1D8, fn_16_B3DC, // fn_16_B378, fn_16_B434, fn_16_B52C, fn_16_B584, fn_16_B5C8, fn_16_B668, fn_16_B6BC, // fn_16_B6EC, fn_16_B8F4 and fn_16_BB3C)
: CPatterned(static_cast< EPatternedAI >(0x0d), uid, name, kFT_Zero, info, xf, modelData, pInfo,
             kMT_Flyer, kCT_One, kBT_Restricted, actParms)
, mHealthInfo(LdrToHealthInfo(data.tentacle.health))            // guest 0xAAC, fn_16_9AB0  // fn_16_9AB0: the health info is copied into the actor's

, mNormalVulnerability(LdrToDamageVulnerability(                  // guest 0xACC, fn_16_98D0  // fn_16_98D0 and fn_16_9980: the damage vulnerability is copied
      data.tentacle.normalVulnerability))
, mWarpVulnerability(LdrToDamageVulnerability(                    // guest 0xAFC, fn_16_98D0
      data.tentacle.warpAttackVulnerability))
, mMeleeVulnerability(LdrToDamageVulnerability(                   // guest 0xB2C, fn_16_98D0
      data.tentacle.meleeAttackVulnerability))
, mProjectileVulnerability(LdrToDamageVulnerability(              // guest 0xB5C, fn_16_98D0
      data.tentacle.projectileAttackVulnerability))
, mMeleeDamage(LdrToDamageInfo(data.melee.damage))               // guest 0xB8C
, mProjectile(data.projectile.projectile,                         // guest 0xBB8/0xBC4, data+0x594
              LdrToDamageInfo(data.projectile.projectileDamage))
, mProjectileVisorEffect(gpSimplePool->GetObj(                     // guest 0xBE0
      SObjectTag('PART', data.projectile.projectileVisorEffect)))
, mWarpPortalEffect(data.warp.portalEffect)                        // guest 0xC00, data+0x5A8
, mWarpAttackTip(data.warp.attackTip)                               // guest 0xC04..0xC10, data+0x5AC
, mWarpStabDamage(LdrToDamageInfo(data.warp.stabDamage))         // guest 0xC10  // fn_16_9980: the damage info is copied
, mBeamProjectile(data.beamAttack.beamInfo.weaponSystem,            // guest 0xC80, 0x9644
                  LdrToDamageInfo(data.beamAttack.damage))
, mBeamInfo(data.beamAttack.beamInfo)                             // guest 0xCA8, fn_16_9610  // fn_16_9610: the plasma beam info is copied
, mBeamSound(data.beamAttack.sound)                               // guest 0xCF0, data+0x66C
, mHeartExposedTime(data.heartExposedTime)                        // guest 0xBA8
, mStayRetractedTime(data.tentacle.stayRetractedTime)            // guest 0xBB4
, mMaxTurnSpeedNormal(data.maxTurnSpeedNormal)                    // guest 0xBF0
, mMaxTurnSpeedMelee(data.maxTurnSpeedMelee)                      // guest 0xBF4
, mJumpRopeTurnSpeed(data.unknown_0xe5a7c358)                     // guest 0xBF8, data+0x6A0
, mTurnSpeedAccel(data.turnSpeedAccel)                            // guest 0xBFC
, mTentacleDamagedSound(data.tentacle.tentacleDamagedSound)  // guest 0xC2C, data+0x57C at 0x95E0
, mWarpSound(data.warp.sound)                                // guest 0xC2E, data+0x5D0
, mWarpSound2(data.warp.sound_0xa93f0198)                     // guest 0xC30, data+0x5D4
, mTauntFrequency(data.tauntFrequency)                            // guest 0xC34
, mTauntLoopDuration(data.tauntAttack.loopDuration)                      // guest 0xC38, data+0x5D8
, mAttackIntervalMin(data.attackIntervalMin)                      // guest 0xCF4
, mAttackIntervalMax(data.attackIntervalMax)                      // guest 0xCF8
, mModeOneHealth(data.unknown_0x905938b8)                         // guest 0xBAC, data+0x674
, mModeTwoHealth(data.unknown_0xb826317a)                         // guest 0xBB0, data+0x678
, mHeartDamageSound(data.heartDamageSound)                        // guest 0xCFC, data+0x67C
, mShockWaveInfo(data.tauntAttack.shockWaveInfo)                    // guest 0xC40, data+0x5E0  // fn_16_9700: the shock wave block is copied
, mShockWaveTravelSound(data.tauntAttack.shockWaveTravelSound)      // guest 0xC7C, data+0x610
, x7c0_(0)                                                        // guest 0x7C0, fn_16_900C clears it
, x7c4_(0)                                                        // guest 0x7C4, cleared
, x7c8_(nullptr)                                                  // guest 0x7C8, cleared
, x7cc_(nullptr)                                                  // guest 0x7CC, cleared
, x7d0_(nullptr)                                                  // guest 0x7D0, cleared
, mJoints()                                                       // guest 0x7D4, the count cleared
, xa18_(kInvalidUniqueId)                                         // guest 0xA18, fn_16_900C clears it
, xa1a_(kInvalidUniqueId)                                         // guest 0xA1A, fn_16_900C clears it
, xa1c_(kInvalidUniqueId)                                         // guest 0xA1C, fn_16_900C clears it
, xa1e_(kInvalidUniqueId)                                         // guest 0xA1E, fn_16_900C clears it
, xa20_(-1)                                                   // guest 0xA20, fn_16_900C clears it
, xa24_(-1)                                                   // guest 0xA24, fn_16_900C clears it
, xa28_(-1)                                                   // guest 0xA28, fn_16_900C clears it
, xa2c_(0.f)                                                      // guest 0xA2C, rodata_380
, xa30_(0.f)                                                      // guest 0xA30, rodata_380
, xa34_(data.maxTurnSpeedNormal)                                   // guest 0xA34, 0x91B8
, xa38_(0.f)                                                      // guest 0xA38, 0x91BC
, xa3c_(0.f)                                                      // guest 0xA3C, rodata_380
, xa40_(GetHealthInfo()->GetInitialHP())                          // guest 0xA40, 0x9120
, xa44_(0.f)                                                      // guest 0xA44, 0x9188
, xa48_(0.f)                                                      // guest 0xA48, rodata_380
, xa4c_(4.f)                                                      // guest 0xA4C, rodata_38C
, xa50_(0.f)                                                      // guest 0xA50, rodata_380
, xa54_(0.f)                                                      // guest 0xA54, rodata_380
, xa58_(0.f)                                                      // guest 0xA58, rodata_380
, xa5c_(0.f)                                                      // guest 0xA5C, rodata_380
, xa60_(0.f)                                                      // guest 0xA60, rodata_380
, xa64_(0.f)                                                      // guest 0xA64, rodata_380
, xa68_(0.f)                                                      // guest 0xA68, rodata_380
, xa6c_(0.f)                                                      // guest 0xA6C
, xa70_(0)                                                        // guest 0xA70, cleared
, xa74_(0)                                                        // guest 0xA74, 0x9178
, xa78_(0.f)                                                      // guest 0xA78, rodata_380
, xa8c_(CSfxHandle::NullHandle())                                  // guest 0xA8C, cleared
, xa90_(kInvalidUniqueId)                                         // guest 0xA90, fn_16_900C clears it
, xa94_(CSfxHandle::NullHandle())                                  // guest 0xA94, cleared
, xa98_(kInvalidUniqueId)                                         // guest 0xA98, 0x91D8
, xa9c_(CSfxHandle::NullHandle())                                  // guest 0xA9C, cleared
, xaa0_(CVector3f::Zero())                                        // guest 0xAA0, the zero vector
{
  // The retail constructor (fn_16_900C) stops the actor drawing a shadow (SetDrawShadow(this, 0)
  // at 0x000095E0), then calls EnableAllAnimReactions(&this->0x620, 0) at 0x000095EC.
  // The shock-wave block at guest 0xC40 and the two tokens at guest 0xBB8 and 0xBE0 belong to
  // attacks the port has not reached yet.
  // 0x917C..0x91AC: the flag byte (guest 0xA74) has its bits 0x80, 0x40, 0x20, 0x10 and 0x08
  // cleared one at a time. The host's heap is not zeroed, so the byte is zeroed in the member
  // list; the mask says the same.
  xa74_ &= ~0xF8;
  SetDrawShadow(false);                            // 0x000095E0
  KnockBackController().EnableAllAnimReactions(false); // 0x000095E4..0x000095EC: this +0x620, li r4,0
}

// fn_16_8B80 (vtable slot 0): only the member destruction the collision managers need is here -
// the three rstl::single_ptr members are deleted in reverse order, 0x7D0 (0x8CDC), 0x7CC
// (0x8CF0) and 0x7C8 (0x8D04).
// The rest of the retail's destructor is the base class's own: fn_16_8D40 (0x8D40..0x8F24, the
// call at 0x8D10) is CPatterned's destructor - it destroys the tokens, the knock-back manager
// (fn_16_8F24, 0x8F24..0x8FB8, which puts the DOL's CAiKnockBackMgr vtable back and unlocks the
// token), the possession data, the body state info and manager, the damage vulnerability and the
// base; fn_16_8FB8 (0x8FB8..0x900C, the call at 0x8D38) is the deleting destructor of the
// rstl::vector<CSegId> at guest 0xA7C. The host's CPatterned, CAi and rstl::vector own those
// members and destroy them themselves, so the retail functions are only cited here.
// The retail's teardown in order: the four damage vulnerabilities (guest 0xACC..0xB5C, 0x8C3C..
// 0x8C68), the segment vector (guest 0xA7C, fn_16_8FB8 at 0x8C74), the three collision actor
// managers (guest 0x7D0, 0x7CC and 0x7C8, 0x8CDC..0x8D04) and then the base (fn_16_8D40 at
// 0x8D10). The port deletes the three managers in the body - the host's members (the four
// vulnerabilities and the vector) are destroyed after it and the base after them, where retail
// destroys the managers last; the two orders differ only in members that do not use each
// other.
// @ fn_16_8B80
CEmperorIngStage1::~CEmperorIngStage1() {
  delete x7d0_;
  delete x7cc_;
  delete x7c8_;
}

// A body state is only ever installed by CBodyController::Activate, and CPatterned::Think /
// ThinkAboutMove / Death / KnockBack all dereference the current state. The analysis does not say
// where retail activates it, so the port activates on kSM_Create and, as the other patterned
// actors in this port do, again on demand in every entry point that can run first.
// The null test is a port limit, not retail's: CPatterned's constructor only builds the body
// controller when the patterned info has model data (CPatterned.cpp's HasModelData() test), so an
// actor without a model has no controller to activate.
void CEmperorIngStage1::EnsureBodyActive(CStateManager& mgr) {
  if (BodyController() != nullptr && !BodyController()->GetIsActive()) {
    BodyController()->Activate(mgr, pas::kAS_Invalid);
  }
}

// vtable slot 3 = fn_16_56D8 (0x56D8..0x5928): retail's Think. The body only runs while the
// actor is active - the asm's gate on the byte at 0x20 (CEntity::mActive) at 0x5708 - and it
// counts the attack timer down (0x5734) and the taunt timer up (0x5740) around the base's own
// Think (the call at 0x5720). The retail's effect state name is a heap string whose destructor runs
// at 0x57D4; the host's string_l is a literal, so there is nothing to destroy.
// @ fn_16_56D8
void CEmperorIngStage1::Think(float dt, CStateManager& mgr) {
  if (!GetActive()) {
    return;
  }
  UpdateTentacles(dt, mgr); // fn_16_3668, 0x5710
  // The retail constructor does not activate the body controller; the port does it here, as the
  // other patterned actors in this port do, because CPatterned::Think dereferences the body state.
  EnsureBodyActive(mgr);
  CPatterned::Think(dt, mgr); // 0x5720
  xa4c_ -= dt;                // 0x5734
  xa50_ += dt;                // 0x5740
  UpdateHeartHealth(mgr);     // fn_16_51D4, 0x5748
  UpdateTentacleAnchor(dt, mgr); // fn_16_4EB8, 0x5758
  UpdateCollisionActors(dt, mgr); // fn_16_3AFC, 0x5768
  UpdateTrackPlayer(dt, mgr); // fn_16_4C0C, 0x5778
  xa78_ -= dt;                // 0x578C
  // The effect state's name is the string lbl_16_rodata_340 points at (0x5780), and it is on
  // while the timer is above zero (the compare at 0x57B0).
  AnimationData()->SetEffectState(rstl::string_l("DarkProjectileCharge"), xa78_ > 0.f, mgr); // 0x57CC
  UpdateMeleeHits(dt, mgr);   // fn_16_4C8, 0x57E4
  UpdateTentacleHits(dt, mgr); // fn_16_8C, 0x57F4
  // The emitter that follows the target (0x57F8..0x5900): the handle must be playing, the
  // target must exist, and the emitter sits on the target's momentum, pointing at the player.
  if (xa8c_ && xa90_ != kInvalidUniqueId && CSfxManager::IsPlaying(xa8c_)) {
    const CEntity* target = mgr.GetObjectById(xa90_);
    if (target != nullptr) {
      // The retail reads the target's position (CActor::mPosition) and its momentum
      // (CPhysicsActor::mMomentum) straight out of the object; the port needs the cast, and a
      // null here means the target is not a physics actor.
      const CPhysicsActor* targetActor = TCastToConstPtr<CPhysicsActor>(target);
      if (targetActor != nullptr) {
        const CVector3f& playerPos = mgr.GetPlayer(0)->GetTranslation();
        const CVector3f& targetPos = targetActor->GetTranslation();
        // The direction is the horizontal one: the asm zeroes the third component at 0x5890.
        CVector3f dir(playerPos.GetX() - targetPos.GetX(), playerPos.GetY() - targetPos.GetY(),
                      0.f);
        if (dir.CanBeNormalized()) {
          dir.Normalize();
          // The retail's float at the target's +0x1C0 is CPhysicsActor::mMomentum's X. The
          // retail's CActor is 0x158 bytes (include/MetroidPrime/CActor.hpp's
          // CHECK_SIZEOF(CActor, 0x158)), so the retail's CPhysicsActor starts at 0x158; the
          // port's CActor is 0x1B8 (the port's own presentation members), so the port's starts
          // 0x60 later. The member order in CPhysicsActor is upstream's, and mMomentum is the
          // member at the CPhysicsActor's +0x68 (the port's mMomentum is at 0x220 = 0x1B8 +
          // 0x68), so 0x158 + 0x68 = 0x1C0 is the same member in both.
          const float offset = targetActor->GetMomentumWR().GetX();
          const CVector3f emitterPos(targetPos.GetX() + dir.GetX() * offset,
                                     targetPos.GetY() + dir.GetY() * offset, targetPos.GetZ());
          CSfxManager::UpdateEmitter(xa8c_, emitterPos, dir, 0xFF);
        }
      }
    }
  }
}

// fn_16_3668 (0x3668..0x36E8): every live tentacle gets its per-frame update.
// @ fn_16_3668
void CEmperorIngStage1::UpdateTentacles(float dt, CStateManager& mgr) {
  for (int i = 0; i < mJoints.mCount; ++i) {
    UpdateTentacle(i, dt, mgr);
  }
}

// fn_16_2B0C (0x2B0C..0x2B68): the per-tentacle update - fn_16_26B8 (the tentacle's own work),
// fn_16_2524 (its motion) and fn_16_24AC (its vulnerability effect states), in that order.
// @ fn_16_2B0C
void CEmperorIngStage1::UpdateTentacle(int index, float dt, CStateManager& mgr) {
  UpdateTentacleCore(index, dt, mgr);
  UpdateTentacleMotion(index, dt, mgr);
  SetVulnerabilityEffects(index);
}

// fn_16_26B8 (0x26B8..0x2A68): the tentacle's additive animations on this actor's animation data
// (0x26F8). The flag 0x04 forces state 2 (0x26F0). The joint's three animations: +0x0C comes out,
// +0x10 goes back, +0x14 the retracted idle.
//   0 retracted: flag 0x40 clear; unless the idle is playing, the other two are deleted and it is
//     added looping at 1.0f (lbl_16_rodata_3B4, 0x2784); the timer (+0x04) counts up (0x278C).
//   1 coming out: until it plays, the tentacle is made shootable unless it is (0x10, fn_16_1FC0 at
//     0x27C0), flag 0x40 is set and the out animation added once (0x2808); while it plays, the
//     state becomes 2 when its tree is gone or has no time left (|t - 0.0f| < 0.00001f,
//     lbl_16_rodata_3C8, 0x2884).
//   2 out: shootable as in 1 (0x28B4), flag 0x40 set, all three animations deleted (0x28E8).
//   3 going back: flag 0x40 clear; the back animation is added once (0x2948) and the state becomes
//     0 when it is done (0x29D0).
// Then flag 0x40 (the collision wanted) and 0x20 (the collision on) are reconciled through
// fn_16_3D64 (0x2A00 on, 0x2A30 off).
// @ fn_16_26B8
void CEmperorIngStage1::UpdateTentacleCore(int index, float dt, CStateManager& mgr) {
  CEmperorIngStage1Joint& joint = mJoints.mJoints[index];
  CAnimData* animData = joint.mOwner->AnimationData();
  int state = joint.mState;
  if ((joint.mFlags & 0x04) != 0) {
    state = 2;
  }
  switch (state) {
    case 0:
      joint.mFlags &= ~0x40;
      if (!animData->IsAdditiveAnimationActive(joint.mAnimId3)) {
        animData->DelAdditiveAnimationImmediately(joint.mAnimId2);
        animData->DelAdditiveAnimationImmediately(joint.mAnimId1);
        animData->AddAdditiveAnimation(joint.mAnimId3, 1.f, true, false);
      }
      joint.mTimer += dt;
      break;
    case 1:
      if (!animData->IsAdditiveAnimationActive(joint.mAnimId1)) {
        if ((joint.mFlags & 0x10) == 0) {
          RestoreTentacleHealth(joint, mgr);
        }
        joint.mFlags |= 0x40;
        animData->DelAdditiveAnimationImmediately(joint.mAnimId2);
        animData->DelAdditiveAnimationImmediately(joint.mAnimId3);
        animData->AddAdditiveAnimation(joint.mAnimId1, 1.f, false, false);
      } else {
        const rstl::rc_ptr< CAnimTreeNode > tree =
            animData->GetAdditiveAnimationTree(joint.mAnimId1);
        if (tree.IsNull() || close_enough(tree->VGetTimeRemaining().GetSeconds(), 0.f)) {
          joint.mState = 2;
        }
      }
      break;
    case 2:
      if ((joint.mFlags & 0x10) == 0) {
        RestoreTentacleHealth(joint, mgr);
      }
      joint.mFlags |= 0x40;
      animData->DelAdditiveAnimationImmediately(joint.mAnimId2);
      animData->DelAdditiveAnimationImmediately(joint.mAnimId3);
      animData->DelAdditiveAnimationImmediately(joint.mAnimId1);
      break;
    case 3:
      joint.mFlags &= ~0x40;
      if (!animData->IsAdditiveAnimationActive(joint.mAnimId2)) {
        animData->DelAdditiveAnimationImmediately(joint.mAnimId3);
        animData->DelAdditiveAnimationImmediately(joint.mAnimId1);
        animData->AddAdditiveAnimation(joint.mAnimId2, 1.f, false, false);
      } else {
        const rstl::rc_ptr< CAnimTreeNode > tree =
            animData->GetAdditiveAnimationTree(joint.mAnimId2);
        if (tree.IsNull() || close_enough(tree->VGetTimeRemaining().GetSeconds(), 0.f)) {
          joint.mState = 0;
        }
      }
      break;
  }
  if ((joint.mFlags & 0x40) != 0) {
    if ((joint.mFlags & 0x20) == 0) {
      joint.mOwner->SetCollisionActorActive(mgr, joint.mIndex, true);
      joint.mFlags |= 0x20;
    }
  } else if ((joint.mFlags & 0x20) != 0) {
    joint.mOwner->SetCollisionActorActive(mgr, joint.mIndex, false);
    joint.mFlags &= ~0x20;
  }
}

// lbl_16_bss_1C: the hurt sound's cool-down, one for all the tentacles (module .bss, so 0.0f).
static float sTentacleHurtSfxTimer = 0.f;

// fn_16_2524 (0x2524..0x26B8): the shared hurt-sound timer runs down (0x2568). For a shootable
// tentacle (flag 0x10, 0x2584) whose collision actor (fn_16_3AA8, 0x2574) lost health since the
// last frame (CHealthInfo +0x04 below the joint's +0x20, 0x25A4): when the timer is below 0.0f
// and the tentacle still lives, tentacleDamagedSound (guest 0xC2C) plays at the actor (0x25F4)
// and the timer takes Random Range(3.0f, 7.0f) (lbl_16_rodata_3C0/3C4, 0x2610); the hit time
// (+0x1C) restarts and +0x20 takes the health. At 0 health or below (0x2638) the tentacle is no
// longer shootable, goes back (state 3) and fn_16_3BC4 runs at its position (0x2664). The hit time
// counts up to CPatterned::skDamageHitTime (0x2678).
// @ fn_16_2524
void CEmperorIngStage1::UpdateTentacleMotion(int index, float dt, CStateManager& mgr) {
  CEmperorIngStage1Joint& joint = mJoints.mJoints[index];
  sTentacleHurtSfxTimer -= dt;
  CCollisionActor* actor = joint.mOwner->GetTentacleActor(mgr, joint.mIndex);
  if (actor != nullptr && (joint.mFlags & 0x10) != 0) {
    const float health = actor->HealthInfo()->GetHP();
    if (health < joint.mSpeed) {
      if (sTentacleHurtSfxTimer < 0.f && health > 0.f) {
        CEmperorIngStage1* owner = joint.mOwner;
        CSfxManager::AddEmitter(owner->mTentacleDamagedSound, actor->GetTranslation(),
                                owner->GetCurrentAreaId().Value(), true, false,
                                CSfxManager::kMedPriority);
        sTentacleHurtSfxTimer = mgr.Random()->Range(3.f, 7.f);
      }
      joint.mDamageHitTime = 0.f;
      joint.mSpeed = health;
    }
    if (health <= 0.f) {
      joint.mFlags &= ~0x10;
      joint.mState = 3;
      joint.mOwner->SendTentacleDestroyed(mgr, actor->GetTranslation());
    }
  }
  float hitTime = joint.mDamageHitTime + dt;
  if (!(hitTime < CPatterned::skDamageHitTime)) { // 0x2678 fcmpo; bge (taken on NaN)
    hitTime = CPatterned::skDamageHitTime;
  }
  joint.mDamageHitTime = hitTime;
}

// fn_16_1FC0 (0x1FC0..0x2070): with a collision actor (fn_16_3AA8, 0x1FEC) the tentacle is flagged
// shootable (0x10, 0x2004), +0x20 takes the actor's initial health (CHealthInfo +0x00, vtable slot
// 12 HealthInfo at 0x2014) and the health is reset to it (+0x04, 0x204C).
// @ fn_16_1FC0
void CEmperorIngStage1::RestoreTentacleHealth(CEmperorIngStage1Joint& joint,
                                              CStateManager& mgr) {
  CCollisionActor* actor = joint.mOwner->GetTentacleActor(mgr, joint.mIndex);
  if (actor != nullptr) {
    joint.mFlags |= 0x10;
    joint.mSpeed = actor->HealthInfo()->GetInitialHP();
    actor->HealthInfo()->SetHP(actor->HealthInfo()->GetInitialHP());
  }
}

// fn_16_3C58 (0x3C58..0x3D64): the first call fills guest 0xA7C (reserved for 0x38, fn_16_BB3C at
// 0x3C84) with the segments of the 8 x 7 spine locators (lbl_16_rodata_21C, "one_Spine_1_SDK" ..
// "eight_Spine_7_SDK", stride 0x1C per tentacle; GetLocatorSegId at 0x3CB8). The result is the
// first matching entry's index / 7 (0x3D24..0x3D40), or -1.
// @ fn_16_3C58
int CEmperorIngStage1::GetTentacleIndex(const CSegId& seg) {
  static const char* const skSpineNames[8] = {"one", "two", "three", "four",
                                              "five", "six", "seven", "eight"};
  if (xa7c_.size() == 0) {
    xa7c_.reserve(0x38);
    for (int i = 0; i < 8; ++i) {
      for (int j = 0; j < 7; ++j) {
        char name[32];
        sprintf(name, "%s_Spine_%d_SDK", skSpineNames[i], j + 1);
        xa7c_.push_back(AnimationData()->GetLocatorSegId(rstl::string_l(name)));
      }
    }
  }
  for (int i = 0; i < xa7c_.size(); ++i) {
    if (xa7c_[i] == seg) {
      return i / 7;
    }
  }
  return -1;
}

// fn_16_3BC4 (0x3BC4..0x3C58): the IS3/ACTIVATE actor (guest 0xA18) moves to the position
// (SetTranslation at 0x3C1C) and the script hears IS03 (0x3C44).
// @ fn_16_3BC4
void CEmperorIngStage1::SendTentacleDestroyed(CStateManager& mgr, const CVector3f& pos) {
  CActor* actor = TCastToPtr< CActor >(mgr.ObjectById(xa18_));
  if (actor != nullptr) {
    actor->SetTranslation(pos);
  }
  SendScriptMsgs(kSS_InternalState3, mgr, kInvalidUniqueId, kSM_None);
}

// fn_16_24AC (0x24AC..0x2524): the tentacle's two vulnerability effect states - the strings
// lbl_16_rodata_344 and lbl_16_rodata_348 point at ("DarkVulnerability" and
// "LightVulnerability"), each handed to fn_16_2070.
// @ fn_16_24AC
void CEmperorIngStage1::SetVulnerabilityEffects(int index) {
  SetTentacleEffectState(index, rstl::string_l("DarkVulnerability"));
  SetTentacleEffectState(index, rstl::string_l("LightVulnerability"));
}

// fn_16_2070 (0x2070..0x219C): the named effect state of this actor's character (the character
// info's effect list at CAnimData+0xC4, looked up by fn_16_219C at 0x20A0): every component whose
// segment is on this tentacle's spine (fn_16_3C58, 0x20D8) gets the particle external parameters
// 0..5 (CAnimData+0x178's SetParticleExternalParam, 0x20F0..0x2178) - the joint's +0x30, +0x34,
// +0x38, then -(+0x3C), -(+0x40), -(+0x44).
// The lookup compares the names with the retail's string comparison (fn_16_219C -> fn_16_2204 ->
// fn_16_22B0 -> fn_16_22D0 -> fn_16_22F8 -> fn_16_231C, which calls fn_16_2398); the host's
// rstl::find_by_key does it.
// @ fn_16_2070
void CEmperorIngStage1::SetTentacleEffectState(int index, const rstl::string& name) {
  const CEmperorIngStage1Joint& joint = mJoints.mJoints[index];
  CAnimData* animData = joint.mOwner->AnimationData();
  const CCECharacterInfo::TEffectList& effects = animData->GetCharacterInfo().GetEffects();
  CCECharacterInfo::TEffectList::const_iterator it = rstl::find_by_key(effects, name);
  if (it == effects.end()) {
    return;
  }
  CParticleDatabase& particles = animData->GetParticleDB();
  rstl::vector< CEffectComponent >::const_iterator comp = it->second.begin();
  for (; comp != it->second.end(); ++comp) {
    if (joint.mIndex == joint.mOwner->GetTentacleIndex(comp->GetSegmentId())) {
      const uint hash = comp->GetComponentNameHash();
      particles.SetParticleExternalParam(hash, 0, joint.mPosition.GetX());
      particles.SetParticleExternalParam(hash, 1, joint.mPosition.GetY());
      particles.SetParticleExternalParam(hash, 2, joint.mPosition.GetZ());
      particles.SetParticleExternalParam(hash, 3, -joint.mUp.GetX());
      particles.SetParticleExternalParam(hash, 4, -joint.mUp.GetY());
      particles.SetParticleExternalParam(hash, 5, -joint.mUp.GetZ());
    }
  }
}

// fn_16_302C (0x302C..0x3060): the actor the module tracks - the id at guest 0xA1A, looked up in
// the state manager and cast to an actor.
// @ fn_16_302C
CActor* CEmperorIngStage1::GetTargetActor(CStateManager& mgr) const {
  return TCastToPtr< CActor >(mgr.ObjectById(xa1a_));
}

// fn_16_2FA8 (0x2FA8..0x2FF8): the collision actor of the second tentacle's collision actor
// manager (guest 0x7CC): its first joint description's collision actor id, looked up in the state
// manager.
// @ fn_16_2FA8
CCollisionActor* CEmperorIngStage1::GetCollisionActor(CStateManager& mgr) const {
  const TUniqueId id = x7cc_->GetCollisionDescFromIndex(0).GetCollisionActorId();
  return TCastToPtr< CCollisionActor >(mgr.ObjectById(id));
}

// fn_16_4E44 (0x4E44..0x4E68) and fn_16_BDD0 (0xBDD0..0xBE34): the caller copies the effect's
// counted particle system reference (CParticleGenInfoGeneric +0x78/+0x7C) and drops it again,
// keeping the raw pointer; the effect still holds the system.
// @ fn_16_4E44
CParticleGen* CEmperorIngStage1::GetEffectSystem(CParticleGenInfo* effect) {
  return static_cast< CParticleGenInfoGeneric* >(effect)->GetParticleSystem().GetPtr();  // fn_16_88BC, fn_16_8918, fn_16_8938, fn_16_8AA0 and fn_16_8A28: the effect's counted // reference is released - the host's reference does it when the copy dies

}

// fn_16_6D10 (0x6D10..0x6D94): unless null (0x6D34), the system takes the external parameter
// (0x6D3C) and so does every system it spawned, recursively (0x6D50..0x6D70).
// @ fn_16_6D10
void CEmperorIngStage1::SetSystemParam(CParticleGen* system, uint index, float value) {
  CElementGen* gen = static_cast< CElementGen* >(system);  // fn_16_2204, fn_16_22B0 and fn_16_22D0: the external parameter and the spawned systems
  if (gen == nullptr) {
    return;
  }
  gen->SetExternalParam(index, value);
  for (int i = 0; i < gen->GetNumSpawnedParticleSystems(); ++i) {
    SetSystemParam(gen->SpawnedParticleSystem(i), index, value);
  }
}

// fn_16_51D4 (0x51D4..0x52FC): the heart's damage reaches the boss. The heart's collision actor
// (fn_16_2FA8, 0x51FC) has its own copy of this actor's health info (fn_16_3EAC), so the shots
// land there; its health (the vtable slot 14 call, HealthInfo, at 0x5210 and the float at +0x04)
// is the boss's. When it is below the last value seen (guest 0xA40, 0x521C) the value is kept,
// the "Damage" effect timer takes 1/60 s (lbl_16_rodata_3E0, 0x5244) and the heart damage sound
// (guest 0xCFC) plays at the heart (PlayCustomSound at 0x5250, the collision actor's position and
// the zero vector). The mode (guest 0x7C0) moves on as the health drops: 0 becomes 1 below guest
// 0xBAC (0x527C) and 1 becomes 2 below guest 0xBB0 (0x5294). Last the health is written back to
// the heart's health info (0x52BC) and this actor's (0x52D4).
// The null test on the heart is the port's: retail calls through it unchecked.
// @ fn_16_51D4
void CEmperorIngStage1::UpdateHeartHealth(CStateManager& mgr) {
  CCollisionActor* heart = GetCollisionActor(mgr);
  if (heart == nullptr) {
    return;
  }
  const float hp = heart->HealthInfo()->GetHP();
  if (hp < xa40_) {
    xa40_ = hp;
    xa58_ = 0.016666668f;
    PlayCustomSound(heart->GetTranslation(), CVector3f::Zero(), mHeartDamageSound, false);
  }
  switch (x7c0_) {
    case 0:
      if (hp < mModeOneHealth) {
        x7c0_ = 1;
      }
      break;
    case 1:
      if (hp < mModeTwoHealth) {
        x7c0_ = 2;
      }
      break;
    default:
      break;
  }
  heart->HealthInfo()->SetHP(hp);
  HealthInfo()->SetHP(hp);
}

// fn_16_4EB8 (0x4EB8..0x51D4, 0x31C): the body follows the heart. The tracked actor (fn_16_302C)
// is the heart, the 'PLAY' actor (fn_16_3060) is the actor the body is played on, and the
// CONNECT/ACTIVATE actor (fn_16_2FF8) is the script actor that rotates.
//   0x4F10: the rotate actor is put on the heart's "brainSpine_scale" locator (rodata_378).
//   0x4FEC: the turn: the angle (guest 0xA2C) is approached at the accel (guest 0xBFC, degrees
//     per second, turned into radians with rodata_3DC = 0.017453292), and the result (guest
//     0xA30, the current angle) is clamped to the turn speed (guest 0xA34, degrees per second).
//   0x5060: the 'PLAY' actor's Y column is flattened (its third component is 0.0f at 0x5088)
//     and normalised, turned by the current angle times dt (RotateZ, 0x50A4), and the actor is
//     turned to look along it from the heart's "sphere_base" (rodata_37C), the heart's Z column
//     as up (LookAt, 0x5110).
//   0x5120: the body itself goes to the 'PLAY' actor's segment 1, offset by 1.5f (rodata_394)
//     along the 'PLAY' actor's X column.
// @ fn_16_4EB8
void CEmperorIngStage1::UpdateTentacleAnchor(float dt, CStateManager& mgr) {
  CActor* heart = GetTargetActor(mgr);   // fn_16_302C, 0x4EE8
  CActor* body = GetPlayActor(mgr);      // fn_16_3060, 0x4EF8
  CActor* rotate = GetActivateActor(mgr); // fn_16_2FF8, 0x4F0C
  // 0x4F10: the rotate actor on the heart's "brainSpine_scale" locator; the translation of the
  // copied transform is handed to SetTranslation at 0x4F80.
  const CTransform4f spine =
      heart->GetTransform() *
      heart->GetModelData()->GetScaledLocatorTransform(rstl::string_l("brainSpine_scale"));
  rotate->SetTranslation(spine.GetTranslation()); // 0x4F80
  // 0x4F84: the heart's "sphere_base" in world space.
  const CTransform4f base =
      heart->GetTransform() *
      heart->GetModelData()->GetScaledLocatorTransform(rstl::string_l("sphere_base"));
  // 0x4FEC: the turn. The step is the difference to the angle, clamped to the accel (0x5004),
  // the new angle is the step times dt plus the current one (0x5028) and it is clamped to the
  // turn speed (0x503C..0x505C).
  const float kDegToRad = 0.017453292f; // lbl_16_rodata_3DC
  const float accel = kDegToRad * mTurnSpeedAccel;
  const float diff = xa2c_ - xa30_;
  float step = diff;
  if (step < -accel) {
    step = -accel;
  } else if (step > accel) {
    step = accel;
  }
  xa30_ = step * dt + xa30_; // 0x5028
  const float speed = kDegToRad * xa34_;
  if (xa30_ < -speed) {
    xa30_ = -speed;
  } else if (xa30_ > speed) {
    xa30_ = speed;
  } // 0x505C
  // 0x5060: the 'PLAY' actor's Y column, flattened and normalised.
  CVector3f facing = body->GetTransform().GetColumn(kDY);
  facing.SetZ(0.f); // 0x5088
  facing.Normalize(); // 0x508C
  // 0x5090: the current angle times dt, around Z.
  const CVector3f dir = CTransform4f::RotateZ(CRelAngle::FromRadians(xa30_ * dt)) * facing;
  // 0x50B8: the position (the heart's "sphere_base"), the direction and the up (the heart's Z
  // column) the body looks along; 0x5110: the 'PLAY' actor takes that transform.
  body->SetTransform(CTransform4f::LookAt(base.GetTranslation(),
                                           base.GetTranslation() + dir, base.GetColumn(kDZ)));
  // 0x5120: the body at the 'PLAY' actor's segment 1, 1.5f along its Z column (m02/m12/m22 at
  // 0x2C/0x3C/0x4C, read after the new transform, 0x5124).
  const CVector3f offset = body->GetTransform().GetColumn(kDZ) * 1.5f; // lbl_16_rodata_394
  const CTransform4f seg = body->GetModelData()->GetScaledLocatorTransform(CSegId(1));
  SetTransform((body->GetTransform() * seg) * CTransform4f::Translate(offset)); // 0x51A8
}

// fn_16_3AFC (0x3AFC..0x3B70): the three collision actor managers get their update (the calls
// at 0x3B28, 0x3B3C and 0x3B50). The managers are fn_16_3EAC's, built on kSM_AreaLoaded.
// @ fn_16_3AFC
void CEmperorIngStage1::UpdateCollisionActors(float dt, CStateManager& mgr) {
  x7c8_->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);
  x7cc_->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);
  x7d0_->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);
}

// fn_16_4C0C (0x4C0C..0x4DF0): the module's track-player update. It looks the tracked actor up
// (fn_16_302C), and for the "Damage" and "Telegraph" effect states it finds the actor's first
// particle effect of that name, hands it to the effect update (fn_16_4E44, fn_16_BDD0 and
// fn_16_6D10) and turns the effect state on (SetEffectState at 0x4CF0 and 0x4DB0). The two
// timers (guest 0xA58 and 0xA5C) count down between and after the blocks (0x4D0C and 0x4DC0).
// @ fn_16_4C0C
void CEmperorIngStage1::UpdateTrackPlayer(float dt, CStateManager& mgr) {
  CActor* target = GetTargetActor(mgr);
  if (target == nullptr) {
    return;
  }
  CParticleGenInfo* damage = target->AnimationData()->GetFirstParticleEffect(
      rstl::string_l("Damage"));
  if (damage != nullptr) {
    SetSystemParam(GetEffectSystem(damage), 0, xa58_ > 0.f ? 1.f : 0.f);
  }
  target->AnimationData()->SetEffectState(rstl::string_l("Damage"), true, mgr);
  xa58_ -= dt;
  CParticleGenInfo* telegraph = target->AnimationData()->GetFirstParticleEffect(
      rstl::string_l("Telegraph"));
  if (telegraph != nullptr) {
    SetSystemParam(GetEffectSystem(telegraph), 0, xa5c_ > 0.f ? 1.f : 0.f);
  }
  target->AnimationData()->SetEffectState(rstl::string_l("Telegraph"), true, mgr);
  xa5c_ -= dt;
}

// The tentacles' joint pairs (lbl_16_rodata_0, 0x1E0 bytes): eight tentacles of five 12-byte
// records {from locator, to locator, the box's extent}, the tentacle stride 0x3C (0x4498). The
// order is retail's - "seven" comes before "six".
struct SEmperorIngStage1JointPair {
  const char* from; // +0x0
  const char* to;   // +0x4
  float extent;     // +0x8
};
static const SEmperorIngStage1JointPair skTentacleJointPairs[8][5] = {
  {
      {"one_Spine_1_SDK", "one_Spine_2_SDK", 2.f},         // rodata 0x0
      {"one_Spine_2_SDK", "one_Spine_3_SDK", 1.5f},        // rodata 0xC
      {"one_Spine_3_SDK", "one_Spine_4_SDK", 0.75f},       // rodata 0x18
      {"one_Spine_4_SDK", "one_Spine_6_SDK", 0.75f},       // rodata 0x24
      {"one_Spine_6_SDK", "one_Spine_7_SDK", 1.75f},       // rodata 0x30
  },
  {
      {"two_Spine_1_SDK", "two_Spine_2_SDK", 2.f},         // rodata 0x3C
      {"two_Spine_2_SDK", "two_Spine_3_SDK", 1.5f},        // rodata 0x48
      {"two_Spine_3_SDK", "two_Spine_4_SDK", 0.75f},       // rodata 0x54
      {"two_Spine_4_SDK", "two_Spine_6_SDK", 0.75f},       // rodata 0x60
      {"two_Spine_6_SDK", "two_Spine_7_SDK", 1.75f},       // rodata 0x6C
  },
  {
      {"three_Spine_1_SDK", "three_Spine_2_SDK", 2.f},     // rodata 0x78
      {"three_Spine_2_SDK", "three_Spine_3_SDK", 1.5f},    // rodata 0x84
      {"three_Spine_3_SDK", "three_Spine_4_SDK", 0.75f},   // rodata 0x90
      {"three_Spine_4_SDK", "three_Spine_6_SDK", 0.75f},   // rodata 0x9C
      {"three_Spine_6_SDK", "three_Spine_7_SDK", 1.75f},   // rodata 0xA8
  },
  {
      {"four_Spine_1_SDK", "four_Spine_2_SDK", 2.f},       // rodata 0xB4
      {"four_Spine_2_SDK", "four_Spine_3_SDK", 1.5f},      // rodata 0xC0
      {"four_Spine_3_SDK", "four_Spine_4_SDK", 0.75f},     // rodata 0xCC
      {"four_Spine_4_SDK", "four_Spine_6_SDK", 0.75f},     // rodata 0xD8
      {"four_Spine_6_SDK", "four_Spine_7_SDK", 1.75f},     // rodata 0xE4
  },
  {
      {"five_Spine_1_SDK", "five_Spine_2_SDK", 2.f},       // rodata 0xF0
      {"five_Spine_2_SDK", "five_Spine_3_SDK", 1.5f},      // rodata 0xFC
      {"five_Spine_3_SDK", "five_Spine_4_SDK", 0.75f},     // rodata 0x108
      {"five_Spine_4_SDK", "five_Spine_6_SDK", 0.75f},     // rodata 0x114
      {"five_Spine_6_SDK", "five_Spine_7_SDK", 1.75f},     // rodata 0x120
  },
  {
      {"seven_Spine_1_SDK", "seven_Spine_2_SDK", 2.f},     // rodata 0x12C
      {"seven_Spine_2_SDK", "seven_Spine_3_SDK", 1.5f},    // rodata 0x138
      {"seven_Spine_3_SDK", "seven_Spine_4_SDK", 0.75f},   // rodata 0x144
      {"seven_Spine_4_SDK", "seven_Spine_6_SDK", 0.75f},   // rodata 0x150
      {"seven_Spine_6_SDK", "seven_Spine_7_SDK", 1.75f},   // rodata 0x15C
  },
  {
      {"six_Spine_1_SDK", "six_Spine_2_SDK", 2.f},         // rodata 0x168
      {"six_Spine_2_SDK", "six_Spine_3_SDK", 1.5f},        // rodata 0x174
      {"six_Spine_3_SDK", "six_Spine_4_SDK", 0.75f},       // rodata 0x180
      {"six_Spine_4_SDK", "six_Spine_6_SDK", 0.75f},       // rodata 0x18C
      {"six_Spine_6_SDK", "six_Spine_7_SDK", 1.75f},       // rodata 0x198
  },
  {
      {"eight_Spine_1_SDK", "eight_Spine_2_SDK", 2.f},     // rodata 0x1A4
      {"eight_Spine_2_SDK", "eight_Spine_3_SDK", 1.5f},    // rodata 0x1B0
      {"eight_Spine_3_SDK", "eight_Spine_4_SDK", 0.75f},   // rodata 0x1BC
      {"eight_Spine_4_SDK", "eight_Spine_6_SDK", 0.75f},   // rodata 0x1C8
      {"eight_Spine_6_SDK", "eight_Spine_7_SDK", 1.75f},   // rodata 0x1D4
  },
};

// fn_16_8C (0x8C..0x43C, 0x3B0; fns.txt and the asm agree, fn_16_43C's prologue follows the blr
// at 0x438): the tentacles' contact damage while they sweep (flag 0x40 of guest 0xA74, set by
// JumpRope; the test at 0xB4). The player's touch bounds (its vtable slot 16, GetTouchBounds, at
// 0xD0) must exist. The cooldown (guest 0xA48) runs down by dt and, once it is <= 0.0f
// (lbl_16_rodata_380, cror at 0x140), the 8 * 5 joint pairs of skTentacleJointPairs (the loops at
// 0x164 and 0x16C) each give a box: the two locators' scaled transforms (0x1D0, 0x220), their
// translations through this actor's transform (0x1F8, 0x248), accumulated into an inverted box
// (0x29C, 0x2A8). The tip pair (index 4, 0x2AC) also takes the point 8.0f (lbl_16_rodata_384)
// past its end along the pair's horizontal direction (the third component is 0.0f at 0x2F4; the
// point only when it normalises, 0x2F8). The first box the player's bounds overlap (0x364)
// damages the player with the melee damage (guest 0xB8C) - this actor the damager and the owner,
// the filter including kMT_Solid (lbl_16_data_0, 0x3B; type 3) and the zero direction
// (ApplyDamage at 0x3EC) - and the cooldown becomes 0.2f (lbl_16_rodata_388, 0x3F8).
// @ fn_16_8C
void CEmperorIngStage1::UpdateTentacleHits(float dt, CStateManager& mgr) {
  if ((xa74_ & 0x40) == 0) {
    return;
  }
  CPlayer* player = mgr.GetPlayer(0);
  const rstl::optional_object< CAABox > touch = player->GetTouchBounds();
  if (!touch.valid()) {
    return;
  }
  xa48_ -= dt;
  if (!(xa48_ <= 0.f)) {
    return;
  }
  const CAnimData* animData = GetModelData()->GetAnimationData();
  for (int tentacle = 0; tentacle < 8; ++tentacle) {
    for (int joint = 0; joint < 5; ++joint) {
      const SEmperorIngStage1JointPair& pair = skTentacleJointPairs[tentacle][joint];
      const CSegId from = animData->GetLocatorSegId(rstl::string_l(pair.from));
      const CSegId to = animData->GetLocatorSegId(rstl::string_l(pair.to));
      const CVector3f p1 = GetTransform() * GetScaledLocatorTransform(from).GetTranslation();
      const CVector3f p2 = GetTransform() * GetScaledLocatorTransform(to).GetTranslation();
      CAABox box = CAABox::MakeMaxInvertedBox();
      box.AccumulateBounds(p1);
      box.AccumulateBounds(p2);
      if (joint == 4) {
        CVector3f dir(p2.GetX() - p1.GetX(), p2.GetY() - p1.GetY(), 0.f);
        if (dir.CanBeNormalized()) {
          dir.Normalize();
          box.AccumulateBounds(p2 + dir * 8.f);
        }
      }
      if (touch.data().DoBoundsOverlap(box)) {
        mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(), mMeleeDamage,
                        CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid),
                                                            CMaterialList()),
                        CVector3f::Zero());
        xa48_ = 0.2f;
        return;
      }
    }
  }
}

// fn_16_4C8 (0x4C8..0x830, 0x368): the melee sweep's contact damage. It is fn_16_8C's body with
// two differences: the gate is the 0x80 flag of guest 0xA74 (the melee attack - fn_16_6D94 sets
// it at 0x6F8C and clears it at 0x713C; the test at 0x4F0), and every box grows by 4.0f
// (lbl_16_rodata_38C: the box's minimum drops by 4.0f on each axis at 0x704 and its maximum
// rises by 4.0f at 0x734) instead of the tip pair reaching 8.0f past its end. The player's touch
// bounds (its vtable slot 16, GetTouchBounds, at 0x50C) must exist (0x554). The cooldown (guest
// 0xA48) runs down by dt and, once it is <= 0.0f (lbl_16_rodata_380, the cror at 0x57C), the
// 8 * 5 joint pairs of skTentacleJointPairs (the loops at 0x5A8 and 0x5B0) each give a box: the
// two locators' scaled transforms (0x614, 0x664), their translations through this actor's
// transform (0x63C, 0x68C), accumulated into an inverted box (0x6E0, 0x6EC) and grown by 4.0f
// (0x71C, 0x74C). The first box the player's bounds overlap (0x758) damages the player with the
// melee damage (guest 0xB8C) - this actor the damager and the owner, the filter including
// kMT_Solid (lbl_16_data_0, 0x3B; type 3) and the zero direction (ApplyDamage at 0x7E0) - and the
// cooldown becomes 0.2f (lbl_16_rodata_388, 0x7EC).
// @ fn_16_4C8
void CEmperorIngStage1::UpdateMeleeHits(float dt, CStateManager& mgr) {
  if ((xa74_ & 0x80) == 0) { // 0x4F0
    return;
  }
  CPlayer* player = mgr.GetPlayer(0);
  const rstl::optional_object< CAABox > touch = player->GetTouchBounds(); // 0x50C
  if (!touch.valid()) {                                                    // 0x554
    return;
  }
  xa48_ -= dt;           // 0x568
  if (!(xa48_ <= 0.f)) { // 0x578
    return;
  }
  const CAnimData* animData = GetModelData()->GetAnimationData(); // 0x598
  for (int tentacle = 0; tentacle < 8; ++tentacle) {              // 0x804
    for (int joint = 0; joint < 5; ++joint) {                     // 0x7FC
      const SEmperorIngStage1JointPair& pair = skTentacleJointPairs[tentacle][joint];
      const CSegId from = animData->GetLocatorSegId(rstl::string_l(pair.from)); // 0x5C8
      const CSegId to = animData->GetLocatorSegId(rstl::string_l(pair.to));     // 0x5F4
      const CVector3f p1 = GetTransform() * GetScaledLocatorTransform(from).GetTranslation(); // 0x614
      const CVector3f p2 = GetTransform() * GetScaledLocatorTransform(to).GetTranslation();   // 0x664
      CAABox box = CAABox::MakeMaxInvertedBox();
      box.AccumulateBounds(p1); // 0x6E0
      box.AccumulateBounds(p2); // 0x6EC
      // 0x704: the box's minimum drops by 4.0f on every axis, 0x734: its maximum rises by 4.0f.
      const CVector3f grow(4.f, 4.f, 4.f);
      box.AccumulateBounds(box.GetMinPoint() - grow); // 0x71C
      box.AccumulateBounds(box.GetMaxPoint() + grow); // 0x74C
      if (touch.data().DoBoundsOverlap(box)) { // 0x758
        mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(), mMeleeDamage,
                        CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid),
                                                            CMaterialList()),
                        CVector3f::Zero()); // 0x7E0
        xa48_ = 0.2f; // 0x7EC
        return;
      }
    }
  }
}

// vtable slot 4 = fn_16_7CE8 (0x7CE8..0x7EA8): retail's message switch. The base's own handler
// runs first (the call at 0x7D14) and then the message decides:
//   kSM_Create (0x58435254, compared at 0x7D24) activates the body controller (0x7D78);
//   kSM_Delete (0x5844454C, at 0x7D64) destroys the collision actors (0x7D88);
//   kSM_AreaLoaded (0x58414C44, at 0x7D3C) looks the three connected objects up and sets the
//     tentacle work up (0x7DAC..0x7E28);
//   kSM_Activate (0x41435456, at 0x7D50) hands the boss bar its id, health and name (0x7E80).
// @ fn_16_7CE8
void CEmperorIngStage1::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  CPatterned::AcceptScriptMsg(mgr, msg);
  switch (msg.GetMessage()) {
    case kSM_Create: {
      // The body controller is the member at guest 0x48C.
      BodyController()->Activate(mgr, pas::kAS_Invalid);
      break;
    }
    case kSM_Delete: {
      DestroyCollisionActors(mgr);
      break;
    }
    case kSM_AreaLoaded: {
      xa18_ = FindConnectedObject(mgr, kSS_InternalState3, kSM_Activate);
      // The second lookup is CONNECT/ATTACH (`lis r7, 0x4154` + 0x4348 at 0x7DB8/0x7DD0).
      xa1a_ = FindConnectedObject(mgr, kSS_Connect, kSM_Attach);
      xa1c_ = FindConnectedObject(mgr, kSS_Connect, kSM_Activate);
#ifdef TARGET_PC
      // A repeated AreaLoaded (the console's `send`) would add the joints again past the
      // vector's eight elements.
      if (mJoints.mCount != 0) {
        break;
      }
#endif
      SetupTentacleAnimations(mgr);
      SetupTentacleCollisions(mgr);
      SetupTentacleJoints(mgr);
      break;
    }
    case kSM_Activate: {
      // The boss bar: the health info's initial health (the vtable slot 13 call at 0x7E40 and the
      // float at +0x00 of the result) and the string table index of the boss's name - the string
      // lbl_16_rodata_41C+0x670 points at, "BossTempleBossStage1".
      const float health = GetHealthInfo()->GetInitialHP();
      // The retail looks the name's index up with CStringTable::GetStringIndexByName (0x7E6C);
      // the host's CStringTable has one lookup, GetStringIndex.
      const int index = gpStringTable->GetStringIndex("BossTempleBossStage1");
      mgr.SetBossParams(GetUniqueId(), health, static_cast< uint >(index));
      break;
    }
  }
}

// fn_16_3B70 (0x3B70..0x3BC4): the three collision actor managers are destroyed.
// @ fn_16_3B70
void CEmperorIngStage1::DestroyCollisionActors(CStateManager& mgr) {
  x7c8_->Destroy(mgr);
  x7cc_->Destroy(mgr);
  x7d0_->Destroy(mgr);
}

// fn_16_30C8 (0x30C8..0x3340): the tentacle animation ids. The tracked actor is the one
// connected on CONNECT/'PLAY' (0x504C4159, looked up at 0x3100 and stored at guest 0xA1E at
// 0x3110); its animation database is the model's animation data at +0x3C (0x3118..0x3124). Three
// parameter sets are searched for the best animation and the ids land at guest 0xA20 (0x31CC, the
// set with FromEnum(0) and FromEnum(0)), 0xA24 (0x3278, FromEnum(0) and FromEnum(7)) and 0xA28
// (0x3324, FromEnum(0) and FromEnum(6)). An id is only taken when the search found a time above
// 1.1920929e-07f (lbl_16_rodata_3A8).
// @ fn_16_30C8
void CEmperorIngStage1::SetupTentacleAnimations(CStateManager& mgr) {
  // The port's EScriptObjectMessage has no name for the retail's 0x504C4159, so the raw value is
  // used.
  xa1e_ = FindConnectedObject(mgr, kSS_Connect,
                              static_cast< EScriptObjectMessage >(0x504C4159u));
  // fn_16_3094 (0x3094..0x30C8): that actor. The retail has no test here - the actor connected
  // on CONNECT/'PLAY' exists by the time the area is loaded - but on the host the connection may
  // not be there yet.
  const CActor* actor = GetPlayActor(mgr);
  if (actor == nullptr) {
    return;
  }
  const CPASDatabase& db = actor->GetModelData()->GetAnimationData()->GetPASDatabase();
  // The first set (0x3128..0x319C): six NoParameter, then FromEnum(0) and FromEnum(0).
  const CPASAnimParmData first(pas::EAnimationState(5), CPASAnimParm::FromEnum(0),  // fn_16_337C: the animation parameter list
                               CPASAnimParm::FromEnum(0), CPASAnimParm::NoParameter(),
                               CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
                               CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
                               CPASAnimParm::NoParameter());
  const rstl::pair< float, int > found = db.FindBestAnimation(first, -1);
  if (found.first > 1.1920929e-07f) {
    xa20_ = found.second;
  }
  // The second set (0x31D0..0x3258): six NoParameter, then FromEnum(0) and FromEnum(7).
  const CPASAnimParmData second(pas::EAnimationState(5), CPASAnimParm::FromEnum(7),
                                CPASAnimParm::FromEnum(0), CPASAnimParm::NoParameter(),
                                CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
                                CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
                                CPASAnimParm::NoParameter());
  const rstl::pair< float, int > found2 = db.FindBestAnimation(second, -1);
  if (found2.first > 1.1920929e-07f) {
    xa24_ = found2.second;
  }
  // The third set (0x3280..0x3308): six NoParameter, then FromEnum(0) and FromEnum(6).
  const CPASAnimParmData third(pas::EAnimationState(5), CPASAnimParm::FromEnum(6),
                               CPASAnimParm::FromEnum(0), CPASAnimParm::NoParameter(),
                               CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
                               CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
                               CPASAnimParm::NoParameter());
  const rstl::pair< float, int > found3 = db.FindBestAnimation(third, -1);
  if (found3.first > 1.1920929e-07f) {
    xa28_ = found3.second;
  }
}

// fn_16_3094 (0x3094..0x30C8): the actor connected on CONNECT/'PLAY' (the id at guest 0xA1E).
// fn_16_3060 (0x3060..0x3094) has the same body, so its callers use this one too.
// @ fn_16_3094
// @ fn_16_3060
CActor* CEmperorIngStage1::GetPlayActor(CStateManager& mgr) const {  // fn_16_3060: the 'PLAY' actor - the twin of fn_16_3094
  return TCastToPtr< CActor >(mgr.ObjectById(xa1e_));
}


// fn_16_3EAC (0x3EAC..0x4640): creates the three collision actor managers. The managers are
// retail's rstl::single_ptr members, so the old one is deleted before the new one is stored
// (0x4004, 0x41D0, 0x4500). The module's own helpers it calls are rstl template bodies the host's
// rstl provides: fn_16_BC44 the vector's reserve, fn_16_471C the description's copy, push_back_unsafe
// the vector's push_back, fn_16_4838 the vector's destructor and fn_16_4640 the string's operator+.
// fn_16_48F4 (0x48F4..0x4954), called from fn_16_48BC for every element, releases the element's
// string at +0x2C - the host's rstl::string destructor does it.
// The rs_new null tests (0x3FC8, 0x4194, 0x44C0) and the string destructors' tests are the
// compiler's; the host's rs_new does not return null.
// @ fn_16_3EAC
void CEmperorIngStage1::SetupTentacleCollisions(CStateManager& mgr) {  // fn_16_4640, fn_16_46D4, fn_16_46F4, fn_16_471C, fn_16_4838 and fn_16_48BC: the three // collision actor managers and their descriptions
  // The heart: one sphere on the tracked actor's (fn_16_302C, 0x3EF0) "brainSpine_scale" locator
  // (lbl_16_rodata_41C+0x5EE, 0x3F08), radius 5.0f (lbl_16_rodata_3AC), name "HeartCollision"
  // (+0x632), mass 0.001f (lbl_16_rodata_3CC); the vector reserves one (0x3EE4).
  {
    rstl::vector< CJointCollisionDescription > descs;
    descs.reserve(1);
    const CAnimData* animData = GetTargetActor(mgr)->GetModelData()->GetAnimationData(); // 0x3EF4
    const CSegId seg = animData->GetLocatorSegId(rstl::string_l("brainSpine_scale"));  // 0x3F1C
    descs.push_back(CJointCollisionDescription::SphereCollision(
        seg, CVector3f::Zero(), 5.f, rstl::string_l("HeartCollision"), 0.001f)); // 0x3F70, 0x3F9C
    // The owner is the tracked actor (guest 0xA1A, 0x3FE8), the area this actor's (0x3FCC), the
    // manager starts active (`li r8, 0x1`, 0x3FE4).
    CCollisionActorManager* manager =
        rs_new CCollisionActorManager(mgr, xa1a_, GetCurrentAreaId(), descs, true); // 0x3FF4
    delete x7cc_;                                                                    // 0x4004
    x7cc_ = manager;                                                                 // 0x4008
  }
  // Its collision actor (fn_16_2FA8, 0x4014) takes this actor's health info (the vtable slot 13
  // result copied into the actor's slot 12 one, 0x402C..0x4098), this actor's damage
  // vulnerability (slot 14, 0x40A4; SetDamageVulnerability at 0x40B8) and the materials kMT_Target
  // (0x28), kMT_Orbit (0x29) and kMT_SeekerTarget (0x3F) (0x40C4..0x40D0). Both sides are host
  // types, so the copies are host assignments: no retail bytes are moved.
  CCollisionActor* heart = GetCollisionActor(mgr);
  if (heart != nullptr) { // 0x401C
    *heart->HealthInfo() = *GetHealthInfo();
    heart->SetDamageVulnerability(*GetDamageVulnerability());
    heart->AddMaterial(kMT_Target, kMT_Orbit, kMT_SeekerTarget, mgr);
  }

  // The body: one sphere on segment 1 (`li r0, 0x1` at 0x4118), radius 9.0f (lbl_16_rodata_3D0),
  // name "SphereCollision" (+0x641), mass 0.001f (lbl_16_rodata_3CC).
  {
    rstl::vector< CJointCollisionDescription > descs;
    descs.reserve(1); // 0x40F8
    descs.push_back(CJointCollisionDescription::SphereCollision(
        CSegId(1), CVector3f::Zero(), 9.f, rstl::string_l("SphereCollision"),
        0.001f)); // 0x4148, 0x4174
    // The owner is the actor connected on CONNECT/'PLAY' (guest 0xA1E, 0x41B4).
    CCollisionActorManager* manager =
        rs_new CCollisionActorManager(mgr, xa1e_, GetCurrentAreaId(), descs, true); // 0x41C0
    delete x7d0_;                                                                    // 0x41D0
    x7d0_ = manager;                                                                 // 0x41D8
    // Its collision actor's filter (0x41D4..0x42D4): include kMT_Projectile (lbl_16_data_250,
    // 0x23) and kMT_Player (lbl_16_data_254, 0x20); exclude kMT_Platform (lbl_16_data_258, 0x31),
    // kMT_CollisionActor (lbl_16_data_25C, 0x2F), kMT_NoStaticCollision (lbl_16_data_260, 0x26)
    // and kMT_NoPlatformCollision (lbl_16_data_264, 0x3C); the type 3 (0x4280) is
    // kFT_IncludeExclude.
    const TUniqueId id = x7d0_->GetCollisionDescFromIndex(0).GetCollisionActorId(); // 0x42A8
    CCollisionActor* body = TCastToPtr< CCollisionActor >(mgr.ObjectById(id));       // 0x42C0
    if (body != nullptr) {                                                            // 0x42C8
      body->SetMaterialFilter(CMaterialFilter::MakeIncludeExclude(
          CMaterialList(kMT_Projectile, kMT_Player),
          CMaterialList(kMT_Platform, kMT_CollisionActor, kMT_NoStaticCollision,
                        kMT_NoPlatformCollision)));
    }
  }

  // The tentacles: 8 * 5 boxes between the joint pairs of skTentacleJointPairs (the loops at
  // 0x4338..0x44A0), on this actor's own animation data (+0x60 -> +0x10, 0x430C); the vector
  // reserves 0x28 (0x4308).
  {
    rstl::vector< CJointCollisionDescription > descs;
    descs.reserve(0x28);
    const CAnimData* animData = GetModelData()->GetAnimationData();
    for (int tentacle = 0; tentacle < 8; ++tentacle) {
      for (int joint = 0; joint < 5; ++joint) {
        const SEmperorIngStage1JointPair& pair = skTentacleJointPairs[tentacle][joint];
        const float extent = pair.extent;                                            // 0x4340
        const CSegId from = animData->GetLocatorSegId(rstl::string_l(pair.from));   // 0x435C
        const CSegId to = animData->GetLocatorSegId(rstl::string_l(pair.to));       // 0x4388
        // The name is from + "->" (+0x651) + to (fn_16_4640, 0x43C8 and 0x43E4); the
        // orientation is 1 (0x4410, kOT_BetweenJoints) and the mass 10.0f (lbl_16_rodata_390).
        descs.push_back(CJointCollisionDescription::OBBAutoSizeCollision(
            from, to, CVector3f(extent, extent, extent),
            CJointCollisionDescription::kOT_BetweenJoints,
            rstl::string_l(pair.from) + rstl::string("->") + rstl::string_l(pair.to),
            10.f)); // 0x4420, 0x4470
      }
    }
    // The owner is this actor (guest +0x8, 0x44E0).
    CCollisionActorManager* manager =
        rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), descs, true); // 0x44F0
    delete x7c8_;                                                                           // 0x4500
    x7c8_ = manager;                                                                        // 0x4504
  }

  // This actor's own filter (0x4514..0x45BC): include kMT_Solid (lbl_16_data_268, 0x3B); exclude
  // kMT_CollisionActor (lbl_16_data_26C, 0x2F), kMT_Character (lbl_16_data_270, 0x21),
  // kMT_AIPassthrough (lbl_16_data_274, 0x1C) and kMT_Player (lbl_16_data_278, 0x20); type 3.
  SetMaterialFilter(CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid),
      CMaterialList(kMT_CollisionActor, kMT_Character, kMT_AIPassthrough, kMT_Player)));
  RemoveMaterial(kMT_Target, kMT_Orbit, kMT_SeekerTarget, mgr); // 0x45D4

  // The tentacles' collision actors go inactive (0x45DC..0x45F8), the body's sphere active (index
  // 9, 0x460C) and the heart's inactive (index 8, 0x4620).
  for (int index = 0; index < 8; ++index) {
    SetCollisionActorActive(mgr, index, false);
  }
  SetCollisionActorActive(mgr, 9, true);
  SetCollisionActorActive(mgr, 8, false);
}

// fn_16_3D64 (0x3D64..0x3EAC): sets a group's collision actors active - a tentacle's five (0..7,
// the descriptions index * 5 .. index * 5 + 4 of the manager at guest 0x7C8, 0x3D90), 9 the
// body's sphere (guest 0x7D0, 0x3E0C) and 8 the heart's (guest 0x7CC, 0x3E54) - through the
// collision actor's vtable slot 5, SetActive (0x3DDC, 0x3E4C, 0x3E94). Any other index does
// nothing.
// @ fn_16_3D64
void CEmperorIngStage1::SetCollisionActorActive(CStateManager& mgr, int index, bool active) {
  if (index >= 0 && index <= 7) {
    uint descIndex = index * 5;
    for (int i = 0; i < 5; ++i) {
      const TUniqueId id = x7c8_->GetCollisionDescFromIndex(descIndex).GetCollisionActorId();
      CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id));
      if (actor != nullptr) {
        actor->SetActive(active);
      }
      ++descIndex;
    }
    return;
  }
  switch (index) {
    case 9: {
      const TUniqueId id = x7d0_->GetCollisionDescFromIndex(0).GetCollisionActorId();
      CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id));
      if (actor != nullptr) {
        actor->SetActive(active);
      }
      break;
    }
    case 8: {
      const TUniqueId id = x7cc_->GetCollisionDescFromIndex(0).GetCollisionActorId();
      CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id));
      if (actor != nullptr) {
        actor->SetActive(active);
      }
      break;
    }
  }
}

// fn_16_830 (0x830..0x8E4): the script is told which stage this is - 1 sends IS01 (0x87C), 2
// sends IS02 (0x8A8) and every other value IS00 (0x8D0).
// @ fn_16_830
void CEmperorIngStage1::SendStageMessage(CStateManager& mgr, int stage) {
  switch (stage) {
    case 1:
      SendScriptMsgs(kSS_InternalState1, mgr, kInvalidUniqueId, kSM_None);
      break;
    case 2:
      SendScriptMsgs(kSS_InternalState2, mgr, kInvalidUniqueId, kSM_None);
      break;
    default:
      SendScriptMsgs(kSS_InternalState0, mgr, kInvalidUniqueId, kSM_None);
      break;
  }
}

// fn_16_353C (0x353C..0x3668): the damage vulnerability every live tentacle gets, and this actor's
// two vulnerability effect states. The mode picks the vulnerability: 0 the immune one (the DOL's
// CDamageVulnerability::ImmuneVulnerability, 0x35AC), 2 the melee one (guest 0xB2C, 0x3584), 3 the
// projectile one (guest 0xB5C, 0x358C) with "DarkVulnerability" on, 4 the warp one (guest 0xAFC,
// 0x3598) with "LightVulnerability" on, every other mode the normal one (guest 0xACC, 0x35A4). The
// effect names are the strings lbl_16_rodata_344 and lbl_16_rodata_348 point at (SetEffectState
// at 0x35DC and 0x3610), and the tentacles' loop is 0x3630..0x3654 (fn_16_1E20 per slot).
// @ fn_16_353C
void CEmperorIngStage1::SetTentacleVulnerability(CStateManager& mgr, int mode) {
  const CDamageVulnerability* vulnerability;
  bool dark = false;
  bool light = false;
  switch (mode) {
    case 2:
      vulnerability = &mMeleeVulnerability;
      break;
    case 3:
      vulnerability = &mProjectileVulnerability;
      dark = true;
      break;
    case 4:
      vulnerability = &mWarpVulnerability;
      light = true;
      break;
    default:
      vulnerability = &mNormalVulnerability;
      break;
    case 0:
      vulnerability = &CDamageVulnerability::ImmuneVulnerabilty();
      break;
  }
  AnimationData()->SetEffectState(rstl::string_l("DarkVulnerability"), dark, mgr);
  AnimationData()->SetEffectState(rstl::string_l("LightVulnerability"), light, mgr);
  if (vulnerability != nullptr) {
    for (int i = 0; i < mJoints.mCount; ++i) {
      SetJointVulnerability(mJoints.mJoints[i], mgr, *vulnerability);
    }
  }
}

// fn_16_1E20 (0x1E20..0x1E6C): the slot's tentacle collision actor (its owner's fn_16_3AA8 with
// the slot's index, 0x1E44) gets the damage vulnerability (0x1E54).
// @ fn_16_1E20
void CEmperorIngStage1::SetJointVulnerability(const CEmperorIngStage1Joint& joint,
                                              CStateManager& mgr,
                                              const CDamageVulnerability& vulnerability) {
  CCollisionActor* actor = joint.mOwner->GetTentacleActor(mgr, joint.mIndex);
  if (actor != nullptr) {
    actor->SetDamageVulnerability(vulnerability);
  }
}

// fn_16_7B40 (0x7B40..0x7C28): the tracked actor's (fn_16_302C) "sphere_base" locator
// (lbl_16_rodata_37C) in world space (0x7B88, 0x7B98), the horizontal direction from it to the
// player (CStateManager+0x14FC, 0x7BB0; the third component is 0.0f from lbl_16_rodata_380,
// 0x7BF4), normalised (0x7BF8) and handed to fn_16_2C9C (0x7C08). Retail normalises without a
// CanBeNormalized test.
// @ fn_16_7B40
void CEmperorIngStage1::LookAtPlayer(CStateManager& mgr) {
  CActor* target = GetTargetActor(mgr);
  const CTransform4f locator =
      target->GetTransform() *
      target->GetModelData()->GetScaledLocatorTransform(rstl::string_l("sphere_base"));
  const CVector3f& playerPos = mgr.GetPlayer(0)->GetTranslation();
  CVector3f dir(playerPos.GetX() - locator.Get03(), playerPos.GetY() - locator.Get13(), 0.f);
  dir.Normalize();
  UpdateTurnAngle(mgr, dir);
}

// fn_16_2DDC (0x2DDC..0x2E1C, 0x40): the cross product of two vectors. The X is
// a.y * b.z - b.y * a.z (the fmsubs at 0x2DFC), the Y is a.z * b.x - b.z * a.x (the fmsubs at
// 0x2E04: f5 * f6 - f1, a.z * b.x - (b.z * a.x)) and the Z is a.x * b.y - b.x * a.y (the fmsubs at
// 0x2E0C: f3 * f7 - f0). The REL's own copy of the DOL's
// Cross__9CVector3fFRC9CVector3fRC9CVector3f (0x8001994C, 0x40 bytes - the same body, branch
// free), so it is the host's function and is not marked; the host's CVector3f::Cross is the
// same code.
static CVector3f CrossProduct(const CVector3f& a, const CVector3f& b) {
  return CVector3f(a.GetY() * b.GetZ() - b.GetY() * a.GetZ(),
                   a.GetZ() * b.GetX() - b.GetZ() * a.GetX(),
                   a.GetX() * b.GetY() - b.GetX() * a.GetY());
}

// fn_16_2C9C (0x2C9C..0x2DDC): the angle (guest 0xA2C) between the 'PLAY' actor's facing and the
// direction. Retail also builds the tracked actor's "sphere_base" world transform (0x2CFC..0x2D20)
// and never reads it; the call has no side effect, so the port keeps it for the call order only.
// The facing is the 'PLAY' actor's transform's Y column (the floats at +0x28, +0x38 and +0x48,
// 0x2D28) flattened (0.0f, 0x2D4C) and normalised (0x2D50); the angle is
// CVector3f::GetAngleDiff (0x2D5C) and it is negated when the cross product of the facing and the
// direction (fn_16_2DDC, 0x2D70) points down (its dot with the up vector below 0.0f, 0x2DA8).
// @ fn_16_2C9C
void CEmperorIngStage1::UpdateTurnAngle(CStateManager& mgr, const CVector3f& dir) {
  CActor* target = GetTargetActor(mgr);
  CActor* body = GetPlayActor(mgr); // fn_16_3060, 0x2CD4
  const CTransform4f unused =
      target->GetTransform() *
      target->GetModelData()->GetScaledLocatorTransform(rstl::string_l("sphere_base"));
  (void)unused;
  const CVector3f column = body->GetTransform().GetColumn(kDY);
  CVector3f facing(column.GetX(), column.GetY(), 0.f);
  facing.Normalize();
  xa2c_ = CVector3f::GetAngleDiff(facing, dir);
  if (CVector3f::Dot(CrossProduct(facing, dir), CVector3f::Up()) < 0.f) {  // fn_16_2DDC, 0x2D70
    xa2c_ = -xa2c_;
  }
}

// fn_16_2C3C (0x2C3C..0x2C9C): the turn angle (fn_16_2C9C, 0x2C80) against the 'PLAY' actor's
// (fn_16_3060, 0x2C58) own Y column (the floats at +0x28, +0x38 and +0x48), unflattened.
// @ fn_16_2C3C
void CEmperorIngStage1::ResetTurnAngle(CStateManager& mgr) {
  UpdateTurnAngle(mgr, GetPlayActor(mgr)->GetTransform().GetColumn(kDY));
}

// fn_16_8E4 (0x8E4..0x920): the attack timer (guest 0xA4C) takes the state manager's random
// (retail CStateManager+0x16E4) Range of attackIntervalMin and attackIntervalMax (guest 0xCF4 and
// 0xCF8, 0x904).
// @ fn_16_8E4
void CEmperorIngStage1::ResetAttackTimer(CStateManager& mgr) {
  xa4c_ = mgr.Random()->Range(mAttackIntervalMin, mAttackIntervalMax);
}

// fn_16_2FF8 (0x2FF8..0x302C): the actor connected on CONNECT/ACTIVATE (the id at guest 0xA1C).
// @ fn_16_2FF8
CActor* CEmperorIngStage1::GetActivateActor(CStateManager& mgr) const {
  return TCastToPtr< CActor >(mgr.ObjectById(xa1c_));
}

// fn_16_920 (0x920..0x9C0): the tracked actor's (fn_16_302C) "brainSpine_scale" locator
// (lbl_16_rodata_378) in world space (0x968, 0x978); the result is its translation (0x99C).
// Area 11 runs it on the natural path: the heart-stage cycles of tools/drivers/emperoring1_heartstage.py (no `fsm`, module note §12).
// @ fn_16_920
CVector3f CEmperorIngStage1::GetBrainSpinePosition(CStateManager& mgr) const {
  CActor* target = GetTargetActor(mgr);
  const CTransform4f locator =
      target->GetTransform() *
      target->GetModelData()->GetScaledLocatorTransform(rstl::string_l("brainSpine_scale"));
  return locator.GetTranslation();
}

// fn_16_CD0 (0xCD0..0xFF4): only when no beam is out (guest 0xA98 invalid, 0xD24). The beam info
// is guest 0xCA8's with attributes 0x201 (0xD34, then copied by fn_16_FF4); the damage is guest
// 0xC80's scaled by the argument, with no immunity (the 0x80 bit at 0xDC0). The beam is an
// "EmperorIngStage1Beam" CPlasmaProjectile (rs_new 0x6A8 at 0xDCC, ctor at 0xEA0): weapon type 0xB,
// identity transform, material 0x14, the new id, this area, the tracked actor (fn_16_302C) as
// owner, default asset info, attributes 0x21000. It gets damage duration 1.5f (lbl_16_rodata_394:
// attribute 0x1000 and +0x1C0, 0xED8) and is added (0xEE4); guest 0xA98 takes the id either way.
// The beam sound (guest 0xCF0) plays looped at the brain spine (fn_16_920, 0xEFC) with
// C3DEmitterParmData(1000.0f, 0.1f, 1, 0x7F, 0x14) and the up direction (0xF84) into guest 0xA9C;
// the "Telegraph" timer (0xA5C) takes 0.9833334f (lbl_16_rodata_3A0) and the refire timer (0xA60)
// 1.5f (0xFB0).
// Area 11 runs it on the natural path: the heart-stage cycles of tools/drivers/emperoring1_heartstage.py (no `fsm`, module note §12).
// @ fn_16_CD0
bool CEmperorIngStage1::FireBeam(CStateManager& mgr, float dt) {
  if (xa98_ != kInvalidUniqueId) {
    return false;
  }
  const CBeamInfo beamInfo = TLdrToBeamInfo(mBeamInfo, 0x201);
  const TUniqueId uid = mgr.AllocateUniqueId();
  // The retail reads the tracked actor's id without a null test (0xE10); the port does not crash.
  const CActor* owner = GetTargetActor(mgr);
  const TUniqueId ownerId = owner != nullptr ? owner->GetUniqueId() : kInvalidUniqueId;
  CDamageInfo damage = mBeamProjectile.GetDamage();
  damage.SetDamage(dt * damage.GetDamage());
  damage.SetNoImmunity(true);
  CPlasmaProjectile* beam = rs_new CPlasmaProjectile(
      mBeamProjectile.Token(), rstl::string_l("EmperorIngStage1Beam"), kWT_AI, beamInfo,
      CTransform4f::Identity(), kMT_ProjectilePassthrough, damage, uid, GetCurrentAreaId(),
      ownerId, CWeaponAssetInfo(), false,
      CWeapon::kPA_KeepInCinematic | CWeapon::kPA_BigStrike);
  if (beam != nullptr) {
    beam->SetDamageDuration(1.5f);
    mgr.AddObject(*beam);
  }
  xa98_ = uid;
  CAudioSys::C3DEmitterParmData parms(1000.f, 0.1f, 1, 0x7F, 0x14);
  parms.mPos = GetBrainSpinePosition(mgr);
  parms.mDir = CVector3f::Up();
  parms.mSfxId = mBeamSound;
  xa9c_ = CSfxManager::AddEmitter(parms, GetCurrentAreaId().Value(), true, true,
                                  CSfxManager::kMedPriority);
  xa5c_ = 0.9833334f;
  xa60_ = 1.5f;
  return true;
}

// fn_16_A64 (0xA64..0xCD0): the beam (guest 0xA98, 0xAA0; retail does not test its type) is aimed
// from the "brainSpine_scale" locator (fn_16_920). While it is inactive (CEntity::mActive, 0xABC)
// and its refire timer (guest 0xA60) has run below 0.0f, the aim point (guest 0xAA0) is reset to
// the player's position at the tracked actor's height (0xB08..0xB38) and the beam fires along
// CTransform4f::LookAt (0xB44, vtable slot 35 Fire at 0xB70). Then the aim point walks towards the
// player at 10.0f (lbl_16_rodata_390) units a second on the horizontal (0xBE4..0xC44), the beam's
// effect follows it (slot 33 UpdateFx at 0xC88) and its emitter (guest 0xA9C) sits on the beam's
// collision point (the beam's +0x444, mCollisionPoint, 0xCA8; volume 0x7F).
// Area 11 runs it on the natural path: the heart-stage cycles of tools/drivers/emperoring1_heartstage.py (no `fsm`, module note §12).
// @ fn_16_A64
void CEmperorIngStage1::UpdateBeam(float dt, CStateManager& mgr) {
  CEntity* entity = mgr.ObjectById(xa98_);
  if (entity == nullptr) {
    return;
  }
  CBeamProjectile* beam = static_cast< CBeamProjectile* >(entity);
  xa60_ -= dt;
  if (!beam->GetActive() && xa60_ < 0.f) {
    const CVector3f origin = GetBrainSpinePosition(mgr);
    xaa0_ = mgr.GetPlayer(0)->GetTranslation();
    xaa0_.SetZ(GetTargetActor(mgr)->GetTranslation().GetZ());
    beam->Fire(CTransform4f::LookAt(origin, xaa0_, CVector3f::Up()), mgr, false);
  }
  const CVector3f origin = GetBrainSpinePosition(mgr);
  const CVector3f& playerPos = mgr.GetPlayer(0)->GetTranslation();
  CVector3f dir(playerPos.GetX() - xaa0_.GetX(), playerPos.GetY() - xaa0_.GetY(), 0.f);
  if (dir.CanBeNormalized()) {
    dir.Normalize();
    xaa0_ += (dir * 10.f) * dt;
  }
  beam->UpdateFx(CTransform4f::LookAt(origin, xaa0_, CVector3f::Up()), dt, mgr);
  CSfxManager::UpdateEmitter(xa9c_, beam->GetCurrentPos(), CVector3f::Up(), 0x7F);
}

// fn_16_9C0 (0x9C0..0xA64): when the beam (guest 0xA98) exists it is reset (vtable slot 34
// ResetBeam with false, 0xA0C), its emitter removed (0xA1C), the object deleted (0xA30), and the
// id and the emitter cleared (0xA44, 0xA48).
// Area 11 runs it on the natural path: the heart-stage cycles of tools/drivers/emperoring1_heartstage.py (no `fsm`, module note §12).
// @ fn_16_9C0
void CEmperorIngStage1::StopBeam(CStateManager& mgr) {
  CEntity* entity = mgr.ObjectById(xa98_);
  if (entity == nullptr) {
    return;
  }
  static_cast< CBeamProjectile* >(entity)->ResetBeam(mgr, false);
  CSfxManager::RemoveEmitter(xa9c_);
  mgr.DeleteObjectRequest(xa98_);
  xa98_ = kInvalidUniqueId;
  xa9c_ = CSfxHandle::NullHandle();
}

// fn_16_36E8 (0x36E8..0x3984): the tentacle joint collision descriptions. For each of the eight
// tentacle slots (the loop at 0x395C..0x3964) three parameter sets are searched for the best
// animation - the first with FromEnum(0), the second with FromEnum(2), the third with FromEnum(1),
// each of them paired with FromInt32(the index) - and when the first two found an animation (the
// tests at 0x3918 and 0x3920) the joint is built and added to the vector at guest 0x7D4
// (0x3928..0x3958).  The retail's parameter ORDER is FromInt32(the index) first and the FromEnum
// second: fn_16_337C gets r5 = the FromInt32 (0x378C, 0x382C, 0x38CC) and r6 = the FromEnum
// (0x3790, 0x3830, 0x38D0), and the second search's FromInt32 is the index too (r22 at 0x3818),
// not the constant 2.  The order matters: the parameter list is matched positionally, and with it
// swapped the searches answered with the same animation for every tentacle (the joints 3..7 got
// the joint 0's animation id), so one tentacle's state arm deleted another tentacle's additive
// animation and the tentacle never left the state 1.
// @ fn_16_36E8
void CEmperorIngStage1::SetupTentacleJoints(CStateManager& mgr) {
  // The database is this actor's own model's animation data at +0x3C (0x3708..0x3728).
  const CPASDatabase& db = GetModelData()->GetAnimationData()->GetPASDatabase();
  for (int index = 0; index < 8; ++index) {
    const CPASAnimParmData first(pas::EAnimationState(27), CPASAnimParm::FromInt32(index),
                                 CPASAnimParm::FromEnum(0), CPASAnimParm::NoParameter(),
                                 CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
                                 CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
                                 CPASAnimParm::NoParameter());
    const rstl::pair< float, int > found = db.FindBestAnimation(first, -1);
    int animId1 = found.first > 1.1920929e-07f ? found.second : -1;
    const CPASAnimParmData second(pas::EAnimationState(27), CPASAnimParm::FromInt32(index),
                                  CPASAnimParm::FromEnum(2), CPASAnimParm::NoParameter(),
                                  CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
                                  CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
                                  CPASAnimParm::NoParameter());
    const rstl::pair< float, int > found2 = db.FindBestAnimation(second, -1);
    int animId2 = found2.first > 1.1920929e-07f ? found2.second : -1;
    const CPASAnimParmData third(pas::EAnimationState(27), CPASAnimParm::FromInt32(index),
                                 CPASAnimParm::FromEnum(1), CPASAnimParm::NoParameter(),
                                 CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
                                 CPASAnimParm::NoParameter(), CPASAnimParm::NoParameter(),
                                 CPASAnimParm::NoParameter());
    const rstl::pair< float, int > found3 = db.FindBestAnimation(third, -1);
    int animId3 = found3.first > 1.1920929e-07f ? found3.second : -1;
    if (animId1 != -1 && animId2 != -1) {
      BuildTentacleJoint(index, animId1, animId2, animId3);
      AddTentacleJoint(mgr);
      AddTentacleJointToVector();
    }
  }
}

// fn_16_2B68 (0x2B68..0x2C3C): the joint description is filled in - the owner (0x2B6C), the
// radius (0.2B88, 0.0f from lbl_16_rodata_380), the index and the three animation ids
// (0x2B98..0x2BA4), the time and the damage hit time (0x2BA8, 0x2BAC) and the speed (0x2BB0),
// the flag byte with its six bits cleared (0x2BB4..0x2BF8), the state (0x2BFC), the id
// (0x2C04, kInvalidUniqueId) and the zero and up vectors (0x2C08..0x2C34).
// @ fn_16_2B68
void CEmperorIngStage1::BuildTentacleJoint(int index, int animId1, int animId2, int animId3) {
  mJoint.mOwner = this;
  mJoint.mTimer = 0.f;
  mJoint.mIndex = index;
  mJoint.mAnimId1 = animId1;
  mJoint.mAnimId2 = animId2;
  mJoint.mAnimId3 = animId3;
  mJoint.mTime = 0.f;
  mJoint.mDamageHitTime = CPatterned::skDamageHitTime;
  mJoint.mSpeed = 0.f;
  // The flag byte keeps its low two bits: the retail clears 0x80 (0x2BB4), 0x40 (0x2BC0), 0x20
  // (0x2BCC), 0x10 (0x2BD8), 0x08 (0x2BE4) and 0x04 (0x2BF0) one bit at a time.
  mJoint.mFlags &= 0x03;
  mJoint.mState = 0;
  mJoint.mId = kInvalidUniqueId;
  mJoint.mPosition = CVector3f::Zero();
  mJoint.mUp = CVector3f::Up();
}

// fn_16_1E6C (0x1E6C..0x1F34): the tentacle actor gets the module's health info (the copy at
// 0x1EB8..0x1F04 takes the module's guest 0xAAC..0xAC8) and then its materials (fn_16_1F34 at
// 0x1F14).
// @ fn_16_1E6C
void CEmperorIngStage1::AddTentacleJoint(CStateManager& mgr) {
  CCollisionActor* actor = GetTentacleActor(mgr, mJoint.mIndex);
  if (actor != nullptr) {
    *actor->HealthInfo() = mHealthInfo;
  }
  UpdateTentacleMaterials(mJoint, mgr, 1);
}

// fn_16_1F34 (0x1F34..0x1FC0): the tentacle actor's materials - kMT_Target (0x28), kMT_Orbit
// (0x29), kMT_SeekerTarget (0x3F) and kMT_ExcludeFromRadar (0x39) are added when the flag's low
// byte is not zero (the asm's clrlwi at 0x1F6C keeps the low byte of the flag) - 0x1F74..0x1F8C -
// and removed when it is (0x1F90).
// @ fn_16_1F34
void CEmperorIngStage1::UpdateTentacleMaterials(const CEmperorIngStage1Joint& joint,
                                                CStateManager& mgr, uint flag) {
  CCollisionActor* actor = GetTentacleActor(mgr, joint.mIndex);
  if (actor == nullptr) {
    return;
  }
  if ((flag & 0xff) != 0) {
    actor->AddMaterial(kMT_Target, kMT_Orbit, kMT_SeekerTarget, kMT_ExcludeFromRadar, mgr);
  } else {
    actor->RemoveMaterial(kMT_Target, kMT_Orbit, kMT_SeekerTarget, kMT_ExcludeFromRadar, mgr);
  }
}

// fn_16_3984 (0x3984..0x39CC): the joint is added to the vector at guest 0x7D4 - the element at
// the count (0x399C, stride 0x48) is copy-constructed (fn_16_39CC, 0x39A8) and the count is
// raised (0x39B4). fn_16_39CC (0x39CC..0x39EC, 0x20) only calls fn_16_39EC (0x39EC..0x3A14), the
// placement new's null test around the element's copy constructor fn_16_3A14 (0x3A14..0x3AA8);
// the vector has a fixed capacity and never grows, so the host assignment is all three.
// @ fn_16_3984
void CEmperorIngStage1::AddTentacleJointToVector() {
  mJoints.mJoints[mJoints.mCount] = mJoint;  // fn_16_39CC, fn_16_39EC and fn_16_3A14: the joint is copy-constructed into the vector
  mJoints.mCount += 1;
}

// fn_16_3AA8 (0x3AA8..0x3AFC): the tentacle's collision actor - the owner's first collision actor
// manager (guest 0x7C8) holds five joint descriptions per tentacle: the index is scaled by five
// (0x3AB4) and the group starts four earlier (the callers pass 4 in r6 - `li r6, 0x4` at 0x1E2C,
// 0x1E74, 0x1F58, 0x1FE0, 0x2560 and 0x7FDC), so the tentacle's own description is the last of
// its five. The description's collision actor id (at +0x3C) is looked up in the state manager
// (0x3AE0) and cast to a collision actor (0x3AE4).
// @ fn_16_3AA8
CCollisionActor* CEmperorIngStage1::GetTentacleActor(CStateManager& mgr, int index) {
  const uint descIndex = 4 + index * 5; // 0x3AB4, 0x3AC0
  const TUniqueId id = x7c8_->GetCollisionDescFromIndex(descIndex).GetCollisionActorId(); // 0x3AC8
  return TCastToPtr< CCollisionActor >(mgr.ObjectById(id)); // 0x3AE0, 0x3AE4
}

// vtable slot 7 = fn_16_52FC (0x20): the retail override is the base class's own PreRender.
// @ fn_16_52FC
void CEmperorIngStage1::PreRender(CStateManager& mgr) { CPatterned::PreRender(mgr); }

// fn_16_1D38 (0x1D38..0x1E6C, 0xC4): the tentacle's draw flags for the model's draw pass. The
// joint's flag byte (the joint's +0x24) has its bit 3 (0x08) tested - the extrwi at 0x1D50 takes
// one bit at the bit 28, the bit 3 of the byte, so the mask is 1u << (31 - 28) = 0x08 - and the
// draw flags get the extra 0x80 bit when it is set (the ori at 0x1D94). The blend mode is opaque
// (0), the shader set 0, the depth flags 0x03 (the depth compare and the depth update) and the
// colour white (the CColor at 0x1D88).
// @ fn_16_1D38
CModelFlags CEmperorIngStage1::BuildJointModelFlags(const CEmperorIngStage1Joint& joint) const {
  uint flags = CModelFlags::kF_DepthCompare | CModelFlags::kF_DepthUpdate;
  if ((joint.mFlags & 0x08) != 0) {
    flags |= CModelFlags::kF_Unknown80; // 0x1D94: the ori with 0x80
  }
  CModelFlags modelFlags(CModelFlags::kT_Opaque, 0, static_cast< CModelFlags::EFlags >(flags),
                         CColor::White());
  // The retail leaves the flags' first word alone (fn_16_1D38 writes +0x04, +0x05, +0x06 and
  // +0x08 only); the host keeps that word in x0_, which nothing reads, so the port zeroes it
  // instead of leaving the stack's garbage in it.
  memset(&modelFlags, 0, sizeof(uint));
  return modelFlags;
}

// fn_16_1C94 (0x1C94..0x1CF4, 0x60): the tentacle's damage colour - the colour lerps from the
// damage colour (CPatterned::skDamageColor) to black with the joint's damage hit time over
// skDamageHitTime (the fdivs at 0x1CD4), so a tentacle that was just hit is dark red and a
// tentacle that was never hit is black.
// fn_16_1C94 is called in the port's render path (the colour of the tentacle's pass, 0x545C):
// an entry once-log proved it runs in area 11.
// @ fn_16_1C94
CColor CEmperorIngStage1::GetJointDamageColor(const CEmperorIngStage1Joint& joint) const {
  const float t = joint.mDamageHitTime / CPatterned::skDamageHitTime; // 0x1CC0, 0x1CD4
  return CColor::Lerp(CPatterned::skDamageColor, CColor::Black(), t); // 0x1CD8
}

// fn_16_1C5C (0x1C5C..0x1C94, 0x38): the tentacle's plane - the joint's up vector is the normal
// and the constant is the position's dot with it (the fmadds at 0x1C78 and 0x1C84).
// fn_16_1C5C is called in the port's render path (the plane of the tentacle's pass, 0x5484): an
// entry once-log proved it runs in area 11.
// @ fn_16_1C5C
CPlane CEmperorIngStage1::GetJointPlane(const CEmperorIngStage1Joint& joint) const {
  const CVector3f up = joint.mUp; // 0x1C6C, 0x1C74, 0x1C7C
  return CPlane(CVector3f::Dot(joint.mPosition, up), // 0x1C68, 0x1C78, 0x1C84
                CUnitVector3f(up, CUnitVector3f::kN_No));
}

// fn_16_1CF4 (0x1CF4..0x1D38, 0x44): the tentacle's draw mask - the joint's index (the joint's
// +0x08) picks one of the eight masks at lbl_16_rodata_300 (0x1D18, 0x1D20); an entry is a u64
// whose high half is zero and whose low half holds the mask (the register pair holds the high
// half in r3 and the low half in r4, 0x1D24, 0x1D28), and the index 0 also gets the bit
// 0x02000000 (the lis 0x200 at 0x1D00, which lands in the low half, 0x1D30).
// @ fn_16_1CF4
u64 CEmperorIngStage1::GetJointDrawMask(const CEmperorIngStage1Joint& joint) const {
  static const u64 skMasks[8] = {0x200, 0x400, 0x100, 0x1000, 0x8, 0x4, 0x10, 0x40};
  u64 mask = skMasks[joint.mIndex]; // 0x1D14, 0x1D20
  if (joint.mIndex == 0) {
    mask |= 0x02000000; // 0x1D00, 0x1D30
  }
  return mask;
}

// vtable slot 9 = fn_16_531C (0x531C..0x55D4, 0x2B8): retail's own render path. The model is
// drawn eight times, once per tentacle, each pass with that tentacle's draw flags, colour, plane
// and mask; the mask picks the model's surfaces (the material mask), so every surface is drawn
// once, by the pass that owns it.
// @ fn_16_531C
void CEmperorIngStage1::Render(const CStateManager& mgr) const {
  // 0x5340: the particle POI mask and target - the flag byte at the guest 0x421's bit 0 (the
  // host's mDrawParticles, as the host's CPatterned::Render asks for the same pair).
  uint mask = 0;
  uint target = 0;
  if (mDrawParticles) {
    mgr.GetCharacterRenderMaskAndTarget(mask, target);
  }
  RenderSystemsToBeDrawnFirst(mgr, mask, target); // 0x535C, the vtable slot 46
  // 0x537C: the point generator particles (the byte at the guest 0x152's bit 6).
  if (GetPointGeneratorParticles()) {
    mgr.SetupParticleHook(*this); // 0x5390
  }
  // 0x5394: the rendering model; 0x53AC: the alpha buffer's value.
  const CModelData::EWhichModel which = CModelData::GetRenderingModel(mgr);
  const int alpha = GetRenderAlphaBufferAlpha(mgr);
  if (alpha != -1) {
    gpRender->SetDestinationAlpha(alpha); // 0x53BC, the vtable slot 0x128
  }
  // 0x53DC: the eight tentacle passes.
  rstl::vector< CModelFlags > flags;
  rstl::vector< CColor > colors;
  rstl::vector< CPlane > planes;
  rstl::vector< u64 > masks;
  flags.reserve(8);
  colors.reserve(8);
  planes.reserve(8);
  masks.reserve(8);
  for (int i = 0; i < 8; ++i) {
    const CEmperorIngStage1Joint& joint = mJoints.mJoints[i];
    flags.push_back(BuildJointModelFlags(joint)); // fn_16_1D38, 0x540C
    colors.push_back(GetJointDamageColor(joint)); // fn_16_1C94, 0x545C
    planes.push_back(GetJointPlane(joint)); // fn_16_1C5C, 0x5484
    masks.push_back(GetJointDrawMask(joint)); // fn_16_1CF4, 0x54C4
  }
  // 0x54FC: the model is drawn once per pass (the count 8 at 0x54E4).
  GetModelData()->RenderModelMultipleTimesWithFlags(which, GetTransform(), GetActorLights(),
                                                     flags.data(), masks.data(), colors.data(),
                                                     planes.data(), 8);
  if (alpha != -1) {
    gpRender->DisableDestinationAlpha(); // 0x5518, the vtable slot 0x12C
  }
  // 0x5580: the point generator particles' own draw.
  if (GetPointGeneratorParticles()) {
    CSkinnedModel::ClearPointGeneratorFunc(); // 0x558C
    mgr.GetActorModelParticles()->Render(mgr, *this); // 0x559C
  }
  RenderSystemsToBeDrawnLast(mgr, mask, target); // 0x55A0, the vtable slot 47
}

// vtable slot 26 = fn_16_7F8C (0x7F8C..0x853C, 0x5B0): retail's user-event switch, which drives
// the tentacles and the heart. The switch is the jump table at lbl_16_data_314 (29 entries; the
// event type is the index); the default entry (fn_16_7F8C+0x58C) and every type above 28 (the
// bgt at 0x7FAC) call the base's own DoUserAnimEvent (the call at 0x8524).
// The hook pass dispatched all 29 event types: the retail's six case bodies (the types 0, 7,
// 17, 22, 27 and 28) and, for the other 23, the base's own handling.
// @ fn_16_7F8C
void CEmperorIngStage1::DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node,
                                        EUserEventType type, float dt) {
  switch (type) {
    case kUE_Projectile:
      // 0x8024: the effect-state timer (0xA78) drops to 0.0f; only a charged projectile attack
      // (0xA74 bit 0x08, 0x8034) fires. The shot starts at the event's locator (0x8048) in world
      // space (0x8070), aimed at the player (LookAt with up, 0x80BC). It is an active
      // CEnergyProjectile (rs_new 0x568 at 0x80E4, ctor at 0x81C8) with guest 0xBB8's weapon, the
      // damage's weapon mode as type, material 0x21, guest 0xBC4's damage, this actor as owner, no
      // homing target, no attributes, unit scale, guest 0xBE0's visor particle (fn_16_853C, no
      // sound, collide message), impact sound on, charge factor 1.0f and impact distances 4.0f
      // (lbl_16_rodata_3B4/38C); it is added at 0x81F0.
      // fn_16_853C (0x853C..0x8600) copies the visor effect's particle descriptor into the
      // projectile: fn_16_85C8 (0x85C8..0x868C) copies the emitter's token and its vectors,
      // fn_16_867C (0x867C..0x8740) copies the token, fn_16_86BC -> fn_16_86DC ->
      // fn_16_8704 (0x8704..0x87C8) copies and locks the CToken and copies the id, and
      // fn_16_8980 (0x8980..0x8A44) sets the flag first. The host's
      // CImpactVisorEffect::ParticleEffect takes the locked token itself, so the functions are
      // the retail functions are only cited here.
      xa78_ = 0.f;
      if ((xa74_ & 0x08) != 0) {
        const CVector3f origin =
            GetTransform() * GetScaledLocatorTransform(node.GetLocatorName()).GetTranslation();
        const CTransform4f xf = CTransform4f::LookAt(
            origin, mgr.GetPlayer(0)->GetTranslation(), CVector3f::Up());
        CEnergyProjectile* projectile = rs_new CEnergyProjectile(
            true, mProjectile.Token(),
            static_cast< EWeaponType >(mProjectile.GetDamage().GetWeaponMode1()), xf,
            kMT_Character, mProjectile.GetDamage(), mgr.AllocateUniqueId(), GetCurrentAreaId(),
            GetUniqueId(), kInvalidUniqueId, CWeapon::kPA_None, false, CVector3f::One(),
            CImpactVisorEffect::ParticleEffect(
                rstl::optional_object< TLockedToken< CGenDescription > >(mProjectileVisorEffect),
                CSfxManager::kInternalInvalidSfxId, true),
            false, true, false, 1.f, 4.f, 4.f);
        if (projectile != nullptr) {
          mgr.AddObject(projectile);
        }
      }
      return;
    case kUE_GenerateEnd:
      // 0x8390: the flag byte at guest 0xA74 gets its bit 4 (0x10) set - the rlwimi at 0x8398
      // rotates 1 left by 4 (0x10) into the bit 27, the bit 4 of the byte (1u << (31 - 27)).
      xa74_ |= 0x10; // 0x8398, 0x839C
      return;
    case kUE_BeginAction: {
      // 0x83A4: the shock wave. The id comes from the state manager (0x83A8) and is kept at
      // guest 0xA90 (0x83B8); the CShockWave is built with it (0x8464).
      const TUniqueId id = mgr.AllocateUniqueId(); // 0x83A8
      xa90_ = id; // 0x83B8
      // 0x83BC: the tracked actor's transform (the retail's +0x24) is the shock wave's.
      const CTransform4f xf = GetTargetActor(mgr)->GetTransform(); // fn_16_302C
      // 0x83E0: the CShockWave (0x9F8 bytes) - the name is the string at lbl_16_rodata_41C's
      // 0x685 ("Shockwave", 0x845C), the entity info is the area (the guest +0x04, 0x8438), no
      // connections, active and no editor id (0x841C, 0x8404), the parent is this actor's id
      // (the guest +0x08, 0x8400), the shock wave info is the one at guest 0xC40 (0x8494), and
      // the two floats are 4.0f (lbl_16_rodata_38C) and 0.4f (lbl_16_rodata_40C).
      CShockWave* wave =
          rs_new CShockWave(id, rstl::string_l("Shockwave"),
                             CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true), xf,
                             GetUniqueId(), mShockWaveInfo, 4.f, 0.4f);
      if (wave != nullptr) { // 0x83F8
        mgr.AddObject(wave); // 0x84AC
      }
      // 0x84E8: the emitter for the shock wave's travel sound (the guest 0xC7C) on this actor's
      // position (the guest 0x54); the handle is kept at guest 0xA8C (0x8510).
      xa8c_ = CSfxManager::AddEmitter(mShockWaveTravelSound, GetTranslation(),
                                     GetCurrentAreaId().Value(), true, false,
                                     CSfxManager::kMedPriority);
      return;
    }
    case kUE_BreakLockOn:
      // 0x7FC8: every tentacle's collision actor becomes the player's orbit target, so the
      // camera orbits the tentacle. The loop runs over the sub-stage's tentacle count
      // (fn_16_2F70, 0x8014) and the request is the player's 8 (0x7FF8).
      for (int i = 0; i < TentacleCount(); ++i) { // 0x8008, 0x8014, 0x801C
        CCollisionActor* actor = GetTentacleActor(mgr, i); // fn_16_3AA8, 0x7FE0
        if (actor != nullptr) {                            // 0x7FE4
          mgr.GetPlayer(0)->SetOrbitRequestForTarget(actor->GetUniqueId(), // 0x8008
                                                      CPlayer::kOR_ActivateOrbitSource, mgr);
        }
      }
      return;
    case kUE_EventStart: {
      // 0x81F8: the tentacle's portal starts, but only while the flag byte's bit 5 (0x20) is
      // set - the extrwi at 0x81FC takes one bit at the bit 26, the bit 5 of the byte, so the
      // mask is 1u << (31 - 26) = 0x20.
      if ((xa74_ & 0x20) == 0) {
        return; // 0x8200
      }
      // 0x8204: the locator's transform in world space; 0x8230: its position (the fourth
      // column) and 0x8294: its Y column (the second basis vector), negated for the effect
      // (the fnegs at 0x82A8).
      const CTransform4f xf = GetTransform() * GetScaledLocatorTransform(node.GetLocatorName());
      const CVector3f pos = xf.GetTranslation();
      const CVector3f up = -xf.GetColumn(kDY);
      // 0x8254: the locator's name is one of the eight portals (lbl_16_rodata_1E0); the
      // tentacle's index is the name's place in the list (0x8298).
      for (int i = 0; i < 8; ++i) {
        if (CStringExtras::CompareCaseInsensitive(rstl::string_l(skPortalNames[i]),
                                                  node.GetLocatorName()) == 0) {
          if (i >= TentacleCount()) { // 0x8284, 0x8290
            break;
          }
          // 0x82D0: the module's own function (fn_16_1A50) starts the portal's effect.
StartTentaclePortal(i, pos, up, mgr);
          break;
        }
      }
      return;
    }
    case kUE_EventStop:
      // 0x82EC: the tentacle's portal stops. The retail builds the locator's world transform
      // here (0x82F0..0x8314) but never reads it.
      // 0x8324: the locator's name is one of the eight portals; the tentacle's index is the
      // name's place in the list (0x8364).
      for (int i = 0; i < 8; ++i) {
        if (CStringExtras::CompareCaseInsensitive(rstl::string_l(skPortalNames[i]),
                                                  node.GetLocatorName()) == 0) {
          if (i >= TentacleCount()) { // 0x8354, 0x8360
            break;
          }
          // 0x8374: the module's own function (fn_16_1950) stops the portal's effect.
          StopTentaclePortal(i, mgr);
          break;
        }
      }
      return;
    default:
      CPatterned::DoUserAnimEvent(mgr, node, type, dt);
      return;
  }
}

// fn_16_9BE8 (0x9BE8..0x9E48, 0x260): the tentacle's portal effect is created and added to the
// state manager; its id is returned (the caller keeps it in the joint, 0x1AF4).
//   The id comes from the state manager (0x9C08).
//   The light parameters (0x9D74): CLightParameters(false, 1.0f, kST_Zero, 0.0f, 0.0f, Black,
//     true, kLO_NoShadowCast (2), kLR_FourFrames (2), the zero vector, 2, 2, false, 0, true) -
//     the retail's arguments in order (0x9C1C..0x9C74).
//   The spline (0x9D7C..0x9D9C): a default maya spline and fn_16_9E9C, which fills the game
//     spline description with the type 3 (kST_Bezier), the duration 1.0f and no closed loop (the
//     rlwimi at 0x9EDC takes the bit 24); the maya spline is destroyed right after (0x9DA4).
//   The effect (0x9DD4): the id, the name "WarpPortalEffect" (the string at lbl_16_rodata_41C's
//     0x68F), the entity info (the area, no connections, active and no editor id), the identity
//     transform, the one vector as the scale, the effect asset, the timers and the rates, the
//     three visor flags, the light parameters and the spline.
// Area 11 runs it on the natural path: the heart-stage cycles of tools/drivers/emperoring1_heartstage.py (no `fsm`, module note §12).
// @ fn_16_9BE8
TUniqueId CEmperorIngStage1::CreateWarpPortalEffect(CStateManager& mgr, CAssetId effectId,
                                                     TAreaId area) {
  const TUniqueId id = mgr.AllocateUniqueId();  // 0x9C08
  const CLightParameters lightParams(false, 1.f, CLightParameters::kST_Zero, 0.f, 0.f,
                                     CColor::Black(), true, CLightParameters::kLO_NoShadowCast,
                                     CLightParameters::kLR_FourFrames, CVector3f::Zero(), 2, 2,
                                     false, 0, true);  // 0x9D74
  CMayaSpline mayaSpline;  // 0x9D7C
  const CGameSplineDesc spline(mayaSpline, CMotionSpline::kST_Bezier, 1.f, false);  // 0x9D9C  // fn_16_9E9C, fn_16_9F04 and fn_16_A004: the game spline description is built
  // fn_16_A1D4 (0xA1D4..0xA22C) destroys the CMayaSpline (0x9D6C) and calls fn_16_9FB0
  // (0x9FB0..0xA004) for its member; fn_16_9E48 (0x9E48..0x9E9C) destroys the CGameSplineDesc
  // (0x9EEC). The host's CMayaSpline and CGameSplineDesc have their own destructors.
  CScriptEffect* effect = rs_new CScriptEffect(
      id, rstl::string_l("WarpPortalEffect"),  // 0x9DD4
      CEntityInfo(area, CEntity::NullConnectionList, true), CTransform4f::Identity(),  // fn_16_67EC and fn_16_6844: the entity info's and the connection list's destructors
      CVector3f::One(), effectId, true, false, true, false, 1.f, 1.f, 0.f, 0.f, false, 1.f, 2.f,
      1.f, true, true, true, lightParams, false, spline, false, false, false,
      CScriptEffect::kRO_Normal);
  if (effect != nullptr) {  // 0x9DDC: the retail's own test (the new returns null when the
                            // allocation fails)
    mgr.AddObject(effect);  // 0x9DE4
  }
  return id;
}

// fn_16_1A50 (0x1A50..0x1C5C, 0x20C): the tentacle's portal starts - the joint's state (the
// joint's +0x28) decides whether anything happens (0x1A70, the compare with 0), the joint's flag
// byte gets its bit 3 (0x08) set (the rlwimi at 0x1A84), the joint takes the position and the up
// vector (0x1A88..0x1ABC), the portal's effect is the joint's id (the joint's +0x2C) and it is
// created when the joint has none (fn_16_9BE8, 0x1AD0). The effect looks along the up vector
// from the joint's position (the fadds at 0x1B34..0x1B4C and the LookAt at 0x1B60) and it hears
// "ACTV" (kSM_Activate, 0x41435456, 0x1B84).
// Area 11 runs it on the natural path: the heart-stage cycles of tools/drivers/emperoring1_heartstage.py (no `fsm`, module note §12).
// @ fn_16_1A50
void CEmperorIngStage1::StartTentaclePortal(int index, const CVector3f& pos, const CVector3f& up,
                                            CStateManager& mgr) {
  CEmperorIngStage1Joint& joint = mJoints.mJoints[index];
  if (joint.mState == 0) {
    return; // 0x1A74, 0x1A78
  }
  joint.mFlags |= 0x08; // 0x1A84
  joint.mPosition = pos; // 0x1A88..0x1AA8
  joint.mUp = up; // 0x1AAC..0x1ABC
  if (joint.mId == kInvalidUniqueId) {
    // 0x1AD0: the portal's effect is created in this actor's area (the owner's +0x04) from the
    // portal effect asset (the owner's +0xC00) - fn_16_9BE8.
    joint.mId = CreateWarpPortalEffect(mgr, mWarpPortalEffect, GetCurrentAreaId());
  }
  // 0x1AFC: the joint's id is the portal's effect.
  CScriptEffect* effect = TCastToPtr< CScriptEffect >(mgr.ObjectById(joint.mId));
  if (effect == nullptr) { // 0x1B14, 0x1B18: the retail's own test
    return;
  }
  // 0x1B1C: the effect looks along the up vector from the joint's position.
  effect->SetTransform(CTransform4f::LookAt(joint.mPosition, joint.mPosition + joint.mUp,
                                            CVector3f::Up())); // 0x1B60, 0x1B6C
  // 0x1B70: the message - the sender is this actor (0x1B80), the id the joint's (0x1B88) and the
  // state the invalid one (0x1B7C).
  effect->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), joint.mId, kSM_Activate)); // 0x1BB0
  // 0x1BD0: the emitter for the warp sound (the guest 0xC2E) at the effect's own position: the
  // max distance is 50.0f (lbl_16_rodata_3BC), the distance compensation 0.1f
  // (lbl_16_rodata_39C), the flags 1, the max volume 0x7F and the min volume 0 (0x1BF4). The
  // emitter's area is the effect's own (0x1C38) and the priority the medium one (0x1C10). The
  // retail throws the handle away.
  CAudioSys::C3DEmitterParmData parms(50.f, 0.1f, 1, 0x7F, 0);
  parms.mPos = effect->GetTranslation();
  parms.mSfxId = mWarpSound;
  CSfxManager::AddEmitter(parms, effect->GetCurrentAreaId().Value(), true, false,
                          CSfxManager::kMedPriority);  // 0x1C3C
}

// The seven locators the portal actor looks for the player in (lbl_16_rodata_200): the eighth
// tentacle's spines.
static const char* const skWarpPortalStabLocators[7] = {
    "eight_Spine_1", "eight_Spine_2", "eight_Spine_3", "eight_Spine_4",
    "eight_Spine_5", "eight_Spine_6", "eight_Spine_7"};

// fn_16_1720 (0x1720..0x18E8, 0x1C8): the "Attack Tip" warp portal's constructor.
//   The base call (0x1848): CPhysicsActor with the uid, the name, the entity info, the flag 0,
//     the transform, the model data, an empty material list (the retail's sp+0x20 and sp+0x24),
//     the bounding box, the mover data, the actor parameters and the default step data.
//   The bounding box (0x1800): the corners (-2.0f, -2.0f, -2.0f) (lbl_16_rodata_3B8) and
//     (2.0f, 2.0f, 2.0f) (lbl_16_rodata_3B0).
//   The mover data (0x17F8, fn_16_18E8): the zero velocity, the identity angular velocity, the
//     zero momentum, the identity and the mass 1.0f.
//   The module's own fields (0x185C..0x18D0): the timer 0.0f (the guest 0x2D0), the invalid
//     effect id (0x2D4), the hit flag 0 (0x2D6), the portal's effect asset (0x2D8), the stab
//     damage (0x2DC..0x2F6) and the two warp sounds (0x2F8 and 0x2FA).
// @ fn_16_1720
CEmperorIngStage1WarpPortal::CEmperorIngStage1WarpPortal(
    TUniqueId uid, const rstl::string& name, const CEntityInfo& info, const CTransform4f& xf,
    const CModelData& modelData, const CActorParameters& actParms, CAssetId portalEffect,
    const CDamageInfo& stabDamage, ushort warpSound, ushort warpSound2)
: CPhysicsActor(uid, name, info, 0, xf, modelData, CMaterialList(),
                CAABox(CVector3f(-2.f, -2.f, -2.f), CVector3f(2.f, 2.f, 2.f)),
                SMoverData(1.f, CVector3f::Zero(), CAxisAngle::Identity(), CVector3f::Zero(),
                           CAxisAngle::Identity()),
                actParms, CPhysicsActor::skDefaultStepData)
, mTimer(0.f)                  // guest 0x2D0, 0x185C
, mEffectId(kInvalidUniqueId)   // guest 0x2D4, 0x1878
, mHit(0)                      // guest 0x2D6, 0x187C
, mPortalEffect(portalEffect)  // guest 0x2D8, 0x1880
, mStabDamage(stabDamage)      // guest 0x2DC, 0x1884
, mWarpSound(warpSound)        // guest 0x2F8, 0x18CC
, mWarpSound2(warpSound2)      // guest 0x2FA, 0x18D0
{}

// fn_16_0 (0x0..0x38): the portal's destructor - it puts the vtable back
// (lbl_16_data_27C, 0x28) and calls the base's own (0x30); the host's default does both.
// @ fn_16_0
CEmperorIngStage1WarpPortal::~CEmperorIngStage1WarpPortal() = default;  // fn_16_0: the destructor - the vtable back and the base's own

// fn_16_1150 (0x1150..0x1574, 0x424): the portal's think.
//   0x1194: the model's animation data (the guest 0x60 -> 0x10, no null test) is set animating:
//     `lbz r0,0x2ac(r5); rlwimi r0,r6,7,24,24` (r6 = 1) at 0x11A4..0x11AC sets the byte's 0x80,
//     CAnimData::mAnimating - the DOL's CModelData::EnableLooping sets the same bit with the same
//     rlwimi (0x800E53E8) and CModelData::IsAnimating reads it (0x800E5C3C). Without it the
//     "Whole Body" animation never advances (its time remaining stays 1.2 s).
//   0x11B0: the base's own think.
//   0x11B4: the portal's timer (the guest 0x2D0) counts up; until it is past 0.75f
//     (lbl_16_rodata_3A4, the `ble` at 0x11D0 to the epilogue 0x1534) nothing else runs. Past it
//     the portal is made active (SetActive, vtable 0x1C, with `li r4,0x1` at 0x11D8) - the
//     portal is created inactive (0x65DC), so this is what lets it render (fn_16_1088).
//   0x1214: while the "Whole Body" animation is still running (IsAnimTimeRemaining with the
//     tolerance dt - 0.00000011920929f, lbl_16_rodata_3A8) the portal goes on to 0x131C; when
//     it is over the portal goes inactive (0x1258, `li r4,0x0`) and its effect (the guest 0x2D4)
//     gets the emitter for the second warp sound (the guest 0x2FA, 0x12BC) and "DCTV"
//     (kSM_Deactivate: `lis r3,0x4443; addi r6,r3,0x5456` at 0x12D4 = 0x44435456, 0x1318) -
//     but only while the effect is active (the byte at the effect's +0x20, 0x1294).
//   0x131C: past 5.0f (lbl_16_rodata_3AC) the portal asks to be deleted.
//   0x1348: the animation is updated.
//   0x1360: the portal hits the player once: the player's touch bounds against the boxes around
//     the seven "eight_Spine_*" locators (0x13E4), each 2.0f around the locator's world position
//     (0x1408), and the stab damage (the guest 0x2DC, 0x151C).
// Area 11 runs it on the natural path: the heart-stage cycles of tools/drivers/emperoring1_heartstage.py (no `fsm`, module note §12).
// The effect-is-gone (0x128C) and effect-inactive (0x1298) arms are forced by gdb
// (tools/cov/specs/emperoring1_portal_force.py); the null model / animation data (0x11F8, 0x1204)
// and no-touch-bounds (0x13CC) arms are dead (module note §12a).
// @ fn_16_1150
void CEmperorIngStage1WarpPortal::Think(float dt, CStateManager& mgr) {
  AnimationData()->SetIsAnimating(true);  // 0x1194..0x11AC
  CActor::Think(dt, mgr);  // 0x11B0
  mTimer += dt;             // 0x11B4
  if (mTimer <= 0.75f) {    // 0x11CC, lbl_16_rodata_3A4: `ble` to the epilogue (0x1534)
    return;
  }
  SetActive(true);  // 0x11E0 (li r4,0x1 at 0x11D8)
  // 0x11EC: the model data and its animation data have to be there; without them the retail
  // goes straight to the stab (0x1210 beq 0x1360).
  // The retail's arm at 0x11F8 (the animation data is null) cannot be taken in the port: the
  // portal is always built with the attack tip's model and its animation data.
  if (ModelData() != nullptr && ModelData()->AnimationData() != nullptr) {  // 0x11F4, 0x1200
    // 0x1214: the "Whole Body" animation is over?
    if (!AnimationData()->IsAnimTimeRemaining(dt - 0.00000011920929f,
                                              rstl::string_l("Whole Body"))) {
      SetActive(false);  // 0x1258 (li r4,0x0 at 0x125C)
      // 0x1270: the portal's effect (the guest 0x2D4).
      CScriptEffect* effect = TCastToPtr< CScriptEffect >(mgr.ObjectById(mEffectId));
      if (effect != nullptr && effect->GetActive()) {  // 0x128C, 0x1290: the retail's own tests
        // 0x12BC: the emitter for the second warp sound at the effect's own place.
        CSfxManager::AddEmitter(mWarpSound2, effect->GetTranslation(),
                                effect->GetCurrentAreaId().Value(), true, false,
                                CSfxManager::kMedPriority);
        // 0x1318: the effect gets "DCTV" (0x44435456, 0x12D4..0x12D8).
        effect->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), mEffectId, kSM_Deactivate));
      }
    }
    if (mTimer > 5.f) {  // 0x131C, lbl_16_rodata_3AC
      mgr.DeleteObjectRequest(GetUniqueId());  // 0x1344
    }
    UpdateAnimation(dt, mgr, true);  // 0x135C
  }
  // 0x1360: the portal hits the player only once.
  if (mHit == 0) {  // 0x1368
    const rstl::optional_object< CAABox > bounds = mgr.GetPlayer(0)->GetTouchBounds();  // 0x136C
    // 0x13CC: dead against the player - CPlayer::GetTouchBounds returns a box in both its arms.
    if (bounds) {
      for (int i = 0; i < 7; ++i) {  // 0x13E4, 0x152C: cmpwi r28,0x7; blt
        // 0x13F0..0x1404: the animation data's own locator (the guest 0x60 -> 0x10, no null test,
        // the CCharAnimTime pointer null: `li r6,0x0` at 0x13FC).
        const CVector3f pos = GetTransform() * AnimationData()
                                                   ->GetLocatorTransform(rstl::string_l(
                                                       skWarpPortalStabLocators[i]), nullptr)
                                                   .GetTranslation();  // 0x1408
        const CAABox box(pos - CVector3f(2.f, 2.f, 2.f), pos + CVector3f(2.f, 2.f, 2.f));
        if (box.DoBoundsOverlap(*bounds)) {  // 0x148C
          mHit = 1;                           // 0x14A0
          // 0x151C: the stab damage; the filter is built like the melee sweep's (0x149C-0x14F0:
          // include kMT_Solid from lbl_16_data_0, exclude empty).
          mgr.ApplyDamage(GetUniqueId(), mgr.GetPlayer(0)->GetUniqueId(), GetUniqueId(),
                          mStabDamage,
                          CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid),
                                                              CMaterialList()),
                          CVector3f::Zero());
          break;
        }
      }
    }
  }
}

// fn_16_1574 (0x1574..0x1720, 0x1AC): the portal's script messages.
//   kSM_Create (0x58435254, 0x15AC): the portal's effect (the guest 0x2D8) is created in this
//     actor's area (fn_16_9BE8, 0x15D8) and its id is kept (0x15E8); the effect takes this
//     actor's transform (0x1608) and hears "ACTV" (0x1620), and the emitter for the first warp
//     sound (the guest 0x2F8) is put at the effect's place (0x1678). The actor's colour
//     (0x16A0) is set to white with the alpha 0x80.
//   kSM_Delete (0x5844454C, 0x157C): the effect is deleted (0x1700).
// @ fn_16_1574
void CEmperorIngStage1WarpPortal::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  switch (msg.GetMessage()) {
    case kSM_Create: {
      mEffectId = CEmperorIngStage1::CreateWarpPortalEffect(mgr, mPortalEffect,
                                                             GetCurrentAreaId());  // 0x15D8
      // 0x15E0: the effect is looked up.
      CScriptEffect* effect = TCastToPtr< CScriptEffect >(mgr.ObjectById(mEffectId));
      if (effect != nullptr) {  // 0x1600: the retail's own test
        effect->SetTransform(GetTransform());  // 0x1608
        // 0x1620: the effect gets the "ACTV" message.
        effect->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), mEffectId, kSM_Activate));
        // 0x1668: the emitter for the first warp sound at the effect's own place.
        CSfxManager::AddEmitter(mWarpSound, effect->GetTranslation(),
                                effect->GetCurrentAreaId().Value(), true, false,
                                CSfxManager::kMedPriority);
      }
      // 0x168C: the actor's draw flags (the guest 0x100) are opaque white (lbl_16_rodata_3B4 =
      // 1.0f for all four channels) with the flags 0x3 | 0x80 (0x16B8, 0x16C8).
      SetModelFlags(CModelFlags(CModelFlags::kT_Opaque, 0,
                                static_cast< CModelFlags::EFlags >(CModelFlags::kF_DepthCompare |
                                                                   CModelFlags::kF_DepthUpdate |
                                                                   CModelFlags::kF_Unknown80),
                                CColor::White()));
      break;
    }
    case kSM_Delete:
      // 0x16F0: the effect is deleted.
      mgr.DeleteObjectRequest(mEffectId);
      break;
    default:
      break;
  }
}

// fn_16_1088 (0x1088..0x1150, 0xC8): the portal's render. The plane is the transform's Y
// column (the guest 0x28/0x38/0x48) as the normal and the position's dot with it as the
// constant (0x10C4..0x111C); the model data's world space portal plane takes it (0x1128) and
// the base's own render follows (0x1134). The retail's first call (the renderer's vtable slot
// 16 with the transform, 0x10A8) is the base's own work.
// It runs in area 11's natural heart-stage cycles (gcov 6/6, module note §12a): the portal is
// created inactive (0x65DC) and renders once its think activates it past 0.75f (0x11E0).
// @ fn_16_1088
void CEmperorIngStage1WarpPortal::Render(const CStateManager& mgr) const {
  const CVector3f normal = GetTransform().GetColumn(kDY);  // 0x10C4
  const CPlane plane(GetTranslation(), CUnitVector3f(normal));  // 0x10E0
  GetModelData()->SetupWorldSpacePortalPlane(GetTransform(), plane);  // 0x1128
  CPhysicsActor::Render(mgr);  // 0x1134
}

// fn_16_1950 (0x1950..0x1A14, 0xC4): the tentacle's portal stops - the joint's flag byte gets its
// bit 3 (0x08) cleared (the rlwimi at 0x1980), the joint's position is the zero vector and its up
// vector the up vector (0x1990..0x19C0), and the portal's effect (the joint's id, 0x19C4) hears
// "DCTV" (kSM_Deactivate, 0x44435456, 0x19F8) - the message's sender is this actor (0x19EC) and
// its id the joint's (0x19F4).
// Area 11 runs it on the natural path: the heart-stage cycles of tools/drivers/emperoring1_heartstage.py (no `fsm`, module note §12).
// @ fn_16_1950
void CEmperorIngStage1::StopTentaclePortal(int index, CStateManager& mgr) {
  CEmperorIngStage1Joint& joint = mJoints.mJoints[index];
  joint.mFlags &= ~0x08; // 0x1980
  joint.mPosition = CVector3f::Zero(); // 0x1990..0x19A8
  joint.mUp = CVector3f::Up(); // 0x19AC..0x19C0
  // 0x19C4: the joint's id is the portal's effect.
  CScriptEffect* effect = TCastToPtr< CScriptEffect >(mgr.ObjectById(joint.mId));
  if (effect == nullptr) { // 0x19D4, 0x19D8: the retail's own test
    return;
  }
  effect->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), joint.mId, kSM_Deactivate)); // 0x1A34
}

// vtable slot 28 = fn_16_7EA8 (0x7EA8..0x7F8C, 0xC4): retail's own sound handling for the boss.
// The sound is swallowed while the locator belongs to a tentacle that cannot be shot: the tentacle
// the segment belongs to (fn_16_3C58, 0x7F08) is -1 or its flag byte (the joint's +0x24, guest
// 0x7D8 + index * 0x48 + 0x24 = 0x7FC, read at 0x7F1C) has its bit 27 clear - the extrwi at 0x7F20
// takes one bit at the bit 27, the bit 4 of the byte, so the mask is 1u << (31 - 27) = 0x10. Only
// then does the base's own handling run (the call at 0x7F6C).
// Both arms run in area 11 (2026-10-21, the boss activated with `send u<uid> Activate`): the
// early return was taken 5627 times (the tentacle index 0..5, the joint's flag byte 0x00, 0x60
// or 0x80 - the tentacle is not shootable, so its locator's sound is swallowed) and the base's
// own handling ran 11838 times.
// @ fn_16_7EA8
void CEmperorIngStage1::ProcessSoundEvent(int sfxId, float weight, int flags, float fallOff,
                                          float maxDist, const CSegId& locator, ushort pitchStart,
                                          ushort pitchEnd, float pitchDuration, uchar minVol,
                                          uchar maxVol, float distanceSquared,
                                          const CVector3f& position, int aid, CStateManager& mgr,
                                          bool translateId) {
  const int index = GetTentacleIndex(locator); // 0x7F08
  if (index != -1 && (mJoints.mJoints[index].mFlags & 0x10) == 0) {
    return; // 0x7F20, 0x7F24
  }
  CPatterned::ProcessSoundEvent(sfxId, weight, flags, fallOff, maxDist, locator, pitchStart,
                                pitchEnd, pitchDuration, minVol, maxVol, distanceSquared, position,
                                aid, mgr, translateId);
}

// vtable slot 40 = fn_16_B9FC: `li r3, 0x1` - the actor can be shot.
// @ fn_16_B9FC
bool CEmperorIngStage1::CanBeShot(const CStateManager&, int) { return true; }

// vtable slot 41 = fn_16_BA04: `li r3, 0x0` - the actor does not listen for noises.
// @ fn_16_BA04
bool CEmperorIngStage1::IsListening() const { return false; }

// vtable slot 58 = fn_16_8AF4 (0x8AF4..0x8B80): CPatterned's own setup first (the call at
// 0x00008B08), then the module's three name tables are handed to the state machine at this+0x350
// - the triggers (lbl_16_data_19C, 6 entries) through the state manager's vtable slot +0x18 (the
// call at 0x00008B20), the states (lbl_16_data_94, 12 entries) through +0x14 (0x00008B40) and
// the codes (lbl_16_data_220, 3 entries) through +0x1C (0x00008B60).
// @ fn_16_8AF4
void CEmperorIngStage1::SetupStateMachine(CStateManager& mgr) {
  CPatterned::SetupStateMachine(mgr);
  StateMachine* stateMachine = mStateMachine.get();
  stateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  stateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  // The actor's machine is an FSM2 one (the host's CPatterned picks the FSM2 runtime when the
  // patterned info has a second machine id, and the run's probe of GetType() gives 1), so the
  // codes are dispatched: the FSM2's CGenericFSM2State::CallCode runs them. The host's FSM1
  // (TStateMachineState::SetCodeFunctions, an empty body like the DOL's fn_80194BF0) is not
  // used here.
  stateMachine->SetCodeFunctions(skCodes, ARRAY_SIZE(skCodes));
}

// ---------------------------------------------------------------------------------------------
// The body's FSM functions. The four triggers and the two codes the run reaches are retail's
// bodies; the rest are stand-ins that log the retail function they stand in for and return the
// neutral value (false for a trigger, nothing for a state or a code).

// fn_16_4BD4 (the trigger "AllTentaclesRetracted", lbl_16_data_19C entry 0): true while every
// live tentacle's state is 0, i.e. while they are all retracted. The loop runs over the live
// tentacles (the count at guest 0x7D4) and the state is the element's int at +0x28.
// @ fn_16_4BD4
bool CEmperorIngStage1::AllTentaclesRetracted(CStateManager& mgr, const CTriggerData& data) const {
  for (int i = 0; i < mJoints.mCount; ++i) {
    if (mJoints.mJoints[i].mState != 0) {
      return false;
    }
  }
  return true;
}

// fn_16_4BB4 (the trigger "HeartStageOver", lbl_16_data_19C entry 1): the heart-stage timer
// (guest 0xA3C, 0.0f in the constructor) has run out. The asm compares it with 0.0f
// (lbl_16_rodata_380) and folds "less than" and "equal" into one bit (cror eq,lt,eq at 0x4BC4),
// so the test is "<= 0.0f".
// Area 11 runs it on the natural path: the heart-stage cycles of tools/drivers/emperoring1_heartstage.py (no `fsm`, module note §12).
// @ fn_16_4BB4
bool CEmperorIngStage1::HeartStageOver(CStateManager& mgr, const CTriggerData& data) const {
  return xa3c_ <= 0.f;
}

// fn_16_4B6C (the trigger "IsDead", lbl_16_data_19C entry 2): the actor's health has run out.
// The asm calls the vtable slot at +0x3C (slot 13, GetHealthInfo) and reads the health info's
// second float (CHealthInfo::mHealth at +0x04), then compares it with 0.0f the same way as
// fn_16_4BB4 (cror eq,lt,eq at 0x4B98).
// Area 11 evaluates it on the natural path (the heart-stage cycles of tools/drivers/emperoring1_heartstage.py, module note §12); its true branch needs the boss dead. A temporary hook drove that (see the module note).
// @ fn_16_4B6C
bool CEmperorIngStage1::IsDead(CStateManager& mgr, const CTriggerData& data) const {
  return GetHealthInfo()->GetHP() <= 0.f;
}

// fn_16_4B20 (the trigger "ShouldMeleeAttack", lbl_16_data_19C entry 3): the squared
// horizontal distance to the player against 900 (30 units). The player is the state manager's
// first player actor (retail CStateManager+0x14FC, mPlayers[0]) and the position is the
// actor's own (CActor::mPosition at +0x54, so the two floats are X and Y).
// @ fn_16_4B20
bool CEmperorIngStage1::ShouldMeleeAttack(CStateManager& mgr, const CTriggerData& data) const {
  const CVector3f& mine = GetTranslation();
  const CVector3f& theirs = mgr.GetPlayer(0)->GetTranslation();
  const float dx = mine.GetX() - theirs.GetX();
  const float dy = mine.GetY() - theirs.GetY();
  return dx * dx + dy * dy < 900.f;
}

// fn_16_4B04 (the trigger "ShouldAttack", lbl_16_data_19C entry 4): the attack timer (guest
// 0xA4C, 4.0f in the constructor) has run below zero.
// @ fn_16_4B04
bool CEmperorIngStage1::ShouldAttack(CStateManager& mgr, const CTriggerData& data) const {
  return xa4c_ < 0.f;
}

// fn_16_4AEC (the trigger "ShouldTaunt", lbl_16_data_19C entry 5): the taunt timer (guest
// 0xA50, 0.0f in the constructor) is past the taunt frequency (guest 0xC34).
// @ fn_16_4AEC
bool CEmperorIngStage1::ShouldTaunt(CStateManager& mgr, const CTriggerData& data) const {
  return xa50_ > mTauntFrequency;
}

// The 12 states of lbl_16_data_94 (.data 0x94; fn_16_BF88's stores at 0x0000C0C0..0x0000C124 and
// 0x0000C214).

// fn_16_7C28 (the state "TrackPlayer", lbl_16_data_94 entry 0): on activate the script hears
// IS01 (fn_16_830, 0x7C64) and the tentacles take the normal vulnerability (fn_16_353C with 1,
// 0x7C74); on update the body stands still - a locomotion command with the zero vector for both
// the move and the face and the weight 1.0f (lbl_16_rodata_3B4, 0x7CC0) - and the turn angle
// towards the player is updated (fn_16_7B40, 0x7CCC). Deactivate does nothing.
// @ fn_16_7C28
void CEmperorIngStage1::TrackPlayer(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
    case kStateMsg_Activate:
      SendStageMessage(mgr, 1);
      SetTentacleVulnerability(mgr, 1);
      break;
    case kStateMsg_Update:
      BodyController()->CommandMgr().DeliverCmd(
          CBCLocomotionCmd(CVector3f::Zero(), CVector3f::Zero(), 1.f));
      LookAtPlayer(mgr);
      break;
    default:
      break;
  }
}

// fn_16_78AC (the state "BeginHeartStage", lbl_16_data_94 entry 1). The 'PLAY' actor (fn_16_3060,
// 0x78D4) and the actor connected on CONNECT/ACTIVATE (fn_16_2FF8, 0x78E8) are looked up first;
// retail tests neither.
//   Activate: the script hears IS00 (fn_16_830, 0x7920), the tentacles go immune (fn_16_353C with
//     0, 0x7930), the heart's collision actor goes active (fn_16_3D64 with 8, 0x7944), the
//     animation state (CPatterned guest 0x6B4) becomes ready (0x7950), the 'PLAY' actor plays the
//     second tentacle animation (guest 0xA24) with no blend, weight 1.0f (lbl_16_rodata_3B4) and
//     animating (SetAnimation at 0x799C), the CONNECT/ACTIVATE actor is sent ACTIVATE from this
//     actor (its vtable slot 4, AcceptScriptMsg, at 0x79FC), the tentacles' 0x80 flags are cleared
//     (fn_16_2EE0 with 0, 0x7A08) and the script hears IS04 (0x7A34).
//   Update: once the 'PLAY' actor's "Whole Body" animation (lbl_16_rodata_41C+0x627) has no time
//     left (IsAnimTimeRemaining at 0x7A60) the animation state is over (3, 0x7A7C).
//   Deactivate: the animation state is not ready (0, 0x7A8C), the 'PLAY' actor goes inactive (its
//     vtable slot 5, SetActive, at 0x7AA0) and so does the body's sphere (fn_16_3D64 with 9,
//     0x7AB4).
// @ fn_16_78AC
void CEmperorIngStage1::BeginHeartStage(CStateManager& mgr, EStateMsg msg, float dt) {
  CActor* body = GetPlayActor(mgr);
  CActor* activator = GetActivateActor(mgr);
  switch (msg) {
    case kStateMsg_Activate:
      SendStageMessage(mgr, 0);
      SetTentacleVulnerability(mgr, 0);
      SetCollisionActorActive(mgr, 8, true);
      mAnimationState.SetState(CAnimationState::kAS_Ready);
      body->AnimationData()->SetAnimation(CAnimPlaybackParms(xa24_, -1, 1.f, true), false);
      activator->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), activator->GetUniqueId(),
                                                 kSM_Activate, kInvalidUniqueId,
                                                 kSS_InvalidState));
      SetTentacleFlags(0);
      SendScriptMsgs(kSS_InternalState4, mgr, kInvalidUniqueId, kSM_None);
      break;
    case kStateMsg_Update:
      if (!body->AnimationData()->IsAnimTimeRemaining(dt, rstl::string_l("Whole Body"))) {
        mAnimationState.SetState(CAnimationState::kAS_Over);
      }
      break;
    case kStateMsg_Deactivate:
      mAnimationState.SetState(CAnimationState::kAS_NotReady);
      body->SetActive(false);
      SetCollisionActorActive(mgr, 9, false);
      break;
    default:
      break;
  }
}

// fn_16_774C (the state "HeartStage", lbl_16_data_94 entry 2).
//   Activate: the heart-stage timer (guest 0xA3C) takes heartExposedTime (guest 0xBA8, 0x7798),
//     the beam delay (guest 0xA64) 3.0f (lbl_16_rodata_3C0) and the locomotion type 9 (0x77B4).
//   Update: the beam delay runs down; once it is below 0.0f and fn_16_CD0 fired a beam (0x77DC)
//     the firing time (guest 0xA68) becomes 3.75f (lbl_16_rodata_408, 0x77F0). The firing time
//     runs down, the beam is updated (fn_16_A64, 0x780C), and a beam that is out past its firing
//     time is stopped (fn_16_9C0, 0x7840) with the delay 0.5f (lbl_16_rodata_3F4, 0x784C). The
//     heart-stage timer runs down last (0x7858).
//   Deactivate: the locomotion type 1 (0x7868), the heart's collision actor inactive (fn_16_3D64
//     with 8, 0x787C) and the beam stopped (fn_16_9C0, 0x7888).
// Area 11 runs it on the natural path: the heart-stage cycles of tools/drivers/emperoring1_heartstage.py (no `fsm`, module note §12).
// @ fn_16_774C
void CEmperorIngStage1::HeartStage(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
    case kStateMsg_Activate:
      xa3c_ = mHeartExposedTime;
      xa64_ = 3.f;
      BodyController()->SetLocomotionType(pas::kLT_Internal9);
      break;
    case kStateMsg_Update:
      xa64_ -= dt;
      if (xa64_ < 0.f && FireBeam(mgr, dt)) {
        xa68_ = 3.75f;
      }
      xa68_ -= dt;
      UpdateBeam(dt, mgr);
      if (xa98_ != kInvalidUniqueId && xa68_ < 0.f) {
        StopBeam(mgr);
        xa64_ = 0.5f;
      }
      xa3c_ -= dt;
      break;
    case kStateMsg_Deactivate:
      BodyController()->SetLocomotionType(pas::kLT_Relaxed);
      SetCollisionActorActive(mgr, 8, false);
      StopBeam(mgr);
      break;
    default:
      break;
  }
}

// fn_16_71BC (the state "EndHeartStage", lbl_16_data_94 entry 3). The 'PLAY' actor (fn_16_3060,
// 0x71E4) and the CONNECT/ACTIVATE actor (fn_16_2FF8, 0x71F8) are looked up first; retail tests
// neither, as in BeginHeartStage.
//   Activate: the animation state becomes ready (0x722C), the body's sphere goes active
//     (fn_16_3D64 with 9, 0x723C), so does the 'PLAY' actor (its vtable slot 5, SetActive, at
//     0x7254), the CONNECT/ACTIVATE actor is sent DEACTIVATE (0x44435456) from this actor (its
//     vtable slot 4, AcceptScriptMsg, at 0x72B4), the 'PLAY' actor plays the third tentacle
//     animation (guest 0xA28) with no blend, weight 1.0f and animating (0x730C) and the script
//     hears IS05 (0x7338).
//   Update: while a generate command can be issued (the try-command test with state 12,
//     kAS_Generate, at 0x734C) the sub-stage (guest 0x7C4) picks the generate type - 0 type 0,
//     1 type 2, 2 type 1 (0x7380, 0x7454, 0x7528; the command id 0xF, the zero target, no anim
//     override); once it cannot, every used tentacle slot's state becomes 2 (fn_16_7AD4, 0x7600).
//     Either way the state is over once the 'PLAY' actor's "Whole Body" animation has no time
//     left (0x7628, 0x7644).
//   Deactivate: the 'PLAY' actor plays the first tentacle animation (guest 0xA20, 0x76A0), the
//     animation state is not ready (0x76A8), the tentacles' 0x80 flags are set (fn_16_2EE0 with
//     1, 0x76B4), the attack timer re-rolls (fn_16_8E4, 0x76C0) and the tentacles take the normal
//     vulnerability (fn_16_353C with 1, 0x76D0).
// Area 11 runs it on the natural path: the heart-stage cycles of tools/drivers/emperoring1_heartstage.py (no `fsm`, module note §12).
// @ fn_16_71BC
void CEmperorIngStage1::EndHeartStage(CStateManager& mgr, EStateMsg msg, float dt) {
  CActor* body = GetPlayActor(mgr);
  CActor* activator = GetActivateActor(mgr);
  switch (msg) {
    case kStateMsg_Activate:
      mAnimationState.SetState(CAnimationState::kAS_Ready);
      SetCollisionActorActive(mgr, 9, true);
      body->SetActive(true);
      activator->AcceptScriptMsg(mgr, CScriptMsg(GetUniqueId(), activator->GetUniqueId(),
                                                 kSM_Deactivate, kInvalidUniqueId,
                                                 kSS_InvalidState));
      body->AnimationData()->SetAnimation(CAnimPlaybackParms(xa28_, -1, 1.f, true), false);
      SendScriptMsgs(kSS_InternalState5, mgr, kInvalidUniqueId, kSM_None);
      break;
    case kStateMsg_Update:
      if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Generate)) {
        switch (x7c4_) {
          case 0:
            BodyController()->CommandMgr().DeliverCmd(
                CBCGenerateCmd(pas::kGType_Zero, CVector3f::Zero()));
            break;
          case 1:
            BodyController()->CommandMgr().DeliverCmd(
                CBCGenerateCmd(pas::kGType_Two, CVector3f::Zero()));
            break;
          case 2:
            BodyController()->CommandMgr().DeliverCmd(
                CBCGenerateCmd(pas::kGType_One, CVector3f::Zero()));
            break;
          default:
            break;
        }
      } else {
        RetractUsedTentacles();
      }
      if (!body->AnimationData()->IsAnimTimeRemaining(dt, rstl::string_l("Whole Body"))) {
        mAnimationState.SetState(CAnimationState::kAS_Over);
      }
      break;
    case kStateMsg_Deactivate:
      body->AnimationData()->SetAnimation(CAnimPlaybackParms(xa20_, -1, 1.f, true), false);
      mAnimationState.SetState(CAnimationState::kAS_NotReady);
      SetTentacleFlags(1);
      ResetAttackTimer(mgr);
      SetTentacleVulnerability(mgr, 1);
      break;
    default:
      break;
  }
}

// fn_16_7AD4 (0x7AD4..0x7B40): every used tentacle slot (fn_16_2F70's count, 0x7B14) gets the
// state 2 (the int at +0x28, stride 0x48, 0x7B04).
// @ fn_16_7AD4
void CEmperorIngStage1::RetractUsedTentacles() {
  const int count = TentacleCount();
  for (int i = 0; i < count; ++i) {
    mJoints.mJoints[i].mState = 2;
  }
}

// fn_16_716C (the state "Dead", lbl_16_data_94 entry 4): on the activate message - the asm's
// `cmpwi r5, 0` at 0x7174, the message is the second argument - the script is told that the
// actor died (the state tag 0x44454144, 'DEAD', at 0x7198).
// @ fn_16_716C
void CEmperorIngStage1::Dead(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    SendScriptMsgs(kSS_Dead, mgr, kInvalidUniqueId, kSM_None);
  }
}

// fn_16_6F28 (0x6F28..0x716C, 0x244): the state "MeleeAttack" (lbl_16_data_94 entry 5).
//   Activate: the script hears IS00 (fn_16_830, 0x6F6C), the tentacles take the melee
//     vulnerability (mode 2, fn_16_353C, 0x6F7C), the animation state is ready (0x6F84), the
//     melee flag (guest 0xA74 bit 0x80, the gate of fn_16_4C8) is set (0x6F8C) and the turn
//     speed is the melee one (guest 0xBF4, 0x6F98).
//   Update: if the animation state lets the loop attack in (CanIssueCommand, pas 9 =
//     kAS_LoopAttack, 0x6FAC) the body gets CBCLoopAttackCmd(kLAT_Four) (0x7004); otherwise the
//     turn angle (guest 0xA2C) is the melee turn speed in radians (lbl_16_rodata_3DC), positive
//     once the state has run for 0.5f (lbl_16_rodata_3F4, 0x7064) and negative before, and after
//     3.0f (lbl_16_rodata_3C0, 0x70B8) the melee trigger (fn_16_4B20) decides whether the
//     animation state is over (0x70E8).
//   Deactivate: the body gets ExitState (0x7110), the animation state is not ready (0x7130), the
//     attack timer is re-rolled (fn_16_8E4, 0x7138), the melee flag is cleared (0x7144) and the
//     turn speed is the normal one (guest 0xBF0, 0x7150).
// @ fn_16_6F28
void CEmperorIngStage1::MeleeAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
    case kStateMsg_Activate:
      SendStageMessage(mgr, 0);
      SetTentacleVulnerability(mgr, 2);
      mAnimationState.SetState(CAnimationState::kAS_Ready);
      xa74_ |= 0x80;
      xa34_ = mMaxTurnSpeedMelee;
      break;
    case kStateMsg_Update: {
      if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_LoopAttack)) {
        BodyController()->CommandMgr().DeliverCmd(CBCLoopAttackCmd(pas::kLAT_Four));
      } else {
        const float kDegToRad = 0.017453292f; // lbl_16_rodata_3DC
        const float turn = kDegToRad * mMaxTurnSpeedMelee;
        xa2c_ = StateMachineState().GetTime() >= 0.5f ? turn : -turn;
        if (StateMachineState().GetTime() > 3.f) {
          if (!ShouldMeleeAttack(mgr, CTriggerData(0.f))) {
            mAnimationState.SetState(CAnimationState::kAS_Over);
          }
        }
      }
      break;
    }
    case kStateMsg_Deactivate:
      BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_ExitState));
      mAnimationState.SetState(CAnimationState::kAS_NotReady);
      ResetAttackTimer(mgr);
      xa74_ &= ~0x80;
      xa34_ = mMaxTurnSpeedNormal;
      break;
    default:
      break;
  }
}

// The attack lists of fn_16_6D94, one per mode (guest 0x7C0): 0 ProjectileAttack, 1 WarpAttack,
// 2 TauntAttack, 3 JumpRope.
static const int skAttacksMode0[3] = {0, 0, 3};    // lbl_16_rodata_34C
static const int skAttacksMode1[4] = {1, 0, 3, 0}; // lbl_16_rodata_358
static const int skAttacksMode2[4] = {2, 3, 1, 0}; // lbl_16_rodata_368

// fn_16_6D94 (the state "Attack", lbl_16_data_94 entry 6): the mode (guest 0x7C0) picks the attack
// list (0x6DE8, 0x6DFC, 0x6E10; any other mode has none and a count of 0). Every message first
// resets the turn angle (fn_16_2C3C, 0x6E28); activate clears the tentacles' 0x80 flags
// (fn_16_2EE0 with 0, 0x6E3C). The list index (guest 0xA70) wraps to 0 at the count (0x6E50) and
// the message goes to that attack's state (0x6E98, 0x6EB0, 0x6EC8, 0x6EE0). On deactivate the
// attack timer is re-rolled (fn_16_8E4, 0x6EF4), the index moves on (0x6F08) and the flags are set
// (fn_16_2EE0 with 1, 0x6F0C).
// @ fn_16_6D94
void CEmperorIngStage1::Attack(CStateManager& mgr, EStateMsg msg, float dt) {
  const int* attacks = nullptr;
  int count = 0;
  switch (x7c0_) {
    case 0:
      attacks = skAttacksMode0;
      count = 3;
      break;
    case 1:
      attacks = skAttacksMode1;
      count = 4;
      break;
    case 2:
      attacks = skAttacksMode2;
      count = 4;
      break;
  }
  ResetTurnAngle(mgr);
  if (msg == kStateMsg_Activate) {
    SetTentacleFlags(0);
  }
  if (xa70_ >= count) {
    xa70_ = 0;
  }
  switch (attacks[xa70_]) {
    case 0:
      ProjectileAttack(mgr, msg, dt);
      break;
    case 1:
      WarpAttack(mgr, msg, dt);
      break;
    case 2:
      TauntAttack(mgr, msg, dt);
      break;
    case 3:
      JumpRope(mgr, msg, dt);
      break;
  }
  if (msg == kStateMsg_Deactivate) {
    ResetAttackTimer(mgr);
    xa70_ += 1;
    SetTentacleFlags(1);
  }
}

// fn_16_68F4 (the attack "ProjectileAttack", list entry 0 of fn_16_6D94).
//   Activate: the script hears IS00 (fn_16_830, 0x694C), the tentacles take vulnerability mode 3
//     (fn_16_353C, 0x695C), the charged flag (0xA74 bit 0x08) is cleared (0x6968) and guest 0xA6C
//     takes the retracted share (fn_16_2E7C over fn_16_2F70, 0x69CC). The share picks the charge
//     sound (below 0.25f 0x2895, 0.5f 0x2896, 0.75f 0x2897, else 0x2898; lbl_16_rodata_3F0/3F4/
//     3A4), an emitter at this actor (C3DEmitterParmData(1000.0f, 1.0f, 1, 0x7F, 0x14), no
//     direction, AddEmitter with acoustics at 0x6A9C) kept in guest 0xA94; the animation state is
//     ready (0x6AAC).
//   Update: the effect-state timer (0xA78) is 1.0f (0x6ACC). The charge lasts 4.0f * share + 2.0f
//     (lbl_16_rodata_38C/3B0, 0x6AE0); its progress is the state time over that (0x6AF0), and at
//     0.9999999f (lbl_16_rodata_3F8) the charged flag is set (0x6B14). The first
//     "DarkProjectileCharge" effect (lbl_16_rodata_340) takes the progress as parameter 0
//     (fn_16_6D10, 0x6B78). If the animation state lets the loop attack in (ShouldRetry, pas 9,
//     0x6B88) the body gets CBCLoopAttackCmd(kLAT_Zero) (0x6BDC); otherwise the actor turns to the
//     player (fn_16_7B40, 0x6C24) and past the charge time the animation state is over (0x6C48).
//   Deactivate: the body gets ExitState (0x6C70); uncharged, the effect-state timer is 0.0f
//     (0x6C9C); a playing emitter is removed (0x6CD0) and the animation state is not ready (0x6CE0).
// @ fn_16_68F4
void CEmperorIngStage1::ProjectileAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
    case kStateMsg_Activate: {
      SendStageMessage(mgr, 0);
      SetTentacleVulnerability(mgr, 3);
      xa74_ &= ~0x08;
      const float count = static_cast< float >(TentacleCount());
      xa6c_ = static_cast< float >(RetractedTentacleCount()) / count;
      ushort sfx;
      if (xa6c_ < 0.25f) {
        sfx = 0x2895;
      } else if (xa6c_ < 0.5f) {
        sfx = 0x2896;
      } else if (xa6c_ < 0.75f) {
        sfx = 0x2897;
      } else {
        sfx = 0x2898;
      }
      CAudioSys::C3DEmitterParmData parms(1000.f, 1.f, 1, 0x7F, 0x14);
      parms.mPos = GetTranslation();
      parms.mDir = CVector3f::Zero();
      parms.mSfxId = sfx;
      xa94_ = CSfxManager::AddEmitter(parms, GetCurrentAreaId().Value(), true, false,
                                      CSfxManager::kMedPriority);
      mAnimationState.SetState(CAnimationState::kAS_Ready);
      break;
    }
    case kStateMsg_Update: {
      xa78_ = 1.f;
      const float chargeTime = 4.f * xa6c_ + 2.f;
      const float progress = StateMachineState().GetTime() / chargeTime;
      if (progress >= 0.9999999f) {
        xa74_ |= 0x08;
      }
      CParticleGenInfo* effect =
          AnimationData()->GetFirstParticleEffect(rstl::string_l("DarkProjectileCharge"));
      if (effect != nullptr) {
SetSystemParam(GetEffectSystem(effect), 0, progress);  // fn_16_219C: the effect's parameter
      }
      if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_LoopAttack)) {
        BodyController()->CommandMgr().DeliverCmd(CBCLoopAttackCmd(pas::kLAT_Zero));
      } else {
        LookAtPlayer(mgr);
        if (StateMachineState().GetTime() > chargeTime) {
          mAnimationState.SetState(CAnimationState::kAS_Over);
        }
      }
      break;
    }
    case kStateMsg_Deactivate:
      BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_ExitState));
      if ((xa74_ & 0x08) == 0) {
        xa78_ = 0.f;
      }
      if (xa94_ && CSfxManager::IsPlaying(xa94_)) {
        CSfxManager::RemoveEmitter(xa94_);
        xa94_ = CSfxHandle();
      }
      mAnimationState.SetState(CAnimationState::kAS_NotReady);
      break;
    default:
      break;
  }
}

// fn_16_2E7C (0x2E7C..0x2EE0): over the used slots (fn_16_2F70, 0x2E98) the tentacles in state 0
// are counted (0x2EAC).
// @ fn_16_2E7C
int CEmperorIngStage1::RetractedTentacleCount() const {
  int retracted = 0;
  const int count = TentacleCount();
  for (int i = 0; i < count; ++i) {
    if (mJoints.mJoints[i].mState == 0) {
      ++retracted;
    }
  }
  return retracted;
}

// fn_16_601C's own statics: the portal counter (lbl_16_bss_4, an int) and its initialisation
// guard (lbl_16_bss_8, the byte the retail sets on the first call, 0x60A0..0x60B0).
static int sWarpPortalCount = -1;                 // lbl_16_bss_4
static bool sWarpPortalCountInitialized = false;  // lbl_16_bss_8

// The module's two body-command records: lbl_16_data_3B0 (the loop attack's) and
// lbl_16_data_3BC (the exit state's). The retail tells a command apart by the address of its
// three-word record {0, 0, the record's own destroy helper (fn_16_5BA8 / fn_16_5C04)}; the port
// keeps the command id and the loop attack's type in the tables.
struct SEmperorIngStage1CmdRecord {
  uint mTypeTag;  // 0 in both records
  uint mId;       // the body command id
  uint mParam;    // the loop attack's type
};
static const SEmperorIngStage1CmdRecord skLoopAttackCmd = {0, kBSC_LoopAttack, 1};  // lbl_16_data_3B0  // fn_16_5BA8: the loop attack's command record
static const SEmperorIngStage1CmdRecord skExitStateCmd = {0, kBSC_ExitState, 0};    // lbl_16_data_3BC  // fn_16_5C04: the exit state's command record

// fn_16_601C (0x601C..0x67EC, 0x7D0): the state "WarpAttack" (lbl_16_data_94 entry 8).
//   Activate (0x60D8): the script hears IS02 (fn_16_830 with 2, 0x60E4), the animation state is
//     ready (0x60F0), the tentacles take the warp vulnerability (mode 4, fn_16_353C, 0x60FC), the
//     portal timer (guest 0xA44) is 0.0f (0x6100), the flag byte's bit 3 (0x08, the charged
//     projectile) is cleared - the rlwimi at 0x6120 takes the bit 27, the byte's bit 4 (0x10) -
//     and its bit 5 (0x20) is set (0x612C, the bit 26), and the portal counter is -1 (0x6134).
//   Update (0x613C): past 4.0f (lbl_16_rodata_38C, 0x6158) the tentacles are immune
//     (fn_16_353C with 0, 0x616C) and lose their materials (fn_16_2E1C with 0, 0x617C), and the
//     portal counter takes the number of tentacles that are out (0x6194..0x6270). If the
//     animation state lets the loop attack in (ShouldRetry, pas 9 = kAS_LoopAttack, 0x61BC) the
//     body gets the loop attack command (the id 7 with the type 1, 0x61C8..0x6250) and the state
//     is done. Otherwise the flag byte's bit 4 (0x10, the "GenerateEnd" event) has to be set
//     (0x6258, the extrwi takes the bit 27), the portal timer counts down (0x6270) and while it
//     is below 0.0f and the counter is above 0 a portal is made (0x62B0..0x66B4). When the
//     counter is spent and the state has run for more than 5.0f (lbl_16_rodata_3AC, 0x6704) the
//     animation state is over (0x6718).
//   Deactivate (0x6720): the body gets the exit state command (the id 10, 0x6740), the flag
//     byte's bit 5 (0x20) is cleared (0x6760, the bit 26), the tentacles get their materials back
//     (fn_16_2E1C with 1, 0x6774) and the animation state is not ready (0x677C).
// @ fn_16_601C
void CEmperorIngStage1::WarpAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  if (sWarpPortalCountInitialized == false) {  // 0x6084..0x60B0: the static's guard.
    sWarpPortalCountInitialized = true;
    sWarpPortalCount = -1;
  }
  switch (msg) {
    case kStateMsg_Activate:
      SendStageMessage(mgr, 2);                             // 0x60E4
      mAnimationState.SetState(CAnimationState::kAS_Ready); // 0x60F0
      SetTentacleVulnerability(mgr, 4);                     // 0x60FC
      xa44_ = 0.f;                                          // 0x6100
      xa74_ &= ~0x10;                                       // 0x6120, the bit 27
      xa74_ |= 0x20;                                        // 0x612C, the bit 26
      sWarpPortalCount = -1;                                // 0x6134
      break;
    case kStateMsg_Update: {
      if (StateMachineState().GetTime() > 4.f) {  // 0x6158, lbl_16_rodata_38C
        SetTentacleVulnerability(mgr, 0);      // 0x616C
        SetTentacleMaterials(mgr, 0);          // 0x617C
        if (sWarpPortalCount == -1) {          // 0x618C
          // 0x6194: the tentacle count (fn_16_2F70) minus the retracted one (fn_16_2E7C).
          sWarpPortalCount = TentacleCount() - RetractedTentacleCount();
        }
      }
      if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_LoopAttack)) {  // 0x61BC
        // 0x61C8: the body gets the loop attack command (the id 7 with the type 1).
        BodyController()->CommandMgr().DeliverCmd(
            CBCLoopAttackCmd(static_cast< pas::ELoopAttackType >(skLoopAttackCmd.mParam)));
        break;
      }
      if ((xa74_ & 0x10) == 0) {  // 0x6258: the bit 27 of the byte, the "GenerateEnd" event.
        break;
      }
      xa44_ -= dt;  // 0x6270
      while (xa44_ < 0.f && sWarpPortalCount > 0) {  // 0x66B8
        // 0x62B0: one warp portal. The counter is spent (0x62C0) and the portal timer is 0.75f
        // (lbl_16_rodata_3A4, 0x62C8).
        --sWarpPortalCount;  // 0x62C0
        xa44_ = 0.75f;       // 0x62C8
        // 0x62CC: the attack tip's CAnimRes - the ANCS id (guest 0xC04), the character index
        // (guest 0xC08), the one vector as the scale and the default animation (guest 0xC0C).
        const CAnimRes animRes(mWarpAttackTip.ancs, mWarpAttackTip.character_index,
                                CVector3f::One(), mWarpAttackTip.initial_anim, false);
        const CModelData modelData(animRes);  // 0x6300
        CVector3f pos;
        CVector3f lookAt;
        GetWarpPortalPlacement(mgr, pos, lookAt);  // 0x6364 / 0x6458
        // 0x6568: the portal's transform looks from the position to the look at point with the
        // up vector.
        const CTransform4f xf = CTransform4f::LookAt(pos, lookAt, CVector3f::Up());
        CreateWarpPortalActor(mgr, modelData, xf);  // 0x6660
      }
      if (xa44_ < 0.f && sWarpPortalCount == 0 && StateMachineState().GetTime() > 5.f) {  // 0x66D0
        mAnimationState.SetState(CAnimationState::kAS_Over);  // 0x6718
      }
      break;
    }
    case kStateMsg_Deactivate:
      // 0x6740: the body gets the exit state command (the id 10).
      BodyController()->CommandMgr().DeliverCmd(
          CBodyStateCmd(static_cast< EBodyStateCmd >(skExitStateCmd.mId)));
      xa74_ &= ~0x20;                        // 0x6760, the bit 26
      SetTentacleMaterials(mgr, 1);           // 0x6774
      mAnimationState.SetState(CAnimationState::kAS_NotReady);  // 0x677C
      break;
    default:
      break;
  }
}

// 0x6364 (the random path) and 0x6458 (the ray path): where the warp portal appears.
//   The random path (the player's morph ball state - the retail's CPlayer+0x38C - is zero): the
//   portal is at the player's eye position plus an offset in the player's own space. The offset
//   is the player's transform applied to the local vector (range2, 10.0f, range1) with range1 =
//   Random Range(2.0f, 5.0f) (lbl_16_rodata_3B0/3AC, 0x637C) and range2 = Random
//   Range(-10.0f, 10.0f) (lbl_16_rodata_3EC/390, 0x63AC) - the retail adds the three columns
//   scaled by their own component (0x6394..0x642C). The portal looks at the player's eye
//   position (0x6438..0x6450).
//   The ray path (the player is morphed): the portal is at the ground under the player - the
//   ray starts at the player's position plus the up vector (0x64A4..0x64FC), goes straight down
//   (sDownVector) for 50.0f (lbl_16_rodata_3BC) and the hit point is the portal's position
//   (0x651C..0x6530); without a hit the player's own position is used. The portal looks at
//   the position plus the up vector (0x6534..0x6564).
void CEmperorIngStage1::GetWarpPortalPlacement(CStateManager& mgr, CVector3f& pos,
                                                CVector3f& lookAt) const {
  CPlayer* player = mgr.GetPlayer(0);  // the state manager's first player (0x14FC, 0x6304)
  const CVector3f eye = player->GetEyePosition();  // 0x63F0
  if (player->GetMorphballTransitionState() == CPlayer::kMS_Unmorphed) {  // 0x6358
    // 0x6364: the random path.
    const float range1 = mgr.Random()->Range(2.f, 5.f);     // 0x637C
    const float range2 = mgr.Random()->Range(-10.f, 10.f);  // 0x63AC
    const CVector3f local(range2, 10.f, range1);
    // 0x6308: the player's transform (the guest's +0x24).
    const CTransform4f& xf = player->GetTransform();
    pos = eye + xf.GetColumn(kDX) * local.GetX() + xf.GetColumn(kDY) * local.GetY() +
          xf.GetColumn(kDZ) * local.GetZ();  // 0x642C
    lookAt = eye;                                      // 0x6448
    return;
  }
  // 0x6458: the ray path. The retail asks the player for its aim position - the CPlayer's
  // vtable slot 21 (the word at 0x54 of __vt__7CPlayer) is GetAimPosition (the DOL's
  // GetAimPosition__7CPlayerCFRC13CStateManagerf) - with the state manager and 0.75f
  // (lbl_16_rodata_3A4, 0x6474).
  const CVector3f origin = player->GetAimPosition(mgr, 0.75f);
  const CRayCastResult hit = mgr.RayStaticIntersection(
      origin + CVector3f::Up(), CVector3f::Down(), 50.f, CMaterialFilter());  // 0x6500
  // fn_16_6898 (0x6898..0x6914): the result is copied field by field into the stack's own slot
  // (the eight floats, the two ints and the byte); the host's assignment does it.
  pos = origin;                       // 0x6488..0x64A0
  if (hit.IsValid()) {                // 0x6514
    pos = hit.GetPoint();             // 0x6528
  }
  // 0x6534..0x6564: the ray path joins the shared tail, which builds the look point as the
  // portal position plus the up vector (r30 = sUpVector; the portal faces straight up). The
  // 0x6404 zero-looking read belongs to the random path (the eye position's x), not this one.
  lookAt = pos + CVector3f::Up();
}

// The module's own CPhysicsActor subclass - the "Attack Tip" portal actor (the retail's vtable
// lbl_16_data_27C, the portal's effect at the guest 0xC00, the warp stab damage and the two warp
// sounds). It is created with the id the state manager hands out (0x661C), the name "Attack
// Tip" (the string at lbl_16_rodata_41C's 0x665), the entity info (this actor's area, no
// connections, not active and no editor id), the transform, the model data, the actor
// parameters, the portal's effect asset, the warp stab damage and the two warp sounds; the
// state manager takes it (0x6670).
void CEmperorIngStage1::CreateWarpPortalActor(CStateManager& mgr, const CModelData& modelData,
                                               const CTransform4f& xf) {
  CEmperorIngStage1WarpPortal* portal = rs_new CEmperorIngStage1WarpPortal(
      mgr.AllocateUniqueId(), rstl::string_l("Attack Tip"),  // 0x660C
      CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, false), xf,  // 0x65F4
      modelData, CActorParameters(), mWarpPortalEffect, mWarpStabDamage, mWarpSound, mWarpSound2);
  if (portal != nullptr) {  // 0x6668: the retail's own test
    mgr.AddObject(portal);  // 0x6670
  }
}

// fn_16_5DBC (0x5DBC..0x601C, 0x260): the state "TauntAttack" - the taunt's own timer (the
// guest 0xA54) counts up and the taunt ends when it is over the taunt's loop duration (the guest
// 0xC38, 0x5F78).
//   Activate: the script hears IS00 (fn_16_830, 0x5E60), the animation state is ready (0x5E6C),
//     the tentacles get the mode 1
//     vulnerability (fn_16_353C, 0x5E7C), the timer is 0.0f (0x5EA8) and the module's static
//     (lbl_16_bss_14, 0x5EB4) takes the retracted tentacle count (fn_16_2E7C, 0x5EAC).
//   Update: the actor looks at the player (fn_16_7B40, 0x5EC4), the timer counts up (0x5ED4) and
//     the animation state is asked for the loop attack (ShouldRetry, pas 9 = kAS_LoopAttack,
//     0x5EE0); if it may, the body gets the loop attack command (the id 7 with the type 2,
//     0x5EEC), otherwise the timer over the loop duration gives the exit state command (the id
//     10, 0x5FA8).
//   Deactivate: the body gets the abort scripted command (the id 13, 0x5FDC) and the animation
//     state is not ready (0x5FF8).
// @ fn_16_5DBC
void CEmperorIngStage1::TauntAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
    case kStateMsg_Activate:
      SendStageMessage(mgr, 0); // 0x5E60
      mAnimationState.SetState(CAnimationState::kAS_Ready); // 0x5E6C
      SetTentacleVulnerability(mgr, 1); // 0x5E7C
      xa54_ = 0.f; // 0x5EA8
      sTauntRetracted = RetractedTentacleCount(); // 0x5EAC, 0x5EB4
      break;
    case kStateMsg_Update:
      LookAtPlayer(mgr); // 0x5EC4
      xa54_ += dt; // 0x5ED4
      if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_LoopAttack)) { // 0x5EE0
        BodyController()->CommandMgr().DeliverCmd(CBCLoopAttackCmd(pas::kLAT_Two)); // 0x5F38
      } else if (xa54_ > mTauntLoopDuration) { // 0x5F78, 0x5F84
        BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_ExitState)); // 0x5FA8
      }
      break;
    case kStateMsg_Deactivate:
      BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_AbortScripted)); // 0x5FDC
      mAnimationState.SetState(CAnimationState::kAS_NotReady); // 0x5FF8
      break;
    default:
      break;
  }
}

// fn_16_5C4C (the state "Taunt").
//   Activate: the script hears IS00 (fn_16_830, 0x5C94) and the animation state is ready (0x5C9C).
//   Update: if the animation state lets the taunt in (ShouldRetry, pas 0x10, 0x5CB0) the body gets
//     CBCTauntCmd(kTT_Zero) (0x5CF4); either way the actor turns to the player (fn_16_7B40, 0x5D28).
//   Deactivate: the animation state is not ready and the taunt timer (0xA50) is 0.0f (0x5D30).
// @ fn_16_5C4C
void CEmperorIngStage1::Taunt(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
    case kStateMsg_Activate:
      SendStageMessage(mgr, 0);
      mAnimationState.SetState(CAnimationState::kAS_Ready);
      break;
    case kStateMsg_Update:
      if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Taunt)) {
        BodyController()->CommandMgr().DeliverCmd(CBCTauntCmd(pas::kTT_Zero));
      }
      LookAtPlayer(mgr);
      break;
    case kStateMsg_Deactivate:
      mAnimationState.SetState(CAnimationState::kAS_NotReady);
      xa50_ = 0.f;
      break;
    default:
      break;
  }
}

// fn_16_5928 (the attack "JumpRope").
//   Activate: the script hears IS00 (fn_16_830, 0x596C), the tentacles take vulnerability mode 2
//     (fn_16_353C, 0x597C) and lose their target materials (fn_16_2E1C with 0, 0x598C); the
//     animation state is ready (0x5994), guest 0xA74 bit 0x40 is set (0x599C) and the turn speed
//     (0xA34) is guest 0xBF8's (0x59A8). The rope lasts by mode (0x7C0): 0 3.5f, 1 5.0f, else 6.5f
//     (lbl_16_rodata_3E4/3AC/3E8, 0x59CC..0x59F4).
//   Update: if the animation state lets the loop attack in (ShouldRetry, pas 9, 0x5A08) the body
//     gets CBCLoopAttackCmd(kLAT_Three) (0x5A60); otherwise the turn angle (0xA2C) is guest 0xBF8
//     in radians (lbl_16_rodata_3DC), negative for the first second (0x5ABC), and past the
//     duration the animation state is over (0x5B18).
//   Deactivate: the body gets ExitState (0x5B40), the animation state is not ready, bit 0x40 is
//     cleared (0x5B70), the tentacles get their materials back (fn_16_2E1C with 1, 0x5B78), the
//     turn speed is maxTurnSpeedNormal again (0x5B88) and the attack timer restarts (fn_16_8E4).
// @ fn_16_5928
void CEmperorIngStage1::JumpRope(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
    case kStateMsg_Activate:
      SendStageMessage(mgr, 0);
      SetTentacleVulnerability(mgr, 2);
      SetTentacleMaterials(mgr, 0);
      mAnimationState.SetState(CAnimationState::kAS_Ready);
      xa74_ |= 0x40;
      xa34_ = mJumpRopeTurnSpeed;
      switch (x7c0_) {
        case 0:
          xa38_ = 3.5f;
          break;
        case 1:
          xa38_ = 5.f;
          break;
        default:
          xa38_ = 6.5f;
          break;
      }
      break;
    case kStateMsg_Update:
      if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_LoopAttack)) {
        BodyController()->CommandMgr().DeliverCmd(CBCLoopAttackCmd(pas::kLAT_Three));
      } else {
        const float angle = 0.017453292f * mJumpRopeTurnSpeed;
        xa2c_ = StateMachineState().GetTime() < 1.f ? -angle : angle;
        if (StateMachineState().GetTime() > xa38_) {
          mAnimationState.SetState(CAnimationState::kAS_Over);
        }
      }
      break;
    case kStateMsg_Deactivate:
      BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_ExitState));
      mAnimationState.SetState(CAnimationState::kAS_NotReady);
      xa74_ &= ~0x40;
      SetTentacleMaterials(mgr, 1);
      xa34_ = mMaxTurnSpeedNormal;
      ResetAttackTimer(mgr);
      break;
    default:
      break;
  }
}

// fn_16_2E1C (0x2E1C..0x2E7C): fn_16_1F34 with the flag for each live tentacle (0x2E50).
// @ fn_16_2E1C
void CEmperorIngStage1::SetTentacleMaterials(CStateManager& mgr, uint flag) {
  for (int i = 0; i < mJoints.mCount; ++i) {
    UpdateTentacleMaterials(mJoints.mJoints[i], mgr, flag);
  }
}

// fn_16_4AE8 (the code "BeginSubStage", lbl_16_data_220 entry 0): retail's body is a single
// `blr` - the sub-stage itself is the state machine's business.
// @ fn_16_4AE8
void CEmperorIngStage1::BeginSubStage(CStateManager& mgr, float dt) {}

// fn_16_1DFC: the tentacle's timer is cleared and its state becomes 1, unless it is already 2.
// @ fn_16_1DFC
static void ResetTentacle(CEmperorIngStage1Joint& tentacle) {
  tentacle.mTimer = 0.f;
  if (tentacle.mState != 2) {
    tentacle.mState = 1;
  }
}

// fn_16_4A04 (the code "ResetTentacles", lbl_16_data_220 entry 1): every tentacle slot of the
// current sub-stage is reset (fn_16_1DFC clears its timer and sets its state to 1 unless it is
// already 2) and gets the retraction timer, then the live tentacles' flags are set (fn_16_2EE0)
// and the script is told which sub-stage this is.
// @ fn_16_4A04
void CEmperorIngStage1::ResetTentacles(CStateManager& mgr, float dt) {
  const int count = TentacleCount();
  for (int i = 0; i < count; ++i) {
    ResetTentacle(mJoints.mJoints[i]);
    mJoints.mJoints[i].mTime = mStayRetractedTime;
  }
  SetTentacleFlags(1);
  // The switch at 0x4A60..0x4A74: 2 sends IS07, 1 sends IS06, every other value sends nothing.
  if (x7c0_ == 2) {
    SendScriptMsgs(kSS_InternalState7, mgr, kInvalidUniqueId, kSM_None);
  } else if (x7c0_ == 1) {
    SendScriptMsgs(kSS_InternalState6, mgr, kInvalidUniqueId, kSM_None);
  }
}

// fn_16_4954 (the code "ChooseRandomTentacleCount", lbl_16_data_220 entry 2): the sub-stage
// (guest 0x7C4) is picked from a roll of 0..100 - the state manager's random (retail
// CStateManager+0x16E4) asked for Range(0.0f, 100.0f) at 0x497C, with 0.0f from
// lbl_16_rodata_380 and 100.0f from lbl_16_rodata_3D4 - against 50.0f (lbl_16_rodata_3BC): in
// mode 0 the roll picks 0 or 1, in mode 1 it picks 1 or 2, and every other mode takes 2.
// The FSM2 machine's code node "ChooseRandomTentacleCount" hangs off the "HeartStageOver"
// trigger of the "HeartStage" state (the machine has 10 states, 4 codes and 12 triggers), so
// the body runs once per heart stage.  The natural fight reaches it: with the tentacles' own
// boxes hit and the 'PLAY' actor active, the FSM walks BeginHeartStage -> HeartStage ->
// EndHeartStage on its own (tools/drivers/emperoring1_heartstage.py `cycles`, no `fsm`), and the
// heart's health steps the mode (fn_16_51D4).  Every live arm ran there (gcov, module note §12):
// both sides of 50 in mode 0 and mode 1, and mode 2.  The mode is never below 0 (the only
// stores to guest 0x7C0 are 0x909C li 0, 0x5284 li 1 and 0x529C li 2), so the arm at 0x4998
// is dead.
// @ fn_16_4954
void CEmperorIngStage1::ChooseRandomTentacleCount(CStateManager& mgr, float dt) {
  const float roll = mgr.Random()->Range(0.f, 100.f);
  if (x7c0_ == 1) {
    x7c4_ = !(roll < 50.f) ? 2 : 1; // 0x49CC fcmpo; bge
  } else if (x7c0_ >= 1) {
    // The asm's `bge` at 0x498C: the mode is 2 or more here, because 1 was taken above.
    x7c4_ = 2;
  } else if (x7c0_ >= 0) {
    x7c4_ = !(roll < 50.f) ? 1 : 0; // 0x49A8 fcmpo; bge
  } else {
    x7c4_ = 2;
  }
}

// fn_16_2F70: how many tentacle slots the current sub-stage uses - four, six or eight. The
// sub-stage is the int at guest 0x7C4.
// @ fn_16_2F70
int CEmperorIngStage1::TentacleCount() const {
  if (x7c4_ == 1) {
    return 6;
  }
  if (x7c4_ >= 1) {
    return 8;
  }
  if (x7c4_ >= 0) {
    return 4;
  }
  return 8;
}

// fn_16_2EE0: every live tentacle's flag byte (guest +0x24) gets its 0x80 bit set when the
// argument is non-zero and the tentacle is one of the current sub-stage's slots; the others get
// it cleared.
// @ fn_16_2EE0
void CEmperorIngStage1::SetTentacleFlags(uint flag) {
  const bool set = (flag & 0xff) != 0;
  const int count = TentacleCount();
  for (int i = 0; i < mJoints.mCount; ++i) {
    if (set && i < count) {
      mJoints.mJoints[i].mFlags |= 0x80;
    } else {
      mJoints.mJoints[i].mFlags &= ~0x80;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// The module's loader (fn_16_A2A0) and entry points.

// Retail fn_16_AF7C: SLdrEmperorIngStage1Data's constructor, called by the loader at 0x0000A2DC
// on its stack object. It calls each sub-struct's constructor - fn_16_B8F4 for the tentacle block
// (+0x000), fn_16_B6BC for melee (+0x580), fn_16_B584 for the projectile block (+0x590),
// fn_16_B3DC for the warp block (+0x5a8), fn_16_B188 for the taunt block (+0x5d8) and
// fn_16_AAD4 for the beam attack (+0x614) - and between them they write -1 to five sound ids,
// where the generated header stores 0 (the guest's "no sound" is -1, the header's 0). Their
// other -1 stores land on CAssetIds (warp portalEffect 0x0, projectile 0x0/0x14) and on the two
// warp ints 0x20/0x24, which the header already has right, as it has fn_16_AF7C's own 0x6a4
// vulnerabilityChangeSound. The sub-struct layout is the guest's here (no editor-properties
// padding inside SLdrEmperorIngStage1Data). A property in the instance's data overwrites any of
// these, so this runs before the .inc reader below.
//   fn_16_B8F4  li r0,-0x1 0x0000B930; stw r0,0x57c(r31) 0x0000B93C
//   fn_16_B3DC  li r0,-0x1 0x0000B408; stw r0,0x28(r31) 0x0000B418, 0x2c(r31) 0x0000B41C
//   fn_16_B188  li r0,-0x1 0x0000B1A8; stw r0,0x38(r31) 0x0000B1AC
//   fn_16_AAD4  li r0,-0x1 0x0000AAF4; stw r0,0x58(r31) 0x0000AAFC
static void xAF7C_SoundDefaults(SLdrEmperorIngStage1& sldr) {
  sldr.data.tentacle.tentacleDamagedSound = -1;    // 0x0000B93C (block 0x0, field 0x57c)
  sldr.data.warp.sound = -1;                       // 0x0000B418 (block 0x5a8, field 0x28)
  sldr.data.warp.sound_0xa93f0198 = -1;            // 0x0000B41C (block 0x5a8, field 0x2c)
  sldr.data.tauntAttack.shockWaveTravelSound = -1; // 0x0000B1AC (block 0x5d8, field 0x38)
  sldr.data.beamAttack.sound = -1;                 // 0x0000AAFC (block 0x614, field 0x58)
}

// @ fn_16_A2A0
static CEntity* PortLoadEmperorIngStage1(CStateManager& mgr, CInputStream& input,
                                         CEntityInfo& info) {
  SLdrEmperorIngStage1 sldrThis;
  xAF7C_SoundDefaults(sldrThis); // fn_16_AF7C's -1 sound ids (the call at 0x0000A2DC)
#include "MetroidPrime/ScriptLoader/SLdrEmperorIngStage1.inc"
  // The retail's reader (fn_16_ABD0) and its helpers - fn_16_AEDC, fn_16_AF7C, fn_16_B014,
  // fn_16_B130, fn_16_B188, fn_16_B1D8, fn_16_B378, fn_16_B3DC, fn_16_B434, fn_16_B52C,
  // fn_16_B584, fn_16_B5C8, fn_16_B668, fn_16_B6BC, fn_16_B6EC and fn_16_B86C - read the
  // block's properties: the SLdr structs' constructors and destructors and the per-type
  // readers (the animation set, the damage info and the health info with its four
  // vulnerabilities). The host's generated SLdrEmperorIngStage1.inc does it.

  // The REL builds the model with LdrToModelData(scale, kInvalidAssetId, the patterned info's
  // animation set, true) at 0x0000A41C and moves the result into the object at 0x0000A434.
  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  // fn_16_A5BC (0xA5BC..0xA5FC) copies the model's optional when it is valid and calls
  // fn_16_A5FC -> fn_16_A61C -> fn_16_A644 (0xA644..0xA780), the CModelData copy constructor:
  // the fields and the three tokens, each locked. The host's CModelData copy does it.
  if (!modelData) {
    // The REL reads the copy's valid byte (sp+0xF8 at 0x0000A440) and branches to its null
    // return at 0x0000A448 when the model is empty.
    return nullptr;
  }

  // The REL's own allocation is rs_new(0xD18, "CEmperorIngStage1") at 0x0000A4B8; the port
  // allocates sizeof(CEmperorIngStage1), which is larger because the host layout is its own.
  return rs_new CEmperorIngStage1(mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
                                  LdrToEntityInfo(info, sldrThis.editorProperties),
                                  LdrToTransform4f(sldrThis.editorProperties), *modelData,
                                  LdrToActorParameters(sldrThis.actorInformation),
                                  LdrToPatternedInfo(sldrThis.patterned, nullptr), sldrThis.data);
}

static SEmperorIngStage1_FuncPtrs REL_loader_EmperorIngStage1;

// Retail's RELMain calls fn_16_A270, which stores the loader fn_16_A2A0 into lbl_16_bss_0 (the
// module's SEmperorIngStage1_FuncPtrs) and calls the DOL's SetSEmperorIngStage1_FuncPtrs. The
// proxy in SEmperorIngStage1_FuncPtrs.cpp calls through it.
// @ fn_16_A250
// @ fn_16_A270
void RELMain() {
  REL_loader_EmperorIngStage1.mLoader = PortLoadEmperorIngStage1;
  SetSEmperorIngStage1_FuncPtrs(&REL_loader_EmperorIngStage1);
}

// @ fn_16_A22C
void RELExit() { SetSEmperorIngStage1_FuncPtrs(nullptr); }
