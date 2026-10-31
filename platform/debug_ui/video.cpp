// F1 overlay: The Video page's Display, Quality and Compatibility tabs, and the shader-compilation
// and F1-hint toasts. State and helpers shared
// with the other parts are in debug_ui_internal.h.

#include "debug_ui_internal.h"

#include "port_gpu_driver.h"
#include "port_input_remap_ui.h"
#include "port_overlay.h"
#include "port_savestate.h"
#include "port_suit_colours.h"
#include "port_build_info.h"
#include "port_update_check.h"
#include "port_video.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace PortDebug {

void DrawVideoDisplay() {
  ImGui::SeparatorText("Display");
  bool fullscreen = sFullscreen;
#if defined(__ANDROID__)
  if (ImGui::Checkbox("Fullscreen (hide the status and navigation bars)", &fullscreen)) {
#else
  if (ImGui::Checkbox("Fullscreen (F11)", &fullscreen)) {
#endif
    PortOverlay::SetFullscreen(fullscreen);
  }
  // Applied by PortOverlay::Draw through aurora_enable_vsync, every frame it changes.
  Changed(ImGui::Checkbox("Vsync", &sVsync));
  ImGui::SetItemTooltip("Waits for the display's refresh before presenting a frame.");
  // Applied by ApplyAspect on the next loop iteration: the render mode is widened and the world and
  // GUI cameras follow its viewport.
  int aspect = sAspectMode;
  if (ImGui::Combo("Aspect ratio", &aspect, "4:3\0" "16:9\0" "Follow window\0")) {
    sAspectMode = aspect;
    Changed(true);
  }
  ImGui::SetItemTooltip("4:3 keeps the game's own picture; Follow window fills the window's shape.");

  ImGui::SeparatorText("Suit colours");
  {
    static int sSuitColoursSuit = 0;
    const PortSuitColours::Suit& suit = PortSuitColours::SuitInfo(sSuitColoursSuit);
    if (ImGui::BeginCombo("Suit", suit.name)) {
      for (int i = 0; i < PortSuitColours::kSuitCount; ++i) {
        const bool custom = !PortSuitColours::PicksKey(i).empty();
        const std::string label = std::string(PortSuitColours::SuitInfo(i).name) + (custom ? " *" : "");
        if (ImGui::Selectable(label.c_str(), i == sSuitColoursSuit)) {
          sSuitColoursSuit = i;
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(
        "Each suit's colours, saved per suit (* = changed). Recoloured from your disc, so the\n"
        "change shows everywhere it is used; Apply suit colours (or a restart) puts it in the game.\n"
        "A mod's own suit textures are left alone. The Light Suit's Main colour tints its\n"
        "silver and white body; its Accent is the gold trim.");
    for (int slot = 0; slot < suit.slots; ++slot) {
      ImGui::PushID(slot);
      const std::optional< uint32_t > picked = PortSuitColours::Colour(sSuitColoursSuit, slot);
      const uint32_t shown = picked ? *picked : PortSuitColours::DefaultColour(sSuitColoursSuit, slot);
      float rgb[3] = {float((shown >> 16) & 255) / 255.f, float((shown >> 8) & 255) / 255.f,
                      float(shown & 255) / 255.f};
      if (ImGui::ColorEdit3(suit.slotNames[slot], rgb, ImGuiColorEditFlags_NoInputs)) {
        uint32_t value = 0;
        for (const float c : rgb) {
          value = (value << 8) | uint32_t(std::clamp(std::lround(c * 255.f), 0l, 255l));
        }
        PortSuitColours::SetColour(sSuitColoursSuit, slot, value);
        Changed(true);
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!picked);
      if (ImGui::SmallButton("Reset")) {
        PortSuitColours::SetColour(sSuitColoursSuit, slot, std::nullopt);
        Changed(true);
      }
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    if (ImGui::Button("Random")) {
      PortSuitColours::Randomise(sSuitColoursSuit, std::random_device{}());
      Changed(true);
    }
    ImGui::SetItemTooltip("Random colours for every slot of this suit; Apply suit colours to see them.");
    ImGui::SameLine();
    // Not on every picker drag frame: the reload rebuilds the room, so it is the player's click.
    const bool pending = PortSuitColours::Pending(true);
    const bool reloading = PortSaveState::ModReloadActive();
    ImGui::BeginDisabled(!pending || reloading);
    if (ImGui::Button(reloading ? "Applying..." : "Apply suit colours")) {
      PortSaveState::RequestModReload();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Recolours the suit textures and rebuilds the room (the same as Game > Mods >\n"
                          "Reload mods), so the suit shows the new colours without a restart. In the pause\n"
                          "menu it refreshes the Inventory model at once and the room follows when you\n"
                          "unpause; on the title screen they apply at the next start.");
    if (pending) {
      ImGui::TextDisabled("Colours not applied yet: Apply suit colours, or they apply at the next start.");
    }
  }

  ImGui::SeparatorText("HUD and view");
  bool hudWide = sHudWide;
  if (ImGui::Checkbox("Widescreen HUD (spread to edges)", &hudWide)) {
    PortOverlay::SetHudWide(hudWide);
  }
  ImGui::SetItemTooltip("Spreads the HUD to the screen edges on a wide screen. Off keeps its 4:3\n"
                        "proportions, centred.");
  {
    static const char* const kSpreadMax[] = {"16:9", "21:9", "Full screen"};
    int spreadMax = PortOverlay::HudSpreadMax();
    ImGui::BeginDisabled(!hudWide);
    if (ImGui::Combo("Widest HUD spread", &spreadMax, kSpreadMax, 3)) {
      PortOverlay::SetHudSpreadMax(spreadMax);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("The widest screen the HUD spreads to; on a wider one it stays centred at\n"
                          "that width. Applies only with the widescreen HUD on.");
  }
  // Read by CGuiCamera::Draw while CSamusHud::Draw is drawing the HUD
  // (PortOverlay::HudScalingActive): the HUD's projection is scaled about the screen centre.
  Changed(ImGui::SliderInt("HUD scale", &sHudScale, PortOverlay::kHudScaleMin, PortOverlay::kHudScaleMax, "%d%%"));
  ImGui::SetItemTooltip("Shrinks the HUD toward the middle; the helmet and menus keep their size.");
  ImGui::BeginDisabled(!hudWide);
  Changed(ImGui::SliderInt("Scan image spread", &sScanSpread, PortOverlay::kScanSpreadMin,
                           PortOverlay::kScanSpreadMax, "%d%%"));
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("How far the widescreen HUD moves the scan visor's side panels toward the screen\n"
                        "edges. 0%% keeps them at the 4:3 position, clear of touch controls; 100%% puts\n"
                        "them at the edges.");
  // The game's Visor Opacity is one value for every visor. A ticked box follows it; an unticked one
  // gives that visor's HUD (and its menus and radar) its own.
  const auto hudOpacityRow = [](const char* label, int& value) {
    ImGui::PushID(label);
    bool followVisor = value < PortOverlay::kHudOpacityMin;
    if (ImGui::Checkbox("##follow", &followVisor)) {
      value = followVisor ? PortOverlay::kHudOpacityVisor : PortOverlay::kHudOpacityMax;
      Changed(true);
    }
    ImGui::SetItemTooltip("On: use the game's Visor Opacity (Options > Visor).");
    ImGui::SameLine();
    ImGui::BeginDisabled(followVisor);
    int percent = followVisor ? PortOverlay::kHudOpacityMax : value;
    if (ImGui::SliderInt(label, &percent, PortOverlay::kHudOpacityMin, PortOverlay::kHudOpacityMax,
                         followVisor ? "Visor Opacity" : "%d%%")) {
      value = percent;
      Changed(true);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  };
  hudOpacityRow("Echo HUD opacity", sHudOpacityEcho);
  hudOpacityRow("Dark HUD opacity", sHudOpacityDark);
  hudOpacityRow("Scan HUD opacity", sHudOpacityScan);
  if (sAimCrosshair) {
    Changed(ImGui::SliderInt("Crosshair opacity", &sCrosshairOpacity, PortOverlay::kCrosshairOpacityMin,
                             PortOverlay::kCrosshairOpacityMax, "%d%%"));
    ImGui::SetItemTooltip("The crosshair in the middle of the screen (mouse aim, twin stick, touch).");
    int style = sCrosshairStyle;
    static const char* const kStyles[] = {"Classic", "Remastered", "GameCube"};
    if (ImGui::Combo("Crosshair style", &style, kStyles, 3)) {
      sCrosshairStyle = std::clamp(style, PortOverlay::kCrosshairStyle_Classic, PortOverlay::kCrosshairStyle_GameCube);
      Changed(true);
    }
    ImGui::SetItemTooltip("Remastered is the one this port can draw on a GameCube disc: the classic\n"
                          "reticle's model does not load there. GameCube: on a Trilogy disc, the\n"
                          "GameCube crosshair in place of Trilogy's aiming cursor (Classic on a\n"
                          "GameCube disc).");
    Changed(ImGui::Checkbox("Custom crosshair colour", &sCrosshairCustomColor));
    ImGui::SetItemTooltip("Off: the game's own colour.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!sCrosshairCustomColor);
    Changed(ImGui::ColorEdit3("##crosshair_color", sCrosshairColor,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel));
    ImGui::EndDisabled();
  }
  Changed(ImGui::Checkbox("Hide helmet", &sHideHelmet));
  Changed(ImGui::Checkbox("Hide visor effects", &sHideVisorEffects));
  ImGui::SetItemTooltip("The scan visor's window and edges are what a visor effect draws here.");
  Changed(ImGui::Checkbox("Cutscene black bars", &sCinemaBars));
  ImGui::SetItemTooltip("Off lets a cutscene fill the screen instead of the game's letterbox.");
  Changed(ImGui::Checkbox("Secondary crosshairs", &sSecondaryCrosshairs));
  ImGui::SetItemTooltip("Off hides the next-target reticle; the lock-on reticle and the scan box stay.");
  // Applied by CCameraManager::UpdateCameras: the first-person camera takes its FOV when it is
  // built, so the live change goes through CCameraManager::mFirstPersonFov. 0 is the game's own 55.
  float fov = sFirstPersonFov > 0.f ? sFirstPersonFov : 55.f;
  if (ImGui::SliderFloat("Field of view", &fov, 30.f, 120.f, "%.0f deg")) {
    sFirstPersonFov = std::isfinite(fov) ? fov : 0.f;
    Changed(true);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(sFirstPersonFov <= 0.f);
  if (ImGui::Button("Retail##fov")) {
    sFirstPersonFov = 0.f;
    Changed(true);
  }
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("Back to the retail 55 deg.");
  {
    float offset[3] = {sGunOffset[0], sGunOffset[1], sGunOffset[2]};
    bool changed = ImGui::SliderFloat("Arm cannon right", &offset[0], -PortOverlay::kGunOffsetMax,
                                      PortOverlay::kGunOffsetMax, "%.2f m");
    changed |= ImGui::SliderFloat("Arm cannon forward", &offset[1], -PortOverlay::kGunOffsetMax,
                                  PortOverlay::kGunOffsetMax, "%.2f m");
    changed |= ImGui::SliderFloat("Arm cannon up", &offset[2], -PortOverlay::kGunOffsetMax,
                                  PortOverlay::kGunOffsetMax, "%.2f m");
    if (changed) {
      // Read every frame by CPlayer::UpdateArmAndGunTransforms, so the gun moves on the next update
      // and the shots leave from the muzzle that is drawn.
      StoreGunOffset(offset);
      Changed(true);
    }
    if (ImGui::Button("Retail##gunoffset")) {
      const float zero[3] = {0.f, 0.f, 0.f};
      StoreGunOffset(zero);
      Changed(true);
    }
    ImGui::SetItemTooltip("Back to the retail place.");
  }
}

void DrawVideoQuality() {
  ImGui::SeparatorText("Quality");
  // Applied by PortOverlay::Draw through aurora_set_graphics_quality: the framebuffers are rebuilt
  // at the next frame start.
  int msaa = sMsaa >= 4 ? 1 : 0;
  if (ImGui::Combo("Anti-aliasing", &msaa, "Off\0" "4x MSAA\0")) {
    sMsaa = msaa == 1 ? 4 : 1;
    Changed(true);
  }
  // The two toasts are drawn by DrawUI (overlays.cpp), with the panel open or closed.
  Changed(ImGui::Checkbox("Show shader compilation", &sShowShaderCompilation));
  ImGui::SetItemTooltip("A small progress bar while the shader cache compiles at startup (longest on the first\n"
                        "start after an install or update). Draws whose shader isn't ready yet are skipped.");
  Changed(ImGui::Checkbox("Show F1 hint at launch", &sShowMenuHint));
  ImGui::SetItemTooltip("A note in the top left corner for the first seconds of each launch saying how to\n"
                        "open this panel.");
  // A max, so the combo shows the smallest offered step that covers it (a hand-written MP2_ANISO=3
  // reads as 4x); picking a row sets that step.
  static const int kAnisoSteps[] = {1, 2, 4, 8, 16};
  int aniso = 4;
  for (int i = 0; i < 5; ++i) {
    if (kAnisoSteps[i] >= sAnisotropy) {
      aniso = i;
      break;
    }
  }
  if (ImGui::Combo("Anisotropic filtering", &aniso, "1x\0" "2x\0" "4x\0" "8x\0" "16x\0")) {
    sAnisotropy = kAnisoSteps[aniso];
    Changed(true);
  }
  // Applied by PortVideo::Frame through aurora_set_per_pixel_lighting, on change.
  Changed(ImGui::Checkbox("Per-pixel lighting", &sPerPixelLighting));
  ImGui::SetItemTooltip("Sums the game's lights per pixel instead of per vertex, like Dolphin: smoother\n"
                        "on low-poly models, not the original look.");
  // Applied by ApplyRenderScale from Draw, through PortVideo::DrawScale (dynamic resolution's while
  // it has lowered the scale).
  // Dynamic resolution's mode is the two saved settings together: Auto draws up to the window's own
  // scale (auto render scale), User up to the resolution scale set. Run by PortVideo::Frame once a
  // second (port_dynamic_res.cpp decides the steps).
  int dynMode = !sDynamicRes ? 0 : sRenderScale <= 0.f ? 1 : 2;
  if (ImGui::Combo("Dynamic resolution", &dynMode, "Off\0" "Auto\0" "User\0")) {
    sDynamicRes = dynMode != 0;
    if (dynMode == 1) {
      sRenderScale = 0.f;
    } else if (dynMode == 2 && sRenderScale <= 0.f) {
      sRenderScale = std::clamp(std::round(PortVideo::BaseRenderScale() * 4.f) / 4.f, 1.f, 4.f);
    }
    PortVideo::ResetDynamicRes();
    Changed(true);
  }
  ImGui::SetItemTooltip("Lowers the resolution scale while the GPU can't hold the target frame rate, and\n"
                        "raises it back when there is room. Each change stutters for a frame, so it moves\n"
                        "only when that is worth it.\n"
                        "Auto: up to the window's own resolution. User: between a minimum and a maximum.");
  // Applied by ApplyRenderScale from Draw, through PortVideo::DrawScale (dynamic resolution's while
  // it has lowered the scale). Applied when a slider is let go: each new scale reallocates the EFB
  // targets, which a drag would otherwise do every frame.
  static float sPendingScale = 0.f;
  static float sPendingMin = 0.f;
  static int sPendingTarget = -1;
  if (dynMode == 0) {
    bool autoScale = sRenderScale <= 0.f;
    if (ImGui::Checkbox("Auto render scale (native)", &autoScale)) {
      sRenderScale = autoScale ? 0.f : 1.f;
      Changed(true);
    }
    ItemHelp("Draws at the window's own size.");
  }
  if (dynMode != 1 && sRenderScale > 0.f) {
    float scale = sPendingScale > 0.f ? sPendingScale : sRenderScale;
    char label[32];
    std::snprintf(label, sizeof(label), "%.2fx (%dp)", scale, PortVideo::DrawnLines(scale));
    if (ImGui::SliderFloat(dynMode == 2 ? "Maximum resolution scale" : "Resolution scale", &scale, 1.f, 4.f,
                           label)) {
      sPendingScale = scale;
    }
    if (sPendingScale > 0.f && !ImGui::IsItemActive()) {
      sRenderScale = sPendingScale;
      sPendingScale = 0.f;
      PortVideo::ResetDynamicRes();
      Changed(true);
    }
    ImGui::SetItemTooltip("The resolution the game draws at, as a multiple of the GameCube's 480 lines;\n"
                          "higher values use more GPU memory. Above 2x it supersamples, and each step\n"
                          "costs much more GPU time.");
  } else if (sRenderScale <= 0.f) {
    ImGui::TextDisabled("Window resolution: %.2fx (%dp)", PortVideo::BaseRenderScale(), PortVideo::DrawnLines(0.f));
  }
  if (dynMode != 0) {
    const float lowestScale = PortOverlay::kDynamicResMinLowest;
    float lowest = sPendingMin > 0.f ? sPendingMin : sDynamicResMin;
    const float top = std::max(PortVideo::DynamicResTop(), lowestScale);
    char label[32];
    std::snprintf(label, sizeof(label), "%.2fx (%dp)", lowest, PortVideo::DrawnLines(lowest));
    if (ImGui::SliderFloat("Minimum resolution scale", &lowest, lowestScale, top, label,
                           ImGuiSliderFlags_AlwaysClamp)) {
      sPendingMin = lowest;
    }
    if (sPendingMin > 0.f && !ImGui::IsItemActive()) {
      sDynamicResMin = sPendingMin;
      sPendingMin = 0.f;
      PortVideo::ResetDynamicRes();
      Changed(true);
    }
    ImGui::SetItemTooltip("The lowest it drops to. Below 1x it draws fewer pixels than the GameCube did\n"
                          "and looks softer, which lets a phone reach 90 or 120 fps; the lowest values are\n"
                          "for very high resolution displays.");
    // The left end is "Display" (saved as 0).
    constexpr int kTargetMin = 30;
    constexpr int kTargetMax = PortOverlay::kFpsCapMax;
    const int saved = sPendingTarget >= 0 ? sPendingTarget : sDynamicResTarget;
    int target = saved > 0 ? std::clamp(saved, kTargetMin, kTargetMax) : kTargetMin - 1;
    if (ImGui::SliderInt("Frame rate target", &target, kTargetMin - 1, kTargetMax,
                         target < kTargetMin ? "Display" : "%d fps", ImGuiSliderFlags_AlwaysClamp)) {
      sPendingTarget = target < kTargetMin ? 0 : target;
    }
    // Applied when let go, like the scales: a change starts the controller over.
    if (sPendingTarget >= 0 && !ImGui::IsItemActive()) {
      sDynamicResTarget = sPendingTarget;
      sPendingTarget = -1;
      PortVideo::ResetDynamicRes();
      Changed(true);
    }
    ImGui::SetItemTooltip("Display (all the way left): the screen's refresh rate. Never above the frame\n"
                          "rate cap: with the 60 FPS cap on it aims for 60. Ctrl+click to type a value.");
    if (PortVideo::DynamicResTop() <= sDynamicResMin) {
      ImGui::PushTextWrapPos(0.f);
      ImGui::TextColored(ThemeWarnColor(), "Not lowering: the minimum is not below the top scale (%.2fx). "
                         "Lower the minimum%s.", PortVideo::DynamicResTop(), dynMode == 1 ? " or enlarge the window" : "");
      ImGui::PopTextWrapPos();
    }
    const float dynScale = PortVideo::DynamicScale();
    ImGui::Text("Drawing at %.2fx (%dp), target %.0f fps", dynScale > 0.f ? dynScale : PortVideo::DynamicResTop(),
                PortVideo::DrawnLines(dynScale > 0.f ? dynScale : sRenderScale), PortVideo::DynamicResTargetFps());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const char* reason = PortVideo::DynamicResReason();
    const double gpuMs = PortVideo::DynamicResGpuMs();
    if (PortVideo::DynamicResGpuTimesDropped()) {
      ImGui::Text("GPU times not used (none arrived, or lowering on them gained nothing): %s", reason);
    } else if (gpuMs > 0.0) {
      ImGui::Text("GPU %.1f of %.1f ms a frame: %s", gpuMs, 1000.0 / PortVideo::DynamicResTargetFps(), reason);
    } else if (gpuMs < 0.0) {
      ImGui::Text("No GPU times (unsupported, or MP2_DYNRES_GPU_TIMES=0); following the frame rate: %s", reason);
    } else {
      ImGui::Text("%s", reason);
    }
    ImGui::PopStyleColor();
  }
  if (dynMode != 0 || sRenderScale > 0.f) {
    // Applied by PortVideo::ApplyPresentSharpness, on change.
    int sharpen = sSharpenMode;
    if (ImGui::Combo("Sharpen when upscaled", &sharpen, "Off\0" "With dynamic resolution\0" "Always\0")) {
      sSharpenMode = sharpen;
      PortVideo::ApplyPresentSharpness();
      Changed(true);
    }
    ImGui::SetItemTooltip("Sharpens a frame drawn smaller than the window, so a lowered scale looks less\n"
                          "soft. With dynamic resolution: only while it has lowered the scale. Always:\n"
                          "also a fixed low scale on a large window.");
    if (sSharpenMode != 0) {
      if (Changed(ImGui::SliderInt("Sharpening", &sSharpenAmount, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp))) {
        PortVideo::ApplyPresentSharpness();
      }
    }
  }
}


// Custom Vulkan drivers (port_gpu_driver.h, Android only): MP1's picker. The file dialog answers
// on another thread, so its pick waits in sGpuDriverPick until the Compatibility tab draws next.
namespace {
std::mutex sGpuDriverPickMutex;
std::optional< std::string > sGpuDriverPick;
std::atomic< bool > sGpuDriverDialogOpen{false};
std::string sGpuDriverStatus;
std::vector< PortGpuDriver::Driver > sGpuDrivers;
bool sGpuDriversListed = false;

void OpenGpuDriverDialog() {
  int windowCount = 0;
  SDL_Window** windows = SDL_GetWindows(&windowCount);
  SDL_Window* window = windows != nullptr && windowCount > 0 ? windows[0] : nullptr;
  SDL_free(windows);
  const SDL_DialogFileCallback done = [](void*, const char* const* files, int) {
    std::lock_guard lock(sGpuDriverPickMutex);
    // Empty = cancelled; a leading \x01 marks a failed dialog's error.
    sGpuDriverPick = files != nullptr ? std::string(files[0] != nullptr ? files[0] : "")
                                      : std::string("\x01") + SDL_GetError();
    sGpuDriverDialogOpen = false;
  };
  sGpuDriverDialogOpen = true;
  SDL_ShowOpenFileDialog(done, nullptr, window, nullptr, 0, nullptr, false);
}

void SetGpuDriver(const std::string& id) {
  if (sGpuDriver != id) {
    sGpuDriver = id;
    Changed(true);
  }
}

void ProcessGpuDriverPick() {
  std::string path;
  {
    std::lock_guard lock(sGpuDriverPickMutex);
    if (!sGpuDriverPick) {
      return;
    }
    path = std::move(*sGpuDriverPick);
    sGpuDriverPick.reset();
  }
  if (path.empty() || path[0] == '\x01') {
    if (path.size() > 1) {
      sGpuDriverStatus = "The file dialog failed: " + path.substr(1);
    }
    return;
  }
  std::string error;
  const std::string id = PortGpuDriver::Install(path, error);
  sGpuDriversListed = false;
  if (id.empty()) {
    sGpuDriverStatus = "Couldn't install it: " + error + ".";
  } else {
    SetGpuDriver(id);
    sGpuDriverStatus = "Installed " + id + ".";
  }
}

void DrawGpuDriver() {
  if (!PortGpuDriver::Supported() || aurora_get_backend() != BACKEND_VULKAN) {
    return;
  }
  ProcessGpuDriverPick();
  if (!sGpuDriversListed) {
    sGpuDrivers = PortGpuDriver::List();
    sGpuDriversListed = true;
  }
  const auto label = [](const PortGpuDriver::Driver& d) {
    return d.version.empty() ? d.name : d.name + " " + d.version;
  };
  std::string preview = "System";
  for (const auto& d : sGpuDrivers) {
    if (d.id == sGpuDriver) {
      preview = label(d);
    }
  }
  if (ImGui::BeginCombo("Vulkan driver", preview.c_str())) {
    if (ImGui::Selectable("System", sGpuDriver.empty())) {
      SetGpuDriver("");
    }
    for (const auto& d : sGpuDrivers) {
      ImGui::PushID(d.id.c_str());
      if (ImGui::Selectable(label(d).c_str(), d.id == sGpuDriver)) {
        SetGpuDriver(d.id);
      }
      if (!d.description.empty()) {
        ImGui::SetItemTooltip("%s", d.description.c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ImGui::SetItemTooltip(
      "Loads a custom Vulkan driver instead of the phone's, such as Mesa Turnip for\n"
      "Adreno GPUs, from a driver zip. Restart to apply; if it crashes at start, the\n"
      "next start goes back to the system driver.");
  // BeginGpuStart already switched a driver that crashed back to System: nothing to restart for.
  const bool crashedBack = sGpuDriver.empty() && !PortGpuDriver::LoadError().empty();
  if (sGpuDriver != sGpuDriverAtStart && !crashedBack) {
    ImGui::SameLine();
    ImGui::TextColored(ThemeWarnColor(), "Restart to apply");
  }
  ImGui::BeginDisabled(sGpuDriverDialogOpen);
  if (ImGui::Button("Install driver (.zip)...")) {
    sGpuDriverStatus.clear();
    OpenGpuDriverDialog();
  }
  ImGui::EndDisabled();
  const bool removable = !sGpuDriver.empty() && sGpuDriver != PortGpuDriver::Active();
  if (removable) {
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
      const std::string id = sGpuDriver;
      sGpuDriverStatus = PortGpuDriver::Remove(id) ? "Removed " + id + "." : "Couldn't remove " + id + ".";
      SetGpuDriver("");
      sGpuDriversListed = false;
    }
  }
  if (!PortGpuDriver::LoadError().empty() && !sGpuDriverAtStart.empty()) {
    ImGui::TextColored(ThemeWarnColor(), "%s didn't load: %s", sGpuDriverAtStart.c_str(),
                       PortGpuDriver::LoadError().c_str());
  }
  ImGui::TextDisabled("Running: %s", aurora_get_gpu_driver());
  ImGui::SetItemTooltip("What the GPU reports now. A loaded custom driver says so here\n"
                        "(Turnip: \"Mesa Turnip ...\").");
  if (!sGpuDriverStatus.empty()) {
    ImGui::TextWrapped("%s", sGpuDriverStatus.c_str());
  }
}
} // namespace

// What PortVideo::BeginGpuStart sets up before the GPU device is made (so after a restart), and the
// GPU self-test. The Renderer combo is Windows and desktop Linux; the Vulkan driver picker is Android only.
void DrawVideoCompatibility() {
  ImGui::SeparatorText("Compatibility");
  ImGui::PushTextWrapPos(0.f);
  ImGui::TextDisabled("For GPU driver problems, such as a black world or a crash at start. Most of "
                      "these take effect after a restart.");
  ImGui::PopTextWrapPos();
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
  // Which API to ask Aurora for. Read at window creation, so it takes a restart: Aurora tries the
  // choice first and falls back to its own order if it won't start, and the setting is left alone.
  // Only this build's choices are listed, so an API it can't start is never offered.
  {
    const std::span<const PortGraphicsBackend::EGraphicsBackend> choices = PortGraphicsBackend::Choices();
    // The labels in Choices() order, NUL-separated as ImGui wants them, built from the choices so
    // the list cannot drift from the span. Auto asks for nothing, so it is named as such.
    static const std::string kBackendLabels = [] {
      std::string labels;
      for (const PortGraphicsBackend::EGraphicsBackend choice : PortGraphicsBackend::Choices()) {
        labels += choice == PortGraphicsBackend::kGraphicsBackend_Auto
                      ? "Auto"
                      : PortGraphicsBackend::DisplayName(PortGraphicsBackend::ToAurora(choice));
        labels.push_back('\0');
      }
      labels.push_back('\0');
      return labels;
    }();
    // The combo's index is a position in Choices(), never an enum value. A saved choice this build
    // doesn't offer (a file from another platform) shows as Auto and is kept until changed here.
    int choice = 0;
    for (size_t i = 0; i < choices.size(); ++i) {
      if (choices[i] == sGraphicsBackend) {
        choice = static_cast< int >(i);
        break;
      }
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.f);
    if (ImGui::Combo("Renderer", &choice, kBackendLabels.c_str(), static_cast< int >(choices.size()))) {
      sGraphicsBackend = choices[static_cast< size_t >(choice)];
      Changed(true);
    }
#if defined(_WIN32)
    ImGui::SetItemTooltip("Which graphics API to start on. Auto tries Direct3D 12, then Vulkan, then\n"
                          "Direct3D 11. OpenGL is Dawn's desktop GL (4.4 or newer). Takes effect after a\n"
                          "restart. If the driver can't start the chosen API, this run falls back to Auto\n"
                          "and the setting is kept, so it can be changed here.\n"
                          "MP2_GRAPHICS_BACKEND=auto|d3d12|vulkan|d3d11|opengl overrides it for one run.");
#else
    ImGui::SetItemTooltip("Which graphics API to start on. Auto uses Vulkan. OpenGL (4.4 or newer) is for\n"
                          "GPUs whose Vulkan driver is missing or draws wrong. Takes effect after a restart.\n"
                          "If the driver can't start the chosen API, this run falls back to Auto and the\n"
                          "setting is kept. MP2_GRAPHICS_BACKEND=auto|vulkan|opengl overrides it for one run.");
#endif
    // What this run asked for is the resolution of the STARTUP selection, not the setting:
    // MP2_GRAPHICS_BACKEND outranks the file, and changing the combo must not pretend the API this
    // run started on was the one asked for.
    const char* const env = PortGraphicsBackend::EnvRequest();
    const PortGraphicsBackend::Request request = PortGraphicsBackend::Resolve(sGraphicsBackendAtStart, env);
    const AuroraBackend live = aurora_get_backend();
    const PortGraphicsBackend::Status status =
        PortGraphicsBackend::Describe(sGraphicsBackend, sGraphicsBackendAtStart, request, live);
    const char* const requested = PortGraphicsBackend::DisplayName(PortGraphicsBackend::ToAurora(status.requested));
    ImGui::SameLine();
    const char* const restartNote =
        status.restartPending && !status.overridden ? " (restart to apply the new selection)" : "";
    if (status.fellBack) {
      ImGui::TextColored(ThemeWarnColor(), "%s could not start; using %s%s", requested,
                         PortGraphicsBackend::DisplayName(live), restartNote);
    } else if (restartNote[0] != '\0') {
      ImGui::TextColored(ThemeWarnColor(), "Restart to apply");
    } else {
      ImGui::TextDisabled("Using %s", PortGraphicsBackend::DisplayName(live));
    }
    if (status.overridden) {
      ImGui::PushTextWrapPos(0.f);
      ImGui::TextDisabled("MP2_GRAPHICS_BACKEND=%s asks for %s this run, over the saved selection; a change here "
                          "starts applying once it is unset%s.",
                          env, requested, status.restartPending ? ", and the next start uses it then" : "");
      ImGui::PopTextWrapPos();
    }
  }
#endif
  size_t backendCount = 0;
  const AuroraBackend* backends = aurora_get_available_backends(&backendCount);
  if (std::find(backends, backends + backendCount, BACKEND_OPENGLES) != backends + backendCount) {
    Changed(ImGui::Checkbox("Use OpenGL ES", &sOpenGles));
    ImGui::SetItemTooltip("Renders through OpenGL ES instead of Vulkan. Try it if the world draws black\n"
                          "or untextured (some Adreno drivers). Takes effect after a restart; if the\n"
                          "driver crashes starting it, the next start goes back to Vulkan.");
    if (sOpenGles != sOpenGlesAtStart) {
      ImGui::SameLine();
      ImGui::TextColored(ThemeWarnColor(), "Restart to apply");
    } else if (sOpenGles && aurora_get_backend() != BACKEND_OPENGLES) {
      ImGui::SameLine();
      ImGui::TextColored(ThemeWarnColor(), "OpenGL ES failed to start");
    }
  }
  DrawGpuDriver();
  {
    static const char* const kClampNames[] = {"Auto", "Off", "On"};
    int clamp = sStorageClamp + 1;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
    if (ImGui::Combo("Adreno shader fix", &clamp, kClampNames, 3)) {
      sStorageClamp = clamp - 1;
      Changed(true);
    }
    ImGui::SetItemTooltip("Reads GPU buffers without the bounds check some Adreno Vulkan drivers\n"
                          "miscompile (black world). Auto turns it on for Adreno 7xx; try On\n"
                          "on another Adreno with a black world. Restart to apply.");
    if (sStorageClamp != sStorageClampAtStart) {
      ImGui::SameLine();
      ImGui::TextColored(ThemeWarnColor(), "Restart to apply");
    }
  }
  // Aurora reads MP2_GPU_POWER=low when it picks the adapter; MP1 has only the variable.
  Changed(ImGui::Checkbox("Low-power GPU", &sLowPowerGpu));
  ImGui::SetItemTooltip("Asks for the integrated GPU on a machine that also has a discrete one (longer\n"
                        "battery, less heat, lower frame rate). Restart to apply.");
  if (sLowPowerGpu != sLowPowerGpuAtStart) {
    ImGui::SameLine();
    ImGui::TextColored(ThemeWarnColor(), "Restart to apply");
  }
  {
    const int ticked = std::popcount(sDriverWorkarounds);
    char header[64];
    std::snprintf(header, sizeof(header),
                  ticked > 0 ? "Driver workarounds (%d on)###driver-workarounds" : "Driver workarounds###driver-workarounds",
                  ticked);
    const bool open = ImGui::TreeNode(header);
    if (sDriverWorkarounds != sDriverWorkaroundsAtStart) {
      ImGui::SameLine();
      ImGui::TextColored(ThemeWarnColor(), "Restart to apply");
    }
    if (open) {
      ImGui::PushTextWrapPos(0.f);
      ImGui::TextDisabled("For a GPU that draws wrong (flickering or missing geometry). Tick one, "
                          "restart, then run the GPU self-test below; leave them off otherwise. If "
                          "a start with them fails, the next start turns them off.");
      if (PortVideo::DriverWorkaroundsCleared()) {
        ImGui::TextColored(ThemeWarnColor(), "Turned off: the last start with them did not reach the game.");
      }
      if (PortVideo::DriverWorkaroundsEnv() != nullptr) {
        ImGui::TextDisabled("MP2_DRIVER_WORKAROUNDS=%s replaces these boxes this run.",
                            PortVideo::DriverWorkaroundsEnv());
      }
      if (PortVideo::DriverChecksThisRun()) {
        ImGui::TextColored(ThemeWarnColor(), "Driver error checks are on this run (slower); the next start "
                                             "turns them off.");
      }
      ImGui::PopTextWrapPos();
      for (size_t i = 0; i < PortVideo::kDriverWorkaroundCount; ++i) {
        bool on = (sDriverWorkarounds & (1u << i)) != 0;
        if (ImGui::Checkbox(PortVideo::kDriverWorkarounds[i].label, &on)) {
          sDriverWorkarounds = on ? sDriverWorkarounds | (1u << i) : sDriverWorkarounds & ~(1u << i);
          Changed(true);
        }
        ImGui::SetItemTooltip("%s", PortVideo::kDriverWorkarounds[i].tooltip);
      }
      ImGui::TreePop();
    }
  }
  {
    // Run by CGraphics::BeginScene (PortVideo::RunGpuSelfTestIfRequested).
    const bool pending = aurora_gpu_selftest_pending();
    ImGui::BeginDisabled(pending);
    if (ImGui::Button("GPU self-test")) {
      PortVideo::RequestGpuSelfTest();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Renders test patterns offscreen and logs one PASS or FAIL line per feature\n"
                          "(vertices, TEV, textures, blend, depth, EFB copy), with checks that say which\n"
                          "stage fails. For GPU driver bugs such as a black world; attach the log to a report.");
    char summary[160];
    if (pending) {
      ImGui::SameLine();
      ImGui::TextDisabled("running...");
    } else if (aurora_gpu_selftest_summary(summary, sizeof(summary)) != 0) {
      ImGui::SameLine();
      ImGui::TextUnformatted(summary);
    }
  }
}

// Top-centre toasts stack downwards in draw order instead of overlapping: each takes the
// next free y and, once drawn, pushes it below itself. Reset at the start of every frame. From
// MP1's overlays.cpp.
namespace {
float sTopToastNextY = 0.f;
} // namespace

void BeginTopToastStack() { sTopToastNextY = 16.f; }

void PlaceTopToast(const ImGuiViewport* viewport) {
  ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + sTopToastNextY),
                          ImGuiCond_Always, ImVec2(0.5f, 0.f));
}

// Call between Begin and End of a toast placed with PlaceTopToast.
void TopToastPlaced(const ImGuiViewport* viewport) {
  sTopToastNextY = ImGui::GetWindowPos().y - viewport->Pos.y + ImGui::GetWindowSize().y + 8.f;
}

// While the startup compile runs (the pipeline cache, queued before the first frame): after half a
// second, a count and a bar since the toast opened. Once that queue has drained, shaders met in
// game compile without it. From MP1's overlays.cpp, top centre.
void DrawShaderCompilationToast() {
  static double sQueuedSince = -1.0;
  static uint32_t sBase = 0; // createdPipelines when the toast opened
  static bool sStartupDone = false;
  const AuroraStats* stats = aurora_get_stats();
  if (stats != nullptr && stats->queuedPipelines == 0) {
    sStartupDone = true;
  }
  if (!PortOverlay::ShowShaderCompilation() || stats == nullptr || sStartupDone) {
    sQueuedSince = -1.0;
    return;
  }
  const double now = ImGui::GetTime();
  if (sQueuedSince < 0.0) {
    sQueuedSince = now;
    sBase = stats->createdPipelines;
  }
  if (now - sQueuedSince < 0.5) {
    return;
  }
  const uint32_t done = stats->createdPipelines >= sBase ? stats->createdPipelines - sBase : 0;
  const uint32_t total = done + stats->queuedPipelines;
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  PlaceTopToast(viewport);
  ImGui::SetNextWindowBgAlpha(0.7f);
  if (ImGui::Begin("##shader-compilation-toast", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_AlwaysAutoResize)) {
    TopToastPlaced(viewport);
    char label[64];
    std::snprintf(label, sizeof(label), "Compiling shaders %u / %u", total, total); // widest, so the count doesn't jitter
    const float width = ImGui::CalcTextSize(label).x;
    std::snprintf(label, sizeof(label), "Compiling shaders %u / %u", done, total);
    ImGui::TextUnformatted(label);
    ImGui::ProgressBar(total != 0 ? static_cast< float >(done) / static_cast< float >(total) : 0.f,
                       ImVec2(width, std::max(6.f, ImGui::GetFontSize() * 0.4f)), "");
  }
  ImGui::End();
}

// Once per launch, for 8 s, until the window is first opened: how to open it. Top left.
void DrawMenuHintToast() {
  static double sShownAt = -1.0;
  static bool sDone = false;
  if (sDone || !PortOverlay::ShowMenuHint()) {
    return;
  }
  if (sVisible) {
    sDone = true;
    return;
  }
  const double now = ImGui::GetTime();
  if (sShownAt < 0.0) {
    sShownAt = now;
  }
  const double age = now - sShownAt;
  if (age > 8.0) {
    sDone = true;
    return;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 16.f, viewport->Pos.y + 16.f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.7f);
  // Fades out over the last second.
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, static_cast< float >(std::clamp(8.0 - age, 0.0, 1.0)));
  if (ImGui::Begin("##menu-hint-toast", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_AlwaysAutoResize)) {
    const std::string keys = PortInputRemap::BindingsText(PortInput::Action::PortMenu);
    if (keys.empty()) {
      ImGui::TextUnformatted("Open settings from the Port menu binding in Remap");
    } else {
      ImGui::Text("Press %s for settings", keys.c_str());
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
}

// Once per launch, for 12 s: a newer release than this build is out (MP1's DrawUpdateToast).
// Taps arrive on Android's UI thread (TouchControlsView), so the rect is shared under a lock and
// a hit only sets a flag; the page opens on the game thread.
namespace {
bool sUpdateToastDone = false;
std::mutex sUpdateToastMutex;
bool sUpdateToastShowing = false;
ImVec4 sUpdateToastRect; // last drawn: x0, y0, x1, y1 as fractions of the window
std::atomic< bool > sUpdateToastTapped{false};

void SetUpdateToastShowing(bool showing, const ImVec4& rect = ImVec4()) {
  std::lock_guard lock(sUpdateToastMutex);
  sUpdateToastShowing = showing;
  sUpdateToastRect = rect;
}

void OpenUpdateToastRelease() {
  SDL_OpenURL(PortUpdateCheck::Latest().url.c_str());
  sUpdateToastDone = true;
  SetUpdateToastShowing(false);
}
} // namespace

void DrawUpdateToast() {
  static double sShownAt = -1.0;
  SetUpdateToastShowing(false);
  if (sUpdateToastDone || PortUpdateCheck::Status() != PortUpdateCheck::kStatus_Available) {
    return;
  }
  if (sUpdateToastTapped.exchange(false)) {
    OpenUpdateToastRelease();
    return;
  }
  const double now = ImGui::GetTime();
  if (sShownAt < 0.0) {
    sShownAt = now;
  }
  if (now - sShownAt > 12.0) {
    sUpdateToastDone = true;
    return;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  PlaceTopToast(viewport);
  ImGui::SetNextWindowSize(ImVec2(std::min(viewport->Size.x - 32.f, 560.f), 0.f));
  ImGui::SetNextWindowBgAlpha(0.8f);
  if (ImGui::Begin("##update-toast", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::TextWrapped("Version %s is out (this is %s). Click or tap here to open its release page.",
                       PortUpdateCheck::Latest().version.c_str(), MP_BUILD_VERSION);
    TopToastPlaced(viewport);
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    if (viewport->Size.x > 0.f && viewport->Size.y > 0.f) {
      SetUpdateToastShowing(true, ImVec4((pos.x - viewport->Pos.x) / viewport->Size.x,
                                         (pos.y - viewport->Pos.y) / viewport->Size.y,
                                         (pos.x + size.x - viewport->Pos.x) / viewport->Size.x,
                                         (pos.y + size.y - viewport->Pos.y) / viewport->Size.y));
    }
    // A mouse click; taps go through PortDebug::TapUpdateToast.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      OpenUpdateToastRelease();
    }
  }
  ImGui::End();
}

bool TapUpdateToast(float x, float y) {
  std::lock_guard lock(sUpdateToastMutex);
  const ImVec4& r = sUpdateToastRect;
  if (!sUpdateToastShowing || x < r.x || x > r.z || y < r.y || y > r.w) {
    return false;
  }
  sUpdateToastTapped.store(true);
  return true;
}

} // namespace PortDebug
