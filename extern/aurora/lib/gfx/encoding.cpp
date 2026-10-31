#include "encoding.hpp"

#include "frame.hpp"

#include "clear.hpp"
#include "depth_peek.hpp"
#include "efb_copy.hpp"
#include "pipeline_cache.hpp"
#include "scan_peek.hpp"
#include "probe.hpp"
#include "resource_cache.hpp"
#include "tex_copy_conv.hpp"
#include "tex_palette_conv.hpp"
#include "../gx/gx.hpp"
#include "../gx/pipeline.hpp"
#ifdef AURORA_ENABLE_RMLUI
#include "../rmlui/pipeline.hpp"
#endif
#include "../webgpu/gpu.hpp"
#include "../webgpu/gpu_prof.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <tracy/Tracy.hpp>

namespace {
// Port: formats whose texels are indices or masks (see the scaled-copy filter below).
inline bool is_index_copy_format(u32 fmt) {
  switch (fmt) {
  case GX_CTF_A8:
  case GX_CTF_R8:
  case GX_CTF_G8:
  case GX_CTF_B8:
    return true;
  default:
    return false;
  }
}
} // namespace

namespace aurora::gfx {
using namespace detail;
using webgpu::g_device;
using webgpu::g_queue;

namespace {
constexpr Module Log{"aurora::gfx"};
PipelineRef g_currentPipeline;
BindGroupRef g_currentTextureGroup;

void apply_viewport(const wgpu::RenderPassEncoder& pass, const Viewport& vp) {
  const float minDepth = gx::UseReversedZ ? 1.f - vp.zfar : vp.znear;
  const float maxDepth = gx::UseReversedZ ? 1.f - vp.znear : vp.zfar;
  pass.SetViewport(vp.left, vp.top, vp.width, vp.height, minDepth, maxDepth);
}

void apply_scissor(const wgpu::RenderPassEncoder& pass, const ClipRect& sc, const wgpu::Extent3D& size) {
  const auto x = std::clamp(static_cast<uint32_t>(sc.x), 0u, size.width);
  const auto y = std::clamp(static_cast<uint32_t>(sc.y), 0u, size.height);
  const auto w = std::clamp(static_cast<uint32_t>(sc.width), 0u, size.width - x);
  const auto h = std::clamp(static_cast<uint32_t>(sc.height), 0u, size.height - y);
  pass.SetScissorRect(x, y, w, h);
}

DrawContext make_draw_context(const RenderPass& passInfo) {
  auto& res = resources();
  return {
      .device = g_device,
      .queue = g_queue,
      .vertexBuffer = res.vertexBuffer,
      .indexBuffer = res.indexBuffer,
      .uniformBuffer = res.uniformBuffer,
      .storageBuffer = res.storageBuffer,
      .layout = passInfo.target_layout(),
  };
}

void render_custom_draw(const CustomDrawCommand& draw, const wgpu::RenderPassEncoder& pass,
                        const RenderPass& passInfo) {
  const auto drawType = find_runtime_draw_type(draw.type);
  if (!drawType) {
    // Unregistered between record and replay; the command is a no-op.
    return;
  }

  const auto context = make_draw_context(passInfo);
  drawType->draw(context, pass, draw.payload.data(), draw.payloadSize, drawType->userdata);
}

void execute_encoder_task(wgpu::CommandEncoder& cmd, FramePacket& frame, const EncoderTask& task) {
  const auto taskType = find_runtime_encoder_task_type(task.type);
  if (!taskType) {
    // Unregistered between record and encode; the task is a no-op.
    return;
  }

  auto& res = resources();
  const EncoderTaskContext context{
      .device = g_device,
      .queue = g_queue,
      .vertexBuffer = res.vertexBuffer,
      .indexBuffer = res.indexBuffer,
      .uniformBuffer = res.uniformBuffer,
      .storageBuffer = res.storageBuffer,
  };
  taskType->callback(context, cmd, task.payload.data(), task.payloadSize, taskType->userdata);
  if (taskType->afterSubmit != nullptr) {
    const auto payload = task.payload;
    const auto payloadSize = task.payloadSize;
    const auto callback = taskType->afterSubmit;
    const auto userdata = taskType->userdata;
    frame.afterSubmitCallbacks.emplace_back([payload, payloadSize, callback, userdata] {
      const EncoderTaskCompletionContext completionContext{
          .device = g_device,
          .queue = g_queue,
      };
      callback(completionContext, payload.data(), payloadSize, userdata);
    });
  }
}

void render_pass(const wgpu::RenderPassEncoder& pass, FramePacket& frame, RenderPass& passInfo) {
  ZoneScoped;
  g_currentPipeline = UINTPTR_MAX;
#ifdef AURORA_GFX_DEBUG_GROUPS
  std::vector<std::string> lastDebugGroupStack;
#endif
  Viewport currentViewport{};
  ClipRect currentScissor{};
  bool hasViewport = false;
  bool hasScissor = false;

  // Bind bind group for the whole pass
  pass.SetBindGroup(0, resources().staticBindGroup);
  pass.SetBindGroup(2, gx::g_emptyTextureBindGroup);
  g_currentTextureGroup = 0;

  for (auto& cmd : passInfo.commands) {
#ifdef AURORA_GFX_DEBUG_GROUPS
    {
      size_t firstDiff = lastDebugGroupStack.size();
      for (size_t i = 0; i < lastDebugGroupStack.size(); ++i) {
        if (i >= cmd.debugGroupStack.size() || cmd.debugGroupStack[i] != lastDebugGroupStack[i]) {
          firstDiff = i;
          break;
        }
      }
      for (size_t i = firstDiff; i < lastDebugGroupStack.size(); ++i) {
        pass.PopDebugGroup();
      }
      for (size_t i = firstDiff; i < cmd.debugGroupStack.size(); ++i) {
        pass.PushDebugGroup(cmd.debugGroupStack[i].c_str());
      }
      lastDebugGroupStack = cmd.debugGroupStack;
    }
#endif
    switch (cmd.type) {
    case CommandType::SetViewport: {
      const auto& vp = cmd.data.setViewport;
      apply_viewport(pass, vp);
      currentViewport = vp;
      hasViewport = true;
    } break;
    case CommandType::SetScissor: {
      const auto& sc = cmd.data.setScissor;
      apply_scissor(pass, sc, passInfo.colorAttachments[SceneColorAttachmentIndex].size);
      currentScissor = sc;
      hasScissor = true;
    } break;
    case CommandType::Draw: {
      auto& draw = cmd.data.draw;
      if (draw.encoder != nullptr) {
        draw.encoder(draw.payload.data(), pass, passInfo);
      }
    } break;
    case CommandType::CustomDraw: {
      render_custom_draw(cmd.data.customDraw, pass, passInfo);
      g_currentPipeline = UINTPTR_MAX;
      pass.SetBindGroup(0, resources().staticBindGroup);
      pass.SetBindGroup(2, gx::g_emptyTextureBindGroup);
      g_currentTextureGroup = 0;
      if (hasViewport) {
        apply_viewport(pass, currentViewport);
      }
      if (hasScissor) {
        apply_scissor(pass, currentScissor, passInfo.colorAttachments[SceneColorAttachmentIndex].size);
      }
    } break;
    case CommandType::DebugMarker: {
#if defined(AURORA_GFX_DEBUG_GROUPS)
      pass.InsertDebugMarker(wgpu::StringView(frame.debugMarkers[cmd.data.debugMarkerIndex]));
#endif
    } break;
    }
  }

#ifdef AURORA_GFX_DEBUG_GROUPS
  for (size_t i = 0; i < lastDebugGroupStack.size(); ++i) {
    pass.PopDebugGroup();
  }
#endif
}

void render(wgpu::CommandEncoder& cmd, FramePacket& frame, RenderPass& passInfo, uint32_t passIndex) {
  ZoneScoped;
  if (!passInfo.sealed) {
    return;
  }

  for (const auto& conv : passInfo.paletteConvs) {
    tex_palette_conv::run(cmd, conv);
  }
  if (passInfo.discardable) {
    // This pass has no effect and can be safely discarded (e.g. an empty EFB segment between two back-to-back pass
    // breaks, or an unresolved offscreen pass).
    if (passInfo.forceDepthSnapshot) {
      // The depth target still holds what the earlier passes wrote.
      depth_peek::encode_frame_snapshot(cmd, passInfo.copySourceDepthView,
                                        passInfo.colorAttachments[SceneColorAttachmentIndex].size,
                                        passInfo.msaaSamples, true);
    }
    return;
  }

  // Port: a continuation pass that draws nothing and loads (never clears) its targets leaves them as the
  // pass before stored them, so beginning it only costs a full-target load and store (a tiler's main
  // cost). Its copy reads the same single-sampled attachment, so skip the pass and keep the rest.
  bool emptyLoadPass = passInfo.msaaSamples == 1;
  if (emptyLoadPass) {
    for (const auto& command : passInfo.commands) {
      if (command.type == CommandType::Draw || command.type == CommandType::CustomDraw) {
        emptyLoadPass = false;
        break;
      }
    }
  }
  if (emptyLoadPass) {
    for (uint32_t i = 0; i < passInfo.colorAttachmentCount; ++i) {
      const auto& source = passInfo.colorAttachments[i];
      if (source.clear || source.loadOp == wgpu::LoadOp::Clear || source.storeOp != wgpu::StoreOp::Store ||
          source.resolveView) {
        emptyLoadPass = false;
      }
    }
    if (passInfo.depthStencilView && passInfo.hasDepth &&
        (passInfo.depthLoadOp == wgpu::LoadOp::Clear ||
         (passInfo.depthLoadOp == wgpu::LoadOp::Undefined && passInfo.clearDepth))) {
      emptyLoadPass = false;
    }
    if (passInfo.depthStencilView && passInfo.hasStencil && passInfo.stencilLoadOp == wgpu::LoadOp::Clear) {
      emptyLoadPass = false;
    }
  }

  std::array<wgpu::RenderPassColorAttachment, MaxColorAttachments> attachments{};
  for (uint32_t i = 0; i < passInfo.colorAttachmentCount && !emptyLoadPass; ++i) {
    const auto& source = passInfo.colorAttachments[i];
    attachments[i] = {
        .view = source.view,
        .resolveTarget = source.resolveView,
        .loadOp = source.loadOp != wgpu::LoadOp::Undefined ? source.loadOp
                                                           : (source.clear ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load),
        .storeOp = source.storeOp,
        .clearValue =
            {
                .r = source.clearValue.x(),
                .g = source.clearValue.y(),
                .b = source.clearValue.z(),
                .a = source.clearValue.w(),
            },
    };
  }
  wgpu::RenderPassDepthStencilAttachment depthStencilAttachment{};
  const wgpu::RenderPassDepthStencilAttachment* depthStencilAttachmentPtr = nullptr;
  if (passInfo.depthStencilView) {
    depthStencilAttachment = {
        .view = passInfo.depthStencilView,
        .depthLoadOp = passInfo.hasDepth ? (passInfo.depthLoadOp != wgpu::LoadOp::Undefined
                                                ? passInfo.depthLoadOp
                                                : (passInfo.clearDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load))
                                         : wgpu::LoadOp::Undefined,
        .depthStoreOp = passInfo.hasDepth ? passInfo.depthStoreOp : wgpu::StoreOp::Undefined,
        .depthClearValue = passInfo.clearDepthValue,
        .stencilLoadOp = passInfo.hasStencil ? passInfo.stencilLoadOp : wgpu::LoadOp::Undefined,
        .stencilStoreOp = passInfo.hasStencil ? passInfo.stencilStoreOp : wgpu::StoreOp::Undefined,
        .stencilClearValue = passInfo.stencilClearValue,
    };
    depthStencilAttachmentPtr = &depthStencilAttachment;
  }
  const auto label = passInfo.label.empty() ? fmt::format("Render pass {}", passIndex)
                                            : fmt::format("{} {}", passInfo.label, passIndex);
  const wgpu::RenderPassDescriptor renderPassDescriptor{
      .label = label.c_str(),
      .colorAttachmentCount = passInfo.colorAttachmentCount,
      .colorAttachments = attachments.data(),
      .depthStencilAttachment = depthStencilAttachmentPtr,
      // Port: a skipped pass must not take a profiler zone (its timestamps would never be written).
      .timestampWrites = emptyLoadPass ? nullptr
                                       : webgpu::gpu_prof::pass_writes(passIndex == 0 ? "EFB pass (first)"
                                                                                      : "EFB pass (later)"),
  };

  if (!emptyLoadPass) {
    auto pass = cmd.BeginRenderPass(&renderPassDescriptor);
    render_pass(pass, frame, passInfo);
    pass.End();
  }

  if (passInfo.captureDepthSnapshot || passInfo.forceDepthSnapshot) {
    depth_peek::encode_frame_snapshot(cmd, passInfo.copySourceDepthView,
                                      passInfo.colorAttachments[SceneColorAttachmentIndex].size, passInfo.msaaSamples,
                                      passInfo.forceDepthSnapshot);
  }

  const bool isDepthCopy = passInfo.resolveTarget && gx::is_depth_format(passInfo.resolveFormat);
  // A depth copy from a multisampled EFB converts sample 0 (Z8 and Z16 on Core devices); any other
  // is skipped, leaving the destination as it was, rather than ending the game.
  if (isDepthCopy && passInfo.msaaSamples > 1 &&
      !tex_copy_conv::depth_conversion_supported(passInfo.resolveFormat, passInfo.msaaSamples)) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      Log.warn("depth tex copy (format {}) from a {}x multisampled EFB is not supported; skipped",
               static_cast<int>(passInfo.resolveFormat), passInfo.msaaSamples);
    }
  } else if (passInfo.resolveTarget) {
    const auto& dstSize = passInfo.resolveTarget->size;
    const bool needsConversion = tex_copy_conv::needs_conversion(passInfo.resolveFormat);
    // A probe face is a layer of a cube texture, which only the blit addresses.
    const bool needsScaling = passInfo.probeFace >= 0 ||
                              dstSize.width != static_cast<uint32_t>(passInfo.resolveRect.width) ||
                              dstSize.height != static_cast<uint32_t>(passInfo.resolveRect.height);
    const bool isDepth = isDepthCopy;
    // Depth copies read the depth buffer, which an HDR scene pass leaves as it is.
    const auto domain = isDepth ? tex_copy_conv::CopyDomain::Display : passInfo.resolveDomain;
    // Port: an index/mask copy from a multisampled EFB reads sample 0 of the original attachment (kept
    // stored by discard_dead_stores), not the averaged resolve: a blend of two ids is another id.
    const bool indexMs = !isDepth && passInfo.msaaSamples > 1 && domain == tex_copy_conv::CopyDomain::Display &&
                         is_index_copy_format(passInfo.resolveFormat) &&
                         tex_copy_conv::index_copy_ms_supported(passInfo.resolveFormat) &&
                         passInfo.colorAttachments[SceneColorAttachmentIndex].storeOp == wgpu::StoreOp::Store;
    const tex_copy_conv::ConvRequest convReq{
        .fmt = passInfo.resolveFormat,
        .srcView = isDepth ? passInfo.copySourceDepthView
                 : indexMs ? passInfo.colorAttachments[SceneColorAttachmentIndex].view
                           : passInfo.copySourceView,
        .uniformRange = passInfo.resolveUniformRange,
        .dst = passInfo.resolveTarget,
        // Port: 8-bit index/mask copies (A8/R8/G8/B8; scan visor ids, C8 lookups) hold values that
        // must not be averaged: a scaled copy takes the nearest texel, never an in-between index.
        .sampleFilter = (needsScaling && !is_index_copy_format(passInfo.resolveFormat))
                            ? tex_copy_conv::SampleFilter::Linear
                            : tex_copy_conv::SampleFilter::Nearest,
        .domain = domain,
        .toneRange = passInfo.resolveToneRange,
        .srcSamples = (isDepth || indexMs) ? passInfo.msaaSamples : 1u,
    };
    // A raw copy only between equal formats in the same domain: an HDR display copy always
    // post-tones, and RGBA16F never goes to an 8-bit texture as is.
    const bool rawCopy = domain == tex_copy_conv::CopyDomain::Display ||
                         (domain == tex_copy_conv::CopyDomain::Linear &&
                          passInfo.copySourceTexture.GetFormat() == passInfo.resolveTarget->format);
    {
      // Port diagnostic (temporary): what each EFB copy resolve actually copies.
      static const bool sLog = std::getenv("MP2_DEBUG_EFB_COPY") != nullptr;
      if (sLog) {
        const auto& srcSize = passInfo.colorAttachments[SceneColorAttachmentIndex].size;
        Log.info("efb copy pass {} '{}': fmt={} rect=({},{},{},{}) src={}x{} dst={}x{} conv={} scale={} uniform={}+{} depth={} msaa={}",
                 passIndex, label, static_cast<int>(passInfo.resolveFormat), passInfo.resolveRect.x,
                 passInfo.resolveRect.y, passInfo.resolveRect.width, passInfo.resolveRect.height, srcSize.width,
                 srcSize.height, dstSize.width, dstSize.height, needsConversion, needsScaling,
                 passInfo.resolveUniformRange.offset, passInfo.resolveUniformRange.size, isDepth,
                 passInfo.msaaSamples);
      }
    }
    bool skipResolve = false;
    {
      // Port diagnostic (temporary): drop one resolve by index, to place a fault.
      const char* skip = std::getenv("MP2_SKIP_CONV_PASS");
      if (skip != nullptr && passIndex == static_cast<uint32_t>(std::atoi(skip))) {
        skipResolve = true;
      }
    }
    if (skipResolve) {
      // Intentionally leave the target unresolved.
    } else if (needsConversion) {
      tex_copy_conv::run(cmd, convReq);
    } else if (needsScaling || !rawCopy) {
      if (isDepth) {
        // The passthrough blit samples a colour view, so binding a depth view to it is an invalid
        // descriptor - the draw then fetches through a garbage texture descriptor and the GPU faults
        // (VK_ERROR_DEVICE_LOST, GPUVM, `SQC (data)`; the scan visor's Z8L copy did exactly this).
        // No scaled depth copy has a conversion pipeline here, so leave the target unresolved rather
        // than hand the GPU a bad binding.
        static bool sWarnedDepthScale = false;
        if (!sWarnedDepthScale) {
          sWarnedDepthScale = true;
          Log.warn("EFB copy skipped: a scaled depth copy (fmt={}) has no conversion pipeline",
                   static_cast<int>(passInfo.resolveFormat));
        }
      } else {
        tex_copy_conv::blit(cmd, convReq);
      }
    } else {
      const webgpu::gpu_prof::Zone zone{cmd, "EFB copy"};
      const wgpu::TexelCopyTextureInfo src{
          .texture = passInfo.copySourceTexture,
          .origin =
              wgpu::Origin3D{
                  .x = static_cast<uint32_t>(passInfo.resolveRect.x),
                  .y = static_cast<uint32_t>(passInfo.resolveRect.y),
              },
      };
      const wgpu::TexelCopyTextureInfo dst{
          .texture = passInfo.resolveTarget->texture,
      };
      const wgpu::Extent3D size{
          .width = static_cast<uint32_t>(passInfo.resolveRect.width),
          .height = static_cast<uint32_t>(passInfo.resolveRect.height),
          .depthOrArrayLayers = 1,
      };
      cmd.CopyTextureToTexture(&src, &dst, &size);
    }
    if (passInfo.resolveMips && passInfo.resolveTarget->mipCount > 1) {
      // Each level is a 2:1 bilinear blit (a box filter) of the one above.
      const webgpu::gpu_prof::Zone zone{cmd, "EFB copy mips"};
      const auto& target = passInfo.resolveTarget;
      for (uint32_t m = 1; m < target->mipCount; ++m) {
        const auto level = [&](uint32_t mip) {
          const wgpu::TextureViewDescriptor desc{
              .label = "EFB copy mip view",
              .format = target->format,
              .dimension = wgpu::TextureViewDimension::e2D,
              .baseMipLevel = mip,
              .mipLevelCount = 1,
          };
          auto view = target->texture.CreateView(&desc);
          const wgpu::Extent3D size{std::max(target->size.width >> mip, 1u), std::max(target->size.height >> mip, 1u),
                                    1};
          return std::make_shared<TextureRef>(target->texture, view, view, size, target->format, 1, target->gxFormat);
        };
        const auto src = level(m - 1);
        tex_copy_conv::blit(cmd, tex_copy_conv::ConvRequest{
                                     .fmt = GX_TF_RGBA8,
                                     .srcView = src->sampleTextureView,
                                     .uniformRange = passInfo.resolveMipsUniformRange,
                                     .dst = level(m),
                                     .sampleFilter = tex_copy_conv::SampleFilter::Linear,
                                     // A linear copy's levels stay RGBA16F; display copies are 8-bit.
                                     .domain = domain == tex_copy_conv::CopyDomain::Linear
                                                   ? tex_copy_conv::CopyDomain::Linear
                                                   : tex_copy_conv::CopyDomain::Display,
                                 });
      }
    }
    if (passInfo.probeFace >= 0) {
      probe::encode_mips(cmd, passInfo.probeFace, passInfo.probeUniformRange);
    }
  }

  if (passInfo.efbCpuCopy.has_value()) {
    if (passInfo.resolveTarget) {
      efb_copy::encode(cmd, passInfo.resolveTarget, *passInfo.efbCpuCopy);
    } else {
      static bool sWarned = false;
      if (!sWarned) {
        sWarned = true;
        Log.warn("EFB copy read-back dropped: the pass it was requested in has no EFB copy");
      }
    }
  }

  if (passInfo.scanPeek.has_value()) {
    scan_peek::encode(cmd, passInfo.copySourceTexture, passInfo.copySourceDepthTexture, passInfo.msaaSamples,
                      *passInfo.scanPeek, passInfo.colorAttachments[SceneColorAttachmentIndex].view);
  }

  if (passInfo.snapshotColorDst) {
    const webgpu::gpu_prof::Zone zone{cmd, "Pass snapshot"};
    const wgpu::TexelCopyTextureInfo src{
        .texture = passInfo.copySourceTexture,
    };
    const wgpu::TexelCopyTextureInfo dst{
        .texture = passInfo.snapshotColorDst,
    };
    const wgpu::Extent3D size{
        .width = passInfo.colorAttachments[SceneColorAttachmentIndex].size.width,
        .height = passInfo.colorAttachments[SceneColorAttachmentIndex].size.height,
        .depthOrArrayLayers = 1,
    };
    cmd.CopyTextureToTexture(&src, &dst, &size);
  }
  if (passInfo.snapshotDepthDst) {
    tex_copy_conv::snapshot_depth(cmd, passInfo.copySourceDepthView, passInfo.msaaSamples, passInfo.snapshotDepthDst);
  }
}

constexpr uint32_t align_down_copy_offset(uint32_t value) noexcept { return value & ~3u; }

// The bytes one copy path wrote into a shared device buffer this call, which texture vertex fetch
// uploads to that buffer's fetch texture afterwards.
struct WrittenRange {
  uint64_t lo = 0;
  uint64_t hi = 0;

  bool empty() const noexcept { return hi <= lo; }
  void add(uint64_t offset, uint64_t size) noexcept {
    if (size == 0) {
      return;
    }
    lo = empty() ? offset : std::min(lo, offset);
    hi = std::max(hi, offset + size);
  }
};

// Copies what the frame wrote since `copied`: from the staging buffer up to stagingSize, and
// the rest (the frame's spill, which the staging buffer was too small for) through a transient
// buffer. Only bytes below highWater are read, so the recording thread may still be appending.
// The frame's data starts at dstBase in dst, after the resident region.
void copy_staging_buffer_range(wgpu::CommandEncoder& cmd, const FramePacket& frame, uint32_t& copied,
                               uint32_t highWater, uint64_t stagingOffset, uint64_t stagingSize,
                               const SpillBuffer& spill, const wgpu::Buffer& dst, uint64_t dstBase,
                               WrittenRange& written) {
  if (highWater <= copied) {
    return;
  }
  const uint32_t copyStart = align_down_copy_offset(copied);
  const uint32_t copyEnd = AURORA_ALIGN(highWater, 4);
  if (copyStart < stagingSize) {
    const uint64_t end = std::min<uint64_t>(copyEnd, stagingSize);
    cmd.CopyBufferToBuffer(staging_buffer(frame.stagingBuffer), stagingOffset + copyStart, dst,
                           dstBase + copyStart, end - copyStart);
    written.add(dstBase + copyStart, end - copyStart);
  }
  if (copyEnd > stagingSize) {
    const uint64_t start = std::max<uint64_t>(copyStart, stagingSize);
    const uint64_t size = copyEnd - start;
    const wgpu::BufferDescriptor descriptor{
        .label = "Spill Upload Buffer",
        .usage = wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc,
        .size = size,
        .mappedAtCreation = true,
    };
    auto buffer = webgpu::g_device.CreateBuffer(&descriptor);
    auto* mapped = static_cast<uint8_t*>(buffer.GetMappedRange(0, size));
    const uint64_t valid = highWater - start;
    memcpy(mapped, spill.data.get() + (start - stagingSize), valid);
    if (size > valid) {
      memset(mapped + valid, 0, size - valid);
    }
    buffer.Unmap();
    // The command buffer keeps the buffer alive until the GPU is done with the copy.
    cmd.CopyBufferToBuffer(buffer, 0, dst, dstBase + start, size);
    written.add(dstBase + start, size);
  }
  copied = highWater;
}

bool needs_staging_copy(const FramePacket& frame, const FrameOp& op) {
  const auto& highWater = op.highWater;
  if (highWater.verts > frame.copied.verts || highWater.uniforms > frame.copied.uniforms ||
      highWater.indices > frame.copied.indices || highWater.storage > frame.copied.storage ||
      op.bufferUploads.size() > frame.copied.bufferUploadCount) {
    return true;
  }
  if constexpr (UseTextureBuffer) {
    return op.textureUploads.size() > frame.copied.textureUploadCount;
  }
  return false;
}

void copy_staging_to_high_water(wgpu::CommandEncoder& cmd, FramePacket& frame, const FrameOp& op) {
  if (!needs_staging_copy(frame, op)) {
    return;
  }
  const webgpu::gpu_prof::Zone zone{cmd, "Staging copies"};
  const auto& highWater = op.highWater;
  auto& res = resources();
  // What the copies below write into the vertex and storage buffers, for texture vertex fetch
  // (the uniform and index buffers have no fetch texture, so theirs goes nowhere).
  WrittenRange vertexWritten;
  WrittenRange storageWritten;
  WrittenRange unusedWritten;
  const auto& sizes = frame_buffer_sizes();
  const uint64_t VertexStagingOffset = 0;
  const uint64_t UniformStagingOffset = VertexStagingOffset + sizes.vertex;
  const uint64_t IndexStagingOffset = UniformStagingOffset + sizes.uniform;
  const uint64_t StorageStagingOffset = IndexStagingOffset + sizes.index;
  const uint64_t TextureUploadStagingOffset = StorageStagingOffset + sizes.storage;
  copy_staging_buffer_range(cmd, frame, frame.copied.verts, highWater.verts, VertexStagingOffset, sizes.vertex,
                            frame.spillVerts, res.vertexBuffer, sizes.base(StagingKind::Vertex), vertexWritten);
  copy_staging_buffer_range(cmd, frame, frame.copied.uniforms, highWater.uniforms, UniformStagingOffset,
                            sizes.uniform, frame.spillUniforms, res.uniformBuffer, sizes.base(StagingKind::Uniform),
                            unusedWritten);
  copy_staging_buffer_range(cmd, frame, frame.copied.indices, highWater.indices, IndexStagingOffset, sizes.index,
                            frame.spillIndices, res.indexBuffer, sizes.base(StagingKind::Index), unusedWritten);
  copy_staging_buffer_range(cmd, frame, frame.copied.storage, highWater.storage, StorageStagingOffset,
                            sizes.storage, frame.spillStorage, res.storageBuffer, sizes.base(StagingKind::Storage),
                            storageWritten);
  for (size_t i = frame.copied.bufferUploadCount; i < op.bufferUploads.size(); ++i) {
    const auto& item = *op.bufferUploads[i];
    const wgpu::Buffer& dst = item.dst == ResidentBuffer::Vertex  ? res.vertexBuffer
                              : item.dst == ResidentBuffer::Index ? res.indexBuffer
                                                                  : res.storageBuffer;
    cmd.CopyBufferToBuffer(item.src, 0, dst, item.dstOffset, item.size);
    if (item.dst == ResidentBuffer::Vertex) {
      vertexWritten.add(item.dstOffset, item.size);
    } else if (item.dst == ResidentBuffer::Storage) {
      storageWritten.add(item.dstOffset, item.size);
    }
  }
  frame.copied.bufferUploadCount = op.bufferUploads.size();

  if (g_vertexFetchTexture) {
    // Texture vertex fetch: the rows of the two buffers that the copies above wrote, after them
    // so the texture copies read what the buffer holds.
    detail::upload_fetch_texture_rows(cmd, StagingKind::Vertex, vertexWritten.lo, vertexWritten.hi);
    detail::upload_fetch_texture_rows(cmd, StagingKind::Storage, storageWritten.lo, storageWritten.hi);
  }

  if constexpr (UseTextureBuffer) {
    for (size_t i = frame.copied.textureUploadCount; i < op.textureUploads.size(); ++i) {
      const auto& item = *op.textureUploads[i];
      const wgpu::TexelCopyBufferInfo buf{
          .layout =
              wgpu::TexelCopyBufferLayout{
                  .offset = item.buffer ? item.layout.offset : item.layout.offset + TextureUploadStagingOffset,
                  .bytesPerRow = AURORA_ALIGN(item.layout.bytesPerRow, 256),
                  .rowsPerImage = item.layout.rowsPerImage,
              },
          .buffer = item.buffer ? item.buffer : staging_buffer(frame.stagingBuffer),
      };
      cmd.CopyBufferToTexture(&buf, &item.tex, &item.size);
    }
    frame.copied.textureUpload = highWater.textureUpload;
    frame.copied.textureUploadCount = op.textureUploads.size();
  }
}
} // namespace

namespace detail {
void encode_op(wgpu::CommandEncoder& cmd, FramePacket& frame, const FrameOp& op) {
  copy_staging_to_high_water(cmd, frame, op);
  switch (op.type) {
  case FrameOpType::RenderPass:
    if (op.renderPass != nullptr) {
      render(cmd, frame, *op.renderPass, op.index);
    }
    break;
  case FrameOpType::TextureCopy:
    if (op.textureCopy != nullptr) {
      const webgpu::gpu_prof::Zone zone{cmd, "Texture copy"};
      cmd.CopyTextureToTexture(&op.textureCopy->src, &op.textureCopy->dst, &op.textureCopy->size);
    }
    break;
  case FrameOpType::EncoderTask:
    if (op.encoderTask != nullptr) {
      execute_encoder_task(cmd, frame, *op.encoderTask);
    }
    break;
  }
}
} // namespace detail

bool bind_pipeline(PipelineRef ref, const wgpu::RenderPassEncoder& pass) {
  if (ref == g_currentPipeline) {
    return true;
  }
  wgpu::RenderPipeline pipeline;
  if (!get_pipeline(ref, pipeline)) {
    note_skipped_draw();
    return false;
  }
  pass.SetPipeline(pipeline);
  g_currentPipeline = ref;
  return true;
}

void bind_texture_group(BindGroupRef ref, const wgpu::RenderPassEncoder& pass) {
  if (ref == g_currentTextureGroup) {
    return;
  }
  pass.SetBindGroup(2, find_bind_group(ref));
  g_currentTextureGroup = ref;
}

void forget_texture_group() { g_currentTextureGroup = 0; }
} // namespace aurora::gfx
