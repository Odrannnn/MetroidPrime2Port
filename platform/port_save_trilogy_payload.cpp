// The stream converter behind the Trilogy save.bin (port_save_trilogy.h): a file's CGameState stream
// in the GameCube framing (a G2ME or R3ME .gci slot) <-> the Wii's two halves of a save.bin slot.
//
// Layouts measured on the real save.bins (docs/TRILOGY.md, docs/SAVE_RELEASES.md):
//  * part B is the slot stream behind 20 zero bytes: GMST, the time, 2 flag bits (hard mode and the
//    dark world; the GameCube writes 3, with "initialise powerups at first spawn" between them), the
//    world, the play time, 4 player states, the hints, PREV, then - byte-aligned - the escape time, the
//    game variables, the cinematic bits (the GameCube keeps those in the system block instead), and the
//    world records. There is no GMND.
//  * part A is the 4-byte slot signature, zeros, then SYST, the system variables and SYND (the
//    GameCube's system block also holds the save index and the cinematics).
// The Trilogy's Echoes is the Trilogy disc's data, so its tables are R3ME's: a G2ME stream is mapped
// onto them by asset ID (tools/trilogy_tables.py writes the tables). What the other release lacks is
// dropped and counted; what it adds starts cleared (layers: at the MLVL default).
#include "port_save_trilogy.h"

#include <algorithm>
#include <cstring>

namespace PortSave::Trilogy {

namespace {

#include "port_save_trilogy_tables.inc"

// The .gci layout; the same numbers as port_save.cpp, which describes the same file.
constexpr size_t kDirEntry = 0x40;
constexpr size_t kBlock = 0x2000;
constexpr size_t kUserHeader = 8; // CRC and generation ahead of the save data
constexpr size_t kUserDataSize = 0x1ff8;
constexpr size_t kSysOffset = 15;
constexpr size_t kSysBytes = 192;
constexpr size_t kSlotsStart = 15 + 192 + 3 * 32 + 32;
constexpr size_t kSlotStride = 0xa38;
constexpr int kSysBits = 29;       // CPersistentOptions' environment variables
constexpr int kWiiPrefixBits = 20 * 8;
constexpr uint32_t kSigUsa = 0xfe44ecc0u; // part A's leading word on the Trilogy's USA / PAL discs
constexpr uint32_t kSigPal = 0xee42ffaeu;

constexpr uint32_t Fourcc(char a, char b, char c, char d) {
  return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) | (uint32_t(uint8_t(c)) << 8) |
         uint32_t(uint8_t(d));
}

uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
uint32_t Le32(const uint8_t* p) {
  return (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[0]);
}
void PutBe32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24);
  p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);
  p[3] = uint8_t(v);
}
void PutLe32(uint8_t* p, uint32_t v) {
  p[3] = uint8_t(v >> 24);
  p[2] = uint8_t(v >> 16);
  p[1] = uint8_t(v >> 8);
  p[0] = uint8_t(v);
}

// CCRC32::Calculate(data, size, 0xFFFFFFFF): the reflected CRC-32 without the final inversion.
uint32_t CardCrc(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) {
      c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
  }
  return c;
}

// MSB-first, as CBitStreamReader / CBitStreamWriter. Reading past the end sets `bad` and yields zeros.
struct BitReader {
  const uint8_t* data;
  size_t bits;
  size_t pos = 0;
  bool bad = false;

  BitReader(const uint8_t* d, size_t byteCount) : data(d), bits(byteCount * 8) {}
  uint32_t Get(int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
      uint32_t bit = 0;
      if (pos >= bits) {
        bad = true;
      } else {
        bit = (data[pos >> 3] >> (7 - (pos & 7))) & 1;
      }
      v = (v << 1) | bit;
      ++pos;
    }
    return v;
  }
  void Align() { pos = (pos + 7) & ~size_t(7); }
};

struct BitWriter {
  std::vector<uint8_t> bytes;
  size_t pos = 0;

  void Put(uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) {
      if ((pos >> 3) >= bytes.size()) {
        bytes.push_back(0);
      }
      if ((v >> i) & 1) {
        bytes[pos >> 3] |= uint8_t(0x80 >> (pos & 7));
      }
      ++pos;
    }
  }
  void Align() {
    pos = (pos + 7) & ~size_t(7);
    if ((pos >> 3) > bytes.size()) {
      bytes.resize(pos >> 3, 0);
    }
  }
};

using Bits = std::vector<uint8_t>; // one entry per bit (or per 2-bit value for scans)

void ReadBits(BitReader& r, Bits& v, size_t n, int width = 1) {
  v.resize(n);
  for (size_t i = 0; i < n; ++i) {
    v[i] = uint8_t(r.Get(width));
  }
}
void WriteBits(BitWriter& w, const Bits& v, int width = 1) {
  for (uint8_t b : v) {
    w.Put(b, width);
  }
}

bool Expect(BitReader& r, uint32_t marker, const char* what, std::string& error) {
  const uint32_t got = r.Get(32);
  if (got != marker) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s marker is %08x, not %08x", what, got, marker);
    error = buf;
    return false;
  }
  return true;
}

// ---- the model: one file's state in a release's layout ------------------------------------------

struct Player {
  Bits a, b, fixed, scans, tail; // PLST body (77), PRST body (186), 4 fixed scans, the scans, completion (18)
};

struct World {
  uint32_t head[2] = {};
  Bits relays, areaBits, doors, unmappable;
  uint8_t mapUsed = 0;
  std::vector<Bits> layers; // per area, indexed by layer (entry 0 is not stored)
};

struct Model {
  const STriRelease* rel = nullptr;
  uint32_t time = 0;
  uint8_t hard = 0, init = 0, dark = 0;
  uint32_t desired = 0, playHi = 0, playLo = 0;
  Player players[4];
  struct Hint {
    uint32_t state = 0, time = 0;
  };
  std::vector<Hint> hints;
  Bits prev; // PREV's body: 180 bits
  uint32_t escape = 0;
  Bits gameVars;
  Bits cines; // every world's cinematics, in the release's world order
  Bits sysBits;
  uint32_t saveIdx = 0;
  size_t lastWorldBit = 0; // where the last world record starts in the stream it was read from
  std::vector<World> worlds;
};

size_t LayerBits(const STriWorld& w) {
  size_t n = 0;
  for (int a = 0; a < w.areas; ++a) {
    n += w.layerCounts[a] - 1;
  }
  return n;
}

size_t BodyBits(const STriWorld& w) {
  return 64 + w.relays + 2 * size_t(w.areas) + w.doors + w.unmappable + 1 + 10 + LayerBits(w);
}

std::string N(size_t v) { return std::to_string(v); }

bool ReadHead(BitReader& r, const STriRelease& rel, Model& m, bool wii, std::string& error) {
  m.rel = &rel;
  if (!Expect(r, Fourcc('G', 'M', 'S', 'T'), "GMST", error)) {
    return false;
  }
  m.time = r.Get(32);
  if (wii) {
    m.hard = uint8_t(r.Get(1));
    m.dark = uint8_t(r.Get(1));
  } else {
    m.hard = uint8_t(r.Get(1));
    m.init = uint8_t(r.Get(1));
    m.dark = uint8_t(r.Get(1));
  }
  m.desired = r.Get(32);
  m.playHi = r.Get(32);
  m.playLo = r.Get(32);
  for (Player& p : m.players) {
    if (!Expect(r, Fourcc('P', 'L', 'S', 'T'), "PLST", error)) {
      return false;
    }
    ReadBits(r, p.a, 32 + 32 + 3 + 2 + 3 + 3 + 2);
    if (!Expect(r, Fourcc('P', 'R', 'S', 'T'), "PRST", error)) {
      return false;
    }
    ReadBits(r, p.b, 186);
    if (!Expect(r, Fourcc('P', 'O', 'W', 'R'), "POWR", error)) {
      return false;
    }
    ReadBits(r, p.fixed, 4 * 2);
    ReadBits(r, p.scans, size_t(rel.scanCount), 2);
    ReadBits(r, p.tail, 18);
    if (!Expect(r, Fourcc('S', 'C', 'A', 'N'), "SCAN", error)) {
      return false;
    }
  }
  m.hints.resize(size_t(rel.hintCount));
  for (Model::Hint& h : m.hints) {
    h.state = r.Get(2);
    h.time = r.Get(32);
  }
  if (!Expect(r, Fourcc('P', 'R', 'E', 'V'), "PREV", error)) {
    return false;
  }
  ReadBits(r, m.prev, 32 + 1 + 8 + 3 + 4 * (2 + 16 + 16));
  r.Align();
  m.escape = r.Get(32);
  ReadBits(r, m.gameVars, size_t(rel.gameVarBits));
  if (wii) {
    ReadBits(r, m.cines, size_t(rel.cinematicBits));
  }
  r.Align();
  if (r.bad) {
    error = "the slot ends inside its player states";
    return false;
  }
  return true;
}

void WriteHead(BitWriter& w, const Model& m, bool wii) {
  w.Put(Fourcc('G', 'M', 'S', 'T'), 32);
  w.Put(m.time, 32);
  w.Put(m.hard, 1);
  if (!wii) {
    w.Put(m.init, 1);
  }
  w.Put(m.dark, 1);
  w.Put(m.desired, 32);
  w.Put(m.playHi, 32);
  w.Put(m.playLo, 32);
  for (const Player& p : m.players) {
    w.Put(Fourcc('P', 'L', 'S', 'T'), 32);
    WriteBits(w, p.a);
    w.Put(Fourcc('P', 'R', 'S', 'T'), 32);
    WriteBits(w, p.b);
    w.Put(Fourcc('P', 'O', 'W', 'R'), 32);
    WriteBits(w, p.fixed);
    WriteBits(w, p.scans, 2);
    WriteBits(w, p.tail);
    w.Put(Fourcc('S', 'C', 'A', 'N'), 32);
  }
  for (const Model::Hint& h : m.hints) {
    w.Put(h.state, 2);
    w.Put(h.time, 32);
  }
  w.Put(Fourcc('P', 'R', 'E', 'V'), 32);
  WriteBits(w, m.prev);
  w.Align();
  w.Put(m.escape, 32);
  WriteBits(w, m.gameVars);
  if (wii) {
    WriteBits(w, m.cines);
  }
  w.Align();
}

bool ReadWorld(BitReader& r, const STriWorld& fw, World& out, bool cut, std::string& error) {
  out.head[0] = r.Get(32); // area ID, desired area asset ID
  out.head[1] = r.Get(32);
  ReadBits(r, out.relays, fw.relays);
  ReadBits(r, out.areaBits, 2 * size_t(fw.areas));
  ReadBits(r, out.doors, fw.doors);
  ReadBits(r, out.unmappable, fw.unmappable);
  out.mapUsed = uint8_t(r.Get(1));
  const uint32_t layerTotal = r.Get(10);
  if (layerTotal != LayerBits(fw) && !cut) {
    error = "layer total " + N(layerTotal) + ", the release has " + N(LayerBits(fw));
    return false;
  }
  out.layers.resize(size_t(fw.areas));
  for (int a = 0; a < fw.areas; ++a) {
    out.layers[size_t(a)].assign(size_t(fw.layerCounts[a]), 0);
    for (int l = 1; l < fw.layerCounts[a]; ++l) {
      // A record the Wii cut off has lost its layers: they start at the world's defaults.
      out.layers[size_t(a)][size_t(l)] = cut ? uint8_t((fw.layerDefaults[a] >> l) & 1) : uint8_t(r.Get(1));
    }
  }
  return true;
}

void WriteWorld(BitWriter& w, const STriWorld& tw, const World& in) {
  w.Put(in.head[0], 32);
  w.Put(in.head[1], 32);
  WriteBits(w, in.relays);
  WriteBits(w, in.areaBits);
  WriteBits(w, in.doors);
  WriteBits(w, in.unmappable);
  w.Put(in.mapUsed, 1);
  w.Put(uint32_t(LayerBits(tw)), 10);
  for (int a = 0; a < tw.areas; ++a) {
    for (int l = 1; l < tw.layerCounts[a]; ++l) {
      w.Put(in.layers[size_t(a)][size_t(l)], 1);
    }
  }
}

// The world count and records, from the byte-aligned `r`; `base`/`size` is the buffer r reads.
bool ReadWorlds(BitReader& r, const uint8_t* base, size_t size, size_t limitBits, const STriRelease& rel,
                Model& m, std::string& error) {
  const uint32_t count = r.Get(8);
  if (count != uint32_t(rel.worldCount)) {
    error = "slot holds " + N(count) + " worlds, the release has " + N(rel.worldCount);
    return false;
  }
  m.worlds.resize(size_t(count));
  for (int i = 0; i < rel.worldCount; ++i) {
    m.lastWorldBit = r.pos;
    const uint32_t id = r.Get(32);
    const uint32_t bitCount = r.Get(16);
    if (r.bad || id != rel.worlds[i].mlvl) {
      error = "world record " + N(size_t(i)) + " has ID " + N(id) + ", not the release's";
      return false;
    }
    if (bitCount != BodyBits(rel.worlds[i])) {
      error = "world record " + N(size_t(i)) + " is " + N(bitCount) + " bits, the release's layout is " +
              N(BodyBits(rel.worlds[i]));
      return false;
    }
    const size_t start = r.pos / 8;
    const size_t bytes = (bitCount + 7) / 8;
    if (start + bytes > size) {
      error = "world record " + N(size_t(i)) + " runs past the slot";
      return false;
    }
    BitReader body(base + start, bytes);
    const bool cut = limitBits != 0 && r.pos + bitCount > limitBits;
    if (!ReadWorld(body, rel.worlds[i], m.worlds[size_t(i)], cut, error)) {
      error = "world record " + N(size_t(i)) + ": " + error;
      return false;
    }
    r.pos += bytes * 8;
  }
  return true;
}

bool WriteWorlds(BitWriter& w, const Model& m, std::string& error) {
  const STriRelease& rel = *m.rel;
  w.Put(uint32_t(rel.worldCount), 8);
  for (int i = 0; i < rel.worldCount; ++i) {
    w.Put(rel.worlds[i].mlvl, 32);
    w.Put(uint32_t(BodyBits(rel.worlds[i])), 16);
    const size_t before = w.pos;
    WriteWorld(w, rel.worlds[i], m.worlds[size_t(i)]);
    if (w.pos - before != BodyBits(rel.worlds[i])) {
      error = "world record " + N(size_t(i)) + " did not come out at its layout's size";
      return false;
    }
    w.Align();
  }
  return true;
}

// ---- the Wii's two halves ---------------------------------------------------------------------

bool ParseWii(const uint8_t* partA, const uint8_t* partB, Model& m, std::string& error) {
  const STriRelease& rel = kR3Release;
  BitReader a(partA, kPartSize);
  a.pos = kWiiPrefixBits;
  if (!Expect(a, Fourcc('S', 'Y', 'S', 'T'), "part A's SYST", error)) {
    return false;
  }
  ReadBits(a, m.sysBits, kSysBits);
  if (!Expect(a, Fourcc('S', 'Y', 'N', 'D'), "part A's SYND", error)) {
    return false;
  }
  // The 20-byte prefix leaves the Wii's 2000 bytes 5 short of the whole stream: the last world's record
  // (the front end's) is cut off there in every real save, so read it from a zero-padded copy.
  std::vector<uint8_t> padded(partB, partB + kPartSize);
  padded.resize(kPartSize + 64, 0);
  BitReader r(padded.data(), padded.size());
  r.pos = kWiiPrefixBits;
  if (!ReadHead(r, rel, m, true, error) || !ReadWorlds(r, padded.data(), padded.size(), kPartSize * 8, rel, m, error)) {
    return false;
  }
  if (m.lastWorldBit + 48 > kPartSize * 8) {
    error = "the Trilogy slot ends before its last world";
    return false;
  }
  return true;
}

bool BuildWii(const Model& m, uint32_t signature, uint8_t* partA, uint8_t* partB, std::string& error) {
  BitWriter a;
  a.Put(signature, 32);
  for (int i = 0; i < 4; ++i) {
    a.Put(0, 32);
  }
  a.Put(Fourcc('S', 'Y', 'S', 'T'), 32);
  WriteBits(a, m.sysBits);
  a.Put(Fourcc('S', 'Y', 'N', 'D'), 32);
  a.Align();
  BitWriter b;
  for (int i = 0; i < 5; ++i) {
    b.Put(0, 32);
  }
  WriteHead(b, m, true);
  if (!WriteWorlds(b, m, error)) {
    return false;
  }
  if (b.bytes.size() > kPartSize && b.bytes.size() <= kPartSize + 8) {
    b.bytes.resize(kPartSize); // the last world's record is cut off, as in the Trilogy's own saves
  }
  if (a.bytes.size() > kPartSize || b.bytes.size() > kPartSize) {
    error = "the converted slot is " + N(b.bytes.size()) + " bytes, over the Trilogy's " + N(kPartSize);
    return false;
  }
  std::memset(partA, 0, kPartSize);
  std::memset(partB, 0, kPartSize);
  std::memcpy(partA, a.bytes.data(), a.bytes.size());
  std::memcpy(partB, b.bytes.data(), b.bytes.size());
  return true;
}

// ---- the GameCube's framing -------------------------------------------------------------------

bool ParseGcSlot(const uint8_t* slot, const STriRelease& rel, Model& m, std::string& error) {
  BitReader r(slot, kSlotStride);
  if (!ReadHead(r, rel, m, false, error) || !ReadWorlds(r, slot, kSlotStride, 0, rel, m, error)) {
    return false;
  }
  if (!Expect(r, Fourcc('G', 'M', 'N', 'D'), "GMND", error) || r.bad) {
    if (error.empty()) {
      error = "the slot ends before its GMND marker";
    }
    return false;
  }
  return true;
}

bool BuildGcSlot(const Model& m, std::vector<uint8_t>& out, std::string& error) {
  BitWriter w;
  WriteHead(w, m, false);
  if (!WriteWorlds(w, m, error)) {
    return false;
  }
  w.Put(Fourcc('G', 'M', 'N', 'D'), 32);
  w.Align();
  if (w.bytes.size() > kSlotStride) {
    error = "the converted slot is " + N(w.bytes.size()) + " bytes, over " + N(kSlotStride);
    return false;
  }
  out = w.bytes;
  out.resize(kSlotStride, 0);
  return true;
}

// The 192-byte system block: SYST, the save index, one bit per cinematic, the system variables, SYND.
bool ParseSysBlock(const uint8_t* sys, const STriRelease& rel, Model& m, std::string& error) {
  BitReader r(sys, kSysBytes);
  if (!Expect(r, Fourcc('S', 'Y', 'S', 'T'), "SYST", error)) {
    return false;
  }
  m.saveIdx = r.Get(2);
  ReadBits(r, m.cines, size_t(rel.cinematicBits));
  ReadBits(r, m.sysBits, kSysBits);
  if (!Expect(r, Fourcc('S', 'Y', 'N', 'D'), "SYND", error) || r.bad) {
    if (error.empty()) {
      error = "the system block ends early";
    }
    return false;
  }
  return true;
}

void BuildSysBlock(const Model& m, uint8_t* sys) {
  BitWriter w;
  w.Put(Fourcc('S', 'Y', 'S', 'T'), 32);
  w.Put(m.saveIdx, 2);
  WriteBits(w, m.cines);
  WriteBits(w, m.sysBits);
  w.Put(Fourcc('S', 'Y', 'N', 'D'), 32);
  w.Align();
  // The other release's block may run longer: nothing of it stays past this one's SYND.
  std::memset(sys, 0, kSysBytes);
  std::memcpy(sys, w.bytes.data(), std::min(w.bytes.size(), kSysBytes));
}

// A .gci: which release it is, and where its newest valid copy sits.
struct Gci {
  const STriRelease* rel = nullptr;
  const STriDirection* fromR3 = nullptr; // this release's layout from the Trilogy's, null for R3 itself
  const STriDirection* toR3 = nullptr;
  uint32_t paSignature = kSigUsa;
  size_t copyBytes = 0;
  int chosen = -1;
  bool little = false;
};

size_t CopyOffset(const Gci& g, int copy) { return kDirEntry + kBlock + size_t(copy) * g.copyBytes; }

bool Locate(const std::vector<uint8_t>& gci, Gci& g, std::string& error) {
  if (gci.size() < kDirEntry + 3 * kBlock) {
    error = "the card file is not a save (too short)";
    return false;
  }
  const std::string code(reinterpret_cast< const char* >(gci.data()), 4);
  if (code == "G2ME") {
    g.rel = &kMERelease;
    g.toR3 = &kMEtoR3;
    g.fromR3 = &kR3toME;
  } else if (code == "R3ME" || code == "R3MP") {
    g.rel = &kR3Release;
    g.paSignature = code == "R3MP" ? kSigPal : kSigUsa;
  } else if (code == "G2MP") {
    error = "PAL Echoes saves (G2MP) are not converted to the Trilogy yet";
    return false;
  } else {
    error = "the card file is not an Echoes save (" + code + ")";
    return false;
  }
  const size_t body = gci.size() - kDirEntry;
  const size_t blocks = body / kBlock;
  if (body % kBlock != 0 || blocks < 3 || (blocks - 1) % 2 != 0) {
    error = "the card file is not a header block and two save copies";
    return false;
  }
  g.copyBytes = (blocks - 1) / 2 * kBlock;
  uint32_t best = 0;
  for (int c = 0; c < 2; ++c) {
    const uint8_t* block = gci.data() + CopyOffset(g, c);
    const uint32_t crc = CardCrc(block + 4, g.copyBytes - 4);
    const bool little = crc == Le32(block);
    if (!little && crc != Be32(block)) {
      continue;
    }
    const uint8_t* user = block + kUserHeader;
    if (std::memcmp(user + 11, "SAVH", 4) != 0) {
      continue;
    }
    uint32_t newest = 0;
    for (int s = 0; s < 3; ++s) {
      const uint8_t* slot = user + kSlotsStart + size_t(s) * kSlotStride;
      if (user[8 + s] != 0 && std::memcmp(slot, "GMST", 4) == 0) {
        newest = std::max(newest, Be32(slot + 4));
      }
    }
    if (g.chosen < 0 || newest > best) {
      g.chosen = c;
      g.little = little;
      best = newest;
    }
  }
  if (g.chosen < 0) {
    error = "the card file has no valid save copy";
    return false;
  }
  return true;
}

// ---- release mapping ----------------------------------------------------------------------------

void Remap(const Bits& src, const int16_t* map, size_t targetCount, Bits& out, int& dropped) {
  if (map == nullptr) {
    out = src;
    out.resize(targetCount, 0);
    return;
  }
  out.assign(targetCount, 0);
  Bits used(src.size(), 0);
  for (size_t t = 0; t < targetCount; ++t) {
    const int s = map[t];
    if (s >= 0 && size_t(s) < src.size()) {
      out[t] = src[size_t(s)];
      used[size_t(s)] = 1;
    }
  }
  for (size_t s = 0; s < src.size(); ++s) {
    if (!used[s] && src[s]) {
      ++dropped;
    }
  }
}

bool Convert(const Model& in, const STriDirection& d, Model& out, ConvertReport& report, std::string& error) {
  const STriRelease& from = *d.from;
  const STriRelease& to = *d.to;
  out = Model();
  out.rel = &to;
  out.time = in.time;
  out.hard = in.hard;
  out.init = in.init;
  out.dark = in.dark;
  out.playHi = in.playHi;
  out.playLo = in.playLo;
  out.prev = in.prev;
  out.escape = in.escape;
  out.gameVars = in.gameVars;
  out.sysBits = in.sysBits;
  out.saveIdx = in.saveIdx;

  out.desired = in.desired;
  for (int t = 0; t < to.worldCount; ++t) {
    if (d.worldSource[t] >= 0 && from.worlds[d.worldSource[t]].mlvl == in.desired) {
      out.desired = to.worlds[t].mlvl;
    }
  }
  for (int p = 0; p < 4; ++p) {
    out.players[p].a = in.players[p].a;
    out.players[p].b = in.players[p].b;
    out.players[p].fixed = in.players[p].fixed;
    out.players[p].tail = in.players[p].tail;
    Remap(in.players[p].scans, d.scans, size_t(to.scanCount), out.players[p].scans, report.droppedScans);
  }
  out.hints.assign(size_t(to.hintCount), Model::Hint());
  Bits usedHints(in.hints.size(), 0);
  for (int t = 0; t < to.hintCount; ++t) {
    const int s = d.hints[t];
    if (s >= 0 && size_t(s) < in.hints.size()) {
      out.hints[size_t(t)] = in.hints[size_t(s)];
      usedHints[size_t(s)] = 1;
    }
  }
  for (size_t s = 0; s < in.hints.size(); ++s) {
    if (!usedHints[s] && in.hints[s].state != 0) {
      ++report.droppedHints;
    }
  }

  out.cines.assign(size_t(to.cinematicBits), 0);
  Bits usedCines(in.cines.size(), 0);
  std::vector<size_t> fromOff(size_t(from.worldCount) + 1, 0);
  for (int i = 0; i < from.worldCount; ++i) {
    fromOff[size_t(i) + 1] = fromOff[size_t(i)] + from.worlds[i].cinematics;
  }
  size_t toOff = 0;
  out.worlds.resize(size_t(to.worldCount));
  for (int t = 0; t < to.worldCount; ++t) {
    const STriWorld& tw = to.worlds[t];
    const int s = d.worldSource[t];
    if (s < 0) {
      error = "world " + N(size_t(t)) + " has no counterpart in the other release";
      return false;
    }
    const STriWorld& fw = from.worlds[s];
    const STriWorldMap& wm = d.worlds[t];
    if (fw.areas != tw.areas) {
      error = "world " + N(size_t(t)) + " has a different area count in the two releases";
      return false;
    }
    const World& src = in.worlds[size_t(s)];
    World& dst = out.worlds[size_t(t)];
    dst.head[0] = src.head[0];
    dst.head[1] = src.head[1];
    dst.areaBits = src.areaBits;
    dst.mapUsed = src.mapUsed;
    Remap(src.relays, wm.relays, tw.relays, dst.relays, report.droppedRelays);
    Remap(src.doors, wm.doors, tw.doors, dst.doors, report.droppedDoors);
    Remap(src.unmappable, wm.unmappable, tw.unmappable, dst.unmappable, report.droppedUnmappable);

    for (int k = 0; k < tw.cinematics; ++k) {
      const int sk = wm.cinematics != nullptr ? wm.cinematics[k] : k;
      if (sk >= 0 && sk < fw.cinematics) {
        out.cines[toOff + size_t(k)] = in.cines[fromOff[size_t(s)] + size_t(sk)];
        usedCines[fromOff[size_t(s)] + size_t(sk)] = 1;
      }
    }
    toOff += size_t(tw.cinematics);

    dst.layers.resize(size_t(tw.areas));
    for (int a = 0; a < fw.areas; ++a) {
      const int fc = fw.layerCounts[a];
      const int tc = tw.layerCounts[a];
      const int8_t* map = nullptr;
      for (int i = 0; i < d.layerMapCount; ++i) {
        if (d.layerMaps[i].world == t && d.layerMaps[i].area == a) {
          map = d.layerMaps[i].map;
          if (d.layerMaps[i].count != tc) {
            error = "layer map of world " + N(size_t(t)) + " area " + N(size_t(a)) + " does not match the target";
            return false;
          }
        }
      }
      const Bits& inl = src.layers[size_t(a)];
      Bits& outl = dst.layers[size_t(a)];
      outl.assign(size_t(tc), 0);
      Bits used(size_t(fc), 0);
      for (int l = 1; l < tc; ++l) {
        const int sl = map != nullptr ? map[l] : (l < fc ? l : -1);
        if (sl > 0 && sl < fc) {
          outl[size_t(l)] = inl[size_t(sl)];
          used[size_t(sl)] = 1;
        } else {
          // A layer the source release has no counterpart of starts as the target's own default.
          outl[size_t(l)] = uint8_t((tw.layerDefaults[a] >> l) & 1);
        }
      }
      for (int l = 1; l < fc; ++l) {
        if (!used[size_t(l)] && inl[size_t(l)]) {
          ++report.droppedLayers;
        }
      }
    }
  }
  for (size_t i = 0; i < in.cines.size(); ++i) {
    if (!usedCines[i] && in.cines[i]) {
      ++report.droppedCinematics;
    }
  }
  return true;
}

} // namespace

int ConvertReport::Total() const {
  return droppedScans + droppedHints + droppedRelays + droppedDoors + droppedUnmappable + droppedLayers +
         droppedCinematics;
}

bool PayloadFromGci(const std::vector<uint8_t>& gci, int slot, uint8_t partA[kPartSize], uint8_t partB[kPartSize],
                    std::string& error, ConvertReport* report) {
  Gci g;
  if (slot < 0 || slot > 2) {
    error = "no such slot";
    return false;
  }
  if (!Locate(gci, g, error)) {
    return false;
  }
  const uint8_t* user = gci.data() + CopyOffset(g, g.chosen) + kUserHeader;
  if (user[8 + slot] == 0) {
    error = "the card's save has no slot " + N(size_t(slot) + 1);
    return false;
  }
  Model m;
  if (!ParseSysBlock(user + kSysOffset, *g.rel, m, error)) {
    return false;
  }
  if (!ParseGcSlot(user + kSlotsStart + size_t(slot) * kSlotStride, *g.rel, m, error)) {
    return false;
  }
  ConvertReport local;
  ConvertReport& rep = report != nullptr ? *report : local;
  rep = ConvertReport();
  Model wii;
  if (g.toR3 != nullptr) {
    if (!Convert(m, *g.toR3, wii, rep, error)) {
      return false;
    }
  } else {
    wii = m;
  }
  return BuildWii(wii, g.paSignature, partA, partB, error);
}

bool GciFromPayload(const Payload& payload, std::vector<uint8_t>& gci, std::string& error, ConvertReport* report) {
  if (gci.empty()) {
    error = "there is no Echoes save on the card to put the Trilogy file into: save once in the port first";
    return false;
  }
  Gci g;
  if (!Locate(gci, g, error)) {
    return false;
  }
  Model wii;
  if (!ParseWii(payload.partA, payload.partB, wii, error)) {
    return false;
  }
  ConvertReport local;
  ConvertReport& rep = report != nullptr ? *report : local;
  rep = ConvertReport();
  Model m;
  if (g.fromR3 != nullptr) {
    if (!Convert(wii, *g.fromR3, m, rep, error)) {
      return false;
    }
  } else {
    m = wii;
  }

  uint8_t* base = gci.data() + CopyOffset(g, g.chosen);
  uint8_t* user = base + kUserHeader;
  if (std::memcmp(user + 11, "SAVH", 4) != 0) {
    error = "the card file has no SAVH header";
    return false;
  }
  // The file goes into the newest present slot (the one an export would have taken), else slot 0.
  int slot = 0;
  uint32_t newest = 0;
  bool present = false;
  for (int s = 0; s < 3; ++s) {
    const uint8_t* p = user + kSlotsStart + size_t(s) * kSlotStride;
    if (user[8 + s] != 0 && std::memcmp(p, "GMST", 4) == 0 && (!present || Be32(p + 4) >= newest)) {
      slot = s;
      newest = Be32(p + 4);
      present = true;
    }
  }
  uint8_t* slotBytes = user + kSlotsStart + size_t(slot) * kSlotStride;

  // What the Trilogy does not store keeps the card's value: the save index, the "initialise powerups
  // at first spawn" bit, and the cinematics the card has seen that the Trilogy file has not (the system
  // block is the card's, shared by its slots).
  Model old;
  if (!ParseSysBlock(user + kSysOffset, *g.rel, old, error)) {
    return false;
  }
  m.saveIdx = old.saveIdx;
  for (size_t i = 0; i < m.cines.size() && i < old.cines.size(); ++i) {
    m.cines[i] |= old.cines[i];
  }
  if (present) {
    Model prev;
    if (ParseGcSlot(slotBytes, *g.rel, prev, error)) {
      m.init = prev.init;
    }
    error.clear();
  }
  std::vector<uint8_t> built;
  if (!BuildGcSlot(m, built, error)) {
    return false;
  }
  std::memcpy(slotBytes, built.data(), built.size());
  BuildSysBlock(m, user + kSysOffset);
  user[8 + slot] = 1;

  // Both copies carry the new data (equal generations), each with its CRC in the byte order the
  // file used.
  std::memcpy(gci.data() + CopyOffset(g, 1 - g.chosen), base, g.copyBytes);
  for (int c = 0; c < 2; ++c) {
    uint8_t* block = gci.data() + CopyOffset(g, c);
    const uint32_t crc = CardCrc(block + 4, g.copyBytes - 4);
    if (g.little) {
      PutLe32(block, crc);
    } else {
      PutBe32(block, crc);
    }
  }
  return true;
}

// The save header's signature the port checks on each disc (docs/SAVE_RELEASES.md §3, console
// order, the value the port writes, as port_save_convert.cpp does); R3MP's is not known, so it has
// no card conversion.
static uint32_t CardSignature(const std::string& code) {
  if (code == "G2ME") {
    return 0x44bfc715u;
  }
  if (code == "R3ME") {
    return 0xfe44ecfeu;
  }
  return 0;
}

bool CanConvertGci(const std::string& from, const std::string& to) {
  return (from == "G2ME" && to == "R3ME") || (from == "R3ME" && to == "G2ME");
}

bool ConvertGci(const std::vector<uint8_t>& in, const std::string& to, std::vector<uint8_t>& out,
                std::string& error, ConvertReport* report) {
  const std::string from = in.size() >= 4 ? std::string(reinterpret_cast< const char* >(in.data()), 4) : "";
  if (!CanConvertGci(from, to)) {
    error = "no conversion from " + from + " to " + to;
    return false;
  }
  Gci g;
  if (!Locate(in, g, error)) {
    return false;
  }
  const STriDirection& d = from == "G2ME" ? kMEtoR3 : kR3toME;
  ConvertReport local;
  ConvertReport& rep = report != nullptr ? *report : local;
  rep = ConvertReport();

  out = in;
  std::memcpy(out.data(), to.data(), 4);
  const uint8_t* src = in.data() + CopyOffset(g, g.chosen);
  uint8_t* base = out.data() + CopyOffset(g, g.chosen);
  uint8_t* user = base + kUserHeader;
  // Every present slot is remapped; the system block (cinematics seen, system variables) is the
  // card's, shared by its slots, and goes over with the first.
  bool converted = false;
  for (int s = 0; s < 3; ++s) {
    uint8_t* slot = user + kSlotsStart + size_t(s) * kSlotStride;
    if (user[8 + s] == 0 || std::memcmp(slot, "GMST", 4) != 0) {
      continue;
    }
    Model m;
    if (!ParseSysBlock(src + kUserHeader + kSysOffset, *g.rel, m, error) ||
        !ParseGcSlot(src + kUserHeader + kSlotsStart + size_t(s) * kSlotStride, *g.rel, m, error)) {
      error = "slot " + N(size_t(s) + 1) + ": " + error;
      return false;
    }
    Model target;
    ConvertReport slotReport;
    if (!Convert(m, d, target, slotReport, error)) {
      return false;
    }
    std::vector<uint8_t> built;
    if (!BuildGcSlot(target, built, error)) {
      return false;
    }
    std::memcpy(slot, built.data(), built.size());
    if (!converted) {
      BuildSysBlock(target, user + kSysOffset);
      rep = slotReport;
      converted = true;
    } else {
      // The system block's cinematics are counted once; the rest per slot.
      rep.droppedScans += slotReport.droppedScans;
      rep.droppedHints += slotReport.droppedHints;
      rep.droppedRelays += slotReport.droppedRelays;
      rep.droppedDoors += slotReport.droppedDoors;
      rep.droppedUnmappable += slotReport.droppedUnmappable;
      rep.droppedLayers += slotReport.droppedLayers;
    }
  }
  if (!converted) {
    error = "the card file has no saved game";
    return false;
  }
  PutBe32(user, CardSignature(to));

  // Both copies carry the converted data with the chosen copy's generation, each with its CRC in
  // the byte order the file used.
  std::memcpy(out.data() + CopyOffset(g, 1 - g.chosen), base, g.copyBytes);
  for (int c = 0; c < 2; ++c) {
    uint8_t* block = out.data() + CopyOffset(g, c);
    const uint32_t crc = CardCrc(block + 4, g.copyBytes - 4);
    if (g.little) {
      PutLe32(block, crc);
    } else {
      PutBe32(block, crc);
    }
  }
  return true;
}

} // namespace PortSave::Trilogy
