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
  ItemHelp("The Dark Visor shows hidden things over the whole view, not only inside its "
           "lens, and the HUD's centre gear and rings are hidden.");
  Changed(ImGui::SliderInt("Dark Visor static", &sDarkVisorStatic, 0, PortOverlay::kDarkVisorStaticMax, "%d%%"));
  ItemHelp("How much the Dark Visor's grey static covers the view; lower keeps the scene's "
           "colours, 0 shows only the highlights.");

  ImGui::SeparatorText("Ammo");
  Changed(ImGui::Checkbox("Infinite Light and Dark Ammo", &sInfiniteBeamAmmo));
  ItemHelp("The Light and Dark Beams use no ammo and the HUD shows infinity. Missiles and "
           "Power Bombs are unchanged. Kept between launches, unlike the Infinite cheat.");

  ImGui::SeparatorText("Pace");
  // The game reads this every frame in its cinematic flow state.
  Changed(ImGui::Checkbox("Skippable cutscenes", &sSkipCinematics));
  ItemHelp("START or A skips any cutscene and the new game's intro text, seen or not. Off: START skips "
           "only the ones already seen.");
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

  ImGui::SeparatorText("Morph Ball");
    Changed(ImGui::Checkbox("Fast Morph", &sFastMorph));
  ItemHelp("The morph and unmorph transitions take 0.2 s instead of the game's second.");
  SameLineAfterHelp();
  Changed(ImGui::Checkbox("Spring Ball", &sSpringBall));
  ItemHelp("A small jump in morph ball with C-stick up, as in Metroid Prime Trilogy. Needs the Morph "
           "Ball Bombs.");
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
  ItemHelp("The game's hint system, the same switch as Hint System in the pause menu's options. "
           "Saved with the game options.");
  ImGui::BeginDisabled(PortOverlay::OriginalExperience());
  int hintDelay = sHintDelay;
  if (ImGui::SliderInt("Hint delay", &hintDelay, PortOverlay::kHintDelayMin,
                       PortOverlay::kHintDelayMax, hintDelay == 0 ? "Game default" : "%d min",
                       ImGuiSliderFlags_AlwaysClamp)) {
    sHintDelay = hintDelay;
    Changed(true);
  }
  ItemHelp("The longest a hint waits before it first shows. Game default keeps the game's own "
           "timing; a shorter wait also applies to hints already waiting in your save. Needs Hints "
           "on.");
  ImGui::EndDisabled();
  ImGui::BeginDisabled(gpGameState == nullptr ||
                       !gpGameState->GameOptions().GetIsHintSystemEnabled());
  if (ImGui::Button("Show current hint")) {
    PortOverlay::RequestShowHint();
  }
  ImGui::EndDisabled();
  ItemHelp("Shows the hint the game would show next, now: the one on screen again, or the one "
           "due soonest. Needs Hints on.");
}

} // namespace PortDebug
