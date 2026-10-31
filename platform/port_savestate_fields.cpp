#include "port_savestate.h"

#include "port_entity_schema.h"

#include "MetroidPrime/CEntity.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/ScriptObjects/CScriptDebris.hpp"
#include "MetroidPrime/ScriptObjects/CScriptPickup.hpp"
#include "MetroidPrime/TCastTo.hpp"

#include <cstdlib>
#include <cstring>
#include <initializer_list>

// The kTagFields block: an enemy's own fields, read and written through the generated schema
// (port_entity_schema.h) instead of per-class code. See SavedField for what is kept.

namespace PortSaveState {
namespace {

bool Named(const char* name, std::initializer_list< const char* > names) {
  for (const char* n : names) {
    if (std::strcmp(name, n) == 0) {
      return true;
    }
  }
  return false;
}

bool Enabled() {
  static const bool on = [] {
    const char* v = std::getenv("MP_SAVESTATE_FIELDS");
    return v == nullptr || std::strcmp(v, "0") != 0;
  }();
  return on;
}

bool SavedField(const PortSchemaClass& cls, const PortSchemaField& f) {
  if ((f.flags & kPortSchemaConst) != 0) {
    return false;
  }
  if (std::strncmp(f.name, "mPort", 5) == 0 || std::strncmp(f.name, "xPort", 5) == 0) {
    return false;
  }
  switch (f.kind) {
  case PortSchemaKind::bool_:
  case PortSchemaKind::int_:
  case PortSchemaKind::float_:
  case PortSchemaKind::bitfield:
  case PortSchemaKind::math:
  case PortSchemaKind::uid:
  case PortSchemaKind::pod:
  case PortSchemaKind::state_machine:
  case PortSchemaKind::body_controller:
  case PortSchemaKind::anim_data:
  case PortSchemaKind::path_search:
    break;
  default:
    return false;
  }
  if (f.kind != PortSchemaKind::bitfield && f.addr == nullptr) {
    return false;
  }
  if (std::strcmp(cls.name, "CEntity") == 0) {
    return false;
  }
  // Asset streaming is this run's, not the game's.
  if (std::strstr(f.name, "oaded") != nullptr || std::strstr(f.name, "oading") != nullptr) {
    return false;
  }
  if (std::strcmp(cls.name, "CActor") == 0) {
    return Named(f.name, {"mModelData", "mDrawFlags", "mMuted", "mCallTouch", "mRenderParticleDBInside",
                          "mTargetableVisorFlags", "mEnableRender", "mDrawEnabled", "mDoTargetDistanceTest"});
  }
  if (std::strcmp(cls.name, "CPhysicsActor") == 0) {
    // Motion too, though kTagMotion has velocity and momentum: these are applied after the
    // codecs, and the force/impulse a Think queued for the next physics step would be lost.
    return Named(f.name, {"mMovable", "mAngularEnabled", "mConstantForce", "mAngularMomentum", "mVelocity",
                          "mAngularVelocity", "mMomentum", "mForce", "mImpulse", "mTorque", "mAngularImpulse",
                          "mMoveImpulse", "mMoveAngularImpulse", "mNumTicksStuck"});
  }
  // CAi's health is the kTagHealth block's.
  if (std::strcmp(cls.name, "CAi") == 0) {
    return std::strcmp(f.name, "mHealthInfo") != 0;
  }
  return true;
}

} // namespace

bool EntityFieldsSaved(const CStateManager& mgr, CEntity& ent) {
  // Enemies, and the pickups and debris a generator made (life timers, generated flag).
  return Enabled() &&
         (TCastToPtr< CPatterned >(&ent) != nullptr ||
          (IsScriptGeneratedCopy(mgr, ent) &&
           (TCastToPtr< CScriptPickup >(&ent) != nullptr || TCastToPtr< CScriptDebris >(&ent) != nullptr)));
}

bool SchemaFieldSaved(const PortSchemaClass& cls, const PortSchemaField& f) { return SavedField(cls, f); }

} // namespace PortSaveState
