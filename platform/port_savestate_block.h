#pragma once

// The byte writer and reader of the room record's blocks (port_savestate_room.cpp) and of the
// kTagFields block (port_savestate_fields.cpp): little-endian, bounds-checked reads.

#include "Kyoto/Math/CVector3f.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace PortSaveState {

struct BlockWriter {
  std::string bytes;
  void U8(uint8_t v) { bytes.push_back(static_cast< char >(v)); }
  void U32(uint32_t v) {
    for (int i = 0; i < 4; ++i) {
      bytes.push_back(static_cast< char >(v >> (8 * i)));
    }
  }
  void F32(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    U32(v);
  }
  void Vec(const CVector3f& v) {
    F32(v.GetX());
    F32(v.GetY());
    F32(v.GetZ());
  }
  void Bytes(const std::string& s) {
    U32(static_cast< uint32_t >(s.size()));
    bytes.append(s);
  }
};

struct BlockReader {
  const std::string& in;
  size_t at = 0;
  bool ok = true;
  explicit BlockReader(const std::string& s) : in(s) {}
  uint32_t U8() {
    if (!ok || at + 1 > in.size()) {
      ok = false;
      return 0;
    }
    return static_cast< uint8_t >(in[at++]);
  }
  uint32_t U32() {
    if (!ok || at + 4 > in.size()) {
      ok = false;
      return 0;
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      v |= static_cast< uint32_t >(static_cast< uint8_t >(in[at + i])) << (8 * i);
    }
    at += 4;
    return v;
  }
  float F32() {
    const uint32_t v = U32();
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }
  // A finite vector, or false.
  bool Vec(CVector3f& out) {
    const float x = F32();
    const float y = F32();
    const float z = F32();
    if (!ok || !(x == x) || !(y == y) || !(z == z) || x > 1e7f || x < -1e7f || y > 1e7f ||
        y < -1e7f || z > 1e7f || z < -1e7f) {
      return false;
    }
    out = CVector3f(x, y, z);
    return true;
  }
  std::string Bytes() {
    const uint32_t len = U32();
    if (!ok || at + len > in.size()) {
      ok = false;
      return std::string();
    }
    std::string s = in.substr(at, len);
    at += len;
    return s;
  }
};

} // namespace PortSaveState
