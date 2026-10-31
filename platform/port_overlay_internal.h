// The F1 overlay's live settings and the settings file's functions, shared by port_overlay.cpp
// (which owns the variables), debug_ui/settings.cpp (the key table, MP1's debug_ui/settings.cpp
// ported) and the pages in debug_ui/. The variables hold the file's and the UI's values; an
// `MP2_<KEY>` environment variable is applied when a value is read (the accessors in
// port_overlay.cpp), never stored here.
#pragma once

#include <iosfwd>
#include <map>
#include <string>

#include "port_graphics_backend.h"

namespace PortOverlayState {

extern bool sSkipCinematics;
extern int sBootLogos;
extern int sWorldNameCard;
extern bool sLogFile;
extern bool sFastMorph;
extern bool sPointerMenus;
extern bool sMapSwapFingers;
extern bool sMapInvertX;
extern bool sMapInvertY;
extern int sWalkSpeed;
extern int sScanSpeed;
extern int sMusicBoost;
extern int sSfxBoost;
extern int sLanguage;
extern int sElevatorRide;
extern int sPortalTransition;
extern int sRotatingRooms;
extern int sComboBeamCost;
extern int sComboMissileCost;
extern int sBeamAmmoPerExpansion;
extern bool sMinimapTap;
extern int sPromptGlyphs;
extern bool sIgtTimer;
extern bool sOriginalExperience;
extern bool sUnlockHardMode;
extern bool sUnlockGalleries;
extern bool sUnlockBeamDoors;
extern bool sStackBeams;
extern bool sStackBeamsSkipDark;
extern int sHintDelay;
extern bool sSpringBall;
extern bool sSpringFlick;
extern float sSpringFlickRate;
extern bool sQuickResume;
extern bool sSaveStateHotkeys;
extern bool sSaveStateRoomState;
extern bool sLockOnToggle;
extern bool sStickyCharge;
extern bool sRapidCharge;
extern bool sAutoAim;
extern bool sPointerAim;
extern bool sTrilogyRadialSelectors;
extern int sPointerAimBox;
extern int sPointerAimTurn;
extern bool sGyroAim;
extern int sGyroSource;
extern int sGyroSpace;
extern int sGyroAutoCalibrate;
extern float sGyroSmoothing;
extern float sGyroTightening;
extern float sGyroCursorAngle;
extern int sGyroMode;
extern float sGyroSensitivity;
extern bool sGyroCursorCamera;
extern bool sGyroSplit;
extern float sGyroSensitivityY;
extern bool sResetAimBoth;
extern bool sLockOnFreeAim;
extern bool sMenuStandardButtons;
extern bool sCardBackup;
extern float sFirstPersonFov;
extern bool sHideHelmet;
extern bool sHideVisorEffects;
extern bool sCinemaBars;
extern bool sSecondaryCrosshairs;
extern bool sDarkVisorFull;
extern int sDarkVisorStatic;
extern int sHudOpacityEcho;
extern int sHudOpacityDark;
extern int sHudOpacityScan;
extern bool sAimCrosshair;
extern int sCrosshairStyle;
extern int sCrosshairSize;
extern int sCrosshairOpacity;
extern bool sCrosshairCustomColor;
extern float sCrosshairColor[3];
extern float sGunOffset[3];
extern int sMsaa;
extern int sAnisotropy;
extern int sHudScale;
extern int sUiScale;
extern bool sVsync;
extern bool sFullscreen;
extern bool sFrameLimit;
extern bool sSmoothFrames;
extern bool sPoseInterpolation;
extern bool sParticleInterpolation;
extern bool sLookInterpolation;
extern bool sHudWide;
extern int sHudSpreadMax;
extern int sScanSpread;
extern float sRenderScale;
extern int sAspectMode;
extern bool sOverlayWindowed;
extern int sUiTheme;
extern bool sUiSounds;
extern bool sDiscord;
extern bool sUpdateCheck;

// Hands both boosts (percent / 100, MP2_MUSIC_BOOST and MP2_SFX_BOOST included, so the effective
// levels) to the mixers: the MusyX voices in hw_pc.c and the AI DMA stream's own gain. Called as a
// value is read from the settings file, by the setters, and once by ReadEnv for the environment.
void ApplyVolumeBoosts();

} // namespace PortOverlayState

// F1 > Video rows from the Prime 1 port (f1b): dynamic resolution, upscale sharpening, the FPS cap,
// the startup toasts, per-pixel lighting and the Compatibility tab. Defined in port_overlay.cpp,
// applied by platform/port_video.cpp. The *AtStart copies are what this run's GPU device got.
namespace PortOverlayState {

extern bool sDynamicRes;
extern int sDynamicResTarget;   // fps; 0 = the display's refresh rate (60 under the cap)
extern float sDynamicResMin;    // kDynamicResMinLowest to 4
extern int sSharpenMode;        // 0 off, 1 while dynamic resolution has lowered the scale, 2 always
extern int sSharpenAmount;      // 0..100
extern int sFpsCap;             // with the 60 FPS cap off; 0 = unlimited
extern bool sShowShaderCompilation;
extern bool sShowMenuHint;
extern bool sShowCheats;          // Debug > Show cheats: only reveals the cheat rows (MP1 saves it too)
extern bool sPerPixelLighting;
extern PortGraphicsBackend::EGraphicsBackend sGraphicsBackend; // renderer choice (Windows, desktop Linux)
extern PortGraphicsBackend::EGraphicsBackend sGraphicsBackendAtStart;
extern bool sOpenGles;
extern bool sOpenGlesAtStart;
extern int sStorageClamp;       // -1 auto, 0 off, 1 on
extern int sStorageClampAtStart;
extern std::string sGpuDriver;        // Android custom Vulkan driver id (port_gpu_driver.h); "" = the system's
extern std::string sGpuDriverAtStart;
extern unsigned sDriverWorkarounds; // bit i = PortVideo::kDriverWorkarounds[i]
extern unsigned sDriverWorkaroundsAtStart;
extern bool sLowPowerGpu;
extern bool sLowPowerGpuAtStart;

} // namespace PortOverlayState

namespace PortOverlaySettings {

// False for a key nothing reads.
bool ApplySetting(const std::string& key, const std::string& value);
// Applies the key=value lines of a settings file.
void ApplySettingsText(std::istream& text);
// key=value lines of a settings text, comments skipped.
std::map< std::string, std::string > SettingsValues(const std::string& text);
// The settings file's text, as SaveSettings writes it.
std::string SettingsText();
// Reads <user>/settings.cfg once per run (a later call does nothing; one before
// PortSettings::Initialize is ignored, and main.cpp calls it right after). Never writes.
void LoadSettings();
// Logs every setting in effect ("port: settings: k=v ...") and the MP2_* variables; main.cpp calls
// it once the log file has started.
void LogSettings();
// Writes the file when a setter or row marked it dirty and the text now differs from what was last
// read or saved.
void SaveSettings();
void MarkDirty();
bool Dirty();
// The disc image F1 > System > Disc image > Replace remembered for the next start; "" when none.
// SetDiscPath marks the settings dirty; the caller saves them.
std::string DiscPath();
void SetDiscPath(const std::string& path);
// After Reset configuration moved the files away: SaveSettings writes nothing more this run.
void FreezeAfterReset();
// The environment variable that overrides `key`.
std::string EnvName(const std::string& key);
// Writes every key and reads it back; the report lines go to `report`. Returns 0 when all pass. It
// leaves the live variables as the table's last samples: the caller restores them.
int SettingsSelfTest(std::string& report);

} // namespace PortOverlaySettings
