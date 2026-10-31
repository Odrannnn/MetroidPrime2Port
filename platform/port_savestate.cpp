// Save states (room state: port_savestate_room.cpp). See platform/include/port_savestate.h.
//
// The hard part is the load, because nothing in the platform layer can stop the game between two
// frames: Tick runs at the start of a CStateManager update, so the old world is still live for the
// rest of that frame and until CMainFlow restarts it. The load therefore
//  - swaps the game state the way CMain::StreamNewGameState does, but keeps the old CGameState
//    alive (the old CStateManager holds raw CPlayerState pointers into it) until the rebuilt world
//    runs, and only then deletes it;
//  - restarts through CStateManager::PortWarpToWorld, which the debug warp uses;
//  - keeps the outgoing world's pak readable, as PortWarpToWorld does for its own warps, because the
//    new state's constructor and the warp both stash every pak but the destination's.
#include "port_savestate.h"
#include "port_utf8_path.h"

#include <SDL3/SDL_mouse.h>

#include <algorithm>

#include "port_debug.h"
#include "port_mods.h"
#include "port_overlay.h"
#include "port_resume_logic.h"
#include "port_save.h"
#include "port_warp.h"

#include "MetroidPrime/PortSkipFrontEnd.hpp"

#include "Kyoto/Alloc/CMemory.hpp"
#include "Kyoto/Input/IController.hpp"
#include "Kyoto/CPakFile.hpp"
#include "Kyoto/CResFactory.hpp"
#include "Kyoto/CResLoader.hpp"
#include "Kyoto/Math/CTransform4f.hpp"
#include "Kyoto/Math/CVector3f.hpp"
#include "Kyoto/Streams/CBitStreamReader.hpp"
#include "Kyoto/Streams/CBitStreamWriter.hpp"
#include "Kyoto/Streams/CMemoryInStream.hpp"
#include "Kyoto/Streams/CMemoryStreamOut.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CGameGlobalObjects.hpp"
#include "MetroidPrime/CMain.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/TGameTypes.hpp"
#include "MetroidPrime/Player/CGameOptions.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/Player/CPersistentOptions.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/Player/CWorldState.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <vector>

namespace PortSaveState {
namespace {

constexpr uint32_t kMagic = 0x5332504d; // "MP2S" read little-endian
// 2: a room state section follows the blob. Version 1 states still load, without room state.
constexpr uint32_t kVersion = 2;
constexpr uint32_t kVersionNoRoom = 1;
// The card slot holds 0xa38 bytes of CGameState stream (SGameFileSlot::mSaveBuffer); the state
// keeps more room than that so a port-only addition to the stream cannot be cut off silently.
constexpr size_t kBlobSize = 0x2000;
// Frames the rebuilt world runs before Samus is put back, and how long a load may take at most.
constexpr int kSettleFrames = 3;
constexpr int kPlaceTimeoutFrames = 60 * 90;
// How long the room restore waits with nothing moving (no area loading, no layer streaming, no
// saved area newly back) before it gives up on the saved areas still missing, and how long it may
// take in all however busy the world stays.
constexpr int kRoomTimeoutFrames = 60 * 20;
constexpr int kRoomHardCapFrames = 60 * 300;
// Free object ids a saved layer waits for before it streams in (CStateManager's kPortWarpMinFreeIds).
constexpr int kRoomMinFreeIds = 256;
// The quick-resume state's slot number: written on a clean quit, loaded on the next launch.
constexpr int kResumeSlot = -1;
// A state that is not in a slot (DoRoomReload).
constexpr int kNoSlot = -2;

struct State {
  uint32_t worldId = 0;
  int32_t areaIndex = -1;
  uint32_t areaAssetId = 0;
  float pos[3] = {};
  float fwd[3] = {0.f, 1.f, 0.f};
  bool morphed = false;
  double playTime = 0.0;
  int64_t savedAt = 0;
  std::string room;
  std::vector< uint8_t > blob;
  RoomState roomState; // empty for a version 1 state
  PortResume::Header resumeHeader; // a quick-resume file only (ReadSlot)
};

std::atomic< int > sSaveRequest{-1};
std::atomic< int > sLoadRequest{-1};
std::atomic_bool sSaveToast{false}; // the request came from a hotkey: toast its result
std::atomic_bool sLoadToast{false};
// Mods reload: Tick turns the request into a load of the current state; CMainFlow calls
// ReloadModsNow once the old world is gone, before the new one starts loading.
std::atomic< bool > sModReloadRequest{false};
std::atomic< bool > sRoomReloadRequest{false};
std::atomic< bool > sModReloadPending{false};
std::atomic< bool > sModReloadWorldStale{false}; // reloaded while paused: the room still has old textures
std::atomic< int > sSelected{1};
std::mutex sMessageMutex;
std::string sMessage;

// A load that has restarted the world and waits to put Samus back.
struct Placement {
  bool active = false;
  State state;
  const CStateManager* oldMgr = nullptr;
  const CWorld* oldWorld = nullptr;
  int settled = 0;
  int waited = 0;
};
Placement sPlace;

// A load that has put Samus back and applies the room state as the saved areas load.
struct RoomRestore {
  bool active = false;
  RoomState state;
  RoomApplyProgress progress;
  int waited = 0;           // ticks since Samus was placed
  int idle = 0;             // ticks in which nothing the restore waits on changed
  uint64_t signature = 0;   // RoomSignature on the last tick
  // Layers the saved objects live in, activated before the areas are applied. A layer that was
  // switched on in the live room but is not in the reload's layer bits spawns none of its objects,
  // and those saved objects come back as unmatched (162 in area 0 of the Temple Grounds save).
  std::vector< std::pair< int, int > > layers;
  size_t nextLayer = 0;
};
RoomRestore sRoom;
CGameState* sRetired = nullptr; // the game state a load replaced, deleted once the world restarted

// True while a hotkey save or load runs: its messages also show as a toast.
bool sToastMessages = false;

void SetMessage(const std::string& message) {
  std::fprintf(stderr, "[savestate] %s\n", message.c_str());
  if (sToastMessages) {
    PortDebug::ShowNote(message);
  }
  std::lock_guard< std::mutex > lock(sMessageMutex);
  sMessage = message;
}

// The last manager Tick ran on, cleared by its destructor (Forget): the quit path's resume write
// runs between updates, outside any Tick.
CStateManager* sLiveMgr = nullptr;
// A quick resume being loaded (its file stays until the next snapshot replaces it, as in MP1);
// the running marker then says "resumed <savedAt>" so a crash right after is noticed.
bool sResuming = false;
int64_t sResumingSavedAt = 0;

std::string SlotName(int slot) {
  return slot == kResumeSlot ? std::string("the resume state") : "slot " + std::to_string(slot);
}

std::string SlotPath(int slot) {
  const std::string folder = Folder();
  if (folder.empty()) {
    return std::string();
  }
  return slot == kResumeSlot ? folder + "/resume.mp2ss"
                             : folder + "/slot" + std::to_string(slot) + ".mp2ss";
}

// --- file format: little-endian header, then the CGameState stream ---------------------------

void Put32(std::string& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast< char >(v >> (8 * i)));
  }
}
void Put64(std::string& out, uint64_t v) {
  Put32(out, static_cast< uint32_t >(v));
  Put32(out, static_cast< uint32_t >(v >> 32));
}
void PutF(std::string& out, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  Put32(out, v);
}
void PutBytes(std::string& out, const void* data, size_t len) {
  Put32(out, static_cast< uint32_t >(len));
  out.append(static_cast< const char* >(data), len);
}

struct Reader {
  const std::string& in;
  size_t at = 0;
  bool ok = true;
  uint32_t U32() {
    if (at + 4 > in.size()) {
      ok = false;
      return 0;
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      v |= static_cast< uint32_t >(static_cast< uint8_t >(in[at + i])) << (8 * i);
    }
    at += 4;
    return v;
  }
  uint64_t U64() {
    const uint64_t lo = U32();
    return lo | (static_cast< uint64_t >(U32()) << 32);
  }
  float F() {
    const uint32_t v = U32();
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }
  std::string Bytes() {
    const uint32_t len = U32();
    if (!ok || at + len > in.size()) {
      ok = false;
      return std::string();
    }
    std::string s = in.substr(at, len);
    at += len;
    return s;
  }
};

std::string Encode(const State& s) {
  std::string out;
  Put32(out, kMagic);
  Put32(out, kVersion);
  Put32(out, s.worldId);
  Put32(out, static_cast< uint32_t >(s.areaIndex));
  Put32(out, s.areaAssetId);
  for (float f : s.pos) {
    PutF(out, f);
  }
  for (float f : s.fwd) {
    PutF(out, f);
  }
  Put32(out, s.morphed ? 1 : 0);
  uint64_t time;
  std::memcpy(&time, &s.playTime, 8);
  Put64(out, time);
  Put64(out, static_cast< uint64_t >(s.savedAt));
  PutBytes(out, s.room.data(), s.room.size());
  PutBytes(out, s.blob.data(), s.blob.size());
  std::string room;
  EncodeRoomState(s.roomState, room);
  PutBytes(out, room.data(), room.size());
  return out;
}

bool Decode(const std::string& in, State& s, std::string& error) {
  Reader r{in};
  if (r.U32() != kMagic) {
    error = "not a save state";
    return false;
  }
  const uint32_t version = r.U32();
  if (version != kVersion && version != kVersionNoRoom) {
    error = "save state version " + std::to_string(version) + " is not supported";
    return false;
  }
  s.worldId = r.U32();
  s.areaIndex = static_cast< int32_t >(r.U32());
  s.areaAssetId = r.U32();
  for (float& f : s.pos) {
    f = r.F();
  }
  for (float& f : s.fwd) {
    f = r.F();
  }
  s.morphed = r.U32() != 0;
  const uint64_t time = r.U64();
  std::memcpy(&s.playTime, &time, 8);
  s.savedAt = static_cast< int64_t >(r.U64());
  s.room = r.Bytes();
  const std::string blob = r.Bytes();
  if (!r.ok || blob.empty()) {
    error = "save state is truncated";
    return false;
  }
  for (float f : s.pos) {
    // Samus is teleported there unchecked; a damaged file must not hand the area lookups a NaN.
    if (!std::isfinite(f) || std::fabs(f) > 1e7f) {
      error = "save state's position is damaged";
      return false;
    }
  }
  s.blob.assign(blob.begin(), blob.end());
  if (version >= kVersion) {
    const std::string room = r.Bytes();
    if (!r.ok || !DecodeRoomState(room, s.roomState)) {
      error = "save state's room state is damaged";
      return false;
    }
  }
  return true;
}

bool ReadSlot(int slot, State& s, std::string& error) {
  const std::string path = SlotPath(slot);
  if (path.empty()) {
    error = "no user directory, so no save state folder";
    return false;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = SlotName(slot) + " is empty";
    return false;
  }
  std::string bytes((std::istreambuf_iterator< char >(file)), std::istreambuf_iterator< char >());
  if (slot == kResumeSlot) {
    std::string inner;
    if (!PortResume::UnwrapContainer(bytes, s.resumeHeader, inner)) {
      error = path + ": not a resume file";
      return false;
    }
    bytes = std::move(inner);
  }
  if (!Decode(bytes, s, error)) {
    error = path + ": " + error;
    return false;
  }
  return true;
}

// Atomic: a temp file, then a rename, so a crash mid-write leaves the old file.
bool WriteFile(const std::string& path, const std::string& bytes, std::string& error) {
  if (path.empty()) {
    error = "no user directory, so no save state folder";
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(Folder(), ec);
  const std::string temp = path + ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast< std::streamsize >(bytes.size()));
    file.flush();
    file.close();
    if (!file) {
      error = "cannot write " + temp;
      std::filesystem::remove(temp, ec);
      return false;
    }
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    error = "cannot replace " + path + ": " + ec.message();
    return false;
  }
  return true;
}

bool WriteSlot(int slot, const State& s, std::string& error) {
  std::string bytes = Encode(s);
  if (slot == kResumeSlot) {
    PortResume::Header h;
    h.fileIdx = gpGameState != nullptr ? gpGameState->SystemOptions().GetSaveIdx() : 0;
    h.cardSerial = gpGameState != nullptr ? gpGameState->GetCardSerial() : 0;
    h.savedAt = s.savedAt;
    bytes = PortResume::WrapContainer(h, bytes);
  }
  return WriteFile(SlotPath(slot), bytes, error);
}

// --- game side -------------------------------------------------------------------------------

// Why the game cannot be saved or loaded from right now, or nullptr when it can.
const char* NotReady(CStateManager& mgr) {
  if (gpGameState == nullptr || gpMain == nullptr || mgr.World() == nullptr ||
      mgr.GetPlayer(0) == nullptr) {
    return "no game running";
  }
  if (!mgr.IsFullyInitialized() || mgr.mQuitGame) {
    return "the world is loading";
  }
  if (sPlace.active || sRoom.active || PortWarp::Pending() ||
      mgr.mPortPendingWarpArea != kInvalidAreaId ||
      mgr.mPortSpawnAtArea != kInvalidAreaId || mgr.mPortWarpQueuedArea != kInvalidAreaId) {
    return "a warp or load is in progress";
  }
  if (mgr.GetGameState() != CStateManager::kGS_Running) {
    return "the game is paused";
  }
  return nullptr;
}

bool Capture(CStateManager& mgr, State& s, std::string& error) {
  if (const char* why = NotReady(mgr)) {
    error = why;
    return false;
  }
  CWorld& world = *mgr.World();
  const TAreaId area = world.GetCurrentAreaId();
  if (!world.DoesAreaExist(area) || world.Area(area) == nullptr) {
    error = "Samus is in no area";
    return false;
  }
  const CGameArea& gameArea = *world.Area(area);
  s.worldId = world.IGetWorldAssetId();
  s.areaIndex = area.Value();
  s.areaAssetId = gameArea.GetAreaAssetId();
  // The English name when the room list has it (same thread: CStateManager feeds it), else the MLVL's.
  s.room = PortOverlay::RoomName(static_cast< uint32_t >(s.worldId), area.Value());
  if (s.room.empty()) {
    s.room = gameArea.IGetInternalAreaName().c_str();
  }
  if (s.room.empty()) {
    s.room = "Area " + std::to_string(area.Value());
  }

  const CPlayer& player = *mgr.GetPlayer(0);
  const CVector3f pos = player.GetTranslation();
  const CVector3f fwd = player.GetTransform().GetForward();
  s.pos[0] = pos.GetX();
  s.pos[1] = pos.GetY();
  s.pos[2] = pos.GetZ();
  s.fwd[0] = fwd.GetX();
  s.fwd[1] = fwd.GetY();
  s.fwd[2] = fwd.GetZ();
  const CPlayer::EPlayerMorphBallState morph = player.GetMorphballTransitionState();
  s.morphed = morph == CPlayer::kMS_Morphed || morph == CPlayer::kMS_Morphing;
  s.playTime = gpGameState->GetTotalPlayTime();
  s.savedAt = static_cast< int64_t >(std::time(nullptr));

  // A card save records the area it was made in; do the same for the stream, and leave the
  // desired area as it was (the load sets it from the header).
  CWorldState& worldState = gpGameState->CurrentWorldState();
  const CAssetId desired = worldState.GetDesiredAreaAssetId();
  worldState.SetAreaId(area);
  s.blob.assign(kBlobSize, 0);
  {
    CMemoryStreamOut stream(s.blob.data(), s.blob.size());
    CBitStreamWriter writer(stream);
    gpGameState->PutTo(writer);
  }
  // Dark/Light ammo above 250 (Beam ammo per expansion) does not fit the retail blob.
  gpGameState->GetPlayerState()->PortRememberBeamAmmo(s.blob.data(), s.blob.size(), true);
  worldState.SetDesiredAreaAssetId(desired);
  if (PortOverlay::SaveStateRoomState()) {
    CaptureRoomState(mgr, s.roomState);
  }
  return true;
}

// PortKeepWorldPakReadable (CStateManager.cpp) for the platform layer: un-stash the pak that owns
// `worldId`, because that world stays live until the restart replaces it.
void KeepWorldPakReadable(CAssetId worldId) {
  if (worldId == kInvalidAssetId || gpResourceFactory == nullptr) {
    return;
  }
  CResLoader& loader = gpResourceFactory->GetResLoader();
  for (int i = 0; i < loader.GetPakCount(); ++i) {
    CPakFile* pak = loader.GetPakFile(i);
    if (pak == nullptr || !pak->IsWorldPak() || !pak->IsCompletelyLoaded()) {
      continue;
    }
    const rstl::vector< rstl::pair< rstl::string, SObjectTag > >& names = pak->GetStringToObjectList();
    for (rstl::vector< rstl::pair< rstl::string, SObjectTag > >::const_iterator it = names.begin();
         it != names.end(); ++it) {
      if (it->second.GetId() == worldId) {
        pak->EnsureWorldPakReady();
        return;
      }
    }
  }
}

// CMain::StreamNewGameState with the state's own stream, keeping the old game state alive.
void SwapGameState(const State& s) {
  const CPersistentOptions systemOptions = gpGameState->SystemOptions();
  const u64 cardSerial = gpGameState->GetCardSerial();
  const rstl::reserved_vector< rstl::vector< uchar >, 3 > states = gpGameState->GetCompressedGameStates();
  const rstl::reserved_vector< rstl::vector< uchar >, 3 > options = gpGameState->GetCompressedGameOptions();
  const rstl::vector< uchar > multiplayerOptions = gpGameState->GetCompressedMultiplayerOptions();
  const CGameOptions gameOptions = gpGameState->GameOptions();

  rstl::single_ptr< CGameState >& owner = gpMain->GetGameGlobalObjects()->GameState();
  delete sRetired;
  sRetired = owner.release();
  {
    CMemoryInStream stream(s.blob.data(), s.blob.size());
    CBitStreamReader reader(stream);
    owner = rs_new CGameState(reader);
  }
  owner->GetPlayerState()->PortApplyBeamAmmo(s.blob.data(), s.blob.size());
  gpGameState = owner.get();
  gpGameState->SystemOptions() = systemOptions;
  gpGameState->SetCompressedGameStates(states);
  gpGameState->SetCompressedGameOptions(options);
  gpGameState->SetCompressedMultiplayerOptions(multiplayerOptions);
  gpGameState->GameOptions() = gameOptions;
  gpGameState->GameOptions().EnsureOptions();
  gpGameState->SetCardSerial(cardSerial);
}

void DoSave(CStateManager& mgr, int slot) {
  State s;
  std::string error;
  // The title and file select run a CMFGame in the 'FRND' front-end world: a state of it is useless
  // and would overwrite the slot. Loads stay allowed there.
  if (gpGameState != nullptr && gpGameState->GetGameMode().GetGameModeType() == 'FRND') {
    SetMessage("save to slot " + std::to_string(slot) + " failed: no game running (title screen)");
    return;
  }
  if (!Capture(mgr, s, error) || !WriteSlot(slot, s, error)) {
    SetMessage("save to slot " + std::to_string(slot) + " failed: " + error);
    return;
  }
  SetMessage("saved slot " + std::to_string(slot) + " (" + s.room + ")");
}

bool LoadState(CStateManager& mgr, const State& s, int slot, const std::string& what);

bool DoLoad(CStateManager& mgr, int slot) {
  const std::string what = "load of " + SlotName(slot);
  if (const char* why = NotReady(mgr)) {
    SetMessage(what + " failed: " + why);
    return false;
  }
  State s;
  std::string error;
  if (!ReadSlot(slot, s, error)) {
    SetMessage(what + " failed: " + error);
    return false;
  }
  return LoadState(mgr, s, slot, what);
}

// The overlay's room Reload: the current state loaded straight back without its room objects, so
// the room is rebuilt from the disc (as leaving and coming back) and Samus is put back where she
// stood. No slot file is written, so the undo slot keeps the player's last load.
bool DoRoomReload(CStateManager& mgr) {
  State s;
  std::string error;
  if (!Capture(mgr, s, error)) {
    SetMessage("room reload failed: " + error);
    return false;
  }
  s.roomState = RoomState();
  return LoadState(mgr, s, kNoSlot, "room reload");
}

bool LoadState(CStateManager& mgr, const State& s, int slot, const std::string& what) {
  std::string error;
  // PortWarpToWorld refuses an invalid world; check here, before the game state is swapped.
  if (s.worldId == kInvalidAssetId || s.areaAssetId == kInvalidAssetId) {
    SetMessage(what + " failed: the state names no world or area");
    return false;
  }
  // A world the disc no longer has (another disc, another version) would leave the swapped game
  // state with nowhere to go: the save's world table and the disc's MLVLs are the authority.
  if (mgr.PortFirstAreaOfWorld(s.worldId) == kInvalidAssetId) {
    char id[16];
    std::snprintf(id, sizeof id, "%08x", static_cast< uint32_t >(s.worldId));
    SetMessage(what + " failed: world " + id + " is not on this disc");
    return false;
  }
  if (slot == kResumeSlot) {
    // A resume belongs to one card file of one card: loading it into another game would hand that
    // game this one's items and map.
    if (s.resumeHeader.fileIdx != gpGameState->SystemOptions().GetSaveIdx() ||
        !PortResume::Matches(s.resumeHeader, gpGameState->GetCardSerial())) {
      SetMessage(what + " failed: it is from another memory card file");
      return false;
    }
  }
  std::string undo;
  if (slot != kNoSlot && slot != kUndoSlot && slot != kResumeSlot) {
    State current;
    if (!Capture(mgr, current, error) || !WriteSlot(kUndoSlot, current, error)) {
      undo = " (no undo state: " + error + ")";
    }
  }

  const CAssetId liveWorld = gpGameState->CurrentWorldAssetId();
  SwapGameState(s);
  // Rebuild the areas with the layers their objects were spawned with (as each area lists them).
  DisarmSpawnLayers();
  if (PortOverlay::SaveStateRoomState()) {
    ArmSpawnLayers(s.roomState, static_cast< uint32_t >(s.worldId));
  }
  mgr.PortWarpToWorld(s.worldId, s.areaAssetId);
  if (liveWorld != s.worldId) {
    KeepWorldPakReadable(liveWorld);
  }

  sPlace = Placement();
  sPlace.active = true;
  sPlace.state = s;
  sPlace.oldMgr = &mgr;
  sPlace.oldWorld = mgr.World();
  if (slot == kResumeSlot) {
    sResuming = true;
    sResumingSavedAt = s.savedAt;
  }
  SetMessage((slot != kNoSlot ? "loading " + SlotName(slot) : "reloading the room") + " (" + s.room + ")" + undo);
  return true;
}

// --- quick resume (MP1's port_resume) --------------------------------------------------------
//
// While the game runs safely, a snapshot of it is taken in memory and written at once to
// `<user>/savestates/resume.mp2ss` (a temp file and a rename), so a crash or kill -9 leaves the
// last one. A `running` marker next to it, written at launch and removed at a clean exit, tells the
// next launch the last run crashed; after a resume it reads "resumed <savedAt>", and a crash that
// finds its own snapshot still marked so makes the prompt default to No (the snapshot may be what
// crashes the game).

bool ResumeActive() {
  if (!PortOverlay::QuickResume()) {
    return false;
  }
  const char* env = std::getenv("MP2_RESUME");
  const bool forced = env != nullptr && std::strcmp(env, "1") == 0;
  if (env != nullptr && std::strcmp(env, "0") == 0) {
    return false;
  }
  // A test run that MP2_FRAMES ends would otherwise hand its end state to the next one on the same
  // user directory; those write one only when asked to (MP2_RESUME=1).
  return forced || std::getenv("MP2_FRAMES") == nullptr;
}

std::mutex sSnapMutex;
struct Snapshot {
  bool have = false;
  std::string bytes; // the container, as it is written
  std::string room;
  int64_t key = 0; // world and area, to tell "the same room"
  int fileIdx = -1;
  int64_t savedAt = 0;
  uint32_t gen = 0;
  uint32_t writtenGen = 0;
  std::chrono::steady_clock::time_point taken;
};
Snapshot sSnap;

bool sInit = false;
bool sLastRunCrashed = false;
std::string sLastMarker;

std::string MarkerPath() {
  const std::string folder = Folder();
  return folder.empty() ? std::string() : folder + "/running";
}

void WriteMarker(const std::string& text) {
  const std::string path = MarkerPath();
  if (path.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(Folder(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text << '\n';
  file.flush();
  file.close();
  if (!file) {
    // Without the marker a crash goes undetected, so the user is told once per failure.
    std::fprintf(stderr, "[savestate] cannot write the crash marker %s\n", path.c_str());
  }
}

void ResumeQuitCleanly() {
  const std::string path = MarkerPath();
  if (!path.empty()) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }
}

// Once, when the user directory is known: reads a marker left by a crashed run, writes ours.
void EnsureInit() {
  if (sInit || !ResumeActive()) {
    return;
  }
  const std::string path = MarkerPath();
  if (path.empty()) {
    return;
  }
  sInit = true;
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    sLastRunCrashed = true;
    std::ifstream file(path);
    std::getline(file, sLastMarker);
    while (!sLastMarker.empty() && (sLastMarker.back() == '\r' || sLastMarker.back() == ' ')) {
      sLastMarker.pop_back();
    }
    SetMessage("resume: the last run did not quit cleanly");
  }
  WriteMarker("running");
  std::atexit(ResumeQuitCleanly);
}

void ResumeMarkResumed(int64_t savedAt) {
  if (sInit) {
    WriteMarker(PortResume::ResumedMarker(savedAt));
  }
}

// Writes the held snapshot unless it was written already. Any thread.
void FlushSnapshot() {
  std::string bytes;
  uint32_t gen = 0;
  {
    std::lock_guard< std::mutex > lock(sSnapMutex);
    if (!sSnap.have || sSnap.writtenGen == sSnap.gen) {
      return;
    }
    bytes = sSnap.bytes;
    gen = sSnap.gen;
  }
  std::string error;
  if (!WriteFile(SlotPath(kResumeSlot), bytes, error)) {
    SetMessage("resume: not written - " + error);
    return;
  }
  std::lock_guard< std::mutex > lock(sSnapMutex);
  if (sSnap.gen == gen) {
    sSnap.writtenGen = gen;
  }
}

// Why a snapshot cannot be taken now, or nullptr.
const char* ResumeSkipReason(CStateManager& mgr) {
  if (const char* why = NotReady(mgr)) {
    return why;
  }
  const CPlayerState* ps = mgr.GetPlayerState();
  if (ps == nullptr || !ps->IsPlayerAlive()) {
    return "Samus is dead";
  }
  CCameraManager* cameras = mgr.CameraManager(0);
  if (cameras != nullptr && cameras->IsInCinematicCamera()) {
    return "a cutscene is playing";
  }
  if (mgr.GetWantsToEnterSaveGameScreen() || mgr.GetWantsToEnterMapScreen() ||
      mgr.GetWantsToEnterPauseScreen() || mgr.GetWantsToEnterLogBookScreen() ||
      mgr.GetWantsToEnterMessageScreen()) {
    return "a screen is about to open";
  }
  return nullptr;
}

// Captures the running game into the held snapshot and writes it.
bool TakeSnapshot(CStateManager& mgr) {
  // No card serial: a save state loaded from the title, or "continue without saving". No card
  // file can take such a state at the title, so it isn't kept.
  if (gpGameState->GetCardSerial() == 0) {
    return false;
  }
  State s;
  std::string error;
  if (!Capture(mgr, s, error)) {
    return false;
  }
  PortResume::Header h;
  h.fileIdx = gpGameState->SystemOptions().GetSaveIdx();
  h.cardSerial = gpGameState->GetCardSerial();
  h.savedAt = s.savedAt;
  {
    std::lock_guard< std::mutex > lock(sSnapMutex);
    sSnap.bytes = PortResume::WrapContainer(h, Encode(s));
    sSnap.room = s.room;
    sSnap.key = (static_cast< int64_t >(s.worldId) << 32) ^ static_cast< uint32_t >(s.areaIndex);
    sSnap.fileIdx = h.fileIdx;
    sSnap.savedAt = h.savedAt;
    sSnap.taken = std::chrono::steady_clock::now();
    sSnap.have = true;
    ++sSnap.gen;
  }
  FlushSnapshot();
  return true;
}

// Every game update: takes a snapshot when one is due.
void TickResume(CStateManager& mgr) {
  EnsureInit();
  if (!sInit || !ResumeActive()) {
    return;
  }
  static const char* sLastWhy = nullptr;
  static int64_t sRoomKey = 0;
  static std::chrono::steady_clock::time_point sRoomSince;
  static bool sHaveRoom = false;
  const auto now = std::chrono::steady_clock::now();
  const char* why = ResumeSkipReason(mgr);
  if (why != sLastWhy) {
    sLastWhy = why;
    if (why != nullptr) {
      SetMessage(std::string("resume: no snapshot while ") + why);
    }
  }
  if (why != nullptr) {
    sHaveRoom = false; // the time in a room counts from when it is safe again
    return;
  }
  CWorld& world = *mgr.World();
  const int64_t key = (static_cast< int64_t >(world.IGetWorldAssetId()) << 32) ^
                      static_cast< uint32_t >(world.GetCurrentAreaId().Value());
  if (!sHaveRoom || key != sRoomKey) {
    sHaveRoom = true;
    sRoomKey = key;
    sRoomSince = now;
  }
  bool have;
  bool sameRoom;
  double sinceSnapshot;
  const int fileIdx = gpGameState->SystemOptions().GetSaveIdx();
  {
    std::lock_guard< std::mutex > lock(sSnapMutex);
    if (sSnap.have && sSnap.fileIdx != fileIdx) {
      sSnap.have = false; // another card file: the snapshot belongs to a game that was left
    }
    have = sSnap.have;
    sameRoom = have && sSnap.key == key;
    sinceSnapshot = have ? std::chrono::duration< double >(now - sSnap.taken).count() : 0.0;
  }
  if (PortResume::RefreshDue(true, have, sameRoom,
                             std::chrono::duration< double >(now - sRoomSince).count(),
                             sinceSnapshot)) {
    TakeSnapshot(mgr);
  }
}

// Put Samus back once the rebuilt world runs.
void TickPlacement(CStateManager& mgr) {
  if (++sPlace.waited > kPlaceTimeoutFrames) {
    sPlace.active = false;
    DisarmSpawnLayers();
    SetMessage("load timed out before the world came back; Samus stays at the area's spawn");
    return;
  }
  // The load quit the old manager, so one that is not quitting is the rebuilt one even if the
  // allocator handed it the old one's address.
  const bool restarted =
      &mgr != sPlace.oldMgr || mgr.World() != sPlace.oldWorld || !mgr.mQuitGame;
  if (!restarted || mgr.mQuitGame || !mgr.IsFullyInitialized() || mgr.World() == nullptr ||
      mgr.GetPlayer(0) == nullptr || mgr.GetGameState() != CStateManager::kGS_Running ||
      PortWarp::Pending() || mgr.mPortPendingWarpArea != kInvalidAreaId ||
      mgr.mPortSpawnAtArea != kInvalidAreaId || mgr.mPortWarpQueuedArea != kInvalidAreaId) {
    sPlace.settled = 0;
    return;
  }
  // The old world, and with it every pointer into the replaced game state, is gone.
  delete sRetired;
  sRetired = nullptr;
  if (mgr.World()->IGetWorldAssetId() != sPlace.state.worldId) {
    sPlace.active = false;
    DisarmSpawnLayers();
    SetMessage("load landed in another world; Samus stays at its spawn");
    return;
  }
  if (CCameraManager* cameras = mgr.CameraManager(0);
      cameras != nullptr && cameras->IsInCinematicCamera()) {
    sPlace.settled = 0;
    return;
  }
  if (++sPlace.settled < kSettleFrames) {
    return;
  }
  sPlace.active = false;

  const State& s = sPlace.state;
  CPlayer& player = *mgr.GetPlayer(0);
  const CVector3f pos(s.pos[0], s.pos[1], s.pos[2]);
  // Face along the saved forward in the horizontal plane, as the console's `face` does.
  float lx = s.fwd[0];
  float ly = s.fwd[1];
  const float len = std::sqrt(lx * lx + ly * ly);
  if (len < 1e-4f || !std::isfinite(len)) {
    lx = 0.f;
    ly = 1.f;
  } else {
    lx /= len;
    ly /= len;
  }
  const CVector3f look(pos.GetX() + lx, pos.GetY() + ly, pos.GetZ());
  player.Teleport(CTransform4f::LookAt(pos, look, CVector3f::Up()), mgr, true);
  player.SetVelocityWR(CVector3f(0.f, 0.f, 0.f));
  // The rebuilt player has no area until its first tick files it; give it Samus's before that
  // tick so nothing the restore starts sees an area-less player (MP1 sync 14-25).
  mgr.SetActorAreaId(player, mgr.World()->GetCurrentAreaId());
  const CPlayer::EPlayerMorphBallState morph = player.GetMorphballTransitionState();
  const bool morphedNow = morph == CPlayer::kMS_Morphed || morph == CPlayer::kMS_Morphing;
  if (morphedNow != s.morphed) {
    player.SetSpawnedMorphBallState(s.morphed ? CPlayer::kMS_Morphed : CPlayer::kMS_Unmorphed, mgr);
  }
  if (CCameraManager* cameras = mgr.CameraManager(0)) {
    cameras->ResetCameras(mgr);
  }
  SetMessage("loaded " + s.room);
  if (sResuming) {
    // The file stays until the next snapshot replaces it: a crash before then still resumes.
    sResuming = false;
    ResumeMarkResumed(sResumingSavedAt);
    SetMessage("resume: Samus is back in " + s.room);
  }
  if (PortOverlay::SaveStateRoomState() && !s.roomState.areas.empty()) {
    sRoom = RoomRestore();
    sRoom.active = true;
    sRoom.state = s.roomState;
    // One entry per layer the saved objects live in: the area is the editor id's top bits, the layer
    // its next byte (the same split port_savestate_room.cpp uses for LayerKey).
    for (const RoomObject& obj : sRoom.state.objects) {
      // The layer is the editor id's top six bits, the area the field below it (LayerKey's split).
      const std::pair< int, int > key(TEditorId(obj.editorId).AreaNum(),
                                      static_cast< int >(obj.editorId >> 26));
      bool seen = false;
      for (const std::pair< int, int >& have : sRoom.layers) {
        if (have == key) {
          seen = true;
          break;
        }
      }
      if (!seen) {
        sRoom.layers.push_back(key);
      }
    }
  }
}

// How far the room restore has got (areas applied and seen, layers requested, areas waiting, the
// areas on the world's loading chain), so a stall can be told from a slow load. `busy` is set while
// a layer streams or an area is loading.
uint64_t RoomSignature(CStateManager& mgr, int waiting, bool& busy) {
  busy = busy || mgr.PortLayerRequestPending();
  uint64_t signature = sRoom.progress.done.size();
  signature = signature * 131 + sRoom.progress.seen.size();
  signature = signature * 131 + sRoom.nextLayer;
  signature = signature * 131 + static_cast< uint32_t >(waiting);
  for (CGameArea::CConstChainIterator it = mgr.World()->GetChainHead(CWorld::kC_Loading);
       CWorld::skGlobalEnd != it; ++it) {
    busy = true;
    signature = signature * 131 + static_cast< uint32_t >(it->GetId().Value());
  }
  return signature;
}

// Apply the room state to each saved area once it is back, Samus's own first.
void TickRoom(CStateManager& mgr) {
  if (mgr.mQuitGame || !mgr.IsFullyInitialized() || mgr.World() == nullptr ||
      mgr.World()->IGetWorldAssetId() != sPlace.state.worldId) {
    sRoom.active = false;
    DisarmSpawnLayers();
    SetMessage("room state dropped: the world changed before it was applied");
    return;
  }
  // Activate the saved layers first, one per tick: a layer's objects only exist once its data has
  // streamed in, and PortWarpToLayer is the port's own path for that (the `LAYER<n>` token uses it).
  // PortWarpToLayer no-ops on a layer that is already active, so an unnecessary entry costs a call.
  // The last layer is waited out too: its objects only exist once its stream finished.
  ++sRoom.waited;
  int waiting = -1;
  // A reload comes up with ~1008 of the 1024 object ids taken and purges a few hundred frames later;
  // a layer streamed in before that runs the pool dry (AllocateUniqueId aborts). Wait, as
  // PortWarpSettle does, without counting the wait as idle.
  const bool idsTight = mgr.PortFreeObjectIds() < kRoomMinFreeIds;
  if (sRoom.nextLayer < sRoom.layers.size() || mgr.PortLayerRequestPending()) {
    if (sRoom.nextLayer < sRoom.layers.size() && !mgr.PortLayerRequestPending() && !idsTight) {
      const std::pair< int, int >& want = sRoom.layers[sRoom.nextLayer++];
      // A layer the state's own layer bits have off (the intro cinematic's once it has played)
      // keeps its saved objects in the room, but switching it on would run its load scripts again.
      if (mgr.PortLayerFlagOn(TAreaId(want.first), want.second)) {
        mgr.PortWarpToLayer(TAreaId(want.first), want.second);
      }
    }
  } else {
    waiting = ApplyRoomState(mgr, sRoom.state, sRoom.progress);
  }
  if (waiting != 0) {
    // Give up only once nothing has moved for kRoomTimeoutFrames: a world that is still loading
    // areas or streaming a layer, or a saved area that just came back, restarts the count.
    bool busy = idsTight && sRoom.nextLayer < sRoom.layers.size();
    const uint64_t signature = RoomSignature(mgr, waiting, busy);
    if (busy || signature != sRoom.signature) {
      sRoom.signature = signature;
      sRoom.idle = 0;
    } else {
      ++sRoom.idle;
    }
    if (sRoom.idle <= kRoomTimeoutFrames && sRoom.waited <= kRoomHardCapFrames) {
      return;
    }
    std::fprintf(stderr, "[savestate] room: giving up after %d frames, the last %d with nothing "
                         "changing\n",
                 sRoom.waited, sRoom.idle);
    if (waiting < 0) {
      waiting = ApplyRoomState(mgr, sRoom.state, sRoom.progress); // the layers never settled
    }
  }
  sRoom.active = false;
  DisarmSpawnLayers();
  const RoomApplyStats& st = sRoom.progress.stats;
  SetMessage("room state restored: " + std::to_string(sRoom.progress.done.size()) + " of " +
             std::to_string(sRoom.state.areas.size()) + " areas, " + std::to_string(st.applied) +
             " objects applied, " + std::to_string(st.changed) + " changed, " +
             std::to_string(st.removed) + " removed, " + std::to_string(st.unmatched) +
             " unmatched, " + std::to_string(st.otherLayer) + " on other layers" +
             (waiting > 0 ? ", " + std::to_string(waiting) + " areas never loaded" : ""));
}

} // namespace

std::string Folder() {
  const std::string backups = PortSave::BackupsDirectory();
  if (backups.empty()) {
    return std::string();
  }
  return PortUtf8::ToUtf8(PortUtf8::FromUtf8(backups).parent_path()) + "/savestates";
}

Info SlotInfo(int slot) {
  Info info;
  State s;
  std::string error;
  if (!ReadSlot(slot, s, error)) {
    return info;
  }
  info.exists = true;
  info.worldId = s.worldId;
  info.areaIndex = s.areaIndex;
  info.room = s.room;
  info.playTime = s.playTime;
  info.savedAt = s.savedAt;
  info.morphed = s.morphed;
  return info;
}

int SelectedSlot() { return sSelected.load(); }

void SetSelectedSlot(int slot) {
  if (slot >= 1 && slot <= kSlotCount) {
    sSelected.store(slot);
  }
}

void SelectNextSlot() { sSelected.store(sSelected.load() % kSlotCount + 1); }

void SelectPrevSlot() { sSelected.store((sSelected.load() + kSlotCount - 2) % kSlotCount + 1); }

void RequestSave(int slot, bool toast) {
  if (slot >= 0 && slot <= kSlotCount) {
    sSaveToast.store(toast);
    sSaveRequest.store(slot);
  }
}

void RequestLoad(int slot, bool toast) {
  if (slot >= 0 && slot <= kSlotCount) {
    sLoadToast.store(toast);
    sLoadRequest.store(slot);
  }
}

std::string LastMessage() {
  std::lock_guard< std::mutex > lock(sMessageMutex);
  return sMessage;
}

// Test hook: MP2_SAVESTATE_AT=<save frame>,<load frame> queues a save of the selected slot and
// later a load of it, counted in Tick calls (game updates). Lets a headless run exercise the
// round trip, since the harness cannot press F5/F9.
void TestHook() {
  static const char* env = std::getenv("MP2_SAVESTATE_AT");
  static long saveAt = -1;
  static long loadAt = -1;
  static bool parsed = false;
  static long frame = 0;
  if (!parsed) {
    parsed = true;
    if (env != nullptr) {
      std::sscanf(env, "%ld,%ld", &saveAt, &loadAt);
    }
  }
  ++frame;
  if (frame == saveAt) {
    RequestSave(SelectedSlot());
  }
  if (frame == loadAt) {
    RequestLoad(SelectedSlot());
  }
}

std::atomic< bool > sResumeAccepted{false};
std::atomic< bool > sResumeLoadRequest{false};
std::atomic< bool > sResumeSnapshotRequest{false};

// Quick resume, read side. Two ways in: the prompt's Yes (sResumeAccepted; the front end is skipped
// into the same card file and this loads the resume over it once the game runs), or an
// MP2_SKIP_FRONTEND launch, which loads it without asking. An explicit boot warp (MP2_WORLD /
// MP2_TELEPORT) wins over both.
void ResumeCheck(CStateManager& mgr) {
  static bool decided = false;
  if (decided || NotReady(mgr) != nullptr) {
    return;
  }
  const bool accepted = sResumeAccepted.load();
  decided = true;
  if (!accepted) {
    if (!ResumeActive() || !PortSkipFrontEnd::Started()) {
      return;
    }
    if (std::getenv("MP2_WORLD") != nullptr || std::getenv("MP2_TELEPORT") != nullptr) {
      SetMessage("resume: a boot warp (MP2_WORLD/MP2_TELEPORT) was asked for; the resume state waits");
      return;
    }
  }
  const std::string path = SlotPath(kResumeSlot);
  std::error_code ec;
  if (path.empty() || !std::filesystem::exists(path, ec)) {
    return;
  }
  SetMessage("resume: found " + path + "; loading it instead of the card's save");
  DoLoad(mgr, kResumeSlot);
}

bool ResumeOffer(ResumeInfo& out) {
  if (!ResumeActive()) {
    return false;
  }
  EnsureInit();
  State s;
  std::string error;
  if (!ReadSlot(kResumeSlot, s, error) || s.worldId == kInvalidAssetId ||
      s.areaAssetId == kInvalidAssetId) {
    return false;
  }
  out.room = s.room;
  out.savedAt = s.savedAt;
  out.fileIdx = s.resumeHeader.fileIdx;
  out.crashedAfterThis = sLastRunCrashed;
  out.crashedAgain = PortResume::CrashedAgain(sLastRunCrashed, sLastMarker, s.savedAt);
  return true;
}

bool ResumeBootArea(uint32_t& world, uint32_t& area) {
  // The same two entries as ResumeCheck: the prompt's Yes, or an MP2_SKIP_FRONTEND launch that no
  // boot warp overrides.
  if (!sResumeAccepted.load()) {
    if (!ResumeActive() || std::getenv("MP2_WORLD") != nullptr || std::getenv("MP2_TELEPORT") != nullptr) {
      return false;
    }
  }
  EnsureInit();
  State s;
  std::string error;
  if (!ReadSlot(kResumeSlot, s, error) || s.worldId == kInvalidAssetId ||
      s.areaAssetId == kInvalidAssetId || gpGameState == nullptr ||
      s.resumeHeader.fileIdx != gpGameState->SystemOptions().GetSaveIdx() ||
      !PortResume::Matches(s.resumeHeader, gpGameState->GetCardSerial()) ||
      gpMain == nullptr || PortWarp::Pending()) {
    return false;
  }
  world = static_cast< uint32_t >(s.worldId);
  area = static_cast< uint32_t >(s.areaAssetId);
  return true;
}

int ResumeAccept(const ResumeInfo& info) {
  sResumeAccepted.store(true);
  SetMessage("resume: accepted (" + info.room + ", file " + std::to_string(info.fileIdx) + ")");
  return info.fileIdx;
}

void ResumeClear() {
  {
    std::lock_guard< std::mutex > lock(sSnapMutex);
    sSnap = Snapshot();
  }
  std::error_code ec;
  const std::string path = SlotPath(kResumeSlot);
  if (!path.empty()) {
    std::filesystem::remove(path, ec);
  }
  SetMessage("resume: cleared");
}

std::string ResumeStatus() {
  std::string out = std::string("quick resume ") + (PortOverlay::QuickResume() ? "on" : "off") +
                    (ResumeActive() ? "" : " (inactive this run)");
  State s;
  std::string error;
  if (ReadSlot(kResumeSlot, s, error)) {
    out += "; file: " + s.room + ", card file " + std::to_string(s.resumeHeader.fileIdx) +
           ", saved " + std::to_string(s.savedAt);
  } else {
    out += "; no file (" + error + ")";
  }
  std::lock_guard< std::mutex > lock(sSnapMutex);
  out += sSnap.have ? "; snapshot held (" + sSnap.room + (sSnap.writtenGen == sSnap.gen ? ", written)" : ", unwritten)")
                    : "; no snapshot held";
  out += sLastRunCrashed ? "; the last run crashed" : "; the last run quit cleanly";
  return out;
}

void RequestResumeSnapshot() { sResumeSnapshotRequest.store(true); }
void RequestResumeLoad() { sResumeLoadRequest.store(true); }

void ResumeSave(bool frameLimit) {
  (void)frameLimit; // ResumeActive keeps a test run from writing one
  if (Folder().empty() || !ResumeActive()) {
    return;
  }
  const bool safe = sLiveMgr != nullptr && ResumeSkipReason(*sLiveMgr) == nullptr;
  bool haveSnapshot;
  {
    std::lock_guard< std::mutex > lock(sSnapMutex);
    haveSnapshot = sSnap.have;
  }
  switch (PortResume::ChooseWrite(safe, haveSnapshot)) {
  case PortResume::Write::Fresh:
    if (TakeSnapshot(*sLiveMgr)) {
      SetMessage("resume: wrote " + SlotPath(kResumeSlot));
    }
    break;
  case PortResume::Write::Snapshot:
    FlushSnapshot();
    SetMessage("resume: the game is not in a safe moment; kept the last snapshot");
    break;
  case PortResume::Write::Nothing:
    SetMessage("resume: not written - no game running, no snapshot");
    break;
  }
}

void ResumeFlush() { FlushSnapshot(); }

void ResumeShutdown() {
  FlushSnapshot();
  ResumeQuitCleanly();
}

bool RoomRestoreOwnsArea(CStateManager& mgr, int area) {
  (void)mgr;
  if (!sRoom.active) {
    return false;
  }
  const auto has = [area](const std::vector< int32_t >& v) {
    return std::find(v.begin(), v.end(), area) != v.end();
  };
  if (has(sRoom.progress.done)) {
    return false;
  }
  for (const RoomArea& ra : sRoom.state.areas) {
    if (ra.area == area) {
      std::fprintf(stderr, "[savestate] room: area %d loaded, its queued messages are skipped\n", area);
      return true; // the restore replays this area's saved messages itself
    }
  }
  return false;
}

void Forget(const CStateManager* mgr) {
  if (sLiveMgr == mgr) {
    sLiveMgr = nullptr;
    // The snapshot belongs to the game that went away; the file stays.
    std::lock_guard< std::mutex > lock(sSnapMutex);
    sSnap.have = false;
  }
}

void RequestModReload() { sModReloadRequest.store(true); }
void RequestRoomReload() { sRoomReloadRequest.store(true); }
bool ModReloadActive() {
  return sModReloadRequest.load() || sModReloadPending.load() || sModReloadWorldStale.load();
}

bool ReloadModsNow() {
  if (!sModReloadPending.exchange(false)) {
    return false;
  }
  if (gpResourceFactory != nullptr) {
    gpResourceFactory->GetResLoader().PortReopenPaks(&PortMods::Reload);
    SetMessage("mods reloaded (" + std::to_string(PortMods::CurrentStatus().overlays) + " disc file(s))");
  }
  return true;
}

bool ServicePausedModReload() {
  if (!sModReloadRequest.load() || gpResourceFactory == nullptr) {
    return false;
  }
  if (gpResourceFactory->PortHasPendingLoads() || !gpResourceFactory->GetResLoader().AreAllPaksLoaded()) {
    return false; // a load in flight would lose its PAK; try again next frame
  }
  sModReloadRequest.store(false);
  gpResourceFactory->GetResLoader().PortReopenPaks(&PortMods::Reload);
  SetMessage("mods reloaded (" + std::to_string(PortMods::CurrentStatus().overlays) + " disc file(s)), the room follows when unpaused");
  sModReloadWorldStale.store(true);
  return true;
}

void Tick(CStateManager& mgr) {
  sLiveMgr = &mgr;
  if (sModReloadWorldStale.exchange(false)) {
    sModReloadRequest.store(true);
  }
  TestHook();
  ResumeCheck(mgr);
  if (sPlace.active) {
    TickPlacement(mgr);
  } else if (sRoom.active) {
    TickRoom(mgr);
  }
  TickResume(mgr);
  if (sResumeSnapshotRequest.exchange(false)) {
    SetMessage(TakeSnapshot(mgr) ? "resume: snapshot written"
                                  : std::string("resume: no snapshot - ") +
                                        (ResumeSkipReason(mgr) != nullptr ? ResumeSkipReason(mgr)
                                          : gpGameState->GetCardSerial() == 0 ? "the game has no memory card"
                                                                              : "capture failed"));
  }
  if (sResumeLoadRequest.exchange(false)) {
    DoLoad(mgr, kResumeSlot);
  }
  if (sModReloadRequest.exchange(false)) {
    // The current state goes through the undo slot and loads back, which restarts the world.
    State current;
    std::string error;
    if (!Capture(mgr, current, error) || !WriteSlot(kUndoSlot, current, error)) {
      SetMessage("mods reload failed: " + error);
    } else if (DoLoad(mgr, kUndoSlot)) {
      sModReloadPending.store(true);
    }
  }
  if (sRoomReloadRequest.exchange(false)) {
    DoRoomReload(mgr);
  }
  const int save = sSaveRequest.exchange(-1);
  if (save >= 0) {
    sToastMessages = sSaveToast.exchange(false);
    DoSave(mgr, save);
    sToastMessages = false;
  }
  const int load = sLoadRequest.exchange(-1);
  if (load >= 0) {
    sToastMessages = sLoadToast.exchange(false);
    DoLoad(mgr, load);
    sToastMessages = false;
  }
}

} // namespace PortSaveState

// --- the resume prompt ---------------------------------------------------------------------------
// Staged from the boot's splash (CSplashScreen ticks it), drawn by the overlay (DrawResumePrompt).

namespace {

enum PromptStage { kPromptCheck, kPromptAsk, kPromptAccepting, kPromptDone };

PromptStage sPromptStage = kPromptCheck;
int sPromptTicks = 0;
std::atomic< int > sPromptAnswer{-1}; // -1 none, 0 no, 1 yes (from the overlay's buttons)
std::atomic< bool > sPromptYes{true};
std::atomic< bool > sPromptShown{false};
std::mutex sPromptMutex;
PortSaveState::ResumeInfo sPromptInfo;

constexpr int kPromptIgnoreTicks = 15;  // presses that started the boot do not answer
constexpr int kPromptCheckTicks = 120;  // wait this long for the save folder
constexpr int kPromptAcceptTicks = 600; // give up holding the splash

bool PromptEnvBlocks() {
  return PortSkipFrontEnd::Armed() || std::getenv("MP2_WORLD") != nullptr ||
         std::getenv("MP2_TELEPORT") != nullptr;
}

} // namespace

namespace PortSaveState {

bool ResumePromptTick() {
  switch (sPromptStage) {
  case kPromptCheck: {
    if (!ResumeActive() || PromptEnvBlocks()) {
      sPromptStage = kPromptDone;
      return false;
    }
    if (SlotPath(kResumeSlot).empty() && ++sPromptTicks < kPromptCheckTicks) {
      return false;
    }
    ResumeInfo info;
    if (!ResumeOffer(info)) {
      sPromptStage = kPromptDone;
      return false;
    }
    {
      std::lock_guard< std::mutex > lock(sPromptMutex);
      sPromptInfo = info;
    }
    sPromptYes.store(!info.crashedAgain);
    sPromptAnswer.store(-1);
    sPromptTicks = 0;
    sPromptStage = kPromptAsk;
    sPromptShown.store(true);
    SetMessage("resume: asking" + std::string(info.crashedAgain ? " (crashed again)" : ""));
    return true;
  }
  case kPromptAsk: {
    ++sPromptTicks;
    int answer = sPromptAnswer.exchange(-1);
    // A real mouse button held is a click on the window, whose Yes/No buttons answer: one that
    // also reaches the pad as A must not accept the highlighted Yes first (MP1 6ab7c1e15).
    const bool mouseHeld = SDL_GetMouseState(nullptr, nullptr) != 0;
    if (sPromptTicks > kPromptIgnoreTicks && gpController != nullptr) {
      const auto& pad = gpController->GetGamepadData(0);
      if (pad.GetButton(kBU_Left).GetPressEvent() || pad.GetButton(kBU_Right).GetPressEvent()) {
        sPromptYes.store(!sPromptYes.load());
      }
      if (answer < 0 && !mouseHeld && (pad.GetButton(kBU_A).GetPressEvent() ||
                         pad.GetButton(kBU_Start).GetPressEvent())) {
        answer = sPromptYes.load() ? 1 : 0;
      }
    }
    if (answer < 0) {
      if (const char* hook = std::getenv("MP2_RESUME_ANSWER")) {
        if (sPromptTicks > 180) {
          answer = hook[0] == 'y' ? 1 : 0;
        }
      }
    }
    if (answer < 0) {
      return true;
    }
    sPromptShown.store(false);
    if (answer == 0) {
      SetMessage("resume: declined");
      sPromptStage = kPromptDone;
      return false;
    }
    ResumeInfo info;
    {
      std::lock_guard< std::mutex > lock(sPromptMutex);
      info = sPromptInfo;
    }
    PortSkipFrontEnd::ArmFromResume(ResumeAccept(info));
    sPromptTicks = 0;
    sPromptStage = kPromptAccepting;
    return true;
  }
  case kPromptAccepting:
    if (PortSkipFrontEnd::Started() || ++sPromptTicks > kPromptAcceptTicks) {
      sPromptStage = kPromptDone;
      return false;
    }
    return true;
  case kPromptDone:
    break;
  }
  return false;
}

bool ResumePromptShown(ResumeInfo& out, bool& yes) {
  if (!sPromptShown.load()) {
    return false;
  }
  {
    std::lock_guard< std::mutex > lock(sPromptMutex);
    out = sPromptInfo;
  }
  yes = sPromptYes.load();
  return true;
}

void AnswerResumePrompt(bool yes) { sPromptAnswer.store(yes ? 1 : 0); }

} // namespace PortSaveState
