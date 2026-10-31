// The game loop's per-iteration Aurora poll. It lives here rather than in
// src/MetroidPrime/main.cpp because aurora/event.h includes SDL3, which only the
// full build (mp_port_audio) has on its include path.

#include "port_mobile.h"
#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>

#include "input/port_input_devices.h"
#include "port_input_remap_ui.h"
#include "port_debug.h"
#include "port_harness.h"
#include "port_input.h"
#include "port_input_pad.h"
#include "port_mouse.h"
#include "port_input_gate.h"
#include "port_overlay.h"
#include "port_photo.h"
#include "port_pointer.h"
#include "port_prompts.h"
#include "port_memory_log.h"
#include "port_savestate.h"
#include "port_textures.h"
#include "port_touch.h"
#include "port_touch_activity.h"
#include "port_warp.h"
#include "port_watchdog.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Full stick deflections: S* is the main stick, C* the C-stick.
struct StickName {
  const char* name;
  s8 stickX, stickY, substickX, substickY;
};

constexpr StickName kSticks[] = {
    {"SUP", 0, 127, 0, 0},   {"SDOWN", 0, -127, 0, 0}, {"SLEFT", -127, 0, 0, 0},
    {"SRIGHT", 127, 0, 0, 0}, {"CUP", 0, 0, 0, 127},   {"CDOWN", 0, 0, 0, -127},
    {"CLEFT", 0, 0, -127, 0}, {"CRIGHT", 0, 0, 127, 0},
};

// Seconds since the first input-script poll, so screenshots can be matched to inputs.
double Seconds() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration< double >(std::chrono::steady_clock::now() - start).count();
}

bool NameIs(const char* name, const char* p, const char* at) {
  return std::strlen(name) == static_cast< size_t >(at - p) && std::strncmp(name, p, at - p) == 0;
}

// The button names are shared with the console's `press` (port_input.h).
using PortInput::kButtonNames;

// One token of the MP2_INPUT script, "NAME@frame[+frames]". Parsed once, because a token fires once:
// `fired` is what makes a token whose frame has already gone by fire at the first frame the script
// sees instead of being lost, and what keeps a frame the counter skips from dropping a token.
struct InputToken {
  int pad;
  const char* name; // the token's text, e.g. "NEXTAREA"
  const char* at;   // the '@' that ends it
  unsigned start;   // the game update it fires on
  unsigned length;  // how many game updates a button or stick stays down
  bool fired;
};

// Far more than a sweep's own script (63 areas x 5 tokens); the rest of a longer one is ignored
// loudly rather than silently.
constexpr size_t kMaxTokens = 512;

InputToken* InputScript(size_t& count) {
  static InputToken tokens[kMaxTokens];
  static size_t sCount = 0;
  static bool sParsed = false;
  if (!sParsed) {
    sParsed = true;
    for (const char* p = std::getenv("MP2_INPUT"); p != nullptr && *p != '\0';) {
      const char* at = std::strchr(p, '@');
      if (at == nullptr) {
        break;
      }
      char* end = nullptr;
      const unsigned start = static_cast< unsigned >(std::strtoul(at + 1, &end, 10));
      unsigned length = 8;
      if (*end == '+') {
        length = static_cast< unsigned >(std::strtoul(end + 1, &end, 10));
      }
      if (sCount == kMaxTokens) {
        std::fprintf(stderr, "[input] MP2_INPUT holds more than %zu tokens; the rest are ignored\n",
                     kMaxTokens);
        break;
      }
      InputToken& token = tokens[sCount++];
      token.name = p;
      token.pad = PortInputPad::SplitPad(token.name, at);
      token.at = at;
      token.start = start;
      token.length = length;
      token.fired = false;
      p = *end == ',' ? end + 1 : end;
    }
  }
  count = sCount;
  return tokens;
}

// The frame the script is on, 1-based and counted in *game updates* (CStateManager::Update, the
// counter MP2_FRAMES uses): frame 1 is the game's first update, so a token at frame 0 or 1 is the
// pad state the game starts with - the state a token sets before the game has updated at all is the
// state its first update reads. Counting loop iterations instead put every early token in the boot,
// which is thousands of iterations long before the game's first update (~34 s): a token framed
// against the game's updates landed in the wrong place and one aimed at the start was lost
// (docs/STATUS.md wall 3, `NEXTAREA@1400` never hopping where `NEXTAREA@3500` did).
//
// The counter also never stands still once the game has started: an iteration the game spends *not*
// updating gains one frame of its own, because a screen that pauses the game performs no game update
// at all and a script that could not advance through that pause could never press the button that
// dismisses it (tools/sweep_areas.sh's periodic `B` depends on it). Before the first update the
// frame stays at 1 - the boot consumes none of the script's timeline, which is also why a screen
// that precedes every game update (the front end) cannot be driven by frame: MP2_SKIP_FRONTEND is
// what gets a run past it.
unsigned ScriptFrame() {
  static unsigned sFrame = 1;
  static unsigned sSeen = 0;
  const unsigned updates = PortHarness::GameUpdates();
  if (updates == 0) {
    return sFrame;
  }
  if (updates != sSeen) {
    sSeen = updates;
    // Back in step with the game: the pad state this call sets is what update `updates + 1` runs on.
    sFrame = updates + 1;
  } else {
    ++sFrame;
  }
  return sFrame;
}

// Queues the PortWarp request a warp token names, and logs it where it fired: a warp token shows up
// in the log as the game serves it (`[warp] moved to area 3`), so without this line a log cannot tell
// a token the game ignored from one that never fired - which is the difference wall 3 turned on.
// Text that names no warp token (a button, a stick, a typo) falls through the chain and is ignored.
void FireWarpToken(const InputToken& token, unsigned frame) {
  if (NameIs("NEXTAREA", token.name, token.at)) {
    PortWarp::CycleArea(1);
  } else if (NameIs("PREVAREA", token.name, token.at)) {
    PortWarp::CycleArea(-1);
  } else if (NameIs("ATDOOR", token.name, token.at)) {
    PortWarp::Dock();
  } else if (NameIs("DOOR", token.name, token.at)) {
    PortWarp::Door();
  } else if (NameIs("STATION", token.name, token.at)) {
    PortWarp::Station();
  } else if (NameIs("WORLDPORTAL", token.name, token.at)) {
    PortWarp::WorldPortal();
  } else if (NameIs("KILL", token.name, token.at)) {
    PortWarp::Kill();
  } else if (NameIs("HEAL", token.name, token.at)) {
    PortWarp::Heal();
  } else if (token.at - token.name > 5 && std::strncmp(token.name, "LAYER", 5) == 0) {
    // LAYER<layer>: the number is part of the token, so a sweep can ask for a different layer in
    // each area it visits.
    PortWarp::Layer(std::atoi(token.name + 5));
  } else if (NameIs("NEXTWORLD", token.name, token.at)) {
    PortWarp::CycleWorld(1);
  } else if (NameIs("PREVWORLD", token.name, token.at)) {
    PortWarp::CycleWorld(-1);
  } else {
    return;
  }
  std::fprintf(stderr, "[input] %.*s at frame %u (%.1f s)\n",
               static_cast< int >(token.at - token.name), token.name, frame, Seconds());
}

// Pad 0 as the debug console drives it (`press`/`stick`/`cstick`, see port_input.h): a hold that
// merges into the same virtual pad the MP2_INPUT script writes. The game thread is the only writer -
// console commands run inside `CStateManager::PortWarpTick` - so no locking is needed.
struct SHoldPad {
  u16 buttons = 0;
  s8 stickX = 0;
  s8 stickY = 0;
  s8 substickX = 0;
  s8 substickY = 0;
  unsigned startUpdate = 0; // PortHarness::GameUpdates() at which the hold starts
  unsigned endUpdate = 0; // PortHarness::GameUpdates() at which the hold ends; ignored when forever
  bool forever = false;
  bool used = false;      // a hold was set at some point, so pad 0 must stay connected afterwards
};

SHoldPad sHold;

bool HoldActive() {
  return sHold.used && PortHarness::GameUpdates() >= sHold.startUpdate &&
         (sHold.forever || PortHarness::GameUpdates() < sHold.endUpdate);
}

// MP2_INPUT="BUTTON@frame[+frames],...": hold BUTTON on pad 0 from that game update for the given
// number of game updates (default 8), so runs can be driven past menus without a player. See
// ScriptFrame for what a frame is, and platform/README.md's "Input" for the table. Stick names
// (kSticks) deflect fully while held; overlapping entries add up. Warp tokens (NEXTAREA, PREVAREA,
// NEXTWORLD, PREVWORLD, KILL, HEAL) queue a PortWarp request at their frame instead, which is how a
// run sweeps every area on its own.
void RunInputScript(unsigned frame) {
  size_t count = 0;
  InputToken* tokens = InputScript(count);
  // No script and no console hold: pad 0 is left to the real controller, as it always was.
  if (count == 0 && !sHold.used) {
    return;
  }
  Seconds();
  // Pad 0 also takes the console hold; pads 1-3 are only written when an entry names them.
  PADStatus pads[PortInputPad::kPadCount]{};
  u16 heldPads[PortInputPad::kPadCount]{};
  bool used[PortInputPad::kPadCount]{};
  used[0] = true;
  heldPads[0] = HoldActive() ? sHold.buttons : 0;
  if (HoldActive()) {
    pads[0].stickX += sHold.stickX;
    pads[0].stickY += sHold.stickY;
    pads[0].substickX += sHold.substickX;
    pads[0].substickY += sHold.substickY;
  }

  for (size_t i = 0; i < count; ++i) {
    InputToken& token = tokens[i];
    PADStatus& status = pads[token.pad];
    u16& held = heldPads[token.pad];
    used[token.pad] = true;
    // Frame 1 is the first game update and 0 means "the state the game starts with", so a token at
    // 0 is served with the first update too rather than never being reached.
    const unsigned start = token.start == 0 ? 1 : token.start;
    const bool down = frame >= start && frame < start + token.length;
    const bool fire = !token.fired && frame >= start;
    token.fired = token.fired || fire;
    if (fire && token.pad == 0) {
      FireWarpToken(token, frame);
    }
    for (const PortInput::SButtonName& button : kButtonNames) {
      if (NameIs(button.name, token.name, token.at)) {
        if (down) {
          held |= button.mask;
        }
        if (fire) {
          std::fprintf(stderr, "[input] %s at frame %u (%.1f s)\n", button.name, frame, Seconds());
        }
      }
    }
    for (const StickName& stick : kSticks) {
      if (NameIs(stick.name, token.name, token.at)) {
        if (down) {
          status.stickX += stick.stickX;
          status.stickY += stick.stickY;
          status.substickX += stick.substickX;
          status.substickY += stick.substickY;
        }
        if (fire) {
          std::fprintf(stderr, "[input] %s at frame %u (%.1f s)\n", stick.name, frame, Seconds());
        }
      }
    }
  }
  // Once the script ends, a pad stays connected and idle: clearing it would leave no controller,
  // and the game then gets no input messages at all (player dt stays 0, the gun never draws).
  for (int pad = 0; pad < PortInputPad::kPadCount; ++pad) {
    if (!used[pad]) {
      continue;
    }
    PADStatus& status = pads[pad];
    const u16 held = heldPads[pad];
    status.err = PAD_ERR_NONE;
    status.button = held;
    // A real pad's L/R click sits at the end of the analog travel, and the keyboard binding drives
    // both too (the input engine's keyboard profile). Commands bound to the analog trigger - the scan visor's Scan Item,
    // for one - read only the analog half, so a held L that left it at 0 locked on but never scanned.
    status.triggerLeft = (held & PAD_TRIGGER_L) != 0 ? 255 : 0;
    status.triggerRight = (held & PAD_TRIGGER_R) != 0 ? 255 : 0;
    PADSetVirtualStatus(pad, &status);
  }
}

} // namespace

namespace PortInput {

void Hold(u16 buttons, s8 stickX, s8 stickY, s8 substickX, s8 substickY, unsigned updates,
          unsigned delay) {
  sHold.buttons = buttons;
  sHold.stickX = stickX;
  sHold.stickY = stickY;
  sHold.substickX = substickX;
  sHold.substickY = substickY;
  sHold.forever = updates == 0;
  // The next update reads the pad state this iteration sets, so `updates` counts from the update
  // after this one.
  sHold.startUpdate = PortHarness::GameUpdates() + delay;
  sHold.endUpdate = sHold.startUpdate + updates + 1;
  sHold.used = true;
  std::fprintf(stderr, "[input] console holds 0x%x stick %d,%d c-stick %d,%d for %u updates (in %u)\n",
               buttons, stickX, stickY, substickX, substickY, updates, delay);
}

void Clear() {
  sHold.buttons = 0;
  sHold.stickX = 0;
  sHold.stickY = 0;
  sHold.substickX = 0;
  sHold.substickY = 0;
  sHold.forever = false;
  sHold.startUpdate = 0;
  sHold.endUpdate = 0;
}

} // namespace PortInput

namespace {
// The 60 FPS cap's next frame deadline; 0 while the cap is off, so turning it on starts afresh.
uint64_t sNextFrameDeadline = 0;
// Pad 0 as PortPumpEvents last read it. Aurora's override is one-shot (consumed by the next
// PADRead), but a loop iteration can run several game ticks, each with its own PADRead.
PADStatus sPad0{};
bool sPad0Valid = false;
} // namespace

// Called before every game tick's PADRead: re-arms pad 0 with this iteration's state. Without it
// the second tick of an iteration read pad 0 as PAD_ERR_NO_CONTROLLER (no physical pad), so
// CDolphinController cleared every button for that tick: a held fire button read as a release
// (the charge fired early) and a new press the next iteration (a stream of shots), and a held
// lock-on dropped and re-locked. Only frames below 60 fps run more than one tick.
void PortRearmPad0() {
  if (sPad0Valid) {
    PADSetPortOverride(0, &sPad0);
  }
}

// MP1's frame pacing (src/MetroidPrime/main.cpp there), called once per main-loop iteration: under
// the 60 FPS cap, sleeps to the next 60 Hz deadline. A frame that overran by more than a period
// resets the deadline instead of racing to catch up.
void PortPaceFrame() {
  // The iteration's work ends here, before any sleep; its start is where the last call returned.
  static uint64_t sWorkStartNs = 0;
  const uint64_t workEndNs = SDL_GetTicksNS();
  if (sWorkStartNs != 0 && workEndNs - sWorkStartNs < 1000000000ull) { // not across a max-speed run
    PortOverlay::NoteFrameWork(workEndNs - sWorkStartNs);
  }
  struct Restart {
    uint64_t& start;
    ~Restart() { start = SDL_GetTicksNS(); }
  } restart{sWorkStartNs};
  if (!PortOverlay::FrameLimit() || PortHarness::Unpaced()) {
    sNextFrameDeadline = 0;
    return;
  }
  constexpr uint64_t kFramePeriodNs = 1000000000ull / 60;
  if (sNextFrameDeadline == 0) {
    sNextFrameDeadline = SDL_GetTicksNS();
    return;
  }
  sNextFrameDeadline += kFramePeriodNs;
  const uint64_t now = SDL_GetTicksNS();
  if (sNextFrameDeadline > now) {
    // SDL_DelayPrecise sleeps in 1 ms slices, about sixteen wake-ups a frame at 60 Hz. One plain
    // sleep up to 2 ms short of the deadline first leaves it only the last stretch, which saves the
    // wake-ups (and battery on phones).
    constexpr uint64_t kCoarseMarginNs = 2000000;
    if (sNextFrameDeadline - now > kCoarseMarginNs) {
      SDL_DelayNS(sNextFrameDeadline - now - kCoarseMarginNs);
    }
    const uint64_t afterCoarse = SDL_GetTicksNS();
    if (sNextFrameDeadline > afterCoarse) {
      SDL_DelayPrecise(sNextFrameDeadline - afterCoarse);
    }
  } else if (now - sNextFrameDeadline > kFramePeriodNs) {
    sNextFrameDeadline = now;
  }
}

// Runs aurora_update (window, input and GX housekeeping); true once the window asks to quit.
// Quick resume on the platforms that background or kill an app: the held snapshot is written when
// the app goes to the background (it may be killed there), and the marker goes at termination.
// May run on another thread, so it only flushes what the game thread already took.
bool ResumeEventWatch(void*, SDL_Event* event) {
  switch (event->type) {
  case SDL_EVENT_WILL_ENTER_BACKGROUND:
  case SDL_EVENT_DID_ENTER_BACKGROUND:
    PortSaveState::ResumeFlush();
    break;
  case SDL_EVENT_TERMINATING:
    PortSaveState::ResumeShutdown();
    break;
  default:
    break;
  }
  return true;
}

bool PortPumpEvents() {
  // The hang watchdog's heartbeat: the game loop asks this once per iteration, so a frame that
  // never returns here is the one the watchdog reports (phase and stack).
  static unsigned sFrame = 0;
  PortWatchdog::Heartbeat(++sFrame);
  PortMemoryLog::Tick(SDL_GetTicksNS());
  static const bool sWatching = SDL_AddEventWatch(ResumeEventWatch, nullptr);
  (void)sWatching;
  PortWarp::ApplyEnvOnce();
  // The aspect setting: widens the render mode before anything this iteration draws.
  PortOverlay::ApplyAspect();
  // A run that has reached its MP2_FRAMES limit leaves through the same door the window's close
  // button opens: the game loop ends, Aurora shuts down and the process exits 0. The check is here
  // rather than in the game because the loop asks this every iteration, whatever the game is doing.
  if (PortHarness::ExitRequested()) {
    std::fprintf(stderr, "[harness] the run is over (MP2_FRAMES); shutting down\n");
    PortSaveState::ResumeSave(true);
    return true;
  }
  // A run that has stopped updating is reported here, every iteration, because this poll is the one
  // thing a paused or wedged game still runs.
  PortHarness::IdleCheck();
  PortTouch::BeginFrame();
  // PortMouse's travel is latched per tick (PortMouse::BeginTick, from CMain), not here.
  RunInputScript(ScriptFrame());
  PortPointer::BeginFrame();
  bool mouseInput = false;
  for (const AuroraEvent* event = aurora_update(); event != nullptr && event->type != AURORA_NONE;
       ++event) {
    if (event->type == AURORA_SDL_EVENT) {
      const SDL_Event& e = event->sdl;
      const SDL_MouseID which = e.type == SDL_EVENT_MOUSE_MOTION        ? e.motion.which
                                : e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? e.button.which
                                : e.type == SDL_EVENT_MOUSE_WHEEL       ? e.wheel.which
                                                                        : SDL_TOUCH_MOUSEID;
      if (which != SDL_TOUCH_MOUSEID && which != SDL_PEN_MOUSEID) {
        mouseInput = true;
      }
    }
    if (event->type == AURORA_EXIT) {
      // A clean quit: between updates, with the game still whole, so the quick-resume state is
      // written here (MP1 does it at the same point of its loop).
      PortSaveState::ResumeSave();
      return true;
    }
    // Menu pointer gestures. A press or wheel over an ImGui window (F1, the resume prompt) is
    // ImGui's alone; releases and motion still pass, so a gesture begun in the game ends cleanly
    // and a release whose press was ImGui's is ignored by the recognizer.
    if (event->type == AURORA_SDL_EVENT) {
      const uint32_t type = event->sdl.type;
      const bool starts = type == SDL_EVENT_MOUSE_BUTTON_DOWN || type == SDL_EVENT_FINGER_DOWN ||
                          type == SDL_EVENT_MOUSE_WHEEL;
      if (!starts || !PortOverlay::PointerBlocked()) {
        PortPointer::HandleEvent(event->sdl);
      }
    }
    if (event->type == AURORA_SDL_EVENT) {
      PortDebug::PhotoTouchEvent(event->sdl);
    }
    if (event->type == AURORA_SDL_EVENT && event->sdl.type == SDL_EVENT_MOUSE_MOTION &&
        event->sdl.motion.which != SDL_TOUCH_MOUSEID && event->sdl.motion.which != SDL_PEN_MOUSEID) {
      // Relative motion only, which is what SDL reports while the port holds capture; the
      // game consumes the resulting aim in CPlayer/CFirstPersonCamera.
      PortMouse::AddMotion(event->sdl.motion.xrel, event->sdl.motion.yrel);
    }
    // Match MP1's last-used-input policy, excluding virtual pads and drift.
    if (event->type == AURORA_SDL_EVENT) {
      const SDL_Event& input = event->sdl;
      const bool virtualPad = input.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN
                                  ? SDL_IsJoystickVirtual(input.gbutton.which)
                                  : input.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
                                        SDL_IsJoystickVirtual(input.gaxis.which);
      if (PortTouch::PhysicalEvent(input, PortOverlay::Visible(), virtualPad)) {
        PortTouch::NotePhysicalInput();
      }
    }
    // MP1's F10: the 60 FPS cap on and off.
    if (event->type == AURORA_SDL_EVENT && event->sdl.type == SDL_EVENT_KEY_DOWN &&
        !event->sdl.key.repeat && event->sdl.key.scancode == SDL_SCANCODE_F10) {
      PortOverlay::SetFrameLimit(!PortOverlay::FrameLimit());
      sNextFrameDeadline = 0;
      std::fprintf(stderr, "Frame limit: %s\n", PortOverlay::FrameLimit() ? "60 FPS" : "unlimited");
    }
    // F11 toggles the Fullscreen setting on desktop; Android has the Video > Display checkbox.
#if !defined(PORT_MOBILE)
    if (event->type == AURORA_SDL_EVENT && event->sdl.type == SDL_EVENT_KEY_DOWN &&
        !event->sdl.key.repeat && event->sdl.key.scancode == SDL_SCANCODE_F11) {
      SDL_Window* window = SDL_GetWindowFromID(event->sdl.key.windowID);
      if (window != nullptr) {
        const bool full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
        PortOverlay::SetFullscreen(!full);
      }
    }
#endif
    // No warp hotkeys: warping is driven from the F1 menu's Game page (as in the Prime 1 port),
    // so the function keys stay free for the input engine's bindings (F8 is photo mode's).
  }
  PortMouse::UpdateIdleCursor(mouseInput, PortPhoto::PanelWantsCursor());
  // Pad 0 is the input engine's (keyboard, mouse buttons and the controller through controls.toml),
  // read once per iteration, before the game's PADRead; Aurora still merges the console's and
  // MP2_INPUT's virtual pad on top of it.
  PortInputDevices::PollPortActions();
  PortInputDevices::PollGyro();
  sPad0 = PortInputDevices::Poll();
  static PortInputGate::Gate<PADStatus> sGate;
  sGate.Apply(PortOverlay::Visible(), sPad0);
  sPad0Valid = true;
  // While the F1 overlay is open the game reads a neutral pad (keyboard, mouse buttons, pad and
  // the console's virtual pad alike; the overlay navigates with the keyboard and the pad). Held
  // input reads as released; on closing, the gate (buttons, sticks, triggers) and Aurora's own
  // unblock suppression keep whatever is still held masked until it is released, so the key used
  // in the menu (Enter, Space, arrows) or a held walk key does not fire in the game.
  // PollPortActions above is not gated: the F1 toggle, screenshot and the remap capture still work.
  PADBlockInput(PortOverlay::Visible());
  PADSetPortOverride(0, &sPad0);
  // A device or pack change requested since the last iteration swaps the replacement set here,
  // and the binding icons follow the input used last (both throttle themselves).
  PortTextures::Poll();
  PortPrompts::Poll();
  return false;
}
