#ifndef _CACTOR
#define _CACTOR

#include "types.h"

#include "Collision/CMaterialFilter.hpp"
#include "Collision/CMaterialList.hpp"

#include "MetroidPrime/ActorCommon.hpp"
#include "MetroidPrime/CActorParameters.hpp"
#include "MetroidPrime/CEntity.hpp"
#include "MetroidPrime/CModelData.hpp"

#include "Kyoto/Animation/CSegId.hpp"
#include "Kyoto/Audio/CAudioSys.hpp"
#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Graphics/CColor.hpp"
#include "Kyoto/Graphics/CModelFlags.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "Kyoto/Math/CTransform4f.hpp"
#include "Kyoto/TToken.hpp"

#include "rstl/optional_object.hpp"
#include "rstl/pair.hpp"
#include "rstl/reserved_vector.hpp"
#include "rstl/single_ptr.hpp"

class CActorLights;
class CActorParameters;
class CEchoEmitter;
struct SEchoParameters;
struct SLdrAudioPlaybackParms;
class CScannableObjectInfo;
class CSimpleShadow;

class CDamageInfo;
class CDamageVulnerability;
class CFrustumPlanes;
class CHealthInfo;
class CScriptWater;
class CWeaponMode;
class CInt32POINode;

class CActor : public CEntity {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  enum EFluidState {
    kFS_EnteredFluid,
    kFS_InFluid,
    kFS_LeftFluid,
  };
  enum EScanState {
    kSS_Start,
    kSS_Processing,
    kSS_Done,
  };

  // Echoes sound records include a locator and a volume-selection flag.
  struct SSound {
    SSound(const CSfxHandle& handle, const CSegId& locator, bool useEchoVolume);

    CSfxHandle mHandle;
    CSegId mLocator;
    bool mUseEchoVolume : 1;
  };
  typedef rstl::pair< TSfxId, SSound > TLoopingSound;

  CActor(TUniqueId uid, const rstl::string& name, const CEntityInfo& info, uint inGrave,
         const CTransform4f& xf, const CModelData& mData, const CMaterialList& list,
         const CActorParameters& params, TUniqueId nextDrawNode);

  // CEntity
  ~CActor() override;
  CEntity* TypesMatch(int typeId) const override;
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg&) override;
  void SetActive(const bool active) override;

  // CActor
  virtual void ClearFluidList(CStateManager& mgr);
  virtual void PreRender(CStateManager&);
  virtual void AddToRenderer(const CStateManager&) const;
  virtual void Render(const CStateManager&) const;
  virtual bool CanRenderUnsorted(const CStateManager&) const;
  virtual void PreRenderAllViewports(CStateManager& mgr);
  virtual CHealthInfo* HealthInfo();
  // @ fn_25_23E8
  virtual const CHealthInfo* GetHealthInfo() const {
    return const_cast< CActor* >(this)->HealthInfo();
  }
  virtual const CDamageVulnerability* GetDamageVulnerability() const;
  virtual const CDamageVulnerability* GetDamageVulnerability(const CVector3f&, const CVector3f&,
                                                             const CDamageInfo&) const;
  virtual rstl::optional_object< CAABox > GetTouchBounds() const;
  virtual void Touch(CActor&, CStateManager&);
  virtual CVector3f GetOrbitPosition(const CStateManager&) const;
  virtual CVector3f GetAimPosition(const CStateManager&, float) const;
  virtual CVector3f GetHomingPosition(const CStateManager&, float) const;
  virtual CVector3f GetScanObjectIndicatorPosition(const CStateManager&) const;
  virtual EWeaponCollisionResponseTypes GetCollisionResponseType(const CVector3f&, const CVector3f&,
                                                                 const CWeaponMode&,
                                                                 int /*EProjectileAttrib?*/) const;
  virtual void FluidFXThink(EFluidState, CScriptWater&, CStateManager&);
  virtual void OnScanStateChange(EScanState, CStateManager&);
  virtual CAABox GetSortingBounds(const CStateManager&) const;
  virtual void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                               float dt);
  virtual CScannableObjectInfo* GetScannableObjectInfo() const;
  virtual void ProcessSoundEvent(int sfxId, float weight, int flags, float fallOff, float maxDist,
                                 const CSegId& locator, ushort pitchStart, ushort pitchEnd,
                                 float pitchDuration, uchar minVol, uchar maxVol,
                                 float distanceSquared, const CVector3f& position, int aid,
                                 CStateManager& mgr, bool translateId);

  CAdvancementDeltas UpdateAnimation(float dt, CStateManager& mgr, bool advTree);

  void UpdateSfxEmitters(CStateManager& mgr);
  void StopLoopedSounds();
  bool FindLoopedSound(ushort sfxId);
  void StopLoopedSound(ushort sfxId); // Guessed name.
  CSfxHandle PlayCustomSound(const CVector3f& position, const CVector3f& direction,
                             const SLdrAudioPlaybackParms& parameters, bool looped) const;
  void SetModelData(const CModelData& modelData, CStateManager& mgr);
  float GetAverageAnimVelocity(int anim);
  void EnsureRendered(const CStateManager& mgr) const;
  void EnsureRendered(const CStateManager& mgr, const CVector3f& pos, const CAABox& bounds) const;
  bool IsModelOpaque(const CStateManager& mgr) const;
  void RenderInternal(const CStateManager& mgr) const;
  void AllocateShadow(); // Guessed name.

  void UpdatePortalSystemState(CStateManager& mgr);
  // Despite the original name, this returns the minimum squared camera distance.
  float GetDistanceToCamera(CStateManager& mgr) const;
  void SetEchoEmitter(bool enabled, CEchoEmitter* emitter);
  CEchoEmitter* EchoEmitter() { return mEchoEmitter.get(); }
  CEchoEmitter* EchoEmitter() const { return mEchoEmitter.get(); }

  bool GetEchoEmitterEnabled() const { return mEchoEmitterEnabled; } // Guessed accessor name.

  CEchoEmitter* AllocateEchoEmitter(bool enabled, const CAABox& bounds,
                                    const SEchoParameters& parameters);
  void SetValidTarget(int playerIndex, bool enabled);
  void SetVisorOrbitableFlags(CVisorParameters::EVisorOrbitableFlags flags, bool enabled);

  const CTransform4f& GetTransform() const { return mTransform; }
  void SetTransform(const CTransform4f& xf);
  void SetRotation(const CQuaternion& rot) { SetTransform(rot.BuildTransform4f(GetTranslation())); }
  CQuaternion GetRotation() const { return CQuaternion::FromMatrix(GetTransform()); }
  const CVector3f& GetTranslation() const { return mPosition; }
  void SetTranslation(const CVector3f& vec);
  CTransform4f GetLocatorTransform(const rstl::string& segName) const;
  CTransform4f GetScaledLocatorTransform(const rstl::string& segName) const;
  CTransform4f GetScaledLocatorTransform(const CSegId& locator) const;
  float GetYaw() const;
  void SetActorLights(rstl::auto_ptr< CActorLights > lights);
  void SetWorldLightingDirty(bool dirty) { mWorldLightingDirty = dirty; }
  void SetInFluid(CStateManager& mgr, bool inFluid, TUniqueId uid);
  TUniqueId InFluidId() const;
  // Guessed names.
  void SetFluidList(const rstl::reserved_vector< TUniqueId, 4 >& fluids);
  const rstl::reserved_vector< TUniqueId, 4 >& GetFluidList() const;
  int GetFluidCount() const { return mFluidIds.size(); }
  void RemoveInvalidFluidIds(CStateManager& mgr);

  bool NullModel() const { return !GetAnimationData() && !GetModelData()->HasNormalModel(); }

  bool HasModelData() const {
    return GetModelData() && (GetModelData()->HasAnimation() || GetModelData()->HasNormalModel());
  }
  CModelData* ModelData() { return mModelData.get(); }
  const CModelData* GetModelData() const { return mModelData.get(); }
  bool GetPointGeneratorParticles() const { return mPointGeneratorParticles; }
  void SetPointGeneratorParticles(bool enabled) { mPointGeneratorParticles = enabled; }

  bool HasAnimation() const { return GetModelData() && GetModelData()->HasAnimation(); }
  CAnimData* AnimationData() { return ModelData()->AnimationData(); }
  const CAnimData* GetAnimationData() const { return GetModelData()->GetAnimationData(); }

  bool HasShadow() const { return GetShadow() != nullptr; }
  CSimpleShadow* Shadow() { return mSimpleShadow.get(); }
  const CSimpleShadow* GetShadow() const { return mSimpleShadow.get(); }

  bool HasActorLights() const { return !mActorLights.null(); }
  CActorLights* ActorLights() { return mActorLights.get(); }
  const CActorLights* GetActorLights() const { return mActorLights.get(); }

  const CModelFlags& GetModelFlags() const { return mDrawFlags; }
  void SetModelFlags(const CModelFlags& flags) { mDrawFlags = flags; }
  void SetModelColor(const CColor& color) { mDrawFlags.SetColor(color); } // Guessed name.

  const CMaterialList& GetMaterialList() const { return mMaterial; }
  CMaterialList& MaterialList() { return mMaterial; }

  const CMaterialFilter& GetMaterialFilter() const;
  void SetMaterialFilter(const CMaterialFilter& filter);

  bool GetTransformDirty() const { return mNotInSortedLists; }
  bool GetTransformDirtySpare() const { return mTransformDirty; }
  bool GetPreRenderHasMoved() const { return mActorLightsDirty; }
#if VERSION == VERSION_G2MP01
  bool GetPreRenderLightsDirty() const { return mPreRenderLightsDirty; } // Guessed name
#endif
  bool GetRenderBoundsDirty() const { return mRenderBoundsDirty; }
  bool GetPreRenderClipped() const { return mOutOfFrustum; }
  bool GetCalculateLighting() const { return mCalculateLighting && HasActorLights(); }
  bool GetDrawShadow() const { return mShadowEnabled; }
  bool GetShadowDirty() const { return mShadowDirty; }
  bool GetMuted() const { return mMuted; }
  bool GetRenderParticleDatabaseInside() const { return mRenderParticleDBInside; }
  uint GetValidTargetPlayers() const { return mValidTargetPlayers; }

  void SetTransformDirty(bool b) { mNotInSortedLists = b; }
  void SetTransformDirtySpare(bool b) { mTransformDirty = b; }
  void SetPreRenderHasMoved(bool b) { mActorLightsDirty = b; }
#if VERSION == VERSION_G2MP01
  void SetPreRenderLightsDirty(bool b) { mPreRenderLightsDirty = b; } // Guessed name
#endif
  void SetRenderBoundsDirty(bool dirty) { mRenderBoundsDirty = dirty; }
  void SetPreRenderClipped(bool b) { mOutOfFrustum = b; }
  void SetCalculateLighting(bool b);
  void SetDrawShadow(bool enabled);
  void SetShadowDirty(bool b) { mShadowDirty = b; }
  void SetMuted(bool b);
  void SetRenderParticleDatabaseInside(bool b) { mRenderParticleDBInside = b; }
  void SetEnableRender(bool b) { mEnableRender = b; } // Guessed name
  void SetDrawEnabled(bool enabled) { mDrawEnabled = enabled; }

  void SetDamageHighlight(bool enabled) { mDamageHighlight = enabled; } // Guessed accessor name.

  void SetHighlightedInDarkVisor(bool enabled) {
    mHighlightedInDarkVisor = enabled;
  } // Guessed name.

  bool GetDrawEnabled() const { return mDrawEnabled; } // Guessed name
  bool GetTakesProjectedShadow() const { return mTakesProjectedShadow; }
  bool GetDoTargetDistanceTest() const { return mDoTargetDistanceTest; }
  uint GetTargetableVisorFlags() const { return mTargetableVisorFlags; }
  void SetDoTargetDistanceTest(bool enabled) { mDoTargetDistanceTest = enabled; }

  void RemoveMaterial(EMaterialTypes, EMaterialTypes, EMaterialTypes, EMaterialTypes,
                      EMaterialTypes, CStateManager&);
  void RemoveMaterial(EMaterialTypes, EMaterialTypes, EMaterialTypes, EMaterialTypes,
                      CStateManager&);
  void RemoveMaterial(EMaterialTypes, EMaterialTypes, EMaterialTypes, CStateManager&);
  void RemoveMaterial(EMaterialTypes, EMaterialTypes, CStateManager&);
  void RemoveMaterial(EMaterialTypes, CStateManager&);
  void AddMaterial(EMaterialTypes, EMaterialTypes, EMaterialTypes, EMaterialTypes, EMaterialTypes,
                   CStateManager&);
  void AddMaterial(EMaterialTypes, EMaterialTypes, EMaterialTypes, EMaterialTypes, CStateManager&);
  void AddMaterial(EMaterialTypes, EMaterialTypes, EMaterialTypes, CStateManager&);
  void AddMaterial(EMaterialTypes, EMaterialTypes, CStateManager&);
  void AddMaterial(EMaterialTypes, CStateManager&);
  void AddMaterial(const CMaterialList& l) { mMaterial.Add(l); }
  void SetMaterialList(const CMaterialList& l, CStateManager&);

  const CAABox& GetRenderBoundsCached() const { return mRenderBounds; }
  void SetRenderBounds(const CAABox& bounds) { mRenderBounds = bounds; }
  const CAABox& GetOtherBounds() const { return mOtherBounds; }
  void SetOtherBounds(const CAABox& bounds) { mOtherBounds = bounds; }

  bool GetUseInSortedLists() const;
  void SetUseInSortedLists(bool use);
  bool GetCallTouch() const;
  void SetCallTouch(bool value);
  void SetVolume(uchar volume);
  void SetSoundEventPitchBend(int);
  void ClearSoundEventPitchBend();
  bool CanDrawStatic() const;
  bool ShouldDrawShadow(const CStateManager& mgr) const; // Guessed name.
  int GetRenderAlphaBufferAlpha(const CStateManager& mgr) const;

  void SetNextDrawNode(TUniqueId id) { mNextDrawNode = id; }

  TUniqueId GetDrawParent() const { return mNextDrawNode; }

  uint GetDrawToken() const { return mDrawnToken; }

  uint GetAddedToken() const { return mAddedToken; }
  void SetAddedToken(uint token) const { mAddedToken = token; }
  bool UsesAlphaSorting() const { return mAlphaSorted; }             // Guessed accessor name.
#ifdef TARGET_PC
  void SetUseAlphaSorting(bool b) { mAlphaSorted = b; } // Guessed accessor name.
#endif
  bool UsesPortalVisibility() const { return mUsePortalVisibility; } // Guessed accessor name.

  void SetDrawToken(uint token) const { mDrawnToken = token; }

  void SetPvsIndex(int index) { mPvsIndex = index; }
  int GetPvsIndex() const { return mPvsIndex; }

  void SetTransformDirty();

#ifdef TARGET_PC
  // The same four-bit setter, under the name the pre-upstream tree gave it. Upstream calls retail's
  // `SetDirtyFlags__6CActorFv` `SetTransformDirty__6CActorFv`, which is the declaration above;
  // this declaration exists only so the port's split unit
  // `src/MetroidPrime/CActorSetDirtyFlags.cpp` still has a declaration to define. It is a plain
  // non-virtual member, so it adds no vtable entry and moves no member.
  void SetDirtyFlags();
  // Port: the transform itself, for a retail body that swaps it round a draw with a plain copy
  // (no dirty flags, unlike SetTransform).
  CTransform4f& PortRawTransform() const { return const_cast< CTransform4f& >(mTransform); }

  // Port: actor transform smoothing (MP1's docs/FRAME_INTERPOLATION.md, "1. Actor transforms").
  // CStateManager::Update records every actor's transform before the tick; frames drawn between
  // ticks then blend the two. The tick generation is PortTiming::TickGeneration (MP1's
  // PortBeginTickSnapshot is PortTiming::BeginTick), bumped once per tick in main.cpp's loop.
  static uint PortTickGeneration();
  void PortSnapshotRenderTransform();
  // The camera-to-world view that draws this actor at its blended transform instead of
  // mTransform. False when no blend applies (the view is left alone): smooth frames off, capped
  // frames, snapped moves, new actors.
  bool PortPresentedView(const CTransform4f& view, CTransform4f& out) const;
  // MP1's PortPresentedPoint: a world point on this actor carried to where the actor is drawn
  // (its blended transform); unchanged when no blend applies.
  CVector3f PortPresentedPoint(const CVector3f& world) const;
  // Rigid blend of two transforms at t, with the snap rule (a teleport or 45 degrees apart =
  // false). `cur` gets the rigid form of `to`. A non-zero `pivot` (world offset to the visual
  // centre, the morph ball's radius) makes that centre lerp and the turn happen about it; then
  // only a teleport cuts, a big roll keeps the translation. `lastStep2` is the squared step of the
  // tick before, so fast movers aren't taken for teleports (port_presentation_rules.h).
  static bool PortBlendRigid(const CTransform4f& from, const CTransform4f& to, float t,
                             CTransform4f& blend, CTransform4f& cur,
                             const CVector3f& pivot = CVector3f::Zero(), float lastStep2 = 0.f);
  // True while a CPortActorRenderScope has the view shifted (MP1's PortRenderScopeActive): a
  // particle system drawn inside one must not blend its own translation again.
  static bool PortRenderScopeActive();
  // MP1's PortSetOwnPresentation: an actor that draws in world space from its own state (beam
  // weapons) opts out of the rigid shift and blends what it draws itself.
  void PortSetOwnPresentation() { mPortOwnPresentation = true; }
#endif

private:
  // Guessed names.
  void RemoveLoopedSoundAt(int index);
  uint GetVisorSoundVolume(const CStateManager& mgr) const;
  void PlayLoopedSound(ushort sfxId, int flags, float fallOff, float maxDist, uchar minVol,
                       uchar maxVol, bool nonEmitter, int area, bool useAcoustics,
                       const CSegId& locator, ushort pitchStart, ushort pitchEnd,
                       float pitchDuration, bool useEchoVolume);
  void AddLoopedSound(ushort sfxId, bool nonEmitter, int area, bool useAcoustics,
                      CAudioSys::C3DEmitterParmData& parameters, const CSegId& locator,
                      ushort pitchStart, ushort pitchEnd, float pitchDuration, bool useEchoVolume);

  CTransform4f mTransform;                   // x24
  CVector3f mPosition;                       // x54
  rstl::single_ptr< CModelData > mModelData; // x60
  CMaterialList mMaterial;                   // x68
  CMaterialFilter mMaterialFilter;
  rstl::reserved_vector< TLoopingSound, 4 > mLoopingSounds; // x88
  rstl::single_ptr< CActorLights > mActorLights;
  rstl::single_ptr< CSimpleShadow > mSimpleShadow;
  rstl::single_ptr< TCachedToken< CScannableObjectInfo > > mScanObjectInfo;
  rstl::single_ptr< CEchoEmitter > mEchoEmitter;
  CAABox mOtherBounds;
  CAABox mRenderBounds;
  CModelFlags mDrawFlags;
  float mTime;
  uint mPitchBend;
  rstl::reserved_vector< TUniqueId, 4 > mFluidIds;
  rstl::reserved_vector< TUniqueId, 4 > mPreviousFluidIds;
  bool mFluidIdsChanged : 1;
  TUniqueId mNextDrawNode;
  mutable int mDrawnToken;
  mutable int mAddedToken;
  int mPvsIndex;
  uchar mMaxVol;
  uchar mNormalVolume;
  uchar mEchoVolume;
  rstl::reserved_vector< SSound, 2 > mNonLoopingSounds; // x13c
  uint mNextNonLoopingSfxHandle : 3;                    // x150
  uint mNotInSortedLists : 1;
  uint mTransformDirty : 1;
  uint mActorLightsDirty : 1;
#if VERSION == VERSION_G2MP01
  uint mPreRenderLightsDirty : 1; // Guessed name
#endif
  uint mRenderBoundsDirty : 1;
  uint mOutOfFrustum : 1;
  uint mCalculateLighting : 1; // x151
  uint mShadowEnabled : 1;
  uint mShadowDirty : 1;
  uint mMuted : 1;
  uint mUseInSortedLists : 1;
  uint mUsePortalVisibility : 1;
  uint mCallTouch : 1;
  uint mGlobalTimeProvider : 1;
  uint mRenderUnsorted : 1; // x152
  uint mPointGeneratorParticles : 1;
  uint mRenderParticleDBInside : 1;
  uint mEnablePitchBend : 1;
  uint mTargetableVisorFlags : 4;
  uint mEnableRender : 1; // x153
  uint mWorldLightingDirty : 1;
  uint mDrawEnabled : 1;
  uint mDoTargetDistanceTest : 1;
  uint mValidTargetPlayers : 4;
  uint mEchoEmitterEnabled : 1;
  uint mHighlightedInDarkVisor : 1;
  uint mDamageHighlight : 1;
  uint mTakesProjectedShadow : 1;
  uint mLoopingSoundCount : 3;
  uint mAlphaSorted : 1; // Guessed name
#ifdef TARGET_PC
  CTransform4f mPortPrevTransform;
  uint mPortPrevGeneration;
  // Squared step of the tick before (0 when that tick had no snapshot).
  float mPortLastStep2;
  bool mPortOwnPresentation;
#endif
};
CHECK_SIZEOF(CActor, 0x158)
NESTED_CHECK_SIZEOF(CActor, SSound, 0x8)
NESTED_CHECK_SIZEOF(CActor, TLoopingSound, 0xc)
#ifdef TARGET_PC
// Port: while alive, draws go through the actor's presented view (see
// CActor::PortPresentedView). Nested scopes leave the outer one's view.
class CPortActorRenderScope {
public:
  explicit CPortActorRenderScope(const CActor& actor);
  ~CPortActorRenderScope();

private:
  CPortActorRenderScope(const CPortActorRenderScope&);
  CPortActorRenderScope& operator=(const CPortActorRenderScope&);
  CTransform4f mSavedView;
  bool mActive;
};

// Port: puts back the view an active CPortActorRenderScope replaced, until Resume or the end of
// its life (MP1's CPortRenderScopeSuspend). A particle system whose spawn point moved draws
// through its own blend instead of the owner's, so the two shifts don't add up.
class CPortRenderScopeSuspend {
public:
  explicit CPortRenderScopeSuspend(bool enable);
  ~CPortRenderScopeSuspend();
  void Resume();

private:
  CPortRenderScopeSuspend(const CPortRenderScopeSuspend&);
  CPortRenderScopeSuspend& operator=(const CPortRenderScopeSuspend&);
  CTransform4f mScopedView;
  bool mActive;
};
#endif

#endif // _CACTOR
