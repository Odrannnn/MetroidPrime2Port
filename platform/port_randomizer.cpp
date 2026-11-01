#include "port_utf8_path.h"
#include "port_randomizer.h"

#include "port_rando_gen.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace PortRandomizer {
namespace {

struct ItemEntry {
  const char* name;
  int value;
};

// Frozen retail data. The enum source is include/MetroidPrime/Player/CPlayerState.hpp
// (Echoes' EItemType, not Prime 1's; names are the enumerators without the kIT_ prefix).
constexpr ItemEntry kItems[] = {
    {"PowerBeam", 0}, {"DarkBeam", 1}, {"LightBeam", 2}, {"AnnihilatorBeam", 3},
    {"SuperMissile", 4}, {"Darkburst", 5}, {"Sunburst", 6}, {"SonicBoom", 7}, {"CombatVisor", 8},
    {"ScanVisor", 9}, {"DarkVisor", 10}, {"EchoVisor", 11}, {"VariaSuit", 12}, {"DarkSuit", 13},
    {"LightSuit", 14}, {"MorphBall", 15}, {"BoostBall", 16}, {"SpiderBall", 17},
    {"MorphBallBombs", 18}, {"DarkBomb", 19}, {"LightBomb", 20}, {"AnnihilatorBomb", 21},
    {"ChargeBeam", 22}, {"GrappleBeam", 23}, {"SpaceJumpBoots", 24}, {"GravityBoost", 25},
    {"SeekerLauncher", 26}, {"ScrewAttack", 27}, {"TranslatorUpgrade_TempETM", 28},
    {"TempleKey1", 29}, {"TempleKey2", 30}, {"TempleKey3", 31}, {"AgonKey1", 32}, {"AgonKey2", 33},
    {"AgonKey3", 34}, {"TorvusKey1", 35}, {"TorvusKey2", 36}, {"TorvusKey3", 37}, {"HiveKey1", 38},
    {"HiveKey2", 39}, {"HiveKey3", 40}, {"HealthRefill", 41}, {"EnergyTanks", 42},
    {"Powerbomb", 43}, {"Missile", 44}, {"DarkAmmo", 45}, {"LightAmmo", 46},
    {"ItemPercentage", 47}, {"Multiplayer_NumPlayersJoined", 48},
    {"Multiplayer_NumPlayersInOptionsMenu", 49}, {"MiscCounter3", 50},
    {"Multiplayer_Archenemy", 51}, {"SwitchWeaponPower", 52}, {"SwitchWeaponDark", 53},
    {"SwitchWeaponLight", 54}, {"SwitchWeaponAnnihilator", 55}, {"MultiChargeUpgrade", 56},
    {"Invisibility", 57}, {"DoubleDamage", 58}, {"Invincibility", 59}, {"MiscCounter1a", 60},
    {"MiscCounter2a", 61}, {"MiscCounter3a", 62}, {"MiscCounter4a", 63}, {"FragCount", 64},
    {"DiedCount", 65}, {"ArchenemyCount", 66}, {"PersistentCounter1", 67},
    {"PersistentCounter2", 68}, {"PersistentCounter3", 69}, {"PersistentCounter4", 70},
    {"PersistentCounter5", 71}, {"PersistentCounter6", 72}, {"PersistentCounter7", 73},
    {"PersistentCounter8", 74}, {"SwitchVisorCombat", 75}, {"SwitchVisorScan", 76},
    {"SwitchVisorDark", 77}, {"SwitchVisorEcho", 78}, {"CoinAmplifier", 79}, {"CoinCounter", 80},
    {"UnlimitedMissiles", 81}, {"UnlimitedBeamAmmo", 82}, {"DarkShield", 83}, {"LightShield", 84},
    {"AbsorbAttack", 85}, {"DeathBall", 86}, {"ScanVirus", 87}, {"VisorStatic", 88},
    {"BeamWeaponsDisabled", 89}, {"MissileWeaponsDisabled", 90}, {"Unknown_91", 91},
    {"DisableBall", 92}, {"DisableSpaceJump", 93}, {"ActivateMorphballBoost", 94},
    {"HackedEffect", 95}, {"CannonBall", 96}, {"VioletTranslator", 97}, {"AmberTranslator", 98},
    {"EmeraldTranslator", 99}, {"CobaltTranslator", 100}, {"TempleKey4", 101}, {"TempleKey5", 102},
    {"TempleKey6", 103}, {"TempleKey7", 104}, {"TempleKey8", 105}, {"TempleKey9", 106},
    {"EnergyTransferModule", 107}, {"ChargeCombo", 108},
};

struct Placement {
  int itemType = -1;
  int amount = 0;
  int capacity = 0;
  bool hasAmount = false;
  bool hasCapacity = false;
};

struct State {
  std::map<std::string, Placement> placements;
  std::string seedName;
  int checkCount = 0;
  bool enabled = false;
  bool dump = false;
};

State& GetState() {
  static State state;
  return state;
}

std::string UserDirectory() {
  std::string dir;
  // MP2_USER_PATH is what this port's card, settings and logs use (MP_USER_PATH is the Prime 1
  // port's name, kept for a script carried over from there).
  for (const char* name : {"MP2_USER_PATH", "MP_USER_PATH"}) {
    if (const char* env = std::getenv(name)) {
      if (env[0] != '\0') {
        dir = env;
      }
    }
    if (!dir.empty()) {
      break;
    }
  }
  if (dir.empty()) {
    if (char* pref = SDL_GetPrefPath(nullptr, "Metroid Prime")) {
      dir = pref;
      SDL_free(pref);
    } else {
      dir = ".";
    }
  }
  if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
    dir += '/';
  return dir;
}

std::string SeedPath() {
  if (const char* env = std::getenv("MP_RANDO_SEED")) {
    if (env[0] != '\0')
      return env;
  }
  return UserDirectory() + "randomizer_seed.json";
}

std::string LogPath(const char* name) { return UserDirectory() + name; }


bool EnvEnabled(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

struct ParseError {
  size_t offset;
  const char* reason;
};

// Seed grammar: a top-level object may contain "seed": string and
// "locations": object; each location maps to an object with required "item":
// string and optional integer "amount"/"capacity". Unknown keys at either
// level are ignored after validating their values. Values may be strings,
// integers, booleans, or objects; arrays, null, comments, and trailing commas
// are not accepted. Whitespace is insignificant.
class Parser {
  const std::string& mText;
  size_t mPos = 0;

  [[noreturn]] void Fail(const char* reason) const { throw ParseError{mPos, reason}; }

  void SkipWhitespace() {
    while (mPos < mText.size() && (mText[mPos] == ' ' || mText[mPos] == '\t' ||
                                   mText[mPos] == '\r' || mText[mPos] == '\n'))
      ++mPos;
  }

  bool Consume(char c) {
    SkipWhitespace();
    if (mPos < mText.size() && mText[mPos] == c) {
      ++mPos;
      return true;
    }
    return false;
  }

  void Expect(char c) {
    SkipWhitespace();
    if (mPos >= mText.size() || mText[mPos] != c)
      Fail("unexpected token");
    ++mPos;
  }

  static void AppendUtf8(std::string& out, unsigned int codepoint) {
    if (codepoint <= 0x7f) {
      out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
      out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
      out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
      out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
  }

  unsigned int ParseHex4() {
    if (mText.size() - mPos < 4)
      Fail("incomplete unicode escape");
    unsigned int value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = mText[mPos++];
      value <<= 4;
      if (c >= '0' && c <= '9')
        value |= static_cast<unsigned int>(c - '0');
      else if (c >= 'a' && c <= 'f')
        value |= static_cast<unsigned int>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        value |= static_cast<unsigned int>(c - 'A' + 10);
      else
        Fail("invalid unicode escape");
    }
    return value;
  }

  std::string ParseString() {
    SkipWhitespace();
    Expect('"');
    std::string result;
    while (mPos < mText.size()) {
      const unsigned char c = static_cast<unsigned char>(mText[mPos++]);
      if (c == '"')
        return result;
      if (c < 0x20)
        Fail("unescaped control character in string");
      if (c != '\\') {
        result.push_back(static_cast<char>(c));
        continue;
      }
      if (mPos >= mText.size())
        Fail("incomplete string escape");
      const char escape = mText[mPos++];
      switch (escape) {
      case '"': result.push_back('"'); break;
      case '\\': result.push_back('\\'); break;
      case '/': result.push_back('/'); break;
      case 'b': result.push_back('\b'); break;
      case 'f': result.push_back('\f'); break;
      case 'n': result.push_back('\n'); break;
      case 'r': result.push_back('\r'); break;
      case 't': result.push_back('\t'); break;
      case 'u': {
        unsigned int cp = ParseHex4();
        if (cp >= 0xd800 && cp <= 0xdbff) {
          if (mText.size() - mPos < 6 || mText[mPos] != '\\' || mText[mPos + 1] != 'u')
            Fail("missing low surrogate");
          mPos += 2;
          const unsigned int low = ParseHex4();
          if (low < 0xdc00 || low > 0xdfff)
            Fail("invalid low surrogate");
          cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
        } else if (cp >= 0xdc00 && cp <= 0xdfff) {
          Fail("unexpected low surrogate");
        }
        AppendUtf8(result, cp);
        break;
      }
      default: Fail("invalid string escape");
      }
    }
    Fail("unterminated string");
  }

  int ParseInteger() {
    SkipWhitespace();
    const size_t start = mPos;
    if (mPos < mText.size() && mText[mPos] == '-')
      ++mPos;
    if (mPos >= mText.size() || mText[mPos] < '0' || mText[mPos] > '9')
      Fail("expected integer");
    if (mText[mPos] == '0') {
      ++mPos;
      if (mPos < mText.size() && mText[mPos] >= '0' && mText[mPos] <= '9')
        Fail("leading zero in integer");
    } else {
      while (mPos < mText.size() && mText[mPos] >= '0' && mText[mPos] <= '9')
        ++mPos;
    }
    long long value = 0;
    try {
      value = std::stoll(mText.substr(start, mPos - start));
    } catch (...) {
      Fail("integer out of range");
    }
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
      Fail("integer out of range");
    return static_cast<int>(value);
  }

  bool ConsumeLiteral(const char* literal) {
    SkipWhitespace();
    const size_t length = std::strlen(literal);
    if (mText.compare(mPos, length, literal) != 0)
      return false;
    mPos += length;
    return true;
  }

  void SkipValue(int objectDepth = 0) {
    SkipWhitespace();
    if (mPos >= mText.size())
      Fail("expected value");
    if (mText[mPos] == '"') {
      (void)ParseString();
    } else if (mText[mPos] == '{') {
      SkipObject(objectDepth + 1);
    } else if (mText[mPos] == '-' || (mText[mPos] >= '0' && mText[mPos] <= '9')) {
      (void)ParseInteger();
    } else if (!ConsumeLiteral("true") && !ConsumeLiteral("false")) {
      Fail("unsupported value (expected string, integer, boolean, or object)");
    }
  }

  void SkipObject(int depth) {
    if (depth > 128)
      Fail("object nesting limit exceeded");
    Expect('{');
    if (Consume('}'))
      return;
    for (;;) {
      (void)ParseString();
      Expect(':');
      SkipValue(depth);
      if (Consume('}'))
        return;
      Expect(',');
    }
  }

  Placement ParsePlacement() {
    Placement placement;
    bool hasItem = false;
    Expect('{');
    if (!Consume('}')) {
      for (;;) {
        const std::string key = ParseString();
        Expect(':');
        if (key == "item") {
          const std::string name = ParseString();
          placement.itemType = PortRandomizer::ItemFromName(name.c_str());
          if (placement.itemType < 0)
            Fail("unknown item name");
          hasItem = true;
        } else if (key == "amount") {
          placement.amount = ParseInteger();
          placement.hasAmount = true;
        } else if (key == "capacity") {
          placement.capacity = ParseInteger();
          placement.hasCapacity = true;
        } else {
          SkipValue();
        }
        if (Consume('}'))
          break;
        Expect(',');
      }
    }
    if (!hasItem)
      Fail("placement is missing required item");
    return placement;
  }

  void ParseLocations(std::map<std::string, Placement>& placements) {
    Expect('{');
    if (Consume('}'))
      return;
    for (;;) {
      std::string key = ParseString();
      Expect(':');
      Placement placement = ParsePlacement();
      placements[std::move(key)] = placement;
      if (Consume('}'))
        return;
      Expect(',');
    }
  }

public:
  explicit Parser(const std::string& text) : mText(text) {}

  void Parse(std::string& seedName, std::map<std::string, Placement>& placements) {
    Expect('{');
    if (!Consume('}')) {
      for (;;) {
        const std::string key = ParseString();
        Expect(':');
        if (key == "seed") {
          seedName = ParseString();
        } else if (key == "locations") {
          ParseLocations(placements);
        } else {
          SkipValue();
        }
        if (Consume('}'))
          break;
        Expect(',');
      }
    }
    SkipWhitespace();
    if (mPos != mText.size())
      Fail("trailing data");
  }
};

bool LoadSeed(State& state) {
  const std::string path = SeedPath();
  std::ifstream input(PortUtf8::FromUtf8(path), std::ios::binary);
  if (!input)
    return false;
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (input.bad()) {
    std::fprintf(stderr, "randomizer: seed read error at byte offset 0: could not read file\n");
    return false;
  }

  std::string seedName;
  std::map<std::string, Placement> placements;
  try {
    Parser(text).Parse(seedName, placements);
  } catch (const ParseError& error) {
    std::fprintf(stderr, "randomizer: seed parse error at byte offset %zu: %s\n", error.offset,
                 error.reason);
    return false;
  }

  state.seedName = std::move(seedName);
  state.placements = std::move(placements);
  state.enabled = !state.placements.empty();
  return state.enabled;
}

void AppendLog(const char* fileName, const char* line) noexcept {
  try {
    const std::string path = LogPath(fileName);
    std::ofstream output(PortUtf8::FromUtf8(path), std::ios::app);
    if (output)
      output << line;
  } catch (...) {
  }
}

} // namespace

// Exported for the F1 overlay's Randomizer page.
std::string SeedFilePath() { return SeedPath(); }

std::string DumpLogFilePath() { return LogPath("randomizer_locations.log"); }

bool GenerateSeed(const char* seedName, std::string& error) {
  error.clear();
  if (seedName == nullptr || seedName[0] == '\0') {
    error = "Give the seed a name first.";
    return false;
  }
  if (!GenerateSeedFile(seedName, SeedPath())) {
    error = "No seed was written: no beatable placement, or the seed file could not be written (see the log).";
    return false;
  }
  return true;
}


void EnsureLoaded() {
  try {
    static const bool loaded = []() noexcept {
      try {
        State& state = GetState();
        state.dump = EnvEnabled("MP_RANDO_DUMP");
        // MP_RANDO_GEN=<seed name>: generate the seed (logic fill, no dump needed), then load it.
        if (const char* gen = std::getenv("MP_RANDO_GEN")) {
          if (gen[0] != '\0' && !state.dump)
            (void)GenerateSeedFile(gen, SeedPath());
        }
        (void)LoadSeed(state);
      } catch (const ParseError& error) {
        State& state = GetState();
        state.enabled = false;
        state.placements.clear();
        state.seedName.clear();
        std::fprintf(stderr, "randomizer: seed parse error at byte offset %zu: %s\n", error.offset,
                     error.reason);
      } catch (...) {
        try {
          State& state = GetState();
          state.enabled = false;
          state.placements.clear();
          state.seedName.clear();
        } catch (...) {
        }
      }
      return true;
    }();
    (void)loaded;
    // Announce the configuration once so a run tells the user whether a seed
    // loaded. Silent when the randomizer is inactive, so normal runs are clean.
    static const bool announced = [] {
      const State& state = GetState();
      if (state.dump) {
        std::fprintf(stderr, "randomizer: dump mode (MP_RANDO_DUMP)\n");
      } else if (state.enabled) {
        std::fprintf(stderr, "randomizer: seed '%s', %zu placements\n", state.seedName.c_str(),
                     state.placements.size());
      }
      return true;
    }();
    (void)announced;
  } catch (...) {
    // Includes function-local static initialization failures: the game hook must not unwind.
  }
}

bool Enabled() {
  EnsureLoaded();
  try {
    return GetState().enabled;
  } catch (...) {
    return false;
  }
}

bool DumpEnabled() {
  EnsureLoaded();
  try {
    return GetState().dump;
  } catch (...) {
    return false;
  }
}

const char* SeedName() {
  EnsureLoaded();
  try {
    return GetState().seedName.c_str();
  } catch (...) {
    return "";
  }
}

int CheckCount() {
  EnsureLoaded();
  try {
    return GetState().checkCount;
  } catch (...) {
    return 0;
  }
}

const char* StatusText() {
  EnsureLoaded();
  static char status[48];
  try {
    const State& state = GetState();
    if (state.dump) {
      std::snprintf(status, sizeof(status), "rando: dump mode");
    } else if (state.enabled) {
      std::snprintf(status, sizeof(status), "rando: %.20s, %d checks", state.seedName.c_str(),
                    state.checkCount);
    } else {
      std::snprintf(status, sizeof(status), "rando: off");
    }
  } catch (...) {
    std::snprintf(status, sizeof(status), "rando: off");
  }
  return status;
}

bool ApplyPickup(uint32_t worldAssetId, uint32_t areaAssetId, uint32_t entityId, int& itemType,
                 int& capacity, int& amount) {
  EnsureLoaded();
  try {
    State& state = GetState();
    char key[32];
    FormatLocationKey(worldAssetId, areaAssetId, entityId, key, sizeof(key));
    if (state.dump) {
      char line[128];
      std::snprintf(line, sizeof(line), "LOC %s %s amount=%d capacity=%d\n", key,
                    ItemName(itemType), amount, capacity);
      AppendLog("randomizer_locations.log", line);
      return false;
    }
    if (!state.enabled)
      return false;
    const auto it = state.placements.find(key);
    if (it == state.placements.end())
      return false;
    itemType = it->second.itemType;
    if (it->second.hasAmount)
      amount = it->second.amount;
    if (it->second.hasCapacity)
      capacity = it->second.capacity;
    return true;
  } catch (...) {
    return false;
  }
}

bool Placed(uint32_t worldAssetId, uint32_t areaAssetId, uint32_t entityId, int& itemType,
            int& capacity) {
  EnsureLoaded();
  try {
    State& state = GetState();
    if (!state.enabled)
      return false;
    char key[32];
    FormatLocationKey(worldAssetId, areaAssetId, entityId, key, sizeof(key));
    const auto it = state.placements.find(key);
    if (it == state.placements.end())
      return false;
    itemType = it->second.itemType;
    if (it->second.hasCapacity)
      capacity = it->second.capacity;
    return true;
  } catch (...) {
    return false;
  }
}

// The game's own wording for the pickup memo; the rest are the enumerator split at its capitals.
static std::string DisplayName(int itemType) {
  switch (itemType) {
  case 18:
    return "Morph Ball Bomb";
  case 28:
  case 107:
    return "Energy Transfer Module";
  case 29: case 30: case 31: case 101: case 102: case 103: case 104: case 105: case 106:
    return "Sky Temple Key";
  case 32: case 33: case 34:
    return "Dark Agon Key";
  case 35: case 36: case 37:
    return "Dark Torvus Key";
  case 38: case 39: case 40:
    return "Ing Hive Key";
  case 42:
    return "Energy Tank";
  case 43:
    return "Power Bomb Expansion";
  case 44:
    return "Missile Expansion";
  case 45:
  case 46:
    return "Beam Ammo Expansion";
  case 73:
    return "Missile Launcher";
  default:
    break;
  }
  std::string out;
  for (const char* c = ItemName(itemType); *c != '\0'; ++c) {
    if (*c == '_') {
      out += ' ';
      continue;
    }
    if (!out.empty() && std::isupper(static_cast< unsigned char >(*c)) && out.back() != ' ')
      out += ' ';
    out += *c;
  }
  return out;
}

bool AcquisitionText(uint32_t worldAssetId, uint32_t areaAssetId, uint32_t entityId, int itemType,
                     std::string& out) {
  EnsureLoaded();
  try {
    State& state = GetState();
    if (!state.enabled || state.dump)
      return false;
    char key[32];
    FormatLocationKey(worldAssetId, areaAssetId, entityId, key, sizeof(key));
    const auto it = state.placements.find(key);
    if (it == state.placements.end())
      return false;
    out = DisplayName(itemType) + " acquired!";
    return true;
  } catch (...) {
    return false;
  }
}

static std::set< uint16_t > gSuppressedMemos;
void SuppressMemoOnce(uint16_t uniqueId) { gSuppressedMemos.insert(uniqueId); }
bool TakeMemoSuppressed(uint16_t uniqueId) { return gSuppressedMemos.erase(uniqueId) != 0; }

void RecordCheck(uint32_t worldAssetId, uint32_t areaAssetId, uint32_t entityId, int itemType) {
  EnsureLoaded();
  try {
    State& state = GetState();
    if (!state.enabled && !state.dump)
      return;
    if (state.checkCount < std::numeric_limits<int>::max())
      ++state.checkCount;
    char key[32];
    FormatLocationKey(worldAssetId, areaAssetId, entityId, key, sizeof(key));
    char line[128];
    std::snprintf(line, sizeof(line), "CHECK %s %s\n", key, ItemName(itemType));
    AppendLog("randomizer_checks.log", line);
  } catch (...) {
  }
}

const char* ItemName(int itemType) {
  for (const ItemEntry& item : kItems) {
    if (item.value == itemType)
      return item.name;
  }
  return "Unknown";
}

int ItemFromName(const char* name) {
  if (name == nullptr)
    return -1;
  for (const ItemEntry& item : kItems) {
    const unsigned char* left = reinterpret_cast<const unsigned char*>(name);
    const unsigned char* right = reinterpret_cast<const unsigned char*>(item.name);
    while (*left != '\0' && *right != '\0' &&
           std::tolower(*left) == std::tolower(*right)) {
      ++left;
      ++right;
    }
    if (*left == '\0' && *right == '\0')
      return item.value;
  }
  return -1;
}

// FormatLocationKey is in port_rando_gen.cpp, shared with the generator's tests.

} // namespace PortRandomizer
