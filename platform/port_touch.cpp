// The Android on-screen controller's native side. See platform/include/port_touch.h for what
// this is; the JNI entry points the Java view calls are platform/port_touch_jni.cpp.
//
// Two threads meet here and that is the whole design problem. The Java view runs on the
// Android UI thread and calls in whenever a finger moves; the game runs on Aurora's thread and
// polls the same state once per iteration through `BeginFrame`. So: the pad is SDL's virtual
// joystick, which is mutexed inside SDL and is the one thing both threads may touch directly;
// the aim goes straight into `PortMouse`, which is safe for it; the settings are under one
// mutex; and everything that reaches the game (wheel picks, beam shift, Turbo) is an atomic the
// UI thread raises and `BeginFrame` applies, so the player state is only ever touched on the
// game thread.

#include "port_utf8_path.h"
#include "port_mobile.h"
#include "port_touch.h"
#include "port_photo.h"

#include "port_harness.h"
#include "port_mouse.h"
#include "port_radial.h"
#include "port_overlay.h"
#include "port_prompts.h"
#include "touch_pad.h"

#include "GuiSys/CGuiModel.hpp"
#include "Kyoto/Graphics/CTexture.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"

#include <SDL3/SDL.h>
#include "port_touch_activity.h"
#include <aurora/gfx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

namespace {

// MP1's defaults (platform/include/port_input_settings.h there). The default layout is MP1's
// Default one - the left stick, the face buttons, drag-to-aim and the visor/beam wheels - which
// MP2 now has the wheels for; Classic and Twin stick are one Layout tap away in the editor.
// An older touch.cfg (written when MP2 defaulted to Classic) keeps its `classic=1`.
constexpr float kAimSpeed = 2.25f;
constexpr uint64_t kTurboHalfNs = 2 * 16666667ull; // Turbo half-cycle: two 60 Hz ticks down, two up

struct Settings {
  bool classic = false;
  bool twin = false;
  // On only where the overlay exists: an enabled touch source makes PortMouse track the camera
  // every tick, which a desktop run (no overlay, no fingers) has no use for. MP2_TOUCH_AIM=1
  // still turns it on there.
#if defined(PORT_MOBILE)
  bool aim = true;
#else
  bool aim = false;
#endif
  bool wheels = false; // retired, until an overlay migrates it (port_touch.h kTouchWheelActions)
  bool turbo = false;
  bool colors = false;
  bool labels = true;
  bool floating = true;
  bool visorTapScan = false;
  bool controls = true;
  float sideMargin = 16.f;
  float stickInset = 32.f;
  float buttonInset = 0.f;
  float opacity = 100.f;
  float aimSpeed = kAimSpeed;
  // MP1's "Separate vertical speed##touch": up and down drags use aimSpeedY.
  bool aimSplit = false;
  float aimSpeedY = kAimSpeed;
  bool springSwipe = true;
  std::string layout;
};

std::mutex sMutex;
Settings sSettings;
std::string sConfigPath;
bool sLoaded = false; // guarded by sMutex
bool sDirty = false;
bool sPadAttachFailed = false; // guarded by sPadMutex: the failure is logged once, not per event

std::atomic< bool > sEditRequested{false};
std::atomic< bool > sPhysicalInput{false};
PortTouch::Activity sActivity;
std::atomic< bool > sRealPad{false};
std::atomic< bool > sDeviceXbox{false};

// What the UI thread asks of the game; BeginFrame applies it on the game thread.
std::atomic< int > sWheelState{0};
// Per player: Players 2-4's pads route their beam/visor picks, Spring Ball and Morph Ball rows
// through these too ([0] is Player 1's, the overlay's).
constexpr int kPlayers = 4;
std::atomic< bool > sMorphed[kPlayers]{};
std::atomic< bool > sOrbitLocked[kPlayers]{};
std::atomic< bool > sScanTextUp{false};
std::atomic<bool> sRadialTouchForeground{true};
std::atomic< int > sPendingVisor[kPlayers]{-1, -1, -1, -1};
std::atomic< int > sPendingBeam[kPlayers]{-1, -1, -1, -1};

bool ValidPlayer(int player) { return player >= 0 && player < kPlayers; }
std::atomic< bool > sBeamShift{false};
std::atomic< bool > sTurboHeld{false};
std::atomic< bool > sMenuBackHeld{false};
// A back-arrow press not yet read, so a tap whose down and up fall between two polls still counts.
std::atomic< bool > sMenuBackPressed{false};
// The D-pad buttons the beam shift swallowed on their press, so their release is swallowed too
// (and a press that reached the pad before the shift still releases there). UI thread only.
bool sDpadShifted[4] = {};
// Turbo's own A presses (game thread only).
std::atomic< bool > sTurboDown{false}; // set by BeginFrame, read by the input poll
uint64_t sTurboStartNs = 0; // 0 = not running

std::mutex sPadMutex;
PortTouchPad::Pad sPad;

constexpr int kDpadUp = 11; // SDL_GAMEPAD_BUTTON_DPAD_UP; then down, left, right
constexpr int kInGame = 1;  // CMFGame::kFS_InGame
constexpr int kPaused = 2;  // CMFGame::kFS_Paused

bool EnvFlag(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
}

// `MP2_DEBUG_PAD=1`: one line per button press and per stick move, so a device log says
// whether a touch ever became a pad write. Off by default; it is a line per event.
bool DebugPad() {
  static const bool on = EnvFlag("MP2_DEBUG_PAD");
  return on;
}

// Callers hold sMutex. An `MP2_TOUCH_*` that is set wins over the file: "0" turns a switch off,
// anything else on; the numbers are clamped as MP1's F1 sliders clamp them.
void ApplyEnvironmentLocked() {
  struct Flag {
    const char* name;
    bool Settings::* field;
  };
  static const Flag kFlags[] = {
      {"MP2_TOUCH_CLASSIC", &Settings::classic},
      {"MP2_TOUCH_TWIN", &Settings::twin},
      {"MP2_TOUCH_AIM", &Settings::aim},
      {"MP2_TOUCH_WHEELS", &Settings::wheels},
      {"MP2_TOUCH_TURBO", &Settings::turbo},
      {"MP2_TOUCH_COLORS", &Settings::colors},
      {"MP2_TOUCH_LABELS", &Settings::labels},
      {"MP2_TOUCH_FLOATING", &Settings::floating},
      {"MP2_TOUCH_VISOR_TAP_SCAN", &Settings::visorTapScan},
      {"MP2_TOUCH_CONTROLS", &Settings::controls},
      {"MP2_TOUCH_SPRING_SWIPE", &Settings::springSwipe},
      {"MP2_TOUCH_AIM_SPLIT", &Settings::aimSplit},
  };
  for (const Flag& flag : kFlags) {
    if (const char* value = std::getenv(flag.name)) {
      sSettings.*flag.field = *value != '\0' && std::strcmp(value, "0") != 0;
    }
  }
  if (sSettings.classic && sSettings.twin) {
    sSettings.twin = false;
  }
  struct Number {
    const char* name;
    float Settings::* field;
    float low;
    float high;
  };
  static const Number kNumbers[] = {
      {"MP2_TOUCH_SIDE_MARGIN", &Settings::sideMargin, 0.f, 96.f},
      {"MP2_TOUCH_STICK_INSET", &Settings::stickInset, 0.f, 160.f},
      {"MP2_TOUCH_BUTTON_INSET", &Settings::buttonInset, 0.f, 160.f},
      {"MP2_TOUCH_OPACITY", &Settings::opacity, 10.f, 100.f},
      {"MP2_TOUCH_AIM_SPEED_Y", &Settings::aimSpeedY, 0.25f, 10.f},
  };
  for (const Number& number : kNumbers) {
    if (const char* text = std::getenv(number.name)) {
      const float value = static_cast< float >(std::atof(text));
      if (std::isfinite(value)) {
        sSettings.*number.field =
            value < number.low ? number.low : (value > number.high ? number.high : value);
      }
    }
  }
  if (const char* speed = std::getenv("MP2_TOUCH_AIM_SPEED")) {
    const float value = static_cast< float >(std::atof(speed));
    if (std::isfinite(value) && value > 0.f) {
      sSettings.aimSpeed = value < 0.25f ? 0.25f : (value > 10.f ? 10.f : value);
    }
  }
}

void ReadEnvironment() {
  static std::once_flag once;
  std::call_once(once, [] {
    std::lock_guard< std::mutex > lock(sMutex);
    ApplyEnvironmentLocked();
  });
}

bool ParseBool(const std::string& value) { return value != "0" && value != "false"; }

// A number from touch.cfg, clamped to the range the setters and the environment use. A value
// that is not a finite number leaves `field` as it was (the default).
void ParseClamped(const std::string& value, float& field, float low, float high) {
  const float parsed = static_cast< float >(std::atof(value.c_str()));
  if (std::isfinite(parsed)) {
    field = parsed < low ? low : (parsed > high ? high : parsed);
  }
}

// The settings file lives in the user directory next to the memory card. Lines are
// `key=value`; the layout string is the only value that may contain anything but digits.
//
// `sLoaded` latches only once a real path has been read: the Java view polls from its first
// draw, which can run before `main` calls `Initialize`, and latching then skipped the file.
void LoadConfig() {
  std::lock_guard< std::mutex > lock(sMutex);
  if (sLoaded || sConfigPath.empty()) {
    return;
  }
  sLoaded = true;
  std::ifstream file(PortUtf8::FromUtf8(sConfigPath));
  std::string line;
  while (std::getline(file, line)) {
    const size_t first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || line[first] == '#') {
      continue;
    }
    const size_t last = line.find_last_not_of(" \t\r\n");
    const std::string entry = line.substr(first, last - first + 1);
    const size_t equals = entry.find('=');
    if (equals == std::string::npos) {
      continue;
    }
    const std::string key = entry.substr(0, equals);
    const std::string value = entry.substr(equals + 1);
    Settings& s = sSettings;
    if (key == "layout") {
      s.layout = value;
    } else if (key == "classic") {
      // Exclusive with twin; classic winning is MP1's rule on load.
      s.classic = ParseBool(value);
      if (s.classic) {
        s.twin = false;
      }
    } else if (key == "twin") {
      s.twin = ParseBool(value) && !s.classic;
    } else if (key == "aim") {
      s.aim = ParseBool(value);
    } else if (key == "wheels") {
      s.wheels = ParseBool(value);
    } else if (key == "turbo") {
      s.turbo = ParseBool(value);
    } else if (key == "colors") {
      s.colors = ParseBool(value);
    } else if (key == "labels") {
      s.labels = ParseBool(value);
    } else if (key == "floating") {
      s.floating = ParseBool(value);
    } else if (key == "visor_tap_scan") {
      s.visorTapScan = ParseBool(value);
    } else if (key == "controls") {
      s.controls = ParseBool(value);
    } else if (key == "side_margin") {
      ParseClamped(value, s.sideMargin, 0.f, 96.f);
    } else if (key == "stick_inset") {
      ParseClamped(value, s.stickInset, 0.f, 160.f);
    } else if (key == "button_inset") {
      ParseClamped(value, s.buttonInset, 0.f, 160.f);
    } else if (key == "aim_speed") {
      ParseClamped(value, s.aimSpeed, 0.25f, 10.f);
    } else if (key == "aim_split") {
      s.aimSplit = ParseBool(value);
    } else if (key == "aim_speed_y") {
      ParseClamped(value, s.aimSpeedY, 0.25f, 10.f);
    } else if (key == "spring_swipe") {
      s.springSwipe = ParseBool(value);
    } else if (key == "opacity") {
      ParseClamped(value, s.opacity, 10.f, 100.f);
    }
  }
  ApplyEnvironmentLocked();
  std::fprintf(stderr,
               "[touch] config %s: layout=\"%s\" classic=%d twin=%d aim=%d wheels=%d turbo=%d\n",
               sConfigPath.c_str(), sSettings.layout.c_str(), sSettings.classic ? 1 : 0,
               sSettings.twin ? 1 : 0, sSettings.aim ? 1 : 0, sSettings.wheels ? 1 : 0,
               sSettings.turbo ? 1 : 0);
}

// Callers hold sMutex. Writes `<path>.tmp` and renames it over the real file, so a write that
// is killed part way never leaves a truncated touch.cfg for the next launch.
void SaveConfigLocked() {
  if (sConfigPath.empty()) {
    return;
  }
  // Cleared first so that a failure below (open, write or rename) is logged once and not retried
  // every frame by BeginFrame's flush; the next change marks the settings dirty and tries again.
  sDirty = false;
  const std::string temporary = sConfigPath + ".tmp";
  {
    std::ofstream file(PortUtf8::FromUtf8(temporary), std::ios::trunc);
    if (!file) {
      std::fprintf(stderr, "[touch] cannot write %s\n", temporary.c_str());
      return;
    }
    const Settings& s = sSettings;
    file << "classic=" << (s.classic ? 1 : 0) << '\n';
    file << "twin=" << (s.twin ? 1 : 0) << '\n';
    file << "aim=" << (s.aim ? 1 : 0) << '\n';
    if (s.wheels) {
      file << "wheels=1\n"; // not migrated yet
    }
    file << "turbo=" << (s.turbo ? 1 : 0) << '\n';
    file << "colors=" << (s.colors ? 1 : 0) << '\n';
    file << "labels=" << (s.labels ? 1 : 0) << '\n';
    file << "floating=" << (s.floating ? 1 : 0) << '\n';
    file << "visor_tap_scan=" << (s.visorTapScan ? 1 : 0) << '\n';
    file << "controls=" << (s.controls ? 1 : 0) << '\n';
    file << "side_margin=" << s.sideMargin << '\n';
    file << "stick_inset=" << s.stickInset << '\n';
    file << "button_inset=" << s.buttonInset << '\n';
    file << "opacity=" << s.opacity << '\n';
    file << "aim_speed=" << s.aimSpeed << '\n';
    file << "aim_split=" << (s.aimSplit ? 1 : 0) << '\n';
    file << "aim_speed_y=" << s.aimSpeedY << '\n';
    file << "spring_swipe=" << (s.springSwipe ? 1 : 0) << '\n';
    file << "layout=" << s.layout << '\n';
    file.flush();
    if (!file) {
      std::fprintf(stderr, "[touch] cannot write %s\n", temporary.c_str());
      std::error_code removeEc;
      std::filesystem::remove(PortUtf8::FromUtf8(temporary), removeEc);
      return;
    }
  }
  std::error_code renameEc;
  std::filesystem::rename(PortUtf8::FromUtf8(temporary), PortUtf8::FromUtf8(sConfigPath), renameEc);
  if (renameEc) {
    std::fprintf(stderr, "[touch] cannot replace %s\n", sConfigPath.c_str());
    std::filesystem::remove(PortUtf8::FromUtf8(temporary), renameEc);
  }
}

template < typename T >
T Read(T Settings::* field) {
  ReadEnvironment();
  LoadConfig();
  std::lock_guard< std::mutex > lock(sMutex);
  return sSettings.*field;
}

PortTouchPad::Pad& Pad() {
  std::lock_guard< std::mutex > lock(sPadMutex);
  if (!sPad.ok()) {
    sPad = PortTouchPad::Attach();
    if (!sPad.ok() && sPadAttachFailed) {
      // Failed before and was logged: this runs once per touch event while SDL's joystick
      // subsystem is not up, and a line per event would flood logcat.
      return sPad;
    }
    // One line per attach (success, or the first failure) so a device log says whether the virtual
    // pad ever existed; without it a broken overlay and a broken mapping look the same from
    // outside. stderr is the right sink rather than a direct logcat call: on Android the entry
    // repoints fd 2 at logcat (platform/main.cpp), so this reaches the same place and still
    // compiles for the host, which has no android/log.h.
    if (sPad.ok()) {
      std::fprintf(stderr, "[touch] pad attached id=%d gamepad=%d\n", static_cast< int >(sPad.id),
                   SDL_IsGamepad(sPad.id) ? 1 : 0);
    } else {
      sPadAttachFailed = true;
      std::fprintf(stderr, "[touch] pad attach failed: %s (retried every frame)\n", SDL_GetError());
    }
  }
  return sPad;
}

void PadButton(int button, bool down) {
  PortTouchPad::Pad& pad = Pad();
  if (pad.handle == nullptr) {
    return;
  }
  if (DebugPad()) {
    std::fprintf(stderr, "[touch] button %d %s\n", button, down ? "down" : "up");
  }
  SDL_SetJoystickVirtualButton(pad.handle, button, down);
}

// The player the wheels act on: player 0's state while the game is in play (MP1's wheels are
// disabled outside play too). The title and file select are a CMFGame too, in the 'FRND' front-end
// world, whose own Samus sits in the Morph Ball: that is not play. Game thread only.
CPlayerState* PlayerInPlay() {
  if (gpGameState == nullptr || PortHarness::FlowState() != kInGame ||
      gpGameState->GetGameMode().GetGameModeType() == 'FRND') {
    return nullptr;
  }
  return gpGameState->PlayerState(0).GetPtr();
}

// The wheels' item indices are CPlayerState's enum values (visors Combat/Echo/Scan/Dark, beams
// Power/Dark/Light/Annihilator) and the power-up item of a beam is its beam id.
CPlayerState::EBeamId BeamOf(int item) { return static_cast< CPlayerState::EBeamId >(item); }
CPlayerState::EItemType BeamItemOf(int item) {
  return static_cast< CPlayerState::EItemType >(item);
}

// A requested visor or beam, on the game thread. The console's path (`visor`/`beam` in
// platform/port_console.cpp), except that the scan visor's gun holster is not done: that needs
// the CStateManager, which nothing outside src/ holds, and CPlayerGun skips drawing in the scan
// visor regardless. Players 2-4 (`player` > 0) have no radial.
void ApplyRequests(CPlayerState* state, int player = 0) {
  int visor = sPendingVisor[player].exchange(-1, std::memory_order_acq_rel);
  int beam = sPendingBeam[player].exchange(-1, std::memory_order_acq_rel);
  if (player == 0) {
    visor = PortRadial::TakeRequest(PortRadial::kVisor, visor);
    beam = PortRadial::TakeRequest(PortRadial::kBeam, beam);
  }
  if (state == nullptr) {
    return;
  }
  if (visor >= 0 && visor < 4) {
    const CPlayerState::EPlayerVisor wanted = static_cast< CPlayerState::EPlayerVisor >(visor);
    if (state->HasVisor(wanted) && state->GetCurrentVisor() != wanted) {
      state->StartTransitionToVisor(wanted);
    }
  }
  if (beam >= 0 && beam < 4 && state->HasPowerUp(BeamItemOf(beam)) &&
      state->GetCurrentBeam() != BeamOf(beam)) {
    // CPlayerGun::HandleWeaponChange reads the beam from the state.
    state->SetCurrentBeam(BeamOf(beam));
  }
}

int BuildWheelState(CPlayerState* state) {
  if (state == nullptr) {
    return 0;
  }
  int bits = 1 << 12;
  for (int item = 0; item < 4; ++item) {
    if (state->HasVisor(static_cast< CPlayerState::EPlayerVisor >(item))) {
      bits |= 1 << item;
    }
    if (state->HasPowerUp(BeamItemOf(item))) {
      bits |= 1 << (4 + item);
    }
  }
  const int visor = static_cast< int >(state->GetCurrentVisor());
  const int beam = static_cast< int >(state->GetCurrentBeam());
  bits |= (visor >= 0 && visor < 4 ? visor : 0) << 8;
  bits |= (beam >= 0 && beam < 4 ? beam : 0) << 10;
  // Bit 13: morphed (published by CPlayer::Update); the Java view swaps to its Ball page.
  if (sMorphed[0].load(std::memory_order_acquire)) {
    bits |= 1 << 13;
  }
  return bits;
}

// Turbo: while the button is held (and the setting is on), A goes down for two ticks' time and
// up for two, as MP1's turbo fire mashes it. The input engine reads it (TurboDown) as a stock
// touch code bound to GameCube A, not the virtual pad, whose A a pad preset may remap.
void UpdateTurbo(bool enabled) {
  const bool held =
      enabled && sTurboHeld.load(std::memory_order_acquire) && PortHarness::FlowState() == kInGame;
  if (!held) {
    sTurboDown.store(false, std::memory_order_release);
    sTurboStartNs = 0;
    return;
  }
  const uint64_t now = SDL_GetTicksNS();
  if (sTurboStartNs == 0) {
    sTurboStartNs = now;
  }
  sTurboDown.store(((now - sTurboStartNs) / kTurboHalfNs) % 2 == 0, std::memory_order_release);
}

bool SDLCALL RadialLifecycleWatch(void*, SDL_Event* event) {
  if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
      event->type == SDL_EVENT_DID_ENTER_BACKGROUND ||
      event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
    sRadialTouchForeground.store(false, std::memory_order_release);
    PortRadial::CancelTouchLifecycle();
  } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND ||
             event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
    sRadialTouchForeground.store(true, std::memory_order_release);
  }
  return true;
}

} // namespace

namespace PortTouch {

void Initialize(const std::string& userDirectory) {
  ReadEnvironment();
  if (!userDirectory.empty()) {
    // The Java view's first draw can call Layout() on the UI thread at any moment.
    std::lock_guard< std::mutex > lock(sMutex);
    sConfigPath = userDirectory + "/touch.cfg";
  }
  LoadConfig();
}

void BeginFrame() {
  static const bool watching = SDL_AddEventWatch(RadialLifecycleWatch, nullptr);
  (void)watching;
  ReadEnvironment();
  LoadConfig();

#ifdef PORT_TOUCH_CONTROLS
  // Retry an attach that failed because SDL's joystick subsystem was not up yet, here on the
  // game thread rather than per touch event on the UI thread. Builds without touch controls have
  // nothing to press it, so they never attach it (as in MP1): a desktop player is not shown a
  // phantom controller and its glyphs.
  {
    bool missing;
    {
      std::lock_guard< std::mutex > lock(sPadMutex);
      missing = !sPad.ok();
    }
    if (missing) {
      Pad();
    }
  }
#endif

  // A pad that is not this overlay's own is a real one; while one is connected the overlay
  // steps aside (MP1 does the same), which is what `TakePhysicalInput` reports.
  {
    int count = 0;
    bool real = false;
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&count)) {
      for (int i = 0; i < count; ++i) {
        if (!SDL_IsJoystickVirtual(ids[i])) {
          real = true;
          break;
        }
      }
      SDL_free(ids);
    }
    sRealPad.store(real, std::memory_order_release);
  }

  // The aim source: the setting asks for it while touch is the last-used input (MP1's
  // TouchDirectAim), and the F1 overlay takes it away while it is up. A pad or keyboard press
  // hands aim back to its own settings, so with Twin Stick Aim off the left stick turns (#58).
  const bool aim = AimEnabled() && Active() && !OverlayVisible();
  PortMouse::SetTouchEnabled(aim);
  // Touch keeps its own flick stick switch while its twin stick is the last-used input.
  PortMouse::SetTouchTwinStickActive(Active() && TwinStick() && !OverlayVisible());
  if (!aim) {
    // The finger's release may never arrive (the overlay opened under it).
    PortMouse::SetTouchAimDown(false);
  }
  PortMouse::SetTouchAimSpeed(AimSpeed());
  PortMouse::SetTouchAimSpeedY(AimSplit() ? AimSpeedY() : AimSpeed());

  CPlayerState* const player = PlayerInPlay();
  PortRadial::SetTouchContext(player && ControlsEnabled() && !OverlayVisible() && !PortMouse::UiActive() &&
                             sRadialTouchForeground.load(std::memory_order_acquire));
  ApplyRequests(player);
  for (int p = 1; p < kPlayers; ++p) {
    const bool inMatch =
        player != nullptr && p < static_cast< int >(gpGameState->GetGameMode().GetNumPlayers());
    ApplyRequests(inMatch ? gpGameState->PlayerState(p).GetPtr() : nullptr, p);
  }
  sWheelState.store(BuildWheelState(player), std::memory_order_release);
  UpdateTurbo(Turbo());

  // SetLayout and CycleLayout save for themselves; this flush writes what a setter left dirty (a
  // failed write is not retried until the next change).
  std::lock_guard< std::mutex > lock(sMutex);
  if (sDirty) {
    SaveConfigLocked();
  }
}

void SetButton(int button, bool down) {
  // The twin layout's Beam button turns the D-pad into the beam picker (MP1's Remastered beam
  // shift): up Power, right Light, down Annihilator, left Dark.
  if (button >= kDpadUp && button < kDpadUp + 4) {
    const int index = button - kDpadUp; // up, down, left, right
    if (down && sBeamShift.load(std::memory_order_acquire)) {
      static const int kBeams[4] = {0, 3, 1, 2};
      RequestBeam(kBeams[index]);
      sDpadShifted[index] = true;
      return;
    }
    if (!down && sDpadShifted[index]) {
      sDpadShifted[index] = false;
      return;
    }
  }
  PadButton(button, down);
}

void SetAxis(int axis, float value) {
  PortTouchPad::Pad& pad = Pad();
  if (pad.handle == nullptr) {
    return;
  }
  // Reported once per move, not once per pixel, so the log stays readable.
  static float lastLogged[SDL_GAMEPAD_AXIS_COUNT] = {};
  if (DebugPad() && axis >= 0 && axis < SDL_GAMEPAD_AXIS_COUNT &&
      std::fabs(value - lastLogged[axis]) > 0.02f) {
    lastLogged[axis] = value;
    std::fprintf(stderr, "[touch] axis %d %.2f\n", axis, value);
  }
  // The overlay sends a normalised -1..1 deflection; the virtual joystick takes a Sint16.
  SDL_SetJoystickVirtualAxis(pad.handle, axis, PortTouchPad::AxisValue(value));
}

void AddAim(float dxDp, float dyDp) {
  if (DebugPad()) {
    std::fprintf(stderr, "[touch] aim %.1f %.1f dp\n", dxDp, dyDp);
  }
  PortMouse::AddTouchAim(dxDp, dyDp);
}

void SetAimDown(bool down) { PortMouse::SetTouchAimDown(down); }

// MP1's TouchSpringSwipe: the swipe both finishes the aim flick and asks CMorphBall for a Spring
// Ball, which it does only on the ground in the ball with Spring Ball on, so elsewhere it expires.
std::atomic< uint64_t > sSpringRequestUntilNs[kPlayers]{};
constexpr uint64_t kSpringRequestNs = 120'000'000;

void SpringSwipe() {
  if (!SpringSwipeEnabled()) return;
  PortMouse::SpringSwipe();
  RequestSpringBall(kSpringRequestNs * 1e-9f);
}

bool SpringBallRequested(int player) {
  return ValidPlayer(player) && SDL_GetTicksNS() < sSpringRequestUntilNs[player].load();
}

void RequestSpringBall(float seconds, int player) {
  if (ValidPlayer(player)) sSpringRequestUntilNs[player].store(SDL_GetTicksNS() + uint64_t(seconds * 1e9f));
}

void ClearSpringBallRequest(int player) {
  if (ValidPlayer(player)) sSpringRequestUntilNs[player].store(0);
}

std::atomic< uint64_t > sDodgeRequestUntilNs[kPlayers]{};

void RequestDodge(int player) {
  if (ValidPlayer(player)) sDodgeRequestUntilNs[player].store(SDL_GetTicksNS() + 120'000'000);
}

bool TakeDodgeRequest(int player) {
  if (!ValidPlayer(player)) return false;
  const uint64_t until = sDodgeRequestUntilNs[player].exchange(0);
  return SDL_GetTicksNS() < until && !PortOverlay::OriginalExperience();
}

bool AimEnabled() { return Read(&Settings::aim); }

float AimSpeed() { return Read(&Settings::aimSpeed); }
bool AimSplit() { return Read(&Settings::aimSplit); }
float AimSpeedY() { return Read(&Settings::aimSpeedY); }

bool Classic() { return Read(&Settings::classic); }
bool TwinStick() { return Read(&Settings::twin); }
bool Labels() { return Read(&Settings::labels); }
bool Colors() { return Read(&Settings::colors); }
bool FloatingStick() { return Read(&Settings::floating); }
bool Turbo() { return Read(&Settings::turbo); }
bool WheelsEnabled() { return Read(&Settings::wheels); }
bool VisorTapScan() { return Read(&Settings::visorTapScan); }
bool ControlsEnabled() { return Read(&Settings::controls); }
float SideMarginDp() { return Read(&Settings::sideMargin); }
float StickInsetDp() { return Read(&Settings::stickInset); }
float ButtonInsetDp() { return Read(&Settings::buttonInset); }
float Opacity() { return Read(&Settings::opacity); }
bool SpringSwipeEnabled() { return Read(&Settings::springSwipe); }

std::string Layout() { return Read(&Settings::layout); }

void SetLayout(const std::string& layout) {
  LoadConfig();
  std::lock_guard< std::mutex > lock(sMutex);
  if (sSettings.layout == layout) {
    return;
  }
  sSettings.layout = layout;
  PortRadial::CancelAllTouches();
  sDirty = true;
  // Under the same lock the mutation took: never a half-updated Settings in the file.
  SaveConfigLocked();
}

namespace {
void SetSetting(bool Settings::* field, bool value) {
  LoadConfig();
  std::lock_guard< std::mutex > lock(sMutex);
  if (sSettings.*field == value) {
    return;
  }
  sSettings.*field = value;
  if (field == &Settings::controls || field == &Settings::wheels ||
      field == &Settings::classic || field == &Settings::twin) PortRadial::CancelAllTouches();
  sDirty = true;
  SaveConfigLocked();
}
} // namespace

void SetControlsEnabled(bool enabled) { SetSetting(&Settings::controls, enabled); }

void SetAimEnabled(bool enabled) { SetSetting(&Settings::aim, enabled); }
void SetColors(bool enabled) { SetSetting(&Settings::colors, enabled); }
void SetLabels(bool enabled) { SetSetting(&Settings::labels, enabled); }
void SetTurbo(bool enabled) { SetSetting(&Settings::turbo, enabled); }
void SetFloatingStick(bool enabled) { SetSetting(&Settings::floating, enabled); }
void SetWheelsEnabled(bool enabled) { SetSetting(&Settings::wheels, enabled); }
void SetVisorTapScan(bool enabled) { SetSetting(&Settings::visorTapScan, enabled); }
void SetSpringSwipe(bool enabled) { SetSetting(&Settings::springSwipe, enabled); }
void SetAimSplit(bool enabled) { SetSetting(&Settings::aimSplit, enabled); }

namespace {
void SetNumber(float Settings::* field, float value, float low, float high) {
  LoadConfig();
  std::lock_guard< std::mutex > lock(sMutex);
  value = value < low ? low : (value > high ? high : value);
  if (sSettings.*field == value) {
    return;
  }
  sSettings.*field = value;
  sDirty = true;
  SaveConfigLocked();
}
} // namespace

void SetSideMarginDp(float dp) { SetNumber(&Settings::sideMargin, dp, 0.f, 300.f); }
void SetStickInsetDp(float dp) { SetNumber(&Settings::stickInset, dp, 0.f, 300.f); }
void SetButtonInsetDp(float dp) { SetNumber(&Settings::buttonInset, dp, 0.f, 300.f); }
void SetOpacity(float percent) { SetNumber(&Settings::opacity, percent, 10.f, 100.f); }
void SetAimSpeed(float pxPerDp) { SetNumber(&Settings::aimSpeed, pxPerDp, 0.5f, 6.f); }
void SetAimSpeedY(float pxPerDp) { SetNumber(&Settings::aimSpeedY, pxPerDp, 0.5f, 6.f); }

void SetMorphed(bool morphed, int player) {
  if (ValidPlayer(player)) sMorphed[player].store(morphed, std::memory_order_release);
}
bool Morphed(int player) { return ValidPlayer(player) && sMorphed[player].load(std::memory_order_acquire); }
void SetOrbitLocked(bool locked, int player) {
  if (ValidPlayer(player)) sOrbitLocked[player].store(locked, std::memory_order_release);
}
bool OrbitLocked(int player) {
  return ValidPlayer(player) && sOrbitLocked[player].load(std::memory_order_acquire);
}
void SetScanTextUp(bool up) { sScanTextUp.store(up, std::memory_order_release); }
bool ScanTextUp() { return sScanTextUp.load(std::memory_order_acquire); }

void RequestEdit() {
  PortRadial::CancelAllTouches();
  sEditRequested.store(true, std::memory_order_release);
}

bool TakeEditRequested() { return sEditRequested.exchange(false, std::memory_order_acq_rel); }

void CycleLayout() {
  PortRadial::CancelAllTouches();
  LoadConfig();
  std::lock_guard< std::mutex > lock(sMutex);
  Settings& s = sSettings;
  if (s.classic) {
    s.classic = false;
    s.twin = true;
  } else if (s.twin) {
    s.twin = false;
  } else {
    s.classic = true;
  }
  std::fprintf(stderr, "[touch] layout -> %s\n",
               s.classic ? "classic" : (s.twin ? "twin stick" : "default"));
  sDirty = true;
  SaveConfigLocked();
}

int WheelState() { return sWheelState.load(std::memory_order_acquire); }

void RequestVisor(int visor, int player) {
  if (ValidPlayer(player)) sPendingVisor[player].store(visor, std::memory_order_release);
}

void RequestBeam(int beam, int player) {
  if (ValidPlayer(player)) sPendingBeam[player].store(beam, std::memory_order_release);
}

namespace {
struct WheelIconData {
  int w = 0;
  int h = 0;
  std::vector< uint32_t > argb;
};
std::mutex sWheelIconMutex;
WheelIconData sWheelIcons[2][4];
} // namespace

void CaptureWheelIcons(int wheel, CGuiWidget* const* icons) {
  if (wheel < 0 || wheel > 1) {
    return;
  }
  for (int i = 0; i < 4; ++i) {
    {
      std::lock_guard< std::mutex > lock(sWheelIconMutex);
      if (!sWheelIcons[wheel][i].argb.empty()) {
        continue;
      }
    }
    if (icons[i] == nullptr || icons[i]->GetWidgetTypeID() != 'MODL') {
      continue;
    }
    CGuiModel::PortIconLayer layers[4];
    const int layerCount = static_cast< const CGuiModel* >(icons[i])->PortIconLayers(layers, 4);
    if (layerCount == 0) {
      continue;
    }
    // The first layer is this icon's cell in the shared atlas (one 256x128 texture), mapped
    // bottom row first (v = 0 is the bottom): cut it out and flip it upright.
    uint32_t w = 0;
    uint32_t h = 0;
    std::vector< uint8_t > rgba(256 * 256 * 4);
    const CTexture* tex = layers[0].tex;
    if (!aurora_gx_texobj_rgba8(tex->PortTexObj(), &w, &h, rgba.data(), rgba.size()) || w == 0 ||
        h == 0) {
      continue;
    }
    float uv[4] = {1e9f, 1e9f, -1e9f, -1e9f};
    for (const auto& corner : layers[0].uv) {
      uv[0] = std::min(uv[0], corner[0]);
      uv[1] = std::min(uv[1], corner[1]);
      uv[2] = std::max(uv[2], corner[0]);
      uv[3] = std::max(uv[3], corner[1]);
    }
    const int x0 = std::max(0, int(std::floor(uv[0] * w + 0.5f)));
    const int x1 = std::min(int(w), int(std::floor(uv[2] * w + 0.5f)));
    const int y0 = std::max(0, int(std::floor((1.f - uv[3]) * h + 0.5f)));
    const int y1 = std::min(int(h), int(std::floor((1.f - uv[1]) * h + 0.5f)));
    if (x1 <= x0 || y1 <= y0) {
      continue;
    }
    WheelIconData icon;
    icon.w = x1 - x0;
    icon.h = y1 - y0;
    icon.argb.resize(size_t(icon.w) * icon.h);
    for (int y = 0; y < icon.h; ++y) {
      for (int x = 0; x < icon.w; ++x) {
        const uint8_t* c = &rgba[(size_t(h - 1 - (y0 + y)) * w + (x0 + x)) * 4];
        icon.argb[size_t(y) * icon.w + x] =
            uint32_t(c[3]) << 24 | uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
      }
    }
    // Further layers (the Annihilator Beam's centre triangle is a second surface with its own
    // texture and its own rotation of it) are drawn over the first, so put each over the cell.
    // The canvas is the first quad's x/z box; a layer's texture coordinates are affine in x/z.
    float xzMin[2] = {1e9f, 1e9f};
    float xzMax[2] = {-1e9f, -1e9f};
    for (const auto& corner : layers[0].xz) {
      for (int axis = 0; axis < 2; ++axis) {
        xzMin[axis] = std::min(xzMin[axis], corner[axis]);
        xzMax[axis] = std::max(xzMax[axis], corner[axis]);
      }
    }
    for (int l = 1; l < layerCount; ++l) {
      const auto& layer = layers[l];
      uint32_t lw = 0;
      uint32_t lh = 0;
      std::vector< uint8_t > lrgba(256 * 256 * 4);
      if (!aurora_gx_texobj_rgba8(layer.tex->PortTexObj(), &lw, &lh, lrgba.data(), lrgba.size()) ||
          lw == 0 || lh == 0) {
        continue;
      }
      const float e1x = layer.xz[1][0] - layer.xz[0][0];
      const float e1z = layer.xz[1][1] - layer.xz[0][1];
      const float e2x = layer.xz[3][0] - layer.xz[0][0];
      const float e2z = layer.xz[3][1] - layer.xz[0][1];
      const float det = e1x * e2z - e1z * e2x;
      if (std::fabs(det) < 1e-12f || xzMax[0] <= xzMin[0] || xzMax[1] <= xzMin[1]) {
        continue;
      }
      for (int y = 0; y < icon.h; ++y) {
        for (int x = 0; x < icon.w; ++x) {
          const float px = xzMin[0] + (x + 0.5f) / icon.w * (xzMax[0] - xzMin[0]) - layer.xz[0][0];
          const float pz = xzMax[1] - (y + 0.5f) / icon.h * (xzMax[1] - xzMin[1]) - layer.xz[0][1];
          const float s = (px * e2z - pz * e2x) / det; // along corner 0 -> 1
          const float t = (e1x * pz - e1z * px) / det; // along corner 0 -> 3
          const float u = layer.uv[0][0] + s * (layer.uv[1][0] - layer.uv[0][0]) +
                          t * (layer.uv[3][0] - layer.uv[0][0]);
          const float v = layer.uv[0][1] + s * (layer.uv[1][1] - layer.uv[0][1]) +
                          t * (layer.uv[3][1] - layer.uv[0][1]);
          // Bilinear, texel centres at k + 0.5; rgba rows run bottom first (row = v * height).
          const float fx = u * lw - 0.5f;
          const float fy = v * lh - 0.5f;
          const int ix = int(std::floor(fx));
          const int iy = int(std::floor(fy));
          const float ax = fx - ix;
          const float ay = fy - iy;
          float sample[4] = {};
          for (int k = 0; k < 4; ++k) {
            const int cx = std::clamp(ix + (k & 1), 0, int(lw) - 1);
            const int cy = std::clamp(iy + (k >> 1), 0, int(lh) - 1);
            const float wgt = ((k & 1) ? ax : 1.f - ax) * ((k >> 1) ? ay : 1.f - ay);
            for (int ch = 0; ch < 4; ++ch) {
              sample[ch] += wgt * lrgba[(size_t(cy) * lw + cx) * 4 + ch];
            }
          }
          // Source-over: the texture's alpha is the layer's coverage.
          uint32_t& dst = icon.argb[size_t(y) * icon.w + x];
          const float sa = sample[3] / 255.f;
          const float da = float(dst >> 24) / 255.f;
          const float oa = sa + da * (1.f - sa);
          uint32_t out = uint32_t(std::lround(oa * 255.f)) << 24;
          for (int ch = 0; ch < 3; ++ch) {
            const float dc = float((dst >> (16 - 8 * ch)) & 0xff);
            const float c = oa > 0.f ? (sample[ch] * sa + dc * da * (1.f - sa)) / oa : 0.f;
            out |= uint32_t(std::lround(std::min(255.f, c))) << (16 - 8 * ch);
          }
          dst = out;
        }
      }
    }
    if (const char* dumpDir = std::getenv("MP2_DUMP_WHEEL_ICONS")) {
      // Debug: the finished icon as a PAM (RGB_ALPHA), icon_<wheel>_<item>.pam.
      char name[512];
      std::snprintf(name, sizeof(name), "%s/icon_%d_%d.pam", dumpDir, wheel, i);
      if (FILE* f = std::fopen(name, "wb")) {
        std::fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n",
                     icon.w, icon.h);
        for (uint32_t px : icon.argb) {
          const uint8_t o[4] = {uint8_t(px >> 16), uint8_t(px >> 8), uint8_t(px), uint8_t(px >> 24)};
          std::fwrite(o, 1, 4, f);
        }
        std::fclose(f);
      }
    }
    std::fprintf(stderr, "[touch] wheel icon %d/%d: %dx%d of %ux%u fmt %d\n", wheel, i, icon.w,
                 icon.h, w, h, int(tex->GetTexelFormat()));
    std::lock_guard< std::mutex > lock(sWheelIconMutex);
    sWheelIcons[wheel][i] = std::move(icon);
  }
}

bool WheelIcon(int wheel, int item, std::vector< uint32_t >& out) {
  if (wheel < 0 || wheel > 1 || item < 0 || item > 3) {
    return false;
  }
  std::lock_guard< std::mutex > lock(sWheelIconMutex);
  const WheelIconData& icon = sWheelIcons[wheel][item];
  if (icon.argb.empty()) {
    return false;
  }
  out.assign(1, uint32_t(icon.w));
  out.push_back(uint32_t(icon.h));
  out.insert(out.end(), icon.argb.begin(), icon.argb.end());
  return true;
}

void SetBeamShift(bool held) { sBeamShift.store(held, std::memory_order_release); }

void SetTurboFire(bool held) { sTurboHeld.store(held, std::memory_order_release); }
bool TurboDown() { return sTurboDown.load(std::memory_order_acquire); }

void SetMenuBack(bool held) {
  if (held) {
    sMenuBackPressed.store(true, std::memory_order_release);
  }
  sMenuBackHeld.store(held, std::memory_order_release);
}

bool TakeMenuBack() {
  const bool pressed = sMenuBackPressed.exchange(false, std::memory_order_acq_rel);
  return sMenuBackHeld.load(std::memory_order_acquire) || pressed;
}

bool PauseScreenOpen() { return PortHarness::FlowState() == kPaused; }

// Photo mode hides the touch controls too (its own touch handling flies the camera).
bool OverlayVisible() { return PortOverlay::Visible() || PortPhoto::Active(); }

void NoteDevice(bool xboxLayout) {
  sActivity.Touch();
  // The prompt icons follow the on-screen layout while touch is the active input.
  sDeviceXbox.store(xboxLayout, std::memory_order_release);
  PortPrompts::NoteTouchInput(xboxLayout);
}

bool DeviceIsXbox() { return sDeviceXbox.load(std::memory_order_acquire); }

bool Active() {
#if defined(PORT_TOUCH_CONTROLS)
  return sActivity.Active(true, PortOverlay::Visible());
#else
  return false;
#endif
}

void NotePhysicalInput() {
  sActivity.Physical();
  sPhysicalInput.store(true, std::memory_order_release);
}

bool TakePhysicalInput() {
  const bool event = sPhysicalInput.exchange(false, std::memory_order_acq_rel);
  return event || sRealPad.load(std::memory_order_acquire);
}

} // namespace PortTouch
