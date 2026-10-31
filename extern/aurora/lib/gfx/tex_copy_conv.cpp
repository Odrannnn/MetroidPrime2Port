#include "tex_copy_conv.hpp"

#include "resources.hpp"

#include "../internal.hpp"
#include "../gx/gx.hpp"
#include "../webgpu/gpu.hpp"
#include "../webgpu/gpu_prof.hpp"
#include "texture.hpp"
#include "../gx/gx_fmt.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include "texture_convert.hpp"
#include "hdr_tone.hpp"

#include <algorithm>
#include <mutex>
#include <tuple>

using namespace std::string_literals;

namespace aurora::gfx::tex_copy_conv {
static Module Log("aurora::gfx::tex_copy_conv");

using webgpu::g_device;

static constexpr std::string_view ShaderPreamble = R"(
@group(0) @binding(0) var src_samp: sampler;
@group(0) @binding(1) var src: texture_2d<f32>;

struct UVTransform {
    offset: vec2f,
    scale: vec2f,
    opaque_alpha: u32,
};
@group(0) @binding(2) var<uniform> uv_xf: UVTransform;

struct VertexOutput {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
};

var<private> positions: array<vec2f, 3> = array(
    vec2f(-1.0, 1.0),
    vec2f(-1.0, -3.0),
    vec2f(3.0, 1.0),
);
var<private> uvs: array<vec2f, 3> = array(
    vec2f(0.0, 0.0),
    vec2f(0.0, 2.0),
    vec2f(2.0, 0.0),
);

@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4f(positions[vi], 0.0, 1.0);
    out.uv = uvs[vi] * uv_xf.scale + uv_xf.offset;
    return out;
}

fn sample_efb(uv: vec2f) -> vec4f {
    let c = textureSample(src, src_samp, uv);
    return vec4f(c.rgb, select(c.a, 1.0, uv_xf.opaque_alpha != 0u));
}

fn intensity(rgb: vec3f) -> f32 {
    // ITU-R BT.601 luma coefficients
    return dot(rgb, vec3f(0.257, 0.504, 0.098)) + 16.0 / 255.0;
}

fn quantize4(v: f32) -> f32 {
    return floor(v * 16.0) / 15.0;
}
)"sv;

static const std::string DepthShaderPreamble = R"(
@group(0) @binding(0) var src: texture_depth_2d;

struct UVTransform {
    offset: vec2f,
    scale: vec2f,
};
@group(0) @binding(1) var<uniform> uv_xf: UVTransform;

struct VertexOutput {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
};

var<private> positions: array<vec2f, 3> = array(
    vec2f(-1.0, 1.0),
    vec2f(-1.0, -3.0),
    vec2f(3.0, 1.0),
);
var<private> uvs: array<vec2f, 3> = array(
    vec2f(0.0, 0.0),
    vec2f(0.0, 2.0),
    vec2f(2.0, 0.0),
);

@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4f(positions[vi], 0.0, 1.0);
    out.uv = uvs[vi] * uv_xf.scale + uv_xf.offset;
    return out;
}
)"s + (gx::UseReversedZ ? R"(
fn gx_z24(uv: vec2f) -> u32 {
    let texSize = vec2i(textureDimensions(src));
    let coord = clamp(vec2i(floor(uv * vec2f(texSize))), vec2i(0), texSize - vec2i(1));
    let depth = textureLoad(src, coord, 0);
    return min(u32(clamp(1.0 - depth, 0.0, 1.0) * 16777215.0 + 0.5), 0x00ffffffu);
}
)"s
                        : R"(
fn gx_z24(uv: vec2f) -> u32 {
    let texSize = vec2i(textureDimensions(src));
    let coord = clamp(vec2i(floor(uv * vec2f(texSize))), vec2i(0), texSize - vec2i(1));
    let depth = textureLoad(src, coord, 0);
    return min(u32(clamp(depth, 0.0, 1.0) * 16777215.0 + 0.5), 0x00ffffffu);
}
)"s);

// Passthrough blit (for scaling)
static constexpr std::string_view FragPassthrough = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return sample_efb(in.uv);
}
)"sv;

// GX_TF_I4: 4-bit intensity -> R8Unorm (quantized)
static constexpr std::string_view FragI4 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let rgb = sample_efb(in.uv).rgb;
    let i = quantize4(intensity(rgb));
    return vec4f(i, i, i, i);
}
)"sv;

// GX_TF_I8: 8-bit intensity -> R8Unorm
static constexpr std::string_view FragI8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let rgb = sample_efb(in.uv).rgb;
    let i = intensity(rgb);
    return vec4f(i, i, i, i);
}
)"sv;

// GX_TF_IA4: 4-bit intensity + 4-bit alpha -> RG8Unorm
static constexpr std::string_view FragIA4 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = sample_efb(in.uv);
    let i = quantize4(intensity(c.rgb));
    let a = quantize4(c.a);
    return vec4f(i, i, i, a);
}
)"sv;

// GX_TF_IA8: 8-bit intensity + 8-bit alpha -> RG8Unorm
static constexpr std::string_view FragIA8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = sample_efb(in.uv);
    let i = intensity(c.rgb);
    return vec4f(i, i, i, c.a);
}
)"sv;

// GX_TF_RGB565: Blit alpha to 1.0
static constexpr std::string_view FragRGB565 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = sample_efb(in.uv);
    return vec4f(c.rgb, 1.0);
}
)"sv;

// GX_CTF_R4: 4-bit red -> R8Unorm
static constexpr std::string_view FragR4 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let r = quantize4(sample_efb(in.uv).r);
    return vec4f(r, r, r, r);
}
)"sv;

// GX_CTF_RA4: 4-bit red + 4-bit alpha -> RG8Unorm
static constexpr std::string_view FragRA4 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = sample_efb(in.uv);
    let r = quantize4(c.r);
    return vec4f(r, r, r, quantize4(c.a));
}
)"sv;

// GX_CTF_RA8: 8-bit red + 8-bit alpha -> RG8Unorm
static constexpr std::string_view FragRA8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = sample_efb(in.uv);
    return vec4f(c.r, c.r, c.r, c.a);
}
)"sv;

// GX_CTF_A8: 8-bit alpha -> R8Unorm
static constexpr std::string_view FragA8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let a = sample_efb(in.uv).a;
    return vec4f(a, a, a, a);
}
)"sv;

// GX_CTF_R8: 8-bit red -> R8Unorm
static constexpr std::string_view FragR8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let r = sample_efb(in.uv).r;
    return vec4f(r, r, r, r);
}
)"sv;

// GX_CTF_G8: 8-bit green -> R8Unorm
static constexpr std::string_view FragG8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let g = sample_efb(in.uv).g;
    return vec4f(g, g, g, g);
}
)"sv;

// GX_CTF_B8: 8-bit blue -> R8Unorm
static constexpr std::string_view FragB8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let b = sample_efb(in.uv).b;
    return vec4f(b, b, b, b);
}
)"sv;

// GX_CTF_RG8: 8-bit red + 8-bit green -> RG8Unorm
static constexpr std::string_view FragRG8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = sample_efb(in.uv);
    return vec4f(c.r, c.r, c.r, c.g);
}
)"sv;

// GX_CTF_GB8: 8-bit green + 8-bit blue -> RG8Unorm
static constexpr std::string_view FragGB8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = sample_efb(in.uv);
    return vec4f(c.g, c.g, c.g, c.b);
}
)"sv;

// GX_TF_Z8: Upper 8-bits depth -> I8
static constexpr std::string_view FragZ8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let z8 = f32((gx_z24(in.uv) >> 16u) & 0xFFu) / 255.0;
    return vec4f(z8, z8, z8, z8);
}
)"sv;

// GX_TF_Z16: Upper 16-bits depth -> IA8
static constexpr std::string_view FragZ16 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let z16 = gx_z24(in.uv) >> 8u;
    let i = f32((z16 >> 8u) & 0xFFu) / 255.0;
    let a = f32(z16 & 0xFFu) / 255.0;
    return vec4f(i, i, i, a);
}
)"sv;

// Depth -> R32Float (no scaling)
static constexpr std::string_view DepthSnapshotShader = R"(
@group(0) @binding(0) var src: texture_depth_2d;

var<private> positions: array<vec2f, 3> = array(
    vec2f(-1.0, 1.0),
    vec2f(-1.0, -3.0),
    vec2f(3.0, 1.0),
);

@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
    return vec4f(positions[vi], 0.0, 1.0);
}

@fragment fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let depth = textureLoad(src, vec2i(pos.xy), 0);
    return vec4f(depth, 0.0, 0.0, 1.0);
}
)"sv;

static constexpr std::string_view DepthSnapshotShaderMS = R"(
@group(0) @binding(0) var src: texture_depth_multisampled_2d;

var<private> positions: array<vec2f, 3> = array(
    vec2f(-1.0, 1.0),
    vec2f(-1.0, -3.0),
    vec2f(3.0, 1.0),
);

@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
    return vec4f(positions[vi], 0.0, 1.0);
}

@fragment fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let depth = textureLoad(src, vec2i(pos.xy), 0);
    return vec4f(depth, 0.0, 0.0, 1.0);
}
)"sv;

struct ConvPipeline {
  GXTexFmt fmt;
  std::string_view fragShader;
  wgpu::TextureFormat outputFormat;
  const char* label;
};

static constexpr std::array ConvPipelines{
    ConvPipeline{GX_TF_I4, FragI4, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv I4"},
    ConvPipeline{GX_TF_I8, FragI8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv I8"},
    ConvPipeline{GX_TF_IA4, FragIA4, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv IA4"},
    ConvPipeline{GX_TF_IA8, FragIA8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv IA8"},
    ConvPipeline{GX_TF_RGB565, FragRGB565, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv RGB565"},
    ConvPipeline{GX_CTF_R4, FragR4, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv R4"},
    ConvPipeline{GX_CTF_RA4, FragRA4, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv RA4"},
    ConvPipeline{GX_CTF_RA8, FragRA8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv RA8"},
    ConvPipeline{GX_CTF_A8, FragA8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv A8"},
    ConvPipeline{GX_CTF_R8, FragR8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv R8"},
    ConvPipeline{GX_CTF_G8, FragG8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv G8"},
    ConvPipeline{GX_CTF_B8, FragB8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv B8"},
    ConvPipeline{GX_CTF_RG8, FragRG8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv RG8"},
    ConvPipeline{GX_CTF_GB8, FragGB8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv GB8"},
};

static constexpr std::array DepthConvPipelines{
    ConvPipeline{GX_TF_Z8, FragZ8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv Z8"},
    ConvPipeline{GX_TF_Z16, FragZ16, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv Z16"},
};

static wgpu::BindGroupLayout g_bindGroupLayout;
static wgpu::BindGroupLayout g_depthBindGroupLayout;
static wgpu::BindGroupLayout g_depthBindGroupLayoutMS;
static absl::flat_hash_map<GXTexFmt, wgpu::RenderPipeline> g_depthPipelinesMS; // Core devices only
// Port: index/mask copies (A8/R8/G8/B8) of a multisampled colour EFB read sample 0 of the original
// attachment, never the averaged resolve (an average of two palette ids is another id). Core only.
static wgpu::BindGroupLayout g_indexBindGroupLayoutMS;
static absl::flat_hash_map<GXTexFmt, wgpu::RenderPipeline> g_indexPipelinesMS;
static wgpu::Sampler g_nearestSampler;
static wgpu::Sampler g_linearSampler;
static absl::flat_hash_map<GXTexFmt, wgpu::RenderPipeline> g_pipelines;
static wgpu::RenderPipeline g_blitPipeline;
// CopyDomain::HdrDisplay / Linear pipelines, made on first use (an LDR run makes none), keyed by
// (format, domain, output format); GX_TF_RGBA8 stands for the passthrough blit.
static std::mutex g_domainMutex;
static wgpu::BindGroupLayout g_hdrBindGroupLayout;
static absl::flat_hash_map<std::tuple<GXTexFmt, CopyDomain, wgpu::TextureFormat>, wgpu::RenderPipeline>
    g_domainPipelines;
static wgpu::BindGroupLayout g_depthSnapshotBindGroupLayout;
static wgpu::BindGroupLayout g_depthSnapshotBindGroupLayoutMS;
static wgpu::RenderPipeline g_depthSnapshotPipeline;
static wgpu::RenderPipeline g_depthSnapshotPipelineMS;

static wgpu::BindGroupLayout create_depth_snapshot_layout(bool multisampled, const char* label) {
  const std::array entries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::Depth,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
                  .multisampled = multisampled,
              },
      },
  };
  const wgpu::BindGroupLayoutDescriptor descriptor{
      .label = label,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  return g_device.CreateBindGroupLayout(&descriptor);
}

static wgpu::RenderPipeline create_depth_snapshot_pipeline(const std::string_view shaderSource,
                                                           const wgpu::BindGroupLayout& bindGroupLayout,
                                                           const char* label) {
  const std::string source{shaderSource};
  const wgpu::ShaderSourceWGSL wgslSource{wgpu::ShaderSourceWGSL::Init{
      .code = source.c_str(),
  }};
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &wgslSource,
      .label = label,
  };
  const auto module = g_device.CreateShaderModule(&moduleDescriptor);

  const std::array colorTargets{wgpu::ColorTargetState{
      .format = wgpu::TextureFormat::R32Float,
  }};
  const wgpu::FragmentState fragmentState{
      .module = module,
      .entryPoint = "fs_main",
      .targetCount = colorTargets.size(),
      .targets = colorTargets.data(),
  };
  const wgpu::PipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &bindGroupLayout,
  };
  const auto pipelineLayout = g_device.CreatePipelineLayout(&layoutDescriptor);
  const wgpu::RenderPipelineDescriptor pipelineDescriptor{
      .label = label,
      .layout = pipelineLayout,
      .vertex =
          wgpu::VertexState{
              .module = module,
              .entryPoint = "vs_main",
          },
      .primitive =
          wgpu::PrimitiveState{
              .topology = wgpu::PrimitiveTopology::TriangleList,
          },
      .fragment = &fragmentState,
  };
  return g_device.CreateRenderPipeline(&pipelineDescriptor);
}

static wgpu::RenderPipeline create_pipeline(const ConvPipeline& conv, const std::string_view shaderPreamble,
                                            const wgpu::BindGroupLayout& bindGroupLayout) {
  std::string shaderSource;
  shaderSource.reserve(shaderPreamble.size() + conv.fragShader.size());
  shaderSource += shaderPreamble;
  shaderSource += conv.fragShader;

  const wgpu::ShaderSourceWGSL wgslSource{wgpu::ShaderSourceWGSL::Init{
      .code = shaderSource.c_str(),
  }};
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &wgslSource,
      .label = conv.label,
  };
  const auto module = g_device.CreateShaderModule(&moduleDescriptor);

  const std::array colorTargets{wgpu::ColorTargetState{
      .format = conv.outputFormat,
  }};
  const wgpu::FragmentState fragmentState{
      .module = module,
      .entryPoint = "fs_main",
      .targetCount = colorTargets.size(),
      .targets = colorTargets.data(),
  };

  const wgpu::PipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &bindGroupLayout,
  };
  const auto pipelineLayout = g_device.CreatePipelineLayout(&layoutDescriptor);

  const wgpu::RenderPipelineDescriptor pipelineDescriptor{
      .label = conv.label,
      .layout = pipelineLayout,
      .vertex =
          wgpu::VertexState{
              .module = module,
              .entryPoint = "vs_main",
          },
      .primitive =
          wgpu::PrimitiveState{
              .topology = wgpu::PrimitiveTopology::TriangleList,
          },
      .fragment = &fragmentState,
  };
  return g_device.CreateRenderPipeline(&pipelineDescriptor);
}

bool needs_conversion(const GXTexFmt fmt) { return g_pipelines.contains(fmt); }

bool index_copy_format(const GXTexFmt fmt) noexcept {
  return fmt == GX_CTF_A8 || fmt == GX_CTF_R8 || fmt == GX_CTF_G8 || fmt == GX_CTF_B8;
}

bool index_copy_ms_supported(const GXTexFmt fmt) noexcept { return g_indexPipelinesMS.contains(fmt); }

static constexpr std::string_view HdrCopyPreamble = R"(
@group(0) @binding(3) var<uniform> hdr_copy_tone: array<vec4f, 3>;

fn hdr_copy_display(c: vec4f) -> vec4f {
    return vec4f(hdr_post_tone(c.rgb, hdr_copy_tone[0], hdr_copy_tone[1], hdr_copy_tone[2]), c.a);
}
)"sv;

static wgpu::BindGroupLayout hdr_bind_group_layout() {
  if (!g_hdrBindGroupLayout) {
    static constexpr std::array entries{
        wgpu::BindGroupLayoutEntry{
            .binding = 0,
            .visibility = wgpu::ShaderStage::Fragment,
            .sampler = wgpu::SamplerBindingLayout{.type = wgpu::SamplerBindingType::Filtering},
        },
        wgpu::BindGroupLayoutEntry{
            .binding = 1,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture =
                wgpu::TextureBindingLayout{
                    .sampleType = wgpu::TextureSampleType::Float,
                    .viewDimension = wgpu::TextureViewDimension::e2D,
                },
        },
        wgpu::BindGroupLayoutEntry{
            .binding = 2,
            .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
            .buffer = wgpu::BufferBindingLayout{.type = wgpu::BufferBindingType::Uniform},
        },
        wgpu::BindGroupLayoutEntry{
            .binding = 3,
            .visibility = wgpu::ShaderStage::Fragment,
            .buffer = wgpu::BufferBindingLayout{.type = wgpu::BufferBindingType::Uniform},
        },
    };
    static constexpr wgpu::BindGroupLayoutDescriptor descriptor{
        .label = "TexCopyConv HDR Bind Group Layout",
        .entryCount = entries.size(),
        .entries = entries.data(),
    };
    g_hdrBindGroupLayout = g_device.CreateBindGroupLayout(&descriptor);
  }
  return g_hdrBindGroupLayout;
}

// HdrDisplay: the format's fragment shader with each sample post-toned first. Linear: the plain
// fragment shader (passthrough, or RGB565's alpha 1) into the RGBA16F destination.
// A scaled HdrDisplay copy filters the light and post-tones the filtered sample, where an LDR copy
// filters display values: the two are not filter-for-filter equal, by design (no exact copy-filter
// parity is claimed; the curve is not linear, so they differ only where the filter mixes texels).
// Once per kind and format: a copy with no pipeline is skipped (see run).
static void warn_missing_pipeline(std::string_view kind, GXTexFmt fmt) {
  static std::mutex mutex;
  static absl::flat_hash_set<std::pair<std::string_view, GXTexFmt>> warned;
  std::lock_guard lock{mutex};
  if (warned.emplace(kind, fmt).second) {
    Log.warn("No {} pipeline for format {}; copy not supported; skipped", kind, static_cast<int>(fmt));
  }
}

static wgpu::RenderPipeline domain_pipeline(GXTexFmt fmt, CopyDomain domain, wgpu::TextureFormat outputFormat) {
  std::lock_guard lock{g_domainMutex};
  const auto key = std::make_tuple(fmt, domain, outputFormat);
  if (const auto it = g_domainPipelines.find(key); it != g_domainPipelines.end()) {
    return it->second;
  }
  std::string_view frag = FragPassthrough;
  if (fmt != GX_TF_RGBA8) {
    const auto it = std::ranges::find(ConvPipelines, fmt, &ConvPipeline::fmt);
    if (it == ConvPipelines.end() || (domain == CopyDomain::Linear && fmt != GX_TF_RGB565)) {
      warn_missing_pipeline(domain == CopyDomain::Linear ? "linear copy" : "HDR copy", fmt);
      return {};
    }
    frag = it->fragShader;
  }
  std::string fragShader{frag};
  std::string preamble{ShaderPreamble};
  wgpu::BindGroupLayout layout = g_bindGroupLayout;
  if (domain == CopyDomain::HdrDisplay) {
    static constexpr std::string_view Sample = "sample_efb(in.uv)";
    std::string wrapped;
    size_t pos = 0;
    for (size_t at; (at = fragShader.find(Sample, pos)) != std::string::npos; pos = at + Sample.size()) {
      wrapped.append(fragShader, pos, at - pos);
      wrapped += "hdr_copy_display(";
      wrapped += Sample;
      wrapped += ")";
    }
    if (pos == 0) {
      Log.fatal("HDR copy pipeline for format {}: no sample to post-tone", static_cast<int>(fmt));
    }
    wrapped.append(fragShader, pos);
    fragShader = std::move(wrapped);
    preamble += hdr::ToneWgsl;
    preamble += hdr::PostToneWgsl;
    preamble += HdrCopyPreamble;
    layout = hdr_bind_group_layout();
  }
  const char* label = domain == CopyDomain::Linear ? "TexCopyConv Linear" : "TexCopyConv HDR";
  auto pipeline = create_pipeline({fmt, fragShader, outputFormat, label}, preamble, layout);
  g_domainPipelines.emplace(key, pipeline);
  return pipeline;
}

void initialize() {
  static constexpr std::array bindGroupLayoutEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment,
          .sampler =
              wgpu::SamplerBindingLayout{
                  .type = wgpu::SamplerBindingType::Filtering,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
          .buffer =
              wgpu::BufferBindingLayout{
                  .type = wgpu::BufferBindingType::Uniform,
              },
      },
  };
  static constexpr wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor{
      .label = "TexCopyConv Bind Group Layout",
      .entryCount = bindGroupLayoutEntries.size(),
      .entries = bindGroupLayoutEntries.data(),
  };
  g_bindGroupLayout = g_device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

  static constexpr std::array depthBindGroupLayoutEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::Depth,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Vertex,
          .buffer =
              wgpu::BufferBindingLayout{
                  .type = wgpu::BufferBindingType::Uniform,
              },
      },
  };
  static constexpr wgpu::BindGroupLayoutDescriptor depthBindGroupLayoutDescriptor{
      .label = "TexCopyConv Depth Bind Group Layout",
      .entryCount = depthBindGroupLayoutEntries.size(),
      .entries = depthBindGroupLayoutEntries.data(),
  };
  g_depthBindGroupLayout = g_device.CreateBindGroupLayout(&depthBindGroupLayoutDescriptor);
  if (webgpu::g_hasCoreFeatures) {
    auto msEntries = depthBindGroupLayoutEntries;
    msEntries[0].texture.multisampled = true;
    const wgpu::BindGroupLayoutDescriptor msDescriptor{
        .label = "TexCopyConv Depth MS Bind Group Layout",
        .entryCount = 2,
        .entries = msEntries.data(),
    };
    g_depthBindGroupLayoutMS = g_device.CreateBindGroupLayout(&msDescriptor);
  }

  g_blitPipeline = create_pipeline(
      {GX_TF_RGBA8, FragPassthrough, webgpu::g_graphicsConfig.surfaceConfiguration.format, "TexCopyConv Blit"},
      ShaderPreamble, g_bindGroupLayout);
  for (const auto& conv : ConvPipelines) {
    g_pipelines[conv.fmt] = create_pipeline(conv, ShaderPreamble, g_bindGroupLayout);
    if (conv.outputFormat != to_wgpu(conv.fmt)) {
      Log.fatal("Output format mismatch for {}", conv.fmt);
    }
  }
  // Skip depth copies in compatibility mode
  if (webgpu::g_hasCoreFeatures) {
    {
      // Port: the same shaders on a multisampled colour source, sample 0 (textureLoad).
      auto msEntries = bindGroupLayoutEntries;
      msEntries[1].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
      msEntries[1].texture.multisampled = true;
      const wgpu::BindGroupLayoutDescriptor msDescriptor{
          .label = "TexCopyConv Index MS Bind Group Layout",
          .entryCount = msEntries.size(),
          .entries = msEntries.data(),
      };
      g_indexBindGroupLayoutMS = g_device.CreateBindGroupLayout(&msDescriptor);
      std::string preamble{ShaderPreamble};
      const auto replace = [&](std::string_view from, std::string_view to) {
        const auto at = preamble.find(from);
        if (at == std::string::npos) {
          Log.fatal("index MS preamble: '{}' not found", from);
        }
        preamble.replace(at, from.size(), to);
      };
      replace("texture_2d<f32>", "texture_multisampled_2d<f32>");
      replace("textureSample(src, src_samp, uv)",
              "textureLoad(src, clamp(vec2i(uv * vec2f(textureDimensions(src))), vec2i(0), "
              "vec2i(textureDimensions(src)) - vec2i(1)), 0)");
      for (const auto& conv : ConvPipelines) {
        if (index_copy_format(conv.fmt)) {
          g_indexPipelinesMS[conv.fmt] = create_pipeline(conv, preamble, g_indexBindGroupLayoutMS);
        }
      }
    }
    for (const auto& conv : DepthConvPipelines) {
      g_pipelines[conv.fmt] = create_pipeline(conv, DepthShaderPreamble, g_depthBindGroupLayout);
      if (conv.outputFormat != to_wgpu(conv.fmt)) {
        Log.fatal("Output format mismatch for {}", conv.fmt);
      }
    }
    // The same shaders on a multisampled source: textureLoad's last argument becomes the sample index.
    std::string msPreamble = DepthShaderPreamble;
    const std::string_view single = "texture_depth_2d";
    msPreamble.replace(msPreamble.find(single), single.size(), "texture_depth_multisampled_2d");
    for (const auto& conv : DepthConvPipelines) {
      g_depthPipelinesMS[conv.fmt] = create_pipeline(conv, msPreamble, g_depthBindGroupLayoutMS);
    }
    g_depthSnapshotBindGroupLayout = create_depth_snapshot_layout(false, "Depth Snapshot Bind Group Layout");
    g_depthSnapshotBindGroupLayoutMS = create_depth_snapshot_layout(true, "Depth Snapshot MS Bind Group Layout");
    g_depthSnapshotPipeline =
        create_depth_snapshot_pipeline(DepthSnapshotShader, g_depthSnapshotBindGroupLayout, "Depth Snapshot");
    g_depthSnapshotPipelineMS =
        create_depth_snapshot_pipeline(DepthSnapshotShaderMS, g_depthSnapshotBindGroupLayoutMS, "Depth Snapshot MS");
  }

  static constexpr wgpu::SamplerDescriptor nearestSamplerDescriptor{
      .label = "TexCopyConv Nearest Sampler",
      .magFilter = wgpu::FilterMode::Nearest,
      .minFilter = wgpu::FilterMode::Nearest,
  };
  g_nearestSampler = g_device.CreateSampler(&nearestSamplerDescriptor);

  static constexpr wgpu::SamplerDescriptor linearSamplerDescriptor{
      .label = "TexCopyConv Linear Sampler",
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
  };
  g_linearSampler = g_device.CreateSampler(&linearSamplerDescriptor);
}

void shutdown() {
  {
    std::lock_guard lock{g_domainMutex};
    g_domainPipelines.clear();
    g_hdrBindGroupLayout = {};
  }
  g_pipelines.clear();
  g_blitPipeline = {};
  g_bindGroupLayout = {};
  g_depthBindGroupLayout = {};
  g_depthBindGroupLayoutMS = {};
  g_depthPipelinesMS.clear();
  g_indexPipelinesMS.clear();
  g_indexBindGroupLayoutMS = {};
  g_nearestSampler = {};
  g_linearSampler = {};
  g_depthSnapshotBindGroupLayout = {};
  g_depthSnapshotBindGroupLayoutMS = {};
  g_depthSnapshotPipeline = {};
  g_depthSnapshotPipelineMS = {};
}

static void execute(const wgpu::CommandEncoder& cmd, const ConvRequest& req, const wgpu::RenderPipeline& pipeline) {
  if (!pipeline) {
    return;
  }
  wgpu::BindGroup bindGroup;
  if (gx::is_depth_format(req.fmt)) {
    // Skip depth copies in compatibility mode
    if (!webgpu::g_hasCoreFeatures) {
      return;
    }
    const std::array bindGroupEntries{
        wgpu::BindGroupEntry{
            .binding = 0,
            .textureView = req.srcView,
        },
        wgpu::BindGroupEntry{
            .binding = 1,
            .buffer = detail::resources().uniformBuffer,
            .offset = req.uniformRange.offset,
            .size = req.uniformRange.size,
        },
    };
    const wgpu::BindGroupDescriptor bindGroupDescriptor{
        .layout = req.srcSamples > 1 ? g_depthBindGroupLayoutMS : g_depthBindGroupLayout,
        .entryCount = bindGroupEntries.size(),
        .entries = bindGroupEntries.data(),
    };
    bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  } else if (req.domain == CopyDomain::HdrDisplay) {
    const auto& sampler = req.sampleFilter == SampleFilter::Linear ? g_linearSampler : g_nearestSampler;
    const std::array bindGroupEntries{
        wgpu::BindGroupEntry{
            .binding = 0,
            .sampler = sampler,
        },
        wgpu::BindGroupEntry{
            .binding = 1,
            .textureView = req.srcView,
        },
        wgpu::BindGroupEntry{
            .binding = 2,
            .buffer = detail::resources().uniformBuffer,
            .offset = req.uniformRange.offset,
            .size = req.uniformRange.size,
        },
        wgpu::BindGroupEntry{
            .binding = 3,
            .buffer = detail::resources().uniformBuffer,
            .offset = req.toneRange.offset,
            .size = req.toneRange.size,
        },
    };
    const wgpu::BindGroupDescriptor bindGroupDescriptor{
        .layout = hdr_bind_group_layout(),
        .entryCount = bindGroupEntries.size(),
        .entries = bindGroupEntries.data(),
    };
    bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  } else {
    const auto& sampler = req.sampleFilter == SampleFilter::Linear ? g_linearSampler : g_nearestSampler;
    const std::array bindGroupEntries{
        wgpu::BindGroupEntry{
            .binding = 0,
            .sampler = sampler,
        },
        wgpu::BindGroupEntry{
            .binding = 1,
            .textureView = req.srcView,
        },
        wgpu::BindGroupEntry{
            .binding = 2,
            .buffer = detail::resources().uniformBuffer,
            .offset = req.uniformRange.offset,
            .size = req.uniformRange.size,
        },
    };
    const wgpu::BindGroupDescriptor bindGroupDescriptor{
        .layout = req.srcSamples > 1 ? g_indexBindGroupLayoutMS : g_bindGroupLayout,
        .entryCount = bindGroupEntries.size(),
        .entries = bindGroupEntries.data(),
    };
    bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  }

  const std::array colorAttachments{
      wgpu::RenderPassColorAttachment{
          .view = req.dst->attachmentTextureView,
          .loadOp = wgpu::LoadOp::Clear,
          .storeOp = wgpu::StoreOp::Store,
          .clearValue = {0.0, 0.0, 0.0, 0.0},
      },
  };
  const wgpu::RenderPassDescriptor renderPassDescriptor{
      .label = "TexCopyConv Pass",
      .colorAttachmentCount = colorAttachments.size(),
      .colorAttachments = colorAttachments.data(),
      .timestampWrites = webgpu::gpu_prof::pass_writes("EFB copy convert"),
  };
  const auto pass = cmd.BeginRenderPass(&renderPassDescriptor);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup);
  pass.Draw(3);
  pass.End();
}

void run(const wgpu::CommandEncoder& cmd, const ConvRequest& req) {
  if (req.domain != CopyDomain::Display) {
    if (const auto pipeline = domain_pipeline(req.fmt, req.domain, req.dst->format)) {
      execute(cmd, req, pipeline);
    }
    return;
  }
  // A format with no pipeline leaves the destination as it was, rather than ending the game.
  const auto& pipelines = req.srcSamples <= 1 ? g_pipelines
                          : gx::is_depth_format(req.fmt) ? g_depthPipelinesMS
                                                         : g_indexPipelinesMS;
  const auto it = pipelines.find(req.fmt);
  if (it == pipelines.end()) {
    warn_missing_pipeline(req.srcSamples > 1 ? "multisampled conversion" : "conversion", req.fmt);
    return;
  }
  execute(cmd, req, it->second);
}

bool depth_conversion_supported(GXTexFmt fmt, uint32_t msaaSamples) noexcept {
  return msaaSamples > 1 ? g_depthPipelinesMS.contains(fmt) : g_pipelines.contains(fmt);
}

void blit(const wgpu::CommandEncoder& cmd, const ConvRequest& req) {
  if (req.domain != CopyDomain::Display) {
    execute(cmd, req, domain_pipeline(GX_TF_RGBA8, req.domain, req.dst->format));
    return;
  }
  execute(cmd, req, g_blitPipeline);
}

bool snapshot_depth_supported() noexcept { return static_cast<bool>(g_depthSnapshotPipeline); }

void snapshot_depth(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& srcDepth, uint32_t msaaSamples,
                    const wgpu::TextureView& dst) {
  const bool multisampled = msaaSamples > 1;
  const auto& pipeline = multisampled ? g_depthSnapshotPipelineMS : g_depthSnapshotPipeline;
  if (!pipeline) {
    return;
  }
  const std::array bindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .textureView = srcDepth,
      },
  };
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .layout = multisampled ? g_depthSnapshotBindGroupLayoutMS : g_depthSnapshotBindGroupLayout,
      .entryCount = bindGroupEntries.size(),
      .entries = bindGroupEntries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);

  const std::array colorAttachments{
      wgpu::RenderPassColorAttachment{
          .view = dst,
          .loadOp = wgpu::LoadOp::Clear,
          .storeOp = wgpu::StoreOp::Store,
          .clearValue = {0.0, 0.0, 0.0, 0.0},
      },
  };
  const wgpu::RenderPassDescriptor renderPassDescriptor{
      .label = "Depth Snapshot Pass",
      .colorAttachmentCount = colorAttachments.size(),
      .colorAttachments = colorAttachments.data(),
      .timestampWrites = webgpu::gpu_prof::pass_writes("Depth snapshot"),
  };
  const auto pass = cmd.BeginRenderPass(&renderPassDescriptor);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup);
  pass.Draw(3);
  pass.End();
}

} // namespace aurora::gfx::tex_copy_conv
