#ifndef _CMINORING
#define _CMINORING

#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "REL/REL_Setup.h"

#ifdef TARGET_PC
#include "Kyoto/SObjectTag.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CSurfaceAlignmentHelper.hpp"
#include "MetroidPrime/Enemies/CAiKnockBackMgr.hpp"
#include "MetroidPrime/Enemies/CBouncyGrenade.hpp"
#include "MetroidPrime/Enemies/CIngSpotData.hpp"
#include "MetroidPrime/Enemies/CIngSpotPathFindNavigation.hpp"
#include "MetroidPrime/PathFinding/CPathFindPointSearch.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"
#include "Kyoto/Audio/CSfxHandle.hpp"
#include "MetroidPrime/CLineOfSightTracker.hpp"
#include "Kyoto/Animation/CSegId.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "Kyoto/TToken.hpp"
#include "rstl/optional_object.hpp"

class CGenDescription;

class CScriptAIHint;
class CPFArea;

// Host copy of the properties the MinorIng loader reads (retail: CMinorIngData, 0x158 bytes at
// 0x7c0, built by fn_44_9B4; the REL is module 44, `docs/module-analysis/IngFamily.md` section B).
// The guest offsets in the comments are the REL's own stores; the host layout is its own and does
// not need to match. The inglet's own data is modelled: its health at guest 0xb0, its grenade
// data at guest 0xd0 (fn_44_C44 copies the struct fn_44_FE8 builds) and the seven spawn values
// at guest 0x120..0x138.
struct CMinorIngData {
  CMinorIngData(CAssetId projectile, const CDamageInfo& projectileDamage,
                const CDamageInfo& ingSpotDamage, const CIngSpotData& ingSpot,
                float unknownA03E450C, float morphballPursuitDistance, float bombStunDuration,
                float ingSpotUnknown7569, float ingSpotSfxFallOff, float attackAngleLimit,
                float lineOfSightHeightOffset, float unknownD6C8EAC2, float unknown2A5449BA,
                float hearingRadius, float speedModifier, bool ingSpotAllowLockOn, uchar flags155,
                uchar flags156, float ingSpotMaxSpeed, float ingSpotMaxWallSpeed,
                float ingSpotBallPursuitSpeed, const CDamageVulnerability& ingSpotVulnerability,
                ushort ingSpotIdleSound, ushort ingSpotMoveSound, ushort ingSpotHitNormalSound,
                ushort ingSpotHitHeavySound, ushort ingSpotDeathSound,
                const CHealthInfo& ingletHealth, const CBouncyGrenadeData& grenadeData,
                float ingletMinLaunchSpeed, float ingletMaxLaunchSpeed, int ingletMinCount,
                int ingletMaxCount, float ingletMaxTurnAngle, float ingletSpawnDistance,
                float ingletExplodeDistance)
  : mProjectile(projectile)
  , mProjectileDamage(projectileDamage)
  , mUnknownA03E450C(unknownA03E450C)
  , mMorphballPursuitDistance(morphballPursuitDistance)
  , mIngSpotDamage(ingSpotDamage)
  , mBombStunDuration(bombStunDuration)
  , mIngSpotUnknown7569(ingSpotUnknown7569)
  , mIngSpotSfxFallOff(ingSpotSfxFallOff)
  , mIngSpot(ingSpot)
  , mAttackAngleLimit(attackAngleLimit)
  , mLineOfSightHeightOffset(lineOfSightHeightOffset)
  , mUnknownD6C8EAC2(unknownD6C8EAC2)
  , mUnknown2A5449BA(unknown2A5449BA)
  , mHearingRadius(hearingRadius)
  , mSpeedModifier(speedModifier)
  , mIngSpotAllowLockOn(ingSpotAllowLockOn)
  , mFlags155(flags155)
  , mFlags156(flags156)
  , mIngSpotMaxSpeed(ingSpotMaxSpeed)
  , mIngSpotMaxWallSpeed(ingSpotMaxWallSpeed)
  , mIngSpotBallPursuitSpeed(ingSpotBallPursuitSpeed)
  , mIngSpotVulnerability(ingSpotVulnerability)
  , mIngSpotIdleSound(ingSpotIdleSound)
  , mIngSpotMoveSound(ingSpotMoveSound)
  , mIngSpotHitNormalSound(ingSpotHitNormalSound)
  , mIngSpotHitHeavySound(ingSpotHitHeavySound)
  , mIngSpotDeathSound(ingSpotDeathSound)
  , mIngletHealth(ingletHealth)
  , mGrenadeData(grenadeData)
  , mIngletMinLaunchSpeed(ingletMinLaunchSpeed)
  , mIngletMaxLaunchSpeed(ingletMaxLaunchSpeed)
  , mIngletMinCount(ingletMinCount)
  , mIngletMaxCount(ingletMaxCount)
  , mIngletMaxTurnAngle(ingletMaxTurnAngle)
  , mIngletSpawnDistance(ingletSpawnDistance)
  , mIngletExplodeDistance(ingletExplodeDistance) {}

  CAssetId mProjectile;      // +0x00: the pair the actor's CProjectileInfo is built from
  CDamageInfo mProjectileDamage; // +0x04 (0x1c) from SLdrMinorIng.projectileDamage
  float mUnknownA03E450C;    // +0x20 (SLdrMinorIng.unknown_0xa03e450c)
  float mMorphballPursuitDistance; // +0x24 (SLdrMinorIngIngSpot.morphballPursuitDistance)
  CDamageInfo mIngSpotDamage;      // +0x28 (0x1c) from SLdrMinorIngIngSpot.damage
  float mBombStunDuration;         // +0x44 (SLdrMinorIngIngSpot.bombStunDuration)
  float mIngSpotUnknown7569;       // +0x48 (SLdrMinorIngIngSpot.unknown_0x7569fdba)
  float mIngSpotSfxFallOff;        // +0x4c (SLdrMinorIngIngSpot.sfxFallOff)
  CIngSpotData mIngSpot; // +0x50 (0x60): fn_44_D54 copies exactly the shape CIngSpotData has
  float mAttackAngleLimit;         // +0x13c (SLdrMinorIng.attackAngleLimit)
  float mLineOfSightHeightOffset;  // +0x140 (SLdrMinorIng.lineOfSightHeightOffset)
  float mUnknownD6C8EAC2;          // +0x144 (SLdrMinorIng.unknown_0xd6c8eac2)
  float mUnknown2A5449BA;          // +0x148 (SLdrMinorIng.unknown_0x2a5449ba)
  float mHearingRadius;            // +0x14c (SLdrMinorIng.hearingRadius)
  float mSpeedModifier;            // +0x150 (SLdrMinorIngIngSpot.speedModifier)
  // The seven bool properties land in three guest bytes. The constructor's argument order (which
  // is what decides the pairing) does not follow the ids in SLdrMinorIng's declaration order, so
  // which property is which bit is not established: held as three opaque values until a milestone
  // reads them.
  bool mIngSpotAllowLockOn; // +0x154 bit 7 (comes from the unknown_0x3da35851 block)
  uchar mFlags155;          // +0x155
  uchar mFlags156;          // +0x156 bits 2..7
  // Host copies of the CIngSpotData fields the behaviour reads (CIngSpotData keeps them private;
  // retail reads them in place at guest 0x820/0x824/0x828/0x834/0x864..0x86c).
  float mIngSpotMaxSpeed;                    // guest 0x820
  float mIngSpotMaxWallSpeed;                // guest 0x824
  float mIngSpotBallPursuitSpeed;            // guest 0x828
  CDamageVulnerability mIngSpotVulnerability; // guest 0x834
  ushort mIngSpotIdleSound;                  // guest 0x864
  ushort mIngSpotMoveSound;                  // guest 0x866
  ushort mIngSpotHitNormalSound;             // guest 0x868
  ushort mIngSpotHitHeavySound;              // guest 0x86a
  ushort mIngSpotDeathSound;                 // guest 0x86c
  // The inglet's own health (the retail's data 0xb0, fn_44_9B4 copies it there with fn_44_D00).
  // It is the health of the unknown_0x3da35851 block (fn_44_1CE0 reads it first); the inglet the
  // massive death spawns takes it.
  CHealthInfo mIngletHealth;                  // guest 0xb0
  // The inglet's grenade data (the retail's data 0xd0, 0x50 bytes): fn_44_FE8 builds the
  // bouncing-projectile struct out of the unknown_0x3da35851 block and fn_44_C44 copies it here.
  // The inglet the massive death spawns is a CBouncyGrenade, which takes this struct.
  CBouncyGrenadeData mGrenadeData;            // guest 0xd0
  // The inglet's spawn values (the retail's data 0x120..0x138, fn_44_9B4's stores), all out of
  // the unknown_0x3da35851 block:
  // - the launch speed range (fn_44_7058 picks the inglet's velocity out of it),
  // - the number of inglets the massive death spawns (the minimum and the maximum),
  // - the angle the inglet may turn in,
  // - the distance at which the inglet count falls to zero (the retail divides the player's
  //   distance by it),
  // - the distance at which the inglet explodes around the player.
  float mIngletMinLaunchSpeed;                // guest 0x120 (the block's minLaunchSpeed)
  float mIngletMaxLaunchSpeed;                // guest 0x124 (the block's maxLaunchSpeed)
  int mIngletMinCount;                        // guest 0x128 (the block's minGeneration)
  int mIngletMaxCount;                        // guest 0x12c (the block's maxGeneration)
  float mIngletMaxTurnAngle;                  // guest 0x130 (the block's maxTurnAngle)
  float mIngletSpawnDistance;                 // guest 0x134 (the block's unknown_0xfbf8ea0a)
  float mIngletExplodeDistance;               // guest 0x138 (the block's unknown_0x47f99fbc)
};
#endif

// The REL's CMinorIng (module 44). It derives from CPatterned directly, not from CIng [proven:
// its ctor fn_44_9D58 calls the CPatterned constructor with EPatternedAI 0x4b].
class REL_EXPORT CMinorIng : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
#ifdef TARGET_PC
  CMinorIng(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
            const CTransform4f& xf, const CModelData& modelData, const CPatternedInfo& pInfo,
            const CActorParameters& actParms, const CMinorIngData& data);
  ~CMinorIng() override;

  // CPatterned
  using CPatterned::GetSearchPath;
  CProjectileInfo* ProjectileInfo() override;
  CPathFindSearch* GetSearchPath() override;
  CPathFindPointSearch* GetPointSearchPath() override;

  void PreThink(float dt, CStateManager& mgr) override;
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  CVector3f GetAimPosition(const CStateManager& mgr, float dt) const override;
  const CHealthInfo* GetHealthInfo() const override;
  void Render(const CStateManager& mgr) const override;
  void Touch(CActor& actor, CStateManager& mgr) override;
  void Death(CStateManager& mgr, const CVector3f& direction, EScriptObjectState state) override;
  void KnockBack(CStateManager& mgr, const CKnockBackInfo& info) override;
  void SetupStateMachine(CStateManager& mgr) override;
  void MassiveDeath(CStateManager& mgr) override;
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;
  using CPatterned::GetDamageVulnerability;
  const CDamageVulnerability* GetDamageVulnerability() const override;
  rstl::optional_object< CAABox > GetTouchBounds() const override;
  CTransform4f GetPrimitiveTransform() const override;

  // CAi
  bool IsListening() const override;
  bool Listen(CStateManager& mgr, const CVector3f& position, EListenNoiseType type) override;

  // CEntity
  CEntity* TypesMatch(int typeId) const override;

  // Triggers (retail table data_1A8, in its order).
  bool PathShagged(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldMoveUnderBall(CStateManager& mgr, const CTriggerData& data) const;
  bool HasPatrolPath(CStateManager& mgr, const CTriggerData& data) const;
  bool PlayerLeashReached(CStateManager& mgr, const CTriggerData& data) const;
  bool LeavePatrol(CStateManager& mgr, const CTriggerData& data) const;
  bool LineOfSight(CStateManager& mgr, const CTriggerData& data) const;
  bool InMaxRange(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldFire(CStateManager& mgr, const CTriggerData& data) const;
  bool PuddleHitByWeapon(CStateManager& mgr, const CTriggerData& data) const;
  bool CancelStun(CStateManager& mgr, const CTriggerData& data) const;
  bool IsBeingKnockedback(CStateManager& mgr, const CTriggerData& data) const;
  bool TargetInSafeZone(CStateManager& mgr, const CTriggerData& data) const;
  bool HasCoverPoint(CStateManager& mgr, const CTriggerData& data) const;
  bool PointPathOver(CStateManager& mgr, const CTriggerData& data) const;
  bool FoundPointPath(CStateManager& mgr, const CTriggerData& data) const;
  bool IsPointPathFinding(CStateManager& mgr, const CTriggerData& data) const;
  bool StunOver(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldPathFind(CStateManager& mgr, const CTriggerData& data) const;
  bool CancelAnim(CStateManager& mgr, const CTriggerData& data) const;
  bool HasAttackPattern(CStateManager& mgr, const CTriggerData& data) const;
  bool AttackPatternOver(CStateManager& mgr, const CTriggerData& data) const;
  bool IsFollowingAttackPath(CStateManager& mgr, const CTriggerData& data) const;
  bool AllowAttackPatternProjectile(CStateManager& mgr, const CTriggerData& data) const;
  bool AllowAttackPatternStun(CStateManager& mgr, const CTriggerData& data) const;
  bool RecalculatePath(CStateManager& mgr, const CTriggerData& data) const;
  bool InsideSafeZone(CStateManager& mgr, const CTriggerData& data) const;
  bool IsMoveAwayFromSafeZoneOver(CStateManager& mgr, const CTriggerData& data) const;
  bool FoundMovementPos(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldEvaporate(CStateManager& mgr, const CTriggerData& data) const;
  bool IsInCorporealForm(CStateManager& mgr, const CTriggerData& data) const;
  bool StayOnPointPathFinding(CStateManager& mgr, const CTriggerData& data) const;
  bool AllowProjectileDuringStayOnPointPathFinding(CStateManager& mgr,
                                                   const CTriggerData& data) const;
  bool StateOver(CStateManager& mgr, const CTriggerData& data) const;
  bool IsLuredBySafeZone(CStateManager& mgr, const CTriggerData& data) const;

  // States (retail table data_47C).
  void PathFind(CStateManager& mgr, EStateMsg msg, float dt);
  void PointPathFind(CStateManager& mgr, EStateMsg msg, float dt);
  void Stunned(CStateManager& mgr, EStateMsg msg, float dt);
  void ProjectileAttack(CStateManager& mgr, EStateMsg msg, float dt);
  void Patrol(CStateManager& mgr, EStateMsg msg, float dt);
  void Dead(CStateManager& mgr, EStateMsg msg, float dt);
  void SetWallPointCoverDest(CStateManager& mgr, EStateMsg msg, float dt);
  void SetCoverDest(CStateManager& mgr, EStateMsg msg, float dt);
  void IntoPuddleForm(CStateManager& mgr, EStateMsg msg, float dt);
  void IntoCorporealForm(CStateManager& mgr, EStateMsg msg, float dt);
  void Idle(CStateManager& mgr, EStateMsg msg, float dt);
  void FollowAttackPattern(CStateManager& mgr, EStateMsg msg, float dt);
  void MoveAwayFromSafeZone(CStateManager& mgr, EStateMsg msg, float dt);
  void AlwaysPointPathFind(CStateManager& mgr, EStateMsg msg, float dt);
  void WaitForFSMTransition(CStateManager& mgr, EStateMsg msg, float dt);

  // Code functions (retail table data_5A8).
  void SelectTarget(CStateManager& mgr, float arg);
  void FindCoverPoint(CStateManager& mgr, float arg);
  void SetPointCoverDest(CStateManager& mgr, float arg);
  void SetLuredDest(CStateManager& mgr, float arg);
  void SetLuredPointDest(CStateManager& mgr, float arg);
#endif

private:
#ifdef TARGET_PC
  // Guest form at 0xcc8: 0 corporeal, 1 puddle (ing spot), 2 into corporeal, 3 into puddle.
  enum EForm { kF_None = -1, kF_Corporeal, kF_Puddle, kF_IntoCorporeal, kF_IntoPuddle };

  void EnsureBodyActive(CStateManager& mgr);
  void LogState(const char* name);
  CVector3f RaisedPoint(const CVector3f& pos) const;
  bool ProjectOntoMesh(CVector3f& pos);
  bool IsTargetInRange(CStateManager& mgr, const CVector3f& pos) const;
  bool IsTargetInAttackAngle(CStateManager& mgr) const;
  bool IsMoveClear(CStateManager& mgr, const CVector3f& offset, bool checkStatic) const;
  bool TargetAimPosition(CStateManager& mgr, CVector3f& out) const;
  CScriptAIHint* GetCoverHint(CStateManager& mgr, TUniqueId id) const;
  void ClaimCoverHint(CScriptAIHint* hint, TUniqueId& id);
  void ReleaseCoverHint(CStateManager& mgr, TUniqueId& id, bool keepTimer);
  void ResetAttackTimer(CStateManager& mgr);
  void PlaySfx(int idx, bool start);
  void UpdateLoopSfx();
  void StopSfx();
  void UpdateTimers(float dt);
  void UpdateLocomotionType();
  void SetForm(CStateManager& mgr, int form, bool setLocomotion);
  void SnapToFloor(CStateManager& mgr);
  void RayDown(CVector3f& out, CStateManager& mgr) const;
  bool UpdateTargetPos(CStateManager& mgr);
  void UpdateBallPursuit(CStateManager& mgr);
  void UpdateValidTarget(CStateManager& mgr);
  void UpdateSafeZone(CStateManager& mgr);
  void PickSafeZone(CStateManager& mgr, const CVector3f& pos);
  void JoinTeam(CStateManager& mgr);
  void LeaveTeam(CStateManager& mgr);
  void EndAttack(CStateManager& mgr, bool projectile);
  void ApplySeparation(CStateManager& mgr);
  void MoveAlongPath(CStateManager& mgr, float dt);
  CTransform4f PrimitiveTransform(bool raised) const;
  void CreateCollisionActor(CStateManager& mgr);
  void SetCollisionActorActive(CStateManager& mgr, bool active);
  void UpdateCollisionActor(CStateManager& mgr, float dt);
  void CreateBlobEffect(CStateManager& mgr, const TLockedToken< CGenDescription >& desc);
  void SetBlobEffectActive(CStateManager& mgr, bool active);
  void UpdateBlobEffect(CStateManager& mgr);
  void UpdateTouchBounds();
  void PuddleDamage(CStateManager& mgr, TUniqueId sender);
  void CollisionActorDamage(CStateManager& mgr, TUniqueId sender);
  bool DamageSource(CStateManager& mgr, TUniqueId touched, TUniqueId& attacker,
                    CVector3f& direction, rstl::optional_object< CDamageInfo >& damage) const;
  void MoveOnSurface(float speed, float dt, const CVector3f& move);
  // Host layout (retail's CMinorIng is 0xd40, CHECK_SIZEOF below; CPatterned is 0x7c0). Guest
  // offsets are the REL's, from the constructor fn_44_9D58 and the two accessors fn_44_0
  // (this+0x960 = GetSearchPath) and fn_44_8 (this+0xa4c = GetPointSearchPath).
  CMinorIngData mData;                        // 0x7c0 (0x158)
  CProjectileInfo mProjectileInfo;            // 0x918 (0x28)
  rstl::optional_object< TLockedToken< CGenDescription > > mNormalHitEffect; // 0x940
  rstl::optional_object< TLockedToken< CGenDescription > > mHeavyHitEffect;  // 0x950
  CPathFindSearch mPathFindSearch;            // 0x960 (0xec)
  CPathFindPointSearch mPointPathFindSearch;  // 0xa4c (0x188)
  CSurfaceAlignmentHelper mSurfaceAlignment;  // 0xbd8 (0x58)
  CIngSpotPathFindNavigation mIngSpotPathFindNavigation; // 0xc30 (0x10)
  CVector3f mSurfaceMoveDir;                  // 0xc40
  rstl::optional_object< CAABox > mPuddleTouchBounds; // 0xc4c (fn_44_3E8C)
  rstl::optional_object< CAABox > mBodyTouchBounds;   // 0xc68 (fn_44_3E8C)
  CLineOfSightTracker mLineOfSight;          // 0xc88 (retail CVisualKnowledge(uid, seg, 0.1, 0.05))
  int mForm;                                  // 0xcc8 EForm
  TUniqueId mTeamId;                          // 0xccc
  TUniqueId mTargetId;                        // 0xcce
  TUniqueId mCoverHintId;                     // 0xcd0
  TUniqueId mLastCoverId;                     // 0xcd2
  TUniqueId mAttackPathLastId;                // 0xcd4 (FollowAttackPattern: the waypoint nav's last)
  TUniqueId mAttackPathId;                    // 0xcd6 (FollowAttackPattern: where to resume)
  TUniqueId mSafeZoneEnterId;                 // 0xcd8 (fn_44_9150: the zone the XENZ message names)
  TUniqueId mSafeZoneId;                      // 0xcda (fn_44_36FC/fn_44_3780: the zone that lured it)
  TUniqueId mBlobEffectId;                    // 0xcdc CCollisionTracker (fn_44_48F0)
  TUniqueId mCollisionActorId;                // 0xcde CCollisionActor (fn_44_319C)
  // The retail's container at 0xce0: fn_44_A5D0 resizes it to two zero ints in the constructor
  // (asm 0xA5B4) and nothing ever reads them.
  int mUnusedZeros[2];                        // 0xce0
  CSfxHandle mLoopSfx[2];                     // 0xce4/0xce8: idle, move
  CVector3f mTargetPos;                       // 0xcec
  CVector3f mModelScale;                      // 0xcf8
  float mAttackTimer;                         // 0xd04
  float mLeashTimer;                          // 0xd08
  float mStunTimer;                           // 0xd0c
  float mPuddleHitTimer;                      // 0xd10
  float mHpTracker;                           // 0xd14
  float mKnockDamage;                         // 0xd18
  float mTimerD1C;                            // 0xd1c
  float mMoveAwayTimer;                       // 0xd20
  int mSafeZoneEnterCount;                    // 0xd28
  float mEvaporate;                           // 0xd34
  float mDamageFxTimer;                       // 0xd38
  int mIntoCorporealAnim;                     // 0xd2c ("Into Corporeal Form")
  int mIntoStunnedAnim;                       // 0xd30 ("Into Stunned")
  CSegId mLockOnLocator;                      // 0xd3c ("lockon_target_SDK")
  const char* mLastLoggedState;               // host: MP2_DEBUG_MINORING
  CPFArea* mPathArea;                         // host: the area both searches were given on XALD
  // The retail's two flag bytes, 0xd3d and 0xd3e. The guest packs them most significant bit
  // first; the declarations below are in the guest's least-significant-bit-first order, so the
  // little-endian compiler (which allocates a bitfield run from the LSB) gives every flag the
  // retail's own bit. The positions are spelled out so `tools/asmcheck.py` can check the masks
  // against the asm. The retail clears them in the opposite order, most significant bit first
  // (asm 0xA284-0xA2DC and asm 0xA2E4-0xA330); the constructor's init list follows it.
  bool mAttackPatternProjectile : 1; // 0xd3d 0x01 (the waypoint message sets it, asm 0x96D8)
  bool mFollowingAttackPath : 1;      // 0xd3d 0x02 (fn_44_6210 reads it)
  bool mIdle : 1;                     // 0xd3d 0x04 (fn_44_8128 reads it)
  bool mRecalculatePath : 1;          // 0xd3d 0x08 (fn_44_6228 reads it)
  bool mFoundMovementPos : 1;         // 0xd3d 0x10 (fn_44_3430 reads it)
  bool mPointPathFinding : 1;         // 0xd3d 0x20 (fn_44_4A68 reads it)
  bool mPathFindActive : 1;           // 0xd3d 0x40 (fn_44_8C1C reads it)
  bool mHasPatrolPath : 1;            // 0xd3d 0x80 (fn_44_6178 reads it, asm 0x928C sets it)
  bool : 1;                           // 0xd3e 0x01: the retail's byte has no flag here (the
                                      // retail's zeroed heap leaves it 0; nothing reads it)
  bool mShouldEvaporate : 1;          // 0xd3e 0x02 (fn_44_5D28 reads it)
  bool mInsideSafeZone : 1;           // 0xd3e 0x04 (fn_44_621C reads it, asm 0x9640 sets it)
  bool mShouldMoveUnderBall : 1;      // 0xd3e 0x08 (fn_44_5D34 reads it)
  bool mTouched : 1;                  // 0xd3e 0x10 (fn_44_424C sets it, asm 0x43B8)
  bool mKnockAnimCancel : 1;          // 0xd3e 0x20 (fn_44_3DE0 reads it)
  bool mCancelStun : 1;               // 0xd3e 0x40 (fn_44_5D58 reads it)
  bool mPuddleHitByWeapon : 1;        // 0xd3e 0x80 (fn_44_5D64 reads it, asm 0x94F4 sets it)
#else
  uchar x7c0_[0x158]; // CMinorIngData
  uchar x918_[0x48];  // CProjectileInfo and the three locked tokens
  uchar x960_[0x3e0]; // path find searches, surface alignment, nav, line of sight, flags
#endif
};
CHECK_SIZEOF(CMinorIng, 0xd48)

#endif
