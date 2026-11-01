#include "port_motion_watch.h"

#include "Kyoto/Math/CVector3f.hpp"
#include "MetroidPrime/CEntity.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <typeinfo>

#if defined(__GNUC__)
#include <cxxabi.h>
#endif

namespace PortMotionWatch {
namespace {

constexpr int kRing = 20;
constexpr float kJumpDist = 2.f;    // units moved in one tick
constexpr float kMaxSpeed = 60.f;   // units/s after a tick
constexpr double kMinDumpGap = 2.0; // seconds of real time between dumps
constexpr int kMaxDumps = 20;

struct Entry {
  unsigned tick = 0;
  int area = -1;
  Branch branch = Branch::None;
  int movement = 0;
  int morph = 0;
  int colliders = 0;
  CVector3f p0, p1, v0, v1, impulse;
  float gap = 0.f; // distance from the previous tick's end to this tick's start
  bool warp = false;
  bool cine = false;
  char kb[112] = {};
};

Entry sRing[kRing];
int sHead = 0; // next slot
int sCount = 0;
const CStateManager* sMgr = nullptr;
const CPlayer* sPlayer = nullptr;
unsigned sLastTick = 0;
bool sHavePrev = false;
CVector3f sPrevEnd;
bool sWarpPending = false;
char sPendingKb[112] = {}; // knockbacks since the last tick, for the next entry
int sDumps = 0;
std::chrono::steady_clock::time_point sLastDump;

const char* BranchName(Branch b) {
  switch (b) {
  case Branch::GroundNew: return "groundNew";
  case Branch::Ball: return "ball";
  case Branch::Air: return "air";
  default: return "none";
  }
}

std::string ClassName(const CEntity& ent) {
  const char* raw = typeid(ent).name();
#if defined(__GNUC__)
  int status = 0;
  char* demangled = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
  if (status == 0 && demangled != nullptr) {
    std::string name = demangled;
    std::free(demangled);
    return name;
  }
#endif
  return raw;
}

void Reset() {
  sCount = 0;
  sHead = 0;
  sHavePrev = false;
}

void WriteEntry(const Entry& e) {
  std::fprintf(stderr,
               "[motion]  t=%u a=%d br=%s ms=%d mb=%d col=%d p=(%.2f %.2f %.2f)->(%.2f %.2f "
               "%.2f) v=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) imp=(%.1f %.1f %.1f) gap=%.2f%s%s%s%s\n",
               e.tick, e.area, BranchName(e.branch), e.movement, e.morph, e.colliders, e.p0.GetX(),
               e.p0.GetY(), e.p0.GetZ(), e.p1.GetX(), e.p1.GetY(), e.p1.GetZ(), e.v0.GetX(),
               e.v0.GetY(), e.v0.GetZ(), e.v1.GetX(), e.v1.GetY(), e.v1.GetZ(), e.impulse.GetX(),
               e.impulse.GetY(), e.impulse.GetZ(), e.gap, e.warp ? " warp" : "",
               e.cine ? " cine" : "", e.kb[0] != '\0' ? " kb=" : "", e.kb);
}

void Dump(const Entry& last, const char* why, float moved, float speed) {
  const auto now = std::chrono::steady_clock::now();
  if (sDumps >= kMaxDumps ||
      (sDumps > 0 && std::chrono::duration<double>(now - sLastDump).count() < kMinDumpGap)) {
    return;
  }
  ++sDumps;
  sLastDump = now;
  std::fprintf(stderr,
               "[motion] %s: tick %u moved %.2f units, speed %.1f, gap %.2f (dump %d/%d, last %d "
               "ticks)\n",
               why, last.tick, moved, speed, last.gap, sDumps, kMaxDumps, sCount);
  for (int i = 0; i < sCount; ++i) {
    WriteEntry(sRing[(sHead - sCount + i + kRing * 2) % kRing]);
  }
}

} // namespace

TickScope::TickScope(CStateManager& mgr, CPlayer* player, const ColliderList* colliderList)
: mPlayer(player) {
  if (player == nullptr) {
    return;
  }
  const unsigned tick = static_cast< unsigned >(mgr.GetUpdateFrameIdx());
  // A new player or a rewound clock is a rebuilt world (state load, world load): start over.
  if (&mgr != sMgr || player != sPlayer || tick < sLastTick) {
    Reset();
    sMgr = &mgr;
    sPlayer = player;
    sWarpPending = false;
  }
  sLastTick = tick;
  Entry& e = sRing[sHead];
  e = Entry();
  e.tick = tick;
  e.area = player->GetCurrentAreaId().Value();
  e.movement = static_cast< int >(player->GetPlayerMovementState());
  e.morph = static_cast< int >(player->GetMorphballTransitionState());
  e.p0 = player->GetTranslation();
  e.v0 = player->GetVelocityWR();
  e.impulse = player->GetImpulseWR();
  if (colliderList != nullptr) {
    const TUniqueId self = player->GetUniqueId();
    for (int i = 0; i < static_cast< int >(colliderList->size()); ++i) {
      if ((*colliderList)[i] != self) {
        ++e.colliders;
      }
    }
  }
  const CCameraManager* cam = mgr.GetCameraManager(0);
  e.cine = cam != nullptr && cam->IsInCinematicCamera();
  e.warp = sWarpPending;
  sWarpPending = false;
  e.gap = sHavePrev ? (e.p0 - sPrevEnd).Magnitude() : 0.f;
  std::memcpy(e.kb, sPendingKb, sizeof(e.kb));
  sPendingKb[0] = '\0';
}

TickScope::~TickScope() {
  if (mPlayer == nullptr) {
    return;
  }
  Entry& e = sRing[sHead];
  e.branch = mBranch;
  e.p1 = mPlayer->GetTranslation();
  e.v1 = mPlayer->GetVelocityWR();
  sHead = (sHead + 1) % kRing;
  if (sCount < kRing) {
    ++sCount;
  }
  sHavePrev = true;
  sPrevEnd = e.p1;
  const float moved = (e.p1 - e.p0).Magnitude();
  const float speed = e.v1.Magnitude();
  // A warp is legitimate: its jump is tagged in the ring and not dumped.
  if (e.warp) {
    return;
  }
  if (e.gap > kJumpDist) {
    Dump(e, "out-of-mover jump", moved, speed);
  } else if (moved > kJumpDist) {
    Dump(e, "moved too far in one tick", moved, speed);
  } else if (speed > kMaxSpeed) {
    Dump(e, "too fast", moved, speed);
  }
}

void NoteKnockback(const CStateManager& mgr, TUniqueId source, const CVector3f& dir, float power) {
  // Damage runs outside MovePlayer: park the note for the next tick's entry.
  const CEntity* src = mgr.GetObjectById(source);
  char text[112];
  const std::string cls = src != nullptr ? ClassName(*src) : std::string("?");
  const int uid = src != nullptr ? static_cast< int >(src->GetUniqueId().Value()) : -1;
  const int n = std::snprintf(text, sizeof(text), "%s#%d(dir %.2f %.2f %.2f pow %.0f)",
                              cls.c_str(), uid, dir.GetX(), dir.GetY(), dir.GetZ(), power);
  size_t have = std::strlen(sPendingKb);
  if (n <= 0 || have + 2 >= sizeof(sPendingKb)) {
    return;
  }
  if (have != 0) {
    sPendingKb[have++] = '+';
  }
  const size_t room = sizeof(sPendingKb) - 1 - have;
  const size_t len = std::min(std::strlen(text), room);
  std::memcpy(sPendingKb + have, text, len);
  sPendingKb[have + len] = '\0';
}

void NoteWarp() { sWarpPending = true; }

} // namespace PortMotionWatch
