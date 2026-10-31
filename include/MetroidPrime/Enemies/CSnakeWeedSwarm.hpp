#ifndef _CSNAKEWEEDSWARM
#define _CSNAKEWEEDSWARM

#include "MetroidPrime/CActor.hpp"

#ifdef TARGET_PC
#include "Kyoto/Animation/CSkinnedModel.hpp"
#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "Kyoto/Math/CVector2i.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "rstl/rc_ptr.hpp"
#include "rstl/reserved_vector.hpp"
#include "rstl/single_ptr.hpp"
#include "rstl/vector.hpp"

class CAnimRes;
#endif

// Original class name from the Wii SEL exports. The class lives in the SnakeWeedSwarm REL; the DOL
// only proves the CActor base through its TypesMatch parent, so the layout is left opaque.
class CSnakeWeedSwarm : public CActor {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
#ifdef TARGET_PC
  // The REL's CSnakeWeedSwarm (module 71), ported from MP1's CSnakeWeedSwarm - see
  // docs/module-analysis/SnakeWeedSwarm.md and PortCSnakeWeedSwarm.cpp. 0x298 bytes on the GameCube
  // (the loader's rs_new size), built by fn_71_2B84: MP1's class on Echoes' 0x158 CActor, every
  // member at MP1's offset plus 0x70. Argument order is the loader's call at 0x000005D0.
  enum ESnakeWeedBoidState { kSWBS_Raised, kSWBS_Raising, kSWBS_Lowered, kSWBS_Lowering };
  enum EBoidPlacement { kBP_None, kBP_Ready, kBP_Invalid, kBP_Placed };

  // 0x24 bytes; the REL's one-instruction accessors fn_71_39B8..fn_71_39FC.
  class CBoid {
    CVector3f x0_pos;
    ESnakeWeedBoidState xc_state;
    float x10_timeOut;
    float x14_depth;
    float x18_speed;
    float x1c_;
    float x20_scale;

  public:
    CBoid(const CVector3f& pos, float depth, float speed, float scale);
    void SetBoidState(ESnakeWeedBoidState state) { xc_state = state; }
    ESnakeWeedBoidState GetBoidState() const { return xc_state; }
    const CVector3f& GetLocation() const { return x0_pos; }
    float GetTimeOut() const { return x10_timeOut; }
    void SetTimeOut(float time) { x10_timeOut = time; }
    float GetDepth() const { return x14_depth; }
    void SetDepth(float depth) { x14_depth = depth; }
    float GetSpeed() const { return x18_speed; }
    void SetSpeed(float speed) { x18_speed = speed; }
    float GetSize() const { return x20_scale; }
  };

  CSnakeWeedSwarm(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                  const CVector3f& pos, const CVector3f& scale, const CAnimRes& animRes,
                  const CActorParameters& actParms, float spacing, float height, float variance,
                  float weaponDamageRadius, float maxPlayerDistance, float loweredTime,
                  float loweredTimeVariation, float maxDepth, float speed, float speedVariation,
                  float slopeAngle, float scaleMin, float scaleMax, float distanceBelowGround,
                  const CDamageInfo& damageInfo, float damageWaitTime, ushort sfxId1,
                  ushort sfxId2, ushort sfxId3);

  // The REL's vtable lbl_71_data_30 names what is overridden (slot numbers count its two header
  // words, as the other module analyses do). Slots 4 (PreThink), 7 (SetActive), 8
  // (ClearFluidList), 11 (Render) and 12.. keep the DOL's base-class symbols.
  void Think(float dt, CStateManager& mgr) override;                        // slot 5, fn_71_1E58
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override; // slot 6, fn_71_274C
  void PreRender(CStateManager& mgr) override;                              // slot 9, fn_71_1934
  void AddToRenderer(const CStateManager& mgr) const override;              // slot 10, fn_71_163C
  rstl::optional_object< CAABox > GetTouchBounds() const override;          // slot 18, fn_71_2494
  void Touch(CActor& actor, CStateManager& mgr) override;                   // slot 19, fn_71_1CC0

  // The two member functions the module hands the DOL through SSnakeWeedSwarm_FuncPtrs
  // (lbl_71_bss_40 +0x4 and +0x10, the pointers-to-member lbl_71_data_18 / lbl_71_data_24).
  void ApplyRadiusDamage(CVector3f pos, const CDamageInfo& info, CStateManager& mgr); // fn_71_1AF8
  void ScareSnakeWeeds(CStateManager& mgr, const CVector3f& pos, float radius);       // fn_71_1B3C
#endif
  // CEntity
  ~CSnakeWeedSwarm() override;
  CEntity* TypesMatch(int typeId) const override;

#ifdef TARGET_PC
private:
  void AllocateSkinnedModels(CStateManager& mgr, CModelData::EWhichModel which); // fn_71_256C
  CAABox GetBoundingBox() const;
  void UpdateTouchBounds();
  void RenderBoid(uint index, const CBoid& boid, uint& posesToBuild,
                  const CModelFlags& flags) const; // fn_71_17C0
  void CreateSwarm(CStateManager& mgr);              // fn_71_133C
  CVector2i GetGridPosition(CVector3f pos);
  int GetGridWidth();
  int GetGridDepth();
  void PushBackPoint(CVector3f pos);
  void FloodFill(CStateManager& mgr, int count); // fn_71_D10
  float GetXVariance(const CVector3f& pos);
  float GetYVariance(const CVector3f& pos);
  bool PlaceSnakeWeedAtPoint(const CVector3f& pos, CStateManager& mgr);

  // Host layout in the guest's order (MP1's names). The guest's particle members (0x268..0x290)
  // are not held: Echoes' loader passes invalid particle ids and a zero count, so MP1's
  // AddContinuousParticles/AddRetreatParticles (fn_71_7F0/fn_71_880) never emit.
  CVector3f xe8_scale;           // 0x158
  float xf4_boidSpacing;         // 0x164
  float xf8_height;
  float xfc_variance;
  float x100_weaponDamageRadius; // 0x170 (Touch's lfs 0x170)
  float x104_maxPlayerDistance;  // 0x174 (Touch's lfs 0x174)
  float x108_loweredTime;
  float x10c_loweredTimeVariation;
  float x110_maxDepth;
  float x114_speed;
  float x118_speedVariation;
  float x11c_cosSlopeAngle;      // 0x18c
  float x120_scaleMin;
  float x124_scaleMax;
  float x128_distanceBelowGround;
  float mDamageWaitTime;         // 0x198 (Echoes-only loader float; MP1's f15, unused there too)
  uint x12c_;                    // 0x19c
  rstl::vector< CBoid > x130_boids; // 0x1a0
  bool x140_24_hasGround : 1;    // 0x1b0 bit 0x80
  bool x140_25_modelAssetDirty : 1;
  uchar mPlayersTouching;        // 0x1b1: one bit per player (MP1's x140_26_playerTouching)
  CAABox x144_touchBounds;       // 0x1b4
  CDamageInfo x15c_damageInfo;   // 0x1cc
  rstl::reserved_vector< CSkinnedModelState, 4 > mModelStates; // MP1's two workspace vectors
  rstl::reserved_vector< rstl::ncrc_ptr< CModelData >, 4 > x1b0_modelData; // 0x22c
  CModelData::EWhichModel x1c4_which; // 0x250
  rstl::single_ptr< rstl::vector< CVector3f > > x1c8_boidPositions;       // 0x254
  rstl::single_ptr< rstl::vector< EBoidPlacement > > x1cc_boidPlacement; // 0x258
  ushort x1d0_sfx1;              // 0x25c looped
  ushort x1d2_sfx2;              // 0x25e into ground
  ushort x1d4_sfx3;              // 0x260 out of ground
  CSfxHandle x1d8_sfxHandle;
  uint mThinkCount;              // port: active updates, for the run log
#endif
};

#endif // _CSNAKEWEEDSWARM
