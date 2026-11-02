#include "../../input.hpp"
#include "../../device.hpp"
#include "../../internal.hpp"
#include "../../io.hpp"
#include <dolphin/pad.h>
#include <dolphin/si.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_timer.h>

#include <array>
#include <filesystem>
#include <limits>
#include <ranges>
#include <sys/stat.h>

namespace {
constexpr aurora::Module Log{"aurora::input"};

constexpr int32_t k_mappingsFileVersion = 4;
constexpr int32_t k_minMappingsFileVersion = 3;

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsStandard{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADStatus, PAD_CHANMAX> g_virtualPadStatus{};
std::array<bool, PAD_CHANMAX> g_virtualPadActive{};
// Port addition to the vendored Aurora (MetroidPrime2Port, after the Prime 1 port's): a one-shot
// status that replaces the controller + keyboard merge for a port on the next PADRead.
std::array<PADStatus, PAD_CHANMAX> g_portOverride{};
std::array<bool, PAD_CHANMAX> g_portOverrideSet{};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsXBox360{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsXBoxOne{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsPS3{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsPS4{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsPS5{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsGamecube{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {SDL_GAMEPAD_BUTTON_MISC3, PAD_TRIGGER_L},
    {SDL_GAMEPAD_BUTTON_MISC4, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsNSOGamecube{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_BACK, PAD_TRIGGER_Z},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PAD_TRIGGER_L},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

// Nintendo pads map by printed label (A on the right, B at the bottom, X on
// top, Y on the left), so the glyphs and the buttons agree.
std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsProCon{{
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsJoyConRight{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsJoyConLeft{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsJoyPair{{
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADKeyButtonBinding, PAD_BUTTON_COUNT> g_defaultKeys{{
    {PAD_KEY_INVALID, PAD_BUTTON_A},
    {PAD_KEY_INVALID, PAD_BUTTON_B},
    {PAD_KEY_INVALID, PAD_BUTTON_X},
    {PAD_KEY_INVALID, PAD_BUTTON_Y},
    {PAD_KEY_INVALID, PAD_BUTTON_START},
    {PAD_KEY_INVALID, PAD_TRIGGER_Z},
    {PAD_KEY_INVALID, PAD_TRIGGER_L},
    {PAD_KEY_INVALID, PAD_TRIGGER_R},
    {PAD_KEY_INVALID, PAD_BUTTON_UP},
    {PAD_KEY_INVALID, PAD_BUTTON_DOWN},
    {PAD_KEY_INVALID, PAD_BUTTON_LEFT},
    {PAD_KEY_INVALID, PAD_BUTTON_RIGHT},
}};

std::array<PADKeyAxisBinding, PAD_AXIS_COUNT> g_defaultKeyAxis{{
    {PAD_KEY_INVALID, PAD_AXIS_LEFT_X_POS, 0},
    {PAD_KEY_INVALID, PAD_AXIS_LEFT_X_NEG, 0},
    {PAD_KEY_INVALID, PAD_AXIS_LEFT_Y_POS, 0},
    {PAD_KEY_INVALID, PAD_AXIS_LEFT_Y_NEG, 0},
    {PAD_KEY_INVALID, PAD_AXIS_RIGHT_X_POS, 0},
    {PAD_KEY_INVALID, PAD_AXIS_RIGHT_X_NEG, 0},
    {PAD_KEY_INVALID, PAD_AXIS_RIGHT_Y_POS, 0},
    {PAD_KEY_INVALID, PAD_AXIS_RIGHT_Y_NEG, 0},
    {PAD_KEY_INVALID, PAD_AXIS_TRIGGER_L, 0},
    {PAD_KEY_INVALID, PAD_AXIS_TRIGGER_R, 0},
}};

std::array<PADAxisMapping, PAD_AXIS_COUNT> g_defaultAxes{{
    {{SDL_GAMEPAD_AXIS_LEFTX, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_X_POS},
    {{SDL_GAMEPAD_AXIS_LEFTX, AXIS_SIGN_NEGATIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_X_NEG},
    // SDL's gamepad y-axis is inverted from GC's
    {{SDL_GAMEPAD_AXIS_LEFTY, AXIS_SIGN_NEGATIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_Y_POS},
    {{SDL_GAMEPAD_AXIS_LEFTY, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_Y_NEG},
    {{SDL_GAMEPAD_AXIS_RIGHTX, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_X_POS},
    {{SDL_GAMEPAD_AXIS_RIGHTX, AXIS_SIGN_NEGATIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_X_NEG},
    // see above
    {{SDL_GAMEPAD_AXIS_RIGHTY, AXIS_SIGN_NEGATIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_Y_POS},
    {{SDL_GAMEPAD_AXIS_RIGHTY, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_Y_NEG},
    {{SDL_GAMEPAD_AXIS_LEFT_TRIGGER, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_TRIGGER_L},
    {{SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_TRIGGER_R},
}};

template <typename T, size_t N>
constexpr const std::array<T, N>& toStdArray(const T (&array)[N]) {
  static_assert(sizeof(array) == sizeof(std::array<T, N>));
  return reinterpret_cast<const std::array<T, N>&>(array);
}

struct PADKeyboardSlot {
  std::array<PADKeyButtonBinding, PAD_BUTTON_COUNT> m_buttonMapping = g_defaultKeys;
  std::array<PADKeyAxisBinding, PAD_AXIS_COUNT> m_axisMapping = g_defaultKeyAxis;
};

struct PADKeyboardState {
  std::array<PADKeyboardSlot, PAD_KEY_SLOT_COUNT> m_slots{};
  bool m_mappingsSet = false;
};

std::array<PADKeyboardState, PAD_MAX_CONTROLLERS> g_keyboardBindings;

struct PADCLampRegion {
  uint8_t minTrigger;
  uint8_t maxTrigger;
  int8_t minStick;
  int8_t maxStick;
  int8_t xyStick;
  int8_t minSubstick;
  int8_t maxSubstick;
  int8_t xySubstick;
  int8_t radStick;
  int8_t radSubstick;
};

constexpr PADCLampRegion ClampRegion{
    // Triggers
    30,
    180,

    // Left stick
    15,
    72,
    40,

    // Right stick
    15,
    59,
    31,

    // Stick radii
    56,
    44,
};

bool g_initialized;
bool g_keyboardBindingsLoaded = false;
bool g_blockPAD = false;
bool g_suppressHeldOnRead = false;
std::array<PADButton, PAD_CHANMAX> g_suppressedButtons{};
std::array<bool, PAD_CHANMAX> g_suppressLeftTrigger{};
std::array<bool, PAD_CHANMAX> g_suppressRightTrigger{};

bool is_mouse_scancode(const s32 scancode) { return scancode < PAD_KEY_INVALID; }
bool is_mouse_button_pressed(const s32 scancode) {
  const int32_t buttonNum = -(scancode + 1);
  if (buttonNum < 1 || buttonNum > 5) {
    return false;
  }
  float x, y;
  const auto buttons = SDL_GetMouseState(&x, &y);
  return (buttons & 1u << (buttonNum - 1)) != 0u;
}
} // namespace

void PADSetSpec(u32 spec [[maybe_unused]]) {}

static void load_keyboard_bindings();
static void save_keyboard_bindings();

static bool seek_aligned(SDL_IOStream* file, Sint64& dataStart) {
  const Sint64 position = SDL_TellIO(file);
  if (position < 0 || position > std::numeric_limits<Sint64>::max() - 31) {
    return false;
  }
  dataStart = (position + 31) & ~Sint64{31};
  return SDL_SeekIO(file, dataStart, SDL_IO_SEEK_SET) == dataStart;
}

static bool device_rumble_available_for_port(const u32 port) {
  return port == PAD_CHAN0 && aurora::device::rumble_available();
}

static bool should_use_device_rumble(const u32 port, const aurora::input::GameController* controller) {
  return device_rumble_available_for_port(port) &&
         (controller == nullptr ||
          (!controller->m_isGameCube && (!controller->m_hasRumble || controller->m_forceDeviceRumble)));
}

static bool device_gyro_available_for_port(const u32 port) {
  return port == PAD_CHAN0 && aurora::device::gyro_available();
}

static bool device_accel_available_for_port(const u32 port) {
  return port == PAD_CHAN0 && aurora::device::accel_available();
}

static bool device_sensor_available_for_port(const u32 port, const PADSensorType sensor) {
  switch (sensor) {
  case PAD_SENSOR_ACCEL:
    return device_accel_available_for_port(port);
  case PAD_SENSOR_GYRO:
    return device_gyro_available_for_port(port);
  default:
    return false;
  }
}

static bool get_device_sensor_data(const PADSensorType sensor, f32* data, const int nValues) {
  switch (sensor) {
  case PAD_SENSOR_ACCEL:
    return aurora::device::accel(data, nValues);
  case PAD_SENSOR_GYRO:
    return aurora::device::gyro(data, nValues);
  default:
    return false;
  }
}

static bool controller_has_sensor(const aurora::input::GameController* controller, const PADSensorType sensor) {
  return controller != nullptr && SDL_GamepadHasSensor(controller->m_controller, static_cast<SDL_SensorType>(sensor));
}

static bool should_use_device_sensor(const u32 port, const aurora::input::GameController* controller,
                                     const PADSensorType sensor) {
  return device_sensor_available_for_port(port, sensor) && !controller_has_sensor(controller, sensor);
}

// ReSharper disable once CppDFAConstantFunctionResult
BOOL PADInit() {
  if (g_initialized) {
    return true;
  }
  g_initialized = true;

  std::ranges::for_each(g_keyboardBindings, [](auto& state) { state.m_slots.fill(PADKeyboardSlot{}); });

  if (!g_keyboardBindingsLoaded) {
    g_keyboardBindingsLoaded = true;
    load_keyboard_bindings();
  }

  return true;
}

BOOL PADRecalibrate(u32 mask [[maybe_unused]]) { return true; }

BOOL PADReset(u32 mask [[maybe_unused]]) { return true; }

void PADSetAnalogMode(u32 mode [[maybe_unused]]) {}

aurora::input::GameController* __PADGetControllerForIndex(const u32 idx) /*  NOLINT(*-reserved-identifier) */
{
  if (idx >= aurora::input::g_GameControllers.size()) {
    return nullptr;
  }

  uint32_t tmp = 0;
  auto iter = aurora::input::g_GameControllers.begin();
  while (tmp < idx) {
    ++iter;
    ++tmp;
  }
  if (iter == aurora::input::g_GameControllers.end()) {
    return nullptr;
  }

  return &iter->second;
}

u32 PADCount() { return aurora::input::g_GameControllers.size(); }

const char* PADGetNameForControllerIndex(const u32 idx) {
  const auto* ctrl = __PADGetControllerForIndex(idx);
  if (ctrl == nullptr) {
    return nullptr;
  }

  return SDL_GetGamepadName(ctrl->m_controller);
}

void PADSetPortForIndex(const u32 idx, const u32 port) {
  const auto* ctrl = __PADGetControllerForIndex(idx);
  if (ctrl == nullptr) {
    return;
  }

  const int32_t oldPort = SDL_GetGamepadPlayerIndex(ctrl->m_controller);
  if (const auto* dest = aurora::input::get_controller_for_player(port); dest != nullptr && dest != ctrl) {
    SDL_SetGamepadPlayerIndex(dest->m_controller, -1);
  }
  if (oldPort >= 0 && oldPort != port) {
    aurora::input::persist_controller_for_player(oldPort, nullptr);
  }
  SDL_SetGamepadPlayerIndex(ctrl->m_controller, static_cast<Sint32>(port));
  aurora::input::persist_controller_for_player(port, ctrl);
}

int32_t PADGetIndexForPort(const u32 port) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return -1;
  }
  int32_t index = 0;
  for (auto iter = aurora::input::g_GameControllers.begin(); iter != aurora::input::g_GameControllers.end();
       ++iter, ++index) {
    if (&iter->second == ctrl) {
      break;
    }
  }

  return index;
}

void PADClearPort(const u32 port) {
  aurora::input::persist_controller_for_player(port, nullptr);
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return;
  }
  SDL_SetGamepadPlayerIndex(ctrl->m_controller, -1);
}

// SDL also types pads as GameCube by VID/PID alone (PowerA's 20d6:a711 is shared with a Switch-style pad), and
// those have no full-pull click on MISC3/MISC4. Bound to it, L and R could never press, and the analog fallback
// in PADRead is skipped; unbound, the trigger past its click point presses them. Also run on saved mappings,
// which hold the old default for such pads.
static void UnbindMissingTriggerClick(aurora::input::GameController* controller) {
  if (SDL_GamepadHasButton(controller->m_controller, SDL_GAMEPAD_BUTTON_MISC3)) {
    return;
  }
  bool changed = false;
  for (auto& mapping : controller->m_buttonMapping) {
    if ((mapping.padButton == PAD_TRIGGER_L || mapping.padButton == PAD_TRIGGER_R) &&
        (mapping.nativeButton == SDL_GAMEPAD_BUTTON_MISC3 || mapping.nativeButton == SDL_GAMEPAD_BUTTON_MISC4)) {
      mapping.nativeButton = PAD_NATIVE_BUTTON_INVALID;
      changed = true;
    }
  }
  if (changed) {
    Log.info("{} has no trigger click: L and R follow the analog triggers",
             SDL_GetGamepadName(controller->m_controller));
  }
}

void __PADSetDefaultMapping(aurora::input::GameController* controller) /*  NOLINT(*-reserved-identifier) */
{
  switch (SDL_GetGamepadType(controller->m_controller)) {
  case SDL_GAMEPAD_TYPE_XBOX360:
    controller->m_buttonMapping = g_defaultButtonsXBox360;
    break;
  case SDL_GAMEPAD_TYPE_XBOXONE:
    controller->m_buttonMapping = g_defaultButtonsXBoxOne;
    break;
  case SDL_GAMEPAD_TYPE_STANDARD:
    controller->m_buttonMapping = g_defaultButtonsStandard;
    break;
  case SDL_GAMEPAD_TYPE_PS3:
    controller->m_buttonMapping = g_defaultButtonsPS3;
    break;
  case SDL_GAMEPAD_TYPE_PS4:
    controller->m_buttonMapping = g_defaultButtonsPS4;
    break;
  case SDL_GAMEPAD_TYPE_PS5:
    controller->m_buttonMapping = g_defaultButtonsPS5;
    break;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    if (controller->m_pid == 0x2073) {
      controller->m_buttonMapping = g_defaultButtonsNSOGamecube;
    } else {
      controller->m_buttonMapping = g_defaultButtonsProCon;
    }
    break;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    controller->m_buttonMapping = g_defaultButtonsJoyConRight;
    break;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    controller->m_buttonMapping = g_defaultButtonsJoyConLeft;
    break;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    controller->m_buttonMapping = g_defaultButtonsJoyPair;
    break;
  case SDL_GAMEPAD_TYPE_GAMECUBE:
    controller->m_buttonMapping = g_defaultButtonsGamecube;
    UnbindMissingTriggerClick(controller);
    break;
  default:
    controller->m_buttonMapping = g_defaultButtonsStandard;
    break;
  }
}

// The on-screen touch pad is a virtual gamepad that always uses the default (GameCube-labelled) mapping:
// never loaded from disk, never saved, and ignoring the setters below.
static bool is_virtual_controller(const aurora::input::GameController* controller) {
  return SDL_IsJoystickVirtual(controller->m_index);
}

void __PADLoadMapping(aurora::input::GameController* controller) /*  NOLINT(*-reserved-identifier) */ {
  int32_t playerIndex = SDL_GetGamepadPlayerIndex(controller->m_controller);
  if (playerIndex < 0 || aurora::g_config.userPath == nullptr) {
    return;
  }

  if (!controller->m_mappingLoaded) {
    __PADSetDefaultMapping(controller);
    controller->m_axisMapping = g_defaultAxes;
  }

  controller->m_mappingLoaded = true;
  if (is_virtual_controller(controller)) {
    return;
  }

  const auto path = aurora::io::fs_path_from_string(aurora::g_config.userPath) /
                    fmt::format("{}_{:04X}_{:04X}.controller", PADGetName(playerIndex), controller->m_vid,
                                controller->m_pid);
  auto file = aurora::io::open_file(path, "rb");
  if (!file) {
    return;
  }

  uint32_t magic = 0;
  if (!SDL_ReadU32LE(file.get(), &magic) || magic != SBIG('CTRL')) {
    Log.warn("Invalid controller mapping magic!");
    return;
  }

  uint32_t version = 0;
  if (!SDL_ReadU32LE(file.get(), &version) || version < k_minMappingsFileVersion ||
      version > k_mappingsFileVersion) {
    Log.warn("Invalid controller mapping version! (Expected {0}..{1}, found {2})", k_minMappingsFileVersion,
             k_mappingsFileVersion, version);
    return;
  }

  uint8_t isGameCubeValue = 0;
  Sint64 dataStart = 0;
  if (!SDL_ReadU8(file.get(), &isGameCubeValue) || !seek_aligned(file.get(), dataStart)) {
    Log.warn("Unable to read controller bindings header! Path: \"{}\"", aurora::io::fs_path_to_string(path));
    return;
  }
  const bool isGameCube = isGameCubeValue != 0;
  if (isGameCube) {
    if (playerIndex >= PAD_CHANMAX) {
      return;
    }
    constexpr uint32_t dzSecLen = sizeof(PADDeadZones);
    constexpr uint32_t btnSecLen = sizeof(PADButtonMapping) * PAD_BUTTON_COUNT;
    constexpr uint32_t axisSecLen = sizeof(PADAxisMapping) * PAD_AXIS_COUNT;
    const Sint64 portOffset = dataStart + (dzSecLen + btnSecLen + axisSecLen) * playerIndex;
    if (SDL_SeekIO(file.get(), portOffset, SDL_IO_SEEK_SET) != portOffset) {
      Log.warn("Unable to seek in controller bindings! Path: \"{}\"", aurora::io::fs_path_to_string(path));
      return;
    }
  }

  auto deadZones = controller->m_deadZones;
  auto buttonMapping = controller->m_buttonMapping;
  auto axisMapping = controller->m_axisMapping;
  auto rumbleIntensityLow = controller->m_rumbleIntensityLow;
  auto rumbleIntensityHigh = controller->m_rumbleIntensityHigh;
  auto forceDeviceRumble = controller->m_forceDeviceRumble;
  bool ok = aurora::io::read_exact(file.get(), &deadZones, sizeof(deadZones)) &&
            aurora::io::read_exact(file.get(), buttonMapping.data(), sizeof(buttonMapping)) &&
            aurora::io::read_exact(file.get(), axisMapping.data(), sizeof(axisMapping));
  if (!isGameCube) {
    ok = ok && SDL_ReadU16LE(file.get(), &rumbleIntensityLow) && SDL_ReadU16LE(file.get(), &rumbleIntensityHigh);
    if (version >= k_mappingsFileVersion) {
      uint8_t forceDeviceRumbleValue = 0;
      ok = ok && SDL_ReadU8(file.get(), &forceDeviceRumbleValue);
      forceDeviceRumble = forceDeviceRumbleValue != 0;
    }
  }
  if (!ok) {
    Log.warn("Unable to read controller bindings! Path: \"{}\"", aurora::io::fs_path_to_string(path));
    return;
  }
  controller->m_deadZones = deadZones;
  controller->m_buttonMapping = buttonMapping;
  controller->m_axisMapping = axisMapping;
  controller->m_rumbleIntensityLow = rumbleIntensityLow;
  controller->m_rumbleIntensityHigh = rumbleIntensityHigh;
  controller->m_forceDeviceRumble = forceDeviceRumble;

  bool axisCorrupt = false;
  for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
    if (controller->m_axisMapping[i].padAxis != static_cast<PADAxis>(i)) {
      axisCorrupt = true;
      break;
    }
  }
  if (axisCorrupt) {
    Log.warn("__PADLoadMapping port={}: corrupt axis data in file, resetting axes to defaults", playerIndex);
    controller->m_axisMapping = g_defaultAxes;
  }

  bool buttonCorrupt = false;
  for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
    if (controller->m_buttonMapping[i].padButton == 0) {
      buttonCorrupt = true;
      break;
    }
  }
  if (buttonCorrupt) {
    Log.warn("__PADLoadMapping port={}: corrupt button data in file, resetting buttons to defaults", playerIndex);
    __PADSetDefaultMapping(controller);
  } else {
    UnbindMissingTriggerClick(controller);
  }
}

static void EnsureMappingLoaded(aurora::input::GameController* controller) {
  if (!controller->m_mappingLoaded) {
    __PADLoadMapping(controller);
  }
}

// A button mapping's native button, which may be an analog trigger.
static bool native_button_held(const aurora::input::GameController* controller, u32 nativeButton) {
  if (nativeButton == PAD_NATIVE_BUTTON_TRIGGER_LEFT || nativeButton == PAD_NATIVE_BUTTON_TRIGGER_RIGHT) {
    const auto axis =
        nativeButton == PAD_NATIVE_BUTTON_TRIGGER_LEFT ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    return SDL_GetGamepadAxis(controller->m_controller, axis) >= 16384;
  }
  if (nativeButton >= SDL_GAMEPAD_BUTTON_COUNT) {
    return false;
  }
  return SDL_GetGamepadButton(controller->m_controller, static_cast<SDL_GamepadButton>(nativeButton));
}

static Sint16 _get_axis_value(const aurora::input::GameController* controller, //  NOLINT(*-reserved-identifier)
                              PADAxis axis) {
  const auto iter =
      std::ranges::find_if(controller->m_axisMapping, [axis](const auto& pair) { return pair.padAxis == axis; });
  if (iter == controller->m_axisMapping.end()) {
    return 0;
  }

  if (iter->nativeAxis.nativeAxis != -1) {
    const auto [nativeAxis, sign] = iter->nativeAxis;
    // clamp value to avoid overflow when casting to Sint16 if -32768 is negated
    return static_cast<Sint16>(
        std::min(SDL_GetGamepadAxis(controller->m_controller, static_cast<SDL_GamepadAxis>(nativeAxis)) * sign,
                 SDL_JOYSTICK_AXIS_MAX));
  }

  if (native_button_held(controller, static_cast<u32>(iter->nativeButton))) {
    return SDL_JOYSTICK_AXIS_MAX;
  }
  return 0;
}

// Whether an L/R analog's input also presses another PAD button (A on the right
// trigger, say, kept there by binding both). Games read L/R's analog value on
// its own (the pause screen pages on it, and the analog press comes before the
// half-pull click), so that input then drives only the button.
static bool trigger_axis_taken(const aurora::input::GameController* controller, PADAxis axis) {
  const auto iter =
      std::ranges::find_if(controller->m_axisMapping, [axis](const auto& pair) { return pair.padAxis == axis; });
  if (iter == controller->m_axisMapping.end()) {
    return false;
  }
  u32 native = static_cast<u32>(iter->nativeButton);
  if (iter->nativeAxis.nativeAxis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
    native = PAD_NATIVE_BUTTON_TRIGGER_LEFT;
  } else if (iter->nativeAxis.nativeAxis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
    native = PAD_NATIVE_BUTTON_TRIGGER_RIGHT;
  } else if (iter->nativeAxis.nativeAxis != -1) {
    return false;
  }
  if (native == PAD_NATIVE_BUTTON_INVALID) {
    return false;
  }
  bool other = false;
  for (const auto& mapping : controller->m_buttonMapping) {
    if (mapping.nativeButton != native) {
      continue;
    }
    if (mapping.padButton == PAD_TRIGGER_L || mapping.padButton == PAD_TRIGGER_R) {
      return false;
    }
    other = true;
  }
  return other;
}

static void neutralize_status(PADStatus& status) {
  status.button = 0;
  status.stickX = 0;
  status.stickY = 0;
  status.substickX = 0;
  status.substickY = 0;
  status.triggerLeft = 0;
  status.triggerRight = 0;
  status.analogA = 0;
  status.analogB = 0;
}

static void apply_unblock_suppression(PADStatus& status, const u32 port, const bool captureHeldInput) {
  if (captureHeldInput) {
    g_suppressedButtons[port] |= status.button;
    g_suppressLeftTrigger[port] = g_suppressLeftTrigger[port] || status.triggerLeft > ClampRegion.minTrigger;
    g_suppressRightTrigger[port] = g_suppressRightTrigger[port] || status.triggerRight > ClampRegion.minTrigger;
  }

  g_suppressedButtons[port] &= status.button;
  status.button &= ~g_suppressedButtons[port];

  if (g_suppressLeftTrigger[port]) {
    if (status.triggerLeft <= ClampRegion.minTrigger) {
      g_suppressLeftTrigger[port] = false;
    } else {
      status.triggerLeft = 0;
    }
  }

  if (g_suppressRightTrigger[port]) {
    if (status.triggerRight <= ClampRegion.minTrigger) {
      g_suppressRightTrigger[port] = false;
    } else {
      status.triggerRight = 0;
    }
  }
}

static int dominant_axis_value(const int physical, const int virtualValue, const int min, const int max) {
  return std::clamp(std::abs(virtualValue) > std::abs(physical) ? virtualValue : physical, min, max);
}

// A Wii U adapter pad on Aurora's SDL sends the bytes the pad read
// (apply-sdl3-gamecube-raw-axes.cmake). Those take the console's path: the
// origin from plug-in, or from holding X+Y+Start for 3 seconds, comes off as in
// the SDK's PADRead, with no deadzone; the game's PADClamp does the rest.
// Remapped axes keep the SDL path.
static SDL_Joystick* gamecube_raw_joystick(const aurora::input::GameController* controller) {
  if (!controller->m_isGameCube) {
    return nullptr;
  }
  SDL_Joystick* joystick = SDL_GetGamepadJoystick(controller->m_controller);
  if (joystick == nullptr ||
      !SDL_GetBooleanProperty(SDL_GetJoystickProperties(joystick), "SDL.joystick.gamecube.raw_axes", false)) {
    return nullptr;
  }
  const bool defaultAxes = std::ranges::equal(controller->m_axisMapping, g_defaultAxes, [](const auto& a, const auto& b) {
    return a.nativeAxis.nativeAxis == b.nativeAxis.nativeAxis && a.nativeAxis.sign == b.nativeAxis.sign &&
           a.nativeButton == b.nativeButton && a.padAxis == b.padAxis;
  });
  return defaultAxes ? joystick : nullptr;
}

static void read_gamecube_raw_axes(aurora::input::GameController* controller, SDL_Joystick* joystick,
                                   PADStatus& status, u16 buttonsBefore) {
  std::array<u8, 6> raw{};
  for (int axis = 0; axis < 6; ++axis) {
    raw[axis] = static_cast<u8>(SDL_GetJoystickAxis(joystick, axis) / 256 + 128);
  }

  // The pad's own X, Y and Start (driver buttons 2, 3, 8), whatever they are bound to.
  const Uint64 now = SDL_GetTicks();
  if (SDL_GetJoystickButton(joystick, 2) && SDL_GetJoystickButton(joystick, 3) && SDL_GetJoystickButton(joystick, 8)) {
    if (controller->m_gcRecalibrateStart == 0) {
      controller->m_gcRecalibrateStart = now;
    } else if (now - controller->m_gcRecalibrateStart >= 3000) {
      controller->m_gcOriginValid = false;
      controller->m_gcRecalibrateStart = now;
    }
  } else {
    controller->m_gcRecalibrateStart = 0;
  }
  if (!controller->m_gcOriginValid) {
    controller->m_gcOrigin = raw;
    controller->m_gcOriginValid = true;
  }

  // ClampS8 and ClampU8 in the SDK's pad.c.
  const auto stick = [&](int axis) {
    int value = raw[axis] - 128;
    const int origin = controller->m_gcOrigin[axis] - 128;
    if (origin > 0) {
      value = std::max(value, -128 + origin);
    } else if (origin < 0) {
      value = std::min(value, 127 + origin);
    }
    return value - origin;
  };
  const auto trigger = [&](int axis) { return std::max<int>(raw[axis], controller->m_gcOrigin[axis]) - controller->m_gcOrigin[axis]; };

  status.stickX = static_cast<s8>(dominant_axis_value(status.stickX, stick(SDL_GAMEPAD_AXIS_LEFTX), -128, 127));
  status.stickY = static_cast<s8>(dominant_axis_value(status.stickY, stick(SDL_GAMEPAD_AXIS_LEFTY), -128, 127));
  status.substickX = static_cast<s8>(dominant_axis_value(status.substickX, stick(SDL_GAMEPAD_AXIS_RIGHTX), -128, 127));
  status.substickY = static_cast<s8>(dominant_axis_value(status.substickY, stick(SDL_GAMEPAD_AXIS_RIGHTY), -128, 127));
  status.triggerLeft = std::max(status.triggerLeft, static_cast<u8>(trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)));
  status.triggerRight = std::max(status.triggerRight, static_cast<u8>(trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)));

  // A keyboard key on L or R still reads as a full analog press.
  if (buttonsBefore & PAD_TRIGGER_L) {
    status.triggerLeft = 180;
  }
  if (buttonsBefore & PAD_TRIGGER_R) {
    status.triggerRight = 180;
  }
}

static void merge_virtual_status(PADStatus& status, const PADStatus& virtualStatus) {
  status.button |= virtualStatus.button;
  status.extButton |= virtualStatus.extButton;
  status.stickX = static_cast<s8>(dominant_axis_value(status.stickX, virtualStatus.stickX, -127, 127));
  status.stickY = static_cast<s8>(dominant_axis_value(status.stickY, virtualStatus.stickY, -127, 127));
  status.substickX = static_cast<s8>(dominant_axis_value(status.substickX, virtualStatus.substickX, -127, 127));
  status.substickY = static_cast<s8>(dominant_axis_value(status.substickY, virtualStatus.substickY, -127, 127));
  status.triggerLeft = std::max(status.triggerLeft, virtualStatus.triggerLeft);
  status.triggerRight = std::max(status.triggerRight, virtualStatus.triggerRight);
  status.analogA = std::max(status.analogA, virtualStatus.analogA);
  status.analogB = std::max(status.analogB, virtualStatus.analogB);
}

u32 PADRead(PADStatus* status) {
  if (!g_initialized) {
    Log.fatal("PADRead called before PADInit()!");
  }

  int numKeys = 0;
  const bool* kbState = SDL_GetKeyboardState(&numKeys);
  const bool captureHeldInput = g_suppressHeldOnRead && !g_blockPAD;
  g_suppressHeldOnRead = false;

  uint32_t rumbleSupport = 0;
  for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
    memset(&status[i], 0, sizeof(PADStatus));
    // Support device rumble on port 0 regardless of whether a controller is connected.
    if (device_rumble_available_for_port(i)) {
      rumbleSupport |= PAD_CHAN0_BIT;
    }
    const bool overridden = g_portOverrideSet[i];
    g_portOverrideSet[i] = false;
    auto controller = aurora::input::get_controller_for_player(i);
    if (controller == nullptr && !g_keyboardBindings[i].m_mappingsSet && !g_virtualPadActive[i] &&
        !(overridden && g_portOverride[i].err == PAD_ERR_NONE)) {
      status[i].err = PAD_ERR_NO_CONTROLLER;
      g_suppressedButtons[i] = 0;
      g_suppressLeftTrigger[i] = false;
      g_suppressRightTrigger[i] = false;
      continue;
    }

    status[i].err = PAD_ERR_NONE;
    if (overridden) {
      status[i] = g_portOverride[i];
      status[i].err = PAD_ERR_NONE;
    } else if (g_keyboardBindings[i].m_mappingsSet) {
      const auto keyPressed = [&kbState, numKeys](const s32 scancode) {
        if (scancode > PAD_KEY_INVALID) {
          return scancode < numKeys && kbState[scancode];
        }
        return is_mouse_scancode(scancode) && is_mouse_button_pressed(scancode);
      };
      // An axis held through both of its slots still counts once, so two keys
      // for one direction don't cancel the opposite direction's key.
      std::array<bool, PAD_AXIS_COUNT> axisPressed{};
      for (const auto& slot : g_keyboardBindings[i].m_slots) {
        for (const auto& mapping : slot.m_buttonMapping) {
          if (keyPressed(mapping.scancode)) {
            status[i].button |= mapping.padButton;
          }
        }
        for (const auto& binding : slot.m_axisMapping) {
          if (binding.padAxis < PAD_AXIS_COUNT && keyPressed(binding.scancode)) {
            axisPressed[binding.padAxis] = true;
          }
        }
      }

      int lx = 0, ly = 0, rx = 0, ry = 0, tl = 0, tr = 0;
      for (u32 padAxis = 0; padAxis < PAD_AXIS_COUNT; ++padAxis) {
        if (!axisPressed[padAxis]) {
          continue;
        }
        switch (padAxis) {
        case PAD_AXIS_LEFT_X_POS:
          lx += 127;
          break;
        case PAD_AXIS_LEFT_X_NEG:
          lx -= 127;
          break;
        case PAD_AXIS_LEFT_Y_POS:
          ly += 127;
          break;
        case PAD_AXIS_LEFT_Y_NEG:
          ly -= 127;
          break;
        case PAD_AXIS_RIGHT_X_POS:
          rx += 127;
          break;
        case PAD_AXIS_RIGHT_X_NEG:
          rx -= 127;
          break;
        case PAD_AXIS_RIGHT_Y_POS:
          ry += 127;
          break;
        case PAD_AXIS_RIGHT_Y_NEG:
          ry -= 127;
          break;
        case PAD_AXIS_TRIGGER_L:
          tl += 255;
          break;
        case PAD_AXIS_TRIGGER_R:
          tr += 255;
          break;
        default:
          break;
        }
      }
      status[i].stickX = static_cast<s8>(std::clamp(static_cast<int>(status[i].stickX) + lx, -127, 127));
      status[i].stickY = static_cast<s8>(std::clamp(static_cast<int>(status[i].stickY) + ly, -127, 127));
      status[i].substickX = static_cast<s8>(std::clamp(static_cast<int>(status[i].substickX) + rx, -127, 127));
      status[i].substickY = static_cast<s8>(std::clamp(static_cast<int>(status[i].substickY) + ry, -127, 127));
      status[i].triggerLeft = static_cast<u8>(std::min(static_cast<int>(status[i].triggerLeft) + tl, 255));
      status[i].triggerRight = static_cast<u8>(std::min(static_cast<int>(status[i].triggerRight) + tr, 255));
    }

    // An override already carries this port's controller (the port reads the gamepad itself), so the
    // mapped read below would overwrite its sticks and triggers; only the LED update still applies.
    if (controller && overridden && controller->m_hasRgbLed && controller->m_isColorDirty) {
      SDL_SetGamepadLED(controller->m_controller, controller->m_ledRed, controller->m_ledGreen, controller->m_ledBlue);
      controller->m_isColorDirty = false;
    }

    if (controller && !overridden) {
      EnsureMappingLoaded(controller);
      // TODO: Add serializable mappings for these (probably not necessary)?
      static constexpr std::array<std::pair<SDL_GamepadButton, PADExtButton>, PAD_EXT_BUTTON_COUNT> kExtButtonMappings{{
          {SDL_GAMEPAD_BUTTON_BACK, PAD_BUTTON_BACK},
          {SDL_GAMEPAD_BUTTON_GUIDE, PAD_BUTTON_GUIDE},
          {SDL_GAMEPAD_BUTTON_MISC1, PAD_BUTTON_MISC1},
          {SDL_GAMEPAD_BUTTON_MISC2, PAD_BUTTON_MISC2},
          {SDL_GAMEPAD_BUTTON_MISC3, PAD_BUTTON_MISC3},
          {SDL_GAMEPAD_BUTTON_MISC4, PAD_BUTTON_MISC4},
          {SDL_GAMEPAD_BUTTON_MISC5, PAD_BUTTON_MISC5},
          {SDL_GAMEPAD_BUTTON_MISC6, PAD_BUTTON_MISC6},
          {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, PAD_BUTTON_RIGHT_PADDLE1},
          {SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, PAD_BUTTON_LEFT_PADDLE1},
          {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, PAD_BUTTON_RIGHT_PADDLE2},
          {SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, PAD_BUTTON_LEFT_PADDLE2},
          {SDL_GAMEPAD_BUTTON_RIGHT_STICK, PAD_BUTTON_RIGHT_STICK},
          {SDL_GAMEPAD_BUTTON_LEFT_STICK, PAD_BUTTON_LEFT_STICK},
          {SDL_GAMEPAD_BUTTON_TOUCHPAD, PAD_BUTTON_TOUCHPAD},
      }};

      for (const auto& [native, button] : kExtButtonMappings) {
        if (SDL_GetGamepadButton(controller->m_controller, native)) {
          status[i].extButton |= button;
        }
      }

      if (!overridden) {
        const u16 buttonsBefore = status[i].button;
        bool leftTriggerSet = false;
        bool rightTriggerSet = false;
        std::ranges::for_each(controller->m_buttonMapping, [&controller, &i, &status, &leftTriggerSet,
                                                            &rightTriggerSet](const auto& mapping) {
          if (native_button_held(controller, mapping.nativeButton)) {
            status[i].button |= mapping.padButton;
          }

          if (mapping.padButton == PAD_TRIGGER_L && mapping.nativeButton != PAD_NATIVE_BUTTON_INVALID) {
            leftTriggerSet = true;
          }
          if (mapping.padButton == PAD_TRIGGER_R && mapping.nativeButton != PAD_NATIVE_BUTTON_INVALID) {
            rightTriggerSet = true;
          }
        });

        if (SDL_Joystick* rawJoystick = gamecube_raw_joystick(controller)) {
          read_gamecube_raw_axes(controller, rawJoystick, status[i], buttonsBefore);
        } else {
          const auto xlPos = _get_axis_value(controller, PAD_AXIS_LEFT_X_POS);
          const auto xlNeg = _get_axis_value(controller, PAD_AXIS_LEFT_X_NEG);
          const auto ylPos = _get_axis_value(controller, PAD_AXIS_LEFT_Y_POS);
          const auto ylNeg = _get_axis_value(controller, PAD_AXIS_LEFT_Y_NEG);

          auto xl = static_cast<Sint16>((xlPos + -xlNeg) / 2);
          // SDL's gamepad y-axis is inverted from GC's
          auto yl = static_cast<Sint16>((-ylPos + ylNeg) / 2);
          if (controller->m_deadZones.useDeadzones) {
            if (std::abs(xl) > controller->m_deadZones.stickDeadZone) {
              xl /= 256;
            } else {
              xl = 0;
            }
            if (std::abs(yl) > controller->m_deadZones.stickDeadZone) {
              yl = static_cast<Sint16>(-(yl + 1u) / 256u);
            } else {
              yl = 0;
            }
          } else {
            xl /= 256;
            yl = static_cast<Sint16>(-(yl + 1u) / 256u);
          }

          // Merge with the keyboard values set above instead of overwriting them: a
          // connected pad (including a centred Android touch pad) would otherwise
          // disable keyboard movement entirely.
          status[i].stickX = static_cast<int8_t>(dominant_axis_value(status[i].stickX, xl, -127, 127));
          status[i].stickY = static_cast<int8_t>(dominant_axis_value(status[i].stickY, yl, -127, 127));

          const auto xrPos = _get_axis_value(controller, PAD_AXIS_RIGHT_X_POS);
          const auto xrNeg = _get_axis_value(controller, PAD_AXIS_RIGHT_X_NEG);
          const auto yrPos = _get_axis_value(controller, PAD_AXIS_RIGHT_Y_POS);
          const auto yrNeg = _get_axis_value(controller, PAD_AXIS_RIGHT_Y_NEG);

          auto xr = static_cast<Sint16>((xrPos + -xrNeg) / 2);
          // SDL's gamepad y-axis is inverted from GC's
          auto yr = static_cast<Sint16>((-yrPos + yrNeg) / 2);
          if (controller->m_deadZones.useDeadzones) {
            if (std::abs(xr) > controller->m_deadZones.substickDeadZone) {
              xr /= 256;
            } else {
              xr = 0;
            }

            if (std::abs(yr) > controller->m_deadZones.substickDeadZone) {
              yr = static_cast<Sint16>(-(yr + 1u) / 256u);
            } else {
              yr = 0;
            }
          } else {
            xr /= 256;
            yr = static_cast<Sint16>(-(yr + 1u) / 256u);
          }

          status[i].substickX = static_cast<int8_t>(dominant_axis_value(status[i].substickX, xr, -127, 127));
          status[i].substickY = static_cast<int8_t>(dominant_axis_value(status[i].substickY, yr, -127, 127));

          Sint16 tl = trigger_axis_taken(controller, PAD_AXIS_TRIGGER_L)
                          ? 0
                          : std::max(static_cast<Sint16>(0), _get_axis_value(controller, PAD_AXIS_TRIGGER_L));
          Sint16 tr = trigger_axis_taken(controller, PAD_AXIS_TRIGGER_R)
                          ? 0
                          : std::max(static_cast<Sint16>(0), _get_axis_value(controller, PAD_AXIS_TRIGGER_R));

          if (controller->m_deadZones.emulateTriggers) {
            if (!leftTriggerSet && tl > controller->m_deadZones.leftTriggerActivationZone) {
              status[i].button |= PAD_TRIGGER_L;
            }
            if (!rightTriggerSet && tr > controller->m_deadZones.rightTriggerActivationZone) {
              status[i].button |= PAD_TRIGGER_R;
            }
          }
          tl /= 128;
          tr /= 128;

          status[i].triggerLeft = std::max(status[i].triggerLeft, static_cast<u8>(tl));
          status[i].triggerRight = std::max(status[i].triggerRight, static_cast<u8>(tr));

          // If the digital button is activated, set the analog value to max.
          if (status[i].button & PAD_TRIGGER_L) {
            status[i].triggerLeft = 180;
          }
          if (status[i].button & PAD_TRIGGER_R) {
            status[i].triggerRight = 180;
          }
        }
      }

      if (controller->m_hasRumble) {
        rumbleSupport |= PAD_CHAN0_BIT >> i;
      }

      // Update the LED colors when they exist and the controller is read (which should happen once per frame in most
      // games)
      if (controller->m_hasRgbLed && controller->m_isColorDirty) {
        SDL_SetGamepadLED(controller->m_controller, controller->m_ledRed, controller->m_ledGreen,
                          controller->m_ledBlue);
        controller->m_isColorDirty = false;
      }
    }

    if (g_blockPAD) {
      neutralize_status(status[i]);
    } else {
      apply_unblock_suppression(status[i], i, captureHeldInput);
      if (g_virtualPadActive[i]) {
        merge_virtual_status(status[i], g_virtualPadStatus[i]);
      }
    }
  }
  return rumbleSupport;
}

void PADSetVirtualStatus(const u32 port, const PADStatus* virtualStatus) {
  if (port >= PAD_CHANMAX || virtualStatus == nullptr) {
    return;
  }

  g_virtualPadStatus[port] = *virtualStatus;
  g_virtualPadStatus[port].err = PAD_ERR_NONE;
  g_virtualPadActive[port] = true;
}

void PADSetPortOverride(const u32 port, const PADStatus* status) {
  if (port >= PAD_CHANMAX) {
    return;
  }
  g_portOverrideSet[port] = status != nullptr;
  if (status != nullptr) {
    g_portOverride[port] = *status;
  }
}

BOOL PADGetRawState(const u32 port, PADRawState* state) {
  if (state == nullptr) {
    return FALSE;
  }
  *state = {};
  auto* controller = port < PAD_CHANMAX ? aurora::input::get_controller_for_player(port) : nullptr;
  if (controller == nullptr || controller->m_controller == nullptr) {
    return FALSE;
  }
  EnsureMappingLoaded(controller);
  static_assert(SDL_GAMEPAD_BUTTON_COUNT <= PAD_RAW_BUTTON_TRIGGER_LEFT);
  for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
    if (SDL_GetGamepadButton(controller->m_controller, static_cast<SDL_GamepadButton>(b))) {
      state->buttons |= 1u << b;
    }
  }
  for (int a = 0; a < PAD_RAW_AXIS_COUNT; ++a) {
    state->axes[a] = SDL_GetGamepadAxis(controller->m_controller, static_cast<SDL_GamepadAxis>(a));
  }
  const Sint16 tl = state->axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER];
  const Sint16 tr = state->axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER];
  // native_button_held's half pull, and PADRead's emulated trigger click.
  if (tl >= 16384) {
    state->buttons |= 1u << PAD_RAW_BUTTON_TRIGGER_LEFT;
  }
  if (tr >= 16384) {
    state->buttons |= 1u << PAD_RAW_BUTTON_TRIGGER_RIGHT;
  }
  if (tl > controller->m_deadZones.leftTriggerActivationZone) {
    state->buttons |= 1u << PAD_RAW_BUTTON_TRIGGER_LEFT_CLICK;
  }
  if (tr > controller->m_deadZones.rightTriggerActivationZone) {
    state->buttons |= 1u << PAD_RAW_BUTTON_TRIGGER_RIGHT_CLICK;
  }
  return TRUE;
}

void PADClearVirtualStatus(const u32 port) {
  if (port >= PAD_CHANMAX) {
    return;
  }

  g_virtualPadStatus[port] = {};
  g_virtualPadActive[port] = false;
}

void PADClearAllVirtualStatus() {
  g_virtualPadStatus.fill({});
  g_virtualPadActive.fill(false);
}

void PADControlMotor(const u32 chan, const u32 cmd) {
  const auto controller = aurora::input::get_controller_for_player(chan);
  if (should_use_device_rumble(chan, controller)) {
    u16 low = 0;
    u16 high = 0;
    if (controller != nullptr) {
      EnsureMappingLoaded(controller);
      low = controller->m_rumbleIntensityLow;
      high = controller->m_rumbleIntensityHigh;
    } else {
      aurora::input::get_device_rumble_intensity(&low, &high);
    }
    if (cmd == PAD_MOTOR_STOP || cmd == PAD_MOTOR_STOP_HARD) {
      aurora::device::rumble(0, 0, 0);
    } else if (cmd == PAD_MOTOR_RUMBLE) {
      aurora::device::rumble(low, high, 0);
    }
    return;
  }

  if (controller == nullptr) {
    return;
  }

  const auto instance = aurora::input::get_instance_for_player(chan);
  if (controller->m_isGameCube) {
    if (cmd == PAD_MOTOR_STOP) {
      aurora::input::controller_rumble(instance, 0, 1, 0);
    } else if (cmd == PAD_MOTOR_RUMBLE) {
      aurora::input::controller_rumble(instance, 1, 1, 0);
    } else if (cmd == PAD_MOTOR_STOP_HARD) {
      aurora::input::controller_rumble(instance, 0, 0, 0);
    }
  } else {
    if (cmd == PAD_MOTOR_STOP) {
      aurora::input::controller_rumble(instance, 0, 0, 1);
    } else if (cmd == PAD_MOTOR_RUMBLE) {
      aurora::input::controller_rumble(instance, controller->m_rumbleIntensityLow, controller->m_rumbleIntensityHigh,
                                       0);
    } else if (cmd == PAD_MOTOR_STOP_HARD) {
      aurora::input::controller_rumble(instance, 0, 0, 0);
    }
  }
}

void PADControlAllMotors(const u32* cmdArr) {
  for (u32 i = 0; i < PAD_CHANMAX; ++i) {
    PADControlMotor(i, cmdArr[i]);
  }
}

void ClampTrigger(u8* trigger, const u8 min, const u8 max) {
  if (*trigger <= min) {
    *trigger = 0;
  } else {
    if (*trigger > max) {
      *trigger = max;
    }
    *trigger -= min;
  }
}

void ClampCircle(s8* px, s8* py, const s8 radius, const s8 min) {
  int x = *px; // NOLINT(*-str34-c)
  int y = *py; // NOLINT(*-str34-c)

  if (-min < x && x < min) {
    x = 0;
  } else if (0 < x) {
    x -= min;
  } else {
    x += min;
  }

  if (-min < y && y < min) {
    y = 0;
  } else if (0 < y) {
    y -= min;
  } else {
    y += min;
  }

  if (const int squared = x * x + y * y; radius * radius < squared) {
    const auto length = static_cast<int32_t>(std::sqrt(squared));
    x = x * radius / length;
    y = y * radius / length;
  }

  *px = static_cast<int8_t>(x);
  *py = static_cast<int8_t>(y);
}

void ClampStick(s8* px, s8* py, const s8 max, const s8 xy, const s8 min) {
  int32_t x = *px; // NOLINT(*-str34-c)
  int32_t y = *py; // NOLINT(*-str34-c)

  int32_t signX = 0;
  if (0 <= x) {
    signX = 1;
  } else {
    signX = -1;
    x = -x;
  }

  int8_t signY = 0;
  if (0 <= y) {
    signY = 1;
  } else {
    signY = -1;
    y = -y;
  }

  if (x <= min) {
    x = 0;
  } else {
    x -= min;
  }
  if (y <= min) {
    y = 0;
  } else {
    y -= min;
  }

  if (x == 0 && y == 0) {
    *px = *py = 0;
    return;
  }

  x = x * max / (INT8_MAX - min);
  y = y * max / (INT8_MAX - min);

  if (xy * y <= xy * x) {
    if (const int32_t d = xy * x + (max - xy) * y; xy * max < d) {
      x = xy * max * x / d;
      y = xy * max * y / d;
    }
  } else {
    if (const int32_t d = xy * y + (max - xy) * x; xy * max < d) {
      x = xy * max * x / d;
      y = xy * max * y / d;
    }
  }

  *px = static_cast<s8>(signX * x);
  *py = static_cast<s8>(signY * y);
}

void PADClamp(PADStatus* status) {
  for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
    if (status[i].err != PAD_ERR_NONE) {
      continue;
    }

    ClampStick(&status[i].stickX, &status[i].stickY, ClampRegion.maxStick, ClampRegion.xyStick, ClampRegion.minStick);
    ClampStick(&status[i].substickX, &status[i].substickY, ClampRegion.maxSubstick, ClampRegion.xySubstick,
               ClampRegion.minSubstick);
    ClampTrigger(&status[i].triggerLeft, ClampRegion.minTrigger, ClampRegion.maxTrigger);
    ClampTrigger(&status[i].triggerRight, ClampRegion.minTrigger, ClampRegion.maxTrigger);
  }
}

void PADClampCircle(PADStatus* status) {
  for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
    if (status[i].err != PAD_ERR_NONE) {
      continue;
    }

    ClampCircle(&status[i].stickX, &status[i].stickY, ClampRegion.radStick, ClampRegion.minStick);
    ClampCircle(&status[i].substickX, &status[i].substickY, ClampRegion.radSubstick, ClampRegion.minSubstick);
    ClampTrigger(&status[i].triggerLeft, ClampRegion.minTrigger, ClampRegion.maxTrigger);
    ClampTrigger(&status[i].triggerRight, ClampRegion.minTrigger, ClampRegion.maxTrigger);
  }
}

void PADGetVidPid(const u32 port, u32* vid, u32* pid) {
  *vid = 0;
  *pid = 0;
  const auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }

  *vid = controller->m_vid;
  *pid = controller->m_pid;
}

const char* PADGetName(const u32 port) {
  const auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return nullptr;
  }

  return SDL_GetGamepadName(controller->m_controller);
}

void PADSetButtonMapping(const u32 port, const PADButtonMapping mapping) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr || is_virtual_controller(controller)) {
    return;
  }

  const auto iter = std::ranges::find_if(controller->m_buttonMapping,
                                         [mapping](const auto& pair) { return mapping.padButton == pair.padButton; });
  if (iter == controller->m_buttonMapping.end()) {
    return;
  }

  *iter = mapping;
}

void PADSetAllButtonMappings(const u32 port, const PADButtonMapping buttons[PAD_BUTTON_COUNT]) {
  for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
    PADSetButtonMapping(port, buttons[i]);
  }
}

PADButtonMapping* PADGetButtonMappings(const u32 port, u32* buttonCount) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    *buttonCount = 0;
    return nullptr;
  }

  EnsureMappingLoaded(controller);

  *buttonCount = PAD_BUTTON_COUNT;
  return controller->m_buttonMapping.data();
}

void PADSetAxisMapping(const u32 port, const PADAxisMapping mapping) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr || is_virtual_controller(controller)) {
    return;
  }

  const auto iter = std::ranges::find_if(controller->m_axisMapping,
                                         [mapping](const auto& pair) { return mapping.padAxis == pair.padAxis; });
  if (iter == controller->m_axisMapping.end()) {
    return;
  }

  *iter = mapping;
}

void PADSetAllAxisMappings(const u32 port, const PADAxisMapping axes[PAD_AXIS_COUNT]) {
  for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
    PADSetAxisMapping(port, axes[i]);
  }
}

PADAxisMapping* PADGetAxisMappings(const u32 port, u32* axisCount) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    *axisCount = 0;
    return nullptr;
  }

  EnsureMappingLoaded(controller);
  *axisCount = PAD_AXIS_COUNT;
  return controller->m_axisMapping.data();
}

BOOL PADSetKeyButtonBindingSlot(const u32 port, const u32 slot, const PADKeyButtonBinding binding) {
  if (port >= PAD_MAX_CONTROLLERS || slot >= PAD_KEY_SLOT_COUNT) {
    return FALSE;
  }

  for (auto& [scancode, padButton] : g_keyboardBindings[port].m_slots[slot].m_buttonMapping) {
    if (padButton == binding.padButton) {
      scancode = binding.scancode;
      return TRUE;
    }
  }

  return FALSE;
}

BOOL PADSetKeyButtonBinding(const u32 port, const PADKeyButtonBinding binding) {
  return PADSetKeyButtonBindingSlot(port, 0, binding);
}

BOOL PADSetKeyButtonBindings(const u32 port, PADKeyButtonBinding bindings[PAD_BUTTON_COUNT]) {
  for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
    if (!PADSetKeyButtonBinding(port, bindings[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

PADKeyButtonBinding* PADGetKeyButtonBindingsSlot(const u32 port, const u32 slot, u32* buttonCount) {
  if (port >= PAD_MAX_CONTROLLERS || slot >= PAD_KEY_SLOT_COUNT || !g_keyboardBindings[port].m_mappingsSet) {
    return nullptr;
  }
  *buttonCount = PAD_BUTTON_COUNT;
  return g_keyboardBindings[port].m_slots[slot].m_buttonMapping.data();
}

PADKeyButtonBinding* PADGetKeyButtonBindings(const u32 port, u32* buttonCount) {
  return PADGetKeyButtonBindingsSlot(port, 0, buttonCount);
}

BOOL PADSetKeyAxisBindingSlot(const u32 port, const u32 slot, const PADKeyAxisBinding binding) {
  if (port >= PAD_MAX_CONTROLLERS || slot >= PAD_KEY_SLOT_COUNT) {
    return FALSE;
  }

  for (auto& b : g_keyboardBindings[port].m_slots[slot].m_axisMapping) {
    if (b.padAxis == binding.padAxis) {
      b.scancode = binding.scancode;
      return TRUE;
    }
  }

  return FALSE;
}

BOOL PADSetKeyAxisBinding(const u32 port, const PADKeyAxisBinding binding) {
  return PADSetKeyAxisBindingSlot(port, 0, binding);
}
BOOL PADSetKeyAxisBindings(const u32 port, PADKeyAxisBinding bindings[PAD_BUTTON_COUNT]) {
  for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
    if (!PADSetKeyAxisBinding(port, bindings[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

PADKeyAxisBinding* PADGetKeyAxisBindingsSlot(const u32 port, const u32 slot, u32* axisCount) {
  if (port >= PAD_MAX_CONTROLLERS || slot >= PAD_KEY_SLOT_COUNT || !g_keyboardBindings[port].m_mappingsSet) {
    return nullptr;
  }
  *axisCount = PAD_AXIS_COUNT;
  return g_keyboardBindings[port].m_slots[slot].m_axisMapping.data();
}

PADKeyAxisBinding* PADGetKeyAxisBindings(const u32 port, u32* axisCount) {
  return PADGetKeyAxisBindingsSlot(port, 0, axisCount);
}

void PADSetKeyboardActive(const u32 port, const BOOL active) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return;
  }
  g_keyboardBindings[port].m_mappingsSet = active != FALSE;
}

void PADClearKeyBindings(const u32 port) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return;
  }
  g_keyboardBindings[port].m_slots.fill(PADKeyboardSlot{});
  g_keyboardBindings[port].m_mappingsSet = false;
}

constexpr uint32_t k_keyboardMagic = SBIG('KBND');
// 4: PAD_KEY_SLOT_COUNT keys per input.
constexpr int32_t k_keyboardVersion = 4;
constexpr int32_t k_keyboardMinVersion = 3;

static void load_keyboard_bindings() {
  if (aurora::g_config.userPath == nullptr) {
    return;
  }
  const auto filePath = aurora::io::fs_path_from_string(aurora::g_config.userPath) / "keyboard_bindings.dat";
  auto file = aurora::io::open_file(filePath, "rb");
  if (!file) {
    return;
  }

  uint32_t magic = 0;
  if (!SDL_ReadU32LE(file.get(), &magic) || magic != k_keyboardMagic) {
    Log.warn("keyboard_bindings.dat: invalid magic");
    return;
  }

  uint32_t version = 0;
  if (!SDL_ReadU32LE(file.get(), &version) || version < static_cast<uint32_t>(k_keyboardMinVersion) ||
      version > static_cast<uint32_t>(k_keyboardVersion)) {
    Log.warn("keyboard_bindings.dat: unsupported version {} (expected {} to {})", version, k_keyboardMinVersion,
             k_keyboardVersion);
    return;
  }
  // Version 3 files hold one key per input: slot 0.
  const uint32_t fileSlots = version >= 4 ? PAD_KEY_SLOT_COUNT : 1;

  Sint64 dataStart = 0;
  if (!seek_aligned(file.get(), dataStart)) {
    Log.warn("keyboard_bindings.dat: unable to seek to bindings");
    return;
  }

  auto bindings = g_keyboardBindings;
  for (uint32_t port = 0; port < bindings.size(); ++port) {
    auto& [slots, mappingsSet] = bindings[port];
    uint8_t mappingsSetValue = 0;
    bool ok = SDL_ReadU8(file.get(), &mappingsSetValue);
    for (uint32_t s = 0; ok && s < fileSlots; ++s) {
      ok = aurora::io::read_exact(file.get(), slots[s].m_buttonMapping.data(), sizeof(slots[s].m_buttonMapping)) &&
           aurora::io::read_exact(file.get(), slots[s].m_axisMapping.data(), sizeof(slots[s].m_axisMapping));
    }
    if (!ok) {
      Log.warn("keyboard_bindings.dat: truncated bindings for port {}", port);
      return;
    }
    mappingsSet = mappingsSetValue != 0;

    bool anyBound = false;
    for (uint32_t s = 0; s < PAD_KEY_SLOT_COUNT; ++s) {
      auto& [buttonMapping, axisMapping] = slots[s];
      bool kbButtonCorrupt = false;
      for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
        if (buttonMapping[i].padButton != g_defaultKeys[i].padButton) {
          kbButtonCorrupt = true;
          break;
        }
      }
      if (kbButtonCorrupt) {
        Log.warn("keyboard_bindings.dat port={} slot={}: corrupt button identifiers, resetting to defaults", port, s);
        buttonMapping = g_defaultKeys;
      }

      bool kbAxisCorrupt = false;
      for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
        if (axisMapping[i].padAxis != g_defaultKeyAxis[i].padAxis) {
          kbAxisCorrupt = true;
          break;
        }
      }
      if (kbAxisCorrupt) {
        Log.warn("keyboard_bindings.dat port={} slot={}: corrupt axis identifiers, resetting to defaults", port, s);
        axisMapping = g_defaultKeyAxis;
      }

      anyBound = anyBound ||
                 std::ranges::any_of(buttonMapping,
                                     [](const PADKeyButtonBinding& b) { return b.scancode != PAD_KEY_INVALID; }) ||
                 std::ranges::any_of(axisMapping,
                                     [](const PADKeyAxisBinding& b) { return b.scancode != PAD_KEY_INVALID; });
    }
    if (!anyBound) {
      mappingsSet = false;
    }
  }
  g_keyboardBindings = std::move(bindings);
}

static void save_keyboard_bindings() {
  if (aurora::io::config_persistence_disabled()) {
    return;
  }
  if (aurora::g_config.userPath == nullptr) {
    return;
  }
  const auto filePath = aurora::io::fs_path_from_string(aurora::g_config.userPath) / "keyboard_bindings.dat";
  const auto pathString = aurora::io::fs_path_to_string(filePath);
  auto file = aurora::io::open_atomic_file(filePath);
  if (!file) {
    Log.warn("save_keyboard_bindings: failed to open {} for writing: {}", pathString, SDL_GetError());
    return;
  }

  bool ok = SDL_WriteU32LE(file.get(), k_keyboardMagic) && SDL_WriteS32LE(file.get(), k_keyboardVersion);
  Sint64 dataStart = 0;
  ok = ok && seek_aligned(file.get(), dataStart);

  for (const auto& [slots, mappingsSet] : g_keyboardBindings) {
    ok = ok && SDL_WriteU8(file.get(), mappingsSet);
    for (const auto& [buttonMapping, axisMapping] : slots) {
      ok = ok && aurora::io::write_exact(file.get(), buttonMapping.data(), sizeof(buttonMapping)) &&
           aurora::io::write_exact(file.get(), axisMapping.data(), sizeof(axisMapping));
    }
  }
  if (!ok || !file.commit()) {
    Log.warn("save_keyboard_bindings: failed to write {}: {}", pathString, SDL_GetError());
  }
}

void PADSerializeMappings() {
  if (aurora::io::config_persistence_disabled()) {
    return;
  }
  if (aurora::g_config.userPath == nullptr) {
    return;
  }
  const auto basePath = aurora::io::fs_path_from_string(aurora::g_config.userPath);

  for (auto& controller : aurora::input::g_GameControllers | std::views::values) {
    EnsureMappingLoaded(&controller);
    if (is_virtual_controller(&controller)) {
      continue;
    }
    const auto filePath =
        basePath / fmt::format("{}_{:04X}_{:04X}.controller", aurora::input::controller_name(controller.m_index),
                               controller.m_vid, controller.m_pid);
    const auto filePathStr = aurora::io::fs_path_to_string(filePath);

    auto file = aurora::io::open_atomic_file(filePath, aurora::io::AtomicFileMode::Preserve);
    if (!file) {
      Log.warn("Unable to open controller bindings! Path: \"{}\": {}", filePathStr, SDL_GetError());
      continue;
    }

    // write header
    constexpr uint32_t magic = SBIG('CTRL');
    bool ok = SDL_SeekIO(file.get(), 0, SDL_IO_SEEK_SET) == 0 && SDL_WriteU32LE(file.get(), magic) &&
              SDL_WriteU32LE(file.get(), k_mappingsFileVersion) &&
              SDL_WriteU8(file.get(), controller.m_isGameCube);

    // start writing data at next 32-byte aligned offset
    Sint64 dataStart = 0;
    ok = ok && seek_aligned(file.get(), dataStart);
    if (controller.m_isGameCube) {
      // GameCube adapters expose 4 input devices with the same vid/pid, we store all 4 in the same file
      const auto port = aurora::input::player_index(controller.m_index);
      if (port < 0 || port >= PAD_CHANMAX) {
        Log.warn("Unable to write controller bindings for invalid port {}! Path: \"{}\"", port, filePathStr);
        continue;
      }
      constexpr int64_t dzSecLen = sizeof(PADDeadZones);
      constexpr int64_t btnSecLen = sizeof(PADButtonMapping) * PAD_BUTTON_COUNT;
      constexpr int64_t axisSecLen = sizeof(PADAxisMapping) * PAD_AXIS_COUNT;
      // skip to offset in file for this particular port
      const Sint64 portOffset = dataStart + (dzSecLen + btnSecLen + axisSecLen) * port;
      ok = ok && SDL_SeekIO(file.get(), portOffset, SDL_IO_SEEK_SET) == portOffset;
    }
    ok = ok && aurora::io::write_exact(file.get(), &controller.m_deadZones, sizeof(controller.m_deadZones)) &&
         aurora::io::write_exact(file.get(), controller.m_buttonMapping.data(), sizeof(controller.m_buttonMapping)) &&
         aurora::io::write_exact(file.get(), controller.m_axisMapping.data(), sizeof(controller.m_axisMapping));

    if (!controller.m_isGameCube) {
      ok = ok && SDL_WriteU16LE(file.get(), controller.m_rumbleIntensityLow) &&
           SDL_WriteU16LE(file.get(), controller.m_rumbleIntensityHigh) &&
           SDL_WriteU8(file.get(), controller.m_forceDeviceRumble);
    }
    if (!ok || !file.commit()) {
      Log.warn("Unable to write controller bindings! Path: \"{}\": {}", filePathStr, SDL_GetError());
    }
  }

  save_keyboard_bindings();
}

PADDeadZones* PADGetDeadZones(const u32 port) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return nullptr;
  }
  return &controller->m_deadZones;
}

static constexpr std::array<std::pair<PADButton, std::string_view>, PAD_BUTTON_COUNT> skButtonNames = {{
    {PAD_BUTTON_LEFT, "Left"sv},
    {PAD_BUTTON_RIGHT, "Right"sv},
    {PAD_BUTTON_DOWN, "Down"sv},
    {PAD_BUTTON_UP, "Up"sv},
    {PAD_TRIGGER_Z, "Z"sv},
    {PAD_TRIGGER_R, "R"sv},
    {PAD_TRIGGER_L, "L"sv},
    {PAD_BUTTON_A, "A"sv},
    {PAD_BUTTON_B, "B"sv},
    {PAD_BUTTON_X, "X"sv},
    {PAD_BUTTON_Y, "Y"sv},
    {PAD_BUTTON_START, "Start"sv},
}};

static constexpr std::array<std::pair<PADButton, std::string_view>, PAD_AXIS_COUNT> skAxisNames = {{
    {PAD_AXIS_LEFT_X_POS, "Left X+"sv},
    {PAD_AXIS_LEFT_X_NEG, "Left X-"sv},
    {PAD_AXIS_LEFT_Y_POS, "Left Y+"sv},
    {PAD_AXIS_LEFT_Y_NEG, "Left Y-"sv},
    {PAD_AXIS_RIGHT_X_POS, "Right X+"sv},
    {PAD_AXIS_RIGHT_X_NEG, "Right X-"sv},
    {PAD_AXIS_RIGHT_Y_POS, "Right Y+"sv},
    {PAD_AXIS_RIGHT_Y_NEG, "Right Y-"sv},
    {PAD_AXIS_TRIGGER_L, "Trigger L"sv},
    {PAD_AXIS_TRIGGER_R, "Trigger R"sv},
}};

static constexpr std::array<std::pair<PADButton, std::string_view>, PAD_AXIS_COUNT> skAxisDirLabels = {{
    {PAD_AXIS_LEFT_X_POS, "Right"sv},
    {PAD_AXIS_LEFT_X_NEG, "Left"sv},
    {PAD_AXIS_LEFT_Y_POS, "Up"sv},
    {PAD_AXIS_LEFT_Y_NEG, "Down"sv},
    {PAD_AXIS_RIGHT_X_POS, "Right"sv},
    {PAD_AXIS_RIGHT_X_NEG, "Left"sv},
    {PAD_AXIS_RIGHT_Y_POS, "Up"sv},
    {PAD_AXIS_RIGHT_Y_NEG, "Down"sv},
    {PAD_AXIS_TRIGGER_L, "N/A"sv},
    {PAD_AXIS_TRIGGER_R, "N/A"sv},
}};

const char* PADGetButtonName(const PADButton button) {

  if (const auto iter =
          std::ranges::find_if(skButtonNames, [&button](const auto& pair) { return button == pair.first; });
      iter != skButtonNames.end()) {
    return iter->second.data();
  }

  return nullptr;
}

const char* PADGetNativeButtonName(u32 button) {
  if (button == PAD_NATIVE_BUTTON_TRIGGER_LEFT || button == PAD_NATIVE_BUTTON_TRIGGER_RIGHT) {
    return SDL_GetGamepadStringForAxis(button == PAD_NATIVE_BUTTON_TRIGGER_LEFT ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                                                                              : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
  }
  return SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(button));
}

const char* PADGetAxisName(const PADAxis axis) {
  if (const auto it = std::ranges::find_if(skAxisNames, [&axis](const auto& pair) { return axis == pair.first; });
      it != skAxisNames.end()) {
    return it->second.data();
  }

  return nullptr;
}

const char* PADGetAxisDirectionLabel(const PADAxis axis) {
  if (const auto it = std::ranges::find_if(skAxisDirLabels, [&axis](const auto& pair) { return axis == pair.first; });
      it != skAxisDirLabels.end()) {
    return it->second.data();
  }

  return nullptr;
}

const char* PADGetNativeAxisName(PADSignedNativeAxis axis) {
  return SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(axis.nativeAxis));
}

int32_t PADGetNativeButtonPressed(const u32 port) {
  const auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return -1;
  }

  for (int32_t i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
    if (SDL_GetGamepadButton(controller->m_controller, static_cast<SDL_GamepadButton>(i)) != 0u) {
      return i;
    }
  }
  return -1;
}

PADSignedNativeAxis PADGetNativeAxisPulled(const u32 port) {
  const auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return {-1, AXIS_SIGN_POSITIVE};
  }

  for (int32_t i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
    const auto axisVal = SDL_GetGamepadAxis(controller->m_controller, static_cast<SDL_GamepadAxis>(i));
    if (axisVal >= 16384) {
      return {i, AXIS_SIGN_POSITIVE};
    }

    if (axisVal <= -16384) {
      // SDL3 triggers rest at -32768, so skip their negative direction.
      if (i == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || i == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        continue;
      }
      return {i, AXIS_SIGN_NEGATIVE};
    }
  }
  return {-1, AXIS_SIGN_POSITIVE};
}

void PADRestoreDefaultMapping(const u32 port) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }
  // A virtual controller is already at its defaults.
  __PADSetDefaultMapping(controller);
  controller->m_axisMapping = g_defaultAxes;
}

BOOL PADIsDefaultMapping(const u32 port) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return FALSE;
  }
  EnsureMappingLoaded(controller);
  const auto buttons = controller->m_buttonMapping;
  __PADSetDefaultMapping(controller);
  const bool same =
      std::ranges::equal(controller->m_buttonMapping, buttons,
                         [](const PADButtonMapping& a, const PADButtonMapping& b) {
                           return a.nativeButton == b.nativeButton && a.padButton == b.padButton;
                         }) &&
      std::ranges::equal(controller->m_axisMapping, g_defaultAxes, [](const PADAxisMapping& a, const PADAxisMapping& b) {
        return a.nativeAxis.nativeAxis == b.nativeAxis.nativeAxis && a.nativeAxis.sign == b.nativeAxis.sign &&
               a.nativeButton == b.nativeButton && a.padAxis == b.padAxis;
      });
  controller->m_buttonMapping = buttons;
  return same ? TRUE : FALSE;
}

void PADBlockInput(const bool block) {
  if (g_blockPAD && !block) {
    g_suppressHeldOnRead = true;
  }
  g_blockPAD = block;
}

SDL_Gamepad* PADGetSDLGamepadForIndex(const u32 index) {
  const auto* ctrl = __PADGetControllerForIndex(index);
  if (ctrl == nullptr) {
    return nullptr;
  }

  return ctrl->m_controller;
}

void PADSetDefaultMapping(const PADDefaultMapping* mapping, const PADControllerType type) {
  if (g_initialized) {
    Log.fatal("PADSetDefaultMapping called after PADInit()!");
  }

  switch (type) {
  case PAD_TYPE_STANDARD:
    g_defaultButtonsStandard = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_XBOX360:
    g_defaultButtonsXBox360 = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_XBOXONE:
    g_defaultButtonsXBoxOne = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_PS3:
    g_defaultButtonsPS3 = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_PS4:
    g_defaultButtonsPS4 = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_PS5:
    g_defaultButtonsPS5 = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_SWITCH_PROCON:
    g_defaultButtonsProCon = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_JOYCON_LEFT:
    g_defaultButtonsJoyConLeft = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_JOYCON_RIGHT:
    g_defaultButtonsJoyConRight = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_JOYCON_PAIR:
    g_defaultButtonsJoyPair = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_GAMECUBE:
    g_defaultButtonsGamecube = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_NSO_GAMECUBE:
    g_defaultButtonsNSOGamecube = toStdArray(mapping->buttons);
    break;
  default:
    break;
  }
  g_defaultAxes = toStdArray(mapping->axes);
}

void PADGetDefaultMapping(PADDefaultMapping* mapping, const PADControllerType type) {
  const std::array<PADButtonMapping, PAD_BUTTON_COUNT>* buttons = &g_defaultButtonsStandard;
  switch (type) {
  case PAD_TYPE_XBOX360: buttons = &g_defaultButtonsXBox360; break;
  case PAD_TYPE_XBOXONE: buttons = &g_defaultButtonsXBoxOne; break;
  case PAD_TYPE_PS3: buttons = &g_defaultButtonsPS3; break;
  case PAD_TYPE_PS4: buttons = &g_defaultButtonsPS4; break;
  case PAD_TYPE_PS5: buttons = &g_defaultButtonsPS5; break;
  case PAD_TYPE_SWITCH_PROCON: buttons = &g_defaultButtonsProCon; break;
  case PAD_TYPE_JOYCON_LEFT: buttons = &g_defaultButtonsJoyConLeft; break;
  case PAD_TYPE_JOYCON_RIGHT: buttons = &g_defaultButtonsJoyConRight; break;
  case PAD_TYPE_JOYCON_PAIR: buttons = &g_defaultButtonsJoyPair; break;
  case PAD_TYPE_GAMECUBE: buttons = &g_defaultButtonsGamecube; break;
  case PAD_TYPE_NSO_GAMECUBE: buttons = &g_defaultButtonsNSOGamecube; break;
  default: break;
  }
  std::copy(buttons->begin(), buttons->end(), mapping->buttons);
  std::copy(g_defaultAxes.begin(), g_defaultAxes.end(), mapping->axes);
}

BOOL PADSetColor(const u32 port, const u8 red, const u8 green, const u8 blue) {
  const auto ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  ctrl->m_ledRed = red;
  ctrl->m_ledGreen = green;
  ctrl->m_ledBlue = blue;
  ctrl->m_isColorDirty = true;
  return true;
}

BOOL PADGetColor(const u32 port, u8* red, u8* green, u8* blue) {
  const auto ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  *red = ctrl->m_ledRed;
  *green = ctrl->m_ledGreen;
  *blue = ctrl->m_ledBlue;
  return TRUE;
}

BOOL PADSetSensorEnabled(const u32 port, const PADSensorType sensor, const BOOL enabled) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);

  if (controller_has_sensor(ctrl, sensor)) {
    return SDL_SetGamepadSensorEnabled(ctrl->m_controller, static_cast<SDL_SensorType>(sensor), enabled ? true : false)
               ? TRUE
               : FALSE;
  }

  return should_use_device_sensor(port, ctrl, sensor) ? TRUE : FALSE;
}

BOOL PADHasSensor(const u32 port, const PADSensorType sensor) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (controller_has_sensor(ctrl, sensor)) {
    return TRUE;
  }

  return should_use_device_sensor(port, ctrl, sensor) ? TRUE : FALSE;
}

BOOL PADGetSensorData(const u32 port, const PADSensorType sensor, f32* data, const int nValues) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (controller_has_sensor(ctrl, sensor)) {
    return SDL_GetGamepadSensorData(ctrl->m_controller, static_cast<SDL_SensorType>(sensor), data, nValues);
  }

  if (should_use_device_sensor(port, ctrl, sensor)) {
    return get_device_sensor_data(sensor, data, nValues) ? TRUE : FALSE;
  }

  return FALSE;
}

BOOL PADHasLED(const u32 port) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);

  if (ctrl == nullptr) {
    return FALSE;
  }

  return ctrl->m_hasRgbLed;
}

BOOL PADSetRumbleIntensity(const u32 port, const u16 low, const u16 high) {
  auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl != nullptr) {
    if (ctrl->m_isGameCube || (!ctrl->m_hasRumble && !should_use_device_rumble(port, ctrl))) {
      return FALSE;
    }
    EnsureMappingLoaded(ctrl);
    ctrl->m_rumbleIntensityLow = low;
    ctrl->m_rumbleIntensityHigh = high;
    return TRUE;
  }

  if (!should_use_device_rumble(port, nullptr)) {
    return FALSE;
  }
  aurora::input::set_device_rumble_intensity(low, high);
  return TRUE;
}

BOOL PADGetRumbleIntensity(const u32 port, u16* low, u16* high) {
  auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl != nullptr) {
    if (ctrl->m_isGameCube || (!ctrl->m_hasRumble && !should_use_device_rumble(port, ctrl))) {
      *low = 0;
      *high = 0;
      return FALSE;
    }
    EnsureMappingLoaded(ctrl);
    *low = ctrl->m_rumbleIntensityLow;
    *high = ctrl->m_rumbleIntensityHigh;
    return TRUE;
  }

  if (!should_use_device_rumble(port, nullptr)) {
    *low = 0;
    *high = 0;
    return FALSE;
  }

  aurora::input::get_device_rumble_intensity(low, high);
  return TRUE;
}

BOOL PADSupportsRumbleIntensity(const u32 port) {
  if (const auto* ctrl = aurora::input::get_controller_for_player(port)) {
    if (!ctrl->m_isGameCube && (ctrl->m_hasRumble || should_use_device_rumble(port, ctrl))) {
      return TRUE;
    }
    return FALSE;
  }
  return should_use_device_rumble(port, nullptr) ? TRUE : FALSE;
}

BOOL PADCanForceDeviceRumble(const u32 port) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  return ctrl != nullptr && !ctrl->m_isGameCube && ctrl->m_hasRumble && device_rumble_available_for_port(port) ? TRUE
                                                                                                               : FALSE;
}

BOOL PADGetForceDeviceRumble(const u32 port) {
  auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr || !PADCanForceDeviceRumble(port)) {
    return FALSE;
  }

  EnsureMappingLoaded(ctrl);
  return ctrl->m_forceDeviceRumble ? TRUE : FALSE;
}

BOOL PADSetForceDeviceRumble(const u32 port, const BOOL force) {
  auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr || !PADCanForceDeviceRumble(port)) {
    return FALSE;
  }

  EnsureMappingLoaded(ctrl);
  ctrl->m_forceDeviceRumble = force != FALSE;
  return TRUE;
}

BOOL PADIsGCAdapter(const u32 port) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }
  return ctrl->m_isGameCube;
}

PADBatteryState PADGetBatteryState(const u32 port, f32* perc) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return PAD_BATTERYSTATE_ERROR;
  }

  int tmp = 0;
  const auto ret = SDL_GetGamepadPowerInfo(ctrl->m_controller, &tmp);
  if (tmp != -1) {
    *perc = static_cast<float>(tmp) / 100.f;
  } else {
    *perc = static_cast<float>(tmp);
  }
  return static_cast<PADBatteryState>(ret);
}

PADControllerType PADGetControllerType(const u32 port) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return PAD_TYPE_UNKNOWN;
  }

  auto type = SDL_GetGamepadType(ctrl->m_controller);
  return static_cast<PADControllerType>(type);
}

PADControllerType PADGetControllerTypeForIndex(const u32 index) {
  const auto* ctrl = __PADGetControllerForIndex(index);
  if (ctrl == nullptr) {
    return PAD_TYPE_UNKNOWN;
  }

  auto type = SDL_GetGamepadType(ctrl->m_controller);
  if (type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && ctrl->m_pid == 0x2073) {
    return PAD_TYPE_NSO_GAMECUBE;
  }
  return static_cast<PADControllerType>(type);
}
