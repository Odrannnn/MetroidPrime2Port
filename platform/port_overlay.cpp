// The F1 debug overlay's settings and game-facing API: the PortOverlayState variables, their
// environment overrides and setters, the runtime state the pages show, and PortOverlay::Draw. The
// window and its pages are in debug_ui/, the Prime 1 port's layout; every control there is wired
// to something that exists, so nothing in the window lies about what it does.
//
// Built by `mp_port_audio` (it needs ImGui, which Aurora brings in with SDL).

#include "port_mobile.h"
#include "port_overlay.h"
#include "port_discord.h"

#include "port_gyro_motion.h"
#include "port_photo.h"
#include "port_video.h"
#include "port_env.h"
#include "port_overlay_internal.h"
#include "port_logfile.h"
#include "port_mouse.h"
#include "port_radial.h"
#include "debug_ui/debug_ui_internal.h"

#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Text/CStringTable.hpp"
#include "MetroidPrime/CMapArea.hpp"
#include "MetroidPrime/CMapWorld.hpp"
#include "MetroidPrime/CMemoryCard.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/IGameArea.hpp"
#include "MetroidPrime/Player/CGameOptions.hpp"
#include "MetroidPrime/CWorldLayerState.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/vi.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <musyx/port_voices.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cfloat>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>


// platform/ai_dma.cpp: the streamed-audio (music, movie) stream's own gain.
extern "C" void AIPortSetOutputGain(float gain);

namespace PortOverlayState {

bool sSkipCinematics = false;
// `MP2_LOG_FILE`: whether stdout and stderr also go to a file (F1 > Testing, console `logfile`).
// PortLogFile::Start reads it before Aurora comes up, so the row only reports the state the next
// launch will take.
bool sLogFile = true;
// The gameplay tweaks (`MP2_FAST_MORPH`, `MP2_SPRING_BALL`, `MP2_LOCK_ON_TOGGLE`,
// `MP2_STICKY_CHARGE`), saved in settings.cfg.
bool sFastMorph = false;
bool sPointerMenus = true;
bool sMapSwapFingers = false;
// Controls > Options > Map: flip either map rotate axis, as Prime 1's #225.
bool sMapInvertX = false;
bool sMapInvertY = false;
int sWalkSpeed = PortOverlay::kWalkSpeedDefault;
int sScanSpeed = PortOverlay::kScanSpeedDefault;
int sMusicBoost = PortOverlay::kVolumeBoostDefault;
int sSfxBoost = PortOverlay::kVolumeBoostDefault;
int sLanguage = 0;
int sElevatorRide = PortOverlay::kElevatorRide_Original;
int sPortalTransition = PortOverlay::kPortalTransition_Original;
int sRotatingRooms = PortOverlay::kRotatingRooms_Original;
int sComboBeamCost = PortOverlay::kComboBeamCostMax;
int sComboMissileCost = PortOverlay::kComboMissileCostMax;
int sBeamAmmoPerExpansion = PortOverlay::kBeamAmmoPerExpansionDefault;
bool sMinimapTap = true;
int sPromptGlyphs = 0;
bool sIgtTimer = false;
bool sOriginalExperience = false;
bool sUnlockHardMode = false;
bool sUnlockGalleries = false;
bool sUnlockBeamDoors = false;
bool sStackBeams = false;
bool sStackBeamsSkipDark = false;
int sHintDelay = 0;
bool sSpringBall = false;
bool sSpringFlick = false;
float sSpringFlickRate = PortOverlay::kSpringFlickRateDefault;
bool sQuickResume = true;
bool sLockOnToggle = false;
bool sStickyCharge = false;
bool sAutoAim = true;
bool sPointerAim = false;
bool sTrilogyRadialSelectors = false;
int sPointerAimBox = PortOverlay::kPointerAimBoxDefault;
int sPointerAimTurn = PortOverlay::kPointerAimTurnDefault;
bool sGyroAim = false;
int sGyroSource = PortOverlay::kGyroSourceDefault;
int sGyroSpace = PortOverlay::kGyroSpaceDefault;
int sGyroAutoCalibrate = PortOverlay::kGyroAutoCalibrateDefault;
float sGyroSmoothing = 0.f;
float sGyroTightening = 0.f;
float sGyroCursorAngle = PortOverlay::kGyroCursorAngleDefault;
int sGyroMode = PortOverlay::kGyroModeDefault;
float sGyroSensitivity = PortOverlay::kGyroSensitivityDefault;
bool sGyroCursorCamera = false;
bool sGyroSplit = false;
float sGyroSensitivityY = PortOverlay::kGyroSensitivityDefault;
bool sResetAimBoth = false;
bool sLockOnFreeAim = false;
bool sMenuStandardButtons = false;
bool sRapidCharge = false;
// `MP2_CARD_BACKUP`: copy the card's save file into save-backups before every card write (F1 >
// System > Memory card, `cardbackup`).
bool sCardBackup = false;
// First-person field of view in degrees, or 0 for the game's own 55 (CCameraManager::sFirstPersonFOV).
float sFirstPersonFov = 0.f;
// Draw the world without the HUD's view overlays (`MP2_NO_HELMET`, `MP2_NO_VISOR_FX`): for
// screenshots of an area, and for looking at what a visor effect covers.
bool sHideHelmet = false;
bool sHideVisorEffects = false;
// `cinema_bars`, `secondary_crosshairs`: the game's own on both. `hud_opacity_*`: -1 follows Visor Opacity.
bool sCinemaBars = true;
bool sSecondaryCrosshairs = true;
bool sDarkVisorFull = false;
int sDarkVisorStatic = PortOverlay::kDarkVisorStaticMax;
bool sInfiniteBeamAmmo = false; // Enhancements; saved, unlike the sInfiniteAmmo cheat
int sHudOpacityEcho = PortOverlay::kHudOpacityVisor;
int sHudOpacityDark = PortOverlay::kHudOpacityVisor;
int sHudOpacityScan = PortOverlay::kHudOpacityVisor;
// `aim_crosshair`: draw the reticle while the port's own aim owns the body heading (see
// PortOverlay::AimCrosshair). On by default: without it the reticle never appears under mouse,
// twin-stick or touch aim, which is the only way the phone plays.
bool sAimCrosshair = true;
// The Prime port's crosshair settings: style (classic = the game's reticle model, remastered = the
// cursor CTargetReticles draws itself), size in percent, opacity, and an optional custom colour.
// MP2's classic model does not resolve in this port, so `remastered` is the default here.
int sCrosshairStyle = PortOverlay::kCrosshairStyle_Remastered;
int sCrosshairSize = PortOverlay::kCrosshairSizeDefault;
int sCrosshairOpacity = PortOverlay::kCrosshairOpacityMax;
bool sCrosshairCustomColor = false;
float sCrosshairColor[3] = {1.f, 1.f, 1.f};
// Arm cannon offset in metres along the camera's right, forward and up axes (`MP2_GUN_OFFSET`).
float sGunOffset[3] = {0.f, 0.f, 0.f};
// Anti-aliasing and filtering (`MP2_MSAA`, `MP2_ANISO`). The defaults are Aurora's own, so an
// untouched port applies nothing.
int sMsaa = 1;
int sAnisotropy = 16;
// HUD scale in percent (`MP2_HUD_SCALE`), and whether the HUD's own draw is the one being scaled.
int sHudScale = PortOverlay::kHudScaleMax;
// Interface scale in percent (`MP2_UI_SCALE`) and vsync (`MP2_VSYNC`).
int sUiScale = 100;
bool sVsync = false;
// Fullscreen (`MP2_FULLSCREEN`): Android hides the status and navigation bars by default, a desktop
// starts windowed (MP1's sFullscreen).
#if defined(PORT_MOBILE)
bool sFullscreen = true;
#else
bool sFullscreen = false;
#endif
// MP1's 60 FPS cap (`MP2_FRAME_LIMIT`), on by default as there: PortPaceFrame holds the loop to 60 Hz.
bool sFrameLimit = true;
// MP1's smooth frames (`MP2_SMOOTH_FRAMES`, on by default): with the cap off, the camera is drawn
// between the last two ticks (CCameraManager::SetPresentationInterpolation, src/MetroidPrime/main.cpp).
bool sSmoothFrames = true;
// Frame-blend tests (`MP2_PRESENT_T`, console `present`): the forced presentation factor, -1 = off.
float sPresentOverride = -1.f;
unsigned sPresentCycleFrame = 0;
// The console's `hold`/`step`: while held the loop runs no ticks except the queued steps.
bool sTickHold = false;
unsigned sHeldTicks = 0;
// MP1's pose interpolation (`MP2_POSE_INTERPOLATION`; on by default, F1's one Smooth frames switch): with smooth frames
// on, skinned poses are blended between the last two ticks (CAnimData::PortPresentedPose).
bool sPoseInterpolation = true;
// MP1's particle interpolation (`MP2_PARTICLE_INTERPOLATION`; on by default, as Smooth frames): with smooth
// frames on, particle systems and weather draw between the last two ticks (CElementGen::Render).
bool sParticleInterpolation = true;
// MP1's look input per frame (`MP2_LOOK_INTERPOLATION`, on by default as there): with smooth
// frames on, pointer aim turns the camera between ticks (CCameraManager, PortMouse::PresentedAimDelta).
bool sLookInterpolation = true;
// MP1's HudWide (`MP2_HUD_WIDE`, on by default as there): on a wide framebuffer the GUI spreads to the
// screen edges; off keeps the game's 4:3 proportions, centred.
bool sHudWide = true;
// The widest screen the HUD spreads to (HudSpreadCap): 0 16:9, 1 21:9, 2 the whole screen. Past it the
// HUD stays centred at that width.
int sHudSpreadMax = 1;
// Scan panel spread in percent (`MP2_SCAN_SPREAD`): the touch controls cover the screen edges on
// Android, so a phone starts them at their 4:3 place.
#if defined(PORT_MOBILE)
int sScanSpread = PortOverlay::kScanSpreadMin;
#else
int sScanSpread = PortOverlay::kScanSpreadMax;
#endif
// MP1's static render scale (`MP2_RENDER_SCALE`): 0 draws the EFB at the window's own size (MP1's
// "Auto"), 1-4 at that multiple of the render mode's 480-line base. MP2 drew at the window's size
// before this setting, so that stays the default here (MP1's is 1).
float sRenderScale = 0.f;
// Aspect ratio (`MP2_ASPECT`): follows the window by default, as the Prime 1 port.
int sAspectMode = PortOverlay::kAspect_Window;
// MP1's sOverlayWindowed (desktop: the old floating window instead of the page window) and sUiTheme
// (0 auto, 1 Prime, 2 Plain; MP1's 2 is Remastered, which MP2 has no theme for).
bool sOverlayWindowed = false;
int sUiTheme = 0;
// MP1's sUiSounds: the F1 overlay plays the game's own UI SFX (debug_ui/
// ui_common.cpp, UpdateMenuSounds). Off by default, as MP1's is off for the Plain theme.
bool sUiSounds = false;
// MP1's Discord Rich Presence (off by default: it needs the desktop Discord client
// running). Applied by PortDiscord::Configure from the settings file and the F1 row.
bool sDiscord = false;
// MP1's update check (port_update_check.h): on by default, asks api.github.com once at launch.
bool sUpdateCheck = true;
} // namespace PortOverlayState

using namespace PortOverlayState;

// The runtime state the pages show (debug_ui/debug_ui_internal.h).
namespace PortDebug {

bool sVisible = false;
bool sGodMode = false; // never saved: a cheat must not persist, so it is not a setting
unsigned sInfiniteAmmo = 0; // InfiniteAmmoKind bits; never saved, as sGodMode
bool sNoDarkAetherDamage = false; // never saved, as sGodMode
uint32_t sWorldId = 0;
int sAreaId = -1;
std::vector< SRoomEntry > sRooms;
std::vector< SLayerEntry > sLayers;
int sLayersArea = -1;
std::atomic< int > sShowPage{-1};
// Frames per second, averaged over a second, from the presented-frame clock.
float sFps = 0.f;
// Frames per second of the work alone, the pacing sleep left out: what the machine could sustain.
float sThroughputFps = 0.f;

} // namespace PortDebug

using namespace PortDebug;

namespace {

bool sHudScaling = false;
bool sReadEnv = false;
uint32_t sRoomsWorld = 0; // the asset id the entries were taken from, 0 before any world
bool sRoomsSplitLogged = false; // the Light/Dark count is logged once per world

void ClearRooms() {
  for (SRoomEntry& room : sRooms) {
    delete room.nameTable;
    delete room.mapArea;
  }
  sRooms.clear();
}

// Asks for the area's map, whose header says Light or Dark Aether; read by ReadDimension once loaded.
void RequestDimension(SRoomEntry& room, const CWorld& world, int index) {
  const CMapWorld* mapWorld = world.GetMapWorld();
  if (room.mapArea != nullptr || room.dimension != SRoomEntry::EDimension::kUnknown ||
      !room.real || mapWorld == nullptr || gpSimplePool == nullptr ||
      index >= static_cast< int >(mapWorld->GetNumAreas())) {
    return;
  }
  const CAssetId mapa = mapWorld->PortMapAreaRes(index);
  if (mapa == kInvalidAssetId) {
    return;
  }
  room.mapArea = rs_new TToken< CMapArea >(gpSimplePool->GetObj(SObjectTag('MAPA', mapa)));
  room.mapArea->Lock();
}

void ReadDimension(SRoomEntry& room) {
  if (room.mapArea == nullptr || !room.mapArea->IsLoaded()) {
    return;
  }
  const CMapArea* map = **room.mapArea;
  if (map != nullptr) {
    // A light-world area the map never shows is a cutscene stage (the temples' energy controllers,
    // game_end_part*, 00_scandummy); 01_temple_hive03_dark is the one never-mapped real room.
    room.dimension = map->IsInDarkWorld()       ? SRoomEntry::EDimension::kDark
                     : map->PortIsNeverMapped() ? SRoomEntry::EDimension::kCutscene
                                                : SRoomEntry::EDimension::kLight;
  }
  delete room.mapArea;
  room.mapArea = nullptr;
}

uint64_t sFpsWindowStart = 0;
int sFpsFrames = 0;
// CPU time of the iterations in the current window, measured before PortPaceFrame sleeps.
uint64_t sFpsWorkNs = 0;

// Handed to aurora_set_graphics_quality only when they change; 0 means "never applied yet".
int sAppliedMsaa = 0;
int sAppliedAnisotropy = 0;
// Handed to aurora_enable_vsync only when it changes; -1 means "never applied yet".
int sAppliedVsync = -1;
// Handed to VISetFrameBufferScale only when it changes; -1 means "never applied yet".
float sAppliedRenderScale = -1.f;
// The render mode's fbWidth as CGraphics::ConfigureVideo set it, before ApplyAspect first widened it.
int sBaseFbWidth = 0;

// Applies <user>/settings.cfg once per run. An MP2_<KEY> environment variable is not applied here:
// the accessors below read it each time, so it wins without ever reaching the file.
void ReadEnv() {
  if (sReadEnv) {
    return;
  }
  sReadEnv = true;
  sGodMode = port::EnvFlag("MP2_GODMODE");
  sInfiniteAmmo = port::EnvFlag("MP2_INFINITE_AMMO") ? PortOverlay::kInfAmmoAll : 0;
  sNoDarkAetherDamage = port::EnvFlag("MP2_NO_DARK_AETHER_DAMAGE");
  PortOverlaySettings::LoadSettings();
  // The volume boosts too: MP2_MUSIC_BOOST and MP2_SFX_BOOST are read only here.
  ApplyVolumeBoosts();
}

// A setter's change is saved at once (a no-op when nothing differs from the file).
void Persist() {
  PortOverlaySettings::MarkDirty();
  PortOverlaySettings::SaveSettings();
}

// The effective values: the file's/UI's variable, unless the environment names it.
bool EnvBool(const char* name, bool value) { return port::EnvFlag(name, value); }

int EnvIntIn(const char* name, int value, int min, int max) {
  const int env = port::EnvInt(name, value);
  return env >= min && env <= max ? env : value;
}

float EnvFloatIn(const char* name, float value, float min, float max) {
  const float env = port::EnvFloat(name, value);
  return std::isfinite(env) && env >= min && env <= max ? env : value;
}

bool EnvFloat3(const char* name, float out[3], float min, float max) {
  const char* text = port::EnvString(name);
  float parsed[3];
  char extra = 0;
  if (text == nullptr || std::sscanf(text, "%f,%f,%f%c", &parsed[0], &parsed[1], &parsed[2], &extra) != 3) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(parsed[i])) {
      return false;
    }
  }
  for (int i = 0; i < 3; ++i) {
    out[i] = std::clamp(parsed[i], min, max);
  }
  return true;
}

void ApplyGraphicsQuality() {
  ReadEnv();
  const int msaa = PortOverlay::Msaa();
  const int anisotropy = PortOverlay::Anisotropy();
  if (sAppliedMsaa == msaa && sAppliedAnisotropy == anisotropy) {
    return;
  }
  sAppliedMsaa = msaa;
  sAppliedAnisotropy = anisotropy;
  // A request: Aurora rebuilds the framebuffers at the next frame start (a rebuild inside a frame
  // would invalidate the binds that frame already recorded).
  aurora_set_graphics_quality(static_cast< uint32_t >(msaa),
                              static_cast< uint16_t >(anisotropy));
}

void ApplyVsync() {
  ReadEnv();
  const bool vsync = PortOverlay::Vsync();
  if (sAppliedVsync == (vsync ? 1 : 0)) {
    return;
  }
  sAppliedVsync = vsync ? 1 : 0;
  aurora_enable_vsync(vsync);
}

// Photo mode's capture holds its own scale until it is done, and puts this one back itself.
void ApplyRenderScale() {
  ReadEnv();
  if (PortPhoto::Capturing()) {
    return;
  }
  // Dynamic resolution's scale while it has one (PortVideo, f1b).
  const float scale = PortVideo::DrawScale();
  if (sAppliedRenderScale == scale) {
    return;
  }
  sAppliedRenderScale = scale;
  // Each change reallocates the EFB targets at the next frame start.
  VISetFrameBufferScale(scale);
  if (scale > 0.f) {
    std::fprintf(stderr, "[video] render scale %.2fx\n", static_cast< double >(scale));
  } else {
    std::fprintf(stderr, "[video] render scale native\n");
  }
}

void TickFps() {
  const uint64_t now = SDL_GetTicksNS();
  if (sFpsWindowStart == 0) {
    sFpsWindowStart = now;
  }
  ++sFpsFrames;
  const uint64_t elapsed = now - sFpsWindowStart;
  if (elapsed >= 1000000000ull) {
    sFps = static_cast< float >(sFpsFrames) / (static_cast< float >(elapsed) / 1e9f);
    sThroughputFps = sFpsWorkNs > 0
                         ? static_cast< float >(sFpsFrames) / (static_cast< float >(sFpsWorkNs) / 1e9f)
                         : 0.f;
    sFpsWorkNs = 0;
    sFpsFrames = 0;
    sFpsWindowStart = now;
  }
}

} // namespace

namespace PortDebug {

void StoreGunOffset(const float offset[3]) {
  for (int i = 0; i < 3; ++i) {
    // A NaN slider value or a hand-written env var must not reach the gun's transform.
    sGunOffset[i] = std::isfinite(offset[i])
                        ? std::clamp(offset[i], -PortOverlay::kGunOffsetMax, PortOverlay::kGunOffsetMax)
                        : 0.f;
  }
}

// The card's world names are a STRG table that the game's own text parser draws, so an entry
// carries its markup (`&line-spacing=75;Temple Grounds`); the overlay shows the text alone.
std::string PlainWorldName(const wchar_t* text) {
  std::string out = PortDiscord::GameTextToUtf8(text);
  for (char& c : out) {
    if (c == '\t') {
      c = ' ';
    }
  }
  const size_t first = out.find_first_not_of(' ');
  const size_t last = out.find_last_not_of(' ');
  return first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
}

} // namespace PortDebug

namespace PortOverlay {

void NoteFrameWork(uint64_t ns) {
  sFpsWorkNs += ns;
}


void SetAreaLayers(const CWorldLayerState* layers, int areaId) {
  if (layers == nullptr || areaId < 0 ||
      areaId >= static_cast< int >(layers->GetAreaLayers().size())) {
    sLayers.clear();
    sLayersArea = -1;
    return;
  }
  const TAreaId aid(areaId);
  const int count = layers->GetLayerCount(aid);
  if (sLayersArea != areaId || static_cast< int >(sLayers.size()) != count) {
    sLayers.clear();
    for (int i = 0; i < count; ++i) {
      sLayers.push_back(SLayerEntry{layers->GetLayerName(aid, TLayerId(i)).c_str(), false});
    }
    sLayersArea = areaId;
  }
  for (int i = 0; i < count; ++i) {
    sLayers[i].active = layers->IsLayerActive(aid, TLayerId(i));
  }
}

void SetGameState(uint32_t worldId, int areaId, CWorld* world) {
  sWorldId = worldId;
  sAreaId = areaId;
  if (world == nullptr) {
    ClearRooms();
    sRoomsWorld = 0;
    return;
  }
  // The world object's own id, not the game state's: on the frame a world teleport is served the
  // outgoing world is still the live one while the game state already names the destination, and
  // keying on the state's id would cache the outgoing rooms under the new world's name.
  const uint32_t roomWorld = static_cast< uint32_t >(world->IGetWorldAssetId());
  if (roomWorld == sRoomsWorld && !sRooms.empty()) {
    // The same world: its area table cannot have changed under it, only name loads finish.
    int light = 0;
    int dark = 0;
    int cutscene = 0;
    int pending = 0;
    for (int i = 0; i < static_cast< int >(sRooms.size()); ++i) {
      SRoomEntry& room = sRooms[i];
      RequestDimension(room, *world, i);
      ReadDimension(room);
      light += room.dimension == SRoomEntry::EDimension::kLight ? 1 : 0;
      dark += room.dimension == SRoomEntry::EDimension::kDark ? 1 : 0;
      cutscene += room.dimension == SRoomEntry::EDimension::kCutscene ? 1 : 0;
      pending += room.mapArea != nullptr ? 1 : 0;
    }
    if (pending == 0 && !sRoomsSplitLogged) {
      sRoomsSplitLogged = true;
      std::fprintf(stderr,
                   "[rooms] %08x: %d Light Aether, %d Dark Aether, %d cutscene (hidden), %d without "
                   "a map\n",
                   roomWorld, light, dark, cutscene,
                   static_cast< int >(sRooms.size()) - light - dark - cutscene);
    }
    for (int i = 0; i < static_cast< int >(sRooms.size()); ++i) {
      SRoomEntry& room = sRooms[i];
      if (room.nameTable == nullptr || !room.nameTable->IsLoaded()) {
        continue;
      }
      const CStringTable* table = **room.nameTable;
      const std::string name = table != nullptr && table->GetStringCount() > 0
                                   ? PlainWorldName(table->GetString(0))
                                   : std::string();
      if (!name.empty()) {
        char text[160];
        std::snprintf(text, sizeof(text), "%d  %s", i, name.c_str());
        room.name = text;
        room.english = name;
      }
      delete room.nameTable;
      room.nameTable = nullptr;
    }
    return;
  }
  sRoomsWorld = roomWorld;
  sRoomsSplitLogged = false;
  ClearRooms();
  const int count = world->IGetAreaCount();
  sRooms.reserve(count);
  for (int i = 0; i < count; ++i) {
    const IGameArea* area = world->IGetAreaAlways(TAreaId(i));
    const char* name = area != nullptr ? area->PortGetMlvlAreaName().data() : nullptr;
    char label[160];
    if (name != nullptr && name[0] != '\0') {
      std::snprintf(label, sizeof(label), "%d  %s", i, name);
    } else {
      std::snprintf(label, sizeof(label), "Area %d", i);
    }
    // A slot with no MREA is not a room the game can travel to; the list hides it instead of
    // offering a warp that would print "does not exist" and do nothing.
    const bool real = area != nullptr && area->IGetAreaAssetId() != kInvalidAssetId;
    // The name tables load asynchronously and are read on later calls; a token kept locked until
    // then, since one that dies cancels its load.
    TToken< CStringTable >* nameTable = nullptr;
    const CAssetId strg = area != nullptr ? area->IGetStringTableAssetId() : kInvalidAssetId;
    if (real && strg != kInvalidAssetId && gpSimplePool != nullptr) {
      nameTable = rs_new TToken< CStringTable >(gpSimplePool->GetObj(SObjectTag('STRG', strg)));
      nameTable->Lock();
    }
    sRooms.push_back(SRoomEntry{label, label, real, nameTable, std::string(),
                                SRoomEntry::EDimension::kUnknown, nullptr});
    RequestDimension(sRooms.back(), *world, i);
  }
}

std::string RoomName(uint32_t worldId, int areaIndex) {
  std::string world;
  if (gpMemoryCard != nullptr) {
    const rstl::vector< CMemoryCard::MemoryWorld >& worlds = gpMemoryCard->GetMemoryWorlds();
    for (int i = 0; i < worlds.size(); ++i) {
      if (static_cast< uint32_t >(worlds[i].first) == worldId) {
        world = PlainWorldName(worlds[i].second.GetFrontEndName());
        break;
      }
    }
  }
  // Only the current world's rooms are named (the room picker's list); another world's keep the
  // saved label.
  std::string room;
  if (worldId == sRoomsWorld && areaIndex >= 0 && areaIndex < static_cast< int >(sRooms.size())) {
    room = sRooms[areaIndex].english;
  }
  if (world.empty() || room.empty()) {
    return room;
  }
  return world + " - " + room;
}

void Toggle() {
  sVisible = !sVisible;
  // MP1's Toggle: the cursor goes back to the window (releases the mouse capture), and opening
  // drops any button or key ImGui still holds (a press whose release never came would make it
  // ignore every later click), as a focus loss would.
  PortMouse::SetUiActive(sVisible);
  if (sVisible && ImGui::GetCurrentContext() != nullptr) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddFocusEvent(false);
    io.AddFocusEvent(true);
  }
}

bool ShowPage(const std::string& name) {
  if (name == "off") {
    if (sVisible) {
      Toggle();
    }
    return true;
  }
  const size_t slash = name.find('/');
  std::string pageName = name.substr(0, slash);
  // Cheats is a section of the Debug page now (as MP2_DEBUG_TAB=Cheats): open it there, shown.
  // Only when no page has the prefix, so `overlay c` still opens Controls.
  const auto pageMatches = [&](const std::string& prefix) {
    for (int i = 0; i < kDebugPageCount; ++i) {
      if (SDL_strncasecmp(kDebugPages[i].name, prefix.c_str(), prefix.size()) == 0) {
        return true;
      }
    }
    return false;
  };
  if (!pageName.empty() && SDL_strncasecmp("cheats", pageName.c_str(), pageName.size()) == 0 &&
      !pageMatches(pageName)) {
    pageName = "Debug";
    if (!sShowCheats) {
      sShowCheats = true;
      PortOverlaySettings::MarkDirty();
    }
  }
  for (int i = 0; i < kDebugPageCount; ++i) {
    const char* page = kDebugPages[i].name;
    if (!pageName.empty() && SDL_strncasecmp(page, pageName.c_str(), pageName.size()) == 0) {
      SetShowSub(slash == std::string::npos ? std::string() : name.substr(slash + 1));
      sShowPage.store(i);
      if (!sVisible) {
        Toggle();
      }
      return true;
    }
  }
  return false;
}

// Called on the UI thread (the two-finger scroll), but ImGui's input queue is only safe on the game
// thread, so the steps wait here until Draw() hands them over.
static std::atomic< int > sPendingWheelMilli{0};
static std::atomic< bool > sToggleRequested{false};

void RequestToggle() { sToggleRequested.store(true, std::memory_order_release); }

void Wheel(float steps) {
  sPendingWheelMilli.fetch_add(static_cast< int >(std::lround(steps * 1000.f)),
                               std::memory_order_relaxed);
}

bool Visible() {
  ReadEnv();
  return sVisible;
}

bool PointerBlocked() {
  return Visible() || PortMouse::UiActive() ||
         (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse);
}

bool SkipCinematics() {
  ReadEnv();
  if (OriginalExperience()) {
    return false;
  }
  return EnvBool("MP2_SKIP_CINEMATICS", sSkipCinematics);
}

namespace {
uint64_t sSkipConfirmUntilNs = 0;
std::atomic<bool> sSkipConfirmTapped{false};
} // namespace

bool SkipConfirmArmed() { return sSkipConfirmUntilNs != 0 && SDL_GetTicksNS() < sSkipConfirmUntilNs; }

void SkipConfirmArm() {
  sSkipConfirmTapped.store(false);
  sSkipConfirmUntilNs = SDL_GetTicksNS() + static_cast< uint64_t >(kSkipConfirmSeconds * 1e9f);
}

void SkipConfirmDisarm() {
  sSkipConfirmUntilNs = 0;
  sSkipConfirmTapped.store(false);
}

float SkipConfirmAlpha() {
  const uint64_t now = SDL_GetTicksNS();
  if (sSkipConfirmUntilNs == 0 || now >= sSkipConfirmUntilNs) {
    return 0.f;
  }
  const float left = static_cast< float >(sSkipConfirmUntilNs - now) * 1e-9f;
  return left >= 0.5f ? 1.f : left * 2.f;
}

void SkipConfirmTap() {
  if (SkipConfirmArmed()) {
    sSkipConfirmTapped.store(true);
  }
}

bool TakeSkipConfirmTap() { return sSkipConfirmTapped.exchange(false); }

void SetSkipCinematics(bool skip) {
  ReadEnv();
  sSkipCinematics = skip;
  Persist();
}

// A kScreen_* setting: `show|skip|off` (or 0..2) in the variable wins over the saved value.
static int EnvScreenMode(const char* name, int value) {
  const char* env = port::EnvString(name);
  if (env != nullptr && std::strcmp(env, "show") == 0) {
    return kScreen_Show;
  }
  if (env != nullptr && std::strcmp(env, "skip") == 0) {
    return kScreen_Skippable;
  }
  if (env != nullptr && std::strcmp(env, "off") == 0) {
    return kScreen_Off;
  }
  return EnvIntIn(name, value, kScreen_Show, kScreen_Off);
}

// The boot logos and the world name card end on A or Start; Original experience shows them as
// retail. No setting: only the env overrides (the harness) pick another mode.
int BootLogos() {
  ReadEnv();
  return EnvScreenMode("MP2_BOOT_LOGOS", OriginalExperience() ? kScreen_Show : kScreen_Skippable);
}

int WorldNameCard() {
  ReadEnv();
  return EnvScreenMode("MP2_WORLD_NAME", OriginalExperience() ? kScreen_Show : kScreen_Skippable);
}

bool HideHelmet() {
  ReadEnv();
  return EnvBool("MP2_NO_HELMET", sHideHelmet);
}

bool HideVisorEffects() {
  ReadEnv();
  return EnvBool("MP2_NO_VISOR_FX", sHideVisorEffects);
}

bool CinemaBars() {
  ReadEnv();
  if (OriginalExperience()) {
    return true;
  }
  return EnvBool("MP2_CINEMA_BARS", sCinemaBars);
}

bool SecondaryCrosshairs() {
  ReadEnv();
  if (OriginalExperience()) {
    return true;
  }
  return EnvBool("MP2_SECONDARY_CROSSHAIRS", sSecondaryCrosshairs);
}

bool DarkVisorFullScreen() {
  ReadEnv();
  if (OriginalExperience()) {
    return false;
  }
  return EnvBool("MP2_DARK_VISOR_FULL", sDarkVisorFull);
}

int DarkVisorStatic() {
  ReadEnv();
  if (OriginalExperience()) {
    return kDarkVisorStaticMax;
  }
  return std::clamp(port::EnvInt("MP2_DARK_VISOR_STATIC", sDarkVisorStatic), 0, kDarkVisorStaticMax);
}

int HudOpacity(HudOpacityPart part) {
  ReadEnv();
  if (OriginalExperience()) {
    return kHudOpacityVisor;
  }
  const int value = part == kHudOpacityEcho   ? sHudOpacityEcho
                    : part == kHudOpacityDark ? sHudOpacityDark
                                              : sHudOpacityScan;
  return value < kHudOpacityMin ? kHudOpacityVisor : std::min(value, kHudOpacityMax);
}

void SetHudOpacity(HudOpacityPart part, int percent) {
  ReadEnv();
  const int value =
      percent < kHudOpacityMin ? kHudOpacityVisor : std::min(percent, kHudOpacityMax);
  (part == kHudOpacityEcho ? sHudOpacityEcho : part == kHudOpacityDark ? sHudOpacityDark
                                                                       : sHudOpacityScan) = value;
  Persist();
}

bool AimCrosshair() {
  ReadEnv();
  return EnvBool("MP2_AIM_CROSSHAIR", sAimCrosshair);
}

void SetAimCrosshair(bool enabled) {
  ReadEnv();
  sAimCrosshair = enabled;
  Persist();
}

int CrosshairStyle() {
  ReadEnv();
  const char* env = port::EnvString("MP2_CROSSHAIR_STYLE");
  if (env != nullptr && std::strcmp(env, "classic") == 0) {
    return kCrosshairStyle_Classic;
  }
  if (env != nullptr && std::strcmp(env, "remastered") == 0) {
    return kCrosshairStyle_Remastered;
  }
  if (env != nullptr && std::strcmp(env, "gamecube") == 0) {
    return kCrosshairStyle_GameCube;
  }
  return EnvIntIn("MP2_CROSSHAIR_STYLE", sCrosshairStyle, kCrosshairStyle_Classic, kCrosshairStyle_GameCube);
}

void SetCrosshairStyle(int style) {
  ReadEnv();
  sCrosshairStyle = std::clamp(style, kCrosshairStyle_Classic, kCrosshairStyle_GameCube);
  Persist();
}

int CrosshairSize() {
  ReadEnv();
  return EnvIntIn("MP2_CROSSHAIR_SIZE", sCrosshairSize, kCrosshairSizeMin, kCrosshairSizeMax);
}

void SetCrosshairSize(int percent) {
  ReadEnv();
  sCrosshairSize = std::clamp(percent, kCrosshairSizeMin, kCrosshairSizeMax);
  Persist();
}

int CrosshairOpacity() {
  ReadEnv();
  return EnvIntIn("MP2_CROSSHAIR_OPACITY", sCrosshairOpacity, kCrosshairOpacityMin, kCrosshairOpacityMax);
}

void SetCrosshairOpacity(int percent) {
  ReadEnv();
  sCrosshairOpacity = std::clamp(percent, kCrosshairOpacityMin, kCrosshairOpacityMax);
  Persist();
}

bool CrosshairColor(float rgb[3]) {
  ReadEnv();
  if (!EnvBool("MP2_CROSSHAIR_CUSTOM_COLOR", sCrosshairCustomColor)) {
    return false;
  }
  if (!EnvFloat3("MP2_CROSSHAIR_COLOR", rgb, 0.f, 1.f)) {
    std::copy_n(sCrosshairColor, 3, rgb);
  }
  return true;
}

void SetCrosshairColor(float r, float g, float b) {
  ReadEnv();
  sCrosshairColor[0] = std::clamp(r, 0.f, 1.f);
  sCrosshairColor[1] = std::clamp(g, 0.f, 1.f);
  sCrosshairColor[2] = std::clamp(b, 0.f, 1.f);
  sCrosshairCustomColor = true;
  Persist();
}

void ClearCrosshairColor() {
  ReadEnv();
  sCrosshairCustomColor = false;
  Persist();
}

float FirstPersonFov() {
  ReadEnv();
  const float env = port::EnvFloat("MP2_FOV", sFirstPersonFov);
  return env == 0.f || (env >= 30.f && env <= 120.f) ? env : sFirstPersonFov;
}

void GunOffset(float out[3]) {
  ReadEnv();
  if (EnvFloat3("MP2_GUN_OFFSET", out, -kGunOffsetMax, kGunOffsetMax)) {
    return;
  }
  out[0] = sGunOffset[0];
  out[1] = sGunOffset[1];
  out[2] = sGunOffset[2];
}

void SetGunOffset(const float offset[3]) {
  ReadEnv();
  StoreGunOffset(offset);
  Persist();
}

int Msaa() {
  ReadEnv();
  const int env = port::EnvInt("MP2_MSAA", sMsaa);
  return env == 1 || env == 4 ? env : sMsaa;
}

void SetMsaa(int samples) {
  ReadEnv();
  sMsaa = samples >= 4 ? 4 : 1;
  Persist();
}

int Anisotropy() {
  ReadEnv();
  return EnvIntIn("MP2_ANISO", sAnisotropy, 1, 16);
}

void SetAnisotropy(int maxAnisotropy) {
  ReadEnv();
  sAnisotropy = std::clamp(maxAnisotropy, 1, 16);
  Persist();
}

int HudScale() {
  ReadEnv();
  // 0 means the full-size HUD.
  const int env = port::EnvInt("MP2_HUD_SCALE", sHudScale);
  return env == 0 ? kHudScaleMax : (env >= kHudScaleMin && env <= kHudScaleMax ? env : sHudScale);
}

void SetHudScale(int percent) {
  ReadEnv();
  sHudScale = std::clamp(percent, kHudScaleMin, kHudScaleMax);
  Persist();
}

bool HudScalingActive() { return sHudScaling; }

void SetHudScalingActive(bool active) { sHudScaling = active; }

bool GodMode() {
  ReadEnv();
  return sGodMode;
}

void SetGodMode(bool enabled) {
  ReadEnv();
  sGodMode = enabled;
}

bool NoDarkAetherDamage() {
  ReadEnv();
  return sNoDarkAetherDamage;
}

void SetNoDarkAetherDamage(bool enabled) {
  ReadEnv();
  sNoDarkAetherDamage = enabled;
}

unsigned InfiniteAmmoKind(int itemType) {
  switch (itemType) {
  case CPlayerState::kIT_Missile:
    return kInfAmmoMissile;
  case CPlayerState::kIT_Powerbomb:
    return kInfAmmoPowerBomb;
  case CPlayerState::kIT_DarkAmmo:
    return kInfAmmoDark;
  case CPlayerState::kIT_LightAmmo:
    return kInfAmmoLight;
  default:
    return 0;
  }
}

bool InfiniteBeamAmmo() {
  ReadEnv();
  if (OriginalExperience()) {
    return false;
  }
  return EnvBool("MP2_INFINITE_BEAM_AMMO", sInfiniteBeamAmmo);
}

bool InfiniteAmmo(unsigned kinds) {
  ReadEnv();
  const unsigned on = sInfiniteAmmo | (InfiniteBeamAmmo() ? kInfAmmoDark | kInfAmmoLight : 0u);
  return kinds != 0 && (on & kinds) == kinds;
}

void SetInfiniteAmmo(unsigned kinds, bool enabled) {
  ReadEnv();
  sInfiniteAmmo = enabled ? sInfiniteAmmo | kinds : sInfiniteAmmo & ~kinds;
}

bool FastMorph() {
  ReadEnv();
  if (OriginalExperience()) {
    return false;
  }
  return EnvBool("MP2_FAST_MORPH", sFastMorph);
}

void SetFastMorph(bool enabled) {
  ReadEnv();
  sFastMorph = enabled;
  Persist();
}

bool MinimapTap() {
  ReadEnv();
  return EnvBool("MP2_MINIMAP_TAP", sMinimapTap);
}

void SetMinimapTap(bool enabled) {
  ReadEnv();
  sMinimapTap = enabled;
  Persist();
}

bool PointerMenus() {
  ReadEnv();
  return EnvBool("MP2_POINTER_MENUS", sPointerMenus);
}

void SetPointerMenus(bool enabled) {
  ReadEnv();
  sPointerMenus = enabled;
  Persist();
}

bool MapSwapFingers() {
  ReadEnv();
  return EnvBool("MP2_MAP_SWAP_FINGERS", sMapSwapFingers);
}

void SetMapSwapFingers(bool swap) {
  ReadEnv();
  sMapSwapFingers = swap;
  Persist();
}

bool MapInvertX() {
  ReadEnv();
  return EnvBool("MP2_MAP_INVERT_X", sMapInvertX);
}

void SetMapInvertX(bool invert) {
  ReadEnv();
  sMapInvertX = invert;
  Persist();
}

bool MapInvertY() {
  ReadEnv();
  return EnvBool("MP2_MAP_INVERT_Y", sMapInvertY);
}

void SetMapInvertY(bool invert) {
  ReadEnv();
  sMapInvertY = invert;
  Persist();
}

int WalkSpeed() {
  ReadEnv();
  if (OriginalExperience()) {
    return kWalkSpeedDefault;
  }
  return EnvIntIn("MP2_WALK_SPEED", sWalkSpeed, kWalkSpeedMin, kWalkSpeedMax);
}

float HintDelaySeconds() {
  ReadEnv();
  if (OriginalExperience()) {
    return 0.f;
  }
  return static_cast< float >(EnvIntIn("MP2_HINT_DELAY", sHintDelay, kHintDelayMin, kHintDelayMax)) *
         60.f;
}

static bool sShowHintRequested = false;

void RequestShowHint() {
  sShowHintRequested = true;
}

bool ConsumeShowHintRequest() {
  const bool requested = sShowHintRequested;
  sShowHintRequested = false;
  return requested;
}

void SetWalkSpeed(int percent) {
  ReadEnv();
  sWalkSpeed = std::clamp(percent, kWalkSpeedMin, kWalkSpeedMax);
  Persist();
}

bool SpringBall() {
  ReadEnv();
  if (OriginalExperience()) {
    return false;
  }
  return EnvBool("MP2_SPRING_BALL", sSpringBall);
}

bool QuickResume() {
  ReadEnv();
  return EnvBool("MP2_QUICK_RESUME", sQuickResume);
}

// No settings: the bindings always work (unbind them in Controls > Remap) and states keep the
// room; the env overrides stay for the harness.
bool SaveStateHotkeys() {
  ReadEnv();
  return EnvBool("MP2_SAVESTATE_HOTKEYS", true);
}

bool SaveStateRoomState() {
  ReadEnv();
  return EnvBool("MP2_SAVESTATE_ROOM_STATE", true);
}

void SetQuickResume(bool enabled) {
  ReadEnv();
  sQuickResume = enabled;
  Persist();
}

void SetSpringBall(bool enabled) {
  ReadEnv();
  sSpringBall = enabled;
  Persist();
}

bool SpringBallFlick() {
  ReadEnv();
  return EnvBool("MP2_SPRING_BALL_FLICK", sSpringFlick);
}

void SetSpringBallFlick(bool enabled) {
  ReadEnv();
  sSpringFlick = enabled;
  Persist();
}

float SpringBallFlickRate() {
  ReadEnv();
  return EnvFloatIn("MP2_SPRING_BALL_FLICK_RATE", sSpringFlickRate, kSpringFlickRateMin,
                    kSpringFlickRateMax);
}

bool LockOnToggle() {
  ReadEnv();
  if (OriginalExperience()) {
    return false;
  }
  return EnvBool("MP2_LOCK_ON_TOGGLE", EnvBool("MP2_LOCKON_TOGGLE", sLockOnToggle));
}

void SetLockOnToggle(bool enabled) {
  ReadEnv();
  sLockOnToggle = enabled;
  Persist();
}

bool StickyCharge() {
  ReadEnv();
  if (OriginalExperience()) {
    return false;
  }
  return EnvBool("MP2_STICKY_CHARGE", sStickyCharge);
}

void SetStickyCharge(bool enabled) {
  ReadEnv();
  sStickyCharge = enabled;
  Persist();
}

bool AutoAim() {
  ReadEnv();
  if (OriginalExperience()) {
    return true;
  }
  return EnvBool("MP2_AUTO_AIM", sAutoAim);
}

void SetAutoAim(bool enabled) {
  ReadEnv();
  sAutoAim = enabled;
  Persist();
}

bool PointerAim() {
  ReadEnv();
  return EnvBool("MP2_POINTER_AIM", sPointerAim);
}

bool TrilogyRadialSelectors() {
  ReadEnv();
  return EnvBool("MP2_TRILOGY_RADIAL_SELECTORS", sTrilogyRadialSelectors);
}

void SetTrilogyRadialSelectors(bool enabled) {
  sTrilogyRadialSelectors = enabled;
  if (!enabled) {
    PortRadial::Transport().SetAvailable(false);
    PortRadial::Cancel();
  }
}

void SetPointerAim(bool enabled) {
  ReadEnv();
  sPointerAim = enabled;
  Persist();
}

int PointerAimBox() {
  ReadEnv();
  return EnvIntIn("MP2_POINTER_AIM_BOX", sPointerAimBox, kPointerAimBoxMin, kPointerAimBoxMax);
}

int PointerAimTurn() {
  ReadEnv();
  return EnvIntIn("MP2_POINTER_AIM_TURN", sPointerAimTurn, kPointerAimTurnMin, kPointerAimTurnMax);
}

bool GyroAim() {
  ReadEnv();
  return EnvBool("MP2_GYRO_AIM", sGyroAim);
}

void SetGyroAim(bool enabled) {
  ReadEnv();
  sGyroAim = enabled;
  Persist();
}

int GyroSource() {
  ReadEnv();
  return EnvIntIn("MP2_GYRO_SOURCE", sGyroSource, 0, 2);
}

int GyroSpace() {
  ReadEnv();
  return EnvIntIn("MP2_GYRO_SPACE", sGyroSpace, 0, PortGyro::kSpaceCount - 1);
}

int GyroAutoCalibrate() {
  ReadEnv();
  return EnvIntIn("MP2_GYRO_AUTO_CALIBRATE", sGyroAutoCalibrate, 0, 2);
}

float GyroSmoothing() {
  ReadEnv();
  return EnvFloatIn("MP2_GYRO_SMOOTHING", sGyroSmoothing, 0.f, kGyroThresholdMax);
}

float GyroTightening() {
  ReadEnv();
  return EnvFloatIn("MP2_GYRO_TIGHTENING", sGyroTightening, 0.f, kGyroThresholdMax);
}

float GyroCursorAngle() {
  ReadEnv();
  return EnvFloatIn("MP2_GYRO_CURSOR_ANGLE", sGyroCursorAngle, kGyroCursorAngleMin, kGyroCursorAngleMax);
}

int GyroMode() {
  ReadEnv();
  return EnvIntIn("MP2_GYRO_MODE", sGyroMode, kGyroModeMin, kGyroModeMax);
}

float GyroSensitivity() {
  ReadEnv();
  return EnvFloatIn("MP2_GYRO_SENSITIVITY", sGyroSensitivity, kGyroSensitivityMin, kGyroSensitivityMax);
}

bool GyroCursorCamera() {
  ReadEnv();
  return EnvBool("MP2_GYRO_CURSOR_CAMERA", sGyroCursorCamera);
}

bool GyroSplit() {
  ReadEnv();
  return EnvBool("MP2_GYRO_SPLIT", sGyroSplit);
}

float GyroSensitivityY() {
  ReadEnv();
  return EnvFloatIn("MP2_GYRO_SENSITIVITY_Y", sGyroSensitivityY, kGyroSensitivityMin, kGyroSensitivityMax);
}

bool LockOnFreeAim() {
  ReadEnv();
  return EnvBool("MP2_LOCK_ON_FREE_AIM", sLockOnFreeAim);
}

bool ResetAimBoth() {
  ReadEnv();
  return EnvBool("MP2_RESET_AIM_BOTH", sResetAimBoth);
}

bool MenuStandardButtons() {
  ReadEnv();
  return EnvBool("MP2_MENU_STANDARD_BUTTONS", sMenuStandardButtons);
}

void SetMenuStandardButtons(bool enabled) {
  ReadEnv();
  sMenuStandardButtons = enabled;
  Persist();
}

bool RapidCharge() {
  ReadEnv();
  if (OriginalExperience()) {
    return false;
  }
  return EnvBool("MP2_RAPID_CHARGE", sRapidCharge);
}

void SetRapidCharge(bool enabled) {
  ReadEnv();
  sRapidCharge = enabled;
  Persist();
}

bool CardBackup() {
  ReadEnv();
  return EnvBool("MP2_CARD_BACKUP", sCardBackup);
}

void SetCardBackup(bool enabled) {
  ReadEnv();
  sCardBackup = enabled;
  Persist();
}

bool LogFile() {
  ReadEnv();
  return EnvBool("MP2_LOG_FILE", sLogFile);
}

void SetLogFile(bool enabled) {
  ReadEnv();
  sLogFile = enabled;
  Persist();
}

int UiScale() {
  ReadEnv();
  return EnvIntIn("MP2_UI_SCALE", sUiScale, kUiScaleMin, kUiScaleMax);
}

void SetUiScale(int percent) {
  ReadEnv();
  sUiScale = std::clamp(percent, kUiScaleMin, kUiScaleMax);
  Persist();
}

bool Vsync() {
  ReadEnv();
  return EnvBool("MP2_VSYNC", sVsync);
}

void SetVsync(bool enabled) {
  ReadEnv();
  sVsync = enabled;
  Persist();
}

bool Fullscreen() {
  ReadEnv();
  return EnvBool("MP2_FULLSCREEN", sFullscreen);
}

void SetFullscreen(bool enabled) {
  ReadEnv();
  sFullscreen = enabled;
  Persist();
  VISetWindowFullscreen(enabled);
}

bool FrameLimit() {
  ReadEnv();
  return EnvBool("MP2_FRAME_LIMIT", sFrameLimit);
}

void SetFrameLimit(bool enabled) {
  ReadEnv();
  if (sFrameLimit != enabled) {
    sFrameLimit = enabled;
    Persist();
  }
}

bool SmoothFrames() {
  ReadEnv();
  return EnvBool("MP2_SMOOTH_FRAMES", sSmoothFrames);
}

void SetSmoothFrames(bool enabled) {
  ReadEnv();
  if (sSmoothFrames != enabled) {
    sSmoothFrames = enabled;
    Persist();
  }
}

// MP2_PRESENT_T is read on the first call, like ReadEnv reads the other overrides.
void ReadPresentEnv() {
  static const bool read = [] {
    const char* present = port::EnvString("MP2_PRESENT_T");
    if (present == nullptr || present[0] == '\0') {
      return true;
    }
    if (std::strcmp(present, "cycle") == 0) {
      sPresentOverride = kPresentCycle;
    } else if (std::strcmp(present, "tick") == 0) {
      sPresentOverride = kPresentTick;
    } else {
      const float t = static_cast< float >(std::atof(present));
      sPresentOverride = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    }
    return true;
  }();
  (void)read;
}

bool PresentOverride(float& t) {
  ReadPresentEnv();
  if (sPresentOverride < 0.f) {
    return false;
  }
  if (sPresentOverride == kPresentTick) {
    t = -1.f;
  } else if (sPresentOverride > 1.f) {
    t = 0.25f * static_cast< float >(sPresentCycleFrame++ & 3);
  } else {
    t = sPresentOverride;
  }
  return true;
}

void SetPresentOverride(float value) {
  sPresentOverride = value < 0.f             ? -1.f
                     : value == kPresentTick ? kPresentTick
                     : value > 1.f           ? kPresentCycle
                                             : value;
  sPresentCycleFrame = 0;
}

float PresentOverrideValue() {
  ReadPresentEnv();
  return sPresentOverride;
}

bool TickHold() { return sTickHold; }

void SetTickHold(bool held) {
  sTickHold = held;
  sHeldTicks = 0;
}

void StepTicks(unsigned count) { sHeldTicks += count; }

unsigned PendingHeldTicks() { return sHeldTicks; }

unsigned TakeHeldTicks() {
  // One tick per loop, as MP1 does, so each step is one drawn frame.
  if (sHeldTicks == 0) {
    return 0;
  }
  --sHeldTicks;
  return 1;
}

bool PoseInterpolation() {
  ReadEnv();
  return EnvBool("MP2_POSE_INTERPOLATION", sPoseInterpolation);
}

void SetPoseInterpolation(bool enabled) {
  ReadEnv();
  if (sPoseInterpolation != enabled) {
    sPoseInterpolation = enabled;
    Persist();
  }
}

bool ParticleInterpolation() {
  ReadEnv();
  return EnvBool("MP2_PARTICLE_INTERPOLATION", sParticleInterpolation);
}

void SetParticleInterpolation(bool enabled) {
  ReadEnv();
  if (sParticleInterpolation != enabled) {
    sParticleInterpolation = enabled;
    Persist();
  }
}

bool HudWide() {
  ReadEnv();
  return EnvBool("MP2_HUD_WIDE", sHudWide);
}

void SetHudWide(bool enabled) {
  ReadEnv();
  if (sHudWide != enabled) {
    sHudWide = enabled;
    Persist();
  }
}

int HudSpreadMax() {
  ReadEnv();
  const char* env = std::getenv("MP2_HUD_SPREAD_MAX");
  int mode = sHudSpreadMax;
  if (env != nullptr) {
    ParseHudSpreadMax(env, mode);
  }
  return std::clamp(mode, 0, 2);
}

void SetHudSpreadMax(int mode) {
  ReadEnv();
  mode = std::clamp(mode, 0, 2);
  if (sHudSpreadMax != mode) {
    sHudSpreadMax = mode;
    Persist();
  }
}

int ScanSpread() {
  ReadEnv();
  const int env = port::EnvInt("MP2_SCAN_SPREAD", -1);
  return std::clamp(env >= kScanSpreadMin && env <= kScanSpreadMax ? env : sScanSpread,
                    kScanSpreadMin, kScanSpreadMax);
}

void SetScanSpread(int percent) {
  ReadEnv();
  percent = std::clamp(percent, kScanSpreadMin, kScanSpreadMax);
  if (sScanSpread != percent) {
    sScanSpread = percent;
    Persist();
  }
}

const char* HudSpreadMaxName(int mode) {
  return mode <= 0 ? "16:9" : mode == 1 ? "21:9" : "full";
}

bool ParseHudSpreadMax(const char* name, int& mode) {
  const std::string text = name != nullptr ? name : "";
  if (text == "16:9" || text == "16x9") {
    mode = 0;
  } else if (text == "21:9" || text == "21x9") {
    mode = 1;
  } else if (text == "full" || text == "0") {
    mode = 2;
  } else {
    return false;
  }
  return true;
}

float HudSpreadCap() {
  const int mode = HudSpreadMax();
  // Widths relative to 4:3.
  return mode <= 0 ? (16.f / 9.f) / (4.f / 3.f) : mode == 1 ? (21.f / 9.f) / (4.f / 3.f) : 1e6f;
}

bool LookInterpolation() {
  ReadEnv();
  return EnvBool("MP2_LOOK_INTERPOLATION", sLookInterpolation);
}

void SetLookInterpolation(bool enabled) {
  ReadEnv();
  if (sLookInterpolation != enabled) {
    sLookInterpolation = enabled;
    Persist();
  }
}

float RenderScale() {
  ReadEnv();
  const float env = EnvFloatIn("MP2_RENDER_SCALE", sRenderScale, 0.f, kRenderScaleMax);
  return env > 0.f && env < kRenderScaleMin ? kRenderScaleMin : env;
}

void SetRenderScale(float scale) {
  ReadEnv();
  if (std::isfinite(scale)) {
    sRenderScale = scale <= 0.f ? 0.f : std::clamp(scale, kRenderScaleMin, kRenderScaleMax);
    Persist();
  }
}

const char* AspectName(int mode) {
  return mode == kAspect_4_3 ? "4:3" : mode == kAspect_16_9 ? "16:9" : "window";
}

bool ParseAspect(const char* name, int& mode) {
  if (name == nullptr) {
    return false;
  }
  if (std::strcmp(name, "4:3") == 0 || std::strcmp(name, "4x3") == 0) {
    mode = kAspect_4_3;
  } else if (std::strcmp(name, "16:9") == 0 || std::strcmp(name, "16x9") == 0) {
    mode = kAspect_16_9;
  } else if (std::strcmp(name, "window") == 0) {
    mode = kAspect_Window;
  } else {
    return false;
  }
  return true;
}

int AspectMode() {
  ReadEnv();
  int mode = sAspectMode;
  if (ParseAspect(port::EnvString("MP2_ASPECT"), mode)) {
    return mode;
  }
  if (port::EnvFlag("MP2_WIDESCREEN")) {
    return kAspect_16_9;
  }
  return mode;
}

void SetAspectMode(int mode) {
  ReadEnv();
  sAspectMode = std::clamp(mode, kAspect_4_3, kAspect_Window);
  Persist();
}

// The Prime 1 port's ApplyAspectMode (src/MetroidPrime/main.cpp there), with one difference: 4:3
// is the game's own width (640 at 448 lines, the 1.42 CCameraManager was built for), not
// efbHeight * 4 / 3.
void ApplyAspect() {
  const GXRenderModeObj& renderMode = CGraphics::GetRenderMode();
  const int efbHeight = renderMode.efbHeight;
  if (efbHeight <= 0) {
    return; // CGraphics::ConfigureVideo has not run yet
  }
  if (sBaseFbWidth == 0) {
    sBaseFbWidth = renderMode.fbWidth;
  }
  int fbWidth = sBaseFbWidth;
  switch (AspectMode()) {
  case kAspect_16_9:
    fbWidth = (efbHeight * 16 + 8) / 9;
    break;
  case kAspect_Window: {
    // Ask the window rather than track resize events, as the Prime 1 port does: the loading loops
    // that run before the main loop drain the startup resize.
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_Window* window = PortDebug::MainWindow();
    if (window == nullptr || !SDL_GetWindowSize(window, &windowWidth, &windowHeight) ||
        windowWidth <= 0 || windowHeight <= 0) {
      windowWidth = 854;
      windowHeight = 480;
    }
    fbWidth = static_cast< int >(static_cast< double >(efbHeight) *
                                     static_cast< double >(windowWidth) /
                                     static_cast< double >(windowHeight) +
                                 0.5);
    break;
  }
  default:
    break;
  }
  fbWidth &= ~1;
  // The GX scissor is a 12-bit field biased by 342, so a framebuffer wider than 3754 wraps it to zero
  // width and clips every draw (an all-black picture).
  const int maxWidth = 3754;
  if (fbWidth > maxWidth) {
    fbWidth = maxWidth;
  }
  if (fbWidth <= 0 || fbWidth == renderMode.fbWidth) {
    return;
  }
  CGraphics::PortResizeFrameBuffer(static_cast< u16 >(fbWidth));
}

float AspectWiden() {
  const int fbWidth = CGraphics::GetRenderMode().fbWidth;
  if (sBaseFbWidth <= 0 || fbWidth <= 0) {
    return 1.f;
  }
  return static_cast< float >(fbWidth) / static_cast< float >(sBaseFbWidth);
}

float VertPlusFov(float fovDegrees, float aspect) {
  const int efbHeight = CGraphics::GetRenderMode().efbHeight;
  if (sBaseFbWidth <= 0 || efbHeight <= 0 || aspect <= 0.f) {
    return fovDegrees;
  }
  const float authored = static_cast< float >(sBaseFbWidth) / static_cast< float >(efbHeight);
  if (aspect >= authored) {
    return fovDegrees;
  }
  const float halfTan = tanf(0.5f * fovDegrees * 3.14159265f / 180.f) * authored / aspect;
  return 2.f * atanf(halfTan) * 180.f / 3.14159265f;
}

int FourThreeWidth(int width, int height) {
  if (height <= 0) {
    return width;
  }
  return AspectMode() == kAspect_4_3 ? width : height * 4 / 3;
}

float UnwidenFov(float fovDegrees, float aspect) {
  const int efbHeight = CGraphics::GetRenderMode().efbHeight;
  if (sBaseFbWidth <= 0 || efbHeight <= 0 || aspect <= 0.f) {
    return fovDegrees;
  }
  const float authored = static_cast< float >(sBaseFbWidth) / static_cast< float >(efbHeight);
  if (aspect >= authored) {
    return fovDegrees;
  }
  const float halfTan = tanf(0.5f * fovDegrees * 3.14159265f / 180.f) * aspect / authored;
  return 2.f * atanf(halfTan) * 180.f / 3.14159265f;
}

float VertPlusGunDrop(float fovDegrees, float aspect) {
  const int efbHeight = CGraphics::GetRenderMode().efbHeight;
  if (sBaseFbWidth <= 0 || efbHeight <= 0 || aspect <= 0.f) {
    return 0.f;
  }
  const float authored = static_cast< float >(sBaseFbWidth) / static_cast< float >(efbHeight);
  if (aspect >= authored) {
    return 0.f;
  }
  return tanf(0.5f * fovDegrees * 3.14159265f / 180.f) * (authored / aspect - 1.f);
}

// The text language follows the setting (file, env, F1 page, console) while the game runs:
// CStringTable keeps every section and picks one per GetString.
void ApplyLanguage() {
  static int applied = -1;
  const int language = Language();
  if (language != applied) {
    applied = language;
    CStringTable::SetLanguage(language);
  }
}

void Draw() {
  TickFps();
  ApplyLanguage();
  if (const int milli = sPendingWheelMilli.exchange(0, std::memory_order_relaxed); milli != 0) {
    ImGui::GetIO().AddMouseWheelEvent(0.f, static_cast< float >(milli) / 1000.f);
  }
  if (sToggleRequested.exchange(false, std::memory_order_acq_rel)) {
    Toggle();
  }
  PortPhoto::Frame();
  // Even with the window closed: the env vars are settings, and a headless run never opens it.
  ApplyGraphicsQuality();
  ApplyVsync();
  PortVideo::Frame();
  ApplyRenderScale();
  SyncGameOptions();
  PortDebug::DrawUI();
}

} // namespace PortOverlay

// F1 > Video rows from the Prime 1 port (f1b): their settings (port_overlay_internal.h) and
// accessors. platform/port_video.cpp applies them.
namespace PortOverlayState {
bool sDynamicRes = false;
int sDynamicResTarget = 0;
float sDynamicResMin = 1.f;
int sSharpenMode = 1;
int sSharpenAmount = 50;
int sFpsCap = 0;
bool sShowShaderCompilation = true;
bool sShowMenuHint = true;
bool sShowCheats = false;
bool sPerPixelLighting = false;
PortGraphicsBackend::EGraphicsBackend sGraphicsBackend = PortGraphicsBackend::kGraphicsBackend_Auto;
PortGraphicsBackend::EGraphicsBackend sGraphicsBackendAtStart = PortGraphicsBackend::kGraphicsBackend_Auto;
bool sOpenGles = false;
bool sOpenGlesAtStart = false;
int sStorageClamp = -1;
int sStorageClampAtStart = -1;
std::string sGpuDriver;
std::string sGpuDriverAtStart;
unsigned sDriverWorkarounds = 0;
unsigned sDriverWorkaroundsAtStart = 0;
bool sLowPowerGpu = false;
bool sLowPowerGpuAtStart = false;
} // namespace PortOverlayState

namespace PortOverlayState {

void ApplyVolumeBoosts() {
  const float music = static_cast< float >(EnvIntIn("MP2_MUSIC_BOOST", sMusicBoost,
                                                   PortOverlay::kVolumeBoostMin,
                                                   PortOverlay::kVolumeBoostMax)) /
                      100.f;
  const float sfx = static_cast< float >(EnvIntIn("MP2_SFX_BOOST", sSfxBoost,
                                                 PortOverlay::kVolumeBoostMin,
                                                 PortOverlay::kVolumeBoostMax)) /
                    100.f;
  // The MusyX voices scale one by one in the mix; the AI DMA stream (front-end music, movie audio)
  // takes the sound effects boost as its stream's gain.
  MusyxPortSetVolumeBoost(music, sfx);
  AIPortSetOutputGain(sfx);
}

} // namespace PortOverlayState

namespace PortOverlay {

bool DynamicRes() {
  ReadEnv();
  return EnvBool("MP2_DYNAMIC_RES", sDynamicRes);
}

int DynamicResTarget() {
  ReadEnv();
  return EnvIntIn("MP2_DYNAMIC_RES_TARGET", sDynamicResTarget, 0, kFpsCapMax);
}

float DynamicResMin() {
  ReadEnv();
  return EnvFloatIn("MP2_DYNAMIC_RES_MIN", sDynamicResMin, kDynamicResMinLowest, 4.f);
}

int UpscaleSharpen() {
  ReadEnv();
  if (const char* env = port::EnvString("MP2_UPSCALE_SHARPEN"); env != nullptr && env[0] != '\0') {
    const std::string value = env;
    return value == "off" ? 0 : value == "always" ? 2 : 1;
  }
  return sSharpenMode;
}

int UpscaleSharpenAmount() {
  ReadEnv();
  return EnvIntIn("MP2_UPSCALE_SHARPEN_AMOUNT", sSharpenAmount, 0, 100);
}

int ClampFpsCap(int fps) { return fps <= 0 ? 0 : std::clamp(fps, kFpsCapMin, kFpsCapMax); }

int FpsCap() {
  ReadEnv();
  return ClampFpsCap(port::EnvInt("MP2_FPS_CAP", sFpsCap));
}

bool ShowShaderCompilation() {
  ReadEnv();
  return EnvBool("MP2_SHOW_SHADER_COMPILATION", sShowShaderCompilation);
}

bool ShowMenuHint() {
  ReadEnv();
  return EnvBool("MP2_SHOW_MENU_HINT", sShowMenuHint);
}

bool PerPixelLighting() {
  ReadEnv();
  return EnvBool("MP2_PER_PIXEL_LIGHTING", sPerPixelLighting);
}

bool OpenGles() {
  ReadEnv();
  return EnvBool("MP2_OPENGLES", sOpenGles);
}

PortGraphicsBackend::EGraphicsBackend GraphicsBackend() {
  ReadEnv();
  return sGraphicsBackend;
}

int StorageClamp() {
  ReadEnv();
  return sStorageClamp;
}

unsigned DriverWorkarounds() {
  ReadEnv();
  return sDriverWorkarounds;
}

bool LowPowerGpu() {
  ReadEnv();
  return sLowPowerGpu;
}

int ScanSpeed() {
  ReadEnv();
  if (OriginalExperience()) {
    return kScanSpeedDefault;
  }
  return EnvIntIn("MP2_SCAN_SPEED", sScanSpeed, kScanSpeedMin, kScanSpeedMax);
}

void SetScanSpeed(int percent) {
  ReadEnv();
  sScanSpeed = std::clamp(percent, kScanSpeedMin, kScanSpeedMax);
  Persist();
}

int MusicBoost() {
  ReadEnv();
  return EnvIntIn("MP2_MUSIC_BOOST", sMusicBoost, kVolumeBoostMin, kVolumeBoostMax);
}

void SetMusicBoost(int percent) {
  ReadEnv();
  sMusicBoost = std::clamp(percent, kVolumeBoostMin, kVolumeBoostMax);
  ApplyVolumeBoosts();
  Persist();
}

int SfxBoost() {
  ReadEnv();
  return EnvIntIn("MP2_SFX_BOOST", sSfxBoost, kVolumeBoostMin, kVolumeBoostMax);
}

void SetSfxBoost(int percent) {
  ReadEnv();
  sSfxBoost = std::clamp(percent, kVolumeBoostMin, kVolumeBoostMax);
  ApplyVolumeBoosts();
  Persist();
}

int Language() {
  ReadEnv();
  return EnvIntIn("MP2_LANGUAGE", sLanguage, 0, kLanguageMax);
}

void SetLanguage(int index) {
  ReadEnv();
  sLanguage = std::clamp(index, 0, kLanguageMax);
  Persist();
}

const char* PromptGlyphs() {
  ReadEnv();
  if (OriginalExperience()) {
    return "gamecube";
  }
  static constexpr const char* kNames[] = {nullptr, "xbox", "playstation", "switch", "gamecube", "keyboard"};
  return kNames[EnvIntIn("MP2_PROMPT_GLYPHS", sPromptGlyphs, kPromptGlyphs_Auto, kPromptGlyphs_Keyboard)];
}

int ElevatorRide() {
  ReadEnv();
  if (OriginalExperience()) {
    return kElevatorRide_Original;
  }
  const char* env = port::EnvString("MP2_ELEVATOR_RIDE");
  if (env != nullptr && std::strcmp(env, "original") == 0) {
    return kElevatorRide_Original;
  }
  if (env != nullptr && std::strcmp(env, "fast") == 0) {
    return kElevatorRide_Fast;
  }
  if (env != nullptr && std::strcmp(env, "skip") == 0) {
    return kElevatorRide_Skip;
  }
  return EnvIntIn("MP2_ELEVATOR_RIDE", sElevatorRide, kElevatorRide_Original, kElevatorRide_Skip);
}

void SetElevatorRide(int mode) {
  ReadEnv();
  sElevatorRide = std::clamp(mode, static_cast< int >(kElevatorRide_Original),
                             static_cast< int >(kElevatorRide_Skip));
  Persist();
}

int ComboBeamCost() {
  ReadEnv();
  if (OriginalExperience()) {
    return kComboBeamCostMax;
  }
  return EnvIntIn("MP2_COMBO_BEAM_COST", sComboBeamCost, 0, kComboBeamCostMax);
}

void SetComboBeamCost(int cost) {
  ReadEnv();
  sComboBeamCost = std::clamp(cost, 0, kComboBeamCostMax);
  Persist();
}

int ComboMissileCost() {
  ReadEnv();
  if (OriginalExperience()) {
    return kComboMissileCostMax;
  }
  return EnvIntIn("MP2_COMBO_MISSILE_COST", sComboMissileCost, 0, kComboMissileCostMax);
}

void SetComboMissileCost(int cost) {
  ReadEnv();
  sComboMissileCost = std::clamp(cost, 0, kComboMissileCostMax);
  Persist();
}

int BeamAmmoPerExpansion() {
  ReadEnv();
  if (OriginalExperience()) {
    return kBeamAmmoPerExpansionDefault;
  }
  return EnvIntIn("MP2_BEAM_AMMO_PER_EXPANSION", sBeamAmmoPerExpansion, kBeamAmmoPerExpansionMin,
                  kBeamAmmoPerExpansionMax);
}

void SetBeamAmmoPerExpansion(int ammo) {
  ReadEnv();
  sBeamAmmoPerExpansion = std::clamp(ammo, kBeamAmmoPerExpansionMin, kBeamAmmoPerExpansionMax);
  Persist();
}

int RotatingRooms() {
  ReadEnv();
  if (OriginalExperience()) {
    return kRotatingRooms_Original;
  }
  const char* env = port::EnvString("MP2_ROTATING_ROOMS");
  if (env != nullptr && std::strcmp(env, "original") == 0) {
    return kRotatingRooms_Original;
  }
  if (env != nullptr && std::strcmp(env, "fast") == 0) {
    return kRotatingRooms_Fast;
  }
  return EnvIntIn("MP2_ROTATING_ROOMS", sRotatingRooms, kRotatingRooms_Original, kRotatingRooms_Fast);
}

void SetRotatingRooms(int mode) {
  ReadEnv();
  sRotatingRooms = std::clamp(mode, static_cast< int >(kRotatingRooms_Original),
                              static_cast< int >(kRotatingRooms_Fast));
  Persist();
}

static unsigned sRotatingSequenceFrame = 0;
static bool sRotatingSequenceSeen = false;

void NoteRotatingSequence(unsigned frame) {
  sRotatingSequenceFrame = frame;
  sRotatingSequenceSeen = true;
}

bool RotatingSequenceActive(unsigned frame) {
  return sRotatingSequenceSeen && frame - sRotatingSequenceFrame <= 2;
}

int PortalTransition() {
  ReadEnv();
  if (OriginalExperience()) {
    return kPortalTransition_Original;
  }
  const char* env = port::EnvString("MP2_PORTAL_TRANSITION");
  if (env != nullptr && std::strcmp(env, "original") == 0) {
    return kPortalTransition_Original;
  }
  if (env != nullptr && std::strcmp(env, "fast") == 0) {
    return kPortalTransition_Fast;
  }
  if (env != nullptr && std::strcmp(env, "skip") == 0) {
    return kPortalTransition_Skip;
  }
  return EnvIntIn("MP2_PORTAL_TRANSITION", sPortalTransition, kPortalTransition_Original,
                  kPortalTransition_Skip);
}

void SetPortalTransition(int mode) {
  ReadEnv();
  sPortalTransition = std::clamp(mode, static_cast< int >(kPortalTransition_Original),
                                 static_cast< int >(kPortalTransition_Skip));
  Persist();
}

bool IgtTimer() {
  ReadEnv();
  return EnvBool("MP2_IGT_TIMER", sIgtTimer);
}

void SetIgtTimer(bool enabled) {
  ReadEnv();
  sIgtTimer = enabled;
  Persist();
}

static double sGameTime = 0.0;
static bool sGameTimeValid = false;

void NoteGameTime(double seconds) {
  sGameTime = seconds;
  sGameTimeValid = true;
}

void ClearGameTime() { sGameTimeValid = false; }

bool GameTime(double& seconds) {
  seconds = sGameTime;
  return sGameTimeValid;
}

static bool sReturnToTitleRequested = false;

void RequestReturnToTitle() { sReturnToTitleRequested = true; }

bool ConsumeReturnToTitleRequest() {
  const bool requested = sReturnToTitleRequested;
  sReturnToTitleRequested = false;
  return requested;
}

bool OriginalExperience() {
  ReadEnv();
  return EnvBool("MP2_ORIGINAL_EXPERIENCE", sOriginalExperience);
}

void SetOriginalExperience(bool enabled) {
  ReadEnv();
  sOriginalExperience = enabled;
  Persist();
}

bool UnlockHardMode() {
  ReadEnv();
  return EnvBool("MP2_UNLOCK_HARD_MODE", sUnlockHardMode) && !OriginalExperience();
}

void SetUnlockHardMode(bool enabled) {
  ReadEnv();
  sUnlockHardMode = enabled;
  Persist();
}

bool UnlockGalleries() {
  ReadEnv();
  return EnvBool("MP2_UNLOCK_GALLERIES", sUnlockGalleries) && !OriginalExperience();
}

void SetUnlockGalleries(bool enabled) {
  ReadEnv();
  sUnlockGalleries = enabled;
  Persist();
}

bool UnlockBeamDoors() {
  ReadEnv();
  return EnvBool("MP2_UNLOCK_BEAM_DOORS", sUnlockBeamDoors) && !OriginalExperience();
}

void SetUnlockBeamDoors(bool enabled) {
  ReadEnv();
  sUnlockBeamDoors = enabled;
  Persist();
}

bool StackBeams() {
  ReadEnv();
  return EnvBool("MP2_STACK_BEAMS", sStackBeams) && !OriginalExperience();
}

void SetStackBeams(bool enabled) {
  ReadEnv();
  sStackBeams = enabled;
  Persist();
}

bool StackBeamsSkipDark() {
  ReadEnv();
  return EnvBool("MP2_STACK_BEAMS_SKIP_DARK", sStackBeamsSkipDark);
}

void SetStackBeamsSkipDark(bool enabled) {
  ReadEnv();
  sStackBeamsSkipDark = enabled;
  Persist();
}

} // namespace PortOverlay
