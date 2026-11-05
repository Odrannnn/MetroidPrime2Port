# Echoes save layouts across releases

This is research for carrying a save from one disc release to another, as the Prime 1 port can.
It describes the formats and the converter (`platform/port_save_convert.cpp`, hooked into `port::save::Import()`); the restore offer is `PortSave::RestoreToOffer`/`RestoreFrom` (boot prompt, overlay button, `gci restore`). Measured on 2026-10-27.

Each claim carries one of two labels:

- **[M]** means measured. The note says how: a tool's output, a source line, a DOL or symbol scan.
- **[I]** means inferred from the code, and not checked against real data.

| Release | Disc used | Status |
| --- | --- | --- |
| G2ME01 (USA GC) | USA GC ISO | Measured |
| G2MP01 (PAL GC) | `G2MP01.iso` | Measured |
| R3ME01 (Trilogy USA Wii) | Trilogy USA image, partition DATA, `MP2/` | Measured |
| R3MP01 (Trilogy PAL) | — | Measured 2026-11-05: its Echoes SAVW lists (every world, scan union 817, system/game variables), HINT bytes and card signature (`fe44ecfe`) equal R3ME01's (`savw_diff.py`), and `trilogy_tables.py` writes byte-identical tables from it. |

The paks and DOLs were extracted read-only, with `dolphin-tool extract`, to `/tmp/opencode/discs/<id>/`.

## 1. Container

### GameCube (G2ME01, G2MP01)

| Field | G2ME01 | G2MP01 | Source |
| --- | --- | --- | --- |
| File name | `MetroidPrime2` | same | [M] Both DOLs contain the string once; `skSaveFileName` |
| Game code / maker | `G2ME` / `01` | `G2MP` / `01` | [M] `CARDInit(<disc code>, "01")`; `platform/port_save.cpp` |
| Comment | `Metroid Prime 2 Echoes          ` + `%02d.%02d.%02d  %02d:%02d` | same | [M] The string is in both DOLs; `CMemoryCardDriver.cpp:154-158` |
| Banner | `TXTR_SaveBanner` | same | [M] The string is in both DOLs; `CSaveGameScreen.cpp:17` |
| Blocks | 3: one header block, plus 2 copies × 1 block | same | [M] `CCardFileInfo`: `mSlotBlocks*2+1`, `mAlignedUserDataSize` 0x2000 |
| Header CRC | CCRC32 of header bytes 4..0x2000, seed 0xFFFFFFFF | same | [M] `src/Kyoto/DolphinCMemoryCardSys.cpp` |
| Copy | u32 CRC (bytes 4..end), u32 generation (0/1, newest wins), 0x1ff8 user bytes | same | [M] Same source; `port_save.cpp` header comment |
| Endianness | Console and new port writes: big-endian. Legacy port CRC and generation: host-LE. | same | [M] `port_save_framing.h`; runtime loader and `port_save.cpp` `Inspect()` accept either |

The `.gci` adds a 0x40 directory entry in front. The rest of the file is the card image. In a `.gci`, the first copy's CRC is at 0x2040, its generation at 0x2044 and the payload (`SSaveHeader` signature first, big-endian) at 0x2048. The second copy starts at 0x4040 [M, the port save in §5].

The two GC DOLs have different sha1s:

- G2ME01: `6ef9b491d0cc08bc81a124fdedb8bfaec34d0010`
- G2MP01: `5a670d5da3d181e86a0df7cf7751c7055eee35fb` [M, `sha1sum` of the extracted `sys/main.dol`]

Every save-serialisation function has the same size in both, from `config/G2M?01/symbols.txt` [M]:

| Function | Size in both DOLs |
| --- | --- |
| `CGameState::PutTo` | 0x36C |
| `CGameState(CBitStreamReader&)` | 0x684 |
| `CMemoryCardDriver::InitializeFileInfo` | 0x1BC |
| `CPlayerState::PutTo` | 0x210 |
| `CWorldState::PutTo` | 0x94 |
| `CScriptMailbox::PutTo` | 0x148 |
| `CMapWorldInfo::PutTo` | 0x2D8 |
| `CWorldLayerState::PutTo` | 0xF0 |
| `CPersistentOptions::PutTo` | 0x258 |
| `CGameOptions::PutTo` | 0x23C |
| `SGameFileSlot::PutTo` | 0x30 |

`kPowerUpMax` (0x1B4) and `kShouldPersist` (0x6D, 59 persisted items) are byte-identical [M]. Measured by taking the sha1 of the rodata bytes at each DOL's symbol address.

### Trilogy (R3ME01; R3MP01 has the same save data)

What is measured, from string and immediate scans of the DOLs [M]:

- `rs5mp2_p.dol` (Echoes) contains `save.bin` and the RVL NAND SDK.
- `rs5fe_p.dol` (front end) contains `save.bin`, `banner.bin`, `TXTR_MP2SaveBanner`, `TXTR_MP1SaveBanner` and `TXTR_RS5SaveBanner`.
- Neither DOL contains `MetroidPrime2`, the GC comment, or `TXTR_SaveBanner`.
- `config/R3ME01/symbols.txt` names only `CSaveGameInterface` and the NAND SDK functions.

What this implies [I]:

- All three games share one NAND `save.bin`, owned by the front end.
- There is no GCI and no `CARD*` container.

**Superseded 2026-10-27: the framing of `save.bin` is known** (measured on the Prime 1 port's real Trilogy fixtures, USA and PAL). The file is plaintext in Dolphin's NAND, 0x1C000 bytes, with a big-endian CRC32 at 0 over [4, 0x1980C); Echoes' summary is at +0x70 of each 0x258-byte file summary (0x64, 0x2BC, 0x514) and Echoes' three slots are at 0x3660, 4000 bytes each (part A the options, part B the GMST stream; `SYST` at +0x14 of A, `GMST` at +0x14 of B), with no two-copy/generation scheme. Code: `platform/port_save_trilogy.cpp`. The SD-export `data.bin` is still encrypted and out of scope.

## 2. Payload (user data, 0x1ff8 bytes)

Sources are `CMemoryCardDriver::BuildSaveBuffer`, `SSaveHeader`, `SGameFileSlot` and `CGameState::PutTo` in `src/` [M]. This is the G2ME layout. Function sizes say G2MP is the same; the Trilogy notes follow the tables.

### Top-level user data

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 15 | `SSaveHeader`: u32 signature, s32 saveIdx, 3 × bool slot-present, u32 `SAVH` |
| 15 | 192 | System data: `CPersistentOptions` bitstream |
| 207 | 3 × 32 | Per-slot `CGameOptions` bitstream |
| 303 | 32 | Global (multiplayer) options |
| 335 | 0xa38 each | Present `SGameFileSlot`s, in slot order. 335 + 3×0xa38 = 0x1ff7, which is ≤ 0x1ff8. |

### Signature

The signature is `0x5553413e ^ Σxor CWorldSaveGameInfo::CalculateHash()`, taken over every world in MemoryWorlds.

- A mismatch marks the save invalid (`CMemoryCardDriver::UpdateFileRead` → `kE_FileCorrupted`).
- The console hashes its big-endian words. The port swaps each 32-bit word before `CCRC32::Calculate` (`CalculateHash(bool consoleOrder = true)`, `TARGET_PC`), so it writes the console value. It also accepts the value older port builds wrote, which hashed host-LE words. At startup it logs both: `[save] console signature 44bfc715` and `[save] legacy port signature 37d0412e` on G2ME01 [M, port run].
- The hash covers areaCount, then the cinematic, relay, door, layer, scan and unmappable arrays, in that order.
- It is computed as CRC32 over the **in-memory** words. The CRC is seeded with the CRC of the env-var names, with no final xor.

### `CPersistentOptions` bitstream

The fields, in order:

1. `SYST` (32 bits).
2. saveIdx (2 bits).
3. One bit per cinematic of every SAVW, in MemoryWorlds order.
4. System env vars, in name order (`rstl::map`), each `GetBitCount(max-min)` bits.
5. `SYND`.

### `CGameOptions` bitstream

It starts with `OPTN`. The field list was not audited for this document; see `CGameOptions::PutTo`. Its size (0x23C) is equal in G2ME and G2MP.

### `SGameFileSlot` (0xa38) = `CGameState::PutTo` bitstream

| Order | Field | Bits |
| --- | --- | --- |
| 1 | `GMST`, timestamp, hardMode, initPowerups, isDarkWorld | 32, 32, 1, 1, 1 |
| 2 | desiredWorld (MLVL ID), playTime (double) | 32, 64 |
| 3 | 4 × `CPlayerState`: `PLST`, enabled 32, HP 32, beam 3, suit 2, sel 3, team 3, scheme 2 | per player |
| | `PRST`, amount and capacity for each persisted powerup | 186 |
| | `POWR`, 2 bits × (4 fixed + **scan union**), scan rates 9+9, `SCAN` | per player |
| 4 | `CHintOptions`: per `HINT_Hints` entry, a state plus a 32-bit time | count from HINT |
| 5 | `PREV` multiplayer results | |
| 6 | Byte-align. Then escapeTime f32 and the game env vars. | |
| 7 | u8 worldCount. Per world: aligned u32 MLVL ID, u16 bitCount, then the `CWorldState` bits. | |
| 8 | u32 `GMND` | |

The world records are written in MemoryWorlds order, which is every MLVL sorted by ID.

`CWorldState` bits, in order:

1. areaId 32 and desiredAreaAssetId 32.
2. One bit per SAVW relay.
3. visited, then mapped: one bit per area each.
4. One bit per door.
5. One bit per unmappable object.
6. mapStationUsed, 1 bit.
7. layerTotal, 10 bits.
8. One bit for each layer 1..n-1 of each area, with n taken from the MLVL layer table.

How the reader treats these records:

- It skips a world whose MLVL ID it does not know, using the record's bitCount.
- It parses a known world with its **own** SAVW.
- So bitCount lets a converter split the records, but the reader does not use it to resync.

### Trilogy differences

Measured by scanning the DOLs for `lis`+`addi`/`ori` FourCC pairs and `li` immediates [M]:

| Pattern | G2ME main.dol | G2MP main.dol | R3ME rs5mp2_p.dol |
| --- | --- | --- | --- |
| `SAVH` | 1 | 1 | **0** |
| `GMST` / `GMND` / `PLST` / `PRST` / `POWR` / `PREV` / `SYST` | 1 each | 1 each | 1 each |
| `SYND` | 1 | 1 | **0** |
| `OPTN` | 1 | 1 | 2 |
| `li 0xa38` | 16 | 16 | **0** |
| `li 0x1ff8` | 5 | 5 | **0** |

`kPowerUpMax` and `kShouldPersist` bytes from G2ME occur exactly once in `rs5mp2_p.dol` [M].

What this suggests [I]:

- The `CGameState` and `CPlayerState` bitstream framing survives in Trilogy.
- The GC `SSaveHeader`, the options packing and the 0xa38 slot do not.
- `SYND` is missing from the Echoes DOL but present in `rs5fe_p.dol`, so the system options may have moved to the shared front end.

Trilogy slot size: 4000 bytes in `save.bin`, as two 2000-byte parts (see the §1 note, 2026-10-27). The payload differs from GC's: part B has the timestamp, then 2 flag bits and the world id, where GC's `PutTo` writes 3 bits.

## 3. World data — `tools/savw_diff.py`

Every release has 12 MLVLs [M]:

- Seven are shared by ID: Temple Grounds `3bfa3eff`, Great Temple `863fcd72`, Agon `42b935e4`, Torvus `3dfd2249`, Sanctuary `1baa96c2`, and the Metroid6 group of six multiplayer worlds.
- The **FrontEnd world ID differs in each release**:
  - G2ME `69802220`
  - G2MP `7b6eaa68`
  - R3ME `daea74a3`

  Its SAVW content is identical (hash `44bc9114`). In Trilogy the new ID sorts later, which moves its record in MemoryWorlds order.
- MLVL area lists, as (MREA, save ID) pairs, are identical in every shared world [M].

Summary of the output below [M]:

| Item | G2ME01 | G2MP01 | R3ME01 |
| --- | --- | --- | --- |
| Signature, console (BE words; the port writes this) | `44bfc715` | `c13c33f1` | `fe44ecfe` |
| Signature, legacy port (LE words; older port builds; accepted on read, see §6) | `37d0412e` | `467484b3` | `9c307b43` |
| Scan union | 815 | 818 | 817 |
| Cinematic bits | 1064 | 1064 | 1030 |
| System / game vars | 19 / 1 | 19 / 1 | 19 / 1 |
| `HINT_Hints` count | 44 | 44 (bytes differ) | 42 |
| Temple Grounds area 6 layers | 8 | 9 (`Missile Trooper Control` appended) | 9 |

### Per-world differences against G2ME01 [M]

| World | G2MP01 | R3ME01 |
| --- | --- | --- |
| Sanctuary `1baa96c2` | Scans reordered | −8 cinematics, +1 relay, 1 door swapped, scans reordered |
| Temple Grounds `3bfa3eff` | +1 layer, 1 relay swapped, 1 scan swapped | +1 layer, −2 cinematics, relays −2/+3, scans ±2 |
| Torvus `3dfd2249` | Identical | −10 cinematics, 1 scan swapped |
| Agon `42b935e4` | +2 scans | −10 cinematics, doors reordered, scans −2/+3 |
| Great Temple `863fcd72` | Identical | −4 cinematics, −1 relay |
| FrontEnd and multiplayer worlds | Identical (FrontEnd ID renamed) | Identical (FrontEnd ID renamed) |

Full output of `cd /tmp && python3 -I <worktree>/tools/savw_diff.py`, with the defaults being the three extracted trees:

```
== G2ME01
  MemoryWorlds (12, sorted by MLVL ID):
    MLVL     SAVW     pak             areas  ver cine rely lyrs door scan unmp  wbits   hashBE   hashLE
    1baa96c2 8754d2a9 Metroid5.pak       67    5  228  180    0  124  215    2    824 ecf2d615 03d94b07
    233e42be bfc006d5 Metroid6.pak        1    5    3    0    0    0    0    0     81 33b30df0 d35bdabd
    3bfa3eff a7047a94 Metroid1.pak       63    5  163  216    0  115  333    7    814 a8f7fc2b 2680e074
    3dfd2249 a1036622 Metroid4.pak       70    5  228  138    0  154  209    2    824 7c5fe2e9 88b80949
    406add7f dc949914 Metroid6.pak        1    5    3    0    0    0    0    0     80 45eda14c 81913db9
    42b935e4 de47718f Metroid3.pak       72    5  280  238    0  162  282    2    949 82f06d8c f95814cb
    69802220 f57e664b FrontEnd.pak        1    5    1    0    0    0    0    0     80 44bc9114 b1a24f14
    7e19ed26 e2e7a94d Metroid6.pak        1    5    3    0    0    0    0    0     80 be9602ac 03254262
    863fcd72 1ac18919 Metroid2.pak       12    5  146   22    0   20   69    3    223 8b416c0e afb5af04
    a50a80cc 39f4c4a7 Metroid6.pak        1    5    3    0    0    0    2    0     80 ba64b6d3 c4ab8e3a
    ae171602 32e95269 Metroid6.pak        1    5    3    0    0    0    0    0     80 5009de08 e5da5dad
    e3b0c703 7f4e8368 Metroid6.pak        1    5    3    0    0    0    0    0     81 461e18a1 58b32000
  signature console (BE words; port writes) 44bfc715   legacy port (LE words) 37d0412e
  scan union 815 entries (+4 fixed) -> POWR..SCAN scan bits 1638
  cinematic bits 1064; system vars 19 (29 bits); game vars 1 (1 bits)
  HINT_Hints 2255c7bc v1 count 44 crc 302f9b1d (NoARAM.pak)
== G2MP01
  MemoryWorlds (12, sorted by MLVL ID):
    MLVL     SAVW     pak             areas  ver cine rely lyrs door scan unmp  wbits   hashBE   hashLE
    1baa96c2 8754d2a9 Metroid5.pak       67    5  228  180    0  124  215    2    824 3f0b61c5 370cc7c9
    233e42be bfc006d5 Metroid6.pak        1    5    3    0    0    0    0    0     81 33b30df0 d35bdabd
    3bfa3eff a7047a94 Metroid1.pak       63    5  163  216    0  115  333    7    815 a0042ea1 523616a0
    3dfd2249 a1036622 Metroid4.pak       70    5  228  138    0  154  209    2    824 7c5fe2e9 88b80949
    406add7f dc949914 Metroid6.pak        1    5    3    0    0    0    0    0     80 45eda14c 81913db9
    42b935e4 de47718f Metroid3.pak       72    5  280  238    0  162  284    2    949 dc79fc32 c89fab4c
    7b6eaa68 e790ee03 FrontEnd.pak        1    5    1    0    0    0    0    0     80 44bc9114 b1a24f14
    7e19ed26 e2e7a94d Metroid6.pak        1    5    3    0    0    0    0    0     80 be9602ac 03254262
    863fcd72 1ac18919 Metroid2.pak       12    5  146   22    0   20   69    3    223 8b416c0e afb5af04
    a50a80cc 39f4c4a7 Metroid6.pak        1    5    3    0    0    0    2    0     80 ba64b6d3 c4ab8e3a
    ae171602 32e95269 Metroid6.pak        1    5    3    0    0    0    0    0     80 5009de08 e5da5dad
    e3b0c703 7f4e8368 Metroid6.pak        1    5    3    0    0    0    0    0     81 461e18a1 58b32000
  signature console (BE words; port writes) c13c33f1   legacy port (LE words) 467484b3
  scan union 818 entries (+4 fixed) -> POWR..SCAN scan bits 1644
  cinematic bits 1064; system vars 19 (29 bits); game vars 1 (1 bits)
  HINT_Hints 2255c7bc v1 count 44 crc ab8f7068 (NoARAM.pak)
== R3ME01
  MemoryWorlds (12, sorted by MLVL ID):
    MLVL     SAVW     pak             areas  ver cine rely lyrs door scan unmp  wbits   hashBE   hashLE
    1baa96c2 8754d2a9 Metroid5.pak       67    5  220  181    0  124  215    2    825 f9d952e1 647696bf
    233e42be bfc006d5 Metroid6.pak        1    5    3    0    0    0    0    0     81 33b30df0 d35bdabd
    3bfa3eff a7047a94 Metroid1.pak       63    5  161  217    0  115  333    7    816 4fe85b0b 60e3dc36
    3dfd2249 a1036622 Metroid4.pak       70    5  218  138    0  154  209    2    824 41142120 88d13eed
    406add7f dc949914 Metroid6.pak        1    5    3    0    0    0    0    0     80 45eda14c 81913db9
    42b935e4 de47718f Metroid3.pak       72    5  270  238    0  162  283    2    949 c6e81ec7 0371def0
    7e19ed26 e2e7a94d Metroid6.pak        1    5    3    0    0    0    0    0     80 be9602ac 03254262
    863fcd72 1ac18919 Metroid2.pak       12    5  142   21    0   20   69    3    222 baddd4b3 dfd9890c
    a50a80cc 39f4c4a7 Metroid6.pak        1    5    3    0    0    0    2    0     80 ba64b6d3 c4ab8e3a
    ae171602 32e95269 Metroid6.pak        1    5    3    0    0    0    0    0     80 5009de08 e5da5dad
    daea74a3 461430c8 FrontEnd.pak        1    5    1    0    0    0    0    0     80 44bc9114 b1a24f14
    e3b0c703 7f4e8368 Metroid6.pak        1    5    3    0    0    0    0    0     81 461e18a1 58b32000
  signature console (BE words; port writes) fe44ecfe   legacy port (LE words) 9c307b43
  scan union 817 entries (+4 fixed) -> POWR..SCAN scan bits 1642
  cinematic bits 1030; system vars 19 (29 bits); game vars 1 (1 bits)
  HINT_Hints e1729bc1 v1 count 42 crc e709f7c0 (NoARAM.pak)
== diff G2ME01 -> G2MP01
  worlds only in G2ME01: ['69802220']
  worlds only in G2MP01: ['7b6eaa68']
  world 1baa96c2: 
    scans: same 215 entries, different order
    hash ecf2d615 -> 3f0b61c5
  world 233e42be: identical
  world 3bfa3eff: 
    layer counts differ in 1 areas: [(6, 8, 9)]
      area 6 base   ['Default', '!No Load', '1st Pass', 'Map Station Cinematic', 'Post Dark Trooper', 'Pre Dark Trooper', 'Missile Trooper', 'Missile trooper gate']
      area 6 other  ['Default', '!No Load', '1st Pass', 'Map Station Cinematic', 'Post Dark Trooper', 'Pre Dark Trooper', 'Missile Trooper', 'Missile trooper gate', 'Missile Trooper Control']
    relays: 216 -> 216; only base 1, only other 1; common entries keep relative order
    scans: 333 -> 333; only base 1, only other 1; common entries reordered
    CWorldState bits 814 -> 815
    hash a8f7fc2b -> a0042ea1
  world 3dfd2249: identical
  world 406add7f: identical
  world 42b935e4: 
    scans: 282 -> 284; only base 0, only other 2; common entries reordered
    hash 82f06d8c -> dc79fc32
  world 7e19ed26: identical
  world 863fcd72: identical
  world a50a80cc: identical
  world ae171602: identical
  world e3b0c703: identical
  signature console 44bfc715 -> c13c33f1
    scan union: 815 -> 818; only base 0, only other 3; common entries keep relative order
    sysvars identical (19)
    gamevars identical (1)
    HINT count 44 -> 44, bytes differ
== diff G2ME01 -> R3ME01
  worlds only in G2ME01: ['69802220']
  worlds only in R3ME01: ['daea74a3']
  world 1baa96c2: 
    cinematics: 228 -> 220; only base 8, only other 0; common entries keep relative order
    relays: 180 -> 181; only base 0, only other 1; common entries keep relative order
    doors: 124 -> 124; only base 1, only other 1; common entries keep relative order
    scans: same 215 entries, different order
    CWorldState bits 824 -> 825
    hash ecf2d615 -> f9d952e1
  world 233e42be: identical
  world 3bfa3eff: 
    layer counts differ in 1 areas: [(6, 8, 9)]
      area 6 base   ['Default', '!No Load', '1st Pass', 'Map Station Cinematic', 'Post Dark Trooper', 'Pre Dark Trooper', 'Missile Trooper', 'Missile trooper gate']
      area 6 other  ['Default', '!No Load', '1st Pass', 'Map Station Cinematic', 'Post Dark Trooper', 'Pre Dark Trooper', 'Missile Trooper', 'Missile trooper gate', 'Missile Trooper Control']
    cinematics: 163 -> 161; only base 2, only other 0; common entries keep relative order
    relays: 216 -> 217; only base 2, only other 3; common entries reordered
    scans: 333 -> 333; only base 2, only other 2; common entries reordered
    CWorldState bits 814 -> 816
    hash a8f7fc2b -> 4fe85b0b
  world 3dfd2249: 
    cinematics: 228 -> 218; only base 10, only other 0; common entries keep relative order
    scans: 209 -> 209; only base 1, only other 1; common entries reordered
    hash 7c5fe2e9 -> 41142120
  world 406add7f: identical
  world 42b935e4: 
    cinematics: 280 -> 270; only base 10, only other 0; common entries keep relative order
    doors: same 162 entries, different order
    scans: 282 -> 283; only base 2, only other 3; common entries reordered
    hash 82f06d8c -> c6e81ec7
  world 7e19ed26: identical
  world 863fcd72: 
    cinematics: 146 -> 142; only base 4, only other 0; common entries keep relative order
    relays: 22 -> 21; only base 1, only other 0; common entries keep relative order
    CWorldState bits 223 -> 222
    hash 8b416c0e -> baddd4b3
  world a50a80cc: identical
  world ae171602: identical
  world e3b0c703: identical
  signature console 44bfc715 -> fe44ecfe
    scan union: 815 -> 817; only base 1, only other 3; common entries keep relative order
    sysvars identical (19)
    gamevars identical (1)
    cinematic bits 1064 -> 1030
    HINT count 44 -> 42, bytes differ
== diff G2MP01 -> R3ME01
  worlds only in G2MP01: ['7b6eaa68']
  worlds only in R3ME01: ['daea74a3']
  world 1baa96c2: 
    cinematics: 228 -> 220; only base 8, only other 0; common entries keep relative order
    relays: 180 -> 181; only base 0, only other 1; common entries keep relative order
    doors: 124 -> 124; only base 1, only other 1; common entries keep relative order
    scans: same 215 entries, different order
    CWorldState bits 824 -> 825
    hash 3f0b61c5 -> f9d952e1
  world 233e42be: identical
  world 3bfa3eff: 
    cinematics: 163 -> 161; only base 2, only other 0; common entries keep relative order
    relays: 216 -> 217; only base 1, only other 2; common entries reordered
    scans: 333 -> 333; only base 1, only other 1; common entries reordered
    CWorldState bits 815 -> 816
    hash a0042ea1 -> 4fe85b0b
  world 3dfd2249: 
    cinematics: 228 -> 218; only base 10, only other 0; common entries keep relative order
    scans: 209 -> 209; only base 1, only other 1; common entries reordered
    hash 7c5fe2e9 -> 41142120
  world 406add7f: identical
  world 42b935e4: 
    cinematics: 280 -> 270; only base 10, only other 0; common entries keep relative order
    doors: same 162 entries, different order
    scans: 284 -> 283; only base 2, only other 1; common entries reordered
    hash dc79fc32 -> c6e81ec7
  world 7e19ed26: identical
  world 863fcd72: 
    cinematics: 146 -> 142; only base 4, only other 0; common entries keep relative order
    relays: 22 -> 21; only base 1, only other 0; common entries keep relative order
    CWorldState bits 223 -> 222
    hash 8b416c0e -> baddd4b3
  world a50a80cc: identical
  world ae171602: identical
  world e3b0c703: identical
  signature console c13c33f1 -> fe44ecfe
    scan union: 818 -> 817; only base 1, only other 0; common entries keep relative order
    sysvars identical (19)
    gamevars identical (1)
    cinematic bits 1064 -> 1030
    HINT count 44 -> 42, bytes differ
```

## 4. Conversion feasibility per ordered pair

The per-player options and header fields map 1:1 in every pair [I]. The `PLST` fields and `PRST` are equal, because the tables are equal.

| Pair | Verdict | What must be rewritten |
| --- | --- | --- |
| G2ME → G2MP | **Remap by ID** | See below |
| G2MP → G2ME | **Remap by ID** (lossy) | The inverse of the above. Drop the PAL-only scans, relay and layer bit. |
| G2ME → R3ME | **Done (2026-10-27), unverified in the Trilogy** | `port_save_trilogy.cpp` (container) and `port_save_trilogy_payload.cpp` (stream, remap by ID with the tables `tools/trilogy_tables.py` writes from both discs). Wii part B = 20 zero bytes, GMST stream with 2 flag bits (hard, dark; the GC's initPowerups bit is dropped and the card's kept), cinematics inline, 12 world records, no GMND; part A = signature, SYST + 29 system variables + SYND. The 2000-byte part B is 5 bytes short of the whole stream: the last world's (e3b0c703) record is cut off in every real save, and the importer reads it from zeros with its layers at the MLVL defaults. The flag meaning (hard, dark) is inferred from the fixtures, not proven. |
| R3ME → G2ME | **Done, same caveats** | The inverse; a cinematic set on the card is kept (union). Lossy: scans/hints/relays/doors/layers/cinematics with no counterpart, counted in `ConvertReport`. |
| any ↔ R3MP | **Done (2026-11-05)** | R3MP's tables are R3ME's: G2ME ↔ R3MP is G2ME ↔ R3ME, R3ME ↔ R3MP only changes the game code. G2MP reaches it through G2ME. |

G2ME → G2MP rewrites:

- **Signature**: recompute it.
- **FrontEnd record**: rename its MLVL ID.
- **Scans**: rebuild the scan bits by asset ID against the target union. PAL adds 3 scans, which start unscanned.
- **Temple Grounds relays**: one relay ID differs, so rebuild by relay ID.
- **Layers**: add one layer bit for area 6. Its default comes from the MLVL layer bits.
- **Bit counts**: fix bitCount and layerTotal.
- **CHintOptions**: equal count (44), so copy it [I: the hint order may differ, because the bytes differ].

Remap by ID needs no game logic. Every indexed bit list is keyed by stable IDs present in the SAVW or MLVL:

- relay, door and unmappable `TEditorId`s
- cinematic IDs
- scan asset IDs
- layer names per area

Where an ID exists only in the source, the bit is dropped [I]. Within the payload, GC ↔ Trilogy would also need these [I]: (implemented 2026-10-27 in `port_save_trilogy_payload.cpp`)

- Cinematic bits remapped by ID. Trilogy drops 34.
- The HINT list remapped (44 → 42). That needs hint names, which `HINT_Hints` carries.
- The FrontEnd record renamed, and re-sorted because its position changes.

## 5. Round trip

`~/.local/share/Metroid Prime 2: Echoes/USA/Card A` exists and is **empty** [M, `ls`]. There is no EUR folder, and no Dolphin Echoes save was found, so **no console save has been decoded**.

One save written by an older port build was decoded [M]: `~/.cache/mp2-orch/saves/mi_u/USA/Card A/01-G2ME-MetroidPrime2.gci`.

- Both copies pass the CRC. The stored CRC bytes at 0x2040 are `2b73bc95`, which is the computed `95bc732b` stored LE. Generation 0.
- The signature at 0x2048 is `37d0412e`, the legacy-port value `savw_diff.py` predicts. Then saveIdx 0, slot-present 1/0/0, and `SAVH`.
- Slot 0: Temple Grounds `3bfa3eff`, area `62b0d67d`, 0%, 00:00. Slots 1 and 2 are empty.
- With the word-swap fix the port still loads it. An `MP2_SKIP_FRONTEND=1` run logged `card slots ... 0=3bfa3eff 0% 00:00 1=empty 2=empty` and `booting slot 0`, with no FileBad or corrupt message. That run goes through the same `CMemoryCardDriver` signature check as the file-select screen. The file-select UI itself was not reached: the attract loop did not take console input.
- A save written now carries `44bfc715`: `CMemoryCardDriver.cpp` builds `SSaveHeader(GetSaveSignature(), mSaveIdx)` with the default console order [code, not run].

## 6. Unknowns and risks

1. ~~Trilogy `save.bin` framing~~ - resolved 2026-10-27 (§1).
2. ~~Trilogy header and slot layout~~ - resolved; what is left is the payload stream (part A/B) conversion.
3. ~~R3MP01~~ - resolved 2026-11-05: same save data as R3ME01 (§1).
4. **Port versus console signature — measured and fixed.** Older port builds hashed host-LE words and wrote `37d0412e` on G2ME, so they would have refused a real console GCI (`44bfc715`). `CalculateHash` now swaps to big-endian words under `TARGET_PC`. The port writes the console value and accepts either (§2 Signature). The two logged values equal `savw_diff.py`'s predictions. Still unverified: a real console (Dolphin) save, which none is available to test.
5. **HINT order.** The G2ME and G2MP lists have the same count but different bytes. Check the names before copying hint states 1:1.
6. **Untested on real data.** No real save was round-tripped, so the bit layouts in §2 come from the decompiled source and are not checked against data.

## 7. Restore after a disc switch: Prime 1's design mapped onto Echoes

Prime 1 port commit `268ef3b9b` ("saves: offer to restore another release's saves after a disc switch", `../MetroidPrimePort`, read only) solves the problem a converter creates on the user's side [M, `git show`]:

- Each release keeps its own card files, filtered by game code. USA/ holds GM8E and R3ME; EUR/ holds GM8P and R3MP. After a disc switch the new disc's card looks empty.
- At boot (`OfferRestoreFromOtherRelease()` in `src/Kyoto/DolphinCMemoryCardSys.cpp`), if the mounted card has no saves for this disc but another release's card beside it has some, an SDL message box asks once: "Copy saves" or "Start fresh". A marker file `.restore-offered-<code>` in the card folder records the question. The prompt is skipped under `MP_CONSOLE` / `MP_NO_DISC_DIALOG`.
- "Copy saves" calls `RestoreFrom()`. It copies the newest release's files through the existing import path, which converts them (`port_save_convert.cpp`, tables generated by `tools/gen_save_layouts.py`). The card's earlier files go to `_replaced`.
- The overlay's Memory card section gets a "Restore from <release>" button per release found, and the console gets `gci restore [n]`.
- The API in `platform/include/port_gci.h` is `OtherRelease{folder, code, label, written}`, `OtherReleaseSaves()`, `RestoreFrom()`, `RestoreToOffer()` and `MarkRestoreOffered()`, over a release table `kReleases`.

How each piece maps onto Echoes [I unless marked]:

| Prime 1 piece | Echoes equivalent | State |
| --- | --- | --- |
| `kReleases` (GM8E, GM8P, R3ME, R3MP) | G2ME, G2MP, R3ME, R3MP. The USA/EUR folder split is the same: `aurora_card_get_path(GameCode(), …)` gives `<user>/USA/Card A` or `<user>/EUR/Card A` [M, `platform/port_save.cpp:23,144`] | Ready |
| Release discovery, marker file, the prompt and its skip knobs | Same logic, over `port::entry::CurrentGameCode()`. Skipped under `MP2_CONSOLE` and `MP2_NO_DISC_DIALOG`. | Done: `PortSave::OtherReleaseSaves/RestoreToOffer/MarkRestoreOffered` |
| Hook at boot in `DolphinCMemoryCardSys.cpp` | Same file exists here; `OfferRestoreFromOtherRelease()` at the end of `CMemoryCardSys::Initialize`, under `TARGET_PC` | Done |
| Import path (`CopyFiles` → converter) | `port::save::Import()` (`port_save.cpp:443`). It backs up what it replaces (`replaced-<stamp>.gci`), and now converts G2ME <-> G2MP through `PortSave::Convert::Gci` instead of refusing (other codes are still refused). | Done |
| `gen_save_layouts.py` tables | `tools/savw_diff.py` already extracts every per-world list and size the converter needs. `tools/savw_diff.py --emit-inc` emits it as `platform/port_save_convert_tables.inc`. | Done |
| `MapIds` / `Remap` (ID remap, dropped counts) | Rebuild each bit list by stable ID: relay, door and unmappable `TEditorId`s, cinematic IDs, scan asset IDs, and layer names per area (§4). Report the dropped bits, as Prime 1 does. | Done: `Convert::Report` |
| `ReadSlot` / `WriteSlot`, `ReadSystem` / `WriteSystem` | The `CGameState::PutTo` and `CPersistentOptions` bitstreams of §2. They are the same on G2ME and G2MP. | Layout from source; not checked on data |
| `MakeHeader` / `PutComment` / `CardCrc` | Rewrite the `SSaveHeader` signature with the **target release's console value** (§3 table), then the two copy CRCs and the header-block CRC. The comment and banner are the same on both GC releases. | Done (header block kept; CRC and generation keep the source file's byte order) |
| `ReadUsaContainer` / `ReadPalContainer` | One GCI reader covers both GC releases, because the container is the same (§1). | Exists (`Inspect()`) |
| `ReadWii` / `WriteWii` (`save.bin`) | Trilogy R3ME/R3MP: container ported as `port_save_trilogy.cpp` (plaintext NAND file, no AES; the AES one is the SD `data.bin`). Payload conversion: `port_save_trilogy_payload.cpp` (G2ME). | Partial: G2MP and signature-less (empty card) imports refused |

The first slice was G2ME ↔ G2MP only (built; see the converter status below): both directions are a remap by ID (§4), and the offer, marker and backup flow carried over unchanged.

Trilogy disc (2026-11-05, issue #76): `kReleases` also lists R3ME and R3MP, and a release is offered only when `CanConvertCard` covers it. `PortSave::Trilogy::ConvertGci` converts a whole card file G2ME ↔ R3ME (every slot and the system block through the same remap as the save.bin path, signature set to the target's §3 console value). G2MP reaches R3ME through G2ME. R3MP (2026-11-05, #76 follow-up: the reporter is on PAL) uses R3ME's tables and signature, measured equal on the PAL disc. `Files()` lists only the mounted code's files, because G2ME and R3ME share `USA/Card A` and an import replaces `Files().front()`.

## Converter status and test (2026-10-27)

`PortSave::Convert::Gci` converts a whole .gci; `Import()` calls it when the file's game code is the
other Echoes release. ME -> MP loses nothing; MP -> ME drops the three scans G2ME lacks and the
Temple Grounds layer it lacks, and the import message says how many were set. The converter writes
the target's console signature, so a port-written sample (legacy signature `37d0412e`) comes back
from ME -> MP -> ME with the console value `44bfc715` and fresh CRCs; every other byte is identical.
`port_save_convert_tests` (`tests/port_save_convert.cpp`, fixture `tests/data/01-G2ME-MetroidPrime2.gci`)
asserts exactly that, plus the refusals (Trilogy, same release, truncated file, damaged SAVH).
Not covered by a test: a slot with PAL-only scans set (no such real save is available), and a real
console save of either release.

Restore surfaces: the boot message box (once per card and release, marker `.restore-offered-<code>` in the card folder), F1 > System > Memory card "Restore from <release>", and `gci restore [n]` (docs/CONSOLE.md). All go through `Import`, so the replaced file is kept in save-backups. Not run against a live window: only built and linked.
