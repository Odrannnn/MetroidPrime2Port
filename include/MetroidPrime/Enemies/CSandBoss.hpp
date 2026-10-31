#ifndef _CSANDBOSS
#define _CSANDBOSS

#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "REL/REL_Setup.h"

#ifdef TARGET_PC
#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Math/CPlane.hpp"
#include "Kyoto/Math/CQuaternion.hpp"
#include "MetroidPrime/CBoneTracking.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/ScriptLoader/SLdrSandBoss.hpp"
#include "MetroidPrime/StateMachineCommon.hpp"
#include "MetroidPrime/Weapons/CPlasmaProjectile.hpp"
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"

#include "rstl/reserved_vector.hpp"
#include "rstl/single_ptr.hpp"
#include "rstl/vector.hpp"

class CCollisionActorManager;
class CPlayer;
class CScannableObjectInfo;
class CSkinnedModel;

// The REL's charge-beam projectile ("SandBossChargeBeam"): a CPlasmaProjectile subclass whose own
// code is fn_55_1027C..fn_55_10760 (fn_55_1027C overrides RayCollisionCheckWithWorld; 10548..10760
// are inlined helpers of it), constructed by fn_55_107B0 (called from CreateChargeBeam,
// fn_55_2D40) and destroyed by fn_55_108E4 / fn_55_10C18. The subclass adds one 16-byte member:
// the plane its ray check clips to (retail 0x6A8: three normal floats, then a constant).
class SandBossChargeBeam : public CPlasmaProjectile {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  SandBossChargeBeam(const TToken< CWeaponDescription >& description, const CBeamInfo& beamInfo,
                     TUniqueId uid, TAreaId areaId, TUniqueId owner,
                     const CPlane& plane); // fn_55_107B0
  ~SandBossChargeBeam() override;      // fn_55_108E4 / fn_55_10C18

  CRayCastResult RayCollisionCheckWithWorld(TUniqueId& idOut, const CVector3f& start,
                                            const CVector3f& end, float magnitude,
                                            rstl::reserved_vector< TUniqueId, 1024 >& nearList,
                                            CStateManager& mgr,
                                            EStaticGeometryTest staticTest) override; // fn_55_1027C

private:
  CPlane mPlane; // 0x6A8 (up plane through the boss; fn_55_1027C clips the beam to it)
};

// One entry of the vector at 0x1478 (reserved to 0x10 by fn_55_10FA8, count 0x147C, capacity
// 0x1480, data 0x1484): a candidate point and its squared distance from the player.
struct SSandBossIdEntry {
  TUniqueId mId;
  float mValue; // squared distance (fn_55_9E3C)
};

// One entry of the charge-beam list at 0xE94 (count 0xE94, data 0xE98, 0x20 bytes each, capacity 3:
// it ends exactly at 0xEF8): the beam actor, the segment it was fired along and its looping emitter.
struct SSandBossChargeBeamEntry {
  TUniqueId mBeam;  // +0x00
  CVector3f mStart; // +0x04
  CVector3f mEnd;   // +0x10
  CSfxHandle mSfx;  // +0x1C
};
#endif

// The REL's CSandBoss (module 55, Amorbis / Dark Amorbis). 0x1660 bytes on the GameCube (the
// loader's rs_new size), built by fn_55_E170; its vtable lbl_55_data_760 is the map of what it
// overrides. docs/module-analysis/SandBoss.md is the analysis (tables, member map, flag bits,
// function map). Slice A (PortCSandBoss.cpp) holds the loader, the constructor, the vtable
// overrides, collision/armour and the FSM tables; PortCSandBossStates.cpp holds the triggers,
// states and code functions; PortCSandBossAttacks.cpp the attack helpers and the charge beam.
class REL_EXPORT CSandBoss : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
#ifdef TARGET_PC
  // Argument order is the REL loader's (fn_55_178): (uid, name, info, xf, model, actParms, pInfo,
  // data).
  CSandBoss(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
            const CTransform4f& xf, const CModelData& modelData, const CActorParameters& actParms,
            const CPatternedInfo& pInfo, const SLdrSandBossData& data); // fn_55_E170
  ~CSandBoss() override;                                                // fn_55_DB60

  // CEntity
  void PreThink(float dt, CStateManager& mgr) override;                  // fn_55_D6BC
  void Think(float dt, CStateManager& mgr) override;                     // fn_55_D620
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override; // fn_55_D8BC

  // CActor
  void PreRender(CStateManager& mgr) override;                       // fn_55_D290
  void AddToRenderer(const CStateManager& mgr) const override;       // fn_55_D208
  void Render(const CStateManager& mgr) const override;              // fn_55_D308
  void PreRenderAllViewports(CStateManager& mgr) override;           // fn_55_D08C
  const CHealthInfo* GetHealthInfo() const override;                 // fn_55_D8
  const CDamageVulnerability* GetDamageVulnerability() const override; // fn_55_CF6C
  rstl::optional_object< CAABox > GetTouchBounds() const override;   // fn_55_10
  CVector3f GetAimPosition(const CStateManager& mgr, float dt) const override; // fn_55_CDF8
  CAABox GetSortingBounds(const CStateManager& mgr) const override;  // fn_55_CF8C
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;                           // fn_55_D384
  CScannableObjectInfo* GetScannableObjectInfo() const override;     // fn_55_CDB0

  // CAi
  void TakeDamage(const CVector3f& direction, float magnitude) override; // fn_55_4C
  bool CanBeShot(const CStateManager& mgr, int) override;               // fn_55_AC
  bool IsListening() const override;                                    // fn_55_8
  bool Listen(CStateManager& mgr, const CVector3f& position,
              EListenNoiseType type) override;                          // fn_55_B4
  CVector3f GetOrigin(const CStateManager& mgr, const CTeamAiRole& role,
                      const CVector3f& aimPos) const override;          // fn_55_BC

  // CPatterned
  void RenderSystemsToBeDrawnLast(const CStateManager& mgr, uint mask,
                                  uint target) const override; // fn_55_D304
  uchar GetModelAlphau8(const CStateManager& mgr) const override; // fn_55_5C
  CProjectileInfo* ProjectileInfo() override;                     // fn_55_CDF0
  using CPatterned::GetSearchPath;
  CPathFindSearch* GetSearchPath() override;                      // fn_55_64
  CPathFindPointSearch* GetPointSearchPath() override;            // fn_55_6C
  void SetupStateMachine(CStateManager& mgr) override;            // fn_55_CAA4
  CRagDoll* GetRagDoll() const override;                          // fn_55_74
  TUniqueId GetAttackTarget() const override;                     // fn_55_7C
  bool IsOnGround() const override;                               // fn_55_8C
  float GetGravityConstant() const override;                      // fn_55_98
  bool IsScanVisorSelfRender() const override;                    // fn_55_0
  CAABox GetScanVisorRenderBounds(const CStateManager& mgr) const override; // fn_55_CC84
  void ScanVisorRender(const CStateManager& mgr, const CTransform4f& xf,
                       const CModelFlags& flags) const override;  // fn_55_CB30
  const rstl::optional_object< TCachedToken< CGenDescription > >&
  GetDeathExplosionParticle() const override; // fn_55_A4

  // Triggers (lbl_55_data_100, PortCSandBossStates.cpp)
  bool StateOver(CStateManager& mgr, const CTriggerData& data) const;            // fn_55_CA90
  bool IsUnderGround(CStateManager& mgr, const CTriggerData& data) const;        // fn_55_CA84
  bool IsRecovered(CStateManager& mgr, const CTriggerData& data) const;          // fn_55_CA70
  bool CanStartNewRound(CStateManager& mgr, const CTriggerData& data) const;     // fn_55_CA1C
  bool BeginStampede(CStateManager& mgr, const CTriggerData& data) const;        // fn_55_CA10
  bool FoundStampedePoint(CStateManager& mgr, const CTriggerData& data) const;   // fn_55_C9D8
  bool StampedeOver(CStateManager& mgr, const CTriggerData& data) const;         // fn_55_C9B8
  bool ShouldDarkBeamAttack(CStateManager& mgr, const CTriggerData& data) const; // fn_55_C894
  bool ShouldRepeaterAttack(CStateManager& mgr, const CTriggerData& data) const; // fn_55_C750
  bool FoundSafeZoneTarget(CStateManager& mgr, const CTriggerData& data) const;  // fn_55_C730
  bool ShouldTripleCharge(CStateManager& mgr, const CTriggerData& data) const;   // fn_55_C534
  bool ShouldDoubleCharge(CStateManager& mgr, const CTriggerData& data) const;   // fn_55_C378
  bool ShouldSnapJaws(CStateManager& mgr, const CTriggerData& data) const;       // fn_55_C2A0
  bool ShouldDestroySphere(CStateManager& mgr, const CTriggerData& data) const;  // fn_55_C258
  bool IsFacingPlayer(CStateManager& mgr, const CTriggerData& data) const;       // fn_55_C1CC
  bool IsBallInSuckRange(CStateManager& mgr, const CTriggerData& data) const;    // fn_55_C188
  bool IsLastStage(CStateManager& mgr, const CTriggerData& data) const;          // fn_55_C174
  bool HeadArmorDestroyed(CStateManager& mgr, const CTriggerData& data) const;   // fn_55_C154
  bool ArmorDestroyed(CStateManager& mgr, const CTriggerData& data) const;       // fn_55_C148
  bool SyncStampede(CStateManager& mgr, const CTriggerData& data) const;         // fn_55_C0A0
  bool SyncAttachToSphere(CStateManager& mgr, const CTriggerData& data) const;   // fn_55_BFF8

  // States (lbl_55_data_340, PortCSandBossStates.cpp)
  void Recover(CStateManager& mgr, EStateMsg msg, float dt);             // fn_55_BF64
  void UnderGround(CStateManager& mgr, EStateMsg msg, float dt);         // fn_55_BEA4
  void Dead(CStateManager& mgr, EStateMsg msg, float dt);                // fn_55_BDAC
  void Deactivate(CStateManager& mgr, EStateMsg msg, float dt);          // fn_55_BD30
  void AttachToSphere(CStateManager& mgr, EStateMsg msg, float dt);      // fn_55_BAAC
  void RotateToPlayer(CStateManager& mgr, EStateMsg msg, float dt);      // fn_55_B98C
  void SuckAir(CStateManager& mgr, EStateMsg msg, float dt);             // fn_55_B718
  void SuckBall(CStateManager& mgr, EStateMsg msg, float dt);            // fn_55_B658
  void BallInMouth(CStateManager& mgr, EStateMsg msg, float dt);         // fn_55_B544
  void DestroySphere(CStateManager& mgr, EStateMsg msg, float dt);       // fn_55_B2D8
  void JumpOffSphere(CStateManager& mgr, EStateMsg msg, float dt);       // fn_55_B06C
  void SpitOutBall(CStateManager& mgr, EStateMsg msg, float dt);         // fn_55_AED8
  void DarkBeamAttack(CStateManager& mgr, EStateMsg msg, float dt);      // fn_55_AC68
  void RepeaterAttack(CStateManager& mgr, EStateMsg msg, float dt);      // fn_55_A9DC
  void DoubleCharge(CStateManager& mgr, EStateMsg msg, float dt);        // fn_55_A6CC
  void TripleCharge(CStateManager& mgr, EStateMsg msg, float dt);        // fn_55_A360
  void SnapJaws(CStateManager& mgr, EStateMsg msg, float dt);            // fn_55_A170
  void SelectStampedePoint(CStateManager& mgr, EStateMsg msg, float dt); // fn_55_9E3C
  void SeekStampedePoint(CStateManager& mgr, EStateMsg msg, float dt);   // fn_55_9BB8
  void Stampede(CStateManager& mgr, EStateMsg msg, float dt);            // fn_55_98FC

  // Code functions (lbl_55_data_5AC, PortCSandBossStates.cpp)
  void SetTripleChargeArmor(CStateManager& mgr, float arg);         // fn_55_986C
  void SetDoubleChargeArmor(CStateManager& mgr, float arg);         // fn_55_97DC
  void SetStampedeArmor(CStateManager& mgr, float arg);             // fn_55_974C
  void SetAttachedArmor(CStateManager& mgr, float arg);             // fn_55_96B0
  void SetSuckAirArmor(CStateManager& mgr, float arg);              // fn_55_9600
  void RemovePlayerCollision(CStateManager& mgr, float arg);        // fn_55_952C
  void RestorePlayerCollision(CStateManager& mgr, float arg);       // fn_55_9438
  void SelectSafeZoneTarget(CStateManager& mgr, float arg);         // fn_55_920C
  void WaitStampede(CStateManager& mgr, float arg);                 // fn_55_9200
  void WaitAttachToSphere(CStateManager& mgr, float arg);           // fn_55_91F4
  void LockSphere(CStateManager& mgr, float arg);                   // fn_55_91E0
  void UnLockSphere(CStateManager& mgr, float arg);                 // fn_55_91CC
  void AllowSyncAttacks(CStateManager& mgr, float arg);             // fn_55_91B8
  void PreventSyncAttacks(CStateManager& mgr, float arg);           // fn_55_91A4
  void BeginStampedeCode(CStateManager& mgr, float arg);            // fn_55_9190 ("BeginStampede")
  void EndStampede(CStateManager& mgr, float arg);                  // fn_55_917C
  void PlayHeadArmorExplosion(CStateManager& mgr, float arg);       // fn_55_8F94
  void ResetHeadArmorHP(CStateManager& mgr, float arg);             // fn_55_8F88
  void ResetAttackTimes(CStateManager& mgr, float arg);             // fn_55_8E60
  void SetCinematicStateUnderGround(CStateManager& mgr, float arg); // fn_55_8E54
  void SetCinematicStateStampede(CStateManager& mgr, float arg);    // fn_55_8E48
  void SetCinematicStateNormal(CStateManager& mgr, float arg);      // fn_55_8E3C
  void SetCinematicStateStunned(CStateManager& mgr, float arg);     // fn_55_8E30
  void SetCinematicStateChoking(CStateManager& mgr, float arg);     // fn_55_8E24
  void SetCinematicStateExitSphere(CStateManager& mgr, float arg);  // fn_55_8E18

private:
  // Slice A helpers (PortCSandBoss.cpp)
  void UpdateArmorResponseTypes(CStateManager& mgr);                       // fn_55_6698
  void SetArmorPieceState(const rstl::string& locator, bool on);           // fn_55_6960
  void BuildArmorModels();                                                 // fn_55_6A68
  void FindConnections(CStateManager& mgr);                                // fn_55_6EA0
  void SyncCollisionHealth(CStateManager& mgr);                            // fn_55_70D0
  void SetupCollisionActors(CStateManager& mgr);                           // fn_55_724C
  void SetCollisionVulnerabilities(CStateManager& mgr, const CDamageVulnerability& head,
                                   const CDamageVulnerability& body);      // fn_55_783C
  void HandleHitObject(CStateManager& mgr, TUniqueId sender);              // fn_55_79B0
  void UpdateFlashColor(float dt);                                         // fn_55_7BB0
  void UpdateGroupCinematicState(CStateManager& mgr);                      // fn_55_84A8
  void UpdateTimers(float dt);                                             // fn_55_86D0
  CAABox GetArmoredBounds() const;                                         // fn_55_8740
  void RenderSphere(const CStateManager& mgr, const CTransform4f& xf,
                    const CModelFlags& flags) const;                       // fn_55_88D0
  void RenderWithArmor(const CStateManager& mgr, const CTransform4f& xf,
                       const CModelFlags& flags) const;                    // fn_55_89E8
  void RenderArmor(const CStateManager& mgr, const CTransform4f& xf, const CModelFlags& bodyFlags,
                   const CModelFlags& headFlags) const;                    // fn_55_8A8C
  int CountAttachedSiblings(const CStateManager& mgr) const;               // fn_55_595C
  int CollectAttachedSiblings(CStateManager& mgr,
                              rstl::vector< TUniqueId >& ids) const;    // fn_55_58B4
  bool NoSiblingHasSphereLocked(const CStateManager& mgr) const;           // fn_55_59E4
  bool IsAttachedLeader(const CStateManager& mgr) const;                   // fn_55_5A84
  bool ProbeDarkBeam(CStateManager& mgr);                                  // fn_55_8CB4
  bool ProbeTripleCharge(CStateManager& mgr);                              // fn_55_8D24
  bool ProbeDoubleCharge(CStateManager& mgr);                              // fn_55_8D94

  // Helpers (PortCSandBossStates.cpp)
  void SnapJawsCollisionSetup(CStateManager& mgr, const CVector3f& bounds); // fn_55_78E0
  bool CheckBodyStateForDarkBeam(CStateManager& mgr) const;                // fn_55_8C88

  // Stand-ins (PortCSandBossAttacks.cpp)
  float GetStampedeDelay(CStateManager& mgr) const;                        // fn_55_1AEC
  int ShouldRepeaterAttackCheck(CStateManager& mgr) const;                 // fn_55_1B94
  void SyncAttacker(CStateManager& mgr, int delta);                        // fn_55_1C70
  void RotateToPlayerStep(CStateManager& mgr, const CVector3f& target, float dt); // fn_55_22E4
  void RotateHelper1D74(CStateManager& mgr, const CVector3f& target, float dt,
                        TUniqueId siblingId);                               // fn_55_1D74
  void FaceTowardPoint(float dt, const CVector3f& target);                 // fn_55_1F84
  void RotateHelper2030(CStateManager& mgr, const CVector3f& target, float dt, TUniqueId firstId,
                        TUniqueId secondId);                               // fn_55_2030
  void RotateHelper2498(rstl::vector< TUniqueId >& ids, int index);       // fn_55_2498
  int RotateHelper254C(CStateManager& mgr, const rstl::vector< TUniqueId >& ids); // fn_55_254C
  int RotateHelper25D0(CStateManager& mgr, const rstl::vector< TUniqueId >& ids,
                       const CVector3f& target);                           // fn_55_25D0
  int CountActiveChargeBeams(CStateManager& mgr);                          // fn_55_26C0
  void UpdateChargeBeams(CStateManager& mgr, float dt);                    // fn_55_2750
  void ResetChargeBeams(CStateManager& mgr);                               // fn_55_2CA4
  void CreateChargeBeam(CStateManager& mgr, const CVector3f& start, const CVector3f& end,
                        ushort sfxId, bool charged);                       // fn_55_2D40
  void StartChargeBeamGrow(CStateManager& mgr, const CVector3f& position); // fn_55_311C
  void EndChargeBeamVolley(CStateManager& mgr);                            // fn_55_31E8
  void UpdateChargeBeamRetarget(CStateManager& mgr, float dt,
                                const SLdrSandBossChargeBeamData& data);   // fn_55_3324
  void UpdateTripleChargeSweep(CStateManager& mgr, float dt);              // fn_55_3434
  void UpdateDoubleChargeSweep(CStateManager& mgr, float dt);              // fn_55_37F4
  void StartTripleChargeBeam(CStateManager& mgr, const rstl::string& locator); // fn_55_3ACC
  void StartDoubleChargeBeam(CStateManager& mgr, const rstl::string& locator); // fn_55_3F44
  TUniqueId ShouldDarkBeamAttackCheck(CStateManager& mgr) const;           // fn_55_42A8
  void FireDarkBeam(CStateManager& mgr, float dt);                         // fn_55_44CC
  void HandleScreenShake(CStateManager& mgr, const rstl::string& locator); // fn_55_4C60
  void DestroySphereHelper(CStateManager& mgr);                            // fn_55_51BC
  void HandleGroundImpact(CStateManager& mgr, const rstl::string& locator); // fn_55_528C
  void CrackSphere(CStateManager& mgr);                                    // fn_55_560C
  bool IsBallInRange(const CPlayer& player) const;                         // fn_55_5B38
  float AttachToSphereHelper();                                            // fn_55_5C30
  void Helper5C6C(CStateManager& mgr, const CVector3f& dir);               // fn_55_5C6C
  void Helper5E5C(CStateManager& mgr);                                     // fn_55_5E5C
  void HandleObjectDrop(CStateManager& mgr);                               // fn_55_5FEC
  void SphereStateHelper6250(CPlayer& player);                             // fn_55_6250
  bool SphereStateHelper62E4(float dt, CPlayer& player);                   // fn_55_62E4
  void SphereStateHelper6434(float dt);                                    // fn_55_6434
  void Helper64E4(CStateManager& mgr, const CTransform4f& locatorXf);       // fn_55_64E4
  void StampedeHelper(CStateManager& mgr, float dt);                       // fn_55_7C68
  void Helper7E60(float dt, CStateManager& mgr, const CVector3f& pos,
                  const CVector3f& dir); // fn_55_7E60
  void Helper8008(float dt, CStateManager& mgr, const CVector3f& pos,
                  const CVector3f& dir); // fn_55_8008
  void HandleDamage(CStateManager& mgr, TUniqueId sender);                 // fn_55_8258
  void RotateToPlayerHelper(const CVector3f& target);                      // fn_55_83AC
  void SortStampedePoints();                                               // fn_55_10CB4

  // Host layout (retail's is 0x1660, CHECK_SIZEOF below; CPatterned is 0x7c0). Guest offsets are
  // fn_55_E170's.
  SLdrSandBossData mData;                                      // 0x7C0 (retail copy, 0x5D8)
  rstl::single_ptr< CCollisionActorManager > mCollisionManager; // 0xD98
  CBoneTracking mBoneTracking;                                 // 0xD9C ("Head_1")
  rstl::reserved_vector< TUniqueId, 4 > mSiblings;             // 0xDD8 (count) / 0xDDC (ids)
  int mRound;                                                  // 0xDE4 (0..2)
  float mPInfoTurnSpeed;                                       // 0xDE8 (pInfo turn speed)
  rstl::single_ptr< TCachedToken< CScannableObjectInfo > > mScanInfo; // 0xDEC (scannableInfo1)
  int mWaitMode;                                               // 0xDF0 (1 stampede, 2 sphere)
  int mCinematicState;                                         // 0xDF4 (1..6)
  int mGroupCinematicState;                                    // 0xDF8
  int mChargeBeamPhase;                                        // 0xDFC (-1 none, 0 grow, 1 hold, 2 shrink)
  CProjectileInfo mDarkBeamInfo;                               // 0xE00
  CProjectileInfo mChargeBeamInfo;                             // 0xE28
  rstl::optional_object< TLockedToken< CGenDescription > > mHeadArmorExplosion;     // 0xE50
  rstl::optional_object< TLockedToken< CGenDescription > > mStampedeArmorExplosion; // 0xE60
  rstl::optional_object< TLockedToken< CGenDescription > > mStampedeSandFountain;   // 0xE70
  TUniqueId mIS02Target;      // 0xE80 (IS02 / FOLW)
  TUniqueId mIS01Target;      // 0xE82 (IS01 / FOLW)
  TUniqueId mWaypoint;        // 0xE84 (IS00 / FOLW; ObjectPickUp moves it)
  TUniqueId mBossInfoSource;  // 0xE86 (MDFY / ACTV; its HP seeds SetBossParams)
  TUniqueId mSafeZoneTarget;  // 0xE88
  TUniqueId mSphere;          // 0xE8A (CONN / ATCH)
  TUniqueId mHeadCollider;    // 0xE8C (the Head_1 collision actor)
  TUniqueId mStampedePoint;   // 0xE8E
  TUniqueId mCoverPoint;      // 0xE90
  rstl::reserved_vector< SSandBossChargeBeamEntry, 3 > mChargeBeams; // 0xE94 (count) / 0xE98 (data)
  TUniqueId mScriptEffect;    // 0xEF8 (the charge-beam CScriptEffect: ACTV/DCTV, global scale)
  int mEFC;                   // 0xEFC (8; meaning unknown)
  CSegId mArmorLocators[8];   // 0xF00 (lbl_55_rodata_48 names)
  CSegId mHeadLocator;        // 0xF08 ("Head_1")
  CColor mFlashColor;         // 0xF0C
  float mFlashTime;           // 0xF10
  float mChargeBeamPhaseTimer; // 0xF14 (grow counts down from 0.6 s, shrink from shutdownTime)
  float mChargeBeamTimer;     // 0xF18
  float mDarkBeamTimer;       // 0xF1C
  float mF20;                 // 0xF20 (stampede wait timer; compared against fn_55_1AEC)
  float mStampedeHP;          // 0xF24
  float mF28;                 // 0xF28 (stampede waypoint z)
  float mHeadArmorHP;         // 0xF2C
  float mF30;                 // 0xF30 (stampede piece timer, 0.2 s gate in fn_55_7C68)
  float mF34;                 // 0xF34 (counts up in fn_55_86D0)
  float mChargeBeamSweepPhase; // 0xF38 (sweep angle in radians, seeded to -pi/2)
  int mChargeBeamSweepDir;     // 0xF3C (1 or 0: which way the sweep turns, fn_55_3324 flips it)
  float mChargeBeamRetargetTimer; // 0xF40 (seconds until the sweep may change direction)
  int mChargeBeamCooldown;     // 0xF44 (blocks a new volley while >= 1; set from the loader at end)
  int mF48;                   // 0xF48 (0; type unknown)
  // Retail holds these three as reserved_vectors whose count the constructor sets to 8 and nothing
  // changes, so the host keeps them as fixed arrays. Armour slots start empty; fn_55_6A68 fills
  // attached slots 0..6 (attachedArmor headArmor..armorPiece7) and stampede slots 0..7
  // (stampedeArmor headArmor..tailArmor).
  rstl::optional_object< CModelData > mAttachedArmorModels[8]; // 0xF4C
  rstl::optional_object< CModelData > mStampedeArmorModels[8]; // 0x11D0
  int mArmorState[8]; // 0x1458 (retail reserved_vector<int,8>: count 0x1454, data 0x1458+4i;
                      //            per locator: 0 none, 1 stampede set, 2 attached set)
  rstl::vector< SSandBossIdEntry > mIdEntries; // 0x1478
  TLockedToken< CSkinnedModel > mNormalSkinnedModel;   // 0x1488
  TLockedToken< CSkinnedModel > mTailArmorSkinnedModel; // 0x1494
  CTransform4f m14A0;                                  // 0x14A0 (the held ball's rotation:
                                                       //        fn_55_6250 stores the player's)
  CQuaternion mSpawnRotation;                          // 0x14D0
  CDamageInfo mDoubleChargeDamage;                     // 0x14E0
  CDamageInfo mTripleChargeDamage;                     // 0x14FC
  CDamageInfo mStampedeDamage;                         // 0x1518
  CDamageInfo mSnapJawDamage;                          // 0x1534
  CDamageInfo mSpitOutDamage;                          // 0x1550
  CDamageVulnerability mNormalVulnerability;           // 0x156C
  CDamageVulnerability mStampedeVulnerability;         // 0x159C
  CDamageVulnerability mSuckAirVulnerability;          // 0x15CC
  CDamageVulnerability mReflectVulnerability0;         // 0x15FC (ReflectVulnerabilty())
  CDamageVulnerability mReflectVulnerability1;         // 0x162C (ReflectVulnerabilty())
  // 0x165C
  bool mUnderGround;          // b7
  bool mBeginStampede;        // b6
  bool mSnapJawContact;       // b5 (fn_55_79B0 applies the snap-jaw damage)
  bool mStampedeActive;       // b4
  bool mDarkBeamResult;       // b3 (probe result)
  bool m165C_b2;              // b2 (unknown)
  bool mDoubleChargeResult;   // b1 (probe result)
  bool mTripleChargeResult;   // b0 (probe result)
  // 0x165D
  bool mAttachedToSphere;     // b7 (IkLock sets, IkRelease clears)
  bool mArmorDestroyed;       // b6
  bool mNotRecovered;         // b5
  bool mSuckAir;              // b4
  bool mHoldingBall;          // b3 [inferred]
  bool mSkipParticleRender;   // b2
  bool m165D_b1;              // b1 (1 in the constructor; unknown)
  bool mSphereLocked;         // b0
  // 0x165E
  bool mProbingDoubleCharge;  // b7
  bool mProbingTripleCharge;  // b6
  bool mProbingDarkBeam;      // b5
  bool mSyncAttacksAllowed;   // b4
  bool m165E_b3 = false;      // b3 (set while SpitOutBall runs; fn_55_5FEC / fn_55_8008 read it;
                              //     the constructor in PortCSandBoss.cpp predates it)

#else
  uchar x7c0_[0x1660 - 0x7c0];
#endif
public:
  // CEntity
  CEntity* TypesMatch(int typeId) const override;
};
CHECK_SIZEOF(CSandBoss, 0x1660)

#endif
