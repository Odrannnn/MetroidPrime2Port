#ifndef _PORTCBOOSTBALLGUARDIANBALL
#define _PORTCBOOSTBALLGUARDIANBALL

// Port-only: the IngBoostBallGuardian REL's second class (module 30), the rolling ball the Boost
// Guardian turns into for its boost attack. 0x5E0 bytes on the GameCube, a CPhysicsActor built by
// fn_30_15924 (vtable lbl_30_data_C44) and spawned by the guardian's fn_30_3478 under the name
// "BoostBallGuardian". Upstream has no source for it; the map of its functions is
// docs/module-analysis/BoostGuardian.md section f.
//
// The guest offsets quoted are the REL's own; the host layout is its own (the members follow the
// guest order, the flag bytes are one-bit members).

#include "Collision/CCollidableSphere.hpp"
#include "Collision/CCollisionInfoList.hpp"
#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Math/CVector3f.hpp"
#include "Kyoto/Particles/CParticleGen.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CPhysicsActor.hpp"
#include "MetroidPrime/Enemies/CIngBoostBallGuardian.hpp"
#include "MetroidPrime/TCastTo.hpp"

#include "rstl/optional_object.hpp"
#include "rstl/single_ptr.hpp"
#include "rstl/vector.hpp"

class CBoostBallGuardianBall : public CPhysicsActor {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  // The DOL's type tag for this class (TypesMatch.cpp's "148", parent CPhysicsActor); the
  // guardian's BoostAttack casts its child with it (fn_800979F0 = TryCast(entity, 0x94)).
  static const int kTypeId = 148;

  // fn_30_15924. The guest passes the material list and the health info by pointer; either may be
  // null (0x00015998, 0x00015CB8).
  CBoostBallGuardianBall(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                         const CTransform4f& xf, const CVector3f& pos, const CModelData& modelData,
                         const CActorParameters& actParams, TUniqueId ownerId,
                         const CBoostBallGuardianData& data, int phaseIndex,
                         const CDamageVulnerability& vulnerability,
                         const rstl::vector< TUniqueId >& modifyTargets,
                         const CMaterialList* matList, const CHealthInfo* health);
  ~CBoostBallGuardianBall() override;

  // CEntity
  CEntity* TypesMatch(int typeId) const override;
  // @ fn_30_13B7C
  TUniqueId GetLastSender() const { return mId4F6; } // fn_30_13B7C
  void AddToRenderer(const CStateManager& mgr) const override; // fn_30_13E68
  void Render(const CStateManager& mgr) const override;        // fn_30_141B8
  void PreRenderAllViewports(CStateManager& mgr) override;     // fn_30_13ECC
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

  // CActor
  CHealthInfo* HealthInfo() override;
  const CHealthInfo* GetHealthInfo() const override;
  const CDamageVulnerability* GetDamageVulnerability() const override;
  const CDamageVulnerability* GetDamageVulnerability(const CVector3f& pos, const CVector3f& dir,
                                                     const CDamageInfo& info) const override;
  rstl::optional_object< CAABox > GetTouchBounds() const override;
  void Touch(CActor& actor, CStateManager& mgr) override;

  // CPhysicsActor
  const CCollisionPrimitive* GetCollisionPrimitive() const override;
  void CollidedWith(const TUniqueId& id, const CCollisionInfoList& list,
                    CStateManager& mgr) override;

  // fn_30_13B98: the shrink is over (guest 0x5D8 bit 0x04); BoostAttack's Update turns it into
  // the guardian's IsBoostOver.
  // @ fn_30_13B98
  bool IsDone() const { return mDone; }
  int GetState() const { return mState; }    // 0x2D4, read by fn_30_6108
  float GetShrink() const { return mShrink; } // 0x530, read by fn_30_6108
  // BoostAttack's Deactivate sets guest 0x5D8 bit 0x01 (0x00007AC4).
  void SetHalted(bool halted) { mHalted = halted; }

private:
  void GrowIn(float dt, CStateManager& mgr);                       // fn_30_137C0
  void UpdateEffects(float dt, CStateManager& mgr, uint mask);      // fn_30_13C78
  void UpdateScale(float dt, CStateManager& mgr);                   // fn_30_152D4
  void Shrink(float dt);                                            // fn_30_13760
  void Roll(float dt, CStateManager& mgr);                          // fn_30_12AB0
  void ContactSweep(float dt, CStateManager& mgr);                  // fn_30_14CCC
  void UpdateRollingSfx();                                          // fn_30_1282C
  void PlaySound(int sound, bool start);                            // fn_30_12904
  float EvaluateSpeed(float time) const;                            // fn_30_11E4C
  void ApplySpeedImpulse(float delta);                              // fn_30_11E70
  bool IsPathClear(CStateManager& mgr, const CVector3f& dir);       // fn_30_148DC
  bool Steer(CStateManager& mgr, const CVector3f& dir, CVector3f& out, float bias); // fn_30_12124
  bool TurnWithinCone(const CVector3f& cur, const CVector3f& target, float& out) const; // fn_30_123CC
  bool AimAtPlayer(CStateManager& mgr, const CVector3f& cur, CVector3f& out);      // fn_30_11F2C
  bool AimAtCoverPoint(CStateManager& mgr, const CVector3f& cur, CVector3f& out);  // fn_30_12510
  CVector3f FlatDelta(const CVector3f& pos) const;                  // fn_30_127FC

  int mPhaseIndex;                                     // 0x2D0: the guardian's phase at spawn
  int mState;                                          // 0x2D4: 0 grow in, 1 roll, 2 shrink
  CVector3f mHomePos;                                  // 0x2D8
  CBoostBallGuardianData mData;                        // 0x2E4: the guardian's boost data
  CCollidableSphere mSphere;                           // 0x4D0
  TUniqueId mOwnerId;                                  // 0x4F0: the guardian
  TUniqueId mCollidedId;                               // 0x4F2: CollidedWith's actor
  TUniqueId mLastCollidedId;                           // 0x4F4
  TUniqueId mId4F6;                                    // 0x4F6: AcceptScriptMsg's sender (0x000146F8)
  TUniqueId mTouchedId;                                // 0x4F8: Think's damage target
  rstl::vector< TUniqueId > mModifyTargets;            // 0x4FC: the guardian's 0x109C
  float mDamageTimer;                                  // 0x50C, 1.0 (guessed name)
  float mSpeedDelta;                                   // 0x510, 0.0: the spline's last step
  float mSplineDuration;                               // 0x514: the speed spline's duration
  float mHalfHeight;                                   // 0x518: half the sphere's local height
  float mSplineTime;                                   // 0x51C: the roll's time on the spline
  float mShieldTime;                                   // 0x520, 0.4: the shield's fade
  float mRollSpeed;                                    // 0x524, 0.0: the roll's top speed
  float mSplineEndTime;                                // 0x528
  float mStateTime;                                    // 0x52C: zeroed on each state change
  float mShrink;                                       // 0x530, 1.0: 0 grown, 1 shrunk
  CVector3f mScale;                                    // 0x534, (1, 1, 1)
  CVector3f mBounceNormal;                             // 0x540, zero: CollidedWith's normal
  CVector3f mLastVelocity;                             // 0x54C, zero: Think's last velocity
  int mBounces;                                        // 0x558: compared with mBounceCounts[0]
  int mMissBounces;                                    // 0x55C: bounces without a player touch
  int mCoverBounces;                                   // 0x560
  rstl::single_ptr< CParticleGen > mTrailGen;          // 0x564: data+0xA8 trail ('SPSC')
  rstl::single_ptr< CParticleGen > mShieldGen;         // 0x568: data+0xAC shield ('PART')
  rstl::single_ptr< CParticleGen > mChargeUpGen;       // 0x56C: data+0xB0 charge up ('PART')
  CDamageVulnerability mVulnerability;                 // 0x570
  rstl::optional_object< CHealthInfo > mHealthInfo;    // 0x5A0, present flag 0x5C0
  CSfxHandle mRollingSfx;                              // 0x5C4: fn_30_1282C's handle
  rstl::vector< int > mBounceCounts;                   // 0x5C8: per roll, the bounces before the
                                                       // spline runs (kSM_Create fills it)
  bool mDirChosen : 1;                                 // 0x5D8 bit 0x80: GrowIn picked its heading
  bool mRandomDir : 1;                                 // 0x5D8 bit 0x40: set by the first roll; later
                                                       // grow-ins pick a random heading
  bool mEffectBounds : 1;                              // 0x5D8 bit 0x20: the render bounds are
                                                       // the effects' (fn_30_13ECC)
  bool mBounced : 1;                                   // 0x5D8 bit 0x10: CollidedWith hit something
  bool mRolling : 1;                                   // 0x5D8 bit 0x08
  bool mDone : 1;                                      // 0x5D8 bit 0x04
  bool mOnSpline : 1;                                  // 0x5D8 bit 0x02: the speed spline runs
  bool mHalted : 1;                                    // 0x5D8 bit 0x01
  bool m5D9_80 : 1;                                    // 0x5D9 bit 0x80
  bool mGrown : 1;                                     // 0x5D9 bit 0x40
  bool mIntoBallSfx : 1;                               // 0x5D9 bit 0x20, set by the ctor
  bool mLoopSfx : 1;                                   // 0x5D9 bit 0x10, set by the ctor
};

template <>
CBoostBallGuardianBall* TCastToPtr< CBoostBallGuardianBall >(CEntity* entity);

#endif // _PORTCBOOSTBALLGUARDIANBALL
