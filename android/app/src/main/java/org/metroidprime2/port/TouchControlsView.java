package org.metroidprime2.port;

import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;

import org.libsdl.app.SDLActivity;

import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * On-screen controller drawn over the game surface.
 *
 * It feeds an SDL virtual gamepad rather than synthesising keyboard keys, so the
 * game's own controller mapping, prompts and rebinding treat it as a real pad,
 * and its sticks are sticks rather than four keys pretending to be one.
 *
 * Three layouts (the F1 "Layout" setting). Default: the GameCube pad's, with no
 * C-stick; a drag on the free screen area aims like a mouse. Classic GameCube
 * brings back the C-stick and the D-pad. Twin stick (Remastered) has a right
 * stick that aims directly and Remastered's Dual Sticks button labels. The game
 * assigns the actions.
 *
 * Metroid Prime 2: this is Metroid Prime 1's view (MetroidPrimePort), copied. What MP2 changes:
 * the wheels name MP2's visors and beams; the layout and the touch settings live in
 * <user>/touch.cfg, not in the F1 window, so the editor opens from the Edit button beside the
 * eye and switches layouts from its own "Layout" button; and while the F1 window is open only
 * MENU is drawn and every other touch goes to SDL (two fingers scroll it). The map gestures,
 * pointer menus, update toast and touch: bindings have no MP2 backend: their natives answer
 * "not there" (no minimap rect, no map screen, no pointer menu, nothing bound).
 */
final class TouchControlsView extends View {
    // Touch target types.
    private static final int LEFT_STICK = 1;
    private static final int RIGHT_STICK = 2;
    private static final int BUTTON = 3;
    private static final int HIDE = 4;
    // A drag on the free screen area that turns the view by its travel.
    private static final int AIM = 5;
    // A tap on the HUD minimap, which opens the map.
    private static final int MAP_TAP = 6;
    // A one-finger drag on the open map screen pans it.
    private static final int MAP_PAN = 7;
    // A second finger on the map: with the MAP_PAN one it pinches to zoom.
    private static final int MAP_PAN2 = 8;
    // Hold-and-slide wheels that replace the D-pad: id 0 = Visor, 1 = Beam.
    private static final int WHEEL = 9;
    // MP2: the Edit button beside the eye, which opens the layout editor.
    private static final int EDIT = 10;

    // Port-only actions, not game inputs.
    private static final int TOGGLE_DEBUG_OVERLAY = -1;
    // Twin stick's Beam button: held, it turns the D-pad into the beam picker (the
    // port's pad preset does the same with Y). It sends no GameCube button.
    private static final int BEAM_SHIFT = -2;
    // The Turbo button: held, the game behaves as if Fire were mashed. Like
    // BEAM_SHIFT it sends no GameCube button itself.
    private static final int TURBO_FIRE = -3;
    // A button added in the layout editor: its action is a touch: binding the
    // native side makes from the layout, so it sends no GameCube button itself.
    private static final int CUSTOM_BUTTON = -4;
    // Axis-held controls are tracked with ids above this, to share one press map.
    private static final int AXIS_ID_BASE = 100;

    // SDL gamepad ids. The port maps these onto the GameCube pad itself: SOUTH is
    // A, EAST is B, WEST is X, NORTH is Y, Start is Start, the right shoulder is
    // Z, the D-pad is the D-pad, the left stick is the main stick, the right
    // stick is the C-stick and the triggers are L and R.
    private static final int BTN_SOUTH = 0;
    private static final int BTN_EAST = 1;
    private static final int BTN_WEST = 2;
    private static final int BTN_NORTH = 3;
    private static final int BTN_START = 6;
    private static final int BTN_RIGHT_SHOULDER = 10;
    private static final int BTN_DPAD_UP = 11;
    private static final int BTN_DPAD_DOWN = 12;
    private static final int BTN_DPAD_LEFT = 13;
    private static final int BTN_DPAD_RIGHT = 14;

    private static final int AXIS_LEFTX = 0;
    private static final int AXIS_LEFTY = 1;
    private static final int AXIS_RIGHTX = 2;
    private static final int AXIS_RIGHTY = 3;
    private static final int AXIS_TRIGGER_L = 4;
    private static final int AXIS_TRIGGER_R = 5;

    // The layout's fractions of the view height stop growing at this many dp
    // (a 500 dp phone is unchanged), so a tablet gets phone-sized controls
    // that keep their distance to the nearest edge.
    private static final float MAX_LAYOUT_DP = 520f;
    // Sticks are drawn and read through these, so the two cannot disagree.
    private static final float STICK_RADIUS = 0.16f;
    private static final float LEFT_STICK_REACH = 1.5f; // grab area, in stick radii
    private static final float STICK_DEAD_ZONE = 0.12f;
    // The GameCube C-stick is smaller than the main stick and sits just below
    // and left of the face buttons: its centre is this far from the right edge,
    // in view heights, like the face buttons' anchor.
    private static final float GC_CSTICK_FROM_RIGHT = 0.60f;
    private static final float GC_CSTICK_Y = 0.76f;
    private static final float GC_CSTICK_RADIUS = 0.115f;

    // The GameCube pad's colours, as RGB; the fill alpha follows the press state.
    private static final int GC_GREEN = 0x2FA864;
    private static final int GC_RED = 0xC8343A;
    private static final int GC_GREY = 0x8A8A94;
    private static final int GC_YELLOW = 0xE0C020;
    private static final int GC_PURPLE = 0x6A4FB0;

    // A's centre: from the right edge and from the top, in view heights. The
    // GameCube cluster is laid out in heights around it, so it keeps the pad's
    // shape whatever the screen's aspect.
    private static final float GC_A_FROM_RIGHT = 0.235f;
    private static final float GC_A_Y = 0.700f;

    // The GameCube pad's face, so the overlay
    // matches the pad the game was authored for: a big green A, a small red B
    // at its lower left, and X and Y as kidneys curving round A's right and top.
    private static final ControlButton[] GAMECUBE_FACE = {
        ControlButton.round("A", BTN_SOUTH, 0f, 0f, 0.085f, GC_GREEN),
        ControlButton.round("B", BTN_EAST, -0.123f, 0.103f, 0.050f, GC_RED),
        ControlButton.kidney("X", BTN_WEST, 0.158f, 0.040f, -37f, 55f, GC_GREY),
        ControlButton.kidney("Y", BTN_NORTH, 0.158f, 0.040f, -150f, 55f, GC_GREY),
    };
    // What each GameCube face button does, drawn small under its letter.
    private static final String[] GAMECUBE_FUNCTIONS = {"Fire", "Jump", "Morph", "Missile"};
    // In the pause menu (Y turns the inventory's suit view), on the map screen
    // (A switches area and world map, Y shows the legend) and in save station or
    // message boxes; blank where the button does nothing there.
    private static final String[] GAMECUBE_PAUSE_FUNCTIONS = {"Select", "Back", "", "View"};
    private static final String[] GAMECUBE_MAP_FUNCTIONS = {"World", "Exit", "", "Legend"};
    private static final String[] GAMECUBE_DIALOG_FUNCTIONS = {"OK", "", "", ""};
    // The optional Turbo button (F1 "Turbo fire button"), above the cluster on the
    // right. Not in the face arrays: those index the editor ids and are always drawn.
    private static final ControlButton GAMECUBE_TURBO =
        ControlButton.round("Turbo", TURBO_FIRE, 0.05f, -0.27f, 0.045f, GC_GREEN);

    // The D-pad is one cross, as on the GameCube pad: its centre as fractions of
    // the view, its arms in view heights so it stays square.
    private static final float DPAD_X = 0.115f;
    private static final float DPAD_Y = 0.335f;
    private static final float DPAD_ARM = 0.130f;  // centre to an arm's end
    private static final float DPAD_HALF = 0.045f; // an arm's half width
    // Up, down, left, right: ids, labels, and each arm's direction.
    private static final int[] DPAD_BUTTONS = {
        BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
    };
    private static final String[] DPAD_LABELS = {"\u25B2", "\u25BC", "\u25C0", "\u25B6"};
    private static final int[] DPAD_DX = {0, 0, -1, 1};
    private static final int[] DPAD_DY = {-1, 1, 0, 0};

    // L and R are the pad's analog triggers; Z is its digital shoulder. Z sits in
    // front of R on the GameCube pad, so Z goes under R here. L and R are tall so
    // a quick lock-on is hard to miss; L stops just above the D-pad. START and
    // MENU ignore their rects: see cornerSlot.
    // Twin stick (Remastered): a diamond in Xbox positions, round A's spot, with
    // Remastered's Dual Sticks functions on the GameCube buttons they send: Jump
    // is B, Fire is A, Morph is X, and Y held shifts the D-pad to beams (Missile is
    // the RB pill). Bottom, right, left, top, so
    // the controls are C_JUMP + index. Coloured, each takes the colour of the
    // GameCube button it sends.
    private static final float TWIN_DIAMOND = 0.095f;
    private static final ControlButton[] TWIN_FACE = {
        ControlButton.round("Jump", BTN_EAST, 0f, TWIN_DIAMOND, 0.055f, GC_RED),
        ControlButton.round("Fire", BTN_SOUTH, TWIN_DIAMOND, 0f, 0.055f, GC_GREEN),
        ControlButton.round("Morph", BTN_WEST, -TWIN_DIAMOND, 0f, 0.055f, GC_GREY),
        ControlButton.round("Beam", BEAM_SHIFT, 0f, -TWIN_DIAMOND, 0.055f, GC_GREY),
    };
    // Turbo, above Beam.
    // The menus' TWIN_FACE functions, as GAMECUBE_PAUSE_FUNCTIONS and the rest.
    private static final String[] TWIN_PAUSE_FUNCTIONS = {"Back", "Select", "", ""};
    private static final String[] TWIN_MAP_FUNCTIONS = {"Exit", "World", "", ""};
    private static final String[] TWIN_DIALOG_FUNCTIONS = {"", "OK", "", ""};
    private static final ControlButton TWIN_TURBO =
        ControlButton.round("Turbo", TURBO_FIRE, 0f, -0.21f, 0.045f, GC_GREEN);

    // The Morph Ball page's buttons, C_BALL + index, where the face buttons that
    // do the same in the ball sit: Bomb on A (Fire in twin), Boost on B (Jump),
    // Unmorph on X (Morph), P.Bomb on Y and Spider on R (twin: above P.Bomb). No
    // button id: they only report their touch code.
    private static final ControlButton[] BALL_FACE = {
        ControlButton.round("Bomb", -1, 0f, 0f, 0.085f, GC_GREEN),
        ControlButton.round("Boost", -1, -0.123f, 0.103f, 0.050f, GC_RED),
        ControlButton.round("P.Bomb", -1, -0.085f, -0.133f, 0.055f, GC_GREY),
        ControlButton.round("Spider", -1, 0.05f, -0.27f, 0.050f, GC_GREY),
        ControlButton.round("Unmorph", -1, 0.156f, -0.026f, 0.055f, GC_GREY),
    };
    private static final ControlButton[] TWIN_BALL_FACE = {
        ControlButton.round("Bomb", -1, TWIN_DIAMOND, 0f, 0.055f, GC_GREEN),
        ControlButton.round("Boost", -1, 0f, TWIN_DIAMOND, 0.055f, GC_RED),
        ControlButton.round("P.Bomb", -1, 0f, -TWIN_DIAMOND, 0.055f, GC_GREY),
        ControlButton.round("Spider", -1, 0f, -0.21f, 0.055f, GC_GREY),
        ControlButton.round("Unmorph", -1, -TWIN_DIAMOND, 0f, 0.055f, GC_GREY),
    };

    private static final PillButton[] GAMECUBE_PILLS = {
        new PillButton("L Lock", AXIS_TRIGGER_L, -1, 0.020f, 0.030f, 0.150f, 0.190f, GC_GREY),
        new PillButton("R Look", AXIS_TRIGGER_R, -1, 0.850f, 0.030f, 0.980f, 0.190f, GC_GREY),
        new PillButton("Z Map", -1, BTN_RIGHT_SHOULDER, 0.870f, 0.205f, 0.960f, 0.265f, GC_PURPLE),
        new PillButton("START", -1, BTN_START, 0f, 0f, 0f, 0f),
        new PillButton("MENU", -1, TOGGLE_DEBUG_OVERLAY, 0f, 0f, 0f, 0f),
    };

    // Twin stick's shoulders, like Remastered's: LT locks on (the L trigger), RT
    // fires, LB jumps and RB fires missiles. Z (the map) sits inboard of the
    // right pair; R (free look) has no use with a right stick that aims. START and MENU as in the GameCube layout.
    // The Xbox letter drawn big on each diamond button, matching the in-game hint.
    private static final String[] TWIN_LETTERS = {"A", "B", "X", "Y"};

    private static final PillButton[] TWIN_PILLS = {
        new PillButton("LT Lock", AXIS_TRIGGER_L, -1, 0.020f, 0.030f, 0.150f, 0.100f, GC_GREY,
                       TouchControlsView.C_LT),
        new PillButton("LB Jump", -1, BTN_EAST, 0.020f, 0.115f, 0.150f, 0.185f,
                       GC_RED, TouchControlsView.C_LB),
        new PillButton("RT Fire", -1, BTN_SOUTH, 0.850f, 0.030f, 0.980f, 0.100f,
                       GC_GREEN, TouchControlsView.C_RT),
        new PillButton("RB Missile", -1, BTN_NORTH, 0.850f, 0.115f, 0.980f, 0.185f, GC_GREY,
                       TouchControlsView.C_RB),
        new PillButton("Map", -1, BTN_RIGHT_SHOULDER, 0.730f, 0.030f, 0.830f, 0.100f, GC_PURPLE,
                       TouchControlsView.C_TZ),
        new PillButton("START", -1, BTN_START, 0f, 0f, 0f, 0f),
        new PillButton("MENU", -1, TOGGLE_DEBUG_OVERLAY, 0f, 0f, 0f, 0f),
    };

    // The round buttons along the bottom: START and MENU on the left, the map
    // and the eye on the right. Inset from the sides and bottom so rounded
    // display corners don't cut them off.
    private static final float BOTTOM_BUTTON_RADIUS_DP = 22f;
    private static final float BOTTOM_BUTTON_SIDE_DP = 28f;
    private static final float BOTTOM_BUTTON_BOTTOM_DP = 12f;
    private static final float BOTTOM_BUTTON_GAP_DP = 8f;

    // The Visor and Beam buttons and their wheels.
    private static final String[] WHEEL_BUTTON_LABELS = {"Visor", "Beam"};
    // Sectors run up, right, down, left, placed as the HUD's own menus place them
    // (CHudVisorBeamMenu::UpdateMenuWidgetTransform). Items are numbered as the native
    // side does: visors Combat/Echo/Scan/Dark, beams Power/Dark/Light/Annihilator (MP2's).
    private static final String[][] WHEEL_LABELS = {
        {"Combat", "Dark", "Echo", "Scan"},
        {"Power", "Dark", "Annihil.", "Light"},
    };
    private static final int[][] WHEEL_ITEMS = {{0, 3, 1, 2}, {0, 1, 3, 2}};
    // Each beam icon's colour: Power yellow, Dark purple, Light white, Annihilator grey.
    // Visor icons stay white.
    private static final int[] BEAM_ICON_COLORS = {0xFFFFFF00, 0xFF8033FF, 0xFFFFFFFF, 0xFFB0B0C0};
    // The bit of nativeWheelOwned() that is set once there is a player.
    private static final int WHEEL_VALID_BIT = 1 << 12;
    // Set while Samus is morphed or morphing.
    private static final int WHEEL_MORPHED_BIT = 1 << 13;
    private static final float WHEEL_BUTTON_RADIUS = 0.072f;
    private static final float WHEEL_BUTTON_GAP_DP = 8f; // wheel to edge / other button
    private static final float WHEEL_BUTTON_ANCHOR_DP = 16f; // gap to the stick / face buttons
    // The open wheel: its outer radius, the dead centre that cancels, and a sector's icon.
    private static final float WHEEL_RADIUS_DP = 112f;
    private static final float WHEEL_DEAD_DP = 30f;
    private static final float WHEEL_ICON_DP = 44f;

    // A swipe up that springs the morph ball: at least this far, in dp, within
    // this many ms of the stroke's start (about 270 dp/s or faster).
    private static final float SWIPE_UP_DP = 40f;
    private static final long SWIPE_UP_MS = 150;

    // Finger spread, in dp, under which a pinch is ignored (the ratio blows up).
    private static final float MAP_PINCH_MIN_DP = 20f;
    // Travel, in dp, past which a touch is a drag and not a tap (minimap, wheel, A).
    private static final float MAP_TAP_SLOP_DP = 12f;

    // Tells the game a finger is still down on the map, so it doesn't drift back.
    private static final long MAP_PAN_KEEPALIVE_MS = 100;
    private static final long MAP_BUTTON_POLL_MS = 200;
    private static final long PHYSICAL_INPUT_POLL_MS = 250;
    private static final long WHEEL_ICON_RETRY_MS = 1000;
    private static final long WHEEL_TAP_MS = 250;

    // Every control is one movable unit. Its override is an offset (in layoutU
    // units, so it follows the screen's size) and a size scale, both applied on
    // top of the default position, which still follows the F1 margin settings.
    private static final int C_LSTICK = 0;
    private static final int C_CSTICK = 1;
    private static final int C_DPAD = 2;
    private static final int C_A = 3; // A, B, X, Y follow in GAMECUBE_FACE's order
    private static final int C_B = 4;
    private static final int C_X = 5;
    private static final int C_Y = 6;
    private static final int C_L = 7;
    private static final int C_R = 8;
    private static final int C_Z = 9;
    private static final int C_VISOR = 10; // the beam button follows
    private static final int C_BEAM = 11;
    private static final int C_START = 12;
    private static final int C_MENU = 13;
    private static final int C_MAP = 14;
    private static final int C_EYE = 15;
    // Twin stick's own controls, so a layout made there doesn't move the
    // default or classic layout's buttons. The four face buttons are bottom,
    // right, left, top (TWIN_FACE's order).
    private static final int C_RSTICK = 16;
    private static final int C_JUMP = 17;
    private static final int C_FIRE = 18;
    private static final int C_MORPH = 19;
    private static final int C_MISSILE = 20;
    private static final int C_LT = 21;
    private static final int C_LB = 22;
    private static final int C_RT = 23;
    private static final int C_RB = 24;
    private static final int C_TZ = 25;
    private static final int C_TR = 26;
    // The Turbo button, once for the default/classic layouts and once for twin.
    private static final int C_TURBO = 27;
    private static final int C_TTURBO = 28;
    // The Morph Ball page's buttons (BALL_FACE's order), shown only in the ball,
    // where the gameplay page's own buttons hide (onPage). Each does a Morph Ball
    // row (port_input_bindings.h's kTouchBallControls), on touch code
    // TOUCH_BALL_BASE + its index, and presses no GameCube button of its own.
    private static final int C_BALL = 29;
    private static final int C_BBOMB = 29;
    private static final int C_BBOOST = 30;
    private static final int C_BPBOMB = 31;
    private static final int C_BSPIDER = 32;
    private static final int C_BUNMORPH = 33;
    private static final int BALL_BUTTONS = 5;
    private static final int TOUCH_BALL_BASE = 40;
    // An added button's page (customPage), saved as its entry's 7th field.
    private static final int PAGE_PLAY = 0;
    private static final int PAGE_BALL = 1;
    private static final int PAGE_BOTH = 2;
    private static final String[] PAGE_NAMES = {"Gameplay", "Ball", "Both"};
    // MP2: the Edit button (MP2's F1 window has no "Edit layout"); never hidden or deleted.
    private static final int C_EDIT = 34;
    private static final int CONTROLS = 35;
    // The buttons the player adds in the editor (issue #92), after the stock
    // controls: control C_CUSTOM + slot, saved as k<slot> with its action's key
    // (customAction), and reported as touch code TOUCH_CUSTOM_BASE + slot
    // (port_input_devices.h's kTouchCustomBase), which the native side binds.
    private static final int CUSTOM_SLOTS = 64;
    private static final int C_CUSTOM = CONTROLS;
    private static final int ALL_CONTROLS = CONTROLS + CUSTOM_SLOTS;
    private static final int TOUCH_CUSTOM_BASE = 64;
    // An added button's radius, in layout heights (A's is 0.085).
    private static final float CUSTOM_RADIUS = 0.07f;
    // The ids the saved layout uses; never rename one.
    private static final String[] CONTROL_IDS = controlIds(
        "lstick", "cstick", "dpad", "a", "b", "x", "y", "l", "r", "z", "visor", "beam", "start",
        "menu", "map", "eye", "rstick", "jump", "fire", "morph", "missile", "lt", "lb", "rt", "rb",
        "tz", "tr", "turbo", "tturbo", "bbomb", "bboost", "bpbomb", "bspider", "bunmorph", "edit");
    private static final String[] CONTROL_NAMES = {
        "Left stick", "C-stick", "D-pad", "A", "B", "X", "Y", "L", "R", "Z", "Visor", "Beam",
        "Start", "Menu", "Map", "Hide", "Right stick", "Jump", "Fire", "Morph", "Missile",
        "LT Lock", "LB Jump", "RT Fire", "RB Missile", "Map (Z)", "R",
        "Turbo", "Turbo", "Bomb", "Boost", "P.Bomb", "Spider", "Unmorph", "Edit",
    };
    private static final float MIN_SCALE = 0.5f;
    private static final float MAX_SCALE = 2.5f;
    private static final float SCALE_STEP = 0.1f;
    // A saved offset beyond this many layout heights is junk, not a layout.
    private static final float MAX_OFFSET = 4f;
    // The editor's toolbar tools, indexed by EDIT_*; which ones show depends on
    // the selection (editTools).
    private static final String[] EDIT_LABELS =
        {"−", "+", "Opacity", "Hide", "Reset", "Reset all", "Done", "Add", "Action", "Delete",
         "Ball page", "Page", "Layout"};
    private static final float[] EDIT_WIDTHS_DP =
        {44f, 44f, 112f, 60f, 64f, 88f, 64f, 64f, 72f, 72f, 100f, 100f, 80f};
    private static final float EDIT_BAR_HEIGHT_DP = 44f;
    private static final float EDIT_BAR_GAP_DP = 6f;
    private static final float EDIT_GRAB_DP = 8f; // slack round a control's bounds
    private static final int EDIT_MINUS = 0;
    private static final int EDIT_PLUS = 1;
    private static final int EDIT_OPACITY = 2;
    private static final int EDIT_HIDE = 3;
    private static final int EDIT_RESET = 4;
    private static final int EDIT_RESET_ALL = 5;
    private static final int EDIT_DONE = 6;
    private static final int EDIT_ADD = 7;
    private static final int EDIT_ACTION = 8;
    private static final int EDIT_DELETE = 9;
    // Which page the editor shows (Gameplay or Morph Ball); an added button's page.
    private static final int EDIT_PAGE = 10;
    private static final int EDIT_CUSTOM_PAGE = 11;
    // MP2: steps Default -> Classic -> Twin stick (the F1 "Layout" setting's place).
    private static final int EDIT_LAYOUT = 12;
    // Nothing selected, a stock control, an added button.
    private static final int[] EDIT_TOOLS_NONE =
        {EDIT_ADD, EDIT_PAGE, EDIT_LAYOUT, EDIT_RESET_ALL, EDIT_DONE};
    private static final int[] EDIT_TOOLS_STOCK =
        {EDIT_MINUS, EDIT_PLUS, EDIT_OPACITY, EDIT_HIDE, EDIT_RESET, EDIT_DELETE, EDIT_DONE};
    private static final int[] EDIT_TOOLS_CUSTOM =
        {EDIT_MINUS, EDIT_PLUS, EDIT_OPACITY, EDIT_HIDE, EDIT_ACTION, EDIT_CUSTOM_PAGE,
         EDIT_DELETE, EDIT_DONE};
    // A control's own opacity cycles through these (percent). MENU stops at
    // MENU_MIN_ALPHA: it is the only way into F1 on a touch-only device.
    private static final int[] ALPHA_STEPS = {100, 75, 50, 25, 0};
    private static final int MENU_MIN_ALPHA = 25;
    // The editor draws a control at its own opacity, but hidden and 0% ones at
    // this, so they can still be found (and 25% still looks fainter than 0%'s).
    private static final int EDIT_MIN_ALPHA = 15;
    // pushControl's results besides a saved layer's count.
    private static final int LAYER_NONE = -1;
    private static final int LAYER_SKIP = -2;
    private static final float LAYER_SLACK_DP = 24f; // strokes and labels past the bounds

    private final float[] ovDx = new float[ALL_CONTROLS];
    private final float[] ovDy = new float[ALL_CONTROLS];
    private final float[] ovScale = new float[ALL_CONTROLS];
    // Per control: its own opacity in percent, and whether it is hidden. A hidden
    // control is gone (touches fall through); opacity 0 is invisible but still active.
    private final int[] ovAlpha = new int[ALL_CONTROLS];
    private final boolean[] ovHidden = new boolean[ALL_CONTROLS];
    // A stock control deleted in the editor: gone from play and from the editor
    // too, until Add's list restores it. Saved as hidden = 2, which a version
    // without deleting reads as hidden.
    private final boolean[] ovRemoved = new boolean[CONTROLS];
    // Each added button's action key, or null for a free slot.
    private final String[] customAction = new String[CUSTOM_SLOTS];
    // Each added button's page: PAGE_PLAY, PAGE_BALL or PAGE_BOTH.
    private final int[] customPage = new int[CUSTOM_SLOTS];
    // The Morph Ball page is up: in play while morphed, in the editor by its toolbar.
    private boolean ballPage;
    private ControlButton[] ballFace = BALL_FACE;
    // Why the last editor tool did nothing (Add with every slot used), shown in
    // the hint line until the next touch.
    private String editNotice;
    // The layout string last read from or written to the native config.
    private String layoutText = "";
    // F1's "Edit layout" was pressed; the editor opens once the overlay is gone.
    private boolean editPending;
    private boolean editing;
    private int selected = -1;
    private int dragPointer = -1;
    private int pinchPointer = -1;
    private int editToolPointer = -1;
    private float dragLastX;
    private float dragLastY;
    private float pinchStartDist;
    private float pinchStartScale;
    private final RectF[] editBar = new RectF[EDIT_LABELS.length];
    private final RectF editRect = new RectF();
    private final RectF clampRect = new RectF();
    private final Paint editPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private DashPathEffect editDashes;

    private ControlButton[] face = GAMECUBE_FACE;
    private PillButton[] pills = GAMECUBE_PILLS;
    // The GameCube pad's colours, off by default (plain translucent buttons);
    // an F1 setting, so it's re-read every draw.
    private boolean colored;
    // Each button's function (Fire, Jump...) drawn with its letter.
    private boolean labels = true;
    // F1's "Floating left stick", re-read every draw: the left stick is hidden
    // until a free touch on the left half, then centred at (floatX, floatY), where
    // that finger came down (pulled in so the ring stays on screen).
    private boolean floating;
    private float floatX;
    private float floatY;
    // The held left stick is a floating one (stays so if the map opens meanwhile).
    private boolean leftFloated;
    // F1's "Turbo fire button", re-read every draw; the button when it is on.
    private boolean turbo;
    private ControlButton turboButton;
    // F1 settings too, in px: every control's gap to the side edges, and the
    // left stick's on top of it.
    private float sideMargin;
    private float stickInset;
    private float buttonInset;
    // Classic GameCube layout (F1 setting, re-read every draw): the C-stick is
    // drawn and grabs presses in its zone.
    private boolean cStick;
    // Twin stick (Remastered) layout, also an F1 setting re-read every draw: the
    // right stick is drawn (cStick is on too) but aims, with Jump/Fire/Morph/Missile.
    private boolean twin;
    // A drag on the free area aims (mouse-style, or the classic turn and look
    // up/down). Always on unless the classic layout turns it off.
    private boolean aim;
    // Beam and visor wheels replace the D-pad. F1 settings, re-read every draw.
    private boolean wheels;
    private boolean visorTapScan;
    private int lastWheelMask;
    // The open wheel (the WHEEL target's pointer), else -1.
    private int wheelPointer = -1;
    private float wheelCx;
    private float wheelCy;
    // Tap the minimap to open the map; replaces the GameCube layout's Z pill. An
    // F1 setting, re-read every draw.
    private boolean mapTap;
    // x0, y0, x1, y1 (fractions of the view), then 1 when the minimap is drawn there.
    private final float[] minimapRect = new float[5];
    // Wherever the map can open or close, a map button sits left of the eye (the minimap
    // is not always drawn: the visors other than Combat hide it). The HUD
    // changes without a touch, so a poll redraws when the button comes or goes.
    // The same poll shows and hides the wheel buttons (only drawn while the
    // wheels work).
    private final RectF mapButtonRect = new RectF();
    private boolean mapButtonShown;
    private boolean wheelButtonsShown;
    // rHidden() as of the last draw; touches go by what is drawn.
    private boolean rHiddenNow;
    // nativePointerMenuActive() as of the last draw: then only the back button
    // shows, and every other finger goes to the SDL surface for the game.
    private boolean pointerMenuNow;
    // nativeTouchControlsEnabled() as of the last draw or poll; touches go by it.
    private boolean controlsOnNow = true;
    private final RectF backButtonRect = new RectF();
    private final RectF cogButtonRect = new RectF();
    private final Runnable mapButtonPoll = new Runnable() {
        @Override
        public void run() {
            // The editor shows every control; leaving it redraws and re-arms this.
            if (editing) {
                return;
            }
            if (nativeTouchControlsEnabled() != controlsOnNow ||
                (controlsOnNow &&
                 ((mapTap && mapButtonState(getWidth(), getHeight()) != mapButtonShown) ||
                  (wheels && ((nativeWheelOwned() & WHEEL_VALID_BIT) != 0) != wheelButtonsShown) ||
                  rHidden() != rHiddenNow)) ||
                nativePointerMenuActive() != pointerMenuNow) {
                invalidate();
            } else {
                postDelayed(this, MAP_BUTTON_POLL_MS);
            }
        }
    };

    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Map<Integer, TouchTarget> targets = new HashMap<>();
    private final Map<Integer, Integer> held = new HashMap<>();
    // Fingers on each editor control (C_*), for touch: bindings in controls.toml.
    private final int[] touchHeld = new int[ALL_CONTROLS];
    // Fingers passed on to SDL; see forwardToSdl.
    private final Set<Integer> forwarded = new HashSet<>();
    private final Set<Integer> cancelledRadialPointers = new HashSet<>();
    private final RectF hideBounds = new RectF();
    private final RectF editBounds = new RectF();
    private final RectF layerRect = new RectF();
    private final Path shapePath = new Path();
    private final Path crossArmPath = new Path();
    private static native boolean nativeDebugOverlayVisible();
    private static native boolean nativePhotoTouch();
    private static native boolean nativeTapUpdateToast(float x, float y);
    private static native boolean nativeTouchClassic();
    private static native boolean nativeTouchTwinStick();
    private static native boolean nativeTouchColors();
    private static native boolean nativeTouchLabels();
    private static native boolean nativeTouchFloatingStick();
    private static native boolean nativeTouchTurbo();
    // F1's side margin (every control) and the left stick's extra inset, in dp.
    private static native float nativeTouchSideMarginDp();
    private static native float nativeTouchStickInsetDp();
    // The face buttons' and C-stick's extra inset from the right edge, in dp.
    private static native float nativeTouchButtonInsetDp();
    // F1's opacity for the gameplay controls, in percent (10-100).
    private static native float nativeTouchOpacity();
    // F1's "On-screen controls": false leaves only the MENU and START pills.
    private static native boolean nativeTouchControlsEnabled();
    // The saved per-control layout (`<id>:<dx>,<dy>,<scale>;...`), and its writer, which
    // also saves F1's config.
    private static native String nativeTouchLayout();
    private static native void nativeSetTouchLayout(String layout);
    // True once after F1's "Edit layout" was pressed.
    private static native boolean nativeTouchEditRequested();
    private static native boolean nativeTouchAimEnabled();
    private static native void nativeTouchAim(float dxDp, float dyDp);
    private static native void nativeTouchAimDown(boolean down);
    // A quick swipe up on the aim drag (trackSwipe): the Spring Ball, if F1 allows it.
    private static native void nativeTouchSpringSwipe();
    private static native boolean nativeTouchWheelsEnabled();
    private static native boolean nativeTouchVisorTapScan();
    // Bits 0-3 visors owned, 4-7 beams owned, 8-9 current visor, 10-11 current
    // beam, 12 valid (0 = no player: wheels disabled).
    private static native int nativeWheelOwned();
    // {width, height, ARGB pixels...} of the game's beam/visor icon, or null until the HUD has it.
    private static native int[] nativeWheelIcon(int wheel, int item);
    private static native void nativeRequestVisor(int visor);
    private static native void nativeRequestBeam(int beam);
    private static native long nativeRadialBegin(int kind, int owner);
    private static native void nativeRadialMove(long token, int owner, float x, float y);
    private static native void nativeRadialEnd(long token, int owner, float x, float y);
    private static native void nativeRadialCancel(long token, int owner);
    private static native void nativeRadialCancelAll();
    private static native void nativeRadialCancelLifecycle();
    private static native boolean nativeTouchMapTapEnabled();
    // The minimap's screen rect into out5: x0, y0, x1, y1 as fractions of the view, then 1
    // when the minimap is drawn there (0: draw a map button instead). False when no rect.
    private static native boolean nativeMinimapRect(float[] out5);
    private static native void nativeMapTap();
    // A map-screen drag in dp (zero deltas = finger still down); the view height in dp.
    private static native void nativeMapPan(float dxDp, float dyDp, float viewHeightDp);
    // True while the map screen is open and can be panned.
    private static native boolean nativeMapScreenOpen();
    // True while the pause menu (inventory, logbook) is up.
    private static native boolean nativePauseScreenOpen();
    // A save station or message box over the paused game (not the pause menu).
    private static native boolean nativeMessageBoxOpen();
    // A game menu or the map takes touches itself (the pointer_menus setting).
    private static native boolean nativePointerMenuActive();
    // A pinch: ratio of the finger spread now to before; above 1 zooms in.
    private static native void nativeMapZoom(float ratio);
    // A twist, in radians; positive turns the map as the stick's right does.
    private static native void nativeMapRotate(float radians);
    private static native void nativeSetTouchDevice(boolean xboxLayout);
    private static native void nativeToggleDebugOverlay();
    // MP2: a two-finger drag on the open F1 window, as mouse-wheel steps.
    private static native void nativeDebugWheel(float steps);
    // MP2: the Edit button asks for the editor (MP1's F1 "Edit layout"), and the editor's
    // Layout button steps Default -> Classic -> Twin stick.
    private static native void nativeRequestTouchEdit();
    private static native void nativeCycleTouchLayout();
    private static native void nativeVirtualButton(int button, boolean down);
    // Every control's press and release (touch: bindings), and whether a binding
    // takes the control over, so the overlay doesn't send its own button too.
    private static native void nativeTouchControl(int control, boolean down);
    private static native boolean nativeTouchControlBound(int control);
    // The control's Morph Ball row label in the ball: "" for a ball button with
    // none, null for a control that does its usual button there.
    private static native String nativeTouchBallLabel(int control);
    // The actions an added button can take: key, label, key, label...
    private static native String[] nativeTouchButtonActions();
    // The twin layout's Beam button is held: the D-pad picks beams, not visors.
    private static native void nativeTouchBeamShift(boolean held);
    // The Turbo button is held: the game acts as if Fire were mashed.
    private static native void nativeTouchTurboFire(boolean held);
    private static native void nativeVirtualAxis(int axis, float value);
    private static native boolean nativeTakePhysicalInput();
    private int leftPointer = -1;
    private int rightPointer = -1;
    private int aimPointer = -1;
    private int panPointer = -1;
    private int pan2Pointer = -1;
    private final Runnable panKeepAlive = new Runnable() {
        @Override
        public void run() {
            if (panPointer != -1) {
                nativeMapPan(0f, 0f, panViewDp());
                postDelayed(this, MAP_PAN_KEEPALIVE_MS);
            }
        }
    };
    private boolean hidden;
    // Hidden because a real pad, keyboard or mouse was used. Unlike HIDE, which
    // leaves a SHOW button, nothing is drawn and any touch brings them back.
    private boolean autoHidden;
    private boolean lastOverlayVisible;
    // What the buttons are labelled for (#106): play, morph ball, pause menu, map
    // screen or a message box. labelContext() as of the last draw; the poll
    // redraws when it changes.
    private static final int LABELS_PLAY = 0;
    private static final int LABELS_MORPH = 1;
    private static final int LABELS_PAUSE = 2;
    private static final int LABELS_MAP = 3;
    private static final int LABELS_DIALOG = 4;
    private int labelContextNow = LABELS_PLAY;

    private final Runnable physicalInputPoll = new Runnable() {
        @Override
        public void run() {
            // Always drained, so input from while the controls were already
            // hidden cannot hide them again the moment they come back.
            // With the controls off only MENU and START are left and they stay up.
            if (nativeTakePhysicalInput() && !hidden && !autoHidden && !editing &&
                nativeTouchControlsEnabled()) {
                autoHidden = true;
                releaseAll();
            }
            if (syncControlsOn()) {
                invalidate();
            }
            if (nativeTouchEditRequested()) {
                editPending = true;
            }
            // F1 can switch Turbo off while the overlay hides this view's draws.
            if (turboTouched() && !nativeTouchTurbo()) {
                releaseTurbo();
            }
            // The overlay can open without this view hearing of it (a pad, or
            // MENU while the first frames are slow enough that a one-off
            // redraw ran before the toggle landed), so redraw on any change.
            if (wheels) {
                final int mask = nativeWheelOwned();
                if (mask != lastWheelMask) {
                    lastWheelMask = mask;
                    invalidate();
                }
            }
            if (labels && !hidden && !autoHidden && labelContext() != labelContextNow) {
                invalidate();
            }
            // Morphing or unmorphing flips the page (drawControls releases what it hides).
            if (!editing && !hidden && controlsOnNow && inBall() != ballPage) {
                invalidate();
            }
            final boolean overlayVisible = nativeDebugOverlayVisible();
            if (overlayVisible != lastOverlayVisible) {
                lastOverlayVisible = overlayVisible;
                invalidate();
            }
            // F1 closes itself on the request; open the editor once it has.
            if (editPending && !overlayVisible) {
                editPending = false;
                beginEdit();
            } else if (editing && overlayVisible) {
                // The overlay was opened some other way (a pad's chord): keep what was done.
                endEdit(true);
            }
            postDelayed(this, PHYSICAL_INPUT_POLL_MS);
        }
    };

    TouchControlsView(Context context) {
        super(context);
        setClickable(true);
        setFocusable(false);
        fillPaint.setStyle(Paint.Style.FILL);
        strokePaint.setStyle(Paint.Style.STROKE);
        strokePaint.setStrokeWidth(dp(2));
        textPaint.setColor(Color.WHITE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setFakeBoldText(true);
        editPaint.setStyle(Paint.Style.STROKE);
        editPaint.setStrokeWidth(dp(2));
        // An empty saved layout is never parsed (syncLayout skips text equal to
        // layoutText's ""), so the defaults must be set here too.
        Arrays.fill(ovScale, 1f);
        Arrays.fill(ovAlpha, 100);
        for (int i = 0; i < editBar.length; ++i) {
            editBar[i] = new RectF();
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!editing) {
            syncLayout();
        }
        // The debug overlay is drawn into the surface below; stay out of its way.
        // It is usually closed from its own Close button, which this view never
        // hears about, so keep checking; otherwise the controls stayed invisible
        // until the next touch happened to redraw them.
        if (nativeDebugOverlayVisible() && !editing) {
            // MP2: only MENU stays, so the window can be closed again.
            drawMenuOnly(canvas, getWidth(), getHeight());
            postInvalidateDelayed(150);
            return;
        }
        syncControlsOn();
        if ((autoHidden || hidden) && !editing && controlsOnNow) {
            return;
        }

        float width = getWidth();
        float height = getHeight();
        // The classic setting can change mid-session, so re-check it every draw.
        // Off, aim and the wheels are always on.
        final boolean classic = nativeTouchClassic();
        colored = nativeTouchColors();
        labels = nativeTouchLabels();
        floating = nativeTouchFloatingStick();
        sideMargin = dp(nativeTouchSideMarginDp());
        stickInset = dp(nativeTouchStickInsetDp());
        buttonInset = dp(nativeTouchButtonInsetDp());
        pointerMenuNow = !editing && nativePointerMenuActive();
        if (pointerMenuNow) {
            bottomButtonRect(0, false, width, height, backButtonRect);
            drawBackButton(canvas);
            // The cog stays beside it, so the overlay opens from any menu.
            bottomButtonRect(1, false, width, height, cogButtonRect);
            final boolean cogDown = held.containsKey(TOGGLE_DEBUG_OVERLAY);
            fillPaint.setColor(cogDown ? 0xCC48C8E8 : 0x99081218);
            strokePaint.setColor(cogDown ? 0xFFE1F8FF : 0xCCFFFFFF);
            drawCornerButton(canvas, 1, cogButtonRect);
            removeCallbacks(mapButtonPoll);
            postDelayed(mapButtonPoll, MAP_BUTTON_POLL_MS);
            return;
        }
        if (!controlsOnNow && !editing) {
            drawMenuPills(canvas, width, height);
            removeCallbacks(mapButtonPoll);
            postDelayed(mapButtonPoll, MAP_BUTTON_POLL_MS);
            return;
        }
        // F1's opacity for the on-screen controls: the gameplay controls (and the
        // layout editor over them) fade together. The pointer menu's back and cog
        // buttons were drawn above this and stay fully visible.
        final float touchOpacity = editing ? 100f : nativeTouchOpacity();
        final int controlsAlpha = Math.round(touchOpacity * 2.55f);
        // 255 means no layer at all, the default, so nothing changes (and nothing
        // is paid for) at 100%.
        final int savedLayer = controlsAlpha < 255
                ? canvas.saveLayerAlpha(0f, 0f, width, height, controlsAlpha) : -1;
        try {
            drawControls(canvas, width, height, classic);
        } finally {
            // Every path back out of the controls, including an exception, puts the
            // canvas back.
            if (savedLayer >= 0) {
                canvas.restoreToCount(savedLayer);
            }
        }
    }

    // Re-reads F1's "On-screen controls"; true when it changed. Nothing held may
    // outlive the controls it came from, so a change lets go of everything.
    private boolean syncControlsOn() {
        final boolean on = nativeTouchControlsEnabled();
        if (on == controlsOnNow) {
            return false;
        }
        controlsOnNow = on;
        releaseAll();
        return true;
    }

    // The controls-off view: only START and MENU, fully opaque and at their default
    // spots (applyOverride skips a saved layout), so MENU is always reachable.
    private void drawMenuPills(Canvas canvas, float width, float height) {
        colored = false;
        labels = false;
        pills = GAMECUBE_PILLS;
        wheels = false;
        mapTap = false;
        mapButtonShown = false;
        for (PillButton pill : pills) {
            if (isMenuPill(pill)) {
                drawPillButton(canvas, pill, width, height);
            }
        }
    }

    // MP2: MENU alone, over the open F1 window. Sets what drawPillButton reads, as a
    // normal draw would, and keeps the rect a tap is tested against.
    private void drawMenuOnly(Canvas canvas, float width, float height) {
        final boolean classic = nativeTouchClassic();
        twin = !classic && nativeTouchTwinStick();
        pills = twin ? TWIN_PILLS : GAMECUBE_PILLS;
        colored = false;
        labels = true;
        sideMargin = dp(nativeTouchSideMarginDp());
        stickInset = dp(nativeTouchStickInsetDp());
        buttonInset = dp(nativeTouchButtonInsetDp());
        syncLayout();
        for (PillButton pill : pills) {
            if (cornerSlot(pill) == 1) {
                drawPillButton(canvas, pill, width, height);
                pillRect(pill, width, height, menuDrawn);
            }
        }
    }

    // The gameplay controls and the layout editor, without the pointer menu's own
    // buttons, which onDraw draws before this (outside the opacity layer).
    private void drawControls(Canvas canvas, float width, float height, boolean classic) {
        // Classic and twin stick are exclusive (the native side keeps them so).
        twin = !classic && nativeTouchTwinStick();
        face = twin ? TWIN_FACE : GAMECUBE_FACE;
        pills = twin ? TWIN_PILLS : GAMECUBE_PILLS;
        ballFace = twin ? TWIN_BALL_FACE : BALL_FACE;
        // The editor keeps the page its toolbar picked; play follows the game.
        final boolean page = editing ? ballPage : inBall();
        if (page != ballPage) {
            ballPage = page;
            releaseOffPage();
        }
        turbo = nativeTouchTurbo();
        turboButton = turbo ? (twin ? TWIN_TURBO : GAMECUBE_TURBO) : null;
        if (!turbo) {
            releaseTurbo();
        }
        cStick = classic || twin;
        aim = !classic || nativeTouchAimEnabled();
        // The wheels are the default layout's only beam and visor control; classic and
        // twin stick have the D-pad (twin: Beam held + D-pad) and the setting.
        wheels = (!classic && !twin) || nativeTouchWheelsEnabled();
        visorTapScan = nativeTouchVisorTapScan();
        mapTap = nativeTouchMapTapEnabled();
        if (!editing) {
            // A layout made on another screen, or before a rotation or resize,
            // must not leave a control off screen where it can't be grabbed.
            for (int i = 0; i < ALL_CONTROLS; ++i) {
                if (!removed(i) && layoutHas(i) && (ovDx[i] != 0f || ovDy[i] != 0f || ovScale[i] != 1f)) {
                    clampToScreen(i, width, height);
                }
            }
        }
        bottomButtonRect(0, true, width, height, hideBounds);
        editButtonRect(width, height, editBounds);
        if (leftPointer != -1 && leftFloated) {
            final float r = leftStickRadius(height);
            layerRect.set(floatX - r, floatY - r, floatX + r, floatY + r);
            final int layer = pushLayer(canvas, C_LSTICK, layerRect);
            if (layer != LAYER_SKIP) {
                drawStick(canvas, floatX, floatY, r, leftPointer, 0);
                popLayer(canvas, layer);
            }
        } else if (!floatingStick()) {
            final int layer = pushControl(canvas, C_LSTICK, width, height);
            if (layer != LAYER_SKIP) {
                drawStick(canvas, leftStickX(width, height), leftStickY(width, height),
                          leftStickRadius(height), leftPointer, 0);
                popLayer(canvas, layer);
            }
        }
        // The right stick is the C-stick, yellow on the GameCube pad, in the classic
        // layout; in twin stick it aims, plain like the left. Otherwise a drag
        // anywhere free aims.
        if (cStick) {
            final int layer = pushControl(canvas, twin ? C_RSTICK : C_CSTICK, width, height);
            if (layer != LAYER_SKIP) {
                drawStick(canvas, rightStickX(width, height), rightStickY(height),
                          rightStickRadius(height), rightPointer, colored && !twin ? GC_YELLOW : 0);
                popLayer(canvas, layer);
            }
        }

        rHiddenNow = !editing && rHidden();
        labelContextNow = labels ? labelContext() : LABELS_PLAY;
        for (PillButton pill : pills) {
            if (pillShown(pill)) {
                final int layer = pushControl(canvas, pillControl(pill), width, height);
                if (layer != LAYER_SKIP) {
                    drawPillButton(canvas, pill, width, height);
                    popLayer(canvas, layer);
                }
            }
        }
        for (ControlButton button : face) {
            drawControlButton(canvas, button, width, height);
        }
        for (ControlButton button : ballFace) {
            drawControlButton(canvas, button, width, height);
        }
        if (turboButton != null) {
            drawControlButton(canvas, turboButton, width, height);
        }
        if (wheels) {
            drawWheelButtons(canvas, width, height);
        } else {
            final int layer = pushControl(canvas, C_DPAD, width, height);
            if (layer != LAYER_SKIP) {
                drawDpad(canvas, width, height);
                popLayer(canvas, layer);
            }
        }
        final int eyeLayer = pushLayer(canvas, C_EYE, hideBounds);
        if (eyeLayer != LAYER_SKIP) {
            drawEye(canvas, hideBounds);
            popLayer(canvas, eyeLayer);
        }
        final int editLayer = pushLayer(canvas, C_EDIT, editBounds);
        if (editLayer != LAYER_SKIP) {
            drawEditButton(canvas, editBounds);
            popLayer(canvas, editLayer);
        }
        mapButtonShown = mapButtonState(width, height);
        if (mapButtonShown) {
            final int layer = pushLayer(canvas, C_MAP, mapButtonRect);
            if (layer != LAYER_SKIP) {
                drawMapButton(canvas);
                popLayer(canvas, layer);
            }
        }
        drawCustomButtons(canvas, width, height);
        removeCallbacks(mapButtonPoll);
        postDelayed(mapButtonPoll, MAP_BUTTON_POLL_MS);
        if (wheels && wheelPointer != -1) {
            drawWheel(canvas);
        }
        if (editing) {
            drawEditOverlay(canvas, width, height);
        }
    }

    // Whether a saved layout hides this control from play. Not in the editor
    // (which still shows and selects it) nor in the controls-off view, where
    // START and MENU ignore the layout.
    private boolean ctrlHidden(int control) {
        return controlsOnNow &&
               (!onPage(control) || (!editing && (ovHidden[control] || removed(control))));
    }

    private boolean removed(int control) {
        return control >= 0 && control < CONTROLS && ovRemoved[control];
    }

    // The percent this control is drawn at, or -1 for not at all.
    private int controlAlpha(int control) {
        if (removed(control)) {
            return -1; // the controls-off view draws START and MENU without this
        }
        if (controlsOnNow && !onPage(control)) {
            return -1;
        }
        if (editing) {
            return ovHidden[control] ? EDIT_MIN_ALPHA : Math.max(EDIT_MIN_ALPHA, ovAlpha[control]);
        }
        if (!controlsOnNow) {
            return 100;
        }
        return ovHidden[control] ? -1 : ovAlpha[control];
    }

    // Opens a layer for one control's own opacity round `bounds` (plus slack for
    // strokes and labels), or LAYER_SKIP for a hidden one. At 100% there is no
    // layer, so default layouts cost nothing. Pair with popLayer.
    private int pushLayer(Canvas canvas, int control, RectF bounds) {
        final int alpha = controlAlpha(control);
        if (alpha < 0) {
            return LAYER_SKIP;
        }
        if (alpha >= 100) {
            return LAYER_NONE;
        }
        final float slack = dp(LAYER_SLACK_DP);
        return canvas.saveLayerAlpha(bounds.left - slack, bounds.top - slack,
                                     bounds.right + slack, bounds.bottom + slack,
                                     Math.round(alpha * 2.55f));
    }

    private int pushControl(Canvas canvas, int control, float width, float height) {
        if (controlAlpha(control) >= 100) {
            return LAYER_NONE; // skip the bounds work too
        }
        controlBounds(control, width, height, layerRect);
        return pushLayer(canvas, control, layerRect);
    }

    private static void popLayer(Canvas canvas, int layer) {
        if (layer >= 0) {
            canvas.restoreToCount(layer);
        }
    }

    private void drawControlButton(Canvas canvas, ControlButton button, float width,
                                   float height) {
        final int layer = pushControl(canvas, controlOf(button), width, height);
        if (layer != LAYER_SKIP) {
            drawButton(canvas, button, width, height);
            popLayer(canvas, layer);
        }
    }

    // True, with mapButtonRect set, when the map button should show.
    private boolean mapButtonState(float width, float height) {
        // Shown in the open map too: a tap is a Z press, which closes it. The
        // editor shows it whenever it can appear.
        if (!mapTap || width <= 0f || height <= 0f ||
            (!editing && !nativeMinimapRect(minimapRect) && !nativeMapScreenOpen())) {
            return false;
        }
        bottomButtonRect(1, true, width, height, mapButtonRect);
        return true;
    }

    // The square around bottom button `slot`, counted from the side edge: on the
    // right, 0 is the eye and 1 the map; on the left, 0 is START and 1 MENU.
    private void bottomButtonRect(int slot, boolean right, float width, float height,
                                  RectF out) {
        final float d = 2f * dp(BOTTOM_BUTTON_RADIUS_DP);
        final float fromSide = sideMargin + dp(BOTTOM_BUTTON_SIDE_DP) + slot * (d + dp(BOTTOM_BUTTON_GAP_DP));
        final float left = right ? width - fromSide - d : fromSide;
        final float bottom = height - dp(BOTTOM_BUTTON_BOTTOM_DP);
        out.set(left, bottom - d, left + d, bottom);
        applyOverride(right ? (slot == 0 ? C_EYE : C_MAP) : (slot == 0 ? C_START : C_MENU), out,
                      height);
    }

    // MP2: the Edit button sits in the map button's slot (MP2 draws no map button), with an
    // override of its own so it moves without the eye.
    private void editButtonRect(float width, float height, RectF out) {
        final float d = 2f * dp(BOTTOM_BUTTON_RADIUS_DP);
        final float fromSide = sideMargin + dp(BOTTOM_BUTTON_SIDE_DP) + d + dp(BOTTOM_BUTTON_GAP_DP);
        final float bottom = height - dp(BOTTOM_BUTTON_BOTTOM_DP);
        out.set(width - fromSide - d, bottom - d, width - fromSide, bottom);
        applyOverride(C_EDIT, out, height);
    }

    private void drawEditButton(Canvas canvas, RectF bounds) {
        if (mapTap) {
            return; // the map button has the slot
        }
        fillPaint.setColor(0x99081218);
        strokePaint.setColor(0xCCFFFFFF);
        final float r = bounds.width() * 0.5f;
        canvas.drawCircle(bounds.centerX(), bounds.centerY(), r, fillPaint);
        canvas.drawCircle(bounds.centerX(), bounds.centerY(), r, strokePaint);
        drawCenteredLabel(canvas, "Edit", bounds.centerX(), bounds.centerY(), dp(11));
    }

    // Moves and scales a control's default rect by its override, round its centre.
    private void applyOverride(int control, RectF rect, float height) {
        if (!controlsOnNow && !editing) {
            return; // MENU and START stay put and reachable
        }
        if (ovDx[control] == 0f && ovDy[control] == 0f && ovScale[control] == 1f) {
            return; // the default layout stays bit-for-bit what it was
        }
        final float u = layoutU(height);
        final float cx = rect.centerX() + ovDx[control] * u;
        final float cy = rect.centerY() + ovDy[control] * u;
        final float halfW = rect.width() * 0.5f * ovScale[control];
        final float halfH = rect.height() * 0.5f * ovScale[control];
        rect.set(cx - halfW, cy - halfH, cx + halfW, cy + halfH);
    }

    // The back button of a pointer menu, where START sits; hiding the controls hides it.
    private boolean onBackButton(float x, float y) {
        return pointerMenuNow && (!controlsOnNow || (!hidden && !autoHidden)) && backButtonRect.contains(x, y);
    }

    // The cog beside it, where MENU sits.
    private boolean onCogButton(float x, float y) {
        return pointerMenuNow && (!controlsOnNow || (!hidden && !autoHidden)) && cogButtonRect.contains(x, y);
    }

    // A round button with a left arrow: B in the game's menus and map.
    private void drawBackButton(Canvas canvas) {
        final float cx = backButtonRect.centerX();
        final float cy = backButtonRect.centerY();
        final float radius = backButtonRect.width() * 0.5f;
        fillPaint.setColor(held.containsKey(BTN_EAST) ? 0xCC3A5A70 : 0x99081218);
        strokePaint.setColor(0xBBFFFFFF);
        canvas.drawCircle(cx, cy, radius, fillPaint);
        canvas.drawCircle(cx, cy, radius, strokePaint);
        final float w = radius * 0.5f;
        shapePath.reset();
        shapePath.moveTo(cx + w, cy);
        shapePath.lineTo(cx - w, cy);
        shapePath.moveTo(cx - w * 0.3f, cy - w * 0.7f);
        shapePath.lineTo(cx - w, cy);
        shapePath.lineTo(cx - w * 0.3f, cy + w * 0.7f);
        canvas.drawPath(shapePath, strokePaint);
    }

    // A round button with a folded map: three panels, the middle one raised.
    private void drawMapButton(Canvas canvas) {
        final float cx = mapButtonRect.centerX();
        final float cy = mapButtonRect.centerY();
        final float radius = mapButtonRect.width() * 0.5f;
        fillPaint.setColor(0x99081218);
        strokePaint.setColor(0xBBFFFFFF);
        canvas.drawCircle(cx, cy, radius, fillPaint);
        canvas.drawCircle(cx, cy, radius, strokePaint);
        final float w = radius * 1.1f;
        final float h = radius * 0.8f;
        final float l = cx - w / 2f;
        final float t = cy - h / 2f;
        final float tilt = h * 0.12f;
        shapePath.reset();
        shapePath.moveTo(l, t + tilt);
        shapePath.lineTo(l + w / 3f, t);
        shapePath.lineTo(l + 2f * w / 3f, t + tilt);
        shapePath.lineTo(l + w, t);
        shapePath.lineTo(l + w, t + h - tilt);
        shapePath.lineTo(l + 2f * w / 3f, t + h);
        shapePath.lineTo(l + w / 3f, t + h - tilt);
        shapePath.lineTo(l, t + h);
        shapePath.close();
        shapePath.moveTo(l + w / 3f, t);
        shapePath.lineTo(l + w / 3f, t + h - tilt);
        shapePath.moveTo(l + 2f * w / 3f, t + tilt);
        shapePath.lineTo(l + 2f * w / 3f, t + h);
        canvas.drawPath(shapePath, strokePaint);
    }

    // A mouse is not a finger on the overlay. Its clicks are dispatched as
    // touches, and hover goes to the topmost hoverable view (this one, being
    // clickable), so without these the SDL surface below saw neither and a
    // click landed on whatever on-screen button was under the cursor. Declining
    // passes them down to the surface. Once SDL captures the pointer for mouse
    // aim, events go to the focused surface and never reach here.
    private static boolean fromMouse(MotionEvent event) {
        return event.getToolType(event.getActionIndex()) == MotionEvent.TOOL_TYPE_MOUSE;
    }

    @Override
    public boolean onHoverEvent(MotionEvent event) {
        return !fromMouse(event) && super.onHoverEvent(event);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        // The editor takes every touch, a mouse's included: nothing reaches the game.
        if (editing) {
            return onEditTouch(event);
        }
        if (fromMouse(event)) {
            return false;
        }
        int action = event.getActionMasked();
        int actionIndex = event.getActionIndex();
        final int eventOwner = event.getPointerId(actionIndex);
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            cancelledRadialPointers.remove(eventOwner); // fresh down, not a late move/up
        } else if ((action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) &&
                   cancelledRadialPointers.remove(eventOwner)) {
            forwarded.remove(eventOwner);
            return true;
        }

        // While the debug overlay is open the game is paused and the touches
        // are for it, so decline them and let the SDL surface below have them.
        // Anything still held has to go first: once this view stops claiming
        // touches the matching releases never arrive, which left whatever was
        // down (a trigger, say) held for the rest of the session.
        syncControlsOn();
        final boolean overlayVisible = nativeDebugOverlayVisible();
        if (overlayVisible && (!targets.isEmpty() || !held.isEmpty())) {
            releaseAll();
        }
        if (overlayVisible || menuPointer != -1) {
            return onDebugTouch(event, action, actionIndex, overlayVisible);
        }
        // In a pointer menu every finger but one on the back or cog button is the game's.
        final boolean menu = !overlayVisible && nativePointerMenuActive();
        final float actionX = event.getX(actionIndex);
        final float actionY = event.getY(actionIndex);
        final boolean toGame = overlayVisible ||
            (menu && !onBackButton(actionX, actionY) && !onCogButton(actionX, actionY));
        if (action == MotionEvent.ACTION_DOWN) {
            cancelForwarded(event);
            if (toGame) {
                return false;
            }
        } else if (forwardToSdl(event, action, actionIndex, toGame) || overlayVisible) {
            return true;
        }

        // A tap on the "newer release" toast opens its page and is not also a press.
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            View parent = (View) getParent();
            if (parent != null
                    && nativeTapUpdateToast(
                            (getLeft() + event.getX(actionIndex)) / Math.max(1, parent.getWidth() - 1),
                            (getTop() + event.getY(actionIndex)) / Math.max(1, parent.getHeight() - 1))) {
                return true;
            }
        }

        // With the controls off nothing is hidden and no touch brings anything back.
        if (autoHidden && controlsOnNow) {
            // The touch that brings them back is not also a press.
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
                autoHidden = false;
                nativeTakePhysicalInput();
                nativeSetTouchDevice(twin);
                invalidate();
            }
            return true;
        }

        if (hidden && controlsOnNow) {
            // Any touch brings the controls back, and is not also a press.
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
                setHidden(false);
                performClick();
            }
            return true;
        }

        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            // The in-game prompts follow the input the player reached for, so
            // tell the port which set this overlay is showing.
            nativeSetTouchDevice(twin);
            int pointerId = event.getPointerId(actionIndex);
            float x = event.getX(actionIndex);
            float y = event.getY(actionIndex);
            if (menu) {
                // Only the back and cog buttons get here (toGame took the rest).
                final int id = onCogButton(x, y) ? TOGGLE_DEBUG_OVERLAY : BTN_EAST;
                targets.put(pointerId, new TouchTarget(BUTTON, id));
                pressControl(id);
                invalidate();
                return true;
            }
            if (controlsOnNow && !ctrlHidden(C_EYE) && hideBounds.contains(x, y)) {
                targets.put(pointerId, new TouchTarget(HIDE, 0));
                return true;
            }
            if (controlsOnNow && !mapTap && editBounds.contains(x, y)) {
                targets.put(pointerId, new TouchTarget(EDIT, 0));
                return true;
            }
                        if (controlsOnNow && mapButtonShown && !ctrlHidden(C_MAP) &&
                mapButtonRect.contains(x, y)) {
                targets.put(pointerId, TouchTarget.begin(MAP_TAP, 0, x, y));
                return true;
            }
            assignPointer(pointerId, x, y);
        } else if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < event.getPointerCount(); ++i) {
                TouchTarget target = targets.get(event.getPointerId(i));
                if (target == null) {
                    continue;
                }
                if (target.radialToken != 0) {
                    updateRadial(target, event.getX(i), event.getY(i));
                } else if (target.type == LEFT_STICK || target.type == RIGHT_STICK) {
                    updateStick(target, event.getX(i), event.getY(i));
                } else if (target.type == AIM || target.aiming) {
                    updateAim(target, event, i);
                } else if (target.type == BUTTON && aimsWhileHeld(target) && aim &&
                           aimPointer == -1) {
                    startButtonAim(event.getPointerId(i), target, event, i);
                } else if (target.type == MAP_PAN) {
                    if (pan2Pointer == -1) {
                        updateMapPan(target, event, i);
                    }
                } else if (target.type == WHEEL) {
                    target.x = event.getX(i);
                    target.y = event.getY(i);
                } else if (target.type == MAP_TAP) {
                    target.x = event.getX(i);
                    target.y = event.getY(i);
                }
            }
            if (panPointer != -1 && pan2Pointer != -1) {
                updatePinch(event);
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            TouchTarget target = targets.get(event.getPointerId(actionIndex));
            if (target != null && target.radialToken != 0) {
                updateRadial(target, event.getX(actionIndex), event.getY(actionIndex));
            }
            releasePointer(event.getPointerId(actionIndex));
        } else if (action == MotionEvent.ACTION_CANCEL) {
            releaseAll();
        }
        invalidate();
        return true;
    }

    // MP2: the MENU pill's rect as it was last drawn over the window, which is what a tap is
    // tested against so the two cannot disagree.
    private final RectF menuDrawn = new RectF();
    // The finger that went down on MENU, so a drag off it does not toggle the window.
    private int menuPointer = -1;
    // A two-finger drag scrolls the window: a finger cannot turn a mouse wheel, and the settings
    // list is taller than a phone screen.
    private boolean scrolling;
    private float scrollLastY;

    // Touches while the F1 window is open: MENU stays live so the window can be closed, a
    // two-finger drag scrolls, and everything else is handed to SDL, whose touch-to-mouse path is
    // what the ImGui window reads. Forwarding through SDL rather than injecting ImGui events here
    // is also what makes the coordinates right: sendToSdl normalises them against the surface.
    private boolean onDebugTouch(MotionEvent event, int action, int actionIndex,
                                 boolean overlayVisible) {
        final int id = event.getPointerId(actionIndex);
        if (!overlayVisible) {
            // The window closed under a finger: SDL must not keep it held.
            cancelForwarded(event);
            menuPointer = -1;
            scrolling = false;
            return true;
        }
        switch (action) {
            case MotionEvent.ACTION_DOWN:
                if (menuDrawn.contains(event.getX(actionIndex), event.getY(actionIndex))) {
                    menuPointer = id;
                    return true;
                }
                forwarded.add(id);
                sendToSdl(event, actionIndex, MotionEvent.ACTION_DOWN);
                return true;
            case MotionEvent.ACTION_POINTER_DOWN:
                // Photo mode: one finger moves and another looks (PhotoTouchEvent), so every
                // finger is SDL's.
                if (!scrolling && nativePhotoTouch()) {
                    if (menuPointer == -1 &&
                        menuDrawn.contains(event.getX(actionIndex), event.getY(actionIndex))) {
                        menuPointer = id;
                    } else {
                        forwarded.add(id);
                        sendToSdl(event, actionIndex, MotionEvent.ACTION_DOWN);
                    }
                    return true;
                }
                // A second finger: the gesture is a scroll, and the finger SDL already holds has
                // to let go or its drag would move the window while it scrolls.
                if (!scrolling) {
                    scrolling = true;
                    scrollLastY = averageY(event);
                    cancelForwarded(event);
                }
                return true;
            case MotionEvent.ACTION_MOVE:
                if (scrolling) {
                    final float y = averageY(event);
                    final float dy = y - scrollLastY;
                    scrollLastY = y;
                    if (Math.abs(dy) >= 1f) {
                        nativeDebugWheel(dy / dp(24));
                    }
                    return true;
                }
                for (int i = 0; i < event.getPointerCount(); ++i) {
                    if (forwarded.contains(event.getPointerId(i))) {
                        sendToSdl(event, i, MotionEvent.ACTION_MOVE);
                    }
                }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                if (id == menuPointer) {
                    menuPointer = -1;
                    if (menuDrawn.contains(event.getX(actionIndex), event.getY(actionIndex))) {
                        cancelForwarded(event);
                        nativeToggleDebugOverlay();
                        invalidate();
                    }
                    return true;
                }
                if (scrolling) {
                    if (event.getPointerCount() <= 2) {
                        scrolling = false;
                    }
                    return true;
                }
                if (forwarded.remove(id)) {
                    sendToSdl(event, actionIndex, MotionEvent.ACTION_UP);
                }
                return true;
            case MotionEvent.ACTION_CANCEL:
                cancelForwarded(event);
                menuPointer = -1;
                scrolling = false;
                return true;
            default:
                return true;
        }
    }

    private static float averageY(MotionEvent event) {
        float sum = 0f;
        for (int i = 0; i < event.getPointerCount(); ++i) {
            sum += event.getY(i);
        }
        return sum / Math.max(1, event.getPointerCount());
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        postDelayed(physicalInputPoll, PHYSICAL_INPUT_POLL_MS);
    }

    @Override
    protected void onDetachedFromWindow() {
        removeCallbacks(physicalInputPoll);
        removeCallbacks(mapButtonPoll);
        // No more touch events will arrive to release what is held.
        releaseAll();
        super.onDetachedFromWindow();
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }

    // Lets go of what a page switch hid (a face button held into the ball, a ball
    // button held out of it), keeping the shared ones, so the stick stays held.
    private void releaseOffPage() {
        final java.util.Iterator<Map.Entry<Integer, TouchTarget>> it = targets.entrySet().iterator();
        while (it.hasNext()) {
            final Map.Entry<Integer, TouchTarget> entry = it.next();
            final TouchTarget target = entry.getValue();
            final boolean off;
            if (target.type == BUTTON) {
                off = target.touchControl >= 0 && !onPage(target.touchControl);
            } else if (target.type == RIGHT_STICK) {
                off = !onPage(twin ? C_RSTICK : C_CSTICK);
            } else {
                off = target.type == WHEEL;
            }
            if (!off) {
                continue;
            }
            releaseTarget(target);
            it.remove();
            if (entry.getKey() == rightPointer) {
                rightPointer = -1;
            }
        }
    }

    void releaseAll() {
        nativeRadialCancelAll(); // touch-only, including a released touch retry with no target
        for (Map.Entry<Integer, TouchTarget> entry : targets.entrySet()) {
            if (entry.getValue().radialToken != 0) cancelledRadialPointers.add(entry.getKey());
        }
        for (TouchTarget target : targets.values()) {
            releaseTarget(target);
        }
        targets.clear();
        held.clear();
        for (int control = 0; control < touchHeld.length; ++control) {
            if (touchHeld[control] != 0) {
                touchHeld[control] = 0;
                nativeTouchControl(touchCode(control), false);
            }
        }
        leftPointer = -1;
        rightPointer = -1;
        aimPointer = -1;
        panPointer = -1;
        pan2Pointer = -1;
        invalidate();
    }

    // Android sends a new finger to the view that already owns the gesture,
    // without asking whether it wants it. So while any finger that went down on
    // these controls is still held (a thumb resting on the stick, or the one that
    // tapped MENU), every tap on the debug overlay lands here rather than on the
    // SDL surface, and the overlay seems frozen until that finger lifts. Fingers
    // that go down while the overlay is open are handed to SDL directly instead,
    // and keep going there until they lift, even if the overlay closes first:
    // SDL drives its touch mouse from the first finger down and ignores every
    // other finger until that one comes up.
    //
    // Returns whether the event was entirely such fingers.
    private boolean forwardToSdl(MotionEvent event, int action, int actionIndex,
                                 boolean toGame) {
        final int pointerId = event.getPointerId(actionIndex);
        switch (action) {
            case MotionEvent.ACTION_POINTER_DOWN:
                if (!toGame) {
                    return false;
                }
                forwarded.add(pointerId);
                sendToSdl(event, actionIndex, MotionEvent.ACTION_DOWN);
                return true;
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_UP:
                if (!forwarded.remove(pointerId)) {
                    return false;
                }
                sendToSdl(event, actionIndex, MotionEvent.ACTION_UP);
                return true;
            case MotionEvent.ACTION_MOVE: {
                boolean all = true;
                for (int i = 0; i < event.getPointerCount(); ++i) {
                    if (forwarded.contains(event.getPointerId(i))) {
                        sendToSdl(event, i, MotionEvent.ACTION_MOVE);
                    } else {
                        all = false;
                    }
                }
                return all;
            }
            case MotionEvent.ACTION_CANCEL:
                cancelForwarded(event);
                return false;
            default:
                return false;
        }
    }

    // A gesture that ends without its releases (cancelled, or a new one begun)
    // must still lift SDL's fingers, or its touch mouse stays stuck on them.
    private void cancelForwarded(MotionEvent event) {
        if (forwarded.isEmpty()) {
            return;
        }
        for (int pointerId : forwarded) {
            SDLActivity.onNativeTouch(event.getDeviceId(), pointerId, MotionEvent.ACTION_CANCEL,
                                      0f, 0f, 0f);
        }
        forwarded.clear();
    }

    private void sendToSdl(MotionEvent event, int index, int action) {
        if (cancelledRadialPointers.contains(event.getPointerId(index))) return;
        // SDL wants the position normalised to its surface, which fills this
        // view's parent; this view does not while it is shrunk to SHOW.
        View parent = (View) getParent();
        if (parent == null) {
            return; // a late touch during teardown
        }
        float x = (getLeft() + event.getX(index)) / Math.max(1, parent.getWidth() - 1);
        float y = (getTop() + event.getY(index)) / Math.max(1, parent.getHeight() - 1);
        float pressure = Math.min(event.getPressure(index), 1f);
        SDLActivity.onNativeTouch(event.getDeviceId(), event.getPointerId(index), action, x, y,
                                  pressure);
    }

    // Touch-down routing, first match wins: pills, added buttons, face buttons, the wheel
    // buttons (a hit swallows the touch even when no wheel opens), the D-pad,
    // the HUD minimap, the left stick, then the open map's pan (nothing else
    // applies while the map is open), the classic C-stick zone, and the free
    // area aims.
    private boolean beginRadial(TouchTarget target, int kind, int pointerId) {
        target.radialToken = nativeRadialBegin(kind, pointerId);
        target.radialOwner = pointerId;
        return target.radialToken != 0;
    }

    private void updateRadial(TouchTarget target, float x, float y) {
        target.x = x;
        target.y = y;
        if (target.radialToken > 0) {
            // Donor's 72 dp reach, from THIS finger's down point. Never viewport fractions.
            final float reach = dp(72f);
            nativeRadialMove(target.radialToken, target.radialOwner,
                             (x - target.startX) / reach, (target.startY - y) / reach);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (!hasFocus) {
            nativeRadialCancelLifecycle();
            releaseAll();
        }
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        if (oldw > 0 && oldh > 0 && (w != oldw || h != oldh)) nativeRadialCancelAll();
    }

    private void assignPointer(int pointerId, float x, float y) {
        float width = getWidth();
        float height = getHeight();
        for (PillButton pill : pills) {
            if (!pillShown(pill)) {
                continue;
            }
            pillRect(pill, width, height, pillHit);
            if (pillHit.contains(x, y)) {
                final TouchTarget target = new TouchTarget(BUTTON, pill.id());
                target.control = pill;
                targets.put(pointerId, target);
                pressTarget(target, pillControl(pill), pill.id() != TOGGLE_DEBUG_OVERLAY);
                return;
            }
        }
        if (!controlsOnNow) {
            return; // only the two menu pills above; the rest of the screen does nothing
        }
        // The topmost (last drawn) added button under the finger.
        for (int control = ALL_CONTROLS - 1; control >= C_CUSTOM; --control) {
            if (customAction[control - C_CUSTOM] == null || ctrlHidden(control)) {
                continue;
            }
            customGeometry(control, width, height, faceGeo);
            final float dx = x - faceGeo[0];
            final float dy = y - faceGeo[1];
            if (dx * dx + dy * dy <= faceGeo[2] * faceGeo[2]) {
                final TouchTarget target = TouchTarget.begin(BUTTON, CUSTOM_BUTTON, x, y);
                targets.put(pointerId, target);
                final String action = customAction[control - C_CUSTOM];
                final int kind = "beam_menu".equals(action) ? 0 : "visor_menu".equals(action) ? 1 : -1;
                if (kind >= 0 && beginRadial(target, kind, pointerId)) {
                    return; // no legacy binding, button/fire or look source from this finger
                }
                pressTarget(target, control, true);
                return;
            }
        }
        for (ControlButton button : face) {
            if (!ctrlHidden(controlOf(button)) && hitButton(button, x, y, width, height)) {
                final TouchTarget target = TouchTarget.begin(BUTTON, button.button, x, y);
                target.control = button;
                targets.put(pointerId, target);
                pressTarget(target, controlOf(button), true);
                return;
            }
        }
        for (ControlButton button : ballFace) {
            if (!ctrlHidden(controlOf(button)) && hitButton(button, x, y, width, height)) {
                // Its action is always a binding, like an added button's.
                final TouchTarget target = TouchTarget.begin(BUTTON, CUSTOM_BUTTON, x, y);
                target.control = button;
                targets.put(pointerId, target);
                pressTarget(target, controlOf(button), true);
                return;
            }
        }
        if (turboButton != null && !ctrlHidden(controlOf(turboButton)) &&
            hitButton(turboButton, x, y, width, height)) {
            final TouchTarget target = TouchTarget.begin(BUTTON, TURBO_FIRE, x, y);
            target.control = turboButton;
            targets.put(pointerId, target);
            pressTarget(target, controlOf(turboButton), true);
            return;
        }
        // Hidden wheel buttons (wheels unusable) let the touch through.
        if (wheels && (nativeWheelOwned() & WHEEL_VALID_BIT) != 0) {
            final int wheel = wheelButtonAt(x, y, width, height);
            if (wheel != -1) {
                // Not while the map is open (the overlay is handled before this).
                if (wheelPointer == -1 && !nativeMapScreenOpen()) {
                    TouchTarget target = TouchTarget.begin(WHEEL, wheel, x, y);
                    target.startMs = SystemClock.uptimeMillis();
                    // A moved button must still open its wheel on screen.
                    final float reach = dp(WHEEL_RADIUS_DP);
                    wheelCx = Math.max(reach, Math.min(width - reach,
                                                       wheelButtonX(wheel, width, height)));
                    wheelCy = Math.max(reach, Math.min(height - reach,
                                                       wheelButtonY(wheel, height)));
                    wheelPointer = pointerId;
                    targets.put(pointerId, target);
                    beginRadial(target, wheel == 1 ? 0 : 1, pointerId);
                }
                return;
            }
        }
        int dpadButton = wheels || ctrlHidden(C_DPAD) ? -1 : dpadButtonAt(x, y, width, height);
        if (dpadButton != -1) {
            targets.put(pointerId, new TouchTarget(BUTTON, dpadButton));
            pressControl(dpadButton);
            return;
        }

        if (mapTap && width > 0f && height > 0f && nativeMinimapRect(minimapRect) &&
            minimapRect[4] != 0f && x >= minimapRect[0] * width && x <= minimapRect[2] * width &&
            y >= minimapRect[1] * height && y <= minimapRect[3] * height) {
            targets.put(pointerId, TouchTarget.begin(MAP_TAP, 0, x, y));
            return;
        }
        final boolean leftHidden = ctrlHidden(C_LSTICK);
        final boolean floatHere = !leftHidden && floatingStick() && x < width * 0.5f;
        if ((floatHere || (!leftHidden && !floatingStick() && onLeftStick(x, y, width, height))) &&
            leftPointer == -1) {
            if (floatHere) {
                final float radius = leftStickRadius(height);
                floatX = Math.max(radius, Math.min(width - radius, x));
                floatY = Math.max(radius, Math.min(height - radius, y));
            }
            TouchTarget target = new TouchTarget(LEFT_STICK, 0);
            leftPointer = pointerId;
            leftFloated = floatHere;
            targets.put(pointerId, target);
            updateStick(target, x, y);
        } else if (nativeMapScreenOpen()) {
            // Everything else that is free pans the open map, one finger at a time.
            if (panPointer == -1) {
                panPointer = pointerId;
                targets.put(pointerId, TouchTarget.begin(MAP_PAN, 0, x, y));
                nativeMapPan(0f, 0f, panViewDp());
                postDelayed(panKeepAlive, MAP_PAN_KEEPALIVE_MS);
            } else if (pan2Pointer == -1) {
                pan2Pointer = pointerId;
                targets.put(pointerId, TouchTarget.begin(MAP_PAN2, 0, x, y));
            }
        } else if (cStick && !ctrlHidden(twin ? C_RSTICK : C_CSTICK) &&
                   inRightStickGrab(x, y, width, height)) {
            if (rightPointer == -1) {
                TouchTarget target = new TouchTarget(RIGHT_STICK, 0);
                rightPointer = pointerId;
                targets.put(pointerId, target);
                updateStick(target, x, y);
            }
        } else if (aim && aimPointer == -1) {
            // Everything else that is free: one finger at a time aims.
            aimPointer = pointerId;
            targets.put(pointerId, TouchTarget.begin(AIM, 0, x, y));
            nativeTouchAimDown(true);
        }
    }

    // The floating left stick, except on the map screen (whose free area pans)
    // and in the layout editor, which place the fixed one.
    private boolean floatingStick() {
        return floating && !editing && !nativeMapScreenOpen();
    }

    // A press within half a stick radius outside the left stick's ring grabs it.
    private boolean onLeftStick(float x, float y, float width, float height) {
        final float dx = x - leftStickX(width, height);
        final float dy = y - leftStickY(width, height);
        final float reach = leftStickRadius(height) * LEFT_STICK_REACH;
        return dx * dx + dy * dy <= reach * reach;
    }

    // The layout height: the view's, capped at a phone's. Fractions of the
    // height are sizes and offsets from it; positions go through the layout
    // helpers below, which keep a fraction's distance to the nearest edge,
    // scaled by u / height.
    private float layoutU(float height) {
        return Math.min(height, dp(MAX_LAYOUT_DP));
    }

    private float layoutScale(float height) {
        return height > 0f ? layoutU(height) / height : 1f;
    }

    // A horizontal fraction of the width, from the nearest side edge, plus the
    // side margin.
    private float layoutX(float fraction, float width, float height) {
        final float s = layoutScale(height);
        return fraction < 0.5f ? sideMargin + fraction * width * s
                               : fraction * width + (1f - fraction) * width * (1f - s) - sideMargin;
    }

    // A vertical fraction of the height. The layout sits in a phone-height band
    // at the bottom of the screen, so the top half is still in reach.
    private float layoutY(float fraction, float height) {
        return fraction < 0.5f ? (height - layoutU(height)) + fraction * layoutU(height)
                               : layoutYFromBottom(fraction, height);
    }

    private float layoutYFromBottom(float fraction, float height) {
        return fraction * height + (1f - fraction) * (height - layoutU(height));
    }

    private final RectF stickFaceRect = new RectF();

    // Mirrors the face cluster's gap to the right edge (side margin included,
    // button inset not), plus the stick inset, so the two thumbs' controls look
    // balanced.
    // The default position, without the override: the Visor button, the face
    // cluster and the pill shift hang off it, so moving the stick moves none of them.
    private float baseLeftStickX(float width, float height) {
        faceBounds(width, height, stickFaceRect);
        return (width - stickFaceRect.right - buttonInset) + stickInset +
               STICK_RADIUS * layoutU(height);
    }

    // Level with the face cluster's middle.
    private float baseLeftStickY(float width, float height) {
        faceBounds(width, height, stickFaceRect);
        return stickFaceRect.centerY();
    }

    private float leftStickX(float width, float height) {
        return baseLeftStickX(width, height) + ovDx[C_LSTICK] * layoutU(height);
    }

    private float leftStickY(float width, float height) {
        return baseLeftStickY(width, height) + ovDy[C_LSTICK] * layoutU(height);
    }

    private float leftStickRadius(float height) {
        return layoutU(height) * STICK_RADIUS * ovScale[C_LSTICK];
    }

    // The control id the right stick's override is stored under.
    private int rightControl() {
        return twin ? C_RSTICK : C_CSTICK;
    }

    private float baseRightStickX(float width, float height) {
        return width - sideMargin - buttonInset - layoutU(height) * GC_CSTICK_FROM_RIGHT;
    }

    private float rightStickX(float width, float height) {
        return baseRightStickX(width, height) + ovDx[rightControl()] * layoutU(height);
    }

    // Twin's stick is level with the left one, which sits at the face cluster's
    // middle: A's spot, the diamond's centre. The C-stick sits lower, as on the pad.
    private float baseRightStickY(float height) {
        return layoutY(twin ? GC_A_Y : GC_CSTICK_Y, height);
    }

    private float rightStickY(float height) {
        return baseRightStickY(height) + ovDy[rightControl()] * layoutU(height);
    }

    // The default radius in layout units: twin's aiming stick matches the left
    // stick, the C-stick is the GameCube pad's small one.
    private float rightStickBaseRadius() {
        return twin ? STICK_RADIUS : GC_CSTICK_RADIUS;
    }

    private float rightStickRadius(float height) {
        return layoutU(height) * rightStickBaseRadius() * ovScale[rightControl()];
    }

    // Whether (x, y) is in the area that grabs the right stick. By default the
    // right edge is the stick's reach (face buttons are hit-tested first, so it
    // can pass them), the left edge a phone's 0.38 of the width with the reach
    // scaled down on a big screen, and the top 0.43 of the height from the
    // bottom. The moved stick takes the whole area along, scaled by its size, so
    // no grab is left at the old place.
    private boolean inRightStickGrab(float x, float y, float width, float height) {
        final float u = layoutU(height);
        final float scale = ovScale[rightControl()];
        final float baseCy = baseRightStickY(height);
        final float baseRight = baseRightStickX(width, height) + u * rightStickBaseRadius() * 1.6f;
        final float baseLeft = baseRight - (baseRight - width * 0.38f) * layoutScale(height);
        final float baseTop = layoutYFromBottom(0.43f, height);
        final float cy = rightStickY(height);
        final float right = rightStickX(width, height) + rightStickRadius(height) * 1.6f;
        return x >= right - (baseRight - baseLeft) * scale && x < right &&
               y > cy - (baseCy - baseTop) * scale && y <= cy + (height - baseCy) * scale;
    }

    // Fire, Jump, Missile and Turbo face buttons aim when slid (see startButtonAim).
    // Pills and the D-pad don't: their targets carry no start point.
    private static boolean aimsWhileHeld(TouchTarget target) {
        return target.control instanceof ControlButton &&
               (target.id == BTN_SOUTH || target.id == BTN_EAST || target.id == BTN_NORTH ||
                target.id == TURBO_FIRE);
    }

    // A held Fire (charging), Jump, Missile or Turbo that slides past the tap slop aims
    // too, so one thumb can press and aim; the button stays held until the
    // finger lifts.
    private void startButtonAim(int pointerId, TouchTarget target, MotionEvent event, int index) {
        if (Math.hypot(event.getX(index) - target.startX, event.getY(index) - target.startY) <
            dp(MAP_TAP_SLOP_DP)) {
            return;
        }
        target.aiming = true;
        target.x = event.getX(index);
        target.y = event.getY(index);
        aimPointer = pointerId;
        nativeTouchAimDown(true);
    }

    // Sends the finger's travel since the last event, in dp, through every
    // historical sample so a fast swipe stays smooth.
    private void updateAim(TouchTarget target, MotionEvent event, int index) {
        final float density = getResources().getDisplayMetrics().density;
        float lastX = target.x;
        float lastY = target.y;
        final int history = event.getHistorySize();
        for (int h = 0; h <= history; ++h) {
            final float x = h < history ? event.getHistoricalX(index, h) : event.getX(index);
            final float y = h < history ? event.getHistoricalY(index, h) : event.getY(index);
            nativeTouchAim((x - lastX) / density, (y - lastY) / density);
            trackSwipe(target, x / density, y / density,
                       h < history ? event.getHistoricalEventTime(h) : event.getEventTime());
            lastX = x;
            lastY = y;
        }
        target.x = lastX;
        target.y = lastY;
    }

    // A quick swipe up while aiming springs the morph ball (F1 "Spring Ball on
    // swipe up"; the game ignores it out of the ball). A stroke starts where the
    // finger last moved down or stalled; it springs once it has gone SWIPE_UP_DP
    // up, more up than sideways, within SWIPE_UP_MS. Positions are in dp.
    private void trackSwipe(TouchTarget target, float x, float y, long timeMs) {
        if (target.swipeMs < 0 || y >= target.swipeY || timeMs - target.swipeMs > SWIPE_UP_MS) {
            target.swipeX = x;
            target.swipeY = y;
            target.swipeMs = timeMs;
            target.swiped = false;
            return;
        }
        final float up = target.swipeY - y;
        if (!target.swiped && up >= SWIPE_UP_DP && Math.abs(x - target.swipeX) < up) {
            target.swiped = true;
            nativeTouchSpringSwipe();
        }
    }

    private float panViewDp() {
        return getHeight() / getResources().getDisplayMetrics().density;
    }

    // Like updateAim, but the deltas pan the map screen.
    private void updateMapPan(TouchTarget target, MotionEvent event, int index) {
        final float density = getResources().getDisplayMetrics().density;
        final float viewDp = panViewDp();
        float lastX = target.x;
        float lastY = target.y;
        final int history = event.getHistorySize();
        for (int h = 0; h <= history; ++h) {
            final float x = h < history ? event.getHistoricalX(index, h) : event.getX(index);
            final float y = h < history ? event.getHistoricalY(index, h) : event.getY(index);
            nativeMapPan((x - lastX) / density, (y - lastY) / density, viewDp);
            lastX = x;
            lastY = y;
        }
        target.x = lastX;
        target.y = lastY;
    }

    // Two fingers on the map: the midpoint's travel pans, the change in their
    // spread zooms. Uses the latest positions only.
    private void updatePinch(MotionEvent event) {
        final int a = event.findPointerIndex(panPointer);
        final int b = event.findPointerIndex(pan2Pointer);
        final TouchTarget ta = targets.get(panPointer);
        final TouchTarget tb = targets.get(pan2Pointer);
        if (a < 0 || b < 0 || ta == null || tb == null) {
            return;
        }
        final float density = getResources().getDisplayMetrics().density;
        final float ax = event.getX(a);
        final float ay = event.getY(a);
        final float bx = event.getX(b);
        final float by = event.getY(b);
        final float midDx = ((ax + bx) - (ta.x + tb.x)) * 0.5f / density;
        final float midDy = ((ay + by) - (ta.y + tb.y)) * 0.5f / density;
        final float beforeAngle = (float) Math.atan2(tb.y - ta.y, tb.x - ta.x);
        final float before = (float) Math.hypot(ta.x - tb.x, ta.y - tb.y) / density;
        final float now = (float) Math.hypot(ax - bx, ay - by) / density;
        ta.x = ax;
        ta.y = ay;
        tb.x = bx;
        tb.y = by;
        nativeMapPan(midDx, midDy, panViewDp());
        if (before >= MAP_PINCH_MIN_DP && now >= MAP_PINCH_MIN_DP) {
            nativeMapZoom(now / before);
            float turn = (float) Math.atan2(by - ay, bx - ax) - beforeAngle;
            if (turn > Math.PI) {
                turn -= 2f * (float) Math.PI;
            } else if (turn <= -Math.PI) {
                turn += 2f * (float) Math.PI;
            }
            nativeMapRotate(turn);
        }
    }

    private void updateStick(TouchTarget target, float x, float y) {
        target.x = x;
        target.y = y;
        final boolean left = target.type == LEFT_STICK;
        float width = getWidth();
        float height = getHeight();
        // A floating stick keeps the centre it was grabbed at, even if the map
        // opens while it is held.
        final boolean floated = left && leftFloated;
        float centreX = floated ? floatX : left ? leftStickX(width, height) : rightStickX(width, height);
        float centreY = floated ? floatY : left ? leftStickY(width, height) : rightStickY(height);
        float radius = left ? leftStickRadius(height) : rightStickRadius(height);
        // SDL's gamepad axes are +X right and +Y *down* (Aurora inverts Y for the
        // GameCube stick, whose +Y is up), so screen coordinates apply as-is.
        float dx = (x - centreX) / radius;
        float dy = (y - centreY) / radius;
        float length = (float) Math.hypot(dx, dy);
        if (length > 1f) {
            dx /= length;
            dy /= length;
            length = 1f;
        }
        if (length < STICK_DEAD_ZONE) {
            dx = 0f;
            dy = 0f;
        }
        nativeVirtualAxis(left ? AXIS_LEFTX : AXIS_RIGHTX, dx);
        nativeVirtualAxis(left ? AXIS_LEFTY : AXIS_RIGHTY, dy);
    }

    private void releasePointer(int pointerId) {
        TouchTarget target = targets.remove(pointerId);
        if (target == null) {
            return;
        }
        if (target.radialToken != 0) {
            if (target.radialToken > 0) {
                final float reach = dp(72f);
                nativeRadialEnd(target.radialToken, target.radialOwner,
                                (target.x - target.startX) / reach,
                                (target.startY - target.y) / reach);
            }
            if (target.type == WHEEL) wheelPointer = -1;
            return; // never also finishWheel / Scan-tap / release a synthetic fire binding
        }
        if (target.type == HIDE) {
            setHidden(true);
            performClick();
            return;
        }
        if (target.type == EDIT) {
            // Poll now rather than wait an interval; beginEdit releases everything held.
            nativeRequestTouchEdit();
            if (nativeTouchEditRequested()) {
                editPending = true;
            }
            return;
        }
        if (target.type == WHEEL) {
            wheelPointer = -1;
            finishWheel(target);
            return;
        }
        if (target.type == MAP_TAP) {
            // A tap, not a drag that began on the minimap.
            final float slop = dp(MAP_TAP_SLOP_DP);
            if (Math.hypot(target.x - target.startX, target.y - target.startY) < slop) {
                nativeMapTap();
            }
            return;
        }
        releaseTarget(target);
        if (pointerId == leftPointer) {
            leftPointer = -1;
        }
        if (pointerId == rightPointer) {
            rightPointer = -1;
        }
        if (pointerId == aimPointer) {
            aimPointer = -1;
        }
        if (pointerId == pan2Pointer) {
            pan2Pointer = -1;
        } else if (pointerId == panPointer) {
            if (pan2Pointer != -1) {
                // The other finger carries on panning from where it is.
                TouchTarget second = targets.get(pan2Pointer);
                if (second != null) {
                    TouchTarget carried = new TouchTarget(MAP_PAN, 0);
                    carried.x = second.x;
                    carried.y = second.y;
                    targets.put(pan2Pointer, carried);
                }
                panPointer = pan2Pointer;
                pan2Pointer = -1;
            } else {
                panPointer = -1;
                removeCallbacks(panKeepAlive);
            }
        }
    }

    private void releaseTarget(TouchTarget target) {
        if (target.radialToken != 0) {
            if (target.radialToken > 0) nativeRadialCancel(target.radialToken, target.radialOwner);
            if (target.type == WHEEL) wheelPointer = -1;
            return;
        }
        // Nothing to zero for a hide tap, an aim drag (a distance) or a map tap
        // (fired on release only; a cancelled touch is no tap).
        if (target.type == AIM) {
            nativeTouchAimDown(false);
            return;
        }
        if (target.type == WHEEL) {
            wheelPointer = -1;
            return;
        }
        if (target.type == HIDE || target.type == EDIT || target.type == MAP_TAP ||
            target.type == MAP_PAN || target.type == MAP_PAN2) {
            return;
        }
        if (target.type == BUTTON) {
            releaseTouchControl(target);
            if (!target.bound) {
                releaseControl(target.id);
            }
            if (target.aiming) {
                nativeTouchAimDown(false);
            }
            return;
        }
        final boolean left = target.type == LEFT_STICK;
        nativeVirtualAxis(left ? AXIS_LEFTX : AXIS_RIGHTX, 0f);
        nativeVirtualAxis(left ? AXIS_LEFTY : AXIS_RIGHTY, 0f);
    }

    // The native touch code of an editor control: the stock ones are their C_* id.
    private static int touchCode(int control) {
        return control >= C_CUSTOM ? TOUCH_CUSTOM_BASE + control - C_CUSTOM
             : control >= C_BALL ? TOUCH_BALL_BASE + control - C_BALL : control;
    }

    // Presses a pill, face or added button: reports the control, and sends its own
    // button unless a touch: binding takes the control over (MENU always opens F1).
    // An added button has no button of its own: its action is always a binding.
    private void pressTarget(TouchTarget target, int control, boolean remappable) {
        target.touchControl = control;
        if (control >= 0 && control < touchHeld.length && touchHeld[control]++ == 0) {
            nativeTouchControl(touchCode(control), true);
        }
        // A ball button, like an added one, is always a binding.
        target.bound = control >= C_BALL ||
                       (remappable && control >= 0 && nativeTouchControlBound(control));
        if (!target.bound) {
            pressControl(target.id);
        }
    }

    private void releaseTouchControl(TouchTarget target) {
        final int control = target.touchControl;
        target.touchControl = -1;
        if (control >= 0 && control < touchHeld.length && touchHeld[control] > 0 &&
            --touchHeld[control] == 0) {
            nativeTouchControl(touchCode(control), false);
        }
    }

    private boolean turboTouched() {
        for (TouchTarget target : targets.values()) {
            if (target.id == TURBO_FIRE) {
                return true;
            }
        }
        return false;
    }

    private void pressControl(int id) {
        if (id == TOGGLE_DEBUG_OVERLAY) {
            // Drive the overlay directly; polling a synthetic F1 key press is
            // unreliable and depends on SDL having keyboard focus.
            if (!held.containsKey(id)) {
                held.put(id, 1);
                nativeToggleDebugOverlay();
                // The toggle lands on the next game frame; redraw after it so
                // the controls get out of the overlay's way.
                postDelayed(new Runnable() {
                    @Override
                    public void run() {
                        invalidate();
                    }
                }, 150);
            }
            return;
        }
        int count = held.containsKey(id) ? held.get(id) : 0;
        if (count == 0) {
            if (id == BEAM_SHIFT) {
                nativeTouchBeamShift(true);
            } else if (id == TURBO_FIRE) {
                nativeTouchTurboFire(true);
            } else if (id >= AXIS_ID_BASE) {
                nativeVirtualAxis(id - AXIS_ID_BASE, 1f);
            } else {
                nativeVirtualButton(id, true);
            }
        }
        held.put(id, count + 1);
    }

    // Lets go of Turbo when its button is no longer drawn (F1 turned it off while
    // a finger was on it), so it cannot stay on with nothing to release it.
    private void releaseTurbo() {
        if (!held.containsKey(TURBO_FIRE) && !turboTouched()) {
            return;
        }
        final Iterator<TouchTarget> it = targets.values().iterator();
        while (it.hasNext()) {
            final TouchTarget target = it.next();
            if (target.id == TURBO_FIRE) {
                it.remove();
                releaseTouchControl(target);
                // A finger sliding on Turbo was also aiming.
                if (target.aiming) {
                    aimPointer = -1;
                    nativeTouchAimDown(false);
                }
            }
        }
        held.remove(TURBO_FIRE);
        nativeTouchTurboFire(false);
        invalidate();
    }

    private void releaseControl(int id) {
        Integer current = held.get(id);
        if (current == null) {
            return;
        }
        if (id == TOGGLE_DEBUG_OVERLAY) {
            held.remove(id);
            return;
        }
        if (current <= 1) {
            held.remove(id);
            if (id == BEAM_SHIFT) {
                nativeTouchBeamShift(false);
            } else if (id == TURBO_FIRE) {
                nativeTouchTurboFire(false);
            } else if (id >= AXIS_ID_BASE) {
                // Triggers are axes, and SDL's joystick axes run -32768..32767
                // with a trigger resting at the minimum: releasing with 0 left
                // the trigger half pressed, so the game stayed locked on (and
                // strafing) after the player let go.
                nativeVirtualAxis(id - AXIS_ID_BASE, -1f);
            } else {
                nativeVirtualButton(id, false);
            }
        } else {
            held.put(id, current - 1);
        }
    }

    // Hidden, the view stays full screen but draws nothing, and the next
    // touch anywhere brings the controls back (as after a physical pad).
    private void setHidden(boolean hide) {
        releaseAll();
        hidden = hide;
        invalidate();
    }

    // color is the base's RGB, or 0 for the overlay's own.
    private void drawStick(Canvas canvas, float x, float y, float radius, int pointerId,
                           int color) {
        boolean active = pointerId != -1;
        if (color != 0) {
            fillPaint.setColor((active ? 0x88000000 : 0x55000000) | color);
        } else {
            fillPaint.setColor(active ? 0x8848C8E8 : 0x66081218);
        }
        strokePaint.setColor(active ? 0xFFE1F8FF : 0xAAFFFFFF);
        canvas.drawCircle(x, y, radius, fillPaint);
        canvas.drawCircle(x, y, radius, strokePaint);
        float knobX = x;
        float knobY = y;
        TouchTarget target = targets.get(pointerId);
        if (target != null) {
            float dx = target.x - x;
            float dy = target.y - y;
            float distance = (float) Math.hypot(dx, dy);
            float limit = radius * 0.58f;
            if (distance > limit) {
                dx *= limit / distance;
                dy *= limit / distance;
            }
            knobX += dx;
            knobY += dy;
        }
        canvas.drawCircle(knobX, knobY, radius * 0.42f, strokePaint);
    }

    private float centreX(ControlButton button, float width, float height) {
        return button.anchored
                   ? width - sideMargin - buttonInset - (GC_A_FROM_RIGHT - button.x) * layoutU(height)
                   : layoutX(button.x, width, height);
    }

    private float centreY(ControlButton button, float height) {
        return layoutY(button.anchored ? GC_A_Y + button.y : button.y, height);
    }

    private int controlOf(ControlButton button) {
        if (button == turboButton) {
            return twin ? C_TTURBO : C_TURBO;
        }
        for (int i = 0; i < BALL_BUTTONS; ++i) {
            if (BALL_FACE[i] == button || TWIN_BALL_FACE[i] == button) {
                return C_BALL + i;
            }
        }
        final int first = twin ? C_JUMP : C_A;
        for (int i = 0; i < face.length; ++i) {
            if (face[i] == button) {
                return first + i;
            }
        }
        return first;
    }

    // The button's centre, radius (a kidney's is its arc's) and band half width
    // into out: x, y, radius, half. `moved` applies the override, which scales a
    // kidney round its own middle rather than round its arc's centre; draw and hit
    // test both go through here.
    private void faceGeometry(ControlButton button, float width, float height, boolean moved,
                              float[] out) {
        final float u = layoutU(height);
        float x = centreX(button, width, height);
        float y = centreY(button, height);
        float radius = button.radius * u;
        float half = button.halfWidth * u;
        if (moved) {
            final int control = controlOf(button);
            final float scale = ovScale[control];
            if (button.isKidney()) {
                final double middle = Math.toRadians(button.arcStart + button.arcSweep / 2);
                x += radius * (1f - scale) * (float) Math.cos(middle);
                y += radius * (1f - scale) * (float) Math.sin(middle);
            }
            x += ovDx[control] * u;
            y += ovDy[control] * u;
            radius *= scale;
            half *= scale;
        }
        out[0] = x;
        out[1] = y;
        out[2] = radius;
        out[3] = half;
    }

    private final float[] faceGeo = new float[4];

    private boolean hitButton(ControlButton button, float x, float y, float width, float height) {
        faceGeometry(button, width, height, true, faceGeo);
        float dx = x - faceGeo[0];
        float dy = y - faceGeo[1];
        if (!button.isKidney()) {
            float radius = faceGeo[2];
            return dx * dx + dy * dy <= radius * radius;
        }
        // Distance to the nearest point of the kidney's centre arc.
        double angle = Math.toDegrees(Math.atan2(dy, dx));
        double along = ((angle - button.arcStart) % 360 + 360) % 360;
        if (along > button.arcSweep) {
            along = along - button.arcSweep < 360 - along ? button.arcSweep : 0;
        }
        double nearest = Math.toRadians(button.arcStart + along);
        float ring = faceGeo[2];
        float px = dx - ring * (float) Math.cos(nearest);
        float py = dy - ring * (float) Math.sin(nearest);
        float half = faceGeo[3];
        return px * px + py * py <= half * half;
    }

    // The D-pad direction under (x, y), or -1. The whole square round the cross
    // counts, by the dominant axis, so a thumb that slips off an arm's side or
    // into a corner still presses something; only a small centre is dead.
    private int dpadButtonAt(float x, float y, float width, float height) {
        float dx = x - dpadX(width, height);
        float dy = y - dpadY(height);
        float arm = DPAD_ARM * layoutU(height) * ovScale[C_DPAD];
        float dead = DPAD_HALF * layoutU(height) * ovScale[C_DPAD] * 0.4f;
        if (Math.abs(dx) > arm || Math.abs(dy) > arm || dx * dx + dy * dy < dead * dead) {
            return -1;
        }
        if (Math.abs(dx) > Math.abs(dy)) {
            return dx < 0 ? BTN_DPAD_LEFT : BTN_DPAD_RIGHT;
        }
        return dy < 0 ? BTN_DPAD_UP : BTN_DPAD_DOWN;
    }

    private final RectF armRect = new RectF();

    private float dpadX(float width, float height) {
        return layoutX(DPAD_X, width, height) + ovDx[C_DPAD] * layoutU(height);
    }

    private float dpadY(float height) {
        return layoutY(DPAD_Y, height) + ovDy[C_DPAD] * layoutU(height);
    }

    private void drawDpad(Canvas canvas, float width, float height) {
        float cx = dpadX(width, height);
        float cy = dpadY(height);
        float arm = DPAD_ARM * layoutU(height) * ovScale[C_DPAD];
        float half = DPAD_HALF * layoutU(height) * ovScale[C_DPAD];
        float corner = half * 0.35f;
        shapePath.reset();
        shapePath.addRoundRect(cx - arm, cy - half, cx + arm, cy + half, corner, corner,
                               Path.Direction.CW);
        crossArmPath.reset();
        crossArmPath.addRoundRect(cx - half, cy - arm, cx + half, cy + arm, corner, corner,
                                  Path.Direction.CW);
        shapePath.op(crossArmPath, Path.Op.UNION);
        fillPaint.setColor(colored ? padFill(GC_GREY, false) : 0x77081218);
        canvas.drawPath(shapePath, fillPaint);
        // A held arm lights from the centre out.
        for (int i = 0; i < DPAD_BUTTONS.length; ++i) {
            if (!held.containsKey(DPAD_BUTTONS[i])) {
                continue;
            }
            float endX = cx + DPAD_DX[i] * arm;
            float endY = cy + DPAD_DY[i] * arm;
            armRect.set(
                Math.min(cx, endX) - (DPAD_DX[i] == 0 ? half : 0),
                Math.min(cy, endY) - (DPAD_DY[i] == 0 ? half : 0),
                Math.max(cx, endX) + (DPAD_DX[i] == 0 ? half : 0),
                Math.max(cy, endY) + (DPAD_DY[i] == 0 ? half : 0));
            fillPaint.setColor(colored ? padFill(GC_GREY, true) : 0xCC48C8E8);
            canvas.drawRoundRect(armRect, corner, corner, fillPaint);
        }
        strokePaint.setColor(0xBBFFFFFF);
        canvas.drawPath(shapePath, strokePaint);
        for (int i = 0; i < DPAD_BUTTONS.length; ++i) {
            drawCenteredLabel(canvas, DPAD_LABELS[i], cx + DPAD_DX[i] * arm * 0.62f,
                              cy + DPAD_DY[i] * arm * 0.62f, dp(12) * ovScale[C_DPAD]);
        }
    }

    private final RectF arcRect = new RectF();

    // A band round (cx, cy) along the arc, with round ends: GameCube X and Y.
    private void kidneyPath(Path path, float cx, float cy, float ring, float half,
                                   float start, float sweep) {
        float end = start + sweep;
        float endX = cx + ring * (float) Math.cos(Math.toRadians(end));
        float endY = cy + ring * (float) Math.sin(Math.toRadians(end));
        float startX = cx + ring * (float) Math.cos(Math.toRadians(start));
        float startY = cy + ring * (float) Math.sin(Math.toRadians(start));
        path.reset();
        arcRect.set(cx - ring - half, cy - ring - half, cx + ring + half, cy + ring + half);
        path.arcTo(arcRect, start, sweep, true);
        arcRect.set(endX - half, endY - half, endX + half, endY + half);
        path.arcTo(arcRect, end, 180f, false);
        arcRect.set(cx - ring + half, cy - ring + half, cx + ring - half, cy + ring - half);
        path.arcTo(arcRect, end, -sweep, false);
        arcRect.set(startX - half, startY - half, startX + half, startY + half);
        path.arcTo(arcRect, start + 180f, 180f, false);
        path.close();
    }

    // A GameCube colour as a fill: translucent at rest, lighter and more solid
    // while held.
    private static int padFill(int rgb, boolean active) {
        if (!active) {
            return 0x99000000 | rgb;
        }
        int r = (rgb >> 16) & 0xFF;
        int g = (rgb >> 8) & 0xFF;
        int b = rgb & 0xFF;
        r += (255 - r) * 2 / 5;
        g += (255 - g) * 2 / 5;
        b += (255 - b) * 2 / 5;
        return 0xDD000000 | (r << 16) | (g << 8) | b;
    }

    // The GameCube layout's Z pill only opens the map, which a minimap tap does.
    private boolean pillHidden(PillButton pill) {
        return mapTap && pill.axis < 0 && pill.button == BTN_RIGHT_SHOULDER;
    }

    // With drag-to-aim, R's hold-still free look is redundant, so R only shows
    // where it does something else: morphed (Spider Ball), in the pause menu,
    // and on the map screen. Not part of pillHidden, so the pills' layout
    // doesn't jump as R comes and goes. Twin stick always shows it.
    private boolean rHidden() {
        return aim && !twin && (nativeWheelOwned() & WHEEL_MORPHED_BIT) == 0 && !nativePauseScreenOpen() &&
               !nativeMapScreenOpen();
    }

    private boolean pillShown(PillButton pill) {
        if (!controlsOnNow && !editing) {
            return isMenuPill(pill);
        }
        return !pillHidden(pill) && !(pill.axis == AXIS_TRIGGER_R && rHiddenNow) &&
               !ctrlHidden(pillControl(pill));
    }

    private final Path facePath = new Path();
    private final RectF faceRect = new RectF();
    private final RectF faceTmp = new RectF();

    // The face buttons' real bounds, kidney arcs included.
    private void faceBounds(float width, float height, RectF out) {
        final float u = layoutU(height);
        boolean first = true;
        for (ControlButton button : face) {
            final float x = centreX(button, width, height);
            final float y = centreY(button, height);
            final float r = button.radius * u;
            if (button.isKidney()) {
                kidneyPath(facePath, x, y, r, button.halfWidth * u, button.arcStart,
                           button.arcSweep);
                facePath.computeBounds(faceTmp, true);
            } else {
                faceTmp.set(x - r, y - r, x + r, y + r);
            }
            if (first) {
                out.set(faceTmp);
                first = false;
            } else {
                out.union(faceTmp);
            }
        }
    }

    // Past the cap, with no D-pad under them, the top pills drop together until
    // the lowest shown pill (the triggers, or Z) is dp(12) above the stick ring
    // or face cluster.
    private float pillShift(float width, float height) {
        if (layoutU(height) >= height || !wheels) {
            return 0f;
        }
        float bottom = 0f;
        for (PillButton pill : pills) {
            if (!pillHidden(pill) && cornerSlot(pill) < 0) {
                bottom = Math.max(bottom, layoutY(pill.bottom, height));
            }
        }
        faceBounds(width, height, faceRect);
        final float stickTop = baseLeftStickY(width, height) - STICK_RADIUS * layoutU(height);
        return Math.max(0f, Math.min(stickTop, faceRect.top) - dp(12) - bottom);
    }

    // START (slot 0) and MENU (slot 1) sit side by side in the bottom-left
    // corner; -1 for the pills placed from their rects. The triggers are axes
    // with button -1, which is also TOGGLE_DEBUG_OVERLAY, hence the axis check.
    private static int cornerSlot(PillButton pill) {
        if (pill.axis >= 0) {
            return -1;
        }
        return pill.button == BTN_START ? 0 : pill.button == TOGGLE_DEBUG_OVERLAY ? 1 : -1;
    }

    // A pill's rect, from its side edge and the top of the layout band, or its
    // corner slot: round buttons mirroring the map button by the eye.
    private void pillRect(PillButton pill, float width, float height, RectF out) {
        final int slot = cornerSlot(pill);
        if (slot >= 0) {
            bottomButtonRect(slot, false, width, height, out);
            return;
        }
        final float shift = pillShift(width, height);
        out.set(layoutX(pill.left, width, height), layoutY(pill.top, height) + shift,
                layoutX(pill.right, width, height), layoutY(pill.bottom, height) + shift);
        applyOverride(pillControl(pill), out, height);
    }

    private static int pillControl(PillButton pill) {
        final int slot = cornerSlot(pill);
        if (slot >= 0) {
            return slot == 0 ? C_START : C_MENU;
        }
        if (pill.control >= 0) {
            return pill.control;
        }
        if (pill.axis == AXIS_TRIGGER_L) {
            return C_L;
        }
        if (pill.axis == AXIS_TRIGGER_R) {
            return C_R;
        }
        return pill.button == BTN_RIGHT_SHOULDER ? C_Z : C_L;
    }

    // Whether a finger is down on this control itself.
    private boolean pressedControl(Object control) {
        for (TouchTarget target : targets.values()) {
            if (target.control == control) {
                return true;
            }
        }
        return false;
    }

    private void drawPillButton(Canvas canvas, PillButton pill, float width, float height) {
        pillRect(pill, width, height, pillHit);
        boolean active = pressedControl(pill);
        if (colored && pill.color != 0) {
            fillPaint.setColor(padFill(pill.color, active));
        } else {
            fillPaint.setColor(active ? 0xCC48C8E8 : 0x99081218);
        }
        strokePaint.setColor(active ? 0xFFE1F8FF : 0xCCFFFFFF);
        final int slot = cornerSlot(pill);
        if (slot >= 0) {
            drawCornerButton(canvas, slot, pillHit);
            return;
        }
        final RectF bounds = pillHit;
        float radius = Math.min(bounds.width(), bounds.height()) * 0.28f;
        canvas.drawRoundRect(bounds, radius, radius, fillPaint);
        canvas.drawRoundRect(bounds, radius, radius, strokePaint);
        if (twin && pill.axis < 0 && pill.button == BTN_RIGHT_SHOULDER) {
            // The Map pill wears Xbox's Menu glyph (three lines), as the hint does.
            final float h = bounds.height();
            final float gx = labels ? bounds.left + bounds.width() * 0.26f : bounds.centerX();
            final float lineW = h * 0.34f;
            final float lineH = Math.max(1.5f, h * 0.06f);
            final int saved = fillPaint.getColor();
            fillPaint.setColor(strokePaint.getColor());
            for (int i = -1; i <= 1; i++) {
                final float ly = bounds.centerY() + i * h * 0.16f;
                canvas.drawRect(gx - lineW / 2f, ly - lineH / 2f, gx + lineW / 2f,
                                ly + lineH / 2f, fillPaint);
            }
            fillPaint.setColor(saved);
            if (labels) {
                drawCenteredLabel(canvas, pill.label, bounds.left + bounds.width() * 0.62f,
                                  bounds.centerY(), dp(11));
            }
            return;
        }
        // "LB Jump": without descriptions, just the button's name.
        final String label = labels ? pillLabel(pill) : pill.label.split(" ")[0];
        drawCenteredLabel(canvas, label, bounds.centerX(), bounds.centerY(),
                          label.length() > 2 ? dp(11) : dp(13));
    }

    // Which functions the buttons wear. The map screen comes first: the pause
    // menu's map is one too. Morphed (or morphing) is only in play: paused, the
    // wheel state reads 0. The editor always shows the usual names.
    private int labelContext() {
        if (editing) {
            return LABELS_PLAY;
        }
        if (nativeMapScreenOpen()) {
            return LABELS_MAP;
        }
        if (nativePauseScreenOpen()) {
            return LABELS_PAUSE;
        }
        if (nativeMessageBoxOpen()) {
            return LABELS_DIALOG;
        }
        return (nativeWheelOwned() & WHEEL_MORPHED_BIT) != 0 ? LABELS_MORPH : LABELS_PLAY;
    }

    // Whether play wants the Morph Ball page: morphed, outside the map, pause menu
    // and message boxes (their face buttons do menu things).
    private boolean inBall() {
        return !nativeMapScreenOpen() && !nativePauseScreenOpen() && !nativeMessageBoxOpen() &&
               (nativeWheelOwned() & WHEEL_MORPHED_BIT) != 0;
    }

    // Whether a control belongs to the page up now. The stick, START, MENU, the
    // map (pill, button and minimap) and the eye are on both.
    private boolean onPage(int control) {
        switch (control) {
        case C_LSTICK: case C_START: case C_MENU: case C_Z: case C_TZ: case C_MAP: case C_EYE:
            return true;
        default:
            break;
        }
        if (control >= C_CUSTOM) {
            final int page = customPage[control - C_CUSTOM];
            return page == PAGE_BOTH || (page == PAGE_BALL) == ballPage;
        }
        return (control >= C_BALL && control < C_BALL + BALL_BUTTONS) == ballPage;
    }

    // A shoulder pill's text for labelContextNow. In a menu, a pill that does
    // nothing there shows just its button's name.
    private String pillLabel(PillButton pill) {
        if (labelContextNow == LABELS_PLAY || isMenuPill(pill)) {
            return pill.label;
        }
        final String name = pill.label.split(" ")[0];
        final int sent = pill.axis >= 0 ? -1 : pill.button;
        switch (labelContextNow) {
        case LABELS_MORPH:
            // Only the map, START and MENU stay up in the ball.
            return pill.label;
        case LABELS_PAUSE:
            // L and R page through the subscreens.
            if (pill.axis == AXIS_TRIGGER_L) {
                return name + " Prev";
            }
            if (pill.axis == AXIS_TRIGGER_R) {
                return name + " Next";
            }
            if (sent == BTN_EAST) {
                return name + " Back";
            }
            if (sent == BTN_SOUTH) {
                return name + " Select";
            }
            if (sent == BTN_NORTH) {
                return name + " View";
            }
            return name;
        case LABELS_MAP:
            // L zooms out and R in; B and Z leave.
            if (pill.axis == AXIS_TRIGGER_L) {
                return name + " Out";
            }
            if (pill.axis == AXIS_TRIGGER_R) {
                return name + " In";
            }
            if (sent == BTN_EAST || sent == BTN_RIGHT_SHOULDER) {
                return name + " Exit";
            }
            if (sent == BTN_SOUTH) {
                return name + " World";
            }
            if (sent == BTN_NORTH) {
                return name + " Legend";
            }
            return name;
        default:
            return sent == BTN_SOUTH ? name + " OK" : name;
        }
    }

    // START and MENU, whose labels name no game function.
    private static boolean isMenuPill(PillButton pill) {
        return pill.axis < 0 && (pill.button == BTN_START || pill.button == TOGGLE_DEBUG_OVERLAY);
    }

    // A face button's function for labelContextNow (the ball hides them).
    private String faceFunction(ControlButton button) {
        if (twin) {
            final int i = controlOf(button) - C_JUMP;
            switch (labelContextNow) {
            case LABELS_PAUSE: return TWIN_PAUSE_FUNCTIONS[i];
            case LABELS_MAP: return TWIN_MAP_FUNCTIONS[i];
            case LABELS_DIALOG: return TWIN_DIALOG_FUNCTIONS[i];
            default: return button.label;
            }
        }
        final int i = controlOf(button) - C_A;
        switch (labelContextNow) {
        case LABELS_PAUSE: return GAMECUBE_PAUSE_FUNCTIONS[i];
        case LABELS_MAP: return GAMECUBE_MAP_FUNCTIONS[i];
        case LABELS_DIALOG: return GAMECUBE_DIALOG_FUNCTIONS[i];
        default: return GAMECUBE_FUNCTIONS[i];
        }
    }

    private void drawButton(Canvas canvas, ControlButton button, float width, float height) {
        faceGeometry(button, width, height, true, faceGeo);
        float x = faceGeo[0];
        float y = faceGeo[1];
        float radius = faceGeo[2];
        final float scale = ovScale[controlOf(button)];
        boolean active = pressedControl(button);
        if (colored && button.color != 0) {
            fillPaint.setColor(padFill(button.color, active));
        } else {
            fillPaint.setColor(active ? 0xCC48C8E8 : 0x77081218);
        }
        strokePaint.setColor(active ? 0xFFE1F8FF : 0xBBFFFFFF);
        if (button.isKidney()) {
            kidneyPath(shapePath, x, y, radius, faceGeo[3], button.arcStart, button.arcSweep);
            canvas.drawPath(shapePath, fillPaint);
            canvas.drawPath(shapePath, strokePaint);
            double middle = Math.toRadians(button.arcStart + button.arcSweep / 2);
            x += radius * (float) Math.cos(middle);
            y += radius * (float) Math.sin(middle);
        } else {
            canvas.drawCircle(x, y, radius, fillPaint);
            canvas.drawCircle(x, y, radius, strokePaint);
        }
        final int control = controlOf(button);
        if (control >= C_BALL && control < C_BALL + BALL_BUTTONS) {
            // A ball button wears its action's name: the row its touch binding does.
            final String row = nativeTouchBallLabel(touchCode(control));
            final String name = row != null && !row.isEmpty() ? row : button.label;
            drawCenteredLabel(canvas, name, x, y, (name.length() > 5 ? dp(10) : dp(12)) * scale);
            return;
        }
        // The letter big (twin: the Xbox one), the function small under it.
        final boolean isTurbo = button == turboButton;
        final String letter = isTurbo ? "T"
                              : twin ? TWIN_LETTERS[controlOf(button) - C_JUMP] : button.label;
        final float letterSize = (twin ? dp(17) : dp(15)) * scale;
        if (!labels) {
            drawCenteredLabel(canvas, letter, x, y, letterSize);
            return;
        }
        final String function = isTurbo ? button.label : faceFunction(button);
        // The function goes below the letter. A kidney is sized by its half width,
        // and a mostly level one (Y) has its text slanted along the band: upright,
        // "Missile" ran into the border of Y's tilted band.
        float size = radius;
        float slant = 0f;
        if (button.isKidney()) {
            size = faceGeo[3] * 1.4f;
            float tangent = button.arcStart + button.arcSweep / 2 + 90f;
            tangent = ((tangent % 180f) + 270f) % 180f - 90f; // into [-90, 90)
            if (Math.abs(tangent) <= 45f) {
                slant = tangent;
            }
        }
        canvas.save();
        canvas.rotate(slant, x, y);
        drawCenteredLabel(canvas, letter, x, y - size * 0.2f, letterSize);
        drawCenteredLabel(canvas, function, x, y + size * 0.52f, dp(8) * scale);
        canvas.restore();
    }

    // The hide button: an eye.
    // START as a pause glyph, MENU (the F1 overlay) as a cog, in a circle like the
    // map button's. The paints are already set for the held state.
    private void drawCornerButton(Canvas canvas, int slot, RectF bounds) {
        final float cx = bounds.centerX();
        final float cy = bounds.centerY();
        final float radius = bounds.width() * 0.5f;
        canvas.drawCircle(cx, cy, radius, fillPaint);
        canvas.drawCircle(cx, cy, radius, strokePaint);
        final float s = radius * 0.36f;
        if (slot == 0) {
            final int fill = fillPaint.getColor();
            fillPaint.setColor(strokePaint.getColor());
            final float barW = s * 0.55f;
            final float corner = barW * 0.3f;
            canvas.drawRoundRect(cx - s * 0.75f - barW / 2f, cy - s, cx - s * 0.75f + barW / 2f,
                                 cy + s, corner, corner, fillPaint);
            canvas.drawRoundRect(cx + s * 0.75f - barW / 2f, cy - s, cx + s * 0.75f + barW / 2f,
                                 cy + s, corner, corner, fillPaint);
            fillPaint.setColor(fill);
        } else {
            // A cog: eight square teeth around a ring, with a hole.
            final int teeth = 8;
            final float outer = s * 1.3f;
            final float inner = s * 0.98f;
            final double step = 2.0 * Math.PI / teeth;
            final double half = step * 0.22;
            shapePath.reset();
            for (int i = 0; i < teeth; i++) {
                final double a = i * step;
                final double[] angles = {a - step / 2 + half, a - half, a - half * 0.8,
                                         a + half * 0.8, a + half, a + step / 2 - half};
                final float[] radii = {inner, inner, outer, outer, inner, inner};
                for (int k = 0; k < angles.length; k++) {
                    final float px = cx + radii[k] * (float) Math.cos(angles[k]);
                    final float py = cy + radii[k] * (float) Math.sin(angles[k]);
                    if (i == 0 && k == 0) {
                        shapePath.moveTo(px, py);
                    } else {
                        shapePath.lineTo(px, py);
                    }
                }
            }
            shapePath.close();
            canvas.drawPath(shapePath, strokePaint);
            canvas.drawCircle(cx, cy, s * 0.42f, strokePaint);
        }
    }

    private void drawEye(Canvas canvas, RectF bounds) {
        fillPaint.setColor(0x99081218);
        strokePaint.setColor(0xCCFFFFFF);
        float cx = bounds.centerX();
        float cy = bounds.centerY();
        float r = bounds.width() * 0.5f;
        canvas.drawCircle(cx, cy, r, fillPaint);
        canvas.drawCircle(cx, cy, r, strokePaint);
        float halfW = r * 0.62f;
        float lid = r * 0.36f;
        Path eye = shapePath;
        eye.reset();
        eye.moveTo(cx - halfW, cy);
        eye.quadTo(cx, cy - lid * 2f, cx + halfW, cy);
        eye.quadTo(cx, cy + lid * 2f, cx - halfW, cy);
        eye.close();
        canvas.drawPath(eye, strokePaint);
        fillPaint.setColor(0xCCFFFFFF);
        canvas.drawCircle(cx, cy, lid * 0.55f, fillPaint);
    }

    // The Visor (left) and Beam (right) buttons sit near the bottom, each a wheel
    // radius up from the edge so its wheel opens on screen. Visor sits just
    // outside the left stick ring, Beam just outside the face cluster (or, in
    // classic, the C-stick ring), where the thumbs already are.
    private float wheelButtonX(int wheel, float width, float height) {
        final float edge = dp(WHEEL_BUTTON_ANCHOR_DP) + WHEEL_BUTTON_RADIUS * layoutU(height);
        float x;
        if (wheel == 0) {
            x = baseLeftStickX(width, height) + STICK_RADIUS * layoutU(height) + edge;
        } else if (cStick) {
            x = baseRightStickX(width, height) - layoutU(height) * rightStickBaseRadius() - edge;
        } else {
            faceBounds(width, height, faceRect);
            x = faceRect.left - edge;
        }
        // The open wheel must stay on screen.
        final float reach = dp(WHEEL_RADIUS_DP);
        return Math.max(reach, Math.min(width - reach, x)) +
               ovDx[C_VISOR + wheel] * layoutU(height);
    }

    private float wheelButtonY(int wheel, float height) {
        return height - dp(WHEEL_RADIUS_DP) - dp(WHEEL_BUTTON_GAP_DP) +
               ovDy[C_VISOR + wheel] * layoutU(height);
    }

    private float wheelButtonRadius(int wheel, float height) {
        return WHEEL_BUTTON_RADIUS * layoutU(height) * ovScale[C_VISOR + wheel];
    }

    private int wheelButtonAt(float x, float y, float width, float height) {
        for (int wheel = 0; wheel < 2; ++wheel) {
            if (ctrlHidden(C_VISOR + wheel)) {
                continue; // the touch falls through, wheel or no wheel
            }
            final float radius = wheelButtonRadius(wheel, height) * 1.1f;
            final double dx = x - wheelButtonX(wheel, width, height);
            final double dy = y - wheelButtonY(wheel, height);
            if (dx * dx + dy * dy <= radius * radius) {
                return wheel;
            }
        }
        return -1;
    }

    // The game's own icons, decoded by the native side once the HUD has loaded them and
    // traced into outlines (they are 32x32, too coarse to scale up). Until one arrives (or
    // when it never does) the text label is drawn.
    private final Path[][] wheelIcons = new Path[2][4];
    private final int[][][] wheelIconSize = new int[2][4][2];
    private final Paint iconPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private long wheelIconTryMs = -WHEEL_ICON_RETRY_MS;

    private void refreshWheelIcons() {
        final long now = SystemClock.uptimeMillis();
        if (now - wheelIconTryMs < WHEEL_ICON_RETRY_MS) {
            return;
        }
        wheelIconTryMs = now;
        for (int wheel = 0; wheel < 2; ++wheel) {
            for (int item = 0; item < 4; ++item) {
                if (wheelIcons[wheel][item] != null) {
                    continue;
                }
                final int[] raw = nativeWheelIcon(wheel, item);
                if (raw == null || raw.length < 3) {
                    continue;
                }
                final int w = raw[0];
                final int h = raw[1];
                if (w <= 0 || h <= 0 || raw.length != 2 + w * h) {
                    continue;
                }
                wheelIcons[wheel][item] = IconTracer.trace(raw, 2, w, h);
                wheelIconSize[wheel][item][0] = w;
                wheelIconSize[wheel][item][1] = h;
            }
        }
    }

    // Draws the icon fitted into a size x size box at (cx, cy); false when it has none yet.
    private boolean drawWheelIcon(Canvas canvas, int wheel, int item, float cx, float cy, float size,
                                  int alpha) {
        final Path icon = wheelIcons[wheel][item];
        if (icon == null) {
            return false;
        }
        final int w = wheelIconSize[wheel][item][0];
        final int h = wheelIconSize[wheel][item][1];
        final float scale = size / Math.max(w, h);
        canvas.save();
        canvas.translate(cx - w * scale / 2f, cy - h * scale / 2f);
        canvas.scale(scale, scale);
        iconPaint.setColor(wheel == 1 ? BEAM_ICON_COLORS[item] : Color.WHITE);
        iconPaint.setAlpha(alpha);
        canvas.drawPath(icon, iconPaint);
        canvas.restore();
        return true;
    }

    // An added button's centre and radius into out (x, y, radius): round the
    // screen's centre, moved and scaled by its override like every control.
    private void customGeometry(int control, float width, float height, float[] out) {
        final float u = layoutU(height);
        out[0] = width / 2f + ovDx[control] * u;
        out[1] = height / 2f + ovDy[control] * u;
        out[2] = CUSTOM_RADIUS * u * ovScale[control];
    }

    // Round buttons with the game's icon for a beam or visor, else a short label.
    private void drawCustomButtons(Canvas canvas, float width, float height) {
        for (int control = C_CUSTOM; control < ALL_CONTROLS; ++control) {
            final String key = customAction[control - C_CUSTOM];
            if (key == null) {
                continue;
            }
            customGeometry(control, width, height, faceGeo);
            final float x = faceGeo[0];
            final float y = faceGeo[1];
            final float radius = faceGeo[2];
            layerRect.set(x - radius, y - radius, x + radius, y + radius);
            final int layer = pushLayer(canvas, control, layerRect);
            if (layer == LAYER_SKIP) {
                continue;
            }
            final boolean active = touchHeld[control] > 0;
            fillPaint.setColor(active ? 0xCC48C8E8 : 0x77081218);
            strokePaint.setColor(active ? 0xFFE1F8FF : 0xBBFFFFFF);
            canvas.drawCircle(x, y, radius, fillPaint);
            canvas.drawCircle(x, y, radius, strokePaint);
            final int icon = actionIcon(key);
            if (icon >= 0) {
                refreshWheelIcons();
            }
            if (icon < 0 || !drawWheelIcon(canvas, icon / 4, icon % 4, x, y, radius * 1.3f, 255)) {
                drawFittedLabel(canvas, shortLabel(key), x, y, dp(14) * ovScale[control],
                                radius * 1.7f);
            }
            popLayer(canvas, layer);
        }
    }

    // A beam or visor action's wheel icon as wheel * 4 + item (nativeWheelIcon's
    // numbering), or -1.
    private static int actionIcon(String key) {
        switch (key) {
            case "visor_combat": return 0;
            case "visor_xray": return 1;
            case "visor_scan": return 2;
            case "visor_thermal": return 3;
            case "beam_power": return 4;
            case "beam_ice": return 5;
            case "beam_wave": return 6;
            case "beam_plasma": return 7;
            default: return -1;
        }
    }

    // What an added button says: its action's label, shortened to fit a button.
    private static String shortLabel(String key) {
        switch (key) {
            case "dpad_up": return "D-pad ↑";
            case "dpad_down": return "D-pad ↓";
            case "dpad_left": return "D-pad ←";
            case "dpad_right": return "D-pad →";
            case "beam_next": return "Beam +";
            case "beam_prev": return "Beam −";
            case "visor_next": return "Visor +";
            case "visor_prev": return "Visor −";
            case "spring_ball": return "Spring";
            case "port_menu": return "Menu";
            case "save_state": return "Save";
            case "load_state": return "Load";
            case "toggle_original": return "Original";
            case "photo_mode": return "Photo";
            case "gyro_aim": return "Gyro";
            case "ball_power_bomb": return "P.Bomb";
            case "ball_boost": return "Boost";
            case "ball_spider": return "Spider";
            default: break;
        }
        String label = actionLabel(key);
        if (label == null) {
            return key;
        }
        final int paren = label.indexOf(" (");
        if (paren > 0) {
            label = label.substring(0, paren); // "L (click)"
        }
        if (label.endsWith(" Beam") || label.endsWith(" Visor")) {
            label = label.substring(0, label.lastIndexOf(' '));
        }
        return label;
    }

    // A label at `size`, or smaller when it is wider than maxWidth.
    private void drawFittedLabel(Canvas canvas, String label, float x, float y, float size,
                                 float maxWidth) {
        textPaint.setTextSize(size);
        final float w = textPaint.measureText(label);
        drawCenteredLabel(canvas, label, x, y, w > maxWidth ? size * maxWidth / w : size);
    }

    // nativeTouchButtonActions(), read once.
    private static String[] buttonActions;

    private static String[] buttonActions() {
        if (buttonActions == null) {
            final String[] list = nativeTouchButtonActions();
            buttonActions = list != null ? list : new String[0];
        }
        return buttonActions;
    }

    // An action's label, or null when it is not one an added button can take
    // (a newer version's, or a hand-edited layout's).
    private static String actionLabel(String key) {
        final String[] list = buttonActions();
        for (int i = 0; i + 1 < list.length; i += 2) {
            if (list[i].equals(key)) {
                return list[i + 1];
            }
        }
        return null;
    }

    private static String[] controlIds(String... stock) {
        final String[] ids = Arrays.copyOf(stock, ALL_CONTROLS);
        for (int slot = 0; slot < CUSTOM_SLOTS; ++slot) {
            ids[C_CUSTOM + slot] = "k" + slot;
        }
        return ids;
    }

    private void drawWheelButtons(Canvas canvas, float width, float height) {
        final int mask = nativeWheelOwned();
        wheelButtonsShown = (mask & WHEEL_VALID_BIT) != 0 || editing;
        if (!wheelButtonsShown) {
            return;
        }
        refreshWheelIcons();
        for (int wheel = 0; wheel < 2; ++wheel) {
            final float cx = wheelButtonX(wheel, width, height);
            final float cy = wheelButtonY(wheel, height);
            final float radius = wheelButtonRadius(wheel, height);
            layerRect.set(cx - radius, cy - radius, cx + radius, cy + radius);
            final int layer = pushLayer(canvas, C_VISOR + wheel, layerRect);
            if (layer == LAYER_SKIP) {
                continue;
            }
            final boolean active = wheelPointer != -1 && wheelTarget() != null &&
                                   wheelTarget().id == wheel;
            fillPaint.setColor(active ? 0xCC48C8E8 : 0x77081218);
            strokePaint.setColor(active ? 0xFFE1F8FF : 0xBBFFFFFF);
            canvas.drawCircle(cx, cy, radius, fillPaint);
            canvas.drawCircle(cx, cy, radius, strokePaint);
            final int current = wheel == 0 ? (mask >> 8) & 3 : (mask >> 10) & 3;
            if (!drawWheelIcon(canvas, wheel, current, cx, cy, radius * 1.4f, 255)) {
                drawCenteredLabel(canvas, WHEEL_BUTTON_LABELS[wheel], cx, cy,
                                  dp(13) * ovScale[C_VISOR + wheel]);
            }
            popLayer(canvas, layer);
        }
    }

    private TouchTarget wheelTarget() {
        return wheelPointer == -1 ? null : targets.get(wheelPointer);
    }

    // The sector under (x, y) of the open wheel, or -1 inside the centre.
    private int wheelSector(float x, float y) {
        final float dx = x - wheelCx;
        final float dy = y - wheelCy;
        final float dead = dp(WHEEL_DEAD_DP);
        if (dx * dx + dy * dy < dead * dead) {
            return -1;
        }
        if (Math.abs(dx) > Math.abs(dy)) {
            return dx > 0 ? 1 : 3;
        }
        return dy < 0 ? 0 : 2;
    }

    private static boolean wheelOwned(int mask, int wheel, int item) {
        return (mask & (1 << (wheel * 4 + item))) != 0;
    }

    private final RectF wheelOval = new RectF();
    private final RectF wheelHole = new RectF();

    // The wheel opens over everything: four sectors, the one under the finger lit.
    private void drawWheel(Canvas canvas) {
        final TouchTarget target = wheelTarget();
        if (target == null || target.radialToken != 0) {
            return;
        }
        final int mask = nativeWheelOwned();
        final int sector = wheelSector(target.x, target.y);
        final float outer = dp(WHEEL_RADIUS_DP);
        final float inner = dp(WHEEL_DEAD_DP);
        wheelOval.set(wheelCx - outer, wheelCy - outer, wheelCx + outer, wheelCy + outer);
        wheelHole.set(wheelCx - inner, wheelCy - inner, wheelCx + inner, wheelCy + inner);
        final int currentIndex = target.id == 0 ? (mask >> 8) & 3 : (mask >> 10) & 3;
        for (int i = 0; i < 4; ++i) {
            final int item = WHEEL_ITEMS[target.id][i];
            final boolean owned = wheelOwned(mask, target.id, item);
            // Sector i is centred on up (-90), right (0), down (90), left (180).
            final float start = -135f + 90f * i;
            shapePath.reset();
            shapePath.arcTo(wheelOval, start, 90f, true);
            shapePath.arcTo(wheelHole, start + 90f, -90f, false);
            shapePath.close();
            final boolean lit = i == sector && owned;
            fillPaint.setColor(lit ? 0xDD48C8E8 : owned ? 0xAA081218 : 0x66081218);
            canvas.drawPath(shapePath, fillPaint);
            strokePaint.setColor(item == currentIndex ? 0xFFE0C020 : 0xBBFFFFFF);
            canvas.drawPath(shapePath, strokePaint);
            // An item not found yet leaves its sector empty.
            if (!owned) {
                continue;
            }
            final double mid = Math.toRadians(start + 45f);
            final float labelR = (outer + inner) / 2f;
            final float lx = wheelCx + labelR * (float) Math.cos(mid);
            final float ly = wheelCy + labelR * (float) Math.sin(mid);
            if (!drawWheelIcon(canvas, target.id, item, lx, ly, dp(WHEEL_ICON_DP), 255)) {
                drawCenteredLabel(canvas, WHEEL_LABELS[target.id][i], lx, ly, dp(12));
            }
        }
    }

    // A finger lifted off a wheel: a slide to an owned sector picks it; a quick
    // tap on Visor picks Scan when the setting is on; the centre cancels.
    private void finishWheel(TouchTarget target) {
        final int mask = nativeWheelOwned();
        if ((mask & WHEEL_VALID_BIT) == 0) {
            return;
        }
        final int sector = wheelSector(target.x, target.y);
        if (sector >= 0) {
            final int item = WHEEL_ITEMS[target.id][sector];
            if (wheelOwned(mask, target.id, item)) {
                if (target.id == 0) {
                    nativeRequestVisor(item);
                } else {
                    nativeRequestBeam(item);
                }
            }
            return;
        }
        final boolean quick = SystemClock.uptimeMillis() - target.startMs < WHEEL_TAP_MS;
        final boolean still = Math.hypot(target.x - target.startX, target.y - target.startY) <
                              dp(MAP_TAP_SLOP_DP);
        if (target.id == 0 && visorTapScan && quick && still && wheelOwned(mask, 0, 2)) {
            nativeRequestVisor(2);
        }
    }

    private void drawCenteredLabel(Canvas canvas, String label, float x, float y, float size) {
        textPaint.setTextSize(size);
        canvas.drawText(label, x, y - (textPaint.ascent() + textPaint.descent()) / 2, textPaint);
    }

    // The layout is stored natively (the F1 config), so a reset there, or the
    // editor's save, reaches the next draw. Re-parsed only when the text changes.
    private void syncLayout() {
        final String text = nativeTouchLayout();
        if (text != null && !text.equals(layoutText)) {
            nativeRadialCancelAll();
            layoutText = text;
            loadLayout(text);
        }
    }

    // `id:dx,dy,scale[,opacity[,hidden]];...` (opacity in percent, hidden 0 or 1),
    // and for an added button `k<slot>:dx,dy,scale,opacity,hidden,<action key>`
    // (always all six; the native side binds the action, PortInput::BindTouchButtons).
    // Unknown ids and actions and malformed entries are skipped, so a layout from
    // a newer version still loads what this one knows.
    private void loadLayout(String text) {
        Arrays.fill(ovDx, 0f);
        Arrays.fill(ovDy, 0f);
        Arrays.fill(ovScale, 1f);
        Arrays.fill(ovAlpha, 100);
        Arrays.fill(ovHidden, false);
        Arrays.fill(ovRemoved, false);
        Arrays.fill(customAction, null);
        Arrays.fill(customPage, PAGE_PLAY);
        for (String entry : text.split(";")) {
            final int colon = entry.indexOf(':');
            if (colon <= 0) {
                continue;
            }
            final String id = entry.substring(0, colon).trim();
            int control = -1;
            for (int i = 0; i < ALL_CONTROLS; ++i) {
                if (CONTROL_IDS[i].equals(id)) {
                    control = i;
                    break;
                }
            }
            final String[] parts = entry.substring(colon + 1).split(",");
            final boolean custom = control >= C_CUSTOM;
            if (control < 0 || (custom ? parts.length < 6 || parts.length > 7 : parts.length < 3 || parts.length > 5)) {
                continue;
            }
            final String key = custom ? parts[5].trim() : null;
            if (custom && actionLabel(key) == null) {
                continue;
            }
            try {
                final float dx = Float.parseFloat(parts[0].trim());
                final float dy = Float.parseFloat(parts[1].trim());
                final float scale = Float.parseFloat(parts[2].trim());
                if (Float.isNaN(dx) || Float.isInfinite(dx) || Float.isNaN(dy) ||
                    Float.isInfinite(dy) || Float.isNaN(scale) || Float.isInfinite(scale)) {
                    continue;
                }
                float alpha = 100f;
                boolean off = false;
                boolean gone = false;
                if (parts.length > 3) {
                    alpha = Float.parseFloat(parts[3].trim());
                    if (Float.isNaN(alpha) || Float.isInfinite(alpha)) {
                        continue;
                    }
                }
                if (parts.length > 4) {
                    final float flag = Float.parseFloat(parts[4].trim());
                    off = flag != 0f;
                    gone = flag == 2f && !custom; // an added button is deleted outright
                }
                ovDx[control] = Math.max(-MAX_OFFSET, Math.min(MAX_OFFSET, dx));
                ovDy[control] = Math.max(-MAX_OFFSET, Math.min(MAX_OFFSET, dy));
                ovScale[control] = Math.max(MIN_SCALE, Math.min(MAX_SCALE, scale));
                ovAlpha[control] = Math.max(0, Math.min(100, Math.round(alpha)));
                ovHidden[control] = off;
                if (gone) {
                    ovRemoved[control] = true;
                }
                if (custom) {
                    customAction[control - C_CUSTOM] = key;
                    if (parts.length > 6) {
                        final int page = Integer.parseInt(parts[6].trim());
                        customPage[control - C_CUSTOM] =
                            page >= PAGE_PLAY && page <= PAGE_BOTH ? page : PAGE_PLAY;
                    }
                }
                if (pinned(control)) {
                    // A hand-edited layout can't lock the only way into F1 (or the editor) away.
                    ovAlpha[control] = Math.max(MENU_MIN_ALPHA, ovAlpha[control]);
                    ovHidden[control] = false;
                    ovRemoved[control] = false;
                }
            } catch (NumberFormatException e) {
                // A malformed entry: leave the control at its default.
            }
        }
    }

    // Only the controls that were changed, so a default layout is the empty string.
    private String serializeLayout() {
        final StringBuilder out = new StringBuilder();
        for (int i = 0; i < CONTROLS; ++i) {
            if (ovDx[i] == 0f && ovDy[i] == 0f && ovScale[i] == 1f && ovAlpha[i] == 100 &&
                !ovHidden[i] && !ovRemoved[i]) {
                continue;
            }
            if (out.length() > 0) {
                out.append(';');
            }
            out.append(String.format(Locale.ROOT, "%s:%.4f,%.4f,%.3f", CONTROL_IDS[i], ovDx[i],
                                     ovDy[i], ovScale[i]));
            // Only as many fields as needed, so older layouts serialize as before.
            if (ovRemoved[i]) {
                out.append(',').append(ovAlpha[i]).append(",2");
            } else if (ovHidden[i]) {
                out.append(',').append(ovAlpha[i]).append(",1");
            } else if (ovAlpha[i] != 100) {
                out.append(',').append(ovAlpha[i]);
            }
        }
        // The added buttons, after the stock controls, so a layout without any is
        // what it was before they existed.
        for (int slot = 0; slot < CUSTOM_SLOTS; ++slot) {
            final String key = customAction[slot];
            if (key == null) {
                continue;
            }
            final int i = C_CUSTOM + slot;
            if (out.length() > 0) {
                out.append(';');
            }
            out.append(String.format(Locale.ROOT, "%s:%.4f,%.4f,%.3f,%d,%d,%s", CONTROL_IDS[i],
                                     ovDx[i], ovDy[i], ovScale[i], ovAlpha[i],
                                     ovHidden[i] ? 1 : 0, key));
            if (customPage[slot] != PAGE_PLAY) {
                out.append(',').append(customPage[slot]); // older layouts lack it: Gameplay
            }
        }
        return out.toString();
    }

    private void resetControl(int control) {
        ovDx[control] = 0f;
        ovDy[control] = 0f;
        ovScale[control] = 1f;
        ovAlpha[control] = 100;
        ovHidden[control] = false;
        if (control < CONTROLS) {
            ovRemoved[control] = false;
        }
    }

    // The editor: every touch is consumed, so nothing reaches the game (F1 does
    // not pause it, so it keeps running underneath). Inputs held on entry are
    // released first so a stick or button isn't left pressed.
    private void beginEdit() {
        releaseAll();
        // Fingers handed to SDL (over the closing overlay) must still be lifted.
        for (int pointerId : forwarded) {
            SDLActivity.onNativeTouch(0, pointerId, MotionEvent.ACTION_CANCEL, 0f, 0f, 0f);
        }
        forwarded.clear();
        hidden = false;
        autoHidden = false;
        editing = true;
        ballPage = false;
        selected = -1;
        editNotice = null;
        dragPointer = -1;
        pinchPointer = -1;
        editToolPointer = -1;
        invalidate();
    }

    private void endEdit(boolean save) {
        if (save) {
            layoutText = serializeLayout();
            nativeSetTouchLayout(layoutText);
        }
        editing = false;
        editPending = false;
        selected = -1;
        dragPointer = -1;
        pinchPointer = -1;
        editToolPointer = -1;
        invalidate();
    }

    // Whether the current settings draw this control.
    private boolean editShown(int control) {
        return !removed(control) && layoutShows(control);
    }

    // Whether the current layout, settings and page have this control at all,
    // deleted or not (Add's list restores only those).
    private boolean layoutShows(int control) {
        return onPage(control) && layoutHas(control);
    }

    // layoutShows on either page.
    private boolean layoutHas(int control) {
        if (control >= C_CUSTOM) {
            return customAction[control - C_CUSTOM] != null;
        }
        switch (control) {
            case C_CSTICK:
                return cStick && !twin;
            case C_A:
            case C_B:
            case C_X:
            case C_Y:
            case C_L:
            case C_R:
                return !twin;
            case C_RSTICK:
            case C_JUMP:
            case C_FIRE:
            case C_MORPH:
            case C_MISSILE:
            case C_LT:
            case C_LB:
            case C_RT:
            case C_RB:
                return twin;
            case C_TR:
                return false; // retired: twin's R pill, kept so saved ids don't shift
            case C_TURBO:
                return turbo && !twin;
            case C_TTURBO:
                return turbo && twin;
            case C_DPAD:
                return !wheels;
            case C_Z:
                return !twin && !mapTap;
            case C_TZ:
                return twin && !mapTap;
            case C_MAP:
                return mapTap;
            case C_EDIT:
                return !mapTap;
            case C_VISOR:
            case C_BEAM:
                return wheels;
            default:
                return true;
        }
    }

    private PillButton pillOf(int control) {
        for (PillButton pill : pills) {
            if (cornerSlot(pill) < 0 && pillControl(pill) == control) {
                return pill;
            }
        }
        return null;
    }

    // A control's current bounds: the same geometry the draw and hit test use.
    private void controlBounds(int control, float width, float height, RectF out) {
        if (control >= C_CUSTOM) {
            customGeometry(control, width, height, faceGeo);
            out.set(faceGeo[0] - faceGeo[2], faceGeo[1] - faceGeo[2], faceGeo[0] + faceGeo[2],
                    faceGeo[1] + faceGeo[2]);
            return;
        }
        final float u = layoutU(height);
        switch (control) {
            case C_LSTICK: {
                final float r = leftStickRadius(height);
                final float x = leftStickX(width, height);
                final float y = leftStickY(width, height);
                out.set(x - r, y - r, x + r, y + r);
                return;
            }
            case C_CSTICK:
            case C_RSTICK: {
                final float r = rightStickRadius(height);
                final float x = rightStickX(width, height);
                final float y = rightStickY(height);
                out.set(x - r, y - r, x + r, y + r);
                return;
            }
            case C_DPAD: {
                final float arm = DPAD_ARM * u * ovScale[C_DPAD];
                final float x = dpadX(width, height);
                final float y = dpadY(height);
                out.set(x - arm, y - arm, x + arm, y + arm);
                return;
            }
            case C_A:
            case C_B:
            case C_X:
            case C_Y:
            case C_JUMP:
            case C_FIRE:
            case C_MORPH:
            case C_MISSILE:
            case C_TURBO:
            case C_TTURBO: {
                final ControlButton button =
                    control == C_TURBO || control == C_TTURBO ? turboButton
                    : face[control >= C_JUMP ? control - C_JUMP : control - C_A];
                if (button == null) {
                    out.setEmpty();
                    return;
                }
                faceBoundsOf(button, width, height, out);
                return;
            }
            case C_BBOMB:
            case C_BBOOST:
            case C_BPBOMB:
            case C_BSPIDER:
            case C_BUNMORPH:
                faceBoundsOf(ballFace[control - C_BALL], width, height, out);
                return;
            case C_L:
            case C_R:
            case C_Z:
            case C_LT:
            case C_LB:
            case C_RT:
            case C_RB:
            case C_TZ:
            case C_TR: {
                final PillButton pill = pillOf(control);
                if (pill != null) {
                    pillRect(pill, width, height, out);
                } else {
                    out.setEmpty();
                }
                return;
            }
            case C_VISOR:
            case C_BEAM: {
                final int wheel = control - C_VISOR;
                final float r = wheelButtonRadius(wheel, height);
                final float x = wheelButtonX(wheel, width, height);
                final float y = wheelButtonY(wheel, height);
                out.set(x - r, y - r, x + r, y + r);
                return;
            }
            case C_START:
                bottomButtonRect(0, false, width, height, out);
                return;
            case C_MENU:
                bottomButtonRect(1, false, width, height, out);
                return;
            case C_MAP:
                bottomButtonRect(1, true, width, height, out);
                return;
            case C_EDIT:
                editButtonRect(width, height, out);
                return;
            default:
                bottomButtonRect(0, true, width, height, out);
        }
    }

    private void faceBoundsOf(ControlButton button, float width, float height, RectF out) {
        faceGeometry(button, width, height, true, faceGeo);
        if (button.isKidney()) {
            kidneyPath(facePath, faceGeo[0], faceGeo[1], faceGeo[2], faceGeo[3],
                       button.arcStart, button.arcSweep);
            facePath.computeBounds(out, true);
        } else {
            out.set(faceGeo[0] - faceGeo[2], faceGeo[1] - faceGeo[2],
                    faceGeo[0] + faceGeo[2], faceGeo[1] + faceGeo[2]);
        }
    }

    // Keeps at least a control's centre on screen, and its offset sane.
    private void clampToScreen(int control, float width, float height) {
        final float u = layoutU(height);
        if (!(u > 0f) || !(width > 0f)) {
            return; // no size yet: dividing by u would put NaN in the layout
        }
        ovDx[control] = Math.max(-MAX_OFFSET, Math.min(MAX_OFFSET, ovDx[control]));
        ovDy[control] = Math.max(-MAX_OFFSET, Math.min(MAX_OFFSET, ovDy[control]));
        controlBounds(control, width, height, clampRect);
        final float cx = clampRect.centerX();
        final float cy = clampRect.centerY();
        ovDx[control] += (Math.max(0f, Math.min(width, cx)) - cx) / u;
        ovDy[control] += (Math.max(0f, Math.min(height, cy)) - cy) / u;
    }

    // The toolbar shows only what applies to the selection, to stay narrow.
    private int[] editTools() {
        return selected < 0 ? EDIT_TOOLS_NONE
            : selected >= C_CUSTOM ? EDIT_TOOLS_CUSTOM : EDIT_TOOLS_STOCK;
    }

    private void layoutEditBar(float width) {
        final int[] tools = editTools();
        float total = 0f;
        for (int tool : tools) {
            total += dp(EDIT_WIDTHS_DP[tool]);
        }
        final float gap = dp(4);
        total += gap * (tools.length - 1);
        final float fit = Math.min(1f, (width - 2f * dp(8)) / total);
        float x = (width - total * fit) / 2f;
        final float top = dp(8);
        for (int tool : tools) {
            final float w = dp(EDIT_WIDTHS_DP[tool]) * fit;
            editBar[tool].set(x, top, x + w, top + dp(EDIT_BAR_HEIGHT_DP));
            x += w + gap * fit;
        }
    }

    private int editBarAt(float x, float y) {
        for (int tool : editTools()) {
            if (editBar[tool].contains(x, y)) {
                return tool;
            }
        }
        return -1;
    }

    private int freeSlot() {
        for (int slot = 0; slot < CUSTOM_SLOTS; ++slot) {
            if (customAction[slot] == null) {
                return slot;
            }
        }
        return -1;
    }

    // The selected control's name: an added button goes by its action.
    private String controlName(int control) {
        if (control < C_CUSTOM) {
            return CONTROL_NAMES[control];
        }
        final String label = actionLabel(customAction[control - C_CUSTOM]);
        return label != null ? label : "Button";
    }

    // Lists the actions an added button can take; a pick adds a button in the
    // first free slot (control -1, then selected) or changes `control`'s action.
    private void pickAction(final int control) {
        // Adding also lists the deleted stock controls this layout has, first;
        // the actions only while a slot is free.
        final int[] restore = control < 0 ? restorable() : new int[0];
        final String[] list = control >= 0 || freeSlot() >= 0 ? buttonActions() : new String[0];
        if (restore.length + list.length / 2 == 0) {
            return;
        }
        final String current = control >= C_CUSTOM ? customAction[control - C_CUSTOM] : null;
        final String[] labels = new String[restore.length + list.length / 2];
        int checked = -1;
        for (int i = 0; i < restore.length; ++i) {
            labels[i] = "Restore " + CONTROL_NAMES[restore[i]];
        }
        for (int i = 0; i < list.length / 2; ++i) {
            labels[restore.length + i] = list[2 * i + 1];
            if (list[2 * i].equals(current)) {
                checked = restore.length + i;
            }
        }
        final DialogInterface.OnClickListener pick = new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                dialog.dismiss();
                if (which < restore.length) {
                    restoreControl(restore[which]);
                } else {
                    applyAction(control, list[2 * (which - restore.length)]);
                }
            }
        };
        final AlertDialog.Builder builder = new AlertDialog.Builder(getContext())
            .setTitle(control < 0 ? "Add a button" : "Button action")
            .setNegativeButton("Cancel", null);
        if (checked >= 0) {
            builder.setSingleChoiceItems(labels, checked, pick);
        } else {
            builder.setItems(labels, pick);
        }
        builder.show();
    }

    // The deleted stock controls the current layout has.
    private int[] restorable() {
        int count = 0;
        for (int c = 0; c < CONTROLS; ++c) {
            if (ovRemoved[c] && layoutShows(c)) {
                ++count;
            }
        }
        final int[] out = new int[count];
        count = 0;
        for (int c = 0; c < CONTROLS; ++c) {
            if (ovRemoved[c] && layoutShows(c)) {
                out[count++] = c;
            }
        }
        return out;
    }

    private void restoreControl(int control) {
        if (!editing) {
            return; // the editor closed under the list
        }
        ovRemoved[control] = false;
        ovHidden[control] = false;
        selected = control;
        invalidate();
    }

    private void applyAction(int control, String key) {
        if (!editing) {
            return; // the editor closed under the list
        }
        if (control < 0) {
            final int slot = freeSlot();
            if (slot < 0) {
                return;
            }
            customAction[slot] = key;
            customPage[slot] = ballPage ? PAGE_BALL : PAGE_PLAY; // the page it was added on
            resetControl(C_CUSTOM + slot);
            selected = C_CUSTOM + slot;
        } else if (customAction[control - C_CUSTOM] != null) {
            customAction[control - C_CUSTOM] = key;
        }
        invalidate();
    }

    // MENU and Edit are the ways into F1 and the editor: never hidden, deleted or fully faded.
    private static boolean pinned(int control) {
        return control == C_MENU || control == C_EDIT;
    }

    // The next opacity in the 100, 75, 50, 25, 0 cycle; MENU wraps after 25.
    private int nextAlpha(int control) {
        final int floor = pinned(control) ? MENU_MIN_ALPHA : 0;
        for (int step : ALPHA_STEPS) {
            if (step < ovAlpha[control] && step >= floor) {
                return step;
            }
        }
        return 100;
    }

    private void editAction(int action, float width, float height) {
        switch (action) {
            case EDIT_MINUS:
            case EDIT_PLUS:
                if (selected >= 0) {
                    final float step = action == EDIT_PLUS ? SCALE_STEP : -SCALE_STEP;
                    // Rounded so repeated steps land on tidy percentages.
                    ovScale[selected] = Math.max(MIN_SCALE, Math.min(MAX_SCALE,
                        Math.round((ovScale[selected] + step) * 100f) / 100f));
                    clampToScreen(selected, width, height);
                }
                break;
            case EDIT_OPACITY:
                if (selected >= 0) {
                    ovAlpha[selected] = nextAlpha(selected);
                }
                break;
            case EDIT_HIDE:
                if (selected >= 0 && !pinned(selected)) {
                    ovHidden[selected] = !ovHidden[selected];
                }
                break;
            case EDIT_RESET:
                if (selected >= 0) {
                    resetControl(selected);
                }
                break;
            case EDIT_RESET_ALL:
                for (int i = 0; i < ALL_CONTROLS; ++i) {
                    resetControl(i);
                }
                Arrays.fill(customAction, null); // the added buttons go too
                Arrays.fill(customPage, PAGE_PLAY);
                break;
            case EDIT_PAGE:
                ballPage = !ballPage;
                selected = -1;
                break;
            case EDIT_CUSTOM_PAGE:
                if (selected >= C_CUSTOM) {
                    final int slot = selected - C_CUSTOM;
                    customPage[slot] = (customPage[slot] + 1) % PAGE_NAMES.length;
                    if (!onPage(selected)) {
                        selected = -1; // it went to the other page
                    }
                }
                break;
            case EDIT_ADD:
                if (freeSlot() < 0 && restorable().length == 0) {
                    editNotice = "All " + CUSTOM_SLOTS + " buttons are in use: delete one to add another";
                } else {
                    pickAction(-1);
                }
                break;
            case EDIT_ACTION:
                if (selected >= C_CUSTOM) {
                    pickAction(selected);
                }
                break;
            case EDIT_DELETE:
                if (selected >= C_CUSTOM) {
                    customAction[selected - C_CUSTOM] = null;
                    resetControl(selected);
                    selected = -1;
                } else if (selected >= 0 && !pinned(selected)) {
                    // Kept where it was, so a restore brings it back as it was.
                    ovRemoved[selected] = true;
                    selected = -1;
                }
                break;
            case EDIT_LAYOUT:
                // MP2: the native side steps Default -> Classic -> Twin stick; the next draw
                // reads it. Each layout keeps its own controls' offsets, as in MP1.
                nativeCycleTouchLayout();
                selected = -1;
                break;
            default:
                endEdit(true);
                return;
        }
        invalidate();
    }

    private boolean onEditTouch(MotionEvent event) {
        final float width = getWidth();
        final float height = getHeight();
        final int action = event.getActionMasked();
        final int index = event.getActionIndex();
        layoutEditBar(width);
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                final int id = event.getPointerId(index);
                final float x = event.getX(index);
                final float y = event.getY(index);
                editNotice = null;
                final int tool = editBarAt(x, y);
                if (tool >= 0) {
                    if (editToolPointer == -1) {
                        editToolPointer = id;
                        editAction(tool, width, height);
                    }
                } else if (dragPointer != -1 && pinchPointer == -1 && selected >= 0) {
                    final int other = event.findPointerIndex(dragPointer);
                    if (other >= 0) {
                        final float dist = (float) Math.hypot(x - event.getX(other),
                                                              y - event.getY(other));
                        if (dist >= dp(MAP_PINCH_MIN_DP)) {
                            pinchPointer = id;
                            pinchStartDist = dist;
                            pinchStartScale = ovScale[selected];
                        }
                    }
                } else if (dragPointer == -1) {
                    final int hit = editHit(x, y, width, height);
                    selected = hit;
                    if (hit >= 0) {
                        dragPointer = id;
                        dragLastX = x;
                        dragLastY = y;
                    }
                    invalidate();
                }
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                if (selected < 0) {
                    return true;
                }
                final int drag = event.findPointerIndex(dragPointer);
                final int pinch = event.findPointerIndex(pinchPointer);
                if (drag >= 0 && pinch >= 0) {
                    final float dist = (float) Math.hypot(event.getX(drag) - event.getX(pinch),
                                                          event.getY(drag) - event.getY(pinch));
                    ovScale[selected] = Math.max(MIN_SCALE, Math.min(MAX_SCALE,
                        pinchStartScale * dist / pinchStartDist));
                    // Keep the drag anchor current so lifting one finger doesn't jump.
                    dragLastX = event.getX(drag);
                    dragLastY = event.getY(drag);
                    clampToScreen(selected, width, height);
                } else if (drag >= 0) {
                    final float u = layoutU(height);
                    ovDx[selected] += (event.getX(drag) - dragLastX) / u;
                    ovDy[selected] += (event.getY(drag) - dragLastY) / u;
                    dragLastX = event.getX(drag);
                    dragLastY = event.getY(drag);
                    clampToScreen(selected, width, height);
                }
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                final int id = event.getPointerId(index);
                if (id == editToolPointer) {
                    editToolPointer = -1;
                } else if (id == pinchPointer) {
                    pinchPointer = -1;
                } else if (id == dragPointer) {
                    // The pinch finger carries on as the drag.
                    dragPointer = pinchPointer;
                    pinchPointer = -1;
                    final int next = event.findPointerIndex(dragPointer);
                    if (next >= 0) {
                        dragLastX = event.getX(next);
                        dragLastY = event.getY(next);
                    }
                }
                if (action == MotionEvent.ACTION_UP) {
                    dragPointer = -1;
                    pinchPointer = -1;
                    editToolPointer = -1;
                }
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                dragPointer = -1;
                pinchPointer = -1;
                editToolPointer = -1;
                return true;
            default:
                return true;
        }
    }

    // The smallest shown control under (x, y), so a small button on a big one
    // (B near A, the stick under Visor) is still reachable. Of equal ones (added
    // buttons stacked at the centre), the selected one, else the last added.
    private int editHit(float x, float y, float width, float height) {
        int best = -1;
        float bestArea = Float.MAX_VALUE;
        final float slack = dp(EDIT_GRAB_DP);
        for (int c = 0; c < ALL_CONTROLS; ++c) {
            if (!editShown(c)) {
                continue;
            }
            controlBounds(c, width, height, editRect);
            if (editRect.isEmpty()) {
                continue;
            }
            editRect.inset(-slack, -slack);
            final float area = editRect.width() * editRect.height();
            if (editRect.contains(x, y) &&
                (area < bestArea ||
                 (area == bestArea && c >= C_CUSTOM && best != selected))) {
                best = c;
                bestArea = area;
            }
        }
        return best;
    }

    private void drawEditOverlay(Canvas canvas, float width, float height) {
        final float slack = dp(EDIT_GRAB_DP) * 0.5f;
        if (editDashes == null) {
            editDashes = new DashPathEffect(new float[] {dp(6), dp(4)}, 0f);
        }
        final DashPathEffect dashes = editDashes;
        for (int c = 0; c < ALL_CONTROLS; ++c) {
            if (!editShown(c)) {
                continue;
            }
            controlBounds(c, width, height, editRect);
            if (editRect.isEmpty()) {
                continue;
            }
            editRect.inset(-slack, -slack);
            final float corner = dp(8);
            if (c == selected) {
                fillPaint.setColor(0x5548C8E8);
                canvas.drawRoundRect(editRect, corner, corner, fillPaint);
                editPaint.setPathEffect(null);
                editPaint.setColor(0xFFFFE04A);
            } else {
                editPaint.setPathEffect(dashes);
                // Hidden ones in orange, so they stand out from the faint ones.
                editPaint.setColor(ovHidden[c] ? 0xFFFF9A3C : 0xCCFFFFFF);
            }
            canvas.drawRoundRect(editRect, corner, corner, editPaint);
            // Tag what isn't default, so a faded control reads as faded on purpose.
            final String tag = ovHidden[c] ? "hidden"
                : ovAlpha[c] != 100 ? ovAlpha[c] + "%" : null;
            if (tag != null) {
                drawCenteredLabel(canvas, tag, editRect.centerX(), editRect.top + dp(10),
                                  dp(10));
            }
        }
        editPaint.setPathEffect(null);

        layoutEditBar(width);
        for (int i : editTools()) {
            fillPaint.setColor(i == EDIT_DONE ? 0xDD2E8B57 : 0xDD081218);
            strokePaint.setColor(0xCCFFFFFF);
            final float corner = dp(8);
            canvas.drawRoundRect(editBar[i], corner, corner, fillPaint);
            canvas.drawRoundRect(editBar[i], corner, corner, strokePaint);
            String label = EDIT_LABELS[i];
            if (i == EDIT_HIDE && selected >= 0 && ovHidden[selected]) {
                label = "Show";
            } else if (i == EDIT_OPACITY && selected >= 0) {
                label = "Opacity " + ovAlpha[selected] + "%"; // a tap steps it down
            } else if (i == EDIT_PAGE) {
                label = ballPage ? "Play page" : "Ball page"; // the page a tap goes to
            } else if (i == EDIT_CUSTOM_PAGE && selected >= C_CUSTOM) {
                label = "On " + PAGE_NAMES[customPage[selected - C_CUSTOM]];
            }
            // Tools that can't act now are dimmed: Hide on MENU, Add with no free slot.
            final boolean idle =
                ((i == EDIT_HIDE || i == EDIT_DELETE) && pinned(selected)) ||
                (i == EDIT_ADD && freeSlot() < 0 && restorable().length == 0);
            textPaint.setAlpha(idle ? 90 : 255);
            drawCenteredLabel(canvas, label, editBar[i].centerX(), editBar[i].centerY(),
                              i <= EDIT_PLUS ? dp(22) : dp(14));
            textPaint.setAlpha(255);
        }

        String hint = (ballPage ? "Morph Ball page. " : "Gameplay page. ") +
            "Tap a control to select it: drag to move, pinch to resize. Add: a new or deleted button";
        if (editNotice != null) {
            hint = editNotice;
        } else if (selected >= 0) {
            hint = controlName(selected) + "  " + Math.round(ovScale[selected] * 100f) + "%";
            if (ovAlpha[selected] != 100) {
                hint += "  · opacity " + ovAlpha[selected] + "%";
            }
            if (ovHidden[selected]) {
                hint += "  · hidden";
            }
        }
        textPaint.setTextSize(dp(13));
        final float textW = textPaint.measureText(hint);
        final float top = editBar[EDIT_DONE].bottom + dp(EDIT_BAR_GAP_DP);
        final float cx = width / 2f;
        editRect.set(cx - textW / 2f - dp(10), top, cx + textW / 2f + dp(10), top + dp(26));
        fillPaint.setColor(0xCC081218);
        canvas.drawRoundRect(editRect, dp(8), dp(8), fillPaint);
        drawCenteredLabel(canvas, hint, cx, editRect.centerY(), dp(13));
    }

    private final RectF pillHit = new RectF();

    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private static final class ControlButton {
        final String label;
        final int button;
        // A plain button's x is a fraction of the width and its y one of the
        // height. An anchored one's are offsets from GameCube A, in heights.
        final boolean anchored;
        final float x;
        final float y;
        // A kidney's radius is its centre arc's, round its x/y.
        final float radius;
        // Kidneys only: the band's half width, and its arc in degrees,
        // clockwise from +x. A zero sweep is a round button.
        final float halfWidth;
        final float arcStart;
        final float arcSweep;
        // An RGB fill, or 0 for the overlay's own.
        final int color;

        private ControlButton(String label, int button, boolean anchored, float x, float y,
                              float radius, float halfWidth, float arcStart, float arcSweep,
                              int color) {
            this.label = label;
            this.button = button;
            this.anchored = anchored;
            this.x = x;
            this.y = y;
            this.radius = radius;
            this.halfWidth = halfWidth;
            this.arcStart = arcStart;
            this.arcSweep = arcSweep;
            this.color = color;
        }

        static ControlButton round(String label, int button, float x, float y, float radius,
                                   int color) {
            return new ControlButton(label, button, true, x, y, radius, 0f, 0f, 0f, color);
        }

        static ControlButton kidney(String label, int button, float ring, float halfWidth,
                                    float arcStart, float arcSweep, int color) {
            return new ControlButton(label, button, true, 0f, 0f, ring, halfWidth, arcStart,
                                     arcSweep, color);
        }

        boolean isKidney() {
            return arcSweep != 0f;
        }
    }

    private static final class PillButton {
        final String label;
        final int axis;
        final int button;
        final float left;
        final float top;
        final float right;
        final float bottom;
        // An RGB fill, or 0 for the overlay's own.
        final int color;
        // The editor control this pill is, or -1 to go by its axis/button (pillControl).
        final int control;
        PillButton(String label, int axis, int button, float left, float top, float right,
                   float bottom) {
            this(label, axis, button, left, top, right, bottom, 0, -1);
        }

        PillButton(String label, int axis, int button, float left, float top, float right,
                   float bottom, int color) {
            this(label, axis, button, left, top, right, bottom, color, -1);
        }

        PillButton(String label, int axis, int button, float left, float top, float right,
                   float bottom, int color, int control) {
            this.control = control;
            this.color = color;
            this.label = label;
            this.axis = axis;
            this.button = button;
            this.left = left;
            this.top = top;
            this.right = right;
            this.bottom = bottom;
        }

        // One identity for the press map and the held-highlight, whether this is
        // a button or an analog trigger sent as an axis.
        int id() {
            return axis >= 0 ? AXIS_ID_BASE + axis : button;
        }
    }

    private static final class TouchTarget {
        final int type;
        final int id;
        float x;
        float y;
        float startX;
        float startY;
        long startMs;
        long radialToken; // 0 legacy, -1 native busy, >0 native session; never switch mid-gesture
        int radialOwner;
        // A held A button that also aims; see startButtonAim.
        boolean aiming;
        // The pill or face button a BUTTON target pressed. Twin sends one GC button
        // from two controls, so the highlight follows the control, not `held`.
        Object control;
        // Its editor control (C_*), still reported as held, or -1.
        int touchControl = -1;
        // A touch: binding took the control over: no button of its own to release.
        boolean bound;
        // The aiming finger's current upward stroke (see trackSwipe): where and
        // when it began (swipeMs -1: none yet), and whether it already sprang.
        float swipeX;
        float swipeY;
        long swipeMs = -1;
        boolean swiped;

        TouchTarget(int type, int id) {
            this.type = type;
            this.id = id;
        }

        // A target whose finger went down at (x, y).
        static TouchTarget begin(int type, int id, float x, float y) {
            final TouchTarget target = new TouchTarget(type, id);
            target.x = x;
            target.y = y;
            target.startX = x;
            target.startY = y;
            return target;
        }
    }
}
