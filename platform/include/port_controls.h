#pragma once

#include <string>
#include <string_view>

// The port's keyboard and controller presets, over the Aurora PAD mappings
// (dolphin/pad.h). Binding itself is the overlay's Controls > Remap page.

namespace PortControls {

// Binds the port's default keyboard layout (WASD move, IJKL look, X/Z/C/V face
// buttons) to the port and turns its keyboard on. Doesn't save.
void ApplyDefaultKeyBindings(unsigned port);
// The Controls page's keyboard preset button: "classic" or "mouse" (Mouse &
// keyboard). Saves. False for any other name.
// `port` 0 only (the one keyboard port): false for any other port.
bool ApplyKeyPresetNamed(std::string_view name, unsigned port = 0);
// The Controls page's controller preset button: "gamecube", "remastered",
// "modern" or "southpaw". Saves. False for any other name.
// `port` is the pad port (0-3, players 1-4) whose controller is remapped; the twin-stick
// switch it sets is that player's (PortMouse::SetPlayerStickEnabled). Aurora ignores mappings
// of a virtual pad and of an empty port (still returns true: use PadPortInfo to tell the caller).
bool ApplyPadPresetNamed(std::string_view name, unsigned port = 0);
// The stick deadzone and trigger click-point sliders of the Controller settings
// page (nothing when no controller is on that port).
void DrawDeadZones(unsigned port = 0);

// The player (pad port 0-3) the F1 Remap page edits; clamped to 0-3. A later F1
// Multiplayer tab sets it before opening the page for a player.
int RemapPort();
void SetRemapPort(int port);

// What is on a pad port: nothing, an SDL virtual pad (Aurora keeps no mapping for it,
// so remaps and presets are no-ops) or a real controller.
enum class EPadPortState { kNone, kVirtual, kReal };
struct SPadPortInfo {
  EPadPortState state = EPadPortState::kNone;
  std::string name;
};
SPadPortInfo PadPortInfo(unsigned port);

} // namespace PortControls
