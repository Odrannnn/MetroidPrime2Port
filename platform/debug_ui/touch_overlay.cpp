// The touch overlay in C++: a faithful translation of Android's TouchControlsView.java, drawn with
// ImGui over the game and fed by an SDL event filter. The Java view is the reference: the same
// fractions, dp constants, tables, hit tests, press and release rules, colours, labels and saved
// layout string, so a layout made on a phone plays the same here. Every Java `nativeX(...)` call is
// the C++ function its JNI wrapper (android_jni.cpp) calls, called directly.
//
// What Java does with a Canvas becomes ImDrawList drawing in the pixels of the main viewport, and a
// Canvas layer opened for one control's own opacity (pushLayer/popLayer) becomes the `sLocal`
// multiplier: every colour goes through Argb(), which folds in F1's opacity (sGlobal) and the
// control's (sLocal).
//
// Android-only parts left out: the mouse (fromMouse, onHoverEvent — a mouse is not a finger on the
// overlay, though the layout editor takes a left-button mouse as one finger), performClick, and
// attach and detach (a view lifecycle with no C++ equivalent; the poll these scheduled runs from
// the draw).
//
// The layout editor is here too: its toolbar, the drag/pinch, the per-control opacity, hide, delete
// and reset, the Morph Ball page switch and the added buttons. The Java's pickAction AlertDialog is
// an ImGui window (the port has no dialogs), and Done saves through SerializeLayout into the same
// string Android writes.
//
// Wheel icons: IconTracer.java is ported below and traces the same outlines; as ImDrawList has no
// even-odd path fill, each outline is filled once into a texture rather than per draw.
#include "debug_ui_internal.h"

#if defined(PORT_TOUCH_OVERLAY)

#include "port_custom_actions.h"
#include "port_input_bindings.h"
#include "../input/port_input_devices.h"
#include "port_input_engine.h"
#include "port_overlay.h"
#include "port_photo.h"
#include "port_touch.h"
#include "port_pointer.h"
#include "port_prompts.h"
#include "port_radial.h"
#include "port_savestate.h"
#include "touch_pad.h"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <aurora/imgui.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979f;

// MP2 has no map-screen pan/zoom/rotate backend (docs/ANDROID.md); the map branches below are
// kept for fidelity with the Java view and never run.
template < typename... Args >
inline void NoMap(Args...) {}

// Touch target types.
enum Type { kLeftStick, kRightStick, kButton, kHide, kAim, kMapTap, kMapPan, kMapPan2, kWheel };

// Port-only actions, not game inputs.
constexpr int kToggleDebugOverlay = -1;
// Twin stick's Beam button: held, it turns the D-pad into the beam picker. Sends no GameCube button.
constexpr int kBeamShift = -2;
// The Turbo button: held, the game behaves as if Fire were mashed. Sends no GameCube button.
constexpr int kTurboFire = -3;
// A button added in the layout editor: its action is a touch: binding. Sends no GameCube button.
constexpr int kCustomButton = -4;
// The pointer menus' back arrow: GameCube B, whatever the remaps do with the overlay's buttons.
constexpr int kMenuBack = -5;
// Axis-held controls are tracked with ids above this, to share one press map.
constexpr int kAxisIdBase = 100;

// SDL gamepad ids: the port maps these onto the GameCube pad itself.
constexpr int kBtnSouth = 0, kBtnEast = 1, kBtnWest = 2, kBtnNorth = 3, kBtnStart = 6,
              kBtnRightShoulder = 10, kBtnDpadUp = 11, kBtnDpadDown = 12, kBtnDpadLeft = 13,
              kBtnDpadRight = 14;
constexpr int kAxisLeftX = 0, kAxisLeftY = 1, kAxisRightX = 2, kAxisRightY = 3, kAxisTriggerL = 4,
              kAxisTriggerR = 5;

// The layout's fractions of the height stop growing at this many dp, so a tablet gets phone-sized
// controls that keep their distance to the nearest edge.
constexpr float kMaxLayoutDp = 520.f;
constexpr float kStickRadius = 0.16f;    // drawn and read through, so the two cannot disagree
constexpr float kLeftStickReach = 1.5f;  // grab area, in stick radii
constexpr float kStickDeadZone = 0.12f;
// The GameCube C-stick: smaller, just below and left of the face buttons.
constexpr float kGcCstickFromRight = 0.60f;
constexpr float kGcCstickY = 0.76f;
constexpr float kGcCstickRadius = 0.115f;

// The GameCube pad's colours, as RGB; the fill alpha follows the press state.
constexpr uint32_t kGcGreen = 0x2FA864, kGcRed = 0xC8343A, kGcGrey = 0x8A8A94, kGcYellow = 0xE0C020,
                   kGcPurple = 0x6A4FB0;

// A's centre: from the right edge and from the top, in view heights. The cluster is laid out in
// heights around it, so it keeps the pad's shape whatever the screen's aspect.
constexpr float kGcAFromRight = 0.235f, kGcAY = 0.700f;

// The D-pad is one cross: its centre as fractions of the view, its arms in view heights.
constexpr float kDpadX = 0.115f, kDpadY = 0.335f, kDpadArm = 0.130f, kDpadHalf = 0.045f;
constexpr int kDpadButtons[4] = {kBtnDpadUp, kBtnDpadDown, kBtnDpadLeft, kBtnDpadRight};
constexpr int kDpadDx[4] = {0, 0, -1, 1};
constexpr int kDpadDy[4] = {-1, 1, 0, 0};

// Twin stick (Remastered): a diamond in Xbox positions, round A's spot.
constexpr float kTwinDiamond = 0.095f;
// The Xbox letter drawn big on each twin diamond button.
constexpr const char* kTwinLetters[4] = {"A", "B", "X", "Y"};

// The round buttons along the bottom: START and MENU on the left, the map and the eye on the right.
constexpr float kBottomButtonRadiusDp = 22.f, kBottomButtonSideDp = 28.f,
                kBottomButtonBottomDp = 12.f, kBottomButtonGapDp = 8.f;

// The Visor and Beam buttons and their wheels.
constexpr const char* kWheelButtonLabels[2] = {"Visor", "Beam"};
// Sectors run up, right, down, left, as the stock D-pad (visors) and C-stick (beams) do. Items are
// numbered as the native side does (MP2: visors Combat/Scan/Dark/Echo, beams Power/Dark/Light/Annihilator).
constexpr const char* kWheelLabels[2][4] = {{"Combat", "Dark", "Echo", "Scan"},
                                            {"Power", "Dark", "Annihil.", "Light"}};
constexpr int kWheelItems[2][4] = {{0, 3, 1, 2}, {0, 1, 3, 2}};
// The bit of WheelState that is set once there is a player.
constexpr uint32_t kWheelValidBit = 1u << 12;
// Set while Samus is morphed or morphing.
constexpr uint32_t kWheelMorphedBit = 1u << 13;
constexpr float kWheelButtonRadius = 0.072f, kWheelButtonGapDp = 8.f, kWheelButtonAnchorDp = 16.f;
constexpr float kWheelRadiusDp = 112.f, kWheelDeadDp = 30.f, kWheelIconDp = 44.f;

// A swipe up that springs the morph ball: at least this far, in dp, within this many ms.
constexpr float kSwipeUpDp = 40.f;
constexpr Uint64 kSwipeUpMs = 150;
// Finger spread, in dp, under which a pinch is ignored (the ratio blows up).
constexpr float kMapPinchMinDp = 20.f;
// Travel, in dp, past which a touch is a drag and not a tap (minimap, wheel, A).
constexpr float kMapTapSlopDp = 12.f;
// How far a Beam/Visor menu button's finger slides to reach a radial icon.
constexpr float kRadialDragDp = 72.f;
// Tells the game a finger is still down on the map, so it doesn't drift back.
constexpr Uint64 kMapPanKeepAliveMs = 100;
constexpr Uint64 kMapButtonPollMs = 200;
constexpr Uint64 kPhysicalInputPollMs = 250;
constexpr Uint64 kWheelTapMs = 250;

// Every control is one movable unit: an override offset in layout units, a size scale and an
// opacity, applied on top of the default position, which still follows the F1 margins.
constexpr int kCLStick = 0, kCCStick = 1, kCDpad = 2, kCA = 3, kCB = 4, kCX = 5, kCY = 6, kCL = 7,
              kCR = 8, kCZ = 9, kCVisor = 10, kCBeam = 11, kCStart = 12, kCMenu = 13, kCMap = 14,
              kCEye = 15;
// Twin stick's own controls, so a layout made there doesn't move the other layouts' buttons.
constexpr int kCRstick = 16, kCJump = 17, kCFire = 18, kCMorph = 19, kCMissile = 20, kCLt = 21,
              kCLb = 22, kCRt = 23, kCRb = 24, kCTz = 25, kCTr = 26;
// The Turbo button, once for the default/classic layouts and once for twin.
constexpr int kCTurbo = 27, kCTurboTwin = 28;
// The Morph Ball page's buttons (kBallFace's order), shown only in the ball.
constexpr int kCBall = 29, kCBbomb = 29, kCBboost = 30, kCBpbomb = 31, kCBspider = 32,
              kCBunmorph = 33;
constexpr int kBallButtons = 5;
// An added button's page, saved as its entry's 7th field.
constexpr int kPagePlay = 0, kPageBall = 1, kPageBoth = 2;
constexpr const char* kPageNames[3] = {"Gameplay", "Ball", "Both"};
constexpr int kControls = 34;
constexpr int kCustomSlots = 64;
constexpr int kCCustom = kControls;
constexpr int kAllControls = kControls + kCustomSlots;
// An added button's radius, in layout heights (A's is 0.085).
constexpr float kCustomRadius = 0.07f;
constexpr float kMinScale = 0.5f, kMaxScale = 2.5f, kMaxOffset = 4.f;

// A control's own opacity cycles through these (percent). MENU stops at kMenuMinAlpha: it is the
// only way into F1 on a touch-only device.
constexpr int kAlphaSteps[] = {100, 75, 50, 25, 0};
constexpr int kMenuMinAlpha = 25;
// The editor draws a control at its own opacity, but hidden and 0% ones at this.
constexpr int kEditMinAlpha = 15;

// The editor's toolbar: the tools (indexed by kEdit*), their labels and widths, and which ones show
// depends on the selection (EditTools).
constexpr int kEditMinus = 0, kEditPlus = 1, kEditOpacity = 2, kEditHide = 3, kEditReset = 4,
              kEditResetAll = 5, kEditDone = 6, kEditAdd = 7, kEditAction = 8, kEditDelete = 9,
              kEditPage = 10, kEditCustomPage = 11, kEditToolCount = 12;
// The Java's "−" is U+2212, which the port's ImGui font has no glyph for: a plain "-" it is.
constexpr const char* kEditLabels[kEditToolCount] = {
    "-", "+", "Opacity", "Hide", "Reset", "Reset all", "Done", "Add", "Action", "Delete",
    "Ball page", "Page"};
constexpr float kEditWidthsDp[kEditToolCount] = {44.f, 44.f, 112.f, 60.f, 64.f, 88.f,
                                                 64.f, 64.f, 72.f,  72.f, 100.f, 100.f};
constexpr float kEditBarHeightDp = 44.f, kEditBarGapDp = 6.f;
constexpr float kEditGrabDp = 8.f; // slack round a control's bounds
constexpr float kEditScaleStep = 0.1f;
// Nothing selected, a stock control, an added button.
constexpr int kEditToolsNone[] = {kEditAdd, kEditPage, kEditResetAll, kEditDone};
constexpr int kEditToolsStock[] = {kEditMinus, kEditPlus, kEditOpacity, kEditHide,
                                   kEditReset, kEditDelete, kEditDone};
constexpr int kEditToolsCustom[] = {kEditMinus,    kEditPlus, kEditOpacity,  kEditHide,
                                    kEditAction,   kEditCustomPage, kEditDelete, kEditDone};
// The ids the saved layout uses; never rename one.
constexpr const char* kControlIdsStock[kControls] = {
    "lstick", "cstick", "dpad", "a", "b", "x", "y", "l", "r", "z", "visor", "beam", "start",
    "menu", "map", "eye", "rstick", "jump", "fire", "morph", "missile", "lt", "lb", "rt", "rb",
    "tz", "tr", "turbo", "tturbo", "bbomb", "bboost", "bpbomb", "bspider", "bunmorph"};
constexpr const char* kControlNames[kControls] = {
    "Left stick", "C-stick", "D-pad", "A", "B", "X", "Y", "L", "R", "Z", "Visor", "Beam", "Start",
    "Menu", "Map", "Hide", "Right stick", "Jump", "Fire", "Morph", "Missile", "LT Lock", "LB Jump",
    "RT Fire", "RB Missile", "Map (Z)", "R", "Turbo", "Turbo", "Bomb", "Boost", "P.Bomb", "Spider",
    "Unmorph"};

// The buttons' labels for each context (#106): play, morph ball, pause menu, map, message box.
enum LabelContext { kLabelsPlay, kLabelsMorph, kLabelsPause, kLabelsMap, kLabelsDialog };

// What each GameCube face button does, drawn small under its letter.
constexpr const char* kGamecubeFunctions[4] = {"Fire", "Jump", "Morph", "Missile"};
// In the pause menu (Y turns the inventory's suit view), on the map screen (A switches area and
// world map, Y shows the legend) and in save station or message boxes; blank where it does nothing.
constexpr const char* kGamecubePauseFunctions[4] = {"Select", "Back", "", "View"};
constexpr const char* kGamecubeMapFunctions[4] = {"World", "Exit", "", "Legend"};
constexpr const char* kGamecubeDialogFunctions[4] = {"OK", "", "", ""};
constexpr const char* kTwinPauseFunctions[4] = {"Back", "Select", "", ""};
constexpr const char* kTwinMapFunctions[4] = {"Exit", "World", "", ""};
constexpr const char* kTwinDialogFunctions[4] = {"", "OK", "", ""};

struct ControlButton {
  const char* label;
  int button;
  // A plain button's x is a fraction of the width and its y one of the height. An anchored one's
  // are offsets from GameCube A, in heights.
  bool anchored;
  float x, y;
  // A kidney's radius is its centre arc's, round its x/y.
  float radius;
  // Kidneys only: the band's half width and its arc in degrees, clockwise from +x.
  float halfWidth, arcStart, arcSweep;
  // An RGB fill, or 0 for the overlay's own.
  uint32_t rgb;
  bool Kidney() const { return arcSweep != 0.f; }
};

constexpr ControlButton Round(const char* label, int button, float x, float y, float radius,
                              uint32_t rgb) {
  return {label, button, true, x, y, radius, 0.f, 0.f, 0.f, rgb};
}
constexpr ControlButton Kidney(const char* label, int button, float ring, float halfWidth,
                               float arcStart, float arcSweep, uint32_t rgb) {
  return {label, button, true, 0.f, 0.f, ring, halfWidth, arcStart, arcSweep, rgb};
}

// A big green A, a small red B at its lower left, X and Y as kidneys curving round A.
constexpr ControlButton kGamecubeFace[4] = {
    Round("A", kBtnSouth, 0.f, 0.f, 0.085f, kGcGreen),
    Round("B", kBtnEast, -0.123f, 0.103f, 0.050f, kGcRed),
    Kidney("X", kBtnWest, 0.158f, 0.040f, -37.f, 55.f, kGcGrey),
    Kidney("Y", kBtnNorth, 0.158f, 0.040f, -150.f, 55.f, kGcGrey),
};
// Twin stick: Jump is B, Fire is A, Morph is X, and Beam held shifts the D-pad to beams. Bottom,
// right, left, top, so the controls are kCJump + index. Each takes the colour of the button it
// sends.
constexpr ControlButton kTwinFace[4] = {
    Round("Jump", kBtnEast, 0.f, kTwinDiamond, 0.055f, kGcRed),
    Round("Fire", kBtnSouth, kTwinDiamond, 0.f, 0.055f, kGcGreen),
    Round("Morph", kBtnWest, -kTwinDiamond, 0.f, 0.055f, kGcGrey),
    Round("Beam", kBeamShift, 0.f, -kTwinDiamond, 0.055f, kGcGrey),
};
// The optional Turbo button, above the cluster on the right. Not in the face arrays: those index
// the editor ids and are always drawn.
constexpr ControlButton kGamecubeTurbo = Round("Turbo", kTurboFire, 0.05f, -0.27f, 0.045f, kGcGreen);
constexpr ControlButton kTwinTurbo = Round("Turbo", kTurboFire, 0.f, -0.21f, 0.045f, kGcGreen);

// The Morph Ball page's buttons, kCBall + index, where the face buttons that do the same in the ball
// sit: Bomb on A (Fire in twin), Boost on B (Jump), Unmorph on X (Morph), P.Bomb on Y and Spider on
// R (twin: above P.Bomb). No button id: they only report their touch code.
constexpr ControlButton kBallFace[5] = {
    Round("Bomb", -1, 0.f, 0.f, 0.085f, kGcGreen),
    Round("Boost", -1, -0.123f, 0.103f, 0.050f, kGcRed),
    Round("P.Bomb", -1, -0.085f, -0.133f, 0.055f, kGcGrey),
    Round("Spider", -1, 0.05f, -0.27f, 0.050f, kGcGrey),
    Round("Unmorph", -1, 0.156f, -0.026f, 0.055f, kGcGrey),
};
constexpr ControlButton kTwinBallFace[5] = {
    Round("Bomb", -1, kTwinDiamond, 0.f, 0.055f, kGcGreen),
    Round("Boost", -1, 0.f, kTwinDiamond, 0.055f, kGcRed),
    Round("P.Bomb", -1, 0.f, -kTwinDiamond, 0.055f, kGcGrey),
    Round("Spider", -1, 0.f, -0.21f, 0.055f, kGcGrey),
    Round("Unmorph", -1, -kTwinDiamond, 0.f, 0.055f, kGcGrey),
};

struct PillButton {
  const char* label;
  int axis;
  int button;
  float left, top, right, bottom;
  // An RGB fill, or 0 for the overlay's own.
  uint32_t rgb;
  // The editor control this pill is, or -1 to go by its axis/button (PillControl).
  int control;
  // One identity for the press map and the held highlight, whether it is a button or an axis.
  int Id() const { return axis >= 0 ? kAxisIdBase + axis : button; }
};

// L and R are the pad's analog triggers; Z is its digital shoulder. START and MENU ignore their
// rects: see CornerSlot.
constexpr PillButton kGamecubePills[5] = {
    {"L Lock", kAxisTriggerL, -1, 0.020f, 0.030f, 0.150f, 0.190f, kGcGrey, -1},
    {"R Look", kAxisTriggerR, -1, 0.850f, 0.030f, 0.980f, 0.190f, kGcGrey, -1},
    {"Z Map", -1, kBtnRightShoulder, 0.870f, 0.205f, 0.960f, 0.265f, kGcPurple, -1},
    {"START", -1, kBtnStart, 0.f, 0.f, 0.f, 0.f, 0, -1},
    {"MENU", -1, kToggleDebugOverlay, 0.f, 0.f, 0.f, 0.f, 0, -1},
};
// Twin stick's shoulders, like Remastered's: LT locks on, RT fires, LB jumps, RB fires missiles. Z
// (the map) sits inboard of the right pair; R (free look) has no use with a right stick that aims.
constexpr PillButton kTwinPills[7] = {
    {"LT Lock", kAxisTriggerL, -1, 0.020f, 0.030f, 0.150f, 0.100f, kGcGrey, kCLt},
    {"LB Jump", -1, kBtnEast, 0.020f, 0.115f, 0.150f, 0.185f, kGcRed, kCLb},
    {"RT Fire", -1, kBtnSouth, 0.850f, 0.030f, 0.980f, 0.100f, kGcGreen, kCRt},
    {"RB Missile", -1, kBtnNorth, 0.850f, 0.115f, 0.980f, 0.185f, kGcGrey, kCRb},
    {"Map", -1, kBtnRightShoulder, 0.730f, 0.030f, 0.830f, 0.100f, kGcPurple, kCTz},
    {"START", -1, kBtnStart, 0.f, 0.f, 0.f, 0.f, 0, -1},
    {"MENU", -1, kToggleDebugOverlay, 0.f, 0.f, 0.f, 0.f, 0, -1},
};

struct Rect {
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
  bool Contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
  bool Empty() const { return x1 <= x0 || y1 <= y0; }
  float Cx() const { return (x0 + x1) * 0.5f; }
  float Cy() const { return (y0 + y1) * 0.5f; }
  float Width() const { return x1 - x0; }
  float Height() const { return y1 - y0; }
  void SetEmpty() { x0 = y0 = x1 = y1 = 0.f; }
  void Inset(float d) {
    x0 -= d;
    y0 -= d;
    x1 += d;
    y1 += d;
  }
};

// A finger's target: one entry of the Java's `targets` map.
struct TouchTarget {
  int type = kButton;
  int id = 0;
  float x = 0.f, y = 0.f, startX = 0.f, startY = 0.f;
  Uint64 startMs = 0;
  // A held A button that also aims; see StartButtonAim.
  bool aiming = false;
  // The pill or face button a BUTTON target pressed. Twin sends one GameCube button from two
  // controls, so the highlight follows the control, not `held`.
  const ControlButton* faceButton = nullptr;
  const PillButton* pill = nullptr;
  // Its editor control (kC*), still reported as held, or -1.
  int touchControl = -1;
  // A touch: binding took the control over: no button of its own to release.
  bool bound = false;
  // The aiming finger's current upward stroke (TrackSwipe): where and when it began (swipeMs -1:
  // none yet), and whether it already sprang.
  float swipeX = 0.f, swipeY = 0.f;
  int64_t swipeMs = -1;
  bool swiped = false;
  // A native radial menu session (PortRadial::BeginTouch): 0 none, -1 native busy (the finger does
  // nothing else), >0 the session token. Never switched mid-gesture.
  int64_t radialToken = 0;
  int64_t radialOwner = 0;
};

// Everything below is guarded by sMutex: the filter runs inside SDL's event pump, the draw in the
// frame, both on the main thread today, but nothing here relies on it.
std::recursive_mutex sMutex;
bool sInstalled = false;

// The view's size in draw pixels, one dp in those pixels, and iOS's safe-area insets.
float sW = 0.f, sH = 0.f, sDensity = 1.f, sInsetSide = 0.f, sBottomInset = 0.f;

// F1's settings and the layout, re-read every draw (as the Java's members are).
bool sColored = false, sLabels = true, sFloating = false;
float sSideMargin = 0.f, sStickInset = 0.f, sButtonInset = 0.f;
bool sTurbo = false;
const ControlButton* sTurboButton = nullptr;
bool sCStick = false, sTwin = false, sAim = true, sWheels = false, sVisorTapScan = false,
     sMapTap = false;
std::array< const ControlButton*, 4 > sFace = {&kGamecubeFace[0], &kGamecubeFace[1],
                                               &kGamecubeFace[2], &kGamecubeFace[3]};
const ControlButton* sBallFace = kBallFace;
const PillButton* sPills = kGamecubePills;
int sPillCount = int(std::size(kGamecubePills));

// Touch state. The Java's `targets` is keyed by pointer id, its `held` by the id a press sends.
std::map< SDL_FingerID, TouchTarget > sTargets;
std::map< int, int > sHeld;
std::array< int, kAllControls > sTouchHeld{};
// Fingers passed on to SDL (the Java's `forwarded`).
std::set< SDL_FingerID > sForwarded;
// Fingers on the left and right sticks, the aim drag, and the open map's two.
SDL_FingerID sLeftPointer = 0, sRightPointer = 0, sAimPointer = 0, sPanPointer = 0,
             sPan2Pointer = 0;
bool sAnyLeft = false, sAnyRight = false, sAnyAim = false, sAnyPan = false, sAnyPan2 = false;
// The floating stick: where the thumb came down, and whether the held one floats.
float sFloatX = 0.f, sFloatY = 0.f;
bool sLeftFloated = false;
// The open wheel (the kWheel target's finger), and the control that opened it: a stock wheel button
// or an added one.
SDL_FingerID sWheelPointer = 0;
bool sWheelOpen = false;
float sWheelCx = 0.f, sWheelCy = 0.f;
int sWheelControl = -1;

// The HUD's minimap rect (fractions, then 1 when drawn there), and the map button beside the eye.
float sMinimapRect[5] = {};
Rect sMapButtonRect, sBackButtonRect, sCogButtonRect, sHideBounds;
bool sMapButtonShown = false;
// RHidden() as of the last draw; touches go by what is drawn.
bool sRHiddenNow = false;
// nativePointerMenuActive() as of the last draw: then only the back button shows.
bool sPointerMenuNow = false;
// nativeTouchControlsEnabled() as of the last draw.
bool sControlsOn = true;
int sLabelContextNow = kLabelsPlay;
bool sHidden = false;      // the eye button; the next touch brings the controls back
bool sAutoHidden = false;  // a real pad, keyboard or mouse was used
bool sBallPage = false;
// The layout editor: it takes every touch and shows every control.
bool sEditing = false;
// F1's "Edit layout" was pressed; the editor opens once the overlay is gone.
bool sEditPending = false;
// The editor's selection (a kC* control), and why the last tool did nothing (Add with every slot
// used), shown in the hint line until the next touch.
int sSelected = -1;
std::string sEditNotice;
// Fingers on the editor's toolbar and on the dragged control, and the pinch that resizes it.
SDL_FingerID sDragPointer = 0, sPinchPointer = 0, sEditToolPointer = 0;
bool sAnyDrag = false, sAnyPinch = false, sAnyEditTool = false;
std::map< SDL_FingerID, ImVec2 > sEditFingers;
float sDragLastX = 0.f, sDragLastY = 0.f, sPinchStartDist = 1.f, sPinchStartScale = 1.f;
// The toolbar's tool rects, laid out for the current width.
Rect sEditBar[kEditToolCount];

// The Java's pickAction AlertDialog, as an ImGui window: the deleted stock controls this layout can
// bring back (Add lists them first), then the actions an added button can take.
struct ActionPicker {
  bool open = false;
  int control = -1; // the added button whose action changes, or -1 for Add
  std::vector< int > restore;
  std::vector< std::string > keys; // action keys, parallel to labels
  std::vector< std::string > labels;
  int checked = -1;
  bool armed = false; // the tap that opened it has lifted, so a tap outside cancels
  int shownFrames = 0;
};
ActionPicker sPicker;

// The layout (`syncLayout`): the per-control overrides, then the player's added buttons.
std::array< float, kAllControls > sOvDx{}, sOvDy{};
std::array< float, kAllControls > sOvScale{};
std::array< int, kAllControls > sOvAlpha{};
std::array< bool, kAllControls > sOvHidden{};
std::array< bool, kControls > sOvRemoved{};
std::array< std::string, kCustomSlots > sCustomAction{};
std::array< int, kCustomSlots > sCustomPage{};
// The layout string last read from or written to the native config.
std::string sLayoutText;

// Drawing: F1's opacity for the whole overlay, and the control being drawn (its own layer).
float sGlobal = 1.f, sLocal = 1.f;

void InitDefaults() {
  sOvScale.fill(1.f);
  sOvAlpha.fill(100);
  sCustomPage.fill(kPagePlay);
}

// Java's dp(): a dp length in view pixels, rounded as it is there.
float Dp(float value) { return std::round(value * sDensity); }

// The layout height: the view's, capped at a phone's.
float LayoutU(float height) { return std::min(height, Dp(kMaxLayoutDp)); }

float LayoutScale(float height) { return height > 0.f ? LayoutU(height) / height : 1.f; }

// A horizontal fraction of the width, from the nearest side edge, plus the side margin.
float LayoutX(float fraction, float width, float height) {
  const float s = LayoutScale(height);
  return fraction < 0.5f ? sSideMargin + fraction * width * s
                         : fraction * width + (1.f - fraction) * width * (1.f - s) - sSideMargin;
}

float LayoutYFromBottom(float fraction, float height) {
  return fraction * height + (1.f - fraction) * (height - LayoutU(height));
}

// A vertical fraction: the layout sits in a phone-height band at the bottom, so the top half is
// still in reach.
float LayoutY(float fraction, float height) {
  return fraction < 0.5f ? (height - LayoutU(height)) + fraction * LayoutU(height)
                         : LayoutYFromBottom(fraction, height);
}

void SetButton(int button, bool down) { PortTouch::SetButton(button, down); }

void SetAxis(int axis, float value) { PortTouch::SetAxis(axis, value); }

// Tells the game which set of prompts this overlay is showing.
void SetTouchDevice(bool xbox) {
  PortTouch::NoteDevice(xbox);
  PortPrompts::NoteTouchInput(xbox);
}

// The native touch code of an editor control: the stock ones are their kC* id.
int TouchCode(int control) {
  return control >= kCCustom ? PortInputDevices::kTouchCustomBase + control - kCCustom
         : control >= kCBall ? PortInputDevices::kTouchBallBase + control - kCBall
                             : control;
}

// The actions an added button can take, key then label, in the action list's order,
// then the player's own actions (controls.toml); rebuilt when that file changes.
const std::vector< std::string >& ButtonActions() {
  static std::vector< std::string > list;
  static uint64_t version = 0;
  static bool loaded = false;
  const uint64_t now = PortInputDevices::UserBindingsVersion();
  if (loaded && version == now) {
    return list;
  }
  loaded = true;
  version = now;
  list.clear();
  for (int i = 0; i < PortInput::kActionCount; ++i) {
    const auto a = static_cast< PortInput::Action >(i);
    if (!PortInput::TouchButtonAction(a)) {
      continue;
    }
    const PortInput::ActionInfo& info = PortInput::Info(a);
    list.emplace_back(info.key);
    list.emplace_back(info.label);
    if (a == PortInput::Action::VisorPrev) {
      list.insert(list.end(), std::begin(PortTouch::kTouchWheelActions),
                  std::end(PortTouch::kTouchWheelActions));
    }
  }
  for (const PortInput::CustomActionDef& def : PortInputDevices::UserBindings()->customActions) {
    const PortInput::Action a = PortInput::CustomAction(def.id);
    if (!PortInput::TouchButtonAction(a)) {
      continue;
    }
    list.emplace_back(PortInput::ActionKey(a));
    list.emplace_back(PortCustomActions::DisplayName(def));
  }
  return list;
}

// nativeTouchButtonActions' lookup: an action's label, or false when a button can't take it.
bool ActionLabel(const std::string& key, std::string& out) {
  const std::vector< std::string >& list = ButtonActions();
  for (size_t i = 0; i + 1 < list.size(); i += 2) {
    if (list[i] == key) {
      out = list[i + 1];
      return true;
    }
  }
  return false;
}

// nativeTouchBallLabel: the Morph Ball row's name for a control, "" for a ball button left without
// one, and nothing for any other control.
std::optional< std::string > TouchBallLabel(int code) {
  if (const PortInput::BallRow* row = PortInput::FindBallRow(PortInputDevices::TouchBallRow(code))) {
    return std::string(row->touch);
  }
  for (const PortInput::TouchBallControl& c : PortInput::kTouchBallControls) {
    if (c.control == code) {
      return std::string();
    }
  }
  return std::nullopt;
}

const std::array< std::string, kAllControls >& ControlIds() {
  static const std::array< std::string, kAllControls > ids = [] {
    std::array< std::string, kAllControls > out{};
    for (int i = 0; i < kControls; ++i) {
      out[i] = kControlIdsStock[i];
    }
    for (int slot = 0; slot < kCustomSlots; ++slot) {
      out[kCCustom + slot] = "k" + std::to_string(slot);
    }
    return out;
  }();
  return ids;
}

// --- the layout string -------------------------------------------------------
// `id:dx,dy,scale[,opacity[,hidden]];...` (opacity in percent, hidden 0 or 1), and for an added
// button `k<slot>:dx,dy,scale,opacity,hidden,<action key>[,page]`. Unknown ids and actions and
// malformed entries are skipped, so a layout from another version still loads what this one knows.

std::string Trim(const std::string& s) {
  const size_t first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return std::string();
  }
  const size_t last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

std::vector< std::string > Split(const std::string& s, char sep) {
  std::vector< std::string > out;
  size_t start = 0;
  for (;;) {
    const size_t at = s.find(sep, start);
    if (at == std::string::npos) {
      out.push_back(s.substr(start));
      return out;
    }
    out.push_back(s.substr(start, at - start));
    start = at + 1;
  }
}

// Float.parseFloat for one field: the whole trimmed field must be a number.
bool ParseFloat(const std::string& field, float& out) {
  const std::string text = Trim(field);
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  const float value = std::strtof(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0') {
    return false;
  }
  if (std::isnan(value) || std::isinf(value)) {
    return false;
  }
  out = value;
  return true;
}

bool ParseInt(const std::string& field, int& out) {
  const std::string text = Trim(field);
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0') {
    return false;
  }
  out = static_cast< int >(value);
  return true;
}

void LoadLayout(const std::string& text) {
  sOvDx.fill(0.f);
  sOvDy.fill(0.f);
  sOvScale.fill(1.f);
  sOvAlpha.fill(100);
  sOvHidden.fill(false);
  sOvRemoved.fill(false);
  for (std::string& key : sCustomAction) {
    key.clear();
  }
  sCustomPage.fill(kPagePlay);
  const std::array< std::string, kAllControls >& ids = ControlIds();
  for (const std::string& entry : Split(text, ';')) {
    const size_t colon = entry.find(':');
    if (colon == std::string::npos || colon == 0) {
      continue;
    }
    const std::string id = Trim(entry.substr(0, colon));
    int control = -1;
    for (int i = 0; i < kAllControls; ++i) {
      if (ids[i] == id) {
        control = i;
        break;
      }
    }
    const std::vector< std::string > parts = Split(entry.substr(colon + 1), ',');
    const bool custom = control >= kCCustom;
    if (control < 0 ||
        (custom ? parts.size() < 6 || parts.size() > 7 : parts.size() < 3 || parts.size() > 5)) {
      continue;
    }
    std::string key;
    if (custom) {
      key = Trim(parts[5]);
      std::string label;
      if (!ActionLabel(key, label)) {
        continue;
      }
    }
    float dx = 0.f, dy = 0.f, scale = 1.f, alpha = 100.f;
    if (!ParseFloat(parts[0], dx) || !ParseFloat(parts[1], dy) || !ParseFloat(parts[2], scale)) {
      continue;
    }
    bool off = false, gone = false;
    if (parts.size() > 3 && !ParseFloat(parts[3], alpha)) {
      continue;
    }
    if (parts.size() > 4) {
      float flag = 0.f;
      if (!ParseFloat(parts[4], flag)) {
        continue;
      }
      off = flag != 0.f;
      gone = flag == 2.f && !custom; // an added button is deleted outright
    }
    sOvDx[control] = std::clamp(dx, -kMaxOffset, kMaxOffset);
    sOvDy[control] = std::clamp(dy, -kMaxOffset, kMaxOffset);
    sOvScale[control] = std::clamp(scale, kMinScale, kMaxScale);
    sOvAlpha[control] =
        std::clamp(static_cast< int >(std::lround(alpha)), 0, 100);
    sOvHidden[control] = off;
    if (gone) {
      sOvRemoved[control] = true;
    }
    if (custom) {
      sCustomAction[control - kCCustom] = key;
      if (parts.size() > 6) {
        int page = kPagePlay;
        if (!ParseInt(parts[6], page) || page < kPagePlay || page > kPageBoth) {
          page = kPagePlay;
        }
        sCustomPage[control - kCCustom] = page;
      }
    }
    if (control == kCMenu) {
      // A hand-edited layout can't lock the only way into F1 away.
      sOvAlpha[control] = std::max(kMenuMinAlpha, sOvAlpha[control]);
      sOvHidden[control] = false;
      sOvRemoved[control] = false;
    }
  }
}

// Only the controls that were changed, so a default layout is the empty string. The editor's Done
// saves through this, and Android reads what it writes.
std::string SerializeLayout() {
  std::string out;
  const std::array< std::string, kAllControls >& ids = ControlIds();
  char field[64];
  for (int i = 0; i < kControls; ++i) {
    if (sOvDx[i] == 0.f && sOvDy[i] == 0.f && sOvScale[i] == 1.f && sOvAlpha[i] == 100 &&
        !sOvHidden[i] && !sOvRemoved[i]) {
      continue;
    }
    if (!out.empty()) {
      out += ';';
    }
    std::snprintf(field, sizeof(field), "%.4f,%.4f,%.3f", double(sOvDx[i]), double(sOvDy[i]),
                  double(sOvScale[i]));
    out += ids[i] + ":" + field;
    // Only as many fields as needed, so older layouts serialize as before.
    if (sOvRemoved[i]) {
      out += "," + std::to_string(sOvAlpha[i]) + ",2";
    } else if (sOvHidden[i]) {
      out += "," + std::to_string(sOvAlpha[i]) + ",1";
    } else if (sOvAlpha[i] != 100) {
      out += "," + std::to_string(sOvAlpha[i]);
    }
  }
  // The added buttons, after the stock controls, so a layout without any is what it was before
  // they existed.
  for (int slot = 0; slot < kCustomSlots; ++slot) {
    if (sCustomAction[slot].empty()) {
      continue;
    }
    const int i = kCCustom + slot;
    if (!out.empty()) {
      out += ';';
    }
    std::snprintf(field, sizeof(field), "%.4f,%.4f,%.3f,%d,%d,", double(sOvDx[i]), double(sOvDy[i]),
                  double(sOvScale[i]), sOvAlpha[i], sOvHidden[i] ? 1 : 0);
    out += ids[i] + ":" + field + sCustomAction[slot];
    if (sCustomPage[slot] != kPagePlay) {
      out += "," + std::to_string(sCustomPage[slot]); // older layouts lack it: Gameplay
    }
  }
  return out;
}

// The layout is stored natively (the F1 config), so a reset there, or the editor's save, reaches the
// next draw. Re-parsed only when the text changes.
void SyncLayout() {
  const std::string text = PortTouch::Layout();
  if (text != sLayoutText) {
    sLayoutText = text;
    LoadLayout(text);
  }
}

void ResetControl(int control) {
  sOvDx[control] = 0.f;
  sOvDy[control] = 0.f;
  sOvScale[control] = 1.f;
  sOvAlpha[control] = 100;
  sOvHidden[control] = false;
  if (control < kControls) {
    sOvRemoved[control] = false;
  }
}

bool Removed(int control) { return control >= 0 && control < kControls && sOvRemoved[control]; }

// The wheel an added button opens (0 Visor, 1 Beam), or -1 for any other control.
int CustomWheel(int control) {
  if (control < kCCustom || control >= kAllControls) {
    return -1;
  }
  const std::string& key = sCustomAction[control - kCCustom];
  return key == PortTouch::kTouchWheelActions[0]   ? 0
         : key == PortTouch::kTouchWheelActions[2] ? 1
                                                   : -1;
}

// --- pages, labels, what is shown -------------------------------------------

// Whether play wants the Morph Ball page: morphed, outside the map, pause menu and message boxes
// (their face buttons do menu things).
bool InBall() {
  return !false && !PortTouch::PauseScreenOpen() &&
         !(PortTouch::PauseScreenOpen() && !PortTouch::PauseScreenOpen()) &&
         (uint32_t(PortTouch::WheelState()) & kWheelMorphedBit) != 0;
}

// Whether a control belongs to the page up now. The stick, START, MENU, the map (pill, button and
// minimap) and the eye are on both.
bool OnPage(int control) {
  switch (control) {
  case kCLStick:
  case kCStart:
  case kCMenu:
  case kCZ:
  case kCTz:
  case kCMap:
  case kCEye:
    return true;
  default:
    break;
  }
  if (control >= kCCustom) {
    const int page = sCustomPage[control - kCCustom];
    return page == kPageBoth || (page == kPageBall) == sBallPage;
  }
  return ((control >= kCBall && control < kCBall + kBallButtons) == sBallPage);
}

// Whether a saved layout hides this control from play. Not in the editor (which still shows and
// selects it) nor in the controls-off view, where START and MENU ignore the layout.
bool CtrlHidden(int control) {
  return sControlsOn &&
         (!OnPage(control) || (!sEditing && (sOvHidden[control] || Removed(control))));
}

// The percent this control is drawn at, or -1 for not at all.
int ControlAlpha(int control) {
  if (Removed(control)) {
    return -1; // the controls-off view draws START and MENU without this
  }
  if (sControlsOn && !OnPage(control)) {
    return -1;
  }
  if (sEditing) {
    return sOvHidden[control] ? kEditMinAlpha : std::max(kEditMinAlpha, sOvAlpha[control]);
  }
  if (!sControlsOn) {
    return 100;
  }
  return sOvHidden[control] ? -1 : sOvAlpha[control];
}

// Which functions the buttons wear. The map screen comes first: the pause menu's map is one too.
int LabelContext() {
  if (sEditing) {
    return kLabelsPlay;
  }
  if (false) {
    return kLabelsMap;
  }
  if (PortTouch::PauseScreenOpen()) {
    return kLabelsPause;
  }
  if (PortTouch::PauseScreenOpen() && !PortTouch::PauseScreenOpen()) {
    return kLabelsDialog;
  }
  return (uint32_t(PortTouch::WheelState()) & kWheelMorphedBit) != 0 ? kLabelsMorph : kLabelsPlay;
}

bool IsMenuPill(const PillButton& pill) {
  return pill.axis < 0 && (pill.button == kBtnStart || pill.button == kToggleDebugOverlay);
}

int CornerSlot(const PillButton& pill) {
  if (pill.axis >= 0) {
    return -1;
  }
  return pill.button == kBtnStart ? 0 : pill.button == kToggleDebugOverlay ? 1 : -1;
}

int PillControl(const PillButton& pill) {
  const int slot = CornerSlot(pill);
  if (slot >= 0) {
    return slot == 0 ? kCStart : kCMenu;
  }
  if (pill.control >= 0) {
    return pill.control;
  }
  if (pill.axis == kAxisTriggerL) {
    return kCL;
  }
  if (pill.axis == kAxisTriggerR) {
    return kCR;
  }
  return pill.button == kBtnRightShoulder ? kCZ : kCL;
}

const PillButton* PillOf(int control) {
  for (int i = 0; i < sPillCount; ++i) {
    if (CornerSlot(sPills[i]) < 0 && PillControl(sPills[i]) == control) {
      return &sPills[i];
    }
  }
  return nullptr;
}

// The GameCube layout's Z pill only opens the map, which a minimap tap does.
bool PillHidden(const PillButton& pill) {
  return sMapTap && pill.axis < 0 && pill.button == kBtnRightShoulder;
}

// With drag-to-aim, R's hold-still free look is redundant, so R only shows where it does something
// else: morphed, in the pause menu, and on the map screen. Not part of PillHidden, so the pills'
// layout doesn't jump as R comes and goes. Twin stick always shows it.
bool RHidden() {
  return sAim && !sTwin && (uint32_t(PortTouch::WheelState()) & kWheelMorphedBit) == 0 &&
         !PortTouch::PauseScreenOpen() && !false;
}

// LayoutShows on either page.
bool LayoutHas(int control) {
  if (control >= kCCustom) {
    return !sCustomAction[control - kCCustom].empty();
  }
  switch (control) {
  case kCCStick:
    return sCStick && !sTwin;
  case kCA:
  case kCB:
  case kCX:
  case kCY:
  case kCL:
  case kCR:
    return !sTwin;
  case kCRstick:
  case kCJump:
  case kCFire:
  case kCMorph:
  case kCMissile:
  case kCLt:
  case kCLb:
  case kCRt:
  case kCRb:
    return sTwin;
  case kCTr:
    return false; // retired: twin's R pill, kept so saved ids don't shift
  case kCTurbo:
    return sTurbo && !sTwin;
  case kCTurboTwin:
    return sTurbo && sTwin;
  case kCDpad:
    return !sWheels;
  case kCZ:
    return !sTwin && !sMapTap;
  case kCTz:
    return sTwin && !sMapTap;
  case kCMap:
    return sMapTap;
  case kCVisor:
  case kCBeam:
    return sWheels;
  default:
    return true;
  }
}

// Whether the current layout, settings and page have this control at all, deleted or not (Add's
// list restores only those).
bool LayoutShows(int control) { return OnPage(control) && LayoutHas(control); }

// Whether the current settings draw this control.
bool EditShown(int control) { return !Removed(control) && LayoutShows(control); }

bool PillShown(const PillButton& pill) {
  if (!sControlsOn && !sEditing) {
    return IsMenuPill(pill);
  }
  return !PillHidden(pill) && !(pill.axis == kAxisTriggerR && sRHiddenNow) &&
         !CtrlHidden(PillControl(pill));
}

// --- geometry ---------------------------------------------------------------

// The square around bottom button `slot`, counted from the side edge: on the right, 0 is the eye and
// 1 the map; on the left, 0 is START and 1 MENU.
void BottomButtonRect(int slot, bool right, float width, float height, Rect& out);

// Moves and scales a control's default rect by its override, round its centre.
void ApplyOverride(int control, Rect& rect, float height) {
  if (!sControlsOn && !sEditing) {
    return; // MENU and START stay put and reachable
  }
  if (sOvDx[control] == 0.f && sOvDy[control] == 0.f && sOvScale[control] == 1.f) {
    return; // the default layout stays bit-for-bit what it was
  }
  const float u = LayoutU(height);
  const float cx = rect.Cx() + sOvDx[control] * u;
  const float cy = rect.Cy() + sOvDy[control] * u;
  const float halfW = rect.Width() * 0.5f * sOvScale[control];
  const float halfH = rect.Height() * 0.5f * sOvScale[control];
  rect = {cx - halfW, cy - halfH, cx + halfW, cy + halfH};
}

void BottomButtonRect(int slot, bool right, float width, float height, Rect& out) {
  const float d = 2.f * Dp(kBottomButtonRadiusDp);
  const float fromSide = sSideMargin + Dp(kBottomButtonSideDp) +
                         float(slot) * (d + Dp(kBottomButtonGapDp));
  const float left = right ? width - fromSide - d : fromSide;
  // The home indicator on iOS: the one departure from the Java's own insets, which Android gets
  // from the display cutout instead.
  const float bottom = height - Dp(kBottomButtonBottomDp) - sBottomInset;
  out = {left, bottom - d, left + d, bottom};
  ApplyOverride(right ? (slot == 0 ? kCEye : kCMap) : (slot == 0 ? kCStart : kCMenu), out, height);
}

int ControlOf(const ControlButton& button) {
  if (&button == sTurboButton) {
    return sTwin ? kCTurboTwin : kCTurbo;
  }
  for (int i = 0; i < kBallButtons; ++i) {
    if (&button == &kBallFace[i] || &button == &kTwinBallFace[i]) {
      return kCBall + i;
    }
  }
  const int first = sTwin ? kCJump : kCA;
  for (int i = 0; i < 4; ++i) {
    if (sFace[i] == &button) {
      return first + i;
    }
  }
  return first;
}

float CentreX(const ControlButton& button, float width, float height) {
  return button.anchored ? width - sSideMargin - sButtonInset -
                               (kGcAFromRight - button.x) * LayoutU(height)
                         : LayoutX(button.x, width, height);
}

float CentreY(const ControlButton& button, float height) {
  return LayoutY(button.anchored ? kGcAY + button.y : button.y, height);
}

struct FaceGeo {
  float x = 0.f, y = 0.f, radius = 0.f, half = 0.f;
};

// The button's centre, radius (a kidney's is its arc's) and band half width. `moved` applies the
// override, which scales a kidney round its own middle rather than round its arc's centre; draw and
// hit test both go through here.
FaceGeo FaceGeometry(const ControlButton& button, float width, float height, bool moved) {
  const float u = LayoutU(height);
  FaceGeo out;
  out.x = CentreX(button, width, height);
  out.y = CentreY(button, height);
  out.radius = button.radius * u;
  out.half = button.halfWidth * u;
  if (moved) {
    const int control = ControlOf(button);
    const float scale = sOvScale[control];
    if (button.Kidney()) {
      const float middle = (button.arcStart + button.arcSweep * 0.5f) * kPi / 180.f;
      out.x += out.radius * (1.f - scale) * std::cos(middle);
      out.y += out.radius * (1.f - scale) * std::sin(middle);
    }
    out.x += sOvDx[control] * u;
    out.y += sOvDy[control] * u;
    out.radius *= scale;
    out.half *= scale;
  }
  return out;
}

// A band round (cx, cy) along the arc, with round ends: GameCube X and Y.
void KidneyPath(ImDrawList* dl, float cx, float cy, float ring, float half, float start,
                float sweep) {
  const float a0 = start * kPi / 180.f;
  const float a1 = (start + sweep) * kPi / 180.f;
  dl->PathArcTo(ImVec2(cx, cy), ring + half, a0, a1, 24);
  dl->PathArcTo(ImVec2(cx + ring * std::cos(a1), cy + ring * std::sin(a1)), half, a1, a1 + kPi, 12);
  dl->PathArcTo(ImVec2(cx, cy), ring - half, a1, a0, 24);
  dl->PathArcTo(ImVec2(cx + ring * std::cos(a0), cy + ring * std::sin(a0)), half, a0 + kPi,
                a0 + 2.f * kPi, 12);
}

// Path.computeBounds for a kidney: the centre arc sampled, grown by the band's half width (the Java
// asks the Path itself; the caps are what the sampling adds).
Rect KidneyBounds(float cx, float cy, float ring, float half, float start, float sweep) {
  Rect r;
  bool any = false;
  const float a0 = start * kPi / 180.f, a1 = (start + sweep) * kPi / 180.f;
  for (int i = 0; i <= 16; ++i) {
    const float a = a0 + (a1 - a0) * float(i) / 16.f;
    const float px = cx + ring * std::cos(a), py = cy + ring * std::sin(a);
    if (!any) {
      r = {px - half, py - half, px + half, py + half};
      any = true;
      continue;
    }
    r.x0 = std::min(r.x0, px - half);
    r.y0 = std::min(r.y0, py - half);
    r.x1 = std::max(r.x1, px + half);
    r.y1 = std::max(r.y1, py + half);
  }
  return r;
}

// The face buttons' real bounds, kidney arcs included.
Rect FaceBounds(float width, float height) {
  const float u = LayoutU(height);
  Rect r;
  bool first = true;
  for (int i = 0; i < 4; ++i) {
    const ControlButton& button = *sFace[i];
    const float x = CentreX(button, width, height);
    const float y = CentreY(button, height);
    const float radius = button.radius * u;
    const Rect b = button.Kidney()
                       ? KidneyBounds(x, y, radius, button.halfWidth * u, button.arcStart,
                                      button.arcSweep)
                       : Rect{x - radius, y - radius, x + radius, y + radius};
    if (first) {
      r = b;
      first = false;
      continue;
    }
    r.x0 = std::min(r.x0, b.x0);
    r.y0 = std::min(r.y0, b.y0);
    r.x1 = std::max(r.x1, b.x1);
    r.y1 = std::max(r.y1, b.y1);
  }
  return r;
}

// Mirrors the face cluster's gap to the right edge (side margin included, button inset not), plus
// the stick inset, so the two thumbs' controls look balanced.
float BaseLeftStickX(float width, float height) {
  const Rect face = FaceBounds(width, height);
  return (width - face.x1 - sButtonInset) + sStickInset + kStickRadius * LayoutU(height);
}

// Level with the face cluster's middle.
float BaseLeftStickY(float width, float height) { return FaceBounds(width, height).Cy(); }

float LeftStickX(float width, float height) {
  return BaseLeftStickX(width, height) + sOvDx[kCLStick] * LayoutU(height);
}

float LeftStickY(float width, float height) {
  return BaseLeftStickY(width, height) + sOvDy[kCLStick] * LayoutU(height);
}

float LeftStickRadius(float height) { return LayoutU(height) * kStickRadius * sOvScale[kCLStick]; }

// The control id the right stick's override is stored under.
int RightControl() { return sTwin ? kCRstick : kCCStick; }

float BaseRightStickX(float width, float height) {
  return width - sSideMargin - sButtonInset - LayoutU(height) * kGcCstickFromRight;
}

float RightStickX(float width, float height) {
  return BaseRightStickX(width, height) + sOvDx[RightControl()] * LayoutU(height);
}

// Twin's stick is level with the left one, at the diamond's centre; the C-stick sits lower.
float BaseRightStickY(float height) { return LayoutY(sTwin ? kGcAY : kGcCstickY, height); }

float RightStickY(float height) {
  return BaseRightStickY(height) + sOvDy[RightControl()] * LayoutU(height);
}

float RightStickBaseRadius() { return sTwin ? kStickRadius : kGcCstickRadius; }

float RightStickRadius(float height) {
  return LayoutU(height) * RightStickBaseRadius() * sOvScale[RightControl()];
}

// Whether (x, y) is in the area that grabs the right stick: the stick's reach on the right (face
// buttons are hit-tested first, so it can pass them), a phone's 0.38 of the width on the left and
// the top 0.43 of the height from the bottom. A moved stick takes the area along, scaled.
bool InRightStickGrab(float x, float y, float width, float height) {
  const float u = LayoutU(height);
  const float scale = sOvScale[RightControl()];
  const float baseCy = BaseRightStickY(height);
  const float baseRight = BaseRightStickX(width, height) + u * RightStickBaseRadius() * 1.6f;
  const float baseLeft = baseRight - (baseRight - width * 0.38f) * LayoutScale(height);
  const float baseTop = LayoutYFromBottom(0.43f, height);
  const float cy = RightStickY(height);
  const float right = RightStickX(width, height) + RightStickRadius(height) * 1.6f;
  return x >= right - (baseRight - baseLeft) * scale && x < right &&
         y > cy - (baseCy - baseTop) * scale && y <= cy + (height - baseCy) * scale;
}

float DpadX(float width, float height) {
  return LayoutX(kDpadX, width, height) + sOvDx[kCDpad] * LayoutU(height);
}

float DpadY(float height) { return LayoutY(kDpadY, height) + sOvDy[kCDpad] * LayoutU(height); }

// The D-pad direction under (x, y), or -1. The whole square round the cross counts, by the dominant
// axis, so a thumb that slips off an arm still presses something; only a small centre is dead.
int DpadButtonAt(float x, float y, float width, float height);

// Past the cap, with no D-pad under them, the top pills drop together until the lowest shown pill
// is dp(12) above the stick ring or face cluster.
float PillShift(float width, float height) {
  if (LayoutU(height) >= height || !sWheels) {
    return 0.f;
  }
  float bottom = 0.f;
  for (int i = 0; i < sPillCount; ++i) {
    const PillButton& pill = sPills[i];
    if (!PillHidden(pill) && CornerSlot(pill) < 0) {
      bottom = std::max(bottom, LayoutY(pill.bottom, height));
    }
  }
  const Rect face = FaceBounds(width, height);
  const float stickTop = BaseLeftStickY(width, height) - kStickRadius * LayoutU(height);
  return std::max(0.f, std::min(stickTop, face.y0) - Dp(12) - bottom);
}

// A pill's rect, from its side edge and the top of the layout band, or its corner slot.
void PillRect(const PillButton& pill, float width, float height, Rect& out) {
  const int slot = CornerSlot(pill);
  if (slot >= 0) {
    BottomButtonRect(slot, false, width, height, out);
    return;
  }
  const float shift = PillShift(width, height);
  out = {LayoutX(pill.left, width, height), LayoutY(pill.top, height) + shift,
         LayoutX(pill.right, width, height), LayoutY(pill.bottom, height) + shift};
  ApplyOverride(PillControl(pill), out, height);
}

// The Visor (left) and Beam (right) buttons: each a wheel radius up from the edge so its wheel opens
// on screen, just outside the left stick ring and the face cluster (or the C-stick ring).
float WheelButtonX(int wheel, float width, float height) {
  const float edge = Dp(kWheelButtonAnchorDp) + kWheelButtonRadius * LayoutU(height);
  float x = 0.f;
  if (wheel == 0) {
    x = BaseLeftStickX(width, height) + kStickRadius * LayoutU(height) + edge;
  } else if (sCStick) {
    x = BaseRightStickX(width, height) - LayoutU(height) * RightStickBaseRadius() - edge;
  } else {
    x = FaceBounds(width, height).x0 - edge;
  }
  // The open wheel must stay on screen.
  const float reach = Dp(kWheelRadiusDp);
  return std::max(reach, std::min(width - reach, x)) + sOvDx[kCVisor + wheel] * LayoutU(height);
}

float WheelButtonY(int wheel, float height) {
  return height - Dp(kWheelRadiusDp) - Dp(kWheelButtonGapDp) - sBottomInset +
         sOvDy[kCVisor + wheel] * LayoutU(height);
}

float WheelButtonRadius(int wheel, float height) {
  return kWheelButtonRadius * LayoutU(height) * sOvScale[kCVisor + wheel];
}

int WheelButtonAt(float x, float y, float width, float height) {
  for (int wheel = 0; wheel < 2; ++wheel) {
    if (CtrlHidden(kCVisor + wheel)) {
      continue; // the touch falls through, wheel or no wheel
    }
    const float radius = WheelButtonRadius(wheel, height) * 1.1f;
    const float dx = x - WheelButtonX(wheel, width, height);
    const float dy = y - WheelButtonY(wheel, height);
    if (dx * dx + dy * dy <= radius * radius) {
      return wheel;
    }
  }
  return -1;
}

int DpadButtonAt(float x, float y, float width, float height) {
  const float dx = x - DpadX(width, height);
  const float dy = y - DpadY(height);
  const float arm = kDpadArm * LayoutU(height) * sOvScale[kCDpad];
  const float dead = kDpadHalf * LayoutU(height) * sOvScale[kCDpad] * 0.4f;
  if (std::abs(dx) > arm || std::abs(dy) > arm || dx * dx + dy * dy < dead * dead) {
    return -1;
  }
  if (std::abs(dx) > std::abs(dy)) {
    return dx < 0.f ? kBtnDpadLeft : kBtnDpadRight;
  }
  return dy < 0.f ? kBtnDpadUp : kBtnDpadDown;
}

bool HitButton(const ControlButton& button, float x, float y, float width, float height) {
  const FaceGeo geo = FaceGeometry(button, width, height, true);
  const float dx = x - geo.x, dy = y - geo.y;
  if (!button.Kidney()) {
    return dx * dx + dy * dy <= geo.radius * geo.radius;
  }
  // Distance to the nearest point of the kidney's centre arc.
  float angle = std::atan2(dy, dx) * 180.f / kPi;
  float along = std::fmod(angle - button.arcStart, 360.f);
  if (along < 0.f) {
    along += 360.f;
  }
  if (along > button.arcSweep) {
    along = along - button.arcSweep < 360.f - along ? button.arcSweep : 0.f;
  }
  const float nearest = (button.arcStart + along) * kPi / 180.f;
  const float px = dx - geo.radius * std::cos(nearest);
  const float py = dy - geo.radius * std::sin(nearest);
  return px * px + py * py <= geo.half * geo.half;
}

// An added button's centre and radius: round the screen's centre, moved and scaled by its override.
FaceGeo CustomGeometry(int control, float width, float height) {
  const float u = LayoutU(height);
  FaceGeo out;
  out.x = width * 0.5f + sOvDx[control] * u;
  out.y = height * 0.5f + sOvDy[control] * u;
  out.radius = kCustomRadius * u * sOvScale[control];
  return out;
}

void FaceBoundsOf(const ControlButton& button, float width, float height, Rect& out) {
  const FaceGeo geo = FaceGeometry(button, width, height, true);
  out = button.Kidney()
            ? KidneyBounds(geo.x, geo.y, geo.radius, geo.half, button.arcStart, button.arcSweep)
            : Rect{geo.x - geo.radius, geo.y - geo.radius, geo.x + geo.radius, geo.y + geo.radius};
}

// A control's current bounds: the same geometry the draw and hit test use.
void ControlBounds(int control, float width, float height, Rect& out) {
  if (control >= kCCustom) {
    const FaceGeo geo = CustomGeometry(control, width, height);
    out = {geo.x - geo.radius, geo.y - geo.radius, geo.x + geo.radius, geo.y + geo.radius};
    return;
  }
  const float u = LayoutU(height);
  switch (control) {
  case kCLStick: {
    const float r = LeftStickRadius(height);
    out = {LeftStickX(width, height) - r, LeftStickY(width, height) - r,
           LeftStickX(width, height) + r, LeftStickY(width, height) + r};
    return;
  }
  case kCCStick:
  case kCRstick: {
    const float r = RightStickRadius(height);
    out = {RightStickX(width, height) - r, RightStickY(height) - r,
           RightStickX(width, height) + r, RightStickY(height) + r};
    return;
  }
  case kCDpad: {
    const float arm = kDpadArm * u * sOvScale[kCDpad];
    out = {DpadX(width, height) - arm, DpadY(height) - arm, DpadX(width, height) + arm,
           DpadY(height) + arm};
    return;
  }
  case kCA:
  case kCB:
  case kCX:
  case kCY:
  case kCJump:
  case kCFire:
  case kCMorph:
  case kCMissile:
  case kCTurbo:
  case kCTurboTwin: {
    if (control == kCTurbo || control == kCTurboTwin) {
      if (sTurboButton == nullptr) {
        out.SetEmpty();
        return;
      }
      FaceBoundsOf(*sTurboButton, width, height, out);
      return;
    }
    const int index = control >= kCJump ? control - kCJump : control - kCA;
    FaceBoundsOf(*sFace[index], width, height, out);
    return;
  }
  case kCBbomb:
  case kCBboost:
  case kCBpbomb:
  case kCBspider:
  case kCBunmorph:
    FaceBoundsOf(sBallFace[control - kCBall], width, height, out);
    return;
  case kCL:
  case kCR:
  case kCZ:
  case kCLt:
  case kCLb:
  case kCRt:
  case kCRb:
  case kCTz:
  case kCTr: {
    if (const PillButton* pill = PillOf(control)) {
      PillRect(*pill, width, height, out);
    } else {
      out.SetEmpty();
    }
    return;
  }
  case kCVisor:
  case kCBeam: {
    const int wheel = control - kCVisor;
    const float r = WheelButtonRadius(wheel, height);
    out = {WheelButtonX(wheel, width, height) - r, WheelButtonY(wheel, height) - r,
           WheelButtonX(wheel, width, height) + r, WheelButtonY(wheel, height) + r};
    return;
  }
  case kCStart:
    BottomButtonRect(0, false, width, height, out);
    return;
  case kCMenu:
    BottomButtonRect(1, false, width, height, out);
    return;
  case kCMap:
    BottomButtonRect(1, true, width, height, out);
    return;
  default:
    BottomButtonRect(0, true, width, height, out);
  }
}

// Keeps at least a control's centre on screen, and its offset sane.
void ClampToScreen(int control, float width, float height) {
  const float u = LayoutU(height);
  if (!(u > 0.f) || !(width > 0.f)) {
    return; // no size yet: dividing by u would put NaN in the layout
  }
  sOvDx[control] = std::clamp(sOvDx[control], -kMaxOffset, kMaxOffset);
  sOvDy[control] = std::clamp(sOvDy[control], -kMaxOffset, kMaxOffset);
  Rect bounds;
  ControlBounds(control, width, height, bounds);
  const float cx = bounds.Cx(), cy = bounds.Cy();
  sOvDx[control] += (std::clamp(cx, 0.f, width) - cx) / u;
  sOvDy[control] += (std::clamp(cy, 0.f, height) - cy) / u;
}

// --- pressing ---------------------------------------------------------------

// A control's press and release, by the id it sends: a gamepad button, an axis id, or one of the
// port's own actions. Refcounted, as the Java view's held map: two fingers on one button release it
// once.
void PressControl(int id) {
  if (id == kToggleDebugOverlay) {
    // Drive the overlay directly; polling a synthetic F1 key press is unreliable.
    if (sHeld.find(id) == sHeld.end()) {
      sHeld[id] = 1;
      PortOverlay::Toggle();
    }
    return;
  }
  const auto it = sHeld.find(id);
  const int count = it == sHeld.end() ? 0 : it->second;
  if (count == 0) {
    if (id == kBeamShift) {
      PortTouch::SetBeamShift(true);
    } else if (id == kTurboFire) {
      PortTouch::SetTurboFire(true);
    } else if (id == kMenuBack) {
      PortTouch::SetMenuBack(true);
    } else if (id >= kAxisIdBase) {
      SetAxis(id - kAxisIdBase, 1.f);
    } else {
      SetButton(id, true);
    }
  }
  sHeld[id] = count + 1;
}

void ReleaseControl(int id) {
  const auto it = sHeld.find(id);
  if (it == sHeld.end()) {
    return;
  }
  if (id == kToggleDebugOverlay) {
    sHeld.erase(it);
    return;
  }
  if (it->second <= 1) {
    sHeld.erase(it);
    if (id == kBeamShift) {
      PortTouch::SetBeamShift(false);
    } else if (id == kTurboFire) {
      PortTouch::SetTurboFire(false);
    } else if (id == kMenuBack) {
      PortTouch::SetMenuBack(false);
    } else if (id >= kAxisIdBase) {
      // Triggers are axes with the minimum as rest: releasing with 0 left them half pressed.
      SetAxis(id - kAxisIdBase, -1.f);
    } else {
      SetButton(id, false);
    }
    return;
  }
  --it->second;
}

void ReleaseTouchControl(TouchTarget& target) {
  const int control = target.touchControl;
  target.touchControl = -1;
  if (control >= 0 && control < kAllControls && sTouchHeld[control] > 0 &&
      --sTouchHeld[control] == 0) {
    PortInputDevices::SetTouchControl(TouchCode(control), false);
  }
}

// Presses a pill, face or added button: reports the control, and sends its own button unless a
// touch: binding takes the control over (MENU always opens F1). An added button, and a ball one,
// has no button of its own: its action is always a binding.
void PressTarget(TouchTarget& target, int control, bool remappable) {
  target.touchControl = control;
  if (control >= 0 && control < kAllControls && sTouchHeld[control]++ == 0) {
    PortInputDevices::SetTouchControl(TouchCode(control), true);
  }
  target.bound = control >= kCBall ||
                 (remappable && control >= 0 && PortInputDevices::TouchControlBound(control));
  if (!target.bound) {
    PressControl(target.id);
  }
}

// Donor's 72 dp reach, from THIS finger's down point. Never viewport fractions.
bool BeginRadial(TouchTarget& target, int kind, SDL_FingerID id) {
  target.radialOwner = int64_t(id);
  target.radialToken = PortRadial::BeginTouch(static_cast< PortRadial::Kind >(kind), target.radialOwner);
  return target.radialToken != 0;
}

void RadialMove(TouchTarget& target, float x, float y) {
  target.x = x;
  target.y = y;
  if (target.radialToken > 0) {
    const float reach = Dp(kRadialDragDp);
    PortRadial::MoveTouch(uint64_t(target.radialToken), target.radialOwner,
                          (x - target.startX) / reach, (target.startY - y) / reach);
  }
}

void ReleaseTarget(TouchTarget& target) {
  if (target.radialToken != 0) {
    if (target.radialToken > 0) {
      PortRadial::CancelTouch(uint64_t(target.radialToken), target.radialOwner);
    }
    if (target.type == kWheel) {
      sWheelOpen = false;
    }
    return;
  }
  // Nothing to zero for a hide tap, an aim drag (a distance) or a map tap.
  if (target.type == kAim) {
    PortTouch::SetAimDown(false);
    return;
  }
  if (target.type == kWheel) {
    sWheelOpen = false;
    return;
  }
  if (target.type == kHide || target.type == kMapTap || target.type == kMapPan ||
      target.type == kMapPan2) {
    return;
  }
  if (target.type == kButton) {
    ReleaseTouchControl(target);
    if (!target.bound) {
      ReleaseControl(target.id);
    }
    if (target.aiming) {
      PortTouch::SetAimDown(false);
    }
    return;
  }
  const bool left = target.type == kLeftStick;
  SetAxis(left ? kAxisLeftX : kAxisRightX, 0.f);
  SetAxis(left ? kAxisLeftY : kAxisRightY, 0.f);
}

void ReleaseAll() {
  PortRadial::CancelAllTouches(); // touch-only, including a released touch retry with no target
  for (auto& [id, target] : sTargets) {
    ReleaseTarget(target);
  }
  sTargets.clear();
  sHeld.clear();
  for (int control = 0; control < kAllControls; ++control) {
    if (sTouchHeld[control] != 0) {
      sTouchHeld[control] = 0;
      PortInputDevices::SetTouchControl(TouchCode(control), false);
    }
  }
  sAnyLeft = sAnyRight = sAnyAim = sAnyPan = sAnyPan2 = false;
  sLeftPointer = sRightPointer = sAimPointer = sPanPointer = sPan2Pointer = 0;
  sWheelOpen = false;
}

// Lets go of what a page switch hid (a face button held into the ball, a ball button held out of
// it), keeping the shared ones, so the stick stays held.
void ReleaseOffPage() {
  for (auto it = sTargets.begin(); it != sTargets.end();) {
    TouchTarget& target = it->second;
    bool off = false;
    if (target.type == kButton) {
      off = target.touchControl >= 0 && !OnPage(target.touchControl);
    } else if (target.type == kRightStick) {
      off = !OnPage(sTwin ? kCRstick : kCCStick);
    } else {
      off = target.type == kWheel;
    }
    if (!off) {
      ++it;
      continue;
    }
    ReleaseTarget(target);
    if (sAnyRight && it->first == sRightPointer) {
      sAnyRight = false;
    }
    it = sTargets.erase(it);
  }
}

bool TurboTouched() {
  for (const auto& [id, target] : sTargets) {
    if (target.id == kTurboFire) {
      return true;
    }
  }
  return false;
}

// Lets go of Turbo when its button is no longer drawn (F1 turned it off while a finger was on it).
void ReleaseTurbo() {
  if (sHeld.find(kTurboFire) == sHeld.end() && !TurboTouched()) {
    return;
  }
  for (auto it = sTargets.begin(); it != sTargets.end();) {
    TouchTarget& target = it->second;
    if (target.id != kTurboFire) {
      ++it;
      continue;
    }
    ReleaseTouchControl(target);
    // A finger sliding on Turbo was also aiming.
    if (target.aiming) {
      sAnyAim = false;
      PortTouch::SetAimDown(false);
    }
    it = sTargets.erase(it);
  }
  sHeld.erase(kTurboFire);
  PortTouch::SetTurboFire(false);
}

// Re-reads F1's "On-screen controls"; nothing held may outlive the controls it came from.
bool SyncControlsOn() {
  const bool on = PortTouch::ControlsEnabled();
  if (on == sControlsOn) {
    return false;
  }
  sControlsOn = on;
  ReleaseAll();
  return true;
}

// The eye: hidden, the view stays full screen but draws nothing, and the next touch brings the
// controls back.
void SetHidden(bool hide) {
  ReleaseAll();
  sHidden = hide;
}

// --- the layout editor ------------------------------------------------------
// The Java's: every touch is consumed, so nothing reaches the game (F1 does not pause it, so it
// keeps running underneath). Held inputs are released on the way in, so nothing is left pressed.

void BeginEdit() {
  ReleaseAll();
  // Fingers handed to SDL over the closing overlay keep their mouse side (the filter passes the
  // synthetic touch-mouse while editing, so ImGui sees them lift); their finger events become the
  // editor's from here. The Java injects a cancel instead, which a filter cannot do.
  sForwarded.clear();
  sEditFingers.clear();
  sAnyDrag = sAnyPinch = sAnyEditTool = false;
  sHidden = false;
  sAutoHidden = false;
  sEditing = true;
  sBallPage = false;
  sSelected = -1;
  sEditNotice.clear();
  sPicker.open = false;
}

// The layout is saved through the same string Android writes, so either device can read the other's.
void EndEdit(bool save) {
  if (save) {
    sLayoutText = SerializeLayout();
    PortTouch::SetLayout(sLayoutText);
  }
  sEditing = false;
  sEditPending = false;
  sSelected = -1;
  sAnyDrag = sAnyPinch = sAnyEditTool = false;
  sEditFingers.clear();
  sPicker.open = false;
}

int FreeSlot() {
  for (int slot = 0; slot < kCustomSlots; ++slot) {
    if (sCustomAction[slot].empty()) {
      return slot;
    }
  }
  return -1;
}

// The deleted stock controls the current layout has.
std::vector< int > Restorable() {
  std::vector< int > out;
  for (int c = 0; c < kControls; ++c) {
    if (sOvRemoved[c] && LayoutShows(c)) {
      out.push_back(c);
    }
  }
  return out;
}

// The selected control's name: an added button goes by its action.
std::string ControlName(int control) {
  if (control < kCCustom) {
    return kControlNames[control];
  }
  std::string label;
  if (ActionLabel(sCustomAction[control - kCCustom], label)) {
    return label;
  }
  return "Button";
}

// The next opacity in the 100, 75, 50, 25, 0 cycle; MENU wraps after 25.
int NextAlpha(int control) {
  const int floor = control == kCMenu ? kMenuMinAlpha : 0;
  for (const int step : kAlphaSteps) {
    if (step < sOvAlpha[control] && step >= floor) {
      return step;
    }
  }
  return 100;
}

void RestoreControl(int control) {
  if (!sEditing) {
    return; // the editor closed under the list
  }
  sOvRemoved[control] = false;
  sOvHidden[control] = false;
  sSelected = control;
}

void ApplyAction(int control, const std::string& key) {
  if (!sEditing) {
    return; // the editor closed under the list
  }
  if (control < 0) {
    const int slot = FreeSlot();
    if (slot < 0) {
      return;
    }
    sCustomAction[slot] = key;
    sCustomPage[slot] = sBallPage ? kPageBall : kPagePlay; // the page it was added on
    ResetControl(kCCustom + slot);
    sSelected = kCCustom + slot;
    return;
  }
  if (control >= kCCustom && !sCustomAction[control - kCCustom].empty()) {
    sCustomAction[control - kCCustom] = key;
  }
}

// pickAction: lists the deleted stock controls this layout has (Add only), then the actions a
// button can take, with the current one ticked.
void OpenActionPicker(int control) {
  const std::vector< int > restore = control < 0 ? Restorable() : std::vector< int >();
  const std::vector< std::string >& list = ButtonActions();
  const bool listActions = control >= 0 || FreeSlot() >= 0;
  sPicker = ActionPicker();
  sPicker.control = control;
  sPicker.restore = restore;
  for (const int c : restore) {
    sPicker.labels.push_back(std::string("Restore ") + kControlNames[c]);
  }
  if (listActions) {
    const std::string current =
        control >= kCCustom ? sCustomAction[control - kCCustom] : std::string();
    for (size_t i = 0; i + 1 < list.size(); i += 2) {
      sPicker.keys.push_back(list[i]);
      sPicker.labels.push_back(list[i + 1]);
      if (!current.empty() && list[i] == current) {
        sPicker.checked = int(sPicker.labels.size() - 1);
      }
    }
  }
  if (sPicker.labels.empty()) {
    return;
  }
  sPicker.open = true;
}

// The toolbar shows only what applies to the selection, to stay narrow.
struct EditTools {
  const int* list;
  int count;
};
EditTools Tools() {
  if (sSelected < 0) {
    return {kEditToolsNone, int(std::size(kEditToolsNone))};
  }
  if (sSelected >= kCCustom) {
    return {kEditToolsCustom, int(std::size(kEditToolsCustom))};
  }
  return {kEditToolsStock, int(std::size(kEditToolsStock))};
}

void LayoutEditBar(float width) {
  const EditTools tools = Tools();
  float total = 0.f;
  for (int i = 0; i < tools.count; ++i) {
    total += Dp(kEditWidthsDp[tools.list[i]]);
  }
  const float gap = Dp(4);
  total += gap * float(tools.count - 1);
  const float fit = std::min(1.f, (width - 2.f * Dp(8)) / total);
  float x = (width - total * fit) * 0.5f;
  const float top = Dp(8);
  for (int i = 0; i < tools.count; ++i) {
    const int tool = tools.list[i];
    const float w = Dp(kEditWidthsDp[tool]) * fit;
    sEditBar[tool] = {x, top, x + w, top + Dp(kEditBarHeightDp)};
    x += w + gap * fit;
  }
}

int EditBarAt(float x, float y) {
  const EditTools tools = Tools();
  for (int i = 0; i < tools.count; ++i) {
    if (sEditBar[tools.list[i]].Contains(x, y)) {
      return tools.list[i];
    }
  }
  return -1;
}

void EditAction(int tool, float width, float height) {
  switch (tool) {
  case kEditMinus:
  case kEditPlus:
    if (sSelected >= 0) {
      const float step = tool == kEditPlus ? kEditScaleStep : -kEditScaleStep;
      // Rounded so repeated steps land on tidy percentages.
      sOvScale[sSelected] = std::clamp(std::round((sOvScale[sSelected] + step) * 100.f) / 100.f,
                                       kMinScale, kMaxScale);
      ClampToScreen(sSelected, width, height);
    }
    break;
  case kEditOpacity:
    if (sSelected >= 0) {
      sOvAlpha[sSelected] = NextAlpha(sSelected);
    }
    break;
  case kEditHide:
    if (sSelected >= 0 && sSelected != kCMenu /* pinned */) {
      sOvHidden[sSelected] = !sOvHidden[sSelected];
    }
    break;
  case kEditReset:
    if (sSelected >= 0) {
      ResetControl(sSelected);
    }
    break;
  case kEditResetAll:
    for (int i = 0; i < kAllControls; ++i) {
      ResetControl(i);
    }
    // The added buttons go too.
    for (std::string& key : sCustomAction) {
      key.clear();
    }
    sCustomPage.fill(kPagePlay);
    break;
  case kEditPage:
    sBallPage = !sBallPage;
    sSelected = -1;
    break;
  case kEditCustomPage:
    if (sSelected >= kCCustom) {
      const int slot = sSelected - kCCustom;
      sCustomPage[slot] = (sCustomPage[slot] + 1) % int(std::size(kPageNames));
      if (!OnPage(sSelected)) {
        sSelected = -1; // it went to the other page
      }
    }
    break;
  case kEditAdd:
    if (FreeSlot() < 0 && Restorable().empty()) {
      sEditNotice = "All " + std::to_string(kCustomSlots) +
                    " buttons are in use: delete one to add another";
    } else {
      OpenActionPicker(-1);
    }
    break;
  case kEditAction:
    if (sSelected >= kCCustom) {
      OpenActionPicker(sSelected);
    }
    break;
  case kEditDelete:
    if (sSelected >= kCCustom) {
      sCustomAction[sSelected - kCCustom].clear();
      ResetControl(sSelected);
      sSelected = -1;
    } else if (sSelected >= 0 && sSelected != kCMenu /* pinned */) {
      // Kept where it was, so a restore brings it back as it was.
      sOvRemoved[sSelected] = true;
      sSelected = -1;
    }
    break;
  default:
    EndEdit(true); // Done
    return;
  }
}

// The smallest shown control under (x, y), so a small button on a big one (B near A, the stick under
// Visor) is still reachable. Of equal ones (added buttons stacked at the centre), the selected one,
// else the last added.
int EditHit(float x, float y, float width, float height) {
  int best = -1;
  float bestArea = FLT_MAX;
  const float slack = Dp(kEditGrabDp);
  for (int c = 0; c < kAllControls; ++c) {
    if (!EditShown(c)) {
      continue;
    }
    Rect bounds;
    ControlBounds(c, width, height, bounds);
    if (bounds.Empty()) {
      continue;
    }
    bounds.Inset(slack);
    const float area = bounds.Width() * bounds.Height();
    if (bounds.Contains(x, y) &&
        (area < bestArea || (area == bestArea && c >= kCCustom && best != sSelected))) {
      best = c;
      bestArea = area;
    }
  }
  return best;
}

// SDL hands one finger per event, so the Java's pointer list is the editor's finger map and the
// pinch reads both fingers' last positions from it.
bool EditTouchDown(SDL_FingerID id, float x, float y) {
  if (sPicker.open) {
    return true; // the dialog is modal
  }
  sEditFingers[id] = ImVec2(x, y);
  sEditNotice.clear();
  LayoutEditBar(sW);
  const int tool = EditBarAt(x, y);
  if (tool >= 0) {
    if (!sAnyEditTool) {
      sAnyEditTool = true;
      sEditToolPointer = id;
      EditAction(tool, sW, sH);
    }
    return true;
  }
  if (sAnyDrag && !sAnyPinch && sSelected >= 0) {
    const auto drag = sEditFingers.find(sDragPointer);
    if (drag != sEditFingers.end()) {
      const float dist = std::hypot(x - drag->second.x, y - drag->second.y);
      if (dist >= Dp(kMapPinchMinDp)) {
        sAnyPinch = true;
        sPinchPointer = id;
        sPinchStartDist = dist;
        sPinchStartScale = sOvScale[sSelected];
      }
    }
    return true;
  }
  if (!sAnyDrag) {
    const int hit = EditHit(x, y, sW, sH);
    sSelected = hit;
    if (hit >= 0) {
      sAnyDrag = true;
      sDragPointer = id;
      sDragLastX = x;
      sDragLastY = y;
    }
  }
  return true;
}

bool EditTouchMove(SDL_FingerID id, float x, float y) {
  if (sPicker.open) {
    return true;
  }
  if (const auto it = sEditFingers.find(id); it != sEditFingers.end()) {
    it->second = ImVec2(x, y);
  }
  if (sSelected < 0) {
    return true;
  }
  const auto drag = sAnyDrag ? sEditFingers.find(sDragPointer) : sEditFingers.end();
  const auto pinch = sAnyPinch ? sEditFingers.find(sPinchPointer) : sEditFingers.end();
  if (drag != sEditFingers.end() && pinch != sEditFingers.end()) {
    const float dist =
        std::hypot(pinch->second.x - drag->second.x, pinch->second.y - drag->second.y);
    sOvScale[sSelected] =
        std::clamp(sPinchStartScale * dist / sPinchStartDist, kMinScale, kMaxScale);
    // Keep the drag anchor current so lifting one finger doesn't jump.
    sDragLastX = drag->second.x;
    sDragLastY = drag->second.y;
    ClampToScreen(sSelected, sW, sH);
  } else if (drag != sEditFingers.end()) {
    // The dragged finger's own travel: another finger moving moves nothing.
    const float u = LayoutU(sH);
    sOvDx[sSelected] += (drag->second.x - sDragLastX) / u;
    sOvDy[sSelected] += (drag->second.y - sDragLastY) / u;
    sDragLastX = drag->second.x;
    sDragLastY = drag->second.y;
    ClampToScreen(sSelected, sW, sH);
  }
  return true;
}

void EditTouchUp(SDL_FingerID id) {
  sEditFingers.erase(id);
  if (sAnyEditTool && id == sEditToolPointer) {
    sAnyEditTool = false;
  } else if (sAnyPinch && id == sPinchPointer) {
    sAnyPinch = false;
  } else if (sAnyDrag && id == sDragPointer) {
    // The pinch finger carries on as the drag.
    sAnyDrag = sAnyPinch;
    if (sAnyDrag) {
      sDragPointer = sPinchPointer;
    }
    sAnyPinch = false;
    const auto next = sAnyDrag ? sEditFingers.find(sDragPointer) : sEditFingers.end();
    if (next != sEditFingers.end()) {
      sDragLastX = next->second.x;
      sDragLastY = next->second.y;
    }
  }
  if (sEditFingers.empty()) {
    sAnyDrag = sAnyPinch = sAnyEditTool = false;
  }
}

void EditTouchCancel() {
  sEditFingers.clear();
  sAnyDrag = sAnyPinch = sAnyEditTool = false;
}

// --- aiming, the map, the sticks --------------------------------------------

void StartButtonAim(TouchTarget& target, SDL_FingerID id, float x, float y) {
  if (std::hypot(x - target.startX, y - target.startY) < Dp(kMapTapSlopDp)) {
    return;
  }
  target.aiming = true;
  target.x = x;
  target.y = y;
  sAimPointer = id;
  sAnyAim = true;
  PortTouch::SetAimDown(true);
}

// A quick swipe up while aiming springs the morph ball. A stroke starts where the finger last moved
// down or stalled; it springs once it has gone kSwipeUpDp up, more up than sideways, within
// kSwipeUpMs. Positions are in dp.
void TrackSwipe(TouchTarget& target, float x, float y, int64_t timeMs) {
  if (target.swipeMs < 0 || y >= target.swipeY || timeMs - target.swipeMs > int64_t(kSwipeUpMs)) {
    target.swipeX = x;
    target.swipeY = y;
    target.swipeMs = timeMs;
    target.swiped = false;
    return;
  }
  const float up = target.swipeY - y;
  if (!target.swiped && up >= kSwipeUpDp && std::abs(x - target.swipeX) < up) {
    target.swiped = true;
    PortTouch::SpringSwipe();
  }
}

float PanViewDp() { return sH / sDensity; }

// The aiming finger's travel, in dp. SDL keeps no event history, so one delta per event.
void UpdateAim(TouchTarget& target, float x, float y) {
  PortTouch::AddAim((x - target.x) / sDensity, (y - target.y) / sDensity);
  TrackSwipe(target, x / sDensity, y / sDensity, int64_t(SDL_GetTicks()));
  target.x = x;
  target.y = y;
}

void UpdateMapPan(TouchTarget& target, float x, float y) {
  NoMap((x - target.x) / sDensity, (y - target.y) / sDensity, PanViewDp());
  target.x = x;
  target.y = y;
}

// Two fingers on the map: the midpoint's travel pans, the change in their spread zooms, and their
// turn rotates. Java's updatePinch reads both pointers from the event; SDL delivers one finger per
// event, so the moving one is updated here and the other keeps its last position.
void UpdatePinch(SDL_FingerID id, TouchTarget& moving, float x, float y) {
  const auto other = sTargets.find(id == sPanPointer ? sPan2Pointer : sPanPointer);
  if (other == sTargets.end()) {
    return;
  }
  TouchTarget& rest = other->second;
  const TouchTarget& a = id == sPanPointer ? moving : rest;
  const TouchTarget& b = id == sPanPointer ? rest : moving;
  const float ax = a.x, ay = a.y, bx = b.x, by = b.y;
  const float oldMidX = (ax + bx) * 0.5f, oldMidY = (ay + by) * 0.5f;
  const float beforeAngle = std::atan2(by - ay, bx - ax);
  const float before = std::hypot(ax - bx, ay - by) / sDensity;
  moving.x = x;
  moving.y = y;
  const bool first = id == sPanPointer;
  const float newAx = first ? x : ax, newAy = first ? y : ay;
  const float newBx = first ? bx : x, newBy = first ? by : y;
  NoMap(((newAx + newBx) * 0.5f - oldMidX) / sDensity,
                       ((newAy + newBy) * 0.5f - oldMidY) / sDensity, PanViewDp());
  const float now = std::hypot(newAx - newBx, newAy - newBy) / sDensity;
  if (before >= kMapPinchMinDp && now >= kMapPinchMinDp) {
    NoMap(now / before);
    float turn = std::atan2(newBy - newAy, newBx - newAx) - beforeAngle;
    if (turn > kPi) {
      turn -= 2.f * kPi;
    } else if (turn <= -kPi) {
      turn += 2.f * kPi;
    }
    NoMap(turn);
  }
}

// A floating stick keeps the centre it was grabbed at, even if the map opens while it is held.
void UpdateStick(TouchTarget& target, float x, float y, float width, float height) {
  target.x = x;
  target.y = y;
  const bool left = target.type == kLeftStick;
  const bool floated = left && sLeftFloated;
  const float centreX = floated ? sFloatX : left ? LeftStickX(width, height)
                                                 : RightStickX(width, height);
  const float centreY =
      floated ? sFloatY : left ? LeftStickY(width, height) : RightStickY(height);
  const float radius = left ? LeftStickRadius(height) : RightStickRadius(height);
  // SDL's gamepad axes are +X right and +Y down (Aurora inverts Y for the GameCube stick), so
  // screen coordinates apply as-is.
  float dx = (x - centreX) / radius;
  float dy = (y - centreY) / radius;
  float length = std::hypot(dx, dy);
  if (length > 1.f) {
    dx /= length;
    dy /= length;
    length = 1.f;
  }
  if (length < kStickDeadZone) {
    dx = 0.f;
    dy = 0.f;
  }
  SetAxis(left ? kAxisLeftX : kAxisRightX, dx);
  SetAxis(left ? kAxisLeftY : kAxisRightY, dy);
}

// The floating left stick, except on the map screen (whose free area pans) and in the editor, which
// place the fixed one.
bool FloatingStick() { return sFloating && !sEditing && !false; }

// A press within half a stick radius outside the left stick's ring grabs it.
bool OnLeftStick(float x, float y, float width, float height) {
  const float dx = x - LeftStickX(width, height);
  const float dy = y - LeftStickY(width, height);
  const float reach = LeftStickRadius(height) * kLeftStickReach;
  return dx * dx + dy * dy <= reach * reach;
}

// An added Beam menu or Visor menu button opens the game's radial menu: the finger's slide from the
// press point is its selection. The kind of menu an action key opens, or -1.
int RadialKindOfAction(const std::string& key) {
  return key == "beam_menu" ? int(PortRadial::kBeam) : key == "visor_menu" ? int(PortRadial::kVisor) : -1;
}

// Fire, Jump and Turbo face buttons aim when slid. Pills and the D-pad don't.
bool AimsWhileHeld(const TouchTarget& target) {
  return target.faceButton != nullptr &&
         (target.id == kBtnSouth || target.id == kBtnEast || target.id == kBtnNorth ||
          target.id == kTurboFire);
}

// The back button of a pointer menu, where START sits; hiding the controls hides it.
bool OnBackButton(float x, float y) {
  return sPointerMenuNow && (!sControlsOn || (!sHidden && !sAutoHidden)) &&
         sBackButtonRect.Contains(x, y);
}

// The cog beside it, where MENU sits.
bool OnCogButton(float x, float y) {
  return sPointerMenuNow && (!sControlsOn || (!sHidden && !sAutoHidden)) &&
         sCogButtonRect.Contains(x, y);
}

// nativeMinimapRect: the HUD's minimap rect into sMinimapRect, with 1 in [4] where it is drawn (0:
// the map opens but the minimap isn't drawn there). False when there is none.
bool MinimapRectNow() {
  if (!PortMinimap::Enabled() || !PortMinimap::RectFraction(sMinimapRect)) {
    return false;
  }
  sMinimapRect[4] = 1.f;
  return true;
}

// Wherever the map can open or close, a map button sits left of the eye (the minimap is not always
// drawn: the visors other than Combat hide it).
bool MapButtonState(float width, float height) {
  if (!sMapTap || width <= 0.f || height <= 0.f ||
      (!sEditing && !MinimapRectNow() && !false)) {
    return false;
  }
  BottomButtonRect(1, true, width, height, sMapButtonRect);
  return true;
}

// A finger down on a wheel button (stock or added, centred on cx, cy) opens its wheel there; not
// while one is open (the open map's overlay is handled before this). The touch is swallowed either
// way.
void OpenWheel(SDL_FingerID id, int wheel, int control, float x, float y, float cx, float cy) {
  if (sWheelOpen) {
    return;
  }
  TouchTarget target;
  target.type = kWheel;
  target.id = wheel;
  target.x = target.startX = x;
  target.y = target.startY = y;
  target.startMs = SDL_GetTicks();
  // A moved button must still open its wheel on screen.
  const float reach = Dp(kWheelRadiusDp);
  sWheelCx = std::max(reach, std::min(sW - reach, cx));
  sWheelCy = std::max(reach, std::min(sH - reach, cy));
  sWheelPointer = id;
  sWheelControl = control;
  sWheelOpen = true;
  // Wheel 1 is Beam, 0 Visor; the game's radial menu takes over when it will, else the wheel drawn
  // by DrawWheel.
  BeginRadial(target, wheel == 1 ? int(PortRadial::kBeam) : int(PortRadial::kVisor), id);
  sTargets[id] = target;
}

// Touch-down routing, first match wins: pills, added buttons, face buttons, the wheel buttons (a
// hit swallows the touch even when no wheel opens), the D-pad, the HUD minimap, the left stick,
// then the open map's pan (nothing else applies while the map is open), the classic C-stick zone,
// and the free area aims.
void AssignPointer(SDL_FingerID id, float x, float y) {
  const float width = sW, height = sH;
  for (int i = 0; i < sPillCount; ++i) {
    const PillButton& pill = sPills[i];
    if (!PillShown(pill)) {
      continue;
    }
    Rect hit;
    PillRect(pill, width, height, hit);
    if (!hit.Contains(x, y)) {
      continue;
    }
    TouchTarget target;
    target.type = kButton;
    target.id = pill.Id();
    target.pill = &pill;
    target.x = target.startX = x;
    target.y = target.startY = y;
    target.startMs = SDL_GetTicks();
    PressTarget(target, PillControl(pill), pill.Id() != kToggleDebugOverlay);
    sTargets[id] = target;
    return;
  }
  if (!sControlsOn) {
    return; // only the two menu pills above; the rest of the screen does nothing
  }
  // The topmost (last drawn) added button under the finger.
  for (int control = kAllControls - 1; control >= kCCustom; --control) {
    if (sCustomAction[control - kCCustom].empty() || CtrlHidden(control)) {
      continue;
    }
    // An unusable wheel button is hidden and lets the touch through, like the stock ones.
    const int wheel = CustomWheel(control);
    if (wheel >= 0 && (uint32_t(PortTouch::WheelState()) & kWheelValidBit) == 0) {
      continue;
    }
    const FaceGeo geo = CustomGeometry(control, width, height);
    const float dx = x - geo.x, dy = y - geo.y;
    if (dx * dx + dy * dy > geo.radius * geo.radius) {
      continue;
    }
    if (wheel >= 0) {
      OpenWheel(id, wheel, control, x, y, geo.x, geo.y);
      return;
    }
    TouchTarget target;
    target.type = kButton;
    target.id = kCustomButton;
    target.x = target.startX = x;
    target.y = target.startY = y;
    target.startMs = SDL_GetTicks();
    const int kind = RadialKindOfAction(sCustomAction[control - kCCustom]);
    if (kind >= 0 && BeginRadial(target, kind, id)) {
      // No legacy binding, button/fire or look source from this finger.
      sTargets[id] = target;
      return;
    }
    PressTarget(target, control, true);
    sTargets[id] = target;
    return;
  }
  for (int i = 0; i < 4; ++i) {
    const ControlButton& button = *sFace[i];
    if (CtrlHidden(ControlOf(button)) || !HitButton(button, x, y, width, height)) {
      continue;
    }
    TouchTarget target;
    target.type = kButton;
    target.id = button.button;
    target.faceButton = &button;
    target.x = target.startX = x;
    target.y = target.startY = y;
    target.startMs = SDL_GetTicks();
    PressTarget(target, ControlOf(button), true);
    sTargets[id] = target;
    return;
  }
  for (int i = 0; i < kBallButtons; ++i) {
    const ControlButton& button = sBallFace[i];
    if (CtrlHidden(ControlOf(button)) || !HitButton(button, x, y, width, height)) {
      continue;
    }
    // Its action is always a binding, like an added button's.
    TouchTarget target;
    target.type = kButton;
    target.id = kCustomButton;
    target.faceButton = &button;
    target.x = target.startX = x;
    target.y = target.startY = y;
    target.startMs = SDL_GetTicks();
    PressTarget(target, ControlOf(button), true);
    sTargets[id] = target;
    return;
  }
  if (sTurboButton != nullptr && !CtrlHidden(ControlOf(*sTurboButton)) &&
      HitButton(*sTurboButton, x, y, width, height)) {
    TouchTarget target;
    target.type = kButton;
    target.id = kTurboFire;
    target.faceButton = sTurboButton;
    target.x = target.startX = x;
    target.y = target.startY = y;
    target.startMs = SDL_GetTicks();
    PressTarget(target, ControlOf(*sTurboButton), true);
    sTargets[id] = target;
    return;
  }
  // Hidden wheel buttons (wheels unusable) let the touch through.
  if (sWheels && (uint32_t(PortTouch::WheelState()) & kWheelValidBit) != 0) {
    const int wheel = WheelButtonAt(x, y, width, height);
    if (wheel != -1) {
      OpenWheel(id, wheel, kCVisor + wheel, x, y, WheelButtonX(wheel, width, height),
                WheelButtonY(wheel, height));
      return;
    }
  }
  const int dpadButton =
      sWheels || CtrlHidden(kCDpad) ? -1 : DpadButtonAt(x, y, width, height);
  if (dpadButton != -1) {
    TouchTarget target;
    target.type = kButton;
    target.id = dpadButton;
    PressControl(dpadButton);
    sTargets[id] = target;
    return;
  }
  if (sMapTap && width > 0.f && height > 0.f && MinimapRectNow() && sMinimapRect[4] != 0.f &&
      x >= sMinimapRect[0] * width && x <= sMinimapRect[2] * width &&
      y >= sMinimapRect[1] * height && y <= sMinimapRect[3] * height) {
    TouchTarget target;
    target.type = kMapTap;
    target.x = target.startX = x;
    target.y = target.startY = y;
    sTargets[id] = target;
    return;
  }
  const bool leftHidden = CtrlHidden(kCLStick);
  const bool floatHere = !leftHidden && FloatingStick() && x < width * 0.5f;
  if ((floatHere || (!leftHidden && !FloatingStick() && OnLeftStick(x, y, width, height))) &&
      !sAnyLeft) {
    if (floatHere) {
      const float radius = LeftStickRadius(height);
      sFloatX = std::max(radius, std::min(width - radius, x));
      sFloatY = std::max(radius, std::min(height - radius, y));
    }
    TouchTarget target;
    target.type = kLeftStick;
    sAnyLeft = true;
    sLeftPointer = id;
    sLeftFloated = floatHere;
    sTargets[id] = target;
    UpdateStick(sTargets[id], x, y, width, height);
  } else if (false) {
    // Everything else that is free pans the open map, one finger at a time.
    if (!sAnyPan) {
      sAnyPan = true;
      sPanPointer = id;
      TouchTarget target;
      target.type = kMapPan;
      target.x = target.startX = x;
      target.y = target.startY = y;
      sTargets[id] = target;
      NoMap(0.f, 0.f, PanViewDp());
    } else if (!sAnyPan2) {
      sAnyPan2 = true;
      sPan2Pointer = id;
      TouchTarget target;
      target.type = kMapPan2;
      target.x = target.startX = x;
      target.y = target.startY = y;
      sTargets[id] = target;
    }
  } else if (sCStick && !CtrlHidden(sTwin ? kCRstick : kCCStick) &&
             InRightStickGrab(x, y, width, height)) {
    if (!sAnyRight) {
      TouchTarget target;
      target.type = kRightStick;
      sAnyRight = true;
      sRightPointer = id;
      sTargets[id] = target;
      UpdateStick(sTargets[id], x, y, width, height);
    }
  } else if (sAim && !sAnyAim) {
    // Everything else that is free: one finger at a time aims.
    sAnyAim = true;
    sAimPointer = id;
    TouchTarget target;
    target.type = kAim;
    target.x = target.startX = x;
    target.y = target.startY = y;
    sTargets[id] = target;
    PortTouch::SetAimDown(true);
  }
}

// The map's pinch and rotate run from the motion handler (SDL delivers one finger per event).

// --- drawing ----------------------------------------------------------------

// The Java view's ARGB literals, with F1's opacity and the control's own layer folded in.
ImU32 Argb(uint32_t argb, float alpha = 1.f) {
  const float a = float(argb >> 24) * alpha * sGlobal * sLocal;
  return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF,
                  static_cast< int >(a + 0.5f));
}

// --- wheel icons ---------------------------------------------------------------
// IconTracer.java's port: one of the game's small HUD icons (alpha = coverage) becomes a vector
// outline, so the wheels draw it sharp without shipping any art of the game's. The icon is
// upsampled with a bicubic filter, its half-coverage contour is traced with marching squares and
// each loop is simplified with Douglas-Peucker. Canvas fills that outline even-odd at draw time;
// ImDrawList has no even-odd fill, so the outline is filled once into an alpha texture at
// kIconTexSize instead, sharp at any size the overlay draws it.
namespace IconTracer {
constexpr int kPad = 2; // empty border, so contours touching the edge close
constexpr int kUp = 8;  // field samples per icon pixel
constexpr float kThreshold = 0.5f;
constexpr float kTolerance = 0.08f; // simplification error, in icon pixels

using Loop = std::vector< ImVec2 >;

float Cubic(float t) {
  t = std::abs(t);
  if (t < 1.f) {
    return (1.5f * t - 2.5f) * t * t + 1.f;
  }
  if (t < 2.f) {
    return ((-0.5f * t + 2.5f) * t - 4.f) * t + 2.f;
  }
  return 0.f;
}

// Catmull-Rom upsampling; field sample (i, j) sits at icon coordinate (i, j) / kUp, with pixel k's
// centre at k + 0.5.
std::vector< float > Upsample(const std::vector< float >& src, int sw, int sh, int nx, int ny) {
  std::vector< float > rows(size_t(sh) * nx);
  for (int s = 0; s < nx; ++s) {
    const float pos = float(s) / kUp - 0.5f;
    const int k0 = int(std::floor(pos)) - 1;
    for (int k = k0; k < k0 + 4; ++k) {
      if (k < 0 || k >= sw) {
        continue;
      }
      const float wgt = Cubic(pos - float(k));
      for (int r = 0; r < sh; ++r) {
        rows[size_t(r) * nx + s] += wgt * src[size_t(r) * sw + k];
      }
    }
  }
  std::vector< float > field(size_t(ny) * nx);
  for (int t = 0; t < ny; ++t) {
    const float pos = float(t) / kUp - 0.5f;
    const int k0 = int(std::floor(pos)) - 1;
    for (int k = k0; k < k0 + 4; ++k) {
      if (k < 0 || k >= sh) {
        continue;
      }
      const float wgt = Cubic(pos - float(k));
      for (int s = 0; s < nx; ++s) {
        field[size_t(t) * nx + s] += wgt * rows[size_t(k) * nx + s];
      }
    }
  }
  return field;
}

void Connect(std::vector< int >& link, int e0, int e1) {
  link[size_t(e0) * 2 + (link[size_t(e0) * 2] < 0 ? 0 : 1)] = e1;
  link[size_t(e1) * 2 + (link[size_t(e1) * 2] < 0 ? 0 : 1)] = e0;
}

// Where the contour crosses an edge, in icon pixels.
ImVec2 Crossing(const std::vector< float >& field, int nx, int edge) {
  const int cell = edge >> 1;
  const int x = cell % nx;
  const int y = cell / nx;
  const bool vertical = (edge & 1) != 0;
  const float f0 = field[cell];
  const float f1 = field[vertical ? cell + nx : cell + 1];
  const float t = (f0 - kThreshold) / (f0 - f1);
  const float px = vertical ? float(x) : float(x) + t;
  const float py = vertical ? float(y) + t : float(y);
  return ImVec2(px / kUp - kPad, py / kUp - kPad);
}

// Douglas-Peucker over points a..b (b may equal the count, meaning point 0 again).
void Mark(const Loop& pts, int a, int b, std::vector< char >& keep) {
  if (b - a < 2) {
    return;
  }
  const int count = int(pts.size());
  const ImVec2 p0 = pts[a];
  const float dx = pts[b % count].x - p0.x;
  const float dy = pts[b % count].y - p0.y;
  const float len = std::max(std::hypot(dx, dy), 1e-6f);
  int worst = -1;
  float dist = kTolerance;
  for (int i = a + 1; i < b; ++i) {
    const float d = std::abs(dy * (pts[i].x - p0.x) - dx * (pts[i].y - p0.y)) / len;
    if (d > dist) {
      dist = d;
      worst = i;
    }
  }
  if (worst < 0) {
    return;
  }
  keep[worst] = 1;
  Mark(pts, a, worst, keep);
  Mark(pts, worst, b, keep);
}

// Keeps only the points of the closed loop that matter: it is split at the point farthest from
// the first, and each half is simplified on its own.
Loop Simplified(const Loop& pts) {
  const int count = int(pts.size());
  int far = 0;
  float best = -1.f;
  for (int i = 1; i < count; ++i) {
    const float dx = pts[i].x - pts[0].x;
    const float dy = pts[i].y - pts[0].y;
    if (dx * dx + dy * dy > best) {
      best = dx * dx + dy * dy;
      far = i;
    }
  }
  std::vector< char > keep(count, 0);
  keep[0] = 1;
  keep[far] = 1;
  Mark(pts, 0, far, keep);
  Mark(pts, far, count, keep);
  Loop out;
  for (int i = 0; i < count; ++i) {
    if (keep[i]) {
      out.push_back(pts[i]);
    }
  }
  return out;
}

// The outline's loops in icon pixel units (0..w, 0..h), filled even-odd; empty when it is empty.
std::vector< Loop > Trace(const uint32_t* argb, int w, int h) {
  const int sw = w + 2 * kPad;
  const int sh = h + 2 * kPad;
  std::vector< float > src(size_t(sw) * sh);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      src[size_t(y + kPad) * sw + x + kPad] = float(argb[size_t(y) * w + x] >> 24) / 255.f;
    }
  }
  const int nx = sw * kUp + 1;
  const int ny = sh * kUp + 1;
  const std::vector< float > field = Upsample(src, sw, sh, nx, ny);

  // Each crossed grid edge links to the two edges it shares a cell segment with.
  // Edge ids: 2 * (y * nx + x) for (x,y)-(x+1,y), + 1 for (x,y)-(x,y+1).
  std::vector< int > link(size_t(nx) * ny * 4, -1);
  for (int y = 0; y + 1 < ny; ++y) {
    for (int x = 0; x + 1 < nx; ++x) {
      const float a = field[size_t(y) * nx + x];
      const float b = field[size_t(y) * nx + x + 1];
      const float c = field[size_t(y + 1) * nx + x + 1];
      const float d = field[size_t(y + 1) * nx + x];
      const int code = (a > kThreshold ? 1 : 0) | (b > kThreshold ? 2 : 0) |
                       (c > kThreshold ? 4 : 0) | (d > kThreshold ? 8 : 0);
      if (code == 0 || code == 15) {
        continue;
      }
      const int top = 2 * (y * nx + x);
      const int bottom = 2 * ((y + 1) * nx + x);
      const int left = top + 1;
      const int right = 2 * (y * nx + x + 1) + 1;
      const bool centre = (a + b + c + d) / 4.f > kThreshold;
      switch (code) {
      case 1: case 14: Connect(link, left, top); break;
      case 2: case 13: Connect(link, top, right); break;
      case 3: case 12: Connect(link, left, right); break;
      case 4: case 11: Connect(link, right, bottom); break;
      case 6: case 9: Connect(link, top, bottom); break;
      case 7: case 8: Connect(link, left, bottom); break;
      case 5:
        if (centre) {
          Connect(link, left, bottom);
          Connect(link, top, right);
        } else {
          Connect(link, left, top);
          Connect(link, right, bottom);
        }
        break;
      case 10:
        if (centre) {
          Connect(link, left, top);
          Connect(link, right, bottom);
        } else {
          Connect(link, left, bottom);
          Connect(link, top, right);
        }
        break;
      default:
        break;
      }
    }
  }

  std::vector< Loop > loops;
  std::vector< char > seen(size_t(nx) * ny * 2, 0);
  Loop loop;
  for (int start = 0; start < int(seen.size()); ++start) {
    if (seen[start] || link[size_t(start) * 2] < 0) {
      continue;
    }
    loop.clear();
    int prev = -1;
    int edge = start;
    while (edge >= 0 && !seen[edge]) {
      seen[edge] = 1;
      loop.push_back(Crossing(field, nx, edge));
      const int l0 = link[size_t(edge) * 2];
      const int next = l0 != prev ? l0 : link[size_t(edge) * 2 + 1];
      prev = edge;
      edge = next;
    }
    if (loop.size() >= 3) {
      loops.push_back(Simplified(loop));
    }
  }
  return loops;
}

// Fills the loops even-odd into a white texture's alpha, scale texels per icon pixel: 4 sample
// rows per texel, each span's horizontal coverage exact.
std::vector< uint8_t > Fill(const std::vector< Loop >& loops, int tw, int th, float scale) {
  constexpr int kRows = 4;
  std::vector< float > cover(size_t(tw) * th);
  std::vector< float > xs;
  for (int ty = 0; ty < th; ++ty) {
    float* row = &cover[size_t(ty) * tw];
    for (int sub = 0; sub < kRows; ++sub) {
      const float sy = (float(ty) + (float(sub) + 0.5f) / kRows) / scale;
      xs.clear();
      for (const Loop& l : loops) {
        for (size_t i = 0, n = l.size(); i < n; ++i) {
          const ImVec2 p = l[i], q = l[(i + 1) % n];
          if ((p.y <= sy) != (q.y <= sy)) {
            xs.push_back((p.x + (sy - p.y) / (q.y - p.y) * (q.x - p.x)) * scale);
          }
        }
      }
      std::sort(xs.begin(), xs.end());
      for (size_t i = 0; i + 1 < xs.size(); i += 2) {
        const float x0 = std::clamp(xs[i], 0.f, float(tw));
        const float x1 = std::clamp(xs[i + 1], 0.f, float(tw));
        for (int x = int(x0); x < tw && float(x) < x1; ++x) {
          row[x] += (std::min(x1, float(x + 1)) - std::max(x0, float(x))) / kRows;
        }
      }
    }
  }
  std::vector< uint8_t > rgba(size_t(tw) * th * 4, 255);
  for (size_t i = 0; i < cover.size(); ++i) {
    rgba[i * 4 + 3] = uint8_t(std::clamp(cover[i], 0.f, 1.f) * 255.f + 0.5f);
  }
  return rgba;
}
} // namespace IconTracer

// Texels along an icon's longer side: past the largest the overlay draws one (the wheel button's
// radius * 1.4 on a tablet).
constexpr int kIconTexSize = 256;
// TouchControlsView.BEAM_ICON_COLORS: Power, Ice, Wave, Plasma.
constexpr uint32_t kBeamIconColors[4] = {0xFFFFFF00, 0xFFFFFFFF, 0xFF8033FF, 0xFFCC1A1A};

struct TracedIcon {
  ImTextureID texture{};
  int w = 0, h = 0; // the icon's own size, in pixels
  bool done = false; // traced (texture is null when the outline was empty)
};
TracedIcon sTracedIcons[2][4];

// refreshWheelIcons: traces each icon once the HUD has captured it. The captures never change, so
// the textures stay for the run.
void RefreshWheelIcons() {
  for (int wheel = 0; wheel < 2; ++wheel) {
    for (int item = 0; item < 4; ++item) {
      TracedIcon& traced = sTracedIcons[wheel][item];
      if (traced.done) {
        continue;
      }
      std::vector< uint32_t > pixels;
      if (!PortTouch::WheelIcon(wheel, item, pixels) || pixels.size() < 3) {
        continue;
      }
      struct {
        int w, h;
        std::vector< uint32_t > argb;
      } icon{int(pixels[0]), int(pixels[1]), std::vector< uint32_t >(pixels.begin() + 2, pixels.end())};
      if (icon.w <= 0 || icon.h <= 0 || icon.argb.size() != size_t(icon.w) * icon.h) {
        continue;
      }
      traced.done = true;
      traced.w = icon.w;
      traced.h = icon.h;
      const std::vector< IconTracer::Loop > loops = IconTracer::Trace(icon.argb.data(), icon.w, icon.h);
      if (loops.empty()) {
        continue;
      }
      const float scale = float(kIconTexSize) / float(std::max(icon.w, icon.h));
      const int tw = std::max(1, int(std::lround(icon.w * scale)));
      const int th = std::max(1, int(std::lround(icon.h * scale)));
      const std::vector< uint8_t > rgba = IconTracer::Fill(loops, tw, th, scale);
      traced.texture = aurora_imgui_add_texture(uint32_t(tw), uint32_t(th), rgba.data());
    }
  }
}

// drawWheelIcon: the icon fitted into a size x size box at (cx, cy), beams in their colour;
// false when it has none yet.
bool DrawWheelIcon(ImDrawList* dl, int wheel, int item, float cx, float cy, float size) {
  const TracedIcon& traced = sTracedIcons[wheel][item];
  if (!traced.done || !traced.texture) {
    return false;
  }
  const float scale = size / float(std::max(traced.w, traced.h));
  const float hw = traced.w * scale / 2.f, hh = traced.h * scale / 2.f;
  dl->AddImage(traced.texture, ImVec2(cx - hw, cy - hh), ImVec2(cx + hw, cy + hh), ImVec2(0, 0),
               ImVec2(1, 1), Argb(wheel == 1 ? kBeamIconColors[item] : 0xFFFFFFFF));
  return true;
}

// actionIcon: a beam or visor action's wheel icon as wheel * 4 + item, or -1.
int ActionIcon(const std::string& key) {
  static const std::map< std::string, int > kIcons = {
      {"visor_combat", 0}, {"visor_xray", 1}, {"visor_scan", 2},  {"visor_thermal", 3},
      {"beam_power", 4},   {"beam_ice", 5},   {"beam_wave", 6},   {"beam_plasma", 7},
  };
  const auto it = kIcons.find(key);
  return it == kIcons.end() ? -1 : it->second;
}

// A GameCube colour as a fill: translucent at rest, lighter and more solid while held.
ImU32 PadFill(uint32_t rgb, bool active) {
  if (!active) {
    return Argb(0x99000000u | rgb);
  }
  auto lift = [](uint32_t c) { return c + (255 - c) * 2 / 5; };
  const uint32_t r = lift((rgb >> 16) & 0xFF), g = lift((rgb >> 8) & 0xFF), b = lift(rgb & 0xFF);
  return Argb(0xDD000000u | (r << 16) | (g << 8) | b);
}

constexpr uint32_t kRestFill = 0x77081218, kPressedFill = 0xCC48C8E8, kRestStroke = 0xBBFFFFFF,
                   kPressedStroke = 0xFFE1F8FF;

// True when a finger is down on this control itself.
bool PressedControl(const ControlButton& button) {
  for (const auto& [id, target] : sTargets) {
    if (target.faceButton == &button) {
      return true;
    }
  }
  return false;
}

bool PressedControl(const PillButton& pill) {
  for (const auto& [id, target] : sTargets) {
    if (target.pill == &pill) {
      return true;
    }
  }
  return false;
}

// One control's own opacity, the Java's pushLayer/popLayer pair. At 100% nothing is paid for.
struct ControlLayer {
  explicit ControlLayer(int control) {
    saved = sLocal;
    const int alpha = ControlAlpha(control);
    if (alpha < 0) {
      return; // LAYER_SKIP: not drawn at all
    }
    skip = false;
    if (alpha < 100) {
      sLocal = saved * (float(alpha) / 100.f);
    }
  }
  ~ControlLayer() { sLocal = saved; }
  ControlLayer(const ControlLayer&) = delete;
  ControlLayer& operator=(const ControlLayer&) = delete;
  bool skip = true;
  float saved = 1.f;
};

void Text(ImDrawList* dl, float cx, float cy, float size, ImU32 col, const char* text) {
  ImFont* font = ImGui::GetFont();
  const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.f, text);
  dl->AddText(font, size, ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f), col, text);
}

// drawCenteredLabel under Canvas.rotate(slant, x, y): ImGui has no rotated text, so the glyphs'
// vertices are turned about the centre after they are added.
void TextRotated(ImDrawList* dl, float cx, float cy, float size, ImU32 col, const char* text,
                 float degrees) {
  if (degrees == 0.f) {
    Text(dl, cx, cy, size, col, text);
    return;
  }
  const int start = dl->VtxBuffer.Size;
  Text(dl, cx, cy, size, col, text);
  const float a = degrees * kPi / 180.f, c = std::cos(a), s = std::sin(a);
  for (int i = start; i < dl->VtxBuffer.Size; ++i) {
    const ImVec2 p = dl->VtxBuffer[i].pos;
    const float dx = p.x - cx, dy = p.y - cy;
    dl->VtxBuffer[i].pos = ImVec2(cx + dx * c - dy * s, cy + dx * s + dy * c);
  }
}

// A label at `size`, or smaller when it is wider than maxWidth.
void TextFitted(ImDrawList* dl, const char* label, float cx, float cy, float size,
                float maxWidth, ImU32 col) {
  ImFont* font = ImGui::GetFont();
  const float w = font->CalcTextSizeA(size, FLT_MAX, 0.f, label).x;
  Text(dl, cx, cy, w > maxWidth ? size * maxWidth / w : size, col, label);
}

// A polygon whose corners are rounded by `radius`: the D-pad's cross, as one outline.
void RoundedPoly(ImDrawList* dl, const ImVec2* points, int count, float radius) {
  for (int i = 0; i < count; ++i) {
    const ImVec2 p = points[i];
    const ImVec2 a = points[(i + count - 1) % count];
    const ImVec2 b = points[(i + 1) % count];
    const float la = std::hypot(a.x - p.x, a.y - p.y);
    const float lb = std::hypot(b.x - p.x, b.y - p.y);
    if (la <= 0.f || lb <= 0.f) {
      dl->PathLineTo(p); // a repeated corner: nothing to round
      continue;
    }
    const float t = std::min(radius, std::min(la, lb) * 0.5f);
    const ImVec2 p1(p.x + (a.x - p.x) / la * t, p.y + (a.y - p.y) / la * t);
    const ImVec2 p2(p.x + (b.x - p.x) / lb * t, p.y + (b.y - p.y) / lb * t);
    dl->PathLineTo(p1);
    dl->PathBezierQuadraticCurveTo(p, p2, 4);
  }
}

// The arrow on a D-pad arm. The Java draws a triangle glyph; the port's ImGui font has no U+25B2,
// so the triangle is drawn as one.
void DpadArrow(ImDrawList* dl, float cx, float cy, int dx, int dy, float size, ImU32 col) {
  const float h = size * 0.5f;
  const ImVec2 tip(cx + dx * h, cy + dy * h);
  const ImVec2 a(cx - dx * h - dy * h * 0.75f, cy - dy * h - dx * h * 0.75f);
  const ImVec2 b(cx - dx * h + dy * h * 0.75f, cy - dy * h + dx * h * 0.75f);
  dl->AddTriangleFilled(tip, a, b, col);
}

void DrawStick(ImDrawList* dl, float x, float y, float radius, SDL_FingerID pointer, bool any,
               uint32_t rgb) {
  const bool active = any;
  const ImU32 fill = rgb != 0 ? Argb((active ? 0x88000000u : 0x55000000u) | rgb)
                              : Argb(active ? 0x8848C8E8 : 0x66081218);
  const ImU32 stroke = Argb(active ? 0xFFE1F8FF : 0xAAFFFFFF);
  dl->AddCircleFilled(ImVec2(x, y), radius, fill, 48);
  dl->AddCircle(ImVec2(x, y), radius, stroke, 48, Dp(2));
  float knobX = x, knobY = y;
  const auto it = any ? sTargets.find(pointer) : sTargets.end();
  if (it != sTargets.end()) {
    float dx = it->second.x - x, dy = it->second.y - y;
    const float distance = std::hypot(dx, dy);
    const float limit = radius * 0.58f;
    if (distance > limit) {
      dx *= limit / distance;
      dy *= limit / distance;
    }
    knobX += dx;
    knobY += dy;
  }
  dl->AddCircle(ImVec2(knobX, knobY), radius * 0.42f, stroke, 32, Dp(2));
}

// A face button's function for LabelContextNow (the ball hides them).
const char* FaceFunction(const ControlButton& button) {
  if (sTwin) {
    const int i = ControlOf(button) - kCJump;
    switch (sLabelContextNow) {
    case kLabelsPause:
      return kTwinPauseFunctions[i];
    case kLabelsMap:
      return kTwinMapFunctions[i];
    case kLabelsDialog:
      return kTwinDialogFunctions[i];
    default:
      return button.label;
    }
  }
  const int i = ControlOf(button) - kCA;
  switch (sLabelContextNow) {
  case kLabelsPause:
    return kGamecubePauseFunctions[i];
  case kLabelsMap:
    return kGamecubeMapFunctions[i];
  case kLabelsDialog:
    return kGamecubeDialogFunctions[i];
  default:
    return kGamecubeFunctions[i];
  }
}

void DrawButton(ImDrawList* dl, const ControlButton& button, float width, float height) {
  const FaceGeo geo = FaceGeometry(button, width, height, true);
  float x = geo.x, y = geo.y;
  const float radius = geo.radius;
  const float scale = sOvScale[ControlOf(button)];
  const bool active = PressedControl(button);
  const ImU32 fill = sColored && button.rgb != 0 ? PadFill(button.rgb, active)
                                                 : Argb(active ? kPressedFill : kRestFill);
  const ImU32 stroke = Argb(active ? kPressedStroke : kRestStroke);
  const ImU32 ink = Argb(0xFFFFFFFF);
  if (button.Kidney()) {
    KidneyPath(dl, x, y, radius, geo.half, button.arcStart, button.arcSweep);
    dl->PathFillConcave(fill);
    KidneyPath(dl, x, y, radius, geo.half, button.arcStart, button.arcSweep);
    dl->PathStroke(stroke, ImDrawFlags_Closed, Dp(2));
    const float middle = (button.arcStart + button.arcSweep * 0.5f) * kPi / 180.f;
    x += radius * std::cos(middle);
    y += radius * std::sin(middle);
  } else {
    dl->AddCircleFilled(ImVec2(x, y), radius, fill, 40);
    dl->AddCircle(ImVec2(x, y), radius, stroke, 40, Dp(2));
  }
  const int control = ControlOf(button);
  if (control >= kCBall && control < kCBall + kBallButtons) {
    // A ball button wears its action's name: the row its touch binding does.
    const std::optional< std::string > row = TouchBallLabel(TouchCode(control));
    const std::string name = row && !row->empty() ? *row : button.label;
    Text(dl, x, y, (name.size() > 5 ? Dp(10) : Dp(12)) * scale, ink, name.c_str());
    return;
  }
  // The letter big (twin: the Xbox one), the function small under it.
  const bool isTurbo = &button == sTurboButton;
  const char* letter = isTurbo ? "T"
                               : sTwin ? kTwinLetters[ControlOf(button) - kCJump]
                                       : button.label;
  const float letterSize = (sTwin ? Dp(17) : Dp(15)) * scale;
  if (!sLabels) {
    Text(dl, x, y, letterSize, ink, letter);
    return;
  }
  const char* function = isTurbo ? button.label : FaceFunction(button);
  // The function goes below the letter. A kidney is sized by its half width, and a mostly level one
  // (Y) has its text slanted along the band: upright, "Missile" ran into the border of Y's band.
  float size = radius;
  float slant = 0.f;
  if (button.Kidney()) {
    size = geo.half * 1.4f;
    float tangent = button.arcStart + button.arcSweep * 0.5f + 90.f;
    tangent = std::fmod(std::fmod(tangent, 180.f) + 270.f, 180.f) - 90.f; // into [-90, 90)
    if (std::abs(tangent) <= 45.f) {
      slant = tangent;
    }
  }
  TextRotated(dl, x, y - size * 0.2f, letterSize, ink, letter, slant);
  TextRotated(dl, x, y + size * 0.52f, Dp(8) * scale, ink, function, slant);
}

// The shoulder pills' text for LabelContextNow. In a menu, a pill that does nothing there shows just
// its button's name.
std::string PillLabel(const PillButton& pill) {
  const std::string full = pill.label;
  const size_t space = full.find(' ');
  const std::string name = space == std::string::npos ? full : full.substr(0, space);
  if (sLabelContextNow == kLabelsPlay || IsMenuPill(pill)) {
    return full;
  }
  const int sent = pill.axis >= 0 ? -1 : pill.button;
  switch (sLabelContextNow) {
  case kLabelsMorph:
    // Only the map, START and MENU stay up in the ball.
    return full;
  case kLabelsPause:
    // L and R page through the subscreens.
    if (pill.axis == kAxisTriggerL) {
      return name + " Prev";
    }
    if (pill.axis == kAxisTriggerR) {
      return name + " Next";
    }
    if (sent == kBtnEast) {
      return name + " Back";
    }
    if (sent == kBtnSouth) {
      return name + " Select";
    }
    if (sent == kBtnNorth) {
      return name + " View";
    }
    return name;
  case kLabelsMap:
    // L zooms out and R in; B and Z leave.
    if (pill.axis == kAxisTriggerL) {
      return name + " Out";
    }
    if (pill.axis == kAxisTriggerR) {
      return name + " In";
    }
    if (sent == kBtnEast || sent == kBtnRightShoulder) {
      return name + " Exit";
    }
    if (sent == kBtnSouth) {
      return name + " World";
    }
    if (sent == kBtnNorth) {
      return name + " Legend";
    }
    return name;
  default:
    return sent == kBtnSouth ? name + " OK" : name;
  }
}

// START and MENU sit in the bottom-left corner, and their labels name no game function.
void DrawCornerButton(ImDrawList* dl, int slot, const Rect& bounds, const ImU32& fill,
                      const ImU32& stroke) {
  const float cx = bounds.Cx(), cy = bounds.Cy();
  const float radius = bounds.Width() * 0.5f;
  dl->AddCircleFilled(ImVec2(cx, cy), radius, fill, 32);
  dl->AddCircle(ImVec2(cx, cy), radius, stroke, 32, Dp(2));
  const float s = radius * 0.36f;
  if (slot == 0) {
    // A pause glyph: two bars.
    const float barW = s * 0.55f;
    const float corner = barW * 0.3f;
    dl->AddRectFilled(ImVec2(cx - s * 0.75f - barW / 2.f, cy - s),
                      ImVec2(cx - s * 0.75f + barW / 2.f, cy + s), stroke, corner);
    dl->AddRectFilled(ImVec2(cx + s * 0.75f - barW / 2.f, cy - s),
                      ImVec2(cx + s * 0.75f + barW / 2.f, cy + s), stroke, corner);
    return;
  }
  // A cog: eight square teeth around a ring, with a hole.
  constexpr int kTeeth = 8;
  const float outer = s * 1.3f, inner = s * 0.98f;
  const double step = 2.0 * double(kPi) / double(kTeeth);
  const double half = step * 0.22;
  for (int i = 0; i < kTeeth; ++i) {
    const double a = double(i) * step;
    const double angles[6] = {a - step / 2 + half, a - half,      a - half * 0.8,
                              a + half * 0.8,       a + half,      a + step / 2 - half};
    const float radii[6] = {inner, inner, outer, outer, inner, inner};
    for (int k = 0; k < 6; ++k) {
      const ImVec2 p(cx + radii[k] * float(std::cos(angles[k])),
                     cy + radii[k] * float(std::sin(angles[k])));
      dl->PathLineTo(p);
    }
  }
  dl->PathStroke(stroke, ImDrawFlags_Closed, Dp(2));
  dl->AddCircle(ImVec2(cx, cy), s * 0.42f, stroke, 20, Dp(2));
}

void DrawPillButton(ImDrawList* dl, const PillButton& pill, float width, float height) {
  Rect bounds;
  PillRect(pill, width, height, bounds);
  const bool active = PressedControl(pill);
  const ImU32 fill = sColored && pill.rgb != 0 ? PadFill(pill.rgb, active)
                                               : Argb(active ? kPressedFill : 0x99081218);
  const ImU32 stroke = Argb(active ? kPressedStroke : 0xCCFFFFFF);
  const int slot = CornerSlot(pill);
  if (slot >= 0) {
    DrawCornerButton(dl, slot, bounds, fill, stroke);
    return;
  }
  const float radius = std::min(bounds.Width(), bounds.Height()) * 0.28f;
  dl->AddRectFilled(ImVec2(bounds.x0, bounds.y0), ImVec2(bounds.x1, bounds.y1), fill, radius);
  dl->AddRect(ImVec2(bounds.x0, bounds.y0), ImVec2(bounds.x1, bounds.y1), stroke, radius, 0,
              Dp(2));
  if (sTwin && pill.axis < 0 && pill.button == kBtnRightShoulder) {
    // The Map pill wears Xbox's Menu glyph (three lines), as the hint does.
    const float h = bounds.Height();
    const float gx = sLabels ? bounds.x0 + bounds.Width() * 0.26f : bounds.Cx();
    const float lineW = h * 0.34f;
    const float lineH = std::max(1.5f, h * 0.06f);
    for (int i = -1; i <= 1; ++i) {
      const float ly = bounds.Cy() + float(i) * h * 0.16f;
      dl->AddRectFilled(ImVec2(gx - lineW / 2.f, ly - lineH / 2.f),
                        ImVec2(gx + lineW / 2.f, ly + lineH / 2.f), stroke);
    }
    if (sLabels) {
      Text(dl, bounds.x0 + bounds.Width() * 0.62f, bounds.Cy(), Dp(11), Argb(0xFFFFFFFF),
           pill.label);
    }
    return;
  }
  // "LB Jump": without descriptions, just the button's name.
  std::string label;
  if (sLabels) {
    label = PillLabel(pill);
  } else {
    const std::string full = pill.label;
    const size_t space = full.find(' ');
    label = space == std::string::npos ? full : full.substr(0, space);
  }
  Text(dl, bounds.Cx(), bounds.Cy(), label.size() > 2 ? Dp(11) : Dp(13), Argb(0xFFFFFFFF),
       label.c_str());
}

// The D-pad is one cross, as on the GameCube pad.
void DrawDpad(ImDrawList* dl, float width, float height) {
  const float cx = DpadX(width, height);
  const float cy = DpadY(height);
  const float arm = kDpadArm * LayoutU(height) * sOvScale[kCDpad];
  const float half = kDpadHalf * LayoutU(height) * sOvScale[kCDpad];
  const float corner = half * 0.35f;
  // Clockwise round the cross: the 12 corners where its four arms meet.
  const ImVec2 cross[12] = {
      {cx - half, cy - arm}, {cx + half, cy - arm}, {cx + half, cy - half},
      {cx + arm, cy - half}, {cx + arm, cy + half}, {cx + half, cy + half},
      {cx + half, cy + arm}, {cx - half, cy + arm}, {cx - half, cy + half},
      {cx - arm, cy + half}, {cx - arm, cy - half}, {cx - half, cy - half}};
  RoundedPoly(dl, cross, 12, corner);
  dl->PathFillConcave(sColored ? PadFill(kGcGrey, false) : Argb(0x77081218));
  // A held arm lights from the centre out.
  for (int i = 0; i < 4; ++i) {
    if (sHeld.find(kDpadButtons[i]) == sHeld.end()) {
      continue;
    }
    const float endX = cx + float(kDpadDx[i]) * arm;
    const float endY = cy + float(kDpadDy[i]) * arm;
    const ImVec2 a(std::min(cx, endX) - (kDpadDx[i] == 0 ? half : 0.f),
                   std::min(cy, endY) - (kDpadDy[i] == 0 ? half : 0.f));
    const ImVec2 b(std::max(cx, endX) + (kDpadDx[i] == 0 ? half : 0.f),
                   std::max(cy, endY) + (kDpadDy[i] == 0 ? half : 0.f));
    dl->AddRectFilled(a, b, sColored ? PadFill(kGcGrey, true) : Argb(0xCC48C8E8), corner);
  }
  RoundedPoly(dl, cross, 12, corner);
  dl->PathStroke(Argb(0xBBFFFFFF), ImDrawFlags_Closed, Dp(2));
  for (int i = 0; i < 4; ++i) {
    DpadArrow(dl, cx + float(kDpadDx[i]) * arm * 0.62f, cy + float(kDpadDy[i]) * arm * 0.62f,
              kDpadDx[i], kDpadDy[i], Dp(12) * sOvScale[kCDpad], Argb(0xFFFFFFFF));
  }
}

// The hide button: an eye.
void DrawEye(ImDrawList* dl, const Rect& bounds) {
  const float cx = bounds.Cx(), cy = bounds.Cy();
  const float r = bounds.Width() * 0.5f;
  dl->AddCircleFilled(ImVec2(cx, cy), r, Argb(0x99081218), 32);
  dl->AddCircle(ImVec2(cx, cy), r, Argb(0xCCFFFFFF), 32, Dp(2));
  const float halfW = r * 0.62f, lid = r * 0.36f;
  const ImU32 ink = Argb(0xCCFFFFFF);
  dl->PathLineTo(ImVec2(cx - halfW, cy));
  dl->PathBezierQuadraticCurveTo(ImVec2(cx, cy - lid * 2.f), ImVec2(cx + halfW, cy), 16);
  dl->PathBezierQuadraticCurveTo(ImVec2(cx, cy + lid * 2.f), ImVec2(cx - halfW, cy), 16);
  dl->PathStroke(ink, ImDrawFlags_Closed, Dp(2));
  // The eye's pupil is filled, so it needs its own path.
  dl->AddCircleFilled(ImVec2(cx, cy), lid * 0.55f, Argb(0xCCFFFFFF), 16);
}

// A round button with a left arrow: B in the game's menus and map.
void DrawBackButton(ImDrawList* dl) {
  const float cx = sBackButtonRect.Cx(), cy = sBackButtonRect.Cy();
  const float radius = sBackButtonRect.Width() * 0.5f;
  const ImU32 fill = Argb(sHeld.find(kMenuBack) != sHeld.end() ? 0xCC3A5A70 : 0x99081218);
  const ImU32 stroke = Argb(0xBBFFFFFF);
  dl->AddCircleFilled(ImVec2(cx, cy), radius, fill, 32);
  dl->AddCircle(ImVec2(cx, cy), radius, stroke, 32, Dp(2));
  const float w = radius * 0.5f;
  dl->AddLine(ImVec2(cx + w, cy), ImVec2(cx - w, cy), stroke, Dp(2));
  dl->AddLine(ImVec2(cx - w * 0.3f, cy - w * 0.7f), ImVec2(cx - w, cy), stroke, Dp(2));
  dl->AddLine(ImVec2(cx - w, cy), ImVec2(cx - w * 0.3f, cy + w * 0.7f), stroke, Dp(2));
}

// A round button with a folded map: three panels, the middle one raised.
void DrawMapButton(ImDrawList* dl) {
  const float cx = sMapButtonRect.Cx(), cy = sMapButtonRect.Cy();
  const float radius = sMapButtonRect.Width() * 0.5f;
  dl->AddCircleFilled(ImVec2(cx, cy), radius, Argb(0x99081218), 32);
  const ImU32 stroke = Argb(0xBBFFFFFF);
  dl->AddCircle(ImVec2(cx, cy), radius, stroke, 32, Dp(2));
  const float w = radius * 1.1f, h = radius * 0.8f;
  const float l = cx - w / 2.f, t = cy - h / 2.f, tilt = h * 0.12f;
  dl->PathLineTo(ImVec2(l, t + tilt));
  dl->PathLineTo(ImVec2(l + w / 3.f, t));
  dl->PathLineTo(ImVec2(l + 2.f * w / 3.f, t + tilt));
  dl->PathLineTo(ImVec2(l + w, t));
  dl->PathLineTo(ImVec2(l + w, t + h - tilt));
  dl->PathLineTo(ImVec2(l + 2.f * w / 3.f, t + h));
  dl->PathLineTo(ImVec2(l + w / 3.f, t + h - tilt));
  dl->PathLineTo(ImVec2(l, t + h));
  dl->PathStroke(stroke, ImDrawFlags_Closed, Dp(2));
  dl->AddLine(ImVec2(l + w / 3.f, t), ImVec2(l + w / 3.f, t + h - tilt), stroke, Dp(2));
  dl->AddLine(ImVec2(l + 2.f * w / 3.f, t + tilt), ImVec2(l + 2.f * w / 3.f, t + h), stroke, Dp(2));
}

// The Visor (left) and Beam (right) buttons.
void DrawWheelButtons(ImDrawList* dl, float width, float height) {
  const uint32_t mask = uint32_t(PortTouch::WheelState());
  const bool shown = (mask & kWheelValidBit) != 0 || sEditing;
  if (!shown) {
    return;
  }
  RefreshWheelIcons();
  for (int wheel = 0; wheel < 2; ++wheel) {
    ControlLayer layer(kCVisor + wheel);
    if (layer.skip) {
      continue;
    }
    const float cx = WheelButtonX(wheel, width, height);
    const float cy = WheelButtonY(wheel, height);
    const float radius = WheelButtonRadius(wheel, height);
    const bool active = sWheelOpen && sWheelControl == kCVisor + wheel;
    dl->AddCircleFilled(ImVec2(cx, cy), radius,
                        Argb(active ? 0xCC48C8E8 : 0x77081218), 32);
    dl->AddCircle(ImVec2(cx, cy), radius, Argb(active ? 0xFFE1F8FF : 0xBBFFFFFF), 32, Dp(2));
    // The current item's icon, or the label until the HUD has loaded it.
    const int current = wheel == 0 ? int((mask >> 8) & 3) : int((mask >> 10) & 3);
    if (!DrawWheelIcon(dl, wheel, current, cx, cy, radius * 1.4f)) {
      Text(dl, cx, cy, Dp(13) * sOvScale[kCVisor + wheel], Argb(0xFFFFFFFF),
           kWheelButtonLabels[wheel]);
    }
  }
}

// The sector under (x, y) of the open wheel, or -1 inside the centre.
int WheelSector(float x, float y) {
  const float dx = x - sWheelCx, dy = y - sWheelCy;
  const float dead = Dp(kWheelDeadDp);
  if (dx * dx + dy * dy < dead * dead) {
    return -1;
  }
  if (std::abs(dx) > std::abs(dy)) {
    return dx > 0.f ? 1 : 3;
  }
  return dy < 0.f ? 0 : 2;
}

bool WheelOwned(uint32_t mask, int wheel, int item) {
  return (mask & (1u << (wheel * 4 + item))) != 0;
}

// The wheel opens over everything: four sectors, the one under the finger lit.
void DrawWheel(ImDrawList* dl) {
  const auto it = sTargets.find(sWheelPointer);
  if (!sWheelOpen || it == sTargets.end()) {
    return;
  }
  const TouchTarget& target = it->second;
  if (target.radialToken != 0) {
    return;
  }
  const uint32_t mask = uint32_t(PortTouch::WheelState());
  const int sector = WheelSector(target.x, target.y);
  const float outer = Dp(kWheelRadiusDp), inner = Dp(kWheelDeadDp);
  const int currentIndex = target.id == 0 ? int((mask >> 8) & 3) : int((mask >> 10) & 3);
  for (int i = 0; i < 4; ++i) {
    const int item = kWheelItems[target.id][i];
    const bool owned = WheelOwned(mask, target.id, item);
    // Sector i is centred on up (-90), right (0), down (90), left (180).
    const float start = -135.f + 90.f * float(i);
    const float a0 = start * kPi / 180.f, a1 = (start + 90.f) * kPi / 180.f;
    const ImVec2 c(sWheelCx, sWheelCy);
    dl->PathArcTo(c, outer, a0, a1, 16);
    dl->PathArcTo(c, inner, a1, a0, 8);
    dl->PathFillConcave(Argb(i == sector && owned ? 0xDD48C8E8
                                                  : owned ? 0xAA081218 : 0x66081218));
    dl->PathArcTo(c, outer, a0, a1, 16);
    dl->PathArcTo(c, inner, a1, a0, 8);
    dl->PathStroke(Argb(item == currentIndex ? 0xFFE0C020 : kRestStroke), ImDrawFlags_Closed,
                   Dp(2));
    // An item not found yet leaves its sector empty.
    if (!owned) {
      continue;
    }
    const float mid = (start + 45.f) * kPi / 180.f;
    const float labelR = (outer + inner) * 0.5f;
    const float lx = sWheelCx + labelR * std::cos(mid);
    const float ly = sWheelCy + labelR * std::sin(mid);
    if (!DrawWheelIcon(dl, target.id, item, lx, ly, Dp(kWheelIconDp))) {
      Text(dl, lx, ly, Dp(12), Argb(0xFFFFFFFF), kWheelLabels[target.id][i]);
    }
  }
}

// A finger lifted off a wheel: a slide to an owned sector picks it; a quick tap on Visor picks Scan
// when the setting is on; the centre cancels.
void FinishWheel(const TouchTarget& target) {
  const uint32_t mask = uint32_t(PortTouch::WheelState());
  if ((mask & kWheelValidBit) == 0) {
    return;
  }
  const int sector = WheelSector(target.x, target.y);
  if (sector >= 0) {
    const int item = kWheelItems[target.id][sector];
    if (WheelOwned(mask, target.id, item)) {
      if (target.id == 0) {
        PortTouch::RequestVisor(item);
      } else {
        PortTouch::RequestBeam(item);
      }
    }
    return;
  }
  const bool quick = SDL_GetTicks() - target.startMs < kWheelTapMs;
  const bool still = std::hypot(target.x - target.startX, target.y - target.startY) <
                     Dp(kMapTapSlopDp);
  if (target.id == 0 && sVisorTapScan && quick && still && WheelOwned(mask, 0, 2)) {
    PortTouch::RequestVisor(2);
  }
}

// What an added button says: its action's label, shortened to fit a button.
std::string ShortLabel(const std::string& key) {
  if (key == "dpad_up") return "D-pad up";
  if (key == "dpad_down") return "D-pad down";
  if (key == "dpad_left") return "D-pad left";
  if (key == "dpad_right") return "D-pad right";
  if (key == "beam_next") return "Beam +";
  if (key == "beam_prev") return "Beam -";
  if (key == "visor_next") return "Visor +";
  if (key == "visor_prev") return "Visor -";
  if (key == "spring_ball") return "Spring";
  if (key == "dodge") return "Dodge";
  if (key == "port_menu") return "Menu";
  if (key == "save_state") return "Save";
  if (key == "load_state") return "Load";
  if (key == "toggle_original") return "Original";
  if (key == "photo_mode") return "Photo";
  if (key == "gyro_aim") return "Gyro";
  if (key == "ball_power_bomb") return "P.Bomb";
  if (key == "ball_boost") return "Boost";
  if (key == "ball_spider") return "Spider";
  std::string label;
  if (!ActionLabel(key, label)) {
    return key;
  }
  const size_t paren = label.find(" (");
  if (paren != std::string::npos && paren > 0) {
    label = label.substr(0, paren); // "L (click)"
  }
  for (const char* suffix : {" Beam", " Visor"}) {
    const size_t at = label.rfind(suffix);
    if (at != std::string::npos && at + std::strlen(suffix) == label.size()) {
      label = label.substr(0, at);
      break;
    }
  }
  return label;
}

// Round buttons with the game's icon for a beam or visor, else a short label. A wheel button wears
// its current item, and hides like the stock ones while the wheels are unusable.
void DrawCustomButtons(ImDrawList* dl, float width, float height) {
  const uint32_t mask = uint32_t(PortTouch::WheelState());
  for (int control = kCCustom; control < kAllControls; ++control) {
    const std::string& key = sCustomAction[control - kCCustom];
    if (key.empty()) {
      continue;
    }
    const int wheel = CustomWheel(control);
    if (wheel >= 0 && (mask & kWheelValidBit) == 0 && !sEditing) {
      continue;
    }
    ControlLayer layer(control);
    if (layer.skip) {
      continue;
    }
    const FaceGeo geo = CustomGeometry(control, width, height);
    const bool active = sTouchHeld[control] > 0 || (sWheelOpen && sWheelControl == control);
    dl->AddCircleFilled(ImVec2(geo.x, geo.y), geo.radius,
                        Argb(active ? 0xCC48C8E8 : 0x77081218), 40);
    dl->AddCircle(ImVec2(geo.x, geo.y), geo.radius,
                  Argb(active ? 0xFFE1F8FF : 0xBBFFFFFF), 40, Dp(2));
    if (wheel >= 0) {
      RefreshWheelIcons();
      const int current = wheel == 0 ? int((mask >> 8) & 3) : int((mask >> 10) & 3);
      if (!DrawWheelIcon(dl, wheel, current, geo.x, geo.y, geo.radius * 1.3f)) {
        TextFitted(dl, kWheelButtonLabels[wheel], geo.x, geo.y, Dp(14) * sOvScale[control],
                   geo.radius * 1.7f, Argb(0xFFFFFFFF));
      }
      continue;
    }
    const int icon = ActionIcon(key);
    if (icon >= 0) {
      RefreshWheelIcons();
    }
    if (icon < 0 || !DrawWheelIcon(dl, icon / 4, icon % 4, geo.x, geo.y, geo.radius * 1.3f)) {
      const std::string label = ShortLabel(key);
      TextFitted(dl, label.c_str(), geo.x, geo.y, Dp(14) * sOvScale[control], geo.radius * 1.7f,
                 Argb(0xFFFFFFFF));
    }
  }
}

// DashPathEffect's stand-in: ImDrawList has no dash pattern, so each edge is walked in dashes.
void DashedRect(ImDrawList* dl, const Rect& r, float dash, float gap, ImU32 col, float thickness) {
  const float step = dash + gap;
  auto edge = [&](float x0, float y0, float x1, float y1) {
    const float len = std::hypot(x1 - x0, y1 - y0);
    if (len <= 0.f) {
      return;
    }
    const float ux = (x1 - x0) / len, uy = (y1 - y0) / len;
    for (float t = 0.f; t < len; t += step) {
      const float e = std::min(t + dash, len);
      dl->AddLine(ImVec2(x0 + ux * t, y0 + uy * t), ImVec2(x0 + ux * e, y0 + uy * e), col,
                  thickness);
    }
  };
  edge(r.x0, r.y0, r.x1, r.y0);
  edge(r.x1, r.y0, r.x1, r.y1);
  edge(r.x1, r.y1, r.x0, r.y1);
  edge(r.x0, r.y1, r.x0, r.y0);
}

// The editor's overlay: every shown control's bounds (solid and yellow for the selection, dashed
// otherwise, orange when hidden), the toolbar, and the hint line.
void DrawEditOverlay(ImDrawList* dl, float width, float height) {
  const float slack = Dp(kEditGrabDp) * 0.5f;
  const float corner = Dp(8);
  for (int c = 0; c < kAllControls; ++c) {
    if (!EditShown(c)) {
      continue;
    }
    Rect bounds;
    ControlBounds(c, width, height, bounds);
    if (bounds.Empty()) {
      continue;
    }
    bounds.Inset(slack);
    if (c == sSelected) {
      dl->AddRectFilled(ImVec2(bounds.x0, bounds.y0), ImVec2(bounds.x1, bounds.y1),
                        Argb(0x5548C8E8), corner);
      dl->AddRect(ImVec2(bounds.x0, bounds.y0), ImVec2(bounds.x1, bounds.y1), Argb(0xFFFFE04A),
                  corner, 0, Dp(2));
    } else {
      // Hidden ones in orange, so they stand out from the faint ones.
      DashedRect(dl, bounds, Dp(6), Dp(4), Argb(sOvHidden[c] ? 0xFFFF9A3C : 0xCCFFFFFF), Dp(2));
    }
    // Tag what isn't default, so a faded control reads as faded on purpose.
    char tag[16];
    const char* text = nullptr;
    if (sOvHidden[c]) {
      text = "hidden";
    } else if (sOvAlpha[c] != 100) {
      std::snprintf(tag, sizeof(tag), "%d%%", sOvAlpha[c]);
      text = tag;
    }
    if (text != nullptr) {
      Text(dl, bounds.Cx(), bounds.y0 + Dp(10), Dp(10), Argb(0xFFFFFFFF), text);
    }
  }
  LayoutEditBar(width);
  const EditTools tools = Tools();
  for (int i = 0; i < tools.count; ++i) {
    const int tool = tools.list[i];
    const Rect& bar = sEditBar[tool];
    dl->AddRectFilled(ImVec2(bar.x0, bar.y0), ImVec2(bar.x1, bar.y1),
                      Argb(tool == kEditDone ? 0xDD2E8B57 : 0xDD081218), corner);
    dl->AddRect(ImVec2(bar.x0, bar.y0), ImVec2(bar.x1, bar.y1), Argb(0xCCFFFFFF), corner, 0,
                Dp(2));
    std::string label = kEditLabels[tool];
    if (tool == kEditHide && sSelected >= 0 && sOvHidden[sSelected]) {
      label = "Show";
    } else if (tool == kEditOpacity && sSelected >= 0) {
      label = "Opacity " + std::to_string(sOvAlpha[sSelected]) + "%"; // a tap steps it down
    } else if (tool == kEditPage) {
      label = sBallPage ? "Play page" : "Ball page"; // the page a tap goes to
    } else if (tool == kEditCustomPage && sSelected >= kCCustom) {
      label = std::string("On ") + kPageNames[sCustomPage[sSelected - kCCustom]];
    }
    // Tools that can't act now are dimmed: Hide on MENU, Add with no free slot.
    const bool idle = ((tool == kEditHide || tool == kEditDelete) && sSelected == kCMenu) ||
                      (tool == kEditAdd && FreeSlot() < 0 && Restorable().empty());
    Text(dl, bar.Cx(), bar.Cy(), tool <= kEditPlus ? Dp(22) : Dp(14),
         Argb(0xFFFFFFFF, idle ? 90.f / 255.f : 1.f), label.c_str());
  }

  // The hint line: the page, the selected control, or why the last tool did nothing.
  std::string hint = std::string(sBallPage ? "Morph Ball page. " : "Gameplay page. ") +
                     "Tap a control to select it: drag to move, pinch to resize. "
                     "Add: a new or deleted button";
  if (!sEditNotice.empty()) {
    hint = sEditNotice;
  } else if (sSelected >= 0) {
    hint = ControlName(sSelected) + "  " +
           std::to_string(static_cast< int >(std::lround(sOvScale[sSelected] * 100.f))) + "%";
    if (sOvAlpha[sSelected] != 100) {
      hint += "  \xC2\xB7 opacity " + std::to_string(sOvAlpha[sSelected]) + "%";
    }
    if (sOvHidden[sSelected]) {
      hint += "  \xC2\xB7 hidden";
    }
  }
  const float size = Dp(13);
  ImFont* font = ImGui::GetFont();
  const float textWidth = font->CalcTextSizeA(size, FLT_MAX, 0.f, hint.c_str()).x;
  const float top = sEditBar[kEditDone].y1 + Dp(kEditBarGapDp);
  const float cx = width * 0.5f;
  const Rect box{cx - textWidth * 0.5f - Dp(10), top, cx + textWidth * 0.5f + Dp(10),
                 top + Dp(26)};
  dl->AddRectFilled(ImVec2(box.x0, box.y0), ImVec2(box.x1, box.y1), Argb(0xCC081218), corner);
  Text(dl, cx, box.Cy(), size, Argb(0xFFFFFFFF), hint.c_str());
}

// The action picker, as an ImGui window: the port has no dialogs of its own, and the picker has to
// be reachable by a thumb, so the rows are tall and the list scrolls.
void DrawActionPicker() {
  if (!sPicker.open) {
    return;
  }
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float width = std::min(vp->Size.x * 0.9f, Dp(520));
  const float rowHeight = std::max(Dp(40), ImGui::GetTextLineHeight() * 1.6f);
  const float listHeight =
      std::min(float(sPicker.labels.size()) * rowHeight + Dp(8), vp->Size.y * 0.6f);
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.f), ImVec2(width, vp->Size.y * 0.85f));
  // A stable id behind the visible title, so the window is one window however the title changes.
  const char* title = sPicker.control < 0 ? "Add a button###touch-action-picker"
                                          : "Button action###touch-action-picker";
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_AlwaysAutoResize;
  if (ImGui::Begin(title, nullptr, flags)) {
    int pick = -1;
    ImGui::BeginChild("##actions", ImVec2(0.f, listHeight), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < sPicker.labels.size(); ++i) {
      ImGui::PushID(static_cast< int >(i));
      if (ImGui::Selectable(sPicker.labels[i].c_str(), int(i) == sPicker.checked, 0,
                            ImVec2(0.f, rowHeight))) {
        pick = static_cast< int >(i);
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::Separator();
    // Cancel, or a tap outside, as the cancelable AlertDialog.
    if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, rowHeight)) ||
        (sPicker.armed && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
         !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))) {
      sPicker.open = false;
    }
    // The opening tap can reach ImGui a frame after the picker shows, so wait for its release, or
    // for two frames with nothing down.
    ++sPicker.shownFrames;
    sPicker.armed = sPicker.armed || ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
                    (sPicker.shownFrames >= 2 && !ImGui::IsMouseDown(ImGuiMouseButton_Left));
    if (pick >= 0) {
      const int restoreCount = static_cast< int >(sPicker.restore.size());
      sPicker.open = false;
      if (pick < restoreCount) {
        RestoreControl(sPicker.restore[pick]);
      } else {
        ApplyAction(sPicker.control, sPicker.keys[size_t(pick) - size_t(restoreCount)]);
      }
    }
  }
  ImGui::End();
}

void DrawControlButton(ImDrawList* dl, const ControlButton& button, float width, float height) {
  ControlLayer layer(ControlOf(button));
  if (layer.skip) {
    return;
  }
  DrawButton(dl, button, width, height);
}

// The retired wheels setting put the stock Visor and Beam buttons in the classic and twin layouts in
// place of the D-pad. Once, in such a layout, they become added wheel buttons where they were drawn
// and the D-pad goes, so the layout plays as before; the stock pair goes too, or the default layout
// (which shares the saved layout) would show both. The Java view does the same (MP1 5ef1edfe1).
void MigrateLegacyWheels(float width, float height) {
  if (sEditing || !PortTouch::WheelsEnabled() || width <= 0.f || height <= 0.f) {
    return;
  }
  if (!sWheels) {
    const float u = LayoutU(height);
    int slot = 0;
    for (int wheel = 0; wheel < 2; ++wheel) {
      const int stock = kCVisor + wheel;
      if (Removed(stock)) {
        continue;
      }
      while (slot < kCustomSlots && !sCustomAction[slot].empty()) {
        ++slot;
      }
      if (slot == kCustomSlots) {
        break;
      }
      const int control = kCCustom + slot;
      sCustomAction[slot] = PortTouch::kTouchWheelActions[wheel * 2];
      sCustomPage[slot] = kPagePlay;
      sOvDx[control] = std::clamp((WheelButtonX(wheel, width, height) - width * 0.5f) / u,
                                  -kMaxOffset, kMaxOffset);
      sOvDy[control] =
          std::clamp((WheelButtonY(wheel, height) - height * 0.5f) / u, -kMaxOffset, kMaxOffset);
      sOvScale[control] =
          std::clamp(WheelButtonRadius(wheel, height) / (kCustomRadius * u), kMinScale, kMaxScale);
      sOvAlpha[control] = sOvAlpha[stock];
      sOvHidden[control] = sOvHidden[stock];
      ResetControl(stock);
      sOvRemoved[stock] = true;
    }
    sOvRemoved[kCDpad] = true;
    sLayoutText = SerializeLayout();
    PortTouch::SetLayout(sLayoutText);
  }
  PortTouch::SetWheelsEnabled(false);
}

// The gameplay controls: everything but the pointer menu's own buttons, which are drawn before this
// (outside the opacity).
void DrawControls(ImDrawList* dl, float width, float height, bool classic) {
  // Classic and twin stick are exclusive (the native side keeps them so).
  sTwin = !classic && PortTouch::TwinStick();
  sFace = sTwin ? std::array< const ControlButton*, 4 >{&kTwinFace[0], &kTwinFace[1], &kTwinFace[2],
                                                        &kTwinFace[3]}
                : std::array< const ControlButton*, 4 >{&kGamecubeFace[0], &kGamecubeFace[1],
                                                        &kGamecubeFace[2], &kGamecubeFace[3]};
  sPills = sTwin ? kTwinPills : kGamecubePills;
  sPillCount = sTwin ? int(std::size(kTwinPills)) : int(std::size(kGamecubePills));
  sBallFace = sTwin ? kTwinBallFace : kBallFace;
  // The editor keeps the page its toolbar picked; play follows the game.
  const bool page = sEditing ? sBallPage : InBall();
  if (page != sBallPage) {
    sBallPage = page;
    ReleaseOffPage();
  }
  sTurbo = PortTouch::Turbo();
  sTurboButton = sTurbo ? (sTwin ? &kTwinTurbo : &kGamecubeTurbo) : nullptr;
  if (!sTurbo) {
    ReleaseTurbo();
  }
  sCStick = classic || sTwin;
  sAim = !classic || PortTouch::AimEnabled();
  // The stock wheels are the default layout's beam and visor control; classic and twin stick have
  // the D-pad (twin: Beam held + D-pad), and either takes wheels as added buttons.
  sWheels = !classic && !sTwin;
  MigrateLegacyWheels(width, height);
  sVisorTapScan = PortTouch::VisorTapScan();
  sMapTap = PortMinimap::Enabled();
  if (!sEditing) {
    // A layout made on another screen, or before a rotation or resize, must not leave a control off
    // screen where it can't be grabbed.
    for (int i = 0; i < kAllControls; ++i) {
      if (!Removed(i) && LayoutHas(i) &&
          (sOvDx[i] != 0.f || sOvDy[i] != 0.f || sOvScale[i] != 1.f)) {
        ClampToScreen(i, width, height);
      }
    }
  }
  // The eye's bounds, and the touch that hides the controls, come from here.
  BottomButtonRect(0, true, width, height, sHideBounds);
  if (sAnyLeft && sLeftFloated) {
    const float r = LeftStickRadius(height);
    ControlLayer layer(kCLStick);
    if (!layer.skip) {
      DrawStick(dl, sFloatX, sFloatY, r, sLeftPointer, sAnyLeft, 0);
    }
  } else if (!FloatingStick()) {
    ControlLayer layer(kCLStick);
    if (!layer.skip) {
      DrawStick(dl, LeftStickX(width, height), LeftStickY(width, height), LeftStickRadius(height),
                sLeftPointer, sAnyLeft, 0);
    }
  }
  // The right stick is the C-stick, yellow on the GameCube pad, in the classic layout; in twin
  // stick it aims, plain like the left.
  if (sCStick) {
    ControlLayer layer(sTwin ? kCRstick : kCCStick);
    if (!layer.skip) {
      DrawStick(dl, RightStickX(width, height), RightStickY(height), RightStickRadius(height),
                sRightPointer, sAnyRight, sColored && !sTwin ? kGcYellow : 0);
    }
  }

  sRHiddenNow = !sEditing && RHidden();
  sLabelContextNow = sLabels ? LabelContext() : kLabelsPlay;
  for (int i = 0; i < sPillCount; ++i) {
    const PillButton& pill = sPills[i];
    if (!PillShown(pill)) {
      continue;
    }
    ControlLayer layer(PillControl(pill));
    if (layer.skip) {
      continue;
    }
    DrawPillButton(dl, pill, width, height);
  }
  for (int i = 0; i < 4; ++i) {
    DrawControlButton(dl, *sFace[i], width, height);
  }
  for (int i = 0; i < kBallButtons; ++i) {
    DrawControlButton(dl, sBallFace[i], width, height);
  }
  if (sTurboButton != nullptr) {
    DrawControlButton(dl, *sTurboButton, width, height);
  }
  if (sWheels) {
    DrawWheelButtons(dl, width, height);
  } else {
    ControlLayer layer(kCDpad);
    if (!layer.skip) {
      DrawDpad(dl, width, height);
    }
  }
  {
    ControlLayer layer(kCEye);
    if (!layer.skip) {
      DrawEye(dl, sHideBounds);
    }
  }
  sMapButtonShown = MapButtonState(width, height);
  if (sMapButtonShown) {
    ControlLayer layer(kCMap);
    if (!layer.skip) {
      DrawMapButton(dl);
    }
  }
  DrawCustomButtons(dl, width, height);
  if (sWheelOpen) {
    DrawWheel(dl);
  }
  if (sEditing) {
    DrawEditOverlay(dl, width, height);
  }
}

// The controls-off view: only START and MENU, fully opaque and at their default spots, so MENU is
// always reachable.
void DrawMenuPills(ImDrawList* dl, float width, float height) {
  sColored = false;
  sLabels = false;
  sPills = kGamecubePills;
  sPillCount = int(std::size(kGamecubePills));
  sWheels = false;
  sMapTap = false;
  sMapButtonShown = false;
  for (int i = 0; i < sPillCount; ++i) {
    if (IsMenuPill(sPills[i])) {
      DrawPillButton(dl, sPills[i], width, height);
    }
  }
}

// --- the view's own size and settings ---------------------------------------

bool UpdateView() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  if (vp == nullptr) {
    return false;
  }
  const float w = vp->Size.x, h = vp->Size.y;
  SDL_Window* window = PortDebug::MainWindow();
  int pointsW = 0, pointsH = 0;
  if (window == nullptr || !SDL_GetWindowSize(window, &pointsW, &pointsH) || pointsW <= 0 ||
      w <= 0.f || h <= 0.f) {
    sW = sH = 0.f;
    return false;
  }
  if (sW > 0.f && (w != sW || h != sH)) {
    PortRadial::CancelAllTouches();
  }
  sW = w;
  sH = h;
  sDensity = w / float(pointsW); // one dp is one point
  // The notch, rounded corners and the home indicator.
  SDL_Rect safe{0, 0, pointsW, pointsH};
  SDL_GetWindowSafeArea(window, &safe);
  sInsetSide = float(std::max(safe.x, pointsW - safe.x - safe.w)) * sDensity;
  sBottomInset = float(std::max(0, pointsH - safe.y - safe.h)) * sDensity;
  return true;
}

// The settings the Java re-reads every draw.
void ReadSettings() {
  sColored = PortTouch::Colors();
  sLabels = PortTouch::Labels();
  sFloating = PortTouch::FloatingStick();
  sSideMargin = Dp(PortTouch::SideMarginDp()) + sInsetSide;
  sStickInset = Dp(PortTouch::StickInsetDp());
  sButtonInset = Dp(PortTouch::ButtonInsetDp());
}

// Whether the game is paused under the F1 overlay, photo mode or the resume prompt: the controls
// get out of the way and the touches are for those.
bool OverlayVisibleNow() {
  PortSaveState::ResumeInfo info;
  bool yes = true;
  return PortTouch::OverlayVisible() || PortPhoto::Active() ||
         PortSaveState::ResumePromptShown(info, yes);
}

// The Java's physicalInputPoll: a real pad or keys hide the controls until the next touch, and
// Turbo is let go when F1 turns it off.
void PollPhysicalInput() {
  if (PortTouch::TakePhysicalInput() && !sHidden && !sAutoHidden &&
      !sEditing && PortTouch::ControlsEnabled()) {
    sAutoHidden = true;
    ReleaseAll();
  }
  // F1 can switch Turbo off while the overlay hides this view's draws.
  if (TurboTouched() && !PortTouch::Turbo()) {
    ReleaseTurbo();
  }
  // F1's "Edit layout" closes the overlay itself and leaves the request; the editor opens once the
  // overlay has gone (the toggle lands a frame later).
  if (PortTouch::TakeEditRequested()) {
    sEditPending = true;
  }
  const bool overlayVisible = OverlayVisibleNow();
  if (sEditPending && !overlayVisible) {
    sEditPending = false;
    BeginEdit();
  } else if (sEditing && overlayVisible) {
    // The overlay was opened some other way (a pad's chord): keep what was done.
    EndEdit(true);
  }
}

// --- touch handling ---------------------------------------------------------

// A finger lifts, `act` false dropping a wheel or minimap tap instead of acting on it.
void ReleasePointer(SDL_FingerID id, bool act) {
  const auto it = sTargets.find(id);
  if (it == sTargets.end()) {
    return;
  }
  TouchTarget target = it->second;
  sTargets.erase(it);
  if (target.radialToken != 0) {
    if (target.radialToken > 0) {
      const float reach = Dp(kRadialDragDp);
      if (act) {
        PortRadial::EndTouch(uint64_t(target.radialToken), target.radialOwner,
                             (target.x - target.startX) / reach, (target.startY - target.y) / reach);
      } else {
        PortRadial::CancelTouch(uint64_t(target.radialToken), target.radialOwner);
      }
    }
    if (target.type == kWheel) {
      sWheelOpen = false;
    }
    return; // never also finish a wheel / Scan-tap / release a synthetic fire binding
  }
  if (target.type == kHide) {
    SetHidden(true);
    return;
  }
  if (target.type == kWheel) {
    sWheelOpen = false;
    if (act) {
      FinishWheel(target);
    }
    return;
  }
  if (target.type == kMapTap) {
    // A tap, not a drag that began on the minimap.
    const float slop = Dp(kMapTapSlopDp);
    if (act && std::hypot(target.x - target.startX, target.y - target.startY) < slop) {
      if (PortMinimap::Enabled()) {
        PortMinimap::RequestTap();
      }
    }
    return;
  }
  ReleaseTarget(target);
  if (sAnyLeft && id == sLeftPointer) {
    sAnyLeft = false;
  }
  if (sAnyRight && id == sRightPointer) {
    sAnyRight = false;
  }
  if (sAnyAim && id == sAimPointer) {
    sAnyAim = false;
  }
  if (sAnyPan2 && id == sPan2Pointer) {
    sAnyPan2 = false;
  } else if (sAnyPan && id == sPanPointer) {
    if (sAnyPan2) {
      // The other finger carries on panning from where it is.
      const auto second = sTargets.find(sPan2Pointer);
      if (second != sTargets.end()) {
        const float x = second->second.x, y = second->second.y;
        second->second = TouchTarget();
        second->second.type = kMapPan;
        second->second.x = second->second.startX = x;
        second->second.y = second->second.startY = y;
      }
      sPanPointer = sPan2Pointer;
      sAnyPan2 = false;
    } else {
      sAnyPan = false;
    }
  }
}

// A finger lands. Returns true when the overlay takes it; false hands it to the game.
bool TouchDown(SDL_FingerID id, float x, float y) {
  if (sEditing) {
    return EditTouchDown(id, x, y); // the editor takes every touch
  }
  SyncControlsOn();
  const bool overlayVisible = OverlayVisibleNow();
  // Anything still held has to go first: once this view stops claiming touches the matching
  // releases never arrive, which left whatever was down held for the rest of the session.
  if (overlayVisible && (!sTargets.empty() || !sHeld.empty())) {
    ReleaseAll();
  }
  // In a pointer menu every finger but one on the back or cog button is the game's.
  const bool menu = !overlayVisible && PortPointer::ScreenActive();
  const bool toGame =
      overlayVisible || (menu && !OnBackButton(x, y) && !OnCogButton(x, y));
  if (toGame) {
    sForwarded.insert(id);
    return false;
  }
  // A tap on the "newer release" toast opens its page and is not also a press.
  if (false) {
    return true;
  }
  // With the controls off nothing is hidden and no touch brings anything back.
  if (sAutoHidden && sControlsOn) {
    // The touch that brings them back is not also a press.
    sAutoHidden = false;
    PortTouch::TakePhysicalInput();
    SetTouchDevice(sTwin);
    return true;
  }
  if (sHidden && sControlsOn) {
    // Any touch brings the controls back, and is not also a press.
    SetHidden(false);
    return true;
  }
  // The in-game prompts follow the input the player reached for.
  SetTouchDevice(sTwin);
  if (menu) {
    // Only the back and cog buttons get here (toGame took the rest).
    const int target = OnCogButton(x, y) ? kToggleDebugOverlay : kMenuBack;
    TouchTarget t;
    t.type = kButton;
    t.id = target;
    sTargets[id] = t;
    PressControl(target);
    return true;
  }
  if (sControlsOn && !CtrlHidden(kCEye) && sHideBounds.Contains(x, y)) {
    TouchTarget t;
    t.type = kHide;
    sTargets[id] = t;
    return true;
  }
  if (sControlsOn && sMapButtonShown && !CtrlHidden(kCMap) && sMapButtonRect.Contains(x, y)) {
    TouchTarget t;
    t.type = kMapTap;
    t.x = t.startX = x;
    t.y = t.startY = y;
    sTargets[id] = t;
    return true;
  }
  AssignPointer(id, x, y);
  return true;
}

// A finger moves. Returns true when the overlay takes the event (a finger it does not own was
// already declined to SDL, and stays so).
bool TouchMove(SDL_FingerID id, float x, float y) {
  if (sEditing) {
    return EditTouchMove(id, x, y);
  }
  if (sForwarded.count(id) != 0) {
    return false; // handed to SDL: it stays the game's
  }
  const auto it = sTargets.find(id);
  if (it == sTargets.end()) {
    return true; // a finger that only brought the controls back, or landed with them off
  }
  TouchTarget& target = it->second;
  if (target.radialToken != 0) {
    RadialMove(target, x, y);
    return false;
  }
  const float width = sW, height = sH;
  if (target.type == kLeftStick || target.type == kRightStick) {
    UpdateStick(target, x, y, width, height);
  } else if (target.type == kAim || target.aiming) {
    UpdateAim(target, x, y);
  } else if (target.type == kButton && AimsWhileHeld(target) && sAim && !sAnyAim) {
    StartButtonAim(target, id, x, y);
  } else if (target.type == kMapPan || target.type == kMapPan2) {
    if (sAnyPan && sAnyPan2) {
      UpdatePinch(id, target, x, y);
    } else {
      UpdateMapPan(target, x, y);
    }
  } else if (target.type == kWheel || target.type == kMapTap) {
    target.x = x;
    target.y = y;
  }
  return false;
}

// A finger lifts. Returns true when the overlay takes the event (the Java consumes every finger it
// did not hand to SDL).
bool TouchUp(SDL_FingerID id) {
  if (sForwarded.erase(id) != 0) {
    return false;
  }
  if (sEditing) {
    EditTouchUp(id);
    return true;
  }
  ReleasePointer(id, true);
  return true;
}

// The gesture was cancelled: what it held is let go without acting on it.
bool TouchCancel(SDL_FingerID id) {
  if (sForwarded.erase(id) != 0) {
    return false;
  }
  if (sEditing) {
    EditTouchCancel();
    return true;
  }
  ReleasePointer(id, false);
  if (sTargets.empty()) {
    ReleaseAll(); // nothing is still down: let go of whatever a cancelled gesture left held
  }
  return true;
}

bool SDLCALL TouchFilter(void*, SDL_Event* event) {
  switch (event->type) {
  case SDL_EVENT_WINDOW_FOCUS_LOST:
  case SDL_EVENT_WILL_ENTER_BACKGROUND: {
    // A real focus or background interruption cancels every source's radial capture too.
    std::lock_guard lock(sMutex);
    PortRadial::CancelTouchLifecycle();
    ReleaseAll();
    return true;
  }
  case SDL_EVENT_MOUSE_MOTION:
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP: {
    // Touches also arrive as a mouse; only the F1 overlay, photo mode and the resume prompt want
    // those, and the pointer menu's own buttons.
    const SDL_MouseID which =
        event->type == SDL_EVENT_MOUSE_MOTION ? event->motion.which : event->button.which;
    std::lock_guard lock(sMutex);
    if (which != SDL_TOUCH_MOUSEID) {
      // The editor takes a real mouse too (an iPad trackpad), its left button as a finger, as the
      // Java view takes every event while editing. The picker is ImGui's, so it keeps the mouse.
      if (!sEditing || sPicker.open || sW <= 0.f) {
        return true;
      }
      const bool motion = event->type == SDL_EVENT_MOUSE_MOTION;
      if (!motion && event->button.button != SDL_BUTTON_LEFT) {
        return false;
      }
      SDL_Window* window =
          SDL_GetWindowFromID(motion ? event->motion.windowID : event->button.windowID);
      const float density = window != nullptr ? SDL_GetWindowPixelDensity(window) : 1.f;
      const float x = (motion ? event->motion.x : event->button.x) * density;
      const float y = (motion ? event->motion.y : event->button.y) * density;
      constexpr SDL_FingerID kMouseFinger = ~SDL_FingerID(0);
      if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        EditTouchDown(kMouseFinger, x, y);
      } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        EditTouchUp(kMouseFinger);
      } else if ((event->motion.state & SDL_BUTTON_LMASK) != 0) {
        EditTouchMove(kMouseFinger, x, y);
      }
      return false;
    }
    // SDL's synthesized touch-mouse is how a thumb reaches ImGui (Aurora's backend reads mouse
    // events only). The editor and its picker are ImGui windows or ImGui-drawn, so they get it;
    // the fingers themselves stay the editor's.
    return OverlayVisibleNow() || sEditing;
  }
  case SDL_EVENT_FINGER_DOWN:
  case SDL_EVENT_FINGER_MOTION:
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
    break;
  default:
    return true;
  }
  std::lock_guard lock(sMutex);
  if (sW <= 0.f) {
    return true; // no frame drawn yet: nothing to hit test against
  }
  const SDL_FingerID id = event->tfinger.fingerID;
  const float x = event->tfinger.x * sW, y = event->tfinger.y * sH;
  bool taken = false;
  switch (event->type) {
  case SDL_EVENT_FINGER_DOWN:
    taken = TouchDown(id, x, y);
    break;
  case SDL_EVENT_FINGER_MOTION:
    taken = TouchMove(id, x, y);
    break;
  case SDL_EVENT_FINGER_UP:
    taken = TouchUp(id);
    break;
  default:
    taken = TouchCancel(id);
    break;
  }
  return !taken;
}

} // namespace

namespace PortDebug {

void InstallTouchOverlay() {
  std::lock_guard lock(sMutex);
  if (sInstalled) {
    return;
  }
  sInstalled = true;
  InitDefaults();
  SDL_SetEventFilter(TouchFilter, nullptr);
}

void DrawTouchOverlay() {
  std::lock_guard lock(sMutex);
  if (!UpdateView()) {
    return;
  }
  if (!sEditing) {
    SyncLayout();
  }
  PollPhysicalInput();
  SyncControlsOn();
  // The debug overlay is drawn into the surface below; stay out of its way. Not while editing:
  // the editor has to keep drawing over whatever opened.
  if (OverlayVisibleNow() && !sEditing) {
    return;
  }
  if ((sAutoHidden || sHidden) && sControlsOn && !sEditing) {
    return;
  }
  // The picker is an ImGui window; it goes over the controls, so they are drawn under it.
  DrawActionPicker();
  ReadSettings();
  // A finger held on the open map must keep telling the game it is still there, or the map drifts
  // back (the Java view posts panKeepAlive for this).
  if (sAnyPan) {
    NoMap(0.f, 0.f, PanViewDp());
  }
  const float width = sW, height = sH;
  // The classic setting can change mid-session, so re-check it every draw.
  const bool classic = PortTouch::Classic();
  ImDrawList* dl = sPicker.open ? ImGui::GetBackgroundDrawList(ImGui::GetMainViewport())
                                : ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
  sPointerMenuNow = !sEditing && PortPointer::ScreenActive();
  if (sPointerMenuNow) {
    // Then only the back button and the cog beside it show, fully opaque, so the overlay opens
    // from any menu.
    sGlobal = 1.f;
    sLocal = 1.f;
    BottomButtonRect(0, false, width, height, sBackButtonRect);
    DrawBackButton(dl);
    BottomButtonRect(1, false, width, height, sCogButtonRect);
    const bool cogDown = sHeld.find(kToggleDebugOverlay) != sHeld.end();
    DrawCornerButton(dl, 1, sCogButtonRect, Argb(cogDown ? 0xCC48C8E8 : 0x99081218),
                     Argb(cogDown ? 0xFFE1F8FF : 0xCCFFFFFF));
    return;
  }
  if (!sControlsOn && !sEditing) {
    sGlobal = 1.f;
    sLocal = 1.f;
    DrawMenuPills(dl, width, height);
    return;
  }
  // F1's opacity for the on-screen controls; the pointer menu's buttons were drawn above it.
  sGlobal = sEditing ? 1.f : std::clamp(PortTouch::Opacity() / 100.f, 0.f, 1.f);
  sLocal = 1.f;
  DrawControls(dl, width, height, classic);
}

bool TouchEditing() {
  std::lock_guard lock(sMutex);
  return sEditing;
}

} // namespace PortDebug

#else

namespace PortDebug {
bool TouchEditing() { return false; }
} // namespace PortDebug

#endif // PORT_TOUCH_OVERLAY
