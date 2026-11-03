#pragma once

// The player's own actions (controls.toml [[custom_action]]): the catalogue of
// things one can be bound to, and running one when its binding is pressed. The
// F1 Remap page builds them, PortInputDevices runs them, and every name here is
// the player-facing English, never console syntax.

#include "port_input_bindings.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace PortCustomActions {

// What the player picks after the preset: a setting, an item, a slot, text, a
// text language or a room.
enum class ArgKind : uint8_t { None, Setting, Item, Slot, Text, Language, Room };

struct Preset {
  const char* key;      // CustomActionDef::preset
  const char* category; // picker group: "Settings", "Cheats", "Travel", "Save states", "Display", "Advanced"
  const char* label;    // picker text
  const char* help;     // one-line tooltip
  bool cheat;           // refused in the Original experience, like the Cheats page
  ArgKind arg;
  bool hasMode;         // on / off / toggle
};

std::span<const Preset> Presets();
const Preset* FindPreset(std::string_view key);

// A setting a `setting` action can flip: its port_settings.ini key (or the input
// settings' key), the label and section the F1 page shows, and the port's own
// getter and setter.
struct BoolSetting {
  const char* key;
  const char* label;
  const char* section;
  bool (*get)();
  void (*set)(bool);
};

std::span<const BoolSetting> BoolSettings();
const BoolSetting* FindBoolSetting(std::string_view key);

// The languages a `language` action can pick: the code the def stores (and the
// console takes) and the name the picker shows.
struct Language {
  const char* code;
  const char* label;
};
std::span<const Language> Languages();

// The worlds and rooms a `warp` action can go to, by their in-game names. The
// def stores RoomArg's "MLVL:MREA" (hex asset ids; MREA 0 is the world's start).
struct Room {
  uint32_t area; // MREA asset id
  std::string name;
};
struct World {
  uint32_t id; // MLVL asset id
  std::string name;
  std::vector<Room> rooms; // by name
  bool loaded = false;     // rooms is complete
};
// Starts, or keeps going, reading every world's room names from the disc. Call
// it on the main thread each frame they are shown; the lists fill as they load.
void LoadRooms();
std::span<const World> Worlds();
std::string RoomArg(uint32_t world, uint32_t area);
bool ParseRoomArg(std::string_view arg, uint32_t& world, uint32_t& area);

// The name a def shows, built from its preset, option and mode.
std::string AutoName(const PortInput::CustomActionDef& def);
// def.name when the player named it, else AutoName.
std::string DisplayName(const PortInput::CustomActionDef& def);

// Runs it now, on its binding's press (and a "hold" one again on the release, which
// switches it back). Returns the short note to show the player
// ("Invincibility on", "Saved to slot 3", "Cheats are off in the Original
// experience"), or "" for none.
std::string Run(const PortInput::CustomActionDef& def);

} // namespace PortCustomActions
