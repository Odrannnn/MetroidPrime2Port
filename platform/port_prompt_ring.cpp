#include "port_prompt_ring.h"

#include "GuiSys/CGuiFrame.hpp"
#include "GuiSys/CGuiModel.hpp"
#include "Kyoto/Basics/CBasics.hpp"
#include "Kyoto/Graphics/CCubeMaterial.hpp"
#include "Kyoto/Graphics/CCubeModel.hpp"
#include "Kyoto/Graphics/CGX.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Graphics/CModelFlags.hpp"
#include "Kyoto/Graphics/CTexture.hpp"
#include "port_overlay.h"
#include "port_prompt_ring_math.h"
#include "port_prompts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <unordered_map>
#include <vector>

namespace PortPromptRing {
namespace {
using Layer = CGuiModel::PortIconLayer;

// The ring's radii as fractions of its texture's width (PortPromptRingMath has them in texels of
// 32, shared with the white ring port_prompts.cpp serves): the frame line's own profile bent round.
// The outside edge is the game's ring's diameter; the game's own ring texture, a soft band from
// about 12.6 to 15.1, drawn crisp at 13 to 15.75 read as twice the line's thickness.
constexpr float kInner = PortPromptRingMath::kInner / 32.f;
constexpr float kOuter = PortPromptRingMath::kOuter / 32.f;
// The radius of the round hole in the fill the button sits in: the ring's outer edge, as the game's
// hexagonal notch cut round the whole button. The ring is added onto the screen, so with its whole
// band over the scene it is one colour all the way round (at the inner edge, the band's upper half
// was over the box's fill and came out darker than the lower). The scene shows through the disc and
// the gap round it as through the rest of the button.
constexpr float kHoleRadius = kOuter;
// The anti-aliased edge, either side of the band's radii.
constexpr float kFeather = 0.35f / 32.f;
// Where a frame line ends: just inside the ring's outside edge, where the band is at full strength.
// The line's end is a straight cut between two corners on the circle, so ending on the edge left a
// hairline gap along the curve; the slight overlap (both added, so a touch brighter) is the
// lesser look.
constexpr float kLineEnd = kOuter - kFeather;
constexpr int kSegments = 96;
constexpr float kTwoPi = 6.2831853f;

uint WordOf(float f) {
  union {
    uint u;
    float f;
  } value;
  value.f = f;
  return CBasics::SwapBytes(value.u);
}

float FloatOf(uint raw) {
  union {
    uint u;
    float f;
  } value;
  value.u = CBasics::SwapBytes(raw);
  return value.f;
}

uint IndexAt(const uchar* p) { return (p[0] << 8) | p[1]; }

// One draw command of a surface's display list (PortPromptRingMath::Command) as pointers into it:
// its opcode byte, and where each vertex's position index sits (a big-endian u16, the vertex's
// first).
struct DlCommand {
  uint op;
  std::vector< uchar* > pos;
  uchar* head = nullptr;         // its opcode byte (the vertex format is the low three bits)
  uint stride = 0;               // bytes per vertex
  const void* surface = nullptr; // the surface's header (ModelCommands only)
};

std::vector< DlCommand > ParseDl(const void* data, uint size, uint desc) {
  uchar* dl = const_cast< uchar* >(static_cast< const uchar* >(data));
  std::vector< DlCommand > out;
  for (const PortPromptRingMath::Command& c :
       PortPromptRingMath::ParseDisplayList(dl, size, desc)) {
    DlCommand cmd{c.op, {}, dl + c.head, c.stride};
    for (const uint32_t at : c.vertices) {
      cmd.pos.push_back(dl + at);
    }
    out.push_back(cmd);
  }
  return out;
}

std::vector< DlCommand > ModelCommands(const CCubeModel* model) {
  std::vector< DlCommand > cmds;
  for (int list = 0; list < 2; ++list) {
    CCubeSurface surface = list == 0 ? model->GetNormalSurfaces() : model->GetAlphaSurfaces();
    for (; surface.IsValid(); surface = surface.GetNextSurface()) {
      std::vector< DlCommand > some =
          ParseDl(surface.GetDisplayList(), surface.GetDisplayListSize(),
                  model->GetMaterial(surface).GetVertexDesc());
      for (DlCommand& c : some) {
        c.surface =
            static_cast< const uchar* >(surface.GetDisplayList()) - surface.GetSurfaceHeaderSize();
      }
      cmds.insert(cmds.end(), some.begin(), some.end());
    }
  }
  return cmds;
}

using UvMap = PortPromptRingMath::UvMap;

bool MapOf(const Layer& layer, UvMap& map) {
  return PortPromptRingMath::MapFromCorners(layer.xz, layer.uv, map);
}

// A model carrying the ring texture: its quad there, and the vertices the ring's surfaces use (so
// the frame line's own can be told from them).
struct RingModel {
  bool scanned = false;
  const CTexture* texture = nullptr;
  UvMap map{};
  float depth = 0.f;
  std::set< uint > vertices;
  // The surface the ring texture is on: the ring is drawn through its material, as the game draws
  // its own ring there, and its commands are emptied so the game's 32x32 ring doesn't draw too.
  const void* surface = nullptr;
  std::vector< DlCommand > commands;
  // The ring's mesh, in the model's vertex format (big-endian arrays, a display list of position,
  // normal and texture coordinate indices), built once.
  std::vector< uint > meshPositions;
  std::vector< uint > meshCoords;
  std::vector< uchar > meshList;
  // How much thicker the ring is drawn than the white ring texture's band (see PatchLine), and what
  // BuildMesh was given, to build it again.
  float thickness = 1.f;
  // How much the ring is drawn larger (or smaller) than the box's ring quad, about its centre, so
  // it sits round its disc as in the scan box (PatchLine).
  float scale = 1.f;
  uint normalIndex = 0, vatFormat = 0;
};
std::unordered_map< const CCubeModel*, RingModel > sRingModels;

// The surface's header (what a CCubeSurface wraps), to make one again when drawing.
const void* surfaceData(const CCubeSurface& surface) {
  return static_cast< const uchar* >(surface.GetDisplayList()) - surface.GetSurfaceHeaderSize();
}

// The ring as a mesh: an annulus a little wider than the ring's band (the texture's own soft edge
// draws inside it), with each vertex's texture coordinate where the game's quad would sample the
// ring texture there, so the mesh samples it as the quad did, only round at any stretch. It stays
// inside the texture (16 of 32): the material wraps coordinates, and past the edge it sampled the
// ring from the opposite side.
void BuildMesh(RingModel& ring, uint normalIndex, uint vatFormat) {
  constexpr float kMeshInner = 12.4f / 32.f, kMeshOuter = 15.98f / 32.f;
  ring.normalIndex = normalIndex;
  ring.vatFormat = vatFormat;
  ring.meshPositions.clear();
  ring.meshCoords.clear();
  ring.meshList.clear();
  // A thicker ring (ring.thickness above 1) spreads the same texture band over a wider one on
  // screen, about the band's middle, so it stays on the same circle; then the whole ring is scaled
  // by ring.scale.
  const float middle = (kInner + kOuter) * 0.5f;
  for (int i = 0; i < kSegments; ++i) {
    const float t = kTwoPi * static_cast< float >(i) / kSegments;
    for (const float r : {kMeshInner, kMeshOuter}) {
      const float rp = (middle + (r - middle) * ring.thickness) * ring.scale;
      const float u = 0.5f + std::cos(t) * r, v = 0.5f + std::sin(t) * r;
      const float pu = 0.5f + std::cos(t) * rp, pv = 0.5f + std::sin(t) * rp;
      ring.meshPositions.push_back(WordOf(ring.map.X(pu, pv)));
      ring.meshPositions.push_back(WordOf(ring.depth));
      ring.meshPositions.push_back(WordOf(ring.map.Z(pu, pv)));
      ring.meshCoords.push_back(WordOf(u));
      ring.meshCoords.push_back(WordOf(v));
    }
  }
  // One strip round the annulus: position, normal and texture coordinate indices (big-endian u16).
  const uint count = 2 * (kSegments + 1);
  std::vector< uchar >& dl = ring.meshList;
  dl.push_back(static_cast< uchar >(0x98 | vatFormat));
  dl.push_back(static_cast< uchar >(count >> 8));
  dl.push_back(static_cast< uchar >(count & 0xff));
  for (uint k = 0; k < count; ++k) {
    const uint index = k % (2 * kSegments);
    for (const uint value : {index, normalIndex, index}) {
      dl.push_back(static_cast< uchar >(value >> 8));
      dl.push_back(static_cast< uchar >(value & 0xff));
    }
  }
  while (dl.size() % 32 != 0) {
    dl.push_back(0); // GX display lists are padded to 32 bytes (no-ops)
  }
}

const RingModel* FindRing(const CCubeModel* model) {
  RingModel& ring = sRingModels[model];
  if (ring.scanned) {
    return ring.texture != nullptr ? &ring : nullptr;
  }
  const CCubeModel::ModelInstance& instance = model->GetModelInstance();
  const uint* coords = static_cast< const uint* >(instance.GetTCPointer());
  const uint* positions = static_cast< const uint* >(model->GetPositions());
  if (coords == nullptr || positions == nullptr) {
    return nullptr;
  }
  const uint pairs = instance.GetTCSize() / (2 * sizeof(uint));
  const uint vertices = instance.GetVertexSize() / (3 * sizeof(uint));
  const rstl::vector< TCachedToken< CTexture > >& textures = model->GetTextures();
  for (int list = 0; list < 2; ++list) {
    CCubeSurface surface = list == 0 ? model->GetNormalSurfaces() : model->GetAlphaSurfaces();
    for (; surface.IsValid(); surface = surface.GetNextSurface()) {
      const uint* mat = reinterpret_cast< const uint* >(model->GetMaterial(surface).GetData());
      const uint texCount = CBasics::SwapBytes(mat[1]);
      const CTexture* found = nullptr;
      for (uint t = 0; t < texCount && t < 8; ++t) {
        const uint index = CBasics::SwapBytes(mat[2 + t]);
        if (index >= textures.size()) {
          continue;
        }
        TCachedToken< CTexture >& token = const_cast< TCachedToken< CTexture >& >(textures[index]);
        if (!token.IsLoaded() || !token.TryCache()) {
          return nullptr; // looked at again once the model's textures are in
        }
        if (PortPrompts::IsBoxRing(token.GetObject())) {
          found = token.GetObject();
        }
      }
      if (found == nullptr) {
        continue;
      }
      const uint desc = model->GetMaterial(surface).GetVertexDesc();
      const std::vector< DlCommand > cmds =
          ParseDl(surface.GetDisplayList(), surface.GetDisplayListSize(), desc);
      for (const DlCommand& c : cmds) {
        for (const uchar* p : c.pos) {
          ring.vertices.insert(IndexAt(p));
        }
        ring.commands.push_back(c);
      }
      // The first four vertices of its first command span the texture's map (position index,
      // normal, texture coordinate index: descriptor 0x30f).
      if (ring.texture != nullptr || desc != 0x30f || cmds.empty() || cmds[0].pos.size() < 4) {
        continue;
      }
      Layer layer{};
      bool ok = true;
      for (int v = 0; v < 4 && ok; ++v) {
        const uchar* vertex = cmds[0].pos[v];
        const uint pos = IndexAt(vertex), tc = IndexAt(vertex + 4);
        ok = pos < vertices && tc < pairs;
        if (ok) {
          ring.depth += 0.25f * FloatOf(positions[pos * 3 + 1]);
          layer.xz[v][0] = FloatOf(positions[pos * 3]);
          layer.xz[v][1] = FloatOf(positions[pos * 3 + 2]);
          layer.uv[v][0] = FloatOf(coords[tc * 2]);
          layer.uv[v][1] = FloatOf(coords[tc * 2 + 1]);
        }
      }
      if (ok && MapOf(layer, ring.map)) {
        ring.texture = found;
        ring.surface = surface.GetDisplayList() != nullptr ? surfaceData(surface) : nullptr;
        BuildMesh(ring, IndexAt(cmds[0].pos[0] + 2),
                  static_cast< const uchar* >(surface.GetDisplayList())[0] & 7);
      } else {
        ring.depth = 0.f;
      }
    }
  }
  ring.scanned = true;
  return ring.texture != nullptr ? &ring : nullptr;
}

// A model carrying the boxes' disc texture: where its centre is (the ring is centred on it when the
// widescreen spread moves the two apart).
struct DiscModel {
  bool scanned = false;
  const CTexture* texture = nullptr;
  float cx = 0.f, cy = 0.f, cz = 0.f;
  float unit = 0.f;
};
std::unordered_map< const CCubeModel*, DiscModel > sDiscModels;

const DiscModel* FindDisc(const CGuiModel& widget, const CCubeModel* model) {
  DiscModel& disc = sDiscModels[model];
  if (!disc.scanned) {
    Layer layers[2];
    const int n = widget.PortIconLayers(layers, 2);
    for (int i = 0; i < n && disc.texture == nullptr; ++i) {
      UvMap map;
      if (PortPrompts::IsBoxDisc(layers[i].tex) && MapOf(layers[i], map)) {
        CAABox box = CAABox::MakeNullBox();
        disc.texture = layers[i].tex;
        disc.cx = map.X(0.5f, 0.5f);
        disc.cz = map.Z(0.5f, 0.5f);
        disc.unit = map.Unit();
        disc.cy = widget.PortGetBounds(box) ? box.GetCenterPoint().GetY() : 0.f;
      }
    }
    disc.scanned = n > 0;
  }
  return disc.texture != nullptr ? &disc : nullptr;
}

// A model whose geometry is changed while rings are drawn: position words and display-list bytes,
// with their originals to put back.
struct Patch {
  bool examined = false;
  // Batches a ring's model has waited for its frame's disc to be drawn (the ring is sized to it).
  int waits = 0;
  bool applied = false;
  std::vector< std::pair< uint, uint > > words; // index into the position array, the new word
  std::vector< uint > original;
  std::vector< uchar* > bytes;
  std::vector< uchar > newBytes, oldBytes;
  // The fill round the button's round hole: drawn through the fill's material in place of the
  // commands that cut the game's hexagonal notch (emptied), right after the fill model.
  const void* holeSurface = nullptr;
  std::vector< uint > holePositions;
  std::vector< uchar > holeList;
};
std::unordered_map< const CCubeModel*, Patch > sPatches;
int sApplied = 0; // patches in place: models are looked at until all are put back

// The frame line's ends at a ring: re-placed each frame for a box drawn in slices.
struct LineEnd {
  uint index;
  float side; // -1 left of the ring, +1 right
  float y, z;
  float radius; // the circle its corner goes on, model units
};
std::unordered_map< const CCubeModel*, std::vector< LineEnd > > sLineEnds;

// What each model was when it was looked at: its arrays, display list and textures. A different
// model in the same memory (a frame unloaded and another loaded) is looked at afresh; nothing
// recorded for the old one (pointers into its data) is used or written again.
std::unordered_map< const CCubeModel*, uint64_t > sIdentities;

uint64_t IdentityOf(const CCubeModel* model) {
  uint64_t h = reinterpret_cast< uintptr_t >(model->GetPositions());
  const auto mix = [&h](uint64_t v) { h = (h ^ v) * 0x100000001b3ull; };
  mix(model->GetModelInstance().GetVertexSize());
  mix(model->GetTextures().size());
  const CCubeSurface normal = model->GetNormalSurfaces();
  mix(normal.IsValid() ? reinterpret_cast< uintptr_t >(normal.GetDisplayList()) : 0);
  const CCubeSurface alpha = model->GetAlphaSurfaces();
  mix(alpha.IsValid() ? reinterpret_cast< uintptr_t >(alpha.GetDisplayList()) : 0);
  return h;
}

// Everything recorded for a model, dropped without writing to it (its data is gone, or is another
// model's now).
void Drop(const void* model) {
  sRingModels.erase(static_cast< const CCubeModel* >(model));
  sDiscModels.erase(static_cast< const CCubeModel* >(model));
  sLineEnds.erase(static_cast< const CCubeModel* >(model));
  if (const auto patch = sPatches.find(static_cast< const CCubeModel* >(model));
      patch != sPatches.end()) {
    if (patch->second.applied) {
      --sApplied; // nothing to put back
    }
    sPatches.erase(patch);
  }
}

// A safety net for ModelGone: a different model in a remembered model's memory is looked at afresh.
void Forget(const CCubeModel* model) {
  const uint64_t identity = IdentityOf(model);
  const auto it = sIdentities.find(model);
  const bool known = it != sIdentities.end();
  if (known && it->second == identity) {
    return;
  }
  sIdentities[model] = identity;
  if (known) {
    Drop(model);
  }
}

// A frame's ring as the screen sees it (perspective-divided view space), for its fill.
struct RingOnScreen {
  float x, z, r;
  // Where a frame line's ends go (just inside the ring's outer edge), and whether the line is in
  // another model of the frame (the message box's): PatchLine found none in the ring's.
  float edge;
  bool lineElsewhere;
};
std::unordered_map< const void*, RingOnScreen > sRingOnScreen;

void Project(const CVector3f& v, bool persp, float& sx, float& sz) {
  const float w = persp ? v.GetY() : 1.f;
  sx = v.GetX() / w;
  sz = v.GetZ() / w;
}

// The point of the model's y = 0 plane (under `mv`) that projects to screen point (sx, sz).
bool Unproject(const CTransform4f& mv, bool persp, float sx, float sz, float& x, float& z) {
  const CVector3f o = mv * CVector3f(0.f, 0.f, 0.f);
  const CVector3f a = mv * CVector3f(1.f, 0.f, 0.f) - o;
  const CVector3f b = mv * CVector3f(0.f, 0.f, 1.f) - o;
  // q = o + x a + z b;  q.x - sx w(q) = 0,  q.z - sz w(q) = 0  (w = q.y, or 1 when orthographic)
  const float ay = persp ? a.GetY() : 0.f, by = persp ? b.GetY() : 0.f, oy = persp ? o.GetY() : 1.f;
  const float m00 = a.GetX() - sx * ay, m01 = b.GetX() - sx * by, r0 = sx * oy - o.GetX();
  const float m10 = a.GetZ() - sz * ay, m11 = b.GetZ() - sz * by, r1 = sz * oy - o.GetZ();
  const float det = m00 * m11 - m01 * m10;
  if (std::fabs(det) < 1e-9f) {
    return false;
  }
  x = (r0 * m11 - m01 * r1) / det;
  z = (m00 * r1 - r0 * m10) / det;
  return true;
}

// The frame line, in the ring's own model: its two segments stop short of the ring or run under
// it, each end slanted along the game's ring (thick on the logbook's box). Each corner is moved
// onto the ring's outside edge (kLineEnd), at its own height, so the line meets the ring without
// overlapping it.
bool PatchLine(const CCubeModel* model, RingModel& ring, Patch& patch, float ringPerDisc) {
  const uint* positions = static_cast< const uint* >(model->GetPositions());
  const uint count = model->GetModelInstance().GetVertexSize() / (3 * sizeof(uint));
  const float unit = ring.map.Unit();
  const float cx = ring.map.X(0.5f, 0.5f), cz = ring.map.Z(0.5f, 0.5f);
  const float end = kLineEnd * unit;
  std::vector< LineEnd > found;
  for (uint i = 0; i < count; ++i) {
    const float x = FloatOf(positions[i * 3]), y = FloatOf(positions[i * 3 + 1]),
                z = FloatOf(positions[i * 3 + 2]);
    const float dx = std::fabs(x - cx), dz = z - cz;
    // A line corner: not the ring's, beside it (within its texture's width), at its height.
    if (ring.vertices.count(i) != 0 || dx > 0.625f * unit || dx < 0.19f * unit ||
        std::fabs(dz) >= end) {
      continue;
    }
    found.push_back(LineEnd{i, x < cx ? -1.f : 1.f, y, z, end});
  }
  // A frame "line" that is a thick border (taller than a fifth of the ring texture's width: the
  // logbook's is about a third, the scan box's line under a tenth) shows as a bright stripe in its
  // texture, measured on screen 1.6 times as thick as the ring drawn to the scan box's line: the
  // ring is drawn that much thicker there (BuildMesh), and the line's ends meet its outer edge.
  constexpr float kBorderLineScale = 1.6f;
  float span = 0.f;
  for (const float side : {-1.f, 1.f}) {
    float lo = 1e9f, hi = -1e9f;
    for (const LineEnd& e : found) {
      if (e.side == side) {
        lo = std::min(lo, e.z);
        hi = std::max(hi, e.z);
      }
    }
    span = std::max(span, hi - lo);
  }
  const float middle = (kInner + kOuter) * 0.5f;
  if (ring.thickness == 1.f && ring.scale == 1.f) {
    ring.thickness = span > 0.2f * unit ? kBorderLineScale : 1.f;
    // The ring sized to its disc as in the scan box (the boxes size their ring and disc quads
    // apart: the message box's ring left twice the gap). The logbook's thick ring keeps its size: its
    // band, spread inward, already leaves the scan box's gap on screen.
    if (ring.thickness == 1.f) {
      ring.scale = PortPromptRingMath::FitScale(ringPerDisc);
    }
    if (ring.thickness != 1.f || ring.scale != 1.f) {
      BuildMesh(ring, ring.normalIndex, ring.vatFormat);
    }
  }
  const float edge = (middle + (kLineEnd - middle) * ring.thickness) * ring.scale * unit;
  // A cut end is a straight edge between its corners, which bows into the ring between them: on a
  // thick line (the logbook's border) far enough to overlap the band (brighter, both are added).
  // The corners go out on a wider circle, so the middle of the cut meets the ring's edge: for a
  // chord of half-height h, on radius sqrt(end^2 + h^2).
  for (const float side : {-1.f, 1.f}) {
    float lo = 1e9f, hi = -1e9f;
    for (const LineEnd& e : found) {
      if (e.side == side) {
        lo = std::min(lo, e.z);
        hi = std::max(hi, e.z);
      }
    }
    const float half = hi > lo ? (hi - lo) * 0.5f : 0.f;
    for (LineEnd& e : found) {
      if (e.side == side) {
        e.radius = PortPromptRingMath::ChordRadius(edge, half);
      }
    }
  }
  for (const LineEnd& e : found) {
    const float dz = e.z - cz;
    const float reach =
        dz * dz < e.radius * e.radius ? std::sqrt(e.radius * e.radius - dz * dz) : 0.f;
    patch.words.push_back({e.index * 3, WordOf(cx + e.side * reach)});
    sLineEnds[model].push_back(e);
  }
  return !found.empty();
}

// The frame line in another model of the frame (the message box's ring model is the button alone):
// its ends, found on screen beside the ring at its height (two corners a side), moved along the
// model's x onto the ring's edge as PatchLine does, so the line meets a ring drawn at any size.
bool PatchFrameLine(const CCubeModel* model, const RingOnScreen& ring, const CTransform4f& mv,
                    bool persp, Patch& patch) {
  const uint* positions = static_cast< const uint* >(model->GetPositions());
  const uint count = model->GetModelInstance().GetVertexSize() / (3 * sizeof(uint));
  struct Corner {
    uint index;
    float side, sx, sz;
  };
  std::vector< Corner > corners;
  int sides[2] = {0, 0};
  for (uint i = 0; i < count; ++i) {
    float sx, sz;
    Project(mv * CVector3f(FloatOf(positions[i * 3]), FloatOf(positions[i * 3 + 1]),
                           FloatOf(positions[i * 3 + 2])),
            persp, sx, sz);
    const float dx = sx - ring.x, dz = sz - ring.z;
    if (std::fabs(dz) < 0.5f * ring.r && std::fabs(dx) > 0.8f * ring.r &&
        std::fabs(dx) < 1.6f * ring.r) {
      corners.push_back(Corner{i, dx < 0.f ? -1.f : 1.f, sx, sz});
      ++sides[dx < 0.f ? 0 : 1];
    }
  }
  if (sides[0] != 2 || sides[1] != 2) {
    return false;
  }
  for (const float side : {-1.f, 1.f}) {
    float lo = 1e9f, hi = -1e9f;
    for (const Corner& c : corners) {
      if (c.side == side) {
        lo = std::min(lo, c.sz - ring.z);
        hi = std::max(hi, c.sz - ring.z);
      }
    }
    const float r = PortPromptRingMath::ChordRadius(ring.edge, (hi - lo) * 0.5f);
    for (const Corner& c : corners) {
      if (c.side != side) {
        continue;
      }
      const float dz = c.sz - ring.z;
      const float want = ring.x + side * (dz * dz < r * r ? std::sqrt(r * r - dz * dz) : 0.f);
      // Screen x along the model's x, at the corner's own y and z (a frame's models face the
      // camera: the step is the same anywhere along it).
      const float x = FloatOf(positions[c.index * 3]), y = FloatOf(positions[c.index * 3 + 1]),
                  z = FloatOf(positions[c.index * 3 + 2]);
      float sx1, sz1;
      Project(mv * CVector3f(x + 1.f, y, z), persp, sx1, sz1);
      const float step = sx1 - c.sx;
      if (std::fabs(step) < 1e-9f) {
        return false;
      }
      patch.words.push_back({c.index * 3, WordOf(x + (want - c.sx) / step)});
    }
  }
  return true;
}

// The fill, in another model of the frame: the notch, a hole cut into the fill around the button
// (a hexagon on the game's boxes), made round, whatever the fill's triangles. The commands drawing
// any triangle at the notch are emptied; the area they covered, its outline with the notch closed
// along the box's edge and a disc cut out at the ring's outer edge (kHoleRadius, ring.r on screen),
// is triangulated and drawn in their place through the fill's own material (DrawFillAroundHole).
// The scene shows through the disc, the gap and the ring, as through the original notch, so the
// added ring is one colour all the way round. Worked on screen (where the ring is a circle), the
// points then taken back to the fill's plane. A fill this doesn't fit keeps its notch (logged).
const char* PatchFill(const CCubeModel* model, const RingOnScreen& ring, const CTransform4f& mv,
                      bool persp, Patch& patch) {
  using PortPromptRingMath::Point;
  const uint* positions = static_cast< const uint* >(model->GetPositions());
  const uint count = model->GetModelInstance().GetVertexSize() / (3 * sizeof(uint));
  if (ring.r <= 0.f || count == 0) {
    return "no ring";
  }
  float bottom = 1e9f;
  std::vector< Point > screen(count);
  for (uint i = 0; i < count; ++i) {
    const float x = FloatOf(positions[i * 3]), y = FloatOf(positions[i * 3 + 1]),
                z = FloatOf(positions[i * 3 + 2]);
    bottom = std::min(bottom, z);
    Project(mv * CVector3f(x, y, z), persp, screen[i].x, screen[i].z);
  }
  const Point centre{ring.x, ring.z};
  // The notch's own vertices: inside the box (above its edge), round the ring.
  const auto notchVertex = [&](uint i) {
    return FloatOf(positions[i * 3 + 2]) > bottom + 1e-3f &&
           std::hypot(screen[i].x - centre.x, screen[i].z - centre.z) < 1.6f * ring.r;
  };
  // The commands drawing a triangle at the notch, all on one surface (one material).
  const std::vector< DlCommand > cmds = ModelCommands(model);
  std::vector< const DlCommand* > affected;
  std::vector< std::array< uint32_t, 3 > > tris;
  for (const DlCommand& c : cmds) {
    std::vector< uint32_t > indices;
    for (const uchar* v : c.pos) {
      indices.push_back(IndexAt(v));
    }
    bool touches = false;
    for (const uint32_t i : indices) {
      touches = touches || (i < count && notchVertex(i));
    }
    if (!touches) {
      continue;
    }
    if (!affected.empty() && c.surface != affected[0]->surface) {
      return "notch over two materials";
    }
    affected.push_back(&c);
    PortPromptRingMath::CommandTriangles(c.op, indices, tris);
  }
  if (affected.empty()) {
    return "no notch";
  }
  for (const auto& t : tris) {
    for (const uint32_t i : t) {
      if (i >= count) {
        return "notch triangles";
      }
    }
  }
  // Their outline, the notch (one run of its vertices along it) closed along the box's edge.
  const std::vector< uint32_t > loop = PortPromptRingMath::BoundaryLoop(tris);
  if (loop.empty()) {
    return "notch outline";
  }
  size_t runStart = loop.size(), runs = 0;
  for (size_t k = 0; k < loop.size(); ++k) {
    if (notchVertex(loop[k]) && !notchVertex(loop[(k + loop.size() - 1) % loop.size()])) {
      runStart = k;
      ++runs;
    }
  }
  if (runs != 1) {
    return "notch outline";
  }
  std::vector< Point > outline;
  for (size_t m = 0; m < loop.size(); ++m) {
    const uint32_t i = loop[(runStart + m) % loop.size()];
    if (!notchVertex(i)) {
      outline.push_back(screen[i]);
    }
  }
  // The disc cut out, the rest triangulated.
  constexpr int kHoleSegments = 48;
  const std::vector< Point > cut =
      PortPromptRingMath::CutDisc(outline, centre, ring.r, kHoleSegments);
  if (cut.empty()) {
    return "ring off the edge";
  }
  std::vector< std::array< uint32_t, 3 > > fill = PortPromptRingMath::EarClip(cut);
  if (fill.empty()) {
    return "triangulation";
  }
  // Wound as the game's triangles were (in case the material culls).
  const auto winding = [](const Point& a, const Point& b, const Point& c) {
    return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
  };
  for (const auto& t : tris) {
    const float w = winding(screen[t[0]], screen[t[1]], screen[t[2]]);
    if (w != 0.f) {
      if ((w > 0.f) != (winding(cut[fill[0][0]], cut[fill[0][1]], cut[fill[0][2]]) > 0.f)) {
        for (auto& f : fill) {
          std::swap(f[1], f[2]);
        }
      }
      break;
    }
  }
  for (const Point& p : cut) {
    float x, z;
    if (!Unproject(mv, persp, p.x, p.z, x, z)) {
      return "projection";
    }
    patch.holePositions.insert(patch.holePositions.end(), {WordOf(x), WordOf(0.f), WordOf(z)});
  }
  // A triangle list in the fill's vertex format: each vertex as the first affected command's
  // first (its normal and any other indices), with its own position index.
  const DlCommand& model0 = *affected[0];
  std::vector< uchar >& dl = patch.holeList;
  const uint vertices = static_cast< uint >(fill.size() * 3);
  dl.push_back(static_cast< uchar >(0x90 | (*model0.head & 7)));
  dl.push_back(static_cast< uchar >(vertices >> 8));
  dl.push_back(static_cast< uchar >(vertices & 0xff));
  for (const auto& t : fill) {
    for (const uint32_t k : t) {
      const size_t at = dl.size();
      dl.insert(dl.end(), model0.pos[0], model0.pos[0] + model0.stride);
      dl[at] = static_cast< uchar >(k >> 8);
      dl[at + 1] = static_cast< uchar >(k & 0xff);
    }
  }
  while (dl.size() % 32 != 0) {
    dl.push_back(0);
  }
  // The game's commands there draw nothing: every vertex on its command's first position.
  for (const DlCommand* c : affected) {
    for (uchar* vertex : c->pos) {
      for (int b = 0; b < 2; ++b) {
        patch.bytes.push_back(vertex + b);
        patch.oldBytes.push_back(vertex[b]);
        patch.newBytes.push_back(c->pos[0][b]);
      }
    }
  }
  patch.holeSurface = model0.surface;
  return nullptr;
}

void Apply(const CCubeModel* model, Patch& patch, bool active) {
  if (patch.words.empty() && patch.bytes.empty()) {
    return;
  }
  // The box loaded again in the same memory (the same model, the game's data fresh): what was
  // written is gone though the patch says applied. One changed byte or word tells.
  if (patch.applied) {
    bool intact = true;
    for (size_t k = 0; k < patch.bytes.size(); ++k) {
      if (patch.newBytes[k] != patch.oldBytes[k]) {
        intact = *patch.bytes[k] == patch.newBytes[k];
        break;
      }
    }
    if (patch.bytes.empty()) {
      const uint* positions = static_cast< const uint* >(model->GetPositions());
      for (size_t k = 0; k < patch.words.size(); ++k) {
        if (patch.words[k].second != patch.original[k]) {
          intact = positions[patch.words[k].first] != patch.original[k];
          break;
        }
      }
    }
    if (!intact) {
      patch.applied = false;
      --sApplied;
    }
  }
  if (patch.applied == active) {
    return;
  }
  uint* positions = const_cast< uint* >(static_cast< const uint* >(model->GetPositions()));
  for (size_t k = 0; k < patch.words.size(); ++k) {
    positions[patch.words[k].first] = active ? patch.words[k].second : patch.original[k];
  }
  for (size_t k = 0; k < patch.bytes.size(); ++k) {
    *patch.bytes[k] = active ? patch.newBytes[k] : patch.oldBytes[k];
  }
  patch.applied = active;
  sApplied += active ? 1 : -1;
}

// A ring to draw at the end of the batch, over the frame's models.
struct PendingRing {
  const CGuiModel* widget;
  const CCubeModel* model;
  const void* frame;
  const RingModel* ring;
  CColor color;
  CTransform4f xf;
  // The scissor the model drew under: the widescreen spread draws a stretched model in slices, each
  // clipped to its columns.
  u32 scissor[4];
};
std::vector< PendingRing > sPending;
// Where each frame's disc was drawn this batch, world space.
std::unordered_map< const void*, CVector3f > sDiscCentres;
// Each frame's disc widget, its quad's size and the batch it was drawn in: a ring drawn before its
// disc in the frame finds it from an earlier batch.
struct DiscWidget {
  const CGuiModel* widget;
  float unit;
  uint batch;
};
std::unordered_map< const void*, DiscWidget > sDiscWidgets;
uint sBatch = 0;

// The world length of a widget's model unit (along its z axis, the boxes' up).
float WorldScale(const CGuiModel& widget) {
  const CTransform4f& xf = widget.GetWorldTransform();
  const CVector3f a = xf * CVector3f(0.f, 0.f, 1.f) - xf * CVector3f(0.f, 0.f, 0.f);
  return std::sqrt(a.GetX() * a.GetX() + a.GetY() * a.GetY() + a.GetZ() * a.GetZ());
}

// The ring, drawn as the game draws its own: the ring mesh through the material of the surface the
// ring texture is on, with the flags the game draws the box with (added, at the model's tint).
// The material binds the ring texture, served as a clean white ring, and does the rest as the
// game would: its colour stages, scale and blend, so the ring is the colour the game's ring is in
// every box, blinks and fades with the model, and a black (hidden) copy adds nothing.
void DrawRing(const RingModel& ring, const CCubeModel* model, const CColor& color) {
  const CCubeSurface surface(ring.surface);
  CModelFlags flags = CModelFlags::Additive(color).DepthCompareUpdate(false, false);
  flags = CModelFlags(flags, flags.GetOtherFlags() | CModelFlags::kF_Unknown400);
  CCubeMaterial::KillCachedViewDepState();
  CCubeMaterial::ResetCachedMaterials();
  CCubeMaterial::ResetTransparencyKColor();
  model->SetArraysCurrent();
  CGX::SetArray(GX_VA_POS, ring.meshPositions.data(),
                static_cast< uint >(ring.meshPositions.size() * sizeof(uint)), sizeof(CVector3f));
  CGX::SetArray(GX_VA_TEX0, ring.meshCoords.data(),
                static_cast< uint >(ring.meshCoords.size() * sizeof(uint)), sizeof(CVector2f));
  model->GetMaterial(surface).SetCurrent(flags, surface, *model);
  CGX::CallDisplayList(ring.meshList.data(), ring.meshList.size());
  // The next model sets its own arrays and material up again.
  CCubeMaterial::ResetCachedMaterials();
}

const CCubeModel* ModelOf(const CGuiModel& widget) {
  const CCubeModel* model = widget.PortCubeModel();
  return model != nullptr && model->GetPositions() != nullptr ? model : nullptr;
}

int ScreenX(const CTransform4f& invView, const CVector3f& world, bool& ok) {
  const CVector3f eye = invView * world;
  ok = eye.GetY() > 0.f;
  return ok ? CGraphics::GetViewport().mLeft + CGraphics::ProjectPoint(eye).GetX() : 0;
}

bool InScissor(int x, const u32* scissor, int slack) {
  return x >= static_cast< int >(scissor[0]) - slack &&
         x < static_cast< int >(scissor[0] + scissor[2]) + slack;
}
} // namespace

void ModelGone(const void* model) {
  if (sIdentities.erase(static_cast< const CCubeModel* >(model)) != 0) {
    Drop(model);
  }
}

void BeforeDraw(const CGuiModel& widget) {
  const bool active = PortPrompts::AnyDrawnRing();
  if (!active && sApplied == 0) {
    return;
  }
  const CCubeModel* model = ModelOf(widget);
  if (model == nullptr) {
    return;
  }
  Forget(model);
  const void* frame = widget.GetParentFrame();
  const bool persp = CGraphics::GetProjectionState().IsPerspective();
  const CTransform4f mv = CGraphics::GetViewMatrix().GetQuickInverse() * widget.GetWorldTransform();
  const RingModel* ring = active ? FindRing(model) : nullptr;
  if (active && widget.PortDrawFlags() == CGuiModel::kGMDF_Additive) {
    if (const DiscModel* disc = FindDisc(widget, model);
        disc != nullptr && PortPrompts::IsTealDisc(disc->texture)) {
      sDiscCentres[frame] = CGraphics::GetModelMatrix() * CVector3f(disc->cx, disc->cy, disc->cz);
      sDiscWidgets[frame] = DiscWidget{&widget, disc->unit, sBatch};
    }
  }
  Patch& patch = sPatches[model];
  if (active && !patch.examined) {
    const char* kept = nullptr;
    if (ring != nullptr) {
      // The ring is fitted to the frame's disc, which some boxes draw after the ring's model: a
      // batch or two for it, else the ring as the box sizes it.
      const auto seen = sDiscWidgets.find(frame);
      const bool discKnown = seen != sDiscWidgets.end() && sBatch - seen->second.batch <= 8;
      if (!discKnown && patch.waits < 2) {
        ++patch.waits;
        return;
      }
      patch.examined = true;
      // Both widgets' transforms now, so a box's opening zoom (on their frame) cancels out.
      const float discWorld = discKnown ? seen->second.unit * WorldScale(*seen->second.widget) : 0.f;
      const bool line = PatchLine(model, sRingModels[model], patch,
                                  discWorld > 0.f ? ring->map.Unit() * WorldScale(widget) / discWorld : 0.f);
      const float unit = ring->map.Unit() * ring->scale;
      const float cx = ring->map.X(0.5f, 0.5f), cz = ring->map.Z(0.5f, 0.5f);
      float x0, z0, x1, z1;
      Project(mv * CVector3f(cx, ring->depth, cz), persp, x0, z0);
      Project(mv * CVector3f(cx + kHoleRadius * unit, ring->depth, cz), persp, x1, z1);
      const float r = std::sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
      const float middle = (kInner + kOuter) * 0.5f;
      const float edge = (middle + (kLineEnd - middle) * ring->thickness) / kHoleRadius * r;
      sRingOnScreen[frame] = RingOnScreen{x0, z0, r, edge, !line};
      // The game's own ring quad draws nothing while the ring is drawn: each of its commands'
      // vertices all on its first position, so every triangle is empty.
      for (const DlCommand& c : ring->commands) {
        for (uchar* vertex : c.pos) {
          for (int b = 0; b < 2; ++b) {
            patch.bytes.push_back(vertex + b);
            patch.oldBytes.push_back(vertex[b]);
            patch.newBytes.push_back(c.pos[0][b]);
          }
        }
      }
    } else if (widget.PortDrawFlags() == CGuiModel::kGMDF_Additive &&
               sRingOnScreen.count(frame) != 0 && sRingOnScreen[frame].lineElsewhere) {
      patch.examined = true;
      if (!PatchFrameLine(model, sRingOnScreen[frame], mv, persp, patch)) {
        patch.words.clear();
      }
    } else if (widget.PortDrawFlags() == CGuiModel::kGMDF_Alpha &&
               sRingOnScreen.count(frame) != 0) {
      patch.examined = true;
      kept = PatchFill(model, sRingOnScreen[frame], mv, persp, patch);
      if (kept != nullptr) {
        patch.words.clear();
        patch.bytes.clear();
        patch.newBytes.clear();
        patch.oldBytes.clear();
        patch.holeSurface = nullptr;
        if (std::strcmp(kept, "no notch") != 0) { // another fill of the frame, nowhere near it
          std::fprintf(stderr, "[prompts] drawn ring: a box keeps its notch (%s)\n", kept);
        }
      }
    }
    const uint* positions = static_cast< const uint* >(model->GetPositions());
    for (const auto& [index, word] : patch.words) {
      patch.original.push_back(positions[index]);
    }
  }
  Apply(model, patch, active);
}

// The fill round the button's round hole, right after the fill model, through the fill surface's
// material and with the flags the game draws the fill with (alpha-blended at the model's colour),
// so it is the fill exactly, as its emptied commands would have drawn it.
void DrawFillAroundHole(const CCubeModel* model, const Patch& patch, const CColor& color) {
  const CCubeSurface surface(patch.holeSurface);
  CModelFlags flags = CModelFlags::AlphaBlended(color).DepthCompareUpdate(false, false);
  flags = CModelFlags(flags, flags.GetOtherFlags() | CModelFlags::kF_Unknown400);
  CCubeMaterial::KillCachedViewDepState();
  CCubeMaterial::ResetCachedMaterials();
  CCubeMaterial::ResetTransparencyKColor();
  model->SetArraysCurrent();
  CGX::SetArray(GX_VA_POS, patch.holePositions.data(),
                static_cast< uint >(patch.holePositions.size() * sizeof(uint)), sizeof(CVector3f));
  model->GetMaterial(surface).SetCurrent(flags, surface, *model);
  CGX::CallDisplayList(patch.holeList.data(), patch.holeList.size());
  CCubeMaterial::ResetCachedMaterials();
}

bool AfterDraw(const CGuiModel& widget, const CColor& color) {
  if (!PortPrompts::AnyDrawnRing()) {
    return false;
  }
  const CCubeModel* model = ModelOf(widget);
  if (model == nullptr) {
    return false;
  }
  if (const auto fill = sPatches.find(model);
      fill != sPatches.end() && fill->second.applied && fill->second.holeSurface != nullptr) {
    DrawFillAroundHole(model, fill->second, color);
    return true;
  }
  const auto it = sRingModels.find(model);
  if (it == sRingModels.end() || it->second.texture == nullptr ||
      !PortPrompts::DrawsRing(it->second.texture)) {
    return false;
  }
  if (const auto patch = sPatches.find(model); patch == sPatches.end() || !patch->second.examined) {
    return false; // still waiting for the disc (BeforeDraw)
  }
  PendingRing pending{
      &widget, model, widget.GetParentFrame(), &it->second, color, CGraphics::GetModelMatrix(), {}};
  GXGetScissor(&pending.scissor[0], &pending.scissor[1], &pending.scissor[2], &pending.scissor[3]);
  sPending.push_back(pending);
  return false;
}

bool EndBatch() {
  ++sBatch;
  if (sPending.empty()) {
    sDiscCentres.clear();
    return false;
  }
  u32 scissor[4];
  GXGetScissor(&scissor[0], &scissor[1], &scissor[2], &scissor[3]);
  const CTransform4f invView = CGraphics::GetViewMatrix().GetQuickInverse();
  for (size_t i = 0; i < sPending.size(); ++i) {
    const PendingRing& first = sPending[i];
    // Each widget once (a sliced one queued a ring per slice): a few entries, so a look back.
    bool seen = false;
    for (size_t j = 0; j < i && !seen; ++j) {
      seen = sPending[j].widget == first.widget;
    }
    if (seen) {
      continue;
    }
    const RingModel& ring = *first.ring;
    const CVector3f centre(ring.map.X(0.5f, 0.5f), ring.depth, ring.map.Z(0.5f, 0.5f));
    size_t copies = 0;
    for (const PendingRing& other : sPending) {
      copies += other.widget == first.widget ? 1 : 0;
    }
    if (copies == 1) {
      GXSetScissor(first.scissor[0], first.scissor[1], first.scissor[2], first.scissor[3]);
      CGraphics::SetModelMatrix(first.xf);
      DrawRing(ring, first.model, first.color);
      continue;
    }
    // Drawn in slices by the widescreen spread, each stretched its own way: the ring stays round,
    // drawn once with the model's own (unstretched) transform, moved to the slice that holds its
    // centre, and onto the button's disc where the frame drew one (the spread moves that small
    // widget whole, a few pixels from where the slices put the centre; matched on screen, at the
    // ring's depth).
    const PendingRing* chosen = &first;
    for (const PendingRing& piece : sPending) {
      bool ok;
      const int x = piece.widget == first.widget ? ScreenX(invView, piece.xf * centre, ok) : 0;
      if (piece.widget == first.widget && ok && InScissor(x, piece.scissor, 0)) {
        chosen = &piece;
        break;
      }
    }
    CVector3f target = chosen->xf * centre;
    if (const auto disc = sDiscCentres.find(chosen->frame); disc != sDiscCentres.end()) {
      const CVector3f eyeRing = invView * target, eyeDisc = invView * disc->second;
      if (eyeDisc.GetY() > 0.f && eyeRing.GetY() > 0.f) {
        target = CGraphics::GetViewMatrix() * (eyeDisc * (eyeRing.GetY() / eyeDisc.GetY()));
      }
    }
    const CTransform4f& world = first.widget->GetWorldTransform();
    const CTransform4f ringXf = CTransform4f::Translate(target - world * centre) * world;
    GXSetScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    CGraphics::SetModelMatrix(ringXf);
    DrawRing(ring, first.model, chosen->color);
    // The frame line's ends, for the next frame: where the round ring's band is on screen, each
    // through the slice that draws it (the slices stretch the line, not the ring).
    const auto ends = sLineEnds.find(first.model);
    if (ends == sLineEnds.end()) {
      continue;
    }
    uint* positions = const_cast< uint* >(static_cast< const uint* >(first.model->GetPositions()));
    for (const LineEnd& end : ends->second) {
      const float dz = end.z - centre.GetZ();
      const float r = end.radius;
      const float reach = dz * dz < r * r ? std::sqrt(r * r - dz * dz) : 0.f;
      const CVector3f want = ringXf * CVector3f(centre.GetX() + end.side * reach, end.y, end.z);
      for (const PendingRing& piece : sPending) {
        if (piece.widget != first.widget) {
          continue;
        }
        const CVector3f origin = piece.xf * CVector3f(centre.GetX(), end.y, end.z);
        const CVector3f col = piece.xf * CVector3f(centre.GetX() + 1.f, end.y, end.z) - origin;
        const float len2 =
            col.GetX() * col.GetX() + col.GetY() * col.GetY() + col.GetZ() * col.GetZ();
        if (len2 <= 0.f) {
          continue;
        }
        const CVector3f delta = want - origin;
        const float x = centre.GetX() + (delta.GetX() * col.GetX() + delta.GetY() * col.GetY() +
                                         delta.GetZ() * col.GetZ()) /
                                            len2;
        bool ok;
        const int sx = ScreenX(invView, piece.xf * CVector3f(x, end.y, end.z), ok);
        if (ok && InScissor(sx, piece.scissor, 1)) {
          positions[end.index * 3] = WordOf(x);
          break;
        }
      }
    }
  }
  GXSetScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
  sPending.clear();
  sDiscCentres.clear();
  return true;
}
} // namespace PortPromptRing
