/**
 * PC-side definitions of the retail globals the decompilation can only declare,
 * and of the small mechanical members a PC link needs that a `Matching` unit
 * cannot supply.
 *
 * For the DOL every symbol in the globals section below is defined by a
 * *retail* object, so the decompilation is right to write `extern` and let the
 * linker bind them - that is what `config/G2ME01/symbols.txt` and the auto_*
 * objects describe, and the matching build reproduces the retail bytes without
 * these definitions existing. A PC link has no retail objects, so each one
 * needs a real definition somewhere, holding the value the retail binary holds.
 * This file is that somewhere.
 *
 * The same reasoning covers two more groups that `tools/link_gap.py` measures
 * and that no port TU defines: the static data members of classes whose `.cpp`
 * is not in the port build, and eight `TypesMatch` overrides whose bodies live
 * in `MetroidPrime/TypesMatch.cpp` - a file that is *not* in the port build and
 * cannot be put there (see the note above `CEntity::TypesMatch` below).
 *
 * It is deliberately NOT a unit in `configure.py`. A definition inside a
 * `Matching` unit would collide with the retail object's own definition at DOL
 * link time. A definition inside a `NonMatching` unit links, but it perturbs
 * that unit: a new small-data symbol shifts every SDA offset in the object, so
 * unrelated functions in it lose instruction matches. Measured, in
 * `MetroidPrime/main.cpp`: adding only `BuildTime` below moved
 * `__ct__CGameArchitectureSupport` 84.51% -> 81.54% and `AddWorldPaks` 96.00%
 * -> 95.97%, which `tools/gate.sh` reports as a regression. A unit that
 * configure.py never claims is invisible to all of that.
 *
 * Every address, width and value below is measured out of `build/G2ME01/main.elf`
 * (a linked ELF) and cross-checked against `main.dol` and
 * `config/G2ME01/symbols.txt`. The DOL's small-data bases are
 * `_SDA_BASE_ = 0x8041FD80` (used as r13) and `_SDA2_BASE_ = 0x804223C0`
 * (used as r2).
 */

#include "Kyoto/Basics/RAssertDolphin.hpp"
#include "MetaRender/CCubeRenderer.hpp"

#include "MetroidPrime/TGameTypes.hpp"
#include "WorldFormat/CAreaOctTree.hpp"
#include "MetroidPrime/Tweaks/CTweakAutoMapper.hpp"
#include "MetroidPrime/Tweaks/CTweakBall.hpp"
#include "MetroidPrime/Tweaks/CTweakContents.hpp"
#include "MetroidPrime/Tweaks/CTweakGame.hpp"
#include "MetroidPrime/Tweaks/CTweakGui.hpp"
#include "MetroidPrime/Tweaks/CTweakGuiColors.hpp"
#include "MetroidPrime/Tweaks/CTweakParticle.hpp"
#include "MetroidPrime/Tweaks/CTweakPlayer.hpp"
#include "MetroidPrime/Tweaks/CTweakPlayerControls.hpp"
#include "MetroidPrime/Tweaks/CTweakPlayerGun.hpp"
#include "MetroidPrime/Tweaks/CTweakPlayerRes.hpp"
#include "MetroidPrime/Tweaks/CTweakSlideShow.hpp"
#include "MetroidPrime/Tweaks/CTweakTargeting.hpp"

#include "Kyoto/Audio/CAudioSys.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/Audio/CStreamAudioManager.hpp"
#include "Kyoto/CResFactory.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include <new>
#include "Kyoto/CARAMManager.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Streams/CBitStreamReader.hpp"
#include "Kyoto/Streams/CBitStreamWriter.hpp"
#include "MetroidPrime/CActorLights.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/CErrorOutputWindow.hpp"
#include "MetroidPrime/CMapWorldInfo.hpp"
#include "MetroidPrime/CWorldLayerState.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/Weapons/CGunWeapon.hpp"
#include "rstl/pair.hpp"
#include "rstl/rc_ptr.hpp"
#include "rstl/string.hpp"

#include "MetroidPrime/CActor.hpp"
#include "MetroidPrime/CEntity.hpp"
#include "MetroidPrime/CInGameGuiManager.hpp"
#include "rstl/auto_ptr.hpp"
#include "MetroidPrime/CPhysicsActor.hpp"
#include "MetroidPrime/ScriptLoader/SLdrTweakPlayer.hpp"
#include "MetroidPrime/Cameras/CGameCamera.hpp"
#include "MetroidPrime/Cameras/CBallCamera.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/Enemies/CAi.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/Enemies/CSwarmBasics.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/ScriptObjects/CScriptActor.hpp"
#include "MetroidPrime/ScriptObjects/CScriptCamera.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTrigger.hpp"
#include "MetroidPrime/ScriptObjects/CScriptCameraShaker.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWater.hpp"
#include "MetroidPrime/ScriptObjects/CScriptPathCamera.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSpindleCamera.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWaypoint.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "Collision/CMaterialFilter.hpp"
#include "Kyoto/Animation/CSoundPOINode.hpp"
#include "MetroidPrime/ScriptObjects/CScriptForgottenObject.hpp"
#include "MetroidPrime/ScriptObjects/CScriptPickup.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSequenceTimer.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSpawnPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptStreamedMusic.hpp"
#include "MetroidPrime/ScriptObjects/CScriptRelay.hpp"
#include "MetroidPrime/ScriptObjects/CUnknown90.hpp"
#include "MetroidPrime/CCameraShakeManager.hpp"
#include "MetroidPrime/CHintManager.hpp"
#include "MetroidPrime/CUnknown85.hpp"
#include "MetroidPrime/CGameHint.hpp"
#include "MetroidPrime/Cameras/CFixedCamera.hpp"
#include "MetroidPrime/Cameras/CSurfaceCamera.hpp"
#include "Kyoto/Particles/CElementGen.hpp"

#include <string.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// The three game-wide sentinels. All of them, and one unnamed fourth, are
// initialised by a single static initialiser in the DOL, fn_800E9BF4
// (.text:0x800E9BF4, 0x20 bytes), which is the only writer of any of them
// anywhere in the image - not one `stw`/`sth` to these addresses outside it. It
// is registered as an initialiser by being the second word of `.ctors` entry 13
// (.ctors is at .data:0x803A54A0, 8 bytes per entry, so 0x803A5508 = fn_800E8B48
// then fn_800E9BF4):
//
//   800e9bf4:  3c 60 00 01  lis    r3,1             ; r3 = 0x00010000
//   800e9bf8:  38 80 ff ff  li     r4,-1            ; r4 = 0xFFFFFFFF
//   800e9bfc:  38 03 ff ff  addi   r0,r3,-1         ; r0 = 0x0000FFFF
//   800e9c00:  90 8d 93 a0  stw    r4,-27744(r13)   ; 0x80419120 kInvalidEditorId
//   800e9c04:  b0 0d 93 a4  sth    r0,-27740(r13)   ; 0x80419124 kInvalidUniqueId
//   800e9c08:  90 8d 93 a8  stw    r4,-27736(r13)   ; 0x80419128 kInvalidAreaId
//   800e9c0c:  90 8d 93 ac  stw    r4,-27732(r13)   ; 0x8041912C (unnamed, also -1)
//   800e9c10:  4e 80 00 20  blr
//
// The map agrees on the widths: kInvalidUniqueId is `size:0x2`, the other two
// `size:0x4`, and the `sth` is the only halfword store of the three.
//
// TUniqueId has no one-argument constructor, so 0xFFFF is spelled the way the
// layout computes it: (kUniqueIdVersionMask << kUniqueIdIndexBits) | kUniqueIdIndexMask.
//
// Note that kInvalidUniqueId has two declarations in the tree with the same link
// name and different C++ types: `const TUniqueId` in TGameTypes.hpp and
// `extern "C" const unsigned short` in CScriptWallCrawler.cpp:5, whose object does
// carry `U kInvalidUniqueId`. Both are 2 bytes at offset 0 so the value is the
// same, and the C spelling cannot be changed - that file is a Matching REL unit.
// This definition satisfies both.
const TEditorId kInvalidEditorId = TEditorId(0xFFFFFFFFu);
const TUniqueId kInvalidUniqueId = TUniqueId(kUniqueIdVersionMask, kUniqueIdIndexMask);
const TAreaId kInvalidAreaId = TAreaId(-1);

// kInvalidPlayerIndex = `.sdata2:0x8041B750` (`lbl_8041B750` in symbols.txt), size:0x8 gap,
// four bytes of value -1. Not one of the four sentinels above: those are `.sbss` and are written by
// `fn_800E9BF4`, whereas this one is `.sdata2` (initialised, not bss) and has **no writer anywhere
// in the DOL** - all eleven references in the image are `lwz` reads, six of them in this unit:
//
//   800721c0: lwz r30,-27760(r2)     ClearInhabitants - seed the player-index search
//   800726b8: lwz r0,-27760(r2)      AddInhabitant
//   80072d14: lwz r29,-27760(r2)     Touch          - seed the player index
//   80073008: lwz r5,-27760(r2)      Touch          - pass -1 when there is no player
//   80073068: lwz r5,-27760(r2)      Touch          - pass -1 when there is no player
//
// and five in another (ShouldSendScriptMsgs__18CScriptBallTriggerCFR6CActorR13CStateManager, Think__18CScriptBallTriggerFfR13CStateManager x2, InhabitantExited__18CScriptBallTriggerFR6CActorR13CStateManager, fn_801193A8), each comparing a
// player index against it. `Touch` is what fixes the meaning: it seeds an index with the constant,
// then passes either the index it found or a reload of the constant instead, so it is "no player".
// Note `-27760(r2)`, i.e. off `_SDA2_BASE_` = 0x804223C0; the same displacement off `_SDA_BASE_` =
// 0x8041FD80 would be 0x80419110, which is a *different* global (a CDecalManager static).
const int kInvalidPlayerIndex = -1;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

// gpRender - `.sbss:0x804192F8`, 4 bytes. Written exactly once in the whole DOL,
// by CGameGlobalObjects::PostInitialize at 0x80008468, immediately after
// AllocateRenderer returns in r3 and the result is stored to the object's own
// +0x148:
//
//   80008434:  bl     AllocateRenderer__FR12IObjectStoreR10COsContextR10CMemorySysR8IFactory
//   80008460:  stw    r31,328(r29)          ; CGameGlobalObjects::x148
//   80008464:  lwz    r0,328(r29)
//   80008468:  stw    r0,-27272(r13)        ; gpRender
//
// So gpRender is a *second name for the singleton CGameGlobalObjects already
// owns*, not an independent object. On PC the owner is the `renderer` member
// (an rstl unique_ptr) and CGameGlobalObjects::PostInitialize already performs
// exactly this store, so the pointer's value is not a decision this file makes.
// 449 `lwz` sites in the DOL read it; none of them writes it.
CCubeRenderer* gpRender = nullptr;

// The tweak singletons. None of them is ever assigned by DOL code: the only
// thing the DOL does with them is `fn_800324A4` (0x800324A4..0x80032670, the
// first word of `.ctors` entry 4), which stores 0 into fifteen of them and
// registers a destructor for each with `__register_global_object` (0x80344E20,
// whose whole body is a 12-byte {next, dtor, slot} record push onto a list).
//
//   800324cc:  stw    r0,-28248(r13)   ; 0x80418F28                + dtor 0x80032BE8
//   800324e0:  stw    r0,-28244(r13)   ; 0x80418F2C                + dtor 0x80032B94
//   800324fc:  stw    r0,-28240(r13)   ; 0x80418F30 gpTweakGame    + dtor 0x80032B40
//   80032518:  stw    r0,-28236(r13)   ; 0x80418F34                + dtor 0x80032AEC
//   80032534:  stw    r0,-28232(r13)   ; 0x80418F38                + dtor 0x80032A98
//   80032550:  stw    r0,-28228(r13)   ; 0x80418F3C                + dtor 0x80032A00
//   8003256c:  stw    r0,-28224(r13)   ; 0x80418F40 gpTweakPlayerB + dtor 0x800329AC
//   80032588:  stw    r0,-28220(r13)   ; 0x80418F44 gpTweakPlayerA + dtor 0x800329AC
//   ... seven more, to 0x80418F60
//
// The objects themselves are created by the Tweaks REL module, in
// `REL_CreateTweakGlobals` (Tweaks.rel .text:0x508, 0x5AC bytes) - fifteen
// "operator new(size, file, 0) -> store into the global slot" sequences, sizes
// 4 eleven times, 52, 248 twice and 604. Fifteen allocations against fifteen
// registrations, and every dtor is one of those same free-a-member-then-free-self
// shapes, so these are one tweak class per slot. gpTweakPlayerA and
// gpTweakPlayerB share a dtor, i.e. one class instantiated twice.
//
// Consumers inside the DOL: gpTweakGame has 9 load sites, gpTweakPlayerA 7,
// gpTweakPlayerB 2. gpTweakContents has *no* DOL reference at all - only the
// Tweaks module touches it, and Tweaks.cpp's `REL_LoadTweaks` already performs
// the `new CTweakContents` that retail's loader does.
//
// Since the 2026-09-29 upstream sync these are upstream's types: every slot but
// gpTweakPlayerGun is an `rstl::single_ptr`, and all sixteen have DOL addresses in
// config/G2ME01/symbols.txt (0x80418F28..0x80418F64; gpTweakPlayerGun is the last,
// the alias REL_CreateTweakGlobals points at gpTweakPlayerGunSingle). An earlier
// version of this comment said the three PlayerGun slots were Tweaks-module BSS
// with no retail address; that was wrong.
CTweakContents* gpTweakContents = nullptr;
rstl::single_ptr< CTweakAutoMapper > gpTweakAutoMapper;
rstl::single_ptr< CTweakBall > gpTweakBall;
rstl::single_ptr< CTweakGame > gpTweakGame;
rstl::single_ptr< CTweakGui > gpTweakGui;
rstl::single_ptr< CTweakGuiColors > gpTweakGuiColors;
rstl::single_ptr< CTweakParticle > gpTweakParticle;
rstl::single_ptr< CTweakPlayer > gpTweakPlayerA;
rstl::single_ptr< CTweakPlayer > gpTweakPlayerB;
CTweakPlayerGun* gpTweakPlayerGun = nullptr;
rstl::single_ptr< CTweakPlayerGun > gpTweakPlayerGunMulti;
rstl::single_ptr< CTweakPlayerGun > gpTweakPlayerGunSingle;
rstl::single_ptr< CTweakPlayerRes > gpTweakPlayerRes;
rstl::single_ptr< CTweakSlideShow > gpTweakSlideShow;
rstl::single_ptr< CTweakTargeting > gpTweakTargeting;

// gpTweakPlayerControlExpert/B are DOL .sbss (0x80418F4C and 0x80418F48, four bytes each), null until
// the Tweaks module fills them, like gpTweakPlayerA/B above. Upstream's CPlayer references them.
rstl::single_ptr< CTweakPlayerControls > gpTweakPlayerControlExpert;
rstl::single_ptr< CTweakPlayerControls > gpTweakPlayerControlsB;

// ---------------------------------------------------------------------------
// CTweakPlayer's five accessors live in two units of their own now -
// MetroidPrime/Tweaks/CTweakPlayer.cpp - so
// that they can be `Matching`. They were briefly here, which closed the five
// undefined symbols but left them in a port-side translation unit that
// `configure.py` never claims, so none of them counted. The reasoning, the
// addresses and the mwcceppc-versus-host-compiler measurement are in those two
// files and in `docs/research/tweak_player.md`.

// ---------------------------------------------------------------------------
// Build stamp
// ---------------------------------------------------------------------------

// BuildTime - `.sdata2:0x8041D550`, holding `&BuildString`. The map's
// `size:0x8` is the distance to the next symbol, not the object's width: the
// relocation against it is `R_PPC_ADDR32` (4 bytes) and the eight bytes at the
// address are the pointer followed by a zero word. The proof of the value is a
// relocation rather than a disassembly: retail's `auto_11_8041D400_sdata2.o`
// covers `.sdata2` 0x8041D400..0x8041E250, defines `BuildTime` at section offset
// 0x150 (= 0x8041D550), and carries
//
//   00000150 R_PPC_ADDR32  BuildString
//
// against it. `BuildString` is `.rodata:0x803AC3C6`, in the unclaimed block
// `auto_06_803A9620_rodata.o` at section offset 0x2DA6, and those bytes are
//
//   "Build v1.028 10/18/2004 10:44:32\0AD\0\0\0"
//
// sitting immediately after `MetroidBuildInfo` at 0x803AC3B0 (offset 0x2D90,
// "!#$MetroidBuildInfo!#$\0"). The whole stamp is therefore one literal and
// BuildTime points 22 bytes into it. BUILD_TIME_DUMMY in RAssertDolphin.hpp -
// the port's own transcription of that literal - is the same 31 visible bytes,
// so `%s` prints exactly what retail prints. (No Matching unit emits the
// literal; it lives in that unclaimed .rodata block, so BUILD_INFO is only
// transcription, and this is the one place it becomes load-bearing.)
//
// The DOL reads BuildTime twice and writes it zero times, both reads in
// ErrorHandler (0x8028C3F4, a Matching unit):
//
//   8028c634:  lwz r4,-20080(r2)   ; 0x804223C0 - 20080 = 0x8041D550
//   8028c778:  lwz r5,-20080(r2)
//
// So the value is never a product of the DOL's own execution, and the PC one is
// entirely the port's to choose.
const char* BuildTime = BUILD_TIME_DUMMY;

// ---------------------------------------------------------------------------
// Static data members
//
// Twelve class statics that the port's own objects reference and that no
// compiled unit defines, because none of the eight classes below has a .cpp in
// `files.cmake`. Every value is read out of `build/G2ME01/main.elf` at the
// address and width `config/G2ME01/symbols.txt` gives, except where the comment
// says otherwise.
// ---------------------------------------------------------------------------

// kUnknownType__10CCallStack = .rodata:0x803AEAB8; size:0xC data:string
//
//   803aeab8  55 6e 6b 6e 6f 77 6e 54 79 70 65 00   "UnknownType\0"
//
// 12 bytes, so a `char[12]` and not a pointer; the map's `data:string` agrees.
//
// **Deleted here 2026-09-28.** It was this port's host copy, written before upstream had a
// CCallStack unit; `src/Kyoto/Alloc/CCallStackDolphin.cpp` is configure.py's
// `Kyoto/Alloc/CCallStackDolphin` (MatchingFor, 100% matched, 3/3, and its .rodata claim
// 0x803AEAB8-0x803AEAC8 is this string) and defines the same member. `files.cmake` lists it
// instead of the port's own `src/MetroidPrime/CCallStack.cpp`, so leaving a second definition
// here would be a duplicate definition the moment the port links.

// kInvalidHandle__12CARAMManager = .sdata2:0x8041E93C; size:0x4 data:4byte
//
//   8041e93c  ff ff ff ff
//
// Already written as -1 in `src/Kyoto/CARAMManager.cpp`, which `files.cmake`
// does not compile (its header is a stub). Defining it here and there would be
// a duplicate the moment that file is enabled, so this is the copy that counts
// and the other one should be deleted when it is.
const int CARAMManager::kInvalidHandle = -1;

// kDefaultPositionUpdateThreshold__12CActorLights = .sdata2:0x8041B604;
// size:0x4 align:4 data:float
//
//   8041b604  3d cc cc cd
//
// 0x3DCCCCCD = 0.1f. A .sdata2 object, so the value is in the file and not
// folded into the instruction that reads it.
//
// PORT NOTE: upstream calls this constant `kDefaultMinPosChange` and already defines it, with the
// same type (`static const float`), the same value and the same address -
// `src/MetroidPrime/CActorLights.cpp:8` is `const float CActorLights::kDefaultMinPosChange = 0.1f;`
// and `CActorLights`'s constructor still defaults `positionUpdateThreshold` to it. Defining it
// here as well is a duplicate symbol, so the definition moves upstream and only the measurement
// stays. No behaviour changes: the object is the same word at the same address.

// kMedPriority__11CSfxManager has **no symbol** in config/G2ME01/symbols.txt,
// though Metroid Prime's does (`kMedPriority__11CSfxManager = .sdata2:0x805D0B8C,
// size:0x2`), so retail folds it as an immediate here. It was found by reading
// the priority argument of the emitter call: `fn_8029EAF4` takes
// (out, id, pos, dir, useAcoustics, looped, prio), and 33 of its call sites pass
// `lha r9,-16604(r2)`. -16604 is 0x8041E2E4, and the map types 0x8041E2E0 as four
// consecutive 2-byte objects:
//
//   8041e2e0  01 02 | 00 ff | 00 7f | ff ff | 00 00 00 00
//              ?      255     127     65535
//
// so 0x8041E2E2 = kMaxPriority (0xFF, as CSfxManager.hpp says), 0x8041E2E4 =
// kMedPriority (0x7F) and 0x8041E2E6 = kInternalInvalidSfxId (0xFFFF). All
// three are unnamed in the Echoes map, which is why they are not findable by
// name; two of them are the constants the neighbouring header comments already
// quote. `li rX,127` also appears at 147 CSfxManager call sites, but there it is
// the *volume* argument of `SfxStart(id, vol, pan, ...)` - not evidence for the
// priority - which is why the .sdata2 word is the reading that counts.
// PORT NOTE: defined by upstream `src/Kyoto/Audio/CSfxManager.cpp` (same value) since the
// 2026-09-28 merge; only the measurement stays here.

// kMaxVolume__9CAudioSys also has no Echoes symbol, and again retail keeps it in
// memory rather than folding it. `CActor::CActor` initialises `xd4_maxVol` from
// it, and the store is the only `lbz`-from-.sdata2 in that constructor:
//
//   8004e028:  lbz r7,-14840(r2)   ; 0x804223C0 - 14840 = 0x8041F018
//   ...
//   8004e090:  stb r7,312(r31)     ; this + 0x138
//
// .bss-style evidence that 0x138 is xd4_maxVol and not a bitfield: retail's
// field offsets run 0x64 above this header's x-prefixed ones, and 0x138 - 0x64 =
// 0xd4 = `xd4_maxVol`, whose predecessor 0x134 is written by a `stw` of -1 and
// 0x13c is the `xd8_nonLoopingSfxHandles` vector (`addi r3,r31,316`).
//
//   8041f018  c0 03 3a 27 | 1c 8a 2d 4b
//
// All fourteen readers of that address in the DOL are `lbz`, and none is an
// `lfs`/`lfd`, so 0x8041F018 is a one-byte object inside a pool of wider
// constants and its value is 0xC0. `fn_8016864C` is the shape that confirms it
// is a maximum rather than a coincidence:
//
//   8016864c:  lbz  r5,0x8041F018
//   80168650:  clrlwi r0,r4,24          ; r0 = the byte being clamped
//   80168654:  cmplw  r5,r0
//   80168658:  bge    +0x14
//   8016865c:  mr     r4,r5             ; r4 = min(byte, kMaxVolume)
//   80168660:  stb    r4,1377(r3)
const uchar CAudioSys::kMaxVolume = 192;

// kVolumeTable__9CAudioSys = .rodata:0x803AFC78; size:0x100
//
// 256 bytes of ushort, and the extent is proved rather than assumed: the map's
// next symbol, `lbl_803AFD78`, starts at exactly +0x100, and all 128 halfwords
// are *exactly* `(i*i*32768) / (127*127)` for i = 0..127 - a square-law
// amplitude ramp from 0 to 32768, with zero mismatches. A 256-entry table
// would break that at i = 128, so the object is 128 entries and the `size` is a
// width here rather than a distance to the next symbol.
//
// Nothing in the DOL references it, so the index range is unproven: the header
// indexes it with an `int` in `GetScaledVolume` and with a `uchar` in
// `CStaticAudioPlayer.cpp`, and both can read past 128 entries. The 7-bit domain
// the curve is exact over is the only hint that the intended index is 0..127.
const ushort CAudioSys::kVolumeTable[] = {
        0,     2,     8,    18,    32,    50,    73,    99,
      130,   164,   203,   245,   292,   343,   398,   457,
      520,   587,   658,   733,   812,   895,   983,  1074,
     1170,  1269,  1373,  1481,  1592,  1708,  1828,  1952,
     2080,  2212,  2348,  2488,  2632,  2781,  2933,  3090,
     3250,  3415,  3583,  3756,  3933,  4114,  4298,  4487,
     4680,  4877,  5079,  5284,  5493,  5706,  5924,  6145,
     6371,  6600,  6834,  7072,  7313,  7559,  7809,  8063,
     8321,  8583,  8849,  9119,  9394,  9672,  9954, 10241,
    10531, 10826, 11125, 11427, 11734, 12045, 12360, 12679,
    13002, 13329, 13660, 13995, 14335, 14678, 15025, 15377,
    15732, 16092, 16456, 16823, 17195, 17571, 17951, 18335,
    18723, 19115, 19511, 19911, 20316, 20724, 21136, 21553,
    21974, 22398, 22827, 23260, 23696, 24137, 24582, 25031,
    25484, 25941, 26402, 26868, 27337, 27810, 28288, 28769,
    29255, 29744, 30238, 30736, 31238, 31744, 32254, 32768,
};

// skMuzzleLocator__10CGunWeapon = .sdata2:0x8041D2D0; size:0x4 data:4byte
//
//   8041d2d0  80 3a ab d8        ; -> .rodata:0x803AABD8
//   803aabd8  4c 42 45 41 4d 00   "LBEAM\0"
//
// A pointer, and the string it names is the LBeam's muzzle locator, which is what
// `CPowerBeam.cpp` builds a `rstl::string_l` from. (The `??(?)` call-stack tag
// sits immediately *before* it, at 0x803AABD0, and `elbow` immediately after.)
// PORT NOTE: defined by upstream `src/MetroidPrime/Weapons/CGunWeapon.cpp` (same string) since
// the 2026-09-28 merge; only the measurement stays here.

// mBrightness__9CGraphics = .sdata:0x80418B00; size:0x8 align:4 data:float
//
//   80418b00  3f 80 00 00 | 00 00 00 00
//
// 0x3F800000 = 1.0f; the map's 0x8 is the distance to the next symbol, and
// `CGraphics.hpp`'s `lwz r7,-30624(r2)` at 0x80089AE0 reads the one word.
float CGraphics::mBrightness = 1.f;

// mViewMatrix__9CGraphics = .bss:0x80416F44; size:0x30 align:4 data:float
//
// .bss, and the only writer in the DOL is
// `SetViewPointMatrix__9CGraphicsFRC12CTransform4f` (0x802C2534), which reaches
// it as `lis r4,-32703; addi r3,r4,28484` = 0x80416F44 and calls
// `__as__12CTransform4fFRC12CTransform4f`. No static initialiser, so the value
// at load is 48 zero bytes and that is what retail has; it is not an identity.
// `CTransform4f` has no default constructor, so the zeros are spelled with its
// inline constructor; a default-constructed one would leave the floats uninitialised.
//
// PORT NOTE: upstream's `CTransform4f` (like retail's) has no four-`CVector3f` constructor - the
// three column vectors became the twelve scalars. The four `CVector3f(0.f, 0.f, 0.f)` here are
// retail's three zero columns plus the zero translation, so the twelve zeros below are the same 48
// bytes at the same offsets in the same (row-major) order, and `GetTranslation()`/`GetRight()` read
// the same fields.
CTransform4f CGraphics::mViewMatrix(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);

// `mViewMatrix__9CGraphics` is the same object under the name retail's `SetViewPointMatrix`
// (0x802C2534) reaches it by, and `src/Kyoto/Graphics/Carve802C2534.cpp` - listed in files.cmake
// since 2026-09-29 - is that function. Without this alias the port would have *two* view matrices:
// the C++ member every caller of the inline `GetViewMatrix()` / `GetViewPoint()` reads, and the
// one the carve writes. An alias, not a second definition, is what makes them one.
//
// A GCC alias attribute is the mechanism because both spellings have to name one object and a
// second `extern "C"` definition would be a duplicate; `alias` on a C++ object with C linkage
// gives `mViewMatrix__9CGraphics` the same address as `_ZN9CGraphics11mViewMatrixE`, which is what
// `config/G2ME01/symbols.txt` records for retail (`.bss:0x80416F44`, `size:0x30`). Verified with
// `nm` on the port object before and after.
extern "C" CTransform4f mViewMatrix__9CGraphics __attribute__((alias("_ZN9CGraphics11mViewMatrixE")));

// mLastFrameUsedAbove__9CGraphics = .sbss:0x804199A4; size:0x1 data:byte
//
// .sbss, so false.
bool CGraphics::mLastFrameUsedAbove = false;

// CGraphics' five unnamed `.sbss` words in the screen-position / time-provider
// group. Retail leaves all five unnamed and `config/G2ME01/symbols.txt` gives them
// the dtk labels below, so **these are the spellings the `Matching` units use** -
// `src/Kyoto/Graphics/CGraphicsTimeProvider.cpp` and
// `src/Kyoto/Graphics/CGraphicsScreenPosition.cpp` reference them by name, because
// `CGraphics` has no `.cpp` in the tree and a reference to a C++-named static data
// member (`CGraphics::mScreenStretch` -> `_ZN9CGraphics13mScreenStretchE`) would
// have nothing to bind to in the DOL.
//
//   lbl_804199D8 = .sbss:0x804199D8; size:0x4 align:4 data:float
//   lbl_804199DC = .sbss:0x804199DC; size:0x4 data:4byte
//   lbl_804199E0 = .sbss:0x804199E0; size:0x4 data:4byte
//   lbl_804199E4 = .sbss:0x804199E4; size:0x4 data:4byte
//   lbl_804199E8 = .sbss:0x804199E8; size:0x4 data:4byte
//
// All five are `.sbss`, so all five are zero at load - which is a real answer and
// not a placeholder: `GetScreenPosition` reports "no stretch, no offset", and
// `GetSecondsMod900` returns 0.0f before any `CTimeProvider` exists.
//
// **The C++-named members in `include/Kyoto/Graphics/CGraphics.hpp:428-432` are
// deliberately left undefined.** They are the same concepts under invented
// names, and defining them here as well would create a *second* object per
// concept, which is worse than a dangling declaration: `GetSecondsMod900` would
// read the `lbl_` one and any future reader of `CGraphics::mSecondsMod900` would
// read the other. Nothing outside the header names them (measured with grep over
// `src/`, `include/` and `platform/`), so no reference exists and nothing breaks.
// If CGraphics ever grows a `.cpp`, these declarations should be *renamed* to the
// `lbl_` spellings rather than defined alongside them.
//
// **The three screen-position words are 0x804199E0/E4/E8.** Measured, not guessed:
// retail's three `lwz` fields are `9c 60`, `9c 64`, `9c 68` (DOL file offsets
// 0x2BB7B0, 0x2BB7C0, 0x2BB7D0, read with `cmp -l`), and the exact rule is
// `field = (address - _SDA_BASE_) & 0xFFFF` with `_SDA_BASE_` 0x8041FD80 - the
// **full** signed displacement, not half of it. So they are -25504, -25500 and
// -25496, and 0x804199E0/E4/E8. Getting this wrong is invisible to every
// percentage: two wrong answers here were each byte-identical objects that paired
// at 100% under objdiff and passed `unit_fit.sh`, and each broke the DOL's sha1 on
// three bytes. src/Kyoto/Graphics/CGraphicsScreenPosition.cpp has the same story
// from the code side.
extern "C" int lbl_804199E0 = 0;
extern "C" int lbl_804199E4 = 0;
extern "C" int lbl_804199E8 = 0;
// lbl_804199D8 is `.sbss:0x804199D8; data:float` and has exactly one reader in the
// DOL, `GetSecondsMod900`'s no-provider fallback at 0x802BF638, which loads it with
// `lfs`. It is not one of the screen-position words - those are E0/E4/E8 - so
// nothing type-puns it and the map's `data:float` is simply the right type.
extern "C" float lbl_804199D8 = 0.f;
extern "C" CTimeProvider* lbl_804199DC = nullptr;

// gpRelFileManager = .sbss:0x80418EC8 - the address of `CGameGlobalObjects::mRelFileManager`
// (+0x150), stored by the constructor at 0x80008558 and read at 0x80006078, 0x80006240 and
// 0x8000743C. The DOL's definition is in `main.cpp`, which the host does not compile.
class CRELFileManager;
CRELFileManager* gpRelFileManager = nullptr;

// The guest constants `CGameState`'s default constructor and its nested constructors read by
// name - retail `.sdata`/`.sdata2`, outside every claimed range, so no mwcceppc unit defines
// them. Values read out of `build/G2ME01/main.elf` with `objdump -s`:
//
//   .sdata  0x80417D90  01 00 01 00 00 00 00 00   lbl_80417D90 = 1, lbl_80417D91 = 0,
//                                                 lbl_80417D93 = 0 (5 bytes, one is read)
//   .sdata  0x804183DC  00 00 00 00 00 01 00 01   lbl_804183DD = 0, lbl_804183DF = 0
//   .sdata2 0x8041C1A8  00000000 00000000         lbl_8041C1A8 = 0.0 (double)
//   .sdata2 0x8041C1B8  00000000                  lbl_8041C1B8 = 0.0f
//
// Readers: CGameStateMemcardCtor.cpp (D90/D91), SGameStateMemcardFill.cpp (D93),
// CGameStateSlotDefaults.cpp (83DD) and CGameStateSysOptsPutTo.cpp (83DF) take the two `.sdata`
// bytes by address as a one-byte source buffer, and CGameStateCtor.cpp loads the two `.sdata2`
// constants into `x48_time` and `x50`. They cost nothing while those units are unlisted:
// nothing asks for them.
extern "C" unsigned char lbl_80417D90 = 1;
extern "C" unsigned char lbl_80417D91 = 0;
extern "C" unsigned char lbl_80417D93 = 0;
// The third of the four, 0x80417D92 = 1 (the dump above), read by
// SGameStateMemcardBufFill.cpp's `fn_80009AC0` as the byte it fills `SGameStateMemcard`+0x00 with.
extern "C" unsigned char lbl_80417D92 = 1;
extern "C" unsigned char lbl_804183DD = 0;
extern "C" unsigned char lbl_804183DF = 0;
extern "C" const double lbl_8041C1A8 = 0.0;
extern "C" const float lbl_8041C1B8 = 0.0f;

// The two one-byte `.sdata2` flags `CPlayer::UpdateAimTarget` (retail 0x8011F338) tests:
// `lbz r0,0x8041A438(r2)` / `cmplwi` / `bne`, then `lbz r0,0x8041A439(r2)` / `cmplwi` / `beq`.
// `objdump -s` on `.sdata2` at 0x8041A438 gives `00 00 01 00` and `nm` calls both `D`
// (writable), so they are two separate globals, not halves of one object. Both hold 0: those
// two `lbz` are the only references to either in `main.elf` and nothing stores to them, so
// retail's `!lbl_8041A438 && lbl_8041A439` is always false and the block above 0x8011F3A4 is
// dead. Declared without `const` in the reader as well - see the note above.
extern "C" bool lbl_8041A438 = false;
extern "C" bool lbl_8041A439 = false;

// CWorld::skGlobalEnd / skGlobalNonConstEnd are 4-byte pointers inside
// `CGameArea::CChainIterator`, and neither is named in the map either. Both are
// read through r13, and `-28104`/`-28100` off _SDA_BASE_ 0x8041FD80 are
// 0x80418FB8 and 0x80418FBC - which the retail `CStateManager.o` slice confirms
// by name, carrying `R_PPC_EMB_SDA21 lbl_80418FB8` at .text+0xbc and
// `R_PPC_EMB_SDA21 lbl_80418FBC` at .text+0x114, the two relocations of
// `fn_80036284` and `fn_800362E0`:
//
//   80036294:  lwz  r3,5636(r3)      ; mWorld
//   80036298:  lwz  r31,76(r3)      ; the area chain head
//   ...
//   800362b8:  lwz  r31,248(r31)    ; area->m_next
//   800362bc:  lwz  r0,-28104(r13)  ; skGlobalEnd
//   800362c0:  cmplw r31,r0
//   800362c4:  bne   800362a0
//
// Both addresses are in .sbss (0x80418EA0..0x8041A3A8), so they are zero at
// load, and the only writer in the whole DOL, `fn_80052870`, writes zero to
// both:
//
//   80052870:  li   r0,0
//   80052874:  stw  r0,-28104(r13)
//   80052878:  stw  r0,-28100(r13)
//
// So the chain is terminated by a null `m_next` and the sentinel is null, which
// is what `CChainIterator`'s default constructor already produces.
// Both are defined by `CWorld.cpp` itself since it was listed (2026-10-01).

// ---------------------------------------------------------------------------
// rstl sentinels
// ---------------------------------------------------------------------------

// mNull__...basic_string<c,...> = .sbss:0x80419AE8; size:0x1 data:byte
// mNull__...basic_string<w,...> = .sbss:0x80419AEA; size:0x2 data:2byte
//
// Both zero. `src/rstl/rstl_strings.cpp` already *says*
// `template <> char basic_string<char>::mNull;`, but a static data member
// declared without an initialiser is a declaration and not a definition, so the
// host compiler emits nothing and the symbol stays undefined - the trap LANE.md
// records for `extern "C"`. That file is a `Matching` unit, so it is left alone
// here; if it ever grows the `= 0`, the two definitions must not both exist.
template <> char rstl::basic_string< char >::mNull = 0;
template <> wchar_t rstl::basic_string< wchar_t >::mNull = 0;

/**
 * `lbl_803DA994 = .bss:0x803DA994; size:0xF4` - the run of five `CDamageVulnerability`
 * singletons that `CDamageVulnerability::NormalVulnerability()` (retail
 * `NormalVulnerability__20CDamageVulnerabilityFv`, 0x800DBB70) hands back pointers into. For
 * the matching build this symbol is dtk's `.bss` fill object's (`auto_08_803C5A20_bss.o`) and
 * `src/MetroidPrime/CDamageVulnerabilityStatics.cpp` only *relocates* against it, which is what
 * that file's header says; a host link has no fill object, so the accessor in the port build
 * had nothing to bind to. Retail's own value is **zero** - it is `.bss`, and
 * `src/MetroidPrime/CDamageVulnerabilityStatics.cpp` measures the stride as five 0x30 objects
 * spanning 0x803DA994..0x803DAA88, which is 0xF4 bytes - so zero is the value, not a stand-in.
 *
 * `extern "C"` with an explicit initialiser, for the reason spelled out at the top of this
 * section: a bare `extern uchar lbl_803DA994[];` is a *declaration* in C++ and emits nothing.
 */
extern "C" uchar lbl_803DA994[0xF4] = {};

// `rstl::sNullRefCount` is the word a default-constructed `rstl::rc_ptr` points its
// `x4_refCount` at. Retail has no object for it either - it is absent from
// config/G2ME01/symbols.txt, from every dtk object under build/G2ME01/obj/, and from
// main.elf's symbol table - but retail's `ReleaseData` (0x80008FA4) has no null test on
// that pointer, so a default rc_ptr has to point at a real word or it faults on
// destruction. The count has to be large: `ReleaseData` frees as soon as
// `--*x4_refCount <= 0`, so 0 or 1 would free this static on the first default-constructed
// rc_ptr going out of scope. 0xFFFFFF is the port layer's own value for the same purpose
// in the sibling tree (../MetroidPrimePort/tests/port_buffers.cpp:30) and never approaches
// zero under balanced AddRef/DelRef. See docs/research/rc_ptr.md.
//
// The old `rstl::CRefData rstl::CRefData::sNull` that stood here is gone with `CRefData`
// itself: retail's refcount is a separate four-byte `CMemory` allocation, not a field of a
// shared control block, and keeping the control block cost an indirection on every
// dereference and a second `operator delete` on every release.
int rstl::sNullRefCount = 0x1000000 - 1;

// Superseding the paragraph above's first sentence: upstream's `rc_ptr` (2026-09-28 merge) brought
// `CRefData` back, and its default constructor and `reset()` point at `CRefData::sNull.mRefCount`,
// so the object is defined again. Retail has it - `sNull__Q24rstl8CRefData = .sdata:0x80418B98`,
// size 8 - and its first word is the same large count:
//
//   80418b98  00ffffff 00000000
//
// Constant-initialised (the `TARGET_PC` constructor in rc_ptr.hpp is `constexpr`), so a static
// `rc_ptr` built before this TU's dynamic initialisers still finds the count in place.
rstl::CRefData rstl::CRefData::sNull(0x00FFFFFF);

// ---------------------------------------------------------------------------
// TypesMatch
// ---------------------------------------------------------------------------

// Eight overrides the port's own vtables reference. The bodies are retail's,
// read off the DOL, and the shape is the `TYPES_MATCH_IMPL` macro in
// src/MetroidPrime/TypesMatch.cpp:
//
//   cmpwi r4,<id>
//   bne   +8
//   b     <return this>          ; typeId == id
//   ble   <call parent>          ; typeId <  id
//   li    r3,0                  ; typeId >  id
//   b     <return>
//   bl    <parent::TypesMatch>
//
// 0x38 bytes each, except CEntity's, which is 0x10 because it has no parent:
//
//   8009cc84 <TypesMatch__7CEntityCFi>:
//   8009cc84:  cmpwi r4,0
//   8009cc88:  beqlr
//   8009cc8c:  li   r3,0
//   8009cc90:  blr
//
// The ids and the parents are the immediates and the branch targets, and all
// eight agree with include/MetroidPrime/CEntityInfo.hpp's EEntityType and with
// each class's declared base:
//
//   class                  address     size  id   parent (branch target)
//   CScriptForgottenObject  0x8009A984 0x38 160  CEntity  (0x8009cc84)
//   CScriptStreamedMusic    0x8009BA24 0x38  84  CEntity  (0x8009cc84)
//   CScriptSpawnPoint       0x8009BB3C 0x38  79  CEntity  (0x8009cc84)
//   CScriptPickup           0x8009BE14 0x38  66  CActor   (0x8009cc4c)
//   CScriptSequenceTimer    0x8009C9E4 0x38  12  CEntity  (0x8009cc84)
//   CPhysicsActor           0x8009CC14 0x38   2  CActor   (0x8009cc4c)
//   CActor                  0x8009CC4C 0x38   1  CEntity  (0x8009cc84)
//   CEntity                 0x8009CC84 0x10   0  -
//
// Six of the eight are already written in src/MetroidPrime/TypesMatch.cpp -
// CEntity, CActor, CPhysicsActor, CScriptForgottenObject, CScriptSpawnPoint and
// CScriptStreamedMusic - and are missing from the port only because that file is
// not in `files.cmake`. It cannot simply be added: it declares its throwaway
// classes with `uchar x_pad0[0x2f0 - sizeof(CPhysicsActor)]` and the host's
// CPhysicsActor is larger than retail's 0x2f0, so the subtraction underflows and
// gcc rejects the array ("size 18446744073709551592 exceeds maximum object
// size"). CScriptPickup and CScriptSequenceTimer are genuinely undefined
// everywhere, retail included - no unit in this tree writes them.
//
// So all eight are defined here, and **adding TypesMatch.cpp to `files.cmake`
// later would duplicate the six.** Either this block moves into that file (after
// the layout problem is fixed) or its five `TYPES_MATCH_IMPL` lines and its
// `CEntity::TypesMatch` body come out. `PORT_TYPES_MATCH` below is a copy of that
// file's `TYPES_MATCH_IMPL`, character for character, because a macro cannot be
// shared between two .cpp files without moving it into a header both include.
CEntity* CEntity::TypesMatch(int typeId) const {
  return typeId == kET_Entity ? const_cast< CEntity* >(this) : nullptr;
}

#define PORT_TYPES_MATCH(cls, parent, id)                                                        \
  CEntity* cls::TypesMatch(int typeId) const {                                                   \
    if (typeId == id) {                                                                          \
      return const_cast< cls* >(this);                                                           \
    }                                                                                            \
    if (typeId > id) {                                                                           \
      return nullptr;                                                                            \
    }                                                                                            \
    return parent::TypesMatch(typeId);                                                           \
  }

PORT_TYPES_MATCH(CActor, CEntity, kET_Actor)
PORT_TYPES_MATCH(CPhysicsActor, CActor, kET_PhysicsActor)
PORT_TYPES_MATCH(CScriptPickup, CActor, kET_ScriptPickup)
PORT_TYPES_MATCH(CScriptSequenceTimer, CEntity, kET_ScriptSequenceTimer)
PORT_TYPES_MATCH(CScriptSpawnPoint, CEntity, kET_ScriptSpawnPoint)
PORT_TYPES_MATCH(CScriptStreamedMusic, CEntity, kET_ScriptStreamedMusic)
PORT_TYPES_MATCH(CScriptForgottenObject, CEntity, kET_ScriptForgottenObject)


// Three more, which upstream's CPlayer/CPatterned bodies reach (2026-09-28 merge). Same shape,
// read off retail:
//
//   CAi         0x8009CBDC 0x38   3  CPhysicsActor (0x8009cc14)
//   CPatterned  0x8009CBA4 0x38   4  CAi           (0x8009cbdc)
//   CPlayer     0x8009C584 0x38  32  CPhysicsActor (0x8009cc14)
//
// CAi's replaces the empty `stub_6` that PortLinkStubs.cpp carried while nothing reachable called it.
PORT_TYPES_MATCH(CAi, CPhysicsActor, kET_Ai)
PORT_TYPES_MATCH(CPatterned, CAi, kET_Patterned)
PORT_TYPES_MATCH(CPlayer, CPhysicsActor, kET_Player)

// Two more, and they are not optional: the 2026-09-28 upstream merge added
// `CEntity* TypesMatch(int) const;` to CScriptRelay.hpp and CUnknown90.hpp as an *override*
// declaration, with no body. Declaring it is enough to make each class's own first virtual
// its key function, so gcc then requires each class's vtable to be emitted in the TU that
// defines that key function - and there is no such TU. The port's link asked for
// `vtable for CScriptRelay` and `vtable for CUnknown90` and had nothing to bind them to,
// while both classes' own `.cpp` (which construct them) referenced them. Writing the two
// overrides here defines both key functions, and the compiler then emits both vtables.
//
// The bodies are retail's, read off the DOL at the two vtables those classes own -
// `lbl_803B35B8` and `lbl_803B7AE0`, 0x20 bytes each, `[0][0][dtor][TypesMatch]` then the
// flat CEntity slot list, which is the layout docs/research/TypesMatch_unnamed_ids.txt
// documents for every one of the 76:
//
//   CScriptRelay  TypesMatch__12CScriptRelayCFi  0x8009BC8C  0x38  id  73  CEntity (0x8009CC84)
//   CUnknown90    TypesMatch__10CUnknown90CFi     0x8009B8D4  0x38  id  90  CEntity (0x8009CC84)
//
// 0x38 is the same five-branch shape as every other override above, and the immediates and
// the branch target are the two numbers in the table:
//
//   cmpwi r4,73 ; bne +8 ; b <return this> ; ble <call CEntity> ; li r3,0 ; b <return> ; bl
//
// 73 is `kET_ScriptRelay`, which `include/MetroidPrime/CEntityInfo.hpp` already names, and it is
// the FourCC SRLY class - so the id and the class agree. 90 has no name in EEntityType
// (the enum skips 90, 91, 96 and 101), so it is written as the literal the instruction
// carries, with the measurement next to it; the class is retail's own `CUnknown90`, and
// `docs/research/TypesMatch_unnamed_ids.txt` is the table of the 33 ids like it whose class
// no source in this tree names.
PORT_TYPES_MATCH(CScriptRelay, CEntity, kET_ScriptRelay)
PORT_TYPES_MATCH(CUnknown90, CEntity, 90)

#undef PORT_TYPES_MATCH

// `TryCast__FP7CEntityi` (0x8009CC94, 0x3C): a null test, then the virtual TypesMatch. The five
// `TCastToPtr<T>(CEntity*)` below are retail's 0x24-byte wrappers that load the type id into r4
// and call it - the ids are their `li r4` immediates and agree with EEntityType:
//
//   TCastToPtr<CGameCamera>   0x8009A8DC  li r4,5
//   TCastToPtr<CScriptActor>  0x80099F58  li r4,34
//   TCastToPtr<CScriptCamera> 0x80099C10  li r4,44
//   TCastToPtr<CScriptWater>  0x80098AAC  li r4,97
//   TCastToPtr<CScriptWaypoint> 0x8009A78C li r4,9
//   TCastToPtr<CScriptTrigger> 0x80098C50  li r4,92
//
// CScriptWaypoint's is here because `CPatterned::fn_80073938` (0x80073938) calls it, and retail's
// wrapper is in a unit this tree does not link; a PC link has no retail object to bind it to.
// CScriptTrigger's is here because the three `CCameraManager` trigger functions
// (0x801AC4C4, 0x801AC588, 0x801AC638) cast every actor in `CStateManager`'s object list to it,
// and `src/MetroidPrime/ScriptObjects/CScriptTrigger.cpp` is excluded from the port build for the
// reason `tools/check_files_cmake.py` records (its out-of-line `GetTriggerBoundsWR` duplicates
// the one in `PortLinkStubs.cpp`).
//
// src/MetroidPrime/TypesMatch.cpp has the same TryCast body and a CAST_TO_PTR_IMPL macro; it is
// still unlisted for the layout reason given above, so adding it later duplicates these too.
CEntity* TryCast(CEntity* entity, int typeId) {
  if (entity != nullptr) {
    return entity->TypesMatch(typeId);
  }
  return nullptr;
}

#define PORT_CAST_TO_PTR(cls, id)                                                                \
  template <>                                                                                    \
  cls* TCastToPtr< cls >(CEntity * entity) {                                                     \
    return static_cast< cls* >(TryCast(entity, id));                                             \
  }

PORT_CAST_TO_PTR(CGameCamera, kET_GameCamera)
PORT_CAST_TO_PTR(CScriptActor, kET_ScriptActor)
PORT_CAST_TO_PTR(CScriptCamera, kET_ScriptCamera)
PORT_CAST_TO_PTR(CScriptWater, kET_ScriptWater)
PORT_CAST_TO_PTR(CScriptWaypoint, kET_ScriptWaypoint)
PORT_CAST_TO_PTR(CScriptTrigger, kET_ScriptTrigger)
//   TCastToPtr<CCollisionActor> 0x8009A498 li r4,18
//
// CCollisionActor's is here for `CBallCamera::TeleportCamera(const CVector3f&, CStateManager&)`
// (0x801A7F10), which the newly written `CBallCamera::UpdateTransitionToBallCamera` (0x801A8B78)
// calls; the cast sits in that body and `src/MetroidPrime/CCollisionActor.cpp` is a
// `NonMatching` unit the port does not list, so nothing else defines this specialization.
PORT_CAST_TO_PTR(CCollisionActor, kET_CollisionActor)
// Type 85 is `CUnknown85` in src/MetroidPrime/TypesMatch.cpp, which is deliberately out of the port
// build; `CCameraManager::SetSurfaceCamera` needs the cast. See include/MetroidPrime/CUnknown85.hpp.
PORT_CAST_TO_PTR(CUnknown85, 85)
// `CCameraManager::SetSpindleCamera` casts the script actor to CScriptSpindleCamera
// (`TCastToPtr<20CScriptSpindleCamera>` 0x80098F44); TypesMatch.cpp is out of the port build.
PORT_CAST_TO_PTR(CScriptSpindleCamera, kET_ScriptSpindleCamera)
// `CCameraManager::SetPathCamera`: `TCastToPtr<17CScriptPathCamera>` 0x8009952C.
PORT_CAST_TO_PTR(CScriptPathCamera, kET_ScriptPathCamera)
// Type 46 is `CUnknown46`, retail's control-hint actor (the `LoadControlHint` / `CTLH` loader);
// `fn_8022A5B4` in `PortCHintManager.cpp` casts every active hint to it. `tools/dis.sh 0x80099B68
// 0x24` is the ordinary wrapper with `li r4,46`, and it is here for the same reason as the two
// above: `src/MetroidPrime/TypesMatch.cpp` holds the cast and is out of the port build. The class
// itself is declared in `include/MetroidPrime/CGameHint.hpp`.
PORT_CAST_TO_PTR(CUnknown46, 46)
// `TCastToPtr<CScriptCameraShaker>` (0x80099D0C) is the ordinary wrapper with `li r4,41`; it is here
// for `CPatterned::ApplyScreenShake` (0x80150FE8), and TypesMatch.cpp holds the other copy.
PORT_CAST_TO_PTR(CScriptCameraShaker, kET_ScriptCameraShaker)

// `TCastToPtr<CPatterned>(CEntity*)` (0x80097584, 0x1C) is not the type-id wrapper the five above
// are: retail's body is a null test plus `rlwinm. r0,r0,30,29,29` on the byte at `0x20`, which is
// cast-flag bit 2 - the same test `src/MetroidPrime/TypesMatch.cpp` spells as
// `(entity->GetCastFlags() & 4) != 0`. That file is unlisted in `files.cmake` for the layout
// reason recorded there, so this definition is needed here: retail's
// `CActorModelParticles::IsMediumOrLarge` (0x8014FD14) calls it, and
// `src/MetroidPrime/CActorModelParticles.cpp` now does too.
template <>
CPatterned* TCastToPtr< CPatterned >(CEntity* entity) {
  if (entity != nullptr && (entity->GetCastFlags() & 4) != 0) {
    return static_cast< CPatterned* >(entity);
  }
  return nullptr;
}

// `TCastToPtr<CSwarmBasics>(CEntity*)` (0x80098908, 0x20) is the ordinary type-id wrapper with
// `li r4,102` = `kET_SwarmBasics` (`tools/dis.sh 0x80098908 0x24`), and `TypesMatch.cpp` spells it
// as `CAST_TO_IMPL(CSwarmBasics, kET_SwarmBasics)`. That file is out of the port build for the
// layout reason recorded in `files.cmake`, so `CPlayer::SetOrbitTargetId` needs it here. It is
// `reinterpret_cast`, not `static_cast`, because `include/MetroidPrime/Enemies/CSwarmBasics.hpp`
// is a partial ABI declaration that deliberately does not name `CActor` as a base - the same
// situation `CAST_TO_IMPL_INCOMPLETE` covers in `TypesMatch.cpp`.
template <>
CSwarmBasics* TCastToPtr< CSwarmBasics >(CEntity* entity) {
  return reinterpret_cast< CSwarmBasics* >(TryCast(entity, kET_SwarmBasics));
}

// `TCastToPtr<CSandwormEye>(CEntity&)` (0x80097FA8) is the reference overload; `TypesMatch.cpp` spells
// it `CAST_TO_IMPL(CSandwormEye, kET_SandwormEye)` (id 131) and is out of the port build. The only
// caller is `CCompoundTargetReticle::CalculateRadiusWorld`, which names `CSandwormEye` by
// forward declaration, so this is a `reinterpret_cast` like `CSwarmBasics`'s above.
class CSandwormEye;
template <>
CSandwormEye* TCastToPtr< CSandwormEye >(CEntity& entity) {
  return reinterpret_cast< CSandwormEye* >(TryCast(&entity, kET_SandwormEye));
}

#undef PORT_CAST_TO_PTR

// `CSoundPOINode::skExtendedVersion` is retail .sdata2 0x8041E410, two bytes, and the bytes are
// `00 01`: version 1. Upstream's CSoundPOINode.cpp (listed in the 2026-09-28 merge) compares
// against it and leaves the definition out, as it does for most retail constants.
const ushort CSoundPOINode::skExtendedVersion = 1;

// `CMaterialFilter::skPassEverything` has no symbol in the Echoes map (it is folded or unnamed),
// but all three Trilogy maps carry it as a 0x18-byte .bss object - `.bss` because it is
// constructed at startup, not stored - and the one constructor that builds a filter with no
// arguments is the pass-everything one (`kFT_Always`, include mask 0xFFFFFFFF, exclude 0).
const CMaterialFilter CMaterialFilter::skPassEverything;

// ---------------------------------------------------------------------------
// Retail read-only data a `Matching` unit has to name instead of writing
// ---------------------------------------------------------------------------
//
// A `Matching` object is linked into the DOL, so it cannot own a `.rodata`, `.sdata2` or `.data`
// byte that `config/G2ME01/splits.txt` does not claim for it: adding four bytes of `.sdata2` grew
// the section from 0x54C0 to 0x54E0, moved .bss, and broke the DOL's sha1 with every function in
// every unit still at 100% (measured in docs/research/frame_loop.md). So the three constants below
// are *named* in the units that need them, and defined here, with retail's values, because a PC link
// has no retail object to bind them to. The values are what the instructions read, not the guest
// addresses: a 64-bit host cannot hold 0x8041E258, and nothing should try.
//
// `lbl_8041E258` is what `CStopwatch::CSWData::Initialize` divides by (`lfs f0,-16744(r2)` against
// `__cvt_sll_flt`), and it is 0x3F800000 - 1.0f exactly. Undefined it would be a zero fill and
// `x10_timerPeriod` would be 0.0f, i.e. `GetElapsedTime()` would return 0 for the whole game.
//
// `lbl_8041E260` is what `CStopwatch::CSWData::Wait` adds and subtracts (`lfd f2,-16736(r2)`), and
// it is 0x4330000000000000 as a double, i.e. 2^52 exactly - the largest power of two below which
// adding an integer is still exact in a double.
//
// `lbl_803A60A0` is the rodata blob `CMainFlow::CMainFlow` points seven bytes into
// (`lis`/`addi`/`addi 7`), because retail's linker merged "MainFlow" with the tail of a longer
// literal. The seven bytes in front are "??" "(?(" ")" and a NUL, reproduced here so that
// `lbl_803A60A0 + 7` is the NUL-terminated "MainFlow" the constructor passes to `CIOWin`.
//
// The six `lbl_80417DE*`/`lbl_80417DF*` words are retail `.sdata` at 0x80417DE0..0x80417DF8,
// read by `CMainFlow::SetGameState` as `addi r5,r13,-32672`-style operands to `fn_80048EA4`,
// which dereferences all three. The values are out of `objdump -s -j .sdata`, not guessed:
//
//   80417de0  0000000c 0000000b  ->  12, 11   the pair kCFS_GameExit's message is built from
//   80417de8  0000000c 0000000b  ->  12, 11   the pair kCFS_PreFrontEnd's message is built from
//   80417df0  0000000a 000003e8  ->  10, 1000 the pair kCFS_Game's message is built from
//
// They are named in the unit that needs them and defined here for the same reason as the three
// above: a PC link has no retail object to bind them to, and a zero fill would be a wrong answer
// rather than a missing one. Their only reader, `fn_80048EA4`, is itself still retail-only, so
// nothing in the port reads them yet - which is exactly why they are cheap to close now and would
// have been expensive to notice later.
extern "C" const float lbl_8041E258 = 1.0f;
extern "C" const double lbl_8041E260 = 4503599627370496.0; // 2^52
extern "C" const char lbl_803A60A0[] = "??(??)\0MainFlow";

// `lbl_8041CB18` - `.sdata2:0x8041CB18`, `size:0x8`, `data:float` (`symbols.txt:23633`). It is in
// the same unclaimed `.sdata2` gap as `lbl_8041C398` above (splits.txt claims 0x8041CB00-0x8041CB08
// and then 0x8041CB20-0x8041CB30), so the DOL link gets it from dtk's `auto_11_8041C148_sdata2.o`
// and only the port needs it defined. `tools/dol_read.py 0x8041CB10 0x10` gives the four bytes as
// `00 00 00 00`, i.e. 0.0f - `CTrigger`'s default constructor initialises `mArg` from it, and a
// `0.f` literal of our own would give `MetroidPrime/Enemies/CStateMachine.cpp` a `.sdata2`
// section `config/G2ME01/splits.txt` does not claim for it.
extern "C" const float lbl_8041CB18 = 0.0f;

// `lbl_803B5D64` and `lbl_803B5D28` - `.data`, `size:0x3C` each (`symbols.txt:18226-18227`), the
// vtable pair `CStateMachineState` installs in its constructor (retail 0x801956BC) and its deleting
// destructor (0x80195914) puts back. Retail's `splits.txt` gives this unit `.text` only, so the DOL
// link resolves both from dtk's own `.data` object and only the port needs them defined.
// **A link stub, not retail's contents**: the 0x3C bytes are retail's, but they are PowerPC
// function addresses and the two `fn_` functions that read them are never called on PC, so zeroing
// them is right for the host and reproducing retail's addresses would not be.
extern "C" const int lbl_803B5D64[15] = {0};
extern "C" const int lbl_803B5D28[15] = {0};

// `lbl_803AA230` - `.rodata:0x803AA230`, `size:0x10` (`symbols.txt:17242`), the same merged shape as
// `lbl_803A60A0` above. Both `CTrigger::Setup` overloads compare the copied name against
// `lbl_803AA230 + 7` (`lis r4,0x803B ; addi r4,r4,-24016 ; addi r4,r4,7`), which is retail's
// "Default"; the seven bytes in front are retail's own, so they are reproduced whole:
//
//   803aa230  3f 3f 28 3f 3f 29 00 44 65 66 61 75 6c 74 00 00   "??(??)..Default.."
//
// The literal is 16 bytes, `symbols.txt`'s `size:0x10` exactly, so the `+7` is retail's arithmetic
// and not an accident of padding.
extern "C" const char lbl_803AA230[] = "??(??)\0Default";
extern "C" int lbl_80417DE0 = 12;
extern "C" int lbl_80417DE4 = 11;
extern "C" int lbl_80417DE8 = 12;
extern "C" int lbl_80417DEC = 11;
extern "C" int lbl_80417DF0 = 10;
extern "C" int lbl_80417DF4 = 1000;

// `lbl_8041C398` is `.sdata2:0x8041C398`, 4 bytes, `3f800000` = 1.0f, and it is owned by no unit
// in `config/G2ME01/splits.txt` - so the DOL link gets it from dtk's
// `build/G2ME01/obj/auto_11_8041C148_sdata2.o`, and this definition is only here for the port.
// `CWorldStateCtor.cpp` declares it `extern "C" float` (not `const float`, matching the storage
// retail keeps it in) and reads it into a local: mwcceppc re-reads a non-`const` global after every
// store it cannot prove does not alias it, and a store to `this+K` is exactly that, so the direct
// spelling emits five `lfs` where retail emits two. `lbl_8041C394` and `lbl_8041C390` either side
// of it are 32.0f and 4096.0f.
extern "C" const float lbl_8041C398 = 1.0f;

// The three `.sdata2` constants `CGameOptions::TuneScreenBrightness` reads, at
// `0x8041C500` / `0x8041C504` / `0x8041C508` for 4 bytes each, from the same unclaimed
// `0x8041C148` region as `lbl_8041C398` above - so again the DOL link gets them from dtk's
// `auto_11_8041C148_sdata2.o` and only the port needs them defined. Values out of
// `objdump -s -j .sdata2 build/G2ME01/main.elf`: `3f800000` = 1.0f, `3ec00000` = 0.375f,
// `3e800000` = 0.25f. Defined `const` here and declared non-`const` by the reader, which is how
// `lbl_8041C398` above is handled too: with a `const` declaration mwcceppc materialises the value
// inline instead of reloading it through `r13`, and `TuneScreenBrightness` falls to 87.65%.
extern "C" const float lbl_8041C500 = 1.0f;
extern "C" const float lbl_8041C504 = 0.375f;
extern "C" const float lbl_8041C508 = 0.25f;

// `lbl_8041A920` - `.sdata2:0x8041A920`, `size:0x8`, `data:float` (`symbols.txt:21860`). It is in
// the same unclaimed `.sdata2` region as the constants above, so the DOL link gets it from dtk's
// `auto_11_8041A900_sdata2.o` and only the port needs it defined. `MetroidPrime/Carve800534BC.c`
// declares it and loads it: retail's `fn_800534BC` is `lfs f1,lbl_8041A920@sda21(r0) ; blr`, and
// the word it loads is `3f800000` = 1.0f (`objdump -s -j .sdata2 build/G2ME01/main.elf`; the
// object's other four bytes are a 0.f pad, which is why `symbols.txt` sizes it 0x8).
// **The carve cannot write `return 1.f;`:** a literal of its own gives that translation unit a
// `.sdata2` section `config/G2ME01/splits.txt` does not claim for it, which is exactly the
// `lbl_8041C398` failure above.
extern "C" const float lbl_8041A920 = 1.0f;

// `lbl_8041ECE8` - `.sdata2:0x8041ECE8`, `size:0x4`, `data:float` (`symbols.txt:25423`). The
// same arrangement as `lbl_8041A920` above, in the same unclaimed `.sdata2` region: no unit
// in `config/G2ME01/splits.txt` claims it, so the DOL link gets the word from dtk's
// `auto_11_8041EC28_sdata2.o` and only the port needs it defined.
// `Kyoto/Math/Carve80335A0C.c` declares it and loads it: retail's `fn_80335A0C` is
// `lfs f1,lbl_8041ECE8@sda21(r0) ; blr`, and the word it loads is `00000000` = 0.0f
// (`objdump -s -j .sdata2 build/G2ME01/main.elf`; `build/G2ME01/asm/
// auto_11_8041EC28_sdata2.s:185` reads the same word as `.float 0`). **The carve cannot write
// `return 0.f;`** for the reason the `lbl_8041A920` entry above gives: the literal would put a
// `.sdata2` section in that translation unit which no claim places. Defined `const` and
// declared non-`const` by the reader, same as above.
extern "C" const float lbl_8041ECE8 = 0.0f;

// `lbl_8041EC88` - `.sdata2:0x8041EC88`, `size:0x8`, `data:float` (`symbols.txt:25402`). The same
// arrangement as `lbl_8041ECE8` immediately above, in the same unclaimed `.sdata2` region: no unit
// in `config/G2ME01/splits.txt` claims it, so the DOL link gets the word from dtk's
// `auto_11_8041EC28_sdata2.o` and only the port needs it defined.
// `Kyoto/Math/Carve8032EE50.c` declares it and loads it: retail's `__sinit_CSortedParticleSystem_cpp` is
// `lfs f0, lbl_8041EC88@sda21(r0)` then three `stfs` (see that file's header), and the word it
// loads is `3f800000` = 1.0f - `python3 tools/dol_read.py 0x8041EC88 0x10` prints
// `u32 0x3f800000 0x0`, and `build/G2ME01/asm/auto_11_8041EC28_sdata2.s:77-80` reads the same two
// words as `.float 1` / `.float 0` (the 0.f pad is why `symbols.txt` sizes it 0x8).
// `Kyoto/Math/Carve8032E604.c:36-45` pins the same constant independently from two other readers.
// **The carve cannot write `1.0f`** for the reason the `lbl_8041A920` entry above gives: the
// literal would put a `.sdata2` section in that translation unit which no claim places. Defined
// `const` and declared non-`const` by the reader, same as above.
extern "C" const float lbl_8041EC88 = 1.0f;

// `lbl_803E0530` - `.bss:0x803E0530`, `size:0x10`, `align:4`, `data:float` (`symbols.txt:19148`).
// Retail's value is **zero**: it is `.bss`, and `build/G2ME01/asm/auto_08_803E051C_bss.s:19-21`
// emits it as `.skip 0x10`, so zero is the value and not a stand-in. Nothing else in the tree
// names it, so this entry exists only for `Kyoto/Math/Carve8032EE50.c`, which *writes* the first
// three of its four floats and leaves the fourth where retail leaves it.
// `extern "C"` with an explicit initialiser, for the reason spelled out at the top of this
// section: a bare `extern float lbl_803E0530[];` is a *declaration* in C++ and emits nothing.
extern "C" float lbl_803E0530[4] = {};

// `lbl_8041EC30` - `.sdata2:0x8041EC30`, `size:0x4`, `data:float` (`symbols.txt:25390`). The same
// arrangement as `lbl_8041EC88` immediately above, in the same unclaimed `.sdata2` region: no unit
// in `config/G2ME01/splits.txt` claims it, so the DOL link gets the word from dtk's
// `auto_11_8041EC28_sdata2.o` and only the port needs it defined.
// `Kyoto/Math/Carve8032DC50.c` declares it and loads it: retail's `__sinit_CParticleSpawnSystem_cpp` is
// `lfs f0, lbl_8041EC30@sda21(r0)` then three `stfs` (see that file's header), and the word it
// loads is `3f800000` = 1.0f - `python3 tools/dol_read.py 0x8041EC30 0x8` prints
// `u32 0x3f800000 0x0`, and `build/G2ME01/asm/auto_11_8041EC28_sdata2.s:15-17` reads the same word
// as `.float 1`. **The carve cannot write `1.0f`** for the reason the `lbl_8041A920` entry above
// gives: the literal would put a `.sdata2` section in that translation unit which no claim
// places. Defined `const` and declared non-`const` by the reader, same as above.
extern "C" const float lbl_8041EC30 = 1.0f;

// `lbl_803E0520` - `.bss:0x803E0520`, `size:0x10`, `align:4`, `data:float` (`symbols.txt:19147`).
// The same arrangement as `lbl_803E0530` immediately above, and for the same reason: retail's
// value is **zero** - it is `.bss`, and `build/G2ME01/asm/auto_08_803E051C_bss.s:14-16` emits it
// as `.skip 0x10`, so zero is the value and not a stand-in. Nothing else in the tree names it, so
// this entry exists only for `Kyoto/Math/Carve8032DC50.c`, which *writes* the first three of its
// four floats and leaves the fourth where retail leaves it.
extern "C" float lbl_803E0520[4] = {};

// ---------------------------------------------------------------------------
// rstl free functions
// ---------------------------------------------------------------------------

// Both are 0x30 / 0x3C bytes of the same shape, and both are exactly the
// header's own `basic_string(literal_t, const _CharTp*)` constructor: store the
// pointer, null the cow block, then measure with a loop that steps by
// sizeof(_CharTp).
//
//   802ff418 <string_l__4rstlFPCc>:
//   802ff418:  stw  r4,0(r3)        ; x0_ptr = data
//   802ff41c:  li   r0,0
//   802ff420:  mr   r5,r4
//   802ff424:  stw  r0,4(r3)        ; x4_cow = nullptr
//   802ff42c:  addi r5,r5,1
//   802ff430:  lbz  r0,0(r5)
//   802ff434:  extsb. r0,r0
//   802ff438:  bne  802ff42c
//   802ff43c:  subf r0,r4,r5
//   802ff440:  stw  r0,8(r3)        ; x8_size
//
// `wstring_l` (0x802FF3DC) is the same with `addi r5,r5,2` / `lhz` and a
// divide-by-2 on the size, because retail's wchar_t is 2 bytes; the host's is 4,
// and the constructor divides by sizeof(wchar_t) on its own.
rstl::string rstl::string_l(const char* data) {
  return rstl::string(rstl::string::literal_t(), data);
}

rstl::wstring rstl::wstring_l(const wchar_t* data) {
  return rstl::wstring(rstl::wstring::literal_t(), data);
}

// `__pl__4rstlFRCQ24rstl66basic_string<c,...>RCQ24rstl66basic_string<c,...>`
//   = .text:0x80005AE8; size:0x5C scope:weak
//
// The Itanium mangling of `operator+` is `pl`, so the undefined symbol the port
// reports, `_ZN4rstlplERKNS_12basic_stringIcNS_11char_traitsIcEENS_17rmemory_allocatorEEES6_`,
// is `rstl::operator+(const string&, const string&)` - already declared at
// include/rstl/string.hpp:344 and never defined, not a separate `pl`. Copy the
// first operand, append the second, copy the result out, destroy the temporary:
//
//   80005b04:  addi r3,r1,8
//   80005b08:  bl   basic_string(const basic_string&)
//   80005b0c:  mr   r4,r31
//   80005b10:  addi r3,r1,8
//   80005b14:  bl   append(const basic_string&)
//   80005b18:  mr   r3,r30
//   80005b1c:  addi r4,r1,8
//   80005b20:  bl   basic_string(const basic_string&)
//   80005b24:  addi r3,r1,8
//   80005b28:  bl   internal_dereference()
//
// Four retail `pl` overloads are named in the map; this is the (string, string)
// one, and the only one the port references.
rstl::string rstl::operator+(const rstl::string& a, const rstl::string& b) {
  rstl::string tmp(a);
  tmp.append(b);
  return tmp;
}

// ---------------------------------------------------------------------------
// ScriptCannonBall's module constants (module 57)
//
// `lbl_57_rodata_*` are `.rodata` of `orig/G2ME01/files/RelProd/ScriptCannonBall.rel`, which
// `dtk rel info` puts at file offset 0x14E0, size 0xB4 - so the five objects are the first
// twenty bytes of the section and the sixth is the 0x1C bytes after them. Read out of the
// retail module (big-endian, so the bytes are in IEEE-754 order already):
//
//   +0x00  3f 80 00 00   1.0f     lbl_57_rodata_0
//   +0x04  00 00 00 00   0.0f     lbl_57_rodata_4
//   +0x08  3e 80 00 00   0.25f    lbl_57_rodata_8
//   +0x0C  43 7f 00 00   255.0f   lbl_57_rodata_C   (declared by the port, never referenced)
//   +0x10  40 00 00 00   2.0f     lbl_57_rodata_10
//   +0x14  3f 3f 28 3f 43 61 6e 6e 6f 6e 42 61 6c 6c 20 45 66 66 65 63 74 00 00 00 00
//                                lbl_57_rodata_14, 0x1C bytes
//
// The last one is a float followed by a string literal with no padding between them, which is
// why dtk gives it `size:0x1C` and no type: the object starts at +0x14 and runs to the next
// symbol. "CannonBall Effect" is at +0x14 + 7, which is exactly the `lbl_57_rodata_14 + 7` in
// `src/MetroidPrime/ScriptObjects/CScriptCannonBall.cpp:64` - the string the port hands to
// `CScriptEffect`'s constructor. The four floats agree with how the port already uses them:
// `_0` is the reset value of `CScriptCannonBall::m_f` and the "1.0" it passes to
// `CScriptEffect`, `_4` is the `m_f > 0` / `m_f < 0` clamp, `_8` is the `dt / 0.25f` decay
// divisor in `Think`, and `_10` is the second `CScriptEffect` scale argument.
//
// A REL module's `.rodata` cannot be claimed by a `Matching` unit without also reproducing the
// module's hash, and no unit claims it, so these are port-side definitions. Measured effect on
// the port link gap: 559 -> 554.
// ---------------------------------------------------------------------------

extern const float lbl_57_rodata_0 = 1.0f;
extern const float lbl_57_rodata_4 = 0.0f;
extern const float lbl_57_rodata_8 = 0.25f;
extern const float lbl_57_rodata_C = 255.0f;
extern const float lbl_57_rodata_10 = 2.0f;
// +0x00 is a float, +0x04 begins the string seven bytes into the object. Only the bytes at and
// after +7 are ever read, so the leading float is left as the value it has in the module.
extern const char lbl_57_rodata_14[] = "\x3f\x3f\x28\x3fCannonBall Effect";

// ---------------------------------------------------------------------------
// fn_8033D2EC - the `std::exception` RTTI accessor
//
// Retail is two instructions and 8 bytes:
//
//   8033d2ec:  lwz  r3,-28920(r13)   ; _SDA_BASE_ = 0x8041FD80 -> 0x80418C98
//   8033d2f0:  blr
//
// 0x80418C98 is `__RTTI__Q23std9exception` (config/G2ME01/symbols.txt:20243, `.sdata`, 8 bytes),
// so the function returns the address of the `std::exception` type descriptor. Its three callers
// all use the value as a pointer and none of them calls anything through it:
//
//   0x80005cac  CMain::RsMain            ; `addis r0,r3,8192; cmplwi r0,0` - tests bit 29, then
//                                         LCEnable() if clear
//   0x802c11d4  fn_802C11C0             ; stores it and four offsets from it (964, 1928, 3856,
//                                         5784) into five consecutive .sbss words at 0x80419938
//   0x80319fe8  ShouldEnableLockedCache  ; `(r3 - 0xE0000000 | 0xE0000000 - r3) >> 31`, i.e.
//                                         `r3 != 0xE0000000`
//
// The port's only reference is that third one, transcribed into CCubeMoviePlayer.cpp's local
// `ShouldEnableLockedCache()`. The address is a DOL address with no host meaning, so the
// definition returns null - which is `!= 0xE0000000` exactly as retail's 0x80418C98 is, and the
// port's behaviour is unchanged. Measured effect on the port link gap: 554 -> 553.
// ---------------------------------------------------------------------------

extern "C" void* fn_8033D2EC() {
  return nullptr;
}

// ---------------------------------------------------------------------------
// FreeLockedCache__FPv - the free half of MWCC's small-block allocator
//
// Retail is 0x64 = 25 instructions at 0x8033D2F4, and it is the fourth member of one family in
// the unclaimed `.text` gap 0x8033D2EC..0x8033D420 (`config/G2ME01/splits.txt` has
// `.text 0x8033B940..0x8033D2EC` and `0x8033D420..0x8033D69C`, so nothing claims the middle).
// `nm` over every object in `build/G2ME01/obj` finds all four defined by the same dtk auto-split
// object, `auto_03_8033D2EC_text.o`, which is what `build.ninja` links into `main.dol` - so the
// **DOL needs nothing from here**. What needs it is the host port, and only because
// `src/MetroidPrime/Player/CMorphBall.cpp` now writes out retail's `fn_800CD4B8`, which calls this
// function at 0x800CD4F8. Without a definition the host link would carry one more undefined symbol
// than `docs/research/port_link_baseline.txt` records and `tools/link_gap.py` would fail with
// `gap grew: FreeLockedCache__FPv is not in port_link_gap_list.md`. This file is the arrangement the repo
// already uses for exactly that case - it is the home of retail's `fn_8033D2EC`, the function
// eight bytes earlier in the same gap (see above), for the same reason.
//
// What the family is, read off the four disassemblies (`./tools/dis.sh 0x8033D2EC 0x134`):
//
//   fn_8033D2EC   return the pool base
//   FreeLockedCache__FPv   free(ptr)      <- this one
//   AllocateLockedCache32__FUi   alloc(size), 32-byte aligned   `addi r0,r3,31 / clrrwi r0,r0,5`
//   AllocateLockedCache4__FUi   alloc(size),  4-byte aligned   `addi r0,r3,3  / clrrwi r0,r0,2`
//
// so it is a LIFO block allocator with a 16-entry record table and a 0x4000-byte ceiling
// (`cmplwi r4,16384 / bgt` in both allocs, `cmplwi r5,16 / bge` before each table push). The free
// decrements the outstanding count first, and only rewinds the bump pointer when the pointer being
// freed is the **most recent** allocation - which is why it compares against the table rather
// than subtracting unconditionally - and raises a "freed out of order" byte when it is not.
//
// The state it maintains is three `.sdata2` words and one `.sdata2` byte, whose DOL addresses are
// 0x8041C2DC (count), 0x8041C2D8 (bump), 0x8041C2E0 (out-of-order flag) and 0x8041B2C8 (base, the
// word `fn_8033D2EC` returns), plus the table at 0x803E05C8. All four hold a DOL address or a DOL
// pointer with no host meaning, so they become file-local variables here. The two words that hold
// an address are `uintptr_t`, **not** `uint`: retail's are 32-bit because retail's pointers are,
// and narrowing a host pointer to `uint` would truncate it. They start null rather than holding
// retail's 0x3F800000 / 0x43E00000, because those are DOL addresses that mean nothing here and
// nothing host-side ever allocates from this pool, so there is nothing for them to point at.
//
// The body below is retail's own instruction sequence, not a stub: the whole function is
// bookkeeping, it calls nothing, and it allocates nothing. Nothing on the boot path allocates from
// this pool - both `AllocateLockedCache32__FUi` and `AllocateLockedCache4__FUi` are still undefined host-side - so in practice
// the count stays 0 and this is reached only from `fn_800CD4B8`, which is reachable only from
// `CMorphBall::FindClosestSpiderBallWaypoint`. **One deviation from retail, deliberate:** retail's
// `lwzx r0,r4,r0` reads `table[count - 1]` with an unshifted 32-bit index, which is
// `table[-1]` when the count is already 0. The index is masked here instead, because that read is
// out of bounds on a 64-bit host and a `-1` index is not something to hand the optimiser.
// ---------------------------------------------------------------------------

namespace {
uint sSmallBlockCount = 0; // 0x8041C2DC
uintptr_t sSmallBlockBump = 0; // 0x8041C2D8
unsigned char sSmallBlockOutOfOrder = 0; // 0x8041C2E0
uintptr_t sSmallBlockBase = 0; // 0x8041B2C8
void* sSmallBlockTable[16]; // 0x803E05C8
} // namespace

extern "C" void FreeLockedCache__FPv(void* p) {
  const uint count = sSmallBlockCount;
  sSmallBlockCount = count - 1;
  if (sSmallBlockOutOfOrder == 0) {
    if (sSmallBlockTable[(count - 1) & 15] == p) {
      sSmallBlockBump = sSmallBlockBase - reinterpret_cast< uintptr_t >(p);
    } else {
      sSmallBlockOutOfOrder = 1;
    }
  }
  if (sSmallBlockCount != 0) {
    return;
  }
  sSmallBlockBump = 0;
  sSmallBlockOutOfOrder = 0;
}

// ---------------------------------------------------------------------------
// fn_80045E18 / __as__6CLightFRC6CLight - the two 0x50-byte light copies behind
// `fn_800C9380` / `fn_800C93B0`
//
// Same arrangement and the same reason as `FreeLockedCache__FPv` above: both symbols live in the
// unclaimed `.text` gap that `auto_03_80045CDC_text.o` covers (it is the only object in
// `build/G2ME01/obj` that defines either, and `build.ninja` links it into `main.dol`, so the DOL
// needs nothing from here), while the **host** link needs them because
// `src/MetroidPrime/Player/CMorphBall.cpp` writes out retail's `fn_800C93B0`, which calls both.
// Without these two definitions `tools/link_gap.py` reports two more MISSING symbols than
// `docs/research/port_link_gap_list.md` documents, and the goal gate fails.
//
// They are the two branches of that function and they are **not** the same copy, which is the
// only reason it is written out at all:
//
//   fn_80045E18   0x80045E18, 0x54 = 21 insns - ten `lfd`/`stfd` pairs, i.e. all 0x50 bytes,
//                 the flag byte's word included. This is the copy `fn_800C93B0` makes when the
//                 destination is not in the scene yet: the light arrives whole and is then
//                 flagged.
//   __as__6CLight 0x80046384, 0xA4 = 41 insns, retail's *weak* `CLight` copy-assign helper.
//                 It copies 0x4C bytes plus the byte at +0x4C and **stops short of +0x50** -
//                 0x4D bytes in all - so it never disturbs the flag. This is the copy made
//                 when the destination is already registered and only its values change.
//
// Both bodies are therefore exactly the bulk copy retail performs, on the byte counts retail
// performs: no arithmetic, no branches, no allocation, nothing invented. `CLight` itself has no
// header in this port (`include/Kyoto/` has no `CLight.hpp`), so the two are reached by the retail
// names `fn_800C93B0` declares and the offsets are retail's, not a model's.
// ---------------------------------------------------------------------------

extern "C" void fn_80045E18(void* dst, const void* src) {
  memcpy(dst, src, 0x50);
}

extern "C" void __as__6CLightFRC6CLight(void* dst, const void* src) {
  memcpy(dst, src, 0x4D);
}

// ---------------------------------------------------------------------------
// fn_80258790 / fn_802588DC - the two workers behind `CMorphBall`'s spider-ball path parser
//
// Same arrangement and the same reason as `FreeLockedCache__FPv` and the pair above: both symbols live in
// the unclaimed `.text` gap `auto_03_80257AF8_text.o` covers (`nm` over every object in
// `build/G2ME01/obj` finds each of them only there, and `build.ninja` links that object into
// `main.dol`, so the DOL needs nothing from here), while the **host** link needs them because
// `src/MetroidPrime/Player/CMorphBall.cpp` now writes out retail's `fn_800CD244` and
// `fn_800CD35C` (0x800CD244, 0x800CD35C), which between them call both. Measured: without these two
// definitions the port carries 326 undefined against `docs/research/port_link_baseline.txt`'s 324
// and `tools/link_gap.py` fails with "gap grew".
//
// **These are transcriptions of retail's own instruction sequences, not stubs and not stand-ins** -
// every store below is a store retail makes, on the field retail reads it from. Neither is
// reachable from the port's boot: their only callers are `fn_800CD244` / `fn_800CD35C`, which are
// reached only from `CMorphBall::FindClosestSpiderBallWaypoint` (a scaffold) and the unclaimed
// `fn_8021EDF4`, so on the host they are not called at all. What the two are:
//
//   fn_80258790  0x80258790, 0x14C = 83 insns. Decode up to four opcodes out of the path's halfword
//                array and dispatch each through a 16-entry jump table at 0x803B8AC0 (entries
//                0x80258894, 0x802587EC, 0x802587FC, 0x80258818, 0x80258824, 0x8025883C,
//                0x80258864, 0x80258888, then the exit block eight more times), advancing the
//                cursor by 1, 2, 4 or 24 per opcode. It ends by loading one more halfword into the
//                cursor's +0x28, advancing the index once more and rounding it **up to an even
//                index**, and returns the two halfwords the opcodes parked in a packed 32-bit value.
//   fn_802588DC  0x802588DC, 0x94 = 37 insns. Advance the cursor past one waypoint: the halfword
//                at the index goes to both +0x24 and +0x2C, +0x30 and +0x1C become
//                `&array[index + 1]`, the index moves on by 1, 1 and 12, and two flag bits are
//                folded into the flag byte at +0x18.
//
// One retail quirk is preserved rather than fixed: the table is indexed by `opcode + 15`, so an
// opcode in 0xFFF1..0xFFFF reads the fifteen words *before* the table (0x803B8A9C, which hold
// unrelated DOL addresses and zeros) and branches through them. The transcription dispatches only
// opcodes 0..15 and treats everything else as the exit block, which is what retail does for every
// opcode a halfword stream can produce in practice.
//
// The two layouts are the ones `src/MetroidPrime/Player/CMorphBall.cpp` declares as
// `SMorphBallPathCursor` / `SMorphBallPath`; they are repeated here because that file's copies are
// file-local, and the two must agree - both are documented on both sides.
// ---------------------------------------------------------------------------

namespace {
// The cursor `fn_80258790` / `fn_802588DC` read and write. The layout is retail's 32-bit one, and
// the pointer-valued fields are `uintptr_t` for the reason `FreeLockedCache__FPv`'s own state is: retail's
// pointers are 32-bit and a 64-bit host pointer narrowed to `uint` would truncate. On the host that
// makes this layout *wider* than retail's - the same divergence `SMorphBallPathCursor` in
// `src/MetroidPrime/Player/CMorphBall.cpp` has - which is why these two are transcriptions for a
// path nothing on the host runs, not an interoperation contract.
struct SMorphBallPathCursorPort {
  unsigned short mOp;    // +0x00, one halfword of the stream
  unsigned char mPad02[2];
  uintptr_t mPtr04;      // +0x04
  uintptr_t mVal08;      // +0x08
  uintptr_t mPad0C;
  uintptr_t mVal10;      // +0x10
  uintptr_t mVal14;      // +0x14
  unsigned char mFlags;  // +0x18
  unsigned char mPad19[3];
  void* mNext;           // +0x1C, &mWaypoints[mIndex + 1]
  uint mIndex;           // +0x20, a halfword index into the path's array
  uint mRemaining;       // +0x24
  uint mCommands;        // +0x28
  uint mCounter;         // +0x2C
  unsigned short* mOut;  // +0x30, &mWaypoints[mIndex + 1]
};
struct SMorphBallPathPort {
  unsigned char mPad00[0x24];
  unsigned short* mWaypoints; // +0x24
};
} // namespace

extern "C" unsigned int fn_80258790(void* path, void* cursor) {
  SMorphBallPathPort* p = static_cast< SMorphBallPathPort* >(path);
  SMorphBallPathCursorPort* c = static_cast< SMorphBallPathCursorPort* >(cursor);
  uint budget = 4;
  uint low = 0;
  unsigned short high = 0;
  unsigned short* walk = p->mWaypoints + c->mIndex;
  for (;;) {
    const unsigned short op = *walk;
    budget -= 1;
    c->mIndex = c->mIndex + 1;
    walk += 1;
    switch (op) {
    case 1:
      c->mOp = 0xFFFF;
      break;
    case 2:
      c->mOp = *walk;
      c->mIndex = c->mIndex + 1;
      break;
    case 3:
      c->mPtr04 = 0;
      break;
    case 4:
      c->mPtr04 = reinterpret_cast< uintptr_t >(walk);
      walk += 24;
      c->mIndex = c->mIndex + 24;
      break;
    case 5:
      c->mVal08 = *reinterpret_cast< uintptr_t* >(walk);
      c->mIndex = c->mIndex + 4;
      low = walk[2];
      high = walk[3];
      walk += 4;
      break;
    case 6:
      c->mVal10 = *reinterpret_cast< uintptr_t* >(walk);
      c->mVal14 = *reinterpret_cast< uintptr_t* >(walk + 1);
      walk += 4;
      c->mIndex = c->mIndex + 4;
      break;
    case 7:
      c->mVal10 = 0;
      c->mVal14 = 0;
      break;
    default:
      break;
    }
    if (budget == 0) {
      break;
    }
  }
  c->mCommands = p->mWaypoints[c->mIndex];
  c->mIndex = c->mIndex + 1;
  if ((c->mIndex & 1) != 0) {
    c->mIndex = c->mIndex + 1;
  }
  return (static_cast< unsigned int >(high) << 16) | (low & 0xFFFF);
}

extern "C" void fn_802588DC(void* path, void* cursor) {
  SMorphBallPathPort* p = static_cast< SMorphBallPathPort* >(path);
  SMorphBallPathCursorPort* c = static_cast< SMorphBallPathCursorPort* >(cursor);
  c->mRemaining = p->mWaypoints[c->mIndex];
  c->mCounter = c->mRemaining;
  c->mIndex = c->mIndex + 1;
  c->mOut = p->mWaypoints + c->mIndex;
  c->mIndex = c->mIndex + 1;
  c->mNext = p->mWaypoints + c->mIndex;
  c->mIndex = c->mIndex + 12;
  // Retail folds two more flag bits into the byte at +0x18 here (0x80258928 and 0x80258960), and
  // **both stores are measured no-ops**: each is `cntlzw` into a `rlwimi` whose shift field never
  // selects a bit `cntlzw` can produce (it returns 0..32), so the byte comes back unchanged. They
  // are not written out here for that reason - reproducing a store that provably stores the value
  // it read would be noise, and the observable result is identical.
}

// ---------------------------------------------------------------------------
// fn_802275B8 / fn_80227624 - one `CGameOptions::unk2` element, written and read
//
// `unk2` is a `reserved_vector<rstl::pair<bool, bool>, 4>` (see
// `src/MetroidPrime/Player/CGameOptions.cpp:119` and `:146`), and these two are its element
// serialisers. Both are small, straight-line, and call only already-named retail functions, so
// the whole body is the call list:
//
//   802275b8 <fn_802275B8>:                        ; (pair<bool,bool>* p, CBitStreamWriter& out)
//   802275c0:  li   r5,1
//   802275d0:  lbz  r4,0(r3)                        ; p->first
//   802275dc:  neg  r0,r4 ; or r0,r0,r4 ; srwi r4,r0,31   ; r4 = p->first != 0
//   802275ec:  bl   WriteBits__16CBitStreamWriterFUiUi
//   802275f0:  lbz  r4,1(r3)                        ; p->second
//   ...            the same five instructions, one more WriteBits
//
//   80227624 <fn_80227624>:                        ; (pair<bool,bool>* p, CBitStreamReader& in)
//   80227638:  li   r4,1
//   80227648:  bl   ReadBits__16CBitStreamReaderFUi
//   8022764c:  neg  r0,r3 ; or r0,r0,r3 ; srwi r0,r0,31 ; stb r0,0(r30)
//   80227664:  bl   ReadBits__16CBitStreamReaderFUi
//   ...            the same, storing to +1
//
// The `neg/or/srwi` triple is MWCC's `x != 0` and the `stb` of a bool is 0 or 1, so
// `WriteBits(p->first, 1)` and `p->first = in.ReadBits(1) != 0` are the same instructions.
// The two are each other's inverse and the only callers are `CGameOptions::PutTo` and
// `CGameOptions::CGameOptions(CBitStreamReader&)`, one per element, four elements each.
// Measured effect on the port link gap: 553 -> 551.
// ---------------------------------------------------------------------------

extern "C" void fn_802275B8(rstl::pair< bool, bool >& value, CBitStreamWriter& out) {
  out.WriteBits(value.first != 0, 1);
  out.WriteBits(value.second != 0, 1);
}

extern "C" rstl::pair< bool, bool > fn_80227624(CBitStreamReader& in) {
  rstl::pair< bool, bool > value;
  value.first = in.ReadBits(1) != 0;
  value.second = in.ReadBits(1) != 0;
  return value;
}

// `lbl_803AFAA0` is `.rodata:0x803AFAA0`, `size:0x10`, and it is the one retail read-only object
// that is *two* strings the linker merged: bytes 0..6 are the `??(??)?` that stands in for
// `__FILE__` in `Kyoto/Alloc/CMemory.hpp:33`'s throwing `operator new`, and byte 7 starts the
// `".pak"` that `CResLoader::AddPakFileAsync` (0x802FC268, a Matching unit -
// `src/Kyoto/CResLoaderAddPakFileAsync.cpp`) appends to the pak name:
//
//   803afaa0  3f 3f 28 3f 3f 29 00 2e 70 61 6b 00 00 00 00 00   "??(??)..pak"
//
// That unit names the object rather than writing the literal, because a Matching unit may not own
// a .rodata byte, and it is why the bytes have to include the `.pak`: `lbl_803AFAA0 + 7` is the
// suffix the function hands to retail's `rstl::basic_string::operator+(const char*)`. Reproduced
// whole so the +7 arithmetic is retail's arithmetic.
extern "C" const char lbl_803AFAA0[] = "??(??)..pak";

// lbl_803B0098 - the pak-format version message, and retail's "??"(??)?" 76 bytes in
// ---------------------------------------------------------------------------
//
// `.rodata:0x803B0098`, `size:0x58`, and like `lbl_803AFAA0` above it is **two** strings retail's
// linker merged into one object: the 72-character message `CPakFile::InitialHeaderLoad` hands to
// `sprintf` at 0x80323F58, and at +76 the 7-byte `"??"(??)?"` that the inlined
// `rstl::rmemory_allocator::allocate` in `Kyoto/CPakFile.cpp` passes as its `CCallStack`'s
// file-and-line text (retail's `addi r5,r5,76` at 0x80324AB4).
//
//   803b0098  25 73 3a 20 49 6e 63 6f 6d 70 61 74   "%s: Incompat"
//   803b00d8  72 65 20 75 73 69 6e 67 20 25 78 00   "re using %x\0"
//   803b00e4  3f 3f 28 3f 3f 29 00                  "??(??)\0"
//
// The matching build resolves this from dtk's retail object, which defines it as a global `R`.
// The port build has no such object, so it is defined here, next to `lbl_803AFAA0` for the same
// reason: a translation unit that names retail's read-only object needs it defined somewhere the
// linker can see, and this file is in `files.cmake` but not in `configure.py`, so it cannot
// affect `main.dol` or any REL.
extern "C" const char lbl_803B0098[] =
    "%s: Incompatible pak file version -- Current version is %x, you're using %x"
    "\0\0\0\0"
    "??(??)?";

// fn_802FC350 / fn_802FC378 - CResLoader's in-progress pak list
// ---------------------------------------------------------------------------
//
// **These two used to be transcribed here and are now gone**: `src/Kyoto/CResLoaderInsert.cpp`
// is a `Matching` DOL unit claiming retail's `.text:0x802FC350..0x802FC420` and defines both, in
// both builds, out of `rstl::list`'s own members. It could not be done before because the
// transcription could only *describe* the list - `SPakLoadList` here spelled out the head at
// +4, the tail at +8 and the count at +0x14 because a 64-bit host's `rstl::list` has none of
// those offsets - whereas the real `do_insert_before` works on the real list at any width.
//
// What the transcription was for, and what is now measured:
//
//   fn_802FC350  0x802FC350, 0x28 - forwards to the insert with the list's own `x8_end`
//   fn_802FC378  0x802FC378, 0xA8 - the insert: a 16-byte node, an 8-byte item copy guarded
//                by `addic. r5,r3,8 / beq`, the caller's flag byte cleared, the head fixup,
//                the two link stores and `++count`
//
// **The `stb r0,0(r30)` that clears the caller's flag byte is the item's copy constructor**,
// not a statement in the insert. `fn_802FC378` is `rstl::list< SPakLoadEntry >::
// do_insert_before(node*, const SPakLoadEntry&)` and nothing else: its 0xA8 bytes are
// identical, register for register, to retail's own named
// `do_insert_before<list<auto_ptr<CFilePreloadData>>>` at 0x803445DC, whose element type has
// an auto-relinquishing copy constructor. `SPakLoadEntry` in `Kyoto/CResLoader.hpp` has that
// constructor for the same reason, and `fn_802FD174` (`do_erase`) confirms the item is
// `rstl::auto_ptr< CPakFile >`: it `lbz`es the flag byte and then calls `__dt__CPakFile` on
// `*(item+4)`, which is that class's destructor.
//
// `AddPakFileAsync`'s `TARGET_PC` half used to spell the insert out itself for the same
// 64-bit reason; it now calls `fn_802FC350` like the retail side does, which is why the two
// halves of that function finally agree.

// `__dt__12CPlayerStateFv` is retail's own name for `CPlayerState`'s deleting destructor
// (0x8000939C), and `src/MetroidPrime/Player/CPlayerStateRefRelease.cpp` (`fn_8000934C`,
// `rstl::rc_ptr<CPlayerState>::ReleaseData`) calls it by that C name so that mwcceppc emits
// retail's relocation. The host's destructor is `CPlayerState::~CPlayerState()`, so this is the
// MWCC deleting-destructor convention in terms of it: a null `self` does nothing, and a positive
// `flag` frees the storage after destroying the object.
extern "C" void __dt__12CPlayerStateFv(CPlayerState* self, int flag) {
  if (self == nullptr) {
    return;
  }
  if (flag > 0) {
    delete self;
  } else {
    self->~CPlayerState();
  }
}

// ---------------------------------------------------------------------------------------
// The deleting destructors `MetroidPrime/main.cpp` models out of line, for the PC link.
// ---------------------------------------------------------------------------------------
//
// `include/MetroidPrime/CMapWorldInfo.hpp` and `include/MetroidPrime/CErrorOutputWindow.hpp`
// declare `~CMapWorldInfo` and `~CErrorOutputWindow` rather than defining them inline, because
// retail keeps one out-of-line copy of each in `MetroidPrime/main.cpp`'s object
// (`__dt__13CMapWorldInfoFv` at 0x800090A8, 124 bytes; `__dt__18CErrorOutputWindowFv` at
// 0x800078F8, 96 bytes) and an inline body in a class the matching build never deletes through is
// never emitted at all. That is a *decompilation* arrangement: it is what makes those two
// functions reproduce retail's bytes.
//
// **It costs the PC link two definitions, and they are here.** The port compiles
// `MetroidPrime/CErrorOutputWindow.cpp` and `MetroidPrime/CMapWorldInfo.cpp`'s users, and with
// the destructors no longer inline the host compiler emits a reference to
// `CMapWorldInfo::~CMapWorldInfo()` and, because a declared-but-undefined destructor is a
// class's key function, to `vtable for CErrorOutputWindow` as well. Both are measured: the port's
// undefined count went 250 -> 252 with exactly these two names, and
// `docs/research/port_link_baseline.txt` names them. This file is where a PC-only definition
// belongs - it is not a `configure.py` unit, so a definition here cannot collide with a retail
// object at DOL link time and cannot perturb any unit's `.text`.
//
// The bodies are the ones retail's own generated shape reduces to, on a host:
// `CMapWorldInfo`'s four members (`rstl::bit_vector` x2, `rstl::vector` x2) are destroyed by the
// compiler and then the storage goes; `CErrorOutputWindow`'s is `CIOWin` plus four bitfields and
// a pointer, and its body is empty for the same reason `CMainFlow::~CMainFlow()` is empty in
// `src/MetroidPrime/CMainFlowDtor.cpp`.
CMapWorldInfo::~CMapWorldInfo() {}

// Declared unconditionally in `CWorldLayerState.hpp` (the matching build does not define
// TARGET_PC, so an `#ifdef` would hide it from exactly the build that needs it), and defined here
// for the same reason as `~CMapWorldInfo` above: this file is not a `configure.py` unit, so a
// definition here cannot collide with a retail object at DOL link time nor perturb any unit's
// `.text`. The body is empty for the same reason: the compiler destroys the four members
// (`rstl::vector`, `rstl::bit_vector`, two `rc_ptr`) and the storage. `CWorldLayerState` is not
// polymorphic, so this is only ever a call and never a vtable slot. Not on the boot path.
CWorldLayerState::~CWorldLayerState() {}

CErrorOutputWindow::~CErrorOutputWindow() {}

// ---------------------------------------------------------------------------------------
// `lbl_803A9F38` - retail's own bytes, read out of main.elf rather than written from a guess.
// ---------------------------------------------------------------------------------------
//
// `CErrorOutputWindow::CErrorOutputWindow(bool)` (retail 0x8018169C, 0xB4) stores a pointer to this
// object, and `config/G2ME01/symbols.txt:17224` describes it as `.rodata size:0x14 data:string`.
// The 0x14 bytes at 0x803A9F38 in main.elf are:
//
//   4572726f 72206f75 74707574 2077696e 646f7700   "Error output window\0"
//
// which is 19 characters and a NUL = 0x14 exactly, so the symbol's size and its type in
// `symbols.txt` agree with retail's bytes rather than with an assumption. It is a real string
// with real text in it, so a host definition can be *the same text* - there is nothing to lie
// about, which is the test every host stand-in should be held to.
//
// **`lbl_803B5910` is deliberately NOT defined here, and the reason is worth stating because the
// obvious move is wrong.** `symbols.txt:18177` calls it `.data size:0x1C`; its 7 words are
//
//   00000000 00000000 800078f8 801815f8 80181684 80181480 80049e10
//
// The first two words being zero is a vtable header (offset-to-top, RTTI) and the remaining five
// are **retail code addresses**, so this is retail's own `vtable for CErrorOutputWindow`. Writing
// those five words as host data would be a lie: they are not callable on a PC. The correct host
// definition is a *real* vtable, which means defining the class's key function - the arrangement
// `src/Kyoto/CResFactoryPortVirtuals.cpp` uses and documents.
//
// That is not done, and the reason is a measured one rather than a scheduling one:
// **`CErrorOutputWindow` cannot be `Matching` at all.** `tools/errwin`'s finding, recorded below
// and in `docs/HANDOFF.md`, is that the compiler retail used for this function emits
// `clrlwi r3,r31,24 ; cntlzw` where a different version of the same compiler emits bare `cntlzw`,
// and retail's own binary contains **both** forms 22 KB apart. So the vtable work buys a
// `NonMatching` function a working vtable, and the body is 78.56% regardless.
//
// SUPERSEDED 2026-09-29: the port now links upstream's header-based
// `src/MetroidPrime/CErrorOutputWindow.cpp` instead of `CErrorOutputWindowCtor.cpp`, so the real
// vtable exists and this string has no port user left. Without it frame 1 faulted in
// `win->PreDraw()` (docs/research/boot_probe.md).
extern "C" const char lbl_803A9F38[] = "Error output window";

// ---------------------------------------------------------------------------------------
// The two managers `CCameraManager::Update` drives, as PC-side stand-ins.
// ---------------------------------------------------------------------------------------
//
// `CCameraManager::Update` (retail 0x801AC434, 0x90 bytes) drives three members of the separate
// hint and camera-shake managers, in this order and with these arguments, read out of main.elf:
//
//   801ac458: 80 63 00 84  lwz     r3,132(r3)        ; this->mCameraHintManager
//   801ac45c: 48 00 d3 3d  bl      801b9798          ; CHintManager::Update(dt)
//   ...              UpdateCameras / UpdateAudioListener ...
//   801ac480: 80 7e 00 88  lwz     r3,136(r30)       ; this->mCameraShakeManager
//   801ac488: 48 03 b4 4d  bl      801e78d4          ; CCameraShakeManager::Update(dt, mgr)
//   ...              UpdateFilters / UpdateCameraHistory ...
//
// and `GetCurrentCameraTransform` (0x801ABE40) and `GetGlobalCameraTranslation` (0x801ABDCC)
// both call a third of them, which takes a `const CStateManager&` and returns the accumulated
// shake offset by value:
//
//   801abe80: 38 61 00 08  addi    r3,r1,8           ; hidden return-slot pointer, r1+8
//   801abe84: 48 03 b9 a9  bl      801e782c
//
// Those three bodies live in retail-derived units the port does not compile, and
// `CCameraManager`'s own constructor still does not allocate either manager, so nothing in the
// port calls them. They are defined here rather than in `PortLinkStubs.cpp` because the stubs
// there are only sound for symbols no *reachable* object references, and these are referenced by
// `CCameraManager.o`.
//
// **This is not decompilation and is not claimed to match retail.** `GetShakeOffset` answers
// "no shake", which is the same value the stub body in `CCameraManager.cpp` returned before
// `Update` was recovered, and each of the three announces itself once if it is ever reached - a
// stand-in that says so is safe where a plausible-looking one is not. The decompilation still
// owes the real bodies, in their own units.
namespace {
bool ReportedCameraManagerStandIn(const char* name) {
  static bool reported = false;
  if (!reported) {
    reported = true;
    printf("port stand-in reached: %s - the real body is not written\n", name);
  }
  return reported;
}
} // namespace


// `CGameCamera::GetCameraManager(CStateManager const&) const`, retail 0x801B0B50 (16 bytes:
// `lwz r3,off(r3) / bctr`-shaped `blr` pair after the `lwz` of the manager array). Its body is
// already written at `src/MetroidPrime/Cameras/CGameCamera.cpp:165`, and that file is excluded from
// `files.cmake` with a measured reason (tools/check_files_cmake.py:427). Writing
// `CBallCamera::CheckFailsafe{From,To}MorphBallState` (retail 0x801AA364 / 0x801A9A30) put a call to
// it in `CBallCameraTransitions.o`, which *is* listed, so without a definition here the port's
// undefined count grew 324 -> 325 and `tools/link_check.sh --strict` failed.
//
// This is the same body, not a stand-in: the manager array is read and returned, so a caller gets
// the manager it asked for. It duplicates `CGameCamera.cpp`'s copy the same way
// `CGameCameraSetAspectRatio.cpp` duplicates `SetAspectRatio`; when `CGameCamera.cpp` is listed,
// this line and that file are what have to go.
CCameraManager& CGameCamera::GetCameraManager(const CStateManager& mgr) const {
  return *const_cast< CCameraManager* >(mgr.GetCameraManager(mControllerIdx));
}

// `CGameCamera::Player(CStateManager&) const` and `CBallCamera::TeleportCamera(const CVector3f&,
// CStateManager&)`, opened the same way. Writing
// `CBallCamera::UpdateTransitionToBallCamera(CStateManager&)` (retail 0x801A8B78, 772 bytes) in
// `CBallCameraTransitions.cpp` - a unit `files.cmake` *does* list - put calls to both into that
// object, and both bodies live in `NonMatching` units the port does not list
// (`CGameCamera.cpp:256` and `CBallCamera.cpp:151`), so the port's undefined count rose and
// `tools/link_gap.py` reported two new names.
//
// These are the same bodies, not stand-ins. `Player` is `mgr.GetPlayer(mControllerIdx)` - the
// inline array read the class's own copy performs, so a caller gets the player for its own
// controller. `TeleportCamera` moves the camera, reteleports the three collider groups and
// teleports the tracked collision actor, which is what its name says. Like the line above, both
// duplicate their `NonMatching` unit's copy; when those units are listed, these go.
CPlayer& CGameCamera::Player(CStateManager& mgr) const { return *mgr.GetPlayer(mControllerIdx); }

void CBallCamera::TeleportCamera(const CVector3f& position, CStateManager& mgr) {
  mDampedPos = position;
  mSmallColliders.TeleportColliders(position);
  mMediumColliders.TeleportColliders(position);
  mLargeColliders.TeleportColliders(position);
  if (CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(mCollisionActorId))) {
    actor->SetTranslation(position);
  }
}

// Writing `CBallCamera::TransitionFromMorphBallState` (retail 0x801AA008, 860 bytes) in
// `CBallCameraTransitions.cpp` - a unit `files.cmake` *does* list - put one more call into that
// object, and the body is not reachable from a port TU:
//
//   * `CBallCamera::DetectCollision` is at `CBallCamera.cpp:343`, still the `return false` TODO
//     stub the decompilation has, and that unit is excluded from `files.cmake`.
//
// So it is a stand-in, announced on first call like `CHintManager::Update` above: not
// decompilation, and not claimed to match retail. It answers "no collision", which
// is what its own unit answers, and deliberately leaves `distance` alone rather than inventing a
// sweep length.
bool CBallCamera::DetectCollision(const CVector3f& from, const CVector3f& to, float radius,
                                  float& distance, const CStateManager& mgr, int controllerIdx) {
  ReportedCameraManagerStandIn(
      "CBallCamera::DetectCollision(CVector3f const&, CVector3f const&, float, float&, "
      "CStateManager const&, int)");
  return false;
}

// `fn_8022EA5C` (retail 0x8022EA5C, 0x64 bytes) is in the *same* unclaimed gap as
// `CHintManager::RemoveHint` above - `config/G2ME01/splits.txt` claims 0x8022E134..0x8022E13C, then
// 0x8022EB54..0x8022EB9C (`Carve8022EB54.c`), and then 0x8022EB9C..0x8022EBC8, so
// 0x8022E13C..0x8022EB54 belongs to no unit of ours. Its callers
// are `CPlayer::ResetRezbitState` (0x8022B164) and `CPlayer::StopRezbitState` (0x8022B0D8), which
// reach it through `mRezbitEffectToken`, so without this the port goes from 250 to 251 undefined
// and `tools/link_check.sh` fails STRICT. The declaration, with the body read off the
// disassembly, is at the end of `include/MetroidPrime/Player/CPlayer.hpp`.
//
// Same caveat as the stand-ins above: **not decompilation and not claimed to match retail.** It
// announces itself on first call. The stand-in deliberately does *not* clear the bit or touch the
// gun: those are the observable effects, and doing half of them silently is worse than doing none.
void fn_8022EA5C(uint& gunDrawBlocks, CStateManager& mgr, int playerIndex) {
  ReportedCameraManagerStandIn("fn_8022EA5C(uint&, CStateManager&, int)");
}

// `fn_8022A640` (retail 0x8022A640, 0x22C bytes) is in an unclaimed gap of the DOL for the same
// reason as `fn_8022EA5C` above - `config/G2ME01/splits.txt` ends
// `MetroidPrime/ScriptLoader/BacteriaSwarm.cpp` at 0x8022A5AC and the next claim,
// `MetroidPrime/Player/CPlayerVisor.cpp`, starts at 0x8022AF0C - and its one caller we compile,
// `CPlayer::fn_8022af0c` (0x8022AF0C), now reaches it. Without this the port goes from 250 to
// 251 undefined and `tools/link_check.sh` fails STRICT. The declaration, with the argument setup
// it was read from, is at the end of `include/MetroidPrime/Player/CPlayer.hpp`.
//
// Same caveat as the stand-ins above: **not decompilation and not claimed to match retail.** It
// announces itself on first call. It deliberately does *not* create the hint: retail's body is
// 0x22C bytes and does, so answering with a plausible id would be worse than answering with the
// invalid one, which is also what `fn_8022af0c` itself answers when there is no hint manager.
TUniqueId fn_8022A640(CHintManager* hints, CStateManager& mgr, const rstl::string& label, int flags,
                      int controls, int& id, const TUniqueId& source, float duration, float fadeInTime,
                      float fadeOutTime) {
  ReportedCameraManagerStandIn("fn_8022A640(CHintManager*, CStateManager&, rstl::string const&, int, "
                               "int, int&, TUniqueId const&, float, float, float)");
  return kInvalidUniqueId;
}

void CCameraShakeManager::Update(float dt, CStateManager& mgr) {
  ReportedCameraManagerStandIn("CCameraShakeManager::Update(float, CStateManager&)");
}

// Retail's unnamed fn_801E7EC0 (0x120): the shake manager's "add this shake" entry, called by
// `CPatterned::ApplyScreenShake`. Its list lives in the unrecovered CCameraShakeManager layout.
void fn_801E7EC0(CCameraShakeManager* self, const CCameraShakerData& data, CStateManager& mgr,
                 int arg0, int arg1) {
  ReportedCameraManagerStandIn("fn_801E7EC0(CCameraShakeManager*, CCameraShakerData const&, "
                               "CStateManager&, int, int)");
}

CVector3f CCameraShakeManager::GetShakeOffset(const CStateManager& mgr) const {
  ReportedCameraManagerStandIn("CCameraShakeManager::GetShakeOffset(CStateManager const&) const");
  return CVector3f::Zero();
}

// The three `CScriptTrigger` occupancy methods `CCameraManager`'s trigger functions call, for the
// same reason as the three above: their bodies are in
// `src/MetroidPrime/ScriptObjects/CScriptTrigger.cpp`, which `files.cmake` excludes because its
// out-of-line `GetTriggerBoundsWR` duplicates `PortLinkStubs.cpp`'s. Retail's are
// `RemoveInhabitantIfOutside` (0x800710F8, 0x180), `ReplaceInhabitant` (0x80071278, 0x114) and
// `UpdateCameraInhabitant` (0x80071D2C, 0x298), all declared with "guessed name" in
// `CScriptTrigger.hpp`. Same caveat: not decompilation, each announces itself once if reached,
// and the decompilation still owes the real bodies. Each returns `false` - the "no change" value -
// because a stand-in reporting a change would be a lie the port could act on.
uchar CScriptTrigger::RemoveInhabitantIfOutside(TUniqueId id, CStateManager& mgr) {
  ReportedCameraManagerStandIn("CScriptTrigger::RemoveInhabitantIfOutside");
  return false;
}

uchar CScriptTrigger::ReplaceInhabitant(TUniqueId oldId, TUniqueId newId, CStateManager& mgr) {
  ReportedCameraManagerStandIn("CScriptTrigger::ReplaceInhabitant");
  return false;
}

void CScriptTrigger::UpdateCameraInhabitant(TUniqueId id, CStateManager& mgr) {
  ReportedCameraManagerStandIn("CScriptTrigger::UpdateCameraInhabitant");
}

// `CSurfaceCamera`'s out-of-line script-id setter, retail 0x801E95A8 (12 bytes:
// `addi r4,r1,8 / sth r0,8(r1) / lwz r3,52(r31) / bl 0x801E95A8` at the call site in
// `CCameraManager::ClearSurfaceCamera`, so the callee is `sth r0,512(r3)` plus an epilogue - the
// same word CPathCamera's and CSpindleCamera's inline setters store).
//
// It is declared and left undefined in `CSurfaceCamera.hpp` **on purpose**: retail's caller is a
// real `bl`, so an inline setter in the header would compile to `sth r0,512(r3)` in
// `CCameraManager.o` and `ClearSurfaceCamera` would stop matching. Same stand-in caveat as the
// six above - not decompilation, announces itself once if reached, and the real body is owed by
// whatever unit ends up claiming 0x801E95A8.
void CSurfaceCamera::SetScriptCameraId(TUniqueId id) {
  ReportedCameraManagerStandIn("CSurfaceCamera::SetScriptCameraId");
}

// `CFixedCamera`'s out-of-line script-id setter, retail 0x80228910 (12 bytes: `sth r0,524(r3)` plus
// an epilogue, from the call site in `CCameraManager::SetFixedCamera`). Declared and left undefined
// in `CFixedCamera.hpp` for the same reason as `CSurfaceCamera::SetScriptCameraId`; unlike that
// stand-in this one is the real body, since the member's offset is known.
void CFixedCamera::SetScriptCameraId(TUniqueId id) { mScriptCameraId = id; }

// CAreaOctTree::Node's two out-of-line accessors.
//
//   GetTriangleArray__Q212CAreaOctTree4NodeCFv = .text:0x80247960; // type:function size:0x24
//   GetChild__Q212CAreaOctTree4NodeCFi          = .text:0x80247984; // type:function size:0x150
//
// Both bodies are written in src/WorldFormat/CAreaOctTree.cpp, and both are decompiled
// (GetTriangleArray 100.00%, GetChild 65.48% in build/report.json). `files.cmake` does not
// compile that file - tools/check_files_cmake.py excludes it with a measured reason, and that
// reason predates the oct-tree line tests, which are the first callers of either accessor.
// `src/WorldFormat/CAreaOctTree_Tests.cpp` IS in the port build, so reconstructing
// LineTestInternal and LineTestExInternal (which call both) put two more names in the port's
// undefined set: measured 250 -> 252 with tools/link_check.sh --strict.
//
// These are copies, exactly as the eight `TypesMatch` bodies above are copies of
// `src/MetroidPrime/TypesMatch.cpp`. **Adding `src/WorldFormat/CAreaOctTree.cpp` to
// `files.cmake` later would duplicate both** - and would close more than it opens, since the
// exclusion's own measurement (it opens `CCollisionPrimitiveData`'s constructor and closes
// nothing) is taken with these two callers absent. Either this block moves into that file, or
// the two definitions come out of it.
// BoxFromIndex is a file-static in src/WorldFormat/CAreaOctTree.cpp (itself a copy, see below),
// so the GetChild copy needs its own.
static CAABox PortBoxFromIndex(int index, const CVector3f& min, const CVector3f& center,
                              const CVector3f& max) {
  switch (index) {
  case 0:
    return CAABox(min, center);
  case 1:
    return CAABox(CVector3f(center.GetX(), min.GetY(), min.GetZ()),
                  CVector3f(max.GetX(), center.GetY(), center.GetZ()));
  case 2:
    return CAABox(CVector3f(min.GetX(), center.GetY(), min.GetZ()),
                  CVector3f(center.GetX(), max.GetY(), center.GetZ()));
  case 3:
    return CAABox(CVector3f(center.GetX(), center.GetY(), min.GetZ()),
                  CVector3f(max.GetX(), max.GetY(), center.GetZ()));
  case 4:
    return CAABox(CVector3f(min.GetX(), min.GetY(), center.GetZ()),
                  CVector3f(center.GetX(), max.GetY(), max.GetZ()));
  case 5:
    return CAABox(CVector3f(center.GetX(), min.GetY(), center.GetZ()),
                  CVector3f(max.GetX(), center.GetY(), max.GetZ()));
  case 6:
    return CAABox(CVector3f(min.GetX(), center.GetY(), min.GetZ()),
                  CVector3f(center.GetX(), center.GetY(), max.GetZ()));
  case 7:
    return CAABox(center, max);
  default:
    return CAABox(min, max);
  }
}

CAreaOctTree::Node CAreaOctTree::Node::GetChild(int index) const {
  const ETreeType type = GetChildType(index);
  const uint* offsets = reinterpret_cast< const uint* >(mPtr + sizeof(uint));
  const void* node = mPtr + 9 * sizeof(uint) + offsets[index];
  if (type == kTT_Leaf) {
    const CAABox bounds = *reinterpret_cast< const CAABox* >(node);
    return Node(node, bounds, GetOwner(), type);
  }
  const CVector3f center = 0.5f * (mAabb.GetMinPoint() + mAabb.GetMaxPoint());
  const CAABox bounds = PortBoxFromIndex(index, mAabb.GetMinPoint(), center, mAabb.GetMaxPoint());
  return Node(node, bounds, GetOwner(), type);
}

CAreaOctTree::TriListReference CAreaOctTree::Node::GetTriangleArray() const {
  static const ushort skDeadArray[2] = {0, 0};
  if (GetTreeType() != kTT_Leaf) {
    return TriListReference(skDeadArray);
  }
  return TriListReference(mPtr);
}

// `CElementGen::SetExternalParam`, retail 0x802D0D50, 0x10 = 16 bytes: `mExternalVars[index] = value`.
// CElementGen.cpp is out of the port build (it opens more undefined than it closes), so this
// stand-in holds the port link. If that file joins `files.cmake`, this definition must go.
void CElementGen::SetExternalParam(uint index, float value) { mExternalVars[index] = value; }

// 0x8032194C is `CStreamAudioManager::Update(float)` (`symbols.txt`), and the port builds that
// body. `Carve80003858.c` calls it by the address name, which on the host is a different symbol;
// without this the frame's audio update was a stub that only printed. It is here and not in
// `CStreamAudioManager.cpp` because that file is a `Matching` unit.
extern "C" void fn_8032194C(float dt) { CStreamAudioManager::Update(dt); }

// Retail 0x800E0BE4, 0x240 = 576 bytes, in `main/auto_03_800E028C_text` and **not claimed by any
// unit**: it is the byte immediately above `src/MetroidPrime/Carve800E0B80.c`'s own claim
// (0x800E0B80..0x800E0BE4) and stays in dtk's `auto_03_800E0BE4_text.o`, which the matching build
// links, so that carve cannot reach it.
//
// **This body used to be `fn_800E0B80`'s, and moving it here is what keeps the port's behaviour
// unmoved.**  `src/MetroidPrime/Carve800E0B80.c` now defines 0x800E0B80 as retail's twenty-five
// instructions and calls this name at retail's own `bl` (0x800E0BB4) with the deleting flag `1`,
// so a host definition of this name is what the port needs in place of the `delete item` that body
// used to spell.  What retail's function does, measured from
// `build/G2ME01/asm/auto_03_800E0BE4_text.s:9-171` (0x800E0BE4..0x800E0E24 - the residue object
// this carve's claim left behind, named for its own first byte): a null-receiver early return,
// then **twenty call sites** - 17 `bl` and 3 `bctrl`, counted in that file - walking the members
// down from +0x144 (`CGuiFrame`) through +0xA0, +0x90 (`rstl::vector<CToken>`), +0x80
// (`rstl::vector<uint>`), +0x70, an in-place `CToken` walk at +0x54, the heap pair at +0x48
// (`CGuiFrame`, `CGuiFrameLoader`, `CMemory::Free`), +0x44 (`CPauseScreen`), +0x40, +0x3C, +0x38
// (virtual), +0x34, +0x30 (virtual), +0x2C (virtual), +0x28, two `CToken` teardowns at +0x14 and
// +0x08 - and then the signed-halfword deleting-flag test and the twentieth call,
// `Free__7CMemoryFPCv` on the object itself.
//
// **That trade is announced, not hidden.**  `CInGameGuiManager.cpp` is a structure-first scaffold
// with no destructor at all, so on the host `delete` runs the implicit destructor and releases the
// block - which is what retail's flag-1 path does at the end of the above - and every class named
// in that list has no host destructor either.  So `delete` is the same work here, and it is what
// this body has always stood in for; it is **not** a claim that 0x800E0BE4 is decompiled.  The
// `-1` case is spelled without a free because **no other caller exists to need it**: the single
// `bl fn_800E0BE4` in `build/G2ME01/main.elf` is 0x800E0BB4, retail's own call from inside
// 0x800E0B80..0x800E0BE4, and it passes `li r4,1`.
extern "C" void fn_800E0BE4(void* self, int flag) {
  (void)flag;
  delete static_cast< CInGameGuiManager* >(self);
}

// Retail 0x801449C8, `CGameState::CGameState()`. `CGameGlobalObjectsCtor.cpp` and
// `CMainResetGameState.cpp` are mwcceppc units that spell the call by its `fn_` name; on the host
// the constructor is `Player/CGameState.cpp`'s.
extern "C" CGameState* fn_801449C8(CGameState* self) { return new (self) CGameState(); }

// Retail 0x8028139C, 0x28 = 40 bytes.  This stand-in predates the claim
// `src/MetroidPrime/Carve8028139C.cpp` (0x8028139C..0x80281490) and stays, because that carve is
// inside `#ifdef __MWERKS__`: it reproduces retail's bytes for the DOL, whose link resolves the
// `bl` against retail's own `rstl::vector< void* >` copy constructor at 0x8005C2D4, and a host
// compilation would mangle that name differently and add one more undefined symbol to the port
// link.  **The two are not duplicates**: `tools/gate.sh`'s `port link dups` step compiles the
// port, where the carve is an empty translation unit, so `fn_8028139C` is defined exactly once in
// the port link and exactly once in the DOL.
//
// The stand-in was written when the only claim that named the symbol was
// `src/MetroidPrime/Carve80281310.c`, which declares `fn_8028139C` and never defines it - listing
// that carve in `files.cmake` put the name in the port's undefined set, which
// `tools/link_check.sh --strict` fails on.
//
// **What retail's bytes are** (`build/G2ME01/asm/auto_03_8028139C_text.s:10-20`):
//
//     stwu r1,-0x10(r1) ; mflr r0 ; cmplwi r3,0 ; stw r0,0x14(r1) ; beq ; bl <the ctor above>
//
// i.e. a null test on the destination and then the callee, which is retail's own
// `rstl::vector<void*, rmemory_allocator>::vector(const vector&)` copy constructor -
// `__ct__Q24rstl37vector<Pv,Q24rstl17rmemory_allocator>FRCQ24rstl37vector<Pv,Q24rstl17rmemory_allocator>`
// (0x8005C2D4, `symbols.txt:1811`), a symbol `<` and `>` make unspellable in C. That is the same
// two-step shape as `Player/CGameStateBlockConstruct.cpp`'s `fn_80004D5C` and as `fn_801449C8`
// above. The header's own definition (`include/rstl/vector.hpp:127-136`) is the copy the bytes are
// the compilation of, so this forwards to it; nothing is invented here and the object is left
// untouched on the null path, exactly as retail's `beq` leaves it.
extern "C" void fn_8028139C(void* self, const void* src) {
  typedef rstl::vector< void*, rstl::rmemory_allocator > SVector;
  if (self != nullptr) {
    new (static_cast< void* >(self)) SVector(*static_cast< const SVector* >(src));
  }
}

// Retail 0x802860BC: `CollisionUtil::AddAverageToFront(const CCollisionInfoList&,
// CCollisionInfoList&)` - declared `include/Collision/CollisionUtil.hpp:14`, implemented by retail's
// own object, and named by `config/G2ME01/symbols.txt`.  Its decompiled body is
// `src/Collision/CollisionUtil.cpp:871`, which is a `NonMatching` unit of `configure.py:445` and
// **not in `files.cmake`**, so the port has no such function at all.
//
// `src/MetroidPrime/Carve801285DC.c` claims `.text 0x801285DC..0x8012862C` as a `Matching` unit,
// and its first function's whole body is `mr r3,r4 / mr r4,r5 / bl <this name>`, so the `bl` has to
// carry retail's symbol.  mwcceppc is given the name verbatim - MWCC's old mangling is
// `[A-Za-z0-9_]` only, so a C declaration can spell it, the same trick
// `src/MetroidPrime/Carve800E1548.c:77` uses on `Free__7CMemoryFPCv` - which is right for the DOL
// and binds to nothing on a host link.  Listing that carve in `files.cmake` therefore put
// `AddAverageToFront__13CollisionUtilFRC18CCollisionInfoListR18CCollisionInfoList` in the port's
// undefined set, which `tools/link_check.sh --strict` fails on.
//
// **This definition does no work, and says so.**  Forwarding to `CollisionUtil::AddAverageToFront`
// was the first thing tried and it is not available: that body is not in the port build, so the
// forwarder would only trade this undefined symbol for
// `_ZN13CollisionUtil17AddAverageToFrontERK18CCollisionInfoListRS0_` being undefined instead, which
// `tools/gate.sh`'s `link-gap` step rejects as a grown gap.  So this announces itself once and
// returns, like `fn_8012862C` below and `ReportedCameraManagerStandIn` above: **a stand-in that says
// so is safe where a plausible-looking one is not.**
extern "C" void AddAverageToFront__13CollisionUtilFRC18CCollisionInfoListR18CCollisionInfoList(
    const void*, void*) {
  static bool reported = false;
  if (!reported) {
    reported = true;
    printf("port stand-in reached: AddAverageToFront__13CollisionUtilFRC18CCollisionInfoList"
           "RS18CCollisionInfoList - Collision/CollisionUtil.cpp is not in the port build\n");
  }
}

// Retail 0x8012862C, `config/G2ME01/symbols.txt:5010`, 0x160 = 352 bytes: in
// `main/auto_03_801285DC_text` and **unclaimed by any unit**, so dtk's own object supplies the bytes
// in the DOL link.  It is the second function `Carve801285DC.c` reaches -
// `src/MetroidPrime/Carve801285DC.c` declares it because `fn_80128604`'s `bl 0x8012862C` has to
// resolve - and listing that carve in `files.cmake` put `fn_8012862C` in the port's undefined set,
// which `tools/link_check.sh --strict` fails on.
//
// **This definition does no work, and says so.**  Unlike `fn_8028139C` above, retail's body is not
// short enough to transliterate without first naming a layout nothing here has:
// `build/G2ME01/asm/auto_03_801285DC_text.s:37-133` is a two-pass filter over an array of 0x60-byte
// entries - pass one keeps a running minimum of the `+0x8` float over the entries whose `+0x34`
// word masks off 0x40000000, pass two copies every entry whose `+0x34` word masks off 0x80000000
// **or** whose `+0x8` float beats that minimum, up to 0x20 of them, and then hands the result to
// `AddAverageToFront`.  0x60 is `CCollisionInfo`'s size and 0x20 its list capacity
// (`include/Collision/CCollisionInfoList.hpp`), so the shape is readable, but the entry layout is
// a claim's job and not this file's.  So this announces itself once and returns, which is the
// contract for a stand-in: **a stub that says so is safe where a plausible-looking one is not** -
// see `ReportedCameraManagerStandIn` above for the same rule in the same file.
extern "C" void fn_8012862C(const void* list, void* out) {
  static bool reported = false;
  if (!reported) {
    reported = true;
    printf("port stand-in reached: fn_8012862C - the real body is not written\n");
  }
}

// Retail 0x80178AD0, `config/G2ME01/symbols.txt:6211`, 0x7C = 124 bytes, inside
// `MetroidPrime/CMemoryCard.cpp`'s claimed 0x801767BC..0x8017904C range.  **This is an empty-body
// stand-in and it is announced as one.**  It does not claim 0x80178AD0 is decompiled: it is not.
// `src/MetroidPrime/CMemoryCard.cpp:32` lists it among that unit's unmatched functions and says it
// has no body there, so the DOL's bytes for it come from elsewhere and this file is not in
// `configure.py`, so a definition here cannot reach `main.dol`.
//
// **What retail's bytes are** (`build/G2ME01/asm/MetroidPrime/CMemoryCard.s:2636-2669`): save
// `r30`/`r31`, build an `rstl::basic_string<char, ...>::basic_string(CInputStream&)` into an
// `0x8(r1)` frame temporary from `r31`, then three times advance `r31`'s cursor at `+0x8` by 4 and
// store the word it pointed at into `r30+0x10`, `r30+0x14`, `r30+0x18`.  **An empty body does none
// of that**, so a save-world loaded through this name silently leaves its name string empty and its
// three `uint`s at whatever was already in the object.
//
// **Why the port asks for it at all.**  `src/MetroidPrime/Carve801832E8.c` declares `__ct__Q218CWorldSaveGameInfo20SEnvironmentVariableFR12CInputStream`
// and its `bl` at 0x801832F4 is retail's own, and listing that carve in `files.cmake` puts
// `__ct__Q218CWorldSaveGameInfo20SEnvironmentVariableFR12CInputStream` in the port's undefined set, which `tools/link_check.sh --strict` and
// `tools/gate.sh`'s `link-gap` step both reject.  The other declaration, none: nothing else in the
// port build names it.  So this announces itself once and returns - the same rule as
// `fn_8012862C` above: **a stand-in that says so is safe where a plausible-looking one is not**.
// Writing the real body is not this file's job either: it would need the `CInputStream` cursor
// layout and the string constructor, and `CMemoryCard.cpp` is the unit that owns both.
extern "C" void __ct__Q218CWorldSaveGameInfo20SEnvironmentVariableFR12CInputStream(void*, void*, void*) {
  static bool reported = false;
  if (!reported) {
    reported = true;
    printf("port stand-in reached: __ct__Q218CWorldSaveGameInfo20SEnvironmentVariableFR12CInputStream - the real body is not written\n");
  }
}

// `fn_80182AF8` - retail `.text:0x80182AF8`, `symbols.txt:6417`, 0x60 = 96 bytes.  **`src/
// MetroidPrime/Carve801831B8.cpp` declares it and its `bl` at 0x801831F8 is retail's own**, and
// listing that carve in `files.cmake` puts `fn_80182AF8` in the port's undefined set, which
// `tools/link_check.sh --strict` and `tools/gate.sh`'s `link-gap` step both reject.  Nothing else in
// the port build names it, so this announces itself once and returns, exactly as `__ct__Q218CWorldSaveGameInfo20SEnvironmentVariableFR12CInputStream` above
// does for `Carve801832E8.c`'s callee.
//
// **What retail's bytes are, so the file says what it does not do** (objdump, `--start-address
// =0x80182af8 --stop-address=0x80182b58 build/G2ME01/main.elf`): `lwz r31,0(r4)` and `lwz r30,0(r3)`
// read the two one-word iterators, then `cmplw r30,r31` with `addi r30,r30,0x1c` walks 28 bytes at a
// time and each element gets `cmplwi r30,0` plus a
// `internal_dereference__Q24rstl66basic_string<c,Q24rstl14char_traits<c>,Q24rstl17rmemory_allocator>
// Fv` (0x802FE9B8) call.  **An empty body does none of that**, so a save-world loaded through this
// name leaks nothing but also tears down no `rstl::basic_string` in its
// `CWorldSaveGameInfo::SEnvironmentVariable`s.  Writing the real body is not this file's job: it
// needs that element's layout, and the range is still dtk's unclaimed `auto_03_80182830_text`.
//
// `PortGlobals.cpp` is not a unit in `configure.py`, so this definition cannot reach `main.dol`.
extern "C" void fn_80182AF8(const void*, const void*) {
  static bool reported = false;
  if (!reported) {
    reported = true;
    printf("port stand-in reached: fn_80182AF8 - the real body is not written\n");
  }
}
