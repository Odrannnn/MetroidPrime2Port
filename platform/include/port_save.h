#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Save tools: the port's memory card as files, and the save file parsed the way the game parses it.
//
// The card is Aurora's (`<user>/USA/Card A`, one .gci per CARD file), so a save *is* a file and the
// tools here are file tools: export a copy, import one back, keep copies of the card before the game
// overwrites it, and read a save's three slots without mounting the card at all. `Inspect` is the
// part worth explaining: a save file is a 64-byte GCI directory entry, a 0x2000 header block and two
// alternating 0x2000 copies of the save data (CMemoryCardSys::CCardFileInfo), and each copy is a
// CRC, a generation and 0x1ff8 user bytes (SSaveHeader, the system options, the options of the three
// slots, then the slots' CGameState buffers). Slot listing then calls the game's own
// `CGameState::LoadGameFileState`, so the numbers are the ones the file select would show.
//
// What is *not* here, and why: MP1's quick resume (a savestate written on quit and loaded at launch)
// and the room-state part of save states need the whole live world serialised - actors, colliders,
// effects - which this tree does not have. Save states themselves are in port_savestate.cpp (phase 1:
// the card stream plus Samus's place, no room state), and this file is the card tools. MP2's own resume is the card's save, which `MP2_SKIP_FRONTEND` boots straight into. See
// docs/STATUS.md.
namespace PortSave {

// The user directory, for the backups folder (`MP2_USER_PATH`, as PortInputDevices gets it). Empty: the
// folder is derived from the card's own path when that is a GCI folder, and backups are off if it
// is not reachable.
void Initialize(const std::string& userDirectory);

// Where the card is: the GCI folder Aurora mounts, or the raw image it mounts instead. Both empty
// until the card system exists (CMemoryCardSys::Initialize runs at boot).
std::string Directory();
std::string ImagePath();
// True when the mounted card is a raw image rather than a folder of .gci files.
bool IsImage();

// One of the three save slots of a file, as the front end's file select shows it.
struct Slot {
  bool present = false;
  uint32_t worldId = 0;
  double playTime = 0.0; // seconds
  float health = 0.f;
  uint32_t energyTanks = 0;
  uint32_t timestamp = 0; // the save time the game stored, in seconds
  uint32_t itemPercent = 0;
  float scanPercent = 0.f;
  bool hardMode = false;
};

// A save file's own header and slots. `copy` names which of the two copies the slots were read
// from: the game alternates them and a write only touches one, so one copy is normally the save
// before the game most recently wrote, and a damaged write leaves the other intact.
struct File {
  std::string path;
  std::string game;  // the GCI directory entry's game code: G2ME (USA) or G2MP (PAL)
  std::string maker; // and its maker code, 01
  int copies = 0;
  int copy = -1;
  int copyGeneration = -1;
  int saveIdx = -1;
  Slot slots[3];
  std::string note; // one line on which copy was used and why, for a log
};

bool ReadFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& error);
// Reads a .gci from disk. Never touches the card, so a backup can be checked while the game runs.
bool Inspect(const std::string& path, File& out, std::string& error);
// The card's own save files: every GCI in the folder whose directory entry is the mounted disc's
// game code (the region's other release sharing the folder is not listed).
std::vector<std::string> Files();

// What `save export` writes: the card's file, or every game file when `destination` is a directory.
// Empty `destination`: `<user>/save-backups/export-<stamp>.gci`.
bool Export(const std::string& destination, std::string& message);
// What `save import` takes: a .gci path, a name inside the backups folder, or "latest" for the
// newest backup. The card's current file is copied into the backups folder before it is replaced,
// never deleted, and Aurora re-reads the card afterwards (`aurora_card_remount`).
bool Import(const std::string& source, std::string& message);
// Import and the Dolphin copies mark the card changed; the front end's save screen re-reads it
// (`aurora_card_remount`) when it is idle, never in game. True when it did.
void MarkCardChanged();
bool RemountIfChanged();
// A copy of the card's file as it is on disk now, pruned to the newest kBackupsKept. `written` is
// the path when one was made.
constexpr int kBackupsKept = 20;
bool Backup(std::string* written, std::string& error);
std::vector<std::string> Backups();
std::string BackupsDirectory();

// The emulator's card, shared with the port (MP1's PortGci emulator folder and "Share memory card"):
// with sharing on, CMemoryCardSys::Initialize mounts <emulator folder>/GC as the card base, so the
// port and Dolphin/PrimeHack read and write the same 01-G2ME-*.gci. Desktop only. Metroid Prime
// Trilogy's Wii save.bin does carry an Echoes slot (port_save_trilogy.h), so a card can also be
// exported into it and mirrored with it (below).
//
// The emulator user folder the player chose (the one with GC/ in it; a GC, GC/<region> or
// GC/<region>/Card A folder resolves to the folder above), or empty for autodetection.
void SetEmulatorFolder(const std::string& path);
std::string EmulatorFolder();
// Autodetected Dolphin/PrimeHack user folders that exist on this machine and contain GC/.
std::vector<std::string> DetectedEmulatorFolders();
// The emulator's GCI-folder card A for the mounted release, or empty.
std::string EmulatorCardFolder();
void SetShareEmulatorCard(bool share);
bool ShareEmulatorCard();
// <emulator folder>/GC when sharing is on and the emulator has a GCI-folder card A for this region;
// otherwise empty, with `why` (if given) saying why for the player.
std::string SharedCardBase(std::string* why = nullptr);

// Metroid Prime Trilogy's Wii save.bin (R3ME/R3MP, in Dolphin's Wii NAND), MP1's "Export into
// save.bin" and mirror. `ExportTrilogy` writes the card's newest slot into the Mii-owned file of a
// save.bin the Trilogy made (the old one is kept as save.bin.bak). The mirror (`MirrorTrilogy`, needs
// sharing on) keeps the card and the bin in step: a save made here goes out once written, and in the
// front end a newer one made in the Trilogy comes in. The slot stream conversion is
// port_save_trilogy_payload.cpp (G2ME only; a G2MP card file is refused). What the Trilogy cannot hold
// is lost on the way out: the "initialise powerups" bit, the save index and, in the Wii's 2000-byte
// part B, the tail of the last world's record; scans, hints, relays, doors, layers and cinematics
// with no counterpart in the other release are dropped.
std::string FindEmulatorTrilogy(); // Dolphin's save.bin for this region, or empty
bool ExportTrilogy(const std::string& saveBin, std::string& message);
// A file to hand to a save dialog (Android: the content:// URI it returns can only be written
// through SDL_IOFromFile). The card's own files, or with `trilogy` the one save.bin: `trilogyBase`
// (the picked save.bin's bytes) with the card's newest slot written into it.
struct ExportFile {
  std::string name;
  std::vector<uint8_t> bytes;
  std::string note;
};
bool ExportBytes(bool trilogy, const std::vector<uint8_t>& trilogyBase, std::vector<ExportFile>& out,
                 std::string& message);
void SetMirrorTrilogy(bool mirror);
bool MirrorTrilogy();
std::string TrilogyShareBin(); // the mirrored save.bin, empty when mirroring is off
// Rate-limited to 500 ms; true when the card changed (it was remounted).
bool SyncTrilogyShare(bool pull);

// Saves the other Echoes release left beside this card: each release keeps its own files
// (`<user>/USA/Card A` for G2ME, `<user>/EUR/Card A` for G2MP), so after switching discs the old
// saves are still there but the game does not see them.
struct OtherRelease {
  std::string folder; // the other release's card folder
  std::string code;   // G2ME or G2MP
  std::string label;  // "Metroid Prime 2: Echoes (PAL GameCube)"
  std::string file;   // its newest save file
  int64_t written = 0; // that file's write time, seconds since the epoch
};
// Those beside the mounted GCI-folder card that have a save, newest first; empty for a raw image.
std::vector<OtherRelease> OtherReleaseSaves();
// Copies the release's newest save onto this card through Import: converted to this disc's release,
// the replaced file kept in save-backups.
bool RestoreFrom(const OtherRelease& source, std::string& message);
// What the startup offer asks: this card has no save of this release, another release's exists,
// and it was not offered on this card before (a marker file in the card folder remembers).
std::vector<OtherRelease> RestoreToOffer();
void MarkRestoreOffered();
// The card-write hook (`MP2_CARD_BACKUP=1`, F1 > Saves): a copy of the file as it is *before* the
// game replaces it, so a bad save can always be recovered. Called by
// CMemoryCardSys::CCardFileInfo::WriteFile under TARGET_PC; `fileName` is the card's own file name.
void BeforeCardWrite(const char* fileName);

} // namespace PortSave
