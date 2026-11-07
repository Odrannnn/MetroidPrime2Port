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

bool OtherFamily(uint16_t vendor, uint16_t product, Family& out) {
  if (vendor == kVendorSony) {
    switch (product) {
    case kProductDualShock4:
    case kProductDualShock4v2:
      out = Family::DualShock4;
      return true;
    case kProductDualSense:
      out = Family::DualSense;
      return true;
    case kProductDualSenseEdge:
      out = Family::DualSenseEdge;
      return true;
    default:
      return false;
    }
  }
  if (vendor == kVendorNintendo) {
    switch (product) {
    case kProductSwitchPro:
      out = Family::SwitchPro;
      return true;
    case kProductJoyConL:
      out = Family::JoyConL;
      return true;
    case kProductJoyConR:
      out = Family::JoyConR;
      return true;
    default:
      return false;
    }
  }
  return false;
}

namespace {

// Sony's nominal resolution: 16 counts per deg/s (SDL: raw * 64 / 1024), 8192 per g.
constexpr float kSonyGyroCountsPerDps = 16.f;
constexpr float kSonyAccelCountsPerG = 8192.f;
// A Switch controller's (SDL's defaults, without the controller's own calibration).
constexpr float kSwitchGyroScale = 14.2842f;
constexpr float kSwitchAccelScale = 4096.f;

uint32_t Crc32(uint32_t crc, const uint8_t* data, size_t size) {
  crc = ~crc;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

// A Bluetooth report's CRC-32 over the HID header byte 0xA1 and its first size - 4 bytes.
bool SonyCrcValid(const uint8_t* data, size_t size) {
  const uint8_t header = 0xA1;
  const uint32_t crc = Crc32(Crc32(0, &header, 1), data, size - 4);
  return crc == U32(data + size - 4);
}

int Abs(int v) { return v < 0 ? -v : v; }
float Fabs(float v) { return v < 0.f ? -v : v; }

// The accelerometer half of a Sony calibration report (the same in both): the plus and minus
// extents of each axis are 2 g apart.
bool SonyAccelCalibration(const uint8_t* data, ImuScale& out) {
  for (int i = 0; i < 3; ++i) {
    const int plus = I16(data + 23 + 4 * i);
    const int minus = I16(data + 25 + 4 * i);
    const int range = int16_t(plus - minus);
    if (range == 0) {
      return false;
    }
    const float sensitivity = 2.f * kSonyAccelCountsPerG / float(range);
    out.bias[3 + i] = float(int16_t(plus - range / 2));
    out.scale[3 + i] = sensitivity / kSonyAccelCountsPerG * kStandardGravity;
    if (Abs(int(out.bias[3 + i])) > 1024 || Fabs(1.f - sensitivity) > 0.5f) {
      return false;
    }
  }
  return true;
}

} // namespace

ImuScale SonyDefaultScale() {
  ImuScale s;
  for (int i = 0; i < 3; ++i) {
    s.scale[i] = 1.f / kSonyGyroCountsPerDps * (kPi / 180.f);
    s.scale[3 + i] = 1.f / kSonyAccelCountsPerG * kStandardGravity;
  }
  return s;
}

bool ParseDualSenseCalibration(const uint8_t* data, size_t size, ImuScale& out) {
  if (data == nullptr || size < 35 || data[0] != kDualSenseCalibrationReport) {
    return false;
  }
  ImuScale s;
  // Bias (1), then plus and minus per axis (7), then the speed the extents were taken at (19).
  const int speed = I16(data + 19) + I16(data + 21);
  for (int i = 0; i < 3; ++i) {
    const int plus = I16(data + 7 + 4 * i);
    const int minus = I16(data + 9 + 4 * i);
    if (plus == minus) {
      return false;
    }
    // SDL's sensitivity is 64 counts of its 1024 per deg/s nominally.
    const float sensitivity = float(speed) * 1024.f / float(plus - minus);
    s.bias[i] = float(I16(data + 1 + 2 * i));
    s.scale[i] = sensitivity / 1024.f * (kPi / 180.f);
    if (Abs(int(s.bias[i])) > 1024 || Fabs(1.f - sensitivity / 64.f) > 0.5f) {
      return false;
    }
  }
  if (!SonyAccelCalibration(data, s)) {
    return false;
  }
  out = s;
  return true;
}

bool ParseDualShock4Calibration(const uint8_t* data, size_t size, bool bluetooth, ImuScale& out) {
  const uint8_t id = bluetooth ? kDualShock4CalibrationReportBluetooth : kDualShock4CalibrationReportUsb;
  if (data == nullptr || size < 35 || data[0] != id) {
    return false;
  }
  ImuScale s;
  const int speed = I16(data + 19) + I16(data + 21);
  for (int i = 0; i < 3; ++i) {
    const int bias = I16(data + 1 + 2 * i);
    const int plus = bluetooth ? I16(data + 7 + 2 * i) : I16(data + 7 + 4 * i);
    const int minus = bluetooth ? I16(data + 13 + 2 * i) : I16(data + 9 + 4 * i);
    const int span = Abs(plus - bias) + Abs(minus - bias);
    if (span == 0) {
      return false;
    }
    const float scale = float(speed) * kSonyGyroCountsPerDps / float(span);
    s.bias[i] = float(bias);
    s.scale[i] = scale / kSonyGyroCountsPerDps * (kPi / 180.f);
    if (Abs(bias) > 1024 || Fabs(1.f - scale) > 0.5f) {
      return false;
    }
  }
  if (!SonyAccelCalibration(data, s)) {
    return false;
  }
  out = s;
  return true;
}

bool ParseSonyState(Family family, const uint8_t* data, size_t size, const ImuScale& scale, Sample& out) {
  if (data == nullptr || size < 1 || !IsSony(family)) {
    return false;
  }
  const uint8_t* state = nullptr;
  size_t gyro = 0;
  if (family == Family::DualShock4) {
    // USB: byte 31 bit 2 set means no controller behind a wireless adapter.
    if (data[0] == 0x01 && size >= 64 && (data[31] & 0x04) == 0) {
      state = data + 1;
    } else if (data[0] >= 0x11 && data[0] <= 0x19 && size >= 78 && (data[1] & 0x80) != 0 &&
               SonyCrcValid(data, 78)) {
      state = data + 3;
    }
    gyro = 12;
  } else {
    // USB: 10 and 78 bytes are the simple report (no IMU).
    if (data[0] == 0x01 && size >= 1 + 31 && size != 78) {
      state = data + 1;
    } else if (data[0] == 0x31 && size >= 2 + 31 + 4 && SonyCrcValid(data, size)) {
      state = data + 2;
    }
    gyro = 15;
  }
  if (state == nullptr) {
    return false;
  }
  out = Sample{};
  out.report = data[0];
  if (family == Family::DualShock4) {
    out.buttons = uint32_t(state[4]) | (uint32_t(state[5]) << 8) | (uint32_t(state[6]) << 16);
    out.imuTimestamp = uint32_t(state[9] | (state[10] << 8));
  } else {
    out.buttons = U32(state + 7);
    out.imuTimestamp = U32(state + 27);
  }
  if (family == Family::DualSenseEdge) {
    const uint8_t b = state[9];
    out.paddles = ((b & kEdgeRightPaddle) ? 1u << kRightPaddle1 : 0u) | ((b & kEdgeLeftPaddle) ? 1u << kLeftPaddle1 : 0u) |
                  ((b & kEdgeRightFn) ? 1u << kRightPaddle2 : 0u) | ((b & kEdgeLeftFn) ? 1u << kLeftPaddle2 : 0u);
  }
  // SDL's axes as the controller reports them.
  for (int i = 0; i < 3; ++i) {
    out.gyro[i] = (float(I16(state + gyro + 2 * i)) - scale.bias[i]) * scale.scale[i];
    out.accel[i] = (float(I16(state + gyro + 6 + 2 * i)) - scale.bias[3 + i]) * scale.scale[3 + i];
  }
  return true;
}

bool ParseSwitchState(Family family, bool miniGamepad, const uint8_t* data, size_t size, Sample& out) {
  if (data == nullptr || size < kSwitchFullReportSize || data[0] != kSwitchReportFull ||
      (family != Family::SwitchPro && !IsJoyCon(family))) {
    return false;
  }
  out = Sample{};
  out.report = data[0];
  // Counter, battery, then the right, shared and left button bytes.
  out.buttons = uint32_t(data[3]) | (uint32_t(data[4]) << 8) | (uint32_t(data[5]) << 16);
  if (family == Family::JoyConL && !miniGamepad) {
    out.paddles = ((data[5] & 0x20) ? 1u << kLeftPaddle1 : 0u) | ((data[5] & 0x10) ? 1u << kLeftPaddle2 : 0u);
  } else if (family == Family::JoyConR && !miniGamepad) {
    out.paddles = ((data[3] & 0x10) ? 1u << kRightPaddle1 : 0u) | ((data[3] & 0x20) ? 1u << kRightPaddle2 : 0u);
  }
  // Three samples of accel x, y, z, gyro x, y, z, 5 ms apart: their mean is the rate over the report.
  const uint8_t* imu = data + 13;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < 36; ++i) {
    hash = (hash ^ imu[i]) * 16777619u;
  }
  out.imuTimestamp = hash;
  int sum[6] = {};
  for (int sample = 0; sample < 3; ++sample) {
    for (int v = 0; v < 6; ++v) {
      sum[v] += I16(imu + 12 * sample + 2 * v);
    }
  }
  const float gyroScale = (kPi / 180.f) / kSwitchGyroScale / 3.f;
  const float accelScale = kStandardGravity / kSwitchAccelScale / 3.f;
  // SDL shuffles the axes to match the PlayStation controllers'.
  float g[3] = {-gyroScale * float(sum[4]), gyroScale * float(sum[5]), -gyroScale * float(sum[3])};
  float a[3] = {-accelScale * float(sum[1]), accelScale * float(sum[2]), -accelScale * float(sum[0])};
  if (family == Family::JoyConR) {
    g[0] = -g[0];
    g[1] = -g[1];
    a[0] = -a[0];
    a[1] = -a[1];
  }
  if (miniGamepad && IsJoyCon(family)) {
    const float sign = family == Family::JoyConL ? 1.f : -1.f;
    float t = g[2];
    g[2] = -sign * g[0];
    g[0] = sign * t;
    t = a[2];
    a[2] = -sign * a[0];
    a[0] = sign * t;
  }
  std::memcpy(out.gyro, g, sizeof(g));
  std::memcpy(out.accel, a, sizeof(a));
  return true;
}

size_t BuildSwitchImuEnable(uint8_t counter, bool bluetooth, uint8_t out[kSwitchOutputReportSize]) {
  static const uint8_t kNeutralRumble[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
  std::memset(out, 0, kSwitchOutputReportSize);
  out[0] = 0x01; // rumble and subcommand
  out[1] = uint8_t(counter & 0x0F);
  std::memcpy(out + 2, kNeutralRumble, sizeof(kNeutralRumble));
  out[10] = 0x40; // enable IMU
  out[11] = 0x01;
  return bluetooth ? 49 : kSwitchOutputReportSize;
}

namespace {

bool SamePad(PadId a, PadId b) { return a.vendor == b.vendor && a.product == b.product; }

bool IsJoyConId(PadId id) {
  return id.vendor == kVendorNintendo &&
         (id.product == kProductJoyConL || id.product == kProductJoyConR || id.product == kProductJoyConPair);
}

template < typename Pred >
size_t CountIf(const PadId* ids, size_t count, Pred pred) {
  size_t n = 0;
  for (size_t i = 0; i < count; ++i) {
    n += pred(ids[i]) ? 1 : 0;
  }
  return n;
}

} // namespace

bool IsJoyConPair(const PadId* steamPads, size_t steamPadCount, const PadId* devices, size_t deviceCount) {
  const PadId left{kVendorNintendo, kProductJoyConL};
  const PadId right{kVendorNintendo, kProductJoyConR};
  return CountIf(devices, deviceCount, [&](PadId d) { return SamePad(d, left); }) == 1 &&
         CountIf(devices, deviceCount, [&](PadId d) { return SamePad(d, right); }) == 1 &&
         CountIf(steamPads, steamPadCount, IsJoyConId) <= 1;
}

bool IsBehindPlayerOne(PadId playerOne, const PadId* steamPads, size_t steamPadCount, PadId device,
                       const PadId* devices, size_t deviceCount) {
  if (playerOne.vendor != device.vendor) {
    return false;
  }
  if (playerOne.product == device.product &&
      CountIf(steamPads, steamPadCount, [&](PadId p) { return SamePad(p, playerOne); }) == 1 &&
      CountIf(devices, deviceCount, [&](PadId d) { return SamePad(d, device); }) == 1) {
    return true;
  }
  if (IsJoyConId(playerOne) && IsJoyConId(device) && IsJoyConPair(steamPads, steamPadCount, devices, deviceCount)) {
    return true;
  }
  const auto sameVendor = [&](PadId p) { return p.vendor == playerOne.vendor; };
  return CountIf(devices, deviceCount, [&](PadId d) { return SamePad(d, playerOne); }) == 0 &&
         CountIf(steamPads, steamPadCount, sameVendor) == 1 && CountIf(devices, deviceCount, sameVendor) == 1;
}

} // namespace PortSteamTriton
