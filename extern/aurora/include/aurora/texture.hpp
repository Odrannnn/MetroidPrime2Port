#ifndef AURORA_TEXTURE_HPP
#define AURORA_TEXTURE_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace aurora::texture {

/// Wildcard hash values used by the replacement filename convention ("$" fields).
inline constexpr uint64_t kWildcardTextureHash = 0xFFFFFFFFFFFFFFFFull;
inline constexpr uint64_t kWildcardTlutHash = 0xFFFFFFFFFFFFFFFEull;

struct TextureSourceKey {
  uint64_t textureHash = 0;
  uint64_t tlutHash = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;
  bool hasTlut = false;

  bool operator==(const TextureSourceKey&) const = default;
};

struct TexturePointerKey {
  const void* data = nullptr;

  bool operator==(const TexturePointerKey&) const = default;
};

using ReplacementKey = std::variant<TexturePointerKey, TextureSourceKey>;

struct RawTextureReplacement {
  std::span<const uint8_t> bytes;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t mipCount = 1;
  uint32_t gxFormat = 0;
  std::string_view label = {};
};

struct ReplacementOptions {
  int32_t priority = 0;
};

struct ReplacementRegistration {
  uint64_t id = 0;
  ReplacementKey key;
};

struct ReplacementGroup {
  std::vector<ReplacementRegistration> registrations;
};

/// Parses a replacement filename of the form "tex1_{w}x{h}[_m]_{texhash}[_{tluthash}]_{fmt}[_arb].dds|.png"
/// into the source key it addresses. Hash fields may be "$" (see the wildcard constants above).
/// Returns nullopt if the filename is invalid.
std::optional<TextureSourceKey> parse_replacement_filename(std::string_view filename) noexcept;

/// Callback source for replacement files that do not live on the filesystem.
///
/// read() loads the entire encoded file at `path` into outBytes, returning false if the file is
/// unavailable. It must be thread safe: it is invoked lazily from Aurora worker threads without
/// internal registry locks held. `userData` must remain valid until unregistration returns;
/// unregistration may wait for one in-flight read callback to finish.
///
/// The callback must not call back into aurora::texture.
struct VirtualFileSource {
  bool (*read)(void* userData, const char* path, std::vector<uint8_t>& outBytes) = nullptr;
  void* userData = nullptr;
};

/// Registers an encoded (.dds/.png) replacement served through callbacks instead of the filesystem.
/// `path` is a relative '/'-separated virtual path whose final component follows the replacement
/// filename convention and determines the key. Mip sidecars are probed lazily by deriving
/// "{stem}_mipN{ext}" from `path` and calling source.read for each level until it returns false.
///
/// Returns ID 0 (invalid) if the filename does not parse or source.read is null.
ReplacementRegistration register_virtual_replacement(std::string_view path, VirtualFileSource source,
                                                     ReplacementOptions options = {});

/// As above, under a key the caller chooses (`path` still names the file and its mip sidecars).
/// With a TexturePointerKey the file stands in for the texture whose GXInitTexObjUserData value
/// (or data pointer) that is; unregister it before the pointer is freed or reused. Returns ID 0 if
/// source.read is null or a pointer key's data is null.
ReplacementRegistration register_virtual_replacement(ReplacementKey key, std::string_view path,
                                                     VirtualFileSource source,
                                                     ReplacementOptions options = {});

/// Registers an encoded (.dds/.png) file under a key the caller chooses, loaded like a directory
/// replacement (thumbnail first, the rest streamed). With a TexturePointerKey the file stands in for
/// the texture whose data pointer that is, or whose GXInitTexObjUserData value it is: the user
/// data is looked up first, and is the one to use when the texel buffer can move. Register it
/// before the texture is first drawn, and unregister it before the pointer is freed or reused.
/// Main thread only.
ReplacementRegistration register_file_replacement(ReplacementKey key, const std::filesystem::path& path,
                                                  ReplacementOptions options = {});

ReplacementRegistration register_replacement(ReplacementKey key, RawTextureReplacement replacement,
                                             ReplacementOptions options = {});
void unregister_replacement(const ReplacementRegistration& registration);
void unregister_replacements(std::span<const ReplacementRegistration> registrations);
void unregister_replacements(const ReplacementGroup& group);
void unregister_replacements(const ReplacementKey& key);
void clear_replacements();

/// How many of `registrations` the game has looked up since they were registered (a texture it
/// loaded had their key), and how many of those failed to load (unreadable file, or a format the
/// GPU cannot sample, such as BC7 without BC support).
struct ReplacementUsage {
  size_t matched = 0;
  size_t failed = 0;
};
ReplacementUsage replacement_usage(std::span<const ReplacementRegistration> registrations);

ReplacementGroup load_replacement_directory(const std::filesystem::path& root, ReplacementOptions options = {});
void reload_replacement_directory(const std::filesystem::path& root, ReplacementGroup& group,
                                  ReplacementOptions options = {});

bool has_replacement(const GXTexObj* obj, const GXTlutObj* tlut = nullptr);

} // namespace aurora::texture

#endif
