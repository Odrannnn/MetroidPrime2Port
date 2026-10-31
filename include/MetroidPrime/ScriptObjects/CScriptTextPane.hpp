#ifndef _CSCRIPTTEXTPANE
#define _CSCRIPTTEXTPANE

#include "MetroidPrime/CActor.hpp"

#include "Kyoto/Text/CGuiTextSupport.hpp"
#include "Kyoto/Text/CStringTable.hpp"

class CScriptTextPane : public CActor {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CScriptTextPane(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                  const CTransform4f& xf, const CVector3f& pivotOffset, float width, float height,
                  int extentX, int extentY, const CColor& fontColor, const CColor& outlineColor,
                  const CColor& geometryColor, CAssetId font, CAssetId defaultString,
                  const rstl::string& defaultStringName, int blendMode, float fadeInTime,
                  float fadeOutTime, const CGuiTextProperties& textProperties,
                  const rstl::string& guiLabel, bool depthCompare, bool depthUpdate,
                  bool depthBackwards);

  // CEntity
  ~CScriptTextPane() override;
  CEntity* TypesMatch(int typeId) const override;
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

  // CActor
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  void Render(const CStateManager& mgr) const override;
  bool CanRenderUnsorted(const CStateManager& mgr) const override;

  void SetRenderScale(float scale);
  CGuiTextSupport& TextSupport() { return mTextSupport; } // Guessed name.
#ifdef TARGET_PC
  // Port: a pane like this one, `scale` of its size, `lineOffset` of its heights below it, added to
  // the same area (Trilogy's file select has no panes for a slot's details). Returns its id.
  TUniqueId PortAddDetailsPane(CStateManager& mgr, float lineOffset, float scale) const;
  // Moves this details pane with `title` (same lineOffset as PortAddDetailsPane).
  void PortFollow(const CScriptTextPane& title, float lineOffset);
#endif

private:
  // Guessed member names.
  float mFadeOpacity;
  CVector3f mPivotOffset;
  float mWidth;
  float mHeight;
  CGuiTextSupport mTextSupport;
  int mBlendMode;
  rstl::optional_object< TLockedToken< CStringTable > > mDefaultString;
  int mStringIndex;
  rstl::string mGuiLabel;
  float mFadeInTime;
  float mFadeOutTime;
  float mRenderScale;
  float mTargetRenderScale;
  bool mDepthCompare : 1;
  bool mDepthUpdate : 1;
  bool mDepthBackwards : 1;
};
CHECK_SIZEOF(CScriptTextPane, 0xeb8)

#ifdef TARGET_PC
// GUI-pixel bounds of the text pane id as drawn this frame, or false.
bool PortTextPaneBounds(TUniqueId id, float& x0, float& y0, float& x1, float& y1);
#endif
#endif
