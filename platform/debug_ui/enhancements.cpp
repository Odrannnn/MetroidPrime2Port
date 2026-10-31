// F1 overlay: the Enhancements page, optional changes to how the game plays or looks that are
// not cheats (they are saved). State and helpers shared with the other parts are in
// debug_ui_internal.h.

#include "debug_ui_internal.h"

#include "port_overlay.h"

#include "MetroidPrime/Player/CGameOptions.hpp"
#include "MetroidPrime/Player/CGameState.hpp"

#include <imgui.h>

#include <algorithm>

namespace PortDebug {

void DrawEnhancementsTab() {
  // Original experience plays the game as it shipped: every enhancement acts as retail.
  ImGui::BeginDisabled(PortOverlay::OriginalExperience());
  ImGui::SeparatorText("Dark Visor");
  Changed(ImGui::Checkbox("Full-screen Dark Visor", &sDarkVisorFull));
  Changed(ImGui::SliderInt("Dark Visor static", &sDarkVisorStatic, 0, PortOverlay::kDarkVisorStaticMax, "%d%%"));

  ImGui::SeparatorText("Ammo");
  Changed(ImGui::Checkbox("Infinite Light and Dark Ammo", &sInfiniteBeamAmmo));

  ImGui::SeparatorText("Pace");
  // The game reads this every frame in its cinematic flow state.
  Changed(ImGui::Checkbox("Skippable cutscenes", &sSkipCinematics));
  int walkSpeed = sWalkSpeed;
  if (ImGui::SliderInt("Walk speed", &walkSpeed, PortOverlay::kWalkSpeedMin,
                       PortOverlay::kWalkSpeedMax, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
    sWalkSpeed = walkSpeed;
    Changed(true);
  }
  int scanSpeed = sScanSpeed;
  if (ImGui::SliderInt("Scan speed", &scanSpeed, PortOverlay::kScanSpeedMin,
                       PortOverlay::kScanSpeedMax, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
    sScanSpeed = scanSpeed;
    Changed(true);
  }
  // Elevator and portal share a row: same choices, short labels.
  const float rideWidth = ImGui::GetFontSize() * 6.f;
  int elevatorRide = sElevatorRide;
  ImGui::SetNextItemWidth(rideWidth);
  if (ImGui::Combo("Elevator ride cutscene", &elevatorRide, "Original\0" "Fast\0" "Skip\0")) {
    sElevatorRide = std::clamp(elevatorRide, static_cast< int >(PortOverlay::kElevatorRide_Original),
                               static_cast< int >(PortOverlay::kElevatorRide_Skip));
    Changed(true);
  }
  ImGui::SameLine();
  int portalTransition = sPortalTransition;
  ImGui::SetNextItemWidth(rideWidth);
  if (ImGui::Combo("Portal transition", &portalTransition, "Original\0" "Fast\0" "Skip\0")) {
    sPortalTransition = std::clamp(portalTransition,
                                   static_cast< int >(PortOverlay::kPortalTransition_Original),
                                   static_cast< int >(PortOverlay::kPortalTransition_Skip));
    Changed(true);
  }
  int rotatingRooms = sRotatingRooms;
  ImGui::SetNextItemWidth(rideWidth);
  if (ImGui::Combo("Energy controller access cutscene", &rotatingRooms, "Original\0" "Fast\0")) {
    sRotatingRooms = std::clamp(rotatingRooms, static_cast< int >(PortOverlay::kRotatingRooms_Original),
                                static_cast< int >(PortOverlay::kRotatingRooms_Fast));
    Changed(true);
  }

  ImGui::SeparatorText("Morph Ball");
  Changed(ImGui::Checkbox("Fast Morph", &sFastMorph));
  ImGui::SameLine();
  Changed(ImGui::Checkbox("Spring Ball", &sSpringBall));
  ImGui::EndDisabled();

  ImGui::SeparatorText("Hints");
  // The game's own option, the same switch as the pause menu's Hint System and saved with the other
  // game options, so Original experience leaves it alone.
  ImGui::BeginDisabled(gpGameState == nullptr);
  bool hints = gpGameState != nullptr && gpGameState->GameOptions().GetIsHintSystemEnabled();
  if (ImGui::Checkbox("Hints", &hints) && gpGameState != nullptr) {
    gpGameState->GameOptions().SetIsHintSystemEnabled(hints);
  }
  ImGui::EndDisabled();
  ImGui::BeginDisabled(PortOverlay::OriginalExperience());
  int hintDelay = sHintDelay;
  if (ImGui::SliderInt("Hint delay", &hintDelay, PortOverlay::kHintDelayMin,
                       PortOverlay::kHintDelayMax, hintDelay == 0 ? "Game default" : "%d min",
                       ImGuiSliderFlags_AlwaysClamp)) {
    sHintDelay = hintDelay;
    Changed(true);
  }
  ImGui::EndDisabled();
  ImGui::BeginDisabled(gpGameState == nullptr ||
                       !gpGameState->GameOptions().GetIsHintSystemEnabled());
  if (ImGui::Button("Show current hint")) {
    PortOverlay::RequestShowHint();
  }
  ImGui::EndDisabled();
}

} // namespace PortDebug
