// F1 overlay: The Debug tab: the live state a test wants to read, the free camera and its photo
// rows, and the console that drives the rest. State and helpers shared with the other parts are in
// debug_ui_internal.h.

#include "debug_ui_internal.h"

#include "port_freecam.h"
#include "port_harness.h"
#include "port_overlay.h"
#include "port_photo.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

namespace PortDebug {

// MP1's DrawFreeCam (debug_ui/cheats.cpp) with this port's photo rows under it.
void DrawFreeCam() {
  bool on = PortFreeCam::Active();
  if (ImGui::Checkbox("Free camera", &on)) {
    PortFreeCam::SetActive(on, nullptr);
  }
  ItemHelp("Fly a camera of your own with the pad; the HUD and arm cannon are hidden. The Photo mode "
           "key toggles it too.");
  if (!PortFreeCam::Active()) {
    return;
  }
  SameLineAfterHelp();
  bool frozen = PortFreeCam::Frozen();
  if (ImGui::Checkbox("Freeze the game", &frozen)) {
    PortFreeCam::SetFrozen(frozen);
  }
  ImGui::SameLine();
  bool showPlayer = PortFreeCam::ShowPlayer();
  if (ImGui::Checkbox("Show Samus", &showPlayer)) {
    PortFreeCam::SetShowPlayer(showPlayer);
  }
  float speed = PortFreeCam::Speed();
  if (ImGui::SliderFloat("Speed", &speed, 1.f, 100.f, "%.0f m/s")) {
    PortFreeCam::SetSpeed(speed);
  }
  PortFreeCam::Pose pose = PortFreeCam::GetPose();
  float pos[3] = {pose.x, pose.y, pose.z};
  float look[2] = {pose.yaw, pose.pitch};
  bool moved = ImGui::InputFloat3("Position", pos, "%.2f");
  moved |= ImGui::InputFloat2("Yaw, pitch", look, "%.1f");
  if (moved) {
    pose.x = pos[0];
    pose.y = pos[1];
    pose.z = pos[2];
    pose.yaw = look[0];
    pose.pitch = look[1];
    PortFreeCam::SetPose(pose);
  }
  if (ImGui::Button("Move Samus here")) {
    PortFreeCam::RequestMovePlayerHere();
  }
  ImGui::SetItemTooltip("Puts Samus where the camera is, facing its way, and turns the camera off.");
  ImGui::TextWrapped("Close this menu to fly with the left stick, C stick, L / R and Z.");

  ImGui::SeparatorText("Photo");
  // 0 keeps the game's own FOV (55 in first person); the slider shows that until it is moved.
  const bool gameFov = PortFreeCam::FovOverride() <= 0.f;
  float fov = gameFov ? 55.f : PortFreeCam::FovOverride();
  if (ImGui::SliderFloat("Field of view##photo", &fov, 30.f, 100.f, gameFov ? "game (%.0f)" : "%.0f deg")) {
    PortFreeCam::SetFovOverride(fov);
  }
  ImGui::SetItemTooltip("The free camera's vertical field of view; untouched, it keeps the game's own.");
  // The slider stops at 30, so the game's own FOV needs its own button once the slider is moved.
  if (!gameFov) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Game FOV")) {
      PortFreeCam::SetFovOverride(0.f);
    }
  }
  float roll = PortPhoto::Roll();
  if (ImGui::SliderFloat("Roll", &roll, -180.f, 180.f, "%.0f deg")) {
    PortPhoto::SetRoll(roll);
  }
  ImGui::SetItemTooltip("Tilts the picture about the view axis; movement ignores it.");
  float photoSpeed = PortPhoto::Speed();
  if (ImGui::SliderFloat("Camera speed", &photoSpeed, 0.5f, 50.f, "%.1f m/s")) {
    PortPhoto::SetSpeed(photoSpeed);
    Changed(true);
  }
  ImGui::SetItemTooltip("Metres per second at a full stick; the right trigger multiplies it by four.");
  int ss = PortPhoto::Supersample() >= 4 ? 2 : PortPhoto::Supersample() >= 2 ? 1 : 0;
  if (ImGui::Combo("Supersample", &ss, "1x\0" "2x\0" "4x\0")) {
    PortPhoto::SetSupersample(ss == 2 ? 4 : ss == 1 ? 2 : 1);
    Changed(true);
  }
  ImGui::SetItemTooltip("Renders the photo at 2x or 4x the window's size; a size the GPU can't hold\n"
                        "steps down.");
  bool showSamus = PortPhoto::ShowSamus();
  if (ImGui::Checkbox("Show Samus in photos", &showSamus)) {
    PortPhoto::SetShowSamus(showSamus);
  }
  ImGui::SetItemTooltip("Draws Samus's body where the player stands.");
  ImGui::BeginDisabled(PortPhoto::Capturing());
  if (ImGui::Button("Take photo")) {
    PortPhoto::RequestCapture(0);
  }
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("Saves a PNG of the picture without the HUD to screenshots/.");
  const PortPhoto::CaptureResult last = PortPhoto::LastCapture();
  if (PortPhoto::Capturing()) {
    ImGui::TextDisabled("Capturing...");
  } else if (last.state > 0) {
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextColored(ThemeGoodColor(), "Saved %ux%u: %s", last.width, last.height, last.path.c_str());
    ImGui::PopTextWrapPos();
  } else if (last.state < 0) {
    ImGui::TextColored(ThemeBadColor(), "Capture failed; the log says why");
  }
}

void DrawDebugTab() {
  // MP1's gate for the cheat rows. Saved, as MP1 does: it only shows them; the cheats themselves
  // (sGodMode) still do not persist.
  ImGui::SeparatorText("Cheats");
  Changed(ImGui::Checkbox("Show cheats (items, health, teleport)", &sShowCheats));
  if (sShowCheats) {
    DrawCheatsSection();
  }

  ImGui::SeparatorText("Game");
  ImGui::Text("world %08X  area %d", sWorldId, sAreaId);
  if (const std::string room = PortOverlay::RoomName(sWorldId, sAreaId); !room.empty()) {
    ImGui::TextUnformatted(room.c_str());
  }
  ImGui::Text("game updates: %u", PortHarness::GameUpdates());

  ImGui::SeparatorText("Camera");
  DrawFreeCam();

  ImGui::SeparatorText("Rendering");
  DrawRendering();

  ImGui::SeparatorText("Audio backends");
  DrawAudio();
  if (ImGui::CollapsingHeader("Sounds playing")) {
    DrawVoices();
  }

  ImGui::SeparatorText("Console");
  ImGui::TextWrapped("The console runs on the game thread and answers one command per update: "
                     "`areas`, `warp`, `world`, `items`, `give`, `god`, `objs`, `send`, `press`.");
  ImGui::TextDisabled("MP2_CONSOLE=<port> serves it over TCP (tools/mp2con.py -p <port> help); "
                      "MP2_INPUT drives the pad from a script.");
}

} // namespace PortDebug
