#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PortSchemaClass;
struct PortSchemaField;
class CEntity;
class CStateManager;

class CGameArea;
class CEntity;
class CStateManager;
class CWorldLayerState;

// Save states: save anywhere, load back to the same spot (MP1's port_savestate, phase 1).
//
// A state is the retail save data - the same CGameState stream a memory card slot holds (items,
// health, ammo, map, scans, script layers, in-game time) - plus the world and area Samus was in,
// where she stood, which way she faced and whether she was in morph ball. Loading swaps that game
// state in the way CMain::StreamNewGameState does, restarts the world the way the debug world warp
// does (CStateManager::PortWarpToWorld), and once the rebuilt world is running puts Samus back.
//
// Room state (phase 2, MP1's "room state" section): the live script objects of the areas loaded
// at save time - which of them still exist (collected pickups and killed enemies stay gone),
// whether each is active, enemy health and pose, timers, counters, and whether doors stand open
// and are locked, platform and rotator poses, door docks, script-spawned copies (generated
// debris, pickups, enemies are made again from their template) and the scripted messages an
// enemy was sent.
// Once Samus is back it is applied to each saved area as that area finishes loading.
//
// Files: `<user>/savestates/slotN.mp2ss`. Slot 0 is the undo slot, written before every load.
namespace PortSaveState {

constexpr int kSlotCount = 8; // slots 1..8
constexpr int kUndoSlot = 0;  // written before every load

struct Info {
  bool exists = false;
  uint32_t worldId = 0;
  int areaIndex = -1;
  std::string room;     // the area's internal name, or "Area N"
  double playTime = 0.0; // in-game seconds
  int64_t savedAt = 0;   // Unix seconds
  bool morphed = false;
};

// `<user>/savestates`, or empty when the user directory is unknown (states are then off).
std::string Folder();
Info SlotInfo(int slot);

int SelectedSlot();          // 1..kSlotCount, the slot F5/F9 act on
void SetSelectedSlot(int slot);
void SelectNextSlot();
void SelectPrevSlot();

// Queue a save or load of `slot` (0..kSlotCount); the game thread runs it on its next Tick.
// A load of the undo slot does not overwrite the undo slot first. `toast` also shows the result
// on screen (the F5/F9 hotkeys).
void RequestSave(int slot, bool toast = false);
void RequestLoad(int slot, bool toast = false);
// The next load holds the simulation (PortOverlay::SetTickHold) on the tick its room state is
// restored, so a harness can read the world exactly as restored (tools/statecheck.py).
void HoldAfterLoad();

// Reload the mods folder and rebuild the room: the current state is written to the undo slot and
// loaded back, and between the old world and the new one every PAK is closed and opened again over
// the freshly read mods (CResLoader::PortReopenPaks). ReloadModsNow is CMainFlow's call before it
// starts the game; true when it reloaded. ModReloadActive is true from the request until then.
void RequestModReload();
// Rebuild the current room from the disc, as leaving and coming back does, and put Samus back where
// she stands (the overlay's Reload): the current state, without its room objects, loaded back.
void RequestRoomReload();
bool ModReloadActive();
bool ReloadModsNow();
// The pause menu's share: the pause screen holds the suit model, no room is running and Tick does not
// run, so the request cannot take the world-restart route. Returns true once, on the frame it reopened
// the PAKs over the new mods (when no resource is mid-load); the caller then refreshes the pause
// screen's models. The room reloads in full on the next Tick, so the world follows after unpausing.
bool ServicePausedModReload();

// The outcome of the last request, for the overlay ("" before the first).
std::string LastMessage();

// Called on the game thread at the start of every CStateManager update (from PortConsole::Tick).
void Tick(CStateManager& mgr);

// Quick resume (MP1's resume.mpss): `<user>/savestates/resume.mp2ss`, the MPRS container of
// port_resume_logic.h around a state, plus the card file and card it came from. While the game
// runs safely a snapshot is taken (2 s into a room, then every 30 s) and written at once, so a
// clean quit, a crash or kill -9 all leave one; a `running` marker beside it tells the next launch
// the last run crashed. The next launch offers it (the prompt over the splash screen, below);
// Yes boots into the same card file and loads it. MP2_RESUME=0 or the quick_resume setting off
// turn all of it off; a test run (MP2_FRAMES) writes none unless MP2_RESUME=1; MP2_SKIP_FRONTEND
// launches load it without asking, as before. An explicit boot warp (MP2_WORLD / MP2_TELEPORT)
// leaves the file for later. The file is kept until the next snapshot replaces it.
//
// ResumeSave: the quit path (PortPumpEvents). Writes a fresh snapshot when the game is in a safe
// moment, else the last one held. `frameLimit` is the MP2_FRAMES exit of a test run.
void ResumeSave(bool frameLimit = false);
// SDL background / terminate events (any thread): write the held snapshot / also remove the marker.
void ResumeFlush();
void ResumeShutdown();
// ~CStateManager: the quit path must not capture from a destroyed manager; drops the snapshot.
void Forget(const CStateManager* mgr);

struct ResumeInfo {
  std::string room;
  int64_t savedAt = 0;
  int fileIdx = 0;
  bool crashedAfterThis = false; // the last run did not quit cleanly
  bool crashedAgain = false;     // ... and it died right after resuming this very snapshot
};
// Is there a resume to offer (on, a readable file)? Reads the marker of the last run once.
bool ResumeOffer(ResumeInfo& out);
// The player said yes: the game loads the resume once it runs. Returns the card file to boot into.
int ResumeAccept(const ResumeInfo& info);
// PortSkipFrontEnd::StartGame, with the card's save already the running state: when a resume is
// pending (accepted, or an MP2_SKIP_FRONTEND auto-resume) and fits this card file, the world and
// area it was saved in, so the first world the game loads is the resume room and not the card's.
bool ResumeBootArea(uint32_t& world, uint32_t& area);
void ResumeClear();
std::string ResumeStatus();
void RequestResumeSnapshot(); // console: take and write a snapshot now
void RequestResumeLoad();     // console: load the resume file

// The boot's resume prompt. ResumePromptTick runs once per splash tick (game thread) and is true
// while the splash must be held; ResumePromptShown fills what the overlay draws (any thread);
// AnswerResumePrompt is the overlay's Yes/No.
bool ResumePromptTick();
bool ResumePromptShown(ResumeInfo& out, bool& yes);
void AnswerResumePrompt(bool yes);

// --- room state (platform/port_savestate_room.cpp), game thread only -------------------------

// One tagged piece of an object's state; an unknown tag is skipped on load.
struct RoomBlock {
  uint32_t tag = 0;
  std::string data;
};
struct RoomObject {
  uint32_t editorId = 0; // TEditorId::Value(); its AreaNum() is the object's area
  bool active = false;
  std::vector< RoomBlock > blocks;
};
struct RoomArea {
  int32_t area = -1;
  uint32_t assetId = 0; // a restore skips the area when another one sits at that index
  // FNV-1a over the area's layer and generated script data when the state was made. `known` is
  // false for a v2 trailer (written before the fingerprint): that area restores unchecked.
  uint64_t fingerprint = 0;
  bool known = false;
  // The layers whose script objects the area was spawned with (bit n: layer n), which the live
  // layer bits of the memory card stream can differ from. `hasSpawn` is false for a v2/v3 trailer.
  uint64_t spawnLayers = 0;
  bool hasSpawn = false;
};
struct RoomState {
  std::vector< RoomArea > areas;   // the areas that were loaded, so captured
  std::vector< RoomObject > objects;
};
struct RoomApplyStats {
  int applied = 0;   // saved objects found and set
  int removed = 0;   // live objects the state does not have (collected, killed)
  int changed = 0;   // applied objects whose active flag, door or health differed
  int unmatched = 0; // saved objects not in the rebuilt area
  int otherLayer = 0; // live objects of a layer the state has none of, left alone
  int skipped = 0;   // areas left alone: another area asset at the index
  int fields = 0;    // schema fields restored (kTagFields)
};

// A restore in progress: areas already applied, and areas seen loaded on an earlier tick (an
// area is applied one tick after it finishes loading, once its objects took their first messages).
struct RoomApplyProgress {
  std::vector< int32_t > done;
  std::vector< int32_t > seen;
  std::vector< uint32_t > held; // running timers restored stopped, started with the last area
  RoomApplyStats stats;
};

// Game hooks, game thread only; ids are raw TUniqueId values. NoteGenerated marks an object a
// CScriptGenerator or CScriptPickupGenerator copied from a template (it shares the template's
// editor id); NoteScriptMsg sees each message a script connection delivers (scripted enemies
// keep the commands that woke them); NoteRemoved drops what was noted for a removed object.
void NoteGenerated(CStateManager& mgr, uint16_t uid);
void NoteRemoved(CStateManager& mgr, uint16_t uid);
void NoteScriptMsg(CStateManager& mgr, uint16_t dest, uint16_t sender, uint32_t msg);

void CaptureRoomState(CStateManager& mgr, RoomState& out);
// True once after a room restore applied its areas: CStateManager::Update then ends before the
// world update, so the restored room does not run a step before anyone sees it.
bool ConsumeSkipUpdate();
// Debug views (port_statedump.cpp): a script generator's copy; the room-saved state of a timer,
// counter, door, platform, rotator, dock, music or cinematic camera as JSON members.
bool IsScriptGeneratedCopy(const CStateManager& mgr, CEntity& ent);
// Entity fields (port_savestate_fields.cpp): for statedump, whether `ent` gets a kTagFields block,
// and whether the block keeps a schema field.
bool EntityFieldsSaved(const CStateManager& mgr, CEntity& ent);
bool SchemaFieldSaved(const PortSchemaClass& cls, const PortSchemaField& f);
// Captures `ent`'s schema fields into a kTagFields block (false: not an entity that gets one);
// applies one, returning the fields set; whether it covers every class of `ent` (the script
// messages that woke the enemy need no replay then).
bool CaptureEntityFields(const CStateManager& mgr, CEntity& ent, std::string& out);
int ApplyEntityFields(CStateManager& mgr, CEntity& ent, const std::string& data);
bool EntityFieldsCover(CEntity& ent, const std::string& data);
std::string RoomStateJson(CStateManager& mgr, CEntity& ent);
// Applies the saved areas once all of them are ready (or, with `force`, those that are ready now);
// returns how many saved areas are still waiting to load.
int ApplyRoomState(CStateManager& mgr, const RoomState& room, RoomApplyProgress& progress,
                   bool force = false);

// Spawn layers, game thread. CGameArea::VerifyTokenList calls NoteAreaSpawn{Begin,End} around
// listing the layers an area spawns: Begin swaps in the recorded bits of an armed area, End puts
// the live ones back. ArmSpawnLayers (a state load, right after its game state is installed) arms
// each recorded area of world `worldId` (the area spawns with the recorded layers that the loaded
// stream also has on; the swapped bits stay in the world's layer state). DisarmSpawnLayers forgets
// whatever is still armed.
void NoteAreaSpawnBegin(CWorldLayerState& layers, int area);
void NoteAreaSpawnEnd(CWorldLayerState& layers, int area);
void ArmSpawnLayers(const RoomState& room, uint32_t worldId);
void DisarmSpawnLayers();
// CStateManager::AreaLoaded: true while a pending room restore is going to apply this area, whose
// objects carry what the mailbox's relayed messages did (it would send them a second time).
bool RoomRestoreOwnsArea(CStateManager& mgr, int area);
// platform/port_savestate_format.cpp. The room trailer is v4 (v3 plus each area's spawn layers; a
// magic word, then 16-bit block lengths and delta-varint editor ids); Decode also reads v3 and the
// v2 trailer old .mp2ss files carry.
// Encode writes an empty state when a block would not fit its length field, rather than a
// trailer the reader would refuse.
void EncodeRoomState(const RoomState& room, std::string& out);
bool DecodeRoomState(const std::string& in, RoomState& room);
uint64_t Fnv1a64(const void* data, size_t size, uint64_t seed = 0xcbf29ce484222325ull);
uint64_t AreaFingerprint(const CGameArea& area);
// Why a saved area must not be applied to the loaded one (nullptr: restore it): its scripts differ.
const char* RoomAreaSkipReason(const RoomArea& saved, uint64_t liveFingerprint);

} // namespace PortSaveState
