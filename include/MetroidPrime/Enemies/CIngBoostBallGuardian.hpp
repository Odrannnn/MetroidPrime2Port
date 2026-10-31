#ifndef _CINGBOOSTBALLGUARDIAN
#define _CINGBOOSTBALLGUARDIAN

#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "REL/REL_Setup.h"

#ifdef TARGET_PC
#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Graphics/CColor.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "Kyoto/Math/CMayaSpline.hpp"
#include "Kyoto/SObjectTag.hpp"
#include "Kyoto/TToken.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CLineOfSightTracker.hpp"
#include "MetroidPrime/CSurfaceAlignmentHelper.hpp"
#include "MetroidPrime/Enemies/CIng.hpp"
#include "MetroidPrime/Enemies/CIngSpotPathFindNavigation.hpp"
#include "MetroidPrime/PathFinding/CPathFindPointSearch.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"
#include "MetroidPrime/ScriptLoader/Structs/SLdrPlasmaBeamInfo.hpp"
#include "rstl/optional_object.hpp"
#include "rstl/single_ptr.hpp"

class CCollisionActorManager;
class CGenDescription;
class CScriptAIHint;
class CPFArea;

// Host copies of the two parameter blocks the REL's constructor copies into the object
// (guest +0x7C0, 0x320 bytes, by fn_30_1056C; guest +0xDEC, 0x110 bytes, by fn_30_10250). The
// host layout is its own; the guest offsets are in docs/module-analysis/BoostGuardian.md. Every
// field is one property of SLdrIngBoostBallGuardian (include/MetroidPrime/ScriptLoader/
// SLdrIngBoostBallGuardian.hpp), which is why the names carry the property hashes where the game
// gives none: nothing in this milestone reads the values, they are held so the loader can build
// them. The Ing half reuses CIng's own sub-structs (CIngSpotData, CIngMiniPortalData,
// CIngBodyProjectileData, CIngSwarmData) because the REL reads the same set of properties into
// them; the grapple block and the trigger/face-plate properties CIng has are absent here.
struct CIngBoostBallGuardianData {
  CIngBoostBallGuardianData(uint unknownFlags, float hearingRadius, float unknown5D0D,
                            float unknownC620, float frustrationTime, float tauntChance,
                            float aggressiveness, const CColor& lightColor,
                            float lightAttenuation, const CDamageInfo& armSwipeDamage,
                            const CDamageVulnerability& ingSpotVulnerability,
                            const CIngBodyProjectileData& bodyProjectile,
                            const CIngSpotData& ingSpot, const CIngMiniPortalData& miniPortal,
                            const CIngSwarmData& swarm)
  : mFlags210(unknownFlags)
  , mHearingRadius(hearingRadius)
  , mUnknown5D0D(unknown5D0D)
  , mUnknownC620(unknownC620)
  , mFrustrationTime(frustrationTime)
  , mTauntChance(tauntChance)
  , mAggressiveness(aggressiveness)
  , mLightColor(lightColor)
  , mLightAttenuation(lightAttenuation)
  , mArmSwipeDamage(armSwipeDamage)
  , mIngSpotVulnerability(ingSpotVulnerability)
  , mBodyProjectile(bodyProjectile)
  , mIngSpot(ingSpot)
  , mMiniPortal(miniPortal)
  , mSwarm(swarm) {}

  uint mFlags210; // guest +0x320 (this+0xAE0): PackBgDataFlags(unknown_0x01789abd), fn_30_10C24
  float mHearingRadius;                        // hearingRadius
  float mUnknown5D0D;                          // unknown_0x5d0d2c40
  float mUnknownC620;                          // unknown_0xc620183a
  float mFrustrationTime;                      // frustrationTime
  float mTauntChance;                          // tauntChance
  float mAggressiveness;                       // aggressiveness
  CColor mLightColor;                          // lightColor
  float mLightAttenuation;                     // lightAttenuation
  CDamageInfo mArmSwipeDamage;                 // armSwipeDamage
  CDamageVulnerability mIngSpotVulnerability;  // ingSpotVulnerability
  CIngBodyProjectileData mBodyProjectile;      // bodyProjectile* properties
  CIngSpotData mIngSpot;                       // ingSpot* properties
  CIngMiniPortalData mMiniPortal;              // miniPortal* properties
  CIngSwarmData mSwarm;                        // pART/sRSC/swarm sound properties
};

// One of the three health phases (SLdrIngBoostBallGuardianStruct). Retail keeps three of them and
// picks the first whose threshold the current health is under - the same shape as Sandworm's
// SLdrSandwormStruct. The six ints are unnamed in the script properties; their meaning is not
// established, so they keep their hashes.
struct CIngBoostBallGuardianPhase {
  CIngBoostBallGuardianPhase(float boostPuddleModeTime, float unknownABE9,
                             float breakBoostPuddleModeDamage, float breakBoostModeDamage,
                             float nextBoostTimeMin, float nextBoostTimeMax, int unknown285D,
                             int unknown6D5F, int unknownE3FF, int unknownA6FD, int unknownECB3,
                             int unknownA9B1, float locomotionSpeedScale,
                             float ingSpotSpeedScale)
  : mBoostPuddleModeTime(boostPuddleModeTime)
  , mUnknownABE9(unknownABE9)
  , mBreakBoostPuddleModeDamage(breakBoostPuddleModeDamage)
  , mBreakBoostModeDamage(breakBoostModeDamage)
  , mNextBoostTimeMin(nextBoostTimeMin)
  , mNextBoostTimeMax(nextBoostTimeMax)
  , mUnknown285D(unknown285D)
  , mUnknown6D5F(unknown6D5F)
  , mUnknownE3FF(unknownE3FF)
  , mUnknownA6FD(unknownA6FD)
  , mUnknownECB3(unknownECB3)
  , mUnknownA9B1(unknownA9B1)
  , mLocomotionSpeedScale(locomotionSpeedScale)
  , mIngSpotSpeedScale(ingSpotSpeedScale) {}

  float mBoostPuddleModeTime;        // boostPuddleModeTime
  float mUnknownABE9;                // unknown_0xabe99de0
  float mBreakBoostPuddleModeDamage; // breakBoostPuddleModeDamage
  float mBreakBoostModeDamage;       // breakBoostModeDamage
  float mNextBoostTimeMin;           // nextBoostTimeMin
  float mNextBoostTimeMax;           // nextBoostTimeMax
  int mUnknown285D;                  // unknown_0x285d67ad
  int mUnknown6D5F;                  // unknown_0x6d5f242f
  int mUnknownE3FF;                  // unknown_0xe3ff2ed6
  int mUnknownA6FD;                  // unknown_0xa6fd6d54
  int mUnknownECB3;                  // unknown_0xecb314b6
  int mUnknownA9B1;                  // unknown_0xa9b15734
  float mLocomotionSpeedScale;       // locomotionSpeedScale
  float mIngSpotSpeedScale;          // ingSpotSpeedScale
};

// The boost-ball half (SLdrBoostBallGuardianData): what the rolling form needs, plus the three
// phases.
struct CBoostBallGuardianData {
  CBoostBallGuardianData(const CVector3f& scale, float mass, float unknownBEA9,
                         const CMayaSpline& speed, float minDamageSpeed, float unknown7B21, CAssetId model,
                         CAssetId chargeUpEffect, CAssetId shieldEffect, CAssetId trailEffect,
                         ushort soundBounce, ushort soundIntoBall, ushort soundOutOfBall,
                         ushort soundLoop, ushort soundBoost, ushort soundRolling,
                         float soundMaxDistance, float soundDistanceComp,
                         const CDamageVulnerability& vulnerability, int unknownEE69,
                         int unknown4B2D, float searchConeAngle, float unknownB0E8,
                         const CDamageInfo& damageToMorphBall,
                         const CDamageInfo& damageToFirstPerson,
                         const CDamageInfo& ingSpotDamageToMorphBall,
                         const CDamageInfo& ingSpotDamageToFirstPerson, CAssetId blobEffect,
                         CAssetId hitNormalDamage, CAssetId hitHeavyDamage,
                         const CIngBoostBallGuardianPhase& phase0,
                         const CIngBoostBallGuardianPhase& phase1,
                         const CIngBoostBallGuardianPhase& phase2)
  : mScale(scale)
  , mMass(mass)
  , mUnknownBEA9(unknownBEA9)
  , mSpeed(speed)
  , mMinDamageSpeed(minDamageSpeed)
  , mUnknown7B21(unknown7B21)
  , mModel(model)
  , mChargeUpEffect(chargeUpEffect)
  , mShieldEffect(shieldEffect)
  , mTrailEffect(trailEffect)
  , mSoundBounce(soundBounce)
  , mSoundIntoBall(soundIntoBall)
  , mSoundOutOfBall(soundOutOfBall)
  , mSoundLoop(soundLoop)
  , mSoundBoost(soundBoost)
  , mSoundRolling(soundRolling)
  , mSoundMaxDistance(soundMaxDistance)
  , mSoundDistanceComp(soundDistanceComp)
  , mVulnerability(vulnerability)
  , mUnknownEE69(unknownEE69)
  , mUnknown4B2D(unknown4B2D)
  , mSearchConeAngle(searchConeAngle)
  , mUnknownB0E8(unknownB0E8)
  , mDamageToMorphBall(damageToMorphBall)
  , mDamageToFirstPerson(damageToFirstPerson)
  , mIngSpotDamageToMorphBall(ingSpotDamageToMorphBall)
  , mIngSpotDamageToFirstPerson(ingSpotDamageToFirstPerson)
  , mBlobEffect(blobEffect)
  , mHitNormalDamage(hitNormalDamage)
  , mHitHeavyDamage(hitHeavyDamage)
  , mPhase0(phase0)
  , mPhase1(phase1)
  , mPhase2(phase2) {}

  CVector3f mScale;                     // boostBallScale
  float mMass;                          // boostBallMass
  float mUnknownBEA9;                   // unknown_0xbea96fb7
  CMayaSpline mSpeed;                   // boostBallSpeed (guest data +0x4C)
  float mMinDamageSpeed;                // boostBallMinDamageSpeed
  float mUnknown7B21;                   // unknown_0x7b21e31a
  CAssetId mModel;                      // boostBallModel
  CAssetId mChargeUpEffect;             // boostBallChargeUpEffect
  CAssetId mShieldEffect;               // boostBallShieldEffect
  CAssetId mTrailEffect;                // boostBallTrailEffect
  ushort mSoundBounce;                  // sound_Bounce
  ushort mSoundIntoBall;                // sound_IntoBall
  ushort mSoundOutOfBall;               // sound_OutofBall
  ushort mSoundLoop;                    // sound (0x9f7372b3)
  ushort mSoundBoost;                   // sound_Boost
  ushort mSoundRolling;                 // sound_Rolling
  // Guest data +0xC0/+0xC4: not properties but the loader's constants 1000.0 and 1.0
  // (fn_30_748, 0x000008E8/0x000008EC); the ball's rolling sfx builds its
  // C3DEmitterParmData(maxDist, distComp) from them (0x000129C8).
  float mSoundMaxDistance;              // 1000.0
  float mSoundDistanceComp;             // 1.0
  CDamageVulnerability mVulnerability;  // boostBallVulnerability
  int mUnknownEE69;                     // unknown_0xee69b993
  int mUnknown4B2D;                     // unknown_0x4b2de673
  float mSearchConeAngle;               // searchConeAngle
  float mUnknownB0E8;                   // unknown_0xb0e85d53
  CDamageInfo mDamageToMorphBall;       // boostBallDamageToMorphBall
  CDamageInfo mDamageToFirstPerson;     // boostBallDamageToFirstPerson
  CDamageInfo mIngSpotDamageToMorphBall;    // ingSpotDamageToMorphBall
  CDamageInfo mIngSpotDamageToFirstPerson;  // ingSpotDamageToFirstPerson
  CAssetId mBlobEffect;                 // boostIngSpotBlobEffect
  CAssetId mHitNormalDamage;            // boostIngSpotHitNormalDamage
  CAssetId mHitHeavyDamage;             // boostIngSpotHitHeavyDamage
  CIngBoostBallGuardianPhase mPhase0;   // ingBoostBallGuardianStruct
  CIngBoostBallGuardianPhase mPhase1;   // ingBoostBallGuardianStruct_0xfe18a18f
  CIngBoostBallGuardianPhase mPhase2;   // ingBoostBallGuardianStruct_0xc2784287
};
#endif

// The REL's CIngBoostBallGuardian (module 30): the Boost Guardian. 0x11F0 bytes on the GameCube,
// built by fn_30_EC6C, its vtable lbl_30_data_9C0 (82 words, 34 local overrides) is
// /tmp/opencode/ibbg_vtable.md and the analysis is docs/module-analysis/BoostGuardian.md.
//
// Milestone 1 (this file's first slice) is load-and-stand: the loader, the constructor, the path
// searches and the accessors the vtable declares. The state machine (fn_30_C6CC installs the 49
// triggers, 28 states and 9 codes listed in the analysis), the boost ball, the puddle form, the
// mini portals and the second class (a CPhysicsActor spawned by fn_30_3478) are not written yet,
// and every override that belongs to them forwards to CPatterned behind a once-only StandIn log.
class REL_EXPORT CIngBoostBallGuardian : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
#ifdef TARGET_PC
  // Argument order is the REL loader's call at 0x0000051C (fn_30_130): uid, editor-property name,
  // entity info, transform, model, actor parameters, patterned info, then the two parameter blocks
  // on the stack. Retail's constructor takes one model only (loader arg r8 = the CModelData at
  // stack 0x320); the boost ball's model is an asset in the boost block, not a second CModelData.
  CIngBoostBallGuardian(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                        const CTransform4f& xf, const CModelData& modelData,
                        const CActorParameters& actParms, const CPatternedInfo& pInfo,
                        const CIngBoostBallGuardianData& data,
                        const CBoostBallGuardianData& boost);
  ~CIngBoostBallGuardian() override;

  // CPatterned accessors this REL overrides with a one-line body, so they are real, not stand-ins.
  // @ fn_30_0
  CPathFindSearch* GetSearchPath() override { return &mPathFindSearch; }              // fn_30_0
  // @ fn_30_8
  CPathFindPointSearch* GetPointSearchPath() override { return &mPointPathFindSearch; } // fn_30_8
  // @ fn_30_58
  float GetGravityConstant() const override { return kDefaultGravityAccel; }          // fn_30_58
  // @ fn_30_6C
  bool IsListening() const override { return true; }                                 // fn_30_6C
  // @ fn_30_10
  bool CanBeShot(const CStateManager&, int) override { return true; }                // fn_30_10
  // @ fn_30_34
  CRagDoll* GetRagDoll() const override { return nullptr; }                          // fn_30_34
  TUniqueId GetAttackTarget() const override;                                        // fn_30_3C
  // The REL's own copies of base-class one-liners (vtable slots 43, 48, 53, 66, 71, 72).
  // @ fn_30_74
  CVector3f GetOrigin(const CStateManager&, const CTeamAiRole&, const CVector3f&) const override {
    return GetTranslation(); // 0x00000074: 0x54/0x58/0x5C
  }
  // @ fn_30_24
  uchar GetModelAlphau8(const CStateManager&) const override { return mColor.GetAlphau8(); } // 0x44F
  // @ fn_30_2C
  CProjectileInfo* ProjectileInfo() override { return nullptr; }
  // @ fn_30_4C
  bool IsOnGround() const override { return mOnGround; } // 0x0000004C: 0x34C 0x08
  // @ fn_30_64
  const rstl::optional_object< TCachedToken< CGenDescription > >&
  GetDeathExplosionParticle() const override {
    return CPatterned::GetDeathExplosionParticle(); // 0x00000064: this + 0x754
  }
  // @ fn_30_18
  float GetDeathTimeScale() const override { return 1.f; } // 0x00000018: rodata_64, no burn scale

  // CAi
  void Death(CStateManager& mgr, const CVector3f& direction, EScriptObjectState state) override;
  void KnockBack(CStateManager& mgr, const CKnockBackInfo& info) override;

  // CEntity / CActor
  void PreThink(float dt, CStateManager& mgr) override;
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  // @ fn_30_D2C0
  void PreRenderAllViewports(CStateManager& mgr) override { CPatterned::PreRenderAllViewports(mgr); }
  void Render(const CStateManager& mgr) const override;
  void CollidedWith(const TUniqueId& id, const CCollisionInfoList& list,
                    CStateManager& mgr) override;
  void SetupStateMachine(CStateManager& mgr) override;
  void Touch(CActor& actor, CStateManager& mgr) override;
  void UpdateHitDamageTime(float dt) override; // fn_30_CA18
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;

  // The Ing half's overrides of the base class (the Ing REL's own copies of them).
  using CPatterned::GetDamageVulnerability;
  const CDamageVulnerability* GetDamageVulnerability() const override;
  rstl::optional_object< CAABox > GetTouchBounds() const override;
  void TakeDamage(const CVector3f& direction, float magnitude) override;
  bool Listen(CStateManager& mgr, const CVector3f& position, EListenNoiseType type) override;
  CVector3f GetAimPosition(const CStateManager& mgr, float dt) const override; // fn_30_CFC4

  // ---------------------------------------------------------------------------------------------
  // The state machine. fn_30_C6CC installs three tables into the machine: the 49 triggers
  // (lbl_30_data_250), the 28 states (lbl_30_data_6B0) and the 9 code functions
  // (lbl_30_data_8DC). The names are the REL's own strings; the order is the table order.
  // The Ing half of the names is the Ing REL's (docs/module-analysis/IngFamily.md section A.2);
  // the boost half is this module's own.

  // Triggers (fn_30_C6CC's table order).
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
  bool UseProjectileFSMEntry(CStateManager& mgr, const CTriggerData& data) const;
  bool TargetInSafeZone(CStateManager& mgr, const CTriggerData& data) const;
  bool TargetInLightSafeZone(CStateManager& mgr, const CTriggerData& data) const;
  bool CanChangeForm(CStateManager& mgr, const CTriggerData& data) const;
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
  bool HasPathToTarget(CStateManager& mgr, const CTriggerData& data) const;
  bool CanGrappleTarget(CStateManager& mgr, const CTriggerData& data) const;
  bool IsBoostPuddleForm(CStateManager& mgr, const CTriggerData& data) const;
  bool IsCurrentlyInBoostPuddleForm(CStateManager& mgr, const CTriggerData& data) const;
  bool IsBoostOver(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldBoost(CStateManager& mgr, const CTriggerData& data) const;
  bool HasCollidedWithCrystal(CStateManager& mgr, const CTriggerData& data) const;
  bool BoostModeRetreatOver(CStateManager& mgr, const CTriggerData& data) const;

  // States.
  void Start(CStateManager& mgr, EStateMsg msg, float dt);
  void WaitForFSMTransition(CStateManager& mgr, EStateMsg msg, float dt);
  void ExitHost(CStateManager& mgr, EStateMsg msg, float dt);
  void BecomeCorporeal(CStateManager& mgr, EStateMsg msg, float dt);
  void BecomeIngSpot(CStateManager& mgr, EStateMsg msg, float dt);
  void BecomeBodyProjectile(CStateManager& mgr, EStateMsg msg, float dt);
  void BecomeWallProjectile(CStateManager& mgr, EStateMsg msg, float dt);
  void Alert(CStateManager& mgr, EStateMsg msg, float dt);
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
  void FindMiniPortals(CStateManager& mgr, EStateMsg msg, float dt);
  void MiniPortalAttack(CStateManager& mgr, EStateMsg msg, float dt);
  void Dead(CStateManager& mgr, EStateMsg msg, float dt);
  void BoostAttack(CStateManager& mgr, EStateMsg msg, float dt);
  void BoostModeRetreat(CStateManager& mgr, EStateMsg msg, float dt);

  // Code functions.
  void SetTargetDest(CStateManager& mgr, float dt);
  void SetCoverDest(CStateManager& mgr, float dt);
  void SetRecoverDest(CStateManager& mgr, float dt);
  void SetPointCoverDest(CStateManager& mgr, float dt);
  void SetWallPointCoverDest(CStateManager& mgr, float dt);
  void SetExitHostDest(CStateManager& mgr, float dt);
  void StartRangedAttack(CStateManager& mgr, float dt);
  void EndRangedAttack(CStateManager& mgr, float dt);
  void SplatOntoMesh(CStateManager& mgr, float dt);
  // fn_30_2F9C takes the actor by pointer; the boost ball calls it too (0x00013304, 0x000144E4).
  static CScriptAIHint* FindBoostNavPoint(CStateManager& mgr, const CActor& actor, int hintType);

private:
  void EnsureBodyActive(CStateManager& mgr);
  // fn_30_C6CC's three tables go into the machine under the REL's own names; the tables live in
  // PortCIngBoostBallGuardianFsm.cpp.
  void InstallStateMachineFunctions();
  // fn_30_*: the helpers the states and triggers share. The names are the Ing REL's
  // (fn_29_*), because this module carries its own copy of the Ing code; the behaviour is the
  // Ing's, ported from the Ing host body.
  void LogState(const char* name);
  void JoinTeam(CStateManager& mgr);
  void MoveOnSurface(const CVector3f& move, float speed, float dt);
  void ApplySeparation(CStateManager& mgr);
  float DistSqToTarget(const CStateManager& mgr) const;
  CScriptAIHint* GetCoverHint(CStateManager& mgr, TUniqueId id) const;
  CScriptAIHint* FindBoostNavPoint(CStateManager& mgr, int hintType) const {
    return FindBoostNavPoint(mgr, *this, hintType);
  }
  // fn_30_3478: spawns the boost ball (PortCBoostBallGuardianBall.hpp); fn_30_340C stores the
  // player's aim position, fn_30_324C finds the ground under the actor.
  void SpawnBoostBall(CStateManager& mgr);
  void UpdateBoostAimPos(CStateManager& mgr);
  CVector3f FindBoostSpawnPos(CStateManager& mgr) const;
  void ReleaseCoverHint(CStateManager& mgr, TUniqueId& id);
  // fn_30_6064: release the cover hint (guest 0x108E); the id is cleared only if the hint exists.
  void ReleaseOwnCoverHint(CStateManager& mgr);
  // fn_30_68F0: the kSM_Create body - the form, the locomotion type and the surface alignment.
  void OnCreate(CStateManager& mgr);
  // fn_30_6014: claim a cover hint (in use, then the hint's id into 0x108E and 0x1090).
  void ClaimCoverHint(CScriptAIHint& hint);
  void UpdateTimers(float dt, CStateManager& mgr);
  void UpdateValidTarget(CStateManager& mgr);
  void UpdateLoopSfx();
  void StopSfx();
  void RemoveSfxEmitters(); // fn_30_3A60
  // The target's aim position (CActor's GetAimPosition, prediction in f1), false when it is gone.
  bool TargetAimPosition(CStateManager& mgr, CVector3f& out, float prediction = 0.f) const;
  // One arm's segment boxes against the target's bounds; on a hit, the arm swipe damage with the
  // given knock-back.
  void ArmSwipeSegment(CStateManager& mgr, const CSegId& a, const CSegId& b, const CSegId& c,
                       const CSegId& d, const CVector3f& knockback);
  // The exit host's Deactivate smoke, at the Ing's transform, plus the swarm block's own sound.
  void SpawnExitHostSmoke(CStateManager& mgr);
  // Turn horizontally to the target (the Ing turns to the lure safe zone when one is set; this
  // module has no lure safe zone).
  // fn_30_30E4: the guest's second manager (0xD64) takes the boost-ball vulnerability or the ing
  // spot's. fn_30_3190: the body manager (guest 0xD60) takes the
  // reflect vulnerability or the base one. fn_30_3068: enter or leave the boost puddle mode.
  void SetIngSpotManagerVulnerability(CStateManager& mgr, bool boost);
  void SetBodyManagerReflect(CStateManager& mgr, bool reflect);
  void SetBoostPuddleMode(bool enter);
  // Guest 0x11D0: the current phase, element mPhaseIndex (guest 0x11CC) of the data's three; the
  // phase advance in Think (fn_30_D94C, 0x0000DBC0) moves it with the damage taken.
  const CIngBoostBallGuardianPhase& CurrentPhase() const {
    return mPhaseIndex == 2 ? mBoost.mPhase2 : mPhaseIndex == 1 ? mBoost.mPhase1 : mBoost.mPhase0;
  }
  // fn_30_55D8: the ing-spot and evaporating forms' think (body, animation, state machine,
  // waypoints). fn_30_56E0: the touch box. fn_30_6108 / fn_30_65C8: the puddle effects and the
  // hit light (stand-ins).
  void UpdateIngSpotBody(float dt, CStateManager& mgr);
  void UpdateTouchBox();
  void UpdatePuddleEffects(float dt, CStateManager& mgr);
  void UpdateHitLight(float dt, CStateManager& mgr);
  // fn_30_4E90: the body manager (guest 0xD60), the seven joint spheres.
  void CreateCollisionActorManager(CStateManager& mgr);
  // fn_30_4A38: the guest's second manager (0xD64), one "PuddleCollisionActor" box at the "Root"
  // locator. fn_30_4924: a manager's collision actors take the health info and CPatterned's own
  // damage vulnerability.
  void CreateIngSpotManager(CStateManager& mgr);
  void SetupManagerActors(CStateManager& mgr, CCollisionActorManager* const& manager);
  // fn_30_648C: an "IngBlobEffect" collision tracker drawn after the guardian, emission 0.
  // fn_30_67B4: the "Ing Light" game light (black point light). fn_30_510C: leave the team.
  TUniqueId CreateBlobTracker(CStateManager& mgr, const TLockedToken< CGenDescription >& desc);
  void CreateHitLight(CStateManager& mgr);
  void LeaveTeam(CStateManager& mgr);
  // A collision actor that last touched the player deals contact damage.
  void CollisionActorHitPlayer(CStateManager& mgr, TUniqueId sender);
  // fn_30_479C: kSM_Damage. fn_30_4078: a weapon hit on a body (0xD60) collider. fn_30_44A4: a
  // weapon hit on the puddle (0xD64) collider, or a hit the boost ball relays. fn_30_4324:
  // kSM_ResistedDamage on the puddle collider. fn_30_4030: the damage that turns on puddle mode.
  // fn_30_3CB0: the ing-spot hit effect and sound.
  void ColliderDamage(CStateManager& mgr, TUniqueId sender);
  void BodyColliderDamage(CStateManager& mgr, TUniqueId sender);
  void PuddleColliderDamage(CStateManager& mgr, TUniqueId sender);
  void PuddleColliderResistedDamage(CStateManager& mgr, TUniqueId sender);
  void AccumulatePuddleDamage(CStateManager& mgr, float hp, float damage);
  void SpawnSpotHitEffect(CStateManager& mgr, float damage);

  // Host layout (retail's object is 0x11F0; CPatterned is 0x7C0). Guest offsets are the REL's,
  // from the constructor fn_30_EC6C - the full table is in the analysis note. The host layout is
  // its own: it holds the Ing half's members under the Ing REL's names, because this module is the
  // Ing REL with the boost half added (the guest offsets are shifted, the names are not). The
  // CHECK_SIZEOF below is the guest's size, which the non-host build keeps; the host's size is
  // its own and is not checked.
  CIngBoostBallGuardianData mData;                       // guest 0x7C0 (0x320)
  int mForm;                                             // guest 0xAE4, 2 (CIng: 0x9D4)
  int mDesiredForm;                                      // guest 0xAE8, -1 (CIng: 0x9D8)
  CPathFindSearch mPathFindSearch;                       // guest 0xAEC
  CPathFindPointSearch mPointPathFindSearch;             // guest 0xBD8
  // Owning pointers, as retail: the dtor (fn_30_E5EC) deletes them as members, 0xD64 then 0xD60
  // (0x0000E7E4/0x0000E7F8), after the boost data and the surface helper.
  rstl::single_ptr< CCollisionActorManager > mCollisionActorManager0; // guest 0xD60
  rstl::single_ptr< CCollisionActorManager > mCollisionActorManager1; // guest 0xD64
  CSurfaceAlignmentHelper mSurfaceAlignment;             // guest 0xD68
  CIngSpotPathFindNavigation mIngSpotPathFindNavigation; // guest 0xDC0
  CBoostBallGuardianData mBoost;                         // guest 0xDEC (0x110)
  // The Ing half's members, under the Ing REL's names (the guest offsets are in
  // docs/module-analysis/BoostGuardian.md section e; the host layout is its own).
  rstl::optional_object< CAABox > mTouchBox;            // CIng: 0xCC0
  TUniqueId mHostId;                                     // CIng: 0xCDC
  TUniqueId mPossessedEffectId;                          // CIng: 0xCDE
  rstl::optional_object< TLockedToken< CGenDescription > > mSwarmEffect;  // CIng: 0xCE0
  rstl::optional_object< TLockedToken< CGenDescription > > mSwarmEffect2; // CIng: 0xCF0
  TUniqueId mBlobId;                                     // CIng: 0xD5C
  TUniqueId mTeamId;                                     // CIng: 0xD6A
  TUniqueId mTargetId;                                   // CIng: 0xD6C
  TUniqueId mPrevTargetId;                               // CIng: 0xD6E
  TUniqueId mAttackPathResumeId;                         // CIng: 0xD70
  TUniqueId mCoverHintId;                                // CIng: 0xD72
  TUniqueId mLastCoverHintId;                            // CIng: 0xD74
  TUniqueId mD76Id;                                      // CIng: 0xD76
  CSfxHandle mSuckSfx;                                   // CIng: 0xD00
  rstl::optional_object< TLockedToken< CGenDescription > > mNormalHitEffect; // CIng: 0xD08
  rstl::optional_object< TLockedToken< CGenDescription > > mHeavyHitEffect;  // CIng: 0xD18
  rstl::optional_object< TLockedToken< CGenDescription > > mDeathEffect;    // CIng: 0xD28
  rstl::optional_object< TLockedToken< CGenDescription > > mMiniPortalEffect; // CIng: 0xD38
  rstl::optional_object< TLockedToken< CGenDescription > > mSplatEffect;   // CIng: 0xD48
  // Guest 0x1020/0x1030: boostIngSpotHitNormalDamage / boostIngSpotHitHeavyDamage (ctor 0x0000F46C,
  // 0x0000F5E8). mNormalHitEffect/mHeavyHitEffect are the guest's 0x1000/0x1010.
  rstl::optional_object< TLockedToken< CGenDescription > > mBoostNormalHitEffect;
  rstl::optional_object< TLockedToken< CGenDescription > > mBoostHeavyHitEffect;
  CSfxHandle mBodyProjectileSfx;                         // CIng: 0xD58
  CSfxHandle mLoopSfx[2];                                // CIng: 0xD60/0xD64
  CSegId mCollarSegId; // CIng: 0xD7A; IngBoostBallGuardian: 0x10AC (Collar_SDK, ctor 0x0000FFAC)
  CSegId mEyeSegId;                                      // CIng: 0xD7B
  CSegId mRightShoulderSegId;                            // CIng: 0xD7C
  CSegId mRightElbowSegId;                               // CIng: 0xD7D
  CSegId mRightForearmSegId;                             // CIng: 0xD7E
  CSegId mRightWristSegId;                               // CIng: 0xD7F
  CSegId mLeftShoulderSegId;                             // CIng: 0xD80
  CSegId mLeftElbowSegId;                                // CIng: 0xD81
  CSegId mLeftForearmSegId;                              // CIng: 0xD82
  CSegId mLeftWristSegId;                                // CIng: 0xD83
  CLineOfSightTracker mLineOfSight;                      // CIng: 0xD88
  CVector3f mTargetPos;                                  // CIng: 0xDC8
  CVector3f mSurfaceMoveDir;                             // CIng: 0xDE0
  CVector3f mPortalPlaneNormal;                          // CIng: 0xDF0
  float mPortalPlaneConstant;                            // CIng: 0xDFC
  int mSwipeArm;                                         // CIng: 0xDEC
  CVector3f mModelScale;                                 // host copy of the model data's scale
  int mMiniPortalCount;                                  // CIng: 0xE00
  CVector3f mMiniPortalPositions[3];                     // CIng: 0xE04
  int mMiniPortalIndex;                                  // CIng: 0xE28
  float mHeardShotTime;                                  // CIng: 0xE30
  float mUnderFireTime;                                  // CIng: 0xE34
  float mArmSwipeTime;                                   // CIng: 0xE38
  float mFormTime;                                       // CIng: 0xE3C
  float mBodyState5Time;                                 // CIng: 0xE40
  float mFrustrationTimer;                               // CIng: 0xE44
  float mBallCooldown;                                   // CIng: 0xE48
  float mHitReactionTime;                                // CIng: 0xE4C
  float mInitialSpeed; // guest 0x11A8: mSpeed at construction (ctor 0x0000FD64); Dead restores it
  float mLightBlend;                                     // guest 0x11C4 (CIng: 0xE50)
  float mDeadFade;                                       // CIng: 0xE54
  float mBodyProjectileTime;                             // CIng: 0xE58
  // The boost half's members (this module's own; the guest offsets are in
  // docs/module-analysis/BoostGuardian.md section e and in docs/module-analysis/BoostGuardian_fsm.tsv).
  bool mCollidedWithCrystal : 1;                         // guest 0x11EE bit 0
  bool mBoostPuddleForm : 1;                             // guest 0x11EE bit 3
  bool mShouldBoost : 1;                                 // guest 0x11EE bit 4
  bool mBoostOver : 1;                                   // guest 0x11EE bit 5
  bool mBoostModeRetreatActive : 1;                      // guest 0x11EE bit 7
  bool mCurrentlyInBoostPuddleForm : 1;                  // guest 0x11EF bit 7
  // Guest 0x11EE bit 2 (0x04): set with mShouldBoost/mBoostPuddleForm by fn_30_3068 (0x000030A4),
  // read by BecomeCorporeal's Update (0x0000B1A0) to pick generate type 2, cleared by its
  // Deactivate (0x0000B42C).
  bool mBoostPuddleGenerate : 1;                        // guest 0x11EE bit 2
  bool mReflectVulnerability : 1;                        // guest 0x11EE bit 1 (0x02), fn_30_3190
  bool mBoostPuddleAllowed : 1;                          // guest 0x11EF bit 5 (0x20), fn_30_3068
  // Guest 0x11EF bit 4 (0x10), ctor 1: while set a body hit may kill (fn_30_4078). Retail's
  // DoUserAnimEvent (fn_30_D4F8) clears it on kUE_Unknown37 and sets it on kUE_Unknown38.
  bool mBodyHitsLethal : 1; // IngBoostBallGuardian 0x11EF bit 4
  bool mBoost11EF_40 : 1;   // IngBoostBallGuardian 0x11EF bit 6: only the ctor writes it (0x0000FF20)
  TUniqueId mBoostChildId;                               // guest 0x1094: the second class's id
  TUniqueId mIngBlobTrackerId;                           // guest 0x1074: ingSpotBlobEffect
  TUniqueId mBoostBlobTrackerId;                         // guest 0x1076: boostIngSpotBlobEffect
  TUniqueId mHitLightId;                                 // guest 0x1084: "Ing Light"
  // Guest 0x109C: the ids connected with (kSS_Modify, kSM_Activate), gathered on kSM_Create
  // (0x0000E06C) and handed to the boost ball.
  rstl::vector< TUniqueId > mModifyTargets;
  CVector3f mBoostAimPos;                                // guest 0x1160 (fn_30_340C)
  CVector3f mBoostSpawnPos;                              // guest 0x116C (fn_30_324C)
  // guest 0x1178 (declared here so it is built after 0x116C and destroyed first, as retail): every weapon resisted; the boost puddle's colliders take it (fn_30_30E4), so
  // their hits arrive as kSM_ResistedDamage (fn_30_4324).
  CDamageVulnerability mVulnerability;
  TUniqueId mBoostNavPointId;                            // guest 0x1096: the retreat's first point
  TUniqueId mBoostNavPointId2;                           // guest 0x1098: the retreat's second point
  float mBoostTimer;                                     // guest 0x11DC
  float mBoost11D4;                                      // guest 0x11D4
  float mBoost11D8;                                      // guest 0x11D8
  float mBoost11E0;     // guest 0x11E0: bomb damage relayed by the ball (fn_30_44A4)
  float mBoostHitTimer; // guest 0x1080: skDamageHitTime on a resisted bomb hit; fn_30_CA18 reads it
  CVector3f mSplatNormal; // guest 0x1104: set by CollidedWith (fn_30_C850), read by fn_30_6980
  int mSafeZoneCount;   // guest 0x115C: XENZ/XEXZ count; read by CA18, C05C, C1B0 and PathFind
  float mBoostPuddleModeTimer;                           // guest 0x11E4: the phase's boostPuddleModeTime
  float mBoostRandomFloat;                               // guest 0x11E8
  int mPhaseIndex;                                       // guest 0x11CC: the current phase
  bool mAggressive : 1;                                  // CIng: 0xE5C bit 4; IngBoostBallGuardian 0x11EC bit 5
  bool xE5C_1 : 1;                                       // CIng: 0xE5C bit 1 (the arm swipe's live flag); IngBoostBallGuardian 0x11EC bit 2
  bool mFoundMovementPos : 1;                            // CIng: 0xE5C bit 3; IngBoostBallGuardian 0x11EC bit 4
  bool mRecalcPath : 1;                                  // CIng: 0xE5C bit 2; IngBoostBallGuardian 0x11EC bit 3
  bool mTakeOff : 1;                                     // CIng: 0xE5C bit 0; IngBoostBallGuardian 0x11EC bit 1
  // guest 0x11ED 0x40 (CIng: 0xE5D bit 5, there mStartAsIngSpot): ctor from the start-as-ing-spot
  // flag, set by BecomeIngSpot and kUE_Activate, cleared by BecomeCorporeal, the death fade and
  // kUE_Deactivate. Its only reader, fn_30_6108, keeps the blob tracker emitting while it is set.
  bool mIngSpotBlobVisible : 1;                          // IngBoostBallGuardian 0x11ED bit 6
  bool mMoving : 1;                                      // CIng: 0xE5D bit 1; IngBoostBallGuardian 0x11ED bit 2
  // CIng: 0xE5D bit 0. This module has no grapple and no guest bit for it: nothing writes it.
  bool mGrappled : 1;
  bool mProjectileFSMEntry : 1;                          // CIng: 0xE5E bit 7; IngBoostBallGuardian 0x11ED bit 1
  bool mSplat : 1; // guest 0x11ED bit 0 (CIng: 0xE5E bit 6): set by CollidedWith (fn_30_C850)
  bool mHurtfulZone : 1;                                 // CIng: 0xE5E bit 4; IngBoostBallGuardian 0x11EE bit 6
  bool mPortalPlane : 1;                                 // CIng: 0xE5D bit 6; IngBoostBallGuardian 0x11ED bit 7
  bool mRenderModel : 1;                                 // CIng: 0xE5D bit 7; IngBoostBallGuardian 0x11EC bit 0
  bool mForm9Render : 1;                                 // CIng: 0xE5D bit 4; IngBoostBallGuardian 0x11ED bit 5
  bool mFollowingAttackPath : 1;                         // CIng: 0xE5D bit 3; IngBoostBallGuardian 0x11ED bit 4
  bool mAlert : 1;                                       // CIng: 0xE5D bit 2; IngBoostBallGuardian 0x11ED bit 3
  bool mTaunt : 1;                                       // CIng: 0xE5C bit 6; IngBoostBallGuardian 0x11EC bit 7
  bool mBodyProjectileAllowed : 1;                       // CIng: 0xE5C bit 5; IngBoostBallGuardian 0x11EC bit 6
  // Host-only bookkeeping.
  const char* mLastLoggedState;                          // host: the last state logged
  float mStateTime;                                      // host: time in the current state
  bool mSuckTickLogged;                                  // host: SuckEnergy's first-frame trace
  CPFArea* mPathArea;                                    // host: the area both searches were given
  int mDebugPoll;                                        // host: the once-a-second position poll

#else
  uchar x7c0_[0x11f0 - 0x7c0];
#endif
public:
  // CEntity
  CEntity* TypesMatch(int typeId) const override;
};
CHECK_SIZEOF(CIngBoostBallGuardian, 0x11f0)

#endif
