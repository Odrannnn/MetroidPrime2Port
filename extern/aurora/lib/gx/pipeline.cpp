#include "pipeline.hpp"

#include "../gfx/encoding.hpp"
#include "../gfx/resources.hpp"
#include "../gfx/pipeline_cache.hpp"
#include "../gfx/resource_cache.hpp"

#include "gx_fmt.hpp"
#include "shader_info.hpp"
#include "../logging.hpp"
#include "../webgpu/gpu.hpp"

#include <tracy/Tracy.hpp>

static aurora::Module Log("aurora::gx::pipeline");

namespace aurora::gx {

wgpu::RenderPipeline create_pipeline(const PipelineConfig& config) {
  if (!pipeline_config_compatible(config, webgpu::g_lightmapBinding)) {
    Log.error("Refusing GX pipeline: shadow pass {} (shader shadow {}), PBR lightmap attribute {} (lightmap "
              "binding {})",
              config.shadowPass, static_cast<int>(config.shaderConfig.shadow), config.shaderConfig.pbrLightmapAttr,
              webgpu::g_lightmapBinding);
    return {};
  }

  ZoneScoped;
  auto dstAlphaMode = dst_alpha_mode(config);
  const auto shader = build_shader(config.shaderConfig, dstAlphaMode);
  const auto label =
      fmt::format("GX Pipeline {:x} shader {:x}", xxh3_hash(config, static_cast<HashType>(gfx::ShaderType::GX)),
                  xxh3_hash(config.shaderConfig));
  return build_pipeline(config, {}, shader, label.c_str(), dstAlphaMode);
}

void render(const DrawData& data, const wgpu::RenderPassEncoder& pass) {
  if (!gfx::bind_pipeline(data.pipeline, pass)) {
    return;
  }

  const auto& resources = gfx::detail::resources();
  pass.SetImmediates(0, &data.immediateData, sizeof(data.immediateData));
  const std::array offsets{data.uniformRange.offset};
  pass.SetBindGroup(1, resources.uniformBindGroup, offsets.size(), offsets.data());
  // A shadow receiver's group 2 has its own layout, so a draw without a group of its own can't
  // inherit it. A stale flag (a pass starts with the empty group bound) only costs a rebind.
  static bool sShadowGroupBound = false;
  if (data.bindGroups.textureBindGroup) {
    gfx::bind_texture_group(data.bindGroups.textureBindGroup, pass);
    sShadowGroupBound = data.shadowGroup;
  } else if (sShadowGroupBound) {
    pass.SetBindGroup(2, g_emptyTextureBindGroup);
    gfx::forget_texture_group();
    sShadowGroupBound = false;
  }
  pass.SetIndexBuffer(resources.indexBuffer, wgpu::IndexFormat::Uint16, data.idxRange.offset, data.idxRange.size);
  if (data.dstAlpha != UINT32_MAX) {
    const wgpu::Color color{0.f, 0.f, 0.f, data.dstAlpha / 255.f};
    pass.SetBlendConstant(&color);
  }
  if (data.indexCount == 0) {
    pass.Draw(data.vtxCount, data.instanceCount);
  } else {
    pass.DrawIndexed(data.indexCount, data.instanceCount);
  }
}

} // namespace aurora::gx
