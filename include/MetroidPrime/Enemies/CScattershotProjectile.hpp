#ifndef _CSCATTERSHOTPROJECTILE
#define _CSCATTERSHOTPROJECTILE

// Port-only header: the Dark Samus REL's scatter shot weapon (vtable lbl_10_data_40, 0x27B0 bytes;
// ctor fn_10_2AD0). One actor carries up to 300 shots that fly at their own targets and are drawn
// as two billboards and a trail each. The names are MP3's (R3ME01 symbols). The bodies are in
// src/MetroidPrime/Enemies/PortCScattershotProjectile.cpp.

#include "Kyoto/Math/CAABox.hpp"
#include "Kyoto/Math/CVector3f.hpp"
#include "Kyoto/TToken.hpp"
#include "MetroidPrime/Weapons/CWeapon.hpp"
#include "rstl/reserved_vector.hpp"

class CRayCastResult;
class CWeaponDescription;

class CScattershotProjectile : public CWeapon {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  // One shot (0x20 bytes, the records at 0x230).
  struct SShot {
    enum EFlags {
      kF_Dead = 0x1,       // stops moving and drawing
      kF_Hit = 0x2,        // reached its target or hit the player
      kF_NeedsSweep = 0x4, // the target is not swept yet (fn_10_1C08)
      kF_HitSfx = 0x8,     // every 17th shot plays the response sound
      kF_HitEffect = 0x10, // every 13th shot spawns the response explosion
      kF_Slow = 0x20,      // every 5th shot flies at 0.67 of the speed
    };
    CVector3f mPos;    // +0x0
    CVector3f mTarget; // +0xC (after a hit: the reversed direction, for the explosion's LookAt)
    float mDistance;   // +0x18
    uchar mFlags;      // +0x1C
  };

  // fn_10_2AD0
  CScattershotProjectile(const TToken< CWeaponDescription >& desc, EWeaponType type,
                         const CTransform4f& xf, const CDamageInfo& damage, TUniqueId uid,
                         TAreaId area, TUniqueId owner, float speed);
  ~CScattershotProjectile() override; // fn_10_29FC

  CEntity* TypesMatch(int typeId) const override;
  void Think(float dt, CStateManager& mgr) override;                        // fn_10_2918
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override; // fn_10_29DC
  void AddToRenderer(const CStateManager& mgr) const override;              // fn_10_E5C
  void Render(const CStateManager& mgr) const override;                     // fn_10_E7C
  rstl::optional_object< CAABox > GetTouchBounds() const override;          // fn_10_C5C

  void AddShot(const CVector3f& pos, const CVector3f& dir); // fn_10_C98

private:
  void CheckPlayerHit(CStateManager& mgr);               // fn_10_2C
  CRayCastResult Sweep(const CVector3f& from, const CVector3f& to,
                       const rstl::reserved_vector< TUniqueId, 1024 >& nearList,
                       CStateManager& mgr) const;         // fn_10_324
  bool CanHit(const CActor& actor) const;                 // fn_10_B78
  void CheckNewShots(CStateManager& mgr);                 // fn_10_1C08
  void SpawnHitExplosion(CStateManager& mgr, const SShot& shot) const; // fn_10_1D8C
  void PlayHitSfx(const CVector3f& pos) const;            // fn_10_213C
  void UpdateShots(CStateManager& mgr, float dt);         // fn_10_2254
  void InitRenderParams();                                // fn_10_2798

  TToken< CWeaponDescription > mDescToken; // 0x1C8
  CWeaponDescription* mDesc;               // 0x1D0
  float mSpeed;                            // 0x1D4
  CAABox mBounds;                          // 0x1D8
  float mLifetime;                         // 0x1F0
  bool mAlive;                             // 0x1F4
  CVector3f mB1Offset;                     // 0x1F8 (B1PO)
  CVector3f mB2Offset;                     // 0x204 (B2PO)
  CVector3f mTrailOffset;                  // 0x210 (TLPO)
  float mB1Size;                           // 0x21C (B1SE)
  float mB2Size;                           // 0x220 (B2SE)
  float mTrailSize;                        // 0x224 (TSZE)
  float mTrailLength;                      // 0x228 (TLEN)
  rstl::reserved_vector< SShot, 300 > mShots; // 0x22C
};

#endif // _CSCATTERSHOTPROJECTILE
