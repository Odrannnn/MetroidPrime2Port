#ifndef _CCOMMANDOPIRATE
#define _CCOMMANDOPIRATE

#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "REL/REL_Setup.h"

#ifdef TARGET_PC
#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Animation/CSegId.hpp"
#include "Kyoto/SObjectTag.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "MetroidPrime/CBoneTracking.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CVisualKnowledge.hpp"
#include "MetroidPrime/Enemies/CBurstFire.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"
#include "rstl/single_ptr.hpp"

class CPirateRagDoll;

// The E-grenade half of the REL's parameter block (guest data+0x68, 0x70 bytes). The guest block
// is built by fn_9_98C (the grenade property block, its own damage info included) and fn_9_F50C (the eight
// floats on top); the guest offsets are the REL's and the host layout is its own. The four
// constants 0.1f/150.f/0.1f/150.f are the guest's (fn_9_98C stores them from its own arguments at
// 0x0000098C..0x00000A0C); the flag byte at guest +0x4C gets its 0x80 bit (0x00000A14) from the
// caller's 1 (fn_9_168 @0x00000610), so CBouncyGrenadeData::x4c_ is true.
struct CCommandoPirateGrenadeData {
  CCommandoPirateGrenadeData(float grenadeMass, float unknownED086CE0,
                             const CDamageInfo& grenadeDamage, int grenadeNumBounces,
                             CAssetId grenadeExplosion, CAssetId grenadeExplosion2,
                             CAssetId grenadeTrail, CAssetId grenadeEffect,
                             ushort soundGrenadeBounce, ushort soundGrenadeExplode,
                             float empDuration,
                             float grenadeAttackChance, float grenadeMinAttackInterval,
                             float grenadePostAttackPause, float grenadeMinAttackDist,
                             float grenadeMaxAttackDist, float grenadeMinLaunchSpeed,
                             float grenadeMaxLaunchSpeed)
  : mGrenadeMass(grenadeMass)
  , mUnknownED086CE0(unknownED086CE0)
  , mGrenadeDamage(grenadeDamage)
  , mGrenadeNumBounces(grenadeNumBounces)
  , mGrenadeExplosion(grenadeExplosion)
  , mGrenadeExplosion2(grenadeExplosion2)
  , mGrenadeTrail(grenadeTrail)
  , mGrenadeEffect(grenadeEffect)
  , mSoundGrenadeBounce(soundGrenadeBounce)
  , mSoundGrenadeExplode(soundGrenadeExplode)
  , mRadiusMin(0.1f)
  , mDamageMax(150.f)
  , mRadiusMax(0.1f)
  , mDamageMin(150.f)
  , mFlags(0x80)
  , mEMPDuration(empDuration)
  , mGrenadeAttackChance(grenadeAttackChance)
  , mGrenadeMinAttackInterval(grenadeMinAttackInterval)
  , mGrenadePostAttackPause(grenadePostAttackPause)
  , mGrenadeMinAttackDist(grenadeMinAttackDist)
  , mGrenadeMaxAttackDist(grenadeMaxAttackDist)
  , mGrenadeMinLaunchSpeed(grenadeMinLaunchSpeed)
  , mGrenadeMaxLaunchSpeed(grenadeMaxLaunchSpeed) {}

  float mGrenadeMass;              // guest +0x00, fn_9_98C @0x0000098C
  float mUnknownED086CE0;          // guest +0x04, fn_9_98C @0x00000994
  CDamageInfo mGrenadeDamage;      // guest +0x08 (0x1c), fn_9_98C @0x0000099C..0x000009D8
  int mGrenadeNumBounces;          // guest +0x24, fn_9_98C @0x000009E4
  CAssetId mGrenadeExplosion;      // guest +0x28, fn_9_98C @0x000009E8
  CAssetId mGrenadeExplosion2;     // guest +0x2c, fn_9_98C @0x000009EC (the same asset again)
  CAssetId mGrenadeTrail;          // guest +0x30, fn_9_98C @0x000009F0
  CAssetId mGrenadeEffect;         // guest +0x34, fn_9_98C @0x000009F4
  ushort mSoundGrenadeBounce;      // guest +0x38, fn_9_98C @0x000009F8
  // guest +0x3a: fn_9_98C is a leaf, so its 0xa(r1) (0x000009D8) is the low half of the
  // caller's stack word at +0x8 - the loader's sound_GrenadeExplode (the lwz 0xc80 at 0x0000062C,
  // clrlwi'd at 0x00000640 and stored at 0x00000660).
  ushort mSoundGrenadeExplode;     // guest +0x3a, fn_9_98C @0x000009FC
  float mRadiusMin;                // guest +0x3c, fn_9_98C @0x00000A00 (the guest's 0.1f)
  float mDamageMax;                // guest +0x40, fn_9_98C @0x00000A04 (the guest's 150.f)
  float mRadiusMax;                // guest +0x44, fn_9_98C @0x00000A08 (the guest's 0.1f)
  float mDamageMin;                // guest +0x48, fn_9_98C @0x00000A0C (the guest's 150.f)
  uchar mFlags;                    // guest +0x4c, fn_9_98C @0x00000A14 (bit 0x80 from fn_9_168's 1)
  float mEMPDuration;              // guest +0x50, fn_9_F50C @0x00000A28
  float mGrenadeAttackChance;      // guest +0x54, fn_9_F50C @0x00000A2C
  float mGrenadeMinAttackInterval; // guest +0x58, fn_9_F50C @0x00000A30
  float mGrenadePostAttackPause;   // guest +0x5c, fn_9_F50C @0x00000A34
  float mGrenadeMinAttackDist;     // guest +0x60, fn_9_F50C @0x00000A38
  float mGrenadeMaxAttackDist;     // guest +0x64, fn_9_F50C @0x00000A3C
  float mGrenadeMinLaunchSpeed;    // guest +0x68, fn_9_F50C @0x00000A40
  float mGrenadeMaxLaunchSpeed;    // guest +0x6c, fn_9_F50C @0x00000A44
};

// The shield half of the REL's parameter block (guest data+0xD8, 0x84 bytes), built by
// fn_9_EF80 out of the shield property block (fn_9_1308) and copied by fn_9_EB84.
struct CCommandoPirateShieldData {
  CCommandoPirateShieldData(const CDamageInfo& chargeDamage,
                             const CDamageVulnerability& chargeVulnerability,
                             float chargeMinAttackDist, float chargeMaxAttackDist,
                             float chargeSpeed, CAssetId explodeEffect, ushort soundExplode,
                             float unknown6CB0DA5A, float unknownC3938663, float armChance,
                             float armTime, float armTimeVariation, CAssetId armExplodeEffect,
                             CAssetId chargeEffect, CAssetId armEffect, ushort soundTurnOn,
                             ushort soundTurnOff);

  CDamageInfo mChargeDamage;            // guest +0x00 (0x1c), fn_9_EF80 @0x0000EF80
  CDamageVulnerability mChargeVulnerability; // guest +0x1c (0x30), fn_9_EF80 @0x0000EFA4
  float mChargeMinAttackDist;           // guest +0x4c, fn_9_EF80 @0x0000EFB4
  float mChargeMaxAttackDist;           // guest +0x50, fn_9_EF80 @0x0000EFB8
  float mChargeSpeed;                   // guest +0x54, fn_9_EF80 @0x0000EFBC
  CAssetId mExplodeEffect;              // guest +0x58, fn_9_EF80 @0x0000EFC0
  ushort mSoundExplode;                 // guest +0x5c, fn_9_EF80 @0x0000EFC4
  float mUnknown6CB0DA5A;                // guest +0x60, fn_9_EF80 @0x0000EFC8
  float mUnknownC3938663;                // guest +0x64, fn_9_EF80 @0x0000EFCC
  float mArmChance;                     // guest +0x68, fn_9_EF80 @0x0000EFD0
  float mArmTime;                       // guest +0x6c, fn_9_EF80 @0x0000EFD4
  float mArmTimeVariation;              // guest +0x70, fn_9_EF80 @0x0000EFD8
  CAssetId mArmExplodeEffect;           // guest +0x74, fn_9_EF80 @0x0000EFDC
  CAssetId mChargeEffect;               // guest +0x78, fn_9_EF80 @0x0000EFE0
  CAssetId mArmEffect;                  // guest +0x7c, fn_9_EF80 @0x0000EFE4
  ushort mSoundTurnOn;                  // guest +0x80, fn_9_EF80 @0x0000EFE8
  ushort mSoundTurnOff;                 // guest +0x82, fn_9_EF80 @0x0000EFEC
};

// fn_9_EF80: the members in the argument order of the loader's call (0x00000728): r4 the
// damage (inlined CDamageInfo copy, 0x0000EFD0..0x0000F054), r5 the vulnerability (fn_9_D294,
// 0x0000F060), f1..f3 +0x4c..+0x54, r6/r7 +0x58/+0x5c, f4..f8 +0x60..+0x70, r8..r10
// +0x74..+0x7c, the stack's two halves +0x80/+0x82 (0x0000F064..0x0000F0A0).
// @ fn_9_EF80
inline CCommandoPirateShieldData::CCommandoPirateShieldData(
    const CDamageInfo& chargeDamage, const CDamageVulnerability& chargeVulnerability,
    float chargeMinAttackDist, float chargeMaxAttackDist, float chargeSpeed,
    CAssetId explodeEffect, ushort soundExplode, float unknown6CB0DA5A, float unknownC3938663,
    float armChance, float armTime, float armTimeVariation, CAssetId armExplodeEffect,
    CAssetId chargeEffect, CAssetId armEffect, ushort soundTurnOn, ushort soundTurnOff)
  : mChargeDamage(chargeDamage)
  , mChargeVulnerability(chargeVulnerability)
  , mChargeMinAttackDist(chargeMinAttackDist)
  , mChargeMaxAttackDist(chargeMaxAttackDist)
  , mChargeSpeed(chargeSpeed)
  , mExplodeEffect(explodeEffect)
  , mSoundExplode(soundExplode)
  , mUnknown6CB0DA5A(unknown6CB0DA5A)
  , mUnknownC3938663(unknownC3938663)
  , mArmChance(armChance)
  , mArmTime(armTime)
  , mArmTimeVariation(armTimeVariation)
  , mArmExplodeEffect(armExplodeEffect)
  , mChargeEffect(chargeEffect)
  , mArmEffect(armEffect)
  , mSoundTurnOn(soundTurnOn)
  , mSoundTurnOff(soundTurnOff) {}

// The REL's parameter block (guest 0x15e bytes at guest 0x7C0), built by fn_9_EDC0 and copied into
// the object by fn_9_EA24. The guest offsets are the REL's; the host layout is its own.
struct CCommandoPirateData {
  CCommandoPirateData(ushort soundImpact, ushort soundHurled, ushort soundDeath, ushort alwaysFF,
                      int alwaysFF2, const CDamageInfo& bladeDamage, CAssetId projectile,
                      const CDamageInfo& projectileDamage, ushort soundProjectile,
                      float aggressiveness, float coverCheck, float searchRadius, float dodgeCheck,
                      float hearingRadius, float intraBurstShotTime, float intraBurstShotVariation,
                      const CCommandoPirateGrenadeData& grenade,
                      const CCommandoPirateShieldData& shield, uchar flags0, uchar flags1)
  : mSoundImpact(soundImpact)
  , mSoundHurled(soundHurled)
  , mSoundDeath(soundDeath)
  , mAlwaysFF(alwaysFF)
  , mAlwaysFF2(alwaysFF2)
  , mBladeDamage(bladeDamage)
  , mProjectile(projectile)
  , mProjectileDamage(projectileDamage)
  , mSoundProjectile(soundProjectile)
  , mAggressiveness(aggressiveness)
  , mCoverCheck(coverCheck)
  , mSearchRadius(searchRadius)
  , mDodgeCheck(dodgeCheck)
  , mHearingRadius(hearingRadius)
  , mIntraBurstShotTime(intraBurstShotTime)
  , mIntraBurstShotVariation(intraBurstShotVariation)
  , mGrenade(grenade)
  , mShield(shield)
  , mFlags0(flags0)
  , mFlags1(flags1) {}

  ushort mSoundImpact;             // guest +0x00, fn_9_EDC0 @0x0000E83C
  ushort mSoundHurled;             // guest +0x02, fn_9_EDC0 @0x0000E844
  ushort mSoundDeath;              // guest +0x04, fn_9_EDC0 @0x0000E84C
  ushort mAlwaysFF;                // guest +0x06, fn_9_EDC0 @0x0000E854
  int mAlwaysFF2;                  // guest +0x08, fn_9_EDC0 @0x0000E85C
  CDamageInfo mBladeDamage;        // guest +0x0c (0x1c), fn_9_EDC0 @0x0000E864..0x0000E8B4
  CAssetId mProjectile;            // guest +0x28, fn_9_EDC0 @0x0000E8B8
  CDamageInfo mProjectileDamage;   // guest +0x2c (0x1c), fn_9_EDC0 @0x0000E8BC..0x0000E90C
  ushort mSoundProjectile;         // guest +0x48, fn_9_EDC0 @0x0000E910
  float mAggressiveness;           // guest +0x4c, fn_9_EDC0 @0x0000E918
  float mCoverCheck;               // guest +0x50, fn_9_EDC0 @0x0000E91C
  float mSearchRadius;             // guest +0x54, fn_9_EDC0 @0x0000E920
  float mDodgeCheck;               // guest +0x58, fn_9_EDC0 @0x0000E924
  float mHearingRadius;            // guest +0x5c, fn_9_EDC0 @0x0000E928
  float mIntraBurstShotTime;       // guest +0x60, fn_9_EDC0 @0x0000E92C
  float mIntraBurstShotVariation;  // guest +0x64, fn_9_EDC0 @0x0000E930
  CCommandoPirateGrenadeData mGrenade; // guest +0x68 (0x70), fn_9_EDC0 @0x0000E934
  CCommandoPirateShieldData mShield;   // guest +0xD8 (0x84), fn_9_EDC0 @0x0000E938
  // guest +0x15c / +0x15d: the guest packs eight and four bits of the "sound" property into these
  // bytes (fn_9_EDC0 @0x0000EED4..0x0000EF60); the bit map is in the analysis note. The block
  // starts at guest +0x7C0, so the two bytes are the object's bytes at +0x91C and +0x91D.
  uchar mFlags0;  // 0x15c (guest this+0x91c)
  uchar mFlags1;  // 0x15d (guest this+0x91d)
};
#endif

// The REL's CCommandoPirate (module 9): the Pirate Commando. The retail object is 0xC68 bytes on
// the GameCube, built by fn_9_D904, and its vtable is lbl_9_data_828 (82 words, 34 local
// overrides); the analysis is docs/module-analysis/CommandoPirate.md.
//
// Milestone 1 (this file's first slice) is load-and-stand: the loader, the constructor, the path
// search, the projectile info, the bone tracking and the accessors the vtable gives one-line
// bodies to are host code. The state machine (the module's trigger, state and code tables), the
// melee, the E-grenade, the shield and the burst fire are milestone 2 and forward, and every
// override that belongs to them forwards to CPatterned behind a once-only StandIn log.
class REL_EXPORT CCommandoPirate : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
#ifdef TARGET_PC
  // Argument order is the REL loader's call at 0x00000964 (fn_9_168): uid, editor-property name,
  // entity info, transform, model, actor parameters, patterned info, then the parameter block.
  CCommandoPirate(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                  const CTransform4f& xf, const CModelData& modelData,
                  const CActorParameters& actParms, const CPatternedInfo& pInfo,
                  const CCommandoPirateData& data);
#endif
  ~CCommandoPirate() override;

  // CEntity
  void PreThink(float dt, CStateManager& mgr) override;
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

  // CActor
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  void Render(const CStateManager& mgr) const override;
  void PreRenderAllViewports(CStateManager& mgr) override;
  const CDamageVulnerability* GetDamageVulnerability() const override;

  // CAi
  void KnockBack(CStateManager& mgr, const CKnockBackInfo& info) override;
  bool CanBeShot(const CStateManager& mgr, int attributes) override;
  bool IsListening() const override;
  bool Listen(CStateManager& mgr, const CVector3f& position, EListenNoiseType type) override;

  // CPatterned
  CProjectileInfo* ProjectileInfo() override;
  CPathFindSearch* GetSearchPath() override;
  float GetGravityConstant() const override;
  CPathFindPointSearch* GetPointSearchPath() override;
  CDamageInfo GetContactDamage() const override;
  CRagDoll* GetRagDoll() const override;
  TUniqueId GetAttackTarget() const override;
  void SetupStateMachine(CStateManager& mgr) override;
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;
  void CollidedWith(const TUniqueId& id, const CCollisionInfoList& list,
                    CStateManager& mgr) override;
#ifdef TARGET_PC
  // Port console (`var <id> cp.<name> [value]`): the module's flag bytes and ids, set by a driver
  // and put back in the same tick, so a state body's arms run on demand.
  static bool PortConsoleVar(CEntity& ent, const char* name, const int* value, int& current);
#endif

#ifdef TARGET_PC
  // The body's state machine. Retail fn_9_BDE0 (vtable slot 60) calls the base's setup and
  // then registers the module's two tables, which the module's .ctors builder (fn_9_F9F8)
  // fills in: 34 triggers (lbl_9_data_198, .data 0x198) and 36 states (lbl_9_data_568,
  // .data 0x568). The names are retail's - the state machine asset's nodes are matched by
  // name - and each comment is the retail function the entry points at. The bodies are
  // stand-ins in this slice; a later one ports them.
  //
  // Triggers (lbl_9_data_198, 34 entries; the .rodata offset is in the analysis note).
  bool StateOver(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BDCC
  bool ShouldWarpIn(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BD98
  bool ShouldMeleeAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BCB8
  bool ShouldFireEGrenade(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BBC8
  bool ShouldRetreat(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BB7C
  bool ShouldJumpBack(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BB10
  bool ShouldAmbush(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BB04
  bool BreakAmbush(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BAF8
  bool HasLineOfSight(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BAEC
  bool UnderFire(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BAD0
  bool HeardShot(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BAB4
  bool HasTarget(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BA78
  bool HasNewTarget(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_BA20
  bool PathShagged(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B9D8
  bool PathOver(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B990
  bool IsOffPath(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B958
  bool HasAttackPattern(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B900
  bool IsFacingTarget(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B818
  bool AttackPatternOver(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B7FC
  bool TooClose(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B76C
  bool ShouldDodge(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B690
  bool ShouldArmShield(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B660
  bool ShouldShieldCharge(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B250
  bool ShouldBoost(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B1E0
  bool AbortShieldCharge(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B1D4
  bool IsAggressive(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B1C8
  bool FoundJumpPoint(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B1A8
  bool ShouldCrouch(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B168
  bool ShouldWallHang(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B114
  bool ShouldCover(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B0F4
  bool FoundCover(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B0D4
  bool ShouldCoverAttack(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_B0B4
  bool CoverBlown(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_AFD4
  bool AbortSeekCover(CStateManager& mgr, const CTriggerData& data) const;  // fn_9_AFB8

  // States (lbl_9_data_568, 36 entries). PathFind, Patrol and Dead hide CPatterned's own,
  // which is what retail does too: the module's table replaces the base's entries.
  void Lurk(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_ADD4
  void Ambush(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_ADA0
  void Alert(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_AE04
  void FaceTarget(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_ABDC
  void SelectTarget(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_AB08
  void SetTargetDest(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_AA20
  void SetRetreatDest(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_A97C
  void SetJumpDest(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_A8E8
  void SetPathMeshDest(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_A838
  void MeleeAttack(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_A4BC
  void EGrenadeAttack(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_A2EC
  void PostEGrenadeAttack(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_A110
  void JumpPointFind(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_9F24
  void JetBoost(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_9BB4
  void PathFind(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_9AAC
  void Patrol(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_996C
  void FollowAttackPattern(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_961C
  void WarpIn(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_9064
  void WarpOut(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_8DF0
  void PostWarpOut(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_8D28
  void JumpBack(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_8B70
  void Dodge(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_8934
  void ArmShield(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_86B4
  void ShieldCharge(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_8028
  void ScriptedShieldCharge(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_7A98
  void RestoreOrientation(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_786C
  void Crouch(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_7724
  void WallHang(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_7358
  void WallDetach(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_728C
  void CoverFind(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_709C
  void SetCoverDest(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_A7A8
  void Cover(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_6DBC
  void CoverAttack(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_6C68
  void BreakCover(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_6BCC
  void GetUp(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_6A40
  void Dead(CStateManager& mgr, EStateMsg msg, float dt);  // fn_9_69C8
#endif

private:
  void EnsureBodyActive(CStateManager& mgr);

  // fn_9_3CBC: the target's aim position - the guest's own helper, called by FaceTarget.
  bool x3CBC_(CStateManager& mgr, CVector3f& out, float dt) const;

  // fn_9_3480: the melee-attack picker - the row of the attack table, or -1.
  int x3480_PickMeleeAttack(CStateManager& mgr) const;

  // fn_9_19D8: the module's team helper - the commando joins the team of its area.
  void x19D8_JoinTeam(CStateManager& mgr);

  // fn_9_662C: the module's per-frame helper - it advances the module's timers and flag bits.
  void x662C_UpdateTimers(CStateManager& mgr, float dt);

  // fn_9_3338: the module's jump-back helper - is the point behind the actor clear?
  bool x3338_IsJumpBackClear(CStateManager& mgr, const CVector3f& dir, float distance) const;

  // fn_9_3678: the module's dodge picker - which side (left or right) can the commando dodge into?
  int x3678_PickDodgeDirection(CStateManager& mgr) const;

  // fn_9_4BC0: the module's sound emitters - the two handles the guest keeps at guest this+0xC04
  // and this+0xC38 follow the actor.
  void x4BC0_UpdateEmitters();

  // fn_9_1C0C: the module's renderable driver - it advances the module's own scale and drives
  // the module's two own renderables.
  void x1C0C_UpdateRenderables(CStateManager& mgr, float dt);

  // fn_9_3E1C: the module's rag-doll driver - with a rag doll it primes and steps it, then it
  // runs the drowning timer, which creates the rag doll when it runs out. The Think passes
  // whether the rag doll was null when it started (the guest's third argument).
  void x3E1C_UpdateRagDoll(CStateManager& mgr, float dt, bool noRagDoll);

  // fn_9_5410: the E-grenade's own creator - the kUE_Projectile event's helper
  // (PortCCommandoPirateCore.cpp).
  void x5410_CreateEGrenade(CStateManager& mgr);

  // fn_9_1F88: the kUE_Activate event's helper (jump-table entry 29) - the shield's sound emitter
  // and the module's state word and scale.
  void x1F88_EventStart();

  // fn_9_1EF8: the kUE_Deactivate event's helper (entry 30) - the counterpart of fn_9_1F88.
  void x1EF8_Activate();

  // fn_9_57A0: the module's shot - the burst fire's projectile, aimed at the target.
  bool x57A0_FireShot(CStateManager& mgr, float dt);

  // fn_9_3090: the module's drowning - the commando dies when its head is under water.
  void x3090_Drown(CStateManager& mgr);

  // fn_9_4D84: the module's aim helper - the additive aim command and the aim direction.
  void x4D84_UpdateAim(CStateManager& mgr);

  // fn_9_4CA4: the module's helper the aim helper asks first - may the actor aim now?
  bool x4CA4_CanAim();

  // fn_9_1A9C: the module's own helper, called by the PathFind state after the path find
  // navigation runs. It walks the listening-AI object list and steers away from the other
  // listening AIs of the same area.
  void x1A9C_AvoidActors(CStateManager& mgr);

  // fn_9_45A4: the module's own collision actor managers - the shield's (guest 0xA3C, the
  // "Shield_CollisionActor") and the blade's (guest 0xA40, the "Blade_CollisionActor"). The
  // guest builds them in its kSM_Create case; the host class CCollisionActorManager covers it.
  void x45A4_CreateCollisionActors(CStateManager& mgr);

  // fn_9_1930: the module's team helper - it leaves the team the commando joined.
  void x1930_LeaveTeam(CStateManager& mgr);

  // fn_9_3D64: the module's "warp out" helper - it enters the WarpOut state of the state
  // machine and sets the module's flag byte's 0x10 bit.
  void x3D64_SetWarpOut(CStateManager& mgr, bool warpOut);

  // fn_9_2D0C: the module's knock-back helper (0x384 bytes) - the shield's and the blade's
  // damage and knock-back.
  void x2D0C_KnockBack(CStateManager& mgr, const CScriptMsg& msg);

  // fn_9_2A98: the module's waypoint picker - the AI waypoint it follows and the waypoint it
  // walks to.
  bool x2A98_PickWaypoint(CStateManager& mgr, bool useNext);

  // fn_9_2474: the module's turn helper - it turns the commando towards the waypoint it follows
  // and charges at it.
  void x2474_TurnToWaypoint(CStateManager& mgr, float dt);

  // fn_9_2048: the module's shield effect - the explosion and the sound the shield makes when
  // it takes a hit.
  void x2048_ShieldEffect(CStateManager& mgr);

  // fn_9_3A5C: the module's hit-object helper (0x260 bytes) - the shield's damage on the
  // player that touched it, and the blade's damage when the blade is out.
  void x3A5C_HitObject(CStateManager& mgr, const CScriptMsg& msg);

  // fn_9_3900: the module's jump-back velocity helper (0x15c bytes) - the ballistic velocity
  // towards the module's own jump target.
  void x3900_JumpBackVelocity(CStateManager& mgr);

  // fn_9_6390: the module's burst-fire helper - the shot timer, the burst fire and the
  // intra-burst shot time.
  void x6390_UpdateBurstFire(CStateManager& mgr, float dt);

  // Host layout. The guest offsets are the REL's; the host layout is its own and does not need to
  // match (host objects outgrow retail). A raw member is named after the offset it stands for
  // (the module's own members are the ones from guest +0x7C0 on; the base's members - the
  // animation state at guest +0x6B4, the body controller, the colour - are the base's own), a
  // structured one after its role; every comment carries the guest offset. The guest member map is
  // in the analysis note.
#ifdef TARGET_PC
  CCommandoPirateData mData;             // 0x7c0 (0x15e), the loader's parameter block
  int x920_;                              // 0x920, -1 from the ctor; the module's states rewrite it
  int x924_;                              // 0x924, -1 (fn_9_D904 @0x0000D9A4)
  CPathFindSearch mPathFindSearch;        // 0x928 (fn_9_D904 @0x0000D990)
  CProjectileInfo mProjectileInfo;        // 0xa14 (fn_9_D904 @0x0000D9B8)
  // The module's two collision actor managers (fn_9_45A4): the shield's at guest 0xA3C and the
  // blade's at guest 0xA40. The guest's constructor leaves both null (fn_9_D904 @0x0000D9D0 and
  // @0x0000D9D8); its kSM_Create case builds them.
  rstl::single_ptr< CCollisionActorManager > mCollisionShield; // 0xa3c, "Shield_CollisionActor"
  rstl::single_ptr< CCollisionActorManager > mCollisionBlade;  // 0xa40, "Blade_CollisionActor"
  CDamageVulnerability mShieldVulnerability; // 0xa44 (fn_9_D904 @0x0000D9D4)
  CBoneTracking mBoneTracking;            // 0xa74 (fn_9_D904 @0x0000DA08)
  // 0xab0 (fn_9_D904 @0x0000DA64): the line of sight to the target. Its flag byte at guest 0xAE8
  // (the class's +0x38, bit 0x80) is the visibility HasLineOfSight and SetTargetDest read.
  CVisualKnowledge mVisualKnowledge;
  CBurstFire mBurstFire;                  // 0xaf0, the module's burst fire (fn_9_D904 @0x0000DA78)
  float xb7c_;                            // 0xb7c, the cooldown since the last melee attack
  float xb80_;                            // 0xb80, the shield's arm timer; fn_9_86B4 advances it
  float xb5c_;                            // 0xb5c, 0.f from the ctor (fn_9_D904 @0x0000DABC)
  uint xb50_;                             // 0xb50, the number of shots; fn_9_6390 raises it
  uint xb54_;                             // 0xb54, the number of shield charges; the charge states raise it
  uint xb58_;                             // 0xb58, a counter; fn_9_6390 raises it
  float xb60_;                            // 0xb60, 1.5f from the ctor (fn_9_D904 @0x0000DACC)
  float xb64_;                            // 0xb64, 0.75f (rodata_4E0) from the ctor; Dead rewrites
  float xb68_;                            // 0xb68, a timer; fn_9_662C advances it
  float xb6c_;                            // 0xb6c, a timer; fn_9_662C advances it
  float xb70_;                            // 0xb70, a timer; fn_9_662C advances it
  float xb74_;                            // 0xb74, a timer; fn_9_662C advances it
  float xb78_;                            // 0xb78, a timer; fn_9_662C advances it
  float xb84_;                            // 0xb84, a timer; fn_9_662C advances it
  float xb88_;                            // 0xb88, the time to the waypoint; fn_9_2A98 sets it
  float xb8c_;                            // 0xb8c, 0.f from the ctor (fn_9_D904 @0x0000DB08)
  float xb90_;                            // 0xb90, 0.f from the ctor (fn_9_D904 @0x0000DB0C)
  CVector3f xb94_;                         // 0xb94, the target's position; zero from the ctor
  CVector3f xba0_;                         // 0xba0, the charge direction; fn_9_8028 sets it
  CVector3f xbac_;                         // 0xbac, the waypoint's up column; fn_9_2A98 sets it
  CVector3f xbb8_;                         // 0xbb8, the fourth vector; fn_9_BF44 stores a normal
  float xbec_;                            // 0xbec, a float of the jump-back helper fn_9_3900
  CVector3f xbf0_;                         // 0xbf0, the jump-back target; fn_9_3900 reads it
  int xc4c_;                               // 0xc4c, the module's own state word; -1 when unset
  float xc44_;                            // 0xc44, the module's own scale; fn_9_1F88 starts it
  float xc48_;                            // 0xc48, the scale's target; fn_9_1F88 sets it
  float xc50_;                            // 0xc50, a counter; fn_9_662C lowers it
  float xc00_;                            // 0xc00, a float; fn_9_3090 sets 0.01f
  // 0xbfc: the module's own rag doll, module 50's CPirateRagDoll (0x11c bytes); fn_9_3E1C
  // creates it, the constructor leaves it null (0x0000DC18).
  rstl::single_ptr< CPirateRagDoll > xbfc_ragDoll;
  TUniqueId xbca_;                       // 0xbca, the target id; kInvalidUniqueId from the ctor
  TUniqueId xbc4_teamId;                 // 0xbc4, the team id; fn_9_19D8 sets it
  TUniqueId xbc6_;                       // 0xbc6, the shield collision actor's id; fn_9_45A4 sets it
  TUniqueId xbc8_;                       // 0xbc8, the blade collision actor's id; fn_9_45A4 sets it
  TUniqueId xbcc_;                       // 0xbcc, the id HasNewTarget compares with; kInvalid
  TUniqueId xbce_;                       // 0xbce, the id ShouldRetreat compares with; kInvalid
  int xbdc_;                             // 0xbdc, the dodge id; fn_9_8934 sets it
  float xbe0_;                           // 0xbe0, the jump-back distance; fn_9_D904 @0x0000E61C
  float xbe4_;                           // 0xbe4, a distance; fn_9_D904 @0x0000E56C
  float xbe8_;                           // 0xbe8, the jump distance; fn_9_D904 @0x0000E4D4
  TUniqueId xbd0_;                       // 0xbd0, kInvalidUniqueId from the ctor (@0x0000DBC0)
  TUniqueId xbd2_;                       // 0xbd2, kInvalidUniqueId from the ctor (@0x0000DBC8)
  TUniqueId xbd4_;                       // 0xbd4, the cover point the actor holds; fn_9_709C
  TUniqueId xbd6_;                       // 0xbd6, the waypoint the WallHang state finds
  TUniqueId xbd8_;                       // 0xbd8, the id the delete case asks to delete
  // Guest this+0x6C0 is the waypoint navigation's own destination field (the navigation sits at
  // guest this+0x6B8 and its destination is at +0x8), so the port uses the host's
  // CWaypointNavigation::GetDestination()/SetDestination() for it and has no member of its own.
  TUniqueId xbda_;                       // 0xbda, the waypoint the commando is at
  CSfxHandle xc04_;                      // 0xc04, the first sound emitter's handle; fn_9_4BC0
  CSfxHandle xc38_;                      // 0xc38, the second sound emitter's handle; fn_9_4BC0
  CSegId mLocatorHead;                    // 0xc5c, "Head_1" (fn_9_D904 @0x0000E29C)
  CSegId mLocatorMidLaunch;               // 0xc5d, "Mid_Launch_LCTRL" (fn_9_D904 @0x0000E2D0)
  CSegId mLocatorGun;                     // 0xc5e, "gun_LCTR" (fn_9_D904 @0x0000E304)
  CSegId mLocatorGun2;                    // 0xc5f, "gun_LCTR" (fn_9_D904 @0x0000E338)
  CSegId mLocatorRWrist;                  // 0xc60, "R_wrist" (fn_9_D904 @0x0000E36C)
  CSegId mLocatorRElbow;                  // 0xc61, "R_elbow" (fn_9_D904 @0x0000E3A0)
  CSegId mLocatorLWrist;                  // 0xc62, "L_wrist" (fn_9_D904 @0x0000E3D4)
  float xc58_;                            // 0xc58, the animation time the WarpOut fade divides by
  uchar xc63_;                            // 0xc63, cleared by the ctor; the states set its bits
  uchar xc64_;                            // 0xc64, cleared (fn_9_D904 @0x0000E150)
  uchar xc65_;                            // 0xc65, 0x80 (fn_9_D904 @0x0000E1B0)
  uchar xc66_;                            // 0xc66, cleared (fn_9_D904 @0x0000E1E8)
  // core (append-only; PortCCommandoPirateCore.cpp)
  int xc54_;                              // 0xc54, the melee attack index; -1 (fn_9_D904 @0x0000E0CC)
  // 0xc3c / 0xc40: the shield's two particle systems, the arm effect (the block's mShield
  // mArmEffect, guest this+0x914) and the charge effect (mChargeEffect, guest this+0x910); the
  // ctor makes them with fn_9_F58C (@0x0000E090 and @0x0000E09C), fn_9_1C0C drives them and
  // fn_9_C598 draws the one the state word at 0xc4c picks.
  rstl::single_ptr< CElementGen > xc3c_armEffect;
  rstl::single_ptr< CElementGen > xc40_chargeEffect;
  // fn_9_F58C: a CElementGen for a PART id, or null for kInvalidAssetId (PortCCommandoPirateCore.cpp)
  static CElementGen* xF58C_CreateEffect(CAssetId id);
  void x1C0C_DriveEffect(CElementGen& effect, float dt);
  // fn_9_52BC / fn_9_5008: the E-grenade's aim point and its launch angle and speed.
  CVector3f x52BC_GetGrenadeTarget(const CStateManager& mgr, const CActor& target) const;
  void x5008_SolveGrenadeLaunch(const CVector3f& target, const CVector3f& origin, float& angle,
                                float& speed) const;
  // cp4: the ctor's three locked 'PART' tokens (fn_9_D904 @0x0000DC24..0x0000E080), each an
  // optional TLockedToken<CGenDescription> (0x10 bytes, the "present" byte at +0xC) that is only
  // filled for a valid asset id: 0xc08 the block's mShield.mExplodeEffect (guest +0x8F0),
  // 0xc18 its mShield.mArmExplodeEffect (guest +0x90C), 0xc28 mAlwaysFF2 (guest +0x7C8).
  rstl::optional_object< TLockedToken< CGenDescription > > xc08_explodeEffect;
  rstl::optional_object< TLockedToken< CGenDescription > > xc18_armExplodeEffect;
  rstl::optional_object< TLockedToken< CGenDescription > > xc28_drownEffect;
  // The ctor's inlined pieces, split out so a forcing run can reach both arms (fn_9_D904).
  static rstl::optional_object< TLockedToken< CGenDescription > > xD904_LockEffect(CAssetId id);
  void xD904_SetChargeTime(float backUpSpeed);

public:
  // vtable slot 0xc8 = fn_9_68, slot 0xb4 = fn_9_AC (PortCCommandoPirateCore.cpp)
  uchar GetModelAlphau8(const CStateManager& mgr) const override;
  CVector3f GetOrigin(const CStateManager& mgr, const CTeamAiRole& role,
                      const CVector3f& aimPos) const override;
#else
  uchar x7c0_[0xc68 - 0x7c0];
#endif
};
CHECK_SIZEOF(CCommandoPirate, 0xc68)

#endif
