#include "port_map_pickups.h"

#include "port_overlay.h"
#include "port_rando_world.h"
#include "port_randomizer.h"

#include "MetroidPrime/CMapWorldInfo.hpp"
#include "MetroidPrime/CScriptMailbox.hpp"
#include "MetroidPrime/CWorldLayerState.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"

#include <cstring>

namespace PortMapPickups {

namespace {
constexpr int kMaxLocations = 160;

const PortRandoWorld::Area* AreaOf(int location) {
  const PortRandoWorld::Location* loc = PortRandoWorld::LocationAt(location);
  return loc != nullptr ? PortRandoWorld::AreaAt(loc->area) : nullptr;
}

// Calls fn(area index in its MLVL, area, visited) for every counted room of `world` (0: all).
template < typename Fn >
void ForEachRoom(uint32_t world, Fn fn) {
  const int count = PortRandoWorld::AreaCount();
  int lastWorld = -1;
  int index = 0;
  const CMapWorldInfo* info = nullptr;
  for (int i = 0; i < count; ++i) {
    const PortRandoWorld::Area* area = PortRandoWorld::AreaAt(i);
    // The flat list is every world's areas in MLVL order.
    index = area->world == lastWorld ? index + 1 : 0;
    const uint32_t mlvl = PortRandoWorld::WorldAt(area->world)->mlvl;
    if (area->world != lastWorld) {
      lastWorld = area->world;
      CWorldState* state =
          gpGameState != nullptr ? gpGameState->PortFindWorldState(CAssetId(mlvl)) : nullptr;
      info = state != nullptr ? state->GetMapWorldInfo().GetPtr() : nullptr;
    }
    if ((world != 0 && mlvl != world) || std::strncmp(area->name, "game_end", 8) == 0) {
      continue;
    }
    fn(index, *area, info != nullptr && info->IsAreaVisited(TAreaId(index)));
  }
}
} // namespace

int Mode() { return PortOverlay::MapPickups(); }

uint32_t World(int location) {
  const PortRandoWorld::Area* area = AreaOf(location);
  const PortRandoWorld::World* world = area != nullptr ? PortRandoWorld::WorldAt(area->world) : nullptr;
  return world != nullptr ? world->mlvl : 0;
}

uint32_t Area(int location) {
  const PortRandoWorld::Area* area = AreaOf(location);
  return area != nullptr ? area->mrea : 0;
}

void Position(int location, float pos[3]) {
  const PortRandoWorld::LocationMap* map = PortRandoWorld::LocationMapAt(location);
  pos[0] = map != nullptr ? map->x : 0.f;
  pos[1] = map != nullptr ? map->y : 0.f;
  pos[2] = map != nullptr ? map->z : 0.f;
}

bool Collected(int location) {
  const PortRandoWorld::LocationMap* map = PortRandoWorld::LocationMapAt(location);
  if (gpGameState == nullptr || map == nullptr || map->relay == 0) {
    return false;
  }
  CWorldState* state = gpGameState->PortFindWorldState(CAssetId(World(location)));
  const CScriptMailbox* mailbox = state != nullptr ? state->Mailbox().GetPtr() : nullptr;
  return mailbox != nullptr && mailbox->HasMsg(TEditorId(map->relay));
}

EKind KindOfItem(int item, int capacity) {
  switch (item) {
  case CPlayerState::kIT_Missile:
  case CPlayerState::kIT_EnergyTanks:
  case CPlayerState::kIT_DarkAmmo:
  case CPlayerState::kIT_LightAmmo:
    return kK_Minor;
  case CPlayerState::kIT_Powerbomb:
    // The main Power Bomb gives two; an expansion one.
    return capacity > 1 ? kK_Major : kK_Minor;
  default:
    return kK_Major;
  }
}

const unsigned char* Kinds() {
  static unsigned char sKinds[kMaxLocations];
  const int count = PortRandoWorld::LocationCount();
  const bool randomized = PortRandomizer::Enabled();
  for (int i = 0; i < count && i < kMaxLocations; ++i) {
    const PortRandoWorld::Location* loc = PortRandoWorld::LocationAt(i);
    int item = loc->item;
    int capacity = loc->capacity;
    if (randomized) {
      // Left alone (the disc's item) where the seed places nothing.
      PortRandomizer::Placed(World(i), Area(i), loc->editorId, item, capacity);
    }
    sKinds[i] = KindOfItem(item, capacity);
  }
  return sKinds;
}

bool RoomHoldsPickup(uint32_t world, uint32_t area) {
  const int count = PortRandoWorld::LocationCount();
  for (int i = 0; i < count; ++i) {
    if (Area(i) == area && World(i) == world && !Collected(i)) {
      return true;
    }
  }
  return false;
}

int Count(uint32_t world) {
  const int count = PortRandoWorld::LocationCount();
  int n = 0;
  for (int i = 0; i < count; ++i) {
    n += world == 0 || World(i) == world ? 1 : 0;
  }
  return n;
}

int CollectedCount(uint32_t world) {
  const int count = PortRandoWorld::LocationCount();
  int n = 0;
  for (int i = 0; i < count; ++i) {
    n += (world == 0 || World(i) == world) && Collected(i) ? 1 : 0;
  }
  return n;
}

int RoomCount(uint32_t world) {
  int n = 0;
  ForEachRoom(world, [&](int, const PortRandoWorld::Area&, bool) { ++n; });
  return n;
}

int VisitedCount(uint32_t world) {
  int n = 0;
  ForEachRoom(world, [&](int, const PortRandoWorld::Area&, bool visited) { n += visited ? 1 : 0; });
  return n;
}

std::vector< Room > UnvisitedRooms(uint32_t world) {
  std::vector< Room > out;
  ForEachRoom(world, [&](int index, const PortRandoWorld::Area& area, bool visited) {
    if (!visited) {
      out.push_back(Room{index, area.name});
    }
  });
  return out;
}

} // namespace PortMapPickups
