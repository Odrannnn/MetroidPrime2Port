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

  if (port_test::Failures() == 0) {
    std::printf("port_steam_triton: ok\n");
  }
  return PORT_TEST_RESULT();
}
