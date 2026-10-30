// Photo mode's panel, compact bar and toasts (MP1's overlays.cpp photo part), and the touch
// handling that flies the camera on Android. The logic is platform/port_photo.cpp.
#include "port_mobile.h"
#include "debug_ui_internal.h"

#include "port_freecam.h"
#include "port_overlay.h"
#include "port_photo.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_touch.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace PortDebug {
namespace {
#if defined(PORT_MOBILE)
constexpr bool kTouchDevice = true;
#else
constexpr bool kTouchDevice = false;
#endif

// Photo mode on a touchscreen: the touch controls hide (nativeDebugOverlayVisible), a finger
// put down on the left half is an invisible stick centred where it landed, and one on the right
// half drags the view. Fingers that land on the photo windows are left to ImGui. Events arrive on
// Android's UI thread, so the state is shared under a lock and the game thread takes it each frame.
constexpr float kPhotoStickRadius = 0.12f; // of the screen height, for a full stick
constexpr float kPhotoLookDegrees = 100.f; // per screen height dragged
struct PhotoTouch {
  bool stickDown = false, lookDown = false; // Android finger ids start at 0
  SDL_FingerID stick = 0;
  SDL_FingerID look = 0;
  float stickX = 0.f, stickY = 0.f; // where the stick finger landed
  float fingerX = 0.f, fingerY = 0.f; // where it is now
  float moveX = 0.f, moveY = 0.f;
  float lookX = 0.f, lookY = 0.f; // the look finger's last place
  float yaw = 0.f, pitch = 0.f;   // not taken yet
  bool enabled = false;
  float aspect = 1.f; // window width over height
  std::vector<ImVec4> windows; // x0, y0, x1, y1 as fractions of the window
};
std::mutex sPhotoTouchMutex;
PhotoTouch sPhotoTouch;
} // namespace

void PhotoTouchEvent(const SDL_Event& event) {
  if (event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_MOTION &&
      event.type != SDL_EVENT_FINGER_UP && event.type != SDL_EVENT_FINGER_CANCELED) {
    return;
  }
  const SDL_TouchFingerEvent& f = event.tfinger;
  std::lock_guard lock(sPhotoTouchMutex);
  PhotoTouch& t = sPhotoTouch;
  if (event.type == SDL_EVENT_FINGER_DOWN) {
    if (!t.enabled) {
      return;
    }
    for (const ImVec4& r : t.windows) {
      if (f.x >= r.x && f.x <= r.z && f.y >= r.y && f.y <= r.w) {
        return;
      }
    }
    if (f.x < 0.5f && !t.stickDown) {
      t.stickDown = true;
      t.stick = f.fingerID;
      t.stickX = f.x;
      t.stickY = f.y;
      t.fingerX = f.x;
      t.fingerY = f.y;
      t.moveX = t.moveY = 0.f;
    } else if (f.x >= 0.5f && !t.lookDown) {
      t.lookDown = true;
      t.look = f.fingerID;
      t.lookX = f.x;
      t.lookY = f.y;
    }
  } else if (event.type == SDL_EVENT_FINGER_MOTION) {
    if (t.stickDown && f.fingerID == t.stick) {
      t.fingerX = f.x;
      t.fingerY = f.y;
      t.moveX = std::clamp((f.x - t.stickX) * t.aspect / kPhotoStickRadius, -1.f, 1.f);
      t.moveY = std::clamp((t.stickY - f.y) / kPhotoStickRadius, -1.f, 1.f);
    } else if (t.lookDown && f.fingerID == t.look) {
      t.yaw -= (f.x - t.lookX) * t.aspect * kPhotoLookDegrees;
      t.pitch -= (f.y - t.lookY) * kPhotoLookDegrees;
      t.lookX = f.x;
      t.lookY = f.y;
    }
  } else {
    if (t.stickDown && f.fingerID == t.stick) {
      t.stickDown = false;
      t.moveX = t.moveY = 0.f;
    }
    if (t.lookDown && f.fingerID == t.look) {
      t.lookDown = false;
    }
  }
}

namespace {
// Once per frame on the game thread: hands the touches to the free camera, and records where the
// photo windows are (`windows`) and whether fingers may fly the camera now.
void PhotoTouchFrame(bool enabled, std::vector<ImVec4> windows) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  float moveX = 0.f, moveY = 0.f, yaw = 0.f, pitch = 0.f;
  bool stickDown = false;
  ImVec2 stickAt, fingerAt;
  {
    std::lock_guard lock(sPhotoTouchMutex);
    PhotoTouch& t = sPhotoTouch;
    t.enabled = enabled;
    t.windows = std::move(windows);
    if (viewport->Size.y > 0.f) {
      t.aspect = viewport->Size.x / viewport->Size.y;
    }
    if (!enabled) {
      t.stickDown = t.lookDown = false;
      t.moveX = t.moveY = 0.f;
    }
    moveX = t.moveX;
    moveY = t.moveY;
    stickDown = t.stickDown;
    stickAt = ImVec2(t.stickX, t.stickY);
    fingerAt = ImVec2(t.fingerX, t.fingerY);
    yaw = t.yaw;
    pitch = t.pitch;
    t.yaw = t.pitch = 0.f;
  }
  if (PortPhoto::Active()) {
    PortFreeCam::SetTouchMove(moveX, moveY);
    if (enabled) {
      PortFreeCam::AddTouchTurn(yaw, pitch);
    }
  }
  // The floating stick, drawn like the touch controls' one while a finger holds it. Captures don't
  // see it: they run with touch disabled, which lifts the stick.
  if (stickDown && enabled) {
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float radius = kPhotoStickRadius * size.y;
    const ImVec2 centre(origin.x + stickAt.x * size.x, origin.y + stickAt.y * size.y);
    float dx = (fingerAt.x - stickAt.x) * size.x, dy = (fingerAt.y - stickAt.y) * size.y;
    if (const float distance = std::hypot(dx, dy); distance > radius) {
      dx *= radius / distance;
      dy *= radius / distance;
    }
    const float ring = radius / 0.58f; // the knob reaches full tilt where the touch stick's does
    const float line = 2.f * sUiAppliedScale;
    const ImU32 stroke = IM_COL32(0xE1, 0xF8, 0xFF, 0xFF);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddCircleFilled(centre, ring, IM_COL32(0x48, 0xC8, 0xE8, 0x88), 48);
    dl->AddCircle(centre, ring, stroke, 48, line);
    dl->AddCircle(ImVec2(centre.x + dx, centre.y + dy), ring * 0.42f, stroke, 32, line);
  }
}

// The window just drawn, as fractions of the main viewport.
ImVec4 PhotoWindowRect(const ImGuiViewport* viewport) {
  const ImVec2 pos = ImGui::GetWindowPos();
  const ImVec2 size = ImGui::GetWindowSize();
  if (viewport->Size.x <= 0.f || viewport->Size.y <= 0.f) {
    return ImVec4();
  }
  return ImVec4((pos.x - viewport->Pos.x) / viewport->Size.x, (pos.y - viewport->Pos.y) / viewport->Size.y,
                (pos.x + size.x - viewport->Pos.x) / viewport->Size.x,
                (pos.y + size.y - viewport->Pos.y) / viewport->Size.y);
}
} // namespace

// The photo mode panel (top right, compact) and its messages (a toast under the others).
void DrawPhotoUI() {
  std::vector<ImVec4> touchWindows;
  const bool touchEnabled = PortPhoto::Active() && !PortPhoto::Capturing() && !PortOverlay::Visible();
  struct TouchFrameAtExit {
    const bool& enabled;
    std::vector<ImVec4>& windows;
    ~TouchFrameAtExit() { PhotoTouchFrame(enabled, std::move(windows)); }
  } touchFrame{touchEnabled, touchWindows};
  // PortPhoto::Frame() runs once per frame from PortOverlay::Draw, before this.
  static std::string sMessage;
  static double sMessageAt = 0.0;
  const double now = ImGui::GetTime();
  if (std::string text = PortPhoto::TakeMessage(); !text.empty()) {
    sMessage = std::move(text);
    sMessageAt = now;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  if (!sMessage.empty() && now - sMessageAt < 4.0 && !PortPhoto::Capturing()) {
    PlaceTopToast(viewport);
    ImGui::SetNextWindowSize(ImVec2(std::min(viewport->Size.x - 32.f, 560.f), 0.f));
    ImGui::SetNextWindowBgAlpha(0.8f);
    if (ImGui::Begin("##photo-toast", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoInputs)) {
      ImGui::TextWrapped("%s", sMessage.c_str());
      TopToastPlaced(viewport);
    }
    ImGui::End();
  }
  if (!PortPhoto::Active() || PortPhoto::Capturing()) {
    return;
  }
  // Keyboard and mouse: H panel, P or a left click while looking captures, Esc exits, the wheel
  // zooms. A captured mouse is the camera's, so the click can't be meant for a widget.
  static bool sWasLeftDown = false;
  const bool leftDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
  const bool leftClicked = leftDown && !sWasLeftDown;
  sWasLeftDown = leftDown;
  if (!kTouchDevice && !PortOverlay::Visible() && !ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_H, false)) {
      PortPhoto::SetPanelShown(!PortPhoto::PanelShown());
    }
    if (ImGui::IsKeyPressed(ImGuiKey_P, false) || (leftClicked && PortPhoto::MouseLook())) {
      PortPhoto::RequestCapture();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      PortPhoto::RequestExit();
    }
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.f && (PortPhoto::MouseLook() || !ImGui::GetIO().WantCaptureMouse)) {
      PortPhoto::SetFov(PortPhoto::Fov() - wheel * 5.f);
    }
  }
  if (!PortPhoto::PanelShown()) {
    // The touch controls are gone in photo mode, so a touchscreen gets a small bar for it.
    if (kTouchDevice) {
      const float margin = 12.f * sUiAppliedScale;
      ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - margin, viewport->Pos.y + margin),
                              ImGuiCond_Always, ImVec2(1.f, 0.f));
      ImGui::SetNextWindowBgAlpha(0.6f);
      if (ImGui::Begin("##photo-bar", nullptr,
                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::Button("Panel")) {
          PortPhoto::SetPanelShown(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Capture")) {
          PortPhoto::RequestCapture();
        }
        ImGui::SameLine();
        if (ImGui::Button("Exit")) {
          PortPhoto::RequestExit();
        }
        touchWindows.push_back(PhotoWindowRect(viewport));
      }
      ImGui::End();
    }
    return;
  }
  // Sized in scaled units so it fits a phone (density ~3). The touch controls hide in photo mode.
  const float margin = 12.f * sUiAppliedScale;
  const float width = std::min(viewport->Size.x - 2.f * margin, 320.f * sUiAppliedScale);
  ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - margin, viewport->Pos.y + margin),
                          ImGuiCond_Always, ImVec2(1.f, 0.f));
  // Height 0 fits the content each frame; the width stays fixed so wide content wraps, not overflows.
  ImGui::SetNextWindowSize(ImVec2(width, 0.f));
  ImGui::SetNextWindowBgAlpha(0.85f);
  // The pad's d-pad and A also fly, roll and capture, so the panel only navigates while it has the
  // pad (Y); the nav cursor starts on the first widget when it takes it.
  const bool padFocus = PortPhoto::PanelFocused();
  static bool sWasPadFocus = false;
  if (padFocus && !sWasPadFocus) {
    ImGui::SetNextWindowFocus();
    ImGui::SetNavCursorVisible(true);
  }
  sWasPadFocus = padFocus;
  if (ImGui::Begin("Photo mode##photo", nullptr,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoSavedSettings | (padFocus ? 0 : ImGuiWindowFlags_NoNav))) {
    float fov = PortPhoto::Fov();
    if (ImGui::SliderFloat("FOV", &fov, 20.f, 120.f, "%.0f")) {
      PortPhoto::SetFov(fov);
    }
    float roll = PortPhoto::Roll();
    if (ImGui::SliderFloat("Roll", &roll, -180.f, 180.f, "%.0f")) {
      PortPhoto::SetRoll(roll);
    }
    bool samus = PortPhoto::ShowSamus();
    if (ImGui::Checkbox("Show Samus", &samus)) {
      PortPhoto::SetShowSamus(samus);
    }
    float speed = PortPhoto::Speed();
    if (ImGui::SliderFloat("Speed", &speed, 1.f, 100.f, "%.0f m/s")) {
      PortPhoto::SetSpeed(speed);
      PortOverlaySettings::MarkDirty();
    }
    int factor = PortPhoto::Supersample();
    ImGui::TextUnformatted("Size");
    for (const int f : {1, 2, 4}) {
      ImGui::SameLine();
      char label[8];
      std::snprintf(label, sizeof(label), "%dx##ss", f);
      if (ImGui::RadioButton(label, factor == f)) {
        PortPhoto::SetSupersample(f);
        PortOverlaySettings::MarkDirty();
      }
    }
    if (ImGui::Button("Capture")) {
      PortPhoto::RequestCapture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Hide panel")) {
      PortPhoto::SetPanelShown(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Exit")) {
      PortPhoto::RequestExit();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (kTouchDevice) {
      ImGui::TextWrapped("Touch: left half moves, right half looks around");
    } else if (padFocus) {
      ImGui::TextWrapped("Pad: d-pad move, A change, Y or B back to the camera");
    } else {
      ImGui::TextWrapped("Pad: A capture, B exit, Y panel, d-pad roll, X+d-pad FOV");
      ImGui::TextWrapped("Mouse: hold right button to look, click captures, wheel FOV; P capture, H panel, "
                         "Esc exit");
    }
    ImGui::PopStyleColor();
    touchWindows.push_back(PhotoWindowRect(viewport));
  }
  ImGui::End();
}

} // namespace PortDebug
