#pragma once

#include <string>
#include <string_view>

struct PADButtonMapping;
struct PADAxisMapping;

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
// Once per poll: a real controller newly on a pad port whose mappings are still its type's
// defaults gets that port's last controller preset (Aurora keeps mappings per controller
// model, so one connected after the preset was picked has none of it).
void ApplySavedPadPresets();
// `port`'s last controller preset over these defaults: what the Remap page shows with no
// controller on that port.
void ApplySavedPadPreset(unsigned port, PADButtonMapping* buttons, unsigned buttonCount, PADAxisMapping* axes,
                         unsigned axisCount);
// The last preset picked for `port` ("gamecube", "remastered", "modern", "southpaw"), or empty
// for none or a custom layout (settings.cfg `pad_preset`, `p2.pad_preset`...). Setting an
// unknown name clears it; neither applies anything.
std::string_view SavedPadPreset(unsigned port);
void SetSavedPadPreset(unsigned port, std::string_view name);
// The stick deadzone and trigger click-point sliders of the Controller settings
// page (nothing when no controller is on that port).
void DrawDeadZones(unsigned port = 0);
// Puts GameCube `button` (PAD_BUTTON_* / PAD_TRIGGER_*) on `native` (SDL_GamepadButton or
// PAD_NATIVE_BUTTON_TRIGGER_*) of the controller on `port`; a GameCube button already on it takes
// `button`'s old input. Saves. False when the port has no remappable controller.
bool BindPadButton(unsigned port, unsigned button, unsigned native);
// The controller mapping of `port` (console `padmap`), "A=A, B=X, ...", or "no controller".
std::string PadButtonsText(unsigned port);
// A GameCube button by label, case-insensitive ("a", "z", "start", "up" for D-pad Up): its
// PAD_BUTTON_* / PAD_TRIGGER_* bit, or -1.
int PadButtonForLabel(std::string_view label);
// A native input by its Remap name ("a", "lb", "lt", "none", case-insensitive) or SDL
// gamepad button number, for BindPadButton; -1 if unknown.
long long NativeForLabel(std::string_view label);

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
