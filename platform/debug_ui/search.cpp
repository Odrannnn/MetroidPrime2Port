// F1 overlay: Pages and sub-tabs, and the settings search (index, jumps, results). State and
// helpers shared with the other parts are in debug_ui_internal.h.

#include "port_mobile.h"
#include "debug_ui_internal.h"

#include "port_env.h"
#include "port_strings.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ImGui's item hooks, forwarded by extern/aurora/extern/imgui_item_hook.cpp.
extern void (*gImGuiItemAddHook)(ImGuiContext* ctx, ImGuiID id, const ImRect& rect);
extern void (*gImGuiItemInfoHook)(ImGuiContext* ctx, ImGuiID id, const char* label,
                                  ImGuiItemStatusFlags flags);

namespace PortDebug {

const DebugPage kDebugPages[] = {
    {"Game", DrawGameTab},
    {"Controls", DrawControlsTab},
    {"Video", DrawVideoTab},
    {"Enhancements", DrawEnhancementsTab},
    {"Mods", DrawModsTab},
    // Randomizer page hidden for now (DrawRandomizerTab).
    {"Save states", DrawSaveStatesTab},
    {"System", DrawSystemTab},
    {"Debug", DrawDebugTab},
};
const int kDebugPageCount = static_cast< int >(std::size(kDebugPages));

// MP2_DEBUG_TAB=<page>[/<sub-tab>], case-insensitive. The old page names are kept: Input, Render,
// Performance, Extras and States.
const StartTarget& GetStartTarget() {
  static const StartTarget target = [] {
    StartTarget t;
    const char* env = port::EnvString("MP2_DEBUG_TAB");
    if (env == nullptr || *env == '\0') {
      return t;
    }
    std::string raw = env;
    if (const size_t slash = raw.find('/'); slash != std::string::npos) {
      t.sub = raw.substr(slash + 1);
      raw.resize(slash);
    }
    struct Alias {
      const char* from;
      const char* page;
      const char* sub;
    };
    static const Alias kAliases[] = {
        {"Input", "Controls", ""},
        {"Render", "Video", ""},
        {"Performance", "Video", "Frame rate"},
        {"Extras", "Game", ""},
        {"States", "Save states", ""},
        {"Cheats", "Debug", ""}, // now a section of the Debug page
    };
    t.page = raw;
    for (const Alias& alias : kAliases) {
      if (SDL_strcasecmp(raw.c_str(), alias.from) == 0) {
        t.page = alias.page;
        if (t.sub.empty()) {
          t.sub = alias.sub;
        }
      }
    }
    return t;
  }();
  return target;
}

namespace {
// A settings search result being opened (see JumpTo).
struct SearchJump {
  const SettingsIndexRow* row = nullptr;
  int page = -1;      // kDebugPages index still to select
  bool sub = false;   // the row's sub-tab is still to select
  int occurrence = 0; // which of the items with this label on the page
  int seen = 0;       // items with the label drawn so far this frame
  bool found = false;
  double start = 0.0;            // when the jump began, then when the widget was found
  ImGuiID added = 0;             // the widget, when the item-add hook matched it
  ImGuiWindow* window = nullptr; // where the widget was drawn this frame, to flash it
  ImRect rect;
  bool inPage = false; // inside a page's body, not the page list, tabs or results
};
SearchJump sJump;
} // namespace

int TakeJumpPage() {
  const int page = sJump.page;
  sJump.page = -1;
  // The console's `overlay <page>` (PortOverlay::ShowPage) comes second.
  const int shown = sShowPage.exchange(-1);
  return page >= 0 ? page : shown;
}

bool TakeJumpSub(const char* page, const char* name) {
  if (!sJump.sub || std::strcmp(sJump.row->page, page) != 0 || std::strcmp(sJump.row->sub, name) != 0) {
    return false;
  }
  sJump.sub = false;
  return true;
}

namespace {
// The console's `overlay <page>/<sub-tab>`: set on the console's thread, read by the window's.
std::mutex sShowSubMutex;
std::string sShowSub;
} // namespace

void SetShowSub(std::string sub) {
  std::lock_guard< std::mutex > lock(sShowSubMutex);
  sShowSub = std::move(sub);
}

// True once for the sub-tab the console asked for.
static bool TakeShowSub(const char* name) {
  std::lock_guard< std::mutex > lock(sShowSubMutex);
  if (sShowSub.empty() || SDL_strncasecmp(name, sShowSub.c_str(), sShowSub.size()) != 0) {
    return false;
  }
  sShowSub.clear();
  return true;
}

// A page's inner tab, opened first when MP2_DEBUG_TAB, the console or a search result names it. The caller
// ends the item with EndTabItem when this returns true.
bool SubTab(const char* page, const char* name) {
  static bool pending = !GetStartTarget().sub.empty();
  const StartTarget& target = GetStartTarget();
  const bool start = pending && SDL_strcasecmp(target.page.c_str(), page) == 0 &&
                     SDL_strcasecmp(target.sub.c_str(), name) == 0;
  const bool jump = TakeJumpSub(page, name) || TakeShowSub(name);
  const bool inPage = std::exchange(sJump.inPage, false); // the tab is not a setting
  const bool shown =
      ImGui::BeginTabItem(name, nullptr, start || jump ? ImGuiTabItemFlags_SetSelected : 0);
  sJump.inPage = inPage;
  if (start) {
    pending = false;
  }
  return shown;
}

// A page's settings, where a jump looks for its widget.
void DrawPageBody(const DebugPage& page) {
  sJump.inPage = true;
  page.draw();
  sJump.inPage = false;
}

// The settings search: a query lists the matching rows of the settings index,
// and picking one opens its page, sub-tab and collapsing header, scrolls to the
// widget and flashes it. The widget is found by its label through ImGui's item
// hook, which reports items only while a jump is under way.
constexpr SettingsIndexRow kSettingsIndex[] = {
#include "../port_settings_index.inc"
};

constexpr double kJumpTimeout = 2.0; // gives up on a widget that is never drawn
constexpr double kFlashTime = 1.5;

char sSearch[128] = "";

// The label without its "##id" part is the text, which may go on with a key
// hint ("Screenshot (F12)").
bool LabelIs(const char* label, const char* text) {
  const char* end = ImGui::FindRenderedTextEnd(label);
  const size_t length = std::strlen(text);
  const size_t shown = static_cast< size_t >(end - label);
  return shown >= length && std::memcmp(label, text, length) == 0 &&
         (shown == length || (shown > length + 1 && label[length] == ' ' && label[length + 1] == '('));
}

// Whether `label` is one of the headers in `section`: a header, or headers inside one another
// written outermost first and joined with " > ", as the results list shows them. Each is opened
// as it is drawn, so the next one in appears on the following frame.
bool InSection(const char* section, const char* label) {
  for (const char* part = section; *part != '\0';) {
    const char* end = std::strstr(part, " > ");
    const std::string header = end != nullptr ? std::string(part, end) : std::string(part);
    if (LabelIs(label, header.c_str())) {
      return true;
    }
    if (end == nullptr) {
      break;
    }
    part = end + 3;
  }
  return false;
}

// The row's widget, drawn this frame: the occurrence it wants is scrolled to
// and focused once, then flashed until the flash fades.
void SearchItemFound(ImGuiContext& g, ImGuiID id, const ImRect& rect) {
  SearchJump& jump = sJump;
  if (jump.seen++ != jump.occurrence) {
    return;
  }
  ImGuiWindow* window = g.CurrentWindow;
  if (!jump.found) {
    jump.found = true;
    jump.start = g.Time;
    ImGui::ScrollToRectEx(window, rect, ImGuiScrollFlags_AlwaysCenterY);
    ImGui::SetFocusID(id, window); // so a pad carries on from it
  }
  jump.window = window; // flashed by EndSearchJump, over the widget
  jump.rect = rect;
}

// Combos report no label, so an item whose id is the row's label is its widget
// too. The label hook then skips it.
void SearchItemAddHook(ImGuiContext* ctx, ImGuiID id, const ImRect& rect) {
  SearchJump& jump = sJump;
  ImGuiWindow* window = ctx->CurrentWindow;
  // Begin reports a window while its id stack is still empty.
  if (jump.row == nullptr || !jump.inPage || id == 0 || window == nullptr || window->IDStack.Size == 0 ||
      id != window->GetID(jump.row->label)) {
    return;
  }
  jump.added = id;
  SearchItemFound(*ctx, id, rect);
}

void SearchItemHook(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
  SearchJump& jump = sJump;
  if (jump.row == nullptr || !jump.inPage || label == nullptr || id == 0 || id == jump.added) {
    return;
  }
  ImGuiContext& g = *ctx;
  const SettingsIndexRow& row = *jump.row;
  if ((flags & ImGuiItemStatusFlags_Openable) != 0 && *row.section != '\0' && InSection(row.section, label)) {
    // A header the widget sits under, opened on the next frame if closed.
    if ((flags & ImGuiItemStatusFlags_Opened) == 0) {
      g.CurrentWindow->DC.StateStorage->SetInt(id, 1);
    }
  } else if (LabelIs(label, row.label)) {
    SearchItemFound(g, id, g.LastItemData.Rect);
  }
}

void JumpTo(const SettingsIndexRow& row) {
  sJump = SearchJump{};
  for (int i = 0; i < kDebugPageCount; ++i) {
    if (std::strcmp(kDebugPages[i].name, row.page) == 0) {
      sJump.page = i;
    }
  }
  sJump.row = &row;
  sJump.sub = *row.sub != '\0';
  // Rows with the same label on one page are in draw order.
  for (const SettingsIndexRow& other : kSettingsIndex) {
    if (&other == &row) {
      break;
    }
    if (std::strcmp(other.page, row.page) == 0 && std::strcmp(other.sub, row.sub) == 0 &&
        std::strcmp(other.label, row.label) == 0) {
      ++sJump.occurrence;
    }
  }
  sJump.start = ImGui::GetTime();
  sSearch[0] = '\0';
}

// Around the overlay window only: while a jump looks for its widget, items are
// reported even when scrolled out of view.
// MP2_DEBUG_SEARCH_AUDIT=1 opens every index row in turn with the overlay up and
// logs whether its widget was found, to check the index against the pages.
void AuditSearchIndex(const SearchJump& jump, double now) {
  static int next = port::EnvFlag("MP2_DEBUG_SEARCH_AUDIT") ? 0 : -1;
  static int missed = 0;
  if (next < 0 || (jump.row != nullptr && !jump.found && now - jump.start <= kJumpTimeout)) {
    return;
  }
  if (jump.row != nullptr) {
    missed += jump.found ? 0 : 1;
    std::fprintf(stderr, "[overlay] search audit: %s: %s > %s > %s > %s\n",
                   jump.found ? "found" : "MISSED", jump.row->page, jump.row->sub, jump.row->section,
                   jump.row->label);
  }
  if (next == static_cast< int >(std::size(kSettingsIndex))) {
    std::fprintf(stderr, "[overlay] search audit: done, %d of %d missed\n", missed, next);
    next = -1;
    sJump = SearchJump{};
    return;
  }
  JumpTo(kSettingsIndex[next++]);
}

void BeginSearchJump() {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  SearchJump& jump = sJump;
  AuditSearchIndex(jump, g.Time);
  if (jump.row != nullptr &&
      g.Time - jump.start > (jump.found ? kFlashTime : kJumpTimeout)) {
    jump = SearchJump{};
  }
  const bool active = jump.row != nullptr;
  jump.seen = 0;
  jump.window = nullptr;
  gImGuiItemAddHook = active ? SearchItemAddHook : nullptr;
  gImGuiItemInfoHook = active ? SearchItemHook : nullptr;
  g.TestEngineHookItems = active;
  g.ItemUnclipByLog = active && !jump.found;
}

void EndSearchJump() {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  const SearchJump& jump = sJump;
  const float fade = 1.f - static_cast< float >((g.Time - jump.start) / kFlashTime);
  if (jump.found && jump.window != nullptr && fade > 0.f) {
    const ImRect& rect = jump.rect;
    const float padX = g.Style.FramePadding.x * 0.5f;
    const float padY = g.Style.FramePadding.y * 0.5f;
    ImDrawList* draw = jump.window->DrawList;
    draw->PushClipRect(jump.window->InnerClipRect.Min, jump.window->InnerClipRect.Max);
    draw->AddRectFilled(ImVec2(rect.Min.x - padX, rect.Min.y - padY), ImVec2(rect.Max.x + padX, rect.Max.y + padY),
                        ImGui::GetColorU32(ImGuiCol_NavCursor, 0.45f * fade), g.Style.FrameRounding);
    draw->PopClipRect();
  }
  gImGuiItemAddHook = nullptr;
  gImGuiItemInfoHook = nullptr;
  g.TestEngineHookItems = false;
  g.ItemUnclipByLog = false;
}

void DrawSearchField() {
  // MP2_DEBUG_SEARCH=<query> opens with it typed, for captures of the overlay.
  static bool sPrefill = true;
  if (sPrefill) {
    sPrefill = false;
    if (const char* query = port::EnvString("MP2_DEBUG_SEARCH")) {
      SDL_strlcpy(sSearch, query, sizeof(sSearch));
    }
  }
  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F)) {
    ImGui::SetKeyboardFocusHere();
  }
#if defined(PORT_MOBILE)
  const char* const kHint = "Search settings";
#else
  const char* const kHint = "Search settings (Ctrl+F)";
#endif
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##settings-search", kHint, sSearch, sizeof(sSearch),
                           ImGuiInputTextFlags_EscapeClearsAll);
}

bool Searching() { return sSearch[0] != '\0'; }

// Every word of the query must be in the row's label, place or keywords. Rows
// whose label starts with the query come first, then those that contain it,
// then those whose label has every word.
void DrawSearchResults() {
  static std::string sQuery;
  static std::vector< const SettingsIndexRow* > sHits;
  const std::string query = port::Lower(port::Trim(sSearch));
  if (query != sQuery) {
    sQuery = query;
    sHits.clear();
    std::vector< std::string > words;
    std::istringstream stream(query);
    for (std::string word; stream >> word;) {
      words.push_back(word);
    }
    std::vector< std::pair< int, const SettingsIndexRow* > > scored;
    for (const SettingsIndexRow& row : kSettingsIndex) {
      const std::string label = port::Lower(row.label);
      const std::string all = label + ' ' + port::Lower(row.section) + ' ' + port::Lower(row.sub) + ' ' +
                              port::Lower(row.page) + ' ' + port::Lower(row.keywords);
      bool match = true;
      bool inLabel = true;
      for (const std::string& word : words) {
        match = match && all.find(word) != std::string::npos;
        inLabel = inLabel && label.find(word) != std::string::npos;
      }
      if (!match) {
        continue;
      }
      const int score = label.rfind(query, 0) == 0                ? 0
                        : label.find(query) != std::string::npos ? 1
                        : inLabel                                ? 2
                                                                 : 3;
      scored.emplace_back(score, &row);
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& hit : scored) {
      sHits.push_back(hit.second);
    }
  }

  if (sHits.empty()) {
    ImGui::TextDisabled("No settings match \"%s\".", sSearch);
    return;
  }
  const ImGuiStyle& style = ImGui::GetStyle();
  const float rowHeight = ImGui::GetTextLineHeight() * 2.f + style.ItemSpacing.y;
  for (size_t i = 0; i < sHits.size(); ++i) {
    const SettingsIndexRow& row = *sHits[i];
    std::string place = row.page;
    for (const char* part : {row.sub, row.section}) {
      if (*part != '\0') {
        place += std::string(" > ") + part;
      }
    }
    ImGui::PushID(static_cast< int >(i));
    if (ImGui::Selectable("##hit", false, ImGuiSelectableFlags_None, ImVec2(0.f, rowHeight))) {
      JumpTo(row);
    }
    const ImVec2 min = ImGui::GetItemRectMin();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(min, ImGui::GetItemRectMax(), true); // long labels stop at the row
    draw->AddText(min, ImGui::GetColorU32(ImGuiCol_Text), row.label);
    draw->AddText(ImVec2(min.x, min.y + ImGui::GetTextLineHeight()),
                  ImGui::GetColorU32(ImGuiCol_TextDisabled), place.c_str());
    draw->PopClipRect();
    ImGui::PopID();
  }
}

// The innermost window under the finger that can actually scroll vertically,
// climbing out of child windows (a table, the page list) that cannot.
ImGuiWindow* ScrollableWindowAt(ImGuiWindow* window) {
  for (; window != nullptr; window = window->ParentWindow) {
    if (window->ScrollMax.y > 0.f && (window->Flags & ImGuiWindowFlags_NoScrollWithMouse) == 0) {
      return window;
    }
    if ((window->Flags & ImGuiWindowFlags_ChildWindow) == 0) {
      break;
    }
  }
  return nullptr;
}

} // namespace PortDebug
