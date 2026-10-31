#ifndef _CINGMINIPORTALATTACK
#define _CINGMINIPORTALATTACK

#include "types.h"

#include "Kyoto/TToken.hpp"
#include "MetroidPrime/CActor.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#ifdef TARGET_PC
#include "MetroidPrime/CRELFileToken.hpp"
#endif
#include "MetroidPrime/ScriptLoader/Structs/SLdrPlasmaBeamInfo.hpp"
#ifdef TARGET_PC
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"
#endif

class CGenDescription;

#ifdef TARGET_PC
// The Ing REL's CIngMiniPortalInfo (module 29, ctor fn_29_12100, copy ctor fn_29_1201C): what one
// mini portal carries. Retail's is 0x88 bytes. The offsets in the comments are the REL's, from those
// two functions; the host members are wider here (a TLockedToken is two pointers), so the host class
// is larger than 0x88 - nothing outside the REL reads it by offset.
class CIngMiniPortalInfo {
public:
  CIngMiniPortalInfo(TUniqueId target, float x4, float x8,
                     const TLockedToken< CGenDescription >& effect, ushort sound, float x1c,
                     float x20, const CDamageInfo& damage, const SLdrPlasmaBeamInfo& beamInfo);
  CIngMiniPortalInfo(const CIngMiniPortalInfo& other);

  TUniqueId GetTarget() const { return mTarget; }
  float GetAimTime() const { return mX4; }
  float GetLifeTime() const { return mX8; }
  const TLockedToken< CGenDescription >& GetEffect() const { return mEffect; }
  ushort GetSound() const { return mSound; }
  float GetSfxMaxDist() const { return mX1c; } // 0x1c: the emitter's max distance [? name]
  float GetSfxFalloff() const { return mX20; } // 0x20: the emitter's distance compensation [? name]
  const CDamageInfo& GetDamage() const { return mDamage; }
  const SLdrPlasmaBeamInfo& GetBeamInfo() const { return mBeamInfo; }

private:
  TUniqueId mTarget;   // 0x00: the player the portal aims at (fn_29_11CA4 looks it up)
  float mX4;           // 0x04: how long the portal aims before it fires [? name, time read at 0x15c]
  float mX8;           // 0x08: the portal's lifetime [? name, time read at 0x160]
  TLockedToken< CGenDescription > mEffect; // 0x0c (0x0c): the portal's particle generator
  ushort mSound;                          // 0x18
  float mX1c;                             // 0x1c [? name]
  float mX20;                             // 0x20 [? name]
  CDamageInfo mDamage;                    // 0x24 (0x1c), the beam's damage
  SLdrPlasmaBeamInfo mBeamInfo;           // 0x40 (0x48), copied word for word by fn_29_E9E8
};

// The Ing REL's CIngMiniPortalAttack (fn_29_11E8C ctor, fn_29_114B0 dtor): the actor a mini portal
// spawns; it aims at its target for mInfo's first time, fires a plasma beam (fn_29_11840) and deletes
// itself when the target or the lifetime runs out (fn_29_11CA4). Retail's is 0x238 bytes (the CActor
// base is 0x158). The REL's own vtable is lbl_29_data_CE4; its overrides are Think (retail slot
// 0x14, fn_29_11CA4) and AcceptScriptMsg (slot 0x18, fn_29_11C14), plus slot 0x3C (fn_29_90), which
// only forwards to slot 0x38 - that pair is CActor::GetHealthInfo/HealthInfo, and the host base
// already writes GetHealthInfo as `return HealthInfo()`, so it needs no override here. The ctor's
// `owner` id ends up in CActor's nextDrawNode (retail 0x12a). fn_29_114B0 destroys those members in
// reverse and resets the vtable, which is what the implicit destructor does.
class CIngMiniPortalAttack : public CActor {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CIngMiniPortalAttack(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                       const CTransform4f& xf, TUniqueId owner, const CVector3f& scale,
                       const CIngMiniPortalInfo& portalInfo);

  // CActor
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

private:
  // fn_29_11A7C: the portal "opening" on kSM_Create - the CExplosion, its sound emitter, the shot.
  void Open(CStateManager& mgr);
  // fn_29_11840: aim at mInfo's target and spawn the one CPlasmaProjectile this portal fires.
  void Fire(CStateManager& mgr);
  // fn_29_11570: steer mAimPoint towards the target's aim position, at most 70 degrees a frame.
  void AimAtTarget(CStateManager& mgr, const CActor& target);

  // The host layout (retail offsets in the comments, from fn_29_11E8C).
  CIngMiniPortalInfo mInfo;        // 0x158 (0x88)
  CProjectileInfo mProjectileInfo; // 0x1e0 (0x28): the beam, from mInfo's weapon and damage
  CVector3f mScale;                // 0x208: the ctor's scale argument
  CVector3f mAimPoint;             // 0x214: sZeroVector; fn_29_11CA4 looks at it from 0x54
  float mTime;                     // 0x220: 0 (rodata_7E0); fn_29_11CA4 adds dt
  TUniqueId mProjectileId;         // 0x224: kInvalidUniqueId; fn_29_11840 stores the fired beam
  CRELFileToken mRelFile;          // 0x228: CRELFileToken("Grenchler.rel", 0), see the ctor
  uchar mFlags;                    // 0x230: bit 7 set once the portal has aimed at its target
};
// Retail size; inert off Metrowerks, and the host members are wider.
#else
// Original Wii export name; the class lives in the Ing REL (module 29), which is not ported. Only
// the constructors used by the IngSpaceJumpGuardian REL are declared, and the layout is not
// reconstructed.
class CIngMiniPortalInfo {
public:
  CIngMiniPortalInfo(TUniqueId target, float x4, float x8,
                     const TLockedToken< CGenDescription >& effect, ushort sound, float x1c,
                     float x20, const CDamageInfo& damage, const SLdrPlasmaBeamInfo& beamInfo);

private:
  TUniqueId mTarget; // Guessed name
  float x4_;
  float x8_;
  TLockedToken< CGenDescription > mEffect; // Guessed name
  ushort mSound;                           // Guessed name
  float x1c_;
  float x20_;
  CDamageInfo mDamage;          // Guessed name
  SLdrPlasmaBeamInfo mBeamInfo; // Guessed name
};

// Original Wii export name; the actor that is spawned by a mini portal attack.
class CIngMiniPortalAttack : public CActor {
public:
  CIngMiniPortalAttack(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                       const CTransform4f& xf, TUniqueId owner, const CVector3f& scale,
                       const CIngMiniPortalInfo& portalInfo);

private:
  uchar mUnported[0x238 - sizeof(CActor)]; // Guessed layout; defined by the Ing REL.
};
#endif
CHECK_SIZEOF(CIngMiniPortalAttack, 0x238)

#endif // _CINGMINIPORTALATTACK
