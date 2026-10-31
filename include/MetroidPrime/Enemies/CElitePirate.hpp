#ifndef _CELITEPIRATE
#define _CELITEPIRATE

#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "REL/REL_Setup.h"

#ifdef TARGET_PC
#include "Collision/CCollidableAABox.hpp"
#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/CToken.hpp"
#include "Kyoto/TToken.hpp"
#include "MetroidPrime/CActorParameters.hpp"
#include "MetroidPrime/CAnimationParameters.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/Enemies/SPositionHistory.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"
#include "MetroidPrime/ScriptLoader/Structs/SLdrShockWaveInfo.hpp"
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"
#include "MetroidPrime/Weapons/CShockWave.hpp"

class CBCLocomotionCmd;
class CCollisionActorManager;
class CElementGen;
class CGenDescription;
class CSkinnedModel;

// Host copy of the properties the ElitePirate loader reads and hands to the class (retail:
// CElitePirateData, 0x190 bytes at 0x828, built on the loader's stack by fn_15_B82C and copied in
// by its copy constructor fn_15_B360). Members are in retail offset order (data+N = this+0x828+N);
// every one of the 0x190 bytes is accounted for below, so the loader's energyAbsorbDuration and
// energyAbsorbVariance (SLdr 0xa70/0xa74) have no slot: fn_15_178 never passes them.
class CElitePirateData {
public:
  // fn_15_B82C's parameter list, in its register/stack order (r4..r10, f1..f8, then the stack
  // words caller sp+0x8..0x64). Each line names the slot and where B82C stores it.
  CElitePirateData(CAssetId stateMachine, int initialAnim, const CDamageInfo& meleeDamage,
                   float maxMeleeRange, float minShockwaveRange, float maxShockwaveRange,
                   float minRocketRange, float maxRocketRange, float unknown5236C2B6,
                   float unknown01EAAB17, CAssetId darkShield, ushort darkShieldSound,
                   CAssetId darkShieldPop, CAssetId lightShield, float tauntInterval,
                   ushort lightShieldSound, CAssetId lightShieldPop, float tauntVariance,
                   float shockWaveWeight0, float shockWaveWeight1, float shockWaveWeight2,
                   float shockWaveWeight5, float repeatedAttackChance, float energyAttractionForce,
                   CAssetId partAsset, ushort alwaysFF23F5E1EE,
                   const CActorParameters& rocketLauncherActorInfo,
                   const CAnimationParameters& rocketLauncherAnimInfo,
                   const CAnimationParameters& unknown7E6E0D38, CAssetId rocket,
                   const CDamageInfo& rocketDamage, int unknown624222F8, int unknown31E43A1C,
                   CAssetId visorElectricEffect, ushort soundVisorElectric,
                   const SLdrShockWaveInfo& singleShockWaveInfo,
                   const SLdrShockWaveInfo& doubleShockWaveInfo, CAssetId shieldedModel,
                   CAssetId shieldedSkinRules);

  CAssetId mStateMachine;       // +0x00 patterned.stateMachine2; the ctor's 'FSM2' token (0xc30)
  int mInitialAnim;             // +0x04 patterned.animationInformation.initial_anim (fn_15_9988)
  CDamageInfo mMeleeDamage;     // +0x08 the melee hit (fn_15_7004)
  float mTauntInterval;         // +0x24
  float mTauntVariance;         // +0x28
  float mRepeatedAttackChance;  // +0x2c
  float mEnergyAttractionForce; // +0x30
  CAssetId mPartAsset;          // +0x34 alwaysFF: the ctor's optional 'PART' token (0x9e8)
  ushort mAlwaysFF23F5E1EE;     // +0x38
  CActorParameters mRocketLauncherActorInfo;  // +0x3c the launcher's actor parameters
  CAnimationParameters mRocketLauncherAnimInfo; // +0x9c the launcher's model (fn_15_2564)
  CAnimationParameters mUnknown7E6E0D38;      // +0xa8 the launcher's model when Ing-possessed
  int mUnknown624222F8;         // +0xb4
  int mUnknown31E43A1C;         // +0xb8
  CAssetId mVisorElectricEffect; // +0xbc
  ushort mSoundVisorElectric;    // +0xc0
  float mUnknown5236C2B6;       // +0xc4
  float mUnknown01EAAB17;       // +0xc8
  CAssetId mDarkShield;         // +0xcc
  ushort mDarkShieldSound;      // +0xd0
  CAssetId mDarkShieldPop;      // +0xd4 'PART' for the shield pop, polarity != 1 (fn_15_3C5C)
  CAssetId mLightShield;        // +0xd8
  ushort mLightShieldSound;     // +0xdc
  CAssetId mLightShieldPop;     // +0xe0 'PART' for the shield pop, polarity 1
  // +0xe4 / +0x114: retail keeps the SLdrShockWaveInfo (fn_15_B57C copies it) and fn_15_1A48 builds
  // the CShockWaveInfo from it on each call (fn_15_1A84); the host builds it once here.
  CShockWaveInfo mSingleShockWaveInfo; // +0xe4
  CShockWaveInfo mDoubleShockWaveInfo; // +0x114 (fn_15_1A48, when 0xcb0 == 5)
  // +0x144..+0x150: the Shockwave state's weights for severities 0/1/2/5 (fn_15_4AE0), the SLdr's
  // 0x28b39197/0xe27de71b/0x665e7ace/0xacd4d06d in that order (fn_15_178 0x9EC..0xA08).
  float mUnknown28B39197;       // +0x144
  float mUnknownE27DE71B;       // +0x148
  float mUnknown665E7ACE;       // +0x14c
  float mUnknownACD4D06D;       // +0x150
  float mMaxMeleeRange;         // +0x154
  float mMinShockwaveRange;     // +0x158
  float mMaxShockwaveRange;     // +0x15c
  float mMinRocketRange;        // +0x160
  float mMaxRocketRange;        // +0x164
  CAssetId mRocket;             // +0x168 the ctor's CProjectileInfo (0xcd0) asset
  CDamageInfo mRocketDamage;    // +0x16c that CProjectileInfo's damage
  CAssetId mShieldedModel;      // +0x188 the shield CSkinnedModel's CMDL
  CAssetId mShieldedSkinRules;  // +0x18c and its CSKR
};
#endif

// The REL's CElitePirate (module 15). 0xd30 bytes on the GameCube (the loader's rs_new size), built
// by fn_15_A190; its vtable lbl_15_data_634 is the map of what it overrides - the slots it inherits
// keep the DOL's own symbols (e.g. slot 4 PreThink is CPatterned::PreThink, so the class does not
// override it). Only the passive half is implemented here: the loader, the constructor and the
// methods that let the actor load, think, stand and render. The state machine (fn_15_C0D0, 0x808),
// the shields, the shock waves, the rockets and the melee attack are later milestones.
class REL_EXPORT CElitePirate : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
#ifdef TARGET_PC
  // Argument order is the REL loader's call at 0x00000BE0: (uid, name, entityInfo, transform,
  // modelData, patternedInfo, actorParameters, data).
  CElitePirate(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
               const CTransform4f& xf, const CModelData& modelData, const CPatternedInfo& pInfo,
               const CActorParameters& actParms, const CElitePirateData& data);
  ~CElitePirate() override;
  // Host: console `var` handler (ep.timer, ep.launcher, ep.target); see PortCElitePirate.cpp.
  static bool PortConsoleVar(CEntity& ent, const char* name, const int* value, int& current);

  // CEntity / CActor
  // PreThink, Touch, Death, KnockBack and SetupStateMachine are not overridden: the REL's vtable
  // keeps the base-class symbols in those slots (4, 19, 38, 39, 60).
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  void Render(const CStateManager& mgr) const override;
  CVector3f GetAimPosition(const CStateManager& mgr, float dt) const override; // slot 21, fn_15_7EEC
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;

  // CAi
  // The REL's own copy of the base's body: it stores skDamageHitTime into the cooldown at 0x448
  // (asm 0x004C: `lfs f0, skDamageHitTime` + `stfs f0, 0x448(r3)`).
  // @ fn_15_4C
  void TakeDamage(const CVector3f&, float) override { mDamageCooldownTimer = skDamageHitTime; }
  // The one-line accessors below are the REL's own bodies (vtable slot -> function), the
  // instruction each body is, and the retail offset it sits at.
  // @ fn_15_A4
  bool CanBeShot(const CStateManager&, int) override { return true; } // slot 42; `li r3, 1` (0xA4)
  // The REL's own copy: `li r3, 0` (asm 0x00AC). It keeps the Ingsmasher out of the state
  // manager's listening list, so the REL's Listen (fn_15_B4) is never called for it.
  // @ fn_15_AC
  bool IsListening() const override { return false; }                 // slot 43, fn_15_AC
  // `li r3, 0` (0xB4): the Ingsmasher never hears noises.
  // @ fn_15_B4
  bool Listen(CStateManager&, const CVector3f&, EListenNoiseType) override { // slot 44
    return false;
  }

  // CPatterned
  // `addi r3, r3, 0xcd0` (0x52C8): mProjectileInfo.
  // @ fn_15_52C8
  CProjectileInfo* ProjectileInfo() override { return &mProjectileInfo; } // slot 55
  using CPatterned::GetSearchPath;
  // The REL's own copy: `addi r3, r3, 0xa50` (asm 0x0000) - its own path search, not the base's.
  // @ fn_15_0
  CPathFindSearch* GetSearchPath() override { return &mPathFindSearch; } // slot 56, fn_15_0
  // `li r3, 0` (0x64).
  // @ fn_15_64
  CPathFindPointSearch* GetPointSearchPath() override { return nullptr; } // slot 57
  // `li r3, 0` (0x6C).
  // @ fn_15_6C
  CRagDoll* GetRagDoll() const override { return nullptr; }              // slot 61
  // `lhz r0, kInvalidUniqueId` then `sth r0, 0x0(r3)` (0x74): the return value is sret.
  // @ fn_15_74
  TUniqueId GetAttackTarget() const override { return kInvalidUniqueId; } // slot 67
  // `lfs f1, kDefaultGravityAccel@l` (0x90): the DOL's constant (9.81f * 2.5f = 24.525001f),
  // not the 24.525f literal the port had.
  // @ fn_15_90
  float GetGravityConstant() const override { return kDefaultGravityAccel; } // slot 69
  // The REL's own copy: `li r3, 0` (asm 0x1A0C) - the Ing cannot be pushed out of it.
  // @ fn_15_1A0C
  bool CanBeUnPossessed(CStateManager&) const override { return false; } // fn_15_1A0C
  // The REL's own copy: `addi r3, r3, 0x9c0` (asm 0x0008) - its own AABB, not the base's.
  // @ fn_15_8
  const CCollisionPrimitive* GetCollisionPrimitive() const override { return &mCollidableBox; } // fn_15_8
  void RenderIngSnatchingTransition(const CStateManager& mgr) const override; // fn_15_800C
  // Slots the REL overrides with the base class's own behaviour, so the port leaves them
  // inherited: 15 GetHealthInfo (fn_15_D8 calls the base), 18 GetTouchBounds (fn_15_10), the
  // origin, model-alpha and IsOnGround accessors, and 73 GetDeathExplosionParticle (fn_15_9C).

  // The class's own virtuals past CPatterned's table (vt+0x148..0x22c). Plain members here: the
  // REL is the only implementation and nothing derives from the Ingsmasher.
  void InstallStateMachine(CStateManager& mgr);                // vt+0x148, fn_15_9840
  void SetShieldVulnerability(CStateManager& mgr, bool shielded); // vt+0x224, fn_15_27FC [name?]
  void SendLauncherMsg(CStateManager& mgr, bool start);        // vt+0x228, fn_15_24F0
  const CShockWaveInfo& GetShockWaveInfo() const;              // vt+0x22c, fn_15_1A48

  // FSM triggers (fn_15_9840's table, in its order). Bodies are later slices.
  bool Alerted(CStateManager& mgr, const CTriggerData& data) const;
  bool AngryAttackOver(CStateManager& mgr, const CTriggerData& data) const;
  bool AttackPatternOver(CStateManager& mgr, const CTriggerData& data) const;
  bool BreakProjectileAttack(CStateManager& mgr, const CTriggerData& data) const;
  bool CanShockwave(CStateManager& mgr, const CTriggerData& data) const;
  bool ClearLineOfSight(CStateManager& mgr, const CTriggerData& data) const;
  bool DonePursuing(CStateManager& mgr, const CTriggerData& data) const;
  bool DoneTurning(CStateManager& mgr, const CTriggerData& data) const;
  bool HasAttackPattern(CStateManager& mgr, const CTriggerData& data) const;
  bool InDetectionRange(CStateManager& mgr, const CTriggerData& data) const;
  bool InPosition(CStateManager& mgr, const CTriggerData& data) const;
  bool NotReachedTarget(CStateManager& mgr, const CTriggerData& data) const;
  bool PickedSpreadShot(CStateManager& mgr, const CTriggerData& data) const;
  bool PlayerInNoAttack(CStateManager& mgr, const CTriggerData& data) const;
  bool PoweredDown(CStateManager& mgr, const CTriggerData& data) const;
  bool PoweredUp(CStateManager& mgr, const CTriggerData& data) const;
  bool ReadyToCharge(CStateManager& mgr, const CTriggerData& data) const;
  bool ReturnedToPatrol(CStateManager& mgr, const CTriggerData& data) const;
  bool ShieldKilled(CStateManager& mgr, const CTriggerData& data) const;
  bool ShockwaveIsNext(CStateManager& mgr, const CTriggerData& data) const;
  bool ShotAt(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldAlert(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldFire(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldMeleeAttack(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldShockwave(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldTurn(CStateManager& mgr, const CTriggerData& data) const;
  bool SpotPlayer(CStateManager& mgr, const CTriggerData& data) const;
  bool StillAngry(CStateManager& mgr, const CTriggerData& data) const;
  bool TargetNotOnMesh(CStateManager& mgr, const CTriggerData& data) const;
  bool TargetUnreachable(CStateManager& mgr, const CTriggerData& data) const;
  bool TooClose(CStateManager& mgr, const CTriggerData& data) const;

  // FSM states.
  void Alert(CStateManager& mgr, EStateMsg msg, float dt);
  void AngryAttackBegin(CStateManager& mgr, EStateMsg msg, float dt);
  void AngryAttackEnd(CStateManager& mgr, EStateMsg msg, float dt);
  void Dead(CStateManager& mgr, EStateMsg msg, float dt);
  void FollowAttackPattern(CStateManager& mgr, EStateMsg msg, float dt);
  void InvulnAlert(CStateManager& mgr, EStateMsg msg, float dt);
  void MeleeAttack(CStateManager& mgr, EStateMsg msg, float dt);
  void Patrol(CStateManager& mgr, EStateMsg msg, float dt);
  void PowerDown(CStateManager& mgr, EStateMsg msg, float dt);
  void PowerUp(CStateManager& mgr, EStateMsg msg, float dt);
  void ProjectileAttack(CStateManager& mgr, EStateMsg msg, float dt);
  void Pursue(CStateManager& mgr, EStateMsg msg, float dt);
  void Shielding(CStateManager& mgr, EStateMsg msg, float dt);
  void ShieldUp(CStateManager& mgr, EStateMsg msg, float dt);
  void Shockwave(CStateManager& mgr, EStateMsg msg, float dt);
  void SpreadShot(CStateManager& mgr, EStateMsg msg, float dt);
  void Stunned(CStateManager& mgr, EStateMsg msg, float dt);
  void TargetPatrol(CStateManager& mgr, EStateMsg msg, float dt);
  void Taunt(CStateManager& mgr, EStateMsg msg, float dt);
  void Turn(CStateManager& mgr, EStateMsg msg, float dt);
  void Wait(CStateManager& mgr, EStateMsg msg, float dt);

  // FSM code functions.
  void SelectTarget(CStateManager& mgr, float dt);
  void PickAttackType(CStateManager& mgr, float dt);
#endif
  // CEntity
  CEntity* TypesMatch(int typeId) const override;

private:
#ifdef TARGET_PC
  // Helpers (retail function on each; shared by Think, AcceptScriptMsg and the FSM states).
  bool IsShieldUp() const;                                         // fn_15_2CE8
  bool IsInitialIdle(int idle) const;                              // fn_15_9988
  CVector3f GetLockOnPosition(const CActor& launcher) const;       // fn_15_2284
  // 0x1B54 rlwimi r0, r4, 6, 25, 25: mask 0x40 (xc10 = (xc10 & ~0x40) | (a1 -> 0x40)), stored to
  // xc10 at 0x1B58 - the XDamaged bit of the flags byte, set or cleared from the bool.
  // @ fn_15_1B50
  void SetXDamaged(bool xdamaged) { mXDamaged = xdamaged; }        // fn_15_1B50
  CPFArea* GetPathArea(CStateManager& mgr) const;                  // fn_15_1598
  void ReleaseClaimedRegion(CStateManager& mgr);                   // fn_15_1328
  void SetupCollisionManagers(CStateManager& mgr);                 // fn_15_36CC
  void ClassifyCollisionActors(CStateManager& mgr);                // fn_15_3574
  void CreateGrenadeLauncher(CStateManager& mgr, TUniqueId uid);   // fn_15_2564
  void DeleteGrenadeLauncher(CStateManager& mgr);                  // fn_15_251C
  void UpdateLauncherTransform(CStateManager& mgr);                // fn_15_2394
  void UpdateTimer(float dt);                                      // fn_15_27DC
  void UpdatePositionHistory();                                    // fn_15_2204
  void UpdateAimBlend(float dt);                                   // fn_15_8D84
  void UpdateParticleGen(float dt);                                // fn_15_8E34
  void UpdateShieldAlpha(float dt);                                // fn_15_8EA0
  void UpdatePushBack(CStateManager& mgr, float dt);               // fn_15_8FB8
  bool IsOnJumpHint(const CStateManager& mgr) const;               // fn_15_1788
  int GetPlayerHintType(const CStateManager& mgr) const;           // fn_15_15F8
  void RenderShield(const CStateManager& mgr) const;               // fn_15_8080
  void CreateShockWave(CStateManager& mgr, const CInt32POINode& node); // fn_15_70D4
  void FireRocket(CStateManager& mgr);                             // fn_15_728C
  void ShakePlayers(CStateManager& mgr);                           // fn_15_1C7C
  void PushPlayer(CStateManager& mgr, float horizontal, float vertical); // fn_15_1B60
  void PopShield(CStateManager& mgr);                              // fn_15_3C5C
  void ApplyMeleeDamage(CStateManager& mgr, TUniqueId target);     // fn_15_7004
  void SetArmTouchBounds(CStateManager& mgr, const CVector3f& bounds); // part of fn_15_7BFC

  // movement-state helpers (PortCElitePirateMove.cpp; shared with the combat states)
  void SetAttackKind(int kind, EStateMsg msg);                     // fn_15_4790 (+47DC/4848)
  void UpdateAbsorb(CStateManager& mgr, EStateMsg msg);            // fn_15_850C
  void TauntCmd(EStateMsg msg, int tauntType);                     // fn_15_BEA0 (retry, body state 0x10)
  void TurnCmd(EStateMsg msg, const CBCLocomotionCmd& cmd);        // fn_15_BDFC (retry, body state 8)
  void PathToward(CStateManager& mgr, EStateMsg msg, float dt, const CVector3f& dest); // fn_15_5CBC
  void PlanPath(CStateManager& mgr, float dt, const CVector3f& dest); // fn_15_1FC0
  void ClaimPathRegion(CStateManager& mgr);                        // fn_15_13B8
  bool IsPlayerOnPath(const CStateManager& mgr) const;             // fn_15_1A14

  // combat-state helpers (PortCElitePirateCombat.cpp)
  void ResetAttackTimer(CStateManager& mgr);                       // fn_15_1F2C
  template < typename T >
  void TryCommand(EStateMsg msg, pas::EAnimationState state, const T& cmd); // fn_15_BAF8/BC04/BD10
  CSfxHandle mShieldSfx;                         // 0xc98 the shield's looping emitter (ShieldUp/Shielding)

  // Host layout; retail offsets (fn_15_A190 and the helpers) on each. Retail is 0xd30 bytes.
  int x7c0_;                                     // 0x7c0 = -1 [?] not read in this slice
  CDamageVulnerability mBodyVulnerability;       // 0x7c4 copy of the patterned info's (fn_15_2B08)
  rstl::single_ptr< CCollisionActorManager > mShieldCollisionManager; // 0x7f4 the "Shield" OBB
  CVector3f mLeftPalmPos;                        // 0x7f8 L_Palm_LCTR (PreRender)
  CVector3f mRightPalmPos;                       // 0x804 R_Palm_LCTR
  CVector3f x810_;                               // 0x810 [?]
  CVector3f x81c_;                               // 0x81c place it powered up in (fn_15_5F08);
                                                 // ShouldAlert is false within 5 m of it
  CElitePirateData mData;                        // 0x828
  rstl::single_ptr< CCollisionActorManager > mBodyCollisionManager; // 0x9b8 the joint spheres
  CCollidableAABox mCollidableBox;               // 0x9c0 GetCollisionPrimitive
  rstl::optional_object< TLockedToken< CGenDescription > > mPart; // 0x9e8 data.mPartAsset
  TUniqueId mHeadColliderId;                     // 0x9f8 "Head_1"
  TUniqueId mLauncherId;                         // 0x9fa the CElitePirateGrenadeLauncher
  rstl::reserved_vector< TUniqueId, 8 > mRightArmColliders; // 0x9fc R_ joints + R_Palm_LCTR
  rstl::reserved_vector< TUniqueId, 8 > mLeftArmColliders;  // 0xa10 L_ joints + L_Palm_LCTR
  TUniqueId mShieldColliderId;                   // 0xa24
  TUniqueId xa26_;                               // 0xa26 [?]
  float mBaseSpeed;                              // 0xa28 mSpeed at construction
  float mSteeringSpeed;                          // 0xa2c XCRT: walk-speed ratio
  float mLastHP;                                 // 0xa30 HP when the vulnerability was last set
  float mTimer;                                  // 0xa34 counts down in Think (fn_15_27DC)
  float mXDamagedTimer;                          // 0xa38 Shielding's countdown to clear mXDamaged;
                                                 // only the ctor writes it (0), so it never runs
  float mTime;                                   // 0xa3c accumulated dt
  float mStuckTime;                              // 0xa40 s at the end of the path with the player
                                                 // off it (fn_15_5CBC); TargetUnreachable > 1
  float mPursueStartTime;                        // 0xa44 mTime when Pursue began (fn_15_5BB4);
                                                 // not written by the ctor
  float mLastPushTime;                           // 0xa48 mTime of the last push-back
  int mClaimedRegion;                            // 0xa4c path region with avoidance flag 4
  CPathFindSearch mPathFindSearch;               // 0xa50 (fn_15_0)
  CVector3f mPatrolGoal;                         // 0xb3c TargetPatrol's goal: the base's
                                                 // mDestPos (0x334) at Activate; InPosition < 5 m
  SPositionHistory mPositionHistory;             // 0xb48 (DOL fn_801931E0, 5.f)
  bool mEvent8Active : 1;                        // 0xc10 0x80: user events 8 (set) / 9 (clear)
  bool mXDamaged : 1;                            // 0xc10 0x40: XDMG/XRDG/XXDG (fn_15_1B50)
  bool mAlerted : 1;                             // 0xc10 0x20: ALRT
  bool xc10_10_ : 1;                             // 0xc10 0x10 [?] not cleared by the ctor
  bool mAlerted2 : 1;                            // 0xc10 0x08: ALRT [?]
  bool xc10_04_ : 1;                             // 0xc10 0x04 [?]
  bool mLauncherIngPossessed : 1;                // 0xc10 0x02: launcher built Ing-possessed
  bool xc10_01_ : 1;                             // 0xc10 0x01 [?]
  int xc14_;                                     // 0xc14 = 0 [?]
  rstl::reserved_vector< int, 3 > mAttackKindHistory; // 0xc14.. (retail's xc14_ is its count; SetAttackKind pushes, never read)
  int xc28_;                                     // 0xc28 = -1 [?]
  int mAttackKind;                               // 0xc2c 2/8 rocket, 4 melee, 7 shockwave
  CToken mFsm2;                                  // 0xc30 'FSM2' data.mStateMachine (fn_15_B748)
  int xc3c_;                                     // 0xc3c = 6 [?]
  int mInitialAnimState;                         // 0xc40 1, or 10/11/12 matching mInitialAnim
  bool mEvent7 : 1;                              // 0xc44 0x80: user event 7
  int xc48_;                                     // 0xc48 = 1 [?]
  int xc4c_;                                     // 0xc4c = 1 [?]
  float mLastMeleeTime;                          // 0xc50 mTime of the last melee/contact hit
  bool mMeleeDamagePending : 1;                  // 0xc54 0x80 (event 17)
  bool mKnockPending : 1;                        // 0xc54 0x40 (event 17)
  float xc58_;                                   // 0xc58 = 0 [?] (fn_15_B080 block 0xc58..0xcb0)
  float mShieldUpTime;                           // 0xc5c mTime of the last shield raise (44A8);
                                                 // -1000 from fn_15_B080 until the first ShieldUp
  float mShieldDamage;                           // 0xc60 damage the shield absorbed
  float mShieldHitTime;                          // 0xc64 mTime of the last shield hit
  float mShieldAlpha;                            // 0xc68
  int mShieldPolarity;                           // 0xc6c 0 dark, 1 light, 2 none
  rstl::optional_object< TLockedToken< CGenDescription > > mLightShieldPop; // 0xc70
  rstl::optional_object< TLockedToken< CGenDescription > > mDarkShieldPop;  // 0xc80
  rstl::single_ptr< CElementGen > mHandGen;      // 0xc94 at the left palm (states create it)
  rstl::optional_object< TLockedToken< CSkinnedModel > > mShieldModel; // 0xc9c
  bool mShieldPopPending : 1;                    // 0xcac 0x40
  bool mDoubleSpeed : 1;                         // 0xcac 0x80
  int xcb0_;                                     // 0xcb0 = 1; 5 selects the double shock wave
  int xcb4_;                                     // 0xcb4 = 7 [?]
  CVector3f xcb8_;                               // 0xcb8 [?]
  CVector3f xcc4_;                               // 0xcc4 [?]
  CProjectileInfo mProjectileInfo;               // 0xcd0 (fn_15_52C8)
  int mRocketsFired;                             // 0xcf8
  int mRocketsToFire;                            // 0xcfc salvo size: SpreadShot sets 4, the
                                                 // other attack a random data range
  int mRocketPattern;                            // 0xd00 0 homing, 1 spread
  int mDefaultRocketPattern;                     // 0xd04 = 1, written by the ctor only: the pattern
                                                 // PickAttackType copies 20% of the time
  bool mRocketEventSeen;                         // 0xd08 event 18 during attack kind 2
  float mAimBlend;                               // 0xd0c towards the shield collider
  mutable CVector3f mShieldAimPos;               // 0xd10
  int xd1c_;                                     // 0xd1c taunt type: Taunt cycles 0 -> 6 -> 1 -> 0
  int mAngryCount;                               // 0xd20 int: AngryAttackEnd decrements, StillAngry tests > 0
  bool xd24_80_ : 1;                             // 0xd24 [?]
  bool xd24_40_ : 1;
  bool xd24_20_ : 1;
  bool xd24_10_ : 1;
  float mEnergyGlow;                             // 0xd28
  bool mAbsorbing;                               // 0xd2c
#else
  uchar x7c0_[0x570]; // CElitePirateData, the path search, the projectile info and everything above
#endif
};
CHECK_SIZEOF(CElitePirate, 0xd30)

#endif
