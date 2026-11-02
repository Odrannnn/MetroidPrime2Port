#include "gx.hpp"

#include "pipeline.hpp"
#include "texture.hpp"
#include "../dolphin/vi/vi_internal.hpp"
#include "../webgpu/gpu.hpp"
#include "../internal.hpp"
#include "../window.hpp"
#include "../gfx/hash.hpp"
#include "../gfx/resources.hpp"
#include "../gfx/probe.hpp"
#include "../gfx/recording.hpp"
#include "../gfx/resource_cache.hpp"
#include "../gfx/shadow.hpp"
#include "../gfx/actor_shadow.hpp"
#include "../gfx/volfog.hpp"
#include "../gfx/texture.hpp"
#include "gx_fmt.hpp"

#include <absl/container/flat_hash_map.h>
#include <tracy/Tracy.hpp>

#include <atomic>
#include <bit>
#include <cfloat>
#include <cmath>
#include <mutex>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <utility>
#include "../gfx/texture_sites.hpp"

static aurora::Module Log("aurora::gx");

namespace aurora::gx {
using webgpu::g_device;
using webgpu::g_graphicsConfig;

GXState g_gxState{};
wgpu::BindGroup g_emptyTextureBindGroup;

namespace {
wgpu::Sampler sEmptySampler;
// The sampler each texture slot took last, by its descriptor's value fields (build_bind_groups)
constexpr auto kSamplerKeyOffset = offsetof(wgpu::SamplerDescriptor, addressModeU);
constexpr auto kSamplerKeySize = offsetof(wgpu::SamplerDescriptor, maxAnisotropy) + sizeof(uint16_t) - kSamplerKeyOffset;
std::array<std::array<u8, kSamplerKeySize>, MaxTextures> sSlotSamplerKeys{};
std::array<wgpu::Sampler, MaxTextures> sSlotSamplers;
wgpu::Texture sEmptyTexture;
wgpu::TextureView sEmptyTextureView;
std::mutex sBindGroupLayoutMutex;
absl::flat_hash_map<u32, wgpu::BindGroupLayout> sUniformBindGroupLayouts;
absl::flat_hash_map<u32, std::pair<wgpu::BindGroupLayout, wgpu::BindGroupLayout>> sTextureBindGroupLayouts;
wgpu::BindGroupLayout sTextureBindGroupLayout;
wgpu::BindGroupLayout sSamplerBindGroupLayout;
wgpu::PipelineLayout sPipelineLayout;
// A shadow receiver's group 2 (the shadow map in the froxel's slots) and its pipeline layout, and the
// shadow map pass's layout, which has no group 2.
wgpu::BindGroupLayout sShadowTextureBindGroupLayout;
wgpu::PipelineLayout sShadowRecvPipelineLayout;
wgpu::PipelineLayout sShadowPipelineLayout;

std::atomic<int> sPendingViewportPolicy{-1};
// Last GXSetDrawSync token whose FIFO command has been processed.
std::atomic<u16> sDrawSyncToken{0};

template <typename T>
T round_away_from_zero(float value) noexcept {
  return static_cast<T>(value < 0.0f ? std::floor(value) : std::ceil(value));
}

std::pair<f32, f32> polygon_offset_for_cull_mode(GXCullMode cullMode) noexcept {
  if (cullMode == GX_CULL_FRONT) {
    return {g_gxState.backOffset, g_gxState.backScale};
  }
  return {g_gxState.frontOffset, g_gxState.frontScale};
}

wgpu::BlendFactor to_blend_factor(GXBlendFactor fac, bool isDst) {
  switch (fac) {
    DEFAULT_FATAL("invalid blend factor {}", underlying(fac));
  case GX_BL_ZERO:
    return wgpu::BlendFactor::Zero;
  case GX_BL_ONE:
    return wgpu::BlendFactor::One;
  case GX_BL_SRCCLR: // + GX_BL_DSTCLR
    if (isDst) {
      return wgpu::BlendFactor::Src;
    } else {
      return wgpu::BlendFactor::Dst;
    }
  case GX_BL_INVSRCCLR: // + GX_BL_INVDSTCLR
    if (isDst) {
      return wgpu::BlendFactor::OneMinusSrc;
    } else {
      return wgpu::BlendFactor::OneMinusDst;
    }
  case GX_BL_SRCALPHA:
    return wgpu::BlendFactor::SrcAlpha;
  case GX_BL_INVSRCALPHA:
    return wgpu::BlendFactor::OneMinusSrcAlpha;
  case GX_BL_DSTALPHA:
    return wgpu::BlendFactor::DstAlpha;
  case GX_BL_INVDSTALPHA:
    return wgpu::BlendFactor::OneMinusDstAlpha;
  }
}

wgpu::CompareFunction to_compare_function(GXCompare func) {
  switch (func) {
    DEFAULT_FATAL("invalid depth fn {}", underlying(func));
  case GX_NEVER:
    return wgpu::CompareFunction::Never;
  case GX_LESS:
    return UseReversedZ ? wgpu::CompareFunction::Greater : wgpu::CompareFunction::Less;
  case GX_EQUAL:
    return wgpu::CompareFunction::Equal;
  case GX_LEQUAL:
    return UseReversedZ ? wgpu::CompareFunction::GreaterEqual : wgpu::CompareFunction::LessEqual;
  case GX_GREATER:
    return UseReversedZ ? wgpu::CompareFunction::Less : wgpu::CompareFunction::Greater;
  case GX_NEQUAL:
    return wgpu::CompareFunction::NotEqual;
  case GX_GEQUAL:
    return UseReversedZ ? wgpu::CompareFunction::LessEqual : wgpu::CompareFunction::GreaterEqual;
  case GX_ALWAYS:
    return wgpu::CompareFunction::Always;
  }
}

wgpu::BlendState to_blend_state(GXBlendMode mode, GXBlendFactor srcFac, GXBlendFactor dstFac, GXLogicOp op,
                                u32 dstAlpha, bool dualSource) {
  wgpu::BlendComponent colorBlendComponent;
  switch (mode) {
    DEFAULT_FATAL("unsupported blend mode {}", underlying(mode));
  case GX_BM_NONE:
    colorBlendComponent = {
        .operation = wgpu::BlendOperation::Add,
        .srcFactor = wgpu::BlendFactor::One,
        .dstFactor = wgpu::BlendFactor::Zero,
    };
    break;
  case GX_BM_BLEND:
    colorBlendComponent = {
        .operation = wgpu::BlendOperation::Add,
        .srcFactor = to_blend_factor(srcFac, false),
        .dstFactor = to_blend_factor(dstFac, true),
    };
    break;
  case GX_BM_SUBTRACT:
    colorBlendComponent = {
        .operation = wgpu::BlendOperation::ReverseSubtract,
        .srcFactor = wgpu::BlendFactor::One,
        .dstFactor = wgpu::BlendFactor::One,
    };
    break;
  case GX_BM_LOGIC:
    switch (op) {
      DEFAULT_FATAL("unsupported logic op {}", underlying(op));
    case GX_LO_CLEAR:
      colorBlendComponent = {
          .operation = wgpu::BlendOperation::Add,
          .srcFactor = wgpu::BlendFactor::Zero,
          .dstFactor = wgpu::BlendFactor::Zero,
      };
      break;
    case GX_LO_COPY:
      colorBlendComponent = {
          .operation = wgpu::BlendOperation::Add,
          .srcFactor = wgpu::BlendFactor::One,
          .dstFactor = wgpu::BlendFactor::Zero,
      };
      break;
    case GX_LO_NOOP:
      colorBlendComponent = {
          .operation = wgpu::BlendOperation::Add,
          .srcFactor = wgpu::BlendFactor::Zero,
          .dstFactor = wgpu::BlendFactor::One,
      };
      break;
    }
    break;
  }
  if (dualSource) {
    // The TEV alpha is the second source; the first's alpha is 1 (DstAlphaMode::DualSource).
    const auto remap = [](wgpu::BlendFactor factor) {
      if (factor == wgpu::BlendFactor::SrcAlpha) {
        return wgpu::BlendFactor::Src1Alpha;
      }
      if (factor == wgpu::BlendFactor::OneMinusSrcAlpha) {
        return wgpu::BlendFactor::OneMinusSrc1Alpha;
      }
      return factor;
    };
    colorBlendComponent.srcFactor = remap(colorBlendComponent.srcFactor);
    colorBlendComponent.dstFactor = remap(colorBlendComponent.dstFactor);
  }
  wgpu::BlendComponent alphaBlendComponent;
  if (dstAlpha != UINT32_MAX) {
    alphaBlendComponent = wgpu::BlendComponent{
        .operation = wgpu::BlendOperation::Add,
        .srcFactor = wgpu::BlendFactor::Constant,
        .dstFactor = wgpu::BlendFactor::Zero,
    };
  } else {
    alphaBlendComponent = colorBlendComponent;
  }
  return {
      .color = colorBlendComponent,
      .alpha = alphaBlendComponent,
  };
}

wgpu::ColorWriteMask to_write_mask(bool colorUpdate, bool alphaUpdate) {
  wgpu::ColorWriteMask writeMask = wgpu::ColorWriteMask::None;
  if (colorUpdate) {
    writeMask |= wgpu::ColorWriteMask::Red | wgpu::ColorWriteMask::Green | wgpu::ColorWriteMask::Blue;
  }
  if (alphaUpdate) {
    writeMask |= wgpu::ColorWriteMask::Alpha;
  }
  return writeMask;
}

wgpu::PrimitiveState to_primitive_state(GXCullMode gx_cullMode) {
  auto cullMode = wgpu::CullMode::None;
  switch (gx_cullMode) {
    DEFAULT_FATAL("unsupported cull mode {}", underlying(gx_cullMode));
  case GX_CULL_FRONT:
    cullMode = wgpu::CullMode::Front;
    break;
  case GX_CULL_BACK:
    cullMode = wgpu::CullMode::Back;
    break;
  case GX_CULL_NONE:
    break;
  }
  return {
      .topology = wgpu::PrimitiveTopology::TriangleList,
      .stripIndexFormat = wgpu::IndexFormat::Undefined,
      .frontFace = wgpu::FrontFace::CW,
      .cullMode = cullMode,
  };
}
std::atomic_bool sPerPixelLighting{false};
std::atomic_bool sBlockingPipelines{false};
} // namespace

void set_per_pixel_lighting(bool enabled) noexcept { sPerPixelLighting.store(enabled, std::memory_order_relaxed); }
bool per_pixel_lighting() noexcept { return sPerPixelLighting.load(std::memory_order_relaxed); }

void set_blocking_pipelines(bool enabled) noexcept { sBlockingPipelines.store(enabled, std::memory_order_relaxed); }
bool blocking_pipelines() noexcept { return sBlockingPipelines.load(std::memory_order_relaxed); }

void set_viewport_policy(AuroraViewportPolicy policy) noexcept {
  sPendingViewportPolicy.store(policy, std::memory_order_release);
}

void update() noexcept {
  if (const int pending = sPendingViewportPolicy.exchange(-1, std::memory_order_acq_rel); pending != -1) {
    const auto policy = static_cast<AuroraViewportPolicy>(pending);
    g_gxState.viewportPolicy = policy;
    window::set_frame_buffer_aspect_fit(policy == AURORA_VIEWPORT_FIT);
  }
}

Vec2<uint32_t> logical_fb_size() noexcept {
  return gfx::is_offscreen() ? gfx::get_render_target_size() : vi::configured_fb_size();
}

gfx::Viewport map_logical_viewport(const gfx::Viewport& logicalViewport) noexcept {
  if (g_gxState.viewportPolicy == AURORA_VIEWPORT_NATIVE) {
    return logicalViewport;
  }

  const auto [logicalFbWidth, logicalFbHeight] = logical_fb_size();
  const auto [targetWidth, targetHeight] = gfx::get_render_target_size();
  if (logicalFbWidth == 0 || logicalFbHeight == 0 || targetWidth == 0 || targetHeight == 0) {
    return logicalViewport;
  }

  const float scaleX = static_cast<float>(targetWidth) / static_cast<float>(logicalFbWidth);
  const float scaleY = static_cast<float>(targetHeight) / static_cast<float>(logicalFbHeight);
  return {
      .left = logicalViewport.left * scaleX,
      .top = logicalViewport.top * scaleY,
      .width = logicalViewport.width * scaleX,
      .height = logicalViewport.height * scaleY,
      .znear = logicalViewport.znear,
      .zfar = logicalViewport.zfar,
  };
}

gfx::ClipRect map_logical_scissor(const gfx::ClipRect& logicalScissor) noexcept {
  if (g_gxState.viewportPolicy == AURORA_VIEWPORT_NATIVE) {
    return logicalScissor;
  }

  const auto [logicalFbWidth, logicalFbHeight] = logical_fb_size();
  const auto [targetWidth, targetHeight] = gfx::get_render_target_size();
  if (logicalFbWidth == 0 || logicalFbHeight == 0 || targetWidth == 0 || targetHeight == 0) {
    return logicalScissor;
  }

  const float scaleX = static_cast<float>(targetWidth) / static_cast<float>(logicalFbWidth);
  const float scaleY = static_cast<float>(targetHeight) / static_cast<float>(logicalFbHeight);

  const float left = static_cast<float>(logicalScissor.x) * scaleX;
  const float top = static_cast<float>(logicalScissor.y) * scaleY;
  const float right = static_cast<float>(logicalScissor.x + logicalScissor.width) * scaleX;
  const float bottom = static_cast<float>(logicalScissor.y + logicalScissor.height) * scaleY;

  // Every edge rounds the same way, so rects that share a logical edge share a target one. Flooring
  // the left and ceiling the right overlapped them by a pixel at non-integer scales, and a model
  // drawn in abutting scissored slices (the widescreen HUD warp) blended that column twice.
  const auto mappedLeft = std::clamp(static_cast<int32_t>(std::lround(left)), 0, static_cast<int32_t>(targetWidth));
  const auto mappedTop = std::clamp(static_cast<int32_t>(std::lround(top)), 0, static_cast<int32_t>(targetHeight));
  const auto mappedRight =
      std::clamp(static_cast<int32_t>(std::lround(right)), mappedLeft, static_cast<int32_t>(targetWidth));
  const auto mappedBottom =
      std::clamp(static_cast<int32_t>(std::lround(bottom)), mappedTop, static_cast<int32_t>(targetHeight));

  return {
      .x = mappedLeft,
      .y = mappedTop,
      .width = mappedRight - mappedLeft,
      .height = mappedBottom - mappedTop,
  };
}

void set_logical_viewport(const gfx::Viewport& viewport) noexcept {
  if (viewport.left != g_gxState.logicalViewport.left || viewport.width != g_gxState.logicalViewport.width ||
      viewport.height != g_gxState.logicalViewport.height) {
    g_gxState.dirty |= DirtyUniform;
  }
  g_gxState.logicalViewport = viewport;
  set_render_viewport(map_logical_viewport(viewport));
}

void set_render_viewport(const gfx::Viewport& viewport) noexcept {
  if (viewport.left != g_gxState.renderViewport.left || viewport.width != g_gxState.renderViewport.width ||
      viewport.height != g_gxState.renderViewport.height) {
    g_gxState.dirty |= DirtyUniform;
  }
  g_gxState.renderViewport = viewport;
  gfx::set_viewport(viewport);
}

void set_logical_scissor(const gfx::ClipRect& scissor) noexcept {
  g_gxState.logicalScissor = scissor;
  set_render_scissor(map_logical_scissor(g_gxState.logicalScissor));
}

void set_render_scissor(const gfx::ClipRect& scissor) noexcept {
  g_gxState.renderScissor = scissor;
  gfx::set_scissor(scissor);
}

void set_draw_sync_token(u16 token) noexcept { sDrawSyncToken.store(token, std::memory_order_release); }

u16 draw_sync_token() noexcept { return sDrawSyncToken.load(std::memory_order_acquire); }

const gfx::TextureBind& get_texture(GXTexMapID id) noexcept { return g_gxState.textures[static_cast<size_t>(id)]; }

DstAlphaMode dst_alpha_mode(const PipelineConfig& config) noexcept {
  // A zero constant times any source alpha is still zero.
  if (config.shadowPass != 0 || !config.alphaUpdate || config.dstAlpha == UINT32_MAX || config.dstAlpha == 0) {
    return DstAlphaMode::None;
  }
  // The draw-id and actor-decal shaders end in their own return (alpha 1).
  const auto& sc = config.shaderConfig;
  if (sc.drawId || (sc.actorDecal != 0 && sc.lineMode == 0)) {
    return DstAlphaMode::None;
  }
  const auto usesSourceAlpha = [](GXBlendFactor factor) {
    return factor == GX_BL_SRCALPHA || factor == GX_BL_INVSRCALPHA;
  };
  const bool needsSourceAlpha = config.colorUpdate && config.blendMode == GX_BM_BLEND &&
                                (usesSourceAlpha(config.blendFacSrc) || usesSourceAlpha(config.blendFacDst));
  if (!needsSourceAlpha) {
    return DstAlphaMode::Replace;
  }
  return webgpu::g_dualSourceBlendingSupported ? DstAlphaMode::DualSource : DstAlphaMode::None;
}

wgpu::RenderPipeline build_pipeline(const PipelineConfig& config, ArrayRef<wgpu::VertexBufferLayout> vtxBuffers,
                                    wgpu::ShaderModule shader, const char* label, DstAlphaMode dstAlphaMode) noexcept {
  ZoneScoped;
  if (config.shadowPass != 0) {
    // The sun's shadow map: depth only, culled as the draw is, with Remastered's shadow pass bias
    // (2 units, slope 4, no clamp).
    const wgpu::DepthStencilState depthStencil{
        .format = gfx::shadow::MapFormat,
        .depthWriteEnabled = true,
        .depthCompare = wgpu::CompareFunction::Less,
        .depthBias = 2,
        .depthBiasSlopeScale = 4.0f,
    };
    // Pass 2 alpha-tests (a Remastered cutout): its fragment stage samples the draw's own textures, so it
    // takes the colour pass's plain texture layout (never the receiver one, which holds this map) and has no colour target.
    const bool cut = config.shadowPass == 2;
    const wgpu::FragmentState cutFragment{
        .module = shader,
        .entryPoint = "fs_shadow",
        .targetCount = 0,
    };
    const wgpu::RenderPipelineDescriptor descriptor{
        .label = label,
        .layout = cut ? sPipelineLayout : sShadowPipelineLayout,
        .vertex =
            {
                .module = shader,
                .entryPoint = cut ? "vs_shadow_cut" : "vs_shadow",
                .bufferCount = static_cast<uint32_t>(vtxBuffers.size()),
                .buffers = vtxBuffers.data(),
            },
        .primitive = to_primitive_state(config.cullMode),
        .depthStencil = &depthStencil,
        .fragment = cut ? &cutFragment : nullptr,
    };
    return g_device.CreateRenderPipeline(&descriptor);
  }
  const float depthBias = (UseReversedZ ? -1.0f : 1.0f) * std::bit_cast<float>(config.polygonOffsetBits);
  const float depthBiasSlopeScale = (UseReversedZ ? -1.0f : 1.0f) * std::bit_cast<float>(config.polygonOffsetScaleBits);
  const float depthBiasClamp = webgpu::g_hasCoreFeatures ? std::bit_cast<float>(config.polygonOffsetClampBits) : 0.0f;
  const wgpu::DepthStencilState depthStencil{
      .format = g_graphicsConfig.depthFormat,
      .depthWriteEnabled = config.depthCompare && config.depthUpdate,
      .depthCompare = config.depthCompare ? to_compare_function(config.depthFunc) : wgpu::CompareFunction::Always,
      .depthBias = round_away_from_zero<int32_t>(depthBias),
      .depthBiasSlopeScale = depthBiasSlopeScale,
      .depthBiasClamp = depthBiasClamp,
  };
  const auto blendState =
      to_blend_state(config.blendMode, config.blendFacSrc, config.blendFacDst, config.blendOp, config.dstAlpha,
                     dstAlphaMode == DstAlphaMode::DualSource);
  const std::array colorTargets{wgpu::ColorTargetState{
      // The HDR scene's RGBA16F target is a different format from every other pass's.
      .format = config.shaderConfig.sceneHdr ? webgpu::SceneHdrFormat : g_graphicsConfig.surfaceConfiguration.format,
      .blend = &blendState,
      .writeMask = to_write_mask(config.colorUpdate, config.alphaUpdate),
  }};
  const wgpu::FragmentState fragmentState{
      .module = shader,
      .entryPoint = "fs_main",
      .targetCount = colorTargets.size(),
      .targets = colorTargets.data(),
  };
  const wgpu::RenderPipelineDescriptor descriptor{
      .label = label,
      .layout = shadow_receives(config.shaderConfig) ? sShadowRecvPipelineLayout : sPipelineLayout,
      .vertex =
          {
              .module = shader,
              .entryPoint = "vs_main",
              .bufferCount = static_cast<uint32_t>(vtxBuffers.size()),
              .buffers = vtxBuffers.data(),
          },
      .primitive = to_primitive_state(config.cullMode),
      .depthStencil = &depthStencil,
      .multisample =
          wgpu::MultisampleState{
              .count = config.msaaSamples,
          },
      .fragment = &fragmentState,
  };
  return g_device.CreateRenderPipeline(&descriptor);
}

// How a draw after the volumetric fog fogs itself. Remastered fogs its transparents per vertex,
// over a frame the full-screen pass has already fogged: an alpha-blended surface as colour T +
// in-scatter, an additive one as colour T, so it adds no in-scatter of its own. Blends that
// multiply or subtract the frame are left alone: fogging them would fog what is behind twice.
// Particles are the exception to the additive rule: Remastered's particle renderers inject only
// the colour T + in-scatter block, and an additive or premultiplied particle (blend mode 2 or 1)
// carries the static render state's "no fog" flag, so it is not fogged at all. So does a
// particle with DFOG 1, whatever its blend (CreateGPSM/CreateSWSH set the flag from it).
static u8 vol_fog_mode(bool depthOnly) noexcept {
  if (!g_gxState.volFog || depthOnly || !g_gxState.colorUpdate ||
      g_gxState.particleFog == GXState::kParticleUnfogged) {
    return VolFogNone;
  }
  const auto src = g_gxState.blendFacSrc;
  const auto dst = g_gxState.blendFacDst;
  if (g_gxState.blendMode == GX_BM_NONE ||
      (g_gxState.blendMode == GX_BM_BLEND && src == GX_BL_ONE && dst == GX_BL_ZERO)) {
    return VolFogOpaque;
  }
  if (g_gxState.blendMode != GX_BM_BLEND) {
    return VolFogNone;
  }
  const bool srcScales = src == GX_BL_ONE || src == GX_BL_SRCALPHA || src == GX_BL_INVSRCALPHA;
  if (srcScales && dst == GX_BL_ONE) {
    return g_gxState.particleFog ? VolFogNone : VolFogAdditive;
  }
  if (src == GX_BL_SRCALPHA && dst == GX_BL_INVSRCALPHA) {
    return VolFogBlended;
  }
  if (src == GX_BL_ONE && dst == GX_BL_INVSRCALPHA) {
    return g_gxState.particleFog ? VolFogNone : VolFogPremultiplied;
  }
  return VolFogNone;
}

void populate_pipeline_config(PipelineConfig& config, GXPrimitive primitive, GXVtxFmt fmt) noexcept {
  ZoneScoped;

  const auto& vtxFmt = g_gxState.vtxFmts[fmt];
  config.shaderConfig = {};
  // From the pass being recorded (the replayed FIFO's), so an offscreen pass or the next frame's
  // LDR scene never takes an HDR pipeline.
  config.shaderConfig.sceneHdr = gfx::scene_hdr_pass_active();
  config.shaderConfig.fogType = pipeline_fog_type(g_gxState.fog.type);
  config.shaderConfig.fogRangeEnabled = g_gxState.fog.rangeEnabled;
  config.shaderConfig.pbr = g_gxState.pbr;
  if (g_gxState.pbr != 0) {
    config.shaderConfig.pbrKind = static_cast<u8>(std::clamp(std::lround(g_gxState.pbrLayer.y()), 0L, 255L));
    // The lightmap's UV attribute, when the bound vertex format has it (and the device a slot for the lightmap).
    const u8 lightmapAttr = g_gxState.pbrLightmapAttr;
    if (webgpu::g_lightmapBinding && lightmapAttr >= GX_VA_TEX0 && lightmapAttr <= GX_VA_TEX7 &&
        g_gxState.vtxDesc[lightmapAttr] != GX_NONE) {
      config.shaderConfig.pbrLightmapAttr = lightmapAttr;
    }
  }
  if (bind_pos_active()) {
    config.shaderConfig.pbrBindPos = true;
    config.shaderConfig.pbrBindLe = g_gxState.arrays[GX_VA_TEX7].le;
  }
  config.shaderConfig.sdf = g_gxState.sdf;
  config.shaderConfig.hudSample = g_gxState.hudSample;
  u8 vtxOffset = 0;
  for (int i = GX_VA_PNMTXIDX; i <= GX_VA_TEX7; ++i) {
    const auto attr = static_cast<GXAttr>(i);
    const auto type = g_gxState.vtxDesc[i];
    auto& mapping = config.shaderConfig.attrs[i];
    if (type == GX_NONE) {
      mapping = {};
      continue;
    }
    const auto& attrFmt = vtxFmt.attrs[i];
    const auto cnt = comp_cnt_count(attr, attrFmt.cnt);
    const bool nbt3 = attr == GX_VA_NRM && attrFmt.cnt == GX_NRM_NBT3;
    mapping = AttrConfig{
        .attrType = static_cast<u8>(type),
        .cnt = cnt,
        .compType = static_cast<u8>(attrFmt.type),
        .offset = vtxOffset,
        .stride = 0,
        .frac = attrFmt.frac,
        .le = false,
        .nbt3 = nbt3,
    };
    switch (type) {
    case GX_DIRECT: {
      vtxOffset += comp_type_size(attr, attrFmt.type) * cnt;
      break;
    }
    case GX_INDEX8:
      mapping.stride = g_gxState.arrays[i].stride;
      mapping.le = g_gxState.arrays[i].le;
      vtxOffset += nbt3 ? 3 : 1;
      break;
    case GX_INDEX16:
      mapping.stride = g_gxState.arrays[i].stride;
      mapping.le = g_gxState.arrays[i].le;
      vtxOffset += nbt3 ? 6 : 2;
      break;
    default:
      Log.fatal("populate_pipeline_config: Invalid vertex type {}", type);
    }
  }
  config.shaderConfig.vtxStride = vtxOffset;
  if (primitive == GX_LINES) {
    config.shaderConfig.lineMode = 1;
  } else if (primitive == GX_LINESTRIP) {
    config.shaderConfig.lineMode = 2;
  } else if (primitive == GX_POINTS) {
    config.shaderConfig.lineMode = 3;
  } else {
    config.shaderConfig.lineMode = 0;
  }
  config.shaderConfig.tevSwapTable = g_gxState.tevSwapTable;
  for (u8 i = 0; i < g_gxState.numTevStages; ++i) {
    config.shaderConfig.tevStages[i] = g_gxState.tevStages[i];
  }
  config.shaderConfig.tevStageCount = g_gxState.numTevStages;
  for (u8 i = 0; i < g_gxState.numIndStages; ++i) {
    config.shaderConfig.indStages[i] = g_gxState.indStages[i];
  }
  config.shaderConfig.numIndStages = g_gxState.numIndStages;
  for (u8 i = 0; i < MaxColorChannels; ++i) {
    const auto& cc = g_gxState.colorChannelConfig[i];
    if (cc.lightingEnabled) {
      config.shaderConfig.colorChannels[i] = cc;
    } else {
      // Only matSrc matters when lighting disabled
      config.shaderConfig.colorChannels[i] = {
          .matSrc = cc.matSrc,
      };
    }
  }
  config.shaderConfig.perPixelLight = wants_per_pixel_light(config.shaderConfig);
  for (u8 i = 0; i < g_gxState.numTexGens; ++i) {
    config.shaderConfig.tcgs[i] = g_gxState.tcgs[i];
  }
  if (g_gxState.alphaCompare) {
    config.shaderConfig.alphaCompare = g_gxState.alphaCompare;
  }
  const auto cullMode = config.shaderConfig.lineMode == 0 ? g_gxState.cullMode : GX_CULL_NONE;
  const auto [polygonOffset, polygonOffsetScale] = polygon_offset_for_cull_mode(cullMode);
  // GX_AURORA_PORT_DEPTH_PREPASS: pass 1 writes only the depth, pass 2 shades where it is equal.
  // Both keep the vertex stage and the polygon offset, so their depths match exactly (the
  // position is @invariant).
  const bool writesDepth = g_gxState.depthCompare && g_gxState.depthUpdate;
  const bool depthOnly = g_gxState.depthPrepass == 1;
  const bool depthEqual = g_gxState.depthPrepass == 2 && writesDepth;
  config.shaderConfig.depthOnly = depthOnly;
  // The "drawid" view: flat colours that must reach the frame as they are, so no fog and no blend (a
  // depth-only pass stays one).
  const bool drawId = g_gxState.drawIdMode && !depthOnly;
  config.shaderConfig.drawId = drawId;
  if (drawId) {
    config.shaderConfig.fogType = GX_FOG_NONE;
    config.shaderConfig.fogRangeEnabled = false;
  }
  config.shaderConfig.volFog = drawId ? VolFogNone : vol_fog_mode(depthOnly);
  config.shaderConfig.shadow = g_gxState.shadowCaster && g_gxState.shadowActive && config.shaderConfig.lineMode == 0;
  if (g_gxState.actorDecal && config.shaderConfig.shadow && !drawId && !depthOnly) {
    // Remastered's actor shadow decal: the receiver's geometry multiplies the frame by the shadow
    // alone, so none of the draw's own shading, fog or PBR reaches the shader.
    auto& sc = config.shaderConfig;
    sc.actorDecal = 1;
    sc.pbr = 0;
    sc.pbrKind = 0;
    sc.pbrBindPos = false;
    sc.pbrBindLe = false;
    sc.pbrLightmapAttr = GX_VA_NULL;
    sc.sdf = 0;
    sc.hudSample = 0;
    sc.perPixelLight = 0;
    sc.fogType = GX_FOG_NONE;
    sc.fogRangeEnabled = false;
    sc.volFog = VolFogNone;
    sc.alphaCompare = {};
    sc.numIndStages = 0;
    sc.indStages = {};
    for (auto& ts : sc.tevStages) {
      ts = {};
    }
    sc.tevStageCount = 1;
  } else if (g_gxState.actorDecal) {
    config.shaderConfig.shadow = false;
  }
  // Field by field: an aggregate assignment would copy the 2.7 KB shader config twice more on
  // every pipeline switch. Every field outside shaderConfig is written here.
  config.version = GXPipelineConfigVersion;
  config.msaaSamples = gfx::get_sample_count();
  config.depthFunc = depthEqual ? GX_EQUAL : g_gxState.depthFunc;
  config.cullMode = cullMode;
  config.blendMode = drawId ? GX_BM_NONE : g_gxState.blendMode;
  // An EFB without alpha: its alpha reads as 1 and is never written (nor is GXSetDstAlpha's).
  const bool hasAlpha = efb_has_alpha(g_gxState.pixelFmt);
  const auto blendFactor = [hasAlpha](GXBlendFactor factor) {
    if (!hasAlpha && factor == GX_BL_DSTALPHA) {
      return GX_BL_ONE;
    }
    if (!hasAlpha && factor == GX_BL_INVDSTALPHA) {
      return GX_BL_ZERO;
    }
    return factor;
  };
  config.blendFacSrc = blendFactor(g_gxState.blendFacSrc);
  config.blendFacDst = blendFactor(g_gxState.blendFacDst);
  config.blendOp = g_gxState.blendOp;
  config.dstAlpha = hasAlpha && g_gxState.alphaUpdate ? g_gxState.dstAlpha : UINT32_MAX;
  config.polygonOffsetBits = std::bit_cast<uint32_t>(polygonOffset);
  config.polygonOffsetScaleBits = std::bit_cast<uint32_t>(polygonOffsetScale);
  config.polygonOffsetClampBits = std::bit_cast<uint32_t>(g_gxState.clamp);
  config.shadowPass = 0;
  config.depthCompare = g_gxState.depthCompare;
  config.depthUpdate = g_gxState.depthUpdate && !depthEqual;
  if (config.shaderConfig.actorDecal != 0) {
    // Framebuffer *= the decal (DstColor, Zero), LEQUAL against the world's depth without writing,
    // back faces culled; the pipeline state of Remastered's actor shadow draw.
    config.blendMode = GX_BM_BLEND;
    config.blendFacSrc = GX_BL_SRCCLR;
    config.blendFacDst = GX_BL_ZERO;
    config.depthFunc = GX_LEQUAL;
    config.depthCompare = true;
    config.depthUpdate = false;
    // The receiver keeps its own cull (back faces for the world's geometry), as Remastered's
    // decal draw does.
  }
  config.alphaUpdate = hasAlpha && g_gxState.alphaUpdate && !depthOnly;
  config.colorUpdate = g_gxState.colorUpdate && !depthOnly;
  static_assert(sizeof(PipelineConfig) == sizeof(ShaderConfig) + 13 * sizeof(uint32_t) + 4 * sizeof(bool),
                "a new PipelineConfig field must be written above");
}

// The lightmap's slot is the last binding, left out of group 2 when the device has no room for it.
static size_t texture_binding_count() noexcept { return kTextureBindings - (webgpu::g_lightmapBinding ? 0 : 1); }

GXBindGroups build_bind_groups(const ShaderInfo& info) noexcept {
  ZoneScoped;

  if (!info.sampledTextures.any() && !info.sampledIndTextures.any() && !info.usesVolFog && !info.shadowReceive &&
      !info.usesLightmap) {
    // Don't bother re-binding anything
    return {};
  }

  // Using C WGPU types instead of C++ wrappers to avoid destructor overhead
  std::array<WGPUBindGroupEntry, kTextureBindings> textureEntries{};
  textureEntries[MaxTextures * 2].binding = MaxTextures * 2;
  textureEntries[MaxTextures * 2].textureView = gfx::probe::cube_view(g_gxState.pbrCube).Get();
  textureEntries[MaxTextures * 2 + 1].binding = MaxTextures * 2 + 1;
  textureEntries[MaxTextures * 2 + 1].sampler = gfx::probe::sampler().Get();
  for (u32 i = 0; i < gfx::probe::VolumeTextures; ++i) {
    textureEntries[MaxTextures * 2 + 2 + i].binding = MaxTextures * 2 + 2 + i;
    textureEntries[MaxTextures * 2 + 2 + i].textureView = gfx::probe::volume_view(g_gxState.pbrVolume, i).Get();
  }
  textureEntries[kLightmapBinding].binding = kLightmapBinding;
  textureEntries[kLightmapBinding].textureView = gfx::probe::lightmap_view(g_gxState.pbrLightmap).Get();
  textureEntries[kBrdfLutBinding].binding = kBrdfLutBinding;
  textureEntries[kBrdfLutBinding].textureView = gfx::probe::brdf_lut_view().Get();
  textureEntries[kVolFogFroxelBinding].binding = kVolFogFroxelBinding;
  textureEntries[kVolFogFroxelBinding].textureView = gfx::volfog::froxel_view().Get();
  textureEntries[kVolFogSamplerBinding].binding = kVolFogSamplerBinding;
  textureEntries[kVolFogSamplerBinding].sampler = gfx::volfog::sampler().Get();
  if (info.shadowReceive && info.actorDecal) {
    textureEntries[kShadowMapBinding].textureView = gfx::actor_shadow::map_view().Get();
    textureEntries[kShadowSamplerBinding].sampler = gfx::actor_shadow::sampler().Get();
  } else if (info.shadowReceive) {
    textureEntries[kShadowMapBinding].textureView = gfx::shadow::map_view().Get();
    textureEntries[kShadowSamplerBinding].sampler = gfx::shadow::sampler().Get();
  }
  for (u32 i = 0; i < MaxTextures; ++i) {
    const auto& tex = g_gxState.textures[i];
    WGPUBindGroupEntry& textureEntry = textureEntries[i * 2];
    WGPUBindGroupEntry& samplerEntry = textureEntries[i * 2 + 1];
    textureEntry.binding = i * 2;
    samplerEntry.binding = i * 2 + 1;
    if (tex && (info.sampledTextures[i] || info.sampledIndTextures[i])) {
      textureEntry.textureView = tex.ref->sampleTextureView.Get();
      auto samplerDescriptor = tex.get_descriptor();
      // A mod's native maps under PBR tile many times over a floor. Anisotropic filtering keeps
      // them sharp across the view and averages them along it, so each screen column shows the
      // mean of one line through the repeated tile: a fan of streaks towards the horizon that
      // slides as the camera turns. Seen from 4x up, in every map, so they all take the limit.
      static const int pbrAniso = getenv("MP2_PBR_ANISO") ? std::clamp(atoi(getenv("MP2_PBR_ANISO")), 1, 16) : 2;
      if (g_gxState.pbr && tex.ref->isReplacement && samplerDescriptor.maxAnisotropy > pbrAniso) {
        samplerDescriptor.maxAnisotropy = pbrAniso;
      }
      // Cost tests 8 and 9 (GXSetPBRCostTest) shade in full with cheaper filtering of the
      // mod's maps: 8 without anisotropy, 9 also without blending between mips.
      const u32 costTest = g_gxState.pbr ? g_gxState.pbr - 1u : 0u;
      if (tex.ref->isReplacement && (costTest == 8 || costTest == 9)) {
        samplerDescriptor.maxAnisotropy = 1;
        if (costTest == 9) {
          samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
        }
      }
      // Each slot keeps the sampler it took last: slots mostly see the same sampler state, and the cache
      // never drops a sampler before shutdown, so this saves the hash and the lock.
      const auto* samplerKey = reinterpret_cast<const u8*>(&samplerDescriptor) + kSamplerKeyOffset;
      if (!sSlotSamplers[i] || std::memcmp(sSlotSamplerKeys[i].data(), samplerKey, kSamplerKeySize) != 0) {
        sSlotSamplers[i] = gfx::sampler_ref(samplerDescriptor);
        std::memcpy(sSlotSamplerKeys[i].data(), samplerKey, kSamplerKeySize);
      }
      samplerEntry.sampler = sSlotSamplers[i].Get();
    } else {
      textureEntry.textureView = sEmptyTextureView.Get();
      samplerEntry.sampler = sEmptySampler.Get();
    }
  }
  const WGPUBindGroupDescriptor textureBindGroupDescriptor{
      .label = {"GX Texture Bind Group", WGPU_STRLEN},
      .layout = info.shadowReceive ? sShadowTextureBindGroupLayout.Get() : sTextureBindGroupLayout.Get(),
      .entryCount = texture_binding_count(),
      .entries = textureEntries.data(),
  };
  // Every entry holds only a texture view or a sampler at a fixed binding, so those handles and the layout
  // identify the bind group; hashing them is far cheaper than hashing the whole entries.
  std::array<const void*, kTextureBindings + 1> key{};
  key[0] = textureBindGroupDescriptor.layout;
  for (size_t i = 0; i < textureBindGroupDescriptor.entryCount; ++i) {
    key[i + 1] = textureEntries[i].textureView ? static_cast<const void*>(textureEntries[i].textureView)
                                               : static_cast<const void*>(textureEntries[i].sampler);
  }
  return {
      .textureBindGroup = gfx::bind_group_ref(xxh3_hash(key), textureBindGroupDescriptor),
  };
}

void initialize() noexcept {
  {
    std::array<wgpu::BindGroupLayoutEntry, kTextureBindings> textureEntries;
    // The PBR environment probe (GX_AURORA_COPY_PROBE_FACE)
    textureEntries[MaxTextures * 2] = {
        .binding = MaxTextures * 2,
        .visibility = wgpu::ShaderStage::Fragment,
        .texture =
            {
                .sampleType = wgpu::TextureSampleType::Float,
                .viewDimension = wgpu::TextureViewDimension::Cube,
            },
    };
    textureEntries[MaxTextures * 2 + 1] = {
        .binding = MaxTextures * 2 + 1,
        .visibility = wgpu::ShaderStage::Fragment,
        .sampler = {.type = wgpu::SamplerBindingType::Filtering},
    };
    // The PBR ambient volume (GX_AURORA_SET_PBR_VOLUME), which shares the probe's sampler
    for (u32 i = 0; i < gfx::probe::VolumeTextures; ++i) {
      textureEntries[MaxTextures * 2 + 2 + i] = {
          .binding = MaxTextures * 2 + 2 + i,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              {
                  .sampleType = wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e3D,
              },
      };
    }
    // The baked lightmap (GX_AURORA_SET_PBR_LIGHTMAP), the 17th sampled texture: only on a device that has it
    textureEntries[kLightmapBinding] = {
        .binding = kLightmapBinding,
        .visibility = wgpu::ShaderStage::Fragment,
        .texture =
            {
                .sampleType = wgpu::TextureSampleType::Float,
                .viewDimension = wgpu::TextureViewDimension::e2DArray,
            },
    };
    // The PBR environment BRDF table (GX_AURORA_SET_PBR_BRDF_LUT), also read with the probe's sampler
    textureEntries[kBrdfLutBinding] = {
        .binding = kBrdfLutBinding,
        .visibility = wgpu::ShaderStage::Fragment,
        .texture =
            {
                .sampleType = wgpu::TextureSampleType::Float,
                .viewDimension = wgpu::TextureViewDimension::e2D,
            },
    };
    // The volumetric fog's froxels (GX_AURORA_PORT_VOLUMETRIC_FOG), read per vertex or per pixel
    textureEntries[kVolFogFroxelBinding] = {
        .binding = kVolFogFroxelBinding,
        .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
        .texture =
            {
                .sampleType = wgpu::TextureSampleType::Float,
                .viewDimension = wgpu::TextureViewDimension::e3D,
            },
    };
    textureEntries[kVolFogSamplerBinding] = {
        .binding = kVolFogSamplerBinding,
        .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
        .sampler = {.type = wgpu::SamplerBindingType::Filtering},
    };
    for (u32 i = 0; i < MaxTextures; ++i) {
      textureEntries[i * 2] = {
          .binding = i * 2,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              {
                  .sampleType = wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      };
      textureEntries[i * 2 + 1] = {
          .binding = i * 2 + 1,
          .visibility = wgpu::ShaderStage::Fragment,
          .sampler = {.type = wgpu::SamplerBindingType::Filtering},
      };
    }
    const wgpu::BindGroupLayoutDescriptor descriptor{
        .label = "GX Texture Bind Group Layout",
        .entryCount = texture_binding_count(),
        .entries = textureEntries.data(),
    };
    sTextureBindGroupLayout = g_device.CreateBindGroupLayout(&descriptor);
    // A shadow receiver's: the sun's shadow map and its comparison sampler in the froxel's slots
    textureEntries[kShadowMapBinding] = {
        .binding = kShadowMapBinding,
        .visibility = wgpu::ShaderStage::Fragment,
        .texture =
            {
                .sampleType = wgpu::TextureSampleType::Depth,
                .viewDimension = wgpu::TextureViewDimension::e2DArray,
            },
    };
    textureEntries[kShadowSamplerBinding] = {
        .binding = kShadowSamplerBinding,
        .visibility = wgpu::ShaderStage::Fragment,
        .sampler = {.type = wgpu::SamplerBindingType::Comparison},
    };
    const wgpu::BindGroupLayoutDescriptor shadowDescriptor{
        .label = "GX Shadow Receiver Texture Bind Group Layout",
        .entryCount = texture_binding_count(),
        .entries = textureEntries.data(),
    };
    sShadowTextureBindGroupLayout = g_device.CreateBindGroupLayout(&shadowDescriptor);
  }
  {
    constexpr wgpu::SamplerDescriptor descriptor{.label = "Empty sampler"};
    sEmptySampler = gfx::sampler_ref(descriptor);
  }
  {
    constexpr wgpu::TextureDescriptor descriptor{
        .label = "Empty texture",
        .usage = wgpu::TextureUsage::TextureBinding,
        .size = {1, 1},
        .format = wgpu::TextureFormat::RGBA8Unorm,
    };
    AURORA_NOTE_TEXTURE(&descriptor);
    sEmptyTexture = g_device.CreateTexture(&descriptor);
    sEmptyTextureView = sEmptyTexture.CreateView();
  }
  {
    std::array<wgpu::BindGroupEntry, kTextureBindings> entries;
    entries[MaxTextures * 2] = {
        .binding = MaxTextures * 2,
        .textureView = gfx::probe::cube_view(),
    };
    entries[MaxTextures * 2 + 1] = {
        .binding = MaxTextures * 2 + 1,
        .sampler = gfx::probe::sampler(),
    };
    for (u32 i = 0; i < gfx::probe::VolumeTextures; ++i) {
      entries[MaxTextures * 2 + 2 + i] = {
          .binding = MaxTextures * 2 + 2 + i,
          .textureView = gfx::probe::volume_view(0, i),
      };
    }
    entries[kLightmapBinding] = {
        .binding = kLightmapBinding,
        .textureView = gfx::probe::lightmap_view(0),
    };
    entries[kBrdfLutBinding] = {
        .binding = kBrdfLutBinding,
        .textureView = gfx::probe::brdf_lut_view(),
    };
    entries[kVolFogFroxelBinding] = {
        .binding = kVolFogFroxelBinding,
        .textureView = gfx::volfog::froxel_view(),
    };
    entries[kVolFogSamplerBinding] = {
        .binding = kVolFogSamplerBinding,
        .sampler = gfx::volfog::sampler(),
    };
    for (u32 i = 0; i < MaxTextures; ++i) {
      entries[i * 2] = {
          .binding = i * 2,
          .textureView = sEmptyTextureView,
      };
      entries[i * 2 + 1] = {
          .binding = i * 2 + 1,
          .sampler = sEmptySampler,
      };
    }
    const wgpu::BindGroupDescriptor desc{
        .label = "GX Empty Texture Bind Group",
        .layout = sTextureBindGroupLayout,
        .entryCount = texture_binding_count(),
        .entries = entries.data(),
    };
    g_emptyTextureBindGroup = g_device.CreateBindGroup(&desc);
  }
  {
    const std::array layouts{
        gfx::detail::resources().staticBindGroupLayout,
        gfx::detail::resources().uniformBindGroupLayout,
        sTextureBindGroupLayout,
    };
    const wgpu::PipelineLayoutDescriptor desc{
        .label = "GX Pipeline Layout",
        .bindGroupLayoutCount = layouts.size(),
        .bindGroupLayouts = layouts.data(),
        .immediateSize = sizeof(DrawImmediateData),
    };
    sPipelineLayout = g_device.CreatePipelineLayout(&desc);
  }
  {
    const std::array layouts{
        gfx::detail::resources().staticBindGroupLayout,
        gfx::detail::resources().uniformBindGroupLayout,
        sShadowTextureBindGroupLayout,
    };
    const wgpu::PipelineLayoutDescriptor desc{
        .label = "GX Shadow Receiver Pipeline Layout",
        .bindGroupLayoutCount = layouts.size(),
        .bindGroupLayouts = layouts.data(),
        .immediateSize = sizeof(DrawImmediateData),
    };
    sShadowRecvPipelineLayout = g_device.CreatePipelineLayout(&desc);
  }
  {
    const std::array layouts{
        gfx::detail::resources().staticBindGroupLayout,
        gfx::detail::resources().uniformBindGroupLayout,
    };
    const wgpu::PipelineLayoutDescriptor desc{
        .label = "GX Shadow Map Pipeline Layout",
        .bindGroupLayoutCount = layouts.size(),
        .bindGroupLayouts = layouts.data(),
        .immediateSize = sizeof(DrawImmediateData),
    };
    sShadowPipelineLayout = g_device.CreatePipelineLayout(&desc);
  }
}

void shutdown() noexcept {
  // TODO we should probably store this all in g_state.gx instead
  sSamplerBindGroupLayout = {};
  sTextureBindGroupLayout = {};
  sShadowTextureBindGroupLayout = {};
  sShadowRecvPipelineLayout = {};
  sShadowPipelineLayout = {};
  sSlotSamplers.fill({});
  {
    std::lock_guard lock{sBindGroupLayoutMutex};
    sUniformBindGroupLayouts.clear();
    sTextureBindGroupLayouts.clear();
  }
  for (auto& item : g_gxState.textures) {
    item.ref.reset();
  }
  g_gxState.loadedTextures.fill({});
  g_gxState.loadedTluts.fill({});
  clear_copy_texture_cache();
  g_gxState.copySceneLinear = false;
  texture::shutdown();
}
} // namespace aurora::gx
