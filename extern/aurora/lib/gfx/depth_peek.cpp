#include "depth_peek.hpp"

#include "../dolphin/vi/vi_internal.hpp"
#include "../gx/gx.hpp"
#include "../gfx/render_worker.hpp"
#include "../webgpu/gpu.hpp"
#include "resources.hpp"
#include "../webgpu/gpu_prof.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <chrono>
#include <mutex>
#include <string>

#include <magic_enum.hpp>
#include <tracy/Tracy.hpp>

namespace aurora::gfx::depth_peek {
namespace {
Module Log("aurora::gfx::depth_peek");

using webgpu::g_device;
using webgpu::g_instance;
using webgpu::g_queue;

using Clock = std::chrono::steady_clock;

constexpr size_t SlotCount = 3;
constexpr uint32_t WorkgroupSizeX = 8;
constexpr uint32_t WorkgroupSizeY = 8;
constexpr uint32_t DepthPeekSnapshotHz = 30;
constexpr auto SnapshotInterval = std::chrono::nanoseconds{1'000'000'000 / DepthPeekSnapshotHz};

struct Params {
  uint32_t dstWidth = 0;
  uint32_t dstHeight = 0;
  uint32_t srcWidth = 0;
  uint32_t srcHeight = 0;
  float offsetX = 0.f;
  float offsetY = 0.f;
  float scaleX = 1.f;
  float scaleY = 1.f;
};
static_assert(sizeof(Params) == 32);

struct LatestSnapshot {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint32_t> data;
  Clock::time_point time;
};

enum class SlotState : uint8_t {
  Available,
  CopySubmitted,
  MapPending,
};

  // The compute path's u32 output, or the fallback's R32Uint snapshot it copies from (never both).
struct Slot {
  wgpu::Texture targetTexture;
  wgpu::TextureView targetView;
  wgpu::Buffer storageBuffer;
  wgpu::Buffer readbackBuffer;
  wgpu::Buffer paramsBuffer;
  uint32_t width = 0;
  uint32_t rowStride = 0; // bytesPerRow in the readback buffer (rows padded to 256 without compute)
  uint32_t height = 0;
  uint64_t byteSize = 0;
  SlotState state = SlotState::Available;
};

struct PendingMap {
  size_t slotIdx = 0;
  wgpu::Buffer readbackBuffer;
  uint64_t byteSize = 0;
};

bool g_enabled = false;
std::array<Slot, SlotCount> g_slots;
size_t g_nextSlot = 0;
// [1] reads a multisampled depth target (sample 0, as the GX depth copies do).
std::array<wgpu::BindGroupLayout, 2> g_bindGroupLayout;
std::array<wgpu::RenderPipeline, 2> g_fallbackPipeline;
std::array<wgpu::ComputePipeline, 2> g_pipeline;
bool g_snapshotRequested = false;
Clock::time_point g_nextSnapshotTime;
LatestSnapshot g_latest;
std::mutex g_mutex;

constexpr std::string_view ShaderPreamble = R"(
struct Params {
    dstSize: vec2u,
    srcSize: vec2u,
    offset: vec2f,
    scale: vec2f,
};

@group(0) @binding(0) var src: texture_depth_2d;

@group(0) @binding(2) var<uniform> params: Params;
)"sv;

constexpr std::string_view ReversedZBody = R"(
fn gx_z24(depth: f32) -> u32 {
    return min(u32(clamp(1.0 - depth, 0.0, 1.0) * 16777215.0 + 0.5), 0x00ffffffu);
}
)"sv;

constexpr std::string_view ForwardZBody = R"(
fn gx_z24(depth: f32) -> u32 {
    return min(u32(clamp(depth, 0.0, 1.0) * 16777215.0 + 0.5), 0x00ffffffu);
}
)"sv;

// The snapshot texel's z, one copy for both paths: the compute pass runs it per invocation, the
// no-compute fallback per fragment of the R32Uint texture it renders.
constexpr std::string_view ShaderShared = R"(
fn load_depth(coord: vec2i) -> f32 {
    return textureLoad(src, coord, 0);
}
fn peek_z(id: vec2u) -> u32 {
    let dstCenter = vec2f(id) + vec2f(0.5, 0.5);
    let srcPixel = clamp(vec2i(floor(params.offset + dstCenter * params.scale)), vec2i(0, 0),
                         vec2i(params.srcSize) - vec2i(1, 1));
    let depth = load_depth(srcPixel);
    return gx_z24(depth);
}
)"sv;

constexpr std::string_view ComputeMain = R"(
@group(0) @binding(1) var<storage, read_write> out_z: array<u32>;

@compute @workgroup_size(8, 8, 1)
fn cs_main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= params.dstSize.x || id.y >= params.dstSize.y) {
        return;
    }
    out_z[id.y * params.dstSize.x + id.x] = peek_z(id.xy);
}
)"sv;

// g_noCompute: the same value per snapshot texel, written by a fragment instead. The pass covers
// the R32Uint texture exactly, so a fragment's pixel is its texel, as id.xy is the invocation's.
constexpr std::string_view RenderMain = R"(
@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var corners = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    let c = corners[i];
    return vec4f(c, 0.0, 1.0);
}

@fragment
fn fs_main(@builtin(position) pos: vec4f) -> @location(0) u32 {
    return peek_z(vec2u(vec2i(floor(pos.xy))));
}
)"sv;

std::string build_shader_source(bool render, bool multisampled) {
  std::string source;
  source.reserve(ShaderPreamble.size() + ShaderShared.size() + ComputeMain.size());
  source += ShaderPreamble;
  if (multisampled) {
    // textureLoad's last argument is the sample index instead of the mip level: 0 either way.
    constexpr std::string_view single = "texture_depth_2d"sv;
    source.replace(source.find(single), single.size(), "texture_depth_multisampled_2d");
  }
  source += gx::UseReversedZ ? ReversedZBody : ForwardZBody;
  source += ShaderShared;
  source += render ? RenderMain : ComputeMain;
  return source;
}

wgpu::ShaderModule create_module(bool render, bool multisampled, const char* label) {
  const auto shaderSource = build_shader_source(render, multisampled);
  const wgpu::ShaderSourceWGSL wgslSource{wgpu::ShaderSourceWGSL::Init{
      .code = shaderSource.c_str(),
  }};
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &wgslSource,
      .label = label,
  };
  return g_device.CreateShaderModule(&moduleDescriptor);
}
wgpu::ComputePipeline create_pipeline(const wgpu::BindGroupLayout& bindGroupLayout, bool multisampled, const char* label) {
  const auto module = create_module(false, multisampled, label);

  const wgpu::PipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &bindGroupLayout,
  };
  const auto pipelineLayout = g_device.CreatePipelineLayout(&layoutDescriptor);
  const wgpu::ComputePipelineDescriptor pipelineDescriptor{
      .label = label,
      .layout = pipelineLayout,
      .compute =
          wgpu::ComputeState{
              .module = module,
              .entryPoint = "cs_main",
          },
  };
  return g_device.CreateComputePipeline(&pipelineDescriptor);
}
wgpu::RenderPipeline create_fallback_pipeline(const wgpu::BindGroupLayout& bindGroupLayout, bool multisampled,
                                                const char* label) {
  const auto module = create_module(true, multisampled, label);
  const wgpu::PipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &bindGroupLayout,
  };
  const auto pipelineLayout = g_device.CreatePipelineLayout(&layoutDescriptor);
  const wgpu::ColorTargetState target{
      .format = wgpu::TextureFormat::R32Uint,
      .writeMask = wgpu::ColorWriteMask::All,
  };
  const wgpu::FragmentState fragment{
      .module = module,
      .entryPoint = "fs_main",
      .targetCount = 1,
      .targets = &target,
  };
  const wgpu::RenderPipelineDescriptor pipelineDescriptor{
      .label = label,
      .layout = pipelineLayout,
      .vertex = {.module = module, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .fragment = &fragment,
  };
  return g_device.CreateRenderPipeline(&pipelineDescriptor);
}

wgpu::BindGroupLayout create_bind_group_layout(bool multisampled, const char* label) {
  if (g_noCompute) {
    // No storage buffer: the snapshot is the pass's colour attachment.
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
        wgpu::BindGroupLayoutEntry{
            .binding = 2,
            .visibility = wgpu::ShaderStage::Fragment,
            .buffer =
                wgpu::BufferBindingLayout{
                    .type = wgpu::BufferBindingType::Uniform,
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
  const std::array entries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Compute,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::Depth,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
                  .multisampled = multisampled,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer =
              wgpu::BufferBindingLayout{
                  .type = wgpu::BufferBindingType::Storage,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer =
              wgpu::BufferBindingLayout{
                  .type = wgpu::BufferBindingType::Uniform,
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

Params make_params(wgpu::Extent3D sourceSize, Vec2<uint32_t> dstSize) noexcept {
  Params params{
      .dstWidth = dstSize.x,
      .dstHeight = dstSize.y,
      .srcWidth = sourceSize.width,
      .srcHeight = sourceSize.height,
  };

  if (gx::g_gxState.viewportPolicy == AURORA_VIEWPORT_NATIVE) {
    return params;
  }

  const auto logicalSize = vi::configured_fb_size();
  if (logicalSize.x == 0 || logicalSize.y == 0 || sourceSize.width == 0 || sourceSize.height == 0) {
    return params;
  }

  const bool stretch = gx::g_gxState.viewportPolicy == AURORA_VIEWPORT_STRETCH;
  const float scaleX = static_cast<float>(sourceSize.width) / static_cast<float>(logicalSize.x);
  const float scaleY = static_cast<float>(sourceSize.height) / static_cast<float>(logicalSize.y);
  const float scale = std::min(scaleX, scaleY);
  params.scaleX = stretch ? scaleX : scale;
  params.scaleY = stretch ? scaleY : scale;
  params.offsetX =
      stretch ? 0.f : (static_cast<float>(sourceSize.width) - static_cast<float>(logicalSize.x) * scale) * 0.5f;
  params.offsetY =
      stretch ? 0.f : (static_cast<float>(sourceSize.height) - static_cast<float>(logicalSize.y) * scale) * 0.5f;
  return params;
}
// Copies need rows padded to 256 bytes; the fallback's texture copy pads them and complete_slot
// de-pads. Without compute the storage buffer's rows are the pixels' own.
constexpr uint32_t CopyRowAlignment = 256;

uint32_t row_stride(uint32_t width) noexcept {
  const uint32_t rowBytes = width * uint32_t{sizeof(uint32_t)};
  if (!g_noCompute) {
    return rowBytes;
  }
  return (rowBytes + CopyRowAlignment - 1u) / CopyRowAlignment * CopyRowAlignment;
}

bool slot_ready(const Slot& slot, uint32_t width, uint32_t height, uint64_t byteSize) noexcept {
  const bool output = g_noCompute ? static_cast<bool>(slot.targetView) : static_cast<bool>(slot.storageBuffer);
  return output && slot.readbackBuffer && slot.paramsBuffer && slot.width == width && slot.height == height &&
         slot.byteSize == byteSize;
}

bool ensure_slot(Slot& slot, uint32_t width, uint32_t height) {
  const uint32_t stride = row_stride(width);
  const uint64_t byteSize = static_cast<uint64_t>(stride) * static_cast<uint64_t>(height);
  if (slot_ready(slot, width, height, byteSize)) {
    return true;
  }

  slot.targetTexture = {};
  slot.targetView = {};
  slot.storageBuffer = {};
  slot.readbackBuffer = {};
  slot.paramsBuffer = {};
  slot.width = width;
  slot.height = height;
  slot.rowStride = stride;
  slot.byteSize = byteSize;

  if (byteSize == 0 || byteSize > UINT32_MAX) {
    return false;
  }

  if (g_noCompute) {
    const wgpu::TextureDescriptor targetDescriptor{
        .label = "Depth Peek Target",
        .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc,
        .size = {width, height, 1},
        .format = wgpu::TextureFormat::R32Uint,
    };
    slot.targetTexture = g_device.CreateTexture(&targetDescriptor);
    slot.targetView = slot.targetTexture.CreateView();
  } else {
    const wgpu::BufferDescriptor storageDescriptor{
        .label = "Depth Peek Storage Buffer",
        .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc,
        .size = byteSize,
    };
    slot.storageBuffer = g_device.CreateBuffer(&storageDescriptor);
  }

  const wgpu::BufferDescriptor readbackDescriptor{
      .label = "Depth Peek Readback Buffer",
      .usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
      .size = byteSize,
  };
  slot.readbackBuffer = g_device.CreateBuffer(&readbackDescriptor);

  const wgpu::BufferDescriptor paramsDescriptor{
      .label = "Depth Peek Params Buffer",
      .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
      .size = sizeof(Params),
  };
  slot.paramsBuffer = g_device.CreateBuffer(&paramsDescriptor);
  return slot.readbackBuffer && slot.paramsBuffer &&
         (g_noCompute ? static_cast<bool>(slot.targetTexture) : static_cast<bool>(slot.storageBuffer));
}

Slot* find_available_slot(uint32_t width, uint32_t height) {
  for (size_t i = 0; i < g_slots.size(); ++i) {
    const size_t idx = (g_nextSlot + i) % g_slots.size();
    auto& slot = g_slots[idx];
    if (slot.state != SlotState::Available) {
      continue;
    }
    if (!ensure_slot(slot, width, height)) {
      continue;
    }
    g_nextSlot = (idx + 1) % g_slots.size();
    return &slot;
  }
  return nullptr;
}

void complete_slot(size_t slotIdx, wgpu::MapAsyncStatus status, wgpu::StringView message) {
  std::lock_guard lock{g_mutex};
  auto& slot = g_slots[slotIdx];
  if (status == wgpu::MapAsyncStatus::Success) {
    const auto valueCount = static_cast<size_t>(slot.width) * static_cast<size_t>(slot.height);
    const auto* mapped = static_cast<const uint32_t*>(slot.readbackBuffer.GetConstMappedRange(0, slot.byteSize));
    if (mapped != nullptr) {
      g_latest.width = slot.width;
      g_latest.height = slot.height;
      g_latest.data.resize(valueCount);
      const size_t rowValues = slot.width;
      const size_t stride = slot.rowStride / sizeof(uint32_t);
      if (stride == rowValues) {
        std::memcpy(g_latest.data.data(), mapped, valueCount * sizeof(uint32_t));
      } else {
        // The texture copy padded every row to 256 bytes: take each row's values and drop the rest.
        for (uint32_t y = 0; y < slot.height; ++y) {
          std::memcpy(g_latest.data.data() + static_cast<size_t>(y) * rowValues,
                      mapped + static_cast<size_t>(y) * stride, rowValues * sizeof(uint32_t));
        }
      }
      g_latest.time = Clock::now();
    }
    slot.readbackBuffer.Unmap();
  } else if (status != wgpu::MapAsyncStatus::CallbackCancelled && status != wgpu::MapAsyncStatus::Aborted) {
    Log.warn("Depth Peek readback mapping failed {}: {}", magic_enum::enum_name(status), message);
  }
  slot.state = SlotState::Available;
}
} // namespace

void initialize() {
  if (!webgpu::g_hasCoreFeatures) {
    return;
  }
  for (size_t ms = 0; ms < g_bindGroupLayout.size(); ++ms) {
    const bool multisampled = ms != 0;
    g_bindGroupLayout[ms] = create_bind_group_layout(multisampled, "Depth Peek Bind Group Layout");
    if (g_noCompute) {
      g_fallbackPipeline[ms] =
          create_fallback_pipeline(g_bindGroupLayout[ms], multisampled, "Depth Peek Fallback Pipeline");
    } else {
      g_pipeline[ms] = create_pipeline(g_bindGroupLayout[ms], multisampled, "Depth Peek Pipeline");
    }
  }
  g_enabled = true;
}

void shutdown() {
  testing::reset();
  g_fallbackPipeline = {};
  g_pipeline = {};
  g_bindGroupLayout = {};
  for (auto& slot : g_slots) {
    slot = {};
  }
}

bool enabled() noexcept { return g_enabled; }

void request_snapshot() noexcept {
  if (!g_enabled) {
    return;
  }
  std::lock_guard lock{g_mutex};
  g_snapshotRequested = true;
}

bool snapshot_wanted() noexcept {
  if (!g_enabled) {
    return false;
  }
  std::lock_guard lock{g_mutex};
  return g_snapshotRequested;
}

bool read_latest(uint16_t x, uint16_t y, uint32_t& z) noexcept {
  std::lock_guard lock{g_mutex};
  if (x >= g_latest.width || y >= g_latest.height || g_latest.data.empty()) {
    return false;
  }
  z = g_latest.data[static_cast<size_t>(y) * g_latest.width + x] & 0x00ffffffu;
  return true;
}

bool read_region(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t* out, uint32_t maxAgeMs) noexcept {
  std::lock_guard lock{g_mutex};
  if (g_latest.data.empty() || x + w > g_latest.width || y + h > g_latest.height ||
      Clock::now() - g_latest.time > std::chrono::milliseconds{maxAgeMs}) {
    return false;
  }
  for (uint32_t row = 0; row < h; ++row) {
    const uint32_t* src = g_latest.data.data() + static_cast<size_t>(y + row) * g_latest.width + x;
    for (uint32_t col = 0; col < w; ++col) {
      out[row * w + col] = src[col] & 0x00ffffffu;
    }
  }
  return true;
}

void encode_frame_snapshot(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& depthView,
                           wgpu::Extent3D sourceSize, uint32_t msaaSamples, bool forced) noexcept {
  if (!g_enabled) {
    return;
  }

  ZoneScoped;
  const auto now = Clock::now();
  if (!forced) {
    std::lock_guard lock{g_mutex};
    if (!g_snapshotRequested || now < g_nextSnapshotTime) {
      return;
    }
    g_snapshotRequested = false;
    g_nextSnapshotTime = now + SnapshotInterval;
  }

  const auto dstSize = vi::configured_fb_size();
  if (!depthView || dstSize.x == 0 || dstSize.y == 0 || sourceSize.width == 0 || sourceSize.height == 0) {
    return;
  }
  const size_t msIdx = msaaSamples > 1 ? 1 : 0;

  const Params params = make_params(sourceSize, dstSize);
  wgpu::Texture targetTexture;
  wgpu::TextureView targetView;
  wgpu::Buffer storageBuffer;
  wgpu::Buffer readbackBuffer;
  uint32_t rowStride = 0;
  wgpu::Buffer paramsBuffer;
  uint64_t byteSize = 0;
  {
    std::lock_guard lock{g_mutex};
    auto* slot = find_available_slot(dstSize.x, dstSize.y);
    if (slot == nullptr) {
      return;
    }
    slot->state = SlotState::CopySubmitted;
    targetTexture = slot->targetTexture;
    targetView = slot->targetView;
    storageBuffer = slot->storageBuffer;
    readbackBuffer = slot->readbackBuffer;
    rowStride = slot->rowStride;
    paramsBuffer = slot->paramsBuffer;
    byteSize = slot->byteSize;
  }

  AURORA_ASSERT(render_worker::is_worker_thread(), "Depth peek queue write must run on the render worker");
  g_queue.WriteBuffer(paramsBuffer, 0, &params, sizeof(params));

  const std::array bindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .textureView = depthView,
      },
      wgpu::BindGroupEntry{
          .binding = 1,
          .buffer = storageBuffer,
          .size = byteSize,
      },
      wgpu::BindGroupEntry{
          .binding = 2,
          .buffer = paramsBuffer,
          .size = sizeof(Params),
      },
  };
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .label = "Depth Peek Bind Group",
      .layout = g_bindGroupLayout[msIdx],
      .entryCount = g_noCompute ? 2u : bindGroupEntries.size(),
      .entries = bindGroupEntries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  if (g_noCompute) {
    // A fragment per snapshot texel into the R32Uint texture, then a texture copy to the readback
    // buffer (whose rows the 256-byte alignment pads; complete_slot drops the padding).
    const wgpu::RenderPassColorAttachment attachment{
        .view = targetView,
        .loadOp = wgpu::LoadOp::Clear,
        .storeOp = wgpu::StoreOp::Store,
        .clearValue = {0.0, 0.0, 0.0, 0.0},
    };
    const wgpu::RenderPassDescriptor passDescriptor{
        .label = "Depth Peek Pass",
        .colorAttachmentCount = 1,
        .colorAttachments = &attachment,
        .timestampWrites = webgpu::gpu_prof::pass_writes("Depth peek"),
    };
    const auto pass = cmd.BeginRenderPass(&passDescriptor);
    pass.SetPipeline(g_fallbackPipeline[msIdx]);
    pass.SetBindGroup(0, bindGroup);
    pass.Draw(3);
    pass.End();

    const wgpu::TexelCopyTextureInfo source{.texture = targetTexture};
    const wgpu::TexelCopyBufferInfo target{
        .layout = {.offset = 0, .bytesPerRow = rowStride, .rowsPerImage = dstSize.y},
        .buffer = readbackBuffer,
    };
    const wgpu::Extent3D copySize{dstSize.x, dstSize.y, 1};
    cmd.CopyTextureToBuffer(&source, &target, &copySize);
    return;
  }


  const wgpu::ComputePassDescriptor passDescriptor{
      .label = "Depth Peek Compute Pass",
      .timestampWrites = webgpu::gpu_prof::pass_writes("Depth peek"),
  };
  const auto pass = cmd.BeginComputePass(&passDescriptor);
  pass.SetPipeline(g_pipeline[msIdx]);
  pass.SetBindGroup(0, bindGroup);
  pass.DispatchWorkgroups((dstSize.x + WorkgroupSizeX - 1) / WorkgroupSizeX,
                          (dstSize.y + WorkgroupSizeY - 1) / WorkgroupSizeY);
  pass.End();

  cmd.CopyBufferToBuffer(storageBuffer, 0, readbackBuffer, 0, byteSize);
}

void after_submit() noexcept {
  if (!g_enabled) {
    return;
  }

  std::vector<PendingMap> pendingMaps;
  {
    std::lock_guard lock{g_mutex};
    for (size_t i = 0; i < g_slots.size(); ++i) {
      auto& slot = g_slots[i];
      if (slot.state != SlotState::CopySubmitted) {
        continue;
      }
      slot.state = SlotState::MapPending;
      pendingMaps.push_back({
          .slotIdx = i,
          .readbackBuffer = slot.readbackBuffer,
          .byteSize = slot.byteSize,
      });
    }
  }

  for (const auto& pending : pendingMaps) {
    pending.readbackBuffer.MapAsync(
        wgpu::MapMode::Read, 0, pending.byteSize, wgpu::CallbackMode::AllowSpontaneous,
        [slotIdx = pending.slotIdx](wgpu::MapAsyncStatus status, wgpu::StringView message) {
          complete_slot(slotIdx, status, message);
        });
  }
}

namespace testing {
void reset() noexcept {
  std::lock_guard lock{g_mutex};
  g_snapshotRequested = false;
  g_nextSlot = 0;
  g_nextSnapshotTime = {};
  g_latest = {};
  for (auto& slot : g_slots) {
    slot.state = SlotState::Available;
  }
}

bool snapshot_requested() noexcept {
  std::lock_guard lock{g_mutex};
  return g_snapshotRequested;
}

void set_latest(uint32_t width, uint32_t height, const std::vector<uint32_t>& data) {
  std::lock_guard lock{g_mutex};
  g_latest.width = width;
  g_latest.height = height;
  g_latest.data = data;
}
} // namespace testing

} // namespace aurora::gfx::depth_peek
