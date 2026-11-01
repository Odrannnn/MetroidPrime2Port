#pragma once

// F1 Controls > Remap: the user's bindings (controls.toml, port_input_bindings.h)
// as profiles of bindings, each an action on an input or a chord of inputs with a
// trigger, turbo and contexts; a pad profile follows a controller (GUID) or a kind
// of controller (SDL type).

#include "port_input_bindings.h"
#include "port_input_engine.h"

#include <string>
#include <string_view>

namespace PortInputRemap {

void Draw();
// The controller combo of pad port `port` (0-3): which controller that player plays with. Remap draws
// Player 1's, the Multiplayer page all four. Overlay thread.
void DrawPlayerController(unsigned port, const char* label);
// Remap's Presets... button: replaces the family's own Default-profile bindings
// with the named PortControls preset ("classic", "mouse"; "gamecube", "remastered",
// "modern", "southpaw") and its Morph Ball rows. Overlay thread.
void ApplyPreset(PortInput::Family family, std::string_view id);
// The same for a player: port 0 as above; ports 1-3 (pad family only) write just that controller's
// pad mapping (PortControls::ApplyPadPresetNamed), not the per-device engine rows.
void ApplyPreset(PortInput::Family family, std::string_view id, unsigned port);
// An input capture is running: the overlay's pad navigation stays off.
bool Capturing();
// The inputs bound to `action` as Remap names them ("F1 or Back + Start"); empty
// when nothing is. Game thread only.
std::string BindingsText(PortInput::Action action, const char* separator = " or ");

} // namespace PortInputRemap
