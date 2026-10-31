// Binding-aware button prompt icons; see port_prompts.h.

#include "port_utf8_path.h"
#include "port_prompts.h"
#include "port_entry.h"
#include "port_embedded.h"
#include "port_env.h"
#include "port_overlay.h"

#include "input/port_input_devices.h"
#include "port_textures.h"

#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <aurora/texture.hpp>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
// Prompts that are not one button: the control stick and the C-stick, at rest
// or pushed one way, and the D-pad as a whole (its directions are buttons).
// Numbered past PADButton's range so they share the table with the buttons.
enum : uint32_t {
  PROMPT_STICK = 0x10000,
  PROMPT_STICK_UP,
  PROMPT_STICK_DOWN,
  PROMPT_STICK_LEFT,
  PROMPT_STICK_RIGHT,
  PROMPT_CSTICK,
  PROMPT_CSTICK_UP,
  PROMPT_CSTICK_DOWN,
  PROMPT_CSTICK_LEFT,
  PROMPT_CSTICK_RIGHT,
  PROMPT_DPAD,
};

// The prompt textures the port can re-icon. Each names the game action it
// stands for, and the hash identifies the game's own texture under Aurora's
// name for it. These are Prime 1's prompt art, identified from that disc by the
// Prime 1 port's tools/extract_textures.py (the comments are its PAK names);
// Trilogy's own are listed further down. Other prompt textures are found in a dump
// (MP2_DUMP_TEXTURES=1) and listed here. A key that the disc does not
// share simply never matches, and the game keeps its own art. One action often has several, as
// the screens draw pressed and released frames and some hints have their own.
struct PromptKey {
  uint32_t prompt;  // a PADButton or a PROMPT_*
  uint32_t width;
  uint32_t height;
  uint64_t hash;
  const char* format;
};
// The game's prompt art by asset id, sorted by id. CTexture binds these by
// pointer (BindTexture), which holds whatever the content: matching on a texel
// hash missed on any disc whose art differs by a pixel. Found by hashing the
// texels of each kKeys entry against G2ME01's paks, and by logging the ids the
// Trilogy disc loads (its Wii-only art is the last few of each action). The few
// textures with no id (kKeys below) are still matched by hash.
struct PromptAsset {
  uint32_t id;
  uint32_t prompt;
};
// Ids marked "Trilogy only" are Wii art that the GameCube disc draws with other
// ids; each was paired with its GameCube counterpart by aligning the two discs'
// STRGs string by string.
constexpr PromptAsset kAssets[] = {
    {0x057FAEE3, PROMPT_STICK}, // Trilogy only: Nunchuk stick frame, "Use (stick) and tap B to Dash"
    {0x08BCA722, PROMPT_STICK_RIGHT}, {0x0A254C97, PAD_BUTTON_START}, // the GameCube pause Exit
    {0x10799395, PAD_TRIGGER_Z}, {0x13AD51E9, PAD_BUTTON_DOWN}, {0x17EB2A51, PROMPT_CSTICK_RIGHT},
    {0x194E0B59, PROMPT_STICK}, // Trilogy only: Nunchuk stick frame (Dash)
    {0x1B525041, PROMPT_CSTICK}, {0x2187B2A1, PROMPT_STICK_UP},
    {0x2CB3B7D2, PROMPT_CSTICK}, {0x2DBD0327, PROMPT_CSTICK}, {0x304DEF5A, PAD_TRIGGER_L},
    {0x3AA65ECD, PAD_BUTTON_Y}, {0x3F609B4A, PROMPT_STICK_LEFT},
    {0x4475EAD6, PAD_BUTTON_X}, // Trilogy only: the Nunchuk C, "Press C to turn into the Morph Ball"
    {0x45990439, PROMPT_CSTICK_DOWN}, {0x47F931C8, PROMPT_STICK}, {0x4EFFA9AA, PROMPT_STICK_DOWN},
    {0x53B7AC95, PAD_BUTTON_B},
    {0x574CDE37, PAD_BUTTON_A}, // Trilogy only: Wii A, fire / charge / back to Beam mode
    {0x59500D0C, PAD_BUTTON_B}, {0x5EA16CAD, PAD_TRIGGER_R},
    // Trilogy only: Nunchuk Z, which scans, grapples and holds the Spider Ball. The
    // GameCube disc says L for the first two and R for the third; one id takes one.
    {0x60ACB985, PAD_TRIGGER_L},
    {0x617D3700, PROMPT_STICK}, // Trilogy only: Nunchuk stick frame (Dash)
    {0x61E49F68, PROMPT_STICK},
    {0x637BA080, PROMPT_STICK}, // Trilogy only: Nunchuk stick frame (Dash)
    {0x6D482337, PAD_BUTTON_A},
    {0x71AA55E3, PAD_BUTTON_B}, // Trilogy only: Wii B, jump / Space Jump / Screw Attack
    {0x72A83A41, PROMPT_STICK},
    {0x738A4798, PROMPT_CSTICK_UP}, {0x73E9B3B2, PROMPT_CSTICK_LEFT},
    {0x7F409C85, PAD_BUTTON_A}, // the logbook's "A to scroll the text" prompt (#34)
    {0x83A73184, PAD_BUTTON_START},
    {0x8D13D507, PROMPT_CSTICK_UP}, {0x8E28D58C, PROMPT_STICK}, {0x9016E5BF, PROMPT_CSTICK_RIGHT},
    {0x944EBD78, PROMPT_STICK}, {0x967A3516, PAD_TRIGGER_L},
    {0x9FFB4F4D, PROMPT_STICK}, // Trilogy only: Nunchuk stick frame (Dash)
    {0xA9CA8654, PROMPT_CSTICK_LEFT}, {0xADCBEDAE, PROMPT_CSTICK}, {0xB4EDE4B6, PROMPT_DPAD},
    {0xB5F3DA09, PROMPT_CSTICK}, // Trilogy only: "hold and point" to pick a Beam (GameCube: C-stick)
    {0xBC322C63, PAD_TRIGGER_Z},
    {0xC14A95C6, PAD_TRIGGER_Z}, {0xC3E07487, PROMPT_STICK_RIGHT}, {0xC622B06F, PAD_BUTTON_RIGHT},
    {0xC83111AA, PROMPT_CSTICK_DOWN}, {0xC96F93E3, PAD_BUTTON_A}, {0xCDA81421, PROMPT_STICK},
    {0xD212C48A, PAD_BUTTON_X}, {0xD3D8817F, PROMPT_CSTICK}, {0xD523DE3B, PAD_BUTTON_A},
    {0xD89B9F7C, PAD_TRIGGER_R}, {0xDE5170B7, PAD_BUTTON_A}, {0xDEA628DC, PAD_BUTTON_B},
    {0xDFC47FA2, PAD_BUTTON_A},
    {0xE2F4B915, PROMPT_DPAD}, // Trilogy only: "hold and point" to pick a Visor (GameCube: D-pad)
    {0xE43966EC, PROMPT_CSTICK}, {0xE72B1419, PAD_BUTTON_A},
    {0xF1FA8D68, PAD_BUTTON_X}, {0xF237E982, PAD_TRIGGER_R}, {0xF43C48EF, PROMPT_STICK_LEFT},
    {0xF6E8276F, PAD_TRIGGER_R}, {0xF7D62F50, PAD_BUTTON_UP}, {0xFF73F326, PROMPT_STICK},
    {0xFFFED384, PAD_BUTTON_LEFT},
};

// Echoes' "1" hint ring shares an id with the GameCube disc's Y art on Trilogy,
// where it is the Z prompt.
constexpr uint32_t kTrilogyZAsset = 0x3AA65ECD;

const PromptAsset* FindAsset(uint32_t id) {
  const auto it = std::lower_bound(std::begin(kAssets), std::end(kAssets), id,
                                   [](const PromptAsset& asset, uint32_t value) { return asset.id < value; });
  return it != std::end(kAssets) && it->id == id ? &*it : nullptr;
}
constexpr bool AssetsSorted() {
  for (size_t i = 1; i < std::size(kAssets); ++i) {
    if (kAssets[i - 1].id >= kAssets[i].id) {
      return false;
    }
  }
  return true;
}
static_assert(AssetsSorted(), "kAssets must be sorted by id");

// Prompt textures with no asset id of their own, matched by texel hash under Aurora's
// name for them (MP2_DUMP_TEXTURES=1 prints it).
constexpr PromptKey kKeys[] = {
    {PAD_BUTTON_X, 32, 32, 0xe14785826e0e103full, "5"},   // XButtonOut on the PAL disc (same asset, other art)
    {PAD_BUTTON_START, 32, 32, 0x1ff9d2b310c0b706ull, "14"},  // StartButtonOut, the pause Return
    {PAD_TRIGGER_R, 32, 32, 0x07165bfca0d0908full, "14"}, // the hints' released R
    {PAD_TRIGGER_L, 32, 32, 0x5fa30f82f962b236ull, "5"},      // Trilogy gray Z (Nunchuk lock-on), "Press and hold Z"
};
constexpr size_t kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

// The actions to resolve bindings for, resolved once per action rather than
// once per texture.
struct PromptAction {
  uint32_t prompt;
  const char* label;
};
constexpr PromptAction kActions[] = {
    {PAD_BUTTON_A, "A"},
    {PAD_BUTTON_B, "B"},
    {PAD_BUTTON_X, "X"},
    {PAD_BUTTON_Y, "Y"},
    {PAD_BUTTON_START, "Start"},
    {PAD_TRIGGER_L, "L"},
    {PAD_TRIGGER_R, "R"},
    {PAD_TRIGGER_Z, "Z"},
    {PROMPT_STICK, "stick"},
    {PROMPT_STICK_UP, "stick up"},
    {PROMPT_STICK_DOWN, "stick down"},
    {PROMPT_STICK_LEFT, "stick left"},
    {PROMPT_STICK_RIGHT, "stick right"},
    {PROMPT_CSTICK, "C-stick"},
    {PROMPT_CSTICK_UP, "C-stick up"},
    {PROMPT_CSTICK_DOWN, "C-stick down"},
    {PROMPT_CSTICK_LEFT, "C-stick left"},
    {PROMPT_CSTICK_RIGHT, "C-stick right"},
    {PROMPT_DPAD, "D-pad"},
    {PAD_BUTTON_UP, "D-pad up"},
    {PAD_BUTTON_DOWN, "D-pad down"},
    {PAD_BUTTON_LEFT, "D-pad left"},
    {PAD_BUTTON_RIGHT, "D-pad right"},
};

// Scancodes mapped to icon stems. Aurora reports mouse buttons as negative
// codes; the rest are SDL scancodes. Stems match the files generated into
// <textures>/bindings/ by tools/make_prompt_glyphs.py.
struct KeyIcon {
  int scancode;
  const char* stem;
};
constexpr KeyIcon kKeyIcons[] = {
    {SDL_SCANCODE_A, "keyboard_a"}, {SDL_SCANCODE_B, "keyboard_b"},
    {SDL_SCANCODE_C, "keyboard_c"}, {SDL_SCANCODE_D, "keyboard_d"},
    {SDL_SCANCODE_E, "keyboard_e"}, {SDL_SCANCODE_F, "keyboard_f"},
    {SDL_SCANCODE_G, "keyboard_g"}, {SDL_SCANCODE_H, "keyboard_h"},
    {SDL_SCANCODE_I, "keyboard_i"}, {SDL_SCANCODE_J, "keyboard_j"},
    {SDL_SCANCODE_K, "keyboard_k"}, {SDL_SCANCODE_L, "keyboard_l"},
    {SDL_SCANCODE_M, "keyboard_m"}, {SDL_SCANCODE_N, "keyboard_n"},
    {SDL_SCANCODE_O, "keyboard_o"}, {SDL_SCANCODE_P, "keyboard_p"},
    {SDL_SCANCODE_Q, "keyboard_q"}, {SDL_SCANCODE_R, "keyboard_r"},
    {SDL_SCANCODE_S, "keyboard_s"}, {SDL_SCANCODE_T, "keyboard_t"},
    {SDL_SCANCODE_U, "keyboard_u"}, {SDL_SCANCODE_V, "keyboard_v"},
    {SDL_SCANCODE_W, "keyboard_w"}, {SDL_SCANCODE_X, "keyboard_x"},
    {SDL_SCANCODE_Y, "keyboard_y"}, {SDL_SCANCODE_Z, "keyboard_z"},
    {SDL_SCANCODE_0, "keyboard_0"}, {SDL_SCANCODE_1, "keyboard_1"},
    {SDL_SCANCODE_2, "keyboard_2"}, {SDL_SCANCODE_3, "keyboard_3"},
    {SDL_SCANCODE_4, "keyboard_4"}, {SDL_SCANCODE_5, "keyboard_5"},
    {SDL_SCANCODE_6, "keyboard_6"}, {SDL_SCANCODE_7, "keyboard_7"},
    {SDL_SCANCODE_8, "keyboard_8"}, {SDL_SCANCODE_9, "keyboard_9"},
    {SDL_SCANCODE_UP, "keyboard_arrow_up"}, {SDL_SCANCODE_DOWN, "keyboard_arrow_down"},
    {SDL_SCANCODE_LEFT, "keyboard_arrow_left"}, {SDL_SCANCODE_RIGHT, "keyboard_arrow_right"},
    {SDL_SCANCODE_F1, "keyboard_f1"}, {SDL_SCANCODE_F2, "keyboard_f2"},
    {SDL_SCANCODE_F3, "keyboard_f3"}, {SDL_SCANCODE_F4, "keyboard_f4"},
    {SDL_SCANCODE_F5, "keyboard_f5"}, {SDL_SCANCODE_F6, "keyboard_f6"},
    {SDL_SCANCODE_F7, "keyboard_f7"}, {SDL_SCANCODE_F8, "keyboard_f8"},
    {SDL_SCANCODE_F9, "keyboard_f9"}, {SDL_SCANCODE_F10, "keyboard_f10"},
    {SDL_SCANCODE_F11, "keyboard_f11"}, {SDL_SCANCODE_F12, "keyboard_f12"},
    {SDL_SCANCODE_SPACE, "keyboard_space"}, {SDL_SCANCODE_RETURN, "keyboard_enter"},
    {SDL_SCANCODE_ESCAPE, "keyboard_escape"}, {SDL_SCANCODE_TAB, "keyboard_tab"},
    {SDL_SCANCODE_BACKSPACE, "keyboard_backspace"}, {SDL_SCANCODE_DELETE, "keyboard_delete"},
    {SDL_SCANCODE_INSERT, "keyboard_insert"}, {SDL_SCANCODE_HOME, "keyboard_home"},
    {SDL_SCANCODE_END, "keyboard_end"}, {SDL_SCANCODE_PAGEUP, "keyboard_page_up"},
    {SDL_SCANCODE_PAGEDOWN, "keyboard_page_down"},
    {SDL_SCANCODE_LSHIFT, "keyboard_shift"}, {SDL_SCANCODE_RSHIFT, "keyboard_shift"},
    {SDL_SCANCODE_LCTRL, "keyboard_ctrl"}, {SDL_SCANCODE_RCTRL, "keyboard_ctrl"},
    {SDL_SCANCODE_LALT, "keyboard_alt"}, {SDL_SCANCODE_RALT, "keyboard_alt"},
    {SDL_SCANCODE_LGUI, "keyboard_win"}, {SDL_SCANCODE_RGUI, "keyboard_win"},
    {SDL_SCANCODE_MINUS, "keyboard_minus"}, {SDL_SCANCODE_EQUALS, "keyboard_equals"},
    {SDL_SCANCODE_LEFTBRACKET, "keyboard_bracket_open"},
    {SDL_SCANCODE_RIGHTBRACKET, "keyboard_bracket_close"},
    {SDL_SCANCODE_SEMICOLON, "keyboard_semicolon"}, {SDL_SCANCODE_APOSTROPHE, "keyboard_apostrophe"},
    {SDL_SCANCODE_COMMA, "keyboard_comma"}, {SDL_SCANCODE_PERIOD, "keyboard_period"},
    {SDL_SCANCODE_SLASH, "keyboard_slash_forward"}, {SDL_SCANCODE_BACKSLASH, "keyboard_slash_back"},
    {SDL_SCANCODE_NONUSBACKSLASH, "keyboard_slash_back"}, {SDL_SCANCODE_GRAVE, "keyboard_tilde"},
    {SDL_SCANCODE_CAPSLOCK, "keyboard_capslock"}, {SDL_SCANCODE_PRINTSCREEN, "keyboard_printscreen"},
    {SDL_SCANCODE_SCROLLLOCK, "keyboard_scroll_lock"}, {SDL_SCANCODE_PAUSE, "keyboard_pause"},
    // The keypad. The pack has no keypad digits, so those share the main row's.
    {SDL_SCANCODE_KP_0, "keyboard_0"}, {SDL_SCANCODE_KP_1, "keyboard_1"},
    {SDL_SCANCODE_KP_2, "keyboard_2"}, {SDL_SCANCODE_KP_3, "keyboard_3"},
    {SDL_SCANCODE_KP_4, "keyboard_4"}, {SDL_SCANCODE_KP_5, "keyboard_5"},
    {SDL_SCANCODE_KP_6, "keyboard_6"}, {SDL_SCANCODE_KP_7, "keyboard_7"},
    {SDL_SCANCODE_KP_8, "keyboard_8"}, {SDL_SCANCODE_KP_9, "keyboard_9"},
    {SDL_SCANCODE_KP_ENTER, "keyboard_numpad_enter"}, {SDL_SCANCODE_KP_PLUS, "keyboard_numpad_plus"},
    {SDL_SCANCODE_KP_MINUS, "keyboard_minus"}, {SDL_SCANCODE_KP_MULTIPLY, "keyboard_asterisk"},
    {SDL_SCANCODE_KP_DIVIDE, "keyboard_slash_forward"}, {SDL_SCANCODE_KP_PERIOD, "keyboard_period"},
    {SDL_SCANCODE_NUMLOCKCLEAR, "keyboard_numlock"},
    {PAD_KEY_MOUSE_LEFT, "mouse_left"}, {PAD_KEY_MOUSE_RIGHT, "mouse_right"},
    {PAD_KEY_MOUSE_MIDDLE, "mouse_scroll"},
    {PAD_KEY_MOUSE_X1, "mouse_side_back"}, {PAD_KEY_MOUSE_X2, "mouse_side_forward"},
};

// A GameCube pad's buttons carry GameCube labels, which SDL's positional names
// don't give; Aurora's default GC mappings (lib/dolphin/pad/pad.cpp) pin each
// SDL button to the GC button of the same name, so they double as the labels.
// Kept in step with g_defaultButtonsGamecube / g_defaultButtonsNSOGamecube.
struct GameCubeButton {
  int sdlButton;
  PADButton action;
  const char* stem;
};
constexpr GameCubeButton kGameCubeButtons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A, "gamecube_a"},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B, "gamecube_b"},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X, "gamecube_x"},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y, "gamecube_y"},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START, "gamecube_start"},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z, "gamecube_z"},
    {SDL_GAMEPAD_BUTTON_MISC3, PAD_TRIGGER_L, "gamecube_l"},
    {SDL_GAMEPAD_BUTTON_MISC4, PAD_TRIGGER_R, "gamecube_r"},
    {PAD_NATIVE_BUTTON_TRIGGER_LEFT, PAD_TRIGGER_L, "gamecube_l"},
    {PAD_NATIVE_BUTTON_TRIGGER_RIGHT, PAD_TRIGGER_R, "gamecube_r"},
};
constexpr GameCubeButton kNsoGameCubeButtons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A, "gamecube_a"},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B, "gamecube_b"},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X, "gamecube_x"},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y, "gamecube_y"},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START, "gamecube_start"},
    {SDL_GAMEPAD_BUTTON_BACK, PAD_TRIGGER_Z, "gamecube_z"},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PAD_TRIGGER_L, "gamecube_l"},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_R, "gamecube_r"},
    {PAD_NATIVE_BUTTON_TRIGGER_LEFT, PAD_TRIGGER_L, "gamecube_l"},
    {PAD_NATIVE_BUTTON_TRIGGER_RIGHT, PAD_TRIGGER_R, "gamecube_r"},
};

// The native pad button that presses `button` for the prompts: the bindings in
// use (Remap's, and the "Menus and map" rows in menus and the map while
// "Separate menu buttons" applies), or PAD_NATIVE_BUTTON_INVALID.
u32 PadNativeForButton(PADButton button) {
  PortInput::Input in;
  if (!PortInputDevices::PromptButtonInput(button, PortInput::Family::Pad, in)) {
    return PAD_NATIVE_BUTTON_INVALID;
  }
  return PortInputDevices::PromptNativeButton(in);
}

// The GameCube icon for an action's own button.
std::string GameCubeStemForAction(PADButton button) {
  for (const GameCubeButton& entry : kGameCubeButtons) {
    if (entry.action == button) {
      return entry.stem;
    }
  }
  return {};
}

// The icon for `button` on a GameCube pad, or empty to leave the game's own art,
// which is already right for a GC pad on its default mapping. So only an action
// moved to another button gets an icon. The GameCube touch layout also resolves
// to "gamecube", hence the check that a GC pad really is in port 0. The Trilogy
// disc's art is the Wii's, so there every action gets its GameCube icon.
std::string GameCubeStemForButton(PADButton button) {
  const std::string fallback = port::entry::IsTrilogy(port::entry::CurrentDiscVersion())
                                   ? GameCubeStemForAction(button)
                                   : std::string();
  const PADControllerType type = PADGetControllerType(PAD_CHAN0);
  const GameCubeButton* table = nullptr;
  size_t tableSize = 0;
  if (type == PAD_TYPE_GAMECUBE) {
    table = kGameCubeButtons;
    tableSize = sizeof(kGameCubeButtons) / sizeof(kGameCubeButtons[0]);
  } else if (type == PAD_TYPE_NSO_GAMECUBE) {
    table = kNsoGameCubeButtons;
    tableSize = sizeof(kNsoGameCubeButtons) / sizeof(kNsoGameCubeButtons[0]);
  } else {
    return fallback;
  }
  const int native = static_cast<int>(PadNativeForButton(button));
  for (size_t j = 0; j < tableSize; ++j) {
    if (table[j].sdlButton == native) {
      return table[j].action == button ? fallback : std::string(table[j].stem);
    }
  }
  return fallback;
}

// The SDL button a mapping points at, named the way the generated pad icons
// are (tools/make_prompt_glyphs.py writes "<device>_<suffix>").
const char* SuffixForSdlButton(int button) {
  switch (button) {
  case SDL_GAMEPAD_BUTTON_SOUTH: return "south";
  case SDL_GAMEPAD_BUTTON_EAST: return "east";
  case SDL_GAMEPAD_BUTTON_WEST: return "west";
  case SDL_GAMEPAD_BUTTON_NORTH: return "north";
  case SDL_GAMEPAD_BUTTON_START: return "start";
  case SDL_GAMEPAD_BUTTON_BACK: return "back";
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "leftshoulder";
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "rightshoulder";
  case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "leftstick";
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "rightstick";
  case SDL_GAMEPAD_BUTTON_DPAD_UP: return "dpad_up";
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "dpad_down";
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "dpad_left";
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "dpad_right";
  case PAD_NATIVE_BUTTON_TRIGGER_LEFT: return "lt";
  case PAD_NATIVE_BUTTON_TRIGGER_RIGHT: return "rt";
  default: return nullptr;
  }
}

struct Registration {
  std::string iconPath;
  std::string activeStem;
  aurora::texture::ReplacementRegistration handle{};
  bool registered = false;
};
Registration sRegistrations[kKeyCount];
std::string sBindingsDir;
bool sEnabled = false;

// Which input the player last used. The prompts, and the static texture set
// (PortTextures asks ActiveDevice too), follow that rather than whatever happens
// to be plugged in, so switching between a keyboard, a pad and the touch overlay
// swaps the icons over. Pad is the starting state: with no pad connected the pad
// device resolves to "keyboard" anyway.
enum class ActiveInput { Pad, Keyboard, TouchXbox, TouchGameCube };
std::atomic< ActiveInput > sActiveInput{ActiveInput::Pad};
// Within Keyboard, whether a key rather than the mouse was used last. Prime 1
// shows A and B as clicks on a screen its pointer drives; Echoes has no such
// screens yet, so this is only kept up to date.
std::atomic< bool > sKeyInput{false};

// Keys sent by the phone or the OS rather than a keyboard in the player's hands.
bool IsSystemKey(SDL_Scancode scancode) {
  switch (scancode) {
  case SDL_SCANCODE_AC_BACK:
  case SDL_SCANCODE_VOLUMEUP:
  case SDL_SCANCODE_VOLUMEDOWN:
  case SDL_SCANCODE_MUTE:
  case SDL_SCANCODE_MEDIA_PLAY:
  case SDL_SCANCODE_MEDIA_PAUSE:
  case SDL_SCANCODE_MEDIA_PLAY_PAUSE:
  case SDL_SCANCODE_MEDIA_NEXT_TRACK:
  case SDL_SCANCODE_MEDIA_PREVIOUS_TRACK:
  case SDL_SCANCODE_MEDIA_STOP:
  case SDL_SCANCODE_POWER:
    return true;
  default:
    return false;
  }
}

bool IsTouchMouse(SDL_MouseID which) { return which == SDL_TOUCH_MOUSEID || which == SDL_PEN_MOUSEID; }

// Only deliberate input switches the device. The touch overlay is itself a
// virtual pad, whose events would undo NoteTouchInput on the next pump; a touch
// or pen also arrives as a mouse; and a stick resting a little off centre would
// flip back to the pad every frame while the keyboard is in use, and each flip
// re-registers textures and clears Aurora's texture cache. Mouse motion is not
// deliberate either: aiming with the mouse while playing on a pad (or a Steam
// Deck trackpad or gyro sent as a mouse) would swap the icons back and forth,
// so it never leaves the pad; it only turns keys into clicks once the keyboard
// is already in use.
bool SDLCALL active_input_watch(void*, SDL_Event* event) {
  switch (event->type) {
  case SDL_EVENT_MOUSE_MOTION:
    if (!IsTouchMouse(event->motion.which) &&
        sActiveInput.load(std::memory_order_relaxed) == ActiveInput::Keyboard) {
      sKeyInput.store(false, std::memory_order_relaxed);
    }
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (!IsTouchMouse(event->button.which)) {
      sActiveInput.store(ActiveInput::Keyboard, std::memory_order_relaxed);
      sKeyInput.store(false, std::memory_order_relaxed);
    }
    break;
  case SDL_EVENT_MOUSE_WHEEL:
    if (!IsTouchMouse(event->wheel.which)) {
      sActiveInput.store(ActiveInput::Keyboard, std::memory_order_relaxed);
      sKeyInput.store(false, std::memory_order_relaxed);
    }
    break;
  case SDL_EVENT_KEY_DOWN:
    // Not TEXT_INPUT: on Android that is the soft keyboard typing into the
    // overlay, not a physical keyboard.
    if (!event->key.repeat && !IsSystemKey(event->key.scancode)) {
      sActiveInput.store(ActiveInput::Keyboard, std::memory_order_relaxed);
      sKeyInput.store(true, std::memory_order_relaxed);
    }
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    if (!SDL_IsJoystickVirtual(event->gbutton.which)) {
      sActiveInput.store(ActiveInput::Pad, std::memory_order_relaxed);
    }
    break;
  case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    // The same threshold the touch overlay uses to decide a pad is in use.
    if (std::abs(static_cast< int >(event->gaxis.value)) > 16000 &&
        !SDL_IsJoystickVirtual(event->gaxis.which)) {
      sActiveInput.store(ActiveInput::Pad, std::memory_order_relaxed);
    }
    break;
  case SDL_EVENT_GAMEPAD_ADDED:
    if (!SDL_IsJoystickVirtual(event->gdevice.which)) {
      sActiveInput.store(ActiveInput::Pad, std::memory_order_relaxed);
    }
    break;
  default:
    break;
  }
  return true;
}

// True when the icons are served from the executable (PortEmbedded) rather than
// <textures>/bindings; sBindingsDir is then the embedded folder prefix.
bool sEmbedded = false;

// The whole file at `path`: an embedded entry, else a file on disk.
bool ReadIconFile(const std::string& path, std::vector<uint8_t>& out) {
  if (sEmbedded) {
    const std::span<const uint8_t> bytes = PortEmbedded::Find(path);
    out.assign(bytes.begin(), bytes.end());
    return !out.empty();
  }
  std::ifstream in(PortUtf8::FromUtf8(path), std::ios::binary);
  if (!in) {
    return false;
  }
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size <= 0) {
    return false;
  }
  in.seekg(0, std::ios::beg);
  out.resize(static_cast<size_t>(size));
  in.read(reinterpret_cast<char*>(out.data()), size);
  return !in.fail();
}

// The file `name` in the bindings folder, or an empty string when it is not there.
std::string FindIconFile(const std::string& name) {
  if (sEmbedded) {
    std::string path = sBindingsDir + "/" + name;
    return PortEmbedded::Find(path).empty() ? std::string() : path;
  }
  const std::filesystem::path path = PortUtf8::FromUtf8(sBindingsDir) / PortUtf8::FromUtf8(name);
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec) ? PortUtf8::ToUtf8(path) : std::string();
}

// The icon for `stem`: the one generated for this texture's size, then the unsuffixed one. A
// sized file is drawn at any whole multiple of that size (IconFitsBound), so "for this size" is
// the game slot the file is named for, not the file's own dimensions.
// A single stem can serve textures of different sizes - the C-stick prompts here are one
// 32x32 and one 64x32 - and a mismatch is not scaled by the game: it reads 64x32 of pixels
// out of 32x32 of data and draws the right-hand half as noise. tools/make_prompt_glyphs.py
// writes the sized variants for exactly this reason.
std::string FindIconForSize(const std::string& stem, uint32_t width, uint32_t height) {
  char sized[160];
  std::snprintf(sized, sizeof(sized), "%s_%ux%u.dds", stem.c_str(), width, height);
  std::string path = FindIconFile(sized);
  return path.empty() ? FindIconFile(stem + ".dds") : path;
}

// The width and height a DDS header declares, or false if the file is too short
// to hold one. A DDS opens with the "DDS " magic then a 124-byte header, of
// which the height and width are the two little-endian uint32 at offset 12.
bool IconDimensions(const std::string& path, uint32_t& width, uint32_t& height) {
  std::vector<uint8_t> bytes;
  if (!ReadIconFile(path, bytes) || bytes.size() < 20 || std::memcmp(bytes.data(), "DDS ", 4) != 0) {
    return false;
  }
  uint32_t both[2] = {};
  std::memcpy(both, bytes.data() + 12, sizeof(both));
  height = both[0];
  width = both[1];
  return true;
}

// Port: whether `iconWidth` x `iconHeight` may stand in for a `width` x `height` texture. Any
// whole multiple of the texture's size is accepted, the same on both axes: the icons are drawn
// larger than the game's art so they stay sharp on a scaled-up HUD (#229, from Prime 1), and a
// file name's `_64x32` suffix is the game texture it serves, not the file's own size. The game
// does not scale, so a file that is not a whole multiple reads past its data as noise.
bool IconFitsBound(uint32_t iconWidth, uint32_t iconHeight, uint32_t width, uint32_t height) {
  if (width == 0 || height == 0 || iconWidth < width || iconWidth % width != 0) {
    return false;
  }
  // The same aspect ratio, so a 64x32 texture takes 128x64 art and not 128x128: one whole
  // multiple on the width and on the height.
  return iconHeight * width == iconWidth * height;
}

// The read callback's userData. Aurora worker threads can still be reading an
// icon after Apply has unregistered it and moved on to another path, so each
// path gets storage that is never changed or freed. Only the main thread adds
// to the set, and there are only as many entries as distinct icon files.
const std::string* StablePath(const std::string& path) {
  static std::set<std::string> sPaths;
  return &*sPaths.insert(path).first;
}

// Serves one generated icon as if it were a replacement file. Called from
// Aurora worker threads, so it only touches the filesystem.
bool ReadIconBytes(void* userData, const char* path, std::vector<uint8_t>& out) {
  // The prompts have a single mip, so any mip sidecar Aurora probes
  // for is a miss rather than the same image again.
  if (path != nullptr && std::strstr(path, "_mip") != nullptr) {
    return false;
  }
  return ReadIconFile(*static_cast<const std::string*>(userData), out);
}

const char* StemForScancode(int scancode) {
  for (const KeyIcon& icon : kKeyIcons) {
    if (icon.scancode == scancode) {
      return icon.stem;
    }
  }
  return nullptr;
}

// A keyboard-family input as the scancode the icon table uses (a mouse button
// as Aurora's PAD_KEY_MOUSE_*), or PAD_KEY_INVALID.
int ScancodeForInput(const PortInput::Input& in) {
  if (in.device == PortInput::Device::Key) {
    return in.code;
  }
  if (in.device == PortInput::Device::MouseButton) {
    // Each run of mouse codes (PortInputDevices::kMouse*) is SDL's left, middle,
    // right, X1, X2.
    const int button = in.code % PortInputDevices::kMouseKeySlot;
    return button <= PAD_KEY_MOUSE_LEFT - PAD_KEY_MOUSE_X2 ? PAD_KEY_MOUSE_LEFT - button : PAD_KEY_INVALID;
  }
  return PAD_KEY_INVALID;
}

// The key (or mouse button) bound to `button`, or PAD_KEY_INVALID.
int KeyForButton(PADButton button) {
  PortInput::Input in;
  return PortInputDevices::PromptButtonInput(button, PortInput::Family::Keyboard, in) ? ScancodeForInput(in)
                                                                                       : PAD_KEY_INVALID;
}

// The key that pushes `axis`, or PAD_KEY_INVALID.
int KeyForAxis(PADAxis axis) {
  PortInput::Input in;
  return PortInputDevices::PromptAxisInput(axis, PortInput::Family::Keyboard, in) ? ScancodeForInput(in)
                                                                                  : PAD_KEY_INVALID;
}

// A whole stick or D-pad on keys: the cluster icon when the four keys are one
// the pack draws, else the up key's own icon, as the nearest single thing to
// name. Keys are up, down, left, right.
std::string KeyClusterStem(const int (&keys)[4]) {
  struct Cluster {
    int keys[4];
    const char* stem;
  };
  static constexpr Cluster kClusters[] = {
      {{SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D}, "keyboard_wasd"},
      {{SDL_SCANCODE_I, SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L}, "keyboard_ijkl"},
      {{SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT}, "keyboard_arrows"},
  };
  for (const Cluster& cluster : kClusters) {
    if (std::equal(std::begin(keys), std::end(keys), std::begin(cluster.keys))) {
      return cluster.stem;
    }
  }
  const char* up = StemForScancode(keys[0]);
  return up != nullptr ? std::string(up) : std::string();
}

// The icon stem for whatever is bound to `button` on `device`, or empty when
// the port has no icon for it (in which case the static set stays in place).
// Both follow the bindings in use (PortInputDevices::PromptButtonInput), so a
// remapped button shows the key or button it was remapped to.
std::string IconStemForButton(PADButton button, const char* device) {
  if (std::strcmp(device, "gamecube") == 0) {
    return GameCubeStemForButton(button);
  }
  if (std::strcmp(device, "keyboard") == 0) {
    const char* stem = StemForScancode(KeyForButton(button));
    return stem != nullptr ? std::string(stem) : std::string();
  }

  // A pad's L and R are analog triggers unless remapped to a button (a preset
  // puts R on a stick click); the generated set carries the triggers under one
  // name per action.
  const bool trigger = button == PAD_TRIGGER_L || button == PAD_TRIGGER_R;
  const std::string triggerStem = std::string(device) + (button == PAD_TRIGGER_L ? "_lt" : "_rt");
  const char* suffix = SuffixForSdlButton(static_cast<int>(PadNativeForButton(button)));
  if (suffix != nullptr) {
    return std::string(device) + "_" + suffix;
  }
  return trigger ? triggerStem : std::string();
}

// The pad axes a stick prompt stands for, in up, down, left, right order.
constexpr PADAxis kStickAxes[4] = {PAD_AXIS_LEFT_Y_POS, PAD_AXIS_LEFT_Y_NEG, PAD_AXIS_LEFT_X_NEG,
                                   PAD_AXIS_LEFT_X_POS};
constexpr PADAxis kCStickAxes[4] = {PAD_AXIS_RIGHT_Y_POS, PAD_AXIS_RIGHT_Y_NEG, PAD_AXIS_RIGHT_X_NEG,
                                    PAD_AXIS_RIGHT_X_POS};

// The icon for one direction of a pad axis: the stick and the way it is pushed,
// or the button driving the axis. With `whole`, the stick itself.
std::string PadStemForAxis(PADAxis axis, const char* device, bool whole) {
  PortInput::Input in;
  if (!PortInputDevices::PromptAxisInput(axis, PortInput::Family::Pad, in)) {
    return {};
  }
  if (in.device == PortInput::Device::PadAxis) {
    const int native = in.code;
    const bool leftStick = native == SDL_GAMEPAD_AXIS_LEFTX || native == SDL_GAMEPAD_AXIS_LEFTY;
    const bool rightStick = native == SDL_GAMEPAD_AXIS_RIGHTX || native == SDL_GAMEPAD_AXIS_RIGHTY;
    if (!leftStick && !rightStick) {
      return {};  // a trigger driving a stick: no art for that
    }
    std::string stem = std::string(device) + (leftStick ? "_stick_l" : "_stick_r");
    if (whole || in.dir == 0) {
      return stem;
    }
    // SDL's Y axis is positive downwards.
    const bool positive = in.dir > 0;
    const bool vertical = native == SDL_GAMEPAD_AXIS_LEFTY || native == SDL_GAMEPAD_AXIS_RIGHTY;
    return stem + (vertical ? (positive ? "_down" : "_up") : (positive ? "_right" : "_left"));
  }
  const char* suffix = SuffixForSdlButton(static_cast<int>(PortInputDevices::PromptNativeButton(in)));
  if (suffix == nullptr) {
    return {};
  }
  // A stick moved onto the D-pad reads as the D-pad; any other set of
  // buttons has no single icon.
  if (whole) {
    return std::strncmp(suffix, "dpad_", 5) == 0 ? std::string(device) + "_dpad" : std::string();
  }
  return std::string(device) + "_" + suffix;
}

// The icon for a stick prompt: the whole stick, or one way (dir 0-3 = up, down,
// left, right; -1 = the whole stick).
std::string IconStemForStick(const PADAxis (&axes)[4], int dir, const char* device) {
  if (std::strcmp(device, "gamecube") == 0) {
    return {};  // the game's stick art is the GameCube's
  }
  if (std::strcmp(device, "keyboard") == 0) {
    if (dir >= 0) {
      const char* stem = StemForScancode(KeyForAxis(axes[dir]));
      return stem != nullptr ? std::string(stem) : std::string();
    }
    const int keys[4] = {KeyForAxis(axes[0]), KeyForAxis(axes[1]), KeyForAxis(axes[2]),
                         KeyForAxis(axes[3])};
    return KeyClusterStem(keys);
  }
  // The whole stick is named by its up direction's mapping.
  return PadStemForAxis(axes[dir >= 0 ? dir : 0], device, dir < 0);
}

std::string IconStemForDpad(const char* device) {
  if (std::strcmp(device, "gamecube") == 0) {
    return {};
  }
  if (std::strcmp(device, "keyboard") == 0) {
    const int keys[4] = {KeyForButton(PAD_BUTTON_UP), KeyForButton(PAD_BUTTON_DOWN),
                         KeyForButton(PAD_BUTTON_LEFT), KeyForButton(PAD_BUTTON_RIGHT)};
    return KeyClusterStem(keys);
  }
  // The pad's D-pad unless up has moved elsewhere, in which case that button.
  const std::string up = IconStemForButton(PAD_BUTTON_UP, device);
  const std::string dpadUp = std::string(device) + "_dpad_up";
  return up == dpadUp ? std::string(device) + "_dpad" : up;
}

// The touch twin-stick layout's hints. The overlay is no pad, so the real pad's mappings
// (PADGetButtonMappings) mean nothing here: this is what the overlay sends for each GameCube
// action, in Remastered's Dual Sticks positions. Z is the Map pill (Menu glyph) and Start the
// Pause button (View glyph); the overlay draws those glyphs on them. The D-pad picks visors, and
// with the Beam button (Y) held the beams, so the C-stick's beam hints show the D-pad direction
// that picks them (the pad preset shows the right stick there, which twin stick consumes).
std::string TouchTwinStemForPrompt(uint32_t prompt) {
  switch (prompt) {
  case PROMPT_STICK: return "xbox_stick_l";
  case PROMPT_STICK_UP: return "xbox_stick_l_up";
  case PROMPT_STICK_DOWN: return "xbox_stick_l_down";
  case PROMPT_STICK_LEFT: return "xbox_stick_l_left";
  case PROMPT_STICK_RIGHT: return "xbox_stick_l_right";
  case PROMPT_CSTICK: return "xbox_dpad";
  case PROMPT_CSTICK_UP: return "xbox_dpad_up";
  case PROMPT_CSTICK_DOWN: return "xbox_dpad_down";
  case PROMPT_CSTICK_LEFT: return "xbox_dpad_left";
  case PROMPT_CSTICK_RIGHT: return "xbox_dpad_right";
  case PROMPT_DPAD: return "xbox_dpad";
  case PAD_BUTTON_UP: return "xbox_dpad_up";
  case PAD_BUTTON_DOWN: return "xbox_dpad_down";
  case PAD_BUTTON_LEFT: return "xbox_dpad_left";
  case PAD_BUTTON_RIGHT: return "xbox_dpad_right";
  case PAD_BUTTON_A: return "xbox_rt";
  case PAD_BUTTON_B: return "xbox_south";
  case PAD_BUTTON_X: return "xbox_west";
  case PAD_BUTTON_Y: return "xbox_rightshoulder";
  case PAD_TRIGGER_L: return "xbox_lt";
  case PAD_TRIGGER_R: return "xbox_rightstick";
  case PAD_TRIGGER_Z: return "xbox_start";
  case PAD_BUTTON_START: return "xbox_back";
  default: return {};
  }
}

std::string IconStemForPrompt(uint32_t prompt, const char* device) {
  if (sActiveInput.load(std::memory_order_relaxed) == ActiveInput::TouchXbox &&
      std::strcmp(device, "xbox") == 0) {
    return TouchTwinStemForPrompt(prompt);
  }
  switch (prompt) {
  case PROMPT_STICK: return IconStemForStick(kStickAxes, -1, device);
  case PROMPT_STICK_UP: return IconStemForStick(kStickAxes, 0, device);
  case PROMPT_STICK_DOWN: return IconStemForStick(kStickAxes, 1, device);
  case PROMPT_STICK_LEFT: return IconStemForStick(kStickAxes, 2, device);
  case PROMPT_STICK_RIGHT: return IconStemForStick(kStickAxes, 3, device);
  case PROMPT_CSTICK: return IconStemForStick(kCStickAxes, -1, device);
  case PROMPT_CSTICK_UP: return IconStemForStick(kCStickAxes, 0, device);
  case PROMPT_CSTICK_DOWN: return IconStemForStick(kCStickAxes, 1, device);
  case PROMPT_CSTICK_LEFT: return IconStemForStick(kCStickAxes, 2, device);
  case PROMPT_CSTICK_RIGHT: return IconStemForStick(kCStickAxes, 3, device);
  case PROMPT_DPAD: return IconStemForDpad(device);
  default: return IconStemForButton(static_cast<PADButton>(prompt), device);
  }
}

void Apply(size_t index, const std::string& stem) {
  Registration& reg = sRegistrations[index];
  if (reg.registered) {
    aurora::texture::unregister_replacement(reg.handle);
    reg.handle = {};
    reg.registered = false;
  }
  reg.activeStem = stem;
  if (stem.empty()) {
    return;
  }

  const PromptKey& key = kKeys[index];
  char keyName[80];
  std::snprintf(keyName, sizeof(keyName), "tex1_%ux%u_%016llx_%s.dds", key.width, key.height,
                static_cast<unsigned long long>(key.hash), key.format);
  reg.iconPath = FindIconForSize(stem, key.width, key.height);
  if (reg.iconPath.empty()) {
    // Nothing generated for this action, so nothing is registered. Note that
    // what stays on screen is not necessarily "the game's own art": the static
    // per-device set in <textures>/<device>/ is registered separately, by
    // filename, so if it has a file for this texture then that is what shows -
    // art for the default bindings. Saying "the game's own art" here would be
    // wrong in the common case.
    //
    // activeStem is deliberately NOT cleared. Poll() skips a key whose stem
    // already matches, so clearing it made Poll re-run Apply on this key every
    // single frame: a stat, a DDS open and a stderr line, 60 times a second,
    // for the rest of the session. Two runs of the map screen logged 4200 of
    // them. Leaving the stem recorded means one attempt and then silence.
    return;
  }
  // Last line of defence, in case a sized file is ever wrong: refuse art that is
  // not a whole multiple of the texture it is standing in for. As above,
  // activeStem is left recorded so Poll does not retry, and as above what
  // remains on screen is the static device set if it has one, not necessarily
  // the game's own art.
  uint32_t iconWidth = 0;
  uint32_t iconHeight = 0;
  if (!IconDimensions(reg.iconPath, iconWidth, iconHeight) ||
      !IconFitsBound(iconWidth, iconHeight, key.width, key.height)) {
    return;
  }
  // Above the static device set (priority 0) and the user's texture pack (1).
  // At equal priority the newest registration wins, so a reload of the static
  // set (a pad unplugged, say) buried the binding icons, and Poll does not
  // re-apply a stem that has not changed.
  reg.handle = aurora::texture::register_virtual_replacement(
      keyName, aurora::texture::VirtualFileSource{&ReadIconBytes, const_cast<std::string*>(StablePath(reg.iconPath))},
      aurora::texture::ReplacementOptions{.priority = 2});
  reg.registered = reg.handle.id != 0;
}

// A texture bound by asset id (BindTexture): its icon is registered under the
// CTexture's own pointer, so it holds whatever the texels are.
struct BoundPrompt {
  uint32_t prompt;
  uint32_t width;
  uint32_t height;
  aurora::texture::ReplacementRegistration handle{};
  bool registered = false;
};
std::unordered_map<const void*, BoundPrompt> sBound;
// Each action's stem as Poll last resolved it, by kActions index.
std::string sActionStems[std::size(kActions)];

const std::string& ActionStem(uint32_t prompt) {
  for (size_t i = 0; i < std::size(kActions); ++i) {
    if (kActions[i].prompt == prompt) {
      return sActionStems[i];
    }
  }
  static const std::string kNone;
  return kNone;
}

void ApplyBound(const void* owner, BoundPrompt& bound, const std::string& stem) {
  if (bound.registered) {
    aurora::texture::unregister_replacement(bound.handle);
    bound.handle = {};
    bound.registered = false;
  }
  if (stem.empty()) {
    return;
  }
  const std::string iconPath = FindIconForSize(stem, bound.width, bound.height);
  uint32_t iconWidth = 0;
  uint32_t iconHeight = 0;
  if (iconPath.empty() || !IconDimensions(iconPath, iconWidth, iconHeight) ||
      !IconFitsBound(iconWidth, iconHeight, bound.width, bound.height)) {
    return;
  }
  const std::string* path = StablePath(iconPath);
  // Above the user's texture pack (priority 1) and the static device set (0): a remapped
  // action still shows the key or button it is bound to.
  bound.handle = aurora::texture::register_virtual_replacement(
      aurora::texture::TexturePointerKey{owner}, *path,
      aurora::texture::VirtualFileSource{&ReadIconBytes, const_cast<std::string*>(path)},
      aurora::texture::ReplacementOptions{.priority = 2});
  bound.registered = bound.handle.id != 0;
}
} // namespace

namespace PortPrompts {
// "gamecube" has no static set, which is deliberate: the game's own prompt art
// already is the GameCube set, so only a remapped GC pad button gets an icon
// (GameCubeStemForButton).
const char* ActiveDevice() {
  // Read once: it is asked every frame, and nothing sets it while the game runs.
  static const char* const env = port::EnvString("MP2_TEXTURE_DEVICE");
  if (env != nullptr && env[0] != '\0') {
    return env;
  }
  if (const char* forced = PortOverlay::PromptGlyphs()) {
    return forced;
  }
  switch (sActiveInput.load(std::memory_order_relaxed)) {
  case ActiveInput::Keyboard:
    return "keyboard";
  case ActiveInput::TouchXbox:
    return "xbox";
  case ActiveInput::TouchGameCube:
    return "gamecube";
  case ActiveInput::Pad:
  default:
    return PortTextures::PadDeviceName();
  }
}

const char* ConfirmLabel() {
  static std::string label;
  const char* device = ActiveDevice();
  if (std::strcmp(device, "keyboard") == 0) {
    const int key = KeyForButton(PAD_BUTTON_A);
    if (key >= 0) {
      if (const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(key)); name != nullptr && name[0] != '\0') {
        label = name;
        return label.c_str();
      }
    }
    return "A";
  }
  if (std::strcmp(device, "xbox") == 0 || std::strcmp(device, "gamecube") == 0) {
    return "A";
  }
  // A pad's mapping is positional: the game's A usually sits on the south button.
  u32 count = 0;
  int native = SDL_GAMEPAD_BUTTON_SOUTH;
  if (const PADButtonMapping* list = PADGetButtonMappings(0, &count)) {
    for (u32 i = 0; i < count; ++i) {
      if (list[i].padButton == PAD_BUTTON_A) {
        native = static_cast<int>(list[i].nativeButton);
      }
    }
  }
  const bool ps = std::strcmp(device, "playstation") == 0;
  const bool nintendo = std::strcmp(device, "switch") == 0;
  switch (native) {
  case SDL_GAMEPAD_BUTTON_EAST: return ps ? "Circle" : nintendo ? "A" : "B";
  case SDL_GAMEPAD_BUTTON_WEST: return ps ? "Square" : nintendo ? "Y" : "X";
  case SDL_GAMEPAD_BUTTON_NORTH: return ps ? "Triangle" : nintendo ? "X" : "Y";
  default: return ps ? "Cross" : nintendo ? "B" : "A";
  }
}

std::string ConfirmIconStem() {
  if (!sEnabled) {
    return {};
  }
  const char* device = ActiveDevice();
  std::string stem = IconStemForButton(PAD_BUTTON_A, device);
  // A GameCube pad on its default mapping leaves the game's own art (empty); here there is none.
  if (stem.empty() && std::strcmp(device, "gamecube") == 0) {
    stem = GameCubeStemForAction(PAD_BUTTON_A);
  }
  return stem;
}

bool LoadIcon(const std::string& stem, std::vector< uint8_t >& rgba, uint32_t& width, uint32_t& height) {
  constexpr size_t kHeader = 128; // "DDS " + the 124-byte header (tools/make_prompt_glyphs.py)
  const std::string path = stem.empty() ? std::string() : FindIconFile(stem + ".dds");
  std::vector< uint8_t > bytes;
  if (path.empty() || !IconDimensions(path, width, height) || width == 0 || height == 0 ||
      !ReadIconFile(path, bytes)) {
    return false;
  }
  const size_t row = size_t(width) * 4;
  if (bytes.size() < kHeader + row * height) {
    return false;
  }
  // The DDS is stored bottom-up.
  rgba.resize(row * height);
  for (uint32_t y = 0; y < height; ++y) {
    std::memcpy(rgba.data() + row * y, bytes.data() + kHeader + row * (height - 1 - y), row);
  }
  return true;
}

void Initialize(const char* textureRoot) {
  // Tracked even without generated icons, since the static set and rumble
  // (KeyboardActive) follow it too.
  SDL_AddEventWatch(active_input_watch, nullptr);
  const bool embedded = textureRoot == nullptr && !PortEmbedded::Under("textures/bindings/").empty();
  if (embedded) {
    sBindingsDir = "textures/bindings";
    sEmbedded = true;
  } else {
    if (textureRoot == nullptr || textureRoot[0] == '\0') {
      std::fprintf(stderr, "[prompts] no texture folder; binding icons off\n");
      return;
    }
    const std::filesystem::path bindingsDir = PortUtf8::FromUtf8(textureRoot) / "bindings";
    std::error_code ec;
    if (!std::filesystem::is_directory(bindingsDir, ec)) {
      std::fprintf(stderr, "[prompts] no %s; binding icons off\n", PortUtf8::ToUtf8(bindingsDir).c_str());
      return;
    }
    sBindingsDir = PortUtf8::ToUtf8(bindingsDir);
  }
  sEnabled = true;
  std::fprintf(stderr, "[prompts] binding icons from %s\n",
               embedded ? "the executable (built in)" : sBindingsDir.c_str());
  Poll();
}

void Poll() {
  if (!sEnabled) {
    return;
  }
  const char* device = ActiveDevice();
  // Resolving every action's binding builds strings and rescans the bindings, so it runs when
  // the device changes and otherwise a few times a second, which still follows a rebind.
  static std::string sLastDevice;
  static uint64_t sLastResolveNs = 0;
  const uint64_t nowNs = SDL_GetTicksNS();
  const bool deviceChanged = sLastDevice != device;
  if (!deviceChanged && sLastResolveNs != 0 && nowNs - sLastResolveNs < 250000000ull) {
    return;
  }
  sLastDevice = device;
  sLastResolveNs = nowNs != 0 ? nowNs : 1;
  for (size_t a = 0; a < std::size(kActions); ++a) {
    const PromptAction& action = kActions[a];
    const std::string stem = IconStemForPrompt(action.prompt, device);
    size_t applied = 0;
    if (stem != sActionStems[a]) {
      sActionStems[a] = stem;
      for (auto& [owner, bound] : sBound) {
        if (bound.prompt == action.prompt) {
          ApplyBound(owner, bound, stem);
          ++applied;
        }
      }
    }
    for (size_t i = 0; i < kKeyCount; ++i) {
      if (kKeys[i].prompt != action.prompt) {
        continue;
      }
      const std::string& current = sRegistrations[i].activeStem;
      if (stem.empty() && current.empty()) {
        continue;
      }
      if (!stem.empty() && stem == current) {
        continue;
      }
      Apply(i, stem);
      ++applied;
    }
    if (applied != 0) {
      std::fprintf(stderr, "[prompts] %s %s\n", action.label,
                     stem.empty() ? "(back to the static icon)" : stem.c_str());
    }
  }
}

bool IsPromptAsset(uint32_t id) { return FindAsset(id) != nullptr; }

bool BindTexture(const void* owner, uint32_t id, uint32_t width, uint32_t height) {
  const PromptAsset* asset = FindAsset(id);
  if (asset == nullptr) {
    return false;
  }
  uint32_t prompt = asset->prompt;
  if (id == kTrilogyZAsset && port::entry::IsTrilogy(port::entry::CurrentDiscVersion())) {
    prompt = PAD_TRIGGER_Z;
  }
  BoundPrompt& bound = sBound[owner];
  if (bound.registered) {
    aurora::texture::unregister_replacement(bound.handle);
  }
  bound = BoundPrompt{prompt, width, height};
  if (sEnabled) {
    // Textures stream in after Poll ran: give this one the action's current icon.
    const std::string& stem = ActionStem(prompt);
    if (!stem.empty()) {
      ApplyBound(owner, bound, stem);
    }
  }
  return true;
}

void UnbindTexture(const void* owner) {
  const auto it = sBound.find(owner);
  if (it == sBound.end()) {
    return;
  }
  if (it->second.registered) {
    aurora::texture::unregister_replacement(it->second.handle);
  }
  sBound.erase(it);
}
bool KeyboardActive() { return sActiveInput.load(std::memory_order_relaxed) == ActiveInput::Keyboard; }

void NoteTouchInput(bool xboxLayout) {
  sActiveInput.store(xboxLayout ? ActiveInput::TouchXbox : ActiveInput::TouchGameCube,
                     std::memory_order_relaxed);
}
} // namespace PortPrompts
