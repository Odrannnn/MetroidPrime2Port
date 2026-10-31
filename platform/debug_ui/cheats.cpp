// F1 overlay: The Cheats page, the Prime 1 port's debug_ui/cheats.cpp for this game: items,
// health, teleport and the room's layers. State and helpers shared with the other parts are in
// debug_ui_internal.h.

#include "port_mobile.h"
#include "debug_ui_internal.h"

#include "port_overlay.h"
#include "port_collision_view.h"
#include "port_savestate.h"
#include "port_warp.h"

#include <aurora/gfx.h>

#include "Kyoto/Text/CStringTable.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CMemoryCard.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/ScriptObjects/CScriptConditionalRelay.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace PortDebug {

namespace {

// The Prime 1 port's Cheats page (debug_ui/cheats.cpp) for this game: the item list is Echoes'
// own and the grant path is the console's `give`/`take` (platform/port_console.cpp, which is
// CScriptPickup's own path): capacity and amount move together, and the step is a delta, so a
// slider sets a value rather than adding one. Zero removes the item, which is all it takes -
// every ability check here reads the capacity (HasPowerUp is `mCapacity > 0`).
void GrantItem(CPlayerState& ps, CPlayerState::EItemType type, int capacity, int amount) {
  // The Prime 1 port's GrantItem in this game's terms: `capacity` is what MP1 calls the power-up
  // and `amount` what it calls the pickup, and the two fields are named that way here. The
  // capacity is moved by the difference, because that is how AddPowerUp takes an expansion, and
  // the amount is then set outright - sliding a counter down must lower the amount even when the
  // capacity has room for it (which AddPowerUp alone would not do).
  ps.AddPowerUp(type, capacity - ps.GetItemCapacity(type));
  ps.SetItemAmount(type, amount);
  // A translator gate reads the inventory once, when its area loads; this has it read again.
  CScriptConditionalRelay::PortNoteInventoryCheat();
}

// A slider takes the value under the finger the moment it is pressed, before
// UpdateTouchScroll can tell a scroll from a drag, so scrolling the page past
// these sliders filled Samus's ammo and tanks. The value is held back until
// release and dropped when the drag became a scroll.
bool CheatSlider(const char* label, int& value, int max) {
  static ImGuiID sEditing = 0;
  static int sPending = 0;
  const ImGuiID id = ImGui::GetID(label);
  int shown = sEditing == id ? sPending : value;
  ImGui::SliderInt(label, &shown, 0, max);
  if (ImGui::IsItemActive()) {
    sEditing = id;
    sPending = shown;
    return false;
  }
  if (sEditing != id) {
    return false;
  }
  sEditing = 0;
  if (TouchScrolling() || sPending == value) {
    return false;
  }
  value = sPending;
  return true;
}

struct SItemCheck {
  const char* name;
  CPlayerState::EItemType type;
};

constexpr SItemCheck kAbilityItems[] = {
    {"Power Beam", CPlayerState::kIT_PowerBeam},
    {"Dark Beam", CPlayerState::kIT_DarkBeam},
    {"Light Beam", CPlayerState::kIT_LightBeam},
    {"Annihilator Beam", CPlayerState::kIT_AnnihilatorBeam},
    {"Charge Beam", CPlayerState::kIT_ChargeBeam},
    {"Super Missile", CPlayerState::kIT_SuperMissile},
    {"Darkburst", CPlayerState::kIT_Darkburst},
    {"Sunburst", CPlayerState::kIT_Sunburst},
    {"Sonic Boom", CPlayerState::kIT_SonicBoom},
    {"Combat Visor", CPlayerState::kIT_CombatVisor},
    {"Scan Visor", CPlayerState::kIT_ScanVisor},
    {"Dark Visor", CPlayerState::kIT_DarkVisor},
    {"Echo Visor", CPlayerState::kIT_EchoVisor},
    {"Varia Suit", CPlayerState::kIT_VariaSuit},
    {"Dark Suit", CPlayerState::kIT_DarkSuit},
    {"Light Suit", CPlayerState::kIT_LightSuit},
    {"Morph Ball", CPlayerState::kIT_MorphBall},
    {"Boost Ball", CPlayerState::kIT_BoostBall},
    {"Spider Ball", CPlayerState::kIT_SpiderBall},
    {"Morph Ball Bombs", CPlayerState::kIT_MorphBallBombs},
    {"Grapple Beam", CPlayerState::kIT_GrappleBeam},
    {"Space Jump Boots", CPlayerState::kIT_SpaceJumpBoots},
    {"Gravity Boost", CPlayerState::kIT_GravityBoost},
    {"Seeker Launcher", CPlayerState::kIT_SeekerLauncher},
    {"Screw Attack", CPlayerState::kIT_ScrewAttack},
    {"Violet Translator", CPlayerState::kIT_VioletTranslator},
    {"Amber Translator", CPlayerState::kIT_AmberTranslator},
    {"Emerald Translator", CPlayerState::kIT_EmeraldTranslator},
    {"Cobalt Translator", CPlayerState::kIT_CobaltTranslator},
};

constexpr SItemCheck kKeyItems[] = {
    {"Sky Temple Key 1", CPlayerState::kIT_TempleKey1},
    {"Sky Temple Key 2", CPlayerState::kIT_TempleKey2},
    {"Sky Temple Key 3", CPlayerState::kIT_TempleKey3},
    {"Sky Temple Key 4", CPlayerState::kIT_TempleKey4},
    {"Sky Temple Key 5", CPlayerState::kIT_TempleKey5},
    {"Sky Temple Key 6", CPlayerState::kIT_TempleKey6},
    {"Sky Temple Key 7", CPlayerState::kIT_TempleKey7},
    {"Sky Temple Key 8", CPlayerState::kIT_TempleKey8},
    {"Sky Temple Key 9", CPlayerState::kIT_TempleKey9},
    {"Agon Key 1", CPlayerState::kIT_AgonKey1},
    {"Agon Key 2", CPlayerState::kIT_AgonKey2},
    {"Agon Key 3", CPlayerState::kIT_AgonKey3},
    {"Torvus Key 1", CPlayerState::kIT_TorvusKey1},
    {"Torvus Key 2", CPlayerState::kIT_TorvusKey2},
    {"Torvus Key 3", CPlayerState::kIT_TorvusKey3},
    {"Hive Key 1", CPlayerState::kIT_HiveKey1},
    {"Hive Key 2", CPlayerState::kIT_HiveKey2},
    {"Hive Key 3", CPlayerState::kIT_HiveKey3},
};

// The toggles as a grid, as many columns as the widest name leaves room for.
void DrawItemChecks(CPlayerState& ps, const char* tableId, const SItemCheck* items, int count) {
  float nameWidth = 0.f;
  for (int i = 0; i < count; ++i) {
    nameWidth = std::max(nameWidth, ImGui::CalcTextSize(items[i].name).x);
  }
  nameWidth += ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
               ImGui::GetStyle().ItemSpacing.x * 2.f;
  const int columns = std::clamp(int(ImGui::GetContentRegionAvail().x / nameWidth), 1, 4);
  if (ImGui::BeginTable(tableId, columns)) {
    for (int i = 0; i < count; ++i) {
      ImGui::TableNextColumn();
      bool owned = ps.HasPowerUp(items[i].type);
      if (ImGui::Checkbox(items[i].name, &owned)) {
        GrantItem(ps, items[i].type, owned ? 1 : 0, owned ? 1 : 0);
      }
    }
    ImGui::EndTable();
  }
}

} // namespace

// The switches and readouts for working on what mods draw: the Collision view (port_collision_view.h)
// and the frame statistics. MP1's PBR / room-geometry counters have no counterpart in this port.
void DrawRendering() {
  static const char* const kCollisionModes[] = {"off", "on top of the world", "only (hide the world)"};
  int collision = int(PortCollisionView::GetMode());
  if (ImGui::Combo("Collision", &collision, kCollisionModes, 3)) {
    PortCollisionView::SetMode(PortCollisionView::Mode(collision));
  }
  ImGui::SetItemTooltip("Recommended: off. Not saved.\n"
                        "Draws what Samus collides with: walls grey, floors blue, ceilings red,\n"
                        "lava orange, phazon cyan, grates yellow, solid objects as orange boxes.");

  if (ImGui::CollapsingHeader("Frame statistics")) {
    if (const AuroraStats* stats = aurora_get_stats()) {
      ImGui::Text("%.0f fps, %u draws (%u merged), %u passes", aurora_get_fps(), stats->drawCallCount,
                  stats->mergedDrawCallCount, stats->renderPassCount);
      ImGui::Text("vertices %.1f MiB, indices %.1f, arrays %.1f, uniforms %.1f, texture uploads %.1f",
                  stats->lastVertSize / 1048576.f, stats->lastIndexSize / 1048576.f,
                  stats->lastStorageSize / 1048576.f, stats->lastUniformSize / 1048576.f,
                  stats->lastTextureUploadSize / 1048576.f);
      if (const uint32_t resident = aurora_get_resident_geometry_mib()) {
        ImGui::Text("kept on the GPU %.1f of %u MiB", aurora_get_resident_geometry_used() / 1048576.f, resident);
      }
      ImGui::Text("pipelines %u made, %u waiting", stats->createdPipelines, stats->queuedPipelines);
    }
    AuroraTextureStats textures{};
    aurora_get_texture_stats(&textures);
    ImGui::Text("textures %u, %.0f MiB; render targets %u, %.0f MiB; lighting %u, %.0f MiB", textures.count[0],
                textures.bytes[0] / 1048576.f, textures.count[1], textures.bytes[1] / 1048576.f, textures.count[2],
                textures.bytes[2] / 1048576.f);
  }
}

void DrawCheatsSection() {
  ImGui::Checkbox("God mode", &sGodMode);
  ItemHelp("The player takes no damage; other actors still do. Not saved.");
  ImGui::Checkbox("No Dark Aether damage", &sNoDarkAetherDamage);
  ItemHelp("Dark Aether's air does not hurt, as with the Light Suit; light crystals still heal. "
           "Not saved.");
  ImGui::BeginDisabled(PortOverlay::OriginalExperience());
  bool beamDoors = sUnlockBeamDoors;
  if (ImGui::Checkbox("Unlock beam doors", &beamDoors)) {
    PortOverlay::SetUnlockBeamDoors(beamDoors);
  }
  ImGui::EndDisabled();
  ItemHelp("Colour-locked doors open to any of the four beams. Missile locks stay.");
  ImGui::BeginDisabled(PortOverlay::OriginalExperience());
  bool stackBeams = sStackBeams;
  if (ImGui::Checkbox("Stack beams", &stackBeams)) {
    PortOverlay::SetStackBeams(stackBeams);
  }
  ImGui::SetItemTooltip("Each shot also fires the other beams you own, at no extra ammo cost.");
  ImGui::SameLine();
  ImGui::BeginDisabled(!sStackBeams);
  bool skipDark = sStackBeamsSkipDark;
  if (ImGui::Checkbox("Leave out Dark Beam", &skipDark)) {
    PortOverlay::SetStackBeamsSkipDark(skipDark);
  }
  ImGui::SetItemTooltip("Stacked shots skip the Dark Beam unless it's the one in hand, so they "
                        "don't drain light beacons and crystals.");
  ImGui::EndDisabled();
  ImGui::EndDisabled();

  CPlayerState* ps = gpGameState != nullptr ? gpGameState->GetPlayerState().GetPtr() : nullptr;
  // The front end has its own player state and its own Samus; the page is about the game's.
  if (ps == nullptr ||
      (gpGameState != nullptr && gpGameState->GetGameMode().GetGameModeType() == 'FRND')) {
    ImGui::TextDisabled("Waiting for gameplay...");
    return;
  }
  CHealthInfo* health = ps->HealthInfo();
  ImGui::Text("Health: %.0f / %.0f", health->GetHP(), ps->CalculateHealth());
  if (ImGui::Button("Full health")) {
    health->SetHP(ps->CalculateHealth());
  }
  ImGui::SameLine();
  if (ImGui::Button("Grant everything")) {
    for (const SItemCheck& item : kAbilityItems) {
      GrantItem(*ps, item.type, 1, 1);
    }
    for (const SItemCheck& item : kKeyItems) {
      GrantItem(*ps, item.type, 1, 1);
    }
    for (const CPlayerState::EItemType counter :
         {CPlayerState::kIT_Missile, CPlayerState::kIT_Powerbomb, CPlayerState::kIT_EnergyTanks,
          CPlayerState::kIT_DarkAmmo, CPlayerState::kIT_LightAmmo}) {
      const int max = CPlayerState::PortPowerUpMaxValue(counter);
      GrantItem(*ps, counter, max, max);
    }
    health->SetHP(ps->CalculateHealth());
  }

  ImGui::SeparatorText("Abilities");
  DrawItemChecks(*ps, "##abilitychecks", kAbilityItems, int(std::size(kAbilityItems)));
  if (ImGui::CollapsingHeader("Keys")) {
    DrawItemChecks(*ps, "##keychecks", kKeyItems, int(std::size(kKeyItems)));
  }

  const auto counter = [ps](const char* label, CPlayerState::EItemType type) {
    const int max = CPlayerState::PortPowerUpMaxValue(type);
    int amount = std::min(ps->GetItemAmount(type, false), max);
    // A fixed width, so the label sits next to its slider instead of at the page's right edge.
    ImGui::SetNextItemWidth(220.f);
    if (CheatSlider(label, amount, max)) {
      // The capacity only rises as far as the amount needs (the HUD draws capacity, so the
      // maximum would show a full expansion whatever the value), and the amount is set
      // outright: a slide down must take ammo away even though the capacity has room.
      GrantItem(*ps, type, std::max(amount, ps->GetItemCapacity(type)), amount);
    }
    ImGui::SameLine();
    ImGui::PushID(label);
    ImGui::CheckboxFlags("Infinite", &sInfiniteAmmo, PortOverlay::InfiniteAmmoKind(type));
    ImGui::PopID();
    ItemHelp("Firing does not use this ammo up, even at zero, and the HUD shows infinity. The real count "
             "is kept and pickups still add to it. Not saved.");
  };
  counter("Missiles", CPlayerState::kIT_Missile);
  counter("Power Bombs", CPlayerState::kIT_Powerbomb);
  counter("Dark Ammo", CPlayerState::kIT_DarkAmmo);
  counter("Light Ammo", CPlayerState::kIT_LightAmmo);
  // Ammo costs and expansion size (#27). Each has a Reset button: a slider is hard to land on
  // an exact value with a finger.
  {
    int comboBeam = PortOverlay::ComboBeamCost();
    ImGui::SetNextItemWidth(220.f);
    if (ImGui::SliderInt("Combo beam ammo cost", &comboBeam, 0, PortOverlay::kComboBeamCostMax,
                         "%d", ImGuiSliderFlags_AlwaysClamp)) {
      PortOverlay::SetComboBeamCost(comboBeam);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##combobeamcost")) {
      PortOverlay::SetComboBeamCost(PortOverlay::kComboBeamCostMax);
    }
    ItemHelp("Dark or Light ammo a charge combo uses (the Annihilator Beam uses it from both). "
             "Reset: 30, the original cost. Saved.");
    int comboMissile = PortOverlay::ComboMissileCost();
    ImGui::SetNextItemWidth(220.f);
    if (ImGui::SliderInt("Combo missile cost", &comboMissile, 0, PortOverlay::kComboMissileCostMax,
                         "%d", ImGuiSliderFlags_AlwaysClamp)) {
      PortOverlay::SetComboMissileCost(comboMissile);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##combomissilecost")) {
      PortOverlay::SetComboMissileCost(PortOverlay::kComboMissileCostMax);
    }
    ItemHelp("Missiles a charge combo uses. At 0 a combo needs no missile. Reset: 5, the original "
             "cost. Saved.");
    int perExpansion = PortOverlay::BeamAmmoPerExpansion();
    ImGui::SetNextItemWidth(220.f);
    if (ImGui::SliderInt("Beam ammo per expansion", &perExpansion,
                         PortOverlay::kBeamAmmoPerExpansionMin, PortOverlay::kBeamAmmoPerExpansionMax,
                         "%d", ImGuiSliderFlags_AlwaysClamp)) {
      PortOverlay::SetBeamAmmoPerExpansion((perExpansion + 5) / 10 * 10);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##beamammoperexpansion")) {
      PortOverlay::SetBeamAmmoPerExpansion(PortOverlay::kBeamAmmoPerExpansionDefault);
    }
    ItemHelp("Dark and Light ammo each Ammo Expansion adds, and the matching ammo it fills; the "
             "maximum is 5 expansions' worth (250 at 50, 500 at 100). Reset: 50, the original. "
             "Lowering it only lowers the cap: ammo above it is trimmed the next time you pick "
             "some up. Saves keep working: the extra above 250 is stored beside the card "
             "(beam_ammo.sidecar), so a save without it loads at 250. Saved.");
  }
  // Energy tanks are the one counter that also moves the health bar.
  {
    const int max = CPlayerState::GetPowerUpMaxValue(CPlayerState::kIT_EnergyTanks);
    int tanks = std::min(ps->GetItemAmount(CPlayerState::kIT_EnergyTanks, false), max);
    ImGui::SetNextItemWidth(220.f);
    if (CheatSlider("Energy Tanks", tanks, max)) {
      GrantItem(*ps, CPlayerState::kIT_EnergyTanks, tanks, tanks); // tanks have no ammo: count is capacity
      health->SetHP(ps->CalculateHealth());
    }
  }
  ItemHelp("The items go into the save in memory: the next save on the memory card writes them, "
           "and nothing here is kept across a restart on its own.");
  ImGui::SeparatorText("Teleport");
  if (ImGui::Button("Previous area")) {
    PortWarp::CycleArea(-1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Next area")) {
    PortWarp::CycleArea(1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Previous world")) {
    PortWarp::CycleWorld(-1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Next world")) {
    PortWarp::CycleWorld(1);
  }
  // The room's twin in the other dimension, where Samus stands (PortWarp::Dimension). The game says
  // whether there was one a frame later; the line stays until the next press.
  static int sDimensionResult = 0;
  if (ImGui::Button("Light / Dark world")) {
    sDimensionResult = 0;
    PortWarp::Dimension();
  }
  ImGui::SetItemTooltip("Cross to the same spot in the other dimension, if this room has a twin there.");
  if (const int result = PortWarp::TakeDimensionResult(); result != 0) {
    sDimensionResult = result;
  }
  if (sDimensionResult < 0) {
    ImGui::SameLine();
    ImGui::TextDisabled("No twin room in the other dimension");
  }
  // The Prime 1 port's Teleport block (debug_ui/cheats.cpp): a filtered list of the current
  // world's rooms and the save's world table, both by name. The requests are the same PortWarp
  // ones the console's `warp`/`world` verbs and MP2_TELEPORT use, so nothing here walks around
  // what the game already does.
  if (sRooms.empty()) {
    ImGui::TextDisabled("(no world yet)");
  } else {
    const int areaCount = static_cast< int >(sRooms.size());
    const int current = sAreaId;
    // Between two updates the published ids are the same frame's, so this only guards a world
    // that is arriving: an area index outside the table is not looked up.
    const bool currentValid = current >= 0 && current < areaCount;
    const auto roomLabel = [](int index) { return sRooms[index].name; };
    const auto roomExists = [](int index) { return sRooms[index].real; };
    const auto goToRoom = [](int index) {
      char text[16];
      std::snprintf(text, sizeof(text), "%d", index);
      PortWarp::Area(text);
    };

    // One list per dimension, split by each area's map (SRoomEntry::dimension); a room whose map
    // has not loaded yet, or that the map does not cover, is listed with Light Aether's. Cutscene
    // stages (SRoomEntry::EDimension::kCutscene) are left out: warping there shows a white screen.
    const auto isDark = [](int index) {
      return sRooms[index].dimension == SRoomEntry::EDimension::kDark;
    };
    static char sRoomFilter[64] = {};
    const float goWidth = ImGui::CalcTextSize("Reload").x + ImGui::GetStyle().FramePadding.x * 2.f;
    const float tagWidth = ImGui::GetCursorPosX() +
                           std::max(ImGui::CalcTextSize("Light").x, ImGui::CalcTextSize("Dark").x) +
                           ImGui::GetStyle().ItemSpacing.x;
    const auto roomCombo = [&](const char* tag, const char* id, bool dark) {
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(tag);
      ImGui::SameLine(tagWidth);
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - goWidth -
                              ImGui::GetStyle().ItemSpacing.x);
      const bool here = currentValid && isDark(current) == dark;
      if (ImGui::BeginCombo(id, here ? roomLabel(current).c_str() : "(pick a room)",
                            ImGuiComboFlags_HeightLarge)) {
#if !defined(PORT_MOBILE) // it would raise the on-screen keyboard every time
        if (ImGui::IsWindowAppearing()) {
          ImGui::SetKeyboardFocusHere();
        }
#endif
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##roomfilter", "Filter rooms", sRoomFilter, sizeof(sRoomFilter));
        for (int i = 0; i < areaCount; ++i) {
          if (!roomExists(i) || isDark(i) != dark ||
              sRooms[i].dimension == SRoomEntry::EDimension::kCutscene) {
            continue;
          }
          const std::string label = roomLabel(i);
          if (sRoomFilter[0] != '\0' && SDL_strcasestr(label.c_str(), sRoomFilter) == nullptr &&
              SDL_strcasestr(sRooms[i].label.c_str(), sRoomFilter) == nullptr) {
            continue;
          }
          ImGui::PushID(i);
          if (ImGui::Selectable(label.c_str(), i == current)) {
            goToRoom(i);
          }
          ImGui::SetItemTooltip("%s", sRooms[i].label.c_str());
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
      ImGui::SetItemTooltip(dark ? "Pick a Dark Aether room to go there. A room that is not loaded "
                                   "streams in first."
                                 : "Pick a Light Aether room to go there. A room that is not loaded "
                                   "streams in first.");
    };
    roomCombo("Light", "##room", false);
    ImGui::SameLine();
    ImGui::BeginDisabled(!currentValid);
    if (ImGui::Button("Reload")) {
      // A world warp to this room would put Samus at the room's spawn point (the elevator, for
      // most); the save-state route rebuilds the room and puts her back where she stands.
      PortSaveState::RequestRoomReload();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Rebuild the room from the disc, as leaving and coming back does.");
    roomCombo("Dark", "##darkroom", true);

    // Rebuilt when the card's table changes size, so loading another save does not leave the
    // combo showing the previous one's worlds.
    static size_t sWorldListCount = 0;
    static std::vector< std::pair< uint32_t, std::string > > sWorldList;
    const int worldCount =
        gpMemoryCard != nullptr ? static_cast< int >(gpMemoryCard->GetMemoryWorlds().size()) : 0;
    if (worldCount > 0 && static_cast< size_t >(worldCount) != sWorldListCount) {
      sWorldListCount = static_cast< size_t >(worldCount);
      sWorldList.clear();
      const rstl::vector< CMemoryCard::MemoryWorld >& worlds = gpMemoryCard->GetMemoryWorlds();
      for (int i = 0; i < worlds.size(); ++i) {
        const uint32_t id = static_cast< uint32_t >(worlds[i].first);
        std::string name = PlainWorldName(worlds[i].second.GetFrontEndName());
        // The six multiplayer arenas (Metroid6.pak, same ids in every release) and the front end
        // carry no name in the card's table (Front End: G2ME, G2MP, R3ME ids); MLVL names from the disc (tools/world_areas.py --list).
        static const struct {
          uint32_t id;
          const char* name;
        } kUnnamedWorlds[] = {
            {0xa50a80cc, "Multiplayer: Sidehopper Station"}, {0xae171602, "Multiplayer: Spires"},
            {0xe3b0c703, "Multiplayer: Crossfire Chaos"},    {0x233e42be, "Multiplayer: Pipeline"},
            {0x406add7f, "Multiplayer: Spider Complex"},     {0x7e19ed26, "Multiplayer: Shooting Gallery"},
            {0x69802220, "Front End"},                       {0x7b6eaa68, "Front End"},
            {0xdaea74a3, "Front End"},
        };
        for (const auto& known : kUnnamedWorlds) {
          if (name.empty() && known.id == id) {
            name = known.name;
          }
        }
        if (name.empty()) {
          char buf[32];
          std::snprintf(buf, sizeof(buf), "MLVL %08X", static_cast< unsigned >(id));
          name = buf;
        }
        sWorldList.emplace_back(id, name);
      }
    }
    if (sWorldList.empty()) {
      ImGui::TextDisabled("(the memory card's world table is not loaded yet)");
    } else {
      const char* currentName = "(another world)";
      for (const std::pair< uint32_t, std::string >& entry : sWorldList) {
        if (entry.first == sWorldId) {
          currentName = entry.second.c_str();
        }
      }
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::BeginCombo("##world", currentName)) {
        for (const std::pair< uint32_t, std::string >& entry : sWorldList) {
          ImGui::PushID(static_cast< int >(entry.first));
          if (ImGui::Selectable(entry.second.c_str(), entry.first == sWorldId)) {
            char text[16];
            std::snprintf(text, sizeof(text), "%08x", entry.first);
            PortWarp::World(text, "");
          }
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
      ImGui::SetItemTooltip("Pick a world to reload into its first room.");
    }
  }

  ImGui::SeparatorText("Room layers");
  ImGui::TextDisabled("The room's script layers, as the world's layer state has them.");
  ItemHelp("The game switches them for good as the story moves on (a boss fight turns its "
           "first-pass layer off when it starts), and the next save keeps them, so leaving a fight "
           "by a warp can leave its room without the enemy. Turning a layer on loads it now; "
           "turning one off takes effect when the room loads again (Reload).");
  if (sLayers.empty() || sLayersArea != sAreaId) {
    ImGui::TextDisabled("(no layer state for this room)");
  } else {
    for (int i = 0; i < static_cast< int >(sLayers.size()); ++i) {
      bool active = sLayers[i].active;
      char label[96];
      std::snprintf(label, sizeof(label), "%d: %s##layer%d", i, sLayers[i].name.c_str(), i);
      if (ImGui::Checkbox(label, &active)) {
        if (active) {
          PortWarp::Layer(i);
        } else {
          PortWarp::LayerOff(i);
        }
      }
    }
  }
}

void DrawCheatsTab() {
  DrawCheatsSection();
}

} // namespace PortDebug
