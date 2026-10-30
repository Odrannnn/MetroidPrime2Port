#include "port_input_user_presets.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

// The pure half: layout text, slugs, and moving a family's rows between binding
// sets. The store (files, live settings) is port_input_user_presets_store.cpp, so
// tests link this without Aurora.

namespace PortInputUserPresets {
namespace {

using PortInput::Family;

constexpr std::string_view kMagic = "#! controls-preset = 1";

const char* FamilyName(Family f) {
  return f == Family::Pad ? "pad" : f == Family::Touch ? "touch" : "keyboard";
}

void Put(std::string& s, const char* key, long value) {
  s += "#! ";
  s += key;
  s += " = ";
  s += std::to_string(value);
  s += '\n';
}

template <typename... Ints>
void PutInts(std::string& s, const char* key, Ints... values) {
  s += "#! ";
  s += key;
  s += " =";
  ((s += ' ', s += std::to_string(values)), ...);
  s += '\n';
}

// Up to `max` integers out of a value; false when fewer than `min` or junk.
bool Ints(std::string_view text, int* out, int min, int max) {
  int n = 0;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && text[i] == ' ') ++i;
    if (i >= text.size()) break;
    const std::string token(text.substr(i, text.find(' ', i) - i));
    i += token.size();
    char* end = nullptr;
    const long v = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0' || n >= max) return false;
    out[n++] = int(v);
  }
  return n >= min && n <= max;
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

const PortInput::CustomActionDef* FindDef(const PortInput::UserBindings& b, uint16_t id) {
  for (const auto& def : b.customActions)
    if (def.id == id) return &def;
  return nullptr;
}

} // namespace

std::string CleanName(std::string_view name) {
  std::string clean;
  for (char c : name)
    if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f) clean += c;
  const std::string_view t = Trim(clean);
  size_t n = std::min(t.size(), kMaxName);
  // Don't cut through a UTF-8 sequence.
  while (n > 0 && n < t.size() && (static_cast<unsigned char>(t[n]) & 0xc0) == 0x80) --n;
  return std::string(t.substr(0, n));
}

std::string Slug(std::string_view name) {
  std::string slug;
  for (char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u) && u < 0x80) {
      slug += char(std::tolower(u));
    } else if (!slug.empty() && slug.back() != '-') {
      slug += '-';
    }
  }
  while (!slug.empty() && slug.back() == '-') slug.pop_back();
  return slug.empty() ? "preset" : slug;
}

std::string Serialize(const Preset& preset) {
  const Layout& l = preset.layout;
  std::string s(kMagic);
  s += '\n';
  s += "#! family = ";
  s += FamilyName(l.family);
  s += "\n#! name = ";
  s += l.name;
  s += '\n';
  // Touch is bindings only: no Aurora layer, no settings.
  if (l.family != Family::Touch) Put(s, "twin_stick", l.twinStick);
  if (l.family == Family::Touch) {
  } else if (l.family == Family::Pad) {
    Put(s, "swap_scan_xray", l.swapScanXray);
    Put(s, "shift_pad", l.shift[2]);
    for (const auto& b : l.padButtons) PutInts(s, "pad_button", b.native, b.pad);
    for (const auto& a : l.padAxes) PutInts(s, "pad_axis", a.axis, a.sign, a.button, a.pad);
    for (size_t bit = 0; bit < l.padAlt.size(); ++bit)
      if (l.padAlt[bit] != -1) PutInts(s, "pad_alt", int(bit), l.padAlt[bit]);
  } else {
    Put(s, "mouse_aim", l.mouseAim);
    Put(s, "shift_key0", l.shift[0]);
    Put(s, "shift_key1", l.shift[1]);
    for (const auto& b : l.keyButtons) PutInts(s, "key_button", b.slot, b.key, b.pad);
    for (const auto& a : l.keyAxes) PutInts(s, "key_axis", a.slot, a.key, a.pad, a.influence);
    for (size_t i = 0; i < l.mouseActions.size(); ++i) PutInts(s, "mouse_action", int(i), l.mouseActions[i]);
  }
  s += '\n';
  s += PortInput::SerializeUserBindings(preset.bindings);
  return s;
}

bool Parse(std::string_view text, Preset& out, std::string* error) {
  const auto fail = [&](const std::string& why) {
    if (error != nullptr) *error = why;
    return false;
  };
  Preset p;
  bool magic = false, haveFamily = false;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string_view::npos) eol = text.size();
    const std::string_view line = Trim(text.substr(pos, eol - pos));
    pos = eol + 1;
    if (line.substr(0, 2) != "#!") continue;
    if (line == kMagic) {
      magic = true;
      continue;
    }
    const std::string_view body = Trim(line.substr(2));
    const size_t eq = body.find('=');
    if (eq == std::string_view::npos) continue;
    const std::string_view key = Trim(body.substr(0, eq));
    const std::string_view value = Trim(body.substr(eq + 1));
    int v[4] = {0, 0, 0, 0};
    Layout& l = p.layout;
    if (key == "family") {
      if (value == "keyboard") l.family = Family::Keyboard;
      else if (value == "pad") l.family = Family::Pad;
      else if (value == "touch") l.family = Family::Touch;
      else return fail("unknown family");
      haveFamily = true;
    } else if (key == "name") {
      l.name = CleanName(value);
    } else if (key == "twin_stick" && Ints(value, v, 1, 1)) {
      l.twinStick = v[0] != 0;
    } else if (key == "swap_scan_xray" && Ints(value, v, 1, 1)) {
      l.swapScanXray = v[0] != 0;
    } else if (key == "mouse_aim" && Ints(value, v, 1, 1)) {
      l.mouseAim = v[0] != 0;
    } else if (key == "shift_pad" && Ints(value, v, 1, 1)) {
      l.shift[2] = v[0];
    } else if (key == "shift_key0" && Ints(value, v, 1, 1)) {
      l.shift[0] = v[0];
    } else if (key == "shift_key1" && Ints(value, v, 1, 1)) {
      l.shift[1] = v[0];
    } else if (key == "pad_button" && Ints(value, v, 2, 2)) {
      l.padButtons.push_back({v[0], v[1]});
    } else if (key == "pad_axis" && Ints(value, v, 4, 4)) {
      l.padAxes.push_back({v[0], v[1], v[2], v[3]});
    } else if (key == "key_button" && Ints(value, v, 3, 3)) {
      l.keyButtons.push_back({v[0], v[1], v[2]});
    } else if (key == "key_axis" && Ints(value, v, 4, 4)) {
      l.keyAxes.push_back({v[0], v[1], v[2], v[3]});
    } else if (key == "pad_alt" && Ints(value, v, 2, 2)) {
      if (v[0] < 0 || v[0] >= 64) return fail("bad pad_alt bit");
      if (l.padAlt.size() <= size_t(v[0])) l.padAlt.resize(size_t(v[0]) + 1, -1);
      l.padAlt[size_t(v[0])] = v[1];
    } else if (key == "mouse_action" && Ints(value, v, 2, 2)) {
      if (v[0] < 0 || v[0] >= 64) return fail("bad mouse_action button");
      if (l.mouseActions.size() <= size_t(v[0])) l.mouseActions.resize(size_t(v[0]) + 1, 0);
      l.mouseActions[size_t(v[0])] = v[1];
    }
    // Unknown keys are ignored: a newer build's file still loads.
  }
  if (!magic) return fail("not a controls preset");
  if (!haveFamily) return fail("no family");
  if (p.layout.name.empty()) return fail("no name");
  std::string tomlError;
  if (!PortInput::ParseUserBindings(text, p.bindings, &tomlError)) return fail(tomlError);
  out = std::move(p);
  return true;
}

PortInput::UserBindings ExtractFamily(const PortInput::UserBindings& all, Family family) {
  PortInput::UserBindings out;
  PortInput::UserProfile profile;
  profile.name = "Default";
  for (const PortInput::UserProfile& p : all.profiles) {
    if (!p.match.empty()) continue;
    for (const PortInput::Binding& b : p.bindings)
      if (PortInput::IsFamilyRow(b, family) && (!PortInput::IsCustom(b.action) || FindDef(all, PortInput::CustomId(b.action))))
        profile.bindings.push_back(b);
    for (const PortInput::Unbind& u : p.unbind)
      if (u.family == family && (!PortInput::IsCustom(u.action) || FindDef(all, PortInput::CustomId(u.action))))
        profile.unbind.push_back(u);
    break;
  }
  const auto need = [&](PortInput::Action a) {
    if (!PortInput::IsCustom(a)) return;
    if (const auto* def = FindDef(all, PortInput::CustomId(a)); def != nullptr && FindDef(out, def->id) == nullptr)
      out.customActions.push_back(*def);
  };
  for (const auto& b : profile.bindings) need(b.action);
  for (const auto& u : profile.unbind) need(u.action);
  out.profiles.push_back(std::move(profile));
  return out;
}

void MergeFamily(PortInput::UserBindings& all, const PortInput::UserBindings& preset) {
  // Where each preset custom id lands in `all`.
  std::vector<std::pair<uint16_t, uint16_t>> remap;
  for (const PortInput::CustomActionDef& def : preset.customActions) {
    uint16_t id = def.id;
    const PortInput::CustomActionDef* same = FindDef(all, def.id);
    if (same == nullptr) {
      all.customActions.push_back(def);
    } else if (!(*same == def)) {
      // Taken by something else: an equal definition under another id, else a new id.
      id = 0;
      for (const auto& other : all.customActions) {
        PortInput::CustomActionDef copy = def;
        copy.id = other.id;
        if (copy == other) {
          id = other.id;
          break;
        }
      }
      if (id == 0) {
        id = PortInput::NextCustomId(all);
        PortInput::CustomActionDef added = def;
        added.id = id;
        all.customActions.push_back(added);
      }
    }
    remap.emplace_back(def.id, id);
  }
  const auto map = [&](PortInput::Action a) {
    if (!PortInput::IsCustom(a)) return a;
    for (const auto& [from, to] : remap)
      if (from == PortInput::CustomId(a)) return PortInput::CustomAction(to);
    return a;
  };
  PortInput::UserProfile* base = nullptr;
  for (PortInput::UserProfile& p : all.profiles)
    if (p.match.empty()) {
      base = &p;
      break;
    }
  if (base == nullptr) {
    // The Default profile comes first.
    PortInput::UserProfile p;
    p.name = "Default";
    all.profiles.insert(all.profiles.begin(), p);
    base = &all.profiles.front();
  }
  for (const PortInput::UserProfile& p : preset.profiles) {
    if (!p.match.empty()) continue;
    for (PortInput::Unbind u : p.unbind) {
      u.action = map(u.action);
      if (std::find(base->unbind.begin(), base->unbind.end(), u) == base->unbind.end()) base->unbind.push_back(u);
    }
    // After the unbinds: adding a row drops an unbind it supersedes.
    for (PortInput::Binding b : p.bindings) {
      b.action = map(b.action);
      PortInput::AddUserBinding(*base, b);
    }
  }
}

} // namespace PortInputUserPresets
