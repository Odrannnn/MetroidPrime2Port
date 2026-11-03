// See platform/include/port_harness.h. The sibling of platform/port_warp.cpp: the warp slot is
// what a test *does*, this is how long it does it for.

#include "port_harness.h"

#include "port_overlay.h"
#include "port_savestate.h"
#include "port_timing.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {

bool sReadEnv = false;
int sLimit = 0;   // 0 = no limit
int sUpdates = 0;
int sCountStart = 0; // StartCountAt: the count the game's first update starts after
bool sExitRequested = false;

// The stall report: the wall time of the last game update, and whether this stall has been
// reported already.
double sLastUpdateSeconds = 0.0;
bool sWarned = false;
constexpr double kIdleWarnSeconds = 30.0;

// MP2_SKIP_INTRO (see the header).
bool sIntroReadEnv = false;
bool sIntroArmed = false;
bool sIntroDone = false;
int sIntroBudget = 3600;  // game updates
int sIntroUpdates = 0;    // game updates seen while armed
bool sFlowInGame = false;
std::atomic< int > sFlowState{-1}; // GameFlowState's last state, read by FlowState()
std::atomic< bool > sMapScreen{false}; // GameMapScreen since the last flow state change
int sPlayingUpdates = 0;  // consecutive game updates that were "playing"
int sDismissed = 0;
constexpr int kSettleUpdates = 120;

void ReadIntroEnvOnce() {
  if (sIntroReadEnv) {
    return;
  }
  sIntroReadEnv = true;
  const char* value = std::getenv("MP2_SKIP_INTRO");
  sIntroArmed = value != nullptr && value[0] != '\0' && value[0] != '0';
  if (!sIntroArmed) {
    return;
  }
  if (const char* frames = std::getenv("MP2_SKIP_INTRO_FRAMES")) {
    const int budget = std::atoi(frames);
    if (budget > 0) {
      sIntroBudget = budget;
    }
  }
  std::fprintf(stderr, "[harness] MP2_SKIP_INTRO=%s: HUD memos are dismissed and the level-load "
                       "text is not played until gameplay (budget %d game updates)\n",
               value, sIntroBudget);
}

void ReadEnvOnce() {
  if (sReadEnv) {
    return;
  }
  sReadEnv = true;
  const char* value = std::getenv("MP2_FRAMES");
  if (value == nullptr) {
    return;
  }
  sLimit = std::atoi(value);
  if (sLimit < 0) {
    sLimit = 0;
  }
  if (sLimit > 0) {
    std::fprintf(stderr, "[harness] MP2_FRAMES=%d: the run ends itself after %d game updates\n",
                 sLimit, sLimit);
  } else {
    std::fprintf(stderr, "[harness] MP2_FRAMES=%s is not a positive frame count; no limit\n", value);
  }
}

} // namespace

namespace PortHarness {

bool GameUpdate() {
  ReadEnvOnce();
  if (++sUpdates == sCountStart + 1) {
    // The first game update is the moment the game is up and running (the world the flow asked for
    // has been loaded), which is the number most test timings are compared against.
    std::fprintf(stderr, "[harness] the first game update is at %.1f s\n", Seconds());
  }
  // A game that is updating is not a stalled one: this both times the last update and re-arms the
  // stall report, so a second stall later in the run is reported too.
  sLastUpdateSeconds = Seconds();
  sWarned = false;
  if (sLimit == 0 || sExitRequested) {
    return false;
  }
  if (sUpdates < sLimit) {
    return false;
  }
  sExitRequested = true;
  return true;
}

bool ExitRequested() { return sExitRequested; }

unsigned GameUpdates() { return static_cast< unsigned >(sUpdates); }

void StartCountAt(unsigned updates) {
  if (sUpdates != sCountStart) {
    return; // the game has updated already: too late to move the count
  }
  sCountStart = sUpdates = static_cast< int >(updates);
  std::fprintf(stderr, "[harness] direct boot: the update count starts at %u, where a save-world "
                       "boot served its warp (MP2_FRAMES, @f and `frame` keep their meaning)\n",
               updates);
}

bool Unpaced() {
  static const bool sFastLoad = [] {
    const char* load = std::getenv("MP2_FAST_LOAD");
    const char* skip = std::getenv("MP2_SKIP_FRONTEND");
    return load != nullptr && load[0] == '1' && skip != nullptr && skip[0] != '\0' && skip[0] != '0';
  }();
  if (PortTiming::FixedTicksPerFrame() != 0 && !PortOverlay::FrameLimit()) {
    return true;
  }
  return sFastLoad && sUpdates == sCountStart;
}

void IdleCheck() {
  if (sUpdates == sCountStart) {
    // Nothing to compare against until the game has updated once: the boot's own loading is long
    // and is not a stall.
    return;
  }
  const double idle = Seconds() - sLastUpdateSeconds;
  if (sWarned || idle < kIdleWarnSeconds) {
    return;
  }
  sWarned = true;
  // Worth a line of its own: an MP2_FRAMES run that stops updating before its limit is otherwise
  // indistinguishable from a hang, and only the script's `timeout` ends it.
  std::fprintf(stderr, "[harness] no game update for %.1f s (%d updates so far, limit %d): the game "
                         "may be paused - a paused game performs no game update, so MP2_FRAMES is "
                         "never reached and only the script's timeout ends the run\n",
               idle, sUpdates, sLimit);
}

int FlowState() { return sFlowState.load(std::memory_order_acquire); }

void GameMapScreen() { sMapScreen.store(true, std::memory_order_release); }

bool MapScreenOpen() { return sMapScreen.load(std::memory_order_acquire); }

void GameFlowState(int state, bool inGame) {
  sMapScreen.store(false, std::memory_order_release);
  sFlowState.store(state, std::memory_order_release);
  sFlowInGame = inGame;
  if (!inGame) {
    sPlayingUpdates = 0;
  }
}

bool IntroDismissMemo(unsigned message) {
  ReadIntroEnvOnce();
  if (!sIntroArmed || sIntroDone) {
    return false;
  }
  ++sDismissed;
  std::fprintf(stderr, "[harness] MP2_SKIP_INTRO dismissed HUD memo %08x at %.1f s\n", message,
               Seconds());
  return true;
}

bool IntroSkipCinematic(unsigned camera) {
  ReadIntroEnvOnce();
  if (PortSaveState::LoadAwaitsCinematic()) {
    return true;
  }
  if (!sIntroArmed || sIntroDone) {
    return false;
  }
  // One line per camera, not per request: a cinematic that may not be skipped is asked every frame.
  static unsigned sLastCamera = ~0u;
  if (camera != sLastCamera) {
    sLastCamera = camera;
    std::fprintf(stderr, "[harness] MP2_SKIP_INTRO skips cinematic camera %04x at %.1f s\n",
                 camera & 0xffff, Seconds());
  }
  return true;
}

bool IntroSkipLevelText() {
  ReadIntroEnvOnce();
  return sIntroArmed;
}

void NotePlaying(bool playing) {
  ReadIntroEnvOnce();
  if (!sIntroArmed || sIntroDone) {
    return;
  }
  ++sIntroUpdates;
  sPlayingUpdates = playing && sFlowInGame ? sPlayingUpdates + 1 : 0;
  if (sPlayingUpdates >= kSettleUpdates) {
    sIntroDone = true;
    std::fprintf(stderr, "[harness] gameplay reached at %.1f s (frame %u)\n", Seconds(),
                 static_cast< unsigned >(sUpdates));
    return;
  }
  if (sIntroUpdates >= sIntroBudget) {
    sIntroDone = true;
    std::fprintf(stderr, "[harness] MP2_SKIP_INTRO gave up after %d game updates at %.1f s (%d memo(s) "
                         "dismissed); no longer dismissing\n",
                 sIntroBudget, Seconds(), sDismissed);
  }
}

double Seconds() {
  // Anchored on the first call into this file, which is the game loop's first iteration: the loop's
  // own poll asks `ExitRequested` before anything else runs, and the game's first update asks
  // `GameUpdate`. Every second a `[skip]`/`[harness]` line prints is measured from that moment, so a
  // log's phases are comparable across runs.
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration< double >(std::chrono::steady_clock::now() - start).count();
}

} // namespace PortHarness
