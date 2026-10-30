#pragma once

#include <cstddef>
#include <functional>
#include <string>

// A minimal zip reader for the texture-pack import: central directory (ZIP64
// included), stored and deflated entries, streamed in chunks so a multi-GB
// pack never sits in memory, CRC32 and size checked. No encryption.
namespace PortZip {

// Normalises an entry name ('\' to '/') and decides whether to extract it; the
// filter may rewrite `name` (the path below the destination). Return false to skip.
using EntryFilter = std::function<bool(std::string& name)>;

// The texture-pack filter: .png/.dds files only, folders kept, any entry that
// could leave the destination (empty, ".", ".." or "__MACOSX" component,
// absolute path, drive letter) and macOS "._" files skipped.
bool TexturePackFilter(std::string& name);

// Extracts every entry `filter` accepts under `destDir` (UTF-8). `count` gets
// the number of files written, also reported through `progress` after each one.
// Returns false with `error` set on a malformed, encrypted or corrupt archive.
bool Extract(const std::string& zipPath, const std::string& destDir, const EntryFilter& filter,
             const std::function<void(size_t)>& progress, size_t& count, std::string& error);

} // namespace PortZip
