#ifndef _CTARGETRETICLES
#define _CTARGETRETICLES

#ifdef TARGET_PC
#include "Kyoto/Graphics/CColor.hpp"
#endif
#include "Kyoto/Math/CQuaternion.hpp"
#include "Kyoto/TToken.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/TGameTypes.hpp"
#include "rstl/vector.hpp"

class CActor;
class CMatrix3f;
class CScriptGrapplePoint;
class CModel;
class CTexture;
class CStateManager;

enum EReticleState { kRS_Combat, kRS_Scan, kRS_Echo, kRS_Dark, kRS_Four, kRS_Unspecified };

class CTargetReticleRenderState {
public:
  CTargetReticleRenderState(TUniqueId target, float radius, CVector3f position, float factor,
                            float minimumViewportScale, bool orbitZoneIdlePosition);

  static void InterpolateWithClamp(const CTargetReticleRenderState& a,
                                   CTargetReticleRenderState& out,
                                   const CTargetReticleRenderState& b, float t);

  void SetTargetId(TUniqueId id) { mTarget = id; }
  void SetFactor(float factor) { mFactor = factor; }
  void SetIsOrbitZoneIdlePosition(bool idle) { mOrbitZoneIdlePosition = idle; }
  void SetRadiusWorld(float radius) { mRadius = radius; }
  void SetTargetPositionWorld(const CVector3f& position) { mPosition = position; }
  void SetMinViewportClampScale(float scale) { mMinimumViewportScale = scale; }

  TUniqueId GetTargetId() const { return mTarget; }
  float GetRadiusWorld() const { return mRadius; }
  CVector3f GetTargetPositionWorld() const { return mPosition; }
  float GetFactor() const { return mFactor; }
  float GetMinViewportClampScale() const { return mMinimumViewportScale; }
  bool GetIsOrbitZoneIdlePosition() const { return mOrbitZoneIdlePosition; }

private:
  TUniqueId mTarget;
  float mRadius;
  CVector3f mPosition;
  float mFactor;
  float mMinimumViewportScale;
  bool mOrbitZoneIdlePosition;
};

class CCompoundTargetReticle {
public:
  struct SOuterItemInfo {
    explicit SOuterItemInfo(const char* modelName);
    TCachedToken< CModel > mModel;
    float mOffshootBaseAngle;
    float mRotationAngle;
    float mBaseAngle;
    float mOffshootAngleDelta;
  };

  CCompoundTargetReticle(const CStateManager& mgr, int playerIndex);
  ~CCompoundTargetReticle() {}
  void SetLeadingOrientation(const CQuaternion& orientation) { mLeadingOrientation = orientation; }

  void Touch() const;
  static float CalculateClampedScale(CVector3f position, float scale, float clampMin,
                                     float clampMax, const CStateManager& mgr, int playerIndex);
  static bool IsGrappleTarget(TUniqueId id, const CStateManager& mgr);
  CVector3f CalculateOrbitZoneReticlePosition(const CStateManager& mgr, bool lag) const;
  CVector3f CalculatePositionWorld(const CActor& actor, const CStateManager& mgr) const;
  float CalculateRadiusWorld(const CActor& actor, const CStateManager& mgr) const;
  void UpdateTargetParameters(CTargetReticleRenderState& state, const CStateManager& mgr);
  void DrawOrbitZoneGroup(const CMatrix3f& rotation, const CStateManager& mgr) const;
  void DrawNextLockOnGroup(const CMatrix3f& rotation, const CStateManager& mgr) const;
  void DrawScanTargetGroup(const CMatrix3f& rotation,
                           const CStateManager& mgr) const;                       // Guessed name
  void DrawCrosshairs(const CMatrix3f& rotation, const CStateManager& mgr) const; // Guessed name
  void DrawSeeker(const CMatrix3f& rotation, const CStateManager& mgr) const;     // Guessed name
  void DrawCurrLockOnGroup(const CMatrix3f& rotation, const CStateManager& mgr) const;
  void DrawGrapplePoint(const CScriptGrapplePoint& point, float factor, const CStateManager& mgr,
                        const CMatrix3f& rotation, bool zEqual) const;
  void DrawGrappleGroup(const CMatrix3f& rotation, const CStateManager& mgr, bool hideLockOn) const;
  void Draw(const CStateManager& mgr, bool hideLockOn) const;
  void UpdateOrbitZoneGroup(float dt, const CStateManager& mgr);
  void UpdateNextLockOnGroup(float dt, const CStateManager& mgr);
  void UpdateCurrLockOnGroup(float dt, const CStateManager& mgr);
  void Update(float dt, const CStateManager& mgr);
#ifdef TARGET_PC
  // The Prime port's remastered crosshair (ported from its CTargetReticles.cpp): the distance
  // scale of the cursor's arms and centre, and how far its colour has faded toward a hostile.
  void PortUpdateRemasteredCrosshair(float dt, const CStateManager& mgr);
  float mPortXhairArmScale = 1.f;
  float mPortXhairCentreScale = 1.f;
  float mPortXhairTargetT = 0.f;
  // MP1's PortPresentGroup: carry a group placed at the tick to where its target is drawn
  // between ticks (CActor::PortPresentedPoint), or an idle group with the presented camera.
  void PortPresentGroup(CTargetReticleRenderState& state, const CStateManager& mgr) const;
  // mTargetPosition (screen-centre point, placed from the tick camera) carried into the presented
  // camera's frame, as MP1 689c6cb1c does for the idle crosshair: the scan visor's centre bracket,
  // the crosshair and the seeker cross are drawn there.
  CVector3f PortPresentedTarget(const CStateManager& mgr) const;

  // Trilogy (rs5mp2_p.dol) scan visor ring and target brackets, ported from MP1's PortTriDrawScan
  // (MP1 bc1332c52..ed42d564a). Echoes' draw is 0x8025de8c; see PortTriDrawScan.
  void PortTriUpdateScanPointer(const CStateManager& mgr);
  void PortTriUpdateScan(float dt, const CStateManager& mgr);
  void PortTriUpdateScanBounds(const CStateManager& mgr);
  static TUniqueId PortTriResolveScanTarget(TUniqueId id, const CStateManager& mgr);
  void PortTriDrawScan(const CMatrix3f& rotation, const CStateManager& mgr) const;
  bool mPortTri = false;
  TCachedToken< CModel > mPortTriScanRing;
  TCachedToken< CModel > mPortTriScanBracket;
  TCachedToken< CModel > mPortTriScanProgress;
  float mPortTriScanFade = 0.f;
  float mPortTriScanLerp = 0.f;
  float mPortTriScanning = 0.f;
  CVector3f mPortTriScanPrevCentre = CVector3f::Zero();
  CVector3f mPortTriScanCentre = CVector3f::Zero();
  CVector3f mPortTriScanPrevSize = CVector3f::Zero();
  CVector3f mPortTriScanSize = CVector3f::Zero();
  int mPortTriScanTargetCount = 0;
  TUniqueId mPortTriScanTargets[1] = {kInvalidUniqueId};
  TUniqueId mPortTriLastNextTarget = kInvalidUniqueId;
  TUniqueId mPortTriScanTarget = kInvalidUniqueId;
  CVector3f mPortTriPointerPos = CVector3f::Zero();

  // Trilogy combat group (MP1 fn_80236A98/8023AC48/8023B228 equivalents; rs5mp2_p.dol loads the
  // same tokens in the ctor at 0x80259b70). The orbit button is not readable from the reticle, so
  // a release is "lock ended and not by a damage orbit break".
  void PortTriUpdateCombat(float dt, const CStateManager& mgr);
  void PortTriDrawNext(const CMatrix3f& rotation, const CStateManager& mgr) const;
  void PortTriDrawLocked(const CMatrix3f& rotation, const CStateManager& mgr) const;
  void PortTriDrawLockAt(const CMatrix3f& rotation, const CVector3f& pos, float lockTime,
                         const CColor& color, float alpha) const;
  void PortTriDrawAiming(const CMatrix3f& rotation, const CStateManager& mgr) const;
  void PortTriDrawScanProgress(const CMatrix3f& rotation, const CVector3f& pos, float progress,
                               const CColor& color) const;
  CVector3f PortTriProjectNear(const CStateManager& mgr, const CVector3f& world) const;
  TCachedToken< CModel > mPortTriAimCenter, mPortTriAimArm, mPortTriLockArm, mPortTriLockTech,
      mPortTriLockBrackets, mPortTriLockBase;
  TUniqueId mPortTriPointerTarget = kInvalidUniqueId;
  float mPortTriAimArmScale = 1.f, mPortTriAimCenterScale = 1.f, mPortTriAimTarget = 0.f;
  CColor mPortTriAimArmColor = CColor::White(), mPortTriAimCenterColor = CColor::White();
  float mPortTriAimArmAlpha = 0.f, mPortTriAimCenterAlpha = 0.f;
  CVector3f mPortTriNextWorld = CVector3f::Zero(), mPortTriNextFrom = CVector3f::Zero();
  TUniqueId mPortTriNext = kInvalidUniqueId;
  float mPortTriNextAlpha = 0.f, mPortTriNextAngle = 0.f, mPortTriNextTime = 0.f;
  bool mPortTriNextMoving = false;
  TUniqueId mPortTriLocked = kInvalidUniqueId;
  CColor mPortTriLockColor = CColor::White();
  float mPortTriLockAlpha = 0.f, mPortTriLockTime = 0.f, mPortTriReleaseAlpha = 0.f;
  float mPortTriLockFade = 0.f, mPortTriReleaseFade = 0.f, mPortTriPrevYaw = 0.f,
        mPortTriBracketAngle = 0.f, mPortTriTechAngle = 0.f;
#endif
  EReticleState GetDesiredReticleState(const CStateManager& mgr) const;
  bool CheckLoadComplete();

private:
  int mPlayerIndex;
  CQuaternion mLeadingOrientation;
  CQuaternion mLaggingOrientation;
  EReticleState mPreviousState;
  EReticleState mNextState;
  mutable int mNoDrawTicks;
  float mOvershootOffsetHalf;
  float mPremultipliedOvershootOffset;
  TCachedToken< CModel > mCrosshairs;
  TCachedToken< CModel > mSeeker;
  TCachedToken< CModel > mTargetFlower;
  TCachedToken< CModel > mMissileBracket;
  TCachedToken< CModel > mInnerBeamIcon;
  TCachedToken< CModel > mLockConfirm;
  TCachedToken< CModel > mLockFire;
  TCachedToken< CModel > mLockDagger;
  TCachedToken< CModel > mGrapple;
  TCachedToken< CModel > mChargeTickFirst;
  TCachedToken< CModel > mScanTargetCenter;
  TCachedToken< CModel > mScanTargetLeft;
  TCachedToken< CModel > mScanTargetRight;
  SOuterItemInfo mChargeGauge;
  rstl::vector< SOuterItemInfo > mOuterBeamIconSquares;
  TCachedToken< CTexture > mQuarterCurve;
  TCachedToken< CModel > mSeekerMissileLockConfirm;
  TCachedToken< CModel > mSeekerMissileCrosshair;
  TCachedToken< CTexture > mRadarPaintFirst;
  TCachedToken< CTexture > mRadarPaintSecond;
  TUniqueId mTargetId;
  TUniqueId mNextTargetId;
  CVector3f mTargetPosition;
  CVector3f mLaggingTargetPosition;
  CTargetReticleRenderState mCurrentGroupInterpolated;
  CTargetReticleRenderState mCurrentGroupA;
  CTargetReticleRenderState mCurrentGroupB;
  float mCurrentGroupDuration;
  float mCurrentGroupTimer;
  CTargetReticleRenderState mNextGroupInterpolated;
  CTargetReticleRenderState mNextGroupA;
  CTargetReticleRenderState mNextGroupB;
  float mNextGroupDuration;
  float mNextGroupTimer;
  TUniqueId mGrapplePointA;
  TUniqueId mGrapplePointB;
  float mGrapplePointFactorA;
  float mGrapplePointFactorB;
  TUniqueId mVulnerabilityTarget;
  CDamageVulnerability mTargetVulnerability;
  float mTargetHealth;
  float mTargetHealthShadow;
  float mCrosshairsDrawScale;
  float mSeekerRotationAngle;
  float mFlowerRotationAngle;
  bool mMissileActive;
  float mMissileBracketTimer;
  float mMissileBracketScaleTimer;
  CPlayerState::EBeamId mBeam;
  float mChargeGaugeOvershootTimer;
  float mLockOnTimer;
  float mNextTargetFade;
  float mLockFireTimer;
  float mFullChargeFadeTimer;
  float mScanBracketFactor;
  float mScanTargetFactor;
  bool mBeamShot : 1;
  bool mMissileShot : 1;
  bool mFullyCharged : 1;
};

class COrbitPointMarker {
public:
  explicit COrbitPointMarker(int playerIndex);

  void ResetInterpolationTimer(float time);
  void Draw(const CStateManager& mgr) const;
  void Update(float dt, const CStateManager& mgr);
  bool CheckLoadComplete();

private:
  int mPlayerIndex;
  float mZOffset;
  bool mCameraRelativeZ;
  float mLagAzimuth;
  float mAzimuth;
  CVector3f mLagTargetPosition;
  bool mLastFreeOrbit;
  float mInterpolationTimer;
  float mCurrentTime;
  TCachedToken< CModel > mOrbitPointModel;
};

class CTargetingManager {
public:
  CTargetingManager(const CStateManager& mgr, int playerIndex);
  ~CTargetingManager() {}
  CCompoundTargetReticle& CompoundTargetReticle() { return mTargetReticle; }
  bool CheckLoadComplete();
  void Update(float dt, const CStateManager& mgr);
  void Draw(const CStateManager& mgr, bool hideLockOn) const;

  void Touch() const;

private:
  int mPlayerIndex;
  CCompoundTargetReticle mTargetReticle;
  COrbitPointMarker mOrbitPointMarker;
};

CHECK_SIZEOF(CTargetReticleRenderState, 0x20)
CHECK_SIZEOF(CCompoundTargetReticle, 0x2ac)
CHECK_SIZEOF(COrbitPointMarker, 0x38)
CHECK_SIZEOF(CTargetingManager, 0x2e8)

#endif // _CTARGETRETICLES
