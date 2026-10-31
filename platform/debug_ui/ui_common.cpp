// F1 overlay: the window's style, theme and scale, controller navigation, the row helpers
// (ItemHelp) and the menu sounds. The Prime 1 port's debug_ui/ui_common.cpp, with its Prime and
// Plain themes (no Remastered) and its menu sounds, without its GPU self-test. State and helpers
// shared with the other parts are in debug_ui_internal.h.
#include "port_mobile.h"
#include "debug_ui_internal.h"

#include "port_env.h"
#include "port_input_remap_ui.h"

#include "Kyoto/Audio/CSfxManager.hpp"
#include "MetroidPrime/SFX/UI.h"
#include "MetroidPrime/SFX/UI3.h"
#include "MetroidPrime/SFX/UIMemory.h"

#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace PortDebug {
namespace {

struct Rgb {
  float x, y, z;
};

struct Palette {
  Rgb bg;
  float bgAlpha;
  Rgb deep;     // child / title / menu background
  Rgb frame;    // frames, buttons, tabs
  Rgb frameHi;  // hovered frames
  Rgb accent;
  Rgb text;
  Rgb textDim;
};

ImVec4 Rgba(const Rgb& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

// Every ImGuiCol_ is derived from the palette so none is left at the stock value.
void FillThemeColors(ImGuiStyle& style, const Palette& p) {
  ImVec4* c = style.Colors;
  const Rgb a = p.accent;
  c[ImGuiCol_Text] = Rgba(p.text, 1.f);
  c[ImGuiCol_TextDisabled] = Rgba(p.textDim, 1.f);
  c[ImGuiCol_WindowBg] = Rgba(p.bg, p.bgAlpha);
  c[ImGuiCol_ChildBg] = Rgba(p.deep, 0.55f);
  c[ImGuiCol_PopupBg] = Rgba(p.bg, 0.97f);
  c[ImGuiCol_Border] = Rgba(a, 0.55f);
  c[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.f);
  c[ImGuiCol_FrameBg] = Rgba(p.frame, 0.90f);
  c[ImGuiCol_FrameBgHovered] = Rgba(p.frameHi, 0.95f);
  c[ImGuiCol_FrameBgActive] = Rgba(a, 0.35f);
  c[ImGuiCol_TitleBg] = Rgba(p.deep, 1.f);
  c[ImGuiCol_TitleBgActive] = Rgba(p.frame, 1.f);
  c[ImGuiCol_TitleBgCollapsed] = Rgba(p.deep, 0.75f);
  c[ImGuiCol_MenuBarBg] = Rgba(p.deep, 1.f);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0.f, 0.f, 0.f, 0.30f);
  c[ImGuiCol_ScrollbarGrab] = Rgba(p.frameHi, 1.f);
  c[ImGuiCol_ScrollbarGrabHovered] = Rgba(a, 0.60f);
  c[ImGuiCol_ScrollbarGrabActive] = Rgba(a, 0.90f);
  c[ImGuiCol_CheckMark] = Rgba(a, 1.f);
  c[ImGuiCol_SliderGrab] = Rgba(a, 0.85f);
  c[ImGuiCol_SliderGrabActive] = Rgba(a, 1.f);
  c[ImGuiCol_Button] = Rgba(p.frame, 0.90f);
  c[ImGuiCol_ButtonHovered] = Rgba(p.frameHi, 1.f);
  c[ImGuiCol_ButtonActive] = Rgba(a, 0.55f);
  c[ImGuiCol_Header] = Rgba(p.frame, 0.80f);
  c[ImGuiCol_HeaderHovered] = Rgba(a, 0.35f);
  c[ImGuiCol_HeaderActive] = Rgba(a, 0.55f);
  c[ImGuiCol_Separator] = Rgba(a, 0.35f);
  c[ImGuiCol_SeparatorHovered] = Rgba(a, 0.70f);
  c[ImGuiCol_SeparatorActive] = Rgba(a, 1.f);
  c[ImGuiCol_ResizeGrip] = Rgba(a, 0.30f);
  c[ImGuiCol_ResizeGripHovered] = Rgba(a, 0.65f);
  c[ImGuiCol_ResizeGripActive] = Rgba(a, 0.90f);
  c[ImGuiCol_TabHovered] = Rgba(a, 0.50f);
  c[ImGuiCol_Tab] = Rgba(p.frame, 0.85f);
  c[ImGuiCol_TabSelected] = Rgba(a, 0.40f);
  c[ImGuiCol_TabSelectedOverline] = Rgba(a, 1.f);
  c[ImGuiCol_TabDimmed] = Rgba(p.deep, 0.90f);
  c[ImGuiCol_TabDimmedSelected] = Rgba(a, 0.25f);
#ifdef IMGUI_HAS_DOCK
  c[ImGuiCol_TabDimmedSelectedOverline] = Rgba(a, 0.55f);
  c[ImGuiCol_DockingPreview] = Rgba(a, 0.45f);
  c[ImGuiCol_DockingEmptyBg] = Rgba(p.deep, 1.f);
#endif
  c[ImGuiCol_PlotLines] = Rgba(a, 0.90f);
  c[ImGuiCol_PlotLinesHovered] = Rgba(p.text, 1.f);
  c[ImGuiCol_PlotHistogram] = Rgba(a, 0.80f);
  c[ImGuiCol_PlotHistogramHovered] = Rgba(p.text, 1.f);
  c[ImGuiCol_TableHeaderBg] = Rgba(p.frame, 1.f);
  c[ImGuiCol_TableBorderStrong] = Rgba(a, 0.50f);
  c[ImGuiCol_TableBorderLight] = Rgba(a, 0.20f);
  c[ImGuiCol_TableRowBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1.f, 1.f, 1.f, 0.035f);
  c[ImGuiCol_TextLink] = Rgba(a, 1.f);
  c[ImGuiCol_TextSelectedBg] = Rgba(a, 0.35f);
  c[ImGuiCol_DragDropTarget] = Rgba(a, 0.90f);
  c[ImGuiCol_NavCursor] = Rgba(a, 1.f);
  c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.f, 1.f, 1.f, 0.70f);
  c[ImGuiCol_NavWindowingDimBg] = Rgba(p.bg, 0.55f);
  c[ImGuiCol_ModalWindowDimBg] = Rgba(p.bg, 0.60f);
}

// Paddings and sizes, unscaled.
void ApplyTouchStyle(ImGuiStyle& style) {
  style.WindowPadding = ImVec2(10.f, 10.f);
  style.FramePadding = ImVec2(10.f, 7.f);
  style.ItemSpacing = ImVec2(10.f, 8.f);
  style.ItemInnerSpacing = ImVec2(8.f, 6.f);
  style.CellPadding = ImVec2(6.f, 4.f);
  style.TouchExtraPadding = ImVec2(3.f, 3.f);
  style.IndentSpacing = 22.f;
  style.ScrollbarSize = 18.f;
  style.GrabMinSize = 18.f;
  style.FrameRounding = 5.f;
  style.GrabRounding = 5.f;
  style.ScrollbarRounding = 9.f;
  style.TabRounding = 5.f;
}

// Retail pause / scan visor: near-black blue, teal frames, amber accent. Borders and rounding in
// unscaled units times the applied UI scale (ScaleAllSizes has already run for the paddings).
// sUiAppliedScale is the scale UpdateUiScale has applied to the style so far, sThemeAppliedScale
// the one the theme's borders and rounding were last set for.
} // namespace
float sUiAppliedScale = 1.f;
namespace {
float sThemeAppliedScale = 0.f;
enum UiThemeId { kThemeAuto = 0, kThemePrime = 1, kThemePlain = 2, kThemeEchoes = 3 };
int sThemeApplied = -1;

int ResolveTheme() {
  if (sUiTheme != kThemeAuto) {
    return sUiTheme;
  }
  return kThemeEchoes;
}

// Only colours, borders and rounding; paddings and sizes stay ApplyTouchStyle's.
void ApplyPrimeTheme(ImGuiStyle& style, int theme) {
  const float scale = sUiAppliedScale;
  if (theme == kThemePlain) {
    ImGui::StyleColorsDark(&style);
    style.WindowBorderSize = style.ChildBorderSize = style.PopupBorderSize = std::round(scale);
    style.FrameBorderSize = 0.f;
    style.TabBorderSize = 0.f;
    style.WindowRounding = style.ChildRounding = style.PopupRounding = 0.f;
    // ApplyTouchStyle's values, which a switch from another theme must restore.
    style.FrameRounding = style.GrabRounding = style.TabRounding = 5.f * scale;
    style.ScrollbarRounding = 9.f * scale;
    return;
  }
  Palette p;
  if (theme == kThemeEchoes) {
    // Echoes / Dark Aether: black panels, white borders, violet accent, lavender-white text.
    p.bg = {0.012f, 0.010f, 0.020f};  // #030305
    p.bgAlpha = 0.95f;
    p.deep = {0.035f, 0.028f, 0.060f};
    p.frame = {0.085f, 0.065f, 0.150f};
    p.frameHi = {0.170f, 0.125f, 0.300f};
    p.accent = {0.690f, 0.520f, 1.000f};  // #B085FF
    p.text = {0.95f, 0.94f, 1.00f};
    p.textDim = {0.58f, 0.55f, 0.72f};
    FillThemeColors(style, p);
    ImVec4* c = style.Colors;
    c[ImGuiCol_Border] = ImVec4(1.f, 1.f, 1.f, 0.80f);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(1.f, 1.f, 1.f, 1.f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(1.f, 1.f, 1.f, 0.50f);
    c[ImGuiCol_NavCursor] = ImVec4(1.f, 1.f, 1.f, 1.f);
    c[ImGuiCol_Header] = Rgba(p.accent, 0.30f); // the selected page stands out from the black
    c[ImGuiCol_Separator] = Rgba(p.accent, 0.55f);
    style.WindowBorderSize = style.ChildBorderSize = style.PopupBorderSize = style.FrameBorderSize =
        std::max(1.f, std::round(scale));
    style.TabBorderSize = 0.f;
    style.WindowRounding = style.ChildRounding = style.PopupRounding = 0.f;
    style.FrameRounding = style.GrabRounding = style.TabRounding = 0.f;
    style.ScrollbarRounding = 0.f;
    return;
  }
  p.bg = {0.020f, 0.063f, 0.102f};  // #05101A
  p.bgAlpha = 0.92f;
  p.deep = {0.030f, 0.090f, 0.140f};
  p.frame = {0.045f, 0.170f, 0.230f};
  p.frameHi = {0.070f, 0.260f, 0.340f};
  p.accent = {1.000f, 0.604f, 0.180f};  // #FF9A2E
  p.text = {0.95f, 0.91f, 0.82f};
  p.textDim = {0.45f, 0.55f, 0.65f};
  FillThemeColors(style, p);
  style.WindowBorderSize = style.ChildBorderSize = style.PopupBorderSize = style.FrameBorderSize =
      std::max(1.f, std::round(scale));
  style.TabBorderSize = 0.f;
  style.WindowRounding = style.ChildRounding = style.PopupRounding = 2.f * scale;
  style.FrameRounding = style.GrabRounding = style.TabRounding = 2.f * scale;
  style.ScrollbarRounding = 2.f * scale;
}

constexpr bool kThemed = true;

} // namespace

SDL_Window* MainWindow() {
  static SDL_Window* sWindow = nullptr;
  if (sWindow == nullptr) {
    int windowCount = 0;
    if (SDL_Window** windows = SDL_GetWindows(&windowCount)) {
      if (windowCount > 0) {
        sWindow = windows[0];
      }
      SDL_free(windows);
    }
  }
  return sWindow;
}

// Phones report a density of roughly 3, which leaves ImGui's default 13px font
// unreadably small, so scale the overlay to match the display. The scale is not
// known on the first frame, so keep watching for it instead of latching once.
void UpdateUiScale(float userScale) {
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  static bool sInitialized = false;
  static ImGuiStyle sBase;     // the style before any scaling
  static float sBaseFont = 1.f; // io.FontGlobalScale before any scaling
  if (!sInitialized) {
    sInitialized = true;
    // The overlay has to size itself to the scaled font, so do not restore a
    // window size remembered from a previous, smaller run.
    ImGui::GetIO().IniFilename = nullptr;
    // Before any scaling, so the scaling applies to these sizes too. The desktop keeps ImGui's
    // own paddings (its look is the window's original one).
    if (kThemed) {
      ApplyTouchStyle(ImGui::GetStyle());
    }
    sBase = ImGui::GetStyle();
    sBaseFont = ImGui::GetIO().FontGlobalScale;
  }
  SDL_Window* window = MainWindow();
  if (window == nullptr) {
    return;
  }
  float displayScale = SDL_GetWindowDisplayScale(window);
  if (!(displayScale > 0.f)) {
    displayScale = 1.f;
  }
#if defined(PORT_MOBILE)
  // 3 made the nine page rows taller than the list and Bindings sat below the fold; 2 keeps the
  // page list on screen and still thumb-sized.
  displayScale = std::min(displayScale, 2.f);
#endif
  const float uiScale = std::clamp(displayScale * userScale, 1.f, 4.f);
  if (uiScale == sUiAppliedScale) {
    return;
  }
  sUiAppliedScale = uiScale;
  // Rebuilt from the unscaled values, so a changed scale never compounds.
  ImGuiStyle& style = ImGui::GetStyle();
  style = sBase;
  style.ScaleAllSizes(uiScale);
  ImGui::GetIO().FontGlobalScale = sBaseFont * uiScale;
  sThemeAppliedScale = 0.f; // the rebuilt style holds the base colours again
}

// Called every frame: restyles only when the UI scale changed.
void ApplyThemeIfChanged() {
  if (!kThemed || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  const int resolved = ResolveTheme();
  if (resolved == sThemeApplied && sUiAppliedScale == sThemeAppliedScale) {
    return;
  }
  sThemeApplied = resolved;
  sThemeAppliedScale = sUiAppliedScale;
  ApplyPrimeTheme(ImGui::GetStyle(), resolved);
}

// The overlay is a full-screen panel with a page list instead of tabs. The desktop can go back to
// the old floating tabbed window (`overlay_windowed`); MP_TOUCH_UI forces the page layout.
bool PageLayout() {
#if defined(PORT_MOBILE)
  return true;
#else
  static const bool sForced = port::EnvFlag("MP_TOUCH_UI");
  return sForced || !sOverlayWindowed;
#endif
}

void UpdateControllerNav() {
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
#if defined(PORT_TOUCH_OVERLAY)
  InstallTouchOverlay(); // idempotent; the SDL event filter that feeds the touch controls
#endif
  ImGuiIO& io = ImGui::GetIO();
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  // The SDL3 backend calls SDL_ShowCursor on every NewFrame unless this flag is set, and the port
  // hides the cursor again while aiming (PortMouse::SetCapture), so the pointer flickered wherever
  // relative mouse mode was not already hiding it: menus, cutscenes, Android. Let the backend own
  // the cursor only while the overlay is open.
  if (sVisible) {
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
  } else {
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  }
  // ImGui applies one change per key and frame and holds everything queued behind a second one.
  // Nothing should queue faster than that; if something does, take the queue in one go.
  io.ConfigInputTrickleEventQueue = ImGui::GetCurrentContext()->InputEventsQueue.Size <= 64;

  // While the Controls page captures a pad input, the pad binds instead of navigating (releasing
  // everything here also ends any nav press in progress); a pad that went away releases it too.
  // Navigate with the player's pad (port 0), which can move to a pad connected mid-game.
  const s32 portIndex = PADGetIndexForPort(PAD_CHAN0);
  const u32 padIndex = portIndex >= 0 ? static_cast< u32 >(portIndex) : 0;
  SDL_Gamepad* pad = PADGetSDLGamepadForIndex(padIndex);
  const bool capturing = pad == nullptr || PortInputRemap::Capturing();
  const auto held = [pad, capturing](SDL_GamepadButton button) {
    return !capturing && SDL_GetGamepadButton(pad, button);
  };
  const auto axis = [pad, capturing](SDL_GamepadAxis a) {
    return capturing ? Sint16{0} : SDL_GetGamepadAxis(pad, a);
  };
  constexpr Sint16 kStickThreshold = 16000;
  io.AddKeyEvent(ImGuiKey_GamepadDpadUp,
                 held(SDL_GAMEPAD_BUTTON_DPAD_UP) || axis(SDL_GAMEPAD_AXIS_LEFTY) < -kStickThreshold);
  io.AddKeyEvent(ImGuiKey_GamepadDpadDown,
                 held(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || axis(SDL_GAMEPAD_AXIS_LEFTY) > kStickThreshold);
  io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,
                 held(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || axis(SDL_GAMEPAD_AXIS_LEFTX) < -kStickThreshold);
  io.AddKeyEvent(ImGuiKey_GamepadDpadRight,
                 held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || axis(SDL_GAMEPAD_AXIS_LEFTX) > kStickThreshold);
  // ImGui activates with FaceDown and cancels with FaceRight. On a Nintendo pad (A on the right)
  // confirm on A.
  const PADControllerType padType = pad != nullptr ? PADGetControllerTypeForIndex(padIndex) : PAD_TYPE_UNKNOWN;
  const bool nintendo = padType == PAD_TYPE_SWITCH_PROCON || padType == PAD_TYPE_JOYCON_PAIR;
  io.AddKeyEvent(ImGuiKey_GamepadFaceDown, held(nintendo ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH));
  io.AddKeyEvent(ImGuiKey_GamepadFaceRight, held(nintendo ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST));
  io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, held(nintendo ? SDL_GAMEPAD_BUTTON_NORTH : SDL_GAMEPAD_BUTTON_WEST));
  io.AddKeyEvent(ImGuiKey_GamepadFaceUp, held(nintendo ? SDL_GAMEPAD_BUTTON_WEST : SDL_GAMEPAD_BUTTON_NORTH));
  io.AddKeyEvent(ImGuiKey_GamepadL1, held(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
  io.AddKeyEvent(ImGuiKey_GamepadR1, held(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
}

ImVec4 ThemeWarnColor(const ImVec4& plain) {
  return ResolveTheme() != kThemePlain ? ImVec4(1.f, 0.88f, 0.32f, 1.f) : plain; // yellow, apart from the amber accent
}

ImVec4 ThemeGoodColor(const ImVec4& plain) {
  return ResolveTheme() != kThemePlain ? ImVec4(0.45f, 0.95f, 0.55f, 1.f) : plain;
}

ImVec4 ThemeBadColor(const ImVec4& plain) {
  return ResolveTheme() != kThemePlain ? ImVec4(1.f, 0.42f, 0.35f, 1.f) : plain;
}

// What a setting does (kInlineHelp: printed under it on Android, a tooltip elsewhere).
void ItemHelp(const char* text) {
  if (kInlineHelp) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
  } else if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

// SameLine, except after an ItemHelp that printed its text on Android.
void SameLineAfterHelp() {
  if (!kInlineHelp) {
    ImGui::SameLine();
  }
}

// Menu sounds, MP1's debug_ui.cpp: the game's own UI SFX, so the overlay sounds like the pause
// screen it imitates. ImGui's state is read once a frame instead of every widget being hooked:
//   open  -> SFXui_x_pause_00   (the pause screen opening)
//   close -> SFXui_x_pause_01   (the same screen closing)
//   click / activate (button, checkbox, combo, tab, selectable) -> SFXui_x_invchoos_00 (choose)
//   keyboard/pad focus move  -> SFXui_x_invscrol_00 (scroll between inventory items)
//   back (Esc, pad B)        -> SFXui_x_invback_00
// The overlay holds the simulation, not the audio, so these play with F1 up. Nothing plays before
// a world is up (the sfx manager is the game's) or with the Plain theme, as in MP1.
namespace {

enum MenuSound { kMenuOpen, kMenuClose, kMenuChoose, kMenuMove, kMenuBack };

// `always` skips the rate limit: opening and closing are rare and must not be swallowed by the
// press that caused them.
void PlayMenuSound(MenuSound sound, bool always = false) {
  if (!sUiSounds || ResolveTheme() == kThemePlain || sWorldId == 0) {
    return;
  }
  static uint64_t sLastMs = 0;
  const uint64_t now = SDL_GetTicks();
  if (!always && sLastMs != 0 && now - sLastMs < 50) {
    return;
  }
  sLastMs = now;
  ushort id = 0;
  switch (sound) {
  case kMenuOpen:
    id = SFXui_x_pause_00_oneshot;
    break;
  case kMenuClose:
    id = SFXui_x_pause_01_oneshot;
    break;
  case kMenuChoose:
    id = SFXui_x_invchoos_00_oneshot;
    break;
  case kMenuMove:
    id = SFXui_x_invscrol_00_oneshot;
    break;
  case kMenuBack:
    id = SFXui_x_invback_00_oneshot;
    break;
  }
  CSfxManager::SfxStart(id, 127, 64, CSfxManager::kAllAreas, false, false,
                        CSfxManager::kMedPriority);
}

} // namespace

// Derives the sounds from ImGui's own state once a frame: a new active item is a press (a held
// slider is one press, not one per frame), and a nav focus change with the nav cursor showing is a
// keyboard or pad move (mouse hover never moves nav focus or shows the cursor). `visible` is
// whether the overlay is open this frame; called at the end of DrawUI.
void UpdateMenuSounds(bool visible) {
  static bool sWasVisible = false;
  static ImGuiID sPrevActive = 0;
  static ImGuiID sPrevNav = 0;
  if (visible != sWasVisible) {
    sWasVisible = visible;
    sPrevActive = sPrevNav = 0;
    // A back press that closes the overlay is the close, not a second sound: this branch returns
    // before the back check below, and the close ignores the rate limit.
    PlayMenuSound(visible ? kMenuOpen : kMenuClose, true);
    return;
  }
  if (!visible || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  ImGuiContext& g = *ImGui::GetCurrentContext();
  const ImGuiID active = g.ActiveId;
  const ImGuiID nav = g.NavId;
  bool pressed = active != 0 && active != sPrevActive;
  if (pressed && g.ActiveIdWindow != nullptr) {
    // Dragging a window by its title, or a scrollbar, is not a choice.
    ImGuiWindow* window = g.ActiveIdWindow;
    pressed = active != window->MoveId &&
              active != ImGui::GetWindowScrollbarID(window, ImGuiAxis_X) &&
              active != ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y);
  }
  const bool moved = nav != 0 && nav != sPrevNav && g.NavCursorVisible && active == 0;
  const bool back = ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                    ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
  sPrevActive = active;
  sPrevNav = nav;
  if (back) {
    PlayMenuSound(kMenuBack);
  } else if (pressed) {
    PlayMenuSound(kMenuChoose);
  } else if (moved) {
    PlayMenuSound(kMenuMove);
  }
}

} // namespace PortDebug
