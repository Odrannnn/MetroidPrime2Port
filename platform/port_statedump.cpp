#include "port_statedump.h"

#include "port_entity_schema.h"
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
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
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

#if defined(__GNUC__)
std::string Fmt(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#endif
std::string Fmt(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  return buf;
}

std::string Vec(const CVector3f& v) { return Fmt("[%.4f,%.4f,%.4f]", v.GetX(), v.GetY(), v.GetZ()); }

std::string Num(double v) { return std::isfinite(v) ? Fmt("%.4f", v) : "null"; }

// A reference by what survives a reload: the editor id of a script object, else its class.
std::string UidRef(CStateManager& mgr, TUniqueId uid) {
  if (uid == kInvalidUniqueId)
    return "\"none\"";
  const CEntity* ent = mgr.GetObjectById(uid);
  if (ent == nullptr)
    return "\"gone\"";
  const uint32_t eid = ent->GetEditorId().Value();
  if ((eid & 0xFFFFFF) != 0xFFFFFF)
    return Fmt("\"eid:%08X\"", eid);
  return "\"cls:" + Esc(ClassName(*ent).c_str()) + "\"";
}

// Every plain-data field the schema (port_entity_schema.h) lists for the object's class
// chain, keyed by field name (Class::name where a base already used the name): `fields`
// those the room record's kTagFields block keeps for this object, `fields_other` the rest.
std::string SchemaFields(CStateManager& mgr, CEntity& ent) {
  std::string out;
  std::string other;
  std::set< std::string > used;
  const bool saved = PortSaveState::EntityFieldsSaved(mgr, ent);
  for (const PortSchemaClass* cls = PortEntitySchema::For(ent); cls != nullptr; cls = PortEntitySchema::Base(*cls)) {
    for (int i = 0; i < cls->fieldCount; ++i) {
      const PortSchemaField& f = cls->fields[i];
      if (!PortEntitySchema::IsPlain(f.kind))
        continue;
      std::string key = f.name;
      if (!used.insert(key).second)
        key = std::string(cls->name) + "::" + key;
      std::string value;
      if (f.getInt != nullptr) {
        value = Fmt("%lld", f.getInt(ent));
      } else if (f.addr != nullptr && f.kind == PortSchemaKind::float_) {
        const void* p = f.addr(ent);
        value = Num(f.size == sizeof(double) ? *static_cast< const double* >(p) : *static_cast< const float* >(p));
      } else if (f.addr != nullptr && f.kind == PortSchemaKind::math) {
        const float* p = static_cast< const float* >(f.addr(ent));
        value = "[";
        for (int j = 0; j < static_cast< int >(f.size / sizeof(float)); ++j)
          value += (j != 0 ? "," : "") + Num(p[j]);
        value += "]";
      } else if (f.addr != nullptr && f.kind == PortSchemaKind::uid) {
        value = UidRef(mgr, *static_cast< const TUniqueId* >(f.addr(ent)));
      } else {
        continue;
      }
      std::string& to = saved && PortSaveState::SchemaFieldSaved(*cls, f) ? out : other;
      to += (to.empty() ? "\"" : ",\"") + Esc(key.c_str()) + "\":" + value;
    }
  }
  return ",\"fields\":{" + out + "},\"fields_other\":{" + other + "}";
}

std::string ObjectLine(CStateManager& mgr, CEntity& ent, int copy) {
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
  if (copy >= 0) {
    line += Fmt(",\"copy\":%d", copy);
  }
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
    line += Fmt(",\"ai\":\"%s\",\"ai_time\":%.4f,\"alive\":%d", Esc(name).c_str(), machine.GetTime(),
                patterned->GetAlive() ? 1 : 0);
    if (const CBodyController* body = patterned->GetBodyController()) {
      line += Fmt(",\"body\":%d,\"loco\":%d,\"anim\":%d", static_cast< int >(body->GetCurrentStateId()),
                  static_cast< int >(body->GetLocomotionType()), body->GetCurrentAnimId());
    }
  }
  line += SchemaFields(mgr, ent);
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
  std::map< uint32_t, int > copies;
  const CObjectList& list = mgr.GetObjectListById(kOL_All);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    CEntity* ent = const_cast< CEntity* >(list[i]);
    if (ent == nullptr) {
      continue;
    }
    // a generated copy's place among its template's copies in list order, as the room state numbers it
    const int copy = PortSaveState::IsScriptGeneratedCopy(mgr, *ent) ? copies[ent->GetEditorId().Value()]++ : -1;
    text += ObjectLine(mgr, *ent, copy);
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
