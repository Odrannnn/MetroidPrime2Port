// F1 overlay: The Save states tab. State and helpers shared with the other parts are in
// debug_ui_internal.h.

#include "debug_ui_internal.h"

#include "port_input_remap_ui.h"
#include "port_overlay.h"
#include "port_savestate.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <string>

namespace PortDebug {

namespace {

// The slot's room by its English name, else the name the state file recorded.
std::string Where(const PortSaveState::Info& info) {
  std::string room = PortOverlay::RoomName(info.worldId, info.areaIndex);
  return room.empty() ? info.room : room;
}

} // namespace

void DrawSaveStatesTab() {
  ImGui::TextDisabled("Save anywhere and load back to the same spot.");
  ItemHelp("A state holds what a memory card save does, plus where Samus stands and whether she's "
           "in morph ball. Loading rebuilds the room, as a card load does, so enemies and doors "
           "start over. Loading first writes the undo state.");
  if (PortSaveState::Folder().empty()) {
    ImGui::TextDisabled("No user folder, so save states are off.");
    return;
  }
  const std::string saveKeys = PortInputRemap::BindingsText(PortInput::Action::SaveState, " / ");
  const std::string loadKeys = PortInputRemap::BindingsText(PortInput::Action::LoadState, " / ");
  bool hotkeys = sSaveStateHotkeys;
  const std::string hotkeysLabel = (saveKeys.empty() ? std::string("Save state binding") : saveKeys) +
                                   " saves, " +
                                   (loadKeys.empty() ? std::string("Load state binding") : loadKeys) +
                                   " loads the selected slot###savestate-hotkeys";
  if (ImGui::Checkbox(hotkeysLabel.c_str(), &hotkeys)) {
    sSaveStateHotkeys = hotkeys;
    Changed(true);
  }
  {
    const std::string prevKeys = PortInputRemap::BindingsText(PortInput::Action::PrevStateSlot, " / ");
    const std::string nextKeys = PortInputRemap::BindingsText(PortInput::Action::NextStateSlot, " / ");
    std::string slotKeys;
    if (!prevKeys.empty()) slotKeys += " " + prevKeys + " picks the previous slot.";
    if (!nextKeys.empty()) slotKeys += " " + nextKeys + " picks the next slot.";
    ItemHelp(("Off, the bindings do nothing, so a stray key cannot save over or load a slot. The "
              "buttons below still work." + slotKeys).c_str());
  }
  bool roomState = sSaveStateRoomState;
  if (ImGui::Checkbox("Keep enemies and room objects###savestate-room-state", &roomState)) {
    sSaveStateRoomState = roomState;
    Changed(true);
  }
  ItemHelp("Save states and quick resume also store the loaded rooms' enemies, doors and objects, "
           "so a killed enemy stays dead. Off, the room starts over as after a memory card load. "
           "Applies to states saved from now on.");

  const int selected = PortSaveState::SelectedSlot();
  if (ImGui::BeginTable("##savestates", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Where");
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (int slot = 1; slot <= PortSaveState::kSlotCount; ++slot) {
      const PortSaveState::Info info = PortSaveState::SlotInfo(slot);
      ImGui::PushID(slot);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      char label[16];
      std::snprintf(label, sizeof(label), "%d", slot);
      if (ImGui::RadioButton(label, selected == slot)) {
        PortSaveState::SetSelectedSlot(slot);
      }
      ImGui::TableNextColumn();
      if (info.exists) {
        ImGui::Text("%s%s", Where(info).c_str(), info.morphed ? " (ball)" : "");
      } else {
        ImGui::TextDisabled("empty");
      }
      ImGui::TableNextColumn();
      if (info.exists) {
        const int total = static_cast< int >(info.playTime);
        ImGui::Text("%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
      }
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("Save")) {
        PortSaveState::SetSelectedSlot(slot);
        PortSaveState::RequestSave(slot);
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!info.exists);
      if (ImGui::SmallButton("Load")) {
        PortSaveState::SetSelectedSlot(slot);
        PortSaveState::RequestLoad(slot);
      }
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  bool quickResume = sQuickResume;
  if (ImGui::Checkbox("Quick resume###quick-resume", &quickResume)) {
    sQuickResume = quickResume;
    PortOverlaySettings::MarkDirty();
  }
  ItemHelp("On quitting during play, a resume state is written, and the next launch asks \"Resume "
           "where you left off?\" Yes restores it like a save state; No keeps it. Nothing is "
           "written in cutscenes, during world transitions, while Samus is dead or on the title "
           "screen.");

  const PortSaveState::Info undo = PortSaveState::SlotInfo(PortSaveState::kUndoSlot);
  ImGui::BeginDisabled(!undo.exists);
  if (ImGui::Button("Undo last load")) {
    PortSaveState::RequestLoad(PortSaveState::kUndoSlot);
  }
  ImGui::EndDisabled();
  if (undo.exists) {
    ImGui::SameLine();
    ImGui::TextDisabled("back to %s", Where(undo).c_str());
  }
  const std::string message = PortSaveState::LastMessage();
  if (!message.empty()) {
    ImGui::TextWrapped("%s", message.c_str());
  }
  ImGui::TextDisabled("Folder: %s", PortSaveState::Folder().c_str());
}

} // namespace PortDebug
