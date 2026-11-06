// The Steam Controller (2026) and Steam Deck HID client: reads the controllers' state reports
// beside Steam Input, for gyro (Gyro source Controller or Auto) and the back buttons. SDL is left
// out of these controllers (Aurora keeps Valve on Steam's ignore list): its drivers manage lizard
// mode and the IMU against Steam. This only reads, and asks for the IMU when Steam turned it off.
// Called only from the input poll (port_events.cpp), so it needs no locking. Nothing here may
// block the poll: controllers are looked for only when SDL sees a HID device come or go, and the
// IMU setting is sent only to the interface that answers (a feature report to an empty puck slot
// waits ~150 ms).
//
// Several can report at once (a Deck's built-in controller beside a Steam Controller on Bluetooth
// or the puck), so each interface keeps its own newest report, and the one behind Player 1's pad
// is used (IsPlayerOnePad; a Steam Controller before the Deck when Player 1's can't be told).

#include "port_steam_triton.h"

#include "port_overlay.h"

#include <dolphin/pad.h>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hidapi.h>
#include <SDL3/SDL_timer.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
  uint16_t product = 0;
  int interfaceNumber = -1;
  bool deck = false;
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
uint64_t sRateStartNs = 0;

const char* Kind(uint16_t product) {
  return IsDeckProduct(product)              ? "steam deck"
         : product == kProductPuck           ? "puck"
         : product == kProductBluetooth      ? "bluetooth"
         : product == kProductInternalAdapter ? "adapter"
                                             : "usb";
}

// The real controller behind Player 1's pad (SDL reports it for a Steam virtual pad).
struct PlayerOne {
  bool havePad = false;
  uint16_t vendor = 0;
  uint16_t product = 0;
};
PlayerOne GetPlayerOne() {
  PlayerOne p;
  const s32 index = PADGetIndexForPort(PAD_CHAN0);
  if (SDL_Gamepad* pad = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr) {
    p.havePad = true;
    p.vendor = SDL_GetGamepadVendor(pad);
    p.product = SDL_GetGamepadProduct(pad);
  }
  return p;
}

// Whether Steam hides Valve's controllers from SDL: a run launched from Steam (Steam Input) sets
// SDL_GAMECONTROLLER_IGNORE_DEVICES with them in it. Without it SDL drives a Steam Controller or a
// Deck itself, gyro and back buttons included, and this reader would only duplicate it.
bool SteamHidesValve() {
  static const bool hides = [] {
    const char* list = std::getenv("SDL_GAMECONTROLLER_IGNORE_DEVICES");
    if (list == nullptr) {
      return false;
    }
    std::string lower(list);
    for (char& c : lower) {
      c = static_cast< char >(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    const bool found = lower.find("0x28de/") != std::string::npos;
    std::fprintf(stderr, "[steamctl] %s\n",
                 found ? "Steam Input hides the Steam Controller / Deck from SDL: reading them directly"
                       : "Steam Input does not hide the Steam Controller / Deck: SDL reads them");
    return found;
  }();
  return hides;
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

void Open() {
  sSearched = true;
  sSearchedChangeCount = SDL_hid_device_change_count();
  SDL_hid_device_info* list = SDL_hid_enumerate(kVendor, 0);
  for (SDL_hid_device_info* info = list; info != nullptr; info = info->next) {
    // The vendor-defined interface (usage page 0xFF00 and up) carries the state reports. The
    // Deck's controller is interface 2, beside its lizard mode mouse and keyboard; that is checked
    // too in case a platform reports no usage page.
    const bool deck = IsDeckProduct(info->product_id);
    if (!IsSupportedProduct(info->product_id) ||
        (info->usage_page < 0xFF00 && !(deck && info->interface_number == 2))) {
      continue;
    }
    if (SDL_hid_device* h = SDL_hid_open_path(info->path)) {
      SDL_hid_set_nonblocking(h, 1);
      Device d;
      d.handle = h;
      d.product = info->product_id;
      d.interfaceNumber = info->interface_number;
      d.deck = deck;
      sDevices.push_back(d);
      std::fprintf(stderr, "[steamctl] opened %s %04x interface %d (usage page %04x)\n",
                   Kind(info->product_id), info->product_id, info->interface_number, info->usage_page);
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
    if (!ParseState(buffer, size_t(got), sample)) {
      continue;
    }
    if (d.lastReportNs == 0 || now - d.lastReportNs >= kFreshNs || d.loggedReport != sample.report) {
      std::fprintf(stderr, "[steamctl] receiving reports from %s %04x interface %d (report %02x, %d bytes)\n",
                   Kind(d.product), d.product, d.interfaceNumber, sample.report, got);
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
// own layout uses no gyro: the reports keep coming with the IMU frozen. While the gyro wants this
// controller it is asked for again (once a second at most, of the used interface only, ~2 ms);
// otherwise (only the back buttons read) it is left as Steam set it. Lizard mode (the
// controller's own mouse and keys) is left to Steam.
void WakeImu(Device& d, uint64_t now, bool gyroWanted) {
  const bool imuOff = !d.imu.Live(now);
  if (imuOff != d.imuOff && gyroWanted) {
    std::fprintf(stderr, imuOff ? "[steamctl] %s motion sensor is off; turning it on\n"
                                : "[steamctl] %s motion sensor on\n",
                 Kind(d.product));
  }
  d.imuOff = imuOff;
  if (!gyroWanted || !imuOff || (d.imuEnableSent && now - d.lastImuEnableNs < kSecondNs)) {
    return;
  }
  d.imuEnableSent = true;
  d.lastImuEnableNs = now;
  if (d.deck) {
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
  if (!SteamHidesValve() || (!gyroWanted && !PortOverlay::SteamInputBackButtons())) {
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
  const uint64_t now = SDL_GetTicksNS();
  for (Device& d : sDevices) {
    if (!Drain(d, now)) {
      // Unplugged: SDL's device change count moves with it, so the next change looks again.
      std::fprintf(stderr, "[steamctl] %s %04x disconnected\n", Kind(d.product), d.product);
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

  // The reporting interface behind Player 1's pad: a Steam Controller first, then a Deck.
  const PlayerOne p1 = GetPlayerOne();
  bool anyReceiving = false;
  int used = -1;
  for (int pass = 0; pass < 2 && used < 0; ++pass) {
    for (size_t i = 0; i < sDevices.size(); ++i) {
      const Device& d = sDevices[i];
      if (d.lastReportNs == 0 || now - d.lastReportNs >= kFreshNs) {
        continue;
      }
      anyReceiving = true;
      if (d.deck == (pass == 1) && IsPlayerOnePad(p1.havePad, p1.vendor, p1.product, d.deck)) {
        used = int(i);
        break;
      }
    }
  }
  if (used != sUsed) {
    if (used >= 0) {
      const Device& d = sDevices[size_t(used)];
      std::fprintf(stderr, "[steamctl] using %s %04x interface %d (Player 1's pad %04x:%04x)\n",
                   Kind(d.product), d.product, d.interfaceNumber, p1.vendor, p1.product);
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
  sStats.paddles = d.last.paddles;
  sStats.reportsPerSecond = d.reportsPerSecond;
  std::snprintf(sStats.device, sizeof(sStats.device), "%s %04x", Kind(d.product), d.product);
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

uint32_t Paddles() { return sStats.paddles; }

Stats GetStats() { return sStats; }

} // namespace PortSteamTriton
