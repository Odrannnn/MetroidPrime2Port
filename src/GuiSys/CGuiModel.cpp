#include "GuiSys/CGuiModel.hpp"

#include "GuiSys/CGuiFrame.hpp"
#include "GuiSys/CGuiFrameModelDatabase.hpp"
#include "GuiSys/CGuiWidgetDrawParms.hpp"
#include "Kyoto/Graphics/CCubeMaterial.hpp"
#include "Kyoto/Graphics/CCubeModel.hpp"
#include "Kyoto/Graphics/CGX.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Graphics/CModelFlags.hpp"
#include "Kyoto/Streams/CInputStream.hpp"
#ifdef TARGET_PC
#include "Kyoto/Basics/CBasics.hpp"
#include "Kyoto/Graphics/CTexture.hpp"
#include "port_pointer.h"
#include "port_prompt_ring.h"
#endif
#ifdef TARGET_PC
#include <cstdlib>
#include <set>
#include <stdio.h>
#include <stdlib.h>

// Port: name every GUI model widget that draws, once per asset id. A button prompt is a model
// widget (`model_abutton` and friends), not one of the textures Prime 1's glyph table keys on, so
// this is how Echoes' prompt art is identified: run with MP2_DEBUG_PROMPTS=1 while a memo is up and
// read the ids it prints. Set<CAssetId::Value()> keeps it to one line per model.
static void PortLogGuiModel(CAssetId id) {
  static bool sEnabled = std::getenv("MP2_DEBUG_PROMPTS") != nullptr;
  static std::set< unsigned int > sSeen;
  if (sEnabled && sSeen.insert(id).second) {
    std::fprintf(stderr, "[guimodel] %08x draws\n", id);
  }
}
#endif

// Guessed names: shared rendering state for a frame's model-widget batch.
static int sDrawFlags = 7;
static bool sDrawing;
static CColor sDrawColor = CColor::Black();

void CGuiModel::UpdateDrawState(int drawFlags, CColor color) {
  if (sDrawFlags != drawFlags) {
    sDrawColor = color;
    sDrawFlags = drawFlags;
    CCubeMaterial::ResetTransparencyKColor();
    CCubeMaterial::ResetCachedMaterials();
  }

  if (!(color == sDrawColor)) {
    if (sDrawFlags == kGMDF_Alpha || sDrawFlags == kGMDF_Additive || drawFlags == kGMDF_Additive ||
        drawFlags == kGMDF_Alpha) {
      sDrawFlags = drawFlags;
      sDrawColor = color;
      CCubeMaterial::ResetTransparencyKColor();
      CCubeMaterial::ResetCachedMaterials();
    } else {
      const int kColor = CCubeMaterial::GetTransparencyKColor();
      if (kColor != -1) {
        CGX::SetTevKColor(static_cast< GXTevKColorID >(kColor), color.GetGXColor());
        sDrawColor = color;
      }
    }
  }
}

void CGuiModel::BeginDraw() {
  CCubeMaterial::ResetCachedMaterials();
  CCubeMaterial::ResetTransparencyKColor();
  UpdateDrawState(-1, CColor::Black());
  sDrawing = true;
}

void CGuiModel::EndDraw() {
#ifdef TARGET_PC
  // Port: the boxes' drawn button rings, over the frame's models (port_prompt_ring.h).
  if (PortPromptRing::EndBatch()) {
    UpdateDrawState(-1, CColor::Black());
  }
#endif
  sDrawing = false;
}

CGuiModel* CGuiModel::Create(CGuiFrame* frame, CInputStream& in, uint version) {
  const CGuiWidgetParms parms = ReadWidgetHeader(frame, in);
  const CAssetId modelId = in.Get< CAssetId >();
  const int modelIndex = in.Get< int >();
  const uint lightMask = in.Get< uint >();

  CGuiModel* model = rs_new CGuiModel(parms, modelId, lightMask, modelIndex);
  model->ParseBaseInfo(frame, in, parms, version);
  return model;
}

CGuiModel::CGuiModel(const CGuiWidgetParms& parms, CAssetId modelId, uint lightMask, int modelIndex)
: CGuiWidget(parms), mModelId(modelId), mModelIndex(modelIndex), mLightMask(lightMask) {}

CGuiModel::~CGuiModel() {}

void CGuiModel::Draw(const CGuiWidgetDrawParms& parms) const {
  CGraphics::SetModelMatrix(GetWorldTransform());
  if (GetIsVisible()) {
#ifdef TARGET_PC
    PortLogGuiModel(mModelId);
    // Port: where the model lands on screen, for mouse and touch menus.
    if (PortPointer::Enabled()) {
      CAABox box = CAABox::MakeNullBox();
      if (PortGetBounds(box)) {
        PortGuiHit::Record(this, CGraphics::GetModelMatrix(), box.GetMinPoint(), box.GetMaxPoint());
      }
    }
    // Port: the drawn button ring closes the box's notch around the button (port_prompt_ring.h).
    PortPromptRing::BeforeDraw(*this);
#endif
    if (!sDrawing) {
      CCubeMaterial::ResetCachedMaterials();
      CCubeMaterial::ResetTransparencyKColor();
      UpdateDrawState(-1, CColor::Black());
    }

    const CColor color = GetModifiedColor().WithAlphaModulatedBy(parms.GetAlpha());
    UpdateDrawState(mDrawFlags, color);
    GetParentFrame()->EnableLights(mLightMask);
    const bool cullChanged = mCullFaces;
    if (cullChanged) {
      CGraphics::SetCullMode(kCM_Front);
    }

    CModelFlags flags = CModelFlags::Normal();
    bool doDraw = true;
    switch (mDrawFlags) {
    case kGMDF_Shadeless:
      flags = CModelFlags::Normal();
      break;
    case kGMDF_Opaque:
      flags = CModelFlags::ColorModulate(color);
      break;
    case kGMDF_Alpha:
      flags = CModelFlags::AlphaBlended(color).DepthCompareUpdate(mDepthTest, mDepthWrite);
      flags = CModelFlags(flags, flags.GetOtherFlags() | CModelFlags::kF_Unknown400);
      break;
    case kGMDF_Additive:
      flags = CModelFlags::Additive(color).DepthCompareUpdate(mDepthTest, mDepthWrite);
      flags = CModelFlags(flags, flags.GetOtherFlags() | CModelFlags::kF_Unknown400);
      break;
    case kGMDF_AlphaAdditiveOverdraw:
      flags = CModelFlags::AlphaBlended(color).DepthCompareUpdate(mDepthTest, false);
      if (mDepthGreater) {
        flags = flags.DepthBackwards();
      }
      DrawModel(flags);
      flags = CModelFlags::AdditiveRGB(color).DepthCompareUpdate(mDepthTest, mDepthWrite);
      if (mDepthGreater) {
        flags = flags.DepthBackwards();
      }
      DrawModel(flags);
      doDraw = false;
      break;
    case kGMDF_ClearAlpha:
      CGX::SetAlphaUpdate(true);
      CGX::SetDstAlpha(true, 0);
      CGX::SetColorUpdate(true);
      CGX::SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_1_8);
      CGX::SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
      CGX::SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
      CGX::SetStandardTevColorAlphaOp(GX_TEVSTAGE0);
      CGX::SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
      CGX::SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, false,
                          GX_PTIDENTITY);
      CGX::SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
      CGX::SetNumTevStages(1);
      CGX::SetNumTexGens(1);
      CGX::SetNumChans(0);
      CGX::SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
      CGX::SetZMode(false, GX_ALWAYS, false);
      GetParentFrame()->GetModelDatabase()->GetModel(mModelIndex)->DrawFlat(kSS_All);
      CGX::SetAlphaUpdate(false);
      CGX::SetColorUpdate(true);
      doDraw = false;
      break;
    case kGMDF_DoubleColor:
      CGX::SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ONE);
      CGX::SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
      CGX::SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_1);
      CGX::SetStandardTevColorAlphaOp(GX_TEVSTAGE0);
      CGX::SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
      CGX::SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, false,
                          GX_PTIDENTITY);
      CGX::SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
      CGX::SetNumTevStages(1);
      CGX::SetNumTexGens(1);
      CGX::SetNumChans(0);
      CGX::SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_SRCCLR, GX_LO_CLEAR);
      CGX::SetZMode(false, GX_ALWAYS, false);
      GetParentFrame()->GetModelDatabase()->GetModel(mModelIndex)->DrawFlat(kSS_All);
      doDraw = false;
      break;
    default:
      doDraw = false;
      break;
    }

    if (doDraw) {
      if (mDepthGreater) {
        flags = flags.DepthBackwards();
      }
      DrawModel(flags);
    }

#ifdef TARGET_PC
    if (PortPromptRing::AfterDraw(*this, color)) {
      UpdateDrawState(-1, CColor::Black());
    }
#endif
    if (cullChanged) {
      CGraphics::SetCullMode(kCM_None);
    }
  }

  CGuiWidget::Draw(parms);
}

void CGuiModel::DrawModel(const CModelFlags& flags) const {
  GetParentFrame()->GetModelDatabase()->Draw(mModelIndex, flags);
}

#ifdef TARGET_PC
int CGuiModel::PortIconLayers(PortIconLayer* out, int max) const {
  const CGuiFrameModelDatabase* db = GetParentFrame()->GetModelDatabase();
  const CCubeModel* model = db != nullptr ? db->GetModel(mModelIndex) : nullptr;
  if (model == nullptr || model->GetPositions() == nullptr) {
    return 0;
  }
  const CCubeModel::ModelInstance& instance = model->GetModelInstance();
  const uint* coords = static_cast< const uint* >(instance.GetTCPointer());
  const uint* positions = static_cast< const uint* >(model->GetPositions());
  const uint pairs = instance.GetTCSize() / (2 * sizeof(uint));
  const uint vertices = instance.GetVertexSize() / (3 * sizeof(uint));
  const rstl::vector< TCachedToken< CTexture > >& textures = model->GetTextures();
  if (coords == nullptr) {
    return 0;
  }
  auto f32 = [](uint raw) {
    union {
      uint u;
      float f;
    } value;
    value.u = CBasics::SwapBytes(raw);
    return value.f;
  };
  int count = 0;
  for (int list = 0; list < 2; ++list) {
    CCubeSurface surface = list == 0 ? model->GetNormalSurfaces() : model->GetAlphaSurfaces();
    for (; surface.IsValid() && count < max; surface = surface.GetNextSurface()) {
      const uint* mat = reinterpret_cast< const uint* >(model->GetMaterial(surface).GetData());
      // A quad: one fan/strip/quads command of four vertices, each a big-endian u16 position
      // index, a u16 (unused) and a u16 texture-coordinate index (vertex descriptor 0x30f).
      const uchar* dl = static_cast< const uchar* >(surface.GetDisplayList());
      const uint cmd = dl[0] & 0xf8;
      if (CBasics::SwapBytes(mat[1]) == 0 || model->GetMaterial(surface).GetVertexDesc() != 0x30f ||
          (cmd != 0x80 && cmd != 0xa0) || ((dl[1] << 8) | dl[2]) != 4) {
        continue;
      }
      const uint index = CBasics::SwapBytes(mat[2]);
      if (index >= textures.size()) {
        continue;
      }
      PortIconLayer layer;
      bool ok = true;
      for (int v = 0; v < 4; ++v) {
        const uchar* vertex = dl + 3 + v * 6;
        const uint pos = (vertex[0] << 8) | vertex[1];
        const uint tc = (vertex[4] << 8) | vertex[5];
        if (pos >= vertices || tc >= pairs) {
          ok = false;
          break;
        }
        layer.xz[v][0] = f32(positions[pos * 3]);
        layer.xz[v][1] = f32(positions[pos * 3 + 2]);
        layer.uv[v][0] = f32(coords[tc * 2]);
        layer.uv[v][1] = f32(coords[tc * 2 + 1]);
      }
      if (!ok) {
        continue;
      }
      // Not drawn yet (an icon the player doesn't own): load it now, it is tiny.
      TCachedToken< CTexture >& token = const_cast< TCachedToken< CTexture >& >(textures[index]);
      if (!token.TryCache()) {
        token.Lock();
        token.ForceCache();
      }
      layer.tex = token.GetObject();
      if (layer.tex != nullptr) {
        out[count++] = layer;
      }
    }
  }
  return count;
}

const CCubeModel* CGuiModel::PortCubeModel() const {
  const CGuiFrameModelDatabase* db = GetParentFrame()->GetModelDatabase();
  return db != nullptr ? db->GetModel(mModelIndex) : nullptr;
}

bool CGuiModel::PortGetBounds(CAABox& out) const {
  const CGuiFrameModelDatabase* db = GetParentFrame()->GetModelDatabase();
  const CCubeModel* model = db != nullptr ? db->GetModel(mModelIndex) : nullptr;
  if (model == nullptr) {
    return false;
  }
  // A frame's models carry no bounds (CGuiFrameModelDatabase builds them with the null box), so
  // take them from the positions: big-endian F32 XYZ (vertex format 1).
  const uint* positions = static_cast< const uint* >(model->GetPositions());
  const uint count = model->GetModelInstance().GetVertexSize() / (3 * sizeof(uint));
  if (positions == nullptr || count == 0) {
    return false;
  }
  float bounds[6];
  for (uint i = 0; i < count; ++i) {
    for (int axis = 0; axis < 3; ++axis) {
      union {
        uint u;
        float f;
      } value;
      value.u = CBasics::SwapBytes(positions[3 * i + axis]);
      if (i == 0 || value.f < bounds[axis]) {
        bounds[axis] = value.f;
      }
      if (i == 0 || value.f > bounds[axis + 3]) {
        bounds[axis + 3] = value.f;
      }
    }
  }
  out = CAABox(bounds[0], bounds[1], bounds[2], bounds[3], bounds[4], bounds[5]);
  return true;
}

int CGuiModel::PortGetPositions(CVector3f* out, int max) const {
  const CGuiFrameModelDatabase* db = GetParentFrame()->GetModelDatabase();
  const CCubeModel* model = db != nullptr ? db->GetModel(mModelIndex) : nullptr;
  const uint* positions =
      model != nullptr ? static_cast< const uint* >(model->GetPositions()) : nullptr;
  if (positions == nullptr) {
    return 0;
  }
  const uint count = model->GetModelInstance().GetVertexSize() / (3 * sizeof(uint));
  int written = 0;
  for (uint i = 0; i < count && written < max; ++i) {
    float xyz[3];
    for (int axis = 0; axis < 3; ++axis) {
      union {
        uint u;
        float f;
      } value;
      value.u = CBasics::SwapBytes(positions[3 * i + axis]);
      xyz[axis] = value.f;
    }
    out[written++] = CVector3f(xyz[0], xyz[1], xyz[2]);
  }
  return written;
}

#endif
CGuiWidget::EWidgetUsageFlags CGuiModel::GetWidgetUsageFlags() const { return kWUF_PreDraw; }

FourCC CGuiModel::GetWidgetTypeID() const { return 'MODL'; }
