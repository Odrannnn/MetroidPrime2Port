#pragma once

// The user's own bindings (<user>/controls.toml), layered over the profile the
// saved controls describe (PortInputDevices::BuildProfile). Plain text and data
// without SDL, so tests/port_input_bindings.cpp links it alone; the device layer
// installs SDL's key names with SetKeyNames.
//
// The file is a TOML subset: [[profile]] tables, each with [[profile.bind]]
// tables below it, string / integer / boolean / float values and arrays of
// strings. Comments (#) and blank lines are ignored; anything else the parser
// doesn't know is an error with its line number.
//
//   [[custom_action]]                # the player's own actions, before the profiles
//   id = 1                           # stable; bindings name it custom:<id>
//   name = "Look back"               # shown to the player
//   renamed = true                   # the player set the name: keep it
//   preset = "setting"               # what it does (a catalogue key)
//   arg = "invert_y"                 # the preset's option
//   mode = "toggle"                  # toggle, on, off, or "" for no mode
//   command = ""                     # "command" preset: the console line
//
//   [[profile]]
//   name = "Default"
//   match = ""                     # "", "guid:<32 hex>" or "type:<SDL gamepad type>"
//   replace = false                # true: start from no bindings at all
//   chord_window_ms = 40           # absent: keep the base profile's
//   unbind = ["pad_z@pad"]         # action@family: drop the base's bindings
//
//   [[profile.bind]]
//   action = "beam_wave"           # an ActionInfo key, or "custom:<id>"
//   inputs = ["pad:leftshoulder", "pad:dpright"]
//   any_order = false
//   trigger = "press"              # press, tap, hold, double_tap, toggle
//   tap_ms = 200
//   hold_ms = 300
//   double_ms = 250
//   turbo_hz = 0
//   scale = 1.0
//   invert = false
//   contexts = ["gameplay"]        # absent: everywhere
//
// Only fields that differ from Binding's defaults are written.
//
// Input text (the threshold suffix "@<percent>" is written only when it isn't 50):
//   key:<name or scancode>          key:space, key:42
//   mouse:<button>                  left, middle, right, x1, x2: SDL's button state
//   mouse:aim:<button>              the captured gameplay buttons (code 0..)
//   mouse:menu:<button>             the menu A/B buttons out of aim (code 16..)
//   mouse:held:<button>             the beam shift / turbo slot read (code 24..)
//   wheel:up|down|left|right
//   motion:x|y  (signed)  motion:x+|x-|y+|y-
//   pad:<SDL gamepad button name>   a, b, x, y, back, guide, start, leftstick,
//                                   rightstick, leftshoulder, rightshoulder, dpup,
//                                   dpdown, dpleft, dpright, misc1, paddle1..4,
//                                   touchpad, misc2..misc6; or pad:<number>
//   axis:<name>[+|-]                leftx, lefty, rightx, righty, lefttrigger,
//                                   righttrigger; or axis:<number>
//   touch:<id>
//   touchaxis:<id>[+|-]
//   gyro:pitch|yaw|roll[+|-]
//
// Contexts: gameplay, morphball, map, menu, scan, layer1..layer4, all.
// Families (for unbind and the overlay rule): keyboard (keys, mouse), pad (pad
// buttons and axes, gyro), touch.
//
// Overlay rule: for each action a profile binds, the base bindings of that
// action whose trigger input (the last one) is in a family the profile binds
// it from are dropped, then the profile's bindings are appended. So rebinding
// the pad's Fire leaves the keyboard's Fire alone. unbind drops one action's
// base bindings in one family without adding any.
//
// The Remap page's device views map onto these profiles by family: Keyboard &
// mouse, Touch and All controllers are the match "" profile restricted to one
// family; a controller kind or model is its own profile (pad family).
//
// Profile choice: the profile with match "" (the first one, if several) always
// applies; then the first profile matching the active pad's GUID, else the first
// matching its type, is layered on top of it.

#include "port_input_engine.h"

#include <string>
#include <string_view>
#include <vector>

namespace PortInput {

enum class Family : uint8_t { Keyboard, Pad, Touch };
Family FamilyOf(Device device);

struct Unbind {
  Action action = Action::None;
  Family family = Family::Keyboard;
  bool operator==(const Unbind&) const = default;
};

// One of the player's own actions: an Action value kCustomBase + id, and what it
// does. The catalogue of presets lives with the overlay that runs them.
struct CustomActionDef {
  uint16_t id = 0;          // stable; bindings name it custom:<id>
  std::string name;         // shown to the player
  bool renamed = false;     // the player set the name; keep it when the options change
  std::string preset;       // catalogue key, e.g. "setting", "phazon_beam", "warp", "command"
  std::string arg;          // the preset's option (setting key, item, "MLVL:MREA", slot, ...)
  std::string mode;         // "toggle", "on", "off" or "" (presets that have no mode)
  std::string command;      // "command" preset: the console line
  bool operator==(const CustomActionDef&) const = default;
};

struct UserProfile {
  std::string name;
  std::string match; // "", "guid:<hex>", "type:<name>"
  bool replace = false;
  uint16_t chordWindowMs = 0; // 0: keep the base's
  std::vector<Unbind> unbind;
  std::vector<Binding> bindings;
  bool operator==(const UserProfile&) const = default;
};

struct UserBindings {
  std::vector<CustomActionDef> customActions;
  std::vector<UserProfile> profiles;
  bool operator==(const UserBindings&) const = default;
};

// The id a new custom action gets: the highest in use plus one, at least 1.
uint16_t NextCustomId(const UserBindings& bindings);
// Deletes a custom action and everything that names it: its definition, and every
// binding and unbind of it in every profile.
void RemoveCustomAction(UserBindings& bindings, uint16_t id);

// Key names: name(scancode) returns "" for none; parse(name) returns -1 for an
// unknown name. Without them (tests), keys are written and read as numbers.
using KeyNameFn = std::string (*)(int scancode);
using KeyParseFn = int (*)(std::string_view name);
void SetKeyNames(KeyNameFn name, KeyParseFn parse);

std::string InputToText(const Input& in);
bool InputFromText(std::string_view text, Input& out);
std::string ContextsToText(uint16_t contexts); // e.g. "gameplay,map"; "all" for kCtxAll
bool ContextFromText(std::string_view text, uint16_t& out);

// error (optional) gets "line N: what". On failure out is left unchanged.
// Rows for the retired beam_shift action (now default chords) are skipped;
// dropped (optional) gets how many.
bool ParseUserBindings(std::string_view text, UserBindings& out, std::string* error = nullptr, int* dropped = nullptr);
std::string SerializeUserBindings(const UserBindings& bindings);

// The profiles that apply for a pad (guid: 32 lowercase hex digits, type: SDL's
// gamepad type string; either may be empty for no pad): the default first, then
// the pad's, each null when absent.
struct Selection {
  const UserProfile* base = nullptr;
  const UserProfile* pad = nullptr;
};
Selection Select(const UserBindings& bindings, std::string_view guid, std::string_view type);

// Applies one user profile over a profile (the overlay rule above).
void Overlay(Profile& profile, const UserProfile& user);

// Family-filtered editing, for the Remap page's per-device views (the Default
// profile holds the keyboard, touch and all-controller views, one family each).
// The family of a binding is its trigger input's (the last one).
Family BindingFamily(const Binding& b);
// Whether the profile overrides (binds or unbinds) an action in a family; if not,
// the action inherits the built-in bindings.
bool Overridden(const UserProfile& user, Action action, Family family);
// A profile's bindings of an action in a family (the profile's own list).
std::vector<Binding> InFamily(const std::vector<Binding>& bindings, Action action, Family family);
// Before editing an inherited action: copies the inherited bindings of (action,
// family) into the profile, unless it overrides them already, since any binding
// replaces all of the inherited ones.
void Materialize(UserProfile& user, Action action, Family family, const Profile& inherited);
// Adds a binding, dropping an unbind it supersedes.
void AddUserBinding(UserProfile& user, const Binding& b);
// Removes the binding at an index; when it was the action's last one in its
// family, an unbind keeps the built-in ones from coming back.
void RemoveUserBinding(UserProfile& user, size_t index);
// The action inherits again in that family (drops its bindings and unbinds).
void Revert(UserProfile& user, Action action, Family family);
// Every override and unbind in one family (a reset to the built-in controls).
bool IsFamilyRow(const Binding& b, Family family);
void ClearFamily(UserProfile& user, Family family);

// The same physical input: mouse buttons match across their code runs (aim, key
// slot, menu, held), and the press threshold is ignored.
bool SamePhysicalInput(const Input& a, const Input& b);
// Makes the inputs of the `isRow` bindings do only those in `ctx`: every other
// binding on the same physical inputs (the same chord, for a chord) gets `ctx`
// blocked, which keeps its rank and what it does elsewhere. everywhereOnly: only
// bindings for every context are blocked, so ones scoped under Advanced still apply.
void BlockRowInputs(Profile& profile, bool (*isRow)(const Binding&), uint16_t ctx, bool everywhereOnly);

// The Morph Ball rows (issue #167): each presses a GameCube button, only in Morph
// Ball. Bound in a family, a row replaces that button in the ball for the family;
// unbound, the button's own inputs do it. Touch has them bound by default on the
// overlay's Morph Ball page (kTouchBallControls); its layout editor buttons can take
// them too.
struct BallRow {
  Action row;
  Action game;
  const char* hint;  // the Remap page's note beside the row
  const char* touch; // the overlay's label for a control doing the row
};
inline constexpr BallRow kBallRows[] = {
    {Action::BallBomb, Action::PadA, "A when unset", "Bomb"},
    {Action::BallPowerBomb, Action::PadY, "Y when unset", "P.Bomb"},
    {Action::BallBoost, Action::PadB, "B when unset", "Boost"},
    {Action::BallSpider, Action::PadR, "R when unset", "Spider"},
    {Action::BallUnmorph, Action::PadX, "X when unset", "Unmorph"},
};
// The touch overlay's Morph Ball page buttons and their default rows (touch codes
// kTouchBallBase.., TouchControlsView's C_BALL + i). They press no GameCube button
// of their own (TouchControlBound).
struct TouchBallControl {
  int control;
  Action row;
};
inline constexpr TouchBallControl kTouchBallControls[] = {
    {40, Action::BallBomb},   {41, Action::BallBoost},   {42, Action::BallPowerBomb},
    {43, Action::BallSpider}, {44, Action::BallUnmorph},
};
// The row of `a`, or nullptr; the row that presses `game`, or nullptr.
const BallRow* FindBallRow(Action a);
const BallRow* BallRowForGame(Action game);
// Before a poll: scopes the Morph Ball rows to Morph Ball (a row with layers to
// Morph Ball while one is held) and blocks Morph Ball for the other bindings on
// their keyboard, mouse and controller inputs (a row with layers blocks nothing: it
// outranks them while one is held), and blocks Morph Ball for its game button's
// bindings in each keyboard or controller family that binds the row (not with
// layers). A row that isn't bound changes nothing.
void ApplyBallButtons(Profile& profile);

// The touch overlay's own buttons (issue #92): the layout editor adds up to
// kTouchButtonSlots, each with one action, on touch codes kTouchButtonBase + slot. They live in the touch layout string
// (TouchControlsView owns it), not in controls.toml, as entries
//   k<slot>:dx,dy,scale,opacity,hidden,<action key>[,page]
// among the stock controls' `id:dx,dy,scale[,opacity[,hidden]]` ones, ';'
// separated (page: the overlay's, 0 gameplay when left out, 1 Morph Ball, 2 both).
// Entries with a bad slot, a field count other than 6 or 7 or an action a button
// can't take are skipped; a repeated slot keeps its last valid entry.
constexpr int kTouchButtonSlots = 64;
constexpr int kTouchButtonBase = 64;
struct TouchButton {
  int slot = 0;
  Action action = Action::None;
  bool operator==(const TouchButton&) const = default;
};
// Digital actions that do something from a touch button: not None, the analog
// and look ones, the layer holds (only controls.toml's layered bindings use
// them) or the Menu rows (inert while touch is active). A Morph Ball row works only
// in the ball, as everywhere (ApplyBallButtons).
bool TouchButtonAction(Action a);
std::vector<TouchButton> ParseTouchButtons(std::string_view layout); // by slot
// Appends a Press binding of each button's action on its touch code.
void BindTouchButtons(Profile& profile, std::string_view layout);

} // namespace PortInput
