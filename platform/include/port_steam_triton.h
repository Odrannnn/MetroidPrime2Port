#pragma once

#include <cstddef>
#include <cstdint>

// The Steam Controller (2026, "Triton") read straight from its HID interface, beside Steam Input.
// Steam Input hands the game a virtual Xbox 360 pad with no gyro and no back buttons; this reads
// the controller's own reports (through SDL's HID API, while Steam keeps running) for its gyro,
// accelerometer and L4/L5/R4/R5. Report layout and scaling: steam-controller-dsu 0.3.4
// (scdsu-core devices/triton.rs) and SDL's driver (src/joystick/hidapi/SDL_hidapi_steam_triton.c),
// checked against reports captured from a puck-connected controller.
//
// The Steam Deck's built-in controller is read the same way: under Steam Input it too reaches the
// game as a virtual pad with no gyro and no back buttons. Its reports use the older Valve layout
// (SDL's src/joystick/hidapi/SDL_hidapi_steamdeck.c and steam/controller_structs.h).
namespace PortSteamTriton {

// The HID client runs on Linux (desktop and the Steam Deck) only, where it is tested; elsewhere
// Update does nothing and the F1 page hides the back-button setting. The report
// parsing below is platform independent (and unit tested everywhere).
#if defined(__linux__) && !defined(__ANDROID__)
constexpr bool kAvailable = true;
#else
constexpr bool kAvailable = false;
#endif

constexpr uint16_t kVendor = 0x28de;
// Wired, Bluetooth, the USB puck and the internal adapter.
constexpr uint16_t kProductWired = 0x1302;
constexpr uint16_t kProductBluetooth = 0x1303;
constexpr uint16_t kProductPuck = 0x1304;
constexpr uint16_t kProductInternalAdapter = 0x1305;
constexpr bool IsTritonProduct(uint16_t product) {
  return product >= kProductWired && product <= kProductInternalAdapter;
}
// The Steam Deck's built-in controller ("Neptune"). It shares its VID/PID with the Deck's lizard
// mode mouse and keyboard interfaces; only the controller interface sends state reports.
constexpr uint16_t kProductDeck = 0x1205;
constexpr bool IsDeckProduct(uint16_t product) { return product == kProductDeck; }
constexpr bool IsSupportedProduct(uint16_t product) {
  return IsTritonProduct(product) || IsDeckProduct(product);
}

// Whether the controller read here is Player 1's, from the physical controller behind Player 1's
// pad (SDL reports a Steam virtual pad's real vendor and product, from Steam's slot info): its
// gyro and back buttons only go to Player 1 then, so a Steam Deck with another controller as
// Player 1 doesn't aim or press for it. Steam's own virtual pad id (no slot info) and no pad on
// Player 1 can't tell, and count as Player 1's, as before; so does a Valve product id not listed
// here, so a new Steam Controller id doesn't lose its gyro.
constexpr uint16_t kProductSteamVirtualGamepad = 0x11ff;
constexpr bool IsPlayerOnePad(bool havePad, uint16_t vendor, uint16_t product, bool deck) {
  if (!havePad || (vendor == kVendor && product == kProductSteamVirtualGamepad)) {
    return true;
  }
  if (vendor != kVendor) {
    return false;
  }
  if (IsDeckProduct(product)) {
    return deck;
  }
  if (IsTritonProduct(product)) {
    return !deck;
  }
  // Another Valve id (a Triton product id not known here): not ruled out.
  return true;
}

// State reports: report id, then (p = data + 1) buttons at p[1], IMU at p[29].
constexpr uint8_t kReportStateUsb = 0x42;
constexpr uint8_t kReportStateBle = 0x45;
constexpr size_t kStateReportMinSize = 1 + 29 + 16;
// The newer state report SDL's driver also reads (TritonMTUNoQuat32TS_t): a trackpad timestamp
// before the pads moves the IMU to p[31], and its timestamp is 16 bits (32 us units).
constexpr uint8_t kReportStateTimestamp = 0x47;
constexpr size_t kStateTimestampReportMinSize = 1 + 31 + 14;
// The settings feature report that turns on raw gyro and accelerometer reporting
// (command 0x87, setting 48 = IMU mode, value 0x18 = raw accel | raw gyro).
constexpr size_t kFeatureReportSize = 64;
void BuildImuEnableReport(uint8_t out[kFeatureReportSize]);

// Steam Deck state report (no report id byte): u16 version 1, type 9, length 64, then
// unPacketNum (4), buttons low word (8), high word (12), accel x/y/z (24), gyro x/y/z (30).
constexpr size_t kDeckStateReportSize = 64;
constexpr uint8_t kDeckReportType = 0x09;
// The same IMU-mode setting as the Triton one, behind report id 0, 64 bytes after it (as SDL's
// Deck driver sends its settings).
constexpr size_t kDeckFeatureReportSize = 65;
void BuildDeckImuEnableReport(uint8_t out[kDeckFeatureReportSize]);
// The Deck's back buttons: L4 and R4 in the high word, L5 and R5 in the low word.
constexpr uint32_t kDeckMaskL5 = 0x00008000;
constexpr uint32_t kDeckMaskR5 = 0x00010000;
constexpr uint32_t kDeckMaskL4 = 0x00000200;
constexpr uint32_t kDeckMaskR4 = 0x00000400;

// The back buttons in the report's button word.
constexpr uint32_t kMaskL4 = 0x00020000;
constexpr uint32_t kMaskL5 = 0x00040000;
constexpr uint32_t kMaskR4 = 0x00000080;
constexpr uint32_t kMaskR5 = 0x00000100;
// SDL_GamepadButton numbers for them, the bits the input engine's pad buttons use (SDL's own
// Steam Controller mapping: L4 left paddle 1, R4 right paddle 1, L5 left paddle 2, R5 right
// paddle 2). The client static_asserts these against SDL.
constexpr int kRightPaddle1 = 16;
constexpr int kLeftPaddle1 = 17;
constexpr int kRightPaddle2 = 18;
constexpr int kLeftPaddle2 = 19;
// The back buttons held in a report's button word, as SDL_GamepadButton bits.
uint32_t PaddleBits(uint32_t buttons);
// The same for a Steam Deck report's two button words.
uint32_t DeckPaddleBits(uint32_t buttonsLow, uint32_t buttonsHigh);

// One state report, in SDL's sensor axes (x right, y up, z toward the player), gyro in rad/s and
// accelerometer in m/s^2, as SDL's driver reports them.
struct Sample {
  // A Steam Deck report (else a Steam Controller one).
  bool deck = false;
  // The report id (a Deck's report type).
  uint8_t report = 0;
  // The raw button word (a Deck's low word).
  uint32_t buttons = 0;
  // The back buttons held, as SDL_GamepadButton bits.
  uint32_t paddles = 0;
  // The IMU's own timestamp; the Deck sends none, so for it a hash of the raw IMU block, which
  // moves with the sensor's noise while it runs and stops when it is off.
  uint32_t imuTimestamp = 0;
  float gyro[3] = {};
  float accel[3] = {};
};

// Parses a state report (Steam Controller USB or BLE id, long enough for the IMU block, or a Steam
// Deck state report). Other reports (battery, settings replies, the Deck's mouse and keyboard
// interfaces) return false.
bool ParseState(const uint8_t* data, size_t size, Sample& out);

// Whether the IMU is live. Steam turns the controller's IMU off when its own layout has no gyro
// in use; the state reports keep coming with the IMU block frozen (timestamp and values
// unchanged, not zero), so liveness is the IMU timestamp moving.
struct ImuWatch {
  // How long an unchanged IMU timestamp may last (the IMU runs at a few hundred Hz).
  static constexpr uint64_t kStaleNs = 250000000ull;
  uint32_t timestamp = 0;
  uint64_t changedNs = 0;
  bool seen = false;

  void Feed(uint32_t imuTimestamp, uint64_t nowNs) {
    if (!seen || imuTimestamp != timestamp) {
      seen = true;
      timestamp = imuTimestamp;
      changedNs = nowNs;
    }
  }
  bool Live(uint64_t nowNs) const { return seen && nowNs - changedNs < kStaleNs; }
};

// The client (SDL's HID API; Linux only, see kAvailable). Update once per input poll, before the
// readers below: it opens the controller while wanted (Gyro source Auto or Controller with gyro in
// use, or the Steam Input back buttons setting on), drains its reports and keeps the newest. The
// motion sensor is only turned back on while the gyro is wanted.
void Update();
// The newest gyro and accelerometer, while reports are arriving.
bool Motion(float gyro[3], float accel[3]);
// The back buttons held, as SDL_GamepadButton bits (0 while no reports arrive).
uint32_t Paddles();

// For the F1 readout.
struct Stats {
  bool open = false;
  bool receiving = false;
  // Reports arrive but the IMU is off (Steam turned it off; it is asked to turn it back on).
  bool imuOff = false;
  // Player 1 is another controller: this one's gyro and back buttons are not used.
  bool notPlayerOne = false;
  char device[48] = {};
  float reportsPerSecond = 0.f;
  float gyroDps[3] = {};
  uint32_t paddles = 0;
};
Stats GetStats();

} // namespace PortSteamTriton
