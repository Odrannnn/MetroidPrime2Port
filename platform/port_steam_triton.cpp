// The Steam Controller (2026) report parser: no SDL, so tests/port_steam_triton.cpp can run it.
// The HID client is platform/input/port_steam_triton_hid.cpp.

#include "port_steam_triton.h"

#include <cstring>

namespace PortSteamTriton {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kStandardGravity = 9.80665f;
// 16-bit full scale: +-2000 deg/s and +-2 g (16.384 per deg/s, 16384 per g).
constexpr float kGyroRadPerCount = 2000.f / 32768.f * (kPi / 180.f);
constexpr float kAccelPerCount = 2.f / 32768.f * kStandardGravity;
constexpr size_t kImu = 29;
constexpr size_t kImuTimestamped = 31;

int16_t I16(const uint8_t* p) { return int16_t(uint16_t(p[0] | (p[1] << 8))); }

uint32_t U32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

} // namespace

void BuildImuEnableReport(uint8_t out[kFeatureReportSize]) {
  std::memset(out, 0, kFeatureReportSize);
  out[0] = 0x01; // feature report id
  out[1] = 0x87; // set settings values
  out[2] = 3;    // one setting: id + u16 value
  out[3] = 48;   // IMU mode
  out[4] = 0x18; // raw accel | raw gyro
  out[5] = 0x00;
}

void BuildDeckImuEnableReport(uint8_t out[kDeckFeatureReportSize]) {
  std::memset(out, 0, kDeckFeatureReportSize);
  out[0] = 0x00; // report id
  out[1] = 0x87; // set settings values
  out[2] = 3;    // one setting: id + u16 value
  out[3] = 48;   // IMU mode
  out[4] = 0x18; // raw accel | raw gyro
  out[5] = 0x00;
}

uint32_t DeckPaddleBits(uint32_t buttonsLow, uint32_t buttonsHigh) {
  uint32_t bits = 0;
  if (buttonsHigh & kDeckMaskL4)
    bits |= 1u << kLeftPaddle1;
  if (buttonsHigh & kDeckMaskR4)
    bits |= 1u << kRightPaddle1;
  if (buttonsLow & kDeckMaskL5)
    bits |= 1u << kLeftPaddle2;
  if (buttonsLow & kDeckMaskR5)
    bits |= 1u << kRightPaddle2;
  return bits;
}

uint32_t PaddleBits(uint32_t buttons) {
  uint32_t bits = 0;
  if (buttons & kMaskL4)
    bits |= 1u << kLeftPaddle1;
  if (buttons & kMaskR4)
    bits |= 1u << kRightPaddle1;
  if (buttons & kMaskL5)
    bits |= 1u << kLeftPaddle2;
  if (buttons & kMaskR5)
    bits |= 1u << kRightPaddle2;
  return bits;
}

namespace {

bool ParseDeckState(const uint8_t* data, size_t size, Sample& out) {
  if (size < kDeckStateReportSize || data[0] != 0x01 || data[1] != 0x00 ||
      data[2] != kDeckReportType || data[3] != kDeckStateReportSize) {
    return false;
  }
  out = Sample{};
  out.deck = true;
  out.report = kDeckReportType;
  out.buttons = U32(data + 8);
  out.paddles = DeckPaddleBits(out.buttons, U32(data + 12));
  // FNV-1a over the raw accelerometer and gyro (bytes 24-35).
  uint32_t hash = 2166136261u;
  for (size_t i = 24; i < 36; ++i) {
    hash = (hash ^ data[i]) * 16777619u;
  }
  out.imuTimestamp = hash;
  const int16_t ax = I16(data + 24);
  const int16_t ay = I16(data + 26);
  const int16_t az = I16(data + 28);
  const int16_t gx = I16(data + 30);
  const int16_t gy = I16(data + 32);
  const int16_t gz = I16(data + 34);
  // Same full scale and axes as the Steam Controller (SDL's Deck driver).
  out.gyro[0] = gx * kGyroRadPerCount;
  out.gyro[1] = gz * kGyroRadPerCount;
  out.gyro[2] = -gy * kGyroRadPerCount;
  out.accel[0] = ax * kAccelPerCount;
  out.accel[1] = az * kAccelPerCount;
  out.accel[2] = -ay * kAccelPerCount;
  return true;
}

} // namespace

bool ParseState(const uint8_t* data, size_t size, Sample& out) {
  if (data != nullptr && size >= 4 && data[0] == 0x01 && data[2] == kDeckReportType) {
    return ParseDeckState(data, size, out);
  }
  if (data == nullptr || size < 1) {
    return false;
  }
  // The IMU block: its timestamp, then accel and gyro (x, y, z) right after it.
  size_t imu = 0;
  size_t values = 0;
  if ((data[0] == kReportStateUsb || data[0] == kReportStateBle) && size >= kStateReportMinSize) {
    imu = kImu;
    values = kImu + 4;
  } else if (data[0] == kReportStateTimestamp && size >= kStateTimestampReportMinSize) {
    imu = kImuTimestamped;
    values = kImuTimestamped + 2;
  } else {
    return false;
  }
  const uint8_t* p = data + 1;
  out = Sample{};
  out.report = data[0];
  out.buttons = U32(p + 1);
  out.paddles = PaddleBits(out.buttons);
  out.imuTimestamp = values - imu == 4 ? U32(p + imu) : uint32_t(p[imu] | (p[imu + 1] << 8));
  const int16_t ax = I16(p + values);
  const int16_t ay = I16(p + values + 2);
  const int16_t az = I16(p + values + 4);
  const int16_t gx = I16(p + values + 6);
  const int16_t gy = I16(p + values + 8);
  const int16_t gz = I16(p + values + 10);
  // The controller's z is SDL's y and its y is SDL's -z (SDL's driver).
  out.gyro[0] = gx * kGyroRadPerCount;
  out.gyro[1] = gz * kGyroRadPerCount;
  out.gyro[2] = -gy * kGyroRadPerCount;
  out.accel[0] = ax * kAccelPerCount;
  out.accel[1] = az * kAccelPerCount;
  out.accel[2] = -ay * kAccelPerCount;
  return true;
}

} // namespace PortSteamTriton
