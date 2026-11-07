#ifndef _CGUIMODEL
#define _CGUIMODEL

#include "GuiSys/CGuiWidget.hpp"

class CModelFlags;
#ifdef TARGET_PC
class CAABox;
class CCubeModel;
class CTexture;
#endif

class CGuiModel : public CGuiWidget {
public:
  CGuiModel(const CGuiWidgetParms& parms, CAssetId modelId, uint lightMask, int modelIndex);

  // CGuiObject
  ~CGuiModel() override;

  // CGuiWidget
  FourCC GetWidgetTypeID() const override;
  EWidgetUsageFlags GetWidgetUsageFlags() const override;
  void Draw(const CGuiWidgetDrawParms& parms) const override;

  void DrawModel(const CModelFlags& flags) const; // Guessed name
#ifdef TARGET_PC
  // Port: the model's local bounds, for the widescreen HUD spread. False until the frame has loaded.
  bool PortGetBounds(CAABox& out) const;
  // Port: the model-space vertex positions, at most `max`; 0 until the frame has loaded.
  int PortGetPositions(CVector3f* out, int max) const;
  // Port (the touch wheels' HUD icons): one textured quad of the model. xz/uv are its four corners
  // in the display list's order (model-space x/z, and the texture coordinates there).
  struct PortIconLayer {
    const CTexture* tex;
    float xz[4][2];
    float uv[4][2];
  };
  // The quads of the model's surfaces (normal ones first, then the sorted ones: the draw order),
  // at most `max`; 0 until the frame has loaded. An icon can be several layers (the Annihilator
  // Beam's centre triangle is a second surface with its own texture). The first layer is the
  // icon's cell in the shared atlas.
  int PortIconLayers(PortIconLayer* out, int max) const;
  // The model's mesh in the frame's database (null until the frame has loaded), and how it blends:
  // for the drawn button ring (port_prompt_ring.h).
  const CCubeModel* PortCubeModel() const;
  int PortDrawFlags() const { return mDrawFlags; }
#endif
  static void BeginDraw();                        // Guessed name
  static void EndDraw();                          // Guessed name
  static CGuiModel* Create(CGuiFrame* frame, CInputStream& in, uint version);

private:
  static void UpdateDrawState(int drawFlags, CColor color); // Guessed name

  CAssetId mModelId;
  int mModelIndex;
  uint mLightMask;
};
CHECK_SIZEOF(CGuiModel, 0xc8)

#endif // _CGUIMODEL
