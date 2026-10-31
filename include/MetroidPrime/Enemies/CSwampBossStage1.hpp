#ifndef _CSWAMPBOSSSTAGE1
#define _CSWAMPBOSSSTAGE1

#include "types.h"

#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "Kyoto/Math/CVector3f.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/ScriptLoader/SLdrSwampBossStage1.hpp"
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"

#include "rstl/optional_object.hpp"
#include "rstl/reserved_vector.hpp"
#include "rstl/single_ptr.hpp"

class CCollisionActor;
class CCollisionActorManager;
class CElementGen;
class CGenDescription;

// Chykka Larva, the first Chykka stage in Torvus Bog (REL module 78, script type SBS1). Port-only
// body written from the retail REL for behaviour; docs/module-analysis/SwampBoss.md maps the
// functions and members. Members keep their retail offset (xNNN_).
class CSwampBossStage1 : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CSwampBossStage1(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                   const CTransform4f& xf, const CModelData& modelData,
                   const CActorParameters& actParms, const CPatternedInfo& pInfo,
                   const SLdrSwampBossStage1Data& data);
  ~CSwampBossStage1() override;

  // CEntity
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

  // CActor
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  void Render(const CStateManager& mgr) const override;
  void PreRenderAllViewports(CStateManager& mgr) override;
  rstl::optional_object< CAABox > GetTouchBounds() const override;
  CVector3f GetOrbitPosition(const CStateManager& mgr) const override;
  void OnScanStateChange(EScanState state, CStateManager& mgr) override;
  CAABox GetSortingBounds(const CStateManager& mgr) const override;
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;
  CScannableObjectInfo* GetScannableObjectInfo() const override;

  // CPhysicsActor / CAi / CPatterned
  void CollidedWith(const TUniqueId& id, const CCollisionInfoList& list,
                    CStateManager& mgr) override;
  void TakeDamage(const CVector3f& dir, float amount) override;
  // @ fn_78_8004
  CProjectileInfo* ProjectileInfo() override { return &xd7c_projectileInfo; }
  void SetupStateMachine(CStateManager& mgr) override;

  // FSM2 states.
  void Beach(CStateManager& mgr, EStateMsg msg, float dt);
  void BeachBubbles(CStateManager& mgr, EStateMsg msg, float dt);
  void BeachRise(CStateManager& mgr, EStateMsg msg, float dt);
  void BeachWait(CStateManager& mgr, EStateMsg msg, float dt);
  void Bob(CStateManager& mgr, EStateMsg msg, float dt);
  void Dead(CStateManager& mgr, EStateMsg msg, float dt);
  void Dive(CStateManager& mgr, EStateMsg msg, float dt);
  void ExposeBelly(CStateManager& mgr, EStateMsg msg, float dt);
  void HeavyHitReact(CStateManager& mgr, EStateMsg msg, float dt);
  void LightHitReact(CStateManager& mgr, EStateMsg msg, float dt);
  void Null(CStateManager& mgr, EStateMsg msg, float dt);
  void PlatformReady(CStateManager& mgr, EStateMsg msg, float dt);
  void ResetPlatform(CStateManager& mgr, EStateMsg msg, float dt);
  void SetSplashOver(CStateManager& mgr, EStateMsg msg, float dt);
  void SlideIntoWater(CStateManager& mgr, EStateMsg msg, float dt);
  void SplashDescend(CStateManager& mgr, EStateMsg msg, float dt);
  void SplashJump(CStateManager& mgr, EStateMsg msg, float dt);
  void SplashRise(CStateManager& mgr, EStateMsg msg, float dt);
  void SplashTelegraph(CStateManager& mgr, EStateMsg msg, float dt);
  void SplashWait(CStateManager& mgr, EStateMsg msg, float dt);
  void StopBeaching(CStateManager& mgr, EStateMsg msg, float dt);
  void Swim(CStateManager& mgr, EStateMsg msg, float dt);
  void ResetTongueState();
  void TongueLoop(CStateManager& mgr, EStateMsg msg, float dt);
  void TonguePull(CStateManager& mgr, EStateMsg msg, float dt);
  void Vomit(CStateManager& mgr, EStateMsg msg, float dt);

  // FSM2 triggers.
  bool BeachAttackOver(CStateManager& mgr, const CTriggerData& data) const;
  bool EnoughSplashing(CStateManager& mgr, const CTriggerData& data) const;
  bool GivingUp(CStateManager& mgr, const CTriggerData& data) const;
  bool HeavyHit(CStateManager& mgr, const CTriggerData& data) const;
  bool InVomitRange(CStateManager& mgr, const CTriggerData& data) const;
  bool LightHit(CStateManager& mgr, const CTriggerData& data) const;
  bool MinHealth(CStateManager& mgr, const CTriggerData& data) const;
  bool ReadyToTongue(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldBeach(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldBob(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldExposeBelly(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldSplash(CStateManager& mgr, const CTriggerData& data) const;
  bool SplashAttackOver(CStateManager& mgr, const CTriggerData& data) const;
  bool TongueHit(CStateManager& mgr, const CTriggerData& data) const;
  bool TongueMissed(CStateManager& mgr, const CTriggerData& data) const;
  bool TooMuchPulling(CStateManager& mgr, const CTriggerData& data) const;
  bool Wait(CStateManager& mgr, const CTriggerData& data) const;

  // FSM2 code functions.
  void IncrementMisses(CStateManager& mgr, float dt);
  void SetSplashRotation(CStateManager& mgr, float dt);
  void ShreddersOn(CStateManager& mgr, float dt);
  void ShreddersOff(CStateManager& mgr, float dt);

  // Phase struct (retail 0xa90/0xab4/0xad8), picked by xb84_phase.
  const SLdrSwampBossStage1Struct& CurrentPhase() const;

private:
  enum ETongueState { kTS_Idle, kTS_Out, kTS_Hit };

  void RecordState(int state, EStateMsg msg);
  void BodyMelee(CStateManager& mgr, EStateMsg msg, int severity);
  void BodyTaunt(CStateManager& mgr, EStateMsg msg, int taunt);
  void HitReactBody(CStateManager& mgr, EStateMsg msg, float dt);
  void SetLocomotion(int type);
  CCollisionActor* JointActor(CStateManager& mgr, int idx) const;
  void CreateCollision(CStateManager& mgr);
  void SetJointsActive(CStateManager& mgr, int type, int flags);
  void SetBeachMaterials(CStateManager& mgr, bool beached);
  bool AnyJointOnRadar(CStateManager& mgr) const;
  bool IsJointOfType(CStateManager& mgr, const CCollisionActor* act, int type) const;
  void ScheduleNextAttack(CStateManager& mgr);
  int CurrentAttack() const;
  bool PlayerInsideHint(CStateManager& mgr) const;
  float TongueAngleToPlayer(const CStateManager& mgr) const;
  CVector3f TongueTargetPos(const CStateManager& mgr) const;
  float WaterHeight(const CStateManager& mgr) const;
  bool CanTakeHits() const;
  bool IsMinHealth() const;

  void ExtendTongue(CStateManager& mgr, float dt);
  void RetractTongue(CStateManager& mgr, float dt);
  void UpdateTongueTip(CStateManager& mgr, float amp, bool ray);
  void TongueRayCheck(CStateManager& mgr, const CVector3f& tip);
  void LaunchTongue(CStateManager& mgr);
  void StopTongueSounds();
  void StopTongue();
  void HeadWeightUp(float dt);
  void HeadWeightDown(float dt);
  void UpdateHeadTurn(CStateManager& mgr, float dt);
  CVector3f UpdateHeadLook(CStateManager& mgr, float dt);
  void ApplyHeadAdditive(CStateManager& mgr, float weight, int side);

  void PullMorphedPlayer(CStateManager& mgr, const CVector3f& pos);
  void PullPlayer(CStateManager& mgr, float dt, const CVector3f& pos);
  void KnockPlayer(CStateManager& mgr, const CVector3f& velocity);
  void Rumble(CStateManager& mgr);
  void ResetPlatformConnection(CStateManager& mgr);
  void SetPlatformTarget(CStateManager& mgr);
  void FacePlatform();
  void PickPlatform(CStateManager& mgr);
  void DeliverToConnected(CStateManager& mgr, EScriptObjectMessage msg);
  void IncrementAttached(CStateManager& mgr);
  void DecrementAttached(CStateManager& mgr);
  void ReleaseOrbitMessage(CStateManager& mgr);
  void SendOrbitMessage(CStateManager& mgr);
  void ReleaseOrbitLock(CStateManager& mgr);
  void LockOrbitOnTongue(CStateManager& mgr);

  void VomitHit(CStateManager& mgr);
  void Spit(CStateManager& mgr);
  void LaunchShockWave(CStateManager& mgr);
  void StartTelegraph(CStateManager& mgr, const CVector3f& pos);
  void UpdateRing(CStateManager& mgr, float dt);
  void UpdateShockWaveSound(CStateManager& mgr);
  void AlertShredders(CStateManager& mgr);
  void PickSplashLocomotion(CStateManager& mgr);
  void SplashWaitBody(CStateManager& mgr, EStateMsg msg);
  bool ApplyDamageMsg(CStateManager& mgr, const CScriptMsg& msg);
  // fn_78_A5B4 (retail): the splash locomotion is 11 when the alt-animation flag is set, 10 otherwise.
  // `lwz r0, 0xb6c(r3)` at 0xA5B4 loads xb6c.
  // `cmpwi r0, 0x0` at 0xA5BC tests it.
  int SplashLocomotion() const;
  // fn_78_A5CC (retail): the rise locomotion is 14 when the alt-animation flag is set, 1 otherwise.
  // `lwz r0, 0xb6c(r3)` at 0xA5CC loads xb6c.
  // `cmpwi r0, 0x0` at 0xA5D4 tests it.
  int RiseLocomotion() const;
  void RenderTongue(const CStateManager& mgr) const;

  SLdrSwampBossStage1Data x7c0_data;
  rstl::reserved_vector< int, 3 > xafc_stateHistory;
  int xb10_;
  int xb14_state;
  float xb18_time;
  float xb1c_clock;
  CVector3f xb20_rootPos;
  CVector3f xb2c_headPos;
  CSfxHandle xb38_telegraphSfx;
  float xb3c_painAccum;
  CAABox xb40_sortBounds;
  bool xb58_scanned : 1;
  bool xb58_radarPresent : 1;
  TUniqueId xb5a_lastHitter;
  int xb5c_shredders;
  CVector3f xb60_shredderOrigin;
  int xb6c_altAnims;
  float xb7c_nextBobTime;
  rstl::single_ptr< CCollisionActorManager > xb80_collision;
  int xb84_phase;
  int xb88_attackIdx;
  float xb8c_nextAttackTime;
  SLdrShockWaveInfo xb90_shockWave;
  rstl::single_ptr< CElementGen > xbc4_telegraphGen;
  int xbc8_jumps;
  int xbcc_jumpTarget;
  float xbd0_splashRotDeg;
  CAssetId xbd4_telegraphPart;
  bool xbd8_splashOver;
  TUniqueId xbda_shockWave;
  CSfxHandle xbdc_shockWaveSfx;
  CVector3f xbe0_shockWaveSfxPos;
  int xbec_splashPhase;
  int xbf0_platformIdx;
  int xbf4_platformPick;
  float xbf8_platformTime;
  int xbfc_misses;
  int xc00_tongues;
  bool xc04_beachOver;
  int xc08_beachPhase;
  CVector3f xc0c_tonguePos;
  CVector3f xc18_tongueTarget;
  CVector3f xc24_tongueTip;
  float xc30_extend;
  int xc34_tongueState;
  float xc38_pullTime;
  float xc3c_pullSpeed;
  float xc40_tongueAmp;
  rstl::optional_object< CModelData > xc44_tongueModel;
  rstl::optional_object< CModelData > xc94_tipModel;
  bool xce4_orbitMsg : 1; // 0xce4 0x80
  bool xce4_playerTouched : 1; // 0xce4
  float xce8_vomitTimer;
  CSfxHandle xcec_retractSfx;
  CSfxHandle xcf0_pullSfx;
  float xcf4_headWeight;
  CVector3f xcf8_headDir;
  bool xd04_headLatch;
  float xd08_headBlend;
  float xd0c_hpSnapshot;
  float xd10_bellyAccum;
  bool xd14_heavyHit;
  CDamageVulnerability xd18_weakVuln;
  rstl::optional_object< TToken< CGenDescription > > xd48_goo;
  int xd54_vomitHits;
  float xd58_savedStepUp;
  rstl::single_ptr< CElementGen > xd60_ringGen;
  TUniqueId xd64_water;
  bool xd66_ringOn;
  CVector3f xd68_headOffset;
  float xd74_ringRate;
  float xd78_waitStart;
  CProjectileInfo xd7c_projectileInfo;
  rstl::optional_object< TLockedToken< CGenDescription > > xda4_spitVisor;
};

#endif // _CSWAMPBOSSSTAGE1
