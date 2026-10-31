// F1 overlay: The Controls page. State and helpers shared with the other parts are in
// debug_ui_internal.h.

#include "debug_ui_internal.h"

#include "../input/port_input_devices.h"
#include "port_controls.h"
#include "port_gyro_motion.h"
#include "port_input_remap_ui.h"
#include "port_mouse.h"
#include "port_overlay.h"
#include "port_touch.h"
#include "port_entry.h"

#include "MetroidPrime/Player/CGameOptions.hpp"
#include "MetroidPrime/Player/CGameState.hpp"

#include <SDL3/SDL_gamepad.h>
#include <dolphin/pad.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace PortDebug {

void DrawControlsOptions() {
  // Issue #5: the one-click way to play with mouse and keyboard, first on the page.
  ImGui::SeparatorText("Quick setup");
  if (ImGui::Button("Use mouse & keyboard")) {
    PortInputRemap::ApplyPreset(PortInput::Family::Keyboard, "mouse");
  }
  ItemHelp("Turns on mouse aim and the Mouse & keyboard layout: WASD to move, mouse to aim, left "
           "click to fire, Space to jump, Q to lock on. Replaces your own keyboard bindings; "
           "Controls > Remap > Presets... has the others.");
  SameLineAfterHelp();
  ImGui::TextDisabled(PortMouse::Enabled() ? "Mouse aim is on." : "Mouse aim is off.");
  // A GameCube pad has no right stick click for the dual-stick layout (as Remap's Presets...).
  ImGui::BeginDisabled(PADIsGCAdapter(0) != 0);
  if (ImGui::Button("Use twin-stick controller")) {
    PortInputRemap::ApplyPreset(PortInput::Family::Pad, "remastered");
  }
  ImGui::EndDisabled();
  ItemHelp("Turns on Twin Stick Aim and the Remastered layout for all controllers: left stick moves, "
           "right stick aims, RT fire, LT lock on, A jump, X morph ball, right stick click free look. "
           "Replaces your own controller bindings; Controls > Remap > Presets... has Modern and the "
           "others. Not for a GameCube controller, which has no right stick click.");
  SameLineAfterHelp();
  ImGui::TextDisabled(PortMouse::StickEnabled() ? "Twin-stick aim is on." : "Twin-stick aim is off.");

  ImGui::SeparatorText("On-screen prompts");
  int glyphs = sPromptGlyphs;
  if (ImGui::Combo("Button glyphs", &glyphs,
                   "Follow the input in use\0" "Xbox\0" "PlayStation\0" "Nintendo Switch\0" "GameCube\0"
                   "Keyboard & mouse\0")) {
    sPromptGlyphs = std::clamp(glyphs, static_cast< int >(PortOverlay::kPromptGlyphs_Auto),
                               static_cast< int >(PortOverlay::kPromptGlyphs_Keyboard));
    Changed(true);
  }
  ItemHelp("Which buttons the game's prompts and tutorials show. Follow: the keyboard, controller or "
           "touch layout you last used. GameCube shows the game's own art; Original experience "
           "forces it.");

  ImGui::SeparatorText("Buttons");
  // Original experience (Game > Cutscenes) reads the tweaks below as retail.
  const bool locked = PortOverlay::OriginalExperience();
  ImGui::BeginDisabled(locked);
  Changed(ImGui::Checkbox("Toggle Lock-On", &sLockOnToggle));
  ItemHelp("Press L once to lock on, scan or grapple, and again to let go. With no target, L works "
           "as usual.");
  Changed(ImGui::Checkbox("Sticky Charge", &sStickyCharge));
  ItemHelp("Tap to fire as usual. Hold fire, let go, and the beam keeps charging; press fire again "
           "to shoot. Needs the Charge Beam.");
  Changed(ImGui::Checkbox("Remastered charge (rapid fire)", &sRapidCharge));
  ItemHelp("As in Metroid Prime Remastered: holding fire after the press shot fires a few quick "
           "shots. Power Beam only.");
  ImGui::EndDisabled();

  ImGui::SeparatorText("Aiming");
  if (port::entry::IsTrilogy(port::entry::CurrentDiscVersion())) {
    bool radial = sTrilogyRadialSelectors;
    if (ImGui::Checkbox("Native Trilogy radial selectors", &radial)) {
      PortOverlay::SetTrilogyRadialSelectors(radial);
      Changed(true);
    }
    ItemHelp("Opt-in, single-player only. Bind Beam menu / Visor menu below. Hold, move mouse or "
             "right stick, release to select. Center selects Power / Combat; pause or morph cancels.");
  }
  ImGui::BeginDisabled(locked);
  Changed(ImGui::Checkbox("Auto-aim", &sAutoAim));
  ItemHelp("On, as the original: without Lock-On, shots home in on the enemy nearest the screen "
           "centre. Off: shots go where you aim.");
  ImGui::EndDisabled();
  Changed(ImGui::Checkbox("Pointer aim (Trilogy style)", &sPointerAim));
  ItemHelp("Mouse, right stick, gyro or a touch drag moves a crosshair and shots follow it; the view "
           "turns when it nears the edge. Needs Mouse aim, Twin stick or Touch aim.");
  SameLineAfterHelp();
  Changed(ImGui::Checkbox("Lock-on free aim", &sLockOnFreeAim));
  ItemHelp("As in Remastered: while locked on, the mouse, right stick, gyro or a touch drag moves a "
           "crosshair and shots follow it. Centred, shots and missiles go for the locked target as "
           "before. Needs Mouse aim, Twin stick or Touch aim.");
  ImGui::BeginDisabled(!sPointerAim);
  if (ImGui::SliderInt("Dead box", &sPointerAimBox, PortOverlay::kPointerAimBoxMin, PortOverlay::kPointerAimBoxMax,
                       "%d%%")) {
    sPointerAimBox = std::clamp(sPointerAimBox, PortOverlay::kPointerAimBoxMin, PortOverlay::kPointerAimBoxMax);
    Changed(true);
  }
  ImGui::SetItemTooltip("How far the crosshair moves from the centre (percent of the half screen) "
                        "before the view turns.");
  if (ImGui::SliderInt("Edge turn speed", &sPointerAimTurn, PortOverlay::kPointerAimTurnMin,
                       PortOverlay::kPointerAimTurnMax, "%d deg/s")) {
    sPointerAimTurn = std::clamp(sPointerAimTurn, PortOverlay::kPointerAimTurnMin, PortOverlay::kPointerAimTurnMax);
    Changed(true);
  }
  ImGui::SetItemTooltip("Turn speed with the crosshair at the screen edge.");
  ImGui::EndDisabled();
  Changed(ImGui::Checkbox("Reset aim resets pointer and camera", &sResetAimBoth));
  ItemHelp("Reset aim (unbound by default) recentres the crosshair with pointer aim on, or levels "
           "the camera with it off. On: it does both.");

  ImGui::SeparatorText("Menus");
  Changed(ImGui::Checkbox("Point and click menus", &sPointerMenus));
  ItemHelp("Mouse and touch in the pause screens and the map: hover and click items, drag and "
           "scroll the map, right click to go back. Gamepad and keyboard work as before.");
  Changed(ImGui::Checkbox("Tap minimap for map", &sMinimapTap));
  ItemHelp("A tap or click on the HUD minimap opens the map screen, like the map button.");
  ImGui::BeginDisabled(!sPointerMenus);
  Changed(ImGui::Checkbox("Swap map finger gestures", &sMapSwapFingers));
  ItemHelp("Touch only: one finger pans the map and two rotate it, instead of two panning and "
           "one rotating.");
  ImGui::EndDisabled();

  // The game's own option, so it is the same switch as the pause menu's and is
  // saved with the other game options.
  ImGui::SeparatorText("Rumble");
  ImGui::BeginDisabled(gpGameState == nullptr);
  bool rumble = gpGameState != nullptr && gpGameState->GameOptions().GetIsRumbleEnabled();
  if (ImGui::Checkbox("Rumble", &rumble) && gpGameState != nullptr) {
    gpGameState->GameOptions().SetIsRumbleEnabled(rumble);
  }
  ImGui::EndDisabled();
  ItemHelp("Same switch as Rumble in the pause menu.");

  ImGui::SeparatorText("Map");
  Changed(ImGui::Checkbox("Invert map rotate X", &sMapInvertX));
  ItemHelp("Flips which way the map turns when you rotate it left and right. The game turns the "
           "map the opposite way to the stick.");
  SameLineAfterHelp();
  Changed(ImGui::Checkbox("Invert map rotate Y", &sMapInvertY));
  ItemHelp("Flips which way the map tilts up and down. Both on gives camera-style rotation.");
}

void DrawControlsKeyboardMouse() {
  ImGui::SeparatorText("Mouse aim");
  bool mouseAim = PortMouse::Enabled();
  if (ImGui::Checkbox("Mouse aim", &mouseAim)) {
    PortMouse::SetEnabled(mouseAim);
    Changed(true);
  }
  // Shown on the Quake/Source/Apex scale (0.022 degrees a count per unit), stored in radians.
  float sourceSensitivity = PortMouse::SourceFromRadians(PortMouse::Sensitivity());
  if (ImGui::SliderFloat("Sensitivity", &sourceSensitivity, 0.05f, 30.f, "%.3f",
                         ImGuiSliderFlags_NoRoundToFormat)) {
    PortMouse::SetSensitivity(PortMouse::RadiansFromSource(sourceSensitivity));
    Changed(true);
  }
  ImGui::SetItemTooltip("Same scale as Quake, Source and Apex games: the same number turns the "
                        "same.\nCtrl+click to type a value.");
  bool mouseSplit = PortMouse::SensitivitySplit();
  if (ImGui::Checkbox("Separate vertical speed##mouse", &mouseSplit)) {
    if (mouseSplit) PortMouse::SetSensitivityY(PortMouse::Sensitivity());
    PortMouse::SetSensitivitySplit(mouseSplit);
    Changed(true);
  }
  ItemHelp("Own sensitivity for looking up and down; Sensitivity then sets left and right only.");
  if (PortMouse::SensitivitySplit()) {
    float sourceSensitivityY = PortMouse::SourceFromRadians(PortMouse::SensitivityY());
    if (ImGui::SliderFloat("Vertical sensitivity", &sourceSensitivityY, 0.05f, 30.f, "%.3f",
                           ImGuiSliderFlags_NoRoundToFormat)) {
      PortMouse::SetSensitivityY(PortMouse::RadiansFromSource(sourceSensitivityY));
      Changed(true);
    }
  }
  int mouseDpi = PortMouse::MouseDpi();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
  if (ImGui::InputInt("Mouse DPI", &mouseDpi, 0, 0)) {
    PortMouse::SetMouseDpi(mouseDpi);
    Changed(true);
  }
  ImGui::SameLine();
  const float cmPer360 = PortMouse::CentimetresPer360(PortMouse::Sensitivity(), float(PortMouse::MouseDpi()));
  ImGui::TextDisabled("%.1f cm (%.1f in) per 360 degrees", cmPer360, cmPer360 / 2.54f);
  ImGui::SetItemTooltip("Only used for this readout.");
  bool invertX = PortMouse::InvertX();
  if (ImGui::Checkbox("Invert X", &invertX)) {
    PortMouse::SetInvertX(invertX);
    Changed(true);
  }
  ImGui::SameLine();
  bool invertY = PortMouse::InvertY();
  if (ImGui::Checkbox("Invert Y", &invertY)) {
    PortMouse::SetInvertY(invertY);
    Changed(true);
  }
  // The game's crosshair input is a look control the port never presses, so the port holds the
  // reticle on while its own aim owns the view.
  Changed(ImGui::Checkbox("Crosshair", &sAimCrosshair));
  ImGui::SameLine();
  ImGui::BeginDisabled(!sAimCrosshair);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
  Changed(ImGui::SliderInt("Size", &sCrosshairSize, PortOverlay::kCrosshairSizeMin, PortOverlay::kCrosshairSizeMax,
                           "%d%%"));
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("Applies under mouse aim, twin stick and touch aim.");
}

void DrawControlsController() {
  ImGui::SeparatorText("Stick aim");
  bool twinStick = PortMouse::StickEnabled();
  if (ImGui::Checkbox("Twin stick (right stick aims)", &twinStick)) {
    PortMouse::SetStickEnabled(twinStick);
    Changed(true);
  }
  ItemHelp("Aims with the right stick like the mouse, so it no longer free-looks, and the R trigger stops "
           "free-looking (it stays Spider Ball in the Morph Ball). Fire stays on A; change it in Controls > Remap.");
  // Shown only with twin stick on, as MP1 does.
  if (PortMouse::StickEnabled()) {
    float rate = PortMouse::StickAimRate();
    if (ImGui::SliderFloat("Stick aim speed", &rate, 100.f, 8100.f, "%.0f px/s")) {
      PortMouse::SetStickAimRate(rate);
      Changed(true);
    }
    ItemHelp(PortMouse::StickAimSplit() ? "How fast the right stick turns left and right at full tilt."
                                        : "How fast the right stick turns the camera at full tilt.");
    bool split = PortMouse::StickAimSplit();
    if (ImGui::Checkbox("Separate vertical speed", &split)) {
      // Start vertical where the shared speed was, so turning the split on changes nothing yet.
      if (split) PortMouse::SetStickAimRateY(PortMouse::StickAimRate());
      PortMouse::SetStickAimSplit(split);
      Changed(true);
    }
    ItemHelp("Own speed for looking up and down; Stick aim speed then sets left and right only.");
    if (PortMouse::StickAimSplit()) {
      float rateY = PortMouse::StickAimRateY();
      if (ImGui::SliderFloat("Vertical stick aim speed", &rateY, 100.f, 8100.f, "%.0f px/s")) {
        PortMouse::SetStickAimRateY(rateY);
        Changed(true);
      }
    }
    // The game's own option, saved with its settings; free look uses it too.
    ImGui::BeginDisabled(gpGameState == nullptr);
    bool invertY = gpGameState != nullptr && gpGameState->GameOptions().GetInvertYAxis();
    if (ImGui::Checkbox("Invert stick aim Y (the game's Reverse Y Axis)", &invertY) && gpGameState != nullptr) {
      gpGameState->GameOptions().SetInvertYAxis(invertY);
    }
    ImGui::EndDisabled();
  }
  const s32 controllerIndex = PADGetIndexForPort(PAD_CHAN0);
  if (SDL_Gamepad* pad = controllerIndex >= 0 ? PADGetSDLGamepadForIndex(static_cast< u32 >(controllerIndex)) : nullptr;
      pad != nullptr && SDL_IsJoystickVirtual(SDL_GetGamepadID(pad))) {
    // Aurora ignores mappings on the virtual touch pad.
    ImGui::TextWrapped("The touch controls always use the GameCube layout; presets and remaps apply to "
                       "real controllers only.");
  } else {
    PortControls::DrawDeadZones();
  }
}

// MP1 draws the touch rows on Android only; this port's touch overlay also runs on a desktop (a
// touch screen or the mouse), so they are drawn everywhere, as before.
void DrawControlsTouchGyro() {
  ImGui::SeparatorText("Touch controls");
  // First on the page: the layout is what touch players come here to change.
  if (ImGui::Button("Edit layout")) {
    // The touch overlay takes the request and opens its editor once this closes (MP1).
    PortTouch::RequestEdit();
    PortOverlay::ShowPage("off");
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset margins")) {
    PortTouch::SetSideMarginDp(16.f);
    PortTouch::SetStickInsetDp(32.f);
    PortTouch::SetButtonInsetDp(0.f);
  }
  // One help line for the pair: inline help under the first would break the row.
  ItemHelp("Edit layout: move, resize, fade, hide, delete or add on-screen buttons. "
           "Reset margins: sets the three margins below back to their defaults.");
  bool controls = PortTouch::ControlsEnabled();
  if (ImGui::Checkbox("On-screen controls", &controls)) {
    PortTouch::SetControlsEnabled(controls);
  }
  ItemHelp("The phone's touch overlay; its layout is touch.cfg in the user folder.");
  ImGui::BeginDisabled(!controls);
  bool b = PortTouch::Colors();
  if (ImGui::Checkbox("Coloured buttons", &b)) {
    PortTouch::SetColors(b);
  }
  ItemHelp("GameCube colours: green A, red B, yellow C-stick, purple Z.");
  b = PortTouch::Labels();
  if (ImGui::Checkbox("Button descriptions", &b)) {
    PortTouch::SetLabels(b);
  }
  ItemHelp("Shows what each button does (Fire, Jump, Lock...) next to its letter.");
  b = PortTouch::Turbo();
  if (ImGui::Checkbox("Turbo fire button", &b)) {
    PortTouch::SetTurbo(b);
  }
  ItemHelp("Adds a Turbo button next to Fire: hold it to fire as fast as the game allows.");
  b = PortTouch::FloatingStick();
  if (ImGui::Checkbox("Floating left stick", &b)) {
    PortTouch::SetFloatingStick(b);
  }
  ItemHelp("The left stick appears wherever your thumb lands on the left half of the screen.");
  const bool classic = PortTouch::Classic();
  const bool twin = PortTouch::TwinStick();
  int layoutIndex = classic ? 1 : (twin ? 2 : 0);
  static const char* const kTouchLayouts[] = {"Default", "Classic GameCube", "Twin stick (Remastered)"};
  if (ImGui::Combo("Layout", &layoutIndex, kTouchLayouts, 3)) {
    // CycleLayout steps Default -> Classic -> Twin -> Default.
    for (int i = 0; i < 3 && (PortTouch::Classic() ? 1 : (PortTouch::TwinStick() ? 2 : 0)) != layoutIndex; ++i) {
      PortTouch::CycleLayout();
    }
    if (layoutIndex == 2) {
      // Remastered picks beams with Y + the D-pad, so twin starts with the D-pad, not the wheels.
      PortTouch::SetWheelsEnabled(false);
    }
  }
  ItemHelp("Default: GameCube buttons, drag the screen to aim, Visor and Beam wheels.\n"
           "Classic GameCube: adds the C-stick and D-pad.\n"
           "Twin stick (Remastered): a right stick that aims.");
  bool aim = PortTouch::AimEnabled();
  if (ImGui::Checkbox("Touch aim", &aim)) {
    PortTouch::SetAimEnabled(aim);
  }
  ItemHelp("Drag the free screen area to turn and look around.");
  ImGui::BeginDisabled(!classic && !twin);
  b = PortTouch::WheelsEnabled();
  if (ImGui::Checkbox("Beam and visor wheels", &b)) {
    PortTouch::SetWheelsEnabled(b);
  }
  ItemHelp("Replaces the D-pad with Visor and Beam buttons: hold, slide to one, let go. "
           "Let go in the middle to cancel.");
  ImGui::EndDisabled();
  ImGui::BeginDisabled(!((!classic && !twin) || PortTouch::WheelsEnabled()));
  b = PortTouch::VisorTapScan();
  if (ImGui::Checkbox("Tap Visor for Scan Visor", &b)) {
    PortTouch::SetVisorTapScan(b);
  }
  ItemHelp("A quick tap on Visor switches to the Scan Visor.");
  ImGui::EndDisabled();
  float speed = PortTouch::AimSpeed();
  if (ImGui::SliderFloat("Touch aim speed", &speed, 0.5f, 6.f, "%.2f px/dp")) {
    PortTouch::SetAimSpeed(speed);
  }
  b = PortTouch::AimSplit();
  if (ImGui::Checkbox("Separate vertical speed##touch", &b)) {
    if (b) PortTouch::SetAimSpeedY(PortTouch::AimSpeed());
    PortTouch::SetAimSplit(b);
  }
  if (PortTouch::AimSplit()) {
    float speedY = PortTouch::AimSpeedY();
    if (ImGui::SliderFloat("Vertical touch aim speed", &speedY, 0.5f, 6.f, "%.2f px/dp")) {
      PortTouch::SetAimSpeedY(speedY);
    }
  }
  b = PortTouch::SpringSwipeEnabled();
  if (ImGui::Checkbox("Spring Ball on swipe up", &b)) {
    PortTouch::SetSpringSwipe(b);
  }
  ItemHelp("In morph ball, swipe up to jump. Needs the Spring Ball option.");
  float v = PortTouch::Opacity();
  if (ImGui::SliderFloat("Opacity", &v, 10.f, 100.f, "%.0f%%")) {
    PortTouch::SetOpacity(v);
  }
  ItemHelp("How visible all on-screen controls are. Edit layout sets single controls.");
  v = PortTouch::SideMarginDp();
  if (ImGui::SliderFloat("Side margin", &v, 0.f, 300.f, "%.0f dp")) {
    PortTouch::SetSideMarginDp(v);
  }
  ItemHelp("Moves every control in from the left and right edges (curved screens, cases).");
  v = PortTouch::StickInsetDp();
  if (ImGui::SliderFloat("Stick inset", &v, 0.f, 300.f, "%.0f dp")) {
    PortTouch::SetStickInsetDp(v);
  }
  v = PortTouch::ButtonInsetDp();
  if (ImGui::SliderFloat("Button inset", &v, 0.f, 300.f, "%.0f dp")) {
    PortTouch::SetButtonInsetDp(v);
  }
  ItemHelp("Moves the face buttons (and the classic C-stick) further in from the edge.");
  ImGui::EndDisabled();

  ImGui::SeparatorText("Gyro aim");
  // MP1's mode list; Off is the gyro_aim switch, the rest are gyro_mode 1..4.
  const char* gyroModes[] = {"Off", "Hold to aim", "Always aim", "Hold to pause", "Toggle to aim"};
  int gyroMode =
      sGyroAim ? std::clamp(sGyroMode, PortOverlay::kGyroModeMin, PortOverlay::kGyroModeMax) : 0;
  if (ImGui::Combo("Mode", &gyroMode, gyroModes, IM_ARRAYSIZE(gyroModes))) {
    sGyroAim = gyroMode > 0;
    if (gyroMode > 0) {
      sGyroMode = std::clamp(gyroMode, PortOverlay::kGyroModeMin, PortOverlay::kGyroModeMax);
    }
    Changed(true);
  }
  ItemHelp("Tilt the pad or phone to aim (with Pointer aim, to move the pointer). Hold to aim: only "
           "while the Gyro aim button is held. Hold to pause: always, except while it is held (to "
           "recentre). Toggle: press to start and stop. The button is right stick click or left Ctrl "
           "(Remap page). Needs mouse aim, twin stick or the touch controls' aim.");
  Changed(ImGui::Checkbox("Spring Ball on flick", &sSpringFlick));
  ItemHelp("A quick upward flick of the pad or phone makes a Spring Ball jump in Morph Ball (needs "
           "Spring Ball on, under Morph ball). Works with Gyro aim off.");
  ImGui::BeginDisabled(!sSpringFlick);
  if (ImGui::SliderFloat("Flick strength", &sSpringFlickRate, PortOverlay::kSpringFlickRateMin,
                         PortOverlay::kSpringFlickRateMax, "%.1f rad/s")) {
    sSpringFlickRate =
        std::clamp(sSpringFlickRate, PortOverlay::kSpringFlickRateMin, PortOverlay::kSpringFlickRateMax);
    Changed(true);
  }
  ItemHelp("How fast the flick must be. Lower triggers more easily.");
  ImGui::EndDisabled();
  ImGui::BeginDisabled(!sGyroAim && !sSpringFlick);
  const char* gyroSources[] = {"Auto", "Controller", "Phone"};
  if (ImGui::Combo("Source", &sGyroSource, gyroSources, 3)) {
    Changed(true);
  }
  ImGui::Text("Gyro: %s", PortInputDevices::GyroStatus());
  ImGui::EndDisabled();
  ImGui::BeginDisabled(!sGyroAim);
  // With pointer aim the gyro moves the pointer at its own speed; the camera sensitivity then only
  // matters if the gyro also turns the camera.
  // Lock-on free aim's pointer moves at Pointer speed too, with or without pointer aim.
  const bool gyroPointer = sPointerAim;
  if (gyroPointer || sLockOnFreeAim) {
    if (ImGui::SliderFloat("Pointer speed##gyro", &sGyroCursorAngle, PortOverlay::kGyroCursorAngleMin,
                           PortOverlay::kGyroCursorAngleMax, "%.0f deg across the screen")) {
      sGyroCursorAngle =
          std::clamp(sGyroCursorAngle, PortOverlay::kGyroCursorAngleMin, PortOverlay::kGyroCursorAngleMax);
      Changed(true);
    }
    ItemHelp("How far the pad turns to sweep the pointer across the screen. Smaller is faster.");
  }
  // A lock owns the view, so this only matters under pointer aim.
  if (gyroPointer) {
    Changed(ImGui::Checkbox("Gyro also turns the camera", &sGyroCursorCamera));
    ItemHelp("Also turns the camera with the gyro, at the Sensitivity below, while it moves the "
             "pointer.");
  }
  ImGui::BeginDisabled(gyroPointer && !sGyroCursorCamera);
  // The ## suffix keeps its ImGui id apart from the mouse Sensitivity slider.
  if (ImGui::SliderFloat("Sensitivity##gyro", &sGyroSensitivity, PortOverlay::kGyroSensitivityMin,
                         PortOverlay::kGyroSensitivityMax, "%.2fx")) {
    sGyroSensitivity =
        std::clamp(sGyroSensitivity, PortOverlay::kGyroSensitivityMin, PortOverlay::kGyroSensitivityMax);
    Changed(true);
  }
  ItemHelp("How far the camera turns per turn of the pad: at 1.00x a 90 degree turn gives 90. With "
           "pointer aim it applies only if the gyro also turns the camera.");
  if (ImGui::Checkbox("Separate vertical speed##gyro", &sGyroSplit)) {
    if (sGyroSplit) sGyroSensitivityY = sGyroSensitivity;
    Changed(true);
  }
  ItemHelp("Own sensitivity for tilting up and down; Sensitivity then sets left and right only. "
           "With pointer aim it scales the pointer's vertical travel.");
  if (sGyroSplit) {
    if (ImGui::SliderFloat("Vertical sensitivity##gyro", &sGyroSensitivityY, PortOverlay::kGyroSensitivityMin,
                           PortOverlay::kGyroSensitivityMax, "%.2fx")) {
      sGyroSensitivityY =
          std::clamp(sGyroSensitivityY, PortOverlay::kGyroSensitivityMin, PortOverlay::kGyroSensitivityMax);
      Changed(true);
    }
  }
  ImGui::EndDisabled();
  // Listed by how common they are; the setting keeps PortGyro::Space's numbers.
  static constexpr int kSpaceOrder[] = {PortGyro::kPlayer, PortGyro::kWorld, PortGyro::kLocal, PortGyro::kLocalRoll,
                                        PortGyro::kLocalBoth};
  static constexpr const char* kSpaceNames[] = {"Player (turn)", "World (turn)", "Local yaw", "Local roll",
                                                "Local yaw and roll"};
  int spaceRow = 0;
  for (int i = 0; i < PortGyro::kSpaceCount; ++i) {
    if (kSpaceOrder[i] == sGyroSpace) {
      spaceRow = i;
    }
  }
  if (ImGui::Combo("Space", &spaceRow, kSpaceNames, PortGyro::kSpaceCount)) {
    sGyroSpace = kSpaceOrder[std::clamp(spaceRow, 0, PortGyro::kSpaceCount - 1)];
    Changed(true);
  }
  ItemHelp("Which pad turn moves the pointer left and right. Player: left/right turns, flat or "
           "upright (the usual choice). World: turns about the real vertical. Local yaw: flat "
           "turns. Local roll: upright turns (handheld). Player and World need an accelerometer; "
           "without one they act as Local yaw.");
  static constexpr const char* kCalibrateNames[] = {"Off", "Always", "Only in menus"};
  int autoCalibrate = std::clamp(sGyroAutoCalibrate, 0, 2);
  if (ImGui::Combo("Auto calibrate", &autoCalibrate, kCalibrateNames, 3)) {
    sGyroAutoCalibrate = autoCalibrate;
    Changed(true);
  }
  ItemHelp("Removes gyro drift while the pad is still, so the pointer doesn't creep; put it down "
           "for a couple of seconds. Needs an accelerometer. Only in menus learns only with this "
           "menu open or outside free aim, and keeps it while you play.");
  const float countdown = PortInputDevices::GyroRecalibrationCountdown();
  const float manual = PortInputDevices::GyroManualCalibrationProgress();
  if (manual >= 0.f) {
    ImGui::BeginDisabled();
    ImGui::Button(("Calibrating, keep the pad still... " + std::to_string(int(manual * 100.f)) + "%").c_str());
    ImGui::EndDisabled();
  } else if (countdown >= 0.f) {
    ImGui::BeginDisabled();
    ImGui::Button(("Calibrating in " + std::to_string(int(std::ceil(countdown))) + "...").c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      PortInputDevices::CancelGyroRecalibration();
    }
  } else if (ImGui::Button("Recalibrate")) {
    PortInputDevices::StartGyroRecalibration();
  }
  ItemHelp("Waits 5 seconds so you can put the pad down, then measures drift for 3. Needs the gyro "
           "in use.");
  const float confidence = PortInputDevices::GyroCalibrationConfidence();
  if (confidence >= 0.f) {
    ImGui::Text("Calibration: %d%%%s", int(confidence * 100.f + 0.5f),
                PortInputDevices::GyroCalibrating() ? " (calibrating)"
                : confidence >= 0.99f              ? " (done)"
                                                   : "");
  } else {
    ImGui::TextDisabled("Calibration: no accelerometer");
  }
  if (ImGui::SliderFloat("Smoothing", &sGyroSmoothing, 0.f, PortOverlay::kGyroThresholdMax,
                         sGyroSmoothing > 0.f ? "below %.0f deg/s" : "off")) {
    sGyroSmoothing = std::clamp(sGyroSmoothing, 0.f, PortOverlay::kGyroThresholdMax);
    Changed(true);
  }
  ItemHelp("Averages slow turns to hide sensor jitter; faster turns stay direct. Adds a little lag "
           "to fine aim.");
  if (ImGui::SliderFloat("Tightening", &sGyroTightening, 0.f, PortOverlay::kGyroThresholdMax,
                         sGyroTightening > 0.f ? "below %.0f deg/s" : "off")) {
    sGyroTightening = std::clamp(sGyroTightening, 0.f, PortOverlay::kGyroThresholdMax);
    Changed(true);
  }
  ItemHelp("Scales down turns slower than this so a pad held still doesn't wobble the aim. High "
           "values make fine aim sluggish.");
  ImGui::EndDisabled();
}

} // namespace PortDebug
