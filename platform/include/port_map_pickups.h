#pragma once

#include <cstdint>
#include <vector>

// Pickups on the map, like MP1's: a white dot where each of the randomizer's locations
// (PortRandoWorld: every item pickup) sits, until it is collected, or a tint on the rooms that
// still hold one (the map_pickups setting, PortOverlay::EMapPickups). One colour for every item so
// the map doesn't spoil a randomized game; with map_pickup_kinds the dot's shape tells a major
// item from a minor one and a collected pickup stays as a cross. CMapWorld draws them (grep
// PortMapPickups). Indices are PortRandoWorld location indices.
namespace PortMapPickups {

// PortOverlay::MapPickups(); Off draws nothing.
int Mode();

// Whether the pickup is collected: its memory relay is in its world's mailbox, which is what keeps
// it gone after a reload. The Bomb Guardian's generated drop has no relay; it counts once the
// boss's death relay is set (killing the boss is what spawns it).
// False outside a game.
bool Collected(int location);
// The location's MLVL and MREA, and its position in world space.
uint32_t World(int location);
uint32_t Area(int location);
void Position(int location, float pos[3]);

enum EKind {
  kK_Unknown, // draws like a major
  kK_Major,   // a beam, visor, suit, ball upgrade, key, launcher or the main Power Bomb
  kK_Minor,   // a missile, power bomb or beam ammo expansion, or an energy tank
};
// By CPlayerState::EItemType and capacity.
EKind KindOfItem(int item, int capacity);
// One EKind per location: the seed's item in a randomized game, else the disc's. Call once a frame,
// not per dot.
const unsigned char* Kinds();

// Rooms mode: the area (by MREA) still holds a pickup not collected.
bool RoomHoldsPickup(uint32_t world, uint32_t area);

// Counts for the tracker, over one world (MLVL) or every world (0).
int Count(uint32_t world);
int CollectedCount(uint32_t world);

// Rooms visited, as the map has them (the world's CMapWorldInfo; Reveal map changes nothing here).
// The ending's `game_end_partN` areas are not counted: they are not somewhere to explore. Over one
// world (MLVL) or every world (0); none visited outside a game.
int RoomCount(uint32_t world);
int VisitedCount(uint32_t world);
struct Room {
  int index;        // the area's index in its MLVL
  const char* name; // the MLVL's internal area name
};
// The world's rooms not visited yet, in MLVL order.
std::vector< Room > UnvisitedRooms(uint32_t world);

} // namespace PortMapPickups
