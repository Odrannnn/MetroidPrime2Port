# Mods folder

Ported from MP1's first mods slice (MP1 `05cbf722a`): loose-file and PAK overrides plus a Mods list on
F1's Mods page. Suit colours and the HD texture pack were added later (sections below); downloads are
not part of it.

`mods` in the user path (`MP2_USER_PATH`, created on first start; `MP2_MODS` points elsewhere) holds one
folder per mod, applied in name order so a later name wins; folders starting with `.` are skipped.
Inside a mod:

- a file at a disc path (`Metroid5.pak`, `Audio/...`, `Video/...`; case-insensitive) replaces that disc
  file. Only existing disc files can be replaced; a `.thp` the disc lacks is added instead (the game
  opens movies by name); anything else is reported and ignored.
- a file named `<8 hex digits>.<TYPE>` (`1A2B3C4D.TXTR`, `0552A456.STRG`), anywhere in the mod, replaces
  that resource, uncompressed, in every PAK holding it. The PAK is served as a virtual file whose table
  points past the original data, where the loose file is appended (32-byte aligned).
- `.txt/.md/.json/.png/...` files are ignored silently.

`platform/port_mods.cpp` (scan, plan, register with Aurora's DVD overlays) runs from `main.cpp` right
after the disc mounts; `platform/port_mods_pak.cpp` is the unit-tested PAK table / virtual file / reader
half (`tests/port_mods.cpp`, ctest `port_mods_tests`), and `platform/include/port_mods_disc.h` lists the
disc's PAKs (`tests/port_mods_disc.cpp`, `port_mods_disc_tests`). PAK format is shared with Prime 1 (version
0x00030005, 20-byte table entries), so the parser is unchanged. Log lines start with `mods:` on stderr.
F1 > Mods lists the mods with a switch each (`mods_disabled`), the Load mods switch (`mods`) and the
problems found; changes apply on Reload mods or a restart. An absent or empty folder registers no overlay
at all.

## Suit colours

Video > Display > Suit colours recolours Samus's Varia (2 slots), Dark (1) and Light (2: Accent = gold trim, Main = silver/white body) suit
textures from the disc (`platform/port_suit_colours*.cpp`; the HSV remap is Metroid Prime's, the
texture ids are Echoes' own). They are generated at startup in `PortMods::Initialize` as loose
`.TXTR` resources, so they replace the texture in every PAK that holds it and a mod's own texture
for the same id wins. Nothing is generated while all picks are default, so rendering is unchanged.
Cache: `<user>/generated/suit-colours/<suit>/` (stamp covers picks and PAK offsets).

How the ids were found: each suit's CMDL (arm cannon, morph ball, beam/visor pieces, suit models)
was parsed for its material texture list, and the textures that carry the suit's orange, copper or
white-blue colouring were kept (tables in `port_suit_colours_disc.cpp`). The Light Suit's Main slot is
not a hue cluster: low-saturation, not-dark texels of its four body textures take the pick's hue and
saturation and keep their value (glow, lens, trim and dark-metal texels stay; Dark and Varia are
coloured enough that they need no main slot). A saved `suit_colour_light=RRGGBB` is the accent;
`-,RRGGBB` is main only. Limits: the Samus body model in cutscenes and some pause screen meshes
were not located, so they keep original colours; the first-person arm cannon is not the suit and
stays as is. The pause menu's Inventory Varia Suit model (LogBook.pak sheets) is recoloured too.
Changes show after a restart or the Apply suit colours button (`reloadmods`), also with the pause menu
open: `CMFGame` services the reload while paused and `CPauseScreen::PortReloadScanModels` reloads
the open Inventory model (the room follows when unpaused).

## Reload, folder, texture pack (MP1 sync 21)

F1 > Mods has Reload mods and Open mods folder (desktop), then the Textures section: the built-in HD set's
name, how many of the pack's textures the game has used so far (and how many failed to load), a note naming the disc's game code and the pack folder that fits it (G2M, or
R3M on Trilogy), the Use the texture pack switch (`texture_pack`), Reload texture pack and
Import texture pack .zip... (desktop). Android: Choose texture pack folder... copies the pick to
`<user>/user_textures.new`, which `PortTextures` swaps in (Remove texture pack deletes it). Console:
`reloadmods`.

Reload is a savestate-style world restart: `PortSaveState::RequestModReload` writes the current state
to the undo slot and loads it back; `CMainFlow` (`kCFS_Game`) calls `ReloadModsNow` after the old world
is gone, which closes every PAK, runs `PortMods::Reload` (rescan, overlay rebuilt through
`aurora_dvd_overlay_files`) and reopens them (`CResLoader::PortReopenPaks`). Needs a running game in a
world; from the title screen restart instead. The built-in HD texture set can win over a replaced TXTR.
