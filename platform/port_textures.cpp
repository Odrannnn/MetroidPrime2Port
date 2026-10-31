// Device-aware HD texture replacement loading; see port_textures.h.

#include "port_utf8_path.h"
#include "port_textures.h"

#include "port_embedded.h"
#include "port_prompts.h"

#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <aurora/texture.hpp>
#include <SDL3/SDL_timer.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <thread>

namespace {
// One folder of replacements: the built-in set, or the user's pack over it.
struct Layer {
  const char* label;
  int32_t priority;
  std::string root{};
  // Served from the executable's built-in set (PortEmbedded) instead of `root`.
  bool embedded = false;
  aurora::texture::ReplacementGroup group{};
  // The folder the registrations came from; empty when nothing is loaded.
  std::filesystem::path dir{};
  bool loaded = false;
};

// The user's pack sits above the built-in set but below the binding icons
// (PortPrompts registers those at priority 2): a remapped action should still
// show the key or button it is bound to, whatever art the pack has for it.
Layer sBuiltIn{"built-in", 0};
Layer sUser{"user", 1};
std::string sDevice = "keyboard";
std::atomic<bool> sUserPending{false};
std::atomic<bool> sUserRemove{false};
// Asked for by SetUserPackEnabled, and what Poll last applied.
std::atomic<bool> sUserEnabled{true};
bool sUserOn = true;

// Every name DeviceDirForType can return; used to tell a device-pack root from
// a single device-agnostic pack.
constexpr const char* kDeviceDirs[] = {"xbox", "playstation", "switch", "gamecube", "standard", "keyboard"};

// SDL's gamepad types back PADControllerType, so this mirrors the pad in use.
const char* DeviceDirForType(PADControllerType type) {
  switch (type) {
  case PAD_TYPE_XBOX360:
  case PAD_TYPE_XBOXONE:
    return "xbox";
  case PAD_TYPE_PS3:
  case PAD_TYPE_PS4:
  case PAD_TYPE_PS5:
    return "playstation";
  case PAD_TYPE_SWITCH_PROCON:
  case PAD_TYPE_JOYCON_LEFT:
  case PAD_TYPE_JOYCON_RIGHT:
  case PAD_TYPE_JOYCON_PAIR:
    return "switch";
  case PAD_TYPE_GAMECUBE:
  case PAD_TYPE_NSO_GAMECUBE:
    return "gamecube";
  case PAD_TYPE_STANDARD:
    return "standard";
  default:
    return "keyboard";
  }
}

// The override, else the input in use. Never allocates.
const char* ResolveDeviceName() { return PortPrompts::ActiveDevice(); }

// Prefer the device folder, falling back to the root when it is absent. When
// the root holds device folders but not the one we need, load nothing: the
// directory scan is recursive, so falling back would merge the other devices'
// packs into this one. A root that does not exist loads nothing either, since
// Aurora would create it.
std::optional<std::filesystem::path> ResolveDirectory(const std::string& root) {
  const std::filesystem::path base = PortUtf8::FromUtf8(root);
  std::error_code ec;
  if (!std::filesystem::is_directory(base, ec)) {
    return std::nullopt;
  }
  const std::filesystem::path device = base / sDevice;
  if (std::filesystem::is_directory(device, ec)) {
    return device;
  }
  for (const char* name : kDeviceDirs) {
    if (std::filesystem::is_directory(base / name, ec)) {
      return std::nullopt;
    }
  }
  return base;
}

void Unload(Layer& layer) {
  if (layer.loaded) {
    aurora::texture::unregister_replacements(layer.group);
    layer.group = {};
    layer.dir.clear();
    layer.loaded = false;
  }
}

// Serves an embedded file to Aurora's worker threads. A mip sidecar Aurora probes
// for is simply absent from the table.
bool ReadEmbedded(void*, const char* path, std::vector<uint8_t>& out) {
  const std::span<const uint8_t> bytes = PortEmbedded::Find(path);
  out.assign(bytes.begin(), bytes.end());
  return !bytes.empty();
}

// The embedded counterpart of ResolveDirectory plus load_replacement_directory:
// the device's folder in the built-in set, registered file by file. A device with
// no folder loads nothing, as with a pack on disk that has device folders.
void LoadEmbedded(Layer& layer, bool force) {
  const std::string dir = "textures/" + sDevice + "/";
  const std::span<const PortEmbedded::Entry> entries = PortEmbedded::Under(dir);
  if (entries.empty()) {
    Unload(layer);
    std::fprintf(stderr, "[textures] no %s texture replacements for device '%s' built into the executable\n",
                 layer.label, sDevice.c_str());
    return;
  }
  if (!force && layer.loaded && layer.dir == dir) {
    return;
  }
  Unload(layer);
  const aurora::texture::ReplacementOptions options{.priority = layer.priority};
  for (const PortEmbedded::Entry& entry : entries) {
    const std::string_view path = entry.path;
    const std::string_view name = path.substr(path.rfind('/') + 1);
    const auto key = aurora::texture::parse_replacement_filename(name);
    if (!key.has_value()) {
      continue;
    }
    const aurora::texture::ReplacementRegistration reg = aurora::texture::register_virtual_replacement(
        path, aurora::texture::VirtualFileSource{&ReadEmbedded, nullptr}, options);
    if (reg.id != 0) {
      layer.group.registrations.push_back(reg);
    }
  }
  layer.dir = dir;
  layer.loaded = true;
  std::fprintf(stderr, "[textures] loaded %zu %s texture replacements built into the executable (device: %s)\n",
               layer.group.registrations.size(), layer.label, sDevice.c_str());
}

// force rescans a folder that is already loaded, for a pack whose files changed.
void Load(Layer& layer, bool force) {
  if (layer.embedded) {
    LoadEmbedded(layer, force);
    return;
  }
  if (layer.root.empty()) {
    return;
  }
  const std::optional<std::filesystem::path> dir = ResolveDirectory(layer.root);
  if (!dir.has_value()) {
    Unload(layer);
    std::error_code ec;
    if (std::filesystem::is_directory(layer.root, ec)) {
      std::fprintf(stderr, "[textures] no %s texture replacements for device '%s' in %s\n",
                     layer.label, sDevice.c_str(), layer.root.c_str());
    }
    return;
  }
  // A pack without device folders serves every device from its root. Reloading
  // it for a device switch re-registers the same files, and every reload drops
  // each texture the game has uploaded, so the next frames stall re-reading the
  // pack: on a phone, switching from the touch controls to a mouse froze the
  // overlay while its input queued up.
  if (!force && layer.loaded && layer.dir == *dir) {
    return;
  }
  const aurora::texture::ReplacementOptions options{.priority = layer.priority};
  layer.dir = *dir;
  if (layer.loaded) {
    aurora::texture::reload_replacement_directory(*dir, layer.group, options);
  } else {
    layer.group = aurora::texture::load_replacement_directory(*dir, options);
    layer.loaded = true;
  }
  std::fprintf(stderr, "[textures] loaded %zu %s texture replacements from %s (device: %s)\n",
                 layer.group.registrations.size(), layer.label, dir->string().c_str(), sDevice.c_str());
}

// A pack of thousands of files takes seconds to delete on a phone's storage,
// so a folder that is done with is renamed out of the way on the main thread
// and deleted on a thread of its own. One the game quit before deleting is
// found by DeleteStaleUserPacks at the next start.
const char* const kAsideTag = ".old-";

// A name beside the pack no earlier run can have used: the clock, and a count
// for two in the same tick.
std::filesystem::path AsideName() {
  static unsigned sCount = 0;
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return PortUtf8::FromUtf8(sUser.root + kAsideTag +
                               std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()) +
                               std::to_string(sCount++));
}

void DeleteInBackground(std::filesystem::path dir) {
  try {
    std::thread([dir = std::move(dir)] {
      try {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
      } catch (...) {
      }
    }).detach();
  } catch (...) {
    // No thread: the folder stays until the next start clears it.
  }
}

// Moves `dir` aside and deletes it in the background. False when it could not
// be moved, with `ec` saying why.
bool DiscardFolder(const std::filesystem::path& dir, std::error_code& ec) {
  const std::filesystem::path aside = AsideName();
  std::filesystem::rename(dir, aside, ec);
  if (ec) {
    return false;
  }
  DeleteInBackground(aside);
  return true;
}

// The folders earlier runs moved aside and did not finish deleting: "<pack>.old"
// from before they had names of their own, and "<pack>.old-<digits>".
void DeleteStaleUserPacks() try {
  if (sUser.root.empty()) {
    return;
  }
  const std::filesystem::path root = PortUtf8::FromUtf8(sUser.root);
  const std::string name = PortUtf8::ToUtf8(root.filename());
  if (name.empty()) {
    return;
  }
  const std::string tagged = name + kAsideTag;
  std::error_code ec;
  std::filesystem::directory_iterator it(root.parent_path().empty() ? std::filesystem::path(".") : root.parent_path(),
                                         ec);
  for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
    const std::string entry = PortUtf8::ToUtf8(it->path().filename());
    bool stale = entry == name + ".old";
    if (!stale && entry.size() > tagged.size() && entry.compare(0, tagged.size(), tagged) == 0) {
      stale = entry.find_first_not_of("0123456789", tagged.size()) == std::string::npos;
    }
    std::error_code dirError;
    if (stale && it->is_directory(dirError)) {
      DeleteInBackground(it->path());
    }
  }
} catch (...) {
  // A name the narrow string cannot hold, say: the folders wait for another start.
}

// A new pack is copied beside the live one and swapped in here, on the main
// thread, so the folder never changes under registrations that still point
// into it. A copy finished after the game last ran is picked up at startup.
void ApplyPendingUserPack() {
  if (sUser.root.empty()) {
    return;
  }
  std::error_code ec;
  const std::filesystem::path root = PortUtf8::FromUtf8(sUser.root);
  const std::filesystem::path pending = PortUtf8::FromUtf8(sUser.root + ".new");
  const bool remove = sUserRemove.exchange(false, std::memory_order_acq_rel);
  const bool swap = std::filesystem::is_directory(pending, ec);
  if (!remove && !swap) {
    return;
  }
  Unload(sUser);
  // The old pack is moved aside rather than deleted first, so a failed install
  // leaves the user with the pack they had instead of none.
  const std::filesystem::path old = AsideName();
  const bool hadPack = std::filesystem::exists(root, ec);
  if (hadPack) {
    std::filesystem::rename(root, old, ec);
    if (ec) {
      std::fprintf(stderr, "[textures] could not replace %s: %s\n", PortUtf8::ToUtf8(root).c_str(),
                     ec.message().c_str());
      return;
    }
  }
  if (swap && !remove) {
    std::filesystem::rename(pending, root, ec);
    if (ec) {
      std::fprintf(stderr, "[textures] could not install the texture pack from %s: %s\n",
                     PortUtf8::ToUtf8(pending).c_str(), ec.message().c_str());
      if (hadPack) {
        std::filesystem::rename(old, root, ec);
      }
      return;
    }
  } else if (swap && !DiscardFolder(pending, ec)) {
    std::filesystem::remove_all(pending, ec);
  }
  if (hadPack) {
    DeleteInBackground(old);
  }
}

void LoadUser(bool force) {
  if (sUserOn) {
    Load(sUser, force);
  }
}
} // namespace

namespace PortTextures {
void Initialize(const char* root, const char* userRoot) {
  sBuiltIn.root = root != nullptr ? root : "";
  // No folder given and the executable carries its own set: use that.
  sBuiltIn.embedded = sBuiltIn.root.empty() && !PortEmbedded::Under("textures/").empty();
  sUser.root = userRoot != nullptr ? userRoot : "";
  sDevice = ResolveDeviceName();
  sUserOn = sUserEnabled.load(std::memory_order_acquire);
  DeleteStaleUserPacks();
  ApplyPendingUserPack();
  Load(sBuiltIn, true);
  LoadUser(true);
  std::fprintf(stderr, "[textures] device '%s'; built-in set: %s; user pack: %s\n", sDevice.c_str(),
               sBuiltIn.embedded ? "(built into the executable)"
                                  : sBuiltIn.root.empty() ? "(none)" : sBuiltIn.root.c_str(),
               sUser.root.empty() ? "(none)" : sUser.root.c_str());
}

void Poll() {
  if (sUserPending.exchange(false, std::memory_order_acq_rel)) {
    ApplyPendingUserPack();
    LoadUser(true);
  }
  if (const bool on = sUserEnabled.load(std::memory_order_acquire); on != sUserOn) {
    sUserOn = on;
    if (on) {
      Load(sUser, true);
    } else {
      Unload(sUser);
    }
    std::fprintf(stderr, "[textures] user texture pack %s\n", on ? "on" : "off");
  }
  const char* device = ResolveDeviceName();
  if (sDevice == device) {
    return;
  }
  sDevice = device;
  const uint64_t startNs = SDL_GetTicksNS();
  Load(sBuiltIn, false);
  LoadUser(false);
  std::fprintf(stderr, "[textures] switched the texture set to '%s' in %llu ms\n", device,
                 static_cast<unsigned long long>((SDL_GetTicksNS() - startNs) / 1000000ull));
}

void RequestUserPackReload() { sUserPending.store(true, std::memory_order_release); }

void RequestUserPackRemoval() {
  sUserRemove.store(true, std::memory_order_release);
  sUserPending.store(true, std::memory_order_release);
}

void SetUserPackEnabled(bool enabled) { sUserEnabled.store(enabled, std::memory_order_release); }

bool UserPackEnabled() { return sUserEnabled.load(std::memory_order_acquire); }

const char* UserRoot() { return sUser.root.c_str(); }

size_t UserPackCount() { return sUser.loaded ? sUser.group.registrations.size() : 0; }

UserPackUsage UserPackMatches() {
  if (!sUser.loaded) {
    return {};
  }
  const aurora::texture::ReplacementUsage usage = aurora::texture::replacement_usage(sUser.group.registrations);
  return {usage.matched, usage.failed};
}

const char* DeviceName() { return sDevice.c_str(); }

const char* PadDeviceName() { return DeviceDirForType(PADGetControllerType(PAD_CHAN0)); }
} // namespace PortTextures
