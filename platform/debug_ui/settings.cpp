// F1 overlay: the settings file: one table of keys (kSettingKeys) that reads and writes
// <user>/settings.cfg, saving it, and the settings self-test. A port of MP1's
// debug_ui/settings.cpp; the keys and the variables they reach are MP2's. State shared with
// port_overlay.cpp is in port_overlay_internal.h.

#include "../port_overlay_internal.h"

#include "port_discord.h"
#include "port_env.h"
#include "port_livesplit.h"
#include "port_mods.h"
#include "port_mouse.h"
#include "port_overlay.h"
#include "port_photo.h"
#include "port_save.h"
#include "port_settings.h"
#include "port_suit_colours.h"
#include "port_textures.h"
#include "port_update_check.h"
#include "port_video.h"

#include "Kyoto/Streams/CBitStreamReader.hpp"
#include "Kyoto/Streams/CBitStreamWriter.hpp"
#include "Kyoto/Streams/CMemoryInStream.hpp"
#include "Kyoto/Streams/CMemoryStreamOut.hpp"
#include "MetroidPrime/Player/CGameOptions.hpp"
#include "MetroidPrime/Player/CGameState.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

#if !defined(_WIN32)
// `environ`, for the MP2_* dump below. Bionic declares it only from here (unistd.h), so a
// desktop-only include chain left this undeclared on Android.
#include <unistd.h>
#endif
#if defined(__APPLE__)
extern char** environ; // not declared by Apple's unistd.h
#endif

using namespace PortOverlayState;

namespace PortDebug {
void ApplyUpdateCheck(); // game_sections.cpp
}

namespace {

std::string Trim(const std::string& s) {
  size_t a = 0;
  size_t b = s.size();
  while (a < b && std::isspace(static_cast< unsigned char >(s[a])))
    ++a;
  while (b > a && std::isspace(static_cast< unsigned char >(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

bool ParseBool(const std::string& value) {
  std::string lower;
  for (char c : value)
    lower += static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
  return !(lower.empty() || lower == "0" || lower == "false" || lower == "off" || lower == "no");
}

// A whole-string integer; false for anything else.
bool ParseInt(const std::string& value, int& out) {
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (value.empty() || end == value.c_str() || *end != '\0')
    return false;
  out = static_cast< int >(parsed);
  return true;
}

// A whole-string float; false for anything else, NaN and infinity included.
bool ParseFloat(const std::string& value, float& out) {
  char* end = nullptr;
  const float parsed = std::strtof(value.c_str(), &end);
  if (value.empty() || end == value.c_str() || *end != '\0' || !std::isfinite(parsed))
    return false;
  out = parsed;
  return true;
}

// Three comma-separated floats, each clamped to [min, max].
bool ParseFloat3(const std::string& value, float out[3], float min, float max) {
  float parsed[3];
  char extra = 0;
  if (std::sscanf(value.c_str(), "%f,%f,%f%c", &parsed[0], &parsed[1], &parsed[2], &extra) != 3)
    return false;
  for (float v : parsed) {
    if (!std::isfinite(v))
      return false;
  }
  for (int i = 0; i < 3; ++i)
    out[i] = std::clamp(parsed[i], min, max);
  return true;
}

// One key of settings.cfg: how its value is read and how it is written.
// The table is the file's order. ApplySetting reads through it, SettingsText
// writes through it, and SettingsSelfTest checks that each key's write reads
// back as the same value.
struct SettingKey {
  const char* key;
  // A value that does not parse or is out of range is ignored.
  void (*read)(const std::string& value);
  // The value only, after "key="; nullptr for an old name that is only read.
  void (*write)(std::ostream& out);
  // Write no line while the value is empty.
  bool omitEmpty;
  // Two different values as written, which the self-test applies in turn and
  // expects back; nullptr when the written form is not the one read.
  const char* sampleA;
  const char* sampleB;
};

void WriteBool(std::ostream& out, bool value) { out << (value ? 1 : 0); }

// suit_colour_<suit>: PortSuitColours::PicksKey, a slot each ("FFD000,-").
template < int Suit >
void ReadSuitColours(const std::string& value) {
  PortSuitColours::SetPicksKey(Suit, value);
}

template < int Suit >
void WriteSuitColours(std::ostream& out) {
  out << PortSuitColours::PicksKey(Suit);
}

// A key that is one bool: read with ParseBool, written as 1 or 0, sampled with both.
template < bool* Flag >
constexpr SettingKey BoolKey(const char* key) {
  return {key,
          [](const std::string& value) { *Flag = ParseBool(value); },
          [](std::ostream& out) { WriteBool(out, *Flag); },
          false,
          "1",
          "0"};
}

// A key that is one int in [Min, Max]: anything else is ignored.
template < int* Value, int Min, int Max >
constexpr SettingKey IntKey(const char* key, const char* sampleA, const char* sampleB) {
  return {key,
          [](const std::string& value) {
            int parsed = 0;
            if (ParseInt(value, parsed) && parsed >= Min && parsed <= Max)
              *Value = parsed;
          },
          [](std::ostream& out) { out << *Value; },
          false,
          sampleA,
          sampleB};
}

// A key that is one float in [Min, Max] (whole-number bounds): anything else is ignored.
template < float* Value, int Min, int Max >
constexpr SettingKey FloatKey(const char* key, const char* sampleA, const char* sampleB) {
  return {key,
          [](const std::string& value) {
            float parsed = 0.f;
            if (ParseFloat(value, parsed) && parsed >= float(Min) && parsed <= float(Max))
              *Value = parsed;
          },
          [](std::ostream& out) { out << *Value; },
          false,
          sampleA,
          sampleB};
}

// CGameOptions::PutTo as hex, the 0x20 bytes CGameState::RecordCompressedGameOptions keeps per save
// file; empty until the game's options are first recorded.
constexpr size_t kGameOptionsBytes = 0x20;
std::string sGameOptions;
// The disc image F1 > System > Disc image > Replace remembered for the next start (port_disc_replace.h).
std::string sDiscPath;
bool sGameOptionsRestored = false;

const SettingKey kSettingKeys[] = {
    BoolKey< &sOriginalExperience >("original_experience"),
    BoolKey< &sSkipCinematics >("skip_cinematics"),
    BoolKey< &sUnlockHardMode >("unlock_hard_mode"),
    BoolKey< &sUnlockGalleries >("unlock_galleries"),
    BoolKey< &sUnlockBeamDoors >("unlock_beam_doors"),
    BoolKey< &sStackBeams >("stack_beams"),
    BoolKey< &sStackBeamsSkipDark >("stack_beams_skip_dark"),
    IntKey< &sBootLogos, PortOverlay::kScreen_Show, PortOverlay::kScreen_Off >(
        "boot_logos", "0", "2"),
    IntKey< &sWorldNameCard, PortOverlay::kScreen_Show, PortOverlay::kScreen_Off >(
        "world_name_card", "0", "2"),
    BoolKey< &sFastMorph >("fast_morph"),
    BoolKey< &sPointerMenus >("pointer_menus"),
    BoolKey< &sMapSwapFingers >("map_swap_fingers"),
    BoolKey< &sMapInvertX >("map_invert_x"),
    BoolKey< &sMapInvertY >("map_invert_y"),
    IntKey< &sWalkSpeed, PortOverlay::kWalkSpeedMin, PortOverlay::kWalkSpeedMax >("walk_speed", "50",
                                                                                  "200"),
    IntKey< &sScanSpeed, PortOverlay::kScanSpeedMin, PortOverlay::kScanSpeedMax >("scan_speed", "50",
                                                                                  "400"),
    // The F1 Game > Audio rows' part above 100 percent: the mixer reads a boost, so a loaded value
    // is handed to it here as well as by the rows (a plain IntKey could not).
    {"music_boost",
     [](const std::string& value) {
       int parsed = 0;
       if (ParseInt(value, parsed) && parsed >= PortOverlay::kVolumeBoostMin &&
           parsed <= PortOverlay::kVolumeBoostMax) {
         sMusicBoost = parsed;
         ApplyVolumeBoosts();
       }
     },
     [](std::ostream& out) { out << sMusicBoost; }, false, "100", "200"},
    {"sfx_boost",
     [](const std::string& value) {
       int parsed = 0;
       if (ParseInt(value, parsed) && parsed >= PortOverlay::kVolumeBoostMin &&
           parsed <= PortOverlay::kVolumeBoostMax) {
         sSfxBoost = parsed;
         ApplyVolumeBoosts();
       }
     },
     [](std::ostream& out) { out << sSfxBoost; }, false, "100", "200"},
    IntKey< &sElevatorRide, PortOverlay::kElevatorRide_Original, PortOverlay::kElevatorRide_Skip >(
        "elevator_ride", "0", "2"),
    IntKey< &sRotatingRooms, PortOverlay::kRotatingRooms_Original, PortOverlay::kRotatingRooms_Fast >(
        "rotating_rooms", "0", "1"),
    IntKey< &sComboBeamCost, 0, PortOverlay::kComboBeamCostMax >("combo_beam_cost", "0", "30"),
    IntKey< &sComboMissileCost, 0, PortOverlay::kComboMissileCostMax >("combo_missile_cost", "0",
                                                                       "5"),
    IntKey< &sBeamAmmoPerExpansion, PortOverlay::kBeamAmmoPerExpansionMin,
            PortOverlay::kBeamAmmoPerExpansionMax >("beam_ammo_per_expansion", "50", "100"),
    IntKey< &sPortalTransition, PortOverlay::kPortalTransition_Original,
            PortOverlay::kPortalTransition_Skip >("portal_transition", "0", "2"),
    IntKey< &sPromptGlyphs, PortOverlay::kPromptGlyphs_Auto, PortOverlay::kPromptGlyphs_Keyboard >(
        "prompt_glyphs", "0", "5"),
    BoolKey< &sMinimapTap >("minimap_tap"),
    BoolKey< &sIgtTimer >("igt_timer"),
    IntKey< &sLanguage, 0, PortOverlay::kLanguageMax >("language", "0", "6"),
    IntKey< &sHintDelay, PortOverlay::kHintDelayMin, PortOverlay::kHintDelayMax >("hint_delay", "0",
                                                                                  "30"),
    BoolKey< &sSpringBall >("spring_ball"),
    BoolKey< &sSpringFlick >("spring_ball_flick"),
    {"spring_ball_flick_rate",
     [](const std::string& value) {
       float parsed = 0.f;
       if (ParseFloat(value, parsed) && parsed >= PortOverlay::kSpringFlickRateMin &&
           parsed <= PortOverlay::kSpringFlickRateMax)
         sSpringFlickRate = parsed;
     },
     [](std::ostream& out) { out << sSpringFlickRate; },
     false,
     "2",
     "20"},
    BoolKey< &sQuickResume >("quick_resume"),
    BoolKey< &sSaveStateHotkeys >("savestate_hotkeys"),
    BoolKey< &sSaveStateRoomState >("savestate_room_state"),
    BoolKey< &sLockOnToggle >("lockon_toggle"),
    BoolKey< &sStickyCharge >("sticky_charge"),
    BoolKey< &sRapidCharge >("rapid_charge"),
    BoolKey< &sAutoAim >("auto_aim"),
    BoolKey< &sPointerAim >("pointer_aim"),
    IntKey< &sPointerAimBox, PortOverlay::kPointerAimBoxMin, PortOverlay::kPointerAimBoxMax >(
        "pointer_aim_box", "50", "70"),
    IntKey< &sPointerAimTurn, PortOverlay::kPointerAimTurnMin, PortOverlay::kPointerAimTurnMax >(
        "pointer_aim_turn", "180", "360"),
    BoolKey< &sGyroAim >("gyro_aim"),
    IntKey< &sGyroSource, 0, 2 >("gyro_source", "0", "1"),
    IntKey< &sGyroSpace, 0, 4 >("gyro_space", "1", "2"),
    IntKey< &sGyroAutoCalibrate, 0, 2 >("gyro_auto_calibrate", "1", "2"),
    FloatKey< &sGyroSmoothing, 0, 50 >("gyro_smoothing", "0", "10"),
    FloatKey< &sGyroTightening, 0, 50 >("gyro_tightening", "0", "5"),
    FloatKey< &sGyroCursorAngle, 5, 180 >("gyro_cursor_angle", "40", "60"),
    IntKey< &sGyroMode, PortOverlay::kGyroModeMin, PortOverlay::kGyroModeMax >("gyro_mode", "2", "1"),
    // 0.1..10 has no whole-number lower bound, so not a FloatKey.
    {"gyro_sensitivity",
     [](const std::string& value) {
       float parsed = 0.f;
       if (ParseFloat(value, parsed) && parsed >= PortOverlay::kGyroSensitivityMin &&
           parsed <= PortOverlay::kGyroSensitivityMax)
         sGyroSensitivity = parsed;
     },
     [](std::ostream& out) { out << sGyroSensitivity; },
     false,
     "2",
     "1"},
    BoolKey< &sGyroCursorCamera >("gyro_cursor_camera"),
    BoolKey< &sGyroSplit >("gyro_split"),
    {"gyro_sensitivity_y",
     [](const std::string& value) {
       float parsed = 0.f;
       if (ParseFloat(value, parsed) && parsed >= PortOverlay::kGyroSensitivityMin &&
           parsed <= PortOverlay::kGyroSensitivityMax)
         sGyroSensitivityY = parsed;
     },
     [](std::ostream& out) { out << sGyroSensitivityY; },
     false,
     "2",
     "1"},
    BoolKey< &sLockOnFreeAim >("lock_on_free_aim"),
    BoolKey< &sResetAimBoth >("reset_aim_both"),
    BoolKey< &sMenuStandardButtons >("menu_standard_buttons"),
    // The emulator whose card the port shares (System > Memory card); normalised on the way in.
    {"emulator_folder", [](const std::string& value) { PortSave::SetEmulatorFolder(value); },
     [](std::ostream& out) { out << PortSave::EmulatorFolder(); }, true, "/tmp/mp2-emu-a",
     "/tmp/mp2-emu-b"},
    {"share_emulator_card",
     [](const std::string& value) { PortSave::SetShareEmulatorCard(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, PortSave::ShareEmulatorCard()); }, false, "1", "0"},
    {"texture_pack", [](const std::string& value) { PortTextures::SetUserPackEnabled(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, PortTextures::UserPackEnabled()); }, false, "0", "1"},
    {"mirror_trilogy_save",
     [](const std::string& value) { PortSave::SetMirrorTrilogy(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, PortSave::MirrorTrilogy()); }, false, "1", "0"},
    // Mouse and stick aim. These live in PortMouse rather than here, and the file is read by
    // PortMouse's own ReadEnv (the file first, the MP2_* environment on top), so the rows below
    // would otherwise be the only F1 switches a restart forgot.
    {"mouse_aim",
     [](const std::string& value) { PortMouse::SetEnabled(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, PortMouse::Enabled()); }, false, "1", "0"},
    {"mouse_invert_x",
     [](const std::string& value) { PortMouse::SetInvertX(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, PortMouse::InvertX()); }, false, "1", "0"},
    {"mouse_invert_y",
     [](const std::string& value) { PortMouse::SetInvertY(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, PortMouse::InvertY()); }, false, "1", "0"},
    {"mouse_sensitivity",
     [](const std::string& value) {
       float parsed = 0.f;
       if (ParseFloat(value, parsed) && parsed >= 0.0001f && parsed <= 0.1f) {
         PortMouse::SetSensitivity(parsed);
       }
     },
     [](std::ostream& out) { out << PortMouse::Sensitivity(); }, false, "0.0035", "0.007"},
    {"mouse_sensitivity_split",
     [](const std::string& value) { PortMouse::SetSensitivitySplit(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, PortMouse::SensitivitySplit()); }, false, "1", "0"},
    {"mouse_sensitivity_y",
     [](const std::string& value) {
       float parsed = 0.f;
       if (ParseFloat(value, parsed) && parsed >= 0.0001f && parsed <= 0.1f) {
         PortMouse::SetSensitivityY(parsed);
       }
     },
     [](std::ostream& out) { out << PortMouse::SensitivityY(); }, false, "0.0035", "0.007"},
    {"mouse_dpi",
     [](const std::string& value) {
       int parsed = 0;
       if (ParseInt(value, parsed) && parsed >= 100 && parsed <= 64000) {
         PortMouse::SetMouseDpi(parsed);
       }
     },
     [](std::ostream& out) { out << PortMouse::MouseDpi(); }, false, "800", "1600"},
    {"twin_stick",
     [](const std::string& value) { PortMouse::SetStickEnabled(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, PortMouse::StickEnabled()); }, false, "1", "0"},
    {"stick_aim_rate",
     [](const std::string& value) {
       float parsed = 0.f;
       if (ParseFloat(value, parsed) && parsed >= 50.f && parsed <= 10000.f) {
         PortMouse::SetStickAimRate(parsed);
       }
     },
     [](std::ostream& out) { out << PortMouse::StickAimRate(); }, false, "900", "1200"},
    {"stick_aim_split",
     [](const std::string& value) { PortMouse::SetStickAimSplit(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, PortMouse::StickAimSplit()); }, false, "1", "0"},
    {"stick_aim_rate_y",
     [](const std::string& value) {
       float parsed = 0.f;
       if (ParseFloat(value, parsed) && parsed >= 50.f && parsed <= 10000.f) {
         PortMouse::SetStickAimRateY(parsed);
       }
     },
     [](std::ostream& out) { out << PortMouse::StickAimRateY(); }, false, "900", "1200"},
    BoolKey< &sCardBackup >("card_backup"),
    BoolKey< &sLogFile >("log_file"),
    BoolKey< &sHideHelmet >("no_helmet"),
    {"trilogy_radial_selectors",
     [](const std::string& value) { PortOverlay::SetTrilogyRadialSelectors(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, sTrilogyRadialSelectors); }, false, "1", "0"},
    BoolKey< &sHideVisorEffects >("no_visor_fx"),
    BoolKey< &sCinemaBars >("cinema_bars"),
    BoolKey< &sSecondaryCrosshairs >("secondary_crosshairs"),
    BoolKey< &sDarkVisorFull >("dark_visor_full"),
    IntKey< &sDarkVisorStatic, 0, PortOverlay::kDarkVisorStaticMax >("dark_visor_static", "50", "100"),
    BoolKey< &sInfiniteBeamAmmo >("infinite_beam_ammo"),
    IntKey< &sHudOpacityEcho, PortOverlay::kHudOpacityVisor, PortOverlay::kHudOpacityMax >(
        "hud_opacity_echo", "-1", "40"),
    IntKey< &sHudOpacityDark, PortOverlay::kHudOpacityVisor, PortOverlay::kHudOpacityMax >(
        "hud_opacity_dark", "-1", "40"),
    IntKey< &sHudOpacityScan, PortOverlay::kHudOpacityVisor, PortOverlay::kHudOpacityMax >(
        "hud_opacity_scan", "-1", "40"),
    // 0 keeps the game's own FOV.
    {"fov",
     [](const std::string& value) {
       float parsed = 0.f;
       if (ParseFloat(value, parsed) && (parsed == 0.f || (parsed >= 30.f && parsed <= 120.f))) {
         sFirstPersonFov = parsed;
       }
     },
     [](std::ostream& out) { out << sFirstPersonFov; }, false, "45", "72.5"},
    {"gun_offset",
     [](const std::string& value) {
       ParseFloat3(value, sGunOffset, -PortOverlay::kGunOffsetMax, PortOverlay::kGunOffsetMax);
     },
     [](std::ostream& out) {
       out << sGunOffset[0] << ',' << sGunOffset[1] << ',' << sGunOffset[2];
     },
     false, "0.5,-0.25,1", "0,0,0"},
    // MSAA is 1 or 4.
    {"msaa",
     [](const std::string& value) {
       int parsed = 0;
       if (ParseInt(value, parsed) && (parsed == 1 || parsed == 4))
         sMsaa = parsed;
     },
     [](std::ostream& out) { out << sMsaa; }, false, "4", "1"},
    // A max, not a step: any value in range is kept, and Aurora clamps what a sampler asks for.
    IntKey< &sAnisotropy, 1, 16 >("aniso", "8", "16"),
    // 0 means the full-size HUD.
    {"hud_scale",
     [](const std::string& value) {
       int parsed = 0;
       if (!ParseInt(value, parsed))
         return;
       if (parsed == 0)
         parsed = PortOverlay::kHudScaleMax;
       if (parsed >= PortOverlay::kHudScaleMin && parsed <= PortOverlay::kHudScaleMax)
         sHudScale = parsed;
     },
     [](std::ostream& out) { out << sHudScale; }, false, "50", "100"},
    IntKey< &sScanSpread, PortOverlay::kScanSpreadMin, PortOverlay::kScanSpreadMax >(
        "scan_spread", "0", "100"),
    IntKey< &sUiScale, PortOverlay::kUiScaleMin, PortOverlay::kUiScaleMax >("ui_scale", "50",
                                                                            "150"),
    BoolKey< &sVsync >("vsync"),
    BoolKey< &sFullscreen >("fullscreen"),
    BoolKey< &sFrameLimit >("frame_limit"),
    BoolKey< &sSmoothFrames >("smooth_frames"),
    BoolKey< &sPoseInterpolation >("pose_interpolation"),
    BoolKey< &sParticleInterpolation >("particle_interpolation"),
    BoolKey< &sLookInterpolation >("look_interpolation"),
    BoolKey< &sHudWide >("hud_wide"),
    {"hud_spread_max",
     [](const std::string& value) { PortOverlay::ParseHudSpreadMax(value.c_str(), sHudSpreadMax); },
     [](std::ostream& out) { out << PortOverlay::HudSpreadMaxName(sHudSpreadMax); }, false, "16:9",
     "full"},
    // 0 is the window's own size; MP1 keeps any value up to 4.
    {"render_scale",
     [](const std::string& value) {
       float parsed = 0.f;
       if (ParseFloat(value, parsed) && parsed >= 0.f && parsed <= PortOverlay::kRenderScaleMax) {
         sRenderScale = parsed;
       }
     },
     [](std::ostream& out) { out << sRenderScale; }, false, "0.5", "2"},
    // The Prime 1 port's names; anything else is ignored.
    {"aspect",
     [](const std::string& value) { PortOverlay::ParseAspect(value.c_str(), sAspectMode); },
     [](std::ostream& out) { out << PortOverlay::AspectName(sAspectMode); }, false, "16:9",
     "4:3"},
    BoolKey< &sOverlayWindowed >("overlay_windowed"),
    {"ui_theme",
     [](const std::string& value) {
       sUiTheme = value == "prime" ? 1 : value == "plain" ? 2 : value == "echoes" ? 3 : 0;
     },
     [](std::ostream& out) {
       out << (sUiTheme == 1   ? "prime"
               : sUiTheme == 2 ? "plain"
               : sUiTheme == 3 ? "echoes"
                               : "auto");
     },
     false, "plain", "auto"},
    BoolKey< &sAimCrosshair >("aim_crosshair"),
    IntKey< &sCrosshairStyle, PortOverlay::kCrosshairStyle_Classic,
            PortOverlay::kCrosshairStyle_GameCube >("crosshair_style", "0", "2"),
    IntKey< &sCrosshairSize, PortOverlay::kCrosshairSizeMin, PortOverlay::kCrosshairSizeMax >(
        "crosshair_size", "25", "100"),
    IntKey< &sCrosshairOpacity, PortOverlay::kCrosshairOpacityMin,
            PortOverlay::kCrosshairOpacityMax >("crosshair_opacity", "0", "100"),
    {"crosshair_color",
     [](const std::string& value) { ParseFloat3(value, sCrosshairColor, 0.f, 1.f); },
     [](std::ostream& out) {
       out << sCrosshairColor[0] << ',' << sCrosshairColor[1] << ',' << sCrosshairColor[2];
     },
     false, "1,0.5,0", "0,0.25,1"},
    BoolKey< &sCrosshairCustomColor >("crosshair_custom_color"),
    // LiveSplit and the photo camera keep their own state; their F1 rows mark the file dirty.
    {"livesplit",
     [](const std::string& value) { PortLiveSplit::SetEnabled(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, PortLiveSplit::Enabled()); }, false, "1", "0"},
    {"livesplit_address",
     [](const std::string& value) {
       if (!value.empty()) {
         PortLiveSplit::SetAddress(value);
       }
     },
     [](std::ostream& out) { out << PortLiveSplit::Address(); }, false, "127.0.0.1:16834", "localhost:1"},
    {"livesplit_split_upgrades",
     [](const std::string& value) { PortLiveSplit::SetSplitUpgrades(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, PortLiveSplit::SplitUpgrades()); }, false, "1", "0"},
    // Discord Rich Presence (port_discord.h): the background thread is started by Configure, here
    // and in LoadSettings, so the saved value is live from the first frame.
    BoolKey< &sDiscord >("discord"),
    // Update check (port_update_check.h): applied by ApplyUpdateCheck, here and in LoadSettings.
    BoolKey< &sUpdateCheck >("update_check"),
    BoolKey< &sUiSounds >("ui_sounds"),
    // Mods folder (port_mods.h): read at startup only.
    {"mods", [](const std::string& value) { PortMods::SetEnabled(ParseBool(value)); },
     [](std::ostream& out) { WriteBool(out, PortMods::Enabled()); }, false, "1", "0"},
    {"mods_disabled", [](const std::string& value) { PortMods::SetDisabled(value); },
     [](std::ostream& out) { out << PortMods::Disabled(); }, true, "a/b", "c"},
    // Suit colours (port_suit_colours.h): served at startup and on a mod reload (Apply suit colours).
    {"suit_colour_varia", &ReadSuitColours< 0 >, &WriteSuitColours< 0 >, true, "FF4000,-", "-,2050FF"},
    {"suit_colour_dark", &ReadSuitColours< 1 >, &WriteSuitColours< 1 >, true, "FF4000", "2050FF"},
    {"suit_colour_light", &ReadSuitColours< 2 >, &WriteSuitColours< 2 >, true, "FF4000,-", "-,2050FF"},
    {"photo_supersample", [](const std::string& value) {
       int parsed = 0;
       if (ParseInt(value, parsed)) {
         PortPhoto::SetSupersample(parsed);
       }
     },
     [](std::ostream& out) { out << PortPhoto::Supersample(); }, false, "4", "1"},
    {"photo_speed",
     [](const std::string& value) {
       float parsed = 0.f;
       if (ParseFloat(value, parsed)) {
         PortPhoto::SetSpeed(parsed);
       }
     },
     [](std::ostream& out) { out << PortPhoto::Speed(); }, false, "4", "1.5"},
    // Video > Quality, Frame rate and Compatibility rows from MP1 (f1b).
    BoolKey< &sShowShaderCompilation >("show_shader_compilation"),
    BoolKey< &sShowMenuHint >("show_menu_hint"),
    BoolKey< &sShowCheats >("show_cheats"),
    BoolKey< &sPerPixelLighting >("per_pixel_lighting"),
    BoolKey< &sDynamicRes >("dynamic_res"),
    {"dynamic_res_target",
     [](const std::string& value) { sDynamicResTarget = std::clamp(std::atoi(value.c_str()), 0, PortOverlay::kFpsCapMax); },
     [](std::ostream& out) { out << sDynamicResTarget; }, false, "0", "60"},
    {"dynamic_res_min",
     [](const std::string& value) {
       const float f = static_cast< float >(std::atof(value.c_str()));
       if (std::isfinite(f)) {
         sDynamicResMin = std::clamp(f, PortOverlay::kDynamicResMinLowest, 4.f);
       }
     },
     [](std::ostream& out) { out << sDynamicResMin; }, false, "0.5", "0.75"},
    {"upscale_sharpen",
     [](const std::string& value) { sSharpenMode = value == "off" ? 0 : value == "always" ? 2 : 1; },
     [](std::ostream& out) { out << (sSharpenMode == 0 ? "off" : sSharpenMode == 2 ? "always" : "dynamic"); },
     false, "off", "always"},
    {"upscale_sharpen_amount",
     [](const std::string& value) { sSharpenAmount = std::clamp(std::atoi(value.c_str()), 0, 100); },
     [](std::ostream& out) { out << sSharpenAmount; }, false, "0", "80"},
    {"fps_cap", [](const std::string& value) { sFpsCap = PortOverlay::ClampFpsCap(std::atoi(value.c_str())); },
     [](std::ostream& out) { out << sFpsCap; }, false, "0", "144"},
    {"opengles",
     [](const std::string& value) {
       sOpenGles = ParseBool(value);
       sOpenGlesAtStart = sOpenGles;
     },
     [](std::ostream& out) { WriteBool(out, sOpenGles); }, false, "1", "0"},
    {"graphics_backend",
     [](const std::string& value) {
       // An unknown value keeps what is already there (the default, Auto, at startup), so a
       // hand-edited or older file never breaks the start.
       PortGraphicsBackend::EGraphicsBackend parsed = sGraphicsBackend;
       if (PortGraphicsBackend::ParseGraphicsBackend(value, parsed)) {
         sGraphicsBackend = parsed;
         sGraphicsBackendAtStart = parsed;
       }
     },
     [](std::ostream& out) { out << PortGraphicsBackend::GraphicsBackendConfigName(sGraphicsBackend); }, false,
     "d3d12", "vulkan"},
    {"storage_clamp",
     [](const std::string& value) {
       const int v = std::atoi(value.c_str());
       sStorageClamp = v < 0 ? -1 : (v > 0 ? 1 : 0);
       sStorageClampAtStart = sStorageClamp;
     },
     [](std::ostream& out) { out << sStorageClamp; }, false, "-1", "1"},
    {"gpu_driver",
     [](const std::string& value) {
       sGpuDriver = value;
       sGpuDriverAtStart = value;
     },
     [](std::ostream& out) { out << sGpuDriver; }, false, "turnip", ""},
    {"low_power_gpu",
     [](const std::string& value) {
       sLowPowerGpu = ParseBool(value);
       sLowPowerGpuAtStart = sLowPowerGpu;
     },
     [](std::ostream& out) { WriteBool(out, sLowPowerGpu); }, false, "1", "0"},
    {"driver_workarounds",
     [](const std::string& value) {
       sDriverWorkarounds = PortVideo::ParseDriverWorkarounds(value);
       sDriverWorkaroundsAtStart = sDriverWorkarounds;
     },
     [](std::ostream& out) {
       const char* sep = "";
       for (size_t i = 0; i < PortVideo::kDriverWorkaroundCount; ++i) {
         if (sDriverWorkarounds & (1u << i)) {
           out << sep << PortVideo::kDriverWorkarounds[i].key;
           sep = ",";
         }
       }
     },
     true, "keep_depth", "unmonitored_fence,validation"},
    {"disc_path", [](const std::string& value) { sDiscPath = value; },
     [](std::ostream& out) { out << sDiscPath; }, true, "/a/disc.iso", "/b/disc.iso"},
    // Last, as in MP1: the game's options blob (PortOverlay::SyncGameOptions).
    {"game_options", [](const std::string& value) { sGameOptions = value; },
     [](std::ostream& out) { out << sGameOptions; }, true, "0a1b", "ff"},
};

// Recursive: a key's read can call back into PortMouse, whose ReadEnv asks for this file (the
// file is its base value), so applying the file can re-enter LoadSettings on the same thread.
std::recursive_mutex sMutex;
std::atomic< bool > sSettingsDirty{false};
bool sLoaded = false;
// Set once Reset configuration has moved the file away: nothing may write it back.
bool sFrozen = false;
// The text last read from or written to the file: SaveSettings writes only when SettingsText()
// differs.
std::string sBaseline;

} // namespace

namespace PortOverlaySettings {

bool ApplySetting(const std::string& key, const std::string& value) {
  for (const SettingKey& entry : kSettingKeys) {
    if (entry.key != nullptr && key == entry.key) {
      entry.read(value);
      return true;
    }
  }
  return false;
}

// Applies the key=value lines of a settings file. Only whole-line comments:
// values are written raw.
void ApplySettingsText(std::istream& text) {
  std::string line;
  while (std::getline(text, line)) {
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line[first] == '#') {
      continue;
    }
    const size_t separator = line.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    const std::string key = Trim(line.substr(0, separator));
    const std::string value = Trim(line.substr(separator + 1));
    if (!key.empty()) {
      ApplySetting(key, value);
    }
  }
}

// key=value lines of a settings text, comments skipped.
std::map< std::string, std::string > SettingsValues(const std::string& text) {
  std::map< std::string, std::string > values;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    const size_t separator = line.find('=');
    if (!line.empty() && line[0] != '#' && separator != std::string::npos) {
      values[line.substr(0, separator)] = line.substr(separator + 1);
    }
  }
  return values;
}

// The settings file's text, as written by SaveSettings.
std::string SettingsText() {
  std::ostringstream file;
  file << "# Metroid Prime 2 native port settings: <user>/settings.cfg. Written by the F1 "
          "overlay.\n";
  file << "# Environment variables (MP2_<KEY>) override these for a single run.\n";
  for (const SettingKey& entry : kSettingKeys) {
    if (entry.write == nullptr) {
      continue;
    }
    std::ostringstream value;
    entry.write(value);
    if (entry.omitEmpty && value.str().empty()) {
      continue;
    }
    file << entry.key << '=' << value.str() << '\n';
  }
  return file.str();
}

void LoadSettings() {
  std::lock_guard< std::recursive_mutex > lock(sMutex);
  // Too early (PortSettings::Initialized): main.cpp loads it once the path is known.
  if (sLoaded || !PortSettings::Initialized()) {
    return;
  }
  sLoaded = true;
  std::string text;
  if (PortSettings::ReadFile(text)) {
    std::fprintf(stderr, "port: loaded settings from %s\n", PortSettings::FilePath().c_str());
    std::istringstream lines(text);
    ApplySettingsText(lines);
  }
  sBaseline = SettingsText();
  // Discord Rich Presence starts with the settings, not with the first F1 visit.
  PortDiscord::Configure(sDiscord);
  PortDebug::ApplyUpdateCheck();
  PortUpdateCheck::CheckNow(); // once per launch
  // The settings in effect go to the log from LogSettings, once the log file has started.
}

// MP1's LogSettings: bug reports come with the log but rarely the settings file, so every setting
// in effect is logged at startup, wrapped into lines of a readable length (keys in alphabetical
// order, a value holding a path cut to its file name), then the MP2_* variables in effect.
void LogSettings() {
  std::string text;
  {
    std::lock_guard< std::recursive_mutex > lock(sMutex);
    text = SettingsText();
  }
  std::string line;
  const auto flush = [&line] {
    if (!line.empty()) {
      std::fprintf(stderr, "port: settings:%s\n", line.c_str());
      line.clear();
    }
  };
  for (const auto& [key, value] : SettingsValues(text)) {
    const size_t slash = value.find_last_of("/\\");
    const std::string entry =
        " " + key + "=" + (slash == std::string::npos ? value : value.substr(slash + 1));
    if (line.size() + entry.size() > 160) {
      flush();
    }
    line += entry;
  }
  flush();
  // MP2_* variables override the file for one run (already in the values above).
#if defined(_WIN32)
  char** env = _environ;
#else
  char** env = environ;
#endif
  for (; env != nullptr && *env != nullptr; ++env) {
    if (std::strncmp(*env, "MP2_", 4) == 0) {
      std::fprintf(stderr, "port: env: %s\n", *env);
    }
  }
}

// One log line per key whose value differs between the old and new settings text (MP1's
// LogSettingChanges); the old text is the baseline, so only real changes are named.
void LogSettingChanges(const std::string& before, const std::string& after) {
  const std::map< std::string, std::string > old = SettingsValues(before);
  for (const auto& [key, value] : SettingsValues(after)) {
    const auto it = old.find(key);
    if (it == old.end() || it->second != value) {
      std::fprintf(stderr, "port: setting %s: %s -> %s\n", key.c_str(),
                   it == old.end() ? "(unset)" : it->second.c_str(), value.c_str());
    }
  }
}

void FreezeAfterReset() {
  std::lock_guard< std::recursive_mutex > lock(sMutex);
  sFrozen = true;
}

void SaveSettings() {
  std::lock_guard< std::recursive_mutex > lock(sMutex);
  if (!sLoaded || sFrozen || !sSettingsDirty.load() || PortSettings::FilePath().empty()) {
    return;
  }
  sSettingsDirty = false;
  const std::string text = SettingsText();
  if (text == sBaseline) {
    return;
  }
  if (!PortSettings::WriteFile(text)) {
    sSettingsDirty = true;
    return;
  }
  LogSettingChanges(sBaseline, text);
  std::fprintf(stderr, "port: saved settings to %s\n", PortSettings::FilePath().c_str());
  sBaseline = text;
}

void MarkDirty() { sSettingsDirty = true; }

std::string DiscPath() {
  std::lock_guard< std::recursive_mutex > lock(sMutex);
  return sDiscPath;
}

void SetDiscPath(const std::string& path) {
  std::lock_guard< std::recursive_mutex > lock(sMutex);
  sDiscPath = path;
  sSettingsDirty = true;
}

bool Dirty() { return sSettingsDirty.load(); }

std::string EnvName(const std::string& key) {
  if (key == "lockon_toggle") {
    return "MP2_LOCK_ON_TOGGLE";
  }
  std::string name = "MP2_";
  for (char c : key)
    name += static_cast< char >(std::toupper(static_cast< unsigned char >(c)));
  return name;
}

int SettingsSelfTest(std::string& report) {
  int failures = 0;
  int sampled = 0;
  char line[512];
  // Each sampled key, through the file reader: both values must come back as written.
  for (const SettingKey& entry : kSettingKeys) {
    if (entry.key == nullptr || entry.write == nullptr || entry.sampleA == nullptr) {
      continue;
    }
    ++sampled;
    for (const char* sample : {entry.sampleA, entry.sampleB}) {
      std::istringstream in(std::string(entry.key) + "=" + sample + "\n");
      ApplySettingsText(in);
      const auto written = SettingsValues(SettingsText());
      const auto found = written.find(entry.key);
      const std::string got = found == written.end() ? std::string() : found->second;
      if (got != sample) {
        std::snprintf(line, sizeof(line), "settings self-test: %s=%s reads back as %s=%s\n",
                      entry.key, sample, entry.key, got.c_str());
        report += line;
        ++failures;
      }
    }
  }
  // Every line written is a key something reads, and reading the whole file
  // back writes it unchanged.
  const std::string first = SettingsText();
  for (const auto& [key, value] : SettingsValues(first)) {
    if (!ApplySetting(key, value)) {
      report += "settings self-test: " + key + " is written but nothing reads it\n";
      ++failures;
    }
  }
  std::istringstream again(first);
  ApplySettingsText(again);
  const std::string second = SettingsText();
  if (second != first) {
    report +=
        "settings self-test: the file changes when read back\n" + first + "---\n" + second + "\n";
    ++failures;
  }
  std::snprintf(line, sizeof(line),
                "settings self-test: %d keys sampled, %zu lines round-tripped, %d failure(s)\n",
                sampled, SettingsValues(first).size(), failures);
  report += line;
  return failures == 0 ? 0 : 1;
}

} // namespace PortOverlaySettings

namespace {

std::string GameOptionsHex(CGameOptions& options) {
  uchar bytes[kGameOptionsBytes * 2] = {};
  {
    CMemoryStreamOut stream(bytes, sizeof(bytes));
    CBitStreamWriter writer(stream);
    options.PutTo(writer);
    writer.FlushAll();
  }
  static const char kDigits[] = "0123456789abcdef";
  std::string hex;
  for (size_t i = 0; i < kGameOptionsBytes; ++i) {
    hex += kDigits[bytes[i] >> 4];
    hex += kDigits[bytes[i] & 0xF];
  }
  return hex;
}

// The recorded options as CGameOptions::PutTo wrote them; false unless they are whole and start
// with PutTo's 'OPTN' tag.
bool GameOptionsBytes(uchar (&bytes)[kGameOptionsBytes]) {
  if (sGameOptions.size() != kGameOptionsBytes * 2) {
    return false;
  }
  for (size_t i = 0; i < kGameOptionsBytes; ++i) {
    char* end = nullptr;
    const std::string digits = sGameOptions.substr(i * 2, 2);
    // strtoul alone would take a sign, a space or "0x" inside the pair.
    if (!std::isxdigit(static_cast< unsigned char >(digits[0])) ||
        !std::isxdigit(static_cast< unsigned char >(digits[1]))) {
      return false;
    }
    const unsigned long value = std::strtoul(digits.c_str(), &end, 16);
    if (end != digits.c_str() + 2) {
      return false;
    }
    bytes[i] = static_cast< uchar >(value);
  }
  return bytes[0] == 'O' && bytes[1] == 'P' && bytes[2] == 'T' && bytes[3] == 'N';
}

} // namespace

namespace PortOverlay {

bool ApplySavedGameOptions(CGameOptions& options) {
  PortOverlaySettings::LoadSettings();
  std::lock_guard< std::recursive_mutex > lock(sMutex);
  uchar bytes[kGameOptionsBytes];
  if (!GameOptionsBytes(bytes)) {
    return false;
  }
  CMemoryInStream stream(bytes, sizeof(bytes));
  CBitStreamReader reader(stream);
  options = CGameOptions(reader);
  return true;
}

void RestoreGameOptions() {
  sGameOptionsRestored = true;
  if (gpGameState != nullptr && ApplySavedGameOptions(gpGameState->GameOptions())) {
    gpGameState->GameOptions().EnsureOptions();
  }
}

// Records the game's options whenever they change, wherever from (pause menu, F1, a loaded file);
// Draw writes the file once no F1 item is active.
void SyncGameOptions() {
  if (!sGameOptionsRestored || gpGameState == nullptr) {
    return;
  }
  std::string hex = GameOptionsHex(gpGameState->GameOptions());
  std::lock_guard< std::recursive_mutex > lock(sMutex);
  if (hex != sGameOptions) {
    sGameOptions = std::move(hex);
    PortOverlaySettings::MarkDirty();
  }
}

} // namespace PortOverlay
