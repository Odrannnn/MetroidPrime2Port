// Port entry point for Metroid Prime 2: Echoes.
//
// Aurora owns the real process entry, so this is what it calls. The order below
// is the one the Metroid Prime port proves: bring Aurora up, mount the disc,
// verify it is the disc this port is built for, then hand control to the game
// and shut the platform down afterwards.
//
// The game's entry is `InvokeCMain` (src/MetroidPrime/main.cpp:79), which builds
// `CMain` and calls `RsMain`. It takes the OS context, the memory system and the
// graphics system from its caller, and that caller is not decompiled - so the three
// objects are built here in the shape the caller will need. `COsContext`'s and
// `CMemorySys`'s constructors are host work (`src/Kyoto/Basics/COsContext.cpp` and
// the SDK), and `CGraphicsSys`'s is `src/Kyoto/Graphics/CGraphicsHostStartup.cpp`,
// which carries retail's `CGraphics::Startup` chain - so the VI bring-up this
// translation unit used to leave to a `CMain::OpenWindow` stand-in inside
// `RsMain` now happens here, where retail does it. Everything else here (Aurora
// setup, disc mounting and checking, teardown) is settled. See PORT_NOTES.md.

// Aurora owns the real process entry: this header's `#define main aurora_main` is
// what turns the `main` below into the function Aurora's own `main` (and SDL's
// Android activity, by that name) calls. Android needs it for its shared library
// to have the entry point at all; ../MetroidPrimePort includes it the same way.
#include "port_utf8_path.h"
#include <aurora/main.h>

#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <dolphin/dvd.h>
#include <dolphin/gx/GXAurora.h>

#include <algorithm>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <vector>
#include <csignal>
#if defined(__ANDROID__)
#include <signal.h>
#include <sys/ioctl.h>
#include <time.h>
#include <android/log.h>
#endif
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#if !defined(_WIN32)
#include <unistd.h>
#else
#include <stdlib.h>
extern "C" __declspec(dllimport) unsigned int __stdcall SetErrorMode(unsigned int mode);
#endif
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
// SDL owns the app's private directories on Android, where the port's own three are resolved
// beneath them, and shows the disc message and picker everywhere. The include directory is
// attached to this target in CMakeLists.txt, in the AURORA_SDL3_TARGET block.
#include <SDL3/SDL.h>
#include <atomic>
#if defined(SDL_PLATFORM_IOS)
#include "port_ios.h"
#endif
#if defined(__ANDROID__)
#include <cerrno>
#include <fstream>
#include <thread>
#include <sys/ioctl.h>
#endif

#include "Kyoto/Alloc/CMemorySys.hpp"

extern "C" void* OSGetArenaLo();
extern "C" void* OSGetArenaHi();
extern void* gPortGameHeapLo;
extern void* gPortGameHeapHi;
#include "Kyoto/Basics/COsContext.hpp"
#include "Kyoto/Graphics/CGraphicsSys.hpp"
#include "compiled_modules.h"
#include "input/port_input_devices.h"
#include "port_settings.h"
#include "port_build_info.h"
#include "port_console.h"
#include "port_debug.h"
#include "port_embedded.h"
#include "port_disc_verify.h"
#include "port_entry.h"
#ifdef MP_PORT_COVERAGE
// The gcov flush hook of a coverage build (CMake option MP_PORT_COVERAGE). The normal build does
// not define the macro, so it does not see this file at all.
#include "port_coverage.h"
#endif
#include "port_env.h"
#include "port_breadcrumb.h"
#include "port_crash.h"
#include "port_storage_report.h"
#include "port_logfile.h"
#include "port_memory_log.h"
#include "port_save.h"
#include "port_overlay.h"
#include "port_disc_replace.h"
#include "port_overlay_internal.h"
#include "port_prompts.h"
#include "port_mods.h"
#include "port_textures.h"
#include "port_touch.h"
#include "port_tweaks.h"
#include "port_video.h"

// The decompilation's exported entry point; see src/MetroidPrime/main.cpp.
namespace port {
void* LoadDefaultFont();
void FreeDefaultFont(void* token);
} // namespace port

extern "C" void InvokeCMain(int argc, char** argv, COsContext* context, void* unk1,
                            CMemorySys* memorySys, void* unk2);

namespace {

const char* kAppName = "Metroid Prime 2: Echoes";
// The folder under the user's data directory. Windows forbids ':' in a name (it reads as an NTFS
// stream), so SDL_GetPrefPath(nullptr, kAppName) fails there and Aurora's null cachePath crashed.
#if defined(_WIN32)
const char* kAppFolder = "Metroid Prime 2 Echoes";
#else
const char* kAppFolder = kAppName;
#endif

std::string ExecutableDir(const char* argv0) {
  if (argv0 == nullptr || argv0[0] == '\0') {
    return {};
  }
  std::error_code error;
  const std::filesystem::path path = std::filesystem::absolute(argv0, error);
  if (error || !path.has_parent_path()) {
    return {};
  }
  return PortUtf8::ToUtf8(path.parent_path());
}

void ReportNoDisc(const char* argv0) {
  std::fprintf(stderr,
               "metroid_prime2_port: no disc image given.\n"
               "  usage: %s <path to a Metroid Prime 2: Echoes (USA or PAL) or Metroid Prime Trilogy disc image>\n"
               "  or set MP2_DISC, or place the image next to the executable.\n",
               argv0 != nullptr ? argv0 : "metroid_prime2_port");
}

// The three directories the port needs: where the game's user data goes, where its cache
// goes, and which disc image to mount. On the desktop all three come from the environment
// (MP2_USER_PATH / MP2_CACHE_PATH / MP2_DISC, or an image beside the executable); an
// installed app has no environment to set, so `ResolveAndroidPaths` fills them in instead
// and the environment only wins over that when it is actually set.
struct PortPaths {
  std::string user;
  std::string cache;
  std::string disc;
  std::string base;  // the app's private files directory, where the APK's textures are unpacked
};

// An environment variable if it is set and non-empty, otherwise the value resolved for this
// platform (empty on the desktop, where there is nothing to resolve).
const char* EnvOrFallback(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  if (value != nullptr && value[0] != '\0') {
    return value;
  }
  return fallback.empty() ? nullptr : fallback.c_str();
}

#if !defined(__ANDROID__)
// The disc image to try after the command line: MP2_DISC, else the one F1 > System > Disc image >
// Replace remembered (settings.cfg `disc_path`, while the file is still there), else none, which
// leaves the search of the executable's folder.
std::string DesktopDiscFallback(const std::string& resolved) {
  if (const char* env = EnvOrFallback("MP2_DISC", resolved)) {
    return env;
  }
  const std::string saved = PortOverlaySettings::DiscPath();
  std::error_code error;
  if (!saved.empty() && std::filesystem::is_regular_file(saved, error)) {
    std::fprintf(stderr, "metroid_prime2_port: using the remembered disc image: %s\n", saved.c_str());
    return saved;
  }
  if (!saved.empty()) {
    std::fprintf(stderr, "metroid_prime2_port: the remembered disc image is gone: %s\n", saved.c_str());
  }
  return {};
}

// SDL_GetPrefPath(nullptr, kAppName), worked out by hand: Aurora falls back to it for its own
// user path, so the port has to name the same folder or the two disagree about where
// settings.cfg, controls.toml and the tools' backups live. Not SDL's call itself because SDL is
// only on the entry's include path on Android, and because asking it creates the folder.
std::filesystem::path Utf8Path(const std::string& text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string PerUserFolder() {
  std::string dir;
#if defined(_WIN32)
  if (const char* appdata = std::getenv("APPDATA"); appdata != nullptr && appdata[0] != '\0') {
    dir = appdata;
  }
#elif defined(SDL_PLATFORM_IOS)
  // The Documents folder, which the Files app shows: settings, saves, logs and the disc image
  // are all there (docs/IOS.md). No app-name folder below it, unlike the other platforms.
  return PortIos::DocumentsFolder();
#elif defined(__APPLE__)
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    dir = std::string(home) + "/Library/Application Support";
  }
#else
  if (const char* data = std::getenv("XDG_DATA_HOME"); data != nullptr && data[0] == '/') {
    dir = data;
  } else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    dir = std::string(home) + "/.local/share";
  }
#endif
  if (dir.empty()) {
    return {};
  }
  if (dir.back() != '/' && dir.back() != '\\') {
    dir += '/';
  }
  // SDL's own name for the folder: the app name, verbatim, with no organisation above it.
  dir += kAppFolder;
  dir += '/';
  return dir;
}

// The folder the program (or, for an AppImage, the .AppImage file) sits in, ending in a
// separator; empty when unknown. Inside an AppImage the executable is in a read-only mount that
// is gone when the game exits, and the file the user copies around is $APPIMAGE.
bool InAppImage() {
  const char* image = std::getenv("APPIMAGE");
  return image != nullptr && image[0] != '\0';
}

// The folder the running executable is in, with a trailing separator; empty when unknown. SDL
// asks the OS (/proc/self/exe, GetModuleFileName, _NSGetExecutablePath), so it holds when the
// program was started by bare name from PATH, where argv[0] has no folder. Decides both the
// portable user/ folder (InstallFolder) and the textures folder.
std::string ExecutableFolder() {
  std::string dir;
  if (const char* base = SDL_GetBasePath(); base != nullptr) {
    dir = base;
  }
  if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
    dir += '/';
  }
  return dir;
}

std::string InstallFolder() {
  std::string dir;
  if (InAppImage()) {
    const std::u8string parent = Utf8Path(std::getenv("APPIMAGE")).parent_path().u8string();
    dir.assign(parent.begin(), parent.end());
  } else {
    dir = ExecutableFolder();
  }
  if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
    dir += '/';
  }
  return dir;
}

// A macOS app bundle is signed as a whole: anything written inside it breaks the signature, and a
// quarantined download runs from a read-only translocated copy anyway.
bool InAppBundle(const std::string& dir) {
  return dir.find(".app/Contents/") != std::string::npos;
}

bool FolderWritable(const std::string& dir) {
  const std::filesystem::path probe = Utf8Path(dir) / ".port_write_test";
  {
    std::ofstream file(probe, std::ios::binary);
    if (!file) {
      return false;
    }
  }
  std::error_code error;
  std::filesystem::remove(probe, error);
  return true;
}

// Where the desktop port keeps settings.cfg, controls.toml, the memory cards, the log and the
// caches (all of them follow AppPaths::user, which Aurora gets too). In order, as in the Prime 1
// port (platform/include/port_paths.h):
//   1. MP2_USER_PATH, when set (the caller resolves that; not here).
//   2. user/ beside the executable (an AppImage: the folder the .AppImage file is in, no user/),
//      when that folder can be written to and is not inside a macOS app bundle. Exception: an
//      install from before this, whose settings.cfg is still in the per-user folder and which has
//      none in user/, stays on the per-user folder so an update does not lose its settings and
//      saves. Moving that folder's contents into user/ switches it over.
//   3. The per-user folder, for a read-only install (a Flatpak, a system package).
// `why` is set to "portable", "legacy" or "per-user" for the startup log line.
std::string DesktopUserFolder(const char** why) {
  const std::string perUser = PerUserFolder();
  const std::string install = InstallFolder();
  std::string dir;
  if (!install.empty() && !InAppBundle(install) && FolderWritable(install)) {
    const std::string portable = InAppImage() ? install : install + "user/";
    std::error_code error;
    const bool hereHasSettings =
        std::filesystem::exists(Utf8Path(portable + "settings.cfg"), error);
    const bool legacyHasSettings =
        !perUser.empty() && std::filesystem::exists(Utf8Path(perUser + "settings.cfg"), error);
    if (hereHasSettings || !legacyHasSettings) {
      dir = portable;
      *why = "portable";
    } else {
      dir = perUser;
      *why = "legacy: settings.cfg is in the per-user folder and none in user/";
    }
  } else {
    dir = perUser;
    *why = "per-user: the executable's folder is read-only or inside an app bundle";
  }
  if (!dir.empty()) {
    std::error_code error;
    std::filesystem::create_directories(Utf8Path(dir), error);
  }
  return dir;
}
#endif

#if defined(__ANDROID__)

// ---------------------------------------------------------------------------
// Android: fd 1/2 to logcat, and the port's directories from SDL.
// ---------------------------------------------------------------------------
// Android gives an app no console: fd 1 and fd 2 go nowhere, so every std::fprintf the
// port makes - and the "no disc" message that says where a run stopped - is discarded, and
// a boot that stops at the disc check reads exactly like one that never started. Both
// descriptors are pointed at a pipe here and a thread turns the bytes back into logcat
// lines. This is the small version of ../MetroidPrimePort's PortLog: lines, one tag,
// nothing else (no file, no buffering, no per-frame state). It runs before anything else in
// the entry, so Aurora's own lines arrive too.
constexpr size_t kMaxPendingLine = 8192;

int gLogPipeRead = -1;
std::atomic<bool> gLogPumpIdle{true};

void PortLogLine(const std::string& line) {
  // A whole line at a time, `%.*s` rather than a format string the writer chose.
  __android_log_print(ANDROID_LOG_INFO, "mp2port", "%.*s", static_cast<int>(line.size()),
                      line.c_str());
  // ... and the same line into the log file, since this pump is what sees stdout and stderr.
  PortLogFile::Write(line);
}

void PortLogPump() {
  std::string pending;
  char buffer[4096];
  for (;;) {
    const ssize_t count = ::read(gLogPipeRead, buffer, sizeof(buffer));
    if (count == 0) {
      break; // the write end is gone: the process is going down
    }
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    gLogPumpIdle.store(false);
    pending.append(buffer, static_cast<size_t>(count));
    size_t start = 0;
    for (;;) {
      const size_t newline = pending.find('\n', start);
      if (newline == std::string::npos) {
        break;
      }
      PortLogLine(pending.substr(start, newline - start));
      start = newline + 1;
    }
    pending.erase(0, start);
    // One line is logged as it arrives: a writer that never sends a newline must not make
    // this grow without bound.
    if (pending.size() > kMaxPendingLine) {
      PortLogLine(pending);
      pending.clear();
    }
    gLogPumpIdle.store(true);
  }
  // The pipe is closed and this thread is going away with it: a last line without its
  // newline is still worth logging.
  if (!pending.empty()) {
    PortLogLine(pending);
  }
}

void PortLogBegin() {
  int descriptors[2];
  if (::pipe(descriptors) != 0) {
    return; // no log; a crash still leaves a tombstone
  }
  gLogPipeRead = descriptors[0];
  // What the C library has already buffered belongs in the log rather than in the pipe's
  // future, so both streams are flushed before the descriptors move.
  std::fflush(stdout);
  std::fflush(stderr);
  if (::dup2(descriptors[1], STDOUT_FILENO) < 0 || ::dup2(descriptors[1], STDERR_FILENO) < 0) {
    ::close(descriptors[0]);
    ::close(descriptors[1]);
    gLogPipeRead = -1;
    return;
  }
  ::close(descriptors[1]); // file descriptors 1 and 2 are the writers now
  // stderr unbuffered and stdout line-buffered even when neither is a terminal: a partial
  // line on an abort()ing run is the line worth having.
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  std::thread(PortLogPump).detach();
}

// Called on every way out of the entry. A pipe cannot be asked how much is still in it, so
// the queue is polled until it is empty and the pump is between reads; bounded, because a
// log line is not worth a hang.
void PortLogDrain() {
  if (gLogPipeRead < 0) {
    return;
  }
  for (int attempt = 0; attempt < 100; ++attempt) {
    int queued = 0;
    if (::ioctl(gLogPipeRead, FIONREAD, &queued) == 0 && queued == 0 && gLogPumpIdle.load()) {
      return;
    }
    ::usleep(2000);
  }
}

// The app's own directories, all three beneath SDL's preference directory - which on Android
// *is* the app's private files directory - as user/, cache/ and disc.iso. That is the shape
// ../MetroidPrimePort settled on (port_paths.h); the difference here is that nothing has
// been migrated yet, so there is only one layout. A `port_paths.txt` beside them overrides
// the three (`user=`, `cache=`, `disc=`), which a debugging push can use to move them, or to
// point at a disc copied into app storage; the Java side writes that file, and the file
// picker that fills in the disc is `EnsureDiscImage` below. A fourth line, `env.NAME=value`,
// sets an environment variable, which is the device equivalent of the desktop harness's
// `MP2_FRAMES=1800 MP2_SKIP_FRONTEND=1 ./metroid_prime2_port` prefix (tools/run_gameplay.sh
// exports MP2_DEBUG_IDS / MP2_SKIP_CINEMATICS / MP2_SKIP_FRONTEND / MP2_DISC; the harness and
// the port read those plus MP2_WORLD and MP2_INPUT from the environment) - an installed app
// has no shell to set them in, so the file is where a device run says them. The assignments
// are made while this function reads the file, which is the top of the entry: before Aurora
// starts and before the game or the harness looks at any of them. SDL is already up when this
// runs: the activity's context is what SDL_GetPrefPath reads, and Aurora itself falls back to
// the same call for its user and cache directories.
PortPaths ResolveAndroidPaths() {
  PortPaths paths;
  char* preference = SDL_GetPrefPath(nullptr, kAppName);
  if (preference == nullptr) {
    std::fprintf(stderr,
                 "metroid_prime2_port: no SDL preference directory; set MP2_USER_PATH, "
                 "MP2_CACHE_PATH and MP2_DISC\n");
    return paths;
  }
  const std::string base = preference;
  SDL_free(preference);

  paths.base = base;
  paths.user = base + "user";
  paths.cache = base + "cache";
  paths.disc = base + "disc.iso";

  std::ifstream overrides(PortUtf8::FromUtf8(base + "port_paths.txt"));
  std::string line;
  while (std::getline(overrides, line)) {
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
    const std::string value = entry.substr(equals + 1);
    if (value.empty()) {
      continue;
    }
    const std::string key = entry.substr(0, equals);
    if (key == "user") {
      paths.user = value;
    } else if (key == "cache") {
      paths.cache = value;
    } else if (key == "disc") {
      paths.disc = value;
    } else if (key.starts_with("env.")) {
      // The variables the desktop harness is given on its command line (see the comment
      // above). `NAME` is whatever follows `env.`, and this file is the app's only way to
      // name one, so it overwrites: a value here is meant. An invalid name is left to
      // setenv to reject, and is reported rather than dropped.
      const std::string name = key.substr(4);
      if (port::SetEnv(name.c_str(), value.c_str()) == 0) {
        std::fprintf(stderr, "metroid_prime2_port: port_paths.txt set %s=%s\n", name.c_str(),
                     value.c_str());
      } else {
        std::fprintf(stderr, "metroid_prime2_port: port_paths.txt cannot set %s\n", name.c_str());
      }
    }
  }

  // A `user=` the player moved the data to (F1 > System > Data folder, port_data_folder.h) in
  // shared storage can stop being writable (the "All files access" grant revoked, a reinstall):
  // run from app storage this once, and let the panel say so through MP2_USER_UNAVAILABLE.
  {
    std::error_code probe;
    std::filesystem::create_directories(PortUtf8::FromUtf8(paths.user), probe);
    const std::string test = paths.user + "/.port_write_test";
    std::ofstream writable(PortUtf8::FromUtf8(test));
    if (paths.user != base + "user" && (probe || !writable)) {
      std::fprintf(stderr, "metroid_prime2_port: %s is not writable; using app storage\n",
                   paths.user.c_str());
      port::SetEnv("MP2_USER_UNAVAILABLE", paths.user.c_str());
      paths.user = base + "user";
    } else {
      writable.close();
      std::filesystem::remove(test, probe);
    }
  }

  // Created before the game can ask for them, so a first run has somewhere to write.
  std::error_code error;
  std::filesystem::create_directories(paths.user, error);
  error.clear();
  std::filesystem::create_directories(paths.cache, error);

  std::printf("metroid_prime2_port: build %s\n", MP_BUILD_REVISION);
  std::printf("metroid_prime2_port: paths chosen: user=%s cache=%s disc=%s\n", paths.user.c_str(),
              paths.cache.c_str(), paths.disc.c_str());
  return paths;
}

// ---------------------------------------------------------------------------
// Android: the disc image, asked for once and copied into the app's own storage.
// ---------------------------------------------------------------------------
// An Android app has no command line and no image beside the executable, so the only disc it
// can ever have is a file in its own files directory - and a first launch has none. This is
// ../MetroidPrimePort's picker (its `AskForDiscImage` and `CopyDiscFromContentUri`), reduced
// to what this port needs.
//
// The trigger is the disc image the resolution named not being a Metroid Prime 2 image, which
// is where a missing file lands: `ResolveDiscPath` hands back the default `<files>/disc.iso`
// whether or not it exists, so the launch would otherwise reach `aurora_dvd_open` and die
// there with nothing on screen saying what to do about it.
//
// It runs *before* `aurora_initialize`, and that is deliberate: the system picker covers the
// app and destroys its surface, which is why MP1 drops its swapchain by hand while a dialog is
// up (`aurora_release_lost_surface`, which this snapshot of Aurora does not export). Before the
// window exists there is no swapchain to lose, and the copy of a 1.4 GB image - minutes on a
// phone - runs before the renderer is up rather than during a frame.
//
// The pick comes back as a `content://` URI, a permission grant the system can revoke, so the
// image is copied into app storage once and later launches read a plain file with no grant at
// all. The copy goes through a `.part` file and a rename: a copy that is killed part way (the
// app can be killed during a multi-minute copy) must not leave a truncated disc.iso, which the
// next launch would then prefer over asking again.

// The picked file's head must name a supported disc (plain iso/gcm: "G2ME", "G2MP", "R3ME" or
// "R3MP", "01", version 0, disc 0) or carry a container magic nod reads (rvz, wia, gcz, ciso,
// wbfs): a container hides the disc ID, so the mount-time `IsSupportedDisc` check verifies it.
bool SupportedHeader(const char* header, size_t size) {
  return port::entry::IsPickableDiscHeader(header, size);
}

bool DiscImageIsUsable(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  std::ifstream image(PortUtf8::FromUtf8(path), std::ios::binary);
  if (!image) {
    return false;
  }
  char header[8] = {};
  image.read(header, sizeof(header));
  return SupportedHeader(header, static_cast<size_t>(image.gcount()));
}

// SDL delivers the answer on the activity's thread through JNI, not through the event queue,
// so it is handed back through atomics and this waits for it; nothing pumps events, because
// there is no window yet to deliver a quit. A picker left unanswered ends at the deadline
// rather than hanging a launch with nothing in logcat saying why.
std::string AskForDiscImage() {
  static std::atomic<bool> answered{false};
  static std::atomic<bool> dismissed{false};
  static std::string chosen;
  answered.store(false);
  dismissed.store(false);
  chosen.clear();
  // Static: SDL reads the filters until the dialog closes, which can outlive this call.
  static const SDL_DialogFileFilter filters[] = {
      {"Metroid Prime 2: Echoes / Trilogy disc image", "iso;gcm;rvz;wia;gcz;ciso;wbfs"},
      {"All files", "*"},
  };
  std::fprintf(stderr, "metroid_prime2_port: asking for a disc image\n");
  const SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER,
                         const_cast<SDL_DialogFileFilter*>(filters));
  SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 2);
  SDL_ShowFileDialogWithProperties(
      SDL_FILEDIALOG_OPENFILE,
      [](void*, const char* const* files, int) {
        if (files != nullptr && files[0] != nullptr) {
          chosen = files[0];
        }
        // An empty list is a cancel (Back on Android); a null one is SDL reporting an error.
        dismissed.store(files != nullptr && files[0] == nullptr);
        answered.store(true);
      },
      nullptr, props);
  SDL_DestroyProperties(props);
  const Uint64 deadline = SDL_GetTicks() + 5 * 60 * 1000;
  while (!answered.load()) {
    if (SDL_GetTicks() > deadline) {
      std::fprintf(stderr, "metroid_prime2_port: disc selection timed out\n");
      return {};
    }
    SDL_Delay(10);
  }
  if (chosen.empty()) {
    if (dismissed.load()) {
      std::fprintf(stderr, "metroid_prime2_port: no disc image picked\n");
    } else {
      std::fprintf(stderr, "metroid_prime2_port: the file picker failed: %s\n", SDL_GetError());
    }
  }
  return chosen;
}

// Copies the picked image to `target` through `target.part` and a rename, and returns `target`
// on success, an empty string after a line saying which step failed. The header is checked
// before anything is written, so a wrong pick costs a read rather than a 1.4 GB copy.
std::string CopyDiscImage(const std::string& uri, const std::string& target) {
  if (target.empty()) {
    std::fprintf(stderr, "metroid_prime2_port: nowhere to copy the picked disc image\n");
    return {};
  }
  const std::filesystem::path partial = PortUtf8::FromUtf8(target + ".part");
  std::error_code error;
  std::filesystem::create_directories(PortUtf8::FromUtf8(target).parent_path(), error);

  SDL_IOStream* in = SDL_IOFromFile(uri.c_str(), "rb");
  if (in == nullptr) {
    std::fprintf(stderr, "metroid_prime2_port: could not read the picked image: %s: %s\n", uri.c_str(),
                 SDL_GetError());
    return {};
  }
  char header[8] = {};
  const bool headerRead = SDL_ReadIO(in, header, sizeof(header)) == sizeof(header);
  if (headerRead && !SupportedHeader(header, sizeof(header))) {
    std::fprintf(stderr, "metroid_prime2_port: not copying the picked image: it is not a "
                         "Metroid Prime 2: Echoes disc (expected G2ME01, G2MP01, R3ME01 or R3MP01, revision 0, or an rvz/wia/gcz/ciso/wbfs image)\n");
    SDL_CloseIO(in);
    return {};
  }
  // Some providers hand out a pipe, which cannot be rewound: start over rather than copy the
  // bytes the header check consumed.
  if (SDL_SeekIO(in, 0, SDL_IO_SEEK_SET) != 0) {
    SDL_CloseIO(in);
    in = SDL_IOFromFile(uri.c_str(), "rb");
    if (in == nullptr) {
      std::fprintf(stderr, "metroid_prime2_port: could not reopen the picked image: %s\n",
                   SDL_GetError());
      return {};
    }
  }
  const Sint64 total = SDL_GetIOSize(in);
  SDL_IOStream* out = SDL_IOFromFile(PortUtf8::ToUtf8(partial).c_str(), "wb");
  if (out == nullptr) {
    std::fprintf(stderr, "metroid_prime2_port: could not create %s: %s\n", PortUtf8::ToUtf8(partial).c_str(),
                 SDL_GetError());
    SDL_CloseIO(in);
    return {};
  }

  char buffer[1 << 16];
  Sint64 copied = 0;
  int lastPercent = -1;
  bool ok = true;
  for (;;) {
    const size_t got = SDL_ReadIO(in, buffer, sizeof(buffer));
    if (got == 0) {
      // Zero is also what a failed read returns; only the stream status tells end of file from
      // an error that would leave a truncated copy.
      if (SDL_GetIOStatus(in) != SDL_IO_STATUS_EOF) {
        std::fprintf(stderr, "metroid_prime2_port: reading the picked image failed: %s\n",
                     SDL_GetError());
        ok = false;
      }
      break;
    }
    if (SDL_WriteIO(out, buffer, got) != got) {
      std::fprintf(stderr, "metroid_prime2_port: writing %s failed: %s\n", PortUtf8::ToUtf8(partial).c_str(),
                   SDL_GetError());
      ok = false;
      break;
    }
    copied += static_cast<Sint64>(got);
    if (total > 0) {
      const int percent = static_cast<int>(copied * 100 / total);
      // Every 5%: a 1.4 GB image would otherwise put thousands of lines in logcat.
      if (percent / 5 != lastPercent / 5) {
        lastPercent = percent;
        std::fprintf(stderr, "metroid_prime2_port: copying the disc image, %d%%\n", percent);
      }
    }
  }
  if (!SDL_FlushIO(out)) {
    std::fprintf(stderr, "metroid_prime2_port: flushing %s failed: %s\n", PortUtf8::ToUtf8(partial).c_str(),
                 SDL_GetError());
    ok = false;
  }
  if (!SDL_CloseIO(out)) {
    std::fprintf(stderr, "metroid_prime2_port: closing %s failed: %s\n", PortUtf8::ToUtf8(partial).c_str(),
                 SDL_GetError());
    ok = false;
  }
  if (!SDL_CloseIO(in)) {
    std::fprintf(stderr, "metroid_prime2_port: closing the picked image failed: %s\n", SDL_GetError());
  }
  if (!ok) {
    std::filesystem::remove(partial, error);
    return {};
  }
  // What landed on disk, not what was handed to the writer: a short write that only fails at
  // close looks identical to a good copy otherwise, and a truncated image fails to mount with
  // no further clue.
  const auto written = std::filesystem::file_size(partial, error);
  if (error || static_cast<Sint64>(written) != copied || (total > 0 && copied != total)) {
    std::fprintf(stderr, "metroid_prime2_port: %s holds %lld bytes, copied %lld of %lld\n",
                 PortUtf8::ToUtf8(partial).c_str(), static_cast<long long>(written),
                 static_cast<long long>(copied), static_cast<long long>(total));
    std::filesystem::remove(partial, error);
    return {};
  }
  std::filesystem::rename(partial, PortUtf8::FromUtf8(target), error);
  if (error) {
    std::fprintf(stderr, "metroid_prime2_port: could not rename %s to %s: %s\n", PortUtf8::ToUtf8(partial).c_str(),
                 target.c_str(), error.message().c_str());
    std::filesystem::remove(partial, error);
    return {};
  }
  return target;
}

// A disc picked after Android destroyed the activity behind the picker: the pick arrives at
// the next launch, which MetroidPrime2Activity.onActivityResult leaves in pending_disc_uri
// (MP1 dc9473176). Copied to `target` like a fresh pick, so the lookup after it finds it.
// Without this the next launch asked again, and on a phone that destroys the activity each
// time the picker covers it the game never started.
void TakeDiscPickedLastLaunch(const std::string& target) {
  const char* dir = SDL_GetAndroidInternalStoragePath();
  if (dir == nullptr) {
    return;
  }
  const std::filesystem::path file = PortUtf8::FromUtf8(dir) / "pending_disc_uri";
  std::string chosen;
  {
    std::ifstream in(file);
    std::getline(in, chosen);
  }
  std::error_code ec;
  std::filesystem::remove(file, ec);
  if (chosen.empty()) {
    return;
  }
  std::fprintf(stderr, "metroid_prime2_port: using the disc image picked before the app was restarted\n");
  const std::string copied = CopyDiscImage(chosen, target);
  if (!copied.empty()) {
    std::printf("metroid_prime2_port: disc image picked: %s, copied to %s\n", chosen.c_str(),
                copied.c_str());
  }
}

// The disc this launch will use. The path the resolution chose is used when it is a usable
// image - an image pushed into app storage, or MP2_DISC - and otherwise the user is asked for
// one, which is copied over that same path (the app's own port_paths.txt makes it
// `<files>/disc.iso`, and naming the path the resolution gave keeps a pushed `disc=` override
// meaningful instead of silently writing somewhere else). An empty return is a user who
// cancelled, a picker that failed or a pick that could not be copied, each of which is logged.
std::string EnsureDiscImage(const std::string& resolved, const std::string& defaultDisc) {
  TakeDiscPickedLastLaunch(defaultDisc);
  std::string disc = resolved.empty() ? defaultDisc : resolved;
  for (;;) {
    if (DiscImageIsUsable(disc)) {
      return disc;
    }
    std::fprintf(stderr,
                 "metroid_prime2_port: %s is not a Metroid Prime 2: Echoes disc image; "
                 "asking for one\n",
                 disc.c_str());
    const std::string picked = AskForDiscImage();
    if (picked.empty()) {
      return {};
    }
    const std::string copied = CopyDiscImage(picked, disc);
    if (copied.empty()) {
      continue; // the failure is logged; ask again rather than give up on one bad pick
    }
    std::printf("metroid_prime2_port: disc image picked: %s, copied to %s\n", picked.c_str(),
                copied.c_str());
    disc = copied; // re-checked on the way round, so a bad copy is asked for again
  }
}

#else

// The desktop keeps its console and its environment: fd 1 and fd 2 already arrive where the
// harness reads them, and MP2_USER_PATH / MP2_CACHE_PATH / MP2_DISC are set by the caller,
// so neither of these has anything to do.
void PortLogBegin() {}
void PortLogDrain() {}

#endif

// ---------------------------------------------------------------------------
// The disc check after mounting (MP1's FindUnreadableDiscFile / AskPickDisc / AskForDiscImage).
// ---------------------------------------------------------------------------
// An image cut short by an interrupted download or copy still opens, since the header is at the
// front, and the game only finds out a few frames in, when a read fails. Images store the disc in
// order, so a cut loses the files that end last first: reading the last byte of those catches it
// before the game starts. Returns the first file that can't be read, or an empty string.
std::string FindUnreadableDiscFile() {
  constexpr size_t kFilesToCheck = 8;
  const Uint64 start = SDL_GetTicks();
  const s32 count = aurora_dvd_base_entry_count();
  std::vector<std::pair<int64_t, s32>> ends; // (end offset, entry), the last ones first
  for (s32 entry = 1; entry < count; ++entry) {
    const int64_t offset = aurora_dvd_base_offset(entry);
    if (offset < 0) {
      continue; // a directory
    }
    void* file = aurora_dvd_base_open(entry);
    const int64_t size = file != nullptr ? aurora_dvd_base_seek(file, 0, SEEK_END) : -1;
    aurora_dvd_base_close(file);
    ends.emplace_back(offset + std::max<int64_t>(size, 0), entry);
  }
  std::sort(ends.begin(), ends.end(), std::greater<>());
  ends.resize(std::min(ends.size(), kFilesToCheck));
  for (const auto& [end, entry] : ends) {
    void* file = aurora_dvd_base_open(entry);
    const int64_t size = file != nullptr ? aurora_dvd_base_seek(file, 0, SEEK_END) : -1;
    uint8_t last = 0;
    const bool ok = size == 0 || (size > 0 && aurora_dvd_base_seek(file, size - 1, SEEK_SET) == size - 1 &&
                                  aurora_dvd_base_read(file, &last, 1) == 1);
    aurora_dvd_base_close(file);
    if (!ok) {
      char path[256] = {};
      return DVDConvertEntrynumToPath(entry, path, sizeof(path)) ? std::string(path)
                                                                 : "entry " + std::to_string(entry);
    }
  }
  std::fprintf(stderr, "metroid_prime2_port: disc image is complete (its last %zu files read in %llu ms)\n",
               ends.size(), static_cast<unsigned long long>(SDL_GetTicks() - start));
  return {};
}

constexpr const char* kSupportedDisc = "Metroid Prime 2: Echoes for the GameCube is supported:\nUSA 1.00 (G2ME01); PAL (G2MP01) and Metroid Prime Trilogy (R3ME01, R3MP01) are experimental.";

// No box and no picker when nothing can answer them: MP2_NO_DISC_DIALOG, a headless run, a bounded
// scripted run (MP2_FRAMES), or no window. A refused disc then ends the run, as it always did.
SDL_Window* DiscDialogWindow() {
  if (port::EnvFlag("MP2_NO_DISC_DIALOG") || port::EnvFlag("MP2_HEADLESS") ||
      port::EnvString("MP2_FRAMES") != nullptr) {
    return nullptr;
  }
  int windowCount = 0;
  SDL_Window** windows = SDL_GetWindows(&windowCount);
  SDL_Window* window = windows != nullptr && windowCount > 0 ? windows[0] : nullptr;
  SDL_free(windows);
  return window;
}

// A read of the image that fails mid-game (damage the mount check missed) ends the session, and
// the same image would end every later one. So the failure is noted in a file naming the image,
// and the next launch refuses that image and asks for another (MP1's main.cpp:520-560). Aurora
// calls this once, from whichever thread read.
std::string sDiscReadFailedMarker;
std::string sMountedDisc;

void NoteDiscReadFailure() {
  std::fprintf(stderr, "metroid_prime2_port: a read of the disc image failed\n");
  if (sDiscReadFailedMarker.empty()) {
    return;
  }
  if (std::ofstream file(PortUtf8::FromUtf8(sDiscReadFailedMarker), std::ios::binary); file) {
    file << sMountedDisc;
  }
}

// Whether the last session failed to read this image. Clears the note, and has the overlay say
// why the last session ended whichever image it was.
bool DiscReadFailedLastTime(const std::string& path) {
  if (sDiscReadFailedMarker.empty()) {
    return false;
  }
  std::ifstream in(PortUtf8::FromUtf8(sDiscReadFailedMarker), std::ios::binary);
  if (!in) {
    return false;
  }
  const std::string failed((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  std::error_code ec;
  std::filesystem::remove(sDiscReadFailedMarker, ec);
  PortDebug::NoteDiscReadFailedLastSession();
  return failed == path;
}

// Says on screen why the disc was refused (without it a refused disc looks like a crash on
// start) and asks whether to pick another (true) or close the game. With offerPick false the
// box only has Close.
bool ShowDiscError(const std::string& problem, const std::string& path, bool offerPick,
                   const char* title = "Metroid Prime 2: wrong disc image",
                   const char* pickLabel = "Pick another disc") {
  SDL_Window* window = DiscDialogWindow();
  if (window == nullptr) {
    return false;
  }
  // Short lines: some message boxes do not wrap (SDL's X11 one).
  const std::string message = problem + "\n\n" + (path.empty() ? "" : path + "\n\n") + kSupportedDisc;
  enum { kClose, kPick };
  const SDL_MessageBoxButtonData buttons[] = {
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kPick, pickLabel},
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | (offerPick ? 0u : SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT), kClose,
       "Close"},
  };
  SDL_MessageBoxData data{};
  data.flags = SDL_MESSAGEBOX_ERROR;
  data.window = window;
  data.title = title;
  data.message = message.c_str();
  data.numbuttons = offerPick ? 2 : 1;
  data.buttons = offerPick ? buttons : buttons + 1;
  std::fprintf(stderr, "metroid_prime2_port: showing the disc message\n");
  int answer = -1;
  if (!SDL_ShowMessageBox(&data, &answer)) {
    std::fprintf(stderr, "metroid_prime2_port: could not show the disc message: %s\n", SDL_GetError());
    return false;
  }
  return offerPick && answer == kPick;
}

#if !defined(__ANDROID__)
// The platform's file dialog for a disc image; main remembers a pick that mounts (settings.cfg
// `disc_path`, as F1 > System > Disc image > Replace does). SDL answers on another thread, so this
// pumps events until it does; a quit or five minutes give up, and set sDiscPickGaveUp.
bool sDiscPickGaveUp = false;

std::string PickDiscImage() {
  static std::atomic<bool> answered{false};
  static std::string chosen;
  answered.store(false);
  chosen.clear();
  sDiscPickGaveUp = false;
  SDL_Window* window = DiscDialogWindow();
  if (window == nullptr) {
    return {};
  }
  // Static: SDL reads the filters until the dialog closes, which can outlive this call.
  static const SDL_DialogFileFilter filters[] = {
      {"Metroid Prime 2: Echoes disc image (iso, gcm, rvz, wbfs, ciso, nkit)", "iso;gcm;rvz;wbfs;ciso;nkit"},
      {"All files", "*"},
  };
  const SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, const_cast<SDL_DialogFileFilter*>(filters));
  SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 2);
  SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
  SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING,
                        "Select your Metroid Prime 2: Echoes disc image (GameCube USA or PAL, or Metroid Prime Trilogy)");
  const SDL_DialogFileCallback onAnswer = [](void*, const char* const* files, int) {
    if (files != nullptr && files[0] != nullptr) {
      chosen = files[0];
    }
    answered.store(true);
  };
#if defined(SDL_PLATFORM_IOS)
  // SDL has no file dialog on iOS.
  PortIos::ShowDiscPicker(window, onAnswer, nullptr);
#else
  SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, onAnswer, nullptr, props);
#endif
  SDL_DestroyProperties(props);
  const Uint64 deadline = SDL_GetTicks() + 5 * 60 * 1000;
  while (!answered.load()) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        std::fprintf(stderr, "metroid_prime2_port: disc selection cancelled\n");
        sDiscPickGaveUp = true;
        return {};
      }
    }
    if (SDL_GetTicks() > deadline) {
      std::fprintf(stderr, "metroid_prime2_port: disc selection timed out\n");
      sDiscPickGaveUp = true;
      return {};
    }
    SDL_Delay(10);
  }
  if (chosen.empty()) {
    std::fprintf(stderr, "metroid_prime2_port: no disc image picked\n");
  }
  return chosen;
}

// Asks until a disc is picked or the user closes the game: a dialog closed without a pick used to
// end the game at once, which looked like a crash (MP1's PickDisc).
std::string PickDiscUntilChosen() {
  for (;;) {
    std::string disc = PickDiscImage();
    if (!disc.empty() || sDiscPickGaveUp || DiscDialogWindow() == nullptr ||
        !ShowDiscError("No disc image was picked.", "", true, "Metroid Prime 2: no disc image", "Pick a disc")) {
      return disc;
    }
  }
}
#endif

} // namespace

// Crash reports are PortCrash's (platform/port_crash.cpp): signal, fault address, module offset,
// build, stack and breadcrumbs, written into the log, and the signal then goes on to the handler
// that was there before (the default one, so the exit status still says "died on signal 11",
// which boot_probe.sh greps; on Android debuggerd's, which writes the tombstone).
#if defined(__ANDROID__)
// Everything the report wrote went into the stderr pipe, and only the pump thread turns it into
// logcat lines: give it up to half a second to empty the pipe, or the process dies with the stack
// and the breadcrumbs still in it (a save-state crash on the tablet left nothing but the exit).
// The main log turns over within seconds on a busy device; the crash buffer keeps the last line
// (and the tombstone) for hours.
static void PortCrashAfterReport(int sig, const void* faultAddress) {
  for (int waited = 0; waited < 50 && gLogPipeRead >= 0; ++waited) {
    int queued = 0;
    if (::ioctl(gLogPipeRead, FIONREAD, &queued) != 0 || (queued == 0 && gLogPumpIdle.load())) {
      break;
    }
    const timespec tick = {0, 10 * 1000 * 1000};
    ::nanosleep(&tick, nullptr);
  }
  __android_log_buf_print(LOG_ID_CRASH, ANDROID_LOG_FATAL, "mp2port", "[port] caught signal %d at %p", sig,
                          faultAddress);
}
#endif

int main(int argc, char** argv) {
#if defined(_WIN32)
  if ((argc == 4 || argc == 5) && std::strcmp(argv[1], "--log-copy") == 0) {
    return PortLogFile::RunCopy(argv[2], argv[3], argc == 5 ? argv[4] : nullptr);
  }
  PortLogFile::AttachParentConsole();
#endif
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("Metroid Prime 2 native port %s (%s)\n", MP_BUILD_VERSION, MP_BUILD_REVISION);
    return 0;
  }
  // Android first, before anything can print: from here on fd 1 and fd 2 reach logcat, which
  // is the only place a device run leaves a record. A no-op on the desktop.
  PortLogBegin();

  // ctest port_settings_roundtrip: every settings.cfg key reads back as written, then exit.
  if (argc == 2 && std::strcmp(argv[1], "--settings-selftest") == 0) {
#if defined(_WIN32)
    // Headless under ctest: an abort or a fault must end the run, not wait on a dialog nobody
    // clicks until the test times out (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX).
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(0x0001 | 0x0002);
    std::fputs("settings self-test: start\n", stderr);
#endif
    std::string report;
    const int result = PortOverlaySettings::SettingsSelfTest(report);
    std::fputs(report.c_str(), result == 0 ? stdout : stderr);
    return result;
  }

#ifdef MP_PORT_COVERAGE
  // A coverage run must leave its .gcda behind however it ends: the clean quit, the harness's
  // SIGTERM, a Ctrl-C. The handler dumps the counters and re-raises, so the exit status still
  // says which signal ended the run. See platform/port_coverage.cpp.
  PortCoverageInstall();
#endif

#if defined(__linux__) && !defined(__ANDROID__)
  // MP2_ALLOW_PTRACE=1: let any process of this user attach (`gdb -p`, tools/cov/force.sh
  // --attach) under yama ptrace_scope 1, which otherwise only lets a parent trace its child.
  if (const char* allow = getenv("MP2_ALLOW_PTRACE"); allow && allow[0] == '1') {
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
  }
#endif

  // Android has no environment to set, so the app's own directories stand in where the
  // variables are unset (and print which they are). On the desktop the user directory is the
  // one Aurora itself falls back to, so settings.cfg, controls.toml, the touch layout and the
  // save tools sit beside the memory card instead of in no folder at all (an empty user path
  // disables every one of those files, which is why the F1 settings did not survive a restart).
  PortPaths appPaths;
#if defined(__ANDROID__)
  appPaths = ResolveAndroidPaths();
#else
  const char* userWhy = "";
  if (const char* env = std::getenv("MP2_USER_PATH"); env != nullptr && env[0] != '\0') {
    appPaths.user = env;
    userWhy = "MP2_USER_PATH";
  } else {
    appPaths.user = DesktopUserFolder(&userWhy);
  }
  appPaths.cache = appPaths.user;  // Aurora's own fallback is this folder too, but can come back null
#if defined(SDL_PLATFORM_IOS)
  appPaths.cache = PortIos::CachesFolder();  // hidden from the Files app; iOS may clear it
#endif
  if (!appPaths.user.empty()) {
    std::printf("metroid_prime2_port: user folder %s (%s)\n", appPaths.user.c_str(), userWhy);
  } else {
    std::fprintf(stderr,
                 "metroid_prime2_port: no home directory; set MP2_USER_PATH to keep settings, "
                 "bindings and saves\n");
  }
#endif

  // The disc, resolved and - if the image is missing or is not this game - picked from the
  // user's storage, all before Aurora comes up: `EnsureDiscImage` says why the trigger is the
  // image not being usable rather than the disc check after the renderer starts. The desktop
  // keeps the resolution it always had, below.
#if defined(__ANDROID__)
  const std::string androidDiscImage =
      EnsureDiscImage(port::entry::ResolveDiscPath(argc, argv, EnvOrFallback("MP2_DISC", appPaths.disc),
                                                   ExecutableDir(argc > 0 ? argv[0] : nullptr)),
                      appPaths.disc);
  if (androidDiscImage.empty()) {
    // Nothing was picked, so there is nothing to boot: exiting here, before Aurora is up,
    // rather than at the disc check below, which would bring the renderer up only to close it.
    std::fprintf(stderr, "metroid_prime2_port: no disc image to boot\n");
    PortLogDrain();
    return 1;
  }
#endif

  // The touch overlay's own settings live in the user directory, next to the memory card:
  // the layout the player moved, and which of the two layouts is active. On the desktop this
  // is MP2_USER_PATH or nothing at all - there is no overlay there, and an empty directory
  // just leaves the defaults in place. Called before Aurora because the Java view polls the
  // layout as soon as the surface exists.
  const char* touchUser = EnvOrFallback("MP2_USER_PATH", appPaths.user);
  PortTouch::Initialize(touchUser != nullptr ? touchUser : std::string());

  // The keyboard and pad bindings (controls.toml, platform/input/) are saved beside touch.cfg, in the same
  // directory. The file is read on the game thread's first frame rather than here, because its
  // key names come from SDL, which is only up after Aurora is.
  PortInputDevices::Initialize(touchUser != nullptr ? touchUser : std::string());

  // settings.cfg (platform/port_settings.cpp), in the same directory: read here, applied as the F1
  // overlay registers its settings (file, then the MP2_* environment on top). Written only when a
  // setting is changed.
  PortSettings::Initialize(touchUser != nullptr ? touchUser : std::string());
  // Read now: an earlier request (the Android view's, on its own thread) found no path and left it.
  PortOverlaySettings::LoadSettings();

  // The log file (platform/port_logfile.cpp) starts here, before aurora_initialize below, so
  // Aurora's own startup lines and anything that aborts during it land in the file too.
  PortLogFile::Start(touchUser != nullptr ? touchUser : std::string());
#if !defined(__ANDROID__)
  // The folder choice was printed before the log existed; say it again so the file has it.
  std::printf("metroid_prime2_port: user folder %s (%s)\n", appPaths.user.c_str(), userWhy);
#endif
  // Crash reports into that log (MP2_CRASH_TEST=segv|abort crashes here to check one).
#if defined(__ANDROID__)
  PortCrash::SetAfterReport(PortCrashAfterReport);
#endif
  PortCrash::Install(touchUser != nullptr ? touchUser : std::string());
  // Into that log, as MP1 does: every setting in effect and the MP2_* variables, then (from a
  // background thread) how much space each folder the port writes to takes.
  PortOverlaySettings::LogSettings();
  // How the previous run ended (Android 11+) and the device's memory now (MP1 issue #102).
  PortMemoryLog::LogStartup();
  {
    const char* cache = EnvOrFallback("MP2_CACHE_PATH", appPaths.cache);
    PortStorageReport::StartLogging({touchUser != nullptr ? touchUser : std::string(),
                                     cache != nullptr ? cache : std::string()});
  }

  // The save tools (platform/port_save.cpp) keep their backups beside controls.toml; the card's own
  // path comes from Aurora when a command needs it.
  PortSave::Initialize(touchUser != nullptr ? touchUser : std::string());

  // The debug command console (platform/port_console.cpp): with MP2_CONSOLE=<port> it listens on
  // 127.0.0.1 before the game starts, so a test can drive it from the first update. Commands are
  // queued here and run inside the state manager's update, where the world is live.
  PortConsole::Start();

  // MP2_HEADLESS=1: a test run with no picture (docs/RUNNING.md "Headless runs"). Aurora makes no
  // window surface and uses Dawn's Null device (AURORA_HEADLESS), and SDL's offscreen video driver
  // needs no X server, so the run skips the GPU start-up and takes no GPU time.
  if (port::EnvFlag("MP2_HEADLESS")) {
    port::SetEnv("AURORA_HEADLESS", "1");
    port::SetEnv("SDL_VIDEODRIVER", "offscreen");
  }

  // 720p by default, as the Prime 1 port: 16:9 unless the aspect setting is 4:3. The render mode
  // follows the setting (PortOverlay::ApplyAspect); Aurora upscales to the window either way.
  const bool widescreen = PortOverlay::AspectMode() != PortOverlay::kAspect_4_3;
  AuroraConfig config = {
      .appName = kAppName,
      .userPath = EnvOrFallback("MP2_USER_PATH", appPaths.user),
      .cachePath = EnvOrFallback("MP2_CACHE_PATH", appPaths.cache),
      .resourcesPath = nullptr,
      .desiredBackend = BACKEND_AUTO,
      .vsync = false, // the `vsync` setting (platform/port_overlay.cpp) is the source of truth
      .startFullscreen = PortOverlay::Fullscreen(),
      // MP2_DUMP_TEXTURES=1 writes every texture the game loads to <cache>/texture_dumps, named
      // as a replacement pack must name it.
      .allowTextureDumps = port::EnvFlag("MP2_DUMP_TEXTURES"),
      .windowPosX = -1, // centred, as the Prime 1 port
      .windowPosY = -1,
      .windowWidth = static_cast< uint32_t >(widescreen ? 1280 : 960),
      .windowHeight = 720u,
      .mem1Size = MEM1_DEFAULT_SIZE,
      .mem2Size = ARAM_DEFAULT_SIZE,
  };
#if !defined(__ANDROID__)
  // The window icon, for a bare binary that no desktop entry describes. Android takes its icon
  // from the APK.
  static uint8_t windowIcon[] = {
#include "port_window_icon.inc"
  };
  config.iconRGBA8 = windowIcon;
  config.iconWidth = 64;
  config.iconHeight = 64;
#endif
  // Lazy pipelines: build them on first use and prefetch an area's recorded ones as it streams in
  // (MP1 sync package 10). The startup compile of the cached pipelines costs MP1 ~950 MiB of process
  // memory (its issue #162), so Android, and any device short on RAM, is lazy; the desktop
  // precompiles. MP2_PIPELINE_LAZY=0|1 overrides.
  {
    char why[96] = "";
    bool lazy = PortMemoryLog::LowRamDevice(why, sizeof(why));
#if defined(__ANDROID__)
    if (!lazy) {
      lazy = true;
      std::strncat(why, ", Android", sizeof(why) - std::strlen(why) - 1);
    }
#endif
    const char* reason = why;
    if (const char* env = std::getenv("MP2_PIPELINE_LAZY"); env != nullptr && env[0] != '\0') {
      lazy = port::EnvFlag("MP2_PIPELINE_LAZY", lazy);
      reason = "MP2_PIPELINE_LAZY";
    }
    if (lazy || why[0] != '\0' || reason != why) {
      std::fprintf(stderr, "port: pipelines %s at startup (%s)\n",
                   lazy ? "built on first use" : "precompiled", reason);
    }
    config.pipelineCacheLazy = lazy;
  }
  // Aurora's own logger is fmt::println(stderr), which throws when the write fails, from inside a
  // noexcept function: with the log copier gone (EPIPE) the next line Aurora logged aborted the
  // game. Same format, through stdio, whose errors are only a lost line.
  config.logCallback = [](AuroraLogLevel level, const char* module, const char* message,
                          unsigned int len) {
    static const char* const kNames[] = {"debug", "info", "warning", "error", "fatal"};
    const char* name = static_cast< unsigned >(level) < 5 ? kNames[level] : "unknown";
    std::fprintf(stderr, "[%s] [%s] %.*s\n", name, module, static_cast< int >(len), message);
  };
  // Lazy mode keeps built pipelines until memory runs short (MP1 issue #187), areas left long ago
  // included, so walking back is free. It trims on Android's memory warnings (port_memory_log.cpp)
  // and on the memory log's MemAvailable check every 5 s: under 1024 MiB available it releases
  // what a minute of frames didn't draw, under 512 MiB all but what the last frames drew (GLOBAL
  // pipelines stay). Overrides: MP2_PIPELINE_TRIM_MIB / MP2_PIPELINE_TRIM_SEVERE_MIB (0 turns a
  // level off), MP2_PIPELINE_EVICT=<frames> (a fixed idle sweep at every frame start, any mode).
  if (config.pipelineCacheLazy) {
    const int trimMib = port::EnvInt("MP2_PIPELINE_TRIM_MIB", 1024);
    const int severeMib = port::EnvInt("MP2_PIPELINE_TRIM_SEVERE_MIB", 512);
    PortMemoryLog::SetPipelinePressure(trimMib, severeMib);
    if (std::getenv("MP2_PIPELINE_TRIM_MIB") != nullptr ||
        std::getenv("MP2_PIPELINE_TRIM_SEVERE_MIB") != nullptr) {
      std::fprintf(stderr,
                   "port: pipeline trim under %d MiB available, all idle under %d MiB "
                   "(MP2_PIPELINE_TRIM_MIB/MP2_PIPELINE_TRIM_SEVERE_MIB)\n",
                   trimMib, severeMib);
    }
  }
  if (const char* env = std::getenv("MP2_PIPELINE_EVICT"); env != nullptr && env[0] != '\0') {
    config.pipelineEvictFrames = static_cast< uint32_t >(std::strtoul(env, nullptr, 10));
    std::fprintf(stderr, "port: pipeline eviction %u frames (MP2_PIPELINE_EVICT)\n",
                 config.pipelineEvictFrames);
  }

  // SDL3 reaches for its Wayland backend whenever a Wayland display is reachable, and does so even
  // without WAYLAND_DISPLAY - it falls back to the default socket in XDG_RUNTIME_DIR. Under GNOME
  // that backend never returns from SDL_ShowWindow: it dispatches pending events, libdecor's
  // client-side decoration configure re-enters GTK layout from inside that dispatch, and the
  // process spins at 100% before the first frame is ever presented (MP1 found it by backtrace; see
  // its docs/NATIVE_PORT.md "A Wayland session hangs at startup"). Nothing in the port can fix
  // that, so whenever an X display is available ask for the X11 backend instead. SDL_VIDEODRIVER
  // still wins, which is also how anyone who wants Wayland opts back in, and MP2_HEADLESS's
  // "offscreen" above is honoured the same way.
#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(__APPLE__)
  {
    const char* requested = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
    const char* x11 = std::getenv("DISPLAY");
    const auto present = [](const char* v) { return v != nullptr && v[0] != '\0'; };
    const bool unnamed = !present(requested);
    if (unnamed && present(x11)) {
      SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
      std::fprintf(stderr,
                   "port: using SDL's x11 video driver on %s; its wayland backend hangs on window "
                   "creation under GNOME (libdecor). Set SDL_VIDEODRIVER to override.\n",
                   x11);
    } else if (unnamed) {
      // Nothing to fall back to: SDL will use Wayland, and on GNOME that is the hang above. Say so
      // before the silence, since there is no way to tell from inside the hang.
      std::fputs("port: no DISPLAY set, so SDL will use its wayland backend, which hangs on window "
                 "creation under GNOME (libdecor). Run under an X display, or set SDL_VIDEODRIVER "
                 "yourself.\n",
                 stderr);
    }
  }
#endif
  // F1 > Video > Compatibility (PortVideo, f1b): OpenGL ES, the storage clamp, the low-power
  // adapter and the driver workarounds' Dawn toggles, all read when the GPU device is made.
  PortVideo::BeginGpuStart(config.userPath != nullptr ? config.userPath : "", config);
  aurora_initialize(argc, argv, &config);
  // The settings file, JSON and the logs read and write numbers in the "C" format. A toolkit loaded
  // with the window (a GTK decoration plugin, an input method) may call setlocale(LC_ALL, ""), and
  // under a comma-decimal locale "1.5" would then read as 1 (MP1's main.cpp:1304).
  std::setlocale(LC_NUMERIC, "C");
  PortVideo::GpuStarted();
  // Keep the internal framebuffer at the game's aspect rather than the window's,
  // so a mismatched window shape letterboxes instead of stretching.
  AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);

  // Texture replacements (platform/port_textures.cpp) and the binding icons drawn over the game's
  // button prompts (platform/port_prompts.cpp), as in the Prime 1 port: the built-in set is
  // MP2_TEXTURES, else the set built into the executable (MP_EMBED_RESOURCES), else a `textures`
  // folder beside the executable (on Android, the app files directory), and the user's pack
  // MP2_USER_TEXTURES, else <user>/user_textures. Both need Aurora's texture registry, so they
  // start after aurora_initialize.
  {
    std::string textures;
    if (const char* env = port::EnvString("MP2_TEXTURES"); env != nullptr && env[0] != '\0') {
      textures = env;
    } else if (!PortEmbedded::Under("textures/").empty()) {
      // MP2_TEXTURES wins. Otherwise a build that carries the set (MP_EMBED_RESOURCES) passes no
      // folder, so PortTextures/PortPrompts use the built-in copy and a stale `textures` folder left
      // in an old install is ignored.
      std::fprintf(stderr, "metroid_prime2_port: textures built into the executable\n");
    } else {
#if defined(__ANDROID__)
      // Unpacked by the activity beside port_paths.txt, so a moved user folder does not hide it.
      const std::string base = appPaths.base;
#else
      // The same helper as the portable user/ rule, not argv[0]: a bare-name launch from PATH has
      // no folder in it. argv[0] only if the OS cannot say.
      std::string base = ExecutableFolder();
      if (base.empty()) {
        base = ExecutableDir(argc > 0 ? argv[0] : nullptr);
      }
      while (base.size() > 1 && (base.back() == '/' || base.back() == '\\')) {
        base.pop_back();
      }
#endif
      std::error_code error;
      if (!base.empty() && std::filesystem::is_directory(base + "/textures", error)) {
        textures = base + "/textures";
        std::fprintf(stderr, "metroid_prime2_port: textures %s\n", textures.c_str());
      }
#if defined(__APPLE__)
      // A macOS bundle keeps data in Contents/Resources (codesign rejects non-code files in
      // Contents/MacOS).
      else if (!base.empty() && std::filesystem::is_directory(base + "/../Resources/textures", error)) {
        textures = base + "/../Resources/textures";
      }
#endif
    }
    std::string userTextures;
    if (const char* env = port::EnvString("MP2_USER_TEXTURES"); env != nullptr && env[0] != '\0') {
      userTextures = env;
    } else if (touchUser != nullptr && touchUser[0] != '\0') {
      userTextures = std::string(touchUser) + "/user_textures";
    }
    PortTextures::Initialize(textures.empty() ? nullptr : textures.c_str(),
                             userTextures.empty() ? nullptr : userTextures.c_str());
    PortPrompts::Initialize(textures.empty() ? nullptr : textures.c_str());
  }

  std::string discImage =
#if defined(__ANDROID__)
      // Resolved and, if need be, picked from the user's storage before Aurora started.
      androidDiscImage;
#else
      port::entry::ResolveDiscPath(argc, argv, DesktopDiscFallback(appPaths.disc).c_str(),
#if defined(SDL_PLATFORM_IOS)
                                   // The app bundle is signed and read-only: an image copied into
                                   // Documents (Files app) starts without asking.
                                   appPaths.user);
#else
                                   // Not argv[0]: a bare-name launch from PATH has no folder in it.
                                   !ExecutableFolder().empty()
                                       ? ExecutableFolder()
                                       : ExecutableDir(argc > 0 ? argv[0] : nullptr));
#endif
#endif
  [[maybe_unused]] bool discPicked = false;
#if !defined(__ANDROID__)
  // A disc named on the command line or by MP2_DISC would win again on the next launch, so a
  // refused one only offers Close (MP1's discFromArgs).
  bool discFromArgs = !discImage.empty() &&
                      discImage == port::entry::ResolveDiscPath(argc, argv, std::getenv("MP2_DISC"), std::string());
  if (discImage.empty() && DiscDialogWindow() != nullptr) {
    // First start: the platform's file dialog (nothing answers it in a scripted run, see
    // DiscDialogWindow, which then falls through to the message below).
    std::fprintf(stderr, "metroid_prime2_port: no disc image found; asking for one\n");
    discImage = PickDiscUntilChosen();
    discPicked = !discImage.empty();
  }
#endif
  if (discImage.empty()) {
    ReportNoDisc(argc > 0 ? argv[0] : nullptr);
    aurora_shutdown();
    PortLogDrain();
    return 1;
  }

  // A disc that will not open, is not the one game this runs, or is cut short is said on screen
  // and, on the desktop, asked for again (MP1's loop). A scripted or headless run, and Android
  // (whose picker ran before Aurora), print the reason and end as before.
  if (touchUser != nullptr && touchUser[0] != '\0') {
    sDiscReadFailedMarker = std::string(touchUser) + "/disc-read-failed";
  }
  for (;;) {
    std::string problem;
    if (!aurora_dvd_open(discImage.c_str())) {
      std::fprintf(stderr, "metroid_prime2_port: failed to open disc image: %s\n", discImage.c_str());
      problem = "This file could not be read as a GameCube disc image.";
    } else if (const DVDDiskID* discId = DVDGetCurrentDiskID();
               discId == nullptr || !port::entry::IsSupportedDisc(*discId)) {
      std::fprintf(stderr,
                   "metroid_prime2_port: unsupported disc; expected Metroid Prime 2: Echoes "
                   "USA 1.00 (G2ME01), PAL (G2MP01) or Metroid Prime Trilogy (R3ME01, R3MP01), revision 0.\n");
      problem = "This disc image is not Metroid Prime 2: Echoes (USA 1.00, PAL or Metroid Prime Trilogy).";
      aurora_dvd_close();
    } else if (port::entry::IsTrilogy(port::entry::CurrentDiscVersion()) &&
               !aurora_dvd_set_root(port::entry::kTrilogyRootDir, port::entry::kTrilogyDol)) {
      // The Trilogy disc keeps Echoes' paks in MP2/ and its DOL as rs5mp2_p.dol; the game opens
      // paks by bare name, so MP2/ becomes the root (docs/TRILOGY.md).
      std::fprintf(stderr, "metroid_prime2_port: the Trilogy disc has no MP2 directory or rs5mp2_p.dol\n");
      problem = "This Metroid Prime Trilogy image has no Echoes (MP2) data.";
      aurora_dvd_close();
    } else if (DiscReadFailedLastTime(discImage)) {
      std::fprintf(stderr, "metroid_prime2_port: refused the disc image: a read of it failed last session\n");
      problem = "Part of this disc image couldn't be read last time, so it's damaged.\n"
                "Copy the file again, or check it in Dolphin (Properties > Verify).";
      aurora_dvd_close();
    } else if (const std::string unreadable = FindUnreadableDiscFile(); !unreadable.empty()) {
      std::fprintf(stderr, "metroid_prime2_port: disc image is incomplete: %s can't be read\n", unreadable.c_str());
      problem = "This disc image is incomplete or damaged: part of it (" + unreadable +
                ") can't be read.\nIt's probably an interrupted download or copy. Copy the file again.";
      aurora_dvd_close();
    }
    if (problem.empty()) {
      const port::entry::DiscVersion version = port::entry::CurrentDiscVersion();
      std::fprintf(stderr, "metroid_prime2_port: disc: Metroid Prime 2: Echoes %s (%s01)%s\n",
                   port::entry::DiscVersionName(version), port::entry::DiscGameCode(version),
                   version == port::entry::DiscVersion::Pal ? ", experimental: USA 1.00 code on PAL data"
                   : port::entry::IsTrilogy(version) ? ", experimental: USA 1.00 code on Trilogy data" : "");
      break;
    }
#if defined(__ANDROID__)
    constexpr bool kCanPick = false;
#else
    const bool kCanPick = !discFromArgs;
#endif
#if !defined(__ANDROID__)
    if (PortOverlaySettings::DiscPath() == discImage) {
      // A refused remembered disc would be refused again on every launch (MP1's main.cpp:498).
      PortOverlaySettings::SetDiscPath({});
      PortOverlaySettings::SaveSettings();
      std::fprintf(stderr, "metroid_prime2_port: forgot the remembered disc image\n");
    }
#endif
    if (!ShowDiscError(problem, discImage, kCanPick)) {
      aurora_shutdown();
      PortLogDrain();
      return 1;
    }
#if !defined(__ANDROID__)
    std::fprintf(stderr, "metroid_prime2_port: asking for the disc image again\n");
    discImage = PickDiscUntilChosen();
    if (discImage.empty()) {
      ReportNoDisc(argc > 0 ? argv[0] : nullptr);
      aurora_shutdown();
      PortLogDrain();
      return 1;
    }
    discPicked = true;
    discFromArgs = false;
#endif
  }
  std::printf("metroid_prime2_port: disc mounted: %s\n", discImage.c_str());
#if !defined(__ANDROID__) && !defined(SDL_PLATFORM_IOS)
  if (discPicked) {
    // The next launch starts without asking (DesktopDiscFallback). Saved now: the overlay only
    // saves from its draw path. iOS keeps a picked file's grant for this launch only.
    PortOverlaySettings::SetDiscPath(discImage);
    PortOverlaySettings::SaveSettings();
    std::fprintf(stderr, "metroid_prime2_port: remembered the disc image\n");
  }
#endif
  sMountedDisc = discImage;
  aurora_dvd_set_read_error_callback(NoteDiscReadFailure);
  // F1 > System > Disc image checks the file the game is playing (platform/port_disc_verify.cpp).
  PortDiscVerify::SetImagePath(discImage);
  // The mods folder layers over the disc through Aurora's overlays; the game caches PAK tables
  // when it boots, so this runs after the mount and before anything reads the disc.
  PortMods::Initialize(touchUser != nullptr ? touchUser : std::string());

  // Prime the window and event state so the game's first frame can succeed: the
  // console build submits GX during early initialization, before its main loop.
  aurora_update();

  // The game's entry owns the OS context and memory system. It returns void, so
  // the exit code is the platform's: reaching the end means the game returned.
  COsContext osContext(true, true);
  // The game heap is carved out of the arena by the line below; `PortMwccNew.cpp`'s `operator
  // delete` routes pointers inside this range to `CMemory::Free`.
  gPortGameHeapLo = OSGetArenaLo();
  gPortGameHeapHi = OSGetArenaHi();
  CMemorySys memorySys(osContext, CMemorySys::GetGameAllocator());

  // Retail builds a `CGraphicsSys` here too, in `main` (0x801EFB00), and passes it to
  // `InvokeCMain` as the sixth argument - an 8-byte object built with `fn_802BE85C`,
  // which is `CGraphicsSys::CGraphicsSys` and whose whole body is
  // `CGraphics::Startup(osContext, progressive)`. That constructor is where retail's VI
  // bring-up happens (`fn_802C329C` -> `fn_802C2FD4` = `ConfigureVideo`:
  // `VIGetTvFormat` -> `GXAdjustForOverscan` -> two framebuffers -> `VIConfigure` ->
  // `VIFlush` -> `GXInit` -> `GXSetCopyFilter`), and it has to run *before* the game
  // starts: without `GXInit` there is no GX, and without `mRenderModeObj` filled there is
  // no EFB shape for Aurora's presenter.
  //
  // The `false` is the progressive flag. Retail reads it from the console's saved region -
  // `fn_801EFC68` / `fn_801EFE6C` read the region `lbl_80419304` points at, and the byte
  // they return is the flag. The host has no saved region: there is no console to have
  // set it, and `OSGetSavedRegion` in platform/sdk_stubs.cpp answers null/null, so
  // progressive is unconditionally off. That is also the value retail's own PAL build
  // uses, and the one `ConfigureVideo`'s NTSC branch takes `GXNtsc480IntDf` for.
  //
  // This is a real object with a real destructor, not a leaked temporary: `~CGraphicsSys`
  // calls `CGraphics::Shutdown`, which restores the texture-region callback and stalls the
  // frame-delayed allocator, and both are retail's.
  //
  // It lives in a scope of its own so that it is destroyed before the platform goes down.
  // `CGraphics::Shutdown` stalls the GPU (`StallAndFlushAllAllocations` -> `GXDrawDone` ->
  // `aurora::gx::fifo::drain`), and in Aurora's threaded mode that waits for the FIFO worker
  // thread, which `aurora_shutdown` joins. As a plain local of `main` it was destroyed after
  // `aurora_shutdown` and waited forever for a thread that was gone (measured 2026-10-01: six
  // threads at the hang, no "Aurora FIFO processor"). On the console `main` returns with the
  // video hardware still up, so retail's order is this one.
  {
    CGraphicsSys graphicsSys(osContext, memorySys, false);

    // Three REL modules are compiled into the game library instead of being read
    // off the disc - Tweaks, CannonBall and ForgottenObject. On the cube each is a
    // separate module whose prolog and epilog run when it loads, which is what
    // publishes their function-pointer tables. Nothing on the host loads them, so
    // the port runs their entry points here, before the game's own entry, or those
    // tables stay null and the loaders behind them are never reached.
    port::modules::InitAll();

    // Tweaks.rel's `REL_CreateTweakGlobals` is the only writer of `gpTweakPlayerA`
    // (0x80418F44), and `CGameArchitectureSupport`'s constructor dereferences it at
    // 0x80007F38 with no null test - so this is the first of the two globals the
    // boot path needs and the only one that can be stood in for. The stand-in
    // allocates real 4-byte cells over a zeroed `SLdrTweakPlayer`, which makes the
    // five `CTweakPlayer` accessors answer 0.0f; it carries no tweak data, because
    // the data lives in `Standard.NTWK` inside a pak and the paks are boot-path
    // step 13. The real globals come later: the game's `main` runs `LoadTweaks` and
    // `CreateTweakGlobals` once `Standard.NTWK` is read (src/MetroidPrime/main.cpp),
    // which overwrites both pointers and leaks the zeroed stand-ins. See
    // src/MetroidPrime/PortTweakGlobals.cpp and docs/research/tweak_globals.md.
    port::tweaks::CreateStandInTweakPlayers();

    // Retail `main` loads the embedded default font right after `CGraphicsSys` and points
    // `gpDefaultFont` at it; `CSplashScreen::Draw` dereferences it with no null test.
    // src/MetroidPrime/PortDefaultFont.cpp.
    void* defaultFont = port::LoadDefaultFont();

    // An exception out of the game is logged with its type and what() before the abort, whose
    // crash report follows; uncaught, it would end the run with only "terminate called".
    try {
      InvokeCMain(argc, argv, &osContext, nullptr, &memorySys, nullptr);
    } catch (...) {
      PortCrash::AbortOnException("the main loop");
    }

    port::FreeDefaultFont(defaultFont);
    port::modules::ShutdownAll();
  }

  PortDiscReplace::Shutdown();
  aurora_dvd_close();
  aurora_shutdown();
#ifdef MP_PORT_COVERAGE
  // The shutdown path's own dump: the run does not depend on the exit hook to leave a profile
  // behind. The counters are out by now, so this is the last write.
  PortCoverageDump();
#endif
  // The game has returned; nothing else will write to fd 1 or 2, so the last lines are
  // handed to logcat before this thread ends.
  PortLogDrain();
  return 0;
}
