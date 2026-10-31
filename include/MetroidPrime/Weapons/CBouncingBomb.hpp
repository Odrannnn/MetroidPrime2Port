#ifndef _CBOUNCINGBOMB
#define _CBOUNCINGBOMB

#include "MetroidPrime/Weapons/CWeapon.hpp"

#include "rstl/single_ptr.hpp"

class CElementGen;
class CGenDescription;
class CRayCastResult;

class CBouncingBomb : public CWeapon {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CBouncingBomb(TToken< CGenDescription > particle, TToken< CGenDescription > explosion,
                TUniqueId uid, TAreaId areaId, TUniqueId ownerId, float fuseTime, float touchRadius,
                EWeaponType type, uint attribs, const CTransform4f& xf,
                const CDamageInfo& damageInfo, float renderRadius, ushort placementSfx,
                ushort bounceSfx, ushort explosionSfx, float gravityScale, float bounceRestitution);

  // CEntity
  ~CBouncingBomb() override;
  CEntity* TypesMatch(int typeId) const override;
  void Think(float dt, CStateManager& mgr) override;

  // CActor
  void AddToRenderer(const CStateManager& mgr) const override;
  void Render(const CStateManager& mgr) const override;
  rstl::optional_object< CAABox > GetTouchBounds() const override;
  void Touch(CActor& actor, CStateManager& mgr) override;

  void ApplyGravity();
#ifdef TARGET_PC
  // Retail CSandworm writes +0x1D0 directly (fn_56_8E90/91D4/9440).
  void SetVelocity(const CVector3f& v) { mVelocity = v; }
#endif

private:
  // Guessed names.
  void UpdateParticles(float dt);
  void UpdateExplosion(float dt, CStateManager& mgr);
  void Explode(CStateManager& mgr);
  void HandleStaticCollision(CStateManager& mgr, const CRayCastResult& result);

  // Guessed member names.
  rstl::single_ptr< CElementGen > mParticle;
  rstl::single_ptr< CElementGen > mExplosionParticle;
  CVector3f mVelocity;
  CVector3f mAcceleration;
  CVector3f mPrevLocation;
  ushort mPlacementSfx;
  ushort mBounceSfx;
  ushort mExplosionSfx;
  float mFuseTime;
  float mTouchRadius;
  float mRenderRadius;
  float mGravityScale;
  float mBounceRestitution;
  float mExplosionElapsed;
  uchar mBounceCount;
  bool mIsNotDetonated : 1;
  bool mDisableFuse : 1;
};
CHECK_SIZEOF(CBouncingBomb, 0x218)

#endif
