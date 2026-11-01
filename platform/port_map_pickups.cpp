#include "port_map_pickups.h"

#include "port_overlay.h"
#include "port_rando_world.h"
#include "port_randomizer.h"

#include "MetroidPrime/CScriptMailbox.hpp"
#include "MetroidPrime/CWorldLayerState.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"

namespace PortMapPickups {

namespace {
constexpr int kMaxLocations = 160;

const PortRandoWorld::Area* AreaOf(int location) {
  const PortRandoWorld::Location* loc = PortRandoWorld::LocationAt(location);
  return loc != nullptr ? PortRandoWorld::AreaAt(loc->area) : nullptr;
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

} // namespace PortMapPickups
