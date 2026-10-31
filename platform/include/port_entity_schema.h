#pragma once

// The field schema of every CEntity subclass, generated from the game headers by
// tools/gen_entity_schema.py (platform/generated/port_entity_schema.inc): per class its
// entity base and its own fields in declaration order, each with a kind and accessors the
// compiler builds by member name, so nothing here depends on one compiler's layout.
// Save states read it to find what a class holds; docs/SAVESTATE_SCHEMA.md has the counts.
//
// Classes with private or protected fields declare `friend struct PortEntitySchema;`
// (gen_entity_schema.py --add-friends puts it after the opening brace).

#include <cstdint>

class CEntity;

enum class PortSchemaKind : uint8_t {
  bool_,           // bool
  int_,            // an integer or enum
  float_,          // float or double
  bitfield,        // a bitfield of any type: getInt/setInt only, no address
  math,            // a float-only math value (CVector3f, CTransform4f, CAABox, ...)
  uid,             // TUniqueId: a reference to another object
  pod,             // a pointer-free struct or array, copyable as bytes (CHealthInfo, CColor, ...)
  pointer,         // a raw pointer or reference
  smart_ptr,       // rstl::auto_ptr/single_ptr/rc_ptr, TToken, TLockedToken, ...
  container,       // any other rstl template (vector, reserved_vector, optional_object, ...)
  state_machine,   // CStateMachineState
  body_controller, // CBodyController, or a smart pointer to one
  anim_data,       // CAnimData/CModelData, or a smart pointer to one
  path_search,     // CPathFindSearch
  other,           // anything else: needs its own codec (docs/SAVESTATE_SCHEMA.md lists them)
};

enum : uint8_t {
  kPortSchemaPrivate = 1, // private or protected
  kPortSchemaConst = 2,   // const: no setInt
  kPortSchemaUnion = 4,   // a member of an anonymous union
};

struct PortSchemaField {
  const char* name;
  const char* type; // as clang spells it (typedefs resolved where clang does)
  PortSchemaKind kind;
  uint8_t bits;      // bitfield width, else 0
  uint8_t flags;     // kPortSchema*
  uint16_t size;     // sizeof the member on this compiler; 0 for a bitfield
  void* (*addr)(CEntity& ent);                          // null for a bitfield or a reference
  long long (*getInt)(const CEntity& ent);              // bool, int and bitfield kinds
  void (*setInt)(CEntity& ent, long long value);        // the same, unless const
};

struct PortSchemaClass {
  const char* name; // qualified: CFireFlea::CDeathCameraEffect
  const char* base; // the entity base class; "" for CEntity
  const PortSchemaField* fields;
  int fieldCount;
  uint64_t fingerprint; // FNV-1a of the base and the fields' names, kinds, types and widths
};

struct PortEntitySchema {
  static const int kClassCount;
  static const int kFieldCount;

  // Every class, CEntity first.
  static const PortSchemaClass* const* Classes(int& count);
  static const PortSchemaClass* Find(const char* name);
  // The most derived class of `ent` the table has (walks up through classes it lacks).
  static const PortSchemaClass* For(const CEntity& ent);
  static const PortSchemaClass* Base(const PortSchemaClass& cls) { return Find(cls.base); }
  // Recomputed from the table, for checking it against `fingerprint`.
  static uint64_t Fingerprint(const PortSchemaClass& cls);
  static const char* KindName(PortSchemaKind kind);
  static bool IsPlain(PortSchemaKind kind) {
    return kind <= PortSchemaKind::uid;
  }

  // One per class, in the generated table; a member so the friend line covers it.
  template < class T >
  static const PortSchemaClass& Describe();
};
