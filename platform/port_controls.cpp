// Controls tab: rebinds keyboard/mouse and controller inputs to the emulated
// pad. The binding backend (matching, persistence, name helpers) is Aurora's.
// MP2: the keyboard has no Aurora key slots here; its presets are user bindings
// of the input engine (controls.toml), over PortInputDevices' keyboard profile.

#include "port_controls.h"
#include "port_input_bindings.h"
#include "port_input_map.h"
#include "port_mouse.h"
#include "port_overlay_internal.h"
#include "input/port_input_devices.h"

#include "MetroidPrime/CControlMapper.hpp"
#include "MetroidPrime/Tweaks/CTweakPlayerControls.hpp"

#include <dolphin/pad.h>
#include <imgui.h>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

namespace {

typedef CControlMapper::EFunctionList EFunctionList;

struct SControlPadButton {
  PADButton button;
  const char* label;
  EFunctionList function;
};
const SControlPadButton kControlPadButtons[] = {
    {PAD_BUTTON_A, "A", CControlMapper::kFL_AButton},
    {PAD_BUTTON_B, "B", CControlMapper::kFL_BButton},
    {PAD_BUTTON_X, "X", CControlMapper::kFL_XButton},
    {PAD_BUTTON_Y, "Y", CControlMapper::kFL_YButton},
    {PAD_TRIGGER_L, "L", CControlMapper::kFL_LeftTriggerPress},
    {PAD_TRIGGER_R, "R", CControlMapper::kFL_RightTriggerPress},
    {PAD_TRIGGER_Z, "Z", CControlMapper::kFL_ZButton},
    {PAD_BUTTON_START, "Start", CControlMapper::kFL_Start},
    {PAD_BUTTON_UP, "D-pad Up", CControlMapper::kFL_DPadUp},
    {PAD_BUTTON_DOWN, "D-pad Down", CControlMapper::kFL_DPadDown},
    {PAD_BUTTON_LEFT, "D-pad Left", CControlMapper::kFL_DPadLeft},
    {PAD_BUTTON_RIGHT, "D-pad Right", CControlMapper::kFL_DPadRight},
};

// Indexed by PADAxis.
struct SControlPadAxis {
  const char* label;
  EFunctionList function;
};
const SControlPadAxis kControlPadAxes[PAD_AXIS_COUNT] = {
    {"Stick Right", CControlMapper::kFL_LeftStickRight},
    {"Stick Left", CControlMapper::kFL_LeftStickLeft},
    {"Stick Up", CControlMapper::kFL_LeftStickUp},
    {"Stick Down", CControlMapper::kFL_LeftStickDown},
    {"C-Stick Right", CControlMapper::kFL_RightStickRight},
    {"C-Stick Left", CControlMapper::kFL_RightStickLeft},
    {"C-Stick Up", CControlMapper::kFL_RightStickUp},
    {"C-Stick Down", CControlMapper::kFL_RightStickDown},
    {"L Analog", CControlMapper::kFL_LeftTrigger},
    {"R Analog", CControlMapper::kFL_RightTrigger},
};

// MP2: MP1's PadBit (PortDebug::PadAltButton's index) is gone with the pad alt
// buttons it served.

// Controller layouts. GameCube is Aurora's default for the pad type; the others
// start from it. The twin-stick beam modifier is L or LB unless a pad beam shift
// is bound, so only Remastered (which binds one) uses LB.
enum class EPadPreset { kGameCube, kRemastered, kModern, kSouthpaw };

s32 OtherStick(s32 axis) {
  switch (axis) {
  case SDL_GAMEPAD_AXIS_LEFTX:
    return SDL_GAMEPAD_AXIS_RIGHTX;
  case SDL_GAMEPAD_AXIS_LEFTY:
    return SDL_GAMEPAD_AXIS_RIGHTY;
  case SDL_GAMEPAD_AXIS_RIGHTX:
    return SDL_GAMEPAD_AXIS_LEFTX;
  case SDL_GAMEPAD_AXIS_RIGHTY:
    return SDL_GAMEPAD_AXIS_LEFTY;
  default:
    return axis;
  }
}

// `port` is the pad port whose controller is remapped; the twin-stick switch is that port's
// player's (port 0 sets the global one, other ports their p2-p4 override).
void ApplyPadPreset(EPadPreset preset, u32 port) {
  PADRestoreDefaultMapping(port);
  // The GameCube layouts use the C-stick; the dual-stick ones turn twin-stick
  // back on. MP2 has no pad beam shift, scan/X-ray swap or pad alt buttons
  // (MP1 PortDebug settings), so those resets are gone.
  PortMouse::SetPlayerStickEnabled(port, false);
  switch (preset) {
  case EPadPreset::kGameCube:
    break;
  case EPadPreset::kRemastered: {
    // Remastered's Dual Sticks scheme: fire on RT, lock on with LT (the default
    // L), missile on RB, jump on the bottom face button, morph on the left one,
    // map on Back (Minus) and pause on Start (Plus). Free look (no Remastered
    // equivalent) goes on the right stick click. Aurora holds one button per
    // GameCube button, so the Remap preset adds the rest as engine rows (fire
    // on the right face button, jump on LB, the top face button's beam shift and
    // Spring Ball: port_input_remap_ui's ApplyRemasteredRows).
    const PADButtonMapping buttons[] = {
        {PAD_NATIVE_BUTTON_TRIGGER_RIGHT, PAD_BUTTON_A},
        {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_B},
        {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_BUTTON_Y},
        {SDL_GAMEPAD_BUTTON_BACK, PAD_TRIGGER_Z},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, PAD_TRIGGER_R},
    };
    for (const PADButtonMapping& mapping : buttons) {
      PADSetButtonMapping(port, mapping);
    }
    PADSetAxisMapping(port, {{-1, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_RIGHT_STICK, PAD_AXIS_TRIGGER_R});
    PortMouse::SetPlayerStickEnabled(port, true);
    break;
  }
  case EPadPreset::kModern: {
    // Fire on RT and lock on with LT (the default L), jump and morph on the
    // face buttons, free look on the right stick click, which also drives the
    // R analog so RT doesn't press R as well.
    const PADButtonMapping buttons[] = {
        {PAD_NATIVE_BUTTON_TRIGGER_RIGHT, PAD_BUTTON_A},
        {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_B},
        {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_BUTTON_Y},
        {SDL_GAMEPAD_BUTTON_NORTH, PAD_TRIGGER_Z},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, PAD_TRIGGER_R},
    };
    for (const PADButtonMapping& mapping : buttons) {
      PADSetButtonMapping(port, mapping);
    }
    PADSetAxisMapping(port, {{-1, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_RIGHT_STICK, PAD_AXIS_TRIGGER_R});
    // The right stick aims; without twin-stick it would be the C-stick.
    PortMouse::SetPlayerStickEnabled(port, true);
    break;
  }
  case EPadPreset::kSouthpaw: {
    u32 count = 0;
    PADAxisMapping* axes = PADGetAxisMappings(port, &count);
    for (u32 i = 0; axes != nullptr && i < count; ++i) {
      axes[i].nativeAxis.nativeAxis = OtherStick(axes[i].nativeAxis.nativeAxis);
    }
    break;
  }
  }
  PADSerializeMappings();
}

// A deadzone as a percentage of full travel; Aurora keeps raw SDL axis units.
// Returns true once an edit is finished, to save then rather than every frame.
bool ZoneSlider(const char* label, u16& zone, int minPercent, int maxPercent, const char* tooltip) {
  int percent = static_cast< int >(std::lround(zone * 100.0 / SDL_JOYSTICK_AXIS_MAX));
  if (ImGui::SliderInt(label, &percent, minPercent, maxPercent, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
    zone = static_cast< u16 >(std::lround(percent * SDL_JOYSTICK_AXIS_MAX / 100.0));
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
    ImGui::SetTooltip("%s", tooltip);
  }
  return ImGui::IsItemDeactivatedAfterEdit();
}

void DrawDeadZoneSliders(PADDeadZones& zones) {
  ImGui::SeparatorText("Sticks and triggers");
  bool save = false;
  const u16 stick = zones.stickDeadZone;
  const u16 substick = zones.substickDeadZone;
  save |= ZoneSlider("Stick deadzone", zones.stickDeadZone, 0, 50,
                     "How far the control stick moves before it registers.");
  save |= ZoneSlider("C-stick deadzone", zones.substickDeadZone, 0, 50,
                     "How far the right stick moves before it registers, for\n"
                     "twin-stick aim as well as the C-stick.");
  // A file written with deadzones off would make these sliders do nothing.
  if (zones.stickDeadZone != stick || zones.substickDeadZone != substick) {
    zones.useDeadzones = true;
  }
  save |= ZoneSlider("L click point", zones.leftTriggerActivationZone, 10, 95,
                     "How far the left trigger pulls before it also clicks L\n"
                     "(lock on). Lower it for triggers that don't reach the end.");
  save |= ZoneSlider("R click point", zones.rightTriggerActivationZone, 10, 95,
                     "How far the right trigger pulls before it also clicks R.");
  // Aurora's defaults, from GameController in lib/input.hpp.
  if (ImGui::Button("Reset sticks and triggers")) {
    zones.useDeadzones = true;
    zones.stickDeadZone = 8000;
    zones.substickDeadZone = 8000;
    zones.leftTriggerActivationZone = 31150;
    zones.rightTriggerActivationZone = 31150;
    save = true;
  }
  if (save) {
    PADSerializeMappings();
  }
}


// Keyboard layouts. Classic is the port's first-run layout; Mouse & keyboard
// is a PC shooter layout for mouse aim.
enum class EKeyPreset { kClassic, kMouseKeyboard };

// The pad input the disc's tweak gives a command, or the retail one before the
// tweaks load.
EFunctionList CommandFunction(CControlMapper::ECommands command, EFunctionList fallback) {
  const CTweakPlayerControls* tweak = gpTweakPlayerControlsA.get();
  return tweak != nullptr ? tweak->GetMapping(command) : fallback;
}

// The engine action that drives a pad function.
PortInput::Action FunctionAction(EFunctionList function) {
  using PortInput::Action;
  switch (function) {
  case CControlMapper::kFL_LeftStickUp: return Action::MainUp;
  case CControlMapper::kFL_LeftStickDown: return Action::MainDown;
  case CControlMapper::kFL_LeftStickLeft: return Action::MainLeft;
  case CControlMapper::kFL_LeftStickRight: return Action::MainRight;
  case CControlMapper::kFL_RightStickUp: return Action::CUp;
  case CControlMapper::kFL_RightStickDown: return Action::CDown;
  case CControlMapper::kFL_RightStickLeft: return Action::CLeft;
  case CControlMapper::kFL_RightStickRight: return Action::CRight;
  case CControlMapper::kFL_LeftTrigger: return Action::LAnalog;
  case CControlMapper::kFL_RightTrigger: return Action::RAnalog;
  case CControlMapper::kFL_DPadUp: return Action::PadUp;
  case CControlMapper::kFL_DPadDown: return Action::PadDown;
  case CControlMapper::kFL_DPadLeft: return Action::PadLeft;
  case CControlMapper::kFL_DPadRight: return Action::PadRight;
  case CControlMapper::kFL_AButton: return Action::PadA;
  case CControlMapper::kFL_BButton: return Action::PadB;
  case CControlMapper::kFL_XButton: return Action::PadX;
  case CControlMapper::kFL_YButton: return Action::PadY;
  case CControlMapper::kFL_ZButton: return Action::PadZ;
  case CControlMapper::kFL_LeftTriggerPress: return Action::PadL;
  case CControlMapper::kFL_RightTriggerPress: return Action::PadR;
  case CControlMapper::kFL_Start: return Action::PadStart;
  default: return Action::None;
  }
}

// A layout being built: MP1's per-slot key table, as the Default profile's
// keyboard-family user bindings. Every pad action the built-in keyboard layout
// binds a key to loses its keys (its mouse buttons stay), then gets the layout's.
struct SKeyLayout {
  PortInput::UserBindings bindings = *PortInputDevices::UserBindings();
  PortInput::UserProfile* user = nullptr;
  std::vector< PortInput::Action > padActions;

  SKeyLayout() {
    for (PortInput::UserProfile& p : bindings.profiles) {
      if (p.match.empty()) {
        user = &p;
        break;
      }
    }
    if (user == nullptr) {
      bindings.profiles.insert(bindings.profiles.begin(), PortInput::UserProfile{});
      user = &bindings.profiles.front();
      user->name = "Default";
    }
    // The pad inputs the built-in keyboard layout has keys for take their keyboard rows from the
    // whole built-in profile, so the mouse buttons on them (in and out of aim) stay; then their
    // keys go. The port's own keys (F1, F12) are not pad inputs and are left alone.
    PortInput::Profile inherited;
    PortInputDevices::BuildBaseProfile(inherited);
    for (const PortInput::Binding& b : PortInputDevices::BaseKeyboardProfile().bindings) {
      if (b.count == 0 || b.inputs[b.count - 1].device != PortInput::Device::Key) continue;
      PortInput::Materialize(*user, b.action, PortInput::Family::Keyboard, inherited);
      padActions.push_back(b.action);
    }
    for (size_t i = user->bindings.size(); i-- > 0;) {
      const PortInput::Binding& b = user->bindings[i];
      if (b.count != 0 && b.inputs[b.count - 1].device == PortInput::Device::Key &&
          std::find(padActions.begin(), padActions.end(), b.action) != padActions.end()) {
        PortInput::RemoveUserBinding(*user, i);
      }
    }
  }

  // Binds key to the pad input with that function.
  void Bind(EFunctionList function, s32 key) {
    const PortInput::Action action = FunctionAction(function);
    if (action == PortInput::Action::None) return;
    PortInput::Binding b;
    b.action = action;
    b.inputs[0].device = PortInput::Device::Key;
    b.inputs[0].code = static_cast< uint16_t >(key);
    b.count = 1;
    PortInput::AddUserBinding(*user, b);
  }

  void Apply() const { PortInputDevices::SetUserBindings(bindings); }
};

// Back to the built-in keyboard layout: the Default profile's keyboard-family
// overrides go.
void ResetKeyBindings() {
  PortInput::UserBindings bindings = *PortInputDevices::UserBindings();
  for (PortInput::UserProfile& p : bindings.profiles) {
    if (p.match.empty()) PortInput::ClearFamily(p, PortInput::Family::Keyboard);
  }
  PortInputDevices::SetUserBindings(bindings);
}

void ApplyKeyPreset(EKeyPreset preset) {
  switch (preset) {
  case EKeyPreset::kClassic:
    ResetKeyBindings();
    break;
  case EKeyPreset::kMouseKeyboard: {
    SKeyLayout layout;
    const struct {
      EFunctionList function;
      s32 key;
    } keys[] = {
        {CControlMapper::kFL_LeftStickUp, SDL_SCANCODE_W},
        {CControlMapper::kFL_LeftStickDown, SDL_SCANCODE_S},
        {CControlMapper::kFL_LeftStickLeft, SDL_SCANCODE_A},
        {CControlMapper::kFL_LeftStickRight, SDL_SCANCODE_D},
        {CControlMapper::kFL_AButton, SDL_SCANCODE_E},     // fire, menu confirm
        {CControlMapper::kFL_BButton, SDL_SCANCODE_SPACE}, // jump
        {CControlMapper::kFL_XButton, SDL_SCANCODE_LCTRL}, // morph ball
        {CControlMapper::kFL_XButton, SDL_SCANCODE_C},
        {CControlMapper::kFL_YButton, SDL_SCANCODE_F}, // missile
        {CControlMapper::kFL_LeftTriggerPress, SDL_SCANCODE_Q}, // lock on
        {CControlMapper::kFL_LeftTrigger, SDL_SCANCODE_Q},
        {CControlMapper::kFL_RightTriggerPress, SDL_SCANCODE_LALT}, // free look
        {CControlMapper::kFL_RightTrigger, SDL_SCANCODE_LALT},
        {CControlMapper::kFL_ZButton, SDL_SCANCODE_TAB}, // map
        {CControlMapper::kFL_ZButton, SDL_SCANCODE_M},
        {CControlMapper::kFL_Start, SDL_SCANCODE_RETURN},
    };
    for (const auto& entry : keys) {
      layout.Bind(entry.function, entry.key);
    }
    // Number keys pick beams and visors directly, through whichever C-stick
    // direction or D-pad button the disc gives each; arrows keep the D-pad.
    const struct {
      CControlMapper::ECommands command;
      EFunctionList fallback;
      s32 key;
    } direct[] = {
        {CControlMapper::kC_PowerBeam, CControlMapper::kFL_RightStickUp, SDL_SCANCODE_1},
        {CControlMapper::kC_WaveBeam, CControlMapper::kFL_RightStickRight, SDL_SCANCODE_2},
        {CControlMapper::kC_IceBeam, CControlMapper::kFL_RightStickDown, SDL_SCANCODE_3},
        {CControlMapper::kC_PlasmaBeam, CControlMapper::kFL_RightStickLeft, SDL_SCANCODE_4},
        {CControlMapper::kC_NoVisor, CControlMapper::kFL_DPadUp, SDL_SCANCODE_5},
        {CControlMapper::kC_EnviroVisor, CControlMapper::kFL_DPadLeft, SDL_SCANCODE_6},
        {CControlMapper::kC_ThermoVisor, CControlMapper::kFL_DPadDown, SDL_SCANCODE_7},
        {CControlMapper::kC_XrayVisor, CControlMapper::kFL_DPadRight, SDL_SCANCODE_8},
    };
    for (const auto& entry : direct) {
      layout.Bind(CommandFunction(entry.command, entry.fallback), entry.key);
    }
    layout.Bind(CControlMapper::kFL_DPadUp, SDL_SCANCODE_UP);
    layout.Bind(CControlMapper::kFL_DPadDown, SDL_SCANCODE_DOWN);
    layout.Bind(CControlMapper::kFL_DPadLeft, SDL_SCANCODE_LEFT);
    layout.Bind(CControlMapper::kFL_DPadRight, SDL_SCANCODE_RIGHT);
    layout.Apply();
    // Twin-stick leaves a C-stick held from the keyboard to the game, so 1-4
    // still pick beams while a pad's right stick aims alongside the mouse.
    PortMouse::SetEnabled(true);
    PortMouse::SetStickEnabled(true);
    break;
  }
  }
  // MP2: MP1 then binds the beam shift to Left Shift (no beam shift here) and
  // resets the mouse buttons' actions, which are fixed here (the keyboard
  // profile's DefaultMouseAction bindings, kept by the layout above).
}

} // namespace

namespace PortControls {

namespace {
std::atomic<int> sRemapPort{0};
}

int RemapPort() { return sRemapPort.load(std::memory_order_relaxed); }

void SetRemapPort(int port) { sRemapPort.store(std::clamp(port, 0, 3), std::memory_order_relaxed); }

SPadPortInfo PadPortInfo(unsigned port) {
  SPadPortInfo info;
  const s32 index = port <= PAD_CHAN3 ? PADGetIndexForPort(port) : -1;
  if (index < 0) return info;
  const char* name = PADGetNameForControllerIndex(static_cast<u32>(index));
  info.name = name != nullptr ? name : "Unknown controller";
  SDL_Gamepad* pad = PADGetSDLGamepadForIndex(static_cast<u32>(index));
  info.state = pad != nullptr && SDL_IsJoystickVirtual(SDL_GetGamepadID(pad)) ? EPadPortState::kVirtual
                                                                              : EPadPortState::kReal;
  return info;
}

// MP2: the default keyboard layout is PortInputDevices' built-in keyboard profile
// (MakeKeyboardProfile, MP1's layout), so the defaults are that with no
// keyboard overrides; port 0 is the only keyboard port.
void ApplyDefaultKeyBindings(unsigned port) {
  (void)port;
  ResetKeyBindings();
}

bool ApplyKeyPresetNamed(std::string_view name, unsigned port) {
  // Port 0 is the only keyboard port.
  if (port != PAD_CHAN0) return false;
  if (name == "classic") {
    ApplyKeyPreset(EKeyPreset::kClassic);
  } else if (name == "mouse") {
    ApplyKeyPreset(EKeyPreset::kMouseKeyboard);
  } else {
    return false;
  }
  // The presets turn mouse aim and Twin Stick Aim on or off; settings.cfg keeps them.
  PortOverlaySettings::MarkDirty();
  return true;
}

bool ApplyPadPresetNamed(std::string_view name, unsigned port) {
  if (port > PAD_CHAN3) return false;
  if (name == "gamecube") {
    ApplyPadPreset(EPadPreset::kGameCube, port);
  } else if (name == "remastered") {
    ApplyPadPreset(EPadPreset::kRemastered, port);
  } else if (name == "modern") {
    ApplyPadPreset(EPadPreset::kModern, port);
  } else if (name == "southpaw") {
    ApplyPadPreset(EPadPreset::kSouthpaw, port);
  } else {
    return false;
  }
  PortOverlaySettings::MarkDirty();
  return true;
}

void DrawDeadZones(unsigned port) {
  if (PADDeadZones* zones = PADGetDeadZones(port)) {
    DrawDeadZoneSliders(*zones);
  }
}

namespace {
// As the Remap page labels them (Xbox names, by SDL_GamepadButton).
std::string NativeName(u32 native) {
  static constexpr const char* kLabels[] = {
      "A",        "B",        "X",        "Y",          "Back",       "Guide",       "Start",
      "Left stick", "Right stick", "LB",   "RB",         "D-pad up",   "D-pad down",  "D-pad left",
      "D-pad right", "Share",  "Paddle 1", "Paddle 2",   "Paddle 3",   "Paddle 4",    "Touchpad",
      "Misc 2",   "Misc 3",   "Misc 4",   "Misc 5",     "Misc 6",
  };
  if (native == PAD_NATIVE_BUTTON_INVALID) return "(none)";
  if (native == PAD_NATIVE_BUTTON_TRIGGER_LEFT) return "LT";
  if (native == PAD_NATIVE_BUTTON_TRIGGER_RIGHT) return "RT";
  if (native < std::size(kLabels)) return kLabels[native];
  return "Button " + std::to_string(native);
}

PADButtonMapping* FindMapping(PADButtonMapping* list, u32 count, PADButton button) {
  for (u32 i = 0; i < count; ++i) {
    if (list[i].padButton == button) return &list[i];
  }
  return nullptr;
}

// L and R's analog follows their button, as the presets set it: a trigger drives it as an axis,
// any other input fully.
void SetTriggerAnalog(u32 port, PADButton button, u32 native) {
  if (button != PAD_TRIGGER_L && button != PAD_TRIGGER_R) return;
  const PADAxis axis = button == PAD_TRIGGER_L ? PAD_AXIS_TRIGGER_L : PAD_AXIS_TRIGGER_R;
  PADAxisMapping mapping{{-1, AXIS_SIGN_POSITIVE}, static_cast< s32 >(native), axis};
  if (native == PAD_NATIVE_BUTTON_TRIGGER_LEFT || native == PAD_NATIVE_BUTTON_TRIGGER_RIGHT) {
    mapping.nativeAxis.nativeAxis =
        native == PAD_NATIVE_BUTTON_TRIGGER_LEFT ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    mapping.nativeButton = SDL_GAMEPAD_BUTTON_INVALID;
  }
  PADSetAxisMapping(port, mapping);
}

// A mapping's input as the player sees it: Aurora's default L and R have no button and come from
// their trigger axis past its activation zone.
u32 ShownNative(u32 port, const PADButtonMapping* mapping) {
  if (mapping == nullptr) return PAD_NATIVE_BUTTON_INVALID;
  if (mapping->nativeButton != PAD_NATIVE_BUTTON_INVALID ||
      (mapping->padButton != PAD_TRIGGER_L && mapping->padButton != PAD_TRIGGER_R)) {
    return mapping->nativeButton;
  }
  const PADAxis axis = mapping->padButton == PAD_TRIGGER_L ? PAD_AXIS_TRIGGER_L : PAD_AXIS_TRIGGER_R;
  u32 count = 0;
  const PADAxisMapping* axes = PADGetAxisMappings(port, &count);
  for (u32 i = 0; axes != nullptr && i < count; ++i) {
    if (axes[i].padAxis != axis) continue;
    if (axes[i].nativeAxis.nativeAxis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) return PAD_NATIVE_BUTTON_TRIGGER_LEFT;
    if (axes[i].nativeAxis.nativeAxis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return PAD_NATIVE_BUTTON_TRIGGER_RIGHT;
  }
  return PAD_NATIVE_BUTTON_INVALID;
}
} // namespace

bool BindPadButton(unsigned port, unsigned padButton, unsigned native) {
  // Aurora keeps a virtual pad at its defaults; editing its list in place would get around that.
  if (port > PAD_CHAN3 || PadPortInfo(port).state != EPadPortState::kReal) return false;
  const PADButton button = static_cast< PADButton >(padButton);
  u32 count = 0;
  PADButtonMapping* list = PADGetButtonMappings(port, &count);
  PADButtonMapping* target = list != nullptr ? FindMapping(list, count, button) : nullptr;
  if (target == nullptr) return false;
  const u32 old = ShownNative(port, target);
  if (old == native && target->nativeButton == native) return true;
  // The GameCube button already on that input takes this one's old input, so nothing is lost.
  for (const SControlPadButton& row : kControlPadButtons) {
    PADButtonMapping* other = FindMapping(list, count, row.button);
    if (other != nullptr && other != target && ShownNative(port, other) == native) {
      other->nativeButton = old;
      SetTriggerAnalog(port, row.button, old);
    }
  }
  target->nativeButton = native;
  SetTriggerAnalog(port, button, native);
  PADSerializeMappings();
  return true;
}

std::string PadButtonsText(unsigned port) {
  u32 count = 0;
  PADButtonMapping* list = port <= PAD_CHAN3 ? PADGetButtonMappings(port, &count) : nullptr;
  if (list == nullptr) return "no controller";
  std::string text;
  for (const SControlPadButton& row : kControlPadButtons) {
    const PADButtonMapping* m = FindMapping(list, count, row.button);
    if (!text.empty()) text += ", ";
    text += std::string(row.label) + "=" + NativeName(ShownNative(port, m));
  }
  return text;
}

static bool EqualsNoCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast< unsigned char >(x)) == std::tolower(static_cast< unsigned char >(y));
         });
}

int PadButtonForLabel(std::string_view label) {
  for (const SControlPadButton& row : kControlPadButtons) {
    std::string_view name = row.label;
    if (name.starts_with("D-pad ")) name.remove_prefix(6);
    if (EqualsNoCase(name, label)) return row.button;
  }
  return -1;
}

long long NativeForLabel(std::string_view label) {
  if (EqualsNoCase(label, "none")) return PAD_NATIVE_BUTTON_INVALID;
  if (EqualsNoCase(label, "lt")) return PAD_NATIVE_BUTTON_TRIGGER_LEFT;
  if (EqualsNoCase(label, "rt")) return PAD_NATIVE_BUTTON_TRIGGER_RIGHT;
  for (u32 b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
    if (EqualsNoCase(NativeName(b), label)) return b;
  }
  if (label.empty() || label.size() > 2 ||
      !std::all_of(label.begin(), label.end(), [](char c) { return c >= '0' && c <= '9'; })) {
    return -1;
  }
  const int b = std::stoi(std::string(label));
  return b < SDL_GAMEPAD_BUTTON_COUNT ? b : -1;
}

} // namespace PortControls
