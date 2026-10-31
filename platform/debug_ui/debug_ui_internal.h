// Internal to the F1 overlay: platform/port_overlay.cpp and the parts in platform/debug_ui/, the
// Prime 1 port's debug_ui/ layout. What more than one of those files uses is declared here;
// anything only one file uses stays static in it.
//
// The settings live in port_overlay.cpp (namespace PortOverlayState, pulled into the global
// namespace below so the parts read them unqualified), the settings file in settings.cpp, and each
// page or section in its own file inside namespace PortDebug.
#pragma once

#include "port_mobile.h"
#include "../port_overlay_internal.h"

#include <imgui.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class CMapArea;
class CStringTable;
template < typename T >
class TToken;
struct ImGuiWindow;
struct SDL_Window;

using namespace PortOverlayState;

namespace PortDebug {

// port_overlay.cpp: the runtime state the pages show.
extern bool sVisible;
extern bool sGodMode; // never saved: a cheat must not persist, so it is not a setting
extern bool sNoDarkAetherDamage; // never saved, as sGodMode
extern unsigned sInfiniteAmmo; // PortOverlay::kInfAmmo bits; never saved, as sGodMode
extern uint32_t sWorldId;
extern int sAreaId;
extern float sFps;
extern float sThroughputFps;
// The room list, taken from the live world at SetGameState time (on the game thread, while the
// world is alive) and never a pointer kept across frames: a world teleport frees the outgoing
// world, and the overlay still draws the frames before the restart replaces it.
// `name` starts as the MLVL's internal label and becomes the area's English name (string 0 of its
// name STRG, what the map screen shows) once that table has loaded; `label` keeps the internal one
// so the filter still finds a room by it.
struct SRoomEntry {
  std::string name;
  std::string label;
  bool real;
  TToken< CStringTable >* nameTable; // pending name load, freed once read
  std::string english;               // the English name alone, empty until it has loaded
  // Light or Dark Aether, from the area's map (CMapArea bit 0, what the map screen splits by);
  // unknown until that MAPA has loaded, and for an area the map does not cover.
  enum class EDimension { kUnknown, kLight, kDark, kCutscene }; // kCutscene: not in the lists
  EDimension dimension;
  TToken< CMapArea >* mapArea; // pending map-area load, freed once read
};
extern std::vector< SRoomEntry > sRooms;
// The current area's layers, copied by SetAreaLayers on the game thread like the room list.
struct SLayerEntry {
  std::string name;
  bool active;
};
extern std::vector< SLayerEntry > sLayers;
extern int sLayersArea;
// A page the console's `overlay <page>` asked for, taken by the next frame's window (-1: none).
extern std::atomic< int > sShowPage;
// The sub-tab (a name prefix, case-insensitive) that `overlay <page>/<sub-tab>` asked for; empty
// clears it. Set before sShowPage, taken by the first SubTab it matches.
void SetShowSub(std::string sub);
// The card's world names without their text markup.
std::string PlainWorldName(const wchar_t* text);
// Clamps and stores the arm cannon offset (NaN becomes 0).
void StoreGunOffset(const float offset[3]);

// A row's change is saved by DrawUI once no control is active, not on every frame of a drag.
inline bool Changed(bool changed) {
  if (changed) {
    PortOverlaySettings::MarkDirty();
  }
  return changed;
}

// Pages and sections, each drawn by the file it is named after.
struct DebugPage {
  const char* name;
  void (*draw)();
};
// search.cpp: the page list, sub-tabs and the settings search.
extern const DebugPage kDebugPages[];
extern const int kDebugPageCount;
// MP2_DEBUG_TAB=<page>[/<sub-tab>].
struct StartTarget {
  std::string page;
  std::string sub;
};
const StartTarget& GetStartTarget();
// One row of port_settings_index.inc.
struct SettingsIndexRow {
  const char* page;
  const char* sub;
  const char* section;
  const char* label;
  const char* keywords;
};
extern char sSearch[128];
// The page a search result or the console asked for, once (-1 when none).
int TakeJumpPage();
void DrawPageBody(const DebugPage& page);
bool SubTab(const char* page, const char* name);
void BeginSearchJump();
void EndSearchJump();
void DrawSearchField();
bool Searching();
void DrawSearchResults();
ImGuiWindow* ScrollableWindowAt(ImGuiWindow* window);

// ui_common.cpp
bool PageLayout();
SDL_Window* MainWindow();
extern float sUiAppliedScale;
// The window's scale is the display's own (capped at 2 on the phone) times `userScale` (the
// `ui_scale` setting, 1 = 100%), clamped to 1..4; rebuilt from the unscaled style when it changes.
void UpdateUiScale(float userScale);
void ApplyThemeIfChanged();
// Gamepad navigation from pad 0; the only writer of ImGui's gamepad keys. Every frame.
void UpdateControllerNav();
#if defined(PORT_TOUCH_OVERLAY)
// touch_overlay.cpp: the SDL event filter that feeds it, and its draw
void InstallTouchOverlay();
void DrawTouchOverlay();
#endif
ImVec4 ThemeWarnColor(const ImVec4& plain = ImVec4(1.f, 0.8f, 0.3f, 1.f));
ImVec4 ThemeGoodColor(const ImVec4& plain = ImVec4(0.4f, 1.f, 0.4f, 1.f));
ImVec4 ThemeBadColor(const ImVec4& plain = ImVec4(1.f, 0.5f, 0.3f, 1.f));
// What a setting does. A tooltip on the desktop; a tap shows none, so Android
// prints it, dimmed, under the setting instead.
#if defined(PORT_MOBILE)
inline constexpr bool kInlineHelp = true;
#else
inline constexpr bool kInlineHelp = false;
#endif
void ItemHelp(const char* text);
void SameLineAfterHelp();
// The menu sounds (MP1's): derived from ImGui's state, called at the end of DrawUI.
void UpdateMenuSounds(bool visible);

// window.cpp
bool TouchScrolling(); // a finger drag is scrolling a page, taken from the widget it began on
void DrawUI();
// The port's own hotkeys, named by what is bound.
std::string HotkeyHint();

// game_sections.cpp
void DrawGameSection();
void DrawCutscenesSection();
void DrawGameAudio();
void DrawSpeedrunSection();
void DrawTexturesSection(); // the Mods page's texture pack section
std::string TexturePackStatus();
#if defined(__ANDROID__)
void PickTexturePack();
#endif
void SetTexturePackStatus(std::string status);
void DrawOverlaySection();
void DrawSettingsSection();
void DrawMemoryCard();
// card_data.cpp: the card import/export rows and the Data folder panel.
void DrawCardTransfer();
void DrawDataFolder();
void DrawDiscordSection();
void DrawUpdateSection();
// Applies sUpdateCheck (and MP2_UPDATE_CHECK) to PortUpdateCheck; settings.cpp calls it at load.
void ApplyUpdateCheck();
void DrawDiscImageSection();
void DrawLogSection();

// video.cpp
void DrawVideoDisplay();
void DrawVideoQuality();
void DrawVideoCompatibility();
// Drawn every frame, the window open or not (DrawUI).
void DrawShaderCompilationToast();
void DrawUpdateToast(); // once per launch: a newer release than this build is out
// photo.cpp: photo mode's panel, bar and toast, and the touch fingers that fly its camera.
void DrawPhotoUI();
void DrawMenuHintToast();
// Top-centre toasts stack instead of overlapping: BeginTopToastStack once per frame before them,
// then PlaceTopToast before each Begin and TopToastPlaced inside it (MP1's API).
void BeginTopToastStack();
void PlaceTopToast(const ImGuiViewport* viewport);
void TopToastPlaced(const ImGuiViewport* viewport);

// controls.cpp
void DrawControlsOptions();
void DrawControlsKeyboardMouse();
void DrawControlsController();
void DrawControlsTouchGyro();

// mods.cpp
void DrawMods();

// The remaining pages and sections.
void DrawFreeCam();
// MP1's cheat rows without the page's own wrapper, for a caller that draws them under a heading.
void DrawCheatsSection();
void DrawRendering();
void DrawAudio();
void DrawVoices();
void DrawDebugTab();
void DrawPerformanceTab();
void DrawSaveStatesTab();
void DrawRandomizerTab();
void DrawGameTab();
void DrawModsTab();
void DrawControlsTab();
void DrawVideoTab();
void DrawEnhancementsTab();
void DrawSystemTab();

} // namespace PortDebug
