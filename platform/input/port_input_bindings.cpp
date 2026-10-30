#include "port_input_bindings.h"
#include "port_strings.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <type_traits>
#include <unordered_set>

namespace PortInput {

namespace {

KeyNameFn sKeyName = nullptr;
KeyParseFn sKeyParse = nullptr;

constexpr std::string_view kMouseNames[] = {"left", "middle", "right", "x1", "x2"};
constexpr int kMouseNameCount = int(std::size(kMouseNames));

// SDL_GamepadButton order (SDL3: 16..19 = right paddle 1, left paddle 1, right
// paddle 2, left paddle 2).
constexpr std::string_view kPadButtonNames[] = {
    "a",          "b",           "x",          "y",          "back",     "guide",    "start",
    "leftstick",  "rightstick",  "leftshoulder", "rightshoulder", "dpup", "dpdown",  "dpleft",
    "dpright",    "misc1",       "paddle1",    "paddle2",    "paddle3",  "paddle4",  "touchpad",
    "misc2",      "misc3",       "misc4",      "misc5",      "misc6",
};
constexpr std::string_view kPadAxisNames[] = {"leftx", "lefty", "rightx", "righty", "lefttrigger",
                                              "righttrigger"};
constexpr std::string_view kGyroNames[] = {"pitch", "yaw", "roll"};
// Touch codes: the overlay's control ids (TouchControlsView.CONTROL_IDS, the names
// its saved layout uses), then the overlay's own signals at 32..34, its Morph Ball
// page's buttons at 40..44, then the buttons added in its layout editor at 64..127
// (their layout ids, k0..k63).
constexpr std::string_view kTouchNames[] = {
    "lstick", "cstick", "dpad",    "a",     "b",    "x",   "y",          "l",          "r",
    "z",      "visor",  "beam",    "start", "menu", "map", "eye",        "rstick",     "jump",
    "fire",   "morph",  "missile", "lt",    "lb",   "rt",  "rb",         "tz",         "tr",
    "turbo",  "tturbo", "",        "",      "",     "beam_shift", "turbo_held", "map_tap",
    "", "", "", "", "", "bbomb", "bboost", "bpbomb", "bspider", "bunmorph",
    "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "k0",
    "k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8", "k9", "k10",
    "k11", "k12", "k13", "k14", "k15", "k16", "k17", "k18", "k19", "k20",
    "k21", "k22", "k23", "k24", "k25", "k26", "k27", "k28", "k29", "k30",
    "k31", "k32", "k33", "k34", "k35", "k36", "k37", "k38", "k39", "k40",
    "k41", "k42", "k43", "k44", "k45", "k46", "k47", "k48", "k49", "k50",
    "k51", "k52", "k53", "k54", "k55", "k56", "k57", "k58", "k59", "k60",
    "k61", "k62", "k63",
};

// Mouse code bases (see the header): aim 0, SDL state 8, menu 16, held 24.
constexpr int kMouseAim = 0, kMouseState = 8, kMouseMenu = 16, kMouseHeld = 24;

constexpr std::string_view kContextNames[16] = {
    "gameplay", "morphball", "map", "menu", "scan", "", "", "",
    "layer1",   "layer2",    "layer3", "layer4", "", "", "", "",
};

constexpr std::string_view kTriggerNames[] = {"press", "tap", "hold", "double_tap", "toggle"};
constexpr std::string_view kFamilyNames[] = {"keyboard", "pad", "touch"};

bool EqualsNoCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}

using port::StartsWith;

// A line of the bindings file without the spaces, tabs and carriage return around it.
std::string_view Trim(std::string_view s) { return port::TrimView(s, " \t\r"); }

// A whole decimal number (no sign) into out.
bool ParseUInt(std::string_view s, long& out) {
  if (s.empty() || s.size() > 9)
    return false;
  long v = 0;
  for (char c : s) {
    if (c < '0' || c > '9')
      return false;
    v = v * 10 + (c - '0');
  }
  out = v;
  return true;
}

template <size_t N>
int IndexOf(const std::string_view (&names)[N], std::string_view name) {
  for (size_t i = 0; i < N; ++i)
    if (!names[i].empty() && names[i] == name)
      return int(i);
  return -1;
}

// A name from a table, or a number; -1 if neither.
template <size_t N>
long NameOrNumber(const std::string_view (&names)[N], std::string_view s) {
  int i = IndexOf(names, s);
  if (i >= 0)
    return i;
  long v;
  return ParseUInt(s, v) ? v : -1;
}

template <size_t N>
std::string NameOf(const std::string_view (&names)[N], int code) {
  if (code >= 0 && size_t(code) < N && !names[code].empty())
    return std::string(names[code]);
  return std::to_string(code);
}

// Splits a trailing +/- off an axis name.
int8_t TakeDir(std::string_view& s) {
  if (!s.empty() && s.back() == '+') {
    s.remove_suffix(1);
    return 1;
  }
  if (!s.empty() && s.back() == '-') {
    s.remove_suffix(1);
    return -1;
  }
  return 0;
}

const char* DirSuffix(int8_t dir) { return dir > 0 ? "+" : dir < 0 ? "-" : ""; }

std::string MouseText(int code) {
  const char* prefix = "";
  int base = kMouseState;
  if (code < kMouseState) {
    prefix = "aim:";
    base = kMouseAim;
  } else if (code >= kMouseHeld) {
    prefix = "held:";
    base = kMouseHeld;
  } else if (code >= kMouseMenu) {
    prefix = "menu:";
    base = kMouseMenu;
  }
  int b = code - base;
  if (b >= kMouseNameCount)
    return std::to_string(code); // a raw code
  return std::string(prefix) + std::string(kMouseNames[b]);
}

long MouseFromText(std::string_view s) {
  int base = kMouseState;
  if (StartsWith(s, "aim:")) {
    base = kMouseAim;
    s.remove_prefix(4);
  } else if (StartsWith(s, "menu:")) {
    base = kMouseMenu;
    s.remove_prefix(5);
  } else if (StartsWith(s, "held:")) {
    base = kMouseHeld;
    s.remove_prefix(5);
  } else {
    long v;
    if (ParseUInt(s, v))
      return v; // a raw code
  }
  int b = IndexOf(kMouseNames, s);
  return b < 0 ? -1 : base + b;
}

int CodeLimit(Device d) {
  switch (d) {
  case Device::Key:
    return kKeyCount;
  case Device::MouseButton:
    return kMouseButtonCount;
  case Device::MouseWheel:
  case Device::MouseMotion:
    return 2;
  case Device::PadButton:
    return kPadButtonCount;
  case Device::PadAxis:
    return kPadAxisCount;
  case Device::Touch:
    return kTouchCount;
  case Device::TouchAxis:
    return kTouchAxisCount;
  case Device::Gyro:
    return 3;
  case Device::None:
    break;
  }
  return 0;
}

// ---- The TOML subset ----

struct Value {
  enum Kind { String, Int, Float, Bool, Array } kind = String;
  std::string str;
  long long i = 0;
  double f = 0;
  bool b = false;
  std::vector<std::string> array;
};

// A quoted string at s[pos] ('"'); pos ends past the closing quote.
bool ParseString(std::string_view s, size_t& pos, std::string& out) {
  if (pos >= s.size() || s[pos] != '"')
    return false;
  ++pos;
  out.clear();
  while (pos < s.size()) {
    char c = s[pos++];
    if (c == '"')
      return true;
    if (c == '\\') {
      if (pos >= s.size())
        return false;
      char e = s[pos++];
      if (e != '"' && e != '\\')
        return false;
      out += e;
    } else {
      out += c;
    }
  }
  return false;
}

void SkipSpace(std::string_view s, size_t& pos) {
  while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t'))
    ++pos;
}

// The rest of the line after the value may be blank or a comment.
bool AtEnd(std::string_view s, size_t pos) {
  SkipSpace(s, pos);
  return pos >= s.size() || s[pos] == '#';
}

bool ParseValue(std::string_view s, Value& out) {
  size_t pos = 0;
  SkipSpace(s, pos);
  if (pos >= s.size())
    return false;
  if (s[pos] == '"') {
    out.kind = Value::String;
    return ParseString(s, pos, out.str) && AtEnd(s, pos);
  }
  if (s[pos] == '[') {
    out.kind = Value::Array;
    out.array.clear();
    ++pos;
    for (;;) {
      SkipSpace(s, pos);
      if (pos < s.size() && s[pos] == ']') {
        ++pos;
        return AtEnd(s, pos);
      }
      std::string item;
      if (!ParseString(s, pos, item))
        return false;
      out.array.push_back(std::move(item));
      SkipSpace(s, pos);
      if (pos < s.size() && s[pos] == ',') {
        ++pos;
        continue;
      }
      if (pos < s.size() && s[pos] == ']') {
        ++pos;
        return AtEnd(s, pos);
      }
      return false;
    }
  }
  // A bare word: up to a comment.
  std::string_view word = s.substr(pos);
  if (size_t hash = word.find('#'); hash != std::string_view::npos)
    word = word.substr(0, hash);
  word = Trim(word);
  if (word == "true" || word == "false") {
    out.kind = Value::Bool;
    out.b = word == "true";
    return true;
  }
  std::string text(word);
  if (text.empty())
    return false;
  const char* begin = text.c_str();
  char* end = nullptr;
  bool isFloat = text.find_first_of(".eE") != std::string::npos ||
                 text == "inf" || text == "nan";
  errno = 0;
  if (isFloat) {
    out.kind = Value::Float;
    out.f = std::strtod(begin, &end);
  } else {
    out.kind = Value::Int;
    out.i = std::strtoll(begin, &end, 10);
  }
  if (isFloat && !std::isfinite(out.f))
    return false;
  return errno == 0 && end == begin + text.size();
}

std::string Quote(std::string_view s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\')
      out += '\\';
    out += c;
  }
  out += '"';
  return out;
}

std::string FloatText(float f) {
  if (!std::isfinite(f))
    f = 1.0f;
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.9g", double(f));
  std::string s = buf;
  if (s.find_first_of(".eE") == std::string::npos)
    s += ".0";
  return s;
}

std::string UnbindText(const Unbind& u) {
  return ActionKey(u.action) + "@" + std::string(kFamilyNames[int(u.family)]);
}

bool UnbindFromText(std::string_view s, Unbind& out) {
  size_t at = s.rfind('@');
  if (at == std::string_view::npos)
    return false;
  Action a = ActionFromKey(s.substr(0, at));
  int f = IndexOf(kFamilyNames, s.substr(at + 1));
  if (a == Action::None || f < 0)
    return false;
  out = {a, Family(f)};
  return true;
}

} // namespace

Family FamilyOf(Device device) {
  switch (device) {
  case Device::PadButton:
  case Device::PadAxis:
  case Device::Gyro:
    return Family::Pad;
  case Device::Touch:
  case Device::TouchAxis:
    return Family::Touch;
  default:
    return Family::Keyboard;
  }
}

void SetKeyNames(KeyNameFn name, KeyParseFn parse) {
  sKeyName = name;
  sKeyParse = parse;
}

std::string InputToText(const Input& in) {
  std::string s;
  switch (in.device) {
  case Device::None:
    return "none";
  case Device::Key: {
    std::string name = sKeyName ? sKeyName(in.code) : std::string();
    s = "key:" + (name.empty() ? std::to_string(in.code) : name);
    break;
  }
  case Device::MouseButton:
    s = "mouse:" + MouseText(in.code);
    break;
  case Device::MouseWheel:
    if (in.code == 0 && in.dir != 0)
      s = in.dir > 0 ? "wheel:up" : "wheel:down";
    else if (in.code == 1 && in.dir != 0)
      s = in.dir > 0 ? "wheel:right" : "wheel:left";
    else
      s = "wheel:" + std::to_string(in.code) + DirSuffix(in.dir);
    break;
  case Device::MouseMotion:
    s = std::string("motion:") + (in.code == 0 ? "x" : in.code == 1 ? "y" : std::to_string(in.code)) +
        DirSuffix(in.dir);
    break;
  case Device::PadButton:
    s = "pad:" + NameOf(kPadButtonNames, in.code);
    break;
  case Device::PadAxis:
    s = "axis:" + NameOf(kPadAxisNames, in.code) + DirSuffix(in.dir);
    break;
  case Device::Touch:
    s = "touch:" + NameOf(kTouchNames, in.code);
    break;
  case Device::TouchAxis:
    s = "touchaxis:" + std::to_string(in.code) + DirSuffix(in.dir);
    break;
  case Device::Gyro:
    s = "gyro:" + NameOf(kGyroNames, in.code) + DirSuffix(in.dir);
    break;
  }
  if (in.threshold != 50)
    s += "@" + std::to_string(in.threshold);
  return s;
}

bool InputFromText(std::string_view text, Input& out) {
  text = Trim(text);
  Input in;
  // "@<number>" is a threshold; any other '@' is part of the name (a key's).
  if (size_t at = text.rfind('@'); at != std::string_view::npos) {
    long t;
    if (ParseUInt(text.substr(at + 1), t)) {
      if (t > 100)
        return false;
      in.threshold = uint8_t(t);
      text = text.substr(0, at);
    }
  }
  size_t colon = text.find(':');
  if (colon == std::string_view::npos)
    return false;
  std::string_view kind = text.substr(0, colon);
  std::string_view rest = text.substr(colon + 1);
  if (rest.empty())
    return false;
  long code = -1;
  if (kind == "key") {
    in.device = Device::Key;
    // A name first: key:1 is the "1" key, not scancode 1.
    if (sKeyParse)
      code = sKeyParse(rest);
    long v;
    if (code < 0 && ParseUInt(rest, v))
      code = v;
  } else if (kind == "mouse") {
    in.device = Device::MouseButton;
    code = MouseFromText(rest);
  } else if (kind == "wheel") {
    in.device = Device::MouseWheel;
    if (rest == "up" || rest == "down") {
      code = 0;
      in.dir = rest == "up" ? 1 : -1;
    } else if (rest == "right" || rest == "left") {
      code = 1;
      in.dir = rest == "right" ? 1 : -1;
    } else {
      in.dir = TakeDir(rest);
      long v;
      if (ParseUInt(rest, v))
        code = v;
    }
  } else if (kind == "motion") {
    in.device = Device::MouseMotion;
    in.dir = TakeDir(rest);
    code = rest == "x" ? 0 : rest == "y" ? 1 : -1;
    long v;
    if (code < 0 && ParseUInt(rest, v))
      code = v;
  } else if (kind == "pad") {
    in.device = Device::PadButton;
    code = NameOrNumber(kPadButtonNames, rest);
  } else if (kind == "axis") {
    in.device = Device::PadAxis;
    in.dir = TakeDir(rest);
    code = NameOrNumber(kPadAxisNames, rest);
  } else if (kind == "touch" || kind == "touchaxis") {
    in.device = kind == "touch" ? Device::Touch : Device::TouchAxis;
    if (in.device == Device::TouchAxis) {
      in.dir = TakeDir(rest);
      long v;
      if (ParseUInt(rest, v))
        code = v;
    } else {
      code = NameOrNumber(kTouchNames, rest);
    }
  } else if (kind == "gyro") {
    in.device = Device::Gyro;
    in.dir = TakeDir(rest);
    code = NameOrNumber(kGyroNames, rest);
  } else {
    return false;
  }
  if (code < 0 || code >= CodeLimit(in.device))
    return false;
  in.code = uint16_t(code);
  out = in;
  return true;
}

std::string ContextsToText(uint16_t contexts) {
  if (contexts == kCtxAll)
    return "all";
  std::string s;
  for (int bit = 0; bit < 16; ++bit) {
    if (!(contexts & (1u << bit)))
      continue;
    if (!s.empty())
      s += ',';
    s += kContextNames[bit].empty() ? "bit" + std::to_string(bit) : std::string(kContextNames[bit]);
  }
  return s;
}

bool ContextFromText(std::string_view text, uint16_t& out) {
  text = Trim(text);
  if (text == "all") {
    out = kCtxAll;
    return true;
  }
  if (StartsWith(text, "bit")) {
    long v;
    if (ParseUInt(text.substr(3), v) && v < 16) {
      out = uint16_t(1u << v);
      return true;
    }
    return false;
  }
  int bit = IndexOf(kContextNames, text);
  if (bit < 0)
    return false;
  out = uint16_t(1u << bit);
  return true;
}

bool ParseUserBindings(std::string_view text, UserBindings& out, std::string* error, int* dropped) {
  UserBindings result;
  enum class Table { None, Profile, Bind, Custom } table = Table::None;
  // Per bind: whether inputs was given (required).
  bool bindHasInputs = false;
  bool bindHasAction = false;
  bool bindLegacy = false; // beam_shift: dropped when the bind closes
  int legacyDropped = 0;
  int bindLine = 0;
  // Per custom action: whether id was given (required).
  bool customHasId = false;
  int customLine = 0;
  int lineNo = 0;
  std::string err;

  auto fail = [&](int line, const std::string& what) {
    err = "line " + std::to_string(line) + ": " + what;
    return false;
  };
  auto closeBind = [&]() {
    if (table != Table::Bind)
      return true;
    if (bindLegacy) {
      result.profiles.back().bindings.pop_back();
      bindLegacy = false;
      ++legacyDropped;
      return true;
    }
    if (!bindHasAction)
      return fail(bindLine, "bind without an action");
    if (!bindHasInputs)
      return fail(bindLine, "bind without inputs");
    return true;
  };
  auto closeCustom = [&]() {
    if (table != Table::Custom)
      return true;
    if (!customHasId)
      return fail(customLine, "custom_action without an id");
    return true;
  };

  if (StartsWith(text, "\xEF\xBB\xBF")) // a UTF-8 BOM (Windows editors)
    text.remove_prefix(3);
  auto run = [&]() {
    size_t pos = 0;
    while (pos <= text.size()) {
      size_t nl = text.find('\n', pos);
      std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
      pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
      ++lineNo;
      line = Trim(line);
      if (line.empty() || line.front() == '#')
        continue;

      if (line.front() == '[') {
        std::string_view header = line;
        if (size_t hash = header.find('#'); hash != std::string_view::npos)
          header = Trim(header.substr(0, hash));
        if (!closeBind())
          return false;
        if (!closeCustom())
          return false;
        if (header == "[[custom_action]]") {
          result.customActions.emplace_back();
          table = Table::Custom;
          customHasId = false;
          customLine = lineNo;
        } else if (header == "[[profile]]") {
          result.profiles.emplace_back();
          table = Table::Profile;
        } else if (header == "[[profile.bind]]") {
          if (result.profiles.empty())
            return fail(lineNo, "[[profile.bind]] before any [[profile]]");
          result.profiles.back().bindings.emplace_back();
          table = Table::Bind;
          bindHasInputs = bindHasAction = bindLegacy = false;
          bindLine = lineNo;
        } else {
          return fail(lineNo, "unknown table " + std::string(header));
        }
        continue;
      }

      size_t eq = line.find('=');
      if (eq == std::string_view::npos)
        return fail(lineNo, "expected key = value");
      std::string key(Trim(line.substr(0, eq)));
      Value v;
      if (!ParseValue(line.substr(eq + 1), v))
        return fail(lineNo, "bad value for " + key);
      if (table == Table::None)
        return fail(lineNo, key + " outside a table");

      auto wantKind = [&](Value::Kind k) {
        if (v.kind == k)
          return true;
        // An integer is a fine float.
        if (k == Value::Float && v.kind == Value::Int) {
          v.f = double(v.i);
          return true;
        }
        return fail(lineNo, key + " has the wrong type");
      };
      auto intField = [&](auto& field, long long lo, long long hi) {
        if (!wantKind(Value::Int))
          return false;
        if (v.i < lo || v.i > hi)
          return fail(lineNo, key + " out of range");
        field = static_cast<std::remove_reference_t<decltype(field)>>(v.i);
        return true;
      };

      if (table == Table::Custom) {
        CustomActionDef& c = result.customActions.back();
        if (key == "id") {
          if (!wantKind(Value::Int))
            return false;
          if (v.i < 1 || v.i > kMaxCustomId)
            return fail(lineNo, "id out of range");
          const uint16_t id = uint16_t(v.i);
          for (const CustomActionDef& other : result.customActions) {
            if (other.id == id)
              return fail(lineNo, "duplicate custom action id " + std::to_string(id));
          }
          c.id = id;
          customHasId = true;
        } else if (key == "name") {
          if (!wantKind(Value::String))
            return false;
          c.name = v.str;
        } else if (key == "renamed") {
          if (!wantKind(Value::Bool))
            return false;
          c.renamed = v.b;
        } else if (key == "preset") {
          if (!wantKind(Value::String))
            return false;
          c.preset = v.str;
        } else if (key == "arg") {
          if (!wantKind(Value::String))
            return false;
          c.arg = v.str;
        } else if (key == "mode") {
          if (!wantKind(Value::String))
            return false;
          c.mode = v.str;
        } else if (key == "command") {
          if (!wantKind(Value::String))
            return false;
          c.command = v.str;
        } else {
          return fail(lineNo, "unknown custom_action field " + key);
        }
        continue;
      }

      if (table == Table::Profile) {
        UserProfile& p = result.profiles.back();
        if (key == "name") {
          if (!wantKind(Value::String))
            return false;
          p.name = v.str;
        } else if (key == "match") {
          if (!wantKind(Value::String))
            return false;
          p.match = v.str;
        } else if (key == "replace") {
          if (!wantKind(Value::Bool))
            return false;
          p.replace = v.b;
        } else if (key == "chord_window_ms") {
          if (!intField(p.chordWindowMs, 0, 1000))
            return false;
        } else if (key == "unbind") {
          if (!wantKind(Value::Array))
            return false;
          p.unbind.clear();
          for (const std::string& item : v.array) {
            Unbind u;
            if (item.rfind("beam_shift@", 0) == 0) {
              ++legacyDropped;
              continue;
            }
            if (!UnbindFromText(item, u))
              return fail(lineNo, "bad unbind " + item);
            p.unbind.push_back(u);
          }
        } else {
          return fail(lineNo, "unknown profile field " + key);
        }
        continue;
      }

      Binding& b = result.profiles.back().bindings.back();
      if (key == "action") {
        if (!wantKind(Value::String))
          return false;
        Action a = ActionFromKey(v.str);
        if (a == Action::None && v.str == "beam_shift") {
          bindLegacy = true;
        } else if (a == Action::None) {
          return fail(lineNo, "unknown action " + v.str);
        }
        b.action = a;
        bindHasAction = true;
      } else if (key == "inputs") {
        if (!wantKind(Value::Array))
          return false;
        if (v.array.empty())
          return fail(lineNo, "inputs is empty");
        if (v.array.size() > size_t(kMaxChord))
          return fail(lineNo, "more than " + std::to_string(kMaxChord) + " inputs");
        b.inputs = {};
        b.count = 0;
        for (const std::string& item : v.array) {
          Input in;
          if (!InputFromText(item, in))
            return fail(lineNo, "bad input " + item);
          b.inputs[b.count++] = in;
        }
        bindHasInputs = true;
      } else if (key == "any_order") {
        if (!wantKind(Value::Bool))
          return false;
        b.anyOrder = v.b;
      } else if (key == "trigger") {
        if (!wantKind(Value::String))
          return false;
        int t = IndexOf(kTriggerNames, v.str);
        if (t < 0)
          return fail(lineNo, "unknown trigger " + v.str);
        b.trigger = Trigger(t);
      } else if (key == "tap_ms") {
        if (!intField(b.tapMs, 0, 65535))
          return false;
      } else if (key == "hold_ms") {
        if (!intField(b.holdMs, 0, 65535))
          return false;
      } else if (key == "double_ms") {
        if (!intField(b.doubleMs, 0, 65535))
          return false;
      } else if (key == "turbo_hz") {
        if (!intField(b.turboHz, 0, 1000))
          return false;
      } else if (key == "scale") {
        if (!wantKind(Value::Float))
          return false;
        if (!(std::fabs(v.f) <= 100.0))
          return fail(lineNo, "scale must be within -100..100");
        b.scale = float(v.f);
      } else if (key == "invert") {
        if (!wantKind(Value::Bool))
          return false;
        b.invert = v.b;
      } else if (key == "contexts") {
        if (!wantKind(Value::Array))
          return false;
        uint16_t mask = 0;
        for (const std::string& item : v.array) {
          uint16_t bit;
          if (!ContextFromText(item, bit))
            return fail(lineNo, "unknown context " + item);
          mask |= bit;
        }
        if (mask == 0)
          return fail(lineNo, "contexts is empty");
        b.contexts = mask;
      } else {
        return fail(lineNo, "unknown bind field " + key);
      }
    }
    return closeBind() && closeCustom();
  };

  if (!run()) {
    if (error)
      *error = err;
    return false;
  }
  out = std::move(result);
  if (dropped)
    *dropped = legacyDropped;
  return true;
}

std::string SerializeUserBindings(const UserBindings& bindings) {
  std::string s = "# Metroid Prime 2 port controls (F1 > Controls > Remap, or `bind`/`binds` on the console).\n";
  const Binding def;
  for (const CustomActionDef& c : bindings.customActions) {
    s += "\n[[custom_action]]\n";
    s += "id = " + std::to_string(c.id) + "\n";
    s += "name = " + Quote(c.name) + "\n";
    if (c.renamed)
      s += "renamed = true\n";
    if (!c.preset.empty())
      s += "preset = " + Quote(c.preset) + "\n";
    if (!c.arg.empty())
      s += "arg = " + Quote(c.arg) + "\n";
    if (!c.mode.empty())
      s += "mode = " + Quote(c.mode) + "\n";
    if (!c.command.empty())
      s += "command = " + Quote(c.command) + "\n";
  }
  for (const UserProfile& p : bindings.profiles) {
    s += "\n[[profile]]\n";
    s += "name = " + Quote(p.name) + "\n";
    s += "match = " + Quote(p.match) + "\n";
    if (p.replace)
      s += "replace = true\n";
    if (p.chordWindowMs != 0)
      s += "chord_window_ms = " + std::to_string(p.chordWindowMs) + "\n";
    if (!p.unbind.empty()) {
      s += "unbind = [";
      for (size_t i = 0; i < p.unbind.size(); ++i)
        s += (i ? ", " : "") + Quote(UnbindText(p.unbind[i]));
      s += "]\n";
    }
    for (const Binding& b : p.bindings) {
      s += "\n[[profile.bind]]\n";
      s += "action = " + Quote(ActionKey(b.action)) + "\n";
      s += "inputs = [";
      for (int i = 0; i < b.count; ++i)
        s += (i ? ", " : "") + Quote(InputToText(b.inputs[i]));
      s += "]\n";
      if (b.anyOrder != def.anyOrder)
        s += "any_order = true\n";
      if (b.trigger != def.trigger)
        s += "trigger = " + Quote(kTriggerNames[int(b.trigger)]) + "\n";
      if (b.tapMs != def.tapMs)
        s += "tap_ms = " + std::to_string(b.tapMs) + "\n";
      if (b.holdMs != def.holdMs)
        s += "hold_ms = " + std::to_string(b.holdMs) + "\n";
      if (b.doubleMs != def.doubleMs)
        s += "double_ms = " + std::to_string(b.doubleMs) + "\n";
      if (b.turboHz != def.turboHz)
        s += "turbo_hz = " + std::to_string(b.turboHz) + "\n";
      if (b.scale != def.scale)
        s += "scale = " + FloatText(b.scale) + "\n";
      if (b.invert != def.invert)
        s += "invert = true\n";
      if (b.contexts != def.contexts) {
        s += "contexts = [";
        bool first = true;
        for (int bit = 0; bit < 16; ++bit) {
          if (!(b.contexts & (1u << bit)))
            continue;
          s += (first ? "" : ", ") + Quote(ContextsToText(uint16_t(1u << bit)));
          first = false;
        }
        s += "]\n";
      }
    }
  }
  return s;
}

uint16_t NextCustomId(const UserBindings& bindings) {
  uint16_t max = 0;
  for (const CustomActionDef& c : bindings.customActions) {
    max = std::max(max, c.id);
  }
  // At least 1; never past the last id a custom action can have.
  return max < kMaxCustomId ? uint16_t(max + 1) : uint16_t(kMaxCustomId);
}

void RemoveCustomAction(UserBindings& bindings, uint16_t id) {
  const Action action = CustomAction(id);
  std::erase_if(bindings.customActions, [id](const CustomActionDef& def) { return def.id == id; });
  for (UserProfile& profile : bindings.profiles) {
    std::erase_if(profile.bindings, [action](const Binding& b) { return b.action == action; });
    std::erase_if(profile.unbind, [action](const Unbind& u) { return u.action == action; });
  }
}

Selection Select(const UserBindings& bindings, std::string_view guid, std::string_view type) {
  Selection sel;
  const UserProfile* byType = nullptr;
  for (const UserProfile& p : bindings.profiles) {
    std::string_view m = p.match;
    if (m.empty()) {
      if (!sel.base)
        sel.base = &p;
    } else if (StartsWith(m, "guid:")) {
      if (!sel.pad && !guid.empty() && EqualsNoCase(m.substr(5), guid))
        sel.pad = &p;
    } else if (StartsWith(m, "type:")) {
      if (!byType && !type.empty() && EqualsNoCase(m.substr(5), type))
        byType = &p;
    }
  }
  if (!sel.pad)
    sel.pad = byType;
  return sel;
}

void Overlay(Profile& profile, const UserProfile& user) {
  if (user.replace)
    profile.bindings.clear();
  if (user.chordWindowMs != 0)
    profile.chordWindowMs = user.chordWindowMs;
  // (action, family) pairs: a custom action's value doesn't index a kActionCount row.
  std::unordered_set<uint32_t> drop;
  auto slot = [](Action a, Family f) { return (uint32_t(uint16_t(a)) << 8) | uint32_t(f); };
  for (const Binding& b : user.bindings)
    if (b.count > 0)
      drop.insert(slot(b.action, FamilyOf(b.inputs[b.count - 1].device)));
  for (const Unbind& u : user.unbind)
    drop.insert(slot(u.action, u.family));
  std::erase_if(profile.bindings, [&](const Binding& b) {
    return b.count > 0 && drop.contains(slot(b.action, FamilyOf(b.inputs[b.count - 1].device)));
  });
  profile.bindings.insert(profile.bindings.end(), user.bindings.begin(), user.bindings.end());
}

Family BindingFamily(const Binding& b) {
  return b.count > 0 ? FamilyOf(b.inputs[b.count - 1].device) : Family::Keyboard;
}

bool Overridden(const UserProfile& user, Action action, Family family) {
  for (const Unbind& u : user.unbind)
    if (u.action == action && u.family == family)
      return true;
  for (const Binding& b : user.bindings)
    if (b.count > 0 && b.action == action && BindingFamily(b) == family)
      return true;
  return false;
}

std::vector<Binding> InFamily(const std::vector<Binding>& bindings, Action action, Family family) {
  std::vector<Binding> out;
  for (const Binding& b : bindings)
    if (b.count > 0 && b.action == action && BindingFamily(b) == family)
      out.push_back(b);
  return out;
}

void Materialize(UserProfile& user, Action action, Family family, const Profile& inherited) {
  if (Overridden(user, action, family))
    return;
  for (const Binding& b : InFamily(inherited.bindings, action, family))
    user.bindings.push_back(b);
}

void AddUserBinding(UserProfile& user, const Binding& b) {
  if (b.count > 0) {
    const Family family = BindingFamily(b);
    std::erase_if(user.unbind, [&](const Unbind& u) { return u.action == b.action && u.family == family; });
  }
  if (std::find(user.bindings.begin(), user.bindings.end(), b) == user.bindings.end())
    user.bindings.push_back(b);
}

void RemoveUserBinding(UserProfile& user, size_t index) {
  if (index >= user.bindings.size())
    return;
  const Binding removed = user.bindings[index];
  user.bindings.erase(user.bindings.begin() + std::ptrdiff_t(index));
  if (removed.count == 0)
    return;
  const Family family = BindingFamily(removed);
  if (InFamily(user.bindings, removed.action, family).empty()) {
    const Unbind u{removed.action, family};
    if (std::find(user.unbind.begin(), user.unbind.end(), u) == user.unbind.end())
      user.unbind.push_back(u);
  }
}

void Revert(UserProfile& user, Action action, Family family) {
  std::erase_if(user.unbind, [&](const Unbind& u) { return u.action == action && u.family == family; });
  std::erase_if(user.bindings,
                [&](const Binding& b) { return b.count > 0 && b.action == action && BindingFamily(b) == family; });
}

bool IsFamilyRow(const Binding& b, Family family) { return b.count > 0 && BindingFamily(b) == family; }

void ClearFamily(UserProfile& user, Family family) {
  std::erase_if(user.unbind, [&](const Unbind& u) { return u.family == family; });
  std::erase_if(user.bindings, [&](const Binding& b) { return IsFamilyRow(b, family); });
}

bool SamePhysicalInput(const Input& a, const Input& b) {
  if (a.device != b.device || a.dir != b.dir)
    return false;
  // Each run of mouse codes holds the five buttons (port_input_devices.h).
  if (a.device == Device::MouseButton)
    return a.code % 8 == b.code % 8;
  return a.code == b.code;
}

void BlockRowInputs(Profile& profile, bool (*isRow)(const Binding&), uint16_t ctx, bool everywhereOnly) {
  std::vector<const Binding*> rows;
  for (const Binding& b : profile.bindings)
    if (b.count > 0 && isRow(b))
      rows.push_back(&b);
  const auto covered = [&](const Binding& b) {
    for (const Binding* r : rows) {
      if (r->count != b.count)
        continue;
      bool all = true;
      for (int k = 0; k < b.count && all; ++k)
        all = std::any_of(r->inputs.begin(), r->inputs.begin() + r->count,
                          [&](const Input& in) { return SamePhysicalInput(in, b.inputs[k]); });
      if (all)
        return true;
    }
    return false;
  };
  std::vector<size_t> hit;
  for (size_t i = 0; i < profile.bindings.size(); ++i) {
    const Binding& b = profile.bindings[i];
    if (b.count == 0 || isRow(b) || (everywhereOnly && b.contexts != kCtxAll))
      continue;
    if (covered(b))
      hit.push_back(i);
  }
  for (size_t i : hit)
    profile.bindings[i].blocked |= ctx;
}

const BallRow* FindBallRow(Action a) {
  for (const BallRow& r : kBallRows)
    if (r.row == a)
      return &r;
  return nullptr;
}

const BallRow* BallRowForGame(Action game) {
  for (const BallRow& r : kBallRows)
    if (r.game == game)
      return &r;
  return nullptr;
}

void ApplyBallButtons(Profile& profile) {
  for (Binding& b : profile.bindings) {
    if (FindBallRow(b.action) == nullptr)
      continue;
    // Contexts match any bit, so a row with layers keeps them and blocks every
    // context but the ball: Morph Ball while a layer is held, ranked as a layer's.
    const uint16_t layers = b.contexts == kCtxAll ? 0 : uint16_t(b.contexts & kCtxLayers);
    b.contexts = layers != 0 ? layers : uint16_t(kCtxMorphBall);
    b.blocked = layers != 0 ? uint16_t(kCtxAll & ~(kCtxMorphBall | kCtxLayers)) : 0;
  }
  // A row with layers outranks the input's other bindings while one is held, and
  // blocking them would leave the input dead in the ball without the layer.
  BlockRowInputs(
      profile,
      [](const Binding& b) {
        return FindBallRow(b.action) != nullptr && b.contexts == kCtxMorphBall && BindingFamily(b) != Family::Touch;
      },
      kCtxMorphBall, false);
  // A row bound in a family replaces its game button there in the ball: that
  // family's inputs for the button stop pressing it. A row with layers adds to it,
  // and touch only replaces the overlay's own controls (TouchControlBound), so the
  // turbo button and the layout's added buttons keep pressing theirs.
  bool replaced[std::size(kBallRows)][size_t(Family::Touch)] = {};
  for (const Binding& b : profile.bindings) {
    const BallRow* row = FindBallRow(b.action);
    const Family family = BindingFamily(b);
    if (row != nullptr && b.count > 0 && b.contexts == kCtxMorphBall && family != Family::Touch)
      replaced[size_t(row - kBallRows)][size_t(family)] = true;
  }
  for (Binding& b : profile.bindings) {
    const BallRow* row = BallRowForGame(b.action);
    const Family family = BindingFamily(b);
    if (row != nullptr && b.count > 0 && family != Family::Touch && replaced[size_t(row - kBallRows)][size_t(family)])
      b.blocked |= kCtxMorphBall;
  }
}

bool TouchButtonAction(Action a) {
  // A custom action is Digital, so a button can take it like any other.
  if (a == Action::None || Info(a).kind != ActionKind::Digital)
    return false;
  if (a >= Action::Layer1 && a <= Action::Layer4)
    return false;
  return !(a >= Action::MenuA && a <= Action::MenuRight);
}

std::vector<TouchButton> ParseTouchButtons(std::string_view layout) {
  Action slots[kTouchButtonSlots] = {};
  while (!layout.empty()) {
    const size_t semi = layout.find(';');
    const std::string_view entry = Trim(layout.substr(0, semi));
    layout = semi == std::string_view::npos ? std::string_view{} : layout.substr(semi + 1);
    const size_t colon = entry.find(':');
    if (colon == std::string_view::npos)
      continue;
    // The stock controls' ids are words; a button's is k and its slot, 0..63.
    const std::string_view id = Trim(entry.substr(0, colon));
    if (id.size() < 2 || id.size() > 3 || id[0] != 'k')
      continue;
    int slot = 0;
    bool digits = true;
    for (char c : id.substr(1)) {
      digits = digits && c >= '0' && c <= '9';
      slot = slot * 10 + (c - '0');
    }
    if (!digits || slot >= kTouchButtonSlots || (id.size() == 3 && id[1] == '0'))
      continue;
    std::string_view fields = entry.substr(colon + 1);
    std::string_view key;
    int count = 0;
    while (true) {
      const size_t comma = fields.find(',');
      if (++count == 6)
        key = fields.substr(0, comma);
      if (comma == std::string_view::npos)
        break;
      fields = fields.substr(comma + 1);
    }
    // The 7th, the page the button shows on, is the overlay's alone.
    if (count != 6 && count != 7)
      continue;
    const Action a = ActionFromKey(Trim(key));
    if (TouchButtonAction(a))
      slots[slot] = a;
  }
  std::vector<TouchButton> out;
  for (int slot = 0; slot < kTouchButtonSlots; ++slot) {
    if (slots[slot] != Action::None)
      out.push_back({slot, slots[slot]});
  }
  return out;
}

void BindTouchButtons(Profile& profile, std::string_view layout) {
  for (const TouchButton& button : ParseTouchButtons(layout)) {
    Binding b;
    b.action = button.action;
    b.inputs[0] = Input{Device::Touch, 0, 50, uint16_t(kTouchButtonBase + button.slot)};
    b.count = 1;
    profile.bindings.push_back(b);
  }
}

} // namespace PortInput
