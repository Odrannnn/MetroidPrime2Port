#pragma once

#include <cstdint>

class CScriptMsg;
class CStateManager;

// Seamless pickups (PortOverlay::SeamlessPickups, docs/SETTINGS.md): what a pickup's script chain
// would present - the control lock, the pausing message box, the acquisition cinematic - is marked
// when the pickup is touched (CScriptPickup::Touch) and dropped where it arrives, which may be
// seconds later (timers delay it, a cinematic runs first). tools/pickup_presentation.py checks the
// same walk against every pickup on the disc.
namespace PortPickupPresentation {

enum Kind {
  kControlHint, // CScriptControlHint: its next Increment adds no hint
  kMemo,        // CScriptHUDMemo: its next message box shows as a non-pausing memo
  kCamera,      // CScriptCamera: CMFGame skips the cinematic it starts, as the skip button does
};

// A mark lasts kMarkSeconds of play time (which stops in cinematics and menus) and is taken once:
// a chain that never delivers does not leave it waiting for a later, unrelated message.
const double kMarkSeconds = 30.0;
void Mark(Kind kind, uint16_t uniqueId);
bool Take(Kind kind, uint16_t uniqueId);

// A map station's "Pickup Acquired" message box: its sender (the "HUD Activation" timer, after the
// download cinematic) also sends Action to the map-station special function in the same state,
// directly or through a timer it reaches (the frame delay). Seamless pickups show it as a memo too;
// tools/pickup_presentation.py checks the rule against every map station on the disc.
bool IsMapStationMemo(CStateManager& mgr, const CScriptMsg& msg);

} // namespace PortPickupPresentation
