#ifndef _CEMPERORINGLIGHTSWARM
#define _CEMPERORINGLIGHTSWARM

// Port-only: the EmperorIngStage3 REL's light swarm (module 18). Upstream has no source for it;
// the retail class is 0x618 bytes: a CSwarmBasics (0x598) whose boids all follow one point that
// homes in on the player. Constructor fn_18_BD84, destructor fn_18_DA14, vtable lbl_18_data_798;
// fn_18_BB8 spawns it at the Ing's "Swarm_LCTR_SDK" locator. The guest offsets are in the member
// comments; the host class has its own layout.

#include "types.h"

#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/Enemies/CSwarmBasics.hpp"
#include "MetroidPrime/ScriptLoader/Structs/SLdrAudioPlaybackParms.hpp"

#include "Kyoto/TToken.hpp"

#include "rstl/optional_object.hpp"
#include "rstl/single_ptr.hpp"

class CEmperorIngLightSwarm : public CSwarmBasics {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  // fn_18_BD84. The death sound and the second sound come on the stack.
  CEmperorIngLightSwarm(TUniqueId uid, const CEntityInfo& info, const CActorParameters& params,
                        const CBasicSwarmData& data, CAssetId particleId, TUniqueId ownerId,
                        const CTransform4f& xf, const SLdrAudioPlaybackParms& deathSound,
                        const SLdrAudioPlaybackParms& expireSound);
  ~CEmperorIngLightSwarm() override; // fn_18_DA14

  // CEntity
  void Think(float dt, CStateManager& mgr) override;                        // fn_18_B254
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override; // fn_18_B1B0

  // CActor
  void Render(const CStateManager& mgr) const override;                  // fn_18_AF9C
  bool CanRenderUnsorted(const CStateManager& mgr) const override;       // fn_18_AF94
  void PreRenderAllViewports(CStateManager& mgr) override;               // fn_18_B0D0
  CHealthInfo* HealthInfo() override;                                    // fn_18_DAE4
  const CHealthInfo* GetHealthInfo() const override;                     // fn_18_AEAC
  rstl::optional_object< CAABox > GetTouchBounds() const override;       // fn_18_AE38

  // CSwarmBasics
  CAABox GetBoundingBox() const override;                                // fn_18_AEB4
  void CreateBoid(CStateManager& mgr, int index) override;               // fn_18_BC1C
  void PreRenderBoid(CBoid* boid, uint* drawMask) override;              // fn_18_B618
  void UpdateBoid(CAreaCollisionCache& cache, CStateManager& mgr, float dt, CBoid& boid,
                  int partitionIndex) override;                          // fn_18_B884
  bool GetLockOnLocationValid(int index) const override;                 // fn_18_B588
  CVector3f GetLockOnLocation(int index) const override;                 // fn_18_B5CC
  void RenderBoid(CBoid* boid) const override;                           // fn_18_B614
  void UpdateAllBoidMovement(CStateManager& mgr, float dt) override;     // fn_18_B7A8

private:
  bool IsDying() const;                                                          // fn_18_B61C
  void MoveSwarmBoid(CStateManager& mgr, CBoid& boid, const CVector3f& delta, float dt); // B694

  TUniqueId mOwnerId;                                                    // guest 0x598
  rstl::optional_object< TLockedToken< CGenDescription > > mEffectDesc;  // guest 0x59C
  rstl::single_ptr< CElementGen > mEffect;                               // guest 0x5AC
  CVector3f mDirection;                                                  // guest 0x5B0
  CHealthInfo mOwnHealth;                                                // guest 0x5BC
  float mDyingTime;                                                      // guest 0x5DC
  float mAge;                                                            // guest 0x5E0
  SLdrAudioPlaybackParms mDeathSound;                                    // guest 0x5E4
  SLdrAudioPlaybackParms mExpireSound;                                   // guest 0x5FC
  float mDamageTime;                                                     // guest 0x614
};

// fn_18_817C: the Ing's "Swarm_LCTR_SDK" locator (rodata 0xAC -> 0x3F2) in world space.
CTransform4f EmperorIngSwarmLocator(const CActor& owner);

#endif // _CEMPERORINGLIGHTSWARM
