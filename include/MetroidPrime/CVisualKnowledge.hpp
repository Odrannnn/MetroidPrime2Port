#ifndef _CVISUALKNOWLEDGE
#define _CVISUALKNOWLEDGE

#include "types.h"

#include "Collision/CMaterialFilter.hpp"
#include "Kyoto/Animation/CSegId.hpp"
#include "MetroidPrime/TGameTypes.hpp"

class CStateManager;

// Port-only: the DOL's CVisualKnowledge (0x801F55F0..0x801F59D0; upstream has no source). An
// owner's line of sight to a target, re-tested by a world ray at random intervals.
class CVisualKnowledge {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  // 0x801F5888.
  CVisualKnowledge(TUniqueId owner, CSegId locator, float minInterval, float randomInterval);
  // 0x801F55F0.
  void Update(CStateManager& mgr, float dt);
  // 0x801F585C.
  void SetVisualTarget(TUniqueId target);
  // No DOL function: owners store the locator (+0x2) and the filter (+0x8) inline.
  void SetLocator(CSegId locator) { x2_locator = locator; }
  void SetMaterialFilter(const CMaterialFilter& filter) { x8_filter = filter; }

  TUniqueId GetVisualTarget() const { return x20_target; }
  bool IsTargetVisible() const { return x38_visible; }
  float GetVisibleTime() const { return x30_visibleTime; }
  float GetHiddenTime() const { return x34_hiddenTime; }
  // Owners write the flag directly (the CommandoPirate REL clears bit 0x80 of +0x38).
  void SetTargetVisible(bool visible) { x38_visible = visible; }

private:
  TUniqueId x0_owner;
  CSegId x2_locator;        // the ray starts at this owner locator; CSegId::Null() for the aim point
  CMaterialFilter x8_filter;
  TUniqueId x20_target;
  float x24_minInterval;    // the ray is cast every minInterval + randomInterval * Float()
  float x28_randomInterval;
  float x2C_timer;
  float x30_visibleTime;
  float x34_hiddenTime;
  bool x38_visible : 1;     // bit 0x80 of 0x38
};

#endif // _CVISUALKNOWLEDGE
