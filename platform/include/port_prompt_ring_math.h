#pragma once

// The drawn button ring's arithmetic (port_prompt_ring.cpp, port_prompts.cpp), with no game or
// Aurora types so it can be tested on its own (tests/port_prompt_ring.cpp): the ring's shape in its
// texture, display-list commands, the map from a quad's texture coordinates to its model's plane,
// the line ends' circle, the round hole cut into the box's fill, and the ring's fit to its disc.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace PortPromptRingMath {

// The ring, in its texture's texels of 32 across: the frame line bent round. The scan box's line
// quad is 2.4 texels tall and its texture fades in over about 0.6 texel at each edge (measured on
// screen); the outside edge is the game's ring's diameter.
constexpr float kOuter = 15.75f;
constexpr float kLineWidth = 2.4f;
constexpr float kLineFade = 0.6f;
constexpr float kInner = kOuter - kLineWidth;
// The teal disc's radius in its own texture (texels of 32): half a texel inside the ring's inner
// edge when the two quads are the same size.
constexpr float kDiscRadius = 12.5f;

// The ring's quad against the disc's, in world units, in the scan box: there the disc sits just
// inside the ring with a narrow even gap. The message box's ring quad is 1.21 times its disc's
// (the gap more than twice as wide); a ring in such a box is scaled to the scan box's.
constexpr float kRingPerDisc = 1.0374f;

// How much to scale a ring whose quad is `ratio` times its disc's to match the scan box's: 1 within
// half a percent (the scan box itself), and 1 past a quarter either way (something measured wrong).
inline float FitScale(float ratio) {
  if (!(ratio > 0.f)) {
    return 1.f;
  }
  const float s = kRingPerDisc / ratio;
  return std::fabs(s - 1.f) < 0.005f || s < 0.75f || s > 1.25f ? 1.f : s;
}

// How much of the ring covers a point d texels from the texture's centre: 0 outside the band, 1 in
// its core, ramping over kLineFade at each edge (the white ring texture's value there).
inline float BandCoverage(float d) {
  return std::clamp(std::min(d - kInner, kOuter - d) / kLineFade, 0.f, 1.f);
}

// One draw command of a GX display list: its opcode byte's offset, its primitive, and each vertex's
// offset. A vertex is a big-endian u16 index for every indexed attribute (two bits of the vertex
// descriptor each, the position's first).
struct Command {
  uint32_t head = 0;
  uint32_t op = 0; // 0x80 quads, 0x90 triangles, 0x98 strip, 0xa0 fan
  uint32_t stride = 0;
  std::vector< uint32_t > vertices;
};

inline uint32_t VertexStride(uint32_t desc) {
  uint32_t stride = 0;
  for (int a = 0; a < 16; ++a) {
    if (((desc >> (a * 2)) & 3) != 0) {
      stride += 2;
    }
  }
  return stride;
}

// The commands at the start of a display list, up to the first that isn't one of those primitives
// (the padding) or runs past its end.
inline std::vector< Command > ParseDisplayList(const uint8_t* dl, uint32_t size, uint32_t desc) {
  std::vector< Command > out;
  const uint32_t stride = VertexStride(desc);
  uint32_t at = 0;
  while (stride != 0 && at + 3 <= size) {
    const uint32_t op = dl[at] & 0xf8;
    if (op != 0x80 && op != 0x90 && op != 0x98 && op != 0xa0) {
      break;
    }
    const uint32_t n = (uint32_t(dl[at + 1]) << 8) | dl[at + 2];
    if (at + 3 + n * stride > size) {
      break;
    }
    Command cmd{at, op, stride, {}};
    for (uint32_t v = 0; v < n; ++v) {
      cmd.vertices.push_back(at + 3 + v * stride);
    }
    out.push_back(cmd);
    at += 3 + n * stride;
  }
  return out;
}

// The affine map from texture coordinates (u, v) to a model's x/z plane, from a quad's corners.
struct UvMap {
  float x0 = 0.f, z0 = 0.f, u0 = 0.f, v0 = 0.f;
  float xu = 0.f, xv = 0.f, zu = 0.f, zv = 0.f;
  float X(float u, float v) const { return x0 + xu * (u - u0) + xv * (v - v0); }
  float Z(float u, float v) const { return z0 + zu * (u - u0) + zv * (v - v0); }
  // Model units per texture width, along u.
  float Unit() const { return std::sqrt(xu * xu + zu * zu); }
};

// From corners 0, 1 and 2 (or 0, 1 and 3 when those three are in a line in texture space).
inline bool MapFromCorners(const float xz[4][2], const float uv[4][2], UvMap& map) {
  int c = 2;
  const float du1 = uv[1][0] - uv[0][0], dv1 = uv[1][1] - uv[0][1];
  float du2 = uv[c][0] - uv[0][0], dv2 = uv[c][1] - uv[0][1];
  float det = du1 * dv2 - du2 * dv1;
  if (std::fabs(det) < 1e-6f) {
    c = 3;
    du2 = uv[c][0] - uv[0][0];
    dv2 = uv[c][1] - uv[0][1];
    det = du1 * dv2 - du2 * dv1;
    if (std::fabs(det) < 1e-6f) {
      return false;
    }
  }
  const float ex1 = xz[1][0] - xz[0][0], ez1 = xz[1][1] - xz[0][1];
  const float ex2 = xz[c][0] - xz[0][0], ez2 = xz[c][1] - xz[0][1];
  const float i00 = dv2 / det, i01 = -du2 / det, i10 = -dv1 / det, i11 = du1 / det;
  map = UvMap{xz[0][0],
              xz[0][1],
              uv[0][0],
              uv[0][1],
              ex1 * i00 + ex2 * i10,
              ex1 * i01 + ex2 * i11,
              ez1 * i00 + ez2 * i10,
              ez1 * i01 + ez2 * i11};
  return true;
}

// A straight cut between two corners on a circle bows inward between them: for its middle to lie
// on radius `edge`, corners a half-height `half` apart go on this radius.
inline float ChordRadius(float edge, float half) { return std::sqrt(edge * edge + half * half); }

// --- The fill's round hole: an outline, the notch closed, a circle cut out, triangulated.
// ----------

struct Point {
  float x = 0.f, z = 0.f;
};

// The triangles a GX command draws, as index triples (its vertices' indices, in its order).
inline void CommandTriangles(uint32_t op, const std::vector< uint32_t >& v,
                             std::vector< std::array< uint32_t, 3 > >& out) {
  const size_t n = v.size();
  if (op == 0x90) {
    for (size_t k = 0; k + 2 < n; k += 3) {
      out.push_back({v[k], v[k + 1], v[k + 2]});
    }
  } else if (op == 0x98) {
    for (size_t k = 0; k + 2 < n; ++k) {
      out.push_back({v[k], v[k + 1], v[k + 2]});
    }
  } else if (op == 0xa0) {
    for (size_t k = 1; k + 1 < n; ++k) {
      out.push_back({v[0], v[k], v[k + 1]});
    }
  } else if (op == 0x80) {
    for (size_t k = 0; k + 3 < n; k += 4) {
      out.push_back({v[k], v[k + 1], v[k + 2]});
      out.push_back({v[k], v[k + 2], v[k + 3]});
    }
  }
}

// The outline of the area some triangles cover: the edges only one of them has, joined into one
// loop of vertex indices. Empty when they don't make exactly one (a hole in them, or apart).
inline std::vector< uint32_t > BoundaryLoop(const std::vector< std::array< uint32_t, 3 > >& tris) {
  std::vector< std::pair< uint32_t, uint32_t > > edges;
  for (const auto& t : tris) {
    if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) {
      continue; // an empty triangle
    }
    for (int k = 0; k < 3; ++k) {
      const uint32_t a = std::min(t[k], t[(k + 1) % 3]), b = std::max(t[k], t[(k + 1) % 3]);
      edges.push_back({a, b});
    }
  }
  std::sort(edges.begin(), edges.end());
  std::vector< std::pair< uint32_t, uint32_t > > outline;
  for (size_t k = 0; k < edges.size();) {
    size_t same = k;
    while (same < edges.size() && edges[same] == edges[k]) {
      ++same;
    }
    if (same - k == 1) {
      outline.push_back(edges[k]);
    }
    k = same;
  }
  if (outline.size() < 3) {
    return {};
  }
  std::vector< uint32_t > loop{outline[0].first, outline[0].second};
  std::vector< bool > used(outline.size(), false);
  used[0] = true;
  for (size_t step = 1; step < outline.size(); ++step) {
    const uint32_t at = loop.back();
    bool found = false;
    for (size_t k = 0; k < outline.size() && !found; ++k) {
      if (used[k] || (outline[k].first != at && outline[k].second != at)) {
        continue;
      }
      used[k] = true;
      found = true;
      const uint32_t next = outline[k].first == at ? outline[k].second : outline[k].first;
      if (step + 1 == outline.size()) {
        if (next != loop.front()) {
          return {};
        }
      } else {
        loop.push_back(next);
      }
    }
    if (!found) {
      return {};
    }
  }
  return loop.size() == outline.size() ? loop : std::vector< uint32_t >{};
}

inline float SignedArea(const std::vector< Point >& poly) {
  float a = 0.f;
  for (size_t k = 0; k < poly.size(); ++k) {
    const Point& p = poly[k];
    const Point& q = poly[(k + 1) % poly.size()];
    a += p.x * q.z - q.x * p.z;
  }
  return a * 0.5f;
}

inline bool PointInPolygon(const std::vector< Point >& poly, Point p) {
  bool in = false;
  for (size_t k = 0, j = poly.size() - 1; k < poly.size(); j = k++) {
    const Point& a = poly[k];
    const Point& b = poly[j];
    if ((a.z > p.z) != (b.z > p.z) && p.x < (b.x - a.x) * (p.z - a.z) / (b.z - a.z) + a.x) {
      in = !in;
    }
  }
  return in;
}

// A polygon with a disc (centre c, radius r) cut out of it, where the disc crosses its outline
// exactly twice (in, then out: on one edge or two): the outline to where it enters the disc, an arc
// of `segments` round the disc's inside-the-polygon side, the outline on from where it leaves.
// Points inside the disc are dropped. Empty when the disc crosses it otherwise.
inline std::vector< Point > CutDisc(const std::vector< Point >& poly, Point c, float r,
                                    int segments) {
  const size_t n = poly.size();
  const auto inside = [&](Point p) { return std::hypot(p.x - c.x, p.z - c.z) < r; };
  size_t start = n; // a point outside the disc to start from
  for (size_t k = 0; k < n; ++k) {
    if (!inside(poly[k])) {
      start = k;
      break;
    }
  }
  if (start == n) {
    return {};
  }
  std::vector< Point > out;
  Point enter{};
  int crossings = 0;
  bool in = false;
  for (size_t m = 0; m < n; ++m) {
    const Point a = poly[(start + m) % n], b = poly[(start + m + 1) % n];
    if (!in) {
      out.push_back(a);
    }
    // The edge's crossings with the circle, in order along it.
    const float dx = b.x - a.x, dz = b.z - a.z;
    const float fx = a.x - c.x, fz = a.z - c.z;
    const float A = dx * dx + dz * dz, B = 2.f * (fx * dx + fz * dz), C = fx * fx + fz * fz - r * r;
    const float disc = B * B - 4.f * A * C;
    if (A <= 0.f || disc <= 0.f) {
      continue;
    }
    const float root = std::sqrt(disc);
    for (const float t : {(-B - root) / (2.f * A), (-B + root) / (2.f * A)}) {
      if (t <= 0.f || t > 1.f) {
        continue;
      }
      const Point p{a.x + dx * t, a.z + dz * t};
      if (!in) {
        enter = p;
        out.push_back(p);
        in = true;
      } else {
        // The arc from enter to here, round the side inside the polygon.
        const float a0 = std::atan2(enter.z - c.z, enter.x - c.x);
        float span = std::atan2(p.z - c.z, p.x - c.x) - a0;
        while (span > 3.14159265f) {
          span -= 6.2831853f;
        }
        while (span < -3.14159265f) {
          span += 6.2831853f;
        }
        const float mid = a0 + span * 0.5f;
        if (!PointInPolygon(poly, {c.x + r * std::cos(mid), c.z + r * std::sin(mid)})) {
          span += span > 0.f ? -6.2831853f : 6.2831853f; // the other way round
        }
        for (int k = 1; k < segments; ++k) {
          const float ang = a0 + span * static_cast< float >(k) / segments;
          out.push_back({c.x + r * std::cos(ang), c.z + r * std::sin(ang)});
        }
        out.push_back(p);
        in = false;
      }
      ++crossings;
    }
  }
  return crossings == 2 && !in ? out : std::vector< Point >{};
}

// A simple polygon cut into triangles (ear clipping), as index triples into it, each wound as the
// polygon is. Empty when it can't be (not simple).
inline std::vector< std::array< uint32_t, 3 > > EarClip(const std::vector< Point >& poly) {
  std::vector< std::array< uint32_t, 3 > > out;
  const size_t n = poly.size();
  if (n < 3) {
    return out;
  }
  const float orient = SignedArea(poly) > 0.f ? 1.f : -1.f;
  std::vector< uint32_t > left(n);
  for (size_t k = 0; k < n; ++k) {
    left[k] = static_cast< uint32_t >(k);
  }
  const auto cross = [&](uint32_t a, uint32_t b, uint32_t c) {
    return (poly[b].x - poly[a].x) * (poly[c].z - poly[a].z) -
           (poly[b].z - poly[a].z) * (poly[c].x - poly[a].x);
  };
  const auto contains = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t p) {
    const float d1 = cross(a, b, p) * orient, d2 = cross(b, c, p) * orient,
                d3 = cross(c, a, p) * orient;
    return d1 >= 0.f && d2 >= 0.f && d3 >= 0.f;
  };
  size_t guard = 0;
  while (left.size() > 3 && guard++ < n * n) {
    bool clipped = false;
    for (size_t k = 0; k < left.size(); ++k) {
      const uint32_t a = left[(k + left.size() - 1) % left.size()], b = left[k],
                     c = left[(k + 1) % left.size()];
      if (cross(a, b, c) * orient <= 0.f) {
        continue; // reflex (or flat): not an ear
      }
      bool empty = true;
      for (const uint32_t p : left) {
        if (p != a && p != b && p != c && contains(a, b, c, p)) {
          empty = false;
          break;
        }
      }
      if (!empty) {
        continue;
      }
      out.push_back({a, b, c});
      left.erase(left.begin() + static_cast< long >(k));
      clipped = true;
      break;
    }
    if (!clipped) {
      return {};
    }
  }
  if (left.size() == 3) {
    out.push_back({left[0], left[1], left[2]});
  }
  return out;
}

} // namespace PortPromptRingMath
