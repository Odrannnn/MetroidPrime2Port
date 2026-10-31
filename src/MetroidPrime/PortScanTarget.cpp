// Port-only: fills the scan visor's target buffers from a CPU read-back of the framebuffer.
// See include/MetroidPrime/PortScanTarget.hpp for why, and extern/aurora/lib/gfx/scan_peek.cpp
// for the read-back itself.

#include "MetroidPrime/PortScanTarget.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dolphin/gx/GXCpu2Efb.h>

namespace PortScanTarget {
namespace {

// The game's capture is 32x64 pixels (kScanTargetTextureSize) and it reads the buffers back as
// height * width entries. The region read back can be larger than that (a wider aspect than the
// console's 4:3 needs more logical pixels to cover the same share of the screen); FillBuffers
// resamples it down to the game's size, so the staging copy is sized for the larger region.
constexpr uint32_t kMaxPixels = 256 * 256;

// Filled by Aurora's render thread when a read-back completes, read here on the game thread.
// `sequence` is the seqlock scan_peek.hpp describes: it is odd while the arrays are being
// written, and even and increased by two once a whole snapshot is there. It must never be read or
// written except through std::atomic_ref.
struct Staging {
  uint32_t sequence;
  uint8_t id[kMaxPixels];
  uint16_t depthHigh[kMaxPixels];
  uint8_t depthLow[kMaxPixels];
};

Staging sStaging;
uint32_t sConsumedSequence = 0;
uint32_t sTornReads = 0;
bool sLoggedFirstFill = false;
int sLastLoggedVisor = -1;
int sLastLoggedFrom = -1;
// Size of the region the last Request asked for (game thread only, as is FillBuffers).
int sRequestWidth = 0;
int sRequestHeight = 0;
Pick sPick;

bool DebugScan() {
  static const bool enabled = std::getenv("MP2_DEBUG_SCAN") != nullptr;
  return enabled;
}

} // namespace

bool Request(int left, int top, int width, int height) {
  // MP2_SCAN_PEEK=0 turns the read-back off, which is the A/B for a crash inside it: the scan
  // visor then behaves exactly as it did before this port work (no target can be selected).
  static const bool enabled = std::getenv("MP2_SCAN_PEEK") == nullptr ||
                              std::strcmp(std::getenv("MP2_SCAN_PEEK"), "0") != 0;
  if (!enabled || width <= 0 || height <= 0) {
    return false;
  }
  if (static_cast<uint32_t>(width) * static_cast<uint32_t>(height) > kMaxPixels) {
    return false;
  }
  // Throttle (issue #33): each peek costs the render thread a snapshot pass and a read-back, and
  // the visor needs no fresh ids every frame. Every Nth call is issued; the region's size always
  // issues one (FillBuffers resamples with it), as does the first. MP2_SCAN_PEEK_EVERY=1 is the A/B.
  static const int every = [] {
    const char* env = std::getenv("MP2_SCAN_PEEK_EVERY");
    const int value = env != nullptr ? std::atoi(env) : 2;
    return value < 1 ? 1 : value > 4 ? 4 : value;
  }();
  static int sSinceIssued = 0;
  if (sRequestWidth == width && sRequestHeight == height && ++sSinceIssued < every) {
    return false;
  }
  sSinceIssued = 0;
  sRequestWidth = width;
  sRequestHeight = height;
  GXRequestScanPeek(left, top, static_cast<uint32_t>(width), static_cast<uint32_t>(height), sStaging.id,
                    sStaging.depthHigh, sStaging.depthLow, &sStaging.sequence);
  return true;
}

void SetPick(const Pick& pick) { sPick = pick; }

const Pick& GetPick() { return sPick; }

float LensRadius(int viewportWidth, int viewportHeight) {
  // The same fit as CPlayerVisor's ScanWindowFit: below 4:3 the lens shrinks with the width.
  float fit = 1.f;
  if (viewportHeight > 0 && viewportWidth * 3 < viewportHeight * 4) {
    fit = static_cast<float>(viewportWidth) * 3.f / (static_cast<float>(viewportHeight) * 4.f);
  }
  return 122.f * fit;
}

void FillBuffers(void* idData, void* depthHighData, void* depthLowData, int width, int height) {
  if (idData == nullptr || depthHighData == nullptr || depthLowData == nullptr || width <= 0 || height <= 0) {
    return;
  }
  const uint32_t pixels = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
  if (pixels > kMaxPixels || sRequestWidth <= 0 || sRequestHeight <= 0) {
    return;
  }
  const int srcWidth = sRequestWidth;
  const int srcHeight = sRequestHeight;

  std::atomic_ref<uint32_t> sequence(sStaging.sequence);
  const uint32_t before = sequence.load(std::memory_order_acquire);
  if ((before & 1u) != 0 || before == sConsumedSequence) {
    // Either the render thread is mid-write or this snapshot is the one already in the buffers.
    return;
  }

  if (sPick.radius > 0.f && sPick.regionWidth == srcWidth && sPick.regionHeight == srcHeight) {
    // A circular pick: pool each grid cell's block of region pixels instead of sampling one, so
    // a thin or distant scannable cannot fall between samples; the cell takes the id most common
    // in its block (any hit counts), and the depth of the first pixel that has it. Pixels outside
    // the circle count for nothing.
    auto* id = static_cast<uint8_t*>(idData);
    auto* depthHigh = static_cast<uint16_t*>(depthHighData);
    auto* depthLow = static_cast<uint8_t*>(depthLowData);
    const float radiusSquared = sPick.radius * sPick.radius;
    for (int j = 0; j < height; ++j) {
      const int y0 = j * srcHeight / height;
      const int y1 = (j + 1) * srcHeight / height > y0 ? (j + 1) * srcHeight / height : y0 + 1;
      for (int i = 0; i < width; ++i) {
        const int x0 = i * srcWidth / width;
        const int x1 = (i + 1) * srcWidth / width > x0 ? (i + 1) * srcWidth / width : x0 + 1;
        uint8_t counts[64] = {};
        int first[64];
        for (int y = y0; y < y1; ++y) {
          const float dy = static_cast<float>(y) + 0.5f - sPick.centreY;
          for (int x = x0; x < x1; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - sPick.centreX;
            if (dx * dx + dy * dy > radiusSquared) {
              continue;
            }
            const int from = y * srcWidth + x;
            const int palette = sStaging.id[from] >> 2;
            if (palette < 1 || palette > 61) {
              continue;
            }
            if (counts[palette]++ == 0) {
              first[palette] = from;
            }
          }
        }
        int best = 0;
        for (int palette = 1; palette < 62; ++palette) {
          if (counts[palette] > counts[best]) {
            best = palette;
          }
        }
        const int to = j * width + i;
        if (best == 0) {
          id[to] = 0;
          depthHigh[to] = 0;
          depthLow[to] = 0;
        } else {
          id[to] = sStaging.id[first[best]];
          depthHigh[to] = sStaging.depthHigh[first[best]];
          depthLow[to] = sStaging.depthLow[first[best]];
        }
      }
    }
  } else if (srcWidth == width && srcHeight == height) {
    std::memcpy(idData, sStaging.id, pixels);
    std::memcpy(depthHighData, sStaging.depthHigh, pixels * sizeof(uint16_t));
    std::memcpy(depthLowData, sStaging.depthLow, pixels);
  } else {
    // Nearest-neighbour from the requested region to the game's grid (the same sampling Aurora
    // uses when it scales), so the histogram sees the same share of the screen at any aspect.
    auto* id = static_cast<uint8_t*>(idData);
    auto* depthHigh = static_cast<uint16_t*>(depthHighData);
    auto* depthLow = static_cast<uint8_t*>(depthLowData);
    for (int j = 0; j < height; ++j) {
      const int sy = (2 * j + 1) * srcHeight / (2 * height);
      for (int i = 0; i < width; ++i) {
        const int sx = (2 * i + 1) * srcWidth / (2 * width);
        const int from = sy * srcWidth + sx;
        const int to = j * width + i;
        id[to] = sStaging.id[from];
        depthHigh[to] = sStaging.depthHigh[from];
        depthLow[to] = sStaging.depthLow[from];
      }
    }
  }

  const uint32_t after = sequence.load(std::memory_order_acquire);
  if (after != before) {
    // Torn: the buffers now hold a mix of two frames, which the histogram in FindScanTargetId
    // reads as a slightly noisy selection for one frame. Report the rate, not every one.
    if (sTornReads++ % 64 == 0) {
      std::fprintf(stderr, "[scan] read-back torn %u times (sequence %u -> %u)\n", sTornReads, before, after);
    }
    return;
  }
  sConsumedSequence = before;

  if (!sLoggedFirstFill) {
    sLoggedFirstFill = true;
    std::fprintf(stderr, "[scan] first scan-target read-back landed (%dx%d, sequence %u)\n", width, height,
                 before);
  }
}

void NoteVisorSelection(int currentVisor, int selectedVisor, bool requested) {
  if (!DebugScan() || selectedVisor == currentVisor) {
    return;
  }
  if (selectedVisor == sLastLoggedVisor && currentVisor == sLastLoggedFrom) {
    return;
  }
  sLastLoggedFrom = currentVisor;
  sLastLoggedVisor = selectedVisor;
  std::fprintf(stderr, "[scan] visor selection: current %d -> selected %d (direct mapping seen: %d)\n",
               currentVisor, selectedVisor, requested ? 1 : 0);
}

} // namespace PortScanTarget
