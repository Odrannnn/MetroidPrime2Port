#include "port_input_user_presets.h"

#include "port_mouse.h"
#include "../port_overlay_internal.h"
#include "port_input_devices.h"
#include "port_input_map.h"

#include <dolphin/pad.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

#include <SDL3/SDL_filesystem.h>

// The store half of port_input_user_presets.h: the files, and the live Aurora
// mappings and mouse settings a preset snapshots and restores. The built-in
// presets write the same layers (port_controls.cpp).

namespace PortInputUserPresets {
namespace {

using PortInput::Family;

constexpr u32 kPort = PAD_CHAN0;
// A preset is a few KiB; a huge file is a mistake.
constexpr uintmax_t kMaxFileBytes = 256 * 1024;

// The folder port_settings.ini lives in (MP_USER_PATH, else the SDL pref path).
std::string Dir() {
  std::string dir;
  if (const char* env = std::getenv("MP_USER_PATH"); env != nullptr && env[0] != '\0') {
    dir = env;
  } else if (char* pref = SDL_GetPrefPath(nullptr, "Metroid Prime")) {
    dir = pref;
    SDL_free(pref);
  } else {
    dir = ".";
  }
  if (dir.back() != '/' && dir.back() != '\\') dir += '/';
  return dir + "control_presets/";
}

const char* Prefix(Family f) { return f == Family::Pad ? "pad-" : f == Family::Touch ? "touch-" : "keyboard-"; }

constexpr Family kFamilies[] = {Family::Keyboard, Family::Pad, Family::Touch};

bool Supported(Family f) { return f == Family::Keyboard || f == Family::Pad || f == Family::Touch; }

bool SameName(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

struct SFile {
  std::string path;
  Preset preset;
};

// Every readable preset of a family. A file that doesn't parse is logged once.
std::vector<SFile> Scan(Family family) {
  static std::set<std::string> sLogged;
  std::vector<SFile> files;
  // Key names must be installed before parsing "key:..." inputs.
  (void)PortInputDevices::UserBindings();
  std::error_code ec;
  for (std::filesystem::directory_iterator it(Dir(), ec), end; !ec && it != end; it.increment(ec)) {
    const std::filesystem::path& path = it->path();
    const std::string file = path.filename().string();
    if (file.rfind(Prefix(family), 0) != 0 || path.extension() != ".toml") continue;
    const auto skip = [&](const std::string& why) {
      if (sLogged.insert(path.string()).second)
        std::fprintf(stderr, "port: control preset %s skipped: %s\n", file.c_str(), why.c_str());
    };
    std::error_code sizeEc;
    const uintmax_t size = std::filesystem::file_size(path, sizeEc);
    if (sizeEc || size > kMaxFileBytes) {
      skip("unreadable or larger than 256 KiB");
      continue;
    }
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    SFile f;
    std::string error;
    if (!Parse(text, f.preset, &error)) {
      skip(error);
      continue;
    }
    if (f.preset.layout.family != family) {
      skip("wrong device for its file name");
      continue;
    }
    f.path = path.string();
    files.push_back(std::move(f));
  }
  return files;
}

const SFile* FindByName(const std::vector<SFile>& files, std::string_view name) {
  // By slug, as ListAll groups the parts: a preset's files can spell its name differently.
  const std::string clean = CleanName(name);
  for (const SFile& f : files)
    if (SameName(f.preset.layout.name, clean)) return &f;
  const std::string slug = Slug(clean);
  for (const SFile& f : files)
    if (Slug(f.preset.layout.name) == slug) return &f;
  return nullptr;
}

Layout Capture(Family family, const std::string& name) {
  Layout l;
  l.family = family;
  l.name = name;
  if (family == Family::Touch) return l; // bindings only
  l.twinStick = PortMouse::StickEnabled();
  if (family == Family::Pad) {
    u32 n = 0;
    if (const PADButtonMapping* b = PADGetButtonMappings(kPort, &n))
      for (u32 i = 0; i < n; ++i) l.padButtons.push_back({int(b[i].nativeButton), int(b[i].padButton)});
    n = 0;
    if (const PADAxisMapping* a = PADGetAxisMappings(kPort, &n))
      for (u32 i = 0; i < n; ++i)
        l.padAxes.push_back({a[i].nativeAxis.nativeAxis, int(a[i].nativeAxis.sign), a[i].nativeButton, int(a[i].padAxis)});
  } else {
    l.mouseAim = PortMouse::Enabled();
    for (u32 slot = 0; slot < PAD_KEY_SLOT_COUNT; ++slot) {
      u32 n = 0;
      if (const PADKeyButtonBinding* b = PADGetKeyButtonBindingsSlot(kPort, slot, &n))
        for (u32 i = 0; i < n; ++i) l.keyButtons.push_back({int(slot), b[i].scancode, int(b[i].padButton)});
      n = 0;
      if (const PADKeyAxisBinding* a = PADGetKeyAxisBindingsSlot(kPort, slot, &n))
        for (u32 i = 0; i < n; ++i) l.keyAxes.push_back({int(slot), a[i].scancode, int(a[i].padAxis), a[i].influence});
    }
  }
  return l;
}

// The same writes as the built-in presets (port_controls.cpp), from the snapshot.
void Restore(const Layout& l) {
  if (l.family == Family::Touch) return;
  PortMouse::SetStickEnabled(l.twinStick);
  if (l.family == Family::Pad) {
    // An empty snapshot (a hand-edited file) keeps the controller's mappings as they are.
    if (!l.padButtons.empty() || !l.padAxes.empty()) PADRestoreDefaultMapping(kPort);
    for (const auto& b : l.padButtons)
      PADSetButtonMapping(kPort, {u32(b.native), PADButton(b.pad)});
    for (const auto& a : l.padAxes)
      PADSetAxisMapping(kPort, {{a.axis, PADAxisSign(a.sign)}, a.button, PADAxis(a.pad)});
  } else {
    for (const auto& b : l.keyButtons) PADSetKeyButtonBindingSlot(kPort, u32(b.slot), {b.key, PADButton(b.pad)});
    for (const auto& a : l.keyAxes)
      PADSetKeyAxisBindingSlot(kPort, u32(a.slot), {a.key, PADAxis(a.pad), s16(a.influence)});
    PADSetKeyboardActive(kPort, TRUE);
    PortMouse::SetEnabled(l.mouseAim);
  }
  PADSerializeMappings();
  PortOverlaySettings::MarkDirty();
  PortOverlaySettings::SaveSettings();
}

} // namespace

std::vector<std::string> List(Family family) {
  std::vector<std::string> names;
  if (!Supported(family)) return names;
  for (const SFile& f : Scan(family)) names.push_back(f.preset.layout.name);
  std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
      return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
  });
  return names;
}

namespace {
bool SaveClean(Family family, const std::string& clean);
bool Remove(Family family, std::string_view name);
} // namespace

bool Save(Family family, std::string_view name) {
  return SaveClean(family, CleanName(name));
}

namespace {

bool SaveClean(Family family, const std::string& clean) {
  if (!Supported(family) || clean.empty()) return false;
  // No controller in port 1: its mappings would read as empty and apply as the defaults.
  if (family == Family::Pad && PADGetIndexForPort(kPort) < 0) return false;
  const auto all = PortInputDevices::UserBindings();
  Preset preset;
  preset.layout = Capture(family, clean);
  if (all != nullptr) preset.bindings = ExtractFamily(*all, family);
  const std::string dir = Dir();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::string path = dir + Prefix(family) + Slug(clean) + ".toml";
  const std::string tmp = path + ".tmp";
  bool written = false;
  {
    std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
    if (file) {
      file << Serialize(preset);
      written = static_cast<bool>(file.flush());
    }
  }
  if (written) std::filesystem::rename(tmp, path, ec);
  if (!written || ec) {
    std::filesystem::remove(tmp, ec);
    std::fprintf(stderr, "port: couldn't write control preset %s\n", path.c_str());
    return false;
  }
  return true;
}

// Deletes a family's file of a name, if any.
bool Remove(Family family, std::string_view name) {
  const std::vector<SFile> files = Scan(family);
  const SFile* file = FindByName(files, name);
  if (file == nullptr) return false;
  std::error_code ec;
  return std::filesystem::remove(file->path, ec) && !ec;
}

} // namespace

bool Apply(Family family, std::string_view name) {
  if (!Supported(family)) return false;
  const std::vector<SFile> files = Scan(family);
  const SFile* file = FindByName(files, name);
  if (file == nullptr) return false;
  const auto saved = PortInputDevices::UserBindings();
  PortInput::UserBindings all = saved != nullptr ? *saved : PortInput::UserBindings{};
  for (PortInput::UserProfile& p : all.profiles)
    if (p.match.empty()) PortInput::ClearFamily(p, family);
  MergeFamily(all, file->preset.bindings);
  Restore(file->preset.layout);
  return PortInputDevices::SetUserBindings(all);
}

bool Delete(Family family, std::string_view name) { return Supported(family) && Remove(family, name); }

std::vector<Entry> ListAll() {
  struct Item {
    Entry entry;
    std::string slug;
  };
  std::vector<Item> items;
  for (const Family family : kFamilies) {
    for (const SFile& f : Scan(family)) {
      const std::string slug = Slug(f.preset.layout.name);
      auto it = std::find_if(items.begin(), items.end(), [&](const Item& i) { return i.slug == slug; });
      if (it == items.end()) {
        items.push_back({{f.preset.layout.name}, slug});
        it = items.end() - 1;
      }
      (family == Family::Pad ? it->entry.pad : family == Family::Touch ? it->entry.touch : it->entry.keyboard) = true;
    }
  }
  std::vector<Entry> entries;
  for (Item& i : items) entries.push_back(std::move(i.entry));
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(), [](char x, char y) {
      return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
  });
  return entries;
}

bool SaveAll(std::string_view name) {
  const std::string clean = CleanName(name);
  if (clean.empty()) return false;
  const auto all = PortInputDevices::UserBindings();
  const bool pad = PADGetIndexForPort(kPort) >= 0;
  bool touch = false;
  if (all != nullptr) {
    const PortInput::UserBindings t = ExtractFamily(*all, Family::Touch);
    touch = !t.profiles.empty() && (!t.profiles[0].bindings.empty() || !t.profiles[0].unbind.empty());
  }
  // The keyboard part first: when it fails, the preset being replaced is left whole.
  if (!SaveClean(Family::Keyboard, clean)) return false;
  if (pad) SaveClean(Family::Pad, clean);
  if (touch) SaveClean(Family::Touch, clean);
  // A replaced preset must not keep a part this save doesn't write.
  if (!pad) Remove(Family::Pad, clean);
  if (!touch) Remove(Family::Touch, clean);
  return true;
}

bool ApplyAll(std::string_view name) {
  bool any = false;
  for (const Family family : kFamilies) any = Apply(family, name) || any;
  return any;
}

bool DeleteAll(std::string_view name) {
  bool any = false;
  for (const Family family : kFamilies) any = Remove(family, name) || any;
  return any;
}

} // namespace PortInputUserPresets
