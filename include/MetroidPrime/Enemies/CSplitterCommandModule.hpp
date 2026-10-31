#ifndef _CSPLITTERCOMMANDMODULE
#define _CSPLITTERCOMMANDMODULE

class CCollisionActorManager;
class CParticleGenInfo;

#include "MetroidPrime/CActorParameters.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"
#include "MetroidPrime/ScriptLoader/SLdrSplitterCommandModule.hpp"
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"

#include "Kyoto/Audio/CSfxHandle.hpp"

#include "REL/REL_Setup.h"

// Host copy of the SPLU parameter block (docs/module-analysis/Splitter.md section c). Retail
// reads the loader's values into a runtime struct (fn_75_791C) and copies them into the object
// (fn_75_72BC, 1:1 from the loader's stack struct at 0x7C0). The host keeps the scalars, the
// *converted* damage infos and vulnerabilities and the beam info; the raw loader copies are not
// kept, because they duplicate the converted ones. Nothing in this milestone reads the values -
// the class keeps them for the state machine the later milestones add.
class CSplitterCommandModuleData {
public:
  CSplitterCommandModuleData()
  : mUnknownBD80FD94(0)
  , mMaxLinearVelocity(0.f)
  , mMaxTurnSpeed(0.f)
  , mScanningTurnSpeed(0.f)
  , mMinLaserPulseAttackTime(0.f)
  , mMinLaserPulseRange(0.f)
  , mMaxLaserPulseRange(0.f)
  , mMaxLaserPulseShots(0)
  , mLaserPulseProjectile(kInvalidAssetId)
  , mLaserPulseDamage()
  , mMinDodges(0)
  , mMaxDodges(0)
  , mDodgeChance(0.f)
  , mResetShieldTime(0.f)
  , mShieldHP(0.f)
  , mLaserSweepTurnSpeed(0.f)
  , mLaserSweepDamage()
  , mLaserSweepBeamInfo()
  , mUnknown9EC51FE4()
  , mSoundLaserSweep(0)
  , mSoundLaserChargeUp(0)
  , mSoundDocking(0)
  , mSoundScanning(0)
  , mSoundLightShield(0)
  , mSoundDarkShield(0)
  , mSoundShieldOn(0)
  , mLightShieldVulnerability(CDamageVulnerability(CWeaponTypeVulnerability::Immune()))
  , mDarkShieldVulnerability(CDamageVulnerability(CWeaponTypeVulnerability::Immune())) {}

  int mUnknownBD80FD94;            // 0x00 unknown_0xbd80fd94
  float mMaxLinearVelocity;        // 0x04
  float mMaxTurnSpeed;             // 0x08
  float mScanningTurnSpeed;        // 0x0C
  float mMinLaserPulseAttackTime;  // 0x10
  float mMinLaserPulseRange;       // 0x14
  float mMaxLaserPulseRange;       // 0x18
  int mMaxLaserPulseShots;         // 0x1C
  CAssetId mLaserPulseProjectile;  // 0x20
  CDamageInfo mLaserPulseDamage;   // 0x24 (converted; guest 0xCD4)
  int mMinDodges;                  // 0x34
  int mMaxDodges;                  // 0x38
  float mDodgeChance;              // 0x3C
  float mResetShieldTime;          // 0x40
  float mShieldHP;                 // 0x44
  float mLaserSweepTurnSpeed;      // 0x48
  CDamageInfo mLaserSweepDamage;   // 0x4C (converted; guest 0xCF0)
  SLdrPlasmaBeamInfo mLaserSweepBeamInfo; // 0x5C (guest 0x81C)
  SLdrUnknownStruct42 mUnknown9EC51FE4;  // 0xA4 (guest 0x864)
  int mSoundLaserSweep;            // 0xC4
  int mSoundLaserChargeUp;         // 0xC8
  int mSoundDocking;               // 0xCC
  int mSoundScanning;              // 0xD0
  int mSoundLightShield;           // 0xD4
  int mSoundDarkShield;            // 0xD8
  int mSoundShieldOn;              // 0xDC
  CDamageVulnerability mLightShieldVulnerability; // 0x25C (converted; guest 0xD0C)
  CDamageVulnerability mDarkShieldVulnerability;  // 0x3B8 (converted; guest 0xD3C)
};

// The REL's CSplitterCommandModule (module 75): the "Quad" robot's detachable head, script type
// SPLU ("Mechanism: Quad CM"). 0xF70 bytes on the GameCube (the loader's __nw__ at 0x00000340),
// built by fn_75_6B08; its vtable lbl_75_data_448 (82 words) and the analysis are
// docs/module-analysis/Splitter.md.
//
// Milestone 1 is load-and-think, like the main chassis: the loader, the constructor and the
// vtable's accessors. The state machine (fn_75_5E8C installs the 17 triggers, 14 states and 7
// codes), the scanning, the laser pulse and sweep, the shields, the docking and the beam effect
// actor are later milestones, and every override that belongs to them forwards to CPatterned
// behind a once-only StandIn log.
class REL_EXPORT CSplitterCommandModule : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  // Argument order is the port's own (the REL loader's call is
  // (uid, editor properties, entity info, transform, model, actor parameters, patterned info)
  // plus the parameter block).
  CSplitterCommandModule(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                          const CTransform4f& xf, const CModelData& modelData,
                          const CActorParameters& actParms, const CPatternedInfo& pInfo,
                          const CSplitterCommandModuleData& data);
  ~CSplitterCommandModule() override;

  // CEntity / CActor
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  void Render(const CStateManager& mgr) const override;
  const CHealthInfo* GetHealthInfo() const override;
  const CDamageVulnerability* GetDamageVulnerability() const override;
  rstl::optional_object< CAABox > GetTouchBounds() const override;
  CVector3f GetAimPosition(const CStateManager& mgr, float dt) const override;

  // CAi
  void KnockBack(CStateManager& mgr, const CKnockBackInfo& info) override;
  bool CanBeShot(const CStateManager&, int) override { return true; }
  bool IsListening() const override { return true; }
  bool Listen(CStateManager&, const CVector3f&, EListenNoiseType) override;

  // CPhysicsActor
  void CollidedWith(const TUniqueId& id, const CCollisionInfoList& list,
                    CStateManager& mgr) override;

  // CPatterned. The accessors below are the REL's own one-line bodies (the module's .text starts
  // with them), so they are real, not stand-ins.
  // @ fn_75_8
  CProjectileInfo* ProjectileInfo() override { return &mProjectileInfo; } // `addi r3, r3, 0xe5c` (0x8)
  using CPatterned::GetSearchPath;
  // @ fn_75_0
  CPathFindSearch* GetSearchPath() override { return &mPathFindSearch; } // `addi r3, r3, 0xd6c` (0x0)
  using CPatterned::GetPointSearchPath;
  CPathFindPointSearch* GetPointSearchPath() override { return nullptr; }
  uchar GetModelAlphau8(const CStateManager&) const override { return mColor.GetAlphau8(); }
  CRagDoll* GetRagDoll() const override { return nullptr; }
  void SetupStateMachine(CStateManager& mgr) override;
  bool CanBeIngPossessed(CStateManager& mgr) const override;

  // The head's state machine names. The REL's table entry is 16 bytes
  // {const char* name, 0, 0xFFFFFFFF, fn}: the module's static initialiser
  // (fn_75_7C80, in the REL's .ctors) copies the three-word records of .data
  // 0x0..0x2D0 into the three tables, so the retail function of each name is that
  // record's third word (docs/module-analysis/Splitter.md section b). The ported ones are
  // the IsScanning trigger (fn_75_58E8), the IsInitiallyDocked (fn_75_58DC), the IsDocked
  // (fn_75_58BC), the HasDockingTarget (fn_75_57F0) and the HasTarget (fn_75_57B4)
  // triggers, the Start state (fn_75_5564), the NotifyDocking code (fn_75_3EEC) and the
  // RaiseShields code (fn_75_3B20); the rest stand in for the retail function and log it
  // once when the state machine runs them.
  bool StateOver(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_5A40
  bool InDetectionRange(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_5944
  bool IsScanning(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_58E8
  bool IsInitiallyDocked(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_58DC
  bool IsDocked(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_58BC
  bool HasDockingPath(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_5864
  bool HasDockingTarget(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_57F0
  bool DockingPathOver(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_57D4
  bool HasTarget(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_57B4
  bool InHoverRange(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_570C
  bool InLaserPulseRange(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_565C
  bool ShouldDodge(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_5644
  bool ShouldAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_5624
  bool ShouldFireAgain(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_5610
  bool ShouldLaserSweep(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_55F8
  bool PathOver(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_55B0
  bool PathShagged(CStateManager& mgr, const CTriggerData& data) const;  // fn_75_5568

  void Start(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_5564
  void Idle(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_5538
  void SpawnIdle(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_5498
  void Dead(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_5448
  void Scanning(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_523C
  void FaceTarget(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_50B0
  void PathFind(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_4E90
  void Hover(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_4D04
  void Dodge(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_4AC0
  void LaserPulse(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_4864
  void LaserSweep(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_44DC
  void LostChassisReaction(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_42F8
  void SeekMainChassis(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_4138
  void FollowDockingPath(CStateManager& mgr, EStateMsg msg, float dt);  // fn_75_3FE0

  void NotifyDocking(CStateManager& mgr, float dt);  // fn_75_3EEC
  void SelectTarget(CStateManager& mgr, float dt);  // fn_75_3E60
  void SetTargetDest(CStateManager& mgr, float dt);  // fn_75_3D20
  void SetDockingDest(CStateManager& mgr, float dt);  // fn_75_3BEC
  void FindBestDodgeDirection(CStateManager& mgr, float dt);  // fn_75_3B34
  void RaiseShields(CStateManager& mgr, float dt);  // fn_75_3B20
  void ResetAttackTimes(CStateManager& mgr, float dt);  // fn_75_3ADC

  // CEntity
  CEntity* TypesMatch(int typeId) const override;

  // guest 0xF24-0xF2C: the vector the chassis's fn_75_ACEC adds to the position it looks from.
  const CVector3f& GetEditorProperties() const { return mEditorProperties; }
  // The retail's fn_75_5A80 (the laser origin) and fn_75_5AD8 (the sweep start), which the
  // chassis's SetupLaserSweep (fn_75_B678) calls.
  CVector3f PortLaserOrigin(CStateManager& mgr) const;
  void PortStartLaserSweep(const CVector3f& first, const CVector3f& second);
  void PortFireLaserSweep(CStateManager& mgr, const CVector3f& end);  // fn_75_1974
  int GetLaserSweepStage() const { return mLaserSweepStage; }  // guest 0xF34
  // guest 0xEFA: the chassis the head is docked in (the chassis's fn_75_8EC8 reads it, 0x00008F68).
  TUniqueId GetChassisId() const { return mChassisId; }
  // guest 0xEFC: the chassis the head is checking (the chassis's WaitForDocking reads it).
  TUniqueId GetCheckedChassisId() const { return mCheckedChassisId; }

  // The retail's fn_75_2994 (the head's turn upright), fn_75_23F0 (its separation from other
  // undocked AIs) and fn_75_2280 (the search for a free chassis).
  void PortTurnUpright(float dt);
  CVector3f PortSeparationVector(CStateManager& mgr) const;
  void PortFindChassis(CStateManager& mgr);
  // The retail's fn_75_270C (the dodge direction pick) and fn_75_25C4 (a clear dodge path).
  int PortDodgeDirection(CStateManager& mgr);
  bool PortDodgeClear(CStateManager& mgr, const CVector3f& dir, float dist);
  // The retail's fn_75_5D50: the head's shield reset (the chassis's Death and its spin, dodge and
  // docking states call it).
  void PortResetShield(CStateManager& mgr);
  // The retail's fn_75_5DDC: the head's spin shield (the chassis's SetupSpinAttack calls it).
  void PortSpinShield(CStateManager& mgr);
  // The retail's fn_75_5CC4 (the chassis's Dodge state's shield) and fn_75_5B28 (its WaitForSnatch
  // state's).
  void PortDodgeShield(CStateManager& mgr);
  void PortSnatchShield(CStateManager& mgr);

private:
  // The chassis's AutoDestruct table entry is this class's fn_75_5A54 (PortAutoDestruct).
  friend class CSplitterMainChassis;
  // The Digital Guardian head's DestroyActiveSplitters (fn_14_14C24) calls fn_75_5A54 too.
  friend class CDigitalGuardianHead;
  void EnsureBodyActive(CStateManager& mgr);

  // Host layout (retail's object is 0xF70, CPatterned is 0x7C0; the guest offsets are the REL's
  // and are in docs/module-analysis/Splitter.md section e). The host class has its own layout and
  // only keeps what this milestone needs.
  CSplitterCommandModuleData mData; // guest 0x7C0 (the parameter block fn_75_72BC copies in)
  CPathFindSearch mPathFindSearch;  // guest 0xD6C (fn_75_0 returns this+0xD6C)
  CProjectileInfo mProjectileInfo;  // guest 0xE5C (fn_75_8 returns this+0xE5C)
  CProjectileInfo mLaserSweepProjectileInfo;  // guest 0xE84 (the laser sweep beam, fn_75_1974)
  // guest 0xE58: the retail's collision actor manager (the command module's Think updates it
  // at 0x64C8). It holds the head's joint collision descriptions, which belong to the state
  // machine, so the port starts it empty.
  CCollisionActorManager* mCollisionActorManager = nullptr;

  // The module's auto-destruct (fn_75_5A54) and the retail's fn_75_21FC, which runs it when
  // the timer reaches 5.0f. The auto-destruct's own body is still unknown (the module note's
  // section j), so it is the same once-logging shell the main chassis has.
  void PortAutoDestruct(float time);
  void PortAutoDestructTimer(float dt);

  // guest 0xF6A: the flag byte; the retail's fn_75_21FC tests its 0x01 (the
  // word's bit 31) and clears it, and the retail's RaiseShields code (fn_75_3B20) sets its
  // 0x08 (the word's bit 28).
  uchar mAutoDestructFlags = 0;
  // guest 0xEF4: the auto-destruct timer the same function advances.
  float mAutoDestructTimer = 0.f;

  // The retail's fn_75_184C: the target it looks at, the locator it looks from, the offset it
  // adds and the sound handle. The retail's own writers of those fields are in the state
  // machine, which the port has not entered, so they keep their defaults here.
  void PortLookAtTarget(CStateManager& mgr, float dt);
  TUniqueId mLookAtTargetId = kInvalidUniqueId;  // guest 0xF02 (the u16 the retail keeps here)
  CSegId mLookAtSegment;                         // guest 0xEF8 (the locator)
  CVector3f mLookAtOffset;                       // guest 0xF50, 0xF54, 0xF58
  CSfxHandle mLookAtSfxHandle;                      // guest 0xF5C (the sound handle)

  // guest 0xF24-0xF2C: the editor-properties vector - the retail's constructor copies the
  // editor's three floats into it (fn_75_6B08) and the head's states overwrite it; the Idle
  // state (fn_75_5538) puts the transform's Y column there.
  CVector3f mEditorProperties;

  // The retail's fn_75_1578: the head's two effect states ("AlertEye" and "LaserMuzzle").
  // guest 0xEDC is the counter the four shield functions set (0, 1, 2 or 3); with 1 or 2 both
  // effects are off.
  void PortUpdateEffectStates(CStateManager& mgr);
  uint mShieldState = 0;  // guest 0xEDC
  // guest 0xEAC: the head's own damage vulnerability (the constructor copies the base's, fn_75_70A0);
  // the vulnerability slot (fn_75_616C) returns it and the shield functions overwrite it.
  CDamageVulnerability mOwnVulnerability;

  // The retail's fn_75_2154: the head's own move. With the auto-destruct timer running the
  // head is teleported to the destination, otherwise it moves towards it in one frame.
  void PortMoveTo(const CVector3f& dest, float dt);

  // The fields the retail's fn_75_31A8 touches.
  void PortUpdateChassis(CStateManager& mgr);
  TUniqueId mChassisId = kInvalidUniqueId;       // guest 0xEFA (the chassis it looks up)
  TUniqueId mSyncedId = kInvalidUniqueId;        // guest 0xEFE (the copy of the chassis's 0xCC0)
  int mLaserPulseShots = 0;  // guest 0xF20 (ResetAttackTimes, fn_75_3ADC)
  float mDodgeDistance = 0.f;  // guest 0xEE8 (the constructor, 0x6EEC-0x6EF8)
  int mDodgeCount = -1;        // guest 0xEEC (the constructor's -1, 0x6C18/0x6C30)
  int mDodgeDirection = -1;    // guest 0xF30 (FindBestDodgeDirection, fn_75_3B34)
  TUniqueId mCheckedChassisId = kInvalidUniqueId;// guest 0xEFC (the second chassis it checks)
  TUniqueId mClearedId = kInvalidUniqueId;        // guest 0x12A (cleared with 0xEFA at 0x324C)
  // guest 0xF34: the laser sweep stage. The constructor leaves -1 (0x6CA0, 0x6CE4), fn_75_5AD8
  // sets 0 for a docked head (0x5AF4), the LaserSweep state (fn_75_44DC) advances and ends it,
  // and the ShouldLaserSweep trigger (fn_75_55F8) tests it against -1.
  int mLaserSweepStage = -1;
  // guest 0xF38 / 0xF44: the laser sweep's two ends (fn_75_5AD8 keeps them; LaserSweep,
  // fn_75_44DC, swaps them and turns toward each in turn).
  CVector3f mLaserSweepStart = CVector3f::Zero();
  CVector3f mLaserSweepEnd = CVector3f::Zero();
  // The retail's fn_75_3EEC: the head's "dock to the chassis". The id at 0xEFC (the connected
  // object the XAND message found) is the chassis; when the chassis accepts this head the id
  // moves to 0xEFA - the id the beam-effect gate (fn_75_58E8) and the chassis check (fn_75_31A8)
  // read. The retail calls this from its state machine, whose tables hold names only, so the
  // port calls it from the Think.
  void PortDockToChassis(CStateManager& mgr);
  float mChassisTimer = 0.f;                     // guest 0xF14 (set to 3.0f at 0x3230)

  // The fields the retail's fn_75_32F4 touches.
  void PortUpdateFlashAndDeath(CStateManager& mgr, float dt);
  float mCountdownTimerA = 0.f;                // guest 0xF10 (counted down at 0x332C)
  float mCountdownTimerB = 0.f;                // guest 0xF0C (counted down at 0x333C)
  float mCountupTimerA = 0.f;                  // guest 0xF18 (counted up at 0x3354)
  float mCountupTimerB = 0.f;                  // guest 0xF1C (counted up at 0x3360)
  float mFlashAlpha = 0.f;                     // guest 0xF08 (the Lerp's weight, 0x3368)
  CColor mFlashColor;                          // guest 0xF04 (the Lerp's result, 0x33C8)
  float mDeathTimer = 0.f;                     // guest 0xEF0 (counted down at 0x33EC)

  // The fields the retail's fn_75_3004 touches.
  void PortUpdateCollision(CStateManager& mgr);
  CSfxHandle mSoundHandle;                       // guest 0xF60 (the sound handle, 0x306C)
  TUniqueId mSoundTargetId = kInvalidUniqueId;  // guest 0xF00 (the id it looks up, 0x3078)
  int mCollisionState = 0;                      // guest 0xEE0 (the state, 0x30C4)
  int mPreviousCollisionState = 0;              // guest 0xEE4 (the previous state, 0x315C)

  // The retail's fn_75_2CE0: the head's shield. It applies the state at 0xEE0 to the beam
  // collision actor (the id at 0xF00): the materials, the damage vulnerability, the two effect
  // states and the shield's sound emitter (the handle at 0xF60). The flag is the "a shield goes
  // up" one: it plays the shield-on sound (the sound id at 0x89C).
  void PortUpdateShield(CStateManager& mgr, bool shieldUp);
  // The retail's fn_75_2BE0: the shield's particle effect. The state at 0xEE0 picks the name
  // (1 gives "LightShield", any other state "DarkShield", the state 0 nothing) and the body
  // returns the animation data's first particle effect with that name.
  CParticleGenInfo* PortGetShieldEffect();
  // The retail's fn_75_10D0: the module's own transform - the actor's own transform with the
  // "Beam_LCTR" locator's translation moved through it.
  void PortModuleTransform(CTransform4f& out) const;

  // The retail's fn_75_1188 (the head's beam-effect spawner) and the gate it asks first
  // (fn_75_58E8: the chassis must be there and its scan timer must run).
  void PortSpawnBeamEffect(CStateManager& mgr);
  bool PortChassisTimerRunning(CStateManager& mgr) const;
  // guest 0xF68: the id of the spawned beam effect. The retail looks it up at the start of
  // fn_75_1188 and keeps the id of the actor it creates in it.
  TUniqueId mBeamEffectId = kInvalidUniqueId;
  // guest 0xF64: the beam effect's looped sound (the data's 0x890, AddEmitter at 0x13E0).
  CSfxHandle mBeamSfxHandle;
  // The retail's fn_75_3A8C (the connected-object lookup) and fn_75_2CC0 (the flash start).
  void PortUpdateConnectedObject(CStateManager& mgr);
  void PortStartFlashIfTarget(const CScriptMsg& msg);
  // The retail's fn_75_1790: the laser-pulse projectile's despawn.
  void PortDespawnProjectile(CStateManager& mgr);
  // The retail's fn_75_3460: the head's collision actor manager.
  void PortCreateCollisionActorManager(CStateManager& mgr);

  // -------------------------------------------------------------------------------------------
  // helpers (this lane's half): the module's stateless helper bodies, ported from the REL's asm
  // (src/MetroidPrime/Enemies/PortCSplitterHelpers.cpp). Append-only: the FSM lane's own members
  // are above. The retail function each one stands for is named on it.
  void PortAddToRenderer(const CStateManager& mgr) const;  // fn_75_6310
  void PortPreRender(CStateManager& mgr);                 // fn_75_633C
  void PortRender(const CStateManager& mgr) const;         // fn_75_6368
  bool PortCanBeIngPossessed(CStateManager& mgr) const;   // fn_75_5E50
  bool PortListen(CStateManager& mgr, const CVector3f& position, EListenNoiseType type);  // fn_75_5F18
  // The retail's fn_75_6394 is the head's DoUserAnimEvent (the REL's vtable slot 0x70 - the DOL's
  // slot 0x70, not the GetSortingBounds at 0x6C; the note's section f row was off by one slot).
  // The port had no override, so the DOL's own DoUserAnimEvent ran.  The body is in
  // src/MetroidPrime/Enemies/PortCSplitterHelpers.cpp.
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;
  void PortDoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                           float dt);
  // The retail's fn_75_1CA0 (the head's projectile aim, the callee of fn_75_6394's kUE_Projectile
  // case) is not ported yet: it is a shell that logs.
  void PortHeadProjectileAim(CStateManager& mgr, const rstl::string& locatorName);
};

#endif // _CSPLITTERCOMMANDMODULE
