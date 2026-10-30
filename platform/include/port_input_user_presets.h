#pragma once

// The player's own binding presets (issue #232): a snapshot of the Remap bindings
// that My presets... applies, beside the built-in layouts. A preset is one file per
// device family it covers in <user folder>/control_presets/,
// "<keyboard|pad|touch>-<slug>.toml": valid controls.toml text (the Default
// profile's rows of that family and the custom actions they name) with the Aurora
// and settings layers in "#! key = value" comment lines, which the TOML parser
// skips (touch has bindings only). A preset named N is the files of slug(N) that
// exist; the *All functions work on all of them, the per-family ones on one.

#include "port_input_bindings.h"

#include <string>
#include <string_view>
#include <vector>

namespace PortInputUserPresets {

// The Aurora PAD layer and PortDebug settings of one family, as plain numbers
// (the pure half; the store reads and writes the live ones).
struct Layout {
  PortInput::Family family = PortInput::Family::Keyboard;
  std::string name;
  // Pad: PADButtonMapping / PADAxisMapping, PADSignedNativeAxis flattened.
  struct PadButton {
    int native = 0, pad = 0;
    bool operator==(const PadButton&) const = default;
  };
  struct PadAxis {
    int axis = 0, sign = 0, button = 0, pad = 0;
    bool operator==(const PadAxis&) const = default;
  };
  // Keyboard: per slot, PADKeyButtonBinding / PADKeyAxisBinding.
  struct KeyButton {
    int slot = 0, key = 0, pad = 0;
    bool operator==(const KeyButton&) const = default;
  };
  struct KeyAxis {
    int slot = 0, key = 0, pad = 0, influence = 1;
    bool operator==(const KeyAxis&) const = default;
  };
  std::vector<PadButton> padButtons;
  std::vector<PadAxis> padAxes;
  std::vector<KeyButton> keyButtons;
  std::vector<KeyAxis> keyAxes;
  // PortDebug settings the family's presets write.
  bool twinStick = false;
  bool swapScanXray = false;          // pad
  bool mouseAim = false;              // keyboard
  int shift[3] = {-1, -1, -1};        // ShiftBinding slots; keyboard 0-1, pad 2
  std::vector<int> padAlt;            // pad: PadAltButton by bit
  std::vector<int> mouseActions;      // keyboard: MouseAction by button
  bool operator==(const Layout&) const = default;
};

// A preset: the layout and the user layer (one Default profile; its custom
// actions are the ones its rows name).
struct Preset {
  Layout layout;
  PortInput::UserBindings bindings;
};

// The file text, and back. Parse fails on a missing or wrong header, an unknown
// family or a controls.toml error; error (optional) says why.
std::string Serialize(const Preset& preset);
bool Parse(std::string_view text, Preset& out, std::string* error = nullptr);

// "<keyboard|pad>-<slug>.toml"'s slug: lowercase letters and digits, runs of
// anything else one '-'; "preset" when nothing is left.
std::string Slug(std::string_view name);
// A name as stored: control characters dropped, trimmed, at most kMaxName bytes.
constexpr size_t kMaxName = 48;
std::string CleanName(std::string_view name);

// The Default profile's rows and unbinds of a family out of all bindings, with the
// custom actions they name (a row whose definition is missing is left out). Uses
// the same predicate as PortInput::ClearFamily.
PortInput::UserBindings ExtractFamily(const PortInput::UserBindings& all, PortInput::Family family);
// Adds a preset's rows to the Default profile of `all` (made when absent), which the
// caller has cleared of the family. A custom action whose id is free is added, an
// equal one reused, and one whose id is taken by something else gets a new id.
void MergeFamily(PortInput::UserBindings& all, const PortInput::UserBindings& preset);

// ---- The store (reads and writes the live bindings and settings) ----

// Saved presets of a family, by name (case-insensitive order). Files that don't
// parse are skipped and logged once.
std::vector<std::string> List(PortInput::Family family);
// Snapshots the family's current bindings under a name; the same slug replaces.
bool Save(PortInput::Family family, std::string_view name);
// Replaces the family's own Default-profile bindings with a saved preset (name
// matched case-insensitively). False when there is none.
bool Apply(PortInput::Family family, std::string_view name);
bool Delete(PortInput::Family family, std::string_view name);

// A preset across families: the union of the names, grouped by slug.
struct Entry {
  std::string name;
  bool keyboard = false, pad = false, touch = false;
};
std::vector<Entry> ListAll();
// Saves the keyboard part always, the pad part when a controller is in port 1 and
// the touch part when touch has bindings; the slug's other parts are deleted first.
// True when the keyboard part was written.
bool SaveAll(std::string_view name);
// Applies every part that exists; true when any did.
bool ApplyAll(std::string_view name);
bool DeleteAll(std::string_view name);

} // namespace PortInputUserPresets
