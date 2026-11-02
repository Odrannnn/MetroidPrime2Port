#pragma once
#include <dolphin/gx.h>
#include <aurora/math.hpp>

#include "../internal.hpp"
#include "../gfx/types.hpp"
#include "../gfx/texture.hpp"

#include <absl/container/flat_hash_map.h>
#include <type_traits>
#include <cstring>
#include <bitset>
#include <memory>
#include <array>
#include <cfloat>

#define M_PIF 3.14159265358979323846f

namespace GX {
constexpr u8 MaxLights = 8;
using LightMask = std::bitset<MaxLights>;
} // namespace GX

struct GXLightObj_ {
  GXColor color;
  float a0 = 1.f;
  float a1 = 0.f;
  float a2 = 0.f;
  float k0 = 1.f;
  float k1 = 0.f;
  float k2 = 0.f;
  float px = 0.f;
  float py = 0.f;
  float pz = 0.f;
  float nx = 0.f;
  float ny = 0.f;
  float nz = 0.f;
};
static_assert(sizeof(GXLightObj_) <= sizeof(GXLightObj), "GXLightObj too small!");

#if GX_IS_WII
constexpr float GX_LARGE_NUMBER = -1.0e+18f;
#else
constexpr float GX_LARGE_NUMBER = -1048576.0f;
#endif

namespace aurora::gx {
constexpr bool EnableNormalVisualization = false;
constexpr bool EnableDebugPrints = false;
constexpr bool UseReversedZ = true;

constexpr u32 MaxTextures = GX_MAX_TEXMAP;
// Group 2 binding of the PBR environment BRDF table, after the probe, its sampler and the volumes.
constexpr u32 kBrdfLutBinding = MaxTextures * 2 + 7;
// Group 2 bindings of the volumetric fog's froxels and their sampler (ShaderConfig::volFog).
constexpr u32 kVolFogFroxelBinding = MaxTextures * 2 + 8;
constexpr u32 kVolFogSamplerBinding = MaxTextures * 2 + 9;
// A shadow receiver (shadow_receives) never fogs per pixel, so its group 2 layout puts the sun's shadow map and
// comparison sampler in the froxel's slots: group 2 already holds WebGPU's default 16 sampled textures.
constexpr u32 kShadowMapBinding = kVolFogFroxelBinding;
constexpr u32 kShadowSamplerBinding = kVolFogSamplerBinding;
// A baked lightmap's 2D array (GX_AURORA_SET_PBR_LIGHTMAP), read with the probe's sampler. It is the 17th
// sampled texture of group 2, one past WebGPU's default limit, so the layout has it only when the device was
// given more (webgpu::g_lightmapBinding); kTextureBindings counts it.
constexpr u32 kLightmapBinding = MaxTextures * 2 + 10;
constexpr u32 kTextureBindings = MaxTextures * 2 + 11;
constexpr u32 MaxTluts = 20;
constexpr u32 MaxTevStages = GX_MAX_TEVSTAGE;
constexpr u32 MaxColorChannels = 4;
constexpr u32 MaxTevRegs = 4; // TEVPREV, TEVREG0-2
constexpr u32 MaxKColors = GX_MAX_KCOLOR;
constexpr u32 MaxTexMtx = 10;
constexpr u32 MaxPTTexMtx = 20;
constexpr u32 MaxTexCoord = GX_MAX_TEXCOORD;
constexpr u32 MaxVtxAttr = GX_VA_MAX_ATTR;
constexpr u32 MaxTevSwap = GX_MAX_TEVSWAP;
constexpr u32 MaxIndStages = GX_MAX_INDTEXSTAGE;
constexpr u32 MaxIndTexMtxs = 3;
constexpr u32 MaxVtxFmt = GX_MAX_VTXFMT;
constexpr u32 MaxPnMtx = (GX_PNMTX9 / 3) + 1;
constexpr u32 MaxIndexAttr = 12; // VA_POS -> VA_TEX7
constexpr u32 MaxUniformSize = 8192; // WebGPU guarantees maxUniformBufferBindingSize >= 16384
constexpr u32 XfRegCount = 0x58; // 0x1000-0x1057

enum DirtyFlag : u8 {
  DirtyPipeline = 1 << 0,
  DirtyTextures = 1 << 1,
  DirtyUniform = 1 << 2,
  DirtyImmediates = 1 << 3,
  DirtyAll = DirtyPipeline | DirtyTextures | DirtyUniform | DirtyImmediates,
};

struct DrawImmediateData {
  u32 vtxStart = 0;
  u32 currentPnMtx = 0;
  u32 fogRangeBase = 0;
  u32 serial = 0; // the draw's serial, read by shaders built with ShaderConfig::drawId; in the shadow pass, the cascade
  std::array<u32, MaxIndexAttr> arrayStart{};
};
static_assert(std::has_unique_object_representations_v<DrawImmediateData>);
static_assert(sizeof(DrawImmediateData) == 64);

extern wgpu::BindGroup g_emptyTextureBindGroup;

template <typename Arg, Arg Default>
struct TevPass {
  Arg a = Default;
  Arg b = Default;
  Arg c = Default;
  Arg d = Default;

  bool operator==(const TevPass& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const TevPass& rhs) const { return !(*this == rhs); }
};
static_assert(std::has_unique_object_representations_v<TevPass<GXTevColorArg, GX_CC_ZERO>>);
static_assert(std::has_unique_object_representations_v<TevPass<GXTevAlphaArg, GX_CA_ZERO>>);
struct TevOp {
  GXTevOp op = GX_TEV_ADD;
  GXTevBias bias = GX_TB_ZERO;
  GXTevScale scale = GX_CS_SCALE_1;
  GXTevRegID outReg = GX_TEVPREV;
  bool clamp = true;
  u8 _p1 = 0;
  u8 _p2 = 0;
  u8 _p3 = 0;

  bool operator==(const TevOp& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const TevOp& rhs) const { return !(*this == rhs); }
};
static_assert(std::has_unique_object_representations_v<TevOp>);
struct TevStage {
  TevPass<GXTevColorArg, GX_CC_ZERO> colorPass;
  TevPass<GXTevAlphaArg, GX_CA_ZERO> alphaPass;
  TevOp colorOp;
  TevOp alphaOp;
  GXTevKColorSel kcSel = GX_TEV_KCSEL_1;
  GXTevKAlphaSel kaSel = GX_TEV_KASEL_1;
  GXTexCoordID texCoordId = GX_TEXCOORD_NULL;
  GXTexMapID texMapId = GX_TEXMAP_NULL;
  GXChannelID channelId = GX_COLOR_NULL;
  GXTevSwapSel tevSwapRas = GX_TEV_SWAP0;
  GXTevSwapSel tevSwapTex = GX_TEV_SWAP0;
  GXIndTexStageID indTexStage = GX_INDTEXSTAGE0;
  GXIndTexFormat indTexFormat = GX_ITF_8;
  GXIndTexBiasSel indTexBiasSel = GX_ITB_NONE;
  GXIndTexAlphaSel indTexAlphaSel = GX_ITBA_OFF;
  GXIndTexMtxID indTexMtxId = GX_ITM_OFF;
  GXIndTexWrap indTexWrapS = GX_ITW_OFF;
  GXIndTexWrap indTexWrapT = GX_ITW_OFF;
  bool indTexUseOrigLOD = false;
  bool indTexAddPrev = false;
  u8 _p1 = 0;
  u8 _p2 = 0;

  bool operator==(const TevStage& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const TevStage& rhs) const { return !(*this == rhs); }
};
static_assert(std::has_unique_object_representations_v<TevStage>);
struct IndStage {
  GXTexCoordID texCoordId;
  GXTexMapID texMapId;
  GXIndTexScale scaleS;
  GXIndTexScale scaleT;
};
static_assert(std::has_unique_object_representations_v<IndStage>);
// For shader generation
struct ColorChannelConfig {
  GXColorSrc matSrc = GX_SRC_REG;
  GXColorSrc ambSrc = GX_SRC_REG;
  GXDiffuseFn diffFn = GX_DF_NONE;
  GXAttnFn attnFn = GX_AF_NONE;
  bool lightingEnabled = false;
  u8 _p1 = 0;
  u8 _p2 = 0;
  u8 _p3 = 0;

  bool operator==(const ColorChannelConfig& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const ColorChannelConfig& rhs) const { return !(*this == rhs); }
};
static_assert(std::has_unique_object_representations_v<ColorChannelConfig>);
// For uniform generation
struct ColorChannelState {
  Vec4<float> matColor;
  Vec4<float> ambColor;
  GX::LightMask lightMask;
};
struct TcgConfig {
  GXTexGenType type = GX_TG_MTX2x4;
  GXTexGenSrc src = GX_MAX_TEXGENSRC;
  GXTexMtx mtx = GX_IDENTITY;
  GXPTTexMtx postMtx = GX_PTIDENTITY;
  bool normalize = false;
  u8 embossSrc = 0; // Emboss source texcoord (GX_TG_BUMP*)
  u8 _p2 = 0;
  u8 _p3 = 0;

  bool operator==(const TcgConfig& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const TcgConfig& rhs) const { return !(*this == rhs); }
};
static_assert(std::has_unique_object_representations_v<TcgConfig>);
struct FogState {
  GXFogType type = GX_FOG_NONE;
  float a = 0.f;
  float b = 0.5f;
  float c = 0.f;
  Vec4<float> color;
  std::array<u16, 10> rangeK{};
  s16 rangeCenter = 0;
  bool rangeEnabled = false;
  // Raw encoded register values for A/B reconstruction across separate BP writes
  u32 fog0Raw = 0; // 0xEE: encoded A parameter
  u32 fog1Raw = 0; // 0xEF: B mantissa
  u32 fog2Raw = 0; // 0xF0: B shift

  bool operator==(const FogState& rhs) const {
    return type == rhs.type && a == rhs.a && b == rhs.b && c == rhs.c && color == rhs.color && rangeK == rhs.rangeK &&
           rangeCenter == rhs.rangeCenter && rangeEnabled == rhs.rangeEnabled;
  }
  bool operator!=(const FogState& rhs) const { return !(*this == rhs); }
};
struct TevSwap {
  GXTevColorChan red = GX_CH_RED;
  GXTevColorChan green = GX_CH_GREEN;
  GXTevColorChan blue = GX_CH_BLUE;
  GXTevColorChan alpha = GX_CH_ALPHA;

  bool operator==(const TevSwap& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const TevSwap& rhs) const { return !(*this == rhs); }
  explicit operator bool() const { return !(*this == TevSwap{}); }
};
static_assert(std::has_unique_object_representations_v<TevSwap>);
struct AlphaCompare {
  GXCompare comp0 = GX_ALWAYS;
  u32 ref0; // would be u8 but extended to avoid padding bytes
  GXAlphaOp op = GX_AOP_AND;
  GXCompare comp1 = GX_ALWAYS;
  u32 ref1;

  bool operator==(const AlphaCompare& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const AlphaCompare& rhs) const { return !(*this == rhs); }
  explicit operator bool() const { return comp0 != GX_ALWAYS || comp1 != GX_ALWAYS; }
};
static_assert(std::has_unique_object_representations_v<AlphaCompare>);
struct IndTexMtxInfo {
  Mat3x2<float> mtx;
  s8 scaleExp = 0;
  // Accumulated adjScale bits from BP registers (2 bits per row, 3 rows)
  u8 adjScaleRaw = 0;

  bool operator==(const IndTexMtxInfo& rhs) const { return mtx == rhs.mtx && scaleExp == rhs.scaleExp; }
  bool operator!=(const IndTexMtxInfo& rhs) const { return !(*this == rhs); }
};
struct TexCoordScale {
  u16 scaleS = 0; // texture width - 1
  u16 scaleT = 0; // texture height - 1
  bool biasS = false;
  bool biasT = false;
  bool cylWrapS = false;
  bool cylWrapT = false;
  bool lineOffset = false;
  bool pointOffset = false;

  bool operator==(const TexCoordScale& rhs) const {
    return scaleS == rhs.scaleS && scaleT == rhs.scaleT && biasS == rhs.biasS && biasT == rhs.biasT &&
           cylWrapS == rhs.cylWrapS && cylWrapT == rhs.cylWrapT && lineOffset == rhs.lineOffset &&
           pointOffset == rhs.pointOffset;
  }
  bool operator!=(const TexCoordScale& rhs) const { return !(*this == rhs); }
};
struct VtxAttrFmt {
  GXCompCnt cnt;
  GXCompType type;
  u8 frac;
  u8 _p1 = 0;
  u8 _p2 = 0;
  u8 _p3 = 0;
};
static_assert(std::has_unique_object_representations_v<VtxAttrFmt>);
struct VtxFmt {
  std::array<VtxAttrFmt, MaxVtxAttr> attrs;
};
static_assert(std::has_unique_object_representations_v<VtxFmt>);
struct PnMtx {
  Mat3x4<float> pos;
  Mat3x4<float> nrm;
};
static_assert(sizeof(PnMtx) == sizeof(Mat3x4<float>) * 2);
struct Light {
  Vec4<float> pos{0.f, 0.f, 0.f};
  Vec4<float> dir{0.f, 0.f, 0.f};
  Vec4<float> color{0.f, 0.f, 0.f, 0.f};
  Vec4<float> cosAtt{0.f, 0.f, 0.f};
  Vec4<float> distAtt{0.f, 0.f, 0.f};

  bool operator==(const Light& rhs) const {
    return pos == rhs.pos && dir == rhs.dir && color == rhs.color && cosAtt == rhs.cosAtt && distAtt == rhs.distAtt;
  }
  bool operator!=(const Light& rhs) const { return !(*this == rhs); }
};
static_assert(sizeof(Light) == 80);
struct Fog {
  Vec4<float> color;
  float a = 0.f;
  float b = 0.5f;
  float c = 0.f;
  // The GXFogType: the curve is chosen in the shader, not by the pipeline (ShaderConfig::fogType).
  u32 type = GX_FOG_NONE;
};
static_assert(sizeof(Fog) == 32);
struct AttrArray {
  const void* data;
  u32 size;
  u8 stride;
  bool le = true;
  gfx::Range cachedRange;
  u32 baseIndex = 0; // GX_AURORA_LOAD_ARRAY_BASE_INDEX
};
inline bool operator==(const AttrArray& lhs, const AttrArray& rhs) {
  return lhs.data == rhs.data && lhs.size == rhs.size && lhs.stride == rhs.stride && lhs.le == rhs.le;
}
inline bool operator!=(const AttrArray& lhs, const AttrArray& rhs) { return !(lhs == rhs); }

struct GXState {
  struct CopyTextureRef {
    gfx::TextureHandle handle;
    u32 revision = 0;
    // Logical (unscaled) GXSetTexCopyDst size of the latest copy to this dest.
    u32 width = 0;
    u32 height = 0;
    // copy_tex call that last wrote this pooled texture (trim_copy_sizes keeps the newest).
    u64 lastCopy = 0;

    operator bool() const noexcept { return handle.operator bool(); }
  };
  struct CopyTextureKey {
    const void* dest = nullptr;
    u32 width = 0;
    u32 height = 0;
    GXTexFmt format = GX_TF_I4;
    bool mips = false;
    // The texture holds the linear RGBA16F scene (GXPortCopyScene in an HDR pass), not a GX copy.
    bool linear = false;

    bool operator==(const CopyTextureKey& rhs) const {
      return dest == rhs.dest && width == rhs.width && height == rhs.height && format == rhs.format &&
             mips == rhs.mips && linear == rhs.linear;
    }

    template <typename H>
    friend H AbslHashValue(H h, const CopyTextureKey& key) {
      return H::combine(std::move(h), key.dest, key.width, key.height, key.format, key.mips, key.linear);
    }
  };

  // Decoded state
  std::array<PnMtx, MaxPnMtx> pnMtx;
  u32 currentPnMtx;
  Mat4x4<float> proj;
  GXProjectionType projType; // for GXGetProjectionv
  FogState fog;
  GXCullMode cullMode = GX_CULL_BACK;
  u8 lineWidth = 0;
  u8 pointSize = 0;
  GXTexOffset lineTexOffset = GX_TO_ZERO;
  GXTexOffset pointTexOffset = GX_TO_ZERO;
  bool lineHalfAspect = false;
  GXBlendMode blendMode = GX_BM_NONE;
  GXBlendFactor blendFacSrc = GX_BL_SRCALPHA;
  GXBlendFactor blendFacDst = GX_BL_INVSRCALPHA;
  GXLogicOp blendOp = GX_LO_CLEAR;
  GXCompare depthFunc = GX_LEQUAL;
  Vec4<float> clearColor{0.f, 0.f, 0.f, 1.f};
  u32 clearDepth = 0xFFFFFF;
  GXPixelFmt pixelFmt = GX_PF_RGB8_Z24;
  GXZFmt16 zFmt = GX_ZC_LINEAR;
  bool zCompLocBeforeTex = false;
  u32 dstAlpha; // u8; UINT32_MAX = disabled
  AlphaCompare alphaCompare;
  std::array<Vec4<float>, MaxTevRegs> colorRegs;
  std::array<Vec4<float>, GX_MAX_KCOLOR> kcolors;
  std::array<ColorChannelConfig, MaxColorChannels> colorChannelConfig;
  std::array<ColorChannelState, MaxColorChannels> colorChannelState;
  std::array<Light, GX::MaxLights> lights;
  std::array<TevStage, MaxTevStages> tevStages;
  std::array<GXTexObj_, MaxTextures> loadedTextures;
  std::array<GXTlutObj_, MaxTluts> loadedTluts;
  std::array<Mat3x4<float>, MaxTexMtx> texMtxs;
  std::array<Mat3x4<float>, MaxPTTexMtx> ptTexMtxs;
  std::array<TcgConfig, MaxTexCoord> tcgs;
  std::array<TexCoordScale, MaxTexCoord> texCoordScales;
  std::array<GXAttrType, MaxVtxAttr> vtxDesc;
  std::array<VtxFmt, MaxVtxFmt> vtxFmts;
  std::array<TevSwap, MaxTevSwap> tevSwapTable{
      TevSwap{},
      TevSwap{GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA},
      TevSwap{GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA},
      TevSwap{GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA},
  };
  std::array<IndStage, MaxIndStages> indStages;
  std::array<IndTexMtxInfo, MaxIndTexMtxs> indTexMtxs;
  std::array<AttrArray, MaxVtxAttr> arrays;
  bool depthCompare = true;
  bool depthUpdate = true;
  bool colorUpdate = true;
  bool alphaUpdate = true;
  u8 numChans = 0;
  u8 numIndStages = 0;
  u8 numTevStages = 0;
  u8 numTexGens = 0;
  u8 pbr = 0; // GX_AURORA_SET_PBR: above 1, a cost test (GXSetPBRCostTest)
  u8 sdf = 0; // GX_AURORA_SET_SDF
  u8 hudSample = 0; // GX_AURORA_SET_HUD_SAMPLE
  Vec4<float> hudDyin{0.f, 0.f, 0.f, 0.f}; // its DYIN
  u8 depthPrepass = 0; // GX_AURORA_PORT_DEPTH_PREPASS
  std::array<u32, 3> drawTag{0, UINT32_MAX, 0}; // GX_AURORA_SET_DRAW_TAG: asset, model index, material
  u32 drawSerial = 0; // GX_AURORA_PORT_DRAW_SERIAL
  bool drawIdMode = false; // GX_AURORA_PORT_DRAW_ID_MODE
  Mat3x4<float> pbrProbe; // GX_AURORA_SET_PBR_PROBE
  Vec4<float> pbrEmissive{1.f, 1.f, 1.f, 0.f}; // GX_AURORA_SET_PBR_MATERIAL
  Vec4<float> pbrBacklight{0.f, 0.f, 0.f, 0.f};
  Vec4<float> pbrLayer{0.f, 0.f, 0.f, 0.f};       // x: the edge width of a second layer's blend
  Vec4<float> pbrLayerHeight{1.f, 0.f, 1.f, 0.f}; // scale and offset of each layer's height
  Vec4<float> pbrParam{0.f, 0.f, 0.f, 0.f};       // the parameters of pbrLayer's kind (y; z its strength)
  Vec4<float> pbrUp{0.f, 1.f, 0.f, 0.f};          // world up in view space
  u32 pbrCube = 0; // GX_AURORA_SET_PBR_CUBE
  Vec4<float> pbrCubeParams{0.f, 0.f, 0.f, 0.f}; // see GXSetPBRCube
  std::array<Vec4<float>, 6> pbrAmbient{}; // GX_AURORA_SET_PBR_AMBIENT
  bool pbrBrdfLut = false; // GX_AURORA_SET_PBR_BRDF_LUT: a table is bound
  u32 pbrVolume = 0; // GX_AURORA_SET_PBR_VOLUME
  std::array<Vec4<float>, 6> pbrVolumeRows{}; // see GXSetPBRVolume
  u32 pbrLightmap = 0; // GX_AURORA_SET_PBR_LIGHTMAP
  Vec4<float> pbrLightmapRect{};                  // offU, offV, scale (0: one texel), level (0: off)
  std::array<Vec4<float>, 3> pbrLightmapAxes{};   // xyz: a row taking a view-space normal to the lightmap's axes
  Vec4<float> pbrRoomLights{}; // GX_AURORA_PORT_ROOM_LIGHTS: x the first record (u32 bits, in words), y the count
  u8 pbrLightmapAttr = GX_VA_NULL; // GX_AURORA_SET_PBR_LIGHTMAP_ATTR
  std::array<Vec4<float>, 3> pbrTone{}; // GX_AURORA_SET_PBR_TONE
  Vec4<float> pbrLightSkip{}; // GX_AURORA_SET_PBR_LIGHT_SKIP: x the mask
  Vec4<float> pbrBakedLightModulation{1.f, 1.f, 1.f, 0.f}; // GX_AURORA_SET_PBR_BAKED_LIGHT_MODULATION
  // GX_AURORA_SET_PBR_BACKLIGHT: the height plane; the back colour direction and strength;
  // the top strength.
  std::array<Vec4<float>, 3> pbrBacklightLights{};
  Vec4<float> pbrLightScale{1.f, 1.f, 0.f, 0.f}; // GX_AURORA_SET_PBR_LIGHT_SCALE: diffuse, f0, -, fade
  // GX_AURORA_SET_PBR_MODEL_MODULATION: the diffuse and incandescence vectors, and the coverage (x)
  Vec4<float> pbrModelDiffuse{1.f, 1.f, 1.f, 1.f};
  Vec4<float> pbrModelIncan{0.f, 0.f, 0.f, 1.f};
  Vec4<float> pbrModelCoverage{1.f, 0.f, 0.f, 0.f};
  std::array<Vec4<float>, 8> pbrShield{}; // GX_AURORA_SET_PBR_SHIELD: CCH0..6, then AUVI xyz + DIFC.w
  // GX_AURORA_SET_PBR_LIGHT_HDR, per light: colour (rgb, falloff + 1; w 0 = off), then
  // (view position, r0), then (r1, -, -, -).
  std::array<Vec4<float>, GX::MaxLights * 3> pbrLightHdr{};
  // GX_AURORA_PORT_VOLUMETRIC_FOG up to its _END: the draws fog themselves (ShaderConfig::volFog)
  // with the recorded fog's near, range, exposure and the start of the world's depth range (nearer is the viewmodel) and its tone curve.
  bool volFog = false;
  Vec4<float> volFogParams{};
  std::array<Vec4<float>, 3> volFogTone{};
  // GX_AURORA_PORT_SHADOW_*: the following draws are the world's (they cast and receive the sun's
  // shadow), and this frame's sun (shadowActive) with its uniform (gfx/shadow.hpp's Uniform).
  // GX_AURORA_PORT_PARTICLE_FOG: the draws are particles, kParticleUnfogged unfogged ones (vol_fog_mode)
  static constexpr u8 kParticleUnfogged = 2; // GX_PORT_PARTICLE_UNFOGGED
  u8 particleFog = 0;
  bool shadowCaster = false;
  bool shadowCasterOnly = false; // they cast but aren't drawn (GXPortSetShadowCasterOnly)
  bool shadowActive = false;
  std::array<Vec4<float>, 36> shadowUniform{};
  // GX_AURORA_PORT_ACTOR_SHADOW: actorLayer >= 0 while an actor's casters are recorded (into that
  // layer of the actor maps; the sun's state is saved in actorSaved* meanwhile); actorDecal while
  // the receivers' decal draws are.
  int actorLayer = -1;
  bool actorDecal = false;
  bool actorSavedCaster = false;
  bool actorSavedCasterOnly = false;
  bool actorSavedActive = false;
  std::array<Vec4<float>, 36> actorSavedUniform{};

  // GX2 polygon offset state
  f32 frontOffset = 0.0f;
  f32 frontScale = 0.0f;
  f32 backOffset = 0.0f;
  f32 backScale = 0.0f;
  f32 clamp = 0.0f;

  // View state
  AuroraViewportPolicy viewportPolicy = AURORA_VIEWPORT_FIT;
  gfx::Viewport logicalViewport{0.f, 0.f, 640.f, 480.f, 0.f, 1.f};
  gfx::Viewport renderViewport{0.f, 0.f, 640.f, 480.f, 0.f, 1.f};
  gfx::ClipRect logicalScissor{0, 0, 640, 480};
  gfx::ClipRect renderScissor{0, 0, 640, 480};

  // EFB copy state
  gfx::ClipRect texCopySrc;
  GXTexFmt texCopyFmt;
  u32 texCopyDstWidth = 0;
  u32 texCopyDstHeight = 0;
  bool texCopyDstWide = false;
  bool texCopyMips = false; // GXSetTexCopyDst mipmap flag: the copy gets a full mip chain
  const void* texCopyDest = nullptr;
  bool copySceneLinear = false; // GX_AURORA_PORT_COPY_SCENE: copies are linear scene copies (GXPortCopyScene)
  absl::flat_hash_map<const void*, CopyTextureRef> copyTextures;
  absl::flat_hash_map<CopyTextureKey, CopyTextureRef> copyTextureCache;

  // Cache state
  std::array<gfx::TextureBind, MaxTextures> textures;
  u16 lastVtxSize = 0;
  GXVtxFmt lastVtxFmt = GX_MAX_VTXFMT;
  u8 dirty = DirtyAll;

  // Shadow registers
  std::array<u32, 0x100> bpRegCache = [] {
    std::array<u32, 0x100> regs{};
    regs[0xFE] = 0x00FFFFFF;
    return regs;
  }();
  std::bitset<0x100> bpRegValid;
  std::array<u32, 0x100> cpRegCache{};
  std::bitset<0x100> cpRegValid;
  std::array<u32, XfRegCount> xfRegCache{};
  std::bitset<XfRegCount> xfRegValid;

  void clearVtxSizeCache() { lastVtxFmt = GX_MAX_VTXFMT; }
};
extern GXState g_gxState;

// The bind-pose positions of a skinned draw: the array at GX_VA_TEX7 (which the draw's own vertices
// must not use), read with the same index as GX_VA_POS, when the backlight asks for them (its z in
// row 2 is the height scale).
inline bool bind_pos_active() noexcept {
  const auto& s = g_gxState;
  return s.pbr != 0 && s.arrays[GX_VA_TEX7].data != nullptr && s.arrays[GX_VA_TEX7].size != 0 &&
         s.vtxDesc[GX_VA_TEX7] == GX_NONE && (s.vtxDesc[GX_VA_POS] == GX_INDEX8 || s.vtxDesc[GX_VA_POS] == GX_INDEX16) &&
         s.pbrBacklightLights[2].z() > 0.f;
}
struct ShaderInfo;

void initialize() noexcept;
void shutdown() noexcept;
void update() noexcept;
void set_viewport_policy(AuroraViewportPolicy policy) noexcept;
void clear_static_texture_cache() noexcept;
void clear_copy_texture_cache() noexcept;
void trim_copy_texture_cache() noexcept;
void evict_copy_texture(const void* dest) noexcept;
// Port: drops the palette-converted textures made from one copy texture, for a caller that
// releases that copy texture itself (copy_tex's per-destination eviction).
void evict_dynamic_palettes(const void* sourceIdentity) noexcept;
void trim_copy_sizes(const void* dest, size_t keep) noexcept;
void evict_texture_object(u32 texObjId) noexcept;
void evict_tlut_object(u32 tlutObjId) noexcept;
Vec2<uint32_t> logical_fb_size() noexcept;
gfx::Viewport map_logical_viewport(const gfx::Viewport& logicalViewport) noexcept;
gfx::ClipRect map_logical_scissor(const gfx::ClipRect& logicalScissor) noexcept;
void set_logical_viewport(const gfx::Viewport& viewport) noexcept;
void set_render_viewport(const gfx::Viewport& viewport) noexcept;
void set_logical_scissor(const gfx::ClipRect& scissor) noexcept;
void set_render_scissor(const gfx::ClipRect& scissor) noexcept;
void set_draw_sync_token(u16 token) noexcept;
u16 draw_sync_token() noexcept;
void copy_tex(const void* dest, GXBool clear) noexcept;
void copy_probe_face(u32 face) noexcept;
// Whether load_word is generated with the clamped storage read (Adreno 730 on Vulkan; see shader.cpp).
bool storage_load_clamp_active() noexcept;
const gfx::TextureBind& get_texture(GXTexMapID id) noexcept;
void resolve_sampled_textures(const ShaderInfo& info) noexcept;

inline float clear_depth_value() {
  const float normalizedDepth = static_cast<float>(g_gxState.clearDepth) / 16777215.f;
  return UseReversedZ ? (1.f - normalizedDepth) : normalizedDepth;
}

static inline bool requires_load_conversion(const GXTexObj_& obj) {
  switch (obj.format()) {
  case GX_TF_I4:
  case GX_TF_I8:
  case GX_TF_C4:
  case GX_TF_C8:
  case GX_TF_C14X2:
  case GX_TF_R8_PC:
    return true;
  default:
    return false;
  }
}
static inline bool is_palette_format(u32 fmt) { return fmt == GX_TF_C4 || fmt == GX_TF_C8 || fmt == GX_TF_C14X2; }
static inline bool is_depth_format(u32 fmt) {
  return fmt == GX_TF_Z8 || fmt == GX_TF_Z16 || fmt == GX_TF_Z24X8 || fmt == GX_CTF_Z4 || fmt == GX_CTF_Z8M ||
         fmt == GX_CTF_Z8L || fmt == GX_CTF_Z16L;
}

struct AttrConfig {
  u8 attrType = GX_NONE; // GXAttrType
  u8 cnt = 0xFF;         // Actual count; not GXCompCnt
  u8 compType = 0xFF;    // GXCompType
  u8 offset = 0;         // Offset within vertex
  u8 stride = 0;         // Array stride
  u8 frac = 0;
  bool le = true;
  bool nbt3 = false; // GX_NRM_NBT3
};
// ShaderConfig::volFog, as Remastered's shaders fog each kind of draw after the full-screen fog.
enum : u8 {
  VolFogNone = 0,       // no fog: multiplying or subtracting blends, depth-only passes
  VolFogBlended = 1,    // alpha blended, per vertex: colour T + in-scatter
  VolFogAdditive = 2,   // added to the frame, per vertex: colour T
  VolFogOpaque = 3,     // not blended, per pixel: as the full-screen pass
  VolFogPremultiplied = 4, // ONE, INVSRCALPHA, per vertex: colour T + in-scatter alpha
};
struct ShaderConfig {
  // GX_FOG_NONE or, for every other type, GX_FOG_PERSP_LIN (pipeline_fog_type): the curve is the Fog
  // uniform's, so a fog change (entering water) needs no new pipeline.
  u8 fogType = GX_FOG_NONE;
  u8 vtxStride = 0;
  u8 lineMode : 2 = 0; // 1 = GX_LINES, 2 = GX_LINESTRIP, 3 = GX_POINTS
  u8 fogRangeEnabled : 1 = false;
  u8 drawId : 1 = false; // debug view "drawid": the fragment is the draw serial (DrawImmediateData::serial)
  // GX_AURORA_PORT_SHADOW_CASTER while a shadow frame is set: the draw casts into the sun's shadow
  // map (an extra vs_shadow entry) and, when PBR, receives the sun through it.
  u8 shadow : 1 = false;
  // With pbr, the draw is a skinned model whose bind-pose positions (GX_VA_TEX7's array, indexed
  // like GX_VA_POS) feed the character backlight's height fade (GXSetPBRBacklight); the array's
  // endianness.
  u8 pbrBindPos : 1 = false;
  u8 pbrBindLe : 1 = false;
  // The draw writes the RGBA16F HDR scene (gfx::scene_hdr_pass_active): exposed linear light, not
  // display sRGB. The old padding bit, so an LDR config keeps its bytes and cache keys.
  u8 sceneHdr : 1 = false;
  u8 pbr = 0; // GX_AURORA_SET_PBR
  u8 sdf = 0; // GX_AURORA_SET_SDF
  u8 depthOnly = 0; // pass 1 of GX_AURORA_PORT_DEPTH_PREPASS: the colour is not written
  u8 pbrKind = 0; // with pbr, the special surface kind (pbrLayer.y), a constant in the shader
  // GXState::volFog: how the draw fogs itself (VolFog*), chosen from its blend.
  u8 volFog = 0;
  // With pbr, the vertex attribute holding the baked lightmap's UV (GX_VA_TEX0..7), or GX_VA_NULL: a varying in the shader.
  u8 pbrLightmapAttr = GX_VA_NULL;
  u8 hudSample = 0; // GX_AURORA_SET_HUD_SAMPLE
  // aurora_set_per_pixel_lighting, on a draw with a lit channel: the GX lights are summed per
  // fragment from the interpolated view-space position and normal instead of per vertex. The
  // old padding, so a config with it off keeps its bytes and cache keys.
  u8 perPixelLight = 0;
  // GX_AURORA_PORT_ACTOR_SHADOW decal: the draw is a receiver redrawn as Remastered's actor shadow
  // decal (multiplies the frame by the shadow, shadowUniform holds its view-space planes). The old
  // padding byte, so other configs keep their bytes and cache keys.
  u8 actorDecal = 0;
  std::array<AttrConfig, MaxVtxAttr> attrs;
  std::array<TevSwap, MaxTevSwap> tevSwapTable;
  std::array<TevStage, MaxTevStages> tevStages;
  u32 tevStageCount = 0;
  std::array<ColorChannelConfig, MaxColorChannels> colorChannels;
  std::array<TcgConfig, MaxTexCoord> tcgs;
  AlphaCompare alphaCompare;
  std::array<IndStage, MaxIndStages> indStages{};
  u32 numIndStages = 0;

  bool operator==(const ShaderConfig& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
};
static_assert(std::has_unique_object_representations_v<ShaderConfig>);
constexpr u8 pipeline_fog_type(u32 fogType) noexcept {
  return fogType == GX_FOG_NONE ? GX_FOG_NONE : GX_FOG_PERSP_LIN;
}
// An alpha compare reference the shader reads from the alpha_ref uniform (ShaderInfo::usesAlphaRef).
constexpr u32 AlphaRefUniform = 0x100;
// The reference as the pipeline sees it: only the values that make the compare constant (0 for
// LESS/GEQUAL, 255 for LEQUAL/GREATER) stay, any other is the uniform's, so a fading cut-out
// needs no new pipeline per reference.
constexpr u32 pipeline_alpha_ref(GXCompare comp, u32 ref) noexcept {
  switch (comp) {
  case GX_LESS:
  case GX_GEQUAL:
    return ref == 0 ? 0 : AlphaRefUniform;
  case GX_LEQUAL:
  case GX_GREATER:
    return ref == 255 ? 255 : AlphaRefUniform;
  case GX_EQUAL:
  case GX_NEQUAL:
    return AlphaRefUniform;
  default:
    return 0;
  }
}
constexpr AlphaCompare pipeline_alpha_compare(AlphaCompare ac) noexcept {
  ac.ref0 = pipeline_alpha_ref(ac.comp0, ac.ref0);
  ac.ref1 = pipeline_alpha_ref(ac.comp1, ac.ref1);
  return ac;
}
// The konst selections are the tev_konst uniform's (ShaderInfo::usesTevKonst), not the pipeline's.
constexpr void pipeline_tev_konst(ShaderConfig& config) noexcept {
  for (auto& stage : config.tevStages) {
    stage.kcSel = GX_TEV_KCSEL_1;
    stage.kaSel = GX_TEV_KASEL_1;
  }
}
// Whether a draw samples the sun's shadow map: a lit PBR surface drawn in colour without per-pixel fog.
inline bool shadow_receives(const ShaderConfig& sc) noexcept {
  return sc.shadow && (sc.pbr != 0 || sc.actorDecal != 0) && sc.volFog == VolFogNone && sc.depthOnly == 0 && sc.lineMode == 0 && !sc.drawId;
}
// Whether a draw is the actor shadow decal (samples gfx/actor_shadow.hpp's maps).
inline bool actor_decal(const ShaderConfig& sc) noexcept { return sc.actorDecal != 0 && sc.shadow; }

struct PipelineConfig;

struct GXBindGroups {
  gfx::BindGroupRef textureBindGroup;
};
// Output info from shader generation
struct ShaderInfo {
  std::bitset<MaxTexCoord> sampledTexCoords;
  std::bitset<MaxTextures> sampledTextures;
  u8 tevStageCount = 0; // the tev_konst uniform's length (usesTevKonst)
  std::bitset<MaxColorChannels / 2> sampledColorChannels;
  std::bitset<MaxTevRegs> loadsTevReg;
  std::bitset<MaxTevRegs> writesTevReg;
  std::bitset<MaxPTTexMtx> usesPTTexMtx;
  std::bitset<MaxVtxAttr> indexAttr;
  std::bitset<MaxIndStages> usedIndStages;
  std::bitset<MaxTextures> sampledIndTextures;
  std::bitset<MaxIndTexMtxs> usedIndTexMtxs;
  u32 uniformSize = 0;
  bool usesFog : 1 = false;
  bool lightingEnabled : 1 = false;
  u8 lineMode : 2 = 0;
  bool usesPbr : 1 = false;
  bool usesHudDyin : 1 = false; // ShaderConfig::hudSample >= 2: the hud_dyin uniform
  bool usesLightmap : 1 = false; // ShaderConfig::pbrLightmapAttr is set
  bool usesVolFog : 1 = false;
  bool usesShadow : 1 = false;    // ShaderConfig::shadow: the shadow uniforms and vs_shadow
  bool shadowReceive : 1 = false; // shadow_receives: group 2 uses the shadow layout
  bool actorDecal : 1 = false;    // actor_decal: group 2 uses the shadow layout with the actor maps
  bool usesHdrTone : 1 = false;   // sceneHdr without pbr: hdr_tone (pbrTone) and hdr_tev_blend
  bool usesTevKonst : 1 = false; // a stage reads a konst input: the tev_konst uniform
  bool usesAlphaRef : 1 = false;  // an alpha compare reference is AlphaRefUniform: the alpha_ref uniform
};
struct BindGroupRanges {
  std::array<gfx::Range, MaxIndexAttr> vaRanges{};
};
void populate_pipeline_config(PipelineConfig& config, GXPrimitive primitive, GXVtxFmt fmt) noexcept;
// aurora_set_per_pixel_lighting; any thread.
void set_per_pixel_lighting(bool enabled) noexcept;
bool per_pixel_lighting() noexcept;
// While set, every draw's pipeline lookup joins the front of the compile queue and waits for it
// (see gfx::PipelinePriority::Blocking): the draws of the passes that set it are never skipped for a
// pipeline that is still compiling. The GPU self-test sets it for its own passes; off by default.
void set_blocking_pipelines(bool enabled) noexcept;
bool blocking_pipelines() noexcept;
// ShaderConfig::perPixelLight for a config with its other fields set: a PBR draw shades per
// pixel already (and has no interpolators to spare).
inline bool wants_per_pixel_light(const ShaderConfig& config) noexcept {
  if (!per_pixel_lighting() || config.pbr != 0) {
    return false;
  }
  for (const auto& cc : config.colorChannels) {
    if (cc.lightingEnabled) {
      return true;
    }
  }
  return false;
}
// How a draw writes GXSetDstAlpha's constant alpha (the pipeline's blend constant). None: alpha as blended
// (no dst alpha, or a zero one, or the fallback below). Replace: the shader writes alpha 1 and the blend takes
// the constant times it. DualSource: as Replace, the TEV alpha going to the colour blend as the second source,
// for a draw whose colour blend reads the source alpha. Without the device feature that draw stays None: its
// alpha is then the constant times the TEV alpha.
enum class DstAlphaMode : u8 { None, Replace, DualSource };
DstAlphaMode dst_alpha_mode(const PipelineConfig& config) noexcept;
wgpu::RenderPipeline build_pipeline(const PipelineConfig& config, ArrayRef<wgpu::VertexBufferLayout> vtxBuffers,
                                    wgpu::ShaderModule shader, const char* label,
                                    DstAlphaMode dstAlphaMode = DstAlphaMode::None) noexcept;
std::string build_shader_source(const ShaderConfig& config) noexcept;
// source (build_shader_source's) writing its alpha as mode has it.
// False (source unchanged) when it has no fs_main return to rewrite: the draw must then use DstAlphaMode::None.
bool apply_dst_alpha_mode(std::string& source, DstAlphaMode mode) noexcept;
// Whether the config's shader has the alpha-tested sun shadow caster (shadowPass 2: vs_shadow_cut, fs_shadow).
bool shader_has_cut_caster(const ShaderConfig& config) noexcept;
// dstAlphaMode falls back to None when the shader can't be rewritten for it; build the pipeline with that.
wgpu::ShaderModule build_shader(const ShaderConfig& config, DstAlphaMode& dstAlphaMode) noexcept;
// Shader debugging (MP2_WGSL_DUMP / MP2_WGSL_OVERRIDE, console `shader`). dump_shaders writes every module built so
// far to dir (and keeps writing new ones) and returns how many files it made; set_shader_override_dir turns the
// override on (empty = off).
u32 dump_shaders(const char* dir) noexcept;
void set_shader_override_dir(const char* dir) noexcept;
// While on, each draw with a nonzero serial records the hash of its ShaderConfig; draw_shader_hash reads it back (0 =
// none) and shader_overridden says whether an edited source is in place for a hash.
void set_draw_shader_log(bool on) noexcept;
void note_draw_shader(u32 serial, const ShaderConfig& config) noexcept;
u64 draw_shader_hash(u32 serial) noexcept;
bool shader_overridden(u64 hash) noexcept;
GXBindGroups build_bind_groups(const ShaderInfo& info) noexcept;

u8 comp_type_size(GXAttr attr, GXCompType type) noexcept;
u8 comp_cnt_count(GXAttr attr, GXCompCnt cnt) noexcept;

// Only RGBA6_Z24 stores alpha: an RGB8 or RGB565 EFB writes none, and it reads (blends, copies) as 1.
constexpr bool efb_has_alpha(GXPixelFmt format) noexcept { return format == GX_PF_RGBA6_Z24; }
} // namespace aurora::gx
