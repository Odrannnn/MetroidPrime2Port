// Host test of the Trilogy save.bin container (platform/port_save_trilogy.cpp) against real save.bins
// from the Prime 1 port's fixture set, copied to tests/fixtures/trilogy/. Argument: that directory.
#include "port_save_trilogy.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace PortSave::Trilogy;

namespace {
int sFailures = 0;
void Check(bool ok, const char* what, const std::string& file) {
  if (!ok) {
    std::fprintf(stderr, "FAIL %s: %s\n", file.c_str(), what);
    ++sFailures;
  }
}
std::vector<uint8_t> Load(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

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

// An R3ME card file holding only an empty system block (all cinematics unseen) and no slot: the
// framing GciFromPayload needs. The system block is SYST, 2 index bits, 1030 + 29 zero bits, SYND.
std::vector<uint8_t> SyntheticGci() {
  std::vector<uint8_t> g(0x40 + 3 * 0x2000, 0);
  std::memcpy(g.data(), "R3ME01", 6);
  for (int c = 0; c < 2; ++c) {
    uint8_t* block = g.data() + 0x40 + 0x2000 + size_t(c) * 0x2000;
    uint8_t* user = block + 8;
    std::memcpy(user + 11, "SAVH", 4);
    uint8_t* sys = user + 15;
    std::memcpy(sys, "SYST", 4);
    const size_t at = 32 + 2 + 1030 + 29; // SYND, MSB-first
    for (size_t b = 0; b < 32; ++b) {
      const uint32_t word = 0x53594e44u; // "SYND"
      if ((word >> (31 - b)) & 1) {
        sys[(at + b) / 8] |= uint8_t(0x80 >> ((at + b) % 8));
      }
    }
    const uint32_t crc = CardCrc(block + 4, 0x2000 - 4);
    for (int i = 0; i < 4; ++i) {
      block[i] = uint8_t(crc >> (8 * i)); // little-endian
    }
  }
  return g;
}

// A payload from a fixture, through a gci and back: the Wii's own bytes must survive.
void RoundTripPayloads(const std::vector<uint8_t>& bin, int f, const char* name) {
  Payload p;
  if (!Read(bin.data(), f, p)) {
    return;
  }
  std::string error;
  std::vector<uint8_t> gci = SyntheticGci();
  ConvertReport rep;
  const bool imported = GciFromPayload(p, gci, error, &rep);
  Check(imported, error.c_str(), name);
  if (!imported) {
    return;
  }
  Check(rep.Total() == 0, "an R3ME gci import reports dropped entries", name);
  uint8_t a[kPartSize], b[kPartSize];
  const bool exported = PayloadFromGci(gci, 0, a, b, error);
  Check(exported, error.c_str(), name);
  // Part A's signature is by region (the synthetic card is USA): compare it past the signature.
  Check(!std::memcmp(b, p.partB, kPartSize), "part B does not survive payload -> gci -> payload", name);
  Check(!std::memcmp(a + 4, p.partA + 4, kPartSize - 4), "part A does not survive payload -> gci -> payload", name);
  // The converter is deterministic and idempotent: a second pass changes nothing.
  std::vector<uint8_t> again = gci;
  Payload q;
  std::memcpy(q.partA, a, kPartSize);
  std::memcpy(q.partB, b, kPartSize);
  Check(GciFromPayload(q, again, error) && again == gci, "importing the same payload twice differs", name);

  // The card itself between the discs (ConvertGci). R3ME -> G2ME drops only the Trilogy's own
  // relays, doors and scans (the tables match entries by ID, so no other bit moves): back on R3ME
  // the file differs from the original in no more bits than were reported dropped, besides each
  // copy's CRC and signature words.
  std::vector<uint8_t> me, r3, me2;
  ConvertReport toMe, toR3, toMe2;
  Check(ConvertGci(gci, "G2ME", me, error, &toMe), error.c_str(), name);
  Check(me.size() == gci.size() && !std::memcmp(me.data(), "G2ME", 4), "the G2ME card is not a G2ME file", name);
  Check(ConvertGci(me, "R3ME", r3, error, &toR3), error.c_str(), name);
  int bits = 0;
  for (size_t i = 0; i < gci.size() && i < r3.size(); ++i) {
    const size_t inCopy = i >= 0x2040 ? (i - 0x2040) % 0x2000 : 0x100;
    if (inCopy >= 4 && (inCopy < 8 || inCopy >= 12)) {
      bits += __builtin_popcount(unsigned(r3[i] ^ gci[i]));
    }
  }
  // Both copies carry each dropped bit (one bit per relay, door or scan).
  Check(r3.size() == gci.size() && bits <= 2 * toMe.Total(), "R3ME -> G2ME -> R3ME changed more than it dropped", name);
  // G2ME -> R3ME, the way a GameCube player moves to the Trilogy disc, is exact: nothing dropped,
  // the Trilogy card exports the same Wii payload the GameCube card does, and converting back gives
  // the same file but for the CRC and signature words.
  Check(toR3.Total() == 0, "G2ME -> R3ME drops entries", name);
  uint8_t ma[kPartSize], mb[kPartSize], ra[kPartSize], rb[kPartSize];
  Check(PayloadFromGci(me, 0, ma, mb, error) && PayloadFromGci(r3, 0, ra, rb, error) && !std::memcmp(ma, ra, kPartSize) &&
            !std::memcmp(mb, rb, kPartSize),
        "the G2ME and converted R3ME cards export different Wii payloads", name);
  Check(ConvertGci(r3, "G2ME", me2, error, &toMe2) && toMe2.Total() == 0, "R3ME -> G2ME of a converted card drops entries", name);
  size_t changed = 0;
  for (size_t i = 0; i < me.size() && i < me2.size(); ++i) {
    const size_t inCopy = i >= 0x2040 ? (i - 0x2040) % 0x2000 : 0x100;
    changed += me2[i] != me[i] && inCopy >= 4 && (inCopy < 8 || inCopy >= 12);
  }
  Check(me2.size() == me.size() && changed == 0, "G2ME -> R3ME -> G2ME changed the save", name);
  // The PAL Trilogy (R3MP) has R3ME's tables and signature: G2ME -> R3MP is G2ME -> R3ME but for
  // the game code, and R3ME <-> R3MP changes nothing else.
  std::vector<uint8_t> mp, r3mp;
  ConvertReport toMp, r3ToMp;
  Check(ConvertGci(me, "R3MP", mp, error, &toMp) && toMp.Total() == 0, error.c_str(), name);
  Check(mp.size() == r3.size() && !std::memcmp(mp.data(), "R3MP", 4) && !std::memcmp(mp.data() + 4, r3.data() + 4, mp.size() - 4),
        "G2ME -> R3MP differs from G2ME -> R3ME beyond the game code", name);
  Check(ConvertGci(r3, "R3MP", r3mp, error, &r3ToMp) && r3mp == mp && r3ToMp.Total() == 0, "R3ME -> R3MP changed the save", name);
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <fixture dir>\n", argv[0]);
    return 2;
  }
  int withEchoes = 0;
  for (const char* name : {"01-firstboot", "12-gb-packed", "13-gbatemp-100", "21-pal-marco-100", "23-pal-toxico-perfect"}) {
    const std::string path = std::string(argv[1]) + "/" + name + ".bin";
    const std::vector<uint8_t> bin = Load(path);
    std::string error;
    Check(!bin.empty(), "fixture missing", path);
    if (bin.empty()) {
      continue;
    }
    Check(Validate(bin.data(), bin.size(), error), error.c_str(), name);
    for (int f = 0; f < int(kFiles); ++f) {
      Payload p;
      if (!Read(bin.data(), f, p)) {
        continue;
      }
      ++withEchoes;
      // Writing a file's own payload back changes nothing: slot, summary, constant and CRC agree.
      std::vector<uint8_t> out;
      Check(Write(bin, f, p, out, error), error.c_str(), name);
      Check(out == bin, "own payload written back is not byte-identical", name);
      // And into another file the result is still a valid save.bin that reads back the same.
      for (int g = 0; g < int(kFiles); ++g) {
        if (g == f) {
          continue;
        }
        std::vector<uint8_t> moved;
        Check(Write(bin, g, p, moved, error), error.c_str(), name);
        Check(Validate(moved.data(), moved.size(), error), "moved save fails validation", name);
        Payload q;
        Check(Read(moved.data(), g, q) && !std::memcmp(q.partA, p.partA, kPartSize) &&
                  !std::memcmp(q.partB, p.partB, kPartSize) && q.summary.mlvl == p.summary.mlvl &&
                  q.summary.tanks == p.summary.tanks && q.summary.percent == p.summary.percent &&
                  q.summary.playTime == p.summary.playTime,
              "payload does not read back", name);
        // The other games' parts are untouched: only the slot, summary line, entry and CRC change.
        size_t outside = 0;
        for (size_t i = 4; i < bin.size(); ++i) {
          const bool inSlot = i >= kSlotBase + kSlotSize * g && i < kSlotBase + kSlotSize * (g + 1);
          if (moved[i] != bin[i] && !inSlot) {
            ++outside;
          }
        }
        Check(outside <= 7 + kSummaryBytes + 4, "a write changed bytes outside Echoes' file", name);
      }
      RoundTripPayloads(bin, f, name);
      // A flipped byte in the checksummed range is caught.
      std::vector<uint8_t> bad = bin;
      bad[0x100] ^= 1;
      Check(!Validate(bad.data(), bad.size(), error), "a corrupted save.bin validates", name);
      break;
    }
  }
  // A real card save (the port's own G2ME slot) exported and imported back: identical but for the
  // fields the Trilogy cannot hold. Skipped when the file is not on this machine.
  if (const char* home = std::getenv("HOME")) {
    const std::string gciPath = std::string(home) + "/.cache/mp2-orch/saves/mi_u/USA/Card A/01-G2ME-MetroidPrime2.gci";
    const std::vector<uint8_t> gci = Load(gciPath);
    if (gci.empty()) {
      std::printf("skip: %s not present\n", gciPath.c_str());
    } else {
      for (int slot = 0; slot < 3; ++slot) {
        std::string error;
        uint8_t a[kPartSize], b[kPartSize];
        ConvertReport rep;
        if (!PayloadFromGci(gci, slot, a, b, error, &rep)) {
          std::printf("slot %d: %s\n", slot, error.c_str());
          continue;
        }
        Payload p;
        std::memcpy(p.partA, a, kPartSize);
        std::memcpy(p.partB, b, kPartSize);
        std::vector<uint8_t> back = gci;
        const bool imported = GciFromPayload(p, back, error);
        Check(imported, error.c_str(), "gci");
        // What may differ: the two CRCs, and the bytes of the world the Wii cuts off (its record is 5
        // bytes short in every real Trilogy save, so that world's layers come back at their defaults).
        size_t changed = 0;
        for (size_t i = 0; i < gci.size(); ++i) {
          const size_t inCopy = i >= 0x2040 ? (i - 0x2040) % 0x2000 : 0x100;
          changed += back[i] != gci[i] && inCopy >= 4;
        }
        std::printf("slot %d: dropped %d, %zu byte(s) differ outside the CRCs\n", slot, rep.Total(), changed);
        Check(changed <= 8, "gci -> payload -> gci changed more than the cut-off world", "gci");
        uint8_t a2[kPartSize], b2[kPartSize];
        Check(PayloadFromGci(back, slot, a2, b2, error) && !std::memcmp(a, a2, kPartSize) && !std::memcmp(b, b2, kPartSize),
              "gci -> payload -> gci -> payload is not stable", "gci");
      }
    }
  }
  Check(withEchoes > 0, "no fixture has Echoes progress; the test proved nothing", "fixtures");
  std::printf("%d fixture file(s) with Echoes progress, %d failure(s)\n", withEchoes, sFailures);
  return sFailures == 0 ? 0 : 1;
}
