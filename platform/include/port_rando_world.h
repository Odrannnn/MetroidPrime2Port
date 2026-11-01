#ifndef METROID_PRIME_PORT_PORT_RANDO_WORLD_H
#define METROID_PRIME_PORT_PORT_RANDO_WORLD_H
#include <cstdint>

// The randomizer's world graph and location list: the single-player worlds' areas, the MLVL
// dock connections between them, the WorldTeleporters, and the pickups a seed may change. Read
// from the disc by tools/rando_world.py (which documents what counts as a location). Pure data:
// no game dependencies, so a test can call it. Area indices index the flat Areas() list.
namespace PortRandoWorld {

struct World {
  uint32_t mlvl;
  const char* name;
};

struct Area {
  int world;        // index into Worlds()
  uint32_t mrea;
  bool dark;        // the area's name ends in `_dark` (Dark Aether)
  const char* name; // MLVL internal area name
};

struct Dock {
  int area, dock;     // this side
  int toArea, toDock; // the other side
  bool isVirtual;     // the area's DOCK object of this number is virtual (no door)
};

// What passing a dock side needs, read from its door and the scripts around it (the rules are in
// tools/rando_world.py). Shield doors are Any once the shield is gone.
enum class DockBeam { NoDoor, Any, Light, Dark, Annihilator, Morph };
enum class DockShield { None, Missile, SuperMissile, Seeker, PowerBomb };
enum class DockTranslator { None, Violet, Amber, Emerald, Cobalt };
enum class DockScriptLock { None, Temporary, Permanent }; // Temporary: the area also sends ULCK

// Parallel to Dock: DockLockAt(i) describes the side DockAt(i) starts from.
struct DockLock {
  int area, dock;
  DockBeam beam;
  DockShield shield;
  DockTranslator translator;
  bool translatorLayered;      // the translator check sits on a layer that is off by default
  bool translatorOneSided;     // the gate lets you through from the door's other face
  DockScriptLock scriptLock;
  bool scriptLockDefaultLayer; // a LOCK sender (or the isLocked door) is on a default-on layer
};

struct Teleporter {
  int area;
  int layer;
  uint32_t editorId;
  uint32_t toMlvl, toMrea;
};

struct Location {
  int area;
  int layer; // script layer, -1 for the MREA's generated-object section
  uint32_t editorId;
  int item;  // vanilla EItemType
  int amount;
  int capacity;
  const char* itemName;
};

// Parallel to Location: where the pickup sits and the memory relay it activates when collected
// (which the world's mailbox keeps, so a reload knows it is gone). The Bomb Guardian's
// generated MorphBallBombs has none; its relay is the one the boss's death activates.
struct LocationMap {
  uint32_t relay;
  float x, y, z; // world space
};

// Things inside a room that need an item (tools/rando_world.py, classify_obstacles). Only the
// requirement is read from the disc; which of the area's docks and locations an obstacle cuts off
// is not, and is left to hand-written rules.
enum class ObstacleKind {
  Barrier,          // a PLAT only some player weapons hurt
  TriggeredBarrier, // a PLAT removed when its DTRG (or PLAT) driver dies; needs what hurts the driver
  GrapplePoint,
  Spinner,
  CannonBall,
  BallTrigger,
  SpiderTrack,      // all of an area's BALW (or BALS) in one row, at their centroid; editorId 0
  Portal,           // PRTT: needs not derived (0)
  SafeZoneCrystal,  // SFZC: needs not derived (0)
};

// Obstacle::needs bits; any one set bit is enough. Names are PortRandomizer's ItemName spellings.
enum ObstacleNeed : uint32_t {
  kNeedDarkBeam = 1u << 0,
  kNeedLightBeam = 1u << 1,
  kNeedAnnihilatorBeam = 1u << 2,
  kNeedMissile = 1u << 3,
  kNeedSuperMissile = 1u << 4,
  kNeedMorphBallBombs = 1u << 5,
  kNeedPowerbomb = 1u << 6,
  kNeedBoostBall = 1u << 7,
  kNeedScrewAttack = 1u << 8,
  kNeedGrappleBeam = 1u << 9,
  kNeedSpiderBall = 1u << 10,
  kNeedMorphBall = 1u << 11,
};
constexpr int kObstacleNeedBits = 12;

struct Obstacle {
  int area;
  int layer;
  bool layerDefault; // the layer is on by default
  uint32_t editorId; // 0 for a SpiderTrack row
  ObstacleKind kind;
  uint32_t needs;    // ObstacleNeed bits, any one of; 0 = none derived
  float x, y, z;     // world position
  int nearestDock;   // dock number in the area, -1 if none
  float dockDistance;
  uint32_t nearestLocation; // editor id of the nearest location in the area, 0xFFFFFFFF if none
  float locationDistance;
  const char* name;  // INAM; a TriggeredBarrier's is "<plat> <= <drivers>"
};

int WorldCount();
int AreaCount();
int DockCount();
int TeleporterCount();
int LocationCount();
const World* WorldAt(int index);
const Area* AreaAt(int index);
const Dock* DockAt(int index);
const DockLock* DockLockAt(int index); // same index as DockAt
const Teleporter* TeleporterAt(int index);
const Location* LocationAt(int index);
const LocationMap* LocationMapAt(int index); // same index as LocationAt
int ObstacleCount();
const Obstacle* ObstacleAt(int index);
// The vanilla EItemType that grants one ObstacleNeed bit, else -1.
int ObstacleNeedItem(uint32_t need);

// Index of the area with this MLVL/MREA pair, else -1.
int FindArea(uint32_t mlvl, uint32_t mrea);
// Index of the location keyed WORLD:AREA:ENTITY (as PortRandomizer::FormatLocationKey), else -1.
int FindLocation(uint32_t mlvl, uint32_t mrea, uint32_t editorId);

} // namespace PortRandoWorld

#endif
