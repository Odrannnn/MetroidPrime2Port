#ifndef _CSCRIPTPLAYERTURRET
#define _CSCRIPTPLAYERTURRET

#include "MetroidPrime/CActor.hpp"
#ifdef TARGET_PC
#include "Kyoto/Animation/CPASAnimParm.hpp"
#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "Kyoto/CToken.hpp"
#include "Kyoto/Graphics/CColor.hpp"
#include "Kyoto/Graphics/CModelFlags.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "Kyoto/SObjectTag.hpp"
#include "MetroidPrime/CDamageInfo.hpp"

class CFinalInput;
#endif

// Partial layout of the REL player turret object, covering only the fields the DOL reads
// directly. Guessed class and member names; the gaps are unidentified.
class CScriptPlayerTurret : public CActor {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  // CEntity
  ~CScriptPlayerTurret() override;
  CEntity* TypesMatch(int typeId) const override;
#ifdef TARGET_PC
  // The module's vtable slot 15 (fn_63_0): the guest's thunk forwards to the vtable's slot 12
  // (the base's HealthInfo), so the port forwards to the same virtual call.  The guest has its own
  // thunk because the module's vtable is the linker's own; the host's CEntity has the entry
  // already, and the port's copy keeps the same shape.
  const CHealthInfo* GetHealthInfo() const override;
#endif

  float GetMaxAimAngle() const { return mMaxAimAngle; }
  void SetTargetPosition(const CVector3f& position) { mTargetPosition = position; }

#ifdef TARGET_PC
  // The REL's CScriptPlayerTurret (module 63): the shooting gallery's player turret. The guest
  // object is 0x230 bytes (the loader's rs_new at 0x00000494) and CActor is 0x158 there, so the
  // turret's own fields run 0x158..0x22F; every offset below is the guest one, read from the
  // constructor fn_63_30E8. The analysis is docs/module-analysis/ScriptPlayerTurret.md.
  //
  // Milestone 1 is load-and-think: the loader, the constructor, the vtable's GetTouchBounds and
  // the five member functions the module hands the DOL through SPlayerTurret_FuncPtrs. Milestone 2
  // adds the message switch of AcceptScriptMsg (fn_63_1BE8): the player enters (kSM_Activate),
  // leaves (kSM_Deactivate) and the turret starts (kSM_Start). Think (fn_63_23C0) and PreRender
  // (fn_63_2D68) are ported as well.
  CScriptPlayerTurret(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                      const CTransform4f& xf, const CActorParameters& actParms, uint flags,
                      float maxHorizRotationLeft, float maxHorizRotationRight,
                      float maxVertElevationUp, float maxVertElevationDown, float damageAngle,
                      float horizSpeed, float vertSpeed, float fireRate,
                      const CDamageInfo& weaponDamage, CAssetId weaponEffect,
                      CAssetId weaponEffectMultiPlayer, ushort sfxTurretRotation,
                      ushort sfxSinglePlayerImpact, ushort sfxMultiPlayerImpact,
                      ushort sfxSinglePlayerProjectile, ushort sfxMultiPlayerProjectile);

  // The vtable lbl_63_data_40: slot 5 is fn_63_23C0, slot 6 fn_63_1BE8, slot 9 fn_63_2D68 and
  // slot 18 fn_63_3034; the slots in between keep CActor's or CEntity's own entry.
  void Think(float dt, CStateManager& mgr) override;                        // slot 5, fn_63_23C0
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override; // slot 6, fn_63_1BE8
  void PreRender(CStateManager& mgr) override;                              // slot 9, fn_63_2D68
  rstl::optional_object< CAABox > GetTouchBounds() const override;          // slot 18, fn_63_3034

  // The five member functions the module hands the DOL through SPlayerTurret_FuncPtrs
  // (lbl_63_bss_20 +0x4 .. +0x3C). The types are the ones the DOL's table declares.
  CTransform4f GetCameraTransform(CStateManager& mgr);    // fn_63_1070
  CTransform4f GetTurretTransform(CStateManager& mgr);    // fn_63_F6C
  void ExitTurret(CStateManager& mgr);                    // fn_63_1B70
  void ProcessInput(const CFinalInput& input, CStateManager& mgr); // fn_63_1134
  TUniqueId GetHullActorId();                             // fn_63_840

private:
  // The turret's own helpers, the module functions below 0x80 bytes that the ported code calls.
  void AddSolidToTopActor(CStateManager& mgr);      // fn_63_14B4 (0x54)
  void RemoveSolidFromTopActor(CStateManager& mgr); // fn_63_1460 (0x54)
  void LoadHullActors(CStateManager& mgr);           // fn_63_15B0 (0x3C4): only the connections
  void FireTurretShot(CStateManager& mgr);           // fn_63_84C (0x184): the turret's shot
  CTransform4f GetMuzzleTransform(CStateManager& mgr); // fn_63_1508 (0x68)
  // fn_63_19B0 (0x184): the animation parameters the lookup takes - the state id and the eight
  // parameter slots. Only the first two lookups pass a parameter (the guest's FromEnum at
  // 0x1670 and 0x1704); the other three pass eight empty ones (0x175C..0x18B4).
  CPASAnimParmData BuildAnimParmData(
      pas::EAnimationState state, const CPASAnimParm& p0 = CPASAnimParm::NoParameter(),
      const CPASAnimParm& p1 = CPASAnimParm::NoParameter(),
      const CPASAnimParm& p2 = CPASAnimParm::NoParameter(),
      const CPASAnimParm& p3 = CPASAnimParm::NoParameter(),
      const CPASAnimParm& p4 = CPASAnimParm::NoParameter(),
      const CPASAnimParm& p5 = CPASAnimParm::NoParameter(),
      const CPASAnimParm& p6 = CPASAnimParm::NoParameter(),
      const CPASAnimParm& p7 = CPASAnimParm::NoParameter());

  // Host layout, in the guest's order. The guest offsets are in the comments.
  CAABox mBounds;                     // 0x158: a one-unit box around the actor's position
  uint mFlags;                        // 0x170: the flagsPlayerTurret property
  float mMaxHorizRotationLeft;        // 0x174, radians
  float mMaxHorizRotationRight;       // 0x178, radians
  float mMaxVertElevationUp;          // 0x17C, radians
  float mMaxVertElevationDown;        // 0x180, radians
  float mMaxAimAngle;                 // 0x184, radians; retail stores the damage angle here
  TUniqueId mPlayerId;                // 0x188
  float mHorizSpeed;                  // 0x18C, radians
  float mVertSpeed;                   // 0x190, radians
  float mFireRate;                    // 0x194
  TUniqueId mBaseActorId;             // 0x198: the actor that carries the model
  TUniqueId mTopActorId;              // 0x19A: the actor moved onto the hull's locator
  uint mAnimId1;                      // 0x19C
  uint mAnimId2;                      // 0x1A0
  uint mUnknown1A4;                   // 0x1A4
  uint mUnknown1A8;                   // 0x1A8
  uint mUnknown1AC;                   // 0x1AC
  float mPitchTarget;               // 0x1B0: the pitch the input asks for
  float mPitch;                    // 0x1B4: the pitch the turret has reached
  float mUnknown1B8;                  // 0x1B8
  float mUnknown1BC;                  // 0x1BC: Think's rotation sound debounce (0..0.05)
  float mUnknown1C0;                  // 0x1C0: Think's alignment (1 = still)
  CVector3f mBaseForward;             // 0x1C4..0x1CC: the editor transform's forward vector, then
                                      // the top actor's one of the last Think
  float mYawTarget;                  // 0x1D0: the yaw the input asks for
  CModelFlags mModelFlags;            // 0x1D4..0x1DC: the base actor's model flags
  CAssetId mWeaponEffect;             // 0x1E0
  CAssetId mWeaponEffectMulti;        // 0x1E4
  rstl::single_ptr< CToken > mToken;  // 0x1E8: the hull's model token
  CDamageInfo mWeaponDamage;          // 0x1F0
  float mUnknown20C;                  // 0x20C
  CVector3f mTargetPosition;          // 0x210: the editor transform's translation
  CSfxHandle mSfxHandle;              // 0x21C: the rotation sound
  TUniqueId mHullActorId;             // 0x220
  ushort mSfxTurretRotation;          // 0x222
  ushort mSfxSinglePlayerImpact;      // 0x224
  ushort mSfxMultiPlayerImpact;       // 0x226
  ushort mSfxSinglePlayerProjectile;  // 0x228
  ushort mSfxMultiPlayerProjectile;   // 0x22A
  bool mPlayerInTurret : 1;           // 0x22C bit 0x80
  bool mUnknown22C_6 : 1;             // 0x22C bit 0x40
#else
  uchar x158_[0x2c];
  float mMaxAimAngle; // 0x184
  uchar x188_[0x88];
  CVector3f mTargetPosition; // 0x210
#endif
};
CHECK_OFFSETOF(CScriptPlayerTurret, mMaxAimAngle, 0x184)
CHECK_OFFSETOF(CScriptPlayerTurret, mTargetPosition, 0x210)

#endif // _CSCRIPTPLAYERTURRET
