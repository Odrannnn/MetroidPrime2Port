#pragma once

#include "types.hpp"

#include <dolphin/gx/GXEnum.h>

#include <array>

namespace aurora::gfx::tex_copy_conv {

enum class SampleFilter : uint8_t {
  Nearest,
  Linear,
};

// What a colour copy turns its source into.
enum class CopyDomain : uint8_t {
  // The source as is, through the format's conversion (the 8-bit EFB's copies).
  Display,
  // The RGBA16F HDR scene, post-toned and sRGB-encoded by toneRange's curve first: the display
  // colour a retail copy of the same scene holds, then the format's conversion.
  HdrDisplay,
  // The RGBA16F HDR scene as linear light into an RGBA16F destination (GXPortCopyScene); only
  // the plain colour formats (RGB565 still writes alpha 1).
  Linear,
};

// The copy shaders' uniform: the source region in UV space, and opaqueAlpha for a source without alpha
// (an RGB8 EFB), whose alpha the copy reads as 1.
struct alignas(16) Uniforms {
  Vec2<float> offset{0.f, 0.f};
  Vec2<float> scale{1.f, 1.f};
  uint32_t opaqueAlpha = 0;
  std::array<uint32_t, 3> _pad{};
};
static_assert(sizeof(Uniforms) == 32);

struct ConvRequest {
  GXTexFmt fmt;
  wgpu::TextureView srcView; // View of resolved EFB / offscreen color/depth
  Range uniformRange;        // Uniforms
  TextureHandle dst;         // Destination texture
  SampleFilter sampleFilter = SampleFilter::Nearest;
  CopyDomain domain = CopyDomain::Display;
  Range toneRange; // HdrDisplay: the tone curve's three vec4f rows
  uint32_t srcSamples = 1; // Depth and index formats: srcView's sample count (a multisampled EFB reads sample 0)
};

bool needs_conversion(GXTexFmt fmt);
// Port: A8/R8/G8/B8, whose texels are indices or masks; and whether run() can convert such a copy
// from a multisampled colour source (sample 0; Core devices only).
bool index_copy_format(GXTexFmt fmt) noexcept;
bool index_copy_ms_supported(GXTexFmt fmt) noexcept;
// Whether run() can convert a depth copy of fmt from an EFB with msaaSamples samples. Compatibility
// devices cannot bind multisampled depth textures.
bool depth_conversion_supported(GXTexFmt fmt, uint32_t msaaSamples) noexcept;

void initialize();
void shutdown();
void run(const wgpu::CommandEncoder& cmd, const ConvRequest& req);
void blit(const wgpu::CommandEncoder& cmd, const ConvRequest& req);

bool snapshot_depth_supported() noexcept;
void snapshot_depth(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& srcDepth, uint32_t msaaSamples,
                    const wgpu::TextureView& dst);

} // namespace aurora::gfx::tex_copy_conv
