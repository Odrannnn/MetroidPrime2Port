// The drawn button ring's arithmetic (platform/include/port_prompt_ring_math.h): the ring's profile
// in its texture, display-list parsing, the map from a quad's texture coordinates to its model's
// plane, the line ends' circle, the fill's round hole and the ring's fit to its disc. No game,
// Aurora or GPU.
#include "port_prompt_ring_math.h"
#include "port_test.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using namespace PortPromptRingMath;

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

void Put16(std::vector< uint8_t >& dl, uint32_t v) {
  dl.push_back(static_cast< uint8_t >(v >> 8));
  dl.push_back(static_cast< uint8_t >(v & 0xff));
}

void TestBand() {
  // Nothing outside the band, full in its core, ramping over kLineFade at each edge.
  CHECK(Near(BandCoverage(0.f), 0.f));
  CHECK(Near(BandCoverage(kInner - 0.5f), 0.f));
  CHECK(Near(BandCoverage(kInner), 0.f));
  CHECK(Near(BandCoverage(kInner + kLineFade * 0.5f), 0.5f));
  CHECK(Near(BandCoverage(kInner + kLineFade), 1.f));
  CHECK(Near(BandCoverage((kInner + kOuter) * 0.5f), 1.f));
  CHECK(Near(BandCoverage(kOuter - kLineFade), 1.f));
  CHECK(Near(BandCoverage(kOuter - kLineFade * 0.5f), 0.5f));
  CHECK(Near(BandCoverage(kOuter), 0.f));
  CHECK(Near(BandCoverage(16.f), 0.f));
  // The band is the line's width, inside the game ring's diameter, and fits in the texture.
  CHECK(Near(kOuter - kInner, kLineWidth));
  CHECK(kOuter < 16.f);
  CHECK(kLineWidth >= 2.f * kLineFade); // a core is left between the two fades
}

void TestDisplayList() {
  CHECK_EQ(VertexStride(0x30fu), 6u); // position, normal, texture coordinate indices
  CHECK_EQ(VertexStride(0xfu), 4u);   // position, normal
  CHECK_EQ(VertexStride(0u), 0u);

  // A strip of three (vertex format 1) and a fan of four, then padding.
  std::vector< uint8_t > dl;
  dl.push_back(0x99);
  Put16(dl, 3);
  for (uint32_t v : {10u, 11u, 12u}) {
    Put16(dl, v);
    Put16(dl, 0);
    Put16(dl, v + 100);
  }
  const uint32_t fanAt = static_cast< uint32_t >(dl.size());
  dl.push_back(0xa1);
  Put16(dl, 4);
  for (uint32_t v : {20u, 21u, 22u, 23u}) {
    Put16(dl, v);
    Put16(dl, 0);
    Put16(dl, v + 100);
  }
  while (dl.size() % 32 != 0) {
    dl.push_back(0);
  }
  const std::vector< Command > cmds =
      ParseDisplayList(dl.data(), static_cast< uint32_t >(dl.size()), 0x30f);
  REQUIRE_MSG(cmds.size() == 2, "the two commands, and the padding not read as one");
  CHECK_EQ(cmds[0].op, 0x98u);
  CHECK_EQ(cmds[0].head, 0u);
  CHECK_EQ(cmds[0].stride, 6u);
  REQUIRE(cmds[0].vertices.size() == 3);
  CHECK_EQ((uint32_t(dl[cmds[0].vertices[2]]) << 8) | dl[cmds[0].vertices[2] + 1], 12u);
  CHECK_EQ((uint32_t(dl[cmds[0].vertices[2] + 4]) << 8) | dl[cmds[0].vertices[2] + 5], 112u);
  CHECK_EQ(cmds[1].op, 0xa0u);
  CHECK_EQ(cmds[1].head, fanAt);
  CHECK_EQ(dl[cmds[1].head] & 7u, 1u); // the vertex format, kept in the opcode byte
  CHECK_EQ(cmds[1].vertices.size(), 4u);

  // A command whose vertices run past the list's end isn't returned.
  std::vector< uint8_t > cut{0x98, 0x00, 0x05, 0x00, 0x01};
  CHECK(ParseDisplayList(cut.data(), static_cast< uint32_t >(cut.size()), 0x30f).empty());
}

void TestMap() {
  // A quad covering a quarter of the texture (u 0.25..0.5, v 0.5..0.75), 0.4 model units a quarter:
  // the texture is 1.6 units across; its centre (0.5, 0.5) lands where the quad's corner is.
  const float xz[4][2] = {{-0.4f, -9.076f}, {0.f, -9.076f}, {-0.4f, -8.676f}, {0.f, -8.676f}};
  const float uv[4][2] = {{0.25f, 0.75f}, {0.5f, 0.75f}, {0.25f, 1.f}, {0.5f, 1.f}};
  UvMap map;
  REQUIRE(MapFromCorners(xz, uv, map));
  CHECK(Near(map.Unit(), 1.6f));
  CHECK(Near(map.X(0.5f, 0.5f), 0.f));
  CHECK(Near(map.Z(0.5f, 0.5f), -9.476f));
  CHECK(Near(map.X(0.f, 0.5f), -0.8f));

  // Corners 0, 1 and 2 in a line in texture space: corner 3 is used instead.
  const float uvLine[4][2] = {{0.f, 0.f}, {0.5f, 0.f}, {1.f, 0.f}, {0.f, 1.f}};
  const float xzLine[4][2] = {{0.f, 0.f}, {1.f, 0.f}, {2.f, 0.f}, {0.f, 2.f}};
  REQUIRE(MapFromCorners(xzLine, uvLine, map));
  CHECK(Near(map.X(1.f, 1.f), 2.f));
  CHECK(Near(map.Z(1.f, 1.f), 2.f));

  // All in a line: no map.
  const float uvFlat[4][2] = {{0.f, 0.f}, {0.5f, 0.f}, {1.f, 0.f}, {0.25f, 0.f}};
  CHECK(!MapFromCorners(xzLine, uvFlat, map));
}

void TestCircle() {
  // A cut between two corners `half` above and below the ring's centre height, both on the chord
  // radius: its middle lies on the ring's edge.
  const float edge = 0.77f, half = 0.27f;
  const float r = ChordRadius(edge, half);
  const float x = std::sqrt(r * r - half * half); // both corners' x
  CHECK(Near(x, edge));
  CHECK(Near(ChordRadius(edge, 0.f), edge));
}

void TestFit() {
  // The scan box's own ratio, and anything within half a percent of it: left alone.
  CHECK(Near(FitScale(kRingPerDisc), 1.f));
  CHECK(Near(FitScale(kRingPerDisc * 1.004f), 1.f));
  // The message box's ring quad, 1.21 times its disc's: drawn at the scan box's ratio.
  CHECK(Near(FitScale(1.2127f) * 1.2127f, kRingPerDisc, 1e-3f));
  // Past a quarter either way, or nothing measured: left alone.
  CHECK(Near(FitScale(2.f), 1.f));
  CHECK(Near(FitScale(0.5f), 1.f));
  CHECK(Near(FitScale(0.f), 1.f));
  // The teal disc stays inside the ring's inner edge at that ratio.
  CHECK(kDiscRadius < kInner * kRingPerDisc);
}

float TriangleArea(const std::vector< Point >& p, const std::array< uint32_t, 3 >& t) {
  return 0.5f * std::fabs((p[t[1]].x - p[t[0]].x) * (p[t[2]].z - p[t[0]].z) -
                          (p[t[1]].z - p[t[0]].z) * (p[t[2]].x - p[t[0]].x));
}

void TestHole() {
  // What each primitive draws.
  std::vector< std::array< uint32_t, 3 > > tris;
  CommandTriangles(0x98, {0, 1, 2, 3, 4}, tris);
  CHECK_EQ(tris.size(), 3u);
  tris.clear();
  CommandTriangles(0xa0, {9, 1, 2, 3}, tris);
  REQUIRE(tris.size() == 2);
  CHECK_EQ(tris[1][0], 9u);
  tris.clear();
  CommandTriangles(0x80, {0, 1, 2, 3}, tris);
  CHECK_EQ(tris.size(), 2u);
  tris.clear();
  CommandTriangles(0x90, {0, 1, 2, 3, 4, 5}, tris);
  CHECK_EQ(tris.size(), 2u);

  // Two triangles making a square: one loop of its four corners. Empty triangles don't count.
  const std::vector< uint32_t > loop = BoundaryLoop({{0, 1, 2}, {0, 2, 3}, {5, 5, 6}});
  REQUIRE(loop.size() == 4);
  // A square with a square hole (eight triangles round it): two loops, so none.
  CHECK(
      BoundaryLoop(
          {{0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}})
          .empty());

  // A box 10 wide and 4 tall above z = 0, a disc of radius 2 centred half a unit below its edge:
  // the box less the part of the disc inside it.
  const std::vector< Point > box{{-5.f, 0.f}, {5.f, 0.f}, {5.f, 4.f}, {-5.f, 4.f}};
  const Point c{0.f, -0.5f};
  const float r = 2.f;
  const std::vector< Point > cut = CutDisc(box, c, r, 96);
  REQUIRE_MSG(!cut.empty(), "the disc crosses the box's edge twice");
  for (const Point& p : cut) {
    CHECK(std::hypot(p.x - c.x, p.z - c.z) >= r - 1e-3f);
    CHECK(p.z >= -1e-4f);
  }
  const float segment = r * r * std::acos(0.5f / r) - 0.5f * std::sqrt(r * r - 0.25f);
  CHECK_MSG(Near(std::fabs(SignedArea(cut)), 40.f - segment, 0.05f), "the box less the cut");
  // A disc clear of the box, or across all of it, isn't a notch.
  CHECK(CutDisc(box, {0.f, -5.f}, 1.f, 16).empty());
  CHECK(CutDisc(box, {0.f, 2.f}, 1.f, 16).empty());

  // Its triangles cover it exactly, the concave part over the hole included.
  const std::vector< std::array< uint32_t, 3 > > clipped = EarClip(cut);
  REQUIRE(clipped.size() == cut.size() - 2);
  float covered = 0.f;
  for (const auto& t : clipped) {
    covered += TriangleArea(cut, t);
  }
  CHECK(Near(covered, std::fabs(SignedArea(cut)), 1e-2f));
  CHECK(PointInPolygon(box, {0.f, 3.f}));
  CHECK(!PointInPolygon(cut, {0.f, 0.5f})); // in the hole
}

} // namespace

int main() {
  TestBand();
  TestDisplayList();
  TestMap();
  TestCircle();
  TestFit();
  TestHole();
  if (port_test::Failures() == 0) {
    std::printf("port_prompt_ring: ok\n");
  }
  return PORT_TEST_RESULT();
}
