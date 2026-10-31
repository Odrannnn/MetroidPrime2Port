/**
 * The pool's stand-in answers, and the retail string pool they are named after.
 *
 * **Not a `configure.py` unit**, for the reason every file in this group gives (see
 * `src/MetroidPrime/PortGlobals.cpp`'s header for the measurement: one added `static` in
 * `MetroidPrime/main.cpp` moved `__ct__CGameArchitectureSupport` 84.51% -> 81.54%). Nothing
 * here can reach `main.dol` or any of the 86 REL modules. It is listed in `files.cmake`, so the
 * port build compiles it, and mwcceppc never sees it.
 *
 * ---------------------------------------------------------------------------
 * Why this file exists: the boot stopped on a *name*, not on a missing symbol
 * ---------------------------------------------------------------------------
 *
 * `tools/boot_probe.sh` ran 103 reach-stubs deep and died here, with no undefined symbol left to
 * blame:
 *
 *   CSimplePool::GetObj(const SObjectTag&, CVParamTransfer)
 *   CSimplePool::GetObj(const char*, CVParamTransfer)
 *   CSimplePool::GetObj(const char*)
 *   CGameGlobalObjects::LoadStringTable()
 *   CMain::RsMain(int, char const* const*)
 *
 * The two statements in front of it are retail's, in order
 * (`src/MetroidPrime/main.cpp:244`):
 *
 *   stringTable = gpSimplePool->GetObj(lbl_803A56C0 + 0x146);   // 0x146 = "STRG_Main"
 *   gpStringTable = **stringTable;
 *
 * and `GetObj(const char*)` is three lines of forwarding
 * (`src/Kyoto/CSimplePoolPort.cpp`):
 *
 *   GetObj(const char* name)            -> GetObj(name, x1c_paramXfr)
 *   GetObj(const char* name, CVParamTransfer xfer)
 *                                       -> GetObj(*GetFactory().GetResourceIdByName(name), xfer)
 *
 * so the fault is `*` on a null `const SObjectTag*`: **`CResFactory::GetResourceIdByName`
 * returns null because no pak is loaded**, and retail's own code does not test the result
 * either. `AddPaksAndFactories` - the 1,936 bytes of boot-path step 13 that would fill the
 * loader - runs to completion one statement earlier, which is why the pool machinery itself
 * works and only the *name* is missing.
 *
 * **This is a data wall, and the data is a pak.** Retail resolves a name through
 * `CResFactory::GetResourceIdByName` (0x80006B80) -> `CResLoader::GetResIdByName`
 * (0x802FCC44, `Matching` in `src/Kyoto/CResLoaderGetResIdByName.cpp`) ->
 * `CPakFile::GetResIdByName` (0x803236CC, `src/Kyoto/CPakFile.cpp`). All three are written. The
 * thing that is missing is the `CPakFile` they walk, and a `CPakFile` comes out of
 * `Strings.pak` on the disc. Stubbing the *symbols* cannot supply that, which is the negative
 * result the probe's own log records: every added stub moves the fault, none of them populates
 * a pool.
 *
 * So this file does what `src/MetroidPrime/PortTweakGlobals.cpp` did for `gpTweakPlayerA`:
 * **it puts a real object at a real address for a name the game asks for by name, and it says in
 * the source that the object is a stand-in.** No frame is faked; the boot is moved past a name
 * lookup, and whatever the next name is, the next backtrace will name it.
 *
 * ---------------------------------------------------------------------------
 * Second user, 2026-09-27: `CCubeRenderer`'s eight pool names
 * ---------------------------------------------------------------------------
 *
 * The same mechanism, and the same reason. `CCubeRenderer`'s constructor (`fn_80271238`, retail
 * 0x80271238) asks the store for eight names by string literal, so it reaches this registry the
 * same way `LoadStringTable` does - through `CSimplePool::GetObj(const char*)`, which asks here
 * before it asks the factory. **The entries and the reasoning are all further down, under
 * `kEntries`; this paragraph is here so a reader arriving at the top of the file knows the second
 * user exists.** The measurement, the three faults and the marker that replaced them are in
 * `docs/HANDOFF.md`, "The pool answers all eight of `CCubeRenderer`'s token names".
 *
 * ---------------------------------------------------------------------------
 * What it bought, measured
 * ---------------------------------------------------------------------------
 *
 * `tools/boot_probe.sh`, same machine, same disc, same hardware Vulkan ICD:
 *
 *   before  103 reach-stubs, `[port] caught SIGSEGV` with the backtrace above.
 *   after   114 reach-stubs, **no SIGSEGV and no backtrace at all.** The ladder in
 *           `src/MetroidPrime/PortBoot.cpp` now runs to the end of what it has:
 *
 *             Initializing renderer...                <- PostInitialize's own printf, real text
 *             boot: step 12 returned
 *             boot: step 12 - gpRender is non-null for the first time
 *             boot: step 17 - new CGameArchitectureSupport(*mOsContext)
 *             boot: step 17 returned - the constructor completed
 *             boot: step 18/18 returned, 19/19 returned, 20/20 returned
 *
 *           The eleven stubs that are new are 0104-0108 (`CCallStack`, from the renderer's
 *           allocation) and 0109-0114: `CEnvFxManager::Initialize`, `fn_8033CEE8`,
 *           `CMain::ResetGameState`, `CConsoleOutputWindow::CConsoleOutputWindow`,
 *           `CAudioStateWin::CAudioStateWin`, `mp_cswarmbasics_exit`. **Three of those are on
 *           the project's named port-blocking list** - `ResetGameState` and
 *           `CConsoleOutputWindow` are two of them - so this made them *reached*, which they
 *           were not before.
 *
 *   **No frame was rendered and none is claimed.** `gpRender` being non-null is not a renderer:
 *           `AllocateRenderer` (0x8026EF54) returns a pointer to an object whose constructor
 *           `fn_80271238` (0x80271238, 0x59C = 1436 bytes) is still a stub - the run log shows
 *           `[auto-stub] fn_80271238` and `[auto-stub] fn_80272958` - and the ladder's
 *           `CMain::RsMain` has no loop in it. The next wall is those two functions, then the
 *           frame loop. `src/MetroidPrime/PortBoot.cpp:240` already says the same about
 *           `fn_80271238`, and this is now the measured state rather than the predicted one.
 *
 * ---------------------------------------------------------------------------
 * `lbl_803A56C0` - retail's own 0x1C0 bytes, transcribed
 * ---------------------------------------------------------------------------
 *
 * `.rodata:0x803A56C0`, `size:0x1C0` in `config/G2ME01/symbols.txt`, and **it is not a stand-in**:
 * every byte below is retail's, read out of `build/G2ME01/main.elf` with
 * `powerpc-eabi-objdump -s -j .rodata --start-address=0x803A56C0 --stop-address=0x803A5880`. It is
 * here rather than in `src/MetroidPrime/PortGlobals.cpp` because the *reason* it has to exist is
 * the name table immediately below: `LoadStringTable` asks for `lbl_803A56C0 + 0x146`, and
 * without the object at that address the pointer is 0x146 bytes past nothing.
 *
 * Until now the symbol was undefined in the port and `tools/boot_probe.sh`'s self-heal stubbed
 * it **as a function** (`extern "C" void lbl_803A56C0(void)`), which linked and made every
 * `lbl_803A56C0 + N` read N bytes past a zero-filled function body. That is why this definition
 * is a data object, and why the stale auto-stub was deleted from
 * `src/MetroidPrime/PortReachStubs.cpp` by hand - which is the fix `tools/boot_probe.sh`'s own
 * duplicate-definition branch prescribes ("delete the stale alias from PortReachStubs.cpp, not
 * the real definition").
 *
 * The offsets the tree actually uses, all re-checked against the bytes below, so a reader does
 * not have to re-derive them:
 *
 *   +0x070  "Strings.pak"               AddPaksAndFactories' probe  (main.cpp:473)
 *   +0x07C  "sound_lookup_ATBL"         CMain::FillInAssetIDs       (main.cpp:582)
 *   +0x0B0  "aram:Strings"              AddPaksAndFactories        (main.cpp:474)
 *   +0x0D6  "NoARAM"                    AddPaksAndFactories        (main.cpp:482)
 *   +0x0E6  "AudioGrp"                  AddPaksAndFactories        (main.cpp:483)
 *   +0x0F4  "aram:MiscData"             AddPaksAndFactories        (main.cpp:484)
 *   +0x104  "aram:TestAnim"             AddPaksAndFactories        (main.cpp:485)
 *   +0x110  "aram:MidiData"             AddPaksAndFactories        (main.cpp:486)
 *   +0x11D  "FrontEnd.pak"              AddPaksAndFactories        (main.cpp:491)
 *   +0x128  "FrontEnd"                  AddPaksAndFactories        (main.cpp:492)
 *   +0x133  "SKIP4INFINITE LOOP"        InfiniteLoopAlarm          (main.cpp:252)
 *   +0x146  "STRG_Main"                 **LoadStringTable**        (main.cpp:245)
 *   +0x150  "Initializing renderer..."  PostInitialize             (main.cpp:238)
 *   +0x16A  "Stack usage: %d bytes (%dk)"                    CMainShutdownSubsystems
 *   +0x187  "Protecting stack... "      CMain::InitializeSubsystems (mainTail.cpp:221)
 *   +0x19D  "Stack: 0x%8.8x down to 0x%8.8x"                 same
 */
#include "Kyoto/Graphics/CModel.hpp"
#include "Kyoto/Graphics/CTexture.hpp"
#include "Kyoto/IObj.hpp"
#include "Kyoto/SObjectTag.hpp"
#include "Kyoto/Text/CStringTable.hpp"

#include "rstl/auto_ptr.hpp"
#include "rstl/rmemory_allocator.hpp"

#include <string.h>

// `lbl_803AAC38` is retail's `.rodata:0x803AAC38`, `size:0x7` in `config/G2ME01/symbols.txt`, and
// `build/G2ME01/obj/auto_06_803AAC38_rodata.o` holds it - an unclaimed rodata range, so no unit of
// ours owns it and nothing in the DOL defines it for the port build. The only reference is
// `CPlayerGunBase::CreateGunLight` (retail 0x801DDF88), which asks for `+7`: the byte *after*
// retail's own string, i.e. the gun light's **empty** name.
//
// **Both NULs are load-bearing, exactly as in `lbl_803A56C0` below.** Retail's seven bytes are
// `3f 3f 28 3f 3f 29 00` - "??(??)" and one NUL (`powerpc-eabi-objdump -s -j .rodata` on
// `build/G2ME01/obj/auto_06_803AAC38_rodata.o` reads `3f3f283f 3f290000`), and `+7` lands on the
// next byte, which in retail is the following zero. `"??(??)\0"` alone is 7 bytes and `+7` would
// read one past the object; the trailing `\0` is the byte the pointer actually names.
extern "C" const char lbl_803AAC38[] = "??(??)\0\0";

extern "C" const char lbl_803A56C0[] =
    // **The leading `\0\0` is not decoration.** Retail's first bytes are `3f 3f 28 3f 3f 29
    // 00 00` at 0x803A56C0 - "??(??)" and *two* NULs - and dropping one moves every offset in
    // this object down by one, which is how "STRG_Main" lands at +0x145 instead of +0x146 and
    // the pool is asked for the name "TRG_Main". This lane made that mistake and fixed it; it
    // is written down so the next person does not make it again.
    "??(??)\0\0"
    "%d\0"
    ".pak\0"
    "AudioTweaks\0"
    "Loaded audio tweaks from memory card\n\0"
    "FAILED to load audio tweaks from memory card\n\0"
    "Strings.pak\0"
    // +0x07C. The one name the pool is asked for *after* "STRG_Main", by
    // `CMain::FillInAssetIDs` - see the tag table below for why its *object* is not stood in.
    "sound_lookup_ATBL\0"
    "Reset failed: Tried %d\0"
    "Wrote: %d\n\0"
    "aram:Strings\0"
    "Standard.NTWK\0"
    "Tweaks.rel\0"
    "NoARAM\0"
    "AudioGrp\0"
    "aram:MiscData\0"
    "aram:TestAnim\0"
    "aram:MidiData\0"
    "aram:GGuiSys\0"
    "FrontEnd.pak\0"
    "FrontEnd\0"
    "SKIP4INFINITE LOOP\0"
    // +0x146. **The name `CGameGlobalObjects::LoadStringTable` asks the object pool for**, and
    // the reason the boot was stopped. It is the English string table, out of `Strings.pak`.
    "STRG_Main\0"
    "Initializing renderer...\n\0"
    "Stack usage: %d bytes (%dk)\n\0"
    "Protecting stack... \0"
    "Stack: 0x%8.8x down to 0x%8.8x\n\0\0\0\0";
    // The four trailing NULs are real bytes too: the object is 0x1C0 long and the last string
    // ends 4 bytes short of that. `CHECK_SIZEOF`-style length assertions on rodata blobs are
    // not a thing in this tree, so this comment is the assertion.

// The two functions this file defines are declared in `Kyoto/CSimplePool.hpp`, next to the class
// whose two name-taking `GetObj` overloads are the only callers. The header is a decompilation
// header (`src/Kyoto/CSimplePoolCtor.cpp` is a `Matching` unit that includes it), so the block it
// carries is declarations only - no new `#include`, no new member, nothing that can move a byte
// of the DOL.
namespace port {
namespace pool {

namespace {

/**
 * What a registered name can be answered with, which is a third thing and is named rather than
 * inferred from a bool: `kSIK_None` means "the registry knows the tag and cannot build the
 * object", and the other three mean "here is a real object of this class". **Class, not
 * `IObj`** - `TLockedToken<T>::GetT()` hands the caller a `T*`, so a name that a `TLockedToken`
 * asks for has to be answered with an object of the class that token names, or the constructor
 * stores a pointer to the wrong type and the *next* thing to touch it faults one frame later
 * with no name in the backtrace.
 */
enum EStandInKind {
  kSIK_None = 0,
  kSIK_StringTable,
  kSIK_Texture,
  kSIK_Model,
  // **A resource whose body is a byte stream and not an `IObj`, so the pool cannot carry it.**
  // `kSIK_Texture` and `kSIK_Model` exist because a `TLockedToken<T>::GetT()` wants a `T*`, and
  // the two classes the previous eight names wanted are those. `"DUMB_SnowForces"` wants neither:
  // see its entry below, which says what it actually wants and why no `EStandInKind` can answer
  // it. The kind is named rather than folded into `kSIK_None` so that the file says *why* there
  // is no object, and so a reader does not read the absence as an oversight.
  kSIK_Stream,
};

/**
 * One registered name, and what the pool may say about it.
 *
 * Three facts per name, deliberately, and nothing that could be silently wrong: a name, the tag
 * retail's own factory table would answer with, and what class of object can be produced for it.
 * "What tag is this name?" and "what object stands in for this tag?" are **different facts**,
 * and one of the entries below answers only the first. Conflating them would put a
 * fabrication in a place the boot cannot detect, which is the one thing a stand-in must not be.
 */
struct SEntry {
  const char* x0_name;
  SObjectTag x4_tag;
  EStandInKind x8_kind;
};

// ---------------------------------------------------------------------------
// The stand-in `CAssetId` range, and why the ids are not `kInvalidAssetId`
// ---------------------------------------------------------------------------
//
// **This is the registry's own key space and it is not retail's.** A resource's real
// `CAssetId` is a row in whichever pak's resource table the object came out of, and no pak is
// on this machine (`docs/HANDOFF.md`, "PROVEN, and it is the answer to 'what would unblock a
// frame'"), so there is no real value to put here. Two things follow, and both are load-bearing:
//
// 1. **`kInvalidAssetId` for every entry would be a bug, not caution.** The pool's map is
//    `hash_map< SObjectTag, CObjectReference* >` keyed on `(type, id)`
//    (`include/Kyoto/CSimplePool.hpp`), and `CSimplePoolPort.cpp`'s `TagsEqual` compares both.
//    Four `CMDL_*` names sharing one id would be **one** map entry, so
//    `GetObj("CMDL_FlatCylinder")` would hand back the object already standing in for
//    `CMDL_FlatSphere` - two different names, one object, silently.
// 2. **These ids must not be mistaken for real ones later.** They start at `0xF0000000`,
//    where a pak's sequential ids never go, so a future entry carrying a *real* id cannot
//    collide with one of these by accident. If a real `Strings.pak` ever lands, every entry
//    below is replaced and this paragraph goes with it.
const CAssetId kStandInIdBase = 0xF0000000u;

// **The eleven entries below, in the order the boot asks for them.** The FourCCs are retail's own -
// `STRG` is the one of `AddPaksAndFactories`' 36 registrations the DOL's symbol table names,
// `main.cpp:514` registers `FStringTableFactory` under it, and the `TXTR`/`CMDL` pair is what
// `fn_80271238`'s own `GetObj` arguments resolve to out of `.rodata` (see the "eight pool names"
// section of `src/MetaRender/Carve80271238.cpp`, which is where the addresses were read). The
// ids are the registry's own, and the comment above them says so.
const SEntry kEntries[] = {
    // "STRG_Main", the English string table, in `Strings.pak`.
    //
    // `STRG` is retail's own type FourCC - it is the one of `AddPaksAndFactories`' 36
    // registrations the DOL's symbol table names, and `main.cpp:514` registers
    // `FStringTableFactory` under it, so this is the tag retail's own factory table answers with.
    //
    // **The object is a STAND-IN: a zeroed `CStringTable`, not the table out of the pak.** What
    // is missing is the English text `Strings.pak` holds; what a PC build has is the header. A
    // zeroed `CStringTable` is observably an *empty* table and provably a safe one:
    // `GetStringCount()` is 0, `GetStringIndexByName()` returns -1 without touching `xc_names` (its
    // `x4_nameCount` is 0), and both `GetString` overloads therefore return `skInvalidString`
    // (`src/Kyoto/Text/CStringTable.cpp:102`). Every string in the game will read "Invalid",
    // which is what the class itself answers when an index is out of range - so the failure mode
    // is a visible, correct-for-the-class answer rather than a crash.
    //
    // It is a raw zeroed block and not `new CStringTable(...)` for the reason
    // `PortTweakGlobals.cpp` gives: the class's only constructor is `CStringTable(CInputStream&)`,
    // so constructing one means handing `Load()` a stream, and `Load()` reads `languages[0]`
    // **before** it has checked that `langCount` is non-zero (`CStringTable.cpp:59`) - an empty
    // language table would index an empty `rstl::reserved_vector`. There is no stream on a PC
    // that carries the pak's bytes, so there is nothing honest to feed it.
    //
    // What would make this real, in order: `CResLoader::AddPakFileAsync` reaching a real
    // `Strings.pak` on the disc (boot-path step 13, `CGameGlobalObjects::AddPaksAndFactories`,
    // 1,936 bytes of retail with an empty body in this tree), and then `FStringTableFactory`
    // building the table off the pak's `CInputStream` with no stand-in anywhere. The factory is
    // retail's and it is written - `src/Kyoto/Text/CStringTable.cpp:130` - but that **file is
    // excluded from `files.cmake`** (it casts a pointer to `uint` at lines 92 and 98, which loses
    // precision on a 64-bit host), so the port build's `FStringTableFactory` is the
    // `return rs_new ...`-free body in `src/Kyoto/CFactoryFunctionsPort.cpp:42`. Listing
    // `CStringTable.cpp` is a prerequisite, and the `uint` casts are the blocker.
    { "STRG_Main", SObjectTag('STRG', kStandInIdBase + 0), kSIK_StringTable },

    // "sound_lookup_ATBL", the audio string table, in `Strings.pak`. **Tag only: the object is
    // NOT stood in, and here is why.**
    //
    // `CMain::FillInAssetIDs` (retail 0x80006B38, `src/MetroidPrime/main.cpp:581`) asks for it
    // by name and hands the tag straight to `gpSimplePool->fn_8029c7e8`. Answering the *name*
    // is therefore worth doing - it turns "the pool has never heard of this" into "the pool
    // knows the tag and cannot build the object", and the second is a real answer.
    //
    // The object is not stood in for three measured reasons. (1) The class is not identified:
    // `AddPaksAndFactories` registers `ATBL` with `fn_8029AB80` (`main.cpp:536`), and
    // `config/G2ME01/symbols.txt` does not name it, so there is no type to allocate. (2) The
    // consumer is `fn_8029c7e8`, which `CSimplePoolPort.cpp`'s own header records is **not a
    // pool method** - at 0x8029C7E8 it takes the store as an argument and calls `GetObj(tag)`
    // through vtable slot 0xC, in the audio code. **Superseded 2026-09-27**: that function has
    // a body in `CSimplePoolPort.cpp` now (retail's `CSfxManager::LoadTranslationTable`,
    // identified from its own instructions), and the body keeps the token over a null object
    // instead of faulting - so what is still missing here is the table's *bytes*, not a call the
    // port cannot make. (3) `CMain::FillInAssetIDs`
    // **is not on the host boot ladder**: `CMain::RsMain` in `src/MetroidPrime/PortBoot.cpp` runs
    // steps 12, 17, 18, 19 and 20 and then stops with a message; it never calls step 16. So a
    // stand-in object here would be an allocation nothing ever reads.
    //
    // There is a second, separate gap on that call, in a file this lane does not own: it reads
    // the tag with `gpResourceFactory->GetResourceIdByName(...)` **directly**, not through the
    // pool, and `CResFactory::GetResourceIdByName` is a `return nullptr;` in
    // `src/Kyoto/CResFactoryPortVirtuals.cpp:86`. The one-line fix, when `FillInAssetIDs` is
    // wired up, is for that body to return `port::pool::FindStandInTag(name)` before falling
    // through to the loader's own two-list walk.
    { "sound_lookup_ATBL", SObjectTag('ATBL', kStandInIdBase + 1), kSIK_None },

    // -------------------------------------------------------------------------
    // The eight names `CCubeRenderer`'s constructor asks for, retail 0x80271238
    // -------------------------------------------------------------------------
    //
    // `src/MetaRender/Carve80271238.cpp` reads them out of `.rodata` at
    // `0x803AE3BC + {93, 106, 126, 144, 160, 179, 197, 218}`; that file's header has the
    // measurement and the `tools/dol_read.py` `.rodata` offset fix it needed to read them.
    //
    // **These eight are the boot's next wall, and it is a null dereference, not a missing
    // symbol.** Seven of them are members - `x4fc_bigRing`, `x508_darkWorldCloud`,
    // `x514_scanSweepBar`, `x520_flatSphere`, `x52c_flatSphereLow`, `x538_flatCylinder`,
    // `x544_flatCylinderLow` - and the eighth is a stack `TLockedToken<CTexture>` handed to
    // `fn_802711A4`, which builds a `CGraphicsPalette` from it. Every one of them is
    // `store.GetObj(name)` then `CToken::GetObj()`, and **`CToken::GetObj()` dereferences
    // `x0_objRef` with no null test** (`src/Kyoto/CToken.cpp:52`, which the port inherited
    // from retail). With no pak loaded there is no object behind the token, so on retail this
    // constructor would fault too - and it does. **Nothing here is a workaround for a
    // decompilation defect: the correct fix is upstream, and it is to give the pool something
    // real for the name.** `GetObj()` is deliberately left alone, and so is every null test.
    //
    // What the stand-in is, per class, and what is missing behind it:
    //
    //  * **`kSIK_Texture` - a real `CTexture`, constructed with `kTF_Invalid`, 0x0 and zero
    //    mip levels.** A `CTexture` with no bitmap behind it: the pixels are in the pak, and
    //    the object is the class, correctly constructed, with the format that says "there is
    //    nothing here". `CTexture(ETexelFormat, short, short, int)` has a host body
    //    (`src/Kyoto/Graphics/CTexturePortStub.cpp`, retail's own `0x802C6000` still
    //    unclaimed), so this is a constructed object and not a zeroed block pretending to be
    //    one - which matters, because the header's bitfields (`mCanLoadObj`, `mIsPowerOfTwo`,
    //    ...) would otherwise be lies. `GetBitMapData(0)` answers null, so a draw through one
    //    of these gets no texture and **renders nothing**. That is the honest answer and it is
    //    why this is a stand-in, not a frame.
    //  * **`kSIK_Model` - a real `CModel`, default-constructed.** `include/Kyoto/Graphics/
    //    CModel.hpp` is a measured, partial model (0x30 bytes: pads, `x1c_numParts` and
    //    `x28_touchTarget`) and has no constructor in the tree, so the implicit one is what
    //    runs. The model's vertex data, skeleton and part list are in `CMDL_*.pak`; there is
    //    none, so `x1c_numParts` is 0 and anything that iterates parts does nothing rather
    //    than reading a bad address. The same rule as above: correct for the class, empty of
    //    the game's data, and named as a stand-in here.
    //
    // What would retire all eight, in one step and in this order: `AddPaksAndFactories`
    // (boot step 13) registering the factories under `TXTR` and `CMDL` so that
    // `CResLoader::GetResIdByName` walks a real `CPakFile`, and a real pak on the disc. Both
    // halves are named in `docs/research/paks.md`; neither is a pool change, which is why
    // this registry is written so that deleting these eight lines is the whole of the
    // retirement.
    { "TXTR_BigRing", SObjectTag('TXTR', kStandInIdBase + 2), kSIK_Texture },
    { "TXTR_DarkWorldCloud", SObjectTag('TXTR', kStandInIdBase + 3), kSIK_Texture },
    { "TXTR_ScanSweepBar", SObjectTag('TXTR', kStandInIdBase + 4), kSIK_Texture },
    { "CMDL_FlatSphere", SObjectTag('CMDL', kStandInIdBase + 5), kSIK_Model },
    { "CMDL_FlatSphereLow", SObjectTag('CMDL', kStandInIdBase + 6), kSIK_Model },
    { "CMDL_FlatCylinder", SObjectTag('CMDL', kStandInIdBase + 7), kSIK_Model },
    { "CMDL_FlatCylinderLow", SObjectTag('CMDL', kStandInIdBase + 8), kSIK_Model },
    // The eighth, and the one that does not become a member: `x550_darkLightworldPalette` is a
    // `rstl::single_ptr<CGraphicsPalette>` built by `fn_802711A4` from a stack token. **The
    // token still has to resolve**, so the name is registered with the same `CTexture`
    // stand-in; what is missing is the palette, and `fn_802711A4` is retail code nobody has
    // written (0x802711A4, unclaimed). Registering the name is what lets the *constructor*
    // complete, and it is not a claim that the palette is there.
    { "TXTR_DarkLightworldPalette", SObjectTag('TXTR', kStandInIdBase + 9), kSIK_Texture },

    // -------------------------------------------------------------------------
    // "DUMB_SnowForces" - and it is NOT a texture, and NOT a pool resource at all
    // -------------------------------------------------------------------------
    //
    // `CEnvFxManager::Initialize` (retail 0x80166880, `MetroidPrime/CEnvFxManagerInitialize.cpp`,
    // a `Matching` unit) is the fourth statement of `CGameGlobalObjects::PostInitialize`, and its
    // whole body is:
    //
    //     const SObjectTag* tag = gpResourceFactory->GetResourceIdByName(lbl_803A96FC);
    //     rstl::auto_ptr<CInputStream> stream(fn_802FC63C(&GetResLoader(), *tag, nullptr));
    //     for (i < 256) for (j < 2) lbl_803DABE0[i][j] = stream->ReadFloat();
    //
    // **Three measurements, and together they say this is not a pool resource:**
    //
    //  1. **The name never reaches the pool.** It goes to `CResFactory::GetResourceIdByName`,
    //     which is a *forwarder* - `addi r3,r3,4` / `b fn_802FCC44` (`CResLoader::GetResIdByName`,
    //     0x802FCC44, `Matching`) - and **not** through `CSimplePool::GetObj(const char*)`. The
    //     registry is consulted by `CSimplePoolPort.cpp` only, so **an entry here does not by
    //     itself make this name resolve**; `CResFactory::GetResourceIdByName` had to be pointed
    //     at `FindStandInTag` as well, and now is (see that file). A previous lane's "one more
    //     registry row" was half of this.
    //  2. **What it wants is a `CInputStream`, not an `IObj`.** `fn_802FC63C` is
    //     `CResLoader::LoadNewResourceSync` (retail 0x802FC63C, 0x1E0 bytes) and its return type
    //     is `CInputStream*`. No `TLockedToken` is involved, so `CreateStandInObject` - whose
    //     whole contract is "an object a token can hand back" - has nothing to answer. That is
    //     what `kSIK_Stream` names.
    //  3. **The type is `DUMB`, measured, not guessed.** The name is `DUMB_SnowForces`, and MP2
    //     resource names are `FourCC_Name` - the five other strings in the same 0x94-byte pool
    //     `lbl_803A96FC` (`TXTR_EnvGradient`, `PART_EnvRainSplash`, `TXTR_SnowFlake`,
    //     `TXTR_UnderwaterFlake`, `TXTR_DarkworldParticleTexture`) all follow it. Independently,
    //     `DUMB` is retail's **own registered type FourCC**: it is the last token of the type
    //     table at `.rodata:0x803AF9D8` (read out of `orig/G2ME01/sys/main.dol` with
    //     `tools/dol_read.py`; the table runs `CLSN CSPP CMDL CSKR ANIM CINF TXTR PLTT FONT
    //     ANCS ANMS MADF MLVL REAM APWM APAW SAVW SAVA PART WPSC WHCD PSCS ELSC CRSC CSPS CSRS
    //     CAFS MDCL NAGS CATB LCSN GSTR GSCAN PATH DGRP HMAP PTLA STLC EGMC RULE FSM2 CTWK FRME
    //     HINT MAPU DUMB`), the same table `TXTR` and `PART` come from. So `SObjectTag('DUMB',…)`
    //     is retail's own answer for this name, not a stand-in invented to fill a gap.
    //
    // **Status (A8-24):** the paks now load, so `CResLoader::GetResourceIdByName` answers with the
    // real tag and `CEnvFxManager::Initialize` (TARGET_PC arm) reads the 256 big-endian float pairs
    // from the disc as retail does. This row is only the fallback `CResFactory::GetResourceIdByName`
    // returns when no pak holds the name; its id (`0xF000000A`) matches no pak row, so
    // `LoadNewResourceSync` would hand back a stream of zeros and the table stays zero.
    { "DUMB_SnowForces", SObjectTag('DUMB', kStandInIdBase + 10), kSIK_Stream },
};

const int kEntriesCount = static_cast< int >(sizeof(kEntries) / sizeof(kEntries[0]));

/**
 * The stand-in object for one entry: the `IObj` wrapper the pool stores, plus the zeroed
 * `CStringTable` it points at, owned together so dropping the last reference frees both.
 *
 * **Why a wrapper at all.** `CToken::GetObj()` returns the `CObjectReference`'s `IObj*`, and
 * `TToken<T>::GetT()` then calls `GetContents()` on it (`include/Kyoto/IObj.hpp:25`,
 * `include/Kyoto/TToken.hpp:20`). What has to be in the reference is therefore a wrapper whose
 * `m_objPtr` is the `CStringTable`, and not the table itself - that is what makes
 * `TLockedToken<CStringTable>`'s `x8_item(*x0_token)` land on the right address.
 *
 * **Why it derives from `CObjOwnerDerivedFromIObjUntyped` and not from retail's
 * `TObjOwnerDerivedFromIObj<CStringTable>`.** Retail's derived template owns its contents -
 * `~TObjOwnerDerivedFromIObj` does `delete Owned()` - and that needs
 * **`CStringTable::~CStringTable()`**, which the port build does not have: the body lives in
 * `src/Kyoto/Text/CStringTable.cpp`, and that file is **excluded from `files.cmake`** because it
 * casts a pointer to `uint` at lines 92 and 98 and loses precision on a 64-bit host. Using
 * retail's wrapper does not link, which was measured rather than guessed - the first version of
 * this file used it and the probe reported the missing destructor by name.
 *
 * **And the `~COwnedStringTable` below is what stops that from being a leak.** The untyped
 * wrapper alone would free only itself and leave `sizeof(CStringTable)` bytes behind every time
 * a reference is dropped; retail's derived wrapper would have freed both, had it compiled. So the
 * derived class owns the block directly. `rstl::rmemory_allocator::deallocate` is the exact
 * counterpart of its `allocate` - `deallocate(T*)` is `delete[] reinterpret_cast<uchar*>(T*)`
 * and `allocate(int)` is `size == 0 ? nullptr : rs_new uchar[size]` (`include/rstl/
 * rmemory_allocator.hpp:102,145`, whose out-of-line body is `src/rstl/rstl_misc.cpp`) - so this
 * is a matched pair and not a `free` of somebody else's `malloc`.
 */
class COwnedStringTable : public CObjOwnerDerivedFromIObjUntyped {
public:
  COwnedStringTable() : CObjOwnerDerivedFromIObjUntyped(AllocateTable()) {}
  ~COwnedStringTable() { rstl::rmemory_allocator::deallocate(m_objPtr); }

private:
  // Raw and zeroed, for the reason on the "STRG_Main" entry above. `rmemory_allocator` rather
  // than `calloc` so the block comes out of the game's own heap, like `PortTweakGlobals.cpp`'s
  // tweak player, and `memset` because there is no constructor to run.
  static CStringTable* AllocateTable() {
    void* const block = rstl::rmemory_allocator::allocate(static_cast< int >(sizeof(CStringTable)));
    memset(block, 0, sizeof(CStringTable));
    return static_cast< CStringTable* >(block);
  }
};

} // namespace

const SObjectTag* FindStandInTag(const char* name) {
  for (int i = 0; i < kEntriesCount; ++i) {
    if (strcmp(kEntries[i].x0_name, name) == 0) {
      // A stable address: `GetResourceIdByName`'s contract is a pointer the caller dereferences
      // and keeps, and this table is `const` storage, so `&kEntries[i].x4_tag` is one fixed
      // address per name for the life of the program.
      return &kEntries[i].x4_tag;
    }
  }
  return nullptr;
}

// **The lookup matches the WHOLE tag - `type` *and* `id` - and that is a fix, not a style.** With
// one `TXTR` entry it could not tell the difference; with four `TXTR_` names and four `CMDL_`
// names, matching on `type` alone hands every texture the object built for the first texture
// name, and `CSimplePool`'s map would agree (its `TagsEqual` compares both words). The
// standalone id each entry carries is what makes the registry a registry: eight names, eight
// tags, eight objects, one `CObjectReference` each - which is the contract
// `include/Kyoto/CSimplePool.hpp` states and `CObjectReference::RemoveReference` relies on.
IObj* CreateStandInObject(const SObjectTag& tag) {
  for (int i = 0; i < kEntriesCount; ++i) {
    if (kEntries[i].x4_tag.type != tag.type || kEntries[i].x4_tag.id != tag.id) {
      continue;
    }
    switch (kEntries[i].x8_kind) {
    case kSIK_StringTable:
      return new COwnedStringTable();
    // **These two are retail's own derived wrapper, `TObjOwnerDerivedFromIObj<T>`, and not
    // another hand-rolled `COwned*`.** That wrapper `delete`s what it owns, and unlike
    // `CStringTable` both of these classes *can* be deleted on a host: `~CTexture` has a body
    // (`src/Kyoto/Graphics/CTexturePortStub.cpp`, an empty one) and so has `~CModel`
    // (`src/Kyoto/Graphics/CModelPortStub.cpp`). So
    // the object is a constructed, destructible `T` and retail's own ownership wrapper is both
    // shorter and more honest than the string table's arrangement above, which exists only
    // because its class has no reachable destructor. `GetNewDerivedObject` returns an
    // `rstl::auto_ptr` whose `release()` hands the pointer over; the temporary's destructor
    // then does nothing, because `release()` cleared its ownership flag.
    case kSIK_Texture:
      // `kTF_Invalid`, 0x0, 0x0, 0 mips: a texture with no bitmap. The comment on the
      // `TXTR_*` entries above says what is missing and why this is not a frame.
      return TObjOwnerDerivedFromIObj< CTexture >::GetNewDerivedObject(
                 new CTexture(kTF_Invalid, 0, 0, 0))
          .release();
    case kSIK_Model:
      return TObjOwnerDerivedFromIObj< CModel >::GetNewDerivedObject(new CModel()).release();
    case kSIK_Stream:
      // **Deliberately null, and this is the correct answer rather than a gap.** An entry of this
      // kind is reached through `CResFactory::GetResourceIdByName` - the loader's own name
      // lookup - and its consumer asks `CResLoader::LoadNewResourceSync` for a `CInputStream*`.
      // Nothing ever calls `CToken::GetObj()` on it, so there is no `IObj` to build; building a
      // `CTexture` here because "the last eight were textures" would be a class the caller never
      // asked for, allocated for nothing, and it is exactly the mistake the `EStandInKind` block
      // warns about. The entry's comment above says what the consumer really wants.
      return nullptr;
    default:
      // `kSIK_None`: the name resolves, the object does not. `GetObj(tag, xfer)` then builds a
      // `CObjectReference` with a null `x18_object`, and the *first* `CToken::GetObj()` on it
      // faults - which is the honest answer for a resource the port cannot build, and the same
      // place retail would be standing with no pak behind the name.
      return nullptr;
    }
  }
  return nullptr;
}

} // namespace pool
} // namespace port
