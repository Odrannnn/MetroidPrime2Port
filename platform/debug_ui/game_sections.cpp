// F1 overlay: The Game and System pages' sections (game, cutscenes, audio, speedrun, overlay,
// settings, memory card, log). State and helpers shared with the other parts are in
// debug_ui_internal.h.

#include "port_utf8_path.h"
#include "port_mobile.h"
#include "debug_ui_internal.h"

#include "port_config_reset.h"
#include "port_disc_replace.h"
#include "port_debug.h"
#include "port_disc_verify.h"
#include "port_discord.h"
#include "port_freecam.h"
#include "port_photo.h"
#include "port_input_remap_ui.h"
#include "port_livesplit.h"
#include "port_mods.h"
#include "port_logfile.h"
#include "port_overlay.h"
#include "port_savestate.h"
#include "port_settings.h"
#include "port_textures.h"
#include "port_zip.h"
#include "port_update_check.h"
#include "port_build_info.h"
#include "port_env.h"

#include "MetroidPrime/Player/CGameOptions.hpp"
#include "MetroidPrime/Player/CGameState.hpp"

#include <aurora/aurora.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_misc.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <string>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#if defined(__ANDROID__)
#include <jni.h>
#include <SDL3/SDL_system.h>
#endif

namespace aurora {
void request_screenshot() noexcept;
}

namespace PortDebug {

// Screenshot, photo mode and exit; the top of the Game page (settings are on System).
void DrawGameSection() {
  double igt = 0.0;
  ImGui::SeparatorText("Game");
  const std::string shotKeys = PortInputRemap::BindingsText(PortInput::Action::Screenshot, " / ");
  const std::string shotLabel =
      (shotKeys.empty() ? std::string("Screenshot") : "Screenshot (" + shotKeys + ")") + "###screenshot";
  if (ImGui::Button(shotLabel.c_str())) {
    aurora::request_screenshot();
  }
  ImGui::SameLine();
  const std::string photoKeys = PortInputRemap::BindingsText(PortInput::Action::PhotoMode, " / ");
  const std::string photoLabel =
      (photoKeys.empty() ? std::string("Photo mode") : "Photo mode (" + photoKeys + ")") + "###photomode";
  if (ImGui::Button(photoLabel.c_str())) {
    // The overlay would cover the picture, so it closes; the game tick resolves the request.
    PortPhoto::RequestEnter();
    sVisible = false;
  }
  ImGui::SameLine();
  // Only while a game runs: the game tick quits to the front end as the pause menu's Quit does.
  ImGui::BeginDisabled(!PortOverlay::GameTime(igt));
  if (ImGui::Button("Return to title")) {
    PortOverlay::RequestReturnToTitle();
    sVisible = false;
  }
  ImGui::EndDisabled();
  ItemHelp("Leaves the game for the title screen, as the pause menu's Quit does. Progress since the "
           "last save station is lost.");
  ImGui::SameLine();
  if (ImGui::Button("Exit game")) {
    ImGui::OpenPopup("Exit game?");
  }
  if (ImGui::BeginPopupModal("Exit game?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Progress since the last save station is lost.");
    if (ImGui::Button("Exit")) {
      // The same path as closing the window: the main loop sees the quit and shuts down cleanly.
      PortOverlaySettings::SaveSettings();
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

// MP1's Game > Unlocks section: what finishing the game unlocks, applied without writing the save.
// Only the unlocks Echoes has: its Fusion Suit and the Remastered galleries are Prime 1's.
static void DrawUnlocksSection() {
  ImGui::SeparatorText("Unlocks");
  const bool locked = PortOverlay::OriginalExperience();
  ImGui::BeginDisabled(locked);
  bool hardMode = sUnlockHardMode;
  if (ImGui::Checkbox("Hard mode", &hardMode)) {
    PortOverlay::SetUnlockHardMode(hardMode);
  }
  ImGui::SetItemTooltip("Offered when starting a file, as after finishing the game.");
  ImGui::SameLine();
  bool galleries = sUnlockGalleries;
  if (ImGui::Checkbox("Image galleries", &galleries)) {
    PortOverlay::SetUnlockGalleries(galleries);
  }
  ImGui::SetItemTooltip("All of them.");
  bool stackBeams = sStackBeams;
  if (ImGui::Checkbox("Stack beams", &stackBeams)) {
    PortOverlay::SetStackBeams(stackBeams);
  }
  ImGui::SetItemTooltip("Each shot also fires the other beams you own, at no extra ammo cost.");
  ImGui::BeginDisabled(!sStackBeams);
  bool skipDark = sStackBeamsSkipDark;
  if (ImGui::Checkbox("Leave out Dark Beam", &skipDark)) {
    PortOverlay::SetStackBeamsSkipDark(skipDark);
  }
  ImGui::SetItemTooltip("Stacked shots skip the Dark Beam unless it's the one in hand, so they "
                        "don't drain light beacons and crystals.");
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  ImGui::TextDisabled("What finishing the game unlocks. Not written into the save.");
  ItemHelp("Turning an option off locks it again.");
}

void DrawCutscenesSection() {
  ImGui::SeparatorText("Cutscenes");
  Changed(ImGui::Checkbox("Original experience", &sOriginalExperience));
  ItemHelp("The game as it shipped: skippable cutscenes, world name, walk speed, hint delay, scan "
           "speed, elevator ride and the Controls page's charge, lock-on, morph ball and auto-aim "
           "tweaks all act as retail while this is on. Your values are kept and return when it is "
           "off.");
  const bool locked = PortOverlay::OriginalExperience();
  ImGui::BeginDisabled(locked);
  // The game reads this every frame in its cinematic flow state.
  Changed(ImGui::Checkbox("Skippable cutscenes", &sSkipCinematics));
  ItemHelp("START or A skips any cutscene and the new game's intro text, seen or not. Off: START skips "
           "only the ones already seen.");
  ImGui::EndDisabled();
  // One row for the boot logos (read by CSplashScreen at launch) and the world name card (read by
  // CMFGameLoader at the next load); the keys stay separate. Original experience shows the card.
  int splash = sBootLogos;
  if (ImGui::Combo("Skip splash screens", &splash, "No\0" "With A or Start\0" "Always\0")) {
    sBootLogos = std::clamp(splash, PortOverlay::kScreen_Show, PortOverlay::kScreen_Off);
    sWorldNameCard = sBootLogos;
    Changed(true);
  }
  ItemHelp("The health warning and the Nintendo, Retro and Dolby logos at launch, and the world's name "
           "typed out while a save loads. The logos change from the next launch. The new game's intro "
           "text follows Skippable cutscenes.");
  ImGui::BeginDisabled(locked);
  int walkSpeed = sWalkSpeed;
  if (ImGui::SliderInt("Walk speed", &walkSpeed, PortOverlay::kWalkSpeedMin,
                       PortOverlay::kWalkSpeedMax, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
    sWalkSpeed = walkSpeed;
    Changed(true);
  }
  ItemHelp("How fast Samus moves on foot, walking, strafing and circle-strafing, in percent of "
           "the original speed. Jumps, dashes, turning and the Morph Ball are unchanged.");
  int scanSpeed = sScanSpeed;
  if (ImGui::SliderInt("Scan speed", &scanSpeed, PortOverlay::kScanSpeedMin,
                       PortOverlay::kScanSpeedMax, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
    sScanSpeed = scanSpeed;
    Changed(true);
  }
  ItemHelp("How fast a scan fills, in percent of the original speed. 200 takes half the time.");
  // Elevator and portal share a row: their choices are short.
  const float rideWidth = ImGui::GetFontSize() * 6.f;
  int elevatorRide = sElevatorRide;
  ImGui::SetNextItemWidth(rideWidth);
  if (ImGui::Combo("Elevator ride", &elevatorRide, "Original\0" "Fast\0" "Skip\0")) {
    sElevatorRide = std::clamp(elevatorRide, static_cast< int >(PortOverlay::kElevatorRide_Original),
                               static_cast< int >(PortOverlay::kElevatorRide_Skip));
    Changed(true);
  }
  ItemHelp("The ride between worlds. Fast plays it at double speed; Skip leaves as soon as the next "
           "world has loaded. Applies from the next ride.");
  int rotatingRooms = sRotatingRooms;
  ImGui::SetNextItemWidth(rideWidth);
  if (ImGui::Combo("Rotating rooms", &rotatingRooms, "Original\0" "Fast\0")) {
    sRotatingRooms = std::clamp(rotatingRooms, static_cast< int >(PortOverlay::kRotatingRooms_Original),
                                static_cast< int >(PortOverlay::kRotatingRooms_Fast));
    Changed(true);
  }
  ItemHelp("The energy controller rooms' bomb-slot cutscene, where the room turns. Fast plays it at "
           "double speed.");
  SameLineAfterHelp();
  int portalTransition = sPortalTransition;
  ImGui::SetNextItemWidth(rideWidth);
  if (ImGui::Combo("Portal transition", &portalTransition, "Original\0" "Fast\0" "Skip\0")) {
    sPortalTransition = std::clamp(portalTransition,
                                   static_cast< int >(PortOverlay::kPortalTransition_Original),
                                   static_cast< int >(PortOverlay::kPortalTransition_Skip));
    Changed(true);
  }
  ItemHelp("The ride through a light/dark world portal. Fast plays it at double speed; Skip leaves as "
           "soon as the other world has loaded. Applies from the next portal.");
  int language = sLanguage;
  if (ImGui::Combo("Language", &language,
                   "English\0" "German\0" "French\0" "Spanish\0" "Italian\0" "Dutch\0"
                   "Japanese\0")) {
    sLanguage = std::clamp(language, 0, static_cast< int >(PortOverlay::kLanguageMax));
    Changed(true);
  }
  ItemHelp("The language of the game's text, changed while the game runs (a screen that is already open "
           "keeps its text until it is reopened). A PAL disc carries English, French, German, Spanish, "
           "Italian and Japanese; a language the disc lacks shows English.");
  int hintDelay = sHintDelay;
  if (ImGui::SliderInt("Hint delay", &hintDelay, PortOverlay::kHintDelayMin,
                       PortOverlay::kHintDelayMax, hintDelay == 0 ? "Game default" : "%d min",
                       ImGuiSliderFlags_AlwaysClamp)) {
    sHintDelay = hintDelay;
    Changed(true);
  }
  ItemHelp("The longest a hint waits before it first shows. Game default keeps the game's own "
           "timing; a shorter wait also applies to hints already waiting in your save. Needs the "
           "in-game hint system on.");
  ImGui::EndDisabled();
  ImGui::BeginDisabled(gpGameState == nullptr ||
                       !gpGameState->GameOptions().GetIsHintSystemEnabled());
  if (ImGui::Button("Show current hint")) {
    PortOverlay::RequestShowHint();
  }
  ImGui::EndDisabled();
  ItemHelp("Shows the hint the game would show next, now: the one on screen again, or the one "
           "due soonest. Needs the in-game hint system on.");
  DrawUnlocksSection();
}

// The game's own volumes, the same as the pause menu's two sliders. Echoes clamps both to 0x69
// (`CGameOptions::SetSfxVolume`/`SetMusicVolume`), not Prime 1's 0x7F, and 0x69 is also the
// default, so 100% here is the retail maximum. Scaling by 127 capped the slider at 83% (#14).
// Above 100% the game stays at 0x69 and the port's mixer boosts that path instead (#41), so the
// row goes on to kVolumeBoostMax.
constexpr int kGameVolumeMax = 0x69;

// A row's value for one of the game's volumes: the game's own volume in percent, or the port's
// boost once the game's volume is at its maximum.
int VolumeSliderPercent(int gameVolume, int boost) {
  if (gameVolume >= kGameVolumeMax) {
    return boost;
  }
  return (gameVolume * 100 + kGameVolumeMax / 2) / kGameVolumeMax;
}

// One row: up to 100 the game's own volume, above it the game at its maximum and the port's boost
// in its place; back at 100 or under the boost returns to 1.0.
void GameVolumeRow(const char* label, int gameVolume, int boost, bool music) {
  int percent = VolumeSliderPercent(gameVolume, boost);
  if (!ImGui::SliderInt(label, &percent, 0, PortOverlay::kVolumeBoostMax, "%d%%",
                        ImGuiSliderFlags_AlwaysClamp) ||
      gpGameState == nullptr) {
    return;
  }
  const bool boosted = percent > PortOverlay::kVolumeBoostDefault;
  const int volume = boosted ? kGameVolumeMax : (percent * kGameVolumeMax + 50) / 100;
  if (music) {
    gpGameState->GameOptions().SetMusicVolume(volume, true);
    sMusicBoost = boosted ? percent : PortOverlay::kVolumeBoostDefault;
  } else {
    gpGameState->GameOptions().SetSfxVolume(volume, true);
    sSfxBoost = boosted ? percent : PortOverlay::kVolumeBoostDefault;
  }
  // The mixer takes it now; DrawUI writes the file once the drag is over, as the other rows do.
  ApplyVolumeBoosts();
  Changed(true);
}

void DrawGameAudio() {
  ImGui::SeparatorText("Audio");
  ImGui::BeginDisabled(gpGameState == nullptr);
  GameVolumeRow("Music volume",
                gpGameState != nullptr
                    ? static_cast< int >(gpGameState->GameOptions().GetMusicVolume())
                    : 0,
                PortOverlay::MusicBoost(), true);
  ImGui::SetItemTooltip("Up to 100 percent the game's own Music volume; above it the game stays at "
                        "its maximum and the port boosts the music. That is louder than retail, and "
                        "very high values can clip.");
  GameVolumeRow("Sound effects volume",
                gpGameState != nullptr
                    ? static_cast< int >(gpGameState->GameOptions().GetSfxVolume())
                    : 0,
                PortOverlay::SfxBoost(), false);
  ImGui::SetItemTooltip("Up to 100 percent the game's own Sound effects volume; above it the game "
                        "stays at its maximum and the port boosts every voice, and the movie audio. "
                        "That is louder than retail, and very high values can clip.");
  // Sound mode: Mono / Stereo / Dolby Surround
  int soundMode = gpGameState != nullptr ? static_cast< int >(gpGameState->GameOptions().GetSurroundMode())
                                         : CAudioSys::kSM_Stereo;
  constexpr const char* kSoundModeLabels[] = {"Mono", "Stereo", "Dolby Surround"};
  if (ImGui::Combo("Sound mode", &soundMode, kSoundModeLabels, IM_ARRAYSIZE(kSoundModeLabels)) &&
      gpGameState != nullptr) {
    gpGameState->GameOptions().SetSurroundMode(static_cast< CAudioSys::ESurroundModes >(soundMode), true);
  }
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("The game's own options: the next save writes them, as the pause menu's do.");
}

void DrawSpeedrunSection() {
  ImGui::SeparatorText("Speedrun");
  Changed(ImGui::Checkbox("On-screen in-game time", &sIgtTimer));
  ItemHelp("The save file's play time in the bottom-right corner while a game runs. It stops in "
           "cutscenes, menus and loads.");
  bool liveSplit = PortLiveSplit::Enabled();
  if (ImGui::Checkbox("LiveSplit", &liveSplit)) {
    PortLiveSplit::SetEnabled(liveSplit);
    Changed(true);
  }
  ItemHelp("In LiveSplit: right-click, Control > Start TCP Server (port 16834), and compare "
           "against Game Time. A new file starts the timer; it splits on the final blow.");
  SameLineAfterHelp();
  switch (PortLiveSplit::Status()) {
  case PortLiveSplit::kStatus_Off:
    ImGui::TextDisabled("off");
    break;
  case PortLiveSplit::kStatus_Connecting:
    ImGui::TextUnformatted("connecting...");
    break;
  case PortLiveSplit::kStatus_Connected:
    ImGui::TextColored(ThemeGoodColor(), "connected");
    break;
  case PortLiveSplit::kStatus_Failed:
    ImGui::TextColored(ThemeBadColor(), "%s", PortLiveSplit::LastError().c_str());
    break;
  }
  // Applied when the field loses focus; until then the text is left alone.
  static char address[128];
  static bool editingAddress = false;
  if (!editingAddress) {
    std::snprintf(address, sizeof(address), "%s", PortLiveSplit::Address().c_str());
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
  ImGui::InputText("Server (host:port)", address, sizeof(address));
  editingAddress = ImGui::IsItemActive();
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    PortLiveSplit::SetAddress(address);
    Changed(true);
  }
  ImGui::SameLine();
  bool splitUpgrades = PortLiveSplit::SplitUpgrades();
  if (ImGui::Checkbox("Split on upgrades", &splitUpgrades)) {
    PortLiveSplit::SetSplitUpgrades(splitUpgrades);
    Changed(true);
  }
  ImGui::SetItemTooltip("Also splits on each new suit, beam, visor, ball part, key or other upgrade.");
}

// The texture pack import's progress: set from the Java copy thread on Android, from the zip
// extraction thread on the desktop.
namespace {
std::mutex sTexturePackStatusMutex;
std::string sTexturePackStatus;
} // namespace

std::string TexturePackStatus() {
  std::lock_guard lock(sTexturePackStatusMutex);
  return sTexturePackStatus;
}

void SetTexturePackStatus(std::string status) {
  std::lock_guard lock(sTexturePackStatusMutex);
  sTexturePackStatus = std::move(status);
}

#if defined(__ANDROID__)

// MetroidPrime2Activity.pickTexturePack opens the system folder picker and copies the chosen folder
// in the background, reporting back through nativeTexturePackStatus / nativeTexturePackReady.
void PickTexturePack() {
  JNIEnv* env = static_cast< JNIEnv* >(SDL_GetAndroidJNIEnv());
  jobject activity = static_cast< jobject >(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    SetTexturePackStatus("Could not open the folder picker.");
    return;
  }
  jclass cls = env->GetObjectClass(activity);
  jmethodID method = env->GetMethodID(cls, "pickTexturePack", "(Ljava/lang/String;)V");
  if (method != nullptr) {
    // The pack is copied next to the user textures folder, wherever the data folder is.
    const std::string folder = PortUtf8::ToUtf8(PortUtf8::FromUtf8(PortTextures::UserRoot()).parent_path());
    jstring jfolder = env->NewStringUTF(folder.c_str());
    env->CallVoidMethod(activity, method, jfolder);
    env->DeleteLocalRef(jfolder);
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    SetTexturePackStatus("Could not open the folder picker.");
  }
  env->DeleteLocalRef(cls);
  env->DeleteLocalRef(activity);
}
#endif

#if !defined(__ANDROID__)
// Import texture pack .zip: the file dialog answers on another thread, so its pick waits in
// sTexturePackZipPick for DrawTexturesSection; the extraction runs on its own detached thread (it
// touches only statics) into `<root>.partial`, then the finished folder is renamed to
// `<root>.new` for PortTextures to swap in.
namespace {
std::mutex sTexturePackZipMutex;
std::optional<std::string> sTexturePackZipPick;
std::atomic<bool> sTexturePackZipDialogOpen{false};
std::atomic<bool> sTexturePackZipBusy{false};

void ImportTexturePackZip(std::string zipPath, std::string root) {
  namespace fs = std::filesystem;
  const std::string partial = root + ".partial";
  std::error_code ec;
  fs::remove_all(PortUtf8::FromUtf8(partial), ec);
  size_t count = 0;
  std::string error;
  const bool ok = PortZip::Extract(
      zipPath, partial, PortZip::TexturePackFilter,
      [](size_t done) { SetTexturePackStatus("Copying the texture pack... " + std::to_string(done) + " textures"); },
      count, error);
  if (!ok) {
    fs::remove_all(PortUtf8::FromUtf8(partial), ec);
    SetTexturePackStatus("Copying the texture pack failed: " + error);
  } else if (count == 0) {
    fs::remove_all(PortUtf8::FromUtf8(partial), ec);
    SetTexturePackStatus("No .png or .dds textures in that file (is it a .zip?); nothing changed.");
  } else {
    fs::remove_all(PortUtf8::FromUtf8(root + ".new"), ec);
    fs::rename(PortUtf8::FromUtf8(partial), PortUtf8::FromUtf8(root + ".new"), ec);
    if (ec) {
      fs::remove_all(PortUtf8::FromUtf8(partial), ec);
      SetTexturePackStatus("Copying the texture pack failed: " + ec.message());
    } else {
      PortTextures::RequestUserPackReload();
      SetTexturePackStatus("Copied " + std::to_string(count) + " textures.");
    }
  }
  sTexturePackZipBusy = false;
}

void OpenTexturePackZipDialog() {
  int windowCount = 0;
  SDL_Window** windows = SDL_GetWindows(&windowCount);
  SDL_Window* window = windows != nullptr && windowCount > 0 ? windows[0] : nullptr;
  SDL_free(windows);
  static const SDL_DialogFileFilter filters[] = {
      {"Zip archives", "zip"},
      {"All files", "*"},
  };
  const SDL_DialogFileCallback done = [](void*, const char* const* files, int) {
    std::lock_guard lock(sTexturePackZipMutex);
    // Empty = cancelled; a leading \x01 marks a failed dialog's error.
    sTexturePackZipPick = files != nullptr ? std::string(files[0] != nullptr ? files[0] : "")
                                           : std::string("\x01") + SDL_GetError();
    sTexturePackZipDialogOpen = false;
  };
  sTexturePackZipDialogOpen = true;
  SDL_ShowOpenFileDialog(done, nullptr, window, filters, 2, nullptr, false);
}

// Starts the extraction for a pick the dialog delivered.
void ProcessTexturePackZipPick(const char* root) {
  std::optional<std::string> pick;
  {
    std::lock_guard lock(sTexturePackZipMutex);
    pick.swap(sTexturePackZipPick);
  }
  if (!pick || pick->empty()) {
    return;
  }
  if ((*pick)[0] == '\x01') {
    SetTexturePackStatus("Could not open the file picker: " + pick->substr(1));
    return;
  }
  if (sTexturePackZipBusy.exchange(true)) {
    SetTexturePackStatus("A texture pack is still being copied.");
    return;
  }
  SetTexturePackStatus("Copying the texture pack... 0 textures");
  std::thread(ImportTexturePackZip, std::move(*pick), std::string(root != nullptr ? root : "")).detach();
}
} // namespace
#endif

// The user's texture pack, layered over the built-in set (see port_textures.h). On Android the
// folder is picked with the system folder picker and copied into the data folder; on the desktop
// the player fills the folder themselves.
void DrawTexturesSection() {
  ImGui::SeparatorText("Textures");
  ImGui::Text("HD texture set: %s", PortTextures::DeviceName());
  ImGui::SetItemTooltip("Follows the input last used (xbox, playstation, switch,\n"
                        "gamecube, standard, keyboard); set MP_TEXTURE_DEVICE to override.");
  const char* root = PortTextures::UserRoot();
  if (root[0] == '\0') {
    ImGui::TextUnformatted("No user texture folder.");
    return;
  }
  bool enabled = PortTextures::UserPackEnabled();
  const std::string toggleKeys = PortInputRemap::BindingsText(PortInput::Action::ToggleTexturePack, " / ");
  const std::string toggleLabel =
      (toggleKeys.empty() ? std::string("Use the texture pack") : "Use the texture pack (" + toggleKeys + ")") +
      "###texture-pack-enabled";
  if (ImGui::Checkbox(toggleLabel.c_str(), &enabled)) {
    PortTextures::SetUserPackEnabled(enabled);
  }
  ImGui::SetItemTooltip("Off shows the game's own textures, without deleting the pack.\n"
                        "The binding (Controls > Remap > Port) switches it in game, to compare.");
  const size_t count = PortTextures::UserPackCount();
  if (!enabled) {
    ImGui::TextUnformatted("Texture pack off.");
  } else if (count > 0) {
    ImGui::Text("%zu replacements loaded, over the built-in set.", count);
  } else {
    ImGui::TextUnformatted("No texture pack loaded.");
  }
#if defined(__ANDROID__)
  const std::string status = TexturePackStatus();
  if (!status.empty()) {
    ImGui::TextWrapped("%s", status.c_str());
  }
  if (ImGui::Button("Choose texture pack folder...")) {
    PickTexturePack();
  }
  ImGui::SameLine();
  if (ImGui::Button("Remove texture pack")) {
    PortTextures::RequestUserPackRemoval();
    SetTexturePackStatus("Texture pack removed.");
  }
  ImGui::TextWrapped("The folder is copied into the data folder, so it keeps working if the "
                     "original is moved. Pick it again after changing it.");
#else
  ImGui::TextWrapped("Folder: %s", root);
  ProcessTexturePackZipPick(root);
  const std::string status = TexturePackStatus();
  if (!status.empty()) {
    ImGui::TextWrapped("%s", status.c_str());
  }
  if (ImGui::Button("Reload texture pack")) {
    PortTextures::RequestUserPackReload();
  }
  ImGui::SameLine();
  const bool busy = sTexturePackZipBusy || sTexturePackZipDialogOpen;
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Import texture pack .zip...")) {
    OpenTexturePackZipDialog();
  }
  ImGui::EndDisabled();
#endif
}

// The look of this overlay: the floating window, the theme and the size.
void DrawOverlaySection() {
  ImGui::SeparatorText("Overlay");
#if !defined(PORT_MOBILE)
  Changed(ImGui::Checkbox("Overlay as a floating window", &sOverlayWindowed));
#endif
  int theme = sUiTheme;
  if (ImGui::Combo("Menu theme", &theme, "Auto\0Metroid Prime\0Plain\0Echoes\0")) {
    sUiTheme = theme;
    Changed(true);
  }
  ItemHelp("The overlay's look. Auto is the Metroid Prime pause-screen style; Plain is stock Dear "
           "ImGui.");
  // Multiplies the display's own scale (UpdateUiScale); the product is clamped to 1..4, so on a
  // desktop at scale 1 only values above 100% make the window larger.
  Changed(ImGui::SliderInt("UI scale", &sUiScale, PortOverlay::kUiScaleMin, PortOverlay::kUiScaleMax, "%d%%"));
  ItemHelp("The size of this window's text and controls.");
}

void DrawSettingsSection() {
  ImGui::SeparatorText("Settings");
  if (ImGui::Button("Save settings now")) {
    PortOverlaySettings::MarkDirty();
    PortOverlaySettings::SaveSettings();
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(PortOverlaySettings::Dirty() ? "Unsaved changes" : "Saved");
  ImGui::SetItemTooltip("Settings are saved automatically when changed. An MP2_<KEY> environment\n"
                        "variable overrides the file for one run.");
  const std::string& path = PortSettings::FilePath();
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("File: %s", path.empty() ? "(no user folder)" : path.c_str());
  ImGui::PopStyleColor();

  static std::string sResetError;
  ImGui::BeginDisabled(path.empty());
  if (ImGui::Button("Reset configuration...")) {
    sResetError.clear();
    ImGui::OpenPopup("Reset configuration?");
  }
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("Back up and clear every port setting and control mapping.");
  if (ImGui::BeginPopupModal("Reset configuration?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted("This resets ALL port settings and control mappings (keyboard, mouse, touch and "
                           "controllers) to their defaults on the next launch. The current files are moved to a "
                           "backup folder inside the user folder, not deleted.");
    ImGui::TextUnformatted("Save files, save states, the disc image and the data folder location are not "
                           "touched. Game options (volumes, brightness...) stored inside a save file come back "
                           "when that save loads.");
    ImGui::TextUnformatted("The game closes now: progress since the last save station is lost. Start it again to "
                           "get the defaults.");
    if (!sResetError.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ThemeBadColor());
      ImGui::TextWrapped("Reset failed: %s", sResetError.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Reset and exit")) {
      // Back up what is current, not what was last saved; a failed write stops here.
      PortOverlaySettings::MarkDirty();
      PortOverlaySettings::SaveSettings();
      if (PortOverlaySettings::Dirty()) {
        sResetError = "could not save the current settings first, so nothing was moved.";
      } else {
        const PortConfigReset::Result result = PortConfigReset::Reset(
            PortUtf8::FromUtf8(path).parent_path().empty() ? std::filesystem::path("./")
                                                           : PortUtf8::FromUtf8(path).parent_path());
        if (!result.ok) {
          sResetError = result.error;
          if (!result.backupDir.empty()) {
            sResetError += " Files that could not be restored are in " + result.backupDir + ".";
          } else {
            sResetError += " Your configuration files were not moved.";
          }
          std::fprintf(stderr, "port: reset configuration failed: %s\n", result.error.c_str());
        } else {
          std::fprintf(stderr, "port: reset configuration: %s\n",
                       result.moved ? ("backup in " + result.backupDir).c_str() : "no files to move");
          // From here no code path may write the configuration again.
          PortOverlaySettings::FreezeAfterReset();
          aurora_disable_config_persistence();
          if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::GetIO().IniFilename = nullptr;
          }
          SDL_Event quit{};
          quit.type = SDL_EVENT_QUIT;
          SDL_PushEvent(&quit);
          ImGui::CloseCurrentPopup();
        }
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

// The whole disc image check (port_disc_verify.h) and MP1's "Replace disc image" (port_disc_replace.h):
// the picked file is checked and remembered for the next start; the mounted disc is not swapped.
void DrawDiscImageSection() {
  using PortDiscVerify::State;
  ImGui::SeparatorText("Disc image");
  const PortDiscVerify::Result r = PortDiscVerify::Snapshot();
  const bool running = r.state == State::Running;
  if (const std::string image = PortDiscVerify::ImagePath(); !image.empty()) {
    ImGui::TextWrapped("Disc image: %s", image.c_str());
  }
  if (PortDiscReplace::Poll()) {
    // Remembered; the status below says to restart.
  }
  if (const std::string next = PortOverlaySettings::DiscPath(); !next.empty()) {
    ImGui::TextWrapped("Disc image for the next start: %s", next.c_str());
  }
  ImGui::BeginDisabled(running);
  if (ImGui::Button("Check disc image")) {
    PortDiscVerify::Start();
  }
  ImGui::EndDisabled();
  const PortDiscReplace::Status replace = PortDiscReplace::Snapshot();
  ImGui::BeginDisabled(replace.state == PortDiscReplace::State::Running);
  ImGui::SameLine();
  if (ImGui::Button("Replace disc image...")) {
    PortDiscReplace::PickAsync();
  }
  ImGui::EndDisabled();
  ItemHelp("Picks another disc image (Echoes USA 1.00, PAL, or Metroid Prime Trilogy), checks it, and "
           "remembers it for the next start. The game keeps running from the disc it started with. A "
           "file or MP2_DISC given on the command line still wins.");
  if (replace.state == PortDiscReplace::State::Running) {
    ImGui::SameLine();
    if (ImGui::Button("Cancel##replace")) {
      PortDiscReplace::Cancel();
    }
    if (replace.copying && replace.bytesTotal > 0) {
      ImGui::ProgressBar(static_cast< float >(replace.bytesDone) / static_cast< float >(replace.bytesTotal),
                         ImVec2(-FLT_MIN, 0.0f));
    } else {
      ImGui::TextUnformatted("Checking the picked file...");
    }
  } else if (replace.state == PortDiscReplace::State::Failed) {
    ImGui::TextWrapped("Not changed: %s", replace.message.c_str());
  } else if (replace.state == PortDiscReplace::State::Done) {
    ImGui::TextWrapped("%s (%s: %s)", replace.message.c_str(), replace.revision.c_str(), replace.path.c_str());
  }
  if (running) {
    ImGui::SameLine();
    if (ImGui::Button("Cancel##disc")) {
      PortDiscVerify::Cancel();
    }
    ImGui::ProgressBar(static_cast< float >(r.Progress()), ImVec2(-FLT_MIN, 0.0f));
  }
  switch (r.state) {
  case State::Idle:
    ImGui::TextWrapped("Compares the whole disc image with the known dump of this release to find "
                       "damage. Runs in the background and takes a minute or two; the result goes "
                       "to the log.");
    break;
  case State::Running:
    ImGui::TextWrapped("Checking the disc image (%.0f%%)...", r.Progress() * 100.0);
    break;
  case State::Cancelled:
    ImGui::TextWrapped("The check was cancelled.");
    break;
  case State::Error:
    ImGui::TextWrapped("The disc image couldn't be checked: %s.", r.error.c_str());
    break;
  case State::Done:
    if (r.Ok()) {
      ImGui::TextWrapped("This disc image is complete and unaltered (%s).",
                         r.revision.c_str());
      ImGui::TextDisabled("SHA-1 %s", r.sha1.c_str());
    } else if (!r.known) {
      ImGui::TextWrapped("This disc image is not a release the port has a record of.");
      ImGui::TextDisabled("SHA-1 %s", r.sha1.c_str());
    } else if (r.bad.empty()) {
      ImGui::TextWrapped("Every part of this %s image matches, but the file does not end where the "
                         "original does.",
                         r.revision.c_str());
    } else {
      ImGui::TextWrapped("This disc image is damaged: %zu of %u MiB differ from %s (%u of them are "
                         "blank).",
                         r.bad.size(), r.chunks, r.revision.c_str(), r.zeroed);
      if (!r.files.empty()) {
        std::string files;
        for (size_t i = 0; i < r.files.size() && i < 8; ++i) {
          files += (i ? ", " : "") + r.files[i];
        }
        if (r.files.size() > 8) {
          files += ", ...";
        }
        ImGui::TextWrapped("Damaged: %s", files.c_str());
      }
      ImGui::TextWrapped("Copy the disc image again; this part of it is damaged.");
    }
    break;
  }
}

// Discord Rich Presence (port_discord.h): MP1's Game > Discord section. The presence itself is
// fed from CStateManager::Update; this row only turns it on and reports what the background
// thread is doing.
void DrawDiscordSection() {
  if (!PortDiscord::Supported()) {
    return;
  }
  ImGui::SeparatorText("Discord");
  bool discord = sDiscord;
  if (ImGui::Checkbox("Rich Presence", &discord)) {
    sDiscord = discord;
    PortDiscord::Configure(discord);
    Changed(true);
  }
  ItemHelp("Shows the area, energy, missiles and item percentage on your Discord profile while the "
           "desktop Discord client runs. Nothing is sent when it is not running.");
  switch (PortDiscord::Status()) {
  case PortDiscord::kStatus_Off:
    ImGui::TextDisabled("off");
    break;
  case PortDiscord::kStatus_Connecting:
    ImGui::TextUnformatted("connecting...");
    break;
  case PortDiscord::kStatus_Connected:
    ImGui::TextColored(ThemeGoodColor(), "connected");
    break;
  case PortDiscord::kStatus_Failed:
    ImGui::TextColored(ThemeBadColor(), "%s", PortDiscord::LastError().c_str());
    break;
  }
  if (sDiscord) {
    ImGui::TextDisabled("Showing: %s", PortDiscord::CurrentText().c_str());
  }
}

// MP1's Game > Updates section (port_update_check.h): asks GitHub's releases API for
// Odrannnn/MetroidPrime2Port once at launch, or on "Check now".
void ApplyUpdateCheck() {
  const std::string& file = PortSettings::FilePath();
  const std::filesystem::path dir = file.empty() ? std::filesystem::path("./") : PortUtf8::FromUtf8(file).parent_path();
  PortUpdateCheck::Configure(sUpdateCheck && port::EnvFlag("MP2_UPDATE_CHECK", true), MP_BUILD_VERSION,
                             PortUtf8::ToUtf8(dir / "update-check.txt"));
}

void DrawUpdateSection() {
  ImGui::SeparatorText("Updates");
  bool check = sUpdateCheck;
  if (ImGui::Checkbox("Check for updates", &check)) {
    sUpdateCheck = check;
    ApplyUpdateCheck();
    PortUpdateCheck::CheckNow(); // switched on: check now, not next launch
    Changed(true);
  }
  ItemHelp("Asks GitHub (api.github.com) once at launch whether a newer release is out. Nothing "
           "else is sent, and nothing is installed: the release page opens in your browser.");
  const PortUpdateCheck::EStatus status = PortUpdateCheck::Status();
  switch (status) {
  case PortUpdateCheck::kStatus_Off:
    ImGui::TextDisabled(sUpdateCheck ? "off (MP2_UPDATE_CHECK=0)" : "off");
    break;
  case PortUpdateCheck::kStatus_Checking:
    ImGui::TextUnformatted("checking...");
    break;
  case PortUpdateCheck::kStatus_UpToDate:
    ImGui::TextColored(ThemeGoodColor(), "up to date");
    break;
  case PortUpdateCheck::kStatus_Available:
    ImGui::TextColored(ThemeGoodColor(), "%s is out", PortUpdateCheck::Latest().version.c_str());
    break;
  case PortUpdateCheck::kStatus_Failed:
    ImGui::TextColored(ThemeBadColor(), "%s", PortUpdateCheck::LastError().c_str());
    break;
  }
  if (status == PortUpdateCheck::kStatus_Off) {
    return;
  }
  ImGui::BeginDisabled(status == PortUpdateCheck::kStatus_Checking);
  if (ImGui::Button("Check now")) {
    PortUpdateCheck::CheckNow();
  }
  ImGui::EndDisabled();
  if (status == PortUpdateCheck::kStatus_Available) {
    ImGui::SameLine();
    if (ImGui::Button("Open release page")) {
      SDL_OpenURL(PortUpdateCheck::Latest().url.c_str());
    }
  }
  if (const int64_t checked = PortUpdateCheck::LastChecked(); checked > 0) {
    const time_t when = static_cast< time_t >(checked);
    char text[64] = {};
    if (const std::tm* local = std::localtime(&when)) {
      std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", local);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("last checked %s", text);
  }
}

void DrawMemoryCard() {
  ImGui::SeparatorText("Memory card");
  Changed(ImGui::Checkbox("Back up the card", &sCardBackup));
  ItemHelp("Copies the save file into save-backups before each card write; `save import latest` on "
           "the console puts one back.");
  DrawCardTransfer();
}

void DrawLogSection() {
  ImGui::SeparatorText("Log");
  // PortLogFile::Start reads the setting before Aurora comes up, so the row sets the next launch.
  Changed(ImGui::Checkbox("Write the log to a file", &sLogFile));
  const std::string logPath = PortLogFile::Path();
  if (!logPath.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Copy log path")) {
      ImGui::SetClipboardText(logPath.c_str());
    }
  }
  if (PortLogFile::Enabled()) {
    ImGui::TextWrapped("Writing to %s.", logPath.c_str());
    if (!sLogFile) {
      ImGui::TextDisabled("Stops at the next start.");
    }
  } else {
    ImGui::TextWrapped("Everything the game logs, including the reason for a crash, goes to %s from the "
                       "next start.",
                       logPath.empty() ? "metroid_prime2_port.log in the user folder" : logPath.c_str());
  }
}

void ToggleTexturePack() {
  const bool on = !PortTextures::UserPackEnabled();
  PortTextures::SetUserPackEnabled(on);
  // Saved now: outside the overlay, changes are otherwise written at exit.
  PortOverlaySettings::MarkDirty();
  PortOverlaySettings::SaveSettings();
  ShowNote(on ? "Texture pack: on" : "Texture pack: off");
}

} // namespace PortDebug
