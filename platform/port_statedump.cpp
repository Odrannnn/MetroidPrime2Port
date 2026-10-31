#include "port_statedump.h"

#include "port_savestate.h"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CActor.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CObjectList.hpp"
#include "MetroidPrime/CPhysicsActor.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/TCastTo.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <typeinfo>
#if defined(__GNUC__)
#include <cxxabi.h>
#endif

namespace PortStateDump {
namespace {
// The class naming of the `objs` console command.
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

std::string Esc(const char* s) {
  std::string out;
  for (; s != nullptr && *s != '\0'; ++s) {
    const unsigned char c = static_cast< unsigned char >(*s);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast< char >(c);
    } else if (c < 0x20 || c >= 0x7f) {
      out += '?';
    } else {
      out += static_cast< char >(c);
    }
  }
  return out;
}

std::string Fmt(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  return buf;
}

std::string Vec(const CVector3f& v) { return Fmt("[%.4f,%.4f,%.4f]", v.GetX(), v.GetY(), v.GetZ()); }

std::string ObjectLine(CStateManager& mgr, CEntity& ent) {
  const int area = ent.GetCurrentAreaId().Value();
  uint32_t mrea = 0;
  const CWorld* world = mgr.GetWorld();
  if (world != nullptr && area >= 0 && area < world->GetNumAreas()) {
    mrea = static_cast< uint32_t >(world->GetAreaAlways(TAreaId(area)).GetAreaAssetId());
  }
  std::string line = Fmt("{\"t\":\"obj\",\"uid\":%u,\"eid\":\"%08X\",\"area\":%d,\"mrea\":\"%08X\","
                         "\"class\":\"%s\",\"active\":%d,\"generated\":%d",
                         ent.GetUniqueId().Value(), ent.GetEditorId().Value(), area, mrea,
                         Esc(ClassName(ent).c_str()).c_str(), ent.GetActive() ? 1 : 0,
                         PortSaveState::IsScriptGeneratedCopy(mgr, ent) ? 1 : 0);
  if (CActor* actor = TCastToPtr< CActor >(&ent)) {
    if (const CHealthInfo* health = actor->GetHealthInfo()) {
      line += Fmt(",\"hp\":%.4f,\"kbres\":%.4f", health->GetHP(), health->GetKnockBackResistance());
    }
    line += ",\"pos\":" + Vec(actor->GetTranslation());
    line += ",\"fwd\":" + Vec(actor->GetTransform().GetForward());
  }
  if (const CPhysicsActor* physics = TCastToConstPtr< CPhysicsActor >(&ent)) {
    line += ",\"vel\":" + Vec(physics->GetVelocityWR());
    line += ",\"mom\":" + Vec(physics->GetMomentumWR());
  }
  line += PortSaveState::RoomStateJson(mgr, ent);
  if (CPatterned* patterned = TCastToPtr< CPatterned >(&ent)) {
    TStateMachineStateBase< CPatterned >& machine = patterned->StateMachineState();
    const char* name = machine.HasState() ? machine.GetName() : nullptr;
    line += Fmt(",\"ai\":\"%s\",\"ai_time\":%.4f", Esc(name).c_str(), machine.GetTime());
    if (const CBodyController* body = patterned->GetBodyController()) {
      line += Fmt(",\"body\":%d,\"loco\":%d,\"anim\":%d", static_cast< int >(body->GetCurrentStateId()),
                  static_cast< int >(body->GetLocomotionType()), body->GetCurrentAnimId());
    }
  }
  return line + "}\n";
}

std::string PlayerLine(CStateManager& mgr) {
  const CPlayer& player = *mgr.GetPlayer(0);
  const CPlayerState& ps = *mgr.GetPlayerState();
  return Fmt("{\"t\":\"player\",\"pos\":%s,\"fwd\":%s,\"morph\":%d,\"hp\":%.4f,\"area\":%d}\n",
             Vec(player.GetTranslation()).c_str(), Vec(player.GetTransform().GetForward()).c_str(),
             static_cast< int >(player.GetMorphballTransitionState()), ps.GetHealthInfo().GetHP(),
             player.GetCurrentAreaId().Value());
}
} // namespace

int Write(CStateManager& mgr, const std::string& path, std::string& error) {
  if (mgr.GetPlayer(0) == nullptr || mgr.GetPlayerState() == nullptr) {
    error = "no player yet";
    return -1;
  }
  std::string text = PlayerLine(mgr);
  int count = 0;
  const CObjectList& list = mgr.GetObjectListById(kOL_All);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    CEntity* ent = const_cast< CEntity* >(list[i]);
    if (ent == nullptr) {
      continue;
    }
    text += ObjectLine(mgr, *ent);
    ++count;
  }
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr || std::fwrite(text.data(), 1, text.size(), f) != text.size()) {
    error = "can't write " + path;
    if (f != nullptr) {
      std::fclose(f);
    }
    return -1;
  }
  std::fclose(f);
  return count;
}
} // namespace PortStateDump
