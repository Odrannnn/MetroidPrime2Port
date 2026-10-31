// Debug command console, modelled on MetroidPrimePort's `platform/port_console.cpp` - same protocol
// (`MP2_CONSOLE=<port>`, one command per line, replies ending in `=> ok`/`=> err: ...`), the same
// one-client-at-a-time TCP listener, and the same rule that everything which reads the game runs on
// the game thread. See platform/include/port_console.h.
//
// What is here is the set of commands this tree can serve today through its existing port APIs
// (PortWarp, PortOverlay) plus the world/player state a state manager gives for free. Adding one is a
// row in `kCommands`.

#include "MetroidPrime/CMain.hpp"
#include <SDL3/SDL.h>
#include "port_harness.h"
#include "port_console.h"
#include "port_discord.h"
#include "port_mouse.h"
#include "port_radial.h"
#include "port_freecam.h"
#include "port_livesplit.h"
#include "port_photo.h"

#include "port_input.h"
#include "port_logfile.h"
#include "port_overlay.h"
#include "port_pointer.h"
#include "port_save.h"
#include "port_savestate.h"
#include "port_statedump.h"
#include "port_env.h"
#include "port_overlay_internal.h"
#include "port_video.h"
#include "port_warp.h"
#ifdef MP_PORT_COVERAGE
#include "port_coverage.h"
#endif

#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/HUD/CSamusHud.hpp"
#include "MetroidPrime/HUD/CHUDMemoParms.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "MetroidPrime/CMemoryCard.hpp"

extern bool gPortForceMultiplayer; // CStateManager.cpp, console `mpforce`
#include "MetroidPrime/CWorld.hpp"
#include "Kyoto/CResFactory.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/SObjectTag.hpp"
#include "Kyoto/Text/CStringTable.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/Cameras/CFirstPersonCamera.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/CSlideShow.hpp"
#include "MetroidPrime/Player/CWorldTransManager.hpp"
#include "MetroidPrime/Player/CMorphBall.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerGun.hpp"
#include "MetroidPrime/Weapons/CGunWeapon.hpp"
#include "MetroidPrime/Weapons/CGameProjectile.hpp"
#include "Weapons/CWeaponDescription.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/CActor.hpp"
#include "MetroidPrime/CPhysicsActor.hpp"
#include "MetroidPrime/Factories/CScannableObjectInfo.hpp"
#include "Kyoto/Animation/CInt32POINode.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include <musyx/port_voices.h>
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CEntity.hpp"
#include "MetroidPrime/CEntityInfo.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "Collision/CMaterialFilter.hpp"
#include "Collision/CRayCastResult.hpp"
#include "Collision/CCollisionInfoList.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/RenderGeometryRayCast.hpp"
#include "MetroidPrime/CObjectList.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "Kyoto/Graphics/CModel.hpp"
#include "Kyoto/Streams/CMemoryInStream.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "Kyoto/Animation/CPASDatabase.hpp"
#include "MetroidPrime/Enemies/CSpacePirate.hpp"
#include "MetroidPrime/Enemies/CSwarmBasics.hpp"
#include "MetroidPrime/CGenericFSM2State.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAIHint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptConditionalRelay.hpp"
#include "MetroidPrime/ScriptObjects/CScriptDoor.hpp"
#include "MetroidPrime/ScriptObjects/CScriptGrapplePoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptPlayerProxy.hpp"
#include "MetroidPrime/Player/CGMMultiplayer.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWater.hpp"
#include "Collision/CMaterialFilter.hpp"

#include <aurora/gfx.h>
#include <aurora/perf.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <typeinfo>
#include <vector>

#if defined(__GNUC__)
#include <cxxabi.h>
#endif

namespace aurora {
void request_screenshot() noexcept;
}

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace PortConsole {
namespace {

constexpr int kDefaultPort = 4780;
// Replies are sent from the game thread: a client that stops reading must not stall it.
constexpr int kSendBudgetMs = 200;
// A line longer than this is not a command.
constexpr size_t kMaxLine = 64 * 1024;
// Lines waiting for the game; more are answered with an error and dropped.
constexpr size_t kMaxQueued = 256;

bool sStarted = false;
bool sEnabled = false;

// Lines the port itself queues (PortConsole::Run): no socket, so they work without MP2_CONSOLE.
std::mutex sLocalMutex;
std::deque< std::string > sLocal;

#ifndef _WIN32
std::mutex sClientMutex;
std::condition_variable sClientArrived;  // MP2_CONSOLE_WAIT: Start() waits on it
int sClient = -1;
unsigned sGeneration = 0;

struct SQueued {
  std::string line;
  unsigned generation;
};

std::mutex sQueueMutex;
std::deque< SQueued > sQueue;


#endif // !_WIN32

// `at <update> <command>`: commands waiting for a game update (game thread only, no lock).
struct SDeferred {
  unsigned update;
  std::string line;
  unsigned generation;
};
std::vector< SDeferred > sDeferred;
unsigned sExecutingGeneration = 0;

#ifndef _WIN32
// macOS has no MSG_NOSIGNAL; SO_NOSIGPIPE is set on the client socket instead.
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

void SendRaw(unsigned generation, const std::string& text) {
  std::lock_guard< std::mutex > lock(sClientMutex);
  if (sClient < 0 || generation != sGeneration) {
    return;
  }
  size_t done = 0;
  int budget = kSendBudgetMs;
  while (done < text.size()) {
    const ssize_t n =
        send(sClient, text.data() + done, text.size() - done, kSendFlags | MSG_DONTWAIT);
    if (n > 0) {
      done += static_cast< size_t >(n);
      budget = kSendBudgetMs;
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && budget > 0) {
      pollfd descriptor{};
      descriptor.fd = sClient;
      descriptor.events = POLLOUT;
      const int wait = budget < 50 ? budget : 50;
      budget -= wait;
      poll(&descriptor, 1, wait);
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      std::fprintf(stderr, "[console] client is not reading; disconnecting it\n");
      shutdown(sClient, SHUT_RDWR);
    }
    return;
  }
}

// "GET / HTTP/1.1": a web page can POST to a loopback port, and its request line would otherwise run
// as a command. No console client speaks HTTP.
bool LooksLikeHttp(const std::string& line) {
  const size_t method = line.find(' ');
  if (method == std::string::npos || method == 0) {
    return false;
  }
  for (size_t i = 0; i < method; ++i) {
    if (line[i] < 'A' || line[i] > 'Z') {
      return false;
    }
  }
  const size_t version = line.rfind(" HTTP/");
  return version != std::string::npos && version > method;
}

void ListenThread(int listener) {
  std::string buffer;
  for (;;) {
    const int client = accept(listener, nullptr, nullptr);
#ifdef SO_NOSIGPIPE
    if (client >= 0) {
      const int on = 1;
      setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
    }
#endif
    if (client < 0) {
      if (errno != EINTR) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      continue;
    }
    unsigned generation;
    {
      std::lock_guard< std::mutex > lock(sClientMutex);
      if (sClient >= 0) {
        close(sClient);
      }
      sClient = client;
      generation = ++sGeneration;
    }
    sClientArrived.notify_all();
    std::fprintf(stderr, "[console] client connected\n");
    buffer.clear();
    bool firstLine = true;
    bool refused = false;
    char chunk[512];
    for (;;) {
      const ssize_t n = recv(client, chunk, sizeof(chunk), 0);
      if (n <= 0) {
        break;
      }
      buffer.append(chunk, static_cast< size_t >(n));
      size_t eol;
      while ((eol = buffer.find('\n')) != std::string::npos) {
        std::string line = buffer.substr(0, eol);
        buffer.erase(0, eol + 1);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        if (firstLine && LooksLikeHttp(line)) {
          std::fprintf(stderr, "[console] HTTP request refused; disconnecting the client\n");
          refused = true;
          break;
        }
        firstLine = false;
        bool queued = false;
        {
          std::lock_guard< std::mutex > lock(sQueueMutex);
          if (sQueue.size() < kMaxQueued) {
            sQueue.push_back(SQueued{line, generation});
            queued = true;
          }
        }
        if (!queued) {
          SendRaw(generation, "=> err: too many commands waiting\n");
        }
      }
      if (refused) {
        break;
      }
      if (buffer.size() > kMaxLine) {
        std::fprintf(stderr, "[console] line too long; disconnecting the client\n");
        break;
      }
    }
    {
      std::lock_guard< std::mutex > lock(sClientMutex);
      if (sClient == client) {
        close(sClient);
        sClient = -1;
      }
    }
    std::fprintf(stderr, "[console] client disconnected\n");
  }
}
#endif

// ---- commands ---------------------------------------------------------------------------------
//
// Each handler appends its output to `out` (one line per item) and returns false with `out` holding
// the reason on error. They run on the game thread with `mgr` live.

using Args = std::vector< std::string_view >;

Args Split(const std::string& line) {
  Args args;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
      ++i;
    }
    const size_t begin = i;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
      ++i;
    }
    if (i > begin) {
      args.emplace_back(line.data() + begin, i - begin);
    }
  }
  return args;
}

std::string Str(std::string_view text) { return std::string(text); }

int ArgToInt(std::string_view text, int fallback) {
  const std::string copy(text);
  char* end = nullptr;
  const long value = std::strtol(copy.c_str(), &end, 0);
  return (end != nullptr && *end == '\0') ? static_cast< int >(value) : fallback;
}

const char* VisorName(CPlayerState::EPlayerVisor visor) {
  switch (visor) {
  case CPlayerState::kPV_Combat:
    return "combat";
  case CPlayerState::kPV_Echo:
    return "echo";
  case CPlayerState::kPV_Dark:
    return "dark";
  case CPlayerState::kPV_Scan:
    return "scan";
  }
  return "?";
}

std::string Lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast< char >(c - 'A' + 'a');
    }
  }
  return out;
}

// A handler that cannot do its job calls Fail(why): Execute then answers `=> err: <why>` in place of
// `=> ok`. Handlers run one at a time on the game thread, so one slot is enough.
std::string sFailure;

void Fail(const std::string& why) { sFailure = why; }

// The name tables are generated from the upstream enums by tools/gen_console_names.py.
struct SEnumName {
  uint value;
  const char* name;
};
#include "console_names.inc"

constexpr int kMessageCount = static_cast< int >(sizeof(kMessageNames) / sizeof(SEnumName));
constexpr int kStateCount = static_cast< int >(sizeof(kStateNames) / sizeof(SEnumName));
constexpr int kItemCount = static_cast< int >(sizeof(kItemNames) / sizeof(SEnumName));

const char* EnumName(const SEnumName* table, int count, uint value) {
  for (int i = 0; i < count; ++i) {
    if (table[i].value == value) {
      return table[i].name;
    }
  }
  return nullptr;
}

// The table's name for `value`, or its hex code when the table has no entry for it.
std::string NameOrHex(const SEnumName* table, int count, uint value) {
  const char* name = EnumName(table, count, value);
  if (name != nullptr) {
    return name;
  }
  char hex[16];
  std::snprintf(hex, sizeof(hex), "%08x", value);
  return hex;
}

// The value an argument names: an entry of `table` (case-insensitive), else a number (base 0), else
// -1.
int EnumValue(const SEnumName* table, int count, std::string_view name) {
  const std::string wanted = Lower(name);
  for (int i = 0; i < count; ++i) {
    if (Lower(table[i].name) == wanted) {
      return static_cast< int >(table[i].value);
    }
  }
  const std::string copy(name);
  char* end = nullptr;
  const long value = std::strtol(copy.c_str(), &end, 0);
  return (!copy.empty() && end != nullptr && *end == '\0') ? static_cast< int >(value) : -1;
}

bool ParseFloat(std::string_view text, float& out) {
  const std::string copy(text);
  char* end = nullptr;
  const double value = std::strtod(copy.c_str(), &end);
  if (copy.empty() || end == nullptr || *end != '\0') {
    return false;
  }
  out = static_cast< float >(value);
  return true;
}

// The id as the game packs it (TUniqueId::value); Value() would drop the version bits. Read as hex,
// which is how every reply prints an id (`u046e`, and 0x046e/u046e are accepted too): `obj 400` is
// u0400, not decimal 400, so an id can always be typed back exactly as it was printed.
bool ParseUid(std::string_view text, TUniqueId& out) {
  std::string copy(text);
  if (!copy.empty() && (copy[0] == 'u' || copy[0] == 'U')) {
    copy.erase(0, 1);
  }
  if (copy.size() > 2 && copy[0] == '0' && (copy[1] == 'x' || copy[1] == 'X')) {
    copy.erase(0, 2);
  }
  char* end = nullptr;
  const unsigned long value = std::strtoul(copy.c_str(), &end, 16);
  if (copy.empty() || end == nullptr || *end != '\0' || value > 0xffff) {
    return false;
  }
  out = TUniqueId(static_cast< ushort >(value));
  return true;
}

// The class of an entity, demangled where the compiler can do it.
std::string ClassName(const CEntity& ent) {
  const char* mangled = typeid(ent).name();
#if defined(__GNUC__)
  int status = 0;
  char* demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
  const std::string name = status == 0 && demangled != nullptr ? demangled : mangled;
  std::free(demangled);
  return name;
#else
  return mangled;
#endif
}

// `visor` and `beam` take a name or an index in these orders, which are CPlayerState's enum orders
// and not the item order.
constexpr CPlayerState::EPlayerVisor kVisorOrder[] = {
    CPlayerState::kPV_Combat,
    CPlayerState::kPV_Echo,
    CPlayerState::kPV_Scan,
    CPlayerState::kPV_Dark,
};

struct SBeamChoice {
  const char* name;
  CPlayerState::EBeamId beam;
  CPlayerState::EItemType item; // The power-up that owns the beam.
};

constexpr SBeamChoice kBeamOrder[] = {
    {"power", CPlayerState::kBI_Power, CPlayerState::kIT_PowerBeam},
    {"dark", CPlayerState::kBI_Dark, CPlayerState::kIT_DarkBeam},
    {"light", CPlayerState::kBI_Light, CPlayerState::kIT_LightBeam},
    {"annihilator", CPlayerState::kBI_Annihilator, CPlayerState::kIT_AnnihilatorBeam},
};

constexpr int kVisorCount = static_cast< int >(sizeof(kVisorOrder) / sizeof(kVisorOrder[0]));
constexpr int kBeamCount = static_cast< int >(sizeof(kBeamOrder) / sizeof(kBeamOrder[0]));

const SBeamChoice* BeamChoice(CPlayerState::EBeamId beam) {
  for (const SBeamChoice& choice : kBeamOrder) {
    if (choice.beam == beam) {
      return &choice;
    }
  }
  return nullptr;
}

bool ParseVisor(std::string_view text, CPlayerState::EPlayerVisor& out) {
  const std::string wanted = Lower(text);
  for (const CPlayerState::EPlayerVisor visor : kVisorOrder) {
    if (wanted == VisorName(visor)) {
      out = visor;
      return true;
    }
  }
  const int index = ArgToInt(text, -1);
  if (index < 0 || index >= kVisorCount) {
    return false;
  }
  out = kVisorOrder[index];
  return true;
}

const SBeamChoice* ParseBeam(std::string_view text) {
  const std::string wanted = Lower(text);
  for (const SBeamChoice& choice : kBeamOrder) {
    if (wanted == choice.name) {
      return &choice;
    }
  }
  const int index = ArgToInt(text, -1);
  return (index >= 0 && index < kBeamCount) ? &kBeamOrder[index] : nullptr;
}

void CmdHelp(CStateManager&, const Args&, std::string& out);

// `status --json`: one line for scripts.
void CmdStatusJson(CStateManager& mgr, std::string& out) {
  CWorld* world = mgr.World();
  const CPlayer* player = mgr.GetPlayer(0);
  const CPlayerState* state = mgr.GetPlayerState(0);
  static const char* const morph[] = {"unmorphed", "morphed", "morphing", "unmorphing"};
  const auto flag = [](bool value) { return value ? "true" : "false"; };
  char line[512];
  uint32_t mlvl = 0;
  uint32_t mrea = 0;
  int areaIdx = -1;
  if (world != nullptr) {
    const TAreaId area = world->GetCurrentAreaId();
    const CGameArea* loaded = world->Area(area);
    areaIdx = area.Value();
    mlvl = static_cast< uint32_t >(world->IGetWorldAssetId());
    mrea = loaded != nullptr ? static_cast< uint32_t >(loaded->GetAreaAssetId()) : 0u;
  }
  std::snprintf(line, sizeof(line), "{\"frame\":%u,\"world\":\"%08X\",\"area\":%d,\"mrea\":\"%08X\"",
                static_cast< unsigned >(PortHarness::GameUpdates()), mlvl, areaIdx, mrea);
  out += line;
  if (player != nullptr) {
    const CVector3f p = player->GetTranslation();
    const CVector3f fwd = player->GetTransform().GetForward();
    const int m = static_cast< int >(player->GetMorphballTransitionState());
    std::snprintf(line, sizeof(line),
                  ",\"pos\":[%.3f,%.3f,%.3f],\"yaw\":%.2f,\"speed\":%.3f,\"morph\":\"%s\"", p.GetX(),
                  p.GetY(), p.GetZ(), std::atan2(-fwd.GetX(), fwd.GetY()) * 57.29578f,
                  player->GetVelocityWR().Magnitude(), m >= 0 && m < 4 ? morph[m] : "?");
    out += line;
  }
  if (state != nullptr) {
    std::snprintf(line, sizeof(line), ",\"hp\":%.1f,\"hp_max\":%.1f,\"visor\":\"%s\",\"beam\":%d",
                  state->GetHealthInfo().GetHP(), state->GetHealthInfo().GetInitialHP(),
                  VisorName(state->GetActiveVisor(mgr)), static_cast< int >(state->GetCurrentBeam()));
    out += line;
  }
  const PortFreeCam::Pose pose = PortFreeCam::GetPose();
  std::snprintf(line, sizeof(line),
                ",\"freecam\":{\"on\":%s,\"pos\":[%.3f,%.3f,%.3f],\"yaw\":%.2f,\"pitch\":%.2f}}\n",
                flag(PortFreeCam::Active()), pose.x, pose.y, pose.z, pose.yaw, pose.pitch);
  out += line;
}

void CmdStatus(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() > 1 && args[1] == "--json") {
    CmdStatusJson(mgr, out);
    return;
  }
  CWorld* world = mgr.World();
  CPlayer* player = mgr.GetPlayer(0);
  const CPlayerState* state = mgr.GetPlayerState(0);
  char line[320];
  std::snprintf(line, sizeof(line), "world %08x", world != nullptr ? world->IGetWorldAssetId() : 0u);
  out += line;
  if (world != nullptr) {
    const TAreaId area = world->GetCurrentAreaId();
    const CGameArea* loaded = world->Area(area);
    std::snprintf(line, sizeof(line), " area %d (mrea %08x)", area.Value(),
                  loaded != nullptr ? loaded->GetAreaAssetId() : 0u);
    out += line;
  }
  out += '\n';
  if (player != nullptr) {
    const CVector3f p = player->GetTranslation();
    std::snprintf(line, sizeof(line), "position %.2f %.2f %.2f  speed %.2f", p.GetX(), p.GetY(),
                  p.GetZ(), player->GetVelocityWR().Magnitude());
    out += line;
    std::snprintf(line, sizeof(line), "  morph %d  in-turret %d\n", static_cast< int >(player->GetMorphballTransitionState()),
                  player->IsInTurret() ? 1 : 0);
    out += line;
    // The input the game is acting on, which is not the pad's own: Toggle Lock-On and Sticky
    // Charge rewrite L and A after the pad has been read, so a latched button shows here and not
    // in `press`.
    const CFinalInput& in = mgr.mFinalInputs[0];
    std::snprintf(line, sizeof(line), "  orbit %d  game input L=%d A=%d\n",
                  static_cast< int >(player->GetOrbitState()), in.DL() || in.DLTrigger() ? 1 : 0,
                  in.DA() ? 1 : 0);
    out += line;
  }
  if (state != nullptr) {
    std::snprintf(line, sizeof(line), "health %.0f/%.0f  visor %s  beam %d\n",
                  state->GetHealthInfo().GetHP(), state->GetHealthInfo().GetInitialHP(),
                  VisorName(state->GetActiveVisor(mgr)), static_cast< int >(state->GetCurrentBeam()));
    out += line;
  }
  // Where the camera the frame is drawn from actually is: the player's position says nothing about
  // a detached camera, which is what a "nothing renders" report usually turns out to be.
  if (const CCameraManager* cameras = mgr.GetCameraManager(0)) {
    const CGameCamera* camera = cameras->GetCurrentCamera(mgr, false);
    const CTransform4f cameraXf = cameras->GetCurrentCameraTransform(mgr, false);
    const CVector3f at = cameraXf.GetTranslation();
    const char* kind = cameras->IsInCinematicCamera() ? "cinematic"
                       : cameras->IsInFPCamera()      ? "first-person"
                       : cameras->IsInBallCamera()    ? "ball"
                                                      : "other";
    std::snprintf(line, sizeof(line), "camera %s fov %.1f at (%.2f, %.2f, %.2f)\n", kind,
                  camera != nullptr ? camera->GetFov() : 0.f, at.GetX(), at.GetY(), at.GetZ());
    out += line;
    {
      // The view's pitch, so an aim drag's release can be read as a number.
      const CVector3f fwd = cameraXf.GetForward();
      std::snprintf(line, sizeof(line), "camera pitch %.1f deg (aim %s)\n",
                    asinf(fwd.GetZ()) * (180.f / 3.14159265f), PortMouse::Aiming() ? "on" : "off");
      out += line;
    }
    // The area's fog, because a white screen with the HUD drawn on it is what a fog range of
    // "everything" looks like from inside.
    const CGameArea::CAreaFog& fog = cameras->GetFog();
    const CColor fogColor = fog.GetColor();
    const CVector2f fogRange = fog.GetRange();
    std::snprintf(line, sizeof(line), "fog mode %d colour (%.2f, %.2f, %.2f) range %.1f..%.1f%s\n",
                  static_cast< int >(fog.GetFogMode()), fogColor.GetRed(), fogColor.GetGreen(),
                  fogColor.GetBlue(), fogRange.GetX(), fogRange.GetY(),
                  fog.IsFogDisabled() ? " disabled" : "");
    out += line;
  }
  // What the renderer will walk: CStateManager::GatherVisibleAreas only takes areas whose occlusion
  // state is kOS_Visible, so a current area that is not visible is a blank world with a normal HUD.
  if (CWorld* world = mgr.World()) {
    int alive = 0;
    std::string visibleList;
    for (CGameArea::CConstChainIterator it = world->GetChainHead(CWorld::kC_Alive);
         it != CWorld::GetAliveAreasEnd(); ++it) {
      ++alive;
      if (it->GetOcclusionState() == CGameArea::kOS_Visible) {
        visibleList += ' ';
        visibleList += std::to_string(it->GetId().Value());
      }
    }
    const CGameArea* current = world->Area(world->GetCurrentAreaId());
    std::snprintf(line, sizeof(line), "areas alive %d, visible:%s; current occlusion %s\n", alive,
                  visibleList.c_str(),
                  current == nullptr   ? "(not loaded)"
                  : current->GetOcclusionState() == CGameArea::kOS_Visible ? "visible"
                                                                           : "occluded");
    out += line;
    // How much the last frame asked the GPU to do. A world that is not drawn at all shows up here
    // as a handful of draw calls (the HUD) instead of hundreds.
    // The escape sequence's timer: while it is in (0, 1) CStateManager::DrawAdditionalFilters adds a
    // full-screen white filter whose alpha is 1 - timer, and the timer's own update clamps it to
    // FLT_EPSILON rather than 0 - so a timer left running is a permanently white screen under the
    // HUD. One line here saves the whole hunt.
    std::snprintf(line, sizeof(line), "escape timer %.6f%s\n",
                  gpGameState != nullptr ? gpGameState->GetEscapeTime() : 0.f,
                  gpGameState != nullptr && gpGameState->GetEscapeTime() > 0.f
                      ? "  <- the screen goes white"
                      : "");
    out += line;
    // The world transition manager's own state: kTT_Portal left running draws an additive white
    // full-screen quad (CWorldTransManager::DrawPortalTransition) that hides the world.
    if (!gpGameState->WorldTransitionManager().IsNull()) {
      const CWorldTransManager* trans = gpGameState->WorldTransitionManager().GetPtr();
      std::snprintf(line, sizeof(line), "transition type %d finished %d\n",
                    static_cast< int >(trans->GetTransType()),
                    trans->IsTransitionFinished() ? 1 : 0);
      out += line;
    }
    const CViewport& vp = CGraphics::GetViewport();
    std::snprintf(line, sizeof(line), "viewport %d,%d %dx%d\n", vp.mLeft, vp.mTop, vp.mWidth,
                  vp.mHeight);
    out += line;
    const AuroraStats* stats = aurora_get_stats();
    std::snprintf(line, sizeof(line), "frame %.1f fps, %u draw calls, %u verts%s\n",
                  aurora_get_fps(), stats != nullptr ? stats->drawCallCount : 0u,
                  stats != nullptr ? stats->lastVertSize : 0u,
                  stats != nullptr && stats->drawCallCount < 40 ? "  <- almost nothing drawn" : "");
    out += line;
    AuroraPipelineStats pipes{};
    aurora_get_pipeline_stats(&pipes);
    std::snprintf(line, sizeof(line),
                  "pipeline cache: %llu skipped draws, misses %llu new %llu known %llu late; %llu prefetches (%llu queued); %u pinned, %u known\n",
                  static_cast< unsigned long long >(pipes.skippedDraws),
                  static_cast< unsigned long long >(pipes.newMisses),
                  static_cast< unsigned long long >(pipes.knownMisses),
                  static_cast< unsigned long long >(pipes.lateMisses),
                  static_cast< unsigned long long >(pipes.prefetches),
                  static_cast< unsigned long long >(pipes.prefetchQueued), pipes.pinned, pipes.known);
    out += line;
    AuroraTextureStats textures{};
    aurora_get_texture_stats(&textures);
    std::snprintf(line, sizeof(line),
                  "textures: %u, %.1f MiB; render targets %u, %.1f MiB; lighting %u, %.1f MiB\n",
                  textures.count[0], textures.bytes[0] / 1048576.f, textures.count[1],
                  textures.bytes[1] / 1048576.f, textures.count[2], textures.bytes[2] / 1048576.f);
    out += line;
    AuroraDawnMemory dawn{};
    if (aurora_get_dawn_memory(&dawn)) {
      std::snprintf(line, sizeof(line),
                    "dawn: %.1f MiB estimated (textures %.1f, buffers %.1f); allocator %.1f MiB used of %.1f allocated\n",
                    dawn.total / 1048576.f, dawn.textures / 1048576.f, dawn.buffers / 1048576.f,
                    dawn.allocatorUsed / 1048576.f, dawn.allocatorAllocated / 1048576.f);
      out += line;
    }
    // Per-frame work counters (aurora/perf.h): render pass breaks by reason (each one is a full
    // colour+depth store and reload on a tiled GPU), GXCopyTex calls by caller (module offsets,
    // addr2line -f -e metroid_prime2_port <offset>), texture cache traffic, extra draws.
    if (const AuroraPerfFrame* perf = aurora_perf_last_frame(); perf != nullptr) {
      const uint32_t* b = perf->passBreaks;
      const uint32_t* c = perf->counters;
      std::snprintf(line, sizeof(line),
                    "perf breaks copy %u depthsnap %u encoder %u offscreen %u resolve %u | copytex %u"
                    " | tex hit %u miss %u hash %u upload %u init %u initdata %u | dstalphadup %u"
                    " bindinval %u\n",
                    b[AURORA_PERF_BREAK_COPY], b[AURORA_PERF_BREAK_DEPTH_SNAPSHOT],
                    b[AURORA_PERF_BREAK_ENCODER_TASK], b[AURORA_PERF_BREAK_OFFSCREEN],
                    b[AURORA_PERF_BREAK_RESOLVE], c[AURORA_PERF_COPY_TEX], c[AURORA_PERF_TEX_OBJ_HIT],
                    c[AURORA_PERF_TEX_OBJ_MISS], c[AURORA_PERF_TEX_HASH], c[AURORA_PERF_TEX_UPLOAD],
                    c[AURORA_PERF_TEX_INIT], c[AURORA_PERF_TEX_INIT_DATA], c[AURORA_PERF_DST_ALPHA_DUP],
                    c[AURORA_PERF_BIND_INVALIDATE]);
      out += line;
      if (perf->copySiteCount != 0) {
        out += "copy sites";
        for (uint32_t i = 0; i < perf->copySiteCount && i < AURORA_PERF_MAX_COPY_SITES; ++i) {
          std::snprintf(line, sizeof(line), " %#llx:%u",
                        static_cast< unsigned long long >(perf->copySiteOffset[i]), perf->copySiteHits[i]);
          out += line;
        }
        out += "\n";
      }
    }
  }
}

void CmdAreas(CStateManager& mgr, const Args&, std::string& out) {
  CWorld* world = mgr.World();
  if (world == nullptr) {
    out += "no world yet\n";
    return;
  }
  // Per area: phase/chain/active, the live objects it owns (what holds the 1024-entry id pool) and
  // the areas its docks attach, so "who loaded this neighbour" can be read off one reply.
  const int count = world->IGetAreaCount();
  std::vector< int > owned(count > 0 ? count : 0, 0);
  const CObjectList& list = mgr.GetObjectListById(kOL_All);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const int a = list[i]->GetCurrentAreaId().Value();
    if (a >= 0 && a < count) {
      ++owned[a];
    }
  }
  char line[160];
  for (int i = 0; i < count; ++i) {
    const CGameArea* area = world->Area(TAreaId(i));
    const IGameArea* any = world->IGetAreaAlways(TAreaId(i));
    std::snprintf(line, sizeof(line), "%2d  %08x  %s", i,
                  any != nullptr ? any->IGetAreaAssetId() : 0u,
                  area != nullptr ? "loaded" : "");
    out += line;
    if (area != nullptr) {
      std::snprintf(line, sizeof(line), " phase %d chain %d%s%s  %d objs  attached", area->GetPhase(),
                    area->GetCurChain(), area->IsActive() ? " active" : "",
                    area->IsLoaded() ? " ready" : "", owned[i]);
      out += line;
      for (int k = 0; k < area->GetNumAttachedAreas(); ++k) {
        std::snprintf(line, sizeof(line), " %d", area->GetAttachedAreaId(k).Value());
        out += line;
      }
      // The world-space AABB CGroundMovement::CheckFalling tests the player against: outside every
      // alive area's box the standing player is held up (retail 0x8012C5C8).
      const CAABox& b = area->GetAABB();
      std::snprintf(line, sizeof(line), "  aabb (%.1f %.1f %.1f)-(%.1f %.1f %.1f)",
                    b.GetMinPoint().GetX(), b.GetMinPoint().GetY(), b.GetMinPoint().GetZ(),
                    b.GetMaxPoint().GetX(), b.GetMaxPoint().GetY(), b.GetMaxPoint().GetZ());
      out += line;
    }
    out += '\n';
  }
}

void CmdWarp(CStateManager&, const Args& args, std::string& out) {
  if (args.size() < 2) {
    Fail("usage: warp <slot|id|name>");
    return;
  }
  const std::string area = Str(args[1]);
  PortWarp::Area(area.c_str());
  out += "warp queued for '" + area + "'\n";
}

void CmdNextArea(CStateManager&, const Args&, std::string&) { PortWarp::CycleArea(1); }
void CmdPrevArea(CStateManager&, const Args&, std::string&) { PortWarp::CycleArea(-1); }
void CmdDoor(CStateManager&, const Args&, std::string&) { PortWarp::Door(); }
void CmdDock(CStateManager&, const Args&, std::string&) { PortWarp::Dock(); }
void CmdDimension(CStateManager&, const Args&, std::string&) { PortWarp::Dimension(); }
void CmdStation(CStateManager&, const Args&, std::string&) { PortWarp::Station(); }
void CmdHeal(CStateManager&, const Args&, std::string&) { PortWarp::Heal(); }
const CEntity* ResolveObject(CStateManager& mgr, std::string_view text);

// kill [id]: no id kills the player; an id zeroes a patterned actor's health and runs its Death the
// way ApplyDamage does (kSS_DeathRattle), so an actor that reflects every weapon (the Splitter's
// chassis) can still be killed.
void CmdKill(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() > 2) {
    Fail("usage: kill [id | target]   (no id: the player; target: the locked-on actor)");
    return;
  }
  if (args.size() == 1) {
    PortWarp::Kill();
    return;
  }
  CEntity* ent = nullptr;
  if (Lower(args[1]) == "target") {
    const CPlayer* player = mgr.GetPlayer(0);
    ent = player != nullptr ? mgr.ObjectById(player->GetOrbitTargetId()) : nullptr;
    if (ent == nullptr) {
      Fail("no locked-on target");
      return;
    }
  } else {
    ent = const_cast< CEntity* >(ResolveObject(mgr, args[1]));
  }
  CPatterned* patterned = ent != nullptr ? TCastToPtr< CPatterned >(ent) : nullptr;
  if (patterned == nullptr || patterned->GetHealthInfo() == nullptr) {
    Fail("not a patterned actor with health");
    return;
  }
  const_cast< CHealthInfo* >(patterned->GetHealthInfo())->SetHP(0.f);
  CVector3f direction = CVector3f::Zero();
  if (const CPlayer* player = mgr.GetPlayer(0)) {
    direction = patterned->GetTranslation() - player->GetTranslation();
  }
  patterned->Death(mgr, direction, kSS_DeathRattle);
  out += "killed\n";
}

// refill [energy|ammo]: energy up to the full tank count; ammo up to every capacity the suit has
// (no argument: both). Unlike `heal` this also touches missiles, power bombs and beam ammo.
void CmdRefill(CStateManager& mgr, const Args& args, std::string& out) {
  const std::string what = args.size() > 1 ? Lower(args[1]) : std::string("all");
  if (args.size() > 2 || (what != "all" && what != "energy" && what != "ammo")) {
    Fail("usage: refill [energy | ammo]");
    return;
  }
  CPlayerState* state = mgr.PlayerState(0);
  if (state == nullptr) {
    Fail("no player state");
    return;
  }
  if (what != "ammo") {
    PortWarp::Heal();
  }
  if (what != "energy") {
    for (const CPlayerState::EItemType type :
         {CPlayerState::kIT_Missile, CPlayerState::kIT_Powerbomb, CPlayerState::kIT_DarkAmmo,
          CPlayerState::kIT_LightAmmo}) {
      const int capacity = state->GetItemCapacity(type);
      state->AddPowerUp(type, capacity - state->GetItemAmount(type));
    }
  }
  out += "refilled " + what + "\n";
}

void CmdLayer(CStateManager&, const Args& args, std::string&) {
  if (args.size() < 2) {
    Fail("usage: layer <n> | layer off <n>");
    return;
  }
  if (Str(args[1]) == "off") {
    if (args.size() < 3) {
      Fail("usage: layer off <n>");
      return;
    }
    PortWarp::LayerOff(ArgToInt(args[2], 0));
    return;
  }
  PortWarp::Layer(ArgToInt(args[1], 0));
}

// settled: whether a warp or layer request is still being served, and how many object ids are
// taken. tools/statecheck.py polls it after a `layer` setup command instead of sleeping.
void CmdSettled(CStateManager& mgr, const Args&, std::string& out) {
  const bool busy = PortWarp::Pending() || mgr.PortLayerRequestPending();
  char line[96];
  std::snprintf(line, sizeof line, "busy %d free_ids %d frame %u\n", busy ? 1 : 0,
                mgr.PortFreeObjectIds(), static_cast< unsigned >(mgr.GetUpdateFrameIdx()));
  out += line;
}

// world <world> [area]: the MP1 console's `warp <world>`, under its own verb because this tree's
// `warp` is the area warp the sweeps and the docs use. Both texts are a name or a hex asset id and
// are resolved where the resource names live (PortWarpToWorld), so nothing here needs a table.
void CmdWorld(CStateManager&, const Args& args, std::string& out) {
  if (args.size() < 2) {
    Fail("usage: world <world name or hex> [area name or hex]");
    return;
  }
  const std::string world = Str(args[1]);
  const std::string area = args.size() > 2 ? Str(args[2]) : std::string();
  PortWarp::World(world.c_str(), area.c_str());
  out += "world warp queued for '" + world + "'" + (area.empty() ? "" : " area '" + area + "'") +
         "\n";
}

// worlds: the memory card's world table, which is what `world <id>` takes. The names are STRG assets
// (the card keeps the id, not the text), so `strg <that id>` prints one.
void CmdWorlds(CStateManager& mgr, const Args&, std::string& out) {
  if (gpMemoryCard == nullptr) {
    Fail("no memory card yet");
    return;
  }
  const rstl::vector< CMemoryCard::MemoryWorld >& worlds = gpMemoryCard->GetMemoryWorlds();
  const CWorld* world = mgr.World();
  const CAssetId current = world != nullptr ? world->IGetWorldAssetId() : kInvalidAssetId;
  char line[160];
  for (int i = 0; i < worlds.size(); ++i) {
    const CAssetId id = worlds[i].first;
    const CSaveWorldMemory& memory = worlds[i].second;
    std::snprintf(line, sizeof(line), "%08x  %2d areas  name STRG %08x%s\n", id,
                  memory.GetAreaCount(), memory.GetWorldNameId(),
                  id == current ? "  <- current" : "");
    out += line;
  }
}

// One game string as a plain UTF-8 console line, including translated STRG tables.
std::string Narrow(const wchar_t* text) {
  return PortDiscord::GameTextToUtf8(text);
}

// The string table `strg` is reading (see there).
TToken< CStringTable >* sTable = nullptr;
CAssetId sTableId = kInvalidAssetId;

// strg <id|name> [index|name]: a string table as the game loads it, for reading a message a level
// script refers to. The asset is a number or a resource name ("STRG_AllPickupsFound_2"), and the
// load is asynchronous, so a second call is what reads it when the first only locked it.
void CmdStrg(CStateManager&, const Args& args, std::string& out) {
  if (args.size() < 2) {
    Fail("usage: strg <asset id or STRG_... name> [index|name]");
    return;
  }
  CAssetId id = kInvalidAssetId;
  const int numeric = ArgToInt(args[1], -1);
  if (numeric >= 0) {
    id = static_cast< CAssetId >(numeric);
  } else if (gpResourceFactory != nullptr) {
    const SObjectTag* tag = gpResourceFactory->GetResourceIdByName(Str(args[1]).c_str());
    if (tag != nullptr) {
      id = tag->id;
    }
  }
  if (id == kInvalidAssetId) {
    Fail("no such resource '" + Str(args[1]) + "'");
    return;
  }
  // The table is kept alive between calls: the load is asynchronous, and a token that dies with the
  // command cancels it, so every call would answer "still loading".
  if (sTable == nullptr || sTableId != id) {
    delete sTable;
    sTable = rs_new TToken< CStringTable >(gpSimplePool->GetObj(SObjectTag('STRG', id)));
    sTableId = id;
    sTable->Lock();
  }
  if (!sTable->IsLoaded()) {
    out += "STRG " + std::to_string(id) + " is still loading; ask again\n";
    return;
  }
  TToken< CStringTable >& table = *sTable;
  const int count = table->GetStringCount();
  char line[320];
  if (args.size() > 2) {
    int index = ArgToInt(args[2], -1);
    if (index < 0) {
      index = table->GetStringIndex(Str(args[2]).c_str());
    }
    if (index < 0 || index >= count) {
      Fail("no string '" + Str(args[2]) + "' in STRG " + std::to_string(id));
      return;
    }
    std::snprintf(line, sizeof(line), "STRG %u [%d]: ", id, index);
    out += line;
    out += Narrow(table->GetString(index));
    out += '\n';
    return;
  }
  std::snprintf(line, sizeof(line), "STRG %u: %d strings\n", id, count);
  out += line;
  // Enough to see what a table is without flooding the socket.
  for (int i = 0; i < count && i < 40; ++i) {
    std::snprintf(line, sizeof(line), "  [%2d] ", i);
    out += line;
    out += Narrow(table->GetString(i));
    out += '\n';
  }
  if (count > 40) {
    out += "  ...\n";
  }
}

void CmdGod(CStateManager&, const Args& args, std::string& out) {
  const bool enabled = args.size() < 2 ? !PortOverlay::GodMode() : ArgToInt(args[1], 1) != 0;
  PortOverlay::SetGodMode(enabled);
  out += enabled ? "god mode on\n" : "god mode off\n";
}

void CmdNoDarkAether(CStateManager&, const Args& args, std::string& out) {
  const bool enabled =
      args.size() < 2 ? !PortOverlay::NoDarkAetherDamage() : ArgToInt(args[1], 1) != 0;
  PortOverlay::SetNoDarkAetherDamage(enabled);
  out += enabled ? "no dark aether damage on\n" : "no dark aether damage off\n";
}

// infammo [all|missile|powerbomb|dark|light] [0|1]: the F1 Cheats "Infinite" boxes; no 0|1 flips.
void CmdInfiniteAmmo(CStateManager&, const Args& args, std::string& out) {
  struct Kind {
    const char* name;
    unsigned bits;
  };
  static const Kind kKinds[] = {{"all", PortOverlay::kInfAmmoAll},
                                {"missile", PortOverlay::kInfAmmoMissile},
                                {"powerbomb", PortOverlay::kInfAmmoPowerBomb},
                                {"dark", PortOverlay::kInfAmmoDark},
                                {"light", PortOverlay::kInfAmmoLight}};
  size_t next = 1;
  unsigned kinds = PortOverlay::kInfAmmoAll;
  if (args.size() > next && !args[next].empty() && (args[next][0] < '0' || args[next][0] > '9')) {
    kinds = 0;
    for (const Kind& kind : kKinds) {
      if (args[next] == kind.name) {
        kinds = kind.bits;
      }
    }
    if (kinds == 0) {
      out += "usage: infammo [all|missile|powerbomb|dark|light] [0|1]\n";
      return;
    }
    ++next;
  }
  const bool enabled =
      args.size() <= next ? !PortOverlay::InfiniteAmmo(kinds) : ArgToInt(args[next], 1) != 0;
  PortOverlay::SetInfiniteAmmo(kinds, enabled);
  out += "infinite ammo:";
  for (const Kind& kind : kKinds) {
    if (kind.bits != PortOverlay::kInfAmmoAll) {
      out += ' ';
      out += kind.name;
      out += PortOverlay::InfiniteAmmo(kind.bits) ? " on" : " off";
    }
  }
  out += '\n';
}

// `mpforce <0|1>`: CStateManager::IsMultiplayer reports true, so multiplayer-only arms run in
// single player. The game mode itself stays single player (no score, no split screen).
void CmdMpForce(CStateManager&, const Args& args, std::string& out) {
  gPortForceMultiplayer = args.size() < 2 ? !gPortForceMultiplayer : ArgToInt(args[1], 1) != 0;
  out += gPortForceMultiplayer ? "forced multiplayer on\n" : "forced multiplayer off\n";
}

void CmdFreeCam(CStateManager& mgr, const Args& args, std::string& out) {
  // freecam player 0|1 | pos <x> <y> <z> | look <yaw> <pitch>: Samus's body and the pose (as MP1).
  if (args.size() >= 3 && args[1] == "player") {
    PortFreeCam::SetShowPlayer(ArgToInt(args[2], 1) != 0);
    out += PortFreeCam::ShowPlayer() ? "player shown\n" : "player hidden\n";
    return;
  }
  if (args.size() >= 5 && args[1] == "pos") {
    PortFreeCam::Pose pose = PortFreeCam::GetPose();
    pose.x = static_cast< float >(std::atof(std::string(args[2]).c_str()));
    pose.y = static_cast< float >(std::atof(std::string(args[3]).c_str()));
    pose.z = static_cast< float >(std::atof(std::string(args[4]).c_str()));
    PortFreeCam::SetPose(pose);
    out += "pose set\n";
    return;
  }
  if (args.size() >= 4 && args[1] == "look") {
    PortFreeCam::Pose pose = PortFreeCam::GetPose();
    pose.yaw = static_cast< float >(std::atof(std::string(args[2]).c_str()));
    pose.pitch = static_cast< float >(std::atof(std::string(args[3]).c_str()));
    PortFreeCam::SetPose(pose);
    out += "pose set\n";
    return;
  }
  const bool enabled = args.size() < 2 ? !PortFreeCam::Active() : ArgToInt(args[1], 1) != 0;
  PortFreeCam::SetActive(enabled, &mgr);
  out += enabled ? "free camera on\n" : "free camera off\n";
}

// `photo [on|off|1|2|4]`: photo mode (the free camera with the HUD hidden and the game frozen, the
// F8 binding). `on`/`off` enter and leave; a factor saves a PNG supersampled by it (the setting
// when none); no argument enters when off and captures when on. The shot is taken a few frames
// later; its result is logged as `[photo] ...`.
void CmdPhoto(CStateManager& mgr, const Args& args, std::string& out) {
  const std::string arg = args.size() < 2 ? std::string() : std::string(args[1]);
  if (!arg.empty() && arg != "on" && arg != "off" && arg != "1" && arg != "2" && arg != "4" &&
      arg != "samus") {
    out += "usage: photo [on|off|1|2|4|samus 0|1]\n";
    return;
  }
  if (arg == "off") {
    PortPhoto::Exit();
    out += "photo mode off\n";
    return;
  }
  if (args.size() >= 3 && args[1] == "samus") {
    PortPhoto::SetShowSamus(ArgToInt(args[2], 1) != 0);
    out += PortPhoto::ShowSamus() ? "samus shown\n" : "samus hidden\n";
    return;
  }
  if (!PortPhoto::Active()) {
    std::string why;
    if (!PortPhoto::Enter(mgr, why)) {
      out += "photo mode refused: " + why + "\n";
    } else {
      out += "photo mode on\n";
    }
    if (arg.empty() || arg == "on") {
      return;
    }
    if (!PortPhoto::Active()) {
      return;
    }
  } else if (arg == "on") {
    out += "photo mode already on\n";
    return;
  }
  if (PortPhoto::Capturing()) {
    out += "a capture is already running\n";
    return;
  }
  const int factor = (arg.empty() || arg == "on") ? 0 : ArgToInt(arg, 1);
  PortPhoto::RequestCapture(factor);
  const PortPhoto::CaptureResult last = PortPhoto::LastCapture();
  out += "capture requested (";
  out += factor > 0 ? std::to_string(factor) : std::to_string(PortPhoto::Supersample());
  out += "x); last: ";
  out += last.state == 0 ? "none" : last.state > 0 ? "saved" : "failed";
  out += "\n";
}

void CmdSkipCine(CStateManager&, const Args& args, std::string& out) {
  const bool enabled = args.size() < 2 ? !PortOverlay::SkipCinematics() : ArgToInt(args[1], 1) != 0;
  PortOverlay::SetSkipCinematics(enabled);
  out += enabled ? "skip cutscenes on\n" : "skip cutscenes off\n";
}

// skipprompt: show the "Press [A] to skip" prompt as a first press in a cinematic would, for screenshots.
void CmdSkipPrompt(CStateManager&, const Args&, std::string& out) {
  PortOverlay::SkipConfirmArm();
  out += "skip prompt shown\n";
}

// sfx: CSfxManager state (channel, mMuted, mDoUpdate, live sounds per channel) and MusyX voices.
void CmdSfx(CStateManager&, const Args&, std::string& out) {
  int channel = 0;
  bool muted = false, doUpdate = false;
  int live[4];
  int poolFree[2];
  CSfxManager::PortDebugState(channel, muted, doUpdate, live, poolFree);
  PortMusyxVoice voices[64];
  const int voiceCount = MusyxPortCopyVoices(voices, 64);
  out += "sfx: channel=" + std::to_string(channel) + " muted=" + std::to_string(muted) +
         " doUpdate=" + std::to_string(doUpdate) + " live=[" + std::to_string(live[0]) + "," +
         std::to_string(live[1]) + "," + std::to_string(live[2]) + "," + std::to_string(live[3]) +
         "] poolFree=" + std::to_string(poolFree[0]) + "/" + std::to_string(poolFree[1]) +
         " musyxVoices=" + std::to_string(voiceCount) + "\n";
  for (int i = 0; i < voiceCount; ++i) {
    const PortMusyxVoice& v = voices[i];
    out += "  voice smp=" + std::to_string(v.smpId) + " looped=" + std::to_string(v.looped) +
           " len=" + std::to_string(v.length) + " loopLen=" + std::to_string(v.loopLength) +
           " pos=" + std::to_string(v.srcPos) + " rms=" + std::to_string(v.rms) +
           " vol=" + std::to_string(v.volL) + "/" + std::to_string(v.volR) + "\n";
  }
}

// credits [1|2]: end the game into the credits (CGMSinglePlayer::EndGame's kRM_Credits1/2 route).
void CmdCredits(CStateManager& mgr, const Args& args, std::string& out) {
  const int which = args.size() < 2 ? 1 : ArgToInt(args[1], 1);
  gpMain->SetRestartMode(which == 2 ? CMain::kRM_Credits2 : CMain::kRM_Credits1);
  mgr.QuitGame();
  out += "credits requested\n";
}

// The gameplay tweaks: the same shape as `god` and `skipcine`, an optional 0/1 and a print of the
// state it ends in, because the F1 row and the env var set the same thing.
void CmdFastMorph(CStateManager&, const Args& args, std::string& out) {
  const bool enabled = args.size() < 2 ? !PortOverlay::FastMorph() : ArgToInt(args[1], 1) != 0;
  PortOverlay::SetFastMorph(enabled);
  out += enabled ? "fast morph on\n" : "fast morph off\n";
}

// hintshow: the F1 "Show current hint" button (hint_delay's companion); the next hint update acts.
void CmdHintShow(CStateManager&, const Args&, std::string& out) {
  PortOverlay::RequestShowHint();
  out += "hint requested\n";
}

// memo <0|1> <text...>: a HUD memo; 1 makes it a hint memo (the framed box with the button ring).
// `{` and `}` in the text stand for `&` and `;`, so a tag (`{image=SI,1,1,F1FA8D68}`) gets past the
// `;` that splits console commands.
// memo pause <strg>: the boxed memo a paused HUDMemo opens (CMessageScreen), as for "Map data acquired".
void CmdMemo(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() == 3 && args[1] == "pause") {
    CAssetId id = kInvalidAssetId;
    const std::string text(args[2]);
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 0);
    if (end != nullptr && *end == '\0' && end != text.c_str()) {
      id = static_cast< CAssetId >(value);
    } else if (gpResourceFactory != nullptr) {
      const SObjectTag* tag = gpResourceFactory->GetResourceIdByName(text.c_str());
      if (tag != nullptr) {
        id = tag->id;
      }
    }
    if (id == kInvalidAssetId) {
      Fail("no such resource '" + text + "'");
      return;
    }
    mgr.ShowPausedHUDMemo(id, 0.f);
    out += "paused memo requested\n";
    return;
  }
  if (args.size() < 3) {
    Fail("usage: memo <0|1> <text...> | memo pause <strg>");
    return;
  }
  const bool hint = ArgToInt(args[1], 0) != 0;
  std::wstring wide = L"&just=center;";
  for (size_t i = 2; i < args.size(); ++i) {
    if (i > 2) {
      wide += L' ';
    }
    wide.append(args[i].begin(), args[i].end());
  }
  for (wchar_t& c : wide) {
    c = c == L'{' ? L'&' : (c == L'}' ? L';' : c);
  }
  CSamusHud::DisplayHudMemo(rstl::wstring(wide.c_str()),
                            CHUDMemoParms(hint ? 0.f : 5.f, true, false, hint, 1, true));
  out += hint ? "hint memo shown\n" : "memo shown\n";
}

// walkspeed [50..200]: on-foot movement speed in percent (walk_speed); no argument prints it.
void CmdWalkSpeed(CStateManager&, const Args& args, std::string& out) {
  if (args.size() > 1) {
    const int value = ArgToInt(args[1], -1);
    if (value < PortOverlay::kWalkSpeedMin || value > PortOverlay::kWalkSpeedMax) {
      Fail("usage: walkspeed [50..200]");
      return;
    }
    PortOverlay::SetWalkSpeed(value);
  }
  out += "walkspeed " + std::to_string(PortOverlay::WalkSpeed()) + "\n";
}

// scanspeed [50..400]: scan speed in percent (scan_speed); no argument prints it.
void CmdScanSpeed(CStateManager&, const Args& args, std::string& out) {
  if (args.size() > 1) {
    const int value = ArgToInt(args[1], -1);
    if (value < PortOverlay::kScanSpeedMin || value > PortOverlay::kScanSpeedMax) {
      Fail("usage: scanspeed [50..400]");
      return;
    }
    PortOverlay::SetScanSpeed(value);
  }
  out += "scanspeed " + std::to_string(PortOverlay::ScanSpeed()) + "\n";
}

// language [0..6]: the text language (language), applied while the game runs; no argument prints it.
void CmdLanguage(CStateManager&, const Args& args, std::string& out) {
  if (args.size() > 1) {
    const int value = ArgToInt(args[1], -1);
    if (value < 0 || value > PortOverlay::kLanguageMax) {
      Fail("usage: language [0..6]");
      return;
    }
    PortOverlay::SetLanguage(value);
  }
  out += "language " + std::to_string(PortOverlay::Language()) + "\n";
}

// elevator [original|fast|skip]: the ride between worlds (elevator_ride); no argument prints it.
void CmdElevator(CStateManager&, const Args& args, std::string& out) {
  static const char* const kNames[] = {"original", "fast", "skip"};
  if (args.size() > 1) {
    int mode = -1;
    for (int i = 0; i < 3; ++i) {
      if (args[1] == kNames[i]) {
        mode = i;
      }
    }
    if (mode < 0) {
      Fail("usage: elevator [original|fast|skip]");
      return;
    }
    PortOverlay::SetElevatorRide(mode);
  }
  out += std::string("elevator ") + kNames[PortOverlay::ElevatorRide()] + "\n";
}

// combocost / combomissiles / beamammo [n]: the F1 Cheats sliders; no argument prints the value.
void CmdComboCost(CStateManager&, const Args& args, std::string& out) {
  if (args.size() > 1) {
    PortOverlay::SetComboBeamCost(ArgToInt(args[1], PortOverlay::kComboBeamCostMax));
  }
  out += "combo beam ammo cost " + std::to_string(PortOverlay::ComboBeamCost()) + "\n";
}

void CmdComboMissiles(CStateManager&, const Args& args, std::string& out) {
  if (args.size() > 1) {
    PortOverlay::SetComboMissileCost(ArgToInt(args[1], PortOverlay::kComboMissileCostMax));
  }
  out += "combo missile cost " + std::to_string(PortOverlay::ComboMissileCost()) + "\n";
}

void CmdBeamAmmo(CStateManager&, const Args& args, std::string& out) {
  if (args.size() > 1) {
    PortOverlay::SetBeamAmmoPerExpansion(ArgToInt(args[1], PortOverlay::kBeamAmmoPerExpansionDefault));
  }
  out += "beam ammo per expansion " + std::to_string(PortOverlay::BeamAmmoPerExpansion()) + "\n";
}

// rooms [original|fast]: the rotating-room cutscenes (rotating_rooms); no argument prints it.
void CmdRooms(CStateManager&, const Args& args, std::string& out) {
  static const char* const kNames[] = {"original", "fast"};
  if (args.size() > 1) {
    int mode = -1;
    for (int i = 0; i < 2; ++i) {
      if (args[1] == kNames[i]) {
        mode = i;
      }
    }
    if (mode < 0) {
      Fail("usage: rooms [original|fast]");
      return;
    }
    PortOverlay::SetRotatingRooms(mode);
  }
  out += std::string("rooms ") + kNames[PortOverlay::RotatingRooms()] + "\n";
}

void CmdPortal(CStateManager&, const Args& args, std::string& out) {
  static const char* const kNames[] = {"original", "fast", "skip"};
  if (args.size() > 1) {
    int mode = -1;
    for (int i = 0; i < 3; ++i) {
      if (args[1] == kNames[i]) {
        mode = i;
      }
    }
    if (mode < 0) {
      Fail("usage: portal [original|fast|skip]");
      return;
    }
    PortOverlay::SetPortalTransition(mode);
  }
  out += std::string("portal ") + kNames[PortOverlay::PortalTransition()] + "\n";
}

// original [0|1]: the Original experience switch (original_experience); no argument prints it.
void CmdOriginal(CStateManager&, const Args& args, std::string& out) {
  if (args.size() > 1) {
    PortOverlay::SetOriginalExperience(ArgToInt(args[1], 1) != 0);
  }
  out += std::string("original ") + (PortOverlay::OriginalExperience() ? "1" : "0") + "\n";
}

// igt [0|1]: the on-screen in-game time (igt_timer); no argument prints it.
void CmdIgt(CStateManager&, const Args& args, std::string& out) {
  if (args.size() > 1) {
    PortOverlay::SetIgtTimer(ArgToInt(args[1], 1) != 0);
  }
  out += std::string("igt ") + (PortOverlay::IgtTimer() ? "1" : "0") + "\n";
}

// pointer <x> <y> move|click|dclick|tap|dtap|down|up|drag|wheel|pinch ... | back | where | hits:
// scripted mouse/touch input for the pointer menus, in GUI (EFB) pixels as screenshots show them.
// Pushes one SDL finger event at window pixel x,y, as a touch screen would: the touch overlay's
// event filter sees it as it is pushed. Tests the touch overlay on a desktop build (MP1's `finger`).
bool PushFinger(Uint32 type, SDL_FingerID id, float x, float y) {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  SDL_Window* window = windows != nullptr && count > 0 ? windows[0] : nullptr;
  SDL_free(windows);
  int w = 0;
  int h = 0;
  if (window == nullptr || !SDL_GetWindowSizeInPixels(window, &w, &h) || w <= 0 || h <= 0) {
    return false;
  }
  SDL_Event e{};
  e.type = type;
  e.tfinger.timestamp = SDL_GetTicksNS();
  e.tfinger.touchID = 1;
  e.tfinger.fingerID = id;
  e.tfinger.x = x / float(w);
  e.tfinger.y = y / float(h);
  e.tfinger.pressure = type == SDL_EVENT_FINGER_UP ? 0.f : 1.f;
  e.tfinger.windowID = SDL_GetWindowID(window);
  SDL_PushEvent(&e);
  return true;
}

void CmdFinger(CStateManager&, const Args& args, std::string&) {
  const auto num = [&](size_t i) { return static_cast< float >(std::atof(std::string(args[i]).c_str())); };
  const char* usage =
      "usage: finger down|move|up <id> <x> <y> | tap <x> <y> | drag <x0> <y0> <x1> <y1> [steps]";
  bool ok = true;
  if (args.size() == 5 && (args[1] == "down" || args[1] == "move" || args[1] == "up")) {
    const Uint32 type = args[1] == "down" ? SDL_EVENT_FINGER_DOWN
                        : args[1] == "move" ? SDL_EVENT_FINGER_MOTION
                                            : SDL_EVENT_FINGER_UP;
    ok = PushFinger(type, SDL_FingerID(std::atoll(std::string(args[2]).c_str())), num(3), num(4));
  } else if (args.size() == 4 && args[1] == "tap") {
    ok = PushFinger(SDL_EVENT_FINGER_DOWN, 1, num(2), num(3)) &&
         PushFinger(SDL_EVENT_FINGER_UP, 1, num(2), num(3));
  } else if ((args.size() == 6 || args.size() == 7) && args[1] == "drag") {
    const int steps = args.size() == 7 ? std::max(1, std::atoi(std::string(args[6]).c_str())) : 10;
    const float x0 = num(2), y0 = num(3), x1 = num(4), y1 = num(5);
    ok = PushFinger(SDL_EVENT_FINGER_DOWN, 1, x0, y0);
    for (int i = 1; ok && i <= steps; ++i) {
      const float t = float(i) / float(steps);
      PushFinger(SDL_EVENT_FINGER_MOTION, 1, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
    }
    if (ok) {
      PushFinger(SDL_EVENT_FINGER_UP, 1, x1, y1);
    }
  } else {
    Fail(usage);
    return;
  }
  if (!ok) {
    Fail("no window");
  }
}

void CmdPointer(CStateManager&, const Args& args, std::string& out) {
  static const char* const kUsage =
      "usage: pointer <x> <y> move|click|dclick|tap|dtap|down|up [l|r|touch]|drag <x1> <y1> "
      "[l|r|touch]|wheel <n>|pinch <ratio> | pointer back | pointer where | pointer hits";
  const std::string verb = args.size() > 1 ? Lower(args[1]) : std::string();
  if (verb == "back") {
    PortPointer::ScriptBack();
    return;
  }
  if (verb == "hits") {
    out += PortGuiHit::Describe(-1.f, -1.f) + "\n";
    return;
  }
  if (verb == "where") {
    float gx = 0.f, gy = 0.f;
    const bool has = PortPointer::Position(gx, gy);
    char line[160];
    std::snprintf(line, sizeof(line), "pointer %s %.1f %.1f last=%s screen=%d enabled=%d\n",
                  has ? "at" : "none", gx, gy,
                  PortPointer::MouseIsLast()   ? "mouse"
                  : PortPointer::TouchIsLast() ? "touch"
                                               : "other",
                  PortPointer::ScreenActive() ? 1 : 0, PortPointer::Enabled() ? 1 : 0);
    out += line;
    out += PortGuiHit::Describe(gx, gy) + "\n";
    return;
  }
  if (args.size() < 4) {
    Fail(kUsage);
    return;
  }
  const float x = static_cast< float >(std::atof(std::string(args[1]).c_str()));
  const float y = static_cast< float >(std::atof(std::string(args[2]).c_str()));
  const std::string action = Lower(args[3]);
  if (action == "move") {
    PortPointer::ScriptMove(x, y);
  } else if (action == "click" || action == "dclick") {
    PortPointer::ScriptClick(x, y, action == "dclick", false);
  } else if (action == "tap" || action == "dtap") {
    PortPointer::ScriptClick(x, y, action == "dtap", true);
  } else if (action == "down" || action == "up") {
    const std::string how = args.size() > 4 ? Lower(args[4]) : std::string("l");
    PortPointer::ScriptPress(x, y, action == "down",
                             how == "r" ? PortPointer::kButtonRight : PortPointer::kButtonLeft,
                             how == "touch");
  } else if (action == "drag" && args.size() >= 6) {
    const std::string how = args.size() > 6 ? Lower(args[6]) : std::string("l");
    PortPointer::ScriptDrag(x, y, static_cast< float >(std::atof(std::string(args[4]).c_str())),
                            static_cast< float >(std::atof(std::string(args[5]).c_str())),
                            how == "r" ? PortPointer::kButtonRight : PortPointer::kButtonLeft,
                            how == "touch");
  } else if (action == "wheel" && args.size() >= 5) {
    PortPointer::ScriptMove(x, y);
    PortPointer::ScriptWheel(static_cast< float >(std::atof(std::string(args[4]).c_str())));
  } else if (action == "pinch" && args.size() >= 5) {
    PortPointer::ScriptPinch(x, y, static_cast< float >(std::atof(std::string(args[4]).c_str())));
  } else {
    Fail(kUsage);
  }
}

// boost: start a boost-ball charge release on the morphed player (what releasing a held A does), so
// lanes reach boost-hit arms without timing pad input.
void CmdBoost(CStateManager& mgr, const Args&, std::string& out) {
  CPlayer* player = mgr.GetPlayer(0);
  if (player == nullptr || player->GetMorphBall() == nullptr ||
      player->GetMorphballTransitionState() != CPlayer::kMS_Morphed) {
    Fail("boost: the player is not morphed (press x, or 'press x 1')");
    return;
  }
  player->GetMorphBall()->EnterBoosting(mgr, false);
  out += "boosting\n";
}

// noise: CStateManager::InformListeners at the player (or at x y z) with a listen-noise type, so
// lanes reach a listener's noise arms without the object that makes it (e.g. type 6, the safe-zone
// noise only an "echo" safe zone sends, lures an Ing).
void CmdNoise(CStateManager& mgr, const Args& args, std::string& out) {
  const int type = args.size() < 2 ? -1 : ArgToInt(args[1], -1);
  if (type < 0) {
    Fail("usage: noise <type 0..6> [<x> <y> <z>] (0 fire, 1 bomb, 2 projectile, 3 scream, "
         "4 path, 6 safe zone)");
    return;
  }
  CVector3f pos;
  if (args.size() >= 5) {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    if (!ParseFloat(args[2], x) || !ParseFloat(args[3], y) || !ParseFloat(args[4], z)) {
      Fail("noise: bad position");
      return;
    }
    pos = CVector3f(x, y, z);
  } else {
    const CPlayer* player = mgr.GetPlayer(0);
    if (player == nullptr) {
      Fail("no player");
      return;
    }
    pos = player->GetTranslation();
  }
  mgr.InformListeners(pos, static_cast< EListenNoiseType >(type));
  char line[96];
  std::snprintf(line, sizeof(line), "noise %d at (%.1f, %.1f, %.1f)\n", type, pos.GetX(),
                pos.GetY(), pos.GetZ());
  out += line;
}

// gameevent <ev> [src] [tgt] [value] [as <proxyType>]: the multiplayer game-event dispatch
// (CGMMultiplayer::NotifyListeners) for the ScriptPlayerProxy listeners, which CGMSinglePlayer never
// registers. src/tgt are player indices (0). `as N` retypes each proxy for the call (the shipped
// single-player ones are EchoVisorWatcher, which ignores events). Not 1 (CoinCollector): its arm
// casts the single-player mode to CGMCoin.
void CmdGameEvent(CStateManager& mgr, const Args& args, std::string& out) {
  static const struct {
    const char* name;
    uint event;
  } kEvents[] = {
      {"score", CGMMultiplayer::kGE_Score},     {"kill", CGMMultiplayer::kGE_Kill},
      {"generic", CGMMultiplayer::kGE_Generic}, {"scan", CGMMultiplayer::kGE_Scan},
      {"damage", CGMMultiplayer::kGE_Damage},   {"stop", CGMMultiplayer::kGE_Stop},
      {"spawn", CGMMultiplayer::kGE_Spawn},
  };
  const char* usage = "usage: gameevent <score|kill|generic|scan|damage|stop|spawn> [src] [tgt] "
                      "[value] [as <proxyType 0..10, not 1>]";
  if (args.size() < 2) {
    Fail(usage);
    return;
  }
  uint event = 0;
  for (const auto& e : kEvents) {
    if (args[1] == e.name) {
      event = e.event;
    }
  }
  if (event == 0) {
    Fail(usage);
    return;
  }
  int asType = -1;
  std::vector< std::string_view > pos;
  for (size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "as" && i + 1 < args.size()) {
      asType = ArgToInt(args[++i], -1);
    } else {
      pos.push_back(args[i]);
    }
  }
  if (asType == 1 || asType > 10) {
    Fail(usage);
    return;
  }
  const uint src = pos.size() > 0 ? uint(ArgToInt(pos[0], 0)) : 0;
  const uint tgt = pos.size() > 1 ? uint(ArgToInt(pos[1], 0)) : 0;
  if (tgt >= uint(mgr.GetNumPlayers()) || src >= uint(mgr.GetNumPlayers())) {
    Fail("gameevent: player index out of range");
    return;
  }
  float fvalue = 0.f;
  uint uvalue = 0;
  const void* value = nullptr;
  if (event == CGMMultiplayer::kGE_Damage) {
    if (pos.size() > 2 && !ParseFloat(pos[2], fvalue)) {
      Fail("gameevent: bad damage");
      return;
    }
    value = &fvalue;
  } else if (event == CGMMultiplayer::kGE_Generic) {
    uvalue = pos.size() > 2 ? uint(ArgToInt(pos[2], 0)) : 0;
    value = &uvalue;
  }
  const int reached = CScriptPlayerProxy::PortDeliverGameEvent(mgr, src, tgt, event, value, asType);
  char line[96];
  std::snprintf(line, sizeof(line), "gameevent %.*s %u->%u: %d proxies\n", int(args[1].size()),
                args[1].data(), src, tgt, reached);
  out += line;
}

void CmdSpringBall(CStateManager&, const Args& args, std::string& out) {
  const bool enabled = args.size() < 2 ? !PortOverlay::SpringBall() : ArgToInt(args[1], 1) != 0;
  PortOverlay::SetSpringBall(enabled);
  out += enabled ? "spring ball on\n" : "spring ball off\n";
}

void CmdLockOn(CStateManager&, const Args& args, std::string& out) {
  const bool enabled = args.size() < 2 ? !PortOverlay::LockOnToggle() : ArgToInt(args[1], 1) != 0;
  PortOverlay::SetLockOnToggle(enabled);
  out += enabled ? "toggle lock-on on\n" : "toggle lock-on off\n";
}

void CmdSticky(CStateManager&, const Args& args, std::string& out) {
  const bool enabled = args.size() < 2 ? !PortOverlay::StickyCharge() : ArgToInt(args[1], 1) != 0;
  PortOverlay::SetStickyCharge(enabled);
  out += enabled ? "sticky charge on\n" : "sticky charge off\n";
}

// minimap: the HUD minimap's tap rect in GUI pixels; maptap: a tap on it (opens the map).
void CmdMinimap(CStateManager&, const Args&, std::string& out) {
  float rect[4];
  char line[128];
  if (PortMinimap::Rect(rect)) {
    std::snprintf(line, sizeof(line), "minimap %.1f %.1f %.1f %.1f enabled=%d\n", rect[0], rect[1],
                  rect[2], rect[3], PortMinimap::Enabled() ? 1 : 0);
  } else {
    std::snprintf(line, sizeof(line), "minimap none enabled=%d\n", PortMinimap::Enabled() ? 1 : 0);
  }
  out += line;
}

void CmdMapTap(CStateManager&, const Args&, std::string& out) {
  PortMinimap::RequestTap();
  out += "maptap queued\n";
}

// pointeraim [on|off|<x> <y>]: MP1's console command. on/off is the F1 Controls row
// (`MP2_POINTER_AIM`); x, y place the pointer (-1..1 of the half view, x right, y down) and hold
// it there, without turning the view, until `pointeraim 0 0` (free aim needs pointer aim on and
// `MP2_MOUSE_AIM=1`). Every form prints the state it ends in.
void CmdPointerAim(CStateManager&, const Args& args, std::string& out) {
  if (args.size() == 2 && (args[1] == "on" || args[1] == "off")) {
    PortOverlay::SetPointerAim(args[1] == "on");
  } else if (args.size() == 3) {
    const float px = static_cast< float >(std::atof(std::string(args[1]).c_str()));
    const float py = static_cast< float >(std::atof(std::string(args[2]).c_str()));
    PortMouse::SetPointerAimPosition(px, py);
    PortMouse::SetPointerAimHold(px != 0.f || py != 0.f);
  } else if (args.size() != 1) {
    Fail("pointeraim [on|off|<x> <y>]   x, y in -1..1 of the half view");
    return;
  }
  float x = 0.f;
  float y = 0.f;
  const bool free = PortMouse::PointerAimPosition(x, y);
  char line[96];
  std::snprintf(line, sizeof(line), "pointeraim %s %s x=%.3f y=%.3f\n",
                PortOverlay::PointerAim() ? "on" : "off", free ? "free" : "centred", x, y);
  out += line;
}

// cardbackup [0|1]: the F1 Saves row (`MP2_CARD_BACKUP`) - copy the card's save file into
// save-backups before every write the game makes to it. Bare, it prints the state it is in.
void CmdCardBackup(CStateManager&, const Args& args, std::string& out) {
  bool enabled = PortOverlay::CardBackup();
  if (args.size() >= 2) {
    if (args.size() > 2 || (args[1] != "0" && args[1] != "1")) {
      Fail("cardbackup <0|1>");
      return;
    }
    enabled = args[1] == "1";
    PortOverlay::SetCardBackup(enabled);
  }
  out += enabled ? "card backup before writes: on (MP2_CARD_BACKUP)\n"
                 : "card backup before writes: off (MP2_CARD_BACKUP)\n";
}

// livesplit <0|1> | addr <host:port> | send <command> | status: the LiveSplit settings (no F1 rows)
// (`MP2_LIVESPLIT`, `MP2_LIVESPLIT_ADDR`) - the LiveSplit Server client.
void CmdLiveSplit(CStateManager&, const Args& args, std::string& out) {
  const std::string action = args.size() > 1 ? std::string(args[1]) : "status";
  if (action == "0" || action == "1") {
    PortLiveSplit::SetEnabled(action == "1");
  } else if (action == "addr" && args.size() == 3) {
    PortLiveSplit::SetAddress(std::string(args[2]));
  } else if (action == "send" && args.size() > 2) {
    std::string line(args[2]);
    for (size_t i = 3; i < args.size(); ++i) {
      line += ' ';
      line.append(args[i]);
    }
    PortLiveSplit::SendRaw(line);
  } else if (action != "status") {
    Fail("livesplit <0|1> | addr <host:port> | send <command> | status");
    return;
  }
  static const char* const kStatusNames[] = {"off", "connecting", "connected", "failed"};
  out += std::string("livesplit ") + (PortLiveSplit::Enabled() ? "on " : "off ") +
         PortLiveSplit::Address() + " " + kStatusNames[PortLiveSplit::Status()] + "\n";
  const std::string error = PortLiveSplit::LastError();
  if (!error.empty()) {
    out += "last error: " + error + "\n";
  }
}

// gallery [mask]: the gallery unlock mask CSlideShow::GetGalleriesUnlocked() returns (0x2B14 in the
// ScriptGui module deactivates the gallery entry on 0; a forced ShowSlideShow with 0 faults in retail
// too). The mask is derived, not stored: bits 0-3 are PercentScans >= 40/60/80/100, bit 4 is
// NormalModeCompleted, bit 5 HardModeCompleted. A set writes those three variables, so only a mask
// whose low four bits are contiguous (0, 1, 3, 7, 15 plus 16/32) is representable.
void CmdGallery(CStateManager&, const Args& args, std::string& out) {
  if (gpGameState == nullptr) {
    Fail("no game state");
    return;
  }
  if (args.size() > 2) {
    Fail("gallery [mask]");
    return;
  }
  if (args.size() == 2) {
    char* end = nullptr;
    const std::string text(args[1]);
    const unsigned long mask = std::strtoul(text.c_str(), &end, 0);
    if (end == text.c_str() || *end != '\0' || mask > 63) {
      Fail("gallery [mask]: mask is 0..63");
      return;
    }
    const unsigned low = mask & 15;
    if (low != 0 && (low & (low + 1)) != 0) {
      Fail("gallery: bits 0-3 must be contiguous from bit 0 (0, 1, 3, 7, 15)");
      return;
    }
    static const int kPercent[] = {0, 40, 60, 80, 100};
    int count = 0;
    while ((low >> count) & 1) {
      ++count;
    }
    CGameStateEnvVarManager& vars = gpGameState->SystemOptions().EnvVars();
    CEnvironmentVariable* percent = vars.FindEnvironmentVariable("PercentScans");
    CEnvironmentVariable* normal = vars.FindEnvironmentVariable("NormalModeCompleted");
    CEnvironmentVariable* hard = vars.FindEnvironmentVariable("HardModeCompleted");
    if (percent == nullptr || normal == nullptr || hard == nullptr) {
      Fail("gallery: PercentScans/NormalModeCompleted/HardModeCompleted env var missing");
      return;
    }
    percent->Set(kPercent[count]);
    normal->Set((mask & 16) != 0 ? 1 : 0);
    hard->Set((mask & 32) != 0 ? 1 : 0);
  }
  char line[64];
  std::snprintf(line, sizeof(line), "gallery mask %u\n", CSlideShow::GetGalleriesUnlocked());
  out += line;
}

// logfile [0|1]: the F1 Testing row (`MP2_LOG_FILE`) - everything the process prints also goes
// to metroid_prime2_port.log in the user folder. PortLogFile::Start reads the setting before
// Aurora comes up, so a change here is what the next launch will do, not this one.
void CmdLogFile(CStateManager&, const Args& args, std::string& out) {
  bool enabled = PortOverlay::LogFile();
  if (args.size() >= 2) {
    if (args.size() > 2 || (args[1] != "0" && args[1] != "1")) {
      Fail("logfile <0|1>");
      return;
    }
    enabled = args[1] == "1";
    PortOverlay::SetLogFile(enabled);
  }
  const std::string path = PortLogFile::Path();
  out += enabled ? "log file: on (" : "log file: off (";
  out += path.empty() ? std::string("no file") : path;
  out += ")\n(takes effect next launch)\n";
}

// settings | settings <key> <value> | settings self-test: settings.cfg's entries (an MP2_<KEY>
// environment variable that overrides one is named); with a key and a value, sets it and saves
// settings.cfg; self-test writes every key, reads it back, and reports any that does not.
void CmdSettings(CStateManager&, const Args& args, std::string& out) {
  if (args.size() == 1) {
    for (const auto& [key, value] : PortOverlaySettings::SettingsValues(PortOverlaySettings::SettingsText())) {
      out += key + "=" + value;
      const std::string env = PortOverlaySettings::EnvName(key);
      if (const char* set = port::EnvString(env.c_str())) {
        out += "  (env " + env + "=" + set + " set; wins if the key reads it)";
      }
      out += "\n";
    }
    return;
  }
  if (args.size() == 2 && args[1] == "self-test") {
    // The test leaves the live variables at its samples: put back what was there, unmarked.
    const std::string before = PortOverlaySettings::SettingsText();
    PortOverlaySettings::SettingsSelfTest(out);
    std::istringstream restore(before);
    PortOverlaySettings::ApplySettingsText(restore);
    return;
  }
  if (args.size() != 3) {
    Fail("usage: settings [<key> <value> | self-test]");
    return;
  }
  if (!PortOverlaySettings::ApplySetting(std::string(args[1]), std::string(args[2]))) {
    Fail(("unknown setting " + std::string(args[1])).c_str());
    return;
  }
  PortOverlaySettings::MarkDirty();
  PortOverlaySettings::SaveSettings();
  const auto values = PortOverlaySettings::SettingsValues(PortOverlaySettings::SettingsText());
  // A key at its default writes nothing (suit colours all "-"): no entry, not an error.
  const auto value = values.find(std::string(args[1]));
  out += std::string(args[1]) + "=" + (value != values.end() ? value->second : std::string("(default)")) + "\n";
}

void CmdGunOffset(CStateManager&, const Args& args, std::string& out) {
  float offset[3];
  PortOverlay::GunOffset(offset);
  if (args.size() >= 4) {
    float parsed[3];
    for (int i = 0; i < 3; ++i) {
      if (!ParseFloat(args[i + 1], parsed[i])) {
        Fail("gunoffset takes three numbers: right forward up (metres, +-1)");
        return;
      }
      if (parsed[i] < -PortOverlay::kGunOffsetMax || parsed[i] > PortOverlay::kGunOffsetMax) {
        Fail("each offset is clamped to +-1 m");
        return;
      }
    }
    PortOverlay::SetGunOffset(parsed);
    PortOverlay::GunOffset(offset);
  } else if (args.size() != 1) {
    Fail("usage: gunoffset [<right> <forward> <up>]");
    return;
  }
  char line[128];
  std::snprintf(line, sizeof(line), "gun offset %.2f %.2f %.2f m\n", offset[0], offset[1],
                offset[2]);
  out += line;
}

void CmdMsaaAniso(CStateManager&, const Args& args, std::string& out) {
  if (args.size() < 2) {
    char line[128];
    std::snprintf(line, sizeof(line), "msaa %dx, anisotropy %dx\n", PortOverlay::Msaa(),
                  PortOverlay::Anisotropy());
    out += line;
    return;
  }
  // The command's own name is the setting (`msaa <n>`), the argument is the value.
  const bool isMsaa = args[0] == "msaa";
  const bool isAniso = args[0] == "aniso";
  if (!isMsaa && !isAniso) {
    Fail("usage: msaa <1|4> | aniso <1..16>");
    return;
  }
  const int value = ArgToInt(args[1], -1);
  if (isMsaa) {
    if (value != 1 && value != 4) {
      Fail("MSAA is 1 (off) or 4");
      return;
    }
    PortOverlay::SetMsaa(value);
  } else {
    if (value < 1 || value > 16) {
      Fail("anisotropy is 1..16");
      return;
    }
    PortOverlay::SetAnisotropy(value);
  }
  // PortOverlay::Draw hands it to aurora_set_graphics_quality, which rebuilds the framebuffers at the
  // next frame start.
  char line[128];
  std::snprintf(line, sizeof(line), "msaa %dx, anisotropy %dx (applied next frame)\n",
                PortOverlay::Msaa(), PortOverlay::Anisotropy());
  out += line;
}

void CmdScanSpread(CStateManager&, const Args& args, std::string& out) {
  if (args.size() >= 2) {
    const int value = ArgToInt(args[1], -1);
    if (value < PortOverlay::kScanSpreadMin || value > PortOverlay::kScanSpreadMax) {
      Fail("scan spread is 0..100 percent (100 = fully spread)");
      return;
    }
    PortOverlay::SetScanSpread(value);
  }
  char line[64];
  std::snprintf(line, sizeof(line), "scan spread %d%%\n", PortOverlay::ScanSpread());
  out += line;
}

void CmdHudScale(CStateManager&, const Args& args, std::string& out) {
  if (args.size() < 2) {
    char line[64];
    std::snprintf(line, sizeof(line), "hud scale %d%%\n", PortOverlay::HudScale());
    out += line;
    return;
  }
  const int value = ArgToInt(args[1], -1);
  if (value < PortOverlay::kHudScaleMin || value > PortOverlay::kHudScaleMax) {
    Fail("hud scale is 50..100 percent (100 = the game's own layout)");
    return;
  }
  PortOverlay::SetHudScale(value);
  char line[64];
  std::snprintf(line, sizeof(line), "hud scale %d%%\n", PortOverlay::HudScale());
  out += line;
}

// hudopacity <echo|dark|scan> <visor|0..100>: that visor's HUD opacity, or the game's Visor Opacity.
void CmdHudOpacity(CStateManager&, const Args& args, std::string& out) {
  static const char* const kParts[PortOverlay::kHudOpacityPartCount] = {"echo", "dark", "scan"};
  int part = -1;
  for (int i = 0; i < PortOverlay::kHudOpacityPartCount; ++i) {
    if (args.size() > 1 && args[1] == kParts[i]) {
      part = i;
    }
  }
  const std::string_view value = args.size() > 2 ? args[2] : std::string_view();
  const int percent = value == "visor" ? PortOverlay::kHudOpacityVisor : ArgToInt(value, -2);
  if (part < 0 || value.empty() ||
      (percent != PortOverlay::kHudOpacityVisor &&
       (percent < PortOverlay::kHudOpacityMin || percent > PortOverlay::kHudOpacityMax))) {
    Fail("usage: hudopacity <echo|dark|scan> <visor|0..100>");
    return;
  }
  PortOverlay::SetHudOpacity(static_cast< PortOverlay::HudOpacityPart >(part), percent);
  char line[64];
  std::snprintf(line, sizeof(line), "hud opacity %s %d\n", kParts[part], percent);
  out += line;
}

// crosshair [<25..100> | style classic|remastered|gamecube | opacity <0..100> | color <r> <g> <b> | color
// clear]: the Prime port's crosshair settings, same meanings.
void CmdCrosshair(CStateManager&, const Args& args, std::string& out) {
  if (args.size() >= 2) {
    const std::string_view what = args[1];
    if (what == "style" && args.size() >= 3) {
      const std::string_view style = args[2];
      if (style != "classic" && style != "remastered" && style != "gamecube") {
        Fail("crosshair style is classic, remastered or gamecube");
        return;
      }
      PortOverlay::SetCrosshairStyle(style == "remastered"
                                         ? PortOverlay::kCrosshairStyle_Remastered
                                     : style == "gamecube" ? PortOverlay::kCrosshairStyle_GameCube
                                                           : PortOverlay::kCrosshairStyle_Classic);
    } else if (what == "opacity" && args.size() >= 3) {
      const int percent = ArgToInt(args[2], -1);
      if (percent < PortOverlay::kCrosshairOpacityMin ||
          percent > PortOverlay::kCrosshairOpacityMax) {
        Fail("crosshair opacity is 0..100 percent");
        return;
      }
      PortOverlay::SetCrosshairOpacity(percent);
    } else if (what == "color") {
      if (args.size() >= 5) {
        const float r = static_cast< float >(std::atof(std::string(args[2]).c_str()));
        const float g = static_cast< float >(std::atof(std::string(args[3]).c_str()));
        const float b = static_cast< float >(std::atof(std::string(args[4]).c_str()));
        PortOverlay::SetCrosshairColor(r, g, b);
      } else {
        PortOverlay::ClearCrosshairColor();
      }
    } else if (what == "aim" && args.size() >= 3) {
      PortOverlay::SetAimCrosshair(args[2] != "0");
    } else {
      const int percent = ArgToInt(args[1], -1);
      if (percent < PortOverlay::kCrosshairSizeMin || percent > PortOverlay::kCrosshairSizeMax) {
        Fail("usage: crosshair [<25..100> | style classic|remastered|gamecube | opacity <0..100> | color "
             "<r> <g> <b> | color clear | aim <0|1>]");
        return;
      }
      PortOverlay::SetCrosshairSize(percent);
    }
  }
  char line[160];
  std::snprintf(line, sizeof(line), "crosshair %s, %d%%, %d%% opacity, aim %s\n",
                PortOverlay::CrosshairStyle() == PortOverlay::kCrosshairStyle_Remastered ? "remastered"
                : PortOverlay::CrosshairStyle() == PortOverlay::kCrosshairStyle_GameCube ? "gamecube"
                                                                                         : "classic",
                PortOverlay::CrosshairSize(), PortOverlay::CrosshairOpacity(),
                PortOverlay::AimCrosshair() ? "on" : "off");
  out += line;
}

void CmdUiScale(CStateManager&, const Args& args, std::string& out) {
  if (args.size() >= 2) {
    const int value = ArgToInt(args[1], -1);
    if (value < PortOverlay::kUiScaleMin || value > PortOverlay::kUiScaleMax) {
      Fail("ui scale is 50..200 percent");
      return;
    }
    PortOverlay::SetUiScale(value);
  }
  char line[64];
  std::snprintf(line, sizeof(line), "ui scale %d%%\n", PortOverlay::UiScale());
  out += line;
}

void CmdVsync(CStateManager&, const Args& args, std::string& out) {
  if (args.size() >= 2) {
    const int value = ArgToInt(args[1], -1);
    if (value != 0 && value != 1) {
      Fail("vsync is 0 or 1");
      return;
    }
    PortOverlay::SetVsync(value == 1);
  }
  out += PortOverlay::Vsync() ? "vsync on\n" : "vsync off\n";
}

void CmdInterp(CStateManager&, const Args& args, std::string& out) {
  if (args.size() >= 2) {
    const int value = ArgToInt(args[1], -1);
    if (value != 0 && value != 1) {
      Fail("interp is 0 or 1");
      return;
    }
    PortOverlay::SetSmoothFrames(value == 1);
  }
  out += PortOverlay::SmoothFrames() ? "smooth frames on" : "smooth frames off";
  out += PortOverlay::FrameLimit() ? " (inactive: 60 FPS cap on)\n" : "\n";
}

// MP1's CmdPresent: the presentation factor the camera and actors are drawn with between ticks,
// forced even with the cap on. No argument prints it.
void CmdPresent(CStateManager&, const Args& args, std::string& out) {
  const std::string arg = args.size() > 1 ? Lower(args[1]) : std::string();
  float t = 0.f;
  if (arg == "off") {
    PortOverlay::SetPresentOverride(-1.f);
  } else if (arg == "cycle") {
    PortOverlay::SetPresentOverride(PortOverlay::kPresentCycle);
  } else if (arg == "tick") {
    PortOverlay::SetPresentOverride(PortOverlay::kPresentTick);
  } else if (args.size() > 1 && !(ParseFloat(arg, t) && t >= 0.f && t <= 1.f)) {
    Fail("usage: present <0..1|cycle|tick|off>");
    return;
  } else if (args.size() > 1) {
    PortOverlay::SetPresentOverride(t);
  }
  const float value = PortOverlay::PresentOverrideValue();
  char line[32];
  if (value < 0.f) {
    std::snprintf(line, sizeof(line), "present off\n");
  } else if (value == PortOverlay::kPresentTick) {
    std::snprintf(line, sizeof(line), "present tick\n");
  } else if (value > 1.f) {
    std::snprintf(line, sizeof(line), "present cycle\n");
  } else {
    std::snprintf(line, sizeof(line), "present %.3f\n", value);
  }
  out += line;
}

// MP1's CmdHold: stop the simulation; `step` then runs ticks one per drawn frame.
void CmdHold(CStateManager&, const Args& args, std::string& out) {
  const std::string arg = args.size() > 1 ? Lower(args[1]) : std::string();
  if (args.size() > 1 && arg != "0" && arg != "1") {
    Fail("usage: hold <0|1>");
    return;
  }
  if (args.size() > 1) {
    PortOverlay::SetTickHold(arg == "1");
  }
  out += PortOverlay::TickHold() ? "hold 1, step runs ticks\n" : "hold 0, the simulation runs\n";
}

void CmdStep(CStateManager&, const Args& args, std::string& out) {
  if (!PortOverlay::TickHold()) {
    Fail("not held (hold 1 first)");
    return;
  }
  const int count = args.size() > 1 ? ArgToInt(args[1], -1) : 1;
  if (count < 1 || count > 600) {
    Fail("usage: step [1..600]");
    return;
  }
  PortOverlay::StepTicks(static_cast< unsigned >(count));
  out += "stepping " + std::to_string(count) + (count == 1 ? " tick\n" : " ticks\n");
}

// The Prime 1 port's CmdAspect; applied by PortOverlay::ApplyAspect on the next loop iteration.
void CmdAspect(CStateManager&, const Args& args, std::string& out) {
  if (args.size() >= 2) {
    std::string name(args[1]);
    for (char& c : name) {
      c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
    }
    int mode = 0;
    if (!PortOverlay::ParseAspect(name.c_str(), mode)) {
      Fail("usage: aspect <4:3|16:9|window>");
      return;
    }
    PortOverlay::SetAspectMode(mode);
  }
  out += "aspect ";
  out += PortOverlay::AspectName(PortOverlay::AspectMode());
  out += '\n';
}

void CmdHudSpread(CStateManager&, const Args& args, std::string& out) {
  if (args.size() >= 2) {
    int mode = 0;
    if (!PortOverlay::ParseHudSpreadMax(std::string(args[1]).c_str(), mode)) {
      Fail("usage: hudspread <16:9|21:9|full>");
      return;
    }
    PortOverlay::SetHudSpreadMax(mode);
  }
  out += "hudspread ";
  out += PortOverlay::HudSpreadMaxName(PortOverlay::HudSpreadMax());
  out += '\n';
}

// One object as `objs` and `obj` show it: uid, editor id, class, area; an actor's position too when
// `withPosition`. `inactive` when it is not active.
void DescribeObject(const CEntity& ent, bool withPosition, std::string& out) {
  char line[320];
  std::snprintf(line, sizeof(line), "u%04x %08x  %-26s a%02d", ent.GetUniqueId().value,
                ent.GetEditorId().Value(), ClassName(ent).c_str(), ent.GetCurrentAreaId().Value());
  out += line;
  const CActor* actor = withPosition ? TCastToConstPtr< CActor >(&ent) : nullptr;
  if (actor != nullptr) {
    const CVector3f p = actor->GetTranslation();
    std::snprintf(line, sizeof(line), " (%.1f, %.1f, %.1f)", p.GetX(), p.GetY(), p.GetZ());
    out += line;
  }
  if (!ent.GetActive()) {
    out += "  inactive";
  }
  out += '\n';
}

// The object an id names: the raw TUniqueId first, then - because the port's own log lines print
// TUniqueId::Value(), which drops the version bits ("[door] 006e got kSM_Open") - the one object
// whose Value() matches, when exactly one does. `obj` and `send` both take their target from here.
const CEntity* ResolveObject(CStateManager& mgr, std::string_view text) {
  // e<hex>: the object with that editor id (as `objs` prints it: e00350060). Unique ids are
  // reused as objects come and go, so a console script that runs across a layer load names the
  // script object instead.
  if (text.size() > 1 && (text[0] == 'e' || text[0] == 'E')) {
    const std::string hex(text.substr(1));
    char* end = nullptr;
    const unsigned long editorId = std::strtoul(hex.c_str(), &end, 16);
    if (end != nullptr && *end == '\0') {
      const CObjectList& list = mgr.GetObjectListById(kOL_All);
      // An area's SCLY writes an id with its layer in bits 26..31 (e2824022f = layer 10), but the
      // object stores the id without them (TEditorId::Value() is value & 0x3ffffff: e0024022f), so
      // a command carrying the SCLY form would never match. The exact id wins; a layer-masked
      // match is the fallback, and with layer bits of zero the two are the same comparison.
      const uint wantId = static_cast< uint >(editorId) & 0x3ffffff;
      const CEntity* masked = nullptr;
      for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
        const CEntity* ent = list[i];
        if (ent == nullptr) {
          continue;
        }
        if (ent->GetEditorId().value == editorId) {
          return ent;
        }
        if (masked == nullptr && ent->GetEditorId().Value() == wantId) {
          masked = ent;
        }
      }
      if (masked != nullptr) {
        return masked;
      }
      char why[48];
      std::snprintf(why, sizeof(why), "no object with editor id %08lx", editorId);
      Fail(why);
      return nullptr;
    }
  }
  TUniqueId uid = kInvalidUniqueId;
  if (!ParseUid(text, uid)) {
    Fail("not an object id '" + Str(text) + "'");
    return nullptr;
  }
  if (const CEntity* exact = mgr.GetObjectById(uid)) {
    return exact;
  }
  const CObjectList& list = mgr.GetObjectListById(kOL_All);
  const CEntity* found = nullptr;
  int matches = 0;
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CEntity* ent = list[i];
    if (ent != nullptr && ent->GetUniqueId().Value() == uid.Value()) {
      found = ent;
      ++matches;
    }
  }
  if (matches == 1) {
    return found;
  }
  char why[48];
  std::snprintf(why, sizeof(why), matches == 0 ? "no object u%04x" : "ambiguous id u%04x",
                uid.value);
  Fail(why);
  return nullptr;
}

void CmdObjs(CStateManager& mgr, const Args& args, std::string& out) {
  const std::string filter = args.size() < 2 ? std::string() : Lower(args[1]);
  const CObjectList& list = mgr.GetObjectListById(kOL_All);
  int count = 0;
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CEntity* ent = list[i];
    if (ent == nullptr) {
      continue;
    }
    if (!filter.empty() && Lower(ClassName(*ent)).find(filter) == std::string::npos) {
      continue;
    }
    DescribeObject(*ent, true, out);
    ++count;
  }
  char line[64];
  std::snprintf(line, sizeof(line), "%d objects\n", count);
  out += line;
}

}  // namespace
}  // namespace PortConsole

// `obj <door>`: the door and lock state machines, which is what a door that will not open is stuck
// in (a lock that never finishes unlocking, an open request count that never rose).
struct PortConsoleDoor {
  static void Describe(const CScriptDoor& door, CStateManager& mgr, std::string& out) {
    static const char* const kDoor[] = {"closed", "waiting-for-area", "opening",
                                        "open",   "close-delay",      "closing"};
    static const char* const kLock[] = {"unlocked", "pending", "locking", "locked", "unlocking"};
    const CActor* lock = TCastToConstPtr< CActor >(mgr.GetObjectById(door.mLockActorId));
    char line[224];
    std::snprintf(line, sizeof(line),
                  "  door %s, lock %s (timer %.2f, lock actor u%04x%s), open requests %d%s\n",
                  door.mDoorState <= CScriptDoor::kDS_Closing ? kDoor[door.mDoorState] : "?",
                  door.mLockState <= CScriptDoor::kLS_Unlocking ? kLock[door.mLockState] : "?",
                  door.mLockTimer, door.mLockActorId.Value(),
                  lock == nullptr                          ? " missing"
                  : lock->GetModelData() == nullptr        ? ""
                  : lock->GetModelData()->IsAnimating()    ? " animating"
                                                           : " still",
                  door.mOpenRequestCount, door.mResetPending ? ", reset pending" : "");
    out += line;
    // Which of the four beams the door's own vulnerability takes, and what the shots see (the
    // Unlock beam doors cheat swaps a colour lock for any beam).
    static const char kBeam[] = "PDLA";
    char base[5] = "----", seen[5] = "----";
    const CDamageVulnerability* shot = door.GetDamageVulnerability();
    for (int type = kWT_Power; type <= kWT_Annihilator; ++type) {
      const CWeaponMode mode(static_cast< EWeaponType >(type));
      if (door.mCurrentVulnerability.WeaponHits(mode, 0)) base[type] = kBeam[type];
      if (shot->WeaponHits(mode, 0)) seen[type] = kBeam[type];
    }
    std::snprintf(line, sizeof(line), "  beams %s%s, shots see %s\n", base,
                  door.mCurrentVulnerability.PortIsColorLock() ? " (colour lock)" : "", seen);
    out += line;
  }
};

namespace PortConsole {
namespace {

// scaninfo <id>: the 'SCAN' asset an actor answers GetScannableObjectInfo with (what the scan
// visor would show), or none; a module's override runs without scanning it.
// `orbit [id]`: why the lock-on takes or skips an actor: the player's orbit state and targets,
// and for the actor its orbit position, ValidateOrbitTargetId's verdict (0 OK, 1 extreme angle,
// 2 not ready / visor, 3 through door, 4 invalid, ...), screen position (-1..1) and distance.
void CmdOrbit(CStateManager& mgr, const Args& args, std::string& out) {
  CPlayer* player = mgr.GetPlayer(0);
  if (player == nullptr) {
    Fail("orbit: no player");
    return;
  }
  char line[256];
  std::snprintf(line, sizeof(line), "  orbit state %d target %04x next %04x max dist %.1f\n",
                static_cast< int >(player->GetOrbitState()), player->GetOrbitTargetId().Value(),
                player->GetOrbitNextTargetId().Value(), player->GetOrbitMaxTargetDistance());
  out += line;
  if (args.size() < 2) {
    return;
  }
  const CActor* act = TCastToConstPtr< CActor >(ResolveObject(mgr, args[1]));
  if (act == nullptr) {
    Fail("orbit: not an actor");
    return;
  }
  const CVector3f pos = act->GetOrbitPosition(mgr);
  const CVector3f eye = player->GetEyePosition();
  const CVector3f scr =
      player->GetCameraManager()->GetFirstPersonCamera()->ConvertToScreenSpace(pos);
  std::snprintf(line, sizeof(line),
                "  %04x orbit pos (%.1f, %.1f, %.1f) dist %.1f screen (%.2f, %.2f, %.2f) "
                "validate %d visorflags %d disttest %d\n",
                act->GetUniqueId().Value(), pos.GetX(), pos.GetY(), pos.GetZ(),
                (pos - eye).Magnitude(), scr.GetX(), scr.GetY(), scr.GetZ(),
                player->ValidateOrbitTargetId(act->GetUniqueId(), mgr),
                static_cast< int >(act->GetTargetableVisorFlags()),
                act->GetDoTargetDistanceTest() ? 1 : 0);
  out += line;
}

void CmdScanInfo(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() != 2) {
    Fail("usage: scaninfo <id>");
    return;
  }
  const CEntity* ent = ResolveObject(mgr, args[1]);
  const CActor* actor = ent != nullptr ? TCastToConstPtr< CActor >(ent) : nullptr;
  if (actor == nullptr) {
    Fail("not an actor");
    return;
  }
  const CScannableObjectInfo* info = actor->GetScannableObjectInfo();
  char buf[64];
  if (info != nullptr) {
    snprintf(buf, sizeof(buf), "scaninfo %08X\n", static_cast< unsigned >(info->GetScannableObjectId()));
  } else {
    snprintf(buf, sizeof(buf), "scaninfo none\n");
  }
  out += buf;
}

void CmdObj(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() < 2) {
    Fail("usage: obj <id>");
    return;
  }
  const CEntity* ent = ResolveObject(mgr, args[1]);
  if (ent == nullptr) {
    return;
  }
  DescribeObject(*ent, false, out);
  const CActor* actor = TCastToConstPtr< CActor >(ent);
  const CHealthInfo* health = actor != nullptr ? actor->GetHealthInfo() : nullptr;
  if (health != nullptr) {
    char line[64];
    std::snprintf(line, sizeof(line), "  hp %.0f/%.0f\n", health->GetHP(),
                  health->GetInitialHP());
    out += line;
  }
  // A patterned's FSM state, which is what `hurt` is usually driven against. Upstream has only the
  // non-const accessor (CPatterned::StateMachineState), and it static_casts to TStateMachineState
  // even when the object holds a CGenericFSM2State (GetType() 1) - a different member layout, so
  // reading TStateMachineState's own mState through that cast reads a vector's guts as a CState*.
  // The base subobject sits at the same address under both, so take it as the base interface and
  // make only virtual calls; the state's own data is touched only on the AFSM type.
  if (const CScriptDoor* door = TCastToConstPtr< CScriptDoor >(ent)) {
    ::PortConsoleDoor::Describe(*door, mgr, out);
  }
  // A water's volume (DescribeObject prints no position for it: the cast to CActor fails on the
  // trigger types), so a driver can put an actor in it: bounds <min x y z> <max x y z>.
  if (const CScriptWater* water = TCastToConstPtr< CScriptWater >(ent)) {
    const CAABox box = water->GetTriggerBoundsWR();
    char line[160];
    std::snprintf(line, sizeof(line), "  bounds %.2f %.2f %.2f %.2f %.2f %.2f\n",
                  box.GetMinPoint().GetX(), box.GetMinPoint().GetY(), box.GetMinPoint().GetZ(),
                  box.GetMaxPoint().GetX(), box.GetMaxPoint().GetY(), box.GetMaxPoint().GetZ());
    out += line;
  }
  if (const CSwarmBasics* swarm = TCastToConstPtr< CSwarmBasics >(ent)) {
    char line[768];
    swarm->PortDescribe(line, sizeof(line));
    out += line;
  }
  const CPatterned* patterned = TCastToConstPtr< CPatterned >(ent);
  if (patterned != nullptr) {
    TStateMachineStateBase< CPatterned >* machine =
        &const_cast< CPatterned* >(patterned)->StateMachineState();
    const char* name = machine->HasState() ? machine->GetName() : nullptr;
    if (name != nullptr && name[0] != '\0') {
      out += "  fsm ";
      out += name;
      out += '\n';
    } else if (!machine->HasState()) {
      out += "  fsm (none)\n";
    } else if (machine->GetType() == 0) {
      const CState* state =
          static_cast< TStateMachineState< CPatterned >* >(machine)->GetCurrentState();
      char line[32];
      std::snprintf(line, sizeof(line), "  fsm #%d\n", state != nullptr ? state->GetIndex() : -1);
      out += line;
    } else {
      out += "  fsm (unnamed)\n";
    }
    // The body controller's state (pas::EAnimationState, the `bodystate` index).
    if (const CBodyController* body = patterned->GetBodyController()) {
      char line[48];
      std::snprintf(line, sizeof(line), "  body state %d\n", int(body->GetCurrentStateId()));
      out += line;
    }
  }
  for (const SConnection& conn : ent->GetConnectionList()) {
    const CEntity* target = mgr.GetObjectById(mgr.GetIdForScript(conn.objId));
    char line[256];
    std::snprintf(line, sizeof(line), "  on %s send %s -> %08x %s\n",
                  NameOrHex(kStateNames, kStateCount, static_cast< uint >(conn.state)).c_str(),
                  NameOrHex(kMessageNames, kMessageCount, static_cast< uint >(conn.msg)).c_str(),
                  conn.objId.Value(),
                  target != nullptr ? ClassName(*target).c_str() : "(not loaded)");
    out += line;
  }
}

// `damage <id> <amount> [knockback] [weapon]`: an immediate player-owned impact through
// ApplyDamage. Weapon messages need a weapon sender (Shredder fn_68_1EA0, 0x1FFC..0x203C),
// and CPatterned's hit flag needs a CGameProjectile. Body attacks still come from the player.
void CmdDamage(CStateManager& mgr, const Args& args, std::string& out) {
  float amount = 0.f;
  float knockback = 0.f;
  if (args.size() < 3 || !ParseFloat(args[2], amount)) {
    Fail("usage: damage <id> <amount> [knockback-power] [power|missile|cannonball|dark|light|"
         "annihilator|powerbomb|bomb|boostball|screwattack|darkburst|sunburst|sonicboom|"
         "supermissile]");
    return;
  }
  // The weapon mode is what a damageable trigger's vulnerability is looked up by, so a wall that
  // only a missile or the cannon ball may break cannot be tested with the default power beam.
  // The charge combos (darkburst, sunburst, sonicboom) are their beam with the combo flag, which is
  // what a knockback rule set tells them apart by (the Ing's death follow-ups, for instance).
  EWeaponType weapon = kWT_Power;
  bool comboed = false;
  for (size_t i = 3; i < args.size(); ++i) {
    float number = 0.f;
    if (ParseFloat(args[i], number)) {
      knockback = number;
      continue;
    }
    if (args[i] == "missile") {
      weapon = kWT_Missile;
    } else if (args[i] == "cannonball") {
      weapon = kWT_CannonBall;
    } else if (args[i] == "dark") {
      weapon = kWT_Dark;
    } else if (args[i] == "light") {
      weapon = kWT_Light;
    } else if (args[i] == "power") {
      weapon = kWT_Power;
    } else if (args[i] == "annihilator") {
      weapon = kWT_Annihilator;
    } else if (args[i] == "powerbomb") {
      weapon = kWT_PowerBomb;
    } else if (args[i] == "bomb") {
      weapon = kWT_Bomb;
    } else if (args[i] == "boostball") {
      weapon = kWT_BoostBall;
    } else if (args[i] == "screwattack") {
      weapon = kWT_ScrewAttack;
    } else if (args[i] == "darkburst") {
      weapon = kWT_Dark;
      comboed = true;
    } else if (args[i] == "sunburst") {
      weapon = kWT_Light;
      comboed = true;
    } else if (args[i] == "sonicboom") {
      weapon = kWT_Annihilator;
      comboed = true;
    } else if (args[i] == "supermissile") {
      weapon = kWT_Power;
      comboed = true;
    } else {
      Fail("damage: weapon is power, missile, cannonball, dark, light, annihilator, powerbomb, "
           "bomb, boostball, screwattack, darkburst, sunburst, sonicboom or supermissile");
      return;
    }
  }
  CEntity* ent = const_cast< CEntity* >(ResolveObject(mgr, args[1]));
  CActor* actor = ent != nullptr ? TCastToPtr< CActor >(ent) : nullptr;
  CPlayer* player = mgr.GetPlayer(0);
  if (actor == nullptr || actor->GetHealthInfo() == nullptr || player == nullptr) {
    Fail("not an actor with health (or no player)");
    return;
  }
  const TUniqueId targetId = actor->GetUniqueId();
  const TUniqueId ownerId = player->GetUniqueId();
  const float before = actor->GetHealthInfo()->GetHP();
  const CDamageInfo info(CWeaponMode(weapon, false, comboed), amount, 0.f, knockback);
  const CVector3f target = actor->GetAimPosition(mgr, 0.f);
  CVector3f direction = target - player->GetAimPosition(mgr, 0.f);
  direction = direction.CanBeNormalized() ? direction.AsNormalized() : CVector3f(0.f, 1.f, 0.f);
  TUniqueId sourceId = ownerId;
  if (weapon <= kWT_Missile) {
    sourceId = mgr.AllocateUniqueId();
    if (sourceId == kInvalidUniqueId) {
      Fail("damage: no free object id for the impact");
      return;
    }
    // The source is at the impact, facing into the target, as directional vulnerabilities expect.
    const CTransform4f xf = CTransform4f::LookAt(target - direction * 0.01f, target,
                                                 CVector3f::Up());
    CWeapon* impact = nullptr;
    if (weapon == kWT_Bomb || weapon == kWT_PowerBomb) {
      impact = rs_new CWeapon(sourceId, actor->GetCurrentAreaId(), false, ownerId, weapon,
                              rstl::string_l("Console damage"), xf,
                              CMaterialFilter::GetPassEverything(), CMaterialList(), info,
                              weapon == kWT_Bomb ? CWeapon::kPA_Bombs : CWeapon::kPA_PowerBombs,
                              CModelData());
    } else {
      // An effect-free projectile needs no loaded gun/resource and cannot hit anything a second
      // time. Its real type/owner/damage remain available during Damage/ResistedDamage dispatch.
      TToken< CWeaponDescription > description(rs_new CWeaponDescription());
      description->mHOMG = false;
      impact = rs_new CGameProjectile(
          false, description, rstl::string_l("Console damage"), weapon, xf, kMT_Player, info,
          sourceId, actor->GetCurrentAreaId(), ownerId, targetId,
          comboed ? CWeapon::kPA_ComboShot : CWeapon::kPA_None, false, CVector3f::One(),
          CImpactVisorEffect::None());
    }
    mgr.AddObject(impact);
  }
  // CGameProjectile::ApplyDamageToOneActor (DOL 0x800342B4..0x800342D8) likewise passes the
  // projectile's id as damager and its player's id as owner; neither bypasses vulnerability.
  mgr.ApplyDamage(sourceId, targetId, ownerId, info,
                  CMaterialFilter::GetPassEverything(), direction);
  if (sourceId != ownerId) {
    // FIFO deletion follows the queued damage message, so its receiver can still resolve sourceId.
    mgr.DeleteObjectRequest(sourceId);
  }
  // The actor may have been deleted by its own death; look it up again.
  const CActor* after = TCastToConstPtr< CActor >(mgr.GetObjectById(targetId));
  char line[96];
  if (after != nullptr && after->GetHealthInfo() != nullptr) {
    std::snprintf(line, sizeof(line), "hp %.1f -> %.1f\n", before, after->GetHealthInfo()->GetHP());
  } else {
    std::snprintf(line, sizeof(line), "hp %.1f -> (gone)\n", before);
  }
  out += line;
}

// frame: the game updates run so far (what MP2_FRAMES counts), so a console script can wait on
// game time rather than wall time (console.py's `@f<n>`), which a fast harness run does not keep.
void CmdFrame(CStateManager&, const Args&, std::string& out) {
  out += "frame " + std::to_string(PortHarness::GameUpdates()) + '\n';
}

// at <update> <command>: hold the command until the game update count reaches <update>; its reply
// arrives then. One command per `at` (a `;` ends it).
void CmdAt(CStateManager&, const Args& args, std::string& out) {
  const int update = args.size() < 3 ? -1 : ArgToInt(args[1], -1);
  if (update < 0) {
    Fail("usage: at <update> <command>");
    return;
  }
  const char* begin = args[2].data();
  const std::string line(begin, args.back().data() + args.back().size() - begin);
  sDeferred.push_back({static_cast< unsigned >(update), line, sExecutingGeneration});
  out += "queued for update " + std::to_string(update) + " (now " + std::to_string(PortHarness::GameUpdates()) +
         "): " + line + '\n';
}

// The state names a `fsm` command can use, for the "unknown state" message: the machine's own
// states and, for an FSM2, the states of its sub-machines.  A long list is cut.
std::string StateNameList(const rstl::vector< rstl::string >& names) {
  const size_t kMaxNames = 24;
  std::string out;
  size_t shown = 0;
  size_t total = 0;
  for (size_t i = 0; i < names.size(); ++i) {
    // The asset's states and the owner's table hold the same names: list each one once.
    bool seen = false;
    for (size_t j = 0; j < i; ++j) {
      if (names[j] == names[i]) {
        seen = true;
        break;
      }
    }
    if (seen) {
      continue;
    }
    ++total;
    if (shown < kMaxNames) {
      if (shown != 0) {
        out += ", ";
      }
      out += names[i].data();
      ++shown;
    }
  }
  if (total > kMaxNames) {
    out += ", ... (+" + std::to_string(total - kMaxNames) + " more)";
  }
  return out;
}

// Forced states and the actor's animation set. A state body forced with `fsm`/`statecall` can
// command a body state the actor's ANCS has no anims for (a Brizgee's Generate/Deactivate deliver
// kBSC_Generate, its Attack/Retreat kBSC_Jump, Halt kBSC_LoopReaction; its PAS has none of those
// states, and its FSM never enters these state bodies). CPASDatabase::FindBestAnimation then answers anim -1 (0x80298CE8) and
// CAnimData::BuildAnimationTree indexes the animation index list with it unchecked (0x80029BAC
// slwi r0,r0,2; 0x80029BB4 lwzx r30,r7,r0; only animB is compared with -1, 0x80029BA8), so retail
// reads the word in front of the list: undefined behaviour no natural run reaches, a host SIGSEGV in
// CBSGenerate/CBSJump::Start here. Every actor the console forced is checked after each tick's
// commands; the state machine delivers after the body update (CPatterned::Think), so a command waits
// for the next frame and is dropped here first, with the frame's other commands
// (CBodyStateCmdMgr::Reset). Actors the console never forced are not touched.
std::vector< TUniqueId > sForcedActors;
std::vector< std::pair< ushort, int > > sUnplayableReported;

struct UnplayableBodyCmd {
  EBodyStateCmd cmd;
  pas::EAnimationState state;
  const char* name;
};
// The action commands and the body state each enters (CBSLocomotion::GetBodyStateTransition);
// the damage reactions (Hurled, KnockDown, KnockBack, LoopHitReaction) and Cover/WallHang/Scripted
// are left alone.
const UnplayableBodyCmd kUnplayableBodyCmds[] = {
    {kBSC_Slide, pas::kAS_Slide, "Slide"},
    {kBSC_Generate, pas::kAS_Generate, "Generate"},
    {kBSC_MeleeAttack, pas::kAS_MeleeAttack, "MeleeAttack"},
    {kBSC_ProjectileAttack, pas::kAS_ProjectileAttack, "ProjectileAttack"},
    {kBSC_LoopAttack, pas::kAS_LoopAttack, "LoopAttack"},
    {kBSC_LoopReaction, pas::kAS_LoopReaction, "LoopReaction"},
    {kBSC_Jump, pas::kAS_Jump, "Jump"},
    {kBSC_Taunt, pas::kAS_Taunt, "Taunt"},
    {kBSC_Step, pas::kAS_Step, "Step"},
};

void NoteForced(const CPatterned& patterned) {
  const TUniqueId uid = patterned.GetUniqueId();
  if (std::find(sForcedActors.begin(), sForcedActors.end(), uid) == sForcedActors.end()) {
    sForcedActors.push_back(uid);
  }
}

// Drops the pending body commands of forced actors whose body state the PAS has no entry for;
// says so once per actor and command (into `out` when given, else stderr).
void DropUnplayableBodyCmds(CStateManager& mgr, std::string* out) {
  for (size_t i = 0; i < sForcedActors.size();) {
    CPatterned* patterned = TCastToPtr< CPatterned >(const_cast< CEntity* >(mgr.GetObjectById(sForcedActors[i])));
    if (patterned == nullptr) {
      sForcedActors.erase(sForcedActors.begin() + i);
      continue;
    }
    ++i;
    CBodyController* body = patterned->BodyController();
    if (body == nullptr) {
      continue;
    }
    CBodyStateCmdMgr& cmds = body->CommandMgr();
    for (const UnplayableBodyCmd& entry : kUnplayableBodyCmds) {
      if (cmds.GetCmd(entry.cmd) == nullptr || body->GetPASDatabase().HasState(entry.state)) {
        continue;
      }
      cmds.Reset();
      const std::pair< ushort, int > key(patterned->GetUniqueId().value, entry.cmd);
      if (std::find(sUnplayableReported.begin(), sUnplayableReported.end(), key) ==
          sUnplayableReported.end()) {
        sUnplayableReported.push_back(key);
        char line[192];
        std::snprintf(line, sizeof(line),
                      "u%04x: body cmd %s dropped: its PAS has no %s state (retail plays anim -1)\n",
                      key.first, entry.name, entry.name);
        if (out != nullptr) {
          *out += line;
        } else {
          std::fprintf(stderr, "[console] %s", line);
        }
      }
      break;
    }
  }
}

// fsm <id> <state>: put a patterned actor's state machine in a named state, through the machine's
// own SetState (the AFSM and FSM2 dispatchers both take a name), so a run can enter a state whose
// trigger it cannot reach. The leave/enter callbacks run as they would in game.
//
// The name is looked up first, so an unknown one is reported as such - with the list of the valid
// names - and a state that was entered and left at once (the machine is back where it was) says
// so instead of reading as an unknown name.
void CmdFsm(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() != 3) {
    Fail("usage: fsm <id> <state name>");
    return;
  }
  CEntity* ent = const_cast< CEntity* >(ResolveObject(mgr, args[1]));
  CPatterned* patterned = ent != nullptr ? TCastToPtr< CPatterned >(ent) : nullptr;
  if (patterned == nullptr) {
    Fail("not a patterned actor");
    return;
  }
  NoteForced(*patterned);
  TStateMachineStateBase< CPatterned >* machine = &patterned->StateMachineState();
  const std::string before = machine->HasState() && machine->GetName() != nullptr ? machine->GetName() : "?";
  // string_l borrows the characters: keep the std::string alive for every use of `name`.
  const std::string wanted(args[2]);
  const rstl::string name = rstl::string_l(wanted.c_str());
  // The names this machine can enter: its own states and, for an FSM2, the states of its
  // sub-machines (the ones PortSetStateDeep reaches).
  rstl::vector< rstl::string > valid;
  std::string where = "top machine";
  machine->PortCollectStateNames(valid, 8);
  // An FSM2's SetState looks only at its own top-level states; a state inside one of its
  // sub-machines is entered the way a transition into that sub-machine would enter it.
  if (machine->GetType() == 1) {
    const char* path[8] = {};
    const int entered = static_cast< CGenericFSM2State< CPatterned >* >(machine)->PortSetStateDeep(
        mgr, *patterned, name, path, 8);
    if (entered > 0) {
      where = "sub-machine ";
      for (int i = 0; i < entered; ++i) {
        where += (i != 0 ? "/" : "");
        where += path[i] != nullptr ? path[i] : "?";
      }
    }
  } else {
    machine->SetState(mgr, *patterned, name);
  }
  const char* after = machine->HasState() ? machine->GetName() : nullptr;
  if (after == nullptr) {
    // No state at all: the machine was never set up (an actor that was not created yet has none).
    Fail("the actor has no state machine state (is it active?)");
    return;
  }
  if (std::string_view(after) == args[2]) {
    out += "fsm " + before + " -> " + after + " [" + where + "]\n";
    return;
  }
  if (before != after) {
    // Another state now: the machine moved on (its own transitions) while the state was entered.
    out += "fsm " + before + " -> " + Str(args[2]) + " [" + where + "] (entered; now " + after +
           ")\n";
    return;
  }
  // The machine is back where it was: the state was entered and left at once, unless the name is
  // not one of the machine's at all.
  bool known = false;
  for (const rstl::string& candidate : valid) {
    if (candidate == name) {
      known = true;
      break;
    }
  }
  if (known) {
    out += "fsm " + before + " -> " + Str(args[2]) + " [" + where +
           "] (entered and left at once; now " + after + ")\n";
    return;
  }
  Fail("unknown state '" + Str(args[2]) + "'; valid names: " + StateNameList(valid));
}

// touch <mover id> <other id>: call one actor's CollidedWith the way the collision code does,
// with a collision list that carries the other actor's material list.
//
// The collision code (CGameCollision::MakeCollisionCallbacks) hands the mover its own
// CollidedWith(id, list, mgr) and then the other actor's CollidedWith with the same list; the
// list is in the mover's frame, so its 0x30 slot holds the *other* side's material list. A
// module that only does its own work in that callback - the SwampBossStage1 "player touched"
// flag, which the retail's fn_78_9A44 sets from the list's 0x30 slot - can then be driven
// without a real collision, which the harness cannot produce (a teleport moves the player
// without a touch).
void CmdTouch(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() != 3) {
    Fail("usage: touch <mover id> <other id>");
    return;
  }
  CEntity* moverEnt = const_cast< CEntity* >(ResolveObject(mgr, args[1]));
  CEntity* otherEnt = const_cast< CEntity* >(ResolveObject(mgr, args[2]));
  CPhysicsActor* mover = moverEnt != nullptr ? TCastToPtr< CPhysicsActor >(moverEnt) : nullptr;
  const CActor* other = otherEnt != nullptr ? TCastToPtr< CActor >(otherEnt) : nullptr;
  if (mover == nullptr || other == nullptr) {
    Fail("the mover must be a physics actor and the other an actor");
    return;
  }
  // One collision, at the other actor's position, with the other actor's material list in the
  // 0x30 slot (the "left" material of the host's CCollisionInfo, which is where the retail
  // reads the u64 it masks) and the mover's own list in the 0x38 slot.
  CCollisionInfoList list;
  list.Add(CCollisionInfo(other->GetTranslation(), mover->GetMaterialList(),
                          other->GetMaterialList(), CVector3f::Zero(), 0xffff));
  mover->CollidedWith(other->GetUniqueId(), list, mgr);
  char line[160];
  std::snprintf(line, sizeof(line), "touch %s -> %s (0x30 material 0x%llx)\n",
                Str(args[1]).c_str(), Str(args[2]).c_str(),
                static_cast< unsigned long long >(other->GetMaterialList().GetValue()));
  out += line;
}

// touchact <actor id> <other id>: call actor->Touch(other, mgr) directly, the call the collision
// code makes when two actors with SetCallTouch overlap (a patterned actor touching a trigger).
void CmdTouchAct(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() != 3) {
    Fail("usage: touchact <actor id> <other id>");
    return;
  }
  CEntity* actorEnt = const_cast< CEntity* >(ResolveObject(mgr, args[1]));
  CEntity* otherEnt = const_cast< CEntity* >(ResolveObject(mgr, args[2]));
  CActor* actor = actorEnt != nullptr ? TCastToPtr< CActor >(actorEnt) : nullptr;
  CActor* other = otherEnt != nullptr ? TCastToPtr< CActor >(otherEnt) : nullptr;
  if (actor == nullptr || other == nullptr) {
    Fail("both objects must be actors");
    return;
  }
  actor->Touch(*other, mgr);
  out += "touchact " + Str(args[1]) + " -> " + Str(args[2]) + "\n";
}

// A Metroid latching onto a space pirate (and letting go) calls the pirate module's attach and
// detach entries through its table (SSpacePirate_FuncPtrs); the port's CMetroid only detaches.
void CmdPirateAttach(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() != 3) {
    Fail("usage: pirateattach <pirate id> <actor id>|-");
    return;
  }
  CEntity* ent = const_cast< CEntity* >(ResolveObject(mgr, args[1]));
  CSpacePirate* pirate = ent != nullptr ? TCastToPtr< CSpacePirate >(ent) : nullptr;
  if (pirate == nullptr) {
    Fail("not a space pirate");
    return;
  }
  char line[96];
  if (args[2] == "-") {
    pirate->DetachActorFromPirate();
    std::snprintf(line, sizeof(line), "detached, held u%04x\n", pirate->GetAttachedActor().value);
  } else {
    const CEntity* actor = ResolveObject(mgr, args[2]);
    if (actor == nullptr) {
      return;
    }
    const bool taken = pirate->AttachActorToPirate(actor->GetUniqueId());
    std::snprintf(line, sizeof(line), "attach %s, held u%04x\n", taken ? "taken" : "refused",
                  pirate->GetAttachedActor().value);
  }
  out += line;
}

// The state/trigger function the actor's own table registered under `name`, from either machine
// type: the FSM2 (GetType() 1, CGenericFSM2State) or the AFSM (0, TStateMachineState).
CPatterned::StateMachine::StateFunc PortFindStateFunction(CPatterned& patterned, const char* name) {
  TStateMachineStateBase< CPatterned >* machine = &patterned.StateMachineState();
  if (machine->GetType() == 1) {
    return static_cast< CGenericFSM2State< CPatterned >* >(machine)->PortFindStateFunction(name);
  }
  return static_cast< TStateMachineState< CPatterned >* >(machine)->PortFindStateFunction(name);
}

CPatterned::StateMachine::TriggerFunc PortFindTriggerFunction(CPatterned& patterned,
                                                              const char* name) {
  TStateMachineStateBase< CPatterned >* machine = &patterned.StateMachineState();
  if (machine->GetType() == 1) {
    return static_cast< CGenericFSM2State< CPatterned >* >(machine)->PortFindTriggerFunction(name);
  }
  return static_cast< TStateMachineState< CPatterned >* >(machine)->PortFindTriggerFunction(name);
}

// statecall <id> <state> <msg> [dt]: run one state body of a patterned actor (FSM2 or AFSM) once with a message (0
// Activate, 1 Update, 2 Deactivate) and dt (default 1/60), outside the machine: the machine's state
// does not change. The body is looked up in the actor's whole state table, so a state the shipped
// FSM2 has no node for (unreachable by `fsm`) can still be run for coverage.
void CmdStateCall(CStateManager& mgr, const Args& args, std::string& out) {
  float msgArg = 0.f;
  float dt = 1.f / 60.f;
  if (args.size() < 4 || args.size() > 5 || !ParseFloat(args[3], msgArg) ||
      (args.size() == 5 && !ParseFloat(args[4], dt))) {
    Fail("usage: statecall <id> <state name> <msg 0..2> [dt]");
    return;
  }
  CEntity* ent = const_cast< CEntity* >(ResolveObject(mgr, args[1]));
  CPatterned* patterned = ent != nullptr ? TCastToPtr< CPatterned >(ent) : nullptr;
  if (patterned == nullptr) {
    Fail("not a patterned actor");
    return;
  }
  const int msg = static_cast< int >(msgArg);
  const std::string wanted(args[2]);
  const CPatterned::StateMachine::StateFunc fn = PortFindStateFunction(*patterned, wanted.c_str());
  if (fn == nullptr) {
    Fail("no state function '" + wanted + "'");
    return;
  }
  NoteForced(*patterned);
  (patterned->*fn)(mgr, static_cast< EStateMsg >(msg), dt);
  out += "statecall " + wanted + " msg " + std::to_string(msg) + "\n";
  DropUnplayableBodyCmds(mgr, &out);
}

// trigcall <id> <trigger> [arg]: evaluate one trigger of an FSM2 actor once with its argument
// (default 0) and print the answer, outside the machine, like `statecall` for the states.
void CmdTrigCall(CStateManager& mgr, const Args& args, std::string& out) {
  float arg = 0.f;
  if (args.size() < 3 || args.size() > 4 || (args.size() == 4 && !ParseFloat(args[3], arg))) {
    Fail("usage: trigcall <id> <trigger name> [arg]");
    return;
  }
  CEntity* ent = const_cast< CEntity* >(ResolveObject(mgr, args[1]));
  CPatterned* patterned = ent != nullptr ? TCastToPtr< CPatterned >(ent) : nullptr;
  if (patterned == nullptr) {
    Fail("not a patterned actor");
    return;
  }
  const std::string wanted(args[2]);
  const CPatterned::StateMachine::TriggerFunc fn =
      PortFindTriggerFunction(*patterned, wanted.c_str());
  if (fn == nullptr) {
    Fail("no trigger function '" + wanted + "'");
    return;
  }
  const bool answer = (patterned->*fn)(mgr, CTriggerData(arg));
  out += "trigcall " + wanted + " = " + (answer ? "true" : "false") + "\n";
}

// codecall <id> <code> [dt]: run one code function of an FSM2 actor once with dt (default 1/60),
// outside the machine, like `statecall` for the states (a code runs on a transition).
void CmdCodeCall(CStateManager& mgr, const Args& args, std::string& out) {
  float dt = 1.f / 60.f;
  if (args.size() < 3 || args.size() > 4 || (args.size() == 4 && !ParseFloat(args[3], dt))) {
    Fail("usage: codecall <id> <code name> [dt]");
    return;
  }
  CEntity* ent = const_cast< CEntity* >(ResolveObject(mgr, args[1]));
  CPatterned* patterned = ent != nullptr ? TCastToPtr< CPatterned >(ent) : nullptr;
  if (patterned == nullptr) {
    Fail("not a patterned actor");
    return;
  }
  TStateMachineStateBase< CPatterned >* machine = &patterned->StateMachineState();
  if (machine->GetType() != 1) {
    Fail("not an FSM2 machine");
    return;
  }
  const std::string wanted(args[2]);
  const CPatterned::StateMachine::CodeFunc fn =
      static_cast< CGenericFSM2State< CPatterned >* >(machine)->PortFindCodeFunction(wanted.c_str());
  if (fn == nullptr) {
    Fail("no code function '" + wanted + "'");
    return;
  }
  (patterned->*fn)(mgr, dt);
  out += "codecall " + wanted + "\n";
}

// The searches `pathfail` holds failing (PortConsole::PathSearchForcedFail). Pointers only, never
// dereferenced: a deleted actor's entry stays until `pathfail <id> off` or a new one reuses it.
std::vector< const CPathFindSearch* >& FailingSearches() {
  static std::vector< const CPathFindSearch* > sSearches;
  return sSearches;
}

// pathfail <id> on|off: make a patterned actor's path search (its GetSearchPath) report a miss, so
// the arms behind a failed Search / FindClosestReachablePoint run on an area whose mesh reaches.
void CmdPathFail(CStateManager& mgr, const Args& args, std::string& out) {
  CEntity* ent = args.size() == 3 ? const_cast< CEntity* >(ResolveObject(mgr, args[1])) : nullptr;
  CPatterned* patterned = ent != nullptr ? TCastToPtr< CPatterned >(ent) : nullptr;
  if (patterned == nullptr || (args[2] != "on" && args[2] != "off")) {
    Fail("usage: pathfail <id> on|off   (id must be a patterned actor)");
    return;
  }
  const CPathFindSearch* search = patterned->GetSearchPath();
  if (search == nullptr) {
    Fail("the actor has no path search");
    return;
  }
  std::vector< const CPathFindSearch* >& searches = FailingSearches();
  searches.erase(std::remove(searches.begin(), searches.end(), search), searches.end());
  if (args[2] == "on") {
    searches.push_back(search);
  }
  out += "pathfail " + Str(args[2]) + "\n";
}

// setarea <id> <area>: set an entity's current area id (CEntity +0x4), e.g. so a same-area filter
// sees a neighbour from another area. Nothing else moves; put the old id back afterwards. An area
// that exists but is not loaded is refused.
void CmdSetArea(CStateManager& mgr, const Args& args, std::string& out) {
  CEntity* ent = args.size() == 3 ? const_cast< CEntity* >(ResolveObject(mgr, args[1])) : nullptr;
  float area = 0.f;
  if (ent == nullptr || !ParseFloat(args[2], area)) {
    Fail("usage: setarea <id> <area index>");
    return;
  }
  // An actor's UpdatePortalSystemState reads the area's post-constructed portal data whenever
  // the area exists (CActor.cpp UpdatePortalSystemState), so an existing area that is not loaded
  // crashes the next update (oct1). -1 and ids past the world's areas are skipped there.
  const TAreaId newArea(static_cast< int >(area));
  const CWorld* world = mgr.GetWorld();
  if (world != nullptr && world->DoesAreaExist(newArea) &&
      (!world->GetArea(newArea)->IsLoaded() ||
       world->GetArea(newArea)->GetPostConstructed() == nullptr)) {
    char why[64];
    std::snprintf(why, sizeof(why), "setarea: area %d is not loaded", newArea.Value());
    Fail(why);
    return;
  }
  const int old = ent->GetCurrentAreaId().Value();
  ent->SetCurrentAreaId(newArea);
  char line[64];
  std::snprintf(line, sizeof(line), "area %d -> %d\n", old, ent->GetCurrentAreaId().Value());
  out += line;
}

// The module handlers `var` asks (PortConsole::RegisterVar); a function-local list, so a module's
// static-init registration never runs before it exists.
CStateManager* sVarManager = nullptr;  // the running `var` command's manager (VarManager)

std::vector< VarHandler >& VarHandlers() {
  static std::vector< VarHandler > sHandlers;
  return sHandlers;
}

// var <id> <name> [value]: read or set a module field a REL's port registered (dghd.attack,
// dghd.flags, dghd.sweeptime), so an arm a fight rarely draws runs without gdb. `var player
// spawnmorph [n]` reads or sets the player's spawned morph-ball state (0x390), keeping its morph
// state.
void CmdVar(CStateManager& mgr, const Args& args, std::string& out) {
  float number = 0.f;
  if (args.size() < 3 || args.size() > 4 || (args.size() == 4 && !ParseFloat(args[3], number))) {
    Fail("usage: var <id>|player <name> [value]");
    return;
  }
  const int value = static_cast< int >(number);
  const std::string name(args[2]);
  int current = 0;
  char line[160];
  if (args[1] == "player") {
    CPlayer* player = mgr.GetPlayer(0);
    if (player == nullptr || name != "spawnmorph") {
      Fail(player == nullptr ? "no player" : "player vars: spawnmorph");
      return;
    }
    if (args.size() == 4) {
      player->SetMorphBallState(player->GetMorphballTransitionState(),
                                static_cast< CPlayer::EPlayerMorphBallState >(value));
    }
    std::snprintf(line, sizeof(line), "player spawnmorph = %d\n",
                  static_cast< int >(player->GetSpawnedMorphballState()));
    out += line;
    return;
  }
  CEntity* ent = const_cast< CEntity* >(ResolveObject(mgr, args[1]));
  if (ent == nullptr) {
    Fail("no object '" + std::string(args[1]) + "'");
    return;
  }
  sVarManager = &mgr;
  for (VarHandler handler : VarHandlers()) {
    if (handler(*ent, name.c_str(), args.size() == 4 ? &value : nullptr, current)) {
      sVarManager = nullptr;
      std::snprintf(line, sizeof(line), "%s = %d (0x%x)\n", name.c_str(), current, current);
      out += line;
      return;
    }
  }
  sVarManager = nullptr;
  Fail("no var '" + name + "' on that object");
}

// hint <id> [type] [area <n>] [radius <r>]: print an AI hint's type, area, active flag and radius,
// after setting its type, area and/or radius, so a module that looks hints up by type and area
// (DarkSamus's phazon waypoints) finds one the area lacks, or skips one of another area.
void CmdHint(CStateManager& mgr, const Args& args, std::string& out) {
  float type = -1.f;
  float area = -1.f;
  float radius = -1.f;
  bool ok = args.size() >= 2;
  for (size_t i = 2; ok && i < args.size(); ++i) {
    if (args[i] == "area" && i + 1 < args.size()) {
      ok = ParseFloat(args[++i], area);
    } else if (args[i] == "radius" && i + 1 < args.size()) {
      ok = ParseFloat(args[++i], radius);
    } else {
      ok = ParseFloat(args[i], type);
    }
  }
  if (!ok) {
    Fail("usage: hint <id> [type] [area <n>] [radius <r>]");
    return;
  }
  CEntity* ent = const_cast< CEntity* >(ResolveObject(mgr, args[1]));
  CScriptAIHint* hint = ent != nullptr ? TCastToPtr< CScriptAIHint >(ent) : nullptr;
  if (hint == nullptr) {
    Fail("not an AI hint");
    return;
  }
  if (type >= 0.f) {
    hint->PortSetHintType(static_cast< CScriptAIHint::EHintType >(static_cast< int >(type)));
  }
  if (area >= 0.f) {
    hint->SetCurrentAreaId(TAreaId(static_cast< int >(area)));
  }
  if (radius >= 0.f) {
    hint->PortSetRadius(radius);
  }
  out += "hint " + std::string(args[1]) + " type " +
         std::to_string(static_cast< int >(hint->GetHintType())) + " area " +
         std::to_string(hint->GetCurrentAreaId().Value()) + " active " +
         std::to_string(hint->GetActive() ? 1 : 0) + " radius " + std::to_string(hint->GetRadius()) +
         "\n";
}

// uevent <id> <type> [dt] [locator]: deliver a user anim event (EUserEventType, e.g. 27 EventStart,
// 28 EventStop, 18 EndAction, 12 Landing) to an actor's DoUserAnimEvent with an empty POI node
// whose locator is `locator` (default "root"), for the event arms an animation never fires on the
// host.
void CmdUEvent(CStateManager& mgr, const Args& args, std::string& out) {
  float type = 0.f;
  float dt = 0.f;
  CEntity* ent = args.size() >= 3 ? const_cast< CEntity* >(ResolveObject(mgr, args[1])) : nullptr;
  CActor* actor = ent != nullptr ? TCastToPtr< CActor >(ent) : nullptr;
  if (actor == nullptr || !ParseFloat(args[2], type) ||
      (args.size() >= 4 && !ParseFloat(args[3], dt))) {
    Fail("usage: uevent <id> <type> [dt] [locator]   (id must be an actor)");
    return;
  }
  const rstl::string locator = rstl::string_l(args.size() >= 5 ? Str(args[4]).c_str() : "root");
  actor->DoUserAnimEvent(mgr, CInt32POINode(-1, kPT_EmptyInt32, CCharAnimTime(), -1, false, 1.f,
                                            -1, 0, 0, locator),
                         static_cast< EUserEventType >(static_cast< int >(type)), dt);
  out += "uevent " + std::string(args[1]) + " " + std::to_string(static_cast< int >(type)) + "\n";
}

// shoot <id> [charged] [missile] [dist] [dir x y z]: fire the player's current beam from dist units
// (default 3) away, travelling along dir (default: from the player's eye), straight at an actor's aim
// position (a joint collision actor's own position), homing on it. The shot is a real
// CEnergyProjectile with the beam's damage, so a module that only counts projectile hits (a
// weak-spot joint's last toucher) sees it. Unlike `damage`, this tests flight and collision too.
// `missile` fires a real player missile
// instead, through the launcher's own path (CPlayerGun::FireSecondary): weapon type kWT_Missile
// and gpTweakPlayerGun's missile damage, aimed at the actor the way player aim is
// (PortSetPointerAimPoint, CPlayer.cpp:1016-1017) and homing on its id. It leaves from the gun, so
// `dist` and `dir` do not apply to it. A module that only reacts to missiles
// (CRezbit::DeflectMissiles, CRezbit.cpp:975) needs it.
void CmdShoot(CStateManager& mgr, const Args& args, std::string& out) {
  CEntity* ent = args.size() >= 2 ? const_cast< CEntity* >(ResolveObject(mgr, args[1])) : nullptr;
  CActor* actor = ent != nullptr ? TCastToPtr< CActor >(ent) : nullptr;
  CPlayer* player = mgr.GetPlayer(0);
  if (actor == nullptr) {
    Fail("usage: shoot <id> [charged|missile] [dist]   (id must be an actor that still exists)");
    return;
  }
  if (player == nullptr) {
    Fail("there is no player");
    return;
  }
  CPlayerState::EChargeStage stage = CPlayerState::kCS_Normal;
  float dist = 3.f;
  bool missile = false;
  const CVector3f target = actor->GetAimPosition(mgr, 0.f);
  // GetTranslation is the player's feet: a low target then sends the shot through the floor
  // (Glowbug in 0i_temple_hall). GetAimPosition includes the eye/ball height, as player aim does.
  CVector3f dir = target - player->GetAimPosition(mgr, 0.f);
  for (size_t i = 2; i < args.size(); ++i) {
    float x = 0.f, y = 0.f, z = 0.f;
    if (args[i] == "charged") {
      stage = CPlayerState::kCS_Charged;
    } else if (args[i] == "missile") {
      missile = true;
    } else if (args[i] == "dir" && i + 3 < args.size() && ParseFloat(args[i + 1], x) &&
               ParseFloat(args[i + 2], y) && ParseFloat(args[i + 3], z)) {
      dir = CVector3f(x, y, z); // the shot's travel direction, e.g. `dir 0 0 1` comes from below
      i += 3;
    } else if (!ParseFloat(args[i], dist) || dist <= 0.f) {
      Fail("shoot: options are `charged`, `missile`, a distance > 0 and `dir <x> <y> <z>`");
      return;
    }
  }
  if (missile) {
    // The launcher's own path. FireSecondary reads the missiles in the ammo and the gun's aim
    // (which PortSetPointerAimPoint turns towards the actor), so it needs one; a harness run
    // without the launcher gets them, and the reply says so.
    CPlayerGun* gun = player->GetPlayerGun();
    CPlayerState* state = mgr.PlayerState(0);
    if (gun == nullptr || state == nullptr) {
      Fail("no player gun or player state");
      return;
    }
    const bool granted = state->GetItemAmount(CPlayerState::kIT_Missile, true) < 1;
    if (granted) {
      state->SetItemAmount(CPlayerState::kIT_Missile, 10);
    }
    gun->PortSetPointerAimPoint(true, target);
    gun->FireSecondary(1.f / 60.f, mgr, actor->GetUniqueId(), 0, nullptr,
                       CSfxManager::kInternalInvalidSfxId);
    gun->PortSetPointerAimPoint(false, CVector3f::Zero());
    char line[160];
    std::snprintf(line, sizeof(line),
                  "missile at u%04x: %d in flight, %d left%s\n", actor->GetUniqueId().value,
                  mgr.GetWeaponIdCount(player->GetUniqueId(), kWT_Missile),
                  state->GetItemAmount(CPlayerState::kIT_Missile, true),
                  granted ? " (gave missile 10)" : "");
    out += line;
    return;
  }
  CGunWeapon* beam = player->GetPlayerGun() != nullptr ? player->GetPlayerGun()->PortCurrentBeam()
                                                       : nullptr;
  if (beam == nullptr) {
    Fail("the player has no beam in hand (`beam <name>`); `missile` needs none");
    return;
  }
  dir = dir.CanBeNormalized() ? dir.AsNormalized() : CVector3f(0.f, 1.f, 0.f);
  const CVector3f start = target - dir * dist;
  TUniqueId shot = kInvalidUniqueId;
  beam->Fire(beam->GetProjectileToken(stage), false, 0.f, stage,
             CTransform4f::LookAt(start, target, CVector3f::Up()), mgr, actor->GetUniqueId(), 0,
             CSfxManager::kInternalInvalidSfxId, &shot, nullptr, 1.f, 1.f);
  char line[128];
  std::snprintf(line, sizeof(line), "shot u%04x at u%04x from %.1f units%s\n", shot.value,
                actor->GetUniqueId().value, dist, stage == CPlayerState::kCS_Charged ? " (charged)" : "");
  out += line;
}

// struggle <value>|off: hold the player's attached-actor struggle (CPlayer::mAttachedActorStruggle,
// CPlayer.cpp:2616-2627) at value 0..1, cleared by `off`. The game itself only grows it from held
// turn/jump buttons while the morph ball is attached to a grabber, and decays it every update
// otherwise, so a set value does not survive a frame on its own: this is a held override, applied
// in CPlayer::Update's own morph-ball block. 1.f is what CTryclops::Think (CTryclops.cpp:212)
// reads to run its dizzy gore and BallCloseToCollision (:900).
void CmdStruggle(CStateManager& mgr, const Args& args, std::string& out) {
  CPlayer* player = mgr.GetPlayer(0);
  if (player == nullptr) {
    Fail("no player");
    return;
  }
  float value = -1.f;
  if (args.size() < 2 ||
      (args[1] != "off" && (!ParseFloat(args[1], value) || value < 0.f || value > 1.f))) {
    Fail("usage: struggle <0..1>|off   (held every update; `off` clears it)");
    return;
  }
  player->PortSetStruggleOverride(value);
  char line[160];
  if (value < 0.f) {
    std::snprintf(line, sizeof(line), "struggle off (was %.3f)\n",
                  static_cast< double >(player->GetAttachedActorStruggle()));
  } else {
    std::snprintf(line, sizeof(line), "struggle held at %.3f (attached %04x, value now %.3f)\n",
                  static_cast< double >(value), player->GetAttachedActorId().value,
                  static_cast< double >(player->GetAttachedActorStruggle()));
  }
  out += line;
}

// place <id> <x> <y> <z> | place <id> player [dist]: move an actor (keeping its rotation), to a
// point or `dist` units (default 6) in front of the player, and stop any physics motion.
void CmdPlace(CStateManager& mgr, const Args& args, std::string& out) {
  CEntity* ent = args.size() >= 3 ? const_cast< CEntity* >(ResolveObject(mgr, args[1])) : nullptr;
  CActor* actor = ent != nullptr ? TCastToPtr< CActor >(ent) : nullptr;
  if (actor == nullptr) {
    Fail("usage: place <id> <x> <y> <z> | place <id> player [dist]   (id must be an actor)");
    return;
  }
  CVector3f pos;
  if (args[2] == "player") {
    CPlayer* player = mgr.GetPlayer(0);
    float dist = 6.f;
    if (player == nullptr || (args.size() > 3 && !ParseFloat(args[3], dist))) {
      Fail("no player, or a bad distance");
      return;
    }
    CVector3f dir = player->GetTransform().GetForward();
    dir.SetZ(0.f);
    dir = dir.CanBeNormalized() ? dir.AsNormalized() : CVector3f(0.f, 1.f, 0.f);
    pos = player->GetTranslation() + dir * dist;
  } else {
    float x = 0.f, y = 0.f, z = 0.f;
    if (args.size() < 5 || !ParseFloat(args[2], x) || !ParseFloat(args[3], y) ||
        !ParseFloat(args[4], z)) {
      Fail("usage: place <id> <x> <y> <z> | place <id> player [dist]");
      return;
    }
    pos = CVector3f(x, y, z);
  }
  actor->SetTranslation(pos);
  // The optional rotation: place <id> <x> <y> <z> [yaw [pitch [roll]]] in degrees - the yaw
  // about the world's up (z), the pitch about the right one (x) and the roll about the forward
  // one (y), applied in that order.  Without it the actor keeps its rotation.
  if (args.size() >= 6) {
    const float yaw = std::atof(Str(args[5]).c_str());
    const float pitch = args.size() >= 7 ? std::atof(Str(args[6]).c_str()) : 0.f;
    const float roll = args.size() >= 8 ? std::atof(Str(args[7]).c_str()) : 0.f;
    const CQuaternion rot = CQuaternion::YRotation(CRelAngle::FromDegrees(roll)) *
                             CQuaternion::XRotation(CRelAngle::FromDegrees(pitch)) *
                             CQuaternion::ZRotation(CRelAngle::FromDegrees(yaw));
    actor->SetRotation(rot);
  }
  if (CPhysicsActor* physics = TCastToPtr< CPhysicsActor >(ent)) {
    physics->Stop();
  }
  char line[64];
  std::snprintf(line, sizeof(line), "placed at %.2f %.2f %.2f\n", pos.GetX(), pos.GetY(), pos.GetZ());
  out += line;
}

// spawn <fourcc> [count] [d <dist>] [ancs <id>|u<id> [char [anim]]] [fsm <id>] [model <id>|u<id>] [inactive]
//   [prop <propid> <value>]...:
// create script objects of a type through its ScriptLoader entry, the way an area load does, for
// loaders no disc placement reaches (CHOG, COIN, DBAR, RIPR, RSFA, SPNK). The property stream is
// built here: the EditorProperties struct (name, transform `dist` (8) units in front of the player
// facing it, active) and only what the options add, so every other SLdr default applies. Options:
// `ancs` adds PatternedAITypedef.animationInformation (patterned loaders refuse without one),
// `model` sets COIN's model / every DBAR chunk model (both take a hex asset id or an actor id
// u<id>/e<id> whose ANCS+character or static model is borrowed as a stand-in body), `prop` adds a raw 4-byte top-level property
// (a value with a '.' is a float, else an integer, hex with 0x). Asset ids are checked against the
// resource factory first, so a missing asset is a refusal, not a CStaticRes on nothing. Each object
// `fsm <id>` sets PatternedAITypedef.stateMachine (an AFSM/FSM2 asset), `rules <id>` its
// knockbackRules (default 393ca543), `wpsc <id>` the type's projectile WPSC, `conn <state> <msg>
// <id>` adds a script connection (CEntityInfo's list, as SCLY gives it); `inactive` spawns it
// inactive (a COIN only rolls its lifetime and impulse on the Activate that wakes it, so spawn it
// inactive and `send e<id> activate`). Each object
// gets a free editor id 0xFF00+ in layer 0 of the player's area (`e<id>` works) and, like
// RegisterScriptObjects, AddObject + kSM_AreaLoaded. A loader returning null is reported.
struct SSpawnBlob {
  std::vector< uint8_t > bytes;
  void U8(uint v) { bytes.push_back(static_cast< uint8_t >(v)); }
  void U16(uint v) { U8(v >> 8); U8(v); }
  void U32(uint v) { U16(v >> 16); U16(v & 0xFFFF); }
  void F32(float f) {
    uint v;
    std::memcpy(&v, &f, sizeof(v));
    U32(v);
  }
  // A property header; EndProp patches the u16 size once the body is written.
  size_t BeginProp(uint id) {
    U32(id);
    U16(0);
    return bytes.size();
  }
  void EndProp(size_t start) {
    const size_t size = bytes.size() - start;
    bytes[start - 2] = static_cast< uint8_t >(size >> 8);
    bytes[start - 1] = static_cast< uint8_t >(size);
  }
};

void CmdSpawn(CStateManager& mgr, const Args& args, std::string& out) {
  CPlayer* player = mgr.GetPlayer(0);
  if (args.size() < 2 || args[1].size() != 4 || player == nullptr) {
    Fail("usage: spawn <fourcc> [count] [d <dist>] [ancs <id>|u<id> [char [anim]]] [fsm <id>] "
         "[rules <id>] [wpsc <id>] [conn <state> <msg> <id>] [model <id>|u<id>] [inactive] [prop <propid> <value>]... "
         "(needs a player)");
    return;
  }
  FourCC type = 0;
  for (char c : args[1]) {
    type = (type << 8) | static_cast< uint8_t >(std::toupper(static_cast< unsigned char >(c)));
  }
  char typeName[5] = {static_cast< char >(type >> 24), static_cast< char >(type >> 16),
                      static_cast< char >(type >> 8), static_cast< char >(type), 0};
  const FScriptLoader loader = GetScriptLoaderForType(type);
  if (loader == nullptr) {
    Fail(std::string("no ScriptLoader entry for '") + typeName + "'");
    return;
  }
  auto parseU32 = [](std::string_view text, uint& value) {
    const std::string s(text);
    char* end = nullptr;
    value = static_cast< uint >(std::strtoul(s.c_str(), &end, 0));
    return !s.empty() && end != nullptr && *end == '\0';
  };
  auto parseHexId = [](std::string_view text, uint& value) {
    const std::string s(text);
    char* end = nullptr;
    value = static_cast< uint >(std::strtoul(s.c_str(), &end, 16));
    return !s.empty() && end != nullptr && *end == '\0';
  };
  int count = 1;
  float dist = 8.f;
  bool haveAncs = false;
  uint ancs = 0, character = 0, anim = 0, model = 0;
  bool haveModel = false;
  uint fsm = 0;
  bool haveFsm = false;
  bool startActive = true;
  // PatternedAITypedef.knockbackRules: the SLdr default -1 reads as an all-zero RULE whose parent
  // rule 0 is itself, and CKnockBackController recurses until the allocator fails. 393ca543 is in
  // every Metroid*.pak; `rules ffffffff` keeps the SLdr default.
  uint rules = 0x393ca543;
  bool haveRules = false;
  uint wpsc = 0;
  bool haveWpsc = false;
  std::vector< std::pair< uint, uint > > props;
  rstl::vector< SConnection > connections;
  size_t i = 2;
  if (i < args.size() && std::isdigit(static_cast< unsigned char >(args[i][0]))) {
    uint n = 0;
    if (!parseU32(args[i], n) || n < 1 || n > 32) {
      Fail("spawn: count is 1..32");
      return;
    }
    count = static_cast< int >(n);
    ++i;
  }
  for (; i < args.size(); ++i) {
    if (args[i] == "d" && i + 1 < args.size() && ParseFloat(args[i + 1], dist)) {
      ++i;
    } else if (args[i] == "ancs" && i + 1 < args.size() &&
               (args[i + 1][0] == 'u' || args[i + 1][0] == 'e' || parseHexId(args[i + 1], ancs))) {
      // ancs u<id>|e<id>: borrow an animated actor's ANCS and character (a stand-in body for a
      // type whose own assets are not in this area's paks).
      if (args[i + 1][0] == 'u' || args[i + 1][0] == 'e') {
        const CActor* donor = TCastToConstPtr< CActor >(ResolveObject(mgr, args[i + 1]));
        const CModelData* data = donor != nullptr ? donor->GetModelData() : nullptr;
        if (data == nullptr || !data->HasAnimation()) {
          Fail("spawn: ancs " + Str(args[i + 1]) + " is not an animated actor");
          return;
        }
        ancs = data->GetAnimationData()->PortSelfId();
        character = static_cast< uint >(data->GetAnimationData()->GetCharacterIndex());
      }
      haveAncs = true;
      ++i;
      // parseU32 stores its strtoul result even when the text is no number, so parse into a
      // scratch: the next option's name (`fsm`) must not reset the donor's character to 0.
      uint number = 0;
      if (i + 1 < args.size() && parseU32(args[i + 1], number)) {
        character = number;
        ++i;
        if (i + 1 < args.size() && parseU32(args[i + 1], number)) {
          anim = number;
          ++i;
        }
      }
    } else if (args[i] == "model" && i + 1 < args.size() &&
               (args[i + 1][0] == 'u' || args[i + 1][0] == 'e' || parseHexId(args[i + 1], model))) {
      // model u<id>|e<id>: borrow an actor's static model.
      if (args[i + 1][0] == 'u' || args[i + 1][0] == 'e') {
        CActor* donor = TCastToPtr< CActor >(const_cast< CEntity* >(ResolveObject(mgr, args[i + 1])));
        CModelData* data = donor != nullptr ? const_cast< CModelData* >(donor->GetModelData()) : nullptr;
        if (data == nullptr || !data->HasNormalModel()) {
          Fail("spawn: model " + Str(args[i + 1]) + " is not an actor with a static model");
          return;
        }
        model = static_cast< const TToken< CModel >& >(*data->NormalModel()).GetTag().id;
      }
      haveModel = true;
      ++i;
    } else if (args[i] == "conn" && i + 3 < args.size()) {
      // conn <state> <msg> <target>: a script connection on the spawned object, the way an area's
      // SCLY lists them (CEntity::mConnections, editor ids). The target is u<id> (that object's
      // editor id), e<hex> or a bare hex editor id (it need not be loaded yet).
      const int state = EnumValue(kStateNames, kStateCount, args[i + 1]);
      const int msg = EnumValue(kMessageNames, kMessageCount, args[i + 2]);
      uint target = 0;
      if (args[i + 3][0] == 'u') {
        const CEntity* ent = ResolveObject(mgr, args[i + 3]);
        if (ent == nullptr) {
          return;
        }
        target = ent->GetEditorId().Value();
      } else if (!parseHexId(args[i + 3][0] == 'e' ? args[i + 3].substr(1) : args[i + 3], target)) {
        target = kInvalidEditorId.Value();
      }
      if (state < 0 || msg < 0 || target == kInvalidEditorId.Value()) {
        Fail("spawn: conn <state> <msg> <target>: bad " +
             Str(state < 0 ? args[i + 1] : msg < 0 ? args[i + 2] : args[i + 3]));
        return;
      }
      connections.push_back(SConnection(static_cast< EScriptObjectState >(state),
                                        static_cast< EScriptObjectMessage >(msg),
                                        TEditorId(target)));
      i += 3;
    } else if (args[i] == "inactive") {
      startActive = false;
    } else if (args[i] == "fsm" && i + 1 < args.size() && parseHexId(args[i + 1], fsm)) {
      haveFsm = true;
      ++i;
    } else if (args[i] == "rules" && i + 1 < args.size() && parseHexId(args[i + 1], rules)) {
      haveRules = true;
      ++i;
    } else if (args[i] == "wpsc" && i + 1 < args.size() && parseHexId(args[i + 1], wpsc)) {
      haveWpsc = true;
      ++i;
    } else if (args[i] == "prop" && i + 2 < args.size()) {
      uint id = 0, value = 0;
      float f = 0.f;
      if (!parseHexId(args[i + 1], id)) {
        Fail("spawn: bad property id '" + Str(args[i + 1]) + "'");
        return;
      }
      if (args[i + 2].find('.') != std::string_view::npos && ParseFloat(args[i + 2], f)) {
        std::memcpy(&value, &f, sizeof(value));
      } else if (!parseU32(args[i + 2], value)) {
        Fail("spawn: bad property value '" + Str(args[i + 2]) + "'");
        return;
      }
      props.emplace_back(id, value);
      i += 2;
    } else {
      Fail("spawn: unknown option '" + Str(args[i]) + "'");
      return;
    }
  }
  // CScriptDestructibleBarrier's ctor uses its base (left chunk) model's CModelData
  // unconditionally (SetRenderUnsortedParts), so a barrier without one dereferences null.
  if (type == 'DBAR' && !haveModel) {
    Fail("spawn DBAR refused: the barrier needs a chunk model (`model <id>`)");
    std::printf("[console] spawn DBAR refused: no chunk model\n");
    return;
  }
  // A missing asset would reach CAnimRes/CStaticRes with nothing behind it.
  if (haveFsm && gpResourceFactory->GetResourceTypeById(fsm) != 'AFSM' &&
      gpResourceFactory->GetResourceTypeById(fsm) != 'FSM2') {
    {
      char why[64];
      std::snprintf(why, sizeof(why), "spawn: fsm %08x is not an AFSM/FSM2 asset here", fsm);
      Fail(why);
    }
    std::printf("[console] spawn %s refused: no state machine asset %08x\n", typeName, fsm);
    return;
  }
  if ((haveAncs && gpResourceFactory->GetResourceTypeById(ancs) != 'ANCS') ||
      (haveModel && gpResourceFactory->GetResourceTypeById(model) != 'CMDL')) {
    char why[96];
    std::snprintf(why, sizeof(why), "spawn %s refused: %s %08x is not loadable here", typeName,
                  haveAncs && gpResourceFactory->GetResourceTypeById(ancs) != 'ANCS' ? "ANCS"
                                                                                      : "CMDL",
                  haveAncs && gpResourceFactory->GetResourceTypeById(ancs) != 'ANCS' ? ancs : model);
    std::printf("[console] %s\n", why);
    Fail(why);
    return;
  }
  const bool patternedTypedef = haveAncs || haveFsm || haveRules;
  if (patternedTypedef && rules != kInvalidAssetId &&
      gpResourceFactory->GetResourceTypeById(rules) != 'RULE') {
    if (haveRules) {
      char why[64];
      std::snprintf(why, sizeof(why), "spawn: rules %08x is not a RULE asset here", rules);
      Fail(why);
      return;
    }
    rules = kInvalidAssetId; // the default RULE is not in this area's paks: SLdr default
  }
  const bool writeRules = patternedTypedef && rules != kInvalidAssetId;
  // The projectile WPSC properties a type's SLdr leaves at -1: CProjectileInfo then holds a token
  // on nothing and the first shot crashes. `wpsc <id>` writes them; without it a type listed here
  // gets the default 681f3907 (the WPSC the ChozoGhost runs used) when it is here, and is refused otherwise.
  // Other types take `wpsc` as their common top-level `projectile` (0xef485db9).
  static const uint kChozoGhostWpsc[] = {0x54151870, 0x3a58089c};
  static const uint kCommonWpsc[] = {0xef485db9};
  const uint* wpscProps = nullptr;
  int wpscCount = 0;
  if (type == 'CHOG') {
    wpscProps = kChozoGhostWpsc;
    wpscCount = 2;
  } else if (haveWpsc) {
    wpscProps = kCommonWpsc;
    wpscCount = 1;
  }
  if (wpscProps != nullptr) {
    // A raw `prop` naming one of these wins over both.
    int explicitProps = 0;
    for (int w = 0; w < wpscCount; ++w) {
      for (const std::pair< uint, uint >& p : props) {
        explicitProps += p.first == wpscProps[w] ? 1 : 0;
      }
    }
    if (!haveWpsc) {
      wpsc = 0x681f3907;
    }
    if (explicitProps == wpscCount) {
      wpscProps = nullptr;
      wpscCount = 0;
    } else if (gpResourceFactory->GetResourceTypeById(wpsc) != 'WPSC') {
      char why[96];
      std::snprintf(why, sizeof(why), "spawn %s refused: %s WPSC %08x is not here (`wpsc <id>`)",
                    typeName, haveWpsc ? "the" : "the default", wpsc);
      std::printf("[console] %s\n", why);
      Fail(why);
      return;
    }
  }
  TAreaId area = player->GetCurrentAreaId();
  if (area == kInvalidAreaId) {
    area = mgr.GetNextAreaId();
  }
  CVector3f dir = player->GetTransform().GetForward();
  dir.SetZ(0.f);
  dir = dir.CanBeNormalized() ? dir.AsNormalized() : CVector3f(0.f, 1.f, 0.f);
  const CVector3f right(dir.GetY(), -dir.GetX(), 0.f);
  // The editor's Z rotation turns forward (0,1,0) into (-sin a, cos a); face the player (-dir).
  const float yawDeg = std::atan2(dir.GetX(), -dir.GetY()) * 57.29578f;
  uint nextIndex = 0xFF00;
  for (int n = 0; n < count; ++n) {
    TEditorId editorId(kInvalidEditorId);
    for (; nextIndex <= 0xFFFE; ++nextIndex) {
      const TEditorId candidate((static_cast< uint >(area.Value()) & 0x3FF) << 16 | nextIndex);
      if (mgr.GetIdForScript(candidate) == kInvalidUniqueId) {
        editorId = candidate;
        ++nextIndex;
        break;
      }
    }
    if (editorId == kInvalidEditorId) {
      Fail("spawn: no free editor id left in 0xFF00..0xFFFE of this area");
      return;
    }
    const CVector3f pos =
        player->GetTranslation() + dir * dist + right * (3.f * (n - (count - 1) * 0.5f));
    SSpawnBlob blob;
    const bool isCoin = type == 'COIN', isBarrier = type == 'DBAR';
    const int modelProps = haveModel ? (isCoin ? 1 : isBarrier ? 6 : 0) : 0;
    blob.U16(1 + (patternedTypedef ? 1 : 0) + modelProps + wpscCount +
             static_cast< int >(props.size()));
    const size_t editor = blob.BeginProp(0x255a4580); // EditorProperties
    blob.U16(3);
    const size_t name = blob.BeginProp('INAM');
    const std::string label = std::string("spawn ") + typeName;
    for (char c : label) {
      blob.U8(static_cast< uint8_t >(c));
    }
    blob.U8(0);
    blob.EndProp(name);
    const size_t xfrm = blob.BeginProp('XFRM');
    blob.F32(pos.GetX());
    blob.F32(pos.GetY());
    blob.F32(pos.GetZ());
    blob.F32(0.f);
    blob.F32(0.f);
    blob.F32(yawDeg);
    blob.F32(1.f);
    blob.F32(1.f);
    blob.F32(1.f);
    blob.EndProp(xfrm);
    const size_t active = blob.BeginProp('ACTV');
    blob.U8(startActive ? 1 : 0);
    blob.EndProp(active);
    blob.EndProp(editor);
    if (patternedTypedef) {
      const size_t patterned = blob.BeginProp(0xb3774750); // PatternedAITypedef
      blob.U16((haveAncs ? 1 : 0) + (haveFsm ? 1 : 0) + (writeRules ? 1 : 0));
      if (haveAncs) {
        const size_t animSet = blob.BeginProp(0xe25fb08c); // animationInformation
        blob.U32(ancs);
        blob.U32(character);
        blob.U32(anim);
        blob.EndProp(animSet);
      }
      if (haveFsm) {
        const size_t machine = blob.BeginProp(0x55744160); // stateMachine
        blob.U32(fsm);
        blob.EndProp(machine);
      }
      if (writeRules) {
        const size_t knockback = blob.BeginProp(0x87011652); // knockbackRules
        blob.U32(rules);
        blob.EndProp(knockback);
      }
      blob.EndProp(patterned);
    }
    static const uint kBarrierModels[] = {0x014a0c36, 0x90f55c5d, 0xe1975355,
                                          0x396660b4, 0x48e25884, 0xf1abb2c7};
    for (int m = 0; m < modelProps; ++m) {
      const size_t prop = blob.BeginProp(isCoin ? 0xc27ffa8f : kBarrierModels[m]);
      blob.U32(model);
      blob.EndProp(prop);
    }
    // Before the raw props: the loader keeps the last value, so a `prop` naming one wins.
    for (int w = 0; w < wpscCount; ++w) {
      const size_t prop = blob.BeginProp(wpscProps[w]);
      blob.U32(wpsc);
      blob.EndProp(prop);
    }
    for (const std::pair< uint, uint >& p : props) {
      const size_t prop = blob.BeginProp(p.first);
      blob.U32(p.second);
      blob.EndProp(prop);
    }
    CMemoryInStream in(blob.bytes.data(), blob.bytes.size());
    CEntityInfo info(area, connections, startActive, editorId);
    CEntity* entity = loader(mgr, in, info);
    char line[160];
    if (entity == nullptr) {
      std::snprintf(line, sizeof(line),
                    "spawn %s refused: the loader returned null (a patterned type needs `ancs <id>`)",
                    typeName);
      std::printf("[console] %s\n", line);
      Fail(line);
      return;
    }
    mgr.AddObject(entity);
    mgr.DeliverScriptMsg(CScriptMsg(kInvalidUniqueId, entity->GetUniqueId(), kSM_AreaLoaded));
    std::snprintf(line, sizeof(line), "spawned %s u%04x e%08x %s at %.2f %.2f %.2f\n", typeName,
                  entity->GetUniqueId().value, editorId.Value(), ClassName(*entity).c_str(),
                  pos.GetX(), pos.GetY(), pos.GetZ());
    std::printf("[console] %s", line);
    out += line;
    if (haveAncs) {
      std::snprintf(line, sizeof(line), "   ancs %08x char %u anim %u\n", ancs, character, anim);
      out += line;
    }
    if (writeRules || wpscCount != 0) {
      std::snprintf(line, sizeof(line), "   knockback rules %08x, wpsc %08x on %d properties\n",
                    writeRules ? rules : kInvalidAssetId, wpscCount != 0 ? wpsc : kInvalidAssetId,
                    wpscCount);
      out += line;
    }
    // A patterned actor whose body controller is still off would crash in its first Think
    // (CPatterned::ThinkAboutMove reads the current body state). Placed actors get it from their
    // state machine (CChozoGhost::InActive) or their Registered handler; a spawn without the
    // type's own state machine asset has neither, so the console turns it on and says so.
    if (CPatterned* patterned = TCastToPtr< CPatterned >(entity)) {
      if (patterned->BodyController() != nullptr && !patterned->BodyController()->GetIsActive()) {
        patterned->BodyController()->Activate(mgr, pas::kAS_Invalid);
        const char* note = "   body controller was off after AreaLoaded: activated by spawn\n";
        std::printf("[console] %s", note + 3);
        out += note;
      }
    }
  }
}

// material <id> [+|-]<name|bit> ...: show an actor's material list, or add (+, the default) or
// remove (-) materials on it (CActor::AddMaterial/RemoveMaterial, which also re-sort the actor in
// the near list).  Names are the EMaterialTypes ones without kMT_, any case, or the bit number.
// Removing kMT_Solid from the player is what entering a turret does (CPlayer.cpp:4337), so a
// line-of-sight ray that ends at the player's aim point can come back clear.
const char* const kMaterialNames[] = {
    "nosteplogic",
    "stone",
    "metal",
    "grass",
    "ice",
    "pillar",
    "metalgrating",
    "phazon",
    "dirt",
    "lava",
    "lavastone",
    "snow",
    "mudslow",
    "halfpipe",
    "mud",
    "glass",
    "shield",
    "sand",
    "unknown18",
    "unknown19",
    "projectilepassthrough",
    "camerapassthrough",
    "wood",
    "organic",
    "noedgecollision",
    "unknown25",
    "seethrough",
    "scanpassthrough",
    "aipassthrough",
    "ceiling",
    "wall",
    "floor",
    "player",
    "character",
    "trigger",
    "projectile",
    "bomb",
    "groundcollider",
    "nostaticcollision",
    "scannable",
    "target",
    "orbit",
    "occluder",
    "immovable",
    "debris",
    "powerbomb",
    "unknown46",
    "collisionactor",
    "aiblock",
    "platform",
    "nonsoliddamageable",
    "radarobject",
    "platformslave",
    "aijoint",
    "unknown54",
    "solidcharacter",
    "excludefromlineofsighttest",
    "excludefromradar",
    "noplayercollision",
    "solid",
    "noplatformcollision",
    "unknown61",
    "screwattackwall",
    "seekertarget",
};

bool ParseMaterial(std::string_view text, EMaterialTypes& mat) {
  const std::string name = Lower(text);
  for (int i = 0; i < static_cast< int >(sizeof(kMaterialNames) / sizeof(kMaterialNames[0])); ++i) {
    if (name == kMaterialNames[i]) {
      mat = static_cast< EMaterialTypes >(i);
      return true;
    }
  }
  char* end = nullptr;
  const long bit = std::strtol(name.c_str(), &end, 0);
  if (name.empty() || end == nullptr || *end != '\0' || bit < 0 || bit > 63) {
    return false;
  }
  mat = static_cast< EMaterialTypes >(bit);
  return true;
}

void CmdMaterial(CStateManager& mgr, const Args& args, std::string& out) {
  CEntity* ent = args.size() >= 2 ? const_cast< CEntity* >(ResolveObject(mgr, args[1])) : nullptr;
  CActor* actor = ent != nullptr ? TCastToPtr< CActor >(ent) : nullptr;
  if (actor == nullptr) {
    Fail("usage: material <id> [+|-]<name|bit> ...   (id must be an actor)");
    return;
  }
  for (size_t i = 2; i < args.size(); ++i) {
    std::string_view arg = args[i];
    const bool remove = !arg.empty() && arg[0] == '-';
    if (!arg.empty() && (arg[0] == '-' || arg[0] == '+')) {
      arg.remove_prefix(1);
    }
    EMaterialTypes mat;
    if (!ParseMaterial(arg, mat)) {
      Fail("unknown material '" + Str(arg) + "'");
      return;
    }
    if (remove) {
      actor->RemoveMaterial(mat, mgr);
    } else {
      actor->AddMaterial(mat, mgr);
    }
  }
  out += "materials";
  for (int i = 0; i < 64; ++i) {
    if (actor->GetMaterialList().HasMaterial(static_cast< EMaterialTypes >(i))) {
      out += ' ';
      out += kMaterialNames[i];
    }
  }
  out += '\n';
}

// bodystate <id> <animation state index>: set a patterned actor's body controller state
// directly (the body's own state, e.g. 2 = kAS_LieOnGround, 19 = kAS_Cover, 20 =
// kAS_WallHang).  The state machine reaches only some of the body states, and the coverage
// runs need the others; the index is the pas::EAnimationState value.
void CmdBodyState(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() < 3) {
    Fail("usage: bodystate <id> <animation state index>");
    return;
  }
  CEntity* ent = const_cast< CEntity* >(ResolveObject(mgr, args[1]));
  CPatterned* patterned = ent != nullptr ? TCastToPtr< CPatterned >(ent) : nullptr;
  if (patterned == nullptr) {
    Fail("not a patterned actor");
    return;
  }
  CBodyController* body = patterned->BodyController();
  if (body == nullptr) {
    Fail("the actor has no body controller");
    return;
  }
  const int value = std::atoi(Str(args[2]).c_str());
  if (value < 0 || value > 28) { // 28 = kAS_AdditiveLoopReaction, the last one
    Fail("the animation state index must be 0..28");
    return;
  }
  body->BodyStateInfo().SetState(static_cast< pas::EAnimationState >(value));
  char line[64];
  std::snprintf(line, sizeof(line), "body state %d\n", value);
  out += line;
}

void CmdSend(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() != 3 && !(args.size() == 5 && args[3] == "from")) {
    Fail("usage: send <id> <msg> [from <id>]");
    return;
  }
  const CEntity* target = ResolveObject(mgr, args[1]);
  if (target == nullptr) {
    return;
  }
  // `from <id>`: the message's sender (default none), e.g. a live shot as a collision actor's
  // last toucher.
  TUniqueId sender = kInvalidUniqueId;
  if (args.size() == 5) {
    const CEntity* from = ResolveObject(mgr, args[4]);
    if (from == nullptr) {
      return;
    }
    sender = from->GetUniqueId();
  }
  const int msg = EnumValue(kMessageNames, kMessageCount, args[2]);
  if (msg < 0) {
    Fail("unknown message '" + Str(args[2]) + "'");
    return;
  }
  const uint editorId = target->GetEditorId().Value();
  const TUniqueId uid = target->GetUniqueId();
  if (msg == kSM_Delete) {
    // A bare Delete message resets the object's FSM but leaves it alive, and its next Think writes
    // past mSubMachines; script deletes go through the state manager's delete request.
    mgr.DeleteObjectRequest(uid);
    out += "delete requested\n";
    return;
  }
  mgr.DeliverScriptMsg(
      CScriptMsg(sender, uid, static_cast< EScriptObjectMessage >(msg)));
  char line[160];
  std::snprintf(line, sizeof(line), "sent %s to u%04x %08x\n",
                NameOrHex(kMessageNames, kMessageCount, static_cast< uint >(msg)).c_str(),
                uid.value, editorId);
  out += line;
}

// hurt <id> <amount> [weaponType]: put a CDamageInfo on an object through the same path a shot takes,
// so an enemy's FSM reacts to it (a state change, a flinch, a death). The weapon type is the raw
// EWeaponType the vulnerability tables use; 0 is the power beam. No damager object, so the damage
// takes the target's plain vulnerability and no directional bonus (CStateManager::ApplyDamage reads
// the weapon mode only to reject kWT_None).
void CmdHurt(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() < 3) {
    Fail("usage: hurt <id> <amount> [weaponType]");
    return;
  }
  const CEntity* ent = ResolveObject(mgr, args[1]);
  if (ent == nullptr) {
    return;
  }
  const float amount = static_cast< float >(std::atof(Str(args[2]).c_str()));
  if (!(amount > 0.f)) {
    Fail("amount must be > 0");
    return;
  }
  const int type = args.size() > 3 ? ArgToInt(args[3], 0) : 0;
  const TUniqueId uid = ent->GetUniqueId();
  mgr.ApplyDamage(kInvalidUniqueId, uid, kInvalidUniqueId,
                  CDamageInfo(CWeaponMode(static_cast< EWeaponType >(type)), amount, 0.f, 0.f),
                  CMaterialFilter::GetPassEverything(), CVector3f::Zero());
  char line[128];
  std::snprintf(line, sizeof(line), "hurt u%04x %.0f (weapon %d)\n", uid.value, amount, type);
  out += line;
  const CActor* actor = TCastToConstPtr< CActor >(ent);
  const CHealthInfo* health = actor != nullptr ? actor->GetHealthInfo() : nullptr;
  if (health != nullptr) {
    std::snprintf(line, sizeof(line), "  hp %.0f/%.0f\n", health->GetHP(),
                  health->GetInitialHP());
    out += line;
  }
}

void CmdVisor(CStateManager& mgr, const Args& args, std::string& out) {
  CPlayerState* state = mgr.PlayerState(0);
  if (state == nullptr) {
    Fail("no player state");
    return;
  }
  if (args.size() < 2) {
    const CPlayerState::EPlayerVisor current = state->GetCurrentVisor();
    char line[160];
    std::snprintf(line, sizeof(line), "visor %s (%d)  owned:", VisorName(current),
                  static_cast< int >(current));
    out += line;
    for (const CPlayerState::EPlayerVisor visor : kVisorOrder) {
      if (state->HasVisor(visor)) {
        out += ' ';
        out += VisorName(visor);
      }
    }
    out += '\n';
    return;
  }
  CPlayerState::EPlayerVisor visor = CPlayerState::kPV_Invalid;
  if (!ParseVisor(args[1], visor)) {
    Fail("unknown visor '" + Str(args[1]) + "'");
    return;
  }
  if (!state->HasVisor(visor)) {
    Fail("visor '" + Str(args[1]) + "' not owned");
    return;
  }
  // A completed scan soft-pauses Think, including the visor transition itself. The command
  // bypasses UpdateVisorState's "not scanning" gate, so end the scan before requesting a switch.
  // Release the console's held L too, or it can immediately start the same scan again.
  CPlayer* player = mgr.GetPlayer(0);
  if (player != nullptr && visor != CPlayerState::kPV_Scan &&
      state->GetCurrentVisor() == CPlayerState::kPV_Scan) {
    PortInput::Hold(0, 0, 0, 0, 0, 0);
    player->SetScanningState(CPlayer::kSS_NotScanning, mgr);
  }
  state->StartTransitionToVisor(visor);
  // What CPlayer::UpdateVisorState does after a switch: the scan visor holsters the gun.
  CPlayerGun* gun = player != nullptr ? player->GetPlayerGun() : nullptr;
  if (gun != nullptr) {
    if (visor == CPlayerState::kPV_Scan) {
      gun->HolsterGun(mgr);
    } else {
      gun->DrawGun(mgr);
    }
  }
  char line[64];
  std::snprintf(line, sizeof(line), "visor -> %s\n", VisorName(visor));
  out += line;
}

void CmdRadial(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() == 1) {
    char line[160];
    const PortRadial::Point p = PortRadial::Transport().Vector();
    std::snprintf(line, sizeof(line), "radial active=%d capture=%d vector=%.3f,%.3f open=%.3f,%.3f applied=%d,%d eligible beam=%d visor=%d\n",
                  PortRadial::Transport().Active(), PortRadial::Capturing(), p.x, p.y,
                  PortRadial::Transport().Open(PortRadial::kBeam),
                  PortRadial::Transport().Open(PortRadial::kVisor),
                  PortRadial::Applied(PortRadial::kBeam), PortRadial::Applied(PortRadial::kVisor), PortRadial::Eligible(PortRadial::kBeam),
                  PortRadial::Eligible(PortRadial::kVisor));
    out += line;
    return;
  }
  if (args[1] == "release") { PortRadial::Force(-1); return; }
  if (args[1] == "cancel") { PortRadial::Cancel(); return; }
  if (args[1] == "padoff") { PortRadial::SimulatePad(-1); return; }
  if (args[1] == "padrelease") { PortRadial::SimulatePad(2); return; }
  if (args[1] == "touch") {
    static uint64_t token = 0; // desktop synthetic UI, using the same any-thread mailbox as JNI
    if (args.size() == 3 && args[2] == "cancelall") {
      PortRadial::CancelAllTouches(); return;
    }
    if (args.size() == 5 && args[2] == "begin") {
      const int kind = args[3] == "beam" ? PortRadial::kBeam :
                       args[3] == "visor" ? PortRadial::kVisor : -1;
      if (kind < 0) { Fail("touch begin beam|visor <owner>"); return; }
      const int64_t result = PortRadial::BeginTouch(PortRadial::Kind(kind), ArgToInt(args[4], -1));
      if (result > 0) token = result;
      out += "native touch token=" + std::to_string(result) + " (0 legacy, -1 busy)\n";
      return;
    }
    if (args.size() == 4 && args[2] == "cancel") {
      PortRadial::CancelTouch(token, ArgToInt(args[3], -1)); return;
    }
    float x = 0.f, y = 0.f;
    if (args.size() == 6 && (args[2] == "move" || args[2] == "end") &&
        ParseFloat(args[4], x) && ParseFloat(args[5], y)) {
      const int owner = ArgToInt(args[3], -1);
      const bool accepted = args[2] == "move" ? PortRadial::MoveTouch(token, owner, x, y) :
                                               PortRadial::EndTouch(token, owner, x, y);
      out += accepted ? "native touch accepted\n" : "native touch ignored (owner/session revoked)\n";
      return;
    }
    Fail("radial touch begin beam|visor owner | move|end owner x y | cancel owner | cancelall");
    return;
  }
  if (!PortRadial::GameplayAllowed(mgr)) { Fail("radial unavailable outside enabled Trilogy gameplay"); return; }
  float x = 0.f, y = 0.f;
  if (args[1] == "pad") {
    if (args.size() != 5 || !ParseFloat(args[3], x) || !ParseFloat(args[4], y) ||
        (args[2] != "beam" && args[2] != "visor")) {
      Fail("radial pad beam|visor x y; bind pad:rightstick / pad:leftstick to held menus first"); return;
    }
    PortRadial::SimulatePad(args[2] == "beam" ? PortRadial::kBeam : PortRadial::kVisor, x, y);
    return;
  }
  if (args.size() != 4 || !ParseFloat(args[2], x) || !ParseFloat(args[3], y)) {
    Fail("radial beam|visor <x right> <y up> | mouse <dx> <dy> | release | cancel"); return;
  }
  if (args[1] == "mouse") { PortMouse::AddMotion(x, y); return; }
  const int kind = args[1] == "beam" ? PortRadial::kBeam :
                   args[1] == "visor" ? PortRadial::kVisor : -1;
  if (kind < 0) { Fail("unknown radial kind"); return; }
  PortRadial::Force(kind, x, y);
}

void CmdBeam(CStateManager& mgr, const Args& args, std::string& out) {
  CPlayerState* state = mgr.PlayerState(0);
  if (state == nullptr) {
    Fail("no player state");
    return;
  }
  if (args.size() < 2) {
    const CPlayerState::EBeamId current = state->GetCurrentBeam();
    const SBeamChoice* now = BeamChoice(current);
    char line[160];
    std::snprintf(line, sizeof(line), "beam %s (%d)  owned:", now != nullptr ? now->name : "?",
                  static_cast< int >(current));
    out += line;
    for (const SBeamChoice& choice : kBeamOrder) {
      if (state->HasPowerUp(choice.item)) {
        out += ' ';
        out += choice.name;
      }
    }
    out += '\n';
    return;
  }
  const SBeamChoice* choice = ParseBeam(args[1]);
  if (choice == nullptr) {
    Fail("unknown beam '" + Str(args[1]) + "'");
    return;
  }
  if (!state->HasPowerUp(choice->item)) {
    Fail("beam '" + Str(args[1]) + "' not owned");
    return;
  }
  // The game's own path: CPlayerGun::HandleWeaponChange reads the beam from the state.
  state->SetCurrentBeam(choice->beam);
  char line[64];
  std::snprintf(line, sizeof(line), "beam -> %s\n", choice->name);
  out += line;
}

// ray <x> <y> <z> [len] [dx dy dz]: cast from the point (straight down, or along the direction)
// and list every static collision surface the ray crosses (point, normal, material bits, hex),
// every area render surface it crosses (the geometry beam projectiles hit, with the surface's
// material mask and whether a beam's filter lets it through), then the first hit of the world
// ray (static plus actors) and the actor it belongs to. For collision bugs: what holds the
// player up, what stops a shot, and where.
void CmdRay(CStateManager& mgr, const Args& args, std::string& out) {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  float len = 100.f;
  float dx = 0.f;
  float dy = 0.f;
  float dz = -1.f;
  if (args.size() < 4 || !ParseFloat(args[1], x) || !ParseFloat(args[2], y) ||
      !ParseFloat(args[3], z) || (args.size() > 4 && !ParseFloat(args[4], len)) ||
      (args.size() > 5 && (args.size() < 8 || !ParseFloat(args[5], dx) ||
                           !ParseFloat(args[6], dy) || !ParseFloat(args[7], dz)))) {
    Fail("usage: ray <x> <y> <z> [len] [dx dy dz]");
    return;
  }
  const CVector3f origin(x, y, z);
  CVector3f dir(dx, dy, dz);
  if (!dir.CanBeNormalized()) {
    Fail("ray: zero direction");
    return;
  }
  dir = dir.AsNormalized();
  const CMaterialFilter filter = CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid));
  char line[192];
  CVector3f start = origin;
  float left = len;
  for (int i = 0; i < 16 && left > 0.f; ++i) {
    const CRayCastResult hit = CGameCollision::RayStaticIntersection(mgr, start, dir, left, filter);
    if (!hit.IsValid()) {
      break;
    }
    const CVector3f& pt = hit.GetPoint();
    const CVector3f n = hit.GetPlane().GetNormal();
    std::snprintf(line, sizeof(line), "static %.3f %.3f %.3f  normal %.2f %.2f %.2f  mat %016llx\n",
                  pt.GetX(), pt.GetY(), pt.GetZ(), n.GetX(), n.GetY(), n.GetZ(),
                  static_cast< unsigned long long >(hit.GetMaterial().GetValue()));
    out += line;
    start = pt + dir * 0.01f;
    left = len - CVector3f::Dot(start - origin, dir);
  }
  // The beam projectiles' filter (CGameProjectile's constructor, no extra exclude).
  const CMaterialFilter beamFilter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid, kMT_NonSolidDamageable),
      CMaterialList(kMT_Projectile, kMT_ProjectilePassthrough));
  start = origin;
  left = len;
  for (int i = 0; i < 16 && left > 0.f; ++i) {
    const CRayCastResult hit =
        RenderGeometryRayCast::RayWorldIntersection(mgr, start, dir, left, filter, nullptr);
    if (!hit.IsValid()) {
      break;
    }
    const CVector3f& pt = hit.GetPoint();
    std::snprintf(line, sizeof(line), "render %.3f %.3f %.3f  mat %016llx  beam %s\n", pt.GetX(),
                  pt.GetY(), pt.GetZ(),
                  static_cast< unsigned long long >(hit.GetMaterial().GetValue()),
                  beamFilter.Passes(hit.GetMaterial()) ? "stops" : "passes");
    out += line;
    start = pt + dir * 0.01f;
    left = len - CVector3f::Dot(start - origin, dir);
  }
  rstl::reserved_vector< TUniqueId, 1024 > nearList;
  mgr.BuildNearList(nearList, origin, dir, len, filter, nullptr);
  TUniqueId id = kInvalidUniqueId;
  const CRayCastResult hit = mgr.RayWorldIntersection(id, origin, dir, len, filter, nearList);
  if (hit.IsValid()) {
    const CEntity* ent = id == kInvalidUniqueId ? nullptr : mgr.GetObjectById(id);
    std::snprintf(line, sizeof(line), "world %.3f %.3f %.3f  id %04x %s (near %d)\n",
                  hit.GetPoint().GetX(), hit.GetPoint().GetY(), hit.GetPoint().GetZ(), id.Value(),
                  ent != nullptr ? ClassName(*ent).c_str() : "static", int(nearList.size()));
    out += line;
  } else {
    out += "world: no hit\n";
  }
}

void CmdTp(CStateManager& mgr, const Args& args, std::string& out) {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  if (args.size() < 4 || !ParseFloat(args[1], x) || !ParseFloat(args[2], y) ||
      !ParseFloat(args[3], z)) {
    Fail("usage: tp <x> <y> <z>");
    return;
  }
  CPlayer* player = mgr.GetPlayer(0);
  if (player == nullptr) {
    Fail("no player");
    return;
  }
  // Keep the facing, flattened onto the floor; a player looking straight up or down has no flat
  // heading, so it faces +y.
  const CVector3f pos(x, y, z);
  CVector3f dir = player->GetTransform().GetForward();
  dir.SetZ(0.f);
  if (dir.CanBeNormalized()) {
    dir.Normalize();
  } else {
    dir = CVector3f(0.f, 1.f, 0.f);
  }
  player->Teleport(CTransform4f::LookAt(pos, pos + dir, CVector3f::Up()), mgr, true);
  char line[64];
  std::snprintf(line, sizeof(line), "position %.2f %.2f %.2f\n", x, y, z);
  out += line;
}

// face <yaw deg>: turn the player where it stands, through the same Teleport the game uses for a
// respawn (which sets the player's look/move/control directions from the transform).
void CmdFace(CStateManager& mgr, const Args& args, std::string& out) {
  float yaw = 0.f;
  if (args.size() < 2 || !ParseFloat(args[1], yaw)) {
    Fail("usage: face <yaw deg>");
    return;
  }
  CPlayer* player = mgr.GetPlayer(0);
  if (player == nullptr) {
    Fail("no player");
    return;
  }
  const float radians = yaw * 3.14159265358979f / 180.f;
  // 0 = +y, 90 = +x (z is up), which is the order of the world's own axes.
  const CVector3f dir(std::sin(radians), std::cos(radians), 0.f);
  const CVector3f pos = player->GetTranslation();
  player->Teleport(CTransform4f::LookAt(pos, pos + dir, CVector3f::Up()), mgr, true);
  char line[64];
  std::snprintf(line, sizeof(line), "facing %.0f deg (%.2f, %.2f)\n", yaw, dir.GetX(), dir.GetY());
  out += line;
}

// aim <yaw_deg> <pitch_deg>: point the view where the run cannot turn it. Yaw reads as `face`
// does (0 = +y, 90 = +x) and a negative pitch looks down, which is how a level shot reaches a
// puddle on the floor. It turns mouse aim on for the rest of the run, since that is the source
// CFirstPersonCamera takes its pitch from; `aim` again re-places the view.
void CmdAim(CStateManager&, const Args& args, std::string& out) {
  float yaw = 0.f;
  float pitch = 0.f;
  if (args.size() != 3 || !ParseFloat(args[1], yaw) || !ParseFloat(args[2], pitch)) {
    Fail("usage: aim <yaw_deg> <pitch_deg>   (0 = +y, 90 = +x; negative pitch looks down)");
    return;
  }
  PortMouse::SetAimAngles(yaw, pitch);
  char line[96];
  std::snprintf(line, sizeof(line), "aim yaw %.0f pitch %.0f (yaw as `face`, pitch +up)\n",
                static_cast< double >(yaw), static_cast< double >(pitch));
  out += line;
}

// give adds capacity and fills it; take removes capacity, and the amount with it (which is how a
// suit upgrade is lost). This is CScriptPickup's own path (CScriptPickup.cpp:342-349).
void CmdGive(CStateManager& mgr, const Args& args, bool take, std::string& out) {
  if (args.size() < 2) {
    Fail(take ? "usage: take <item> [n]" : "usage: give <item> [n]");
    return;
  }
  CPlayerState* state = mgr.PlayerState(0);
  if (state == nullptr) {
    Fail("no player state");
    return;
  }
  const int item = EnumValue(kItemNames, kItemCount, args[1]);
  if (item < 0 || item >= CPlayerState::kIT_Max) {
    Fail("unknown item '" + Str(args[1]) + "'");
    return;
  }
  int amount = 1;
  if (args.size() > 2) {
    amount = ArgToInt(args[2], -1);
    if (amount < 1) {
      Fail("the amount is a positive number");
      return;
    }
  }
  const CPlayerState::EItemType type = static_cast< CPlayerState::EItemType >(item);
  if (take) {
    state->AddPowerUp(type, -amount);
  } else {
    state->AddPowerUp(type, amount);
    state->IncrPickUp(type, amount);
  }
  // A translator gate reads the inventory once, when its area loads; this has it read again.
  CScriptConditionalRelay::PortNoteInventoryCheat();
  const char* name = EnumName(kItemNames, kItemCount, static_cast< uint >(item));
  char line[128];
  std::snprintf(line, sizeof(line), "%s %s: %d/%d\n", take ? "took" : "gave",
                name != nullptr ? name : args[1].data(), state->GetItemAmount(type),
                state->GetItemCapacity(type));
  out += line;
}

void CmdGiveAdd(CStateManager& mgr, const Args& args, std::string& out) {
  CmdGive(mgr, args, false, out);
}

void CmdGiveTake(CStateManager& mgr, const Args& args, std::string& out) {
  CmdGive(mgr, args, true, out);
}

// press <a+b+...> [updates]: hold pad buttons on the virtual pad (the one MP2_INPUT writes), for
// `updates` game updates (default 8); 0 holds them until the next press or stick.
void CmdPress(CStateManager& mgr, const Args& args, std::string& out) {
  (void)mgr;
  if (args.size() < 2) {
    Fail("usage: press <a+b+...> [updates]   (0 holds until the next press/stick)");
    return;
  }
  u16 mask = 0;
  const std::string wanted = Str(args[1]);
  size_t begin = 0;
  while (begin <= wanted.size()) {
    const size_t plus = wanted.find('+', begin);
    const std::string name = Lower(wanted.substr(begin, plus == std::string::npos ? std::string::npos
                                                                                 : plus - begin));
    const PortInput::SButtonName* button = nullptr;
    for (const PortInput::SButtonName& candidate : PortInput::kButtonNames) {
      if (name == Lower(candidate.name)) {
        button = &candidate;
      }
    }
    if (button == nullptr) {
      Fail("unknown button '" + name + "'");
      return;
    }
    mask |= button->mask;
    if (plus == std::string::npos) {
      break;
    }
    begin = plus + 1;
  }
  const unsigned updates = args.size() > 2 ? static_cast< unsigned >(ArgToInt(args[2], 8)) : 8u;
  PortInput::Hold(mask, 0, 0, 0, 0, updates);
  char line[128];
  std::snprintf(line, sizeof(line), "holding %s for %u updates\n", wanted.c_str(), updates);
  out += line;
}

// grapple <id> [dist]: the lock-on a grapple needs, set up in one go. A lock-on press takes
// CPlayer's next orbit target, which comes from candidate lists rebuilt only every 20 updates, so a
// press right after a teleport finds no target and a held L never presses again; and a player put
// in the air falls out of the orbit zone in ~15 updates. This wakes the point (kSM_Activate, which
// also restores kMT_Orbit), puts the player dist units away with the eye level with the point,
// facing it (along the point's axis when it is axis-constrained), has the lists rebuilt on the next
// update and holds L from the update after that (`press` or `stick` ends the hold).
void CmdGrapple(CStateManager& mgr, const Args& args, std::string& out) {
  const CEntity* ent = args.size() >= 2 ? ResolveObject(mgr, args[1]) : nullptr;
  const CScriptGrapplePoint* point =
      ent != nullptr ? TCastToConstPtr< CScriptGrapplePoint >(ent) : nullptr;
  float dist = 8.f;
  if (point == nullptr || (args.size() > 2 && (!ParseFloat(args[2], dist) || !(dist > 2.f)))) {
    Fail("usage: grapple <grapple point id> [dist > 2]   (`objs grapple` lists them)");
    return;
  }
  CPlayer* player = mgr.GetPlayer(0);
  CPlayerState* state = mgr.PlayerState(0);
  if (player == nullptr || state == nullptr) {
    Fail("no player");
    return;
  }
  const bool woken = !point->GetActive();
  if (woken) {
    mgr.DeliverScriptMsg(CScriptMsg(kInvalidUniqueId, point->GetUniqueId(), kSM_Activate));
  }
  const CVector3f target = point->GetTranslation();
  CVector3f dir = player->GetTranslation() - target;
  if (point->GetGrappleParameters().GetConstrainToAxis()) {
    const CVector3f axis = point->GetTransform().GetForward();
    dir = CVector3f::Dot(axis, dir) < 0.f ? -axis : axis;
  }
  dir.SetZ(0.f);
  dir = dir.CanBeNormalized() ? dir.AsNormalized() : CVector3f(0.f, -1.f, 0.f);
  const float eyeHeight = player->GetEyePosition().GetZ() - player->GetTranslation().GetZ();
  CVector3f pos = target + dir * dist;
  pos.SetZ(target.GetZ() - eyeHeight);
  player->Teleport(CTransform4f::LookAt(pos, pos - dir, CVector3f::Up()), mgr, true);
  player->PortRefreshOrbitCandidates();
  PortInput::Hold(PAD_TRIGGER_L, 0, 0, 0, 0, 0, 2);
  char line[192];
  std::snprintf(line, sizeof(line),
                "grapple u%04x at (%.1f, %.1f, %.1f)%s: player at (%.1f, %.1f, %.1f), L held from "
                "2 updates on%s\n",
                point->GetUniqueId().value, target.GetX(), target.GetY(), target.GetZ(),
                woken ? " (woken)" : "", pos.GetX(), pos.GetY(), pos.GetZ(),
                state->HasPowerUp(CPlayerState::kIT_GrappleBeam) ? ""
                                                                 : "; no grapple beam (`give grappleBeam 1`)");
  out += line;
}

// lock <id> [d] [updates]: the real lock-on press on any actor (no forced orbit): puts the player d
// (10) units away with the eye level with the actor's orbit point, facing it, has the candidate
// lists rebuilt on the next update and holds L from the update after that (`orbit` shows the result).
void CmdLock(CStateManager& mgr, const Args& args, std::string& out) {
  const CEntity* ent = args.size() >= 2 ? ResolveObject(mgr, args[1]) : nullptr;
  const CActor* actor = ent != nullptr ? TCastToConstPtr< CActor >(ent) : nullptr;
  float dist = 10.f;
  if (actor == nullptr || (args.size() > 2 && (!ParseFloat(args[2], dist) || !(dist > 2.f)))) {
    Fail("usage: lock <actor id> [dist > 2] [updates]");
    return;
  }
  CPlayer* player = mgr.GetPlayer(0);
  if (player == nullptr) {
    Fail("no player");
    return;
  }
  const unsigned updates = args.size() > 3 ? static_cast< unsigned >(ArgToInt(args[3], 0)) : 0u;
  const CVector3f target = actor->GetOrbitPosition(mgr);
  CVector3f dir = player->GetTranslation() - target;
  dir.SetZ(0.f);
  dir = dir.CanBeNormalized() ? dir.AsNormalized() : CVector3f(0.f, -1.f, 0.f);
  const float eyeHeight = player->GetEyePosition().GetZ() - player->GetTranslation().GetZ();
  // The side the player came from may be behind a wall: take the first of 8 directions around the
  // actor (starting there) whose eye-level line to the orbit point is clear for the lock-on filter.
  const CMaterialFilter los = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid),
      CMaterialList(kMT_ProjectilePassthrough, kMT_ScanPassthrough, kMT_Character, kMT_Player));
  CVector3f pos = target + dir * dist;
  for (int i = 0; i < 8; ++i) {
    const float a = static_cast< float >(i) * (M_PIF / 4.f);
    const float c = std::cos(a);
    const float sn = std::sin(a);
    const CVector3f d(dir.GetX() * c - dir.GetY() * sn, dir.GetX() * sn + dir.GetY() * c, 0.f);
    const CVector3f p(target.GetX() + d.GetX() * dist, target.GetY() + d.GetY() * dist,
                      target.GetZ() - eyeHeight);
    if (mgr.RayCollideWorld(p + CVector3f(0.f, 0.f, eyeHeight), target, los, actor)) {
      dir = d;
      pos = p;
      break;
    }
  }
  pos.SetZ(target.GetZ() - eyeHeight);
  player->Teleport(CTransform4f::LookAt(pos, pos - dir, CVector3f::Up()), mgr, true);
  player->PortRefreshOrbitCandidates();
  PortInput::Hold(PAD_TRIGGER_L, 0, 0, 0, 0, updates, 2);
  char line[192];
  std::snprintf(line, sizeof(line),
                "lock u%04x orbit point (%.1f, %.1f, %.1f): player at (%.1f, %.1f, %.1f), L held "
                "from 2 updates on\n",
                actor->GetUniqueId().value, target.GetX(), target.GetY(), target.GetZ(), pos.GetX(),
                pos.GetY(), pos.GetZ());
  out += line;
}

// scan <id> [d] [updates]: a scan of an actor without aiming. The orbit candidate lists rarely
// offer a big actor whose orbit point is far above the player, so this puts the player d (40)
// units from the actor on its level, facing it, locks the orbit on it (CPlayer::PortForceOrbit:
// what a lock-on press does) and holds L (lock-on and Scan Item) for `updates` (the scan's
// download time plus 2 s). It takes two calls ~30 updates apart: the first raises the scan visor
// and starts holding L, because the update that sees L go down runs UpdateOrbitSelection, which
// would move a fresh lock to the player's next orbit candidate. A completed scan soft-pauses the game and the scan display renders the
// actor (CScanDisplay::PrepareScanDisplay and Draw: GetScanVisorRenderBounds / ScanVisorRender
// for a self-rendering CPatterned). `scan` alone prints the orbit target, scanning time and the
// game state (1 = soft-paused: the scan display is up).
void CmdScan(CStateManager& mgr, const Args& args, std::string& out) {
  CPlayer* player = mgr.GetPlayer(0);
  CPlayerState* state = mgr.PlayerState(0);
  if (player == nullptr || state == nullptr) {
    Fail("no player");
    return;
  }
  char line[256];
  if (args.size() < 2) {
    std::snprintf(line, sizeof(line),
                  "scan: visor %s, orbit state %d target u%04x, scanning time %.2f, game state %d\n",
                  VisorName(state->GetCurrentVisor()), static_cast< int >(player->GetOrbitState()),
                  player->GetOrbitTargetId().value, player->GetScanningTime(),
                  static_cast< int >(mgr.GetGameState()));
    out += line;
    return;
  }
  const CEntity* ent = ResolveObject(mgr, args[1]);
  const CActor* actor = ent != nullptr ? TCastToConstPtr< CActor >(ent) : nullptr;
  float dist = 40.f;
  if (actor == nullptr || (args.size() > 2 && (!ParseFloat(args[2], dist) || !(dist > 1.f)))) {
    Fail("usage: scan <actor id> [dist > 1] [updates]   (`scan` alone: progress)");
    return;
  }
  if (!state->HasVisor(CPlayerState::kPV_Scan)) {
    Fail("scan visor not owned (`give scanVisor 1`)");
    return;
  }
  static TUniqueId sArmed = kInvalidUniqueId;
  if (state->GetCurrentVisor() != CPlayerState::kPV_Scan || sArmed != actor->GetUniqueId()) {
    if (state->GetCurrentVisor() != CPlayerState::kPV_Scan) {
      state->StartTransitionToVisor(CPlayerState::kPV_Scan);
      if (CPlayerGun* gun = player->GetPlayerGun()) {
        gun->HolsterGun(mgr);
      }
    }
    sArmed = actor->GetUniqueId();
    PortInput::Hold(PAD_TRIGGER_L, 0, 0, 0, 0, 0);
    out += "scan visor up, L held: repeat the command in ~30 updates\n";
    return;
  }
  sArmed = kInvalidUniqueId;
  const CScannableObjectInfo* info = actor->GetScannableObjectInfo();
  const float download = info != nullptr ? info->GetTotalDownloadTime() : 0.f;
  const unsigned updates = args.size() > 3 ? static_cast< unsigned >(ArgToInt(args[3], 0))
                                           : static_cast< unsigned >(download * 60.f) + 120u;
  const CVector3f target = actor->GetOrbitPosition(mgr);
  CVector3f dir = player->GetTranslation() - actor->GetTranslation();
  dir.SetZ(0.f);
  dir = dir.CanBeNormalized() ? dir.AsNormalized() : CVector3f(0.f, -1.f, 0.f);
  CVector3f pos = actor->GetTranslation() + dir * dist;
  pos.SetZ(actor->GetTranslation().GetZ());
  player->Teleport(CTransform4f::LookAt(pos, pos - dir, CVector3f::Up()), mgr, true);
  player->PortForceOrbit(actor->GetUniqueId(), mgr);
  PortInput::Hold(PAD_TRIGGER_L, 0, 0, 0, 0, updates);
  std::snprintf(line, sizeof(line),
                "scan u%04x (orbit point %.1f, %.1f, %.1f, visor flags %x, download %.1f s)%s%s: "
                "player at (%.1f, %.1f, %.1f), orbit locked, L held for %u updates\n",
                actor->GetUniqueId().value, target.GetX(), target.GetY(), target.GetZ(),
                actor->GetTargetableVisorFlags(), download, info != nullptr ? "" : "; no scan info",
                actor->GetMaterialList().HasMaterial(kMT_Scannable) ? "" : "; not kMT_Scannable",
                pos.GetX(), pos.GetY(), pos.GetZ(), updates);
  out += line;
}

// stick <x> <y> [updates]: hold the main stick, -127..127 (0 holds until the next press/stick).
// covdump: a coverage build (MP_PORT_COVERAGE) writes its gcov counters now and zeroes them; the
// next dump (another covdump, or the exit) adds to the .gcda files. A run that hangs in shutdown
// and is killed keeps what was dumped. Other builds answer that there is nothing to write.
void CmdCovdump(CStateManager&, const Args&, std::string& out) {
#ifdef MP_PORT_COVERAGE
  PortCoverageFlush();
  out += "gcov counters written\n";
#else
  out += "not a coverage build\n";
#endif
}

void CmdStick(CStateManager& mgr, const Args& args, std::string& out) {
  (void)mgr;
  if (args.size() < 3) {
    Fail("usage: stick <x> <y> [updates]   (-127..127; 0 updates holds until the next press/stick)");
    return;
  }
  const int x = ArgToInt(args[1], 1000);
  const int y = ArgToInt(args[2], 1000);
  if (x < -127 || x > 127 || y < -127 || y > 127) {
    Fail("the stick is -127..127 on each axis");
    return;
  }
  const unsigned updates = args.size() > 3 ? static_cast< unsigned >(ArgToInt(args[3], 8)) : 8u;
  PortInput::Hold(0, static_cast< s8 >(x), static_cast< s8 >(y), 0, 0, updates);
  char line[96];
  std::snprintf(line, sizeof(line), "holding stick %d,%d for %u updates\n", x, y, updates);
  out += line;
}

// cstick <x> <y> [updates]: hold the C-stick, -127..127 (0 holds until the next press/stick).
// Spring Ball reads C-stick up, so this is the one way to trigger it without a pad.
void CmdCstick(CStateManager& mgr, const Args& args, std::string& out) {
  (void)mgr;
  if (args.size() < 3) {
    Fail("usage: cstick <x> <y> [updates]   (-127..127; 0 updates holds until the next press/stick)");
    return;
  }
  const int x = ArgToInt(args[1], 1000);
  const int y = ArgToInt(args[2], 1000);
  if (x < -127 || x > 127 || y < -127 || y > 127) {
    Fail("the C-stick is -127..127 on each axis");
    return;
  }
  const unsigned updates = args.size() > 3 ? static_cast< unsigned >(ArgToInt(args[3], 8)) : 8u;
  PortInput::Hold(0, 0, 0, static_cast< s8 >(x), static_cast< s8 >(y), updates);
  char line[96];
  std::snprintf(line, sizeof(line), "holding C-stick %d,%d for %u updates\n", x, y, updates);
  out += line;
}

// The rest of the line from `first`, joined with spaces: a card path can hold one (`USA/Card A`).
std::string RestOf(const Args& args, size_t first) {
  std::string text;
  for (size_t i = first; i < args.size(); ++i) {
    if (i > first) {
      text += ' ';
    }
    text += Str(args[i]);
  }
  return text;
}

// gci restore [n]: another release's saves beside this card (platform/port_save.cpp). Without an
// index, lists them; with one, copies that release's newest save onto the card, converted.
void CmdGci(CStateManager&, const Args& args, std::string& out) {
  if (args.size() < 2 || Lower(args[1]) != "restore" || args.size() > 3) {
    Fail("usage: gci restore [n]");
    return;
  }
  const std::vector< PortSave::OtherRelease > others = PortSave::OtherReleaseSaves();
  if (args.size() == 2) {
    for (size_t i = 0; i < others.size(); ++i) {
      out += std::to_string(i) + ": " + others[i].label + " (" + others[i].folder + ")\n";
    }
    out += others.empty() ? "no other release's saves found\n" : "gci restore <n> copies one in\n";
    return;
  }
  const size_t i = std::strtoul(std::string(args[2]).c_str(), nullptr, 10);
  if (i >= others.size()) {
    Fail("no such entry");
    return;
  }
  std::string message;
  if (!PortSave::RestoreFrom(others[i], message)) {
    Fail(message);
    return;
  }
  out += others[i].label + ": " + message + '\n';
}

// save info [<file>] | export [<path>] | import <path|latest> | backup | backups: the card's save
// as the file it is (platform/port_save.cpp). `info` reads a .gci without mounting the card, so a
// backup can be checked while the game runs; with no file it reads the card's own.
void CmdSave(CStateManager&, const Args& args, std::string& out) {
  const std::string sub = args.size() < 2 ? std::string("info") : Lower(args[1]);
  // `save <file>` is `save info <file>`: an argument that names no subcommand is a path to read,
  // which is what the help line says (`save [info] [<file>]`).
  const bool known =
      sub == "export" || sub == "import" || sub == "backup" || sub == "backups";
  if (!known) {
    const size_t first = sub == "info" ? 2 : 1;
    std::string path;
    if (args.size() > first) {
      path = RestOf(args, first);
    } else {
      const std::vector< std::string > files = PortSave::Files();
      if (files.empty()) {
        Fail("the card has no save file");
        return;
      }
      path = files.front();
    }
    PortSave::File file;
    std::string error;
    if (!PortSave::Inspect(path, file, error)) {
      Fail(error);
      return;
    }
    out += PortSave::IsImage() ? "card image: " + PortSave::ImagePath()
                               : "card: " + PortSave::Directory();
    out += '\n';
    out += "file: " + file.path + " (" + file.note + "; the header points at slot " +
           std::to_string(file.saveIdx) + ")\n";
    char line[224];
    for (int i = 0; i < 3; ++i) {
      const PortSave::Slot& slot = file.slots[i];
      if (!slot.present) {
        std::snprintf(line, sizeof(line), "slot %d: empty\n", i);
        out += line;
        continue;
      }
      const int seconds = static_cast< int >(slot.playTime);
      std::snprintf(line, sizeof(line),
                    "slot %d: world %08x, %.0f%% items, %.0f%% scans, %02d:%02d:%02d, %.0f energy, "
                    "%u tanks%s\n",
                    i, slot.worldId, static_cast< double >(slot.itemPercent),
                    static_cast< double >(slot.scanPercent), seconds / 3600, (seconds / 60) % 60,
                    seconds % 60, static_cast< double >(slot.health), slot.energyTanks,
                    slot.hardMode ? ", hard" : "");
      out += line;
    }
    return;
  }
  if (sub == "export") {
    std::string message;
    if (!PortSave::Export(args.size() >= 3 ? RestOf(args, 2) : std::string(), message)) {
      Fail(message);
      return;
    }
    out += message;
    out += '\n';
    return;
  }
  if (sub == "import") {
    if (args.size() < 3) {
      Fail("usage: save import <path|latest>");
      return;
    }
    std::string message;
    if (!PortSave::Import(RestOf(args, 2), message)) {
      Fail(message);
      return;
    }
    out += message;
    out += '\n';
    return;
  }
  if (sub == "backup") {
    std::string written;
    std::string error;
    if (!PortSave::Backup(&written, error)) {
      Fail(error);
      return;
    }
    out += "card backup: " + written + "\n";
    return;
  }
  if (sub == "backups") {
    const std::vector< std::string > backups = PortSave::Backups();
    if (backups.empty()) {
      out += "no card backups\n";
      const std::string dir = PortSave::BackupsDirectory();
      out += dir.empty() ? "(no user directory: nothing can be kept)\n" : dir + "\n";
      return;
    }
    for (const std::string& path : backups) {
      out += path;
      out += '\n';
    }
    return;
  }
  Fail("save <info|export|import|backup|backups> ...");
}

// screenshot: the F12 capture. Aurora writes it under the working directory and logs the exact path.
void CmdScreenshot(CStateManager&, const Args&, std::string& out) {
  aurora::request_screenshot();
  out +=
      "screenshot requested: the log's `Screenshot saved to screenshots/metroid-prime-<stamp>.bmp`\n"
      "names the file (relative to the run's working directory)\n";
}

// overlay <page|off>: open the F1 window on a page (a name prefix: `overlay save`), for screenshots.
void CmdOverlay(CStateManager&, const Args& args, std::string& out) {
  if (args.size() < 2 || !PortOverlay::ShowPage(std::string(args[1]))) {
    Fail("usage: overlay <page name prefix>[/<sub-tab prefix>] | off");
    return;
  }
  out += args[1] == "off" ? std::string("overlay closed\n") : "overlay open on " + std::string(args[1]) + "\n";
}

// state save|load <slot> [hold]: the F5/F9 save states (slot 0 is the undo slot, load only). `hold`
// stops the simulation on the tick the load's room state is restored.
void CmdState(CStateManager&, const Args& args, std::string& out) {
  const int slot = args.size() < 3 ? -1 : ArgToInt(args[2], -1);
  const bool save = args.size() >= 2 && args[1] == "save";
  const bool load = args.size() >= 2 && args[1] == "load";
  if ((!save && !load) || slot < (save ? 1 : PortSaveState::kUndoSlot) ||
      slot > PortSaveState::kSlotCount) {
    Fail("usage: state save <1..8> | state load <0..8> [hold]");
    return;
  }
  if (save) {
    PortSaveState::RequestSave(slot);
  } else {
    if (args.size() > 3 && args[3] == "hold") {
      PortSaveState::HoldAfterLoad();
    }
    PortSaveState::RequestLoad(slot);
  }
  out += std::string(save ? "save" : "load") + " of slot " + std::to_string(slot) +
         " requested (the log's [savestate] lines say how it went)\n";
}

// statedump <file>: every live object's state as JSON lines (tools/statecheck.py).
void CmdStateDump(CStateManager& mgr, const Args& args, std::string& out) {
  if (args.size() < 2) {
    Fail("usage: statedump <file>");
    return;
  }
  std::string error;
  const int count = PortStateDump::Write(mgr, std::string(args[1]), error);
  if (count < 0) {
    Fail(error);
    return;
  }
  out += std::to_string(count) + " objects written to " + std::string(args[1]) + "\n";
}

// reloadmods: read the mods folder again and rebuild the room (the F1 Mods page's button).
void CmdReloadMods(CStateManager&, const Args&, std::string& out) {
  PortSaveState::RequestModReload();
  out += "mods reload requested (the log's [savestate] and mods: lines say how it went)\n";
}

// reloadroom: rebuild the room from the disc, Samus kept where she stands (F1 Cheats' Reload).
void CmdReloadRoom(CStateManager&, const Args&, std::string& out) {
  PortSaveState::RequestRoomReload();
  out += "room reload requested (the log's [savestate] lines say how it went)\n";
}

// resume [status|save|load|clear]: quick resume (savestates/resume.mp2ss), the same one the quit
// path and the splash-screen prompt use.
void CmdResume(CStateManager&, const Args& args, std::string& out) {
  const std::string_view action = args.size() > 1 ? args[1] : std::string_view("status");
  if (action == "status") {
    out += PortSaveState::ResumeStatus() + "\n";
  } else if (action == "save") {
    PortSaveState::RequestResumeSnapshot();
    out += "snapshot requested (the log's [savestate] lines say how it went)\n";
  } else if (action == "load") {
    PortSaveState::RequestResumeLoad();
    out += "load of the resume state requested (see the [savestate] lines)\n";
  } else if (action == "clear") {
    PortSaveState::ResumeClear();
    out += "resume state deleted\n";
  } else {
    Fail("usage: resume [status|save|load|clear]");
  }
}

void CmdItems(CStateManager& mgr, const Args&, std::string& out) {
  const CPlayerState* state = mgr.GetPlayerState(0);
  if (state == nullptr) {
    Fail("no player state");
    return;
  }
  char line[96];
  for (int i = 0; i < CPlayerState::kIT_Max; ++i) {
    const CPlayerState::EItemType type = static_cast< CPlayerState::EItemType >(i);
    const int capacity = state->GetItemCapacity(type);
    if (capacity <= 0) {
      continue;
    }
    const char* name = EnumName(kItemNames, kItemCount, static_cast< uint >(i));
    const std::string label = name != nullptr ? name : std::to_string(i);
    std::snprintf(line, sizeof(line), "%3d %-26s %d/%d\n", i, label.c_str(),
                  state->GetItemAmount(type), capacity);
    out += line;
  }
}

// gpuselftest: Video > Compatibility > GPU self-test (PortVideo::RunGpuSelfTestIfRequested).
void CmdGpuSelfTest(CStateManager&, const Args&, std::string& out) {
  PortVideo::RequestGpuSelfTest();
  out += "gpu self-test requested (results in the log)\n";
}

void CmdTitle(CStateManager& mgr, const Args&, std::string& out) {
  if (gpMain == nullptr) {
    Fail("not running");
    return;
  }
  // The pause menu's Quit: the restart mode stays kRM_Default, so CMainFlow goes back to the front end.
  gpMain->SetRestartMode(CMain::kRM_Default);
  mgr.QuitGame();
  out += "quitting to the title\n";
}

// pipetrim [frames]: releases the built pipelines unused for the last `frames` (16, the minimum)
// frames at the next frame start, as Android's onTrimMemory does (MP1 cb79569ac). Prints the count
// before; Aurora's "pipeline cache: evicted N" log line has the result.
void CmdPipeTrim(CStateManager&, const Args& args, std::string& out) {
  const uint32_t frames = args.size() > 1 ? static_cast< uint32_t >(std::max(ArgToInt(args[1], 16), 0)) : 16;
  out += "pipelines built: " + std::to_string(aurora_get_pipeline_count()) + "; evicting those unused for " +
         std::to_string(std::max< uint32_t >(frames, 16)) + " frames at the next frame (see the log)\n";
  aurora_trim_pipelines(frames);
}

void CmdWindow(CStateManager&, const Args& args, std::string& out) {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  SDL_Window* window = windows != nullptr && count > 0 ? windows[0] : nullptr;
  SDL_free(windows);
  if (window == nullptr) {
    Fail("no window");
    return;
  }
  if (args.size() > 2) {
    const int w = ArgToInt(args[1], 0);
    const int h = ArgToInt(args[2], 0);
    if (w < 64 || h < 64) {
      Fail("usage: window [<width> <height>]");
      return;
    }
    SDL_SetWindowFullscreen(window, false);
    SDL_SetWindowSize(window, w, h);
    SDL_SyncWindow(window);
  }
  int w = 0;
  int h = 0;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  out += "window " + std::to_string(w) + "x" + std::to_string(h) + "\n";
}

struct CommandSpec {
  std::string_view name;
  void (*run)(CStateManager&, const Args&, std::string&);
  const char* help; // the `help` text for this row ("" = covered by an earlier row)
};

// Every command, with its `help` lines: one table feeds the dispatch and `help`, and
// tests/port_console_docs.py checks it against docs/CONSOLE.md. A command's first row carries the
// handler; a row with an empty name is help text only.
const CommandSpec kCommands[] = {
    {"status", CmdStatus,
      "status [--json]            world, area, player position, health, visor, beam, morph state; --json: one line for scripts\n"},
    {"areas", CmdAreas,
      "areas                       the current world's area slots (index, MREA id, name if loaded)\n"},
    {"warp", CmdWarp,
      "warp <slot|id|name>         load an area of the current world (what MP2_TELEPORT takes)\n"},
    {"nextarea", CmdNextArea,
      "nextarea | prevarea         the next or previous existing area of this world\n"},
    {"prevarea", CmdPrevArea, ""},
    {"door", CmdDoor,
      "door | dock | station       stand in front of the nearest door / doorway / save station\n"},
    {"dock", CmdDock, ""},
    {"dimension", CmdDimension,
      "dimension                   go to this room's Light/Dark Aether twin, keeping the position\n"},
    {"station", CmdStation, ""},
    {"heal", CmdHeal,
      "heal | kill [id|target]     refill energy; kill the player (a respawn test), an actor or the lock-on\n"},
    {"kill", CmdKill, ""},
    {"refill", CmdRefill,
      "refill [energy|ammo]        energy to full / missiles, power bombs and beam ammo to capacity\n"},
    {"damage", CmdDamage,
      "damage <id> <n> [kb] [w]    hit an actor for n as a player shot (w = power, missile,\n"
      "                            cannonball, dark, light, annihilator, powerbomb, bomb,\n"
      "                            boostball, screwattack, darkburst,\n"
      "                            sunburst, sonicboom, supermissile; kb = knockback power)\n"},
    {"layer", CmdLayer,
      "layer <n>                   activate a layer of the current area\n"},
    {"settled", CmdSettled,
      "settled                     busy 0|1 (a warp/layer request in flight), free_ids, frame\n"},
    {"god", CmdGod,
      "god <0|1>                   god mode (the F1 cheat)\n"},
    {"nodarkair", CmdNoDarkAether,
      "nodarkair <0|1>             no Dark Aether air damage (the F1 cheat)\n"},
    {"infammo", CmdInfiniteAmmo,
      "infammo [kind] [0|1]        infinite ammo (F1 Cheats); kind all (default), missile,\n"
      "                            powerbomb, dark or light\n"},
    {"mpforce", CmdMpForce,
      "mpforce <0|1>               IsMultiplayer reports true (multiplayer-only arms)\n"},
    {"overlay", CmdOverlay,
      "overlay <page>[/<sub>]|off  open the F1 window on a page (name prefixes: `overlay video/qual`)\n"},
    {"gpuselftest", CmdGpuSelfTest,
      "gpuselftest                 run the GPU self-test next frame; PASS/FAIL lines go to the log\n"},
    {"statedump", CmdStateDump,
      "statedump <file>            every live object's state as JSON lines (tools/statecheck.py)\n"},
    {"state", CmdState,
      "state save <1..8>|load <0..8> [hold]  the F5/F9 save states (load 0 = undo the last load),\n"
      "                            hold stops the simulation once the load has restored\n"},
    {"reloadmods", CmdReloadMods,
      "reloadmods                  read the mods folder again and rebuild the room (F1 Mods)\n"},
    {"reloadroom", CmdReloadRoom,
      "reloadroom                  rebuild the room from the disc, Samus kept in place (F1 Cheats)\n"},
    {"resume", CmdResume,
      "resume [status|save|load|clear] quick resume (savestates/resume.mp2ss): show, snapshot now, load, delete\n"},
    {"credits", CmdCredits,
      "credits [1|2]               end the game into the credits (<75% / <100% route)\n"},
    {"skipcine", CmdSkipCine,
      "skipcine <0|1>              skip cutscenes the player has not seen (the F1 setting)\n"},
    {"sfx", CmdSfx,
      "sfx                         CSfxManager channel, mMuted, live sounds per channel, MusyX voices\n"},
    {"skipprompt", CmdSkipPrompt,
      "skipprompt                  show the cutscene skip prompt for a few seconds\n"},
    {"gunoffset", CmdGunOffset,
      "gunoffset [<r> <f> <u>]     arm cannon offset in metres (+-1), as the F1 Render row does\n"},
    {"msaa", CmdMsaaAniso,
      "msaa <1|4> | aniso <1..16>  anti-aliasing and anisotropic filtering (applied next frame)\n"},
    {"aniso", CmdMsaaAniso, ""},
    {"scanspread", CmdScanSpread,
      "scanspread [0..100]         how far the scan visor side panels spread (0 = 4:3 place)\n"},
    {"hudscale", CmdHudScale,
      "hudscale <50..100>          HUD scale in percent, as the F1 HUD row does\n"},
    {"hudopacity", CmdHudOpacity,
      "hudopacity <echo|dark|scan> <visor|0..100>  that visor HUD's opacity, or Visor Opacity's\n"},
    {"crosshair", CmdCrosshair,
      "crosshair [<25..100>]       aim crosshair size, `style classic|remastered|gamecube`,\n"
      "                            opacity <0..100>, color <r> <g> <b>|clear, aim <0|1> (F1 HUD)\n"},
    {"uiscale", CmdUiScale,
      "uiscale <50..200>           the F1 window's size in percent, as the Render row does\n"},
    {"vsync", CmdVsync,
      "vsync <0|1>                 vertical sync, as the F1 Render row does\n"},
    {"interp", CmdInterp,
      "interp [0|1]                smooth frames: the camera between ticks with the cap off\n"},
    {"present", CmdPresent,
      "present [<0..1|cycle|tick|off>]  the presentation factor the camera is drawn at, forced even\n"
      "                            with the cap on (also MP2_PRESENT_T); tick draws the plain tick state\n"},
    {"hold", CmdHold,
      "hold [0|1]                  stop the simulation: no tick runs until `step` runs some\n"},
    {"step", CmdStep,
      "step [1..600]               while held, run that many ticks, one per drawn frame (default 1)\n"},
    {"hudspread", CmdHudSpread,
      "hudspread [16:9|21:9|full]  widest screen the HUD spreads to, as the F1 Video row does\n"},
    {"aspect", CmdAspect,
      "aspect [4:3|16:9|window]    aspect ratio, as the F1 Video > Display row does\n"},
    {"boost", CmdBoost,
      "boost                       boost-ball release on the morphed player\n"},
    {"noise", CmdNoise,
      "noise <type> [x y z]        InformListeners at the player (6 = safe-zone noise)\n"},
    {"gameevent", CmdGameEvent,
      "gameevent <ev> [src] [tgt] [value] [as <proxyType>]  deliver a game event to every\n"
      "                            CScriptPlayerProxy listener (ev: score kill generic scan\n"
      "                            damage stop spawn; value: float for damage, state for generic)\n"},
    {"fastmorph", CmdFastMorph,
      "fastmorph <0|1>             fast morph: the morph transition takes 0.2 s, not a second\n"},
    {"finger", CmdFinger,
      "finger down|move|up <id> <x> <y> | tap <x> <y> | drag <x0> <y0> <x1> <y1> [steps]  SDL touch\n"
      "                           events at window pixels (the touch overlay's input; MP_TOUCH_OVERLAY_DESKTOP)\n"},
    {"pointer", CmdPointer,
      "pointer <x> <y> move|click|dclick|tap|dtap|down|up|drag <x1> <y1>|wheel <n>|pinch <r> | back | where | hits\n"
      "                            scripted mouse/touch input for the pointer menus (GUI pixels)\n"},
    {"minimap", CmdMinimap, "minimap                     the HUD minimap's tap rect (GUI pixels)\n"},
    {"maptap", CmdMapTap, "maptap                      tap the minimap: opens the map screen\n"},
    {"hintshow", CmdHintShow,
      "hintshow                    show the hint the game would show next (F1 Show current hint)\n"},
    {"memo", CmdMemo, "memo <0|1> <text...>        show a HUD memo; 1 = hint memo (framed, button ring)\n"
      "memo pause <strg>           show a string table as the boxed memo a paused HUDMemo opens\n"},
    {"walkspeed", CmdWalkSpeed,
      "walkspeed [50..200]         on-foot movement speed in percent (walk_speed); no argument prints it\n"},
    {"scanspeed", CmdScanSpeed,
      "scanspeed [50..400]         scan speed in percent (scan_speed); no argument prints it\n"},
    {"language", CmdLanguage,
      "language [0..6]             text language, applied live (language); no argument prints it\n"},
    {"combocost", CmdComboCost,
      "combocost [0..30]           beam ammo a combo takes (combo_beam_cost); no argument prints it\n"},
    {"combomissiles", CmdComboMissiles,
      "combomissiles [0..5]        missiles a combo takes (combo_missile_cost); no argument prints it\n"},
    {"beamammo", CmdBeamAmmo,
      "beamammo [50..100]          Dark/Light ammo per capacity pickup (beam_ammo_per_expansion); no argument prints it\n"},
    {"rooms", CmdRooms,
      "rooms [original|fast]      the energy controller rooms' turning-room cutscene (rotating_rooms); no argument prints it\n"},
    {"portal", CmdPortal,
      "portal [original|fast|skip] the light/dark world portal ride (portal_transition); no argument prints it\n"},
    {"elevator", CmdElevator,
      "elevator [original|fast|skip] the ride between worlds (elevator_ride); no argument prints it\n"},
    {"original", CmdOriginal,
      "original [0|1]             Original experience: the port's tweaks read as retail (original_experience)\n"},
    {"igt", CmdIgt,
      "igt [0|1]                  on-screen in-game time (igt_timer)\n"},
    {"springball", CmdSpringBall,
      "springball <0|1>            spring ball: C-stick up in Morph Ball bomb-jumps (needs Bombs)\n"},
    {"lockon", CmdLockOn,
      "lockon <0|1>                toggle lock-on: an L press latches until the next press\n"},
    {"sticky", CmdSticky,
      "sticky <0|1>                sticky charge: a long A hold keeps charging after the release\n"},
    {"pointeraim", CmdPointerAim,
      "pointeraim [on|off|<x> <y>] pointer aim (the F1 row), or place its pointer (-1..1)\n"},
    {"gci", CmdGci,
      "gci restore [n]            list or copy in the other release's saves, converted for this disc\n"},
    {"save", CmdSave,
      "save [info] [<file>]        the card's save slots, or one .gci on disk: world, items, time\n"},
    {"cardbackup", CmdCardBackup,
      "cardbackup <0|1>            back the card up before each write (the F1 Saves row)\n"},
    {"settings", CmdSettings,
      "settings [<key> <value>|self-test]  settings.cfg's entries; set one (and save), or check every key round-trips\n"},
    {"logfile", CmdLogFile,
      "logfile <0|1>               stdout and stderr also go to metroid_prime2_port.log (next launch)\n"},
    {"objs", CmdObjs,
      "objs [filter]               objects of the world: uid, editor id, class, area, position\n"},
    {"obj", CmdObj,
      "obj <id>                    one object (an id as `objs` prints it: u046e, or e<editor id>): health, fsm, messages, door beams\n"},
    {"send", CmdSend,
      "send <id> <msg> [from <id>] deliver a script message to an object (name or number), from a sender\n"},
    {"covdump", CmdCovdump,
      "covdump                     coverage build: write the gcov counters now (and zero them)\n"},
    {"hurt", CmdHurt,
      "hurt <id> <amount> [weapon] damage an object as a shot would (weapon 0 = power beam)\n"},
    {"frame", CmdFrame,
      "frame                       the game updates run so far (what MP2_FRAMES counts)\n"},
    {"fsm", CmdFsm,
      "fsm <id> <state>            force a patterned actor's AI state by name (as `obj` prints it; FSM2\n"
      "                              sub-machines too; an unknown name lists the valid ones)\n"},
    {"bodystate", CmdBodyState,
      "bodystate <id> <n>          set a patterned actor's body state (the pas::EAnimationState index)\n"},
    {"statecall", CmdStateCall,
      "statecall <id> <state> <msg> [dt]  run one FSM2 state body once (msg 0/1/2), also states with no FSM node\n"},
    {"codecall", CmdCodeCall,
      "codecall <id> <code> [dt]   run one FSM2 code function once (codes run on transitions)\n"},
    {"pathfail", CmdPathFail,
      "pathfail <id> on|off        the actor's path search fails (Search, FindClosestReachablePoint)\n"},
    {"setarea", CmdSetArea,
      "setarea <id> <area>         set an entity's current area id (put it back after; obj shows it; loaded areas only)\n"},
    {"var", CmdVar,
      "var <id> <name> [value]     a module field a port registered (dghd.attack, dghd.flags, dghd.sweeptime); var player spawnmorph [n]\n"},
    {"scaninfo", CmdScanInfo,
      "scaninfo <id>               the SCAN asset an actor's GetScannableObjectInfo answers\n"},
    {"orbit", CmdOrbit,
      "orbit [id]                  lock-on state; with id: orbit position, ValidateOrbitTargetId\n"},
    {"hint", CmdHint,
      "hint <id> [type] [area n] [radius <r>]  an AI hint's type, area, active flag and radius;\n"
      "                            set type / area / radius (coverage)\n"},
    {"place", CmdPlace,
      "place <id> <x y z>|player [d] [yaw [pitch [roll]]]  move an actor to a point, or d (6)\n"
      "                            units in front of the player; x y z form: angles in degrees\n"},
    {"spawn", CmdSpawn,
      "spawn <fourcc> [n] [d <dist>] [ancs <id>|u<id> [char [anim]]] [fsm <id>] [model <id>|u<id>] [inactive] [prop <pid> <v>]  create script objects via their loader\n"
      "                            [rules <id>] knockback RULE (patterned default 393ca543; ffffffff = none)\n"
      "                            [conn <state> <msg> <id>] add a script connection (names as obj prints them)\n"
      "                            [wpsc <id>] projectile WPSC (CHOG's two; else prop ef485db9; CHOG default 681f3907)\n"},
    {"touch", CmdTouch,
      "touch <mover id> <other id>  call the mover's CollidedWith with the other actor's material\n"
      "                           list, the way the collision code does (no real collision needed)\n"},
    {"pirateattach", CmdPirateAttach,
      "pirateattach <pirate> <id>|-  AttachActorToPirate(id) (- : DetachActorFromPirate) through the\n"
      "                           module's table, as a Metroid latching on does (coverage)\n"},
    {"material", CmdMaterial,
      "material <id> [+|-]<m> ...  show or add/remove an actor's materials (name w/o kMT_, or bit; -solid)\n"
      "                           with x y z the yaw (and pitch, roll) in degrees may follow\n"},
    {"shoot", CmdShoot,
      "shoot <id> [charged|missile] [d] [dir x y z]  fire the beam in hand (or a real player\n"
      "                            missile) at an actor from d (3) units; travelling along dir\n"
      "                            (default from the player): a real projectile\n"},
    {"struggle", CmdStruggle,
      "struggle <0..1>|off         hold the player's attached-actor struggle (1 wakes a grabber's\n"
      "                            dizzy gore; the game only grows it from held turn/jump buttons)\n"},
    {"visor", CmdVisor,
      "visor [name|0-3]            show or switch the visor (combat, echo, scan, dark)\n"},
    {"beam", CmdBeam,
      "beam [name|0-3]             show or switch the beam (power, dark, light, annihilator)\n"},
    {"radial", CmdRadial,
      "radial [beam|visor x y|mouse dx dy|release|cancel|pad beam|visor x y|padrelease|padoff]\n"
      "                           native selector harness (Trilogy opt-in)\n"
      "radial touch begin beam|visor owner | move|end owner x y | cancel owner | cancelall\n"},
    {"tp", CmdTp,
      "tp <x> <y> <z>              teleport the player, keeping its facing\n"},
    {"ray", CmdRay,
      "ray <x> <y> <z> [len] [dx dy dz]  static and render surfaces (beam stops/passes) and the world hit along the ray (default: down)\n"},
    {"face", CmdFace,
      "face <yaw deg>              turn the player where it stands (0 = +y, 90 = +x)\n"},
    {"aim", CmdAim,
      "aim <yaw> <pitch>          point the view outright, in degrees; negative pitch looks down\n"},
    {"items", CmdItems,
      "items                       power-up amounts and capacities\n"},
    {"world", CmdWorld,
      "world <world> [area]        load another world (the MP1 console's `warp <world>`)\n"},
    {"worlds", CmdWorlds,
      "worlds                      the memory card's world ids, areas and name tables\n"},
    {"strg", CmdStrg,
      "strg <id|name> [index]      a string table as the game loads it\n"},
    {"give", CmdGiveAdd,
      "give <item> [n] | take      add an item (capacity and amount), or remove capacity\n"},
    {"take", CmdGiveTake, ""},
    {"press", CmdPress,
      "press <a+b+...> [updates]   hold pad buttons (0 updates: until the next press/stick)\n"},
    {"grapple", CmdGrapple,
      "grapple <id> [d]            wake a grapple point, stand d (8) units from it facing it, hold L\n"},
    {"lock", CmdLock,
      "lock <id> [d] [updates]     face an actor d (10) units away, rebuild the candidates, hold L\n"},
    {"scan", CmdScan,
      "scan <id> [d] [updates]     scan an actor: d (40) units away, orbit locked, L held (alone: progress)\n"},
    {"stick", CmdStick,
      "stick <x> <y> [updates]     hold the main stick, -127..127\n"},
    {"cstick", CmdCstick,
      "cstick <x> <y> [updates]    hold the C-stick (Spring Ball in Morph Ball), -127..127\n"},
    {"screenshot", CmdScreenshot,
      "screenshot                  the F12 capture; the log names the file it wrote\n"},
    {"at", CmdAt,
      "at <update> <command>       run one command on that game update (`frame`), not on arrival,\n"
      "                            so runs at different speeds act on the same tick\n"},
    {"gallery", CmdGallery,
      "gallery [mask]              the slideshow gallery unlock mask (0..63): print, or set via PercentScans/mode flags\n"},
    {"help", CmdHelp,
      "help                        this list\n"},
    {"freecam", CmdFreeCam,
      "freecam [0|1]              free camera (photo mode); no argument toggles it\n"},
    {"photo", CmdPhoto,
      "photo [on|off|1|2|4]       photo mode (F8): on/off; a factor saves a PNG, supersampled by it\n"},
    {"livesplit", CmdLiveSplit,
      "livesplit [0|1|addr <host:port>|send <line>]  the LiveSplit link (the F1 setting); no argument: status\n"},
    {"touchact", CmdTouchAct,
      "touchact <actor id> <other id>  run an actor's touch handler for the other actor (coverage)\n"},
    {"trigcall", CmdTrigCall,
      "trigcall <id> <trigger name> [arg]  run one named trigger function of an actor once\n"},
    {"uevent", CmdUEvent,
      "uevent <id> <type> [dt]     call an actor's user-event handler with an event type (coverage)\n"},
    {"title", CmdTitle,
      "title                      quit to the title: the game restarts as after the pause menu's Quit\n"},
    {"window", CmdWindow,
      "window [<width> <height>]  the window size in pixels; with two numbers, leave fullscreen and resize\n"},
    {"pipetrim", CmdPipeTrim,
      "pipetrim [frames]          release built pipelines unused for `frames` (min 16) frames; they rebuild on demand\n"},
    {"", nullptr,
      "save export [<path>]        write the card's save file out (default: save-backups/)\n"},
    {"", nullptr,
      "save import <path|latest>   put a .gci back on the card; the file it replaces is kept\n"},
    {"", nullptr,
      "save backup | backups       copy the card now, or list the copies in save-backups\n"},
    {"", nullptr,
      "a; b; c                     run several commands in the same game tick (one reply each)\n"},
};

void CmdHelp(CStateManager&, const Args&, std::string& out) {
  for (const CommandSpec& command : kCommands) {
    out += command.help;
  }
}

void Execute(CStateManager& mgr, const std::string& line, std::string& out) {
  const Args args = Split(line);
  if (args.empty()) {
    out += "=> err: empty command (try `help`)\n";
    return;
  }
  for (const CommandSpec& command : kCommands) {
    if (command.run == nullptr || command.name.size() != args[0].size() ||
        std::memcmp(command.name.data(), args[0].data(), args[0].size()) != 0) {
      continue;
    }
    std::string body;
    sFailure.clear();
    command.run(mgr, args, body);
    out += body;
    if (sFailure.empty()) {
      out += "=> ok\n";
    } else {
      out += "=> err: " + sFailure + "\n";
    }
    return;
  }
  out += "=> err: unknown command '" + Str(args[0]) + "' (try `help`)\n";
}

} // namespace

void Start() {
  if (sStarted) {
    return;
  }
  sStarted = true;
  const char* value = std::getenv("MP2_CONSOLE");
  if (value == nullptr || value[0] == '\0' || std::strcmp(value, "0") == 0) {
    return;
  }
#ifdef _WIN32
  std::fprintf(stderr, "[console] MP2_CONSOLE is not supported on Windows\n");
  return;
#else
  int port = kDefaultPort;
  if (std::strcmp(value, "1") != 0) {
    const int parsed = std::atoi(value);
    if (parsed < 2 || parsed > 65535) {
      std::fprintf(stderr, "[console] MP2_CONSOLE=%s is not a port (2-65535)\n", value);
      return;
    }
    port = parsed;
  }
  const int listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    std::fprintf(stderr, "[console] no socket: %s\n", std::strerror(errno));
    return;
  }
  int yes = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast< uint16_t >(port));
  if (bind(listener, reinterpret_cast< sockaddr* >(&addr), sizeof(addr)) != 0 ||
      listen(listener, 1) != 0) {
    std::fprintf(stderr, "[console] cannot listen on 127.0.0.1:%d: %s\n", port,
                 std::strerror(errno));
    close(listener);
    return;
  }
  sEnabled = true;
  std::fprintf(stderr, "[console] listening on 127.0.0.1:%d (try `help`)\n", port);
  std::thread(ListenThread, listener).detach();
  // MP2_CONSOLE_WAIT=<secs> (1 = 600): hold the boot until a client connects, so a script driver
  // that retries its connect (tools/drivers/console.py, every 0.5 s) is in before the first update. An
  // unpaced fast run does thousands of updates a second; without the wait, a late connect queues
  // every overdue `at` line for the same update and the script's spacing is lost.
  if (const char* wait = std::getenv("MP2_CONSOLE_WAIT");
      wait != nullptr && wait[0] != '\0' && std::strcmp(wait, "0") != 0) {
    const int secs = std::strcmp(wait, "1") == 0 ? 600 : std::max(1, std::atoi(wait));
    std::unique_lock< std::mutex > lock(sClientMutex);
    if (!sClientArrived.wait_for(lock, std::chrono::seconds(secs), [] { return sClient >= 0; })) {
      std::fprintf(stderr, "[console] no client after %d s (MP2_CONSOLE_WAIT), booting anyway\n",
                   secs);
    }
  }
#endif
}

bool Enabled() { return sEnabled; }

namespace {

// Set by Tick (the state manager's update ran) and cleared by IdleTick, so IdleTick knows whether
// this frame's commands have already been served.
bool sTickedSinceIdle = false;

void RunLocal(CStateManager& mgr) {
  for (;;) {
    std::string line;
    {
      std::lock_guard< std::mutex > lock(sLocalMutex);
      if (sLocal.empty()) {
        return;
      }
      line = std::move(sLocal.front());
      sLocal.pop_front();
    }
    std::string out = "[local] > " + line + '\n';
    sExecutingGeneration = 0;
    Execute(mgr, line, out);
    std::fputs(out.c_str(), stderr);
  }
}

void RunCommands(CStateManager& mgr) {
  RunLocal(mgr);
#ifndef _WIN32
  if (!sEnabled) {
    return;
  }
  for (size_t i = 0; i < sDeferred.size();) {
    if (sDeferred[i].update > PortHarness::GameUpdates()) {
      ++i;
      continue;
    }
    const SDeferred deferred = std::move(sDeferred[i]);
    sDeferred.erase(sDeferred.begin() + static_cast< std::ptrdiff_t >(i));
    std::string out = "[at " + std::to_string(deferred.update) + "] > " + deferred.line + '\n';
    sExecutingGeneration = deferred.generation;
    Execute(mgr, deferred.line, out);
    // The client may have moved on or left; the log keeps the reply.
    std::fputs(out.c_str(), stderr);
    SendRaw(deferred.generation, out);
  }
  for (;;) {
    SQueued queued;
    {
      std::lock_guard< std::mutex > lock(sQueueMutex);
      if (sQueue.empty()) {
        return;
      }
      queued = std::move(sQueue.front());
      sQueue.pop_front();
    }
    // `a; b; c` runs every command in this one tick, so a forced arm can land inside a one-frame
    // window (separate lines can arrive a frame apart).
    size_t start = 0;
    for (;;) {
      const size_t sep = queued.line.find(';', start);
      std::string out;
      sExecutingGeneration = queued.generation;
      Execute(mgr, queued.line.substr(start, sep == std::string::npos ? std::string::npos : sep - start), out);
      SendRaw(queued.generation, out);
      if (sep == std::string::npos) {
        break;
      }
      start = sep + 1;
    }
  }
#else
  (void)mgr;
#endif
}

} // namespace

void Tick(CStateManager& mgr) {
  // Save states share this game-thread hook (CStateManager::PortWarpTick) and run without the console.
  PortSaveState::Tick(mgr);
  sTickedSinceIdle = true;
  RunCommands(mgr);
  DropUnplayableBodyCmds(mgr, nullptr);
}

void IdleTick(CStateManager& mgr) {
  if (sTickedSinceIdle) {
    sTickedSinceIdle = false;
    return;
  }
  RunCommands(mgr);
}

bool Run(std::string_view line) {
  std::lock_guard< std::mutex > lock(sLocalMutex);
  if (sLocal.size() >= kMaxQueued) {
    return false;
  }
  sLocal.emplace_back(line);
  return true;
}

std::span<const Item> Items() {
  // Key = the `give` spelling (console_names.inc), label = the F1 Cheats wording.
  static const Item kItems[] = {
    {"powerBeam", "Power Beam"},
    {"darkBeam", "Dark Beam"},
    {"lightBeam", "Light Beam"},
    {"annihilatorBeam", "Annihilator Beam"},
    {"chargeBeam", "Charge Beam"},
    {"superMissile", "Super Missile"},
    {"darkburst", "Darkburst"},
    {"sunburst", "Sunburst"},
    {"sonicBoom", "Sonic Boom"},
    {"combatVisor", "Combat Visor"},
    {"scanVisor", "Scan Visor"},
    {"darkVisor", "Dark Visor"},
    {"echoVisor", "Echo Visor"},
    {"variaSuit", "Varia Suit"},
    {"darkSuit", "Dark Suit"},
    {"lightSuit", "Light Suit"},
    {"morphBall", "Morph Ball"},
    {"boostBall", "Boost Ball"},
    {"spiderBall", "Spider Ball"},
    {"morphBallBombs", "Morph Ball Bombs"},
    {"powerbomb", "Power Bombs"},
    {"grappleBeam", "Grapple Beam"},
    {"spaceJumpBoots", "Space Jump Boots"},
    {"gravityBoost", "Gravity Boost"},
    {"seekerLauncher", "Seeker Launcher"},
    {"screwAttack", "Screw Attack"},
    {"missile", "Missiles"},
    {"energyTanks", "Energy Tanks"},
    {"darkAmmo", "Dark Ammo"},
    {"lightAmmo", "Light Ammo"},
    {"violetTranslator", "Violet Translator"},
    {"amberTranslator", "Amber Translator"},
    {"emeraldTranslator", "Emerald Translator"},
    {"cobaltTranslator", "Cobalt Translator"},
    {"templeKey1", "Sky Temple Key 1"},
    {"templeKey2", "Sky Temple Key 2"},
    {"templeKey3", "Sky Temple Key 3"},
    {"templeKey4", "Sky Temple Key 4"},
    {"templeKey5", "Sky Temple Key 5"},
    {"templeKey6", "Sky Temple Key 6"},
    {"templeKey7", "Sky Temple Key 7"},
    {"templeKey8", "Sky Temple Key 8"},
    {"templeKey9", "Sky Temple Key 9"},
    {"agonKey1", "Agon Key 1"},
    {"agonKey2", "Agon Key 2"},
    {"agonKey3", "Agon Key 3"},
    {"torvusKey1", "Torvus Key 1"},
    {"torvusKey2", "Torvus Key 2"},
    {"torvusKey3", "Torvus Key 3"},
    {"hiveKey1", "Hive Key 1"},
    {"hiveKey2", "Hive Key 2"},
    {"hiveKey3", "Hive Key 3"},
  };
  return kItems;
}

bool RegisterVar(VarHandler handler) {
  VarHandlers().push_back(handler);
  return true;
}

CStateManager* VarManager() { return sVarManager; }

bool PathSearchForcedFail(const CPathFindSearch* search) {
  const std::vector< const CPathFindSearch* >& searches = FailingSearches();
  return !searches.empty() && std::find(searches.begin(), searches.end(), search) != searches.end();
}

} // namespace PortConsole
