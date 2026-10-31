#ifndef _CDIGITALGUARDIAN
#define _CDIGITALGUARDIAN

#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "REL/REL_Setup.h"

#include "Kyoto/Audio/CSfxHandle.hpp"
#include "rstl/optional_object.hpp"

class CCollisionActorManager;
class CDigitalGuardianHead;
class CElementGen;
class CParticleGen;
class CGenDescription;

#ifdef TARGET_PC
#include "Kyoto/TToken.hpp"
#include "rstl/single_ptr.hpp"
#include "MetroidPrime/CActorParameters.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/ScriptLoader/Structs/SLdrAudioPlaybackParms.hpp"
#include "MetroidPrime/ScriptLoader/Structs/SLdrDamageInfo.hpp"
#include "MetroidPrime/ScriptLoader/Structs/SLdrDamageVulnerability.hpp"
#include "MetroidPrime/ScriptLoader/Structs/SLdrEchoParameters.hpp"
#include "MetroidPrime/ScriptLoader/Structs/SLdrShockWaveInfo.hpp"
#include "Kyoto/Animation/CPASAnimParmData.hpp"

// Host copy of the module's parameter block (SLdrDigitalGuardianData). Retail reads it into a
// stack struct and copies it into the object verbatim (fn_14_C68C, guest 0x7C0, 0x640 bytes), so
// the host keeps the loader's own types: one member per row of
// docs/module-analysis/DigitalGuardian.md section c.2, named after the SLdr property it holds.
// Nothing in this milestone reads the values - the class keeps them for the state machine the
// later milestones add - so the fields are only a faithful copy of what the loader read.
class CDigitalGuardianData {
public:
  CDigitalGuardianData()
  : mScannableInfoCrippled(kInvalidAssetId)
  , mNearLegStabRange(0.f)
  , mFarLegStabRange(0.f)
  , mMinLegStabAttackTime(0.f)
  , mLegStabDamage()
  , mToeTargetHP(0.f)
  , mToeTargetModel(kInvalidAssetId)
  , mToeTargetExplosion(kInvalidAssetId)
  , mSoundToeTarget()
  , mSoundToeTargetAttack()
  , mSoundToeTargetExplosion()
  , mSoundToeTargetHit()
  , mSoundShockWave()
  , mShockWaveInfo()
  , mVortexAttackDuration(0.f)
  , mVortexAttractionForce(0.f)
  , mMaxVortexAttractionDistance(0.f)
  , mVortexLinearVelocity(0.f)
  , mVortexLinearAcceleration(0.f)
  , mVortexDamage()
  , mUnknownFB5263E8(0)
  , mUnknown6AAF33E3(0)
  , mUnknown4F5D725C(0.f)
  , mSoundVortexFlash()
  , mLegModel(kInvalidAssetId)
  , mShinArmor(kInvalidAssetId)
  , mKneeArmorHP(0.f)
  , mSoundKneeArmorHit()
  , mSoundKneeVulnerable()
  , mKneeArmor(kInvalidAssetId)
  , mKneeEchoParameters()
  , mUnknownA324E26C(0.f)
  , mUnknown6A754EBD(0.f)
  , mJumpTimer(0.f)
  , mUnknown8106CDA9(0.f)
  , mUnknown9E1B8105(0.f)
  , mUnknownA08FCC70(0.f)
  , mUnknown3254A16B(0.f)
  , mTransmissionBeacon(kInvalidAssetId)
  , mTransmissionBeaconFx(kInvalidAssetId)
  , mTransmissionBeaconEchoParameters()
  , mSoundTransmissionBeacon()
  , mSoundBeaconExtract()
  , mSoundBeaconRetract()
  , mTransmissionBeaconHP(0.f)
  , mTransmissionBeaconExplosion(kInvalidAssetId)
  , mTransmissionBeaconEchoHit(kInvalidAssetId)
  , mSoundBeaconExplode()
  , mSoundBeaconHit()
  , mKneeVulnerability()
  , mVortexVulnerability()
  , mToeTargetVulnerability() {}

  CAssetId mScannableInfoCrippled;       // 0x00 scannableInfoCrippled
  float mNearLegStabRange;               // 0x04 nearLegStabRange
  float mFarLegStabRange;                // 0x08 farLegStabRange
  float mMinLegStabAttackTime;           // 0x0C minLegStabAttackTime
  SLdrDamageInfo mLegStabDamage;         // 0x10 legStabDamage
  float mToeTargetHP;                    // 0x20 toeTargetHP
  CAssetId mToeTargetModel;              // 0x24 toeTargetModel
  CAssetId mToeTargetExplosion;          // 0x28 toeTargetExplosion
  SLdrAudioPlaybackParms mSoundToeTarget;         // 0x2C sound_ToeTarget
  SLdrAudioPlaybackParms mSoundToeTargetAttack;   // 0x44 sound_ToeTargetAttack
  SLdrAudioPlaybackParms mSoundToeTargetExplosion; // 0x5C sound_ToeTargetExplosion
  SLdrAudioPlaybackParms mSoundToeTargetHit;      // 0x74 sound_ToeTargetHit
  SLdrAudioPlaybackParms mSoundShockWave;         // 0x8C sound_ShockWave
  SLdrShockWaveInfo mShockWaveInfo;               // 0xA4 shockWaveInfo
  float mVortexAttackDuration;           // 0xD4 vortexAttackDuration
  float mVortexAttractionForce;          // 0xD8 vortexAttractionForce
  float mMaxVortexAttractionDistance;    // 0xDC maxVortexAttractionDistance
  float mVortexLinearVelocity;           // 0xE0 vortexLinearVelocity
  float mVortexLinearAcceleration;       // 0xE4 vortexLinearAcceleration
  SLdrDamageInfo mVortexDamage;          // 0xE8 vortexDamage
  int mUnknownFB5263E8;                  // 0xF8 unknown_0xfb5263e8
  int mUnknown6AAF33E3;                  // 0xFC unknown_0x6aaf33e3
  float mUnknown4F5D725C;                // 0x100 unknown_0x4f5d725c
  SLdrAudioPlaybackParms mSoundVortexFlash;      // 0x104 sound_VortexFlash
  CAssetId mLegModel;                    // 0x11C legModel
  CAssetId mShinArmor;                   // 0x120 shinArmor
  float mKneeArmorHP;                    // 0x124 kneeArmorHP
  SLdrAudioPlaybackParms mSoundKneeArmorHit;     // 0x128 sound_KneeArmorHit
  SLdrAudioPlaybackParms mSoundKneeVulnerable;   // 0x140 sound_KneeVulnerable
  CAssetId mKneeArmor;                   // 0x158 kneeArmor
  SLdrEchoParameters mKneeEchoParameters;         // 0x15C kneeEchoParameters
  float mUnknownA324E26C;                // 0x170 unknown_0xa324e26c
  float mUnknown6A754EBD;                // 0x174 unknown_0x6a754ebd
  float mJumpTimer;                      // 0x178 jumpTimer
  float mUnknown8106CDA9;                // 0x17C unknown_0x8106cda9
  float mUnknown9E1B8105;                // 0x180 unknown_0x9e1b8105
  float mUnknownA08FCC70;                // 0x184 unknown_0xa08fcc70
  float mUnknown3254A16B;                // 0x188 unknown_0x3254a16b
  CAssetId mTransmissionBeacon;          // 0x18C transmissionBeacon
  CAssetId mTransmissionBeaconFx;        // 0x190 transmissionBeaconFx
  SLdrEchoParameters mTransmissionBeaconEchoParameters; // 0x194 transmissionBeaconEchoParameters
  SLdrAudioPlaybackParms mSoundTransmissionBeacon;  // 0x1A8 sound_TransmissionBeacon
  SLdrAudioPlaybackParms mSoundBeaconExtract;       // 0x1C0 sound_BeaconExtract
  SLdrAudioPlaybackParms mSoundBeaconRetract;       // 0x1D8 sound_BeaconRetract
  float mTransmissionBeaconHP;           // 0x1F0 transmissionBeaconHP
  CAssetId mTransmissionBeaconExplosion; // 0x1F4 transmissionBeaconExplosion
  CAssetId mTransmissionBeaconEchoHit;   // 0x1F8 transmissionBeaconEchoHit
  SLdrAudioPlaybackParms mSoundBeaconExplode;       // 0x1FC sound_BeaconExplode
  SLdrAudioPlaybackParms mSoundBeaconHit;           // 0x214 sound_BeaconHit
  SLdrDamageVulnerability mKneeVulnerability;    // 0x22C kneeVulnerability
  SLdrDamageVulnerability mVortexVulnerability;  // 0x388 vortexVulnerability
  SLdrDamageVulnerability mToeTargetVulnerability; // 0x4E4 toeTargetVulnerability
};
#endif

// The REL's CDigitalGuardian (module 14): the Sanctuary Fortress Guardian boss body, script type
// DGRD ("Mechanism: Quadraxis"). 0x1290 bytes on the GameCube (the loader's __nw__ at
// 0x000003F0), built by fn_14_B100; its vtable lbl_14_data_484 (82 words, 33 local overrides)
// and the analysis are docs/module-analysis/DigitalGuardian.md.
//
// Milestone 1 (this file's first slice) is load-and-think: the loader, the constructor and the
// vtable's accessors, so the actor registers and thinks. The state machine (fn_14_9DFC installs
// the 12 triggers, 14 states and 13 codes), the vortex, the knee armour, the transmission
// beacon, the toe targets and the head module (CDigitalGuardianHead, guest 0x10F8) are later
// milestones, and every override that belongs to them forwards to CPatterned behind a once-only
// StandIn log.
class REL_EXPORT CDigitalGuardian : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
#ifdef TARGET_PC
  // Argument order is the port's own (the REL loader's call at 0x00000468 is
  // (uid, editor properties, entity info, transform, model, actor parameters, patterned info)
  // plus the parameter block on the stack).
  CDigitalGuardian(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                   const CTransform4f& xf, const CModelData& modelData,
                   const CActorParameters& actParms, const CPatternedInfo& pInfo,
                   const CDigitalGuardianData& data);
  ~CDigitalGuardian() override;
  // Host: the console's `var <id> dg.<name> [value]` for the body (platform/port_console.cpp):
  // dg.flags 0x128C, dg.flags2 0x128D, dg.vortex 0x11B8, dg.vortexhp 0x11C8, dg.knee 0x1284,
  // dg.facex/facey/facez 0x1278/0x127C/0x1280, dg.beacon 0x1268, dg.jumptime 0x11E4.
  // dg.toe0..3 0x1078, dg.toestate0..3 0x10DC, dg.shaker 0x11C0, dg.armor0..3 0xFB0, dg.exit
  // 0x1168, dg.follow 0x116A, dg.root 0x1080, dg.beaconh 0x1264 (thousandths).
  static bool PortConsoleVar(CEntity& ent, const char* name, const int* value, int& current);

  // CEntity / CActor
  void PreThink(float dt, CStateManager& mgr) override;
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  void Render(const CStateManager& mgr) const override;
  void PreRenderAllViewports(CStateManager& mgr) override;
  const CHealthInfo* GetHealthInfo() const override;
  const CDamageVulnerability* GetDamageVulnerability() const override;
  rstl::optional_object< CAABox > GetTouchBounds() const override;
  CVector3f GetAimPosition(const CStateManager& mgr, float dt) const override;
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;
  CScannableObjectInfo* GetScannableObjectInfo() const override;

  // CAi
  void TakeDamage(const CVector3f& direction, float magnitude) override;
  bool CanBeShot(const CStateManager&, int) override { return true; }
  bool IsListening() const override { return true; }
  bool Listen(CStateManager&, const CVector3f&, EListenNoiseType) override { return false; }
  CVector3f GetOrigin(const CStateManager&, const CTeamAiRole&, const CVector3f&) const override;

  // CPatterned. The accessors below are the REL's own one-line bodies (the module's .text starts
  // with them), so they are real, not stand-ins.
  void RenderSystemsToBeDrawnLast(const CStateManager& mgr, uint mask, uint target) const override;
  uchar GetModelAlphau8(const CStateManager&) const override { return mColor.GetAlphau8(); }
  CProjectileInfo* ProjectileInfo() override { return nullptr; }
  using CPatterned::GetSearchPath;
  CPathFindSearch* GetSearchPath() override { return nullptr; }
  using CPatterned::GetPointSearchPath;
  CPathFindPointSearch* GetPointSearchPath() override { return nullptr; }
  void SetupStateMachine(CStateManager& mgr) override;
  CRagDoll* GetRagDoll() const override { return nullptr; }
  TUniqueId GetAttackTarget() const override { return kInvalidUniqueId; }
  bool IsOnGround() const override { return CPatterned::IsOnGround(); }
  float GetGravityConstant() const override { return kDefaultGravityAccel; }
  bool IsScanVisorSelfRender() const override { return true; }
  CAABox GetScanVisorRenderBounds(const CStateManager&) const override;
  void ScanVisorRender(const CStateManager&, const CTransform4f&, const CModelFlags&) const override;
  const rstl::optional_object< TCachedToken< CGenDescription > >&
  GetDeathExplosionParticle() const override;

  // The body's state machine. Retail fn_14_9DFC (vtable slot 60) calls the base's setup and
  // then registers the three tables fn_14_D904 builds: 12 triggers (lbl_14_data_9C), 14 states
  // (lbl_14_data_204) and 13 codes (lbl_14_data_380). The names are retail's - the state
  // machine asset's nodes are matched by name - and each comment is the retail function the
  // entry points at. The bodies are stand-ins in this slice; a later one ports them.
  //
  // Triggers (lbl_14_data_9C, 12 entries).
  bool StateOver(CStateManager& mgr, const CTriggerData& data) const;         // fn_14_9AA8
  bool IsIdle(CStateManager& mgr, const CTriggerData& data) const;             // fn_14_9A9C
  bool HasFaceTarget(CStateManager& mgr, const CTriggerData& data) const;      // fn_14_9A60
  bool KneeDestroyed(CStateManager& mgr, const CTriggerData& data) const;      // fn_14_9A48
  bool AllKneesDestroyed(CStateManager& mgr, const CTriggerData& data) const;  // fn_14_9A0C
  bool ShouldProtectKnees(CStateManager& mgr, const CTriggerData& data) const; // fn_14_9994
  bool ShouldVortexAttack(CStateManager& mgr, const CTriggerData& data) const; // fn_14_9930
  bool ShouldMeleeAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_14_97F0
  bool ShouldProjectileAttack(CStateManager& mgr,
                              const CTriggerData& data) const;                 // fn_14_97A4
  bool ShouldJumpAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_14_9678
  bool VortexStruckPlayer(CStateManager& mgr, const CTriggerData& data) const; // fn_14_966C
  bool BreakOutOfVortex(CStateManager& mgr, const CTriggerData& data) const;  // fn_14_9638

  // States (lbl_14_data_204, 14 entries). Start hides CPatterned::Start, which is what retail
  // does too: the module's table replaces the base's "Start" entry.
  void Start(CStateManager& mgr, EStateMsg msg, float dt);                     // fn_14_9634
  void Idle(CStateManager& mgr, EStateMsg msg, float dt);                      // fn_14_95B8
  void FaceTarget(CStateManager& mgr, EStateMsg msg, float dt);                // fn_14_94BC
  void VortexAttack(CStateManager& mgr, EStateMsg msg, float dt);              // fn_14_9260
  void VortexEndAttack(CStateManager& mgr, EStateMsg msg, float dt);           // fn_14_9164
  void VortexRetreat(CStateManager& mgr, EStateMsg msg, float dt);             // fn_14_9070
  void ExitVortexAttack(CStateManager& mgr, EStateMsg msg, float dt);          // fn_14_8E44
  void MeleeAttackTelegraph(CStateManager& mgr, EStateMsg msg, float dt);      // fn_14_8CCC
  void MeleeAttack(CStateManager& mgr, EStateMsg msg, float dt);               // fn_14_89D4
  void ProjectileAttack(CStateManager& mgr, EStateMsg msg, float dt);          // fn_14_8794
  void JumpAttack(CStateManager& mgr, EStateMsg msg, float dt);                // fn_14_8580
  void KneeDestroyedReaction(CStateManager& mgr, EStateMsg msg, float dt);     // fn_14_8090
  void CrippledReaction(CStateManager& mgr, EStateMsg msg, float dt);          // fn_14_7D8C
  void Crippled(CStateManager& mgr, EStateMsg msg, float dt);                  // fn_14_7CB4

  // Codes (lbl_14_data_380, 13 entries).
  void SetupBossEnergyBar(CStateManager& mgr, float dt);                       // fn_14_7BD4
  void ResetProtectKneesTime(CStateManager& mgr, float dt);                    // fn_14_7B90
  void ResetJumpTime(CStateManager& mgr, float dt);                            // fn_14_7B84
  void ResetAttackTimes(CStateManager& mgr, float dt);                         // fn_14_7B24
  void SetProtectKneesFaceTarget(CStateManager& mgr, float dt);               // fn_14_77EC
  void SetVortexAttackMode(CStateManager& mgr, float dt);                      // fn_14_775C
  void SetNormalAttackMode(CStateManager& mgr, float dt);                      // fn_14_76D0
  void DisableHeadAttacks(CStateManager& mgr, float dt);                       // fn_14_768C
  void ResetHeadAttacks(CStateManager& mgr, float dt);                         // fn_14_7648
  void SelectExitVortexTarget(CStateManager& mgr, float dt);                   // fn_14_7600
  void SelectBestMeleeAttackLeg(CStateManager& mgr, float dt);                // fn_14_7504
  void SelectMeleeAttackRange(CStateManager& mgr, float dt);                  // fn_14_73D4
  void SetJumpFaceTarget(CStateManager& mgr, float dt);                        // fn_14_7208

  // fn_14_7168 (0x9C bytes): the head links itself ("XALD", the head's fn_14_13830): its id goes
  // to 0x1084 and 0x1082, 0x1086 is cleared, the body stops being a target and its attack times
  // reset, and the head's attack mode becomes 0 (fn_14_13B30).
  void x7168_(CStateManager& mgr, TUniqueId head);
  // fn_14_70E4 (0x84 bytes): the head detaches (its DetachFromBody state calls it).
  void x70E4_(CStateManager& mgr);
#endif
  // CEntity
  CEntity* TypesMatch(int typeId) const override;

private:
#ifdef TARGET_PC
  // The head module reads the body's flag byte and calls fn_14_70B4 (its triggers
  // MainBodyDestroyed and IsAntennaTransmitting).
  friend class CDigitalGuardianHead;

  void EnsureBodyActive(CStateManager& mgr);

  // fn_14_70B4 (0x30 bytes): bit 0x08 of 0x128C clear and the beacon's state (0x1268) 1 or 2.
  bool x70B4_() const;
  // The head test four of the body's triggers share (head+0x10F4 bit 0x20).
  bool HeadFlag20_(CStateManager& mgr) const;

  // The guest members the body's state machine reads (docs/module-analysis/DigitalGuardian.md
  // section e). The host class has its own layout, so the guest offsets are in the names and
  // the values are the ones the retail constructor (fn_14_B100) stores.
  u16 xFB0_;        // 0xFB0: a knee id, kInvalidUniqueId (fn_14_705C counts these)
  u16 xFB2_;        // 0xFB2: a knee id, kInvalidUniqueId (fn_14_705C counts these)
  u16 xFB4_;        // 0xFB4: a knee id, kInvalidUniqueId (fn_14_705C counts these)
  u16 xFB6_;        // 0xFB6: a knee id, kInvalidUniqueId (fn_14_705C counts these)
  int x10C4_;       // 0x10C4: how many locator ids follow at 0x10C8 (4 in the ctor)
  CSegId x10C8_[4]; // 0x10C8: the four locator segment ids, all invalid (0xFF) in the ctor
  u16 x1080_;       // 0x1080: the root collider's id (fn_14_61DC sets it, fn_14_3974 reads it)
  TUniqueId x1082_; // 0x1082: the head module's uid (fn_14_9994 looks the head up with it)
  TUniqueId x1084_; // 0x1084: the linked head (fn_14_7168; fn_14_2A28 aims the beacon at it)
  TUniqueId x1086_; // 0x1086: cleared by fn_14_7168 and the detach fn_14_70E4
  uchar x1010_;     // 0x1010: the flag byte the "IM01"/"IM02" messages set (fn_14_A6A8)
  uchar x1011_;     // 0x1011: the flag byte the "IM07"/"IM08" messages set (fn_14_A6A8)
  uchar x1012_;     // 0x1012: the flag byte the "IM03"/"IM04" messages set (fn_14_A6A8)
  uchar x1013_;     // 0x1013: the flag byte the "IM05"/"IM06" messages set (fn_14_A6A8)
  TUniqueId x1168_; // 0x1168: the exit vortex target's waypoint id (fn_14_7600)
  TUniqueId x116A_; // 0x116A: the IS05/Follow object (fn_14_5910)
  CDamageInfo x116C_; // 0x116C: vortexDamage (data +0xE8, LdrToDamageInfo at 0xB5C4; fn_14_4F2C)
  int x11B8_;       // 0x11B8: s32, 0 (fn_14_9638 compares it with 1); the vortex mode: 1 in
                    // VortexAttack, 2 in VortexRetreat, 0 in ExitVortexAttack/SetNormalAttackMode
  float x11BC_;     // 0x11BC: the exit vortex speed, 0.0f (the store at 0xB65C; fn_14_34E8)
  TUniqueId x11C0_; // 0x11C0: the IS00/Attach object (fn_14_5910), the vortex's camera shaker
  int x11C4_;       // 0x11C4: the camera shake id, -1 (the store at 0xB66C; fn_14_4D54)
  float x11C8_;     // 0x11C8: the vortex's health (data +0x100, the store at 0xB674; fn_14_5384
                    // takes damage from it, fn_14_9638 reads it)
  CVector3f x11CC_; // 0x11CC: the vortex's drift velocity, zero (0xB67C; fn_14_3300 sets it)
  int x11D8_;       // 0x11D8: the melee leg, -1 (0xB690; fn_14_7504 sets it, fn_14_73D4 reads it)
  int x11DC_;       // 0x11DC: the melee range, -1 (0xB694; fn_14_73D4, read by fn_14_89D4)
  float x11E0_;     // 0x11E0: f32, 0.0f (fn_14_97F0 compares it with 0.0f)
  float x11E4_;     // 0x11E4: f32, 0.0f (fn_14_9678 compares it with 0.0f)
  int x11E8_;       // 0x11E8: s32, 1 (the store at 0xB6A0; the reaction states clear it,
                    // fn_14_4F2C reads it)
  float x1274_;     // 0x1274: f32, 0 (fn_14_9994 compares it with 0.0f)
  CVector3f x1278_; // 0x1278: the face target's position, zero (fn_14_9A60 reads x, y and z)
  int x1284_;       // 0x1284: s32, -1 (fn_14_9A48 and fn_14_9A0C read it)
  float x1288_;     // 0x1288: the base speed, CPatterned's speed at the ctor's end (0xBA28)
  uchar x128C_;     // 0x128C: the flag byte; bit 7 = "is idle" (fn_14_9A9C reads bit 7)
  uchar x128D_;     // 0x128D: the vortex flags; 0x80 = drift (fn_14_3300 sets it, user event 12
                    // clears it), 0x40 = pull the player (user event 17 sets it)

  // The five models the module's AddToRenderer (fn_14_A004) draws, and the state they read.
  // The guest offsets are the REL's (docs/module-analysis/DigitalGuardian.md section e); the
  // host class has its own layout. Each one is an optional: the guest's CModelData is 0x4C
  // bytes and the byte after it is the optional's valid flag, which the draws test.
  CCollisionActorManager* xE60_; // 0xE60: the first collision actor manager (fn_14_61DC builds it)
  CCollisionActorManager* xE64_; // 0xE64: the second collision actor manager (fn_14_5C30 builds it)
  // 0xE68: the crippled 'SCAN' token (data +0, the ctor at 0xBF4C..0xC00C); fn_14_9DBC reads it.
  rstl::single_ptr< TCachedToken< CScannableObjectInfo > > xE68_;
  // 0xE6C (0x4c, valid flag 0xEB8): the leg model, built by fn_14_6864 and drawn by
  // fn_14_1994 at the four knee locators.
  rstl::optional_object< CModelData > xE6C_;
  // 0xEBC (0x4c, valid flag 0xF08): the shin model, built by fn_14_6864 and drawn by
  // fn_14_18B4 at the four knee locators.
  rstl::optional_object< CModelData > xEBC_;
  // 0xF0C (0x4c, valid flag 0xF58): the knee model, built by fn_14_6864.
  rstl::optional_object< CModelData > xF0C_;
  // 0xF5C (0x4c, valid flag 0xFA8): the toe target's model, built by fn_14_6864 and drawn by
  // fn_14_1700 at the four ankle locators.
  rstl::optional_object< CModelData > xF5C_;
  // 0x11EC (0x4c, valid flag 0x1238): the transmission beacon's model, built by fn_14_6864 and
  // drawn by fn_14_17DC at the neck locator.
  rstl::optional_object< CModelData > x11EC_;
  uchar x10B8_;      // 0x10B8: the "Head_1" locator segment id (fn_14_9E88)
  uchar x10B9_;      // 0x10B9: the neck locator segment id (fn_14_2578)
  uchar x10C0_;      // 0x10C0: the first leg locator segment id (fn_14_1994, fn_14_18B4)
  uchar x10C1_;      // 0x10C1: the second leg locator segment id
  uchar x10C2_;      // 0x10C2: the third leg locator segment id
  uchar x10C3_;      // 0x10C3: the fourth leg locator segment id
  uchar x10D4_;      // 0x10D4: the first toe locator segment id (fn_14_1700)
  uchar x10D5_;      // 0x10D5: the second toe locator segment id
  uchar x10D6_;      // 0x10D6: the third toe locator segment id
  uchar x10D7_;      // 0x10D7: the fourth toe locator segment id
  int x10DC_[4];     // 0x10DC: the four per-leg words (0x10DC, 0x10E0, 0x10E4, 0x10E8); the
                     // value 3 skips the leg (fn_14_1700)
  CParticleGen* x123C_;  // 0x123C: the particle generator the echo emitter's render draws (a
                         // CElementGen in retail); fn_14_D664 creates it
  TUniqueId x10CC_;      // 0x10CC: the guard's collision actor (fn_14_28D0 wakes it)
  CSfxHandle x1240_;   // 0x1240: the beacon's sound emitter handle (fn_14_28D0 sets it)
  // 0x1244 (present byte 0x1250): transmissionBeaconExplosion (data +0x1F4), fn_14_266C's.
  rstl::optional_object< TLockedToken< CGenDescription > > x1244_;
  // 0x1254 (present byte 0x1260): transmissionBeaconEchoHit (data +0x1F8), fn_14_2E60's.
  rstl::optional_object< TLockedToken< CGenDescription > > x1254_;
  float x126C_;        // 0x126C: the echo flash time (0.0f, the store at 0xB9C4; fn_14_5274)
  float x1264_;        // 0x1264: the beacon's height (fn_14_17DC, fn_14_2578, fn_14_2CC4)
  int x1268_;          // 0x1268: the beacon's state word (fn_14_2CC4)
  uint x1270_;       // 0x1270: the packed colour the echo emitter's model is drawn with
  u16 xFBC_[4];      // 0xFBC: the four knee joint colliders ("*_knee_2" spheres; fn_14_44F8)
  u16 x1078_[4];     // 0x1078: the four toe target colliders ("*_col_LCTR" spheres; fn_14_4464)
  // 0x1014: the knees' open vulnerability (kneeVulnerability, the ctor's 0xB1B0..0xB248);
  // fn_14_3A44 gives it to a knee collider whose flag byte (0x1010 + i) is set.
  CDamageVulnerability x1014_;
  // 0x1044: immune-ricochet with the AI weapon type passed through (the ctor's 0xB258/0xBEF4).
  CDamageVulnerability x1044_;
  CDamageVulnerability x1110_; // 0x1110: the toe targets' (toeTargetVulnerability, 0xB534)
  CDamageVulnerability x1188_; // 0x1188: the root collider's (vortexVulnerability, 0xB618)
  // 0xE00 (0x60): the actor parameters, a copy of the constructor's (fn_14_C57C at 0xB188); the
  // knee armour is built with them (fn_14_4080).
  CActorParameters xE00_;
  // 0x1100 (present flag 0x110C): the toe target's explosion ('PART' toeTargetExplosion, locked by
  // the constructor at 0xB3D0..0xB4A8).
  rstl::optional_object< TLockedToken< CGenDescription > > x1100_;
  CSfxHandle x10F0_[4]; // 0x10F0: the toe targets' sound emitters (fn_14_1A68)
  float x1144_[4];      // 0x1144: the toe targets' health (toeTargetHP; fn_14_24E4, fn_14_5644)
  float x1158_[4];      // 0x1158: the toe targets' hit timers (0; fn_14_24E4, fn_14_5644)

  // 0x10CE: the IS04/Activate object (fn_14_5910, the "XALC" case); fn_14_5470 takes one from
  // its health per destroyed knee.
  TUniqueId x10CE_;

  // fn_14_3A44 (0x108 bytes): each knee's vulnerability - open (0x1014) when its flag byte
  // (0x1010 + i) is set and bit 5 of 0x128C is clear, else the ricochet one (0x1044) - and its
  // armour's armoured flag (fn_14_CE94).
  void x3A44_(CStateManager& mgr);
  // fn_14_3E2C (0x5C bytes): which knee collider (0xFBC + 2i) an id is, or -1.
  int x3E2C_(TUniqueId id) const;
  // fn_14_143C (0xBC bytes): moves the IS06/Follow waypoint to a position, then sends a state.
  void x143C_(CStateManager& mgr, const CVector3f& pos, EScriptObjectState state);
  // fn_14_5838 (0xD8 bytes): the "XDMG" case - which collider was hit.
  void x5838_(CStateManager& mgr, TUniqueId sender);
  // fn_14_5470 (0x1D4 bytes): a knee collider hit: the armour flashes, or goes when the knee's
  // health is gone (KneeDestroyed: 0x1284 = the knee).
  void x5470_(CStateManager& mgr, TUniqueId sender, int knee);
  // fn_14_4080 (0x274 bytes): the four knee armours, one at each knee locator.
  void x4080_(CStateManager& mgr);
  // fn_14_24E4 (0x94 bytes): the toe targets' reset (health, state, timer), then state 1 each.
  void x24E4_(CStateManager& mgr);
  // fn_14_1A68 (0x580 bytes): a toe target's state (0 attack, 1 target, 2 flinch, 3 destroyed):
  // its effects, its sound and, for 3, the explosion.
  void x1A68_(CStateManager& mgr, int leg, int state);
  // fn_14_3D2C (0x7C bytes): bit 5 of 0x128C, and IS10 when it is set, else IS11.
  void x3D2C_(CStateManager& mgr, bool set);
  // fn_14_3F14 (0x7C bytes): the knee armours are deleted (the XDEL and IM00 messages).
  void x3F14_(CStateManager& mgr);
  // fn_14_5910 (0xAC bytes): the "XALC" case - the connected objects at 0x11C0, 0x10CE, 0x116A.
  void x5910_(CStateManager& mgr);
  // fn_14_14F8 (0x74 bytes): the speed for the number of knee armours left.
  void x14F8_();

  // fn_14_44F8 / fn_14_4464: record id in xFBC_ resp. x1078_ when name is one of the four knee
  // resp. toe target locators. fn_14_4408 / fn_14_43AC: whether id is one of them.
  bool x44F8_(const rstl::string& name, TUniqueId id);
  bool x4464_(const rstl::string& name, TUniqueId id);
  bool x4408_(TUniqueId id) const;
  bool x43AC_(TUniqueId id) const;
  // fn_14_4350 (0x5C bytes): which toe target (0x1078 + 2i) an id is, or -1.
  int x4350_(TUniqueId id) const;
  // fn_14_5140 (0x134 bytes): a toe target hit: its health (0x1144) drops; at zero it is
  // destroyed (IS08 at the IS06/Follow waypoint), else it flinches.
  void x5140_(CStateManager& mgr, TUniqueId sender, float damage);
  // fn_14_5384 (0xEC bytes): a toe target or the root collider hit (from fn_14_5838).
  void x5384_(CStateManager& mgr, TUniqueId sender);
  // fn_14_5274 (0x100 bytes): the head collider (the beacon) hit (from fn_14_5838).
  void x5274_(CStateManager& mgr, TUniqueId sender);
  // fn_14_4F2C (0x214 bytes): the "XHIT" case - a collider the player touched damages the player.
  void x4F2C_(CStateManager& mgr, TUniqueId sender);
  // fn_14_266C (0x264 bytes): the beacon is destroyed.
  void x266C_(CStateManager& mgr);
  // fn_14_2E60 (0x234 bytes): the beacon is hit and survives.
  void x2E60_(CStateManager& mgr);

  // The four model renders fn_14_A004 calls (the calls at 0xA0A0, 0xA0B4, 0xA0E8 and 0xA0FC),
  // named by their retail offset.
  void x1994_(const CStateManager& mgr, const CTransform4f& xf, const CModelFlags& flags) const;
  void x18B4_(const CStateManager& mgr, const CTransform4f& xf, const CModelFlags& flags) const;
  void x17DC_(const CStateManager& mgr, const CTransform4f& xf) const;
  void x1700_(const CStateManager& mgr, const CTransform4f& xf, const CModelFlags& flags) const;

  // fn_14_1570 (0x190 bytes): the model's vertex bounds, rotated and scaled about the origin.
  CAABox x1570_() const;

  // fn_14_2578 (0xF4 bytes): the echo emitter's transform.
  void x2578_(CTransform4f& out, const CTransform4f& xf) const;

  // fn_14_D664 (0x178 bytes): the beacon's particle generator.
  CElementGen* xD664_(CAssetId id);

  // fn_14_2CC4 (0x19C bytes): the beacon's update.
  void x2CC4_(CStateManager& mgr, float dt);

  // fn_14_3E88 (0x88 bytes): the knee colliders' active flag.
  void x3E88_(CStateManager& mgr, bool active);

  // fn_14_3974 (0x58 bytes): the object with the id at guest 0x1080 gets its active flag set.
  void x3974_(CStateManager& mgr, bool active);
  // fn_14_39CC (0x68 bytes): the toe target colliders (0x1078, count 0x1074) get a vulnerability.
  void x39CC_(CStateManager& mgr, const CDamageVulnerability& vuln);
  // fn_14_3DA8 (0x84 bytes): the four knee armours (0xFB0) orbitable or not (fn_14_CE1C).
  void x3DA8_(CStateManager& mgr, bool orbitable);
  // fn_14_34E8 (0x18C bytes): a step towards the exit vortex waypoint (0x1168); true while the
  // step falls short of it.
  bool x34E8_(CStateManager& mgr, float dt, float minDist);
  // fn_14_3674 (0x164 bytes): the vortex pulls the player.
  void x3674_(CStateManager& mgr, float dt);
  // fn_14_4D54 / fn_14_4878 / fn_14_4838: the vortex camera shake (0x11C0's shaker, id 0x11C4)
  // is added, moved to the body, removed.
  void x4D54_(CStateManager& mgr);
  void x4878_(CStateManager& mgr);
  void x4838_(CStateManager& mgr);
  // fn_14_3300 (0x1E8 bytes): the vortex drift towards the IS05/Follow object (0x11CC).
  void x3300_(CStateManager& mgr);
  // fn_14_4E00 (0x12C bytes): a camera shake at a locator (the user event ScreenShake).
  void x4E00_(CStateManager& mgr, const rstl::string& locator);
  // fn_14_20E4 (0xBC bytes): the leg of an ankle or toe collider locator, or -1.
  int x20E4_(const rstl::string& name) const;
  // fn_14_458C (0x1E0 bytes): a toe's shock wave (the user event EventStart).
  void x458C_(CStateManager& mgr, const rstl::string& locator);

  // fn_14_676C (0xF8 bytes): the collision actor managers' builder, and the three builders it
  // calls: fn_14_61DC (0x590 bytes, the manager at 0xE60), fn_14_5C30 (0x2F4 bytes, the manager
  // at 0xE64) and fn_14_59BC (0x274 bytes, the colliders' health).
  void x676C_(CStateManager& mgr);
  void x61DC_(CStateManager& mgr);
  void x5C30_(CStateManager& mgr);
  void x59BC_(CStateManager& mgr);

  // fn_14_28D0 (0x158 bytes) and fn_14_2A28 (0x29C bytes): the beacon's arrival and its
  // effect update. Both are over this slice's callee limit, so they are stand-ins.
  void x28D0_(CStateManager& mgr);
  void x2A28_(CStateManager& mgr, float dt);

  // fn_14_6864 (0x1BC bytes): the constructor's tail builds the module's five models.
  void x6864_();

  // The retail's per-segment rotation: the character's rest pose rotation for that segment.
  CTransform4f SegmentRotation_(const CSegId& seg) const;

  // fn_14_308C: the id of the connected waypoint that is active and closest to the given
  // position (the helper fn_14_9678 measures against).
  TUniqueId x308C_(CStateManager& mgr, const CVector3f& position) const;

  // fn_14_705C: how many of the four knee ids (guest this+0xFB0, 0xFB2, 0xFB4 and 0xFB6) are
  // not kInvalidUniqueId.
  int x705C_() const;

  // Host layout (retail's object is 0x1290, CPatterned is 0x7c0). The guest offsets are the
  // REL's and are in docs/module-analysis/DigitalGuardian.md section e; the host class has its
  // own layout and only keeps what this milestone needs.
  CDigitalGuardianData mData; // guest 0x7C0 (0x640), the block fn_14_C68C copies in

  // -------------------------------------------------------------------------------------------
  // The helpers this lane ports: the module's stateless functions that are not FSM handlers
  // (the bodies are in src/MetroidPrime/Enemies/PortCDigitalGuardianHelpers.cpp; the function
  // map, the caller map and the arm tables are in
  // docs/module-analysis/DigitalGuardian_helpers.md). Append-only, so the FSM lane's edits in
  // the same header merge cleanly.
  float x37FC_(float v) const;   // fn_14_37FC: the nearest of the five 45-degree values
  float x6C74_() const;          // fn_14_6C74: the attack angle (4*pi, pi/3 or 2*pi/3)
  void x3F90_(CStateManager& mgr);            // fn_14_3F90: the knee armours follow the knees
  void x38BC_(CStateManager& mgr, float dt);  // fn_14_38BC: the leg-stab, jump and protect timers
  void x2358_(CStateManager& mgr, float dt);  // fn_14_2358: the toe targets' sound emitters
  void x5644_(CStateManager& mgr, TUniqueId sender); // fn_14_5644: the "XRDG" case
  bool x6C24_(CDigitalGuardianHead& head); // fn_14_6C24: the head's turn direction
  void x6CB0_(CDigitalGuardianHead& head, const CTransform4f& xf, CVector3f& outForward,
              CVector3f& outUp); // fn_14_6CB0: the aim (the forward and the up vector)
  void x6E04_(CStateManager& mgr, float dt);   // fn_14_6E04: the body turns to the head
  // fn_14_83C0 (0x184 bytes): the knock-back animation parm data (the state id and the
  // eight parameters).
  CPASAnimParmData x83C0_(pas::EAnimationState state, const CPASAnimParm& p1,
                          const CPASAnimParm& p2, const CPASAnimParm& p3,
                          const CPASAnimParm& p4, const CPASAnimParm& p5,
                          const CPASAnimParm& p6, const CPASAnimParm& p7,
                          const CPASAnimParm& p8) const;
  CTransform4f x1088_ = CTransform4f::Identity(); // 0x1088: the aim transform (fn_14_6CB0)
#else
  uchar x7c0_[0x1290 - 0x7c0]; // CDigitalGuardianData and everything above it in retail
#endif
};

#endif
