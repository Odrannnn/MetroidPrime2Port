#include "MetroidPrime/Player/CPlayerVisor.hpp"

#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#ifdef TARGET_PC
#include "MetroidPrime/PortEfbCopy.hpp"
#endif
#include "MetroidPrime/SFX/SamusPowerups_MP.h"
#include "MetroidPrime/SFX/UI.h"
#include "MetroidPrime/SFX/UI2.h"
#include "MetroidPrime/SFX/UI_MP.h"
#include "MetroidPrime/Tweaks/CTweakGui.hpp"
#include "MetroidPrime/Tweaks/CTweakGuiColors.hpp"

#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Graphics/CModel.hpp"
#include "Kyoto/Graphics/CModelFlags.hpp"
#include "MetaRender/CCubeRenderer.hpp"

#include <dolphin/gx/GXFrameBuffer.h>
#include <dolphin/gx/GXManage.h>
#include <dolphin/gx/GXTexture.h>
#ifdef TARGET_PC
#include "port_entry.h"
#include "port_mouse.h"
#include "port_overlay.h"
#endif

#ifdef TARGET_PC
// Port: Trilogy's scan lens clock (rs5mp2_p CPlayerVisor +0x660 equivalent): reset as the scan
// visor comes in, advanced by UpdateScanWindow; TriDrawScanLens animates by it.
static float sPortTriLensTime = 0.f;
#endif

static const int skPixelsPerTileDimension16Bit = 4;

static inline int round_up_n(int value, int n) { return (value + n - 1) & ~(n - 1); }

static inline int round_up_to_tile(float value) {
  return round_up_n(static_cast< int >(value), skPixelsPerTileDimension16Bit);
}

CPlayerVisor::CPlayerVisor(const CStateManager& mgr, int playerIndex)
: mPlayerIndex(playerIndex)
, mCurVisor(CPlayerState::kPV_Combat)
, mNextVisor(CPlayerState::kPV_Combat)
, mVisorSfxVol(127)
, mVisorTransitioning(false)
, x29_25_(false)
, mScanTimer(0.f)
, mScanDimInterp(1.f)
, mPrevState(kSWS_NotInScanVisor)
, mNextState(kSWS_NotInScanVisor)
, mWindowInterpDuration(0.f)
, mWindowInterpTimer(0.f)
, mPrevWindowDims(CVector2f::Zero())
, mInterpWindowDims(mPrevWindowDims)
, mNextWindowDims(mPrevWindowDims)
, mScanMagInterp(1.f)
, mVpScaleX(1.f)
, mVpScaleY(1.f)
, mScanFrameFixedCorner(gpSimplePool->GetObj("CMDL_ScanFrame2FixedCorner"))
, mScanFrameCenterLeft(gpSimplePool->GetObj("CMDL_ScanFrame2CenterLeft"))
, mScanFrameCenterTop(gpSimplePool->GetObj("CMDL_ScanFrame2CenterTop"))
, mScanFrameBottomLeftCorner(gpSimplePool->GetObj("CMDL_ScanFrame2BottomLeftCorner"))
, mScanFrameStretchCorner(gpSimplePool->GetObj("CMDL_ScanFrame2StretchCorner"))
, mScanFrameLowerRight(gpSimplePool->GetObj("CMDL_ScanFrame2LowerRight"))
, mScanFrameWindow(gpSimplePool->GetObj("CMDL_ScanFrame2Window"))
, mAssetLockCountdown(0)
, mScanTargets(64, SScanObjectIndicatorInfo(kInvalidUniqueId, 0.f, 0.f))
, mScanFrameColorInterp(0.f)
, mScanFrameColorImpulseInterp(0.f) {
  mScanWindowSizes.push_back(CVector2f::Zero());
  const CTweakGui& tweakGui = *gpTweakGui;
  const float idleHeight = tweakGui.GetScanWindowIdleHeight();
  const float idleWidth = tweakGui.GetScanWindowIdleWidth();
  mScanWindowSizes.push_back(CVector2f(idleWidth, idleHeight));
  const CTweakGui& tweakGui2 = *gpTweakGui;
  const float activeHeight = tweakGui2.GetScanWindowActiveHeight();
  const float activeWidth = tweakGui2.GetScanWindowActiveWidth();
  mScanWindowSizes.push_back(CVector2f(activeWidth, activeHeight));
}

CPlayerVisor::~CPlayerVisor() {
  CSfxManager::SfxStop(mVisorLoopSfx);
  CSfxManager::SfxStop(mScanningLoopSfx);
}

float CPlayerVisor::GetDesiredViewportScaleX(const CStateManager& mgr) const {
  return mgr.GetPlayerState(mPlayerIndex)->GetActiveVisor(mgr) == CPlayerState::kPV_Combat
             ? 1.f
             : mVpScaleX;
}

float CPlayerVisor::GetDesiredViewportScaleY(const CStateManager& mgr) const {
  return mgr.GetPlayerState(mPlayerIndex)->GetActiveVisor(mgr) == CPlayerState::kPV_Combat
             ? 1.f
             : mVpScaleY;
}

void CPlayerVisor::BeginTransitionOut(const CStateManager& mgr) {
  if (mVisorLoopSfx) {
    CSfxManager::SfxStop(mVisorLoopSfx);
    mVisorLoopSfx.Clear();
  }
  switch (mCurVisor) {
  case CPlayerState::kPV_Combat:
    break;
  case CPlayerState::kPV_Echo:
    CSfxManager::SfxStart(mgr.ReturnFirstIfSingleElseSecond(SFXui_x_visoroff_00_oneshot,
                                                            SFXui2_x_visoroff_00_oneshot),
                          mVisorSfxVol, mgr.GetPlayer(mPlayerIndex)->GetSoundPan(CPlayer::kMSP_4),
                          CSfxManager::kAllAreas, false, false, CSfxManager::kMedPriority);
    break;
  case CPlayerState::kPV_Scan:
    if (mScanningLoopSfx) {
      CSfxManager::SfxStop(mScanningLoopSfx);
      mScanningLoopSfx.Clear();
    }
    CSfxManager::SfxStart(mgr.ReturnFirstIfSingleElseSecond(SFXui_x_visoroff_00_oneshot,
                                                            SFXui2_x_visoroff_00_oneshot),
                          mVisorSfxVol, mgr.GetPlayer(mPlayerIndex)->GetSoundPan(CPlayer::kMSP_4),
                          CSfxManager::kAllAreas, false, false, CSfxManager::kMedPriority);
    break;
  case CPlayerState::kPV_Dark:
    CSfxManager::SfxStart(mgr.ReturnFirstIfSingleElseSecond(SFXui_x_visoroff_00_oneshot,
                                                            SFXui2_x_visoroff_00_oneshot),
                          mVisorSfxVol, mgr.GetPlayer(mPlayerIndex)->GetSoundPan(CPlayer::kMSP_4),
                          CSfxManager::kAllAreas, false, false, CSfxManager::kMedPriority);
    break;
  default:
    break;
  }
}

void CPlayerVisor::FinishTransitionOut(const CStateManager& mgr) {
  switch (mCurVisor) {
  case CPlayerState::kPV_Combat:
    break;
  case CPlayerState::kPV_Scan:
    mScanDim.DisableFilter(0.f);
    mNextState = kSWS_NotInScanVisor;
    mPrevState = kSWS_NotInScanVisor;
    break;
  case CPlayerState::kPV_Echo:
  case CPlayerState::kPV_Dark:
    mBlur.DisableBlur(0.f);
    break;
  default:
    break;
  }
}

void CPlayerVisor::BeginTransitionIn(const CStateManager& mgr) {
  ushort loopSingle = 0xFFFF;
  ushort loopMulti = 0xFFFF;
  switch (mCurVisor) {
  case CPlayerState::kPV_Combat:
    break;
  case CPlayerState::kPV_Scan:
    CSfxManager::SfxStart(
        mgr.ReturnFirstIfSingleElseSecond(SFXui_x_visoron_00_oneshot, SFXui2_x_visoron_00_oneshot),
        mVisorSfxVol, mgr.GetPlayer(mPlayerIndex)->GetSoundPan(CPlayer::kMSP_4),
        CSfxManager::kAllAreas, false, false, CSfxManager::kMedPriority);
    mScanDim.SetFilter(CCameraFilterPass::kFT_Multiply, CCameraFilterPass::kFS_Fullscreen, 0.f,
                       CColor::White(), kInvalidAssetId);
#ifdef TARGET_PC
    sPortTriLensTime = 0.f;
#endif
    break;
  case CPlayerState::kPV_Echo:
    CSfxManager::SfxStart(
        mgr.ReturnFirstIfSingleElseSecond(SFXui_x_visoron_00_oneshot, SFXui2_x_visoron_00_oneshot),
        mVisorSfxVol, mgr.GetPlayer(mPlayerIndex)->GetSoundPan(CPlayer::kMSP_4),
        CSfxManager::kAllAreas, false, false, CSfxManager::kMedPriority);
    break;
  case CPlayerState::kPV_Dark:
    CSfxManager::SfxStart(
        mgr.ReturnFirstIfSingleElseSecond(SFXui_x_visoron_00_oneshot, SFXui2_x_visoron_00_oneshot),
        mVisorSfxVol, mgr.GetPlayer(mPlayerIndex)->GetSoundPan(CPlayer::kMSP_4),
        CSfxManager::kAllAreas, false, false, CSfxManager::kMedPriority);
    loopSingle = SFXui_x_visor_lp_01_looped;
    loopMulti = SFXui2_x_visor_lp_01_looped;
    break;
  default:
    break;
  }
  if (mCurVisor != CPlayerState::kPV_Combat && !mVisorLoopSfx &&
      static_cast< ushort >(mgr.ReturnFirstIfSingleElseSecond(loopSingle, loopMulti)) != 0xFFFF) {
    mVisorLoopSfx = CSfxManager::SfxStart(
        mgr.ReturnFirstIfSingleElseSecond(loopSingle, loopMulti), mVisorSfxVol,
        mgr.GetPlayer(mPlayerIndex)->GetSoundPan(CPlayer::kMSP_4), CSfxManager::kAllAreas, false,
        true, CSfxManager::kMedPriority + 0x10);
  }
}

void CPlayerVisor::FinishTransitionIn(const CStateManager& mgr) {
  ushort loopSingle = 0xFFFF;
  ushort loopMulti = 0xFFFF;
  switch (mCurVisor) {
  case CPlayerState::kPV_Combat:
    mBlur.DisableBlur(0.f);
    break;
  case CPlayerState::kPV_Echo:
    break;
  case CPlayerState::kPV_Scan: {
    const CTweakGuiColors& colors = *gpTweakGuiColors;
    CColor dimColor = CColor::Lerp(colors.GetScanVisorScreenDimColor(),
                                   colors.GetScanVisorHUDLightMultiply(), mScanDimInterp);
    mScanDim.SetFilter(CCameraFilterPass::kFT_Multiply, CCameraFilterPass::kFS_Fullscreen, 0.f,
                       dimColor, kInvalidAssetId);
    loopSingle = 0x36;
    loopMulti = 0x2664;
    break;
  }
  case CPlayerState::kPV_Dark:
  default:
    break;
  }
  if (mCurVisor != CPlayerState::kPV_Combat && !mVisorLoopSfx &&
      static_cast< ushort >(mgr.ReturnFirstIfSingleElseSecond(loopSingle, loopMulti)) != 0xFFFF) {
    mVisorLoopSfx = CSfxManager::SfxStart(
        mgr.ReturnFirstIfSingleElseSecond(loopSingle, loopMulti), mVisorSfxVol,
        mgr.GetPlayer(mPlayerIndex)->GetSoundPan(CPlayer::kMSP_4), CSfxManager::kAllAreas, false,
        true, CSfxManager::kMedPriority + 0x10);
  }
}

void CPlayerVisor::UpdateCurrentVisor(float transFactor) {
  switch (mCurVisor) {
  case CPlayerState::kPV_Combat:
    break;
  case CPlayerState::kPV_Echo:
    break;
  case CPlayerState::kPV_Scan: {
    const CTweakGuiColors& colors = *gpTweakGuiColors;
    CColor dimColor =
        CColor::Lerp(colors.GetScanVisorHUDLightMultiply(), CColor::White(), 1.f - transFactor);
    mScanDim.SetFilter(CCameraFilterPass::kFT_Multiply, CCameraFilterPass::kFS_Fullscreen, 0.f,
                       dimColor, kInvalidAssetId);
    break;
  }
  case CPlayerState::kPV_Dark:
  default:
    break;
  }
}

void CPlayerVisor::Update(float dt, const CStateManager& mgr) {
  mBlur.Update(dt);
  const CPlayerState& playerState = *mgr.GetPlayerState(mPlayerIndex);
  CPlayerState::EPlayerVisor activeVisor = playerState.GetActiveVisor(mgr);
  CPlayerState::EPlayerVisor curVisor = playerState.GetCurrentVisor();
  CPlayerState::EPlayerVisor transVisor = playerState.GetTransitioningVisor();
  CPlayer::EPlayerScanState scanState = mgr.GetPlayer(mPlayerIndex)->GetPlayerScanState();
  bool visorTransitioning = playerState.GetIsVisorTransitioning();
  UpdateScanWindow(dt, mgr);
  if (transVisor != mNextVisor)
    mNextVisor = transVisor;
  LockUnlockAssets();
  if (scanState == CPlayer::kSS_ScanComplete)
    mScanDimInterp = rstl::max_val(0.f, mScanDimInterp - 2.f * dt);
  else
    mScanDimInterp = rstl::min_val(1.f, mScanDimInterp + 2.f * dt);
  if (visorTransitioning) {
    if (!mVisorTransitioning)
      BeginTransitionOut(mgr);
    if (curVisor != mCurVisor) {
      FinishTransitionOut(mgr);
      mCurVisor = curVisor;
      BeginTransitionIn(mgr);
    }
    UpdateCurrentVisor(playerState.GetVisorTransitionFactor());
  } else {
    if (mVisorTransitioning) {
      FinishTransitionIn(mgr);
    } else if (curVisor == CPlayerState::kPV_Scan) {
      const CTweakGuiColors& colors = *gpTweakGuiColors;
      CColor dimColor = CColor::Lerp(colors.GetScanVisorScreenDimColor(),
                                     colors.GetScanVisorHUDLightMultiply(), mScanDimInterp);
      mScanDim.SetFilter(CCameraFilterPass::kFT_Multiply, CCameraFilterPass::kFS_Fullscreen, 0.f,
                         dimColor, kInvalidAssetId);
    }
  }
  mVisorTransitioning = visorTransitioning;
  if (activeVisor != mCurVisor) {
    if (mVisorSfxVol != 0) {
      mVisorSfxVol = 0;
      CSfxManager::SfxVolume(mVisorLoopSfx, mVisorSfxVol);
      CSfxManager::SfxVolume(mScanningLoopSfx, mVisorSfxVol);
    }
  } else {
    if (mVisorSfxVol != 127) {
      mVisorSfxVol = 127;
      CSfxManager::SfxVolume(mVisorLoopSfx, mVisorSfxVol);
      CSfxManager::SfxVolume(mScanningLoopSfx, mVisorSfxVol);
    }
  }
  float scanMag = gpTweakGui->GetScanWindowMagnification();
  if (mScanMagInterp < scanMag)
    mScanMagInterp = rstl::min_val(scanMag, mScanMagInterp + 2.f * dt);
  else
    mScanMagInterp = rstl::max_val(scanMag, mScanMagInterp - 2.f * dt);
}

void CPlayerVisor::Draw(const CStateManager& mgr, const CTargetingManager* tgtMgr) const {
  CGraphics::SetAmbientColor(CColor::White());
  CGraphics::DisableAllLights();
#ifdef TARGET_PC
  // Port: `MP2_NO_VISOR_FX=1` / F1 > HUD draws the world without the visor's own overlay. The light
  // setup above stays, because the draws after this one share it.
  if (PortOverlay::HideVisorEffects()) {
    return;
  }
#endif
  switch (mgr.GetPlayerState(mPlayerIndex)->GetActiveVisor(mgr)) {
  case CPlayerState::kPV_Combat:
    break;
  case CPlayerState::kPV_Echo:
    DrawEchoEffect(mgr);
    break;
  case CPlayerState::kPV_Dark:
    DrawDarkEffect(mgr);
    break;
  case CPlayerState::kPV_Scan:
    DrawScanEffect(mgr, tgtMgr);
    break;
  default:
    break;
  }
}

void CPlayerVisor::DrawDarkEffect(const CStateManager& mgr) const {}

void CPlayerVisor::DrawEchoEffect(const CStateManager& mgr) const {}

static const float skViewportLayoutScale[3] = {1.f, 0.8f, 0.6f};

#ifdef TARGET_PC
// Port (MP1 629170f37): the scan window is authored in pixels of a 448-high 4:3 view. Below 4:3
// the view is narrower than that, so the window shrinks to keep its share of the width (and its
// proportions) instead of growing with the height and overrunning the width. 1 elsewhere.
static float ScanWindowFit(int vpWidth, int vpHeight) {
  if (vpHeight > 0 && vpWidth * 3 < vpHeight * 4) {
    return static_cast< float >(vpWidth) * 3.f / (static_cast< float >(vpHeight) * 4.f);
  }
  return 1.f;
}
#endif

#ifdef TARGET_PC
// Trilogy's scan lens (drawn by its DrawScanEffect, fn_801A3428, in place of the scan dim):
// vignette rings and a flickering outline round the window that morph from a circle into the
// window's square as it opens. Sizes are Wii view pixels, scaled here as the frame's 17 is.

// fn_801A3314: the direction at `angle`, morphed toward the square of half-size `square`.
static CVector2f TriLensDir(float angle, float morph, float square) {
  const float x = CMath::FastCosR(angle);
  const float y = CMath::FastSinR(angle);
  const float s = square / rstl::max_val(fabsf(x), fabsf(y));
  return CVector2f(x * (1.f - morph) + x * s * morph, y * (1.f - morph) + y * s * morph);
}

struct STriLensDir {
  float x;
  float y;
};

static void TriLensVertex(float cx, float cy, float r, const CVector2f& v) {
  gpRender->PrimVertex(CVector3f(cx + r * v.GetX(), 0.f, cy + r * v.GetY()));
}

// fn_801A3148: a band between two radii over degrees start..end, turned by rot.
static void TriLensArc(int start, int end, int rot, const CColor& color, const STriLensDir* table,
                       float cx, float cy, float rIn, float rOut) {
  gpRender->BeginTriangleStrip((end - start + 360) % 360 + 2);
  gpRender->PrimColor(color);
  for (int i = rot + start; i <= rot + end; i += 2) {
    const CVector2f v(table[i % 360].x, table[i % 360].y);
    TriLensVertex(cx, cy, rIn, v);
    TriLensVertex(cx, cy, rOut, v);
  }
  gpRender->EndPrimitive();
}

// t: the visor transition; open: the window's open factor (ScanWindowCopySize's t); cx, cy: the
// lens centre in the -4096..4096 ortho; px: view pixels per Wii pixel.
static void TriDrawScanLens(float t, float open, float cx, float cy, float px) {
  // lbl_804C3528, lbl_80474C58
  static const float kRingRgb[3][3] = {{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, {0.f, 0.2f, 0.3f}};
  static const float kRingClosed[3] = {100.f, 150.f, 400.f};
  static const float kRingOpen[3] = {115.f, 120.f, 400.f};
  static const float kRingAlpha[3] = {0.f, 0.67f, 0.f};
  static const float kNoiseSpeed[4] = {1.f, -1.5f, 2.5f, -3.5f};
  static const float kNoiseFreq[4] = {4.f, 6.f, 10.f, 14.f};
  static const float kNoisePhase[4] = {0.f, 5.f, 7.f, 6.f};
  const float time = sPortTriLensTime;
  float radius[3];
  for (int i = 0; i < 3; ++i)
    radius[i] = px * ((1.f - open) * kRingClosed[i] + open * kRingOpen[i]);
  gpRender->SetDepthReadWrite(false, false);
  gpRender->SetBlendMode_AlphaBlended();
  for (int ring = 0; ring < 2; ++ring) {
    const float* rgbIn = kRingRgb[ring];
    const float* rgbOut = kRingRgb[ring + 1];
    const CColor inner(rgbIn[0], rgbIn[1], rgbIn[2], t * kRingAlpha[ring]);
    const CColor outer(rgbOut[0], rgbOut[1], rgbOut[2], t * kRingAlpha[ring + 1]);
    gpRender->BeginTriangleStrip(74);
    for (int i = 0; i <= 36; ++i) {
      const CVector2f d = TriLensDir(static_cast< float >(i % 36) * 0.17453292f, open, 0.72f);
      gpRender->PrimColor(inner);
      TriLensVertex(cx, cy, radius[ring], d);
      gpRender->PrimColor(outer);
      // The outer ring's rim is about the view's centre, not the lens's (as shipped).
      if (ring == 0)
        TriLensVertex(cx, cy, radius[1], d);
      else
        TriLensVertex(0.f, 0.f, radius[2], d);
    }
    gpRender->EndPrimitive();
  }
  const CColor arcColor(0.5f, 0.8f, 1.f, 0.3f * t * (1.f - open));
  const CColor glowColor(0.2f, 0.2f, 0.2f, 0.1f * t);
  const CColor clearColor(0.f, 0.f, 0.f, 0.f);
  const float shadowRadius = px * 400.f * ((1.f - open) + open * 1.3888888f);
  STriLensDir table[360];
  bool on[360];
  for (int deg = 0; deg < 360; ++deg) {
    const float angle = static_cast< float >(deg) * 0.017453292f;
    const CVector2f dir = TriLensDir(angle, open, 0.72f);
    table[deg].x = dir.GetX();
    table[deg].y = dir.GetY();
    float noise = 0.f;
    for (int i = 0; i < 4; ++i)
      noise += CMath::FastSinR(kNoisePhase[i] + time * kNoiseSpeed[i] + kNoiseFreq[i] * angle);
    on[deg] = noise > 0.5f;
  }
  const int rot1 = static_cast< int >(30.f * time) % 360;
  TriLensArc(0, 160, rot1, arcColor, table, cx, cy, px * 115.f, px * 120.f);
  TriLensArc(200, 246, rot1, arcColor, table, cx, cy, px * 115.f, px * 120.f);
  TriLensArc(250, 350, rot1, arcColor, table, cx, cy, px * 115.f, px * 120.f);
  const CColor& active = gpTweakGuiColors->GetScanWindowFrameActiveColor();
  const CColor activeArc = active.WithAlphaOf(active.GetAlpha() * (t * (1.f - open)));
  const int rot2 = 360 - static_cast< int >(45.f * time) % 360;
  TriLensArc(100, 270, rot2, activeArc, table, cx, cy, px * 110.f, px * 112.f);
  TriLensArc(290, 440, rot2, activeArc, table, cx, cy, px * 110.f, px * 112.f);
  // The outline: each run of lit degrees is a band with a glow out to the shadow's radius and
  // a tick at its middle as long as the run.
  const float rIn = px * 122.f;
  const float rOut = px * 124.f;
  for (int deg = 0; deg < 360; ++deg) {
    if (on[(deg + 359) % 360] || !on[deg])
      continue;
    int n = 1;
    while (on[(deg + n) % 360] && n < 360)
      ++n;
    const int count = (n / 2 + 1) * 2;
    gpRender->BeginTriangleStrip(count);
    gpRender->PrimColor(arcColor);
    for (int j = 0; j <= n; j += 2) {
      const CVector2f v(table[(deg + j) % 360].x, table[(deg + j) % 360].y);
      TriLensVertex(cx, cy, rIn, v);
      TriLensVertex(cx, cy, rOut, v);
    }
    gpRender->EndPrimitive();
    gpRender->BeginTriangleStrip(count);
    for (int j = 0; j <= n; j += 2) {
      const CVector2f v(table[(deg + j) % 360].x, table[(deg + j) % 360].y);
      gpRender->PrimColor(glowColor);
      TriLensVertex(cx, cy, rOut, v);
      gpRender->PrimColor(clearColor);
      TriLensVertex(0.f, 0.f, shadowRadius, v);
    }
    gpRender->EndPrimitive();
    const float vx = table[(deg + n / 2) % 360].x;
    const float vy = table[(deg + n / 2) % 360].y;
    const float baseX = cx + rOut * vx;
    const float baseY = cy + rOut * vy;
    const float tipR = rOut + px * static_cast< float >(n);
    const float tipX = cx + tipR * vx;
    const float tipY = cy + tipR * vy;
    // Across the band (3 either side at the base, 1 at the tip), set 0.5 inside it.
    const float baseW = px * 3.f;
    const float tipW = px * 1.f;
    const float in = px * -0.5f;
    gpRender->BeginTriangleStrip(4);
    gpRender->PrimColor(arcColor);
    gpRender->PrimVertex(CVector3f(baseX - baseW * vy + in * vx, 0.f, baseY + baseW * vx + in * vy));
    gpRender->PrimVertex(CVector3f(baseX + baseW * vy + in * vx, 0.f, baseY - baseW * vx + in * vy));
    gpRender->PrimVertex(CVector3f(tipX - tipW * vy, 0.f, tipY + tipW * vx));
    gpRender->PrimVertex(CVector3f(tipX + tipW * vy, 0.f, tipY - tipW * vx));
    gpRender->EndPrimitive();
  }
}
#endif

void CPlayerVisor::DrawScanEffect(const CStateManager& mgr,
                                  const CTargetingManager* const tgtMgr) const {
  const CViewport& viewport = CGraphics::GetViewport();
  const int vpLeft = viewport.mLeft;
  const int vpTop = viewport.mTop;
  const int vpWidth = viewport.mWidth;
  const int vpHeight = viewport.mHeight;
  const float transFactor = mgr.GetPlayerState(mPlayerIndex)->GetVisorTransitionFactor();
  const float scanSidesStart = gpTweakGui->GetScanSidesStartTime();
  const float scanSidesDuration = gpTweakGui->GetScanSidesDuration();
  float t;
  if (mNextState == kSWS_Scan)
    t = 1.f - (mWindowInterpTimer < scanSidesDuration
                   ? 0.f
                   : (mWindowInterpTimer - scanSidesDuration) / scanSidesStart);
  else
    t = mWindowInterpTimer > scanSidesStart ? 1.f : mWindowInterpTimer / scanSidesStart;
  const float divisor =
      transFactor * ((1.f - t) * mScanMagInterp + t * gpTweakGui->GetScanWindowScanningAspect()) +
      (1.f - transFactor);
  const float layoutScale = skViewportLayoutScale[mgr.GetViewportLayoutIndex()];
  const float windowX = mInterpWindowDims.GetX();
  const float windowY = mInterpWindowDims.GetY();
#ifdef TARGET_PC
  const float fit = ScanWindowFit(vpWidth, vpHeight);
  const float vpW = 139.35576f * fit * windowX;
  const float vpH = 125.36f * fit * windowY;
#else
  const float vpW = 139.35576f * windowX;
  const float vpH = 125.36f * windowY;
#endif
  const int width = CMath::Clamp(skPixelsPerTileDimension16Bit,
                                 round_up_to_tile(layoutScale * (vpW / divisor)), vpWidth);
  const int height = CMath::Clamp(skPixelsPerTileDimension16Bit,
                                  round_up_to_tile(layoutScale * (vpH / divisor)), vpHeight);
#ifdef TARGET_PC
  // Pointer aim: the scan window is the pointer's cursor. It magnifies the screen around the
  // pointer (half-view units, +y down) and the scan zone moves with it (CPlayerOrbit.cpp). Locked,
  // the pointer is held at the centre and so is the window.
  float pointerX = 0.f;
  float pointerY = 0.f;
  const bool portPointer = PortMouse::PointerAimPosition(pointerX, pointerY, static_cast< int >(mPlayerIndex));
  // The window's centre in pixels from the view's centre (+y down).
  const float portShiftX = portPointer ? pointerX * 0.5f * static_cast< float >(vpWidth) : 0.f;
  const float portShiftY = portPointer ? pointerY * 0.5f * static_cast< float >(vpHeight) : 0.f;
  // The copy is centred on the window, kept inside the view.
  GXSetTexCopySrc(
      CMath::Clamp(vpLeft, (vpLeft + (vpWidth - width) / 2 + static_cast< int >(portShiftX)) & ~1,
                   vpLeft + vpWidth - width),
      CMath::Clamp(vpTop, (vpTop + (vpHeight - height) / 2 + static_cast< int >(portShiftY)) & ~1,
                   vpTop + vpHeight - height),
      width, height);
#else
  GXSetTexCopySrc(vpLeft + (vpWidth - width) / 2, vpTop + (vpHeight - height) / 2, width, height);
#endif
#ifdef TARGET_PC
  // The scan window's pane samples these bytes as a texture, so the console's copy wrote them to
  // main memory too - and this copy shares the spare buffer with the visor overlay's viewport copy
  // (an A8 one) issued in the same frame. Take this copy's own destination, so Aurora resolves this
  // copy into the pane's texture rather than into whichever copy into the spare buffer came last.
  // Aurora then draws the pane from that copy texture and never reads the bytes, so no CPU
  // read-back is asked for: it copied a full render-scale frame back every frame, for nothing.
  void* const buffer = PortEfbCopy::Destination(CGraphics::GetDolphinSpareBuffer(), width, height, 2);
  GXSetTexCopyDst(width, height, GX_TF_RGB565, GX_FALSE);
#else
  void* const buffer = CGraphics::GetDolphinSpareBuffer();
  GXSetTexCopyDst(width, height, GX_TF_RGB565, GX_FALSE);
#endif
  GXCopyTex(buffer, GX_FALSE);
  GXPixModeSync();
#ifdef TARGET_PC
  // Trilogy (rs5mp2_p DrawScanEffect 0x80170e0c): the lens replaces the dim, and the window and
  // its frame are drawn only while the window is open (r31 gate at 0x801723e8).
  const bool trilogy = port::entry::IsTrilogy(port::entry::CurrentDiscVersion());
  const float windowOpen = t;
  const float windowFactor = trilogy ? windowOpen : 1.f;
  const float windowAlpha = transFactor * windowFactor;
  if (trilogy) {
    gpRender->SetViewportOrtho(true, -4096.f, 4096.f);
    TriDrawScanLens(transFactor, windowOpen, portPointer ? portShiftX : 0.f,
                    portPointer ? -portShiftY : 0.f, fit);
  }
  if (!trilogy)
#endif
  mScanDim.Draw();
  gpRender->SetViewportOrtho(true, -1.f, 1.f);
#ifdef TARGET_PC
  if (trilogy && fabsf(windowOpen) < 0.00001f)
    return;
  // Moving the view the other way draws the window and its frame around the pointer.
  if (portPointer)
    CGraphics::SetViewPointMatrix(CTransform4f::Translate(-portShiftX, 0.f, portShiftY));
#endif
  const CTransform4f windowScale = CTransform4f::Scale(windowX, 1.f, windowY);
#ifdef TARGET_PC
  // Port (MP1 629170f37): the frame and the pane's clip shield scale with the window.
  const CTransform4f seventeenScale =
      CTransform4f::Scale(14.f * layoutScale * fit, 1.f, 14.f * layoutScale * fit);
#else
  const CTransform4f seventeenScale =
      CTransform4f::Scale(14.f * layoutScale, 1.f, 14.f * layoutScale);
#endif
  const CTransform4f mm = seventeenScale * windowScale;
  const CTransform4f verticalFlip = CTransform4f::Scale(1.f, 1.f, -1.f);
  const CTransform4f horizontalFlip = CTransform4f::Scale(-1.f, 1.f, 1.f);
  gpRender->SetModelMatrix(mm);
  if (!CMath::IsEpsilon(mScanDimInterp, 0.f, 0.00001f)) {
    CGraphics::LoadDolphinSpareTexture(width, height, GX_TF_RGB565, buffer, GX_TEXMAP0);
    GXInvalidateTexAll();
    const CColor paneColor =
        CColor::Lerp(gpTweakGuiColors->GetScanWindowTintColor(), CColor::White(), mScanDimInterp)
#ifdef TARGET_PC
            .WithAlphaOf(windowAlpha);
#else
            .WithAlphaOf(transFactor);
#endif
    if (const CModel* pane = mScanFrameWindow.GetObject())
      pane->Draw(CModelFlags::AlphaBlended(paneColor).DontLoadTextures());
  }
  if (!CMath::IsEpsilon(mScanDimInterp, 1.f, 0.00001f)) {
#ifdef TARGET_PC
    const float paneAlpha = (1.f - mScanDimInterp) * windowFactor;
#else
    const float paneAlpha = 1.f - mScanDimInterp;
#endif
    const CColor paneColor = CColor::White().WithAlphaOf(paneAlpha);
    if (const CModel* pane = mScanFrameWindow.GetObject())
      pane->Draw(CModelFlags::AlphaBlended(paneColor).DepthCompareUpdate(false, false));
  }
  CGraphics::SetCullMode(kCM_None);
  CColor frameColor =
      CColor::Lerp(gpTweakGuiColors->GetScanWindowFrameBaseColor(),
                   gpTweakGuiColors->GetScanWindowFrameActiveColor(), mScanFrameColorInterp);
#ifdef TARGET_PC
  frameColor = frameColor.WithAlphaOf(windowAlpha);
#else
  frameColor = frameColor.WithAlphaOf(transFactor);
#endif
  const CColor impulseColor =
      CColor::Modulate(gpTweakGuiColors->GetScanWindowFrameFlashAddColor(),
                       CColor(mScanFrameColorImpulseInterp, mScanFrameColorImpulseInterp,
                              mScanFrameColorImpulseInterp, mScanFrameColorImpulseInterp));
  frameColor = CColor::Add(frameColor, impulseColor);
  const CModelFlags flags = CModelFlags::AlphaBlended(frameColor).DepthCompareUpdate(false, false);
  const float xScale = windowScale.Get00();
  const CVector3f up = windowScale.GetColumn(kDZ);
  const float zScale = up.GetZ();
  const CVector3f topPosition(0.f, 0.f, 4.447f * zScale);
  if (const CModel* model = mScanFrameCenterTop.GetObject()) {
    const CTransform4f modelXf =
        seventeenScale * CTransform4f::Translate(topPosition) *
        CTransform4f::Scale(2.f * (4.977f * xScale + 1.f - 2.432f) / 6.464f, 1.f, 1.f);
    gpRender->SetModelMatrix(modelXf);
    model->Draw(flags);
  }
  const CVector3f sidePosition(-4.977f * xScale, 0.f, 0.f);
  if (const CModel* model = mScanFrameCenterLeft.GetObject()) {
    const CTransform4f modelXf =
        seventeenScale * CTransform4f::Translate(sidePosition) *
        CTransform4f::Scale(1.f, 1.f, 2.f * (1.f + 4.447f * zScale - 2.233f) / 6.396743f);
    gpRender->SetModelMatrix(modelXf);
    model->Draw(flags);
    gpRender->SetModelMatrix(horizontalFlip * modelXf);
    model->Draw(flags);
  }
  const CVector3f cornerOffset(-4.977f, 0.f, 4.447f);
  const CVector3f cornerPosition = windowScale * cornerOffset;
  if (const CModel* model = mScanFrameFixedCorner.GetObject()) {
    const CTransform4f modelXf = seventeenScale * CTransform4f::Translate(cornerPosition);
    gpRender->SetModelMatrix(modelXf);
    model->Draw(flags);
    gpRender->SetModelMatrix(horizontalFlip * modelXf);
    model->Draw(flags);
  }
  const CVector3f bottomCornerOffset(-4.977f, 0.f, -4.447f);
  const CVector3f bottomCornerPosition = windowScale * bottomCornerOffset;
  if (const CModel* model = mScanFrameBottomLeftCorner.GetObject()) {
    const CTransform4f modelXf = seventeenScale * CTransform4f::Translate(bottomCornerPosition);
    gpRender->SetModelMatrix(modelXf);
    model->Draw(flags);
    gpRender->SetModelMatrix(horizontalFlip * modelXf);
    model->Draw(flags);
  }
  const float stretchWidth = (2.f * (4.977f * xScale - 0.4f) - 8.697885f) * 0.5f;
  const CVector3f stretchPosition(-4.3489423f, 0.f, 4.447f * -zScale);
  if (const CModel* model = mScanFrameStretchCorner.GetObject()) {
    const CTransform4f modelXf = seventeenScale * CTransform4f::Translate(stretchPosition) *
                                 CTransform4f::Scale(stretchWidth / 0.151104f, 1.f, 1.f);
    gpRender->SetModelMatrix(modelXf);
    model->Draw(flags);
    gpRender->SetModelMatrix(horizontalFlip * modelXf);
    model->Draw(flags);
  }
  const CVector3f lowerPosition(0.f, 0.f, 4.447f * -zScale);
  if (const CModel* model = mScanFrameLowerRight.GetObject()) {
    const CTransform4f modelXf = seventeenScale * CTransform4f::Translate(lowerPosition);
    gpRender->SetModelMatrix(modelXf);
    model->Draw(flags);
  }
#ifdef TARGET_PC
  if (portPointer)
    CGraphics::SetViewPointMatrix(CTransform4f::Identity());
#endif
  CGraphics::SetCullMode(kCM_Front);
}

void CPlayerVisor::LockUnlockAssets() {
  if (mCurVisor == CPlayerState::kPV_Scan)
    mAssetLockCountdown = 2;
  else
    --mAssetLockCountdown;
  if (mAssetLockCountdown > 0) {
    mScanFrameFixedCorner.Lock();
    mScanFrameCenterLeft.Lock();
    mScanFrameCenterTop.Lock();
    mScanFrameWindow.Lock();
    mScanFrameBottomLeftCorner.Lock();
    mScanFrameStretchCorner.Lock();
    mScanFrameLowerRight.Lock();
    mScanFrameFixedCorner.TryCache();
    mScanFrameCenterLeft.TryCache();
    mScanFrameCenterTop.TryCache();
    mScanFrameWindow.TryCache();
    mScanFrameBottomLeftCorner.TryCache();
    mScanFrameStretchCorner.TryCache();
    mScanFrameLowerRight.TryCache();
  } else {
    mScanFrameFixedCorner.Unlock();
    mScanFrameCenterLeft.Unlock();
    mScanFrameCenterTop.Unlock();
    mScanFrameWindow.Unlock();
    mScanFrameBottomLeftCorner.Unlock();
    mScanFrameStretchCorner.Unlock();
    mScanFrameLowerRight.Unlock();
  }
}

CPlayerVisor::EScanWindowState
CPlayerVisor::GetDesiredScanWindowState(const CStateManager& mgr) const {
  const CPlayer* player = mgr.GetPlayer(mPlayerIndex);
  CPlayerState::EPlayerVisor visor = player->GetPlayerState()->GetCurrentVisor();
  CPlayer::EPlayerScanState scanState = player->GetPlayerScanState();
  if (visor == CPlayerState::kPV_Scan) {
    if (scanState == CPlayer::kSS_Scanning || scanState == CPlayer::kSS_ScanComplete)
      return kSWS_Scan;
    return kSWS_Idle;
  }
  return kSWS_NotInScanVisor;
}

void CPlayerVisor::UpdateScanWindow(float dt, const CStateManager& mgr) {
#ifdef TARGET_PC
  sPortTriLensTime += dt;
#endif
  const CPlayer* player = mgr.GetPlayer(mPlayerIndex);
  CPlayer::EPlayerScanState scanState = player->GetPlayerScanState();
  float scanTimer = player->GetScanningTime();
  if (scanState == CPlayer::kSS_Scanning) {
    if (!mScanningLoopSfx) {
      if (mgr.IsMultiplayer()) {
        mScanningLoopSfx = CSfxManager::SfxStart(
            SFXsa2_a_virus_lp_00_looped, mVisorSfxVol, player->GetSoundPan(CPlayer::kMSP_4),
            CSfxManager::kAllAreas, false, true, CSfxManager::kMedPriority);
      } else {
        mScanningLoopSfx = CSfxManager::SfxStart(
            SFXui_x_download_lp_00_looped, mVisorSfxVol, player->GetSoundPan(CPlayer::kMSP_4),
            CSfxManager::kAllAreas, false, true, CSfxManager::kMedPriority);
      }
    }
  } else {
    CSfxManager::SfxStop(CSfxManager::kSC_Game, mScanningLoopSfx);
    mScanningLoopSfx.Clear();
  }
  const bool scanTimerChanged = mScanTimer != scanTimer;
  mScanTimer = scanTimer;
  EScanWindowState desiredState = GetDesiredScanWindowState(mgr);
  switch (mNextState) {
  case kSWS_NotInScanVisor:
    if (desiredState != kSWS_NotInScanVisor) {
      if (mPrevState == kSWS_NotInScanVisor)
        mInterpWindowDims = mScanWindowSizes[desiredState];
      mNextWindowDims = mScanWindowSizes[desiredState];
      mPrevWindowDims = mInterpWindowDims;
      mPrevState = mNextState;
      mNextState = desiredState;
      mWindowInterpDuration =
          desiredState == kSWS_Scan ? gpTweakGui->GetScanSidesEndTime() - mWindowInterpTimer : 0.f;
      mWindowInterpTimer = mWindowInterpDuration;
    }
    break;
  case kSWS_Idle:
    if (desiredState != kSWS_Idle) {
      mNextWindowDims =
          desiredState == kSWS_NotInScanVisor ? mInterpWindowDims : mScanWindowSizes[desiredState];
      mPrevWindowDims = mInterpWindowDims;
      mPrevState = mNextState;
      mNextState = desiredState;
      mWindowInterpDuration =
          desiredState == kSWS_Scan ? gpTweakGui->GetScanSidesEndTime() - mWindowInterpTimer : 0.f;
      mWindowInterpTimer = mWindowInterpDuration;
      if (desiredState == kSWS_Scan)
        CSfxManager::SfxStart(SFXui_x_scanon_00_oneshot, mVisorSfxVol,
                              player->GetSoundPan(CPlayer::kMSP_4), CSfxManager::kAllAreas, false,
                              false, CSfxManager::kMedPriority);
    }
    break;
  case kSWS_Scan:
    if (desiredState != kSWS_Scan) {
      mNextWindowDims =
          desiredState == kSWS_NotInScanVisor ? mInterpWindowDims : mScanWindowSizes[desiredState];
      mPrevWindowDims = mInterpWindowDims;
      mPrevState = mNextState;
      mNextState = desiredState;
      mWindowInterpDuration =
          desiredState == kSWS_Idle ? gpTweakGui->GetScanSidesEndTime() - mWindowInterpTimer : 0.f;
      mWindowInterpTimer = mWindowInterpDuration;
      if (mgr.GetPlayerState(mPlayerIndex)->GetVisorTransitionFactor() == 1.f &&
          !mgr.IsMultiplayer())
        CSfxManager::SfxStart(SFXui_x_scanoff_00_oneshot, mVisorSfxVol,
                              player->GetSoundPan(CPlayer::kMSP_4), CSfxManager::kAllAreas, false,
                              false, CSfxManager::kMedPriority);
    }
    if (scanTimerChanged && mgr.IsMultiplayer() &&
        player->GetPlayerScanState() == CPlayer::kSS_ScanComplete)
      CSfxManager::SfxStart(SFXsa2_a_virusaff_00_oneshot, mVisorSfxVol,
                            player->GetSoundPan(CPlayer::kMSP_4), CSfxManager::kAllAreas, false,
                            false, CSfxManager::kMedPriority);
    break;
  default:
    break;
  }
  if (mPrevState != mNextState) {
    mWindowInterpTimer = rstl::max_val(0.f, mWindowInterpTimer - dt);
    if (mWindowInterpTimer == 0.f)
      mPrevState = mNextState;
    float t = 0.f;
    if (mWindowInterpDuration > 0.f) {
      float scanSidesStart = gpTweakGui->GetScanSidesStartTime();
      float scanSidesDuration = gpTweakGui->GetScanSidesDuration();
      if (mNextState == kSWS_Scan)
        t = mWindowInterpTimer < scanSidesDuration
                ? 0.f
                : (mWindowInterpTimer - scanSidesDuration) / scanSidesStart;
      else
        t = mWindowInterpTimer > scanSidesStart ? 1.f : mWindowInterpTimer / scanSidesStart;
    }
    mInterpWindowDims = CVector2f::Lerp(mNextWindowDims, mPrevWindowDims, t);
  }
}

CVector2i CPlayerVisor::GetScanWindowViewportSize(const CStateManager& mgr) const {
  const CViewport& viewport = CGraphics::GetViewport();
  int vpWidth = viewport.mWidth;
  int vpHeight = viewport.mHeight;
  const float transFactor = mgr.GetPlayerState(mPlayerIndex)->GetVisorTransitionFactor();
  const float scanSidesStart = gpTweakGui->GetScanSidesStartTime();
  const float scanSidesDuration = gpTweakGui->GetScanSidesDuration();
  float t;
  if (mNextState == kSWS_Scan)
    t = 1.f - (mWindowInterpTimer < scanSidesDuration
                   ? 0.f
                   : (mWindowInterpTimer - scanSidesDuration) / scanSidesStart);
  else
    t = mWindowInterpTimer > scanSidesStart ? 1.f : mWindowInterpTimer / scanSidesStart;
  const float divisor =
      transFactor * ((1.f - t) * mScanMagInterp + t * gpTweakGui->GetScanWindowScanningAspect()) +
      (1.f - transFactor);
  const float layoutScale = skViewportLayoutScale[mgr.GetViewportLayoutIndex()];
  const float windowX = mInterpWindowDims.GetX();
  const float windowY = mInterpWindowDims.GetY();
  const float vpW = layoutScale * (139.35576f * windowX);
  const float vpH = layoutScale * (125.36f * windowY);
  const int width =
      CMath::Clamp(skPixelsPerTileDimension16Bit, round_up_to_tile(vpW / divisor), vpWidth);
  const int height =
      CMath::Clamp(skPixelsPerTileDimension16Bit, round_up_to_tile(vpH / divisor), vpHeight);
  return CVector2i(width, height);
}
