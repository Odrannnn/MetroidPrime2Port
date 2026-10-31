#pragma once

// Mods folder: files served over the disc through Aurora's DVD overlays.
//
// Each folder in <user path>/mods (or MP2_MODS) is a mod, applied in name order, so
// a later mod wins over an earlier one. Inside a mod:
//  - a file at a disc path (Metroid5.pak, Audio/SplinterHive-132.dsp,
//    Video/openPartOne.thp; case-insensitive) replaces that disc file, and a
//    `.thp` the disc lacks is added instead (the game opens movies by name);
//  - a file named <8 hex digits>.<4 letters> (1A2B3C4D.TXTR), anywhere in the
//    mod, replaces that resource in every PAK that holds it. The PAK is served
//    as a virtual file: its table patched to point past the original data, where
//    the loose file is appended.
// Mods are read at startup, and again by Reload() (F1 > Mods > Reload mods, which
// reopens every PAK and rebuilds the room): the game caches PAK tables when it boots.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace PortMods {

// --- PAK tables (big-endian, version 3.5: Prime 1 and 2 share it) ---------------------------

struct PakResource {
  uint32_t compressed = 0;
  uint32_t type = 0;
  uint32_t id = 0;
  uint32_t size = 0;
  uint32_t offset = 0;
  // Where this entry's 20 bytes sit in the file.
  size_t entryOffset = 0;
};

struct PakTable {
  // End of the resource table: everything before it is header.
  size_t headerEnd = 0;
  std::vector<PakResource> resources;
};

// False when the data is not a PAK, or is cut short before the table's
// end (read more of the file and try again; `needed` says how much at least).
bool ParsePakTable(const uint8_t* data, size_t size, PakTable& table, size_t& needed);

// "1A2B3C4D.TXTR" (any case) -> id 0x1A2B3C4D, type 'TXTR'.
bool ParseLooseName(const std::string& fileName, uint32_t& type, uint32_t& id);
std::string FourCCString(uint32_t type);

// Every .pak on the base disc, as (entryNum, path), in entry order. The disc must be open.
std::vector<std::pair<int32_t, std::string>> DiscPaks();

// --- Virtual files ------------------------------------------------------------

struct Segment {
  enum Kind { kMemory, kSource, kHost };
  Kind kind = kMemory;
  uint64_t start = 0;
  uint64_t length = 0;
  size_t memory = 0;         // kMemory: index into VirtualFile::memory
  uint64_t sourceOffset = 0; // kSource, kHost: where the segment starts in its file
  std::string hostPath;      // kHost: read from this file, zeros past hostSize
  uint64_t hostSize = 0;
};

struct VirtualFile {
  std::string discPath;
  // The base disc entry kSource segments read.
  int32_t sourceEntry = -1;
  uint64_t size = 0;
  std::vector<std::vector<uint8_t>> memory;
  std::vector<Segment> segments; // contiguous, in order, covering [0, size)
};

struct LooseResource {
  uint32_t type = 0;
  uint32_t id = 0;
  std::string hostPath;
  uint64_t hostSize = 0;
  std::string mod;
};

// A PAK with `loose` swapped in: header is at least table.headerEnd bytes of
// the original, which is originalSize long. Every table entry matching a loose
// resource's type and id points at that resource's appended copy. The original
// data comes from the base disc (kSource), or from sourceHost when that is set
// (a PAK a mod replaced whole).
VirtualFile PatchPak(const std::vector<uint8_t>& header, const PakTable& table, uint64_t originalSize,
                     const std::vector<const LooseResource*>& loose, const std::string& sourceHost = {});

// How a Reader reaches the file being patched. Tests supply their own.
struct SourceIo {
  void* (*open)(const VirtualFile& file) = nullptr;
  int64_t (*readAt)(void* handle, uint64_t offset, uint8_t* buffer, size_t length) = nullptr;
  void (*close)(void* handle) = nullptr;
};

// One open handle on a virtual file. Not thread safe; each open gets its own.
class Reader {
public:
  Reader(std::shared_ptr<const VirtualFile> file, const SourceIo* io);
  ~Reader();
  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;

  int64_t Read(uint8_t* buffer, size_t length);
  int64_t Seek(int64_t offset, int32_t whence);

private:
  int64_t ReadSegment(const Segment& segment, uint64_t at, uint8_t* buffer, size_t length);

  std::shared_ptr<const VirtualFile> mFile;
  const SourceIo* mIo;
  uint64_t mPos = 0;
  void* mSource = nullptr;
  bool mSourceTried = false;
  struct Host;
  std::unique_ptr<Host> mHost;
};

// --- Startup --------------------------------------------------------------------

struct ModInfo {
  std::string name;
  bool enabled = true;
  int files = 0;     // disc files replaced
  int resources = 0; // loose resources used
};

struct Status {
  std::string folder;
  bool active = false; // mods setting on
  std::vector<ModInfo> mods;
  int overlays = 0; // disc files served from mods
  std::vector<std::string> messages;
};

// Scans the mods folder and registers the overlays. Call once, after the disc
// is open and before the game starts.
void Initialize(const std::string& userPath);
// Initialize again with the same folder and the current settings; only with every PAK closed.
void Reload();
const Status& CurrentStatus();
// The mods folder: MP2_MODS, else <user path>/mods. Empty if there is no user path.
std::string Folder(const std::string& userPath);

// The settings (mods, mods_disabled): all mods on or off and the folder names turned off,
// '/'-separated. Both take effect on the next launch.
bool Enabled();
void SetEnabled(bool enabled);
const std::string& Disabled();
void SetDisabled(const std::string& list);

// Folder names the settings disable, '/'-separated (no folder name has one).
std::vector<std::string> SplitDisabled(const std::string& list);
std::string JoinDisabled(const std::vector<std::string>& names);

} // namespace PortMods
