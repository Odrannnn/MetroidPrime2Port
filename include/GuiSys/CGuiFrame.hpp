#ifndef _CGUIFRAME
#define _CGUIFRAME
#include "GuiSys/CGuiWidgetIdDB.hpp"
#include "Kyoto/CToken.hpp"
#include "Kyoto/Graphics/CLight.hpp"
#include "rstl/auto_ptr.hpp"
#include "rstl/reserved_vector.hpp"
class CGuiWidget;
class CGuiHeadWidget;
class CGuiCamera;
class CGuiLight;
class CGuiWidgetDrawParms;
class CGuiFrameModelDatabase; // Guessed name
class CSimplePool;
class CInputStream;
class CFinalInput;
class CGuiFrame {
public:
  CGuiFrame(CInputStream& in, CSimplePool* pool);
  ~CGuiFrame();
  CGuiWidget* FindWidget(const char* name) const;
  CGuiWidget* FindWidget(short id) const;
  CGuiWidget* FindWidget(const rstl::string& name) const;
  bool GetIsFinishedLoading() const;
  void Update(float dt);
  void ProcessUserInput(const CFinalInput& input);
  void Draw(const CGuiWidgetDrawParms& parms) const;
  CGuiCamera* GetFrameCamera() const { return mCamera; }
  CGuiHeadWidget* GetRootWidget() const { return mRootWidget; }
  const CGuiFrameModelDatabase* GetModelDatabase() const { return mModelDatabase.get(); }
  CGuiWidgetIdDB& WidgetIdDB() { return mWidgetIds; }
  void SetFrameCamera(CGuiCamera* camera);
  void SetHeadWidget(CGuiHeadWidget* widget);
  void Initialize();
  void SortDrawOrder();
  void AddLight(CGuiLight* light);
  void RemoveLight(CGuiLight* light);
  void EnableLights(uint mask) const;
  void DisableLights() const;
  void ApplyLights() const; // Guessed name
#ifdef TARGET_PC
  // Port: widgets that move as one unit under the widescreen spread share the anchor's position.
  // An explicit tree anchor lets sibling trees (scan backgrounds/rows) share one side's motion.
  void SetSpreadAnchor(const CGuiWidget* member, const CGuiWidget* anchor);
  void SetSpreadAnchorTree(const CGuiWidget* root, const CGuiWidget* anchor = nullptr);
  // Port: move `root` and every widget under it by only `weight()` of the horizontal spread (0 keeps
  // the 4:3 place, 1 is the full spread). Read at every draw, so it follows a setting live.
  void SetSpreadWeightTree(const CGuiWidget* root, float (*weight)());
  // Port: draw `member` (a model spanning the view) cut at `cut` of the authored half-width: the
  // middle stays, everything outside slides whole with the screen edge, nothing stretches. A null
  // `member` splits every model of the frame that spans the view. A negative `cut` stretches the
  // model evenly instead, the authored view's edges onto the screen's.
  void SetSpreadSplit(const CGuiWidget* member, float cut);
  // Port: a frame made of a main window and a side panel (the pause screen): under the spread the
  // window [windowLeft, windowRight] (fractions of the authored half-width) grows to the screen's
  // left edge and the panel's divider, the panel slides to the right edge. Backgrounds stretch,
  // everything else keeps its shape and moves with its place in the window; `bandModel`'s middle
  // [bandLeft, bandRight] is the only part of the border frame that stretches.
  void SetSpreadPanel(float windowLeft, float windowRight, const CGuiWidget* bandModel,
                      float bandLeft, float bandRight);
  // Port: `member` moves by `side` (-1 left edge .. +1 right edge) of the panel spread, for widgets
  // that hug a corner of the window.
  void SetSpreadPanelSide(const CGuiWidget* member, float side);
#endif

private:
  // Reconstructed resource-reader names and source qualifiers; native ABI verified.
  void LoadWidgetsInGame(CInputStream& in, CSimplePool* pool, uint version);
  static rstl::vector< CToken > LoadAssets(CInputStream& in, CSimplePool* pool, uint version);
  static uint ReadVersion(CInputStream& in);

  uint mVersion;
  rstl::vector< CToken > mAssets;
  CGuiHeadWidget* mRootWidget;
  CGuiCamera* mCamera;
  CGuiWidgetIdDB mWidgetIds;
  rstl::vector< CGuiWidget* > mWidgets;
  rstl::vector< CGuiWidget* > mDrawWidgets;
  rstl::vector< CGuiWidget* > mPreDrawWidgets;
  rstl::vector< CGuiWidget* > mUpdateWidgets;
  rstl::vector< CGuiWidget* > mInputWidgets;
  rstl::vector< CGuiLight* > mLights;
  rstl::reserved_vector< CLight, 8 > mActiveLights;
  CColor mAmbientColor;
  rstl::auto_ptr< CGuiFrameModelDatabase > mModelDatabase;
  mutable bool mLoaded : 1;
};
CHECK_SIZEOF(CGuiFrame, 0x334)
#endif // _CGUIFRAME
