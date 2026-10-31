#ifndef _CDARKSAMUSBOOSTBALL
#define _CDARKSAMUSBOOSTBALL

// Port-only header: the Dark Samus REL's boost ball actor (vtable lbl_10_data_104, 0x358 bytes;
// ctors fn_10_3FD8 / fn_10_3BC4). The bodies are in
// src/MetroidPrime/Enemies/PortCDarkSamusHelpers.cpp.

#include "Kyoto/Graphics/CColor.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CPhysicsActor.hpp"
#include "MetroidPrime/CRELFileToken.hpp"

class CCollisionActorManager;
class CModelData;

class CDarkSamusBoostBall : public CPhysicsActor {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  // fn_10_3FD8 (silhouette false) and fn_10_3BC4 (silhouette true, with the fade and the colour).
  CDarkSamusBoostBall(TUniqueId uid, TAreaId area, const CTransform4f& xf, const CModelData& model,
                      int defaultAnim, const CAABox& bounds, ushort hitSfx,
                      const CDamageInfo& damage, const CDamageVulnerability& vulnerability,
                      bool silhouette, float fade, const CColor& color);
  ~CDarkSamusBoostBall() override; // fn_10_2E64

  void Think(float dt, CStateManager& mgr) override;                        // fn_10_3B08
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override; // fn_10_3998
  void PreRender(CStateManager& mgr) override;                              // fn_10_3174
  void Render(const CStateManager& mgr) const override;                     // fn_10_3008
  CVector3f GetOrbitPosition(const CStateManager& mgr) const override;      // fn_10_3934
  CVector3f GetAimPosition(const CStateManager& mgr, float dt) const override; // fn_10_38D0

  float GetDamageTaken() const;  // fn_10_2EFC: 0x314
  bool HasBeenDamaged() const;   // fn_10_2F04: 0x318 bit 0x40

private:
  void CreateCollisionActors(CStateManager& mgr);           // fn_10_31E4
  void ApplyTouchDamage(CStateManager& mgr);                // fn_10_2F10

  CAABox mBounds;                     // 0x2D0, the Dark Samus's touch bounds (never read)
  CColor mSilhouetteColor;            // 0x2E8
  float mSilhouetteFade;              // 0x2EC
  ushort mHitSfx;                     // 0x2F0
  CDamageInfo mDamage;                // 0x2F4
  float mHitCooldown;                 // 0x310
  float mDamageTaken;                 // 0x314
  bool mSilhouette : 1;               // 0x318 0x80 (`rlwimi` at 0x00003EC4, `extrwi r0, r0, 1, 24`)
  bool mDamaged : 1;                  // 0x318 0x40 (`rlwimi` at 0x00003ED0, `extrwi r3, r0, 1, 25`)
  CDamageVulnerability mVulnerability; // 0x31C (copied, never read)
  CRELFileToken mRelToken;            // 0x34C, "DarkSamus.rel" (lbl_10_bss_5C), mode 0
  CCollisionActorManager* mCollisionManager; // 0x354
};

#endif // _CDARKSAMUSBOOSTBALL
