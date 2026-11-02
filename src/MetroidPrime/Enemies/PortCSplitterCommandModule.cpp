// Host body for the Splitter REL (module 75)'s command module - the "Quad" robot's detachable
// head, script type SPLU. Milestone 1: the actor loads, registers and thinks. The analysis is
// docs/module-analysis/Splitter.md; the module's entry points live in PortCSplitterMainChassis.cpp
// (the two files are the one REL's body).
//
// The accessors at the head of the module's .text are the REL's own one- or two-instruction bodies
// (fn_75_0, fn_75_8, fn_75_10, fn_75_18, fn_75_64, fn_75_6C, fn_75_74), so they are real rather than
// stand-ins; the bodies that belong to the state machine, the scanning, the laser pulse and sweep,
// the shields and the docking forward to CPatterned behind a once-only StandIn log, and the retail
// function each stands in for is named on it.
#include "MetroidPrime/Enemies/CSplitterCommandModule.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/Enemies/CSplitterMainChassis.hpp"
#include "MetroidPrime/TCastTo.hpp"

#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CParticleGenInfo.hpp"
#include "MetroidPrime/Collision/CJointCollisionDescription.hpp"
#include "Collision/CCollisionInfoList.hpp"

#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/Math/CRelAngle.hpp"

#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Weapons/CWeaponAssetInfo.hpp"
#include "MetroidPrime/Weapons/CBeamInfo.hpp"
#include "MetroidPrime/Weapons/CPlasmaProjectile.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/Enemies/CPatternedInfo.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrSplitterCommandModule.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"

#include "Kyoto/CRandom16.hpp"
#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "Kyoto/Graphics/CTexture.hpp"
#include "MetroidPrime/CActorParameters.hpp"
#include "MetroidPrime/CModelData.hpp"

#include "Collision/CMaterialFilter.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CGX.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Graphics/CModel.hpp"
#include "Kyoto/Math/CFrustumPlanes.hpp"
#include "Kyoto/PVS/CPVSVisOctree.hpp"
#include "Kyoto/PVS/CPVSVisSet.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#include "WorldFormat/CPVSAreaSet.hpp"

#include <dolphin/gx.h>
#include <dolphin/gx/GXCpu2Efb.h>

#include <atomic>
#include <cstring>
#include <math.h>
#include <stdio.h>

namespace {

// Reports a stand-in the first time it runs, so a log lists what is still a shell.
void StandIn(const char* what) {
  static const char* sSeen[256] = {};
  static int sCount = 0;
  for (int i = 0; i < sCount; ++i) {
    if (sSeen[i] == what) {
      return;
    }
  }
  if (sCount < 256) {
    sSeen[sCount++] = what;
  }
  fprintf(stderr, "[splitter] %s is a port stand-in (see PortCSplitterCommandModule.cpp)\n",
          what);
}

// The head's FSM names: the REL's lbl_75_data_CC (the 17 triggers),
// lbl_75_data_284 (the 14 states) and lbl_75_data_3B8 (the 7 codes). The tables'
// entries are 16 bytes {const char* name, 0, 0xFFFFFFFF, fn} and the module's static
// initialiser (fn_75_7C80, in the REL's .ctors) fills the function word from the
// three-word records of .data 0x0..0x2D0, so the retail function of each name is
// known (docs/module-analysis/Splitter.md section b). The ported ones are the IsScanning
// trigger (fn_75_58E8), the IsInitiallyDocked (fn_75_58DC), the IsDocked (fn_75_58BC), the
// HasDockingTarget (fn_75_57F0), the HasTarget (fn_75_57B4) and the InDetectionRange
// (fn_75_5944) triggers, the Start (fn_75_5564) and the Scanning (fn_75_523C) states, the NotifyDocking code (fn_75_3EEC) and the RaiseShields code (fn_75_3B20);
// the rest are once-logging shells for the retail function they name.
// fn_75_7C80 is straight-line code (no arms): `tools/fsm_ctor_tables.py <asm dir> fn_75_7C80
// lbl_75_data_CC:17 lbl_75_data_284:14 lbl_75_data_3B8:7` replays it (134 stores, every entry
// {0, 0xFFFFFFFF, fn}) and the names and functions match the tables below in order, so the marker
// sits above them (the MinorIng fn_44_ABCC precedent).
#define SPLITTER_TRIGGER(name) \
  { #name, static_cast< CPatterned::StateMachine::TriggerFunc >(&CSplitterCommandModule::name) }
#define SPLITTER_STATE(name) \
  { #name, static_cast< CPatterned::StateMachine::StateFunc >(&CSplitterCommandModule::name) }
#define SPLITTER_CODE(name) \
  { #name, static_cast< CPatterned::StateMachine::CodeFunc >(&CSplitterCommandModule::name) }
#define SPLITTER_TRIGGER_PORTED(name) \
  { #name, static_cast< CPatterned::StateMachine::TriggerFunc >(&CSplitterCommandModule::name) }
#define SPLITTER_STATE_PORTED(name) \
  { #name, static_cast< CPatterned::StateMachine::StateFunc >(&CSplitterCommandModule::name) }
#define SPLITTER_TRIGGER_PORTED(name) \
  { #name, static_cast< CPatterned::StateMachine::TriggerFunc >(&CSplitterCommandModule::name) }
#define SPLITTER_CODE_PORTED(name) \
  { #name, static_cast< CPatterned::StateMachine::CodeFunc >(&CSplitterCommandModule::name) }


// @ fn_75_7C80
CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    SPLITTER_TRIGGER(StateOver),
    SPLITTER_TRIGGER_PORTED(InDetectionRange),
    SPLITTER_TRIGGER_PORTED(IsScanning),
    SPLITTER_TRIGGER_PORTED(IsInitiallyDocked),
    SPLITTER_TRIGGER_PORTED(IsDocked),
    SPLITTER_TRIGGER(HasDockingPath),
    SPLITTER_TRIGGER_PORTED(HasDockingTarget),
    SPLITTER_TRIGGER(DockingPathOver),
    SPLITTER_TRIGGER_PORTED(HasTarget),
    SPLITTER_TRIGGER(InHoverRange),
    SPLITTER_TRIGGER(InLaserPulseRange),
    SPLITTER_TRIGGER(ShouldDodge),
    SPLITTER_TRIGGER(ShouldAttack),
    SPLITTER_TRIGGER(ShouldFireAgain),
    SPLITTER_TRIGGER(ShouldLaserSweep),
    SPLITTER_TRIGGER(PathOver),
    SPLITTER_TRIGGER(PathShagged),
};

CPatterned::StateMachine::SStateFunction skStates[] = {
    SPLITTER_STATE_PORTED(Start),
    SPLITTER_STATE_PORTED(Idle),
    SPLITTER_STATE(SpawnIdle),
    SPLITTER_STATE(Dead),
    SPLITTER_STATE_PORTED(Scanning),
    SPLITTER_STATE(FaceTarget),
    SPLITTER_STATE(PathFind),
    SPLITTER_STATE_PORTED(Hover),
    SPLITTER_STATE(Dodge),
    SPLITTER_STATE(LaserPulse),
    SPLITTER_STATE(LaserSweep),
    SPLITTER_STATE(LostChassisReaction),
    SPLITTER_STATE(SeekMainChassis),
    SPLITTER_STATE(FollowDockingPath),
};

CPatterned::StateMachine::SCodeFunction skCodeFuncs[] = {
    SPLITTER_CODE_PORTED(NotifyDocking),
    SPLITTER_CODE_PORTED(SelectTarget),
    SPLITTER_CODE(SetTargetDest),
    SPLITTER_CODE(SetDockingDest),
    SPLITTER_CODE(FindBestDodgeDirection),
    SPLITTER_CODE_PORTED(RaiseShields),
    SPLITTER_CODE(ResetAttackTimes),
};

#undef SPLITTER_TRIGGER
#undef SPLITTER_STATE
#undef SPLITTER_CODE
#undef SPLITTER_TRIGGER_PORTED
#undef SPLITTER_STATE_PORTED
#undef SPLITTER_TRIGGER_PORTED
#undef SPLITTER_CODE_PORTED


// The module's own per-frame calls, as named once-logging stand-ins: the name is the retail
// function and its size.

// The module's own calls in the command module's script-message handler (fn_75_654C). Both are
// over 0x80, so each stays a named once-logging shell.

// The head's own move (fn_75_2154, 0xA8) is ported as
// CSplitterCommandModule::PortMoveTo.

// The DOL's CSfxManager::UpdateEmitter (the sound the look-at plays).


// Port: the beam's depth read-back. Retail's PreRender copies the EFB depth of its small viewport
// into the beam texture (GXCopyTex, GX_TF_Z16) and Render reads that texture's bytes back on the
// CPU; on the host the copy only lands in a GPU texture, so PreRender also asks Aurora for a scan
// peek of the same region (left 0, top 0, the texture's size) and Render fills the texture's
// bytes from the latest one. The peek writes each depth as the console's Z16 (the top 16 bits of
// the 24-bit z, high byte first), so the texture holds what the copy would have left there. The
// staging is a static pool rather than a member so a read-back that completes after the beam is
// gone never writes freed memory.
constexpr int kBeamPeekSlots = 4;
constexpr int kBeamPeekPixels = 256; // the head's beam is 4 x 0x40 texels

struct SBeamPeekStaging {
  // The seqlock scan_peek.hpp describes: odd while the render thread writes, then even again.
  // Only read or written through std::atomic_ref.
  uint32_t sequence;
  uint8_t id[kBeamPeekPixels];
  uint16_t depthHigh[kBeamPeekPixels];
  uint8_t depthLow[kBeamPeekPixels];
  uint32_t consumed;
  bool used;
};

SBeamPeekStaging sBeamPeeks[kBeamPeekSlots];

class CPortBeamDepthPeek {
public:
  CPortBeamDepthPeek() = default;
  CPortBeamDepthPeek(const CPortBeamDepthPeek&) = delete;
  CPortBeamDepthPeek& operator=(const CPortBeamDepthPeek&) = delete;
  ~CPortBeamDepthPeek() {
    if (mSlot >= 0) {
      sBeamPeeks[mSlot].used = false;
    }
  }

  // Called just before PreRender's GXCopyTex, which ends the pass the peek reads. The first call
  // takes a slot and fills the texture with 0xFF (the far plane, a full-length cone) until the
  // first read-back lands; with no slot free, or a texture too big for one, the cone stays at
  // full length (a port limit).
  void Request(CTexture& texture) {
    const int width = texture.GetWidth();
    const int height = texture.GetHeight();
    if (width <= 0 || height <= 0 || width * height > kBeamPeekPixels) {
      return;
    }
    if (mSlot < 0) {
      for (int i = 0; i < kBeamPeekSlots; ++i) {
        if (!sBeamPeeks[i].used) {
          mSlot = i;
          break;
        }
      }
      if (mSlot < 0) {
        // No staging slot (more than kBeamPeekSlots beams): Fill never runs, so give Render the
        // far plane rather than uninitialised host bytes.
        memset(texture.GetBitMapData(0), 0xFF, static_cast< size_t >(width * height * 2));
        return;
      }
      SBeamPeekStaging& staging = sBeamPeeks[mSlot];
      staging.used = true;
      // A snapshot already in the slot belongs to its previous owner.
      staging.consumed = std::atomic_ref< uint32_t >(staging.sequence).load(std::memory_order_acquire);
      memset(texture.GetBitMapData(0), 0xFF, static_cast< size_t >(width * height * 2));
    }
    SBeamPeekStaging& staging = sBeamPeeks[mSlot];
    // Depth only (no scan-id diagnostics), and only on frames that are rendered anyway: under
    // AURORA_RENDER_EVERY a skipped frame records no draw, so its EFB holds nothing to read, and
    // forcing it rendered would defeat the skipping. The beam keeps the last snapshot meanwhile.
    GXRequestScanPeekEx(0, 0, static_cast< u32 >(width), static_cast< u32 >(height), staging.id,
                        staging.depthHigh, staging.depthLow, &staging.sequence,
                        GX_SCAN_PEEK_DEPTH_ONLY | GX_SCAN_PEEK_IF_RENDERED);
  }

  // Called by Render before it reads the texture: copies the newest whole read-back into the
  // texture's bytes in the console's IA8 layout (4x4 texel tiles, which for the 4-texel-wide beam
  // texture is simply row-major).
  void Fill(CTexture& texture) const {
    if (mSlot < 0) {
      return;
    }
    const int width = texture.GetWidth();
    const int height = texture.GetHeight();
    if (width <= 0 || height <= 0 || width * height > kBeamPeekPixels) {
      return;
    }
    SBeamPeekStaging& staging = sBeamPeeks[mSlot];
    std::atomic_ref< uint32_t > sequence(staging.sequence);
    const uint32_t before = sequence.load(std::memory_order_acquire);
    if ((before & 1u) != 0 || before == staging.consumed) {
      return;
    }
    uint16_t depth[kBeamPeekPixels];
    memcpy(depth, staging.depthHigh, static_cast< size_t >(width * height) * sizeof(uint16_t));
    if (sequence.load(std::memory_order_acquire) != before) {
      return; // torn: keep the previous snapshot for this frame
    }
    staging.consumed = before;
    uchar* data = static_cast< uchar* >(texture.GetBitMapData(0));
    const int tilesWide = (width + 3) / 4;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const int texel = ((y / 4) * tilesWide + x / 4) * 16 + (y % 4) * 4 + x % 4;
        memcpy(data + texel * 2, &depth[y * width + x], 2);
      }
    }
  }

private:
  int mSlot = -1;
};

// Port: the dark world cloud texture. Retail Render loads the renderer's own token
// (*(CCubeRenderer::sRenderer + 0x510), mDarkWorldCloud).
CTexture& PortDarkWorldCloud() { return CCubeRenderer::That()->GetDarkWorldCloud(); }

} // namespace

// The head's beam effect (0x1A0 bytes; the REL has no type name for it, so the name is a guess):
// a CActor with no model whose vtable (lbl_75_data_E34) keeps the CActor slots except the
// dtor (fn_75_11804), Think (fn_75_10DC4), PreRender (fn_75_10F1C), AddToRenderer
// (fn_75_10EF0), Render (fn_75_10778) and PreRenderAllViewports (fn_75_1167C). The slot after
// HealthInfo is fn_75_D0, the REL's copy of the DOL's CActor::GetHealthInfo (not ported here).
// A cone opens from 1 degree to the data's angle in front of the head while the head scans.
class CSplitterBeamEffect : public CActor {
public:
  CSplitterBeamEffect(TUniqueId uid, const CEntityInfo& info, const rstl::string& name,
                      const CTransform4f& xf, short textureHeight, float angle, float length,
                      const CColor& cloudColor1, const CColor& cloudColor2,
                      const CColor& addColor1, const CColor& addColor2, float cloudScale,
                      float fadeOffSize, float openSpeed);
  ~CSplitterBeamEffect() override;

  void Think(float dt, CStateManager& mgr) override;
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  void Render(const CStateManager& mgr) const override;
  void PreRenderAllViewports(CStateManager& mgr) override;

  void SetOpening(bool opening) { mOpening = opening; }
  bool IsOpening() const { return mOpening; }
  float GetOpenFraction() const { return mOpenFraction; }

private:
  float mMaxAngle;        // 0x158
  float mLength;          // 0x15C
  CColor mCloudColor1;    // 0x160
  CColor mCloudColor2;    // 0x164
  CColor mAddColor1;      // 0x168
  CColor mAddColor2;      // 0x16C
  float mCloudScale;      // 0x170
  float mFadeOffSize;     // 0x174
  float mOpenSpeed;       // 0x178
  CTexture* mTexture;     // 0x17C
  float mAngle;           // 0x180
  int mUnknown184;        // 0x184
  float mOpenFraction;    // 0x188
  CRandom16 mRandom;      // 0x18C
  float mUnknown190;      // 0x190
  float mUnknown194;      // 0x194
  // Port: retail's PreRender runs once per 1/60 s tick; uncapped rendering runs it more often, so
  // the count and the two colour rolls advance only after a Think (the depth copy still runs).
  bool mPortTicked = true;
  bool mOpening;          // 0x198

  // Port: the depth read-back that stands in for reading the GXCopyTex result (see above).
  CPortBeamDepthPeek mPortDepthPeek;
};

namespace {
// lbl_75_bss_38, set by the REL's static initialiser fn_75_11AAC from lbl_75_rodata_884: the
// cone's closed angle, one degree in radians.
const float kBeamEffectClosedAngle = 0.017453292f;

// lbl_75_rodata_7C0: Render's fade ramp, an 8x4 GX_TF_I8 texture (one tile) falling from 0xFF to 0
// along s.
alignas(32) const uchar kBeamFadeRamp[32] = {
    0xFF, 0xDB, 0xB7, 0x93, 0x6F, 0x4B, 0x27, 0x00, 0xFF, 0xDB, 0xB7, 0x93, 0x6F, 0x4B, 0x27, 0x00,
    0xFF, 0xDB, 0xB7, 0x93, 0x6F, 0x4B, 0x27, 0x00, 0xFF, 0xDB, 0xB7, 0x93, 0x6F, 0x4B, 0x27, 0x00,
};

// The wrap both the head (0x12F4-0x1330) and the beam (0x10E9C-0x10ECC) do: x minus whole turns
// (fctiwz of x / 2pi, lbl_75_rodata_78 / 7C), plus a turn when that is below 0.0f.
float PortWrapTurn(float x) {
  float r = x - static_cast< float >(static_cast< int >(x * 0.15915494f)) * 6.2831855f;
  if (r < 0.f) {
    r += 6.2831855f;
  }
  return r;
}

void StandInBeamEffect(const char* what) {
  static const char* sLogged[4];
  static int sCount;
  for (int i = 0; i < sCount; ++i) {
    if (sLogged[i] == what) {
      return;
    }
  }
  if (sCount < 4) {
    sLogged[sCount++] = what;
  }
  fprintf(stderr, "[splitter] %s is a port stand-in (see PortCSplitterCommandModule.cpp)\n", what);
}
} // namespace

// Arms: all ran; the coverage build's merged profile says 20/20 lines and 6/6 branches, ALL-ARMS.
// @ fn_75_11884
// The ctor (0x11884-0x11A8C): a CActor (0x1197C) with an empty model (fn_75_11A8C, the
// CModelData default ctor), the material list holding the material lbl_75_data_E30 names (0,
// kMT_NoStepLogic; 0x11910-0x11954), default CActorParameters (0x11908) and no next draw node
// (0x118F8-0x11904). The fields take the arguments (0x11990-0x119DC); a kTF_IA8 texture of width 4
// and the given height with one mip is made (0x119A8-0x119FC); the angle starts closed
// (lbl_75_bss_38, 0x11A04-0x11A24), the open fraction and 0x184 at 0.0f/0 (0x11A2C-0x11A30), the
// random source is seeded with 99 (0x11A34), 0x190/0x194 are 0.0f and the cone opens (0x11A54).
CSplitterBeamEffect::CSplitterBeamEffect(TUniqueId uid, const CEntityInfo& info,
                                         const rstl::string& name, const CTransform4f& xf,
                                         short textureHeight, float angle, float length,
                                         const CColor& cloudColor1, const CColor& cloudColor2,
                                         const CColor& addColor1, const CColor& addColor2,
                                         float cloudScale, float fadeOffSize, float openSpeed)
: CActor(uid, name, info, 0, xf, CModelData(), CMaterialList(kMT_NoStepLogic), CActorParameters(),
         kInvalidUniqueId)
, mMaxAngle(angle)
, mLength(length)
, mCloudColor1(cloudColor1)
, mCloudColor2(cloudColor2)
, mAddColor1(addColor1)
, mAddColor2(addColor2)
, mCloudScale(cloudScale)
, mFadeOffSize(fadeOffSize)
, mOpenSpeed(openSpeed)
, mTexture(new CTexture(kTF_IA8, 4, textureHeight, 1))
, mAngle(kBeamEffectClosedAngle)
, mUnknown184(0)
, mOpenFraction(0.f)
, mRandom(99)
, mUnknown190(0.f)
, mUnknown194(0.f)
, mOpening(true) {}

// Arms: all ran but the deleting flag's arm (the only caller is the operator delete, so the
// direct-destructor path is dead); the coverage build's merged profile says 4/4 lines and 1/2
// branches.
// @ fn_75_11804
// The dtor (0x11804-0x11884): the texture is scheduled for deletion (0x11834) and freed
// (0x11848), then the CActor dtor runs (0x11854).
CSplitterBeamEffect::~CSplitterBeamEffect() {
  mTexture->ScheduleDeletion();
  delete mTexture;
}

// fn_75_10DC4 (Think, 0x10DC4-0x10EF0): after CActor::Think (0x10DE4), an active beam
// (0x10DE8-0x10DF0) moves its open fraction by dt times the open speed toward 1.0f while opening
// (lbl_75_rodata_848, 0x10E00-0x10E24) or toward 0.0f otherwise (lbl_75_rodata_85C,
// 0x10E2C-0x10E50), and its angle becomes the closed angle lerped to the full one by that
// fraction, wrapped to one turn (0x10E54-0x10ED0).
// Arms: 6/6, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov merged): the
// active ones in play; the inactive arm and the wrap's below-zero arm (the full angle -3) forced
// by gdb calls (splitter_split24_beam.py).
// @ fn_75_10DC4
void CSplitterBeamEffect::Think(float dt, CStateManager& mgr) {
  CActor::Think(dt, mgr);
  mPortTicked = true;
  if (!GetActive()) {
    return;
  }
  if (mOpening) {
    // 0x10E14 fcmpo 1.0f, open; 0x10E18 bge (taken when !(1.0f < open)) keeps the sum, so 1.0f
    // only when the sum exceeds it (a NaN sum is kept).
    const float open = dt * mOpenSpeed + mOpenFraction;
    mOpenFraction = 1.f < open ? 1.f : open;
  } else {
    // 0x10E40 fcmpo 0.0f, open; 0x10E44 bge (taken when !(0.0f < open)) stores 0.0f, so the
    // difference only when above 0.0f (a NaN one gives 0.0f).
    const float open = mOpenFraction - dt * mOpenSpeed;
    mOpenFraction = 0.f < open ? open : 0.f;
  }
  mAngle = PortWrapTurn((mMaxAngle - kBeamEffectClosedAngle) * mOpenFraction +
                        kBeamEffectClosedAngle);
}

// fn_75_10F1C (PreRender, 0x10F1C-0x1167C): renders the depth the cone sees. From the beam's own
// viewpoint, in a viewport the texture's size at the EFB's top left, the visible area geometry
// and the near actors that take projected shadows are drawn flat with depth writes, and the depth
// is copied into the texture (GX_TF_Z16) for Render to read back; then the view state is put
// back, the clip flag set from the actor's visibility, and the two colour blends re-rolled.
// Port: the copy only reaches a GPU texture, so a scan peek of the same region (CPortBeamDepthPeek)
// is requested just before it.
// Arms: 70/71, split26 cov4-cov6 (tools/cov/specs/splitter_split26.py, gcov merged): cov4 914f1381
// in play; cov6 03_cliff_chicane (5571e89e layer 3, splitter_split26_chicane.con, the one
// Splitter area with a VISI section) for the PVS arm; the null actor, no projected shadow and
// near-box miss forced in cov5 (splitter_split26_force.py). Untaken: 0x11148's bne, dead
// (gkPVSEnabled is const 1).
// @ fn_75_10F1C
void CSplitterBeamEffect::PreRender(CStateManager& mgr) {
  const bool portAdvance = mPortTicked;
  mPortTicked = false;
  if (portAdvance) {
    ++mUnknown184; // 0x10F4C-0x10F58
  }

  // 0x10F5C-0x10FF4: the projection state, view matrix, viewport and depth range are saved.
  const CGraphics::CProjectionState savedProjection = CGraphics::GetProjectionState();
  const CTransform4f savedView = CGraphics::GetViewMatrix();
  const CViewport savedViewport = CGraphics::GetViewport();
  const float savedNear = CGraphics::GetDepthNear();
  const float savedFar = CGraphics::GetDepthFar();
  const bool useVideoFilter = CGraphics::GetUseVideoFilter(); // 0x10FF8
  CGraphics::SetUseVideoFilter(false);                        // 0x11000-0x11004

  // 0x11008-0x11040: viewport (0, efbHeight - height, width, height) through vtable slot 0x68.
  gpRender->SetViewport(0, CGraphics::GetRenderMode().efbHeight - mTexture->GetHeight(),
                        mTexture->GetWidth(), mTexture->GetHeight());
  CGraphics::SetDepthRange(0.f, 1.f); // lbl_75_rodata_85C / 848, 0x11044-0x11054
  gpRender->SetWorldViewpoint(GetTransform()); // vtable slot 0x58, 0x11058-0x11074
  CGraphics::SetModelMatrix(CTransform4f::Identity()); // 0x11078-0x11080

  // 0x11084-0x110F0: the near box, the transform's translation (0x30/0x40/0x50, not the cached
  // 0x54) plus and minus the length on every axis.
  const CVector3f reach = CVector3f(mLength, mLength, mLength);
  const CVector3f origin = GetTransform().GetTranslation();
  const CAABox nearBox(origin - reach, origin + reach);

  CGX::SetNumChans(0);          // 0x110F4-0x110F8
  CGraphics::DisableAllLights(); // 0x110FC

  // 0x11100-0x11250: the visibility set is out of bounds unless the area has a PVS and PVS is on,
  // when it is the octree's set at the actor's position in area space.
  CPVSVisSet visSet(kVSS_OutOfBounds); // 0x11100-0x11108
  const CGameArea& area = mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()); // 0x1110C-0x11128
  const CPVSAreaSet* areaPVS = area.GetPostConstructed()->mPvs.get(); // 0x1112C-0x11130
  // 0x11134 cmplwi 0 / 0x11138 beq: no PVS; 0x11144 cmpwi gkPVSEnabled, 1 / 0x11148 bne.
  if (areaPVS != nullptr && gkPVSEnabled == 1) {
    visSet = areaPVS->GetVisOctree().GetVisSet(area.GetPostConstructed()->mInverseTransform *
                                               GetTranslation()); // 0x1114C-0x1124C
  }

  // 0x11250-0x112CC: aspect = pixel aspect * width / height; the frustum from the transform with
  // the cone's angle, near 0.2f (lbl_75_rodata_860), far the length.
  const float aspect = CGraphics::GetPixelAspectRatio() *
                       (static_cast< float >(mTexture->GetWidth()) /
                        static_cast< float >(mTexture->GetHeight()));
  const CFrustumPlanes frustum(GetTransform(), mAngle, aspect, 0.2f, true, mLength);
  // 0x112D0-0x11308: vtable slot 0x60, fovy in degrees (lbl_75_rodata_880, 57.295776f).
  gpRender->SetPerspective(57.295776f * mAngle, aspect, 0.2f, mLength);

  // 0x1130C-0x113D8: one stage that writes only depth (colour ONE, alpha KONST, no texture).
  CGX::SetNumTevStages(1);
  CGX::SetNumTexGens(1);
  CGX::SetNumIndStages(0);
  CGX::SetTevDirect(GX_TEVSTAGE0);
  CGX::SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ONE);
  CGX::SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
  CGX::SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_1);
  CGX::SetStandardTevColorAlphaOp(GX_TEVSTAGE0);
  CGX::SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
  CGX::SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_POS, GX_IDENTITY, false, GX_PTIDENTITY);
  CGX::SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
  CGX::SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
  CGX::SetZMode(true, GX_LEQUAL, true);

  // 0x113DC-0x113F8: the area geometry the vis set and frustum pass, culled by the other bounds.
  CCubeRenderer::That()->DrawVisibleAreaGeometry(GetCurrentAreaId().Value(), visSet, frustum,
                                                 GetOtherBounds());

  // 0x113FC-0x1151C: every actor near the box.
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, nearBox, CMaterialFilter::GetPassEverything(), nullptr);
  for (rstl::reserved_vector< TUniqueId, kMaxObjects >::const_iterator it = nearList.begin();
       it != nearList.end(); ++it) {
    // 0x1142C-0x1143C: the near list holds actors; no type check.
    const CActor* actor = static_cast< const CActor* >(mgr.GetObjectById(*it));
    if (actor == nullptr) {
      continue; // 0x11440 beq
    }
    if (!actor->CanDrawStatic()) {
      continue; // 0x1144C beq
    }
    if (!actor->GetTakesProjectedShadow()) {
      continue; // 0x11458 extrwi. 0x154 bit 0x10 / 0x1145C beq
    }
    if (!frustum.BoxInFrustumPlanes(actor->GetOtherBounds())) {
      continue; // 0x11474 beq: the other bounds are outside the frustum
    }
    if (!nearBox.DoBoundsOverlap(actor->GetOtherBounds())) {
      continue; // 0x11488 beq: they miss the near box
    }
    // 0x1148C-0x11504: transform times the model's scale (vtable slot 0x40), then the normal
    // static model drawn flat (flags 2).
    const CModelData& modelData = *actor->GetModelData();
    gpRender->SetModelMatrix(actor->GetTransform() * CTransform4f::Scale(modelData.GetScale()));
    modelData.PickStaticModel(CModelData::kWM_Normal)->DolphinDrawFlat(
        static_cast< CModel::EDrawFlatFlags >(2));
  }

  // 0x11520-0x11590: the depth copy (GX_TF_Z16 0x13, no mips) into the locked texture, clearing.
  GXSetTexCopySrc(0, 0, static_cast< u16 >(mTexture->GetWidth()),
                  static_cast< u16 >(mTexture->GetHeight()));
  GXSetTexCopyDst(static_cast< u16 >(mTexture->GetWidth()),
                  static_cast< u16 >(mTexture->GetHeight()), GX_TF_Z16, false);
  mPortDepthPeek.Request(*mTexture); // port: before the copy ends the pass
  GXCopyTex(mTexture->Lock(), true);
  mTexture->UnLock();
  GXPixModeSync();

  // 0x11594-0x115F8: the saved state goes back.
  CGraphics::SetDepthRange(savedNear, savedFar);
  CGraphics::SetUseVideoFilter(useVideoFilter);
  CGraphics::SetProjectionState(savedProjection);
  gpRender->SetWorldViewpoint(savedView);
  gpRender->SetViewport(savedViewport.mLeft, savedViewport.mTop, savedViewport.mWidth,
                        savedViewport.mHeight);

  SetPreRenderClipped(!mgr.IsActorVisible(*this)); // 0x115FC-0x1161C
  if (portAdvance) {
    mUnknown190 = mRandom.Float(); // 0x11618-0x11624
    mUnknown194 = mRandom.Float(); // 0x11628-0x11634
  }
}

// fn_75_10EF0 (AddToRenderer, 0x10EF0-0x10F1C): unless the actor is out of the frustum (0x150's
// low bit, 0x10EFC-0x10F04) it is rendered (0x10F08 EnsureRendered).
// Arms: 2/2, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov merged): the
// clipped arm forced (0x150's low bit set for one call).
// @ fn_75_10EF0
void CSplitterBeamEffect::AddToRenderer(const CStateManager& mgr) const {
  if (!GetPreRenderClipped()) {
    EnsureRendered(mgr);
  }
}

// fn_75_10778 (Render, 0x10778-0x10DC4): the cone as an additive triangle fan in the beam's
// plane. Once two PreRenders have copied depth, each of the texture's rows gives the distance
// the cone reaches along that ray (the depth of the texture's middle column, unprojected with
// near 0.2f and far the length), so the fan stops at walls. Stage 0 is the dark world cloud
// times the cloud colour, stage 1 a second cloud sample times that plus the add colour, stage 2
// the result faded along the length by an 8-texel ramp; the colours are lerped by the two
// randoms PreRender rolls.
// Port: the texture's bytes come from CPortBeamDepthPeek, filled just before they are read.
// Arms: ALL-ARMS, split26 cov4 (tools/cov/specs/splitter_split26.py, 914f1381, gcov): the early
// return ran in play (the first frames after the beam spawns).
// @ fn_75_10778
void CSplitterBeamEffect::Render(const CStateManager&) const {
  // 0x107A4 lwz 0x184 / 0x107A8 cmpwi 2 / 0x107AC blt (signed): nothing before two copies.
  if (mUnknown184 < 2) {
    return;
  }
  CGraphics::SetModelMatrix(GetTransform()); // 0x107B0-0x107B4

  // 0x107B8-0x10824: half the cone's height at distance 1 (tan of half the angle,
  // lbl_75_rodata_840 0.5f) and the step between rows, 2.0f (844) times that over height - 1.0f
  // (848); the height is converted signed (lbl_75_rodata_868).
  const float height = static_cast< float >(mTexture->GetHeight());
  float spread = static_cast< float >(tan(0.5f * mAngle));
  const float spreadStep = 2.f * spread / (height - 1.f);

  // 0x10808-0x10854: KCOLOR0 the cloud colours by 0x194, KCOLOR1 the add colours by 0x190.
  CGX::SetTevKColor(GX_KCOLOR0, CColor::Lerp(mCloudColor1, mCloudColor2, mUnknown194).GetGXColor());
  CGX::SetTevKColor(GX_KCOLOR1, CColor::Lerp(mAddColor1, mAddColor2, mUnknown190).GetGXColor());

  // 0x10858-0x1096C: the three stages.
  CGX::SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO);
  CGX::SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV, GX_CC_KONST);
  CGX::SetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO);
  CGX::SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
  CGX::SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
  CGX::SetTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
  CGX::SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
  CGX::SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_1);
  CGX::SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K1);
  CGX::SetStandardTevColorAlphaOp(GX_TEVSTAGE0);
  CGX::SetStandardTevColorAlphaOp(GX_TEVSTAGE1);
  CGX::SetStandardTevColorAlphaOp(GX_TEVSTAGE2);
  CGX::SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR_NULL);
  CGX::SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD2, GX_TEXMAP1, GX_COLOR_NULL);
  CGX::SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
  CGX::SetNumTevStages(3);
  CGX::SetNumChans(0);

  // 0x10970-0x109DC: TEXMTX0 from the zero template lbl_75_rodata_7E0: s = y / fadeOffSize +
  // 1.0f - length / fadeOffSize, so the ramp ends at the cone's far end.
  float fadeMtx[2][4] = {{0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}};
  fadeMtx[0][1] = 1.f / mFadeOffSize;
  fadeMtx[0][3] = 1.f + -mLength / mFadeOffSize;
  // 0x109E0-0x10AFC: the two cloud matrices (zero templates 800 and 820) scroll with the time:
  // TEXMTX1 (y, z) / cloudScale + (t, 0.5f t); TEXMTX2 (1.4f z, 1.5f y) / cloudScale +
  // (0.3f t, 1.15f t) (lbl_75_rodata_84C/850/854/858).
  const float time = CGraphics::GetSecondsMod900();
  const float invScale = 1.f / mCloudScale;
  float cloudMtx1[2][4] = {{0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}};
  cloudMtx1[0][1] = invScale;
  cloudMtx1[0][3] = time;
  cloudMtx1[1][2] = invScale;
  cloudMtx1[1][3] = 0.5f * time;
  float cloudMtx2[2][4] = {{0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}};
  cloudMtx2[0][2] = 1.4f * invScale;
  cloudMtx2[0][3] = 0.3f * time;
  cloudMtx2[1][1] = 1.5f * invScale;
  cloudMtx2[1][3] = 1.15f * time;
  GXLoadTexMtxImm(fadeMtx, GX_TEXMTX0, GX_MTX2x4);   // 0x10B00
  GXLoadTexMtxImm(cloudMtx1, GX_TEXMTX1, GX_MTX2x4); // 0x10B10
  GXLoadTexMtxImm(cloudMtx2, GX_TEXMTX2, GX_MTX2x4); // 0x10B20
  CGX::SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_POS, GX_TEXMTX0, false, GX_PTIDENTITY);
  CGX::SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_POS, GX_TEXMTX1, false, GX_PTIDENTITY);
  CGX::SetTexCoordGen(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_POS, GX_TEXMTX2, false, GX_PTIDENTITY);
  CGX::SetNumTexGens(3); // 0x10B78

  // 0x10B80-0x10BE8: TEXMAP0 is the ramp (lbl_75_rodata_7C0, 8x4 GX_TF_I8, clamped, linear).
  CTexture::InvalidateTexmap(GX_TEXMAP0);
  GXTexObj rampObj;
  GXInitTexObj(&rampObj, const_cast< uchar* >(kBeamFadeRamp), 8, 4, GX_TF_I8, GX_CLAMP, GX_CLAMP,
               false);
  GXInitTexObjLOD(&rampObj, GX_LINEAR, GX_LINEAR, 0.f, 0.f, 0.f, true, false, GX_ANISO_1);
  GXLoadTexObj(&rampObj, GX_TEXMAP0);
  // 0x10BEC-0x10C04: TEXMAP1 the renderer's dark world cloud, mirrored.
  PortDarkWorldCloud().Load(GX_TEXMAP1, CTexture::kCM_Mirror);

  // 0x10C08-0x10C5C: additive, no alpha test, depth test without writes, both faces.
  CGraphics::SetBlendMode(kBM_Blend, kBF_One, kBF_One, kLO_Clear);
  CGraphics::SetAlphaCompare(kAF_Always, 0, kAO_Or, kAF_Always, 0);
  gpRender->SetDepthReadWrite(true, false); // vtable slot 0x6C
  CGraphics::SetCullMode(kCM_None);

  // 0x10C60-0x10CA8: a fan of height + 1 positions from the origin.
  CGX::ResetVtxDescv();
  CGX::SetVtxDesc(GX_VA_POS, GX_DIRECT);
  CGX::Begin(GX_TRIANGLEFAN, GX_VTXFMT0, static_cast< ushort >(mTexture->GetHeight() + 1));
  GXPosition3f32(0.f, 0.f, 0.f);

  mPortDepthPeek.Fill(*mTexture); // port: the copy's bytes
  const ushort* depth = static_cast< const ushort* >(mTexture->GetConstBitMapData(0)); // 0x10CB0
  // 0x10CB4-0x10D64: one vertex per row i (re-reading the height, signed compare at 0x10D60/
  // 0x10D64 blt): the Z16 at the middle column (width / 2, rounded toward zero), byte-swapped
  // (0x10D20 lhbrx), over 65535.0f (lbl_75_rodata_864) is d; the distance is
  // (0.2f * -length) / (d * (length - 0.2f) - length), the inverse of the perspective depth.
  const float nearTimesFar = 0.2f * -mLength;
  const float farMinusNear = mLength - 0.2f;
  const int half = mTexture->GetWidth() / 2;
  for (int i = 0, row = 0; i < mTexture->GetHeight(); ++i, row += mTexture->GetWidth()) {
    const ushort raw = depth[half + row];
    const ushort swapped = static_cast< ushort >((raw << 8) | (raw >> 8));
    const float d = static_cast< float >(swapped) / 65535.f;
    const float distance = nearTimesFar / (d * farMinusNear - mLength);
    GXPosition3f32(0.f, distance, spread * distance);
    spread -= spreadStep;
  }
  CGX::End(); // 0x10D68

  // 0x10D6C-0x10D8C: the texture's bytes are invalidated for the next copy.
  DCInvalidateRange(const_cast< ushort* >(depth),
                    (mTexture->GetWidth() * mTexture->GetHeight() * 2 + 0x1F) & ~0x1F);
  CGraphics::SetCullMode(kCM_Front); // 0x10D90-0x10D94
}

// fn_75_1167C (PreRenderAllViewports, 0x1167C-0x11804): the bounds start at the translation
// (0x116A4-0x116CC) and take the two cone edges, (0, length, +-length * tan(angle / 2))
// (lbl_75_rodata_840 0.5f, 0x116D0-0x11754) through the transform; they are both the render
// bounds (0xCC) and the other bounds (0xE4) (0x11758-0x117B4). With render bounds dirty
// (0x117B8-0x117C0) the portal state is updated and the flag cleared (0x117CC-0x117DC).
// Arms: 6/6, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov merged):
// render bounds dirty and clean, both forced.
// @ fn_75_1167C
void CSplitterBeamEffect::PreRenderAllViewports(CStateManager& mgr) {
  CAABox bounds(GetTranslation(), GetTranslation());
  const float half = mLength * static_cast< float >(tan(0.5f * mAngle));
  bounds.AccumulateBounds(GetTransform() * CVector3f(0.f, mLength, half));
  bounds.AccumulateBounds(GetTransform() * CVector3f(0.f, mLength, -half));
  SetRenderBounds(bounds);
  SetOtherBounds(bounds);
  if (GetRenderBoundsDirty()) {
    UpdatePortalSystemState(mgr);
    SetRenderBoundsDirty(false);
  }
}

// Every state machine name the REL's initialiser installs is ported.

// The state machine entries the area-4 run reaches, whose retail body is ported here.

// fn_75_5864 (the head's HasDockingPath trigger, 0x5864-0x58BC): the "CONN"/"FOLW" connected
// object (0x588C GetConnectedObject) is valid.
// The true arm is dead in shipped data: no Splitter head has a CONN/FOLW link in any SCLY or SCGN
// layer (disc scan).
// Arms: both, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov merged):
// false by a gdb call, true with GetConnectedObject answering the head's own id.
// @ fn_75_5864
bool CSplitterCommandModule::HasDockingPath(CStateManager& mgr, const CTriggerData&) const {
  return GetConnectedObject(mgr, kSS_Connect, kSM_Follow) != kInvalidUniqueId;
}

// fn_75_57D4 (the head's DockingPathOver trigger, 0x57D4-0x57F0): the waypoint navigation's
// destination at 0x6C0 is kInvalidUniqueId (cntlzw/srwi by 5).
// Not reachable in shipped data: entered after HasDockingPath (fn_75_5864), whose true arm is
// dead (no CONN/FOLW link). No marker.
bool CSplitterCommandModule::DockingPathOver(CStateManager&, const CTriggerData&) const {
  return mWaypointNavigation.GetDestination() == kInvalidUniqueId;
}

// fn_75_55B0 (the head's PathOver trigger, 0x55B0-0x55F8): with the flag byte 0xF6A's 0x80 bit
// (SetDockingDest found the chassis) set the base's PathOver decides (0x55C4 extrwi with the
// word's bit 24, 0x55D0); without it the path is over.
// Arms: both false ran (04_cliff_contraption 914f1381, the head docking to another chassis after
// `kill` of a chassis and a head); the 0x80-clear and base true arms forced under gdb.
// @ fn_75_55B0
bool CSplitterCommandModule::PathOver(CStateManager& mgr, const CTriggerData& data) const {
  return (mAutoDestructFlags & 0x80) == 0 || CPatterned::PathOver(mgr, data);
}

// fn_75_5568 (the head's PathShagged trigger, 0x5568-0x55B0): true when 0xF6A's 0x40 bit is set
// (0x557C extrwi with the word's bit 25), else the base's PathShagged (0x5588).
// Arms: both false ran (the same 914f1381 docking run); the 0x40 and base true arms forced under
// gdb.
// @ fn_75_5568
bool CSplitterCommandModule::PathShagged(CStateManager& mgr, const CTriggerData& data) const {
  return (mAutoDestructFlags & 0x40) != 0 || CPatterned::PathShagged(mgr, data);
}

// fn_75_5498 (the head's SpawnIdle state, 0x5498-0x5538). Activate (0x54C8): the vector at 0xF24
// is zeroed (sZeroVector). Update (0x54E8): while the checked chassis id at 0xEFC is invalid it
// becomes the "CONN"/"ATCH" connected object (0x5518 FindConnectedObject).
// Arms: every arm ran in 09_cliff_temple_dark (a92f00b3 layer 10, the generated SpawnIdle heads).
// @ fn_75_5498
void CSplitterCommandModule::SpawnIdle(CStateManager& mgr, EStateMsg msg, float) {
  if (msg == kStateMsg_Activate) {
    mEditorProperties = CVector3f::Zero();
  } else if (msg == kStateMsg_Update) {
    if (mCheckedChassisId == kInvalidUniqueId) {
      mCheckedChassisId = FindConnectedObject(mgr, kSS_Connect, kSM_Attach);
    }
  }
}

// fn_75_4138 (the head's SeekMainChassis state, 0x4138-0x42F8). Activate (0x4184): the animation
// state is kAS_Ready (1), the locomotion type kLT_Combat (3) and 0xF6A's 0x08 bit is cleared.
// Update (0x41AC): without a CSplitterMainChassis at 0xEFC (0x41C0) the animation state is kAS_Over
// (3). With one, its head attach point (fn_75_E68C) is the goal: while the goal is farther than dt
// * the data's max linear velocity (0x7C4, squared, 0x4234) and the offset is safe the head moves
// that far toward it (0x42A0 fn_75_2154); otherwise it is put on the goal (0x42B0 SetTranslation)
// and the animation state is kAS_Over. Deactivate (0x42CC): kAS_NotReady (0).
// Arms (probed; head u0531 docking to u0521 after `kill u0532`): chassis, far and safe (moving) and
// near (put on the goal) ran. No chassis forced under gdb (0xEFC kInvalidUniqueId for the first
// Update, e4). Far but unsafe forced under gdb (the head's x set to -inf for one Update, e11: the
// offset is not finite, so IsMagnitudeSafe fails; the head is then put on the goal). A finite
// unsafe offset needs a squared length below 1e-28, not reachable at these coordinates.
// @ fn_75_4138
void CSplitterCommandModule::SeekMainChassis(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    BodyController()->SetLocomotionType(pas::kLT_Combat);
    mAutoDestructFlags &= ~0x8;
    break;
  case kStateMsg_Update:
    if (const CSplitterMainChassis* chassis =
            TCastToConstPtr< CSplitterMainChassis >(mgr.GetObjectById(mCheckedChassisId))) {
      const CVector3f goal = chassis->PortHeadAttachPoint();
      const CVector3f offset = goal - GetTranslation();
      const float step = dt * mData.mMaxLinearVelocity;
      if (offset.MagSquared() > step * step && offset.IsMagnitudeSafe()) {
        PortMoveTo(GetTranslation() + step * offset.AsNormalized(), dt);
      } else {
        SetTranslation(goal);
        mAnimationState.SetState(CAnimationState::kAS_Over);
      }
    } else {
      mAnimationState.SetState(CAnimationState::kAS_Over);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  default:
    break;
  }
}

// fn_75_3FE0 (the head's FollowDockingPath state, 0x3FE0-0x4138). Every message first runs the
// waypoint navigation's Patrol (0x6B8, 0x4020). Activate (0x403C): the destination at 0x6C0 is the
// "CONN"/"FOLW" connected object (0x4058) and the locomotion type kLT_Combat (3). Update (0x407C):
// with a safe body command move vector the head moves by it times dt * the data's max linear
// velocity (0x7C4, 0x40F8 fn_75_2154) and faces it (0x4108 FaceDirection).
// Not reachable in shipped data: entered only through HasDockingPath (fn_75_5864); no Splitter head
// has a CONN/FOLW link.
// Arms: 6/6, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov merged):
// every message by gdb calls; IsMagnitudeSafe answering false and true (Patrol rewrites the move
// vector).
// @ fn_75_3FE0
void CSplitterCommandModule::FollowDockingPath(CStateManager& mgr, EStateMsg msg, float dt) {
  mWaypointNavigation.Patrol(mgr, msg, dt, *this);
  if (msg == kStateMsg_Activate) {
    mWaypointNavigation.SetDestination(GetConnectedObject(mgr, kSS_Connect, kSM_Follow));
    BodyController()->SetLocomotionType(pas::kLT_Combat);
  } else if (msg == kStateMsg_Update) {
    const CVector3f move = BodyController()->CommandMgr().GetMoveVector();
    const float step = dt * mData.mMaxLinearVelocity;
    if (move.IsMagnitudeSafe()) {
      PortMoveTo(GetTranslation() + step * move, dt);
      BodyController()->FaceDirection(move, dt);
    }
  }
}

// fn_75_3BEC (the head's SetDockingDest code, 0x3BEC-0x3D20): the destination starts as the
// head's own position and 0xF6A's 0x80 bit is cleared (0x3C40-0x3C48). A CSplitterMainChassis at
// 0xEFC (0x3C58) makes it 4.0f (lbl_75_rodata_5C) above the chassis's head attach point
// (fn_75_E68C), sets the bit and sets the chassis's base 0x420 0x80 bit (the host's
// mHitByPlayerProjectile, 0x3CCC-0x3CD4). The base's path navigation (0x6F4) gets the destination
// and its face target (0x718) kInvalidUniqueId.
// Arms: the chassis ran (docking after `kill u0532`); no chassis forced under gdb (0xEFC invalid).
// @ fn_75_3BEC
void CSplitterCommandModule::SetDockingDest(CStateManager& mgr, float) {
  CVector3f dest = GetTranslation();
  mAutoDestructFlags &= ~0x80;
  if (CSplitterMainChassis* chassis =
          TCastToPtr< CSplitterMainChassis >(mgr.ObjectById(mCheckedChassisId))) {
    dest = chassis->PortHeadAttachPoint() + 4.f * CVector3f::Up();
    mAutoDestructFlags |= 0x80;
    chassis->SetHitByPlayerProjectile(true);
  }
  mPathFindNavigation.SetDestination(dest);
  mPathFindNavigation.SetFaceTarget(kInvalidUniqueId);
}


// fn_75_5564 (the head's Start state, 0x4 bytes): the body is a single return, so the state
// does nothing.
void CSplitterCommandModule::Start(CStateManager&, EStateMsg, float) {}

// fn_75_58DC (the head's IsInitiallyDocked trigger, 0x58DC-0x58E8): the SPLU parameter block's
// first word (0x7C0, the loader's unknown_0xbd80fd94) is read (0x58DC lwz) and its low bit is
// kept (0x58E0 clrlwi with the word's bit 31 - the byte's 0x01); that bit is the answer.
// Arms: no branches; the coverage build's merged profile hits every line.
// @ fn_75_58DC
bool CSplitterCommandModule::IsInitiallyDocked(CStateManager&, const CTriggerData&) const {
  return (mData.mUnknownBD80FD94 & 0x1) != 0;
}

// fn_75_58BC (the head's IsDocked trigger, 0x58BC-0x58DC): the id at 0xEFA (the chassis the
// head is docked to) is compared with kInvalidUniqueId (0x58C0 lhz, 0x58C4 lhz, 0x58C8 subf,
// 0x58CC subf, 0x58D0 or, 0x58D4 srwi by 31): the trigger is true when the id is valid.
// Arms: no branches; the coverage build's merged profile hits every line.
// @ fn_75_58BC
bool CSplitterCommandModule::IsDocked(CStateManager&, const CTriggerData&) const {
  return mChassisId != kInvalidUniqueId;
}

// fn_75_3B20 (the head's RaiseShields code, 0x3B20-0x3B34): the flag byte at 0xF6A gets its
// 0x08 bit set (0x3B28 rlwimi with the word's bit 28 - the byte's 0x08) and the body ends.
// Arms: all ran; the coverage build's merged profile says 3/3 lines and no branches.
// @ fn_75_3B20
void CSplitterCommandModule::RaiseShields(CStateManager&, float) {
  mAutoDestructFlags |= 0x8;
}

// fn_75_57B4 (the head's HasTarget trigger, 0x57B4-0x57D4): the id at 0xEFE (the copy of the
// chassis's 0xCC0) is compared with kInvalidUniqueId (0x57B8 lhz, 0x57BC lhz, 0x57C0 subf,
// 0x57C4 subf, 0x57C8 or, 0x57CC srwi): the trigger is true when the id is valid.
// Arms: no branches; the coverage build's merged profile hits every line.
// @ fn_75_57B4
bool CSplitterCommandModule::HasTarget(CStateManager&, const CTriggerData&) const {
  return mSyncedId != kInvalidUniqueId;
}

// fn_75_57F0 (the head's HasDockingTarget trigger, 0x57F0-0x5864): the id at 0xEFC (the second
// chassis the head checks) is looked up (0x580C GetObjectById) and cast to the main chassis
// (0x5810); a null cast (0x5814, 0x581C) gives false, and so do a chassis that is not alive
// (0x5820 lbz, 0x5824 extrwi with the word's bit 25 - the byte's 0x40, the host's GetAlive) and
// one that is not active (0x582C lbz, 0x5830 extrwi with the word's bit 24 - the byte's 0x80,
// the host's GetActive). What is left is the chassis's own command module id (0xC6C, 0x583C lhz):
// the trigger is true when the chassis has no command module yet (0x5848 bne).
// Arms: ALL-ARMS; the docking chassis ran (after `kill u0532`); the invalid id, a non-chassis id, a
// headless, an inactive and a dead chassis forced (split23 force_ret, splitter_split23.py).
// @ fn_75_57F0
bool CSplitterCommandModule::HasDockingTarget(CStateManager& mgr, const CTriggerData&) const {
  const CSplitterMainChassis* chassis =
      TCastToConstPtr< CSplitterMainChassis >(mgr.GetObjectById(mCheckedChassisId));
  if (chassis == nullptr) {
    return false;
  }
  if (!chassis->GetAlive()) {
    return false;
  }
  if (!chassis->GetActive()) {
    return false;
  }
  return chassis->GetCommandModuleId() == kInvalidUniqueId;
}


// fn_75_4D04 (the head's Hover state, 0x4D04-0x4F90): with the activate message (0x4D2C cmpwi
// r5, 1 - 0x4D40 cmpwi r5, 0) the body controller's locomotion type becomes 3 (0x4D50 li r4, 3
// - the host's kLT_Combat) (0x4D54 SetLocomotionType). With the update message and a frame time
// above zero (0x4D5C-0x4D68: lbl_75_rodata_40 is 0.0f) the head's synced id at 0xEFE (0x4D6C
// lhz) is looked up (0x4D7C GetObjectById) and that actor's aim position is asked for
// (0x4D90-0x4DAC: the vtable's word 0x54 - the host's GetAimPosition - the state manager and
// 0.0f); with an actor there (0x4D80 cmplwi) the head faces it (0x4DB0-0x4DF4: the direction is
// the aim position's x and y less the head's own 0x54 and 0x58, its z is 0.0f - 0x4DD8
// lbl_75_rodata_40 - and FaceDirection takes it with the frame time). The height difference
// (0x4DB0-0x4E04: the aim position's z less the head's own 0x5C) decides the step: when it is
// bigger than the frame time times the data's max linear velocity at 0x7C4 (0x4DFC-0x4E0C) the
// head moves up or down by that step (0x4E10-0x4E54) and the module's own move runs
// (0x4E64 fn_75_2154).
// Arms: 14/14, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov merged):
// the update in play; a zero frame time, no target and the head 200 above and below its target
// forced.
// @ fn_75_4D04
void CSplitterCommandModule::Hover(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    BodyController()->SetLocomotionType(pas::kLT_Combat);
    return;
  }
  if (msg != kStateMsg_Update) {
    return;
  }
  // 0x4D64 fcmpo dt, 0.0f; ble: only a frame time above zero goes on (a NaN one returns).
  if (!(dt > 0.f)) {
    return;
  }
  // The retail calls the vtable's word 0x54 on the entity it looked up (0x4D90-0x4DAC); the
  // host's GetAimPosition is CActor's, so the port casts - the asm's test is the null check
  // below (0x4D80 cmplwi r3, 0).
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mSyncedId));
  if (target == nullptr) {
    return;
  }
  const CVector3f aim = target->GetAimPosition(mgr, 0.f);
  const CVector3f pos = GetTranslation();
  BodyController()->FaceDirection(
      CVector3f(aim.GetX() - pos.GetX(), aim.GetY() - pos.GetY(), 0.f), dt);
  const float dz = aim.GetZ() - pos.GetZ();
  const float step = dt * mData.mMaxLinearVelocity;
  // 0x4E08 fcmpo |dz|, step; ble: only a height difference above the step moves (a NaN one
  // returns).
  if (!(fabsf(dz) > step)) {
    return;
  }
  // 0x4E18 fcmpo dz, 0.0f; ble: up by the step when dz > 0.0f, else down (0x4E28 fneg); the
  // position plus (0.0f, 0.0f, step) (0x4E3C-0x4E54 fadds, x and y plus 0.0f).
  PortMoveTo(pos + CVector3f(0.f, 0.f, dz > 0.f ? step : -step), dt);
}

// fn_75_2154 (the head's own move, 0x2154-0x21FC): the auto-destruct timer at 0xEF4 (0x2170
// lfs) decides - above zero (0x2180 fcmpo against 0.0f, 0x2184 ble) the head is teleported to
// the destination (0x2188 SetTranslation); otherwise the difference between the destination and
// the head's own position (0x54, 0x58 and 0x5C - 0x2190-0x21BC) is moved through the head's
// transform (0x21B0 the transform at 0x24, 0x21CC TransposeRotate) and the result moves the
// head in one frame (0x21DC MoveInOneFrameOR).
// Arms: the move ran in every head run; the teleport (the timer above 0) forced under gdb (the
// 0xEF4 timer set to 10.0f in a 914f1381 run).
// @ fn_75_2154
void CSplitterCommandModule::PortMoveTo(const CVector3f& dest, float dt) {
  if (mAutoDestructTimer > 0.f) {
    SetTranslation(dest);
    return;
  }
  const CVector3f delta = dest - GetTranslation();
  MoveInOneFrameOR(GetTransform().TransposeRotate(delta), dt);
}

// fn_75_5538 (the head's Idle state, 0x5538-0x5564): the state message is compared with 2
// (0x5538 cmpwi r5, 2 - 0x553C bgelr) and with 0 (0x5540 cmpwi r5, 0 - 0x5544 bltlr), so the
// body only runs for the update and the activate message - the retail's deactivate (2) and
// the messages above it return. The body then copies the head's transform's Y column (the
// floats at 0x28, 0x38 and 0x48 - 0x5548-0x5550) into the vector at 0xF24-0xF2C
// (0x5554-0x555C), which is the vector the retail's constructor fills from the editor
// properties (fn_75_6B08) and the head's other states overwrite.
// Arms: all ran - the update and the activate message take the body, the deactivate and the
// messages above it return; the coverage build's merged profile says 3/3 lines and 2/2 branches,
// ALL-ARMS.
// @ fn_75_5538
void CSplitterCommandModule::Idle(CStateManager&, EStateMsg msg, float) {
  if (msg != kStateMsg_Update && msg != kStateMsg_Activate) {
    return;
  }
  mEditorProperties = GetTransform().GetColumn(kDY);
}

// The two state machine entries whose retail body is ported: the IsScanning trigger
// (fn_75_58E8, the chassis's scan timer) and the NotifyDocking code (fn_75_3EEC, the head's
// dock to the chassis). The retail runs them as state machine nodes, by name.
bool CSplitterCommandModule::IsScanning(CStateManager& mgr, const CTriggerData&) const {
  return PortChassisTimerRunning(mgr);
}

void CSplitterCommandModule::NotifyDocking(CStateManager& mgr, float) {
  PortDockToChassis(mgr);
}

// fn_75_5944 (the head's InDetectionRange trigger, 0x5944-0x5A3C): false unless the IsScanning
// trigger (0x5968 fn_75_58E8) and the base's InDetectionRange (0x5980) both answer true. Then
// the player (the manager's 0x14FC) less the head's position, with z set to 0.0f (0x598C-0x59D0),
// is dotted with the head's transform's Y column (0x28, 0x38 and 0x48 - 0x59D8-0x5A04) and the
// answer is that dot above the distance times the base's detection cosine at 0x3D8
// (0x59EC-0x5A18: fcmpo and the cr0 gt bit).
// The shipped head's detection angle is 180 degrees (the cosine at 0x3D8 reads -1.0, area 5 run
// 2026-10-21). The dot is never below -dist, so the cone can fail only for a player exactly
// behind the head; that arm is unreachable in shipped data and never ran. The other two false
// arms ran: the timer off, and the timer on with the base range false (the player 41 units away).
// @ fn_75_5944
bool CSplitterCommandModule::InDetectionRange(CStateManager& mgr, const CTriggerData& data) const {
  if (!PortChassisTimerRunning(mgr) || !CPatterned::InDetectionRange(mgr, data)) {
    return false;
  }
  CVector3f delta = mgr.GetPlayer(0)->GetTranslation() - GetTranslation();
  delta.SetZ(0.f);
  const float dist = delta.Magnitude();
  return CVector3f::Dot(delta, GetTransform().GetColumn(kDY)) > dist * mDetectionAngle;
}

// fn_75_523C (the head's Scanning state, 0x523C-0x5444). The message picks the arm
// (0x5260-0x5288): 0 (activate, 0x528C) clears the base's flag byte 0x420's 0x80 bit (the
// host's hit-by-player-projectile flag, 0x528C-0x5298), zeroes the vector at 0xF24-0xF2C
// (0x529C-0x52B4, sZeroVector) and runs the effect-state update (0x52B8 fn_75_1578); 2
// (deactivate, 0x5420) runs only the update; 1 (update, 0x52C0) does the scan; others return.
// The scan: the chassis at 0xEFA is looked up and cast (0x52C0-0x52D8); with one, the head's own
// 0x80 bit set (0x52E0-0x52E8) or the InDetectionRange trigger with the value 0.0f
// (0x52EC-0x5310, fn_75_5944) sets the chassis's 0x80 bit (0x5314-0x5320), which its ShotAt
// trigger (fn_75_E5B8) reads. Then, chassis or not, the turn is the frame time times the data's
// scanning turn speed at 0x7CC (0x5324-0x5334), and the head's Y and X columns flattened to z
// 0.0f (0x28/0x38 and 0x24/0x34 - 0x5324-0x53A4) must both be IsMagnitudeSafe (0x53A8-0x53C0);
// the vector at 0xF24 becomes Slerp(Y normalized, X normalized, turn * 0.017453292f)
// (0x53C4-0x5418, the degrees to radians constant lbl_75_rodata_74).
// The own-0x80 shortcut is set only by the head's ALRT case (0x6710) or the base's Damage/Touch
// with a player projectile; the null chassis is reachable (the chassis can die while the head is
// docked).
// Arms: 19/19, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov merged): no
// chassis, the own hit bit, InDetectionRange false and true (forced), both columns flattened to
// zero, an out-of-range message.
// @ fn_75_523C
void CSplitterCommandModule::Scanning(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:
    SetHitByPlayerProjectile(false);
    mEditorProperties = CVector3f::Zero();
    PortUpdateEffectStates(mgr);
    break;
  case kStateMsg_Update: {
    CSplitterMainChassis* chassis = TCastToPtr< CSplitterMainChassis >(mgr.ObjectById(mChassisId));
    if (chassis != nullptr &&
        (GetHitByPlayerProjectile() || InDetectionRange(mgr, CTriggerData(0.f)))) {
      chassis->SetHitByPlayerProjectile(true);
    }
    const float turn = dt * mData.mScanningTurnSpeed;
    const CVector3f yAxis = GetTransform().GetColumn(kDY);
    const CVector3f xAxis = GetTransform().GetColumn(kDX);
    const CVector3f forward(CVector2f(yAxis.GetX(), yAxis.GetY()), 0.f);
    const CVector3f right(CVector2f(xAxis.GetX(), xAxis.GetY()), 0.f);
    if (forward.IsMagnitudeSafe() && right.IsMagnitudeSafe()) {
      mEditorProperties = CVector3f::Slerp(forward.AsNormalized(), right.AsNormalized(),
                                           CRelAngle::FromRadians(turn * 0.017453292f));
    }
    break;
  }
  case kStateMsg_Deactivate:
    PortUpdateEffectStates(mgr);
    break;
  default:
    break;
  }
}

// fn_75_50B0 (the head's FaceTarget state, 0x50B0-0x523C). Activate (0x50E8): the vector at
// 0xF24 becomes the head's transform's Y column (0x28, 0x38 and 0x48). Update (0x5104), only
// with a frame time above 0.0f (0x510C ble): the target at 0xEFE is looked up (0x5124); the
// offset to it with z set to 0.0f (0x5130-0x516C) must be IsMagnitudeSafe (0x5170). The turn
// is the frame time times the data's max turn speed (0x7C8, 0x519C-0x51A0) in degrees
// (lbl_75_rodata_74, 0.017453292f): with the angle between the Y column and the offset
// (0x51A4 GetAngleDiff) below it the vector at 0xF24 is the offset normalized (0x51BC-0x51DC),
// else the Slerp from the Y column toward the normalized offset by the turn (0x51E4-0x521C).
// Deactivate and other messages do nothing.
// Arms: ALL-ARMS; the turn ran (`fsm u0520 FaceTarget`); no target, a zero offset (the head as its
// own target) and a zero dt forced (split23 force_ret, splitter_split23.py).
// @ fn_75_50B0
void CSplitterCommandModule::FaceTarget(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    mEditorProperties = GetTransform().GetColumn(kDY);
    return;
  }
  // 0x510C fcmpo dt, 0.0f (lbl_75_rodata_40); 0x5110 ble: a NaN frame time skips the update.
  if (msg != kStateMsg_Update || !(dt > 0.f)) {
    return;
  }
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mSyncedId));
  if (target == nullptr) {
    return;
  }
  CVector3f offset = target->GetTranslation() - GetTranslation();
  offset.SetZ(0.f);
  if (!offset.IsMagnitudeSafe()) {
    return;
  }
  const CVector3f forward = GetTransform().GetColumn(kDY);
  const float turn = 0.017453292f * (dt * mData.mMaxTurnSpeed);
  if (CVector3f::GetAngleDiff(forward, offset) < turn) {
    mEditorProperties = offset.AsNormalized();
  } else {
    mEditorProperties =
        CVector3f::Slerp(forward, offset.AsNormalized(), CRelAngle::FromRadians(turn));
  }
}

// fn_75_5A80 (0x5A80-0x5AD8): the head's laser origin - the translation of the locator at
// 0xEF8 (0x5A9C GetLctrTransform, 0x5AAC-0x5AC0).
// @ fn_75_5A80
CVector3f CSplitterCommandModule::PortLaserOrigin(CStateManager&) const {
  return GetLctrTransform(mLookAtSegment).GetTranslation();
}

// fn_75_5AD8 (0x5AD8-0x5B24): only for a docked head (the chassis id at 0xEFA valid,
// 0x5ADC-0x5AE8) the laser sweep stage at 0xF34 is 0 and the two sweep ends are kept at 0xF38
// and 0xF44 (0x5AEC-0x5B20).
// @ fn_75_5AD8
void CSplitterCommandModule::PortStartLaserSweep(const CVector3f& first, const CVector3f& second) {
  if (mChassisId == kInvalidUniqueId) {
    return;
  }
  mLaserSweepStage = 0;
  mLaserSweepStart = first;
  mLaserSweepEnd = second;
}

// fn_75_44DC (the head's LaserSweep state, 0x44DC-0x4864). Activate (0x4528): the look-at
// vector at 0xF50 becomes the transform's Y column; with the target at 0xEFE (0x4550) the sweep
// ends swap when the end at 0xF44 is closer in angle to that vector than the start at 0xF38
// (0x455C-0x4600, both offsets from the head's position); then the data's laser charge-up sound
// (0x888) plays from the head (0x462C AddEmitter: volume 0x7F, the area at 0x4, acoustics,
// not looped, kMedPriority). Update (0x4634), only with a frame time above 0.0f: the current
// end (0xF38 for stage 0, else 0xF44) is offset from the locator at 0xEF8 (0x4680
// GetLctrTransform); its flat form (z 0.0f) must be IsMagnitudeSafe, else the stage ends (-1,
// 0x482C). The turn is the frame time times the data's laser sweep turn speed (0x808) in degrees
// (lbl_75_rodata_74). Within it of the Y column (0x4710 GetAngleDiff) the vector at 0xF24 is the
// normalized flat offset and 0xF50 the normalized offset; stage 0 then moves to 1 and fires
// (0x4790 fn_75_1974, the laser shot at that end), any other stage ends (-1). Otherwise both
// vectors Slerp toward those by the turn (0x47A4-0x4824). Deactivate (0x4838): the projectile
// despawns (fn_75_1790) and the stage ends (-1).
// Arms: ALL-ARMS, splitter_split25 (tools/cov/specs/splitter_split25.py, 914f1381, gcov merged): activate with
// the ends swapped, not swapped and without the target; update with a zero frame time, the end
// over the locator (flat unsafe), ahead at stages 0 (fires) and 1 (ends), to the side (both
// Slerps); deactivate and an out-of-range message, by gdb calls.
// @ fn_75_44DC
void CSplitterCommandModule::LaserSweep(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    mLookAtOffset = GetTransform().GetColumn(kDY);
    if (mgr.GetObjectById(mSyncedId) != nullptr) {  // 0x4554-0x4558
      const float startAngle =
          CVector3f::GetAngleDiff(mLookAtOffset, mLaserSweepStart - GetTranslation());
      if (CVector3f::GetAngleDiff(mLookAtOffset, mLaserSweepEnd - GetTranslation()) <
          startAngle) {
        const CVector3f start = mLaserSweepStart;
        mLaserSweepStart = mLaserSweepEnd;
        mLaserSweepEnd = start;
      }
    }
    CSfxManager::AddEmitter(static_cast< ushort >(mData.mSoundLaserChargeUp & 0xFFFF),
                            GetTranslation(), 0x7F, GetCurrentAreaId().Value(), true, false,
                            CSfxManager::kMedPriority);
  } else if (msg == kStateMsg_Update) {
    // 0x463C fcmpo, 0x4640 ble: only for a frame time above 0.0f (a NaN time leaves).
    if (!(dt > 0.f)) {
      return;
    }
    const CVector3f end = mLaserSweepStage == 0 ? mLaserSweepStart : mLaserSweepEnd;
    const CVector3f offset = end - GetLctrTransform(mLookAtSegment).GetTranslation();
    const CVector3f flat(offset.GetX(), offset.GetY(), 0.f);
    if (!flat.IsMagnitudeSafe()) {
      mLaserSweepStage = -1;
      return;
    }
    const CVector3f forward = GetTransform().GetColumn(kDY);
    const float turn = 0.017453292f * (dt * mData.mLaserSweepTurnSpeed);
    if (CVector3f::GetAngleDiff(forward, flat) < turn) {
      mEditorProperties = flat.AsNormalized();
      mLookAtOffset = offset.AsNormalized();
      if (mLaserSweepStage == 0) {
        mLaserSweepStage = 1;
        PortFireLaserSweep(mgr, end);
      } else {
        mLaserSweepStage = -1;
      }
    } else {
      mEditorProperties =
          CVector3f::Slerp(forward, flat.AsNormalized(), CRelAngle::FromRadians(turn));
      mLookAtOffset = CVector3f::Slerp(mLookAtOffset.AsNormalized(), offset.AsNormalized(),
                                       CRelAngle::FromRadians(turn));
    }
  } else if (msg == kStateMsg_Deactivate) {
    PortDespawnProjectile(mgr);
    mLaserSweepStage = -1;
  }
}

// fn_75_1974 (the head's laser shot, 0x1974-0x1C0C): the laser sweep beam's info (data 0x81C)
// becomes a CBeamInfo with the attributes 0x91 (0x199C fn_80239B64, the DOL's TLdrToBeamInfo;
// 0x19A8 fn_75_1C0C copies it). A new id (0x19B4 AllocateUniqueId) gets the 0x6A8-byte
// CPlasmaProjectile "LaserSweepBeam" (lbl_75_rodata_A0 + 0x245; 0x19D8 new, 0x1AA0 ctor): the
// weapon at 0xE84, weapon type 2, the identity transform, material 0x14 (kMT_ProjectilePassthrough),
// the converted sweep damage at 0xCF0, the area at 0x4, this actor as owner, a default
// CWeaponAssetInfo (8 entries of -1, 0x19E4-0x1A20), not drawn owner first and the attributes
// 0x21000. When it exists (0x1AB8): it fires (the vtable word 0x94, Fire with false; 0x1B2C) along
// LookAt from the locator at 0xEF8 (0x1ACC GetLctrTransform) toward the given end with the up
// vector (0x1B08), takes the chassis id at 0xEFA as its next draw node (its 0x12A, 0x1B48) and is
// added (0x1B4C AddObject). Then the data's laser sweep sound (0x884) loops from the beam's
// collision point (its 0x444-0x44C, CBeamProjectile::GetCurrentPos) with the 3D parameters 150.0f
// and 0.1f (lbl_75_rodata_2C / lbl_75_rodata_48), flags 1, volume 0x7F and priority 0x23, the up
// vector as direction, the area at 0x4, acoustics on and looped (0x1BD8 AddEmitter); its handle
// goes to 0xF5C and the beam's id to 0xF02 (0x1BDC-0x1BE8). Last the effect states update
// (0x1BF4 fn_75_1578), with or without the beam.
// Arms: all ran - the beam info, the new beam, its fire, the sound and the two ids.  The coverage
// build's merged profile says 19/19 lines and 4/4 branches, ALL-ARMS.
// @ fn_75_1974
void CSplitterCommandModule::PortFireLaserSweep(CStateManager& mgr, const CVector3f& end) {
  const CBeamInfo beamInfo = TLdrToBeamInfo(mData.mLaserSweepBeamInfo, 0x91);
  const TUniqueId beamId = mgr.AllocateUniqueId();
  CPlasmaProjectile* beam = rs_new CPlasmaProjectile(
      mLaserSweepProjectileInfo.Token(), rstl::string_l("LaserSweepBeam"), EWeaponType(2),
      beamInfo, CTransform4f::Identity(), kMT_ProjectilePassthrough, mData.mLaserSweepDamage,
      beamId, GetCurrentAreaId(), GetUniqueId(), CWeaponAssetInfo(), false, 0x21000);
  // The null arm is an allocation failure only (rs_new); it never happens in a run.
  if (beam != nullptr) {  // 0x1AB8-0x1ABC
    beam->Fire(CTransform4f::LookAt(GetLctrTransform(mLookAtSegment).GetTranslation(), end,
                                    CVector3f::Up()),
               mgr, false);
    beam->SetNextDrawNode(mChassisId);
    mgr.AddObject(*beam);
    CAudioSys::C3DEmitterParmData parms(150.f, 0.1f, 1, 0x7F, 0x23);
    parms.mPos = beam->GetCurrentPos();
    parms.mDir = CVector3f::Up();
    parms.mSfxId = static_cast< ushort >(mData.mSoundLaserSweep & 0xFFFF);  // 0x1BD0-0x1BD4
    mLookAtSfxHandle = CSfxManager::AddEmitter(parms, GetCurrentAreaId().Value(), true, true,
                                               CSfxManager::kMedPriority);
    mLookAtTargetId = beamId;
  }
  PortUpdateEffectStates(mgr);
}

// fn_75_5A40 (the head's StateOver trigger, 0x5A40-0x5A50): a REL copy of the DOL's
// CPatterned::GetAnimOver (0x80151990) - the animation state at 0x6B4 is kAS_Over (3).
bool CSplitterCommandModule::StateOver(CStateManager& mgr, const CTriggerData& data) const {
  return GetAnimOver(mgr, data);
}

// fn_75_55F8 (the head's ShouldLaserSweep trigger, 0x55F8-0x5610): the laser sweep stage at
// 0xF34 is not -1 (0x55FC-0x5608: subfic -1, addi 1, or, srwi 31).
// @ fn_75_55F8
bool CSplitterCommandModule::ShouldLaserSweep(CStateManager&, const CTriggerData&) const {
  return mLaserSweepStage != -1;
}

// The head's own effect-state update (fn_75_1578, 0x218) is ported as
// CSplitterCommandModule::PortUpdateEffectStates.

// fn_75_3E60 (the head's SelectTarget code, 0x3E60-0x3EEC): with the head's synced id at 0xEFE
// invalid (0x3E84 lhz, 0x3E8C cmplw, 0x3E90 bne) the id of the actor the state manager holds at
// 0x14FC (0x3E94 lwz - the player, the actor the head aims at) is taken (0x3E98 lhz of that
// actor's own id at 0x8) and kept at 0xEFE (0x3E9C sth); then fn_75_1578 runs (0x3EA8) and the
// head sends the state 'ATTK' to its connections (0x3EB0 lis 0x4154, 0x3EC0 addi 0x544b,
// 0x3ED0 bl SendScriptMsgs).
//
// The retail's call is SendScriptMsgs(this, 0x4154544B, mgr, id, -1): the state, the state
// manager, the head's own unique id (0x3EAC lhz 0x8(r30) - the entity's own id - copied to the
// stack at 0x3ECC sth) and the message 0xFFFFFFFF (0x3EC8 li r7, -1). The retail hands that id
// over by address (0x3EC4 addi r6, sp+0xc), because its TUniqueId is a 16-bit class; the host's
// CEntity::SendScriptMsgs takes it by value, so the port passes the id itself. The host's names
// for the two constants: kSS_Attack (0x4154544B) and kSM_None (0xFFFFFFFF). The message
// argument is the one a connection's own message must differ from, so "none" reaches every
// connection but the ones that ask for no message.
// Arms: all ran - the synced id is taken or the player's, the effect states are updated and the
// state 'ATTK' goes out; the coverage build's merged profile says 6/6 lines and 2/2 branches,
// ALL-ARMS.
// @ fn_75_3E60
void CSplitterCommandModule::SelectTarget(CStateManager& mgr, float) {
  if (mSyncedId == kInvalidUniqueId) {
    mSyncedId = mgr.GetPlayer(0)->GetUniqueId();
  }
  PortUpdateEffectStates(mgr);
  SendScriptMsgs(kSS_Attack, mgr, GetUniqueId(), kSM_None);
}


// fn_75_6B08. The base call is the retail one: EPatternedAI 0x39 (li r4, 0x39 at 0x00006B68),
// flavor 0 (li r7, 0x0 at 0x00006B30), movement 1 / collider 1 / body 7 (the stack words at
// 0x00006B60..0x00006B6C - the head flies), all passed to CPatterned at 0x00006B78.
// Arms: all ran; the coverage build's merged profile says 22/23 lines and 27/27 branches,
// ALL-ARMS.
// @ fn_75_6B08
CSplitterCommandModule::CSplitterCommandModule(TUniqueId uid, const rstl::string& name,
                                                 const CEntityInfo& info, const CTransform4f& xf,
                                                 const CModelData& modelData,
                                                 const CActorParameters& actParms,
                                                 const CPatternedInfo& pInfo,
                                                 const CSplitterCommandModuleData& data)
: CPatterned(static_cast< EPatternedAI >(0x39), uid, name, kFT_Zero, info, xf, modelData, pInfo,
             kMT_Flyer, kCT_One, kBT_4WayBlended, actParms)
, mData(data) // guest 0x7C0: the parameter block fn_75_72BC copies in
// fn_75_6B08 @0x00006BA0: the CPathFindSearch (0xec) at 0xd6c, built with the null area, the
// flags 3, the patterned info's pathfinding index and the two radii lbl_75_rodata_34 (1.0f);
// fn_75_0 returns it.
, mPathFindSearch(nullptr, 3, pInfo.GetPathfindingIndex(), 1.f, 1.f, 0, CPFRegion::kRP_Center)
// fn_75_6B08 @0x00006BC4: the CProjectileInfo (0x28) at 0xe5c, built from the data's laser pulse
// projectile (+0x20) and damage (+0x514 of the runtime struct); fn_75_8 returns it.
, mProjectileInfo(data.mLaserPulseProjectile, data.mLaserPulseDamage)
// fn_75_6B08 @0x00006BD8-0x00006BEC: the second CProjectileInfo at 0xe84, built from the laser
// sweep beam's weapon system (data +0x60, the beam info's second word) and a default damage
// (fn_75_722C: weapon -1, every amount 0.0f, the sounds 0xFFFF); fn_75_1974 fires it.
, mLaserSweepProjectileInfo(data.mLaserSweepBeamInfo.weaponSystem, CDamageInfo())
, mOwnVulnerability(*CPatterned::GetDamageVulnerability()) {
  // fn_75_6B08 @0x00006C04-0x00006D40: the scalar fields. The members' own initializers give the
  // zeros, the -1s and the invalid ids; these are the rest: the flash colour at 0xF04 is White
  // (0x6C68), the chassis timer at 0xF14 lbl_75_rodata_90 (3.0f, 0x6CC4), the vector at
  // 0xF24-0xF2C the transform's Y column (the xf's floats at 0x4, 0x14 and 0x24, 0x6C9C-0x6CDC)
  // and the look-at offset at 0xF50 the zero vector (0x6D18-0x6D2C).
  mFlashColor = CColor::White();
  mChassisTimer = 3.f;
  mEditorProperties = xf.GetColumn(kDY);
  mLookAtOffset = CVector3f::Zero();
  // fn_75_6B08 @0x00006D44-0x00006DA0: the flag byte at 0xF6A is 0x92 (rlwimi of 1 into the
  // byte's 0x80, 0x10 and 0x02, of 0 into the rest): PathOver waits (0x80), the Dodge flag
  // (0x10) and the first dock's sound is skipped (0x02, fn_75_3EEC).
  mAutoDestructFlags = 0x92;
  // fn_75_6B08 @0x00006DA4-0x00006DAC: both projectile infos' weapon tokens are locked.
  mProjectileInfo.Token().Lock();
  mLaserSweepProjectileInfo.Token().Lock();
  // fn_75_6B08 @0x00006DB0-0x00006DE8: the locator at 0xEF8 is the model's "Beam_LCTR"
  // (lbl_75_rodata_293) segment.
  mLookAtSegment =
      GetModelData()->GetAnimationData()->GetLocatorSegId(rstl::string_l("Beam_LCTR"));
  // fn_75_6B08 @0x00006DEC-0x00006E5C: the material filter - the include mask is the data word
  // 0x3B (lbl_75_data_428, kMT_Solid), the exclude mask the data words 0x2F and 0x21
  // (lbl_75_data_42C and lbl_75_data_430, kMT_CollisionActor and kMT_Character) and the type 3
  // (0x00006E3C, the include/exclude one). Excluding kMT_Character keeps the docked head out of
  // the chassis's collider list (CSortedListManager asks both actors' filters).
  SetMaterialFilter(CMaterialFilter(CMaterialList(kMT_Solid),
                                    CMaterialList(kMT_CollisionActor, kMT_Character),
                                    CMaterialFilter::kFT_IncludeExclude));
  // fn_75_6B08 @0x00006E60-0x00006EF8: the dodge distance at 0xEE8 is the model's scale x times
  // the animation distance of a right (3) dodge (1) step (body state 3, fn_75_6F1C).
  mDodgeDistance = GetModelData()->GetScale().GetX() *
                   GetAnimationDistance(CPASAnimParmData(pas::kAS_Step, CPASAnimParm::FromEnum(3),
                                                         CPASAnimParm::FromEnum(1)));
}

// vtable slot 0x08 = fn_75_6798 (0x6798-0x6890): the members' destructors in reverse order -
// the vulnerability 0xEAC (0x67CC), the two projectile infos' tokens 0xE84/0xE5C (0x67F0,
// 0x6814), the collision actor manager 0xE58 (0x6820 lwz, 0x6828 bl __dt__22CCollisionActorManager
// with r4 = 1: deleted), the parameter block 0x7C0 (0x683C-0x6854) - then the base's (0x6860
// fn_75_6890) and, with the delete flag (0x6864 extsh., ble), CMemory::Free (0x6870). The
// member-address tests (0x67D0-0x682C addic.) are the compiler's, never null. The host's implicit
// member destructors do the rest; the manager is a raw pointer, so it is deleted here.
// @ fn_75_6798
CSplitterCommandModule::~CSplitterCommandModule() { delete mCollisionActorManager; }

// A body state is only ever installed by CBodyController::Activate, and CPatterned::Think /
// ThinkAboutMove all dereference the current state. The analysis does not say where retail
// activates it, so the port activates on kSM_Create and, as the other patterned actors in this
// port do, again on demand in every entry point that can run first.
void CSplitterCommandModule::EnsureBodyActive(CStateManager& mgr) {
  if (!BodyController()->GetIsActive()) {
    BodyController()->Activate(mgr, pas::kAS_Invalid);
  }
}

// fn_75_5A54 (the module's auto-destruct, 0x5A54-0x5A80): only for a live module (the base's flag
// byte at 0x420, the word's bit 25 - the byte's 0x40, 0x5A58 extrwi.) whose 0xF6A 0x04 bit is still
// clear (0x5A64 extrwi.) the bit is set (0x5A70 rlwimi) and the death timer at 0xEF0 is the time
// (0x5A78 stfs); fn_75_32F4 counts it down and kills the module at zero.
// Arms: 4/4, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov merged):
// alive with the 0x04 bit clear (set, the timer taken), alive with it set, dead - gdb calls.
// @ fn_75_5A54
void CSplitterCommandModule::PortAutoDestruct(float time) {
  if (!GetAlive()) {
    return;
  }
  if ((mAutoDestructFlags & 0x4) != 0) {
    return;
  }
  mAutoDestructFlags |= 0x4;
  mDeathTimer = time;
}

// The module's own calls in the command module's chassis check: both are over 0x80, so each
// stays a named once-logging shell.

// The vtable word 0x4C of the object fn_75_2BE0 returns. The port has no name for that DOL
// virtual yet, so it stays a shell. The CCollisionActor's virtual at the vtable word 0x38 (the
// float at its +0x4) is CCollisionActor::HealthInfo() (found while porting fn_75_2CE0, which
// calls it), so the body asks the host for it now.

// fn_75_2CE0. The head's shield (0x2CE0-0x3004). The flag (the third argument, r5) is the "a shield goes up"
// one: the asm tests its low byte (0x2D0C clrlwi with the word's bit 24 - the byte's 0xFF). With
// it set the shield-on sound (the sound id at 0x89C) is added (0x2D1C-0x2E44: the volume 0x7F,
// the area at 0x4, useAcoustics true, looped false) and its handle is dropped - the retail writes
// it to a stack slot it never reads. Then the tracked emitter (the handle at 0xF60) is removed and
// cleared (0x2D48-0x2D6C). The beam collision actor (the id at 0xF00) is looked up (0x2D7C
// ObjectById) and cast (0x2D80); with no actor the body ends (0x2D88). Its health info's health
// (the float at +0x4, 0x2DA0) is set to the shield's HP (the float at 0x804). The state at 0xEE0
// (0x2DA8-0x2DC0) then picks one of three:
//   1 (0x2DC4): the character material (0x21) goes to the collision actor and the material 0x36 to
//     the head, the light shield's vulnerability (0xD0C) goes to the collision actor, the
//     "LightShield" effect is on and the "DarkShield" effect is off, and the light shield's sound
//     (the sound id at 0x894) is added as a looping emitter whose handle goes to 0xF60
//     (0x2E78-0x2E80).
//   2 (0x2E88): the same with the material removed from the collision actor, the dark shield's
//     vulnerability (0xD3C), the effects swapped and the dark shield's sound (the sound id at
//     0x898).
//   otherwise (0x2F4C): both materials are removed, the collision actor's vulnerability becomes the
//     pass-through one (fn_800DBB50) and both effects are off.
// Arms (the coverage build, 2026-10-22, K3 fight natural + forced: 34/34 lines, 18/18 branches):
// the flag clear and set, the actor and no actor, the state 1 and the default ran (the actor at
// 0xF00 exists since fn_75_3460's name fix); forced (SPLITTER_FORCE=R_SHIELD, force_ret.py): the
// state 2 with the flag set, no actor (0xF00 invalid), and the handle at 0xF60 (the run's
// AddEmitter hands back null handles, so 0xF60 is never set naturally).
// asmcheck: the rstl string's internal_dereference (0x2E1C) is the effect names' string_l
// temporaries' destructor.
// @ fn_75_2CE0
void CSplitterCommandModule::PortUpdateShield(CStateManager& mgr, bool shieldUp) {
  if (shieldUp) {
    // 0x2D1C-0x2E44: the shield-on sound. The retail's emitter position is the vector at 0x54
    // and its area the word at 0x4; the host's are the actor's translation and its area.
    CSfxManager::AddEmitter(static_cast< ushort >(mData.mSoundShieldOn & 0xFFFF), GetTranslation(),
                            0x7F, GetCurrentAreaId().Value(), true, false, CSfxManager::kMedPriority);
  }
  // 0x2D48-0x2D6C: the tracked emitter (the handle at 0xF60) is removed and the handle cleared.
  if (mSoundHandle) {
    CSfxManager::RemoveEmitter(mSoundHandle);
    mSoundHandle.Clear();
  }
  // 0x2D6C-0x2D88: the beam collision actor (the id at 0xF00) is looked up and cast; with no
  // actor the body ends.
  CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(mSoundTargetId));
  if (actor == nullptr) {
    return;
  }
  // 0x2D8C-0x2DA0: the collision actor's health info (the vtable word 0x38) and its health (the
  // float at +0x4) get the shield's HP (the float at 0x804).
  actor->HealthInfo()->SetHP(mData.mShieldHP);
  switch (mCollisionState) {
  case 1: {
    // 0x2DC4-0x2DE0: the character material (0x21) goes to the collision actor and the material
    // 0x36 to the head.
    actor->AddMaterial(kMT_Character, mgr);
    AddMaterial(kMT_Unknown54, mgr);
    // 0x2DE4-0x2DEC: the light shield's vulnerability (0xD0C) goes to the collision actor.
    actor->SetDamageVulnerability(mData.mLightShieldVulnerability);
    // 0x2DF0-0x2E4C: the "LightShield" effect (rodata 0x10) is on and the "DarkShield" effect
    // (rodata 0x14) is off.
    AnimationData()->SetEffectState(rstl::string_l("LightShield"), true, mgr);
    AnimationData()->SetEffectState(rstl::string_l("DarkShield"), false, mgr);
    // 0x2E50-0x2E80: the light shield's sound (the sound id at 0x894) is added as a looping
    // emitter and its handle goes to 0xF60.
    mSoundHandle = CSfxManager::AddEmitter(static_cast< ushort >(mData.mSoundLightShield & 0xFFFF),
                                           GetTranslation(), 0x7F, GetCurrentAreaId().Value(), true,
                                           true, CSfxManager::kMedPriority);
    break;
  }
  case 2: {
    // 0x2E88-0x2EA4: the character material (0x21) is removed from the collision actor and the
    // material 0x36 is added to the head.
    actor->RemoveMaterial(kMT_Character, mgr);
    AddMaterial(kMT_Unknown54, mgr);
    // 0x2EA8-0x2EB0: the dark shield's vulnerability (0xD3C) goes to the collision actor.
    actor->SetDamageVulnerability(mData.mDarkShieldVulnerability);
    // 0x2EB4-0x2F0C: the effects are swapped.
    AnimationData()->SetEffectState(rstl::string_l("LightShield"), false, mgr);
    AnimationData()->SetEffectState(rstl::string_l("DarkShield"), true, mgr);
    // 0x2F14-0x2F44: the dark shield's sound (the sound id at 0x898) is added as a looping
    // emitter and its handle goes to 0xF60.
    mSoundHandle = CSfxManager::AddEmitter(static_cast< ushort >(mData.mSoundDarkShield & 0xFFFF),
                                           GetTranslation(), 0x7F, GetCurrentAreaId().Value(), true,
                                           true, CSfxManager::kMedPriority);
    break;
  }
  default: {
    // 0x2F4C-0x2F6C: both materials are removed.
    actor->RemoveMaterial(kMT_Character, mgr);
    RemoveMaterial(kMT_Unknown54, mgr);
    // 0x2F6C-0x2F78: the collision actor's vulnerability becomes the pass-through one.
    actor->SetDamageVulnerability(CDamageVulnerability::PassThroughVulnerabilty());
    // 0x2F7C-0x2FD8: both effects are off.
    AnimationData()->SetEffectState(rstl::string_l("LightShield"), false, mgr);
    AnimationData()->SetEffectState(rstl::string_l("DarkShield"), false, mgr);
    break;
  }
  }
}

// fn_75_2BE0. The head's shield particle effect (0x2BE0-0x2BC4): the state at 0xEE0 picks the
// name - 1 gives "LightShield" (rodata 0x10, 0x2C18-0x2C34), any other state gives "DarkShield"
// (rodata 0x14, 0x2C38-0x2C50) and the state 0 gives nothing (0x2C00-0x2C04, the return at
// 0x2CA0). The name is the animation data's first particle effect (0x2C88
// GetFirstParticleEffect), which is what the body returns.
// Arms: the state 0 ran in the fight; the states 1 and 2 were forced under gdb (the head's
// shield never comes up in the shipped spawns).
// @ fn_75_2BE0
CParticleGenInfo* CSplitterCommandModule::PortGetShieldEffect() {
  if (mCollisionState == 0) {
    return nullptr;
  }
  const char* name = mCollisionState == 1 ? "LightShield" : "DarkShield";
  return AnimationData()->GetFirstParticleEffect(rstl::string_l(name));
}

// Arms: all ran; the coverage build's merged profile says 6/6 lines and 2/2 branches, ALL-ARMS.
// @ fn_75_10D0
// The module's own transform (0x10D0-0x1188): the actor's own transform (the guest's at 0x24) is
// copied (0x10F0-0x10F4), the "Beam_LCTR" locator's transform (rodata 0x293, 0x1118
// GetLocatorTransform) gives the point, the point is moved through the copy (0x1140 the
// operator*) and the result's translation goes into the copy (0x1154-0x115C), which is the
// result.
void CSplitterCommandModule::PortModuleTransform(CTransform4f& out) const {
  CTransform4f copy = GetTransform();
  const CVector3f moved = copy * GetLocatorTransform(rstl::string_l("Beam_LCTR")).GetTranslation();
  copy.SetTranslation(moved);
  out = copy;
}

// fn_75_3004 (0x1A4) - the command module's collision update. The body: the module's own call
// fn_75_2BE0 (0x3024) gives an object whose vtable word 0x4C is called with the colour at 0xF04
// (0x3030-0x3040); the DOL's CSfxManager::UpdateEmitter runs with the handle at 0xF60, the
// position at 0x54 and the byte 0x7f (0x3044-0x3074); the id at 0xF00 is looked up (0x3088) and
// cast to a CCollisionActor (0x308C); the manager at 0xE58, when there is one, is deactivated
// when this actor has no chassis (0x3098-0x30C0 - the DOL's
// CCollisionActorManager::SetActive, with the flag built by the cntlzw idiom at 0x30B4-0x30BC).
// With the state in 0xEE0 at 1 or 2 (0x30C8-0x30D8) the shield goes down (0x3114-0x3130: the
// state cleared, the timer 0xF10 set to the data's 0x800 and fn_75_2CE0 with 0) unless the flag
// byte 0xF6A's 0x08 is set (0x30E0, a clear bit branches straight to 0x3114) and the collision
// actor's health (0x30E8-0x3100) is above zero. With any other state, the flag set and the timer
// at zero or below the state goes to 1, or 2 after a 1 (0x3138-0x3170), and fn_75_2CE0 runs
// with 1 (0x3174-0x3188).
// Arms: ALL-ARMS; states 0-3 with the bit set or clear, no actor, no manager and a shield actor at
// 0.0f health (CCollisionActor::HealthInfo overridden) forced (split23 force_ret, splitter_split23.py).
// @ fn_75_3004
void CSplitterCommandModule::PortUpdateCollision(CStateManager& mgr) {
  CParticleGenInfo* effect = PortGetShieldEffect();
  if (effect != nullptr) {  // 0x3028 cmplwi
    // asm 0x3034-0x3044: the effect's virtual at the vtable word 0x4C is called with the
    // module's flash colour at 0xF04 - CParticleGenInfo::SetModulationColor.
    effect->SetModulationColor(mFlashColor);
  }
  // asm 0x3048-0x3078: the sound handle at 0xF60 is updated with the module's position (0x54)
  // and its transform's Y column (0x28, 0x38 and 0x48), the volume 0x7F.
  CSfxManager::UpdateEmitter(mSoundHandle, GetTranslation(), GetTransform().GetColumn(kDY), 0x7F);
  // 0x3088 ObjectById, 0x308C TCastToPtr (a missing object casts to null), 0x3090 mr. + beq.
  CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(mSoundTargetId));
  if (actor == nullptr) {
    return;
  }
  if (mCollisionActorManager != nullptr) {  // 0x309C cmplwi
    mCollisionActorManager->SetActive(mgr, mChassisId == kInvalidUniqueId);  // 0x30A4-0x30C0
  }
  if (mCollisionState > 0 && mCollisionState < 3) {  // 0x30C8-0x30D8
    // 0x30E0 extrwi. bit 28 (0x8) + 0x30E4 beq 0x3114: a clear bit takes the shield down at once.
    // 0x30E8-0x3110: the collision actor's virtual at the vtable word 0x38 is
    // CCollisionActor::HealthInfo() and the float at its +0x4 is the health; 0x3108 fcmpo,
    // 0x310C cror eq,lt,eq, 0x3110 bne: the shield stays while the health is above zero (a NaN
    // health keeps it).
    if ((mAutoDestructFlags & 0x8) != 0 && !(actor->HealthInfo()->GetHP() <= 0.f)) {
      return;
    }
    mCollisionState = 0;                         // 0x311C
    mCountdownTimerA = mData.mResetShieldTime;   // 0x3128-0x312C
    // 0x3124-0x3130: the retail calls fn_75_2CE0 with the flag clear (the shield goes down).
    PortUpdateShield(mgr, false);
  } else if ((mAutoDestructFlags & 0x8) != 0 && mCountdownTimerA <= 0.f) {  // 0x313C, 0x3150-0x3158
    mCollisionState = mPreviousCollisionState == 1 ? 2 : 1;  // 0x315C-0x3170
    mPreviousCollisionState = mCollisionState;               // 0x3180-0x3184
    // 0x317C-0x3188: the retail calls fn_75_2CE0 with the flag set (a shield goes up).
    PortUpdateShield(mgr, true);
  }
}

// The head's "dock to the chassis" (0x3EEC-0x3FE0): the id at 0xEFC (the connected object the
// XAND message found) is looked up (0x3F18 ObjectById) and cast to a main chassis (0x3F1C); with
// no chassis the body ends (0x3F24). The chassis is asked to accept this head (0x3F3C
// fn_75_E6E4, with the head's own id at 0x8); when it does not (0x3F40, the low byte) the body
// ends. Then the chassis's id goes to 0xEFA (0x3F50) - the id fn_75_31A8 and the beam-effect
// gate fn_75_58E8 read - the id at 0xEFC is cleared (0x3F58) and the id goes to CActor 0x12A, the
// next draw node, so the head draws after the chassis (0x3F60).
// The docking sound (the sound id at 0x88C) is added when the flag byte 0xF6A's 0x02 bit is clear
// (0x3F6C extrwi, the word's bit 30) or the data's flag word at 0x7C0 has its 0x1 bit clear
// (0x3F78 clrlwi, the word's bit 31); the flag byte's 0x02 bit is cleared either way (0x3FB4
// rlwimi, the word's bit 30). The locomotion type becomes 1 (kLT_Relaxed, 0x3FC4
// SetLocomotionType).
// Arms: 8/8, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov
// merged): the docking in play; no chassis, a chassis that already has a head, and the 0x02 bit
// and the data's 0x1 bit in every combination by gdb calls.
// @ fn_75_3EEC
void CSplitterCommandModule::PortDockToChassis(CStateManager& mgr) {
  CSplitterMainChassis* chassis =
      TCastToPtr< CSplitterMainChassis >(mgr.ObjectById(mCheckedChassisId));
  // 0x3F24 / 0x3F44: no chassis, or one that refuses the head, branch to 0x3FBC - the
  // locomotion type below is still set.
  if (chassis != nullptr && chassis->PortAcceptHead(mgr, GetUniqueId())) {
    mChassisId = mCheckedChassisId;
    mCheckedChassisId = kInvalidUniqueId;
    SetNextDrawNode(mChassisId); // 0x3F60: CActor 0x12A, the head draws after the chassis
    if ((mAutoDestructFlags & 0x2) == 0 || (mData.mUnknownBD80FD94 & 0x1) == 0) {
      // 0x3F80-0x3FA8: the docking sound; its handle is dropped (the retail writes it to a stack
      // slot it never reads).
      CSfxManager::AddEmitter(static_cast< ushort >(mData.mSoundDocking & 0xFFFF),
                              GetTranslation(), 0x7F, GetCurrentAreaId().Value(), true, false,
                              CSfxManager::kMedPriority);
    }
    mAutoDestructFlags &= ~static_cast< uchar >(0x2);
  }
  BodyController()->SetLocomotionType(pas::kLT_Relaxed); // 0x3FBC-0x3FC4
}

// Arms: the flash weight above zero (asm 0x336C) and the countdown below zero (asm 0x3374), the
// flag byte's 0x04 bit set (asm 0x33CC) with the head alive (asm 0x33D8) and dead, and the death
// timer at or below zero (asm 0x33FC) and above it.  The fight never flashes the head and never
// sets the flag byte's 0x04 bit, so the members the arms read are written through $this at the
// body's entry (tools/cov/specs/splitter_helpers_force.py: SPLITTER_FORCE=32F4, 32F4D, 32F4DEAD,
// 32F4F).  The coverage build's merged profile of those runs and the natural run says 17/17 lines
// and 10/10 branches, ALL-ARMS.
// @ fn_75_32F4
// The command module's flash and death (0x32F4-0x3460): three timers are counted down (0xF10 at
// 0x332C, 0xF0C at 0x333C and 0xF14 at 0x3344) and two counted up (0xF18 at 0x3354 and 0xF1C at
// 0x3360). While the flash weight 0xF08 is above zero (0x336C-0x3370) it is counted down and
// clamped at zero (0x3374-0x338C) and the colour at 0xF04 is the Lerp of the white and the base
// colour (0x450) with the weight capped at 1.0f (0x3390-0x33A8, 0x33C0). Then the flag byte at
// 0xF6A (the word's bit 29, the byte's 0x04, 0x33CC-0x33D4) and the alive bit of the byte at
// 0x420 (the word's bit 25, the byte's 0x40, 0x33D8-0x33E0) gate the death timer 0xEF0, which is
// counted down (0x33EC-0x33F4) and, at zero or below (0x33FC-0x3404), kills the actor with the
// direction (the transform's Y column, the CActor's 0x28, 0x38 and 0x48) and the state
// 0x5241544C ('RATL', 0x340C-0x3434).
void CSplitterCommandModule::PortUpdateFlashAndDeath(CStateManager& mgr, float dt) {
  mCountdownTimerA -= dt;
  mCountdownTimerB -= dt;
  mChassisTimer -= dt;
  mCountupTimerA += dt;
  mCountupTimerB += dt;
  if (mFlashAlpha > 0.f) {
    mFlashAlpha -= dt;
    if (mFlashAlpha < 0.f) {
      mFlashAlpha = 0.f;
    }
    const float weight = mFlashAlpha < 1.f ? mFlashAlpha : 1.f;
    mFlashColor = CColor::Lerp(CColor::White(), mColor, weight);
  }
  if ((mAutoDestructFlags & 0x4) != 0 && GetAlive()) {
    mDeathTimer -= dt;
    if (mDeathTimer <= 0.f) {
      Death(mgr, GetTransform().GetColumn(kDY), kSS_DeathRattle);
    }
  }
}

// Arms: all ran but the 0x8-clear arm of the data word 0x7C0, dead in shipped data: every SPLU
// placement has 0xB there (tools/scan_splitter_props.py).
// @ fn_75_31A8
// The command module's chassis check (0x31A8-0x32F4): the id at 0xEFA (0x31D8 lhz) is looked up
// (0x31E8 GetObjectById) and cast to the main chassis (0x31EC); a null cast or a chassis whose
// command module id (0xC6C) is not this actor (0x31F8-0x3204, the retail's compare is against
// this+0x8 - the entity's own id) goes to 0x3238, which clears the id at 0xEFA and the next draw
// node at CActor 0x12A (0x3240-0x324C) and ends. Ours: the copy at 0xEFE takes the chassis's
// 0xCC0 when the two differ (0x3208-0x3218), fn_75_1578 runs (0x3224) and the timer 0xF14 is set to 3.0f
// (0x322C-0x3230). With no chassis id, the data's flag bit 0x8 (0x3258 rlwinm 28-28), the timer
// <= 0.0f (0x326C-0x3274) and the id 0xEFC being invalid (0x327C-0x3280) gate fn_75_2280
// (0x3284), the timer is set to 3.0f again (0x328C-0x3290) and the function ends (0x3294).
// Otherwise the id 0xEFC (0x32A4) is looked up and cast (0x32B4-0x32B8): a dead chassis (the
// byte at 0x420, the word's bit 25 = the byte's 0x40 - the host's GetAlive) clears it
// (0x32D0-0x32D8).
void CSplitterCommandModule::PortUpdateChassis(CStateManager& mgr) {
  if (mChassisId != kInvalidUniqueId) {
    const CSplitterMainChassis* chassis =
        TCastToConstPtr< CSplitterMainChassis >(mgr.GetObjectById(mChassisId));
    if (chassis == nullptr || chassis->GetCommandModuleId() != GetUniqueId()) {
      mChassisId = kInvalidUniqueId;
      SetNextDrawNode(kInvalidUniqueId); // 0x324C: CActor 0x12A
      return;
    }
    if (mSyncedId != chassis->GetSyncId()) {
      mSyncedId = chassis->GetSyncId();
      PortUpdateEffectStates(mgr);
    }
    mChassisTimer = 3.f;
    return;
  }
  if ((mData.mUnknownBD80FD94 & 0x8) != 0 && mChassisTimer <= 0.f &&
      mCheckedChassisId == kInvalidUniqueId) {
    PortFindChassis(mgr);
    mChassisTimer = 3.f;
    // Retail ends here: 0x3294 `b` to the epilogue (0x32DC), skipping the 0xEFC lookup.
    return;
  }
  if (mCheckedChassisId != kInvalidUniqueId) {
    const CSplitterMainChassis* chassis =
        TCastToConstPtr< CSplitterMainChassis >(mgr.GetObjectById(mCheckedChassisId));
    if (chassis == nullptr || !chassis->GetAlive()) {
      mCheckedChassisId = kInvalidUniqueId;
    }
  }
}

// @ fn_75_21FC
// The auto-destruct timer (0x21FC-0x2280): the flag byte at 0xF6A (0x2210 lbz, 0x2214 clrlwi with
// the word's bit 31 - the byte's 0x01) gates it. With the flag set the timer at 0xEF4 (0x221C lfs)
// is advanced by dt (0x2224 fadds, 0x222C stfs) and when it reaches 5.0f (0x2228 lbl_75_rodata_58,
// 0x2234 fcmpo and 0x2238 cror eq,gt,eq - the >= sense) the module's auto-destruct runs with 0.0f
// (0x2240/0x2244 lbl_75_rodata_40, 0x2248 the call); the flag is cleared either way (0x224C-0x2258).
// With the flag clear the timer is reset to 0.0f (0x2260-0x2268).
void CSplitterCommandModule::PortAutoDestructTimer(float dt) {
  if (mAutoDestructFlags & 0x1) {
    mAutoDestructTimer += dt;
    if (mAutoDestructTimer >= 5.f) {
      PortAutoDestruct(0.f);
    }
    mAutoDestructFlags &= ~static_cast< uchar >(0x1);
  } else {
    mAutoDestructTimer = 0.f;
  }
}

// Arms: there is no target at 0xF02 (the body ends at once), the target is there but it is not a
// CBeamProjectile (the cast fails) and the target is the laser sweep beam (the virtual is
// called and the sound is moved).  The fight only ever puts the laser sweep beam at 0xF02, so
// the first two arms are forced (tools/cov/specs/splitter_helpers_force.py: SPLITTER_FORCE=
// LOOKATN writes the id invalid, LOOKAT writes the chassis's id).  The coverage build's merged
// profile of those runs and the natural run says 13/13 lines and 4/4 branches, ALL-ARMS.
// @ fn_75_184C
// The command module's look-at (0x184C-0x1974): the target id (the u16 at 0xF02, 0x1874 lhz) is
// looked up (0x1888 ObjectById) and, when it is there, the module's locator transform (the
// segment id at 0xEF8, 0x18A0 GetLctrTransform) gives the position; the offset vector (0xF50,
// 0xF54 and 0xF58, added at 0x18E8-0x18F0) is added to it, the transform looks at that point
// (0x1900 LookAt with the up vector) and the result goes to the target's virtual at the vtable
// slot 0x8C (0x1924-0x192C). The sound (the handle at 0xF5C read at 0x1930, the target's position
// at 0x444 and the byte 0x7f, 0x194C UpdateEmitter) is the DOL's CSfxManager call, which the host
// has as CSfxManager::UpdateEmitter (the handle, the position, the direction and the volume).
// The vtable slot 0x8C: the DOL's CEntity virtual the look-at calls is the CBeamProjectile's
// first own virtual, UpdateFx - the next slot, 0x90, is CBeamProjectile::ResetBeam (asm 0x17D4,
// fn_75_1790) and the header's order is UpdateFx, ResetBeam, Fire
// (include/MetroidPrime/Weapons/CBeamProjectile.hpp:24-26).  The port's own chain (CEntity's 7
// slots, CActor's 22, CGameProjectile's 4) puts UpdateFx at 0x84, two slots low because the
// port's CEntity has two virtuals fewer than the DOL's; the port calls it by name.
void CSplitterCommandModule::PortLookAtTarget(CStateManager& mgr, float dt) {
  CEntity* target = mgr.ObjectById(mLookAtTargetId);
  if (target == nullptr) {
    return;
  }
  const CTransform4f locator = GetLctrTransform(mLookAtSegment);
  const CVector3f position(locator.GetTranslation().GetX() + mLookAtOffset.GetX(),
                            locator.GetTranslation().GetY() + mLookAtOffset.GetY(),
                            locator.GetTranslation().GetZ() + mLookAtOffset.GetZ());
  const CTransform4f lookAt =
      CTransform4f::LookAt(locator.GetTranslation(), position, CVector3f::Up());
  // asm 0x1910-0x192C: the target's virtual at the vtable slot 0x8C gets the transform (r4), the
  // manager (r5) and the dt (f1).
  if (CBeamProjectile* beam = TCastToPtr< CBeamProjectile >(target)) {
    beam->UpdateFx(lookAt, dt, mgr);
    // asm 0x1930-0x194C: the sound handle at 0xF5C is updated with the beam's +0x444 (asm 0x193C
    // `addi r4, r31, 0x444`) - CBeamProjectile::mCollisionPoint (0x418 + 0x2C), not the actor's
    // translation (0x54) - the up vector and the volume 0x7F.  The id at 0xF02 only ever holds the
    // laser sweep beam (fn_75_1974).
    CSfxManager::UpdateEmitter(mLookAtSfxHandle, beam->GetCurrentPos(), CVector3f::Up(), 0x7F);
  }
}

// @ fn_75_58E8
// The gate the head's beam-effect spawner asks first (0x58E8-0x5944): the chassis id at 0xEFA
// (0x58F4 lhz) is looked up (0x5904 GetObjectById) and cast to the main chassis (0x5908
// TCastToPtr); a null cast gives false (0x5930 li r3, 0) and the chassis's scan timer at 0xD28
// decides the rest (0x5918 lfs, 0x5920 fcmpo against zero and 0x5928 extrwi, which takes cr0's
// greater-than bit).
bool CSplitterCommandModule::PortChassisTimerRunning(CStateManager& mgr) const {
  const CSplitterMainChassis* chassis =
      TCastToConstPtr< CSplitterMainChassis >(mgr.GetObjectById(mChassisId));
  if (chassis == nullptr) {
    return false;
  }
  return chassis->GetScanTimer() > 0.f;
}

// Arms: the beam effect already there (the id at 0xF68) and not, the detection range at or below
// zero (asm 0x1214) and above it.  The fight never has the detection range at or below zero, so
// the range (the base's field at 0x3D0) is written through $this at the body's entry
// (tools/cov/specs/splitter_helpers_force.py: SPLITTER_FORCE=1188).  The coverage build's merged
// profile says 35/35 lines and 21/23 branches: the two branches still untaken are the
// allocation-failure path of the new and an internal path of the inlined AddObject, neither of
// which is a live arm.
// @ fn_75_1188
// The head's beam-effect spawner (0x1188-0x14CC). The beam effect's id (0xF68) is looked up
// (0x11B0 ObjectById), the module's own transform (fn_75_10D0, 0x11C0) is built and copied
// (0x11CC) and set on the beam effect (0x11E0 SetTransform), and the gate (fn_75_58E8, 0x1200)
// decides whether the body runs at all (0x1204-0x1208). With a beam effect already there the
// body goes to 0x13EC, which sets the beam's byte at 0x198 (0x13F8) and updates the sound
// (0x142C UpdateEmitter). Otherwise the base's field at 0x3D0 must be above zero (0x1214-0x1224),
// a fresh id is allocated (0x1230 AllocateUniqueId) and kept at 0xF68 (0x1244), and the
// 0x1A0-byte beam effect actor is created (0x1258), built (0x12A0 the CEntityInfo, 0x12B8 the
// name, 0x12C4-0x1334 the rotation) and added to the manager (0x13B4 AddObject).
void CSplitterCommandModule::PortSpawnBeamEffect(CStateManager& mgr) {
  // 0x11A4-0x11B4: the beam effect's id (0xF68) is looked up; the id only ever holds the beam
  // effect, so the retail's unchecked use as one is kept.
  CSplitterBeamEffect* beam = static_cast< CSplitterBeamEffect* >(mgr.ObjectById(mBeamEffectId));
  // 0x11B8-0x11E0: the module's own transform (fn_75_10D0) is built and set on the beam.
  CTransform4f moduleTransform = GetTransform();
  PortModuleTransform(moduleTransform);
  if (beam != nullptr) {  // port limit: the retail calls SetTransform (0x11E0) with no test
    beam->SetTransform(moduleTransform);
  }
  // 0x1200-0x1208: the gate (fn_75_58E8) decides whether the body runs.
  if (!PortChassisTimerRunning(mgr)) {
    if (beam == nullptr) {  // 0x1434-0x1438
      // 0x1494-0x14B4: the id at 0xF68 is cleared and the sound removed, its handle cleared.
      mBeamEffectId = kInvalidUniqueId;
      CSfxManager::RemoveEmitter(mBeamSfxHandle);
      mBeamSfxHandle = CSfxHandle();
      return;
    }
    // 0x143C-0x1490: the beam closes (0x198 cleared, 0x1440); once it is fully closed (its
    // 0x188 is 0.0f, lbl_75_rodata_40, 0x1450-0x1460) it is deleted and the id cleared.
    beam->SetOpening(false);
    if (!beam->IsOpening() && beam->GetOpenFraction() == 0.f) {
      mgr.DeleteObjectRequest(mBeamEffectId);
      mBeamEffectId = kInvalidUniqueId;
    }
    return;
  }
  // 0x120C-0x1224: a missing beam is made only when the base's field at 0x3D0 (its copy of the
  // patterned info's detection range) is above zero (ble skips to 0x13EC, where the emitter is
  // still updated).
  if (beam == nullptr && GetDetectionRange() > 0.f) {
    // 0x1230-0x1244: a fresh id, kept at 0xF68. 0x1248-0x1380: the beam effect is built with the
    // CEntityInfo the retail makes at 0x12A0 (the area id, NullConnectionList, true and
    // kInvalidEditorId), the name (0x12B8, the rodata string at 0xA0 + 0x230), the module
    // transform, a texture height of 0x40 (0x136C), the data's angle (0x864) in radians wrapped
    // to one turn (0x12C4-0x1334), the detection range as the length (0x1370) and the data's
    // colours (0x868-0x874), cloud scale, fade-off size and open speed (0x878-0x880). It is
    // added to the manager (0x13B4 AddObject) and its looped sound (the data's 0x890) starts at
    // the head (0x13B8-0x13E8: volume 0x7F, the area, acoustics, looped, medium priority).
    mBeamEffectId = mgr.AllocateUniqueId();
    const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, true,
                           kInvalidEditorId);
    const rstl::string name = rstl::string_l("Splitter Beam Effect");
    const SLdrUnknownStruct42& cone = mData.mUnknown9EC51FE4;
    beam = new CSplitterBeamEffect(mBeamEffectId, info, name, moduleTransform, 0x40,
                                   PortWrapTurn(cone.angle * 0.017453292f), GetDetectionRange(),
                                   cone.cloudColor1, cone.cloudColor2, cone.addColor1,
                                   cone.addColor2, cone.cloudScale, cone.fadeOffSize,
                                   cone.openSpeed);
    mgr.AddObject(beam);
    // 0x13BC-0x13CC: the sound word at 0x890, its low half (clrlwi r4, r0, 16).
    mBeamSfxHandle = CSfxManager::AddEmitter(static_cast< ushort >(mData.mSoundScanning & 0xFFFF),
                                             GetTranslation(), 0x7F, GetCurrentAreaId().Value(),
                                             true, true, CSfxManager::kMedPriority);
  }
  // 0x13EC-0x142C: the beam opens (0x198 set, 0x13F8) and the sound follows the head, facing
  // along its Y column (0x13FC-0x142C UpdateEmitter, volume 0x7F).
  if (beam != nullptr) {  // 0x13EC-0x13F0
    beam->SetOpening(true);
  }
  CSfxManager::UpdateEmitter(mBeamSfxHandle, GetTranslation(), GetTransform().GetColumn(kDY),
                             0x7F);
}

// fn_75_1578 (the head's effect-state update, 0x1578-0x178C): the anim data (the model data at
// 0x60 and its 0x10 - 0x1594 lwz, 0x159C lwz) gets two effect states. The counter at 0xEDC
// (0x1590 lwz) picks the branch: 1 or 2 (0x1598 cmpwi r0, 2 - 0x15A0 beq, 0x15A4 cmpwi r0, 1)
// turns both off ("AlertEye" and "LaserMuzzle", 0x15AC-0x160C). Otherwise the laser-pulse
// projectile's id at 0xF02 (0x1614 lhz) decides: a valid id (the head is firing) turns
// "LaserMuzzle" on and "AlertEye" off (0x1628-0x1688). With no projectile the head's synced id
// at 0xEFE (0x168C lhz) decides: a valid id turns "AlertEye" on (0x16B4-0x1714); with no id
// either the IsScanning trigger (fn_75_58E8, 0x16A8) turns "AlertEye" on or both stay off
// (0x1718-0x1774). The two names are the rodata strings at 0x18 ("LaserMuzzle") and 0x1C (the
// "AlertEye" blob at 0x2C0).
// Arms: all ran - the shield state 1 and 2 turn both effects off, the laser-pulse projectile
// turns LaserMuzzle on, the synced id turns AlertEye on and the IsScanning trigger is the last
// way to it; the coverage build's merged profile says 9/9 lines and 10/10 branches, ALL-ARMS.
// @ fn_75_1578
void CSplitterCommandModule::PortUpdateEffectStates(CStateManager& mgr) {
  bool alertEye = false;
  bool laserMuzzle = false;
  if (mShieldState != 1 && mShieldState != 2) {
    if (mLookAtTargetId != kInvalidUniqueId) {
      laserMuzzle = true;
    } else if (mSyncedId != kInvalidUniqueId || PortChassisTimerRunning(mgr)) {
      alertEye = true;
    }
  }
  AnimationData()->SetEffectState(rstl::string_l("AlertEye"), alertEye, mgr);
  AnimationData()->SetEffectState(rstl::string_l("LaserMuzzle"), laserMuzzle, mgr);
}

// fn_75_5DDC (0x5DDC-0x5E4C): the head's own vulnerability copy at 0xEAC becomes the DOL's
// ImmuneRicochetVulnerability (0x5DF8, the host's ReflectVulnerabilty; 0x5E04 fn_75_5B9C), the
// head becomes orbitable (0x5E14 SetVisorOrbitableFlags 0xF, true), the shield state at 0xEDC is
// 2 (0x5E1C-0x5E28), the timer at 0xF18 is zeroed (0.0f lbl_75_rodata_40, 0x5E30) and the effect
// states are updated (0x5E34 fn_75_1578).
// @ fn_75_5DDC
void CSplitterCommandModule::PortSpinShield(CStateManager& mgr) {
  mOwnVulnerability = CDamageVulnerability::ReflectVulnerabilty();
  SetVisorOrbitableFlags(static_cast< CVisorParameters::EVisorOrbitableFlags >(0xF), true);
  mShieldState = 2;
  mCountupTimerA = 0.f;
  PortUpdateEffectStates(mgr);
}

// fn_75_5CC4 (0x5CC4-0x5D50): as fn_75_5DDC, but the shield state at 0xEDC is 1 (0x5D1C-0x5D28)
// and, when it was 2, the timer at 0xF1C is zeroed first (0x5D00-0x5D14).
// @ fn_75_5CC4
void CSplitterCommandModule::PortDodgeShield(CStateManager& mgr) {
  mOwnVulnerability = CDamageVulnerability::ReflectVulnerabilty();
  SetVisorOrbitableFlags(static_cast< CVisorParameters::EVisorOrbitableFlags >(0xF), true);
  if (mShieldState == 2) {
    mCountupTimerB = 0.f;
  }
  mShieldState = 1;
  mCountupTimerA = 0.f;
  PortUpdateEffectStates(mgr);
}

// fn_75_5B28 (0x5B28-0x5B9C): the reflecting vulnerability as in fn_75_5DDC, but the head is made
// not orbitable (0x5B60 SetVisorOrbitableFlags 0xF, false) and the shield state at 0xEDC is 3.
// @ fn_75_5B28
void CSplitterCommandModule::PortSnatchShield(CStateManager& mgr) {
  mOwnVulnerability = CDamageVulnerability::ReflectVulnerabilty();
  SetVisorOrbitableFlags(static_cast< CVisorParameters::EVisorOrbitableFlags >(0xF), false);
  mShieldState = 3;
  mCountupTimerA = 0.f;
  PortUpdateEffectStates(mgr);
}

// fn_75_5D50 (0x5D50-0x5DE0): the head's own vulnerability copy at 0xEAC takes the base's again
// (0x5D6C GetDamageVulnerability, 0x5D78 fn_75_5B9C, the REL copy of the CDamageVulnerability
// assignment). The head becomes orbitable (0x5D88
// SetVisorOrbitableFlags 0xF, true); with the shield state at 0xEDC at 2 (0x5D90 cmpwi) the timer
// at 0xF1C is zeroed (0x5DA0); the shield state and the timer at 0xF18 are zeroed (0x5DB4,
// 0x5DBC) and the effect states are updated (0x5DC0 fn_75_1578).
// @ fn_75_5D50
void CSplitterCommandModule::PortResetShield(CStateManager& mgr) {
  mOwnVulnerability = *CPatterned::GetDamageVulnerability();
  SetVisorOrbitableFlags(static_cast< CVisorParameters::EVisorOrbitableFlags >(0xF), true);
  if (mShieldState == 2) {
    mCountupTimerB = 0.f;
  }
  mShieldState = 0;
  mCountupTimerA = 0.f;
  PortUpdateEffectStates(mgr);
}

// fn_75_42F8 (the head's LostChassisReaction state, 0x42F8-0x4438). Activate (0x4330): the
// animation state is kAS_Ready (0x4334) and the flag byte 0xF6A's 0x20 bit is set (0x433C rlwimi).
// Update (0x4348): the head turns upright (fn_75_2994) and, when CTryCommand::ShouldRetry
// (the host's CAnimationState::CanIssueCommand) allows body state 6 (kAS_KnockBack, 0x4354-0x4360),
// a knock-back command along the head's Y column (the transform's 0x28, 0x38, 0x48) with
// severity 2 goes out (lbl_75_data_5B8, 0x4364-0x43C4). Deactivate (0x441C): the animation state is kAS_NotReady.
// @ fn_75_42F8
void CSplitterCommandModule::LostChassisReaction(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mAutoDestructFlags |= 0x20;
  } else if (msg == kStateMsg_Update) {
    PortTurnUpright(dt);
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_KnockBack)) {
      BodyController()->CommandMgr().DeliverCmd(CBCKnockBackCmd(GetTransform().GetColumn(kDY), pas::kS_Two));
    }
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
  }
}

// fn_75_2994 (the head's turn upright, 0x2994-0x2BE0): only with the flag byte 0xF6A's 0x20 bit
// (0x29BC-0x29C8). The turn is the frame time times 2 pi (lbl_75_rodata_7C). With the Z column
// (0x2C, 0x3C, 0x4C) within it of the up vector (0x2A38 GetAngleDiff, 0x2A3C-0x2A44) and the Y
// column's x or y non-zero (0x2A48-0x2A7C) the head looks along its flat Y column (0x2AC0
// LookAt, 0x2ACC SetTransform) and the 0x20 bit is cleared (0x2AD8). Otherwise the Z column is
// slerped toward the up vector by the turn (0x2B10) and the rotation arc from the Z column to it
// (0x2B38) is applied after the head's rotation (0x2B64 FromMatrix, 0x2B74 operator*,
// 0x2BA4 BuildTransform4f at the translation, 0x2BB0 SetTransform).
// Arms (the coverage build, 2026-10-22: 18/18 lines, 12/12 branches): the flag clear and the
// LookAt with x non-zero ran; forced (SPLITTER_FORCE=R_TURN, force_ret.py, the head's columns):
// the Z column (1,0,0) (the slerp), Z up with Y (0,1,0) (x zero, y not), (0,0,1) (both zero:
// the slerp) and (1,0,0).
// asmcheck: the 0xF6A 0x20 bit (0x29C4 test, 0x2AD8 clear) is mAutoDestructFlags & 0x20, the
// host's name for the flag byte at 0xF6A; FromMatrix (0x2B64) is GetRotation()'s body.
// @ fn_75_2994
void CSplitterCommandModule::PortTurnUpright(float dt) {
  if ((mAutoDestructFlags & 0x20) == 0) {  // 0x29BC-0x29C8
    return;
  }
  const CVector3f pos = GetTranslation();
  const float turn = 6.2831855f * dt;
  const CVector3f up = CVector3f::Up();
  const CVector3f zColumn = GetTransform().GetColumn(kDZ);
  const CVector3f yColumn = GetTransform().GetColumn(kDY);
  if (CVector3f::GetAngleDiff(up, zColumn) <= turn &&
      (yColumn.GetX() != 0.f || yColumn.GetY() != 0.f)) {
    SetTransform(CTransform4f::LookAt(
        pos, CVector3f(pos.GetX() + yColumn.GetX(), pos.GetY() + yColumn.GetY(), pos.GetZ() + 0.f),
        up));  // 0x2A94-0x2AA8: the z is pos.z + 0.0f (lbl_75_rodata_40)
    mAutoDestructFlags &= ~0x20;
    return;
  }
  const CVector3f toward =
      CVector3f::Slerp(zColumn.AsNormalized(), up.AsNormalized(), CRelAngle::FromRadians(turn));
  const CQuaternion arc = CQuaternion::ShortestRotationArc(zColumn, toward);
  SetTransform((GetRotation() * arc).BuildTransform4f(pos));
}

// fn_75_23F0 (the head's separation, 0x23F0-0x25C4): the chassis's fn_75_8EC8 with the head's
// radius - the listening-AI list at 0x830 walked from 0x2008 (0x2448) to -1; every CPatterned
// (0x245C) other than the head (0x2468) in its area (0x2470-0x247C) that is not a docked head
// (0x2480-0x249C) gives the steering separation (0x24DC) from its position with 5.0f
// (lbl_75_rodata_58) times the model data's scale x; safe ones (0x24FC) are normalized and summed
// and counted; a count above 0.0f (0x2564) divides the sum (0x2574).
// Arms: 21/22, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov merged)
// (other area, a docked and an undocked second head forced). The null cast is dead:
// CListeningAiList::IsQualified (DOL 0x8013C05C, src/MetroidPrime/GameObjectLists.cpp:28-34)
// admits only a CPatterned.
// @ fn_75_23F0
CVector3f CSplitterCommandModule::PortSeparationVector(CStateManager& mgr) const {
  CObjectList& list = mgr.ObjectListById(kOL_ListeningAi);
  CVector3f sum = CVector3f::Zero();
  float count = 0.f;
  for (int idx = list.GetFirstObjectIndex(); idx != -1; idx = list.GetNextObjectIndex(idx)) {
    const CPatterned* other = TCastToConstPtr< CPatterned >(list[idx]);
    if (other == nullptr || other == this || other->GetCurrentAreaId() != GetCurrentAreaId()) {  // 0x2460, 0x2468, 0x2470-0x247C
      continue;
    }
    const CSplitterCommandModule* head = TCastToConstPtr< CSplitterCommandModule >(other);
    if (head != nullptr && head->GetChassisId() != kInvalidUniqueId) {  // 0x2484-0x249C
      continue;
    }
    const CVector3f separation = mSteeringBehaviors.Separation(
        *this, other->GetTranslation(), 5.f * GetModelData()->GetScale().GetX());
    if (separation.IsMagnitudeSafe()) {
      sum += separation.AsNormalized();
      count += 1.f;
    }
  }
  if (count > 0.f) {
    sum *= 1.f / count;
  }
  return sum;
}

// fn_75_2280 (the head's chassis search, 0x2280-0x23F0): the listening-AI list at 0x830 is walked
// (0x22D8 lha 0x2008, to -1); a main chassis (0x22EC) in the head's area (0x22F8-0x2304) that
// takes the head's reservation (0x2318 fn_75_E788) and is nearer than the best so far (squared,
// from FLT_MAX lbl_75_rodata_88; 0x2324-0x234C) releases the old pick's reservation (0x2350-0x2380
// ObjectById, cast, fn_75_E768) and becomes the pick at 0xEFC (0x238C).
// Arms (the coverage build, 2026-10-22: 19/19 lines, 14/14 branches): the walk, the cast's null,
// the failed reserve (the 0521 chassis's head 0520 holds 0xC6C) ran; forced
// (SPLITTER_FORCE=R_FIND, force_ret.py): every chassis in another area (the head's area 12), the
// successful reserve (the chassis's 0xC6C and 0xC6E invalid), the not-nearer skip (the head's x
// at 1e30), the pick with no old pick, and the old pick 0521 released (0xEFC = 0x521).
// @ fn_75_2280
void CSplitterCommandModule::PortFindChassis(CStateManager& mgr) {
  CObjectList& list = mgr.ObjectListById(kOL_ListeningAi);
  const CVector3f pos = GetTranslation();
  float best = 3.4028235e38f;
  for (int idx = list.GetFirstObjectIndex(); idx != -1; idx = list.GetNextObjectIndex(idx)) {
    CSplitterMainChassis* chassis = TCastToPtr< CSplitterMainChassis >(list[idx]);
    if (chassis == nullptr || chassis->GetCurrentAreaId() != GetCurrentAreaId()) {  // 0x22EC cast, 0x22F8-0x2304
      continue;
    }
    if (!chassis->PortReserveDock(GetUniqueId())) {
      continue;
    }
    const float distSq = (chassis->GetTranslation() - pos).MagSquared();
    // 0x2348 fcmpo f31, f27 + 0x234C bge: distSq >= best or a NaN skips (bge is taken on a
    // NaN), so only an ordered smaller distance becomes the pick.
    if (!(distSq < best)) {
      continue;
    }
    if (CSplitterMainChassis* old =
            TCastToPtr< CSplitterMainChassis >(mgr.ObjectById(mCheckedChassisId))) {
      old->PortReleaseDock(GetUniqueId());
    }
    best = distSq;
    mCheckedChassisId = chassis->GetUniqueId();
  }
}

// fn_75_3ADC (the head's ResetAttackTimes code, 0x3ADC-0x3B20): the countdown at 0xF0C takes the
// data's minimum laser pulse attack time (0x7D0) and the shot count at 0xF20 a random 1 to the
// data's maximum laser pulse shots (0x7DC; 0x3B04 CRandom16::Range on the manager's 0x16E4).
// @ fn_75_3ADC
void CSplitterCommandModule::ResetAttackTimes(CStateManager& mgr, float) {
  mCountdownTimerB = mData.mMinLaserPulseAttackTime;
  mLaserPulseShots = mgr.Random()->Range(1, mData.mMaxLaserPulseShots);
}

// fn_75_3D20 (the head's SetTargetDest code, 0x3D20-0x3E60): the destination starts at the head's
// position and the flag byte 0xF6A's 0x80 bit is cleared (0x3D7C). With the target at 0xEFE
// (0x3D8C GetObjectById) the destination is its aim position (0x3DE4, vtable 0x54 with 0.0f)
// plus 4.0f (lbl_75_rodata_5C) up and the 0x80 bit is set (0x3E14). The path finder at 0x6F4
// takes the destination (0x3E24 SetDestination) and the target as its face target (0x3E2C, 0x718).
// Arms: the target arm ran in the 04_cliff_contraption (914f1381) runs; no target ran in the
// scripted 914f1381 run (u0521 deactivated and reactivated).
// @ fn_75_3D20
void CSplitterCommandModule::SetTargetDest(CStateManager& mgr, float) {
  CVector3f dest = GetTranslation();
  mAutoDestructFlags &= ~0x80;
  if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mSyncedId))) {
    // The id is the chassis's target (0xCC0), always an actor; retail does no cast.
    dest = target->GetAimPosition(mgr, 0.f) + CVector3f::Up() * 4.f;
    mAutoDestructFlags |= 0x80;
  }
  mPathFindNavigation.SetDestination(dest);
  mPathFindNavigation.SetFaceTarget(mSyncedId);
}

// fn_75_4E90 (the head's PathFind state, 0x4E90-0x50B0): with 0xF6A's 0x80 bit the path finder at
// 0x6F4 runs first (0x4EEC CPathFindNavigation::PathFind). Activate (0x4F14): the 0x40 bit is
// cleared. Update (0x4F28): the body's move vector (the body controller's command manager, its
// 0x4) plus the separation (0x4F50 fn_75_23F0) must be IsMagnitudeSafe (0x4F88); it is scaled by
// the frame time times the data's maximum linear velocity (0x7C4) and the head moves there
// (0x4FE4 fn_75_2154). The facing is that step, or with the path finder's face target (0x718,
// 0x5008 GetObjectById) the flat offset to it (z 0.0f; 0x5014-0x504C); the body faces it
// (0x505C FaceDirection). Deactivate (0x5064): the 0x80 bit is set.
// Arms: 12/12, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov merged):
// both 0xF6A 0x80 senses, a zero move vector and every message forced.
// @ fn_75_4E90
void CSplitterCommandModule::PathFind(CStateManager& mgr, EStateMsg msg, float dt) {
  if ((mAutoDestructFlags & 0x80) != 0) {  // 0x4EC8-0x4ED8
    mPathFindNavigation.PathFind(mgr, msg, dt, *this);
  }
  if (msg == kStateMsg_Activate) {
    mAutoDestructFlags &= ~0x40;
  } else if (msg == kStateMsg_Update) {
    const CVector3f move =
        BodyController()->CommandMgr().GetMoveVector() + PortSeparationVector(mgr);
    if (!move.IsMagnitudeSafe()) {
      return;
    }
    const CVector3f step = move * (dt * mData.mMaxLinearVelocity);
    const CVector3f pos = GetTranslation();
    PortMoveTo(pos + step, dt);
    CVector3f face = step;
    if (const CActor* target = static_cast< const CActor* >(
            mgr.GetObjectById(mPathFindNavigation.GetFaceTarget()))) {
      const CVector3f delta = target->GetTranslation() - GetTranslation();
      face = CVector3f(delta.GetX(), delta.GetY(), 0.f);
    }
    BodyController()->FaceDirection(face, dt);
  } else if (msg == kStateMsg_Deactivate) {
    mAutoDestructFlags |= 0x80;
  }
}

// fn_75_5624 (the head's ShouldAttack trigger, 0x5624-0x5644): the countdown at 0xF0C is at or
// below 0.0f (0x5630 fcmpo, 0x5634 cror lt|eq).
// @ fn_75_5624
bool CSplitterCommandModule::ShouldAttack(CStateManager&, const CTriggerData&) const {
  return mCountdownTimerB <= 0.f;
}

// fn_75_4864 (the head's LaserPulse state, 0x4864-0x4A5C). Activate (0x48B0): the animation
// state is kAS_Ready (0x48B8), the dodge count at 0xEEC is 0 (0x48BC) and the shot count at 0xF20
// drops by one (0x48C0-0x48C8). Update (0x48D0): when CTryCommand::ShouldRetry (the host's
// CAnimationState::CanIssueCommand) allows body state 0x12 (kAS_ProjectileAttack, 0x48D8-0x48E4)
// a projectile attack command (lbl_75_data_5AC, kind 6) with severity 1 (0x4914) at the position
// plus the Y column (0x48E8-0x4930) without horizon blending (0x4918) goes out (0x4958).
// Otherwise, with a positive frame time (0x49B0 fcmpo against 0.0f lbl_75_rodata_40) and the
// target at 0xEFE (0x49C8 GetObjectById, 0x49CC), the head faces the flat offset (z 0.0f, 0x4A2C)
// to the target's aim position (0x49F8, vtable 0x54 with 0.0f; 0x4A34 FaceDirection).
// Deactivate (0x4A3C): the animation state is kAS_NotReady.
// The dt <= 0 arm (0x49B0) is unreachable: CStateManager::Update thinks only when dt > FLT_EPSILON
// and a live head's state machine gets that dt (CPatterned::Think). The no-target arm (0x49CC) ran
// forced (fsm): LaserPulse on a docked head before its chassis has a target.
// @ fn_75_4864
void CSplitterCommandModule::LaserPulse(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mDodgeCount = 0;
    --mLaserPulseShots;
  } else if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_ProjectileAttack)) {
      BodyController()->CommandMgr().DeliverCmd(CBCProjectileAttackCmd(
          pas::kS_One, GetTranslation() + GetTransform().GetColumn(kDY), false));
    } else if (dt > 0.f) {
      if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mSyncedId))) {
        // The id is the chassis's target (0xCC0), always an actor; retail does no cast.
        const CVector3f aim = target->GetAimPosition(mgr, 0.f);
        BodyController()->FaceDirection(
            CVector3f(aim.GetX() - GetTranslation().GetX(), aim.GetY() - GetTranslation().GetY(), 0.f),
            dt);
      }
    }
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
  }
}

// fn_75_5448 (the head's Dead state, 0x5448-0x5498): Activate only (0x5450-0x5468): the laser
// pulse projectile despawns (0x5470 fn_75_1790) and the head is deleted (0x547C
// CPatterned::DeathDelete).
// The messages other than Activate ran forced (damage): a head killed by a shot still gets an
// Update and a Deactivate before the delete request is processed.
// @ fn_75_5448
void CSplitterCommandModule::Dead(CStateManager& mgr, EStateMsg msg, float) {
  if (msg == kStateMsg_Activate) {
    PortDespawnProjectile(mgr);
    DeathDelete(mgr);
  }
}

// fn_75_4AC0 (the head's Dodge state, 0x4AC0-0x4CA8). Activate (0x4B0C): the animation state is
// kAS_Ready (0x4B10) and the flag byte 0xF6A gets its 0x10 bit (0x4B14-0x4B1C rlwimi). Update
// (0x4B24): when CTryCommand::ShouldRetry (CAnimationState::CanIssueCommand) allows body state 3
// (kAS_Step, 0x4B2C-0x4B38) a step command (lbl_75_data_5A0, kind 1) in the dodge direction at
// 0xF30 (0x4B4C) of type 1 (kStep_Dodge, 0x4B54) goes out (0x4B9C). Otherwise, with a positive
// frame time (0x4BFC fcmpo against 0.0f) and the target at 0xEFE (0x4C14 GetObjectById, 0x4C18),
// the head faces the flat offset (z 0.0f, 0x4C64) to the target's position (0x4C20-0x4C54;
// 0x4C6C FaceDirection). Deactivate (0x4C74): kAS_NotReady and the 0x10 bit cleared.
// The dt <= 0 arm (0x4BFC) is unreachable, as in LaserPulse. The no-target arm (0x4C18) ran
// forced (send Deactivate + fsm): the head of a deactivated chassis, before the chassis has a
// target, sent into Dodge.
// @ fn_75_4AC0
void CSplitterCommandModule::Dodge(CStateManager& mgr, EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mAutoDestructFlags |= 0x10;
  } else if (msg == kStateMsg_Update) {
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Step)) {
      BodyController()->CommandMgr().DeliverCmd(CBCStepCmd(
          static_cast< pas::EStepDirection >(mDodgeDirection), pas::kStep_Dodge));
    } else if (dt > 0.f) {
      if (const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mSyncedId))) {
        // The id is the chassis's target (0xCC0), always an actor; retail does no cast.
        const CVector3f delta = target->GetTranslation() - GetTranslation();
        BodyController()->FaceDirection(CVector3f(delta.GetX(), delta.GetY(), 0.f), dt);
      }
    }
  } else if (msg == kStateMsg_Deactivate) {
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    mAutoDestructFlags &= ~0x10;
  }
}

// fn_75_5610 (the head's ShouldFireAgain trigger, 0x5610-0x5624): the shot count at 0xF20 is
// above 0 (0x5614-0x561C neg/andc/srwi).
// @ fn_75_5610
bool CSplitterCommandModule::ShouldFireAgain(CStateManager&, const CTriggerData&) const {
  return mLaserPulseShots > 0;
}

// fn_75_5644 (the head's ShouldDodge trigger, 0x5644-0x565C): the dodge direction at 0xF30 is not
// -1 (0x5648-0x5654 subfic/addi/or/srwi).
// @ fn_75_5644
bool CSplitterCommandModule::ShouldDodge(CStateManager&, const CTriggerData&) const {
  return mDodgeDirection != -1;
}

// fn_75_3B34 (the head's FindBestDodgeDirection code, 0x3B34-0x3BEC): the dodge direction at 0xF30
// is -1 (0x3B54). With the data word 0x7C0's 0x2 bit (0x3B5C) and the dodge count at 0xEEC below
// the data's maximum dodges (0x7F8, 0x3B6C): below the minimum dodges (0x7F4, 0x3B78) or a roll of
// 0.0f to 100.0f (lbl_75_rodata_40/64, 0x3B98) at or below the dodge chance (0x7FC, 0x3BA0-0x3BA8)
// picks a direction (0x3BB4 fn_75_270C) and counts the dodge (0x3BC0); otherwise the count goes
// to the maximum (0x3BCC-0x3BD0).
// Unreachable with the shipped data (tools/scan_splitter_props.py: all 13 SPLU placements have
// dodgeChance 100 and flags 11): the 0x2 bit clear (0x3B5C), and the count-to-max arm, because the
// roll (CRandom16::Range, 100 * Float(), Float() at most 1.0) is at most 100 and the test is
// at-or-below (0x3BA0-0x3BA4 fcmpo/cror).
// @ fn_75_3B34
void CSplitterCommandModule::FindBestDodgeDirection(CStateManager& mgr, float) {
  mDodgeDirection = -1;
  if ((mData.mUnknownBD80FD94 & 0x2) == 0 || mDodgeCount >= mData.mMaxDodges) {  // 0x3B5C, 0x3B6C
    return;
  }
  if (mDodgeCount < mData.mMinDodges || mgr.Random()->Range(0.f, 100.f) <= mData.mDodgeChance) {
    mDodgeDirection = PortDodgeDirection(mgr);
    ++mDodgeCount;
  } else {
    mDodgeCount = mData.mMaxDodges;
  }
}

// fn_75_270C (the dodge direction pick, 0x270C-0x2994): the right (the X column, 0x24/0x34/0x44)
// and left sides start open. The listening-AI list at 0x830 is walked (0x2780 lha 0x2008, to -1);
// every entry (0x2794) other than the head (0x279C) in its area (0x27A4-0x27B0) nearer than the
// dodge distance at 0xEE8 (squared, 0x27EC) closes the side it lies on: a dot with the X column
// at or above 0.0f (0x2814-0x281C) closes the right within 60 degrees of it (lbl_75_rodata_8C,
// 0x2830-0x2840), below it the left within 60 degrees of -X (0x2874-0x2884). An open side needs a
// clear dodge path (0x28C8 / 0x290C fn_75_25C4). With both open a random bit 0x4000 (0x2928
// CRandom16::Next, 0x292C) keeps the right, else the left. The result is 2 (left, 0x2948), 3
// (right, 0x2958) or -1 (0x2768).
// The near-AI arms and -1 ran forced (place): the other chassis parked beside the head. The left
// pick ran forced (fsm): the head sent back into LaserPulse until the random bit picked it.
// Arms: 33/34, splitter_split25 (tools/cov/specs/splitter_split25.py, 914f1381, gcov merged): the
// chassis in another area, beside the head on each side (one and two AIs: a side already closed)
// and past 60 degrees, a short distance eight times (both picks), by gdb calls. The null entry
// (0x2794-0x2798) is dead: the walk follows the list's links (0x2780 lha 0x2008, the next index),
// and a linked entry always holds its object (CObjectList unlinks an entry before clearing it).
// @ fn_75_270C
int CSplitterCommandModule::PortDodgeDirection(CStateManager& mgr) {
  const float dist = mDodgeDistance;
  const CVector3f pos = GetTranslation();
  const CVector3f right = GetTransform().GetColumn(kDX);
  bool leftOpen = true;
  bool rightOpen = true;
  CObjectList& list = mgr.ObjectListById(kOL_ListeningAi);
  for (int idx = list.GetFirstObjectIndex(); idx != -1; idx = list.GetNextObjectIndex(idx)) {
    // The list holds AIs, always actors; retail reads the position with no cast (0x27B4).
    const CActor* other = static_cast< const CActor* >(list[idx]);
    // 0x2794 (null), 0x279C (the head itself), 0x27A4-0x27B0 (the area).
    if (other == nullptr || other == this || other->GetCurrentAreaId() != GetCurrentAreaId()) {
      continue;
    }
    const CVector3f delta = other->GetTranslation() - pos;
    // 0x27EC fcmpo, 0x27F0 bge: skips when !(dist^2 < range^2) (a NaN distance is skipped).
    if (!(delta.MagSquared() < dist * dist)) {
      continue;
    }
    // 0x2814 fcmpo, 0x2818 cror eq,gt,eq, 0x281C bne: at or above 0.0f is the right (a NaN dot
    // is the left).
    if (CVector3f::Dot(delta, right) >= 0.f) {
      // 0x283C fcmpo, 0x2840 bge: closes only under 60 degrees (a NaN angle leaves it open).
      if (rightOpen && CVector3f::GetAngleDiff(right, delta) < 1.0471976f) {
        rightOpen = false;
      }
    } else if (leftOpen && CVector3f::GetAngleDiff(-right, delta) < 1.0471976f) {  // 0x2880-0x2884
      leftOpen = false;
    }
  }
  if (rightOpen) {
    rightOpen = PortDodgeClear(mgr, right, mDodgeDistance);
  }
  if (leftOpen) {
    leftOpen = PortDodgeClear(mgr, -right, mDodgeDistance);
  }
  if (leftOpen && rightOpen) {
    if ((mgr.Random()->Next() & 0x4000) != 0) {  // 0x2928-0x292C
      leftOpen = false;
    } else {
      rightOpen = false;
    }
  }
  if (leftOpen) {
    return pas::kSD_Left;
  }
  if (rightOpen) {
    return pas::kSD_Right;
  }
  return pas::kSD_Invalid;
}

// fn_75_25C4 (a clear dodge path, 0x25C4-0x270C): from the bounding box's center (0x2600-0x260C)
// to the center plus the direction times the distance (0x2610-0x2664), a ray with the solid
// material (lbl_75_data_440, 0x3B) included and the collision actor one (lbl_75_data_444, 0x2F)
// excluded (type 3) hits no world (0x26B8 RayCollideWorld, 0x26C0) and the end lies on the path
// search at 0xD6C (0x26CC OnPath, 0x26D0 against kR_Success).
// The ray hit and the OnPath failure ran forced (place): the head held above its nav area.
// @ fn_75_25C4
bool CSplitterCommandModule::PortDodgeClear(CStateManager& mgr, const CVector3f& dir, float dist) {
  const CVector3f center = GetBoundingBox().GetCenterPoint();
  const CVector3f end = center + dir * dist;
  const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid), CMaterialList(kMT_CollisionActor));
  return mgr.RayCollideWorld(center, end, filter, this) &&
         mPathFindSearch.OnPath(end) == CPathFindSearch::kR_Success;
}

// fn_75_565C (the head's InLaserPulseRange trigger, 0x565C-0x570C): with the target at 0xEFE
// (0x5680) its squared distance (0x568C-0x56CC) lies between the data's minimum (0x7D4) and
// maximum (0x7D8) laser pulse ranges squared (0x56D0-0x56E4).
// Arms: ALL-ARMS; the in-range, below-min, above-max and no-target arms forced (SPLITTER22 force_ret, splitter_split22.py), with
// the natural 914f1381 and a92f00b3 head runs; gdb saw both results.
// @ fn_75_565C
bool CSplitterCommandModule::InLaserPulseRange(CStateManager& mgr, const CTriggerData&) const {
  const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mSyncedId));
  if (target == nullptr) {  // 0x5684-0x5688
    return false;
  }
  const float distSq = (target->GetTranslation() - GetTranslation()).MagSquared();
  return distSq >= mData.mMinLaserPulseRange * mData.mMinLaserPulseRange &&
         distSq <= mData.mMaxLaserPulseRange * mData.mMaxLaserPulseRange;
}

// fn_75_570C (the head's InHoverRange trigger, 0x570C-0x57B4): with the target at 0xEFE (0x5730)
// its squared distance is within half (lbl_75_rodata_44) the sum of the two laser pulse ranges,
// squared (0x573C-0x5794).
// Arms: ALL-ARMS; the no-target, in-range and out-of-range arms forced (SPLITTER22 force_ret, splitter_split22.py), with the natural
// head runs; gdb saw both results.
// @ fn_75_570C
bool CSplitterCommandModule::InHoverRange(CStateManager& mgr, const CTriggerData&) const {
  const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(mSyncedId));
  if (target == nullptr) {  // 0x5734
    return false;
  }
  const float distSq = (target->GetTranslation() - GetTranslation()).MagSquared();
  const float range = 0.5f * (mData.mMinLaserPulseRange + mData.mMaxLaserPulseRange);
  return distSq <= range * range;
}

// vtable slot 0x14 = fn_75_6490 (0xBC), the command module's Think. The active gate (the byte at
// 0x20, 0x64B4 lbz and 0x64B8 extrwi bit 24 - the byte's 0x80), CPatterned::Think (0x64C0), the collision actor manager's update (the manager at
// 0xE58, 0x64C8 lwz; the update options 0 at 0x64D0, which is kUO_ObjectSpace; 0x64D4 the update)
// and then the module's own six per-frame calls in order: fn_75_31A8 (0x64E0, 0x14C), fn_75_32F4
// (0x64F0, 0x16C), fn_75_3004 (0x64FC, 0x1A4), fn_75_184C (0x650C, 0x128), fn_75_1188 (0x651C, 0x344)
// and fn_75_21FC (0x652C, 0x84): PortUpdateChassis, PortUpdateFlashAndDeath, PortUpdateCollision,
// PortLookAtTarget, PortSpawnBeamEffect and PortAutoDestructTimer, every one ported and marked.
// EnsureBodyActive is the port's Activate-before-Think guard (GOTCHAS), not retail code.
// Arms: 3/4, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov merged): the
// inactive arm by a gdb call; the untaken one is the host-safety null test on the collision
// actor manager, which the retail lacks.
// @ fn_75_6490
void CSplitterCommandModule::Think(float dt, CStateManager& mgr) {
  if (!GetActive()) {
    return;
  }
  EnsureBodyActive(mgr);
  CPatterned::Think(dt, mgr);
  // The retail has no null test (0x64C8 lwz → 0x64D4 bl): the manager is built at the XCRT
  // message (fn_75_3460, 0x662C) before the first Think. The guard is host safety only.
  if (mCollisionActorManager != nullptr) {
    mCollisionActorManager->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);
  }
  // The retail's state machine calls fn_75_3EEC (the docking) as its NotifyDocking code, so
  // the port runs it from there and not from the Think.
  PortUpdateChassis(mgr);
  PortUpdateFlashAndDeath(mgr, dt);
  PortUpdateCollision(mgr);
  PortLookAtTarget(mgr, dt);
  PortSpawnBeamEffect(mgr);
  PortAutoDestructTimer(dt);
}

// Arms: all ran - the base's alive flag's two sides are forced (the base is dead,
// tools/cov/specs/splitter_helpers_force.py: SPLITTER_FORCE=DESPAWN); a projectile that is not
// a CBeamProjectile is dead: the head only fires its laser sweep beam through this path.
// @ fn_75_1790
// The head's laser-pulse projectile despawn (0x1790-0x184C): the projectile's id at 0xF02 is
// looked up (0x17BC ObjectById) and, when it is there, its virtual at the vtable word 0x90 is
// called with the manager and false (0x17D4, 0x17C8-0x17DC), the sound handle at 0xF5C is removed
// (0x17EC RemoveEmitter) and the object is deleted (0x1800 DeleteObjectRequest). Then the id
// at 0xF02 is cleared (0x1814), the handle at 0xF5C is zeroed (0x1818) and, when the base's
// flag byte at 0x420 has its 0x40 bit (the word's bit 25, asm 0x1820 `extrwi. r0, r0, 1, 25`)
// set, the module's own call runs (0x1830 fn_75_1578).
void CSplitterCommandModule::PortDespawnProjectile(CStateManager& mgr) {
  CEntity* projectile = mgr.ObjectById(mLookAtTargetId);
  if (projectile != nullptr) {
    // The vtable word 0x90 with false (0x17C8-0x17DC): the DOL's CPlasmaProjectile::ResetBeam, a
    // host virtual. The id only ever holds the laser sweep beam (fn_75_1974).
    if (CBeamProjectile* beam = TCastToPtr< CBeamProjectile >(projectile)) {
      beam->ResetBeam(mgr, false);
    }
    CSfxManager::RemoveEmitter(mLookAtSfxHandle);
    mgr.DeleteObjectRequest(mLookAtTargetId);
  }
  mLookAtTargetId = kInvalidUniqueId;
  mLookAtSfxHandle.Clear();
  // asm 0x1820 `extrwi. r0, r0, 1, 25`: the base's flag byte at 0x420 has its 0x40 bit set -
  // the host's alive flag.
  if (GetAlive()) {
    PortUpdateEffectStates(mgr);
  }
}

// Arms: all ran; a joint that is not a collision actor is dead (the manager makes a collision
// actor for its one joint), and so is a name that is not "Skeleton_Root" (that one joint is the
// "Skeleton_Root" sphere built above); the delete's null test is the compiler's, not a retail arm.
// The coverage build's merged profile (2026-10-22, after the name fix) says 19/20 lines and 21/24
// branches: the three untaken are those (the cast's null, the name's mismatch, the delete's test).
// @ fn_75_3460
// The head's collision actor manager (0x3460-0x3674): the head's one joint sphere - the
// "Skeleton_Root" locator (rodata 0x0) with the radius 3.0f (rodata 0x4) and the mass 1000.0f
// (rodata 0x94) - is built (0x34C0-0x3514 SphereCollision) and the manager is made with it
// (0x35A4), the old one deleted (0x35B4) and the new one kept at 0xE58 (0x35B8). Then every
// collision actor of the manager is walked (0x35C4-0x3644): the description's id (its 0x3C) is
// looked up (0x35E8 ObjectById) and cast to a CCollisionActor (0x35EC); when the description's
// name is "Skeleton_Root" (0x35F8-0x3604 lbl_75_rodata_8, a pointer to lbl_75_rodata_285
// "Skeleton_Root" - the same string as the joint's name at lbl_75_rodata_0; 0x3614 fn_75_3678,
// the string ==) the id goes to 0xF00 (0x3630). So 0xF00 holds the head's one collision actor.
// Finally the module's own call runs (0x3654 fn_75_2CE0, the shield, ported).
void CSplitterCommandModule::PortCreateCollisionActorManager(CStateManager& mgr) {
  rstl::vector< CJointCollisionDescription > joints;
  joints.reserve(1);
  const CAnimData* anim = GetModelData()->GetAnimationData();
  const CSegId seg = anim->GetLocatorSegId(rstl::string_l("Skeleton_Root"));
  joints.push_back_unsafe(CJointCollisionDescription::SphereCollision(
      seg, CVector3f::Zero(), 3.f, rstl::string_l("Skeleton_Root"), 1000.f));
  CCollisionActorManager* manager =
      rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), joints, false);
  delete mCollisionActorManager;
  mCollisionActorManager = manager;
  for (uint i = 0; i < mCollisionActorManager->GetNumCollisionActors(); ++i) {
    const CJointCollisionDescription& desc = mCollisionActorManager->GetCollisionDescFromIndex(i);
    const TUniqueId id = desc.GetCollisionActorId();
    if (TCastToPtr< CCollisionActor >(mgr.ObjectById(id)) == nullptr) {
      continue;
    }
    if (strcmp(desc.GetName().c_str(), "Skeleton_Root") == 0) {  // 0x35F8-0x3628, lbl_75_rodata_8
      mSoundTargetId = id;
    }
  }
  // 0x3648-0x3654: the retail calls fn_75_2CE0 with the flag clear (no shield goes up).
  PortUpdateShield(mgr, false);
}

// @ fn_75_3A8C
// The command module's connected-object lookup (0x3A8C-0x3ADC): the object connected with the
// state "CONN" (0x3AA4) and the message "ATCH" (0x3AA8) is looked up (0x3ABC FindConnectedObject)
// and its id is kept at 0xEFC (0x3AC4 sth).
void CSplitterCommandModule::PortUpdateConnectedObject(CStateManager& mgr) {
  mCheckedChassisId = FindConnectedObject(mgr, kSS_Connect, kSM_Attach);
}

// @ fn_75_2CC0
// The command module's flash start (0x2CC0-0x2CE0): when the message's sender id (the u16 at
// the message's 0x0, 0x2CC0 lhz) is the id the module keeps at 0xF00 (0x2CC4 lhz), the flash
// weight at 0xF08 is set to 1.0f (0x2CD8 stfs, lbl_75_rodata_34).
void CSplitterCommandModule::PortStartFlashIfTarget(const CScriptMsg& msg) {
  if (msg.GetSenderId() == mSoundTargetId) {
    mFlashAlpha = 1.f;
  }
}

// vtable slot 0x18 = fn_75_654C (0x24C): it calls the base class's AcceptScriptMsg and then
// switches on the script message's four-character code.
// Arms: every case the runs sent ran; the null collision actor manager arms are dead: the
// manager is made at XCRT and never cleared.
// @ fn_75_654C
// The command module's script-message handler (0x654C-0x6798): the base's call (0x6578) and a
// switch on the message's four-character code. The retail's cases: XCRT (the body controller's
// activation, the module's own call and the random roll for the state at 0xEE4), XDEL (the
// collision actor manager's destruction, the beam effect's deletion and the module's own call),
// ACTV and DCVT (the collision actor manager's activation), AUDT (its physics), ALRT (the
// base's flag byte), XAND (the connected-object lookup) and XCMG (the flash start).
void CSplitterCommandModule::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  CPatterned::AcceptScriptMsg(mgr, msg);
  switch (msg.GetMessage()) {
  case kSM_Create: { // "XCRT" (0x65F8)
    // 0x6614-0x666C: the body controller's activation with the animation state -1 (0x6620), the
    // module's own call (0x662C fn_75_3460) and the random roll (0x6644 Range over the
    // manager's random, 0.0f to 100.0f): the state at 0xEE4 is 2 when the roll is 50.0f or more
    // and 1 otherwise (0x6668 stw).
    BodyController()->Activate(mgr, pas::kAS_Invalid);
    PortCreateCollisionActorManager(mgr);
    mPreviousCollisionState = mgr.Random()->Range(0.f, 100.f) < 50.f ? 1 : 2;
    break;
  }
  case kSM_Delete: { // "XDEL" (0x65DC)
    // 0x6670-0x66B4: the collision actor manager is destroyed (0x6680 Destroy), the beam effect
    // at 0xF68 is deleted (0x66A8 DeleteObjectRequest) and the module's own call runs (0x66B4
    // fn_75_1790).
    if (mCollisionActorManager != nullptr) {
      mCollisionActorManager->Destroy(mgr);
    }
    if (mBeamEffectId != kInvalidUniqueId) {
      mgr.DeleteObjectRequest(mBeamEffectId);
    }
    PortDespawnProjectile(mgr);
    break;
  }
  case kSM_Activate: { // "ACTV" (0x65B8)
    // 0x66BC-0x66D0: the collision actor manager is activated (0x66D0 SetActive with true).
    if (mCollisionActorManager != nullptr) {
      mCollisionActorManager->SetActive(mgr, true);
    }
    break;
  }
  case kSM_Deactivate: { // "DCVT" (0x6590)
    // 0x66D8-0x66EC: the collision actor manager is deactivated (0x66EC SetActive with false).
    if (mCollisionActorManager != nullptr) {
      mCollisionActorManager->SetActive(mgr, false);
    }
    break;
  }
  case kSM_AIUpdateDisabled: { // "AUDT" (0x657C)
    // 0x66F4-0x6708: the collision actor manager's physics is switched off (0x6708
    // SetPhysicsActive with false).
    if (mCollisionActorManager != nullptr) {
      mCollisionActorManager->SetPhysicsActive(mgr, false);
    }
    break;
  }
  case kSM_Alert: { // "ALRT" (0x65A4)
    // 0x6710-0x671C: the base's flag byte at 0x420 gets its 0x80 bit (the word's bit 24) set.
    SetHitByPlayerProjectile(true);
    break;
  }
  case kSM_AreaLoaded: { // "XAND" (0x65CC)
    // 0x6724-0x6754: the path area of the head's current area (the world at the manager's
    // 0x1604, its area list at 0x24 indexed by the area id at 0x4, the area's post-constructed
    // data at 0x104 and its path area at 0xEC) goes into the path search at 0xD6C; then the
    // module's own call runs (0x6758 fn_75_3A8C, ported).
    mPathFindSearch.SetArea(
        mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()).GetPostConstructed()->mPathArea);
    PortUpdateConnectedObject(mgr);
    break;
  }
  case kSM_Damage: { // "XCMG" (0x6604)
    // 0x6760-0x6774: the module's own call (0x6774 fn_75_2CC0, ported) with the message's
    // sender id.
    PortStartFlashIfTarget(msg);
    break;
  }
  default:
    break;
  }
}

// vtable slot 0x24 = fn_75_633C: it renders only when the object's own flag byte says so (the
// byte at 0x420, bit 1, read at 0x00006348).
void CSplitterCommandModule::PreRender(CStateManager& mgr) {
  PortPreRender(mgr);
}

// vtable slot 0x28 = fn_75_6310: the same flag byte guards the base class's call.
void CSplitterCommandModule::AddToRenderer(const CStateManager& mgr) const {
  PortAddToRenderer(mgr);
}

// vtable slot 0x2C = fn_75_6368: the same flag byte guards the base class's call.
void CSplitterCommandModule::Render(const CStateManager& mgr) const {
  PortRender(mgr);
}

// vtable slot 0x3C = fn_75_D0: it calls the vtable's HealthInfo slot (0x38) and returns its
// result, which is what the base class's own accessor does.
const CHealthInfo* CSplitterCommandModule::GetHealthInfo() const {
  return CPatterned::GetHealthInfo();
}

// vtable slot 0x40 = fn_75_616C: it returns the object's own copy of the base class's damage
// vulnerability (the ctor copies it into 0xeac with fn_75_70A0): `addi r3, r3, 0xeac` (0x616C).
// @ fn_75_616C
const CDamageVulnerability* CSplitterCommandModule::GetDamageVulnerability() const {
  return &mOwnVulnerability;
}

// vtable slot 0x48 = fn_75_18: it takes the actor's bounding box and copies it into the optional
// (fn_75_7C44 copies the six words and sets the valid byte).
rstl::optional_object< CAABox > CSplitterCommandModule::GetTouchBounds() const {
  return rstl::optional_object< CAABox >(GetBoundingBox());
}

// vtable slot 0x54 = fn_75_5F8C (0x5F8C-0x616C): the aim position the head scans from.  The
// base's own aim (asm 0x5FD8) is the start.  With the shield state at 0xEDC equal to 2 (asm
// 0x5FDC, 0x5FE4) the chassis at 0xEFA is asked for its aim (the chassis's vtable slot 0x54,
// asm 0x6028) and the two aims are mixed with the module's timer at 0xF18 as the weight (asm
// 0x6038, 0x6044; the weight is the timer capped at 1.0f, asm 0x6058, 0x6070): the base's aim
// weighs (1 - w) and the chassis's w (asm 0x6074-0x607C).  Otherwise, with the timer at 0xF1C
// below 1.0f (asm 0x6088, 0x6090), the chassis's aim (asm 0x60CC) is mixed the other way round
// (asm 0x6118-0x6120).  The result is the mixed aim (asm 0x6124).
// @ fn_75_5F8C
CVector3f CSplitterCommandModule::GetAimPosition(const CStateManager& mgr, float dt) const {
  CVector3f aim = CPatterned::GetAimPosition(mgr, dt);
  if (mShieldState == 2) {
    // The retail looks the id up with the manager's ObjectById (asm 0x6004); the port's method is
    // const, so the manager's const GetObjectById is used.
    if (const CSplitterMainChassis* chassis =
            TCastToConstPtr< CSplitterMainChassis >(mgr.GetObjectById(mChassisId))) {
      const CVector3f other = chassis->GetAimPosition(mgr, dt);
      const float weight = mCountupTimerA < 1.f ? mCountupTimerA : 1.f;
      aim = aim * (1.f - weight) + other * weight;
    }
  } else if (mCountupTimerB < 1.f) {
    if (const CSplitterMainChassis* chassis =
            TCastToConstPtr< CSplitterMainChassis >(mgr.GetObjectById(mChassisId))) {
      const CVector3f other = chassis->GetAimPosition(mgr, dt);
      const float weight = mCountupTimerB < 1.f ? mCountupTimerB : 1.f;
      aim = other * (1.f - weight) + aim * weight;
    }
  }
  return aim;
}


// The REL's data words at 0x434, 0x438 and 0x43C: the materials the head collides with (the
// wall, the floor and the ceiling).  The port's SLdrSplitterCommandModuleData does not carry
// them, so the REL's initialized values are used.
static const EMaterialTypes kSplitterCollisionMaterials[] = {kMT_Wall, kMT_Floor, kMT_Ceiling};
// The REL's two globals: the flag that says the material list is built (lbl_75_bss_0) and the
// list itself (lbl_75_bss_8).
static bool sSplitterCollisionMaterialsBuilt = false;
static CMaterialList sSplitterCollisionMaterialList;

// vtable slot 0x84 = fn_75_6178 (0x6178-0x6310): the head's collision.  The base's own
// collision runs first (asm 0x619C); then the flag byte at 0xF6A's 0x01 bit must be clear (asm
// 0x61A0-0x61A8) and the collided id must be invalid (asm 0x61B0-0x61BC).  The material list
// is built once (asm 0x61C0-0x6290: the three data words, the flag set at 0x6284) and every
// collision info's material (asm 0x62B8, 0x62C0) is matched against it (asm 0x62D4): a match
// sets the flag byte's 0x01 bit (asm 0x62E0-0x62E4) and ends the walk (asm 0x62E8).
// Arms: 14/14, splitter_split24 r3-r6 (tools/cov/specs/splitter_split24.py, 914f1381, gcov merged):
// gdb calls with a calloc'd list - the 0x01 bit set, a valid id, no infos (the material list
// built, then reused), a non-matching info, a non-matching then a matching one (the bit set, the
// walk ends).
// @ fn_75_6178
void CSplitterCommandModule::CollidedWith(const TUniqueId& id, const CCollisionInfoList& list,
                                           CStateManager& mgr) {
  CPatterned::CollidedWith(id, list, mgr);
  if ((mAutoDestructFlags & 0x1) != 0) {  // 0x61A0-0x61A8
    return;
  }
  if (id != kInvalidUniqueId) {  // 0x61B0-0x61BC
    return;
  }
  if (!sSplitterCollisionMaterialsBuilt) {  // 0x61C0-0x61CC
    CMaterialList materials;
    for (EMaterialTypes material : kSplitterCollisionMaterials) {
      materials.Add(material);
    }
    sSplitterCollisionMaterialList = materials;
    sSplitterCollisionMaterialsBuilt = true;  // 0x6284
  }
  for (uint i = 0; i < list.GetCount(); ++i) {  // 0x629C-0x62F0
    if ((sSplitterCollisionMaterialList.GetValue() &
         list[i].GetMaterialLeft().GetValue()) != 0) {  // 0x62C0-0x62D4
      mAutoDestructFlags |= 0x1;  // 0x62E0-0x62E4
      break;  // 0x62E8
    }
  }
}

// vtable slot 0x9C = fn_75_6174: the REL's whole body is a single `blr`, so the head ignores a
// knock-back.
void CSplitterCommandModule::KnockBack(CStateManager& mgr, const CKnockBackInfo& info) {}

// vtable slot 0xB0 = fn_75_5F18: it listens for the noise types the state machine asks for.
bool CSplitterCommandModule::Listen(CStateManager& mgr, const CVector3f& position,
                                     EListenNoiseType type) {
  return PortListen(mgr, position, type);
}

// vtable slot 0xF0 = fn_75_5E8C: it registers the module's three tables (17 triggers, 14 states
// and 7 codes) on the state machine. That is milestone 2, so only the base setup runs here.
// @ fn_75_5E8C
// The head's state machine (0x5E8C-0x5F18): the base's setup (0x5EA0) and then the module's own
// tables - the 17 triggers (lbl_75_data_CC, 0x5EB4), the 14 states (lbl_75_data_284, 0x5ED4) and
// the 7 codes (lbl_75_data_3B8, 0x5EF4).
void CSplitterCommandModule::SetupStateMachine(CStateManager& mgr) {
  CPatterned::SetupStateMachine(mgr);
  mStateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  mStateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  mStateMachine->SetCodeFunctions(skCodeFuncs, ARRAY_SIZE(skCodeFuncs));
}

// vtable slot 0xF8 = fn_75_5E50: it is only possessed while it is docked to a main chassis (the
// main chassis' unique id at 0xefa).
bool CSplitterCommandModule::CanBeIngPossessed(CStateManager& mgr) const {
  return PortCanBeIngPossessed(mgr);
}

// ---------------------------------------------------------------------------------------------
// The module's SPLU loader (retail fn_75_FC).

// Retail fn_75_FC. The property ids and their order are the generated
// SLdrSplitterCommandModule reader's, which the analysis checked against the REL's switch id by
// id: the loader's dispatch tree reads all four blocks (0x255a4580 editor properties, 0xB3774750
// patterned AI, 0x7E397FED actor parameters, 0x560CF813 the command module data).
// The SLdr defaults the REL writes inline (0x13C-0x1A4) equal the generated reader's. Arms: 17/18
// in every run; the null model (0x2DC) is dead in the data: tools/scan_splitter_loader.py finds
// an ANCS in all 28 placements and no unknown top-level id (the ReadBytes skip, 0x280).
// Retail fn_75_F00: SLdrSplitterCommandModuleData's constructor, which the loader calls at
// 0x00000138. Its `li r0,-0x1` at 0x00000F3C feeds seven stores over the data's 0xc4..0xdc, the
// sound ids sound_LaserSweep..sound_ShieldOn; the guest's default is -1 (no sound) and the
// generated header stores 0. The .inc reader below overwrites each one the instance carries.
//   fn_75_F00  li r0,-0x1 0x00000F3C; stw r0,0xc4..0xdc(r31) 0x00000F44..0x00000F5C
static void xF00_SoundDefaults(SLdrSplitterCommandModule& sldr) {
  SLdrSplitterCommandModuleData& data = sldr.commandModuleProperties;
  data.sound_LaserSweep = -1;    // 0x00000F44 (field 0xc4)
  data.sound_LaserChargeUp = -1; // 0x00000F48 (0xc8)
  data.sound_Docking = -1;       // 0x00000F4C (0xcc)
  data.sound_Scanning = -1;      // 0x00000F50 (0xd0)
  data.sound_LightShield = -1;   // 0x00000F54 (0xd4)
  data.sound_DarkShield = -1;    // 0x00000F58 (0xd8)
  data.sound_ShieldOn = -1;      // 0x00000F5C (0xdc)
}

// @ fn_75_FC
CEntity* PortLoadSplitterCommandModule(CStateManager& mgr, CInputStream& input,
                                       CEntityInfo& info) {
  SLdrSplitterCommandModule sldrThis;
  xF00_SoundDefaults(sldrThis); // fn_75_F00's -1 sound ids (the call at 0x00000138)
#include "MetroidPrime/ScriptLoader/SLdrSplitterCommandModule.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    // The REL reads the model copy's valid byte (the byte at 0x148 of its stack frame, read at
    // 0x000002D4) and returns null when it is clear.
    return nullptr;
  }

  // The parameter block, field by field: the loader's stack struct is copied into the object
  // (fn_75_73A0), so the host keeps the loader's own values, with the damage infos and the
  // vulnerabilities in their converted forms.
  const SLdrSplitterCommandModuleData& sldr = sldrThis.commandModuleProperties;
  CSplitterCommandModuleData data;
  data.mUnknownBD80FD94 = sldr.unknown_0xbd80fd94;
  data.mMaxLinearVelocity = sldr.maxLinearVelocity;
  data.mMaxTurnSpeed = sldr.maxTurnSpeed;
  data.mScanningTurnSpeed = sldr.scanningTurnSpeed;
  data.mMinLaserPulseAttackTime = sldr.minLaserPulseAttackTime;
  data.mMinLaserPulseRange = sldr.minLaserPulseRange;
  data.mMaxLaserPulseRange = sldr.maxLaserPulseRange;
  data.mMaxLaserPulseShots = sldr.maxLaserPulseShots;
  data.mLaserPulseProjectile = sldr.laserPulseProjectile;
  data.mLaserPulseDamage = LdrToDamageInfo(sldr.laserPulseDamage);
  data.mMinDodges = sldr.minDodges;
  data.mMaxDodges = sldr.maxDodges;
  data.mDodgeChance = sldr.dodgeChance;
  data.mResetShieldTime = sldr.resetShieldTime;
  data.mShieldHP = sldr.shieldHP;
  data.mLaserSweepTurnSpeed = sldr.laserSweepTurnSpeed;
  data.mLaserSweepDamage = LdrToDamageInfo(sldr.laserSweepDamage);
  data.mLaserSweepBeamInfo = sldr.laserSweepBeamInfo;
  data.mUnknown9EC51FE4 = sldr.unknown_0x9ec51fe4;
  data.mSoundLaserSweep = sldr.sound_LaserSweep;
  data.mSoundLaserChargeUp = sldr.sound_LaserChargeUp;
  data.mSoundDocking = sldr.sound_Docking;
  data.mSoundScanning = sldr.sound_Scanning;
  data.mSoundLightShield = sldr.sound_LightShield;
  data.mSoundDarkShield = sldr.sound_DarkShield;
  data.mSoundShieldOn = sldr.sound_ShieldOn;
  data.mLightShieldVulnerability = LdrToDamageVulnerability(sldr.lightShieldVulnerability);
  data.mDarkShieldVulnerability = LdrToDamageVulnerability(sldr.darkShieldVulnerability);

  return rs_new CSplitterCommandModule(mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
                                       LdrToEntityInfo(info, sldrThis.editorProperties),
                                       LdrToTransform4f(sldrThis.editorProperties), *modelData,
                                       LdrToActorParameters(sldrThis.actorInformation),
                                       LdrToPatternedInfo(sldrThis.patterned,
                                                          &sldr.ingPossessionData),
                                       data);
}
