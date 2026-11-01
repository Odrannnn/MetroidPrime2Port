#include "gx/pipeline.hpp"

#include <gtest/gtest.h>

TEST(GXPipelineCompatibility, PbrLightmapRequiresSupportedBinding) {
  for (const bool pbr : {false, true}) {
    for (const bool hasLightmapAttribute : {false, true}) {
      for (const bool lightmapBinding : {false, true}) {
        aurora::gx::PipelineConfig config{};
        config.shaderConfig.pbr = pbr;
        config.shaderConfig.pbrLightmapAttr = hasLightmapAttribute ? GX_VA_TEX0 : GX_VA_NULL;

        const bool expected = !pbr || !hasLightmapAttribute || lightmapBinding;
        SCOPED_TRACE(::testing::Message() << "pbr=" << pbr << ", hasLightmapAttribute=" << hasLightmapAttribute
                                          << ", lightmapBinding=" << lightmapBinding);
        EXPECT_EQ(aurora::gx::pipeline_config_compatible(config, lightmapBinding), expected);
      }
    }
  }
}

TEST(GXPipelineCompatibility, ShadowPassRequiresShadowShader) {
  aurora::gx::PipelineConfig config{};
  EXPECT_TRUE(aurora::gx::pipeline_config_compatible(config, true));
  config.shadowPass = 1;
  EXPECT_FALSE(aurora::gx::pipeline_config_compatible(config, true));
  config.shaderConfig.shadow = true;
  EXPECT_TRUE(aurora::gx::pipeline_config_compatible(config, true));
  config.shadowPass = 2;
  EXPECT_TRUE(aurora::gx::pipeline_config_compatible(config, true));
  config.shadowPass = 3;
  EXPECT_FALSE(aurora::gx::pipeline_config_compatible(config, true));
  config.shadowPass = 1;
  config.shaderConfig.lineMode = 1;
  EXPECT_FALSE(aurora::gx::pipeline_config_compatible(config, true));
}
