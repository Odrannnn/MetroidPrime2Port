#include "port_mobile.h"
#include "port_input_remap_ui.h"
#include "port_strings.h"

#include "port_console.h"
#include "port_controls.h"
#include "port_custom_actions.h"
#include "port_input_bindings.h"
#include "port_input_capture.h"
#include "port_input_devices.h"
#include "port_input_user_presets.h"
#include "port_overlay.h"
#include "port_savestate.h"

#include <dolphin/pad.h>
#include <imgui.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace PortInputRemap {
namespace {

using PortInput::Action;
using PortInput::Binding;
using PortInput::Device;
using PortInput::Input;
using PortInput::Trigger;
using PortInput::UserBindings;
using PortInput::UserProfile;

constexpr uint64_t kCaptureTimeoutMs = 8000;
constexpr uint64_t kCaptureStaleMs = 250; // the page wasn't drawn for this long

struct STriggerInfo {
  Trigger trigger;
  const char* label;
};
constexpr STriggerInfo kTriggers[] = {
    {Trigger::Press, "Press (while held)"},
    {Trigger::Tap, "Tap"},
    {Trigger::Hold, "Hold"},
    {Trigger::DoubleTap, "Double tap"},
    {Trigger::Toggle, "Toggle"},
};

struct SContextInfo {
  uint16_t bit;
  const char* label;
};
constexpr SContextInfo kContexts[] = {
    {PortInput::kCtxGameplay, "Gameplay"}, {PortInput::kCtxMorphBall, "Morph Ball"},
    {PortInput::kCtxMap, "Map"},           {PortInput::kCtxMenu, "Menus"},
    {PortInput::kCtxScan, "Scan Visor"},   {PortInput::kCtxLayer1, "Layer 1"},
    {PortInput::kCtxLayer2, "Layer 2"},    {PortInput::kCtxLayer3, "Layer 3"},
    {PortInput::kCtxLayer4, "Layer 4"},
};

constexpr const char* kFamilies[] = {"Keyboard & mouse", "Controller", "Touch"};

// The touch controls Record can't reach (the overlay hides while F1 is open), as
// "touch:" names and the editor's labels. The sticks, D-pad and menu button
// aren't remappable, so they're left out.
struct STouchInfo {
  const char* name;
  const char* label;
};
constexpr STouchInfo kTouchControls[] = {
    {"a", "A"},
    {"b", "B"},
    {"x", "X"},
    {"y", "Y"},
    {"l", "L"},
    {"r", "R"},
    {"z", "Z"},
    {"visor", "Visor"},
    {"beam", "Beam"},
    {"start", "Start"},
    {"map", "Map"},
    {"eye", "Hide controls"},
    {"turbo", "Turbo (not in this port yet)"},
    {"jump", "Twin stick: Jump"},
    {"fire", "Twin stick: Fire"},
    {"morph", "Twin stick: Morph"},
    {"missile", "Twin stick: Missile"},
    {"lt", "Twin stick: LT Lock"},
    {"lb", "Twin stick: LB Jump"},
    {"rt", "Twin stick: RT Fire"},
    {"rb", "Twin stick: RB Missile"},
    {"tz", "Twin stick: Map (Z)"},
    {"tturbo", "Twin stick: Turbo (not in this port yet)"},
    {"beam_shift", "Twin stick: Beam (held, not in this port yet)"},
    {"map_tap", "Minimap tap"},
    {"bbomb", "Morph Ball: Bomb"},
    {"bboost", "Morph Ball: Boost"},
    {"bpbomb", "Morph Ball: Power Bomb"},
    {"bspider", "Morph Ball: Spider"},
    {"bunmorph", "Morph Ball: Unmorph"},
};

UserBindings sWork;     // what the page edits; written through on every change
bool sLoaded = false;
uint64_t sSeenVersion = 0; // the bindings version sWork matches
// What the Controller column edits: "pads" (the Default profile's controller
// bindings) or a controller profile's match ("type:..." / "guid:..."). Keyboard
// and touch always edit the Default profile.
std::string sPadDevice = "pads";
#if defined(PORT_MOBILE)
constexpr bool kShowTouch = true;
#else
constexpr bool kShowTouch = false; // the touch overlay is the phones'
#endif
constexpr int kEditNone = -1;
constexpr int kEditNew = -2;
constexpr int kEditInherited = -3; // sEditOrig, a built-in binding, is being changed
int sEditing = kEditNone;          // else a binding index in sEditFamily's profile
PortInput::Family sEditFamily = PortInput::Family::Keyboard; // where the edited binding is
Binding sEditOrig;
Binding sDraft;
char sInputsText[256] = {};
std::string sError;
char sFilter[64] = {};
bool sOnlyBound = false;

struct SCapture {
  bool active = false;
  bool armed = false; // everything was released once since the start
  uint64_t startMs = 0;
  std::vector<Input> chord;
} sCapture;
// sCapture.active and the last frame the page was drawn, for Capturing() on the
// game thread.
std::atomic<bool> sCaptureActive{false};
std::atomic<uint64_t> sLastDrawMs{0};

void EndCapture() {
  sCapture.active = false;
  sCaptureActive.store(false, std::memory_order_relaxed);
}

const char* TriggerLabel(Trigger t) {
  for (const STriggerInfo& info : kTriggers) {
    if (info.trigger == t) return info.label;
  }
  return "?";
}

std::string InputsToText(const Binding& b) {
  std::string out;
  for (int i = 0; i < b.count; ++i) {
    if (i != 0) out += " + ";
    out += PortInput::InputToText(b.inputs[size_t(i)]);
  }
  return out;
}

// Whether an input ("kind:") starts at s[i]: a known kind at the start or after a
// space, ',' or '+'. Key names hold spaces, ',' and '+' ("Keypad +"), so inputs are
// split where the next one starts rather than at a separator character.
bool InputStartsAt(const std::string& s, size_t i) {
  static constexpr const char* kKinds[] = {"key:",  "mouse:", "wheel:",     "motion:", "pad:",
                                           "axis:", "touch:", "touchaxis:", "gyro:"};
  if (i > 0 && s[i - 1] != ' ' && s[i - 1] != ',' && s[i - 1] != '+') return false;
  for (const char* kind : kKinds) {
    if (s.compare(i, std::strlen(kind), kind) == 0) return true;
  }
  return false;
}

std::string TrimSpaces(const std::string& s) { return port::Trim(s, " \t"); }

// "a + b + c" (or comma separated) into a binding's inputs.
bool InputsFromText(const char* text, Binding& b) {
  Binding parsed = b;
  parsed.count = 0;
  const std::string s(text);
  std::vector<size_t> starts;
  for (size_t i = 0; i < s.size(); ++i) {
    if (InputStartsAt(s, i)) starts.push_back(i);
  }
  if (starts.empty() || !TrimSpaces(s.substr(0, starts[0])).empty()) return false;
  for (size_t n = 0; n < starts.size(); ++n) {
    const bool last = n + 1 == starts.size();
    std::string part = TrimSpaces(s.substr(starts[n], last ? std::string::npos : starts[n + 1] - starts[n]));
    // Before another input, drop one separator: a ',' or a '+' after a space (a
    // '+' right after the name is a directed axis's sign, as in axis:leftx+).
    if (!last && !part.empty() &&
        (part.back() == ',' || (part.back() == '+' && part.size() > 1 && part[part.size() - 2] == ' '))) {
      part = TrimSpaces(part.substr(0, part.size() - 1));
    }
    Input in;
    if (parsed.count >= PortInput::kMaxChord || !PortInput::InputFromText(part, in)) return false;
    parsed.inputs[parsed.count++] = in;
  }
  b = parsed;
  return true;
}

void Commit() {
  if (!PortInputDevices::SetUserBindings(sWork)) {
    sError = "Couldn't write controls.toml.";
  } else {
    sError.clear();
    sSeenVersion = PortInputDevices::UserBindingsVersion();
  }
}

// An action's name for the player: the player's own actions carry theirs, the
// engine's carry the label. A custom id with no definition shows nothing.
std::string ActionLabel(Action a) {
  if (!PortInput::IsCustom(a)) return std::string(PortInput::Info(a).label);
  const uint16_t id = PortInput::CustomId(a);
  for (const PortInput::CustomActionDef& def : sWork.customActions) {
    if (def.id == id) return PortCustomActions::DisplayName(def);
  }
  return std::string();
}

// The player's definition of a custom action, or null.
const PortInput::CustomActionDef* CustomDef(Action a) {
  if (!PortInput::IsCustom(a)) return nullptr;
  const uint16_t id = PortInput::CustomId(a);
  for (const PortInput::CustomActionDef& def : sWork.customActions) {
    if (def.id == id) return &def;
  }
  return nullptr;
}

// A user profile by match, or null (reading never creates one).
UserProfile* Find(const std::string& match) {
  for (UserProfile& p : sWork.profiles) {
    if (p.match == match) return &p;
  }
  return nullptr;
}

// The profile for a match, made on first edit. The Default one comes first.
UserProfile& Ensure(const std::string& match, const std::string& name) {
  if (UserProfile* p = Find(match)) return *p;
  UserProfile p;
  p.name = match.empty() ? std::string("Default") : name;
  p.match = match;
  sWork.profiles.push_back(p);
  return sWork.profiles.back();
}

// What one family's column edits.
struct SView {
  PortInput::Family family = PortInput::Family::Keyboard;
  std::string match; // the profile's match: "" for the Default profile
  std::string name;  // for a profile made on first edit
  PortInput::Profile inherited; // what the action falls back to
};

SView MakeView(PortInput::Family family) {
  SView v;
  v.family = family;
  if (family == PortInput::Family::Pad && sPadDevice != "pads") {
    v.match = sPadDevice;
    if (const UserProfile* p = Find(sPadDevice)) v.name = p->name;
  }
  PortInputDevices::BuildBaseProfile(v.inherited);
  // A kind or model sits on top of the Default profile's own overrides.
  if (!v.match.empty()) {
    if (const UserProfile* def = Find("")) PortInput::Overlay(v.inherited, *def);
  }
  return v;
}

UserProfile& EditedProfile(const SView& v) { return Ensure(v.match, v.name); }

// The three columns' views, indexed by Family.
struct SViews {
  SView v[3];
  const SView& operator[](PortInput::Family f) const { return v[int(f)]; }
};

SViews MakeViews() {
  using PortInput::Family;
  return {{MakeView(Family::Keyboard), MakeView(Family::Pad), MakeView(Family::Touch)}};
}

struct SDeviceEntry {
  std::string key;
  std::string label;
};

// What the Controller column can edit.
std::vector<SDeviceEntry> PadEntries() {
  std::vector<SDeviceEntry> out = {{"pads", "All controllers"}};
  const auto listed = [&](const std::string& key) {
    return std::any_of(out.begin(), out.end(), [&](const SDeviceEntry& e) { return e.key == key; });
  };
  for (const PortInputDevices::SPadInfo& pad : PortInputDevices::ConnectedPads()) {
    const std::string type = "type:" + pad.type;
    const std::string guid = "guid:" + pad.guid;
    if (!pad.type.empty() && !listed(type)) {
      out.push_back({type, "Only " + pad.type + " controllers"});
    }
    if (!pad.guid.empty() && !listed(guid)) {
      out.push_back({guid, "Only " + pad.name + " (this model)"});
    }
  }
  for (const UserProfile& p : sWork.profiles) {
    if (p.match.empty() || listed(p.match)) continue;
    out.push_back({p.match, (p.name.empty() ? p.match : p.name) + " (not connected)"});
  }
  return out;
}

// Whether every input of a binding comes from one family (keyboard and mouse are one).
bool OneFamily(const Binding& b) {
  for (int i = 1; i < b.count; ++i) {
    if (PortInput::FamilyOf(b.inputs[size_t(i)].device) != PortInput::FamilyOf(b.inputs[0].device)) return false;
  }
  return true;
}

void StartCapture() {
  sCapture = SCapture{};
  sCapture.active = true;
  sCapture.startMs = SDL_GetTicks();
  sCaptureActive.store(true, std::memory_order_relaxed);
}

void FinishCapture() {
  sDraft.count = 0;
  for (const Input& in : sCapture.chord) {
    if (sDraft.count < PortInput::kMaxChord) sDraft.inputs[sDraft.count++] = in;
  }
  std::snprintf(sInputsText, sizeof(sInputsText), "%s", InputsToText(sDraft).c_str());
  EndCapture();
}

// Records every input pressed until all are released again: one input, or a
// chord in press order (the last one is the trigger). The first input picks the
// device: a chord's other inputs come from the same one. Esc cancels.
void UpdateCapture() {
  using PortInput::Family;
  if (!sCapture.active) return;
  PortInput::RawState raw;
  PortInputDevices::ReadRaw(raw);
  if (raw.keys.test(SDL_SCANCODE_ESCAPE) || SDL_GetTicks() - sCapture.startMs > kCaptureTimeoutMs) {
    EndCapture();
    return;
  }
  std::vector<Input> down = PortInputCapture::DownInputs(raw);
  const bool started = !sCapture.chord.empty();
  const Family family = started ? PortInput::FamilyOf(sCapture.chord[0].device) : Family::Keyboard;
  const ImGuiIO& io = ImGui::GetIO();
  if (sCapture.armed && (!started || family == Family::Keyboard) && (io.MouseWheel != 0.f || io.MouseWheelH != 0.f)) {
    const bool vertical = io.MouseWheel != 0.f;
    const float v = vertical ? io.MouseWheel : io.MouseWheelH;
    sCapture.chord.push_back({Device::MouseWheel, int8_t(v > 0.f ? 1 : -1), 50, uint16_t(vertical ? 0 : 1)});
    FinishCapture();
    return;
  }
  if (!sCapture.armed) {
    sCapture.armed = down.empty();
    return;
  }
  if (PortInputCapture::Accumulate(sCapture.chord, down)) FinishCapture();
}

bool sAdvancedSync = false; // set the editor's Advanced node from the draft on the next draw
// The editor is drawn under the row it edits (a new binding: under its action's
// last row). When that row isn't on screen (filtered, section closed), it goes
// above the table instead.
Action sEditAction = Action::None;
bool sEditRowShown = true; // the last table draw reached the edited row

// Whether a binding uses anything the editor keeps under Advanced.
bool UsesAdvanced(const Binding& b) {
  return b.trigger != Trigger::Press || b.turboHz != 0 || b.contexts != PortInput::kCtxAll || (b.IsChord() && b.anyOrder) ||
         b.scale != 1.f || b.invert;
}

void BeginEdit(int index, const Binding& b, PortInput::Family family) {
  sEditing = index;
  sEditFamily = family;
  sEditAction = b.action;
  sEditRowShown = true;
  sDraft = b;
  sAdvancedSync = true;
  std::snprintf(sInputsText, sizeof(sInputsText), "%s", InputsToText(b).c_str());
  EndCapture();
}

// Inputs are shown as a device icon and a name, not as their "kind:" text.
enum class Icon { Keyboard, Mouse, Pad, Touch };

Icon IconOf(Device d) {
  switch (d) {
  case Device::MouseButton:
  case Device::MouseWheel:
  case Device::MouseMotion: return Icon::Mouse;
  case Device::PadButton:
  case Device::PadAxis:
  case Device::Gyro: return Icon::Pad;
  case Device::Touch:
  case Device::TouchAxis: return Icon::Touch;
  default: return Icon::Keyboard;
  }
}

const char* IconName(Icon icon) {
  switch (icon) {
  case Icon::Mouse: return "mouse";
  case Icon::Pad: return "controller";
  case Icon::Touch: return "touch";
  default: return "keyboard";
  }
}

// An icon in the s x s square at p.
void DrawIcon(ImDrawList* dl, ImVec2 p, float s, Icon icon, ImU32 col) {
  const float t = std::max(1.f, s * 0.09f);
  const auto at = [&](float x, float y) { return ImVec2(p.x + s * x, p.y + s * y); };
  switch (icon) {
  case Icon::Keyboard: // a box with two rows of keys and a space bar
    dl->AddRect(at(0.f, 0.2f), at(1.f, 0.82f), col, s * 0.12f, 0, t);
    for (int row = 0; row < 2; ++row) {
      for (int i = 0; i < 4; ++i) {
        const ImVec2 c = at(0.2f + 0.2f * float(i), 0.37f + 0.15f * float(row));
        const float k = s * 0.055f;
        dl->AddRectFilled(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), col);
      }
    }
    dl->AddLine(at(0.3f, 0.68f), at(0.7f, 0.68f), col, t);
    break;
  case Icon::Mouse: // a capsule split into two buttons
    dl->AddRect(at(0.22f, 0.04f), at(0.78f, 0.96f), col, s * 0.28f, 0, t);
    dl->AddLine(at(0.22f, 0.42f), at(0.78f, 0.42f), col, t);
    dl->AddLine(at(0.5f, 0.04f), at(0.5f, 0.42f), col, t);
    break;
  case Icon::Pad: // a pad body with a D-pad and two buttons
    dl->AddRect(at(0.f, 0.24f), at(1.f, 0.8f), col, s * 0.24f, 0, t);
    dl->AddLine(at(0.17f, 0.52f), at(0.41f, 0.52f), col, t);
    dl->AddLine(at(0.29f, 0.4f), at(0.29f, 0.64f), col, t);
    dl->AddCircleFilled(at(0.66f, 0.46f), s * 0.07f, col);
    dl->AddCircleFilled(at(0.8f, 0.58f), s * 0.07f, col);
    break;
  case Icon::Touch: // a touch point with a ripple
    dl->AddCircleFilled(at(0.5f, 0.5f), s * 0.14f, col);
    dl->AddCircle(at(0.5f, 0.5f), s * 0.3f, col, 0, t);
    dl->AddCircle(at(0.5f, 0.5f), s * 0.46f, col, 0, t * 0.7f);
    break;
  }
}

// An input's name without its "kind:" (the icon says it).
// Pad buttons by raw bit (SDL_GamepadButton, then the triggers' pulls and clicks).
constexpr const char* kPadButtonLabels[] = {
    "A",       "B",        "X",        "Y",        "Back",     "Guide",    "Start",     "Left stick",
    "Right stick", "LB",   "RB",       "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Share",
    "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", "Touchpad", "Misc 2",   "Misc 3",    "Misc 4",
    "Misc 5",  "Misc 6",   "LT",       "RT",       "LT click", "RT click",
};

// An axis or half of one, as on the controller.
std::string PadAxisName(const Input& in) {
  static constexpr const char* kSticks[] = {"Left stick", "Left stick", "Right stick", "Right stick"};
  if (in.code < 4) {
    const bool x = in.code % 2 == 0;
    const char* way = in.dir == 0 ? (x ? " X" : " Y") : x ? (in.dir > 0 ? " right" : " left")
                                                          : (in.dir > 0 ? " down" : " up");
    return std::string(kSticks[in.code]) + way;
  }
  if (in.code == 4) return "LT";
  if (in.code == 5) return "RT";
  return "Axis " + std::to_string(in.code);
}

// Names without the text form's kind prefix, as the device labels them.
std::string InputName(const Input& in, bool includeMouseMode = false) {
  const std::string text = PortInput::InputToText(in);
  const size_t at = text.find('@');
  const std::string threshold = at == std::string::npos ? std::string() : " " + text.substr(at + 1) + "%";
  if (in.device == Device::PadButton && in.code < std::size(kPadButtonLabels)) {
    return kPadButtonLabels[in.code] + threshold;
  }
  if (in.device == Device::PadAxis) return PadAxisName(in) + threshold;
  const size_t colon = text.find(':');
  std::string name = colon == std::string::npos ? text : text.substr(colon + 1);
  switch (in.device) {
  case Device::MouseButton: {
    // aim:/menu:/held: pick when the click counts.
    static constexpr std::pair<const char*, const char*> kWhen[] = {
        {"aim:", " (aiming)"}, {"menu:", " (menus)"}, {"held:", " (held)"}};
    for (const auto& [prefix, suffix] : kWhen) {
      if (name.starts_with(prefix)) {
        name = name.substr(std::strlen(prefix));
        if (includeMouseMode) name += suffix;
      }
    }
    if (!includeMouseMode) {
      if (name == "left") name = "LMB";
      else if (name == "right") name = "RMB";
      else if (name == "middle") name = "MMB";
    }
    break;
  }
  case Device::MouseWheel: name = "wheel " + name; break;
  case Device::MouseMotion: name = "move " + name; break;
  case Device::Gyro: name = "gyro " + name; break;
  case Device::TouchAxis: name = "stick " + name; break;
  case Device::Touch:
    for (const STouchInfo& t : kTouchControls) {
      if (name == t.name) return t.label;
    }
    break;
  default: break;
  }
  if (!name.empty()) name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
  return name;
}

// The parts of a chip: icon + name per input, " + " between them.
struct SChipLayout {
  std::vector<std::string> names;
  float width = 0.f;
};

SChipLayout ChipLayout(const Binding& b) {
  const ImGuiStyle& style = ImGui::GetStyle();
  const float icon = ImGui::GetTextLineHeight();
  const float gap = style.ItemInnerSpacing.x * 0.5f;
  SChipLayout out;
  out.width = style.FramePadding.x * 2.f;
  for (int i = 0; i < b.count; ++i) {
    if (i != 0) out.width += ImGui::CalcTextSize(" + ").x;
    out.names.push_back(InputName(b.inputs[size_t(i)]));
    out.width += icon + gap + ImGui::CalcTextSize(out.names.back().c_str()).x;
  }
  return out;
}

// A binding as a button (or just drawn, when !interactive). dim: a default the
// user hasn't made theirs. Returns whether it was clicked.
bool Chip(const char* id, const Binding& b, bool dim, bool selected, bool interactive = true) {
  const ImGuiStyle& style = ImGui::GetStyle();
  const SChipLayout layout = ChipLayout(b);
  const float icon = ImGui::GetTextLineHeight();
  // Frame height, so the text lines up with buttons and text on the same line.
  const ImVec2 size(layout.width, ImGui::GetFrameHeight());
  const ImVec2 p = ImGui::GetCursorScreenPos();
  bool clicked = false;
  if (interactive) {
    clicked = ImGui::InvisibleButton(id, size);
  } else {
    ImGui::Dummy(size);
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (interactive) {
    const ImGuiCol bg = selected || ImGui::IsItemActive() ? ImGuiCol_ButtonActive
                        : ImGui::IsItemHovered()          ? ImGuiCol_ButtonHovered
                                                          : ImGuiCol_Button;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(bg, dim && !selected ? 0.45f : 1.f),
                      style.FrameRounding);
  }
  const ImU32 col = ImGui::GetColorU32(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text);
  const float gap = style.ItemInnerSpacing.x * 0.5f;
  float x = p.x + (interactive ? style.FramePadding.x : 0.f);
  const float y = p.y + style.FramePadding.y;
  for (int i = 0; i < b.count; ++i) {
    if (i != 0) {
      dl->AddText(ImVec2(x, y), col, " + ");
      x += ImGui::CalcTextSize(" + ").x;
    }
    DrawIcon(dl, ImVec2(x, y), icon, IconOf(b.inputs[size_t(i)].device), col);
    x += icon + gap;
    const std::string& name = layout.names[size_t(i)];
    dl->AddText(ImVec2(x, y), col, name.c_str());
    x += ImGui::CalcTextSize(name.c_str()).x;
  }
  return clicked;
}

// A binding in words for tooltips: "keyboard Left Shift + keyboard 1".
std::string ChipTooltip(const Binding& b) {
  std::string out;
  for (int i = 0; i < b.count; ++i) {
    if (i != 0) out += " + ";
    out += std::string(IconName(IconOf(b.inputs[size_t(i)].device))) + " " + InputName(b.inputs[size_t(i)], true);
  }
  return out;
}

void DrawAdvancedEditor(const Binding& parsed, bool inputsOk);
void SaveDraft(const SViews& views, Binding parsed);

// Appends a touch control to the draft's inputs (a second one makes a chord).
void TouchPicker() {
  ImGui::SetNextItemWidth(260.f);
  if (ImGui::BeginCombo("##touch", "Add a touch control...")) {
    for (const STouchInfo& t : kTouchControls) {
      if (ImGui::Selectable(t.label)) {
        std::string text = TrimSpaces(sInputsText);
        if (!text.empty()) text += " + ";
        text += std::string("touch:") + t.name;
        std::snprintf(sInputsText, sizeof(sInputsText), "%s", text.c_str());
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SetItemTooltip("The touch controls hide while this menu is open, so they can't be recorded: "
                        "pick one here. Picking another makes a chord.");
}

void DrawEditor(const SViews& views) {
  const std::string title = std::string(sEditing == kEditNew ? "New binding: " : "Edit binding: ") +
                            ActionLabel(sEditAction);
  ImGui::SeparatorText(title.c_str());

  Binding parsed = sDraft;
  const bool inputsOk = InputsFromText(sInputsText, parsed);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Input:");
  ImGui::SameLine();
  if (sCapture.active) {
    ImGui::TextColored(ImVec4(1.f, 0.85f, 0.3f, 1.f), sCapture.armed ? "Press an input or a chord, then release it (Esc cancels)..."
                                                                     : "Release everything...");
  } else {
    if (inputsOk && parsed.count > 0) {
      Chip("##draft", parsed, false, false, false);
      ImGui::SetItemTooltip("%s", sInputsText);
    } else {
      ImGui::TextDisabled(sInputsText[0] == '\0' ? "(none)" : "(not understood: see Advanced)");
    }
    ImGui::SameLine();
    if (ImGui::Button("Record")) StartCapture();
    ImGui::SetItemTooltip("Press one key, mouse button or controller input (mouse wheel and sticks too), or "
                          "hold several in order (the last one is the trigger), then release them all. "
                          "Esc cancels.");
    ImGui::SameLine();
    if (kShowTouch) {
      TouchPicker();
      ImGui::SameLine();
    }
    if (ImGui::Button("Clear")) sInputsText[0] = '\0';
  }

  // Additional settings open by themselves when the binding uses them.
  if (sAdvancedSync) {
    ImGui::SetNextItemOpen(UsesAdvanced(sDraft), ImGuiCond_Always);
    sAdvancedSync = false;
  }
  if (ImGui::TreeNode("Advanced##edit")) {
    DrawAdvancedEditor(parsed, inputsOk);
    ImGui::TreePop();
  }

  const bool familyOk = inputsOk && parsed.count > 0 && OneFamily(parsed);
  if (inputsOk && parsed.count > 0 && !familyOk) {
    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f),
                       "A chord's inputs must come from one device (keyboard and mouse count as one).");
  }
  const bool valid = familyOk && sDraft.action != Action::None && sDraft.contexts != 0;
  ImGui::BeginDisabled(!valid);
  if (ImGui::Button("Save")) {
    SaveDraft(views, parsed);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    sEditing = kEditNone;
    EndCapture();
  }
}

void DrawAdvancedEditor(const Binding& parsed, bool inputsOk) {
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##inputs", "pad:leftshoulder + pad:a", sInputsText, sizeof(sInputsText));
  ImGui::SetItemTooltip("Inputs, joined with +: key:<name>, mouse:left, wheel:up, pad:<button>, "
                        "axis:<name>+/-, touch:<control> (a, fire, missile...), gyro:yaw+ ... (docs/NATIVE_PORT.md). "
                        "Append @<percent> for an axis threshold.");
  if (!inputsOk && sInputsText[0] != '\0') ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "Unknown input text.");
  if (parsed.count > 1) {
    ImGui::Checkbox("Any order", &sDraft.anyOrder);
    ImGui::SetItemTooltip("Off: hold the first inputs, then press the last one. On: press them all at "
                          "once, in any order.");
  }

  int trigger = 0;
  for (int i = 0; i < int(std::size(kTriggers)); ++i) {
    if (kTriggers[i].trigger == sDraft.trigger) trigger = i;
  }
  if (ImGui::BeginCombo("Trigger", kTriggers[trigger].label)) {
    for (int i = 0; i < int(std::size(kTriggers)); ++i) {
      if (ImGui::Selectable(kTriggers[i].label, i == trigger)) sDraft.trigger = kTriggers[i].trigger;
    }
    ImGui::EndCombo();
  }
  int ms = 0;
  switch (sDraft.trigger) {
  case Trigger::Tap:
    ms = sDraft.tapMs;
    if (ImGui::SliderInt("Tap shorter than (ms)", &ms, 50, 1000)) sDraft.tapMs = uint16_t(ms);
    break;
  case Trigger::Hold:
    ms = sDraft.holdMs;
    if (ImGui::SliderInt("Hold for (ms)", &ms, 50, 2000)) sDraft.holdMs = uint16_t(ms);
    break;
  case Trigger::DoubleTap:
    ms = sDraft.doubleMs;
    if (ImGui::SliderInt("Second press within (ms)", &ms, 50, 1000)) sDraft.doubleMs = uint16_t(ms);
    break;
  default: break;
  }
  int turbo = sDraft.turboHz;
  if (ImGui::SliderInt("Turbo (presses/s)", &turbo, 0, 30, turbo == 0 ? "Off" : "%d")) sDraft.turboHz = uint16_t(turbo);

  const PortInput::ActionKind kind = PortInput::Info(sDraft.action).kind;
  if (kind != PortInput::ActionKind::Digital) {
    ImGui::SliderFloat("Scale", &sDraft.scale, 0.1f, 4.f, "%.2f");
    ImGui::Checkbox("Invert", &sDraft.invert);
  }

  if (PortInput::FindBallRow(sDraft.action) != nullptr) {
    ImGui::TextDisabled("Works only in Morph Ball, where its inputs do nothing else.");
    return;
  }
  bool everywhere = sDraft.contexts == PortInput::kCtxAll;
  if (ImGui::Checkbox("Works everywhere", &everywhere)) {
    sDraft.contexts = everywhere ? PortInput::kCtxAll : PortInput::kCtxGameplay;
  }
  ImGui::SetItemTooltip("Off: the binding works only where you tick below, so the same input can do "
                        "different things on the map, in menus or in Morph Ball.");
  if (!everywhere) {
    ImGui::TextUnformatted("Only in:");
    for (size_t i = 0; i < std::size(kContexts); ++i) {
      bool on = (sDraft.contexts & kContexts[i].bit) != 0;
      if (i % 5 != 0) ImGui::SameLine();
      if (ImGui::Checkbox(kContexts[i].label, &on)) {
        sDraft.contexts = uint16_t(on ? sDraft.contexts | kContexts[i].bit : sDraft.contexts & ~kContexts[i].bit);
      }
    }
    ImGui::TextDisabled("Layer n: only while the input bound to \"Layer n (hold)\" is held, like a shift "
                        "key. There it wins over the input's normal binding.");
  }
}

void SaveDraft(const SViews& views, Binding parsed) {
  using PortInput::Family;
  parsed.action = sDraft.action;
  parsed.anyOrder = parsed.count > 1 && sDraft.anyOrder;
  parsed.trigger = sDraft.trigger;
  parsed.tapMs = sDraft.tapMs;
  parsed.holdMs = sDraft.holdMs;
  parsed.doubleMs = sDraft.doubleMs;
  parsed.turboHz = sDraft.turboHz;
  parsed.scale = sDraft.scale;
  parsed.invert = sDraft.invert;
  parsed.contexts = sDraft.contexts;
  // The binding goes to its own device's column, wherever it was edited from.
  const Family to = PortInput::BindingFamily(parsed);
  const auto finish = [] {
    sEditing = kEditNone;
    EndCapture();
    Commit();
  };
  if (sEditing == kEditNew) {
    const SView& target = views[to];
    const UserProfile* user = Find(target.match);
    const auto& bindings = user != nullptr && PortInput::Overridden(*user, parsed.action, to)
                               ? user->bindings : target.inherited.bindings;
    if (std::find(bindings.begin(), bindings.end(), parsed) != bindings.end()) {
      // An existing default is already bound: don't turn it into an override.
      sEditing = kEditNone;
      EndCapture();
      return;
    }
  }
  if (sEditing == kEditInherited || sEditing >= 0) {
    const SView& from = views[sEditFamily];
    UserProfile& profile = EditedProfile(from);
    if (sEditing == kEditInherited) {
      PortInput::Materialize(profile, sEditOrig.action, sEditFamily, from.inherited);
      const auto it = std::find(profile.bindings.begin(), profile.bindings.end(), sEditOrig);
      if (it != profile.bindings.end()) PortInput::RemoveUserBinding(profile, size_t(it - profile.bindings.begin()));
    } else if (sEditing < int(profile.bindings.size())) {
      const Binding& old = profile.bindings[size_t(sEditing)];
      if (old.action == parsed.action && to == sEditFamily) {
        PortInput::RemoveUserBinding(profile, size_t(sEditing));
        PortInput::AddUserBinding(profile, parsed);
        finish();
        return;
      }
      PortInput::RemoveUserBinding(profile, size_t(sEditing));
    }
  }
  // Fetched again: making another profile can move the one above.
  UserProfile& profile = EditedProfile(views[to]);
  // Keep the other built-in bindings of this action: a user binding drops them all.
  PortInput::Materialize(profile, parsed.action, to, views[to].inherited);
  PortInput::AddUserBinding(profile, parsed);
  finish();
}

void DrawBindingRow(const Binding& b, bool inherited = false) {
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(ActionLabel(b.action).c_str());
  ImGui::TableNextColumn();
  Chip("##in", b, false, false, false);
  if (b.IsChord() && b.anyOrder) {
    ImGui::SameLine();
    ImGui::TextDisabled("(any order)");
  }
  ImGui::TableNextColumn();
  if (b.turboHz != 0) {
    ImGui::Text("%s, turbo %d/s", TriggerLabel(b.trigger), int(b.turboHz));
  } else {
    ImGui::TextUnformatted(TriggerLabel(b.trigger));
  }
  ImGui::TableNextColumn();
  std::string where = b.contexts == PortInput::kCtxAll ? "everywhere" : PortInput::ContextsToText(b.contexts);
  if (inherited) where += " (inherited)";
  ImGui::TextUnformatted(where.c_str());
}

void DrawEffective() {
  if (!ImGui::TreeNode("Everything bound now")) return;
  ImGui::TextDisabled("The built-in controls with every profile in use on top.");
  PortInput::Profile p;
  PortInputDevices::BuildProfile(p);
  if (ImGui::BeginTable("effective", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    for (const Binding& b : p.bindings) {
      ImGui::TableNextRow();
      DrawBindingRow(b);
    }
    ImGui::EndTable();
  }
  ImGui::TreePop();
}

bool Contains(const std::string& text, const char* filter) {
  if (filter[0] == '\0') return true;
  const auto lower = [](std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  return lower(text).find(lower(filter)) != std::string::npos;
}

// A change the table asks for; applied after it so the rows aren't edited mid-draw.
struct SPending {
  enum class Op { None, RemoveUser, RemoveInherited, Revert } op = Op::None;
  Action action = Action::None;
  PortInput::Family family = PortInput::Family::Keyboard;
  size_t index = 0;
  Binding binding;
  unsigned revertFamilies = 0; // Revert: bit per overridden family
};

void ApplyPending(const SViews& views, const SPending& pending) {
  using PortInput::Family;
  if (pending.op == SPending::Op::None) return;
  if (pending.op == SPending::Op::Revert) {
    for (const Family f : {Family::Keyboard, Family::Pad, Family::Touch}) {
      if ((pending.revertFamilies >> int(f)) & 1u) PortInput::Revert(EditedProfile(views[f]), pending.action, f);
    }
  } else {
    const SView& v = views[pending.family];
    UserProfile& profile = EditedProfile(v);
    if (pending.op == SPending::Op::RemoveUser) {
      PortInput::RemoveUserBinding(profile, pending.index);
    } else {
      PortInput::Materialize(profile, pending.action, v.family, v.inherited);
      const auto it = std::find(profile.bindings.begin(), profile.bindings.end(), pending.binding);
      if (it != profile.bindings.end()) PortInput::RemoveUserBinding(profile, size_t(it - profile.bindings.begin()));
    }
  }
  sEditing = kEditNone;
  EndCapture();
  Commit();
}

// The custom action editor (Add and Edit) and the delete confirmation. Both are
// drawn once at the end of the page, outside any table, so their popups' IDs are
// the same wherever they are opened from.
struct SCustomDraft {
  bool open = false;        // the editor is up
  bool requestOpen = false; // open it at the end of this frame
  uint16_t editId = 0;      // 0: a new action
  std::string preset;       // the chosen preset key; "" while the list is up
  std::string arg;          // the chosen setting, item, slot or language code
  std::string mode = "toggle";
  char argText[160] = {};   // a message or a console line
  char name[96] = {};       // the Name field
  bool renamed = false;     // the player typed the name: keep it
  char search[64] = {};
  char argSearch[64] = {};
  bool shown = false;       // the room list scrolled to the chosen room once
  std::string error;
};
SCustomDraft sCustom;
uint16_t sDeleteId = 0; // the custom action the confirmation asks about
bool sDeleteRequest = false;

// What the editor has so far, as a definition (its id is set on Save).
PortInput::CustomActionDef DraftDef() {
  PortInput::CustomActionDef def;
  def.preset = sCustom.preset;
  def.arg = sCustom.arg;
  if (const PortCustomActions::Preset* preset = PortCustomActions::FindPreset(sCustom.preset)) {
    // Only a preset that has a mode keeps one, so the file stays clean.
    if (preset->hasMode) def.mode = sCustom.mode;
    if (preset->arg == PortCustomActions::ArgKind::Text) {
      if (std::string_view(preset->key) == "command") {
        def.command = sCustom.argText;
      } else {
        def.arg = sCustom.argText;
      }
    }
  }
  def.renamed = sCustom.renamed;
  def.name = sCustom.renamed ? std::string(sCustom.name) : std::string();
  return def;
}

// Opens the editor on a def, or on a new one when def is null.
void OpenCustomEditor(const PortInput::CustomActionDef* def) {
  sCustom = SCustomDraft{};
  if (def != nullptr) {
    sCustom.editId = def->id;
    sCustom.preset = def->preset;
    sCustom.arg = def->arg;
    sCustom.mode = def->mode;
    sCustom.renamed = def->renamed;
    const PortCustomActions::Preset* preset = PortCustomActions::FindPreset(def->preset);
    if (preset != nullptr && preset->arg == PortCustomActions::ArgKind::Text) {
      const std::string& text = std::string_view(preset->key) == "command" ? def->command : def->arg;
      std::snprintf(sCustom.argText, sizeof(sCustom.argText), "%s", text.c_str());
    }
    std::snprintf(sCustom.name, sizeof(sCustom.name), "%s", def->name.c_str());
  }
  sCustom.requestOpen = true;
  EndCapture();
}

// The presets, grouped by category, with the search box above them.
void DrawPresetPicker() {
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##actionsearch", "Search actions", sCustom.search, sizeof(sCustom.search));
  // The list takes the window's height, less the separator and Cancel below it.
  ImGui::BeginChild("##presets", ImVec2(0.f, -ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y));
  const char* category = nullptr;
  bool first = true;
  bool shown = false; // the current category's header is open
  for (const PortCustomActions::Preset& preset : PortCustomActions::Presets()) {
    if (!Contains(preset.label, sCustom.search) && !Contains(preset.help, sCustom.search)) continue;
    if (category == nullptr || std::string_view(category) != preset.category) {
      category = preset.category;
      // A filter opens every category, so a match is never hidden in a closed one.
      if (sCustom.search[0] != '\0') ImGui::SetNextItemOpen(true, ImGuiCond_Always);
      shown = ImGui::CollapsingHeader(category, first ? ImGuiTreeNodeFlags_DefaultOpen : 0);
      first = false;
      if (shown && preset.cheat) ImGui::TextDisabled("Off in the Original experience");
    }
    if (!shown) continue;
    if (ImGui::Selectable(preset.label)) {
      sCustom.preset = preset.key;
      sCustom.arg.clear();
      sCustom.argText[0] = '\0';
      sCustom.argSearch[0] = '\0';
    }
    ImGui::SetItemTooltip("%s", preset.help);
  }
  ImGui::EndChild();
}

// The option a preset takes, then its mode and its name.
void DrawPresetOptions(const PortCustomActions::Preset& preset) {
  using PortCustomActions::ArgKind;
  const std::string_view key(preset.key);
  // A list takes the window's height, less the rows below it: the mode, the name,
  // an error and Save.
  const int rowsBelow = (preset.hasMode ? 1 : 0) + 2 + (sCustom.error.empty() ? 0 : 1);
  const ImVec2 listSize(0.f, -rowsBelow * ImGui::GetFrameHeightWithSpacing());
  if (preset.arg == ArgKind::Setting) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##settingsearch", "Search settings", sCustom.argSearch, sizeof(sCustom.argSearch));
    ImGui::BeginChild("##settings", listSize);
    const char* section = nullptr;
    for (const PortCustomActions::BoolSetting& setting : PortCustomActions::BoolSettings()) {
      if (!Contains(setting.label, sCustom.argSearch) && !Contains(setting.section, sCustom.argSearch)) continue;
      if (section == nullptr || std::string_view(section) != setting.section) {
        section = setting.section;
        ImGui::SeparatorText(section);
      }
      if (ImGui::Selectable(setting.label, sCustom.arg == setting.key)) sCustom.arg = setting.key;
    }
    ImGui::EndChild();
  } else if (preset.arg == ArgKind::Item) {
    ImGui::BeginChild("##items", listSize);
    for (const PortConsole::Item& item : PortConsole::Items()) {
      if (ImGui::Selectable(item.label, sCustom.arg == item.key)) sCustom.arg = item.key;
    }
    ImGui::EndChild();
  } else if (preset.arg == ArgKind::Slot) {
    // Slot 1 unless the player picks another.
    if (sCustom.arg.empty()) sCustom.arg = "1";
    for (int slot = 1; slot <= PortSaveState::kSlotCount; ++slot) {
      const std::string label = "Slot " + std::to_string(slot);
      if (ImGui::Selectable(label.c_str(), sCustom.arg == std::to_string(slot))) {
        sCustom.arg = std::to_string(slot);
      }
      if (slot != PortSaveState::kSlotCount) ImGui::SameLine();
    }
  } else if (preset.arg == ArgKind::Language) {
    ImGui::BeginChild("##languages", listSize);
    for (const PortCustomActions::Language& language : PortCustomActions::Languages()) {
      if (ImGui::Selectable(language.label, sCustom.arg == language.code)) sCustom.arg = language.code;
    }
    ImGui::EndChild();
  } else if (preset.arg == ArgKind::Room) {
    PortCustomActions::LoadRooms();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##roomsearch", "Search rooms", sCustom.argSearch, sizeof(sCustom.argSearch));
    ImGui::BeginChild("##rooms", listSize);
    if (PortCustomActions::Worlds().empty()) ImGui::TextDisabled("Start a game to list the rooms");
    for (const PortCustomActions::World& world : PortCustomActions::Worlds()) {
      // The one level with no front-end name is the ending's, not a place to go.
      if (world.name.empty()) continue;
      // A world's name matching lists all its rooms; else only the rooms that match.
      const bool worldMatch = Contains(world.name, sCustom.argSearch);
      bool any = worldMatch || !world.loaded;
      for (size_t i = 0; !any && i < world.rooms.size(); ++i) {
        any = Contains(world.rooms[i].name, sCustom.argSearch);
      }
      if (!any) continue;
      ImGui::SeparatorText(world.name.c_str());
      ImGui::PushID(static_cast<int>(world.id));
      const std::string start = PortCustomActions::RoomArg(world.id, 0);
      if (worldMatch && ImGui::Selectable("Where the world starts", sCustom.arg == start)) sCustom.arg = start;
      if (!world.loaded) ImGui::TextDisabled("Reading the room names...");
      for (const PortCustomActions::Room& room : world.rooms) {
        if (!world.loaded) break;
        if (!worldMatch && !Contains(room.name, sCustom.argSearch)) continue;
        const std::string arg = PortCustomActions::RoomArg(world.id, room.area);
        ImGui::PushID(static_cast<int>(room.area));
        if (ImGui::Selectable(room.name.c_str(), sCustom.arg == arg)) {
          sCustom.arg = arg;
          sCustom.shown = true;
        } else if (sCustom.arg == arg && !sCustom.shown) {
          // Editing: bring the room already picked into view.
          ImGui::SetScrollHereY(0.5f);
          sCustom.shown = true;
        }
        ImGui::PopID();
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
  } else if (preset.arg == ArgKind::Text) {
    const bool command = key == "command";
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##argtext", command ? "give Missiles 10" : "Text to show", sCustom.argText,
                             sizeof(sCustom.argText));
    ImGui::SetItemTooltip("%s", command
                                  ? "The console command to run (docs/CONSOLE.md), where the game is."
                                  : "The text the HUD message shows.");
  }

  if (preset.hasMode) {
    int mode = sCustom.mode == "on" ? 1 : sCustom.mode == "off" ? 2 : 0;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("When pressed:");
    ImGui::SameLine();
    ImGui::RadioButton("Toggle", &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Turn on", &mode, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Turn off", &mode, 2);
    sCustom.mode = mode == 1 ? "on" : mode == 2 ? "off" : "toggle";
  }

  // The name follows the options until the player types one; clearing it goes
  // back to the automatic name.
  const PortInput::CustomActionDef draft = DraftDef();
  if (!sCustom.renamed) {
    std::snprintf(sCustom.name, sizeof(sCustom.name), "%s", PortCustomActions::AutoName(draft).c_str());
  }
  ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Name").x - ImGui::GetStyle().ItemInnerSpacing.x);
  if (ImGui::InputTextWithHint("Name", "What this action is called", sCustom.name, sizeof(sCustom.name))) {
    sCustom.renamed = sCustom.name[0] != '\0';
  }
  ImGui::SetItemTooltip("The name the binding list and the touch buttons show. Clear it for the name built from "
                        "the action and its option.");
}

void SaveCustom() {
  PortInput::CustomActionDef def = DraftDef();
  if (sCustom.editId == 0) {
    const uint16_t id = PortInput::NextCustomId(sWork);
    const bool taken = std::any_of(sWork.customActions.begin(), sWork.customActions.end(),
                                   [id](const PortInput::CustomActionDef& other) { return other.id == id; });
    if (taken) {
      sCustom.error = "No free id is left for a new action: delete one first.";
      return;
    }
    def.id = id;
    sWork.customActions.push_back(def);
  } else {
    def.id = sCustom.editId;
    for (PortInput::CustomActionDef& other : sWork.customActions) {
      if (other.id == def.id) other = def;
    }
  }
  sCustom.open = false;
  sCustom.editId = 0;
  sEditing = kEditNone;
  EndCapture();
  Commit();
  ImGui::CloseCurrentPopup();
}

void DrawCustomEditor() {
  if (sCustom.preset.empty()) {
    DrawPresetPicker();
    ImGui::Separator();
    if (ImGui::Button("Cancel")) {
      sCustom.open = false;
      ImGui::CloseCurrentPopup();
    }
    return;
  }
  const PortCustomActions::Preset* preset = PortCustomActions::FindPreset(sCustom.preset);
  if (preset == nullptr) {
    ImGui::TextDisabled("This action's kind no longer exists: pick another one.");
    if (ImGui::Button("Change")) {
      sCustom.preset.clear();
      sCustom.arg.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      sCustom.open = false;
      ImGui::CloseCurrentPopup();
    }
    return;
  }
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(preset->label);
  ImGui::SameLine();
  if (ImGui::SmallButton("Change")) {
    sCustom.preset.clear();
    sCustom.arg.clear();
    return;
  }
  ImGui::SetItemTooltip("%s", preset->help);
  if (preset->cheat) ImGui::TextDisabled("Off in the Original experience");
  DrawPresetOptions(*preset);

  const bool command = std::string_view(preset->key) == "command";
  bool valid = preset->arg == PortCustomActions::ArgKind::None || !sCustom.arg.empty();
  if (preset->arg == PortCustomActions::ArgKind::Text) valid = sCustom.argText[0] != '\0';
  // A console command needs a name of its own, to tell it apart from any other.
  if (command) valid = valid && sCustom.renamed && sCustom.name[0] != '\0';
  if (!sCustom.error.empty()) ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", sCustom.error.c_str());
  ImGui::BeginDisabled(!valid);
  if (ImGui::Button("Save")) SaveCustom();
  ImGui::EndDisabled();
  if (command && !sCustom.renamed) {
    ImGui::SetItemTooltip("Give it a name first, so the console command is recognisable in the list.");
  } else if (!valid) {
    ImGui::SetItemTooltip("Choose what the action does first.");
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    sCustom.open = false;
    ImGui::CloseCurrentPopup();
  }
}

// The editor's and the delete confirmation's popups, drawn once per frame outside
// the tables so their IDs don't depend on the row they came from.
void DrawCustomActionPopups() {
  if (sCustom.requestOpen) {
    sCustom.requestOpen = false;
    sCustom.open = true;
    sCustom.error.clear();
    ImGui::OpenPopup("Custom action");
  }
  // Resizable, opening at most of the screen; a resize is kept for the next time.
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.6f, viewport->Size.y * 0.75f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(viewport->Size.x, 420.f), std::min(viewport->Size.y, 360.f)),
                                      viewport->Size);
  if (ImGui::BeginPopupModal("Custom action", nullptr)) {
    DrawCustomEditor();
    if (!sCustom.open) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  } else {
    sCustom.open = false;
  }

  if (sDeleteRequest) {
    sDeleteRequest = false;
    ImGui::OpenPopup("Delete action?");
  }
  if (ImGui::BeginPopupModal("Delete action?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const PortInput::CustomActionDef* def = CustomDef(PortInput::CustomAction(sDeleteId));
    const std::string name = def != nullptr ? PortCustomActions::DisplayName(*def) : std::string("this action");
    ImGui::Text("Delete %s and its buttons?", name.c_str());
    ImGui::TextUnformatted("The action and every binding of it go; this can't be undone.");
    if (ImGui::Button("Delete")) {
      if (PortInput::IsCustom(sEditAction) && PortInput::CustomId(sEditAction) == sDeleteId) sEditing = kEditNone;
      PortInput::RemoveCustomAction(sWork, sDeleteId);
      EndCapture();
      Commit();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

// The table's sections. Layers are for shift-key setups, so they start closed.
struct SGroup {
  const char* label;
  std::vector<Action> actions;
  bool open;
  bool custom = false; // the player's own actions: rows carry Edit and Delete, and the list builds them
};

const std::vector<SGroup>& Groups() {
  static const std::vector<SGroup> groups = [] {
    std::vector<SGroup> g = {
        {"Game buttons",
         {Action::PadA, Action::PadB, Action::PadX, Action::PadY, Action::PadZ, Action::PadStart, Action::PadL,
          Action::PadR, Action::LAnalog, Action::RAnalog, Action::PadUp, Action::PadDown, Action::PadLeft,
          Action::PadRight},
         true},
        // The C-stick is the right stick: twin-stick aim, classic beam picks, map panning.
        {"Move and aim",
         {Action::MainUp, Action::MainDown, Action::MainLeft, Action::MainRight, Action::CUp, Action::CDown,
          Action::CLeft, Action::CRight},
         true},
        // Direct picks work in every scheme; the GameCube inputs behind them are listed above.
        {"Beams",
         {Action::BeamPower, Action::BeamWave, Action::BeamIce, Action::BeamPlasma, Action::BeamNext,
          Action::BeamPrev, Action::BeamMenu},
         true},
        {"Visors",
         {Action::VisorCombat, Action::VisorScan, Action::VisorThermal, Action::VisorXray, Action::VisorNext,
          Action::VisorPrev, Action::VisorMenu},
         true},
        // Issue #167: the ball's own buttons; an input on one does only that in the ball.
        {"Morph Ball",
         {Action::BallBomb, Action::BallPowerBomb, Action::BallBoost, Action::BallSpider, Action::BallUnmorph,
          Action::SpringBall},
         true},
        {"Gyro", {Action::GyroAim, Action::ResetAim}, true},
        // Read instead of the game buttons in menus and on the map ("Separate menu buttons").
        {"Menus and map",
         {Action::MenuA, Action::MenuB, Action::MenuX, Action::MenuY, Action::MenuL, Action::MenuR,
          Action::MenuLAnalog, Action::MenuRAnalog, Action::MenuUp, Action::MenuDown, Action::MenuLeft,
          Action::MenuRight},
         true},
        {"Port", {Action::PortMenu, Action::SaveState, Action::LoadState, Action::Screenshot, Action::ToggleOriginal,
          Action::ToggleTexturePack, Action::PhotoMode, Action::PrevStateSlot, Action::NextStateSlot}, true},
        {"Layers (shift keys, advanced)", {Action::Layer1, Action::Layer2, Action::Layer3, Action::Layer4}, false},
    };
    // An action added to the engine but not sorted here still gets a row.
    SGroup other{"Other", {}, true};
    // Aim*/Look* aren't read by the game yet (aim is the C-stick, mouse look its own path): no rows.
    const Action unused[] = {Action::AimUp, Action::AimDown, Action::AimLeft, Action::AimRight, Action::LookX,
                             Action::LookY};
    for (int i = 1; i < PortInput::kActionCount; ++i) {
      if (std::find(std::begin(unused), std::end(unused), Action(i)) != std::end(unused)) continue;
      const bool listed = std::any_of(g.begin(), g.end(), [&](const SGroup& s) {
        return std::find(s.actions.begin(), s.actions.end(), Action(i)) != s.actions.end();
      });
      if (!listed) other.actions.push_back(Action(i));
    }
    if (!other.actions.empty()) g.push_back(other);
    return g;
  }();
  return groups;
}

// What a GameCube button does in the game, next to its name.
const char* GameHint(Action a) {
  switch (a) {
  case Action::PadA: return "Fire";
  case Action::PadB: return "Jump";
  case Action::PadX: return "Morph Ball";
  case Action::PadY: return "Missile";
  case Action::PadZ: return "Map";
  case Action::PadStart: return "Pause";
  case Action::PadL:
  case Action::LAnalog: return "Lock on, scan";
  case Action::PadR:
  case Action::RAnalog: return "Free aim";
  case Action::MainUp:
  case Action::MainDown:
  case Action::MainLeft:
  case Action::MainRight: return "Move";
  case Action::CUp:
  case Action::CDown:
  case Action::CLeft:
  case Action::CRight: return "Aim, or pick a beam";
  case Action::PadUp:
  case Action::PadDown:
  case Action::PadLeft:
  case Action::PadRight: return "Pick a visor";
  case Action::AimUp:
  case Action::AimDown:
  case Action::AimLeft:
  case Action::AimRight: return "Twin-stick aim";
  case Action::MenuA: return "Confirm";
  case Action::MenuB: return "Back";
  case Action::MenuL:
  case Action::MenuR:
  case Action::MenuLAnalog:
  case Action::MenuRAnalog: return "Page, map zoom";
  case Action::MenuUp:
  case Action::MenuDown:
  case Action::MenuLeft:
  case Action::MenuRight: return "Navigate";
  default: break;
  }
  const PortInput::BallRow* ball = PortInput::FindBallRow(a);
  return ball != nullptr ? ball->hint : nullptr;
}

// A binding's non-default settings, in words ("" when it has none).
std::string Notes(const Binding& b, bool includeContexts = true) {
  std::string out;
  const auto add = [&](const std::string& s) {
    if (!out.empty()) out += ", ";
    out += s;
  };
  if (b.trigger != Trigger::Press) add(TriggerLabel(b.trigger));
  if (b.turboHz != 0) add("turbo " + std::to_string(b.turboHz) + "/s");
  if (b.IsChord() && b.anyOrder) add("any order");
  if (includeContexts && b.contexts != PortInput::kCtxAll) {
    std::string where;
    for (const SContextInfo& c : kContexts) {
      if ((b.contexts & c.bit) == 0) continue;
      if (!where.empty()) where += ", ";
      where += c.label;
    }
    add("only in " + where);
  }
  return out;
}

// One action's inputs in one device's column.
struct SCell {
  std::vector<std::pair<Binding, int>> rows; // (binding, index into the user's bindings or -1 for a default)
  bool overridden = false;
};

SCell CellOf(const SView& v, const UserProfile* user, Action action) {
  SCell cell;
  cell.overridden = user != nullptr && PortInput::Overridden(*user, action, v.family);
  if (cell.overridden) {
    for (int n = 0; n < int(user->bindings.size()); ++n) {
      const Binding& b = user->bindings[size_t(n)];
      if (b.count > 0 && b.action == action && PortInput::BindingFamily(b) == v.family) cell.rows.push_back({b, n});
    }
  } else {
    for (const Binding& b : PortInput::InFamily(v.inherited.bindings, action, v.family)) cell.rows.push_back({b, -1});
  }
  return cell;
}

// editorDrawn: the editor already went above the table this frame.
void DrawTable(const SViews& views, bool editorDrawn) {
  using PortInput::Family;
  ImGui::SetNextItemWidth(220.f);
  ImGui::InputTextWithHint("##filter", "Filter actions", sFilter, sizeof(sFilter));
  ImGui::SameLine();
  ImGui::Checkbox("Only bound", &sOnlyBound);

  std::vector<Family> families = {Family::Keyboard, Family::Pad};
  if (kShowTouch) families.push_back(Family::Touch);
  const UserProfile* users[3] = {};
  const auto findUsers = [&] {
    for (const Family f : families) users[int(f)] = Find(views[f].match);
  };
  findUsers();
  std::string inputsLabel = "Inputs";
  if (!views[Family::Pad].match.empty()) {
    inputsLabel += " (controller: " +
                   (views[Family::Pad].name.empty() ? views[Family::Pad].match : views[Family::Pad].name) + ")";
  }

  const ImGuiStyle& style = ImGui::GetStyle();
  const float xWidth = ImGui::CalcTextSize("x").x + style.FramePadding.x * 2.f;
  const float xGap = style.ItemInnerSpacing.x * 0.5f;
  SPending pending;
  bool editRowShown = false;
  bool saved = false;
  // The player's own actions, after Port and before Layers: they come and go with
  // controls.toml, so they aren't part of the cached Groups().
  std::vector<SGroup> groups(Groups().begin(), Groups().end());
  SGroup customGroup{"Custom actions (advanced)", {}, false, true};
  for (const PortInput::CustomActionDef& def : sWork.customActions) {
    customGroup.actions.push_back(PortInput::CustomAction(def.id));
  }
  const auto layersAt = std::find_if(groups.begin(), groups.end(),
                                    [](const SGroup& g) { return std::string_view(g.label).starts_with("Layers"); });
  groups.insert(layersAt, std::move(customGroup));
  for (const SGroup& group : groups) {
    // A filter opens every section, so a match is never hidden in a closed one.
    if (sFilter[0] != '\0') ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (!ImGui::CollapsingHeader(group.label, group.open ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
    // Called with the same ID stack each time, so a table resumed under the
    // editor keeps the columns of the one above it.
    const auto beginTable = [&](bool headers) {
      if (!ImGui::BeginTable(group.label, 3,
                             ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        return false;
      }
      ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.f);
      ImGui::TableSetupColumn(inputsLabel.c_str(), ImGuiTableColumnFlags_WidthStretch, 2.4f);
      // Room for "+", "Edit" and "Delete" (a custom action's row) in every table, so a
      // row getting a Revert moves nothing and all the sections' columns line up.
      ImGui::TableSetupColumn(
          "", ImGuiTableColumnFlags_WidthFixed,
          ImGui::CalcTextSize("+EditDelete").x + style.FramePadding.x * 6.f + style.ItemSpacing.x * 2.f);
      if (headers) ImGui::TableHeadersRow();
      return true;
    };
    if (!beginTable(true)) continue;
    bool tableOpen = true;
    // Breaks the table under the row just drawn for the editor, then goes on.
    const auto editorHere = [&] {
      editRowShown = true;
      if (editorDrawn) return;
      editorDrawn = true;
      ImGui::EndTable();
      ImGui::Indent();
      DrawEditor(views);
      ImGui::Unindent();
      ImGui::Separator();
      saved = sEditing == kEditNone;
      findUsers(); // a Save can add a profile
      tableOpen = beginTable(false);
    };
    for (const Action action : group.actions) {
      if (!tableOpen) break;
      const std::string label = ActionLabel(action);
      const char* hint = GameHint(action);
      if (!Contains(label, sFilter) && (hint == nullptr || !Contains(hint, sFilter))) continue;
      SCell cells[3];
      bool any = false;
      unsigned overridden = 0;
      for (const Family f : families) {
        cells[int(f)] = CellOf(views[f], users[int(f)], action);
        any = any || !cells[int(f)].rows.empty();
        if (cells[int(f)].overridden) overridden |= 1u << int(f);
      }
      if (!any && sOnlyBound) continue;

      // The IDs are popped before the editor, so its table resumes with the same ID.
      ImGui::PushID(int(action));
      ImGui::TableNextRow();
      const bool editingAction = sEditing != kEditNone && sEditAction == action;
      if (editingAction) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Header));
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      // The player's own names can be long ("Warp to … (Phendrana Drifts)"): they wrap.
      if (group.custom) ImGui::PushTextWrapPos(0.f);
      ImGui::TextUnformatted(label.c_str());
      if (group.custom) ImGui::PopTextWrapPos();
      if (hint != nullptr) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", hint);
      }

      // Every device's inputs in one cell; the icons tell them apart.
      ImGui::TableNextColumn();
      const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
      // The Port menu's last key or controller input can't be removed (a touch one
      // isn't enough on desktop); BuildProfile falls back to F1 if it goes anyway.
      bool keepLast = false;
      if (action == PortInput::Action::PortMenu) {
        size_t reachable = 0;
        for (const Family f : families)
          if (f != Family::Touch) reachable += cells[int(f)].rows.size();
        keepLast = reachable <= 1;
      }
      bool first = true;
      for (const Family f : families) {
        ImGui::PushID(int(f));
        const SCell& cell = cells[int(f)];
        for (size_t r = 0; r < cell.rows.size(); ++r) {
          const Binding& b = cell.rows[r].first;
          const int userIndex = cell.rows[r].second;
          const bool isDefault = userIndex < 0;
          const std::string notes = Notes(b, false);
          // Inputs flow left to right and wrap at the column's edge.
          if (!first) {
            float width = ChipLayout(b).width + xGap + xWidth;
            if (!notes.empty()) width += style.ItemSpacing.x + ImGui::CalcTextSize(("(" + notes + ")").c_str()).x;
            if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right) ImGui::SameLine();
          }
          first = false;
          ImGui::PushID(int(r));
          const bool selected = sEditFamily == f && sEditAction == action &&
                                (isDefault ? sEditing == kEditInherited && b == sEditOrig : sEditing == userIndex);
          if (Chip("##in", b, isDefault, selected)) {
            BeginEdit(isDefault ? kEditInherited : userIndex, b, f);
            sEditOrig = b;
          }
          if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            std::string tip = ChipTooltip(b);
            const std::string tooltipNotes = Notes(b);
            if (!tooltipNotes.empty()) tip += "\n" + tooltipNotes;
            tip += isDefault ? "\nDefault: changing or removing it makes this device's inputs for the action "
                               "yours; Revert gives the defaults back."
                             : "\nClick to change.";
            ImGui::SetTooltip("%s", tip.c_str());
          }
          if (!notes.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", notes.c_str());
          }
          ImGui::SameLine(0.f, xGap);
          const bool locked = keepLast && f != Family::Touch;
          ImGui::BeginDisabled(locked);
          const bool remove = ImGui::SmallButton("x");
          ImGui::EndDisabled();
          if (remove) {
            pending.action = action;
            pending.family = f;
            if (isDefault) {
              pending.op = SPending::Op::RemoveInherited;
              pending.binding = b;
            } else {
              pending.op = SPending::Op::RemoveUser;
              pending.index = size_t(userIndex);
            }
          }
          if (locked)
            ImGui::SetItemTooltip("The Port menu needs at least one key or controller input. Add another "
                                  "one first to remove this.");
          else
            ImGui::SetItemTooltip("Remove this input.");
          ImGui::PopID();
        }
        ImGui::PopID();
      }
      if (first) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(overridden != 0 ? "(removed)" : "-");
      }

      ImGui::TableNextColumn();
      if (ImGui::SmallButton("+")) {
        BeginEdit(kEditNew, Binding{action}, Family::Keyboard);
        StartCapture();
      }
      ImGui::SetItemTooltip("Add a binding for this action and start recording any key, mouse button or "
                            "controller input. Release it, then Save.");
      // A custom action has no defaults to go back to: x removes its inputs.
      if (overridden != 0 && !group.custom) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Revert")) {
          pending.op = SPending::Op::Revert;
          pending.action = action;
          pending.revertFamilies = overridden;
        }
        ImGui::SetItemTooltip("Back to the default inputs on every device.");
      }
      if (group.custom) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit")) OpenCustomEditor(CustomDef(action));
        ImGui::SetItemTooltip("Change what this action does.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
          sDeleteId = PortInput::CustomId(action);
          sDeleteRequest = true;
        }
        ImGui::SetItemTooltip("Delete this action and its buttons.");
      }
      ImGui::PopID();
      // Read after the row's buttons: a click this frame opens the editor at once.
      if (sEditing != kEditNone && sEditAction == action) editorHere();
    }
    if (tableOpen) ImGui::EndTable();
    if (group.custom) {
      if (ImGui::Button("+ Add action")) OpenCustomEditor(nullptr);
      ImGui::SetItemTooltip("Make your own action: a setting, a cheat, a save state, a message or a console "
                            "command. Bind it to any input afterwards, like the actions above.");
    }
  }
  sEditRowShown = editRowShown;
  // A remove index from before a Save in the same frame may be stale.
  if (!saved) ApplyPending(views, pending);
}

struct SPreset {
  const char* name;
  const char* id;
  const char* tooltip;
};
constexpr SPreset kKeyPresets[] = {
    {"Classic", "classic", "The default layout. Sets the keys back to the built-in keyboard profile."},
    {"Mouse & keyboard", "mouse",
     "For mouse aim: WASD move, E fire, Space jump, left ctrl or C morph ball,\n"
     "F missile, Q lock on, left alt free look, Tab or M map, 1-4 beams, 5-8 visors\n"
     "(arrows too). Turns on mouse aim and Twin Stick Aim, so a controller's right\n"
     "stick aims too."},
};
constexpr SPreset kPadPresets[] = {
    {"GameCube", "gamecube",
     "The default layout: the face buttons and shoulder as on a GameCube pad.\nTurns off Twin Stick Aim."},
    {"Remastered", "remastered",
     "Metroid Prime Remastered's Dual Sticks scheme (Xbox labels): RT fire, LT lock on,\n"
     "A jump, X morph ball, RB missile, Menu map, View pause, right stick click free look.\n"
     "Turns on Twin Stick Aim."},
    {"Modern", "modern",
     "RT fire, LT lock on, A jump, B morph ball, RB missile, Y map,\n"
     "right stick click free look. Turns on Twin Stick Aim."},
    {"Southpaw", "southpaw", "The GameCube layout with the two sticks swapped.\nTurns off Twin Stick Aim."},
};

// Removes the user's own bindings for a view: a family of the Default profile,
// or a kind or model's whole profile.
void ClearOwn(const SView& v) {
  sEditing = kEditNone;
  EndCapture();
  if (UserProfile* p = Find(v.match)) {
    if (v.match.empty()) {
      PortInput::ClearFamily(*p, v.family);
    } else {
      p->bindings.clear();
      p->unbind.clear();
    }
    Commit();
  }
}

// One device's reset, behind the popup's confirmation. The keyboard and all
// controllers go back to their first preset (the first-run layout).
void ResetSection(const SView& v) {
  using PortInput::Family;
  ImGui::PushID(int(v.family));
  ImGui::SeparatorText(v.family == Family::Pad && !v.match.empty() ? (v.name.empty() ? v.match : v.name).c_str()
                                                                   : kFamilies[int(v.family)]);
  const bool layout = v.match.empty() && v.family != Family::Touch;
  if (layout) {
    ImGui::TextDisabled("Your own bindings for this device are removed and the %s layout comes back.",
                        v.family == Family::Keyboard ? kKeyPresets[0].name : kPadPresets[0].name);
  } else if (v.family == Family::Touch) {
    ImGui::TextDisabled("Removes every touch binding of yours.");
  } else {
    ImGui::TextDisabled("Removes this controller's own bindings, so it uses Controllers' again.");
  }
  if (ImGui::Button("Reset")) {
    ClearOwn(v);
    if (layout && v.family == Family::Keyboard) PortControls::ApplyKeyPresetNamed(kKeyPresets[0].id);
    if (layout && v.family == Family::Pad) PortControls::ApplyPadPresetNamed(kPadPresets[0].id);
    ImGui::CloseCurrentPopup();
  }
  ImGui::PopID();
}

// The reset buttons, one per device, behind a popup.
void DrawReset(const SViews& views) {
  using PortInput::Family;
  if (ImGui::Button("Reset...")) ImGui::OpenPopup("resetDevice");
  if (!ImGui::BeginPopup("resetDevice")) return;
  ResetSection(views[Family::Keyboard]);
  ResetSection(views[Family::Pad]);
  if (kShowTouch) ResetSection(views[Family::Touch]);
  ImGui::Separator();
  if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// A preset's Morph Ball rows, after ClearOwn unbinds them all (so each does its
// game button). The dual-stick layouts move R to the right stick click, so Spider
// Ball goes on lock-on instead, as in Remastered (LT, or Q on the keyboard).
void ApplyBallPreset(PortInput::Family family, std::string_view id) {
  const char* spider = nullptr;
  if (family == PortInput::Family::Pad && (id == "remastered" || id == "modern")) spider = "axis:lefttrigger+";
  if (family == PortInput::Family::Keyboard && id == "mouse") spider = "key:20"; // SDL_SCANCODE_Q
  Binding b;
  if (spider == nullptr || !PortInput::InputFromText(spider, b.inputs[0])) return;
  b.action = PortInput::Action::BallSpider;
  b.count = 1;
  PortInput::AddUserBinding(Ensure("", "Default"), b);
  Commit();
}

// Takes the saved bindings as the page's copy: a PortControls preset writes them
// itself, and the next Commit would otherwise put the page's older copy back.
void Reload() {
  sWork = *PortInputDevices::UserBindings();
  sSeenVersion = PortInputDevices::UserBindingsVersion();
  sLoaded = true;
}

// One row of preset buttons. A click replaces the family's own Default-profile
// bindings with the preset; a kind or model's own bindings stay on top.
void PresetRow(const SView& v, const SPreset* presets, int count) {
  using PortInput::Family;
  ImGui::PushID(int(v.family));
  ImGui::SeparatorText(kFamilies[int(v.family)]);
  const bool pads = v.family == Family::Pad;
  // A GameCube pad has no right stick (click) for the dual-stick layouts.
  const bool gcAdapter = pads && PADIsGCAdapter(0) != 0;
  for (int i = 0; i < count; ++i) {
    const std::string_view id = presets[i].id;
    if (i != 0) ImGui::SameLine();
    ImGui::BeginDisabled(gcAdapter && (id == "remastered" || id == "modern"));
    if (ImGui::Button(presets[i].name)) {
      ApplyPreset(v.family, id);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("%s", presets[i].tooltip);
    }
  }
  if (pads && !v.match.empty()) {
    ImGui::TextDisabled("Applies to all controllers; %s keeps its own bindings on top.",
                        (v.name.empty() ? v.match : v.name).c_str());
  }
  ImGui::PopID();
}

// The keyboard and controller presets, behind a popup.
void DrawPresets(const SViews& views) {
  using PortInput::Family;
  if (ImGui::Button("Presets...")) ImGui::OpenPopup("presets");
  ImGui::SetItemTooltip("Ready-made layouts: Classic or Mouse & keyboard for the keyboard; GameCube,\n"
                        "Remastered, Modern or Southpaw for controllers.");
  if (!ImGui::BeginPopup("presets")) return;
  ImGui::TextDisabled("A preset replaces your own bindings for that device.");
  PresetRow(views[Family::Keyboard], kKeyPresets, int(std::size(kKeyPresets)));
  PresetRow(views[Family::Pad], kPadPresets, int(std::size(kPadPresets)));
  ImGui::Separator();
  if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void DrawProfileOptions(const SView& v) {
  const UserProfile* user = Find(v.match);
  const bool isProfile = !v.match.empty();
  if (isProfile && user != nullptr) {
    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", user->name.c_str());
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) EditedProfile(v).name = nameBuf;
    if (ImGui::IsItemDeactivatedAfterEdit()) Commit();
    bool replace = user->replace;
    if (ImGui::Checkbox("Replace the built-in controls", &replace)) {
      EditedProfile(v).replace = replace;
      Commit();
    }
    ImGui::SetItemTooltip("On: only this device's bindings apply (with Controllers'). Off: they are "
                          "added on top, replacing what the other bindings give the same action.");
  }
  int window = user != nullptr ? user->chordWindowMs : 0;
  if (ImGui::SliderInt("Chord window (ms)", &window, 0, 200, window == 0 ? "Default" : "%d")) {
    EditedProfile(v).chordWindowMs = uint16_t(window);
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) Commit();
  ImGui::SetItemTooltip("How long an input that starts a chord waits for the rest before it acts alone.");
  if (isProfile && user != nullptr) {
    if (ImGui::Button("Delete this profile")) ImGui::OpenPopup("deleteProfile");
    if (ImGui::BeginPopup("deleteProfile")) {
      ImGui::Text("Delete %s and its %d bindings?", user->name.c_str(), int(user->bindings.size()));
      if (ImGui::Button("Delete")) {
        std::erase_if(sWork.profiles, [&](const UserProfile& p) { return p.match == v.match; });
        sPadDevice = "pads";
        sEditing = kEditNone;
        Commit();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }
}

void DrawActiveController() {
  const s32 activeIndex = PADGetIndexForPort(PAD_CHAN0);
  const char* activeName = activeIndex >= 0 ? PADGetNameForControllerIndex(static_cast<u32>(activeIndex)) : nullptr;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.f);
  if (ImGui::BeginCombo("Active controller", activeName != nullptr ? activeName : "No controller assigned")) {
    const u32 count = PADCount();
    if (count == 0) ImGui::TextDisabled("No controllers detected.");
    for (u32 index = 0; index < count; ++index) {
      SDL_Gamepad* pad = PADGetSDLGamepadForIndex(index);
      const char* name = PADGetNameForControllerIndex(index);
      ImGui::PushID(static_cast<int>(index));
      const bool selected = static_cast<s32>(index) == activeIndex;
      const std::string label = std::string(name != nullptr ? name : "Unknown controller") + " (" +
                                std::to_string(index + 1) + ")";
      if (ImGui::Selectable(label.c_str(), selected) && !selected) {
        EndCapture();
        sEditing = kEditNone;
        PADSetPortForIndex(index, PAD_CHAN0);
      }
      if (selected) ImGui::SetItemDefaultFocus();
      if (pad != nullptr) {
        ImGui::SetItemTooltip("SDL device %u%s", SDL_GetGamepadID(pad),
                             SDL_IsJoystickVirtual(SDL_GetGamepadID(pad)) ? " (virtual/touch)" : "");
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ImGui::SetItemTooltip("Controller used to play and record bindings. The choice is saved and restored on "
                        "reconnect. Keyboard and touch remain available. 'Controller bindings for' selects "
                        "the profile to edit, independently.");
}

// The player's saved presets (port_input_user_presets.h), listed once when the
// popup opens and after each save or delete; the files aren't read per frame.
std::vector<PortInputUserPresets::Entry> sSaved;
bool sSavedStale = true;
char sSaveName[PortInputUserPresets::kMaxName + 1] = {};
std::string sPendingDelete;

const std::vector<PortInputUserPresets::Entry>& Saved() {
  if (sSavedStale) {
    sSaved = PortInputUserPresets::ListAll();
    sSavedStale = false;
  }
  return sSaved;
}

// The page's copy goes stale when a preset writes the bindings, as for a built-in one.
void ApplySaved(const std::string& name) {
  EndCapture();
  sEditing = kEditNone;
  PortInputUserPresets::ApplyAll(name);
  Reload();
}

// The player's own saved presets, behind their own popup: one list, a preset
// covering every device it was saved with. One button applies it, a small x deletes
// it (after a confirm), and Save current as... adds one.
void DrawMyPresets() {
  if (ImGui::Button("My presets...")) {
    sSavedStale = true;
    ImGui::OpenPopup("myPresets");
  }
  ImGui::SetItemTooltip("Your own saved layouts: keep the current bindings as a preset, apply or delete one.");
  if (!ImGui::BeginPopup("myPresets")) return;
  ImGui::TextDisabled("Applying one replaces your own bindings for the devices it covers.");
  const std::vector<PortInputUserPresets::Entry> saved = Saved();
  if (saved.empty()) ImGui::TextDisabled("None saved yet.");
  for (size_t i = 0; i < saved.size(); ++i) {
    ImGui::PushID(int(i));
    if (ImGui::Button(saved[i].name.c_str())) {
      ApplySaved(saved[i].name);
      ImGui::CloseCurrentPopup();
    }
    std::string devices;
    const auto add = [&](bool has, const char* what) {
      if (!has) return;
      if (!devices.empty()) devices += ", ";
      devices += what;
    };
    add(saved[i].keyboard, "Keyboard & mouse");
    add(saved[i].pad, "Controllers");
    add(saved[i].touch, "Touch");
    ImGui::SetItemTooltip("Your preset: %s. Replaces your own bindings for those devices.", devices.c_str());
    ImGui::SameLine(0.f, 2.f);
    if (ImGui::SmallButton("x")) {
      sPendingDelete = saved[i].name;
      ImGui::OpenPopup("deleteSaved");
    }
    ImGui::SetItemTooltip("Delete this saved preset.");
    if (ImGui::BeginPopup("deleteSaved")) {
      ImGui::Text("Delete the preset %s?", sPendingDelete.c_str());
      if (ImGui::Button("Delete")) {
        PortInputUserPresets::DeleteAll(sPendingDelete);
        sSavedStale = true;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    ImGui::PopID();
    if (ImGui::GetContentRegionAvail().x > ImGui::GetFontSize() * 10.f && i + 1 < saved.size()) ImGui::SameLine();
  }
  if (ImGui::Button("Save current as...")) {
    sSaveName[0] = '\0';
    ImGui::OpenPopup("saveCurrent");
  }
  ImGui::SetItemTooltip("Keep the current bindings of every device as a preset of your own.\n"
                        "The controller part needs a controller connected.");
  if (ImGui::BeginPopup("saveCurrent")) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("Name", sSaveName, sizeof(sSaveName));
    const std::string name = PortInputUserPresets::CleanName(sSaveName);
    bool exists = false;
    for (const PortInputUserPresets::Entry& e : saved) {
      exists = exists || PortInputUserPresets::Slug(e.name) == PortInputUserPresets::Slug(name);
    }
    ImGui::BeginDisabled(name.empty());
    if (ImGui::Button(exists ? "Replace" : "Save")) {
      PortInputUserPresets::SaveAll(name);
      sSavedStale = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::Separator();
  if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

} // namespace

void ApplyPreset(PortInput::Family family, std::string_view id) {
  // The page may never have been drawn; start from the saved bindings.
  if (sEditing == kEditNone && !sCapture.active) Reload();
  // The presets set the all-controllers layer, so clear that one even
  // while a kind or model is selected.
  SView base;
  base.family = family;
  ClearOwn(base);
  if (family == PortInput::Family::Pad) {
    PortControls::ApplyPadPresetNamed(id);
  } else {
    PortControls::ApplyKeyPresetNamed(id);
  }
  Reload();
  ApplyBallPreset(family, id);
}

std::string BindingsText(PortInput::Action action, const char* separator) {
  // Hints ask every frame; the profile is rebuilt at most twice a second.
  struct SCached {
    PortInput::Action action;
    std::string separator;
    std::string text;
    uint64_t atMs;
  };
  static std::vector<SCached> sCache;
  const uint64_t now = SDL_GetTicks();
  for (const SCached& c : sCache) {
    if (c.action == action && c.separator == separator && now - c.atMs < 500) return c.text;
  }
  PortInput::Profile profile;
  PortInputDevices::BuildProfile(profile);
  std::vector<std::vector<Input>> seen;
  std::string text;
  for (const Binding& b : profile.bindings) {
    if (b.action != action || b.count == 0) continue;
    // A chord bound in both orders is listed once.
    std::vector<Input> set(b.inputs.begin(), b.inputs.begin() + b.count);
    std::sort(set.begin(), set.end(), [](const Input& x, const Input& y) {
      return PortInput::InputToText(x) < PortInput::InputToText(y);
    });
    if (std::find(seen.begin(), seen.end(), set) != seen.end()) continue;
    seen.push_back(set);
    if (!text.empty()) text += separator;
    for (int k = 0; k < b.count; ++k) {
      if (k != 0) text += " + ";
      text += InputName(b.inputs[k]);
    }
  }
  std::erase_if(sCache, [&](const SCached& c) { return c.action == action && c.separator == separator; });
  sCache.push_back({action, separator, text, now});
  return text;
}

bool Capturing() {
  // The game thread asks too; a capture left behind when the page closed or the
  // tab changed stops counting once the page isn't drawn.
  if (!sCaptureActive.load(std::memory_order_relaxed)) return false;
  return SDL_GetTicks() - sLastDrawMs.load(std::memory_order_relaxed) < kCaptureStaleMs;
}

void Draw() {
  const uint64_t now = SDL_GetTicks();
  if (sCapture.active && now - sLastDrawMs.load(std::memory_order_relaxed) >= kCaptureStaleMs) EndCapture();
  sLastDrawMs.store(now, std::memory_order_relaxed);
  // Picks up a hand edit or a deleted file, unless the page is mid-edit.
  const uint64_t version = PortInputDevices::UserBindingsVersion();
  if (!sLoaded || (version != sSeenVersion && sEditing == kEditNone && !sCapture.active)) {
    sWork = *PortInputDevices::UserBindings();
    sSeenVersion = version;
    sLoaded = true;
  }
  // A warp action's name is its room's, read from the disc.
  for (const PortInput::CustomActionDef& def : sWork.customActions) {
    if (def.preset == "warp") {
      PortCustomActions::LoadRooms();
      break;
    }
  }

  ImGui::TextWrapped("Each action lists its inputs for every device. Press + to add a binding; use Record and Save "
                     "in the editor. Click an input to change it, x removes it. Greyed inputs are "
                     "the defaults; Revert brings them back.");
  if (!sError.empty()) ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", sError.c_str());

  const float selectorWidth = ImGui::GetContentRegionAvail().x;
  DrawActiveController();
  if (selectorWidth >= ImGui::GetFontSize() * 70.f) ImGui::SameLine();

  // The profile selector changes which controller bindings are edited, not the active pad.
  const std::vector<SDeviceEntry> pads = PadEntries();
  if (std::none_of(pads.begin(), pads.end(), [](const SDeviceEntry& e) { return e.key == sPadDevice; })) {
    sPadDevice = "pads";
  }
  std::string current;
  for (const SDeviceEntry& e : pads) {
    if (e.key == sPadDevice) current = e.label;
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.f);
  if (ImGui::BeginCombo("Controller bindings for", current.c_str())) {
    for (const SDeviceEntry& e : pads) {
      if (ImGui::Selectable((e.label + "##" + e.key).c_str(), e.key == sPadDevice) && e.key != sPadDevice) {
        sPadDevice = e.key;
        sEditing = kEditNone;
        EndCapture();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SetItemTooltip("All controllers: bindings every controller uses. A kind or model gets its own bindings "
                        "on top of those; connected controllers are listed.");

  const SViews views = MakeViews();
  UpdateCapture();

  ImGui::SameLine();
  DrawPresets(views);
  ImGui::SameLine();
  DrawMyPresets();
  ImGui::SameLine();
  DrawReset(views);
  bool menuStandard = PortOverlay::MenuStandardButtons();
  if (ImGui::Checkbox("Separate menu buttons", &menuStandard)) PortOverlay::SetMenuStandardButtons(menuStandard);
  ImGui::SetItemTooltip(
      "On: menus and the map use the controller's \"Menus and map\" rows (by default A\n"
      "confirms, B backs out, the triggers page and zoom). Start and the map button keep\n"
      "their gameplay inputs. Keys and mouse buttons on those rows replace their\n"
      "gameplay action there.\nOff: menus and the map follow your gameplay bindings.");
  // MP2: MP1's "Point and click menus" and "Swap map finger gestures" are not here; this port
  // has no pointer menus.
  ImGui::Separator();

  const bool editorOnTop = sEditing != kEditNone && !sEditRowShown;
  if (editorOnTop) DrawEditor(views);
  DrawTable(views, editorOnTop);
  // Outside the tables, so the popups' IDs don't depend on the row that opened them.
  DrawCustomActionPopups();
  ImGui::Separator();
  if (ImGui::TreeNode("Advanced##page")) {
    DrawProfileOptions(views[PortInput::Family::Pad]);
    DrawEffective();
    ImGui::TreePop();
  }
}

} // namespace PortInputRemap
