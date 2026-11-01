// F1 overlay: the Tracker page, pickups on the map (PortMapPickups) and how many each world has
// left. State and helpers shared with the other parts are in debug_ui_internal.h.

#include "debug_ui_internal.h"

#include "port_map_pickups.h"
#include "port_overlay.h"
#include "port_rando_world.h"

#include <imgui.h>

#include <algorithm>

namespace PortDebug {

namespace {

void DrawTrackerCount(const char* label, int have, int total) {
  if (total > 0 && have >= total) {
    ImGui::TextColored(ThemeGoodColor(), "%s %d/%d", label, have, total);
  } else {
    ImGui::Text("%s %d/%d", label, have, total);
  }
}

} // namespace

void DrawTrackerTab() {
  ImGui::BeginDisabled(PortOverlay::OriginalExperience());
  ImGui::SeparatorText("Map");
  int mode = sMapPickups;
  if (ImGui::Combo("Pickups on the map", &mode, "Off\0" "Rooms\0" "All\0")) {
    sMapPickups = std::clamp(mode, static_cast< int >(PortOverlay::kMapPickups_Off),
                             static_cast< int >(PortOverlay::kMapPickups_All));
    Changed(true);
  }
  ItemHelp("Rooms tints the map's rooms that still hold a pickup. All draws a white dot on every "
           "pickup not collected yet. Every item looks the same, so a randomized game isn't "
           "spoiled.");
  ImGui::BeginDisabled(sMapPickups != PortOverlay::kMapPickups_All);
  Changed(ImGui::Checkbox("Show pickup kinds", &sMapPickupKinds));
  ItemHelp("A ring for an expansion or energy tank, a dot for a major item, and a grey cross where "
           "a pickup was collected. In a randomized game the shape follows the seed's item.");
  ImGui::EndDisabled();
  ImGui::EndDisabled();

  ImGui::SeparatorText("Collected");
  DrawTrackerCount("All worlds", PortMapPickups::CollectedCount(0), PortMapPickups::Count(0));
  const int worlds = PortRandoWorld::WorldCount();
  for (int i = 0; i < worlds; ++i) {
    const PortRandoWorld::World* world = PortRandoWorld::WorldAt(i);
    const int total = PortMapPickups::Count(world->mlvl);
    if (total > 0) {
      DrawTrackerCount(world->name, PortMapPickups::CollectedCount(world->mlvl), total);
    }
  }
}

} // namespace PortDebug
