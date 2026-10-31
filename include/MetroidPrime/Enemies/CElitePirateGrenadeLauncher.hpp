#ifndef _CELITEPIRATEGRENADELAUNCHER
#define _CELITEPIRATEGRENADELAUNCHER

#include "MetroidPrime/CActor.hpp"
#include "MetroidPrime/CActorParameters.hpp"
#include "MetroidPrime/CRELFileToken.hpp"

class CScriptAIHint;

// Gun turret of the Elite Pirate. Prime's CGrenadeLauncher is the closest relative.
class CElitePirateGrenadeLauncher : public CActor {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CElitePirateGrenadeLauncher(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                              const CTransform4f& xf, const CModelData& mData,
                              const CActorParameters& actParams, TUniqueId parentId, float f);

  // CEntity
  ~CElitePirateGrenadeLauncher() override;
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

  // CActor
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  void Render(const CStateManager& mgr) const override;

  void SetSlowedSpeed(float speed);
#ifdef TARGET_PC
  // Inline stores in the Ingsmasher's Think (REL 15, fn_15_9598) and rocket code (fn_15_728C).
  void SetDamageAddColor(const CColor& color) { mDamageAddColor = color; } // 0x1f8
  void SetFollowPlayer(bool follow) { mFollowPlayer = follow; }            // 0x200 bit 0x20
  const CTransform4f& GetTurretTransform() const { return mTurretTransform; } // 0x204
#endif

private:
  bool IsTrackingPlayer() const;
  void UpdateGunTracking(float dt, CStateManager& mgr);
  bool IsNearHint(const CStateManager& mgr, const CVector3f& pos) const;
  void LaunchGrenadeProjectile(CStateManager& mgr);
  void UpdateLauncherAnimation();

  int mPlayerIndex;
  int mStarted;
  TUniqueId mParentId;
  float x164_;
  CColor x168_;
  CActorParameters mGrenadeActorParams;
  int mAnimIds[4];
  float mYaw;
  float mYawVelocity;
  float mPitch;
  float mPitchVelocity;
  float mSlowedSpeed;
  float mThermalMag;
  CColor mDamageColor;
  CColor mDamageAddColor;
  float x1fc_;
  bool mLaunchGrenade : 1;
  bool mVisible : 1;
  bool mFollowPlayer : 1;
  CTransform4f mTurretTransform;
  CRELFileToken mRelToken;
};

#endif // _CELITEPIRATEGRENADELAUNCHER
