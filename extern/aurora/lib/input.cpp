#include "input.hpp"
#include "device.hpp"
#include "internal.hpp"
#include "io.hpp"

#include "magic_enum.hpp"

#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL.h>
#include <absl/container/flat_hash_map.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <utility>

using namespace std::string_view_literals;

namespace aurora::input {
absl::flat_hash_map<Uint32, GameController> g_GameControllers;

namespace {
constexpr Module Log{"aurora::input"};

constexpr uint32_t kPortPreferencesMagic = SBIG('CPRT');
constexpr uint32_t kPortPreferencesVersion = 3;
constexpr uint32_t kMaxPersistedStringLength = 256;
constexpr uint16_t kDefaultDeviceRumbleIntensityLow = 0x8000;
constexpr uint16_t kDefaultDeviceRumbleIntensityHigh = 0x8000;

enum class PortPreferenceState : uint8_t {
  Unset = 0,
  None = 1,
  Controller = 2,
};

struct ControllerIdentity {
  std::string guid;
  std::string serial;
};

struct PortPreference {
  PortPreferenceState state = PortPreferenceState::Unset;
  ControllerIdentity identity;
};

struct DevicePreference {
  uint16_t rumbleIntensityLow = kDefaultDeviceRumbleIntensityLow;
  uint16_t rumbleIntensityHigh = kDefaultDeviceRumbleIntensityHigh;
};

std::array<PortPreference, PAD_MAX_CONTROLLERS> g_portPreferences;
DevicePreference g_devicePreference;
bool g_portPreferencesLoaded = false;

std::filesystem::path port_preferences_path() {
  if (g_config.userPath == nullptr) {
    return {};
  }
  return io::fs_path_from_string(g_config.userPath) / "controller_ports.dat";
}

std::string normalize_serial(const char* serial) {
  std::string normalized;
  if (serial == nullptr) {
    return normalized;
  }

  for (const char* c = serial; *c != '\0'; ++c) {
    if (*c == ':' || *c == '-') {
      continue;
    }
    normalized += *c >= 'A' && *c <= 'Z' ? static_cast<char>(*c - 'A' + 'a') : *c;
  }
  return normalized;
}

bool serial_matches(const std::string& saved, const std::string& current) {
  return !saved.empty() && !current.empty() && normalize_serial(saved.c_str()) == normalize_serial(current.c_str());
}

ControllerIdentity controller_identity(const GameController& controller) {
  ControllerIdentity identity;
  char guid[33] = {};
  SDL_GUIDToString(SDL_GetGamepadGUIDForID(SDL_GetGamepadID(controller.m_controller)), guid, sizeof(guid));
  identity.guid = guid;
  identity.serial = normalize_serial(SDL_GetGamepadSerial(controller.m_controller));
  return identity;
}

template <typename T>
bool read_value(SDL_IOStream* file, T& value) {
  return io::read_exact(file, &value, sizeof(value));
}

template <typename T>
bool write_value(SDL_IOStream* file, const T& value) {
  return io::write_exact(file, &value, sizeof(value));
}

bool read_string(SDL_IOStream* file, std::string& value) {
  uint32_t size = 0;
  if (!read_value(file, size) || size > kMaxPersistedStringLength) {
    return false;
  }

  value.resize(size);
  return size == 0 || io::read_exact(file, value.data(), size);
}

bool write_string(SDL_IOStream* file, const std::string& value) {
  const uint32_t size = static_cast<uint32_t>(std::min<size_t>(value.size(), kMaxPersistedStringLength));
  return write_value(file, size) && (size == 0 || io::write_exact(file, value.data(), size));
}

bool read_identity(SDL_IOStream* file, ControllerIdentity& identity) {
  return read_string(file, identity.guid) && read_string(file, identity.serial);
}

bool write_identity(SDL_IOStream* file, const ControllerIdentity& identity) {
  return write_string(file, identity.guid) && write_string(file, identity.serial);
}

bool read_port_preferences_file(std::array<PortPreference, PAD_MAX_CONTROLLERS>& preferences) {
  const auto path = port_preferences_path();
  if (path.empty()) {
    return true;
  }

  auto file = io::open_file(path, "rb");
  if (!file) {
    return true;
  }

  uint32_t magic = 0;
  uint32_t version = 0;
  auto devicePreference = g_devicePreference;
  bool ok = read_value(file.get(), magic) && read_value(file.get(), version) && magic == kPortPreferencesMagic &&
            (version == 2 || version == kPortPreferencesVersion);

  for (auto& preference : preferences) {
    uint8_t state = 0;
    ControllerIdentity identity;
    ok = ok && read_value(file.get(), state) && state <= static_cast<uint8_t>(PortPreferenceState::Controller) &&
         read_identity(file.get(), identity);
    if (ok) {
      preference.state = static_cast<PortPreferenceState>(state);
      preference.identity = std::move(identity);
    }
  }
  if (ok && version >= 3) {
    ok = read_value(file.get(), devicePreference.rumbleIntensityLow) &&
         read_value(file.get(), devicePreference.rumbleIntensityHigh);
  }

  if (!ok) {
    Log.warn("Ignoring invalid controller port preference file '{}'", io::fs_path_to_string(path));
  } else {
    g_devicePreference = devicePreference;
  }
  return ok;
}

void ensure_port_preferences_loaded() {
  if (g_portPreferencesLoaded) {
    return;
  }

  std::array<PortPreference, PAD_MAX_CONTROLLERS> preferences;
  if (read_port_preferences_file(preferences)) {
    g_portPreferences = std::move(preferences);
  }
  g_portPreferencesLoaded = true;
}

void save_port_preferences() {
  if (io::config_persistence_disabled()) {
    return;
  }
  const auto path = port_preferences_path();
  if (path.empty()) {
    return;
  }

  const auto pathString = io::fs_path_to_string(path);
  auto file = io::open_atomic_file(path);
  if (!file) {
    Log.warn("Failed to open controller port preference file '{}': {}", pathString, SDL_GetError());
    return;
  }

  bool ok = write_value(file.get(), kPortPreferencesMagic) && write_value(file.get(), kPortPreferencesVersion);
  for (const auto& preference : g_portPreferences) {
    const auto state = static_cast<uint8_t>(preference.state);
    ok = ok && write_value(file.get(), state) && write_identity(file.get(), preference.identity);
  }
  ok = ok && write_value(file.get(), g_devicePreference.rumbleIntensityLow) &&
       write_value(file.get(), g_devicePreference.rumbleIntensityHigh);
  if (!ok || !file.commit()) {
    Log.warn("Failed to write controller port preference file '{}': {}", pathString, SDL_GetError());
  }
}

enum class IdentityMatch {
  None,
  Fallback,
  Exact,
};

IdentityMatch identity_match(const ControllerIdentity& saved, const ControllerIdentity& current) {
  if (saved.guid.empty()) {
    return IdentityMatch::None;
  }
  if (saved.guid == current.guid) {
    return saved.serial.empty() || serial_matches(saved.serial, current.serial) ? IdentityMatch::Exact
                                                                                : IdentityMatch::None;
  }
  if (!serial_matches(saved.serial, current.serial)) {
    return IdentityMatch::None;
  }

  // Attempt to match against VID/PID as a fallback if the GUID changes
  uint16_t savedVendor = 0;
  uint16_t savedProduct = 0;
  uint16_t currentVendor = 0;
  uint16_t currentProduct = 0;
  SDL_GetJoystickGUIDInfo(SDL_StringToGUID(saved.guid.c_str()), &savedVendor, &savedProduct, nullptr, nullptr);
  SDL_GetJoystickGUIDInfo(SDL_StringToGUID(current.guid.c_str()), &currentVendor, &currentProduct, nullptr, nullptr);
  return savedVendor != 0 && savedVendor == currentVendor && savedProduct != 0 && savedProduct == currentProduct
             ? IdentityMatch::Fallback
             : IdentityMatch::None;
}

bool is_instance_claimed(const std::array<Uint32, PAD_MAX_CONTROLLERS>& claimedControllers, size_t claimedCount,
                         Uint32 instance) {
  return std::find(claimedControllers.begin(), claimedControllers.begin() + claimedCount, instance) !=
         claimedControllers.begin() + claimedCount;
}

void apply_port_preferences() noexcept {
  ensure_port_preferences_loaded();
  if (!std::any_of(g_portPreferences.begin(), g_portPreferences.end(),
                   [](const auto& preference) { return preference.state != PortPreferenceState::Unset; })) {
    return;
  }

  for (auto& [instance, controller] : g_GameControllers) {
    const int32_t player = SDL_GetGamepadPlayerIndex(controller.m_controller);
    if (player >= 0 && player < PAD_MAX_CONTROLLERS && g_portPreferences[player].state != PortPreferenceState::Unset) {
      // Keep SDL's default player assignment from taking explicitly configured ports
      SDL_SetGamepadPlayerIndex(controller.m_controller, -1);
    }
  }

  std::array<Uint32, PAD_MAX_CONTROLLERS> claimedControllers{};
  size_t claimedCount = 0;
  for (uint32_t port = 0; port < g_portPreferences.size(); ++port) {
    const auto& preference = g_portPreferences[port];
    if (preference.state != PortPreferenceState::Controller) {
      continue;
    }

    Uint32 fallbackInstance = 0;
    GameController* fallbackController = nullptr;
    for (auto& [instance, controller] : g_GameControllers) {
      if (is_instance_claimed(claimedControllers, claimedCount, instance)) {
        continue;
      }

      switch (identity_match(preference.identity, controller_identity(controller))) {
      case IdentityMatch::Exact:
        SDL_SetGamepadPlayerIndex(controller.m_controller, static_cast<int32_t>(port));
        claimedControllers[claimedCount++] = instance;
        fallbackController = nullptr;
        break;
      case IdentityMatch::Fallback:
        // Prefer any later exact match before claiming a fallback candidate
        if (fallbackController == nullptr) {
          fallbackInstance = instance;
          fallbackController = &controller;
        }
        continue;
      case IdentityMatch::None:
        continue;
      }
      break;
    }

    if (fallbackController != nullptr) {
      SDL_SetGamepadPlayerIndex(fallbackController->m_controller, static_cast<int32_t>(port));
      claimedControllers[claimedCount++] = fallbackInstance;
    }
  }
}

// The port's Android touch overlay is an SDL virtual gamepad, attached on the first touch. Once it
// holds player 1, a real pad connected later gets player 2, which a single-player game never reads.
// Keep player 1 for a real pad when one is connected, and hand it back to the virtual pad when the
// last real one leaves. An explicitly configured first port is left alone.
// The Steam Deck's built-in controller (SDL reports it, 28de:1205, behind Steam's virtual pad too)
// ranks with the virtual pads: a controller connected to a Deck takes player 1 from it, whatever
// Steam's order, and the Deck gets it back when that controller leaves.
bool is_fallback_controller(Uint32 instance, const GameController& controller) noexcept {
  return SDL_IsJoystickVirtual(instance) || (controller.m_vid == 0x28de && controller.m_pid == 0x1205);
}

void prefer_real_controller_on_first_port() noexcept {
  ensure_port_preferences_loaded();
  if (g_portPreferences[0].state != PortPreferenceState::Unset) {
    return;
  }

  GameController* first = nullptr;
  GameController* real = nullptr;
  GameController* virt = nullptr;
  for (auto& [instance, controller] : g_GameControllers) {
    if (SDL_GetGamepadPlayerIndex(controller.m_controller) == 0) {
      first = &controller;
    } else if (!is_fallback_controller(instance, controller)) {
      real = real != nullptr ? real : &controller;
    } else {
      virt = virt != nullptr ? virt : &controller;
    }
  }

  // A real pad on port 0 stays, and a virtual pad only takes port 0 when it is empty
  const bool firstIsReal = first != nullptr && !is_fallback_controller(first->m_index, *first);
  GameController* wanted = real != nullptr ? real : virt;
  if (wanted == nullptr || firstIsReal || (first != nullptr && wanted == virt)) {
    return;
  }
  const int32_t freed = SDL_GetGamepadPlayerIndex(wanted->m_controller);
  if (first != nullptr) {
    SDL_SetGamepadPlayerIndex(first->m_controller, freed);
  }
  SDL_SetGamepadPlayerIndex(wanted->m_controller, 0);
  Log.info("Controller '{}' moved to port 0", SDL_GetGamepadName(wanted->m_controller) != nullptr
                                                  ? SDL_GetGamepadName(wanted->m_controller)
                                                  : "unknown");
}
} // namespace

GameController* get_controller_for_player(uint32_t player) noexcept {
  for (auto& [which, controller] : g_GameControllers) {
    if (player_index(which) == player) {
      return &controller;
    }
  }

#if 0
  /* If we don't have a controller assigned to this port use the first unassigned controller */
  if (!g_GameControllers.empty()) {
    int32_t availIndex = -1;
    GameController* ct = nullptr;
    for (auto& controller : g_GameControllers) {
      if (player_index(controller.first) == -1) {
        availIndex = controller.first;
        ct = &controller.second;
        break;
      }
    }
    if (availIndex != -1) {
      set_player_index(availIndex, player);
      return ct;
    }
  }
#endif
  return nullptr;
}

Sint32 get_instance_for_player(uint32_t player) noexcept {
  for (const auto& [which, controller] : g_GameControllers) {
    if (player_index(which) == player) {
      return which;
    }
  }

  return {};
}

#ifdef SDL_PLATFORM_ANDROID
// SDL's mapping for an Android pad it doesn't know covers buttons 0-14 and
// analog trigger axes only. Digital L2/R2 keys arrive as raw buttons 15 and 16
// and reach nothing, so handhelds such as the RG Rotate (#81) can't bind them.
// Map them to the triggers when the pad has no trigger of its own.
//
// Anbernic's built-in "retrogame_joypad" (484b:1101, the RG Rotate) also
// declares LTRIGGER/RTRIGGER/BRAKE/GAS axes that never move: L2/R2 only ever
// arrive as keys. SDL maps the triggers to those dead axes, so on that pad
// the buttons replace the axis bindings.
static bool has_dead_trigger_axes(SDL_Gamepad* ctrl) noexcept {
  return SDL_GetGamepadVendor(ctrl) == 0x484b && SDL_GetGamepadProduct(ctrl) == 0x1101;
}

static void map_digital_triggers(SDL_Gamepad* ctrl) noexcept {
  const int buttons = SDL_GetNumJoystickButtons(SDL_GetGamepadJoystick(ctrl));
  if (buttons <= 15) {
    return;
  }
  const bool deadAxes = has_dead_trigger_axes(ctrl);
  int count = 0;
  SDL_GamepadBinding** bindings = SDL_GetGamepadBindings(ctrl, &count);
  if (bindings == nullptr) {
    return;
  }
  bool hasTrigger = false;
  for (int i = 0; i < count; ++i) {
    if (bindings[i]->output_type == SDL_GAMEPAD_BINDTYPE_AXIS &&
        (bindings[i]->output.axis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
         bindings[i]->output.axis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
      hasTrigger = true;
    }
  }
  SDL_free(bindings);
  char* mapping = SDL_GetGamepadMapping(ctrl);
  if ((hasTrigger && !deadAxes) || mapping == nullptr) {
    SDL_free(mapping);
    return;
  }
  std::string updated;
  {
    // Copy the mapping without its trigger entries.
    std::string_view rest = mapping;
    while (!rest.empty()) {
      const size_t comma = rest.find(',');
      const std::string_view entry = rest.substr(0, comma);
      if (!entry.starts_with("lefttrigger:") && !entry.starts_with("righttrigger:") && !entry.empty()) {
        updated.append(entry);
        updated += ',';
      }
      rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
    }
  }
  SDL_free(mapping);
  updated += "lefttrigger:b15,";
  if (buttons > 16) {
    updated += "righttrigger:b16,";
  }
  if (SDL_AddGamepadMapping(updated.c_str()) < 0) {
    Log.warn("Failed to map digital triggers: {}", SDL_GetError());
  }
}
#endif

SDL_JoystickID add_controller(SDL_JoystickID which) noexcept {
  auto* ctrl = SDL_OpenGamepad(which);
  if (ctrl != nullptr) {
#ifdef SDL_PLATFORM_ANDROID
    map_digital_triggers(ctrl);
#endif
    GameController controller;
    controller.m_controller = ctrl;
    controller.m_index = which;
    controller.m_vid = SDL_GetGamepadVendor(ctrl);
    controller.m_pid = SDL_GetGamepadProduct(ctrl);
    if (controller.m_vid == 0x05ac /* USB_VENDOR_APPLE */ && controller.m_pid == 3) {
      // Ignore Apple TV remote
      SDL_CloseGamepad(ctrl);
      return -1;
    }
    controller.m_isGameCube = controller.m_vid == 0x057E && controller.m_pid == 0x0337;
    if (controller.m_isGameCube ||
        (SDL_GetGamepadType(ctrl) == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && controller.m_pid == 0x2073)) {
      controller.m_deadZones.emulateTriggers = false;
    }
    const auto props = SDL_GetGamepadProperties(ctrl);
    controller.m_hasRumble = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, true);
    controller.m_hasRgbLed = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false);
    SDL_JoystickID instance = SDL_GetJoystickID(SDL_GetGamepadJoystick(ctrl));
    g_GameControllers[instance] = controller;
    apply_port_preferences();
    prefer_real_controller_on_first_port();
    Log.info("Added controller '{}' (instance {}, vid {:04x}, pid {:04x}, type {}, port {})",
             SDL_GetGamepadName(ctrl) != nullptr ? SDL_GetGamepadName(ctrl) : "unknown", instance, controller.m_vid,
             controller.m_pid, static_cast<int>(SDL_GetGamepadType(ctrl)), SDL_GetGamepadPlayerIndex(ctrl));
    return instance;
  }

  return -1;
}

void remove_controller(Uint32 instance) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    Log.info("Removed controller '{}' (instance {})",
             SDL_GetGamepadName(it->second.m_controller) != nullptr ? SDL_GetGamepadName(it->second.m_controller)
                                                                    : "unknown",
             instance);
    SDL_CloseGamepad(it->second.m_controller);
    g_GameControllers.erase(it);
    apply_port_preferences();
    prefer_real_controller_on_first_port();
  }
}

bool is_gamecube(Uint32 instance) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    return it->second.m_isGameCube;
  }
  return false;
}

int32_t player_index(Uint32 instance) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    return SDL_GetGamepadPlayerIndex(it->second.m_controller);
  }
  return -1;
}

void set_player_index(Uint32 instance, Sint32 index) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    SDL_SetGamepadPlayerIndex(it->second.m_controller, index);
  }
}

std::string controller_name(Uint32 instance) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    const auto* name = SDL_GetGamepadName(it->second.m_controller);
    if (name != nullptr) {
      return {name};
    }
  }
  return {};
}

bool controller_has_rumble(Uint32 instance) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    return it->second.m_hasRumble;
  }
  return false;
}

void controller_rumble(uint32_t instance, uint16_t low_freq_intensity, uint16_t high_freq_intensity,
                       uint16_t duration_ms) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    SDL_RumbleGamepad(it->second.m_controller, low_freq_intensity, high_freq_intensity, duration_ms);
  }
}

void get_device_rumble_intensity(uint16_t* low_freq_intensity, uint16_t* high_freq_intensity) noexcept {
  ensure_port_preferences_loaded();
  *low_freq_intensity = g_devicePreference.rumbleIntensityLow;
  *high_freq_intensity = g_devicePreference.rumbleIntensityHigh;
}

void set_device_rumble_intensity(const uint16_t low_freq_intensity, const uint16_t high_freq_intensity) noexcept {
  ensure_port_preferences_loaded();
  g_devicePreference.rumbleIntensityLow = low_freq_intensity;
  g_devicePreference.rumbleIntensityHigh = high_freq_intensity;
  save_port_preferences();
}

uint32_t controller_count() noexcept { return g_GameControllers.size(); }

void persist_controller_for_player(uint32_t player, const GameController* controller) noexcept {
  if (player >= PAD_MAX_CONTROLLERS) {
    return;
  }

  ensure_port_preferences_loaded();
  if (controller != nullptr) {
    g_portPreferences[player].state = PortPreferenceState::Controller;
    g_portPreferences[player].identity = controller_identity(*controller);
  } else {
    g_portPreferences[player].state = PortPreferenceState::None;
    g_portPreferences[player].identity = {};
  }
  save_port_preferences();
}

void initialize() noexcept {
  /* Make sure we initialize everything input related now, this will automatically add all of the connected controllers
   * as expected */
  SDL_InitFlags flags = SDL_INIT_HAPTIC | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_SENSOR;
  if (headless()) {
    // Port: AURORA_HEADLESS: a test run reads no host controller (the virtual pads still work).
    // SDL opens every /dev/input/event* device to probe it, and each close costs ~16 ms, which
    // was ~0.6 s of every start-up: classic enumeration only probes /dev/input/js*, HIDAPI is
    // off and the haptic subsystem (another probe of every event device) is not started.
    SDL_SetHint(SDL_HINT_JOYSTICK_LINUX_CLASSIC, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    flags &= ~SDL_INIT_HAPTIC;
  }
  AURORA_ASSERT(SDL_Init(flags), "Failed to initialize SDL subsystems: {}", SDL_GetError());
}

struct MouseScrollStatus {
  float scrollX;
  float scrollY;
};

static MouseScrollStatus g_MouseStatus;

void set_mouse_scroll(const float scrollX, const float scrollY) noexcept {
  g_MouseStatus.scrollX = scrollX;
  g_MouseStatus.scrollY = scrollY;
}

void get_mouse_scroll(float* scrollX, float* scrollY) noexcept {
  *scrollX = g_MouseStatus.scrollX;
  *scrollY = g_MouseStatus.scrollY;
}

void shutdown() noexcept {
  // Upon shutdown we want to ensure all controllers are in a default state, so force all rumble supporting controllers
  // to shut off their rumble motors.
  for (const auto& controller : g_GameControllers) {
    if (!controller.second.m_hasRumble) {
      continue;
    }
    controller_rumble(controller.first, 0, 0, 0);
  }
  device::shutdown();
}
} // namespace aurora::input
