#ifndef _CEMPERORINGSTAGE3
#define _CEMPERORINGSTAGE3

#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "REL/REL_Setup.h"

#ifdef TARGET_PC
#include "MetroidPrime/CBasicSwarmData.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/ScriptLoader/SLdrEmperorIngStage3.hpp"
#include "MetroidPrime/Weapons/CShockWave.hpp"
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"
#include "rstl/optional_object.hpp"
#include "Kyoto/Audio/CSfxHandle.hpp"

#include "Kyoto/Math/CTransform4f.hpp"
#include "rstl/reserved_vector.hpp"
#include "rstl/vector.hpp"

class CCollisionActor;
class CCollisionActorManager;
class CJointCollisionDescription;

// The module's three lists (guests 0x830, 0x840 and 0x850): rstl vectors (the count at +0x4, the
// capacity at +0x8, the entries at +0xC) the constructor reserves sixteen entries in (fn_18_ADF8
// with 0x10, 0x9C84..0x9C9C); each entry is 0xC bytes - a weight at +0x0, a threshold at +0x4
// and a timer at +0x8.
struct SEmperorIngListEntry {
  float mWeight;    // guest +0x0
  float mThreshold; // guest +0x4
  float mTimer;     // guest +0x8
};
typedef rstl::vector< SEmperorIngListEntry > SEmperorIngList;

// The module's portal list at guest 0x8CC: 0x34-byte records, the id of an actor connected on
// INTERNAL_STATE_02/ACTIVATE and that actor's transform relative to this actor's area.
struct SEmperorIngPortal {
  TUniqueId mId;     // guest +0x0
  CTransform4f mXf;  // guest +0x4
  SEmperorIngPortal(TUniqueId id, const CTransform4f& xf) : mId(id), mXf(xf) {}
};
#endif

// The REL's CEmperorIngStage3 (module 18): the mutated Emperor Ing, the boss the player fights
// in the Great Temple's moth lord sanctuary (dark), layer 4. 0xED0 bytes on the GameCube (the
// loader's rs_new at 0x0000C598), built by fn_18_9A8C, its vtable lbl_18_data_4F0 (80 entries, 21
// of them local) and the analysis is docs/module-analysis/EmperorIngStage3.md.
//
// Milestone 1 (this file's first slice) is load-and-stand: the loader, the constructor and the
// accessors the vtable gives one-line bodies to. The state machine's tables (fn_18_943C installs
// the 21 triggers, 21 states and 2 code functions), the four attacks (melee, stampede, jump slide,
// ground pound), the two beams, the portal attack and the light swarm are milestone 2.
//
// The host layout is its own: the retail object is 0xED0 bytes, CPatterned is 0x7C0, and the guest
// offset of every member the constructor touches is in the comment above it. Host objects
// outgrow retail, so the class is allocated with sizeof, not with 0xED0.
#ifdef TARGET_PC
class REL_EXPORT CEmperorIngStage3 : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  // Argument order is the REL loader's call at 0x0000C620 (fn_18_C380 -> fn_18_9A8C): uid,
  // editor-property name, entity info, transform, model, actor parameters, patterned info, then
  // the script's data block (the REL passes it on the stack, the slot 0x308 at 0x0000C5B8).
  CEmperorIngStage3(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                     const CTransform4f& xf, const CModelData& modelData,
                     const CActorParameters& actParms, const CPatternedInfo& pInfo,
                     const SLdrEmperorIngStage3Data& data);
  ~CEmperorIngStage3() override;

  // CEntity
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

  // CActor
  void PreRender(CStateManager& mgr) override;
  void Render(const CStateManager& mgr) const override;
  const CHealthInfo* GetHealthInfo() const override;
  rstl::optional_object< CAABox > GetTouchBounds() const override;
  CVector3f GetAimPosition(const CStateManager& mgr, float param) const override;
  CAABox GetSortingBounds(const CStateManager& mgr) const override;
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;

  // CAi
  void TakeDamage(const CVector3f& direction, float magnitude) override;
  bool CanBeShot(const CStateManager&, int) override;
  bool IsListening() const override;
  bool Listen(CStateManager& mgr, const CVector3f& position, EListenNoiseType type) override;
  CVector3f GetOrigin(const CStateManager& mgr, const CTeamAiRole& role,
                      const CVector3f& aimPos) const override;

  // CPatterned
  uchar GetModelAlphau8(const CStateManager& mgr) const override;
  CProjectileInfo* ProjectileInfo() override;
  CPathFindSearch* GetSearchPath() override;
  CPathFindPointSearch* GetPointSearchPath() override;
  CRagDoll* GetRagDoll() const override;
  TUniqueId GetAttackTarget() const override;
  bool IsOnGround() const override;
  float GetGravityConstant() const override;
  const rstl::optional_object< TCachedToken< CGenDescription > >&
  GetDeathExplosionParticle() const override; // fn_18_88
  void SetupStateMachine(CStateManager& mgr) override;

  // The body's state machine. Retail fn_18_943C (vtable slot 60) calls the base's setup and
  // then registers the three tables fn_18_E180 builds: 21 triggers (lbl_18_data_34C), 21 states
  // (lbl_18_data_100) and 2 codes (lbl_18_data_4B4). The names are retail's - the state machine
  // asset's nodes are matched by name - and each comment is the retail function the entry points
  // at. The triggers whose retail body is small enough are ported; the rest are stand-ins that log
  // the retail function they stand in for.
  //
  // Triggers (lbl_18_data_34C, 21 entries).
  bool TooClose(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8DA4
  bool ShouldAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8D88
  bool ShouldMeleeAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8C4C
  bool ShouldTaunt(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8A4C
  bool ShouldStampede(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_897C
  bool ShouldDodge(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8A34
  bool ShouldChangeVulnerabilityToVulnerable(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8A20
  bool ShouldChangeVulnerabilityToInvulnerable(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_898C
  bool ShouldDarkBeamAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8968
  bool ShouldLightBeamAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8954
  bool ShouldLightSwarmAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_87F0
  bool ShouldGroundPound(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8ACC
  bool ShouldGenerateDarkFliers(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_868C
  bool ShouldPortalAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8678
  bool ShouldJumpSlideAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8664
  bool ShouldGrowHeart(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8648
  bool JumpSlideAttackOver(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8600
  bool ShouldEyeShut(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_85A4
  bool AbortedJumpSlide(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_858C
  bool ShouldJumpAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8518
  bool ShouldStampedeReact(CStateManager& mgr, const CTriggerData& data) const;  // fn_18_8500

  // States (lbl_18_data_100, 21 entries).
  void Start(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_7ADC
  void Idle(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_788C
  void MeleeAttack(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_76D0
  void GainDistance(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_7504
  void Stampede(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_71D8
  void Dodge(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_6E80
  void Taunt(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_6D84
  void ChangeVulnerability(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_6A40
  void EyeShutResetVulnerability(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_6B80
  void DarkBeamAttack(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_6904
  void LightBeamAttack(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_67C0
  void LightSwarmAttack(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_6698
  void GroundPound(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_6390
  void GenerateDarkFliers(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_6258
  void PortalAttack(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_60AC
  void JumpSlideAttack(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_5844
  void GrowHeart(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_5614
  void EyeShut(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_5264
  void JumpAttack(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_4EB4
  void StampedeReact(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_4CC8
  void Dead(CStateManager& mgr, EStateMsg msg, float dt);  // fn_18_4C3C

  // Codes (lbl_18_data_4B4, 2 entries).
  void ResetAttackTimer(CStateManager& mgr, float dt);  // fn_18_84A8
  void ChoseAction(CStateManager& mgr, float dt);  // fn_18_8200

private:
  void EnsureBodyActive(CStateManager& mgr);
  // fn_18_F8: the module's look check. The parameter is the angle in degrees the player may
  // be off the actor's facing by; the triggers that need it pass retail's constant.
  bool RandomCheck(CStateManager& mgr, float maxAngleDeg) const;
  // fn_18_DBC: the actor's "DarkBeam_LCTR_SDK" locator in world space.
  CVector3f DarkBeamLocatorPosition() const;
  // fn_18_2298: the taunt average over the module's bone list. Its own callee fn_18_AB9C
  // needs the list at guest 0x840, which the port does not model, so this stays a stand-in.
  float TauntAverage() const;
  // fn_18_1A54: the look along the actor's facing for something to hit. The answer is "the way
  // is clear": true when the ray hit nothing.
  bool LineOfSight(CStateManager& mgr, const CVector3f& facing, float length) const;
  // fn_18_3804: the turn towards a position, which writes the facing at guest 0x818.
  void FaceTarget(const CVector3f& target);
  // fn_18_3744: the angle snapped to the nearest 45 degrees.
  float SnapAngleTo45(float angle) const;
  // fn_18_22BC: the module's average over the list at guest 0x840.
  float BoneAverage() const;
  // fn_18_AC44: the list walk - every entry's timer grows and an entry past its threshold
  // is erased.
  static void UpdateList(SEmperorIngList& list, float dt);
  // fn_18_AC00: an entry is added when the list has room.
  static void PushListEntry(SEmperorIngList& list, float weight, float threshold);
  // fn_18_2BF0: the module's entry kinds for the list at guest 0x830 (3 ages it by 5.0).
  void AddListEntry(int kind);
  // fn_18_3BB0: the leg boxes and the three spheres (two of them the hearts) in a collision
  // actor manager, kept at guest 0x7C0.
  void SetupCollisionActors(CStateManager& mgr);
  // fn_18_39E0: the actor is a target while either heart is active.
  void UpdateTargetMaterials(CStateManager& mgr);
  // fn_18_214: a heart's collision actor is switched on or off.
  void SetHeartActive(CStateManager& mgr, int heart, bool active);
  // fn_18_AB9C: the weighted average over a list.
  float ListAverage(const SEmperorIngList& list) const;
  // fn_18_1978: the module's flag check over the list at guest 0x850.
  void CheckList(CStateManager& mgr);
  // fn_18_125C: the module watches the object its id at guest 0x8C0 names and clears the id
  // when the object is gone.
  void WatchObject(CStateManager& mgr);
  // fn_18_E40: the per-frame light-swarm, effect and portal-actor update.
  void UpdateLightSwarm(CStateManager& mgr, float dt);
  // fn_18_2D1C: the vector from the actor to the first player, flattened to the XY plane.
  CVector3f PlayerDirection(CStateManager& mgr) const;
  // fn_18_2CB8: the distance from the actor to the player.
  float PlayerDistance(CStateManager& mgr) const;
  // fn_18_25C: the distance to the static geometry ahead, along the facing; false on a miss.
  bool ForwardClearance(CStateManager& mgr, float& distance) const;
  // fn_18_2C98: the actor's facing, flattened to the XY plane.
  CVector3f FlatFacing() const;
  // fn_18_1BD8: both hearts get the module's vulnerability at guest 0x86C.
  void SetHeartVulnerability(CStateManager& mgr) const;
  // fn_18_B04: the stage record for the actor's health percentage (guest 0xB54..0xC98).
  const SLdrEmperorIngStage3Stage& CurrentStage();
  // fn_18_AA4: a stage's action record (0 stampede .. 6 dark flier), null past six.
  static const SLdrEmperorIngStage3Action* StageAction(const SLdrEmperorIngStage3Stage& stage,
                                                       int action);
  // fn_18_458C, 431C, 4468, 4528, 44C4, 4344, 436C: whether ChoseAction may pick an action.
  bool CanStampede(CStateManager& mgr) const;
  bool CanJumpSlide(CStateManager& mgr) const;
  bool CanLightSwarm(CStateManager& mgr) const;
  bool CanDarkBeam(CStateManager& mgr) const;
  bool CanLightBeam(CStateManager& mgr) const;
  bool CanPortalAttack(CStateManager& mgr) const;
  bool CanDarkFlier(CStateManager& mgr) const;
  // fn_18_408: the heart ChangeVulnerability turns on (1 dark, 2 light), counted per heart.
  int PickHeart(CStateManager& mgr);
  // fn_18_27DC: the melee contact - the leg locators' box against the player's.
  void MeleeContact(CStateManager& mgr);
  // fn_18_22E0: the stampede and jump-slide contact damage.
  void ChargeContact(CStateManager& mgr);
  // fn_18_1C34: saves the first heart's vulnerability and makes both hearts immune.
  void SetHeartsImmune(CStateManager& mgr);
  // fn_18_2B68: the entry kinds for the list at guest 0x840.
  void AddTauntEntry(int kind);
  // fn_18_1BB0: the heart switch with the heart that is on and no vulnerability change.
  void SetCurrentHeart(CStateManager& mgr);
  // fn_18_1DF4: the heart switch (0 the enraged heart, 1 the dark heart, 2 the light heart).
  void SetHeartState(CStateManager& mgr, int heart, bool applyVulnerability);
  // fn_18_1B88: the heart switch with no heart (3).
  void HeartsOff(CStateManager& mgr);
  // fn_18_12C4: resets and deletes the beam (guest 0x8AC) and removes its sound.
  void StopBeam(CStateManager& mgr);
  // fn_18_1370: turns the beam towards the player and moves its sound with it.
  void SteerBeam(CStateManager& mgr, float dt);
  // fn_18_1530 / fn_18_BB8 / fn_18_4D0: the beam start, kUE_BeginAction's and kUE_EggLay's spawns.
  void StartBeam(CStateManager& mgr);
  void BeginActionEvent(CStateManager& mgr);
  void EggLayEvent(CStateManager& mgr, float dt);
  // fn_18_36BC: the damage's source id against the two hearts' ids.
  void OnDamage(CStateManager& mgr, u16 sourceId);
  // fn_18_3448: the heart damage update (fn_18_2F04) and the heart effect's external params.
  void UpdateHearts(CStateManager& mgr, float dt);
  // fn_18_2F04: the heart hit timer, both hearts' reactions and the vulnerability step.
  void UpdateHeartDamage(CStateManager& mgr, float dt);
  // fn_18_3164: one heart's damage reaction; answers whether the heart is dead.
  bool HeartDamageReaction(CStateManager& mgr, int heart);
  // fn_18_2D50: the hit object's id looked up in the heart holder.
  void OnHitObject(CStateManager& mgr, u16 sourceId);
  // The holder's record with the id that is looked for.
  const CCollisionActor* HeartCollisionActorById(CStateManager& mgr, u16 id) const;
  // fn_18_3B14: the module's heart record (0 the first heart, 1 the second).
  const CJointCollisionDescription* HeartRecord(int heart) const;
  // fn_18_3ACC: the collision actor of a heart.
  CCollisionActor* HeartCollisionActor(CStateManager& mgr, int heart) const;

  // Host layout. The guest offsets are the REL's (fn_18_9A8C); the full member map, including
  // the parts this milestone does not build, is in docs/module-analysis/EmperorIngStage3.md.
  float mVulnerableDamageThreshold;      // guest 0x814, the data's vulnerableDamageThreshold
  float mTauntFrequency;                 // guest 0x91C, the data's tauntFrequency
  CHealthInfo mYellowHealth;             // guest 0x920, the data's yellowHealth
  CHealthInfo mHealth;                   // guest 0x940, the data's health
  CDamageVulnerability mRedVulnerability;   // guest 0x960, the data's redVulnerability
  CDamageVulnerability mLightVulnerability; // guest 0x990, the data's lightVulnerability
  CDamageVulnerability mDarkVulnerability;  // guest 0x9C0, the data's darkVulnerability
  CDamageInfo mMeleeDamage;              // guest 0x9F0, the data's meleeDamage
  CDamageInfo mStampedeDamage;           // guest 0xA0C, the data's stampedeDamage
  CDamageInfo mJumpSlideDamage;          // guest 0xA28, the data's jumpSlideDamage
  CDamageInfo mGroundPoundDamage;        // guest 0xA44, the data's groundPoundDamage
  // fn_18_44: GetModelAlphau8 reads the byte at guest 0x44F. It sits in CPatterned's own area in
  // the retail object, so the host keeps it here.
  // The module's own two flag bytes at guest 0x824 and 0x825. The retail word at 0x824 is 32 bits
  // wide, but every test the module makes is a single byte's bit, so the host keeps the two bytes.
  uchar x824_;                           // guest 0x824, 0: 0x02 the listen flag, 0x04/0x08 the
                                        // jump-attack and dodge flags
  uchar x825_;                           // guest 0x825, 0: 0x01 the stampede-react flag, 0x04 the
                                        // aborted-jump-slide flag
  uchar x826_;                           // guest 0x826, 0: 0x80 the heart-reacted flag
                                        // (fn_18_3164 sets it)
  CCollisionActorManager* x7c0_;        // guest 0x7C0, 0: the collision actors (fn_18_3BB0
                                        // makes them, fn_18_3B14 reads the hearts)
  int mVulnerabilityState;               // guest 0x7C4, 0: 1 is the vulnerable state
  float mX7c8_;                          // guest 0x7C8, 0.0f: the taunt timer
  float mX7cc_;                          // guest 0x7CC, 0.0f: the eye-shut timer
  float mX7d4_;                          // guest 0x7D4, 0.0f: the too-close distance
  float mX7d0_;                          // guest 0x7D0, 0.0f: the time since the last attack
  float mX7e0_;                          // guest 0x7E0, 10000.0f: the jump-attack timer
  float mAttackTimer;                    // guest 0x7E4, 4.0f in the constructor
  int mAction;                           // guest 0x7E8, -1 in the constructor
  float mX810_;                          // guest 0x810, 0.0f: the damage taken while vulnerable
  CDamageVulnerability mX86c_;           // guest 0x86C: the hearts' vulnerability
  u16 mX8c0_;                            // guest 0x8C0, 0xFFFF: the id of the object the
                                        // module watches (fn_18_125C clears it)
  u16 x8c2_;                             // guest 0x8C2: the INTERNAL_STATE_00 connection
  u16 x8c4_;                             // guest 0x8C4: INTERNAL_STATE_01/PLAY
  u16 x8c6_;                             // guest 0x8C6: INTERNAL_STATE_01/ACTIVATE
  u16 x8c8_;                             // guest 0x8C8: INTERNAL_STATE_02/DEACTIVATE
  rstl::vector< SEmperorIngPortal > x8cc_; // guest 0x8CC: INTERNAL_STATE_02/ACTIVATE actors
  SEmperorIngList mX830_;                 // guest 0x830: the module's first list
  SEmperorIngList mX840_;                 // guest 0x840: the list the taunt average reads
  SEmperorIngList mX850_;                 // guest 0x850: the list the flag check reads
  float mX8ec_;                          // guest 0x8EC, 0.0f: the eye-shut time
  float mX8f0_;                          // guest 0x8F0, 0.0f: the eye-shut duration
  float mX8f4_;                          // guest 0x8F4, 0.0f: the eye-shut random time
  CVector3f mX910_;                      // guest 0x910, 0: the jump attack's target
  CVector3f mX818_;                      // guest 0x818, 0: the facing the idle state turns
                                        // towards the player (fn_18_3804 writes it)
  float mX864_;                          // guest 0x864, 0.0f: the idle state's timer
  int mX868_;                            // guest 0x868, -1: the heart that is on
  int x8a4_;                             // guest 0x8A4, -1: the dodge's step direction
  float mX7d8_;                          // guest 0x7D8, 100000.0f: the dark-flier timer (fn_18_E40)
  float mX7dc_;                          // guest 0x7DC, 100000.0f (0x9B5C): the time since the light
                                        // swarm went (fn_18_E40)
  rstl::reserved_vector< int, 8 > mActionHistory; // guest 0x7EC: the last actions ChoseAction picked
  int mX89c_;                            // guest 0x89C, 0: the light-heart count (fn_18_408)
  int mX8a0_;                            // guest 0x8A0, 0: the dark-heart count (fn_18_408)
  int x8a8_;                             // guest 0x8A8, -1: the taunt type
  int mX8e0_;                            // guest 0x8E0, 0: the portal attack's count (fn_18_508)
  int mX8e4_;                            // guest 0x8E4, 0: the portal attack's portal, 0..3
  float mX828_;                          // guest 0x828, FLT_MAX: the jump slide's end time
  float mX82c_;                          // guest 0x82C, 0.0f: the heart effect's growth
  float mX860_;                          // guest 0x860, 0.0f: the heart hit timer
  float mX8e8_;                          // guest 0x8E8, 0.0f: the jump slide's extra time
  float mX8dc_;                          // guest 0x8DC, 0.0f: set to 1.5 by kUE_ObjectDrop
  u16 mBeamId;                           // guest 0x8AC, 0xFFFF: the beam (fn_18_1530)
  CSfxHandle mBeamSfx;                   // guest 0x8B0, 0: the beam's looped sound
  CVector3f mX8b4_;                      // guest 0x8B4, 0: the point the beam aims at
  u16 mShockWaveId;                      // guest 0x90C, 0xFFFF: the jump attack's shock wave
  CSfxHandle mShockWaveSfx;              // guest 0x908, 0: the shock wave's sound
  CShockWaveInfo mJumpAttackShockWave;   // guest 0xE90, the data's jumpAttackShockWaveInfo
  u16 mJumpAttackSound;                  // guest 0xECC, the data's sound
  CProjectileInfo mDarkBeamProjectile;   // guest 0xA60: darkBeamAttack's weapon and damage
  SLdrPlasmaBeamInfo mDarkBeamInfo;      // guest 0xA88: darkBeamAttack's beam info (fn_18_A794)
  u16 mDarkBeamSound;                    // guest 0xAD0: darkBeamAttack's beamSound
  CProjectileInfo mLightBeamProjectile;  // guest 0xAD4: lightBeamAttack's weapon and damage
  SLdrPlasmaBeamInfo mLightBeamInfo;     // guest 0xAFC: lightBeamAttack's beam info
  u16 mLightBeamSound;                   // guest 0xB44: lightBeamAttack's beamSound
  rstl::optional_object< TLockedToken< CGenDescription > > mPortalEffect; // guest 0xD04
  u16 mPortalSound;                      // guest 0xD14: portalAttack's portalOpenSound
  CDamageInfo mPortalDamage;             // guest 0xD18: portalAttack's projectileDamage
  SLdrPlasmaBeamInfo mPortalBeamInfo;    // guest 0xD34: portalAttack's beamInfo
  float mX8f8_;                          // guest 0x8F8, 0.0f: the player knock-back timer
  CVector3f mX8fc_;                      // guest 0x8FC, 0: the player knock-back direction
  float mVulnerableTime;                 // guest 0xB4C, the data's vulnerableTime
  float mVulnerableDamageThresholdReset; // guest 0xB50, the data's vulnerableDamageThreshold
                                        // (GrowHeart restores guest 0x814 from it)
  SLdrEmperorIngStage3Stage mStages[4];  // guest 0xB54, 0xBC0, 0xC2C, 0xC98: the data's stage1..4
  float mBaseTurnSpeed;                  // guest 0xE5C, the patterned info's turn speed
  CAssetId mLightSwarmEffect;            // guest 0xB48, the data's lightSwarmEffect
  CBasicSwarmData mLightSwarmData;       // guest 0xD7C, the data's lightSwarmProperties
  SLdrAudioPlaybackParms mLightSwarmDeathSound;  // guest 0xE60, the data's lightSwarmDeathSound
  SLdrAudioPlaybackParms mLightSwarmExpireSound; // guest 0xE78, the data's audioPlaybackParms
};
#endif

#endif
