// The generated entity field schema (platform/generated/port_entity_schema.inc): every class
// reaches CEntity through classes the table has, the counts and fingerprints agree with what
// the generator wrote, and each field's accessors fit its kind. Whether the table matches the
// headers is port_entity_schema_current (tools/gen_entity_schema.py --check).
#include "port_entity_schema.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

static int g_failures = 0;
#define CHECK(cond, ...)                                                                           \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);                                  \
      std::printf(__VA_ARGS__);                                                                    \
      std::printf("\n");                                                                           \
      ++g_failures;                                                                                \
    }                                                                                              \
  } while (0)

static const PortSchemaField* FieldOf(const char* cls, const char* field) {
  const PortSchemaClass* c = PortEntitySchema::Find(cls);
  for (int i = 0; c != nullptr && i < c->fieldCount; ++i)
    if (std::strcmp(c->fields[i].name, field) == 0)
      return &c->fields[i];
  return nullptr;
}

int main() {
  int count = 0;
  const PortSchemaClass* const* table = PortEntitySchema::Classes(count);
  CHECK(count == PortEntitySchema::kClassCount, "%d classes, %d expected", count, PortEntitySchema::kClassCount);
  CHECK(count > 0 && std::strcmp(table[0]->name, "CEntity") == 0, "CEntity first");

  std::set< std::string > names;
  int fields = 0;
  for (int i = 0; i < count; ++i) {
    const PortSchemaClass& c = *table[i];
    CHECK(names.insert(c.name).second, "%s listed twice", c.name);
    CHECK(PortEntitySchema::Fingerprint(c) == c.fingerprint, "%s fingerprint", c.name);
    int depth = 0;
    const PortSchemaClass* up = &c;
    while (up != nullptr && std::strcmp(up->name, "CEntity") != 0 && depth < 32) {
      const PortSchemaClass* base = PortEntitySchema::Base(*up);
      CHECK(base != nullptr, "%s: base %s not in the table", up->name, up->base);
      up = base;
      ++depth;
    }
    CHECK(up != nullptr && depth < 32, "%s doesn't reach CEntity", c.name);
    for (int j = 0; j < c.fieldCount; ++j) {
      const PortSchemaField& f = c.fields[j];
      ++fields;
      const bool integral = f.kind == PortSchemaKind::bool_ || f.kind == PortSchemaKind::int_ ||
                            f.kind == PortSchemaKind::bitfield;
      CHECK(f.name[0] != '\0' && f.type[0] != '\0', "%s field %d unnamed", c.name, j);
      CHECK((f.getInt != nullptr) == integral, "%s::%s getInt", c.name, f.name);
      CHECK((f.setInt != nullptr) == (integral && !(f.flags & kPortSchemaConst)), "%s::%s setInt", c.name, f.name);
      if (f.kind == PortSchemaKind::bitfield) {
        CHECK(f.bits > 0 && f.size == 0 && f.addr == nullptr, "%s::%s bitfield", c.name, f.name);
      } else {
        CHECK(f.bits == 0 && f.size > 0, "%s::%s size %d", c.name, f.name, f.size);
        CHECK(f.addr != nullptr || f.kind == PortSchemaKind::pointer, "%s::%s addr", c.name, f.name);
      }
      if (f.kind == PortSchemaKind::float_)
        CHECK(f.size == sizeof(float) || f.size == sizeof(double), "%s::%s float size %d", c.name, f.name, f.size);
      if (f.kind == PortSchemaKind::math)
        CHECK(f.size % sizeof(float) == 0, "%s::%s math size %d", c.name, f.name, f.size);
      if (f.kind == PortSchemaKind::uid)
        CHECK(f.size == 2, "%s::%s TUniqueId size %d", c.name, f.name, f.size);
      if (f.uidAt != nullptr)
        CHECK(f.kind == PortSchemaKind::pod, "%s::%s embedded IDs outside POD", c.name, f.name);
      CHECK(PortEntitySchema::IsPlain(f.kind) ==
                (integral || f.kind == PortSchemaKind::float_ || f.kind == PortSchemaKind::math ||
                 f.kind == PortSchemaKind::uid),
            "%s::%s IsPlain", c.name, f.name);
    }
  }
  CHECK(fields == PortEntitySchema::kFieldCount, "%d fields, %d expected", fields, PortEntitySchema::kFieldCount);

  // Classes the save states restore by hand today, and the fields they lean on.
  for (const char* name : {"CActor", "CPhysicsActor", "CPatterned", "CAi", "CPlayer", "CScriptDoor", "CScriptTimer",
                           "CScriptCounter", "CSplinter", "CGrenchler", "CGrappleArm"})
    CHECK(PortEntitySchema::Find(name) != nullptr, "%s missing", name);
  const PortSchemaField* uid = FieldOf("CEntity", "mUniqueId");
  CHECK(uid != nullptr && uid->kind == PortSchemaKind::uid, "CEntity::mUniqueId");
  const PortSchemaField* active = FieldOf("CEntity", "mActive");
  CHECK(active != nullptr && active->kind == PortSchemaKind::bitfield && active->bits == 1, "CEntity::mActive");
  const PortSchemaField* sm = FieldOf("CPatterned", "mStateMachine");
  CHECK(sm != nullptr && sm->kind == PortSchemaKind::state_machine, "CPatterned::mStateMachine");
  const PortSchemaField* body = FieldOf("CPatterned", "mBodyController");
  CHECK(body != nullptr && body->kind == PortSchemaKind::body_controller, "CPatterned::mBodyController");

  // A pointer-free POD is not necessarily reference-free. These used to copy old slot IDs
  // into a rebuilt room (CParasite's owner could become an actor with no model).
  const char* referencePods[][2] = {
      {"CParasite", "mLineOfSight"}, {"CBlogg", "mLineOfSightTracker"},
      {"CCommandoPirate", "mVisualKnowledge"}, {"CPatterned", "mWaypointNavigation"},
      {"CPatterned", "mPathFindNavigation"}, {"CAi", "mHealthInfo"},
      {"CSplinter", "mSac"}, {"CLumite", "mSurface"},
      {"CDarkSamus", "mAttackerUids"}, {"CDarkSamus", "mMeleeContact"},
      {"CGameCamera", "mFovInterpolation"}, {"CScriptTimer", "mScriptMsg"}};
  for (const auto& pod : referencePods) {
    const PortSchemaField* f = FieldOf(pod[0], pod[1]);
    CHECK(f != nullptr && f->uidAt != nullptr, "%s::%s missing reference accessor", pod[0], pod[1]);
  }
  const PortSchemaField* color = FieldOf("CActor", "mDrawFlags");
  CHECK(color != nullptr && color->uidAt == nullptr, "scalar-only POD must not invent references");

  if (g_failures != 0) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("port_entity_schema: %d classes, %d fields ok\n", count, fields);
  return 0;
}
