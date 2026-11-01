// F1 overlay: Touch scrolling, the page and desktop windows, and DrawUI. State and helpers shared
// with the other parts are in debug_ui_internal.h.

#include "port_mobile.h"
#include "debug_ui_internal.h"

#include "port_build_info.h"
#include "port_env.h"
#include "port_input_remap_ui.h"
#include "port_mouse.h"
#include "port_overlay.h"
#include "port_prompts.h"
#include "port_savestate.h"

#include <SDL3/SDL.h>
#include <aurora/imgui.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <ctime>
#include <map>
#include <mutex>
#include <string>

namespace PortDebug {

namespace {

bool IsResizeGrip(ImGuiWindow* window, ImGuiID id) {
  for (int n = 0; n < 4; ++n) {
    if (id == ImGui::GetWindowResizeCornerID(window, n) ||
        id == ImGui::GetWindowResizeBorderID(window, static_cast< ImGuiDir >(n))) {
      return true;
    }
  }
  return false;
}

// ImGui has no touch scrolling: a finger dragged down a page presses whatever
// it landed on and scrolls nothing. A mostly vertical drag is taken away from
// the widget it began on (so a button under it does not fire on release) and
// scrolls the window instead, and the finger's speed carries on as a fling
// after it lifts. A mostly horizontal drag is left alone, so sliders still
// work, and so are the scrollbar, a window being moved or resized, and drags
// that start outside the content area. Runs after NewFrame, before any window.
struct TouchScroll {
  ImGuiWindow* window = nullptr;
  bool decided = false;
  bool dragging = false;
  float velocity = 0.f; // pixels per second, positive scrolls down
};
TouchScroll sTouchScroll;

void UpdateTouchScroll() {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  const ImGuiIO& io = g.IO;
  TouchScroll& scroll = sTouchScroll;
  const bool touch = io.MouseSource == ImGuiMouseSource_TouchScreen;

  if (io.MouseClicked[0]) {
    scroll = TouchScroll{};
    if (touch) {
      ImGuiWindow* window = ScrollableWindowAt(g.HoveredWindow);
      if (window != nullptr && window->InnerClipRect.Contains(io.MouseClickedPos[0])) {
        scroll.window = window;
      }
    }
  }
  if (scroll.window == nullptr) {
    return;
  }

  if (io.MouseDown[0]) {
    if (!scroll.decided) {
      const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.f);
      // A share of the font size rather than io.MouseDragThreshold's fixed
      // pixels, which on a dense phone screen is a jitter, not a drag.
      const float threshold = g.FontSize * 0.6f;
      if (delta.x * delta.x + delta.y * delta.y < threshold * threshold) {
        return;
      }
      scroll.decided = true;
      ImGuiWindow* activeWindow = g.ActiveIdWindow;
      const bool ownDrag =
          g.MovingWindow != nullptr ||
          (g.ActiveId != 0 && activeWindow != nullptr &&
           (g.ActiveId == ImGui::GetWindowScrollbarID(activeWindow, ImGuiAxis_X) ||
            g.ActiveId == ImGui::GetWindowScrollbarID(activeWindow, ImGuiAxis_Y) ||
            IsResizeGrip(activeWindow, g.ActiveId)));
      if (std::fabs(delta.y) <= std::fabs(delta.x) || ownDrag) {
        scroll.window = nullptr;
        return;
      }
      scroll.dragging = true;
    }
    if (!scroll.dragging) {
      return;
    }
    if (g.ActiveId != 0) {
      ImGui::ClearActiveID();
    }
    ImGui::SetScrollY(scroll.window, scroll.window->Scroll.y - io.MouseDelta.y);
    if (io.DeltaTime > 0.f) {
      // Smoothed, so the last jittery frame before the finger lifts does not
      // decide the whole fling.
      const float instant = -io.MouseDelta.y / io.DeltaTime;
      scroll.velocity += (instant - scroll.velocity) * 0.4f;
    }
    return;
  }

  // Released: keep scrolling at the finger's speed, easing off.
  if (!scroll.dragging) {
    scroll.window = nullptr;
    return;
  }
  const float y = scroll.window->Scroll.y;
  const bool atEdge = (scroll.velocity < 0.f && y <= 0.f) ||
                      (scroll.velocity > 0.f && y >= scroll.window->ScrollMax.y);
  if (atEdge || std::fabs(scroll.velocity) < g.FontSize) {
    scroll = TouchScroll{};
    return;
  }
  ImGui::SetScrollY(scroll.window, y + scroll.velocity * io.DeltaTime);
  scroll.velocity *= std::exp(-4.f * io.DeltaTime);
}

// A finger that lifts leaves ImGui's cursor where it was, so whatever was last
// tapped stays drawn as hovered. Move the cursor off-screen once the release
// has been seen; queued now, it lands on the next frame.
void ClearTouchHover() {
  ImGuiIO& io = ImGui::GetIO();
  if (io.MouseSource == ImGuiMouseSource_TouchScreen && io.MouseReleased[0] &&
      !ImGui::IsAnyMouseDown()) {
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  }
}

// SDL's safe area, except that on Android the top edge only keeps clear of the cutout and a visible
// status bar. SDL's also avoids the swipe-down band, which Android reports even with the bars
// hidden, leaving a wide empty strip; the back-gesture edges at the sides stay avoided.
static bool DrawArea(SDL_Window* window, SDL_Rect* area) {
  if (!SDL_GetWindowSafeArea(window, area))
    return false;
#if defined(__ANDROID__)
  int left, top, right, bottom;
  int h = 0, ph = 0;
  if (DrawInsetsPx(left, top, right, bottom) && SDL_GetWindowSize(window, nullptr, &h) &&
      SDL_GetWindowSizeInPixels(window, nullptr, &ph) && h > 0 && ph > 0) {
    const int drawTop = static_cast< int >(top * static_cast< float >(h) / static_cast< float >(ph));
    if (drawTop < area->y) {
      area->h += area->y - drawTop;
      area->y = drawTop;
    }
  }
#endif
  return true;
}

// Full screen inside the display's drawable area (clear of the notch and any bar on screen), no
// title bar to drag, a Close button a thumb can hit, and a page list down the side in place of a
// tab strip too narrow to tap.
bool DrawPageWindow() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 pos = viewport->WorkPos;
  ImVec2 size = viewport->WorkSize;
  SDL_Window* window = MainWindow();
  SDL_Rect safe;
  int windowWidth = 0, windowHeight = 0;
  if (window != nullptr && DrawArea(window, &safe) &&
      SDL_GetWindowSize(window, &windowWidth, &windowHeight) && windowWidth > 0 &&
      windowHeight > 0 && safe.w > 0 && safe.h > 0) {
    // ImGui's display size need not be in window coordinates.
    const float sx = size.x / static_cast< float >(windowWidth);
    const float sy = size.y / static_cast< float >(windowHeight);
    pos = ImVec2(pos.x + safe.x * sx, pos.y + safe.y * sy);
    size = ImVec2(safe.w * sx, safe.h * sy);
  }
  ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse;
  bool open = true;
  if (ImGui::Begin("Metroid Prime 2 Port##touch", nullptr, kFlags)) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* const kClose = "Close";
    const float closeWidth = ImGui::CalcTextSize(kClose).x + style.FramePadding.x * 4.f;
    const char* const kTitle = "Metroid Prime 2 Port";
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(kTitle);
    const char* const kBuild = MP_BUILD_REVISION;
    const float buildWidth = ImGui::CalcTextSize(kBuild).x;
    if (ImGui::GetContentRegionAvail().x >
        ImGui::CalcTextSize(kTitle).x + buildWidth + closeWidth + style.ItemSpacing.x * 5.f) {
      ImGui::SameLine(0.f, style.ItemSpacing.x * 2.f);
      ImGui::TextDisabled("%s", kBuild);
    }
#if !defined(PORT_MOBILE)
    // Only when it fits beside the title, the build and the Close button.
    const std::string keyHint = HotkeyHint();
    const char* const kKeys = keyHint.c_str();
    if (ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize(kTitle).x + buildWidth +
                                               ImGui::CalcTextSize(kKeys).x + closeWidth +
                                               style.ItemSpacing.x * 8.f) {
      ImGui::SameLine(0.f, style.ItemSpacing.x * 3.f);
      ImGui::TextDisabled("%s", kKeys);
    }
#endif
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - closeWidth);
    if (ImGui::Button(kClose, ImVec2(closeWidth, 0.f))) {
      open = false;
    }
    ImGui::Separator();
    DrawSearchField();

    static int sPage = 0;
    if (const int jumpPage = TakeJumpPage(); jumpPage >= 0) {
      sPage = jumpPage;
    }
    // MP2_DEBUG_TAB=<name> opens on that page, for captures of the overlay.
    static bool sStartPending = true;
    if (sStartPending) {
      for (int i = 0; i < kDebugPageCount; ++i) {
        if (SDL_strcasecmp(GetStartTarget().page.c_str(), kDebugPages[i].name) == 0) {
          sPage = i;
        }
      }
      sStartPending = false;
    }
    float listWidth = 0.f;
    for (int i = 0; i < kDebugPageCount; ++i) {
      listWidth = std::max(listWidth, ImGui::CalcTextSize(kDebugPages[i].name).x);
    }
    listWidth += style.FramePadding.x * 2.f + style.WindowPadding.x * 2.f;
    const float rowHeight = ImGui::GetFrameHeight() * 1.2f;
    if (ImGui::BeginChild("##pages", ImVec2(listWidth, 0.f), ImGuiChildFlags_Borders)) {
      for (int i = 0; i < kDebugPageCount; ++i) {
        if (ImGui::Selectable(kDebugPages[i].name, sPage == i && !Searching(),
                              ImGuiSelectableFlags_None, ImVec2(0.f, rowHeight))) {
          sPage = i;
          sSearch[0] = '\0';
        }
      }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    // Keyed by page, so each page keeps its own scroll position.
    const bool searching = Searching();
    ImGui::PushID(searching ? -1 : sPage);
    if (ImGui::BeginChild("##page", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders)) {
      if (searching) {
        DrawSearchResults();
      } else {
        DrawPageBody(kDebugPages[sPage]);
      }
    }
    ImGui::EndChild();
    ImGui::PopID();
  }
  ImGui::End();
  return open;
}

bool DrawDesktopWindow() {
  ImGui::SetNextWindowPos(ImVec2(8.f, 8.f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(440.f, 200.f), ImGuiCond_FirstUseEver);
  if (port::EnvString("MP2_DEBUG_TAB") != nullptr) // a capture wants to see the tab
    ImGui::SetNextWindowSize(ImVec2(520.f, 620.f), ImGuiCond_Once);
  bool open = true;
  if (ImGui::Begin("Metroid Prime 2 Port", &open, ImGuiWindowFlags_MenuBar)) {
    if (ImGui::BeginMenuBar()) {
      ImGui::TextUnformatted(HotkeyHint().c_str());
      // Only when it fits beside the hotkey hint.
      const ImGuiStyle& style = ImGui::GetStyle();
      if (ImGui::GetContentRegionAvail().x >
          ImGui::CalcTextSize(MP_BUILD_REVISION).x + style.ItemSpacing.x * 3.f) {
        ImGui::SameLine(0.f, style.ItemSpacing.x * 3.f);
        ImGui::TextDisabled("%s", MP_BUILD_REVISION);
      }
      ImGui::EndMenuBar();
    }

    DrawSearchField();
    if (Searching()) {
      if (ImGui::BeginChild("##search-results")) {
        DrawSearchResults();
      }
      ImGui::EndChild();
    } else if (ImGui::BeginTabBar("##debug_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
      // MP2_DEBUG_TAB=<name> opens on that tab, for captures of the overlay.
      static bool sStartPending = true;
      const int jumpPage = TakeJumpPage();
      for (int i = 0; i < kDebugPageCount; ++i) {
        const DebugPage& page = kDebugPages[i];
        const bool start =
            sStartPending && SDL_strcasecmp(GetStartTarget().page.c_str(), page.name) == 0;
        if (ImGui::BeginTabItem(page.name, nullptr,
                                start || i == jumpPage ? ImGuiTabItemFlags_SetSelected : 0)) {
          if (start)
            sStartPending = false;
          DrawPageBody(page);
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();
  return open;
}

} // namespace

// ShowNote's text, set from any thread and timed from the first frame that draws it (MP1's
// overlays.cpp:468-512).
namespace {
std::mutex sNoteMutex;
std::string sNote;
bool sNoteNew = false;
// Set at startup when the last session ended on a failed disc read (main.cpp's marker file).
std::atomic_bool sDiscReadFailedLastSession{false};
} // namespace

void ShowNote(const std::string& text) {
  std::lock_guard<std::mutex> lock(sNoteMutex);
  sNote = text;
  sNoteNew = true;
}

void NoteDiscReadFailedLastSession() { sDiscReadFailedLastSession = true; }

namespace {
void DrawNoteToast() {
  static std::string sText;
  static double sShownAt = -1.0;
  const double now = ImGui::GetTime();
  {
    std::lock_guard<std::mutex> lock(sNoteMutex);
    if (sNoteNew) {
      sText = sNote;
      sNoteNew = false;
      sShownAt = now;
    }
  }
  if (sText.empty() || now - sShownAt > 2.5) {
    return;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  PlaceTopToast(viewport);
  ImGui::SetNextWindowBgAlpha(0.8f);
  if (ImGui::Begin("##note-toast", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(sText.c_str());
    TopToastPlaced(viewport);
  }
  ImGui::End();
}

// Once per launch, for longer and in red: the last session ended on a failed disc read.
void DrawDiscReadFailedAlert() {
  static double sShownAt = -1.0;
  if (!sDiscReadFailedLastSession) {
    return;
  }
  const double now = ImGui::GetTime();
  if (sShownAt < 0.0) {
    sShownAt = now;
  }
  if (now - sShownAt > 20.0) {
    sDiscReadFailedLastSession = false;
    return;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  PlaceTopToast(viewport);
  ImGui::SetNextWindowSize(ImVec2(std::min(viewport->Size.x - 32.f, 640.f), 0.f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.6f, 0.05f, 0.05f, 0.92f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.f, 0.35f, 0.35f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 10.f));
  if (ImGui::Begin("##disc-read-failed-alert", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextWrapped("The last session crashed: the disc image is damaged.");
    ImGui::SetWindowFontScale(1.f);
    ImGui::TextWrapped("Part of it couldn't be read. If the game stops again, copy the image again or check it in "
                       "Dolphin (Properties > Verify).");
    TopToastPlaced(viewport);
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(3);
}
} // namespace

bool TouchScrolling() { return sTouchScroll.dragging; }

std::string HotkeyHint() {
  std::string hint;
  const std::string menu = PortInputRemap::BindingsText(PortInput::Action::PortMenu, " / ");
  if (!menu.empty()) hint += menu + ": hide   ";
  hint += "F10: frame limit";
  const std::string shot = PortInputRemap::BindingsText(PortInput::Action::Screenshot, " / ");
  if (!shot.empty()) hint += "   " + shot + ": screenshot";
  return hint;
}

// The boot's "Resume where you left off?" prompt: a dimmed viewport with a centred window. Yes and
// No call AnswerResumePrompt; the pad and keys answer through the splash (CSplashScreen).
void DrawResumePrompt() {
  PortSaveState::ResumeInfo info;
  bool yes = true;
  if (ImGui::GetCurrentContext() == nullptr || !PortSaveState::ResumePromptShown(info, yes)) {
    return;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float scale = std::max(1.f, ImGui::GetFontSize() / 13.f);
  ImGui::GetBackgroundDrawList()->AddRectFilled(
      viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
      IM_COL32(0, 0, 0, 150));
  const float width = std::min(viewport->Size.x - 24.f * scale, 440.f * scale);
  ImGui::SetNextWindowPos(
      ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f),
      ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(width, 0.f), ImGuiCond_Always);
  if (ImGui::Begin("##resume-prompt", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
    const char* title = info.crashedAgain
                            ? "The game closed unexpectedly again after resuming here.\nResume anyway?"
                        : info.crashedAfterThis ? "The game closed unexpectedly.\nResume where you left off?"
                                                : "Resume where you left off?";
    ImGui::TextWrapped("%s", title);
    ImGui::Spacing();
    char when[32] = "";
    const std::time_t t = static_cast< std::time_t >(info.savedAt);
    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &t) == 0) {
#else
    if (localtime_r(&t, &local) != nullptr) {
#endif
      std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &local);
    }
    ImGui::TextDisabled("%s", info.room.c_str());
    ImGui::TextDisabled("Saved %s", when);
    ImGui::Spacing();
    const float button = (width - ImGui::GetStyle().WindowPadding.x * 2.f -
                          ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const auto answer = [&](const char* label, bool value) {
      const bool chosen = yes == value;
      if (chosen) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
      }
      if (ImGui::Button(label, ImVec2(button, 0.f))) {
        PortSaveState::AnswerResumePrompt(value);
      }
      if (chosen) {
        ImGui::PopStyleColor();
      }
    };
    answer("Yes", true);
    ImGui::SameLine();
    answer("No", false);
  }
  ImGui::End();
}

// After the frame's settings are applied (PortOverlay::Draw): saves a changed setting, feeds the
// pad to ImGui, and draws the window while it is open.
// The save file's play time in the bottom-right corner while a game runs.
void DrawIgtTimer() {
  double seconds = 0.0;
  if (ImGui::GetCurrentContext() == nullptr || !PortOverlay::IgtTimer() ||
      !PortOverlay::GameTime(seconds)) {
    return;
  }
  const long long cs = static_cast< long long >(std::floor(seconds * 100.0));
  char text[32];
  if (cs >= 360000) {
    std::snprintf(text, sizeof(text), "%lld:%02lld:%02lld.%02lld", cs / 360000, cs / 6000 % 60,
                  cs / 100 % 60, cs % 100);
  } else {
    std::snprintf(text, sizeof(text), "%lld:%02lld.%02lld", cs / 6000, cs / 100 % 60, cs % 100);
  }
  ImDrawList* draw = ImGui::GetForegroundDrawList();
  ImFont* font = ImGui::GetFont();
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float size = std::max(ImGui::GetFontSize() * 1.5f, display.y * 0.035f);
  const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.f, text);
  const float margin = size * 0.6f;
  const ImVec2 pos(display.x - extent.x - margin, display.y - extent.y - margin);
  const float shadow = std::max(1.f, size / 12.f);
  draw->AddText(font, size, ImVec2(pos.x + shadow, pos.y + shadow), IM_COL32(0, 0, 0, 200), text);
  draw->AddText(font, size, pos, IM_COL32(255, 255, 255, 230), text);
}

// The A button's icon on the device in use (PortPrompts::ConfirmIconStem), null without one. Each
// icon becomes a texture the first time it is asked for and stays for the run: there are only a
// handful of devices and keys.
ImTextureID SkipConfirmIcon(float& aspect) {
  struct Icon {
    ImTextureID texture{};
    float aspect = 1.f;
  };
  static std::map< std::string, Icon > sIcons;
  const std::string stem = PortPrompts::ConfirmIconStem();
  if (stem.empty()) {
    return {};
  }
  auto it = sIcons.find(stem);
  if (it == sIcons.end()) {
    Icon icon;
    std::vector< uint8_t > rgba;
    uint32_t w = 0, h = 0;
    if (PortPrompts::LoadIcon(stem, rgba, w, h)) {
      icon.texture = aurora_imgui_add_texture(w, h, rgba.data());
      icon.aspect = float(w) / float(h);
    }
    it = sIcons.emplace(stem, icon).first;
  }
  aspect = it->second.aspect;
  return it->second.texture;
}

// "Press [A] to skip" at the bottom centre while the cinematic skip is armed
// (PortOverlay::SkipConfirm*), with the button's icon when there is one. A click or tap on it
// counts as the second press.
void DrawSkipConfirm() {
  const float alpha = PortOverlay::SkipConfirmAlpha();
  if (ImGui::GetCurrentContext() == nullptr || alpha <= 0.f) {
    return;
  }
  float aspect = 1.f;
  const ImTextureID icon = SkipConfirmIcon(aspect);
  char text[64];
  std::snprintf(text, sizeof(text), "Press [%s] to skip", PortPrompts::ConfirmLabel());
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float scale = std::max(1.f, ImGui::GetFontSize() / 13.f);
  ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                                 viewport->Pos.y + viewport->Size.y - 12.f * scale),
                          ImGuiCond_Always, ImVec2(0.5f, 1.f));
  ImGui::SetNextWindowBgAlpha(0.6f * alpha);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
  if (ImGui::Begin("##skip-confirm", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_AlwaysAutoResize)) {
    if (icon) {
      // The text centred on an icon a little taller than it.
      const float size = ImGui::GetTextLineHeight() * 1.6f;
      const float y = ImGui::GetCursorPosY();
      const float textY = y + 0.5f * (size - ImGui::GetTextLineHeight());
      ImGui::SetCursorPosY(textY);
      ImGui::TextUnformatted("Press");
      ImGui::SameLine();
      ImGui::SetCursorPosY(y);
      ImGui::Image(icon, ImVec2(size * aspect, size));
      ImGui::SameLine();
      ImGui::SetCursorPosY(textY);
      ImGui::TextUnformatted("to skip");
    } else {
      ImGui::TextUnformatted(text);
    }
    if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      PortOverlay::SkipConfirmTap();
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
}

void DrawUI() {
  // Not while a control is held: a dragged slider changes a setting every frame. It is saved the
  // frame the control is let go.
  if (PortOverlaySettings::Dirty() && ImGui::GetCurrentContext() != nullptr && !ImGui::IsAnyItemActive()) {
    PortOverlaySettings::SaveSettings();
  }
  UpdateControllerNav();
  // MP2_DEBUG_TAB=<page> opens the window on its first frame, not just preselects the page.
  static bool sStartOpenPending = true;
  if (sStartOpenPending) {
    sStartOpenPending = false;
    if (!sVisible && !GetStartTarget().page.empty()) {
      PortOverlay::Toggle();
    }
    // `Cheats` aliases to Debug; show the rows it named (not saved).
    if (const char* env = port::EnvString("MP2_DEBUG_TAB"); env != nullptr && SDL_strncasecmp(env, "cheats", 6) == 0) {
      sShowCheats = true;
    }
  }
  // While the window is open the cursor belongs to it, not to the game's aim.
  PortSaveState::ResumeInfo promptInfo;
  bool promptYes = true;
  const bool promptUp = PortSaveState::ResumePromptShown(promptInfo, promptYes);
  PortMouse::SetUiActive(sVisible || promptUp);
  // Before the prompt and toasts too: until the window first opened they drew at ImGui's
  // unscaled 13px, tiny on a phone.
  UpdateUiScale(static_cast< float >(PortOverlay::UiScale()) / 100.f);
  ApplyThemeIfChanged();
  BeginTopToastStack();
  if (!sVisible) {
    DrawResumePrompt();
  }
  DrawMenuHintToast();
  DrawDiscReadFailedAlert();
  DrawIgtTimer();
  DrawSkipConfirm();
  DrawShaderCompilationToast();
  DrawUpdateToast();
  DrawNoteToast();
  DrawPhotoUI();
#if defined(PORT_TOUCH_OVERLAY)
  DrawTouchOverlay();
#endif
  if (!sVisible) {
    sTouchScroll = TouchScroll{};
    UpdateMenuSounds(false);
    return;
  }

  UpdateTouchScroll();
  BeginSearchJump();
  const bool open = PageLayout() ? DrawPageWindow() : DrawDesktopWindow();
  EndSearchJump();
  ClearTouchHover();

  if (!open) {
    sVisible = false;
  }
  UpdateMenuSounds(sVisible);
}

} // namespace PortDebug
