#include "GuiSys/CGuiFrame.hpp"

#include "GuiSys/CGuiCamera.hpp"
#include "GuiSys/CGuiFactories.hpp"
#include "GuiSys/CGuiFrameLoader.hpp"
#include "GuiSys/CGuiFrameModelDatabase.hpp"
#include "GuiSys/CGuiHeadWidget.hpp"
#include "GuiSys/CGuiLight.hpp"
#include "GuiSys/CGuiModel.hpp"
#include "GuiSys/CGuiWidget.hpp"
#include "Kyoto/CResFactory.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Streams/CMemoryInStream.hpp"
#include "rstl/algorithm.hpp"
#include "rstl/list.hpp"

#ifdef TARGET_PC
#include "Kyoto/Math/CAABox.hpp"

// Port: widescreen HUD, as MP1's CGuiFrame::Draw. The camera widened the projection by `spread`
// (CGuiCamera::Draw). A model spanning both outer quarters of the authored view (the visor frame)
// is warped in slices (PortDrawSliced): the middle, with the energy bar's housing and lettering,
// keeps its authored size, a band either side stretches, and the rest slides out with the screen
// edge, keeping its authored distance to it. A model registered with SetSpreadSplit (the glass
// brackets) is cut in two instead: the middle stays and each side slides whole with the edge. A
// negative split (the helmet, every model of its frame) stretches the model evenly to the screen.
// Every other widget slides as an image by CGuiCamera::GetSpreadTransform at its anchor (a model's
// bounds centre, since several icons keep their place inside the vertices and their widget at the
// frame centre): past the middle by the strut's whole offset, so each side cluster keeps its shape
// and stays on its strut.

static void PortDrawSlicedPiece(const CGuiCamera& camera, const CGuiWidget* widget,
                                const CGuiWidgetDrawParms& parms, const CTransform4f& toView,
                                const CTransform4f& fromView, const u32* scissor, float from,
                                float to, float fromWarped, float toWarped) {
  // Maps the authored span [from, to] (view X per unit depth, or ortho units from the centre)
  // onto [fromWarped, toWarped]: x' = scale * x + offset * depth in view space, a screen-space
  // scale and slide. Drawn under a scissor covering the warped span's columns.
  const float scale = (toWarped - fromWarped) / (to - from);
  const float offset = fromWarped - scale * from;
  const CViewport& viewport = CGraphics::GetViewport();
  const float screenHalf = camera.GetSpreadScreenHalfWidth();
  const float columns[2] = {fromWarped, toWarped};
  int cut[2];
  for (int i = 0; i < 2; ++i) {
    float t = columns[i];
    t = t < -screenHalf ? -screenHalf : t > screenHalf ? screenHalf : t;
    cut[i] = viewport.mLeft +
             static_cast< int >(0.5f * static_cast< float >(viewport.mWidth) *
                                    (1.f + t / screenHalf) +
                                0.5f);
  }
  const int left = cut[0] > static_cast< int >(scissor[0]) ? cut[0] : static_cast< int >(scissor[0]);
  const int right = cut[1] < static_cast< int >(scissor[0] + scissor[2])
                        ? cut[1]
                        : static_cast< int >(scissor[0] + scissor[2]);
  if (right <= left) {
    return;
  }
  CTransform4f warp = CTransform4f::Identity();
  if (camera.IsPerspective()) {
    warp = CTransform4f(scale, offset, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f);
  } else {
    const float centerX = camera.GetSpreadCenterX();
    warp = CTransform4f(scale, 0.f, 0.f, centerX - scale * centerX + offset, 0.f, 1.f, 0.f, 0.f,
                        0.f, 0.f, 1.f, 0.f);
  }
  CTransform4f savedXf = CTransform4f::Identity();
  bool savedValid = false;
  if (widget->PortSetWorldTransform(toView * warp * fromView, savedXf, savedValid)) {
    GXSetScissor(left, scissor[1], right - left, scissor[3]);
    widget->Draw(parms);
    widget->PortRestoreWorldTransform(savedXf, savedValid);
  }
}

static void PortDrawSliced(const CGuiCamera& camera, const CGuiWidget* widget,
                           const CGuiWidgetDrawParms& parms, const CTransform4f& view,
                           const CTransform4f& fromView, bool curved, float depth,
                           float split = 0.f) {
  // Width (of the authored half-width) past a rigid split's cut that stretches over the gap: clear of
  // the energy digits and of the glass brackets at 16:9.
  static const float kSpreadSplitBand = 0.11f;
  u32 scissor[4];
  GXGetScissor(&scissor[0], &scissor[1], &scissor[2], &scissor[3]);
  const float halfWidth = camera.GetSpreadHalfWidth();
  const float inner =
      (split > 0.f ? split
                   : curved ? CGuiCamera::kSpreadCurveInner : CGuiCamera::kSpreadSliceInner) *
      halfWidth;
  const float outer =
      (curved ? CGuiCamera::kSpreadCurveOuter : CGuiCamera::kSpreadSliceOuter) * halfWidth;
  // Past the screen edge, so the outer scissors reach it.
  const float edge = 2.f * camera.GetSpreadScreenHalfWidth();
  // The slices are cut in the unlagged (authored) tangents and carried through the HUD lag at the
  // model's view depth: each piece is the linear map through its lagged end points.
  const auto piece = [&](float from, float to, float fromWarped, float toWarped) {
    PortDrawSlicedPiece(camera, widget, parms, view, fromView, scissor,
                        camera.GetLaggedTangent(from, depth), camera.GetLaggedTangent(to, depth),
                        camera.GetLaggedTangent(fromWarped, depth),
                        camera.GetLaggedTangent(toWarped, depth));
  };
  if (split < 0.f) {
    // Uniform: the authored view's edges land on the screen's, everything between scaled evenly
    // (the helmet, as an emulator's widescreen stretch draws it).
    const float screenHalf = camera.GetSpreadScreenHalfWidth();
    piece(-edge, edge, -edge * screenHalf / halfWidth, edge * screenHalf / halfWidth);
    GXSetScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    return;
  }
  piece(-inner, inner, -inner, inner);
  if (split > 0.f) {
    // Rigid split: past a narrow band both sides slide whole by the full spread offset. The band
    // holds only the glass's gradient and stretches across the gap, so the gradient stays unbroken.
    const float moved = camera.GetSpreadSliceOffset(edge);
    const float band = inner + kSpreadSplitBand * halfWidth;
    piece(inner, band, inner, band + moved);
    piece(-band, -inner, -band - moved, -inner);
    piece(band, edge, band + moved, edge + moved);
    piece(-edge, -band, -edge - moved, -band - moved);
    GXSetScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    return;
  }
  // The band is one linear piece, or for the eased curve enough chords that the joints don't
  // show; each maps its authored span onto the warped one, mirrored on the left.
  const int pieces = curved ? 16 : 1;
  float from = inner;
  float fromWarped = inner;
  for (int i = 1; i <= pieces; ++i) {
    const float to = inner + (outer - inner) * static_cast< float >(i) / pieces;
    const float toWarped = to + camera.GetSpreadSliceOffset(to, curved);
    piece(from, to, fromWarped, toWarped);
    piece(-to, -from, -toWarped, -fromWarped);
    from = to;
    fromWarped = toWarped;
  }
  piece(outer, edge, fromWarped, edge + fromWarped - outer);
  piece(-edge, -outer, -edge - fromWarped + outer, -fromWarped);
  GXSetScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
}

// Cluster members (the visor/beam selectors) share their group's anchor, so the cluster moves as one
// unit instead of each icon and lozenge drifting by its own bounding box. Kept out of CGuiFrame so
// the frame's layout is unchanged; entries are dropped with the frame.
struct SPortSpreadAnchor {
  const CGuiFrame* frame;
  const CGuiWidget* member;
  const CGuiWidget* group;
};
static SPortSpreadAnchor sPortSpreadAnchors[256];
static int sPortSpreadAnchorCount = 0;

static const CGuiWidget* PortFindSpreadAnchor(const CGuiFrame* frame, const CGuiWidget* widget) {
  for (int i = 0; i < sPortSpreadAnchorCount; ++i) {
    if (sPortSpreadAnchors[i].frame == frame && sPortSpreadAnchors[i].member == widget) {
      return sPortSpreadAnchors[i].group;
    }
  }
  return nullptr;
}

void CGuiFrame::SetSpreadAnchor(const CGuiWidget* member, const CGuiWidget* anchor) {
  for (int i = 0; i < sPortSpreadAnchorCount; ++i) {
    if (sPortSpreadAnchors[i].frame == this && sPortSpreadAnchors[i].member == member) {
      sPortSpreadAnchors[i].group = anchor;
      return;
    }
  }
  if (sPortSpreadAnchorCount < 256) {
    SPortSpreadAnchor& entry = sPortSpreadAnchors[sPortSpreadAnchorCount++];
    entry.frame = this;
    entry.member = member;
    entry.group = anchor;
  }
}

void CGuiFrame::SetSpreadAnchorTree(const CGuiWidget* root, const CGuiWidget* anchor) {
  if (root == nullptr) {
    return;
  }
  if (anchor == nullptr) {
    anchor = root;
  }
  SetSpreadAnchor(root, anchor);
  for (rstl::vector< CGuiWidget* >::const_iterator it = mWidgets.begin(); it != mWidgets.end();
       ++it) {
    for (const CGuiObject* obj = (*it)->GetParent(); obj != nullptr; obj = obj->GetParent()) {
      if (obj == root) {
        SetSpreadAnchor(*it, anchor);
        break;
      }
    }
  }
}

// Widgets moved by only part of the horizontal spread (the scan visor's side panels). Kept beside
// the anchors for the same reason; `weight` is read at every draw.
struct SPortSpreadWeight {
  const CGuiFrame* frame;
  const CGuiWidget* member;
  float (*weight)();
};
static SPortSpreadWeight sPortSpreadWeights[256];
static int sPortSpreadWeightCount = 0;

static float PortFindSpreadWeight(const CGuiFrame* frame, const CGuiWidget* widget) {
  for (int i = 0; i < sPortSpreadWeightCount; ++i) {
    if (sPortSpreadWeights[i].frame == frame && sPortSpreadWeights[i].member == widget) {
      const float w = sPortSpreadWeights[i].weight();
      return w < 0.f ? 0.f : (w > 1.f ? 1.f : w);
    }
  }
  return 1.f;
}

void CGuiFrame::SetSpreadWeightTree(const CGuiWidget* root, float (*weight)()) {
  if (root == nullptr || weight == nullptr) {
    return;
  }
  for (rstl::vector< CGuiWidget* >::const_iterator it = mWidgets.begin(); it != mWidgets.end();
       ++it) {
    const CGuiObject* obj = *it;
    for (; obj != nullptr && obj != root; obj = obj->GetParent()) {
    }
    if (obj == nullptr) {
      continue;
    }
    int slot = -1;
    for (int i = 0; i < sPortSpreadWeightCount; ++i) {
      if (sPortSpreadWeights[i].frame == this && sPortSpreadWeights[i].member == *it) {
        slot = i;
      }
    }
    if (slot < 0 && sPortSpreadWeightCount < 256) {
      slot = sPortSpreadWeightCount++;
    }
    if (slot >= 0) {
      sPortSpreadWeights[slot].frame = this;
      sPortSpreadWeights[slot].member = *it;
      sPortSpreadWeights[slot].weight = weight;
    }
  }
}


static void PortDropSpreadWeights(const CGuiFrame* frame) {
  int kept = 0;
  for (int i = 0; i < sPortSpreadWeightCount; ++i) {
    if (sPortSpreadWeights[i].frame != frame) {
      sPortSpreadWeights[kept++] = sPortSpreadWeights[i];
    }
  }
  sPortSpreadWeightCount = kept;
}

// Models cut rigidly instead of stretched (the visor frame's glass): `cut` is the fraction of the
// authored half-width kept in the middle. A null member stands for every model of the frame that
// spans the view, so models meeting the glass (the helmet's plate) keep meeting it.
struct SPortSpreadSplit {
  const CGuiFrame* frame;
  const CGuiWidget* member;
  float cut;
};
static SPortSpreadSplit sPortSpreadSplits[16];
static int sPortSpreadSplitCount = 0;

static float PortFindSpreadSplit(const CGuiFrame* frame, const CGuiWidget* widget, bool spanning) {
  for (int i = 0; i < sPortSpreadSplitCount; ++i) {
    if (sPortSpreadSplits[i].frame == frame &&
        (sPortSpreadSplits[i].member == widget ||
         ((spanning || sPortSpreadSplits[i].cut < 0.f) &&
          sPortSpreadSplits[i].member == nullptr))) {
      return sPortSpreadSplits[i].cut;
    }
  }
  return 0.f;
}

void CGuiFrame::SetSpreadSplit(const CGuiWidget* member, float cut) {
  int slot = -1;
  for (int i = 0; i < sPortSpreadSplitCount; ++i) {
    if (sPortSpreadSplits[i].frame == this && sPortSpreadSplits[i].member == member) {
      slot = i;
    }
  }
  if (slot < 0 && sPortSpreadSplitCount < 16) {
    slot = sPortSpreadSplitCount++;
  }
  if (slot >= 0) {
    sPortSpreadSplits[slot].frame = this;
    sPortSpreadSplits[slot].member = member;
    sPortSpreadSplits[slot].cut = cut;
  }
}

static void PortDropSpreadSplits(const CGuiFrame* frame) {
  int kept = 0;
  for (int i = 0; i < sPortSpreadSplitCount; ++i) {
    if (sPortSpreadSplits[i].frame != frame) {
      sPortSpreadSplits[kept++] = sPortSpreadSplits[i];
    }
  }
  sPortSpreadSplitCount = kept;
}

static void PortDropSpreadAnchors(const CGuiFrame* frame) {
  int kept = 0;
  for (int i = 0; i < sPortSpreadAnchorCount; ++i) {
    if (sPortSpreadAnchors[i].frame != frame) {
      sPortSpreadAnchors[kept++] = sPortSpreadAnchors[i];
    }
  }
  sPortSpreadAnchorCount = kept;
}

// The pause screen's layout (SetSpreadPanel). One window and a side panel per frame; the table is
// kept here for the same reason as the anchors above.
struct SPortSpreadPanel {
  const CGuiFrame* frame;
  float windowLeft;
  float windowRight;
  const CGuiWidget* bandModel;
  float bandLeft;
  float bandRight;
};
static SPortSpreadPanel sPortSpreadPanels[4];
static int sPortSpreadPanelCount = 0;
struct SPortSpreadPanelSide {
  const CGuiFrame* frame;
  const CGuiWidget* member;
  float side;
};
static SPortSpreadPanelSide sPortSpreadPanelSides[32];
static int sPortSpreadPanelSideCount = 0;

void CGuiFrame::SetSpreadPanel(float windowLeft, float windowRight, const CGuiWidget* bandModel,
                               float bandLeft, float bandRight) {
  int slot = -1;
  for (int i = 0; i < sPortSpreadPanelCount; ++i) {
    if (sPortSpreadPanels[i].frame == this) {
      slot = i;
    }
  }
  if (slot < 0 && sPortSpreadPanelCount < 4) {
    slot = sPortSpreadPanelCount++;
  }
  if (slot >= 0) {
    sPortSpreadPanels[slot] = {this, windowLeft, windowRight, bandModel, bandLeft, bandRight};
  }
}

void CGuiFrame::SetSpreadPanelSide(const CGuiWidget* member, float side) {
  for (int i = 0; i < sPortSpreadPanelSideCount; ++i) {
    if (sPortSpreadPanelSides[i].frame == this && sPortSpreadPanelSides[i].member == member) {
      sPortSpreadPanelSides[i].side = side;
      return;
    }
  }
  if (sPortSpreadPanelSideCount < 32) {
    sPortSpreadPanelSides[sPortSpreadPanelSideCount++] = {this, member, side};
  }
}

static void PortDropSpreadPanels(const CGuiFrame* frame) {
  int kept = 0;
  for (int i = 0; i < sPortSpreadPanelCount; ++i) {
    if (sPortSpreadPanels[i].frame != frame) {
      sPortSpreadPanels[kept++] = sPortSpreadPanels[i];
    }
  }
  sPortSpreadPanelCount = kept;
  kept = 0;
  for (int i = 0; i < sPortSpreadPanelSideCount; ++i) {
    if (sPortSpreadPanelSides[i].frame != frame) {
      sPortSpreadPanelSides[kept++] = sPortSpreadPanelSides[i];
    }
  }
  sPortSpreadPanelSideCount = kept;
}

// Draws `widget` for a SetSpreadPanel frame; false = not handled (no layout, or behind the eye).
// Fractions are of the authored half-width, i.e. view tangents over camera.GetSpreadHalfWidth().
static bool PortDrawPanel(const CGuiFrame* frame, const CGuiCamera& camera, const CGuiWidget* widget,
                          const CGuiWidgetDrawParms& parms, const CTransform4f& view,
                          const CTransform4f& invView, const CTransform4f& world) {
  const SPortSpreadPanel* panel = nullptr;
  for (int i = 0; i < sPortSpreadPanelCount; ++i) {
    if (sPortSpreadPanels[i].frame == frame) {
      panel = &sPortSpreadPanels[i];
    }
  }
  const float hw = camera.GetSpreadHalfWidth();
  if (panel == nullptr || !camera.IsPerspective() || hw <= 0.f ||
      camera.GetSpreadScreenHalfWidth() <= 0.f) {
    return false;
  }
  // How far the screen edge moved, in fractions: the window's edges follow it, the panel slides.
  const float moved = camera.GetSpreadSliceOffset(2.f * camera.GetSpreadScreenHalfWidth()) / hw;
  const float center = 0.5f * (panel->windowLeft + panel->windowRight);
  const float halfWindow = 0.5f * (panel->windowRight - panel->windowLeft);
  float side = 2.f;
  for (int i = 0; i < sPortSpreadPanelSideCount; ++i) {
    if (sPortSpreadPanelSides[i].frame == frame && sPortSpreadPanelSides[i].member == widget) {
      side = sPortSpreadPanelSides[i].side;
    }
  }
  // Where a point at `u` goes: the window's own points spread evenly, the panel moves rigidly.
  const auto shiftFor = [&](float u) {
    if (side <= 1.f) {
      return side * moved;
    }
    const float t = (u - center) / halfWindow;
    return moved * (t < -1.f ? -1.f : t > 1.f ? 1.f : t);
  };
  const CTransform4f fromView = invView * world;
  float depth = 0.f;
  float shift = 0.f;
  CAABox bounds(CVector3f(0.f, 0.f, 0.f), CVector3f(0.f, 0.f, 0.f));
  if (widget->GetWidgetTypeID() == 'MODL' &&
      static_cast< const CGuiModel* >(widget)->PortGetBounds(bounds)) {
    const CAABox eyeBounds = bounds.GetTransformedAABox(fromView);
    depth = eyeBounds.GetCenterPoint().GetY();
    if (eyeBounds.GetMinPoint().GetY() <= 0.f) {
      return false;
    }
    const float left = eyeBounds.GetMinPoint().GetX() / depth / hw;
    const float right = eyeBounds.GetMaxPoint().GetX() / depth / hw;
    if (right - left > 0.45f) {
      // Backgrounds and the border: stretched between rigid ends.
      u32 scissor[4];
      GXGetScissor(&scissor[0], &scissor[1], &scissor[2], &scissor[3]);
      const float edge = 2.f * camera.GetSpreadScreenHalfWidth() / hw;
      const auto piece = [&](float from, float to, float fromWarped, float toWarped) {
        PortDrawSlicedPiece(camera, widget, parms, view, fromView, scissor, from * hw, to * hw,
                            fromWarped * hw, toWarped * hw);
      };
      if (widget == panel->bandModel) {
        piece(-edge, panel->bandLeft, -edge - moved, panel->bandLeft - moved);
        piece(panel->bandLeft, panel->bandRight, panel->bandLeft - moved,
              panel->bandRight + moved);
        piece(panel->bandRight, edge, panel->bandRight + moved, edge + moved);
      } else {
        const float margin = 0.08f;
        const float shiftLeft = shiftFor(left);
        const float shiftRight = shiftFor(right);
        piece(-edge, left + margin, -edge + shiftLeft, left + margin + shiftLeft);
        piece(left + margin, right - margin, left + margin + shiftLeft,
              right - margin + shiftRight);
        piece(right - margin, edge, right - margin + shiftRight, edge + shiftRight);
      }
      GXSetScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
      return true;
    }
    shift = shiftFor(0.5f * (left + right));
  } else {
    const CVector3f eye = invView * world.GetTranslation();
    if (eye.GetY() <= 0.f) {
      return false;
    }
    depth = eye.GetY();
    shift = shiftFor(eye.GetX() / depth / hw);
  }
  CTransform4f savedXf = CTransform4f::Identity();
  bool savedValid = false;
  const CTransform4f moveXf = view * CTransform4f::Translate(shift * hw * depth, 0.f, 0.f) * fromView;
  if (widget->PortSetWorldTransform(moveXf, savedXf, savedValid)) {
    widget->Draw(parms);
    widget->PortRestoreWorldTransform(savedXf, savedValid);
  } else {
    widget->Draw(parms);
  }
  return true;
}

static void PortDrawSpread(const CGuiFrame* frame, const CGuiCamera& camera,
                           const CGuiWidget* widget, const CGuiWidgetDrawParms& parms) {
  const float spread = camera.GetAspectSpread();
  const float spreadY = camera.GetSpreadY();
  CTransform4f savedXf = CTransform4f::Identity();
  bool savedValid = false;
  if ((spread == 1.f && spreadY == 1.f) || widget->GetParent() == nullptr) {
    widget->Draw(parms);
    return;
  }
  const CTransform4f view = camera.GetSpreadView();
  const CTransform4f invView = view.GetInverse();
  const CTransform4f world = widget->GetWorldTransform();
  if (spread != 1.f && sPortSpreadPanelCount > 0 &&
      PortDrawPanel(frame, camera, widget, parms, view, invView, world)) {
    return;
  }
  CVector3f anchor = world.GetTranslation();
  bool stretch = false;
  bool stretchY = false;
  float eyeMinDepth = 0.f;
  float eyeDepth = 0.f;
  CAABox bounds(CVector3f(0.f, 0.f, 0.f), CVector3f(0.f, 0.f, 0.f));
  const CGuiWidget* group = PortFindSpreadAnchor(frame, widget);
  const float weight = sPortSpreadWeightCount > 0 ? PortFindSpreadWeight(frame, widget) : 1.f;
  if (group != nullptr) {
    anchor = group->GetWorldTransform().GetTranslation();
  } else if (weight == 1.f && widget->GetWidgetTypeID() == 'MODL' &&
      static_cast< const CGuiModel* >(widget)->PortGetBounds(bounds)) {
    anchor = world * bounds.GetCenterPoint();
    const CAABox eyeBounds = bounds.GetTransformedAABox(invView * world);
    float quarterWidth = 0.5f * camera.GetSpreadHalfWidth();
    if (camera.IsPerspective()) {
      quarterWidth *= eyeBounds.GetMaxPoint().GetY();
    }
    eyeMinDepth = eyeBounds.GetMinPoint().GetY();
    eyeDepth = eyeBounds.GetCenterPoint().GetY();
    const float centerX = camera.GetSpreadCenterX();
    if (spreadY != 1.f) {
      // Meshes reaching both outer quarters of the authored height (the helmet's bars) stretch so
      // they land on the top and bottom edges. A frame stretched evenly (negative split, the
      // helmet) stretches every model, or its plate and lights part and the corners show (#97).
      float quarterHeight = 0.5f * camera.GetSpreadHalfHeight();
      if (camera.IsPerspective()) {
        quarterHeight *= eyeBounds.GetMaxPoint().GetY();
      }
      const float centerZ = camera.GetSpreadCenterZ();
      const bool uniform = sPortSpreadSplitCount > 0 && PortFindSpreadSplit(frame, widget, false) < 0.f;
      stretchY = quarterHeight > 0.f &&
                 (uniform || (eyeBounds.GetMinPoint().GetZ() < centerZ - quarterHeight &&
                              eyeBounds.GetMaxPoint().GetZ() > centerZ + quarterHeight)) &&
                 (!camera.IsPerspective() || eyeMinDepth > 0.f);
    }
    stretch = spread != 1.f && quarterWidth > 0.f &&
              eyeBounds.GetMinPoint().GetX() < centerX - quarterWidth &&
              eyeBounds.GetMaxPoint().GetX() > centerX + quarterWidth;
  }
  // A model registered for a rigid split is cut even when it doesn't reach the outer quarters.
  const float split = sPortSpreadSplitCount > 0 && spread != 1.f && bounds.GetMaxPoint().GetX() !=
                                                                         bounds.GetMinPoint().GetX()
                          ? PortFindSpreadSplit(frame, widget, stretch)
                          : 0.f;
  if ((stretch || split != 0.f) && (!camera.IsPerspective() || eyeMinDepth > 0.f) &&
      camera.GetSpreadScreenHalfWidth() > 0.f) {
    PortDrawSliced(camera, widget, parms, view, invView * world, true,
                   camera.IsPerspective() ? eyeDepth : 0.f, split);
    return;
  }
  CTransform4f moved = world;
  if (stretchY) {
    moved = view * CTransform4f::Translate(0.f, 0.f, camera.GetSpreadCenterZ()) *
            CTransform4f::Scale(1.f, 1.f, spreadY) *
            CTransform4f::Translate(0.f, 0.f, -camera.GetSpreadCenterZ()) * invView * world;
  } else if (stretch) {
    moved = view * CTransform4f::Translate(camera.GetSpreadCenterX(), 0.f, 0.f) *
            CTransform4f::Scale(spread, 1.f, 1.f) *
            CTransform4f::Translate(-camera.GetSpreadCenterX(), 0.f, 0.f) * invView * world;
  } else {
    moved = camera.GetSpreadTransform(anchor, weight) * world;
  }
  if (widget->PortSetWorldTransform(moved, savedXf, savedValid)) {
    widget->Draw(parms);
    widget->PortRestoreWorldTransform(savedXf, savedValid);
  } else {
    widget->Draw(parms);
  }
}
#endif
namespace rstl {
class CWidgetFartherFromCamera {
public:
  CWidgetFartherFromCamera() {}
  bool operator()(const CGuiWidget* a, const CGuiWidget* b) const {
    return a->GetWorldPosition().GetY() > b->GetWorldPosition().GetY();
  }
};
} // namespace rstl

uint CGuiFrame::ReadVersion(CInputStream& in) { return in.Get< uint >(); }

rstl::vector< CToken > CGuiFrame::LoadAssets(CInputStream& in, CSimplePool* pool, uint version) {
  rstl::vector< CToken > assets;
  if (version > 1u) {
    assets.reserve(in.ReadInt32());
    for (int i = 0; i < assets.capacity(); ++i) {
      const FourCC type = in.Get< uint >();
      const CAssetId id = in.Get< uint >();
      CToken token = pool->GetObj(SObjectTag(type, id));
      token.Lock();
      assets.push_back_unsafe(token);
    }
  } else {
    in.ReadInt32();
    in.ReadInt32();
    in.ReadInt32();
  }
  return assets;
}

CGuiFrame::CGuiFrame(CInputStream& in, CSimplePool* pool)
: mVersion(ReadVersion(in))
, mAssets(LoadAssets(in, pool, mVersion))
, mRootWidget(nullptr)
, mCamera(nullptr)
, mLights(rstl::vector< CGuiLight* >(8, static_cast< CGuiLight* >(nullptr)))
, mModelDatabase(rs_new CGuiFrameModelDatabase(in, pool))
, mLoaded(false) {
  LoadWidgetsInGame(in, pool, mVersion);
}

#ifdef TARGET_PC
CGuiFrame::~CGuiFrame() {
  PortDropSpreadAnchors(this);
  PortDropSpreadWeights(this);
  PortDropSpreadSplits(this);
  PortDropSpreadPanels(this);
  delete mRootWidget;
}
#else
CGuiFrame::~CGuiFrame() { delete mRootWidget; }
#endif

void CGuiFrame::LoadWidgetsInGame(CInputStream& in, CSimplePool* pool, uint version) {
  const int count = in.Get< int >();
  mWidgets.reserve(count);
  mWidgetIds.Reserve(count);

  int drawCount = 0;
  int updateCount = 0;
  int inputCount = 0;
  int preDrawCount = 0;
  for (int i = 0; i < count; ++i) {
    CGuiWidget* widget = FGuiWidgetFactoryInGame(in.Get< uint >(), this, in, pool, version);
    if (widget->GetWidgetTypeID() != 'CAMR' && widget->GetWidgetTypeID() != 'LITE') {
      mWidgets.push_back_unsafe(widget);
      const CGuiWidget::EWidgetUsageFlags flags = widget->GetWidgetUsageFlags();
      if (flags & CGuiWidget::kWUF_Draw) {
        ++drawCount;
      }
      if (flags & CGuiWidget::kWUF_Update) {
        ++updateCount;
      }
      if (flags & CGuiWidget::kWUF_Input) {
        ++inputCount;
      }
      if (flags & CGuiWidget::kWUF_PreDraw) {
        ++preDrawCount;
      }
    }
  }

  mDrawWidgets.reserve(drawCount);
  mUpdateWidgets.reserve(updateCount);
  mInputWidgets.reserve(inputCount);
  mPreDrawWidgets.reserve(preDrawCount);
  for (rstl::vector< CGuiWidget* >::const_iterator it = mWidgets.begin(); it != mWidgets.end();
       ++it) {
    CGuiWidget* widget = *it;
    const CGuiWidget::EWidgetUsageFlags flags = widget->GetWidgetUsageFlags();
    if (flags & CGuiWidget::kWUF_Draw) {
      mDrawWidgets.push_back_unsafe(widget);
    }
    if (flags & CGuiWidget::kWUF_Update) {
      mUpdateWidgets.push_back_unsafe(widget);
    }
    if (flags & CGuiWidget::kWUF_Input) {
      mInputWidgets.push_back_unsafe(widget);
    }
    if (flags & CGuiWidget::kWUF_PreDraw) {
      mPreDrawWidgets.push_back_unsafe(widget);
    }
  }
  Initialize();
}

void CGuiFrame::Initialize() {
  SortDrawOrder();
  CGuiWidget* root = mRootWidget;
  root->RecalcWidgetColor(kTM_ChildrenAndSiblings);
  root->DispatchInitialize();
}

void CGuiFrame::Draw(const CGuiWidgetDrawParms& parms) const {
  CGraphics::SetCullMode(kCM_None);
  CGraphics::ResetGfxStates();
  CGraphics::SetAmbientColor(CColor::White());
  DisableLights();
  mCamera->Draw(parms);
  CGraphics::SetTevOp(kTS_Stage0, CGraphics::kEnvModulate);
  CGraphics::SetBlendMode(kBM_Blend, kBF_SrcAlpha, kBF_InvSrcAlpha, kLO_Clear);
  ApplyLights();

  CGuiModel::BeginDraw();
  for (rstl::vector< CGuiWidget* >::const_iterator it = mPreDrawWidgets.begin();
       it != mPreDrawWidgets.end(); ++it) {
    CGuiWidget* widget = *it;
    if (widget->GetIsVisible()) {
#ifdef TARGET_PC
      PortDrawSpread(this, *mCamera, widget, parms);
#else
      widget->Draw(parms);
#endif
    }
  }
  CGuiModel::EndDraw();
  DisableLights();

  for (rstl::vector< CGuiWidget* >::const_iterator it = mDrawWidgets.begin();
       it != mDrawWidgets.end(); ++it) {
    CGuiWidget* widget = *it;
    if (widget->GetIsVisible()) {
#ifdef TARGET_PC
      PortDrawSpread(this, *mCamera, widget, parms);
#else
      widget->Draw(parms);
#endif
    }
  }
  CGraphics::SetCullMode(kCM_Front);
  CGraphics::SetDepthWriteMode(true, kE_LEqual, true);
}

void CGuiFrame::Update(float dt) {
  for (rstl::vector< CGuiWidget* >::iterator it = mUpdateWidgets.begin();
       it != mUpdateWidgets.end(); ++it) {
    (*it)->Update(dt);
  }

  mActiveLights.clear();
  mAmbientColor = CColor::Black();
  // The original compares against size() after clear(), not the fixed capacity.
  for (int i = 0; i < mLights.size() && i < mActiveLights.size(); ++i) {
    bool added = false;
    CGuiLight* light = mLights[i];
    if (light != nullptr && light->GetIsVisible()) {
      const CColor& color = light->GetModifiedColor();
      if (color.GetRedu8() != 0 || color.GetGreenu8() != 0 || color.GetBlueu8() != 0) {
        mActiveLights.push_back(light->BuildLight());
        added = true;
      }
      mAmbientColor = CColor::Add(mAmbientColor, light->GetAmbientContribution());
    }
    if (!added) {
      mActiveLights.push_back(CLight::BuildPoint(CVector3f::Zero(), CColor::Black()));
    }
  }
}

void CGuiFrame::ProcessUserInput(const CFinalInput& input) {
  rstl::list< CGuiWidget* > activeWidgets;
  for (rstl::vector< CGuiWidget* >::const_iterator it = mInputWidgets.begin();
       it != mInputWidgets.end(); ++it) {
    CGuiWidget* widget = *it;
    if (widget->GetIsActive()) {
      activeWidgets.push_back(widget);
    }
  }
  for (rstl::list< CGuiWidget* >::iterator it = activeWidgets.begin(); it != activeWidgets.end();
       ++it) {
    (*it)->ProcessUserInput(input);
  }
}

bool CGuiFrame::GetIsFinishedLoading() const {
  if (mLoaded) {
    return true;
  }
  for (rstl::vector< CToken >::const_iterator it = mAssets.begin(); it != mAssets.end(); ++it) {
    if (!it->IsLoaded()) {
      return false;
    }
  }
  mLoaded = true;
  return true;
}

void CGuiFrame::AddLight(CGuiLight* light) { mLights[light->GetLightIndex()] = light; }

void CGuiFrame::RemoveLight(CGuiLight* light) {
  if (mLights[light->GetLightIndex()] == light) {
    mLights[light->GetLightIndex()] = nullptr;
  }
}

void CGuiFrame::DisableLights() const { EnableLights(0); }

void CGuiFrame::ApplyLights() const {
  for (int i = 0; i < mActiveLights.size(); ++i) {
    CGraphics::LoadLight(static_cast< ERglLight >(i), mActiveLights[i]);
  }
}

void CGuiFrame::EnableLights(uint mask) const {
  uint enabled = 0;
  for (int i = 0; i < mLights.size(); ++i) {
    if ((mask & (1 << i)) != 0) {
      CGuiLight* light = mLights[i];
      if (light != nullptr && light->GetIsVisible()) {
        enabled |= 1 << i;
      }
    }
  }
  if (enabled == 0) {
    CGraphics::DisableAllLights();
    CGraphics::SetAmbientColor(CColor::White());
  } else {
    CGraphics::SetLightState(enabled);
    CGraphics::SetAmbientColor(mAmbientColor);
  }
}

void CGuiFrame::SortDrawOrder() {
  rstl::sort(mDrawWidgets.begin(), mDrawWidgets.end(), rstl::CWidgetFartherFromCamera());
  rstl::sort(mPreDrawWidgets.begin(), mPreDrawWidgets.end(), rstl::CWidgetFartherFromCamera());
}

CGuiWidget* CGuiFrame::FindWidget(const rstl::string& name) const {
  const short id = mWidgetIds.FindWidgetID(name);
  return id != CGuiWidgetIdDB::kInvalidWidgetId ? FindWidget(id) : nullptr;
}

CGuiWidget* CGuiFrame::FindWidget(short id) const {
  return mRootWidget != nullptr ? mRootWidget->FindWidget(id) : nullptr;
}

void CGuiFrame::SetHeadWidget(CGuiHeadWidget* widget) { mRootWidget = widget; }

void CGuiFrame::SetFrameCamera(CGuiCamera* camera) { mCamera = camera; }

CGuiWidget* CGuiFrame::FindWidget(const char* name) const {
  return FindWidget(rstl::string_l(name));
}

CGuiFrameLoader::CGuiFrameLoader(CAssetId asset, CResFactory& factory, CSimplePool& pool)
: mTag('FRME', asset)
, mPool(&pool)
, mBufferLength(factory.ResourceSize(mTag))
, mBuffer(static_cast< uchar* >(CMemory::Alloc(mBufferLength, IAllocator::kHI_RoundUpLen)))
, mRequest(
      factory.GetResLoader().LoadResourceAsync(mTag, reinterpret_cast< char* >(mBuffer.get()))) {}

CGuiFrameLoader::~CGuiFrameLoader() {}

bool CGuiFrameLoader::IsFinishedLoading() const { return mRequest->IsComplete(); }

CGuiFrame* CGuiFrameLoader::CreateFrame() {
  if (mRequest->IsComplete()) {
    CMemoryInStream in(mBuffer.get(), mBufferLength);
    return rs_new CGuiFrame(in, mPool);
  }
  return nullptr;
}
