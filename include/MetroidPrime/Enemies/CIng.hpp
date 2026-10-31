#ifndef _CING
#define _CING

#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "REL/REL_Setup.h"

#ifdef TARGET_PC
#include "Kyoto/Graphics/CColor.hpp"
#include "Kyoto/SObjectTag.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CSurfaceAlignmentHelper.hpp"
#include "MetroidPrime/Enemies/CIngSpotData.hpp"
#include "MetroidPrime/Enemies/CIngSpotPathFindNavigation.hpp"
#include "MetroidPrime/PathFinding/CPathFindPointSearch.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"
#include "MetroidPrime/ScriptLoader/Structs/SLdrPlasmaBeamInfo.hpp"
#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "Kyoto/TToken.hpp"
#include "MetroidPrime/CLineOfSightTracker.hpp"
#include "rstl/optional_object.hpp"

class CCollisionActorManager;
class CGenDescription;
class CScriptAIHint;
class CPFArea;

// Host copies of the sub-structs CIngData embeds (retail: built by the Ing REL's fn_29_F12C,
// fn_29_F090, fn_29_F000 and fn_29_EFC0, copied into CIngData by its constructor fn_29_EE68 - the
// analysis is docs/module-analysis/IngFamily.md section A). The guest offsets in the comments are
// the REL's; the host layout is its own and does not need to match. Every name here is the
// analysis's, i.e. guessed from the properties the loader reads; the state machine in PortCIng.cpp
// reads the attack ranges, odds and sounds.
struct CIngBodyProjectileData {
  // fn_29_F12C (0x74, branch-free): the CDamageInfo copy at +0..+0x1a (0xF12C-0xF170), f1 -> +0x1c
  // (0xF174), r5/r6/r7 -> +0x20/+0x24/+0x26 (0xF178-0xF180), f3..f6 -> +0x28..+0x34, f2 -> +0x38,
  // f7 -> +0x3c (0xF184-0xF198).
  // @ fn_29_F12C
  CIngBodyProjectileData(const CDamageInfo& contactDamage, float unknownA0D6, float suckTime,
                         float speed, float dropTime, float minAttackDist, float maxAttackDist,
                         float odds, CAssetId splatEffect, ushort soundProjectile,
                         ushort soundSplatWall)
  : mContactDamage(contactDamage)
  , mUnknownA0D6(unknownA0D6)
  , mSplatEffect(splatEffect)
  , mSoundProjectile(soundProjectile)
  , mSoundSplatWall(soundSplatWall)
  , mSpeed(speed)
  , mDropTime(dropTime)
  , mMinAttackDist(minAttackDist)
  , mMaxAttackDist(maxAttackDist)
  , mSuckTime(suckTime)
  , mOdds(odds) {}

  // Every slot below is proven twice: the loader's property parse switch (fn_29_130, each property's
  // id word -> the stack slot it reads) and the builder fn_29_F12C, which stores its float arguments
  // in the order f3->+0x28, f4->+0x2c, f5->+0x30, f6->+0x34, f2->+0x38, f7->+0x3c.
  CDamageInfo mContactDamage; // +0x00 (0x1c) SLdrIng.bodyProjectileContactDamage (0xab258f6b)
  float mUnknownA0D6;         // +0x1c SLdrIng.unknown_0xa0d63374 (0xa0d63374), builder's f1
  CAssetId mSplatEffect;      // +0x20 SLdrIng.bodyProjectileSplatEffect (0x601cc5b4)
  ushort mSoundProjectile;    // +0x24 SLdrIng.sound_BodyProjectile (0x2025858b)
  ushort mSoundSplatWall;     // +0x26 SLdrIng.sound_BodyProjectileSplatWall (0x19f8fee6)
  float mSpeed;               // +0x28 SLdrIng.bodyProjectileSpeed (0xce980820), builder's f3
  float mDropTime;            // +0x2c SLdrIng.bodyProjectileDropTime (0xb752c177), builder's f4
  float mMinAttackDist;       // +0x30 SLdrIng.bodyProjectileMinAttackDist (0xe6ddb662), builder's f5
  float mMaxAttackDist;       // +0x34 SLdrIng.bodyProjectileMaxAttackDist (0xb57bae86), f6
  float mSuckTime;            // +0x38 SLdrIng.bodyProjectileSuckTime (0xdfea60a2), builder's f2
  float mOdds;                // +0x3c SLdrIng.bodyProjectileOdds (0xdc741fbd), builder's f7
};

// Retail shape from fn_29_F090: +0 f, +4 CDamageInfo (0x1c), +0x20 f, +0x24/+0x26 sounds,
// +0x28..0x30 floats, +0x34 CDamageVulnerability (0x30) = 0x64.
struct CIngGrappleData {
  // fn_29_F090 (0x9C, branch-free): f1 -> +0 (0xF0B0), the CDamageInfo r4 -> +4..+0x1e, f2 -> +0x20
  // (0xF0F8), the two sounds -> +0x24/+0x26 (0xF0FC/0xF100), f3..f5 -> +0x28..+0x30 (0xF104-0xF10C)
  // and the vulnerability r7 -> +0x34 (fn_29_EC88, 0xF110). The retail passes the vulnerability
  // last (r7); the host's argument order is its own, the field each value lands in is retail's.
  // @ fn_29_F090
  CIngGrappleData(float unknown67F6, const CDamageInfo& exitDamage, float spitForce,
                  float maxHoldTime, float postWaitTime, float pursuitRange,
                  const CDamageVulnerability& vulnerability, ushort soundExitGrapple,
                  ushort soundGrapple)
  : mUnknown67F6(unknown67F6)
  , mExitGrappleDamage(exitDamage)
  , mSpitForce(spitForce)
  , mSoundExitGrapple(soundExitGrapple)
  , mSoundGrapple(soundGrapple)
  , mMaxHoldTime(maxHoldTime)
  , mPostWaitTime(postWaitTime)
  , mPursuitRange(pursuitRange)
  , mVulnerability(vulnerability) {}

  float mUnknown67F6;               // +0x00 SLdrIng.unknown_0x67f6c10e
  CDamageInfo mExitGrappleDamage;   // +0x04 (0x1c) SLdrIng.exitGrappleDamage
  float mSpitForce;                 // +0x20 SLdrIng.exitGrappleSpitForce
  ushort mSoundExitGrapple;         // +0x24 SLdrIng.sound_ExitGrapple
  ushort mSoundGrapple;             // +0x26 SLdrIng.sound_Grapple
  float mMaxHoldTime;               // +0x28..0x30, slot [?]: maxGrappleBallHoldTime,
  float mPostWaitTime;              // postGrappleBallWaitTime and grappleBallPursuitRange
  float mPursuitRange;
  CDamageVulnerability mVulnerability; // +0x34 (0x30) SLdrIng.grappleBallVulnerability
};

// Retail shape from fn_29_F000: +0 min, +4 max, +8 effect, +0xc sound, +0x10 CDamageInfo (0x1c),
// +0x2c SLdrPlasmaBeamInfo (0x48) = 0x74.
struct CIngMiniPortalData {
  // fn_29_F000 (0x90, branch-free): f1/f2 -> +0/+4 (0xF020/0xF028), r4 -> +8 (0xF034), the sound
  // r5 -> +0xc (sth, 0xF040), the CDamageInfo r6 -> +0x10..+0x2a and the beam info r7 -> +0x2c
  // (fn_29_E9E8, 0xF074).
  // @ fn_29_F000
  CIngMiniPortalData(float minAttackDist, float maxAttackDist, CAssetId effect, ushort sound,
                     const CDamageInfo& damage, const SLdrPlasmaBeamInfo& beamInfo)
  : mMinAttackDist(minAttackDist)
  , mMaxAttackDist(maxAttackDist)
  , mEffect(effect)
  , mSound(sound)
  , mDamage(damage)
  , mBeamInfo(beamInfo) {}

  float mMinAttackDist;              // +0x00 SLdrIng.miniPortalMinAttackDist
  float mMaxAttackDist;              // +0x04 SLdrIng.miniPortalMaxAttackDist
  CAssetId mEffect;                  // +0x08 SLdrIng.miniPortalEffect
  ushort mSound;                     // +0x0c SLdrIng.sound_MiniPortal
  CDamageInfo mDamage;               // +0x10 (0x1c) SLdrIng.miniPortalProjectileDamage
  SLdrPlasmaBeamInfo mBeamInfo;      // +0x2c (0x48) SLdrIng.miniPortalBeamInfo
};

// Retail shape from fn_29_EFC0: +0/+4/+8 assets, +0xc f, +0x10 asset, +0x14..0x1c floats,
// +0x20..0x28 five u16 sounds = 0x2a (padded to 0x2c). This is the swarm/exit-host block; the
// ctor's two optional GEN tokens at 0xce0/0xcf0 come from +0x00 and +0x10.
struct CIngSwarmData {
  // fn_29_EFC0 (0x40, branch-free): r4/r5/r6 -> +0/+4/+8, f1 -> +0xc, r7 -> +0x10, f2..f4 ->
  // +0x14..+0x1c, r8/r9/r10 and the two stack halfwords (sp+0xa, sp+0xe) -> +0x20..+0x28 (0xEFC0-0xEFF8).
  // @ fn_29_EFC0
  CIngSwarmData(CAssetId pART, CAssetId sRSC, CAssetId pART_3DA2, float unknown2327,
                CAssetId pART_081E, float unknownCB39, float unknown587C, float unknown0BD7,
                ushort soundSwarmMove, ushort soundExitHost, ushort soundExitHostSafeZone,
                ushort soundInsideHost, ushort sound)
  : mPART(pART)
  , mSRSC(sRSC)
  , mPART3DA2(pART_3DA2)
  , mUnknown2327(unknown2327)
  , mPART081E(pART_081E)
  , mUnknownCB39(unknownCB39)
  , mUnknown587C(unknown587C)
  , mUnknown0BD7(unknown0BD7)
  , mSoundSwarmMove(soundSwarmMove)
  , mSoundExitHost(soundExitHost)
  , mSoundExitHostSafeZone(soundExitHostSafeZone)
  , mSoundInsideHost(soundInsideHost)
  , mSound(sound) {}

  CAssetId mPART;                 // +0x00 SLdrIng.pART (a GEN token in the ctor)
  CAssetId mSRSC;                 // +0x04 SLdrIng.sRSC
  CAssetId mPART3DA2;             // +0x08 SLdrIng.pART_0x3da219c7
  float mUnknown2327;             // +0x0c SLdrIng.unknown_0x23271976
  CAssetId mPART081E;             // +0x10 SLdrIng.pART_0x081e9e6c (a GEN token in the ctor)
  float mUnknownCB39;             // +0x14 SLdrIng.unknown_0xcb39eccb
  float mUnknown587C;             // +0x18 SLdrIng.unknown_0x587ca175
  float mUnknown0BD7;             // +0x1c SLdrIng.unknown_0x0bd7d5a9
  ushort mSoundSwarmMove;         // +0x20 SLdrIng.sound_SwarmMove
  ushort mSoundExitHost;          // +0x22 SLdrIng.sound_ExitHost
  ushort mSoundExitHostSafeZone;  // +0x24 SLdrIng.sound_ExitHostSafeZone
  ushort mSoundInsideHost;        // +0x26 SLdrIng.sound_InsideHost
  ushort mSound;                  // +0x28 SLdrIng.sound (0x148b81e4)
};

// Host copy of the properties the Ing loader reads (retail: CIngData, 0x214 bytes at 0x7c0, built
// by fn_29_EE68 - the constructor's stores and the loader's call fixed the argument order). The
// +0x210 flags are the word at the actor's 0x9d0: its most significant byte is the retail's flag
// byte, which fn_29_EE68 fills with the ingFlags' low byte bit-reversed (eight rlwimi's, asm
// 0x0000EF4C-0x0000EFA8: the ingFlags' bit 0 goes to the byte's bit 7). The host keeps that word,
// so a byte mask m is the word mask (m << 24) - the kFlag_* enum in PortCIng.cpp. The triggers'
// tests give the bits their meaning (the table is in IngFamily.md section A.5).
class CIngData {
public:
  // fn_29_EE68 (0x158, branch-free): f1..f6 -> +0..+0x14 (0xEEB0-0xEED0), the colour r7 -> +0x18
  // (0xEED4), f7 -> +0x1c (0xEEDC), the CDamageInfo r5 -> +0x20..+0x3a, the vulnerability r6 -> +0x3c
  // (fn_29_EC88, 0xEF08), the five sub-structs r8/r9/r10/sp+0x38/sp+0x3c -> +0x6c/+0xac/+0x10c/
  // +0x170/+0x1e4 (0xEF14-0xEF44), then the flag byte +0x210 (IngFlagsWord, 0xEF4C-0xEFA8).
  // @ fn_29_EE68
  CIngData(uint ingFlags, float hearingRadius, const CDamageInfo& armSwipeDamage,
           const CDamageVulnerability& triggerVulnerability, const CColor& lightColor,
           const CIngBodyProjectileData& bodyProjectile, const CIngSpotData& ingSpot,
           const CIngGrappleData& grapple, const CIngMiniPortalData& miniPortal,
           const CIngSwarmData& swarm, float unknown5D0D, float unknownC620,
           float frustrationTime, float tauntChance, float aggressiveness,
           float lightAttenuation)
  : mHearingRadius(hearingRadius)
  , mUnknown5D0D(unknown5D0D)
  , mUnknownC620(unknownC620)
  , mFrustrationTime(frustrationTime)
  , mTauntChance(tauntChance)
  , mAggressiveness(aggressiveness)
  , mLightColor(lightColor)
  , mLightAttenuation(lightAttenuation)
  , mArmSwipeDamage(armSwipeDamage)
  , mTriggerVulnerability(triggerVulnerability)
  , mBodyProjectile(bodyProjectile)
  , mIngSpot(ingSpot)
  , mGrapple(grapple)
  , mMiniPortal(miniPortal)
  , mSwarm(swarm)
  , mFlags210(IngFlagsWord(ingFlags)) {}

  float mHearingRadius;      // +0x00 SLdrIng.hearingRadius (fn_29_EE68's first float argument)
  float mUnknown5D0D;        // +0x04 SLdrIng.unknown_0x5d0d2c40
  float mUnknownC620;        // +0x08 SLdrIng.unknown_0xc620183a
  float mFrustrationTime;    // +0x0c SLdrIng.frustrationTime
  float mTauntChance;        // +0x10 SLdrIng.tauntChance
  float mAggressiveness;     // +0x14 SLdrIng.aggressiveness
  CColor mLightColor;        // +0x18 SLdrIng.lightColor
  float mLightAttenuation;   // +0x1c SLdrIng.lightAttenuation
  CDamageInfo mArmSwipeDamage; // +0x20 (0x1c) SLdrIng.armSwipeDamage
  CDamageVulnerability mTriggerVulnerability; // +0x3c (0x30) SLdrIng.triggerVulnerability
  CIngBodyProjectileData mBodyProjectile;     // +0x6c (0x40)
  CIngSpotData mIngSpot;                      // +0xac (0x60)
  CIngGrappleData mGrapple;                   // +0x10c (0x64)
  CIngMiniPortalData mMiniPortal;             // +0x170 (0x74)
  CIngSwarmData mSwarm;                       // +0x1e4 (0x2c)
  uint mFlags210;                             // +0x210: the retail's flag word (the ingFlags' low
                                             // byte, bit-reversed, in the most significant byte)

  // The retail's flag byte (the data block's +0x210 = the actor's 0x9d0, the most significant byte
  // of the big-endian word) is the ingFlags' low byte bit-reversed: fn_29_EE68 inserts the
  // ingFlags' bits 0..7 into the byte's bits 7..0 with eight rlwimi's (asm 0x0000EF4C-0x0000EFA8).
  // The host keeps the word the retail has, so a byte mask m is the word mask (m << 24).
  static uint IngFlagsWord(uint flags) {
    uint byte = 0;
    for (int i = 0; i < 8; ++i) {
      if ((flags & (1u << i)) != 0) {
        byte |= 1u << (7 - i);
      }
    }
    return byte << 24;
  }
};
#endif

// The REL's CIng (module 29). 0xe60 bytes on the GameCube, built by fn_29_D540; its vtable
// lbl_29_data_A24 (82 words) is the map of what it overrides, and the analysis is
// docs/module-analysis/IngFamily.md section A (A.5 is the behaviour port). The state machine
// (fn_29_F2C8: 49 triggers, 29 states, 12 code functions) is registered in full; the form changes,
// the attacks and the mini-portal/exit-host states are minimal bodies marked [?].
class REL_EXPORT CIng : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
#ifdef TARGET_PC
  // Argument order is the REL loader's call at 0x00000FF8: (uid, name, entityInfo, transform,
  // modelData, actorParameters, patternedInfo, data).
  CIng(TUniqueId uid, const rstl::string& name, const CEntityInfo& info, const CTransform4f& xf,
       const CModelData& modelData, const CActorParameters& actParms, const CPatternedInfo& pInfo,
       const CIngData& data);
  ~CIng() override;

  // The console's `var <id> ing.<field> [value]` (PortCIng.cpp), for the trigger arms a run
  // rarely draws.
  static bool PortConsoleVar(CEntity& ent, const char* name, const int* value, int& current);

  // CPatterned
  // The seven accessors below are the REL's own one-line bodies (vtable slot -> function), so they
  // are real rather than stand-ins.
  using CPatterned::GetSearchPath;
  CProjectileInfo* ProjectileInfo() override { return nullptr; }        // slot 55, fn_29_2C
  CPathFindSearch* GetSearchPath() override { return &mPathFindSearch; } // slot 56, fn_29_0
  CPathFindPointSearch* GetPointSearchPath() override {                  // slot 57, fn_29_8
    return &mPointPathFindSearch;
  }
  CRagDoll* GetRagDoll() const override { return nullptr; }             // slot 61, fn_29_34
  TUniqueId GetAttackTarget() const override { return kInvalidUniqueId; } // slot 67, fn_29_3C

  // CAi
  bool CanBeShot(const CStateManager&, int) override { return true; }   // slot 42, fn_29_6C
  bool IsListening() const override { return true; }                    // slot 43, fn_29_10
  bool Listen(CStateManager& mgr, const CVector3f& position, EListenNoiseType type) override;
  // CPatterned, vtable slot 0xec (the REL's fn_29_B784, 0x10C): the module overrides it; the host
  // runs the base class's body and logs the stand-in.
  void UpdateHitDamageTime(float dt) override;
  void Death(CStateManager& mgr, const CVector3f& direction, EScriptObjectState state) override;
  void KnockBack(CStateManager& mgr, const CKnockBackInfo& info) override;
  void TakeDamage(const CVector3f& direction, float magnitude) override;

  // CActor
  using CPatterned::GetDamageVulnerability;
  const CDamageVulnerability* GetDamageVulnerability() const override; // fn_29_BF20
  rstl::optional_object< CAABox > GetTouchBounds() const override;   // fn_29_B970

  // CEntity / CActor
  void PreThink(float dt, CStateManager& mgr) override;
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  void Render(const CStateManager& mgr) const override;
  void Touch(CActor& actor, CStateManager& mgr) override;
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;
  void SetupStateMachine(CStateManager& mgr) override;

  // CPhysicsActor, vtable slot 0x80 (the REL's fn_29_B534): the flying body projectile remembers
  // the surface it hit, so the splat can be oriented to it.
  void CollidedWith(const TUniqueId& id, const CCollisionInfoList& list,
                    CStateManager& mgr) override;
  // CPhysicsActor, vtable slot 0x54 (the REL's fn_29_BCB4): the Ing's aim position.
  CVector3f GetAimPosition(const CStateManager& mgr, float dt) const override;

  // CEntity
  CEntity* TypesMatch(int typeId) const override;

  // Triggers (fn_29_F2C8's table order).
  bool StateOver(CStateManager& mgr, const CTriggerData& data) const;
  bool IsAlert(CStateManager& mgr, const CTriggerData& data) const;
  bool IsControllingHost(CStateManager& mgr, const CTriggerData& data) const;
  bool IsFacingTarget(CStateManager& mgr, const CTriggerData& data) const;
  bool HasTarget(CStateManager& mgr, const CTriggerData& data) const;
  bool HasNewTarget(CStateManager& mgr, const CTriggerData& data) const;
  bool HasLineOfSight(CStateManager& mgr, const CTriggerData& data) const;
  bool UnderFire(CStateManager& mgr, const CTriggerData& data) const;
  bool HeardShot(CStateManager& mgr, const CTriggerData& data) const;
  bool AreaClear(CStateManager& mgr, const CTriggerData& data) const;
  bool CoverLeash(CStateManager& mgr, const CTriggerData& data) const;
  bool HasCoverPoint(CStateManager& mgr, const CTriggerData& data) const;
  bool HasWallCoverPoint(CStateManager& mgr, const CTriggerData& data) const;
  bool HasMiniPortals(CStateManager& mgr, const CTriggerData& data) const;
  bool IsCorporeal(CStateManager& mgr, const CTriggerData& data) const;
  bool IsIngSpot(CStateManager& mgr, const CTriggerData& data) const;
  bool IsBodyProjectile(CStateManager& mgr, const CTriggerData& data) const;
  bool IsFrustrated(CStateManager& mgr, const CTriggerData& data) const;
  bool IsAggressive(CStateManager& mgr, const CTriggerData& data) const;
  bool IsLuredBySafeZone(CStateManager& mgr, const CTriggerData& data) const;
  bool StillLuredBySafeZone(CStateManager& mgr, const CTriggerData& data) const;
  bool UseProjectileFSMEntry(CStateManager& mgr, const CTriggerData& data) const;
  bool TargetInSafeZone(CStateManager& mgr, const CTriggerData& data) const;
  bool TargetInLightSafeZone(CStateManager& mgr, const CTriggerData& data) const;
  bool EmergePtInSafeZone(CStateManager& mgr, const CTriggerData& data) const;
  bool CanChangeForm(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldEvaporate(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldFleeSafeZone(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldBecomeCorporeal(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldBecomeIngSpot(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldArmSwipe(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldTaunt(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldBodyProjectile(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldOpenMiniPortal(CStateManager& mgr, const CTriggerData& data) const;
  bool ProjectileSplat(CStateManager& mgr, const CTriggerData& data) const;
  bool ProjectileHitTarget(CStateManager& mgr, const CTriggerData& data) const;
  bool FoundMovementPos(CStateManager& mgr, const CTriggerData& data) const;
  bool PathShagged(CStateManager& mgr, const CTriggerData& data) const;
  bool PathOver(CStateManager& mgr, const CTriggerData& data) const;
  bool PointPathOver(CStateManager& mgr, const CTriggerData& data) const;
  bool FoundPointPath(CStateManager& mgr, const CTriggerData& data) const;
  bool IsOffPath(CStateManager& mgr, const CTriggerData& data) const;
  bool HasAttackPattern(CStateManager& mgr, const CTriggerData& data) const;
  bool AttackPatternOver(CStateManager& mgr, const CTriggerData& data) const;
  bool TargetIsBall(CStateManager& mgr, const CTriggerData& data) const;
  bool InBallPursuitRange(CStateManager& mgr, const CTriggerData& data) const;
  bool HasPathToTarget(CStateManager& mgr, const CTriggerData& data) const;
  bool InGrappleRange(CStateManager& mgr, const CTriggerData& data) const;
  bool CanGrappleTarget(CStateManager& mgr, const CTriggerData& data) const;

  // States.
  void Start(CStateManager& mgr, EStateMsg msg, float dt);
  void WaitForFSMTransition(CStateManager& mgr, EStateMsg msg, float dt);
  void ExitHost(CStateManager& mgr, EStateMsg msg, float dt);
  void BecomeCorporeal(CStateManager& mgr, EStateMsg msg, float dt);
  void BecomeIngSpot(CStateManager& mgr, EStateMsg msg, float dt);
  void BecomeBodyProjectile(CStateManager& mgr, EStateMsg msg, float dt);
  void BecomeWallProjectile(CStateManager& mgr, EStateMsg msg, float dt);
  void Alert(CStateManager& mgr, EStateMsg msg, float dt);
  void SafeZoneReaction(CStateManager& mgr, EStateMsg msg, float dt);
  void Taunt(CStateManager& mgr, EStateMsg msg, float dt);
  void Lurk(CStateManager& mgr, EStateMsg msg, float dt);
  void PathFind(CStateManager& mgr, EStateMsg msg, float dt);
  void IngSpotPathFind(CStateManager& mgr, EStateMsg msg, float dt);
  void IngSpotPointPathFind(CStateManager& mgr, EStateMsg msg, float dt);
  void Patrol(CStateManager& mgr, EStateMsg msg, float dt);
  void FollowAttackPattern(CStateManager& mgr, EStateMsg msg, float dt);
  void BodyProjectileFlight(CStateManager& mgr, EStateMsg msg, float dt);
  void SuckEnergy(CStateManager& mgr, EStateMsg msg, float dt);
  void SeekWallPoint(CStateManager& mgr, EStateMsg msg, float dt);
  void SelectTarget(CStateManager& mgr, EStateMsg msg, float dt);
  void FaceTarget(CStateManager& mgr, EStateMsg msg, float dt);
  void FindCoverPoint(CStateManager& mgr, EStateMsg msg, float dt);
  void ArmSwipe(CStateManager& mgr, EStateMsg msg, float dt);
  void FailSafeMode(CStateManager& mgr, EStateMsg msg, float dt);
  void GrappleBall(CStateManager& mgr, EStateMsg msg, float dt);
  void FindMiniPortals(CStateManager& mgr, EStateMsg msg, float dt);
  void MiniPortalAttack(CStateManager& mgr, EStateMsg msg, float dt);
  void Dead(CStateManager& mgr, EStateMsg msg, float dt);
  void Evaporate(CStateManager& mgr, EStateMsg msg, float dt);

  // Code functions.
  void SetTargetDest(CStateManager& mgr, float dt);
  void SetCoverDest(CStateManager& mgr, float dt);
  void SetRecoverDest(CStateManager& mgr, float dt);
  void SetPointCoverDest(CStateManager& mgr, float dt);
  void SetWallPointCoverDest(CStateManager& mgr, float dt);
  void SetExitHostDest(CStateManager& mgr, float dt);
  void SetLuredDest(CStateManager& mgr, float dt);
  void SetLuredPointDest(CStateManager& mgr, float dt);
  void StartRangedAttack(CStateManager& mgr, float dt);
  void EndRangedAttack(CStateManager& mgr, float dt);
  void SplatOntoMesh(CStateManager& mgr, float dt);
  void ExpelGrappledBall(CStateManager& mgr, float dt);

private:
  void EnsureBodyActive(CStateManager& mgr);
  void LogState(const char* name);
  void JoinTeam(CStateManager& mgr);
  void MoveOnSurface(const CVector3f& move, float speed, float dt);
  void ApplySeparation(CStateManager& mgr);
  float DistSqToTarget(const CStateManager& mgr) const;
  CScriptAIHint* GetCoverHint(CStateManager& mgr, TUniqueId id) const;
  void ReleaseCoverHint(CStateManager& mgr, TUniqueId& id);
  void UseCoverHint(CScriptAIHint& hint);
  void UpdateTouchBox();
  void ValidateLureZone(CStateManager& mgr);
  void UpdateTimers(float dt, CStateManager& mgr);
  void UpdateValidTarget(CStateManager& mgr);
  void UpdateLoopSfx();
  // fn_29_4604 (CIng::Think's call at asm 0x0000C794): the blob (mBlobId, 0xd5c) follows the
  // surface the Ing stands on.
  void AlignBlobToSurface(CStateManager& mgr);
  void StopSfx();
  // fn_29_1A80: turn horizontally to the lure safe zone when one is set, else to the target.
  void FaceLureOrTarget(CStateManager& mgr);
  // fn_29_1F1C: ExitHost's Deactivate smoke, "IngExitHostSmokeFx" at the Ing's transform, plus the
  // swarm block's own sound (CIngData swarm +0x28); only when the second GEN token is loaded.
  void SpawnExitHostSmoke(CStateManager& mgr);
  // fn_29_4830: the ing spot's blob (a GeomBlobV2, module 25's CCollisionTracker).
  void CreateBlob(CStateManager& mgr);
  // fn_29_4B4C: the actor's own light (a CGameLight).
  void CreateLight(CStateManager& mgr);
  // fn_29_4960: the actor's light (0xd68) follows the Ing's eye segment and its data colour.
  void UpdateLight(CStateManager& mgr, float dt);
  // fn_29_4468: the target's aim position (CActor's GetAimPosition, prediction in f1), false when
  // it is gone. Callers pass rodata_6C (0.0f) except the body projectile's flight (rodata_B4, 0.5f).
  bool TargetAimPosition(CStateManager& mgr, CVector3f& out, float prediction = 0.f);
  // fn_29_3E1C: one arm's segment boxes (fn_29_3D74) against the target's bounds; on a hit, the
  // arm swipe damage with the given knock-back.
  void ArmSwipeSegment(CStateManager& mgr, const CSegId& a, const CSegId& b, const CSegId& c,
                       const CSegId& d, const CVector3f& knockback);
  // fn_29_3190: builds the manager of seven joint spheres and installs it at 0xc50.
  void CreateCollisionActorManager(CStateManager& mgr);
  // fn_29_3014: gives every collision actor the Ing's health and damage vulnerability.
  void SetupCollisionActors(CStateManager& mgr);
  // fn_29_2ED8 (kSM_HitObject): a collision actor that last touched the player deals contact damage.
  void CollisionActorHitPlayer(CStateManager& mgr, TUniqueId sender);
  // fn_29_2B70 (kSM_Damage in the ing-spot form): the hit reaction and its effects.
  void IngSpotDamage(CStateManager& mgr, TUniqueId sender);
  // fn_29_2684 (kSM_Damage otherwise): a weapon hit on a collision actor becomes the Ing's damage.
  void CollisionActorDamage(CStateManager& mgr, TUniqueId sender);
  // fn_29_2414: the explosion of the hit effect token (heavy when asked and loaded); not added.
  CEntity* SpawnHitEffect(CStateManager& mgr, bool heavy);

  // Host layout (retail's CIng is 0xe60, CHECK_SIZEOF below; CPatterned is 0x7c0). Guest offsets
  // are the REL's, from the constructor fn_29_D540 and the tiny accessors fn_29_0 (this+0x9dc),
  // fn_29_8 (this+0xac8), the DOL's fn_801F8258 (this+0xcb0) and fn_801F7CA0 (this+0xc58).
  CIngData mData;                            // 0x7c0 (0x214)
  CPathFindSearch mPathFindSearch;           // 0x9dc (0xec)
  CPathFindPointSearch mPointPathFindSearch; // 0xac8 (0x188)
  rstl::single_ptr< CCollisionActorManager > mCollisionActorManager; // 0xc50
  CSurfaceAlignmentHelper mSurfaceAlignment; // 0xc58 (0x58)
  CIngSpotPathFindNavigation mIngSpotPathFindNavigation; // 0xcb0 (0x10)
  int mForm;                                 // 0x9d4 (2): 2 corporeal, 3 ing spot, 4 flying body proj, 5 evaporating, 6..9 the form changes' own forms
  int mDesiredForm;                          // 0x9d8 (-1)
  rstl::optional_object< CAABox > mTouchBox; // 0xcc0 (0xcd8 = engaged)
  TUniqueId mHostId;                         // 0xcdc
  TUniqueId mPossessedEffectId;              // 0xcde: the "Ing Possessed Mold" billboard
  // 0xce0: the swarm GEN token (from CIngData+0x1e4). Retail's own engaged byte is 0xcec (the
  // token's +0xc) and SuckEnergy reads it, so the optional's engaged flag stands in for it, as with
  // mMiniPortalEffect below.
  rstl::optional_object< TLockedToken< CGenDescription > > mSwarmEffect;
  // 0xcf0: CIngData+0x1e4+0x10, the swarm block's second GEN token (SLdrIng.pART_0x081e9e6c). Its
  // engaged byte is 0xcfc, which ExitHost's smoke helper fn_29_1F1C tests, so the optional's own
  // flag stands in for it as with mSwarmEffect above.
  rstl::optional_object< TLockedToken< CGenDescription > > mSwarmEffect2;
  TUniqueId mBlobId;                         // 0xd5c [?] never spawned yet
  TUniqueId mLightId;                        // 0xd68: the actor's light (fn_29_4B4C)
  TUniqueId mTeamId;                         // 0xd6a
  TUniqueId mTargetId;                       // 0xd6c
  TUniqueId mPrevTargetId;                   // 0xd6e
  TUniqueId mAttackPathResumeId;             // 0xd70
  TUniqueId mCoverHintId;                    // 0xd72
  TUniqueId mLastCoverHintId;                // 0xd74
  TUniqueId mD76Id;                          // 0xd76, cleared once its object is gone
  TUniqueId mLureSafeZoneId;                 // 0xd78
  CSfxHandle mSuckSfx;                       // 0xd00: SuckEnergy's looping voice
  CSfxHandle mGrappleSfx;                    // 0xd04: the grapple's looping voice, fn_29_6578/4D18
  rstl::optional_object< TLockedToken< CGenDescription > > mNormalHitEffect; // 0xd08 ing-spot hit fx
  rstl::optional_object< TLockedToken< CGenDescription > > mHeavyHitEffect;  // 0xd18 ing-spot heavy hit fx
  rstl::optional_object< TLockedToken< CGenDescription > > mDeathEffect; // 0xd28
  // 0xd38: CIngData+0x178 (the mini-portal effect). Retail's engaged byte is 0xd44, which the ctor
  // writes and fn_29_C228 reads; the optional's own engaged flag stands in for it.
  rstl::optional_object< TLockedToken< CGenDescription > > mMiniPortalEffect;
  // 0xd48: the body projectile's splat effect (CIngData+0x8c); retail's engaged byte is 0xd54.
  rstl::optional_object< TLockedToken< CGenDescription > > mSplatEffect;
  CSfxHandle mBodyProjectileSfx;             // 0xd58: the body projectile's looping flight sound
  CSfxHandle mLoopSfx[2];                    // 0xd60/0xd64: idle, move
  CSegId mEyeSegId;                          // 0xd7b: the "head" locator, the ctor copies it to 0xd8a
  CSegId mCollarSegId;                       // 0xd7a: the "Collar_SDK" locator, the corporeal aim
                                            // (fn_29_BCB4); the ctor's lookup at asm 0x0000E448
  // fn_29_6A88's arm locators, looked up by the ctor's GetLocatorSegId calls (the names are in
  // rodata 0x68E: "R_shoulder" at +0x632, "R_elbow" at +0x55c, "R_forearm" at +0x63d,
  // "R_wrist_end" at +0x647, "L_shoulder" at +0x653, "L_elbow" at +0x554, "L_forearm" at +0x65e,
  // "L_wrist_end" at +0x668). 0xd7a is "Collar_SDK" (GetAimPosition).
  CSegId mRightShoulderSegId;                // 0xd7c
  CSegId mRightElbowSegId;                   // 0xd7d
  CSegId mRightForearmSegId;                 // 0xd7e
  CSegId mRightWristSegId;                   // 0xd7f
  CSegId mLeftShoulderSegId;                 // 0xd80
  CSegId mLeftElbowSegId;                    // 0xd81
  CSegId mLeftForearmSegId;                   // 0xd82
  CSegId mLeftWristSegId;                    // 0xd83
  CLineOfSightTracker mLineOfSight;          // 0xd88 (its segment is the byte at 0xd8a)
  CVector3f mTargetPos;                      // 0xdc8
  CVector3f mSurfaceNormal;                  // 0xdd4: the surface the body projectile hit
                                           // (fn_29_B534 writes it), the splat's orientation
  CVector3f mSurfaceMoveDir;                 // 0xde0
  CVector3f mPortalPlaneNormal;              // 0xdf0: the portal plane's normal, Up() = (0,0,1)
  float mPortalPlaneConstant;                // 0xdfc: dot(translation, normal) = translation.z
  int mSwipeArm;                             // 0xdec: 0 right, 1 left, -1 none (fn_29_6A88/421C)
  CVector3f mModelScale;                     // host copy of the model data's scale
  int mMiniPortalCount;                      // 0xe00
  CVector3f mMiniPortalPositions[3];         // 0xe04..0xe24: where FindMiniPortals' rays hit
  int mMiniPortalIndex;                      // 0xe28: the next portal to fire (-1 when idle)
  int mSafeZoneCount;                        // 0xe2c
  float mHeardShotTime;                      // 0xe30
  float mUnderFireTime;                      // 0xe34
  float mArmSwipeTime;                       // 0xe38
  float mFormTime;                           // 0xe3c
  float mBodyState5Time;                     // 0xe40
  float mFrustrationTimer;                   // 0xe44
  float mBallCooldown;                       // 0xe48
  float mHitReactionTime;                    // 0xe4c [?] set -1 by an ing-spot hit (fn_29_2B70), read by fn_29_6578/4D18
  float mLightBlend;                         // 0xe50
  float mDeadFade;                           // 0xe54
  float mBodyProjectileTime;                 // 0xe58
  bool mEvaporate : 1;                       // 0xe5c bit 7
  bool mTaunt : 1;                           // 0xe5c bit 6
  bool mBodyProjectileAllowed : 1;           // 0xe5c bit 5
  bool mAggressive : 1;                      // 0xe5c bit 4
  bool mFoundMovementPos : 1;                // 0xe5c bit 3
  bool mRecalcPath : 1;                      // 0xe5c bit 2
  bool xE5C_1 : 1;                           // 0xe5c bit 1 [?] set/cleared by fn_29_6A88 only
  bool mTakeOff : 1;                         // 0xe5c bit 0: kUE_TakeOff sets it (fn_29_C228)
  bool mRenderModel : 1;                     // 0xe5d bit 7
  bool mPortalPlane : 1;                     // 0xe5d bit 6
  bool mStartAsIngSpot : 1;                  // 0xe5d bit 5 (data bit 7)
  bool mForm9Render : 1;                     // 0xe5d bit 4
  bool mFollowingAttackPath : 1;             // 0xe5d bit 3
  bool mAlert : 1;                           // 0xe5d bit 2
  bool mMoving : 1;                          // 0xe5d bit 1
  bool mGrappled : 1;                        // 0xe5d bit 0
  bool mProjectileFSMEntry : 1;              // 0xe5e bit 7
  bool mSplat : 1;                           // 0xe5e bit 6
  bool mSafeZoneIngSpot : 1;                 // 0xe5e bit 5
  bool mHurtfulZone : 1;                     // 0xe5e bit 4
  const char* mLastLoggedState;              // host: MP2_DEBUG_ING
  float mStateTime;                          // host: time in the current state [?] retail's source
  bool mSuckTickLogged;                      // host: SuckEnergy's first-frame trace
  CPFArea* mPathArea;                        // host: the area both searches were given on XALD
  int mDebugPoll;                            // host: MP2_DEBUG_ING's once-a-second position poll
  // Not held: the other optional PART/GEN tokens at 0xcf0..0xd58, the nine CSegId locators at
  // 0xd7a/0xd7c..0xd83 ("Collar_SDK", the shoulders/forearms/wrists/elbows) and the light at 0xd68.
#else
  uchar x7c0_[0x6a0]; // CIngData, the path searches, the seg ids, the tokens and the flags
#endif
};
CHECK_SIZEOF(CIng, 0xe60)

#endif
