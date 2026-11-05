#include "MetroidPrime/ScriptObjects/CScriptTextPane.hpp"

#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Math/CVector2f.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrTextPane.hpp"
#include "rstl/math.hpp"
#ifdef TARGET_PC
#include "port_pointer.h"
#include <string.h>

namespace {
struct SPortPaneRect {
  ushort id;
  float x0, y0, x1, y1;
};
SPortPaneRect sPortPaneRects[64];
int sPortPaneCount = 0;
int sPortPaneFrame = -1;
} // namespace

bool PortTextPaneBounds(TUniqueId id, float& x0, float& y0, float& x1, float& y1) {
  for (int i = 0; i < sPortPaneCount; ++i) {
    if (sPortPaneRects[i].id == id.value) {
      x0 = sPortPaneRects[i].x0;
      y0 = sPortPaneRects[i].y0;
      x1 = sPortPaneRects[i].x1;
      y1 = sPortPaneRects[i].y1;
      return true;
    }
  }
  return false;
}
#endif

CScriptTextPane::CScriptTextPane(
    TUniqueId uid, const rstl::string& name, const CEntityInfo& info, const CTransform4f& xf,
    const CVector3f& pivotOffset, float width, float height, int extentX, int extentY,
    const CColor& fontColor, const CColor& outlineColor, const CColor& geometryColor, CAssetId font,
    CAssetId defaultString, const rstl::string& defaultStringName, int blendMode, float fadeInTime,
    float fadeOutTime, const CGuiTextProperties& textProperties, const rstl::string& guiLabel,
    bool depthCompare, bool depthUpdate, bool depthBackwards)
: CActor(uid, name, info, 0, xf, CModelData::CModelDataNull(), CMaterialList(),
         CActorParameters::None(), kInvalidUniqueId)
, mFadeOpacity(info.GetActive() ? 1.f : 0.f)
, mPivotOffset(pivotOffset)
, mWidth(width)
, mHeight(height)
, mTextSupport(font, extentX, extentY, textProperties, fontColor, outlineColor, geometryColor,
               gpSimplePool)
, mBlendMode(blendMode)
, mDefaultString(defaultString == kInvalidAssetId
                     ? rstl::optional_object_null()
                     : rstl::optional_object< TLockedToken< CStringTable > >(
                           gpSimplePool->GetObj(SObjectTag('STRG', defaultString))))
, mStringIndex(0)
, mGuiLabel(guiLabel)
, mFadeInTime(fadeInTime)
, mFadeOutTime(fadeOutTime)
, mRenderScale(1.f)
, mTargetRenderScale(1.f)
, mDepthCompare(depthCompare)
, mDepthUpdate(depthUpdate)
, mDepthBackwards(depthBackwards) {
  switch (blendMode) {
  case 0:
    SetModelFlags(CModelFlags::ColorModulate(geometryColor));
    break;
  case 1:
    SetModelFlags(CModelFlags::AlphaBlended(geometryColor));
    // The original has no break here, so blend mode 1 ends up additive.
  case 2:
    SetModelFlags(CModelFlags::Additive(geometryColor));
    break;
  }

  if (mDefaultString) {
    if (defaultStringName.size() != 0) {
      const int index = (*mDefaultString)->GetStringIndex(defaultStringName.c_str());
      if (index != -1) {
        mStringIndex = index;
      }
    }
    mTextSupport.SetText(rstl::wstring_l((*mDefaultString)->GetString(mStringIndex)));
  }
}

void CScriptTextPane::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  CActor::AcceptScriptMsg(mgr, msg);
  switch (msg.GetMessage()) {
  case kSM_Increment:
    if (!GetActive()) {
      mgr.SendScriptMsg(this, GetUniqueId(), kSM_Activate, kInvalidUniqueId);
      mFadeOpacity = FLT_EPSILON;
    }
    break;
  case kSM_Decrement:
    mFadeOpacity = -(1.f - FLT_EPSILON);
    break;
  default:
    break;
  }
}

void CScriptTextPane::Think(float dt, CStateManager& mgr) {
  const float fadeOpacity = mFadeOpacity;
  if (fadeOpacity < 0.f) {
    mFadeOpacity = rstl::min_val(0.f, 1.f / mFadeOutTime * dt + fadeOpacity);
    if (0.f == mFadeOpacity) {
      mgr.SendScriptMsg(this, GetUniqueId(), kSM_Deactivate, kInvalidUniqueId);
    }
  } else if (fadeOpacity < 1.f) {
    mFadeOpacity = rstl::min_val(1.f, 1.f / mFadeInTime * dt + fadeOpacity);
  }

  const float renderScale = mRenderScale;
  const float targetRenderScale = mTargetRenderScale;
  if (renderScale < targetRenderScale) {
    mRenderScale = rstl::min_val(targetRenderScale, 8.f * dt + renderScale);
  } else if (renderScale > targetRenderScale) {
    mRenderScale = rstl::max_val(targetRenderScale, renderScale - 8.f * dt);
  }
}

void CScriptTextPane::PreRender(CStateManager& mgr) {
  mTextSupport.SetGeometryColor(
      GetModelFlags().GetColor().WithAlphaModulatedBy(fabs(mFadeOpacity)));
}

void CScriptTextPane::AddToRenderer(const CStateManager& mgr) const { EnsureRendered(mgr); }

void CScriptTextPane::Render(const CStateManager& mgr) const {
  const CVector2f dimensions(mWidth, mHeight);
  const int extentX = mTextSupport.GetTextBoundingWidth();
  const float scaleX = extentX != 0 ? dimensions.GetX() / extentX : 0.f;
  const int extentY = mTextSupport.GetTextBoundingHeight();
  const float scaleY = extentY != 0 ? dimensions.GetY() / extentY : 0.f;
  const CVector3f offset(-1.f * mPivotOffset.GetX() * mRenderScale,
                         -1.f * mPivotOffset.GetY() * mRenderScale,
                         mPivotOffset.GetZ() * mRenderScale);
  const CTransform4f local = CTransform4f::Translate(offset) *
                             CTransform4f::Scale(scaleX * mRenderScale, 1.f, scaleY * mRenderScale);
  const CTransform4f model = GetTransform() * local;
  CGraphics::SetModelMatrix(model);
  CGraphics::SetDepthWriteMode(mDepthCompare, kE_LEqual, mDepthUpdate);
  switch (mBlendMode) {
  case 0:
    CGraphics::SetBlendMode(kBM_Blend, kBF_One, kBF_Zero, kLO_Clear);
    break;
  case 1:
    CGraphics::SetBlendMode(kBM_Blend, kBF_SrcAlpha, kBF_InvSrcAlpha, kLO_Clear);
    break;
  case 2:
    CGraphics::SetBlendMode(kBM_Blend, kBF_SrcAlpha, kBF_One, kLO_Clear);
    break;
  }
  mTextSupport.Render();
#ifdef TARGET_PC
  if (PortPointer::Enabled() && extentX != 0 && extentY != 0) {
    const int frame = static_cast< int >(CGraphics::GetFrameCounter());
    if (frame != sPortPaneFrame) {
      sPortPaneFrame = frame;
      sPortPaneCount = 0;
    }
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    bool ok = true;
    for (int c = 0; c < 4 && ok; ++c) {
      const CVector3f p(static_cast< float >((c & 1) ? extentX : 0), 0.f,
                        static_cast< float >((c & 2) ? -extentY : 0));
      float gx, gy;
      if (!PortGuiHit::Project(CTransform4f::Identity() * model, p, gx, gy)) {
        ok = false;
        break;
      }
      minX = gx < minX ? gx : minX;
      maxX = gx > maxX ? gx : maxX;
      minY = gy < minY ? gy : minY;
      maxY = gy > maxY ? gy : maxY;
    }
    if (ok && sPortPaneCount < 64) {
      SPortPaneRect& r = sPortPaneRects[sPortPaneCount++];
      r.id = GetUniqueId().value;
      r.x0 = minX;
      r.y0 = minY;
      r.x1 = maxX;
      r.y1 = maxY;
    }
  }
#endif
}

bool CScriptTextPane::CanRenderUnsorted(const CStateManager& mgr) const { return mBlendMode == 0; }

void CScriptTextPane::SetRenderScale(float scale) { mTargetRenderScale = scale; }

#ifdef TARGET_PC
TUniqueId CScriptTextPane::PortAddDetailsPane(CStateManager& mgr, float lineOffset,
                                              float scale) const {
  const TUniqueId uid = mgr.AllocateUniqueId();
  const CEntityInfo info(GetCurrentAreaId(), rstl::vector< SConnection >(), GetActive());
  const CGuiTextSupport& ts = mTextSupport;
  CScriptTextPane* pane = rs_new CScriptTextPane(
      uid, rstl::string_l("PortDetails"), info,
      GetTransform() * CTransform4f::Translate(0.f, 0.f, -lineOffset * mHeight),
      CVector3f(mPivotOffset.GetX() - mWidth * (1.f - scale) * 0.5f, mPivotOffset.GetY(),
                mPivotOffset.GetZ() - mHeight * (1.f - scale) * 0.5f),
      mWidth * scale, mHeight * scale, ts.GetTextBoundingWidth(),
      ts.GetTextBoundingHeight(), ts.PortFontColor(), ts.PortOutlineColor(), ts.PortGeometryColor(),
      ts.GetFontID(), kInvalidAssetId, rstl::string_l(""), mBlendMode, mFadeInTime, mFadeOutTime,
      ts.PortProperties(), rstl::string_l(""), mDepthCompare, mDepthUpdate, mDepthBackwards);
  if (pane != nullptr) {
    mgr.AddObject(*pane);
  }
  return uid;
}

void CScriptTextPane::PortFollow(const CScriptTextPane& title, float lineOffset) {
  // The slot panes are moved and scaled by the screen's script while selected.
  mRenderScale = title.mRenderScale;
  mTargetRenderScale = title.mTargetRenderScale;
  SetTransform(title.GetTransform() *
               CTransform4f::Translate(0.f, 0.f, -lineOffset * title.mHeight * title.mRenderScale));
}

bool CScriptTextPane::PortShowsString(const char* name) const {
  if (!mDefaultString) {
    return false;
  }
  const CStringTable* table = *const_cast< TLockedToken< CStringTable >& >(*mDefaultString);
  const int index = table->GetStringIndex(name);
  if (index == -1 || mStringIndex != index) {
    return false;
  }
  // Panes placed without a name keep string 0, and scripts may retext a pane: compare the text.
  const wchar_t* want = table->GetString(index);
  const rstl::wstring& text = mTextSupport.GetText();
  const wchar_t* have = text.data();
  int i = 0;
  for (; i < text.size(); ++i) {
    if (want[i] != have[i]) {
      return false;
    }
  }
  return want[i] == L'\0';
}
#endif

CEntity* LoadTextPane(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrTextPane sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrTextPane.inc"

  return rs_new CScriptTextPane(
      mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
      LdrToEntityInfo(info, sldrThis.editorProperties), LdrToTransform4f(sldrThis.editorProperties),
      sldrThis.pivotOffset, sldrThis.editorProperties.transform.scale.GetX(),
      sldrThis.editorProperties.transform.scale.GetZ(), sldrThis.textProperties.textBoundingWidth,
      sldrThis.textProperties.textBoundingHeight, sldrThis.textProperties.foregroundColor,
      sldrThis.textProperties.outlineColor, sldrThis.textProperties.geometryColor,
      sldrThis.textProperties.defaultFont, sldrThis.defaultString, sldrThis.defaultStringName,
      sldrThis.blend_Mode, sldrThis.fadeInTime, sldrThis.fadeOutTime,
      CGuiTextProperties(sldrThis.textProperties.wrapText,
                         EJustification(sldrThis.textProperties.horizontalJustification),
                         EVerticalJustification(sldrThis.textProperties.verticalJustification)),
      sldrThis.guiLabel, sldrThis.depth_Compare, sldrThis.depth_Update, sldrThis.depth_Backwards);
}

CScriptTextPane::~CScriptTextPane() {}
