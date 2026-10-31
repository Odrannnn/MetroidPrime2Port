#include "port_entity_schema.h"

#include "MetroidPrime/CEntity.hpp"

#include <cstring>
#include <mutex>
#include <string>
#include <typeinfo>
#include <unordered_map>

// The generated accessors: a member's address, and an integral member (bitfields too) read
// and written by value. Captureless lambdas, so they convert to the table's function pointers.
#define PES_ADDR(T, f)                                                                             \
  [](CEntity& e) -> void* { return const_cast< void* >(static_cast< const void* >(&static_cast< T& >(e).f)); }
#define PES_GETI(T, f)                                                                             \
  [](const CEntity& e) -> long long { return static_cast< long long >(static_cast< const T& >(e).f); }
#define PES_SETI(T, f)                                                                             \
  [](CEntity& e, long long v) { static_cast< T& >(e).f = static_cast< decltype(T::f) >(v); }

#include "generated/port_entity_schema.inc"

#undef PES_ADDR
#undef PES_GETI
#undef PES_SETI

namespace {
// typeid(...).name() for a class named `qualified`: Itanium (GCC, clang) mangles a global class
// as <len><name> and a nested one as N<len><name>...E; MSVC writes "class X::Y" or "struct X::Y".
bool TypeNameIs(const char* raw, const char* qualified) {
  std::string itanium;
  const char* p = qualified;
  int parts = 0;
  while (*p != '\0') {
    const char* end = std::strstr(p, "::");
    const size_t len = end != nullptr ? static_cast< size_t >(end - p) : std::strlen(p);
    itanium += std::to_string(len);
    itanium.append(p, len);
    ++parts;
    p += len + (end != nullptr ? 2 : 0);
  }
  if (parts > 1)
    itanium = "N" + itanium + "E";
  if (std::strcmp(raw, itanium.c_str()) == 0)
    return true;
  for (const char* prefix : {"class ", "struct "}) {
    const size_t n = std::strlen(prefix);
    if (std::strncmp(raw, prefix, n) == 0 && std::strcmp(raw + n, qualified) == 0)
      return true;
  }
  return false;
}
} // namespace

const PortSchemaClass* PortEntitySchema::Find(const char* name) {
  if (name == nullptr || *name == '\0')
    return nullptr;
  int count = 0;
  const PortSchemaClass* const* table = Classes(count);
  for (int i = 0; i < count; ++i)
    if (std::strcmp(table[i]->name, name) == 0)
      return table[i];
  return nullptr;
}

const PortSchemaClass* PortEntitySchema::For(const CEntity& ent) {
  static std::mutex mutex;
  static std::unordered_map< const std::type_info*, const PortSchemaClass* > cache;
  const std::type_info& type = typeid(ent);
  std::lock_guard< std::mutex > lock(mutex);
  auto it = cache.find(&type);
  if (it != cache.end())
    return it->second;
  int count = 0;
  const PortSchemaClass* const* table = Classes(count);
  const PortSchemaClass* found = nullptr;
  for (int i = 0; i < count && found == nullptr; ++i)
    if (TypeNameIs(type.name(), table[i]->name))
      found = table[i];
  // A class the headers don't define (one a .cpp declares): its fields are unknown, but the
  // CEntity fields still are.
  if (found == nullptr)
    found = Find("CEntity");
  cache.emplace(&type, found);
  return found;
}

uint64_t PortEntitySchema::Fingerprint(const PortSchemaClass& cls) {
  // Must match fingerprint() in tools/gen_entity_schema.py.
  uint64_t h = 0xcbf29ce484222325ull;
  auto mix = [&h](const char* s) {
    for (; *s != '\0'; ++s)
      h = (h ^ static_cast< unsigned char >(*s)) * 0x100000001b3ull;
  };
  mix(cls.base);
  mix("\n");
  for (int i = 0; i < cls.fieldCount; ++i) {
    const PortSchemaField& f = cls.fields[i];
    mix(f.name);
    mix(":");
    mix(KindName(f.kind));
    mix(":");
    mix(f.type);
    mix(":");
    mix(std::to_string(f.bits).c_str());
    mix("\n");
  }
  return h;
}

const char* PortEntitySchema::KindName(PortSchemaKind kind) {
  switch (kind) {
  case PortSchemaKind::bool_:
    return "bool";
  case PortSchemaKind::int_:
    return "int";
  case PortSchemaKind::float_:
    return "float";
  case PortSchemaKind::bitfield:
    return "bitfield";
  case PortSchemaKind::math:
    return "math";
  case PortSchemaKind::uid:
    return "uid";
  case PortSchemaKind::pod:
    return "pod";
  case PortSchemaKind::pointer:
    return "pointer";
  case PortSchemaKind::smart_ptr:
    return "smart_ptr";
  case PortSchemaKind::container:
    return "container";
  case PortSchemaKind::state_machine:
    return "state_machine";
  case PortSchemaKind::body_controller:
    return "body_controller";
  case PortSchemaKind::anim_data:
    return "anim_data";
  case PortSchemaKind::path_search:
    return "path_search";
  case PortSchemaKind::other:
    return "other";
  }
  return "?";
}
