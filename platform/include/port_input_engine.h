#pragma once

// The binding engine: raw device state -> bindings -> actions. Every control the
// port has (pad buttons, keys, mouse, the touch overlay, the console's virtual
// pad) is an Input; a Binding maps an input or a chord of inputs to an Action,
// with a trigger (press, tap, hold, double tap, toggle) and optional turbo. The
// engine is plain data and arithmetic over a RawState snapshot and a time stamp,
// so it is tested without SDL or the game (tests/port_input_engine.cpp).
//
// Chord rule (one profile, the active contexts):
// - A satisfied chord wins over its members' own bindings and consumes those
//   inputs until each is released, so releasing one member of "LB + A" doesn't
//   leave LB alone firing.
// - An ordered chord (the default) is the trigger, its last input, pressed while
//   the others (modifiers) are held. An any-order chord is all its inputs pressed
//   within the chord window of each other.
// - A binding whose inputs could still grow into a bound chord (they are the
//   modifiers of an ordered chord, or part of an any-order one) waits the chord
//   window before it fires; released before then, it fires for one poll.
// - Everything else fires on the poll its inputs go down.
// - Where bindings share an input, a layer-only binding outranks a context one,
//   which outranks one bound everywhere: while the higher one uses the input,
//   the lower ones don't see it.
// - On a context change, a held input whose actions differ in the new context is
//   consumed until released (the A that closed the map doesn't jump).

#include <array>
#include <bitset>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace PortInput {

enum class Device : uint8_t {
  None,
  Key,         // code: SDL scancode
  MouseButton, // code: 0 left, 1 middle, 2 right, 3 x1, 4 x2 (the port maps more ids above)
  MouseWheel,  // code: 0 vertical, 1 horizontal; dir picks the way
  MouseMotion, // code: 0 x, 1 y; dir 0 = signed delta
  PadButton,   // code: SDL_GamepadButton
  PadAxis,     // code: SDL_GamepadAxis; dir 0 = signed value
  Touch,       // code: touch overlay control id
  TouchAxis,   // code: touch overlay axis id (stick/look), dir as PadAxis
  Gyro,        // code: 0 pitch, 1 yaw, 2 roll (rad/s); dir as PadAxis
};

constexpr int kKeyCount = 512;
constexpr int kMouseButtonCount = 32;
constexpr int kPadButtonCount = 32;
constexpr int kPadAxisCount = 8;
constexpr int kTouchCount = 128;
constexpr int kTouchAxisCount = 8;

// One physical input. dir: 0 the whole axis (analog, signed), +1/-1 one half of
// it. threshold: where an axis counts as pressed, in percent of full travel.
struct Input {
  Device device = Device::None;
  int8_t dir = 0;
  uint8_t threshold = 50;
  uint16_t code = 0;

  bool operator==(const Input&) const = default;
  bool Valid() const { return device != Device::None; }
  bool Analog() const;
};

// Everything the devices report for one poll. Axes are -1..1 after deadzones
// (pad triggers 0..1); mouse motion and wheel are this poll's deltas.
struct RawState {
  std::bitset<kKeyCount> keys;
  uint32_t mouseButtons = 0;
  float wheel[2] = {};
  float mouseDelta[2] = {};
  uint32_t padButtons = 0;
  float padAxes[kPadAxisCount] = {};
  std::bitset<kTouchCount> touch;
  float touchAxes[kTouchAxisCount] = {};
  float gyro[3] = {};

  // The input's value: 0/1 for buttons, the signed value for a whole axis, the
  // clamped-at-0 half for a directed one.
  float Value(const Input& in) const;
  bool Down(const Input& in) const;
};

// Gameplay contexts and user layers, as bits. A binding applies when its mask
// meets the active one.
enum Context : uint16_t {
  kCtxGameplay = 1 << 0,
  kCtxMorphBall = 1 << 1,
  kCtxMap = 1 << 2,
  kCtxMenu = 1 << 3,
  kCtxScan = 1 << 4,
  // The scan text (the scan freeze), on top of gameplay and scan. Port-internal: no remap
  // page lists it; with separate menu buttons only Menu A applies there (issue #11).
  kCtxScanText = 1 << 5,
  kCtxLayer1 = 1 << 8,
  kCtxLayer2 = 1 << 9,
  kCtxLayer3 = 1 << 10,
  kCtxLayer4 = 1 << 11,
  kCtxLayers = 0x0f00,
  kCtxAll = 0xffff,
};

enum class ActionKind : uint8_t {
  Digital,  // 0/1
  HalfAxis, // 0..1, the strongest binding wins
  Delta,    // signed, summed over bindings (mouse look)
};

#define PORT_INPUT_ACTIONS(X)                                                                      \
  X(None, Digital, "none", "None")                                                                 \
  X(PadA, Digital, "pad_a", "A")                                                                   \
  X(PadB, Digital, "pad_b", "B")                                                                   \
  X(PadX, Digital, "pad_x", "X")                                                                   \
  X(PadY, Digital, "pad_y", "Y")                                                                   \
  X(PadZ, Digital, "pad_z", "Z")                                                                   \
  X(PadStart, Digital, "pad_start", "Start")                                                       \
  X(PadL, Digital, "pad_l", "L (click)")                                                           \
  X(PadR, Digital, "pad_r", "R (click)")                                                           \
  X(PadUp, Digital, "dpad_up", "D-pad up")                                                         \
  X(PadDown, Digital, "dpad_down", "D-pad down")                                                   \
  X(PadLeft, Digital, "dpad_left", "D-pad left")                                                   \
  X(PadRight, Digital, "dpad_right", "D-pad right")                                                \
  X(MainUp, HalfAxis, "stick_up", "Control stick up")                                              \
  X(MainDown, HalfAxis, "stick_down", "Control stick down")                                        \
  X(MainLeft, HalfAxis, "stick_left", "Control stick left")                                        \
  X(MainRight, HalfAxis, "stick_right", "Control stick right")                                     \
  X(CUp, HalfAxis, "cstick_up", "C-stick up")                                                      \
  X(CDown, HalfAxis, "cstick_down", "C-stick down")                                                \
  X(CLeft, HalfAxis, "cstick_left", "C-stick left")                                                \
  X(CRight, HalfAxis, "cstick_right", "C-stick right")                                             \
  X(LAnalog, HalfAxis, "l_analog", "L (analog)")                                                   \
  X(RAnalog, HalfAxis, "r_analog", "R (analog)")                                                   \
  X(AimUp, HalfAxis, "aim_up", "Aim up")                                                           \
  X(AimDown, HalfAxis, "aim_down", "Aim down")                                                     \
  X(AimLeft, HalfAxis, "aim_left", "Aim left")                                                     \
  X(AimRight, HalfAxis, "aim_right", "Aim right")                                                  \
  X(LookX, Delta, "look_x", "Mouse look X")                                                        \
  X(LookY, Delta, "look_y", "Mouse look Y")                                                        \
  X(BeamPower, Digital, "beam_power", "Power Beam")                                                \
  X(BeamWave, Digital, "beam_wave", "Light Beam")                                                  \
  X(BeamIce, Digital, "beam_ice", "Dark Beam")                                                     \
  X(BeamPlasma, Digital, "beam_plasma", "Annihilator Beam")                                        \
  X(VisorCombat, Digital, "visor_combat", "Combat Visor")                                          \
  X(VisorScan, Digital, "visor_scan", "Scan Visor")                                                \
  X(VisorThermal, Digital, "visor_thermal", "Dark Visor")                                          \
  X(VisorXray, Digital, "visor_xray", "Echo Visor")                                                \
  X(BeamNext, Digital, "beam_next", "Next beam")                                                   \
  X(BeamPrev, Digital, "beam_prev", "Previous beam")                                               \
  X(VisorNext, Digital, "visor_next", "Next visor")                                                \
  X(VisorPrev, Digital, "visor_prev", "Previous visor")                                            \
  X(SpringBall, Digital, "spring_ball", "Spring Ball")                                             \
  X(Layer1, Digital, "layer_1", "Layer 1 (hold)")                                                  \
  X(Layer2, Digital, "layer_2", "Layer 2 (hold)")                                                  \
  X(Layer3, Digital, "layer_3", "Layer 3 (hold)")                                                  \
  X(Layer4, Digital, "layer_4", "Layer 4 (hold)")                                                  \
  X(PortMenu, Digital, "port_menu", "Port menu")                                              \
  X(SaveState, Digital, "save_state", "Save state")                                                \
  X(LoadState, Digital, "load_state", "Load state")                                                \
  X(Screenshot, Digital, "screenshot", "Screenshot")                                               \
  X(ToggleOriginal, Digital, "toggle_original", "Original experience")                             \
  X(PhotoMode, Digital, "photo_mode", "Photo mode")                                                \
  X(GyroAim, Digital, "gyro_aim", "Gyro aim")                                                      \
  X(ResetAim, Digital, "reset_aim", "Reset aim")                                                   \
  X(MenuA, Digital, "menu_a", "Menu A")                                                            \
  X(MenuB, Digital, "menu_b", "Menu B")                                                            \
  X(MenuX, Digital, "menu_x", "Menu X")                                                            \
  X(MenuY, Digital, "menu_y", "Menu Y")                                                            \
  X(MenuL, Digital, "menu_l", "Menu L (click)")                                                    \
  X(MenuR, Digital, "menu_r", "Menu R (click)")                                                    \
  X(MenuLAnalog, HalfAxis, "menu_l_analog", "Menu L (analog)")                                     \
  X(MenuRAnalog, HalfAxis, "menu_r_analog", "Menu R (analog)")                                     \
  X(MenuUp, Digital, "menu_up", "Menu up")                                                         \
  X(MenuDown, Digital, "menu_down", "Menu down")                                                   \
  X(MenuLeft, Digital, "menu_left", "Menu left")                                                   \
  X(MenuRight, Digital, "menu_right", "Menu right")                                                \
  X(BallBomb, Digital, "ball_bomb", "Bomb")                                                        \
  X(BallPowerBomb, Digital, "ball_power_bomb", "Power Bomb")                                       \
  X(BallBoost, Digital, "ball_boost", "Boost Ball")                                                \
  X(BallSpider, Digital, "ball_spider", "Spider Ball")                                             \
  X(BallUnmorph, Digital, "ball_unmorph", "Unmorph")                                                 \
  X(BeamMenu, Digital, "beam_menu", "Beam menu (hold)")                                            \
  X(VisorMenu, Digital, "visor_menu", "Visor menu (hold)")                                        \
  X(ToggleTexturePack, Digital, "toggle_texture_pack", "Texture pack") /* appended: IDs persist */ \
  X(NextStateSlot, Digital, "next_state_slot", "Next save state slot")                            \
  X(PrevStateSlot, Digital, "prev_state_slot", "Previous save state slot")

enum class Action : uint16_t {
#define PORT_INPUT_ACTION_ENUM(id, kind, key, label) id,
  PORT_INPUT_ACTIONS(PORT_INPUT_ACTION_ENUM)
#undef PORT_INPUT_ACTION_ENUM
      Count
};
constexpr int kActionCount = int(Action::Count);

// Custom actions are Action values kCustomBase + id (id >= 1), defined by the
// player in controls.toml ([[custom_action]]); they are always Digital. The
// value range works everywhere an Action does: bindings, unbind, the overlay
// rule and the touch buttons.
constexpr uint16_t kCustomBase = 0x1000;
constexpr int kMaxCustomId = 0xffff - kCustomBase;

inline bool IsCustom(Action a) { return uint16_t(a) > kCustomBase; }
inline Action CustomAction(uint16_t id) { return Action(uint16_t(kCustomBase + id)); }
inline uint16_t CustomId(Action a) { return IsCustom(a) ? uint16_t(uint16_t(a) - kCustomBase) : 0; }

struct ActionInfo {
  ActionKind kind;
  std::string_view key;   // controls.toml name
  std::string_view label; // UI name
};
// A custom action's Info is the generic Digital one; Info(a).key is "custom".
const ActionInfo& Info(Action a);
Action ActionFromKey(std::string_view key); // Action::None if unknown
// The controls.toml name: Info(a).key, or "custom:<id>" for a custom action.
std::string ActionKey(Action a);

enum class Trigger : uint8_t {
  Press,     // while held (analog values pass through)
  Tap,       // one poll on a release shorter than tapMs
  Hold,      // while held, after holdMs
  DoubleTap, // the second press within doubleMs of the first's release, while held
  Toggle,    // each press flips it
};

constexpr int kMaxChord = 4;

struct Binding {
  Action action = Action::None;
  std::array<Input, kMaxChord> inputs{}; // the last one is an ordered chord's trigger
  uint8_t count = 0;
  bool anyOrder = false;
  Trigger trigger = Trigger::Press;
  uint16_t tapMs = 200;
  uint16_t holdMs = 300;
  uint16_t doubleMs = 250;
  uint16_t turboHz = 0; // 0 = off; else on/off this many times a second while active
  float scale = 1.f;
  bool invert = false;
  uint16_t contexts = kCtxAll;
  // Context bits that turn it off, even with a held layer matching `contexts`.
  // Set only by the passes that build the polled profile (never saved), so taking
  // a binding out of one context leaves its rank alone.
  uint16_t blocked = 0;

  bool operator==(const Binding&) const = default;
  bool IsChord() const { return count > 1; }
  bool Uses(const Input& in) const;
};

struct Profile {
  std::vector<Binding> bindings;
  uint16_t chordWindowMs = 40;

  bool operator==(const Profile&) const = default;
};

struct Output {
  std::array<float, kActionCount> value{};
  std::bitset<kActionCount> held, pressed, released;
  // Custom actions, by custom id (CustomId), sized to the profile's ids: a read
  // past the end is false. A caller that wants every custom press walks
  // customPressed, or uses ForEachCustomPressed.
  std::vector<uint8_t> customHeld, customPressed, customReleased;

  static bool CustomBit(const std::vector<uint8_t>& bits, uint16_t id) {
    return id < bits.size() && bits[id] != 0;
  }
  float Value(Action a) const {
    return IsCustom(a) ? (CustomBit(customHeld, CustomId(a)) ? 1.f : 0.f) : value[size_t(a)];
  }
  bool Held(Action a) const {
    return IsCustom(a) ? CustomBit(customHeld, CustomId(a)) : held[size_t(a)];
  }
  bool Pressed(Action a) const {
    return IsCustom(a) ? CustomBit(customPressed, CustomId(a)) : pressed[size_t(a)];
  }
  bool Released(Action a) const {
    return IsCustom(a) ? CustomBit(customReleased, CustomId(a)) : released[size_t(a)];
  }
  // f(custom action) for each custom action pressed this poll.
  template <typename F> void ForEachCustomPressed(F f) const {
    for (size_t id = 0; id < customPressed.size(); ++id) {
      if (customPressed[id] != 0) {
        f(CustomAction(uint16_t(id)));
      }
    }
  }
  // Positive minus negative half, -1..1.
  float Axis(Action neg, Action pos) const;
};

// Per-poll state: chord timers, consumed inputs, triggers, toggles, turbo phase.
class Runtime {
public:
  void SetProfile(const Profile* profile);
  // Drops every latch and timer; inputs held now are consumed until released.
  void Reset();
  // One poll. contexts: the active context bits (layers are added from the
  // profile's Layer actions). nowMs: a monotonic clock. quiet: context bits whose
  // switching on or off leaves held inputs alone instead of consuming them.
  const Output& Poll(const RawState& raw, uint16_t contexts, double nowMs, uint16_t quiet = 0);
  const Output& Last() const { return mOut; }
  // The context bits the last poll used (with layers).
  uint16_t ActiveContexts() const { return mContexts; }

private:
  struct InputState {
    Input input;
    bool down = false;
    bool edge = false; // went down this poll
    double pressedAt = 0;
    int consumedBy = -1; // binding index, kConsumedAll, or -1
    int8_t claimRank = -1; // the highest-ranked binding using it this poll
  };
  struct BindingState {
    int inputs[kMaxChord] = {};
    int8_t rank = 0;
    bool active = false;  // the expression has fired and is held
    bool pending = false; // waiting the chord window
    double pendingSince = 0;
    bool cancelled = false; // a chord took this press: no tap, no toggle
    // Trigger state.
    bool exprWas = false;
    double exprSince = 0;
    double lastRelease = -1e9;
    double lastPressDur = 1e9;
    bool doubleActive = false;
    bool toggled = false;
    bool toggledThisPress = false;
    bool outWas = false;
    double outSince = 0;
  };
  static constexpr int kConsumedAll = -2;

  void Rebuild();
  bool InContext(const Binding& b, uint16_t contexts) const {
    return (b.contexts & contexts) != 0 && (b.blocked & contexts) == 0;
  }
  static bool Passthrough(const Binding& b);
  bool MembersFree(int bi) const;
  void Consume(int bi);
  void Claim(int bi);
  bool Deferrable(int bi, uint16_t contexts) const;
  bool Evaluate(int bi, double nowMs, uint16_t contexts);
  float RunTrigger(int bi, bool expr, const RawState& raw, double nowMs);
  void ConsumeChangedOnContextSwitch(uint16_t before, uint16_t after);

  const Profile* mProfile = nullptr;
  std::vector<InputState> mInputs;
  std::vector<BindingState> mBindings;
  std::vector<std::vector<int>> mSupersets; // per binding: chords it could grow into
  std::vector<int> mPasses[2];              // layer bindings, then the rest
  Output mOut;
  uint16_t mContexts = 0;
  bool mHavePolled = false;
  bool mResetPending = false;
};

} // namespace PortInput
