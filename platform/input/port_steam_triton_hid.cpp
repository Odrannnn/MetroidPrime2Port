// The HID client for the controllers Steam Input hides from SDL (its
// SDL_GAMECONTROLLER_IGNORE_DEVICES list): the Steam Controller (2026), the Steam Deck, and Sony
// and Nintendo controllers. Reads their state reports beside Steam Input, for gyro (Gyro source
// Controller or Auto) and the back buttons, which Steam's virtual pads lack. SDL stays out of them:
// its drivers manage lizard mode, the IMU, lights and report modes against Steam. This only reads,
// and asks for the IMU when Steam turned it off (Valve and Switch controllers; Sony's always run).
// Called only from the input poll (port_events.cpp), so it needs no locking. Nothing here may
// block the poll: controllers are looked for only when SDL sees a HID device come or go, and the
// IMU setting is sent only to the interface that answers (a feature report to an empty puck slot
// waits ~150 ms).
//
// Several can report at once (a Deck's built-in controller beside a Steam Controller on Bluetooth
// or the puck, or a DualSense), so each interface keeps its own newest report, and the one behind
// Player 1's pad is used (IsPlayerOnePad; a Steam Controller before the Deck when Player 1's can't
// be told; IsBehindPlayerOne for Sony and Nintendo ones, both Joy-Cons of a pair).

#include "port_steam_triton.h"

#include "port_overlay.h"

#include <dolphin/pad.h>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hidapi.h>
#include <SDL3/SDL_timer.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace PortSteamTriton {

static_assert(kRightPaddle1 == SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 &&
                  kLeftPaddle1 == SDL_GAMEPAD_BUTTON_LEFT_PADDLE1 &&
                  kRightPaddle2 == SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2 &&
                  kLeftPaddle2 == SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,
              "paddle bits follow SDL_GamepadButton");

namespace {

Stats sStats;
// The used controller's newest report.
Sample sLast;

#if defined(__linux__) && !defined(__ANDROID__)
constexpr uint64_t kSecondNs = 1000000000ull;
// The controller reports a few hundred times a second; quieter than this is gone.
constexpr uint64_t kFreshNs = kSecondNs / 4;

// One opened interface. The puck has one per controller slot, so every candidate is opened
// (cheap, and a controller can pair to any slot) and the ones that send reports are used.
struct Device {
  SDL_hid_device* handle = nullptr;
  uint16_t vendor = 0;
  uint16_t product = 0;
  int interfaceNumber = -1;
  Family family = Family::Valve;
  bool deck = false;
  bool bluetooth = false;
  // A Sony controller's calibration (its factory one, else nominal).
  ImuScale scale;
  // A Joy-Con held alone sideways (not one of a pair), as last seen.
  bool miniGamepad = false;
  // A Switch controller's subcommand packet number.
  uint8_t switchCounter = 0;
  Sample last;
  uint64_t lastReportNs = 0;
  ImuWatch imu;
  uint8_t loggedReport = 0;
  uint32_t rateCount = 0;
  float reportsPerSecond = 0.f;
  uint64_t lastImuEnableNs = 0;
  bool imuEnableSent = false;
  bool imuOff = false;
};
std::vector< Device > sDevices;
// SDL_hid_device_change_count when the controllers were last looked for; they are looked for
// again only when that changes (a HID device came or went), as enumerating takes ~20 ms.
Uint32 sSearchedChangeCount = 0;
bool sSearched = false;
// The device used last poll (an index into sDevices, -1 for none), for the log.
int sUsed = -1;
// Update's scratch lists (kept to spare the poll an allocation).
std::vector< PadId > sSteamPads;
std::vector< PadId > sOthers;
uint64_t sRateStartNs = 0;

const char* Kind(uint16_t product) {
  return IsDeckProduct(product)              ? "steam deck"
         : product == kProductPuck           ? "puck"
         : product == kProductBluetooth      ? "bluetooth"
         : product == kProductInternalAdapter ? "adapter"
                                             : "usb";
}

const char* Kind(const Device& d) {
  switch (d.family) {
  case Family::Valve:
    return Kind(d.product);
  case Family::DualShock4:
    return d.bluetooth ? "dualshock 4 bluetooth" : "dualshock 4 usb";
  case Family::DualSense:
    return d.bluetooth ? "dualsense bluetooth" : "dualsense usb";
  case Family::DualSenseEdge:
    return d.bluetooth ? "dualsense edge bluetooth" : "dualsense edge usb";
  case Family::SwitchPro:
    return d.bluetooth ? "switch pro bluetooth" : "switch pro usb";
  case Family::JoyConL:
    return "joy-con (L)";
  case Family::JoyConR:
    return "joy-con (R)";
  }
  return "controller";
}

// The real controller behind Player 1's pad (SDL reports it for a Steam virtual pad).
struct PlayerOne {
  bool havePad = false;
  // A Steam Input virtual pad.
  bool steam = false;
  uint16_t vendor = 0;
  uint16_t product = 0;
};
PlayerOne GetPlayerOne() {
  PlayerOne p;
  const s32 index = PADGetIndexForPort(PAD_CHAN0);
  if (SDL_Gamepad* pad = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr) {
    p.havePad = true;
    p.steam = SDL_GetGamepadSteamHandle(pad) != 0;
    p.vendor = SDL_GetGamepadVendor(pad);
    p.product = SDL_GetGamepadProduct(pad);
  }
  return p;
}

// The physical controllers behind Steam's virtual pads (SDL reports them from Steam's slot info).
void SteamPads(std::vector< PadId >& out) {
  out.clear();
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  for (int i = 0; ids != nullptr && i < count; ++i) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
    if (pad != nullptr && SDL_GetGamepadSteamHandle(pad) != 0) {
      out.push_back(PadId{SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad)});
    }
  }
  SDL_free(ids);
}

// The controllers Steam hides from SDL: a run launched from Steam (Steam Input) sets
// SDL_GAMECONTROLLER_IGNORE_DEVICES with them in it. Without it SDL drives them itself, gyro and
// back buttons included, and this reader would only duplicate it.
struct SteamHidden {
  bool valve = false;
  std::vector< PadId > others;
};
const SteamHidden& Hidden() {
  static const SteamHidden hidden = [] {
    SteamHidden h;
    const char* list = std::getenv("SDL_GAMECONTROLLER_IGNORE_DEVICES");
    for (const char* p = list; p != nullptr && *p != '\0';) {
      unsigned vendor = 0;
      unsigned product = 0;
      Family family;
      if (std::sscanf(p, "%x/%x", &vendor, &product) == 2) {
        if (vendor == kVendor) {
          h.valve = true;
        } else if (OtherFamily(uint16_t(vendor), uint16_t(product), family)) {
          h.others.push_back(PadId{uint16_t(vendor), uint16_t(product)});
        }
      }
      p = std::strchr(p, ',');
      p = p != nullptr ? p + 1 : nullptr;
    }
    std::fprintf(stderr, "[steamctl] %s; %zu Sony / Nintendo controller ids hidden (read directly)\n",
                 h.valve ? "Steam Input hides the Steam Controller / Deck from SDL: reading them directly"
                         : "Steam Input does not hide the Steam Controller / Deck: SDL reads them",
                 h.others.size());
    return h;
  }();
  return hidden;
}

bool SteamHides(uint16_t vendor, uint16_t product) {
  const SteamHidden& h = Hidden();
  if (vendor == kVendor) {
    return h.valve;
  }
  for (const PadId& id : h.others) {
    if (id.vendor == vendor && id.product == product) {
      return true;
    }
  }
  return false;
}

// Whether the gyro may read this controller (Gyro aim or Spring Ball on flick, from source Auto or
// Controller). The Steam Input back buttons setting alone opens it too, but leaves its motion
// sensor as Steam set it.
bool GyroWanted() {
  const int source = PortOverlay::GyroSource();
  return (PortOverlay::GyroAim() || PortOverlay::SpringBallFlick()) && (source == 0 || source == 1);
}

void Close() {
  for (Device& d : sDevices) {
    SDL_hid_close(d.handle);
  }
  sDevices.clear();
  sUsed = -1;
  sStats = Stats{};
  sLast = Sample{};
}

// A Sony controller's factory calibration (one feature report read; it blocks a few ms, once per
// controller found). Without it, or if SDL would reject it, the nominal scale.
void LoadSonyCalibration(Device& d) {
  const bool ds4 = d.family == Family::DualShock4;
  uint8_t report[64] = {};
  report[0] = !ds4                ? kDualSenseCalibrationReport
              : d.bluetooth ? kDualShock4CalibrationReportBluetooth
                            : kDualShock4CalibrationReportUsb;
  const int got = SDL_hid_get_feature_report(d.handle, report, sizeof(report));
  const bool ok = got > 0 && (ds4 ? ParseDualShock4Calibration(report, size_t(got), d.bluetooth, d.scale)
                                  : ParseDualSenseCalibration(report, size_t(got), d.scale));
  if (!ok) {
    d.scale = SonyDefaultScale();
  }
  std::fprintf(stderr, "[steamctl] %s motion calibration: %s\n", Kind(d),
               ok ? "the controller's" : got > 0 ? "rejected, nominal" : "unread, nominal");
}

void Open() {
  sSearched = true;
  sSearchedChangeCount = SDL_hid_device_change_count();
  SDL_hid_device_info* list = SDL_hid_enumerate(0, 0);
  for (SDL_hid_device_info* info = list; info != nullptr; info = info->next) {
    Family family = Family::Valve;
    if (info->vendor_id == kVendor) {
      // The vendor-defined interface (usage page 0xFF00 and up) carries the state reports. The
      // Deck's controller is interface 2, beside its lizard mode mouse and keyboard; that is
      // checked too in case a platform reports no usage page.
      const bool deck = IsDeckProduct(info->product_id);
      if (!IsSupportedProduct(info->product_id) ||
          (info->usage_page < 0xFF00 && !(deck && info->interface_number == 2))) {
        continue;
      }
    } else if (!OtherFamily(info->vendor_id, info->product_id, family)) {
      continue;
    }
    // Only while Steam hides it from SDL (SDL reads it itself otherwise).
    if (!SteamHides(info->vendor_id, info->product_id)) {
      continue;
    }
    if (SDL_hid_device* h = SDL_hid_open_path(info->path)) {
      SDL_hid_set_nonblocking(h, 1);
      Device d;
      d.handle = h;
      d.vendor = info->vendor_id;
      d.product = info->product_id;
      d.interfaceNumber = info->interface_number;
      d.family = family;
      d.deck = family == Family::Valve && IsDeckProduct(info->product_id);
      d.bluetooth = info->bus_type == SDL_HID_API_BUS_BLUETOOTH;
      std::fprintf(stderr, "[steamctl] opened %s %04x:%04x interface %d (usage page %04x)\n", Kind(d), d.vendor,
                   d.product, info->interface_number, info->usage_page);
      if (IsSony(family)) {
        LoadSonyCalibration(d);
      }
      sDevices.push_back(d);
    } else {
      std::fprintf(stderr, "[steamctl] cannot open %s: %s\n", info->path, SDL_GetError());
    }
  }
  SDL_hid_free_enumeration(list);
  sStats.open = !sDevices.empty();
}

// Reads one interface's queued reports, keeping the newest state; false when it is gone.
bool Drain(Device& d, uint64_t now) {
  uint8_t buffer[128];
  for (int i = 0; i < 256; ++i) {
    const int got = SDL_hid_read(d.handle, buffer, sizeof(buffer));
    if (got < 0) {
      return false;
    }
    if (got == 0) {
      break;
    }
    Sample sample;
    const bool parsed = d.family == Family::Valve ? ParseState(buffer, size_t(got), sample)
                        : IsSony(d.family)        ? ParseSonyState(d.family, buffer, size_t(got), d.scale, sample)
                                                  : ParseSwitchState(d.family, d.miniGamepad, buffer, size_t(got), sample);
    if (!parsed) {
      continue;
    }
    if (d.lastReportNs == 0 || now - d.lastReportNs >= kFreshNs || d.loggedReport != sample.report) {
      std::fprintf(stderr, "[steamctl] receiving reports from %s %04x interface %d (report %02x, %d bytes)\n",
                   Kind(d), d.product, d.interfaceNumber, sample.report, got);
      d.loggedReport = sample.report;
    }
    d.imu.Feed(sample.imuTimestamp, now);
    d.last = sample;
    d.lastReportNs = now;
    ++d.rateCount;
  }
  return true;
}

// Raw gyro and accelerometer reporting is a controller setting, and Steam turns it off when its
// own layout uses no gyro: the reports keep coming with the IMU frozen (a Switch controller's
// zeroed). While the gyro wants this controller it is asked for again (once a second at most, of
// the used interface only, ~2 ms); otherwise (only the back buttons read) it is left as Steam set
// it. Lizard mode (the controller's own mouse and keys) is left to Steam. Sony controllers always
// report their IMU, and are sent nothing.
void WakeImu(Device& d, uint64_t now, bool gyroWanted) {
  const bool imuOff = !d.imu.Live(now);
  const bool canWake = !IsSony(d.family);
  if (imuOff != d.imuOff && gyroWanted) {
    std::fprintf(stderr, !imuOff ? "[steamctl] %s motion sensor on\n"
                         : canWake ? "[steamctl] %s motion sensor is off; turning it on\n"
                                   : "[steamctl] %s motion sensor is off\n",
                 Kind(d));
  }
  d.imuOff = imuOff;
  if (!canWake || !gyroWanted || !imuOff || (d.imuEnableSent && now - d.lastImuEnableNs < kSecondNs)) {
    return;
  }
  d.imuEnableSent = true;
  d.lastImuEnableNs = now;
  if (d.family != Family::Valve) {
    uint8_t report[kSwitchOutputReportSize];
    const size_t size = BuildSwitchImuEnable(d.switchCounter++, d.bluetooth, report);
    SDL_hid_write(d.handle, report, size);
  } else if (d.deck) {
    uint8_t report[kDeckFeatureReportSize];
    BuildDeckImuEnableReport(report);
    SDL_hid_send_feature_report(d.handle, report, sizeof(report));
  } else {
    uint8_t report[kFeatureReportSize];
    BuildImuEnableReport(report);
    SDL_hid_send_feature_report(d.handle, report, sizeof(report));
  }
}
#endif

} // namespace

void Update() {
#if defined(__linux__) && !defined(__ANDROID__)
  const bool gyroWanted = GyroWanted();
  const SteamHidden& hidden = Hidden();
  if ((!hidden.valve && hidden.others.empty()) || (!gyroWanted && !PortOverlay::SteamInputBackButtons())) {
    if (!sDevices.empty()) {
      Close();
    }
    return;
  }
  if (sDevices.empty()) {
    if (sSearched && SDL_hid_device_change_count() == sSearchedChangeCount) {
      return;
    }
    Open();
    if (sDevices.empty()) {
      return;
    }
  }
  // Sony and Nintendo controllers: the Steam pads they may be behind, and whether two Joy-Cons are
  // a pair (whose SL and SR are back buttons, and whose axes are a full pad's).
  std::vector< PadId >& steamPads = sSteamPads;
  std::vector< PadId >& others = sOthers;
  others.clear();
  for (const Device& d : sDevices) {
    if (d.family != Family::Valve) {
      others.push_back(PadId{d.vendor, d.product});
    }
  }
  if (others.empty()) {
    steamPads.clear();
  } else {
    SteamPads(steamPads);
  }
  const bool joyConPair = IsJoyConPair(steamPads.data(), steamPads.size(), others.data(), others.size());
  for (Device& d : sDevices) {
    d.miniGamepad = IsJoyCon(d.family) && !joyConPair;
  }

  const uint64_t now = SDL_GetTicksNS();
  for (Device& d : sDevices) {
    if (!Drain(d, now)) {
      // Unplugged: SDL's device change count moves with it, so the next change looks again.
      std::fprintf(stderr, "[steamctl] %s %04x disconnected\n", Kind(d), d.product);
      Close();
      return;
    }
  }
  if (sRateStartNs == 0) {
    sRateStartNs = now;
  } else if (now - sRateStartNs >= kSecondNs) {
    for (Device& d : sDevices) {
      d.reportsPerSecond = float(double(d.rateCount) * 1e9 / double(now - sRateStartNs));
      d.rateCount = 0;
    }
    sRateStartNs = now;
  }

  // The reporting interface behind Player 1's pad: a Steam Controller first, then a Deck, then a
  // Sony or Nintendo controller (of a Joy-Con pair, the right one's gyro and both's back buttons).
  const PlayerOne p1 = GetPlayerOne();
  bool anyReceiving = false;
  int used = -1;
  uint32_t paddles = 0;
  for (int pass = 0; pass < 2 && used < 0; ++pass) {
    for (size_t i = 0; i < sDevices.size(); ++i) {
      const Device& d = sDevices[i];
      if (d.family != Family::Valve || d.lastReportNs == 0 || now - d.lastReportNs >= kFreshNs) {
        continue;
      }
      anyReceiving = true;
      if (d.deck == (pass == 1) && IsPlayerOnePad(p1.havePad, p1.vendor, p1.product, d.deck)) {
        used = int(i);
        paddles = d.last.paddles;
        break;
      }
    }
  }
  for (size_t i = 0; i < sDevices.size(); ++i) {
    const Device& d = sDevices[i];
    if (d.family == Family::Valve || d.lastReportNs == 0 || now - d.lastReportNs >= kFreshNs) {
      continue;
    }
    anyReceiving = true;
    if (used >= 0 && sDevices[size_t(used)].family == Family::Valve) {
      continue;
    }
    if (p1.steam && IsBehindPlayerOne(PadId{p1.vendor, p1.product}, steamPads.data(), steamPads.size(),
                                      PadId{d.vendor, d.product}, others.data(), others.size())) {
      paddles |= d.last.paddles;
      if (used < 0 || (joyConPair && d.family == Family::JoyConR)) {
        used = int(i);
      }
    }
  }
  if (used != sUsed) {
    if (used >= 0) {
      const Device& d = sDevices[size_t(used)];
      std::fprintf(stderr, "[steamctl] using %s %04x interface %d (Player 1's pad %04x:%04x)\n", Kind(d),
                   d.product, d.interfaceNumber, p1.vendor, p1.product);
    } else if (anyReceiving) {
      std::fprintf(stderr, "[steamctl] Player 1 (%04x:%04x) is another controller; not using these\n",
                   p1.vendor, p1.product);
    } else if (sUsed >= 0) {
      std::fprintf(stderr, "[steamctl] no reports\n");
    }
    sUsed = used;
  }

  sStats.open = true;
  sStats.receiving = used >= 0;
  sStats.notPlayerOne = used < 0 && anyReceiving;
  if (used < 0) {
    sStats.imuOff = false;
    sStats.paddles = 0;
    sStats.reportsPerSecond = 0.f;
    return;
  }
  Device& d = sDevices[size_t(used)];
  WakeImu(d, now, gyroWanted);
  sLast = d.last;
  sStats.imuOff = d.imuOff;
  sStats.paddles = paddles;
  sStats.reportsPerSecond = d.reportsPerSecond;
  std::snprintf(sStats.device, sizeof(sStats.device), "%s %04x", Kind(d), d.product);
  for (int i = 0; i < 3; ++i) {
    sStats.gyroDps[i] = d.last.gyro[i] * (180.f / 3.14159265358979323846f);
  }
#endif
}

bool Motion(float gyro[3], float accel[3]) {
  // A frozen IMU would hold the last rates: no motion until it moves again.
  if (!sStats.receiving || sStats.imuOff) {
    return false;
  }
  std::memcpy(gyro, sLast.gyro, sizeof(sLast.gyro));
  std::memcpy(accel, sLast.accel, sizeof(sLast.accel));
  return true;
}

// (The gyro alone may keep the controllers open: the back buttons only with their setting.)
uint32_t Paddles() { return PortOverlay::SteamInputBackButtons() ? sStats.paddles : 0; }

Stats GetStats() { return sStats; }

} // namespace PortSteamTriton
