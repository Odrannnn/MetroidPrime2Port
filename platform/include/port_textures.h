#pragma once

#include <cstddef>

// Device-aware HD texture replacements.
//
// Replacements live in the folder given by MP2_TEXTURES, else in the set built into
// the executable (MP_EMBED_RESOURCES), else in `textures` next to it, in Aurora's
// naming convention. Inside it an optional
// per-device subfolder is selected from the input the player last used
// (PortPrompts::ActiveDevice): xbox, playstation, switch, gamecube or standard
// for a pad, keyboard for a keyboard and mouse or when no pad is connected. The
// subfolder is used when it exists, otherwise the root is used, so a single
// device-agnostic pack keeps working. MP2_TEXTURE_DEVICE overrides the name. The
// set is reloaded when the active device changes.
//
// A user pack (MP2_USER_TEXTURES, else `user_textures` in the user folder) is
// layered over that built-in set, with the same device-folder rules. Nothing
// the app does on update touches it. A folder copied to `<user root>.new` is
// swapped into place on the main thread (or at the next start); in the Prime 1
// port the Android overlay's folder picker fills it, which Echoes lacks so far.
//
// Texture dumping (for authoring replacements) is enabled with
// MP2_DUMP_TEXTURES; dumps land in <cachePath>/texture_dumps.
namespace PortTextures {
// Resolves the device folder, loads both sets and remembers their folders.
// Either root may be null or empty to leave that set disabled.
void Initialize(const char* root, const char* userRoot);

// Reloads the sets when the active device changed, and installs a user pack
// that finished copying. Call once per frame, on the main thread.
void Poll();

// Thread-safe: ask the next Poll to install `<user root>.new` if present and
// reload the user pack.
void RequestUserPackReload();

// Thread-safe: ask the next Poll to unload and delete the user pack.
void RequestUserPackRemoval();

// Thread-safe: the user pack on or off (the texture_pack setting); the next
// Poll unloads or reloads it. Off keeps the folder, unlike RequestUserPackRemoval.
void SetUserPackEnabled(bool enabled);
bool UserPackEnabled();

// The user pack folder (never null; empty when there is none).
const char* UserRoot();

// Replacements the user pack registered for the current device.
size_t UserPackCount();

// Name of the current device folder (never null).
const char* DeviceName();

// The folder name for the pad on channel 0, or "keyboard" when there is none.
const char* PadDeviceName();
} // namespace PortTextures
