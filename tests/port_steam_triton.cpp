#include "port_steam_triton.h"
#include "port_gyro_motion.h"
#include "port_test.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kG = 9.80665f;
constexpr float kRadPerCount = 2000.f / 32768.f * (kPi / 180.f);
constexpr float kAccelPerCount = 2.f / 32768.f * kG;

// State reports captured from a 2026 Steam Controller on its USB puck (28de:1304), with Steam
// running and Steam Input on, through SDL_hid.
// Lying still on a desk, face up.
const uint8_t kStill[] = {
    0x42, 0xd2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf4, 0x02, 0xb1, 0x03,
    0xdf, 0x01, 0x05, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x05, 0x61, 0xb7, 0x3d, 0xef, 0xff, 0xe5, 0xf4, 0x09, 0x3f, 0x01, 0x00,
    0x01, 0x00, 0x05, 0x00, 0x18, 0x78, 0x75, 0xf5, 0x5a, 0xfc, 0xd7, 0x2a,
};
// L4 held: buttons 0x00020000, accel (1875, 8734, 13640), gyro (-80, 58, 53).
const uint8_t kL4[] = {
    0x42, 0x0b, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x65, 0x02, 0x6d, 0x03,
    0xc8, 0x01, 0xc0, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xd2, 0x0c, 0x83, 0x40, 0x53, 0x07, 0x1e, 0x22, 0x48, 0x35, 0xb0, 0xff,
    0x3a, 0x00, 0x35, 0x00, 0xaf, 0x74, 0x07, 0x25, 0x05, 0x05, 0x07, 0x25,
};
// R5 held: buttons 0x00000100.
const uint8_t kR5[] = {
    0x42, 0xd3, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x03, 0x38, 0x04,
    0xac, 0x02, 0xea, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x92, 0xcd, 0xd8, 0x40, 0xe4, 0x02, 0x20, 0x27, 0xed, 0x33, 0x66, 0xff,
    0x94, 0xff, 0x77, 0x00, 0x17, 0x74, 0xe7, 0x26, 0x05, 0x0a, 0xef, 0x23,
};
// Mid-turn: accel (63, 373, 10195), gyro (2352, -1841, 278).
const uint8_t kTurning[] = {
    0x42, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf4, 0x02, 0xb1, 0x03,
    0x0d, 0x02, 0x77, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xe8, 0x65, 0x76, 0x40, 0x3f, 0x00, 0x75, 0x01, 0xd3, 0x27, 0x30, 0x09,
    0xcf, 0xf8, 0x16, 0x01, 0xc7, 0x78, 0xcd, 0x0e, 0x5a, 0x04, 0x77, 0x27,
};

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

} // namespace

int main() {
  using namespace PortSteamTriton;

  // Lying still face up: about +1 g on SDL's y, as the gyro filter's tests expect of a flat pad.
  {
    Sample s;
    REQUIRE_MSG(ParseState(kStill, sizeof(kStill), s), "still report parses");
    CHECK_EQ(s.buttons, 0u);
    CHECK_MSG(std::fabs(s.accel[1] - kG) < 0.05f * kG, "face up is +1 g on SDL's y");
    CHECK_MSG(std::fabs(s.accel[0]) < 0.05f * kG && std::fabs(s.accel[2]) < 0.2f * kG,
              "little on x and z");
    CHECK_MSG(std::fabs(s.gyro[0]) < 0.01f && std::fabs(s.gyro[1]) < 0.01f &&
                  std::fabs(s.gyro[2]) < 0.01f,
              "still");
  }

  // Back buttons, as SDL paddles: L4 left paddle 1, R5 right paddle 2.
  {
    Sample s;
    REQUIRE(ParseState(kL4, sizeof(kL4), s));
    CHECK_EQ(s.buttons, kMaskL4);
    CHECK_EQ(PaddleBits(s.buttons), 1u << kLeftPaddle1);
    REQUIRE(ParseState(kR5, sizeof(kR5), s));
    CHECK_EQ(s.buttons, kMaskR5);
    CHECK_EQ(PaddleBits(s.buttons), 1u << kRightPaddle2);
    CHECK_EQ(PaddleBits(kMaskR4), 1u << kRightPaddle1);
    CHECK_EQ(PaddleBits(kMaskL5), 1u << kLeftPaddle2);
    CHECK_EQ(PaddleBits(kMaskL4 | kMaskL5 | kMaskR4 | kMaskR5), 0xFu << 16);
    CHECK_EQ(PaddleBits(0xFFFFFFFFu & ~(kMaskL4 | kMaskL5 | kMaskR4 | kMaskR5)), 0u);
  }

  // Axes and scaling, as SDL's driver: gyro (x, z, -y), accelerometer (x, z, -y).
  {
    Sample s;
    REQUIRE(ParseState(kTurning, sizeof(kTurning), s));
    CHECK(Near(s.gyro[0], 2352 * kRadPerCount));
    CHECK(Near(s.gyro[1], 278 * kRadPerCount));
    CHECK(Near(s.gyro[2], 1841 * kRadPerCount));
    CHECK(Near(s.accel[0], 63 * kAccelPerCount));
    CHECK(Near(s.accel[1], 10195 * kAccelPerCount));
    CHECK(Near(s.accel[2], -373 * kAccelPerCount));

    // The same reading through steam-controller-dsu's conversion (triton.rs: pitch gx, yaw -gz,
    // roll gy in deg/s; accel (-ax, -az, ay) in g), taken to SDL's axes as Dolphin's DSU backend
    // does (gyro (pitch, -yaw, -roll), accelerometer negated): the two agree.
    const float dsuGyro[3] = {2352 / 16.384f, -278 / 16.384f, -1841 / 16.384f};
    const float dsuAccel[3] = {-63 / 16384.f, -10195 / 16384.f, 373 / 16384.f};
    constexpr float kRadPerDeg = 3.14159265358979323846f / 180.f;
    constexpr float kStandardGravity = 9.80665f;
    const float gyro[3] = {dsuGyro[0] * kRadPerDeg, -dsuGyro[1] * kRadPerDeg, -dsuGyro[2] * kRadPerDeg};
    for (int i = 0; i < 3; ++i) {
      CHECK_MSG(Near(gyro[i], s.gyro[i]), "gyro matches steam-controller-dsu");
      CHECK_MSG(std::fabs(-dsuAccel[i] * kStandardGravity - s.accel[i]) < 1e-3f,
                "accelerometer matches steam-controller-dsu");
    }
  }

  // BLE reports share the layout; other reports and short ones are refused.
  {
    Sample s;
    uint8_t ble[sizeof(kL4)];
    std::memcpy(ble, kL4, sizeof(ble));
    ble[0] = kReportStateBle;
    CHECK_MSG(ParseState(ble, sizeof(ble), s) && s.buttons == kMaskL4, "BLE state report");
    // Seen from the puck: a 45-byte 0x42, and 0x7b / 0x43 reports.
    CHECK_MSG(!ParseState(kL4, kStateReportMinSize - 1, s), "short state report");
    uint8_t other[sizeof(kL4)];
    std::memcpy(other, kL4, sizeof(other));
    other[0] = 0x7b;
    CHECK_MSG(!ParseState(other, sizeof(other), s), "other report id");
    CHECK_MSG(!ParseState(nullptr, 0, s), "no data");
  }

  // The newer timestamped report (0x47, SDL's TritonMTUNoQuat32TS_t): same buttons, a trackpad
  // timestamp before the pads, and the IMU at p[31] with a 16-bit timestamp.
  {
    uint8_t r[kStateTimestampReportMinSize] = {};
    r[0] = kReportStateTimestamp;
    uint8_t* p = r + 1;
    p[1] = uint8_t(kMaskR4);
    p[2] = uint8_t(kMaskR4 >> 8);
    auto put16 = [&](size_t at, int v) {
      p[at] = uint8_t(v & 0xff);
      p[at + 1] = uint8_t((v >> 8) & 0xff);
    };
    put16(31, 0x1234);
    put16(33, 63);
    put16(35, 373);
    put16(37, 10195);
    put16(39, 2352);
    put16(41, -1841);
    put16(43, 278);
    Sample s;
    REQUIRE_MSG(ParseState(r, sizeof(r), s), "0x47 report parses");
    CHECK_EQ(s.report, kReportStateTimestamp);
    CHECK_EQ(s.paddles, 1u << kRightPaddle1);
    CHECK_EQ(s.imuTimestamp, 0x1234u);
    Sample t;
    REQUIRE(ParseState(kTurning, sizeof(kTurning), t));
    for (int i = 0; i < 3; ++i) {
      CHECK_MSG(Near(s.gyro[i], t.gyro[i]), "0x47 gyro at its own offset");
      CHECK_MSG(Near(s.accel[i], t.accel[i]), "0x47 accel at its own offset");
    }
    CHECK_MSG(!ParseState(r, sizeof(r) - 1, s), "short 0x47 report");
  }

  // The IMU enable request, as steam-controller-dsu sends it.
  {
    uint8_t report[kFeatureReportSize];
    BuildImuEnableReport(report);
    const uint8_t head[] = {0x01, 0x87, 3, 48, 0x18, 0x00};
    CHECK(std::memcmp(report, head, sizeof(head)) == 0);
    bool restZero = true;
    for (size_t i = sizeof(head); i < sizeof(report); ++i)
      restZero = restZero && report[i] == 0;
    CHECK(restZero);
  }

  // Through the gyro filter (no accelerometer, raw rates): turning right (clockwise from above,
  // SDL's -y) is positive yaw.
  {
    PortGyro::Motion m;
    float pitch = 0.f;
    float yaw = 0.f;
    Sample s;
    REQUIRE(ParseState(kTurning, sizeof(kTurning), s));
    m.Process(s.gyro, nullptr, 1.f / 60.f, PortGyro::kPlayer, false, pitch, yaw);
    CHECK_MSG(Near(pitch, s.gyro[0]) && Near(yaw, -s.gyro[1]),
              "rates reach the filter in SDL's axes");
  }

  // A frozen IMU (Steam turned it off): reports keep coming with the same IMU timestamp. Two
  // captures of that: 263 reports in a second, timestamp 2537738953 throughout.
  {
    constexpr uint64_t kMs = 1000000ull;
    ImuWatch w;
    CHECK_MSG(!w.Live(0), "nothing seen yet");
    w.Feed(2537738953u, 1000 * kMs);
    CHECK_MSG(w.Live(1000 * kMs), "first report counts");
    for (uint64_t t = 1004; t < 1300; t += 4) w.Feed(2537738953u, t * kMs);
    CHECK_MSG(!w.Live(1300 * kMs), "frozen timestamp goes stale");
    w.Feed(2537742953u, 1304 * kMs);
    CHECK_MSG(w.Live(1304 * kMs), "a moving timestamp is live again");
    // Live while it keeps moving, including across the 32-bit wrap.
    w.Feed(0xFFFFFF00u, 1308 * kMs);
    w.Feed(0x00000100u, 1312 * kMs);
    CHECK_MSG(w.Live(1500 * kMs), "wrap is a change");
  }

  CHECK_MSG(IsTritonProduct(kProductPuck) && IsTritonProduct(kProductWired) &&
                !IsTritonProduct(0x1205),
            "Triton products only (not the Steam Deck)");
  CHECK_MSG(IsDeckProduct(0x1205) && IsSupportedProduct(0x1205) && IsSupportedProduct(kProductPuck) &&
                !IsSupportedProduct(0x1142),
            "the Steam Deck is supported beside the Triton products");

  // Steam Deck state reports, laid out as SDL's SteamDeckStatePacket_t (no capture from a Deck
  // yet): header 01 00 09 40, then packet number, button words, pads, accel, gyro.
  {
    auto put16 = [](uint8_t* r, size_t at, int v) {
      r[at] = uint8_t(v & 0xff);
      r[at + 1] = uint8_t((v >> 8) & 0xff);
    };
    auto put32 = [](uint8_t* r, size_t at, uint32_t v) {
      for (int i = 0; i < 4; ++i) r[at + i] = uint8_t(v >> (8 * i));
    };
    uint8_t deck[kDeckStateReportSize] = {0x01, 0x00, kDeckReportType, 64};
    put32(deck, 4, 1234);
    put32(deck, 8, kDeckMaskL5);
    put32(deck, 12, kDeckMaskR4);
    put16(deck, 24, 63);
    put16(deck, 26, 373);
    put16(deck, 28, 10195);
    put16(deck, 30, 2352);
    put16(deck, 32, -1841);
    put16(deck, 34, 278);
    Sample s;
    REQUIRE_MSG(ParseState(deck, sizeof(deck), s), "deck report parses");
    CHECK(s.deck);
    CHECK_EQ(s.paddles, (1u << kLeftPaddle2) | (1u << kRightPaddle1));
    CHECK_EQ(DeckPaddleBits(0, kDeckMaskL4), 1u << kLeftPaddle1);
    CHECK_EQ(DeckPaddleBits(kDeckMaskR5, 0), 1u << kRightPaddle2);
    CHECK_EQ(DeckPaddleBits(~(kDeckMaskL5 | kDeckMaskR5), ~(kDeckMaskL4 | kDeckMaskR4)), 0u);
    // The same axes and scale as the Steam Controller reading of the same counts.
    Sample t;
    REQUIRE(ParseState(kTurning, sizeof(kTurning), t));
    for (int i = 0; i < 3; ++i) {
      CHECK_MSG(Near(s.gyro[i], t.gyro[i]), "deck gyro axes match SDL's driver");
      CHECK_MSG(Near(s.accel[i], t.accel[i]), "deck accel axes match SDL's driver");
    }
    // The IMU "timestamp" moves with the IMU block, and only with it.
    const uint32_t first = s.imuTimestamp;
    put32(deck, 4, 1235);
    put32(deck, 8, 0);
    put32(deck, 12, 0);
    REQUIRE(ParseState(deck, sizeof(deck), s));
    CHECK_MSG(s.imuTimestamp == first && s.paddles == 0, "same IMU block, same stamp");
    put16(deck, 30, 2353);
    REQUIRE(ParseState(deck, sizeof(deck), s));
    CHECK_MSG(s.imuTimestamp != first, "a gyro change moves the stamp");
    // Short, other types, and the Steam Controller's own reports are not read as a Deck's.
    CHECK_MSG(!ParseState(deck, sizeof(deck) - 1, s), "short deck report");
    deck[2] = 0x01;
    CHECK_MSG(!ParseState(deck, sizeof(deck), s), "other Valve report type");
    REQUIRE(ParseState(kL4, sizeof(kL4), s));
    CHECK_MSG(!s.deck && s.paddles == (1u << kLeftPaddle1), "triton report is not a deck one");
  }

  // Player 1's pad decides whose gyro and back buttons are used (Steam reports the real controller).
  {
    CHECK_MSG(IsPlayerOnePad(false, 0, 0, true), "no pad on Player 1: used");
    CHECK_MSG(IsPlayerOnePad(true, kVendor, kProductSteamVirtualGamepad, true),
              "Steam's virtual pad without slot info: can't tell, used");
    CHECK_MSG(IsPlayerOnePad(true, kVendor, kProductDeck, true), "the Deck as Player 1 reading the Deck");
    CHECK_MSG(!IsPlayerOnePad(true, 0x045e, 0x0b13, true), "an Xbox pad as Player 1: the Deck isn't used");
    CHECK_MSG(!IsPlayerOnePad(true, 0x054c, 0x0ce6, false), "a DualSense as Player 1: the Steam Controller isn't used");
    CHECK_MSG(!IsPlayerOnePad(true, kVendor, kProductPuck, true), "a Steam Controller as Player 1: the Deck isn't used");
    CHECK_MSG(!IsPlayerOnePad(true, kVendor, kProductDeck, false), "the Deck as Player 1: the Steam Controller isn't used");
    CHECK_MSG(IsPlayerOnePad(true, kVendor, kProductPuck, false) && IsPlayerOnePad(true, kVendor, kProductWired, false),
              "the Steam Controller as Player 1 reading it");
  }

  // The Deck's IMU enable request: report id 0, then the same setting.
  {
    uint8_t report[kDeckFeatureReportSize];
    BuildDeckImuEnableReport(report);
    const uint8_t head[] = {0x00, 0x87, 3, 48, 0x18, 0x00};
    CHECK(std::memcmp(report, head, sizeof(head)) == 0);
  }

  // Sony and Nintendo controllers: which are read, their reports and calibration.
  auto put16 = [](uint8_t* r, size_t at, int v) {
    r[at] = uint8_t(v & 0xff);
    r[at + 1] = uint8_t((v >> 8) & 0xff);
  };
  auto put32 = [](uint8_t* r, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) r[at + i] = uint8_t(v >> (8 * i));
  };
  // A Bluetooth report's CRC (zlib's CRC-32 over 0xA1 and the report), as SDL's VerifyCRC.
  auto sealCrc = [&](uint8_t* r, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    auto feed = [&](uint8_t b) {
      crc ^= b;
      for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    };
    feed(0xA1);
    for (size_t i = 0; i + 4 < size; ++i) feed(r[i]);
    put32(r, size - 4, ~crc);
  };
  constexpr float kDegPerCountSony = 1.f / 16.f;
  {
    Family f = Family::Valve;
    CHECK(OtherFamily(kVendorSony, kProductDualSense, f) && f == Family::DualSense);
    CHECK(OtherFamily(kVendorSony, kProductDualSenseEdge, f) && f == Family::DualSenseEdge);
    CHECK(OtherFamily(kVendorSony, kProductDualShock4v2, f) && f == Family::DualShock4);
    CHECK(OtherFamily(kVendorNintendo, kProductSwitchPro, f) && f == Family::SwitchPro);
    CHECK(OtherFamily(kVendorNintendo, kProductJoyConR, f) && f == Family::JoyConR);
    CHECK_MSG(!OtherFamily(kVendorNintendo, 0x2069, f) && !OtherFamily(kVendorNintendo, kProductJoyConPair, f),
              "not the Switch 2 Pro Controller, nor SDL's virtual Joy-Con pair id");
    CHECK_MSG(!OtherFamily(kVendor, kProductPuck, f) && !OtherFamily(0x045e, 0x0b13, f), "Valve and others aren't");
  }
  // DualSense (Edge) USB state: gyro at state 15, accel at 21, sensor timestamp at 27, Edge buttons at 9.
  {
    uint8_t r[64] = {0x01};
    uint8_t* st = r + 1;
    put16(st, 15, 1600);
    put16(st, 17, -320);
    put16(st, 19, 16);
    put16(st, 21, 0);
    put16(st, 23, 8192);
    put16(st, 25, -4096);
    put32(st, 27, 777);
    st[9] = kEdgeRightPaddle | kEdgeLeftFn;
    const ImuScale nominal = SonyDefaultScale();
    Sample s;
    REQUIRE_MSG(ParseSonyState(Family::DualSenseEdge, r, sizeof(r), nominal, s), "DualSense USB report");
    CHECK(Near(s.gyro[0], 100.f * kPi / 180.f) && Near(s.gyro[1], -20.f * kPi / 180.f) &&
          Near(s.gyro[2], 1.f * kPi / 180.f));
    CHECK(Near(s.accel[1], kG) && Near(s.accel[2], -kG / 2.f) && Near(s.accel[0], 0.f));
    CHECK_EQ(s.imuTimestamp, 777u);
    CHECK_EQ(s.paddles, (1u << kRightPaddle1) | (1u << kLeftPaddle2));
    REQUIRE(ParseSonyState(Family::DualSense, r, sizeof(r), nominal, s));
    CHECK_MSG(s.paddles == 0, "a plain DualSense has no back buttons");
    CHECK_MSG(!ParseSonyState(Family::DualSense, r, 10, nominal, s), "the simple report has no IMU");
    CHECK_MSG(!ParseSonyState(Family::SwitchPro, r, sizeof(r), nominal, s), "not a Sony family");
    // Bluetooth 0x31: state at 2, CRC over the report.
    uint8_t bt[78] = {0x31, 0x00};
    std::memcpy(bt + 2, st, 31);
    sealCrc(bt, sizeof(bt));
    Sample b;
    REQUIRE_MSG(ParseSonyState(Family::DualSenseEdge, bt, sizeof(bt), nominal, b), "DualSense Bluetooth report");
    CHECK(Near(b.gyro[0], s.gyro[0]) && b.imuTimestamp == 777u && b.paddles != 0);
    bt[20] ^= 1;
    CHECK_MSG(!ParseSonyState(Family::DualSense, bt, sizeof(bt), nominal, b), "a bad CRC is dropped");
  }
  // DualShock 4: USB 0x01 (state at 1, gyro at 12, accel at 18, u16 timestamp at 9), Bluetooth
  // 0x11 (state at 3, CRC over 78 bytes).
  {
    uint8_t usb[64] = {0x01};
    put16(usb + 1, 9, 0xBEEF);
    put16(usb + 1, 12, -160);
    put16(usb + 1, 18, 8192);
    Sample s;
    REQUIRE(ParseSonyState(Family::DualShock4, usb, sizeof(usb), SonyDefaultScale(), s));
    CHECK(Near(s.gyro[0], -10.f * kPi / 180.f) && Near(s.accel[0], kG) && s.imuTimestamp == 0xBEEFu);
    usb[31] = 0x04;
    CHECK_MSG(!ParseSonyState(Family::DualShock4, usb, sizeof(usb), SonyDefaultScale(), s),
              "no controller behind the wireless adapter");
    uint8_t bt[78] = {0x11, 0xC0, 0x00};
    put16(bt + 3, 12, -160);
    sealCrc(bt, sizeof(bt));
    REQUIRE(ParseSonyState(Family::DualShock4, bt, sizeof(bt), SonyDefaultScale(), s));
    CHECK(Near(s.gyro[0], -10.f * kPi / 180.f));
    bt[1] = 0x00;
    sealCrc(bt, sizeof(bt));
    CHECK_MSG(!ParseSonyState(Family::DualShock4, bt, sizeof(bt), SonyDefaultScale(), s), "no HID data in it");
  }
  // Calibration: nominal extents give the nominal scale; a bias moves the zero; SDL's sanity limits.
  {
    uint8_t c[41] = {kDualSenseCalibrationReport};
    put16(c, 1, 10);
    put16(c, 3, -20);
    put16(c, 5, 0);
    for (int i = 0; i < 3; ++i) {
      put16(c, 7 + 4 * i, 8640);
      put16(c, 9 + 4 * i, -8640);
      put16(c, 23 + 4 * i, 8192 + 100);
      put16(c, 25 + 4 * i, -8192 + 100);
    }
    put16(c, 19, 540);
    put16(c, 21, 540);
    ImuScale cal;
    REQUIRE_MSG(ParseDualSenseCalibration(c, sizeof(c), cal), "DualSense calibration");
    const ImuScale nominal = SonyDefaultScale();
    for (int i = 0; i < 6; ++i) {
      CHECK_MSG(Near(cal.scale[i], nominal.scale[i]), "nominal extents, nominal scale");
    }
    CHECK(cal.bias[0] == 10.f && cal.bias[1] == -20.f && cal.bias[3] == 100.f);
    ImuScale kept = nominal;
    put16(c, 1, 2000);
    CHECK_MSG(!ParseDualSenseCalibration(c, sizeof(c), kept) && kept.bias[0] == 0.f, "a bias over 1024 is rejected");
    put16(c, 1, 10);
    put16(c, 7, 30000);
    CHECK_MSG(!ParseDualSenseCalibration(c, sizeof(c), kept), "a sensitivity off by half is rejected");
    CHECK_MSG(!ParseDualSenseCalibration(c, 34, kept), "short report");

    // DualShock 4: USB lists plus, minus per axis; Bluetooth all plus, then all minus.
    uint8_t u[41] = {kDualShock4CalibrationReportUsb};
    uint8_t b[41] = {kDualShock4CalibrationReportBluetooth};
    for (uint8_t* r : {u, b}) {
      put16(r, 1, 10);
      put16(r, 19, 540);
      put16(r, 21, 540);
      for (int i = 0; i < 3; ++i) {
        put16(r, 23 + 4 * i, 8192);
        put16(r, 25 + 4 * i, -8192);
      }
    }
    for (int i = 0; i < 3; ++i) {
      const int bias = i == 0 ? 10 : 0;
      put16(u, 7 + 4 * i, bias + 8640);
      put16(u, 9 + 4 * i, bias - 8640);
      put16(b, 7 + 2 * i, bias + 8640);
      put16(b, 13 + 2 * i, bias - 8640);
    }
    ImuScale cu, cb;
    REQUIRE(ParseDualShock4Calibration(u, sizeof(u), false, cu));
    REQUIRE(ParseDualShock4Calibration(b, sizeof(b), true, cb));
    for (int i = 0; i < 6; ++i) {
      CHECK_MSG(Near(cu.scale[i], nominal.scale[i]) && Near(cb.scale[i], nominal.scale[i]), "DS4 nominal scale");
    }
    CHECK(cu.bias[0] == 10.f && cb.bias[0] == 10.f);
    CHECK_MSG(!ParseDualShock4Calibration(u, sizeof(u), true, cu), "the Bluetooth id over Bluetooth only");
    uint8_t zero[41] = {kDualShock4CalibrationReportUsb};
    CHECK_MSG(!ParseDualShock4Calibration(zero, sizeof(zero), false, cu), "an all-zero report is rejected");
  }
  // Switch full report: three IMU samples averaged, SDL's axis shuffle, Joy-Con flips and paddles.
  {
    uint8_t r[kSwitchFullReportSize] = {kSwitchReportFull};
    for (int k = 0; k < 3; ++k) {
      uint8_t* imu = r + 13 + 12 * k;
      put16(imu, 0, 4096);      // accel x: 1 g
      put16(imu, 2, 0);
      put16(imu, 4, -2048 * k); // accel z: mean -1/2 g
      put16(imu, 6, 1428);      // gyro x ~100 deg/s
      put16(imu, 8, 0);
      put16(imu, 10, 0);
    }
    const float gx = 1428.f / 14.2842f * kPi / 180.f;
    Sample s;
    REQUIRE(ParseSwitchState(Family::SwitchPro, false, r, sizeof(r), s));
    CHECK_MSG(Near(s.gyro[2], -gx) && Near(s.gyro[0], 0.f) && Near(s.gyro[1], 0.f), "gyro x is SDL's -z");
    CHECK_MSG(Near(s.accel[2], -kG) && Near(s.accel[1], -kG / 2.f), "accel shuffle, three samples averaged");
    Sample right;
    REQUIRE(ParseSwitchState(Family::JoyConR, false, r, sizeof(r), right));
    CHECK_MSG(Near(right.gyro[2], -gx) && Near(right.accel[1], kG / 2.f), "right Joy-Con flips x and y");
    Sample mini;
    REQUIRE(ParseSwitchState(Family::JoyConL, true, r, sizeof(r), mini));
    CHECK_MSG(Near(mini.gyro[0], -gx) && Near(mini.gyro[2], 0.f) && Near(mini.accel[0], -kG),
              "sideways left Joy-Con: SDL's mini-gamepad swap");
    REQUIRE(ParseSwitchState(Family::JoyConR, true, r, sizeof(r), mini));
    CHECK_MSG(Near(mini.gyro[0], gx) && Near(mini.gyro[2], 0.f), "sideways right Joy-Con");
    r[5] = 0x20; // left SL
    r[3] = 0x10; // right SR
    REQUIRE(ParseSwitchState(Family::JoyConL, false, r, sizeof(r), s));
    CHECK_EQ(s.paddles, 1u << kLeftPaddle1);
    REQUIRE(ParseSwitchState(Family::JoyConR, false, r, sizeof(r), s));
    CHECK_EQ(s.paddles, 1u << kRightPaddle1);
    REQUIRE(ParseSwitchState(Family::JoyConR, true, r, sizeof(r), s));
    CHECK_MSG(s.paddles == 0, "a sideways Joy-Con's SL and SR are its shoulders");
    REQUIRE(ParseSwitchState(Family::SwitchPro, false, r, sizeof(r), s));
    CHECK_MSG(s.paddles == 0, "the Pro Controller has no back buttons");
    // Steam's IMU off zeroes the block: the stamp stops; a running IMU moves it.
    uint8_t off[kSwitchFullReportSize] = {kSwitchReportFull, 1};
    Sample a, b2;
    REQUIRE(ParseSwitchState(Family::SwitchPro, false, off, sizeof(off), a));
    off[1] = 2;
    off[3] = 0x04;
    REQUIRE(ParseSwitchState(Family::SwitchPro, false, off, sizeof(off), b2));
    CHECK_MSG(a.imuTimestamp == b2.imuTimestamp, "a zeroed IMU block keeps its stamp");
    off[20] = 1;
    REQUIRE(ParseSwitchState(Family::SwitchPro, false, off, sizeof(off), b2));
    CHECK(a.imuTimestamp != b2.imuTimestamp);
    CHECK_MSG(!ParseSwitchState(Family::SwitchPro, false, r, sizeof(r) - 1, s), "short report");
    r[0] = 0x21;
    CHECK_MSG(!ParseSwitchState(Family::SwitchPro, false, r, sizeof(r), s), "subcommand replies aren't state");
    CHECK_MSG(!ParseSwitchState(Family::DualSense, false, off, sizeof(off), s), "not a Switch family");

    uint8_t cmd[kSwitchOutputReportSize];
    CHECK_EQ(BuildSwitchImuEnable(0x13, false, cmd), size_t(64));
    const uint8_t head[] = {0x01, 0x03, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x40, 0x01, 0x00};
    CHECK(std::memcmp(cmd, head, sizeof(head)) == 0);
    CHECK_EQ(BuildSwitchImuEnable(0, true, cmd), size_t(49));
  }
  // Which Sony or Nintendo controller is Player 1's: unambiguous matches only.
  {
    const PadId ds{kVendorSony, kProductDualSense};
    const PadId ds4{kVendorSony, kProductDualShock4};
    const PadId pro{kVendorNintendo, kProductSwitchPro};
    const PadId left{kVendorNintendo, kProductJoyConL};
    const PadId right{kVendorNintendo, kProductJoyConR};
    const PadId pair{kVendorNintendo, kProductJoyConPair};
    const PadId xbox{0x045e, 0x0b13};
    {
      const PadId steam[] = {ds, xbox};
      const PadId devs[] = {ds, pro};
      CHECK_MSG(IsBehindPlayerOne(ds, steam, 2, ds, devs, 2), "the one DualSense behind the one DualSense pad");
      CHECK_MSG(!IsBehindPlayerOne(ds, steam, 2, pro, devs, 2), "not another vendor's");
      CHECK_MSG(!IsBehindPlayerOne(xbox, steam, 2, ds, devs, 2), "an Xbox pad as Player 1");
    }
    {
      const PadId steam[] = {ds, ds};
      const PadId devs[] = {ds, ds};
      CHECK_MSG(!IsBehindPlayerOne(ds, steam, 2, ds, devs, 2), "two DualSenses: neither");
      const PadId one[] = {ds};
      CHECK_MSG(!IsBehindPlayerOne(ds, steam, 2, ds, one, 1), "two DualSense pads, one read: still ambiguous");
    }
    {
      // Steam reports another product id: the one Sony controller behind the one Sony pad.
      const PadId steam[] = {ds4};
      const PadId devs[] = {ds};
      CHECK_MSG(IsBehindPlayerOne(ds4, steam, 1, ds, devs, 1), "vendor fallback");
      const PadId two[] = {ds, ds4};
      CHECK_MSG(!IsBehindPlayerOne(ds4, steam, 1, ds, two, 2), "the DS4 itself is there: not the DualSense");
    }
    {
      const PadId devs[] = {left, right};
      const PadId steamPair[] = {pair};
      CHECK(IsJoyConPair(steamPair, 1, devs, 2));
      CHECK_MSG(IsBehindPlayerOne(pair, steamPair, 1, left, devs, 2) &&
                    IsBehindPlayerOne(pair, steamPair, 1, right, devs, 2),
                "both Joy-Cons of a pair");
      const PadId steamAsRight[] = {right};
      CHECK_MSG(IsBehindPlayerOne(right, steamAsRight, 1, left, devs, 2), "a pair Steam reports as the right one");
      const PadId steamSingles[] = {left, right};
      CHECK(!IsJoyConPair(steamSingles, 2, devs, 2));
      CHECK_MSG(IsBehindPlayerOne(left, steamSingles, 2, left, devs, 2) &&
                    !IsBehindPlayerOne(left, steamSingles, 2, right, devs, 2),
                "two single Joy-Cons: Player 1's only");
      const PadId lone[] = {right};
      CHECK_MSG(!IsJoyConPair(steamPair, 1, lone, 1) && IsBehindPlayerOne(pair, steamPair, 1, right, lone, 1),
                "a pair's survivor by vendor");
    }
  }

  if (port_test::Failures() == 0) {
    std::printf("port_steam_triton: ok\n");
  }
  return PORT_TEST_RESULT();
}
