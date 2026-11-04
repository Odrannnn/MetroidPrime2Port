#include "port_data_folder.h"
#include "port_strings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#if defined(__ANDROID__)
#include "port_overlay_internal.h"
#include "port_logfile.h"
#include "port_save.h"

#include <imgui.h>
#include <jni.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_system.h>
#include <SDL3/SDL_timer.h>

#include <cinttypes>
#include <cstdio>
#include <mutex>
#include <thread>
#endif

namespace PortDataFolder {
namespace {

namespace fs = std::filesystem;

fs::path FromUtf8(const std::string& text) {
  return fs::path(std::u8string(text.begin(), text.end()));
}

std::string ToUtf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

std::string WithSeparator(std::string path) {
  if (!path.empty() && path.back() != '/') {
    path += '/';
  }
  return path;
}

const char* const kTempSuffix = ".mpcopy";
const char* const kIncomplete = ".mpcopy_incomplete";

std::string Relative(const fs::path& path, const fs::path& base) {
  const std::u8string text = path.lexically_relative(base).generic_u8string();
  return std::string(text.begin(), text.end());
}

using port::EndsWith;

// Every file a move copies, relative to `root` with '/' separators.
// `failed`, when given, is set if the walk stopped on an error: the list is then
// incomplete, which a copy must not take for the whole folder.
std::vector< std::string > ListFiles(const std::string& root, bool* failed = nullptr) {
  std::vector< std::string > files;
  std::error_code ec;
  const fs::path base = FromUtf8(root);
  if (failed != nullptr) {
    *failed = false;
  }
  if (!fs::is_directory(base, ec)) {
    return files;
  }
  fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
  for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const std::string relative = Relative(it->path(), base);
    if (IsSkipped(relative)) {
      if (it->is_directory(ec)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (it->is_regular_file(ec)) {
      files.push_back(relative);
    }
  }
  if (ec && failed != nullptr) {
    *failed = true;
  }
  return files;
}

} // namespace

std::string SharedFolder() {
#if defined(__ANDROID__)
  // EXTERNAL_STORAGE is usually /sdcard, a link to the primary volume of the
  // current user; the resolved path is what a file manager shows.
  const char* env = std::getenv("EXTERNAL_STORAGE");
  std::string root = env != nullptr && env[0] == '/' ? env : "/storage/emulated/0";
  std::error_code ec;
  const fs::path resolved = fs::canonical(FromUtf8(root), ec);
  if (!ec) {
    root = ToUtf8(resolved);
  }
  return WithSeparator(root) + "MetroidPrime2/";
#else
  return {};
#endif
}

bool IsSkipped(const std::string& relative) {
  static const char* const kTopLevel[] = {kIncomplete};
  if (relative.find('/') == std::string::npos) {
    for (const char* name : kTopLevel) {
      if (relative == name) {
        return true;
      }
    }
  }
  const std::string name = relative.substr(relative.rfind('/') + 1);
  return name == ".port_write_test" || EndsWith(name, ".partial") || EndsWith(name, ".part") ||
         EndsWith(name, kTempSuffix);
}

bool WriteUserLine(const std::string& markerFile, const std::string& userFolder) {
  const fs::path marker = FromUtf8(markerFile);
  std::vector< std::string > lines;
  {
    std::ifstream in(marker);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      const size_t first = line.find_first_not_of(" \t");
      if (first != std::string::npos && line.compare(first, 5, "user=") == 0) {
        continue;
      }
      lines.push_back(line);
    }
  }
  fs::path temp = marker;
  temp += ".new";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << "user=" << userFolder << '\n';
    for (const std::string& line : lines) {
      out << line << '\n';
    }
    out.close();
    if (!out) {
      return false;
    }
  }
  std::error_code ec;
  fs::rename(temp, marker, ec);
  return !ec;
}

Totals Measure(const std::string& from) {
  Totals totals;
  const fs::path base = FromUtf8(from);
  for (const std::string& relative : ListFiles(from)) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(base / FromUtf8(relative), ec);
    if (!ec) {
      totals.bytes += size;
    }
    ++totals.files;
  }
  return totals;
}

bool HasData(const std::string& dir) {
  return !ListFiles(dir).empty();
}

bool IsComplete(const std::string& dir) {
  std::error_code ec;
  return !fs::exists(FromUtf8(dir) / kIncomplete, ec);
}

bool CopyTree(const std::string& from, const std::string& to, Progress& progress, std::string& error,
              bool changedOnly) {
  const fs::path source = FromUtf8(from);
  const fs::path target = FromUtf8(to);
  const fs::path incomplete = target / kIncomplete;
  {
    std::error_code ec;
    fs::create_directories(target, ec);
    std::ofstream flag(incomplete, std::ios::binary | std::ios::trunc);
    if (!flag) {
      error = "Could not write to " + to;
      return false;
    }
  }
  std::vector< char > buffer(1 << 20);
  bool listFailed = false;
  const std::vector< std::string > files = ListFiles(from, &listFailed);
  if (listFailed) {
    // The old data is left alone, and the incomplete flag stays in the target.
    error = "Could not read all of " + from + " (a folder could not be listed).";
    return false;
  }
  for (const std::string& relative : files) {
    const fs::path in = source / FromUtf8(relative);
    const fs::path out = target / FromUtf8(relative);
    fs::path temp = out;
    temp += kTempSuffix;
    std::error_code ec;
    if (changedOnly) {
      // The copy is written after the original, so a newer original changed since.
      std::error_code inEc;
      const uintmax_t inSize = fs::file_size(in, inEc);
      const uintmax_t outSize = fs::file_size(out, ec);
      const bool sameSize = !inEc && !ec && inSize == outSize;
      const auto inTime = fs::last_write_time(in, inEc);
      const auto outTime = fs::last_write_time(out, ec);
      if (sameSize && !inEc && !ec && outTime >= inTime) {
        progress.filesDone.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
    }
    fs::create_directories(out.parent_path(), ec);
    std::ifstream input(in, std::ios::binary);
    std::ofstream output(temp, std::ios::binary | std::ios::trunc);
    if (!input || !output) {
      error = "Could not " + std::string(!input ? "read " : "write ") + ToUtf8(!input ? in : out);
      fs::remove(temp, ec);
      return false;
    }
    while (input) {
      if (progress.cancel.load(std::memory_order_relaxed)) {
        output.close();
        fs::remove(temp, ec);
        error = "Cancelled.";
        return false;
      }
      input.read(buffer.data(), static_cast< std::streamsize >(buffer.size()));
      const std::streamsize got = input.gcount();
      if (got > 0 && !output.write(buffer.data(), got)) {
        break;
      }
      progress.bytesDone.fetch_add(static_cast< uint64_t >(got), std::memory_order_relaxed);
    }
    output.close();
    if (input.bad() || !output) {
      fs::remove(temp, ec);
      error = "Writing " + ToUtf8(out) + " failed (is the storage full?)";
      return false;
    }
    fs::rename(temp, out, ec);
    if (ec) {
      fs::remove(temp, ec);
      error = "Could not replace " + ToUtf8(out);
      return false;
    }
    progress.filesDone.fetch_add(1, std::memory_order_relaxed);
  }
  std::error_code ec;
  fs::remove(incomplete, ec);
  if (ec) {
    error = "Could not finish the copy in " + to;
    return false;
  }
  return true;
}

void DeleteData(const std::string& dir, const std::function< bool(const std::string&) >& keep) {
  const fs::path base = FromUtf8(dir);
  std::error_code ec;
  for (const std::string& relative : ListFiles(dir)) {
    if (!keep || !keep(relative)) {
      fs::remove(base / FromUtf8(relative), ec);
    }
  }
  // Folders left empty by that, deepest first; a folder that still holds a
  // skipped file stays.
  std::vector< fs::path > dirs;
  fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
  for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const std::string relative = Relative(it->path(), base);
    if (IsSkipped(relative)) {
      if (it->is_directory(ec)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (it->is_directory(ec)) {
      dirs.push_back(it->path());
    }
  }
  for (auto dirIt = dirs.rbegin(); dirIt != dirs.rend(); ++dirIt) {
    if (fs::is_empty(*dirIt, ec)) {
      fs::remove(*dirIt, ec);
    }
  }
}

#if defined(__ANDROID__)
namespace {

// Calls a no-argument method of MetroidPrimeActivity; `boolResult` for "()Z".
bool CallActivity(const char* method, bool boolResult) {
  JNIEnv* env = static_cast< JNIEnv* >(SDL_GetAndroidJNIEnv());
  jobject activity = static_cast< jobject >(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    return false;
  }
  bool result = false;
  jclass cls = env->GetObjectClass(activity);
  if (jmethodID id = env->GetMethodID(cls, method, boolResult ? "()Z" : "()V"); id != nullptr) {
    if (boolResult) {
      result = env->CallBooleanMethod(activity, id) == JNI_TRUE;
    } else {
      env->CallVoidMethod(activity, id);
      result = true;
    }
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    result = false;
  }
  env->DeleteLocalRef(cls);
  env->DeleteLocalRef(activity);
  return result;
}

enum class EState { Idle, ConfirmOverwrite, Copying, Ready, Failed };

struct Move {
  std::mutex mutex;
  EState state = EState::Idle;
  std::string target;
  std::string message;
  bool copied = false;
  bool targetComplete = true;
  // Tells a finished copy thread whether its result is still the one shown.
  uint64_t generation = 0;
  // Set by the copy thread once it has measured the data.
  std::atomic< uint64_t > totalBytes{0};
  std::atomic< uint64_t > totalFiles{0};
  Progress progress;
};

// Where the game keeps its data and how the next launch is told. App storage is SDL's preference
// directory (the activity's files directory); port_paths.txt in it carries the `user=` choice.
std::string AppFolder() {
  char* pref = SDL_GetPrefPath(nullptr, "Metroid Prime 2");
  const std::string folder = pref != nullptr ? WithSeparator(pref) : std::string();
  SDL_free(pref);
  return folder;
}

std::string PrivateUserFolder() {
  const std::string app = AppFolder();
  return app.empty() ? std::string() : app + "user/";
}

std::string MarkerPath() {
  const std::string app = AppFolder();
  return app.empty() ? std::string() : app + "port_paths.txt";
}

// The folder this run reads and writes, with a trailing '/'.
std::string CurrentUserFolder() {
  const std::string backups = PortSave::BackupsDirectory();
  return backups.empty() ? std::string() : WithSeparator(ToUtf8(FromUtf8(backups).parent_path()));
}

Move& State() {
  static Move sMove;
  return sMove;
}

std::string Size(uint64_t bytes) {
  char text[32];
  if (bytes >= (1ull << 30)) {
    std::snprintf(text, sizeof(text), "%.1f GB", static_cast< double >(bytes) / (1ull << 30));
  } else {
    std::snprintf(text, sizeof(text), "%.0f MB", static_cast< double >(bytes) / (1ull << 20));
  }
  return text;
}

void StartCopy(const std::string& target) {
  Move& move = State();
  const std::string from = CurrentUserFolder();
  uint64_t generation = 0;
  {
    std::lock_guard lock(move.mutex);
    move.target = target;
    move.state = EState::Copying;
    move.copied = true;
    move.message.clear();
    move.totalBytes = 0;
    move.totalFiles = 0;
    move.progress.bytesDone = 0;
    move.progress.filesDone = 0;
    move.progress.cancel = false;
    generation = ++move.generation;
  }
  // Measuring walks the whole folder (mods can be thousands of files), so it
  // runs here and not on the render thread.
  std::thread([from, target, generation] {
    Move& move = State();
    const Totals totals = Measure(from);
    move.totalBytes = totals.bytes;
    move.totalFiles = totals.files;
    std::error_code ec;
    fs::create_directories(FromUtf8(target), ec);
    const fs::space_info space = fs::space(FromUtf8(target), ec);
    std::string error;
    bool ok = false;
    if (!ec && space.available < totals.bytes + (64ull << 20)) {
      error = "Not enough free space: the data needs " + Size(totals.bytes) + ", " + Size(space.available) +
              " is free.";
    } else {
      PortLogFile::Write("data folder: copying " + std::to_string(totals.files) + " files (" +
                         std::to_string(totals.bytes) + " bytes) from " + from + " to " + target);
      ok = CopyTree(from, target, move.progress, error);
      PortLogFile::Write(ok ? std::string("data folder: copy done") : "data folder: copy failed: " + error);
    }
    std::lock_guard lock(move.mutex);
    if (move.generation != generation) {
      return;
    }
    move.state = ok ? EState::Ready : EState::Failed;
    move.message = ok ? std::string() : error;
  }).detach();
}

// Points the next launch at the target and restarts into it.
void Switch() {
  Move& move = State();
  std::string target;
  bool copied = false;
  {
    std::lock_guard lock(move.mutex);
    target = move.target;
    copied = move.copied;
  }
  // A second pass for what changed since the copy: the settings, a save made
  // while it ran or after "Not now".
  PortOverlaySettings::SaveSettings();
  if (!copied && !IsComplete(target)) {
    std::lock_guard lock(move.mutex);
    move.state = EState::Failed;
    move.message = target + " holds a copy that did not finish; copy the data again.";
    return;
  }
  if (copied) {
    Progress sync;
    std::string error;
    if (!CopyTree(CurrentUserFolder(), target, sync, error, true)) {
      std::lock_guard lock(move.mutex);
      move.state = EState::Failed;
      move.message = error;
      return;
    }
  }
  const std::string marker = MarkerPath();
  const bool written = !marker.empty() && WriteUserLine(marker, target);
  std::lock_guard lock(move.mutex);
  if (!written) {
    move.state = EState::Failed;
    move.message = "Could not record the new folder in " + marker;
    return;
  }
  PortLogFile::Write("data folder: next launch uses " + target);
  if (!CallActivity("restartApp", false)) {
    move.message = "Close the game and open it again to finish the move.";
  }
}

// Asked for once a second at most while the panel is open: the grant happens
// in the system settings, and coming back from there is not reported.
bool StorageAccess() {
  static uint64_t sLastCheck = 0;
  static bool sAccess = false;
  const uint64_t now = SDL_GetTicks();
  if (sLastCheck == 0 || now - sLastCheck > 1000) {
    sLastCheck = now;
    sAccess = CallActivity("hasStorageAccess", true);
  }
  return sAccess;
}

// A move to shared storage that waits for "All files access": the panel starts
// it once the grant shows up, so coming back from the system settings is enough.
bool sMoveAfterGrant = false;

void RequestMove(const std::string& target, bool needsAccess) {
  Move& move = State();
  sMoveAfterGrant = false;
  if (needsAccess && !StorageAccess()) {
    CallActivity("requestStorageAccess", false);
    sMoveAfterGrant = true;
    std::lock_guard lock(move.mutex);
    move.state = EState::Idle;
    move.message = "Allow \"All files access\" for Metroid Prime 2 and come back; the copy then starts by itself.";
    return;
  }
  if (HasData(target)) {
    const bool complete = IsComplete(target);
    std::lock_guard lock(move.mutex);
    move.target = target;
    move.targetComplete = complete;
    move.message.clear();
    move.state = EState::ConfirmOverwrite;
    return;
  }
  StartCopy(target);
}

} // namespace

void DrawPanel() {
  ImGui::SeparatorText("Data folder");
  Move& move = State();
  const std::string current = CurrentUserFolder();
  const std::string priv = PrivateUserFolder();
  const std::string shared = SharedFolder();
  const bool inPrivate = current == priv;
  ImGui::TextWrapped("Settings, memory card, mods, save states and the texture pack are in %s%s.",
                     current.c_str(), inPrivate ? " (app storage, hidden from file managers)" : "");
  if (const char* env = std::getenv("MP2_USER_UNAVAILABLE"); env != nullptr && env[0] != 0) {
    const std::string lost = env;
    ImGui::TextColored(ImVec4(1.f, 0.6f, 0.2f, 1.f), "%s could not be opened, so app storage is used this time.",
                       lost.c_str());
    ImGui::TextWrapped("Allow \"All files access\" for Metroid Prime 2 and restart, or stay in app storage.");
    if (ImGui::Button("Allow access")) {
      CallActivity("requestStorageAccess", false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stay in app storage")) {
      WriteUserLine(MarkerPath(), priv);
    }
  }

  std::unique_lock lock(move.mutex);
  const EState state = move.state;
  const std::string target = move.target;
  const std::string message = move.message;
  lock.unlock();
  const std::string targetName = target == priv ? "app storage" : target;

  switch (state) {
  case EState::Idle:
  case EState::Failed:
    if (!message.empty()) {
      ImGui::TextWrapped("%s", message.c_str());
    }
    if (inPrivate) {
      if (sMoveAfterGrant && StorageAccess()) {
        RequestMove(shared, true);
      }
      const std::string label = "Move to " + shared;
      if (ImGui::Button(label.c_str())) {
        RequestMove(shared, true);
      }
      ImGui::TextWrapped(
          "Copies the data there and restarts. It stays there after an uninstall, and needs the "
          "\"All files access\" permission.");
    } else {
      if (ImGui::Button("Move back to app storage")) {
        RequestMove(priv, false);
      }
      // Measured once: walking a big mods folder every frame would stutter,
      // and nothing else writes to app storage while the data is elsewhere.
      static bool sConfirmDelete = false;
      static bool sOldMeasured = false;
      static uint64_t sOldBytes = 0;
      if (!sOldMeasured) {
        sOldMeasured = true;
        sOldBytes = Measure(priv).bytes;
      }
      if (sOldBytes > 0) {
        ImGui::TextWrapped("App storage still holds the copy from before the move (%s).", Size(sOldBytes).c_str());
        if (!sConfirmDelete) {
          if (ImGui::Button("Delete the old copy")) {
            sConfirmDelete = true;
          }
        } else {
          if (ImGui::Button("Delete it for good")) {
            // Kept: the disc image when it is the only copy (a move made before
            // the disc was copied, or "Use the data already there"), and the file
            // the settings point at, whatever its name.
            const bool discElsewhere = fs::exists(FromUtf8(current + "disc.iso"));
            const std::string configured = PortOverlaySettings::DiscPath();
            const fs::path privBase = FromUtf8(priv);
            DeleteData(priv, [&](const std::string& rel) {
              if (rel == "disc.iso" && !discElsewhere) {
                return true;
              }
              if (configured.empty()) {
                return false;
              }
              std::error_code same;
              return fs::equivalent(privBase / FromUtf8(rel), FromUtf8(configured), same) && !same;
            });
            sOldMeasured = false;
            sConfirmDelete = false;
          }
          ImGui::SameLine();
          if (ImGui::Button("Keep it")) {
            sConfirmDelete = false;
          }
        }
      }
    }
    break;
  case EState::ConfirmOverwrite:
    if (move.targetComplete) {
      ImGui::TextWrapped("%s already holds game data (from an earlier install or move).", targetName.c_str());
      if (ImGui::Button("Use the data already there")) {
        lock.lock();
        move.copied = false;
        move.state = EState::Ready;
        lock.unlock();
      }
      ImGui::SameLine();
    } else {
      ImGui::TextWrapped("%s holds an earlier copy that did not finish.", targetName.c_str());
    }
    if (ImGui::Button("Copy this data over it")) {
      StartCopy(target);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      lock.lock();
      move.state = EState::Idle;
      lock.unlock();
    }
    break;
  case EState::Copying: {
    const uint64_t totalBytes = move.totalBytes.load();
    const uint64_t totalFiles = move.totalFiles.load();
    if (totalFiles == 0) {
      ImGui::TextWrapped("Counting files...");
    } else {
      const uint64_t done = move.progress.bytesDone.load(std::memory_order_relaxed);
      const float fraction = totalBytes > 0 ? static_cast< float >(done) / totalBytes : 0.f;
      const std::string overlay = Size(done) + " / " + Size(totalBytes);
      ImGui::TextWrapped("Copying to %s... (%" PRIu64 " of %" PRIu64 " files)", targetName.c_str(),
                         move.progress.filesDone.load(std::memory_order_relaxed), totalFiles);
      ImGui::ProgressBar(fraction, ImVec2(-1.f, 0.f), overlay.c_str());
    }
    if (ImGui::Button("Cancel##datafolder")) {
      move.progress.cancel = true;
    }
    break;
  }
  case EState::Ready:
    ImGui::TextWrapped("The data is in %s. The game restarts to switch to it; the original stays until deleted.",
                       targetName.c_str());
    if (!message.empty()) {
      ImGui::TextWrapped("%s", message.c_str());
    }
    if (ImGui::Button("Restart now")) {
      Switch();
    }
    ImGui::SameLine();
    if (ImGui::Button("Not now")) {
      lock.lock();
      move.state = EState::Idle;
      lock.unlock();
    }
    break;
  }
}
#endif

} // namespace PortDataFolder
