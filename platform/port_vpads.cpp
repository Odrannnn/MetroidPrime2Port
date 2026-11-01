// SDL virtual gamepads for the 2-4 controller multiplayer tests; see port_vpads.h.
#include "port_vpads.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

namespace PortVpads {
namespace {

constexpr int kMax = 4;
SDL_JoystickID sIds[kMax] = {};
SDL_Joystick* sJoy[kMax] = {};
bool sPolled = false;

struct SName {
  const char* name;
  int id;
};

// Virtual gamepad button i is SDL_GamepadButton i.
constexpr SName kButtons[] = {
    {"a", SDL_GAMEPAD_BUTTON_SOUTH},        {"b", SDL_GAMEPAD_BUTTON_EAST},
    {"x", SDL_GAMEPAD_BUTTON_WEST},         {"y", SDL_GAMEPAD_BUTTON_NORTH},
    {"back", SDL_GAMEPAD_BUTTON_BACK},      {"guide", SDL_GAMEPAD_BUTTON_GUIDE},
    {"start", SDL_GAMEPAD_BUTTON_START},    {"ls", SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"rs", SDL_GAMEPAD_BUTTON_RIGHT_STICK}, {"lb", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"rb", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}, {"up", SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN}, {"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
};
constexpr SName kAxes[] = {
    {"lx", SDL_GAMEPAD_AXIS_LEFTX},         {"ly", SDL_GAMEPAD_AXIS_LEFTY},
    {"rx", SDL_GAMEPAD_AXIS_RIGHTX},        {"ry", SDL_GAMEPAD_AXIS_RIGHTY},
    {"lt", SDL_GAMEPAD_AXIS_LEFT_TRIGGER},  {"rt", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER},
};

bool Attach(int slot, std::string& err) {
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  static char names[kMax][24];
  std::snprintf(names[slot], sizeof(names[slot]), "Virtual Pad %d", slot + 1);
  desc.name = names[slot];
  // A gyro, so gyro aim can be tested per pad (`vpad <i> gyro <yaw> <pitch>`, GYRO= in MP2_VPAD_INPUT).
  static const SDL_VirtualJoystickSensorDesc kSensors[] = {{SDL_SENSOR_GYRO, 250.f}};
  desc.nsensors = 1;
  desc.sensors = kSensors;
  desc.vendor_id = 0x4d50; // distinct ids: SDL's mapping name lookup is by GUID and would name every pad alike
  desc.product_id = static_cast<Uint16>(slot + 1);
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  if (id == 0) {
    err = std::string("SDL_AttachVirtualJoystick: ") + SDL_GetError();
    return false;
  }
  sIds[slot] = id;
  sJoy[slot] = SDL_OpenJoystick(id);
  if (sJoy[slot] != nullptr) {
    SDL_SetJoystickVirtualAxis(sJoy[slot], SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
    SDL_SetJoystickVirtualAxis(sJoy[slot], SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
  }
  return true;
}

bool Detach(int slot, std::string& err) {
  if (sIds[slot] == 0) {
    err = "pad not attached";
    return false;
  }
  if (sJoy[slot] != nullptr) {
    SDL_CloseJoystick(sJoy[slot]);
    sJoy[slot] = nullptr;
  }
  SDL_DetachVirtualJoystick(sIds[slot]);
  sIds[slot] = 0;
  return true;
}

} // namespace

void Poll() {
  if (sPolled) {
    return;
  }
  sPolled = true;
  const char* env = std::getenv("MP2_VPADS");
  if (env == nullptr || *env == '\0') {
    return;
  }
  const int n = std::min(std::max(std::atoi(env), 0), kMax);
  for (int i = 0; i < n; ++i) {
    std::string err;
    if (!Attach(i, err)) {
      std::fprintf(stderr, "[vpad] %s\n", err.c_str());
    }
  }
  std::fprintf(stderr, "[vpad] attached %d virtual gamepads\n", n);
}

bool Set(int slot, const std::string& what, double value, std::string& err) {
  if (slot < 0 || slot >= kMax || sJoy[slot] == nullptr) {
    err = "vpad " + std::to_string(slot) + " not attached";
    return false;
  }
  for (const SName& b : kButtons) {
    if (what == b.name) {
      SDL_SetJoystickVirtualButton(sJoy[slot], b.id, value != 0);
      return true;
    }
  }
  for (const SName& a : kAxes) {
    if (what == a.name) {
      const bool trigger = a.id == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a.id == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
      const double v = std::min(std::max(value, trigger ? 0.0 : -1.0), 1.0);
      // A trigger spans the whole axis range and rests at its minimum.
      SDL_SetJoystickVirtualAxis(sJoy[slot], a.id,
                                 static_cast<Sint16>(trigger ? v * 65535.0 - 32768.0 : v * 32767.0));
      return true;
    }
  }
  err = "unknown button/axis '" + what + "'";
  return false;
}

bool SetGyro(int slot, double yaw, double pitch, std::string& err) {
  if (slot < 0 || slot >= kMax || sJoy[slot] == nullptr) {
    err = "vpad " + std::to_string(slot) + " not attached";
    return false;
  }
  // SDL gamepad gyro: x = pitch rate, y = yaw rate (positive turns left, counter-clockwise from
  // above), rad/s; it holds the last value sent. The yaw/pitch arguments are the turn the game
  // reads: positive yaw turns right, positive pitch looks up.
  const float data[3] = {static_cast<float>(pitch), static_cast<float>(-yaw), 0.f};
  if (!SDL_SendJoystickVirtualSensorData(sJoy[slot], SDL_SENSOR_GYRO, SDL_GetTicksNS(), data, 3)) {
    err = std::string("SDL_SendJoystickVirtualSensorData: ") + SDL_GetError();
    return false;
  }
  return true;
}

bool Present(int slot) { return slot >= 0 && slot < kMax && sIds[slot] != 0; }

bool AttachSlot(int slot, std::string& err) {
  if (slot < 0 || slot >= kMax || sIds[slot] != 0) {
    err = "bad or already attached slot";
    return false;
  }
  return Attach(slot, err);
}

bool DetachSlot(int slot, std::string& err) {
  if (slot < 0 || slot >= kMax) {
    err = "bad slot";
    return false;
  }
  return Detach(slot, err);
}

bool Command(const std::string& args, std::string& out, std::string& err) {
  std::istringstream in(args);
  std::string first;
  in >> first;
  if (first == "list" || first.empty()) {
    for (int i = 0; i < kMax; ++i) {
      out += "vpad " + std::to_string(i) + ": " + (sIds[i] != 0 ? "attached" : "none") + "\n";
    }
    return true;
  }
  if (first == "attach") {
    int n = kMax;
    in >> n;
    int done = 0;
    for (int i = 0; i < kMax && done < n; ++i) {
      if (sIds[i] == 0) {
        if (!Attach(i, err)) {
          return false;
        }
        out += "attached vpad " + std::to_string(i) + "\n";
        ++done;
      }
    }
    return true;
  }
  const int slot = std::atoi(first.c_str());
  if (slot < 0 || slot >= kMax || (first[0] < '0' || first[0] > '3')) {
    err = "usage: vpad list | attach [n] | detach <i> | <i> <button|axis> <value> | <i> gyro <yaw> <pitch>";
    return false;
  }
  std::string what;
  in >> what;
  if (what == "detach") {
    if (!Detach(slot, err)) {
      return false;
    }
    out += "detached vpad " + std::to_string(slot) + "\n";
    return true;
  }
  if (what == "gyro") {
    double yaw = 0, pitch = 0;
    in >> yaw >> pitch;
    if (!SetGyro(slot, yaw, pitch, err)) {
      return false;
    }
    out += "ok\n";
    return true;
  }
  double value = 1;
  in >> value;
  if (!Set(slot, what, value, err)) {
    return false;
  }
  out += "ok\n";
  return true;
}

} // namespace PortVpads
