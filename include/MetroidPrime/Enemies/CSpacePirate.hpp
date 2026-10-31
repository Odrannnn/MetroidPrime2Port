#ifndef _CSPACEPIRATE
#define _CSPACEPIRATE

#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "REL/REL_Setup.h"

#ifdef TARGET_PC
#include "MetroidPrime/CBoneTracking.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CIkChain.hpp"
#include "MetroidPrime/Enemies/CBouncyGrenade.hpp"
#include "MetroidPrime/Enemies/CBurstFire.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"
#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Math/CPlane.hpp"
#include "rstl/list.hpp"
#include "rstl/optional_object.hpp"
#include "rstl/single_ptr.hpp"

class CPirateRagDoll;

// Host copy of the properties the SpacePirate loader reads (retail: CSpacePirateData, 0x134 bytes
// at 0x7c0, built by fn_72_10A10). Only the fields milestone 1a keeps are held; the retail offset
// inside the 0x134 block is noted on each.
struct CSpacePirateData {
  float mAggressiveness;       // +0x00
  float mCoverCheck;           // +0x04
  float mSearchRadius;         // +0x08
  float mFallBackCheck;        // +0x0c
  float mFallBackRadius;       // +0x10
  float mHearingRadius;        // +0x14
  uint mFlags;                 // +0x18
  bool mUnknownCe670970;       // +0x1c
  CAssetId mProjectile;        // +0x20
  CDamageInfo mProjectileDamage; // +0x24
  ushort mProjectileSfx;       // +0x40
  CDamageInfo mBladeDamage;    // +0x44
  float mKneelAttackChance;    // +0x60
  CAssetId mKneelAttackShot;   // +0x64
  CDamageInfo mKneelAttackDamage; // +0x68
  float mDodgeCheck;           // +0x84
  ushort mImpactSfx;           // +0x88
  float mIntraBurstTime;       // +0x8c
  float mIntraBurstVariation;  // +0x90
  float mIngIntraBurstTime;    // +0x94 unknown_0x5080162a (UpdateAttacks 0x2D94, Ing-possessed)
  float mIngIntraBurstVariation; // +0x98 unknown_0xc78b40e0 (UpdateAttacks 0x2D8C, Ing-possessed)
  ushort mAlertSfx;            // +0x9c
  float mGunTrackDelay;        // +0xa0
  uint mFirstBurstCount;       // +0xa4
  float mCloakOpacity;         // +0xa8
  float mMaxCloakOpacity;      // +0xac
  float mBreakDodgeMinTime;    // +0xb0
  float mBreakDodgeMaxTime;    // +0xb4
  ushort mHurledSfx;           // +0xb8
  ushort mDeathSfx;            // +0xba
  float mAvoidDistance;        // +0xc0
  // Launcher block, retail +0xc8..0x134 (built by fn_72_10C64; the loader's fn_72_140 fills it
  // from the SLdr weapon-data property 0xdc89cc3c). The 0x50-byte bouncy-grenade data stays flat
  // so the struct remains trivially copyable; BuildGrenadeData() assembles retail's
  // CBouncyGrenadeData - the object at this+0x890 that 58D8/4174 hand to CBouncyGrenade.
  uint mEquippedWeapon;          // +0xc8 (block +0x00) weaponData.equippedWeapon
  CAssetId mGrenadeLauncher;     // +0xcc (block +0x04) weaponData.grenadeLauncher
  float mGrenadeMass;            // +0xd0 (grenade data +0x00) weaponData.grenadeMass
  float mGrenadeSpeed;           // +0xd4 (grenade data +0x04) weaponData.unknown_0xed086ce0
  CDamageInfo mGrenadeDamage;    // +0xd8 (grenade data +0x08) weaponData.grenadeDamage
  uint mGrenadeNumBounces;       // +0xf4 (grenade data +0x24) grenadeNumBounces; fn_72_4030 reads it
  CAssetId mGrenadeExplosion;    // +0xf8 (grenade data +0x28/+0x2c: retail passes it as both gen ids)
  CAssetId mGrenadeTrail;        // +0x100 (grenade data +0x30) grenadeTrail
  CAssetId mGrenadeEffect;       // +0x104 (grenade data +0x34) grenadeEffect
  ushort mGrenadeBounceSfx;      // +0x108 (grenade data +0x38) sound_GrenadeBounce
  ushort mGrenadeExplodeSfx;     // +0x10a (grenade data +0x3a) sound_GrenadeExplode
  float mGrenadeSfxFalloff;      // +0x10c (grenade data +0x3c) constant 0.1 (rodata B3C, fn_72_140 0x880)
  float mGrenadeSfxMaxDist;      // +0x110 (grenade data +0x40) constant 150 (rodata B40, fn_72_140 0x874)
  float mGrenadeX44;             // +0x114 (grenade data +0x44) constant 0.1 (rodata B3C)
  float mGrenadeX48;             // +0x118 (grenade data +0x48) constant 150 (rodata B40)
  bool mGrenadeX4c;              // +0x11c (grenade data +0x4c) constant true
  int mGrenadeCount;             // +0x120 (block +0x58) weaponData.unknown_0xa95a025b; 58D8's Range cap
  float mGrenadeMinLaunchSpeed;  // +0x124 (block +0x5c) grenadeMinLaunchSpeed; 4174 seeds the solve
  float mGrenadeMaxLaunchSpeed;  // +0x128 (block +0x60) grenadeMaxLaunchSpeed
  float mGrenadeMinAttackDist;   // +0x12c (block +0x64) grenadeMinAttackDist
  float mGrenadeMaxAttackDist;   // +0x130 (block +0x68) grenadeMaxAttackDist

  CBouncyGrenadeData BuildGrenadeData() const {
    return CBouncyGrenadeData(
        mGrenadeMass, mGrenadeSpeed, mGrenadeDamage, mGrenadeNumBounces, mGrenadeExplosion,
        mGrenadeExplosion, mGrenadeTrail, mGrenadeEffect, mGrenadeBounceSfx, mGrenadeExplodeSfx,
        mGrenadeSfxFalloff, mGrenadeSfxMaxDist, mGrenadeX44, mGrenadeX48, mGrenadeX4c);
  }
};
#endif

class REL_EXPORT CSpacePirate : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
#ifdef TARGET_PC
  CSpacePirate(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
               const CTransform4f& xf, const CModelData& modelData,
               const CActorParameters& actParms, const CPatternedInfo& pInfo,
               const CSpacePirateData& data);
  ~CSpacePirate() override;

  // The module's table (SSpacePirate_FuncPtrs) points at these; AttachActorToPirate and
  // DetachActorFromPirate are the proxies that call through the table, so the bodies live here.
  bool PortAttachActor(TUniqueId id);
  void PortDetachActor();

  void PreThink(float dt, CStateManager& mgr) override;
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;
  void PreRender(CStateManager& mgr) override;
  void Render(const CStateManager& mgr) const override;
  void Touch(CActor& actor, CStateManager& mgr) override;
  void Death(CStateManager& mgr, const CVector3f& direction, EScriptObjectState state) override;
  void KnockBack(CStateManager& mgr, const CKnockBackInfo& info) override;
  void SetupStateMachine(CStateManager& mgr) override;
  // @ fn_72_18
  // arms and the run: docs/module-analysis/SpacePirate_helpers.md (section 3).
  TUniqueId GetAttackTarget() const override { return mAttackTarget; }
  void SetAttackTarget(CStateManager& mgr, TUniqueId target) override;

  // CActor / CAi / CPatterned overrides the retail vtable (82 slots) replaces. The trivial ones
  // are real; the rest forward to the base behind a StandIn until a later milestone ports them.
  void PreRenderAllViewports(CStateManager& mgr) override;
  const CHealthInfo* GetHealthInfo() const override;
  const CDamageVulnerability* GetDamageVulnerability() const override;
  const CDamageVulnerability* GetDamageVulnerability(const CVector3f& position,
                                                     const CVector3f& direction,
                                                     const CDamageInfo& damage) const override;
  rstl::optional_object< CAABox > GetTouchBounds() const override;
  CAABox GetSortingBounds(const CStateManager& mgr) const override;
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;
  void SpawnGrenade(CStateManager& mgr); // fn_72_4174, called from DoUserAnimEvent's kUE_Projectile
  bool CanBeShot(const CStateManager&, int) override { return true; } // fn_72_5C
  // @ fn_72_10 (0x8): `li r3, 1` (0x10). Branchless; the 2026-10-21 gdb run saw 400+ calls.
  bool IsListening() const override { return true; }
  bool Listen(CStateManager& mgr, const CVector3f& position, EListenNoiseType type) override;
  CVector3f GetOrigin(const CStateManager& mgr, const CTeamAiRole& role,
                      const CVector3f& aimPos) const override;
  uchar GetModelAlphau8(const CStateManager& mgr) const override;
  CProjectileInfo* ProjectileInfo() override; // fn_72_4F0C
  // @ fn_72_8 (0x8): `addi r3, r3, 0x920` (0x8), this + 0x920. Branchless; 400+ calls in the gdb run.
  CPathFindSearch* GetSearchPath() override { return &mPathFindSearch; }
  CPathFindPointSearch* GetPointSearchPath() override { return nullptr; } // fn_72_40
  // fn_72_17A0 (0x28): the ragdoll (0xb30) is returned only while it is live: the ragdoll's flag
  // byte 0x70 has to carry 0x40 (asm 0x17B0-0x17B4) and 0x80 (asm 0x17B8-0x17BC), else null
  // (asm 0x17C0).  The body is in PortCSpacePirateHelpers.cpp.
  CRagDoll* GetRagDoll() const override;
  // @ fn_72_24
  // arms and the run: docs/module-analysis/SpacePirate_helpers.md (section 3).
  float GetGravityConstant() const override { return 50.f; } // fn_72_24
  // @ fn_72_0
  // arms and the run: docs/module-analysis/SpacePirate_helpers.md (section 3).
  bool IsScanVisorSelfRender() const override { return true; } // fn_72_0
  CAABox GetScanVisorRenderBounds(const CStateManager& mgr) const override;
  void ScanVisorRender(const CStateManager& mgr, const CTransform4f& xf,
                       const CModelFlags& flags) const override;
  bool TryToBeCaptured(CStateManager& mgr) override;

  // The 39 triggers and 37 states of the retail table (fn_72_10E50), in table order. The
  // comment on each is its retail function. Names that CPatterned also declares hide it.
  // Triggers
  bool Stuck(CStateManager& mgr, const CTriggerData& data) const;             // C488
  bool PatternShagged(CStateManager& mgr, const CTriggerData& data) const;    // C468
  bool HearShot(CStateManager& mgr, const CTriggerData& data) const;          // BC78
  bool HearPlayer(CStateManager& mgr, const CTriggerData& data) const;        // BBC0
  bool AggressionCheck(CStateManager& mgr, const CTriggerData& data) const;   // ADC0
  bool CoverCheck(CStateManager& mgr, const CTriggerData& data) const;        // ADB4
  bool CoverFind(CStateManager& mgr, const CTriggerData& data) const;         // ABC0
  bool CoverBlown(CStateManager& mgr, const CTriggerData& data) const;        // AA64
  bool CoverNearlyBlown(CStateManager& mgr, const CTriggerData& data) const;  // A988
  bool CoveringFire(CStateManager& mgr, const CTriggerData& data) const;      // A8D0
  bool ShouldAttack(CStateManager& mgr, const CTriggerData& data) const;      // A71C
  bool LineOfSight(CStateManager& mgr, const CTriggerData& data) const;       // 9378
  bool PatternOver(CStateManager& mgr, const CTriggerData& data) const;       // BC94
  bool SpotPlayer(CStateManager& mgr, const CTriggerData& data) const;        // 938C
  bool ShouldDodge(CStateManager& mgr, const CTriggerData& data) const;       // 82D0
  bool ShouldRetreat(CStateManager& mgr, const CTriggerData& data) const;     // 7C28
  bool InRange(CStateManager& mgr, const CTriggerData& data) const;           // 8A84
  bool ShouldCrouch(CStateManager& mgr, const CTriggerData& data) const;      // 99A4
  bool ShouldMove(CStateManager& mgr, const CTriggerData& data) const;        // 7BD4
  bool ShotAt(CStateManager& mgr, const CTriggerData& data) const;            // 7BA0
  bool Attacked(CStateManager& mgr, const CTriggerData& data) const;          // 7B6C
  bool HasTargetingPoint(CStateManager& mgr, const CTriggerData& data) const; // 7970
  bool ShouldWallHang(CStateManager& mgr, const CTriggerData& data) const;    // 767C
  bool AnimOver(CStateManager& mgr, const CTriggerData& data) const;          // 689C
  bool ShouldStrafe(CStateManager& mgr, const CTriggerData& data) const;      // 960C
  bool ShouldSpecialAttack(CStateManager& mgr, const CTriggerData& data) const; // 638C
  bool StartAttack(CStateManager& mgr, const CTriggerData& data) const;       // 6334
  bool BreakAttack(CStateManager& mgr, const CTriggerData& data) const;       // 6328
  bool LostInterest(CStateManager& mgr, const CTriggerData& data) const;      // 635C
  bool BounceFind(CStateManager& mgr, const CTriggerData& data) const;        // 602C
  bool OffLine(CStateManager& mgr, const CTriggerData& data) const;           // 55B8
  bool Landed(CStateManager& mgr, const CTriggerData& data) const;            // 558C
  bool ShouldJumpBack(CStateManager& mgr, const CTriggerData& data) const;    // 686C
  bool Leash(CStateManager& mgr, const CTriggerData& data) const;             // 52F8
  bool HasAttackPattern(CStateManager& mgr, const CTriggerData& data) const;  // 7810
  bool IsAmbushing(CStateManager& mgr, const CTriggerData& data) const;       // 7804
  bool ShouldWarpIn(CStateManager& mgr, const CTriggerData& data) const;      // 77D0
  bool ShouldLaunchGrenade(CStateManager& mgr, const CTriggerData& data) const; // 779C
  bool InProjectileRange(CStateManager& mgr, const CTriggerData& data) const; // 76D0
  // States (Patrol, Dead and PathFind hide CPatterned's)
  void Ambushing(CStateManager& mgr, EStateMsg msg, float dt);     // 7630
  void WarpIn(CStateManager& mgr, EStateMsg msg, float dt);        // 7078
  void WarpOut(CStateManager& mgr, EStateMsg msg, float dt);       // 6E6C
  void PostWarpOut(CStateManager& mgr, EStateMsg msg, float dt);   // 6DA4
  void Attack(CStateManager& mgr, EStateMsg msg, float dt);        // 9F1C
  void Crouch(CStateManager& mgr, EStateMsg msg, float dt);        // 98D8
  void CoverAttack(CStateManager& mgr, EStateMsg msg, float dt);   // A310
  void Halt(CStateManager& mgr, EStateMsg msg, float dt);          // BBB0
  void Run(CStateManager& mgr, EStateMsg msg, float dt);           // BBA0
  void PathFind(CStateManager& mgr, EStateMsg msg, float dt);      // 8A90
  void Patrol(CStateManager& mgr, EStateMsg msg, float dt);        // BFCC
  void TargetPatrol(CStateManager& mgr, EStateMsg msg, float dt);  // BCB0
  void Shuffle(CStateManager& mgr, EStateMsg msg, float dt);       // 8670
  void TurnAround(CStateManager& mgr, EStateMsg msg, float dt);    // 8464
  void Dodge(CStateManager& mgr, EStateMsg msg, float dt);         // 7DFC
  void Lurk(CStateManager& mgr, EStateMsg msg, float dt);          // AF70
  void Taunt(CStateManager& mgr, EStateMsg msg, float dt);         // B59C
  void Cover(CStateManager& mgr, EStateMsg msg, float dt);         // A3C0
  void Dead(CStateManager& mgr, EStateMsg msg, float dt);          // 5C10
  void TargetCover(CStateManager& mgr, EStateMsg msg, float dt);   // 78DC
  void TargetPlayer(CStateManager& mgr, EStateMsg msg, float dt);  // 7868
  void Approach(CStateManager& mgr, EStateMsg msg, float dt);      // 9478
  void WallHang(CStateManager& mgr, EStateMsg msg, float dt);      // 69AC
  void WallDetach(CStateManager& mgr, EStateMsg msg, float dt);    // 68E4
  void GetUp(CStateManager& mgr, EStateMsg msg, float dt);         // B3D4
  void Generate(CStateManager& mgr, EStateMsg msg, float dt);      // C1A4
  void Skid(CStateManager& mgr, EStateMsg msg, float dt);          // 94D0
  void DoubleSnap(CStateManager& mgr, EStateMsg msg, float dt);    // 99F0
  void JumpBack(CStateManager& mgr, EStateMsg msg, float dt);      // 63C8
  void Bounce(CStateManager& mgr, EStateMsg msg, float dt);        // 5D0C
  void PathFindEx(CStateManager& mgr, EStateMsg msg, float dt);    // 5EA4
  void Enraged(CStateManager& mgr, EStateMsg msg, float dt);       // A2B8
  void Jump(CStateManager& mgr, EStateMsg msg, float dt);          // 5310
  void Deactivate(CStateManager& mgr, EStateMsg msg, float dt);    // 5BF4
  void LaunchGrenade(CStateManager& mgr, EStateMsg msg, float dt); // 58D8
  void Captured(CStateManager& mgr, EStateMsg msg, float dt);      // 5610
  void RemoveFromWorld(CStateManager& mgr, EStateMsg msg, float dt); // 5B88
#endif
  // CEntity
  CEntity* TypesMatch(int typeId) const override;

  bool AttachActorToPirate(TUniqueId id);
  void DetachActorFromPirate();
  TUniqueId GetAttachedActor() const { return mAttachedActor; }

  bool GetEnableAim() const { return mEnableAim; } // Guessed Prime name.
  bool AllEnergyDrained() const { return mAllEnergyDrained; }
  void SetPortalPlane(const CPlane& plane) { mPortalPlane = plane; } // Guessed name.

private:
#ifdef TARGET_PC
  void EnsureBodyActive(CStateManager& mgr);
  void InitLauncherModel(const CSpacePirateData& data);    // 4F28
  void RenderLauncherModel(const CStateManager& mgr, const CTransform4f& xf,
                           const CModelFlags& flags) const; // 36CC
  void SquadRemove(CStateManager& mgr);                  // DF2C
  void SquadJoin(CStateManager& mgr);                    // DFD4
  void UpdateHeldPosition(CStateManager& mgr, float dt); // D720
  void UpdateGuardPosition(CStateManager& mgr, float dt); // D800
  void UpdateLeashTimer(float dt);                       // 5280
  void AvoidActors(CStateManager& mgr);                  // D40C
  void CheckBlade(CStateManager& mgr);                   // CC80
  bool CantJumpBack(CStateManager& mgr, const CVector3f& dir, float dist); // D2A0
  void TrooperWarpOut(CStateManager& mgr, bool flag);    // 51C8
  void SetVelocityForJump();                             // E168
  // Think path (fn_72_1980); the comment on each is its retail function.
  void UpdateCloak(float dt, CStateManager& mgr);        // 2F4C
  void UpdateAttacks(float dt, CStateManager& mgr);      // 28F0
  void UpdateAimBodyState(float dt, CStateManager& mgr); // 253C
  bool ShouldFrenzy(CStateManager& mgr);                 // 3220
  CVector3f GetTargetPos(CStateManager& mgr);            // CB30
  int GetStrafeDir(CStateManager& mgr, float dist);      // CE30
  void CheckForProjectiles(CStateManager& mgr);          // DBD4
  bool LineOfSightTest(CStateManager& mgr, const CVector3f& start, const CVector3f& end,
                       const CMaterialList& exclude);    // DB48
  void SetCinematicCollision(CStateManager& mgr);        // CA94
  void SetNonCinematicCollision(CStateManager& mgr);     // C9DC
  bool FireProjectile(float dt, CStateManager& mgr);     // 45F8
  void SquadReset(CStateManager& mgr);                   // DEDC
  void EmptyE314(CStateManager& mgr, bool flag);         // E314 (empty)
  TUniqueId ChooseTarget(CStateManager& mgr);            // EDEC
  TUniqueId ChooseTeamTarget(CStateManager& mgr);        // EFD4
  void UpdateTeamTarget(CStateManager& mgr);             // DE5C
  bool CheckTargetable(CStateManager& mgr);              // E2DC
  static rstl::list< TUniqueId > mChargePlayerList; // bss_40, shared by every pirate
  // Host layout (retail's is 0xc70 bytes, CHECK_SIZEOF below). Guest offsets kept as comments.
  // Members up to 0xa94 sit at MP1 offset + 0x2c8..0x2d0, so MP1's names carry over. Bit names
  // are xOFF_BB_name for retail's flag byte and bit (24 = the byte's top bit, as in MP1).
  CSpacePirateData mData;       // 0x7c0 (0x134)
  // 0x8f4..0x8fa: bits 0x8f4_24..0x8f6_24 are unpacked from mData.mFlags by the ctor, the rest
  // start cleared. 0x8f4_24 is `flags & 3` (MP1: `& 1`).
  bool x8f4_24_pendingAmbush : 1;         // flags & 3
  bool x8f4_25_ceilingAmbush : 1;         // flags 0x2
  bool x8f4_26_nonAggressive : 1;         // flags 0x4
  bool x8f4_27_melee : 1;                 // flags 0x8
  bool x8f4_28_noShuffleCloseCheck : 1;   // flags 0x10
  bool x8f4_29_onlyAttackInRange : 1;     // flags 0x20
  bool x8f4_30_ : 1;                      // flags 0x40
  bool x8f4_31_noKnockbackImpulseReset : 1; // flags 0x80
  bool x8f5_24_noMeleeAttack : 1;         // flags 0x200
  bool x8f5_25_breakAttack : 1;           // flags 0x400
  bool x8f5_26_seated : 1;                // flags 0x1000
  bool x8f5_27_shadowPirate : 1;          // flags 0x2000
  bool x8f5_28_alertBeforeCloak : 1;      // flags 0x4000
  bool x8f5_29_noBreakDodge : 1;          // flags 0x8000
  bool x8f5_30_floatingCorpse : 1;        // flags 0x10000
  bool x8f5_31_ragdollNoAiCollision : 1;  // flags 0x20000
  bool x8f6_24_trooper : 1;               // flags 0x40000
  bool x8f6_25_hearNoise : 1;
  bool x8f6_26_enableMeleeAttack : 1;
  bool x8f6_27_ : 1;
  bool x8f6_28_ : 1;
  bool x8f6_29_enableRetreat : 1;
  bool x8f6_30_shuffleClose : 1;
  bool x8f6_31_inAttackState : 1;
  bool x8f7_24_enablePatrol : 1;
  bool mEnableAim : 1;                    // 0x8f7_25 (MP1 x637_25_enableAim)
  bool x8f7_26_hearPlayerFire : 1;
  bool x8f7_27_inProjectilePath : 1;
  bool x8f7_28_noPlayerLos : 1;
  bool x8f7_29_inWallHang : 1;
  bool x8f7_30_jumpVelSet : 1;
  bool x8f7_31_prevInCineCam : 1;
  bool x8f8_24_pendingFrenzyChance : 1;
  bool x8f8_25_appliedBladeDamage : 1;
  bool x8f8_26_alwaysAggressive : 1;
  bool x8f8_27_coverCheck : 1;
  bool x8f8_28_enableDodge : 1;
  bool x8f8_29_noPlayerDodge : 1;
  bool mAllEnergyDrained : 1;             // 0x8f8_30 (MP1 x638_30_allEnergyDrained)
  bool x8f8_31_mayStartAttack : 1;
  bool x8f9_24_ : 1;
  bool x8f9_25_useJumpBackJump : 1;
  bool x8f9_26_started : 1;
  bool x8f9_27_inRange : 1;
  bool x8f9_28_satUp : 1;
  bool x8f9_29_enableBreakDodge : 1;      // the retail ctor never clears this bit
  bool x8f9_30_closeMelee : 1;
  bool x8f9_31_sentAttackMsg : 1;
  bool x8fa_24_normalDodge : 1;
  bool x8fa_25_ : 1;
  bool x8fa_26_ : 1;                      // ShouldWarpIn (fn_72_77D0) reads it; Echoes-only
  bool x8fa_27_ : 1;
  bool x8fa_28_ : 1;
  bool x8fa_29_ : 1;
  bool x8fa_30_ : 1;
  bool x8fa_31_ : 1;
  int x8fc_frenzyFrames;                  // 0x8fc
  TUniqueId x900_coverPoint;              // 0x900
  TUniqueId x902_previousCoverPoint;      // 0x902
  float x904_steeringSpeed;               // 0x904 (1.0)
  CVector3f x908_targetDelta;             // 0x908 (forward)
  CVector3f x914_coverPointRearDir;       // 0x914 (zero)
  CPathFindSearch mPathFindSearch;        // 0x920 (0xf0); GetSearchPath
  float xa0c_unkTimer;                    // 0xa0c (MP1 x744_unkTimer; counts down in Think)
  float xa10_steeringDelayTimer;          // 0xa10 (MP1 x748; non-zero holds the steering speed at 0)
  int xa14_;                              // 0xa14
  float xa18_;                            // 0xa18 = pInfo+0x58; MP1 x750_initialHP? [unresolved]
  float xa1c_coverRange;                  // 0xa1c
  CSegId mHeadSegId;                      // 0xa20 "Head_1"
  int xa24_;                              // 0xa24
  int xa28_taunt;                         // 0xa28 (-1)
  CBoneTracking mBoneTracking;            // 0xa2c (0x3c)
  int xa68_coverDir;                      // 0xa68 (-1)
  float xa70_intoJumpDist;                // 0xa70
  float xa74_eyeHeight;                   // 0xa74
  float xa78_;                            // 0xa78
  float xa7c_timeNoPlayerLos;             // 0xa7c
  float xa80_cantSeePlayerCycleCounter;   // 0xa80 (retail lfs/stfs: a float, the 0.1 s re-test timer)
  TUniqueId mAttachedActor;               // 0xa84
  CSegId xa86_gunSeg;                     // 0xa86 "gun_LCTR"
  CSegId xa87_elbowSeg;                   // 0xa87 "R_elbow"
  CSegId xa88_wristSeg;                   // 0xa88 "R_wrist"
  CSegId xa89_swooshSeg;                  // 0xa89 "Swoosh_LCTR"
  CSegId xa8a_lHipSeg;                    // 0xa8a "L_hip"
  CSegId xa8b_rHipSeg;                    // 0xa8b "R_hip"
  CSegId xa8c_collarSeg;                  // 0xa8c "Collar"
  float mAttackRemTime;                   // 0xa90 (1.0)
  TUniqueId mAttackTarget;                // 0xa94
  CBurstFire mBurstFire;                  // 0xa98 (0x60)
  float xaf8_jumpHeight;                  // 0xaf8 (3.0)
  CVector3f xafc_patrolDestPos;           // 0xafc
  int xb08_skidDir;                       // 0xb08 (-1)
  float xb0c_strafeDelayTimer;            // 0xb0c
  int xb10_meleeSeverity;                 // 0xb10 (-1)
  TUniqueId xb14_jumpPoint;               // 0xb14
  int xb18_dodgeDir;                      // 0xb18 (-1)
  float xb1c_dodgeDist;                   // 0xb1c
  float xb20_breakDodgeDist;              // 0xb20
  float xb24_timeSinceHitByPlayer;        // 0xb24 (FLT_MAX)
  float xb28_lowHealthFrenzyTimer;        // 0xb28 (FLT_MAX)
  float xb2c_ragdollDelayTimer;           // 0xb2c
  // 0xb30: module 50's CPirateRagDoll (0x11c bytes). The ctor stores 0 (0xFA14), the dtor frees it
  // (0xF188), Think's tail (0x1F08-0x2030) builds it once xb2c runs out.
  rstl::single_ptr< CPirateRagDoll > xb30_ragDoll;
  CIkChain xb34_ikChain;                  // 0xb34 (fn_72_10568 is CIkChain's default ctor)
  float xb7c_cloakDelayTimer;             // 0xb7c
  float xb80_electricParticleTimer;       // 0xb80
  float xb84_cloakStepTime;               // 0xb84
  float xb88_shadowPirateAlpha;           // 0xb88 (0.5)
  float xb8c_minCloakAlpha;               // 0xb8c = data.mCloakOpacity
  float xb90_maxCloakAlpha;               // 0xb90 = data.mMaxCloakOpacity
  float xb94_dodgeDelayTimer;             // 0xb94 = data.mBreakDodgeMinTime
  float xb98_aimDelayTimer;               // 0xb98 = data.mGunTrackDelay
  float xb9c_;                            // 0xb9c
  TUniqueId xba0_teamAiMgrId;             // 0xba0
  float xba4_heldPositionX;               // 0xba4 (CVector2f)
  float xba8_heldPositionY;               // 0xba8
  float xbac_holdPositionTime;            // 0xbac
  float xbb0_leashTimer;                  // 0xbb0
  CVector3f xbb4_;                        // 0xbb4
  int xbc0_playerIndex;                   // 0xbc0 (-1); HearPlayer's round-robin
  CVector3f xbc4_;                        // 0xbc4
  float xbd0_;                            // 0xbd0
  rstl::optional_object< CProjectileInfo > mProjectileInfo; // 0xbd4 (0x28), only if projectile valid
  bool xbfc_;                             // 0xbfc
  CSfxHandle xc00_;                       // 0xc00 (the sfx emitter handle; fn_72_2EA8)
  int xc04_warpPoint;                     // 0xc04 (-1); ShouldWarpIn
  // 0xc08 (0x4c, valid flag 0xc54): the grenade launcher's static model, built by fn_72_4F28 and
  // drawn at the R_wrist locator by fn_72_36CC.
  rstl::optional_object< CModelData > xc08_launcherModel;
  int xc58_;                              // 0xc58
  CPlane mPortalPlane;                    // 0xc5c (0x10); written by the WispTentacle while it drags the pirate
#else
  uchar x7c0_[0x137];
  bool x8f7_24_ : 1;
  bool mEnableAim : 1; // Guessed Prime name; read by flying pirate taunts.
  bool x8f7_26_ : 1;
  bool x8f7_27_ : 1;
  bool x8f7_28_ : 1;
  bool x8f7_29_ : 1;
  bool x8f7_30_ : 1;
  bool x8f7_31_ : 1;
  bool x8f8_24_ : 1;
  bool x8f8_25_ : 1;
  bool x8f8_26_ : 1;
  bool x8f8_27_ : 1;
  bool x8f8_28_ : 1;
  bool x8f8_29_ : 1;
  bool mAllEnergyDrained : 1; // Guessed Prime name; read by the Metroid energy drain.
  bool x8f8_31_ : 1;
  uchar x8f9_[0x18b];
  TUniqueId mAttachedActor; // Guessed member name.
  uchar xa86_[0xaa];
  void* xb30_;
  uchar xb34_[0x128];
  CPlane mPortalPlane; // Guessed name; written by the WispTentacle while it drags the pirate.
  uchar xc6c_[4];
#endif
#ifdef TARGET_PC // helpers
  // The module's stateless helper half (retail fn_72_*): the bodies that are neither an FSM state
  // nor an FSM trigger. They live in PortCSpacePirateHelpers.cpp; the note with every offset and
  // the arm tables is docs/module-analysis/SpacePirate_helpers.md.
  void UpdateSfxEmitter();                                    // 2EA8
  rstl::optional_object< CAABox > GetActorTouchBounds() const; // 1854
#endif
};
CHECK_SIZEOF(CSpacePirate, 0xc70)

#endif
