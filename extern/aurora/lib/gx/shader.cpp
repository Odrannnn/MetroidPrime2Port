#include "../gfx/hash.hpp"
#include "../gfx/hdr_tone.hpp"
#include "../gfx/probe.hpp"
#include "../gfx/resources.hpp"
#include "../gfx/types.hpp"

#include "../internal.hpp"
#include "../webgpu/gpu.hpp"
#include "gx.hpp"
#include "gx_fmt.hpp"
#include "shader_info.hpp"

#include <dolphin/gx/GXEnum.h>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include "tracy/Tracy.hpp"

namespace aurora::gx {
using namespace fmt::literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {
constexpr Module Log{"aurora::gfx::gx"};

// Position of a mandatory anchor in the generated WGSL. A missing one means the text it patches changed under
// us; abort naming it rather than let replace/insert(npos) throw inside the noexcept source builder.
// last = search from the end (rfind).
size_t find_anchor(const std::string& source, std::string_view what, bool last = false) {
  const size_t at = last ? source.rfind(what) : source.find(what);
  if (at == std::string::npos) {
    Log.fatal("shader source: anchor not found: '{}'", what);
  }
  return at;
}

// The inventory visor kinds (36: Remastered's coloured coverage glass 4c37625c, 37: the visor backlight 17dd0a37).
// The scalar expressions the shader emits, over the lets it declares: inv_ba = BCLR.a, inv_da = DIFC.a, inv_c = COVG,
// inv_b = BCLR.rgb x DIFC.rgb (per channel), inv_a = the opacity. Exported (inventory_visor_expr) so the tests run
// the very text the shader contains.
constexpr std::string_view InvAlphaExpr = "clamp(inv_ba * inv_ba * inv_da, 0.0, 1.0)";
// MFC0 on (O2, One / InvSrc1Color): the scene keeps 1 - C + C b (1 - A).
constexpr std::string_view InvWeightExpr = "vec3f(1.0 - inv_c) + inv_c * inv_b * (1.0 - inv_a)";

// Adreno 730 (Vulkan, driver V@0615.98) drops every GX draw whose fragment shader reads a
// varying when load_word branches on arrayLength, so the world renders black (issue #7).
// An Adreno 710 shows the same black world on some drivers. A clamp works there; it only
// differs from the branch on out-of-range reads, so Auto takes it for every Adreno 7xx.
// The bug needs the game's draw state (a standalone self-test of the same shader passes),
// so there is no probe for it.
bool clamp_storage_loads() noexcept {
  static const bool clamp = [] {
    // MP2_STORAGE_CLAMP=1/0 forces it on/off; unset = Vulkan on an Adreno 7xx.
    if (const char* force = std::getenv("MP2_STORAGE_CLAMP"); force != nullptr && *force != '\0') {
      return *force == '1';
    }
    if (webgpu::g_backendType != wgpu::BackendType::Vulkan) {
      return false;
    }
    // Device names look like "Adreno (TM) 730": the model is the first number after "Adreno".
    const std::string_view device{webgpu::g_adapterInfo.device};
    const size_t adreno = device.find("Adreno");
    if (adreno == std::string_view::npos) {
      return false;
    }
    const size_t model = device.find_first_of("0123456789", adreno);
    return model != std::string_view::npos && device[model] == '7' && model + 2 < device.size() &&
           device[model + 1] >= '0' && device[model + 1] <= '9' && device[model + 2] >= '0' && device[model + 2] <= '9';
  }();
  return clamp;
}

std::string_view chan_comp(GXTevColorChan chan) noexcept {
  switch (chan) {
  case GX_CH_RED:
    return "r";
  case GX_CH_GREEN:
    return "g";
  case GX_CH_BLUE:
    return "b";
  case GX_CH_ALPHA:
    return "a";
  default:
    return "?";
  }
}

bool is_alpha_bump_channel(GXChannelID id) noexcept { return id == GX_ALPHA_BUMP || id == GX_ALPHA_BUMPN; }

std::string tev_mask_expr(const std::string& value, u32 mask) {
  // t_IndTexCoord is already expanded into the 0..255 indirect sample domain.
  return fmt::format("(f32(u32({}) & 0x{:X}u) / 255.0)", value, mask);
}

std::string alpha_bump_sel(size_t stageIdx, const ShaderConfig& config, const TevStage& stage) {
  if (stage.indTexStage >= config.numIndStages || stage.indTexAlphaSel == GX_ITBA_OFF) {
    return "0.0";
  }

  std::string baseCoord;
  switch (stage.indTexAlphaSel) {
    DEFAULT_FATAL("invalid indTexAlphaSel {} for stage {}", underlying(stage.indTexAlphaSel), stageIdx);
  case GX_ITBA_S:
    baseCoord = fmt::format("t_IndTexCoord{}.x", underlying(stage.indTexStage));
    break;
  case GX_ITBA_T:
    baseCoord = fmt::format("t_IndTexCoord{}.y", underlying(stage.indTexStage));
    break;
  case GX_ITBA_U:
    baseCoord = fmt::format("t_IndTexCoord{}.z", underlying(stage.indTexStage));
    break;
  case GX_ITBA_OFF:
    return "0.0";
  }

  switch (stage.indTexFormat) {
    DEFAULT_FATAL("invalid indirect format {} for stage {}", underlying(stage.indTexFormat), stageIdx);
  case GX_ITF_8:
    return tev_mask_expr(baseCoord, 0xF8u);
  case GX_ITF_5:
    return tev_mask_expr(baseCoord, 0xE0u);
  case GX_ITF_4:
    return tev_mask_expr(baseCoord, 0xF0u);
  case GX_ITF_3:
    return tev_mask_expr(baseCoord, 0xF8u);
  }
}

bool uses_texture_sample(const TevStage& stage) noexcept {
  if (stage.texMapId == GX_TEXMAP_NULL) {
    return false;
  }
  const auto& c = stage.colorPass;
  const auto& a = stage.alphaPass;
  return c.a == GX_CC_TEXC || c.a == GX_CC_TEXA || c.b == GX_CC_TEXC || c.b == GX_CC_TEXA || c.c == GX_CC_TEXC ||
         c.c == GX_CC_TEXA || c.d == GX_CC_TEXC || c.d == GX_CC_TEXA || a.a == GX_CA_TEXA || a.b == GX_CA_TEXA ||
         a.c == GX_CA_TEXA || a.d == GX_CA_TEXA;
}

std::string color_arg_reg(GXTevColorArg arg, size_t stageIdx, const ShaderConfig& config, const TevStage& stage) {
  switch (arg) {
    DEFAULT_FATAL("invalid color arg {}", underlying(arg));
  case GX_CC_CPREV:
    return "prev.rgb";
  case GX_CC_APREV:
    return "vec3f(prev.a)";
  case GX_CC_C0:
    return "tevreg0.rgb";
  case GX_CC_A0:
    return "vec3f(tevreg0.a)";
  case GX_CC_C1:
    return "tevreg1.rgb";
  case GX_CC_A1:
    return "vec3f(tevreg1.a)";
  case GX_CC_C2:
    return "tevreg2.rgb";
  case GX_CC_A2:
    return "vec3f(tevreg2.a)";
  case GX_CC_TEXC: {
    if (stage.texMapId == GX_TEXMAP_NULL) {
      return "vec3f(1.0)";
    }
    CHECK(stage.texMapId >= GX_TEXMAP0 && stage.texMapId <= GX_TEXMAP7, "invalid texture {} for stage {}",
          underlying(stage.texMapId), stageIdx);
    const auto& swap = config.tevSwapTable[stage.tevSwapTex];
    return fmt::format("sampled{}.{}{}{}", stageIdx, chan_comp(swap.red), chan_comp(swap.green), chan_comp(swap.blue));
  }
  case GX_CC_TEXA: {
    if (stage.texMapId == GX_TEXMAP_NULL) {
      return "vec3f(1.0)";
    }
    CHECK(stage.texMapId >= GX_TEXMAP0 && stage.texMapId <= GX_TEXMAP7, "invalid texture {} for stage {}",
          underlying(stage.texMapId), stageIdx);
    const auto& swap = config.tevSwapTable[stage.tevSwapTex];
    return fmt::format("vec3f(sampled{}.{})", stageIdx, chan_comp(swap.alpha));
  }
  case GX_CC_RASC: {
    // CHECK(stage.channelId != GX_COLOR_NULL, "unmapped color channel for stage {}", stageIdx);
    if (stage.channelId == GX_COLOR_ZERO || stage.channelId == GX_COLOR_NULL) {
      return "vec3f(0.0)";
    }
    if (is_alpha_bump_channel(stage.channelId)) {
      std::string alpha = alpha_bump_sel(stageIdx, config, stage);
      if (stage.channelId == GX_ALPHA_BUMPN) {
        alpha = fmt::format("({} * (255.0 / 248.0))", alpha);
      }
      return fmt::format("vec3f({})", alpha);
    }
    u32 idx = color_channel(stage.channelId);
    const auto& swap = config.tevSwapTable[stage.tevSwapRas];
    return fmt::format("rast{}.{}{}{}", idx, chan_comp(swap.red), chan_comp(swap.green), chan_comp(swap.blue));
  }
  case GX_CC_RASA: {
    // CHECK(stage.channelId != GX_COLOR_NULL, "unmapped color channel for stage {}", stageIdx);
    if (stage.channelId == GX_COLOR_ZERO || stage.channelId == GX_COLOR_NULL) {
      return "vec3f(0.0)";
    }
    if (is_alpha_bump_channel(stage.channelId)) {
      std::string alpha = alpha_bump_sel(stageIdx, config, stage);
      if (stage.channelId == GX_ALPHA_BUMPN) {
        alpha = fmt::format("({} * (255.0 / 248.0))", alpha);
      }
      return fmt::format("vec3f({})", alpha);
    }
    u32 idx = color_channel(stage.channelId);
    const auto& swap = config.tevSwapTable[stage.tevSwapRas];
    return fmt::format("vec3f(rast{}.{})", idx, chan_comp(swap.alpha));
  }
  case GX_CC_ONE:
    return "vec3f(1.0)";
  case GX_CC_HALF:
    return "vec3f(0.5)";
  case GX_CC_KONST:
    return fmt::format("ubuf.tev_konst[{}].rgb", stageIdx); // tev_konst (ShaderInfo::usesTevKonst)
  case GX_CC_ZERO:
    return "vec3f(0.0)";
  }
}

std::string alpha_arg_reg(GXTevAlphaArg arg, size_t stageIdx, const ShaderConfig& config, const TevStage& stage) {
  switch (arg) {
    DEFAULT_FATAL("invalid alpha arg {}", underlying(arg));
  case GX_CA_APREV:
    return "prev.a";
  case GX_CA_A0:
    return "tevreg0.a";
  case GX_CA_A1:
    return "tevreg1.a";
  case GX_CA_A2:
    return "tevreg2.a";
  case GX_CA_TEXA: {
    if (stage.texMapId == GX_TEXMAP_NULL) {
      return "1.0";
    }
    CHECK(stage.texMapId >= GX_TEXMAP0 && stage.texMapId <= GX_TEXMAP7, "invalid texture {} for stage {}",
          underlying(stage.texMapId), stageIdx);
    const auto& swap = config.tevSwapTable[stage.tevSwapTex];
    return fmt::format("sampled{}.{}", stageIdx, chan_comp(swap.alpha));
  }
  case GX_CA_RASA: {
    // CHECK(stage.channelId != GX_COLOR_NULL, "unmapped color channel for stage {}", stageIdx);
    if (stage.channelId == GX_COLOR_ZERO || stage.channelId == GX_COLOR_NULL) {
      return "0.0";
    }
    if (is_alpha_bump_channel(stage.channelId)) {
      std::string alpha = alpha_bump_sel(stageIdx, config, stage);
      if (stage.channelId == GX_ALPHA_BUMPN) {
        alpha = fmt::format("({} * (255.0 / 248.0))", alpha);
      }
      return alpha;
    }
    u32 idx = color_channel(stage.channelId);
    const auto& swap = config.tevSwapTable[stage.tevSwapRas];
    return fmt::format("rast{}.{}", idx, chan_comp(swap.alpha));
  }
  case GX_CA_KONST:
    return fmt::format("ubuf.tev_konst[{}].a", stageIdx);
  case GX_CA_ZERO:
    return "0.0";
  }
}

bool tev_color_arg_is_normalized(GXTevColorArg arg, const std::array<bool, MaxTevRegs>& colorNormalized,
                                 const std::array<bool, MaxTevRegs>& alphaNormalized) {
  switch (arg) {
  case GX_CC_CPREV:
    return colorNormalized[GX_TEVPREV];
  case GX_CC_APREV:
    return alphaNormalized[GX_TEVPREV];
  case GX_CC_C0:
    return colorNormalized[GX_TEVREG0];
  case GX_CC_A0:
    return alphaNormalized[GX_TEVREG0];
  case GX_CC_C1:
    return colorNormalized[GX_TEVREG1];
  case GX_CC_A1:
    return alphaNormalized[GX_TEVREG1];
  case GX_CC_C2:
    return colorNormalized[GX_TEVREG2];
  case GX_CC_A2:
    return alphaNormalized[GX_TEVREG2];
  default:
    return true;
  }
}

bool tev_alpha_arg_is_normalized(GXTevAlphaArg arg, const std::array<bool, MaxTevRegs>& alphaNormalized) {
  switch (arg) {
  case GX_CA_APREV:
    return alphaNormalized[GX_TEVPREV];
  case GX_CA_A0:
    return alphaNormalized[GX_TEVREG0];
  case GX_CA_A1:
    return alphaNormalized[GX_TEVREG1];
  case GX_CA_A2:
    return alphaNormalized[GX_TEVREG2];
  default:
    return true;
  }
}

std::string tev_op(GXTevOp op, std::string_view bias, std::string_view scale, std::string_view a, std::string_view b,
                   std::string_view c, std::string_view d, std::string_view zero) {
  switch (op) {
    DEFAULT_FATAL("unimplemented tev op {}", underlying(op));
  case GX_TEV_ADD:
  case GX_TEV_SUB: {
    std::string_view neg = op == GX_TEV_SUB ? "-"sv : ""sv;
    return fmt::format("(({0}mix({1}, {2}, {3}) + {4}){5}){6}", neg, a, b, c, d, bias, scale);
  }
  case GX_TEV_COMP_R8_GT:
    return fmt::format("select({3}, {2}, round({0}.r * 255.0) > round({1}.r * 255.0)) + {4}", a, b, c, zero, d);
  case GX_TEV_COMP_R8_EQ:
    return fmt::format("select({3}, {2}, round({0}.r * 255.0) == round({1}.r * 255.0)) + {4}", a, b, c, zero, d);
  case GX_TEV_COMP_GR16_GT:
    return fmt::format(
        "select({3}, {2}, round(dot({0}.rg * 255.0, vec2(1.0, 256.0))) > round(dot({1}.rg * 255.0, vec2(1.0, 256.0))))"
        " + {4}",
        a, b, c, zero, d);
  case GX_TEV_COMP_GR16_EQ:
    return fmt::format(
        "select({3}, {2}, round(dot({0}.rg * 255.0, vec2(1.0, 256.0))) == round(dot({1}.rg * 255.0, vec2(1.0, 256.0))))"
        " + {4}",
        a, b, c, zero, d);
  case GX_TEV_COMP_BGR24_GT:
    return fmt::format(
        "select({3}, {2}, round(dot({0}.rgb * 255.0, vec3(1.0, 256.0, 65536.0))) > round(dot({1}.rgb * 255.0, "
        "vec3(1.0, 256.0, 65536.0)))) + {4}",
        a, b, c, zero, d);
  case GX_TEV_COMP_BGR24_EQ:
    return fmt::format(
        "select({3}, {2}, round(dot({0}.rgb * 255.0, vec3(1.0, 256.0, 65536.0))) == round(dot({1}.rgb * 255.0, "
        "vec3(1.0, 256.0, 65536.0)))) + {4}",
        a, b, c, zero, d);
  case GX_TEV_COMP_RGB8_GT:
    return fmt::format("select({3}, {2}, round({0} * 255.0) > round({1} * 255.0)) + {4}", a, b, c, zero, d);
  case GX_TEV_COMP_RGB8_EQ:
    return fmt::format("select({3}, {2}, round({0} * 255.0) == round({1} * 255.0)) + {4}", a, b, c, zero, d);
  }
}

std::string tev_color_op(GXTevOp op, std::string_view bias, std::string_view scale, bool clamp, std::string_view a,
                         std::string_view b, std::string_view c, std::string_view d) {
  std::string expr = tev_op(op, bias, scale, a, b, c, d, "vec3(0)"sv);
  return clamp ? fmt::format("clamp({}, vec3f(0.0), vec3f(1.0))", expr)
               : fmt::format("clamp({}, vec3f(-4.0), vec3f(4.0))", expr);
}

std::string tev_alpha_op(GXTevOp op, std::string_view bias, std::string_view scale, bool clamp, std::string_view a,
                         std::string_view b, std::string_view c, std::string_view d) {
  std::string expr = tev_op(op, bias, scale, a, b, c, d, "0.0"sv);
  return clamp ? fmt::format("clamp({}, 0.0, 1.0)", expr) : fmt::format("clamp({}, -4.0, 4.0)", expr);
}

std::string_view tev_bias(GXTevBias bias) {
  switch (bias) {
    DEFAULT_FATAL("invalid tev bias {}", underlying(bias));
  case GX_TB_ZERO:
    return ""sv;
  case GX_TB_ADDHALF:
    return " + 0.5"sv;
  case GX_TB_SUBHALF:
    return " - 0.5"sv;
  }
}

struct AlphaCompareExpr {
  std::string expr;
  int constant = -1;
};

AlphaCompareExpr alpha_compare_const(bool value) { return {value ? "true"s : "false"s, value ? 1 : 0}; }

AlphaCompareExpr alpha_compare_not(const AlphaCompareExpr& expr) {
  if (expr.constant != -1) {
    return alpha_compare_const(expr.constant == 0);
  }
  return {fmt::format("!{}", expr.expr), -1};
}

AlphaCompareExpr alpha_compare_and(const AlphaCompareExpr& lhs, const AlphaCompareExpr& rhs) {
  if (lhs.constant == 0 || rhs.constant == 0) {
    return alpha_compare_const(false);
  }
  if (lhs.constant == 1) {
    return rhs;
  }
  if (rhs.constant == 1) {
    return lhs;
  }
  return {fmt::format("({} && {})", lhs.expr, rhs.expr), -1};
}

AlphaCompareExpr alpha_compare_or(const AlphaCompareExpr& lhs, const AlphaCompareExpr& rhs) {
  if (lhs.constant == 1 || rhs.constant == 1) {
    return alpha_compare_const(true);
  }
  if (lhs.constant == 0) {
    return rhs;
  }
  if (rhs.constant == 0) {
    return lhs;
  }
  return {fmt::format("({} || {})", lhs.expr, rhs.expr), -1};
}

AlphaCompareExpr alpha_compare_xor(const AlphaCompareExpr& lhs, const AlphaCompareExpr& rhs) {
  if (lhs.constant != -1 && rhs.constant != -1) {
    return alpha_compare_const(lhs.constant != rhs.constant);
  }
  if (lhs.constant == 0) {
    return rhs;
  }
  if (rhs.constant == 0) {
    return lhs;
  }
  if (lhs.constant == 1) {
    return alpha_compare_not(rhs);
  }
  if (rhs.constant == 1) {
    return alpha_compare_not(lhs);
  }
  return {fmt::format("({} != {})", lhs.expr, rhs.expr), -1};
}

AlphaCompareExpr alpha_compare_xnor(const AlphaCompareExpr& lhs, const AlphaCompareExpr& rhs) {
  if (lhs.constant != -1 && rhs.constant != -1) {
    return alpha_compare_const(lhs.constant == rhs.constant);
  }
  if (lhs.constant == 0) {
    return alpha_compare_not(rhs);
  }
  if (rhs.constant == 0) {
    return alpha_compare_not(lhs);
  }
  if (lhs.constant == 1) {
    return rhs;
  }
  if (rhs.constant == 1) {
    return lhs;
  }
  return {fmt::format("({} == {})", lhs.expr, rhs.expr), -1};
}

// ref is the pipeline's (pipeline_alpha_ref): AlphaRefUniform reads it from alpha_ref.x (comp0) or .y.
AlphaCompareExpr alpha_compare(GXCompare comp, u32 ref, char slot) {
  const auto iref = ref == AlphaRefUniform ? fmt::format("ubuf.alpha_ref.{}", slot) : fmt::format("{}u", ref);
  switch (comp) {
    DEFAULT_FATAL("invalid alpha comp {}", underlying(comp));
  case GX_NEVER:
    return alpha_compare_const(false);
  case GX_LESS:
    if (ref == 0) {
      return alpha_compare_const(false);
    }
    return {fmt::format("(alphaCompare < {})", iref), -1};
  case GX_LEQUAL:
    if (ref == 255) {
      return alpha_compare_const(true);
    }
    return {fmt::format("(alphaCompare <= {})", iref), -1};
  case GX_EQUAL:
    return {fmt::format("(alphaCompare == {})", iref), -1};
  case GX_NEQUAL:
    return {fmt::format("(alphaCompare != {})", iref), -1};
  case GX_GEQUAL:
    if (ref == 0) {
      return alpha_compare_const(true);
    }
    return {fmt::format("(alphaCompare >= {})", iref), -1};
  case GX_GREATER:
    if (ref == 255) {
      return alpha_compare_const(false);
    }
    return {fmt::format("(alphaCompare > {})", iref), -1};
  case GX_ALWAYS:
    return alpha_compare_const(true);
  }
}

// When the alpha compare drops a pixel, in terms of alphaCompare (the alpha as 0-255).
AlphaCompareExpr alpha_compare_discard(const ShaderConfig& config) {
  const auto comp0 = alpha_compare(config.alphaCompare.comp0, config.alphaCompare.ref0, 'x');
  const auto comp1 = alpha_compare(config.alphaCompare.comp1, config.alphaCompare.ref1, 'y');
  AlphaCompareExpr pass;
  switch (config.alphaCompare.op) {
    DEFAULT_FATAL("invalid alpha compare op {}", underlying(config.alphaCompare.op));
  case GX_AOP_AND:
    pass = alpha_compare_and(comp0, comp1);
    break;
  case GX_AOP_OR:
    pass = alpha_compare_or(comp0, comp1);
    break;
  case GX_AOP_XOR:
    pass = alpha_compare_xor(comp0, comp1);
    break;
  case GX_AOP_XNOR:
    pass = alpha_compare_xnor(comp0, comp1);
    break;
  }
  return alpha_compare_not(pass);
}

std::string_view tev_scale(GXTevScale scale) {
  switch (scale) {
    DEFAULT_FATAL("invalid tev scale {}", underlying(scale));
  case GX_CS_SCALE_1:
    return ""sv;
  case GX_CS_SCALE_2:
    return " * 2.0"sv;
  case GX_CS_SCALE_4:
    return " * 4.0"sv;
  case GX_CS_DIVIDE_2:
    return " / 2.0"sv;
  }
}

std::string vtx_attr(const ShaderConfig& config, GXAttr attr) {
  const auto type = config.attrs[attr].attrType;
  if (type == GX_NONE) {
    if (attr == GX_VA_PNMTXIDX) {
      return "imm.current_pnmtx";
    }
    if (attr == GX_VA_NRM) {
      // Default normal
      return "vec3f(1.0, 0.0, 0.0)"s;
    }
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
      // A channel reading a colour the vertices don't carry gets opaque white, as Dolphin's
      // missing colour value does (0xFFFFFFFF). The game sets only GX_COLOR1's control for its
      // modulation channel (CGX::FlushState), so GX_ALPHA1 keeps GXInit's GX_SRC_VTX source, and
      // a zero there made every modulated particle (boost ball glow, coloured inner glows)
      // invisible.
      return "vec4f(1.0, 1.0, 1.0, 1.0)"s;
    }
    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
      // A texgen reading a UV set the vertices don't carry (seen with a mod model): draw
      // it with zero UVs rather than abort the game. Shaders are cached, so this logs once
      // per shader.
      Log.warn("unmapped vtx attr {} (texcoord {}), using zero UVs", underlying(attr), attr - GX_VA_TEX0);
      return "vec2f(0.0, 0.0)"s;
    }
    // Port: a draw can bind a texture (so the shader needs a texcoord) while its vertex
    // descriptor has no such attribute - the water plane's grid strips do exactly that, using
    // texgen from the position, and retail's GX simply reads whatever the vertex format leaves
    // there. Aurora made that a fatal, which turns a visible-but-odd draw into a dead port.
    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
      return config.attrs[attr].cnt == GX_TEX_S ? "float(0.0)"s : "vec2f(0.0, 0.0)"s;
    }
    UNLIKELY FATAL("unmapped vtx attr {}", underlying(attr));
  }
  if (attr == GX_VA_POS) {
    return "in_pos"s;
  }
  if (attr == GX_VA_NRM) {
    return "in_nrm"s;
  }
  if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
    const auto idx = attr - GX_VA_CLR0;
    return fmt::format("in_clr{}", idx);
  }
  if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
    const auto idx = attr - GX_VA_TEX0;
    return fmt::format("in_tex{}_uv", idx);
  }
  if (attr == GX_VA_PNMTXIDX) {
    return "in_pnmtxidx"s;
  }
  if (attr >= GX_VA_TEX0MTXIDX && attr <= GX_VA_TEX7MTXIDX) {
    const auto idx = attr - GX_VA_TEX0MTXIDX;
    return fmt::format("in_texmtxidx{}", idx);
  }
  UNLIKELY FATAL("unhandled vtx attr {}", underlying(attr));
}

constexpr std::array<std::string_view, GX_CC_ZERO + 1> TevColorArgNames{
    "CPREV"sv, "APREV"sv, "C0"sv,   "A0"sv,   "C1"sv,  "A1"sv,   "C2"sv,    "A2"sv,
    "TEXC"sv,  "TEXA"sv,  "RASC"sv, "RASA"sv, "ONE"sv, "HALF"sv, "KONST"sv, "ZERO"sv,
};
constexpr std::array<std::string_view, GX_CA_ZERO + 1> TevAlphaArgNames{
    "APREV"sv, "A0"sv, "A1"sv, "A2"sv, "TEXA"sv, "RASA"sv, "KONST"sv, "ZERO"sv,
};

auto fetch_fixed16_attr(std::string_view fetchFn, const AttrConfig& mapping, std::string_view buf,
                        std::string_view offs, bool le) -> std::string {
  // Some Adreno drivers appear sensitive to generated shaders that route 2- and
  // 3-component fixed-16 vertex attributes through reusable vector fetch helpers.
  // Emitting scalar component fetches at the call site avoids observed artifacts.
  if (mapping.cnt == 2) {
    const auto comp0 = fmt::format("{}_1(&{}, {} + 0u, {}u, {})", fetchFn, buf, offs, mapping.frac, le);
    const auto comp1 = fmt::format("{}_1(&{}, {} + 2u, {}u, {})", fetchFn, buf, offs, mapping.frac, le);
    return fmt::format("vec2f({}, {})", comp0, comp1);
  }
  if (mapping.cnt == 3) {
    const auto comp0 = fmt::format("{}_1(&{}, {} + 0u, {}u, {})", fetchFn, buf, offs, mapping.frac, le);
    const auto comp1 = fmt::format("{}_1(&{}, {} + 2u, {}u, {})", fetchFn, buf, offs, mapping.frac, le);
    const auto comp2 = fmt::format("{}_1(&{}, {} + 4u, {}u, {})", fetchFn, buf, offs, mapping.frac, le);
    return fmt::format("vec3f({}, {}, {})", comp0, comp1, comp2);
  }
  return fmt::format("{}_{}(&{}, {}, {}u, {})", fetchFn, mapping.cnt, buf, offs, mapping.frac, le);
}

auto fetch_attr(const AttrConfig& mapping, std::string_view buf, std::string_view offs, bool le) -> std::string {
  switch (mapping.compType) {
  case GX_U8:
    return fmt::format("fetch_u8_{}(&{}, {}, {}, {})", mapping.cnt, buf, offs, mapping.frac, le);
  case GX_S8:
    return fmt::format("fetch_s8_{}(&{}, {}, {}, {})", mapping.cnt, buf, offs, mapping.frac, le);
  case GX_U16:
    return fetch_fixed16_attr("fetch_u16"sv, mapping, buf, offs, le);
  case GX_S16:
    return fetch_fixed16_attr("fetch_s16"sv, mapping, buf, offs, le);
  case GX_F32:
    return fmt::format("fetch_f32_{}(&{}, {}, {})", mapping.cnt, buf, offs, le);
  case GX_RGBA8:
    return fmt::format("unpack4x8unorm(load_u32_raw(&{}, {}))", buf, offs);
  default:
    Log.fatal("fetch_attr: Unimplemented {}", static_cast<GXCompType>(mapping.compType));
  }
}

auto fetch_color_attr(const AttrConfig& mapping, std::string_view buf, std::string_view offs, bool le) -> std::string {
  switch (mapping.compType) {
  case GX_RGB565:
    return fmt::format("fetch_rgb565(&{}, {}, {})", buf, offs, le);
  case GX_RGB8:
    return fmt::format("fetch_rgb8(&{}, {}, {})", buf, offs, le);
  case GX_RGBX8:
    return fmt::format("fetch_rgbx8(&{}, {}, {})", buf, offs, le);
  case GX_RGBA4:
    return fmt::format("fetch_rgba4(&{}, {}, {})", buf, offs, le);
  case GX_RGBA6:
    return fmt::format("fetch_rgba6(&{}, {}, {})", buf, offs, le);
  case GX_RGBA8:
    return fmt::format("fetch_rgba8(&{}, {}, {})", buf, offs, le);
  default:
    Log.fatal("fetch_color_attr: Unimplemented {}", static_cast<GXCompType>(mapping.compType));
  }
}

struct AttrAddress {
  std::string offs;
  std::string_view buf;
  bool le;
};

// Immediates cannot contain arrays, so array_start is packed as three vec4u.
std::string imm_array_start(GXAttr attr) noexcept {
  const u32 idx = attr - GX_VA_POS;
  return fmt::format("imm.array_start{}.{}", idx / 4, "xyzw"[idx % 4]);
}

auto attr_address(const AttrConfig& mapping, GXAttr attr, std::string_view vidx, u32 vtxStride, u32 dlExtra, u32 within)
    -> AttrAddress {
  const u32 dlOffset = mapping.offset + dlExtra;
  if (mapping.attrType == GX_INDEX8) {
    return {fmt::format("{} + raw_fetch_u8_1(&vbuf, imm.vtx_start + {} * {}u + {}u) * {}u + {}u", imm_array_start(attr),
                        vidx, vtxStride, dlOffset, mapping.stride, within),
            "abuf"sv, mapping.le};
  }
  if (mapping.attrType == GX_INDEX16) {
    return {fmt::format("{} + raw_fetch_u16_1(&vbuf, imm.vtx_start + {} * {}u + {}u, false) * {}u + {}u",
                        imm_array_start(attr), vidx, vtxStride, dlOffset, mapping.stride, within),
            "abuf"sv, mapping.le};
  }
  return {fmt::format("imm.vtx_start + {} * {}u + {}u", vidx, vtxStride, dlOffset + within), "vbuf"sv, false};
}

auto attr_load(const ShaderConfig& config, GXAttr attr, std::string_view vidx) -> std::string {
  const auto& mapping = config.attrs[attr];
  if (mapping.attrType == GX_NONE) {
    return vtx_attr(config, attr);
  }
  const auto [offs, buf, le] = attr_address(mapping, attr, vidx, config.vtxStride, 0u, 0u);
  switch (attr) {
  case GX_VA_PNMTXIDX:
    return fmt::format("(raw_fetch_u8_1(&{}, {}) / 3u)", buf, offs);
  case GX_VA_TEX0MTXIDX:
  case GX_VA_TEX1MTXIDX:
  case GX_VA_TEX2MTXIDX:
  case GX_VA_TEX3MTXIDX:
  case GX_VA_TEX4MTXIDX:
  case GX_VA_TEX5MTXIDX:
  case GX_VA_TEX6MTXIDX:
  case GX_VA_TEX7MTXIDX:
    return fmt::format("raw_fetch_u8_1(&{}, {})", buf, offs);
  case GX_VA_POS: {
    const auto posLoad = fetch_attr(mapping, buf, offs, le);
    if (mapping.cnt == 2) {
      return fmt::format("vec3f({}, 0.0)", posLoad);
    }
    return posLoad;
  }
  case GX_VA_NRM:
    // NBT: normal only here; binormal/tangent loaded via attr_load_nbt_slice
    if (mapping.cnt > 3) {
      auto nrmMapping = mapping;
      nrmMapping.cnt = 3;
      return fetch_attr(nrmMapping, buf, offs, le);
    }
    return fetch_attr(mapping, buf, offs, le);
  case GX_VA_CLR0:
  case GX_VA_CLR1:
    return fetch_color_attr(mapping, buf, offs, le);
  case GX_VA_TEX0:
  case GX_VA_TEX1:
  case GX_VA_TEX2:
  case GX_VA_TEX3:
  case GX_VA_TEX4:
  case GX_VA_TEX5:
  case GX_VA_TEX6:
  case GX_VA_TEX7: {
    const auto texLoad = fetch_attr(mapping, buf, offs, le);
    if (mapping.cnt == 1) {
      return fmt::format("vec2f({}, 0.0)", texLoad);
    }
    return texLoad;
  }
  default:
    Log.fatal("attr_load: Unimplemented {}", attr);
  }
}

enum class NbtSlice : u8 {
  N,
  B,
  T,
  B1, // GX_NRM_NBT5 only: the second tangent frame
  T1,
};

auto attr_load_nbt_slice(const ShaderConfig& config, NbtSlice slice, std::string_view vidx) -> std::string {
  const auto& mapping = config.attrs[GX_VA_NRM];
  if (mapping.attrType == GX_NONE || (mapping.cnt != 9 && mapping.cnt != 15)) {
    Log.fatal("attr_load_nbt_slice: GX_TG_BINRM/TANGENT requires GX_NRM_NBT or GX_NRM_NBT3");
  }
  const auto sliceIdx = static_cast<u32>(slice);
  const auto compsize = comp_type_size(GX_VA_NRM, static_cast<GXCompType>(mapping.compType));
  u32 dlExtra = 0;
  if (mapping.nbt3) {
    if (mapping.attrType == GX_INDEX8) {
      dlExtra = sliceIdx;
    } else if (mapping.attrType == GX_INDEX16) {
      dlExtra = sliceIdx * 2u;
    }
  }
  const u32 within = sliceIdx * 3u * compsize;
  const auto [offs, buf, le] = attr_address(mapping, GX_VA_NRM, vidx, config.vtxStride, dlExtra, within);
  auto sliceMapping = mapping;
  sliceMapping.cnt = 3;
  return fetch_attr(sliceMapping, buf, offs, le);
}

constexpr std::string_view nbt_slice_local(NbtSlice slice) noexcept {
  return slice == NbtSlice::B ? "in_binrm" : "in_tangent";
}

constexpr bool is_emboss_texgen(GXTexGenType type) noexcept { return type >= GX_TG_BUMP0 && type <= GX_TG_BUMP7; }

auto lighting_func(const ShaderConfig& config, const ColorChannelConfig& cc, u8 i, bool alpha) -> std::string {
  std::string_view swizzle = alpha ? ".a"sv : ""sv;
  std::string outVar;
  std::string_view posVar;
  if (config.perPixelLight) {
    outVar = fmt::format("rast{}", i);
    posVar = "in.mv_pos"sv;
  } else {
    outVar = fmt::format("out.cc{}", i);
    posVar = "mv_pos"sv;
  }
  std::string ambSrc, matSrc;
  if (cc.ambSrc == GX_SRC_VTX) {
    if (config.perPixelLight) {
      ambSrc = fmt::format("in.clr{}", i);
    } else {
      ambSrc = vtx_attr(config, static_cast<GXAttr>(GX_VA_CLR0 + i));
    }
  } else if (cc.ambSrc == GX_SRC_REG) {
    ambSrc = fmt::format("ubuf.cc{0}{1}_amb", i, alpha ? "a"sv : ""sv);
  }
  if (cc.matSrc == GX_SRC_VTX) {
    if (config.perPixelLight) {
      matSrc = fmt::format("in.clr{}", i);
    } else {
      matSrc = vtx_attr(config, static_cast<GXAttr>(GX_VA_CLR0 + i));
    }
  } else if (cc.matSrc == GX_SRC_REG) {
    matSrc = fmt::format("ubuf.cc{0}{1}_mat", i, alpha ? "a"sv : ""sv);
  }
  if (!cc.lightingEnabled) {
    return fmt::format("\n    {0}{2} = {1}{2};", outVar, matSrc, swizzle);
  }
  GXDiffuseFn diffFn = cc.diffFn;
  std::string lightAttnFn;
  if (cc.attnFn == GX_AF_NONE) {
    lightAttnFn = "attn = 1.0;"s;
  } else if (cc.attnFn == GX_AF_SPOT) {
    lightAttnFn = fmt::format(R"""(
          var cosine = max(0.0, dot(ldir, light.dir));
          var cos_attn = dot(light.cos_att, vec3f(1.0, cosine, cosine * cosine));
          var dist_attn = dot(light.dist_att, vec3f(1.0, dist, dist2));
          attn = max(0.0, cos_attn / dist_attn);)""");
  } else if (cc.attnFn == GX_AF_SPEC) {
    std::string_view normal = config.perPixelLight ? "pp_nrm"sv : "mv_nrm"sv;
    std::string dist_attn = diffFn != GX_DF_NONE
                                ? "max(0.0, dot(normalize(light.dist_att), vec3f(1.0, attn, attn * attn)));"
                                : "max(0.0, dot(light.dist_att, vec3f(1.0, attn, attn * attn)));";
    lightAttnFn = fmt::format(R"""(
          attn = select(0.0, max(0.0, dot({0}, light.dir)), dot({0}, ldir) >= 0.0);
          var cos_attn = dot(light.cos_att, vec3f(1.0, attn, attn * attn));
          var dist_attn = {1};
          attn = max(0.0, cos_attn / dist_attn);)""",
                              normal, dist_attn);
  }
  std::string_view lightDiffFn;
  if (diffFn == GX_DF_NONE) {
    lightDiffFn = "1.0"sv;
  } else if (diffFn == GX_DF_SIGN) {
    if (config.perPixelLight) {
      lightDiffFn = "dot(ldir, pp_nrm)"sv;
    } else {
      lightDiffFn = "dot(ldir, mv_nrm)"sv;
    }
  } else if (diffFn == GX_DF_CLAMP) {
    if (config.perPixelLight) {
      lightDiffFn = "max(0.0, dot(ldir, pp_nrm))"sv;
    } else {
      lightDiffFn = "max(0.0, dot(ldir, mv_nrm))"sv;
    }
  }
  return fmt::format(R"""(
    {{
      var lighting = {5};
      for (var i = 0u; i < {1}u; i++) {{
          if ((ubuf.lightState{0}{9} & (1u << i)) == 0u) {{ continue; }}
          var light = ubuf.lights[i];
          var ldir = light.pos - {6};
          var dist2 = dot(ldir, ldir);
          var dist = sqrt(dist2);
          ldir = ldir / dist;
          var attn: f32;{2}
          var diff = {3};
          lighting = lighting + (attn * diff * light.color);
      }}
      {7}{8} = ({4} * clamp(lighting, vec4f(0.0), vec4f(1.0))){8};
    }})""",
                     i, GX::MaxLights, lightAttnFn, lightDiffFn, matSrc, ambSrc, posVar, outVar, swizzle,
                     alpha ? "a"sv : ""sv);
}

// GX_AURORA_SET_PBR: replaces the TEV colour result with a metal/roughness evaluation of
// texture maps 0-3 (base colour, glTF occlusion/roughness/metal in R/G/B, two-channel
// tangent-space normal, emissive) lit by channel 0's GX lights and ambient. TEV alpha is
// kept. The lobe is Metroid Prime Remastered's, as its model shader evaluates it: GGX with
// alpha = roughness^2 floored at roughness 0.02, visibility 1/4((N.L(1-k)+k)(N.V(1-k)+k))
// with k = alpha/2, Schlick Fresnel on the specular only, and occlusion on the direct
// diffuse as well as the ambient. Each map is read from the TEV stage that samples it, so the material's fallback
// TEV must reference all four; a missing map gets a neutral default. The tangent frame
// comes from screen-space derivatives, so no tangent attribute is needed. Maths is done
// on linearised colours and converted back, since the rest of the pipeline is gamma.
// Channel 1's lights (bar the model shadow's, below) and a vertex-sourced ambient are not
// used (vertex ambient falls back to a constant). With channel 0 unlit there are no lights
// to sum and the channel's material colour, which is all an unlit surface shows, is the ambient; the game draws a model that
// way when no light reaches it, and a room lit by an ambient volume always. A material
// without channel 0 keeps its TEV result. A vertex colour, where the vertex format has
// one, multiplies the diffuse albedo (not the specular) as a linear value, and its alpha
// the output's: that is what Remastered's shader does with it.
auto pbr_func(const ShaderConfig& config, const ShaderInfo& info, std::string& vtxOutAttrs,
              std::string& vtxXfrAttrs, size_t& vtxOutIdx, std::string_view vidx) -> std::string {
  const auto& cc = config.colorChannels[GX_COLOR0];
  if (!info.sampledColorChannels.test(0)) {
    return {};
  }
  const bool lit = info.lightingEnabled && cc.lightingEnabled;
  // The game's projected shadow (CCubeMaterial's model shadow) takes the first stage:
  // channel 1, lit by the shadow-casting light alone, times the shadow map. That light
  // is then summed with channel 0's lights, scaled by the map's sample.
  const bool shadowed = config.tevStageCount > 2 && config.tevStages[0].channelId == GX_COLOR1A1 &&
                        config.tevStages[0].texMapId != GX_TEXMAP_NULL &&
                        config.tevStages[0].texCoordId != GX_TEXCOORD_NULL &&
                        config.colorChannels[GX_COLOR1].lightingEnabled;
  std::array<int, 8> mapStage{-1, -1, -1, -1, -1, -1, -1, -1};
  for (int i = shadowed ? 1 : 0; i < config.tevStageCount; ++i) {
    const auto& stage = config.tevStages[i];
    const u32 map = underlying(stage.texMapId);
    if (map < mapStage.size() && mapStage[map] == -1 && uses_texture_sample(stage) &&
        stage.texCoordId != GX_TEXCOORD_NULL && stage.indTexMtxId == GX_ITM_OFF) {
      mapStage[map] = i;
    }
  }
  if (mapStage[0] == -1) {
    return {};
  }
  // GXSetPBRCostTest: 1 flat, 2 no lights, 3 no ambient volume, 4 no reflection cube, 5 no
  // normal maps, 6 no metal/roughness and emissive maps, 7 flat with every map read.
  const int costTest = config.pbr - 1;
  if (costTest == 5) {
    mapStage[2] = -1;
  } else if (costTest == 6) {
    mapStage[1] = -1;
    mapStage[3] = -1;
  }
  // Glass (kind 8) also samples map 7, a copy of what is behind it on screen. The
  // shadow's stage samples map 7 too, so a shadowed surface goes without.
  // 3c66aaef (layered + MNMP) binds its macro normal as map 7, which is not a scene copy.
  const bool layeredMacro = mapStage[4] != -1 && mapStage[7] != -1 && (config.pbrKind == 0 || config.pbrKind == 24);
  bool screen = false;
  for (int i = 0; i < config.tevStageCount && !shadowed && !layeredMacro; ++i) {
    const auto& stage = config.tevStages[i];
    if (stage.texMapId == GX_TEXMAP7 && uses_texture_sample(stage) && stage.texCoordId != GX_TEXCOORD_NULL) {
      screen = true;
    }
  }
  const auto sampled = [&](int map, std::string_view fallback) {
    return mapStage[map] == -1 ? std::string(fallback) : fmt::format("sampled{}", mapStage[map]);
  };
  vtxOutAttrs += fmt::format("\n    @location({}) pbr_pos: vec3f,", vtxOutIdx++);
  vtxOutAttrs += fmt::format("\n    @location({}) pbr_nrm: vec3f,", vtxOutIdx++);
  vtxXfrAttrs += "\n    out.pbr_pos = mv_pos;\n    out.pbr_nrm = mv_nrm;";
  // The backlight's height fade of a skinned draw, from the bind pose (see bind_pos_active):
  // saturate(bind y * scale + offset), or -1 where the draw has none.
  vtxOutAttrs += fmt::format("\n    @location({}) pbr_bty: f32,", vtxOutIdx++);
  vtxXfrAttrs += "\n    out.pbr_bty = pbr_bind_y;";
  // A model with vertex tangents (the NBT normal array: N, B, T) shades its normal maps with
  // Remastered's frame: T and the handedness w (B = w * cross(N, T), so w is the sign of
  // dot(cross(N, T), B)). Without them the frame comes from the screen derivatives below.
  // GX_NRM_NBT5 adds a second frame (B1, T1) for the layered shaders A978D507 and D363D694,
  // whose second normal map has a tangent stream of its own (TANGENT_1).
  const bool tangents2 = config.attrs[GX_VA_NRM].attrType != GX_NONE && config.attrs[GX_VA_NRM].cnt == 15;
  const bool tangents = tangents2 || (config.attrs[GX_VA_NRM].attrType != GX_NONE && config.attrs[GX_VA_NRM].cnt == 9);
  // Remastered's vertex shaders (static and skinned) multiply the handedness by the sign of
  // the model-view determinant, so a mirrored model keeps its bitangent (0 for a flat matrix).
  if (tangents) {
    vtxXfrAttrs += "\n    let pbr_mv = ubuf.postex_mtx[in_pnmtxidx];"
                   "\n    let pbr_mvsign = sign(dot(pbr_mv[0].xyz, cross(pbr_mv[1].xyz, pbr_mv[2].xyz)));";
  }
  if (tangents2) {
    vtxOutAttrs += fmt::format("\n    @location({}) pbr_tan2: vec4f,", vtxOutIdx++);
    vtxXfrAttrs += fmt::format(
        "\n    let pbr_vb2 = {};"
        "\n    let pbr_vt2 = {};"
        "\n    let pbr_vtv2 = vec4f(pbr_vt2, 0.0) * ubuf.postex_mtx[in_pnmtxidx];"
        "\n    out.pbr_tan2 = vec4f(pbr_vtv2, pbr_mvsign * select(-1.0, 1.0, dot(cross(in_nrm, pbr_vt2), pbr_vb2) >= 0.0));",
        attr_load_nbt_slice(config, NbtSlice::B1, vidx), attr_load_nbt_slice(config, NbtSlice::T1, vidx));
  }
  if (tangents) {
    vtxOutAttrs += fmt::format("\n    @location({}) pbr_tan: vec4f,", vtxOutIdx++);
    vtxXfrAttrs += fmt::format(
        "\n    let pbr_vb = {};"
        "\n    let pbr_vt = {};"
        "\n    let pbr_vtv = vec4f(pbr_vt, 0.0) * ubuf.postex_mtx[in_pnmtxidx];"
        "\n    out.pbr_tan = vec4f(pbr_vtv, pbr_mvsign * select(-1.0, 1.0, dot(cross(in_nrm, pbr_vt), pbr_vb) >= 0.0));",
        attr_load_nbt_slice(config, NbtSlice::B, vidx), attr_load_nbt_slice(config, NbtSlice::T, vidx));
  }
  // A vertex colour is the surface's tint where the material says so (mode 4), whatever
  // the channel does with it: the game points an unlit channel at its material register,
  // which would lose it. A retail model's colours are not a tint.
  // A special surface (the kind in pbr_layer.y, built into the shader as ShaderConfig::pbrKind
  // so the other kinds' code drops out) reads the colour its own way, tint or not.
  std::string tint, tintAlpha, vclr = "vec4f(1.0)";
  if (config.attrs[GX_VA_CLR0].attrType != GX_NONE) {
    vtxOutAttrs += fmt::format("\n    @location({}) pbr_vclr: vec4f,", vtxOutIdx++);
    vtxXfrAttrs += fmt::format("\n    out.pbr_vclr = {};", vtx_attr(config, GX_VA_CLR0));
    tint = " * pbr_vc.rgb";
    tintAlpha = " * pbr_vc.a";
    vclr = "in.pbr_vclr";
  }
  // The baked lightmap's UVs (GXSetPBRLightmapAttr), which populate_pipeline_config sets only when the vertices
  // carry them.
  const auto lightmapAttr = static_cast<GXAttr>(config.pbrLightmapAttr);
  const bool lightmapUsed = lightmapAttr != GX_VA_NULL && config.attrs[lightmapAttr].attrType != GX_NONE;
  if (lightmapUsed) {
    vtxOutAttrs += fmt::format("\n    @location({}) pbr_lmuv: vec2f,", vtxOutIdx++);
    vtxXfrAttrs += fmt::format("\n    out.pbr_lmuv = {};", vtx_attr(config, lightmapAttr));
  }

  // A second layer (maps 4-6: base, MR, normal) over the first. The vertex alpha says
  // where, and the two base maps' alphas are heights that decide which layer shows
  // through first across the edge: this is Remastered's blend.
  const bool layered = mapStage[4] != -1;
  // The fragment's screen position, for everything below that reads the screen copy (map 7): the
  // layered block's glass/holo/field/shield branches, kind 34, and kinds 23/30. Declared once here.
  if (screen && (layered || config.pbrKind == 34 || config.pbrKind == 36 ||
                 ((config.pbrKind == 23 || config.pbrKind == 30) && mapStage[2] != -1))) {
    vtxOutAttrs += fmt::format("\n    @location({}) pbr_scr: vec4f,", vtxOutIdx++);
    vtxXfrAttrs += "\n    out.pbr_scr = out.pos;";
  }
  std::string base = sampled(0, ""), orm = sampled(1, "vec4f(1.0, 0.6, 0.0, 1.0)");
  // Kind 10, lit glass drawn premultiplied (One, InvSrcAlpha): the opacity scales only the
  // diffuse light, so the reflection and the glow are not dimmed with it.
  // Kinds 36/37 (inventory visor): BCLR x DIFC is the albedo and BCLR.a^2 x DIFC.a the opacity, explicit (no TEV
  // alpha, no vertex alpha, no generic fade). Kind 36 premultiplies the diffuse by it, like kind 34.
  const bool inventory = (config.pbrKind == 36 || config.pbrKind == 37) && mapStage[0] != -1 && !layered;
  const std::string& alphaTint = inventory ? std::string() : tintAlpha;
  const std::string diffTint =
      inventory ? (config.pbrKind == 36 ? tint + " * inv_a" : tint)
      : (config.pbrKind == 10 || config.pbrKind == 34)
          ? fmt::format("{} * ({}{})", tint, layered ? "1.0" : "prev.a", tintAlpha)
          : tint;
  // Remastered's diffuse vertex colour (MFVC) is decoded in its vertex shader as
  // 2 |c|^2.2 (alpha raw), so a white vertex doubles the diffuse: 882014ee, f22feb5b,
  // the layered 7248969b/a978d507, 11b30369 and every kShaderTints shader. Only the premultiplied
  // glass 941068bf/bcc73459 keep the colour raw (mode bit 524288).
  const std::string vtint =
      "select(vec4f(2.0 * pow(abs(pbr_vraw.rgb), vec3f(2.2)), pbr_vraw.a), pbr_vraw, pbr_rawv)";
  std::string normalXy = mapStage[2] == -1 ? std::string() : fmt::format("sampled{}.rg", mapStage[2]);
  std::string normalXy2; // a layered shader's second normal map

  std::string layer = fmt::format(R"""(
      // The stored normal. A PBR6 back copy (F0 factor 0, LITS) is stored turned round like
      // every back copy; Remastered keeps the surface's own normal there, and so does this.
      let pbr_ngs = normalize(in.pbr_nrm);
      let pbr_ng = select(pbr_ngs, -pbr_ngs, ubuf.pbr_light_scale.y == 0.0);
      let pbr_kind = {:.1f};
      let pbr_vraw = {};
      // 8 = Remastered's ColorUnlit: its vertex shader linearises the colour and doubles
      // it, and the pixel shader's gain is in the backlight's place.
      // 16 = a sky (with 1, unlit): the backlight's place holds the gain on its colour.
      // 256 = no reflection of the surroundings (a Remastered material with no REFL: its
      // shader samples no cube). 128 = the vertex colour tints the albedo before F0 too
      // (Remastered's kShaderTints).
      // 512 = Remastered's 1-bit cutout: discard where base.a^2 < 0.25, opaque otherwise,
      // in place of the GX alpha compare.
      // 1024 = no specular from the lights (Remastered's pure Lambert VFX_Model_Base, 29d9fdfb).
      // 2048 = the opacity is the base map's alpha as it is: no vertex alpha, nothing squared.
      // 4096 = the baked light's modulation (BLCM) multiplies the lights' diffuse too, not
      // just the baked ambient (that shader's baked-light perms: BLCM x (lobe + lights)).
      // 32768 = the vertex shader's procedural wind (Remastered's USE_PROCEDURAL_WIND_ANIMATION);
      // the fragment ignores it.
      // 65536 = the opacity is the vertex alpha alone (495899e7: o0.w = v5.w * DIFC.a, DIFC.a = 1).
      // 262144 = flat ambient only: no baked lobes, grid volume or lightmap, and no BLCM (4bc890c1
      // LambertFx, whose perms sample none of them and seed the light sum with the flat constant).
      // 524288 = the vertex colour is used raw (premultiplied glass 941068bf / bcc73459, whose
      // vertex shader has no log2/exp2 decode).
      let pbr_rawv = ubuf.pbr_backlight.w > 524287.5;
      let pbr_mwv5 = ubuf.pbr_backlight.w - select(0.0, 524288.0, pbr_rawv);
      let pbr_flat = pbr_mwv5 > 262143.5;
      let pbr_mwf = pbr_mwv5 - select(0.0, 262144.0, pbr_flat);
      // 131072 = a bare unlit surface (the Surface shaders 67135a0b / 6fc4d540): Remastered
      // multiplies it by no exposure of its own, so the frame's tonemap exposes it; the backlight
      // rgb holds the part of that exposure GlowScale (tone row 0 w) leaves.
      let pbr_uex = pbr_mwf > 131071.5;
      let pbr_mwu = pbr_mwf - select(0.0, 131072.0, pbr_uex);
      let pbr_vao = pbr_mwu > 65535.5;
      let pbr_mwv = pbr_mwu - select(0.0, 65536.0, pbr_vao);
      let pbr_wnd = pbr_mwv > 32767.5;
      let pbr_mww = pbr_mwv - select(0.0, 32768.0, pbr_wnd);
      // 16384 = a macro normal map (MNMP, map 6) whiteout-blended over the normal in TANGENT_1's
      // frame (a3c367be, 72b34e42, b9e899f3).
      let pbr_macro = pbr_mww > 16383.5;
      let pbr_mwz = pbr_mww - select(0.0, 16384.0, pbr_macro);
      // 8192 = map 1 is an indirect offset map (Remastered's INDI, 8ce05ed0), not metal/roughness:
      // the base map is sampled at uv + (INDI.xy - 0.5) * INDS (INDS in the backlight's x), and
      // the surface has no AO or metal.
      let pbr_ind = pbr_mwz > 8191.5;
      let pbr_mwi = pbr_mwz - select(0.0, 8192.0, pbr_ind);
      let pbr_blit = pbr_mwi > 4095.5;
      let pbr_mwa = pbr_mwi - select(0.0, 4096.0, pbr_blit);
      let pbr_raw = pbr_mwa > 2047.5;
      let pbr_mwb = pbr_mwa - select(0.0, 2048.0, pbr_raw);
      let pbr_nols = pbr_mwb > 1023.5;
      let pbr_mwc = pbr_mwb - select(0.0, 1024.0, pbr_nols);
      let pbr_cut = pbr_mwc > 511.5;
      let pbr_mw1 = pbr_mwc - select(0.0, 512.0, pbr_cut);
      let pbr_noenv = pbr_mw1 > 255.5;
      let pbr_mwr = pbr_mw1 - select(0.0, 256.0, pbr_noenv);
      let pbr_f0t = pbr_mwr > 127.5;
      let pbr_mw0 = pbr_mwr - select(0.0, 128.0, pbr_f0t);
      let pbr_sky = pbr_mw0 > 15.5;
      let pbr_mw = pbr_mw0 - select(0.0, 16.0, pbr_sky);
      let pbr_cu = pbr_mw > 7.5;
      let pbr_flags = pbr_mw - select(0.0, 8.0, pbr_cu);
      var pbr_vc = select(vec4f(1.0), {}, pbr_flags > 3.5);
      if (pbr_cu) {{
          pbr_vc = vec4f(2.0 * pow(abs(pbr_vraw.rgb), vec3f(2.2)) * ubuf.pbr_backlight.rgb, pbr_vraw.a);
      }})""",
                                  float(config.pbrKind), vclr, vtint);
  // A cut-out pixel (grass, leaves) that the alpha compare drops is dropped here instead of
  // after the shading. The compare sees prev.a times the vertex alpha unless a height blend,
  // ColorUnlit or a glow mode changes it (pbr_alpha below), and a layered surface's alpha is
  // its own.
  if (config.alphaCompare && mapStage[4] == -1) {
    const auto discard = alpha_compare_discard(config);
    if (discard.constant == -1) {
      layer += fmt::format(R"""(
      if (!pbr_cut && !pbr_raw && ubuf.pbr_emissive.w <= 0.0 && !pbr_cu && pbr_flags - select(0.0, 4.0, pbr_flags > 3.5) < 1.5) {{
          let alphaCompare = u32(round(clamp(prev.a{}, 0.0, 1.0) * 255.0));
          if ({}) {{ discard; }}
      }})""",
                           tintAlpha, discard.expr);
    }
  }
  if (mapStage[4] == -1 && config.pbrKind == 24) {
    // The decal cutouts (kind 24: e538b757, 42fe2ed0): t = clamp((A - 0.5 + c) / 2c), cut where t²(3 - 2t) < 0.25,
    // with c = CCH0.x = 0.5 on all of them, so t = A. A is the alpha map's red (42fe2ed0's TCH0, map 3), else
    // the base alpha squared; no vertex alpha.
    layer += fmt::format(R"""(
      let pbr_dca = clamp({}, 0.0, 1.0);
      if (pbr_cut && pbr_dca * pbr_dca * (3.0 - 2.0 * pbr_dca) < 0.25) {{
          discard;
      }}
      //@CUT_END)""",
                         mapStage[3] != -1 ? fmt::format("sampled{}.r", mapStage[3]) : "prev.a * prev.a"s);
  } else if (mapStage[4] == -1) {
    // Remastered's cutout (mode 512): the filtered base alpha squared against 0.25. The
    // DepthOnly caster (93e0c484, 0b2e072b) multiplies by c3[0].w = DIFC.w = 1 and has no
    // vertex colour, so the caster test (up to the marker) has no vertex alpha. The colour
    // pass also multiplies by the raw vertex alpha on tinted shaders (pbr_vc.a is 1
    // otherwise); that second test discards a superset since vc.a <= 1.
    layer += R"""(
      if (pbr_cut && prev.a * prev.a < 0.25) {
          discard;
      }
      //@CUT_END
      if (pbr_cut && prev.a * prev.a * pbr_vc.a < 0.25) {
          discard;
      }
)""";
  }
  if (costTest == 1 || costTest == 7) {
    // The base map, shaded by the facing only, and nothing else. Test 7 keeps the other
    // maps' reads alive with a weight too small to see.
    std::string maps;
    for (int map = 1; costTest == 7 && map < 7; ++map) {
      if (mapStage[map] != -1) {
        maps += fmt::format(" + sampled{}.rgb * 1e-6", mapStage[map]);
      }
    }
    return fmt::format(R"""(
    // PBR, flat (GXSetPBRCostTest)
    {{{}
      let pbr_flat = 0.4 + 0.6 * max(dot(pbr_ng, normalize(-in.pbr_pos)), 0.0);
      prev = vec4f({}.rgb * pbr_flat{}, prev.a{});
    }})""",
                       layer, base, maps, tintAlpha);
  }
  const bool framed = mapStage[2] != -1 || layered;
  if (framed) {
    // Cotangent frame (Schüler): pbr_t and pbr_b are the directions U and V grow in. WebGPU's
    // framebuffer Y runs down, so the Y derivatives are negated to get GL's (view space Y up)
    // orientation. The frame is built on the stored normal: seen from behind, the screen's
    // handedness flips with the winding, so the turned-round normal of a back copy gives the
    // front's own T and B (a LITS back, which shades with the turned-back normal, keeps them).
    layer += fmt::format(R"""(
      let pbr_dp1 = dpdx(in.pbr_pos);
      let pbr_dp2 = -dpdy(in.pbr_pos);
      let pbr_duv1 = dpdx(tex{0}_uv);
      let pbr_duv2 = -dpdy(tex{0}_uv);
      let pbr_dp2perp = cross(pbr_dp2, pbr_ngs);
      let pbr_dp1perp = cross(pbr_ngs, pbr_dp1);
      let pbr_t0 = pbr_dp2perp * pbr_duv1.x + pbr_dp1perp * pbr_duv2.x;
      let pbr_b0 = pbr_dp2perp * pbr_duv1.y + pbr_dp1perp * pbr_duv2.y;
      let pbr_tlen = max(dot(pbr_t0, pbr_t0), dot(pbr_b0, pbr_b0));)""",
                         underlying(config.tevStages[mapStage[mapStage[2] != -1 ? 2 : 0]].texCoordId));
    if (tangents) {
      // Remastered's vertex frame: T normalised (not made orthogonal to N), B = cross(N, T) * w,
      // given the derivative frame's length (every consumer scales by 1 / sqrt(pbr_tlen)) and
      // its orientation: B against V (n = t * x - b * y).
      layer += R"""(
      let pbr_tu = normalize(in.pbr_tan.xyz);
      let pbr_tsc = sqrt(pbr_tlen);
      let pbr_t = pbr_tu * pbr_tsc;
      let pbr_b = -cross(pbr_ngs, pbr_tu) * (in.pbr_tan.w * pbr_tsc);)""";
      if (tangents2) {
        layer += R"""(
      let pbr_tu2 = normalize(in.pbr_tan2.xyz);
      let pbr_t2 = pbr_tu2 * pbr_tsc;
      let pbr_b2 = -cross(pbr_ngs, pbr_tu2) * (in.pbr_tan2.w * pbr_tsc);)""";
      }
    } else {
      layer += R"""(
      let pbr_t = pbr_t0;
      let pbr_b = pbr_b0;)""";
    }
  }
  std::string kinds = "\n      var pbr_kglow = vec3f(0.0);\n      var pbr_kns = 1.0;\n      var pbr_knoise = 0.0;";
  if (layered) {
    // Kind 1 lays the second layer on what faces up: the weight is the first layer's own
    // normal along world up, lifted by the vertex alpha.
    std::string first = "\n              var pbr_n1 = pbr_ng;";
    if (mapStage[2] != -1) {
      first += fmt::format(R"""(
              let pbr_ts1 = sampled{}.rg * 1.9921875 - 1.0;
              if (pbr_tlen > 1e-24) {{
                  let pbr_s1 = inverseSqrt(pbr_tlen);
                  pbr_n1 = normalize(pbr_t * (pbr_s1 * pbr_ts1.x) - pbr_b * (pbr_s1 * pbr_ts1.y) +
                                     pbr_ng * sqrt(max(0.0, 1.0 - dot(pbr_ts1, pbr_ts1))));
              }})""",
                           mapStage[2]);
    }
    const auto& inner = config.tevStages[mapStage[4]];
    // Kind 4 sees map 4 inside the surface: where the view ray is once it has gone the
    // base map's alpha deep, in one unclamped step. Remastered's basis is T and
    // normalize(cross(N, T)) without the handedness (V runs against it, hence the minus).
    // The sample is taken whatever the kind, as one in a branch has no derivatives.
    const std::string_view parallaxBasis =
        tangents ? "vec2f(dot(pbr_tu, in.pbr_pos), -dot(normalize(cross(pbr_ngs, pbr_tu)), in.pbr_pos))"
                 : "vec2f(dot(pbr_t * inverseSqrt(max(dot(pbr_t, pbr_t), 1e-30)), in.pbr_pos),\n"
                   "                           dot(pbr_b * inverseSqrt(max(dot(pbr_b, pbr_b), 1e-30)), in.pbr_pos))";
    layer += fmt::format(R"""(
      var pbr_poff = vec2f(0.0);
      if (pbr_kind > 3.5 && pbr_kind < 4.5{6}) {{
          pbr_poff = {1} * ({0}.a * ubuf.pbr_param.w / dot(pbr_ng, in.pbr_pos));
      }}
      let pbr_inner = max(textureSampleBias(tex{4}, tex{4}_samp, tex{5}_uv + pbr_poff, ubuf.tex{4}_size_bias.z).rgb,
                          vec3f(0.0));
      var pbr_ls = 0.0;
      if (ubuf.pbr_layer.x > 0.0) {{
          var pbr_lw = pbr_vraw.a * 2.0 - 1.0;
          if ((pbr_kind > 0.5 && pbr_kind < 1.5) || (pbr_kind > 25.5 && pbr_kind < 26.5)) {{{3}
              let pbr_va2 = max(pbr_vraw.a * 2.0, 1e-4);
              pbr_lw = max(0.0, (dot(pbr_n1, ubuf.pbr_up.xyz) + pbr_va2 - 1.0) / pbr_va2) * 2.0 - 1.0;
          }}
          let pbr_lh = clamp(sampled{2}.a * ubuf.pbr_layer_height.z + ubuf.pbr_layer_height.w, 0.0, 1.0) -
                       clamp({0}.a * ubuf.pbr_layer_height.x + ubuf.pbr_layer_height.y, 0.0, 1.0);
          let pbr_lt = ubuf.pbr_layer.x;
          let pbr_lx = clamp((pbr_lh + pbr_lw * pbr_lt + pbr_lw + pbr_lt) * 0.5 / pbr_lt, 0.0, 1.0);
          pbr_ls = pbr_lx * pbr_lx * (3.0 - 2.0 * pbr_lx);
      }}
      // Kind 0 (the BCRL shaders: 7248969b, a978d507, d363d694, df0677ff, ea49e9e1...): the opacity is
      // the blended alpha squared times DIFC.a (1 on all of them), not the vertex alpha, which is the weight.
      let pbr_lma = mix({0}.a, sampled{2}.a, pbr_ls);
      let pbr_lalpha = pbr_lma * pbr_lma;)""",
                         base, parallaxBasis, mapStage[4], first, underlying(inner.texMapId), underlying(inner.texCoordId),
                         tangents ? "" : " && pbr_tlen > 1e-24");
    // Kind 20 (E9DF2188): map 3 is a detail map (the kind has no glow). The albedo is the blend times the
    // vertex colour (2 |c|^2.2, applied below) times 2 times the detail as stored.
    if (mapStage[3] != -1) {
      kinds += fmt::format(R"""(
      if (pbr_kind > 19.5 && pbr_kind < 20.5) {{
          pbr_base = pbr_base * max(sampled{}.rgb, vec3f(0.0)) * 2.0;
      }})""",
                           mapStage[3]);
    }
    // Kind 21, Remastered's Phazon3 (CC96C27D, the Phazon Mines' stone), permutation 034_0. Constants are
    // GXSetPBRShield's rows: CCH0..CCH3 in rows 0-3 and DIFC in row 7; pbr_param.x is the time. Map 0 is BCLR
    // (its alpha a weight), map 1 METL, map 2 NMAP, map 4 TCH0 (a colour ramp) and map 5 TCH1 (a mask read at
    // the base UV times CCH0.w). The ramp's column is a rim term of the normal map's z (1 - nz^(1/2.2))^2.2
    // plus an overlay of BCLR.r and BCLR.a^2.2, its row the pulse sin(t CCH2.x + mask CCH2.y) / 2 + 1/2. Where
    // the vertex alpha (2 va + a^2.2 - 1, squared) says, the albedo is that colour; the vertex blue (with the
    // AO cavity) masks the CCH3 glow. The vertex colour is raw. The glow is added at the inverse of the exposure
    // (see liquid below).
    if (config.pbrKind == 21 && mapStage[2] != -1 && mapStage[4] != -1 && mapStage[5] != -1) {
      kinds += fmt::format(R"""(
      let pbr_zc0 = ubuf.pbr_shield[0];
      let pbr_zc1 = ubuf.pbr_shield[1];
      let pbr_zc2 = ubuf.pbr_shield[2];
      let pbr_zdf = ubuf.pbr_shield[7];
      let pbr_zxy = sampled{0}.rg * 1.9921875 - 1.0;
      let pbr_znz = sqrt(1.0 - clamp(dot(pbr_zxy, pbr_zxy), 0.0, 1.0));
      let pbr_zuv = tex{2}_uv * pbr_zc0.w;
      let pbr_zm = textureSampleGrad(tex{1}, tex{1}_samp, pbr_zuv, dpdx(pbr_zuv), dpdy(pbr_zuv)).x;
      let pbr_zph = sin(ubuf.pbr_param.x * pbr_zc2.x + pbr_zm * pbr_zc2.y);
      let pbr_zr = {3}.r;
      let pbr_za = pow(abs({3}.a), 2.2);
      let pbr_zg = select(2.0 * pbr_zr * (1.0 - pbr_za), 1.0 - 2.0 * pbr_za * (1.0 - pbr_zr), pbr_zr > 0.5);
      let pbr_zu = (pow(abs(1.0 - pow(pbr_znz, 0.454545438)), 2.2) + pbr_zg + pbr_zc1.x) * pbr_zc1.y;
      let pbr_zgrad = max(textureSampleLevel(tex{4}, tex{4}_samp, vec2f(pbr_zu, pbr_zph * 0.5 + 0.5), 0.0).rgb,
                          vec3f(0.0));
      let pbr_zt = clamp(2.0 * pbr_vraw.a + pbr_za - 1.0, 0.0, 1.0);
      let pbr_zt2 = pbr_zt * pbr_zt;
      let pbr_zs = clamp(pbr_vraw.b - 1.0 + clamp((pbr_zc1.z - {5}.r + 1.0) * pbr_zc1.w, 0.0, 1.0), 0.0, 1.0);
      let pbr_zs2 = pbr_zs * pbr_zs;
      pbr_base = mix(max({3}.rgb, vec3f(0.0)), pbr_zgrad * (1.0 - pbr_zc0.y), pbr_zt2) * pbr_zdf.rgb;)""",
                           mapStage[2], underlying(config.tevStages[mapStage[5]].texMapId),
                           underlying(config.tevStages[mapStage[0]].texCoordId), base,
                           underlying(config.tevStages[mapStage[4]].texMapId), sampled(1, "vec4f(1.0, 0.6, 0.0, 1.0)"));
    }
    // Kind 22, Remastered's 9e52aa74 (the Phazon Mines' stone and the crater's flesh), permutation 034_0. The
    // maps and constants are kind 21's, with these differences: the mask (map 5) is read at the base UV; the
    // pulse is sin(mask + t) and the mask also joins the vertex alpha (2 va + mask - 1, not squared) for the
    // ramp's weight; the ramp's column is offset and scaled by CCH0.z/.w; a second ramp read at (a^2.2 + CCH2.z +
    // CCH2.x) CCH2.y on row 0 gives the alpha that, with the vertex green, lights the CCH3 cavity glow (no
    // phase); the ramp glow is 4 t va.r CCH0.x CCH0.y grad. There is no DIFC, and no exposure factor on the glow.
    if (config.pbrKind == 22 && mapStage[2] != -1 && mapStage[4] != -1 && mapStage[5] != -1) {
      kinds += fmt::format(R"""(
      let pbr_zc0 = ubuf.pbr_shield[0];
      let pbr_zc1 = ubuf.pbr_shield[1];
      let pbr_zc2 = ubuf.pbr_shield[2];
      let pbr_zxy = sampled{0}.rg * 1.9921875 - 1.0;
      let pbr_znz = sqrt(1.0 - clamp(dot(pbr_zxy, pbr_zxy), 0.0, 1.0));
      let pbr_zm = textureSampleGrad(tex{1}, tex{1}_samp, tex{2}_uv, dpdx(tex{2}_uv), dpdy(tex{2}_uv)).x;
      let pbr_zph = sin(pbr_zm + ubuf.pbr_param.x);
      let pbr_zr = {3}.r;
      let pbr_za = pow(abs({3}.a), 2.2);
      let pbr_zg = select(2.0 * pbr_zr * (1.0 - pbr_za), 1.0 - 2.0 * pbr_za * (1.0 - pbr_zr), pbr_zr > 0.5);
      let pbr_zu = (pow(abs(1.0 - pow(pbr_znz, 0.454545438)), 2.2) + pbr_zg + pbr_zc0.z) * pbr_zc0.w;
      let pbr_zgrad = max(textureSampleLevel(tex{4}, tex{4}_samp, vec2f(pbr_zu, pbr_zph * 0.5 + 0.5), 0.0).rgb,
                          vec3f(0.0));
      let pbr_zca = textureSampleLevel(tex{4}, tex{4}_samp, vec2f((pbr_za + pbr_zc2.z + pbr_zc2.x) * pbr_zc2.y, 0.0), 0.0).a;
      let pbr_zt = clamp(2.0 * pbr_vraw.a + pbr_zm - 1.0, 0.0, 1.0);
      let pbr_zcav = clamp(pbr_vraw.g - 1.0 + pbr_zca, 0.0, 1.0) +
                     clamp(pbr_vraw.b - 1.0 + clamp((pbr_zc1.x - {5}.r + 1.0) * pbr_zc1.y, 0.0, 1.0), 0.0, 1.0);
      pbr_base = mix(max({3}.rgb, vec3f(0.0)), pbr_zgrad * (1.0 - pbr_zc0.y), pbr_zt);)""",
                           mapStage[2], underlying(config.tevStages[mapStage[5]].texMapId),
                           underlying(config.tevStages[mapStage[0]].texCoordId), base,
                           underlying(config.tevStages[mapStage[4]].texMapId), sampled(1, "vec4f(1.0, 0.6, 0.0, 1.0)"));
    }
    // Kind 32, Remastered's PhazonPool (07acff46, the blisters), permutation 018_0. Constants are GXSetPBRShield's
    // rows: CCH0 (fresnel power, gain, noise scale along the view, emission gain), CCH1 (noise speed, depth along
    // BCLR.a), ICMC in row 6 and DIFC in row 7; pbr_param.x is the sim clock. Map 4 is TCH0, a 64^3 noise volume the
    // importer stacked into a 512x512 atlas (slice z at tile (z % 8, z / 8)), read here as a repeating trilinear
    // 3D texture on its own texcoord set; map 5 is TCH1, the emission ramp. The noise is read at the UV offset
    // along the view in the tangent frame (T, normalize(cross(N, T)), no handedness) by BCLR.a x CCH0.z over
    // N.pos, and at z = fract(t CCH1.x + BCLR.a CCH1.y). It scales the normal map's tilt (max(1.25 n, 0.1)) and
    // lowers the roughness; the ramp is read at (n + f, f) for the fresnel f = (N.V)^CCH0.x CCH0.y of the
    // geometric normal, and its colour times CCH0.w is the glow (times the exposure, as every glow here).
    if (config.pbrKind == 32 && mapStage[2] != -1 && mapStage[4] != -1 && mapStage[5] != -1) {
      kinds += fmt::format(R"""(
      let pbr_pc0 = ubuf.pbr_shield[0];
      let pbr_pc1 = ubuf.pbr_shield[1];
      let pbr_pdf = ubuf.pbr_shield[7];
      let pbr_ps = {0}.a * pbr_pc0.z;
      let pbr_pnp = dot(pbr_ng, in.pbr_pos);
      let pbr_pq = pbr_ps / select(-1e-6, pbr_pnp, abs(pbr_pnp) > 1e-6);
      let pbr_pbv = {3};
      let pbr_pz = fract(ubuf.pbr_param.x * pbr_pc1.x + {0}.a * pbr_pc1.y);
      let pbr_puv = (tex{1}_uv + pbr_pbv * pbr_pq) * 64.0 - 0.5;
      let pbr_pf = fract(pbr_puv);
      let pbr_pci = vec2i(floor(pbr_puv));
      let pbr_pzz = pbr_pz * 64.0 - 0.5;
      let pbr_pzf = fract(pbr_pzz);
      let pbr_pzi = i32(floor(pbr_pzz));
      var pbr_pn = 0.0;
      for (var pbr_pk = 0; pbr_pk < 2; pbr_pk++) {{
          let pbr_pl = (pbr_pzi + pbr_pk) & 63;
          let pbr_po = vec2i((pbr_pl & 7) * 64, (pbr_pl >> 3) * 64);
          let pbr_px0 = pbr_pci.x & 63;
          let pbr_px1 = (pbr_pci.x + 1) & 63;
          let pbr_py0 = pbr_pci.y & 63;
          let pbr_py1 = (pbr_pci.y + 1) & 63;
          let pbr_pa = mix(textureLoad(tex{2}, pbr_po + vec2i(pbr_px0, pbr_py0), 0).x,
                           textureLoad(tex{2}, pbr_po + vec2i(pbr_px1, pbr_py0), 0).x, pbr_pf.x);
          let pbr_pb = mix(textureLoad(tex{2}, pbr_po + vec2i(pbr_px0, pbr_py1), 0).x,
                           textureLoad(tex{2}, pbr_po + vec2i(pbr_px1, pbr_py1), 0).x, pbr_pf.x);
          pbr_pn += mix(pbr_pa, pbr_pb, pbr_pf.y) * select(1.0 - pbr_pzf, pbr_pzf, pbr_pk == 1);
      }}
      pbr_knoise = pbr_pn;
      pbr_kns = max(1.25 * pbr_pn, 0.1);
      let pbr_pfr = exp2(max(pbr_pc0.x, 0.001) * log2(clamp(dot(pbr_ng, normalize(-in.pbr_pos)), 0.0, 1.0))) * pbr_pc0.y;
      let pbr_pramp = textureSampleLevel(tex{4}, tex{4}_samp, vec2f(clamp(pbr_pn + pbr_pfr, 0.0, 1.0), clamp(pbr_pfr, 0.0, 1.0)), 0.0).rgb;
      pbr_base = pbr_base * pbr_pdf.rgb;)""",
                           base, underlying(config.tevStages[mapStage[4]].texCoordId),
                           underlying(config.tevStages[mapStage[4]].texMapId),
                           parallaxBasis,
                           underlying(config.tevStages[mapStage[5]].texMapId));
    }
    // Kind 2: map 4 is a detail map that leaves the base alone where the sampler returns 0.5 (the texture's
    // own format decides which byte that is). Kind 4: the
    // inside shows where the surface is seen edge on (a fresnel that also takes it to the
    // vertex colour) and where the base map's alpha says it is clear, and glows there.
    kinds += fmt::format(R"""(
      if (pbr_kind > 1.5 && pbr_kind < 2.5) {{
          pbr_base = pbr_base * max(sampled{1}.rgb, vec3f(0.0)) * 2.0;
      }}
      if (pbr_kind > 3.5 && pbr_kind < 4.5) {{
          let pbr_kf = pow(clamp(dot(pbr_ng, normalize(-in.pbr_pos)), 0.0, 1.0), max(ubuf.pbr_param.x, 0.001)) *
                       ubuf.pbr_param.y;
          // 2F3FB02B's vertex shader hands the pixel shader |c|^2.2 (no doubling): 034_1 o3, 034_0 temp_245.
          let pbr_ki = mix(pbr_inner, pow(abs(pbr_vraw.rgb), vec3f(2.2)), pbr_kf);
          let pbr_ka = {0}.a * {0}.a * pbr_vraw.a;
          pbr_base = mix(pbr_base, pbr_ki, clamp(pbr_kf + pbr_ka - 1.0, 0.0, 1.0));
          pbr_kglow = pbr_ki * (pbr_ka * ubuf.pbr_layer.z);
      }})""",
                         base, mapStage[4]);
    // Kind 12, the Ice Beam cannon's frost shell, a dissolve: map 4 is noise that the
    // charge (pbr_param.x, times pbr_param.y) eats into. What it leaves, unless the base
    // map's alpha squared times pbr_param.z keeps it, is discarded, and the edge glows
    // pbr_layer_height times pbr_layer.z.
    kinds += fmt::format(R"""(
      if (pbr_kind > 11.5 && pbr_kind < 12.5) {{
          let pbr_dt = clamp(sampled{1}.r - ubuf.pbr_param.x * ubuf.pbr_param.y + 1.0, 0.0, 1.0);
          let pbr_de = clamp(9.99999809 * (1.0 - pbr_dt), 0.0, 1.0);
          if (pbr_de * pbr_de * (3.0 - 2.0 * pbr_de) * ({0}.a * {0}.a * ubuf.pbr_param.z + pbr_dt) < 0.25) {{
              discard;
          }}
          pbr_kglow = ubuf.pbr_layer_height.xyz * (pbr_dt * ubuf.pbr_layer.z);
      }})""",
                         base, mapStage[4]);
    // Kind 35, Remastered's Phazon_Veins_Mat (d61ae63a, the Phazon Beam's veins), permutation 002_0. Constants are
    // GXSetPBRShield's rows: CCH0 (glow gain, ramp gain, amount scale, vertex alpha weight), ICMC in row 6 and DIFC
    // in row 7, whose w is the "DisintegrationAmount" variable. Map 5 is TCH2, a mask the amount eats into: m =
    // clamp(mask + vtx.a CCH0.w - amount CCH0.z (CCH0.w + 2) + 1, 0, 1). What the base alpha squared plus m doesn't
    // lift over 0.25 through the smoothstep of (1 - m) 9.99999809 is discarded. Map 4 is TCH1, the edge ramp, read at
    // (1 - m, 0); its colour times m CCH0.y is the glow (times the exposure, as every glow here); ICMC is added with
    // none. The albedo is the base map times DIFC.rgb.
    if (config.pbrKind == 35 && mapStage[4] != -1 && mapStage[5] != -1) {
      kinds += fmt::format(R"""(
      if (pbr_kind > 34.5 && pbr_kind < 35.5) {{
          let pbr_vc0 = ubuf.pbr_shield[0];
          let pbr_vdf = ubuf.pbr_shield[7];
          let pbr_vm = clamp(sampled{1}.x + pbr_vraw.a * pbr_vc0.w - pbr_vdf.w * pbr_vc0.z * (pbr_vc0.w + 2.0) + 1.0,
                             0.0, 1.0);
          let pbr_vs = clamp((1.0 - pbr_vm) * 9.99999809, 0.0, 1.0);
          if (({0}.a * {0}.a + pbr_vm) * (pbr_vs * pbr_vs * (3.0 - 2.0 * pbr_vs)) < 0.25) {{
              discard;
          }}
          pbr_base = pbr_base * pbr_vdf.rgb;
          pbr_kglow = textureSampleLevel(tex{2}, tex{2}_samp, vec2f(1.0 - pbr_vm, 0.0), 0.0).rgb * (pbr_vm * pbr_vc0.y);
      }})""",
                         base, mapStage[5], underlying(config.tevStages[mapStage[4]].texMapId));
    }
    // Kind 13, a Metroid's dome (Remastered's c83e6fcd, a matcap shell): map 4 is the matcap,
    // read where the normal map's tilt (pbr_param.z, CCH1.x, scaling the map's xy, which are
    // rescaled by 255/128 as in the shader) turns the view-space normal, at 0.5 + 0.5 xy with
    // no flip. Its colour times pbr_layer.z (CCH0.z) times pbr_emissive (DIFC) is the albedo,
    // and the lighting is the standard PBR one on map 1 (a METL map: AO, roughness, metal).
    if (mapStage[2] != -1) {
      kinds += fmt::format(R"""(
      if (pbr_kind > 12.5 && pbr_kind < 13.5) {{
          let pbr_mt = (sampled{0}.rg * 1.9921875 - 1.0) * ubuf.pbr_param.z;
          let pbr_ms = select(0.0, inverseSqrt(pbr_tlen), pbr_tlen > 1e-24);
          // c83e6fcd uses the mapped normal as it is, never normalised.
          let pbr_mm = pbr_t * (pbr_ms * pbr_mt.x) - pbr_b * (pbr_ms * pbr_mt.y) +
                       pbr_ng * sqrt(max(0.0, 1.0 - dot(pbr_mt, pbr_mt)));
          // c83e6fcd's matcap read is an implicit-derivative texture(), so it picks the matcap's
          // mips; the derivatives are taken here, outside the pbr_tlen test, to stay uniform.
          let pbr_muv = 0.5 + 0.5 * pbr_mm.xy;
          let pbr_mc = textureSampleGrad(tex{1}, tex{1}_samp, pbr_muv, dpdx(pbr_muv), dpdy(pbr_muv)).rgb;
          if (pbr_tlen > 1e-24) {{
              pbr_base = max(pbr_mc, vec3f(0.0)) * ubuf.pbr_layer.z * max(ubuf.pbr_emissive.rgb, vec3f(0.0));
          }}
      }})""",
                           mapStage[2], underlying(inner.texMapId));
    }
    // Kind 7, falling water: map 4's three channels are sheets that scroll at speeds of
    // their own (pbr_layer_height the first two, pbr_layer.x and pbr_param.y the third,
    // pbr_param.x the time). The vertex colour says how much of each there is, and their
    // sum picks the colour and opacity from map 0, a ramp whose row is the vertex alpha
    // (pbr_param.z and w and pbr_layer.z are the sum's softness and the ramp's offsets,
    // pbr_emissive the colour). It is unlit.
    {
      const auto ramp = underlying(config.tevStages[mapStage[0]].texMapId);
      layer += fmt::format(R"""(
      let pbr_fuv1 = dpdx(tex{0}_uv);
      let pbr_fuv2 = dpdy(tex{0}_uv);)""",
                           underlying(inner.texCoordId));
      kinds += fmt::format(R"""(
      var pbr_kalpha = 1.0;
      if (pbr_kind > 6.5 && pbr_kind < 7.5) {{
          let pbr_ft = ubuf.pbr_param.x;
          let pbr_fr = fract(vec2f(ubuf.pbr_layer_height.x, ubuf.pbr_layer_height.y) * pbr_ft) * vec2f(1.0, -1.0);
          let pbr_fg = fract(vec2f(ubuf.pbr_layer_height.z, ubuf.pbr_layer_height.w) * pbr_ft) * vec2f(1.0, -1.0);
          let pbr_fb = fract(vec2f(ubuf.pbr_layer.x, ubuf.pbr_param.y) * pbr_ft) * vec2f(1.0, -1.0);
          let pbr_fs = vec3f(textureSampleGrad(tex{0}, tex{0}_samp, tex{1}_uv + pbr_fr, pbr_fuv1, pbr_fuv2).r,
                             textureSampleGrad(tex{0}, tex{0}_samp, tex{1}_uv + pbr_fg, pbr_fuv1, pbr_fuv2).g,
                             textureSampleGrad(tex{0}, tex{0}_samp, tex{1}_uv + pbr_fb, pbr_fuv1, pbr_fuv2).b);
          let pbr_fw = max(pbr_fs + pbr_vraw.rgb * 2.0 + ubuf.pbr_param.w - 1.0, vec3f(0.0));
          let pbr_fsum = pbr_fw.x + pbr_fw.y + pbr_fw.z;
          let pbr_framp = textureSample(tex{2}, tex{2}_samp,
                                        vec2f(pbr_fsum / max(pbr_fsum + ubuf.pbr_param.z, 1.0),
                                              pbr_vraw.a + ubuf.pbr_layer.z));
          pbr_base = vec3f(0.0);
          pbr_kglow = max(pbr_framp.rgb, vec3f(0.0)) * max(ubuf.pbr_emissive.rgb, vec3f(0.0));
          pbr_kalpha = pbr_framp.a;
      }})""",
                           underlying(inner.texMapId), underlying(inner.texCoordId), ramp);
    }
    // Kind 9, a beam's glow on the arm cannon: map 4's three channels are noise that
    // scrolls at speeds of their own (pbr_layer_height the first two, pbr_param.y and z the
    // third, pbr_param.x the time). Their sum less twice the vertex colour's, offset by
    // pbr_param.w, picks the glow from map 5, a ramp whose row is the vertex alpha; it is
    // scaled by its own alpha and pbr_layer.z. The surface under it stays lit. Both maps are
    // sRGB in Remastered, and so are the converted textures (written sRGB when their source
    // format is), so the sampler linearises the noise as well as the ramp; the vertex colour is
    // a plain UNORM attribute and stays as it is.
    if (mapStage[5] != -1) {
      kinds += fmt::format(R"""(
      if (pbr_kind > 8.5 && pbr_kind < 9.5) {{
          let pbr_gt = ubuf.pbr_param.x;
          let pbr_gr = fract(ubuf.pbr_layer_height.xy * pbr_gt) * vec2f(1.0, -1.0);
          let pbr_gg = fract(ubuf.pbr_layer_height.zw * pbr_gt) * vec2f(1.0, -1.0);
          let pbr_gb = fract(vec2f(ubuf.pbr_param.y, ubuf.pbr_param.z) * pbr_gt) * vec2f(1.0, -1.0);
          let pbr_gn = vec3f(textureSampleGrad(tex{0}, tex{0}_samp, tex{1}_uv + pbr_gr, pbr_fuv1, pbr_fuv2).r,
                             textureSampleGrad(tex{0}, tex{0}_samp, tex{1}_uv + pbr_gg, pbr_fuv1, pbr_fuv2).g,
                             textureSampleGrad(tex{0}, tex{0}_samp, tex{1}_uv + pbr_gb, pbr_fuv1, pbr_fuv2).b);
          let pbr_gl = max(pbr_gn, vec3f(0.0));
          let pbr_gs = pbr_gl.x + pbr_gl.y + pbr_gl.z;
          let pbr_gu = pbr_gs - 2.0 * (pbr_vraw.r + pbr_vraw.g + pbr_vraw.b) + 3.0 + ubuf.pbr_param.w;
          let pbr_gramp = textureSampleLevel(tex{2}, tex{2}_samp,
                                             clamp(vec2f(pbr_gu, 1.0 - pbr_vraw.a), vec2f(0.02), vec2f(0.98)), 0.0);
          pbr_kglow = max(pbr_gramp.rgb, vec3f(0.0)) * (pbr_gramp.a * ubuf.pbr_layer.z);
      }})""",
                           underlying(inner.texMapId), underlying(inner.texCoordId),
                           underlying(config.tevStages[mapStage[5]].texMapId));
    }
    if (mapStage[5] != -1) {
      orm = fmt::format("mix({}, sampled{}, pbr_ls)", orm, mapStage[5]);
    }
    // Glass keeps its roughness in map 4's blue: map 5 is its distortion noise.
    orm = fmt::format("select({}, vec4f(1.0, sampled{}.b, 0.0, 1.0), pbr_kind > 7.5 && pbr_kind < 8.5)", orm,
                      mapStage[4]);
    // Glass_DX11 reads its cube at level 1 of a smooth surface.
    orm = fmt::format("select({}, vec4f(1.0, 0.1, 0.0, 1.0), pbr_kind > 10.5 && pbr_kind < 11.5)", orm);
    if (mapStage[6] != -1 && mapStage[2] != -1) {
      normalXy2 = fmt::format("sampled{}.rg", mapStage[6]);
    }
    // The vertex alpha is the layers' weight there and no opacity.
    tintAlpha.clear();
  }
  // Kinds 5 and 6 are a liquid's surface. Their maps move, so they are sampled at
  // coordinates of their own inside the branch, with the derivatives taken outside it.
  std::string liquid;
  if (layered && mapStage[5] != -1 && mapStage[6] != -1) {
    const auto mapOf = [&](int map) { return underlying(config.tevStages[mapStage[map]].texMapId); };
    const auto uv = underlying(config.tevStages[mapStage[0]].texCoordId);
    layer += fmt::format(R"""(
      let pbr_quv1 = dpdx(tex{0}_uv);
      let pbr_quv2 = dpdy(tex{0}_uv);)""",
                         uv);
    // Kind 6, a lava pool: map 4 is a pattern carried along map 6's flow in two phases half
    // a period apart, each fading out as it wraps, and map 5's noise offsets the phase so
    // that the pool does not pulse as one. The pattern's two channels and the heat (the
    // vertex alpha, the flow's speed and a slight shimmer) pick the colour from map 0, a ramp
    // (Remastered's ps b9c24545 004_0). pbr_param is the phase, the flow's reach and the
    // pattern's scale; pbr_layer_height the noise's scale, the heat's gain and the period.
    kinds += fmt::format(R"""(
      if (pbr_kind > 5.5 && pbr_kind < 6.5) {{
          let pbr_qs = vec2f(ubuf.pbr_param.z, ubuf.pbr_param.w);
          let pbr_qns = vec2f(pbr_qs.x * ubuf.pbr_layer_height.x / max(pbr_qs.y, 1e-4), ubuf.pbr_layer_height.x);
          let pbr_qnoise = textureSampleGrad(tex{2}, tex{2}_samp, tex{4}_uv * pbr_qns, pbr_quv1 * pbr_qns,
                                             pbr_quv2 * pbr_qns).r;
          let pbr_qflow = sampled{3}.rg * 2.0 - 1.0;
          let pbr_qph = ubuf.pbr_param.x + pbr_qnoise;
          let pbr_qp0 = fract(pbr_qph);
          let pbr_qp1 = fract(pbr_qph + 0.5);
          let pbr_quv = tex{4}_uv * pbr_qs;
          let pbr_qa = textureSampleGrad(tex{1}, tex{1}_samp, pbr_quv - pbr_qflow * (ubuf.pbr_param.y * pbr_qp0),
                                         pbr_quv1 * pbr_qs, pbr_quv2 * pbr_qs).rg;
          let pbr_qb = textureSampleGrad(tex{1}, tex{1}_samp, pbr_quv - pbr_qflow * (ubuf.pbr_param.y * pbr_qp1) + 0.5,
                                         pbr_quv1 * pbr_qs, pbr_quv2 * pbr_qs).rg;
          let pbr_qw = abs(pbr_qp0 - 0.5) * 2.0;
          let pbr_qxy = mix(pbr_qa, pbr_qb, pbr_qw);
          let pbr_qsec = ubuf.pbr_param.x * ubuf.pbr_layer_height.z;
          let pbr_qheat = clamp(sin(pbr_qw + pbr_qsec) * 0.03 + pbr_vraw.a * 2.0 + min(length(pbr_qflow), 1.0) - 1.0,
                                0.0, 1.0);
          let pbr_qd = pbr_qxy.y - pbr_qxy.x;
          let pbr_qt = clamp((pbr_qheat * ubuf.pbr_layer_height.y + pbr_qd) * 2.5 - 2.5, 0.0, 1.0);
          let pbr_qramp = textureSampleLevel(tex{0}, tex{0}_samp,
                                             clamp(vec2f(pbr_qxy.x + pbr_qd * (3.0 - 2.0 * pbr_qt) * pbr_qt * pbr_qt, pbr_qheat),
                                                   vec2f(0.02), vec2f(0.98)), 0.0).rgb;
          pbr_base = vec3f(0.0);
          pbr_kglow = max(pbr_qramp, vec3f(0.0)) * ubuf.pbr_layer.z;
      }})""",
                         mapOf(0), mapOf(4), mapOf(5), mapStage[6], uv);
    // Kind 5, water: the normal is two copies of map 2 moving across each other
    // (pbr_layer_height is their speeds, pbr_param.x the time, pbr_layer.z the strength).
    // Lava has no normal map.
    if (mapStage[2] != -1) {
      liquid = fmt::format(R"""(
        if (pbr_kind > 4.5 && pbr_kind < 5.5 && pbr_tlen > 1e-24) {{
            let pbr_qw0 = textureSampleGrad(tex{0}, tex{0}_samp, tex{1}_uv + ubuf.pbr_layer_height.xy * ubuf.pbr_param.x,
                                            pbr_quv1, pbr_quv2).rg * 2.0 - 1.0;
            let pbr_qw1 = textureSampleGrad(tex{0}, tex{0}_samp,
                                            tex{1}_uv.yx * 0.73 + ubuf.pbr_layer_height.zw * ubuf.pbr_param.x,
                                            pbr_quv1.yx * 0.73, pbr_quv2.yx * 0.73).gr * 2.0 - 1.0;
            let pbr_qwn = (pbr_qw0 + pbr_qw1) * ubuf.pbr_layer.z;
            let pbr_qws = inverseSqrt(pbr_tlen);
            pbr_n = normalize(pbr_t * (pbr_qws * pbr_qwn.x) - pbr_b * (pbr_qws * pbr_qwn.y) + pbr_ng);
        }})""",
                           mapOf(2), uv);
    }
  }
  const std::string baseRgb =
      inventory ? "inv_b"s :
      layered ? fmt::format("mix(max({0}.rgb, vec3f(0.0)), max(sampled{1}.rgb, vec3f(0.0)), pbr_ls)", base,
                            mapStage[4])
              : fmt::format("max({}.rgb, vec3f(0.0))", base);
  std::string normal;
  if (mapStage[2] != -1) {
    // glTF normal maps are +Y up with V running down the image, so the bitangent is the
    // negated dP/dV. Kind 4 scales the map's tilt. Remastered decodes x 255/128 - 1, so that
    // byte 128 is flat.
    // The layered shaders (7248969B, A978D507) decode each layer's normal in full and mix
    // the vectors, which keeps more tilt at mid weights. Kind 1 (9EFE0D2E) and kind 20
    // (E9DF2188) mix the maps' xy and rebuild z.
    std::string tn = "vec3f(pbr_ts, sqrt(max(0.0, 1.0 - dot(pbr_ts, pbr_ts))))";
    if (!normalXy2.empty()) {
      tn = fmt::format(
          "select(normalize(mix({}, vec3f(pbr_ts2, sqrt(max(0.0, 1.0 - dot(pbr_ts2, pbr_ts2)))), pbr_ls)), "
          "vec3f(pbr_tsm, sqrt(max(0.0, 1.0 - dot(pbr_tsm, pbr_tsm)))), (pbr_kind > 0.5 && pbr_kind < 1.5) || pbr_kind > 19.5)",
          tn);
      normal = fmt::format("\n      let pbr_ts2 = {} * 1.9921875 - 1.0;", normalXy2);
    }
    normal += fmt::format(R"""(
      let pbr_ts = ({0} * 1.9921875 - 1.0) * select(select(1.0, ubuf.pbr_param.z, pbr_kind > 3.5 && pbr_kind < 4.5), ubuf.pbr_shield[0].z, pbr_kind > 27.5 && pbr_kind < 28.5) * select(1.0, pbr_kns, pbr_kind > 31.5 && pbr_kind < 32.5);
      {2}let pbr_tn = {1};
      if (pbr_tlen > 1e-24) {{
        let pbr_s = inverseSqrt(pbr_tlen);
        let pbr_nu = pbr_t * (pbr_s * pbr_tn.x) - pbr_b * (pbr_s * pbr_tn.y) + pbr_ng * pbr_tn.z;
        // 07acff46 (kind 32) lights the mapped normal as it is, never normalised (kb material/07acff46.md);
        // so does 2F3FB02B (kind 4, 034_0 temp_125..127).
        pbr_n = select(normalize(pbr_nu), pbr_nu, (pbr_kind > 31.5 && pbr_kind < 32.5) || (pbr_kind > 3.5 && pbr_kind < 4.5));
      }})""",
                          normalXy, tn,
                          normalXy2.empty() ? "" : "let pbr_tsm = mix(pbr_ts, pbr_ts2, pbr_ls);\n      ");
    if (tangents2 && !normalXy2.empty() && !layeredMacro) {
      // A978D507 / D363D694 (LayerBaseNormal_2TangentStream): each layer's normal is built in
      // its own tangent frame (TANGENT_0 for map 1, TANGENT_1 for map 2) and the two are mixed.
      normal += R"""(
      if (pbr_tlen > 1e-24 && !((pbr_kind > 0.5 && pbr_kind < 1.5) || pbr_kind > 19.5)) {
        let pbr_s2 = inverseSqrt(pbr_tlen);
        let pbr_nl1 = normalize(pbr_t * (pbr_s2 * pbr_ts.x) - pbr_b * (pbr_s2 * pbr_ts.y) +
                                pbr_ng * sqrt(max(0.0, 1.0 - dot(pbr_ts, pbr_ts))));
        let pbr_nl2 = normalize(pbr_t2 * (pbr_s2 * pbr_ts2.x) - pbr_b2 * (pbr_s2 * pbr_ts2.y) +
                                pbr_ng * sqrt(max(0.0, 1.0 - dot(pbr_ts2, pbr_ts2))));
        pbr_n = normalize(mix(pbr_nl1, pbr_nl2, pbr_ls));
      })""";
    }
    // 231F8383 (kind 8) lerps the map's normal in by the vertex colour's blue:
    // normalize(Ng + (N - Ng) x v4.z) (kb material/231f8383.md).
    normal += R"""(
      if (pbr_kind > 7.5 && pbr_kind < 8.5) {
        pbr_n = normalize(pbr_ng + (pbr_n - pbr_ng) * pbr_vraw.b);
      })""";
  }
  const int macroStage = layered ? (layeredMacro ? mapStage[7] : -1) : mapStage[6];
  if (macroStage != -1) {
    // Remastered's macro normal (MNMP): the detail normal M (map 1, TANGENT_0's frame) goes into
    // TANGENT_1's frame, the macro map's xy is added there and z is scaled by its z:
    // x = m.x + T2.M, y = m.y + B2.M, z = N.M sqrt(1 - |m|^2), B2 = cross(N, T2) w (our
    // bitangent is the negated one).
    normal += fmt::format(R"""(
      if (pbr_macro && pbr_tlen > 1e-24) {{
        let pbr_ks = inverseSqrt(pbr_tlen);
        let pbr_km = clamp(sampled{}.rg * 1.9921875 - 1.0, vec2f(-1.0), vec2f(1.0));
        let pbr_kt = {} * pbr_ks;
        let pbr_kb = -{} * pbr_ks;
        let pbr_kx = pbr_km.x + dot(pbr_kt, pbr_n);
        let pbr_ky = pbr_km.y + dot(pbr_kb, pbr_n);
        let pbr_kz = dot(pbr_ng, pbr_n) * sqrt(max(0.0, 1.0 - dot(pbr_km, pbr_km)));
        pbr_n = normalize(pbr_ng * pbr_kz + pbr_kt * pbr_kx + pbr_kb * pbr_ky);
      }})""",
                          macroStage, tangents2 ? "pbr_t2" : "pbr_t", tangents2 ? "pbr_b2" : "pbr_b");
  }
  normal += liquid;
  // And what is seen of it: its own colour in the room's light where it is looked into,
  // the surroundings' reflection where it is seen at a grazing angle (Schlick, pbr_param.w
  // at the normal), and more opaque the more it reflects. pbr_param.z is the opacity seen
  // straight on, pbr_emissive the colour.
  if (!liquid.empty()) {
    liquid = R"""(
      if (pbr_kind > 4.5 && pbr_kind < 5.5) {
          let pbr_qf = ubuf.pbr_param.w + (1.0 - ubuf.pbr_param.w) * pow(1.0 - pbr_nv, 5.0);
          pbr_alpha = mix(ubuf.pbr_param.z, 1.0, pbr_qf);
          pbr_lo = (max(ubuf.pbr_emissive.rgb, vec3f(0.0)) * pbr_ambd * (ubuf.pbr_param.z * (1.0 - pbr_qf)) +
                    pbr_envspec * pbr_qf) / max(pbr_alpha, 1e-3);
          pbr_glow = vec3f(0.0);
      })""";
  }
  if (layered) {
    liquid += R"""(
      if (pbr_kind > 6.5 && pbr_kind < 7.5) {
          pbr_alpha = pbr_kalpha;
      })""";
    // Kind 8, glass (Remastered's 231F8383): map 4 is a mask (red how clear, green the
    // glow, alpha the opacity, each lifted by the vertex colour), and the room behind it
    // (map 7) is seen through it, bent by map 5's noise (pbr_param.x) and tinted
    // (pbr_emissive). pbr_param.z is how fast the clear part loses its opacity, y the
    // reflection's weight, pbr_layer_height the glow's colour and in w the reflection's
    // level. It is drawn with straight alpha (class 1), as Remastered's output rgb holds the
    // scene term too, so what shows through is added after the tone curve and blended by alpha.
    const auto& inner4 = config.tevStages[mapStage[4]];
    std::string through;
    if (screen && config.pbrKind != 31) {
      through = fmt::format(R"""(
          let pbr_gn = ({0} - 0.5) * ubuf.pbr_param.x * vec2f(1.0, -1.0);
          let pbr_guv = in.pbr_scr.xy / in.pbr_scr.w * vec2f(0.5, -0.5) + 0.5 + pbr_gn;
          let pbr_gs = textureSampleLevel(tex7, tex7_samp, clamp(pbr_guv, vec2f(0.0), vec2f(1.0)), 0.0).rgb;
          pbr_pass = srgb_dec(pbr_gs) * max(ubuf.pbr_emissive.rgb, vec3f(0.0)) * pbr_gt;)""",
                            mapStage[5] == -1 ? "vec2f(0.5)"s : fmt::format("sampled{}.rg", mapStage[5]));
    }
    liquid += fmt::format(R"""(
      if (pbr_kind > 7.5 && pbr_kind < 8.5) {{
          let pbr_gm = sampled{0};
          let pbr_gt = clamp(pbr_gm.r * pbr_vraw.b + pbr_vraw.r, 0.0, 1.0);
          pbr_alpha = clamp(pbr_gm.a * pow(max(1.0 - pbr_gt, 1e-6), ubuf.pbr_param.z) * pbr_vraw.a, 0.0, 1.0);
          // The reflection is the bare cube at lod m.z x REFP.x (no occlusion), and the BRDF
          // row is m.z too (TCH0 blue is the roughness, not the MR map's).
          let pbr_gq = pbr_gm.b * pbr_c0 + pbr_c1;
          let pbr_ga = min(pbr_gq.x * pbr_gq.x, exp2(-9.28 * saturate(pbr_nv))) * pbr_gq.x + pbr_gq.y;
          var pbr_gab = vec2f(-1.04, 1.04) * pbr_ga + pbr_gq.zw;
          if (ubuf.pbr_light_scale.z > 0.0) {{
              pbr_gab = textureSampleLevel(pbr_brdf_lut, pbr_cube_samp, vec2f(saturate(pbr_nv), pbr_gm.b), 0.0).rg;
          }}
          var pbr_gcube = pbr_envspec;
          if (pbr_hdr > 0.0 && ubuf.pbr_probe[0].w > 0.0) {{
              pbr_gcube = textureSampleLevel(pbr_cube, pbr_cube_samp, pbr_pd, pbr_gm.b * pbr_lod).rgb * pbr_hdr;
          }}
          pbr_lo = clamp(pbr_gm.g + pbr_vraw.g, 0.0, 1.0) * max(ubuf.pbr_layer_height.xyz, vec3f(0.0)) +
                   pbr_gcube * (pbr_gab.x * pbr_alpha * ubuf.pbr_param.y + pbr_gab.y) * ubuf.pbr_layer_height.w;
          pbr_glow = vec3f(0.0);{1}
      }})""",
                          mapStage[4], through);
    // Kind 11, Remastered's Glass_DX11 (the Waste Disposal tank): the room behind it (map 7)
    // bent by map 4's two channels, which scroll at pbr_param.y (x the time), by pbr_param.z
    // over the distance, and tinted by pbr_emissive where the vertex alpha and pbr_param.w
    // say. Over it the reflection (its fresnel scaled by the vertex alpha and
    // pbr_layer_height.z), map 0 (weight x) and the linearised, doubled vertex colour
    // (weight y). Drawn premultiplied and opaque: the room comes through pbr_pass.
    std::string holo;
    if (screen) {
      holo = fmt::format(R"""(
          let pbr_hs = fract(vec2f(0.25, 1.0) * (ubuf.pbr_param.y * ubuf.pbr_param.x)) * vec2f(1.0, -1.0);
          let pbr_hn = textureSampleGrad(tex{0}, tex{0}_samp, tex{1}_uv + pbr_hs, pbr_fuv1, pbr_fuv2).rg - 0.5;
          let pbr_hd = pbr_hn * (ubuf.pbr_param.z * min(1.0 / max(length(in.pbr_pos), 1e-3), 1.0)) * vec2f(1.0, -1.0);
          let pbr_huv = in.pbr_scr.xy / in.pbr_scr.w * vec2f(0.5, -0.5) + 0.5 + pbr_hd;
          let pbr_hg = textureSampleLevel(tex7, tex7_samp, clamp(pbr_huv, vec2f(0.0), vec2f(1.0)), 0.0).rgb;
          pbr_pass = srgb_dec(pbr_hg) *
                     max(vec3f(1.0) + ubuf.pbr_emissive.rgb * (ubuf.pbr_param.w - 1.0 + pbr_vraw.a), vec3f(0.0));)""",
                         underlying(inner4.texMapId), underlying(inner4.texCoordId));
    }
    liquid += fmt::format(R"""(
      if (pbr_kind > 10.5 && pbr_kind < 11.5) {{
          pbr_alpha = 1.0;
          pbr_lo = pbr_envspec * (pbr_ab.x * pbr_vraw.a * ubuf.pbr_layer_height.z + pbr_ab.y) +
                   max({0}.rgb, vec3f(0.0)) * ubuf.pbr_layer_height.x +
                   2.0 * pow(max(pbr_vraw.rgb, vec3f(0.0)), vec3f(2.2)) * ubuf.pbr_layer_height.y;
          pbr_glow = vec3f(0.0);{1}
      }})""",
                          base, holo);
    // Kind 13's opacity is the vertex alpha times DIFC.a (pbr_layer_height.y), blended. Its
    // rim, 1 - |n.z|^pbr_param.x - pbr_param.w (CCH0.x, CCH1.y), times the vertex colour and
    // pbr_param.y (CCH0.y), is at inverse exposure (c4[0].z), so it is divided by the room-exposure
    // scaling below where that applies, as kind 32's ICMC.
    liquid += R"""(
      if (pbr_kind > 12.5 && pbr_kind < 13.5) {
          pbr_alpha = clamp(pbr_vraw.a * ubuf.pbr_layer_height.y, 0.0, 1.0);
          let pbr_mrim = clamp(1.0 - pow(abs(pbr_n.z), max(ubuf.pbr_param.x, 1e-3)) - ubuf.pbr_param.w, 0.0, 1.0);
          pbr_glow += pbr_mrim * pbr_vraw.rgb * ubuf.pbr_param.y /
                      select(1.0, ubuf.pbr_tone[0].w, ubuf.pbr_tone[1].x > 0.0 && ubuf.pbr_tone[0].w > 0.0);
      })""";
    // Kind 21's glow, at the inverse of the exposure: the pulsing CCH3 glow in the AO cavities (blue mask) and the
    // ramp's colour where the vertex alpha says, scaled by the vertex red.
    if (config.pbrKind == 21 && mapStage[2] != -1 && mapStage[4] != -1 && mapStage[5] != -1) {
      liquid += R"""(
      pbr_glow += (pbr_zph + 2.0) * ubuf.pbr_shield[3].rgb * (ubuf.pbr_shield[0].z * pbr_zs2) +
                  pbr_zgrad * (pbr_zt2 * pbr_vraw.r * ubuf.pbr_shield[0].x * ubuf.pbr_shield[0].y);)""";
    }
    // Kind 32's glow: the ramp colour times CCH0.w, times the exposure (the generic scaling below), plus ICMC, which
    // has none (so it is divided by the scaling here where that applies).
    if (config.pbrKind == 32 && mapStage[2] != -1 && mapStage[4] != -1 && mapStage[5] != -1) {
      liquid += R"""(
      pbr_glow += pbr_pramp * pbr_pc0.w;
      pbr_glow += ubuf.pbr_shield[6].rgb /
                  select(1.0, ubuf.pbr_tone[0].w, ubuf.pbr_tone[1].x > 0.0 && ubuf.pbr_tone[0].w > 0.0);)""";
    }
    // Kind 35's ICMC, with no exposure (undone here where the generic scaling applies).
    if (config.pbrKind == 35 && mapStage[4] != -1 && mapStage[5] != -1) {
      liquid += R"""(
      pbr_glow += ubuf.pbr_shield[6].rgb /
                  select(1.0, ubuf.pbr_tone[0].w, ubuf.pbr_tone[1].x > 0.0 && ubuf.pbr_tone[0].w > 0.0);)""";
    }
    // Kind 22's glow: Remastered adds it with no exposure factor, so the room-exposure scale below is undone for it.
    if (config.pbrKind == 22 && mapStage[2] != -1 && mapStage[4] != -1 && mapStage[5] != -1) {
      liquid += R"""(
      pbr_glow += pbr_zcav * ubuf.pbr_shield[3].rgb +
                  pbr_zgrad * (4.0 * pbr_zt * pbr_vraw.r * ubuf.pbr_shield[0].x * ubuf.pbr_shield[0].y);)""";
    }
    // Kind 14, Remastered's BoundaryShield_Ship1_DX11 (the Frigate's force fields), permutation
    // 002_0. Its constants are GXSetPBRShield's rows: CCH0..CCH6, then DIFC. Map 0 is BCLR, map 4
    // TCH0 (the field's pattern) and map 5 TCH1 (a noise), each at its own UV set. pbr_param.x is
    // the time. Unlit and alpha-blended: the glow is written at the exposure's inverse (the generic
    // scaling re-applies it), and the screen behind (map 7), bent by the noise,
    // comes through pbr_pass.
    if (screen && mapStage[5] != -1) {
      liquid += fmt::format(R"""(
      if (pbr_kind > 13.5 && pbr_kind < 14.5) {{
          let pbr_c0 = ubuf.pbr_shield[0];
          let pbr_c1 = ubuf.pbr_shield[1];
          let pbr_c2 = ubuf.pbr_shield[2];
          let pbr_c3 = ubuf.pbr_shield[3];
          let pbr_c4 = ubuf.pbr_shield[4];
          let pbr_c5 = ubuf.pbr_shield[5];
          let pbr_c6 = ubuf.pbr_shield[6];
          let pbr_df = ubuf.pbr_shield[7];
          let pbr_sb = {4};
          let pbr_uvm = tex{1}_uv;
          let pbr_uvg = tex{3}_uv;
          let pbr_m1 = dpdx(pbr_uvm);
          let pbr_m2 = dpdy(pbr_uvm);
          let pbr_g1 = dpdx(pbr_uvg);
          let pbr_g2 = dpdy(pbr_uvg);
          let pbr_t = ubuf.pbr_param.x;
          let pbr_sT = pbr_t * pbr_c3.y;
          let pbr_sq = vec2f((pbr_sT + pbr_sb.x) % 0.45, (pbr_sT + pbr_sb.y) % 0.45);
          let pbr_sm = mix(pbr_sb.x, pbr_sb.y, pbr_c3.x);
          let pbr_sn = textureSampleGrad(tex{2}, tex{2}_samp,
                                         vec2f(pbr_uvg.x + 0.1 * pbr_sq.x * pbr_c1.y * pbr_vraw.a + pbr_t * pbr_c5.z,
                                               pbr_uvg.y + 0.1 * pbr_sq.y * pbr_c1.y * pbr_vraw.a) * pbr_c5.w,
                                         pbr_g1 * pbr_c5.w, pbr_g2 * pbr_c5.w).x;
          let pbr_sk = mix(pbr_sn, 1.0 - pbr_sn, pbr_c6.w);
          let pbr_sr = 2.0 * pbr_sk * (pbr_uvm - 0.5);
          let pbr_sa = textureSampleGrad(tex{0}, tex{0}_samp,
                                         pbr_uvm + pbr_sm + fract(0.1 * pbr_sT - pbr_sb.z), pbr_m1, pbr_m2).x;
          let pbr_sbw = textureSampleGrad(tex{0}, tex{0}_samp,
                                          pbr_uvm + pbr_sm + fract(1.25 * pbr_sT - pbr_sb.z), pbr_m1, pbr_m2).w;
          let pbr_s2 = textureSampleGrad(tex{0}, tex{0}_samp,
                                         pbr_uvm + 0.1 * pbr_vraw.a * (pbr_sq * pbr_c1.z + pbr_sr * pbr_c6.x),
                                         pbr_m1, pbr_m2);
          let pbr_sp = pow(clamp(pbr_sa - 0.1, 0.0, 1.0), 2.0);
          let pbr_sc = pbr_s2.w * clamp(pbr_sbw - 0.1, 0.0, 1.0);
          let pbr_tri = abs(1.0 - 2.0 * fract(pbr_t * pbr_c3.z));
          let pbr_sg = 1.5 * pbr_sb.w * mix(pbr_sp * pbr_sc, pbr_sp + pbr_sc, pbr_tri);
          let pbr_g1c = clamp(pbr_sg, 0.0, 1.0);
          let pbr_pulse = 0.5 + 0.5 * sin(pbr_t * pbr_c3.w);
          let pbr_colg = 0.5 * pbr_c0.rgb + (pbr_sg * pbr_c0.rgb - 0.5 * pbr_c0.rgb) * pbr_g1c;
          let pbr_kf = 1.0 - pbr_df.x * (1.0 - pbr_sk);
          let pbr_sal = clamp(pbr_sb.x * pbr_sb.w + 2.0 * pbr_c5.x * pbr_df.w - 1.0, 0.0, 1.0);
          let pbr_sw = pbr_c6.z * max(min(pbr_g1c, pbr_c4.w), pbr_c4.z) + max(pbr_kf, pbr_c4.z);
          let pbr_sw2 = mix(pbr_sw, pbr_c4.w, pbr_s2.y);
          let pbr_sl = pbr_kf * pbr_c0.rgb * pbr_c5.y + pbr_pulse * pbr_s2.z * pbr_c2.rgb * pbr_c2.w +
                       pbr_s2.y * pbr_c0.rgb * pbr_c1.w +
                       (2.0 - pbr_df.x) * pbr_g1c * pbr_colg * (1.0 + 9.0 * pbr_s2.y) * pbr_c0.w + pbr_c1.x * pbr_c2.rgb;
          // Remastered writes the glow times c3.z (the tone curve's inverse exposure), so it nets
          // to the glow itself on screen. The generic scaling below applies the exposure, so the
          // inverse goes in here.
          var pbr_sx = 1.0;
          if (ubuf.pbr_tone[1].x > 0.0 && ubuf.pbr_tone[0].w > 0.0) {{
              pbr_sx = 1.0 / ubuf.pbr_tone[0].w;
          }}
          let pbr_sd = min(abs(1.0 / min(in.pbr_pos.z, -1e-3)), 1.0);
          let pbr_suv = in.pbr_scr.xy / in.pbr_scr.w * vec2f(0.5, -0.5) + 0.5 +
                        pbr_vraw.a * pbr_sd * vec2f(pbr_sq.x * pbr_c4.y + pbr_sr.x * pbr_c6.y,
                                                    -(pbr_sq.y * pbr_c4.y - pbr_sr.y * pbr_c6.y)) +
                        vec2f(0.0, 0.75 * pbr_sd);
          let pbr_sfb = textureSampleLevel(tex7, tex7_samp, clamp(pbr_suv, vec2f(0.0), vec2f(1.0)), 0.0).rgb;
          pbr_alpha = pbr_sal * pbr_c4.w;
          pbr_lo = vec3f(0.0);
          pbr_glow = pbr_sl * ((10.0 - 9.0 * pbr_sal) * pbr_sx * pbr_sw2);
          pbr_pass = srgb_dec(pbr_sfb) * pbr_c4.x * pbr_vraw.rgb;
      }})""",
                            underlying(config.tevStages[mapStage[4]].texMapId),
                            underlying(config.tevStages[mapStage[4]].texCoordId),
                            underlying(config.tevStages[mapStage[5]].texMapId),
                            underlying(config.tevStages[mapStage[5]].texCoordId), base);
    }
    // Kind 29, Remastered's HoloGlass (3991DA00, the intro's hologram glass and the Mines' energy
    // glass). Map 0 is BCLR (alpha = roughness, raw), map 4 TCH0 and map 6 TCH2 are indirect
    // offsets, map 5 TCH1 a colour, each at its own UV set; map 7 is the screen copy (mipped as
    // kind 23's). Constants are GXSetPBRShield's rows CCH0..CCH3 and DIFC (row 7); row 4 x is the
    // object's phase and row 6 w says whether the material has a cube of its own. Unlit and alpha
    // blended: the sum is all glow (no exposure factor) times DIFC, and the room behind it,
    // bent by TCH2, comes through pbr_pass.
    if (screen && mapStage[0] != -1 && mapStage[5] != -1 && mapStage[6] != -1) {
      liquid += fmt::format(R"""(
      if (pbr_kind > 28.5 && pbr_kind < 29.5) {{
          let pbr_h0 = ubuf.pbr_shield[0];
          let pbr_h2 = ubuf.pbr_shield[2];
          let pbr_h3 = ubuf.pbr_shield[3];
          let pbr_ph = ubuf.pbr_shield[4].x;
          let pbr_df = ubuf.pbr_shield[7];
          let pbr_t = ubuf.pbr_param.x;
          let pbr_uvb = tex{1}_uv;
          let pbr_uva = tex{3}_uv;
          let pbr_uvc = tex{5}_uv;
          let pbr_uve = tex{7}_uv;
          let pbr_i1 = textureSampleGrad(tex{2}, tex{2}_samp, pbr_uva, dpdx(pbr_uva), dpdy(pbr_uva)).xy;
          let pbr_sec = textureSampleGrad(tex{4}, tex{4}_samp, pbr_uvc, dpdx(pbr_uvc), dpdy(pbr_uvc));
          let pbr_i2 = textureSampleGrad(tex{6}, tex{6}_samp,
                                         pbr_uve + vec2f(pbr_t * 0.25 * pbr_h3.y + pbr_ph, pbr_t * pbr_h3.y + pbr_ph),
                                         dpdx(pbr_uve), dpdy(pbr_uve)).xy;
          let pbr_r = {0}.a;
          let pbr_uvb2 = pbr_uvb + (pbr_i1 - 0.5) * pbr_h0.x + (pbr_h0.z * pbr_t * 0.5 + pbr_ph);
          let pbr_bc = textureSampleGrad(tex{8}, tex{8}_samp, pbr_uvb2, dpdx(pbr_uvb), dpdy(pbr_uvb)).rgb;
          let pbr_v5 = vec4f(2.0 * pow(abs(pbr_vraw.rgb), vec3f(2.2)), pbr_vraw.a);
          let pbr_vx = min(abs(1.0 / min(in.pbr_pos.z, -1e-3)), 1.0);
          let pbr_suv = in.pbr_scr.xy / in.pbr_scr.w * vec2f(0.5, -0.5) + 0.5 +
                        (pbr_i2 - 0.5) * (pbr_h3.x * pbr_vx) * vec2f(1.0, -1.0);
          let pbr_sdim = vec2f(textureDimensions(tex7));
          let pbr_smips = ceil(log2(max(pbr_sdim.x, pbr_sdim.y))) + 1.0;
          let pbr_slod = max(0.0, pbr_smips * pbr_h3.w * (1.0 - pbr_sec.a) - 1.0);
          let pbr_sscene = srgb_dec(textureSampleLevel(tex7, tex7_samp, clamp(pbr_suv, vec2f(0.0), vec2f(1.0)), pbr_slod).rgb);
          let pbr_snv = saturate(-dot(normalize(in.pbr_pos), pbr_n));
          let pbr_sq = pbr_r * pbr_c0 + pbr_c1;
          let pbr_sa = min(pbr_sq.x * pbr_sq.x, exp2(-9.28 * pbr_snv)) * pbr_sq.x + pbr_sq.y;
          var pbr_sab = vec2f(-1.04, 1.04) * pbr_sa + pbr_sq.zw;
          if (ubuf.pbr_light_scale.z > 0.0) {{
              pbr_sab = textureSampleLevel(pbr_brdf_lut, pbr_cube_samp, vec2f(pbr_snv, pbr_r), 0.0).rg;
          }}
          var pbr_scube = vec3f(0.0);
          if (ubuf.pbr_shield[6].w > 0.0) {{
              let pbr_sqc = textureSampleLevel(pbr_cube, pbr_cube_samp, pbr_pd, pbr_r * pbr_lod).rgb;
              pbr_scube = select(srgb_dec(pbr_sqc), pbr_sqc * pbr_hdr, pbr_hdr > 0.0);
          }}
          pbr_alpha = clamp(pbr_v5.a * pbr_df.a, 0.0, 1.0);
          pbr_lo = vec3f(0.0);
          pbr_glow = (pbr_scube * (pbr_sab.x * (pbr_v5.a * pbr_h0.y) + pbr_sab.y) + pbr_v5.rgb * pbr_h2.y +
                      pbr_bc * pbr_h0.w + pbr_sec.rgb * pbr_h2.w) * pbr_df.rgb * pbr_df.a;
          pbr_pass = pbr_sscene * pbr_h3.z * pbr_df.rgb * pbr_df.a;
          if (ubuf.pbr_shield[5].x > 0.5) {{
              // 2f95a061: the bent room is tinted by CCH1, and what is behind keeps (o1) of itself
              // (SRC1_COLOR): DIFC.a x (1 - CCH1 x (1 - vertex alpha)) per channel. Drawn opaque, the
              // room (map 7 at the fragment, unbent) comes through pbr_pass.
              let pbr_h1 = ubuf.pbr_shield[1];
              let pbr_sdst = srgb_dec(textureSampleLevel(tex7, tex7_samp,
                                      clamp(in.pbr_scr.xy / in.pbr_scr.w * vec2f(0.5, -0.5) + 0.5, vec2f(0.0), vec2f(1.0)), 0.0).rgb);
              pbr_alpha = 1.0;
              pbr_pass = pbr_sscene * pbr_h3.z * pbr_h1.rgb * pbr_df.rgb * pbr_df.a +
                         pbr_sdst * (pbr_df.a * (vec3f(1.0) - pbr_h1.rgb * (1.0 - pbr_v5.a)));
          }}
      }})""",
                          base,
                          underlying(config.tevStages[mapStage[0]].texCoordId),
                          underlying(config.tevStages[mapStage[4]].texMapId),
                          underlying(config.tevStages[mapStage[4]].texCoordId),
                          underlying(config.tevStages[mapStage[5]].texMapId),
                          underlying(config.tevStages[mapStage[5]].texCoordId),
                          underlying(config.tevStages[mapStage[6]].texMapId),
                          underlying(config.tevStages[mapStage[6]].texCoordId),
                          underlying(config.tevStages[mapStage[0]].texMapId));
    }
    // Kind 15, Remastered's PickUp (3E95A9FE, the item pickups' rings and beams), permutation 000_0.
    // Its constants are GXSetPBRShield's rows: CCH0..CCH3, rows 4 and 5 (world x and y as a dot
    // of the view-space position with xyz, plus w; the game fills them) and DIFC in row 7. Map 0
    // is BCLR, a three-channel mask, map 2 the normal map and map 4 TCH0, a gradient read at the
    // world position and scrolled along V by CCH0.z a second. Unlit and alpha blended: all of it
    // is glow, at the screen level of the glow x DIFC plus the gradient's own term (Cg), which
    // is exposure x Cg. The vertex shader's travelling sine bump is not drawn.
    if (mapStage[2] != -1 && mapStage[4] != -1) {
      liquid += fmt::format(R"""(
      if (pbr_kind > 14.5 && pbr_kind < 15.5) {{
          let pbr_c0 = ubuf.pbr_shield[0];
          let pbr_c1 = ubuf.pbr_shield[1];
          let pbr_c2 = ubuf.pbr_shield[2];
          let pbr_c3 = ubuf.pbr_shield[3];
          let pbr_df = ubuf.pbr_shield[7];
          let pbr_pn = sampled{0}.rg * (pbr_c0.w * 1.9921875) - 1.0;
          let pbr_pnz = sqrt(1.0 - clamp(dot(pbr_pn, pbr_pn), 0.0, 1.0));
          var pbr_pc = abs(pbr_ng.z);
          if (pbr_tlen > 1e-24) {{
              let pbr_ps = inverseSqrt(pbr_tlen);
              pbr_pc = abs(normalize(pbr_t * (pbr_ps * pbr_pn.x) - pbr_b * (pbr_ps * pbr_pn.y) + pbr_ng * pbr_pnz).z);
          }}
          let pbr_pcl = max(pbr_pc, 1e-6);
          let pbr_pf1 = max(pow(clamp(1.0 - pow(pbr_pcl, pbr_c0.y), 0.0, 1.0), 2.0), pbr_c1.z);
          let pbr_pfb = pow(clamp(1.0 - pow(pbr_pcl, pbr_c0.x), 0.0, 1.0), 2.0);
          let pbr_pw = vec2f(dot(ubuf.pbr_shield[4].xyz, in.pbr_pos) + ubuf.pbr_shield[4].w,
                             dot(ubuf.pbr_shield[5].xyz, in.pbr_pos) + ubuf.pbr_shield[5].w);
          let pbr_puv = vec2f(pbr_pw.x * pbr_c1.x, pbr_pw.y * pbr_c1.x - ubuf.pbr_param.x * pbr_c0.z);
          let pbr_pg = max(textureSampleGrad(tex{1}, tex{1}_samp, pbr_puv, dpdx(pbr_puv), dpdy(pbr_puv)).rgb,
                           vec3f(0.0));
          let pbr_pcg = pbr_pf1 * pbr_pg * pbr_c1.w;
          let pbr_ph = {2}.rgb;
          let pbr_pgl = (pbr_ph.x * pbr_vraw.rgb * pbr_c1.y + pbr_ph.y * pbr_pcg.x * pbr_c2.rgb * pbr_c2.w) * pbr_pfb;
          // The generic scaling after this block applies the room's exposure: Remastered's sum
          // nets to itself on screen (times DIFC) and Cg to exposure x Cg, so only Cg keeps it.
          // The vertex colour is the model's own cyan tint (R~0.1, G~0.65, B~0.93).
          var pbr_ix = 1.0;
          if (ubuf.pbr_tone[1].x > 0.0 && ubuf.pbr_tone[0].w > 0.0) {{
              pbr_ix = 1.0 / ubuf.pbr_tone[0].w;
          }}
          pbr_alpha = clamp(pbr_df.w * pbr_c3.w, 0.0, 1.0);
          pbr_lo = vec3f(0.0);
          pbr_glow = (pbr_pcg + pbr_pgl + pbr_ph.z * pbr_vraw.rgb) * pbr_df.rgb * pbr_ix + pbr_pcg;
          pbr_pass = vec3f(0.0);
      }})""",
                          mapStage[2], underlying(config.tevStages[mapStage[4]].texMapId), base);
    }
  }
  // Kind 23, Remastered's Distortion2 (24670BF0, the ice walls and flesh glass), permutations 018 and
  // 002 (lightmapped). Map 0 is BCLR, map 2 NMAP, map 7 the screen copy with its mips. CCH0
  // is in pbr_param: x the normal map's distortion, y the geometry normal's, z the fresnel
  // power, w its scale. r = BCLR.a^2 is the roughness and the blur of the copy and the cube. The
  // surface is the room behind it, bent by the maps, times BCLR x the vertex colour (v3, alpha raw),
  // plus the room cube's reflection (F0 0.04, no metal, no AO, no direct light). On a lightmap the
  // diffuse part is the baked level L0 x BLCM for what the vertex alpha leaves of the glass.
  // Opaque (alpha 1); the room comes through pbr_pass, after the tone curve.
  if ((config.pbrKind == 23 || config.pbrKind == 30) && screen && mapStage[2] != -1) {
    liquid += fmt::format(R"""(
      if ((pbr_kind > 22.5 && pbr_kind < 23.5) || (pbr_kind > 29.5 && pbr_kind < 30.5)) {{
          let pbr_xr = {0}.a * {0}.a;
          let pbr_xv3 = vec4f(2.0 * pow(abs(pbr_vraw.rgb), vec3f(2.2)), pbr_vraw.a);
          let pbr_xnm = sampled{1}.rg;
          let pbr_xvd = normalize(in.pbr_pos);
          let pbr_xf = pow(saturate(dot(pbr_ng, -pbr_xvd)), max(ubuf.pbr_param.z, 0.001)) * ubuf.pbr_param.w;
          let pbr_xs = min(abs(1.0 / min(in.pbr_pos.z, -1e-3)), 1.0) * 10.0;
          let pbr_xd = vec2f((pbr_xnm.x * 0.99609375 - 0.5) * ubuf.pbr_param.x + pbr_ng.x * (1.0 - pbr_xf) * ubuf.pbr_param.y,
                             (pbr_xnm.y * 0.99609375 - 0.5) * ubuf.pbr_param.x + pbr_ng.y * (pbr_xf - 1.0) * ubuf.pbr_param.y) * pbr_xs;
          let pbr_xdim = vec2f(textureDimensions(tex7));
          // The engine's copy has ceil(log2(max side)) + 1 mips.
          let pbr_xmips = ceil(log2(max(pbr_xdim.x, pbr_xdim.y))) + 1.0;
          let pbr_xlod = max(0.0, pbr_xr * (0.5 * pbr_xmips - 2.0) + 1.0);
          let pbr_xuv = in.pbr_scr.xy / in.pbr_scr.w * vec2f(0.5, -0.5) + 0.5 + pbr_xd * vec2f(1.0, -1.0);
          let pbr_xscene = srgb_dec(textureSampleLevel(tex7, tex7_samp, clamp(pbr_xuv, vec2f(0.0), vec2f(1.0)), pbr_xlod).rgb);
          let pbr_xnv = saturate(-dot(pbr_xvd, pbr_n));
          let pbr_xrough = max(pbr_xr, 0.02);
          let pbr_xq = pbr_xrough * pbr_c0 + pbr_c1;
          let pbr_xa = min(pbr_xq.x * pbr_xq.x, exp2(-9.28 * pbr_xnv)) * pbr_xq.x + pbr_xq.y;
          var pbr_xab = vec2f(-1.04, 1.04) * pbr_xa + pbr_xq.zw;
          if (ubuf.pbr_light_scale.z > 0.0) {{
              pbr_xab = textureSampleLevel(pbr_brdf_lut, pbr_cube_samp, vec2f(pbr_xnv, pbr_xrough), 0.0).rg;
          }}
          var pbr_xenv = pbr_envspec;
          if (pbr_hdr > 0.0 && ubuf.pbr_probe[0].w > 0.0 && ubuf.pbr_probe[2].w > 0.0) {{
              let pbr_xc = textureSampleLevel(pbr_cube, pbr_cube_samp, pbr_pd, pbr_xr * pbr_lod).rgb * pbr_hdr;
              var pbr_xl = 1.0;
              if (ubuf.pbr_lmap_rect.w != 0.0) {{
                  pbr_xl = max(pbr_l0v.r, max(pbr_l0v.g, pbr_l0v.b)) * dot(pbr_blcm, vec3f(0.2126, 0.7152, 0.0722));
              }} else if (pbr_kind > 29.5 && pbr_bmeanok) {{
                  // cc8afd0f's probe perms (BLPD 008-011, grid 014-017): the same occlusion from the mean.
                  pbr_xl = max(pbr_bmean.r, max(pbr_bmean.g, pbr_bmean.b)) * dot(pbr_blcm, vec3f(0.2126, 0.7152, 0.0722));
              }}
              pbr_xenv = pbr_xc * mix(ubuf.pbr_probe[1].w, 1.0, saturate(pbr_xl * ubuf.pbr_probe[2].w));
          }}
          let pbr_xbase = {0}.rgb * pbr_xv3.rgb;
          pbr_alpha = 1.0;
          pbr_glow = vec3f(0.0);
          pbr_lo = pbr_xenv * (pbr_xab.x * 0.04 + pbr_xab.y);
          if (ubuf.pbr_lmap_rect.w != 0.0) {{
              pbr_lo += pbr_xbase * pbr_l0v * pbr_blcm * ((1.0 - pbr_xv3.w) / pbr_pi);
          }} else if (pbr_kind > 29.5 && pbr_bmeanok) {{
              pbr_lo += pbr_xbase * pbr_bmean * pbr_blcm * ((1.0 - pbr_xv3.w) / pbr_pi);
          }}
          pbr_pass = select(pbr_xbase, vec3f(1.0), pbr_kind > 29.5) * pbr_xv3.w * pbr_xscene;
      }})""",
                        base, mapStage[2]);
  }
  // Kinds 16 and 17, Remastered's unlit holograms 86CD1703 and 6344950D (permutation 002_0,
  // additive): rgb = DIFT x DIFC + ICNC + ICMC (+ the material's own REFL cube at the reflection
  // vector for 17), alpha = DIFT.a^2 x DIFC.a. No exposure factor in either, so none here (this
  // block runs after the generic scaling). Constants: row 6 = ICNC + ICMC (rgb) and, for 17, the
  // cube's gain in w (0 when the material's cube is the black default); row 7 = DIFC.
  if (mapStage[0] != -1) {
    liquid += fmt::format(R"""(
    if (pbr_kind > 15.5 && pbr_kind < 17.5) {{
        let pbr_yi = ubuf.pbr_shield[6];
        let pbr_yd = ubuf.pbr_shield[7];
        var pbr_yc = max({0}.rgb, vec3f(0.0)) * pbr_yd.rgb + pbr_yi.rgb;
        if (pbr_kind > 16.5 && pbr_yi.w > 0.0) {{
            let pbr_yq = textureSampleLevel(pbr_cube, pbr_cube_samp, pbr_pd, 0.0).rgb;
            pbr_yc += select(srgb_dec(pbr_yq), pbr_yq * pbr_hdr, pbr_hdr > 0.0) * pbr_yi.w;
        }}
        pbr_alpha = clamp({0}.a * {0}.a * pbr_yd.w, 0.0, 1.0);
        pbr_lo = vec3f(0.0);
        pbr_glow = pbr_yc;
        pbr_pass = vec3f(0.0);
    }})""",
                        base);
    // Kind 18, Remastered's Hologram (4CA0017C, permutation 000_0): BCLR scrolled up through
    // world space with a sawtooth warp, a flicker, and a fresnel-like fade by the normal.
    // Constants: rows 0-3 CCH0..CCH3, rows 4 and 5 world x and y (filled by the game, as the
    // pickup's), row 6 ICMC, row 7 DIFC. The vertex colour is linearised 2 pow(|c|, 2.2): red
    // scales the glow, green is a mask. Only the glow term is at the room's exposure. The
    // fade and the vertex shader's collapse are not drawn (fade 1).
    liquid += fmt::format(R"""(
    if (pbr_kind > 17.5 && pbr_kind < 18.5) {{
        let pbr_yc0 = ubuf.pbr_shield[0];
        let pbr_yc1 = ubuf.pbr_shield[1];
        let pbr_yc2 = ubuf.pbr_shield[2];
        let pbr_yc3 = ubuf.pbr_shield[3];
        let pbr_yd = ubuf.pbr_shield[7];
        let pbr_yt = ubuf.pbr_param.x;
        let pbr_yw = vec2f(dot(ubuf.pbr_shield[4].xyz, in.pbr_pos) + ubuf.pbr_shield[4].w,
                           dot(ubuf.pbr_shield[5].xyz, in.pbr_pos) + ubuf.pbr_shield[5].w);
        let pbr_yv = 2.0 * pow(abs(pbr_vraw.rg), vec2f(2.2));
        let pbr_yper = max(pbr_yc1.z, 1e-4);
        let pbr_ys = pbr_yt - pbr_yper * floor(pbr_yt / pbr_yper);
        let pbr_yk = pbr_yc1.y + pbr_ys * (1.0 - pbr_yc1.y);
        let pbr_ysc = select(1e-4, pbr_yc0.x, abs(pbr_yc0.x) > 1e-4);
        let pbr_yuv = vec2f(pbr_yw.x / pbr_ysc, pbr_yk * (pbr_yw.y * pbr_yc0.x + pbr_yt * pbr_yc0.y - pbr_yc1.w));
        let pbr_yb = textureSampleGrad(tex{1}, tex{1}_samp, pbr_yuv, dpdx(pbr_yuv), dpdy(pbr_yuv));
        let pbr_yj = 0.01 * pbr_yc0.z * pbr_yc0.w * sin(6.2831853 * pbr_yt * pbr_yc1.x);
        let pbr_ya = pbr_yb.a * select(0.75, 1.0, pbr_yj > 0.0);
        let pbr_ym = pbr_yv.y > 0.01;
        let pbr_yri = max(pbr_yb.rgb, vec3f(0.0)) * pbr_yd.rgb;
        let pbr_yg = pbr_yri * select(pbr_yv.x, 1.0, pbr_ym) * pbr_yc0.z + vec3f(pbr_yj);
        let pbr_yp = pow(max(abs(pbr_ng.z), 1e-6), pbr_yc3.z);
        let pbr_yrm = pbr_yc3.w + (1.0 - pbr_yc3.w) * (1.0 - pbr_yp * (1.0 - pbr_yv.y));
        var pbr_yx = 1.0;
        if (ubuf.pbr_tone[1].x > 0.0 && ubuf.pbr_tone[0].w > 0.0) {{
            // c3[0].z is the inverse tonemap exposure; the glow is scaled by the exposure below.
            pbr_yx = 1.0 / ubuf.pbr_tone[0].w;
        }}
        pbr_alpha = clamp(pbr_yrm * select(pbr_ya, pbr_yv.y * pbr_ya * pbr_yc2.w, pbr_ym), 0.0, 1.0);
        pbr_lo = vec3f(0.0);
        pbr_glow = pbr_yri + pbr_yx * pbr_yg * (1.0 + pbr_yrm * pbr_yc3.y) + ubuf.pbr_shield[6].rgb;
        pbr_pass = vec3f(0.0);
    }})""",
                        base, underlying(config.tevStages[mapStage[0]].texMapId));
  }
  // Kind 19, Remastered's lit sphere-map shader 98F0556D (permutation 002_0): L is the ambient
  // plus the lights' N.L; rgb = REFV x REFS(0.5 + 0.5 n.xy) x luminance(L) + DIFT x DIFC x L / pi
  // + ICNC + ICMC (the base map's albedo is DIFT; row 7 = DIFC; no 1/pi, as for the other lit kinds). Map 4 is REFS, map 5 REFV.
  // Row 6 = ICNC + ICMC (at the room's exposure), w = the retail konst alpha of a particle model (1 otherwise).
  // Alpha = DIFT.a^2 x DIFC.a x that; it only counts where the converter gave the material retail's blend (4,5).
  if (mapStage[4] != -1 && mapStage[5] != -1) {
    liquid += fmt::format(R"""(
    if (pbr_kind > 18.5 && pbr_kind < 19.5) {{
        let pbr_zl = (pbr_ambd + pbr_lnl) * pbr_ao;
        let pbr_zs = max(textureSampleLevel(tex{0}, tex{0}_samp, 0.5 + 0.5 * pbr_n.xy, 0.0).rgb, vec3f(0.0));
        let pbr_zv = max(sampled{1}.rgb, vec3f(0.0));
        pbr_lo = pbr_zv * pbr_zs * dot(pbr_zl, vec3f(0.2126, 0.7152, 0.0722)) + pbr_base * pbr_zl * ubuf.pbr_shield[7].rgb;
        pbr_glow = ubuf.pbr_shield[6].rgb;
        pbr_alpha = clamp({2}.a * {2}.a * ubuf.pbr_shield[7].w * ubuf.pbr_shield[6].w, 0.0, 1.0);
        pbr_pass = vec3f(0.0);
    }})""",
                        underlying(config.tevStages[mapStage[4]].texMapId), mapStage[5], base);
  }
  // Kind 25, Remastered's dda64c97 (the Eyon's eyeball_gloss, permutation 042_0): kind 19's matcap, but read at
  // the normal map's N' (pbr_n), and bfb300b6's lighting kept: rgb = REFV x REFS(0.5 + 0.5 N'.xy) x luminance(L)
  // + ICNC + ICMC + AO x BCLR x (1 - metal) x DIFC x L / pi (c1[0].w) + the lights' GGX specular (unmasked, no reflection of the
  // surroundings). L = ambient + the lights' N.L (not times AO). Alpha = BCLR.a^2 x DIFC.a, as kind 19.
  if (mapStage[4] != -1 && mapStage[5] != -1) {
    liquid += fmt::format(R"""(
    if (pbr_kind > 24.5 && pbr_kind < 25.5) {{
        let pbr_gl = pbr_ambd + pbr_lnl;
        let pbr_gs = max(textureSampleLevel(tex{0}, tex{0}_samp, 0.5 + 0.5 * pbr_n.xy, 0.0).rgb, vec3f(0.0));
        let pbr_gv = max(sampled{1}.rgb, vec3f(0.0));
        pbr_lo = pbr_gv * pbr_gs * dot(pbr_gl, vec3f(0.2126, 0.7152, 0.0722)) +
                 pbr_base * (1.0 - pbr_metal) * pbr_ao * pbr_gl * ubuf.pbr_shield[7].rgb * 0.31830988 + pbr_lspec;
        pbr_glow = ubuf.pbr_shield[6].rgb;
        pbr_alpha = clamp({2}.a * {2}.a * ubuf.pbr_shield[7].w * ubuf.pbr_shield[6].w, 0.0, 1.0);
        pbr_pass = vec3f(0.0);
    }})""",
                        underlying(config.tevStages[mapStage[4]].texMapId), mapStage[5], base);
  }
  // Kind 28, Remastered's 088e025e (Model_IceSpreader, permutation 002_0). Shield rows: 0 = CCH0 (z normal strength,
  // w frost parallax), 1 = CCH1 (x frost gain, y rim gain, z rim power), 2 = CCH2 rgb (cube tint) and CCH5.x in w
  // (cube gain), 3 = CCH3 rgb (rim colour). Map 3 is the frost TCH0. L is the baked probe's mean (BLPD) times BLCM:
  //   rgb = AO x BCLR x (1 - metal) x L / pi
  //       + occ x cube x CCH5.x x CCH2 x (F0 x ab.x + ab.y)   occ = AO x mix(min, 1, sat(max(mean) x luma(BLCM) x 1/max))
  //       + TCH0(uv + BCLR.a x CCH0.w x (T.V, B.V) / (N0.V)) x BCLR.a^2 x vertex alpha x CCH1.x
  //       + (1 - sat(N'.V))^CCH1.z x CCH1.y x CCH3 x (1 / exposure)
  // with B = normalize(cross(N0, T)) and no handedness, as the parallax is written there. Alpha = BCLR.a (not squared).
  if (mapStage[2] != -1 && mapStage[3] != -1) {
    const auto& frost = config.tevStages[mapStage[3]];
    liquid += fmt::format(R"""(
    if (pbr_kind > 27.5 && pbr_kind < 28.5) {{
        let pbr_ic = ubuf.pbr_shield[1];
        let pbr_it = {2};
        let pbr_ib = normalize(cross(pbr_ngs, pbr_it));
        let pbr_iv = in.pbr_pos;
        let pbr_ip = {1}.a * ubuf.pbr_shield[0].w / dot(pbr_ngs, pbr_iv);
        let pbr_iuv = tex{3}_uv + pbr_ip * vec2f(dot(pbr_it, pbr_iv), dot(pbr_ib, pbr_iv));
        let pbr_ifr = textureSampleBias(tex{0}, tex{0}_samp, pbr_iuv, ubuf.tex{0}_size_bias.z).rgb;
        var pbr_il = pbr_amb;
        if (ubuf.pbr_ambient[0].w > 0.0) {{
            pbr_il = select(dot(pbr_amb, vec3f(0.2126, 0.7152, 0.0722)), 1.0, ubuf.pbr_ambient[0].w > 1.5) *
                     max(ubuf.pbr_ambient[0].rgb + ubuf.pbr_ambient[1].rgb / (ubuf.pbr_ambient[2].rgb + 1.0), vec3f(0.0));
        }}
        let pbr_iocc = pbr_ao * mix(ubuf.pbr_probe[1].w, 1.0,
                                    clamp(max(pbr_il.r, max(pbr_il.g, pbr_il.b)) * dot(pbr_blcm, vec3f(0.2126, 0.7152, 0.0722)) *
                                          ubuf.pbr_probe[2].w, 0.0, 1.0));
        let pbr_if0 = mix(vec3f(0.04), pbr_base, pbr_metal);
        let pbr_ix = select(1.0, 1.0 / ubuf.pbr_tone[0].w, ubuf.pbr_tone[1].x > 0.0 && ubuf.pbr_tone[0].w > 0.0);
        pbr_lo = pbr_ao * pbr_base * (1.0 - pbr_metal) * pbr_il * pbr_blcm * 0.31830988 +
                 pbr_iocc * pbr_cubel * (ubuf.pbr_shield[2].w * ubuf.pbr_shield[2].rgb) * (pbr_if0 * pbr_ab.x + pbr_ab.y);
        pbr_glow = pbr_ifr * ({1}.a * {1}.a * pbr_vraw.a * pbr_ic.x) +
                   pbr_ix * pow(1.0 - clamp(dot(pbr_n, pbr_v), 0.0, 1.0), pbr_ic.z) * pbr_ic.y * ubuf.pbr_shield[3].rgb;
        pbr_alpha = clamp({1}.a, 0.0, 1.0);
        pbr_pass = vec3f(0.0);
    }})""",
                        underlying(frost.texMapId), base,
                        tangents ? "normalize(in.pbr_tan.xyz)"s : "normalize(pbr_t0)"s, underlying(frost.texCoordId));
  }
  // Kind 31, Remastered's fb2bc671 (static) and df3e3423 (skinned, ICAN emissive), the ChozoGhost's X-ray material (every
  // fragment permutation shares this core; their ambient is zeroed by c1[0].z). With fade = mix(CCH0.z, CCH0.w, sat((-z -
  // CCH1.x) / (CCH1.y - CCH1.x))) by view depth and fr = 1 - max(0, N'.z)^CCH0.x (N' the normal map's, in view space):
  // alpha = (CCH1.z != 0 ? fade x fr : fade) x ICNC.a. rim = fr + BCLR.a; fb2bc671: rim unless CCH1.w != 0 (then 0);
  // df3e3423: CCH1.w != 0 ? rim x ICAN x CCH0.y : ICAN x CCH0.y + rim. rgb = max(0, (that + ICMC) x ICNC) x exposure
  // + the lights' GGX specular (roughness 0.1, F = 1, no diffuse: pbr_lspec / pi). shield rows: 0 CCH0, 1 CCH1, 2 ICNC, 3 ICMC
  // (w = 1 for df3e3423).
  // Kind 33, 65e90e82: the same with DIFC for ICNC (row 2), ICAN always, TCH0 (map 1) in rgb - 2 x TCH0 and, with CCH1.z != 0,
  // alpha = fade x fr + mean(TCH0) (then x DIFC.a); fade alone otherwise.
  if (config.pbrKind == 31 || config.pbrKind == 33) {
    const bool map65 = config.pbrKind == 33;
    liquid += fmt::format(R"""(
    if (pbr_kind > 30.5) {{
        let pbr_c0 = ubuf.pbr_shield[0];
        let pbr_c1 = ubuf.pbr_shield[1];
        let pbr_fz = clamp((-in.pbr_pos.z - pbr_c1.x) / (pbr_c1.y - pbr_c1.x), 0.0, 1.0);
        let pbr_fade = mix(pbr_c0.z, pbr_c0.w, pbr_fz);
        let pbr_fr = 1.0 - pow(max(pbr_n.z, 0.0), pbr_c0.x);
        let pbr_rim = pbr_fr + {0}.a;
        let pbr_ican = max({1}.rgb, vec3f(0.0)) * pbr_c0.y;
        let pbr_sw = pbr_c1.w != 0.0;
        let pbr_val = select(select(vec3f(pbr_rim), vec3f(0.0), pbr_sw),
                             select(pbr_ican + vec3f(pbr_rim), pbr_ican * pbr_rim, pbr_sw),
                             ubuf.pbr_shield[3].w > 0.5);
        pbr_lo = pbr_lspec / pbr_pi;
        pbr_glow = max((pbr_val + ubuf.pbr_shield[3].rgb) * ubuf.pbr_shield[2].rgb{2}, vec3f(0.0));
        pbr_alpha = clamp(select(pbr_fade, {3}, pbr_c1.z != 0.0) * ubuf.pbr_shield[2].w, 0.0, 1.0);
        pbr_pass = vec3f(0.0);
    }})""", base, mapStage[3] == -1 ? "vec4f(0.0)"s : sampled(3, "vec4f(0.0)"),
                           map65 ? " - 2.0 * " + sampled(1, "vec4f(0.0)") + ".rgb" : ""s,
                           map65 ? "pbr_fade * pbr_fr + (" + sampled(1, "vec4f(0.0)") + ".r + " + sampled(1, "vec4f(0.0)") + ".g + " + sampled(1, "vec4f(0.0)") + ".b) * 0.333333343" : "pbr_fade * pbr_fr"s);
  }
  std::string attn;
  if (cc.attnFn == GX_AF_SPOT) {
    attn = R"""(
          let cosine = max(0.0, dot(ldir, light.dir));
          let cos_attn = dot(light.cos_att, vec3f(1.0, cosine, cosine * cosine));
          let dist_attn = dot(light.dist_att, vec3f(1.0, dist, dist2));
          let attn = max(0.0, cos_attn / dist_attn);)""";
  } else {
    attn = "\n          let attn = 1.0;";
  }
  const std::string amb = lit ? (cc.ambSrc == GX_SRC_REG ? "ubuf.cc0_amb.rgb"s : "vec3f(0.2)"s)
                              : (cc.matSrc == GX_SRC_REG ? "ubuf.cc0_mat.rgb"s : "vec3f(1.0)"s);
  // Kind 27, Remastered's 17e458cd (a decal with an alpha map): g = clamp(2 va (1 + TCH0.x) - 1), c = CCH0.x
  // (pbr_layer.x), t = clamp((g - 0.5 + c) / 2c), alpha = t²(3 - 2t). BCLR.a is not used; no discard.
  std::string decalAlpha;
  if (config.pbrKind == 27 && mapStage[3] != -1) {
    decalAlpha = fmt::format(R"""(
      if (pbr_kind > 26.5 && pbr_kind < 27.5) {{
          let pbr_dg = clamp(2.0 * pbr_vraw.a * (1.0 + sampled{}.x) - 1.0, 0.0, 1.0);
          let pbr_dt = clamp((pbr_dg - 0.5 + ubuf.pbr_layer.x) / (2.0 * ubuf.pbr_layer.x), 0.0, 1.0);
          pbr_alpha = pbr_dt * pbr_dt * (3.0 - 2.0 * pbr_dt);
      }})""",
                             mapStage[3]);
  }
  if (inventory) {
    layer += fmt::format(R"""(
      let inv_ba = {0}.a;
      let inv_da = ubuf.pbr_model_diffuse.a;
{1}
      let inv_b = max({0}.rgb, vec3f(0.0)) * ubuf.pbr_model_diffuse.rgb;
      let inv_a = {2};)""",
                         base, config.pbrKind == 36 ? "\n      let inv_c = ubuf.pbr_model_coverage.x;" : "", InvAlphaExpr);
  }
  // In pieces: MSVC rejects one string literal over 16 KB (adjacent ones join).
  std::string source = fmt::format(R"""(
    // PBR (GX_AURORA_SET_PBR)
    {{
      let pbr_pi = 3.14159265;{10}
      var pbr_base = {11};{13}
      let pbr_orm = {1}.rgb;
      let pbr_ao = select(pbr_orm.r, 1.0, pbr_ind);
      let pbr_rough = select(select(clamp(select(pbr_orm.g, 0.6, pbr_ind), 0.02, 1.0), 0.1, pbr_kind > 30.5 && pbr_kind < 33.5),
                             max(pbr_orm.g - pbr_knoise, 0.02), pbr_kind > 31.5 && pbr_kind < 32.5);
      let pbr_metal = clamp(select(pbr_orm.b, 0.0, pbr_ind), 0.0, 1.0);
      let pbr_emissive = max({2}.rgb, vec3f(0.0)) * ubuf.pbr_emissive.rgb;
      var pbr_n = pbr_ng;{3}
      let pbr_v = normalize(-in.pbr_pos);
      let pbr_nv = max(dot(pbr_n, pbr_v), 1e-4);
      let pbr_f0 = select(mix(vec3f(0.04), pbr_base * select(vec3f(1.0), pbr_vc.rgb, pbr_f0t), pbr_metal) *
          ubuf.pbr_light_scale.y, vec3f(1.0), pbr_kind > 30.5 && pbr_kind < 33.5);
      let pbr_diff = select(pbr_base * (1.0 - pbr_metal){8} * ubuf.pbr_light_scale.x *
          select(1.0, clamp(pbr_vraw.a * ubuf.pbr_layer_height.y, 0.0, 1.0), pbr_kind > 12.5 && pbr_kind < 13.5),
          vec3f(0.0), pbr_kind > 30.5 && pbr_kind < 33.5);
      let pbr_a2 = pow(pbr_rough, 4.0);
      let pbr_k = pbr_rough * pbr_rough * 0.5;
      // A normal-mapped reflection can point into the surface; lift it back to the horizon
      // of the geometric normal.
      let pbr_rn = reflect(-pbr_v, pbr_n);
      let pbr_refl = pbr_rn + pbr_ng * clamp(-dot(pbr_ng, pbr_rn), 0.0, 1.0);
      var pbr_lo = vec3f(0.0);
      var pbr_env = vec3f(0.0);
      var pbr_lsum = vec3f(0.0);
      // The lights' diffuse alone: a lightmapped surface takes it times the baked-light modulation.
      var pbr_ldiff = vec3f(0.0);
      var pbr_l0v = vec3f(0.0);
      var pbr_lnl = vec3f(0.0);
      // The lights' specular alone (kind 25 adds it unmasked to the matcap).
      var pbr_lspec = vec3f(0.0);
      // pbr-sun-vis
      // pbr-lights-begin
      for (var i = 0u; i < {4}u; i++) {{
          if (({15} & ~u32(ubuf.pbr_light_skip.x) & (1u << i)) == 0u) {{ continue; }}
          let light = ubuf.lights[i];
          // A Remastered HDR light (GXSetPBRLightHdr) brings its own colour, position and
          // falloff.
          let hdr_c = ubuf.pbr_light_hdr[i * 3u];
          let hdr_p = ubuf.pbr_light_hdr[i * 3u + 1u];
          let hdr_on = hdr_c.w > 0.5;
          var ldir = select(light.pos, hdr_p.xyz, hdr_on) - in.pbr_pos;
          let dist2 = dot(ldir, ldir);
          let dist = sqrt(dist2);
          ldir = ldir / max(dist, 1e-4);{5}
          let nl = max(dot(pbr_n, ldir), 0.0);
          let h = normalize(ldir + pbr_v);
          let nh = max(dot(pbr_n, h), 0.0);
          let vh = max(dot(pbr_v, h), 0.0);
          let dd = nh * nh * (pbr_a2 - 1.0) + 1.0;
          let d = pbr_a2 / max(pbr_pi * dd * dd, 1e-6 * pbr_a2);
          let vis = 0.25 / (max(pbr_nv * (1.0 - pbr_k) + pbr_k, 1e-4) * max(nl * (1.0 - pbr_k) + pbr_k, 1e-4));
          let f = pbr_f0 + (1.0 - pbr_f0) * pow(1.0 - vh, 5.0);
          let spec = d * vis * f;
          // GX lights are unnormalised (colour * N.L is full brightness), so Lambert has no
          // 1/pi and the specular lobe is scaled by pi to match.
          var rad = ubuf.pbr_light_color[i].rgb * attn{16};
          if (hdr_on) {{
              let hdr_r1 = ubuf.pbr_light_hdr[i * 3u + 2u].x;
              let hdr_t = clamp((dist - hdr_p.w) / max(hdr_r1 - hdr_p.w, 1e-4), 0.0, 1.0);
              var hdr_fa = 1.0;
              if (hdr_c.w > 3.5) {{
                  hdr_fa = 1.0 - smoothstep(0.0, 1.0, hdr_t);
              }} else if (hdr_c.w > 2.5) {{
                  hdr_fa = (1.0 - hdr_t) * (1.0 - hdr_t);
              }} else if (hdr_c.w > 1.5) {{
                  hdr_fa = 1.0 - hdr_t;
              }}
              rad = hdr_c.rgb * hdr_fa{16};
          }}
          // pbr-sun-light
          pbr_lo += (pbr_diff * pbr_ao + select(spec * pbr_pi, vec3f(0.0), pbr_nols)) * rad * nl;
          pbr_ldiff += pbr_diff * pbr_ao * rad * nl;
          pbr_lspec += select(spec * pbr_pi, vec3f(0.0), pbr_nols) * rad * nl;
          // Stand-in environment: the surroundings as a soft hemisphere lit by this light,
          // seen along the reflection vector.
          let env_w = 0.5 + 0.5 * dot(pbr_refl, ldir);
          pbr_env += rad * (env_w * env_w);
          pbr_lsum += rad;
          pbr_lnl += rad * nl;
      }}
      // pbr-lights-end
      // The room's Remastered point and spot lights (GX_AURORA_PORT_ROOM_LIGHTS), on every surface,
      // lightmapped or not: Lambert times a distance falloff and, for a spot, its cone.
      for (var ri = 0u; ri < u32(ubuf.pbr_room_lights.y); ri++) {{
          let rb = bitcast<u32>(ubuf.pbr_room_lights.x) + ri * 16u;
          let r0 = bitcast<vec4f>(vec4u(abuf[rb], abuf[rb + 1u], abuf[rb + 2u], abuf[rb + 3u]));
          let r1 = bitcast<vec4f>(vec4u(abuf[rb + 4u], abuf[rb + 5u], abuf[rb + 6u], abuf[rb + 7u]));
          let r2 = bitcast<vec4f>(vec4u(abuf[rb + 8u], abuf[rb + 9u], abuf[rb + 10u], abuf[rb + 11u]));
          let r3 = bitcast<vec4f>(vec4u(abuf[rb + 12u], abuf[rb + 13u], abuf[rb + 14u], abuf[rb + 15u]));
          var ldir = r0.xyz - in.pbr_pos;
          let dist = length(ldir);
          ldir = ldir / max(dist, 1e-4);
          let rt = clamp(dist * r0.w + r1.w, 0.0, 1.0);
          var fa = 1.0;
          if (r3.y > 2.5) {{
              fa = 1.0 - rt * rt * (3.0 - 2.0 * rt);
          }} else if (r3.y > 1.5) {{
              fa = (1.0 - rt) * (1.0 - rt);
          }} else if (r3.y > 0.5) {{
              fa = 1.0 - rt;
          }}
          if (r3.z > 0.5) {{
              fa *= clamp(dot(ldir, r2.xyz) * r2.w + r3.x, 0.0, 1.0);
          }}
          if (fa <= 0.0) {{ continue; }}
          let rad = r1.rgb * fa;
          let nl = max(dot(pbr_n, ldir), 0.0);
          let h = normalize(ldir + pbr_v);
          let nh = max(dot(pbr_n, h), 0.0);
          let vh = max(dot(pbr_v, h), 0.0);
          let dd = nh * nh * (pbr_a2 - 1.0) + 1.0;
          let d = pbr_a2 / max(pbr_pi * dd * dd, 1e-6 * pbr_a2);
          let vis = 0.25 / (max(pbr_nv * (1.0 - pbr_k) + pbr_k, 1e-4) * max(nl * (1.0 - pbr_k) + pbr_k, 1e-4));
          let f = pbr_f0 + (1.0 - pbr_f0) * pow(1.0 - vh, 5.0);
          pbr_lo += (pbr_diff * pbr_ao + select(d * vis * f * pbr_pi, vec3f(0.0), pbr_nols)) * rad * nl;
          pbr_ldiff += pbr_diff * pbr_ao * rad * nl;
          pbr_lspec += select(d * vis * f * pbr_pi, vec3f(0.0), pbr_nols) * rad * nl;
          let env_w = 0.5 + 0.5 * dot(pbr_refl, ldir);
          pbr_env += rad * (env_w * env_w);
          pbr_lsum += rad;
          pbr_lnl += rad * nl;
      }}
      // pbr-sun
      // Ambient: diffuse plus the split-sum environment BRDF (Karis' analytic fit) applied
      // to the reflection probe, a cube map whose mips are picked by roughness. The
      // probe's weight is 0 until the game has filled it; the ambient and the stand-in
      // environment above take its place then. Both occluded.
      let pbr_c0 = vec4f(-1.0, -0.0275, -0.572, 0.022);
      let pbr_c1 = vec4f(1.0, 0.0425, 1.04, -0.04);
      let pbr_r = pbr_rough * pbr_c0 + pbr_c1;
      let pbr_a004 = min(pbr_r.x * pbr_r.x, exp2(-9.28 * pbr_nv)) * pbr_r.x + pbr_r.y;
      var pbr_ab = vec2f(-1.04, 1.04) * pbr_a004 + pbr_r.zw;
      // A mod's table (GX_AURORA_SET_PBR_BRDF_LUT) replaces the fit.
      if (ubuf.pbr_light_scale.z > 0.0) {{
          pbr_ab = textureSampleLevel(pbr_brdf_lut, pbr_cube_samp, vec2f(saturate(pbr_nv), pbr_rough), 0.0).rg;
      }}
      let pbr_amb = pow(max({6}, vec3f(0.0)), vec3f(2.2));
      let pbr_pd = ubuf.pbr_probe[0].xyz * pbr_refl.x + ubuf.pbr_probe[1].xyz * pbr_refl.y +
                   ubuf.pbr_probe[2].xyz * pbr_refl.z;
      // A room cube (GX_AURORA_SET_PBR_CUBE) is linear HDR: x is its exposure, y the mip
      // a roughness of 1 samples. The probe is the display-referred scene, x is 0 then.
      let pbr_hdr = ubuf.pbr_cube.x;
      let pbr_lod = select({7}.0, ubuf.pbr_cube.y, pbr_hdr > 0.0);
      // The LOD takes the roughness unfloored, as Remastered's does (only the BRDF floors it).
      let pbr_cubed = textureSampleLevel(pbr_cube, pbr_cube_samp, pbr_pd, saturate(pbr_orm.g) * pbr_lod).rgb;
      let pbr_cubel = select(srgb_dec(pbr_cubed), pbr_cubed * pbr_hdr, pbr_hdr > 0.0);
      var pbr_envspec = mix(pbr_amb + pbr_env * 0.35, pbr_cubel, min(ubuf.pbr_probe[0].w, 1.0));
      // The room cube also shapes the ambient: its blurriest useful mip (z) along the
      // normal says how much of the room's light comes from that side, and w scales that
      // to 1 for the cube's average. The game's ambient keeps the level and the colour.
      var pbr_ambd = pbr_amb;
      // The baked mean without the modulation (the BLPD record's mean, or the grid's mean
      // texture): the glass kinds (30) light with this alone, no lobes.
      var pbr_bmean = vec3f(0.0);
      var pbr_bmeanok = false;
      if (pbr_hdr > 0.0 && ubuf.pbr_cube.w > 0.0) {{
          let pbr_nd = ubuf.pbr_probe[0].xyz * pbr_n.x + ubuf.pbr_probe[1].xyz * pbr_n.y +
                       ubuf.pbr_probe[2].xyz * pbr_n.z;
          let pbr_irr = textureSampleLevel(pbr_cube, pbr_cube_samp, pbr_nd, ubuf.pbr_cube.z).rgb;
          pbr_ambd = pbr_amb * clamp(dot(pbr_irr, vec3f(0.2126, 0.7152, 0.0722)) * pbr_hdr * ubuf.pbr_cube.w, 0.35, 2.5);
      }}
      // Remastered's baked-lighting modulation (GX_AURORA_SET_PBR_BAKED_LIGHT_MODULATION, white
      // but in a power bomb's flash) multiplies the baked ambient below.
      let pbr_blcm = ubuf.pbr_light_skip.yzw;
      // Baked ambient (GX_AURORA_SET_PBR_AMBIENT) replaces all of that: a lobe per colour
      // channel around the direction most of that channel's light comes from.
      if (ubuf.pbr_ambient[0].w > 0.0) {{
          let pbr_aq = clamp(vec3f(dot(pbr_n, ubuf.pbr_ambient[3].xyz), dot(pbr_n, ubuf.pbr_ambient[4].xyz),
                                   dot(pbr_n, ubuf.pbr_ambient[5].xyz)) * 0.5 + 0.5, vec3f(0.0), vec3f(1.0));
          // w is 1 when the game's ambient sets the level, 2 when the baked light is the level.
          pbr_ambd = select(dot(pbr_amb, vec3f(0.2126, 0.7152, 0.0722)), 1.0, ubuf.pbr_ambient[0].w > 1.5) * max(ubuf.pbr_ambient[0].rgb + ubuf.pbr_ambient[1].rgb * pow(pbr_aq, ubuf.pbr_ambient[2].rgb),
                         vec3f(0.0)) * pbr_blcm;
          pbr_bmean = select(dot(pbr_amb, vec3f(0.2126, 0.7152, 0.0722)), 1.0, ubuf.pbr_ambient[0].w > 1.5) *
                      max(ubuf.pbr_ambient[0].rgb + ubuf.pbr_ambient[1].rgb / (ubuf.pbr_ambient[2].rgb + 1.0), vec3f(0.0));
          pbr_bmeanok = true;
      }}
      // An ambient volume (GX_AURORA_SET_PBR_VOLUME) is the same lobes, read at this pixel
      // from the room's grid, a little off the surface so that a wall is lit by the air in
      // front of it.
      var pbr_vdiag = vec3f(0.0);
      if (ubuf.pbr_volume[3].w > 0.0) {{
          let pbr_vp = vec4f(in.pbr_pos + pbr_n * ubuf.pbr_volume[4].w, 1.0);
          let pbr_vuv0 = vec3f(dot(ubuf.pbr_volume[0], pbr_vp), dot(ubuf.pbr_volume[1], pbr_vp),
                               dot(ubuf.pbr_volume[2], pbr_vp));
          // Remastered samples with a black border (CLAMP_TO_BORDER), so outside the grid it
          // reads 0. The volume carries one zero texel around it; vuv is mapped into that.
          let pbr_vsize = vec3f(textureDimensions(pbr_vol_mean)) - 2.0;
          let pbr_vuv = (pbr_vuv0 * pbr_vsize + 1.0) / (pbr_vsize + 2.0);
          let pbr_vmean = textureSampleLevel(pbr_vol_mean, pbr_cube_samp, pbr_vuv, 0.0).rgb;
          let pbr_vlobe = textureSampleLevel(pbr_vol_lobe, pbr_cube_samp, pbr_vuv, 0.0).rgb;
          let pbr_vr = textureSampleLevel(pbr_vol_r, pbr_cube_samp, pbr_vuv, 0.0);
          let pbr_vg = textureSampleLevel(pbr_vol_g, pbr_cube_samp, pbr_vuv, 0.0);
          let pbr_vb = textureSampleLevel(pbr_vol_b, pbr_cube_samp, pbr_vuv, 0.0);
)"""
R"""(          let pbr_vn = vec3f(dot(ubuf.pbr_volume[3].xyz, pbr_n), dot(ubuf.pbr_volume[4].xyz, pbr_n),
                             dot(ubuf.pbr_volume[5].xyz, pbr_n));
          let pbr_vq = clamp(vec3f(dot(pbr_vn, pbr_vr.xyz * 2.0 - 1.0), dot(pbr_vn, pbr_vg.xyz * 2.0 - 1.0),
                                   dot(pbr_vn, pbr_vb.xyz * 2.0 - 1.0)) * 0.5 + 0.5, vec3f(0.0), vec3f(1.0));
          let pbr_vs = vec3f(pbr_vr.a, pbr_vg.a, pbr_vb.a);
          pbr_ambd = max(pbr_vmean - pbr_vlobe + 2.0 * pbr_vlobe * (1.0 + pbr_vs) * pow(pbr_vq, 1.0 + 2.0 * pbr_vs),
                         vec3f(0.0)) * ubuf.pbr_volume[3].w * pbr_blcm;
          // The same lobes along the reflection stand in for the environment: unlike a cube
          // for the room, they are dark where this spot is.
          let pbr_vrn = vec3f(dot(ubuf.pbr_volume[3].xyz, pbr_refl), dot(ubuf.pbr_volume[4].xyz, pbr_refl),
                              dot(ubuf.pbr_volume[5].xyz, pbr_refl));
          let pbr_vrq = clamp(vec3f(dot(pbr_vrn, pbr_vr.xyz * 2.0 - 1.0), dot(pbr_vrn, pbr_vg.xyz * 2.0 - 1.0),
                                    dot(pbr_vrn, pbr_vb.xyz * 2.0 - 1.0)) * 0.5 + 0.5, vec3f(0.0), vec3f(1.0));
          pbr_bmean = pbr_vmean * ubuf.pbr_volume[3].w;
          pbr_bmeanok = true;
          pbr_envspec = max(pbr_vmean - pbr_vlobe + 2.0 * pbr_vlobe * (1.0 + pbr_vs) * pow(pbr_vrq, 1.0 + 2.0 * pbr_vs),
                            vec3f(0.0)) * ubuf.pbr_volume[3].w;
          // With a room cube and Remastered's reflection occlusion (w of probe rows 1 and 2,
          // GXSetPBRProbeEx), the cube is reflected instead, darkened where the baked light
          // is weak: mix(min, 1, saturate(brightest channel of the mean x the modulation's
          // luminance / max)).
          if (pbr_hdr > 0.0 && ubuf.pbr_probe[0].w > 0.0 && ubuf.pbr_probe[2].w > 0.0) {{
              let pbr_vocc = clamp(max(pbr_vmean.r, max(pbr_vmean.g, pbr_vmean.b)) *
                                   dot(pbr_blcm, vec3f(0.2126, 0.7152, 0.0722)) * ubuf.pbr_probe[2].w, 0.0, 1.0);
              pbr_envspec = pbr_cubel * mix(ubuf.pbr_probe[1].w, 1.0, pbr_vocc);
          }}
          // Diagnostics (w of row 5): 1 the texture coordinates, 2 the light alone, 3 the
          // normal.
          pbr_vdiag = select(pbr_ambd, pbr_vuv0, ubuf.pbr_volume[5].w < 1.5);
          if (ubuf.pbr_volume[5].w > 2.5) {{
              pbr_vdiag = pbr_n * 0.5 + 0.5;
          }}
      }}
      // The same modulation on the lights where the baked ambient is a lobe or a grid volume
      // (not a lightmap, which does it below): BLCM x (ambient + lights), mode 4096.
      if (pbr_blit && ubuf.pbr_lmap_rect.w == 0.0 && (ubuf.pbr_ambient[0].w > 0.0 || ubuf.pbr_volume[3].w > 0.0)) {{
          pbr_lo += pbr_ldiff * (pbr_blcm - 1.0);
      }}
      // CharacterBacklight (ae819893) without probe data (no baked ambient, no volume) is
      // Remastered's perm 078: flat ambient, no environment reflection, white backlights.
      let pbr_bkl = ubuf.pbr_backlight.xyz;
      let pbr_bnoprobe = pbr_bkl.z > 0.5 && ubuf.pbr_ambient[0].w <= 0.0 && ubuf.pbr_volume[3].w <= 0.0;
      // pbr-lightmap
      if (pbr_flat) {{
          pbr_ambd = pbr_amb;
      }}
      // Kind 13 scales the probe's reflection by pbr_layer_height.z (CCH1.z); mode 256 has none.
      pbr_lo += (pbr_ambd * pbr_diff + pbr_envspec * select(select(1.0, ubuf.pbr_layer_height.z, pbr_kind > 12.5 && pbr_kind < 13.5), 0.0, pbr_noenv || pbr_bnoprobe) *
                                           (pbr_f0 * pbr_ab.x + pbr_ab.y)) * pbr_ao;
      // Remastered's CharacterBacklight (GX_AURORA_SET_PBR_BACKLIGHT; the material's
      // strengths and falloff in the backlight's place): a light from world up and one from
      // behind, each coloured like the baked ambient on its side brought up to a luminance of
      // 1, so that it does not go dark with the room. Both fade towards the bottom of the
      // model's bounds; ambient occlusion counts twice, as it does there.
      if (!pbr_cu && !pbr_sky && pbr_bkl.z > 0.5 && ubuf.pbr_bklight[2].x + ubuf.pbr_bklight[1].w > 0.0) {{
          let pbr_bt = select(clamp(dot(ubuf.pbr_bklight[0], vec4f(in.pbr_pos, 1.0)), 0.0, 1.0), in.pbr_bty, in.pbr_bty >= 0.0);
          let pbr_bf = select(pow(pbr_bt, pbr_bkl.z - 1.0), 1.0, pbr_bkl.z < 1.5);
          var pbr_btc = pbr_amb;
          var pbr_bbc = pbr_amb;
          if (ubuf.pbr_ambient[0].w > 0.0) {{
              let pbr_blv = select(dot(pbr_amb, vec3f(0.2126, 0.7152, 0.0722)), 1.0, ubuf.pbr_ambient[0].w > 1.5);
              let pbr_bdir = ubuf.pbr_bklight[1].xyz;
              let pbr_btq = clamp(vec3f(ubuf.pbr_ambient[3].y, ubuf.pbr_ambient[4].y, ubuf.pbr_ambient[5].y) * 0.5 + 0.5,
                                  vec3f(0.0), vec3f(1.0));
              let pbr_bbq = clamp(vec3f(dot(pbr_bdir, ubuf.pbr_ambient[3].xyz), dot(pbr_bdir, ubuf.pbr_ambient[4].xyz),
                                        dot(pbr_bdir, ubuf.pbr_ambient[5].xyz)) * 0.5 + 0.5, vec3f(0.0), vec3f(1.0));
              pbr_btc = pbr_blv * max(ubuf.pbr_ambient[0].rgb + ubuf.pbr_ambient[1].rgb * pow(pbr_btq, ubuf.pbr_ambient[2].rgb),
                                      vec3f(0.0)) * pbr_blcm;
              pbr_bbc = pbr_blv * max(ubuf.pbr_ambient[0].rgb + ubuf.pbr_ambient[1].rgb * pow(pbr_bbq, ubuf.pbr_ambient[2].rgb),
                                      vec3f(0.0)) * pbr_blcm;
          }}
          let pbr_btl = dot(pbr_btc, vec3f(0.2126, 0.7152, 0.0722));
          let pbr_bbl = dot(pbr_bbc, vec3f(0.2126, 0.7152, 0.0722));
          pbr_btc = select(vec3f(1.0), pbr_btc * max(1.0 / pbr_btl, 1.0), pbr_btl > 0.05 && !pbr_bnoprobe);
          pbr_bbc = select(vec3f(1.0), pbr_bbc * max(1.0 / pbr_bbl, 1.0), pbr_bbl > 0.05 && !pbr_bnoprobe);
          let pbr_bla = normalize(ubuf.pbr_up.xyz);
          let pbr_blb = vec3f(0.57735, 0.57735, -0.57735);
          let pbr_bnla = clamp(dot(pbr_n, pbr_bla), 0.0, 1.0);
          let pbr_bnlb = clamp(dot(pbr_n, pbr_blb), 0.0, 1.0);
          let pbr_bra = pbr_btc * (pbr_bnla * pbr_ao * pbr_bf * max(pbr_bkl.y, 0.0) * ubuf.pbr_bklight[2].x);
          let pbr_brb = pbr_bbc * (pbr_bnlb * pbr_ao * pbr_bf * max(pbr_bkl.x, 0.0) * ubuf.pbr_bklight[1].w);
          // GGX with each light's own remap of the roughness. The back light's half vector and
          // fresnel are fixed: Remastered takes the view along -z there.
          let pbr_bqa = pbr_rough * 0.88 + 0.12;
          let pbr_bqb = pbr_rough * 0.3 + 0.7;
          let pbr_ba2 = vec2f(pow(pbr_bqa, 4.0), pow(pbr_bqb, 4.0));
          let pbr_bk = vec2f(pbr_bqa * pbr_bqa, pbr_bqb * pbr_bqb) * 0.5;
          let pbr_bha = (pbr_bla + pbr_v) / max(length(pbr_bla + pbr_v), 1e-6);
          let pbr_bnh = vec2f(clamp(dot(pbr_n, pbr_bha), 0.0, 1.0),
                              clamp(dot(pbr_n, vec3f(0.627963, 0.627963, 0.4597009)), 0.0, 1.0));
          let pbr_bdt = pbr_bnh * pbr_bnh * (pbr_ba2 - 1.0) + 1.0;
          let pbr_bd = pbr_ba2 / (pbr_pi * pbr_bdt * pbr_bdt);
          let pbr_bvis = 1.0 / max((pbr_nv * (1.0 - pbr_bk) + pbr_bk) *
                                   (vec2f(pbr_bnla, pbr_bnlb) * (1.0 - pbr_bk) + pbr_bk), vec2f(1e-6));
          let pbr_bfa = pbr_f0 + (1.0 - pbr_f0) * pow(1.0 - clamp(dot(pbr_bha, pbr_bla), 0.0, 1.0), 5.0);
          let pbr_bfb = pbr_f0 * 0.9539562 + 0.0460438505;
          pbr_lo += pbr_ao * pbr_diff * (pbr_bra + pbr_brb) / pbr_pi +
                    (pbr_bfa * pbr_bra * (pbr_bd.x * pbr_bvis.x) + pbr_bfb * pbr_brb * (pbr_bd.y * pbr_bvis.y)) * 0.25;
      }}
      // The material's alpha and shading modes (w of the GX_AURORA_SET_PBR_MATERIAL rows).
      // Kind 3 (lava, embers): the vertex alpha is how much of the glow shows.
      var pbr_glow = pbr_emissive * select(1.0, pbr_vraw.a, pbr_kind > 2.5 && pbr_kind < 3.5) + pbr_kglow;
      var pbr_alpha = {12}{9};
      var pbr_pass = vec3f(0.0);
      if (ubuf.pbr_emissive.w > 0.0) {{
          // A height blend (snow and ice laid over rock): the base map's alpha lifts the
          // vertex alpha, and a smoothstep as wide as the threshold cuts the edge. The
          // alpha is squared (ca10c453: fma(fma(a*a, vw, vw), 2, -1)).
          let pbr_hx = clamp(({0}.a * {0}.a + 1.0){9} * 2.0 - 1.0, 0.0, 1.0);
          let pbr_hs = clamp((pbr_hx - 0.5 + ubuf.pbr_emissive.w) / (2.0 * ubuf.pbr_emissive.w), 0.0, 1.0);
          pbr_alpha = pbr_hs * pbr_hs * (3.0 - 2.0 * pbr_hs);
      }}{17}
      if (pbr_raw) {{
          pbr_alpha = {12};
      }}
      if (pbr_cu) {{
          // ColorUnlit's alpha: the base map's times the vertex's.
          pbr_alpha = {0}.a * pbr_vraw.a;
      }}
      if (pbr_cut) {{
          pbr_alpha = 1.0;
      }}
      // 1 = unlit, 2 = the base map's alpha masks the glow, 4 = tinted by the vertex colour
      // (pbr_vc), 8 = ColorUnlit, 16 = sky (above); the sum of those.
      let pbr_mode = pbr_flags - select(0.0, 4.0, pbr_flags > 3.5);
      if (pbr_mode > 1.5) {{
          // The base map's alpha is how much of the glow shows, and no opacity: the
          // vertex alpha alone is.
          pbr_glow = pbr_emissive * {0}.a;
          pbr_alpha = 1.0{9};
      }}
      if (pbr_vao) {{
          pbr_alpha = pbr_vraw.a;
      }}
      if ((pbr_mode > 0.5 && pbr_mode < 1.5) || pbr_mode > 2.5) {{
          // Unlit (screens, holograms): the surface's own colour and its glow.
          pbr_lo = pbr_diff * pbr_ao;
          if (pbr_sky || pbr_uex) {{
              pbr_lo *= max(ubuf.pbr_backlight.rgb, vec3f(0.0));
          }}
      }}
      // Kind 12's rim, at inverse exposure like its edge: F0 x AO x (1 - n'.v) to the power
      // pbr_layer_height.w (n' the normal nudged by (0.1, -0.1, 0) in view space), times
      // pbr_param.w, on faces turned to world up.
      if (pbr_kind > 11.5 && pbr_kind < 12.5) {{
          let pbr_rn = normalize(pbr_n + vec3f(0.1, -0.1, 0.0));
          let pbr_rim = clamp(pow(abs(1.0 - dot(pbr_rn, pbr_v)), max(ubuf.pbr_layer_height.w, 1e-3)), 0.0, 1.0) *
                        clamp(dot(pbr_n, normalize(ubuf.pbr_up.xyz)), 0.0, 1.0) * ubuf.pbr_param.w;
          pbr_glow += pbr_f0 * (pbr_rim * pbr_ao);
      }}
      // Emitted light is at the room's static exposure, not the frame's (w of tone row 0).
      if (ubuf.pbr_tone[1].x > 0.0 && ubuf.pbr_tone[0].w > 0.0) {{
          if (!(pbr_kind > 21.5 && pbr_kind < 22.5)) {{
              pbr_glow *= ubuf.pbr_tone[0].w;
          }}
          if ((pbr_mode > 0.5 && pbr_mode < 1.5) || pbr_mode > 2.5) {{
              pbr_lo *= ubuf.pbr_tone[0].w;
          }}
      }}{14}
      let pbr_out = max(pbr_lo + pbr_glow, vec3f(0.0));
      // Without a room's tone data Remastered draws through its static default
      // (STonemapParams::BuildLinear(3.0)): exposure 1 and a straight line, clipped at 1.
      var pbr_tm = clamp(pbr_out, vec3f(0.0), vec3f(1.0));
      if (ubuf.pbr_tone[1].x > 0.0) {{
          // Or a tone curve (GX_AURORA_SET_PBR_TONE): a cubic toe, a line and a shoulder
          // that approaches 1.
          let pbr_toe = ((ubuf.pbr_tone[0].x * pbr_out + ubuf.pbr_tone[0].y) * pbr_out + ubuf.pbr_tone[0].z) * pbr_out;
          let pbr_line = ubuf.pbr_tone[1].x * pbr_out + ubuf.pbr_tone[1].y;
          let pbr_st = max(ubuf.pbr_tone[2].y * pbr_out + ubuf.pbr_tone[2].z, vec3f(0.0));
          let pbr_sh = ubuf.pbr_tone[2].x * pbr_st / (1.0 + pbr_st) + ubuf.pbr_tone[2].w;
          pbr_tm = select(select(pbr_sh, pbr_line, pbr_out < vec3f(ubuf.pbr_tone[1].w)), pbr_toe,
                          pbr_out < vec3f(ubuf.pbr_tone[1].z));
          pbr_tm = clamp(pbr_tm, vec3f(0.0), vec3f(1.0));
      }}
      // A model fading (w of the light scale): its alpha in place of an opaque material's, or
      // times a blended one's.
      if (ubuf.pbr_light_scale.w > 0.5) {{
          pbr_alpha = ubuf.pbr_light_scale.w - 1.0;
      }} else if (ubuf.pbr_light_scale.w < -0.5) {{
          pbr_alpha *= -ubuf.pbr_light_scale.w - 1.0;
      }}
)"""
R"""(      // Encoded as Remastered's sRGB swapchain does it: the exact piecewise sRGB curve.
      prev = vec4f(srgb_enc(pbr_tm + pbr_pass), pbr_alpha);
      // A debug view (GXSetPBRDebugView): one input of the shading in place of the result.
      if (ubuf.pbr_layer.w > 0.5) {{
          let pbr_dv = ubuf.pbr_layer.w;
          var pbr_dc = srgb_enc(pbr_base);
          if (pbr_dv > 1.5) {{ pbr_dc = pbr_n * 0.5 + 0.5; }}
          if (pbr_dv > 2.5) {{ pbr_dc = vec3f(pbr_rough); }}
          if (pbr_dv > 3.5) {{ pbr_dc = vec3f(pbr_metal); }}
          if (pbr_dv > 4.5) {{ pbr_dc = vec3f(pbr_ao); }}
          if (pbr_dv > 5.5) {{ pbr_dc = srgb_enc(pbr_ambd); }}
          if (pbr_dv > 6.5) {{ pbr_dc = srgb_enc(pbr_envspec); }}
          if (pbr_dv > 7.5) {{ pbr_dc = srgb_enc(pbr_glow); }}
          if (pbr_dv > 8.5) {{
              // The lit level in stops around middle grey: blue 4 under, green at, red 4 over.
              let pbr_dt = clamp(log2(max(dot(pbr_out, vec3f(0.2126, 0.7152, 0.0722)), 1e-6) / 0.18) / 8.0 + 0.5,
                                 0.0, 1.0);
              pbr_dc = clamp(vec3f(4.0 * pbr_dt - 2.0, 2.0 - abs(4.0 * pbr_dt - 2.0), 2.0 - 4.0 * pbr_dt),
                             vec3f(0.0), vec3f(1.0));
          }}
          if (pbr_dv > 9.5) {{
              // The special surface's kind: grey for none, then a colour each.
              let pbr_dk = i32(pbr_kind + 0.5);
              pbr_dc = vec3f(0.5 * f32(pbr_dk & 1) + 0.25 * f32(pbr_dk == 0),
                             0.5 * f32((pbr_dk >> 1) & 1) + 0.25 * f32(pbr_dk == 0),
                             0.5 * f32((pbr_dk >> 2) & 1) + 0.25 * f32(pbr_dk == 0)) * 1.6;
          }}
          if (pbr_dv > 10.5) {{
              // How much of the sun's shadow map lets through (white lit, black shadowed); magenta
              // where the surface doesn't take the sun's shadow.
              pbr_dc = vec3f(1.0, 0.0, 1.0);
              // pbr-sun-view
          }}
          prev = vec4f(clamp(pbr_dc, vec3f(0.0), vec3f(1.0)), prev.a);
      }}
      if (ubuf.pbr_volume[5].w > 0.5) {{
          prev = vec4f(clamp(pbr_vdiag, vec3f(0.0), vec3f(1.0)), prev.a);
      }}
      // A weight above 1 is a diagnostic: 2 makes the surface a perfect mirror of the
      // probe, and 3 a window onto it, which must line up with the scene around it.
      if (ubuf.pbr_probe[0].w > 1.5) {{
          var pbr_dd = pbr_pd;
          if (ubuf.pbr_probe[0].w > 2.5) {{
              pbr_dd = -(ubuf.pbr_probe[0].xyz * pbr_v.x + ubuf.pbr_probe[1].xyz * pbr_v.y +
                         ubuf.pbr_probe[2].xyz * pbr_v.z);
          }}
          let pbr_diag = textureSampleLevel(pbr_cube, pbr_cube_samp, pbr_dd, 0.0).rgb;
          prev = vec4f(select(pbr_diag, srgb_enc(pbr_diag * pbr_hdr),
                              pbr_hdr > 0.0), prev.a);
      }}
    }})""",
                     base, orm,
                     // A sky's ICAN is its base map (the converter writes a black emissive map where
                     // it copies the base), and its glow ICAN x ICNC is most of what it shows. The
                     // map's last mip, its mean, tells that black map from one with dark texels.
                     mapStage[3] == -1 || config.pbrKind == 20 || config.pbrKind == 24 || config.pbrKind == 27
                         ? "vec4f(0.0)"s
                         : fmt::format("select({0}, {1}, pbr_sky && dot(textureSampleLevel(tex3, tex3_samp, "
                                       "vec2f(0.5), 16.0).rgb, vec3f(1.0)) < 0.004)",
                                       sampled(3, "vec4f(0.0)"), base),
                     normal, GX::MaxLights, attn, amb,
                     gfx::probe::MipCount - 1, diffTint, alphaTint, layer, baseRgb,
                     inventory ? "inv_a"s : layered ? "1.0"s : "prev.a"s, kinds, liquid,
                     shadowed ? "(ubuf.lightState0 | ubuf.lightState1)" : "ubuf.lightState0",
                     shadowed ? " * select(vec3f(1.0), sampled0.rgb, (ubuf.lightState0 & (1u << i)) == 0u)" : "",
                     (layered ? "\n      if (pbr_kind < 0.5 && ubuf.pbr_emissive.w <= 0.0) {\n          pbr_alpha = pbr_lalpha;\n      }"
                                "\n      if (pbr_kind > 25.5 && pbr_kind < 26.5) {\n          pbr_alpha = pbr_lma;\n      }"s
                              : ""s) + decalAlpha);
  if (config.pbrKind == 34 && screen) {
    const size_t at = find_anchor(source, "      // A model fading (w of the light scale)");
    source.insert(at, R"""(
      if (pbr_kind > 33.5 && pbr_kind < 34.5) {
          // Kind 34, Remastered's 788360de / 4c37625c with the material's MFC0 feature off (every current
          // material): ordinary premultiplied blend. o0 = lit colour x C and alpha = A x C (C = 1 here), so
          // One / InvSrcAlpha keeps the scene behind by 1 - A x C, independent of the base colour (no
          // transmission tint). Emulated through the screen copy (map 7), as 2f95a061 is: the scene target
          // holds sRGB-encoded values, so a GPU blend would not be exact. Drawn opaque (alpha 1) with that
          // weight of the scene kept in linear light. The MFC0-on form (1 - C + C T (1 - A)) is not wired.
          let pbr_sdst = srgb_dec(textureSampleLevel(tex7, tex7_samp,
                                  clamp(in.pbr_scr.xy / in.pbr_scr.w * vec2f(0.5, -0.5) + 0.5, vec2f(0.0), vec2f(1.0)), 0.0).rgb);
          pbr_pass += pbr_sdst * vec3f(1.0 - pbr_alpha);
          pbr_alpha = 1.0;
      })""");
  }
  if (inventory) {
    // The incandescence ICMC (rgb only; its alpha is not DMGI) is added linearly after the exposure scaling.
    // DMGI is CCH2.y = 1 on all three inventory materials and is not carried: the bounded default 1.
    const size_t out = find_anchor(source, "      let pbr_out = max(pbr_lo + pbr_glow, vec3f(0.0));");
    source.insert(out, R"""(      pbr_glow += ubuf.pbr_model_incan.rgb;
)""");
    if (config.pbrKind == 36) {
      // Kind 36 coverage C scales the foreground after the port's tone curve, so C = 0 leaves the scene
      // unchanged whatever the global tone (a toe/offset cannot inject a nonzero foreground). o0 = (lit + ICMC) C
      // of the source is not exact through the port tone; C is applied once, here.
      const size_t at = find_anchor(source, "      // A model fading (w of the light scale)");
      source.insert(at, "      pbr_tm *= inv_c;\n");
    }
    if (config.pbrKind == 36 && screen) {
      const size_t at = find_anchor(source, "      // A model fading (w of the light scale)");
      source.insert(at, fmt::format(R"""(
      if (pbr_kind > 35.5 && pbr_kind < 36.5) {{
          // Kind 36, Remastered's 4c37625c with MFC0 on (O2: One / InvSrc1Color): the scene keeps
          // 1 - C + C b (1 - A), by the screen copy (map 7) as kind 34 does; drawn opaque.
          let pbr_sdst = srgb_dec(textureSampleLevel(tex7, tex7_samp,
                                  clamp(in.pbr_scr.xy / in.pbr_scr.w * vec2f(0.5, -0.5) + 0.5, vec2f(0.0), vec2f(1.0)), 0.0).rgb);
          pbr_pass += pbr_sdst * ({});
          pbr_alpha = 1.0;
      }})""",
                                    InvWeightExpr));
    }
    // DIFC alpha is explicit: no generic model fade.
    const size_t fade = find_anchor(source, "      // A model fading (w of the light scale)");
    const size_t fadeEnd = find_anchor(source, "      // Encoded as Remastered's sRGB swapchain");
    assert(fade < fadeEnd);
    source.erase(fade, fadeEnd - fade);
  }
  if (!lit || costTest == 2) {
    // The uniform block has no lights then.
    const size_t begin = find_anchor(source, "// pbr-lights-begin");
    const size_t end = find_anchor(source, "// pbr-lights-end");
    source.erase(begin, end - begin);
  }
  const auto cut = [&](std::string_view what, std::string_view with) {
    const size_t at = find_anchor(source, what);
    source.replace(at, what.size(), with);
  };
  // A baked lightmap (GX_AURORA_SET_PBR_LIGHTMAP) replaces the diffuse ambient: its level L0 in layer 0 and the
  // direction's x, y, z in layers 1-3, each relative to L0, along the three axes in pbr_lmap_axes. The volume's
  // reflection stays, dimmed where the lightmap is dark. Cost test 3 (no ambient volume) goes without it too.
  cut("// pbr-lightmap", lightmapUsed && costTest != 3 ? R"""(if (ubuf.pbr_lmap_rect.w != 0.0) {
          let pbr_luv = ubuf.pbr_lmap_rect.xy + in.pbr_lmuv * ubuf.pbr_lmap_rect.z;
          let pbr_l0 = textureSampleLevel(pbr_lmap, pbr_cube_samp, pbr_luv, 0, 0.0).rgb;
          pbr_l0v = pbr_l0;
          let pbr_l1x = textureSampleLevel(pbr_lmap, pbr_cube_samp, pbr_luv, 1, 0.0).rgb;
          let pbr_l1y = textureSampleLevel(pbr_lmap, pbr_cube_samp, pbr_luv, 2, 0.0).rgb;
          let pbr_l1z = textureSampleLevel(pbr_lmap, pbr_cube_samp, pbr_luv, 3, 0.0).rgb;
          let pbr_ln = vec3f(dot(ubuf.pbr_lmap_axes[0].xyz, pbr_n), dot(ubuf.pbr_lmap_axes[1].xyz, pbr_n),
                             dot(ubuf.pbr_lmap_axes[2].xyz, pbr_n));
          pbr_ambd = max(pbr_l0 * (1.0 + pbr_l1x * pbr_ln.x + pbr_l1y * pbr_ln.y + pbr_l1z * pbr_ln.z),
                         vec3f(0.0)) * ubuf.pbr_lmap_rect.w * pbr_blcm;
          // The modulation scales the lights' diffuse too: BLCM * (lightmap + sun + clustered
          // lights), the specular left as it is (bfb300b6 perm 018, kb material/bfb300b6.md).
          pbr_lo += pbr_ldiff * (pbr_blcm - 1.0);
          // Remastered occludes the probe's reflection by the lightmap's level in place of the
          // grid's (bfb300b6 perm 018): mix(REFP min, intensity, saturate(max(L0) x the
          // modulation's luminance / REFP max)). It replaces the volume's occlusion, not adds to
          // it. Without those parameters the raw level stands in, from 0 to 1.
          let pbr_lmax = max(pbr_l0.r, max(pbr_l0.g, pbr_l0.b));
          if (pbr_hdr > 0.0 && ubuf.pbr_probe[0].w > 0.0 && ubuf.pbr_probe[2].w > 0.0) {
              pbr_envspec = pbr_cubel * mix(ubuf.pbr_probe[1].w, 1.0,
                  clamp(pbr_lmax * dot(pbr_blcm, vec3f(0.2126, 0.7152, 0.0722)) * ubuf.pbr_probe[2].w, 0.0, 1.0));
          } else {
              pbr_envspec *= clamp(pbr_lmax, 0.0, 1.0);
          }
          if (ubuf.pbr_volume[5].w > 1.5 && ubuf.pbr_volume[5].w < 2.5) {
              pbr_vdiag = pbr_ambd;
          }
      })""" : "");
  if (costTest == 3) {
    cut("if (ubuf.pbr_volume[3].w > 0.0) {", "if (false) {");
  } else if (costTest == 4) {
    cut("textureSampleLevel(pbr_cube, pbr_cube_samp, pbr_pd, saturate(pbr_orm.g) * pbr_lod).rgb", "vec3f(0.2)");
    cut("if (pbr_hdr > 0.0 && ubuf.pbr_cube.w > 0.0) {", "if (false) {");
  }
  if (config.sceneHdr) {
    // Into the HDR scene: the exposed light (exposure applied once, above) plus the pass-through,
    // without the tone curve, the clip or the sRGB encode; the single post-tone after the scene
    // does those. The debug views are display colours, so they go back through the curve.
    // Screen reads (map 7, kinds 8/11/14/23/29/30/34/36): in the HDR scene CCubeModel's copy is
    // GXPortCopyScene's linear RGBA16F one, so they read the scene raw (no sRGB decode, no second
    // exposure). Base and cube maps keep their decodes.
    // NOT CLASSIFIED YET (stage 3): pbr_pass mixes terms LDR adds after the tone curve (display
    // domain) with terms that may be light. Each producer needs its own domain from the material
    // before HDR is activated or the render is called correct; until then this sum is not exact,
    // and no blanket inverse of pbr_pass stands in for that classification.
    // Kind 36: the coverage C scales the light before the tone, which is the source's HDR coverage
    // (source (lit + ICMC) * C); LDR's C after the tone is a port workaround. The pause inventory
    // stays an LDR pass, so this is not its default.
    const auto put = [&](std::string_view what, std::string_view with) {
      const size_t at = source.find(what);
      if (at == std::string::npos) {
        Log.fatal("hdr: pbr_func has no '{}'", what);
      }
      source.replace(at, what.size(), with);
    };
    put("      // Without a room's tone data", "      var pbr_lin = pbr_out;\n      // Without a room's tone data");
    if (source.find("      pbr_tm *= inv_c;\n") != std::string::npos) {
      put("      pbr_tm *= inv_c;\n", "      pbr_tm *= inv_c;\n      pbr_lin *= inv_c;\n");
    }
    put("prev = vec4f(srgb_enc(pbr_tm + pbr_pass), pbr_alpha);", "prev = vec4f(pbr_lin + pbr_pass, pbr_alpha);");
    put("prev = vec4f(clamp(pbr_dc, vec3f(0.0), vec3f(1.0)), prev.a);",
        "prev = vec4f(hdr_exposed(pbr_dc, ubuf.pbr_tone[0], ubuf.pbr_tone[1], ubuf.pbr_tone[2]), prev.a);");
    put("prev = vec4f(clamp(pbr_vdiag, vec3f(0.0), vec3f(1.0)), prev.a);",
        "prev = vec4f(hdr_exposed(pbr_vdiag, ubuf.pbr_tone[0], ubuf.pbr_tone[1], ubuf.pbr_tone[2]), prev.a);");
    put("prev = vec4f(select(pbr_diag, ", "prev = vec4f(hdr_exposed(select(pbr_diag, ");
    put("pbr_hdr > 0.0), prev.a);", "pbr_hdr > 0.0), ubuf.pbr_tone[0], ubuf.pbr_tone[1], ubuf.pbr_tone[2]), prev.a);");
    const auto rawScreen = [&](std::string_view what, std::string_view with) {
      for (size_t at = source.find(what); at != std::string::npos; at = source.find(what, at + with.size())) {
        source.replace(at, what.size(), with);
      }
    };
    rawScreen("srgb_dec(pbr_gs)", "pbr_gs");
    rawScreen("srgb_dec(pbr_hg)", "pbr_hg");
    rawScreen("srgb_dec(pbr_sfb)", "pbr_sfb");
    rawScreen("srgb_dec(textureSampleLevel(tex7, tex7_samp,", "(textureSampleLevel(tex7, tex7_samp,");
  }
  return source;
}

absl::flat_hash_set<gfx::ShaderRef> s_seenShaders;
} // namespace

std::string_view inventory_visor_expr(std::string_view name) noexcept {
  return name == "alpha" ? InvAlphaExpr : name == "weight" ? InvWeightExpr : std::string_view{};
}

namespace {
// The sun shadow map's alpha-tested caster of a Remastered cutout (shadowPass 2): vs_shadow_cut is vs_main (so the
// fragment stage gets every interpolant) with the position of vs_shadow, and fs_shadow is fs_main up to its cut test
// (marked //@CUT_END), where it returns: the pixel the colour pass discards casts no shadow either. Remastered's
// DepthOnly perms of the three cutout shaders (93e0c484, 0b2e072b, 42fe2ed0) are the same test followed by KIL.
void add_cut_caster(std::string& source, const ShaderConfig& config) {
  constexpr std::string_view marker = "//@CUT_END";
  constexpr std::string_view fsHead = "@fragment\nfn fs_main(in: VertexOutput) -> @location(0) vec4f {";
  constexpr std::string_view vsHead = "@vertex\nfn vs_main(";
  constexpr std::string_view vsTail = "\n    return out;\n}";
  if (!config.shadow || config.lineMode != 0 || config.pbr == 0) {
    return;
  }
  const size_t markerAt = source.find(marker);
  const size_t fsAt = source.find(fsHead);
  const size_t vsAt = source.find(vsHead);
  if (markerAt == std::string::npos || fsAt == std::string::npos || vsAt == std::string::npos || markerAt < fsAt) {
    return;
  }
  const size_t vsEnd = source.find(vsTail, vsAt);
  if (vsEnd == std::string::npos) {
    return;
  }
  int depth = 1;
  for (size_t i = fsAt + fsHead.size(); i < markerAt; ++i) {
    depth += source[i] == '{' ? 1 : source[i] == '}' ? -1 : 0;
  }
  if (depth < 1) {
    return;
  }
  std::string vs = source.substr(vsAt, vsEnd - vsAt);
  vs.replace(find_anchor(vs, "fn vs_main("), 11, "fn vs_shadow_cut(");
  vs += "\n    var shadow_pos = vec4f(mv_pos, 1.0) * ubuf.shadow_caster[imm.serial];"
        "\n    shadow_pos.z = max(shadow_pos.z, 0.0);"
        // As vs_shadow: an actor's caster[0] holds its height weight in column 3, so w is pinned to 1.
        "\n    shadow_pos.w = 1.0;"
        "\n    out.pos = shadow_pos;";
  vs += vsTail;
  std::string fs = "@fragment\nfn fs_shadow(in: VertexOutput) {";
  fs += source.substr(fsAt + fsHead.size(), markerAt - (fsAt + fsHead.size()));
  fs += "\n    return;";
  for (int i = 0; i < depth; ++i) {
    fs += "\n}";
  }
  source += "\n\n" + vs + "\n\n" + fs + "\n";
}
} // namespace

bool apply_dst_alpha_mode(std::string& source, DstAlphaMode mode) noexcept {
  if (mode == DstAlphaMode::None) {
    return true;
  }
  // fs_main's own return (the draw-id and actor-decal tails replace it and return alpha 1 already).
  constexpr std::string_view tail = "    return prev;\n}";
  const size_t at = source.rfind(tail);
  if (mode == DstAlphaMode::Replace) {
    if (at == std::string::npos) {
      return false;
    }
    source.replace(at, tail.size(), "    return vec4f(prev.rgb, 1.0);\n}");
    return true;
  }
  constexpr std::string_view head = "fn fs_main(in: VertexOutput) -> @location(0) vec4f {";
  const size_t headAt = source.find(head);
  if (at == std::string::npos || headAt == std::string::npos) {
    Log.error("dual-source dst alpha: no fs_main to rewrite, using plain blending");
    return false;
  }
  source.replace(at, tail.size(), "    return GxDstAlphaOut(vec4f(prev.rgb, 1.0), prev);\n}");
  source.replace(headAt, head.size(), "fn fs_main(in: VertexOutput) -> GxDstAlphaOut {");
  source.insert(0, "enable dual_source_blending;\n");
  source += R"(
struct GxDstAlphaOut {
    @location(0) @blend_src(0) color: vec4f,
    @location(0) @blend_src(1) blend: vec4f,
};
)";
  return true;
}

bool shader_has_cut_caster(const ShaderConfig& config) noexcept {
  return build_shader_source(config).find("fn fs_shadow(") != std::string::npos;
}

namespace {
// Texture vertex fetch (gfx::g_vertexFetchTexture): the shader reads the vertex and array data from
// R32Uint textures instead of read-only storage arrays, which a GPU with no vertex-stage storage
// buffers cannot declare. Every word access already goes through load_word, which addresses the
// texture as rows of 4096 texels (gfx::detail::FetchTextureRowBytes), so this only rewrites the
// data's declarations, the helpers' parameter types and load_word's body.
void apply_texture_vertex_fetch(std::string& source) {
  const auto replaceAll = [&source](std::string_view from, std::string_view to) {
    for (size_t at = source.find(from); at != std::string::npos; at = source.find(from, at + to.size())) {
      source.replace(at, from.size(), to);
    }
  };
  replaceAll("var<storage, read> vbuf: array<u32>;", "var vbuf: texture_2d<u32>;");
  replaceAll("var<storage, read> abuf: array<u32>;", "var abuf: texture_2d<u32>;");
  // The helpers take the data as a parameter; a texture is a value, not a pointer to one.
  replaceAll("ptr<storage, array<u32>>", "texture_2d<u32>");
  replaceAll("&vbuf", "vbuf");
  replaceAll("&abuf", "abuf");
  // The only word read: the storage version's arrayLength guard becomes the texture's extent.
  constexpr std::string_view guard = "  if (word_idx < arrayLength(p)) {\n    return p[word_idx];\n  }\n  return 0u;\n";
  const size_t wordAt = find_anchor(source, guard);
  source.replace(wordAt, guard.size(),
                 "  let dims = textureDimensions(p);\n"
                 "  if (word_idx < dims.x * dims.y) {\n"
                 "    return textureLoad(p, vec2u(word_idx & 4095u, word_idx >> 12u), 0).x;\n"
                 "  }\n"
                 "  return 0u;\n");
  // A few expressions index the data directly (the fog row, the TEV colour rows); they become
  // load_word calls, as their indices are u32 word indices and never nested.
  for (const std::string_view name : {"vbuf"sv, "abuf"sv}) {
    std::string needle{name};
    needle += '[';
    size_t at = 0;
    while ((at = source.find(needle, at)) != std::string::npos) {
      const size_t close = source.find(']', at + needle.size());
      assert(close != std::string::npos);
      std::string replacement{"load_word("};
      replacement += name;
      replacement += ", ";
      replacement += source.substr(at + needle.size(), close - at - needle.size());
      replacement += ')';
      source.replace(at, close + 1 - at, replacement);
      at += replacement.size();
    }
  }
}
} // namespace

std::string build_shader_source(const ShaderConfig& config) noexcept {
  ZoneScoped;
  const auto hash = xxh3_hash(config);
  const auto info = build_shader_info(config);
  if (EnableDebugPrints && !s_seenShaders.contains(hash)) {
    s_seenShaders.insert(hash);

    Log.info("Shader config (hash {:x}):", hash);
    {
      for (int i = 0; i < config.tevStageCount; ++i) {
        const auto& stage = config.tevStages[i];
        Log.info("  tevStages[{}]:", i);
        Log.info("    color_a: {}", TevColorArgNames[stage.colorPass.a]);
        Log.info("    color_b: {}", TevColorArgNames[stage.colorPass.b]);
        Log.info("    color_c: {}", TevColorArgNames[stage.colorPass.c]);
        Log.info("    color_d: {}", TevColorArgNames[stage.colorPass.d]);
        Log.info("    alpha_a: {}", TevAlphaArgNames[stage.alphaPass.a]);
        Log.info("    alpha_b: {}", TevAlphaArgNames[stage.alphaPass.b]);
        Log.info("    alpha_c: {}", TevAlphaArgNames[stage.alphaPass.c]);
        Log.info("    alpha_d: {}", TevAlphaArgNames[stage.alphaPass.d]);
        Log.info("    color_op_clamp: {}", stage.colorOp.clamp);
        Log.info("    color_op_op: {}", stage.colorOp.op);
        Log.info("    color_op_bias: {}", stage.colorOp.bias);
        Log.info("    color_op_scale: {}", stage.colorOp.scale);
        Log.info("    color_op_reg_id: {}", stage.colorOp.outReg);
        Log.info("    alpha_op_clamp: {}", stage.alphaOp.clamp);
        Log.info("    alpha_op_op: {}", stage.alphaOp.op);
        Log.info("    alpha_op_bias: {}", stage.alphaOp.bias);
        Log.info("    alpha_op_scale: {}", stage.alphaOp.scale);
        Log.info("    alpha_op_reg_id: {}", stage.alphaOp.outReg);
        Log.info("    kc_sel: {}", stage.kcSel);
        Log.info("    ka_sel: {}", stage.kaSel);
        Log.info("    texCoordId: {}", stage.texCoordId);
        Log.info("    texMapId: {}", stage.texMapId);
        Log.info("    channelId: {}", stage.channelId);
        Log.info("    tevSwapRas: {}", stage.tevSwapRas);
        Log.info("    tevSwapTex: {}", stage.tevSwapTex);
        Log.info("    indTexStage: {}", stage.indTexStage);
        Log.info("    indTexFormat: {}", stage.indTexFormat);
        Log.info("    indTexBiasSel: {}", stage.indTexBiasSel);
        Log.info("    indTexAlphaSel: {}", stage.indTexAlphaSel);
        Log.info("    indTexMtxId: {}", stage.indTexMtxId);
        Log.info("    indTexWrapS: {}", stage.indTexWrapS);
        Log.info("    indTexWrapT: {}", stage.indTexWrapT);
        Log.info("    indTexUseOrigLOD: {}", stage.indTexUseOrigLOD);
        Log.info("    indTexAddPrev: {}", stage.indTexAddPrev);
      }
      Log.info("  numIndStages: {}", config.numIndStages);
      for (u32 i = 0; i < config.numIndStages; ++i) {
        const auto& stage = config.indStages[i];
        Log.info("  indStages[{}]: texCoordId {} texMapId {} scaleS {} scaleT {}", i, stage.texCoordId, stage.texMapId,
                 stage.scaleS, stage.scaleT);
      }
      for (int i = 0; i < config.colorChannels.size(); ++i) {
        const auto& chan = config.colorChannels[i];
        Log.info("  colorChannels[{}]: enabled {} mat {} amb {}", static_cast<GXChannelID>(i), chan.lightingEnabled,
                 chan.matSrc, chan.ambSrc);
      }
      for (int i = 0; i < config.tcgs.size(); ++i) {
        const auto& tcg = config.tcgs[i];
        if (tcg.src != GX_MAX_TEXGENSRC) {
          Log.info("  tcg[{}]: src {} mtx {} post {} type {} norm {}", i, tcg.src, tcg.mtx, tcg.postMtx, tcg.type,
                   tcg.normalize);
        }
      }
      Log.info("  alphaCompare: comp0 {} ref0 {} op {} comp1 {} ref1 {}", config.alphaCompare.comp0,
               config.alphaCompare.ref0, config.alphaCompare.op, config.alphaCompare.comp1, config.alphaCompare.ref1);
      Log.info("  fogType: {}", config.fogType);
      Log.info("  fogRangeEnabled: {}", config.fogRangeEnabled);
    }
  }

  std::string uniformPre;
  std::string uniBufAttrs;
  std::string texBindings;
  std::string vtxOutAttrs;
  std::string vtxInAttrs;
  std::string vtxXfrAttrsPre;
  std::string vtxXfrAttrs;
  size_t vtxOutIdx = 0;

  // Load points for line/point expansion
  std::string_view vidxAttr = "vidx"sv;
  if (config.lineMode != 0) {
    vtxInAttrs += ",\n    @builtin(instance_index) iidx: u32";
    uniBufAttrs +=
        "\n    line_width: f32,"
        "\n    line_aspect_y: f32,"
        "\n    line_tex_offset: f32,"
        "\n    line_texcoord_mask: u32,";
    if (config.lineMode == 3) {
      // GX_POINTS: each instance = one vertex, expand to quad
      vtxXfrAttrsPre += fmt::format(
          "\n    let in_vidx = iidx;"
          "\n    let in_pos = {};"
          "\n    let in_pnmtxidx = {};"
          "\n    let mv_pos = vec4f(in_pos, 1.0) * ubuf.postex_mtx[in_pnmtxidx];",
          attr_load(config, GX_VA_POS, "in_vidx"sv), attr_load(config, GX_VA_PNMTXIDX, "in_vidx"sv));
    } else {
      // GX_LINES / GX_LINESTRIP: each instance = two vertices, expand to quad
      vtxXfrAttrsPre += fmt::format(
          "\n    let use_b = vidx >= 2u;"
          "\n    let vidx_a = iidx * {}u;"
          "\n    let vidx_b = vidx_a + 1u;"
          "\n    let in_vidx = select(vidx_a, vidx_b, use_b);"
          "\n    let pos_a = {};"
          "\n    let pos_b = {};"
          "\n    let in_pos = select(pos_a, pos_b, use_b);"
          "\n    let pnmtxidx_a = {};"
          "\n    let pnmtxidx_b = {};"
          "\n    let in_pnmtxidx = select(pnmtxidx_a, pnmtxidx_b, use_b);"
          "\n    let mv_pos_a = vec4f(pos_a, 1.0) * ubuf.postex_mtx[pnmtxidx_a];"
          "\n    let mv_pos_b = vec4f(pos_b, 1.0) * ubuf.postex_mtx[pnmtxidx_b];"
          "\n    let mv_pos = select(mv_pos_a, mv_pos_b, use_b);",
          config.lineMode == 1 ? 2 : 1, attr_load(config, GX_VA_POS, "vidx_a"sv),
          attr_load(config, GX_VA_POS, "vidx_b"sv), attr_load(config, GX_VA_PNMTXIDX, "vidx_a"sv),
          attr_load(config, GX_VA_PNMTXIDX, "vidx_b"sv));
    }
    vidxAttr = "in_vidx"sv;
  } else if (config.attrs[GX_VA_PNMTXIDX].attrType == GX_NONE) {
    vtxXfrAttrsPre += "\n    let in_pnmtxidx = imm.current_pnmtx;";
  }

  // Load vertex attributes
  for (GXAttr attr = GX_VA_PNMTXIDX; attr <= GX_VA_TEX7; attr = static_cast<GXAttr>(attr + 1)) {
    const auto attrType = config.attrs[attr].attrType;
    if (attrType == GX_NONE) {
      continue;
    }
    // in_pnmtxidx and in_pos written above for line mode
    if ((attr != GX_VA_PNMTXIDX && attr != GX_VA_POS) || config.lineMode == 0) {
      vtxXfrAttrsPre += fmt::format("\n    let {} = {};", vtx_attr(config, attr), attr_load(config, attr, vidxAttr));
    }
  }
  if (config.pbr != 0) {
    if (config.pbrBindPos && config.attrs[GX_VA_POS].attrType != GX_NONE) {
      const auto& pm = config.attrs[GX_VA_POS];
      // TEX7's array holds the bind-pose positions (12 bytes each), indexed like POS.
      const auto index = fmt::format(fmt::runtime(pm.attrType == GX_INDEX8 ? "raw_fetch_u8_1(&vbuf, imm.vtx_start + {} * {}u + {}u)"
                                                              : "raw_fetch_u16_1(&vbuf, imm.vtx_start + {} * {}u + {}u, false)"),
                                     vidxAttr, config.vtxStride, pm.offset);
      vtxXfrAttrsPre += fmt::format(
          "\n    let pbr_bind_y = clamp(raw_fetch_f32_1(&abuf, {} + {} * 12u + 4u, {}) * ubuf.pbr_bklight[2].z"
          " + ubuf.pbr_bklight[2].y, 0.0, 1.0);",
          imm_array_start(GX_VA_TEX7), index, config.pbrBindLe ? "true" : "false");
    } else {
      vtxXfrAttrsPre += "\n    let pbr_bind_y = -1.0;";
    }
  }
  bool needsBinrm = false;
  bool needsTangent = false;
  for (int i = 0; i < info.sampledTexCoords.size(); ++i) {
    if (!info.sampledTexCoords.test(i)) {
      continue;
    }
    const bool emboss = is_emboss_texgen(config.tcgs[i].type);
    needsBinrm = needsBinrm || config.tcgs[i].src == GX_TG_BINRM || emboss;
    needsTangent = needsTangent || config.tcgs[i].src == GX_TG_TANGENT || emboss;
  }
  if (needsBinrm) {
    vtxXfrAttrsPre += fmt::format("\n    let {} = {};", nbt_slice_local(NbtSlice::B),
                                  attr_load_nbt_slice(config, NbtSlice::B, vidxAttr));
  }
  if (needsTangent) {
    vtxXfrAttrsPre += fmt::format("\n    let {} = {};", nbt_slice_local(NbtSlice::T),
                                  attr_load_nbt_slice(config, NbtSlice::T, vidxAttr));
  }

  // Remastered's procedural foliage sway (WindAnimData c4[0..3], in pbr_shield[0..3]): the
  // model's position moves by the inverse of the model->world 3x3 (rows in pbr_shield[4..6],
  // the world translation in their w) applied to a per-axis sine, scaled by the vertex
  // colour's alpha. The impulse terms (c4[4..11]) are zero without a source.
  // The sway as WGSL: wind_pos from the model-space position and the vertex colour (its alpha is the weight).
  const bool hasWind = config.pbr && config.pbrKind == 0 && config.attrs[GX_VA_CLR0].attrType != GX_NONE;
  const auto windCode = [&](const std::string& pos, const std::string& clr) {
    return fmt::format(
        "\n    var wind_pos = {0};"
        "\n    if ((u32(ubuf.pbr_backlight.w) & 32768u) != 0u) {{"
        "\n      let w_p = {0};"
        "\n      let w_a = {1}.a;"
        "\n      let w_c0 = ubuf.pbr_shield[0];"
        "\n      let w_c1 = ubuf.pbr_shield[1];"
        "\n      let w_c2 = ubuf.pbr_shield[2];"
        "\n      let w_c3 = ubuf.pbr_shield[3];"
        "\n      let w_s = vec3f("
        "\n        sin(w_c1.w + ubuf.pbr_shield[4].w + w_p.y * w_c2.y + w_p.z * w_c2.z),"
        "\n        sin(w_c1.w + ubuf.pbr_shield[5].w + w_p.x * w_c2.x + w_p.z * w_c2.z),"
        "\n        sin(w_c1.w + ubuf.pbr_shield[6].w + w_p.x * w_c2.x + w_p.y * w_c2.y));"
        "\n      let w_d = ((w_s * w_c0.w + w_c0.xyz) * w_c3.xyz) * w_a;"
        "\n      wind_pos = w_p + vec3f(dot(w_d, ubuf.pbr_shield[4].xyz), dot(w_d, ubuf.pbr_shield[5].xyz),"
        "\n                             dot(w_d, ubuf.pbr_shield[6].xyz)) * w_c1.xyz;"
        "\n    }}",
        pos, clr);
  };

  // The pickup's (kind 15, Remastered's PickUp 3E95A9FE, perm 000_1) travelling bump: the skinned model-space
  // position moves along its unit normal by sin(CCH3.y t - CCH3.x p.y) CCH3.z a, a = the vertex colour's alpha
  // (raw), times vp_c1[0].x (an engine constant, 1 here). CCH3 is pbr_shield[3], t is pbr_param.x.
  const bool hasBump = config.pbr && config.pbrKind == 15 && config.attrs[GX_VA_CLR0].attrType != GX_NONE &&
                       config.attrs[GX_VA_NRM].attrType != GX_NONE;
  const auto bumpCode = [&](const std::string& pos, const std::string& clr, const std::string& nrm) {
    return fmt::format(
        "\n    let bump_n = {2};"
        "\n    let bump_k = sin(ubuf.pbr_shield[3].y * ubuf.pbr_param.x - ubuf.pbr_shield[3].x * {0}.y) *"
        "\n                 ubuf.pbr_shield[3].z * {1}.a;"
        "\n    let bump_pos = {0} + select(bump_n, normalize(bump_n), dot(bump_n, bump_n) > 1e-10) * bump_k;",
        pos, clr, nrm);
  };

  // ShaderConfig::shadow: vs_shadow places the vertex in the sun's shadow map (gfx/shadow.cpp's caster pass).
  std::string shadowVs;
  if (config.shadow && config.lineMode == 0) {
    shadowVs = "\n\n@vertex\nfn vs_shadow(@builtin(vertex_index) vidx: u32) -> @builtin(position) vec4f {";
    if (config.attrs[GX_VA_PNMTXIDX].attrType == GX_NONE) {
      shadowVs += "\n    let in_pnmtxidx = imm.current_pnmtx;";
    } else {
      shadowVs += fmt::format("\n    let {} = {};", vtx_attr(config, GX_VA_PNMTXIDX),
                              attr_load(config, GX_VA_PNMTXIDX, vidxAttr));
    }
    // Remastered's depth/shadow perm (000_1) carries the same sway, so the caster moves with the foliage.
    std::string shadowPos = vtx_attr(config, GX_VA_POS);
    std::string shadowWind;
    if (hasWind) {
      shadowWind = fmt::format("\n    let {} = {};", vtx_attr(config, GX_VA_CLR0), attr_load(config, GX_VA_CLR0, vidxAttr)) +
                   windCode(shadowPos, vtx_attr(config, GX_VA_CLR0));
      shadowPos = "wind_pos";
    }
    shadowVs += fmt::format("\n    let {} = {};", vtx_attr(config, GX_VA_POS), attr_load(config, GX_VA_POS, vidxAttr));
    shadowVs += shadowWind;
    shadowVs += fmt::format("\n    let mv_pos = vec4f({}, 1.0) * ubuf.postex_mtx[in_pnmtxidx];"
                            "\n    var pos = vec4f(mv_pos, 1.0) * ubuf.shadow_caster[imm.serial];"
                            // Pancaked: a caster further toward the sun than the map reaches (a roof high
                            // over the floor) sits on its near plane rather than being clipped. The map is
                            // orthographic, so that moves nothing across it.
                            "\n    pos.z = max(pos.z, 0.0);"
                            // The map is orthographic: an actor's caster[0] holds its height weight in column 3
                            // (the decal reads it), so w is pinned to 1 rather than taken from the matrix.
                            "\n    pos.w = 1.0;"
                            "\n    return pos;\n}}",
                            shadowPos);
  }
  if (config.lineMode == 0) {
    std::string windPos = vtx_attr(config, GX_VA_POS);
    if (hasWind) {
      vtxXfrAttrsPre += windCode(windPos, vtx_attr(config, GX_VA_CLR0));
      windPos = "wind_pos";
    }
    if (hasBump) {
      vtxXfrAttrsPre += bumpCode(windPos, vtx_attr(config, GX_VA_CLR0), vtx_attr(config, GX_VA_NRM));
      windPos = "bump_pos";
    }
    vtxXfrAttrsPre += fmt::format(
        "\n    let mv_pos = vec4f({}, 1.0) * ubuf.postex_mtx[in_pnmtxidx];"
        "\n    out.pos = vec4f(mv_pos, 1.0) * ubuf.proj;",
        windPos);
  } else if (config.lineMode == 3) {
    // GX_POINTS: expand single vertex to axis-aligned screen-space square
    vtxXfrAttrsPre +=
        "\n    let clip = vec4f(mv_pos, 1.0) * ubuf.proj;"
        "\n    let viewport_scale = ubuf.render_viewport_size / max(ubuf.logical_viewport_size, vec2f(1.0));"
        "\n    let point_size = ubuf.line_width * min(viewport_scale.x, viewport_scale.y);"
        "\n    let x_sign = select(-1.0, 1.0, (vidx & 1u) != 0u);"
        "\n    let y_sign = select(-1.0, 1.0, vidx >= 2u);"
        "\n    let offset_px = vec2f(x_sign, y_sign) * (point_size / 2.0);"
        "\n    let offset_ndc = (offset_px * 2.0) / ubuf.render_viewport_size;"
        "\n    out.pos = vec4f(clip.xy + offset_ndc * clip.w, clip.zw);";
  } else {
    // GX_LINES / GX_LINESTRIP: expand line segment perpendicular to direction
    // Port: GX clips a line at the near plane before widening it. An endpoint behind the eye has a
    // negative w, so its NDC flips to the far side and the widened quad spans the screen (Echo
    // Visor sound waves around Quadraxis filled the view, #65): move it onto w = eps along the
    // segment first, and drop a segment with both ends behind (z = -2w is clipped).
    vtxXfrAttrsPre +=
        "\n    var clip_a = vec4f(mv_pos_a, 1.0) * ubuf.proj;"
        "\n    var clip_b = vec4f(mv_pos_b, 1.0) * ubuf.proj;"
        "\n    let line_near_w = 1e-4;"
        "\n    if (clip_a.w < line_near_w && clip_b.w < line_near_w) {"
        "\n        clip_a = vec4f(0.0, 0.0, -2.0, 1.0);"
        "\n        clip_b = clip_a;"
        "\n    } else if (clip_a.w < line_near_w) {"
        "\n        clip_a = mix(clip_a, clip_b, (line_near_w - clip_a.w) / (clip_b.w - clip_a.w));"
        "\n    } else if (clip_b.w < line_near_w) {"
        "\n        clip_b = mix(clip_b, clip_a, (line_near_w - clip_b.w) / (clip_a.w - clip_b.w));"
        "\n    }"
        "\n    let ndc_a = clip_a.xy / clip_a.w;"
        "\n    let ndc_b = clip_b.xy / clip_b.w;"
        "\n    let viewport_scale = ubuf.render_viewport_size / max(ubuf.logical_viewport_size, vec2f(1.0));"
        "\n    let delta_px = (ndc_b - ndc_a) / 2.0 * ubuf.render_viewport_size;"
        "\n    let dir_px = select(vec2f(1.0, 0.0), normalize(delta_px), dot(delta_px, delta_px) > 1e-10);"
        "\n    let perp_px = vec2f(-dir_px.y, dir_px.x);"
        "\n    let line_width = ubuf.line_width * min(viewport_scale.x, viewport_scale.y);"
        "\n    let offset_px = perp_px * (line_width / 2.0) * select(-1.0, 1.0, (vidx & 1u) != 0u);"
        "\n    let offset_ndc = (offset_px * 2.0) / ubuf.render_viewport_size;"
        "\n    let clip_base = select(clip_a, clip_b, use_b);"
        "\n    out.pos = vec4f(clip_base.xy + offset_ndc * clip_base.w, clip_base.zw);";
  }
  vtxXfrAttrsPre += fmt::format(
      "\n    let nrm_tmp = vec4f({}, 0.0) * ubuf.nrm_mtx[in_pnmtxidx];"
      "\n    let mv_nrm = select(nrm_tmp, normalize(nrm_tmp), dot(nrm_tmp, nrm_tmp) > 1e-10);",
      vtx_attr(config, GX_VA_NRM));
  if (config.actorDecal != 0 && config.lineMode == 0) {
    // Remastered's projected actor shadow (gfx/actor_shadow.hpp): the fragment stage needs the view-space
    // position, and the decal sits 0.015 m toward the camera so it wins LEQUAL against the surface it paints.
    vtxOutAttrs += fmt::format("\n    @location({}) dc_pos: vec3f,", vtxOutIdx++);
    vtxXfrAttrsPre += "\n    out.dc_pos = mv_pos;"
                      "\n    out.pos = vec4f(mv_pos + vec3f(0.0, 0.0, 0.015), 1.0) * ubuf.proj;";
  }
  if constexpr (EnableNormalVisualization) {
    vtxOutAttrs += fmt::format("\n    @location({}) nrm: vec3f,", vtxOutIdx++);
    vtxXfrAttrsPre += "\n    out.nrm = mv_nrm;";
  }

  uniBufAttrs += "\n    proj: mat4x4f,";
  uniBufAttrs += fmt::format("\n    postex_mtx: array<mat3x4f, {}>,", MaxPnMtx + MaxTexMtx);
  uniBufAttrs += fmt::format("\n    nrm_mtx: array<mat3x4f, {}>,", MaxPnMtx);
  std::string fragmentFnPre;
  std::string fragmentFn;

  static std::array regName{"prev"sv, "tevreg0"sv, "tevreg1"sv, "tevreg2"sv};
  std::array<bool, MaxTevRegs> colorNormalized{};
  std::array<bool, MaxTevRegs> alphaNormalized{};
  for (u32 idx = 0; idx < config.tevStageCount; ++idx) {
    const auto& stage = config.tevStages[idx];
    const auto color_arg = [&](GXTevColorArg arg) {
      auto value = color_arg_reg(arg, idx, config, stage);
      if (tev_color_arg_is_normalized(arg, colorNormalized, alphaNormalized)) {
        return fmt::format("vec3f({})", value);
      }
      return fmt::format("tev_overflow_vec3f({})", value);
    };
    const bool alphaCompareColor =
        stage.alphaOp.op >= GX_TEV_COMP_R8_GT && stage.alphaOp.op <= GX_TEV_COMP_BGR24_EQ;
    // Packed alpha comparisons use color A/B from before this stage writes its color result.
    if (alphaCompareColor) {
      fragmentFn += fmt::format("\n    let tev_cmp_a{0} = {1};\n    let tev_cmp_b{0} = {2};", idx,
                                color_arg(stage.colorPass.a), color_arg(stage.colorPass.b));
    }
    {
      std::string_view outReg = regName[stage.colorOp.outReg];
      std::string op = tev_color_op(stage.colorOp.op, tev_bias(stage.colorOp.bias), tev_scale(stage.colorOp.scale),
                                    stage.colorOp.clamp, color_arg(stage.colorPass.a), color_arg(stage.colorPass.b),
                                    color_arg(stage.colorPass.c), color_arg_reg(stage.colorPass.d, idx, config, stage));
      fragmentFn += fmt::format("\n    // TEV stage {2}\n    {0} = vec4f({1}, {0}.a);", outReg, op, idx);
      colorNormalized[stage.colorOp.outReg] = stage.colorOp.clamp;
    }
    {
      const auto alpha_arg = [&](GXTevAlphaArg arg) {
        auto value = alpha_arg_reg(arg, idx, config, stage);
        if (tev_alpha_arg_is_normalized(arg, alphaNormalized)) {
          return value;
        }
        return fmt::format("tev_overflow_f32({})", value);
      };
      std::string_view outReg = regName[stage.alphaOp.outReg];
      std::string op = tev_alpha_op(
          stage.alphaOp.op, tev_bias(stage.alphaOp.bias), tev_scale(stage.alphaOp.scale), stage.alphaOp.clamp,
          alphaCompareColor ? fmt::format("tev_cmp_a{}", idx) : alpha_arg(stage.alphaPass.a),
          alphaCompareColor ? fmt::format("tev_cmp_b{}", idx) : alpha_arg(stage.alphaPass.b),
          alpha_arg(stage.alphaPass.c), alpha_arg_reg(stage.alphaPass.d, idx, config, stage));
      fragmentFn += fmt::format("\n    {0}.a = {1};", outReg, op);
      alphaNormalized[stage.alphaOp.outReg] = stage.alphaOp.clamp;
    }
  }

  const auto& lastStage = config.tevStages[config.tevStageCount - 1];
  const bool prevColorNormalized = colorNormalized[lastStage.colorOp.outReg];
  const bool prevAlphaNormalized = alphaNormalized[lastStage.alphaOp.outReg];
  if (lastStage.colorOp.outReg != 0) {
    fragmentFn += fmt::format("\n    prev = vec4f({0}.rgb, prev.a);", regName[lastStage.colorOp.outReg]);
  }
  if (lastStage.alphaOp.outReg != 0) {
    fragmentFn += fmt::format("\n    prev.a = {0}.a;", regName[lastStage.alphaOp.outReg]);
  }

  if (info.loadsTevReg.test(0)) {
    uniBufAttrs += "\n    tevprev: vec4f,";
    fragmentFnPre += "\n    var prev = ubuf.tevprev;";
  } else {
    fragmentFnPre += "\n    var prev: vec4f;";
  }
  for (int i = 1 /* Skip TEVPREV */; i < info.loadsTevReg.size(); ++i) {
    if (info.loadsTevReg.test(i)) {
      uniBufAttrs += fmt::format("\n    tevreg{}: vec4f,", i - 1);
      fragmentFnPre += fmt::format("\n    var tevreg{0} = ubuf.tevreg{0};", i - 1);
    } else if (info.writesTevReg.test(i)) {
      fragmentFnPre += fmt::format("\n    var tevreg{0}: vec4f;", i - 1);
    }
  }

  if (info.lightingEnabled) {
    uniBufAttrs += fmt::format(FMT_STRING(R"""(
    lights: array<Light, {}>,
    lightState0: u32,
    lightState1: u32,
    lightState0a: u32,
    lightState1a: u32,)"""),
                               GX::MaxLights);
    uniformPre +=
        "\n"
        "struct Light {\n"
        "    pos: vec3f,\n"
        "    dir: vec3f,\n"
        "    color: vec4f,\n"
        "    cos_att: vec3f,\n"
        "    dist_att: vec3f,\n"
        "};";
    if (config.perPixelLight) {
      vtxOutAttrs += fmt::format("\n    @location({}) mv_pos: vec3f,", vtxOutIdx++);
      vtxOutAttrs += fmt::format("\n    @location({}) mv_nrm: vec3f,", vtxOutIdx++);
      vtxXfrAttrs += fmt::format(FMT_STRING(R"""(
    out.mv_pos = mv_pos;
    out.mv_nrm = mv_nrm;)"""));
      // Interpolation shortens the normal; renormalised as the vertex stage does.
      fragmentFnPre += "\n    let pp_nrm = select(in.mv_nrm, normalize(in.mv_nrm), dot(in.mv_nrm, in.mv_nrm) > 1e-10);";
    }
  }

  for (int i = 0; i < info.sampledColorChannels.size(); ++i) {
    if (!info.sampledColorChannels.test(i)) {
      continue;
    }

    const auto& cc = config.colorChannels[i];
    const auto& cca = config.colorChannels[i + GX_ALPHA0];
    if (cc.lightingEnabled && cc.ambSrc == GX_SRC_REG) {
      uniBufAttrs += fmt::format("\n    cc{0}_amb: vec4f,", i);
    }
    if (cc.matSrc == GX_SRC_REG) {
      uniBufAttrs += fmt::format("\n    cc{0}_mat: vec4f,", i);
    }
    if (cca.lightingEnabled && cca.ambSrc == GX_SRC_REG) {
      uniBufAttrs += fmt::format("\n    cc{0}a_amb: vec4f,", i);
    }
    if (cca.matSrc == GX_SRC_REG) {
      uniBufAttrs += fmt::format("\n    cc{0}a_mat: vec4f,", i);
    }

    // Output vertex color if necessary
    if (config.perPixelLight) {
      if ((cc.lightingEnabled && cc.ambSrc == GX_SRC_VTX) || cc.matSrc == GX_SRC_VTX ||
          (cca.lightingEnabled && cca.ambSrc == GX_SRC_VTX) || cca.matSrc == GX_SRC_VTX) {
        vtxOutAttrs += fmt::format("\n    @location({}) clr{}: vec4f,", vtxOutIdx++, i);
        vtxXfrAttrs += fmt::format("\n    out.clr{} = {};", i, vtx_attr(config, static_cast<GXAttr>(GX_VA_CLR0 + i)));
      }
    }

    if (config.perPixelLight) {
      fragmentFnPre += fmt::format("\n    var rast{}: vec4f;", i);
      fragmentFnPre += lighting_func(config, cc, i, false);
      fragmentFnPre += lighting_func(config, cca, i, true);
    } else {
      vtxOutAttrs += fmt::format("\n    @location({}) cc{}: vec4f,", vtxOutIdx++, i);
      vtxXfrAttrs += lighting_func(config, cc, i, false);
      vtxXfrAttrs += lighting_func(config, cca, i, true);
      fragmentFnPre += fmt::format("\n    var rast{0} = in.cc{0};", i);
    }
  }
  if (info.usesTevKonst) {
    // Each stage's konst input (its kcSel and kaSel), so a konst selection needs no new pipeline.
    uniBufAttrs += fmt::format("\n    tev_konst: array<vec4f, {}>,", config.tevStageCount);
  }
  for (int i = 0; i < info.sampledTexCoords.size(); ++i) {
    if (!info.sampledTexCoords.test(i)) {
      continue;
    }
    const auto& tcg = config.tcgs[i];
    if (tcg.type == GX_TG_MTX3x4) {
      vtxOutAttrs += fmt::format("\n    @location({}) tex{}_uvw: vec3f,", vtxOutIdx++, i);
    } else {
      vtxOutAttrs += fmt::format("\n    @location({}) tex{}_uv: vec2f,", vtxOutIdx++, i);
    }
    if (is_emboss_texgen(tcg.type)) {
      // Emboss bump: offset the source texcoord by the light projected onto tangent/binormal
      const u32 lightIdx = tcg.type - GX_TG_BUMP0;
      vtxXfrAttrs += fmt::format(
          "\n    let bump_ldir{0} = normalize(ubuf.lights[{1}].pos - mv_pos);"
          "\n    let bump_tan{0} = vec4f(in_tangent, 0.0) * ubuf.nrm_mtx[in_pnmtxidx];"
          "\n    let bump_bin{0} = vec4f(in_binrm, 0.0) * ubuf.nrm_mtx[in_pnmtxidx];"
          "\n    out.tex{0}_uv = tc{2}_proj.xy + vec2f(dot(bump_ldir{0}, bump_tan{0}), dot(bump_ldir{0}, "
          "bump_bin{0}));",
          i, lightIdx, tcg.embossSrc);
      fragmentFnPre += fmt::format("\n    var tex{0}_uv = in.tex{0}_uv.xy;", i);
      continue;
    }
    if (tcg.src >= GX_TG_TEX0 && tcg.src <= GX_TG_TEX7) {
      vtxXfrAttrs += fmt::format("\n    var tc{} = vec4f({}, 1.0, 1.0);", i,
                                 vtx_attr(config, GXAttr(GX_VA_TEX0 + (tcg.src - GX_TG_TEX0))));
    } else if (tcg.src >= GX_TG_TEXCOORD0 && tcg.src <= GX_TG_TEXCOORD6) {
      vtxXfrAttrs += fmt::format("\n    var tc{} = tc{};", i, (tcg.src - GX_TG_TEXCOORD0));
    } else if (tcg.src == GX_TG_POS) {
      vtxXfrAttrs += fmt::format("\n    var tc{} = vec4f({}, 1.0);", i, vtx_attr(config, GX_VA_POS));
    } else if (tcg.src == GX_TG_NRM) {
      vtxXfrAttrs += fmt::format("\n    var tc{} = vec4f({}, 1.0);", i, vtx_attr(config, GX_VA_NRM));
    } else if (tcg.src == GX_TG_COLOR0) {
      // Per-pixel lighting has no vertex-stage out.cc0; fall back to the raw vertex colour there.
      if (config.perPixelLight) {
        vtxXfrAttrs += fmt::format("\n    var tc{} = {};", i, vtx_attr(config, GX_VA_CLR0));
      } else {
        vtxXfrAttrs += fmt::format("\n    var tc{} = out.cc0;", i);
      }
    } else if (tcg.src == GX_TG_COLOR1) {
      // Per-pixel lighting has no vertex-stage out.cc1; fall back to the raw vertex colour there.
      if (config.perPixelLight) {
        vtxXfrAttrs += fmt::format("\n    var tc{} = {};", i, vtx_attr(config, GX_VA_CLR1));
      } else {
        vtxXfrAttrs += fmt::format("\n    var tc{} = out.cc1;", i);
      }
    } else if (tcg.src == GX_TG_BINRM) {
      vtxXfrAttrs += fmt::format("\n    var tc{} = vec4f({}, 1.0);", i, nbt_slice_local(NbtSlice::B));
    } else if (tcg.src == GX_TG_TANGENT) {
      vtxXfrAttrs += fmt::format("\n    var tc{} = vec4f({}, 1.0);", i, nbt_slice_local(NbtSlice::T));
    } else if (tcg.src == GX_MAX_TEXGENSRC) {
      // The game leaves a texcoord generator's source register undefined when
      // it disables that generator, and only writes the ones it enables. This
      // value means "no source": the stage contributes nothing, so give it a
      // constant texcoord instead of aborting on the game's own idle state.
      // Logging already skips this value (see the dump above).
      vtxXfrAttrs += fmt::format("\n    var tc{} = vec4f(0.0, 0.0, 1.0, 1.0);", i);
    } else
      UNLIKELY FATAL("unhandled tcg src {}", underlying(tcg.src));
    if (tcg.type == GX_TG_MTX2x4 || tcg.type == GX_TG_MTX3x4) {
      if (info.indexAttr.test(GX_VA_TEX0MTXIDX + i)) {
        vtxXfrAttrs += fmt::format("\n    var tc{0}_tmp = tc{0} * ubuf.postex_mtx[in_texmtxidx{0} / 3u];", i);
      } else {
        // The matrix from the uniform (texcoord_scale[i].z, TexMtxIdentity for GX_IDENTITY), so the
        // pipeline does not key it (pipeline_unused_inputs).
        vtxXfrAttrs += fmt::format(
            "\n    var tc{0}_tmp = tc{0}.xyz;"
            "\n    let tc{0}_mtx = u32(ubuf.texcoord_scale[{0}].z);"
            "\n    if (tc{0}_mtx < {1}u) {{ tc{0}_tmp = tc{0} * ubuf.postex_mtx[tc{0}_mtx]; }}",
            i, TexMtxIdentity);
      }
      if (tcg.type == GX_TG_MTX2x4) {
        vtxXfrAttrs += fmt::format("\n    tc{0}_tmp.z = 1.0f;", i);
      }
    } else if (tcg.type == GX_TG_SRTG) {
      vtxXfrAttrs += fmt::format("\n    var tc{0}_tmp = vec3f(tc{0}.xy, 1.0f);", i);
    }
    if (tcg.normalize) {
      vtxXfrAttrs += fmt::format("\n    tc{0}_tmp = normalize(tc{0}_tmp);", i);
    }
    if (tcg.postMtx == GX_PTIDENTITY) {
      vtxXfrAttrs += fmt::format("\n    var tc{0}_proj = tc{0}_tmp;", i);
    } else {
      u32 postMtxIdx = (tcg.postMtx - GX_PTTEXMTX0) / 3;
      vtxXfrAttrs +=
          fmt::format("\n    var tc{0}_proj = vec4f(tc{0}_tmp.xyz, 1.0) * ubuf.postmtx[{1}];", i, postMtxIdx);
    }
    // Apply line/point tex offset
    if (config.lineMode == 3) {
      // GX_POINTS: offset S for right columns, T for bottom rows
      vtxXfrAttrs += fmt::format(
          "\n    if ((ubuf.line_texcoord_mask & (1u << {0})) != 0u) {{"
          "\n        if ((vidx & 1u) != 0u) {{ tc{0}_proj.x += ubuf.line_tex_offset; }}"
          "\n        if (vidx >= 2u) {{ tc{0}_proj.y += ubuf.line_tex_offset; }}"
          "\n    }}",
          i);
    } else if (config.lineMode != 0) {
      // GX_LINES / GX_LINESTRIP: offset one axis for perpendicular side
      vtxXfrAttrs += fmt::format(
          "\n    if ((ubuf.line_texcoord_mask & (1u << {0})) != 0u && (vidx & 1u) != 0u) {{"
          "\n        tc{0}_proj.y += ubuf.line_tex_offset;"
          "\n    }}",
          i);
    }
    if (tcg.type == GX_TG_MTX3x4) {
      vtxXfrAttrs += fmt::format("\n    out.tex{0}_uvw = tc{0}_proj.xyz;", i);
      fragmentFnPre += fmt::format("\n    var tex{0}_uv = in.tex{0}_uvw.xy / in.tex{0}_uvw.z;", i);
    } else {
      vtxXfrAttrs += fmt::format("\n    out.tex{0}_uv = tc{0}_proj.xy;", i);
      fragmentFnPre += fmt::format("\n    var tex{0}_uv = in.tex{0}_uv.xy;", i);
    }
  }
  // Multiple TEV stages may reference the same indirect stage,
  // so we sample each indirect texture only once.
  const auto ind_scale = [](const GXIndTexScale s) -> std::string_view {
    switch (s) {
    case GX_ITS_1:
      return "1.0"sv;
    case GX_ITS_2:
      return "(1.0 / 2.0)"sv;
    case GX_ITS_4:
      return "(1.0 / 4.0)"sv;
    case GX_ITS_8:
      return "(1.0 / 8.0)"sv;
    case GX_ITS_16:
      return "(1.0 / 16.0)"sv;
    case GX_ITS_32:
      return "(1.0 / 32.0)"sv;
    case GX_ITS_64:
      return "(1.0 / 64.0)"sv;
    case GX_ITS_128:
      return "(1.0 / 128.0)"sv;
    case GX_ITS_256:
      return "(1.0 / 256.0)"sv;
    default:
      FATAL("unhandled indirect scale {}", underlying(s));
    }
  };
  for (int i = 0; i < info.usedIndStages.size(); ++i) {
    if (!info.usedIndStages.test(i)) {
      continue;
    }
    const auto& indStage = config.indStages[i];
    const u32 texCoordId = underlying(indStage.texCoordId);
    const u32 texMapId = underlying(indStage.texMapId);
    // GX applies the SU texture-coordinate scale before the indirect stage scale.
    // The shader carries normalized UVs, so convert that texel-space result back
    // into normalized coordinates for the indirect texture sample.
    const auto scaleExpr =
        fmt::format("tex{0}_uv * ubuf.texcoord_scale[{0}].xy * vec2f({1}, {2}) / ubuf.tex{3}_size_bias.xy", texCoordId,
                    ind_scale(indStage.scaleS), ind_scale(indStage.scaleT), texMapId);
    fragmentFnPre += fmt::format(
        "\n    // Indirect stage {0}"
        "\n    var t_IndTexCoord{0} = 255.0 * textureSampleBias(tex{1}, tex{1}_samp, {2}, "
        "ubuf.tex{1}_size_bias.z).abg;",
        i, texMapId, scaleExpr);
  }
  if (info.usedIndStages.any()) {
    fragmentFnPre += "\n    var t_TexCoord = vec2f(0.0);";
  }
  // PBR mode bit 8192 (8ce05ed0): the first stage reading map 0 samples at uv + (INDI.xy - 0.5) * INDS,
  // INDI being map 1 on its own texcoord. The bit is read at run time from the material.
  int indBase = -1, indMr = -1;
  if (config.pbr) {
    const bool indShadowed = config.tevStageCount > 2 && config.tevStages[0].channelId == GX_COLOR1A1;
    for (int i = indShadowed ? 1 : 0; i < config.tevStageCount; ++i) {
      const auto& stage = config.tevStages[i];
      const u32 map = underlying(stage.texMapId);
      if (map < 2 && uses_texture_sample(stage) && stage.texCoordId != GX_TEXCOORD_NULL &&
          stage.indTexMtxId == GX_ITM_OFF) {
        int& slot = map == 0 ? indBase : indMr;
        slot = slot == -1 ? i : slot;
      }
    }
  }
  for (int i = 0; i < config.tevStageCount; ++i) {
    const auto& stage = config.tevStages[i];
    const bool needsIndirectCoord = stage.indTexMtxId != GX_ITM_OFF;
    const bool hasIndirectStage = stage.indTexStage < config.numIndStages;
    const bool needsTevTexCoord =
        needsIndirectCoord || stage.indTexWrapS != GX_ITW_OFF || stage.indTexWrapT != GX_ITW_OFF || stage.indTexAddPrev;
    const bool needsTextureSample = uses_texture_sample(stage);
    if (!needsTevTexCoord && !needsTextureSample) {
      continue;
    }
    const bool hasBaseTexCoord = stage.texCoordId != GX_TEXCOORD_NULL;
    const bool hasBaseTexture = stage.texMapId != GX_TEXMAP_NULL;
    const bool hasBaseCoord = hasBaseTexCoord && hasBaseTexture;
    std::string uvIn;
    if (needsTevTexCoord) {
      fragmentFnPre += fmt::format("\n    // TEV stage {} indirect", i);

      // Apply indirect texture matrix (produces a texel-space offset)
      std::string indirectOffsetTexel;
      if (needsIndirectCoord && hasIndirectStage) {
        std::string_view fmtShift;
        switch (stage.indTexFormat) {
        case GX_ITF_8:
          break;
        case GX_ITF_5:
          fmtShift = " / 8.0"sv;
          break;
        case GX_ITF_4:
          fmtShift = " / 16.0"sv;
          break;
        case GX_ITF_3:
          fmtShift = " / 32.0"sv;
          break;
        default:
          FATAL("unhandled indirect format {}", underlying(stage.indTexFormat));
        }
        if (fmtShift.empty()) {
          fragmentFnPre += fmt::format("\n    var ind{0}_coord = t_IndTexCoord{1};", i, underlying(stage.indTexStage));
        } else {
          fragmentFnPre += fmt::format("\n    var ind{0}_coord = floor(t_IndTexCoord{1}{2});", i,
                                       underlying(stage.indTexStage), fmtShift);
        }

        if (stage.indTexBiasSel != GX_ITB_NONE) {
          auto bias = stage.indTexFormat == GX_ITF_8 ? "-128.0"sv : "1.0"sv;
          auto biasS = "0.0"sv, biasT = "0.0"sv, biasU = "0.0"sv;
          if (stage.indTexBiasSel == GX_ITB_S || stage.indTexBiasSel == GX_ITB_ST || stage.indTexBiasSel == GX_ITB_SU ||
              stage.indTexBiasSel == GX_ITB_STU) {
            biasS = "1.0"sv;
          }
          if (stage.indTexBiasSel == GX_ITB_T || stage.indTexBiasSel == GX_ITB_ST || stage.indTexBiasSel == GX_ITB_TU ||
              stage.indTexBiasSel == GX_ITB_STU) {
            biasT = "1.0"sv;
          }
          if (stage.indTexBiasSel == GX_ITB_U || stage.indTexBiasSel == GX_ITB_SU || stage.indTexBiasSel == GX_ITB_TU ||
              stage.indTexBiasSel == GX_ITB_STU) {
            biasU = "1.0"sv;
          }
          fragmentFnPre += fmt::format("\n    ind{0}_coord = ind{0}_coord + vec3f({1}, {2}, {3}) * {4};", i, biasS,
                                       biasT, biasU, bias);
        }

        if (stage.indTexMtxId >= GX_ITM_0 && stage.indTexMtxId <= GX_ITM_2) {
          // Static 2x3 matrix: dot(mat_row, vec3(S,T,U)) * scale
          u32 mtxIdx = stage.indTexMtxId - GX_ITM_0;
          fragmentFnPre += fmt::format(
              "\n    let ind{0}_c0 = ubuf.ind_mtx[{1}][0];"
              "\n    let ind{0}_c1 = ubuf.ind_mtx[{1}][1];",
              i, mtxIdx);
          indirectOffsetTexel = fmt::format(
              "vec2f("
              "dot(vec3f(ind{0}_c0.xz, ind{0}_c1.x), ind{0}_coord), "
              "dot(vec3f(ind{0}_c0.yw, ind{0}_c1.y), ind{0}_coord)"
              ") * ind{0}_c1.z",
              i);
        } else if (stage.indTexMtxId >= GX_ITM_S0 && stage.indTexMtxId <= GX_ITM_S2 && hasBaseCoord) {
          // Dynamic S: result = scaled texcoord * ind_coord.x * scale / 256
          u32 mtxIdx = stage.indTexMtxId - GX_ITM_S0;
          u32 regTexCoord = underlying(stage.texCoordId);
          indirectOffsetTexel = fmt::format(
              "tex{1}_uv * ubuf.texcoord_scale[{1}].xy * ind{0}_coord.x"
              " * ubuf.ind_mtx[{2}][1][2] / 256.0",
              i, regTexCoord, mtxIdx);
        } else if (stage.indTexMtxId >= GX_ITM_T0 && stage.indTexMtxId <= GX_ITM_T2 && hasBaseCoord) {
          // Dynamic T: result = scaled texcoord * ind_coord.y * scale / 256
          u32 mtxIdx = stage.indTexMtxId - GX_ITM_T0;
          u32 regTexCoord = underlying(stage.texCoordId);
          indirectOffsetTexel = fmt::format(
              "tex{1}_uv * ubuf.texcoord_scale[{1}].xy * ind{0}_coord.y"
              " * ubuf.ind_mtx[{2}][1][2] / 256.0",
              i, regTexCoord, mtxIdx);
        }
      }

      // Don't convert to/from texel space if we can avoid it
      const bool useSimpleCoords = stage.indTexMtxId == GX_ITM_OFF && !stage.indTexAddPrev;

      // Wrap base coord and combine with the indirect translation.
      auto wrap_comp = [](GXIndTexWrap wrap, std::string&& coord) -> std::string {
        switch (wrap) {
        case GX_ITW_OFF:
          return std::move(coord);
        case GX_ITW_256:
          return fmt::format("({} % 256.0)", coord);
        case GX_ITW_128:
          return fmt::format("({} % 128.0)", coord);
        case GX_ITW_64:
          return fmt::format("({} % 64.0)", coord);
        case GX_ITW_32:
          return fmt::format("({} % 32.0)", coord);
        case GX_ITW_16:
          return fmt::format("({} % 16.0)", coord);
        case GX_ITW_0:
          return "0.0";
        default:
          FATAL("unhandled indirect wrap {}", underlying(wrap));
        }
      };
      std::string baseCoordExpr;
      if (hasBaseCoord) {
        u32 texCoordId = underlying(stage.texCoordId);
        if (useSimpleCoords) {
          baseCoordExpr = fmt::format("tex{}_uv", texCoordId);
        } else {
          fragmentFnPre +=
              fmt::format("\n    var ind{0}_texel = tex{1}_uv * ubuf.texcoord_scale[{1}].xy;", i, texCoordId);
          baseCoordExpr = fmt::format("ind{}_texel", i);
        }
      }
      std::string wrappedExpr = baseCoordExpr;
      if (!baseCoordExpr.empty() && (stage.indTexWrapS != GX_ITW_OFF || stage.indTexWrapT != GX_ITW_OFF)) {
        wrappedExpr = fmt::format("vec2f({}, {})", wrap_comp(stage.indTexWrapS, fmt::format("{}.x", baseCoordExpr)),
                                  wrap_comp(stage.indTexWrapT, fmt::format("{}.y", baseCoordExpr)));
      }

      std::string finalCoord;
      if (!wrappedExpr.empty() && !indirectOffsetTexel.empty()) {
        finalCoord = fmt::format("{} + ({})", wrappedExpr, indirectOffsetTexel);
      } else if (!wrappedExpr.empty()) {
        finalCoord = wrappedExpr;
      } else {
        finalCoord = indirectOffsetTexel;
      }

      if (info.usedIndStages.any() && !finalCoord.empty()) {
        if (stage.indTexAddPrev) {
          fragmentFnPre += fmt::format("\n    t_TexCoord += {};", finalCoord);
        } else {
          fragmentFnPre += fmt::format("\n    t_TexCoord = {};", finalCoord);
        }

        if (needsTextureSample && hasBaseTexture) {
          u32 texMapId = underlying(stage.texMapId);
          if (useSimpleCoords) {
            fragmentFnPre += fmt::format("\n    var ind{0}_uv = t_TexCoord;", i);
          } else {
            fragmentFnPre += fmt::format("\n    var ind{0}_uv = t_TexCoord / ubuf.tex{1}_size_bias.xy;", i, texMapId);
          }
          uvIn = fmt::format("ind{0}_uv", i);
        }
      }
    }
    if (!needsTextureSample) {
      continue;
    }

    CHECK(stage.texMapId != GX_TEXMAP_NULL, "unmapped texture for stage {}", i);
    CHECK(stage.texCoordId != GX_TEXCOORD_NULL, "unmapped texcoord for stage {}", i);
    if (uvIn.empty()) {
      // No indirect texturing
      uvIn = fmt::format("tex{0}_uv", underlying(stage.texCoordId));
    }
    if (i == indBase && indMr != -1) {
      const auto& ms = config.tevStages[indMr];
      fragmentFnPre += fmt::format(
          "\n    var ind_off = vec2f(0.0);"
          "\n    if ((u32(ubuf.pbr_backlight.w) & 8192u) != 0u) {{"
          "\n        let ind_s = textureSampleBias(tex{0}, tex{0}_samp, tex{1}_uv, ubuf.tex{0}_size_bias.z);"
          "\n        ind_off = (ind_s.xy - vec2f(0.5)) * ubuf.pbr_backlight.x;"
          "\n    }}",
          underlying(ms.texMapId), underlying(ms.texCoordId));
      uvIn = fmt::format("({} + ind_off)", uvIn);
    }
    fragmentFnPre +=
        fmt::format("\n    var sampled{0} = textureSampleBias(tex{1}, tex{1}_samp, {2}, ubuf.tex{1}_size_bias.z);", i,
                    underlying(stage.texMapId), uvIn);
    if (config.sdf != 0) {
      // GX_AURORA_SET_SDF: the sample is a distance, made coverage a screen pixel wide.
      fragmentFnPre += fmt::format("\n    {{"
                                   "\n        let sdf_d = sampled{0}.r;"
                                   "\n        let sdf_w = max(fwidth(sdf_d), 0.0001);"
                                   "\n        sampled{0} = vec4f(vec3f(clamp((sdf_d - 0.5) / sdf_w + 0.5, 0.0, 1.0)),"
                                   "\n            clamp((sdf_d - {1:.6f}) / sdf_w + 0.5, 0.0, 1.0));"
                                   "\n    }}",
                                   i, float(config.sdf) / 255.f);
    }
    if (config.hudSample == 1) {
      // GX_AURORA_SET_HUD_SAMPLE: Remastered's UI shaders square the filtered alpha.
      fragmentFnPre += fmt::format("\n    sampled{0}.a = sampled{0}.a * sampled{0}.a;", i);
    } else if (config.hudSample >= 2) {
      // Remastered's UI_Interference (ad2c208c): DYIN.x scales a per-row jitter of the three
      // channels' vertical sample position, DYIN.y seeds the rows' hash; DYIN.x == 0 is the
      // plain picture. The picture is hudRows tall (CCH5.x).
      static constexpr float kRows[5] = {414.476f, 195.048f, 100.f, 64.f, 128.f};
      const float rows = kRows[std::min<int>(config.hudSample - 2, 4)];
      fragmentFnPre += fmt::format(
          "\n    {{"
          "\n        let hi_uv = {1};"
          "\n        let hi_amp = ubuf.hud_dyin.x / {2:.6f};"
          "\n        var hi_s = 1.0;"
          "\n        if (hi_uv.x < 0.1) {{ hi_s = 0.0; }}"
          "\n        else if (hi_uv.x < 0.2) {{ hi_s = 0.1; }}"
          "\n        else if (hi_uv.x < 0.3) {{ hi_s = 0.2; }}"
          "\n        else if (hi_uv.x < 0.6) {{ hi_s = 0.3; }}"
          "\n        else if (hi_uv.x < 0.65) {{ hi_s = 0.6; }}"
          "\n        else if (hi_uv.x < 0.8) {{ hi_s = 0.65; }}"
          "\n        else if (hi_uv.x < 0.9) {{ hi_s = 0.8; }}"
          "\n        let hi_seed = hi_s + ubuf.hud_dyin.y;"
          "\n        let hi_row = floor(hi_uv.y * {2:.6f});"
          "\n        let hi_h0 = fract(sin(hi_row * 78.233 + hi_seed * 12.9898) * 43758.5469);"
          "\n        let hi_h1 = fract(sin(hi_row * 78.233 + (hi_seed + 1.0) * 12.9898) * 43758.5469);"
          "\n        let hi_h2 = fract(sin(hi_row * 78.233 + (hi_seed + 2.0) * 12.9898) * 43758.5469);"
          "\n        let hi_r = textureSampleBias(tex{3}, tex{3}_samp, vec2f(hi_uv.x, hi_uv.y + hi_amp * -40.3 * (hi_h0 - 0.5)), ubuf.tex{3}_size_bias.z);"
          "\n        let hi_g = textureSampleBias(tex{3}, tex{3}_samp, vec2f(hi_uv.x, hi_uv.y + hi_amp * -80.0 * (hi_h1 - 0.5)), ubuf.tex{3}_size_bias.z);"
          "\n        let hi_b = textureSampleBias(tex{3}, tex{3}_samp, vec2f(hi_uv.x, hi_uv.y + hi_amp * 40.1 * (hi_h2 - 0.5)), ubuf.tex{3}_size_bias.z);"
          "\n        if (ubuf.hud_dyin.x != 0.0) {{"
          "\n            sampled{0} = vec4f(hi_r.r, hi_g.g, hi_b.b, (hi_r.a + hi_g.a + hi_b.a) / 3.0);"
          "\n        }}"
          "\n    }}",
          i, uvIn, rows, underlying(stage.texMapId));
    }
  }
  // Remastered's volumetric fog on the draws after its full-screen pass (between
  // GX_AURORA_PORT_VOLUMETRIC_FOG and its END): blended and additive draws are fogged per vertex,
  // as Remastered's transparent shaders do, and opaque ones per pixel, as the full-screen pass
  // does. A froxel texel is the in-scatter before the exposure (rgb) and the transmittance (a), at
  // slice sqrt((z - near) / (range - near)). Blended draws (and premultiplied ones, by their alpha)
  // take on the in-scatter; additive ones are only dimmed.
  bool pbrVolFog = false;
  bool pbrEmitted = false; // pbr_func wrote prev: in the HDR scene it is exposed light already
  std::string volFogSample;
  const auto volFogWeight = [&](std::string_view alpha) -> std::string {
    switch (config.volFog) {
    case VolFogAdditive:
      return "0.0";
    case VolFogPremultiplied:
      return std::string{alpha};
    default:
      return "1.0";
    }
  };
  if (info.usesVolFog) {
    const std::string_view fragDepth = UseReversedZ ? "(1.0 - in.pos.z)" : "in.pos.z";
    vtxOutAttrs += fmt::format("\n    @location({}) vf: vec4f,", vtxOutIdx++);
    if (config.volFog == VolFogOpaque) {
      vtxXfrAttrs += "\n    out.vf = vec4f(out.pos.xy, out.pos.w, -mv_pos.z);";
      volFogSample = fmt::format("vf_at(vec4f(in.vf.xy, 0.0, in.vf.z), vf_depth(in.vf.w, {}))", fragDepth);
    } else {
      vtxXfrAttrs += "\n    out.vf = vf_at(out.pos, -mv_pos.z);";
      volFogSample = "in.vf";
    }
    texBindings += fmt::format("\n@group(2) @binding({})\n"
                               "var vf_froxels: texture_3d<f32>;\n"
                               "@group(2) @binding({})\n"
                               "var vf_samp: sampler;",
                               kVolFogFroxelBinding, kVolFogSamplerBinding);
    // The tone curve, its inverse and the exposure, as volfog.cpp's full-screen pass has them.
    uniformPre += R"""(
fn vf_at(clip: vec4f, viewz: f32) -> vec4f {
    let ndc = clip.xy / clip.w;
    let span = ubuf.volfog.y - ubuf.volfog.x; // as the fog pass: 0029257's NaN-safe clamp
    let slice = sqrt(clamp(select((viewz - ubuf.volfog.x) / span, select(0.0, 1.0, viewz > ubuf.volfog.x), span == 0.0), 0.0, 1.0));
    return textureSampleLevel(vf_froxels, vf_samp, vec3f(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, slice), 0.0);
}

// The per-pixel (opaque) fog is the full-screen pass, which reads the depth buffer. Nearer than
// the world's depth range is the viewmodel; Remastered draws it in the range 0.0097656..0.0386719
// (construct_depth_range_from_flags 0xb40e20, flag bit 8) and the pass reads that as a full-range
// depth, so it takes the first froxel slices. volfog_tone[0].w is 1 - near / far.
fn vf_depth(viewz: f32, z: f32) -> f32 {
    if (z < ubuf.volfog.w) {
        let d = mix(0.0097656, 0.0386719, z / ubuf.volfog.w);
        return ubuf.volfog.x / (1.0 - d * ubuf.volfog_tone[0].w);
    }
    return viewz;
}

fn vf_tone(x: f32) -> f32 {
    if (x < ubuf.volfog_tone[1].z) {
        return ((ubuf.volfog_tone[0].x * x + ubuf.volfog_tone[0].y) * x + ubuf.volfog_tone[0].z) * x;
    }
    if (x < ubuf.volfog_tone[1].w) {
        return ubuf.volfog_tone[1].x * x + ubuf.volfog_tone[1].y;
    }
    let st = max(ubuf.volfog_tone[2].y * x + ubuf.volfog_tone[2].z, 0.0);
    return ubuf.volfog_tone[2].x * st / (1.0 + st) + ubuf.volfog_tone[2].w;
}

fn vf_untone(y: f32) -> f32 {
    let mid = ubuf.volfog_tone[1].z;
    let lineStart = ubuf.volfog_tone[1].x * mid + ubuf.volfog_tone[1].y;
    if (y < lineStart) {
        var x = y / max(lineStart, 1e-4) * mid;
        for (var n = 0; n < 4; n++) {
            let slope = (3.0 * ubuf.volfog_tone[0].x * x + 2.0 * ubuf.volfog_tone[0].y) * x + ubuf.volfog_tone[0].z;
            x = clamp(x - (vf_tone(x) - y) / max(slope, 1e-4), 0.0, mid);
        }
        return x;
    }
    let top = ubuf.volfog_tone[2].w;
    if (y < top || ubuf.volfog_tone[2].y <= 0.0) {
        return (y - ubuf.volfog_tone[1].y) / ubuf.volfog_tone[1].x;
    }
    let u = min((y - top) / max(ubuf.volfog_tone[2].x, 1e-4), 0.999);
    return u / (1.0 - u) / ubuf.volfog_tone[2].y + ubuf.volfog_tone[1].w;
}

// The EFB holds colour as Remastered's sRGB swapchain does: the exact piecewise sRGB curve.
fn vf_srgb_enc(c: vec3f) -> vec3f {
  let l = clamp(c, vec3f(0.0), vec3f(1.0));
  return select(1.055 * pow(l, vec3f(1.0 / 2.4)) - 0.055, 12.92 * l, l <= vec3f(0.0031308));
}

fn vf_srgb_dec(c: vec3f) -> vec3f {
  let e = clamp(c, vec3f(0.0), vec3f(1.0));
  return select(pow((e + 0.055) / 1.055, vec3f(2.4)), e / 12.92, e <= vec3f(0.04045));
}

fn vf_exposed(c: vec3f) -> vec3f {
    let y = vf_srgb_dec(c);
    return min(vec3f(vf_untone(y.r), vf_untone(y.g), vf_untone(y.b)), vec3f(4.0));
}

// A display-referred colour fogged in the light: back through the tone curve, times the
// transmittance plus w of the in-scatter, and on through the curve again.
fn vf_apply(c: vec4f, fog: vec4f, w: f32) -> vec4f {
    let light = max(fog.rgb, vec3f(0.0)) * ubuf.volfog.z * w;
    if (fog.a > 0.9995 && max(max(light.r, light.g), light.b) < 1e-4) {
        return c;
    }
    let x = vf_exposed(c.rgb) * fog.a + light;
    let drawn = vec3f(vf_tone(x.r), vf_tone(x.g), vf_tone(x.b));
    return vec4f(vf_srgb_enc(drawn), c.a);
})""";
    if (config.sceneHdr) {
      uniformPre += R"""(

// The HDR scene's fog: c is exposed linear light already, so it is fogged as it is (transmittance
// plus w of the in-scatter) and left as light; the post-tone after the scene draws it.
fn vf_apply_hdr(c: vec4f, fog: vec4f, w: f32) -> vec4f {
    let light = max(fog.rgb, vec3f(0.0)) * ubuf.volfog.z * w;
    if (fog.a > 0.9995 && max(max(light.r, light.g), light.b) < 1e-4) {
        return c;
    }
    return vec4f(c.rgb * fog.a + light, c.a);
})""";
    }
  }
  if (config.pbr) {
    uniBufAttrs += "\n    pbr_probe: mat3x4f,";
    uniBufAttrs += "\n    pbr_emissive: vec4f,";
    uniBufAttrs += "\n    pbr_backlight: vec4f,";
    uniBufAttrs += "\n    pbr_layer: vec4f,";
    uniBufAttrs += "\n    pbr_layer_height: vec4f,";
    uniBufAttrs += "\n    pbr_param: vec4f,";
    uniBufAttrs += "\n    pbr_up: vec4f,";
    uniBufAttrs += "\n    pbr_cube: vec4f,";
    uniBufAttrs += "\n    pbr_ambient: array<vec4f, 6>,";
    uniBufAttrs += "\n    pbr_volume: array<vec4f, 6>,";
    uniBufAttrs += "\n    pbr_tone: array<vec4f, 3>,";
    uniBufAttrs += "\n    pbr_bklight: array<vec4f, 3>,";
    uniBufAttrs += "\n    pbr_light_skip: vec4f,";
    uniBufAttrs += "\n    pbr_light_scale: vec4f,";
    uniBufAttrs += fmt::format("\n    pbr_light_color: array<vec4f, {}>,", GX::MaxLights);
    uniBufAttrs += fmt::format("\n    pbr_light_hdr: array<vec4f, {}>,", GX::MaxLights * 3);
    uniBufAttrs += "\n    pbr_shield: array<vec4f, 8>,";
    uniBufAttrs += "\n    pbr_lmap_rect: vec4f,";
    uniBufAttrs += "\n    pbr_lmap_axes: array<vec4f, 3>,";
    uniBufAttrs += "\n    pbr_room_lights: vec4f,";
    uniBufAttrs += "\n    pbr_model_diffuse: vec4f,";
    uniBufAttrs += "\n    pbr_model_incan: vec4f,";
    uniBufAttrs += "\n    pbr_model_coverage: vec4f,";
    auto pbr = pbr_func(config, info, vtxOutAttrs, vtxXfrAttrs, vtxOutIdx, vidxAttr);
    // An unlit surface (baked room light: pbr_func drops its light loop) has no room light to
    // shadow, but takes the sun's own colour.
    if (!pbr.empty() && info.shadowReceive) {
      // The sun (GXPortSetShadowFrame) through its cascaded shadow map, as Remastered's receivers
      // (KB material/bfb300b6.md sec 6): the cascade picked by view depth dithered by up to 2.5 on a
      // 4x4 Bayer pattern, a constant depth bias, 3x3 tent PCF, and faded out over the last cascade's
      // last fifth; before a map is drawn it is lit. It shadows the room's own directional light
      // along its direction (CGraphics::LoadLight puts one at -dir * 2^20 in view space) and adds its
      // own colour, if any, as one more light.
      const auto put = [&](std::string_view what, std::string_view with) {
        const size_t at = pbr.find(what);
        if (at == std::string::npos) {
          Log.fatal("shadow: pbr_func has no '{}'", what);
        }
        pbr.replace(at, what.size(), with);
      };
      put("// pbr-sun-vis", R"""(var sun_vis = 1.0;
      {
          var bayer = array<f32, 16>(16.0, 144.0, 48.0, 176.0, 208.0, 80.0, 240.0, 112.0,
                                     64.0, 192.0, 32.0, 160.0, 255.0, 128.0, 224.0, 96.0);
          let fc = vec2u(in.pos.xy) & vec2u(3u);
          let depth = -in.pbr_pos.z;
          let dz = depth + 2.5 * (bayer[fc.y * 4u + fc.x] / 255.0);
          let c = min(dot(select(vec4u(0u), vec4u(1u), vec4f(dz) > ubuf.shadow_splits), vec4u(1u)), 3u);
          let sp = vec4f(in.pbr_pos, 1.0) * ubuf.shadow_recv[c];
          let suv = vec2f(sp.x * 0.5 + 0.5, 0.5 - sp.y * 0.5);
          let sref = saturate(sp.z - 0.00025);
          let st = ubuf.shadow_color.w;
          var sum = 0.0;
          for (var sy = -1; sy <= 1; sy++) {
              for (var sx = -1; sx <= 1; sx++) {
                  let w = f32((2 - abs(sx)) * (2 - abs(sy))) / 16.0;
                  sum += w * textureSampleCompareLevel(shadow_map, shadow_samp, suv + vec2f(f32(sx), f32(sy)) * st,
                                                      c, sref);
              }
          }
          let f = saturate((depth - ubuf.shadow_fade.x) / (ubuf.shadow_fade.y - ubuf.shadow_fade.x));
          sun_vis = max(sum, f * f * (3.0 - 2.0 * f));
      })""");
      if (pbr.find("// pbr-sun-view") != std::string::npos) {
        put("// pbr-sun-view", "pbr_dc = vec3f(sun_vis);");
      }
      if (pbr.find("// pbr-sun-light") != std::string::npos) {
        put("// pbr-sun-light", R"""(if (dist > 1e5 && dot(ldir, ubuf.shadow_dir.xyz) > 0.995) {
              rad *= sun_vis;
          })""");
      }
      put("// pbr-sun", R"""(if (any(ubuf.shadow_color.rgb > vec3f(0.0))) {
          let ldir = ubuf.shadow_dir.xyz;
          let nl = max(dot(pbr_n, ldir), 0.0);
          let h = normalize(ldir + pbr_v);
          let nh = max(dot(pbr_n, h), 0.0);
          let vh = max(dot(pbr_v, h), 0.0);
          let dd = nh * nh * (pbr_a2 - 1.0) + 1.0;
          let d = pbr_a2 / max(pbr_pi * dd * dd, 1e-6 * pbr_a2);
          let vis = 0.25 / (max(pbr_nv * (1.0 - pbr_k) + pbr_k, 1e-4) * max(nl * (1.0 - pbr_k) + pbr_k, 1e-4));
          let f = pbr_f0 + (1.0 - pbr_f0) * pow(1.0 - vh, 5.0);
          let rad = ubuf.shadow_color.rgb;
          pbr_lo += (pbr_diff * pbr_ao + select(d * vis * f * pbr_pi, vec3f(0.0), pbr_nols)) * rad * (nl * sun_vis);
          pbr_ldiff += pbr_diff * pbr_ao * rad * (nl * sun_vis);
          let env_w = 0.5 + 0.5 * dot(pbr_refl, ldir);
          pbr_env += rad * (env_w * env_w);
          pbr_lsum += rad;
          pbr_lnl += rad * (nl * sun_vis);
      })""");
      texBindings += fmt::format("\n@group(2) @binding({})\n"
                                 "var shadow_map: texture_depth_2d_array;\n"
                                 "@group(2) @binding({})\n"
                                 "var shadow_samp: sampler_comparison;",
                                 kShadowMapBinding, kShadowSamplerBinding);
    }
    if (!pbr.empty() && info.usesVolFog) {
      // The volumetric fog in the light before the tone curve, as Remastered's shaders fog it.
      // The pass-through light is already display-referred, so it is only dimmed.
      const std::string_view what = "let pbr_out = max(pbr_lo + pbr_glow, vec3f(0.0));";
      const size_t at = find_anchor(pbr, what);
      pbr.replace(at, what.size(),
                  fmt::format("var pbr_out = max(pbr_lo + pbr_glow, vec3f(0.0));"
                              "\n      let pbr_vf = {};"
                              "\n      pbr_out = pbr_out * pbr_vf.a + max(pbr_vf.rgb, vec3f(0.0)) * ubuf.volfog.z * {};"
                              "\n      pbr_pass *= pbr_vf.a;",
                              volFogSample, volFogWeight("pbr_alpha")));
      pbrVolFog = true;
    }
    if (!pbr.empty()) {
      pbrEmitted = true;
      fragmentFn += pbr;
      texBindings += fmt::format("\n@group(2) @binding({})\n"
                                 "var pbr_cube: texture_cube<f32>;\n"
                                 "@group(2) @binding({})\n"
                                 "var pbr_cube_samp: sampler;",
                                 MaxTextures * 2, MaxTextures * 2 + 1);
      static constexpr std::array<const char*, gfx::probe::VolumeTextures> volumeNames{"mean", "lobe", "r", "g", "b"};
      for (u32 i = 0; i < gfx::probe::VolumeTextures; ++i) {
        texBindings += fmt::format("\n@group(2) @binding({})\nvar pbr_vol_{}: texture_3d<f32>;",
                                   MaxTextures * 2 + 2 + i, volumeNames[i]);
      }
      texBindings += fmt::format("\n@group(2) @binding({})\nvar pbr_brdf_lut: texture_2d<f32>;", kBrdfLutBinding);
      if (config.pbrLightmapAttr != GX_VA_NULL) {
        texBindings +=
            fmt::format("\n@group(2) @binding({})\nvar pbr_lmap: texture_2d_array<f32>;", kLightmapBinding);
      }
    }
  }
  if (info.usesHdrTone) {
    uniBufAttrs += "\n    hdr_tone: array<vec4f, 3>,";
    uniBufAttrs += "\n    hdr_tev_blend: vec4f, // x: SRCALPHA/ONE or SRCALPHA/INVSRCALPHA, y: dst factor mode";
  }
  // The tone rows a display colour goes back through into the HDR scene.
  const std::string_view hdrTone = config.pbr ? "ubuf.pbr_tone[0], ubuf.pbr_tone[1], ubuf.pbr_tone[2]"
                                              : "ubuf.hdr_tone[0], ubuf.hdr_tone[1], ubuf.hdr_tone[2]";
  // Only non-PBR HDR has hdr_tev_blend. In particular, a PBR/SDF fallback must not read it.
  const auto hdrTevConvert = [&] {
    const std::string original = fmt::format("prev = vec4f(hdr_exposed(prev.rgb, {}), prev.a);", hdrTone);
    if (!info.usesHdrTone) {
      return std::string("\n    // HDR scene\n    ") + original;
    }
    return fmt::format(
        "\n    // HDR scene\n    if (ubuf.hdr_tev_blend.x > 0.5) {{\n"
        "        prev = vec4f(hdr_tev_srcalpha_add(prev.rgb, prev.a, {}), prev.a);\n"
        "    }} else if (ubuf.hdr_tev_blend.y > 0.5) {{\n"
        "        prev = vec4f(hdr_tev_dst_factor(prev.rgb, ubuf.hdr_tev_blend.y), prev.a);\n"
        "    }} else {{\n        {}\n    }}",
        hdrTone, original);
  };
  if (info.usesHudDyin) {
    uniBufAttrs += "\n    hud_dyin: vec4f,";
  }
  if (info.usesPTTexMtx.any()) {
    uniBufAttrs += fmt::format("\n    postmtx: array<mat3x4f, {}>,", MaxPTTexMtx);
  }
  if (info.usesFog) {
    uniformPre +=
        "\n"
        "struct Fog {\n"
        "    color: vec4f,\n"
        "    a: f32,\n"
        "    b: f32,\n"
        "    c: f32,\n"
        "    kind: u32,\n"
        "}";
    uniBufAttrs += "\n    fog: Fog,";

    const std::string_view fogDepth = UseReversedZ ? "(1.0 - in.pos.z)" : "in.pos.z";
    // The GXFogType from the uniform: one pipeline for every fog type, GX_FOG_NONE (0) included
    // (pipeline_fog).
    fragmentFn += fmt::format(
        "\n    if (ubuf.fog.kind != 0u) {{"
        "\n    var fogBase: f32;"
        "\n    if ((ubuf.fog.kind & 8u) != 0u) {{"
        "\n        fogBase = ubuf.fog.a * {0};"
        "\n    }} else {{"
        "\n        fogBase = ubuf.fog.a / (ubuf.fog.b - {0});"
        "\n    }}",
        fogDepth);
    if (config.fogRangeEnabled) {
      fragmentFn += "\n        fogBase *= bitcast<f32>(abuf[imm.fog_range_base + u32(in.pos.x)]);";
    }
    fragmentFn +=
        "\n    var fogF = clamp(fogBase - ubuf.fog.c, 0.0, 1.0);"
        "\n    var fogZ = fogF;" // GX_FOG_PERSP_LIN, GX_FOG_ORTHO_LIN
        "\n    switch ubuf.fog.kind & 7u {"
        "\n        case 4u: { fogZ = 1.0 - exp2(-8.0 * fogF); }"
        "\n        case 5u: { fogZ = 1.0 - exp2(-8.0 * fogF * fogF); }"
        "\n        case 6u: { fogZ = exp2(-8.0 * (1.0 - fogF)); }"
        "\n        case 7u: { fogZ = exp2(-8.0 * (1.0 - fogF) * (1.0 - fogF)); }"
        "\n        default: {}"
        "\n    }";
    if (config.sceneHdr && pbrEmitted) {
      // The PBR light is exposed linear here: the display fog colour goes back through the curve
      // (an approximation of fogging before the tone; the display-domain mix is not reproduced).
      fragmentFn += fmt::format(
          "\n    prev = vec4f(mix(prev.rgb, hdr_exposed(ubuf.fog.color.rgb, {}), clamp(fogZ, 0.0, 1.0)), prev.a);",
          hdrTone);
    } else {
      fragmentFn += "\n    prev = vec4f(mix(prev.rgb, ubuf.fog.color.rgb, clamp(fogZ, 0.0, 1.0)), prev.a);";
    }
    fragmentFn += "\n    }";
  }
  if (info.usesVolFog) {
    uniBufAttrs += "\n    volfog: vec4f,";
    uniBufAttrs += "\n    volfog_tone: array<vec4f, 3>,";
  }
  uniBufAttrs += fmt::format("\n    texcoord_scale: array<vec4f, {}>,", MaxTexCoord);
  if (info.usedIndTexMtxs.any()) {
    uniBufAttrs += fmt::format("\n    ind_mtx: array<mat2x4f, {}>,", MaxIndTexMtxs);
  }
  for (int i = 0; i < info.sampledTextures.size(); ++i) {
    if (!info.sampledTextures.test(i)) {
      continue;
    }
    uniBufAttrs += fmt::format("\n    tex{}_size_bias: vec4f,", i);
    texBindings += fmt::format(
        "\n@group(2) @binding({1})\n"
        "var tex{0}: texture_2d<f32>;\n"
        "@group(2) @binding({2})\n"
        "var tex{0}_samp: sampler;",
        i, i * 2, i * 2 + 1);
  }
  if (config.actorDecal != 0 && info.shadowReceive) {
    texBindings += fmt::format("\n@group(2) @binding({})\n"
                               "var shadow_map: texture_depth_2d_array;\n"
                               "@group(2) @binding({})\n"
                               "var shadow_samp: sampler_comparison;",
                               kShadowMapBinding, kShadowSamplerBinding);
  }
  if (info.usesShadow) {
    // GXState::shadowUniform (gfx/shadow.hpp's Uniform): view space to each cascade's map for this
    // frame's casters and for the maps the receivers read (the previous frame's), the sun's view-space
    // direction, its colour (w: one texel of the map in uv), the receivers' cascade far ends and fade.
    uniBufAttrs += "\n    shadow_caster: array<mat4x4f, 4>,";
    uniBufAttrs += "\n    shadow_recv: array<mat4x4f, 4>,";
    uniBufAttrs += "\n    shadow_dir: vec4f,";
    uniBufAttrs += "\n    shadow_color: vec4f,";
    uniBufAttrs += "\n    shadow_splits: vec4f,";
    uniBufAttrs += "\n    shadow_fade: vec4f,";
  }
  if (info.usesAlphaRef) {
    uniBufAttrs += "\n    alpha_ref: vec4u,"; // the alpha compare references (pipeline_alpha_ref)
  }
  // HDR PBR light above 1 is light, not a TEV byte overflow: only its alpha wraps.
  const bool hdrPbrColor = config.sceneHdr && pbrEmitted;
  if (!prevColorNormalized && !prevAlphaNormalized && !hdrPbrColor) {
    fragmentFn += "\n    prev = tev_overflow_vec4f(prev);";
  } else if (!prevColorNormalized && !hdrPbrColor) {
    fragmentFn += "\n    prev = vec4f(tev_overflow_vec3f(prev.rgb), prev.a);";
  } else if (!prevAlphaNormalized) {
    fragmentFn += "\n    prev.a = tev_overflow_f32(prev.a);";
  }
  bool hdrConverted = false;
  if (info.usesVolFog && !pbrVolFog && config.sceneHdr) {
    // The HDR scene fogs in the light it holds: the TEV colour (its display fog mixed in above) goes
    // into the exposed domain first, then the fog applies to it there, with no curve either side.
    // A PBR draw fogs its own light (pbrVolFog), so only TEV colour reaches here.
    fragmentFn += hdrTevConvert();
    hdrConverted = true;
    fragmentFn += fmt::format("\n    // Volumetric fog\n    prev = vf_apply_hdr(prev, {}, {});", volFogSample,
                              volFogWeight("clamp(prev.a, 0.0, 1.0)"));
  } else if (info.usesVolFog && !pbrVolFog) {
    fragmentFn += fmt::format("\n    // Volumetric fog\n    prev = vf_apply(prev, {}, {});", volFogSample,
                              volFogWeight("clamp(prev.a, 0.0, 1.0)"));
  }
  if (config.alphaCompare) {
    const auto discard = alpha_compare_discard(config);
    if (discard.constant == 1) {
      fragmentFn += "\n    // Alpha compare\n    discard;";
    } else if (discard.constant != 0) {
      fragmentFn +=
          "\n    // Alpha compare"
          "\n    let alphaCompare = u32(round(clamp(prev.a, 0.0, 1.0) * 255.0));";
      fragmentFn += fmt::format("\n    if ({}) {{ discard; }}", discard.expr);
    }
  }
  if (config.sceneHdr && !pbrEmitted && !hdrConverted) {
    // Retail TEV (and SDF) colour is display sRGB: decoded and taken back through the room's tone
    // curve. SRCALPHA/ONE weights display RGB first; blends where the source scales the destination
    // (multiply, add-dest-colour) take it as a linear factor; other blends keep the original inverse.
    // Alpha is preserved in both branches; hardware blending still operates on exposed light.
    fragmentFn += hdrTevConvert();
  }
  if (config.depthOnly != 0) {
    // Depth pre-pass: nothing is written but the depth, so the compiler can drop all the
    // shading that the alpha compare doesn't read.
    fragmentFn += "\n    prev = vec4f(0.0);";
  }
  if constexpr (EnableNormalVisualization) {
    fragmentFn += "\n    prev = vec4f(in.nrm, prev.a);";
  }

  auto shaderSource = fmt::format(R"""(
fn bswap32(v: u32, le: bool) -> u32 {{
  if (le) {{
    return v;
  }}
  return ((v & 0x000000FFu) << 24u) |
         ((v & 0x0000FF00u) << 8u) |
         ((v & 0x00FF0000u) >> 8u) |
         ((v & 0xFF000000u) >> 24u);
}}

fn bswap16(v: u32, le: bool) -> u32 {{
  return select(((v & 0xFFu) << 8u) | (v >> 8u), v, le);
}}

fn load_word(p: ptr<storage, array<u32>>, word_idx: u32) -> u32 {{
  // This guard is not expected to handle routine out-of-bounds accesses.
  // It appears to discourage some Adreno drivers/optimizers from storage buffer
  // optimizations that can cause visual artifacts, including vertex explosions
  // in Dusklight. (Swapped for a clamp on Adreno 7xx below.)
  if (word_idx < arrayLength(p)) {{
    return p[word_idx];
  }}
  return 0u;
}}

fn load_u8(p: ptr<storage, array<u32>>, byte_off: u32) -> u32 {{
  let word = load_word(p, byte_off / 4u);
  let shift = (byte_off & 3u) * 8u;
  return (word >> shift) & 0xFFu;
}}

fn load_u32_raw(p: ptr<storage, array<u32>>, byte_off: u32) -> u32 {{
  let word_idx = byte_off >> 2u;
  let sub = byte_off & 3u;
  let lo = load_word(p, word_idx);
  if (sub == 0u) {{
    return lo;
  }}
  let hi = load_word(p, word_idx + 1u);
  let shift = sub * 8u;
  return (lo >> shift) | (hi << (32u - shift));
}}

fn load_u16(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> u32 {{
  let word_idx = byte_off >> 2u;
  let sub = byte_off & 3u;
  let word = load_word(p, word_idx);
  if (sub <= 2u) {{
    return bswap16(extractBits(word, sub * 8u, 16u), le);
  }}
  let next = load_word(p, word_idx + 1u);
  let raw = extractBits(word, 24u, 8u) | (extractBits(next, 0u, 8u) << 8u);
  return bswap16(raw, le);
}}

fn load_u24(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> u32 {{
  let raw = load_u32_raw(p, byte_off) & 0x00FFFFFFu;
  if (le) {{
    return raw;
  }}
  return ((raw & 0x0000FFu) << 16u) |
         (raw & 0x00FF00u) |
         ((raw & 0xFF0000u) >> 16u);
}}

fn load_u32(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> u32 {{
  return bswap32(load_u32_raw(p, byte_off), le);
}}

fn load_f32(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> f32 {{
  return bitcast<f32>(load_u32(p, byte_off, le));
}}

fn raw_fetch_u8_1(p: ptr<storage, array<u32>>, byte_off: u32) -> u32 {{
  return load_u8(p, byte_off);
}}

fn raw_fetch_u8_2(p: ptr<storage, array<u32>>, byte_off: u32) -> vec2u {{
  let word_idx = byte_off >> 2u;
  let sub = byte_off & 3u;
  let word = load_word(p, word_idx);
  if (sub <= 2u) {{
    let shift = sub * 8u;
    return vec2u(
      extractBits(word, shift + 0u, 8u),
      extractBits(word, shift + 8u, 8u),
    );
  }}
  let next = load_word(p, word_idx + 1u);
  return vec2u(
    extractBits(word, 24u, 8u),
    extractBits(next, 0u, 8u),
  );
}}

fn raw_fetch_u8_3(p: ptr<storage, array<u32>>, byte_off: u32) -> vec3u {{
  let raw = load_u32_raw(p, byte_off);
  return vec3u(
    extractBits(raw, 0u, 8u),
    extractBits(raw, 8u, 8u),
    extractBits(raw, 16u, 8u),
  );
}}

fn raw_fetch_u8_4(p: ptr<storage, array<u32>>, byte_off: u32) -> vec4u {{
  let raw = load_u32_raw(p, byte_off);
  return vec4u(
    extractBits(raw, 0u, 8u),
    extractBits(raw, 8u, 8u),
    extractBits(raw, 16u, 8u),
    extractBits(raw, 24u, 8u),
  );
}}

fn raw_fetch_u16_1(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> u32 {{
  return load_u16(p, byte_off, le);
}}

fn raw_fetch_u16_2(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec2u {{
  return vec2u(
    load_u16(p, byte_off + 0u, le),
    load_u16(p, byte_off + 2u, le),
  );
}}

fn raw_fetch_u16_3(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec3u {{
  return vec3u(
    load_u16(p, byte_off + 0u, le),
    load_u16(p, byte_off + 2u, le),
    load_u16(p, byte_off + 4u, le),
  );
}}

fn raw_fetch_u16_4(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec4u {{
  return vec4u(
    load_u16(p, byte_off + 0u, le),
    load_u16(p, byte_off + 2u, le),
    load_u16(p, byte_off + 4u, le),
    load_u16(p, byte_off + 6u, le),
  );
}}

fn raw_fetch_f32_1(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> f32 {{
  return load_f32(p, byte_off, le);
}}

fn raw_fetch_f32_2(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec2f {{
  return vec2f(
    load_f32(p, byte_off + 0u, le),
    load_f32(p, byte_off + 4u, le),
  );
}}

fn raw_fetch_f32_3(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec3f {{
  return vec3f(
    load_f32(p, byte_off + 0u, le),
    load_f32(p, byte_off + 4u, le),
    load_f32(p, byte_off + 8u, le),
  );
}}

fn raw_fetch_f32_4(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec4f {{
  return vec4f(
    load_f32(p, byte_off + 0u, le),
    load_f32(p, byte_off + 4u, le),
    load_f32(p, byte_off + 8u, le),
    load_f32(p, byte_off + 12u, le),
  );
}}

fn fetch_u8_1(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> f32 {{
  let v = raw_fetch_u8_1(p, byte_off);
  return f32(v) / f32(1u << frac);
}}

fn fetch_s8_1(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> f32 {{
  let v = (bitcast<i32>(raw_fetch_u8_1(p, byte_off)) << 24) >> 24;
  return f32(v) / f32(1u << frac);
}}

fn fetch_u8_2(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> vec2f {{
  let v = raw_fetch_u8_2(p, byte_off);
  return vec2f(v) / f32(1u << frac);
}}

fn fetch_s8_2(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> vec2f {{
  let v = (bitcast<vec2i>(raw_fetch_u8_2(p, byte_off)) << vec2u(24u)) >> vec2u(24u);
  return vec2f(v) / f32(1u << frac);
}}

fn fetch_u8_3(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> vec3f {{
  let v = raw_fetch_u8_3(p, byte_off);
  return vec3f(v) / f32(1u << frac);
}}

fn fetch_s8_3(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> vec3f {{
  let v = (bitcast<vec3i>(raw_fetch_u8_3(p, byte_off)) << vec3u(24u)) >> vec3u(24u);
  return vec3f(v) / f32(1u << frac);
}}

fn fetch_u8_4(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> vec4f {{
  let v = raw_fetch_u8_4(p, byte_off);
  return vec4f(v) / f32(1u << frac);
}}

fn fetch_s8_4(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> vec4f {{
  let v = (bitcast<vec4i>(raw_fetch_u8_4(p, byte_off)) << vec4u(24u)) >> vec4u(24u);
  return vec4f(v) / f32(1u << frac);
}}

fn fetch_u16_1(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> f32 {{
  let v = raw_fetch_u16_1(p, byte_off, le);
  return f32(v) / f32(1u << frac);
}}

fn fetch_s16_1(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> f32 {{
  let v = bitcast<i32>(raw_fetch_u16_1(p, byte_off, le) << 16u) >> 16;
  return f32(v) / f32(1u << frac);
}}

fn fetch_u16_2(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> vec2f {{
  let v = raw_fetch_u16_2(p, byte_off, le);
  return vec2f(v) / f32(1u << frac);
}}

fn fetch_s16_2(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> vec2f {{
  let v = (bitcast<vec2i>(raw_fetch_u16_2(p, byte_off, le)) << vec2u(16u)) >> vec2u(16u);
  return vec2f(v) / f32(1u << frac);
}}

fn fetch_u16_3(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> vec3f {{
  let v = raw_fetch_u16_3(p, byte_off, le);
  return vec3f(v) / f32(1u << frac);
}}

fn fetch_s16_3(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> vec3f {{
  let v = (bitcast<vec3i>(raw_fetch_u16_3(p, byte_off, le)) << vec3u(16u)) >> vec3u(16u);
  return vec3f(v) / f32(1u << frac);
}}

fn fetch_u16_4(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> vec4f {{
  let v = raw_fetch_u16_4(p, byte_off, le);
  return vec4f(v) / f32(1u << frac);
}}

fn fetch_s16_4(p: ptr<storage, array<u32>>, byte_off: u32, frac: u32, le: bool) -> vec4f {{
  let v = (bitcast<vec4i>(raw_fetch_u16_4(p, byte_off, le)) << vec4u(16u)) >> vec4u(16u);
  return vec4f(v) / f32(1u << frac);
}}

fn fetch_f32_1(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> f32 {{
  return raw_fetch_f32_1(p, byte_off, le);
}}

fn fetch_f32_2(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec2f {{
  return raw_fetch_f32_2(p, byte_off, le);
}}

fn fetch_f32_3(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec3f {{
  return raw_fetch_f32_3(p, byte_off, le);
}}

fn fetch_f32_4(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec4f {{
  return raw_fetch_f32_4(p, byte_off, le);
}}

fn fetch_rgb565(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec4f {{
  let v = load_u16(p, byte_off, le);
  return vec4f(
    f32((v >> 11u) & 0x1Fu) / f32(0x1Fu),
    f32((v >>  5u) & 0x3Fu) / f32(0x3Fu),
    f32((v >>  0u) & 0x1Fu) / f32(0x1Fu),
    1.0,
  );
}}

fn fetch_rgb8(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec4f {{
  let v = raw_fetch_u8_3(p, byte_off);
  return vec4f(f32(v.x), f32(v.y), f32(v.z), 255.0) / 255.0;
}}

fn fetch_rgbx8(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec4f {{
  let v = raw_fetch_u8_4(p, byte_off);
  return vec4f(f32(v.x), f32(v.y), f32(v.z), 255.0) / 255.0;
}}

fn fetch_rgba4(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec4f {{
  let v = load_u16(p, byte_off, le);
  return vec4f(
    f32((v >> 12u) & 0x0Fu) / f32(0x0Fu),
    f32((v >>  8u) & 0x0Fu) / f32(0x0Fu),
    f32((v >>  4u) & 0x0Fu) / f32(0x0Fu),
    f32((v >>  0u) & 0x0Fu) / f32(0x0Fu),
  );
}}

fn fetch_rgba6(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec4f {{
  let v = load_u24(p, byte_off, le);
  return vec4f(
    f32((v >> 18u) & 0x3Fu) / f32(0x3Fu),
    f32((v >> 12u) & 0x3Fu) / f32(0x3Fu),
    f32((v >>  6u) & 0x3Fu) / f32(0x3Fu),
    f32((v >>  0u) & 0x3Fu) / f32(0x3Fu),
  );
}}

fn fetch_rgba8(p: ptr<storage, array<u32>>, byte_off: u32, le: bool) -> vec4f {{
  let v = raw_fetch_u8_4(p, byte_off);
  return vec4f(v) / 255.0;
}}

fn tev_overflow_f32(in: f32) -> f32 {{
  let byte_space = in * 255.0;
  return (byte_space - floor(byte_space / 256.0) * 256.0) / 255.0;
}}

fn tev_overflow_vec3f(in: vec3f) -> vec3f {{
  let byte_space = in * 255.0;
  return (byte_space - floor(byte_space / 256.0) * 256.0) / 255.0;
}}

fn tev_overflow_vec4f(in: vec4f) -> vec4f {{
  let byte_space = in * 255.0;
  return (byte_space - floor(byte_space / 256.0) * 256.0) / 255.0;
}}

// The EFB (and the probes captured from it) holds colour as Remastered's sRGB swapchain
// does: the exact piecewise sRGB curve.
fn srgb_dec(e: vec3f) -> vec3f {{
  let c = max(e, vec3f(0.0));
  return select(pow((c + 0.055) / 1.055, vec3f(2.4)), c / 12.92, c <= vec3f(0.04045));
}}

// Its inverse, clamped to the range the swapchain stores.
fn srgb_enc(l: vec3f) -> vec3f {{
  let c = clamp(l, vec3f(0.0), vec3f(1.0));
  return select(1.055 * pow(c, vec3f(1.0 / 2.4)) - 0.055, 12.92 * c, c <= vec3f(0.0031308));
}}

{8}

struct Immediate {{
    vtx_start: u32,
    current_pnmtx: u32,
    fog_range_base: u32,
    serial: u32,
    array_start0: vec4u,
    array_start1: vec4u,
    array_start2: vec4u,
}};
var<immediate> imm: Immediate;

struct Uniform {{
    render_viewport_size: vec2f,
    logical_viewport_size: vec2f,{0}
}};
@group(0) @binding(0)
var<storage, read> vbuf: array<u32>;
@group(0) @binding(1)
var<storage, read> abuf: array<u32>;
@group(1) @binding(0)
var<uniform> ubuf: Uniform;{1}

struct VertexOutput {{
    // GX multipass draws need identical depth across lighting/TEV shader variants.
    @builtin(position) @invariant pos: vec4f,{2}
}};

@vertex
fn vs_main(
    @builtin(vertex_index) vidx: u32{3}
) -> VertexOutput {{
    var out: VertexOutput;{7}{4}
    return out;
}}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {{{6}{5}
    return prev;
}}{9}
)""",
                                        uniBufAttrs, texBindings, vtxOutAttrs, vtxInAttrs, vtxXfrAttrs, fragmentFn,
                                        fragmentFnPre, vtxXfrAttrsPre, uniformPre, shadowVs);
  if (clamp_storage_loads() && !gfx::g_vertexFetchTexture) {
    constexpr std::string_view guard =
        "  if (word_idx < arrayLength(p)) {\n    return p[word_idx];\n  }\n  return 0u;\n";
    const size_t at = find_anchor(shaderSource, guard);
    shaderSource.replace(at, guard.size(), "  return p[min(word_idx, arrayLength(p) - 1u)];\n");
  }
  add_cut_caster(shaderSource, config);
  if (config.drawId) {
    // The draw serial as the colour, 8 bits a channel, whatever the shading came to (it can still discard).
    constexpr std::string_view tail = "    return prev;\n}";
    const size_t at = find_anchor(shaderSource, tail, true);
    shaderSource.replace(at, tail.size(),
                         "    return vec4f(f32(imm.serial & 255u), f32((imm.serial >> 8u) & 255u), "
                         "f32((imm.serial >> 16u) & 255u), 255.0) / 255.0;\n}");
  }
  if (config.actorDecal != 0 && config.lineMode == 0) {
    // The decal colour: 1 - (1 - vis) * opacity * (1 - sat(w))^2, vis one bilinear compare tap of the caster's
    // 256^2 depth map (white outside it). shadowUniform holds, as vec4s: 0-2 view -> map clip x/y/z, 3 height
    // row, 4-9 the receiver box planes, 10 (opacity, fade start height, 1/fade range, layer), 11 (up, normal
    // threshold). Vec4 k of the uniform is column k % 4 of matrix k / 4.
    constexpr std::string_view tail = "    return prev;\n}";
    const size_t at = find_anchor(shaderSource, tail, true);
    shaderSource.replace(at, tail.size(), R"WGSL(    let dc_dx = dpdx(in.dc_pos);
    let dc_dy = dpdy(in.dc_pos);
    var dc_n = normalize(cross(dc_dx, dc_dy));
    if (dot(dc_n, in.dc_pos) > 0.0) {
        dc_n = -dc_n;
    }
    let dc_p = vec4f(in.dc_pos, 1.0);
    let dc_up = ubuf.shadow_caster[2][3];
    for (var i = 0u; i < 6u; i++) {
        let k = 4u + i;
        if (dot(dc_p, ubuf.shadow_caster[k / 4u][k % 4u]) < 0.0) {
            discard;
        }
    }
    if (dot(dc_n, dc_up.xyz) < dc_up.w) {
        discard;
    }
    let dc_par = ubuf.shadow_caster[2][2];
    let dc_cx = dot(dc_p, ubuf.shadow_caster[0][0]);
    let dc_cy = dot(dc_p, ubuf.shadow_caster[0][1]);
    let dc_cz = dot(dc_p, ubuf.shadow_caster[0][2]);
    let dc_uv = vec2f(dc_cx * 0.5 + 0.5, 0.5 - dc_cy * 0.5);
    var dc_vis = 1.0;
    if (all(dc_uv >= vec2f(0.0)) && all(dc_uv <= vec2f(1.0))) {
        dc_vis = textureSampleCompareLevel(shadow_map, shadow_samp, dc_uv, i32(dc_par.w), saturate(dc_cz));
    }
    let dc_w = saturate((dot(dc_p, ubuf.shadow_caster[0][3]) - dc_par.y) * dc_par.z);
    let dc_f = 1.0 - dc_w;
    let dc_rgb = 1.0 - (1.0 - dc_vis) * dc_par.x * dc_f * dc_f;
    return vec4f(vec3f(dc_rgb), 1.0);
}
)WGSL");
  }
  if (config.sceneHdr) {
    shaderSource += gfx::hdr::ToneWgsl;
  }
  if (info.usesHdrTone) {
    shaderSource += gfx::hdr::TevBlendWgsl;
  }
  if (gfx::g_vertexFetchTexture) {
    apply_texture_vertex_fetch(shaderSource);
  }
  if (EnableDebugPrints) {
    Log.info("Generated shader (hash {:x}): {}", hash, shaderSource);
  }

  return shaderSource;
}

namespace {
// Shader debugging: a record of every config a module was built from (a few hundred), so a dump started late
// still has the modules built before it, and the override directory. Touched once per module, never per draw.
struct ShaderDebug {
  std::mutex mutex;
  bool envRead = false;
  std::string dumpDir;
  std::string overrideDir;
  absl::flat_hash_map<u64, ShaderConfig> configs;
  absl::flat_hash_set<u64> dumped;
};

ShaderDebug& shader_debug() {
  static auto* s = new ShaderDebug;
  return *s;
}

void read_shader_env(ShaderDebug& dbg) {
  if (dbg.envRead) {
    return;
  }
  dbg.envRead = true;
  if (const char* dir = std::getenv("MP2_WGSL_DUMP"); dir != nullptr && *dir != '\0') {
    dbg.dumpDir = dir;
  }
  if (const char* dir = std::getenv("MP2_WGSL_OVERRIDE"); dir != nullptr && *dir != '\0') {
    dbg.overrideDir = dir;
  }
}

std::string describe_config(u64 hash, const ShaderConfig& c) {
  std::string out = fmt::format(
      "// hash {:016x}\n"
      "// pbr {} ({}) pbrKind {} sdf {} depthOnly {} volFog {} drawId {}\n"
      "// fogType {} fogRange {} lineMode {} vtxStride {}\n"
      "// tevStages {} indStages {} alphaCompare {}/{}/{}/{}/{}\n"
      "// (the PBR debug view is a uniform, not part of the config)\n",
      hash, c.pbr, c.pbr == 0 ? "off" : c.pbr == 1 ? "full" : "cost test", c.pbrKind, c.sdf, c.depthOnly, c.volFog,
      static_cast<u32>(c.drawId), c.fogType, static_cast<u32>(c.fogRangeEnabled), static_cast<u32>(c.lineMode),
      c.vtxStride, c.tevStageCount, c.numIndStages, static_cast<u32>(c.alphaCompare.comp0), c.alphaCompare.ref0,
      static_cast<u32>(c.alphaCompare.op), static_cast<u32>(c.alphaCompare.comp1), c.alphaCompare.ref1);
  for (u32 i = 0; i < c.tevStageCount && i < c.tevStages.size(); ++i) {
    const auto& s = c.tevStages[i];
    out += fmt::format("// stage {}: texCoord {} texMap {} channel {}\n", i, static_cast<u32>(s.texCoordId),
                       static_cast<u32>(s.texMapId), static_cast<u32>(s.channelId));
  }
  return out;
}

// Writes <dir>/<hash16>.wgsl once per hash, and a line to <dir>/index.tsv. Caller holds the mutex.
bool write_shader_dump(ShaderDebug& dbg, u64 hash, const ShaderConfig& config, const std::string& source) {
  if (dbg.dumpDir.empty() || !dbg.dumped.insert(hash).second) {
    return false;
  }
  std::error_code ec;
  const std::filesystem::path dir{dbg.dumpDir};
  std::filesystem::create_directories(dir, ec);
  const auto name = fmt::format("{:016x}", hash);
  {
    std::ofstream file(dir / (name + ".wgsl"), std::ios::binary | std::ios::trunc);
    if (!file) {
      Log.warn("wgsl dump: cannot write {}", (dir / (name + ".wgsl")).string());
      return false;
    }
    file << describe_config(hash, config) << "\n" << source;
  }
  std::ofstream index(dir / "index.tsv", std::ios::app);
  const char* kind = config.depthOnly ? "depth" : config.pbr == 0 ? "gx" : config.pbr == 1 ? "pbr" : "pbr-cost";
  index << name << '\t' << kind << "\tpbrKind=" << static_cast<u32>(config.pbrKind)
        << " volFog=" << static_cast<u32>(config.volFog) << " tev=" << config.tevStageCount
        << " drawId=" << static_cast<u32>(config.drawId) << '\n';
  return true;
}

// Compiles `source`; null when Dawn rejected it (message in `error`).
wgpu::ShaderModule create_module_checked(const std::string& source, const char* label, std::string& error) {
  wgpu::ShaderSourceWGSL wgslDescriptor{};
  wgslDescriptor.code = source.c_str();
  const auto descriptor = wgpu::ShaderModuleDescriptor{
      .nextInChain = &wgslDescriptor,
      .label = label,
  };
  webgpu::g_device.PushErrorScope(wgpu::ErrorFilter::Validation);
  auto module = webgpu::g_device.CreateShaderModule(&descriptor);
  bool failed = false;
  const auto future = webgpu::g_device.PopErrorScope(
      wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView message) {
        if (type != wgpu::ErrorType::NoError) {
          failed = true;
          error = std::string{std::string_view{message}};
        }
      });
  webgpu::g_instance.WaitAny(future, 5000000000);
  return failed ? wgpu::ShaderModule{} : module;
}
} // namespace

u32 dump_shaders(const char* dir) noexcept {
  auto& dbg = shader_debug();
  std::scoped_lock guard{dbg.mutex};
  read_shader_env(dbg);
  dbg.dumpDir = dir != nullptr ? dir : "";
  dbg.dumped.clear();
  if (dbg.dumpDir.empty()) {
    return 0;
  }
  u32 count = 0;
  for (const auto& [hash, config] : dbg.configs) {
    // The sources of modules built before the dump are generated again: keeping all of them would cost memory
    // on every run.
    if (write_shader_dump(dbg, hash, config, build_shader_source(config))) {
      ++count;
    }
  }
  return count;
}

void set_shader_override_dir(const char* dir) noexcept {
  auto& dbg = shader_debug();
  std::scoped_lock guard{dbg.mutex};
  read_shader_env(dbg);
  dbg.overrideDir = dir != nullptr ? dir : "";
}

namespace {
std::atomic_bool g_drawShaderLog{false};
std::mutex g_drawShaderMutex;
// Serials are handed out in order, so the last few frames' fit in a ring.
constexpr u32 kDrawShaderRing = 1u << 16;
struct DrawShader {
  u32 serial = 0;
  u64 hash = 0;
};
std::vector<DrawShader> g_drawShaders;
} // namespace

void set_draw_shader_log(bool on) noexcept { g_drawShaderLog.store(on, std::memory_order_relaxed); }

void note_draw_shader(u32 serial, const ShaderConfig& config) noexcept {
  if (serial == 0 || !g_drawShaderLog.load(std::memory_order_relaxed)) {
    return;
  }
  const u64 hash = xxh3_hash(config);
  std::scoped_lock guard{g_drawShaderMutex};
  if (g_drawShaders.empty()) {
    g_drawShaders.resize(kDrawShaderRing);
  }
  g_drawShaders[serial % kDrawShaderRing] = {serial, hash};
}

u64 draw_shader_hash(u32 serial) noexcept {
  std::scoped_lock guard{g_drawShaderMutex};
  if (g_drawShaders.empty()) {
    return 0;
  }
  const DrawShader& entry = g_drawShaders[serial % kDrawShaderRing];
  return entry.serial == serial ? entry.hash : 0;
}

bool shader_overridden(u64 hash) noexcept {
  auto& dbg = shader_debug();
  std::string dir;
  {
    std::scoped_lock guard{dbg.mutex};
    read_shader_env(dbg);
    dir = dbg.overrideDir;
  }
  if (dir.empty()) {
    return false;
  }
  std::error_code error;
  return std::filesystem::exists(std::filesystem::path{dir} / fmt::format("{:016x}.wgsl", hash), error);
}

wgpu::ShaderModule build_shader(const ShaderConfig& config, DstAlphaMode& dstAlphaMode) noexcept {
  ZoneScoped;
  auto shaderSource = build_shader_source(config);
  if (!apply_dst_alpha_mode(shaderSource, dstAlphaMode)) {
    dstAlphaMode = DstAlphaMode::None;
  }
  // A dst alpha mode is its own module (a plain one keeps the config's hash, as dumps and overrides name it).
  const auto hash = dstAlphaMode == DstAlphaMode::None ? xxh3_hash(config) : xxh3_hash(dstAlphaMode, xxh3_hash(config));
  const auto label = fmt::format("GX Shader {:x}", hash);
  auto& dbg = shader_debug();
  std::string overrideDir;
  {
    std::scoped_lock guard{dbg.mutex};
    read_shader_env(dbg);
    dbg.configs.emplace(hash, config);
    write_shader_dump(dbg, hash, config, shaderSource);
    overrideDir = dbg.overrideDir;
  }
  if (!overrideDir.empty()) {
    const auto path = std::filesystem::path{overrideDir} / fmt::format("{:016x}.wgsl", static_cast<u64>(hash));
    std::ifstream file(path, std::ios::binary);
    if (file) {
      const std::string edited{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
      std::string error;
      if (dstAlphaMode == DstAlphaMode::DualSource && edited.find("@blend_src(1)") == std::string::npos) {
        // The pipeline blends with the second source, which this module wouldn't write.
        Log.error("wgsl override {:016x} has no @blend_src(1) output, using the generated source",
                  static_cast<u64>(hash));
      } else if (auto module = create_module_checked(edited, label.c_str(), error)) {
        Log.info("wgsl override {:016x}", static_cast<u64>(hash));
        return module;
      }
      Log.error("wgsl override {:016x} rejected, using the generated source: {}", static_cast<u64>(hash), error);
    }
  }
  wgpu::ShaderSourceWGSL wgslDescriptor{};
  wgslDescriptor.code = shaderSource.c_str();
  const auto shaderDescriptor = wgpu::ShaderModuleDescriptor{
      .nextInChain = &wgslDescriptor,
      .label = label.c_str(),
  };
  return webgpu::g_device.CreateShaderModule(&shaderDescriptor);
}

bool storage_load_clamp_active() noexcept { return clamp_storage_loads() && !gfx::g_vertexFetchTexture; }
} // namespace aurora::gx
