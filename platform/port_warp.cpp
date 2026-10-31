// The warp request slot. See `include/port_warp.h` for what the kinds mean and who fills it in.

#include "port_warp.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

PortWarp::SRequest sRequest;
bool sReadEnv = false;

void CopyText(char* out, const size_t size, const char* text) {
  if (text == nullptr) {
    out[0] = '\0';
    return;
  }
  size_t i = 0;
  for (; text[i] != '\0' && i + 1 < size; ++i) {
    out[i] = text[i];
  }
  out[i] = '\0';
}

// "something,else" -> first and second halves.
void Split(const char* text, char* first, const size_t firstSize, char* second,
           const size_t secondSize) {
  first[0] = '\0';
  second[0] = '\0';
  if (text == nullptr || *text == '\0') {
    return;
  }
  const char* comma = std::strchr(text, ',');
  if (comma == nullptr) {
    CopyText(first, firstSize, text);
    return;
  }
  const size_t length = static_cast< size_t >(comma - text);
  size_t i = 0;
  for (; i < length && i + 1 < firstSize; ++i) {
    first[i] = text[i];
  }
  first[i] = '\0';
  CopyText(second, secondSize, comma + 1);
}

} // namespace

namespace PortWarp {

namespace {
// A boot request (MP2_WORLD / MP2_TELEPORT) has to survive the `MP2_INPUT` tokens that follow it:
// they share one slot, and a sink that advances by *loop iteration* can fire its first gameplay
// token (a `HEAL`, an `A`) before the game's first Update has consumed the boot request - which then
// vanishes and the run quietly tests the save's own area instead of the one it asked for (measured:
// a run booted `MP2_WORLD="3bfa3eff,5b92d146"` and played area 0, with no line saying why). So while
// a boot request is pending, a token goes to one deferred slot instead and takes the queue once the
// boot request has been consumed. With no boot request pending the old rule stands: latest wins.
SRequest sDeferred;
bool sBootPending = false;

void Queue(const SRequest& request) {
  if (sBootPending && sRequest.kind != EKind::kNone) {
    if (sDeferred.kind == EKind::kNone) {
      std::fprintf(stderr,
                   "[warp] a token arrived while the boot request was pending; serving it after\n");
    }
    sDeferred = request;
    return;
  }
  sRequest = request;
}

// Layer requests are queued, not replaced: `layer 2; layer 14` in one tick (a console line, an
// `MP2_INPUT` LAYER token) used to keep only the last one, so a module whose actors live in the
// first layer never appeared. The game serves them one at a time, because PortWarpToLayer streams
// a layer in over frames and finishes it in CStateManager::PortLayerTick: the next one is handed
// out only once the previous has finished (SetLayerBusy, from PortWarpTick).
constexpr int kMaxQueuedLayers = 8;
SRequest sLayers[kMaxQueuedLayers];
int sLayerFirst = 0;
int sLayerCount = 0;
bool sLayerBusy = false;

void QueueLayer(const SRequest& request) {
  if (sLayerCount == kMaxQueuedLayers) {
    std::fprintf(stderr, "[layer] the layer queue is full (%d); dropping layer %d\n",
                 kMaxQueuedLayers, request.delta);
    return;
  }
  sLayers[(sLayerFirst + sLayerCount) % kMaxQueuedLayers] = request;
  ++sLayerCount;
}

// The oldest queued layer, or false when none is waiting.
bool TakeLayer(SRequest& out) {
  if (sLayerCount == 0) {
    return false;
  }
  out = sLayers[sLayerFirst];
  sLayerFirst = (sLayerFirst + 1) % kMaxQueuedLayers;
  --sLayerCount;
  return true;
}
} // namespace

void Area(const char* area) {
  SRequest request;
  request.kind = EKind::kArea;
  CopyText(request.area, sizeof request.area, area);
  Queue(request);
}

void World(const char* world, const char* area) {
  SRequest request;
  request.kind = EKind::kWorld;
  CopyText(request.world, sizeof request.world, world);
  CopyText(request.area, sizeof request.area, area);
  Queue(request);
}

void CycleArea(int delta) {
  SRequest request;
  request.kind = EKind::kCycleArea;
  request.delta = delta;
  Queue(request);
}

void CycleWorld(int delta) {
  SRequest request;
  request.kind = EKind::kCycleWorld;
  request.delta = delta;
  Queue(request);
}

void Dock() {
  SRequest request;
  request.kind = EKind::kDock;
  Queue(request);
}

void Dimension() {
  SRequest request;
  request.kind = EKind::kDimension;
  Queue(request);
}

namespace {
std::atomic<int> sDimensionResult{0};
}

void NoteDimension(bool found) { sDimensionResult.store(found ? 1 : -1, std::memory_order_relaxed); }

int TakeDimensionResult() { return sDimensionResult.exchange(0, std::memory_order_relaxed); }

void Door() {
  SRequest request;
  request.kind = EKind::kDoor;
  Queue(request);
}

void Station() {
  SRequest request;
  request.kind = EKind::kStation;
  Queue(request);
}

void WorldPortal() {
  SRequest request;
  request.kind = EKind::kWorldPortal;
  Queue(request);
}

void Kill() {
  SRequest request;
  request.kind = EKind::kKill;
  Queue(request);
}

void Heal() {
  SRequest request;
  request.kind = EKind::kHeal;
  Queue(request);
}
void Layer(int layer) {
  SRequest request;
  request.kind = EKind::kLayer;
  request.delta = layer;
  QueueLayer(request);
}
void LayerOff(int layer) {
  SRequest request;
  request.kind = EKind::kLayerOff;
  request.delta = layer;
  QueueLayer(request);
}

void ApplyEnvOnce() {
  if (sReadEnv) {
    return;
  }
  sReadEnv = true;
  // MP2_WORLD="<world name or hex>[,<area name or hex>]" jumps worlds at start-up;
  // MP2_TELEPORT="<area name or hex>" jumps within the world the save is in. The one that was
  // set most recently in this function wins, and both are consumed on the first game update,
  // so a normal launch never sees them.
  if (const char* world = std::getenv("MP2_WORLD")) {
    char worldText[80];
    char areaText[80];
    Split(world, worldText, sizeof worldText, areaText, sizeof areaText);
    sBootPending = true;
    World(worldText, areaText);
    std::fprintf(stderr, "[warp] boot request: world '%s' area '%s'\n", worldText, areaText);
  }
  if (const char* area = std::getenv("MP2_TELEPORT")) {
    // MP2_WORLD and MP2_TELEPORT share the slot, and the teleport is applied second, so it wins -
    // which is the documented way to combine them (see platform/README.md).
    sBootPending = true;
    Area(area);
    std::fprintf(stderr, "[warp] boot request: area '%s'\n", area);
  }
}

bool Pending() {
  // A queued layer counts as pending - `settled` and the save state read this - but only while the
  // one before it has finished, since until then there is nothing to hand out.
  return sRequest.kind != EKind::kNone || (sLayerCount > 0 && !sLayerBusy);
}

bool PendingIsBoot() { return sBootPending && sRequest.kind != EKind::kNone; }

bool PeekBoot(SRequest& out) {
  if (!PendingIsBoot()) {
    return false;
  }
  out = sRequest;
  return true;
}

bool Consume(SRequest& out) {
  if (sRequest.kind == EKind::kNone) {
    // No warp in the slot: the oldest queued layer goes, unless the last one is still streaming.
    return !sLayerBusy && TakeLayer(out);
  }
  out = sRequest;
  sRequest = SRequest();
  if (sBootPending) {
    // The boot request has been served (or dropped, which is the game's business, not ours): a token
    // that was held back while it was pending takes the slot now.
    sBootPending = false;
    if (sDeferred.kind != EKind::kNone) {
      sRequest = sDeferred;
      sDeferred = SRequest();
    }
  }
  return true;
}

void Clear() {
  sRequest = SRequest();
  // Everything waiting goes, the queued layers included.
  sLayerFirst = 0;
  sLayerCount = 0;
}

void SetLayerBusy(bool busy) { sLayerBusy = busy; }

} // namespace PortWarp
