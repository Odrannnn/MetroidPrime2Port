#ifndef _CDARKSAMUS
#define _CDARKSAMUS

#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "REL/REL_Setup.h"

#ifdef TARGET_PC
#include "MetroidPrime/CAnimationParameters.hpp"
#include "MetroidPrime/CActorParameters.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"
#include "rstl/optional_object.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"
#include "MetroidPrime/ScriptLoader/Structs/SLdrAudioPlaybackParms.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "Kyoto/TToken.hpp"
#include "Weapons/CCollisionResponseData.hpp"
#include "rstl/auto_ptr.hpp"
#include "Collision/CCollidableAABox.hpp"

class CBCProjectileAttackCmd;

// The fight script's object (the guest's CScriptDarkSamusBattleStage), whose stage info the
// ShouldFadeOut and CanMissileDeflect triggers read.
class CDarkSamusBattleStage;
class CParticleSwoosh;
class CSwooshDescription;
class CEnergyProjectile;
class CScriptAIHint;
struct SLdrDSStageInfo;

// Host copy of the properties the DarkSamus loader reads (fn_10_CF44 reads the module's 63
// property ids, which are exactly the ids of the generated SLdrDarkSamus reader). Retail keeps no
// single block of them: the loader hands them to the constructor (fn_10_1FA14) as eight floats
// plus about fifty stack arguments, and the constructor spreads them over the sub-objects it
// builds (0x94C, 0x9A4, 0x9E0, 0xA0C, 0xA4C, 0xA88, 0xAC0, 0xB50, 0xCD0, 0xD74, 0xDF0, 0xE10,
// 0xEF0, 0xF68, 0xF80, 0x1064). The host therefore keeps them in one struct, one member per
// property (60 of the 63 ids: the editor properties, the patterned AI typedef and the actor
// parameters are the three the loader passes on separately), named after the property it holds.
// Nothing in milestone 1 reads the values: the class
// holds them for the state machine the later milestones add. The guest offsets the constructor
// writes them to are in docs/module-analysis/DarkSamus.md.
struct CDarkSamusData {
  CDarkSamusData()
  : mUnknown72EDEB7D(-1.f)
  , mUnknown74FA22F0(-1.f)
  , mGlideSound(0)
  , mMissileRicochetSound(0)
  , mStartsInTheAir(false)
  , mTXTR(kInvalidAssetId)
  , mUnknown2C6A3344(100.f)
  , mMeleeAttackDamage()
  , mMeleeAttackFX(kInvalidAssetId)
  , mPart(kInvalidAssetId)
  , mDiveAttackDamage()
  , mDiveAttackImpulseVertical(40.f)
  , mDiveAttackImpulseHorizontal(500.f)
  , mDiveAttackEffect(kInvalidAssetId)
  , mScatterShotProjectile(kInvalidAssetId)
  , mScatterShotProjectile2(kInvalidAssetId)
  , mScatterShotDamage()
  , mUnknown378285B9(0.f)
  , mScatterShotProjectilesPerSecond(300.f)
  , mNormalMissileProjectile(kInvalidAssetId)
  , mNormalMissileDamage()
  , mUnknown1F1EF7A9(1)
  , mUnknownC4A1A44E(3)
  , mSuperMissileProjectile(kInvalidAssetId)
  , mSuperMissileDamage()
  , mFreezeBeamProjectile(kInvalidAssetId)
  , mFreezeBeamDamage()
  , mFreezeBeamVisorTexture(kInvalidAssetId)
  , mDamageInterruptThreshold(10.f)
  , mUnknownF317F4D5(4.f)
  , mSweepSwoosh(kInvalidAssetId)
  , mSweepBeamDamage()
  , mSweepBeamCollisionEffect(kInvalidAssetId)
  , mSweepBeamSound(0)
  , mUnknown0EF8DC15(-1)
  , mInvulnerableModel(kInvalidAssetId)
  , mInvulnerableSkinRules(kInvalidAssetId)
  , mBoostBallModel(CAssetId(0), 0, 0)
  , mBoostBallDamage()
  , mBoostBallGlow(kInvalidAssetId)
  , mBoostBallTrailSwoosh(kInvalidAssetId)
  , mSWHC(kInvalidAssetId)
  , mBoostBallRollSound(0)
  , mBoostBallHitPlayerSound(0)
  , mBoostBallCollision(kInvalidAssetId)
  , mBoostBallCollisionSoundOneShot()
  , mIceSpreadFX(kInvalidAssetId)
  , mIceSpreadSound(0)
  , mPart908B06E9(kInvalidAssetId)
  , mPart494DE4A4(kInvalidAssetId)
  , mSound(0)
  , mDamageInfo()
  , mPartE701DAEA(kInvalidAssetId)
  , mPhazonProjectile(kInvalidAssetId)
  , mPhazonSuperMissileProjectile(kInvalidAssetId)
  , mPhazonSuperMissileDamage()
  , mPhazonProjectileDamage()
  , mPhazonEnrageSphere(kInvalidAssetId)
  , mPhazonEnrageSphereContactDamage()
  , mAlternateScannableInfo(kInvalidAssetId) {}

  float mUnknown72EDEB7D;      // unknown_0x72edeb7d
  float mUnknown74FA22F0;      // unknown_0x74fa22f0
  int mGlideSound;             // glideSound
  int mMissileRicochetSound;   // missileRicochetSound
  bool mStartsInTheAir;        // startsInTheAir
  CAssetId mTXTR;              // tXTR
  float mUnknown2C6A3344;      // unknown_0x2c6a3344
  CDamageInfo mMeleeAttackDamage;    // meleeAttackDamage
  CAssetId mMeleeAttackFX;     // meleeAttackFX
  CAssetId mPart;              // pART
  CDamageInfo mDiveAttackDamage;      // diveAttackDamage
  float mDiveAttackImpulseVertical;   // diveAttackImpulseVertical
  float mDiveAttackImpulseHorizontal; // diveAttackImpulseHorizontal
  CAssetId mDiveAttackEffect;  // diveAttackEffect
  CAssetId mScatterShotProjectile;    // scatterShotProjectile
  CAssetId mScatterShotProjectile2;   // scatterShotProjectile2
  CDamageInfo mScatterShotDamage;     // scatterShotDamage
  float mUnknown378285B9;      // unknown_0x378285b9
  float mScatterShotProjectilesPerSecond; // scatterShotProjectilesPerSecond
  CAssetId mNormalMissileProjectile;   // normalMissileProjectile
  CDamageInfo mNormalMissileDamage;    // normalMissileDamage
  int mUnknown1F1EF7A9;        // unknown_0x1f1ef7a9
  int mUnknownC4A1A44E;        // unknown_0xc4a1a44e
  CAssetId mSuperMissileProjectile;    // superMissileProjectile
  CDamageInfo mSuperMissileDamage;     // superMissileDamage
  CAssetId mFreezeBeamProjectile;      // freezeBeamProjectile
  CDamageInfo mFreezeBeamDamage;       // freezeBeamDamage
  CAssetId mFreezeBeamVisorTexture;    // freezeBeamVisorTexture
  float mDamageInterruptThreshold;     // damageInterruptThreshold
  float mUnknownF317F4D5;      // unknown_0xf317f4d5
  CAssetId mSweepSwoosh;       // sweepSwoosh
  CDamageInfo mSweepBeamDamage;        // sweepBeamDamage
  CAssetId mSweepBeamCollisionEffect;  // sweepBeamCollisionEffect
  int mSweepBeamSound;         // sweepBeamSound
  int mUnknown0EF8DC15;        // unknown_0x0ef8dc15
  CAssetId mInvulnerableModel; // invulnerableModel
  CAssetId mInvulnerableSkinRules; // invulnerableSkinRules
  CAnimationParameters mBoostBallModel; // boostBallModel (an SLdrAnimationSet)
  CDamageInfo mBoostBallDamage;        // boostBallDamage
  CAssetId mBoostBallGlow;      // boostBallGlow
  CAssetId mBoostBallTrailSwoosh; // boostBallTrailSwoosh
  CAssetId mSWHC;              // sWHC
  int mBoostBallRollSound;     // boostBallRollSound
  int mBoostBallHitPlayerSound; // boostBallHitPlayerSound
  CAssetId mBoostBallCollision; // boostBallCollision
  SLdrAudioPlaybackParms mBoostBallCollisionSoundOneShot; // boostBallCollisionSound_OneShot
  CAssetId mIceSpreadFX;       // iceSpreadFX
  int mIceSpreadSound;         // iceSpreadSound
  CAssetId mPart908B06E9;      // pART_0x908b06e9
  CAssetId mPart494DE4A4;      // pART_0x494de4a4
  int mSound;                  // sound
  CDamageInfo mDamageInfo;     // damageInfo
  CAssetId mPartE701DAEA;      // pART_0xe701daea
  CAssetId mPhazonProjectile;  // phazonProjectile
  CAssetId mPhazonSuperMissileProjectile; // phazonSuperMissileProjectile
  CDamageInfo mPhazonSuperMissileDamage;  // phazonSuperMissileDamage
  CDamageInfo mPhazonProjectileDamage;    // phazonProjectileDamage
  CAssetId mPhazonEnrageSphere;          // phazonEnrageSphere
  CDamageInfo mPhazonEnrageSphereContactDamage; // phazonEnrageSphereContactDamage
  CAssetId mAlternateScannableInfo;       // alternateScannableInfo
};
#endif

// The REL's CDarkSamus (module 10): the dark Samus boss. 0x1128 bytes on the GameCube (the
// loader's __nw__FUlPCcPCc at 0x0000DA78), built by fn_10_1FA14; its vtable lbl_10_data_CBC
// (174 words, 32 local overrides) and the analysis are docs/module-analysis/DarkSamus.md.
//
// Milestone 1 (this file's first slice) is load-and-think: the loader, the constructor and the
// accessors the vtable gives one-line bodies, so the actor registers, takes kSM_Create and thinks.
// The state machine, the melee / dive / scatter-shot / missile attacks, the boost ball, the
// freeze beam, the sweep beam and the phazon enrage are later milestones, and every override that
// belongs to them forwards to CPatterned behind a once-only stand-in log.
// The dash record at the guest's 0x9E0..0xA08 (fn_10_15348 builds it, the constructor at 0x0001FD04
// and the Dash state on a stack copy at 0x0001518C). fn_10_154FC fills it with the dash it picks.
struct CDarkSamusDash {
  CDarkSamusDash(); // fn_10_15348

  int mSlot;                 // +0x00, the picked dash slot 0..7 (fn_10_154FC at 0x00015F94); -1
  CVector3f mDestination;    // +0x04, the picked slot's end point (0x00015FA8..0x00015FB4)
  CVector3f mStartPosition;  // +0x10, the actor's position at the pick (0x00015FBC..0x00015FCC)
  int mLastSlot;             // +0x1C, the slot of the previous dash (fn_10_12CE0 at 0x00012DB0);
                             // fn_10_1DF74 sets it to -1 on a new cover point
  float mWeight;             // +0x20, the picked slot's weight (0x00015FD4)
  int mStepsLeft;            // +0x24, the extra steps of a double dash (fn_10_14D14 sets 1 at
                             // 0x00014DC0, fn_10_14C50 counts down); BreakDash waits while above 0
  // +0x28, flags, MSB-first: 0x80 (`rlwimi 7,24,24` at 0x000153E0, BreakDash's `extrwi 1,24`)
  // and 0x40 (`rlwimi 6,25,25` at 0x00015398), both clear
  bool mBlocked : 1;    // 0x28 mask 0x80, no slot has any weight: the dash ends (BreakDash)
  bool mDoubleDash : 1; // 0x28 mask 0x40, fn_10_150DC rolled the stage's chanceToDoubleDash
};

// The dive record at the guest's 0xAC0..0xB28 (fn_10_E784 builds it from the data's
// diveAttackDamage and its two impulses, the constructor's call at 0x0001FE64). The +0x4C float
// (fn_10_E910 writes lbl_10_rodata_3EC = -1.0f) has no reader.
struct CDarkSamusDive {
  CDarkSamusDive(const CDamageInfo& damage, float impulseHorizontal,
                 float impulseVertical); // fn_10_E784
  void Reset();                                       // fn_10_E910

  CVector3f mTarget;     // +0x00 (0xAC0), where the dive lands (fn_10_12B38 / fn_10_12960)
  CVector3f mStart;      // +0x0C (0xACC), the actor's position at the launch ('XLAU')
  CVector3f mBounceDir;  // +0x18 (0xAD8), the direction fn_10_1CDD0 bounces the actor along
  CVector3f mLastPos;    // +0x24 (0xAE4), the position the stuck timer measures from
  CDamageInfo mDamage;   // +0x30 (0xAF0), the data's diveAttackDamage (0x0000E83C..0x0000E85C)
  // +0x4C (0xB0C): the float fn_10_E910 writes lbl_10_rodata_3EC = -1.0f to (0x0000E9E0). The
  // REL reads it nowhere else (the guest's offset 0xB0C appears once, in that store).
  float mGuest4C;
  float mImpulseHorizontal; // +0x50 (0xB10), diveAttackImpulseHorizontal (f1, 0x0000E860; the
                            // loader's 0x964 is the constructor's f4, 0x0000DD70)
  float mImpulseVertical;   // +0x54 (0xB14), diveAttackImpulseVertical (f2, 0x0000E864; the
                            // loader's 0x960 is the constructor's f5, 0x0000DD74)
  float mBounceTime;     // +0x58 (0xB18), the time since the bounce
  float mStuckTime;      // +0x5C (0xB1C), the time the actor has not moved 0.2 in flight
  // +0x60 (0xB20), flags, LSB-first (`clrlwi 31` is 0x01, `extrwi 1,30` 0x02 ... `extrwi 1,24` 0x80)
  bool mFromBoost : 1;   // 0x01, the dive starts from the boost ball (the jump type 3)
  bool mBounced : 1;     // 0x02, fn_10_1CDD0 bounced the actor off a wall
  bool mAttackSent : 1;  // 0x04, fn_10_11DDC sent kSS_Attack
  bool mHitFloor : 1;    // 0x08, fn_10_1C99C saw the floor or the player
  bool mLanding : 1;     // 0x10, the landing has started
  bool mHitPlayer : 1;   // 0x20, the dive hit the player (fn_10_138A4)
  bool mLaunched : 1;    // 0x40, 'XLAU' came: the actor is in the air
  bool mDone : 1;        // 0x80, the dive is over
  // +0x68 (0xB28), the CCollidableAABox fn_10_E784 builds (0x0000E8A8, the null box, kMT_Solid). Both
  // writers replace its material list and box whole before it is handed out: 'XALD'
  // (fn_10_1E89C, 0x0001EC38..0x0001EC68, the animation's bounds) and GetCollisionPrimitive
  // (fn_10_1AAC4, 0x0001AC3C..0x0001AC68, the bounds grown for the dive). Mutable because
  // GetCollisionPrimitive is const on the host.
  mutable CCollidableAABox mCollidable{CAABox::MakeNullBox(), CMaterialList()};
};

// The melee contact record at the guest's 0x9A4..0x9E0 (fn_10_EA24 builds it, the constructor's
// call at 0x0001FCA8, and ends with fn_10_EA08 at 0x0000EB6C). On the host only the two fields
// fn_10_EA08 resets; the damage, the "PART" token and the rest of the record stay out until a
// reader (fn_10_12DD0, fn_10_13998, fn_10_13AB4, fn_10_1AE08) is ported.
struct CDarkSamusMeleeContact {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
  CDarkSamusMeleeContact() : mHitUid(kInvalidUniqueId), mHit(0) {}
  void Reset(); // fn_10_EA08

  TUniqueId mHitUid; // +0x38 (0x9DC), kInvalidUniqueId (0x0000EA18)
  uchar mHit;        // +0x3A (0x9DE), 0 (0x0000EA1C)
};

class REL_EXPORT CDarkSamus : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
#ifdef TARGET_PC
  // Argument order is the REL loader's call at 0x0000DD84 (fn_10_CF44): (uid, name, entity info,
  // transform, model, patterned info, then the properties).
  CDarkSamus(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
             const CTransform4f& xf, const CModelData& modelData, const CPatternedInfo& pInfo,
             const CActorParameters& actParms, const CDarkSamusData& data);
  ~CDarkSamus() override;

  // CEntity / CActor. PreThink is not overridden: the REL's vtable keeps the base-class symbol in
  // slot 4 (CPatterned::PreThink).
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  void Render(const CStateManager& mgr) const override;
  const CHealthInfo* GetHealthInfo() const override;
  const CDamageVulnerability* GetDamageVulnerability() const override;
  rstl::optional_object< CAABox > GetTouchBounds() const override;
  CVector3f GetOrbitPosition(const CStateManager& mgr) const override;
  CVector3f GetAimPosition(const CStateManager& mgr, float dt) const override;
  EWeaponCollisionResponseTypes GetCollisionResponseType(const CVector3f& position,
                                                         const CVector3f& direction,
                                                         const CWeaponMode& mode,
                                                         int attributes) const override;
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;
  CScannableObjectInfo* GetScannableObjectInfo() const override;

  // CPhysicsActor
  const CCollisionPrimitive* GetCollisionPrimitive() const override;
  void CollidedWith(const TUniqueId& id, const CCollisionInfoList& list, CStateManager& mgr) override;

  // CAi
  CDamageVulnerability* DamageVulnerability() override;
  void TakeDamage(const CVector3f& direction, float magnitude) override;
  // The three one-line accessors below are the REL's own bodies, so they are real, not stand-ins.
  bool CanBeShot(const CStateManager&, int) override { return true; }   // slot 42, fn_10_CE9C
  bool IsListening() const override { return false; }                  // slot 43, fn_10_CEA4
  bool Listen(CStateManager&, const CVector3f&, EListenNoiseType) override { // slot 44, fn_10_CEAC
    return false;
  }
  CVector3f GetOrigin(const CStateManager&, const CTeamAiRole&,
                      const CVector3f&) const override; // slot 45, fn_10_CEB4

  // CPatterned. The accessors below are the REL's own one-line bodies (the module's .text starts
  // with them), so they are real, not stand-ins.
  // slot 50, fn_10_CE54: the byte at guest this+0x44F, the base's model colour's alpha.
  uchar GetModelAlphau8(const CStateManager&) const override { return mColor.GetAlphau8(); }
  CProjectileInfo* ProjectileInfo() override;                                             // 55
  using CPatterned::GetSearchPath;
  CPathFindSearch* GetSearchPath() override { return &mPathFindSearch; }                    // 56
  using CPatterned::GetPointSearchPath;
  CPathFindPointSearch* GetPointSearchPath() override { return nullptr; }                   // 57
  CRagDoll* GetRagDoll() const override { return nullptr; }                                 // 61
  TUniqueId GetAttackTarget() const override { return kInvalidUniqueId; }                    // 67
  bool IsOnGround() const override { return CPatterned::IsOnGround(); }                     // 68
  float GetGravityConstant() const override { return kDefaultGravityAccel; }                // 69
  const rstl::optional_object< TCachedToken< CGenDescription > >&
  GetDeathExplosionParticle() const override;                                              // 73

  // ---------------------------------------------------------------------------------------------
  // The state machine. fn_10_18D78 (the module's own setup, called from the first Think) hands the
  // machine two tables: lbl_10_data_450 with 49 entries (the triggers) and lbl_10_data_964 with 43
  // entries (the states). The names are the REL's own strings and the order is the table order;
  // the machine finds a node's handler by name. The comment on each name is the REL's function that
  // implements it (the .data record beside the entry holds its offset in lbl_10_data_CBC).
  void SetupStateMachine(CStateManager& mgr) override;
  void InstallStateMachineFunctions();

  // The 49 triggers (lbl_10_data_450's order).

  bool Alerted(CStateManager& mgr, const CTriggerData& data) const; // fn_10_F840
  bool Arrived(CStateManager& mgr, const CTriggerData& data) const; // fn_10_62B4
  bool AnyIncomingMissiles(CStateManager& mgr, const CTriggerData& data) const; // fn_10_1BB08
  bool BreakFreezeBeam(CStateManager& mgr, const CTriggerData& data) const; // fn_10_16B48
  bool BreakDash(CStateManager& mgr, const CTriggerData& data) const; // fn_10_149B4
  bool BreakDiveAttack(CStateManager& mgr, const CTriggerData& data) const; // fn_10_11C54
  bool BreakNormalMissile(CStateManager& mgr, const CTriggerData& data) const; // fn_10_16B5C
  bool BreakPause(CStateManager& mgr, const CTriggerData& data) const; // fn_10_106F8
  bool BreakPhazonAttack(CStateManager& mgr, const CTriggerData& data) const; // fn_10_4ABC
  bool BreakSuperMissile(CStateManager& mgr, const CTriggerData& data) const; // fn_10_16B54
  bool CanMeleeAttack(CStateManager& mgr, const CTriggerData& data) const; // fn_10_10634
  bool CanMissileDeflect(CStateManager& mgr, const CTriggerData& data) const; // fn_10_14284
  bool DiveAfterBoost(CStateManager& mgr, const CTriggerData& data) const; // fn_10_95A8
  bool DoneRolling(CStateManager& mgr, const CTriggerData& data) const; // fn_10_9314
  bool DoneTurning(CStateManager& mgr, const CTriggerData& data) const; // fn_10_F864
  bool FacingPlayer(CStateManager& mgr, const CTriggerData& data) const; // fn_10_142CC
  bool HitDuringEnrage(CStateManager& mgr, const CTriggerData& data) const; // fn_10_7B68
  bool OutOfMeleeRange(CStateManager& mgr, const CTriggerData& data) const; // fn_10_17CB8
  bool PickedBoostBall(CStateManager& mgr, const CTriggerData& data) const; // fn_10_116F8
  bool PickedFreezeBeam(CStateManager& mgr, const CTriggerData& data) const; // fn_10_11760
  bool PickedDiveAttack(CStateManager& mgr, const CTriggerData& data) const; // fn_10_1174C
  bool PickedMelee(CStateManager& mgr, const CTriggerData& data) const; // fn_10_117D8
  bool PickedMeleeDash(CStateManager& mgr, const CTriggerData& data) const; // fn_10_11710
  bool PickedMissileJump(CStateManager& mgr, const CTriggerData& data) const; // fn_10_1178C
  bool PickedMovingSShot(CStateManager& mgr, const CTriggerData& data) const; // fn_10_11724
  bool PickedNormalMissile(CStateManager& mgr, const CTriggerData& data) const; // fn_10_117A0
  bool PickedPause(CStateManager& mgr, const CTriggerData& data) const; // fn_10_11800
  bool PickedPhazonAttack(CStateManager& mgr, const CTriggerData& data) const; // fn_10_8680
  bool PickedPhazonEnrage(CStateManager& mgr, const CTriggerData& data) const; // fn_10_8114
  bool PickedScatterShot(CStateManager& mgr, const CTriggerData& data) const; // fn_10_11738
  bool PickedSuperMissile(CStateManager& mgr, const CTriggerData& data) const; // fn_10_11778
  bool PickedSweepBeam(CStateManager& mgr, const CTriggerData& data) const; // fn_10_CDA0
  bool PickedTaunt(CStateManager& mgr, const CTriggerData& data) const; // fn_10_117EC
  bool PlayerIsMorphball(CStateManager& mgr, const CTriggerData& data) const; // fn_10_112E8
  bool PlayerTooClose(CStateManager& mgr, const CTriggerData& data) const; // fn_10_10658
  bool ReachedPissedOff(CStateManager& mgr, const CTriggerData& data) const; // fn_10_FAF4
  bool ReadyForMissiles(CStateManager& mgr, const CTriggerData& data) const; // fn_10_6A40
  bool ShouldEnragedMelee(CStateManager& mgr, const CTriggerData& data) const; // fn_10_6B78
  bool ShouldFadeIn(CStateManager& mgr, const CTriggerData& data) const; // fn_10_BB78
  bool ShouldFadeOut(CStateManager& mgr, const CTriggerData& data) const; // fn_10_BBE4
  bool ShouldGetPissedOff(CStateManager& mgr, const CTriggerData& data) const; // fn_10_FD30
  bool ShouldLeaveSphere(CStateManager& mgr, const CTriggerData& data) const; // fn_10_6A60
  bool ShouldReact(CStateManager& mgr, const CTriggerData& data) const; // fn_10_11158
  bool ShouldRiseAgain(CStateManager& mgr, const CTriggerData& data) const; // fn_10_6AE8
  bool SkipPhazonAttack(CStateManager& mgr, const CTriggerData& data) const; // fn_10_110E0
  bool StartsInTheAir(CStateManager& mgr, const CTriggerData& data) const; // fn_10_F924
  bool TooMuchDamage(CStateManager& mgr, const CTriggerData& data) const; // fn_10_10698
  bool WallCollision(CStateManager& mgr, const CTriggerData& data) const; // fn_10_F800
  bool WasHeavyHit(CStateManager& mgr, const CTriggerData& data) const; // fn_10_112AC

  // The 43 states (lbl_10_data_964's order). Start, Patrol and Dead hide CPatterned's own.
  void BoostBallFire(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_9724
  void BoostBallInto(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_B164
  void BoostBallRoll(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_A3B0
  void FreezeBeam(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_16CDC
  void Dash(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_150DC
  void DiveAttack(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_11FA4
  void DiveFromBoost(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_9424
  void EnrageHitReact(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_7A14
  void EnragedTravel(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_6E38
  void FadeIn(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_BD40
  void FadeOut(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_BC54
  void FallFromTheAir(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_F86C
  void GetUp(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_18EAC
  void GoToPhazonAttack(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_652C
  void GoToPhazonEnrage(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_6460
  void HeavyHit(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_10C9C
  void Interrupt(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_10FF4
  void MeleeAttack(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_17BC0
  void MeleeDash(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_17ABC
  void MissileDeflect(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_178E4
  void MissileJump(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_177BC
  void MovingScatterShot(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_11AD8
  void NormalMissileAttack(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_17448
  void Null(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_14448
  void Dead(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_143A8
  void Patrol(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_1142C
  void Pause(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_10710
  void PhazonAttack(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_81D4
  void PhazonEnraged(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_7BC0
  void PhazonFall(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_6BA0
  void PhazonMissiles(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_67B8
  void PhazonRise(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_7054
  void PhazonSuck(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_4AC8
  void PissedOff(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_F930
  void ScatterShot(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_175AC
  void SideDash(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_153EC
  void Start(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_F920
  void SuperMissileAttack(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_16ECC
  void SweepBeam(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_CA0C
  void Taunt(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_11544
  void TravelToCoverPoints(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_1DF74
  void TravelToPissedOff(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_FB8C
  void TurnToFacePlayer(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_FE18
#endif
  // CEntity
  CEntity* TypesMatch(int typeId) const override;

private:
#ifdef TARGET_PC
  // fn_10_142F4: is the player within `angle` of the actor's facing axis? The direction to the
  // player and the forward axis (the guest's 0x28/0x38/0x48) are flattened into the XY plane,
  // normalized and compared (the fcmpo at 0x00014384).
  bool IsPlayerInFront(CStateManager& mgr, float angle) const;

  // The attack picker's weights: a count and one weight per attack bit (bit i is the attack id
  // 1 << i, 24 of them - the loops' `cmpwi 0x18`). The pickers build it on the stack (the count at
  // sp+0xC, the weights from sp+0x10) and copy it to the guest's 0xC54/0xC58.
  struct SAttackWeights {
    int count;
    int weights[24];
  };

  // The pickers' leaves (fn_10_1AA30, 1A980, 1A8F8, 1AA5C, 10BBC, 1A6C4, 17D50, 11308, 17CEC,
  // 1AA6C), the per-attack collectors (fn_10_1A5BC, 1A554, 1A278, 1A440, 1A34C, 1A1D4) and the
  // weighted pick (fn_10_19918).
  static void SetAttackBit(uint* mask, uchar set, uint bit);
  static void SetAttackBits(uint* mask, uchar set, uint a, uint b, uint c, uint d);
  static void SetAttackBitsExcept(uint* mask, uchar set, uint a, uint b, uint c, uint d);
  static bool IsAttackTwo(uint bit);
  static int GetAttackBitIndex(uint bit);
  static float GetAttackWeight(const SLdrDSStageInfo& stage, int attack);
  bool IsAttackEnabled(CStateManager& mgr, int attack) const;
  static bool IsPlayerMorphed(CStateManager& mgr);
  float GetDistanceToPlayer(CStateManager& mgr) const;
  int GetPlayerRangeBand(CStateManager& mgr) const;
  void CollectStageWeights(CStateManager& mgr, SAttackWeights& weights, uint* mask) const;
  void CollectFadeAttacks(CStateManager& mgr, SAttackWeights& weights, uint* mask) const;
  void CollectMorphballAttacks(CStateManager& mgr, SAttackWeights& weights, uint* mask) const;
  void CollectRangeAttacks(CStateManager& mgr, SAttackWeights& weights, uint* mask) const;
  void CollectHistoryPenalties(CStateManager& mgr, SAttackWeights& weights, uint* mask) const;
  void CollectFacingAttacks(CStateManager& mgr, SAttackWeights& weights, uint* mask) const;
  int PickWeightedAttack(CStateManager& mgr, SAttackWeights& weights, uint mask) const;

  // fn_10_1905C: three flat rays at the player (heights 1, 2.5, 4) from up to 8 units short of
  // it: 0 when none hits a platform (or the player is straight above), 1 when the nearest hit is
  // short of the player, 2 when it is beyond.
  int ClassifyLineToPlayer(CStateManager& mgr) const;
  // fn_10_19A1C: the collector for the repulsion point and the line to the player.
  void CollectLineAttacks(CStateManager& mgr, SAttackWeights& weights, uint* mask) const;
  // fn_10_14648: can the actor walk the straight line to the player on the path mesh?
  bool CanWalkToPlayer(CStateManager& mgr) const;
  // fn_10_164C0 / 16418: the line from the gun locator (plus an offset) to a target / to the
  // player's aim position, 5 up, clear of static solids.
  bool IsGunLineClear(CStateManager& mgr, const CVector3f& offset, const CVector3f& target) const;
  bool IsPlayerAimLineClear(CStateManager& mgr) const;
  // fn_10_166AC / 16600: one / all eight ring rays from the actor to dest, radius out, clear.
  bool IsRingRayClear(CStateManager& mgr, const CVector3f& dest, int i, float reach,
                      float radius) const;
  bool IsRingPathClear(CStateManager& mgr, const CVector3f& dest, float reach, float radius) const;
  // fn_10_12B38: the dive's landing point.
  CVector3f GetDiveTarget(CStateManager& mgr) const;
  // fn_10_141BC: the player's distance within [min, max] times the animation's reach.
  bool IsPlayerInAnimReach(CStateManager& mgr, const CPASAnimParmData& parms, float minScale,
                           float maxScale) const;
  // fn_10_13EE0 / 13E2C: the break-dodge step's distance / the dash slot's dodge offset.
  float GetBreakDodgeDistance(int dir) const;
  CVector3f GetDodgeStepOffset(int slot) const;
  // fn_10_16B1C / 1444C / 145E4: on the path; the line to point on the mesh within 5 of height;
  // the same from the actor's own height.
  bool IsOnPath(const CVector3f& point) const;
  bool IsLineOnMesh(const CVector3f& point, float height) const;
  bool CanWalkToPoint(const CVector3f& point) const;
  // fn_10_19B64: the collector for the attacks' own reach checks (it also sets mScatterSide).
  void CollectReachAttacks(CStateManager& mgr, SAttackWeights& weights, uint* mask);
  // fn_10_197D8 / fn_10_1969C: the visible and the faded pickers - the collectors' weights and
  // mask go to mAttackWeights/mAttackMask and the weighted pick to mWeightedPick, which they
  // answer.
  int PickVisibleAttack(CStateManager& mgr);
  int PickFadedAttack(CStateManager& mgr);
  // fn_10_1954C: picks the next attack into mPickedAttack when none runs and its time has come.
  void UpdateAttackPick(CStateManager& mgr);
  // fn_10_1B478: the attack bookkeeping every attack state runs first - on Activate the attack
  // becomes mCurrentAttack and the next pick's delay is drawn, on Deactivate it is pushed onto
  // mAttackHistory and the delay becomes an absolute time.
  void UpdateCurrentAttack(uint attack, CStateManager& mgr, EStateMsg msg);
  // fn_10_1B5E4: mAttackHistory's erase (the rstl::reserved_vector<uint, 3> at 0x994).
  uint* EraseFinishedAttack(uint* pos);
  // fn_10_10520: the orbit and aim positions at the attack's start (0xD28/0xD34).
  void UpdateAttackAimPoints(CStateManager& mgr, EStateMsg msg);
  // fn_10_1AEC0: CGameCollision::CollisionFailsafe over the actor's motion volume.
  void RunCollisionFailsafe(CStateManager& mgr);
  // fn_10_16B64 / fn_10_16B8C: turn toward the player / a point by at most dt * rate radians.
  void TurnTowardPlayer(CStateManager& mgr, float dt, float rate);
  void TurnTowardPoint(float dt, const CVector3f& point, float rate);
  // fn_10_11974: the scatter shot's aim drift and fire timer while firing (0xBAD).
  void UpdateScatterShotFire(CStateManager& mgr, float dt);
  // fn_10_11814: the scatter shot's start (the aim line, the firing byte, the timer).
  void StartScatterShotFire(CStateManager& mgr, uchar side);
  // fn_10_13230: the jittered point along the aim line the next shot aims at.
  CVector3f GetScatterShotAimPoint(CStateManager& mgr) const;
  // fn_10_12F60: fires one scatter shot projectile (a CEnergyProjectile stands in for the
  // module's CScattershotProjectile).
  void FireScatterShotProjectile(CStateManager& mgr);
  // fn_10_21DF8: the step command driven by the animation state.
  void DoStepCmd(EStateMsg msg, pas::EAnimationState state, const CBCStepCmd& cmd);
  // fn_10_21F14: the projectile-attack command driven by the animation state.
  void DoProjectileAttackCmd(EStateMsg msg, pas::EAnimationState state,
                             const CBCProjectileAttackCmd& cmd);
  // fn_10_12C2C: the dash's step command toward the dash slot (mDash.mSlot).
  void DoDashStepCmd(EStateMsg msg);
  // fn_10_12CE0: the dash step - the attack bookkeeping with id 0x8000, the collision failsafe and
  // the dash's step command; Activate remembers the slot in mDash.mLastSlot.
  void UpdateDashStep(CStateManager& mgr, EStateMsg msg);
  // fn_10_14A00: the blast when the dash is stuck - radius damage to the solid / platform actors
  // around the actor.
  void ApplyDashStuckBlast(CStateManager& mgr);
  // fn_10_14FE0: a new dash for the double dash - the best cover point, the dash pick without a
  // target and the stuck count.
  void PickNextDash(CStateManager& mgr);
  // fn_10_14C50: the double dash's second step (a new pick, the step command, the animation).
  void StartNextDashStep(CStateManager& mgr);
  // fn_10_14E5C: the step animation of the dash slot, set and advanced by 0.3 s at once.
  void PlayDashStepAnimation(CStateManager& mgr);
  // fn_10_14D14: the double dash.
  void UpdateDoubleDash(CStateManager& mgr, EStateMsg msg);
  // fn_10_154FC: weighs the eight dash slots (weights[8], in/out) and picks one into mDash;
  // returns its weight. target, when given, is where the dash should end near.
  float PickDash(CStateManager& mgr, float* weights, const CVector3f* target);
  // fn_10_16048: how free the path to dest is: 1.0 clear, 0.49 off per blocked side ray, 0 when
  // the centre ray is blocked.
  float GetDashPathClearance(CStateManager& mgr, const CVector3f& dest,
                             const rstl::reserved_vector< TUniqueId, 1024 >& nearList) const;

  // fn_10_17E14: the fight script's object - the one with the uid at the guest's 0x974 - and
  // module 11's accessor on it (fn_11_B70, which returns the object's 0x24).
  const CDarkSamusBattleStage* GetBattleStage(CStateManager& mgr) const;
  // fn_10_17E90 / 194AC / B86C / 193A0: the health in percent; the next stage (its kSS_Connect
  // link); the AI hint chains the actor wakes; the stage advance and the 0x97C flags.
  float GetHealthPercent() const;
  TUniqueId GetNextBattleStage(CStateManager& mgr) const;
  void ActivateHintChains(CStateManager& mgr);
  void AdvanceBattleStage(CStateManager& mgr);
  // fn_10_B6C4 / B458 / BA38: the boost ball's hints. The nearest active type-0x11 hint the
  // actor links with kSS_InternalState6 whose radius holds the actor; the first active hint a
  // hint links with kSS_Arrived; whether a type-0x11 hint chain from a hint reaches an inactive
  // type-0x11 hint within 15 steps.
  const CScriptAIHint* FindBoostBallHint(CStateManager& mgr) const;
  static CScriptAIHint* FindArrivedHint(CStateManager& mgr, TUniqueId uid);
  bool BoostChainHasInactiveHint(CStateManager& mgr, TUniqueId uid) const;
  // fn_10_9920 / A06C: the boost ball actor (the 0xE10 sub-object's uid, 0xE38); the ball's
  // transform under the actor and the roll emitter's position (BoostBallInto's Update).
  CActor* GetBoostBallActor(CStateManager& mgr) const;
  const CActor* GetBoostBallActor(const CStateManager& mgr) const; // fn_10_996C
  void UpdateBoostBallAtActor(CStateManager& mgr);
  // fn_10_C85C / C798: the sweep beam's effect (0xD78) made at the kUE_BeginAction event with
  // emission on and 0xDC8 bit 0x40 set; released with the bit cleared. fn_10_C2B0 / C470: the
  // beam's aim each Update (the static ray from the gun and its end point, the wall's collision
  // response and decal, the direction, length and effect). fn_10_C104 / BE18: the dynamic rays
  // swept from the last direction to this one and the damage the first hit takes.
  void StartSweepBeam();
  void StopSweepBeam();
  void UpdateSweepBeamAim(CStateManager& mgr, float dt);
  void UpdateSweepBeamEnd(CStateManager& mgr);
  void SweepBeamDamage(CStateManager& mgr);
  void SweepBeamRay(CStateManager& mgr, const CVector3f& dir, float length);

  // fn_10_1D794: the still timer (the guest's 0x964) and the position it measures from (the
  // guest's 0x958). kStateMsg_Activate clears the timer and moves the reference position to the
  // actor's own position.
  void UpdateStillTimer(EStateMsg msg, float dt);

  // fn_10_8764: a melee attack command run over a state's messages - Activate delivers it,
  // Update re-delivers it while the animation state allows, Deactivate ends the state.
  void RunMeleeAttackCmd(EStateMsg msg, pas::EAnimationState state, const CBCMeleeAttackCmd& cmd);

  // The dive (fn_10_11FA4). fn_10_12960: moves *target up to 8 back toward the actor until the
  // ring path to it is clear. fn_10_12B00: the body's target vector to the dive target.
  // fn_10_1285C + fn_10_12764: the jump command toward it.
  bool FindDiveLanding(CStateManager& mgr, CVector3f* target) const;
  void SetDiveTargetVector();
  void DoDiveJumpCmd();
  // fn_10_11C60 / 1CDD0 / 11EB8 / 11DDC: the flight push; the bounce off a wall; the stuck
  // timer; the landing's attack message.
  void UpdateDiveFlight(float scale);
  void BounceDive(CStateManager& mgr, const CVector3f& normal);
  void UpdateDiveStuck(CStateManager& mgr, float dt);
  void SendDiveAttack(CStateManager& mgr);
  // fn_10_12E90 / 12EF8: the "DiveBubble" effect off / on.
  void StopDiveBubble(CStateManager& mgr);
  void StartDiveBubble(CStateManager& mgr);
  // fn_10_1C99C: CollidedWith's dive arm (a wall bounces, the floor or the player lands).
  void CheckDiveCollision(CStateManager& mgr, CActor* collider, const CCollisionInfoList& list);
  // CollidedWith's other helpers (PortCDarkSamus.cpp): fn_10_1D0A0 the platform contact,
  // fn_10_1CF84 the wall / ceiling hit time, fn_10_A734 the boost ball's wall flag, fn_10_1D194
  // the platform crush.
  void RecordPlatformContact(const CCollisionInfoList& list);
  void RecordWallHit(const TUniqueId& id, const CCollisionInfoList& list);
  void CheckBoostBallWallHit(CStateManager& mgr, const CCollisionInfoList& list);
  void CrushPlatform(CStateManager& mgr, const TUniqueId& id, const CCollisionInfoList& list);
  // fn_10_10484: the phazon volley's bob added to an orbit / aim position's z.
  void AddPhazonBob(CVector3f& pos) const;
  // fn_10_9644: the actor turned around (its facing negated).
  void TurnAround();
  // fn_10_9AC4: the boost ball actor deleted.
  void DeleteBoostBall(CStateManager& mgr);
  // fn_10_1B658 / 138A4: the projectile reflect and the dive's hit on the player (stand-ins).
  void ReflectProjectiles(CStateManager& mgr);
  // fn_10_1B9B0: whether the projectile may be sent back (type -1 = any weapon type).
  bool CanReflectProjectile(CStateManager& mgr, const CEnergyProjectile* proj, int type) const;
  // fn_10_F298 / F210: the reflect chance roll and the cache's erase.
  float RollReflectChance(CStateManager& mgr, TUniqueId uid) const;
  void EraseReflectChance(uint index) const;
  // fn_10_F100: the cache's other roll, the one the hit arm (fn_10_1EE4C) uses.
  float RollReflectChanceForHit(CStateManager& mgr, TUniqueId uid) const;
  // fn_10_1EE4C: the hit's damage may be sent back.
  bool CanReflectHit(CStateManager& mgr, const CDamageInfo& damage, TUniqueId uid) const;
  void HitPlayerWithDive(CStateManager& mgr);
  // fn_10_1382C: the player's orbit target cleared.
  void ClearPlayerOrbitTarget(CStateManager& mgr);
  // fn_10_13750: the player pushed away from the actor (flat), up by the vertical impulse.
  void PushPlayer(CStateManager& mgr, float horizontal, float vertical);
  // fn_10_1367C: the impulse (times the player's mass) on the stopped player, with a rumble.
  void KnockPlayer(CStateManager& mgr, const CVector3f& impulse);
  // fn_10_18628: the three floats at 0xE04 (the elapsed time, a, b).
  void SetGuestE04(float a, float b);
  // fn_10_18FCC: vertical movement off, on the ground as given, kMT_GroundCollider back.
  void SetGrounded(CStateManager& mgr, bool onGround);
  void LeaveGround(CStateManager& mgr); // fn_10_19014
  // fn_10_1707C: the side dodge away from the player's line of fire.
  void UpdateDodge(CStateManager& mgr, float dt, float impulse, float time);

  // fn_10_1E178: the damage the actor has taken (the guest's 0xCC0). The health that is gone is
  // only added to the damage on a message other than kStateMsg_Activate.
  void AccumulateDamage(EStateMsg msg, float dt);

  // fn_10_1C778: the destination of the path find (the guest's 0x8B4/0x8B8/0x8BC) and the path
  // find itself.
  const CScriptAIHint* FindAreaHint(CStateManager& mgr, int type) const; // fn_10_FC64
  void UpdateShieldFlag(EStateMsg msg);                                   // fn_10_105F4
  void UpdateGroundCollision(CStateManager& mgr, EStateMsg msg);          // fn_10_18F90
  void WatchStageChange(CStateManager& mgr);                              // fn_10_FD4C
  void SetPathFindDestination(CStateManager& mgr, const CVector3f& destination, float dt);

  // fn_10_1E26C: the uid of the last attacker (the guest's 0xC24) and the two uids before it
  // (the count at 0xC28, the uids at 0xC2C).
  void PushAttacker(TUniqueId uid);

  // fn_10_1E2E8: the oldest uid is dropped and the rest move one down.
  void EraseFirstAttacker();

  // fn_10_1E360: the player's aim position the cover points are scored against.
  CVector3f GetPlayerAimPosition(CStateManager& mgr) const;

  // fn_10_1E3C0: the uid of the best-scoring active cover point in the actor's area, or
  // kInvalidUniqueId.
  TUniqueId FindBestCoverPoint(CStateManager& mgr) const;

  // fn_10_1E4C4: the score of a cover point against `target` (recent attackers, the attack
  // ranges and the angles to the actor and its facing).
  float ScoreCoverPoint(const CScriptCoverPoint& cover, const CVector3f& target) const;

  // fn_10_1E740: should the actor forget the uid of the attacker that hit it last (the guest's
  // 0xC24)? True when the path to the attacker is shagged, when the path find is over, when the
  // attacker has no cover point, or when the attacker's cover point is within 2 units.
  bool ShouldForgetLastAttacker(CStateManager& mgr) const;

  // fn_10_1E864: the path search's area (the path area of the actor's area).
  void SetPathSearchArea(CStateManager& mgr);

  // fn_10_1D9A4: the point the actor is pushed away from - an active repulsor of the actor's
  // connections in the actor's area that holds the actor inside its radius (`strength`, when not
  // null, gets 1 - distance / radius), else the centroid of an obstructed path find region the
  // actor stands in, else the last contact point (mContactPoint) for 0.3 seconds after the
  // contact, else the zero vector.
  CVector3f GetRepulsionPoint(CStateManager& mgr, float* strength) const;

  // fn_10_1DD14: the separation - the actor steers away from GetRepulsionPoint's point
  // (mSeparationDir is the direction it steers along, or zero).
  void Separate(CStateManager& mgr);

  // fn_10_1D860: the additive animation of the state's message - kStateMsg_Activate adds the
  // best kAS_AdditiveReaction animation of the type 5 (its parms built by fn_10_13FFC, the
  // CPASAnimParmData constructor), kStateMsg_Deactivate removes it.
  void UpdateAdditiveAnimation(CStateManager& mgr, EStateMsg msg);

  // fn_10_EFC8: is the actor more than 10 units below its safe position (mSafePos), or inside an
  // active type 0x1C AI hint of its area?
  bool IsInUnsafeSpot(CStateManager& mgr) const;

  // fn_10_128D8: keeps the actor's position as mSafePos the first time it is not unsafe.
  void UpdateSafePosition(CStateManager& mgr);

  // fn_10_11334: the looping glide emitter at the actor (mGlideSfxId, the handle mGlideSfx) -
  // kStateMsg_Activate starts it, kStateMsg_Update starts it when it is not running and moves
  // it to the actor, kStateMsg_Deactivate removes it.
  void UpdateGlideEmitter(EStateMsg msg);

  // Host layout (retail's object is 0x1128, CHECK_SIZEOF below; CPatterned is 0x7c0). The guest
  // offsets are the REL's, read from the constructor fn_10_1FA14 and the module's one-line
  // accessors; the host layout is its own.
  CPathFindSearch mPathFindSearch; // guest 0x7C0 (0xEC): fn_10_CE4C returns this+0x7C0
  CDarkSamusData mData;            // host only: the loader's 60 data properties (see above)
  uchar mGuestCD0;                 // guest 0xCD0, the first byte of the sub-object fn_10_E560
                                   // builds; fn_10_F84C reads it to pick the collision response
  uchar mFlags90C;                 // guest 0x90C, the flag byte: bit 6 (0x40) is the "starts in
                                   // the air" flag (fn_10_F924) and bit 7 (0x80) the "alerted" one
                                   // (fn_10_F840); the constructor's rlwimi chain (0x0001FBF8..)
                                   // clears bit 7 and copies startsInTheAir into bit 6 (0x0001FC08)
  uchar mTurnDone;                 // guest 0xD4C, the "done turning" flag: fn_10_FE18 sets it and
                                   // fn_10_F864 (the DoneTurning trigger) reads it
  uchar mPissedOff;                // guest 0xD58, the flag the PissedOff state clears (fn_10_F930)
                                   // and the fight script sets; fn_10_FD30 (ShouldGetPissedOff)
                                   // reads it
  uchar mFadeOut;                  // guest 0xDEC, the fade flag: fn_10_117A0 (PickedNormalMissile)
                                   // and fn_10_BBE4 (ShouldFadeOut) read it
  TUniqueId mBattleStageUid;        // guest 0x974, the uid of the fight script's object: the
                                   // helper fn_10_17DE0 looks it up in the state manager and
                                   // casts it to CScriptDarkSamusBattleStage. The constructor
                                   // writes kInvalidUniqueId to it (the lhz/sth at 0x0001FCC0);
                                   // the 'XALD' arm of AcceptScriptMsg links it (0x0001EBB8) and
                                   // fn_10_193A0 moves it to the next stage (0x00019410)
  uint mStageCount;                 // guest 0x978, the stages the actor has had: the 'XALD' arm
                                   // writes 1 (0x0001EBC4), fn_10_193A0 adds 1 (0x0001941C); the
                                   // constructor writes 0 (0x0001FCC8)
  // guest 0x97C, two flags fn_10_193A0 copies from the stage info (0x00019470/0x0001948C); the
  // constructor writes 0 / 1 (0x0001FCD0/0x0001FCDC). The guest packs both into the byte
  // 0x97C (the rlwimi at 0x0001946C sets bit 0x80, the one at 0x00019488 bit 0x40); the host
  // keeps two bools, so the comment names the byte first and the bit after it.
  bool mUseAltScan;                 // 0x97C, bit 0x80: usesAlternateScannableInfo; fn_10_F40C reads it
  bool mCanDie;                     // 0x97C, bit 0x40: the info's +0x32 not 1; fn_10_F488 keeps the
                                    // health at 1.0f while it is clear (0x0000F4F4)
  float mPhazonHealthMark;          // guest 0x1054, the health the phazon phase started at
                                    // (fn_10_6BA0 at 0x00006D20); the constructor writes 0.0f
                                    // (0x00020214)
  bool mPhazonPhase;                // guest 0x1060 bit 0x80: fn_10_6BA0 sets it (0x00006C90),
                                    // fn_10_7054 clears it (0x00007154); the constructor clears it
                                    // (0x00020224)
  float mGuestD24;                  // guest 0xD24, the 0xCD0 sub-object's +0x54 (fn_10_E560 writes
                                    // 0.0f at 0x0000E5DC); fn_10_F654 ramps it in [0, 1]
  CDamageVulnerability mGuestCD4;   // guest 0xCD4, the 0xCD0 sub-object's vulnerability: fn_10_E560
                                    // copies CDamageVulnerability::ReflectVulnerabilty() into it
                                    // (0x0000E580/0x0000E58C); fn_10_100DC / fn_10_FF20 return it
                                    // while mGuestCD0 is 1
  // guest 0xFD0 (the 0xF80 sub-object's +0x50, built from ReflectVulnerabilty() by fn_10_20F50 at
  // 0x00020FB8/0x00020FC4), the phazon-enrage vulnerability fn_10_100DC rebuilds from the
  // patterned one on every call; mutable because that call is the const GetDamageVulnerability
  mutable CDamageVulnerability mEnrageVulnerability;
  // guest 0x92C, the alternate 'SCAN' (the data's alternateScannableInfo, locked by the
  // constructor at 0x0002048C..0x00020538 when it is valid); fn_10_F40C answers it
  rstl::single_ptr< TLockedToken< CScannableObjectInfo > > mAltScanInfo;
  TUniqueId mSeenStageUid;          // guest 0xD50, the stage fn_10_FD4C last saw: the 'XALD' arm
                                   // writes the first stage (0x0001EBC0), fn_10_FD4C the current
                                   // one (0x0000FDD4/0x0000FDF4); the constructor writes
                                   // kInvalidUniqueId (0x00020018)
  uint mSeenStageCount;             // guest 0xD54, the stage count fn_10_FD4C last saw
                                   // (0x0000FDDC/0x0000FDFC); the constructor writes 1 (0x0002001C)
  uchar mStageChanged;              // guest 0xD59, a stage change with no pissed-off reaction
                                   // (fn_10_FD4C at 0x0000FDC8/0x0000FDEC); the PissedOff state
                                   // clears it (0x0000F96C); fn_10_F710 and fn_10_1BEFC read it
  CVector3f mGuestD18;              // guest 0xD18, the 0xCD0 sub-object's +0x48: fn_10_105F4
                                   // copies lbl_10_bss_128 (0x0001060C); fn_10_F654 tests it
  TUniqueId mLastAttackerUid;       // guest 0xC24, the uid of the attacker that hit the actor
                                   // last: fn_10_1DF74 clears it and fn_10_1E740 sets it (neither
                                   // is in the port yet); the constructor writes
                                   // kInvalidUniqueId to it (0x0001FF08)
  uint mAttackerCount;              // guest 0xC28, the number of uids in mAttackerUids; the
                                   // constructor writes 0 to it (0x0001FF14)
  TUniqueId mAttackerUids[2];       // guest 0xC2C, the uids of the attackers that hit the actor
                                   // (at most two): fn_10_1E26C pushes the last attacker's uid
                                   // here and drops the oldest when there are two
  int mPickedAttack;               // guest 0x984, the word the fight script writes and the
                                   // Picked* triggers compare: the picked attack's id (1 melee,
                                   // 4 melee dash, 8 scatter shot, 0x10 moving scatter shot,
                                   // 0x20 dive attack, 0x40 normal missile, 0x80 missile jump,
                                   // 0x100 super missile, 0x200 taunt, 0x400 pause, 0x10000 sweep
                                   // beam, 0x20000 boost ball, 0x100000 freeze beam, 0x200000 the
                                   // fade variant, 0x400000 phazon attack, 0x800000 phazon
                                   // enrage). The constructor writes 0xFFFF to it (0x0001FCE8),
                                   // which is "no attack picked"
  uint mCurrentAttack;             // guest 0x980, the word the module writes when an attack starts
                                   // (fn_10_1B478) and reads in
                                   // ShouldReact and TooMuchDamage: the id of the attack the actor
                                   // is performing (the same id space as mPickedAttack; 0xFFFF =
                                   // none). The constructor writes 0xFFFF to it (0x0001FCE4)
  uint mAttackHistory[3];          // guest 0x998, the ids of the attacks the actor has finished:
                                   // fn_10_1B478 pushes the current attack here and trims the
                                   // oldest entry when there are three
  uint mAttackHistoryCount;        // guest 0x994, the number of ids in mAttackHistory; the
                                   // constructor writes 0 to it (0x0001FCF8)
  float mAccumulatedDamage;        // guest 0xCC0, the damage the actor has taken: fn_10_1E178 adds
                                   // the health that is gone and decays it; the constructor writes
                                   // 0.0f to it (0x0001FF58)
  float mLastHealth;               // guest 0xCC4, the health at the last damage check: fn_10_1E178
                                   // compares it with the health info's current health; the
                                   // constructor writes lbl_10_rodata_3EC = -1.0f to it (0x0001FF5C)
  float mDamageDecay;              // guest 0xCCC, the rate the accumulated damage decays at:
                                   // fn_10_1E178 subtracts it times dt; the constructor writes
                                   // lbl_10_rodata_3E8 = 1.0f to it (0x0001FF64)
  float mDamageThreshold;          // guest 0xCC8, the threshold TooMuchDamage compares the
                                   // accumulated damage with; the constructor writes its 8th float
                                   // argument to it (0x0001FF60), the loader's
                                   // damageInterruptThreshold (the property id 0x8f70d3f2)
  float mStillTime;                // guest 0x964, how long the actor has stood still: fn_10_1D794
                                   // adds dt to it while the actor stays within 0.6 of
                                   // mStillPosition; the constructor writes 0.0f to it (0x0001FCB0)
  uchar mDashStuckCount;           // guest 0x968, the dashes in a row that started while the
                                   // actor stood still (mStillTime above lbl_10_rodata_42C =
                                   // 1.2f): fn_10_150DC / fn_10_14FE0 count it, two or more fire
                                   // the stuck blast (fn_10_14A00); the constructor writes 0 to it
                                   // (0x0001FCB4)
  CVector3f mStillPosition;        // guest 0x958/0x95C/0x960, the position the still timer
                                   // measures from: fn_10_1D794 moves it to the actor's position
                                   // when the actor walks away; the constructor writes the zero
                                   // vector to it (0x0001FC64..0x0001FC74)
  float mLastWallHitTime;          // guest 0xC44 (inside the 0xB50 sub-object), the time of the
                                   // last wall collision: fn_10_1CF84 and fn_10_1DF74 write it;
                                   // the constructor writes 0.0f to it (0x0001FF38)
  float mElapsedTime;              // guest 0x8B0, the time since the actor was created: the Think
                                   // (fn_10_1B1FC) adds dt to it (0x0001B2DC); the constructor
                                   // writes 0.0f to it (0x0001FB88)
  CVector3f mPathFindDestination;  // guest 0x8B4/0x8B8/0x8BC, the destination of the path find:
                                   // fn_10_1C778 writes it (0x0001C7A4..0x0001C7BC); the
                                   // constructor writes the zero vector to it (0x0001FB94..)
  int mAdditiveAnim;               // guest 0x970, the additive animation fn_10_1D860 added (-1 =
                                   // none); the constructor writes -1 to it (0x0001FCBC)
  ushort mGlideSfxId;              // guest 0xC48, the glide sound (the loader's glideSound, the
                                   // property 0x1f468967: its case at 0x0000D334 stores it at
                                   // the loader's 0x924, which becomes the constructor's first
                                   // stack argument at 0x0000DB98; -1 is
                                   // kInternalInvalidSfxId, 0x0000DAAC); the constructor's sth
                                   // at 0x0001FF3C
  CSfxHandle mGlideSfx;            // guest 0xC4C, the handle of the glide emitter fn_10_11334
                                   // runs; the constructor writes 0 to it (0x0001FF44)
  float mSavedSpeed;               // guest 0x96C, the speed TravelToCoverPoints (fn_10_1DF74) puts
                                   // back on Deactivate; the constructor writes 0.0f (0x0001FCB8)
  CDarkSamusDash mDash;            // guest 0x9E0..0xA08, built at 0x0001FD04 (fn_10_15348)
  float mGuestC00;                 // guest 0xC00, the float fn_10_1DF74 clears every call; the
                                   // constructor writes 0.0f (0x0001FECC)
  CVector3f mSafePos;              // guest 0x91C/0x920/0x924, the first position fn_10_128D8
                                   // found the actor safe at; the guest constructor leaves it
                                   // unset (the host zeroes it)
  uchar mHasSafePos;               // guest 0x928, set once mSafePos is; the constructor clears it
                                   // (0x0001FC34)
  CVector3f mSeparationDir;        // guest 0x910/0x914/0x918, the direction fn_10_1DD14 steers
                                   // the actor along to get away from a repulsor (zero when it
                                   // does not); the constructor writes the zero vector to it
                                   // (0x0001FC20..)
  float mContactTime;              // guest 0xC34, the elapsed time (mElapsedTime) of the last
                                   // platform contact fn_10_1D0A0 records (0x0001D154); the
                                   // constructor writes 0.0f (0x0001FF1C)
  CVector3f mContactPoint;         // guest 0xC38/0xC3C/0xC40, the point of that contact; the
                                   // constructor writes the zero vector to it (0x0001FF24..)
  float mLastMeleeTime;            // guest 0x9C0, the elapsed time the MeleeAttack state
                                   // fn_10_17BC0 stores (0x00017C9C); fn_10_1A34C reads it
                                   // (0x0001A3E0). The 0x9A4 record's constructor fn_10_EA24
                                   // starts it at lbl_10_rodata_410 = -1000.0f (0x0000EAAC)
  SAttackWeights mAttackWeights;   // guest 0xC54/0xC58, the pickers' last weights; the
                                   // constructor writes the count 0 (0x0001FF4C)
  uint mAttackMask;                // guest 0xCB8, the pickers' last attack mask; 0 (0x0001FF50)
  int mWeightedPick;               // guest 0xCBC, fn_10_19918's last pick; 0xFFFF (0x0001FF54)
  int mPickCount;                  // guest 0x988, the picks fn_10_1954C made (0x00019640); the
                                   // constructor writes 0 (0x0001FCEC)
  float mLastPickTime;             // guest 0x98C, the elapsed time of the last pick (0x00019678);
                                   // lbl_10_rodata_410 = -1000.0f (0x0001FC9C, 0x0001FCF0)
  float mNextActionTime;           // guest 0x990, the earliest time of the next pick: fn_10_1B478
                                   // draws the delay when an attack starts (0x0001B558) and adds
                                   // the elapsed time when it ends (0x0001B5B8); -1000.0f (0x0001FCF4)
  CVector3f mAttackOrbitPos;       // guest 0xD28, CPatterned::GetOrbitPosition at the attack's
                                   // Activate (fn_10_10520, 0x00010564), zero on Deactivate
  CVector3f mAttackAimPos;         // guest 0xD34, CPatterned::GetAimPosition(0.0f) at the same
                                   // point (0x00010588), zero on Deactivate
  // The scatter shot sub-object at 0xB50 (fn_10_215FC builds it, the call at 0x0001FE80):
  CVector3f mScatterLineStart;     // guest 0xB7C, zero (0x00021660); fn_10_11814 sets it
  CVector3f mScatterAim;           // guest 0xB88, zero (0x0002167C); fn_10_11814 sets it
  CVector3f mScatterLineEnd;       // guest 0xB94, zero (0x00021698); fn_10_11814 sets it
  int mScatterShotCount;           // guest 0xBA0, 0 (0x000216A8)
  float mScatterStartTime;         // guest 0xBA4, 0.0f (0x000216AC); fn_10_11814 stores the
                                   // elapsed time 0x8B0 at the start (0x00011858)
  float mScatterTimer;             // guest 0xBA8, 0.0f (0x000216B0)
  uchar mScatterFiring;            // guest 0xBAD, 0 (0x000216B8); fn_10_11814 sets it (0x00011868)
  float mScatterChance;            // guest 0xBDC, the constructor's 6th float (0x000216C4), the
                                   // loader's unknown_0x378285b9 (0x0000D660)
  float mScatterRate;              // guest 0xBE0, the constructor's 7th float (0x000216C8), the
                                   // loader's scatterShotProjectilesPerSecond (0x0000D670)
  TUniqueId mScatterProjectileUid; // guest 0xBE4, kInvalidUniqueId (0x000216D0)
  uchar mScatterSide;              // guest 0xBAC, 0 (0x000216B4); the side MovingScatterShot's
                                   // start passes to fn_10_11814 (0x0001C264)
  uchar mScatterUseSecondInfo;     // guest 0xBAE, 0 (0x000216BC); fn_10_12F60 draws it
                                   // (0x000130CC) and ProjectileInfo reads it (0x0001C718)
  // guest 0xB50 / 0xBB0, the scatter shot's two projectile infos with their valid bytes at 0xB78
  // (fn_10_20C2C clears it at 0x00020C4C) and 0xBD8 (0x000216C0): built from the loader's
  // scatterShotProjectile / scatterShotProjectile2 and the one scatterShotDamage when the asset
  // is valid (`addis r0, rX, 0x1; cmplwi r0, 0xffff` at 0x00020C38 / 0x00021644), then locked
  // (0x00020C78 / 0x00021704).
  rstl::optional_object< CProjectileInfo > mScatterShotInfo;
  rstl::optional_object< CProjectileInfo > mScatterShotInfo2;
  CDarkSamusDive mDive;            // guest 0xAC0..0xB28, fn_10_E784 at 0x0001FE64
  CDarkSamusMeleeContact mMeleeContact; // guest 0x9A4..0x9E0, fn_10_EA24 at 0x0001FCA8
  float mGuestE04;                 // guest 0xE04, the elapsed time at fn_10_18628
  float mGuestE08;                 // guest 0xE08, fn_10_18628's first float
  float mGuestE0C;                 // guest 0xE0C, fn_10_18628's second float
  // The boost ball sub-object at 0xE10 (fn_10_895C, built at 0x000200F8): the host keeps only
  // the members BoostBallInto (fn_10_B164) reads.
  TUniqueId mBoostBallUid;         // guest 0xE38 (+0x28), the ball actor; kInvalidUniqueId
                                   // (0x000089FC)
  ushort mBoostBallRollSfxId;      // guest 0xE68 (+0x58), the roll sound (0x00008A2C; -1 is
                                   // kInternalInvalidSfxId). [probable] the loader's
                                   // boostBallRollSound: fn_10_895C's stack arguments follow
                                   // the data order (the r9 token is the 'SWHC' trail swoosh,
                                   // then the two sounds +0x58 / +0x5A)
  ushort mBoostBallHitPlayerSfxId; // guest 0xE6A (+0x5A), the ball's hit-player sound
                                   // (0x00008A28..0x00008A40; -1 is kInternalInvalidSfxId).
                                   // [probable] the loader's boostBallHitPlayerSound (the data
                                   // order, as above); fn_10_9E00 passes it to the ball (`lhz`)
  CSfxHandle mBoostBallRollSfx;    // guest 0xE6C (+0x5C), the roll emitter; 0 (0x00008A50)
  CVector3f mBoostBallPos = CVector3f::Zero(); // guest 0xE98/0x9C/0xA0, where the boost ball rolls to
                                    // (fn_10_A3B0, 0x0000A4B0..0x0000A4CC); zero (fn_10_895C, +0x88..0x90)
  bool mBoostBallFlag40 : 1;       // 0xebc 0x40 (+0xAC); BoostBallInto clears it
                                   // (`rlwimi r0, r8, 6, 25, 25` at 0x0000B3B4)
  TUniqueId mBoostHintUid;         // guest 0xEC0, the hint the ball rolls to; kInvalidUniqueId
                                   // (0x0002011C)
  float mBoostHintDist;            // guest 0xEC4, the distance to it; lbl_10_rodata_558 =
                                   // 1000.0f (0x0002012C)
  uchar mGuestEC8;                 // guest 0xEC8, 0 (0x00020138)
  CVector3f mGuestECC;             // guest 0xECC, the zero vector (0x00020140..0x00020150)
  float mBoostHintValue;           // guest 0xED8, the hint's value parameter clamped to 0..2,
                                   // lbl_10_rodata_3E8 = 1.0f (0x00020154)
  // The sweep beam sub-object at 0xD74 (fn_10_8DB4, built at 0x00020060): the host keeps only
  // the members SweepBeam (fn_10_CA0C) and its callees read. [probable] fn_10_8DB4's arguments
  // follow the data order (sweepSwoosh, sweepBeamDamage, sweepBeamCollisionEffect, sweepBeamSound,
  // unknown_0x0ef8dc15), as fn_10_895C's do for the boost ball.
  CAssetId mSweepBeamPart;         // guest 0xD74 (+0x00), the beam effect's 'PART' (`lis r5,
                                   // 0x5041` at 0x0000C8A8); r4 (0x00008DF8)
  rstl::auto_ptr< CElementGen > mSweepBeamGen; // guest 0xD78/0xD7C (+0x04/+0x08), null
                                   // (0x00008E08/0x00008E18)
  CDamageInfo mSweepBeamDamageInfo; // guest 0xD80 (+0x0C), r5 (0x00008E28..0x00008E64)
  CVector3f mSweepBeamDir;         // guest 0xD9C (+0x28), gun to end point, normalized; zero
                                   // (0x00008E6C)
  CVector3f mSweepBeamPrevDir;     // guest 0xDA8 (+0x34), the last swept direction; zero
  CVector3f mSweepBeamEnd;         // guest 0xDB4 (+0x40), the end point; zero
  float mSweepBeamLength;          // guest 0xDC0 (+0x4C), lbl_10_rodata_25C = 0.0f (0x00008EB0)
  float mSweepBeamSpeed;           // guest 0xDC4 (+0x50), lbl_10_rodata_264 = 1.0f (0x00008EB4);
                                   // SweepBeam keeps the base's speed 0x3C8 here
  bool mSweepBeamHitPlayer : 1;    // 0xdc8 0x80 (+0x54), false (0x00008EBC)
  bool mSweepBeamOn : 1;           // 0xdc8 0x40, false (0x00008EC8)
  // lbl_10_bss_114: the module's weapon mode, a word the module's init fn_10_CDB8 writes at
  // 0x0000CE30 (0x000A0000: the guest's CWeaponMode keeps the type in the high half, so the type
  // is 10 = kWT_Phazon and the flags are 0). The sweep beam's "any other hit" damage uses it
  // (fn_10_BE18, the read at 0x0000C024 and the store at 0x0000C05C).
  CWeaponMode mSweepBeamWeaponMode;
  TLockedToken< CCollisionResponseData > mSweepBeamResponse; // guest 0xDCC (+0x58), 'CRSC'
                                   // (`lis r4, 0x4352` at 0x00008E10), r6
  ushort mSweepBeamSfxId;          // guest 0xDD8 (+0x64), r7 (0x00008F18)
  CSfxHandle mSweepBeamSfx;        // guest 0xDDC (+0x68), 0 (0x00008F24)
  ushort mSweepBeamEndSfxId;       // guest 0xDE0 (+0x6C), r8 (0x00008F28)
  CSfxHandle mSweepBeamEndSfx;     // guest 0xDE4 (+0x70), 0 (0x00008F2C)
  float mDodgeTime;                // guest 0x1090, the dodge's time left (fn_10_1707C)
  CVector3f mDodgeDir;             // guest 0x1094, the dodge's direction (fn_10_1707C)
  CTransform4f mGunLctrXf;         // guest 0x8CC, identity (0x0001FBC0); PreRender (fn_10_18AB8)
                                   // stores GetLctrTransform("GUN_Particle_LCTR") (0x00018C24)
  CVector3f mRootPos;              // guest 0x8FC/0x900/0x904, the zero vector (0x0001FBDC..);
                                   // PreRender stores GetLctrTransform("Skeleton_Root")'s
                                   // translation (lbl_10_rodata_560 + 0x551, 0x00018C64..)
  rstl::reserved_vector< TUniqueId, 5 > mReflectedUids; // guest 0xBEC (count, 0 at 0x0001FE9C)
                                   // and 0xBF0 (five uids): the projectiles fn_10_1B658 sent back
  float mLastReflectTime;          // guest 0xC08, lbl_10_rodata_410 = -1000.0f (0x0001FEE0);
                                   // fn_10_1B658 stores the elapsed time 0x8B0 (0x0001B8D4)
  float mGuestC0C;                 // guest 0xC0C, the elapsed time of the last hit (the damage
                                   // arm of AcceptScriptMsg, 0x0001EAF8); the constructor writes
                                   // lbl_10_rodata_3FC = 0.0f (0x0001FEE4)
  float mGuestC20;                 // guest 0xC20, the elapsed time of the last heavy hit (the
                                   // HeavyHit state's Deactivate, 0x00010F4C); the constructor
                                   // writes lbl_10_rodata_410 = -1000.0f (0x0001FF04)
  ushort mRicochetSfxId;           // guest 0xC50, the loader's missileRicochetSound (its 0x928;
                                   // -1 is kInternalInvalidSfxId, 0x0000DA94); 0x0001FF48
  // guest 0xA40 / 0xA80 / 0xAB8: the record bytes of the missile attacks and the freeze beam.
  // fn_10_62B4 (the Arrived trigger) reads the shield flag byte 0xCD0 and the float 0xF64 (the
  // time offset the trigger adds to its 6.0f and 20.0f tests - the fsm2 lane's member) and the
  // point 0xEE4/0xEE8 (the helpers branch's mGuestEE4, the path find's fallback destination).
  // fn_10_9314 (the DoneRolling trigger) reads the float 0xEA4 (the roll's time), 0xEA8 (the
  // roll's other time), the point 0xEAC/0xEB0/0xEB4 and the radius 0xEB8, all in the boost
  // ball's record (the 0xE10 sub-object's +0x9C..+0xA8). Nothing in the port writes them yet.
  float mGuestEA4;                 // guest 0xEA4, the roll's time
  uchar mGuestA40;                 // guest 0xA40, the normal missile's record byte; the
                                   // BreakNormalMissile trigger (fn_10_16B5C) reads it
  uchar mGuestA80;                 // guest 0xA80, the super missile's record byte; the
                                   // BreakSuperMissile trigger (fn_10_16B54) reads it
  float mGuest1048;                // guest 0x1048, the EnragedTravel state's point x
  float mGuest104C;                // guest 0x104C, the EnragedTravel state's point y
  float mGuest1050;                // guest 0x1050, the EnragedTravel state's point z
  // fn_10_CDB8 (the module's init, 0x0000CDB8) writes the module's own globals:
  // lbl_10_bss_118 = lbl_10_rodata_320 = 160.0f (the turn speed in degrees a second, the
  // FreezeBeam's turn rate is lbl_10_rodata_454 = 0.017453292 radians a degree times it),
  // lbl_10_bss_11C = lbl_10_rodata_328 = 22.0f, lbl_10_bss_120 = lbl_10_rodata_32C = 0.5f and
  // lbl_10_bss_124 = lbl_10_rodata_324 = 1.9f.
  float mBss118;                   // lbl_10_bss_118, the module's turn speed (160.0f)
  uchar mGuestAB8;                 // guest 0xAB8, the freeze beam's record byte; the
                                   // BreakFreezeBeam trigger (fn_10_16B48) reads its bit 0x80
  float mGuestC30;                 // guest 0xC30, the time of the last pause; the BreakPause
                                   // trigger (fn_10_106F8) compares it with the elapsed time
  // guest 0x10A8: the reflect chance cache. The count (a word) and ten 12-byte entries from
  // 0x10AC: the projectile's uid (a u16 in the guest) and the two rolls fn_10_F100 and fn_10_F298
  // put in. The constructor writes the count 0 (0x00020270); the entries are written at run time
  // by fn_10_F100 (0x0001F1CC) and fn_10_F298 (0x0001F3C8) and erased by fn_10_F210 (0x0001F27C).
  struct SReflectChance {
    SReflectChance() : mUid(kInvalidUniqueId), mChance(0.f), mRoll(0.f) {}
    TUniqueId mUid;   // +0x00 (0x10AC), the projectile's uid
    float mChance;    // +0x04, the roll fn_10_F298's lookup answers and 1B9B0 compares
    float mRoll;      // +0x08, the roll fn_10_F100's lookup answers
  };
  // The cache is mutable because the trigger fn_10_1BB08 fills it through the const
  // CanReflectProjectile, as the retail's trigger does.
  mutable uint mReflectChanceCount;        // guest 0x10A8, the number of entries in the cache
  mutable SReflectChance mReflectChances[10]; // guest 0x10AC, the ten entries
  CVector3f mHitDirection;         // guest 0x8C0/0x8C4/0x8C8, the direction of the last hit
                                    // (the XDMG arm, 0x0001EA98)
  CVector3f mHitDirectionFlat;     // guest 0xC10/0xC14/0xC18, the same direction flattened
                                    // (its z 0.0f) and normalized (0x0001EB04..0x0001EB30)
  // fsm2: the members the second FSM lane's states touch (PortCDarkSamusFsm2.cpp). The names are
  // the guest offsets (the loader does not name them); the initialisers are the constructor's.
  CVector3f GetPhazonWaypoint(CStateManager& mgr, int type);     // fn_10_65CC
  float GetIncomingProjectileDistance(CStateManager& mgr) const; // fn_10_1BC58
  int mGuestA38 = 0;                 // guest 0xA38, the normal missiles fired (fn_10_1BEFC counts)
  int mGuestA3C = 0;                 // guest 0xA3C, the normal missiles to fire (fn_10_17448)
  int mGuestA78 = 0;                 // guest 0xA78, the super missiles fired (fn_10_1BEFC counts)
  int mGuestA7C = 0;                 // guest 0xA7C, the super missiles to fire (fn_10_16ECC)
  uchar mGuestA84 = 0;               // guest 0xA84, the super missile's dodge byte (fn_10_1BEFC)
  int mGuestBE8 = 0xB;               // guest 0xBE8, the taunt type (0x0001FE8C)
  bool mGuestBFC80 = true;           // guest 0xBFC bit 0x80, the deflect's severity (0x0001FEBC)
  float mGuestC04 = 0.f;             // guest 0xC04, the deflect's slow-down time (0x0001FEDC)
  int mGuestC1C = 0;                 // guest 0xC1C, the heavy hit's step direction (0x0001FF00)
  float mGuestF10 = 0.f;             // guest 0xF10, the phazon attack's range (fn_10_81D4)
  CVector3f mGuestF14 = CVector3f::Zero(); // guest 0xF14, the gun's forward (fn_10_81D4)
  CVector3f mGuestF20 = CVector3f::Zero(); // guest 0xF20, the phazon beam's emitter position
  signed char mGuestF48 = 0;         // guest 0xF48, the phazon attack's shots (fn_10_81D4 reads)
  int mGuestF4C = 0;                 // guest 0xF4C, the phazon attack's shots to fire
  float mGuestF58 = 0.f;             // guest 0xF58, the height PhazonSuck starts at
  bool mGuestF5C40 = false;          // guest 0xF5C bit 0x40 (fn_10_81D4 reads)
  float mGuestF64 = 0.f;             // guest 0xF64, the phazon go-to time (fn_10_6460, 652C); the
                                    // Arrived trigger (fn_10_62B4) reads it as its time offset
  float mGuestFB0 = 0.f;             // guest 0xFB0, the enrage's timer (fn_10_7BC0); the
                                    // EnrageHitReact state (fn_10_7A14) writes its random time
  int mGuestFB4 = 0;                 // guest 0xFB4, the phazon volley's missiles (fn_10_67B8)
  int mGuestFB8 = 1;                 // guest 0xFB8, the phazon volley is a single (0x00020FA0 writes 1)
  int mGuestFBC = 0;                 // guest 0xFBC, the phazon volley pattern index
  float mGuestFC0 = 0.f;             // guest 0xFC0, the phazon blend in [0, 1]
  float mGuestFC4 = 0.f;             // guest 0xFC4, the health the enrage started at; the
                                    // EnrageHitReact state (fn_10_7A14) writes the health
  CAABox mGuest102C = CAABox::MakeNullBox(); // guest 0x102C, the base box (mskNullBox, 0x00020FE4)
  bool mGuest1044 = false;           // guest 0x1044 bit 0x80, 0x102C is kept
  int mGuest1058 = 4;                // guest 0x1058, the phazon volleys to fire: the ctor's
                                    // `li r0, 0x4` (0x000201E8), `stw r0, 0x1058` (0x00020218);
                                    // the EnrageHitReact state (fn_10_7A14) reads it as the
                                    // multiplier of its step (the `mulhw` at 0x00007A8C)
  int mGuest105C = 0;                // guest 0x105C, the phazon volleys fired (0x0002021C); the
                                    // EnrageHitReact state adds its step to it (0x00007A9C)
#ifdef TARGET_PC
  // The stateless helpers of the module (the ones below 0xA000). Their bodies live in
  // src/MetroidPrime/Enemies/PortCDarkSamusHelpers.cpp; docs/module-analysis/DarkSamus_helpers.md
  // has the function map and the arm tables.
  bool HasBoostBall() const; // fn_10_9624: the boost ball's uid (0xE38) is not kInvalidUniqueId
  // The helpers below 0xA000 that the FSM states call (the module's own members, which the host
  // class did not have). The bodies are in src/MetroidPrime/Enemies/PortCDarkSamusHelpers.cpp;
  // docs/module-analysis/DarkSamus_helpers.md has the caller map.
  void DeleteGuestActor(CStateManager& mgr);        // fn_10_4868: the actor at guest 0xF78
  void DeleteGuestActorIfExpired(CStateManager& mgr); // fn_10_48C8: ... when 0xF7C has expired
  void StopGuestSfx();           // fn_10_812C: the sfx handle at guest 0xF60
  void EnsureRenderedActor(const CStateManager& mgr) const; // fn_10_E5C: the base's EnsureRendered
  void InitGuestModelData(CModelData* model); // fn_10_2E44: the CModelData ctor
  void RunTauntCmd(EStateMsg msg, pas::ETauntType type); // fn_10_8698: the taunt command
  void RunGetupCmd(EStateMsg msg, const CBCGetupCmd& cmd); // fn_10_22020: the getup command
  void StopBoostBallParticles();                  // fn_10_99B8: the boost ball's old effects
  void RollBoostBallToPosition(float dt);         // fn_10_A154: the actor moves to the ball
  void RunLoopAttackCmd(EStateMsg msg, const CBCLoopAttackCmd& attack); // fn_10_8870
  void UpdateShieldFromHealth(EStateMsg msg);     // fn_10_6DAC: the shield flag and direction
  void FindPathToDestination(CStateManager& mgr, const CVector3f& destination,
                             float dt); // fn_10_6F90: the path find to the destination
  void DeleteGuestUids(CStateManager& mgr); // fn_10_69C0: the uids at guest 0x934 are deleted
  void FindPathToWaypoint(CStateManager& mgr, float dt, EStateMsg msg); // fn_10_6374
  // The module's own members whose guest offsets the helpers below 0xA000 touch. The host class
  // had none of them; the names are the guest offsets because the loader does not name them.
  int mGuest930 = 0;                 // guest 0x930, the number of uids in 0x934 (the ctor writes 0)
  ushort mGuest934[18] = {};          // guest 0x934, the uids fn_10_69C0 deletes (TUniqueId)
  // The phazon enrage (the 0xF80 sub-object, fn_10_20F50; the 0xEF0 one, fn_10_45F8; the 0xF68
  // visor effect token, fn_10_21104). The sphere's asset (0x1020) and contact damage (0x1000) are
  // mData's mPhazonEnrageSphere / mPhazonEnrageSphereContactDamage, which the ctor copies.
  void UpdatePhazonEnrage(CStateManager& mgr, EStateMsg msg, float dt); // fn_10_7F98
  void GrowPhazonScale(CStateManager& mgr, float dt);   // fn_10_7538
  void ShrinkPhazonScale(CStateManager& mgr, float dt); // fn_10_746C
  void SetPhazonScale(const CVector3f& scale, CStateManager& mgr); // fn_10_7304
  void UpdatePhazonSphere(float dt);                    // fn_10_7610
  CVector3f GetPhazonSpherePos() const;                 // fn_10_7780
  void CreatePhazonSphere();                            // fn_10_786C
  void TouchPhazonSphere(CStateManager& mgr);           // fn_10_7D00
  void StartPhazonVisorEffect(CStateManager& mgr, float duration); // fn_10_490C
  void ResetPhazonAttack();                             // fn_10_452C (on 0xEF0)
  static rstl::optional_object< TToken< CGenDescription > > PhazonVisorToken(CAssetId id);
  float mGuestFAC = 0.f;             // guest 0xFAC, the z scale the phazon scale started at
  float mGuestFC8 = -1000.f;         // guest 0xFC8, when the phazon volley started (fn_10_7610)
  float mGuestFCC = -1000.f;         // guest 0xFCC (fn_10_7F98 lowers it, lbl_10_rodata_410)
  float mGuest101C = -1000.f;        // guest 0x101C, the sphere's last contact (fn_10_7D00)
  rstl::auto_ptr< CElementGen > mGuest1028; // guest 0x1024/0x1028, the enrage sphere (fn_10_786C)
  float mGuestF50 = 0.f;             // guest 0xF50 (fn_10_452C clears it)
  float mGuestF54 = -1000.f;         // guest 0xF54 (fn_10_452C: lbl_10_rodata_140)
  bool mGuestF5C80 = false;          // guest 0xF5C bit 0x80 (fn_10_452C clears it)
  // guest 0xF6C, the visor effect's particle (mData.mPartE701DAEA, fn_10_21104)
  rstl::optional_object< TToken< CGenDescription > > mGuestF6C =
      PhazonVisorToken(mData.mPartE701DAEA);
  float mGuestD14 = 0.f;             // guest 0xD14 (fn_10_9E00)
  float mGuestD1C = 0.f;             // guest 0xD1C, a copy of the module's bss_128[1] (fn_10_6DAC)
  float mGuestD20 = 0.f;             // guest 0xD20, a copy of the module's bss_128[2] (fn_10_6DAC)
  float mGuestE2C = 0.f;             // guest 0xE2C (fn_10_9E00)
  float mGuestE30 = 0.f;             // guest 0xE30 (fn_10_9E00)
  float mGuestE34 = 0.f;             // guest 0xE34 (fn_10_9E00)
  float mGuestE38 = 0.f;             // guest 0xE38 (fn_10_9E00)
  CVector3f mGuestEE4 = CVector3f::Zero(); // guest 0xEE4, the path find's fallback destination
  TUniqueId mGuestF78 = kInvalidUniqueId; // guest 0xF78 (fn_10_4868, fn_10_48C8)
  float mGuestF7C = 0.f;         // guest 0xF7C (fn_10_48C8); no ctor stores it, read only after 490C writes it (tail14)
  CSfxHandle mGuestF60;          // guest 0xF60 (fn_10_812C)
  // The phazon beam (the 0xEF0 sub-object, fn_10_45F8, whose ctor arguments are the loader's
  // pART_0x908b06e9 into 0xEF0, pART_0x494de4a4 into 0xEFC (its locked token 0xF00), damageInfo
  // copied to 0xF2C and sound into 0xF5E): the generator 0xEF4/0xEF8 and its collision effects.
  // The host reads the three from mData, which nothing writes after the loader.
  void CreatePhazonBeam();                             // fn_10_6118
  void ReleasePhazonBeam();                            // fn_10_58C0
  void StartPhazonBeamSfx();                           // fn_10_8178
  void UpdatePhazonBeam(CStateManager& mgr, float dt); // fn_10_5974
  static rstl::optional_object< TLockedToken< CGenDescription > > PhazonBeamHitToken(CAssetId id);
  rstl::auto_ptr< CElementGen > mGuestEF8; // guest 0xEF4/0xEF8, the beam (fn_10_6118, fn_10_58C0)
  // guest 0xF00, the beam's hit effect (mData.mPart494DE4A4, fn_10_45F8 at 0x00004644)
  rstl::optional_object< TLockedToken< CGenDescription > > mGuestF00 =
      PhazonBeamHitToken(mData.mPart494DE4A4);
  // The render pass (fn_10_17F04) and what it reads.
  void ConstructPhazonBeam();   // fn_10_45F8, the 0xEF0 sub-object's ctor (the scalars)
  bool HasPhazonBeam() const;   // fn_10_4AA8: the beam 0xEF8 is not null
  float GetFadeFactor() const;  // fn_10_185CC: 1, then down to 0 over fn_10_18628's window
  // guest 0xE3C / 0xE4C, the boost ball's two swooshes (fn_10_9B24 builds them, fn_10_99B8
  // deletes them). Owned: the 0xE10 sub-object's destructor fn_10_1F72C deletes both (the
  // vtable's word 2 with 1, 0x0001F7F4 and 0x0001F840), as ~CDarkSamus does.
  CParticleSwoosh* mBoostBallSwooshE3C = nullptr;
  CParticleSwoosh* mBoostBallSwooshE4C = nullptr;
  // The 0xE10 sub-object's effect tokens (fn_10_895C; its arguments follow the data order:
  // boostBallModel r4..r6, boostBallDamage *r7, boostBallGlow r8, boostBallTrailSwoosh r9, sWHC
  // r10, the two sounds, boostBallCollision, the collision sound). A valid swoosh id (`addis r0,
  // rX, 0x1; cmplwi r0, 0xffff` at 0x00008A48/0x00008A58 and 0x00008B70/0x00008B74) is fetched as
  // an 'SWHC' (`lis r4, 0x5357; addi r0, r4, 0x4843`) into the optional at 0xE40 (valid byte
  // 0xE48, 0x00008B3C) or 0xE50 (0xE58, 0x00008BDC) and locked (0x00008B6C, 0x00008C0C).
  static rstl::optional_object< TLockedToken< CSwooshDescription > >
  BoostBallSwooshToken(CAssetId id);
  rstl::optional_object< TLockedToken< CSwooshDescription > > mBoostBallSwooshTokenE40 =
      BoostBallSwooshToken(mData.mBoostBallTrailSwoosh); // r9
  rstl::optional_object< TLockedToken< CSwooshDescription > > mBoostBallSwooshTokenE50 =
      BoostBallSwooshToken(mData.mSWHC); // r10
  CAssetId mBoostBallGlowPart = mData.mBoostBallGlow; // guest 0xE5C (+0x4C), r8 (0x00008A10)
  // guest 0xE70 (+0x60, valid byte 0xE7C = +0x6C), the boost ball's collision 'PART'
  // (boostBallCollision, fn_10_895C's r30 stack argument): an invalid id leaves it empty (`addis
  // r0, r30, 0x1; cmplwi r0, 0xffff` at 0x00008C10/0x00008C14), a valid one is the pool's 'PART'
  // (`lis r4, 0x5041` at 0x00008C1C) copied and locked by fn_10_8D30 (0x00008C78) - the same as
  // PhazonBeamHitToken. The collision sound at guest 0xE80 (+0x70, 0x00008A6C..) is
  // mData.mBoostBallCollisionSoundOneShot.
  rstl::optional_object< TLockedToken< CGenDescription > > mBoostBallCollisionToken =
      PhazonBeamHitToken(mData.mBoostBallCollision);
  void StartBoostBallRoll(CStateManager& mgr);            // fn_10_A1C0
  void PlayBoostBallCollision(CStateManager& mgr);        // fn_10_A814
  void RollToBoostHint(CStateManager& mgr, float dt);     // fn_10_AA04
  void CreateBoostBallEffects(); // fn_10_9B24: the swooshes 0xE3C/0xE4C and the generator 0xE64
#endif
  // guest 0x6B4 is the base's animation state (the byte fn_10_8698 stores 1 to on Activate and 0
  // to on Deactivate, and the DOL's ShouldRetry advances): it is the base's own member, the
  // host's CAnimationState mAnimationState, which CPatterned::GetAnimOver reads.
  // helpers2: the bodies this lane ported (the REL's functions at 0xA000 and up).
#ifdef TARGET_PC
  uint GetStageCount() const;   // fn_10_17E48 (0x00017E48, 0x8 bytes): the guest's 0x978
#endif
#ifdef TARGET_PC
  // dsfsm2 slice 7: the anim-event handler's (fn_10_1BEFC) helpers and the members it writes.
  float GetMeleeReach() const;            // fn_10_1BDF0
  void FadeEnvFxOut(CStateManager& mgr);  // fn_10_1BE04
  void FadeEnvFxIn(CStateManager& mgr);   // fn_10_1BE80
  int mGuestAB0 = 0;                      // guest 0xAB0, the freeze beam shots fired (fn_10_1BEFC);
                                          // the FreezeBeam state (fn_10_16CDC) uses it as its
                                          // counter: at least 1 makes it deliver the exit state
                                          // command (0x00016DA8)
  bool mBoostBallFlag20 : 1;              // 0xebc 0x20; the ctor and fn_10_1BEFC clear it,
                                          // fn_10_A3B0 tests it at 0x0000A4DC, sets it at 0x0000A660
  bool mBoostBallFlag80 = false;          // guest 0xEBC bit 0x80 (fn_10_1BEFC clears it); the
                                          // DoneRolling trigger reads it (`extrwi r0, r0, 1, 24` at
                                          // 0x00009378, the PowerPC bit 24 being
                                          // 1u << (31 - 24) = 0x80)
  float mGuestEA8 = -1000.f;              // guest 0xEA8, the boost ball's start time (fn_10_1BEFC);
                                          // -1000.0f at construction (fn_10_895C +0x98, rodata_260);
                                          // the DoneRolling trigger reads it as the roll's other
                                          // time (0x00009390)
  CVector3f mGuestEAC = CVector3f::Zero(); // guest 0xEAC, the actor's position then; the
                                          // DoneRolling trigger measures to it (0x000093B8)
  float mGuestEB8 = 0.f;                  // guest 0xEB8, the player's distance then
#endif
#ifdef TARGET_PC
  // dsfsm2 slice 8: the anim event's attack callees and the records they read.
  void ClearEnrageSphere();                 // fn_10_77B8
  void ApplyMeleeDamage(CStateManager& mgr); // fn_10_13998
  void SpawnMeleePulse(CStateManager& mgr); // fn_10_13AB4
  void FireMissile(CStateManager& mgr);     // fn_10_1353C
  void FireSuperMissile(CStateManager& mgr); // fn_10_133D0
  void FireFreezeBeam(CStateManager& mgr);  // fn_10_9164
  // The melee record's two 'PART' tokens (fn_10_EA24): +0x20 (guest 0x9C4) meleeAttackFX, +0x2C
  // (guest 0x9D0) pART. Retail builds them unconditionally; the host only for a valid asset.
  rstl::optional_object< TLockedToken< CGenDescription > > mMeleeAttackFXToken;
  rstl::optional_object< TLockedToken< CGenDescription > > mMeleePartToken;
  // guest 0xA0C (fn_10_21A14) and 0xA4C (fn_10_2199C): the missile and super missile infos the
  // vtable's ProjectileInfo (fn_10_1C6C8) answers for the attacks 0x40/0x80 and 0x100.
  rstl::optional_object< CProjectileInfo > mMissileInfo;
  rstl::optional_object< CProjectileInfo > mSuperMissileInfo;
  // guest 0xA88 (fn_10_217FC), the freeze beam's 'WPSC' token; its valid byte 0xA90 is the
  // optional's presence. The damage 0xA94 and visor texture 0xAB4 are mData's.
  rstl::optional_object< TToken< CWeaponDescription > > mFreezeBeamToken;
#endif
#ifdef TARGET_PC
  // dsfsm2 slice 8, the Render chain (Render fn_10_18964 calls 1890C, slice 10).
  void DrawFlashQuad(float alpha, float size) const; // fn_10_1863C
  void RenderFlash() const;                          // fn_10_1890C
  // fn_10_EDF4's record at guest 0xDF0: the flash's 'TXTR' token (tXTR; its valid byte 0xDFC is
  // the optional's presence, built only for a valid asset) and the quad's size 0xE00
  // (unknown_0x2c6a3344). EDF4 also writes 0xE04 = -1000 (0x0000EE34) and 0xE08/0xE0C = 0.
  rstl::optional_object< TLockedToken< CTexture > > mFlashTexture;
  float mFlashSize = 0.f;
#endif
#ifdef TARGET_PC
  // dsfsm2 slice 9: the phazon volley (DoUserAnimEvent's 0x800000 shot).
  void FirePhazonVolley(CStateManager& mgr);       // fn_10_5840
  void FirePhazonShot(CStateManager& mgr);         // fn_10_50B8
  void FirePhazonSuperMissile(CStateManager& mgr); // fn_10_4C40
  // guest 0xF80 (the phazon sub-object's info, fn_10_20C2C from fn_10_20F50) and 0x1064
  // (fn_10_20BFC): the infos ProjectileInfo answers for the attack 0x800000 by mGuestFB8 0 / 1.
  rstl::optional_object< CProjectileInfo > mPhazonShotInfo;
  rstl::optional_object< CProjectileInfo > mPhazonSuperMissileInfo;
  // fn_10_9E00: the boost ball actor spawned (DoUserAnimEvent's BecomeRagDoll with 0x20000). The
  // uid goes to mBoostBallUid (guest 0xE38 is that halfword, `sth` at 0x00009E4C; mGuestE38 is
  // unused).
  void SpawnBoostBall(CStateManager& mgr);
#endif
#ifdef TARGET_PC
  // dsfsm2 slice 10: the render chain (PreRender fn_10_18AB8, Render fn_10_18964).
  void RenderSilhouetteModel(const CStateManager& mgr) const; // fn_10_18238
  void RenderInvulnerableModel() const;                      // fn_10_183E8
  CColor GetSilhouetteColor() const;                         // fn_10_181E0
  // guest 0xDE8: the model's visibility; the constructor writes lbl_10_rodata_3E8 = 1.0f
  // (0x00020094), Think's fade (fn_10_1D6CC) moves it.
  float mGuestDE8 = 1.f;
  // guest 0xDED bits 0x80 / 0x40: PreRender's visor was the dark / echo visor (0x00018CD8 /
  // 0x00018CF0); the constructor clears both (0x000200A0 / 0x000200AC).
  bool mDarkVisorDED80 = false;
  bool mEchoVisorDED40 = false;
  // guest 0xD64..0xD70: the invulnerable model, an optional (its valid byte 0xD70, cleared at
  // 0x00020038) the constructor fills with the CSkinnedModel of mInvulnerableModel /
  // mInvulnerableSkinRules (the new at 0x000202D8, fn_10_20600 at 0x00020408); 183E8 renders it.
  rstl::optional_object< TLockedToken< CSkinnedModel > > mInvulnerableSkinnedModel;
  // guest 0xE60/0xE64: the boost ball's element generator (fn_10_9B24 builds it from 0xE5C).
  rstl::auto_ptr< CElementGen > mBoostBallElementGen;
#endif
#ifdef TARGET_PC
  // dsfsm2 slice 11: PreRender's two calls and Think's head aim.
  void UpdateHeadAim(CStateManager& mgr);                        // fn_10_10A64
  void TwistHead();                                              // fn_10_10864
  void UpdateMeleePulseAndEffects(CStateManager& mgr, float dt); // fn_10_12DD0
  void UpdateBoostBallEffects(CStateManager& mgr, float dt);     // fn_10_8F50
  // guest 0xD40: the head's yaw toward the player in degrees, clamped to -30..30 (fn_10_10A64);
  // the constructor writes lbl_10_rodata_3FC = 0.0f (0x0001FFE8).
  float mHeadYaw = 0.f;
  // guest 0xD44: the "Head_1" bone (GetSegIdFromString at 0x0001FFCC, the byte at 0x0001FFF0).
  CSegId mHeadSegId;
  // guest 0xD48: the elapsed time 0x8B0 of the last twist; the constructor writes
  // lbl_10_rodata_410 = -1000.0f (0x0001FFF4).
  float mHeadTwistTime = -1000.f;
#endif
#ifdef TARGET_PC
  // dsfsm2 slice 11: Think's visibility fade and targetability.
  void UpdateFade(float dt);                // fn_10_1D6CC
  void UpdateTargetable(CStateManager& mgr); // fn_10_1D5C4
#endif
#ifdef TARGET_PC
  // dsfsm2 slice 12: the rest of Think's calls (fn_10_1B1FC).
  void UpdateDiveContact(CStateManager& mgr);              // fn_10_1ACA0
  void PushPlayerAfterBounce(CStateManager& mgr);          // fn_10_13868
  void UpdateSilhouetteFade(CStateManager& mgr, float dt); // fn_10_F710
  void UpdateInvulnerableFade(float dt);                   // fn_10_F654
  void UpdateMeleeHit(CStateManager& mgr);                 // fn_10_1AE08
  void UpdateStageFlash(CStateManager& mgr);               // fn_10_1B0E0
  void CheckHealthThreshold(CStateManager& mgr);           // fn_10_1B154
  // guest 0x10A4: fn_10_1B154 sent kSS_AILogicState1 (0x0001B1E0); the constructor clears it
  // (0x0002026C). Its threshold 0x10A0 is mData.mUnknown72EDEB7D (the constructor's f1,
  // `stfs f23, 0x10a0` at 0x00020268; the loader's property 0x72EDEB7D at 0x0000D4FC).
  bool mHealthThresholdSent = false;
#endif
#else
  uchar x7c0_[0x1128 - 0x7c0];
#endif
};
CHECK_SIZEOF(CDarkSamus, 0x1128)

#endif
