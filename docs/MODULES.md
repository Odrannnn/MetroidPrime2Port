# REL module manifest (Echoes)

Bookkeeping for the 86 REL modules in `config/G2ME01/rels/`. A REL's class name is not always the
in-game enemy (`ElitePirate` is the **Ingsmasher**), and a module can have no body at all (the 2
`entry-stub` ones load and do nothing), so keep these three files current:

- `docs/modules.tsv` - one row per module, written by `python3 tools/rel_manifest.py`.
- `docs/module_spawns.tsv` - every (module, world, area, layer) the module is listed for (full list).
- `docs/progress.tsv` - dated coverage counts, appended by `python3 tools/port_progress.py --record`.

`rel_manifest.py` reads the user's own disc (`MP2_DISC`, else the ISO path in `tools/echoes_disc.py`);
without one the name/spawn columns are `?`. A run takes a few seconds (~5 s with the disc cached).
`tools/echoes_disc.py` is the minimal ISO/PAK/MLVL/MREA/SCLY/STRG reader behind it (pure Python, LZO
included); `rel_manifest.py` itself scans the MREA objects' properties for SCAN ids.

## Columns of `modules.tsv`

| column | meaning |
|---|---|
| `module` | REL name (directory in `config/G2ME01/rels/`) |
| `id` | module id from the REL header; it is the `N` in `fn_N_<off>` symbols and in markers |
| `text_bytes`, `functions` | sum of sizes / count of `type:function` symbols in the module's `symbols.txt` |
| `status` | `upstream`: real decomp source is built. `port-body`: only `Port*.cpp` sources (in `tools/modules_port.txt`) with real code. `entry-stub`: `PortModuleStubs.cpp` only - loads, does nothing. Read from `files.cmake` |
| `source` | the module's source files from `files.cmake` (`MP_MODULE_<Module>_SOURCES`, the module's name) |
| `script_types` | FourCCs of the script objects the module loads (`ScriptLoader.cpp`). `*` = no object of that type exists on the disc, so the mapping is by name only |
| `in_game_name` | scan title (first line of the STRG a SCAN refers to) of objects of that type, up to 3, most frequent first; only `Category: Name` scans count. `?` = none provable (no scannable object, or the type is not an enemy) |
| `areas`, `spawns` | how many areas have an MLVL entry that lists the module for some layer, and which: `world/area`, `[L3,5]` = layers other than 0 (layer 0 alone has no tag); first 5 shown, then `(+N more)` |

### How the data is derived (no guessing)

- **Spawns**: each MLVL area record carries the REL module names and, per layer, the slice of that list
  the layer needs (`CGameArea::LoadLayerRelModules`). That is the world/area/layer a module is loaded for.
- **Type -> module**: a script FourCC is mapped to a module by the loader name in `ScriptLoader.cpp`
  (`LoadElitePirate` -> `ElitePirate`, with a few aliases in `ALIASES`), and the pair is accepted only
  if *every* layer that holds an object of that type lists the module. That check is the proof.
- **Names**: every object of the type in every MREA script layer is searched for u32 values that are
  SCAN asset ids on the disc (properties are an unaligned stream, so every offset is tried); the SCAN's
  STRG title is the name. A scan is attached to the object that holds it, so a generated sub-object's
  scan can show up under its parent type; the top-3 list shows that.
- Generic scans ("Bioscan complete.") and bare sentences are dropped.

## Port-progress markers (`tools/port_progress.py`)

Mark a ported function on the line above its definition:

    // @ fn_15_C8D8
    static void ElitePirateSomething(...) { ... }

`fn_<id>_<hexoffset>` is the retail function's name in `symbols.txt` (module id + `.text` offset; named
symbols such as `RELMain` use the same spelling with their offset, e.g. `fn_15_128`). Rules:

- Only code that has been checked against the retail behaviour gets a marker. A stand-in that logs,
  returns a constant or is a guess does not - a stub is not progress.
- One marker per retail function replaced; several on one definition if it folds several.
- Markers are scanned in `src/`, `platform/`, `include/`. A marker naming a function that is not in any
  `symbols.txt` is reported as a warning.
- `python3 tools/port_progress.py [--all]` prints per-module covered/total functions and bytes and the
  totals per status; `--record` appends a dated row to `docs/progress.tsv`.
- `upstream` modules are real decomp and are not counted as covered unless marked; the marker count is
  mainly for `port-body` and `entry-stub` modules.
