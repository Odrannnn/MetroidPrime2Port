# Randomizer

## Decision (2026-10-27): no Randovania in the port

The port carries no Randovania code or data: no patch-data importer, no location table taken from
its logic database. Seeds use the port's own format (`"locations"` keyed `WORLD:AREA:ENTITY`, below)
and come from the port's own generator; any logic the generator grows is written here, from the
disc. Logic may also live outside the port: an Archipelago apworld (a separate program, licensed
on its own terms, GPL if it derives from Randovania's database) generates the placements and hands
the port `WORLD:AREA:ENTITY` keys through slot data; the port is then only a client (checks out,
items in) and stays MIT. (Superseded the same day's "Randovania decides, the port applies" plan; its
importer and table were removed in the commit that records this.)

Still true from that work: pickups **can** be enumerated statically from the disc, so the dump sweep
is no longer the only source of locations. Plan, each step checkable on its own:

1. **Location list from the disc** - walk every `PCKP` of the single-player worlds (`Metroid1.pak`
   ..`Metroid5.pak`; `Metroid6.pak` is the multiplayer arenas), script layers and the MREA's
   generated-object section, into a `WORLD:AREA:ENTITY` table the generator reads instead of the
   dump log. `tools/rando_models.py` already does this walk for models.
   **Done (2026-10-27):** `tools/rando_world.py --write` writes
   `platform/include/port_rando_world.inc` (read by `platform/port_rando_world.cpp`, tested by
   `port_rando_world_tests`): 5 worlds, 284 areas, 678 MLVL dock edges (all symmetric, 78 virtual),
   53 `TEL1` teleporters and 114 locations. A location is a layer `PCKP` that is not a refill
   (items 41/43-46 with itemPercentageIncrease 0), or a generated-section `PCKP` that raises the
   percentage (only the Bomb Guardian's bombs). Excluded: the nine TempleKey1 copies in
   `12_temple_finalgate_dark` layer 3 and the AgonKey2/3 copies in `03_sand_temple_dark` layer 2.
   The rules are in the tool's docstring. `port_rando_gen.cpp` generates from this table.
2. **Seed format** - the native JSON only (done; see Workflow).
3. **Pickup presentation** - the placed item's model and acquisition text.
   *Done (builds, unit-tested, not yet seen in game):* `tools/rando_models.py` reads every PCKP on
   disc and writes `platform/include/port_rando_models.inc` (44 rows: item -> CMDL + animation set,
   with the world paks that hold them). `LoadPickup` swaps in the donor only when
   `gpResourceFactory->GetResourceTypeById` resolves its CMDL (and ANCS), since lookup only sees
   loaded paks (`CResLoader::FindResourceForLoad`); otherwise the original model stays. ETM (107)
   borrows item 28's model, MissileLauncher (73) the Missile's. Items with no vanilla Pickup (Power
   Beam, Combat/Scan Visor, Varia, Morph Ball, Charge, Cannon Ball, ...) have no donor yet.
   Acquisition text: pickups reach their MEMO through SRLY/MRLY relays on `ZERO` (120 of 132 within
   4 hops), so `Touch` suppresses MEMO display while it sends `Arrived` and shows
   "<ItemName> acquired!" itself. A MEMO behind a timer would still show vanilla text. Scans are
   out of scope: vanilla pickups carry no scan.
4. **Logic** - from an apworld (port as AP client), or the port's own for offline beatable seeds.
   **Dock locks from the disc: done (2026-10-27).** `tools/rando_world.py` also writes
   `PORT_RANDO_DOCK_LOCK` rows, one per `PORT_RANDO_DOCK` edge (`PortRandoWorld::DockLockAt`).
   - The door is the DOOR that sends INCR to the dock's DOCK. 569 doors are on docks, and 6 are
     not: cutscene copies and in-room morph doors. 110 edges have no door.
   - Beam is the set of player weapons the door's vulnerability lets through (multiplier != 0,
     effect != Immune). Edges: Any 434, Light 60, Dark 48, Annihilator 14, Morph 12.
   - A shield door is immune until a MemoryRelay sends it RSET (`ResetDoor`). The relay is
     activated by a health-5 ACTR's DEAD, and that actor's only damaging weapon names the shield:
     Missile 35, SuperMissile 24, PowerBomb 24. For Seeker (14) it is a relay behind a CNTR that
     counts 5 seeker-lockable DTRGs.
   - A translator gate is a CRLY that tests a translator item and activates a scan POIN named
     "…Gate…". There are 16: Violet 7, Amber 4, Emerald 3, Cobalt 2.
   - Script lock: LOCK sent to the door, or `isLocked` at load. Temporary (54) when the area also
     sends ULCK; Permanent (3) otherwise, with `isLocked` counting as Permanent.
   - The tool fails unless docked doors, shields and gates match the measured counts per world.

   Ambiguities:
   - Nothing wires a translator gate to a door. It is assigned to the nearest docked door, 6 to 45
     units away (0u_temple_hall is 44.9).
   - Obstacles are extracted separately and are not wired to docks. `classify_obstacles` lists
     708 in-room obstacles (`PORT_RANDO_OBSTACLE`, `ObstacleAt()`), each with its position, the
     nearest dock and the nearest location. Barrier PLATs that power does not hurt need the
     weapons that do. A PLAT that a DTRG removes on DEAD (Agon's Dark Webbing) needs what hurts
     the DTRG. The other rows are grapple points, spinners, cannon balls, ball triggers, spider
     tracks (one row per area), portals and safe-zone crystals. Each requirement is an OR of the
     `ObstacleNeed` items; the docstring has the item mapping and the skipped names.
     `--dump-obstacles` prints every area's docks, locations and obstacles.
   - Topology is not derived: nothing says which docks or locations an obstacle separates, so
     that needs hand rules. The PATH navmesh cannot stand in for it: 150 of 284 areas have no
     regions, only 141 of 441 docks lie within 3 units of a region, and only 94 of 131 multi-dock
     areas have all their docks in one component.
   - Layer state is read only as the MLVL default flags. Which events switch a layer on is not
     traced: 2 translator checks and roughly half of the LOCK/ULCK senders sit on layers that are
     off by default.
   - Temporary locks are fight and cutscene locks, mostly harmless to logic but not proven so.

   **Hand rules (format).** One file per world, `tools/rando_rules/<MLVL internal name>.rules`,
   compiled by `tools/rando_rules.py` into `platform/include/port_rando_logic.inc`:

   ```
   area 0a_temple_hall                       # MLVL internal area name; one block per area
     dock0 - dock1: any                      # '-' both ways, '>' one way
     dock1 > loc0x00020034: SpaceJumpBoots | (MorphBall & MorphBallBombs)
     @ledge - dock2: GrappleBeam             # @name: an in-room node
     tel0x0C06013A - dock0: any              # a teleporter (editor id)
     covers 0x00020041 0x00020042            # obstacles these rules account for
     ignore 0x00020050: decorative webbing   # obstacles that block nothing, with a reason
     event AmorbisDefeated: loc0x28240265    # reaching the node grants the event, usable in any file
     atmosphere DarkSuit | LightSuit         # dark areas only: replaces the generated air requirement
   ```

   - Requirements: `any`, `never`, item names as spelled in `port_randomizer.cpp`'s table,
     `Name>=N` for counts, `&`, `|`, parentheses. `Missile` means at least one Missile.
     `ScrewAttack` also asks for `SpaceJumpBoots` (IMPLIES: it starts out of a double jump).
   - Every obstacle of the area (`--dump-obstacles`) must be in a `covers` or an `ignore` line,
     and every `dockN`/`locX`/`telX` must exist in the area, or the compiler fails.
   - Fail closed: an area with no block connects nothing, so its docks are dead ends.
   - Dock edges come from `PORT_RANDO_DOCK`; the evaluator adds their locks: the beam of the
     side you leave from (each face has its own colour), both sides' translator gates (fail
     closed) except a gate in `ONE_SIDED_GATES` (`tools/rando_world.py`, each with its disc
     evidence; today only Temple Sanctuary's North gate, raised by the Alpha Splinter fight),
     which counts only when leaving its own room, both sides' blast shields, and `never` for a Permanent script lock on the side you leave from. Temporary locks are ignored. Rules only describe movement inside a room.
   - Events are pseudo-items (ids from `kFirstEventItem` = 120); the evaluator grants one when its
     node is reached and searches again. The Light Suit wants the three temple boss drops' events.
   - Dark Aether air (`tools/rando_rules.py`, constants cited there from the decomp): every edge of
     a dark area is ANDed with `LightSuit | DarkSuit & EnergyTanks>=a | EnergyTanks>=b`, or with the
     block's `atmosphere` line. Safe zones are the default-layer SafeZoneCrystals (light crystals
     only with `LightBeam`, so a second tier `LightBeam & ...` is generated). For every pair of
     docks/locations, the stretch is the walked distance away from any zone: straight lines x 1.5
     (detour), each end continuing through up to 3 dark-to-dark doors to the nearest zone, and a
     pair may stop at one zone on the way. The worst pair / 16.5 units/s + 10 s margin gives
     seconds; damage is 6/s with no suit, 1.2/s with the Dark Suit; tanks = what keeps above 0 from
     99 + 100 per tank (more than 14, or no zone in reach, is `never`). `--exposure` prints the
     worst areas.
   - When unsure whether a route works without an item, require the item. A rule that is too
     strict only shrinks the seed space; one that is too loose makes unbeatable seeds.
   - Terrain check (enforced, 2026-11-03): `tools/rando_terrain.py` reads each area's static
     collision (MREA CAreaOctTree), finds standable spots (`IsFloor`: Floor material or normal
     z > 0.85, facing up: a ceiling's top is no floor), and links them by walking (step 1.0),
     falling straight down, rolling and analytical jumps (apex 3.78, Space Jump 7.68), including a
     jump that morphs into a raised tunnel mouth and a ball rolling off a tunnel lip. For each rule
     edge it reports what the mesh needs (the item sets below, or no path); MorphBall is in the
     start inventory, so it never refutes. Script platforms and
     solid actors (PLAT, ACTR with isSolid) on default-on layers are added at their initial place
     (DCLN, else collision box, else model bounds) in two states: as the area starts (breakable
     crates included) and once switches are thrown (ACTV, never DCTV or destroyed); an edge that
     passes only with them says "(with platforms)". Docks are anchored by shape (DOCK XFRM scale;
     a tall door's floor is searched deeper);
     virtual docks are skipped; a pickup is reached within jump touch range (6.2, 10.1 with Space
     Jump). Water (WATR boxes that stay on) changes the jump per take-off depth, simulated from
     CPlayerDynamics: fluid gravity 10, jump factor 0.37 submerged, Gravity Boost 45 for 1.5 s, dark
     water low gravity unless the Light Suit; never below the dry jump (the vanilla route climbs
     0r_swamp_hall's underwater stairs, steps 1.5 to 2.5, before Gravity Boost: the simulated 1.65
     walk jump can't, step-up and slopes being unmodelled), so water only adds height. Spider tracks (material bit 61) join the floors
     within 1.9 below them to every floor beside or below the same track (needs MorphBall and
     SpiderBall). Half-pipes (material bit 13) join the floors within 1.0 below them to every
     floor beside a cell of the same pipe up to 3.2 (ballHalfPipeBoostHeight) above or below it
     (needs MorphBall and BoostBall; boosted gap jumps and ramps are not modelled). Bomb jumps
     (MorphBallBombs) lift the ball 3.64 (CPlayer::BombJump: sqrt(2 * 35 * bombJumpHeight 7.9)
     against ball gravity 76) from any spot, drifting at the ball's air speed 10 onto a spot up to
     3.24 higher; one bomb only (the 1.0 s fuse outlasts the 0.62 s air time; double bomb jumps are a
     timing trick). Power Bombs don't
     bomb-jump in Echoes (only kPA_TriggerBomb calls BombJump, DOL 0x8003CA5C). Screw Attack
     starts on the third press out of a double jump, dry; each hop (up to 5, 20 a second across)
     peaks 5.0 over the double jump's spot and is allowed down to 4.5 under it
     (CMorphBall::ComputeScrewAttackMovement, disc TweakBall), so it needs the Space Jump Boots
     (without them that spot is stale) and reaches spots up to 8.78 over the take-off, searched
     40 away; screw wall jumps are a trick. Each edge's
     verdict is the minimal sets of MorphBall, SpaceJumpBoots, GravityBoost, SpiderBall, LightSuit,
     BoostBall, MorphBallBombs and ScrewAttack that make it. Terms with grapple are not refuted
     (UNMODELLED). An edge only a Screw Attack set crosses reads nopath for its other terms (the
     mesh misses their route, mostly moving platforms). Not modelled: swimming, moving platforms, story layer switches (GFMC
     Compound's Kinetic Orb Cannon sits on "Load After Movie"), cannons, elevators,
     air drift during a long fall (Sanctum Access).
     Verdicts are cached per area in `~/.cache/mp2-rando-terrain/` (key: the tool's model, i.e. its
     bytes above `check()`, mesh, edges, platforms): a cold run takes ~60 min, a warm one seconds; the key holds rule line
     numbers, so a rule edit rechecks every area below it in that file. Full run: 959 edge
     directions, ok 569, refuted 16, nopath 208 (84 crossed only by Screw Attack), unanchored 28, virtual 138.
     `tools/rando_rules.py` fails on a refuted edge unless `tools/rando_rules/terrain_accepted.txt`
     lists it (`area src > dst: reason`), and on an accepted line nothing refutes any more
     (`--no-terrain` skips; no disc skips too). Every one of the 65 refutations of the 2026-11-03
     baseline is triaged: most were Space Jump ledges the rules missed (measured steps of 4.2 to
     7.2 in each rule's comment); signed floors cleared 11. The accepted ones (13 with Hydrodynamo Shaft's water cannon) are model gaps
     with reasons: moving platforms (Kinetic Orb Cannons, Sacred Path's ball cannon, the Elevator
     platforms of 0h_temple_hall and 08_sand_casualpirates, Torvus Lagoon's Swamp Bridge, the hidden half-pipe's Sliding Coffins), a
     drifting fall. Rule fixes also moved Torvus's 0f_swamp_hall energy tank to the upper
     corridor (its tunnel plug is in that floor; the lower blocker opens only from inside). Early
     vanilla areas crossed before Space Jump are model gaps, not rule errors (`TestVanillaSpheres`
     guards that).
   - State (2026-10-27): all five worlds have rules (284 areas). With every item, all 114
     locations are reachable from the Landing Site. With the starting inventory and the vanilla
     placement all 114 open in 9 spheres (`TestVanillaSpheres` in `tests/port_rando_logic.cpp`,
     granting the four translators at their energy controllers). Elevator rooms need an explicit
     `dock0 - telX` edge, or their world is cut off.
   - Fill (2026-10-27): `platform/port_rando_gen.cpp` shuffles the 114 vanilla pickups (item,
     amount, capacity) with its own SplitMix64 seeded from the seed name's FNV-1a hash. Progression
     copies are pool copies of an item a requirement names, up to the largest count asked for (one
     Missile expansion, five Energy Tanks; `PortRandoLogic::RequiredCounts`); the rest are filler.
     The progression copies are placed in collection order, each in a random empty location
     reachable with the start and the copies before it (on the last empty location, the next copy is
     one that opens another), then filler fills the rest. `PortRandoLogic::Beatable` checks the
     result (the sphere model the vanilla test uses, `Collect`); a failed attempt retries with the
     continued PRNG, up to 64. `port_rando_gen_tests`: 200/200 seeds beatable on the first attempt,
     5-13 spheres (average 9.4), about 1 ms per seed.
5. **Non-pickup grants** - items a script grants (the energy controllers' translators, ...).
6. **Save binding and the F1 page** - seed hash in the save, spoiler. The F1 Randomizer page is
   hidden (`platform/debug_ui/search.cpp`) until then.

Disc findings:

- Dark-world areas are areas of their light world's MLVL.
- The Bomb Guardian's drop (Agon Temple `081A0074`) is not in any script layer but in the MREA's
  generated-object section (`SCGN`, header word 19; `tools/echoes_disc.py` `mrea_generated`).
  `CScriptObjectLoaderHelper::GenerateScriptObject` builds it with its own editor id through the same
  `LoadPickup`, so the hook keys it like any other pickup.
- The Energy Transfer Module location gives item 28 (`TranslatorUpgrade_TempETM`), not
  `EnergyTransferModule` (107).

## Decision (2026-10-18) - superseded 2026-10-27

Superseded: locations now come from the disc (step 1) and the generator fills with logic (step 4).
Kept for its reasoning.

MP2 gets a **no-logic shuffle generator over a dumped location list**, not a port of the MP1
generator. `../MetroidPrimePort/platform/port_rando_gen.cpp` is the Archipelago apworld
in C++: its locations are the fixed table of MP1 `5031xxx` ids and its fill is
driven by region/item logic (`PortApLogic`, elevators, blast shields, start rooms). MP2 has none of
that data, and its pickups cannot be enumerated statically: they exist only as `Pickup` script
objects inside each area's MREA, so the only list the port can get is the one the game itself reports
as areas load (`MP_RANDO_DUMP`). The phone's `/sdcard/MetroidPrime/randomizer_seeds/*.json` are MP1
Archipelago seeds (`placements` keyed by `5031xxx`, `start_items`, `slot_data`), not MP2 seeds, and
the MP2 loader's grammar (`"locations"` keyed `WORLD:AREA:ENTITY`) does not read them. So:
dump the locations by visiting areas, then shuffle their items with a deterministic PRNG. Seeds may
be unbeatable (no logic) and only cover areas that were dumped.

## Workflow

1. Generate: `MP_RANDO_GEN=<seed name>` (without dump mode), or the F1 Randomizer page's Generate
   button, fills all 114 locations with logic (no dump needed) and writes the seed JSON to
   `MP_RANDO_SEED` (default `<user>/randomizer_seed.json`); the env var then loads it in the same
   process. Same name + same build = same seed.
2. Play: leave `MP_RANDO_GEN` off; the seed file is loaded at startup.
3. Optional, for checking keys in game: `MP_RANDO_DUMP=1` logs each pickup load as
   `LOC <WORLD>:<AREA>:<ENTITY> <Item> amount=<n> capacity=<n>` to `<user>/randomizer_locations.log`.
   The generator does not read it.

`<user>` is `MP2_USER_PATH` (then `MP_USER_PATH`, the Prime 1 port's name) or SDL's pref path. Code: `platform/port_rando_gen.cpp`,
`platform/port_randomizer.cpp`.

## Notes

- Everything is lazy: the seed is loaded (or generated) at the first pickup load, so a run that never
  enters an area with a pickup (Temple Grounds area 0, for one) prints nothing and writes no file. Use
  area 7 to see it in a short run.
- `kItems` in `port_randomizer.cpp` is Echoes' `EItemType`. It used to be Prime 1's, so every Echoes
  item but the bombs came out as `Unknown`.
- Verified 2026-10-18: a 5-shard sweep (`tools/sweep_areas.sh`, `DWELL=500`) with `MP_RANDO_DUMP=1`
  logged 18 distinct locations (Temple Grounds); `MP_RANDO_GEN=echoes1` then wrote a seed with all 18
  and the next run loaded it (`randomizer: seed 'echoes1', 18 placements`).
- Not done: start items, non-pickup grants (step 5), and seeing step 3's model/scan text in game.
  Beatable seeds and all five worlds' locations are done (steps 1 and 4).
