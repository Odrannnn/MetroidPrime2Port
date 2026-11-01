// F1 > Multiplayer: each local player's controller and their own input settings. Player 1 edits
// the same values as the Controls tab; players 2-4 follow Player 1 until a row is changed for them
// (settings.cfg `p<N>.<key>`).
#include "debug_ui_internal.h"

#include "port_controls.h"
#include "port_input_remap_ui.h"
#include "port_mouse.h"
#include "port_overlay.h"

#include <imgui.h>

#include <algorithm>

namespace PortDebug {
namespace {

using PortOverlay::PlayerSetting;

int sPlayer = 0;

// Player 1's global for `key`, as a PlayerOverride value.
float GlobalSetting(PlayerSetting key) {
  switch (key) {
  case PortOverlay::kPlayer_LockOnToggle: return sLockOnToggle ? 1.f : 0.f;
  case PortOverlay::kPlayer_StickyCharge: return sStickyCharge ? 1.f : 0.f;
  case PortOverlay::kPlayer_RapidCharge: return sRapidCharge ? 1.f : 0.f;
  case PortOverlay::kPlayer_AutoAim: return sAutoAim ? 1.f : 0.f;
  case PortOverlay::kPlayer_LockOnFreeAim: return sLockOnFreeAim ? 1.f : 0.f;
  case PortOverlay::kPlayer_GyroAim: return sGyroAim ? 1.f : 0.f;
  case PortOverlay::kPlayer_GyroMode: return float(sGyroMode);
  case PortOverlay::kPlayer_GyroSensitivity: return sGyroSensitivity;
  case PortOverlay::kPlayer_GyroSplit: return sGyroSplit ? 1.f : 0.f;
  case PortOverlay::kPlayer_GyroSensitivityY: return sGyroSensitivityY;
  default: return 0.f;
  }
}

// The stored value (not the effective one: Original experience is shown by disabling the row).
float Setting(int player, PlayerSetting key) {
  const PlayerOverride& o = sPlayerSettings[player][key];
  return player > 0 && o.set ? o.value : GlobalSetting(key);
}

// Written in place rather than through SetPlayerSetting, which saves the file on every call (a
// slider drag would save each frame); Changed() saves once the control is let go.
void SetSetting(int player, PlayerSetting key, float value) {
  if (key == PortOverlay::kPlayer_GyroMode) {
    value = std::clamp(value, float(PortOverlay::kGyroModeMin), float(PortOverlay::kGyroModeMax));
  } else if (key == PortOverlay::kPlayer_GyroSensitivity || key == PortOverlay::kPlayer_GyroSensitivityY) {
    value = std::clamp(value, PortOverlay::kGyroSensitivityMin, PortOverlay::kGyroSensitivityMax);
  }
  if (player > 0) {
    sPlayerSettings[player][key] = {value, true};
  } else {
    switch (key) {
    case PortOverlay::kPlayer_LockOnToggle: sLockOnToggle = value != 0.f; break;
    case PortOverlay::kPlayer_StickyCharge: sStickyCharge = value != 0.f; break;
    case PortOverlay::kPlayer_RapidCharge: sRapidCharge = value != 0.f; break;
    case PortOverlay::kPlayer_AutoAim: sAutoAim = value != 0.f; break;
    case PortOverlay::kPlayer_LockOnFreeAim: sLockOnFreeAim = value != 0.f; break;
    case PortOverlay::kPlayer_GyroAim: sGyroAim = value != 0.f; break;
    case PortOverlay::kPlayer_GyroMode: sGyroMode = int(value); break;
    case PortOverlay::kPlayer_GyroSensitivity: sGyroSensitivity = value; break;
    case PortOverlay::kPlayer_GyroSplit: sGyroSplit = value != 0.f; break;
    case PortOverlay::kPlayer_GyroSensitivityY: sGyroSensitivityY = value; break;
    default: break;
    }
  }
  Changed(true);
}

// After a row of players 2-4: Reset when the player has their own value, else a dimmed note.
// Returns true when Reset was clicked.
bool OverrideMark(int player, bool set, int id) {
  if (player == 0) return false;
  ImGui::SameLine();
  if (!set) {
    ImGui::TextDisabled("same as Player 1");
    return false;
  }
  ImGui::PushID(id);
  const bool reset = ImGui::SmallButton("Reset");
  ImGui::PopID();
  ImGui::SetItemTooltip("Use Player 1's value again.");
  return reset;
}

void SettingMark(int player, PlayerSetting key) {
  if (OverrideMark(player, player > 0 && PortOverlay::PlayerSettingSet(player, key), key)) {
    PortOverlay::ClearPlayerSetting(player, key);
  }
}

void StickMark(int player, PortMouse::StickKey key) {
  if (OverrideMark(player, player > 0 && PortMouse::PlayerStickSet(player, key), 100 + key)) {
    PortMouse::ClearPlayerStick(player, key);
    Changed(true);
  }
}

void SettingCheckbox(const char* label, int player, PlayerSetting key) {
  bool v = Setting(player, key) != 0.f;
  if (ImGui::Checkbox(label, &v)) SetSetting(player, key, v ? 1.f : 0.f);
  SettingMark(player, key);
}

void DrawStickRows(int p) {
  ImGui::SeparatorText("Stick aim");
  bool twinStick = PortMouse::StickEnabled(p);
  if (ImGui::Checkbox("Twin stick (right stick aims)##mp", &twinStick)) {
    PortMouse::SetPlayerStickEnabled(p, twinStick);
    Changed(true);
  }
  StickMark(p, PortMouse::kStick_Enabled);
  if (!PortMouse::StickEnabled(p)) return;
  float rate = PortMouse::StickAimRate(p);
  if (ImGui::SliderFloat("Stick aim speed##mp", &rate, 100.f, 8100.f, "%.0f px/s")) {
    PortMouse::SetPlayerStickAimRate(p, rate);
    Changed(true);
  }
  StickMark(p, PortMouse::kStick_Rate);
  bool split = PortMouse::StickAimSplit(p);
  if (ImGui::Checkbox("Separate vertical speed##mpstick", &split)) {
    if (split) PortMouse::SetPlayerStickAimRateY(p, PortMouse::StickAimRate(p));
    PortMouse::SetPlayerStickAimSplit(p, split);
    Changed(true);
  }
  StickMark(p, PortMouse::kStick_Split);
  if (PortMouse::StickAimSplit(p)) {
    float rateY = PortMouse::StickAimRateY(p);
    if (ImGui::SliderFloat("Vertical stick aim speed##mp", &rateY, 100.f, 8100.f, "%.0f px/s")) {
      PortMouse::SetPlayerStickAimRateY(p, rateY);
      Changed(true);
    }
    StickMark(p, PortMouse::kStick_RateY);
  }
}

void DrawButtonRows(int p) {
  ImGui::SeparatorText("Buttons and aiming");
  // Original experience (Game > Cutscenes) reads these as retail for every player.
  const bool locked = PortOverlay::OriginalExperience();
  ImGui::BeginDisabled(locked);
  SettingCheckbox("Toggle Lock-On##mp", p, PortOverlay::kPlayer_LockOnToggle);
  SettingCheckbox("Sticky Charge##mp", p, PortOverlay::kPlayer_StickyCharge);
  SettingCheckbox("Remastered charge (rapid fire)##mp", p, PortOverlay::kPlayer_RapidCharge);
  SettingCheckbox("Auto-aim##mp", p, PortOverlay::kPlayer_AutoAim);
  ImGui::EndDisabled();
  SettingCheckbox("Lock-on free aim##mp", p, PortOverlay::kPlayer_LockOnFreeAim);
}

void DrawGyroRows(int p) {
  ImGui::SeparatorText("Gyro aim");
  // Off is the gyro_aim switch, the rest gyro_mode 1..4, as on Controls > Touch & gyro.
  const char* gyroModes[] = {"Off", "Hold to aim", "Always aim", "Hold to pause", "Toggle to aim"};
  const bool gyroAim = Setting(p, PortOverlay::kPlayer_GyroAim) != 0.f;
  int gyroMode = gyroAim ? std::clamp(int(Setting(p, PortOverlay::kPlayer_GyroMode)), PortOverlay::kGyroModeMin,
                                      PortOverlay::kGyroModeMax)
                         : 0;
  if (ImGui::Combo("Gyro mode##mp", &gyroMode, gyroModes, IM_ARRAYSIZE(gyroModes))) {
    SetSetting(p, PortOverlay::kPlayer_GyroAim, gyroMode > 0 ? 1.f : 0.f);
    if (gyroMode > 0) SetSetting(p, PortOverlay::kPlayer_GyroMode, float(gyroMode));
  }
  // One mark for both keys: the combo shows them as one setting.
  const bool modeSet = p > 0 && (PortOverlay::PlayerSettingSet(p, PortOverlay::kPlayer_GyroAim) ||
                                 PortOverlay::PlayerSettingSet(p, PortOverlay::kPlayer_GyroMode));
  if (OverrideMark(p, modeSet, PortOverlay::kPlayer_GyroAim)) {
    PortOverlay::ClearPlayerSetting(p, PortOverlay::kPlayer_GyroAim);
    PortOverlay::ClearPlayerSetting(p, PortOverlay::kPlayer_GyroMode);
  }
  ItemHelp("Turns this player's view with their own pad's gyro. Source, pointer speed and the rest "
           "are on Controls > Touch & gyro and apply to every player.");
  ImGui::BeginDisabled(!gyroAim);
  float sensitivity = Setting(p, PortOverlay::kPlayer_GyroSensitivity);
  if (ImGui::SliderFloat("Gyro sensitivity##mp", &sensitivity, PortOverlay::kGyroSensitivityMin,
                         PortOverlay::kGyroSensitivityMax, "%.2fx")) {
    SetSetting(p, PortOverlay::kPlayer_GyroSensitivity, sensitivity);
  }
  SettingMark(p, PortOverlay::kPlayer_GyroSensitivity);
  bool split = Setting(p, PortOverlay::kPlayer_GyroSplit) != 0.f;
  if (ImGui::Checkbox("Separate vertical speed##mpgyro", &split)) {
    if (split) SetSetting(p, PortOverlay::kPlayer_GyroSensitivityY, Setting(p, PortOverlay::kPlayer_GyroSensitivity));
    SetSetting(p, PortOverlay::kPlayer_GyroSplit, split ? 1.f : 0.f);
  }
  SettingMark(p, PortOverlay::kPlayer_GyroSplit);
  if (Setting(p, PortOverlay::kPlayer_GyroSplit) != 0.f) {
    float sensitivityY = Setting(p, PortOverlay::kPlayer_GyroSensitivityY);
    if (ImGui::SliderFloat("Vertical gyro sensitivity##mp", &sensitivityY, PortOverlay::kGyroSensitivityMin,
                           PortOverlay::kGyroSensitivityMax, "%.2fx")) {
      SetSetting(p, PortOverlay::kPlayer_GyroSensitivityY, sensitivityY);
    }
    SettingMark(p, PortOverlay::kPlayer_GyroSensitivityY);
  }
  ImGui::EndDisabled();
}

bool AnyOverride(int p) {
  for (int k = 0; k < PortOverlay::kPlayerSettingCount; ++k) {
    if (PortOverlay::PlayerSettingSet(p, PlayerSetting(k))) return true;
  }
  for (int k = PortMouse::kStick_Enabled; k <= PortMouse::kStick_RateY; ++k) {
    if (PortMouse::PlayerStickSet(p, PortMouse::StickKey(k))) return true;
  }
  return false;
}

} // namespace

void DrawMultiplayerTab() {
  static const char* const kPlayers[] = {"Player 1", "Player 2", "Player 3", "Player 4"};
  for (int i = 0; i < PortOverlay::kSettingPlayers; ++i) {
    if (i > 0) ImGui::SameLine();
    if (ImGui::RadioButton(kPlayers[i], sPlayer == i)) sPlayer = i;
  }
  const int p = sPlayer;
  ImGui::PushID(p);

  ImGui::SeparatorText("Controller");
  PortInputRemap::DrawPlayerController(static_cast<unsigned>(p), "Controller##mp");
  ItemHelp("The controller this player plays with. Choosing one already used by another player "
           "moves it here.");

  // This player's Controls > Remap (buttons, beams, visors, chords, contexts, presets).
  ImGui::SeparatorText("Remap");
  PortInputRemap::DrawForPlayer(static_cast<unsigned>(p));

  ImGui::SeparatorText("Settings");
  if (p == 0) {
    ImGui::TextDisabled("Player 1's settings are the Controls tab's; changing them here changes them there.");
  } else {
    ImGui::BeginDisabled(!AnyOverride(p));
    if (ImGui::Button("Reset to Player 1")) {
      PortOverlay::ClearPlayerSettings(p);
      PortMouse::ClearPlayerStick(p);
      Changed(true);
    }
    ImGui::EndDisabled();
    ItemHelp("Drops this player's own values; every row follows Player 1 again.");
  }

  DrawStickRows(p);
  DrawButtonRows(p);
  DrawGyroRows(p);
  ImGui::PopID();
}

} // namespace PortDebug
