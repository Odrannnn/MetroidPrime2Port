#pragma once

// Warp requests: jump straight to an area - or another world - instead of walking there. The
// platform layer only queues them (`MP2_WORLD` / `MP2_TELEPORT`, the F5-F8 hotkeys, and the
// `MP2_INPUT` tokens `NEXTAREA`/`PREVAREA`/`NEXTWORLD`/`PREVWORLD`); the game consumes one per
// update in `CStateManager::PortWarpTick`, because only the game can read the resource factory,
// the world and the memory card's world table.
//
// The text fields take an asset name or a hex asset id, so nothing here needs a lookup table:
// the game resolves names where the resource names actually live.

namespace PortWarp {

enum class EKind {
  kNone,
  kArea,      // an area of the current world
  kWorld,     // reload into another world, optionally into one of its areas
  kCycleArea, // the next/previous existing area of the current world
  kCycleWorld, // the next/previous world of the game's world table
  kDock,      // stand in the nearest doorway of this area, facing through it
  kDoor,      // stand in front of the nearest door, facing it, to test opening it
  kStation,   // stand on the nearest save station, to test saving in game
  kWorldPortal, // activate the nearest elevator (world teleporter), to test a world change
  kKill,       // kill the player, to test dying and respawning
  kHeal,       // refill the player's energy, so a sweep survives a damaging environment
  kLayer,      // activate a layer of the current area (its `delta` is the layer number)
  kLayerOff,   // mark a layer of the current area inactive (takes effect on the area's next load)
  kDimension   // the same spot in the other dimension (light <-> dark), if this room has a twin there
};

struct SRequest {
  EKind kind = EKind::kNone;
  char world[80] = {};
  char area[80] = {};
  int delta = 1;
};

void Area(const char* area);
void World(const char* world, const char* area);
void CycleArea(int delta);
void CycleWorld(int delta);
void Dock();
void Door();
void Station();
void WorldPortal();
void Kill();
void Heal();
// Cross to the other dimension where the player stands: the area there whose bounds match this
// one's (Dark Aether's rooms sit on their Light twins' coordinates), keeping the position.
void Dimension();
// The last Dimension request's outcome, for the menu: the game notes it, the menu reads and clears it
// (0 none since the last read, 1 crossing, -1 no twin).
void NoteDimension(bool found);
int TakeDimensionResult();
// Activate a layer of the current area. A module whose actors live in a layer other than 0 - every
// one of ElitePirate's five areas, for instance - only appears once that layer runs, so this is what
// makes such a module verifiable from the `MP2_INPUT` token `LAYER<layer>`.
// Layer requests are queued instead of replacing each other, so `layer 2; layer 14` in one tick both
// run: one is served at a time, the next when the game has finished streaming the one before.
void Layer(int layer);
// Mark a layer of the current area inactive in the world's layer state, as a script's layer
// controller does: the live objects stay until the area loads again (the overlay's Reload).
// Queued like `Layer`.
void LayerOff(int layer);
// Reads MP2_WORLD / MP2_TELEPORT once; called from the platform event loop.
void ApplyEnvOnce();
// Whether a request is waiting - a warp in the slot, or a queued layer whose turn it is. The game
// checks this before it is ready to act (no world yet, or the front end), so that `Consume` does not
// take a request it would then have to drop.
bool Pending();
// Whether a layer request handed out by `Consume` is still being streamed in by the game
// (CStateManager::PortLayerTick finishes it over frames). Set from PortWarpTick every update: while
// it is set, the next queued layer waits, so two `layer` commands in one tick are not merged.
void SetLayerBusy(bool busy);
// Whether the waiting request is the one `MP2_WORLD`/`MP2_TELEPORT` queued at start-up. The game
// holds that one until gameplay, because a warp served while the save's world is still loading
// leaves the destination's scripted camera filters applying their flash for the rest of the run;
// a warp issued once the game is running - a console `warp`, an `MP2_INPUT` token - is clean.
bool PendingIsBoot();
// A copy of the pending boot request (MP2_WORLD / MP2_TELEPORT), left pending; false when none.
bool PeekBoot(SRequest& out);
// Serve a MP2_WORLD="<world>,<area>" boot request before the first world loads (defined in
// src/MetroidPrime/CStateManager.cpp, beside the resolvers it uses); true when it did.
bool DirectBoot();
// Takes the pending request, if any. One at a time on purpose: a warp reloads the world, which
// invalidates anything resolved against the old one.
bool Consume(SRequest& out);
void Clear();

} // namespace PortWarp
