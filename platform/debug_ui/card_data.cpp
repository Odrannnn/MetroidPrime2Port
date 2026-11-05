// F1 overlay: System > Memory card import/export and System > Data folder. The Prime 1 port's
// debug_ui/file_dialogs.cpp card section and data folder panel, on MP2's own card tools
// (PortSave: one 01-G2ME-*.gci, or 01-G2MP-*.gci on PAL, per card, no region conversion), including
// MP1's "Export as" Wii Trilogy (save.bin) choice and the Trilogy mirror; MP1's raw-image Dolphin
// cards have no counterpart here. The file dialogs answer on another thread, so a pick waits in
// sPicks until the next frame.
//
// Android has no folder picker (SDL3's SDL_ShowOpenFolderDialog fails there), so "Export..." asks
// for one file at a time: the card's files are queued and SDL_ShowSaveFileDialog opens once per
// file with its name suggested (the Prime 1 port's approach). The Trilogy is a source save.bin
// picked with the open dialog, then the result saved through the save dialog. The content:// URI a
// save dialog returns is written with SDL_IOFromFile, which handles it.
#include "port_utf8_path.h"
#include "port_mobile.h"
#include "debug_ui_internal.h"

#include "port_data_folder.h"
#include "port_overlay.h"
#include "port_entry.h"
#include "port_save.h"
#include "MetroidPrime/Player/CGameOptions.hpp"

#include <SDL3/SDL.h>
#if defined(__ANDROID__)
#include <jni.h>
#endif
#include <SDL3/SDL_timer.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace PortDebug {
namespace {

namespace fs = std::filesystem;

enum Pick { kPick_Import, kPick_Export, kPick_Emulator, kPick_Trilogy, kPick_ExportFile };
// The overlay's "Export as" choice: 0 the card's files as they are, 1 the Wii Trilogy's save.bin.
int sExportChoice = 0;
std::mutex sPickMutex;
std::vector< std::pair< Pick, std::string > > sPicks;
bool sDialogOpen = false;

// A game is running: the card must not change under it. The title and file select screens are a
// world of their own (the front end's), but their save screen re-reads the card after an import.
bool InGame() { return sWorldId != 0 && !CGameOptions::IsFrontEndActive(); }

std::string sStatus;
#if defined(__ANDROID__)
// Files still to be saved one by one (main thread only).
std::vector< PortSave::ExportFile > sExportQueue;
#endif

SDL_Window* FirstWindow() {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  SDL_Window* window = windows != nullptr && count > 0 ? windows[0] : nullptr;
  SDL_free(windows);
  return window;
}

#if defined(__ANDROID__)
// SDL's Android save picker ignores its initial-location argument, so the suggested name goes to
// SDLActivity.setNextSaveDialogSuggestedFilename first.
void SetNextAndroidSaveDialogFilename(const char* filename) {
  JNIEnv* env = static_cast< JNIEnv* >(SDL_GetAndroidJNIEnv());
  jobject activity = env != nullptr ? static_cast< jobject >(SDL_GetAndroidActivity()) : nullptr;
  if (activity == nullptr) {
    return;
  }
  jclass cls = env->GetObjectClass(activity);
  jmethodID method = nullptr;
  if (cls != nullptr && !env->ExceptionCheck()) {
    method = env->GetStaticMethodID(cls, "setNextSaveDialogSuggestedFilename", "(Ljava/lang/String;)V");
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    method = nullptr;
  }
  jstring name = method != nullptr ? env->NewStringUTF(filename != nullptr ? filename : "") : nullptr;
  if (name != nullptr) {
    env->CallStaticVoidMethod(cls, method, name);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
    }
    env->DeleteLocalRef(name);
  }
  if (cls != nullptr) {
    env->DeleteLocalRef(cls);
  }
  env->DeleteLocalRef(activity);
}
#endif

// A failed dialog (files == nullptr) is queued as an empty path with SDL's error in sPickError, so
// it shows in sStatus instead of vanishing; a cancel (files[0] == nullptr) is an empty path alone.
std::string sPickError;

void OnPick(void* userdata, const char* const* files, int) {
  std::lock_guard< std::mutex > lock(sPickMutex);
  const Pick pick = static_cast< Pick >(reinterpret_cast< intptr_t >(userdata));
  if (files != nullptr && files[0] != nullptr) {
    sPicks.emplace_back(pick, files[0]);
  } else {
    if (files == nullptr) {
      const char* error = SDL_GetError();
      sPickError = error != nullptr && error[0] != '\0' ? error : "the system dialog failed";
    }
    sPicks.emplace_back(pick, std::string());
  }
  sDialogOpen = false;
}

void OpenDialog(Pick pick) {
  static const SDL_DialogFileFilter filters[] = {{"Save files (.gci)", "gci"}, {"All files", "*"}};
  sDialogOpen = true;
  void* userdata = reinterpret_cast< void* >(static_cast< intptr_t >(pick));
  if (pick == kPick_Import) {
    SDL_ShowOpenFileDialog(OnPick, userdata, FirstWindow(), filters, 2, nullptr, false);
  } else if (pick == kPick_Trilogy) {
    static const SDL_DialogFileFilter bin[] = {{"Trilogy save (save.bin)", "bin"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(OnPick, userdata, FirstWindow(), bin, 2, nullptr, false);
  } else if (pick == kPick_ExportFile) {
#if defined(__ANDROID__)
    static std::string name;
    name = sExportQueue.empty() ? std::string() : sExportQueue.front().name;
    SetNextAndroidSaveDialogFilename(name.c_str());
    SDL_ShowSaveFileDialog(OnPick, userdata, FirstWindow(), nullptr, 0, name.empty() ? nullptr : name.c_str());
#else
    sDialogOpen = false;
#endif
  } else {
    // A folder: the card's files keep their names, so they import back as they are; the emulator
    // pick is its user folder.
    SDL_ShowOpenFolderDialog(OnPick, userdata, FirstWindow(), nullptr, false);
  }
}

// Dolphin's region folder for the mounted disc's card, as Aurora picks its own.
const char* DolphinRegion() {
  return port::entry::IsPalRegion(port::entry::CurrentDiscVersion()) ? "EUR" : "USA";
}

// Dolphin's (or PrimeHack's) GCI-folder card A for the mounted disc's release, where this machine
// has one: the chosen emulator folder, else the usual user folders (port_save.cpp).
fs::path DolphinCardFolder() { return PortUtf8::FromUtf8(PortSave::EmulatorCardFolder()); }

// Dolphin's card files for the mounted release, newest last: 01-G2ME-*.gci (01-G2MP-*.gci on PAL).
std::vector< fs::path > DolphinSaves(const fs::path& card) {
  std::vector< fs::path > out;
  const std::string prefix = std::string("01-") + port::entry::CurrentGameCode() + "-";
  std::error_code ec;
  for (const fs::directory_entry& entry : fs::directory_iterator(card, ec)) {
    const std::string name = PortUtf8::ToUtf8(entry.path().filename());
    if (entry.is_regular_file(ec) && name.rfind(prefix, 0) == 0 && entry.path().extension() == ".gci") {
      out.push_back(entry.path());
    }
  }
  std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return fs::last_write_time(a, ec) < fs::last_write_time(b, ec);
  });
  return out;
}

[[maybe_unused]] void OpenFolder(const std::string& path) {
  // "Card A" has a space, which a URL can't carry as is.
  std::string url = !path.empty() && path.front() == '/' ? "file://" : "file:///";
  for (const char c : path) {
    url += c == ' ' ? std::string("%20") : std::string(1, c == '\\' ? '/' : c);
  }
  SDL_OpenURL(url.c_str());
}

void TakePicks() {
  std::vector< std::pair< Pick, std::string > > picks;
  {
    std::lock_guard< std::mutex > lock(sPickMutex);
    picks.swap(sPicks);
  }
  for (const auto& [pick, path] : picks) {
    std::string message;
    if (path.empty()) {
      // Cancelled or failed; the rest of an Android export queue goes with it.
#if defined(__ANDROID__)
      sExportQueue.clear();
#endif
      std::lock_guard< std::mutex > lock(sPickMutex);
      if (!sPickError.empty()) {
        sStatus = "The file dialog failed: " + sPickError;
        sPickError.clear();
      }
      continue;
    }
#if defined(__ANDROID__)
    if (pick == kPick_Trilogy) {
      size_t size = 0;
      void* data = SDL_LoadFile(path.c_str(), &size);
      if (data == nullptr) {
        sStatus = std::string("Export failed: could not read the save.bin: ") + SDL_GetError();
        continue;
      }
      const uint8_t* raw = static_cast< const uint8_t* >(data);
      const std::vector< uint8_t > base(raw, raw + size);
      SDL_free(data);
      if (PortSave::ExportBytes(true, base, sExportQueue, message)) {
        OpenDialog(kPick_ExportFile);
      } else {
        sStatus = "Export failed: " + message;
      }
      continue;
    }
    if (pick == kPick_ExportFile) {
      if (!sExportQueue.empty()) {
        const PortSave::ExportFile& file = sExportQueue.front();
        SDL_IOStream* out = SDL_IOFromFile(path.c_str(), "wb");
        bool ok = out != nullptr && (file.bytes.empty() || SDL_WriteIO(out, file.bytes.data(), file.bytes.size()) == file.bytes.size());
        if (out != nullptr) {
          ok = SDL_CloseIO(out) && ok;
        }
        sStatus = ok ? "Exported: " + file.name + (file.note.empty() ? "" : " (" + file.note + ")")
                     : "Export failed: could not save " + file.name + ": " + SDL_GetError();
        sExportQueue.erase(sExportQueue.begin());
        if (ok && !sExportQueue.empty()) {
          OpenDialog(kPick_ExportFile);
        } else {
          sExportQueue.clear();
        }
      }
      continue;
    }
#endif
    if (pick == kPick_Emulator) {
      PortSave::SetEmulatorFolder(path);
      PortOverlaySettings::MarkDirty();
      sStatus = "Emulator folder: " + PortSave::EmulatorFolder();
    } else if (pick == kPick_Trilogy) {
      const bool ok = PortSave::ExportTrilogy(path, message);
      sStatus = ok ? "Exported: " + message : "Export failed: " + message;
    } else if (pick == kPick_Import) {
      const bool ok = PortSave::Import(path, message);
      sStatus = ok ? "Imported: " + message : "Import failed: " + message;
    } else {
      const bool ok = PortSave::Export(path, message);
      sStatus = ok ? "Exported: " + message : "Export failed: " + message;
    }
  }
}

void PathRow(const char* label, const std::string& path) {
  ImGui::TextWrapped("%s: %s", label, path.empty() ? "(not set)" : path.c_str());
}

// MP1's emulator card block (file_dialogs.cpp DrawEmulatorCard): the emulator folder, the installed
// emulators, and sharing its card. Sharing mounts <folder>/GC as the card base at launch
// (CMemoryCardSys::Initialize), so it takes effect on the next start.
void DrawEmulatorCard() {
#if !defined(PORT_MOBILE)
  const bool inGame = InGame();
  const std::string chosen = PortSave::EmulatorFolder();
  std::string found = "none found";
  if (chosen.empty()) {
    const fs::path card = DolphinCardFolder();
    if (!card.empty()) {
      found = PortUtf8::ToUtf8(card.parent_path().parent_path().parent_path());
    }
  }
  ImGui::TextUnformatted("Emulator folder");
  ImGui::SameLine(0.0f, 0.0f);
  ImGui::TextWrapped(": %s", chosen.empty() ? ("Auto (" + found + ")").c_str() : chosen.c_str());
  ImGui::BeginDisabled(sDialogOpen);
  if (ImGui::Button("Choose...")) {
    OpenDialog(kPick_Emulator);
  }
  ImGui::EndDisabled();
  ItemHelp("The Dolphin or PrimeHack user folder (the one containing GC/). Only needed for "
           "portable installs.");
  ImGui::SameLine();
  ImGui::BeginDisabled(chosen.empty());
  if (ImGui::Button("Auto")) {
    PortSave::SetEmulatorFolder("");
    PortOverlaySettings::MarkDirty();
  }
  ImGui::EndDisabled();
  // With Dolphin and PrimeHack both installed, Auto takes the first; pick the other here.
  const std::vector< std::string > detected = PortSave::DetectedEmulatorFolders();
  if (!detected.empty()) {
    const std::string preview = chosen.empty() ? "Auto" : chosen;
    if (ImGui::BeginCombo("Installed emulators", preview.c_str())) {
      for (const std::string& root : detected) {
        const std::string label =
            (root.find("primehack") != std::string::npos ? "PrimeHack: " : "Dolphin: ") + root;
        if (ImGui::Selectable(label.c_str(), root == chosen)) {
          PortSave::SetEmulatorFolder(root);
          PortOverlaySettings::MarkDirty();
        }
      }
      ImGui::EndCombo();
    }
    ItemHelp("Installs found on this computer. Pick one instead of the automatic choice.");
  }
  bool share = PortSave::ShareEmulatorCard();
  ImGui::BeginDisabled(inGame);
  if (ImGui::Checkbox("Share memory card with emulator", &share)) {
    PortSave::SetShareEmulatorCard(share);
    PortOverlaySettings::MarkDirty();
  }
  ImGui::EndDisabled();
  ItemHelp("The port and the emulator share the same saves: never run both at once. Takes effect "
           "at once in the front end; in game the card never changes, so it waits for the front end.");
  if (!share) {
    return;
  }
  bool mirror = PortSave::MirrorTrilogy();
  if (ImGui::Checkbox("Mirror with the Trilogy save.bin", &mirror)) {
    PortSave::SetMirrorTrilogy(mirror);
    PortOverlaySettings::MarkDirty();
  }
  ItemHelp("Keeps the card and Dolphin's Metroid Prime Trilogy save.bin (Wii/title/00010000/"
           "52334d45/data/save.bin) in step: a save made here goes out once written, and a newer "
           "one made in the Trilogy comes in at the front end. The Trilogy file must have been "
           "started once.");
  if (mirror) {
    const std::string bin = PortSave::FindEmulatorTrilogy();
    if (bin.empty()) {
      ImGui::TextColored(ThemeWarnColor(), "No Trilogy save.bin found in the emulator folder.");
    } else {
      ImGui::TextWrapped("Mirroring: %s", bin.c_str());
    }
  }
  std::string why;
  const std::string base = PortSave::SharedCardBase(&why);
  if (base.empty()) {
    ImGui::TextColored(ThemeWarnColor(), "%s", why.c_str());
    return;
  }
  const std::string active = PortSave::Directory();
  const fs::path shared = PortUtf8::FromUtf8(base) / DolphinRegion() / "Card A";
  std::error_code ec;
  if (!active.empty() && fs::equivalent(PortUtf8::FromUtf8(active), shared, ec)) {
    ImGui::TextWrapped("Sharing: %s", PortUtf8::ToUtf8(shared).c_str());
    return;
  }
  ImGui::TextWrapped("Will share %s once the front end is shown.", PortUtf8::ToUtf8(shared).c_str());
  ImGui::BeginDisabled(inGame || sDialogOpen || active.empty());
  if (ImGui::Button("Copy port saves into shared card")) {
    std::string message;
    const bool ok = PortSave::Export(PortUtf8::ToUtf8(shared), message);
    sStatus = ok ? "Copied: " + message : "Copy failed: " + message;
  }
  ImGui::EndDisabled();
  ItemHelp("Copies the port's current card saves into the shared card; a file of the same name "
           "there gets a numbered copy, not replaced.");
#endif
}

} // namespace

void DrawCardTransfer() {
  TakePicks();
  DrawEmulatorCard();
  const std::string folder = PortSave::Directory();
  const size_t saves = PortSave::Files().size();
  if (PortSave::IsImage() || folder.empty()) {
    ImGui::TextDisabled("The card is not a GCI folder (yet); import and export need one.");
    return;
  }
  ImGui::TextWrapped("Card: %s (%zu save file%s)", folder.c_str(), saves, saves == 1 ? "" : "s");
  const bool inGame = InGame();
  ImGui::BeginDisabled(inGame || sDialogOpen);
  if (ImGui::Button("Import file...")) {
    OpenDialog(kPick_Import);
  }
  ItemHelp("Loads a .gci save of this release. The card's current save is copied to save-backups "
           "first.");
  const fs::path dolphin = DolphinCardFolder();
  ImGui::SameLine();
  ImGui::BeginDisabled(dolphin.empty());
  if (ImGui::Button("Import from Dolphin/PrimeHack")) {
    const std::vector< fs::path > saved = DolphinSaves(dolphin);
    std::string message;
    if (saved.empty()) {
      sStatus = "Dolphin's card has no Metroid Prime 2 save.";
    } else {
      const bool ok = PortSave::Import(PortUtf8::ToUtf8(saved.back()), message);
      sStatus = ok ? "Imported: " + message : "Import failed: " + message;
    }
  }
  ImGui::EndDisabled();
  ItemHelp(dolphin.empty() ? (std::string("No Dolphin card folder (GC/") + DolphinRegion() +
                            "/Card A) was found in the usual user folders.").c_str()
                         : (std::string("Imports the newest save in Dolphin's ") + DolphinRegion() +
                            " card folder.").c_str());
  // Saves another release left on the port's card (each release keeps its own files). Rescanned
  // every two seconds: it reads every file's header.
  static std::vector< PortSave::OtherRelease > sOthers;
  static Uint64 sOthersNext = 0;
  if (SDL_GetTicks() >= sOthersNext) {
    sOthers = PortSave::OtherReleaseSaves();
    sOthersNext = SDL_GetTicks() + 2000;
  }
  for (size_t i = 0; i < sOthers.size(); ++i) {
    ImGui::PushID(static_cast< int >(i));
    if (ImGui::Button(("Restore from " + sOthers[i].label).c_str())) {
      std::string message;
      const bool ok = PortSave::RestoreFrom(sOthers[i], message);
      sStatus = ok ? "Restored: " + message : "Restore failed: " + message;
      sOthersNext = 0;
    }
    ItemHelp("Copies that release's newest save onto this card, converted; this card's save is "
             "copied to save-backups first, and the other release's file is left as it is.");
    ImGui::PopID();
  }
  ImGui::EndDisabled();
  ImGui::BeginDisabled(sDialogOpen || saves == 0);
  const char* choices[] = {"As on the card (.gci)", "Wii Trilogy (save.bin)"};
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
  ImGui::Combo("Export as", &sExportChoice, choices, IM_ARRAYSIZE(choices));
  ItemHelp("The format to export in. For the Trilogy, start Metroid Prime 2 in an existing save.bin "
           "and save once first: that file's Echoes game is replaced. A Wii's signed data.bin "
           "can't be made here.");
  const bool trilogy = sExportChoice == 1;
#if defined(__ANDROID__)
  if (ImGui::Button(trilogy ? "Export into save.bin..." : "Export...")) {
    std::string message;
    if (trilogy) {
      OpenDialog(kPick_Trilogy);
    } else if (PortSave::ExportBytes(false, {}, sExportQueue, message)) {
      OpenDialog(kPick_ExportFile);
    } else {
      sStatus = "Export failed: " + message;
    }
  }
  ItemHelp(trilogy ? "Pick the Trilogy save.bin to start from, then choose where to save the updated "
                     "copy; the file you picked is left as it is."
                   : "Saves the card's save file (one at a time if there are several) wherever you "
                     "choose, with its own name so it can be imported again.");
#else
  if (ImGui::Button(trilogy ? "Export into save.bin..." : "Export to folder...")) {
    OpenDialog(trilogy ? kPick_Trilogy : kPick_Export);
  }
  ItemHelp(trilogy ? "Pick the Trilogy save.bin to write into; the old one is kept as save.bin.bak."
                   : "Copies the card's save files into the folder you pick, with their own names so "
                     "they can be imported again.");
  ImGui::SameLine();
  const std::string trilogyBin = trilogy ? PortSave::FindEmulatorTrilogy() : std::string();
  ImGui::BeginDisabled(trilogy ? trilogyBin.empty() : dolphin.empty());
  if (ImGui::Button("Export to Dolphin/PrimeHack")) {
    std::string message;
    const bool ok = trilogy ? PortSave::ExportTrilogy(trilogyBin, message)
                            : PortSave::Export(PortUtf8::ToUtf8(dolphin), message);
    sStatus = ok ? "Exported: " + message : "Export failed: " + message;
  }
  ImGui::EndDisabled();
  ItemHelp(trilogy ? "Close the emulator first. Goes to its Trilogy save (Wii/title/00010000/52334d45/"
                     "data/save.bin); the old one is kept as save.bin.bak."
                   : "Close the emulator first. Writes the save into its card folder for this release; a file "
                     "of the same name there gets a numbered copy, not replaced.");
#endif
  ImGui::EndDisabled();
#if !defined(__ANDROID__)
  ImGui::SameLine();
  if (ImGui::Button("Open card folder")) {
    OpenFolder(folder);
  }
#endif
  if (inGame) {
    ImGui::TextColored(ThemeWarnColor(), "Return to the title screen to import.");
  }
  if (!sStatus.empty()) {
    ImGui::TextWrapped("%s", sStatus.c_str());
  }
}

// Where everything is; on Android also the move to shared storage (port_data_folder.cpp), on the
// desktop a way to open the folder.
void DrawDataFolder() {
  ImGui::SeparatorText("Data folder");
  const std::string backups = PortSave::BackupsDirectory();
  const std::string user =
      backups.empty() ? std::string() : PortUtf8::ToUtf8(PortUtf8::FromUtf8(backups).parent_path());
  PathRow("User folder", user);
  PathRow("Memory card", PortSave::Directory());
  PathRow("Save backups", backups);
#if defined(__ANDROID__)
  PortDataFolder::DrawPanel();
#else
  if (!user.empty() && ImGui::Button("Open data folder")) {
    OpenFolder(user);
  }
#endif
  ItemHelp("Settings, memory card, save backups, save states and mods live in the user folder "
           "(`MP2_USER_PATH` overrides it).");
}

} // namespace PortDebug
