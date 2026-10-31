/**
 * Port-only: `__nw__FUlPCcPCc`, mwcceppc's mangling of `operator new(unsigned long, const char*,
 * const char*)`, retail 0x802CE278, 0x54 (the body is `src/Kyoto/Alloc/CMemory.cpp`'s).
 *
 * Units written against retail's bytes call it by that mangled name through `extern "C"`, which is
 * what reproduces their call sites under mwcceppc: `CMainFlowDtor.cpp` (`CMainFlow::SetGameState`,
 * reached on the first frame), `Player/CGameStateCtor.cpp` and `ScriptObjects/CScriptSkyRipple.cpp`.
 * On a host link that name binds to nothing, so this defines it.
 *
 * It forwards to the **host** `operator new`, not CMemory's: on `TARGET_PC` the host runtime owns
 * global new/delete (`CMemory.hpp` declares the CMemory-backed operators for `__MWERKS__` only, and
 * `rs_new` is plain `new`), so a `delete` of anything outside the game heap frees through the host
 * heap (see `PortDelete` below) and an object allocated here has to come from it too. The file-and-line and type strings are retail's
 * allocation-tracking tags and have no host-side consumer. Not in configure.py, so it cannot affect
 * main.dol.
 */
#include <cstdlib>
#include <cstring>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif

#include "Kyoto/Alloc/CMemory.hpp"
#include "Kyoto/CToken.hpp"
#include "Kyoto/Text/CFontImageDef.hpp"
#include "Kyoto/Text/CTextRenderBuffer.hpp"
#include "MetroidPrime/CActor.hpp"
#include "MetroidPrime/CActorModelParticles.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"

extern "C" void* __nw__FUlPCcPCc(unsigned long size, const char* /*fileAndLine*/,
                                 const char* /*type*/) {
  return ::operator new(size);
}

// Retail's own mangled name for the free itself, which `Matching` units reproduce by calling it
// through `extern "C"` - the same mechanism as `__nw__FUlPCcPCc` above.
// `src/WorldFormat/Carve80255A0C.c` (retail 0x80255A0C..0x80255B28) is the unit that needs it:
// its three free steps are `__sys_free`-shaped, `rstl::destroy_impl`-shaped and the
// deleting-destructor convention, and the last two reach `CMemory::Free` by that name. It is
// unclaimed by any unit (`symbols.txt:12992`, 0x802CE388, 0x64 bytes), so `dtk`'s own object
// supplies the bytes in the DOL link and this is only what a host link binds to.
extern "C" void Free__7CMemoryFPCv(const void* ptr) { CMemory::Free(ptr); }

// Retail's own mangled name for `CToken::~CToken()`, reached the same way.  `src/MetroidPrime/
// Carve80024B70.c` (retail 0x80024B70..0x80024C18) is the unit that needs it: that is
// `rstl::vector<TToken<CCharLayoutInfo>>::~vector()`, whose element loop calls
// `bl __dt__6CTokenFv` with `li r4,0` once per token.  Like `Free__7CMemoryFPCv` above, retail
// defines it (`symbols.txt:13922`, 0x8030154C, 0x68 bytes, claimed by `Kyoto/CToken.cpp`), so the
// DOL link resolves it and this is only what a host link binds to.  The 16-bit second argument is
// MWCC's deleting-destructor flag, which the host's C++ destructor does not take; the flag is
// always 0 from this call site, so it is accepted and ignored and the work is `CToken`'s own
// destructor above.
extern "C" void __dt__6CTokenFv(void* self, short) {
  static_cast< CToken* >(self)->~CToken();
}

// Retail's own mangled name for `CToken::CToken(const CToken&)`, reached the same way, and the
// copy side of the pair above.  `src/MetroidPrime/Carve80046C0C.c` (retail
// 0x80046C0C..0x80046D44) is the unit that needs it: that is
// `rstl::uninitialized_copy<rstl::pointer_iterator<TCachedToken<CStringTable>,...>,
// TCachedToken<CStringTable>*>` whose per-element body is `include/rstl/construct.hpp:53-56`'s
// placement-new copy, and retail's bytes are a `bl __ct__6CTokenFRC6CToken` with the argument
// registers copied straight through, so the call cannot be dropped without losing the match.
// Retail defines it (`symbols.txt:13923`, 0x803015B4, 0x5C bytes, claimed by `Kyoto/CToken.cpp`),
// so the DOL link resolves it and this is only what a host link binds to.  Nothing here is a stub:
// the body is `CToken`'s own copy constructor, which on `TARGET_PC` the host compiler would
// otherwise inline into the caller under `_ZN6CTokenC1ERKS_` and never export under the name the
// carve's `bl` asks for.
extern "C" void __ct__6CTokenFRC6CToken(void* self, const void* src) {
  new (self) CToken(*static_cast< const CToken* >(src));
}

// Retail's own mangled name for `CFontImageDef::~CFontImageDef()`, reached the same way.
// `src/MetroidPrime/Carve80024D24.c` (retail 0x80024D24..0x80024D68) is the unit that needs it:
// that is `rstl::destroy_impl< CFontImageDef >`, whose whole body is `bl __dt__13CFontImageDefFv`
// with `li r4,-1`.  Unlike the two names above, **no unit in this tree claims the bytes** - retail's
// `__dt__13CFontImageDefFv` (0x80024D68, `symbols.txt:641`, 0x58 bytes, `scope:weak`) sits in the
// same unclaimed `auto_*` range as the carve, one function above it, so dtk's own object supplies
// them in the DOL link and this is only what a host link binds to.  The 16-bit second argument is
// MWCC's deleting-destructor flag, which the host's C++ destructor does not take; the only call site
// passes -1, so it is accepted and ignored and the work is `CFontImageDef`'s own destruction - the
// texture vector at `+0x04`, whose host destructor `Kyoto/Text/CFontImageDef.cpp` already
// instantiates as a weak symbol in this same link.
extern "C" void __dt__13CFontImageDefFv(void* self, short) {
  static_cast< CFontImageDef* >(self)->~CFontImageDef();
}

// Retail's own mangled name for `CTextRenderBuffer::SFontPalette::~SFontPalette()`, reached the
// same way.  `src/MetroidPrime/Carve80024F6C.c` (retail 0x80024F6C..0x80024FB0) is the unit that
// needs it: that is `rstl::destroy_impl< SFontPalette >`, whose whole body is
// `bl __dt__Q217CTextRenderBuffer12SFontPaletteFv` with `li r4,-1`.  Like
// `__dt__13CFontImageDefFv` above, **no unit in this tree claims the bytes** - retail's
// `__dt__Q217CTextRenderBuffer12SFontPaletteFv` (0x80024FB0, `symbols.txt:648`, 0xCC bytes,
// `scope:weak`) sits in the same unclaimed `auto_*` range as the carve, one function above it, so
// dtk's own object supplies them in the DOL link and this is only what a host link binds to.  The
// 16-bit second argument is MWCC's deleting-destructor flag, which the host's C++ destructor does
// not take; the only call site passes -1, so it is accepted and ignored and the work is the four
// `rstl::auto_ptr< CGraphicsPalette >` members' own destruction (`mHas`/`mItem`, 8 bytes each,
// `include/rstl/auto_ptr.hpp`), whose target destructor `Kyoto/Graphics/CGraphicsPalettePortStub.cpp`
// already defines for the host - so no further undefined symbol appears.
extern "C" void __dt__Q217CTextRenderBuffer12SFontPaletteFv(void* self, short) {
  static_cast< CTextRenderBuffer::SFontPalette* >(self)->~SFontPalette();
}

// Retail's own mangled name for `CHealthInfo`'s copy constructor, reached the same way.
// `src/MetroidPrime/Carve8016FD4C.c` (retail 0x8016FD4C..0x8016FD94) is the unit that needs it:
// that is `rstl::construct< CHealthInfo >`'s null-guarded half, whose whole body is a frame and
// `bl __ct__11CHealthInfoFRC11CHealthInfo` behind a `cmplwi r3,0x0`.  Unlike the two names above,
// **the bytes are claimed by a unit in this tree** - retail's copy constructor (0x80070D60,
// `symbols.txt:2082`, 0x54 bytes) sits inside `ScriptObjects/CScriptActor.cpp`'s claim
// (0x8006EB98..0x80070DB4), which is `NonMatching`, so dtk's own object supplies them in the DOL
// link; there is simply no host object for them, so this is only what a host link binds to.
// The body is that copy: the 0x20 bytes `include/MetroidPrime/CHealthInfo.hpp` declares
// (`CHECK_SIZEOF(CHealthInfo, 0x20)`), copied memberwise into `self`.  Nothing here is a stub -
// on `TARGET_PC` the class's copy is otherwise made inline by the compiler, as
// `MetroidPrime/Enemies/CAi.cpp` (which opts into the out-of-line declaration) does, and this
// gives the host link the one symbol the carve's `bl` names.
extern "C" void __ct__11CHealthInfoFRC11CHealthInfo(void* self, const void* src) {
  new (self) CHealthInfo(*static_cast< const CHealthInfo* >(src));
}

// The third of the same family, and the reason it is here rather than in the unit that needs it:
// the name is shared by every unit whose bytes call `CDamageVulnerability`'s out-of-line copy, and
// by 2026-10-02 exactly one does - `src/MetroidPrime/ScriptLoader/Carve80229EE8.cpp` (retail
// 0x80229EE8..0x80229F90), whose `CBasicSwarmData` constructor at +0x3C is a `bl` to it.  The
// bytes are retail's copy constructor at 0x8001C634 (`symbols.txt:510`, 0x5C bytes), inside
// `Player/CPlayer.cpp`'s claim (0x8000B8E8..0x8001D0CC), which is `NonMatching` - so dtk's object
// supplies them in the DOL link and this is only what a host link binds to.  The body is that
// copy - the class's own, which on `TARGET_PC` is the compiler's implicit one, because the
// out-of-line declaration is behind `MP_RETAIL_OUT_OF_LINE_COPIES`; the host's layout is not the
// card's (`sizeof(CDamageVulnerability)` is 0x38 there against `CHECK_SIZEOF`'s 0x30, `rstl::vector`
// holding pointers), which is why this copies through the class rather than memcpy-ing a size.
// Nothing here is a stub.
extern "C" void __ct__20CDamageVulnerabilityFRC20CDamageVulnerability(void* self, const void* src) {
  new (self) CDamageVulnerability(*static_cast< const CDamageVulnerability* >(src));
}

// Retail's own mangled name for `CActorModelParticles::CItem::~CItem()`, reached the same way, and
// the first of this family whose element type is a class of ours rather than a `CToken`.
// `src/MetroidPrime/Carve80046F88.c` (retail 0x80046F88..0x80047014) is the unit that needs it:
// that is `rstl::list< CActorModelParticles::CItem >::~list()`, whose per-node `rstl::destroy`
// (`include/rstl/list.hpp:257`) is a `bl __dt__Q220CActorModelParticles5CItemFv` with `li r4,-1`.
// Retail defines it (`symbols.txt:5580`, 0x8014F598, 0x238 bytes, claimed by
// `MetroidPrime/CActorModelParticles.cpp`), so the DOL link resolves it and this is only what a
// host link binds to.  The 16-bit second argument is MWCC's deleting-destructor flag, which the
// host's C++ destructor does not take; the only call site passes -1, so it is accepted and ignored
// and the work is `CItem`'s own destruction - the sfx emitter removal and the dependency walk at
// `src/MetroidPrime/CActorModelParticles.cpp:149`, whose members are already host objects in this
// same link.  Nothing here is a stub.
extern "C" void __dt__Q220CActorModelParticles5CItemFv(void* self, short) {
  static_cast< CActorModelParticles::CItem* >(self)->~CItem();
}

// Retail's own mangled name for `CActor::AddToRenderer(const CStateManager&) const`, reached the
// same way, and the only retail name of a *non*-constructor or destructor in this file.
// `src/MetroidPrime/Carve801708C4.c` (retail 0x801708C4..0x801708E4) is the unit that needs it:
// `AddToRenderer__27CElitePirateGrenadeLauncherCFRC13CStateManager` is vtable slot 8 of the DOL's `lbl_803B5500` - the slot
// `include/MetroidPrime/CActor.hpp:81` declares `AddToRenderer` in - and its whole body is a frame
// and one `bl` to this name, both argument registers forwarded, so the call cannot be dropped
// without losing the match. Retail's bytes are at 0x8004CB7C (`symbols.txt:1485`, 0x184 bytes),
// inside `MetroidPrime/CActor.cpp`'s claim (0x80049ED8..0x8004E84C), which is `NonMatching`, so
// dtk's object supplies them in the DOL link and this is only what a host link binds to. Nothing
// here is a stub: the body is the host's own `CActor::AddToRenderer`
// (`src/MetroidPrime/CActor.cpp:352`), called **qualified** so it is the base implementation the
// carve's `bl` names rather than a re-dispatch through whatever vtable `self` happens to carry -
// the retail symbol is the base one. Without the qualifier the host compiler would emit the same
// work under `_ZNK6CActor13AddToRendererERKN13CStateManagerE` and leave this name undefined
// anyway, since `CActor.cpp`'s own object is the only host definition of that method.
extern "C" void AddToRenderer__6CActorCFRC13CStateManager(const void* self, const void* mgr) {
  static_cast< const CActor* >(self)->CActor::AddToRenderer(
      *static_cast< const CStateManager* >(mgr));
}

// The same split for the next virtual in `CActor`'s list, reached the same way.
// `src/MetroidPrime/Carve80170958.c` (retail 0x80170958..0x80170984) is the unit that needs it:
// `Render__27CElitePirateGrenadeLauncherCFRC13CStateManager` is vtable slot 9 of the DOL's `lbl_803B5500` - the slot
// `include/MetroidPrime/CActor.hpp:82` declares `Render` in - and its whole body is a frame, a
// test of bit 6 of the flag byte at `+0x200`, and one `bl` to this name, both argument registers
// forwarded, so the call cannot be dropped without losing the match. Retail's bytes are at
// 0x8004C6E0 (`symbols.txt:1480`, 0x188 bytes), inside `MetroidPrime/CActor.cpp`'s claim
// (0x80049ED8..0x8004E84C), which is `NonMatching`, so dtk's object supplies them in the DOL
// link and this is only what a host link binds to. Nothing here is a stub, and like the definition
// above the call is **qualified**: it is the base implementation the carve's `bl` names rather than
// a re-dispatch through whatever vtable `self` happens to carry.
extern "C" void Render__6CActorCFRC13CStateManager(const void* self, const void* mgr) {
  static_cast< const CActor* >(self)->CActor::Render(*static_cast< const CStateManager* >(mgr));
}

// The other half of the same split. Retail's `operator delete` is `CMemory::Free`, and `Matching`
// units rely on it: `CResLoader::AddGroupCache` takes its buffer from `CMemory::Alloc` and gives it
// to an `rstl::auto_ptr`, whose `delete` on the host would hand a game-heap pointer to glibc
// (`munmap_chunk(): invalid pointer`, first hit when the memory card read its first MLVL). So the
// host's `operator delete` looks at the address: inside the arena the game heap was carved from
// (`platform/main.cpp` records it) it is CMemory's, anywhere else it is malloc's.
void* gPortGameHeapLo = nullptr;
void* gPortGameHeapHi = nullptr;

static inline void PortDelete(void* ptr) noexcept {
  if (ptr >= gPortGameHeapLo && ptr < gPortGameHeapHi) {
    CMemory::Free(ptr);
  } else {
    std::free(ptr);
  }
}

// Zero-filled `new` (sc/zero): retail's console heap is zeroed, the host's is not, so a member a
// constructor never touches read host-heap garbage (save-state diffs that varied run to run). Every
// global `new` - game objects, containers - returns zeroed memory; `calloc` takes fresh pages for
// free and memsets recycled chunks. Frees go through `PortDelete` unchanged.
static void* PortZeroNew(std::size_t n) {
  void* p = std::calloc(1, n ? n : 1);
  if (p == nullptr) {
    throw std::bad_alloc();
  }
  return p;
}
static void* PortZeroNewAligned(std::size_t n, std::size_t al) {
  if (al < sizeof(void*)) {
    al = sizeof(void*);
  }
  n = n ? n : 1;
#ifdef _WIN32
  void* p = _aligned_malloc(n, al);
#else
  void* p = std::aligned_alloc(al, (n + al - 1) & ~(al - 1));
#endif
  if (p == nullptr) {
    throw std::bad_alloc();
  }
  std::memset(p, 0, n);
  return p;
}
void* operator new(std::size_t n) { return PortZeroNew(n); }
void* operator new[](std::size_t n) { return PortZeroNew(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  return std::calloc(1, n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  return std::calloc(1, n ? n : 1);
}
void* operator new(std::size_t n, std::align_val_t a) {
  return PortZeroNewAligned(n, static_cast< std::size_t >(a));
}
void* operator new[](std::size_t n, std::align_val_t a) {
  return PortZeroNewAligned(n, static_cast< std::size_t >(a));
}

void operator delete(void* ptr) noexcept { PortDelete(ptr); }
void operator delete[](void* ptr) noexcept { PortDelete(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { PortDelete(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { PortDelete(ptr); }
// The aligned `operator new` above is `_aligned_malloc` on Windows, whose
// block `free` corrupts the heap (0xC0000374), so the aligned deletes give it to `_aligned_free`.
static inline void PortAlignedDelete(void* ptr) noexcept {
#ifdef _WIN32
  if (ptr >= gPortGameHeapLo && ptr < gPortGameHeapHi) {
    CMemory::Free(ptr);
  } else {
    _aligned_free(ptr);
  }
#else
  PortDelete(ptr);
#endif
}

void operator delete(void* ptr, std::align_val_t) noexcept { PortAlignedDelete(ptr); }
void operator delete[](void* ptr, std::align_val_t) noexcept { PortAlignedDelete(ptr); }
void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept { PortAlignedDelete(ptr); }
void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept { PortAlignedDelete(ptr); }
